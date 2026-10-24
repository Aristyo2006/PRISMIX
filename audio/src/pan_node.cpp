#include "audiocore/pan_node.hpp"
#include "audiocore/audio_buffer.hpp"
#include <cmath>
#include <algorithm>

namespace audiocore {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kHalfPi = kPi * 0.5f;
}

PanNode::PanNode(float initialPan, double sampleRate)
    : pan_param_("Pan", initialPan, -1.0f, 1.0f, ""),
      smoother_(initialPan, sampleRate, 10.0f)
{
    setPan(initialPan);
    smoother_.reset(initialPan);
}

void PanNode::computeGains(float pan, float& outL, float& outR) noexcept {
    const float clampedPan = std::max(-1.0f, std::min(1.0f, pan));
    // theta in [0, pi/2]
    const float theta = ((clampedPan + 1.0f) * 0.5f) * kHalfPi;
    outL = std::cos(theta);
    outR = std::sin(theta);
}

void PanNode::setPan(float pan) noexcept {
    pan_param_.setValue(pan);
    smoother_.setTarget(pan_param_.getValue());
    float gL, gR;
    computeGains(pan_param_.getValue(), gL, gR);
    left_gain_.store(gL, std::memory_order_release);
    right_gain_.store(gR, std::memory_order_release);
}

float PanNode::getPan() const noexcept {
    return pan_param_.getValue();
}

void PanNode::setSampleRate(double sampleRate) noexcept {
    smoother_.setSampleRate(sampleRate);
}

void PanNode::setSmoothingTimeMs(float ms) noexcept {
    smoother_.setTimeMs(ms);
}

void PanNode::reset() {
    float cur = pan_param_.getValue();
    smoother_.reset(cur);
    float gL, gR;
    computeGains(cur, gL, gR);
    left_gain_.store(gL, std::memory_order_release);
    right_gain_.store(gR, std::memory_order_release);
}

void PanNode::process(
    const AudioBuffer& input,
    AudioBuffer& output,
    uint32_t frames)
{
    const uint32_t inChannels = input.getChannelCount();
    const uint32_t outChannels = output.getChannelCount();
    const uint32_t actualFrames = std::min(frames, std::min(input.getFrameCount(), output.getFrameCount()));

    if (inChannels == 0 || outChannels == 0 || actualFrames == 0) {
        return;
    }

    if (outChannels < 2) {
        // Output is mono: copy input channel 0
        if (&input != &output) {
            output.copyFrom(input, actualFrames);
        }
        return;
    }

    const bool smoothing = smoother_.isSmoothing();
    const float* inL = input.getChannelData(0);
    const float* inR = (inChannels > 1) ? input.getChannelData(1) : inL;
    float* outL = output.getChannelData(0);
    float* outR = output.getChannelData(1);

    if (smoothing) {
        for (uint32_t i = 0; i < actualFrames; ++i) {
            float smoothedPan = smoother_.nextSample();
            float gL, gR;
            computeGains(smoothedPan, gL, gR);
            float sL = inL[i] * gL;
            float sR = inR[i] * gR;
            outL[i] = sL;
            outR[i] = sR;
        }
        float cur = smoother_.getCurrentValue();
        float gL, gR;
        computeGains(cur, gL, gR);
        left_gain_.store(gL, std::memory_order_relaxed);
        right_gain_.store(gR, std::memory_order_relaxed);
    } else {
        const float gL = left_gain_.load(std::memory_order_relaxed);
        const float gR = right_gain_.load(std::memory_order_relaxed);

        for (uint32_t i = 0; i < actualFrames; ++i) {
            float sL = inL[i] * gL;
            float sR = inR[i] * gR;
            outL[i] = sL;
            outR[i] = sR;
        }
    }

    // Zero any extra channels
    for (uint32_t ch = 2; ch < outChannels; ++ch) {
        std::memset(output.getChannelData(ch), 0, actualFrames * sizeof(float));
    }
}

} // namespace audiocore
