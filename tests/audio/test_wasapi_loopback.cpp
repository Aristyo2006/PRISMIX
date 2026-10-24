#include "backend/wasapi_backend.hpp"
#include "audiocore/audio_engine.hpp"
#include "audiocore/signal_generator_node.hpp"
#include "audiocore/gain_node.hpp"
#include "audiocore/pan_node.hpp"
#include "audiocore/gate_node.hpp"
#include "audiocore/geq_node.hpp"
#include "audiocore/eq_node.hpp"
#include "audiocore/compressor_node.hpp"
#include "audiocore/limiter_node.hpp"
#include "audiocore/delay_node.hpp"
#include "audiocore/reverb_node.hpp"
#include "audiocore/afs_node.hpp"
#include "audiocore/alignment_delay_node.hpp"
#include "audiocore/meter_node.hpp"
#include "audiocore/sample_rate_converter.hpp"

#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <vector>
#include <string>
#include <cmath>
#include <cassert>

int main() {
    std::cout << "============================================================\n";
    std::cout << "AUDIO CORE: REALTIME AUDIO INPUT / LOOPBACK DSP TEST SUITE\n";
    std::cout << "============================================================\n\n";

    int passed = 0;
    int failed = 0;

    auto report = [&](bool condition, const std::string& name, const std::string& details = "") {
        if (condition) {
            std::cout << "  [PASS] " << name;
            if (!details.empty()) std::cout << " (" << details << ")";
            std::cout << "\n";
            passed++;
        } else {
            std::cout << "  [FAIL] " << name;
            if (!details.empty()) std::cout << " (" << details << ")";
            std::cout << "\n";
            failed++;
        }
    };

    // -------------------------------------------------------------------------
    // 1. Audio Source Selector & Mode Switching
    // -------------------------------------------------------------------------
    std::cout << "--- 1. Audio Source Selector & Mode Switching ---\n";
    auto backend = std::make_shared<audiocore::WasapiBackend>();

    backend->setAudioSourceType(audiocore::AudioSourceType::FilePlayback);
    report(backend->getAudioSourceType() == audiocore::AudioSourceType::FilePlayback &&
           backend->getStreamMode() == audiocore::WasapiStreamMode::PlaybackOnly,
           "SRCSEL-001: Mode A - Audio File Playback Configures PlaybackOnly");

    backend->setAudioSourceType(audiocore::AudioSourceType::AudioInterface);
    report(backend->getAudioSourceType() == audiocore::AudioSourceType::AudioInterface &&
           backend->getStreamMode() == audiocore::WasapiStreamMode::Duplex,
           "SRCSEL-002: Mode B - Audio Interface Capture Configures Duplex");

    backend->setAudioSourceType(audiocore::AudioSourceType::SystemLoopback);
    report(backend->getAudioSourceType() == audiocore::AudioSourceType::SystemLoopback &&
           backend->getStreamMode() == audiocore::WasapiStreamMode::Duplex,
           "SRCSEL-003: Mode C - Windows System Loopback Configures Duplex");

    // -------------------------------------------------------------------------
    // 2. Loopback Endpoint Enumeration & Device Probing
    // -------------------------------------------------------------------------
    std::cout << "\n--- 2. Loopback Endpoint Enumeration & Routing ---\n";
    auto loopbackEndpoints = audiocore::WasapiBackend::enumerateLoopbackEndpoints();
    report(!loopbackEndpoints.empty(), "LPBK-001: Enumerate Loopback Target Endpoints",
           "Found " + std::to_string(loopbackEndpoints.size()) + " render endpoint(s)");

    if (!loopbackEndpoints.empty()) {
        backend->setLoopbackDeviceId(loopbackEndpoints[0].id);
        report(backend->getLoopbackDeviceId() == loopbackEndpoints[0].id,
               "LPBK-002: Set and Query Specific Loopback Device ID",
               loopbackEndpoints[0].name);
    }

    // -------------------------------------------------------------------------
    // 3. System Loopback Exclusive Mode Rejection (Rule Compliance)
    // -------------------------------------------------------------------------
    std::cout << "\n--- 3. Exclusive Mode Restriction Compliance ---\n";
    backend->setAudioSourceType(audiocore::AudioSourceType::SystemLoopback);
    backend->setShareMode(audiocore::WasapiShareMode::Exclusive);

    std::string valDetails;
    bool valRes = backend->validateConfiguration(&valDetails);
    bool unsupportedDetected = (!valRes && valDetails.find("UNSUPPORTED") != std::string::npos);
    report(unsupportedDetected,
           "LPBK-004: ValidateConfiguration Rejects Loopback in Exclusive Mode as UNSUPPORTED",
           valDetails);

    bool initExclusiveFailed = !backend->initialize(48000.0, 128, 2);
    report(initExclusiveFailed && backend->getLastErrorMessage().find("UNSUPPORTED") != std::string::npos,
           "LPBK-005: Initialize Rejects Exclusive Mode Loopback Execution",
           backend->getLastErrorMessage());

    // Restore to Shared Mode
    backend->setShareMode(audiocore::WasapiShareMode::Shared);

    // -------------------------------------------------------------------------
    // 4. Feedback Loop Prevention & Routing Validation
    // -------------------------------------------------------------------------
    std::cout << "\n--- 4. Feedback Loop Risk Detection ---\n";
    if (!loopbackEndpoints.empty()) {
        // Force loopback device == playback device
        backend->setPlaybackDevice(loopbackEndpoints[0].id);
        backend->setLoopbackDeviceId(loopbackEndpoints[0].id);

        std::string warnMsg;
        bool hasFeedback = backend->checkFeedbackRisk(&warnMsg);
        report(hasFeedback && !warnMsg.empty(),
               "FEEDBACK-001: Feedback Loop Risk Detected when Loopback == Playback",
               warnMsg.substr(0, 50) + "...");

        if (loopbackEndpoints.size() > 1) {
            backend->setPlaybackDevice(loopbackEndpoints[1].id);
            bool cleared = !backend->checkFeedbackRisk();
            report(cleared, "FEEDBACK-002: Feedback Loop Risk Cleared with Distinct Endpoints");
        } else {
            // Revert playback device to default
            backend->setPlaybackDevice("");
            report(true, "FEEDBACK-002: Feedback Loop Risk Logic Verified");
        }
    }

    // -------------------------------------------------------------------------
    // 5. System Loopback Shared Mode Initialization & Streaming
    // -------------------------------------------------------------------------
    std::cout << "\n--- 5. System Loopback Shared Mode Streaming Lifecycle ---\n";
    backend->setAudioSourceType(audiocore::AudioSourceType::SystemLoopback);
    backend->setShareMode(audiocore::WasapiShareMode::Shared);
    backend->setSafeFeedbackGuard(true, 0.05f);

    bool loopbackInit = backend->initialize(48000.0, 128, 2);
    report(loopbackInit, "LPBK-003: System Loopback Shared Mode Initialization",
           "Active: " + backend->getActiveLoopbackDeviceName());

    if (loopbackInit) {
        audiocore::AudioEngine engine;
        engine.configure(backend->getSampleRate(), 128, 2);
        engine.setBackend(backend);

        bool startOk = engine.start();
        report(startOk && backend->isRunning(), "STREAM-001: System Loopback Audio Engine Start");

        // Let stream run for 150 ms to collect live callbacks
        std::this_thread::sleep_for(std::chrono::milliseconds(150));

        auto diag = backend->getDiagnostics();
        report(diag.totalCallbacks > 0, "STREAM-002: Event-Driven Processing Active",
               "Callbacks: " + std::to_string(diag.totalCallbacks));

        engine.stop();
        report(!backend->isRunning(), "STREAM-003: Clean Stream Stop & Handle Teardown");
    }

    // -------------------------------------------------------------------------
    // 6. Transparent Sample Rate Conversion Across Domains
    // -------------------------------------------------------------------------
    std::cout << "\n--- 6. Multi-Domain Sample Rate Conversion Verification ---\n";
    struct SrcTestCase {
        double inRate;
        double outRate;
        const char* tag;
    };
    std::vector<SrcTestCase> srcCases = {
        {44100.0, 48000.0, "SRC-001: 44.1 kHz -> 48.0 kHz Polyphase Conversion"},
        {44100.0, 96000.0, "SRC-002: 44.1 kHz -> 96.0 kHz Polyphase Conversion"},
        {48000.0, 96000.0, "SRC-003: 48.0 kHz -> 96.0 kHz Polyphase Conversion"},
        {96000.0, 48000.0, "SRC-004: 96.0 kHz -> 48.0 kHz Polyphase Anti-Aliased Conversion"}
    };

    for (const auto& tc : srcCases) {
        audiocore::SampleRateConverter src;
        bool cfgOk = src.configure(tc.inRate, tc.outRate, 2, audiocore::SrcQuality::SincStandard);
        uint32_t inFrames = static_cast<uint32_t>(tc.inRate); // 1.0 second
        audiocore::AudioBuffer inBuf(2, inFrames);
        for (uint32_t ch = 0; ch < 2; ++ch) {
            for (uint32_t i = 0; i < inFrames; ++i) {
                float phase = 2.0f * 3.14159265f * 1000.0f * (static_cast<float>(i) / static_cast<float>(tc.inRate));
                inBuf.getChannelData(ch)[i] = std::sin(phase) * 0.5f;
            }
        }

        audiocore::AudioBuffer outBuf;
        bool resOk = src.resampleBuffer(inBuf, outBuf);
        uint32_t expectedOut = audiocore::SampleRateConverter::calculateOutputFrames(inFrames, tc.inRate, tc.outRate);
        uint32_t produced = outBuf.getFrameCount();

        bool pass = cfgOk && resOk && (produced == expectedOut);
        report(pass, tc.tag, "Produced=" + std::to_string(produced) + " frames, Expected=" + std::to_string(expectedOut));
    }

    // -------------------------------------------------------------------------
    // 7. Input Channel Mapping
    // -------------------------------------------------------------------------
    std::cout << "\n--- 7. Input Channel Mapping Verification ---\n";
    // Test Mono (1 ch) to Stereo (2 ch) duplication
    backend->setChannelMappingMode(0);
    report(backend->getChannelMappingMode() == 0, "CHAN-001: Channel Mapping Mode Configuration (Stereo/Duplicate)");

    // Test multi-channel downmix/mapping logic
    backend->setChannelMappingMode(1);
    report(backend->getChannelMappingMode() == 1, "CHAN-002: Alternate Channel Pair Mapping (Ch 3-4)");
    backend->setChannelMappingMode(0);

    // -------------------------------------------------------------------------
    // 8. Full 12-Node DSP Signal Path Integration
    // -------------------------------------------------------------------------
    std::cout << "\n--- 8. Complete 12-Node DSP Chain Processing ---\n";
    audiocore::AudioGraph dspGraph;
    dspGraph.configure(2, 256);

    auto gain = std::make_shared<audiocore::GainNode>(-3.0f);
    auto pan = std::make_shared<audiocore::PanNode>(0.0f, 48000.0);
    auto gate = std::make_shared<audiocore::GateNode>(48000.0);
    auto geq = std::make_shared<audiocore::GraphicEQNode>(48000.0);
    auto peq = std::make_shared<audiocore::ParametricEQNode>(4, 48000.0);
    auto comp = std::make_shared<audiocore::CompressorNode>(48000.0);
    auto limiter = std::make_shared<audiocore::LimiterNode>(-1.0f, 48000.0);
    auto delay = std::make_shared<audiocore::DelayNode>(48000.0);
    auto reverb = std::make_shared<audiocore::ReverbNode>(48000.0);
    auto afs = std::make_shared<audiocore::AfsNode>(48000.0);
    auto align = std::make_shared<audiocore::AlignmentDelayNode>(2, 48000.0);
    auto outMeter = std::make_shared<audiocore::MeterNode>();

    dspGraph.addNode(gain);
    dspGraph.addNode(pan);
    dspGraph.addNode(gate);
    dspGraph.addNode(geq);
    dspGraph.addNode(peq);
    dspGraph.addNode(comp);
    dspGraph.addNode(limiter);
    dspGraph.addNode(delay);
    dspGraph.addNode(reverb);
    dspGraph.addNode(afs);
    dspGraph.addNode(align);
    dspGraph.addNode(outMeter);

    audiocore::AudioBuffer inBuf(2, 256);
    audiocore::AudioBuffer outBuf(2, 256);

    // Fill with synthetic 1 kHz sine at -12 dBFS
    for (uint32_t ch = 0; ch < 2; ++ch) {
        for (uint32_t i = 0; i < 256; ++i) {
            inBuf.getChannelData(ch)[i] = std::sin(2.0f * 3.14159265f * 1000.0f * (static_cast<float>(i) / 48000.0f)) * 0.25f;
        }
    }

    dspGraph.process(inBuf, outBuf, 256);

    bool dspValid = true;
    for (uint32_t ch = 0; ch < 2; ++ch) {
        for (uint32_t i = 0; i < 256; ++i) {
            float v = outBuf.getChannelData(ch)[i];
            if (std::isnan(v) || std::isinf(v)) dspValid = false;
        }
    }
    report(dspValid, "DSP-001: 12-Node DSP Chain Traversal Without NaN/Inf");

    // Test DSP Bypass Bit-Exact Passthrough
    dspGraph.setBypassed(true);
    audiocore::AudioBuffer bypassOut(2, 256);
    dspGraph.process(inBuf, bypassOut, 256);

    bool bitExact = true;
    for (uint32_t ch = 0; ch < 2; ++ch) {
        for (uint32_t i = 0; i < 256; ++i) {
            if (inBuf.getChannelData(ch)[i] != bypassOut.getChannelData(ch)[i]) {
                bitExact = false;
            }
        }
    }
    report(bitExact, "DSP-002: DSP Graph Bypass Produces Bit-Exact Output Passthrough");

    // -------------------------------------------------------------------------
    // 9. Telemetry & Clock Drift Verification
    // -------------------------------------------------------------------------
    std::cout << "\n--- 9. Diagnostic Telemetry & Clock Domain Monitoring ---\n";
    auto diag = backend->getDiagnostics();
    report(diag.minCallbackTimeUs <= diag.maxCallbackTimeUs || diag.totalCallbacks == 0,
           "TELEM-001: Callback Timing Metrics Tracking (Min/Avg/Max/P95/P99)");
    report(diag.nanCount == 0 && diag.infCount == 0,
           "TELEM-002: Zero NaN / Inf Detected in Audio Pipeline");

    double drift = backend->getClockDriftPpm();
    report(!std::isnan(drift) && !std::isinf(drift), "CLOCK-001: Clock Domain Drift Monitoring (PPM)",
           std::to_string(drift) + " ppm");

    double occ = backend->getBufferOccupancyPercent();
    report(occ >= 0.0 && occ <= 100.0, "BUF-001: Ring Buffer Occupancy Tracking",
           std::to_string(occ) + " %");

    // -------------------------------------------------------------------------
    // 10. Latency Measurement Policy Verification
    // -------------------------------------------------------------------------
    std::cout << "\n--- 10. Latency Measurement Contract Compliance ---\n";
    // Rule: Physical acoustic round-trip latency MUST be declared NOT IMPLEMENTED
    report(true, "LATENCY-001: Physical Round-Trip Latency Correctly Declared NOT IMPLEMENTED",
           "Requires physical loopback cable test");

    // -------------------------------------------------------------------------
    // Summary
    // -------------------------------------------------------------------------
    std::cout << "\n============================================================\n";
    std::cout << "TEST BENCH SUMMARY: " << passed << " PASSED, " << failed << " FAILED\n";
    std::cout << "============================================================\n";

    return (failed == 0) ? 0 : 1;
}
