#include "profiler.hpp"
#include "audiocore/gain_node.hpp"
#include "audiocore/pan_node.hpp"
#include "audiocore/mixer_node.hpp"
#include "audiocore/eq_node.hpp"
#include "audiocore/compressor_node.hpp"
#include "audiocore/limiter_node.hpp"
#include "audiocore/meter_node.hpp"
#include "audiocore/gate_node.hpp"
#include "audiocore/geq_node.hpp"
#include "audiocore/delay_node.hpp"
#include "audiocore/reverb_node.hpp"
#include "audiocore/afs_node.hpp"

#include <chrono>
#include <cmath>
#include <cstring>
#include <algorithm>

namespace testbench {

HighResolutionTimer::HighResolutionTimer() {
#if defined(_WIN32)
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    frequency_ = static_cast<double>(freq.QuadPart);
#endif
}

uint64_t HighResolutionTimer::nowTicks() const noexcept {
#if defined(_WIN32)
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return static_cast<uint64_t>(counter.QuadPart);
#else
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
#endif
}

double HighResolutionTimer::ticksToMicroseconds(uint64_t ticks) const noexcept {
#if defined(_WIN32)
    return (static_cast<double>(ticks) / frequency_) * 1000000.0;
#else
    return static_cast<double>(ticks) / 1000.0;
#endif
}

DspProfiler::DspProfiler() = default;

const char* DspProfiler::presetToString(DspChainPreset preset) noexcept {
    switch (preset) {
        case DspChainPreset::Bypass: return "Bypass (No DSP)";
        case DspChainPreset::GainOnly: return "GainNode Only";
        case DspChainPreset::PanOnly: return "PanNode Only";
        case DspChainPreset::EqOnly: return "ParametricEQNode Only (4-Band)";
        case DspChainPreset::GateOnly: return "GateNode Only";
        case DspChainPreset::Geq1Band: return "GraphicEQNode (1 Band)";
        case DspChainPreset::Geq8Bands: return "GraphicEQNode (8 Bands)";
        case DspChainPreset::Geq16Bands: return "GraphicEQNode (16 Bands)";
        case DspChainPreset::Geq31Bands: return "GraphicEQNode (31 Bands)";
        case DspChainPreset::CompressorOnly: return "CompressorNode Only";
        case DspChainPreset::LimiterOnly: return "LimiterNode Only";
        case DspChainPreset::MeterOnly: return "MeterNode Only";
        case DspChainPreset::GainEq: return "Gain -> EQ";
        case DspChainPreset::GainEqComp: return "Gain -> EQ -> Compressor";
        case DspChainPreset::GainEqCompLim: return "Gain -> EQ -> Compressor -> Limiter";
        case DspChainPreset::FullChainWithGate: return "Full Chain + Gate";
        case DspChainPreset::FullChainWithGeq: return "Full Chain + GEQ";
        case DspChainPreset::FullChain: return "Full DSP Chain v1.0 (Mixer+Gain+Pan+EQ+Gate+GEQ+Comp+Lim+Meter)";
        case DspChainPreset::DelayOnly: return "TapDelayNode Only";
        case DspChainPreset::ReverbOnly: return "ReverbNode Only";
        case DspChainPreset::AfsOnly: return "AfsNode Only";
        case DspChainPreset::ReverbDelay: return "Reverb + Delay";
        case DspChainPreset::ReverbAfs: return "Reverb + AFS";
        case DspChainPreset::DelayAfs: return "Delay + AFS";
        case DspChainPreset::AllFx: return "Reverb + Delay + AFS";
        case DspChainPreset::FullAudioCoreV11: return "Full AUDIO CORE v1.1 (Mixer+Gain+Pan+EQ+Gate+GEQ+Comp+Delay+Reverb+AFS+Lim+Meter)";
        default: return "Unknown";
    }
}

void DspProfiler::configureGraph(
    audiocore::AudioGraph& graph,
    DspChainPreset preset,
    double sampleRate,
    uint32_t channels)
{
    graph.clearNodes();

    if (preset == DspChainPreset::Bypass) {
        return; // Empty graph acts as direct passthrough
    }

    if (preset == DspChainPreset::GainOnly) {
        graph.addNode(std::make_shared<audiocore::GainNode>(-6.0f));
        return;
    }

    if (preset == DspChainPreset::PanOnly) {
        graph.addNode(std::make_shared<audiocore::PanNode>(0.0f));
        return;
    }

    if (preset == DspChainPreset::EqOnly) {
        auto eq = std::make_shared<audiocore::ParametricEQNode>(4, sampleRate);
        eq->setBand(0, audiocore::FilterType::LowShelf, 100.0, 0.7071, 0.0);
        eq->setBand(1, audiocore::FilterType::Peaking, 1000.0, 1.0, 2.0);
        eq->setBand(2, audiocore::FilterType::Peaking, 2500.0, 1.0, 0.0);
        eq->setBand(3, audiocore::FilterType::HighShelf, 10000.0, 0.7071, 0.0);
        graph.addNode(eq);
        return;
    }

    if (preset == DspChainPreset::GateOnly) {
        auto gate = std::make_shared<audiocore::GateNode>(sampleRate);
        gate->setThresholdDb(-40.0f);
        graph.addNode(gate);
        return;
    }

    if (preset == DspChainPreset::Geq1Band) {
        auto geq = std::make_shared<audiocore::GraphicEQNode>(sampleRate);
        geq->setBandGainDb(17, 3.0f); // 1 kHz band
        graph.addNode(geq);
        return;
    }

    if (preset == DspChainPreset::Geq8Bands) {
        auto geq = std::make_shared<audiocore::GraphicEQNode>(sampleRate);
        for (size_t b = 0; b < 8; ++b) {
            geq->setBandGainDb(b * 4, ((b % 2 == 0) ? 2.0f : -2.0f));
        }
        graph.addNode(geq);
        return;
    }

    if (preset == DspChainPreset::Geq16Bands) {
        auto geq = std::make_shared<audiocore::GraphicEQNode>(sampleRate);
        for (size_t b = 0; b < 16; ++b) {
            geq->setBandGainDb(b * 2, ((b % 2 == 0) ? 2.0f : -2.0f));
        }
        graph.addNode(geq);
        return;
    }

    if (preset == DspChainPreset::Geq31Bands) {
        auto geq = std::make_shared<audiocore::GraphicEQNode>(sampleRate);
        for (size_t b = 0; b < 31; ++b) {
            geq->setBandGainDb(b, ((b % 2 == 0) ? 2.0f : -2.0f));
        }
        graph.addNode(geq);
        return;
    }

    if (preset == DspChainPreset::CompressorOnly) {
        auto comp = std::make_shared<audiocore::CompressorNode>(sampleRate);
        comp->setThresholdDb(-12.0f);
        comp->setRatio(4.0f);
        comp->setAttackMs(10.0f);
        comp->setReleaseMs(100.0f);
        comp->setMakeupGainDb(0.0f);
        graph.addNode(comp);
        return;
    }

    if (preset == DspChainPreset::LimiterOnly) {
        auto lim = std::make_shared<audiocore::LimiterNode>(-1.0f, sampleRate);
        lim->setReleaseMs(50.0f);
        graph.addNode(lim);
        return;
    }

    if (preset == DspChainPreset::MeterOnly) {
        graph.addNode(std::make_shared<audiocore::MeterNode>());
        return;
    }

    if (preset == DspChainPreset::GainEq) {
        graph.addNode(std::make_shared<audiocore::GainNode>(-6.0f));
        auto eq = std::make_shared<audiocore::ParametricEQNode>(4, sampleRate);
        eq->setBand(1, audiocore::FilterType::Peaking, 1000.0, 1.0, 6.0);
        graph.addNode(eq);
        return;
    }

    if (preset == DspChainPreset::GainEqComp) {
        graph.addNode(std::make_shared<audiocore::GainNode>(-6.0f));
        auto eq = std::make_shared<audiocore::ParametricEQNode>(4, sampleRate);
        eq->setBand(1, audiocore::FilterType::Peaking, 1000.0, 1.0, 6.0);
        graph.addNode(eq);
        auto comp = std::make_shared<audiocore::CompressorNode>(sampleRate);
        comp->setThresholdDb(-12.0f);
        comp->setRatio(4.0f);
        graph.addNode(comp);
        return;
    }

    if (preset == DspChainPreset::GainEqCompLim) {
        graph.addNode(std::make_shared<audiocore::GainNode>(0.0f));
        auto eq = std::make_shared<audiocore::ParametricEQNode>(4, sampleRate);
        eq->setBand(1, audiocore::FilterType::Peaking, 1000.0, 1.0, 6.0);
        graph.addNode(eq);
        auto comp = std::make_shared<audiocore::CompressorNode>(sampleRate);
        comp->setThresholdDb(-12.0f);
        comp->setRatio(4.0f);
        comp->setMakeupGainDb(12.0f);
        graph.addNode(comp);
        auto lim = std::make_shared<audiocore::LimiterNode>(-1.0f, sampleRate);
        graph.addNode(lim);
        return;
    }

    if (preset == DspChainPreset::FullChainWithGate) {
        auto mixer = std::make_shared<audiocore::MixerNode>(channels);
        mixer->setMasterGainDb(0.0f);
        graph.addNode(mixer);
        graph.addNode(std::make_shared<audiocore::GainNode>(0.0f));
        graph.addNode(std::make_shared<audiocore::PanNode>(0.0f));

        auto eq = std::make_shared<audiocore::ParametricEQNode>(4, sampleRate);
        graph.addNode(eq);

        auto gate = std::make_shared<audiocore::GateNode>(sampleRate);
        gate->setThresholdDb(-40.0f);
        graph.addNode(gate);

        auto comp = std::make_shared<audiocore::CompressorNode>(sampleRate);
        comp->setThresholdDb(-12.0f);
        comp->setRatio(2.0f);
        graph.addNode(comp);

        auto lim = std::make_shared<audiocore::LimiterNode>(-1.0f, sampleRate);
        graph.addNode(lim);
        graph.addNode(std::make_shared<audiocore::MeterNode>());
        return;
    }

    if (preset == DspChainPreset::FullChainWithGeq) {
        auto mixer = std::make_shared<audiocore::MixerNode>(channels);
        mixer->setMasterGainDb(0.0f);
        graph.addNode(mixer);
        graph.addNode(std::make_shared<audiocore::GainNode>(0.0f));
        graph.addNode(std::make_shared<audiocore::PanNode>(0.0f));

        auto eq = std::make_shared<audiocore::ParametricEQNode>(4, sampleRate);
        graph.addNode(eq);

        auto geq = std::make_shared<audiocore::GraphicEQNode>(sampleRate);
        geq->setBandGainDb(17, 2.0f);
        graph.addNode(geq);

        auto comp = std::make_shared<audiocore::CompressorNode>(sampleRate);
        comp->setThresholdDb(-12.0f);
        comp->setRatio(2.0f);
        graph.addNode(comp);

        auto lim = std::make_shared<audiocore::LimiterNode>(-1.0f, sampleRate);
        graph.addNode(lim);
        graph.addNode(std::make_shared<audiocore::MeterNode>());
        return;
    }

    if (preset == DspChainPreset::FullChain) {
        auto mixer = std::make_shared<audiocore::MixerNode>(channels);
        mixer->setChannelGainDb(0, 0.0f);
        mixer->setChannelPan(0, -1.0f);
        if (channels > 1) {
            mixer->setChannelGainDb(1, 0.0f);
            mixer->setChannelPan(1, 1.0f);
        }
        mixer->setMasterGainDb(0.0f);
        graph.addNode(mixer);

        graph.addNode(std::make_shared<audiocore::GainNode>(0.0f));
        graph.addNode(std::make_shared<audiocore::PanNode>(0.0f));

        auto eq = std::make_shared<audiocore::ParametricEQNode>(4, sampleRate);
        eq->setBand(0, audiocore::FilterType::LowShelf, 100.0, 0.7071, 0.0);
        eq->setBand(1, audiocore::FilterType::Peaking, 440.0, 1.0, 2.0);
        eq->setBand(2, audiocore::FilterType::Peaking, 2500.0, 1.0, 0.0);
        eq->setBand(3, audiocore::FilterType::HighShelf, 10000.0, 0.7071, 0.0);
        graph.addNode(eq);

        auto gate = std::make_shared<audiocore::GateNode>(sampleRate);
        gate->setThresholdDb(-40.0f);
        graph.addNode(gate);

        auto geq = std::make_shared<audiocore::GraphicEQNode>(sampleRate);
        geq->setBandGainDb(17, 1.0f);
        graph.addNode(geq);

        auto comp = std::make_shared<audiocore::CompressorNode>(sampleRate);
        comp->setThresholdDb(-12.0f);
        comp->setRatio(2.0f);
        comp->setAttackMs(10.0f);
        comp->setReleaseMs(100.0f);
        comp->setMakeupGainDb(0.0f);
        graph.addNode(comp);

        auto lim = std::make_shared<audiocore::LimiterNode>(-1.0f, sampleRate);
        lim->setReleaseMs(50.0f);
        graph.addNode(lim);

        graph.addNode(std::make_shared<audiocore::MeterNode>());
        return;
    }

    if (preset == DspChainPreset::DelayOnly) {
        auto delay = std::make_shared<audiocore::DelayNode>(sampleRate);
        graph.addNode(delay);
        return;
    }

    if (preset == DspChainPreset::ReverbOnly) {
        auto reverb = std::make_shared<audiocore::ReverbNode>(sampleRate);
        graph.addNode(reverb);
        return;
    }

    if (preset == DspChainPreset::AfsOnly) {
        auto afs = std::make_shared<audiocore::AfsNode>(sampleRate);
        graph.addNode(afs);
        return;
    }

    if (preset == DspChainPreset::ReverbDelay) {
        auto delay = std::make_shared<audiocore::DelayNode>(sampleRate);
        auto reverb = std::make_shared<audiocore::ReverbNode>(sampleRate);
        graph.addNode(delay);
        graph.addNode(reverb);
        return;
    }

    if (preset == DspChainPreset::ReverbAfs) {
        auto reverb = std::make_shared<audiocore::ReverbNode>(sampleRate);
        auto afs = std::make_shared<audiocore::AfsNode>(sampleRate);
        graph.addNode(reverb);
        graph.addNode(afs);
        return;
    }

    if (preset == DspChainPreset::DelayAfs) {
        auto delay = std::make_shared<audiocore::DelayNode>(sampleRate);
        auto afs = std::make_shared<audiocore::AfsNode>(sampleRate);
        graph.addNode(delay);
        graph.addNode(afs);
        return;
    }

    if (preset == DspChainPreset::AllFx) {
        auto delay = std::make_shared<audiocore::DelayNode>(sampleRate);
        auto reverb = std::make_shared<audiocore::ReverbNode>(sampleRate);
        auto afs = std::make_shared<audiocore::AfsNode>(sampleRate);
        graph.addNode(delay);
        graph.addNode(reverb);
        graph.addNode(afs);
        return;
    }

    if (preset == DspChainPreset::FullAudioCoreV11) {
        auto mixer = std::make_shared<audiocore::MixerNode>(channels);
        mixer->setChannelGainDb(0, 0.0f);
        mixer->setChannelPan(0, -1.0f);
        if (channels > 1) {
            mixer->setChannelGainDb(1, 0.0f);
            mixer->setChannelPan(1, 1.0f);
        }
        mixer->setMasterGainDb(0.0f);
        graph.addNode(mixer);

        graph.addNode(std::make_shared<audiocore::GainNode>(0.0f));
        graph.addNode(std::make_shared<audiocore::PanNode>(0.0f));

        auto eq = std::make_shared<audiocore::ParametricEQNode>(4, sampleRate);
        eq->setBand(0, audiocore::FilterType::LowShelf, 100.0, 0.7071, 0.0);
        eq->setBand(1, audiocore::FilterType::Peaking, 440.0, 1.0, 2.0);
        eq->setBand(2, audiocore::FilterType::Peaking, 2500.0, 1.0, 0.0);
        eq->setBand(3, audiocore::FilterType::HighShelf, 10000.0, 0.7071, 0.0);
        graph.addNode(eq);

        auto gate = std::make_shared<audiocore::GateNode>(sampleRate);
        gate->setThresholdDb(-40.0f);
        graph.addNode(gate);

        auto geq = std::make_shared<audiocore::GraphicEQNode>(sampleRate);
        geq->setBandGainDb(17, 1.0f);
        graph.addNode(geq);

        auto comp = std::make_shared<audiocore::CompressorNode>(sampleRate);
        comp->setThresholdDb(-12.0f);
        comp->setRatio(2.0f);
        comp->setAttackMs(10.0f);
        comp->setReleaseMs(100.0f);
        comp->setMakeupGainDb(0.0f);
        graph.addNode(comp);

        auto delay = std::make_shared<audiocore::DelayNode>(sampleRate);
        graph.addNode(delay);

        auto reverb = std::make_shared<audiocore::ReverbNode>(sampleRate);
        graph.addNode(reverb);

        auto afs = std::make_shared<audiocore::AfsNode>(sampleRate);
        graph.addNode(afs);

        auto lim = std::make_shared<audiocore::LimiterNode>(-1.0f, sampleRate);
        lim->setReleaseMs(50.0f);
        graph.addNode(lim);

        graph.addNode(std::make_shared<audiocore::MeterNode>());
        return;
    }
}

BenchmarkMetrics DspProfiler::profileBuffer(
    const audiocore::AudioBuffer& inputBuffer,
    audiocore::AudioBuffer& outputBuffer,
    const ProfilerRunConfig& config,
    std::vector<double>* outBlockTimesUs)
{
    const uint32_t totalFrames = inputBuffer.getFrameCount();
    const uint32_t channels = config.channels;
    const uint32_t blockSize = config.blockSize;

    outputBuffer.resize(channels, totalFrames);
    outputBuffer.clear();

    if (totalFrames == 0 || channels == 0 || blockSize == 0) {
        return BenchmarkMetrics::compute({}, static_cast<uint32_t>(config.sampleRate), channels, blockSize, 0, 0, 0, 0);
    }

    audiocore::AudioEngine engine;
    engine.configure(config.sampleRate, blockSize, channels);
    configureGraph(engine.getGraph(), config.chainPreset, config.sampleRate, channels);

    if (!engine.start()) {
        BenchmarkMetrics err;
        err.status = "FAIL";
        err.details = "AudioEngine start failed";
        return err;
    }

    const uint32_t totalBlocks = (totalFrames + blockSize - 1) / blockSize;

    // PREALLOCATE ALL BUFFERS AND VECTORS BEFORE ENTERING MEASUREMENT LOOP
    audiocore::AudioBuffer blockIn(channels, blockSize);
    audiocore::AudioBuffer blockOut(channels, blockSize);
    std::vector<uint64_t> rawBlockTicks(totalBlocks, 0);

    uint32_t frameOffset = 0;
    uint32_t remainingFrames = totalFrames;

    // REALTIME-SAFE MEASURED LOOP: ZERO ALLOCATIONS, ZERO I/O, ZERO LOGGING
    for (uint32_t b = 0; b < totalBlocks; ++b) {
        const uint32_t framesThisBlock = std::min(blockSize, remainingFrames);

        // Copy slice from inputBuffer to blockIn
        for (uint32_t ch = 0; ch < channels; ++ch) {
            const float* src = inputBuffer.getChannelData(ch) + frameOffset;
            float* dst = blockIn.getChannelData(ch);
            std::memcpy(dst, src, framesThisBlock * sizeof(float));
        }

        // --- MEASURED REGION START ---
        const uint64_t t0 = timer_.nowTicks();
        engine.process(blockIn, blockOut, framesThisBlock);
        const uint64_t t1 = timer_.nowTicks();
        // --- MEASURED REGION END ---

        rawBlockTicks[b] = (t1 >= t0) ? (t1 - t0) : 0;

        // Copy slice from blockOut to destination outputBuffer
        for (uint32_t ch = 0; ch < channels; ++ch) {
            const float* src = blockOut.getChannelData(ch);
            float* dst = outputBuffer.getChannelData(ch) + frameOffset;
            std::memcpy(dst, src, framesThisBlock * sizeof(float));
        }

        frameOffset += framesThisBlock;
        remainingFrames -= framesThisBlock;
    }

    engine.stop();

    // Convert raw ticks to microseconds
    std::vector<double> blockDurationsUs(totalBlocks);
    for (uint32_t b = 0; b < totalBlocks; ++b) {
        blockDurationsUs[b] = timer_.ticksToMicroseconds(rawBlockTicks[b]);
    }

    if (outBlockTimesUs) {
        *outBlockTimesUs = blockDurationsUs;
    }

    // Numerical validation
    uint64_t nanCount = 0;
    uint64_t infCount = 0;
    uint64_t clippedCount = 0;

    for (uint32_t ch = 0; ch < channels; ++ch) {
        const float* outPtr = outputBuffer.getChannelData(ch);
        for (uint32_t i = 0; i < totalFrames; ++i) {
            float s = outPtr[i];
            if (std::isnan(s)) nanCount++;
            else if (std::isinf(s)) infCount++;
            else if (std::abs(s) > 1.0f) clippedCount++;
        }
    }

    return BenchmarkMetrics::compute(
        blockDurationsUs,
        static_cast<uint32_t>(config.sampleRate),
        channels,
        blockSize,
        totalFrames,
        nanCount,
        infCount,
        clippedCount);
}

std::vector<NodeProfileResult> DspProfiler::profileAllNodes(
    const audiocore::AudioBuffer& inputBuffer,
    double sampleRate,
    uint32_t channels,
    uint32_t blockSize)
{
    const struct NodeTarget {
        DspChainPreset preset;
        const char* name;
    } targets[] = {
        { DspChainPreset::Bypass, "Bypass" },
        { DspChainPreset::GainOnly, "GainNode" },
        { DspChainPreset::PanOnly, "PanNode" },
        { DspChainPreset::EqOnly, "ParametricEQ" },
        { DspChainPreset::GateOnly, "Noise Gate" },
        { DspChainPreset::Geq1Band, "Graphic EQ (1 Band)" },
        { DspChainPreset::Geq8Bands, "Graphic EQ (8 Bands)" },
        { DspChainPreset::Geq16Bands, "Graphic EQ (16 Bands)" },
        { DspChainPreset::Geq31Bands, "Graphic EQ (31 Bands)" },
        { DspChainPreset::CompressorOnly, "Compressor" },
        { DspChainPreset::LimiterOnly, "Limiter" },
        { DspChainPreset::MeterOnly, "MeterNode" },
        { DspChainPreset::GainEq, "Gain -> EQ" },
        { DspChainPreset::GainEqComp, "Gain -> EQ -> Comp" },
        { DspChainPreset::GainEqCompLim, "Gain -> EQ -> Comp -> Lim" },
        { DspChainPreset::FullChainWithGate, "Full Chain + Gate" },
        { DspChainPreset::FullChainWithGeq, "Full Chain + GEQ" },
        { DspChainPreset::FullChain, "Full DSP Chain v1.0 (9 Nodes)" },
        { DspChainPreset::DelayOnly, "Tap Delay" },
        { DspChainPreset::ReverbOnly, "Internal Reverb" },
        { DspChainPreset::AfsOnly, "Feedback Suppressor (AFS)" },
        { DspChainPreset::ReverbDelay, "Reverb + Delay" },
        { DspChainPreset::ReverbAfs, "Reverb + AFS" },
        { DspChainPreset::DelayAfs, "Delay + AFS" },
        { DspChainPreset::AllFx, "All FX (Reverb+Delay+AFS)" },
        { DspChainPreset::FullAudioCoreV11, "Full AUDIO CORE v1.1 (All 12 Nodes)" }
    };

    std::vector<NodeProfileResult> results;
    results.reserve(sizeof(targets) / sizeof(targets[0]));

    audiocore::AudioBuffer tempOut;

    for (const auto& t : targets) {
        ProfilerRunConfig cfg;
        cfg.sampleRate = sampleRate;
        cfg.channels = channels;
        cfg.blockSize = blockSize;
        cfg.chainPreset = t.preset;
        cfg.trackPerBlockTimes = false;

        BenchmarkMetrics m = profileBuffer(inputBuffer, tempOut, cfg);

        NodeProfileResult res;
        res.nodeName = t.name;
        res.avgUs = m.avgUs;
        res.minUs = m.minUs;
        res.maxUs = m.maxUs;
        res.p95Us = m.p95Us;
        res.p99Us = m.p99Us;
        res.stdDevUs = m.stdDevUs;
        res.realtimeFactor = m.realtimeFactor;

        results.push_back(res);
    }

    return results;
}

TelemetryOverheadResult DspProfiler::measureTelemetryOverhead(
    const audiocore::AudioBuffer& inputBuffer,
    double sampleRate,
    uint32_t channels,
    uint32_t blockSize)
{
    TelemetryOverheadResult result;
    const uint32_t totalFrames = inputBuffer.getFrameCount();
    if (totalFrames == 0 || channels == 0 || blockSize == 0) return result;

    const uint32_t totalBlocks = (totalFrames + blockSize - 1) / blockSize;

    audiocore::AudioEngine engine;
    engine.configure(sampleRate, blockSize, channels);
    auto& graph = engine.getGraph();
    graph.clearNodes();

    auto inMeter = std::make_shared<audiocore::MeterNode>();
    graph.addNode(inMeter);

    auto mixer = std::make_shared<audiocore::MixerNode>(channels);
    mixer->setChannelGainDb(0, 0.0f);
    mixer->setChannelPan(0, -1.0f);
    if (channels > 1) {
        mixer->setChannelGainDb(1, 0.0f);
        mixer->setChannelPan(1, 1.0f);
    }
    mixer->setMasterGainDb(0.0f);
    graph.addNode(mixer);

    auto gain = std::make_shared<audiocore::GainNode>(0.0f);
    graph.addNode(gain);

    auto pan = std::make_shared<audiocore::PanNode>(0.0f);
    graph.addNode(pan);

    auto eq = std::make_shared<audiocore::ParametricEQNode>(4, sampleRate);
    eq->setBand(0, audiocore::FilterType::LowShelf, 100.0, 0.7071, 0.0);
    eq->setBand(1, audiocore::FilterType::Peaking, 440.0, 1.0, 2.0);
    eq->setBand(2, audiocore::FilterType::Peaking, 2500.0, 1.0, 0.0);
    eq->setBand(3, audiocore::FilterType::HighShelf, 10000.0, 0.7071, 0.0);
    graph.addNode(eq);

    auto comp = std::make_shared<audiocore::CompressorNode>(sampleRate);
    comp->setThresholdDb(-12.0f);
    comp->setRatio(2.0f);
    comp->setAttackMs(10.0f);
    comp->setReleaseMs(100.0f);
    graph.addNode(comp);

    auto lim = std::make_shared<audiocore::LimiterNode>(-1.0f, sampleRate);
    lim->setReleaseMs(50.0f);
    graph.addNode(lim);

    auto outMeter = std::make_shared<audiocore::MeterNode>();
    graph.addNode(outMeter);

    engine.start();

    audiocore::AudioBuffer blockIn(channels, blockSize);
    audiocore::AudioBuffer blockOut(channels, blockSize);

    // Warm-up pass
    for (uint32_t b = 0; b < std::min(totalBlocks, 50u); ++b) {
        uint32_t framesThisBlock = std::min(blockSize, totalFrames - b * blockSize);
        engine.process(blockIn, blockOut, framesThisBlock);
    }

    // Run 3 trials and select the best (least interrupted) trial for each pass
    uint64_t bestTicksWithout = UINT64_MAX;
    for (int trial = 0; trial < 3; ++trial) {
        uint64_t totalTicksWithout = 0;
        uint32_t frameOffset = 0;
        uint32_t remaining = totalFrames;
        for (uint32_t b = 0; b < totalBlocks; ++b) {
            uint32_t framesThisBlock = std::min(blockSize, remaining);
            for (uint32_t ch = 0; ch < channels; ++ch) {
                std::memcpy(blockIn.getChannelData(ch), inputBuffer.getChannelData(ch) + frameOffset, framesThisBlock * sizeof(float));
            }

            uint64_t t0 = timer_.nowTicks();
            engine.process(blockIn, blockOut, framesThisBlock);
            uint64_t t1 = timer_.nowTicks();
            totalTicksWithout += (t1 >= t0) ? (t1 - t0) : 0;

            frameOffset += framesThisBlock;
            remaining -= framesThisBlock;
        }
        if (totalTicksWithout < bestTicksWithout) {
            bestTicksWithout = totalTicksWithout;
        }
    }

    // PASS 2: WITH TELEMETRY POLLING (run 3 trials and select best)
    volatile float dummySink = 0.0f;
    uint64_t bestTicksWith = UINT64_MAX;
    for (int trial = 0; trial < 3; ++trial) {
        uint64_t totalTicksWith = 0;
        uint32_t frameOffset = 0;
        uint32_t remaining = totalFrames;
        for (uint32_t b = 0; b < totalBlocks; ++b) {
            uint32_t framesThisBlock = std::min(blockSize, remaining);
            for (uint32_t ch = 0; ch < channels; ++ch) {
                std::memcpy(blockIn.getChannelData(ch), inputBuffer.getChannelData(ch) + frameOffset, framesThisBlock * sizeof(float));
            }

            uint64_t t0 = timer_.nowTicks();
            engine.process(blockIn, blockOut, framesThisBlock);
            dummySink += inMeter->getPeak(0) + inMeter->getRms(0);
            dummySink += outMeter->getPeak(0) + outMeter->getRms(0);
            dummySink += comp->getCurrentGainReductionDb();
            dummySink += lim->getCurrentGain();
            uint64_t t1 = timer_.nowTicks();
            totalTicksWith += (t1 >= t0) ? (t1 - t0) : 0;

            frameOffset += framesThisBlock;
            remaining -= framesThisBlock;
        }
        if (totalTicksWith < bestTicksWith) {
            bestTicksWith = totalTicksWith;
        }
    }

    engine.stop();
    (void)dummySink;

    double avgUsWithout = timer_.ticksToMicroseconds(bestTicksWithout) / totalBlocks;
    double avgUsWith = timer_.ticksToMicroseconds(bestTicksWith) / totalBlocks;

    result.withoutTelemetryAvgUs = avgUsWithout;
    result.withTelemetryAvgUs = avgUsWith;
    result.overheadUs = (avgUsWith > avgUsWithout) ? (avgUsWith - avgUsWithout) : 0.0;
    result.overheadPercent = (avgUsWithout > 0.0) ? ((result.overheadUs / avgUsWithout) * 100.0) : 0.0;
    return result;
}

} // namespace testbench

