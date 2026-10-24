#include "benchmark_metrics.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace testbench {

namespace {

double getPercentile(const std::vector<double>& sortedData, double percentile) {
    if (sortedData.empty()) return 0.0;
    if (sortedData.size() == 1) return sortedData[0];
    if (percentile <= 0.0) return sortedData.front();
    if (percentile >= 100.0) return sortedData.back();

    double rank = (percentile / 100.0) * (sortedData.size() - 1);
    size_t lower = static_cast<size_t>(std::floor(rank));
    size_t upper = static_cast<size_t>(std::ceil(rank));
    double weight = rank - lower;

    return sortedData[lower] * (1.0 - weight) + sortedData[upper] * weight;
}

} // namespace

BenchmarkMetrics BenchmarkMetrics::compute(
    const std::vector<double>& blockDurationsUs,
    uint32_t sampleRate,
    uint32_t channels,
    uint32_t blockSize,
    uint32_t totalFrames,
    uint64_t nanCount,
    uint64_t infCount,
    uint64_t clippedCount)
{
    BenchmarkMetrics m;
    m.sampleRate = sampleRate;
    m.channels = channels;
    m.blockSize = blockSize;
    m.totalBlocks = static_cast<uint32_t>(blockDurationsUs.size());
    m.totalFrames = totalFrames;
    m.audioDurationSeconds = (sampleRate > 0) ? (static_cast<double>(totalFrames) / sampleRate) : 0.0;
    m.blockDeadlineUs = (sampleRate > 0) ? ((static_cast<double>(blockSize) / sampleRate) * 1000000.0) : 0.0;

    m.nanCount = nanCount;
    m.infCount = infCount;
    m.invalidSampleCount = nanCount + infCount;
    m.clippedSampleCount = clippedCount;

    if (blockDurationsUs.empty()) {
        m.status = "FAIL";
        m.details = "No blocks processed";
        return m;
    }

    double sum = 0.0;
    double minVal = blockDurationsUs[0];
    double maxVal = blockDurationsUs[0];
    uint32_t violations = 0;

    for (double t : blockDurationsUs) {
        sum += t;
        if (t < minVal) minVal = t;
        if (t > maxVal) maxVal = t;
        if (m.blockDeadlineUs > 0.0 && t > m.blockDeadlineUs) {
            violations++;
        }
    }

    m.totalDspTimeUs = sum;
    m.minUs = minVal;
    m.maxUs = maxVal;
    m.avgUs = sum / blockDurationsUs.size();
    m.deadlineViolations = violations;

    // Median, P95, P99
    std::vector<double> sorted = blockDurationsUs;
    std::sort(sorted.begin(), sorted.end());
    m.medianUs = getPercentile(sorted, 50.0);
    m.p95Us = getPercentile(sorted, 95.0);
    m.p99Us = getPercentile(sorted, 99.0);

    // Standard deviation
    double varianceSum = 0.0;
    for (double t : blockDurationsUs) {
        double diff = t - m.avgUs;
        varianceSum += diff * diff;
    }
    m.stdDevUs = std::sqrt(varianceSum / blockDurationsUs.size());

    // Jitter: mean absolute diff between successive blocks
    if (blockDurationsUs.size() > 1) {
        double jitterSum = 0.0;
        for (size_t i = 1; i < blockDurationsUs.size(); ++i) {
            jitterSum += std::abs(blockDurationsUs[i] - blockDurationsUs[i - 1]);
        }
        m.jitterUs = jitterSum / (blockDurationsUs.size() - 1);
    } else {
        m.jitterUs = 0.0;
    }

    // Realtime factor & utilization
    double totalDspSec = m.totalDspTimeUs * 1e-6;
    m.realtimeFactor = (totalDspSec > 1e-9) ? (m.audioDurationSeconds / totalDspSec) : 0.0;

    if (m.blockDeadlineUs > 0.0) {
        m.avgRealtimeUtilization = m.avgUs / m.blockDeadlineUs;
        m.worstRealtimeUtilization = m.maxUs / m.blockDeadlineUs;
    } else {
        m.avgRealtimeUtilization = 0.0;
        m.worstRealtimeUtilization = 0.0;
    }

    if (m.invalidSampleCount > 0) {
        m.status = "FAIL";
        m.details = "NaN/Inf detected in output";
    } else if (m.deadlineViolations > 0) {
        m.status = "WARN";
        m.details = "Realtime deadline violations occurred";
    } else {
        m.status = "PASS";
        m.details = "All blocks processed cleanly within realtime deadline";
    }

    return m;
}

} // namespace testbench

