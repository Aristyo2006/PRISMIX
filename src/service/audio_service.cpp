#include "audio_service.h"

#include "audiocore/audio_engine.hpp"
#include "audiocore/alignment_delay_node.hpp"
#include "audiocore/afs_node.hpp"
#include "audiocore/compressor_node.hpp"
#include "audiocore/delay_node.hpp"
#include "audiocore/eq_node.hpp"
#include "audiocore/gate_node.hpp"
#include "audiocore/gain_node.hpp"
#include "audiocore/geq_node.hpp"
#include "audiocore/limiter_node.hpp"
#include "audiocore/meter_node.hpp"
#include "audiocore/pan_node.hpp"
#include "audiocore/reverb_node.hpp"
#include "../dsp/lv2/lv2_node.h"
#include "../dsp/lv2/lv2_host.h"
#include "../dsp/nam/nam_node.h"
#include "../dsp/nam/nam_model.h"
#if defined(_WIN32)
#include "backend/wasapi_backend.hpp"
namespace audiocore {
using PlatformBackend = WasapiBackend;
using PlatformDeviceInfo = WasapiDeviceInfo;
}
#else
#include "backend/jack_backend.hpp"
namespace audiocore {
using PlatformBackend = JackBackend;
using PlatformDeviceInfo = JackDeviceInfo;
}
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstring>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>

namespace {
constexpr size_t kCommandCapacity = 128;
static_assert((kCommandCapacity & (kCommandCapacity - 1)) == 0, "capacity must be a power of two");

float clampGain(float value) noexcept { return std::clamp(value, -60.0f, 12.0f); }
float clampPan(float value) noexcept { return std::clamp(value, -1.0f, 1.0f); }

float fxValue(const AudioStripFxConfig& fx, const char* id, float fallback) {
    for (const auto& parameter : fx.parameters) {
        if (parameter.id == id) return parameter.value;
    }
    return fallback;
}

bool isSupportedFx(const std::string& uri) {
    return uri.empty() || uri == "http://audiocore.org/plugins/compressor" ||
        uri == "http://audiocore.org/plugins/eq" || uri == "http://audiocore.org/plugins/reverb" ||
        uri == "http://audiocore.org/plugins/delay" || uri == "http://audiocore.org/plugins/limiter" ||
        uri == "http://audiocore.org/plugins/gate" || uri == "http://audiocore.org/plugins/afs";
}

std::unique_ptr<audiocore::AudioNode> createStripFx(const AudioStripFxConfig& strip, double sampleRate) {
    using namespace audiocore;
    if (strip.uri.empty()) return {};
    if (strip.uri == "http://audiocore.org/plugins/compressor") {
        auto node = std::make_unique<CompressorNode>(sampleRate);
        node->setThresholdDb(fxValue(strip, "thresh", -18.0f));
        node->setRatio(fxValue(strip, "ratio", 4.0f));
        node->setAttackMs(fxValue(strip, "attack", 10.0f));
        node->setReleaseMs(fxValue(strip, "release", 80.0f));
        node->setKneeDb(fxValue(strip, "knee", 6.0f));
        node->setMakeupGainDb(fxValue(strip, "makeup", 0.0f));
        node->setBypassed(strip.bypassed);
        return node;
    }
    if (strip.uri == "http://audiocore.org/plugins/eq") {
        auto node = std::make_unique<ParametricEQNode>(4, sampleRate);
        node->setBand(0, FilterType::LowShelf, fxValue(strip, "low_freq", 80.0f), 0.707,
                      fxValue(strip, "low_gain", 0.0f));
        node->setBand(1, FilterType::Peaking, fxValue(strip, "mid1_freq", 400.0f), 1.0,
                      fxValue(strip, "mid1_gain", 0.0f));
        node->setBand(2, FilterType::Peaking, fxValue(strip, "mid2_freq", 2500.0f), 1.0,
                      fxValue(strip, "mid2_gain", 0.0f));
        node->setBand(3, FilterType::HighShelf, fxValue(strip, "high_freq", 10000.0f), 0.707,
                      fxValue(strip, "high_gain", 0.0f));
        node->setBypassed(strip.bypassed);
        return node;
    }
    if (strip.uri == "http://audiocore.org/plugins/reverb") {
        auto node = std::make_unique<ReverbNode>(sampleRate);
        const int roomIdx = std::clamp(static_cast<int>(std::round(fxValue(strip, "room_type", 1.0f))), 0, 3);
        node->setType(static_cast<ReverbType>(roomIdx));
        node->setDecay(fxValue(strip, "decay", 2.2f));
        node->setPreDelayMs(fxValue(strip, "predelay", 20.0f), true);
        node->setDamping(fxValue(strip, "damping", 0.45f));
        node->setWidth(fxValue(strip, "width", 0.85f));
        node->setMix(fxValue(strip, "mix", 0.30f));
        node->setLowCutHz(fxValue(strip, "low_cut", 100.0f));
        node->setHighCutHz(fxValue(strip, "high_cut", 10000.0f));
        node->setDiffusion(fxValue(strip, "diffusion", 0.75f));
        node->setBypassed(strip.bypassed);
        return node;
    }
    if (strip.uri == "http://audiocore.org/plugins/delay") {
        auto node = std::make_unique<DelayNode>(sampleRate);
        node->setFeedback(fxValue(strip, "feedback", 0.35f));
        node->setMix(fxValue(strip, "mix", 0.25f));
        node->setHighCutHz(fxValue(strip, "tone", 6500.0f));
        node->setLowCutHz(fxValue(strip, "low_cut", 80.0f));
        node->setStereoWidth(fxValue(strip, "width", 0.85f));
        const bool syncEn = fxValue(strip, "sync_en", 0.0f) >= 0.5f;
        const float bpm = fxValue(strip, "bpm", 120.0f);
        const int divIdx = std::clamp(static_cast<int>(std::round(fxValue(strip, "sync_div", 2.0f))), 0, 6);
        const auto div = static_cast<TempoDivision>(divIdx);
        const bool linkLr = fxValue(strip, "link_lr", 0.0f) >= 0.5f;
        node->setTempoSyncEnabled(syncEn);
        node->setBpm(bpm);
        node->setTempoDivision(div);
        const float timeL = syncEn ? DelayNode::calculateTempoDelayMs(bpm, div) : fxValue(strip, "time_l", 320.0f);
        const float timeR = linkLr ? timeL : fxValue(strip, "time_r", 480.0f);
        node->setTapConfig(0, {timeL, 0.0f, -1.0f, true});
        node->setTapConfig(1, {timeR, 0.0f, 1.0f, true});
        node->setTapEnabled(2, false);
        node->setTapEnabled(3, false);
        node->setBypassed(strip.bypassed);
        return node;
    }
    if (strip.uri == "http://audiocore.org/plugins/afs") {
        auto node = std::make_unique<AfsNode>(sampleRate);
        node->setSensitivity(fxValue(strip, "sensitivity", 0.75f));
        node->setMaxNotches(static_cast<size_t>(std::clamp(std::round(fxValue(strip, "max_notches", 8.0f)), 1.0f, 16.0f)));
        node->setMaxDepthDb(fxValue(strip, "max_depth", -18.0f));
        node->setQ(fxValue(strip, "q", 15.0f));
        node->setAttackMs(fxValue(strip, "attack", 20.0f));
        node->setHoldMs(fxValue(strip, "hold", 3000.0f));
        node->setReleaseMs(fxValue(strip, "release", 1000.0f));
        node->setBypassed(strip.bypassed);
        return node;
    }
    if (strip.uri == "http://audiocore.org/plugins/limiter") {
        auto node = std::make_unique<LimiterNode>(fxValue(strip, "ceiling", -0.1f), sampleRate);
        node->setCeilingDb(fxValue(strip, "ceiling", fxValue(strip, "threshold", -0.5f)));
        node->setReleaseMs(fxValue(strip, "release", 50.0f));
        node->setBypassed(strip.bypassed);
        return node;
    }
    if (strip.uri == "http://audiocore.org/plugins/gate") {
        auto node = std::make_unique<GateNode>(sampleRate);
        node->setThresholdDb(fxValue(strip, "thresh", -48.0f));
        node->setRangeDb(fxValue(strip, "range", -60.0f));
        node->setAttackMs(fxValue(strip, "attack", 5.0f));
        node->setHoldMs(fxValue(strip, "hold", 50.0f));
        node->setReleaseMs(fxValue(strip, "release", 120.0f));
        node->setBypassed(strip.bypassed);
        return node;
    }
    return {};
}

std::vector<AudioDeviceChoice> toChoices(const std::vector<audiocore::PlatformDeviceInfo>& devices) {
    std::vector<AudioDeviceChoice> result;
    result.reserve(devices.size());
    for (const auto& device : devices) {
        result.push_back({device.id, device.name.empty() ? device.friendlyName : device.name,
                          device.isDefault, device.channels, device.isCapture});
    }
    return result;
}
}

struct StereoDemoDspNode::Impl {
    struct Command { StereoControlId id; float value; };

    explicit Impl(double rate, uint32_t frames)
        : sampleRate(rate > 0.0 ? rate : 48000.0), maxFrames(std::max(1u, frames)),
          gain(0.0f), pan(0.0f, sampleRate), eq(4, sampleRate), compressor(sampleRate),
          scratchA(2, maxFrames), scratchB(2, maxFrames) {
        eq.setBypassed(false);
        compressor.setThresholdDb(-18.0f);
        compressor.setRatio(3.0f);
        compressor.setAttackMs(10.0f);
        compressor.setReleaseMs(100.0f);
        compressor.setBypassed(false);
    }

    bool enqueue(StereoControlId id, float value) noexcept {
        const uint32_t headValue = head.load(std::memory_order_relaxed);
        const uint32_t tailValue = tail.load(std::memory_order_acquire);
        if (headValue - tailValue >= kCommandCapacity) {
            dropped.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        commands[headValue & (kCommandCapacity - 1)] = {id, value};
        switch (id) {
            case StereoControlId::GainDb: requestedGain.store(value, std::memory_order_relaxed); break;
            case StereoControlId::Pan: requestedPan.store(value, std::memory_order_relaxed); break;
            case StereoControlId::DspBypass: requestedBypass.store(value >= 0.5f, std::memory_order_relaxed); break;
            case StereoControlId::PeqEnabled: requestedPeq.store(value >= 0.5f, std::memory_order_relaxed); break;
            case StereoControlId::CompressorEnabled: requestedCompressor.store(value >= 0.5f, std::memory_order_relaxed); break;
        }
        head.store(headValue + 1, std::memory_order_release);
        return true;
    }

    void consume() noexcept {
        uint32_t read = tail.load(std::memory_order_relaxed);
        const uint32_t write = head.load(std::memory_order_acquire);
        while (read != write) {
            const Command command = commands[read & (kCommandCapacity - 1)];
            switch (command.id) {
                case StereoControlId::GainDb:
                    gain.setGainDb(command.value);
                    appliedGain.store(command.value, std::memory_order_relaxed);
                    break;
                case StereoControlId::Pan:
                    pan.setPan(command.value);
                    appliedPan.store(command.value, std::memory_order_relaxed);
                    break;
                case StereoControlId::DspBypass:
                    bypass.store(command.value >= 0.5f, std::memory_order_relaxed);
                    appliedBypass.store(command.value >= 0.5f, std::memory_order_relaxed);
                    break;
                case StereoControlId::PeqEnabled:
                    eq.setBypassed(command.value < 0.5f);
                    appliedPeq.store(command.value >= 0.5f, std::memory_order_relaxed);
                    break;
                case StereoControlId::CompressorEnabled:
                    compressor.setBypassed(command.value < 0.5f);
                    appliedCompressor.store(command.value >= 0.5f, std::memory_order_relaxed);
                    break;
            }
            ++read;
        }
        tail.store(read, std::memory_order_release);
    }

    double sampleRate;
    uint32_t maxFrames;
    audiocore::GainNode gain;
    audiocore::PanNode pan;
    audiocore::ParametricEQNode eq;
    audiocore::CompressorNode compressor;
    audiocore::AudioBuffer scratchA;
    audiocore::AudioBuffer scratchB;
    std::array<Command, kCommandCapacity> commands{};
    alignas(64) std::atomic<uint32_t> head{0};
    alignas(64) std::atomic<uint32_t> tail{0};
    std::atomic<uint32_t> dropped{0};
    std::atomic<float> requestedGain{0.0f};
    std::atomic<float> requestedPan{0.0f};
    std::atomic<bool> requestedBypass{false};
    std::atomic<bool> requestedPeq{true};
    std::atomic<bool> requestedCompressor{true};
    std::atomic<float> appliedGain{0.0f};
    std::atomic<float> appliedPan{0.0f};
    std::atomic<bool> appliedBypass{false};
    std::atomic<bool> appliedPeq{true};
    std::atomic<bool> appliedCompressor{true};
    std::atomic<bool> bypass{false};
    std::atomic<float> compressorGr{0.0f};
    std::atomic<float> compressorDetector{-90.0f};
    std::atomic<float> compressorOutput{-90.0f};
};

StereoDemoDspNode::StereoDemoDspNode(double sampleRate, uint32_t maxFrames)
    : impl_(std::make_unique<Impl>(sampleRate, maxFrames)) {}
StereoDemoDspNode::~StereoDemoDspNode() = default;

bool StereoDemoDspNode::setGainDb(float value) noexcept {
    return impl_->enqueue(StereoControlId::GainDb, clampGain(value));
}
bool StereoDemoDspNode::setPan(float value) noexcept {
    return impl_->enqueue(StereoControlId::Pan, clampPan(value));
}
bool StereoDemoDspNode::setDspBypass(bool enabled) noexcept {
    return impl_->enqueue(StereoControlId::DspBypass, enabled ? 1.0f : 0.0f);
}
bool StereoDemoDspNode::setPeqEnabled(bool enabled) noexcept {
    return impl_->enqueue(StereoControlId::PeqEnabled, enabled ? 1.0f : 0.0f);
}
bool StereoDemoDspNode::setCompressorEnabled(bool enabled) noexcept {
    return impl_->enqueue(StereoControlId::CompressorEnabled, enabled ? 1.0f : 0.0f);
}
StereoDemoControls StereoDemoDspNode::requestedControls() const noexcept {
    return {impl_->requestedGain.load(std::memory_order_relaxed),
            impl_->requestedPan.load(std::memory_order_relaxed),
            impl_->requestedBypass.load(std::memory_order_relaxed),
            impl_->requestedPeq.load(std::memory_order_relaxed),
            impl_->requestedCompressor.load(std::memory_order_relaxed)};
}
StereoDemoControls StereoDemoDspNode::appliedControls() const noexcept {
    return {impl_->appliedGain.load(std::memory_order_relaxed),
            impl_->appliedPan.load(std::memory_order_relaxed),
            impl_->appliedBypass.load(std::memory_order_relaxed),
            impl_->appliedPeq.load(std::memory_order_relaxed),
            impl_->appliedCompressor.load(std::memory_order_relaxed)};
}
uint32_t StereoDemoDspNode::droppedCommandCount() const noexcept {
    return impl_->dropped.load(std::memory_order_relaxed);
}
float StereoDemoDspNode::compressorGainReductionDb() const noexcept {
    return impl_->compressorGr.load(std::memory_order_relaxed);
}
float StereoDemoDspNode::compressorDetectorDb() const noexcept {
    return impl_->compressorDetector.load(std::memory_order_relaxed);
}
float StereoDemoDspNode::compressorOutputDb() const noexcept {
    return impl_->compressorOutput.load(std::memory_order_relaxed);
}

void StereoDemoDspNode::process(const audiocore::AudioBuffer& input,
                                audiocore::AudioBuffer& output,
                                uint32_t frames) {
    impl_->consume();
    const uint32_t count = std::min({frames, input.getFrameCount(), output.getFrameCount(), impl_->maxFrames});
    if (impl_->bypass.load(std::memory_order_relaxed)) {
        output.copyFrom(input, count);
        impl_->compressorGr.store(0.0f, std::memory_order_relaxed);
        const float inPk = count > 0 ? std::max(std::abs(input.getChannelData(0)[0]), input.getChannelCount() > 1 ? std::abs(input.getChannelData(1)[0]) : 0.0f) : 0.0f;
        const float inDb = inPk > 1e-6f ? 20.0f * std::log10(inPk) : -90.0f;
        impl_->compressorDetector.store(inDb, std::memory_order_relaxed);
        impl_->compressorOutput.store(inDb, std::memory_order_relaxed);
        return;
    }

    impl_->gain.process(input, impl_->scratchA, count);
    impl_->pan.process(impl_->scratchA, impl_->scratchB, count);
    impl_->eq.process(impl_->scratchB, impl_->scratchA, count);
    impl_->compressor.process(impl_->scratchA, output, count);
    impl_->compressorGr.store(impl_->compressor.getCurrentGainReductionDb(), std::memory_order_relaxed);
    impl_->compressorDetector.store(impl_->compressor.getDetectorLevelDb(), std::memory_order_relaxed);
    impl_->compressorOutput.store(impl_->compressor.getOutputLevelDb(), std::memory_order_relaxed);
}

void StereoDemoDspNode::reset() {
    impl_->gain.reset();
    impl_->pan.reset();
    impl_->eq.reset();
    impl_->compressor.reset();
    impl_->compressorGr.store(0.0f, std::memory_order_relaxed);
    impl_->compressorDetector.store(-90.0f, std::memory_order_relaxed);
    impl_->compressorOutput.store(-90.0f, std::memory_order_relaxed);
}

namespace {
float blockPeak(const float* samples, uint32_t frames) noexcept {
    float peak = 0.0f;
    for (uint32_t i = 0; i < frames; ++i) peak = std::max(peak, std::abs(samples[i]));
    return peak;
}

float blockRms(const float* samples, uint32_t frames) noexcept {
    if (!frames) return 0.0f;
    double sum = 0.0;
    for (uint32_t i = 0; i < frames; ++i) sum += static_cast<double>(samples[i]) * samples[i];
    return static_cast<float>(std::sqrt(sum / frames));
}

float stripGainLinear(float db) noexcept {
    return db <= -120.0f ? 0.0f : std::pow(10.0f, db / 20.0f);
}

enum class FxSlotType : uint8_t {
    None = 0,
    Compressor = 1,
    Eq = 2,
    Reverb = 3,
    Delay = 4,
    Limiter = 5,
    Gate = 6,
    Afs = 7,
    Lv2 = 8,
    Nam = 9
};

FxSlotType parseFxSlotType(const std::string& uri) noexcept {
    if (uri == "http://audiocore.org/plugins/compressor") return FxSlotType::Compressor;
    if (uri == "http://audiocore.org/plugins/eq") return FxSlotType::Eq;
    if (uri == "http://audiocore.org/plugins/reverb") return FxSlotType::Reverb;
    if (uri == "http://audiocore.org/plugins/delay") return FxSlotType::Delay;
    if (uri == "http://audiocore.org/plugins/limiter") return FxSlotType::Limiter;
    if (uri == "http://audiocore.org/plugins/gate") return FxSlotType::Gate;
    if (uri == "http://audiocore.org/plugins/afs") return FxSlotType::Afs;
    if (uri.rfind("nam:", 0) == 0) return FxSlotType::Nam;
    if (!uri.empty()) return FxSlotType::Lv2;
    return FxSlotType::None;
}

struct FxSlotControl {
    std::atomic<uint8_t> type{0};
    std::atomic<bool> bypassed{false};
    std::atomic<bool> resetRequested{false};
    std::atomic<float> params[12];
    std::atomic<float> appliedClearSeq{0.0f};
    std::atomic<int> appliedRoomType{-1};
    std::atomic<audiocore::CompressorNode*> compNode{nullptr};
    std::atomic<audiocore::ParametricEQNode*> eqNode{nullptr};
    std::atomic<audiocore::ReverbNode*> reverbNode{nullptr};
    std::atomic<audiocore::DelayNode*> delayNode{nullptr};
    std::atomic<audiocore::LimiterNode*> limiterNode{nullptr};
    std::atomic<audiocore::GateNode*> gateNode{nullptr};
    std::atomic<audiocore::AfsNode*> afsNode{nullptr};
    std::atomic<prismix::lv2::Lv2AudioNode*> lv2Node{nullptr};
    std::atomic<prismix::nam::NamAudioNode*> namNode{nullptr};
    // Per-slot dynamics gain-reduction telemetry (dB, <= 0). Written by the
    // audio thread after each processed block, read by the UI telemetry snapshot.
    mutable std::atomic<float> grDb{0.0f};
    // Expander / Gate slot telemetry: detector level (dBFS) and GateState code
    // (0 Closed, 1 Attack, 2 Open, 3 Hold, 4 Release). Written by the audio
    // thread after each processed block.
    mutable std::atomic<float> gateDetectorDb{-90.0f};
    mutable std::atomic<uint8_t> gateState{0};

