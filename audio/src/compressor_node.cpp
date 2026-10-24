#include "audiocore/compressor_node.hpp"
#include "audiocore/audio_buffer.hpp"
#include <cmath>
#include <algorithm>

namespace audiocore {

CompressorNode::CompressorNode(double sampleRate)
    : sample_rate_(sampleRate > 0.0 ? sampleRate : 48000.0)
{
    updateTimeConstants();
    reset();
}

void CompressorNode::setSampleRate(double sampleRate) noexcept {
    if (sampleRate > 0.0 && sampleRate != sample_rate_) {
        sample_rate_ = sampleRate;
        updateTimeConstants();
    }
}

void CompressorNode::setThresholdDb(float thresholdDb) noexcept {
    threshold_db_ = thresholdDb;
}

void CompressorNode::setRatio(float ratio) noexcept {
    ratio_ = std::max(1.0f, ratio);
}

void CompressorNode::setAttackMs(float attackMs) noexcept {
    attack_ms_ = std::max(0.1f, attackMs);
    updateTimeConstants();
}

void CompressorNode::setReleaseMs(float releaseMs) noexcept {
    release_ms_ = std::max(1.0f, releaseMs);
    updateTimeConstants();
}

void CompressorNode::setMakeupGainDb(float makeupDb) noexcept {
    makeup_db_ = makeupDb;
}

void CompressorNode::setKneeDb(float kneeDb) noexcept {
    knee_db_ = std::max(0.0f, kneeDb);
}

void CompressorNode::updateTimeConstants() noexcept {
    const double fs = (sample_rate_ > 0.0) ? sample_rate_ : 48000.0;
    const double dtAtt = (static_cast<double>(attack_ms_) * 0.001);
    const double dtRel = (static_cast<double>(release_ms_) * 0.001);

    alpha_att_ = static_cast<float>(1.0 - std::exp(-1.0 / (dtAtt * fs)));
    alpha_rel_ = static_cast<float>(1.0 - std::exp(-1.0 / (dtRel * fs)));
}

void CompressorNode::reset() {
    env_db_ = 0.0f; // 0 dB gain reduction initially
    detector_db_ = -90.0f;
    input_db_l_ = -90.0f;
    input_db_r_ = -90.0f;
    input_rms_l_ = -90.0f;
    input_rms_r_ = -90.0f;
    output_db_ = -90.0f;
    output_db_l_ = -90.0f;
    output_db_r_ = -90.0f;
    output_rms_l_ = -90.0f;
    output_rms_r_ = -90.0f;
}

float CompressorNode::computeStaticOutputDb(float inputDb) const noexcept {
    if (bypassed_.load(std::memory_order_relaxed)) {
        return inputDb;
    }
    const float T = threshold_db_;
    const float R = std::max(1.0f, ratio_);
    const float makeup = makeup_db_;
    const float grFactor = 1.0f - (1.0f / R);

    float targetGR_dB = 0.0f;
    if (knee_db_ > 0.0f) {
        const float W = knee_db_;
        const float diff = inputDb - T;
        if (diff <= -W * 0.5f) {
            targetGR_dB = 0.0f;
        } else if (std::abs(diff) <= W * 0.5f) {
            const float tmp = diff + W * 0.5f;
            targetGR_dB = -grFactor * (tmp * tmp) / (2.0f * W);
        } else {
            targetGR_dB = (T - inputDb) * grFactor;
        }
    } else {
        if (inputDb > T) {
            targetGR_dB = (T - inputDb) * grFactor;
        } else {
            targetGR_dB = 0.0f;
        }
    }
    return inputDb + targetGR_dB + makeup;
}

void CompressorNode::process(
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
        env_db_ = 0.0f;
        float maxMagL = 0.0f, maxMagR = 0.0f;
        double sumSqL = 0.0, sumSqR = 0.0;
        const float* inL = input.getChannelData(0);
        const float* inR = (inChannels > 1) ? input.getChannelData(1) : inL;
        for (uint32_t i = 0; i < actualFrames; ++i) {
            float magL = std::abs(inL[i]);
            float magR = std::abs(inR[i]);
            if (magL > maxMagL) maxMagL = magL;
            if (magR > maxMagR) maxMagR = magR;
            sumSqL += static_cast<double>(inL[i]) * inL[i];
            sumSqR += static_cast<double>(inR[i]) * inR[i];
        }
        float maxMag = std::max(maxMagL, maxMagR);
        detector_db_ = maxMag > 1e-6f ? 20.0f * std::log10(maxMag) : -90.0f;
        input_db_l_ = maxMagL > 1e-6f ? 20.0f * std::log10(maxMagL) : -90.0f;
        input_db_r_ = maxMagR > 1e-6f ? 20.0f * std::log10(maxMagR) : -90.0f;
        input_rms_l_ = (actualFrames > 0 && sumSqL > 0.0)
            ? static_cast<float>(20.0 * std::log10(std::max(1e-6, std::sqrt(sumSqL / actualFrames))))
            : -90.0f;
        input_rms_r_ = (actualFrames > 0 && sumSqR > 0.0)
            ? static_cast<float>(20.0 * std::log10(std::max(1e-6, std::sqrt(sumSqR / actualFrames))))
            : -90.0f;
        output_db_ = detector_db_;
        output_db_l_ = input_db_l_;
        output_db_r_ = input_db_r_;
        output_rms_l_ = input_rms_l_;
        output_rms_r_ = input_rms_r_;
        return;
    }

