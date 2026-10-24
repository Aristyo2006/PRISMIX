#pragma once

#include "benchmark_metrics.hpp"
#include "correctness_tests.hpp"
#include "profiler.hpp"
#include "cpu_telemetry.hpp"
#include "cpu_workload_benchmark.hpp"

#include <string>
#include <vector>

namespace testbench {

class ReportExporter {
public:
    static std::string generateTextReport(
        const std::string& inputName,
        const BenchmarkMetrics& metrics,
        const std::vector<NodeProfileResult>& nodeProfiles = {},
        const CorrectnessSuiteResult* correctness = nullptr,
        const TelemetryOverheadResult* telemetry = nullptr,
        const CpuUtilizationSnapshot* cpuSnapshot = nullptr,
        const MultiCoreBenchmarkResult* multiCore = nullptr,
        const PipelineTimingMetrics* pipeline = nullptr);

    static std::string generateJsonReport(
        const BenchmarkMetrics& metrics,
        const std::vector<NodeProfileResult>& nodeProfiles = {},
        const TelemetryOverheadResult* telemetry = nullptr,
        const CpuUtilizationSnapshot* cpuSnapshot = nullptr,
        const MultiCoreBenchmarkResult* multiCore = nullptr,
        const PipelineTimingMetrics* pipeline = nullptr);

    static std::string generateCsvReport(
        const BenchmarkMetrics& metrics,
        const std::vector<double>& blockTimesUs = {},
        const std::vector<NodeProfileResult>& nodeProfiles = {},
        const CpuUtilizationSnapshot* cpuSnapshot = nullptr,
        const PipelineTimingMetrics* pipeline = nullptr);

    static bool saveToFile(const std::string& filePath, const std::string& content);
};

} // namespace testbench