    FxSlotControl() {
        for (auto& p : params) p.store(0.0f, std::memory_order_relaxed);
    }
};

void writeFxSlotControl(FxSlotControl& slot, const AudioStripFxConfig& fx, double sampleRate,
                        std::vector<std::unique_ptr<audiocore::AudioNode>>& ownedPool) {
    using namespace audiocore;
    const FxSlotType newType = parseFxSlotType(fx.uri);
    const uint8_t prevType = slot.type.load(std::memory_order_relaxed);

    switch (newType) {
        case FxSlotType::Compressor: {
            if (!slot.compNode.load(std::memory_order_relaxed)) {
                auto node = std::make_unique<CompressorNode>(sampleRate);
                slot.compNode.store(node.get(), std::memory_order_release);
                ownedPool.push_back(std::move(node));
            }
            slot.params[0].store(fxValue(fx, "thresh", -18.0f), std::memory_order_relaxed);
            slot.params[1].store(fxValue(fx, "ratio", 4.0f), std::memory_order_relaxed);
            slot.params[2].store(fxValue(fx, "attack", 10.0f), std::memory_order_relaxed);
            slot.params[3].store(fxValue(fx, "release", 80.0f), std::memory_order_relaxed);
            slot.params[4].store(fxValue(fx, "knee", 6.0f), std::memory_order_relaxed);
            slot.params[5].store(fxValue(fx, "makeup", 0.0f), std::memory_order_relaxed);
            break;
        }
        case FxSlotType::Eq: {
            if (!slot.eqNode.load(std::memory_order_relaxed)) {
                auto node = std::make_unique<ParametricEQNode>(4, sampleRate);
                slot.eqNode.store(node.get(), std::memory_order_release);
                ownedPool.push_back(std::move(node));
            }
            slot.params[0].store(fxValue(fx, "low_freq", 80.0f), std::memory_order_relaxed);
            slot.params[1].store(fxValue(fx, "low_gain", 0.0f), std::memory_order_relaxed);
            slot.params[2].store(fxValue(fx, "mid1_freq", 400.0f), std::memory_order_relaxed);
            slot.params[3].store(fxValue(fx, "mid1_gain", 0.0f), std::memory_order_relaxed);
            slot.params[4].store(fxValue(fx, "mid2_freq", 2500.0f), std::memory_order_relaxed);
            slot.params[5].store(fxValue(fx, "mid2_gain", 0.0f), std::memory_order_relaxed);
            slot.params[6].store(fxValue(fx, "high_freq", 10000.0f), std::memory_order_relaxed);
            slot.params[7].store(fxValue(fx, "high_gain", 0.0f), std::memory_order_relaxed);
            break;
        }
        case FxSlotType::Reverb: {
            if (!slot.reverbNode.load(std::memory_order_relaxed)) {
                auto node = std::make_unique<ReverbNode>(sampleRate);
                slot.reverbNode.store(node.get(), std::memory_order_release);
                ownedPool.push_back(std::move(node));
            }
            slot.params[0].store(fxValue(fx, "decay", 2.2f), std::memory_order_relaxed);
            slot.params[1].store(fxValue(fx, "predelay", 20.0f), std::memory_order_relaxed);
            slot.params[2].store(fxValue(fx, "damping", 0.45f), std::memory_order_relaxed);
            slot.params[3].store(fxValue(fx, "width", 0.85f), std::memory_order_relaxed);
            slot.params[4].store(fxValue(fx, "mix", 0.30f), std::memory_order_relaxed);
            slot.params[5].store(fxValue(fx, "low_cut", 100.0f), std::memory_order_relaxed);
            slot.params[6].store(fxValue(fx, "high_cut", 10000.0f), std::memory_order_relaxed);
            slot.params[7].store(fxValue(fx, "diffusion", 0.75f), std::memory_order_relaxed);
            slot.params[8].store(fxValue(fx, "room_type", 1.0f), std::memory_order_relaxed);
            break;
        }
        case FxSlotType::Delay: {
            if (!slot.delayNode.load(std::memory_order_relaxed)) {
                auto node = std::make_unique<DelayNode>(sampleRate);
                slot.delayNode.store(node.get(), std::memory_order_release);
                ownedPool.push_back(std::move(node));
            }
            slot.params[0].store(fxValue(fx, "feedback", 0.35f), std::memory_order_relaxed);
            slot.params[1].store(fxValue(fx, "mix", 0.25f), std::memory_order_relaxed);
            slot.params[2].store(fxValue(fx, "tone", 6500.0f), std::memory_order_relaxed);
            slot.params[3].store(fxValue(fx, "time_l", 320.0f), std::memory_order_relaxed);
            slot.params[4].store(fxValue(fx, "time_r", 480.0f), std::memory_order_relaxed);
            slot.params[5].store(fxValue(fx, "low_cut", 80.0f), std::memory_order_relaxed);
            slot.params[6].store(fxValue(fx, "width", 0.85f), std::memory_order_relaxed);
            slot.params[7].store(fxValue(fx, "sync_en", 0.0f), std::memory_order_relaxed);
            slot.params[8].store(fxValue(fx, "bpm", 120.0f), std::memory_order_relaxed);
            slot.params[9].store(fxValue(fx, "sync_div", 2.0f), std::memory_order_relaxed);
            slot.params[10].store(fxValue(fx, "link_lr", 0.0f), std::memory_order_relaxed);
            break;
        }
        case FxSlotType::Limiter: {
            if (!slot.limiterNode.load(std::memory_order_relaxed)) {
                auto node = std::make_unique<LimiterNode>(-0.3f, sampleRate);
                slot.limiterNode.store(node.get(), std::memory_order_release);
                ownedPool.push_back(std::move(node));
            }
            slot.params[0].store(fxValue(fx, "ceiling", fxValue(fx, "threshold", -0.3f)), std::memory_order_relaxed);
            slot.params[1].store(fxValue(fx, "release", 50.0f), std::memory_order_relaxed);
            break;
        }
        case FxSlotType::Gate: {
            if (!slot.gateNode.load(std::memory_order_relaxed)) {
                auto node = std::make_unique<GateNode>(sampleRate);
                slot.gateNode.store(node.get(), std::memory_order_release);
                ownedPool.push_back(std::move(node));
            }
            slot.params[0].store(fxValue(fx, "thresh", -48.0f), std::memory_order_relaxed);
            slot.params[1].store(fxValue(fx, "range", -60.0f), std::memory_order_relaxed);
            slot.params[2].store(fxValue(fx, "attack", 5.0f), std::memory_order_relaxed);
            slot.params[3].store(fxValue(fx, "hold", 50.0f), std::memory_order_relaxed);
            slot.params[4].store(fxValue(fx, "release", 120.0f), std::memory_order_relaxed);
            break;
        }
        case FxSlotType::Afs: {
            if (!slot.afsNode.load(std::memory_order_relaxed)) {
                auto node = std::make_unique<AfsNode>(sampleRate);
                slot.afsNode.store(node.get(), std::memory_order_release);
                ownedPool.push_back(std::move(node));
            }
            slot.params[0].store(fxValue(fx, "sensitivity", 0.75f), std::memory_order_relaxed);
            slot.params[1].store(fxValue(fx, "max_notches", 8.0f), std::memory_order_relaxed);
            slot.params[2].store(fxValue(fx, "max_depth", -18.0f), std::memory_order_relaxed);
            slot.params[3].store(fxValue(fx, "q", 15.0f), std::memory_order_relaxed);
            slot.params[4].store(fxValue(fx, "attack", 20.0f), std::memory_order_relaxed);
            slot.params[5].store(fxValue(fx, "hold", 3000.0f), std::memory_order_relaxed);
            slot.params[6].store(fxValue(fx, "release", 1000.0f), std::memory_order_relaxed);
            slot.params[7].store(fxValue(fx, "clear_seq", 0.0f), std::memory_order_relaxed);
            break;
        }
        case FxSlotType::Lv2: {
            auto* existing = slot.lv2Node.load(std::memory_order_relaxed);
            if (!existing || existing->getUri() != fx.uri) {
                auto instance = prismix::lv2::Lv2Host::instance().instantiatePlugin(fx.uri, sampleRate);
                if (instance) {
                    auto node = std::make_unique<prismix::lv2::Lv2AudioNode>(std::move(instance), sampleRate);
                    slot.lv2Node.store(node.get(), std::memory_order_release);
                    ownedPool.push_back(std::move(node));
                }
            }
            if (auto* node = slot.lv2Node.load(std::memory_order_relaxed)) {
                for (const auto& p : fx.parameters) {
                    node->setControlValue(p.id, p.value);
                }
            }
            break;
        }
        case FxSlotType::Nam: {
            auto* node = slot.namNode.load(std::memory_order_relaxed);
            if (!node) {
                auto newNode = std::make_unique<prismix::nam::NamAudioNode>(sampleRate);
                node = newNode.get();
                slot.namNode.store(node, std::memory_order_release);
                ownedPool.push_back(std::move(newNode));
            }
            std::string modelPath = (fx.uri.size() > 4) ? fx.uri.substr(4) : "";
            auto curModel = node->getModel();
            if (!curModel || curModel->metadata().file_path != modelPath) {
                auto model = prismix::nam::NamModel::loadFromFile(modelPath);
                if (model) {
                    node->setModel(model);
                }
            }
            node->setInputGainDb(fxValue(fx, "in_gain", 0.0f));
            node->setOutputLevelDb(fxValue(fx, "out_level", 0.0f));
            break;
        }
        case FxSlotType::None:
        default:
            break;
    }

    if (prevType != static_cast<uint8_t>(newType)) {
        slot.resetRequested.store(true, std::memory_order_relaxed);
    }
    slot.bypassed.store(fx.bypassed, std::memory_order_relaxed);
    slot.type.store(static_cast<uint8_t>(newType), std::memory_order_release);
}

void applyFxSlotControl(FxSlotControl& slot, bool sendReturnWetOnly) noexcept {
    using namespace audiocore;
    const auto type = static_cast<FxSlotType>(slot.type.load(std::memory_order_acquire));
    const bool bypassed = slot.bypassed.load(std::memory_order_relaxed);
    const bool doReset = slot.resetRequested.exchange(false, std::memory_order_relaxed);

    switch (type) {
        case FxSlotType::Compressor: {
            if (auto* node = slot.compNode.load(std::memory_order_acquire)) {
                if (doReset) node->reset();
                node->setThresholdDb(slot.params[0].load(std::memory_order_relaxed));
                node->setRatio(slot.params[1].load(std::memory_order_relaxed));
                node->setAttackMs(slot.params[2].load(std::memory_order_relaxed));
                node->setReleaseMs(slot.params[3].load(std::memory_order_relaxed));
                node->setKneeDb(slot.params[4].load(std::memory_order_relaxed));
                node->setMakeupGainDb(slot.params[5].load(std::memory_order_relaxed));
                node->setBypassed(bypassed);
            }
            break;
        }
        case FxSlotType::Eq: {
            if (auto* node = slot.eqNode.load(std::memory_order_acquire)) {
                if (doReset) node->reset();
                node->setBand(0, FilterType::LowShelf, slot.params[0].load(std::memory_order_relaxed), 0.707,
                              slot.params[1].load(std::memory_order_relaxed));
                node->setBand(1, FilterType::Peaking, slot.params[2].load(std::memory_order_relaxed), 1.0,
                              slot.params[3].load(std::memory_order_relaxed));
                node->setBand(2, FilterType::Peaking, slot.params[4].load(std::memory_order_relaxed), 1.0,
                              slot.params[5].load(std::memory_order_relaxed));
                node->setBand(3, FilterType::HighShelf, slot.params[6].load(std::memory_order_relaxed), 0.707,
                              slot.params[7].load(std::memory_order_relaxed));
                node->setBypassed(bypassed);
            }
            break;
        }
        case FxSlotType::Reverb: {
            if (auto* node = slot.reverbNode.load(std::memory_order_acquire)) {
                if (doReset) node->reset();
                const int roomIdx = std::clamp(static_cast<int>(std::round(slot.params[8].load(std::memory_order_relaxed))), 0, 3);
                if (slot.appliedRoomType.exchange(roomIdx, std::memory_order_relaxed) != roomIdx) {
                    node->setType(static_cast<ReverbType>(roomIdx));
                }
                node->setDecay(slot.params[0].load(std::memory_order_relaxed));
                node->setPreDelayMs(slot.params[1].load(std::memory_order_relaxed), true);
                node->setDamping(slot.params[2].load(std::memory_order_relaxed));
                node->setWidth(slot.params[3].load(std::memory_order_relaxed));
                node->setMix(slot.params[4].load(std::memory_order_relaxed));
                node->setLowCutHz(slot.params[5].load(std::memory_order_relaxed));
                node->setHighCutHz(slot.params[6].load(std::memory_order_relaxed));
                node->setDiffusion(slot.params[7].load(std::memory_order_relaxed));
                node->setRoutingMode(sendReturnWetOnly ? ReverbRoutingMode::SendReturn : ReverbRoutingMode::Insert);
                node->setBypassed(bypassed);
            }
            break;
        }
        case FxSlotType::Delay: {
            if (auto* node = slot.delayNode.load(std::memory_order_acquire)) {
                if (doReset) node->reset();
                node->setFeedback(slot.params[0].load(std::memory_order_relaxed));
                node->setMix(sendReturnWetOnly ? 1.0f : slot.params[1].load(std::memory_order_relaxed));
                node->setHighCutHz(slot.params[2].load(std::memory_order_relaxed));
                node->setLowCutHz(slot.params[5].load(std::memory_order_relaxed));
                node->setStereoWidth(slot.params[6].load(std::memory_order_relaxed));
                const bool syncEn = slot.params[7].load(std::memory_order_relaxed) >= 0.5f;
                const float bpm = slot.params[8].load(std::memory_order_relaxed);
                const int divIdx = std::clamp(static_cast<int>(std::round(slot.params[9].load(std::memory_order_relaxed))), 0, 6);
                const auto div = static_cast<TempoDivision>(divIdx);
                const bool linkLr = slot.params[10].load(std::memory_order_relaxed) >= 0.5f;
                node->setTempoSyncEnabled(syncEn);
                node->setBpm(bpm);
                node->setTempoDivision(div);
                const float timeL = syncEn ? DelayNode::calculateTempoDelayMs(bpm, div)
                                           : slot.params[3].load(std::memory_order_relaxed);
                const float timeR = linkLr ? timeL : slot.params[4].load(std::memory_order_relaxed);
                node->setTapConfig(0, {timeL, 0.0f, -1.0f, true});
                node->setTapConfig(1, {timeR, 0.0f, 1.0f, true});
                node->setTapEnabled(2, false);
                node->setTapEnabled(3, false);
                node->setBypassed(bypassed);
            }
            break;
        }
        case FxSlotType::Limiter: {
            if (auto* node = slot.limiterNode.load(std::memory_order_acquire)) {
                if (doReset) node->reset();
                node->setCeilingDb(slot.params[0].load(std::memory_order_relaxed));
                node->setReleaseMs(slot.params[1].load(std::memory_order_relaxed));
                node->setBypassed(bypassed);
            }
            break;
        }
        case FxSlotType::Gate: {
            if (auto* node = slot.gateNode.load(std::memory_order_acquire)) {
                if (doReset) node->reset();
                node->setThresholdDb(slot.params[0].load(std::memory_order_relaxed));
                node->setRangeDb(slot.params[1].load(std::memory_order_relaxed));
                node->setAttackMs(slot.params[2].load(std::memory_order_relaxed));
                node->setHoldMs(slot.params[3].load(std::memory_order_relaxed));
                node->setReleaseMs(slot.params[4].load(std::memory_order_relaxed));
                node->setBypassed(bypassed);
            }
            break;
        }
        case FxSlotType::Afs: {
            if (auto* node = slot.afsNode.load(std::memory_order_acquire)) {
                if (doReset) node->reset();
                node->setSensitivity(slot.params[0].load(std::memory_order_relaxed));
                node->setMaxNotches(static_cast<size_t>(
                    std::clamp(std::round(slot.params[1].load(std::memory_order_relaxed)), 1.0f, 16.0f)));
                node->setMaxDepthDb(slot.params[2].load(std::memory_order_relaxed));
                node->setQ(slot.params[3].load(std::memory_order_relaxed));
                node->setAttackMs(slot.params[4].load(std::memory_order_relaxed));
                node->setHoldMs(slot.params[5].load(std::memory_order_relaxed));
                node->setReleaseMs(slot.params[6].load(std::memory_order_relaxed));
                const float reqClear = slot.params[7].load(std::memory_order_relaxed);
                if (std::abs(slot.appliedClearSeq.exchange(reqClear, std::memory_order_relaxed) - reqClear) > 0.1f &&
                    reqClear > 0.0f) {
                    node->clearFilters();
                }
                node->setBypassed(bypassed);
            }
            break;
        }
        case FxSlotType::Lv2: {
            if (auto* node = slot.lv2Node.load(std::memory_order_acquire)) {
                if (doReset) node->reset();
                node->setBypassed(bypassed);
            }
            break;
        }
        case FxSlotType::Nam: {
            if (auto* node = slot.namNode.load(std::memory_order_acquire)) {
                if (doReset) node->reset();
                node->setBypassed(bypassed);
            }
            break;
        }
        case FxSlotType::None:
        default:
            break;
    }
}

void processFxSlot(const FxSlotControl& slot, bool sendReturnWetOnly,
                   audiocore::AudioBuffer*& current, audiocore::AudioBuffer*& next,
                   audiocore::AudioBuffer& scratchA, audiocore::AudioBuffer& scratchB,
                   uint32_t count) noexcept {
    const auto type = static_cast<FxSlotType>(slot.type.load(std::memory_order_acquire));
    if (type == FxSlotType::None) return;

    const bool bypassed = slot.bypassed.load(std::memory_order_relaxed);
    if (sendReturnWetOnly && (type == FxSlotType::Reverb || type == FxSlotType::Delay)) {
        if (bypassed) {
            next->clear(count);
            current = next;
            next = (current == &scratchA) ? &scratchB : &scratchA;
            return;
        }
        audiocore::AudioNode* node = (type == FxSlotType::Reverb)
            ? static_cast<audiocore::AudioNode*>(slot.reverbNode.load(std::memory_order_acquire))
            : static_cast<audiocore::AudioNode*>(slot.delayNode.load(std::memory_order_acquire));
        if (!node) return;
        node->process(*current, *next, count);
        const float wetMix = std::clamp(
            slot.params[type == FxSlotType::Reverb ? 4 : 1].load(std::memory_order_relaxed), 0.0f, 1.0f);
        for (uint32_t ch = 0; ch < next->getChannelCount(); ++ch) {
            float* data = next->getChannelData(ch);
            for (uint32_t f = 0; f < count; ++f) data[f] *= wetMix;
        }
        current = next;
        next = (current == &scratchA) ? &scratchB : &scratchA;
        return;
    }

    audiocore::AudioNode* node = nullptr;
    switch (type) {
        case FxSlotType::Compressor: node = slot.compNode.load(std::memory_order_acquire); break;
        case FxSlotType::Eq:         node = slot.eqNode.load(std::memory_order_acquire); break;
        case FxSlotType::Reverb:     node = slot.reverbNode.load(std::memory_order_acquire); break;
        case FxSlotType::Delay:      node = slot.delayNode.load(std::memory_order_acquire); break;
        case FxSlotType::Limiter:    node = slot.limiterNode.load(std::memory_order_acquire); break;
        case FxSlotType::Gate:       node = slot.gateNode.load(std::memory_order_acquire); break;
        case FxSlotType::Afs:        node = slot.afsNode.load(std::memory_order_acquire); break;
        case FxSlotType::Lv2:        node = slot.lv2Node.load(std::memory_order_acquire); break;
        case FxSlotType::Nam:        node = slot.namNode.load(std::memory_order_acquire); break;
        case FxSlotType::None:
        default: break;
    }
    if (!node) return;
    node->process(*current, *next, count);
    if (type == FxSlotType::Compressor) {
        const auto* comp = static_cast<const audiocore::CompressorNode*>(node);
        slot.grDb.store(bypassed ? 0.0f : std::min(0.0f, comp->getCurrentGainReductionDb()),
                        std::memory_order_relaxed);
    } else if (type == FxSlotType::Gate) {
        const auto* gate = static_cast<const audiocore::GateNode*>(node);
        slot.grDb.store(bypassed ? 0.0f : std::min(0.0f, gate->getGainReductionDb()),
                        std::memory_order_relaxed);
        slot.gateDetectorDb.store(gate->getDetectorLevelDb(), std::memory_order_relaxed);
        slot.gateState.store(static_cast<uint8_t>(gate->getCurrentState()), std::memory_order_relaxed);
    } else if (type == FxSlotType::Limiter) {
        const auto* limiter = static_cast<const audiocore::LimiterNode*>(node);
        slot.grDb.store(bypassed ? 0.0f : std::min(0.0f, limiter->getCurrentGainReductionDb()),
                        std::memory_order_relaxed);
    }
    current = next;
    next = (current == &scratchA) ? &scratchB : &scratchA;
}

void resetFxSlot(FxSlotControl& slot) noexcept {
    if (auto* n = slot.compNode.load(std::memory_order_relaxed)) n->reset();
    if (auto* n = slot.eqNode.load(std::memory_order_relaxed)) n->reset();
    if (auto* n = slot.reverbNode.load(std::memory_order_relaxed)) n->reset();
    if (auto* n = slot.delayNode.load(std::memory_order_relaxed)) n->reset();
    if (auto* n = slot.limiterNode.load(std::memory_order_relaxed)) n->reset();
    if (auto* n = slot.gateNode.load(std::memory_order_relaxed)) n->reset();
    if (auto* n = slot.afsNode.load(std::memory_order_relaxed)) n->reset();
    if (auto* n = slot.lv2Node.load(std::memory_order_relaxed)) n->reset();
    if (auto* n = slot.namNode.load(std::memory_order_relaxed)) n->reset();
    slot.grDb.store(0.0f, std::memory_order_relaxed);
    slot.gateDetectorDb.store(-90.0f, std::memory_order_relaxed);
    slot.gateState.store(0, std::memory_order_relaxed);
}

void populateFxSlotTelemetry(const std::array<FxSlotControl, 8>& fxSlots, StripAudioTelemetry& item) noexcept {
    for (size_t s = 0; s < 8; ++s) {
        const auto type = static_cast<FxSlotType>(fxSlots[s].type.load(std::memory_order_acquire));
        if (type == FxSlotType::Compressor) {
            item.fxSlotGrDb[s] = fxSlots[s].grDb.load(std::memory_order_relaxed);
        } else if (type == FxSlotType::Gate) {
            const float gr = std::min(0.0f, fxSlots[s].grDb.load(std::memory_order_relaxed));
            item.fxSlotGrDb[s] = gr;
            item.fxSlotGateGrDb[s] = gr;
            item.fxSlotGateDetectorDb[s] = fxSlots[s].gateDetectorDb.load(std::memory_order_relaxed);
            const uint8_t state = fxSlots[s].gateState.load(std::memory_order_relaxed);
            item.fxSlotGateState[s] = (state <= 4) ? state : 4;
        } else if (type == FxSlotType::Limiter) {
            item.fxSlotLimiterGrDb[s] = std::min(0.0f, fxSlots[s].grDb.load(std::memory_order_relaxed));
        }
    }
}

void populateAfsTelemetryFromSlots(const std::array<FxSlotControl, 8>& fxSlots, StripAudioTelemetry& item) {
    for (size_t s = 0; s < 8; ++s) {
        const auto type = static_cast<FxSlotType>(fxSlots[s].type.load(std::memory_order_acquire));
        if (type != FxSlotType::Afs) continue;
        if (auto* afs = fxSlots[s].afsNode.load(std::memory_order_acquire)) {
            while (afs->hasPendingAnalysisWindow()) {
                afs->stepAnalysis();
            }
            item.afsAssigned = true;
            item.afsMaxNotches = static_cast<uint8_t>(std::clamp<size_t>(afs->getMaxNotches(), 1, 16));
            const std::string status = afs->getStatusString();
            if (status == "DISABLED") item.afsState = 0;
            else if (status == "BYPASS") item.afsState = 1;
            else if (status == "MONITORING") item.afsState = 2;
            else if (status == "CANDIDATE") item.afsState = 3;
            else if (status == "ACTIVE") item.afsState = 4;
            else item.afsState = 2;

            const auto snaps = afs->getNotchSnapshots();
            item.afsActiveNotchCount = static_cast<uint8_t>(snaps.size());
            for (size_t k = 0; k < 8 && k < snaps.size(); ++k) {
                item.afsNotches[k].id = static_cast<uint8_t>(snaps[k].id);
                item.afsNotches[k].frequencyHz = snaps[k].frequencyHz;
                item.afsNotches[k].depthDb = snaps[k].depthDb;
                item.afsNotches[k].q = snaps[k].q;
                item.afsNotches[k].state = static_cast<uint8_t>(snaps[k].state);
            }
            return;
        }
    }
}

class ConsoleStripFxNode final : public audiocore::AudioNode {
public:
    struct StripDspControl {
        uint32_t logicalId = 0;
        std::atomic<bool> dirty{false};
        std::atomic<uint8_t> format{0};
        std::atomic<bool> polarityInverted{false};
        std::atomic<bool> hpfEnabled{true};
        std::atomic<float> hpfFrequency{80.0f};

