#include "audiocore/delay_node.hpp"
#include "audiocore/audio_buffer.hpp"
#include <algorithm>
#include <cmath>

namespace audiocore {

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {
inline float sanitize(float x) noexcept {
    return (std::abs(x) < 1e-15f) ? 0.0f : x;
}

inline float softClip(float x) noexcept {
    // Symmetrical, continuous algebraic soft-saturation
    float clamped = std::clamp(x, -2.5f, 2.5f);
    return clamped / std::sqrt(1.0f + clamped * clamped);
}
} // namespace

DelayNode::DelayNode(double sampleRate)
    : sample_rate_(sampleRate > 0.0 ? sampleRate : 48000.0)
{
    // Initialize default taps
    taps_[0] = {120.0f,  0.0f, -0.3f, true};  // Tap 1: 120ms,  0dB, Pan L30%
    taps_[1] = {240.0f, -4.0f,  0.3f, true};  // Tap 2: 240ms, -4dB, Pan R30%
    taps_[2] = {360.0f, -8.0f, -0.6f, true};  // Tap 3: 360ms, -8dB, Pan L60%
    taps_[3] = {480.0f, -12.0f, 0.6f, true};  // Tap 4: 480ms, -12dB, Pan R60%

    allocateBuffers();
    updateFilterCoefficients();
    reset();
}

void DelayNode::setSampleRate(double sampleRate) noexcept {
    if (sampleRate > 0.0 && sampleRate != sample_rate_) {
        sample_rate_ = sampleRate;
        allocateBuffers();
        updateFilterCoefficients();
        reset();
    }
}

void DelayNode::allocateBuffers() {
    max_buffer_frames_ = static_cast<size_t>(sample_rate_ * MAX_DELAY_SECONDS) + 256;
    delay_buffer_left_.assign(max_buffer_frames_, 0.0f);
    delay_buffer_right_.assign(max_buffer_frames_, 0.0f);
    write_pos_ = 0;
}

void DelayNode::reset() {
    std::fill(delay_buffer_left_.begin(), delay_buffer_left_.end(), 0.0f);
    std::fill(delay_buffer_right_.begin(), delay_buffer_right_.end(), 0.0f);
    write_pos_ = 0;

    hp_state_l_ = 0.0f;
    hp_state_r_ = 0.0f;
    lp_state_l_ = 0.0f;
    lp_state_r_ = 0.0f;
    fb_out_l_ = 0.0f;
    fb_out_r_ = 0.0f;

    for (size_t t = 0; t < NUM_TAPS; ++t) {
        current_smoothed_samples_[t] = taps_[t].delayMs * 0.001f * static_cast<float>(sample_rate_);
    }
}

void DelayNode::setMasterDelayMs(float delayMs) noexcept {
    master_delay_ms_ = std::clamp(delayMs, 0.0f, static_cast<float>(MAX_DELAY_SECONDS * 1000.0));
    // Proportional scaling for taps
    taps_[0].delayMs = master_delay_ms_ * 0.25f;
    taps_[1].delayMs = master_delay_ms_ * 0.50f;
    taps_[2].delayMs = master_delay_ms_ * 0.75f;
    taps_[3].delayMs = master_delay_ms_ * 1.00f;
}

void DelayNode::setFeedback(float feedback) noexcept {
    // Upper bound strictly clamped to 0.99 for unconditional mathematical stability
    feedback_ = std::clamp(feedback, 0.0f, 0.99f);
}

void DelayNode::setMix(float mix) noexcept {
    mix_ = std::clamp(mix, 0.0f, 1.0f);
}

void DelayNode::setOutputLevelDb(float levelDb) noexcept {
    output_level_db_ = std::clamp(levelDb, -60.0f, 12.0f);
}

void DelayNode::setStereoWidth(float width) noexcept {
    stereo_width_ = std::clamp(width, 0.0f, 2.0f);
}

void DelayNode::setLowCutHz(float freqHz) noexcept {
    low_cut_hz_ = std::clamp(freqHz, 20.0f, 2000.0f);
    updateFilterCoefficients();
}

void DelayNode::setHighCutHz(float freqHz) noexcept {
    high_cut_hz_ = std::clamp(freqHz, 500.0f, 20000.0f);
    updateFilterCoefficients();
}

void DelayNode::updateFilterCoefficients() noexcept {
    const double fs = sample_rate_;
    // 1st-order Highpass: cutoff low_cut_hz_
    double wc_hp = 2.0 * M_PI * static_cast<double>(low_cut_hz_) / fs;
    hp_coeff_ = static_cast<float>(std::clamp(1.0 - std::exp(-wc_hp), 0.0001, 0.9999));

    // 1st-order Lowpass: cutoff high_cut_hz_
    double wc_lp = 2.0 * M_PI * static_cast<double>(high_cut_hz_) / fs;
    lp_coeff_ = static_cast<float>(std::clamp(1.0 - std::exp(-wc_lp), 0.0001, 0.9999));
}

void DelayNode::setTapConfig(size_t tapIndex, const DelayTapConfig& config) noexcept {
    if (tapIndex < NUM_TAPS) {
        taps_[tapIndex].delayMs = std::clamp(config.delayMs, 0.0f, static_cast<float>(MAX_DELAY_SECONDS * 1000.0));
        taps_[tapIndex].levelDb = std::clamp(config.levelDb, -60.0f, 12.0f);
        taps_[tapIndex].pan = std::clamp(config.pan, -1.0f, 1.0f);
        taps_[tapIndex].enabled = config.enabled;
    }
}

DelayTapConfig DelayNode::getTapConfig(size_t tapIndex) const noexcept {
    if (tapIndex < NUM_TAPS) return taps_[tapIndex];
    return {};
}

void DelayNode::setTapDelayMs(size_t tapIndex, float delayMs) noexcept {
    if (tapIndex < NUM_TAPS) {
        taps_[tapIndex].delayMs = std::clamp(delayMs, 0.0f, static_cast<float>(MAX_DELAY_SECONDS * 1000.0));
    }
}

float DelayNode::getTapDelayMs(size_t tapIndex) const noexcept {
    return (tapIndex < NUM_TAPS) ? taps_[tapIndex].delayMs : 0.0f;
}

void DelayNode::setTapLevelDb(size_t tapIndex, float levelDb) noexcept {
    if (tapIndex < NUM_TAPS) {
        taps_[tapIndex].levelDb = std::clamp(levelDb, -60.0f, 12.0f);
    }
}

float DelayNode::getTapLevelDb(size_t tapIndex) const noexcept {
    return (tapIndex < NUM_TAPS) ? taps_[tapIndex].levelDb : 0.0f;
}

void DelayNode::setTapPan(size_t tapIndex, float pan) noexcept {
    if (tapIndex < NUM_TAPS) {
        taps_[tapIndex].pan = std::clamp(pan, -1.0f, 1.0f);
    }
}

float DelayNode::getTapPan(size_t tapIndex) const noexcept {
    return (tapIndex < NUM_TAPS) ? taps_[tapIndex].pan : 0.0f;
}

void DelayNode::setTapEnabled(size_t tapIndex, bool enabled) noexcept {
    if (tapIndex < NUM_TAPS) {
        taps_[tapIndex].enabled = enabled;
    }
}

bool DelayNode::isTapEnabled(size_t tapIndex) const noexcept {
    return (tapIndex < NUM_TAPS) ? taps_[tapIndex].enabled : false;
}

void DelayNode::setBpm(float bpm) noexcept {
    bpm_ = std::clamp(bpm, 20.0f, 300.0f);
    if (isTempoSyncEnabled()) {
        setMasterDelayMs(calculateTempoDelayMs(bpm_, tempo_division_));
    }
}

void DelayNode::setTempoDivision(TempoDivision division) noexcept {
    tempo_division_ = division;
    if (isTempoSyncEnabled()) {
        setMasterDelayMs(calculateTempoDelayMs(bpm_, tempo_division_));
    }
}

float DelayNode::calculateTempoDelayMs(float bpm, TempoDivision division) noexcept {
    float clampedBpm = std::clamp(bpm, 20.0f, 300.0f);
    float beatMs = (60.0f / clampedBpm) * 1000.0f;
    switch (division) {
        case TempoDivision::Div_1_1:  return beatMs * 4.0f;
        case TempoDivision::Div_1_2:  return beatMs * 2.0f;
        case TempoDivision::Div_1_4:  return beatMs * 1.0f;
        case TempoDivision::Div_1_8:  return beatMs * 0.5f;
        case TempoDivision::Div_1_8T: return beatMs * (1.0f / 3.0f);
        case TempoDivision::Div_1_16: return beatMs * 0.25f;
        case TempoDivision::Div_1_32: return beatMs * 0.125f;
        default:                      return beatMs;
    }
}

void DelayNode::process(
    const AudioBuffer& input,
    AudioBuffer& output,
    uint32_t frames)
{
    if (frames == 0) return;

    if (bypassed_.load(std::memory_order_relaxed)) {
        if (&input != &output) {
            output.copyFrom(input, frames);
        }
        return;
    }

    const uint32_t inChannels = input.getChannelCount();
    const uint32_t outChannels = output.getChannelCount();
    const float* inL = input.getChannelData(0);
    const float* inR = (inChannels > 1) ? input.getChannelData(1) : inL;
    float* outL = output.getChannelData(0);
    float* outR = (outChannels > 1) ? output.getChannelData(1) : outL;

    const size_t bufCap = delay_buffer_left_.size();
    float* dBufL = delay_buffer_left_.data();
    float* dBufR = delay_buffer_right_.data();

    const float outGainLin = std::pow(10.0f, output_level_db_ / 20.0f);
    const float mix = mix_;
    const float dryGain = 1.0f - mix;
    const float wetGain = mix * outGainLin;
    const float fb = feedback_;

    // Precalculate tap gains & pans outside per-sample loop
    float tapLinearGain[NUM_TAPS];
    float tapPanL[NUM_TAPS];
    float tapPanR[NUM_TAPS];
    float tapTargetSamples[NUM_TAPS];

    for (size_t t = 0; t < NUM_TAPS; ++t) {
        if (taps_[t].enabled) {
            tapLinearGain[t] = std::pow(10.0f, taps_[t].levelDb / 20.0f);
            float pan = taps_[t].pan; // -1 to +1
            // Equal power panning
            float angle = (pan + 1.0f) * 0.25f * static_cast<float>(M_PI);
            tapPanL[t] = std::cos(angle);
            tapPanR[t] = std::sin(angle);
            tapTargetSamples[t] = taps_[t].delayMs * 0.001f * static_cast<float>(sample_rate_);
        } else {
            tapLinearGain[t] = 0.0f;
            tapPanL[t] = 0.0f;
            tapPanR[t] = 0.0f;
            tapTargetSamples[t] = 0.0f;
        }
    }

    // Slew smoothing constant (~15ms response time at standard sample rates)
    const float slewAlpha = static_cast<float>(1.0 - std::exp(-1.0 / (0.015 * sample_rate_)));

    for (uint32_t i = 0; i < frames; ++i) {
        float xL = sanitize(inL[i]);
        float xR = sanitize(inR[i]);

        // Feedback processing through highpass and lowpass filters
        float fbL = fb_out_l_;
        float fbR = fb_out_r_;

        // Highpass filter in feedback path
        hp_state_l_ += hp_coeff_ * (fbL - hp_state_l_);
        hp_state_r_ += hp_coeff_ * (fbR - hp_state_r_);
        fbL = sanitize(fbL - hp_state_l_);
        fbR = sanitize(fbR - hp_state_r_);

        // Lowpass filter in feedback path
        lp_state_l_ += lp_coeff_ * (fbL - lp_state_l_);
        lp_state_r_ += lp_coeff_ * (fbR - lp_state_r_);
        fbL = sanitize(lp_state_l_);
        fbR = sanitize(lp_state_r_);

        // Soft saturate feedback to strictly guarantee stability even if feedback exceeds 0 dB
        fbL = softClip(fbL * fb);
        fbR = softClip(fbR * fb);

        // Write current input + feedback into delay line
        dBufL[write_pos_] = sanitize(xL + fbL);
        dBufR[write_pos_] = sanitize(xR + fbR);

        // Read all active taps with fractional interpolation
        float tapSumL = 0.0f;
        float tapSumR = 0.0f;

        for (size_t t = 0; t < NUM_TAPS; ++t) {
            if (!taps_[t].enabled) continue;

            // Slew smoothed delay position to prevent zipper noise
            current_smoothed_samples_[t] += slewAlpha * (tapTargetSamples[t] - current_smoothed_samples_[t]);
            float dSamples = current_smoothed_samples_[t];

            float rPos = static_cast<float>(write_pos_) - dSamples;
            while (rPos < 0.0f) rPos += static_cast<float>(bufCap);
            while (rPos >= static_cast<float>(bufCap)) rPos -= static_cast<float>(bufCap);

            size_t idx0 = static_cast<size_t>(rPos);
            size_t idx1 = (idx0 + 1 < bufCap) ? (idx0 + 1) : 0;
            float frac = rPos - static_cast<float>(idx0);

            float sL = (1.0f - frac) * dBufL[idx0] + frac * dBufL[idx1];
            float sR = (1.0f - frac) * dBufR[idx0] + frac * dBufR[idx1];

            float tapVal = 0.5f * (sL + sR) * tapLinearGain[t];
            tapSumL += tapVal * tapPanL[t];
            tapSumR += tapVal * tapPanR[t];
        }

        // Stereo width control (Mid/Side processing)
        float mid = 0.5f * (tapSumL + tapSumR);
        float side = 0.5f * (tapSumR - tapSumL) * stereo_width_;
        float wetL = mid - side;
        float wetR = mid + side;

        // Feedback output updated for next sample (from master tap 3 or tapSum)
        fb_out_l_ = wetL;
        fb_out_r_ = wetR;

        // Output mix (Dry/Wet)
        outL[i] = sanitize(xL * dryGain + wetL * wetGain);
        if (outChannels > 1) {
            outR[i] = sanitize(xR * dryGain + wetR * wetGain);
        }

        write_pos_ = (write_pos_ + 1 < bufCap) ? (write_pos_ + 1) : 0;
    }
}

} // namespace audiocore

