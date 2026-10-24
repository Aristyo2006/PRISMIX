#include "backend/wasapi_backend.hpp"
#include "audiocore/audio_engine.hpp"
#include "audiocore/signal_generator_node.hpp"
#include "audiocore/gain_node.hpp"
#include "audiocore/eq_node.hpp"
#include "audiocore/compressor_node.hpp"
#include "audiocore/limiter_node.hpp"

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
    std::cout << "AUDIO CORE: WASAPI AUDIO DEVICE TEST & BENCHMARK SUITE\n";
    std::cout << "============================================================\n\n";

    int passed = 0;
    int failed = 0;
    int notRun = 0;
    int unsupported = 0;

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
    // TEST 1: Hardware Device Enumeration
    // -------------------------------------------------------------------------
    std::cout << "--- 1. Device Enumeration & Endpoint Inspection ---\n";
    auto renderDevs = audiocore::WasapiBackend::enumerateDevices(false);
    auto captureDevs = audiocore::WasapiBackend::enumerateDevices(true);

    report(!renderDevs.empty(), "DEV-001: Active Render Device Enumeration",
           "Found " + std::to_string(renderDevs.size()) + " render endpoint(s)");
    report(!captureDevs.empty(), "DEV-002: Active Capture Device Enumeration",
           "Found " + std::to_string(captureDevs.size()) + " capture endpoint(s)");

    bool renderHasDefault = false;
    for (const auto& d : renderDevs) {
        if (d.isDefault) renderHasDefault = true;
    }
    report(renderHasDefault, "DEV-003: Default Render Device Detection");

    bool captureHasDefault = false;
    for (const auto& d : captureDevs) {
        if (d.isDefault) captureHasDefault = true;
    }
    report(captureHasDefault, "DEV-004: Default Capture Device Detection");

    bool allHaveIds = true;
    for (const auto& d : renderDevs) {
        if (d.id.empty() || d.name.empty()) allHaveIds = false;
    }
    report(allHaveIds, "DEV-005: Endpoint Identification & Friendly Names Non-Empty");

    // Print discovery details
    std::cout << "  Discovered Render Endpoints:\n";
    for (size_t i = 0; i < renderDevs.size(); ++i) {
        const auto& d = renderDevs[i];
        std::cout << "    [" << i << "] " << d.name 
                  << (d.isDefault ? " [DEFAULT]" : "")
                  << " | ID: " << d.id.substr(0, 24) << "..."
                  << " | " << d.channels << " ch | " << d.defaultSampleRate << " Hz"
                  << " | MinPeriod: " << std::fixed << std::setprecision(2) << d.minDevicePeriodMs << " ms\n";
    }

    if (renderDevs.empty()) {
        std::cout << "\n[SKIPPED: REASON: No physical audio render endpoints available in current test environment]\n";
        return (failed == 0) ? 0 : 1;
    }

    // -------------------------------------------------------------------------
    // TEST 2: Device Selection & Backend Abstraction
    // -------------------------------------------------------------------------
    std::cout << "\n--- 2. Device Selection & Backend Abstraction ---\n";
    auto backend = std::make_shared<audiocore::WasapiBackend>();
    report(backend->getBackendType() == audiocore::AudioBackendType::WASAPI,
           "BACKEND-001: Backend Type Identification is WASAPI");

    bool setPlayOk = backend->setPlaybackDevice(renderDevs[0].id);
    report(setPlayOk, "DEV-006: Select Specific Playback Device ID", renderDevs[0].name);

    if (!captureDevs.empty()) {
        bool setCapOk = backend->setCaptureDevice(captureDevs[0].id);
        report(setCapOk, "DEV-007: Select Specific Capture Device ID", captureDevs[0].name);
    }

    // -------------------------------------------------------------------------
    // TEST 3: Format & Configuration Validation
    // -------------------------------------------------------------------------
    std::cout << "\n--- 3. Configuration Validation & Shared Mode Negotiation ---\n";
    std::string valDetails;
    bool valOk = backend->validateConfiguration(&valDetails);
    report(valOk, "CONFIG-001: Configuration Validation", valDetails);

    backend->setShareMode(audiocore::WasapiShareMode::Shared);
    report(backend->getShareMode() == audiocore::WasapiShareMode::Shared,
           "CONFIG-002: Share Mode Setting: Shared");

    backend->setEventDriven(true);
    report(backend->isEventDriven(), "CONFIG-003: Event-Driven Buffering Flag: Enabled");

    backend->setRequestedSampleRate(48000.0);
    report(backend->getRequestedSampleRate() == 48000.0,
           "CONFIG-004: Requested Sample Rate Setting: 48000 Hz");

    backend->setRequestedBlockSize(128);
    report(backend->getRequestedBlockSize() == 128,
           "CONFIG-005: Requested Buffer Size Setting: 128 frames");

    bool initSharedOk = backend->initialize(48000.0, 128, 2);
    report(initSharedOk, "STREAM-001: Shared Mode Stream Initialization",
           "Active: " + backend->getActivePlaybackDeviceName());

    report(backend->getSampleRate() > 0.0, "STREAM-002: Actual Sample Rate Reporting",
           std::to_string(static_cast<uint32_t>(backend->getSampleRate())) + " Hz");
    report(backend->getChannels() >= 1, "STREAM-003: Actual Channel Count Reporting",
           std::to_string(backend->getChannels()) + " ch");
    report(backend->getActualBufferSizeFrames() > 0, "STREAM-004: Actual Buffer Size Reporting",
           std::to_string(backend->getActualBufferSizeFrames()) + " frames");
    report(backend->getBitDepth() >= 16, "STREAM-005: Bit Depth Reporting",
           std::to_string(backend->getBitDepth()) + " bits");
    report(!backend->getFormatDescription().empty(), "STREAM-006: Sample Format Specification",
           backend->getFormatDescription());
    report(backend->getActualPeriodicityMs() > 0.0, "STREAM-007: Actual Periodicity Reporting",
           std::to_string(backend->getActualPeriodicityMs()) + " ms");
    report(backend->getReportedLatencyMs() > 0.0, "STREAM-008: Reported WASAPI Latency",
           std::to_string(backend->getReportedLatencyMs()) + " ms");
    report(backend->getNominalBufferDurationMs() > 0.0, "STREAM-009: Nominal Buffer Duration",
           std::to_string(backend->getNominalBufferDurationMs()) + " ms");

    // -------------------------------------------------------------------------
    // TEST 4: Exclusive Mode Validation
    // -------------------------------------------------------------------------
    std::cout << "\n--- 4. Exclusive Mode Validation ---\n";
    backend->stop();
    backend->setShareMode(audiocore::WasapiShareMode::Exclusive);
    bool initExclusiveOk = backend->initialize(48000.0, 128, 2);
    if (initExclusiveOk) {
        report(true, "EXCLUSIVE-001: Exclusive Mode Stream Initialization",
               "Buffer: " + std::to_string(backend->getActualBufferSizeFrames()) + " frames");
        backend->stop();
    } else {
        std::cout << "  [UNSUPPORTED] EXCLUSIVE-001: Exclusive Mode Stream Initialization ("
                  << backend->getLastErrorMessage() << ")\n";
        unsupported++;
    }
    // Restore shared mode for subsequent streaming tests
    backend->setShareMode(audiocore::WasapiShareMode::Shared);
    backend->initialize(48000.0, 128, 2);

    // -------------------------------------------------------------------------
    // TEST 5: Event-Driven Audio Engine Streaming Lifecycle
    // -------------------------------------------------------------------------
    std::cout << "\n--- 5. Event-Driven Audio Engine Streaming Lifecycle ---\n";
    auto engine = std::make_shared<audiocore::AudioEngine>();
    engine->configure(backend->getSampleRate(), 128, backend->getChannels());
    engine->setBackend(backend);

    report(!backend->isRunning(), "STREAM-010: Stream Stopped Before Start");
    bool startOk = backend->start();
    report(startOk && backend->isRunning(), "STREAM-011: Stream Started Successfully");

    std::cout << "  Active streaming for 300 ms...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    report(backend->isRunning(), "STREAM-012: Stream Remained Active During Processing");
    backend->stop();
    report(!backend->isRunning(), "STREAM-013: Stream Stopped Cleanly");

    // -------------------------------------------------------------------------
    // TEST 6: Performance & Telemetry Measurement
    // -------------------------------------------------------------------------
    std::cout << "\n--- 6. Performance Telemetry & Realtime Utilization ---\n";
    auto diag = backend->getDiagnostics();
    report(diag.totalCallbacks > 0, "TELEM-001: Total Callbacks Count Recorded",
           std::to_string(diag.totalCallbacks) + " callbacks");
    report(diag.totalFramesProcessed > 0, "TELEM-002: Total Frames Processed Recorded",
           std::to_string(diag.totalFramesProcessed) + " frames");
    report(diag.minCallbackTimeUs < UINT64_MAX, "TELEM-003: Min Callback Execution Time",
           std::to_string(diag.minCallbackTimeUs) + " us");
    report(diag.avgCallbackTimeUs > 0, "TELEM-004: Avg Callback Execution Time",
           std::to_string(diag.avgCallbackTimeUs) + " us");
    report(diag.maxCallbackTimeUs > 0, "TELEM-005: Max Callback Execution Time",
           std::to_string(diag.maxCallbackTimeUs) + " us");
    report(diag.p95CallbackTimeUs > 0, "TELEM-006: P95 Callback Execution Time",
           std::to_string(diag.p95CallbackTimeUs) + " us");
    report(diag.p99CallbackTimeUs > 0, "TELEM-007: P99 Callback Execution Time",
           std::to_string(diag.p99CallbackTimeUs) + " us");
    report(diag.periodDurationMs > 0.0, "TELEM-008: Processing Period Duration Computed",
           std::to_string(diag.periodDurationMs) + " ms");
    report(diag.blockDeadlineMs > 0.0, "TELEM-009: Realtime Block Deadline Computed",
           std::to_string(diag.blockDeadlineMs) + " ms");

    std::cout << "  Diagnostics Summary:\n";
    std::cout << "    Callbacks   : " << diag.totalCallbacks << "\n";
    std::cout << "    Callback us : Avg=" << diag.avgCallbackTimeUs
              << " us, P95=" << diag.p95CallbackTimeUs
              << " us, P99=" << diag.p99CallbackTimeUs
              << " us, Max=" << diag.maxCallbackTimeUs << " us\n";
    std::cout << "    DSP us      : Avg=" << diag.avgDspTimeUs
              << " us, Max=" << diag.maxDspTimeUs << " us\n";
    std::cout << "    Utilization : " << std::fixed << std::setprecision(2)
              << diag.realtimeUtilizationPercent << " %\n";
    std::cout << "    Underruns   : " << diag.renderUnderruns << "\n";
    std::cout << "    Deadlines   : " << diag.deadlineMisses << "\n";

    // -------------------------------------------------------------------------
    // TEST 7: XRUN & Error Detection Monitoring
    // -------------------------------------------------------------------------
    std::cout << "\n--- 7. XRUN & Error Detection Monitoring ---\n";
    report(diag.renderUnderruns == 0, "XRUN-001: Zero Render Underruns Under Normal Load");
    report(diag.renderErrors == 0, "XRUN-002: Zero Render Errors");
    report(diag.getBufferFailures == 0, "XRUN-003: Zero GetBuffer Failures");
    report(diag.releaseBufferFailures == 0, "XRUN-004: Zero ReleaseBuffer Failures");
    report(diag.deadlineMisses == 0, "XRUN-005: Zero Deadline Misses");
    report(!backend->isDeviceInvalidated(), "XRUN-006: Device Invalidation Flag False Under Normal Operation");

    // -------------------------------------------------------------------------
    // TEST 8: Test Signal Generator Verification
    // -------------------------------------------------------------------------
    std::cout << "\n--- 8. Realtime Test Signal Generator Verification ---\n";
    audiocore::SignalGeneratorNode sigGen(48000.0);
    sigGen.setLevelDb(-20.0f);
    sigGen.setEnabled(true);

    audiocore::AudioBuffer inBuf(2, 512);
    audiocore::AudioBuffer outBuf(2, 512);

    // 8.1 Silence
    sigGen.setSignalType(audiocore::TestSignalType::Silence);
    sigGen.process(inBuf, outBuf, 512);
    bool silenceOk = true;
    for (uint32_t ch = 0; ch < 2; ++ch) {
        for (uint32_t i = 0; i < 512; ++i) {
            if (std::abs(outBuf.getChannelData(ch)[i]) > 1e-6f) silenceOk = false;
        }
    }
    report(silenceOk, "SIGGEN-001: Silence Signal Generates Exact Zeros");

    // 8.2 1 kHz Sine at -20 dBFS (~0.10 peak)
    sigGen.setSignalType(audiocore::TestSignalType::Sine1kHz);
    sigGen.process(inBuf, outBuf, 512);
    float maxSine = 0.0f;
    for (uint32_t ch = 0; ch < 2; ++ch) {
        for (uint32_t i = 0; i < 512; ++i) {
            maxSine = std::max(maxSine, std::abs(outBuf.getChannelData(ch)[i]));
        }
    }
    report(maxSine > 0.08f && maxSine < 0.11f, "SIGGEN-002: 1 kHz Sine Generates -20 dBFS Amplitude",
           "Peak=" + std::to_string(maxSine));

    // 8.3 100 Hz Sine
    sigGen.setSignalType(audiocore::TestSignalType::Sine100Hz);
    sigGen.process(inBuf, outBuf, 512);
    float max100 = 0.0f;
    for (uint32_t i = 0; i < 512; ++i) max100 = std::max(max100, std::abs(outBuf.getChannelData(0)[i]));
    report(max100 > 0.05f && max100 < 0.11f, "SIGGEN-003: 100 Hz Sine Generates -20 dBFS Amplitude");

    // 8.4 10 kHz Sine
    sigGen.setSignalType(audiocore::TestSignalType::Sine10kHz);
    sigGen.process(inBuf, outBuf, 512);
    float max10k = 0.0f;
    for (uint32_t i = 0; i < 512; ++i) max10k = std::max(max10k, std::abs(outBuf.getChannelData(0)[i]));
    report(max10k > 0.08f && max10k < 0.11f, "SIGGEN-004: 10 kHz Sine Generates -20 dBFS Amplitude");

    // 8.5 Log Sweep
    sigGen.setSignalType(audiocore::TestSignalType::LogSweep);
    sigGen.process(inBuf, outBuf, 512);
    float maxSweep = 0.0f;
    for (uint32_t i = 0; i < 512; ++i) maxSweep = std::max(maxSweep, std::abs(outBuf.getChannelData(0)[i]));
    report(maxSweep > 0.05f && maxSweep < 0.11f, "SIGGEN-005: Log Sweep Generates Bounded Energy");

    // 8.6 Pink Noise
    sigGen.setSignalType(audiocore::TestSignalType::PinkNoise);
    sigGen.process(inBuf, outBuf, 512);
    float maxPink = 0.0f;
    for (uint32_t i = 0; i < 512; ++i) maxPink = std::max(maxPink, std::abs(outBuf.getChannelData(0)[i]));
    report(maxPink > 0.01f && maxPink < 0.20f, "SIGGEN-006: Pink Noise Generates Non-Zero Bounded Energy");

    // 8.7 White Noise
    sigGen.setSignalType(audiocore::TestSignalType::WhiteNoise);
    sigGen.process(inBuf, outBuf, 512);
    float maxWhite = 0.0f;
    for (uint32_t i = 0; i < 512; ++i) maxWhite = std::max(maxWhite, std::abs(outBuf.getChannelData(0)[i]));
    report(maxWhite > 0.05f && maxWhite <= 0.1001f, "SIGGEN-007: White Noise Generates Bounded Energy");

    // 8.8 Safety Level Clamping
    sigGen.setLevelDb(12.0f); // request +12 dBFS
    report(sigGen.getLevelDb() == 0.0f, "SIGGEN-008: Positive dBFS Clamped to Strict 0 dBFS Ceiling");
    sigGen.setLevelDb(-120.0f); // request -120 dBFS
    report(sigGen.getLevelDb() == -96.0f, "SIGGEN-009: Negative dBFS Clamped to -96 dBFS Floor");

    // -------------------------------------------------------------------------
    // TEST 9: DSP Signal Path Integration
    // -------------------------------------------------------------------------
    std::cout << "\n--- 9. DSP Signal Path Integration (Generator -> Complete Chain) ---\n";
    audiocore::GainNode gain(48000.0);
    gain.setGainDb(-6.0f);
    audiocore::CompressorNode comp(48000.0);
    audiocore::LimiterNode limiter(48000.0);

    audiocore::AudioBuffer dspIn(2, 256);
    audiocore::AudioBuffer dspMid1(2, 256);
    audiocore::AudioBuffer dspMid2(2, 256);
    audiocore::AudioBuffer dspOut(2, 256);

    sigGen.setLevelDb(-12.0f);
    sigGen.setSignalType(audiocore::TestSignalType::Sine1kHz);
    sigGen.process(inBuf, dspIn, 256);
    gain.process(dspIn, dspMid1, 256);
    comp.process(dspMid1, dspMid2, 256);
    limiter.process(dspMid2, dspOut, 256);

    bool dspValid = true;
    for (uint32_t ch = 0; ch < 2; ++ch) {
        for (uint32_t i = 0; i < 256; ++i) {
            float v = dspOut.getChannelData(ch)[i];
            if (std::isnan(v) || std::isinf(v)) dspValid = false;
        }
    }
    report(dspValid, "DSPPATH-001: Signal Passes Through Complete Chain Without NaN/Inf");

    // -------------------------------------------------------------------------
    // TEST 10: Benchmark Matrix Execution
    // -------------------------------------------------------------------------
    std::cout << "\n--- 10. Automated Benchmark Matrix Execution ---\n";
    auto matrix = backend->runBenchmarkMatrix(50);
    report(!matrix.empty(), "BENCH-001: Benchmark Matrix Executed Across Configurations",
           std::to_string(matrix.size()) + " configurations evaluated");

    std::cout << "\n  Benchmark Matrix Results:\n";
    std::cout << "  ReqRate   ActRate   ReqBlock  ActBuffer Status        Avg (us)  Max (us)  RT %    Notes\n";
    std::cout << "  ----------------------------------------------------------------------------------------\n";
    size_t matrixPassed = 0, matrixUnsupported = 0, matrixFailed = 0;
    for (const auto& r : matrix) {
        if (r.status == "PASS") ++matrixPassed;
        else if (r.status == "UNSUPPORTED") ++matrixUnsupported;
        else if (r.status == "FAIL") ++matrixFailed;
        std::cout << "  " << std::left << std::setw(10) << static_cast<uint32_t>(r.sampleRate)
                  << std::setw(10) << static_cast<uint32_t>(r.actualSampleRate)
                  << std::setw(10) << r.requestedBuffer
                  << std::setw(10) << r.actualBuffer
                  << std::setw(14) << r.status
                  << std::setw(10) << std::fixed << std::setprecision(1) << r.avgCallbackUs
                  << std::setw(10) << std::fixed << std::setprecision(1) << r.maxCallbackUs
                  << std::setw(8) << std::fixed << std::setprecision(1) << r.utilizationPct
                  << r.notes.substr(0, 24) << "\n";
    }
    std::cout << "  Matrix totals: " << matrixPassed << " PASS, " << matrixUnsupported
              << " UNSUPPORTED, " << matrixFailed << " FAIL\n";

    // -------------------------------------------------------------------------
    // Final Summary
    // -------------------------------------------------------------------------
    std::cout << "\n============================================================\n";
    std::cout << "WASAPI DEVICE TEST SUMMARY: "
              << passed << " PASSED, "
              << failed << " FAILED, "
              << unsupported << " UNSUPPORTED, "
              << notRun << " NOT RUN\n";
    std::cout << "============================================================\n";

    return (failed == 0) ? 0 : 1;
}

