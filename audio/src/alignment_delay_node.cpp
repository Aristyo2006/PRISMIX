#include "audiocore/alignment_delay_node.hpp"
#include "audiocore/audio_buffer.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace audiocore {

namespace {

// Returns next power of 2 greater than or equal to n
size_t nextPowerOfTwo(size_t n) {
    if (n == 0) return 1;
    --n;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
#if defined(_WIN64) || defined(__x86_64__) || defined(__aarch64__)
    n |= n >> 32;
#endif
    return n + 1;
}

} // namespace

AlignmentDelayNode::AlignmentDelayNode(size_t numChannels, double sampleRate)
    : sample_rate_(sampleRate > 0.0 ? sampleRate : 48000.0)
    , num_channels_(numChannels > 0 ? numChannels : DEFAULT_CHANNELS)
    , channels_(num_channels_) {
    allocateBuffers();
}

void AlignmentDelayNode::allocateBuffers() {
    // 500 ms delay at current sample rate + 8 safety samples for 4-point Hermite interpolation
    size_t minRequired = static_cast<size_t>(std::ceil(MAX_DELAY_MS * 0.001 * sample_rate_)) + 8;
    size_t capacity = nextPowerOfTwo(minRequired);
    if (capacity < 1024) capacity = 1024;

    for (size_t c = 0; c < num_channels_; ++c) {
        channels_[c].buffer.assign(capacity, 0.0f);
        channels_[c].buffer_mask = capacity - 1;
        channels_[c].write_index = 0;

        float targetMs = channels_[c].target_delay_ms.load(std::memory_order_relaxed);
        double targetSamples = targetMs * 0.001 * sample_rate_;
        channels_[c].target_delay_samples = targetSamples;
        channels_[c].current_delay_samples = targetSamples;
    }
}

void AlignmentDelayNode::setSampleRate(double sampleRate) noexcept {
    if (sampleRate <= 0.0 || sampleRate == sample_rate_) return;
    sample_rate_ = sampleRate;
    allocateBuffers();
}

void AlignmentDelayNode::setStereoLink(bool link) noexcept {
    stereo_link_.store(link, std::memory_order_relaxed);
    if (link && num_channels_ > 1) {
        float ch0 = channels_[0].target_delay_ms.load(std::memory_order_relaxed);
        channels_[1].target_delay_ms.store(ch0, std::memory_order_relaxed);
        snapToTarget(1);
    }
}

void AlignmentDelayNode::setChannelDelayMs(size_t channel, float delayMs, bool snap) noexcept {
    if (channel >= num_channels_) return;
    float clampedMs = std::clamp(delayMs, 0.0f, MAX_DELAY_MS);
    channels_[channel].target_delay_ms.store(clampedMs, std::memory_order_relaxed);

    if (stereo_link_.load(std::memory_order_relaxed) && num_channels_ > 1) {
        if (channel == 0) {
            channels_[1].target_delay_ms.store(clampedMs, std::memory_order_relaxed);
            if (snap) snapToTarget(1);
        } else if (channel == 1) {
            channels_[0].target_delay_ms.store(clampedMs, std::memory_order_relaxed);
            if (snap) snapToTarget(0);
        }
    }

    if (snap) {
        snapToTarget(channel);
    }
}

float AlignmentDelayNode::getChannelDelayMs(size_t channel) const noexcept {
    if (channel >= num_channels_) return 0.0f;
    return channels_[channel].target_delay_ms.load(std::memory_order_relaxed);
}

uint32_t AlignmentDelayNode::getChannelDelaySamples(size_t channel) const noexcept {
    if (channel >= num_channels_) return 0;
    float ms = channels_[channel].target_delay_ms.load(std::memory_order_relaxed);
    return static_cast<uint32_t>(std::round(ms * 0.001 * sample_rate_));
}

double AlignmentDelayNode::getChannelDelaySamplesFractional(size_t channel) const noexcept {
    if (channel >= num_channels_) return 0.0;
    float ms = channels_[channel].target_delay_ms.load(std::memory_order_relaxed);
    return static_cast<double>(ms * 0.001 * sample_rate_);
}

void AlignmentDelayNode::setChannelEnabled(size_t channel, bool enabled) noexcept {
    if (channel >= num_channels_) return;
    channels_[channel].enabled.store(enabled, std::memory_order_relaxed);
}

bool AlignmentDelayNode::isChannelEnabled(size_t channel) const noexcept {
    if (channel >= num_channels_) return false;
    return channels_[channel].enabled.load(std::memory_order_relaxed);
}

void AlignmentDelayNode::setChannelMute(size_t channel, bool mute) noexcept {
    if (channel >= num_channels_) return;
    channels_[channel].muted.store(mute, std::memory_order_relaxed);
}

bool AlignmentDelayNode::isChannelMuted(size_t channel) const noexcept {
    if (channel >= num_channels_) return false;
    return channels_[channel].muted.load(std::memory_order_relaxed);
}

void AlignmentDelayNode::setChannelPolarity(size_t channel, bool inverted) noexcept {
    if (channel >= num_channels_) return;
    channels_[channel].polarity_inverted.store(inverted, std::memory_order_relaxed);
}

bool AlignmentDelayNode::isChannelPolarityInverted(size_t channel) const noexcept {
    if (channel >= num_channels_) return false;
    return channels_[channel].polarity_inverted.load(std::memory_order_relaxed);
}

