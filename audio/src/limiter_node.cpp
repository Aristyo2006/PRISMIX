#include "audiocore/limiter_node.hpp"
#include "audiocore/audio_buffer.hpp"
#include <cmath>
#include <algorithm>

namespace audiocore {

LimiterNode::LimiterNode(float ceilingDb, double sampleRate)
    : sample_rate_(sampleRate > 0.0 ? sampleRate : 48000.0)
{
    setCeilingDb(ceilingDb);
    updateReleaseCoeff();
    reset();
}

void LimiterNode::setSampleRate(double sampleRate) noexcept {
    if (sampleRate > 0.0 && sampleRate != sample_rate_) {
        sample_rate_ = sampleRate;
        updateReleaseCoeff();
    }
}

void LimiterNode::setCeilingDb(float ceilingDb) noexcept {
    ceiling_db_ = std::min(0.0f, ceilingDb);
    ceiling_linear_ = std::pow(10.0f, ceiling_db_ / 20.0f);
}

void LimiterNode::setReleaseMs(float releaseMs) noexcept {
    release_ms_ = std::max(1.0f, releaseMs);
    updateReleaseCoeff();
}

void LimiterNode::updateReleaseCoeff() noexcept {
    const double fs = (sample_rate_ > 0.0) ? sample_rate_ : 48000.0;
    const double dtRel = (static_cast<double>(release_ms_) * 0.001);
    alpha_rel_ = static_cast<float>(1.0 - std::exp(-1.0 / (dtRel * fs)));
}

void LimiterNode::reset() {
    current_gain_ = 1.0f;
}

float LimiterNode::getCurrentGainReductionDb() const noexcept {
    float g = current_gain_;
    if (g <= 1e-6f) return -120.0f;
    if (g >= 0.99999f) return 0.0f;
    return 20.0f * std::log10(g);
}

void LimiterNode::process(
    const AudioBuffer& input,
    AudioBuffer& output,
    uint32_t frames)
{
    const uint32_t inChannels = input.getChannelCount();
    const uint32_t outChannels = output.getChannelCount();
    const uint32_t actualFrames = std::min(frames, std::min(input.getFrameCount(), output.getFrameCount()));

    if (inChannels == 0 || outChannels == 0 || actualFrames == 0) return;

    if (bypassed_.load(std::memory_order_relaxed)) {
        if (&input != &output) {
            for (uint32_t ch = 0; ch < outChannels; ++ch) {
                uint32_t inCh = (ch < inChannels) ? ch : (inChannels - 1);
                std::copy_n(input.getChannelData(inCh), actualFrames, output.getChannelData(ch));
            }
        }
        current_gain_ = 1.0f;
        return;
    }

    const float* inL = input.getChannelData(0);
    const float* inR = (inChannels > 1) ? input.getChannelData(1) : inL;
    float* outL = output.getChannelData(0);
    float* outR = (outChannels > 1) ? output.getChannelData(1) : nullptr;

    const float ceiling = ceiling_linear_;
    const float alphaRel = alpha_rel_;
    float gain = current_gain_;

    for (uint32_t i = 0; i < actualFrames; ++i) {
        const float sL = inL[i];
        const float sR = inR[i];

        // Stereo-linked peak detector
        const float peakMag = std::max(std::abs(sL), std::abs(sR));

        // Clamping logic:
        if (peakMag > ceiling) {
            const float clampGain = ceiling / peakMag;
            if (clampGain < gain) {
                gain = clampGain; // Instantaneous brickwall attack
            } else {
                gain += alphaRel * (clampGain - gain); // Smooth release toward raised ceiling
            }
        } else {
            // Smooth release back to unity
            gain += alphaRel * (1.0f - gain);
        }

        outL[i] = sL * gain;
        if (outR) {
            outR[i] = sR * gain;
        }
    }

    current_gain_ = gain;

    // Zero extra output channels
    for (uint32_t ch = 2; ch < outChannels; ++ch) {
        output.clear(actualFrames);
    }
}

} // namespace audiocore