        std::atomic<bool> peqBypassed{false};
        std::atomic<bool> geqMode{false};
        std::atomic<bool> geqBypassed{false};
        std::atomic<float> eqFrequency[4];
        std::atomic<float> eqGain[4];
        std::atomic<float> eqQ[4];
        std::atomic<uint8_t> eqType[4];
        std::atomic<bool> eqBandEnabled[4];
        std::atomic<float> geqGain[31];

        std::atomic<bool> compressorEnabled{true};
        std::atomic<float> compressorThresholdDb{-18.0f};
        std::atomic<float> compressorRatio{4.0f};
        std::atomic<float> compressorAttackMs{10.0f};
        std::atomic<float> compressorReleaseMs{80.0f};
        std::atomic<float> compressorKneeDb{6.0f};
        std::atomic<float> compressorMakeupDb{0.0f};

        std::atomic<bool> gateEnabled{true};
        std::atomic<float> gateThresholdDb{-48.0f};
        std::atomic<float> gateRangeDb{-60.0f};
        std::atomic<float> gateAttackMs{5.0f};
        std::atomic<float> gateHoldMs{50.0f};
        std::atomic<float> gateReleaseMs{120.0f};

        std::atomic<bool> limiterEnabled{false};
        std::atomic<float> limiterCeilingDb{-0.3f};
        std::atomic<float> limiterReleaseMs{50.0f};

        std::array<FxSlotControl, 8> fxSlots{};

        StripDspControl() {
            for (int i = 0; i < 4; ++i) {
                eqFrequency[i].store(1000.0f);
                eqGain[i].store(0.0f);
                eqQ[i].store(1.0f);
                eqType[i].store(1);
                eqBandEnabled[i].store(true);
            }
            for (int i = 0; i < 31; ++i) {
                geqGain[i].store(0.0f);
            }
        }
    };

    ConsoleStripFxNode(const std::vector<AudioStripConfig>& strips, double sampleRate, uint32_t maxFrames)
        : sampleRate_(sampleRate > 0.0 ? sampleRate : 48000.0)
    {
        runtimes_.reserve(strips.size());
        meters_.reserve(strips.size());
        controls_.reserve(strips.size());
        for (const auto& strip : strips) {
            auto runtime = std::make_unique<Runtime>();
            runtime->logicalId = strip.logicalId;
            runtime->channelCount = (strip.format == AudioStripFormat::Stereo) ? 2u : 1u;
            runtime->inputMono.resize(1, maxFrames);
            runtime->scratchAMono.resize(1, maxFrames);
            runtime->scratchBMono.resize(1, maxFrames);
            runtime->inputStereo.resize(2, maxFrames);
            runtime->scratchAStereo.resize(2, maxFrames);
            runtime->scratchBStereo.resize(2, maxFrames);

            runtime->hpf = std::make_unique<audiocore::ParametricEQNode>(1, sampleRate_);
            runtime->hpf->setBand(0, audiocore::FilterType::HighPass, strip.hpfFrequency, 0.707, 0.0);
            runtime->hpf->setBypassed(!strip.hpfEnabled);
            runtime->peq = std::make_unique<audiocore::ParametricEQNode>(4, sampleRate_);
            for (uint32_t band = 0; band < 4; ++band) {
                const auto type = static_cast<audiocore::FilterType>(strip.eqType[band]);
                runtime->peq->setBand(band, type, strip.eqFrequency[band], strip.eqQ[band], strip.eqGain[band]);
                runtime->peq->setBandEnabled(band, strip.eqBandEnabled[band]);
            }
            runtime->peq->setBypassed(strip.geqMode || strip.peqBypassed);
            runtime->geq = std::make_unique<audiocore::GraphicEQNode>(sampleRate_);
            for (size_t band = 0; band < strip.geqGain.size(); ++band)
                runtime->geq->setBandGainDb(band, strip.geqGain[band]);
            runtime->geq->setBypassed(!strip.geqMode || strip.geqBypassed);
            runtime->compressor = std::make_unique<audiocore::CompressorNode>(sampleRate_);
            runtime->compressor->setThresholdDb(strip.compressorThresholdDb);
            runtime->compressor->setRatio(strip.compressorRatio);
            runtime->compressor->setAttackMs(strip.compressorAttackMs);
            runtime->compressor->setReleaseMs(strip.compressorReleaseMs);
            runtime->compressor->setKneeDb(strip.compressorKneeDb);
            runtime->compressor->setMakeupGainDb(strip.compressorMakeupDb);
            runtime->compressor->setBypassed(!strip.compressorEnabled);
            runtime->gate = std::make_unique<audiocore::GateNode>(sampleRate_);
            runtime->gate->setThresholdDb(strip.gateThresholdDb);
            runtime->gate->setRangeDb(strip.gateRangeDb);
            runtime->gate->setAttackMs(strip.gateAttackMs);
            runtime->gate->setHoldMs(strip.gateHoldMs);
            runtime->gate->setReleaseMs(strip.gateReleaseMs);
            runtime->gate->setBypassed(!strip.gateEnabled);
            runtime->limiter = std::make_unique<audiocore::LimiterNode>(strip.limiterCeilingDb, sampleRate_);
            runtime->limiter->setReleaseMs(strip.limiterReleaseMs);
            runtime->limiter->setBypassed(!strip.limiterEnabled);
            runtime->polarityInverted = strip.polarityInverted;

            auto ctrl = std::make_unique<StripDspControl>();
            ctrl->logicalId = strip.logicalId;
            populateControl(*ctrl, strip);
            for (size_t s = 0; s < 8; ++s) {
                applyFxSlotControl(ctrl->fxSlots[s], false);
            }
            ctrl->dirty.store(false, std::memory_order_relaxed);

            runtimes_.push_back(std::move(runtime));
            meters_.push_back(std::make_unique<MeterState>());
            controls_.push_back(std::move(ctrl));
        }
    }

    void updateStripDsp(const AudioStripConfig& config) noexcept {
        std::lock_guard<std::mutex> lock(controlWriteMutex_);
        for (auto& ctrl : controls_) {
            if (ctrl->logicalId == config.logicalId) {
                populateControl(*ctrl, config);
                ctrl->dirty.store(true, std::memory_order_release);
                break;
            }
        }
    }

    void setAnalyzerTap(uint32_t channelId, uint8_t tapSource, AnalyzerRingBuffer* ring) noexcept {
        tapTargetId_.store(channelId, std::memory_order_relaxed);
        tapSource_.store(tapSource, std::memory_order_relaxed);
        tapRingBuffer_.store(ring, std::memory_order_release);
    }

    uint32_t stripBufferChannels(size_t index) const noexcept {
        if (index >= runtimes_.size()) return 0;
        return runtimes_[index]->channelCount.load(std::memory_order_relaxed);
    }

    void process(const audiocore::AudioBuffer& input, audiocore::AudioBuffer& output,
                 uint32_t frames) override {
        const uint32_t count = std::min({frames, input.getFrameCount(), output.getFrameCount()});
        output.clear(count);
        for (size_t index = 0; index < runtimes_.size(); ++index) {
            auto& runtime = *runtimes_[index];
            auto& ctrl = *controls_[index];

            if (ctrl.dirty.exchange(false, std::memory_order_acquire)) {
                runtime.channelCount.store(
                    ctrl.format.load(std::memory_order_relaxed) == static_cast<uint8_t>(AudioStripFormat::Stereo) ? 2u : 1u,
                    std::memory_order_relaxed);
                runtime.polarityInverted = ctrl.polarityInverted.load(std::memory_order_relaxed);
                const bool hpfEn = ctrl.hpfEnabled.load(std::memory_order_relaxed);
                const float hpfFreq = ctrl.hpfFrequency.load(std::memory_order_relaxed);
                runtime.hpf->setBand(0, audiocore::FilterType::HighPass, hpfFreq, 0.707, 0.0);
                runtime.hpf->setBypassed(!hpfEn);

                const bool peqBypassed = ctrl.peqBypassed.load(std::memory_order_relaxed);
                const bool geqMode = ctrl.geqMode.load(std::memory_order_relaxed);
                runtime.peq->setBypassed(geqMode || peqBypassed);
                for (uint32_t b = 0; b < 4; ++b) {
                    auto type = static_cast<audiocore::FilterType>(ctrl.eqType[b].load(std::memory_order_relaxed));
                    float freq = ctrl.eqFrequency[b].load(std::memory_order_relaxed);
                    float q = ctrl.eqQ[b].load(std::memory_order_relaxed);
                    float gain = ctrl.eqGain[b].load(std::memory_order_relaxed);
                    bool en = ctrl.eqBandEnabled[b].load(std::memory_order_relaxed);
                    runtime.peq->setBand(b, type, freq, q, gain);
                    runtime.peq->setBandEnabled(b, en);
                }

                const bool geqBypassed = ctrl.geqBypassed.load(std::memory_order_relaxed);
                runtime.geq->setBypassed(!geqMode || geqBypassed);
                for (size_t b = 0; b < 31; ++b) {
                    runtime.geq->setBandGainDb(b, ctrl.geqGain[b].load(std::memory_order_relaxed));
                }

                const bool compEn = ctrl.compressorEnabled.load(std::memory_order_relaxed);
                runtime.compressor->setBypassed(!compEn);
                runtime.compressor->setThresholdDb(ctrl.compressorThresholdDb.load(std::memory_order_relaxed));
                runtime.compressor->setRatio(ctrl.compressorRatio.load(std::memory_order_relaxed));
                runtime.compressor->setAttackMs(ctrl.compressorAttackMs.load(std::memory_order_relaxed));
                runtime.compressor->setReleaseMs(ctrl.compressorReleaseMs.load(std::memory_order_relaxed));
                runtime.compressor->setKneeDb(ctrl.compressorKneeDb.load(std::memory_order_relaxed));
                runtime.compressor->setMakeupGainDb(ctrl.compressorMakeupDb.load(std::memory_order_relaxed));

                const bool gateEn = ctrl.gateEnabled.load(std::memory_order_relaxed);
                runtime.gate->setBypassed(!gateEn);
                runtime.gate->setThresholdDb(ctrl.gateThresholdDb.load(std::memory_order_relaxed));
                runtime.gate->setRangeDb(ctrl.gateRangeDb.load(std::memory_order_relaxed));
                runtime.gate->setAttackMs(ctrl.gateAttackMs.load(std::memory_order_relaxed));
                runtime.gate->setHoldMs(ctrl.gateHoldMs.load(std::memory_order_relaxed));
                runtime.gate->setReleaseMs(ctrl.gateReleaseMs.load(std::memory_order_relaxed));

                const bool limEn = ctrl.limiterEnabled.load(std::memory_order_relaxed);
                runtime.limiter->setBypassed(!limEn);
                runtime.limiter->setCeilingDb(ctrl.limiterCeilingDb.load(std::memory_order_relaxed));
                runtime.limiter->setReleaseMs(ctrl.limiterReleaseMs.load(std::memory_order_relaxed));

                for (size_t s = 0; s < 8; ++s) {
                    applyFxSlotControl(ctrl.fxSlots[s], false);
                }
            }

            const uint32_t left = static_cast<uint32_t>(index * 2);
            const uint32_t right = left + 1;
            if (right >= input.getChannelCount() || right >= output.getChannelCount()) continue;
            const float* inL = input.getChannelData(left);
            const float* inR = input.getChannelData(right);
            auto& meter = *meters_[index];

            const uint32_t chCount = runtime.channelCount.load(std::memory_order_relaxed);
            audiocore::AudioBuffer& inBuf = (chCount == 2) ? runtime.inputStereo : runtime.inputMono;
            audiocore::AudioBuffer& scA = (chCount == 2) ? runtime.scratchAStereo : runtime.scratchAMono;
            audiocore::AudioBuffer& scB = (chCount == 2) ? runtime.scratchBStereo : runtime.scratchBMono;

            meter.peak[0].store(blockPeak(inL, count), std::memory_order_relaxed);
            meter.peak[1].store(blockPeak(chCount == 2 ? inR : inL, count), std::memory_order_relaxed);
            meter.rms[0].store(blockRms(inL, count), std::memory_order_relaxed);
            meter.rms[1].store(blockRms(chCount == 2 ? inR : inL, count), std::memory_order_relaxed);

            if (chCount == 2) {
                for (uint32_t frame = 0; frame < count; ++frame) {
                    inBuf[0][frame] = runtime.polarityInverted ? -inL[frame] : inL[frame];
                    inBuf[1][frame] = runtime.polarityInverted ? -inR[frame] : inR[frame];
                }
            } else {
                for (uint32_t frame = 0; frame < count; ++frame) {
                    inBuf[0][frame] = runtime.polarityInverted ? -inL[frame] : inL[frame];
                }
            }

            audiocore::AudioBuffer* current = &inBuf;
            audiocore::AudioBuffer* next = &scA;
            const auto run = [&](audiocore::AudioNode* node) {
                if (!node) return;
                node->process(*current, *next, count);
                current = next;
                next = (current == &scA) ? &scB : &scA;
            };
            const uint32_t tapTarget = tapTargetId_.load(std::memory_order_relaxed);
            const uint8_t tapSrc = tapSource_.load(std::memory_order_relaxed);
            auto* ring = tapRingBuffer_.load(std::memory_order_relaxed);
            const bool isTapTarget = (ring != nullptr && runtime.logicalId == tapTarget);

            run(runtime.hpf.get());
            run(runtime.gate.get());
            if (isTapTarget && tapSrc == 0) { // PRE_EQ
                ring->push((*current)[0], count);
            }
            run(runtime.peq.get());
            run(runtime.geq.get());
            if (isTapTarget && tapSrc == 1) { // POST_EQ
                ring->push((*current)[0], count);
            }
            run(runtime.compressor.get());

            const bool isStereoBuf = current->getChannelCount() > 1;
            meter.fxInputPeak[0].store(blockPeak((*current)[0], count), std::memory_order_relaxed);
            meter.fxInputPeak[1].store(isStereoBuf ? blockPeak((*current)[1], count) : 0.0f, std::memory_order_relaxed);
            meter.fxInputRms[0].store(blockRms((*current)[0], count), std::memory_order_relaxed);
            meter.fxInputRms[1].store(isStereoBuf ? blockRms((*current)[1], count) : 0.0f, std::memory_order_relaxed);

            for (size_t s = 0; s < 8; ++s) {
                processFxSlot(ctrl.fxSlots[s], false, current, next, scA, scB, count);
            }

            const bool isStereoPostFx = current->getChannelCount() > 1;
            meter.fxOutputPeak[0].store(blockPeak((*current)[0], count), std::memory_order_relaxed);
            meter.fxOutputPeak[1].store(isStereoPostFx ? blockPeak((*current)[1], count) : 0.0f, std::memory_order_relaxed);
            meter.fxOutputRms[0].store(blockRms((*current)[0], count), std::memory_order_relaxed);
            meter.fxOutputRms[1].store(isStereoPostFx ? blockRms((*current)[1], count) : 0.0f, std::memory_order_relaxed);

            meter.preLimiterPeak[0].store(blockPeak((*current)[0], count), std::memory_order_relaxed);
            meter.preLimiterPeak[1].store(
                blockPeak(current->getChannelCount() > 1 ? (*current)[1] : (*current)[0], count),
                std::memory_order_relaxed);
            run(runtime.limiter.get());

            float* outL = output.getChannelData(left);
            float* outR = output.getChannelData(right);
            std::memcpy(outL, (*current)[0], count * sizeof(float));
            if (current->getChannelCount() > 1) {
                std::memcpy(outR, (*current)[1], count * sizeof(float));
            } else {
                std::memcpy(outR, (*current)[0], count * sizeof(float));
            }

            meter.outputPeak[0].store(blockPeak(outL, count), std::memory_order_relaxed);
            meter.outputPeak[1].store(blockPeak(outR, count), std::memory_order_relaxed);
            meter.outputRms[0].store(blockRms(outL, count), std::memory_order_relaxed);
            meter.outputRms[1].store(blockRms(outR, count), std::memory_order_relaxed);
            meter.gainReduction.store(runtime.compressor->getCurrentGainReductionDb(), std::memory_order_relaxed);
            meter.compressorDetectorDb.store(runtime.compressor->getDetectorLevelDb(), std::memory_order_relaxed);
            meter.compressorOutputDb.store(runtime.compressor->getOutputLevelDb(0), std::memory_order_relaxed);
            meter.compressorOutputDbR.store(runtime.compressor->getOutputLevelDb(1), std::memory_order_relaxed);
            meter.gateGainReduction.store(runtime.gate->getGainReductionDb(), std::memory_order_relaxed);
            meter.gateDetectorDb.store(runtime.gate->getDetectorLevelDb(), std::memory_order_relaxed);
            meter.gateState.store(static_cast<uint8_t>(runtime.gate->getCurrentState()), std::memory_order_relaxed);
            meter.limiterGainReduction.store(runtime.limiter->getCurrentGainReductionDb(), std::memory_order_relaxed);
        }
    }

