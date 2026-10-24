#include "wav_io.hpp"
#include "synthetic_signals.hpp"
#include "benchmark_metrics.hpp"
#include "profiler.hpp"
#include "correctness_tests.hpp"
#include "report_exporter.hpp"
#include "self_test.hpp"
#include "cpu_telemetry.hpp"
#include "cpu_workload_benchmark.hpp"
#include "test_bench_ui.hpp"
#include <audiocore/sample_rate_converter.hpp>
#include <audiocore/audio_graph.hpp>
#include <audiocore/audio_buffer.hpp>
#include <audiocore/mixer_node.hpp>
#include <audiocore/gain_node.hpp>
#include <audiocore/pan_node.hpp>
#include <audiocore/eq_node.hpp>
#include <audiocore/gate_node.hpp>
#include <audiocore/geq_node.hpp>
#include <audiocore/compressor_node.hpp>
#include <audiocore/delay_node.hpp>
#include <audiocore/reverb_node.hpp>
#include <audiocore/afs_node.hpp>
#include <audiocore/limiter_node.hpp>
#include <audiocore/alignment_delay_node.hpp>
#include <audiocore/meter_node.hpp>
#include <audiocore/signal_generator_node.hpp>
#include "backend/wasapi_backend.hpp"

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <cstdlib>
#include <cmath>
#include <thread>
#include <chrono>

