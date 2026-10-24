#include "cpu_workload_benchmark.hpp"
#include "cpu_telemetry.hpp"

#include <thread>
#include <chrono>
#include <vector>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <algorithm>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif defined(__linux__)
#include <pthread.h>
#include <time.h>
#endif

namespace testbench {

namespace {

// Deterministic Biquad Filter Workload unit
void runDeterministicDspWorkload(uint32_t blockCount, uint32_t blockSize, double* outCpuTimeMs) {
    auto tThreadStart = std::chrono::steady_clock::now();
    uint64_t cpuStart100ns = 0;
    uint64_t cpuEnd100ns = 0;

#if defined(_WIN32)
    FILETIME cr, ex, kr0, us0;
    if (GetThreadTimes(GetCurrentThread(), &cr, &ex, &kr0, &us0)) {
        ULARGE_INTEGER uK, uU;
        uK.LowPart = kr0.dwLowDateTime; uK.HighPart = kr0.dwHighDateTime;
        uU.LowPart = us0.dwLowDateTime; uU.HighPart = us0.dwHighDateTime;
        cpuStart100ns = uK.QuadPart + uU.QuadPart;
    }
#endif

    // Preallocated buffers
    std::vector<float> buffer(blockSize, 0.5f);
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
    const float b0 = 0.2929f, b1 = 0.5858f, b2 = 0.2929f;
    const float a1 = -0.0000f, a2 = 0.1716f;
    volatile float accumulator = 0.0f;

    for (uint32_t b = 0; b < blockCount; ++b) {
        for (uint32_t i = 0; i < blockSize; ++i) {
            float inSample = buffer[i] + (b & 1 ? 0.01f : -0.01f);
            float outSample = b0 * inSample + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1;
            x1 = inSample;
            y2 = y1;
            y1 = outSample;
            buffer[i] = outSample;
            accumulator += outSample * 1e-9f;
        }
    }

#if defined(_WIN32)
    FILETIME kr1, us1;
    if (GetThreadTimes(GetCurrentThread(), &cr, &ex, &kr1, &us1)) {
        ULARGE_INTEGER uK, uU;
        uK.LowPart = kr1.dwLowDateTime; uK.HighPart = kr1.dwHighDateTime;
        uU.LowPart = us1.dwLowDateTime; uU.HighPart = us1.dwHighDateTime;
        cpuEnd100ns = uK.QuadPart + uU.QuadPart;
    }
    if (cpuEnd100ns >= cpuStart100ns && cpuStart100ns > 0) {
        *outCpuTimeMs = static_cast<double>(cpuEnd100ns - cpuStart100ns) / 10000.0; // 100ns to ms
    } else {
        auto tThreadEnd = std::chrono::steady_clock::now();
        *outCpuTimeMs = std::chrono::duration<double, std::milli>(tThreadEnd - tThreadStart).count();
    }
#else
    auto tThreadEnd = std::chrono::steady_clock::now();
    *outCpuTimeMs = std::chrono::duration<double, std::milli>(tThreadEnd - tThreadStart).count();
#endif
}

} // namespace

MultiCoreBenchmarkResult CpuWorkloadBenchmark::run(uint32_t maxWorkers) {
    MultiCoreBenchmarkResult result;
    auto telemetry = CpuTelemetry::create();
    auto hw = telemetry->getHardwareInfo();

    result.onlineCpus = (hw.onlineCpuCount > 0) ? hw.onlineCpuCount : 1;
    result.affinityMask = hw.affinityMask;
    result.workloadDescription = "Deterministic DSP Workload: 40,000 blocks (128 frames) 4-Band Biquad Filter";

    uint32_t limit = (maxWorkers > 0) ? maxWorkers : result.onlineCpus;
    limit = std::min(limit, std::max(1u, result.onlineCpus));
    limit = std::min(limit, 8u); // Cap diagnostic benchmark at 8 threads for responsiveness

    std::ostringstream affOss;
    if (!hw.affinityCpus.empty()) {
        affOss << "CPU ";
        for (size_t i = 0; i < hw.affinityCpus.size(); ++i) {
            if (i > 0) affOss << ", ";
            affOss << hw.affinityCpus[i];
            if (i >= 7 && hw.affinityCpus.size() > 8) {
                affOss << "... (" << hw.affinityCpus.size() << " total)";
                break;
            }
        }
    } else {
        affOss << "System Default";
    }
    std::string affinityStr = affOss.str();

    const uint32_t kTotalBlocks = 40000;
    const uint32_t kBlockSize = 128;
    double t1WallTimeMs = 0.0;

    for (uint32_t w = 1; w <= limit; ++w) {
        WorkerBenchmarkRow row;
        row.workerCount = w;
        row.cpuAffinity = affinityStr;

        uint32_t blocksPerWorker = kTotalBlocks / w;
        std::vector<double> threadCpuTimes(w, 0.0);
        std::vector<std::thread> workers;
        workers.reserve(w);

        auto tStart = std::chrono::high_resolution_clock::now();

        for (uint32_t i = 0; i < w; ++i) {
            workers.emplace_back([i, blocksPerWorker, kBlockSize, &threadCpuTimes]() {
                runDeterministicDspWorkload(blocksPerWorker, kBlockSize, &threadCpuTimes[i]);
            });
        }

        for (auto& th : workers) {
            if (th.joinable()) th.join();
        }

        auto tEnd = std::chrono::high_resolution_clock::now();
        row.wallTimeMs = std::chrono::duration<double, std::milli>(tEnd - tStart).count();

        double totalCpu = 0.0;
        for (double t : threadCpuTimes) {
            totalCpu += t;
        }
        row.totalCpuTimeMs = totalCpu;

        if (w == 1) {
            t1WallTimeMs = row.wallTimeMs;
            row.speedup = 1.00;
            row.scalingEfficiency = 100.0;
        } else {
            if (row.wallTimeMs > 0.0 && t1WallTimeMs > 0.0) {
                row.speedup = t1WallTimeMs / row.wallTimeMs;
                row.scalingEfficiency = (row.speedup / static_cast<double>(w)) * 100.0;
            }
        }

        row.status = (row.wallTimeMs > 0.0) ? "PASS" : "FAIL";
        result.rows.push_back(row);
    }

    result.passed = true;
    for (const auto& r : result.rows) {
        if (r.status != "PASS") result.passed = false;
    }

    return result;
}

std::string CpuWorkloadBenchmark::formatTable(const MultiCoreBenchmarkResult& res) {
    std::ostringstream ss;
    ss << "================================================================================\n";
    ss << "CONTROLLED MULTI-CORE WORKLOAD BENCHMARK (DIAGNOSTIC ONLY)\n";
    ss << "================================================================================\n";
    ss << "Workload : " << res.workloadDescription << "\n";
    ss << "Online Cores : " << res.onlineCpus << "\n";
    ss << "Affinity Mask : 0x" << std::hex << res.affinityMask << std::dec << "\n";
    ss << "--------------------------------------------------------------------------------\n";
    ss << "Workers | Wall Time | Total CPU Time | Speedup | Efficiency | Status\n";
    ss << "--------------------------------------------------------------------------------\n";

    for (const auto& r : res.rows) {
        ss << std::setw(7) << r.workerCount << " | "
           << std::setw(7) << std::fixed << std::setprecision(1) << r.wallTimeMs << "ms | "
           << std::setw(12) << std::fixed << std::setprecision(1) << r.totalCpuTimeMs << "ms | "
           << std::setw(6) << std::fixed << std::setprecision(2) << r.speedup << "x | "
           << std::setw(9) << std::fixed << std::setprecision(1) << r.scalingEfficiency << "% | "
           << r.status << "\n";
    }

    ss << "--------------------------------------------------------------------------------\n";
    ss << "Notice: Diagnostic benchmark only. Production realtime audio engine remains\n";
    ss << "single-thread deterministic. Multi-core workers are strictly non-realtime.\n";
    ss << "================================================================================\n";

    return ss.str();
}

} // namespace testbench