    void reset() override {
        for (size_t i = 0; i < runtimes_.size(); ++i) {
            auto& runtime = runtimes_[i];
            runtime->hpf->reset();
            runtime->peq->reset();
            runtime->geq->reset();
            runtime->compressor->reset();
            runtime->gate->reset();
            runtime->limiter->reset();
            for (size_t s = 0; s < 8; ++s) {
                resetFxSlot(controls_[i]->fxSlots[s]);
            }
            for (size_t ch = 0; ch < 2; ++ch) {
                meters_[i]->peak[ch].store(0.0f, std::memory_order_relaxed);
                meters_[i]->rms[ch].store(0.0f, std::memory_order_relaxed);
                meters_[i]->fxInputPeak[ch].store(0.0f, std::memory_order_relaxed);
                meters_[i]->fxInputRms[ch].store(0.0f, std::memory_order_relaxed);
                meters_[i]->fxOutputPeak[ch].store(0.0f, std::memory_order_relaxed);
                meters_[i]->fxOutputRms[ch].store(0.0f, std::memory_order_relaxed);
                meters_[i]->outputPeak[ch].store(0.0f, std::memory_order_relaxed);
                meters_[i]->outputRms[ch].store(0.0f, std::memory_order_relaxed);
                meters_[i]->preLimiterPeak[ch].store(0.0f, std::memory_order_relaxed);
            }
        }
    }

    std::vector<StripAudioTelemetry> telemetry() const {
        std::vector<StripAudioTelemetry> result;
        result.reserve(runtimes_.size());
        for (size_t i = 0; i < runtimes_.size(); ++i) {
            StripAudioTelemetry item;
            item.logicalId = runtimes_[i]->logicalId;
            item.channelCount = runtimes_[i]->channelCount.load(std::memory_order_relaxed);
            for (size_t ch = 0; ch < 2; ++ch) {
                item.inputPeak[ch] = meters_[i]->peak[ch].load(std::memory_order_relaxed);
                item.inputRms[ch] = meters_[i]->rms[ch].load(std::memory_order_relaxed);
                item.fxInputPeak[ch] = meters_[i]->fxInputPeak[ch].load(std::memory_order_relaxed);
                item.fxInputRms[ch] = meters_[i]->fxInputRms[ch].load(std::memory_order_relaxed);
                item.fxOutputPeak[ch] = meters_[i]->fxOutputPeak[ch].load(std::memory_order_relaxed);
                item.fxOutputRms[ch] = meters_[i]->fxOutputRms[ch].load(std::memory_order_relaxed);
                item.outputPeak[ch] = meters_[i]->outputPeak[ch].load(std::memory_order_relaxed);
                item.outputRms[ch] = meters_[i]->outputRms[ch].load(std::memory_order_relaxed);
                item.preLimiterPeak[ch] = meters_[i]->preLimiterPeak[ch].load(std::memory_order_relaxed);
            }
            item.gainReductionDb = meters_[i]->gainReduction.load(std::memory_order_relaxed);
            item.compressorDetectorDb = meters_[i]->compressorDetectorDb.load(std::memory_order_relaxed);
            item.compressorOutputDb = meters_[i]->compressorOutputDb.load(std::memory_order_relaxed);
            item.compressorOutputDbR = meters_[i]->compressorOutputDbR.load(std::memory_order_relaxed);
            item.gateGainReductionDb = meters_[i]->gateGainReduction.load(std::memory_order_relaxed);
            item.gateDetectorDb = meters_[i]->gateDetectorDb.load(std::memory_order_relaxed);
            item.gateState = meters_[i]->gateState.load(std::memory_order_relaxed);
            item.limiterGainReductionDb = meters_[i]->limiterGainReduction.load(std::memory_order_relaxed);
            populateAfsTelemetryFromSlots(controls_[i]->fxSlots, item);
            populateFxSlotTelemetry(controls_[i]->fxSlots, item);
            result.push_back(item);
        }
        return result;
    }

private:
    void populateControl(StripDspControl& ctrl, const AudioStripConfig& strip) {
        ctrl.format.store(static_cast<uint8_t>(strip.format), std::memory_order_relaxed);
        ctrl.polarityInverted.store(strip.polarityInverted, std::memory_order_relaxed);
        ctrl.hpfEnabled.store(strip.hpfEnabled, std::memory_order_relaxed);
        ctrl.hpfFrequency.store(strip.hpfFrequency, std::memory_order_relaxed);
        ctrl.peqBypassed.store(strip.peqBypassed, std::memory_order_relaxed);
        ctrl.geqMode.store(strip.geqMode, std::memory_order_relaxed);
        ctrl.geqBypassed.store(strip.geqBypassed, std::memory_order_relaxed);
        for (size_t b = 0; b < 4; ++b) {
            ctrl.eqFrequency[b].store(strip.eqFrequency[b], std::memory_order_relaxed);
            ctrl.eqGain[b].store(strip.eqGain[b], std::memory_order_relaxed);
            ctrl.eqQ[b].store(strip.eqQ[b], std::memory_order_relaxed);
            ctrl.eqType[b].store(strip.eqType[b], std::memory_order_relaxed);
            ctrl.eqBandEnabled[b].store(strip.eqBandEnabled[b], std::memory_order_relaxed);
        }
        for (size_t b = 0; b < 31 && b < strip.geqGain.size(); ++b) {
            ctrl.geqGain[b].store(strip.geqGain[b], std::memory_order_relaxed);
        }
        ctrl.compressorEnabled.store(strip.compressorEnabled, std::memory_order_relaxed);
        ctrl.compressorThresholdDb.store(strip.compressorThresholdDb, std::memory_order_relaxed);
        ctrl.compressorRatio.store(strip.compressorRatio, std::memory_order_relaxed);
        ctrl.compressorAttackMs.store(strip.compressorAttackMs, std::memory_order_relaxed);
        ctrl.compressorReleaseMs.store(strip.compressorReleaseMs, std::memory_order_relaxed);
        ctrl.compressorKneeDb.store(strip.compressorKneeDb, std::memory_order_relaxed);
        ctrl.compressorMakeupDb.store(strip.compressorMakeupDb, std::memory_order_relaxed);
        ctrl.gateEnabled.store(strip.gateEnabled, std::memory_order_relaxed);
        ctrl.gateThresholdDb.store(strip.gateThresholdDb, std::memory_order_relaxed);
        ctrl.gateRangeDb.store(strip.gateRangeDb, std::memory_order_relaxed);
        ctrl.gateAttackMs.store(strip.gateAttackMs, std::memory_order_relaxed);
        ctrl.gateHoldMs.store(strip.gateHoldMs, std::memory_order_relaxed);
        ctrl.gateReleaseMs.store(strip.gateReleaseMs, std::memory_order_relaxed);
        ctrl.limiterEnabled.store(strip.limiterEnabled, std::memory_order_relaxed);
        ctrl.limiterCeilingDb.store(strip.limiterCeilingDb, std::memory_order_relaxed);
        ctrl.limiterReleaseMs.store(strip.limiterReleaseMs, std::memory_order_relaxed);

        for (size_t s = 0; s < 8; ++s) {
            if (s < strip.fxRack.size()) {
                writeFxSlotControl(ctrl.fxSlots[s], strip.fxRack[s], sampleRate_, ownedFxNodes_);
            } else {
                AudioStripFxConfig emptyFx{};
                writeFxSlotControl(ctrl.fxSlots[s], emptyFx, sampleRate_, ownedFxNodes_);
            }
        }
    }

    struct Runtime {
        uint32_t logicalId = 0;
        std::atomic<uint32_t> channelCount{1};
        bool polarityInverted = false;
        std::unique_ptr<audiocore::ParametricEQNode> hpf;
        std::unique_ptr<audiocore::ParametricEQNode> peq;
        std::unique_ptr<audiocore::GraphicEQNode> geq;
        std::unique_ptr<audiocore::CompressorNode> compressor;
        std::unique_ptr<audiocore::GateNode> gate;
        std::unique_ptr<audiocore::LimiterNode> limiter;
        audiocore::AudioBuffer inputMono;
        audiocore::AudioBuffer scratchAMono;
        audiocore::AudioBuffer scratchBMono;
        audiocore::AudioBuffer inputStereo;
        audiocore::AudioBuffer scratchAStereo;
        audiocore::AudioBuffer scratchBStereo;
    };
    struct MeterState {
        std::atomic<float> peak[2]{{0.0f}, {0.0f}};
        std::atomic<float> rms[2]{{0.0f}, {0.0f}};
        std::atomic<float> fxInputPeak[2]{{0.0f}, {0.0f}};
        std::atomic<float> fxInputRms[2]{{0.0f}, {0.0f}};
        std::atomic<float> fxOutputPeak[2]{{0.0f}, {0.0f}};
        std::atomic<float> fxOutputRms[2]{{0.0f}, {0.0f}};
        std::atomic<float> outputPeak[2]{{0.0f}, {0.0f}};
        std::atomic<float> outputRms[2]{{0.0f}, {0.0f}};
        std::atomic<float> preLimiterPeak[2]{{0.0f}, {0.0f}};
        std::atomic<float> gainReduction{0.0f};
        std::atomic<float> compressorDetectorDb{-90.0f};
        std::atomic<float> compressorOutputDb{-90.0f};
        std::atomic<float> compressorOutputDbR{-90.0f};
        std::atomic<float> gateGainReduction{0.0f};
        std::atomic<float> gateDetectorDb{-90.0f};
        std::atomic<uint8_t> gateState{0};
        std::atomic<float> limiterGainReduction{0.0f};
    };
    double sampleRate_;
    std::mutex controlWriteMutex_;
    std::vector<std::unique_ptr<audiocore::AudioNode>> ownedFxNodes_;
    std::vector<std::unique_ptr<Runtime>> runtimes_;
    std::vector<std::unique_ptr<MeterState>> meters_;
    std::vector<std::unique_ptr<StripDspControl>> controls_;
    std::atomic<uint32_t> tapTargetId_{0};
    std::atomic<uint8_t> tapSource_{1};
    std::atomic<AnalyzerRingBuffer*> tapRingBuffer_{nullptr};
};

class ConsoleMainOutputNode final : public audiocore::AudioNode {
public:
    struct StripState {
        uint32_t logicalId = 0;
        std::atomic<uint8_t> format{0};
        std::atomic<float> gainDb{0.0f};
        std::atomic<float> pan{0.0f};
        std::atomic<bool> muted{false};
        std::atomic<bool> solo{false};
        std::atomic<bool> routeToMain{true};
        std::atomic<uint8_t> outputRoute{static_cast<uint8_t>(AudioOutputRoute::MainLR)};
        std::atomic<uint32_t> outputRouteIndex{0};
        std::atomic<float> auxSendLevel[4];
        std::atomic<bool> auxSendEnabled[4];
        std::atomic<bool> auxSendPreFader[4];
        std::atomic<float> postFaderPeak[2]{{0.0f}, {0.0f}};

        StripState() {
            for (int i = 0; i < 4; ++i) {
                auxSendLevel[i].store(0.0f, std::memory_order_relaxed);
                auxSendEnabled[i].store(true, std::memory_order_relaxed);
                auxSendPreFader[i].store(false, std::memory_order_relaxed);
            }
        }
    };

    struct BusDspChain {
        uint32_t logicalId = 0;
        bool isAuxBus = false;
        std::atomic<bool> configured{false};
        std::atomic<bool> dirty{false};
        std::atomic<uint8_t> format{static_cast<uint8_t>(AudioStripFormat::Stereo)};
        std::atomic<float> gainDb{0.0f};
        std::atomic<float> returnGainDb{0.0f};
        std::atomic<float> pan{0.0f};
        std::atomic<bool> muted{false};
        std::atomic<bool> routeToMain{true};

        std::atomic<bool> hpfEnabled{false};
        std::atomic<float> hpfFrequency{80.0f};
        std::atomic<bool> peqBypassed{true};
        std::atomic<bool> geqMode{false};
        std::atomic<bool> geqBypassed{true};
        std::atomic<float> eqFrequency[4];
        std::atomic<float> eqGain[4];
        std::atomic<float> eqQ[4];
        std::atomic<uint8_t> eqType[4];
        std::atomic<bool> eqBandEnabled[4];
        std::atomic<float> geqGain[31];

        std::atomic<bool> compressorEnabled{false};
        std::atomic<float> compressorThresholdDb{-18.0f};
        std::atomic<float> compressorRatio{4.0f};
        std::atomic<float> compressorAttackMs{10.0f};
        std::atomic<float> compressorReleaseMs{80.0f};
        std::atomic<float> compressorKneeDb{6.0f};
        std::atomic<float> compressorMakeupDb{0.0f};

        std::atomic<bool> gateEnabled{false};
        std::atomic<float> gateThresholdDb{-48.0f};
        std::atomic<float> gateRangeDb{-60.0f};
        std::atomic<float> gateAttackMs{5.0f};
        std::atomic<float> gateHoldMs{50.0f};
        std::atomic<float> gateReleaseMs{120.0f};

        std::atomic<bool> limiterEnabled{false};
        std::atomic<float> limiterCeilingDb{-0.3f};
        std::atomic<float> limiterReleaseMs{50.0f};

        std::atomic<bool> speakerDelayEnabled{false};
        std::atomic<bool> speakerDelayLink{true};
        std::atomic<float> speakerDelayMsL{0.0f};
        std::atomic<float> speakerDelayMsR{0.0f};

        std::array<FxSlotControl, 8> fxSlots{};

        std::unique_ptr<audiocore::ParametricEQNode> hpf;
        std::unique_ptr<audiocore::ParametricEQNode> peq;
        std::unique_ptr<audiocore::GraphicEQNode> geq;
        std::unique_ptr<audiocore::CompressorNode> compressor;
        std::unique_ptr<audiocore::GateNode> gate;
        std::unique_ptr<audiocore::LimiterNode> limiter;
        std::unique_ptr<audiocore::AlignmentDelayNode> alignmentDelay;

        audiocore::AudioBuffer inputMono;
        audiocore::AudioBuffer scratchAMono;
        audiocore::AudioBuffer scratchBMono;
        audiocore::AudioBuffer inputStereo;
        audiocore::AudioBuffer scratchAStereo;
        audiocore::AudioBuffer scratchBStereo;

        std::atomic<uint32_t> activeChannels{2};
        std::atomic<float> inputPeak[2]{{0.0f}, {0.0f}};
        std::atomic<float> inputRms[2]{{0.0f}, {0.0f}};
        std::atomic<float> fxInputPeak[2]{{0.0f}, {0.0f}};
        std::atomic<float> fxInputRms[2]{{0.0f}, {0.0f}};
        std::atomic<float> fxOutputPeak[2]{{0.0f}, {0.0f}};
        std::atomic<float> fxOutputRms[2]{{0.0f}, {0.0f}};
        std::atomic<float> preLimiterPeak[2]{{0.0f}, {0.0f}};
        std::atomic<float> outputPeak[2]{{0.0f}, {0.0f}};
        std::atomic<float> outputRms[2]{{0.0f}, {0.0f}};
        std::atomic<float> compGrDb{0.0f};
        std::atomic<float> compDetectorDb{-90.0f};
        std::atomic<float> compOutputDb{-90.0f};
        std::atomic<float> compOutputDbR{-90.0f};
        std::atomic<float> gateGrDb{0.0f};
        std::atomic<float> gateDetectorDb{-90.0f};
        std::atomic<uint8_t> gateState{0};
        std::atomic<float> limiterGrDb{0.0f};