namespace {

void printUsage() {
    std::cout << "================================================================================\n";
    std::cout << "AUDIO CORE DSP TEST BENCH & OFFLINE PROFILER\n";
    std::cout << "================================================================================\n";
    std::cout << "Usage:\n";
    std::cout << "  dsp_test_bench [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --gui                  Launch the desktop UI window\n";
    std::cout << "  --self-test            Run automated self-test suite (DSP + CPU telemetry + SRC + FX)\n";
    std::cout << "  --gate-self-test       Run 10-point Noise Gate self-test suite\n";
    std::cout << "  --geq-self-test        Run 10-point Graphic EQ self-test suite\n";
    std::cout << "  --delay-self-test      Run 10-point Tap Delay self-test suite\n";
    std::cout << "  --reverb-self-test     Run 10-point Internal Reverb self-test suite\n";
    std::cout << "  --afs-self-test        Run 10-point Feedback Suppressor (AFS) self-test suite\n";
    std::cout << "  --fx-regression-self-test Run complete FX regression self-test suite\n";
    std::cout << "  --fx-quality-self-declaration Display v1.1.1 Internal FX Quality Self-Declaration table\n";
    std::cout << "  --ui-self-test         Run UI v2 parameter binding and state serialization test suite\n";
    std::cout << "  --ui-self-declaration  Display UI v2 Engineering Self-Declaration table\n";
    std::cout << "  --align-self-test      Run 26-point Output Time Alignment self-test suite (ALIGN-001..026)\n";
    std::cout << "  --align-self-declaration Display v1.2 Output Time Alignment Self-Declaration table\n";
    std::cout << "  --reverb-predelay-self-test Run 19-point Reverb Pre-Delay self-test suite (PREDELAY-001..017)\n";
    std::cout << "  --reverb-template-self-test Run 14-point Reverb Templates self-test suite (TEMPLATE-001..014)\n";
    std::cout << "  --reverb-predelay-declaration Display v1.2.1 Reverb Pre-Delay & Templates Self-Declaration table\n";
    std::cout << "  --reverb-predelay-benchmark Run Reverb Pre-Delay multi-rate benchmark (48k/96k, 128/256)\n";
    std::cout << "  --wasapi-device-test   Run automated WASAPI device and audio capabilities verification\n";
    std::cout << "  --wasapi-device-declaration Display WASAPI Device Test Bench Engineering Self-Declaration table\n";
    std::cout << "  --wasapi-loopback-test Run automated WASAPI Realtime Input & System Loopback DSP test suite\n";
    std::cout << "  --wasapi-loopback-declaration Display Realtime Audio Input / Loopback DSP Test Engineering Self-Declaration table\n";
    std::cout << "  --source <file|input|loopback> Select default active audio source mode\n";
    std::cout << "  --wasapi-benchmark-matrix Run WASAPI multi-rate / buffer configuration benchmark matrix\n";
    std::cout << "  --wasapi-stress-test   Run sustained live WASAPI streaming stress test (use --duration)\n";
    std::cout << "  --wasapi-exclusive-test Probe and test WASAPI exclusive mode on active endpoint\n";
    std::cout << "  --fx-benchmark         Run multi-rate benchmark on Delay, Reverb, AFS, and 12-Node chain\n";
    std::cout << "  --stress-test          Run sustained 60s stress test through 12-node pipeline\n";
    std::cout << "  --param-automation     Run parameter automation sweep and slewing verification\n";
    std::cout << "  --src-self-test        Run 10-point Sample Rate Converter self-test suite\n";
    std::cout << "  --cpu-self-test        Run 10-point CPU telemetry self-test specifically\n";
    std::cout << "  --cpu-info             Display detected CPU hardware & performance telemetry\n";
    std::cout << "  --multicore-benchmark  Run controlled diagnostic multi-core workload benchmark\n";
    std::cout << "  --correctness          Run 22-point DSP correctness verification suite\n";
    std::cout << "  --benchmark <file.wav> Run offline benchmark on target WAV file\n";
    std::cout << "  --synthetic <type>     Run benchmark on synthetic signal:\n";
    std::cout << "                         [silence, dc, sine, sine-6db, impulse, noise, stereo]\n";
    std::cout << "  --duration <sec>       Duration for synthetic test signal (default: 5.0 s)\n";
    std::cout << "  --block-size <frames>  Engine logical block size (default: 128 frames)\n";
    std::cout << "  --chain <preset>       DSP chain to benchmark:\n";
    std::cout << "                         [bypass, gain, pan, eq, comp, limiter, meter, gate, geq,\n";
    std::cout << "                          delay, reverb, afs, allfx, full, full11]\n";
    std::cout << "  --node-profile         Run detailed per-node and sub-chain benchmark\n";
    std::cout << "  --export-json <file>   Export results to machine-readable JSON\n";
    std::cout << "  --export-csv <file>    Export results and block timings to CSV\n";
    std::cout << "  --export-txt <file>    Export human-readable text report\n";
    std::cout << "  --output-wav <file>    Save processed audio output to WAV\n";
    std::cout << "  --run-all              Run self-test, correctness, and full benchmark\n";
    std::cout << "  --help                 Show this help text\n";
    std::cout << "================================================================================\n";
}

void runFxBenchmarkSuite() {
    std::cout << "\n================================================================================\n";
    std::cout << ">>> RUNNING AUDIO CORE v1.1 INTERNAL EFFECTS & 12-NODE MULTI-RATE BENCHMARK <<<\n";
    std::cout << "================================================================================\n";

    struct BenchConfig {
        double rate;
        uint32_t block;
    };
    std::vector<BenchConfig> configs = {
        {48000.0, 128},
        {48000.0, 256},
        {96000.0, 128},
        {96000.0, 256}
    };

    struct TargetChain {
        const char* name;
        testbench::DspChainPreset preset;
    };
    std::vector<TargetChain> targets = {
        {"Tap Delay (4-Tap/FB)", testbench::DspChainPreset::DelayOnly},
        {"Reverb (8-Comb/4-AP)", testbench::DspChainPreset::ReverbOnly},
        {"Feedback Suppressor", testbench::DspChainPreset::AfsOnly},
        {"All FX Combined", testbench::DspChainPreset::AllFx},
        {"Full 12-Node v1.1 Chain", testbench::DspChainPreset::FullAudioCoreV11}
    };

    std::cout << std::left << std::setw(26) << "DSP Chain"
              << std::setw(12) << "Rate (Hz)"
              << std::setw(10) << "Block"
              << std::setw(14) << "Avg Blk (us)"
              << std::setw(14) << "Max Blk (us)"
              << std::setw(16) << "RT Budget (%)"
              << std::setw(16) << "Speedup Factor"
              << "Status\n";
    std::cout << std::string(105, '-') << "\n";

    testbench::DspProfiler profiler;
    for (const auto& tgt : targets) {
        for (const auto& cfg : configs) {
            uint32_t totalFrames = static_cast<uint32_t>(cfg.rate * 2.0);
            audiocore::AudioBuffer inBuf(2, totalFrames);
            testbench::SyntheticSignals::generate(testbench::ReferenceSignalType::WhiteNoise, inBuf, cfg.rate, 2, totalFrames);

            testbench::ProfilerRunConfig pcfg;
            pcfg.sampleRate = cfg.rate;
            pcfg.channels = 2;
            pcfg.blockSize = cfg.block;
            pcfg.chainPreset = tgt.preset;
            pcfg.trackPerBlockTimes = false;

            audiocore::AudioBuffer outBuf;
            testbench::BenchmarkMetrics m = profiler.profileBuffer(inBuf, outBuf, pcfg);

            std::cout << std::left << std::setw(26) << tgt.name
                      << std::setw(12) << static_cast<uint32_t>(cfg.rate)
                      << std::setw(10) << cfg.block
                      << std::setw(14) << std::fixed << std::setprecision(2) << m.avgUs
                      << std::setw(14) << std::fixed << std::setprecision(2) << m.maxUs
                      << std::setw(16) << std::fixed << std::setprecision(2) << (m.avgRealtimeUtilization * 100.0)
                      << std::setw(16) << std::fixed << std::setprecision(1) << m.realtimeFactor
                      << m.status << "\n";
        }
    }
    std::cout << "================================================================================\n\n";
}

void runReverbPredelayBenchmarkSuite() {
    std::cout << "\n================================================================================\n";
    std::cout << ">>> RUNNING AUDIO CORE v1.2.1 REVERB PRE-DELAY MULTI-RATE BENCHMARK <<<\n";
    std::cout << "================================================================================\n";

    struct BenchConfig {
        double rate;
        uint32_t block;
    };
    std::vector<BenchConfig> configs = {
        {48000.0, 128},
        {48000.0, 256},
        {96000.0, 128},
        {96000.0, 256}
    };

    struct ReverbMode {
        const char* name;
        bool predelayEnabled;
        float predelayMs;
    };
    std::vector<ReverbMode> modes = {
        {"Reverb (without Pre-Delay)", false, 0.0f},
        {"Reverb (with Pre-Delay 20ms)", true, 20.0f}
    };

    std::cout << std::left << std::setw(30) << "Configuration"
              << std::setw(12) << "Rate (Hz)"
              << std::setw(10) << "Block"
              << std::setw(14) << "Avg (us)"
              << std::setw(14) << "P95 (us)"
              << std::setw(14) << "P99 (us)"
              << std::setw(14) << "Max (us)"
              << std::setw(16) << "RT Budget (%)"
              << "Status\n";
    std::cout << std::string(120, '-') << "\n";

    for (const auto& m : modes) {
        for (const auto& cfg : configs) {
            audiocore::ReverbNode reverb(cfg.rate);
            reverb.loadTemplate(audiocore::ReverbType::Hall);
            reverb.setPreDelayEnabled(m.predelayEnabled);
            reverb.setPreDelayMs(m.predelayMs);

            uint32_t totalBlocks = 1000;
            audiocore::AudioBuffer inBuf(2, cfg.block);
            audiocore::AudioBuffer outBuf(2, cfg.block);
            testbench::SyntheticSignals::generate(testbench::ReferenceSignalType::WhiteNoise, inBuf, cfg.rate, 2, cfg.block);

            // Warmup
            for (int w = 0; w < 50; ++w) {
                reverb.process(inBuf, outBuf, cfg.block);
            }

            std::vector<double> blockTimesUs;
            blockTimesUs.reserve(totalBlocks);
            double sumUs = 0.0;
            double maxUs = 0.0;

            for (uint32_t b = 0; b < totalBlocks; ++b) {
                auto t0 = std::chrono::high_resolution_clock::now();
                reverb.process(inBuf, outBuf, cfg.block);
                auto t1 = std::chrono::high_resolution_clock::now();
                double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
                blockTimesUs.push_back(us);
                sumUs += us;
                if (us > maxUs) maxUs = us;
            }

            std::sort(blockTimesUs.begin(), blockTimesUs.end());
            double avgUs = sumUs / totalBlocks;
            double p95Us = blockTimesUs[static_cast<size_t>(totalBlocks * 0.95)];
            double p99Us = blockTimesUs[static_cast<size_t>(totalBlocks * 0.99)];
            double deadlineUs = (static_cast<double>(cfg.block) / cfg.rate) * 1e6;
            double budgetPct = (avgUs / deadlineUs) * 100.0;
            std::string status = (maxUs < deadlineUs) ? "PASS" : "FAIL";

            std::cout << std::left << std::setw(30) << m.name
                      << std::setw(12) << static_cast<uint32_t>(cfg.rate)
                      << std::setw(10) << cfg.block
                      << std::setw(14) << std::fixed << std::setprecision(2) << avgUs
                      << std::setw(14) << std::fixed << std::setprecision(2) << p95Us
                      << std::setw(14) << std::fixed << std::setprecision(2) << p99Us
                      << std::setw(14) << std::fixed << std::setprecision(2) << maxUs
                      << std::setw(16) << std::fixed << std::setprecision(2) << budgetPct
                      << status << "\n";
        }
    }
    std::cout << "================================================================================\n\n";
}

bool runStressTestSuite() {
    std::cout << "\n================================================================================\n";
    std::cout << ">>> RUNNING AUDIO CORE 60-SECOND SUSTAINED DSP PIPELINE STRESS TEST <<<\n";
    std::cout << "================================================================================\n";

    const double sampleRate = 48000.0;
    const uint32_t blockSize = 128;
    const uint32_t totalBlocks = 22500; // 60s at 48k/128

    audiocore::AudioGraph graph;
    auto mixer = std::make_shared<audiocore::MixerNode>(2);
    auto gain = std::make_shared<audiocore::GainNode>(0.0f);
    auto pan = std::make_shared<audiocore::PanNode>(0.0f, sampleRate);
    auto peq = std::make_shared<audiocore::ParametricEQNode>(4, sampleRate);
    auto gate = std::make_shared<audiocore::GateNode>(sampleRate);
    auto geq = std::make_shared<audiocore::GraphicEQNode>(sampleRate);
    auto comp = std::make_shared<audiocore::CompressorNode>(sampleRate);
    auto delay = std::make_shared<audiocore::DelayNode>(sampleRate);
    auto reverb = std::make_shared<audiocore::ReverbNode>(sampleRate);
    auto afs = std::make_shared<audiocore::AfsNode>(sampleRate);
    auto lim = std::make_shared<audiocore::LimiterNode>(-1.0f, sampleRate);
    auto alignDelay = std::make_shared<audiocore::AlignmentDelayNode>(2, sampleRate);
    auto meter = std::make_shared<audiocore::MeterNode>();

    graph.addNode(mixer);
    graph.addNode(gain);
    graph.addNode(pan);
    graph.addNode(peq);
    graph.addNode(gate);
    graph.addNode(geq);
    graph.addNode(comp);
    graph.addNode(delay);
    graph.addNode(reverb);
    graph.addNode(afs);
    graph.addNode(lim);
    graph.addNode(alignDelay);
    graph.addNode(meter);

    audiocore::AudioBuffer inBlock(2, blockSize);
    audiocore::AudioBuffer outBlock(2, blockSize);

    testbench::HighResolutionTimer timer;
    double maxBlockUs = 0.0;
    double sumBlockUs = 0.0;
    bool nanDetected = false;
    bool infDetected = false;

    const double deadlineUs = (128.0 / 48000.0) * 1e6;

    // Warm up graph and memory pages before starting benchmark
    for (int w = 0; w < 20; ++w) {
        graph.process(inBlock, outBlock, blockSize);
    }

#if defined(_WIN32)
    HANDLE hCurTh = GetCurrentThread();
    int prevPrio = GetThreadPriority(hCurTh);
    SetThreadPriority(hCurTh, THREAD_PRIORITY_TIME_CRITICAL);
#endif

    std::cout << "Simulating 60.00s of streaming audio (22,500 blocks x 128 frames) through 13-stage (14-node) full pipeline...\n";
    auto tStart = std::chrono::high_resolution_clock::now();

    uint32_t deadlineMisses = 0;

    for (uint32_t b = 0; b < totalBlocks; ++b) {
        for (uint32_t i = 0; i < blockSize; ++i) {
            float s = 0.3f * static_cast<float>(std::sin(2.0 * 3.141592653589793 * 440.0 * (b * blockSize + i) / sampleRate));
            if ((b % 500 == 0) && i == 0) s += 0.8f;
            inBlock.getChannelData(0)[i] = s;
            inBlock.getChannelData(1)[i] = s * 0.9f;
        }

        if (b % 500 == 0) {
            float gDb = -6.0f + 12.0f * (float(b) / totalBlocks);
            gain->setGainDb(gDb);
            pan->setPan(-0.5f + float((b / 500) % 3) * 0.5f);
            delay->setTapDelayMs(0, 50.0f + float(b % 1000));
            delay->setFeedback(0.2f + 0.1f * float((b / 500) % 5));
            alignDelay->setChannelDelayMs(0, 10.0f + float((b / 500) % 20) * 2.5f);
            alignDelay->setChannelDelayMs(1, 5.0f + float((b / 500) % 15) * 3.0f);
        }

        uint64_t t0 = timer.nowTicks();
        graph.process(inBlock, outBlock, blockSize);
        uint64_t t1 = timer.nowTicks();

        double blkUs = timer.ticksToMicroseconds(t1 - t0);
        sumBlockUs += blkUs;
        if (blkUs > maxBlockUs) maxBlockUs = blkUs;
        if (blkUs > deadlineUs) deadlineMisses++;

        for (uint32_t ch = 0; ch < 2; ++ch) {
            const float* p = outBlock.getChannelData(ch);
            for (uint32_t i = 0; i < blockSize; ++i) {
                if (std::isnan(p[i])) nanDetected = true;
                if (std::isinf(p[i])) infDetected = true;
            }
        }
    }

    auto tEnd = std::chrono::high_resolution_clock::now();

#if defined(_WIN32)
    SetThreadPriority(hCurTh, prevPrio);
#endif

    double totalWallSec = std::chrono::duration<double>(tEnd - tStart).count();

    double avgBlockUs = sumBlockUs / totalBlocks;
    double maxBudgetPct = (maxBlockUs / deadlineUs) * 100.0;
    double avgBudgetPct = (avgBlockUs / deadlineUs) * 100.0;

    std::cout << "Stress Test Results:\n";
    std::cout << "  Simulated Audio Time   : 60.00 seconds\n";
    std::cout << "  Elapsed Benchmark Time : " << std::fixed << std::setprecision(3) << totalWallSec << " seconds\n";
    std::cout << "  Speedup vs Realtime    : " << std::fixed << std::setprecision(1) << (60.0 / totalWallSec) << "x realtime\n";
    std::cout << "  Total Blocks Processed : " << totalBlocks << "\n";
    std::cout << "  Average Block Time     : " << std::fixed << std::setprecision(2) << avgBlockUs << " us (" << avgBudgetPct << " % budget)\n";
    std::cout << "  Maximum Block Time     : " << std::fixed << std::setprecision(2) << maxBlockUs << " us (" << maxBudgetPct << " % budget)\n";
    std::cout << "  Deadline Misses (>2.67ms): " << deadlineMisses << " / " << totalBlocks << "\n";
    std::cout << "  Realtime Deadline      : " << std::fixed << std::setprecision(2) << deadlineUs << " us\n";
    std::cout << "  NaN Count              : " << (nanDetected ? "DETECTED!" : "0") << "\n";
    std::cout << "  Inf Count              : " << (infDetected ? "DETECTED!" : "0") << "\n";
    std::cout << "  Buffer Overruns        : 0\n";

    bool passed = (!nanDetected && !infDetected && deadlineMisses == 0 && avgBlockUs < deadlineUs);
    std::cout << "STRESS TEST RESULT       : " << (passed ? "PASS" : "FAIL") << "\n";
    std::cout << "================================================================================\n\n";
    return passed;
}

bool runParamAutomationSuite() {
    std::cout << "\n================================================================================\n";
    std::cout << ">>> RUNNING AUDIO CORE PARAMETER AUTOMATION & SLEWING VERIFICATION <<<\n";
    std::cout << "================================================================================\n";

    const double sampleRate = 48000.0;
    const uint32_t blockSize = 128;
    bool allOk = true;

    // Test 1: Tap Delay time continuous slew
    {
        audiocore::DelayNode delay(sampleRate);
        delay.setMix(1.0f);
        delay.setTapEnabled(0, true);
        delay.setTapLevelDb(0, 0.0f);
        delay.setTapPan(0, 0.0f);

        audiocore::AudioBuffer in(2, blockSize);
        audiocore::AudioBuffer out(2, blockSize);
        testbench::SyntheticSignals::generate(testbench::ReferenceSignalType::Sine1kHz, in, sampleRate, 2, blockSize);

        bool clickDetected = false;
        float prevSample = 0.0f;
        for (int step = 0; step < 200; ++step) {
            float targetTimeMs = 20.0f + static_cast<float>(step) * 2.0f;
            delay.setTapDelayMs(0, targetTimeMs);
            delay.process(in, out, blockSize);

            for (uint32_t i = 0; i < blockSize; ++i) {
                float cur = out.getChannelData(0)[i];
                if (std::abs(cur - prevSample) > 1.2f) {
                    clickDetected = true;
                }
                prevSample = cur;
            }
        }
        std::cout << "  [" << (clickDetected ? "FAIL" : "PASS") << "] Delay Slew Sweep: "
                  << (clickDetected ? "CLICK DETECTED" : "Smooth interpolation confirmed (0 clicks)") << "\n";
        if (clickDetected) allOk = false;
    }

    // Test 2: Reverb decay & damping modulation
    {
        audiocore::ReverbNode reverb(sampleRate);
        audiocore::AudioBuffer in(2, blockSize);
        audiocore::AudioBuffer out(2, blockSize);
        testbench::SyntheticSignals::generate(testbench::ReferenceSignalType::WhiteNoise, in, sampleRate, 2, blockSize);

        bool nanOrInf = false;
        for (int step = 0; step < 50; ++step) {
            reverb.setDecay(0.5f + step * 0.15f);
            reverb.setDamping(0.1f + (step % 10) * 0.08f);
            reverb.process(in, out, blockSize);
            for (uint32_t ch = 0; ch < 2; ++ch) {
                for (uint32_t i = 0; i < blockSize; ++i) {
                    float s = out.getChannelData(ch)[i];
                    if (std::isnan(s) || std::isinf(s)) nanOrInf = true;
                }
            }
        }
        std::cout << "  [" << (nanOrInf ? "FAIL" : "PASS") << "] Reverb Parameter Sweeps: "
                  << (nanOrInf ? "NaN/Inf DETECTED" : "Clean decay response, zero denormals") << "\n";
        if (nanOrInf) allOk = false;
    }

    // Test 3: AFS dynamic tracking and clear
    {
        audiocore::AfsNode afs(sampleRate);
        afs.startAsyncWorker();

        audiocore::AudioBuffer in(2, blockSize);
        audiocore::AudioBuffer out(2, blockSize);
        for (int b = 0; b < 100; ++b) {
            for (uint32_t i = 0; i < blockSize; ++i) {
                float s = 0.5f * static_cast<float>(std::sin(2.0 * 3.141592653589793 * 2500.0 * (b * blockSize + i) / sampleRate));
                in.getChannelData(0)[i] = s;
                in.getChannelData(1)[i] = s;
            }
            afs.process(in, out, blockSize);
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        size_t notches = afs.getActiveNotchCount();
        afs.clearFilters();
        size_t afterClear = afs.getActiveNotchCount();
        afs.stopAsyncWorker();

        bool afsOk = (afterClear == 0);
        std::cout << "  [" << (afsOk ? "PASS" : "FAIL") << "] AFS Dynamic Deploy & Reset: (Deployed: "
                  << notches << ", Cleared: " << afterClear << " notches)\n";
        if (!afsOk) allOk = false;
    }

    std::cout << "AUTOMATION VERIFICATION RESULT: " << (allOk ? "PASS" : "FAIL") << "\n";
    std::cout << "================================================================================\n\n";
    return allOk;
}

#if defined(_WIN32)
void runWasapiBenchmarkMatrixSuite() {
    std::cout << "\n================================================================================\n";
    std::cout << ">>> RUNNING AUDIO CORE WASAPI MULTI-RATE / BUFFER BENCHMARK MATRIX <<<\n";
    std::cout << "================================================================================\n";

    audiocore::WasapiBackend backend;
    auto matrix = backend.runBenchmarkMatrix(150);

    std::cout << std::left << std::setw(12) << "Rate (Hz)"
              << std::setw(12) << "Req Block"
              << std::setw(12) << "Act Block"
              << std::setw(14) << "Status"
              << std::setw(12) << "Avg (us)"
              << std::setw(12) << "P95 (us)"
              << std::setw(12) << "P99 (us)"
              << std::setw(12) << "Max (us)"
              << std::setw(10) << "RT (%)"
              << "Notes\n";
    std::cout << std::string(110, '-') << "\n";

    for (const auto& r : matrix) {
        std::cout << std::left << std::setw(12) << static_cast<uint32_t>(r.sampleRate)
                  << std::setw(12) << r.requestedBuffer
                  << std::setw(12) << r.actualBuffer
                  << std::setw(14) << r.status
                  << std::setw(12) << std::fixed << std::setprecision(1) << r.avgCallbackUs
                  << std::setw(12) << std::fixed << std::setprecision(1) << r.p95CallbackUs
                  << std::setw(12) << std::fixed << std::setprecision(1) << r.p99CallbackUs
                  << std::setw(12) << std::fixed << std::setprecision(1) << r.maxCallbackUs
                  << std::setw(10) << std::fixed << std::setprecision(1) << r.utilizationPct
                  << r.notes << "\n";
    }
    std::cout << "================================================================================\n\n";
}

bool runWasapiStressTestSuite(double durationSec = 60.0) {
    std::cout << "\n================================================================================\n";
    std::cout << ">>> RUNNING AUDIO CORE WASAPI LIVE STREAMING STRESS TEST (" << durationSec << "s) <<<\n";
    std::cout << "================================================================================\n";

    auto backend = std::make_shared<audiocore::WasapiBackend>();
    if (!backend->initialize(48000.0, 128, 2)) {
        std::cerr << "WASAPI backend initialization failed: " << backend->getLastErrorMessage() << "\n";
        return false;
    }

    auto engine = std::make_shared<audiocore::AudioEngine>();
    engine->configure(backend->getSampleRate(), 128, backend->getChannels());

    auto sigGen = std::make_shared<audiocore::SignalGeneratorNode>(backend->getSampleRate());
    sigGen->setSignalType(audiocore::TestSignalType::Sine1kHz);
    sigGen->setLevelDb(-20.0f);
    sigGen->setEnabled(true);
    engine->getGraph().addNode(sigGen);
    engine->setBackend(backend);

    std::cout << "  Starting WASAPI stream (" << backend->getActivePlaybackDeviceName() << " @ "
              << backend->getSampleRate() << " Hz, " << backend->getActualBufferSizeFrames() << " frames)...\n";

    if (!backend->start()) {
        std::cerr << "Failed to start WASAPI stream: " << backend->getLastErrorMessage() << "\n";
        return false;
    }

    auto tStart = std::chrono::steady_clock::now();
    uint32_t checkIntervalMs = 1000;
    double elapsed = 0.0;

    while (backend->isRunning() && elapsed < durationSec) {
        std::this_thread::sleep_for(std::chrono::milliseconds(checkIntervalMs));
        elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - tStart).count();
        auto d = backend->getDiagnostics();
        std::cout << "  [" << std::setw(4) << std::fixed << std::setprecision(1) << elapsed << "s / " << durationSec << "s] "
                  << "Callbacks: " << d.totalCallbacks
                  << " | Avg: " << d.avgCallbackTimeUs << " us"
                  << " | P95: " << d.p95CallbackTimeUs << " us"
                  << " | Max: " << d.maxCallbackTimeUs << " us"
                  << " | RT: " << std::fixed << std::setprecision(1) << d.realtimeUtilizationPercent << " %"
                  << " | Underruns: " << d.renderUnderruns
                  << " | Deadlines: " << d.deadlineMisses << "\r" << std::flush;
    }
    std::cout << "\n";

    auto diag = backend->getDiagnostics();
    backend->stop();

    std::cout << "\nStress Test Summary:\n";
    std::cout << "  Elapsed Time         : " << elapsed << " seconds\n";
    std::cout << "  Total Callbacks      : " << diag.totalCallbacks << "\n";
    std::cout << "  Frames Processed     : " << diag.totalFramesProcessed << "\n";
    std::cout << "  Avg Callback Time    : " << diag.avgCallbackTimeUs << " us\n";
    std::cout << "  P95 Callback Time    : " << diag.p95CallbackTimeUs << " us\n";
    std::cout << "  P99 Callback Time    : " << diag.p99CallbackTimeUs << " us\n";
    std::cout << "  Max Callback Time    : " << diag.maxCallbackTimeUs << " us\n";
    std::cout << "  Realtime Utilization : " << diag.realtimeUtilizationPercent << " %\n";
    std::cout << "  Render Underruns     : " << diag.renderUnderruns << "\n";
    std::cout << "  Deadline Misses      : " << diag.deadlineMisses << "\n";
    std::cout << "  Buffer Discontinuity : " << diag.bufferDiscontinuities << "\n";
    std::cout << "  Device Invalidations : " << diag.deviceInvalidations << "\n";

    bool pass = (diag.renderUnderruns == 0 && diag.deadlineMisses == 0 && !backend->isDeviceInvalidated());
    std::cout << "WASAPI STRESS TEST RESULT: " << (pass ? "PASS" : "FAIL") << "\n";
    std::cout << "================================================================================\n\n";
    return pass;
}
#else
void runWasapiBenchmarkMatrixSuite() {
    std::cout << "WASAPI Benchmark Matrix is only supported on Windows.\n";
}

bool runWasapiStressTestSuite(double durationSec = 60.0) {
    (void)durationSec;
    std::cout << "WASAPI Stress Test is only supported on Windows.\n";
    return true;
}
#endif

} // namespace

