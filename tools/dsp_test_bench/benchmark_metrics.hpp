#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace testbench {

struct BenchmarkMetrics {
    // Input / Configuration
    uint32_t sampleRate{48000};
    uint32_t channels{2};
    uint32_t blockSize{128};
    uint32_t totalBlocks{0};
    uint32_t totalFrames{0};
    double audioDurationSeconds{0.0};

    // Timing (microseconds)
    double totalDspTimeUs{0.0};
    double minUs{0.0};
    double maxUs{0.0};
    double avgUs{0.0};
    double medianUs{0.0};
    double p95Us{0.0};
    double p99Us{0.0};
    double stdDevUs{0.0};
    double jitterUs{0.0};

    // Realtime Evaluation
    double realtimeFactor{0.0};        // audioDuration / (totalDspTimeUs * 1e-6)
    double blockDeadlineUs{0.0};       // (blockSize / sampleRate) * 1e6
    double avgRealtimeUtilization{0.0};// avgUs / blockDeadlineUs
    double worstRealtimeUtilization{0.0};// maxUs / blockDeadlineUs
    uint32_t deadlineViolations{0};

    // Numerical Validation
    uint64_t nanCount{0};
    uint64_t infCount{0};
    uint64_t invalidSampleCount{0};
    uint64_t clippedSampleCount{0};

    // Overall Status
    std::string status{"PASS"};
    std::string details;

    static BenchmarkMetrics compute(
        const std::vector<double>& blockDurationsUs,
        uint32_t sampleRate,
        uint32_t channels,
        uint32_t blockSize,
        uint32_t totalFrames,
        uint64_t nanCount,
        uint64_t infCount,
        uint64_t clippedCount);
};

struct NodeProfileResult {
    std::string nodeName;
    double avgUs{0.0};
    double minUs{0.0};
    double maxUs{0.0};
    double p95Us{0.0};
    double p99Us{0.0};
    double stdDevUs{0.0};
    double realtimeFactor{0.0};
};

} // namespace testbench

