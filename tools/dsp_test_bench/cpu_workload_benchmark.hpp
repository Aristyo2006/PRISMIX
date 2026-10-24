#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace testbench {

struct WorkerBenchmarkRow {
    uint32_t workerCount{1};
    std::string cpuAffinity;
    double wallTimeMs{0.0};
    double totalCpuTimeMs{0.0};
    double speedup{1.0};
    double scalingEfficiency{100.0};
    std::string status{"PASS"};
};

struct MultiCoreBenchmarkResult {
    uint32_t onlineCpus{1};
    uint64_t affinityMask{0};
    std::string workloadDescription;
    std::vector<WorkerBenchmarkRow> rows;
    bool passed{true};
    std::string details;
};

class CpuWorkloadBenchmark {
public:
    /**
     * @brief Run controlled CPU workload benchmark across 1..N workers.
     * @param maxWorkers Max workers to benchmark (0 = up to min(onlineCpus, 8)).
     */
    static MultiCoreBenchmarkResult run(uint32_t maxWorkers = 0);

    /**
     * @brief Formats benchmark result into human-readable tabular text.
     */
    static std::string formatTable(const MultiCoreBenchmarkResult& res);
};

} // namespace testbench