    const float* inL = input.getChannelData(0);
    const float* inR = (inChannels > 1) ? input.getChannelData(1) : inL;
    float* outL = output.getChannelData(0);
    float* outR = (outChannels > 1) ? output.getChannelData(1) : nullptr;

    const float T = threshold_db_;
    const float R = ratio_;
    const float makeup = makeup_db_;
    const float alphaAtt = alpha_att_;
    const float alphaRel = alpha_rel_;
    const float invRatio = (R > 0.0f) ? (1.0f / R) : 1.0f;
    const float grFactor = 1.0f - invRatio;

    float maxInMagL = 0.0f, maxInMagR = 0.0f;
    float maxOutMagL = 0.0f, maxOutMagR = 0.0f;
    double inSumSqL = 0.0, inSumSqR = 0.0;
    double outSumSqL = 0.0, outSumSqR = 0.0;

    for (uint32_t i = 0; i < actualFrames; ++i) {
        const float sL = inL[i];
        const float sR = inR[i];
        const float absInL = std::abs(sL);
        const float absInR = std::abs(sR);
        if (absInL > maxInMagL) maxInMagL = absInL;
        if (absInR > maxInMagR) maxInMagR = absInR;
        inSumSqL += static_cast<double>(sL) * sL;
        inSumSqR += static_cast<double>(sR) * sR;

        // 1. Stereo-linked peak detector
        const float peakMag = std::max(absInL, absInR);
        const float x_dB = 20.0f * std::log10(std::max(peakMag, 1e-6f));

        // 2. Gain computer (with hard knee or optional soft knee)
        float targetGR_dB = 0.0f;
        if (knee_db_ > 0.0f) {
            const float W = knee_db_;
            const float diff = x_dB - T;
            if (diff <= -W * 0.5f) {
                targetGR_dB = 0.0f;
            } else if (std::abs(diff) <= W * 0.5f) {
                const float tmp = diff + W * 0.5f;
                targetGR_dB = -grFactor * (tmp * tmp) / (2.0f * W);
            } else {
                targetGR_dB = (T - x_dB) * grFactor;
            }
        } else {
            if (x_dB > T) {
                targetGR_dB = (T - x_dB) * grFactor;
            } else {
                targetGR_dB = 0.0f;
            }
        }

        // 3. Envelope follower (ballistics)
        if (targetGR_dB < env_db_) {
            // Attack (more gain reduction needed)
            env_db_ += alphaAtt * (targetGR_dB - env_db_);
        } else {
            // Release (recovery toward 0 dB)
            env_db_ += alphaRel * (targetGR_dB - env_db_);
        }

        // Anti-denormal flush on envelope
        if (std::abs(env_db_) < 1e-6f) env_db_ = 0.0f;

        // 4. Output gain application
        const float totalGain_dB = env_db_ + makeup;
        const float linGain = std::pow(10.0f, totalGain_dB / 20.0f);

        outL[i] = sL * linGain;
        const float absOutL = std::abs(outL[i]);
        if (absOutL > maxOutMagL) maxOutMagL = absOutL;
        outSumSqL += static_cast<double>(outL[i]) * outL[i];

        if (outR) {
            outR[i] = sR * linGain;
            const float absOutR = std::abs(outR[i]);
            if (absOutR > maxOutMagR) maxOutMagR = absOutR;
            outSumSqR += static_cast<double>(outR[i]) * outR[i];
        } else {
            maxOutMagR = maxOutMagL;
            outSumSqR = outSumSqL;
        }
    }

    const float maxInMag = std::max(maxInMagL, maxInMagR);
    const float maxOutMag = std::max(maxOutMagL, maxOutMagR);

    detector_db_ = maxInMag > 1e-6f ? 20.0f * std::log10(maxInMag) : -90.0f;
    input_db_l_ = maxInMagL > 1e-6f ? 20.0f * std::log10(maxInMagL) : -90.0f;
    input_db_r_ = maxInMagR > 1e-6f ? 20.0f * std::log10(maxInMagR) : -90.0f;
    input_rms_l_ = (actualFrames > 0 && inSumSqL > 0.0)
        ? static_cast<float>(20.0 * std::log10(std::max(1e-6, std::sqrt(inSumSqL / actualFrames))))
        : -90.0f;
    input_rms_r_ = (actualFrames > 0 && inSumSqR > 0.0)
        ? static_cast<float>(20.0 * std::log10(std::max(1e-6, std::sqrt(inSumSqR / actualFrames))))
        : -90.0f;

    output_db_ = maxOutMag > 1e-6f ? 20.0f * std::log10(maxOutMag) : -90.0f;
    output_db_l_ = maxOutMagL > 1e-6f ? 20.0f * std::log10(maxOutMagL) : -90.0f;
    output_db_r_ = maxOutMagR > 1e-6f ? 20.0f * std::log10(maxOutMagR) : -90.0f;
    output_rms_l_ = (actualFrames > 0 && outSumSqL > 0.0)
        ? static_cast<float>(20.0 * std::log10(std::max(1e-6, std::sqrt(outSumSqL / actualFrames))))
        : -90.0f;
    output_rms_r_ = (actualFrames > 0 && outSumSqR > 0.0)
        ? static_cast<float>(20.0 * std::log10(std::max(1e-6, std::sqrt(outSumSqR / actualFrames))))
        : -90.0f;

    // Zero extra output channels
    for (uint32_t ch = 2; ch < outChannels; ++ch) {
        output.clear(actualFrames);
    }
}

} // namespace audiocore