        BusDspChain(double sampleRate, uint32_t maxFrames, bool auxBus, uint32_t id)
            : logicalId(id), isAuxBus(auxBus),
              hpf(std::make_unique<audiocore::ParametricEQNode>(1, sampleRate)),
              peq(std::make_unique<audiocore::ParametricEQNode>(4, sampleRate)),
              geq(std::make_unique<audiocore::GraphicEQNode>(sampleRate)),
              compressor(std::make_unique<audiocore::CompressorNode>(sampleRate)),
              gate(std::make_unique<audiocore::GateNode>(sampleRate)),
              limiter(std::make_unique<audiocore::LimiterNode>(-0.3f, sampleRate)),
              alignmentDelay(std::make_unique<audiocore::AlignmentDelayNode>(2, sampleRate)),
              inputMono(1, maxFrames), scratchAMono(1, maxFrames), scratchBMono(1, maxFrames),
              inputStereo(2, maxFrames), scratchAStereo(2, maxFrames), scratchBStereo(2, maxFrames)
        {
            const float defaultFreqs[4] = {80.0f, 350.0f, 2500.0f, 10000.0f};
            const float defaultQs[4] = {0.7f, 1.0f, 1.0f, 0.7f};
            const uint8_t defaultTypes[4] = {2, 1, 1, 3};
            for (int i = 0; i < 4; ++i) {
                eqFrequency[i].store(defaultFreqs[i], std::memory_order_relaxed);
                eqGain[i].store(0.0f, std::memory_order_relaxed);
                eqQ[i].store(defaultQs[i], std::memory_order_relaxed);
                eqType[i].store(defaultTypes[i], std::memory_order_relaxed);
                eqBandEnabled[i].store(true, std::memory_order_relaxed);
            }
            for (int i = 0; i < 31; ++i) {
                geqGain[i].store(0.0f, std::memory_order_relaxed);
            }
            hpf->setBypassed(true);
            peq->setBypassed(true);
            geq->setBypassed(true);
            compressor->setBypassed(true);
            gate->setBypassed(true);
            limiter->setBypassed(true);
            alignmentDelay->setBypassed(true);
        }

        void reset() noexcept {
            hpf->reset();
            peq->reset();
            geq->reset();
            compressor->reset();
            gate->reset();
            limiter->reset();
            if (alignmentDelay) alignmentDelay->reset();
            for (size_t s = 0; s < 8; ++s) resetFxSlot(fxSlots[s]);
            for (size_t ch = 0; ch < 2; ++ch) {
                inputPeak[ch].store(0.0f, std::memory_order_relaxed);
                inputRms[ch].store(0.0f, std::memory_order_relaxed);
                fxInputPeak[ch].store(0.0f, std::memory_order_relaxed);
                fxInputRms[ch].store(0.0f, std::memory_order_relaxed);
                fxOutputPeak[ch].store(0.0f, std::memory_order_relaxed);
                fxOutputRms[ch].store(0.0f, std::memory_order_relaxed);
                preLimiterPeak[ch].store(0.0f, std::memory_order_relaxed);
                outputPeak[ch].store(0.0f, std::memory_order_relaxed);
                outputRms[ch].store(0.0f, std::memory_order_relaxed);
            }
            inputMono.clear();
            scratchAMono.clear();
            scratchBMono.clear();
            inputStereo.clear();
            scratchAStereo.clear();
            scratchBStereo.clear();
        }
    };

    ConsoleMainOutputNode(const std::vector<AudioStripConfig>& strips,
                          std::shared_ptr<StereoDemoDspNode> masterProcessor,
                          uint32_t renderChannels, uint32_t maxFrames,
                          double sampleRate = 48000.0)
        : strips_(strips), masterProcessor_(std::move(masterProcessor)),
          sampleRate_(sampleRate > 0.0 ? sampleRate : 48000.0),
          mixed_(2, maxFrames), processed_(2, maxFrames), meteredMain_(2, maxFrames), renderChannels_(renderChannels),
          outputMeter_(std::make_shared<audiocore::MeterNode>()),
          masterBus_(std::make_unique<BusDspChain>(sampleRate_, maxFrames, false, 100))
    {
        for (size_t a = 0; a < 4; ++a) {
            auxBuses_[a] = std::make_unique<BusDspChain>(sampleRate_, maxFrames, true, static_cast<uint32_t>(101 + a));
        }
        liveStates_.reserve(strips.size());
        for (const auto& s : strips) {
            auto st = std::make_unique<StripState>();
            st->logicalId = s.logicalId;
            applyStripState(*st, s);
            liveStates_.push_back(std::move(st));
        }
    }

    void updateStripByLogicalId(uint32_t logicalId, float gainDb, float pan, bool muted, bool solo) noexcept {
        for (auto& st : liveStates_) {
            if (st->logicalId == logicalId) {
                st->gainDb.store(gainDb, std::memory_order_relaxed);
                st->pan.store(pan, std::memory_order_relaxed);
                st->muted.store(muted, std::memory_order_relaxed);
                st->solo.store(solo, std::memory_order_relaxed);
                return;
            }
        }
    }

    void setAnalyzerTap(uint32_t channelId, uint8_t tapSource, AnalyzerRingBuffer* ring) noexcept {
        tapTargetId_.store(channelId, std::memory_order_relaxed);
        tapSource_.store(tapSource, std::memory_order_relaxed);
        tapRingBuffer_.store(ring, std::memory_order_release);
    }

    void updateStripConfig(const AudioStripConfig& config) noexcept {
        for (auto& st : liveStates_) {
            if (st->logicalId == config.logicalId) {
                applyStripState(*st, config);
                return;
            }
        }
    }

    void updateAuxBusConfig(size_t auxIndex, const AudioBusConfig& config) noexcept {
        if (auxIndex >= 4) return;
        std::lock_guard<std::mutex> lock(controlWriteMutex_);
        populateBusChain(*auxBuses_[auxIndex], config);
    }

    void updateMasterBusConfig(const AudioBusConfig& config) noexcept {
        std::lock_guard<std::mutex> lock(controlWriteMutex_);
        populateBusChain(*masterBus_, config);
    }

    StripAudioTelemetry masterTelemetry() const noexcept {
        return readBusTelemetry(*masterBus_);
    }

    std::array<StripAudioTelemetry, 4> auxTelemetry() const noexcept {
        std::array<StripAudioTelemetry, 4> res{};
        for (size_t a = 0; a < 4; ++a) res[a] = readBusTelemetry(*auxBuses_[a]);
        return res;
    }

    std::array<float, 2> stripPostFaderPeak(uint32_t logicalId) const noexcept {
        for (const auto& st : liveStates_) {
            if (st->logicalId == logicalId) {
                return {st->postFaderPeak[0].load(std::memory_order_relaxed),
                        st->postFaderPeak[1].load(std::memory_order_relaxed)};
            }
        }
        return {0.0f, 0.0f};
    }

    void process(const audiocore::AudioBuffer& input, audiocore::AudioBuffer& output,
                 uint32_t frames) override {
        const uint32_t count = std::min({frames, input.getFrameCount(), mixed_.getFrameCount(), output.getFrameCount()});
        output.clear(count);
        mixed_.clear(count);
        for (size_t a = 0; a < 4; ++a) {
            auxBuses_[a]->inputMono.clear(count);
            auxBuses_[a]->inputStereo.clear(count);
        }

        const bool anySolo = std::any_of(liveStates_.begin(), liveStates_.end(), [](const auto& st) {
            return st->solo.load(std::memory_order_relaxed) && !st->muted.load(std::memory_order_relaxed);
        });
        float* outL = mixed_[0];
        float* outR = mixed_[1];

        for (size_t i = 0; i < strips_.size(); ++i) {
            auto& st = *liveStates_[i];
            const bool isMuted = st.muted.load(std::memory_order_relaxed);
            const bool isSolo = st.solo.load(std::memory_order_relaxed);
            const bool activePreFader = (!anySolo || isSolo);
            const bool activePostFader = !isMuted && activePreFader;

            const uint32_t left = static_cast<uint32_t>(i * 2);
            const uint32_t right = left + 1;
            if (right >= input.getChannelCount()) continue;

            const auto format = static_cast<AudioStripFormat>(st.format.load(std::memory_order_relaxed));
            const float gainDb = st.gainDb.load(std::memory_order_relaxed);
            const float faderGain = stripGainLinear(gainDb);
            const float pan = std::clamp(st.pan.load(std::memory_order_relaxed), -1.0f, 1.0f);
            const float* inL = input.getChannelData(left);
            const float* inR = input.getChannelData(right);

            if (activePostFader) {
                st.postFaderPeak[0].store(blockPeak(inL, count) * faderGain, std::memory_order_relaxed);
                st.postFaderPeak[1].store(
                    blockPeak(format == AudioStripFormat::Stereo ? inR : inL, count) * faderGain,
                    std::memory_order_relaxed);
            } else {
                st.postFaderPeak[0].store(0.0f, std::memory_order_relaxed);
                st.postFaderPeak[1].store(0.0f, std::memory_order_relaxed);
            }

            // Constant-power pan (for Mono strip) vs Stereo Balance (for Stereo strip)
            const float angle = (pan + 1.0f) * 0.25f * 3.14159265358979323846f;
            const float panCos = std::cos(angle);
            const float panSin = std::sin(angle);
            const float balL = pan <= 0.0f ? 1.0f : (1.0f - pan);
            const float balR = pan >= 0.0f ? 1.0f : (1.0f + pan);

            // Route Pre-Fader and Post-Fader AUX Sends into AUX Buses 0..3
            for (size_t a = 0; a < 4; ++a) {
                if (!st.auxSendEnabled[a].load(std::memory_order_relaxed)) continue;
                const float sendLvl = st.auxSendLevel[a].load(std::memory_order_relaxed);
                if (sendLvl <= 0.0f) continue;
                const bool isPre = st.auxSendPreFader[a].load(std::memory_order_relaxed);
                const float tapGain = isPre
                    ? (activePreFader ? sendLvl : 0.0f)
                    : (activePostFader ? faderGain * sendLvl : 0.0f);
                if (tapGain <= 0.0f) continue;

                auto& aux = *auxBuses_[a];
                const auto auxFmt = static_cast<AudioStripFormat>(aux.format.load(std::memory_order_relaxed));
                if (auxFmt == AudioStripFormat::Mono) {
                    float* auxIn = aux.inputMono[0];
                    if (format == AudioStripFormat::Stereo) {
                        for (uint32_t f = 0; f < count; ++f) {
                            auxIn[f] += 0.5f * (inL[f] + inR[f]) * tapGain;
                        }
                    } else {
                        for (uint32_t f = 0; f < count; ++f) {
                            auxIn[f] += inL[f] * tapGain;
                        }
                    }
                } else {
                    float* auxInL = aux.inputStereo[0];
                    float* auxInR = aux.inputStereo[1];
                    if (format == AudioStripFormat::Stereo) {
                        const float gL = tapGain * balL;
                        const float gR = tapGain * balR;
                        for (uint32_t f = 0; f < count; ++f) {
                            auxInL[f] += inL[f] * gL;
                            auxInR[f] += inR[f] * gR;
                        }
                    } else {
                        const float gL = tapGain * panCos;
                        const float gR = tapGain * panSin;
                        for (uint32_t f = 0; f < count; ++f) {
                            auxInL[f] += inL[f] * gL;
                            auxInR[f] += inL[f] * gR;
                        }
                    }
                }
            }

            const auto outRoute = static_cast<AudioOutputRoute>(st.outputRoute.load(std::memory_order_relaxed));
            const uint32_t outRouteIdx = st.outputRouteIndex.load(std::memory_order_relaxed);
            const bool routeToMain = st.routeToMain.load(std::memory_order_relaxed);

            if (!activePostFader || outRoute == AudioOutputRoute::None) continue;

            if (outRoute == AudioOutputRoute::MainLR) {
                if (!routeToMain) continue;
                if (format == AudioStripFormat::Stereo) {
                    const float leftGain = faderGain * balL;
                    const float rightGain = faderGain * balR;
                    for (uint32_t frame = 0; frame < count; ++frame) {
                        outL[frame] += inL[frame] * leftGain;
                        outR[frame] += inR[frame] * rightGain;
                    }
                } else {
                    const float leftGain = faderGain * panCos;
                    const float rightGain = faderGain * panSin;
                    for (uint32_t frame = 0; frame < count; ++frame) {
                        outL[frame] += inL[frame] * leftGain;
                        outR[frame] += inL[frame] * rightGain;
                    }
                }
            } else {
                const uint32_t physicalOutput = outRoute == AudioOutputRoute::Aux
                    ? 2 + outRouteIdx : outRouteIdx;
                if (physicalOutput >= renderChannels_ || physicalOutput >= output.getChannelCount()) continue;
                float* destination = output.getChannelData(physicalOutput);
                if (format == AudioStripFormat::Stereo) {
                    for (uint32_t frame = 0; frame < count; ++frame)
                        destination[frame] += (inL[frame] + inR[frame]) * 0.5f * faderGain;
                } else {
                    for (uint32_t frame = 0; frame < count; ++frame)
                        destination[frame] += inL[frame] * faderGain;
                }
            }
        }

        // Process AUX Buses 0..3 and sum their returns into Main L/R
        for (size_t a = 0; a < 4; ++a) {
            auto& aux = *auxBuses_[a];
            syncBusChain(aux);

            const uint32_t auxCh = aux.activeChannels.load(std::memory_order_relaxed);
            audiocore::AudioBuffer& inBuf = (auxCh == 2) ? aux.inputStereo : aux.inputMono;
            audiocore::AudioBuffer& scA = (auxCh == 2) ? aux.scratchAStereo : aux.scratchAMono;
            audiocore::AudioBuffer& scB = (auxCh == 2) ? aux.scratchBStereo : aux.scratchBMono;

            const float inPeakL = blockPeak(inBuf[0], count);
            const float inPeakR = blockPeak(auxCh == 2 ? inBuf[1] : inBuf[0], count);
            aux.inputPeak[0].store(inPeakL, std::memory_order_relaxed);
            aux.inputPeak[1].store(inPeakR, std::memory_order_relaxed);
            aux.inputRms[0].store(blockRms(inBuf[0], count), std::memory_order_relaxed);
            aux.inputRms[1].store(blockRms(auxCh == 2 ? inBuf[1] : inBuf[0], count), std::memory_order_relaxed);

            audiocore::AudioBuffer* current = &inBuf;
            audiocore::AudioBuffer* next = &scA;
            const auto run = [&](audiocore::AudioNode* node) {
                if (!node) return;
                node->process(*current, *next, count);
                current = next;
                next = (current == &scA) ? &scB : &scA;
            };
            const uint32_t tapTarget = tapTargetId_.load(std::memory_order_relaxed);
            const uint8_t tapSrc = tapSource_.load(std::memory_order_relaxed);
            auto* ring = tapRingBuffer_.load(std::memory_order_relaxed);
            const bool isAuxTap = (ring != nullptr && aux.logicalId == tapTarget);

            run(aux.hpf.get());
            run(aux.gate.get());
            if (isAuxTap && tapSrc == 0) { // PRE_EQ
                ring->push((*current)[0], count);
            }
            run(aux.peq.get());
            run(aux.geq.get());
            if (isAuxTap && tapSrc == 1) { // POST_EQ
                ring->push((*current)[0], count);
            }
            run(aux.compressor.get());

            const bool auxStereoPre = current->getChannelCount() > 1;
            aux.fxInputPeak[0].store(blockPeak((*current)[0], count), std::memory_order_relaxed);
            aux.fxInputPeak[1].store(auxStereoPre ? blockPeak((*current)[1], count) : 0.0f, std::memory_order_relaxed);
            aux.fxInputRms[0].store(blockRms((*current)[0], count), std::memory_order_relaxed);
            aux.fxInputRms[1].store(auxStereoPre ? blockRms((*current)[1], count) : 0.0f, std::memory_order_relaxed);

            for (size_t s = 0; s < 8; ++s) {
                processFxSlot(aux.fxSlots[s], true, current, next, scA, scB, count);
            }

            const bool auxStereoPost = current->getChannelCount() > 1;
            aux.fxOutputPeak[0].store(blockPeak((*current)[0], count), std::memory_order_relaxed);
            aux.fxOutputPeak[1].store(auxStereoPost ? blockPeak((*current)[1], count) : 0.0f, std::memory_order_relaxed);
            aux.fxOutputRms[0].store(blockRms((*current)[0], count), std::memory_order_relaxed);
            aux.fxOutputRms[1].store(auxStereoPost ? blockRms((*current)[1], count) : 0.0f, std::memory_order_relaxed);

            // Apply AUX bus fader & mute before AUX limiter
            const bool auxMuted = aux.muted.load(std::memory_order_relaxed);
            const float auxFaderGain = auxMuted ? 0.0f : stripGainLinear(aux.gainDb.load(std::memory_order_relaxed));
            for (uint32_t ch = 0; ch < current->getChannelCount(); ++ch) {
                float* chData = (*current)[ch];
                for (uint32_t f = 0; f < count; ++f) chData[f] *= auxFaderGain;
            }

            aux.preLimiterPeak[0].store(blockPeak((*current)[0], count), std::memory_order_relaxed);
            aux.preLimiterPeak[1].store(
                blockPeak(current->getChannelCount() > 1 ? (*current)[1] : (*current)[0], count),
                std::memory_order_relaxed);

            run(aux.limiter.get());

            const float outPeakL = blockPeak((*current)[0], count);
            const float outPeakR = blockPeak(current->getChannelCount() > 1 ? (*current)[1] : (*current)[0], count);
            aux.outputPeak[0].store(outPeakL, std::memory_order_relaxed);
            aux.outputPeak[1].store(outPeakR, std::memory_order_relaxed);
            aux.outputRms[0].store(blockRms((*current)[0], count), std::memory_order_relaxed);
            aux.outputRms[1].store(
                blockRms(current->getChannelCount() > 1 ? (*current)[1] : (*current)[0], count),
                std::memory_order_relaxed);
            aux.compGrDb.store(aux.compressor->getCurrentGainReductionDb(), std::memory_order_relaxed);
            aux.compDetectorDb.store(aux.compressor->getDetectorLevelDb(), std::memory_order_relaxed);
            aux.compOutputDb.store(aux.compressor->getOutputLevelDb(0), std::memory_order_relaxed);
            aux.compOutputDbR.store(aux.compressor->getOutputLevelDb(1), std::memory_order_relaxed);
            aux.gateGrDb.store(aux.gate->getGainReductionDb(), std::memory_order_relaxed);
            aux.gateDetectorDb.store(aux.gate->getDetectorLevelDb(), std::memory_order_relaxed);
            aux.gateState.store(static_cast<uint8_t>(aux.gate->getCurrentState()), std::memory_order_relaxed);
            aux.limiterGrDb.store(aux.limiter->getCurrentGainReductionDb(), std::memory_order_relaxed);

            if (!auxMuted && aux.routeToMain.load(std::memory_order_relaxed)) {
                const float retGain = stripGainLinear(aux.returnGainDb.load(std::memory_order_relaxed));
                const float auxPan = std::clamp(aux.pan.load(std::memory_order_relaxed), -1.0f, 1.0f);
                if (current->getChannelCount() > 1) {
                    const float bL = (auxPan <= 0.0f ? 1.0f : (1.0f - auxPan)) * retGain;
                    const float bR = (auxPan >= 0.0f ? 1.0f : (1.0f + auxPan)) * retGain;
                    const float* srcL = (*current)[0];
                    const float* srcR = (*current)[1];
                    for (uint32_t f = 0; f < count; ++f) {
                        outL[f] += srcL[f] * bL;
                        outR[f] += srcR[f] * bR;
                    }
                } else {
                    const float ang = (auxPan + 1.0f) * 0.25f * 3.14159265358979323846f;
                    const float gL = std::cos(ang) * retGain;
                    const float gR = std::sin(ang) * retGain;
                    const float* srcM = (*current)[0];
                    for (uint32_t f = 0; f < count; ++f) {
                        outL[f] += srcM[f] * gL;
                        outR[f] += srcM[f] * gR;
                    }
                }
            }
        }

        // Process Master Bus DSP Chain:
        // Summed Bus Signal -> StereoDemoDspNode (when not configured via MasterBusConfig)
        // -> Master EQ -> Master Compressor -> Master Insert FX -> Master Fader/Mute
        // -> Pre-Limiter Meter Tap -> Master Brickwall Limiter -> Post-Limiter Output Meter Tap
        auto& master = *masterBus_;
        syncBusChain(master);
        if (master.configured.load(std::memory_order_relaxed)) {
            processed_.copyFrom(mixed_, count);
        } else {
            masterProcessor_->process(mixed_, processed_, count);
        }

        master.inputPeak[0].store(blockPeak(processed_[0], count), std::memory_order_relaxed);
        master.inputPeak[1].store(blockPeak(processed_[1], count), std::memory_order_relaxed);
        master.inputRms[0].store(blockRms(processed_[0], count), std::memory_order_relaxed);
        master.inputRms[1].store(blockRms(processed_[1], count), std::memory_order_relaxed);

        audiocore::AudioBuffer* mCur = &processed_;
        audiocore::AudioBuffer* mNext = &master.scratchAStereo;
        const auto runMaster = [&](audiocore::AudioNode* node) {
            if (!node) return;
            node->process(*mCur, *mNext, count);
            mCur = mNext;
            mNext = (mCur == &master.scratchAStereo) ? &master.scratchBStereo : &master.scratchAStereo;
        };

        const uint32_t tapTarget = tapTargetId_.load(std::memory_order_relaxed);
        const uint8_t tapSrc = tapSource_.load(std::memory_order_relaxed);
        auto* ring = tapRingBuffer_.load(std::memory_order_relaxed);
        const bool isMasterTap = (ring != nullptr && tapTarget == 0);

        if (!master.configured.load(std::memory_order_relaxed) && isMasterTap) {
            ring->push((*mCur)[0], count);
        }

        if (master.configured.load(std::memory_order_relaxed)) {
            runMaster(master.hpf.get());
            runMaster(master.gate.get());
            if (isMasterTap && tapSrc == 0) { // PRE_EQ
                ring->push((*mCur)[0], count);
            }
            runMaster(master.peq.get());
            runMaster(master.geq.get());
            if (isMasterTap && tapSrc == 1) { // POST_EQ
                ring->push((*mCur)[0], count);
            }
            runMaster(master.compressor.get());

            master.fxInputPeak[0].store(blockPeak((*mCur)[0], count), std::memory_order_relaxed);
            master.fxInputPeak[1].store(blockPeak((*mCur)[1], count), std::memory_order_relaxed);
            master.fxInputRms[0].store(blockRms((*mCur)[0], count), std::memory_order_relaxed);
            master.fxInputRms[1].store(blockRms((*mCur)[1], count), std::memory_order_relaxed);

            for (size_t s = 0; s < 8; ++s) {
                processFxSlot(master.fxSlots[s], false, mCur, mNext,
                              master.scratchAStereo, master.scratchBStereo, count);
            }

            master.fxOutputPeak[0].store(blockPeak((*mCur)[0], count), std::memory_order_relaxed);
            master.fxOutputPeak[1].store(blockPeak((*mCur)[1], count), std::memory_order_relaxed);
            master.fxOutputRms[0].store(blockRms((*mCur)[0], count), std::memory_order_relaxed);
            master.fxOutputRms[1].store(blockRms((*mCur)[1], count), std::memory_order_relaxed);

            const bool masterMuted = master.muted.load(std::memory_order_relaxed);
            const float masterGain = masterMuted ? 0.0f : stripGainLinear(master.gainDb.load(std::memory_order_relaxed));
            const float masterPan = std::clamp(master.pan.load(std::memory_order_relaxed), -1.0f, 1.0f);
            const float mBalL = (masterPan <= 0.0f ? 1.0f : (1.0f - masterPan)) * masterGain;
            const float mBalR = (masterPan >= 0.0f ? 1.0f : (1.0f + masterPan)) * masterGain;
            float* mL = (*mCur)[0];
            float* mR = (*mCur)[1];
            for (uint32_t f = 0; f < count; ++f) {
                mL[f] *= mBalL;
                mR[f] *= mBalR;
            }
        } else {
            master.fxInputPeak[0].store(blockPeak((*mCur)[0], count), std::memory_order_relaxed);
            master.fxInputPeak[1].store(blockPeak((*mCur)[1], count), std::memory_order_relaxed);
            master.fxInputRms[0].store(blockRms((*mCur)[0], count), std::memory_order_relaxed);
            master.fxInputRms[1].store(blockRms((*mCur)[1], count), std::memory_order_relaxed);
            master.fxOutputPeak[0].store(blockPeak((*mCur)[0], count), std::memory_order_relaxed);
            master.fxOutputPeak[1].store(blockPeak((*mCur)[1], count), std::memory_order_relaxed);
            master.fxOutputRms[0].store(blockRms((*mCur)[0], count), std::memory_order_relaxed);
            master.fxOutputRms[1].store(blockRms((*mCur)[1], count), std::memory_order_relaxed);
        }

        // Pre-Limiter Master Meter Tap
        master.preLimiterPeak[0].store(blockPeak((*mCur)[0], count), std::memory_order_relaxed);
        master.preLimiterPeak[1].store(blockPeak((*mCur)[1], count), std::memory_order_relaxed);

        // Master Brickwall Peak Limiter
        runMaster(master.limiter.get());

        // Dedicated Master Speaker Alignment Delay Stage
        runMaster(master.alignmentDelay.get());

        // Post-Limiter Master Output Meter Tap
        master.outputPeak[0].store(blockPeak((*mCur)[0], count), std::memory_order_relaxed);
        master.outputPeak[1].store(blockPeak((*mCur)[1], count), std::memory_order_relaxed);
        master.outputRms[0].store(blockRms((*mCur)[0], count), std::memory_order_relaxed);
        master.outputRms[1].store(blockRms((*mCur)[1], count), std::memory_order_relaxed);
        master.compGrDb.store(
            master.configured.load(std::memory_order_relaxed)
                ? master.compressor->getCurrentGainReductionDb()
                : masterProcessor_->compressorGainReductionDb(),
            std::memory_order_relaxed);
        master.compDetectorDb.store(
            master.configured.load(std::memory_order_relaxed)
                ? master.compressor->getDetectorLevelDb()
                : masterProcessor_->compressorDetectorDb(),
            std::memory_order_relaxed);
        master.compOutputDb.store(
            master.configured.load(std::memory_order_relaxed)
                ? master.compressor->getOutputLevelDb(0)
                : masterProcessor_->compressorOutputDb(),
            std::memory_order_relaxed);
        master.compOutputDbR.store(
            master.configured.load(std::memory_order_relaxed)
                ? master.compressor->getOutputLevelDb(1)
                : masterProcessor_->compressorOutputDb(),
            std::memory_order_relaxed);
        master.gateGrDb.store(master.gate->getGainReductionDb(), std::memory_order_relaxed);
        master.gateDetectorDb.store(master.gate->getDetectorLevelDb(), std::memory_order_relaxed);
        master.gateState.store(static_cast<uint8_t>(master.gate->getCurrentState()), std::memory_order_relaxed);
        master.limiterGrDb.store(master.limiter->getCurrentGainReductionDb(), std::memory_order_relaxed);

        if (output.getChannelCount() >= 2) {
            outputMeter_->process(*mCur, meteredMain_, count);
            std::memcpy(output[0], meteredMain_[0], count * sizeof(float));
            std::memcpy(output[1], meteredMain_[1], count * sizeof(float));
        }
    }

