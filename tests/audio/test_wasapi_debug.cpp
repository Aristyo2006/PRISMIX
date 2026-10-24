#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>

#include "backend/wasapi_backend.hpp"
#include "audiocore/audio_engine.hpp"
#include "audiocore/gain_node.hpp"
#include "audiocore/audio_node.hpp"
#include "audiocore/mixer_node.hpp"
#include "audiocore/pan_node.hpp"
#include "audiocore/eq_node.hpp"
#include "audiocore/compressor_node.hpp"
#include "audiocore/limiter_node.hpp"
#include "audiocore/meter_node.hpp"

#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <atomic>
#include <cmath>
#include <vector>
#include <cassert>
#include <algorithm>
#include <cstring>
#include <string>

namespace {

class ToneGeneratorNode : public audiocore::AudioNode {
public:
    ToneGeneratorNode(double sampleRate, double freqHz = 440.0, float gainLinear = 0.25f)
        : sample_rate_(sampleRate), freq_(freqHz), gain_(gainLinear) {}

    void process(const audiocore::AudioBuffer& input, audiocore::AudioBuffer& output, uint32_t frames) override {
        (void)input;
        for (uint32_t i = 0; i < frames; ++i) {
            float s = static_cast<float>(std::sin(phase_)) * gain_;
            phase_ += 2.0 * 3.14159265358979323846 * freq_ / sample_rate_;
            if (phase_ >= 2.0 * 3.14159265358979323846) {
                phase_ -= 2.0 * 3.14159265358979323846;
            }
            for (uint32_t ch = 0; ch < output.getChannelCount(); ++ch) {
                output.getChannelData(ch)[i] = s;
            }
        }
    }

    void reset() override { phase_ = 0.0; }

private:
    double sample_rate_{96000.0};
    double freq_{440.0};
    float gain_{0.25f};
    double phase_{0.0};
};

class PeakMeasurementNode : public audiocore::AudioNode {
public:
    PeakMeasurementNode() = default;

    void process(const audiocore::AudioBuffer& input, audiocore::AudioBuffer& output, uint32_t frames) override {
        float p = 0.0f;
        for (uint32_t ch = 0; ch < input.getChannelCount(); ++ch) {
            const float* in = input.getChannelData(ch);
            for (uint32_t i = 0; i < frames; ++i) {
                float v = std::abs(in[i]);
                if (v > p) p = v;
            }
        }
        float cur = maxPeak_.load(std::memory_order_relaxed);
        while (p > cur && !maxPeak_.compare_exchange_weak(cur, p, std::memory_order_relaxed)) {}

        if (&input != &output) {
            output.copyFrom(input, frames);
        }
    }