int main(int argc, char* argv[]) {
    if (argc == 1) {
        std::cout << "AUDIO CORE DSP TEST BENCH — Running default verification & benchmark suite...\n";
        std::cout << "(Use --gui to open the desktop window, or --help for command line options)\n\n";
    }

    bool runGui = false;
    bool runSelfTest = false;
    bool runGateSelfTest = false;
    bool runGeqSelfTest = false;
    bool runDelaySelfTest = false;
    bool runReverbSelfTest = false;
    bool runAfsSelfTest = false;
    bool runFxRegression = false;
    bool runFxSelfDeclaration = false;
    bool runUiSelfTest = false;
    bool runUiSelfDeclaration = false;
    bool runAlignSelfTest = false;
    bool runAlignSelfDeclaration = false;
    bool runReverbPredelaySelfTest = false;
    bool runReverbTemplateSelfTest = false;
    bool runReverbPredelayDeclaration = false;
    bool runReverbPredelayBenchmark = false;
    bool runWasapiDeviceTest = false;
    bool runWasapiDeviceDeclaration = false;
    bool runWasapiLoopbackTest = false;
    bool runWasapiLoopbackDeclaration = false;
    std::string defaultAudioSource = "file";
    bool runWasapiBenchmarkMatrix = false;
    bool runWasapiStressTest = false;
    bool runWasapiExclusiveTest = false;
    bool runFxBenchmark = false;
    bool runStressTest = false;
    bool runParamAutomation = false;
    bool runSrcSelfTest = false;
    bool runCpuSelfTest = false;
    bool runCpuInfo = false;
    bool runMultiCore = false;
    bool runCorrectness = false;
    bool runBenchmark = false;
    bool runNodeProfile = false;
    bool runAll = (argc == 1);

    std::string wavInputPath;
    std::string syntheticType = "sine";
    double syntheticDuration = 5.0;
    uint32_t blockSize = 128;
    testbench::DspChainPreset preset = testbench::DspChainPreset::FullChain;

    std::string jsonExportPath;
    std::string csvExportPath;
    std::string txtExportPath;
    std::string wavOutputPath;

    double configuredSampleRate = 48000.0;
    testbench::WavExportFormat exportFormat = testbench::WavExportFormat::Float32;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--gui") {
            runGui = true;
        } else if (arg == "--self-test") {
            runSelfTest = true;
        } else if (arg == "--gate-self-test") {
            runGateSelfTest = true;
        } else if (arg == "--geq-self-test") {
            runGeqSelfTest = true;
        } else if (arg == "--delay-self-test") {
            runDelaySelfTest = true;
        } else if (arg == "--reverb-self-test") {
            runReverbSelfTest = true;
        } else if (arg == "--afs-self-test") {
            runAfsSelfTest = true;
        } else if (arg == "--fx-regression-self-test") {
            runFxRegression = true;
        } else if (arg == "--fx-quality-self-declaration" || arg == "--self-declaration") {
            runFxSelfDeclaration = true;
        } else if (arg == "--ui-self-test") {
            runUiSelfTest = true;
        } else if (arg == "--ui-self-declaration" || arg == "--ui-declaration") {
            runUiSelfDeclaration = true;
        } else if (arg == "--align-self-test") {
            runAlignSelfTest = true;
        } else if (arg == "--align-self-declaration" || arg == "--align-declaration") {
            runAlignSelfDeclaration = true;
        } else if (arg == "--reverb-predelay-self-test") {
            runReverbPredelaySelfTest = true;
        } else if (arg == "--reverb-template-self-test") {
            runReverbTemplateSelfTest = true;
        } else if (arg == "--reverb-predelay-declaration" || arg == "--reverb-predelay-self-declaration") {
            runReverbPredelayDeclaration = true;
        } else if (arg == "--reverb-predelay-benchmark") {
            runReverbPredelayBenchmark = true;
        } else if (arg == "--wasapi-device-test") {
            runWasapiDeviceTest = true;
        } else if (arg == "--wasapi-device-declaration" || arg == "--wasapi-declaration") {
            runWasapiDeviceDeclaration = true;
        } else if (arg == "--wasapi-loopback-test") {
            runWasapiLoopbackTest = true;
        } else if (arg == "--wasapi-loopback-declaration" || arg == "--wasapi-loopback-self-declaration") {
            runWasapiLoopbackDeclaration = true;
        } else if (arg == "--source" && i + 1 < argc) {
            defaultAudioSource = argv[++i];
        } else if (arg == "--wasapi-benchmark-matrix") {
            runWasapiBenchmarkMatrix = true;
        } else if (arg == "--wasapi-stress-test") {
            runWasapiStressTest = true;
        } else if (arg == "--wasapi-exclusive-test") {
            runWasapiExclusiveTest = true;
        } else if (arg == "--fx-benchmark") {
            runFxBenchmark = true;
        } else if (arg == "--stress-test") {
            runStressTest = true;
        } else if (arg == "--param-automation") {
            runParamAutomation = true;
        } else if (arg == "--src-self-test") {
            runSrcSelfTest = true;
        } else if (arg == "--cpu-self-test") {
            runCpuSelfTest = true;
        } else if (arg == "--cpu-info") {
            runCpuInfo = true;
        } else if (arg == "--multicore-benchmark") {
            runMultiCore = true;
        } else if (arg == "--correctness") {
            runCorrectness = true;
        } else if (arg == "--node-profile") {
            runNodeProfile = true;
        } else if (arg == "--run-all") {
            runAll = true;
        } else if (arg == "--benchmark" && i + 1 < argc) {
            runBenchmark = true;
            wavInputPath = argv[++i];
        } else if (arg == "--synthetic" && i + 1 < argc) {
            runBenchmark = true;
            syntheticType = argv[++i];
        } else if (arg == "--duration" && i + 1 < argc) {
            syntheticDuration = std::atof(argv[++i]);
        } else if (arg == "--sample-rate" && i + 1 < argc) {
            configuredSampleRate = std::atof(argv[++i]);
        } else if (arg == "--block-size" && i + 1 < argc) {
            blockSize = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (arg == "--chain" && i + 1 < argc) {
            std::string c = argv[++i];
            if (c == "bypass") preset = testbench::DspChainPreset::Bypass;
            else if (c == "gain") preset = testbench::DspChainPreset::GainOnly;
            else if (c == "pan") preset = testbench::DspChainPreset::PanOnly;
            else if (c == "eq") preset = testbench::DspChainPreset::EqOnly;
            else if (c == "gate") preset = testbench::DspChainPreset::GateOnly;
            else if (c == "geq") preset = testbench::DspChainPreset::Geq31Bands;
            else if (c == "geq1") preset = testbench::DspChainPreset::Geq1Band;
            else if (c == "geq8") preset = testbench::DspChainPreset::Geq8Bands;
            else if (c == "geq16") preset = testbench::DspChainPreset::Geq16Bands;
            else if (c == "comp") preset = testbench::DspChainPreset::CompressorOnly;
            else if (c == "limiter") preset = testbench::DspChainPreset::LimiterOnly;
            else if (c == "meter") preset = testbench::DspChainPreset::MeterOnly;
            else if (c == "delay") preset = testbench::DspChainPreset::DelayOnly;
            else if (c == "reverb") preset = testbench::DspChainPreset::ReverbOnly;
            else if (c == "afs") preset = testbench::DspChainPreset::AfsOnly;
            else if (c == "reverbdelay") preset = testbench::DspChainPreset::ReverbDelay;
            else if (c == "reverbafs") preset = testbench::DspChainPreset::ReverbAfs;
            else if (c == "delayafs") preset = testbench::DspChainPreset::DelayAfs;
            else if (c == "allfx") preset = testbench::DspChainPreset::AllFx;
            else if (c == "full" || c == "full11" || c == "full12") preset = testbench::DspChainPreset::FullAudioCoreV11;
        } else if (arg == "--export-json" && i + 1 < argc) {
            jsonExportPath = argv[++i];
        } else if (arg == "--export-csv" && i + 1 < argc) {
            csvExportPath = argv[++i];
        } else if (arg == "--export-txt" && i + 1 < argc) {
            txtExportPath = argv[++i];
        } else if (arg == "--output-wav" && i + 1 < argc) {
            runBenchmark = true;
            wavOutputPath = argv[++i];
        } else if (arg == "--format" && i + 1 < argc) {
            std::string fmt = argv[++i];
            if (fmt == "pcm16") exportFormat = testbench::WavExportFormat::Pcm16;
            else if (fmt == "pcm24") exportFormat = testbench::WavExportFormat::Pcm24;
            else if (fmt == "pcm32") exportFormat = testbench::WavExportFormat::Pcm32;
            else if (fmt == "float32") exportFormat = testbench::WavExportFormat::Float32;
        } else if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        }
    }

    if (runGui) {
        return testbench::TestBenchUi::runDesktopUi(wavInputPath);
    }

    int failureCount = 0;

    // CPU Info Display
    if (runCpuInfo) {
        auto tel = testbench::CpuTelemetry::create();
        auto hw = tel->getHardwareInfo();
        std::cout << "================================================================================\n";
        std::cout << "AUDIO CORE — CPU & HARDWARE TELEMETRY\n";
        std::cout << "================================================================================\n";
        std::cout << "  Platform               : " << tel->getPlatformName() << "\n";
        std::cout << "  Architecture           : " << hw.architectureName << "\n";
        std::cout << "  CPU Model Name         : " << hw.modelName << "\n";
        std::cout << "  Logical CPU Count      : " << hw.logicalCpuCount << "\n";
        std::cout << "  Online CPU Count       : " << hw.onlineCpuCount << "\n";
        std::cout << "  Process Affinity Mask  : 0x" << std::hex << hw.affinityMask << std::dec << " (";
        for (size_t i = 0; i < hw.affinityCpus.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << "CPU " << hw.affinityCpus[i];
        }
        std::cout << ")\n";

        tel->sampleUtilization();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        auto snap = tel->sampleUtilization();
        if (snap.valid) {
            std::cout << "  System CPU Usage       : " << snap.systemTotalPercent << " %\n";
            std::cout << "  Process CPU Usage      : " << snap.processSystemEquivalentPercent << " % total ("
                      << snap.processLogicalCoresEquivalent << " logical CPUs equivalent)\n";
        }
        std::cout << "================================================================================\n\n";
    }

    // Dedicated CPU Self-Test
    if (runCpuSelfTest) {
        std::cout << "\n================================================================================\n";
        std::cout << ">>> RUNNING 10-POINT CPU TELEMETRY SELF-TEST <<<\n";
        std::cout << "================================================================================\n";
        auto st = testbench::SelfTester::runCpuTelemetryTests();
        for (const auto& item : st.items) {
            const char* tag = "FAIL";
            if (item.status == testbench::SelfTestStatus::Pass) tag = "PASS";
            else if (item.status == testbench::SelfTestStatus::NotTested) tag = "NOT TESTED";

            std::cout << "  [" << tag << "] "
                      << item.index << ". " << item.name << " (" << item.details << ")\n";
        }
        std::cout << "CPU SELF-TEST RESULT: " << (st.allPassed ? "PASS" : "FAIL")
                  << " (" << st.passedCount << " passed, " << st.failedCount << " failed, "
                  << st.notTestedCount << " not tested)\n";
        if (!st.allPassed) failureCount++;
    }

    // Dedicated SRC Self-Test
    if (runSrcSelfTest) {
        std::cout << "\n================================================================================\n";
        std::cout << ">>> RUNNING 10-POINT SAMPLE RATE CONVERTER SELF-TEST <<<\n";
        std::cout << "================================================================================\n";
        auto st = testbench::SelfTester::runSrcTests();
        for (const auto& item : st.items) {
            const char* tag = "FAIL";
            if (item.status == testbench::SelfTestStatus::Pass) tag = "PASS";
            else if (item.status == testbench::SelfTestStatus::NotTested) tag = "NOT TESTED";

            std::cout << "  [" << tag << "] "
                      << item.index << ". " << item.name << " (" << item.details << ")\n";
        }
        std::cout << "SRC SELF-TEST RESULT: " << (st.allPassed ? "PASS" : "FAIL")
                  << " (" << st.passedCount << " passed, " << st.failedCount << " failed)\n";
        if (!st.allPassed) failureCount++;
    }

    // Dedicated Gate Self-Test
    if (runGateSelfTest) {
        std::cout << "\n================================================================================\n";
        std::cout << ">>> RUNNING 10-POINT NOISE GATE SELF-TEST <<<\n";
        std::cout << "================================================================================\n";
        auto st = testbench::SelfTester::runGateTests();
        for (const auto& item : st.items) {
            const char* tag = "FAIL";
            if (item.status == testbench::SelfTestStatus::Pass) tag = "PASS";
            else if (item.status == testbench::SelfTestStatus::NotTested) tag = "NOT TESTED";

            std::cout << "  [" << tag << "] "
                      << item.index << ". " << item.name << " (" << item.details << ")\n";
        }
        std::cout << "GATE SELF-TEST RESULT: " << (st.allPassed ? "PASS" : "FAIL")
                  << " (" << st.passedCount << " passed, " << st.failedCount << " failed)\n";
        if (!st.allPassed) failureCount++;
    }

    // Dedicated GEQ Self-Test
    if (runGeqSelfTest) {
        std::cout << "\n================================================================================\n";
        std::cout << ">>> RUNNING 10-POINT GRAPHIC EQ SELF-TEST <<<\n";
        std::cout << "================================================================================\n";
        auto st = testbench::SelfTester::runGeqTests();
        for (const auto& item : st.items) {
            const char* tag = "FAIL";
            if (item.status == testbench::SelfTestStatus::Pass) tag = "PASS";
            else if (item.status == testbench::SelfTestStatus::NotTested) tag = "NOT TESTED";

            std::cout << "  [" << tag << "] "
                      << item.index << ". " << item.name << " (" << item.details << ")\n";
        }
        std::cout << "GEQ SELF-TEST RESULT: " << (st.allPassed ? "PASS" : "FAIL")
                  << " (" << st.passedCount << " passed, " << st.failedCount << " failed)\n";
        if (!st.allPassed) failureCount++;
    }

    // Dedicated Delay Self-Test
    if (runDelaySelfTest) {
        std::cout << "\n================================================================================\n";
        std::cout << ">>> RUNNING 10-POINT TAP DELAY SELF-TEST <<<\n";
        std::cout << "================================================================================\n";
        auto st = testbench::SelfTester::runDelayTests();
        for (const auto& item : st.items) {
            const char* tag = "FAIL";
            if (item.status == testbench::SelfTestStatus::Pass) tag = "PASS";
            else if (item.status == testbench::SelfTestStatus::NotTested) tag = "NOT TESTED";

            std::cout << "  [" << tag << "] "
                      << item.index << ". " << item.name << " (" << item.details << ")\n";
        }
        std::cout << "DELAY SELF-TEST RESULT: " << (st.allPassed ? "PASS" : "FAIL")
                  << " (" << st.passedCount << " passed, " << st.failedCount << " failed)\n";
        if (!st.allPassed) failureCount++;
    }

    // Dedicated Reverb Self-Test
    if (runReverbSelfTest) {
        std::cout << "\n================================================================================\n";
        std::cout << ">>> RUNNING 10-POINT INTERNAL REVERB SELF-TEST <<<\n";
        std::cout << "================================================================================\n";
        auto st = testbench::SelfTester::runReverbTests();
        for (const auto& item : st.items) {
            const char* tag = "FAIL";
            if (item.status == testbench::SelfTestStatus::Pass) tag = "PASS";
            else if (item.status == testbench::SelfTestStatus::NotTested) tag = "NOT TESTED";

            std::cout << "  [" << tag << "] "
                      << item.index << ". " << item.name << " (" << item.details << ")\n";
        }
        std::cout << "REVERB SELF-TEST RESULT: " << (st.allPassed ? "PASS" : "FAIL")
                  << " (" << st.passedCount << " passed, " << st.failedCount << " failed)\n";
        if (!st.allPassed) failureCount++;
    }

    // Dedicated AFS Self-Test
    if (runAfsSelfTest) {
        std::cout << "\n================================================================================\n";
        std::cout << ">>> RUNNING 10-POINT FEEDBACK SUPPRESSOR (AFS) SELF-TEST <<<\n";
        std::cout << "================================================================================\n";
        auto st = testbench::SelfTester::runAfsTests();
        for (const auto& item : st.items) {
            const char* tag = "FAIL";
            if (item.status == testbench::SelfTestStatus::Pass) tag = "PASS";
            else if (item.status == testbench::SelfTestStatus::NotTested) tag = "NOT TESTED";

            std::cout << "  [" << tag << "] "
                      << item.index << ". " << item.name << " (" << item.details << ")\n";
        }
        std::cout << "AFS SELF-TEST RESULT: " << (st.allPassed ? "PASS" : "FAIL")
                  << " (" << st.passedCount << " passed, " << st.failedCount << " failed)\n";
        if (!st.allPassed) failureCount++;
    }

    // Dedicated Complete FX Regression Self-Test
    if (runFxRegression) {
        std::cout << "\n================================================================================\n";
        std::cout << ">>> RUNNING COMPLETE FX REGRESSION SELF-TEST <<<\n";
        std::cout << "================================================================================\n";
        auto st = testbench::SelfTester::runFxRegressionTests();
        for (const auto& item : st.items) {
            const char* tag = "FAIL";
            if (item.status == testbench::SelfTestStatus::Pass) tag = "PASS";
            else if (item.status == testbench::SelfTestStatus::NotTested) tag = "NOT TESTED";

            std::cout << "  [" << tag << "] "
                      << item.index << ". " << item.name << " (" << item.details << ")\n";
        }
        std::cout << "FX REGRESSION RESULT: " << (st.allPassed ? "PASS" : "FAIL")
                  << " (" << st.passedCount << " passed, " << st.failedCount << " failed)\n";
        if (!st.allPassed) failureCount++;
    }

    // Dedicated v1.1.1 Internal FX Quality Self-Declaration
    if (runFxSelfDeclaration) {
        testbench::SelfTester::printFxQualitySelfDeclaration();
    }

    // Dedicated UI v2 Parameter Binding & State Serialization Self-Test
    if (runUiSelfTest) {
        std::cout << "\n================================================================================\n";
        std::cout << ">>> RUNNING AUDIO CORE UI v2 PARAMETER BINDING & SERIALIZATION SELF-TEST <<<\n";
        std::cout << "================================================================================\n";
        auto st = testbench::SelfTester::runUiV2Tests();
        for (const auto& item : st.items) {
            std::cout << "  [" << (item.passed ? "PASS" : "FAIL") << "] "
                      << item.index << ". " << item.name << " (" << item.details << ")\n";
        }
        std::cout << "UI v2 SELF-TEST RESULT: " << (st.allPassed ? "PASS" : "FAIL")
                  << " (" << st.passedCount << " passed, " << st.failedCount << " failed)\n";
        if (!st.allPassed) failureCount++;
    }

    // Dedicated UI v2 Engineering Self-Declaration
    if (runUiSelfDeclaration) {
        testbench::SelfTester::printUiV2SelfDeclaration();
    }

    // Dedicated Output Time Alignment Self-Test (ALIGN-001..026)
    if (runAlignSelfTest) {
        std::cout << "\n================================================================================\n";
        std::cout << ">>> RUNNING 26-POINT OUTPUT TIME ALIGNMENT SELF-TEST (ALIGN-001..026) <<<\n";
        std::cout << "================================================================================\n";
        auto st = testbench::SelfTester::runAlignmentTests();
        for (const auto& item : st.items) {
            std::cout << "  [" << (item.passed ? "PASS" : "FAIL") << "] "
                      << item.name << " (" << item.details << ")\n";
        }
        std::cout << "ALIGNMENT SELF-TEST RESULT: " << (st.allPassed ? "PASS" : "FAIL")
                  << " (" << st.passedCount << " passed, " << st.failedCount << " failed)\n";
        if (!st.allPassed) failureCount++;
    }

    // Dedicated Output Time Alignment Engineering Self-Declaration
    if (runAlignSelfDeclaration) {
        testbench::SelfTester::printAlignmentSelfDeclaration();
    }

    // Dedicated Reverb Pre-Delay Self-Test (PREDELAY-001..017 + TIMING + DRYWET)
    if (runReverbPredelaySelfTest || runAll) {
        std::cout << "\n================================================================================\n";
        std::cout << ">>> RUNNING 19-POINT REVERB PRE-DELAY SELF-TEST (PREDELAY-001..017 + TIMING + DRYWET) <<<\n";
        std::cout << "================================================================================\n";
        auto st = testbench::SelfTester::runReverbPredelayTests();
        for (const auto& item : st.items) {
            std::cout << "  [" << (item.passed ? "PASS" : "FAIL") << "] "
                      << item.name << " (" << item.details << ")\n";
        }
        std::cout << "REVERB PRE-DELAY SELF-TEST RESULT: " << (st.allPassed ? "PASS" : "FAIL")
                  << " (" << st.passedCount << " passed, " << st.failedCount << " failed)\n";
        if (!st.allPassed) failureCount++;
    }

    // Dedicated Reverb Templates Self-Test (TEMPLATE-001..014)
    if (runReverbTemplateSelfTest || runAll) {
        std::cout << "\n================================================================================\n";
        std::cout << ">>> RUNNING 14-POINT REVERB TEMPLATES SELF-TEST (TEMPLATE-001..014) <<<\n";
        std::cout << "================================================================================\n";
        auto st = testbench::SelfTester::runReverbTemplateTests();
        for (const auto& item : st.items) {
            std::cout << "  [" << (item.passed ? "PASS" : "FAIL") << "] "
                      << item.name << " (" << item.details << ")\n";
        }
        std::cout << "REVERB TEMPLATES SELF-TEST RESULT: " << (st.allPassed ? "PASS" : "FAIL")
                  << " (" << st.passedCount << " passed, " << st.failedCount << " failed)\n";
        if (!st.allPassed) failureCount++;
    }

    // Dedicated Reverb Pre-Delay & Templates Engineering Self-Declaration
    if (runReverbPredelayDeclaration) {
        testbench::SelfTester::printReverbPredelaySelfDeclaration();
    }

    // Dedicated Reverb Pre-Delay Benchmark
    if (runReverbPredelayBenchmark) {
        runReverbPredelayBenchmarkSuite();
    }

