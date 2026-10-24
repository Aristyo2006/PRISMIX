#include "audiocore/gain_node.hpp"
#include "audiocore/audio_buffer.hpp"
#include <algorithm>

namespace audiocore {

GainNode::GainNode(float initialGainDb)
    : gain_param_("Gain", initialGainDb, -96.0f, 24.0f, "dB")
{
    setGainDb(initialGainDb);
}

void GainNode::setGainDb(float gainDb) noexcept {
    gain_param_.setValue(gainDb);
    updateLinearGain(gain_param_.getValue());
}

float GainNode::getGainDb() const noexcept {
    return gain_param_.getValue();
}

float GainNode::getLinearGain() const noexcept {
    return linear_gain_.load(std::memory_order_relaxed);
}

void GainNode::setMuted(bool muted) noexcept {
    is_muted_.store(muted, std::memory_order_release);
}

bool GainNode::isMuted() const noexcept {
    return is_muted_.load(std::memory_order_relaxed);
}

void GainNode::reset() {
    // Stateless gain node; nothing to reset
}

void GainNode::updateLinearGain(float db) noexcept {
    if (db <= -120.0f) {
        linear_gain_.store(0.0f, std::memory_order_release);
    } else {
        const float linear = std::pow(10.0f, db / 20.0f);
        linear_gain_.store(linear, std::memory_order_release);
    }
}

void GainNode::process(
    const AudioBuffer& input,
    AudioBuffer& output,
    uint32_t frames)
{
    const bool muted = is_muted_.load(std::memory_order_relaxed);
    const float gain = muted ? 0.0f : linear_gain_.load(std::memory_order_relaxed);

    const uint32_t process_channels = std::min(input.getChannelCount(), output.getChannelCount());
    const uint32_t actual_frames = std::min(frames, std::min(input.getFrameCount(), output.getFrameCount()));

    if (gain <= 0.0f) {
        // Full mute / zero output
        output.clear(actual_frames);
        return;
    }

    const bool is_in_place = (&input == &output);

    if (gain == 1.0f) {
        // Unity gain (0 dB) optimization
        if (!is_in_place) {
            output.copyFrom(input, actual_frames);
        }
        return;
    }

    // Direct, branch-free numerical gain processing
    for (uint32_t ch = 0; ch < process_channels; ++ch) {
        const float* in = input.getChannelData(ch);
        float* out = output.getChannelData(ch);

        for (uint32_t i = 0; i < actual_frames; ++i) {
            out[i] = in[i] * gain;
        }
    }

    // Zero any extra output channels
    for (uint32_t ch = process_channels; ch < output.getChannelCount(); ++ch) {
        float* out = output.getChannelData(ch);
        std::memset(out, 0, actual_frames * sizeof(float));
    }
}

} // namespace audiocore

