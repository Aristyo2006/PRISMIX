#pragma once

#include "audiocore/audio_buffer.hpp"
#include "audiocore/audio_engine.hpp"
#include "audiocore/audio_graph.hpp"
#include "benchmark_metrics.hpp"
#include <vector>
#include <string>
#include <memory>
#include <functional>

#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace testbench {

class HighResolutionTimer {
public:
    HighResolutionTimer();
    uint64_t nowTicks() const noexcept;
    double ticksToMicroseconds(uint64_t ticks) const noexcept;

private:
#if defined(_WIN32)
    double frequency_{1.0};
#else
    // Fallback for portable platforms
#endif
};

enum class DspChainPreset {
    Bypass,
    GainOnly,
    PanOnly,
    EqOnly,
    GateOnly,
    Geq1Band,
    Geq8Bands,
    Geq16Bands,
    Geq31Bands,
    CompressorOnly,
    LimiterOnly,
    MeterOnly,
    GainEq,
    GainEqComp,
    GainEqCompLim,
    FullChainWithGate,
    FullChainWithGeq,
    FullChain,
    DelayOnly,
    ReverbOnly,
    AfsOnly,
    ReverbDelay,
    ReverbAfs,
    DelayAfs,
    AllFx,
    FullAudioCoreV11
};

struct ProfilerRunConfig {
    double sampleRate{48000.0};
    uint32_t channels{2};
    uint32_t blockSize{128};
    DspChainPreset chainPreset{DspChainPreset::FullChain};
    bool trackPerBlockTimes{true};
};

struct TelemetryOverheadResult {
    double withoutTelemetryAvgUs{0.0};
    double withTelemetryAvgUs{0.0};
    double overheadUs{0.0};
    double overheadPercent{0.0};
};

struct PipelineTimingMetrics {
    double decoderTimeMs{0.0};
    double srcTimeMs{0.0};
    double dspTimeMs{0.0};
    double outputTimeMs{0.0};
    double totalTimeMs{0.0};
    bool srcActive{false};
    double inSampleRate{48000.0};
    double engineSampleRate{48000.0};
    std::string srcStatusText{"BYPASS"};
};

class DspProfiler {
public:
    DspProfiler();
    ~DspProfiler() = default;

    // Configures the audio graph for a given preset
    static void configureGraph(
        audiocore::AudioGraph& graph,
        DspChainPreset preset,
        double sampleRate,
        uint32_t channels);

    // Benchmarks the engine with a prepared input buffer block-by-block
    BenchmarkMetrics profileBuffer(
        const audiocore::AudioBuffer& inputBuffer,
        audiocore::AudioBuffer& outputBuffer,
        const ProfilerRunConfig& config,
        std::vector<double>* outBlockTimesUs = nullptr);

    // Profiles all individual nodes and composite sub-chains
    std::vector<NodeProfileResult> profileAllNodes(
        const audiocore::AudioBuffer& inputBuffer,
        double sampleRate,
        uint32_t channels,
        uint32_t blockSize);

    // Measures real-time DSP cost difference with vs without UI telemetry
    TelemetryOverheadResult measureTelemetryOverhead(
        const audiocore::AudioBuffer& inputBuffer,
        double sampleRate,
        uint32_t channels,
        uint32_t blockSize);

    static const char* presetToString(DspChainPreset preset) noexcept;

private:
    HighResolutionTimer timer_;
};

} // namespace testbench