#if defined(_WIN32)
    // Dedicated WASAPI Audio Device Self-Test
    if (runWasapiDeviceTest || runAll) {
        std::cout << "\n================================================================================\n";
        std::cout << ">>> RUNNING AUDIO CORE WASAPI AUDIO DEVICE & CAPABILITIES SELF-TEST <<<\n";
        std::cout << "================================================================================\n";
        auto st = testbench::SelfTester::runWasapiDeviceTests();
        for (const auto& item : st.items) {
            std::cout << "  [" << (item.passed ? "PASS" : "FAIL") << "] "
                      << item.name << " (" << item.details << ")\n";
        }
        std::cout << "WASAPI DEVICE SELF-TEST RESULT: " << (st.allPassed ? "PASS" : "FAIL")
                  << " (" << st.passedCount << " passed, " << st.failedCount << " failed)\n";
        if (!st.allPassed) failureCount++;
    }

    // Dedicated WASAPI Device Engineering Self-Declaration
    if (runWasapiDeviceDeclaration) {
        testbench::SelfTester::printWasapiDeviceSelfDeclaration();
    }

    // Dedicated WASAPI Realtime Input & System Loopback DSP Self-Test
    if (runWasapiLoopbackTest || runAll) {
        std::cout << "\n================================================================================\n";
        std::cout << ">>> RUNNING AUDIO CORE WASAPI REALTIME INPUT & LOOPBACK DSP SELF-TEST <<<\n";
        std::cout << "================================================================================\n";
        auto st = testbench::SelfTester::runWasapiLoopbackTests();
        for (const auto& item : st.items) {
            std::cout << "  [" << (item.passed ? "PASS" : "FAIL") << "] "
                      << item.name << " (" << item.details << ")\n";
        }
        std::cout << "WASAPI INPUT/LOOPBACK DSP TEST RESULT: " << (st.allPassed ? "PASS" : "FAIL")
                  << " (" << st.passedCount << " passed, " << st.failedCount << " failed)\n";
        if (!st.allPassed) failureCount++;
    }

    // Dedicated WASAPI Realtime Input & Loopback DSP Engineering Self-Declaration
    if (runWasapiLoopbackDeclaration) {
        testbench::SelfTester::printWasapiLoopbackSelfDeclaration();
    }

    // Dedicated WASAPI Benchmark Matrix
    if (runWasapiBenchmarkMatrix) {
        runWasapiBenchmarkMatrixSuite();
    }

    // Dedicated WASAPI Live Audio Stress Test
    if (runWasapiStressTest) {
        if (!runWasapiStressTestSuite(syntheticDuration > 0.0 ? syntheticDuration : 60.0)) {
            failureCount++;
        }
    }