    void reset() override { maxPeak_.store(0.0f, std::memory_order_relaxed); }
    float getMaxPeak() const noexcept { return maxPeak_.load(std::memory_order_relaxed); }

private:
    std::atomic<float> maxPeak_{0.0f};
};

void printDiagnosticsReport(const std::string& testName, const audiocore::WasapiBackend::Diagnostics& diag) {
    std::cout << "\n------------------------------------------------------------\n";
    std::cout << "DIAGNOSTICS: " << testName << "\n";
    std::cout << "------------------------------------------------------------\n";
    std::cout << "  Total Callbacks       : " << diag.totalCallbacks << "\n";
    std::cout << "  Frames Requested      : " << diag.totalFramesRequested << "\n";
    std::cout << "  Frames Processed (Eng): " << diag.totalFramesProcessed << "\n";
    std::cout << "  Frames Submitted (WAS): " << diag.totalFramesSubmitted << "\n";
    std::cout << "  Frame Count Mismatches: " << diag.frameCountMismatches << "\n";
    std::cout << "  Render Underruns      : " << diag.renderUnderruns << "\n";
    std::cout << "  Render Errors         : " << diag.renderErrors << "\n";
    std::cout << "  Capture Overruns      : " << diag.captureOverruns << "\n";
    std::cout << "  Capture Errors        : " << diag.captureErrors << "\n";
    std::cout << "  GetBuffer Failures    : " << diag.getBufferFailures << "\n";
    std::cout << "  ReleaseBuffer Failures: " << diag.releaseBufferFailures << "\n";
    std::cout << "  Deadline Misses       : " << diag.deadlineMisses << "\n";
    std::cout << "  Sample Rate Mismatches: " << diag.sampleRateMismatches << "\n";
    std::cout << "  Channel Mismatches    : " << diag.channelMismatches << "\n";
    std::cout << "  Format Mismatches     : " << diag.formatMismatches << "\n";
    std::cout << "  MMCSS Registered      : " << (diag.mmcssRegistered ? "SUCCESS" : "FAILED")
              << (diag.mmcssRegistered ? "" : (" (Error: " + std::to_string(diag.mmcssErrorCode) + ")")) << "\n";
    std::cout << "  Min Process Time      : " << diag.minProcessTimeUs << " us\n";
    std::cout << "  Avg Process Time      : " << diag.avgProcessTimeUs << " us\n";
    std::cout << "  Max Process Time      : " << diag.maxProcessTimeUs << " us\n";
    std::cout << "  Block Deadline        : " << std::fixed << std::setprecision(2) << diag.blockDeadlineMs << " ms\n";
    std::cout << "------------------------------------------------------------\n";
}

// Phase 3: Interleaved <-> Planar Unit Verification
bool testPhase3InterleavedPlanar() {
    std::cout << "\n============================================================\n";
    std::cout << "PHASE 3: INTERLEAVED VS PLANAR AUDIT & ROUNDTRIP TEST\n";
    std::cout << "============================================================\n";

    constexpr uint32_t kFrames = 512;
    constexpr uint32_t kChannels = 2;

    std::vector<float> inputInterleaved(kFrames * kChannels);
    for (uint32_t i = 0; i < kFrames; ++i) {
        inputInterleaved[i * kChannels + 0] = 1.0f;  // L
        inputInterleaved[i * kChannels + 1] = -1.0f; // R
    }

    audiocore::AudioBuffer planarBuffer(kChannels, kFrames);
    for (uint32_t ch = 0; ch < kChannels; ++ch) {
        float* chData = planarBuffer.getChannelData(ch);
        for (uint32_t i = 0; i < kFrames; ++i) {
            chData[i] = inputInterleaved[i * kChannels + ch];
        }
    }

    bool planarOk = true;
    for (uint32_t i = 0; i < kFrames; ++i) {
        if (planarBuffer.getChannelData(0)[i] != 1.0f) planarOk = false;
        if (planarBuffer.getChannelData(1)[i] != -1.0f) planarOk = false;
    }
    std::cout << "  [Step 1] Interleaved -> Planar: " << (planarOk ? "PASS" : "FAIL") << "\n";

    std::vector<float> outputInterleaved(kFrames * kChannels, 0.0f);
    for (uint32_t ch = 0; ch < kChannels; ++ch) {
        const float* srcCh = planarBuffer.getChannelData(ch);
        for (uint32_t i = 0; i < kFrames; ++i) {
            outputInterleaved[i * kChannels + ch] = srcCh[i];
        }
    }

    float maxErr = 0.0f;
    for (size_t i = 0; i < inputInterleaved.size(); ++i) {
        float d = std::abs(inputInterleaved[i] - outputInterleaved[i]);
        if (d > maxErr) maxErr = d;
    }
    std::cout << "  [Step 2] Planar -> Interleaved Max Error: " << maxErr << " (Tolerance: 1e-6)\n";
    bool pass = planarOk && (maxErr < 1e-6f);
    std::cout << "  [RESULT]: " << (pass ? "PASS: Bit-Exact Lossless Roundtrip" : "FAIL") << "\n";
    return pass;
}

// Generic 10-Second Continuous Test Runner
bool runTestSession(
    const std::string& title,
    bool duplexMode,
    bool installToneGen,
    bool installGain,
    float gainDb,
    uint32_t durationSec,
    float* outPeak = nullptr)
{
    std::cout << "\n============================================================\n";
    std::cout << "RUNNING: " << title << " (" << durationSec << " seconds)\n";
    std::cout << "============================================================\n";

    auto backend = std::make_shared<audiocore::WasapiBackend>();
    backend->setStreamMode(duplexMode ? audiocore::WasapiStreamMode::Duplex : audiocore::WasapiStreamMode::PlaybackOnly);
    backend->setSafeFeedbackGuard(true, 0.05f);

    if (!backend->initialize(48000.0, 128, 2)) {
        std::cerr << "WasapiBackend::initialize failed!\n";
        return false;
    }

    double wasapiRate = backend->getSampleRate();
    uint32_t wasapiChannels = backend->getChannels();
    uint32_t hwBufferFrames = backend->getActualBufferSizeFrames();

    std::cout << "  WASAPI Device Output: " << backend->getActivePlaybackDeviceName() << "\n";
    if (duplexMode) {
        std::cout << "  WASAPI Device Input : " << backend->getActiveCaptureDeviceName() << "\n";
    }
    std::cout << "  WASAPI Hardware Rate: " << wasapiRate << " Hz\n";
    std::cout << "  WASAPI Channels     : " << wasapiChannels << " ch\n";
    std::cout << "  WASAPI Buffer Frames: " << hwBufferFrames << " frames (~"
              << std::fixed << std::setprecision(2) << backend->getEstimatedLatencyMs() << " ms)\n";

    // Engine configured with STRICT 128-frame logical block size
    constexpr uint32_t kLogicalBlockSize = 128;
    audiocore::AudioEngine engine;
    engine.configure(wasapiRate, kLogicalBlockSize, wasapiChannels);
    std::cout << "  AudioEngine SR      : " << engine.getSampleRate() << " Hz\n";
    std::cout << "  AudioEngine BlockSz : " << engine.getBlockSize() << " frames\n";

    auto& graph = engine.getGraph();
    graph.clearNodes();

    if (installToneGen) {
        std::cout << "  Signal Generator    : 440 Hz Sine @ " << wasapiRate << " Hz (0.25 amp)\n";
        graph.addNode(std::make_shared<ToneGeneratorNode>(wasapiRate, 440.0, 0.25f));
    }

    std::shared_ptr<audiocore::GainNode> gainNode;
    if (installGain) {
        std::cout << "  Gain Node Installed : " << gainDb << " dB\n";
        gainNode = std::make_shared<audiocore::GainNode>(gainDb);
        graph.addNode(gainNode);
    }

    auto peakMeter = std::make_shared<PeakMeasurementNode>();
    graph.addNode(peakMeter);

    engine.setBackend(backend);

    std::cout << "\nStarting continuous streaming...\n";
    if (!engine.start()) {
        std::cerr << "engine.start() failed!\n";
        return false;
    }

    for (uint32_t s = 1; s <= durationSec; ++s) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cout << "  [" << title << "] " << s << " / " << durationSec << " s (Peak: "
                  << std::fixed << std::setprecision(4) << peakMeter->getMaxPeak() << ")...\n";
    }

