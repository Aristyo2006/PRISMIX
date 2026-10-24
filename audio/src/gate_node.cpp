#include "audiocore/gate_node.hpp"
#include "audiocore/audio_buffer.hpp"
#include <cmath>
#include <algorithm>

namespace audiocore {

GateNode::GateNode(double sampleRate)
    : sample_rate_(sampleRate > 0.0 ? sampleRate : 48000.0)
{
    updateTimeConstants();
    reset();
}

void GateNode::setSampleRate(double sampleRate) noexcept {
    if (sampleRate > 0.0 && sampleRate != sample_rate_) {
        sample_rate_ = sampleRate;
        updateTimeConstants();
    }
}

void GateNode::setThresholdDb(float thresholdDb) noexcept {
    threshold_db_ = std::clamp(thresholdDb, -80.0f, 0.0f);
}

void GateNode::setAttackMs(float attackMs) noexcept {
    attack_ms_ = std::clamp(attackMs, 0.1f, 200.0f);
    updateTimeConstants();
}

void GateNode::setHoldMs(float holdMs) noexcept {
    hold_ms_ = std::clamp(holdMs, 0.0f, 1000.0f);
    updateTimeConstants();
}

void GateNode::setReleaseMs(float releaseMs) noexcept {
    release_ms_ = std::clamp(releaseMs, 10.0f, 2000.0f);
    updateTimeConstants();
}

void GateNode::setRangeDb(float rangeDb) noexcept {
    range_db_ = std::clamp(rangeDb, -80.0f, 0.0f);
    if (state_ == GateState::Closed) {
        current_gain_db_ = range_db_;
    }
}

void GateNode::updateTimeConstants() noexcept {
    const double fs = (sample_rate_ > 0.0) ? sample_rate_ : 48000.0;
    const double dtAtt = static_cast<double>(attack_ms_) * 0.001;
    const double dtRel = static_cast<double>(release_ms_) * 0.001;

    alpha_att_ = static_cast<float>(1.0 - std::exp(-1.0 / (dtAtt * fs)));
    alpha_rel_ = static_cast<float>(1.0 - std::exp(-1.0 / (dtRel * fs)));

    hold_samples_total_ = static_cast<uint32_t>(static_cast<double>(hold_ms_) * 0.001 * fs);
}

void GateNode::reset() {
    state_ = GateState::Closed;
    current_gain_db_ = range_db_;
    hold_counter_samples_ = 0;
    detector_db_ = -90.0f;
}

float GateNode::computeStaticOutputDb(float inputDb) const noexcept {
    if (bypassed_.load(std::memory_order_relaxed)) {
        return inputDb;
    }
    if (inputDb < threshold_db_) {
        return std::max(-90.0f, inputDb + range_db_);
    }
    return inputDb;
}

const char* GateNode::stateToString(GateState state) noexcept {
    switch (state) {
        case GateState::Closed:  return "CLOSED";
        case GateState::Attack:  return "ATTACK";
        case GateState::Open:    return "OPEN";
        case GateState::Hold:    return "HOLD";
        case GateState::Release: return "RELEASE";
        default:                 return "UNKNOWN";
    }
}

void GateNode::process(
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
        current_gain_db_ = 0.0f;
        state_ = GateState::Open;
        float maxMag = 0.0f;
        const float* inL = input.getChannelData(0);
        const float* inR = (inChannels > 1) ? input.getChannelData(1) : inL;
        for (uint32_t i = 0; i < actualFrames; ++i) {
            float mag = std::abs(inL[i]);
            if (inChannels > 1) mag = std::max(mag, std::abs(inR[i]));
            if (mag > maxMag) maxMag = mag;
        }
        detector_db_ = maxMag > 1e-6f ? 20.0f * std::log10(maxMag) : -90.0f;
        return;
    }

    const float* inL = input.getChannelData(0);
    const float* inR = (inChannels > 1) ? input.getChannelData(1) : inL;
    float* outL = output.getChannelData(0);
    float* outR = (outChannels > 1) ? output.getChannelData(1) : nullptr;

    const float T = threshold_db_;
    const float rangeFloor = range_db_;
    const float alphaAtt = alpha_att_;
    const float alphaRel = alpha_rel_;
    const bool isStereo = stereo_linked_.load(std::memory_order_relaxed);

    float maxInMag = 0.0f;

    for (uint32_t i = 0; i < actualFrames; ++i) {
        const float sL = inL[i];
        const float sR = inR[i];

        // 1. Peak envelope detector (stereo linked or left channel if mono)
        float peakMag = std::abs(sL);
        if (isStereo && inChannels > 1) {
            peakMag = std::max(peakMag, std::abs(sR));
        }
        if (peakMag > maxInMag) maxInMag = peakMag;

        const float x_dB = 20.0f * std::log10(std::max(peakMag, 1e-6f));

        // 2. Gate Dynamic State Machine
        if (x_dB >= T) {
            // Signal above threshold -> Open or Opening
            if (state_ == GateState::Closed || state_ == GateState::Release || state_ == GateState::Hold) {
                state_ = GateState::Attack;
            }

            if (state_ == GateState::Attack) {
                current_gain_db_ += alphaAtt * (0.0f - current_gain_db_);
                if (current_gain_db_ >= -0.1f) {
                    current_gain_db_ = 0.0f;
                    state_ = GateState::Open;
                }
            } else if (state_ == GateState::Open) {
                current_gain_db_ = 0.0f;
            }

            hold_counter_samples_ = hold_samples_total_;
        } else {
            // Signal below threshold -> Hold, Release, or Closed
            if (state_ == GateState::Open || state_ == GateState::Attack) {
                if (hold_samples_total_ > 0) {
                    state_ = GateState::Hold;
                    hold_counter_samples_ = hold_samples_total_;
                } else {
                    state_ = GateState::Release;
                }
            }

            if (state_ == GateState::Hold) {
                if (hold_counter_samples_ > 0) {
                    --hold_counter_samples_;
                } else {
                    state_ = GateState::Release;
                }
                current_gain_db_ = 0.0f;
            }

            if (state_ == GateState::Release) {
                current_gain_db_ += alphaRel * (rangeFloor - current_gain_db_);
                if (current_gain_db_ <= rangeFloor + 0.1f) {
                    current_gain_db_ = rangeFloor;
                    state_ = GateState::Closed;
                }
            } else if (state_ == GateState::Closed) {
                current_gain_db_ = rangeFloor;
            }
        }

        // Anti-denormal flush
        if (std::abs(current_gain_db_) < 1e-6f) current_gain_db_ = 0.0f;
        if (current_gain_db_ < rangeFloor) current_gain_db_ = rangeFloor;

        // 3. Linear gain application
        const float linGain = std::pow(10.0f, current_gain_db_ / 20.0f);

        outL[i] = sL * linGain;
        if (outR) {
            outR[i] = sR * linGain;
        }
    }

    detector_db_ = maxInMag > 1e-6f ? 20.0f * std::log10(maxInMag) : -90.0f;

    // Clear extra channels if any
    for (uint32_t ch = 2; ch < outChannels; ++ch) {
        output.clear(actualFrames);
    }
}

} // namespace audiocore