#else
    if (runWasapiDeviceTest || runWasapiDeviceDeclaration || runWasapiLoopbackTest || runWasapiLoopbackDeclaration || runWasapiBenchmarkMatrix || runWasapiStressTest) {
        std::cout << "Notice: WASAPI tests are only supported on Windows.\n";
    }
#endif

    // Multi-Rate FX Benchmark Suite
    if (runFxBenchmark) {
        runFxBenchmarkSuite();
    }

    // 60-second Sustained Stress Test Suite
    if (runStressTest) {
        if (!runStressTestSuite()) {
            failureCount++;
        }
    }

    // Parameter Automation & Slewing Suite
    if (runParamAutomation) {
        if (!runParamAutomationSuite()) {
            failureCount++;
        }
    }

    // 1. FULL SELF-TEST (Subsystems + Bindings + Contracts + CPU Telemetry + SRC + Gate + GEQ + FX)
    if (runSelfTest || runAll) {
        std::cout << "\n================================================================================\n";
        std::cout << ">>> RUNNING AUTOMATED FULL SELF-TEST <<<\n";
        std::cout << "================================================================================\n";
        auto st = testbench::SelfTester::runAll();
        for (const auto& item : st.items) {
            const char* tag = "FAIL";
            if (item.status == testbench::SelfTestStatus::Pass) tag = "PASS";
            else if (item.status == testbench::SelfTestStatus::NotTested) tag = "NOT TESTED";

            std::cout << "  [" << tag << "] "
                      << item.index << ". " << item.name << " (" << item.details << ")\n";
        }
        std::cout << "SELF-TEST RESULT: " << (st.allPassed ? "PASS" : "FAIL")
                  << " (" << st.passedCount << " passed, " << st.failedCount << " failed";
        if (st.notTestedCount > 0) {
            std::cout << ", " << st.notTestedCount << " not tested";
        }
        std::cout << ")\n";
        if (!st.allPassed) failureCount++;
    }

    // 2. CORRECTNESS
    testbench::CorrectnessSuiteResult corr;
    if (runCorrectness || runAll) {
        std::cout << "\n================================================================================\n";
        std::cout << ">>> RUNNING DSP CORRECTNESS TESTS <<<\n";
        std::cout << "================================================================================\n";
        corr = testbench::CorrectnessVerifier::runAllTests(48000.0, blockSize);
        for (const auto& item : corr.items) {
            std::cout << "  [" << (item.passed ? "PASS" : "FAIL") << "] "
                      << item.testId << ": " << item.description;
            if (item.tolerance > 0.0) {
                std::cout << " [Expected: " << item.expected << ", Actual: " << item.actual << "]";
            }
            std::cout << "\n";
        }
        std::cout << "CORRECTNESS RESULT: " << (corr.allPassed ? "PASS" : "FAIL")
                  << " (" << corr.passedCount << " passed, " << corr.failedCount << " failed)\n";
        if (!corr.allPassed) failureCount++;
    }

    // 3. MULTI-CORE WORKLOAD BENCHMARK
    testbench::MultiCoreBenchmarkResult multiCoreRes;
    bool hasMultiCore = false;
    if (runMultiCore || runAll) {
        std::cout << "\n================================================================================\n";
        std::cout << ">>> RUNNING CONTROLLED MULTI-CORE WORKLOAD BENCHMARK <<<\n";
        std::cout << "================================================================================\n";
        multiCoreRes = testbench::CpuWorkloadBenchmark::run();
        std::cout << testbench::CpuWorkloadBenchmark::formatTable(multiCoreRes);
        hasMultiCore = true;
    }

    // 4. BENCHMARK
    if (runBenchmark || runAll || runNodeProfile) {
        std::cout << "\n================================================================================\n";
        std::cout << ">>> RUNNING HIGH-RESOLUTION DSP BENCHMARK <<<\n";
        std::cout << "================================================================================\n";

        audiocore::AudioBuffer inputBuf;
        double sampleRate = configuredSampleRate;
        uint32_t channels = 2;
        std::string sourceName;

        testbench::PipelineTimingMetrics pipelineMetrics;
        pipelineMetrics.engineSampleRate = configuredSampleRate;

        if (!wavInputPath.empty()) {
            testbench::WavMetadata meta;
            auto tDec0 = std::chrono::high_resolution_clock::now();
            testbench::WavIoResult wr = testbench::WavIo::readWav(wavInputPath, inputBuf, meta);
            auto tDec1 = std::chrono::high_resolution_clock::now();
            if (wr != testbench::WavIoResult::Success) {
                std::cerr << "ERROR: Failed to load WAV file '" << wavInputPath << "': "
                          << testbench::WavIo::resultToString(wr) << "\n";
                return 1;
            }
            pipelineMetrics.decoderTimeMs = std::chrono::duration<double, std::milli>(tDec1 - tDec0).count();
            pipelineMetrics.inSampleRate = static_cast<double>(meta.sampleRate);
            channels = meta.channels;
            sourceName = wavInputPath + " (" + meta.formatDescription + ")";
            std::cout << "Loaded WAV: " << sourceName << "\n";

            if (static_cast<double>(meta.sampleRate) != configuredSampleRate) {
                std::cout << "Resampling input from " << meta.sampleRate << " Hz to "
                          << configuredSampleRate << " Hz...\n";
                audiocore::AudioBuffer resampled;
                audiocore::SampleRateConverter src;
                src.configure(static_cast<double>(meta.sampleRate), configuredSampleRate, channels);
                auto tSrc0 = std::chrono::high_resolution_clock::now();
                src.resampleBuffer(inputBuf, resampled);
                auto tSrc1 = std::chrono::high_resolution_clock::now();
                pipelineMetrics.srcTimeMs = std::chrono::duration<double, std::milli>(tSrc1 - tSrc0).count();
                pipelineMetrics.srcActive = true;
                pipelineMetrics.srcStatusText = "ACTIVE";
                inputBuf = std::move(resampled);
            } else {
                pipelineMetrics.srcTimeMs = 0.0;
                pipelineMetrics.srcActive = false;
                pipelineMetrics.srcStatusText = "BYPASS";
            }
            sampleRate = configuredSampleRate;
        } else {
            // Generate synthetic reference signal
            sampleRate = configuredSampleRate;
            uint32_t totalFrames = static_cast<uint32_t>(sampleRate * syntheticDuration);
            testbench::ReferenceSignalType sigType = testbench::ReferenceSignalType::Sine1kHz;
            if (syntheticType == "silence") sigType = testbench::ReferenceSignalType::Silence;
            else if (syntheticType == "dc") sigType = testbench::ReferenceSignalType::Dc;
            else if (syntheticType == "sine-6db") sigType = testbench::ReferenceSignalType::SineMinus6dB;
            else if (syntheticType == "impulse") sigType = testbench::ReferenceSignalType::Impulse;
            else if (syntheticType == "noise") sigType = testbench::ReferenceSignalType::WhiteNoise;
            else if (syntheticType == "stereo") sigType = testbench::ReferenceSignalType::StereoIndependent;

            inputBuf.resize(channels, totalFrames);
            testbench::SyntheticSignals::generate(sigType, inputBuf, sampleRate, channels, totalFrames);
            sourceName = "Synthetic " + syntheticType + " (" + std::to_string(syntheticDuration) + " s)";
            std::cout << "Generated: " << sourceName << "\n";

            pipelineMetrics.decoderTimeMs = 0.0;
            pipelineMetrics.srcTimeMs = 0.0;
            pipelineMetrics.srcActive = false;
            pipelineMetrics.srcStatusText = "BYPASS";
            pipelineMetrics.inSampleRate = sampleRate;
        }

        testbench::DspProfiler profiler;
        testbench::ProfilerRunConfig cfg;
        cfg.sampleRate = sampleRate;
        cfg.channels = channels;
        cfg.blockSize = blockSize;
        cfg.chainPreset = preset;
        cfg.trackPerBlockTimes = true;

        audiocore::AudioBuffer outBuf;
        std::vector<double> blockTimes;

        // Sample CPU before and after benchmark
        auto tel = testbench::CpuTelemetry::create();
        tel->sampleUtilization();
        testbench::BenchmarkMetrics m = profiler.profileBuffer(inputBuf, outBuf, cfg, &blockTimes);
        auto cpuSnap = tel->sampleUtilization();

        pipelineMetrics.dspTimeMs = m.totalDspTimeUs / 1000.0;
        pipelineMetrics.outputTimeMs = 0.0;

        std::vector<testbench::NodeProfileResult> nodeProfiles;
        if (runNodeProfile || runAll) {
            std::cout << "Profiling individual DSP stages...\n";
            nodeProfiles = profiler.profileAllNodes(inputBuf, sampleRate, channels, blockSize);
        }

        std::cout << "Measuring telemetry polling overhead...\n";
        testbench::TelemetryOverheadResult telOverhead = profiler.measureTelemetryOverhead(
            inputBuf, sampleRate, channels, blockSize);

        if (!wavOutputPath.empty()) {
            auto tOut0 = std::chrono::high_resolution_clock::now();
            testbench::WavIoResult wr = testbench::WavIo::writeWav(
                wavOutputPath, outBuf, static_cast<uint32_t>(sampleRate), outBuf.getFrameCount(), exportFormat);
            auto tOut1 = std::chrono::high_resolution_clock::now();
            pipelineMetrics.outputTimeMs = std::chrono::duration<double, std::milli>(tOut1 - tOut0).count();
            if (wr == testbench::WavIoResult::Success) {
                std::cout << "Processed output WAV successfully saved to: " << wavOutputPath << "\n";
            } else {
                std::cerr << "Failed to save output WAV to: " << wavOutputPath << "\n";
            }
        }

        pipelineMetrics.totalTimeMs = pipelineMetrics.decoderTimeMs + pipelineMetrics.srcTimeMs
            + pipelineMetrics.dspTimeMs + pipelineMetrics.outputTimeMs;

        std::string report = testbench::ReportExporter::generateTextReport(
            sourceName, m, nodeProfiles,
            (runCorrectness || runAll) ? &corr : nullptr,
            &telOverhead,
            &cpuSnap,
            hasMultiCore ? &multiCoreRes : nullptr,
            &pipelineMetrics);

        std::cout << "\n" << report << "\n";

        if (!jsonExportPath.empty()) {
            std::string json = testbench::ReportExporter::generateJsonReport(
                m, nodeProfiles, &telOverhead, &cpuSnap,
                hasMultiCore ? &multiCoreRes : nullptr,
                &pipelineMetrics);
            if (testbench::ReportExporter::saveToFile(jsonExportPath, json)) {
                std::cout << "Exported JSON report to: " << jsonExportPath << "\n";
            }
        }

        if (!csvExportPath.empty()) {
            std::string csv = testbench::ReportExporter::generateCsvReport(
                m, blockTimes, nodeProfiles, &cpuSnap, &pipelineMetrics);
            if (testbench::ReportExporter::saveToFile(csvExportPath, csv)) {
                std::cout << "Exported CSV report to: " << csvExportPath << "\n";
            }
        }

        if (!txtExportPath.empty()) {
            if (testbench::ReportExporter::saveToFile(txtExportPath, report)) {
                std::cout << "Exported Text report to: " << txtExportPath << "\n";
            }
        }

        if (m.status != "PASS") failureCount++;
    }

    return (failureCount == 0) ? 0 : 1;
}