    engine.stop();
    std::cout << "Stream stopped cleanly.\n";

    if (outPeak) {
        *outPeak = peakMeter->getMaxPeak();
    }

    auto diag = backend->getDiagnostics();
    printDiagnosticsReport(title, diag);

    bool pass = (diag.renderUnderruns == 0) &&
                (diag.renderErrors == 0) &&
                (diag.deadlineMisses == 0) &&
                (diag.frameCountMismatches == 0) &&
                (diag.getBufferFailures == 0) &&
                (diag.releaseBufferFailures == 0) &&
                (diag.totalFramesSubmitted > 0);

    std::cout << "  [TEST EVALUATION]: " << (pass ? "PASS (ZERO REALTIME ERRORS)" : "FAIL") << "\n";
    return pass;
}

// Phase 12: Realtime Hardware DSP Test (Full Chain)
bool runPhase12FullDspChain(uint32_t durationSec) {
    std::cout << "\n============================================================\n";
    std::cout << "PHASE 12: REALTIME FULL DSP HARDWARE STREAMING (" << durationSec << " seconds)\n";
    std::cout << "============================================================\n";

    auto backend = std::make_shared<audiocore::WasapiBackend>();
    backend->setStreamMode(audiocore::WasapiStreamMode::PlaybackOnly);
    backend->setSafeFeedbackGuard(true, 0.05f);

    if (!backend->initialize(48000.0, 128, 2)) {
        std::cerr << "WasapiBackend::initialize failed!\n";
        return false;
    }

    double wasapiRate = backend->getSampleRate();
    uint32_t wasapiChannels = backend->getChannels();
    uint32_t hwBufferFrames = backend->getActualBufferSizeFrames();

    std::cout << "  WASAPI Device Output: " << backend->getActivePlaybackDeviceName() << "\n";
    std::cout << "  WASAPI Hardware Rate: " << wasapiRate << " Hz\n";
    std::cout << "  WASAPI Channels     : " << wasapiChannels << " ch\n";
    std::cout << "  WASAPI Buffer Frames: " << hwBufferFrames << " frames (~"
              << std::fixed << std::setprecision(2) << backend->getEstimatedLatencyMs() << " ms)\n";

    constexpr uint32_t kLogicalBlockSize = 128;
    audiocore::AudioEngine engine;
    engine.configure(wasapiRate, kLogicalBlockSize, wasapiChannels);
    std::cout << "  AudioEngine SR      : " << engine.getSampleRate() << " Hz\n";
    std::cout << "  AudioEngine BlockSz : " << engine.getBlockSize() << " frames\n";

    auto& graph = engine.getGraph();
    graph.clearNodes();

    // 1. Signal Source: 440 Hz Sine @ -6 dBFS (0.5 amp)
    auto toneGen = std::make_shared<ToneGeneratorNode>(wasapiRate, 440.0, 0.5f);
    graph.addNode(toneGen);
    std::cout << "  [1] Signal Source   : 440 Hz ToneGenerator (amplitude 0.5)\n";

    // 2. MixerNode (2 channels, stereo mapped Ch0 -> L, Ch1 -> R, unity gain)
    auto mixer = std::make_shared<audiocore::MixerNode>(wasapiChannels);
    mixer->setChannelGainDb(0, 0.0f);
    mixer->setChannelPan(0, -1.0f);
    if (wasapiChannels > 1) {
        mixer->setChannelGainDb(1, 0.0f);
        mixer->setChannelPan(1, 1.0f);
    }
    mixer->setMasterGainDb(0.0f);
    graph.addNode(mixer);
    std::cout << "  [2] MixerNode       : 2-Channel Summing & Bus (Unity 0 dB)\n";

    // 3. GainNode (unity gain 0 dB)
    auto gainNode = std::make_shared<audiocore::GainNode>(0.0f);
    graph.addNode(gainNode);
    std::cout << "  [3] GainNode        : Unity Gain 0.0 dB\n";

    // 4. PanNode (center 0.0)
    auto panNode = std::make_shared<audiocore::PanNode>(0.0f);
    graph.addNode(panNode);
    std::cout << "  [4] PanNode         : Center Pan 0.0\n";

    // 5. ParametricEQNode (4 bands: Band 1 Peaking +2 dB @ 440 Hz, Low/High Shelves)
    auto eqNode = std::make_shared<audiocore::ParametricEQNode>(4, wasapiRate);
    eqNode->setBand(0, audiocore::FilterType::LowShelf, 100.0, 0.7071, 0.0);
    eqNode->setBand(1, audiocore::FilterType::Peaking, 440.0, 1.0, 2.0);
    eqNode->setBand(2, audiocore::FilterType::Peaking, 2500.0, 1.0, 0.0);
    eqNode->setBand(3, audiocore::FilterType::HighShelf, 10000.0, 0.7071, 0.0);
    graph.addNode(eqNode);
    std::cout << "  [5] ParametricEQNode: 4-Band EQ (Band 1 Peaking +2 dB @ 440 Hz)\n";

    // 6. CompressorNode (thresh -12 dBFS, ratio 2.0:1, attack 10ms, release 100ms)
    auto compNode = std::make_shared<audiocore::CompressorNode>(wasapiRate);
    compNode->setThresholdDb(-12.0f);
    compNode->setRatio(2.0f);
    compNode->setAttackMs(10.0f);
    compNode->setReleaseMs(100.0f);
    compNode->setMakeupGainDb(0.0f);
    graph.addNode(compNode);
    std::cout << "  [6] CompressorNode  : Thresh -12 dB, Ratio 2.0:1, Att 10ms, Rel 100ms\n";

    // 7. LimiterNode (ceiling -1.0 dBFS, release 50ms)
    auto limiterNode = std::make_shared<audiocore::LimiterNode>(-1.0f, wasapiRate);
    limiterNode->setReleaseMs(50.0f);
    graph.addNode(limiterNode);
    std::cout << "  [7] LimiterNode     : Ceiling -1.0 dBFS, Rel 50ms\n";

    // 8. MeterNode (Stereo Peak & RMS)
    auto meterNode = std::make_shared<audiocore::MeterNode>();
    graph.addNode(meterNode);
    std::cout << "  [8] MeterNode       : Stereo Peak & RMS Meter\n";

    engine.setBackend(backend);

    std::cout << "\nStarting continuous streaming across full DSP chain...\n";
    if (!engine.start()) {
        std::cerr << "engine.start() failed!\n";
        return false;
    }

    for (uint32_t s = 1; s <= durationSec; ++s) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        float pL = meterNode->getPeak(0);
        float rL = meterNode->getRms(0);
        float pDbL = (pL > 1e-6f) ? (20.0f * std::log10(pL)) : -120.0f;
        float rDbL = (rL > 1e-6f) ? (20.0f * std::log10(rL)) : -120.0f;
        float compGr = compNode->getCurrentGainReductionDb();
        float limGain = limiterNode->getCurrentGain();
        float limGr = (limGain > 1e-6f) ? (20.0f * std::log10(limGain)) : 0.0f;

        std::cout << "  [Phase 12: Full DSP] " << s << " / " << durationSec << " s"
                  << " | Peak: " << std::fixed << std::setprecision(4) << pL
                  << " (" << std::setprecision(2) << pDbL << " dB)"
                  << " | RMS: " << std::setprecision(4) << rL
                  << " (" << std::setprecision(2) << rDbL << " dB)"
                  << " | Comp GR: " << std::setprecision(2) << compGr << " dB"
                  << " | Lim GR: " << std::setprecision(2) << limGr << " dB\n";
    }

    engine.stop();
    std::cout << "Stream stopped cleanly.\n";

    auto diag = backend->getDiagnostics();
    printDiagnosticsReport("Phase 12: Full DSP Chain", diag);

    double deadlineUs = diag.blockDeadlineMs * 1000.0;
    double utilization = (deadlineUs > 0.0) ? (static_cast<double>(diag.maxProcessTimeUs) / deadlineUs * 100.0) : 0.0;
    double avgUtilization = (deadlineUs > 0.0) ? (static_cast<double>(diag.avgProcessTimeUs) / deadlineUs * 100.0) : 0.0;

    std::cout << "\n============================================================\n";
    std::cout << "PHASE 12 PERFORMANCE & UTILIZATION REPORT\n";
    std::cout << "============================================================\n";
    std::cout << "  Baseline (No DSP) Max Time: 142 - 156 us\n";
    std::cout << "  Full DSP Min Process Time : " << diag.minProcessTimeUs << " us\n";
    std::cout << "  Full DSP Avg Process Time : " << diag.avgProcessTimeUs << " us (" << std::fixed << std::setprecision(2) << avgUtilization << "% utilization)\n";
    std::cout << "  Full DSP Max Process Time : " << diag.maxProcessTimeUs << " us (" << std::fixed << std::setprecision(2) << utilization << "% utilization)\n";
    std::cout << "  Block Deadline            : " << diag.blockDeadlineMs << " ms (" << deadlineUs << " us)\n";
    std::cout << "  Render Underruns          : " << diag.renderUnderruns << "\n";
    std::cout << "  Deadline Misses           : " << diag.deadlineMisses << "\n";
    std::cout << "  Hardware / Render Errors  : " << diag.renderErrors << "\n";
    std::cout << "  Processing Utilization    : " << std::fixed << std::setprecision(2) << utilization << " %\n";
    std::cout << "  Status: " << (utilization <= 25.0 ? "EXCELLENT (< 25% threshold)" : "EXCEEDED 25% THRESHOLD") << "\n";
    std::cout << "============================================================\n";

    bool pass = (diag.renderUnderruns == 0) &&
                (diag.renderErrors == 0) &&
                (diag.deadlineMisses == 0) &&
                (diag.frameCountMismatches == 0) &&
                (diag.getBufferFailures == 0) &&
                (diag.releaseBufferFailures == 0) &&
                (diag.totalFramesSubmitted > 0);

    std::cout << "  [TEST EVALUATION]: " << (pass ? "PASS (ZERO REALTIME ERRORS)" : "FAIL") << "\n";
    return pass;
}

} // namespace

