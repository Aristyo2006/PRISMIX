#include "audiocore/mixer_node.hpp"
#include "audiocore/audio_buffer.hpp"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace audiocore {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kHalfPi = kPi * 0.5f;

inline float dbToLinear(float db) noexcept {
    if (db <= -120.0f) return 0.0f;
    return std::pow(10.0f, db / 20.0f);
}
}

MixerNode::MixerNode(uint32_t channelCount) {
    configureChannels(channelCount);
}

void MixerNode::configureChannels(uint32_t count) {
    channels_.resize(std::max(1u, count));
    for (size_t i = 0; i < channels_.size(); ++i) {
        channels_[i].gainDb = 0.0f;
        channels_[i].linearGain = 1.0f;
        channels_[i].pan = 0.0f;
        channels_[i].panGainL = 0.70710678f;
        channels_[i].panGainR = 0.70710678f;
        channels_[i].muted = false;
        channels_[i].soloed = false;
    }
    active_solo_count_.store(0, std::memory_order_release);
}

void MixerNode::setChannelGainDb(uint32_t channel, float gainDb) noexcept {
    if (channel < channels_.size()) {
        channels_[channel].gainDb = gainDb;
        channels_[channel].linearGain = dbToLinear(gainDb);
    }
}

float MixerNode::getChannelGainDb(uint32_t channel) const noexcept {
    if (channel < channels_.size()) return channels_[channel].gainDb;
    return 0.0f;
}

float MixerNode::getChannelLinearGain(uint32_t channel) const noexcept {
    if (channel < channels_.size()) return channels_[channel].linearGain;
    return 1.0f;
}

void MixerNode::updateChannelPanGains(uint32_t channel) noexcept {
    const float p = std::max(-1.0f, std::min(1.0f, channels_[channel].pan));
    const float theta = ((p + 1.0f) * 0.5f) * kHalfPi;
    channels_[channel].panGainL = std::cos(theta);
    channels_[channel].panGainR = std::sin(theta);
}

void MixerNode::setChannelPan(uint32_t channel, float pan) noexcept {
    if (channel < channels_.size()) {
        channels_[channel].pan = pan;
        updateChannelPanGains(channel);
    }
}

float MixerNode::getChannelPan(uint32_t channel) const noexcept {
    if (channel < channels_.size()) return channels_[channel].pan;
    return 0.0f;
}

void MixerNode::setChannelMute(uint32_t channel, bool muted) noexcept {
    if (channel < channels_.size()) {
        channels_[channel].muted = muted;
    }
}

bool MixerNode::isChannelMuted(uint32_t channel) const noexcept {
    if (channel < channels_.size()) return channels_[channel].muted;
    return false;
}

void MixerNode::setChannelSolo(uint32_t channel, bool soloed) noexcept {
    if (channel < channels_.size()) {
        bool prev = channels_[channel].soloed;
        channels_[channel].soloed = soloed;
        if (prev != soloed) {
            uint32_t cur = active_solo_count_.load(std::memory_order_relaxed);
            if (soloed) cur++;
            else if (cur > 0) cur--;
            active_solo_count_.store(cur, std::memory_order_release);
        }
    }
}

bool MixerNode::isChannelSoloed(uint32_t channel) const noexcept {
    if (channel < channels_.size()) return channels_[channel].soloed;
    return false;
}

void MixerNode::setMasterGainDb(float gainDb) noexcept {
    master_gain_db_.store(gainDb, std::memory_order_relaxed);
    master_linear_gain_.store(dbToLinear(gainDb), std::memory_order_release);
}

float MixerNode::getMasterGainDb() const noexcept {
    return master_gain_db_.load(std::memory_order_relaxed);
}

float MixerNode::getMasterLinearGain() const noexcept {
    return master_linear_gain_.load(std::memory_order_relaxed);
}

void MixerNode::setMasterMute(bool muted) noexcept {
    master_muted_.store(muted, std::memory_order_release);
}

bool MixerNode::isMasterMuted() const noexcept {
    return master_muted_.load(std::memory_order_relaxed);
}

bool MixerNode::hasAnySoloActive() const noexcept {
    return active_solo_count_.load(std::memory_order_relaxed) > 0;
}

bool MixerNode::isChannelAudible(uint32_t channel) const noexcept {
    if (master_muted_.load(std::memory_order_relaxed)) return false;
    if (channel >= channels_.size()) return false;
    const auto& strip = channels_[channel];

    // MUTE ALWAYS OVERRIDES SOLO
    if (strip.muted) return false;

    // Solo Check
    if (hasAnySoloActive()) {
        return strip.soloed;
    }

    return true;
}

void MixerNode::reset() {
    // Reset any state if needed
}

void MixerNode::process(
    const AudioBuffer& input,
    AudioBuffer& output,
    uint32_t frames)
{
    const uint32_t actualFrames = std::min(frames, std::min(input.getFrameCount(), output.getFrameCount()));
    if (actualFrames == 0 || output.getChannelCount() == 0) return;

    const bool masterMuted = master_muted_.load(std::memory_order_relaxed);
    const float masterGain = masterMuted ? 0.0f : master_linear_gain_.load(std::memory_order_relaxed);

    if (masterGain <= 0.0f) {
        output.clear(actualFrames);
        return;
    }

    const uint32_t inChannels = input.getChannelCount();
    const uint32_t outChannels = output.getChannelCount();
    const bool anySolo = hasAnySoloActive();

    // Clear output buffer before accumulation
    output.clear(actualFrames);

    float* outL = output.getChannelData(0);
    float* outR = (outChannels > 1) ? output.getChannelData(1) : nullptr;

    const uint32_t limitChannels = std::min(inChannels, static_cast<uint32_t>(channels_.size()));

    // Special case: 2-channel stereo pass-through mixer (e.g. Channel 0 = L, Channel 1 = R)
    if (inChannels == 2 && limitChannels == 2 && channels_[0].pan == -1.0f && channels_[1].pan == 1.0f) {
        // Direct stereo path
        const bool ch0Audible = !channels_[0].muted && (!anySolo || channels_[0].soloed);
        const bool ch1Audible = !channels_[1].muted && (!anySolo || channels_[1].soloed);

        const float* in0 = input.getChannelData(0);
        const float* in1 = input.getChannelData(1);
        const float g0 = ch0Audible ? (channels_[0].linearGain * masterGain) : 0.0f;
        const float g1 = ch1Audible ? (channels_[1].linearGain * masterGain) : 0.0f;

        for (uint32_t i = 0; i < actualFrames; ++i) {
            outL[i] = in0[i] * g0;
            if (outR) outR[i] = in1[i] * g1;
        }
        return;
    }

    // General multi-channel summing
    for (uint32_t c = 0; c < limitChannels; ++c) {
        const auto& strip = channels_[c];

        // Mute strictly overrides solo
        if (strip.muted) continue;
        if (anySolo && !strip.soloed) continue;

        const float effGain = strip.linearGain * masterGain;
        if (effGain <= 0.0f) continue;

        const float* inData = input.getChannelData(c);
        const float effGainL = effGain * strip.panGainL;
        const float effGainR = effGain * strip.panGainR;

        if (outR) {
            for (uint32_t i = 0; i < actualFrames; ++i) {
                outL[i] += inData[i] * effGainL;
                outR[i] += inData[i] * effGainR;
            }
        } else {
            // Mono master
            for (uint32_t i = 0; i < actualFrames; ++i) {
                outL[i] += inData[i] * effGain;
            }
        }
    }
}

} // namespace audiocore