    void reset() override {
        mixed_.clear();
        processed_.clear();
        meteredMain_.clear();
        outputMeter_->reset();
        masterBus_->reset();
        for (auto& aux : auxBuses_) aux->reset();
    }

    std::shared_ptr<audiocore::MeterNode> outputMeter() const { return outputMeter_; }

private:
    static void applyStripState(StripState& st, const AudioStripConfig& s) noexcept {
        st.format.store(static_cast<uint8_t>(s.format), std::memory_order_relaxed);
        st.gainDb.store(s.gainDb, std::memory_order_relaxed);
        st.pan.store(s.pan, std::memory_order_relaxed);
        st.muted.store(s.muted, std::memory_order_relaxed);
        st.solo.store(s.solo, std::memory_order_relaxed);
        st.routeToMain.store(s.routeToMain, std::memory_order_relaxed);
        st.outputRoute.store(static_cast<uint8_t>(s.outputRoute), std::memory_order_relaxed);
        st.outputRouteIndex.store(s.outputRouteIndex, std::memory_order_relaxed);
        for (size_t a = 0; a < 4; ++a) {
            st.auxSendLevel[a].store(s.auxSendLevel[a], std::memory_order_relaxed);
            st.auxSendEnabled[a].store(s.auxSendEnabled[a], std::memory_order_relaxed);
            st.auxSendPreFader[a].store(s.auxSendPreFader[a], std::memory_order_relaxed);
        }
    }

    void populateBusChain(BusDspChain& bus, const AudioBusConfig& cfg) {
        bus.format.store(static_cast<uint8_t>(cfg.format), std::memory_order_relaxed);
        bus.gainDb.store(cfg.gainDb, std::memory_order_relaxed);
        bus.returnGainDb.store(cfg.returnGainDb, std::memory_order_relaxed);
        bus.pan.store(cfg.pan, std::memory_order_relaxed);
        bus.muted.store(cfg.muted, std::memory_order_relaxed);
        bus.routeToMain.store(cfg.routeToMain, std::memory_order_relaxed);
        bus.hpfEnabled.store(cfg.hpfEnabled, std::memory_order_relaxed);
        bus.hpfFrequency.store(cfg.hpfFrequency, std::memory_order_relaxed);
        bus.peqBypassed.store(cfg.peqBypassed, std::memory_order_relaxed);
        bus.geqMode.store(cfg.geqMode, std::memory_order_relaxed);
        bus.geqBypassed.store(cfg.geqBypassed, std::memory_order_relaxed);
        for (size_t b = 0; b < 4; ++b) {
            bus.eqFrequency[b].store(cfg.eqFrequency[b], std::memory_order_relaxed);
            bus.eqGain[b].store(cfg.eqGain[b], std::memory_order_relaxed);
            bus.eqQ[b].store(cfg.eqQ[b], std::memory_order_relaxed);
            bus.eqType[b].store(cfg.eqType[b], std::memory_order_relaxed);
            bus.eqBandEnabled[b].store(cfg.eqBandEnabled[b], std::memory_order_relaxed);
        }
        for (size_t b = 0; b < 31 && b < cfg.geqGain.size(); ++b) {
            bus.geqGain[b].store(cfg.geqGain[b], std::memory_order_relaxed);
        }
        bus.compressorEnabled.store(cfg.compressorEnabled, std::memory_order_relaxed);
        bus.compressorThresholdDb.store(cfg.compressorThresholdDb, std::memory_order_relaxed);
        bus.compressorRatio.store(cfg.compressorRatio, std::memory_order_relaxed);
        bus.compressorAttackMs.store(cfg.compressorAttackMs, std::memory_order_relaxed);
        bus.compressorReleaseMs.store(cfg.compressorReleaseMs, std::memory_order_relaxed);
        bus.compressorKneeDb.store(cfg.compressorKneeDb, std::memory_order_relaxed);
        bus.compressorMakeupDb.store(cfg.compressorMakeupDb, std::memory_order_relaxed);
        bus.gateEnabled.store(cfg.gateEnabled, std::memory_order_relaxed);
        bus.gateThresholdDb.store(cfg.gateThresholdDb, std::memory_order_relaxed);
        bus.gateRangeDb.store(cfg.gateRangeDb, std::memory_order_relaxed);
        bus.gateAttackMs.store(cfg.gateAttackMs, std::memory_order_relaxed);
        bus.gateHoldMs.store(cfg.gateHoldMs, std::memory_order_relaxed);
        bus.gateReleaseMs.store(cfg.gateReleaseMs, std::memory_order_relaxed);
        bus.limiterEnabled.store(cfg.limiterEnabled, std::memory_order_relaxed);
        bus.limiterCeilingDb.store(cfg.limiterCeilingDb, std::memory_order_relaxed);
        bus.limiterReleaseMs.store(cfg.limiterReleaseMs, std::memory_order_relaxed);
        bus.speakerDelayEnabled.store(cfg.speakerDelayEnabled, std::memory_order_relaxed);
        bus.speakerDelayLink.store(cfg.speakerDelayLink, std::memory_order_relaxed);
        bus.speakerDelayMsL.store(cfg.speakerDelayMsL, std::memory_order_relaxed);
        bus.speakerDelayMsR.store(cfg.speakerDelayMsR, std::memory_order_relaxed);

        for (size_t s = 0; s < 8; ++s) {
            if (s < cfg.fxRack.size()) {
                writeFxSlotControl(bus.fxSlots[s], cfg.fxRack[s], sampleRate_, ownedFxNodes_);
            } else {
                AudioStripFxConfig emptyFx{};
                writeFxSlotControl(bus.fxSlots[s], emptyFx, sampleRate_, ownedFxNodes_);
            }
        }
        bus.configured.store(true, std::memory_order_relaxed);
        bus.dirty.store(true, std::memory_order_release);
    }

    static void syncBusChain(BusDspChain& bus) noexcept {
        if (!bus.dirty.exchange(false, std::memory_order_acquire)) return;
        const bool isStereo = !bus.isAuxBus ||
            bus.format.load(std::memory_order_relaxed) == static_cast<uint8_t>(AudioStripFormat::Stereo);
        bus.activeChannels.store(isStereo ? 2u : 1u, std::memory_order_relaxed);

        const bool hpfEn = bus.hpfEnabled.load(std::memory_order_relaxed);
        const float hpfFreq = bus.hpfFrequency.load(std::memory_order_relaxed);
        bus.hpf->setBand(0, audiocore::FilterType::HighPass, hpfFreq, 0.707, 0.0);
        bus.hpf->setBypassed(!hpfEn);

        const bool peqBypassed = bus.peqBypassed.load(std::memory_order_relaxed);
        const bool geqMode = bus.geqMode.load(std::memory_order_relaxed);
        bus.peq->setBypassed(geqMode || peqBypassed);
        for (uint32_t b = 0; b < 4; ++b) {
            auto type = static_cast<audiocore::FilterType>(bus.eqType[b].load(std::memory_order_relaxed));
            float freq = bus.eqFrequency[b].load(std::memory_order_relaxed);
            float q = bus.eqQ[b].load(std::memory_order_relaxed);
            float gain = bus.eqGain[b].load(std::memory_order_relaxed);
            bool en = bus.eqBandEnabled[b].load(std::memory_order_relaxed);
            bus.peq->setBand(b, type, freq, q, gain);
            bus.peq->setBandEnabled(b, en);
        }

        const bool geqBypassed = bus.geqBypassed.load(std::memory_order_relaxed);
        bus.geq->setBypassed(!geqMode || geqBypassed);
        for (size_t b = 0; b < 31; ++b) {
            bus.geq->setBandGainDb(b, bus.geqGain[b].load(std::memory_order_relaxed));
        }

        const bool compEn = bus.compressorEnabled.load(std::memory_order_relaxed);
        bus.compressor->setBypassed(!compEn);
        bus.compressor->setThresholdDb(bus.compressorThresholdDb.load(std::memory_order_relaxed));
        bus.compressor->setRatio(bus.compressorRatio.load(std::memory_order_relaxed));
        bus.compressor->setAttackMs(bus.compressorAttackMs.load(std::memory_order_relaxed));
        bus.compressor->setReleaseMs(bus.compressorReleaseMs.load(std::memory_order_relaxed));
        bus.compressor->setKneeDb(bus.compressorKneeDb.load(std::memory_order_relaxed));
        bus.compressor->setMakeupGainDb(bus.compressorMakeupDb.load(std::memory_order_relaxed));

        const bool gateEn = bus.gateEnabled.load(std::memory_order_relaxed);
        bus.gate->setBypassed(!gateEn);
        bus.gate->setThresholdDb(bus.gateThresholdDb.load(std::memory_order_relaxed));
        bus.gate->setRangeDb(bus.gateRangeDb.load(std::memory_order_relaxed));
        bus.gate->setAttackMs(bus.gateAttackMs.load(std::memory_order_relaxed));
        bus.gate->setHoldMs(bus.gateHoldMs.load(std::memory_order_relaxed));
        bus.gate->setReleaseMs(bus.gateReleaseMs.load(std::memory_order_relaxed));

        const bool limEn = bus.limiterEnabled.load(std::memory_order_relaxed);
        bus.limiter->setBypassed(!limEn);
        bus.limiter->setCeilingDb(bus.limiterCeilingDb.load(std::memory_order_relaxed));
        bus.limiter->setReleaseMs(bus.limiterReleaseMs.load(std::memory_order_relaxed));

        if (bus.alignmentDelay) {
            bus.alignmentDelay->setBypassed(!bus.speakerDelayEnabled.load(std::memory_order_relaxed));
            bus.alignmentDelay->setStereoLink(bus.speakerDelayLink.load(std::memory_order_relaxed));
            bus.alignmentDelay->setChannelDelayMs(0, bus.speakerDelayMsL.load(std::memory_order_relaxed));
            bus.alignmentDelay->setChannelDelayMs(1, bus.speakerDelayMsR.load(std::memory_order_relaxed));
        }

        for (size_t s = 0; s < 8; ++s) {
            applyFxSlotControl(bus.fxSlots[s], bus.isAuxBus);
        }
    }

    static StripAudioTelemetry readBusTelemetry(const BusDspChain& bus) noexcept {
        StripAudioTelemetry item;
        item.logicalId = bus.logicalId;
        item.channelCount = bus.activeChannels.load(std::memory_order_relaxed);
        for (size_t ch = 0; ch < 2; ++ch) {
            item.inputPeak[ch] = bus.inputPeak[ch].load(std::memory_order_relaxed);
            item.inputRms[ch] = bus.inputRms[ch].load(std::memory_order_relaxed);
            item.fxInputPeak[ch] = bus.fxInputPeak[ch].load(std::memory_order_relaxed);
            item.fxInputRms[ch] = bus.fxInputRms[ch].load(std::memory_order_relaxed);
            item.fxOutputPeak[ch] = bus.fxOutputPeak[ch].load(std::memory_order_relaxed);
            item.fxOutputRms[ch] = bus.fxOutputRms[ch].load(std::memory_order_relaxed);
            item.preLimiterPeak[ch] = bus.preLimiterPeak[ch].load(std::memory_order_relaxed);
            item.outputPeak[ch] = bus.outputPeak[ch].load(std::memory_order_relaxed);
            item.outputRms[ch] = bus.outputRms[ch].load(std::memory_order_relaxed);
            item.postFaderPeak[ch] = item.outputPeak[ch];
        }
        item.gainReductionDb = bus.compGrDb.load(std::memory_order_relaxed);
        item.compressorDetectorDb = bus.compDetectorDb.load(std::memory_order_relaxed);
        item.compressorOutputDb = bus.compOutputDb.load(std::memory_order_relaxed);
        item.compressorOutputDbR = bus.compOutputDbR.load(std::memory_order_relaxed);
        item.gateGainReductionDb = bus.gateGrDb.load(std::memory_order_relaxed);
        item.gateDetectorDb = bus.gateDetectorDb.load(std::memory_order_relaxed);
        item.gateState = bus.gateState.load(std::memory_order_relaxed);
        item.limiterGainReductionDb = bus.limiterGrDb.load(std::memory_order_relaxed);
        populateAfsTelemetryFromSlots(bus.fxSlots, item);
        populateFxSlotTelemetry(bus.fxSlots, item);
        return item;
    }