void AlignmentDelayNode::snapToTarget(size_t channel) noexcept {
    if (channel >= num_channels_) return;
    float ms = channels_[channel].target_delay_ms.load(std::memory_order_relaxed);
    double targetSamples = ms * 0.001 * sample_rate_;
    channels_[channel].target_delay_samples = targetSamples;
    channels_[channel].current_delay_samples = targetSamples;
}

void AlignmentDelayNode::snapAllToTarget() noexcept {
    for (size_t c = 0; c < num_channels_; ++c) {
        snapToTarget(c);
    }
}

void AlignmentDelayNode::clearBuffers() noexcept {
    for (size_t c = 0; c < num_channels_; ++c) {
        std::fill(channels_[c].buffer.begin(), channels_[c].buffer.end(), 0.0f);
        channels_[c].write_index = 0;
    }
}

void AlignmentDelayNode::process(const AudioBuffer& input, AudioBuffer& output, uint32_t frames) {
    if (frames == 0) return;

    const size_t inChannels = input.getChannelCount();
    const size_t outChannels = output.getChannelCount();
    const size_t activeChannels = std::min({num_channels_, inChannels, outChannels});

    // Clear any extra output channels beyond active channels
    for (size_t ch = activeChannels; ch < outChannels; ++ch) {
        std::memset(output.getChannelData(static_cast<uint32_t>(ch)), 0, frames * sizeof(float));
    }

    const bool masterBypass = isBypassed();

    // Slew coefficient for smooth live transitions (~20 ms time constant)
    const double slewAlpha = 1.0 - std::exp(-1.0 / (sample_rate_ * 0.020));

    for (size_t c = 0; c < activeChannels; ++c) {
        auto& state = channels_[c];
        const float* inData = input.getChannelData(static_cast<uint32_t>(c));
        float* outData = output.getChannelData(static_cast<uint32_t>(c));

        float* ringBuf = state.buffer.data();
        const size_t mask = state.buffer_mask;
        size_t writeIdx = state.write_index;

        const bool chEnabled = state.enabled.load(std::memory_order_relaxed);
        const bool chMuted = state.muted.load(std::memory_order_relaxed);
        const bool chInverted = state.polarity_inverted.load(std::memory_order_relaxed);

        const float targetMs = std::clamp(state.target_delay_ms.load(std::memory_order_relaxed), 0.0f, MAX_DELAY_MS);
        const double targetDelaySamples = targetMs * 0.001 * sample_rate_;
        state.target_delay_samples = targetDelaySamples;

        double curDelay = state.current_delay_samples;

        // If master is bypassed or channel is disabled: bit-exact passthrough
        // (Circular buffer still records audio so that enabling does not pop)
        if (masterBypass || !chEnabled) {
            for (uint32_t n = 0; n < frames; ++n) {
                float sample = inData[n];
                ringBuf[writeIdx] = sample;
                writeIdx = (writeIdx + 1) & mask;
                outData[n] = sample;
            }
            state.write_index = writeIdx;
            state.current_delay_samples = targetDelaySamples;
            continue;
        }

        for (uint32_t n = 0; n < frames; ++n) {
            const float inSample = inData[n];

            // 1. Write incoming sample to circular delay line
            ringBuf[writeIdx] = inSample;

            // 2. Smoothly slew delay pointer towards target
            if (std::abs(targetDelaySamples - curDelay) > 1e-6) {
                curDelay += slewAlpha * (targetDelaySamples - curDelay);
            } else {
                curDelay = targetDelaySamples;
            }

            float delayedSample = 0.0f;

            // 3. Exact 0 ms delay optimization (bit-exact passthrough)
            if (curDelay < 1e-5) {
                delayedSample = inSample;
            } else {
                // 4. Fractional delay calculation with Catmull-Rom / Hermite cubic interpolation
                double readPos = static_cast<double>(writeIdx) - curDelay;
                double readFloor = std::floor(readPos);
                double frac = readPos - readFloor;

                int64_t i0 = static_cast<int64_t>(readFloor);
                size_t idx0 = static_cast<size_t>(i0) & mask;

                if (frac < 1e-5) {
                    delayedSample = ringBuf[idx0];
                } else {
                    size_t idx_m1 = static_cast<size_t>(i0 - 1) & mask;
                    size_t idx_p1 = static_cast<size_t>(i0 + 1) & mask;
                    size_t idx_p2 = static_cast<size_t>(i0 + 2) & mask;

                    float ym1 = ringBuf[idx_m1];
                    float y0  = ringBuf[idx0];
                    float yp1 = ringBuf[idx_p1];
                    float yp2 = ringBuf[idx_p2];

                    float c0 = y0;
                    float c1 = 0.5f * (yp1 - ym1);
                    float c2 = ym1 - 2.5f * y0 + 2.0f * yp1 - 0.5f * yp2;
                    float c3 = 0.5f * (yp2 - ym1) + 1.5f * (y0 - yp1);

                    float f = static_cast<float>(frac);
                    delayedSample = ((c3 * f + c2) * f + c1) * f + c0;
                }
            }

            // 5. Polarity Inversion (Normal = +1.0, Inverted = -1.0)
            if (chInverted) {
                delayedSample = -delayedSample;
            }

            // 6. Output Mute (Buffer continues processing, output silenced)
            if (chMuted) {
                delayedSample = 0.0f;
            }

            // 7. Write to output buffer
            outData[n] = delayedSample;

            // 8. Advance circular buffer write pointer
            writeIdx = (writeIdx + 1) & mask;
        }

        state.write_index = writeIdx;
        state.current_delay_samples = curDelay;
    }
}

} // namespace audiocore
