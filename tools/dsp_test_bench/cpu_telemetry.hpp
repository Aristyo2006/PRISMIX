#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace testbench {

enum class CpuArchitecture {
    Unknown,
    X86,
    X86_64,
    ARM,
    ARM64,
    RISCV64
};

struct CpuHardwareInfo {
    CpuArchitecture architecture{CpuArchitecture::Unknown};
    std::string architectureName{"Unknown"};
    std::string modelName{"Unknown"};
    uint32_t logicalCpuCount{0};
    uint32_t onlineCpuCount{0};
    uint64_t affinityMask{0};
    std::vector<uint32_t> affinityCpus;
    bool isHeterogeneous{false};
    double maxFrequencyMhz{0.0};
    double currentFrequencyMhz{0.0};
};

struct PerCoreUtilization {
    uint32_t coreIndex{0};
    double userPercent{0.0};
    double kernelPercent{0.0};
    double totalPercent{0.0};
};

struct CpuUtilizationSnapshot {
    // System CPU
    double systemTotalPercent{0.0};
    double systemKernelPercent{0.0};
    double systemUserPercent{0.0};

    // Process CPU
    double processSystemEquivalentPercent{0.0}; // Normalized to total system capacity (e.g. 3.7%)
    double processLogicalCoresEquivalent{0.0};   // Normalized to 1 logical CPU (e.g. 0.15 cores)
    double processUserTimeUs{0.0};
    double processKernelTimeUs{0.0};
    double processTotalCpuTimeUs{0.0};

    // Audio Thread CPU
    double audioThreadSystemEquivalentPercent{0.0};
    double audioThreadCorePercent{0.0};          // Thread CPU % relative to 1 core
    double audioThreadCpuTimeUs{0.0};            // Accumulated CPU time for audio thread
    int32_t audioThreadCurrentCpu{-1};           // Core currently executing audio thread

    // Per-core breakdown
    std::vector<PerCoreUtilization> perCore;

    // Measurement metadata
    double sampleIntervalSec{0.0};
    bool valid{false};
};

class CpuTelemetry {
public:
    virtual ~CpuTelemetry() = default;

    virtual std::string getPlatformName() const = 0;
    virtual CpuHardwareInfo getHardwareInfo() const = 0;
    virtual CpuUtilizationSnapshot sampleUtilization(
        void* nativeAudioThreadHandle = nullptr,
        int32_t audioThreadCpuId = -1) = 0;

    static std::unique_ptr<CpuTelemetry> create();
};

class WindowsCpuTelemetry : public CpuTelemetry {
public:
    WindowsCpuTelemetry();
    ~WindowsCpuTelemetry() override;

    std::string getPlatformName() const override { return "Windows"; }
    CpuHardwareInfo getHardwareInfo() const override;
    CpuUtilizationSnapshot sampleUtilization(
        void* nativeAudioThreadHandle = nullptr,
        int32_t audioThreadCpuId = -1) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class LinuxCpuTelemetry : public CpuTelemetry {
public:
    LinuxCpuTelemetry();
    ~LinuxCpuTelemetry() override;

    std::string getPlatformName() const override { return "Linux"; }
    CpuHardwareInfo getHardwareInfo() const override;
    CpuUtilizationSnapshot sampleUtilization(
        void* nativeAudioThreadHandle = nullptr,
        int32_t audioThreadCpuId = -1) override;

    // Portable parser methods for verifying parsing logic & cross-platform portability
    static bool parseProcCpuInfo(
        const std::string& content,
        std::string& outModel,
        std::string& outArch,
        bool& outHetero);

    static bool parseProcStat(
        const std::string& content,
        std::vector<std::vector<uint64_t>>& outCpuTimes);

    static bool parseProcSelfStat(
        const std::string& content,
        uint64_t& outUtime,
        uint64_t& outStime);
};

class UnsupportedCpuTelemetry : public CpuTelemetry {
public:
    std::string getPlatformName() const override { return "Unsupported"; }
    CpuHardwareInfo getHardwareInfo() const override;
    CpuUtilizationSnapshot sampleUtilization(
        void* nativeAudioThreadHandle = nullptr,
        int32_t audioThreadCpuId = -1) override;
};

} // namespace testbench