    std::vector<AudioStripConfig> strips_;
    std::vector<std::unique_ptr<StripState>> liveStates_;
    std::shared_ptr<StereoDemoDspNode> masterProcessor_;
    double sampleRate_;
    std::mutex controlWriteMutex_;
    std::vector<std::unique_ptr<audiocore::AudioNode>> ownedFxNodes_;
    audiocore::AudioBuffer mixed_;
    audiocore::AudioBuffer processed_;
    audiocore::AudioBuffer meteredMain_;
    uint32_t renderChannels_;
    std::shared_ptr<audiocore::MeterNode> outputMeter_;
    std::array<std::unique_ptr<BusDspChain>, 4> auxBuses_;
    std::unique_ptr<BusDspChain> masterBus_;
    std::atomic<uint32_t> tapTargetId_{0};
    std::atomic<uint8_t> tapSource_{1};
    std::atomic<AnalyzerRingBuffer*> tapRingBuffer_{nullptr};
};
}

struct StripInsertDspNode::Impl {
    Impl(const std::vector<AudioStripConfig>& strips, double rate, uint32_t frames)
        : node(strips, rate, frames) {}
    ConsoleStripFxNode node;
};

StripInsertDspNode::StripInsertDspNode(const std::vector<AudioStripConfig>& strips,
                                       double sampleRate, uint32_t maxFrames)
    : impl_(std::make_unique<Impl>(strips, sampleRate, maxFrames)) {}
StripInsertDspNode::~StripInsertDspNode() = default;
void StripInsertDspNode::process(const audiocore::AudioBuffer& input,
                                 audiocore::AudioBuffer& output, uint32_t frames) {
    impl_->node.process(input, output, frames);
}
void StripInsertDspNode::reset() { impl_->node.reset(); }
std::vector<StripAudioTelemetry> StripInsertDspNode::telemetry() const { return impl_->node.telemetry(); }
void StripInsertDspNode::updateStripDsp(const AudioStripConfig& config) noexcept {
    impl_->node.updateStripDsp(config);
}
uint32_t StripInsertDspNode::stripBufferChannels(size_t index) const noexcept {
    return impl_->node.stripBufferChannels(index);
}
void StripInsertDspNode::setAnalyzerTap(uint32_t channelId, uint8_t tapSource, AnalyzerRingBuffer* ring) noexcept {
    impl_->node.setAnalyzerTap(channelId, tapSource, ring);
}

struct StripMainMixDspNode::Impl {
    Impl(const std::vector<AudioStripConfig>& strips,
         std::shared_ptr<StereoDemoDspNode> processor, uint32_t renderChannels,
         uint32_t frames, double sampleRate)
        : node(strips, std::move(processor), renderChannels, frames, sampleRate) {}
    ConsoleMainOutputNode node;
};

StripMainMixDspNode::StripMainMixDspNode(const std::vector<AudioStripConfig>& strips,
                                         std::shared_ptr<StereoDemoDspNode> masterProcessor,
                                         uint32_t renderChannels, uint32_t maxFrames,
                                         double sampleRate)
    : impl_(std::make_unique<Impl>(strips, std::move(masterProcessor), renderChannels, maxFrames, sampleRate)) {}
StripMainMixDspNode::~StripMainMixDspNode() = default;
void StripMainMixDspNode::process(const audiocore::AudioBuffer& input,
                                  audiocore::AudioBuffer& output, uint32_t frames) {
    impl_->node.process(input, output, frames);
}
void StripMainMixDspNode::reset() { impl_->node.reset(); }
std::shared_ptr<audiocore::MeterNode> StripMainMixDspNode::outputMeter() const {
    return impl_->node.outputMeter();
}
void StripMainMixDspNode::updateStripByLogicalId(uint32_t logicalId, float gainDb, float pan, bool muted, bool solo) noexcept {
    impl_->node.updateStripByLogicalId(logicalId, gainDb, pan, muted, solo);
}
void StripMainMixDspNode::updateStripConfig(const AudioStripConfig& config) noexcept {
    impl_->node.updateStripConfig(config);
}
void StripMainMixDspNode::updateAuxBusConfig(size_t auxIndex, const AudioBusConfig& config) noexcept {
    impl_->node.updateAuxBusConfig(auxIndex, config);
}
void StripMainMixDspNode::updateMasterBusConfig(const AudioBusConfig& config) noexcept {
    impl_->node.updateMasterBusConfig(config);
}
StripAudioTelemetry StripMainMixDspNode::masterTelemetry() const noexcept {
    return impl_->node.masterTelemetry();
}
std::array<StripAudioTelemetry, 4> StripMainMixDspNode::auxTelemetry() const noexcept {
    return impl_->node.auxTelemetry();
}
std::array<float, 2> StripMainMixDspNode::stripPostFaderPeak(uint32_t logicalId) const noexcept {
    return impl_->node.stripPostFaderPeak(logicalId);
}
void StripMainMixDspNode::setAnalyzerTap(uint32_t channelId, uint8_t tapSource, AnalyzerRingBuffer* ring) noexcept {
    impl_->node.setAnalyzerTap(channelId, tapSource, ring);
}

namespace {
bool validateDspChainConfig(const AudioStripConfig& strip) {
    if (!std::isfinite(strip.gainDb) || strip.gainDb < -60.0f || strip.gainDb > 12.0f ||
        !std::isfinite(strip.returnGainDb) || strip.returnGainDb < -60.0f || strip.returnGainDb > 12.0f ||
        !std::isfinite(strip.pan) || strip.pan < -1.0f || strip.pan > 1.0f ||
        !std::isfinite(strip.hpfFrequency) || strip.hpfFrequency < 20.0f || strip.hpfFrequency > 20000.0f ||
        !std::isfinite(strip.compressorThresholdDb) || strip.compressorThresholdDb < -80.0f || strip.compressorThresholdDb > 12.0f ||
        !std::isfinite(strip.compressorRatio) || strip.compressorRatio < 1.0f || strip.compressorRatio > 20.0f ||
        !std::isfinite(strip.compressorAttackMs) || strip.compressorAttackMs < 0.1f || strip.compressorAttackMs > 200.0f ||
        !std::isfinite(strip.compressorReleaseMs) || strip.compressorReleaseMs < 1.0f || strip.compressorReleaseMs > 2000.0f ||
        !std::isfinite(strip.compressorKneeDb) || strip.compressorKneeDb < 0.0f || strip.compressorKneeDb > 24.0f ||
        !std::isfinite(strip.compressorMakeupDb) || strip.compressorMakeupDb < -24.0f || strip.compressorMakeupDb > 24.0f ||
        !std::isfinite(strip.gateThresholdDb) || strip.gateThresholdDb < -80.0f || strip.gateThresholdDb > 0.0f ||
        !std::isfinite(strip.gateRangeDb) || strip.gateRangeDb < -80.0f || strip.gateRangeDb > 0.0f ||
        !std::isfinite(strip.gateAttackMs) || strip.gateAttackMs < 0.1f || strip.gateAttackMs > 200.0f ||
        !std::isfinite(strip.gateHoldMs) || strip.gateHoldMs < 0.0f || strip.gateHoldMs > 1000.0f ||
        !std::isfinite(strip.gateReleaseMs) || strip.gateReleaseMs < 1.0f || strip.gateReleaseMs > 2000.0f ||
        !std::isfinite(strip.limiterCeilingDb) || strip.limiterCeilingDb < -24.0f || strip.limiterCeilingDb > 0.0f ||
        !std::isfinite(strip.limiterReleaseMs) || strip.limiterReleaseMs < 1.0f || strip.limiterReleaseMs > 2000.0f ||
        strip.fxRack.size() > 8 ||
        static_cast<uint8_t>(strip.outputRoute) > static_cast<uint8_t>(AudioOutputRoute::Omni)) {
        return false;
    }
    for (size_t a = 0; a < 4; ++a) {
        if (!std::isfinite(strip.auxSendLevel[a]) || strip.auxSendLevel[a] < 0.0f || strip.auxSendLevel[a] > 2.0f)
            return false;
    }
    for (size_t band = 0; band < strip.eqFrequency.size(); ++band) {
        if (!std::isfinite(strip.eqFrequency[band]) || strip.eqFrequency[band] < 10.0f || strip.eqFrequency[band] > 24000.0f ||
            !std::isfinite(strip.eqGain[band]) || strip.eqGain[band] < -24.0f || strip.eqGain[band] > 24.0f ||
            !std::isfinite(strip.eqQ[band]) || strip.eqQ[band] < 0.05f || strip.eqQ[band] > 30.0f ||
            strip.eqType[band] > 5) return false;
    }
    for (const auto& effect : strip.fxRack) {
        if (!isSupportedFx(effect.uri) || effect.uri.empty()) return false;
        for (const auto& parameter : effect.parameters)
            if (parameter.id.empty() || !std::isfinite(parameter.value)) return false;
    }
    return true;
}
}

struct AudioService::Impl {
    Impl() : processor(std::make_shared<StereoDemoDspNode>()) {}

    void setError(const std::string& message) {
        {
            std::lock_guard<std::mutex> lock(snapshotMutex);
            published.error = message;
        }
        state.store(AudioServiceState::Error, std::memory_order_release);
    }

    void publishDevices() {
        std::vector<AudioDeviceChoice> capture;
        std::vector<AudioDeviceChoice> render;
        try {
            capture = toChoices(audiocore::PlatformBackend::enumerateDevices(true));
            render = toChoices(audiocore::PlatformBackend::enumerateDevices(false));
        } catch (...) {
            // A failed enumeration remains visible as an empty device list.
        }
        std::lock_guard<std::mutex> lock(snapshotMutex);
        published.captureDevices = std::move(capture);
        published.renderDevices = std::move(render);
        published.devicesReady = true;
    }

    void publishTelemetry(audiocore::PlatformBackend& backend,
                          const StripInsertDspNode& stripFx,
                          const StripMainMixDspNode& mainOutput) {
        std::shared_ptr<StereoDemoDspNode> activeProcessor;
        {
            std::lock_guard<std::mutex> lock(processorMutex);
            activeProcessor = processor;
        }
        AudioServiceSnapshot current;
        current.state = AudioServiceState::Streaming;
        current.actualSampleRate = backend.getSampleRate();
        current.actualBufferFrames = backend.getActualBufferSizeFrames();
        current.captureChannels = backend.getCaptureChannels();
        current.renderChannels = backend.getChannels();
        current.actualFormat = "Render: " + backend.getFormatDescription() +
            " / Capture: " + backend.getCaptureFormatDescription();
        current.actualCaptureDevice = backend.getActiveCaptureDeviceName();
        current.actualRenderDevice = backend.getActivePlaybackDeviceName();
        current.captureDeviceId = backend.getActiveCaptureDeviceId();
        current.renderDeviceId = backend.getActivePlaybackDeviceId();
        const auto metrics = backend.getRealtimeMetrics();
        current.dspTimeUs = metrics.averageDspTimeUs;
        current.underruns = metrics.renderUnderruns;
        current.strips = stripFx.telemetry();
        for (auto& strip : current.strips) {
            strip.postFaderPeak = mainOutput.stripPostFaderPeak(strip.logicalId);
        }
        current.auxBuses = mainOutput.auxTelemetry();
        const auto masterTel = mainOutput.masterTelemetry();
        auto outputMeter = mainOutput.outputMeter();
        current.inputPeak = masterTel.inputPeak;
        current.inputRms = masterTel.inputRms;
        current.masterFxInputPeak = masterTel.fxInputPeak;
        current.masterFxInputRms = masterTel.fxInputRms;
        current.masterFxOutputPeak = masterTel.fxOutputPeak;
        current.masterFxOutputRms = masterTel.fxOutputRms;
        current.preLimiterPeak = masterTel.preLimiterPeak;
        current.outputPeak = {outputMeter->getPeak(0), outputMeter->getPeak(1)};
        current.outputRms = {outputMeter->getRms(0), outputMeter->getRms(1)};
        current.compressorGainReductionDb = masterTel.gainReductionDb;
        current.limiterGainReductionDb = masterTel.limiterGainReductionDb;
        current.masterCompressorDetectorDb = masterTel.compressorDetectorDb;
        current.masterCompressorOutputDb = masterTel.compressorOutputDb;
        current.masterCompressorOutputDbR = masterTel.compressorOutputDbR;
        current.masterGateDetectorDb = masterTel.gateDetectorDb;
        current.masterGateGainReductionDb = masterTel.gateGainReductionDb;
        current.masterGateState = masterTel.gateState;
        current.masterAfsAssigned = masterTel.afsAssigned;
        current.masterAfsState = masterTel.afsState;
        current.masterAfsActiveNotchCount = masterTel.afsActiveNotchCount;
        current.masterAfsMaxNotches = masterTel.afsMaxNotches;
        current.masterAfsNotches = masterTel.afsNotches;
        current.controls = activeProcessor->requestedControls();
        std::lock_guard<std::mutex> lock(snapshotMutex);
        current.captureDevices = std::move(published.captureDevices);
        current.renderDevices = std::move(published.renderDevices);
        current.devicesReady = published.devicesReady;
        current.error = published.error;
        published = std::move(current);
    }

    bool prepareAndRun() {
        std::string captureId;
        std::string renderId;
        std::vector<AudioStripConfig> strips;
        std::array<AudioBusConfig, 4> auxConfigs{};
        bool hasAuxConfigs = false;
        AudioBusConfig masterCfg{};
        bool hasMasterCfg = false;
        uint32_t reqRate = 48000;
        uint32_t reqFrames = 128;
        {
            std::lock_guard<std::mutex> lock(configMutex);
            captureId = captureDeviceId;
            renderId = renderDeviceId;
            strips = consoleStrips;
            auxConfigs = consoleAuxBuses;
            hasAuxConfigs = auxBusesConfigured;
            masterCfg = consoleMasterBus;
            hasMasterCfg = masterBusConfigured;
            reqRate = requestedSampleRate > 0 ? requestedSampleRate : 48000;
            reqFrames = requestedBufferFrames > 0 ? requestedBufferFrames : 128;
        }
        if (strips.empty()) {
            setError("Add a strip, assign a physical capture input, and route it to Main L/R before starting Real Audio.");
            return false;
        }
        if (std::none_of(strips.begin(), strips.end(), [](const auto& strip) {
                const bool hasAuxSend = std::any_of(strip.auxSendLevel.begin(), strip.auxSendLevel.end(),
                                                    [](float lvl) { return lvl > 0.0f; });
                return strip.outputRoute != AudioOutputRoute::None || hasAuxSend;
            })) {
            setError("Assign at least one strip to Main L/R, an AUX bus, or an Omni output.");
            return false;
        }
        if (strips.size() > 32) {
            setError("The live mixer supports at most 32 strips.");
            return false;
        }
        std::vector<uint32_t> captureMap;
        captureMap.reserve(strips.size() * 2);
        std::vector<uint32_t> usedInputs;
        usedInputs.reserve(strips.size() * 2);
        std::vector<uint32_t> logicalIds;
        logicalIds.reserve(strips.size());
        for (const auto& strip : strips) {
            if (strip.logicalId == 0 ||
                std::find(logicalIds.begin(), logicalIds.end(), strip.logicalId) != logicalIds.end()) {
                setError("Live strip IDs must be nonzero and unique.");
                return false;
            }
            if (std::find(usedInputs.begin(), usedInputs.end(), strip.captureChannel) != usedInputs.end()) {
                setError("Each live strip must use a different physical capture channel.");
                return false;
            }
            usedInputs.push_back(strip.captureChannel);
            if (strip.format == AudioStripFormat::Stereo) {
                if (strip.captureChannelRight == strip.captureChannel ||
                    std::find(usedInputs.begin(), usedInputs.end(), strip.captureChannelRight) != usedInputs.end()) {
                    setError("Each stereo strip must use two distinct, unassigned physical capture channels.");
                    return false;
                }
                usedInputs.push_back(strip.captureChannelRight);
                captureMap.push_back(strip.captureChannel);
                captureMap.push_back(strip.captureChannelRight);
            } else {
                captureMap.push_back(strip.captureChannel);
                captureMap.push_back(strip.captureChannel);
            }
            for (const auto& effect : strip.fxRack) {
                if (!isSupportedFx(effect.uri)) {
                    setError("A selected strip effect does not have a live DSP processor yet.");
                    return false;
                }
            }
            logicalIds.push_back(strip.logicalId);
        }
        const uint32_t engineChannels = static_cast<uint32_t>(strips.size() * 2);

        auto backend = std::make_shared<audiocore::PlatformBackend>();
#if defined(_WIN32)
        backend->setAudioSourceType(audiocore::AudioSourceType::AudioInterface);
        backend->setStreamMode(audiocore::WasapiStreamMode::Duplex);
        backend->setShareMode(audiocore::WasapiShareMode::Shared);
        backend->setEventDriven(true);
        backend->setSafeFeedbackGuard(true, 0.05f);
#else
        backend->setSafeFeedbackGuard(false, 1.0f);
#endif
        backend->setRequestedSampleRate(static_cast<double>(reqRate));
        backend->setRequestedBlockSize(reqFrames);
        if (!captureId.empty() && !backend->setCaptureDevice(captureId)) {
            setError("Capture device could not be selected.");
            return false;
        }
        if (!renderId.empty() && !backend->setPlaybackDevice(renderId)) {
            setError("Render device could not be selected.");
            return false;
        }
        if (!backend->setCaptureChannelMap(captureMap)) {
            setError("Could not set physical input channel mapping.");
            return false;
        }

        if (!backend->initialize(static_cast<double>(reqRate), reqFrames, engineChannels)) {
            setError(backend->getLastErrorMessage().empty()
                ? "Audio backend initialization failed." : backend->getLastErrorMessage());
            return false;
        }
        const uint32_t renderChannels = backend->getChannels();
        if (renderChannels < 2) {
            setError("The selected render endpoint exposes fewer than two channels; Main L/R cannot be rendered.");
            backend->stop();
            return false;
        }
        if (backend->getCaptureChannels() == 0) {
            setError("The selected capture endpoint exposes no audio channels.");
            backend->stop();
            return false;
        }
        for (const auto channel : captureMap) {
            if (channel >= backend->getCaptureChannels()) {
                setError("A strip is assigned to a capture channel that this endpoint does not expose.");
                backend->stop();
                return false;
            }
        }
        for (const auto& strip : strips) {
            uint32_t outputIndex = 0;
            switch (strip.outputRoute) {
                case AudioOutputRoute::None: continue;
                case AudioOutputRoute::MainLR: outputIndex = 0; break;
                case AudioOutputRoute::Aux:
                    outputIndex = 2 + strip.outputRouteIndex;
                    break;
                case AudioOutputRoute::Omni: outputIndex = strip.outputRouteIndex; break;
            }
            const uint32_t needed = strip.outputRoute == AudioOutputRoute::MainLR ? 2 : 1;
            if (outputIndex >= renderChannels || needed > renderChannels - outputIndex ||
                (strip.outputRoute == AudioOutputRoute::Omni && outputIndex < 2)) {
                setError(strip.name + " is routed to an output the selected render endpoint does not expose.");
                backend->stop();
                return false;
            }
        }
        if (stopRequested.load(std::memory_order_acquire)) {
            backend->stop();
            state.store(AudioServiceState::Stopped, std::memory_order_release);
            return false;
        }

        const uint32_t activeBlockSize = backend->getBlockSize() > 0 ? backend->getBlockSize() : reqFrames;
        const double activeRate = backend->getSampleRate() > 0.0 ? backend->getSampleRate() : static_cast<double>(reqRate);

        engine.setBackend(backend);
        if (!engine.configure(activeRate, activeBlockSize, engineChannels)) {
            backend->stop();
            engine.setBackend(nullptr);
            setError("Audio engine configuration failed.");
            return false;
        }
        std::shared_ptr<StereoDemoDspNode> activeProcessor;
        {
            std::lock_guard<std::mutex> lock(processorMutex);
            const auto controls = processor->requestedControls();
            activeProcessor = std::make_shared<StereoDemoDspNode>(activeRate, activeBlockSize);
            activeProcessor->setGainDb(controls.gainDb);
            activeProcessor->setPan(controls.pan);
            activeProcessor->setDspBypass(controls.bypass);
            activeProcessor->setPeqEnabled(controls.peqEnabled);
            activeProcessor->setCompressorEnabled(controls.compressorEnabled);
            processor = activeProcessor;
        }
        auto stripFx = std::make_shared<StripInsertDspNode>(strips, activeRate, activeBlockSize);
        auto mainOutput = std::make_shared<StripMainMixDspNode>(
            strips, activeProcessor, renderChannels, activeBlockSize, activeRate);
        if (hasAuxConfigs) {
            for (size_t a = 0; a < 4; ++a) {
                mainOutput->updateAuxBusConfig(a, auxConfigs[a]);
            }
        }
        if (hasMasterCfg) {
            mainOutput->updateMasterBusConfig(masterCfg);
        }
        {
            std::lock_guard<std::mutex> lock(stripFxMutex);
            activeStripFx = stripFx;
        }
        {
            std::lock_guard<std::mutex> lock(mainOutputMutex);
            activeMainOutput = mainOutput;
        }
        stripFx->setAnalyzerTap(tapChannelId_.load(std::memory_order_relaxed),
                                tapSource_.load(std::memory_order_relaxed),
                                &ringBuffer_);
        mainOutput->setAnalyzerTap(tapChannelId_.load(std::memory_order_relaxed),
                                   tapSource_.load(std::memory_order_relaxed),
                                   &ringBuffer_);
        auto& graph = engine.getGraph();
        graph.clearNodes();
        graph.addNode(stripFx);
        graph.addNode(mainOutput);

        if (stopRequested.load(std::memory_order_acquire)) {
            backend->stop();
            engine.setBackend(nullptr);
            state.store(AudioServiceState::Stopped, std::memory_order_release);
            return false;
        }
        if (!engine.start()) {
            backend->stop();
            engine.setBackend(nullptr);
            setError(backend->getLastErrorMessage().empty()
                ? "Audio stream start failed." : backend->getLastErrorMessage());
            return false;
        }

        state.store(AudioServiceState::Streaming, std::memory_order_release);
        while (!shutdownRequested.load(std::memory_order_acquire) &&
               !stopRequested.load(std::memory_order_acquire) && backend->isRunning()) {
            publishTelemetry(*backend, *stripFx, *mainOutput);
            std::unique_lock<std::mutex> lock(wakeMutex);
            wake.wait_for(lock, std::chrono::milliseconds(40), [&] {
                return shutdownRequested.load(std::memory_order_acquire) ||
                       stopRequested.load(std::memory_order_acquire);
            });
        }

        {
            std::lock_guard<std::mutex> lock(stripFxMutex);
            activeStripFx.reset();
        }
        {
            std::lock_guard<std::mutex> lock(mainOutputMutex);
            activeMainOutput.reset();
        }

        const bool explicitStop = stopRequested.load(std::memory_order_acquire) ||
                                  shutdownRequested.load(std::memory_order_acquire);
        if (!explicitStop && !backend->isRunning()) {
            const auto error = backend->getLastErrorMessage();
            setError(error.empty() ? "Audio stream stopped unexpectedly." : error);
        } else {
            state.store(AudioServiceState::Stopping, std::memory_order_release);
        }
        backend->stop();
        engine.stop();
        engine.setBackend(nullptr);
        if (explicitStop) state.store(AudioServiceState::Stopped, std::memory_order_release);
        return true;
    }

