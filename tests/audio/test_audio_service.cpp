#include "service/audio_service.h"
#include "audiocore/audio_backend.hpp"
#include "audiocore/audio_buffer.hpp"
#include "audiocore/audio_engine.hpp"
#include "audiocore/meter_node.hpp"

#include <cmath>
#include <iostream>
#include <string>

namespace {
class FailingBackend final : public audiocore::AudioBackend {
public:
    bool initialize(double, uint32_t, uint32_t) override { return false; }
    bool start() override { return false; }
    void stop() override {}
    bool isRunning() const override { return false; }
    double getSampleRate() const override { return 0.0; }
    uint32_t getBlockSize() const override { return 0; }
    uint32_t getChannels() const override { return 0; }
};

bool check(const char* name, bool condition) {
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << name << '\n';
    return condition;
}
}

int main() {
    bool ok = true;
    StereoDemoDspNode processor(48000.0, 128);
    audiocore::AudioBuffer input(2, 128), output(2, 128);
    for (uint32_t i = 0; i < 128; ++i) {
        input[0][i] = 0.25f;
        input[1][i] = 0.25f;
    }

    ok &= check("Gain command accepted", processor.setGainDb(-6.0f));
    processor.process(input, output, 128);
    const float expected = 0.25f * std::pow(10.0f, -6.0f / 20.0f) * 0.70710678f;
    ok &= check("Gain and center pan use the existing engine nodes",
                std::abs(output[0][64] - expected) < 0.002f &&
                std::abs(output[1][64] - expected) < 0.002f);

    ok &= check("Pan command accepted", processor.setPan(-1.0f));
    for (int block = 0; block < 8; ++block) processor.process(input, output, 128);
    ok &= check("Pan moves stereo image left", output[0][64] > 0.12f && std::abs(output[1][64]) < 1e-6f);

    ok &= check("DSP bypass command accepted", processor.setDspBypass(true));
    processor.process(input, output, 128);
    ok &= check("DSP bypass passes input unchanged", std::abs(output[0][64] - 0.25f) < 1e-6f &&
                std::abs(output[1][64] - 0.25f) < 1e-6f);

    ok &= check("PEQ enable command accepted", processor.setPeqEnabled(false));
    ok &= check("Compressor enable command accepted", processor.setCompressorEnabled(false));
    processor.process(input, output, 128);
    const auto requested = processor.requestedControls();
    const auto applied = processor.appliedControls();
    ok &= check("Requested state reports queued controls", requested.bypass && !requested.peqEnabled &&
                !requested.compressorEnabled && std::abs(requested.gainDb + 6.0f) < 1e-5f);
    ok &= check("Audio block applies bypass, PEQ, and compressor commands", applied.bypass &&
                !applied.peqEnabled && !applied.compressorEnabled && std::abs(applied.gainDb + 6.0f) < 1e-5f);

    StereoDemoDspNode bounded(48000.0, 128);
    bool firstRejected = false;
    for (int i = 0; i < 128; ++i) firstRejected = firstRejected || !bounded.setGainDb(-static_cast<float>(i));
    ok &= check("Control queue accepts its bounded capacity", !firstRejected);
    ok &= check("Control queue rejects overflow without blocking", !bounded.setGainDb(1.0f) &&
                bounded.droppedCommandCount() == 1);

    auto inputMeter = std::make_shared<audiocore::MeterNode>();
    auto outputMeter = std::make_shared<audiocore::MeterNode>();
    audiocore::AudioEngine meteredEngine;
    audiocore::AudioBuffer meteredOutput(2, 128);
    meteredEngine.configure(48000.0, 128, 2);
    auto& meterGraph = meteredEngine.getGraph();
    meterGraph.clearNodes();
    meterGraph.addNode(inputMeter);
    meterGraph.addNode(std::make_shared<StereoDemoDspNode>(48000.0, 128));
    meterGraph.addNode(outputMeter);
    meteredEngine.start();
    meteredEngine.process(input, meteredOutput, 128);
    meteredEngine.stop();
    ok &= check("Meter bridge reports actual input peak and RMS",
                std::abs(inputMeter->getPeak(0) - 0.25f) < 1e-5f &&
                std::abs(inputMeter->getRms(1) - 0.25f) < 1e-5f);
    ok &= check("Meter bridge reports processed output telemetry",
                outputMeter->getPeak(0) > 0.0f && outputMeter->getPeak(0) < inputMeter->getPeak(0));

    std::vector<AudioStripConfig> strips(2);
    strips[0].logicalId = 1;
    strips[0].captureChannel = 0;
    strips[0].name = "Vocal";
    strips[0].hpfEnabled = false;
    strips[0].gateEnabled = false;
    strips[0].peqBypassed = true;
    strips[0].fxRack.push_back({"http://audiocore.org/plugins/compressor", false,
        {{"thresh", -40.0f}, {"ratio", 10.0f}, {"attack", 1.0f}, {"release", 20.0f}, {"knee", 0.0f}}});
    strips[0].fxRack.push_back({"http://audiocore.org/plugins/delay", false,
        {{"time_l", 40.0f}, {"time_r", 55.0f}, {"feedback", 0.1f}, {"mix", 0.1f}}});
    strips[1].logicalId = 2;
    strips[1].captureChannel = 1;
    strips[1].name = "Guitar";
    strips[1].hpfEnabled = false;
    strips[1].gateEnabled = false;
    strips[1].compressorEnabled = false;
    strips[1].peqBypassed = true;
    audiocore::AudioBuffer multichannel(4, 128), processed(4, 128);
    for (uint32_t i = 0; i < 128; ++i) {
        for (uint32_t channel = 0; channel < 4; ++channel) multichannel[channel][i] = 0.8f;
    }
    StripInsertDspNode inserts(strips, 48000.0, 128);
    for (int block = 0; block < 24; ++block) inserts.process(multichannel, processed, 128);
    const auto stripMeters = inserts.telemetry();
    ok &= check("Each configured strip receives independent DSP and telemetry",
                stripMeters.size() == 2 && stripMeters[0].logicalId == 1 &&
                stripMeters[1].logicalId == 2 && stripMeters[0].inputPeak[0] > 0.79f &&
                stripMeters[0].outputPeak[0] < stripMeters[1].outputPeak[0] &&
                stripMeters[1].inputRms[1] > 0.79f);

    AudioStripConfig cleanStrip;
    cleanStrip.logicalId = 3;
    cleanStrip.hpfEnabled = false;
    cleanStrip.gateEnabled = false;
    cleanStrip.compressorEnabled = false;
    cleanStrip.peqBypassed = true;
    std::vector<AudioStripConfig> cleanConfig{cleanStrip};
    audiocore::AudioBuffer toneInput(2, 128), toneOutput(2, 128);
    for (uint32_t frame = 0; frame < 128; ++frame)
        toneInput[0][frame] = toneInput[1][frame] = 0.25f;
    StripInsertDspNode dryStrip(cleanConfig, 48000.0, 128);
    dryStrip.process(toneInput, toneOutput, 128);
    ok &= check("A strip with its processors bypassed passes its own input unchanged",
                std::abs(toneOutput[0][64] - 0.25f) < 1e-6f && std::abs(toneOutput[1][64] - 0.25f) < 1e-6f);

    auto hpfConfig = cleanConfig;
    hpfConfig[0].hpfEnabled = true;
    hpfConfig[0].hpfFrequency = 120.0f;
    StripInsertDspNode hpfStrip(hpfConfig, 48000.0, 128);
    for (int block = 0; block < 80; ++block) hpfStrip.process(toneInput, toneOutput, 128);
    ok &= check("The per-strip HPF removes DC independently of the master stereo chain",
                std::abs(toneOutput[0][64]) < 0.001f && std::abs(toneOutput[1][64]) < 0.001f);

    audiocore::AudioBuffer sineInput(2, 128), eqOutput(2, 128);
    for (uint32_t frame = 0; frame < 128; ++frame) {
        const float sample = 0.1f * std::sin(2.0f * 3.14159265358979323846f * 1000.0f * frame / 48000.0f);
        sineInput[0][frame] = sineInput[1][frame] = sample;
    }
    auto peqConfig = cleanConfig;
    peqConfig[0].peqBypassed = false;
    peqConfig[0].eqFrequency[1] = 1000.0f;
    peqConfig[0].eqGain[1] = 12.0f;
    peqConfig[0].eqQ[1] = 1.0f;
    StripInsertDspNode peqStrip(peqConfig, 48000.0, 128);
    for (int block = 0; block < 40; ++block) peqStrip.process(sineInput, eqOutput, 128);
    double peqEnergy = 0.0;
    for (uint32_t frame = 0; frame < 128; ++frame) peqEnergy += eqOutput[0][frame] * eqOutput[0][frame];
    auto geqConfig = cleanConfig;
    geqConfig[0].geqMode = true;
    geqConfig[0].geqGain[19] = 9.0f;
    StripInsertDspNode geqStrip(geqConfig, 48000.0, 128);
    for (int block = 0; block < 40; ++block) geqStrip.process(sineInput, eqOutput, 128);
    double geqEnergy = 0.0;
    for (uint32_t frame = 0; frame < 128; ++frame) geqEnergy += eqOutput[0][frame] * eqOutput[0][frame];
    ok &= check("Per-strip parametric and graphic EQ modes process their configured bands",
                peqEnergy > 0.01 && geqEnergy > 0.01);

    auto polarityConfig = cleanConfig;
    polarityConfig[0].polarityInverted = true;
    audiocore::AudioBuffer positiveInput(2, 128), invertedOutput(2, 128);
    for (uint32_t frame = 0; frame < 128; ++frame)
        positiveInput[0][frame] = positiveInput[1][frame] = 0.25f;
    StripInsertDspNode polarityStrip(polarityConfig, 48000.0, 128);
    polarityStrip.process(positiveInput, invertedOutput, 128);
    ok &= check("Polarity inversion is applied before the strip processor chain",
                std::abs(invertedOutput[0][64] + 0.25f) < 1e-6f &&
                std::abs(invertedOutput[1][64] + 0.25f) < 1e-6f);

    auto compConfig = cleanConfig;
    compConfig[0].compressorEnabled = true;
    compConfig[0].compressorThresholdDb = -30.0f;
    compConfig[0].compressorRatio = 12.0f;
    compConfig[0].compressorAttackMs = 0.2f;
    audiocore::AudioBuffer loudInput(2, 128), compOutput(2, 128);
    for (uint32_t frame = 0; frame < 128; ++frame)
        loudInput[0][frame] = loudInput[1][frame] = 0.8f;
    StripInsertDspNode compStrip(compConfig, 48000.0, 128);
    for (int block = 0; block < 30; ++block) compStrip.process(loudInput, compOutput, 128);
    ok &= check("Each strip's built-in compressor processes before bus summing",
                compOutput[0][64] < 0.5f && compStrip.telemetry()[0].gainReductionDb < -1.0f);

    auto gateConfig = cleanConfig;
    gateConfig[0].gateEnabled = true;
    gateConfig[0].gateThresholdDb = -20.0f;
    gateConfig[0].gateRangeDb = -60.0f;
    audiocore::AudioBuffer quietInput(2, 128), gateOutput(2, 128);
    for (uint32_t frame = 0; frame < 128; ++frame)
        quietInput[0][frame] = quietInput[1][frame] = 0.01f;
    StripInsertDspNode gateStrip(gateConfig, 48000.0, 128);
    for (int block = 0; block < 30; ++block) gateStrip.process(quietInput, gateOutput, 128);
    ok &= check("Each strip's gate has independent threshold and gain-reduction state",
                std::abs(gateOutput[0][64]) < 0.002f && gateStrip.telemetry()[0].gateGainReductionDb < -10.0f);

    auto transparentMaster = std::make_shared<StereoDemoDspNode>(48000.0, 128);
    transparentMaster->setDspBypass(true);
    StripMainMixDspNode mainMix(strips, transparentMaster, 2, 128);
    audiocore::AudioBuffer mixed(2, 128);
    mainMix.process(processed, mixed, 128);
    const float twoStripLevel = mixed[0][64];
    strips[0].solo = true;
    StripMainMixDspNode soloMix(strips, transparentMaster, 2, 128);
    soloMix.process(processed, mixed, 128);
    ok &= check("Main bus sums strips and solo isolates the selected strip",
                twoStripLevel > mixed[0][64] && mixed[0][64] > 0.001f && mixed[1][64] > 0.001f &&
                soloMix.outputMeter()->getPeak(0) > 0.0f);

    auto hardwareRoutes = strips;
    hardwareRoutes[0].solo = false;
    hardwareRoutes[1].solo = false;
    hardwareRoutes[0].outputRoute = AudioOutputRoute::Aux;
    hardwareRoutes[0].outputRouteIndex = 1;
    hardwareRoutes[1].outputRoute = AudioOutputRoute::Omni;
    hardwareRoutes[1].outputRouteIndex = 4;
    auto bypassMaster = std::make_shared<StereoDemoDspNode>(48000.0, 128);
    bypassMaster->setDspBypass(true);
    StripMainMixDspNode multiOut(hardwareRoutes, bypassMaster, 6, 128);
    audiocore::AudioBuffer multiOutput(6, 128);
    multiOut.process(processed, multiOutput, 128);
    ok &= check("AUX bus and Omni assignments reach their selected physical outputs",
                multiOutput[3][64] > 0.0f && multiOutput[4][64] > 0.0f &&
                std::abs(multiOutput[0][64]) < 1e-6f && std::abs(multiOutput[1][64]) < 1e-6f);

    AudioService service;
    ok &= check("Audio service defaults to explicitly labeled simulation mode",
                service.snapshot().mode == AudioMode::Simulation &&
                service.snapshot().state == AudioServiceState::Stopped);
    ok &= check("Real stream cannot start while simulation is selected", !service.startRealAudio());
    ok &= check("Backend selection changes only when stream is stopped",
                service.selectMode(AudioMode::RealAudio) &&
                service.snapshot().mode == AudioMode::RealAudio);
    ok &= check("Service accepts a unique pair of physical strip inputs",
                service.setConsoleConfiguration(strips));
    auto duplicateInputs = strips;
    duplicateInputs[1].captureChannel = duplicateInputs[0].captureChannel;
    ok &= check("Service rejects duplicate physical input assignments",
                !service.setConsoleConfiguration(duplicateInputs));
    auto unsupportedFx = strips;
    unsupportedFx[0].fxRack[0].uri = "http://audiocore.org/plugins/chorus";
    ok &= check("Service rejects effects without a real DSP implementation",
                !service.setConsoleConfiguration(unsupportedFx));

    audiocore::AudioEngine engine;
    engine.setBackend(std::make_shared<FailingBackend>());
    ok &= check("Engine reports backend initialization failure", !engine.configure(48000.0, 128, 2) &&
                engine.getState() == audiocore::EngineState::Error);

    // Test live strip DSP parameter update & channel isolation
    AudioStripConfig liveStripA, liveStripB;
    liveStripA.logicalId = 10;
    liveStripA.captureChannel = 0;
    liveStripA.hpfEnabled = false;
    liveStripA.peqBypassed = true;
    liveStripA.compressorEnabled = false;
    liveStripA.gateEnabled = false;

    liveStripB.logicalId = 20;
    liveStripB.captureChannel = 1;
    liveStripB.hpfEnabled = false;
    liveStripB.peqBypassed = true;
    liveStripB.compressorEnabled = false;
    liveStripB.gateEnabled = false;

    std::vector<AudioStripConfig> dualStrips{liveStripA, liveStripB};
    StripInsertDspNode dualNode(dualStrips, 48000.0, 128);
    audiocore::AudioBuffer dualInput(4, 128), dualOutput(4, 128);
    for (uint32_t i = 0; i < 128; ++i) {
        dualInput[0][i] = dualInput[1][i] = 0.5f; // Ch 10
        dualInput[2][i] = dualInput[3][i] = 0.5f; // Ch 20
    }
    dualNode.process(dualInput, dualOutput, 128);
    ok &= check("Dual strips initially pass flat audio without modification",
                std::abs(dualOutput[0][64] - 0.5f) < 1e-5f &&
                std::abs(dualOutput[2][64] - 0.5f) < 1e-5f);

    // Invert polarity on Strip A ONLY
    liveStripA.polarityInverted = true;
    dualNode.updateStripDsp(liveStripA);
    dualNode.process(dualInput, dualOutput, 128);
    ok &= check("Live parameter update changes Strip A without affecting Strip B (Channel Isolation)",
                std::abs(dualOutput[0][64] + 0.5f) < 1e-5f &&
                std::abs(dualOutput[2][64] - 0.5f) < 1e-5f);

    // Verify audio device selection persistence in AudioService snapshot
    ok &= check("selectCaptureDevice persists and reflects in snapshot while stopped",
                service.selectCaptureDevice("hw:1") && service.snapshot().captureDeviceId == "hw:1");
    ok &= check("selectRenderDevice persists and reflects in snapshot while stopped",
                service.selectRenderDevice("hw:1") && service.snapshot().renderDeviceId == "hw:1");

    // =========================================================================
    // 1. MONO vs STEREO CHANNEL FORMAT TESTS
    // =========================================================================
    {
        AudioStripConfig monoStrip;
        monoStrip.logicalId = 101;
        monoStrip.format = AudioStripFormat::Mono;
        monoStrip.captureChannel = 0;
        monoStrip.hpfEnabled = false;
        monoStrip.peqBypassed = true;
        monoStrip.compressorEnabled = false;
        monoStrip.gateEnabled = false;

        AudioStripConfig stereoStrip;
        stereoStrip.logicalId = 102;
        stereoStrip.format = AudioStripFormat::Stereo;
        stereoStrip.captureChannel = 1;
        stereoStrip.captureChannelRight = 2;
        stereoStrip.hpfEnabled = false;
        stereoStrip.peqBypassed = true;
        stereoStrip.compressorEnabled = false;
        stereoStrip.gateEnabled = false;

        std::vector<AudioStripConfig> fmtStrips{monoStrip, stereoStrip};
        StripInsertDspNode fmtNode(fmtStrips, 48000.0, 128);
        ok &= check("Mono strip allocates 1-channel buffer and Stereo strip allocates 2-channel buffer",
                    fmtNode.stripBufferChannels(0) == 1 && fmtNode.stripBufferChannels(1) == 2);

        audiocore::AudioBuffer fmtIn(4, 128), fmtOut(4, 128);
        for (uint32_t i = 0; i < 128; ++i) {
            fmtIn[0][i] = 0.4f;  // Mono strip L
            fmtIn[1][i] = 0.0f;  // Mono strip R ignored by 1-ch strip
            fmtIn[2][i] = 0.7f;  // Stereo strip L
            fmtIn[3][i] = -0.3f; // Stereo strip R
        }
        fmtNode.process(fmtIn, fmtOut, 128);
        ok &= check("Mono strip processes single mono path and duplicates to strip L/R bus feed",
                    std::abs(fmtOut[0][64] - 0.4f) < 1e-5f && std::abs(fmtOut[1][64] - 0.4f) < 1e-5f);
        ok &= check("Stereo strip preserves distinct L and R channel content",
                    std::abs(fmtOut[2][64] - 0.7f) < 1e-5f && std::abs(fmtOut[3][64] + 0.3f) < 1e-5f);

        // Pan vs Balance in StripMainMixDspNode
        auto masterPass = std::make_shared<StereoDemoDspNode>(48000.0, 128);
        masterPass->setDspBypass(true);

        // Mono pan hard right (+1.0)
        fmtStrips[0].pan = 1.0f;
        fmtStrips[1].muted = true;
        StripMainMixDspNode monoPanMix(fmtStrips, masterPass, 2, 128);
        audiocore::AudioBuffer panOut(2, 128);
        monoPanMix.process(fmtOut, panOut, 128);
        ok &= check("Mono strip constant-power pan hard right routes signal to Right bus only",
                    std::abs(panOut[0][64]) < 1e-5f && std::abs(panOut[1][64] - 0.4f) < 1e-4f);

        // Stereo balance left (-0.5 -> L=1.0, R=0.5)
        fmtStrips[0].muted = true;
        fmtStrips[1].muted = false;
        fmtStrips[1].pan = -0.5f;
        StripMainMixDspNode stereoBalMix(fmtStrips, masterPass, 2, 128);
        audiocore::AudioBuffer balOut(2, 128);
        stereoBalMix.process(fmtOut, balOut, 128);
        ok &= check("Stereo strip balance law preserves L and scales R proportionally",
                    std::abs(balOut[0][64] - 0.7f) < 1e-5f && std::abs(balOut[1][64] + 0.15f) < 1e-5f);

        // Conflict validation in AudioService: Stereo channel overlapping Mono channel's physical input
        ok &= check("AudioService accepts non-overlapping Mono (ch 0) + Stereo (ch 1,2) strips",
                    service.setConsoleConfiguration(fmtStrips));
        auto conflictingStereo = fmtStrips;
        conflictingStereo[1].captureChannel = 0; // overlaps monoStrip captureChannel 0
        conflictingStereo[1].captureChannelRight = 1;
        ok &= check("AudioService rejects Stereo strip whose L/R pair conflicts with an assigned input",
                    !service.setConsoleConfiguration(conflictingStereo));
    }

    // =========================================================================
    // 2. MASTER BUS DSP CHAIN & BRICKWALL LIMITER TESTS
    // =========================================================================
    {
        AudioStripConfig hotStripA, hotStripB;
        hotStripA.logicalId = 1;
        hotStripA.captureChannel = 0;
        hotStripA.gainDb = 6.0f; // ~2.0x gain
        hotStripA.hpfEnabled = false;
        hotStripA.peqBypassed = true;
        hotStripA.compressorEnabled = false;

        hotStripB.logicalId = 2;
        hotStripB.captureChannel = 1;
        hotStripB.gainDb = 6.0f;
        hotStripB.hpfEnabled = false;
        hotStripB.peqBypassed = true;
        hotStripB.compressorEnabled = false;

        std::vector<AudioStripConfig> hotStrips{hotStripA, hotStripB};
        auto masterPass = std::make_shared<StereoDemoDspNode>(48000.0, 128);
        masterPass->setDspBypass(true);
        StripMainMixDspNode masterMix(hotStrips, masterPass, 2, 128);

        AudioBusConfig masterCfg;
        masterCfg.logicalId = 0;
        masterCfg.format = AudioStripFormat::Stereo;
        masterCfg.gainDb = 0.0f;
        masterCfg.hpfEnabled = false;
        masterCfg.gateEnabled = false;
        masterCfg.peqBypassed = true;
        masterCfg.compressorEnabled = false;
        masterCfg.limiterEnabled = true;
        masterCfg.limiterCeilingDb = -6.0f; // Ceiling at ~0.501187 linear
        masterCfg.limiterReleaseMs = 50.0f;
        masterMix.updateMasterBusConfig(masterCfg);

        audiocore::AudioBuffer hotIn(4, 128), limitedOut(2, 128);
        for (uint32_t i = 0; i < 128; ++i) {
            hotIn[0][i] = hotIn[1][i] = 0.6f;
            hotIn[2][i] = hotIn[3][i] = 0.6f;
        }
        for (int b = 0; b < 8; ++b) masterMix.process(hotIn, limitedOut, 128);

        const float ceilingLin = std::pow(10.0f, -6.0f / 20.0f);
        const auto mTelem = masterMix.masterTelemetry();
        const float prePeak = mTelem.preLimiterPeak[0];
        const float postPeak = masterMix.outputMeter()->getPeak(0);
        const float limGr = mTelem.limiterGainReductionDb;
        ok &= check("Master brickwall limiter clamps hot summed bus to ceiling and reports Pre/Post/GR telemetry",
                    prePeak > 1.2f && postPeak <= ceilingLin + 0.01f && limGr < -6.0f);

        // Bypass limiter -> output rises back to unattenuated preLimiterPeak
        masterCfg.limiterEnabled = false;
        masterMix.updateMasterBusConfig(masterCfg);
        masterMix.process(hotIn, limitedOut, 128);
        ok &= check("Bypassing Master Limiter restores unattenuated master output and 0 dB GR",
                    masterMix.outputMeter()->getPeak(0) > 1.2f &&
                    std::abs(masterMix.masterTelemetry().limiterGainReductionDb) < 1e-4f);
    }

    // =========================================================================
    // 3. AUX BUS SENDS (PRE/POST-FADER), MONO DOWNMIX & SEND/RETURN WET FX TESTS
    // =========================================================================
    {
        AudioStripConfig chStrip;
        chStrip.logicalId = 1;
        chStrip.format = AudioStripFormat::Stereo;
        chStrip.captureChannel = 0;
        chStrip.captureChannelRight = 1;
        chStrip.gainDb = 0.0f;
        chStrip.hpfEnabled = false;
        chStrip.gateEnabled = false;
        chStrip.peqBypassed = true;
        chStrip.compressorEnabled = false;
        chStrip.routeToMain = false; // Isolate AUX bus contribution on Main L/R
        chStrip.auxSendLevel = {0.0f, 0.0f, 0.0f, 0.0f};
        chStrip.auxSendEnabled = {true, true, true, true};
        chStrip.auxSendPreFader = {true, false, false, false}; // AUX 1 = Pre-Fader, AUX 2 = Post-Fader

        std::vector<AudioStripConfig> auxTestStrips{chStrip};
        auto masterPass = std::make_shared<StereoDemoDspNode>(48000.0, 128);
        masterPass->setDspBypass(true);
        StripMainMixDspNode auxMix(auxTestStrips, masterPass, 2, 128);

        // Configure AUX 1 as Mono bus, AUX 2 as Stereo bus
        AudioBusConfig aux1Cfg, aux2Cfg;
        aux1Cfg.logicalId = 101;
        aux1Cfg.format = AudioStripFormat::Mono;
        aux1Cfg.gainDb = 0.0f;
        aux1Cfg.returnGainDb = 0.0f;
        aux1Cfg.routeToMain = false;
        aux1Cfg.hpfEnabled = false;
        aux1Cfg.gateEnabled = false;
        aux1Cfg.peqBypassed = true;
        aux1Cfg.compressorEnabled = false;
        aux1Cfg.limiterEnabled = false;

        aux2Cfg.logicalId = 102;
        aux2Cfg.format = AudioStripFormat::Stereo;
        aux2Cfg.gainDb = 0.0f;
        aux2Cfg.returnGainDb = 0.0f;
        aux2Cfg.routeToMain = false;
        aux2Cfg.hpfEnabled = false;
        aux2Cfg.gateEnabled = false;
        aux2Cfg.peqBypassed = true;
        aux2Cfg.compressorEnabled = false;
        aux2Cfg.limiterEnabled = false;

        auxMix.updateAuxBusConfig(0, aux1Cfg);
        auxMix.updateAuxBusConfig(1, aux2Cfg);

        audiocore::AudioBuffer stIn(2, 128), mainOut(2, 128);
        for (uint32_t i = 0; i < 128; ++i) {
            stIn[0][i] = 0.8f; // L
            stIn[1][i] = 0.2f; // R -> Mono downmix = 0.5 * (0.8 + 0.2) = 0.5
        }

        // Send = 0 -> AUX 1 and AUX 2 are silent
        auxMix.process(stIn, mainOut, 128);
        auto auxTelem0 = auxMix.auxTelemetry();
        ok &= check("Channel with 0 AUX send level produces silence in AUX buses",
                    auxTelem0[0].outputPeak[0] < 1e-6f && auxTelem0[1].outputPeak[0] < 1e-6f);

        // Turn up AUX 1 (Pre-Fader, Mono) and AUX 2 (Post-Fader, Stereo) to 1.0, while strip fader is -60 dB (silent)
        chStrip.gainDb = -60.0f;
        chStrip.auxSendLevel[0] = 1.0f; // Pre-Fader -> should still receive full signal (downmixed to 0.5 mono)
        chStrip.auxSendLevel[1] = 1.0f; // Post-Fader -> should be attenuated by -60 dB strip fader
        auxMix.updateStripConfig(chStrip);
        auxMix.updateStripByLogicalId(1, -60.0f, 0.0f, false, false);
        auxMix.process(stIn, mainOut, 128);
        auto auxTelemPrePost = auxMix.auxTelemetry();
        ok &= check("Pre-fader AUX send is unaffected by strip fader and downmixes Stereo->Mono (0.5*(L+R))",
                    std::abs(auxTelemPrePost[0].outputPeak[0] - 0.5f) < 0.01f &&
                    std::abs(auxTelemPrePost[0].outputPeak[1] - 0.5f) < 0.01f);
        ok &= check("Post-fader AUX send follows strip fader attenuation",
                    auxTelemPrePost[1].outputPeak[0] < 0.002f);

        // Bring strip fader to 0 dB -> Post-Fader AUX 2 now receives full stereo L=0.8, R=0.2
        chStrip.gainDb = 0.0f;
        auxMix.updateStripConfig(chStrip);
        auxMix.updateStripByLogicalId(1, 0.0f, 0.0f, false, false);
        auxMix.process(stIn, mainOut, 128);
        auto auxTelemFull = auxMix.auxTelemetry();
        ok &= check("Post-fader Stereo AUX bus preserves L/R separation when strip fader is 0 dB",
                    std::abs(auxTelemFull[1].outputPeak[0] - 0.8f) < 0.01f &&
                    std::abs(auxTelemFull[1].outputPeak[1] - 0.2f) < 0.01f);

        // Send/Return Wet-Only Mode on AUX 2: add Reverb with mix=0.0 -> output is 0 (no duplicate dry signal!)
        for (uint32_t i = 0; i < 128; ++i) {
            const float s = 0.7f * std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 48000.0f);
            stIn[0][i] = s;
            stIn[1][i] = s;
        }
        aux2Cfg.routeToMain = true;
        aux2Cfg.fxRack.push_back({"http://audiocore.org/plugins/reverb", false,
            {{"room_size", 0.7f}, {"damping", 0.4f}, {"mix", 0.0f}}});
        auxMix.updateAuxBusConfig(1, aux2Cfg);
        auxMix.process(stIn, mainOut, 128);
        ok &= check("AUX Send/Return Reverb with mix=0 outputs zero dry signal (prevents duplicate dry path)",
                    auxMix.auxTelemetry()[1].outputPeak[0] < 1e-5f &&
                    std::abs(mainOut[0][64]) < 1e-5f);

        // Increase AUX 2 Reverb mix to 1.0 -> wet reverb tail returns to Main L/R!
        aux2Cfg.fxRack[0].parameters[2].value = 1.0f;
        auxMix.updateAuxBusConfig(1, aux2Cfg);
        for (int b = 0; b < 24; ++b) auxMix.process(stIn, mainOut, 128);
        ok &= check("AUX Send/Return Reverb with mix=1.0 generates wet return into Main L/R",
                    auxMix.auxTelemetry()[1].outputPeak[0] > 0.005f &&
                    auxMix.outputMeter()->getPeak(0) > 0.005f &&
                    auxMix.outputMeter()->getPeak(1) > 0.005f);
    }

    // =========================================================================
    // 4. LIVE END-TO-END EFX LIFECYCLE TESTS (ADD / PARAM EDIT / BYPASS / REMOVE)
    // =========================================================================
    {
        AudioStripConfig fxStrip;
        fxStrip.logicalId = 7;
        fxStrip.format = AudioStripFormat::Stereo;
        fxStrip.captureChannel = 0;
        fxStrip.captureChannelRight = 1;
        fxStrip.hpfEnabled = false;
        fxStrip.peqBypassed = true;
        fxStrip.compressorEnabled = false;
        fxStrip.gateEnabled = false;

        std::vector<AudioStripConfig> fxStrips{fxStrip};
        StripInsertDspNode liveFxNode(fxStrips, 48000.0, 128);
        audiocore::AudioBuffer fxIn(2, 128), fxOut(2, 128);
        for (uint32_t i = 0; i < 128; ++i) {
            fxIn[0][i] = fxIn[1][i] = 0.8f;
        }

        // Step A: Initially empty rack -> output equals 0.8
        liveFxNode.process(fxIn, fxOut, 128);
        ok &= check("EFX Lifecycle Step 1: Empty rack passes dry signal (0.8)",
                    std::abs(fxOut[0][64] - 0.8f) < 1e-5f);

        // Step B: Dynamically add a Limiter FX at -12 dB ceiling (~0.251 linear) while live
        fxStrip.fxRack.push_back({"http://audiocore.org/plugins/limiter", false,
            {{"ceiling", -12.0f}, {"release", 5.0f}}});
        liveFxNode.updateStripDsp(fxStrip);
        for (int b = 0; b < 8; ++b) liveFxNode.process(fxIn, fxOut, 128);
        const float ceil12 = std::pow(10.0f, -12.0f / 20.0f);
        ok &= check("EFX Lifecycle Step 2: Dynamically added Limiter FX attenuates signal to -12 dB",
                    std::abs(fxOut[0][64] - ceil12) < 0.01f);

        // Step C: Dynamically update Limiter parameter (ceiling from -12 dB to -6 dB ~0.501 linear)
        fxStrip.fxRack[0].parameters[0].value = -6.0f;
        liveFxNode.updateStripDsp(fxStrip);
        for (int b = 0; b < 20; ++b) liveFxNode.process(fxIn, fxOut, 128);
        const float ceil6 = std::pow(10.0f, -6.0f / 20.0f);
        ok &= check("EFX Lifecycle Step 3: Live parameter update (-12 dB -> -6 dB) changes audio output",
                    std::abs(fxOut[0][64] - ceil6) < 0.01f);

        // Step D: Dynamically bypass the effect -> dry signal (0.8) restored
        fxStrip.fxRack[0].bypassed = true;
        liveFxNode.updateStripDsp(fxStrip);
        liveFxNode.process(fxIn, fxOut, 128);
        ok &= check("EFX Lifecycle Step 4: Bypassing effect restores dry signal (0.8)",
                    std::abs(fxOut[0][64] - 0.8f) < 1e-5f);

        // Step E: Unbypass, then dynamically remove the effect -> dry signal (0.8) restored without crash
        fxStrip.fxRack[0].bypassed = false;
        liveFxNode.updateStripDsp(fxStrip);
        liveFxNode.process(fxIn, fxOut, 128);
        fxStrip.fxRack.clear();
        liveFxNode.updateStripDsp(fxStrip);
        liveFxNode.process(fxIn, fxOut, 128);
        ok &= check("EFX Lifecycle Step 5: Removing effect while live restores dry signal cleanly",
                    std::abs(fxOut[0][64] - 0.8f) < 1e-5f);
    }

    // =========================================================================
    // 5. REVERB, DELAY & AFS RACK DSP, ISOLATION & TELEMETRY TESTS
    // =========================================================================
    {
        AudioStripConfig ch1, ch2;
        ch1.logicalId = 1;
        ch1.format = AudioStripFormat::Stereo;
        ch1.captureChannel = 0;
        ch1.captureChannelRight = 1;
        ch1.hpfEnabled = false;
        ch1.peqBypassed = true;
        ch1.compressorEnabled = false;
        ch1.gateEnabled = false;

        ch2 = ch1;
        ch2.logicalId = 2;
        ch2.captureChannel = 2;
        ch2.captureChannelRight = 3;

        // Assign AFS to Ch 1 only; Ch 2 has empty rack
        ch1.fxRack.push_back({"http://audiocore.org/plugins/afs", false,
            {{"sensitivity", 0.85f},
             {"max_notches", 8.0f},
             {"max_depth", -18.0f},
             {"q", 15.0f},
             {"hold_ms", 2000.0f},
             {"release_ms", 1500.0f},
             {"clear_seq", 0.0f}}});

        std::vector<AudioStripConfig> afsStrips{ch1, ch2};
        StripInsertDspNode afsNode(afsStrips, 48000.0, 128);
        audiocore::AudioBuffer inBuf(4, 128), outBuf(4, 128);

        // Step A: Quiet/no feedback signal -> AFS reports MONITORING and 0 active notches
        for (uint32_t i = 0; i < 128; ++i) {
            inBuf[0][i] = inBuf[1][i] = 0.0f;
            inBuf[2][i] = inBuf[3][i] = 0.5f;
        }
        afsNode.process(inBuf, outBuf, 128);
        auto telemQuiet = afsNode.telemetry();
        ok &= check("AFS Telemetry: Ch 1 has AFS assigned in MONITORING state with 0 active notches; Ch 2 is unassigned",
                    telemQuiet.size() == 2 &&
                    telemQuiet[0].afsAssigned &&
                    telemQuiet[0].afsState == 2 &&
                    telemQuiet[0].afsActiveNotchCount == 0 &&
                    !telemQuiet[1].afsAssigned);

        // Step B: Feed sustained 2000 Hz narrowband feedback tone into Ch 1 and Ch 2
        double phase = 0.0;
        const double phaseInc = 2.0 * 3.14159265358979323846 * 2000.0 / 48000.0;
        for (int block = 0; block < 180; ++block) {
            for (uint32_t i = 0; i < 128; ++i) {
                const float s = 0.5f * static_cast<float>(std::sin(phase));
                phase += phaseInc;
                inBuf[0][i] = inBuf[1][i] = s;
                inBuf[2][i] = inBuf[3][i] = s;
            }
            afsNode.process(inBuf, outBuf, 128);
            if ((block % 8) == 7) {
                (void)afsNode.telemetry(); // Drain FFT analysis window on non-RT polling path
            }
        }
        auto telemFb = afsNode.telemetry();
        double ch1Energy = 0.0;
        double ch2Energy = 0.0;
        for (uint32_t i = 0; i < 128; ++i) {
            ch1Energy += outBuf[0][i] * outBuf[0][i];
            ch2Energy += outBuf[2][i] * outBuf[2][i];
        }
        ok &= check("AFS Detection & Suppression: Sustained 2 kHz tone triggers active notch near 2 kHz on Ch 1 only",
                    telemFb[0].afsAssigned &&
                    telemFb[0].afsActiveNotchCount >= 1 &&
                    std::abs(telemFb[0].afsNotches[0].frequencyHz - 2000.0f) < 60.0f &&
                    telemFb[0].afsNotches[0].depthDb < -3.0f &&
                    ch1Energy < ch2Energy * 0.35 &&
                    std::abs(telemFb[1].outputPeak[0] - 0.5f) < 0.02f);

        // Step C: Increment clear_seq on Ch 1 AFS -> clears active notches in real time
        ch1.fxRack[0].parameters[6].value = 1.0f; // clear_seq = 1.0
        afsNode.updateStripDsp(ch1);
        for (uint32_t i = 0; i < 128; ++i) {
            inBuf[0][i] = inBuf[1][i] = 0.0f;
        }
        afsNode.process(inBuf, outBuf, 128);
        auto telemCleared = afsNode.telemetry();
        ok &= check("AFS Clear Notches: Incrementing clear_seq resets active notch count to 0",
                    telemCleared[0].afsActiveNotchCount == 0);

        // Step D: Test Extended Delay (Tempo Sync 120 BPM Quarter Note = 500 ms, Link L/R) & Extended Reverb
        ch1.fxRack.clear();
        ch1.fxRack.push_back({"http://audiocore.org/plugins/delay", false,
            {{"time_l", 320.0f},
             {"time_r", 480.0f},
             {"feedback", 0.4f},
             {"tone", 6000.0f},
             {"mix", 0.5f},
             {"low_cut", 120.0f},
             {"width", 1.0f},
             {"sync_en", 1.0f},
             {"bpm", 120.0f},
             {"sync_div", 2.0f},
             {"link_lr", 1.0f}}});
        ch1.fxRack.push_back({"http://audiocore.org/plugins/reverb", false,
            {{"room_size", 0.78f},
             {"decay", 2.8f},
             {"predelay", 24.0f},
             {"damping", 0.35f},
             {"width", 1.0f},
             {"mix", 0.35f},
             {"low_cut", 100.0f},
             {"high_cut", 8500.0f},
             {"diffusion", 0.78f},
             {"room_type", 2.0f}}});
        afsNode.updateStripDsp(ch1);
        for (uint32_t i = 0; i < 128; ++i) {
            const float s = (i < 32) ? 0.6f : 0.0f;
            inBuf[0][i] = inBuf[1][i] = s;
            inBuf[2][i] = inBuf[3][i] = 0.4f;
        }
        for (int b = 0; b < 16; ++b) {
            afsNode.process(inBuf, outBuf, 128);
            for (uint32_t i = 0; i < 128; ++i) inBuf[0][i] = inBuf[1][i] = 0.0f;
        }
        ok &= check("Extended Delay + Reverb chain processes cleanly on Ch 1 while Ch 2 remains isolated",
                    std::abs(outBuf[2][64] - 0.4f) < 1e-5f);
    }

    // =========================================================================
    // 6. DEDICATED FX RACK INPUT & OUTPUT TAP POINT NUMERICAL VALIDATION
    // =========================================================================
    {
        AudioStripConfig monoCh, stereoCh;
        monoCh.logicalId = 1;
        monoCh.format = AudioStripFormat::Mono;
        monoCh.captureChannel = 0;
        monoCh.hpfEnabled = false;
        monoCh.peqBypassed = true;
        monoCh.compressorEnabled = false;
        monoCh.gateEnabled = false;

        stereoCh.logicalId = 2;
        stereoCh.format = AudioStripFormat::Stereo;
        stereoCh.captureChannel = 1;
        stereoCh.captureChannelRight = 2;
        stereoCh.hpfEnabled = false;
        stereoCh.peqBypassed = true;
        stereoCh.compressorEnabled = false;
        stereoCh.gateEnabled = false;

        std::vector<AudioStripConfig> tapStrips{monoCh, stereoCh};
        StripInsertDspNode tapNode(tapStrips, 48000.0, 128);
        audiocore::AudioBuffer inBuf(4, 128), outBuf(4, 128);

        // 6.1 Silence Test: Zero input -> fxInputPeak == 0, fxOutputPeak == 0
        inBuf.clear();
        tapNode.process(inBuf, outBuf, 128);
        auto tSilence = tapNode.telemetry();
        ok &= check("FX Tap Silence Test: Zero PCM produces 0.0f (-90 dBFS) on PRE-FX and POST-FX L/R",
                    tSilence[0].fxInputPeak[0] == 0.0f && tSilence[0].fxOutputPeak[0] == 0.0f &&
                    tSilence[1].fxInputPeak[0] == 0.0f && tSilence[1].fxInputPeak[1] == 0.0f &&
                    tSilence[1].fxOutputPeak[0] == 0.0f && tSilence[1].fxOutputPeak[1] == 0.0f);

        // 6.2 Known Sine Wave & Mono vs Stereo Non-Fabrication Test
        // Feed 1 kHz sine (amp = 0.5 -> -6.02 dBFS peak, 0.35355 RMS) on Mono Ch 1 (strip index 0 -> ch 0,1),
        // and asymmetric L=0.6, R=0.25 sine on Stereo Ch 2 (strip index 1 -> ch 2,3).
        for (uint32_t i = 0; i < 128; ++i) {
            const float s = std::sin(2.0f * 3.14159265358979323846f * 1000.0f * static_cast<float>(i) / 48000.0f);
            inBuf[0][i] = 0.5f * s;  // Mono Ch 1 L
            inBuf[1][i] = 0.5f * s;  // Mono Ch 1 R (ignored by Mono strip)
            inBuf[2][i] = 0.6f * s;  // Stereo Ch 2 L
            inBuf[3][i] = 0.25f * s; // Stereo Ch 2 R
        }
        tapNode.process(inBuf, outBuf, 128);
        auto tSine = tapNode.telemetry();
        ok &= check("FX Tap Known Sine Test (Empty/Unity Rack): PRE-FX and POST-FX match 0.5f peak & ~0.3535f RMS",
                    std::abs(tSine[0].fxInputPeak[0] - 0.5f) < 0.005f &&
                    std::abs(tSine[0].fxOutputPeak[0] - tSine[0].fxInputPeak[0]) < 1e-6f &&
                    std::abs(tSine[0].fxInputRms[0] - (0.5f / std::sqrt(2.0f))) < 0.015f);
        ok &= check("FX Tap Mono Non-Fabrication Test: Mono channel never fabricates Right-channel fxInput/fxOutput",
                    tSine[0].fxInputPeak[1] == 0.0f && tSine[0].fxInputRms[1] == 0.0f &&
                    tSine[0].fxOutputPeak[1] == 0.0f && tSine[0].fxOutputRms[1] == 0.0f);
        ok &= check("FX Tap Stereo Independence Test: Stereo channel measures L (0.6) and R (0.25) independently",
                    std::abs(tSine[1].fxInputPeak[0] - 0.6f) < 0.006f &&
                    std::abs(tSine[1].fxInputPeak[1] - 0.25f) < 0.004f &&
                    std::abs(tSine[1].fxOutputPeak[0] - 0.6f) < 0.006f &&
                    std::abs(tSine[1].fxOutputPeak[1] - 0.25f) < 0.004f);

        // 6.3 Effect Processing Difference & Bypass Test (Limiter at -12 dB ceiling ~0.251 linear)
        stereoCh.fxRack.push_back({"http://audiocore.org/plugins/limiter", false,
            {{"ceiling", -12.0f}, {"release", 10.0f}}});
        tapNode.updateStripDsp(stereoCh);
        for (int b = 0; b < 8; ++b) tapNode.process(inBuf, outBuf, 128);
        auto tLim = tapNode.telemetry();
        const float ceil12Lin = std::pow(10.0f, -12.0f / 20.0f);
        ok &= check("FX Tap Processing Difference: Limiter FX reduces POST-FX peak to -12 dB while PRE-FX stays at 0.6",
                    std::abs(tLim[1].fxInputPeak[0] - 0.6f) < 0.006f &&
                    std::abs(tLim[1].fxOutputPeak[0] - ceil12Lin) < 0.01f &&
                    tLim[1].fxOutputPeak[0] < tLim[1].fxInputPeak[0] * 0.5f);

        // Toggle Bypass -> POST-FX matches PRE-FX again
        stereoCh.fxRack[0].bypassed = true;
        tapNode.updateStripDsp(stereoCh);
        tapNode.process(inBuf, outBuf, 128);
        auto tByp = tapNode.telemetry();
        ok &= check("FX Tap Bypass Test: Bypassing FX restores POST-FX peak == PRE-FX peak (0.6)",
                    std::abs(tByp[1].fxOutputPeak[0] - tByp[1].fxInputPeak[0]) < 1e-6f);

        // 6.4 Reverb Wet Tail & Delay Echo Output Tests (Signal in POST-FX when PRE-FX drops to 0)
        stereoCh.fxRack.clear();
        stereoCh.fxRack.push_back({"http://audiocore.org/plugins/reverb", false,
            {{"decay", 3.0f}, {"predelay", 0.0f}, {"mix", 1.0f}}});
        tapNode.updateStripDsp(stereoCh);
        for (int b = 0; b < 12; ++b) tapNode.process(inBuf, outBuf, 128);
        inBuf.clear(); // Cut input to silence
        tapNode.process(inBuf, outBuf, 128);
        auto tRevTail = tapNode.telemetry();
        ok &= check("FX Tap Reverb Tail Test: When input cuts to silence, PRE-FX is 0.0 while POST-FX measures wet reverb tail",
                    tRevTail[1].fxInputPeak[0] == 0.0f && tRevTail[1].fxOutputPeak[0] > 0.01f);

        // 6.5 Downstream Fader Isolation Test on AUX & Master Bus FX Taps
        AudioStripConfig sendCh = stereoCh;
        sendCh.fxRack.clear();
        sendCh.gainDb = 0.0f;
        sendCh.routeToMain = true;
        sendCh.auxSendEnabled = {true, false, false, false};
        sendCh.auxSendPreFader = {true, false, false, false};
        sendCh.auxSendLevel = {1.0f, 0.0f, 0.0f, 0.0f};

        auto masterPass = std::make_shared<StereoDemoDspNode>(48000.0, 128);
        masterPass->setDspBypass(true);
        StripMainMixDspNode busMix({sendCh}, masterPass, 2, 128);

        AudioBusConfig aux1Bus, masterBus;
        aux1Bus.logicalId = 101;
        aux1Bus.format = AudioStripFormat::Stereo;
        aux1Bus.gainDb = -60.0f; // Pull AUX 1 bus output fader down to -60 dB!
        aux1Bus.returnGainDb = 0.0f;
        aux1Bus.routeToMain = false;
        aux1Bus.hpfEnabled = false;
        aux1Bus.gateEnabled = false;
        aux1Bus.peqBypassed = true;
        aux1Bus.compressorEnabled = false;
        aux1Bus.limiterEnabled = false;
        aux1Bus.fxRack.push_back({"http://audiocore.org/plugins/limiter", false,
            {{"ceiling", -12.0f}, {"release", 10.0f}}});

        masterBus = aux1Bus;
        masterBus.logicalId = 100;
        masterBus.gainDb = -60.0f; // Pull Master bus fader down to -60 dB!

        busMix.updateAuxBusConfig(0, aux1Bus);
        busMix.updateMasterBusConfig(masterBus);

        audiocore::AudioBuffer stBusIn(2, 128), stBusOut(2, 128);
        for (uint32_t i = 0; i < 128; ++i) {
            stBusIn[0][i] = stBusIn[1][i] = 0.6f;
        }
        for (int b = 0; b < 8; ++b) busMix.process(stBusIn, stBusOut, 128);
        auto auxBusTelem = busMix.auxTelemetry()[0];
        auto mstBusTelem = busMix.masterTelemetry();
        ok &= check("FX Tap Bus Isolation Test: AUX & Master FX taps measure pre-FX (0.6) and post-FX (-12 dB) before -60 dB bus fader",
                    std::abs(auxBusTelem.fxInputPeak[0] - 0.6f) < 0.01f &&
                    std::abs(auxBusTelem.fxOutputPeak[0] - ceil12Lin) < 0.01f &&
                    auxBusTelem.outputPeak[0] < 0.001f &&
                    std::abs(mstBusTelem.fxInputPeak[0] - 0.6f) < 0.01f &&
                    std::abs(mstBusTelem.fxOutputPeak[0] - ceil12Lin) < 0.01f &&
                    mstBusTelem.outputPeak[0] < 0.001f);
    }

    return ok ? 0 : 1;
}
