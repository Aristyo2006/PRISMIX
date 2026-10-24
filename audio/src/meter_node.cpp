#include "audiocore/meter_node.hpp"
#include "audiocore/audio_buffer.hpp"
#include <cmath>
#include <algorithm>

namespace audiocore {

MeterNode::MeterNode() {
    reset();
}

float MeterNode::calculatePeak(const float* data, uint32_t frames) noexcept {
    if (data == nullptr || frames == 0) return 0.0f;
    float maxMag = 0.0f;
    for (uint32_t i = 0; i < frames; ++i) {
        float mag = std::abs(data[i]);
        if (mag > maxMag) maxMag = mag;
    }
    return maxMag;
}

float MeterNode::calculateRms(const float* data, uint32_t frames) noexcept {
    if (data == nullptr || frames == 0) return 0.0f;
    double sumSq = 0.0;
    for (uint32_t i = 0; i < frames; ++i) {
        double v = static_cast<double>(data[i]);
        sumSq += v * v;
    }
    return static_cast<float>(std::sqrt(sumSq / static_cast<double>(frames)));
}

float MeterNode::getPeak(uint32_t channel) const noexcept {
    if (channel < 2) return peak_[channel].load(std::memory_order_relaxed);
    return 0.0f;
}

float MeterNode::getPeakHold(uint32_t channel) const noexcept {
    if (channel < 2) return peak_hold_[channel].load(std::memory_order_relaxed);
    return 0.0f;
}

float MeterNode::getRms(uint32_t channel) const noexcept {
    if (channel < 2) return rms_[channel].load(std::memory_order_relaxed);
    return 0.0f;
}

void MeterNode::resetPeakHold() noexcept {
    peak_hold_[0].store(0.0f, std::memory_order_relaxed);
    peak_hold_[1].store(0.0f, std::memory_order_relaxed);
}

void MeterNode::reset() {
    peak_[0].store(0.0f, std::memory_order_relaxed);
    peak_[1].store(0.0f, std::memory_order_relaxed);
    resetPeakHold();
    rms_[0].store(0.0f, std::memory_order_relaxed);
    rms_[1].store(0.0f, std::memory_order_relaxed);
}

void MeterNode::process(
    const AudioBuffer& input,
    AudioBuffer& output,
    uint32_t frames)
{
    const uint32_t inChannels = input.getChannelCount();
    const uint32_t actualFrames = std::min(frames, std::min(input.getFrameCount(), output.getFrameCount()));

    if (&input != &output) {
        output.copyFrom(input, actualFrames);
    }

    if (inChannels == 0 || actualFrames == 0) return;

    for (uint32_t ch = 0; ch < std::min(2u, inChannels); ++ch) {
        const float* d = input.getChannelData(ch);
        float p = calculatePeak(d, actualFrames);
        float r = calculateRms(d, actualFrames);

        peak_[ch].store(p, std::memory_order_relaxed);
        rms_[ch].store(r, std::memory_order_relaxed);

        float curHold = peak_hold_[ch].load(std::memory_order_relaxed);
        while (p > curHold && !peak_hold_[ch].compare_exchange_weak(curHold, p, std::memory_order_relaxed)) {}
    }
}

} // namespace audiocore