    void run() {
        for (;;) {
            bool doRefresh = false;
            bool doStart = false;
            {
                std::unique_lock<std::mutex> lock(wakeMutex);
                wake.wait(lock, [&] {
                    return shutdownRequested.load(std::memory_order_acquire) || refreshRequested || startRequested;
                });
                if (shutdownRequested.load(std::memory_order_acquire)) break;
                doRefresh = std::exchange(refreshRequested, false);
                doStart = std::exchange(startRequested, false);
            }
            if (doRefresh) publishDevices();
            if (doStart) {
                bool ran = false;
                try {
                    ran = prepareAndRun();
                } catch (const std::exception& error) {
                    setError(std::string("Audio service failure: ") + error.what());
                } catch (...) {
                    setError("Audio service failed with an unknown error.");
                }
                if (!ran && state.load(std::memory_order_acquire) == AudioServiceState::Initializing)
                    state.store(AudioServiceState::Stopped, std::memory_order_release);
            }
        }
        if (state.load(std::memory_order_acquire) == AudioServiceState::Streaming ||
            state.load(std::memory_order_acquire) == AudioServiceState::Stopping) {
            state.store(AudioServiceState::Stopping, std::memory_order_release);
        }
    }

    std::atomic<AudioMode> mode{AudioMode::Simulation};
    std::atomic<AudioServiceState> state{AudioServiceState::Stopped};
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> shutdownRequested{false};
    std::shared_ptr<StereoDemoDspNode> processor;
    mutable std::mutex processorMutex;
    audiocore::AudioEngine engine;
    std::thread worker;
    mutable std::mutex snapshotMutex;
    AudioServiceSnapshot published{};
    std::mutex configMutex;
    std::string captureDeviceId;
    std::string renderDeviceId;
    std::vector<AudioStripConfig> consoleStrips;
    std::array<AudioBusConfig, 4> consoleAuxBuses{};
    bool auxBusesConfigured = false;
    AudioBusConfig consoleMasterBus{};
    bool masterBusConfigured = false;
    std::shared_ptr<StripInsertDspNode> activeStripFx;
    mutable std::mutex stripFxMutex;
    std::shared_ptr<StripMainMixDspNode> activeMainOutput;
    mutable std::mutex mainOutputMutex;
    uint32_t requestedSampleRate{48000};
    uint32_t requestedBufferFrames{128};
    std::mutex wakeMutex;
    std::condition_variable wake;
    bool refreshRequested = false;
    bool startRequested = false;
    AnalyzerRingBuffer ringBuffer_;
    std::atomic<uint32_t> tapChannelId_{0};
    std::atomic<uint8_t> tapSource_{1};
};

AudioService::AudioService() : impl_(std::make_unique<Impl>()) {
    impl_->worker = std::thread([state = impl_.get()] { state->run(); });
}

AudioService::~AudioService() {
    impl_->shutdownRequested.store(true, std::memory_order_release);
    impl_->stopRequested.store(true, std::memory_order_release);
    impl_->wake.notify_all();
    if (impl_->worker.joinable()) impl_->worker.join();
}

bool AudioService::selectMode(AudioMode mode) noexcept {
    const auto state = impl_->state.load(std::memory_order_acquire);
    if (state != AudioServiceState::Stopped && state != AudioServiceState::Error) return false;
    impl_->mode.store(mode, std::memory_order_release);
    return true;
}

bool AudioService::selectCaptureDevice(const std::string& deviceId) {
    const auto state = impl_->state.load(std::memory_order_acquire);
    if (state != AudioServiceState::Stopped && state != AudioServiceState::Error) return false;
    {
        std::lock_guard<std::mutex> lock(impl_->configMutex);
        impl_->captureDeviceId = deviceId;
    }
    {
        std::lock_guard<std::mutex> lock(impl_->snapshotMutex);
        impl_->published.captureDeviceId = deviceId;
    }
    return true;
}

bool AudioService::selectRenderDevice(const std::string& deviceId) {
    const auto state = impl_->state.load(std::memory_order_acquire);
    if (state != AudioServiceState::Stopped && state != AudioServiceState::Error) return false;
    {
        std::lock_guard<std::mutex> lock(impl_->configMutex);
        impl_->renderDeviceId = deviceId;
    }
    {
        std::lock_guard<std::mutex> lock(impl_->snapshotMutex);
        impl_->published.renderDeviceId = deviceId;
    }
    return true;
}

bool AudioService::setConsoleConfiguration(const std::vector<AudioStripConfig>& strips) {
    const auto state = impl_->state.load(std::memory_order_acquire);
    if (state != AudioServiceState::Stopped && state != AudioServiceState::Error) return false;
    if (strips.size() > 32) return false;
    std::vector<uint32_t> ids;
    std::vector<uint32_t> inputs;
    for (const auto& strip : strips) {
        if (strip.logicalId == 0 ||
            std::find(ids.begin(), ids.end(), strip.logicalId) != ids.end() ||
            std::find(inputs.begin(), inputs.end(), strip.captureChannel) != inputs.end() ||
            !validateDspChainConfig(strip)) {
            return false;
        }
        inputs.push_back(strip.captureChannel);
        if (strip.format == AudioStripFormat::Stereo) {
            if (strip.captureChannelRight == strip.captureChannel ||
                std::find(inputs.begin(), inputs.end(), strip.captureChannelRight) != inputs.end()) {
                return false;
            }
            inputs.push_back(strip.captureChannelRight);
        }
        ids.push_back(strip.logicalId);
    }
    std::lock_guard<std::mutex> lock(impl_->configMutex);
    impl_->consoleStrips = strips;
    return true;
}

bool AudioService::setAuxBusConfigurations(const std::array<AudioBusConfig, 4>& auxBuses) {
    for (const auto& bus : auxBuses) {
        if (!validateDspChainConfig(bus)) return false;
    }
    {
        std::lock_guard<std::mutex> lock(impl_->configMutex);
        impl_->consoleAuxBuses = auxBuses;
        impl_->auxBusesConfigured = true;
    }
    for (size_t a = 0; a < 4; ++a) {
        updateLiveAuxBusDsp(a, auxBuses[a]);
    }
    return true;
}

bool AudioService::setMasterBusConfiguration(const AudioBusConfig& masterBus) {
    if (!validateDspChainConfig(masterBus)) return false;
    {
        std::lock_guard<std::mutex> lock(impl_->configMutex);
        impl_->consoleMasterBus = masterBus;
        impl_->masterBusConfigured = true;
    }
    updateLiveMasterBusDsp(masterBus);
    return true;
}

void AudioService::refreshDevices() {
    {
        std::lock_guard<std::mutex> lock(impl_->wakeMutex);
        impl_->refreshRequested = true;
    }
    impl_->wake.notify_one();
}

bool AudioService::startRealAudio() {
    if (impl_->mode.load(std::memory_order_acquire) != AudioMode::RealAudio) return false;
    auto state = impl_->state.load(std::memory_order_acquire);
    while (state == AudioServiceState::Stopped || state == AudioServiceState::Error) {
        if (impl_->state.compare_exchange_weak(state, AudioServiceState::Initializing,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            impl_->stopRequested.store(false, std::memory_order_release);
            {
                std::lock_guard<std::mutex> lock(impl_->snapshotMutex);
                impl_->published.error.clear();
            }
            {
                std::lock_guard<std::mutex> lock(impl_->wakeMutex);
                impl_->startRequested = true;
            }
            impl_->wake.notify_one();
            return true;
        }
    }
    return false;
}

void AudioService::stop() {
    auto state = impl_->state.load(std::memory_order_acquire);
    if (state == AudioServiceState::Streaming || state == AudioServiceState::Initializing) {
        impl_->state.store(AudioServiceState::Stopping, std::memory_order_release);
        impl_->stopRequested.store(true, std::memory_order_release);
        impl_->wake.notify_all();
    }
}

bool AudioService::setGainDb(float value) noexcept {
    if (impl_->mode.load(std::memory_order_acquire) != AudioMode::RealAudio) return false;
    std::lock_guard<std::mutex> lock(impl_->processorMutex);
    return impl_->processor->setGainDb(value);
}
bool AudioService::setPan(float value) noexcept {
    if (impl_->mode.load(std::memory_order_acquire) != AudioMode::RealAudio) return false;
    std::lock_guard<std::mutex> lock(impl_->processorMutex);
    return impl_->processor->setPan(value);
}
bool AudioService::setDspBypass(bool enabled) noexcept {
    if (impl_->mode.load(std::memory_order_acquire) != AudioMode::RealAudio) return false;
    std::lock_guard<std::mutex> lock(impl_->processorMutex);
    return impl_->processor->setDspBypass(enabled);
}
bool AudioService::setPeqEnabled(bool enabled) noexcept {
    if (impl_->mode.load(std::memory_order_acquire) != AudioMode::RealAudio) return false;
    std::lock_guard<std::mutex> lock(impl_->processorMutex);
    return impl_->processor->setPeqEnabled(enabled);
}
bool AudioService::setCompressorEnabled(bool enabled) noexcept {
    if (impl_->mode.load(std::memory_order_acquire) != AudioMode::RealAudio) return false;
    std::lock_guard<std::mutex> lock(impl_->processorMutex);
    return impl_->processor->setCompressorEnabled(enabled);
}

bool AudioService::setRequestedAudioConfig(uint32_t sampleRate, uint32_t bufferFrames) {
    const auto state = impl_->state.load(std::memory_order_acquire);
    if (state != AudioServiceState::Stopped && state != AudioServiceState::Error) return false;
    std::lock_guard<std::mutex> lock(impl_->configMutex);
    impl_->requestedSampleRate = sampleRate > 0 ? sampleRate : 48000;
    impl_->requestedBufferFrames = bufferFrames > 0 ? bufferFrames : 128;
    return true;
}

uint32_t AudioService::getRequestedSampleRate() const noexcept {
    std::lock_guard<std::mutex> lock(impl_->configMutex);
    return impl_->requestedSampleRate;
}

uint32_t AudioService::getRequestedBufferFrames() const noexcept {
    std::lock_guard<std::mutex> lock(impl_->configMutex);
    return impl_->requestedBufferFrames;
}

void AudioService::updateLiveStrip(uint32_t logicalId, float gainDb, float pan, bool muted, bool solo) {
    std::shared_ptr<StripMainMixDspNode> out;
    {
        std::lock_guard<std::mutex> lock(impl_->mainOutputMutex);
        out = impl_->activeMainOutput;
    }
    if (out) {
        out->updateStripByLogicalId(logicalId, gainDb, pan, muted, solo);
    }
}

void AudioService::updateLiveStripDsp(const AudioStripConfig& config) {
    std::shared_ptr<StripInsertDspNode> fx;
    {
        std::lock_guard<std::mutex> lock(impl_->stripFxMutex);
        fx = impl_->activeStripFx;
    }
    if (fx) {
        fx->updateStripDsp(config);
    }
    std::shared_ptr<StripMainMixDspNode> out;
    {
        std::lock_guard<std::mutex> lock(impl_->mainOutputMutex);
        out = impl_->activeMainOutput;
    }
    if (out) {
        out->updateStripConfig(config);
    }
}

void AudioService::updateLiveAuxBusDsp(size_t auxIndex, const AudioBusConfig& config) {
    if (auxIndex >= 4) return;
    {
        std::lock_guard<std::mutex> lock(impl_->configMutex);
        impl_->consoleAuxBuses[auxIndex] = config;
        impl_->auxBusesConfigured = true;
    }
    std::shared_ptr<StripMainMixDspNode> out;
    {
        std::lock_guard<std::mutex> lock(impl_->mainOutputMutex);
        out = impl_->activeMainOutput;
    }
    if (out) {
        out->updateAuxBusConfig(auxIndex, config);
    }
}

void AudioService::updateLiveMasterBusDsp(const AudioBusConfig& config) {
    {
        std::lock_guard<std::mutex> lock(impl_->configMutex);
        impl_->consoleMasterBus = config;
        impl_->masterBusConfigured = true;
    }
    std::shared_ptr<StripMainMixDspNode> out;
    {
        std::lock_guard<std::mutex> lock(impl_->mainOutputMutex);
        out = impl_->activeMainOutput;
    }
    if (out) {
        out->updateMasterBusConfig(config);
    }
}

void AudioService::setAnalyzerTap(uint32_t logicalChannelId, uint8_t tapSource) noexcept {
    impl_->tapChannelId_.store(logicalChannelId, std::memory_order_relaxed);
    impl_->tapSource_.store(tapSource, std::memory_order_relaxed);
    std::shared_ptr<StripInsertDspNode> sfx;
    std::shared_ptr<StripMainMixDspNode> smm;
    {
        std::lock_guard<std::mutex> lock(impl_->stripFxMutex);
        sfx = impl_->activeStripFx;
    }
    {
        std::lock_guard<std::mutex> lock(impl_->mainOutputMutex);
        smm = impl_->activeMainOutput;
    }
    if (sfx) sfx->setAnalyzerTap(logicalChannelId, tapSource, &impl_->ringBuffer_);
    if (smm) smm->setAnalyzerTap(logicalChannelId, tapSource, &impl_->ringBuffer_);
}

AnalyzerRingBuffer& AudioService::analyzerRingBuffer() noexcept {
    return impl_->ringBuffer_;
}

AudioServiceSnapshot AudioService::snapshot() const {
    StereoDemoControls controls;
    {
        std::lock_guard<std::mutex> processorLock(impl_->processorMutex);
        controls = impl_->processor->requestedControls();
    }
    std::string capId;
    std::string renId;
    {
        std::lock_guard<std::mutex> configLock(impl_->configMutex);
        capId = impl_->captureDeviceId;
        renId = impl_->renderDeviceId;
    }
    std::lock_guard<std::mutex> lock(impl_->snapshotMutex);
    auto result = impl_->published;
    result.mode = impl_->mode.load(std::memory_order_acquire);
    result.state = impl_->state.load(std::memory_order_acquire);
    result.error = impl_->published.error;
    result.controls = controls;
    if (result.state != AudioServiceState::Streaming) {
        if (!capId.empty() || result.captureDeviceId.empty()) {
            result.captureDeviceId = capId;
        }
        if (!renId.empty() || result.renderDeviceId.empty()) {
            result.renderDeviceId = renId;
        }
    }
    return result;
}

AudioService& audio_service() {
    static AudioService service;
    return service;
}

const char* audio_service_state_name(AudioServiceState state) noexcept {
    switch (state) {
        case AudioServiceState::Stopped: return "STOPPED";
        case AudioServiceState::Initializing: return "INITIALIZING";
        case AudioServiceState::Streaming: return "STREAMING";
        case AudioServiceState::Stopping: return "STOPPING";
        case AudioServiceState::Error: return "ERROR";
    }
    return "UNKNOWN";
}