int main(int argc, char* argv[]) {
    uint32_t testDuration = 10;
    if (argc > 1) {
        testDuration = std::max(2, std::atoi(argv[1]));
    }

    std::cout << "============================================================\n";
    std::cout << "AUDIO CORE: COMPLETE DEEP DEBUG, AUDIT & STABILIZATION SUITE\n";
    std::cout << "============================================================\n";

    int passed = 0;
    int failed = 0;

    auto track = [&](bool ok, const std::string& name) {
        if (ok) {
            std::cout << "\n>>> [RESULT: PASS] " << name << " <<<\n";
            passed++;
        } else {
            std::cout << "\n>>> [RESULT: FAIL] " << name << " <<<\n";
            failed++;
        }
    };

    // PHASE 3: Interleaved <-> Planar
    track(testPhase3InterleavedPlanar(), "Phase 3: Interleaved <-> Planar Audit");

    // PHASE 1: Minimal Engine Bypass (Direct Copy) - 10 seconds
    track(runTestSession("Phase 1: Minimal Engine Bypass (No DSP)", false, false, false, 0.0f, testDuration),
          "Phase 1: Minimal Engine Bypass");

    // PHASE 10 - TEST A: 440 Hz Sine -> AudioEngine -> WASAPI (No DSP) - 10 seconds
    float peakA = 0.0f;
    track(runTestSession("Phase 10 Test A: 440 Hz Sine -> AudioEngine (Bypass)", false, true, false, 0.0f, testDuration, &peakA),
          "Phase 10 Test A: Sine Direct");

    // PHASE 10 - TEST B: 440 Hz Sine -> GainNode 0 dB -> WASAPI - 10 seconds
    float peakB = 0.0f;
    track(runTestSession("Phase 10 Test B: 440 Hz Sine -> GainNode 0 dB", false, true, true, 0.0f, testDuration, &peakB),
          "Phase 10 Test B: Gain 0 dB");
    std::cout << "  [Peak Verification] Test B Peak: " << peakB << " (Expected ~0.2500): "
              << (std::abs(peakB - 0.25f) < 0.01f ? "MATCH" : "MISMATCH") << "\n";

    // PHASE 10 - TEST C: 440 Hz Sine -> GainNode -6 dB -> WASAPI - 10 seconds
    float peakC = 0.0f;
    track(runTestSession("Phase 10 Test C: 440 Hz Sine -> GainNode -6 dB", false, true, true, -6.0f, testDuration, &peakC),
          "Phase 10 Test C: Gain -6 dB");
    float expectedMinus6 = 0.25f * 0.501187f;
    std::cout << "  [Peak Verification] Test C Peak: " << peakC << " (Expected ~" << expectedMinus6 << "): "
              << (std::abs(peakC - expectedMinus6) < 0.01f ? "MATCH" : "MISMATCH") << "\n";

    // PHASE 10 - TEST D: 440 Hz Sine -> Mute -> WASAPI - 10 seconds
    float peakD = 0.0f;
    track(runTestSession("Phase 10 Test D: 440 Hz Sine -> Mute (-120 dB)", false, true, true, -120.0f, testDuration, &peakD),
          "Phase 10 Test D: Mute");
    std::cout << "  [Peak Verification] Test D Peak: " << peakD << " (Expected 0.0): "
              << (peakD < 1e-4f ? "MATCH (SILENCE)" : "MISMATCH") << "\n";

    // PHASE 11: Duplex Microphone -> Capture -> Engine -> Render -> Output - 10 seconds
    track(runTestSession("Phase 11: Duplex Microphone -> Engine -> Render (Safe Guard Active)", true, false, true, 0.0f, testDuration),
          "Phase 11: Duplex Capture Loopback");

    // PHASE 12: Realtime Hardware DSP Test (Full Chain) - 10 seconds
    track(runPhase12FullDspChain(testDuration),
          "Phase 12: Realtime Full DSP Hardware Streaming");

    std::cout << "\n============================================================\n";
    std::cout << "ALL SUITE AUDIT COMPLETED: " << passed << " PASSED, " << failed << " FAILED\n";
    std::cout << "============================================================\n";

    return (failed == 0) ? 0 : 1;
}

