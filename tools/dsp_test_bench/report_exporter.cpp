#include "report_exporter.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>

namespace testbench {

bool ReportExporter::saveToFile(const std::string& filePath, const std::string& content) {
    std::ofstream file(filePath);
    if (!file.is_open()) return false;
    file << content;
    return file.good();
}

std::string ReportExporter::generateTextReport(
    const std::string& inputName,
    const BenchmarkMetrics& m,
    const std::vector<NodeProfileResult>& nodeProfiles,
    const CorrectnessSuiteResult* correctness,
    const TelemetryOverheadResult* telemetry,
    const CpuUtilizationSnapshot* cpuSnapshot,
    const MultiCoreBenchmarkResult* multiCore,
    const PipelineTimingMetrics* pipeline)
{
    std::ostringstream ss;
    ss << "================================================================================\n";
    ss << "AUDIO CORE DSP TEST BENCH — PERFORMANCE & VERIFICATION REPORT\n";
    ss << "================================================================================\n\n";

    ss << "--- [1. INPUT & ENGINE CONFIGURATION] ---\n";
    ss << "  Target Audio Input     : " << inputName << "\n";
    ss << "  Sample Rate            : " << m.sampleRate << " Hz\n";
    ss << "  Channel Count          : " << m.channels << " channels\n";
    ss << "  Engine Block Size      : " << m.blockSize << " frames\n";
    ss << "  Block Deadline         : " << std::fixed << std::setprecision(2) << m.blockDeadlineUs << " us ("
       << (m.blockDeadlineUs / 1000.0) << " ms)\n";
    ss << "  Total Processed Frames : " << m.totalFrames << " frames\n";
    ss << "  Total Processed Blocks : " << m.totalBlocks << " blocks\n";
    ss << "  Audio Content Duration : " << std::fixed << std::setprecision(3) << m.audioDurationSeconds << " seconds\n";

    if (pipeline) {
        ss << "\n--- [PIPELINE TIMING BREAKDOWN (DECODER / SRC / DSP / OUTPUT)] ---\n";
        ss << "  Input Sample Rate      : " << pipeline->inSampleRate << " Hz\n";
        ss << "  Engine Sample Rate     : " << pipeline->engineSampleRate << " Hz\n";
        ss << "  SRC Conversion Status  : " << pipeline->srcStatusText << "\n";
        ss << "  Decoder Time           : " << std::fixed << std::setprecision(2) << pipeline->decoderTimeMs << " ms\n";
        ss << "  SRC Time               : " << pipeline->srcTimeMs << " ms\n";
        ss << "  DSP Processing Time    : " << pipeline->dspTimeMs << " ms (isolated)\n";
        ss << "  Output Time            : " << pipeline->outputTimeMs << " ms\n";
        ss << "  Total Pipeline Time    : " << pipeline->totalTimeMs << " ms\n";
    }
    ss << "\n";

    auto tel = CpuTelemetry::create();
    auto hw = tel->getHardwareInfo();

    ss << "--- [2. HARDWARE & CPU PERFORMANCE TELEMETRY] ---\n";
    ss << "  CPU Architecture       : " << hw.architectureName << "\n";
    ss << "  CPU Model Name         : " << hw.modelName << "\n";
    ss << "  Logical CPU Count      : " << hw.logicalCpuCount << "\n";
    ss << "  Online CPU Count       : " << hw.onlineCpuCount << "\n";
    ss << "  Process Affinity Mask  : 0x" << std::hex << hw.affinityMask << std::dec << " (";
    for (size_t i = 0; i < hw.affinityCpus.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << "CPU " << hw.affinityCpus[i];
    }
    ss << ")\n";

    if (cpuSnapshot && cpuSnapshot->valid) {
        ss << "  System CPU Usage       : " << std::fixed << std::setprecision(1) << cpuSnapshot->systemTotalPercent << " %\n";
        ss << "  Process CPU Usage      : " << std::fixed << std::setprecision(2) << cpuSnapshot->processSystemEquivalentPercent
           << " % total-system (" << cpuSnapshot->processLogicalCoresEquivalent << " logical CPUs equivalent)\n";
        ss << "  Audio Thread CPU Usage : " << std::fixed << std::setprecision(2) << cpuSnapshot->audioThreadCorePercent
           << " % (" << cpuSnapshot->audioThreadCpuTimeUs << " us accumulated)\n";
        if (cpuSnapshot->audioThreadCurrentCpu >= 0) {
            ss << "  Audio Thread Core      : CPU " << cpuSnapshot->audioThreadCurrentCpu << "\n";
        }
    }
    ss << "  DSP Realtime Util (Avg): " << std::fixed << std::setprecision(4) << (m.avgRealtimeUtilization * 100.0) << " %\n";
    ss << "  DSP Realtime Util (Max): " << std::fixed << std::setprecision(4) << (m.worstRealtimeUtilization * 100.0) << " %\n\n";

    ss << "--- [3. BENCHMARK TIMING STATISTICS] ---\n";
    ss << "  Total DSP Time         : " << std::fixed << std::setprecision(2) << m.totalDspTimeUs << " us ("
       << (m.totalDspTimeUs / 1000.0) << " ms)\n";
    ss << "  Average Block Latency  : " << m.avgUs << " us\n";
    ss << "  Median Block Latency   : " << m.medianUs << " us\n";
    ss << "  Minimum Block Latency  : " << m.minUs << " us\n";
    ss << "  P95 Block Latency      : " << m.p95Us << " us\n";
    ss << "  P99 Block Latency      : " << m.p99Us << " us\n";
    ss << "  Maximum Block Latency  : " << m.maxUs << " us\n";
    ss << "  Standard Deviation     : " << m.stdDevUs << " us\n";
    ss << "  Processing Jitter      : " << m.jitterUs << " us\n\n";

    ss << "--- [4. REALTIME & PRODUCTION EVALUATION] ---\n";
    ss << "  Realtime Speedup Factor: " << std::fixed << std::setprecision(1) << m.realtimeFactor << "x realtime\n";
    ss << "  Average Realtime Util  : " << std::fixed << std::setprecision(4) << (m.avgRealtimeUtilization * 100.0) << " %\n";
    ss << "  Worst Realtime Util    : " << std::fixed << std::setprecision(4) << (m.worstRealtimeUtilization * 100.0) << " %\n";
    ss << "  Deadline Violations    : " << m.deadlineViolations << " (tolerance = 0)\n";
    ss << "  Hardware Margin        : " << std::fixed << std::setprecision(2) << ((1.0 - m.worstRealtimeUtilization) * 100.0) << " % headroom\n\n";

    ss << "--- [5. NUMERICAL & CORRECTNESS VALIDATION] ---\n";
    ss << "  NaN Count              : " << m.nanCount << "\n";
    ss << "  Infinity Count         : " << m.infCount << "\n";
    ss << "  Invalid Samples Total  : " << m.invalidSampleCount << "\n";
    ss << "  Clipped Samples Total  : " << m.clippedSampleCount << "\n";
    ss << "  Overall Test Status    : [" << m.status << "]\n";
    if (!m.details.empty()) {
        ss << "  Status Details         : " << m.details << "\n";
    }
    ss << "\n";

    if (!nodeProfiles.empty()) {
        ss << "--- [6. PER-NODE & SUB-CHAIN BREAKDOWN] ---\n";
        ss << "  " << std::left << std::setw(28) << "DSP Stage / Node"
           << std::right << std::setw(12) << "Avg (us)"
           << std::setw(12) << "P95 (us)"
           << std::setw(12) << "Max (us)"
           << std::setw(12) << "Speedup" << "\n";
        ss << "  ----------------------------------------------------------------------------\n";
        for (const auto& np : nodeProfiles) {
            ss << "  " << std::left << std::setw(28) << np.nodeName
               << std::right << std::setw(12) << std::fixed << std::setprecision(2) << np.avgUs
               << std::setw(12) << np.p95Us
               << std::setw(12) << np.maxUs
               << std::setw(11) << std::fixed << std::setprecision(1) << np.realtimeFactor << "x\n";
        }
        ss << "\n";
    }

    if (telemetry) {
        ss << "--- [7. TELEMETRY OVERHEAD BENCHMARK] ---\n";
        ss << "  DSP without Telemetry  : " << std::fixed << std::setprecision(2) << telemetry->withoutTelemetryAvgUs << " us\n";
        ss << "  DSP with Telemetry     : " << telemetry->withTelemetryAvgUs << " us\n";
        ss << "  Telemetry Overhead     : " << std::showpos << telemetry->overheadUs << std::noshowpos << " us ("
           << std::showpos << telemetry->overheadPercent << std::noshowpos << " %)\n\n";
    }

    if (multiCore) {
        ss << "--- [8. CONTROLLED MULTI-CORE WORKLOAD BENCHMARK] ---\n";
        ss << CpuWorkloadBenchmark::formatTable(*multiCore) << "\n";
    }

    if (correctness) {
        ss << "--- [9. DSP CORRECTNESS SUITE] ---\n";
        ss << "  Suite Passed           : " << (correctness->allPassed ? "YES" : "NO") << "\n";
        ss << "  Tests Passed           : " << correctness->passedCount << "\n";
        ss << "  Tests Failed           : " << correctness->failedCount << "\n";
        for (const auto& it : correctness->items) {
            ss << "  [" << (it.passed ? "PASS" : "FAIL") << "] " << it.testId << ": " << it.description << "\n";
        }
        ss << "\n";
    }

    ss << "================================================================================\n";
    ss << "END OF PERFORMANCE REPORT\n";
    ss << "================================================================================\n";

    return ss.str();
}

std::string ReportExporter::generateJsonReport(
    const BenchmarkMetrics& m,
    const std::vector<NodeProfileResult>& nodeProfiles,
    const TelemetryOverheadResult* telemetry,
    const CpuUtilizationSnapshot* cpuSnapshot,
    const MultiCoreBenchmarkResult* multiCore,
    const PipelineTimingMetrics* pipeline)
{
    auto tel = CpuTelemetry::create();
    auto hw = tel->getHardwareInfo();

    std::ostringstream ss;
    ss << "{\n";
    ss << "    \"hardware\": {\n";
    ss << "        \"architecture\": \"" << hw.architectureName << "\",\n";
    ss << "        \"model\": \"" << hw.modelName << "\",\n";
    ss << "        \"logical_cpus\": " << hw.logicalCpuCount << ",\n";
    ss << "        \"online_cpus\": " << hw.onlineCpuCount << "\n";
    ss << "    },\n";

    ss << "    \"cpu\": {\n";
    if (cpuSnapshot && cpuSnapshot->valid) {
        ss << "        \"system_percent\": " << std::fixed << std::setprecision(2) << cpuSnapshot->systemTotalPercent << ",\n";
        ss << "        \"process_percent\": " << cpuSnapshot->processSystemEquivalentPercent << ",\n";
        ss << "        \"process_logical_cores_equivalent\": " << cpuSnapshot->processLogicalCoresEquivalent << ",\n";
        ss << "        \"audio_thread_percent\": " << cpuSnapshot->audioThreadCorePercent << ",\n";
        ss << "        \"audio_thread_current_cpu\": " << cpuSnapshot->audioThreadCurrentCpu << "\n";
    } else {
        ss << "        \"system_percent\": 0.0,\n";
        ss << "        \"process_percent\": 0.0,\n";
        ss << "        \"process_logical_cores_equivalent\": 0.0,\n";
        ss << "        \"audio_thread_percent\": 0.0,\n";
        ss << "        \"audio_thread_current_cpu\": -1\n";
    }
    ss << "    },\n";

    ss << "    \"dsp\": {\n";
    ss << "        \"average_us\": " << std::fixed << std::setprecision(2) << m.avgUs << ",\n";
    ss << "        \"p99_us\": " << m.p99Us << ",\n";
    ss << "        \"max_us\": " << m.maxUs << ",\n";
    ss << "        \"realtime_utilization\": " << std::setprecision(6) << m.avgRealtimeUtilization << "\n";
    ss << "    },\n";

    ss << "    \"input\": {\n";
    ss << "        \"sample_rate\": " << m.sampleRate << ",\n";
    ss << "        \"channels\": " << m.channels << ",\n";
    ss << "        \"frames\": " << m.totalFrames << ",\n";
    ss << "        \"duration_seconds\": " << std::fixed << std::setprecision(4) << m.audioDurationSeconds << "\n";
    ss << "    },\n";
    ss << "    \"engine\": {\n";
    ss << "        \"block_size\": " << m.blockSize << "\n";
    ss << "    },\n";
    ss << "    \"benchmark\": {\n";
    ss << "        \"average_us\": " << std::fixed << std::setprecision(2) << m.avgUs << ",\n";
    ss << "        \"median_us\": " << m.medianUs << ",\n";
    ss << "        \"min_us\": " << m.minUs << ",\n";
    ss << "        \"p95_us\": " << m.p95Us << ",\n";
    ss << "        \"p99_us\": " << m.p99Us << ",\n";
    ss << "        \"max_us\": " << m.maxUs << ",\n";
    ss << "        \"std_dev_us\": " << m.stdDevUs << ",\n";
    ss << "        \"jitter_us\": " << m.jitterUs << "\n";
    ss << "    },\n";
    ss << "    \"realtime\": {\n";
    ss << "        \"factor\": " << std::fixed << std::setprecision(2) << m.realtimeFactor << ",\n";
    ss << "        \"average_utilization\": " << std::setprecision(6) << m.avgRealtimeUtilization << ",\n";
    ss << "        \"worst_utilization\": " << m.worstRealtimeUtilization << ",\n";
    ss << "        \"deadline_violations\": " << m.deadlineViolations << "\n";
    ss << "    },\n";
    ss << "    \"validation\": {\n";
    ss << "        \"nan\": " << m.nanCount << ",\n";
    ss << "        \"infinity\": " << m.infCount << ",\n";
    ss << "        \"invalid_samples\": " << m.invalidSampleCount << ",\n";
    ss << "        \"clipped_samples\": " << m.clippedSampleCount << "\n";
    ss << "    },\n";
    ss << "    \"status\": \"" << m.status << "\"";

    if (!nodeProfiles.empty()) {
        ss << ",\n    \"node_breakdown\": [\n";
        for (size_t i = 0; i < nodeProfiles.size(); ++i) {
            const auto& np = nodeProfiles[i];
            ss << "        {\n";
            ss << "            \"node\": \"" << np.nodeName << "\",\n";
            ss << "            \"average_us\": " << std::fixed << std::setprecision(2) << np.avgUs << ",\n";
            ss << "            \"min_us\": " << np.minUs << ",\n";
            ss << "            \"max_us\": " << np.maxUs << ",\n";
            ss << "            \"p95_us\": " << np.p95Us << ",\n";
            ss << "            \"p99_us\": " << np.p99Us << ",\n";
            ss << "            \"std_dev_us\": " << np.stdDevUs << ",\n";
            ss << "            \"realtime_factor\": " << np.realtimeFactor << "\n";
            ss << "        }" << (i + 1 < nodeProfiles.size() ? "," : "") << "\n";
        }
        ss << "    ]";
    }

    if (telemetry) {
        ss << ",\n    \"telemetry_overhead\": {\n";
        ss << "        \"without_telemetry_avg_us\": " << std::fixed << std::setprecision(2) << telemetry->withoutTelemetryAvgUs << ",\n";
        ss << "        \"with_telemetry_avg_us\": " << telemetry->withTelemetryAvgUs << ",\n";
        ss << "        \"overhead_us\": " << telemetry->overheadUs << ",\n";
        ss << "        \"overhead_percent\": " << telemetry->overheadPercent << "\n";
        ss << "    }";
    }

    if (multiCore) {
        ss << ",\n    \"multi_core_benchmark\": [\n";
        for (size_t i = 0; i < multiCore->rows.size(); ++i) {
            const auto& r = multiCore->rows[i];
            ss << "        {\n";
            ss << "            \"workers\": " << r.workerCount << ",\n";
            ss << "            \"wall_time_ms\": " << std::fixed << std::setprecision(2) << r.wallTimeMs << ",\n";
            ss << "            \"total_cpu_time_ms\": " << r.totalCpuTimeMs << ",\n";
            ss << "            \"speedup\": " << r.speedup << ",\n";
            ss << "            \"efficiency\": " << r.scalingEfficiency << ",\n";
            ss << "            \"status\": \"" << r.status << "\"\n";
            ss << "        }" << (i + 1 < multiCore->rows.size() ? "," : "") << "\n";
        }
        ss << "    ]";
    }

    if (pipeline) {
        ss << ",\n    \"pipeline_timing\": {\n";
        ss << "        \"in_sample_rate\": " << pipeline->inSampleRate << ",\n";
        ss << "        \"engine_sample_rate\": " << pipeline->engineSampleRate << ",\n";
        ss << "        \"src_status\": \"" << pipeline->srcStatusText << "\",\n";
        ss << "        \"decoder_time_ms\": " << std::fixed << std::setprecision(2) << pipeline->decoderTimeMs << ",\n";
        ss << "        \"src_time_ms\": " << pipeline->srcTimeMs << ",\n";
        ss << "        \"dsp_time_ms\": " << pipeline->dspTimeMs << ",\n";
        ss << "        \"output_time_ms\": " << pipeline->outputTimeMs << ",\n";
        ss << "        \"total_pipeline_time_ms\": " << pipeline->totalTimeMs << "\n";
        ss << "    }";
    }

    ss << "\n}\n";
    return ss.str();
}

std::string ReportExporter::generateCsvReport(
    const BenchmarkMetrics& m,
    const std::vector<double>& blockTimesUs,
    const std::vector<NodeProfileResult>& nodeProfiles,
    const CpuUtilizationSnapshot* cpuSnapshot,
    const PipelineTimingMetrics* pipeline)
{
    auto tel = CpuTelemetry::create();
    auto hw = tel->getHardwareInfo();

    std::ostringstream ss;
    ss << "# AUDIO CORE DSP TEST BENCH CSV EXPORT\n";
    ss << "# Architecture: " << hw.architectureName << " | Model: " << hw.modelName << "\n";
    ss << "# Logical CPUs: " << hw.logicalCpuCount << " | Online CPUs: " << hw.onlineCpuCount << "\n";
    if (cpuSnapshot && cpuSnapshot->valid) {
        ss << "# System CPU: " << cpuSnapshot->systemTotalPercent << "% | Process CPU: "
           << cpuSnapshot->processSystemEquivalentPercent << "%\n";
    }
    ss << "Metric,Value,Unit\n";
    ss << "SampleRate," << m.sampleRate << ",Hz\n";
    ss << "Channels," << m.channels << ",channels\n";
    ss << "BlockSize," << m.blockSize << ",frames\n";
    ss << "TotalFrames," << m.totalFrames << ",frames\n";
    ss << "TotalBlocks," << m.totalBlocks << ",blocks\n";
    ss << "Duration," << std::fixed << std::setprecision(4) << m.audioDurationSeconds << ",s\n";
    ss << "TotalDspTimeUs," << std::fixed << std::setprecision(2) << m.totalDspTimeUs << ",us\n";
    ss << "AverageUs," << m.avgUs << ",us\n";
    ss << "MedianUs," << m.medianUs << ",us\n";
    ss << "MinUs," << m.minUs << ",us\n";
    ss << "P95Us," << m.p95Us << ",us\n";
    ss << "P99Us," << m.p99Us << ",us\n";
    ss << "MaxUs," << m.maxUs << ",us\n";
    ss << "StdDevUs," << m.stdDevUs << ",us\n";
    ss << "JitterUs," << m.jitterUs << ",us\n";
    ss << "RealtimeFactor," << std::fixed << std::setprecision(2) << m.realtimeFactor << ",x\n";
    ss << "AverageRealtimeUtil," << std::setprecision(6) << m.avgRealtimeUtilization << ",ratio\n";
    ss << "WorstRealtimeUtil," << m.worstRealtimeUtilization << ",ratio\n";
    ss << "DeadlineViolations," << m.deadlineViolations << ",count\n";
    ss << "NaNCount," << m.nanCount << ",count\n";
    ss << "InfCount," << m.infCount << ",count\n";
    ss << "InvalidCount," << m.invalidSampleCount << ",count\n";
    ss << "ClippedCount," << m.clippedSampleCount << ",count\n";
    ss << "Status," << m.status << ",string\n\n";

    if (!nodeProfiles.empty()) {
        ss << "# Node Breakdown\n";
        ss << "NodeName,AvgUs,MinUs,MaxUs,P95Us,P99Us,StdDevUs,RealtimeFactor\n";
        for (const auto& np : nodeProfiles) {
            ss << np.nodeName << ","
               << std::fixed << std::setprecision(2) << np.avgUs << ","
               << np.minUs << ","
               << np.maxUs << ","
               << np.p95Us << ","
               << np.p99Us << ","
               << np.stdDevUs << ","
               << np.realtimeFactor << "\n";
        }
        ss << "\n";
    }

    if (!blockTimesUs.empty()) {
        ss << "# Per-Block Processing Times\n";
        ss << "BlockIndex,DurationUs\n";
        for (size_t i = 0; i < blockTimesUs.size(); ++i) {
            ss << i << "," << std::fixed << std::setprecision(2) << blockTimesUs[i] << "\n";
        }
        ss << "\n";
    }

    if (pipeline) {
        ss << "# Pipeline Timing Breakdown\n";
        ss << "InputSampleRate," << pipeline->inSampleRate << ",Hz\n";
        ss << "EngineSampleRate," << pipeline->engineSampleRate << ",Hz\n";
        ss << "SrcStatus," << pipeline->srcStatusText << ",string\n";
        ss << "DecoderTimeMs," << std::fixed << std::setprecision(2) << pipeline->decoderTimeMs << ",ms\n";
        ss << "SrcTimeMs," << pipeline->srcTimeMs << ",ms\n";
        ss << "DspTimeMs," << pipeline->dspTimeMs << ",ms\n";
        ss << "OutputTimeMs," << pipeline->outputTimeMs << ",ms\n";
        ss << "TotalPipelineTimeMs," << pipeline->totalTimeMs << ",ms\n";
    }

    return ss.str();
}

} // namespace testbench
