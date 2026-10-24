#include "audiocore/reverb_node.hpp"
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

// Mutually prime delay lengths at 44.1 kHz for Left channel combs
constexpr size_t BASE_COMB_LENGTHS_L[8] = {
    1117, 1187, 1279, 1361, 1423, 1493, 1559, 1621
};

// Mutually prime delay lengths at 44.1 kHz for Right channel combs (decorrelated from Left)
constexpr size_t BASE_COMB_LENGTHS_R[8] = {
    1129, 1213, 1297, 1381, 1439, 1511, 1571, 1637
};

// Mutually prime delay lengths at 44.1 kHz for Left channel allpasses
constexpr size_t BASE_ALLPASS_LENGTHS_L[4] = {
    557, 443, 347, 227
};

// Mutually prime delay lengths at 44.1 kHz for Right channel allpasses (decorrelated from Left)
constexpr size_t BASE_ALLPASS_LENGTHS_R[4] = {
    569, 457, 353, 233
};
} // namespace

ReverbNode::ReverbNode(double sampleRate)
    : sample_rate_(sampleRate > 0.0 ? sampleRate : 48000.0)
    , type_(ReverbType::Hall)
    , mix_(0.35f)
{
    allocateBuffers();
    loadTemplate(ReverbType::Hall);
    mix_ = 0.35f; // Maintain default mix 0.35 for REVERB-002 compatibility
    updateParameters();
    reset();
}

void ReverbNode::setSampleRate(double sampleRate) noexcept {
    if (sampleRate > 0.0 && sampleRate != sample_rate_) {
        sample_rate_ = sampleRate;
        allocateBuffers();
        updateParameters();
        reset();
    }
}

void ReverbNode::allocateBuffers() {
    const double rateScale = sample_rate_ / 44100.0;

    // Pre-delay buffer: preallocate power-of-two capacity
    pre_delay_buffer_l_.assign(PRE_DELAY_CAPACITY, 0.0f);
    pre_delay_buffer_r_.assign(PRE_DELAY_CAPACITY, 0.0f);
    pre_delay_write_ = 0;

    // Allocate Comb filters with sample-rate scaling
    for (size_t i = 0; i < NUM_COMBS; ++i) {
        size_t lenL = static_cast<size_t>(std::round(BASE_COMB_LENGTHS_L[i] * rateScale));
        size_t lenR = static_cast<size_t>(std::round(BASE_COMB_LENGTHS_R[i] * rateScale));
        combs_l_[i].init(lenL);
        combs_r_[i].init(lenR);
    }

    // Allocate Allpass filters with sample-rate scaling
    for (size_t i = 0; i < NUM_ALLPASS; ++i) {
        size_t lenL = static_cast<size_t>(std::round(BASE_ALLPASS_LENGTHS_L[i] * rateScale));
        size_t lenR = static_cast<size_t>(std::round(BASE_ALLPASS_LENGTHS_R[i] * rateScale));
        allpass_l_[i].init(lenL);
        allpass_r_[i].init(lenR);
    }
}

void ReverbNode::reset() {
    std::fill(pre_delay_buffer_l_.begin(), pre_delay_buffer_l_.end(), 0.0f);
    std::fill(pre_delay_buffer_r_.begin(), pre_delay_buffer_r_.end(), 0.0f);
    pre_delay_write_ = 0;
    snapPreDelayToTarget();

    in_hp_state_l_ = 0.0f;
    in_hp_state_r_ = 0.0f;
    out_lp_state_l_ = 0.0f;
    out_lp_state_r_ = 0.0f;

    for (size_t i = 0; i < NUM_COMBS; ++i) {
        combs_l_[i].reset();
        combs_r_[i].reset();
    }
    for (size_t i = 0; i < NUM_ALLPASS; ++i) {
        allpass_l_[i].reset();
        allpass_r_[i].reset();
    }
}

const ReverbTemplateParams& ReverbNode::getTemplate(ReverbType type) noexcept {
    static constexpr ReverbTemplateParams templates[4] = {
        // Room
        {"Room", 15.0f, 0.75f, 0.50f, 0.75f, 150.0f, 10000.0f, 0.80f, 0.25f},
        // Hall
        {"Hall", 30.0f, 2.50f, 0.40f, 0.85f, 120.0f, 9000.0f, 1.00f, 0.30f},
        // Plate
        {"Plate", 20.0f, 2.00f, 0.25f, 0.95f, 180.0f, 14000.0f, 1.00f, 0.25f},
        // Ambience
        {"Ambience", 5.0f, 0.35f, 0.65f, 0.70f, 200.0f, 8000.0f, 0.75f, 0.20f}
    };
    size_t idx = static_cast<size_t>(type);
    if (idx >= 4) idx = 1;
    return templates[idx];
}

void ReverbNode::loadTemplate(ReverbType type) noexcept {
    type_ = type;
    const auto& t = getTemplate(type);
    pre_delay_ms_.store(t.preDelayMs, std::memory_order_relaxed);
    decay_s_ = t.decaySeconds;
    damping_ = t.damping;
    diffusion_ = t.diffusion;
    low_cut_hz_ = t.lowCutHz;
    high_cut_hz_ = t.highCutHz;
    width_ = t.width;
    mix_ = t.mix;
    target_delay_samples_ = t.preDelayMs * 0.001 * sample_rate_;
    snapPreDelayToTarget();
    updateParameters();
}

void ReverbNode::resetToTemplate(ReverbType type) noexcept {
    loadTemplate(type);
}

void ReverbNode::setType(ReverbType type) noexcept {
    loadTemplate(type);
}

void ReverbNode::setPreDelayMs(float preDelayMs, bool snap) noexcept {
    float clampedMs = std::clamp(preDelayMs, 0.0f, 250.0f);
    pre_delay_ms_.store(clampedMs, std::memory_order_relaxed);
    target_delay_samples_ = clampedMs * 0.001 * sample_rate_;
    if (snap) {
        snapPreDelayToTarget();
    }
}

uint32_t ReverbNode::getPreDelaySamples() const noexcept {
    float ms = pre_delay_ms_.load(std::memory_order_relaxed);
    return static_cast<uint32_t>(std::round(ms * 0.001 * sample_rate_));
}

double ReverbNode::getPreDelaySamplesFractional() const noexcept {
    float ms = pre_delay_ms_.load(std::memory_order_relaxed);
    return static_cast<double>(ms * 0.001 * sample_rate_);
}

void ReverbNode::snapPreDelayToTarget() noexcept {
    float ms = pre_delay_ms_.load(std::memory_order_relaxed);
    target_delay_samples_ = ms * 0.001 * sample_rate_;
    current_delay_samples_ = target_delay_samples_;
}

void ReverbNode::processPreDelay(const AudioBuffer& input, AudioBuffer& output, uint32_t frames) noexcept {
    if (frames == 0) return;
    const uint32_t inChannels = input.getChannelCount();
    const uint32_t outChannels = output.getChannelCount();
    const float* inL = input.getChannelData(0);
    const float* inR = (inChannels > 1) ? input.getChannelData(1) : inL;
    float* outL = output.getChannelData(0);
    float* outR = (outChannels > 1) ? output.getChannelData(1) : outL;

    if (!pre_delay_enabled_.load(std::memory_order_relaxed)) {
        if (&input != &output) {
            output.copyFrom(input, frames);
        }
        return;
    }

    for (uint32_t n = 0; n < frames; ++n) {
        float xL = sanitize(inL[n]);
        float xR = sanitize(inR[n]);

        pre_delay_buffer_l_[pre_delay_write_] = xL;
        pre_delay_buffer_r_[pre_delay_write_] = xR;

        current_delay_samples_ += slew_coeff_ * (target_delay_samples_ - current_delay_samples_);
        double d = current_delay_samples_;

        float yL = xL;
        float yR = xR;

        if (d >= 1.0) {
            size_t dInt = static_cast<size_t>(d);
            float f = static_cast<float>(d - dInt);
            size_t readPos = (pre_delay_write_ + PRE_DELAY_CAPACITY - dInt) & PRE_DELAY_MASK;

            if (f < 1e-5f) {
                yL = pre_delay_buffer_l_[readPos];
                yR = pre_delay_buffer_r_[readPos];
            } else {
                size_t idx0 = (readPos + 1) & PRE_DELAY_MASK;
                size_t idx1 = readPos;
                size_t idx2 = (readPos + PRE_DELAY_CAPACITY - 1) & PRE_DELAY_MASK;
                size_t idx3 = (readPos + PRE_DELAY_CAPACITY - 2) & PRE_DELAY_MASK;

                float p0L = pre_delay_buffer_l_[idx0];
                float p1L = pre_delay_buffer_l_[idx1];
                float p2L = pre_delay_buffer_l_[idx2];
                float p3L = pre_delay_buffer_l_[idx3];
                float c0L = p1L;
                float c1L = 0.5f * (p2L - p0L);
                float c2L = p0L - 2.5f * p1L + 2.0f * p2L - 0.5f * p3L;
                float c3L = 0.5f * (p3L - p0L) + 1.5f * (p1L - p2L);
                yL = c0L + f * (c1L + f * (c2L + f * c3L));

                float p0R = pre_delay_buffer_r_[idx0];
                float p1R = pre_delay_buffer_r_[idx1];
                float p2R = pre_delay_buffer_r_[idx2];
                float p3R = pre_delay_buffer_r_[idx3];
                float c0R = p1R;
                float c1R = 0.5f * (p2R - p0R);
                float c2R = p0R - 2.5f * p1R + 2.0f * p2R - 0.5f * p3R;
                float c3R = 0.5f * (p3R - p0R) + 1.5f * (p1R - p2R);
                yR = c0R + f * (c1R + f * (c2R + f * c3R));
            }
        } else if (d > 0.0) {
            float f = static_cast<float>(d);
            size_t idx0 = pre_delay_write_;
            size_t idx1 = (pre_delay_write_ + PRE_DELAY_CAPACITY - 1) & PRE_DELAY_MASK;
            yL = (1.0f - f) * pre_delay_buffer_l_[idx0] + f * pre_delay_buffer_l_[idx1];
            yR = (1.0f - f) * pre_delay_buffer_r_[idx0] + f * pre_delay_buffer_r_[idx1];
        } else {
            yL = pre_delay_buffer_l_[pre_delay_write_];
            yR = pre_delay_buffer_r_[pre_delay_write_];
        }

        pre_delay_write_ = (pre_delay_write_ + 1) & PRE_DELAY_MASK;
        outL[n] = sanitize(yL);
        if (outChannels > 1) {
            outR[n] = sanitize(yR);
        }
    }
}

void ReverbNode::setSize(float size) noexcept {
    size_ = std::clamp(size, 0.0f, 1.0f);
    updateParameters();
}

void ReverbNode::setDecay(float decaySeconds) noexcept {
    decay_s_ = std::clamp(decaySeconds, 0.1f, 10.0f);
    updateParameters();
}

void ReverbNode::setDamping(float damping) noexcept {
    damping_ = std::clamp(damping, 0.0f, 1.0f);
    updateParameters();
}

void ReverbNode::setDiffusion(float diffusion) noexcept {
    diffusion_ = std::clamp(diffusion, 0.0f, 1.0f);
    updateParameters();
}

void ReverbNode::setWidth(float width) noexcept {
    width_ = std::clamp(width, 0.0f, 2.0f);
}

void ReverbNode::setLowCutHz(float freqHz) noexcept {
    low_cut_hz_ = std::clamp(freqHz, 20.0f, 1000.0f);
    updateParameters();
}

void ReverbNode::setHighCutHz(float freqHz) noexcept {
    high_cut_hz_ = std::clamp(freqHz, 2000.0f, 20000.0f);
    updateParameters();
}

void ReverbNode::setMix(float mix) noexcept {
    mix_ = std::clamp(mix, 0.0f, 1.0f);
}

void ReverbNode::updateParameters() noexcept {
    const float fs = static_cast<float>(sample_rate_);

    target_delay_samples_ = pre_delay_ms_.load(std::memory_order_relaxed) * 0.001 * sample_rate_;
    slew_coeff_ = 1.0 - std::exp(-1.0 / (0.020 * sample_rate_));

    // Frequency-dependent feedback damping in combs
    // Damping 0% -> damp=0 (full HF preservation); Damping 100% -> damp=0.85 (rapid HF decay)
    const float combDamp = std::clamp(damping_ * 0.85f, 0.0f, 0.85f);

    // Each comb feedback calculated explicitly from target RT60 decay time:
    // g_i = exp(-6.907755 * len_i / (decay_s * fs))
    for (size_t i = 0; i < NUM_COMBS; ++i) {
        float lenL = static_cast<float>(combs_l_[i].buffer.size());
        float lenR = static_cast<float>(combs_r_[i].buffer.size());

        float fbL = std::exp(-6.907755f * lenL / (decay_s_ * fs));
        float fbR = std::exp(-6.907755f * lenR / (decay_s_ * fs));

        // Incorporate room size modifier slightly on feedback while bounding strictly <= 0.98
        fbL = std::clamp(fbL * (0.85f + 0.15f * size_), 0.10f, 0.98f);
        fbR = std::clamp(fbR * (0.85f + 0.15f * size_), 0.10f, 0.98f);

        combs_l_[i].feedback = fbL;
        combs_l_[i].damp = combDamp;
        combs_r_[i].feedback = fbR;
        combs_r_[i].damp = combDamp;
    }

    // Allpass diffusion coefficient (Schroeder canonical allpass)
    float allpassFb = std::clamp(0.40f + diffusion_ * 0.22f, 0.20f, 0.65f);
    for (size_t i = 0; i < NUM_ALLPASS; ++i) {
        allpass_l_[i].feedback = allpassFb;
        allpass_r_[i].feedback = allpassFb;
    }

    // Reverb Input Highpass filter
    double wc_hp = 2.0 * M_PI * static_cast<double>(low_cut_hz_) / sample_rate_;
    in_hp_coeff_ = static_cast<float>(std::clamp(1.0 - std::exp(-wc_hp), 0.0001, 0.9999));

    // Reverb Output Lowpass filter
    double wc_lp = 2.0 * M_PI * static_cast<double>(high_cut_hz_) / sample_rate_;
    out_lp_coeff_ = static_cast<float>(std::clamp(1.0 - std::exp(-wc_lp), 0.0001, 0.9999));
}

void ReverbNode::process(
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

    // Mix Law:
    // Insert mode: dry = (1 - mix), wet = mix
    // SendReturn mode: dry = 0, wet = 1.0 (or mix)
    const float mix = mix_;
    const float dryGain = (routing_mode_ == ReverbRoutingMode::Insert) ? (1.0f - mix) : 0.0f;
    const float wetGain = (routing_mode_ == ReverbRoutingMode::Insert) ? mix : 1.0f;
    const float width = width_;

    for (uint32_t n = 0; n < frames; ++n) {
        float xL = sanitize(inL[n]);
        float xR = sanitize(inR[n]);

        // 1. Reverb Input Highpass filter (20 - 1000 Hz)
        in_hp_state_l_ += in_hp_coeff_ * (xL - in_hp_state_l_);
        in_hp_state_r_ += in_hp_coeff_ * (xR - in_hp_state_r_);
        float filL = sanitize(xL - in_hp_state_l_);
        float filR = sanitize(xR - in_hp_state_r_);

        // 2. Pre-delay buffer write
        pre_delay_buffer_l_[pre_delay_write_] = filL;
        pre_delay_buffer_r_[pre_delay_write_] = filR;

        float preL = filL;
        float preR = filR;

        if (pre_delay_enabled_.load(std::memory_order_relaxed)) {
            current_delay_samples_ += slew_coeff_ * (target_delay_samples_ - current_delay_samples_);
            double d = current_delay_samples_;
            if (d >= 1.0) {
                size_t dInt = static_cast<size_t>(d);
                float f = static_cast<float>(d - dInt);
                size_t readPos = (pre_delay_write_ + PRE_DELAY_CAPACITY - dInt) & PRE_DELAY_MASK;

                if (f < 1e-5f) {
                    preL = pre_delay_buffer_l_[readPos];
                    preR = pre_delay_buffer_r_[readPos];
                } else {
                    size_t idx0 = (readPos + 1) & PRE_DELAY_MASK;
                    size_t idx1 = readPos;
                    size_t idx2 = (readPos + PRE_DELAY_CAPACITY - 1) & PRE_DELAY_MASK;
                    size_t idx3 = (readPos + PRE_DELAY_CAPACITY - 2) & PRE_DELAY_MASK;

                    float p0L = pre_delay_buffer_l_[idx0];
                    float p1L = pre_delay_buffer_l_[idx1];
                    float p2L = pre_delay_buffer_l_[idx2];
                    float p3L = pre_delay_buffer_l_[idx3];
                    float c0L = p1L;
                    float c1L = 0.5f * (p2L - p0L);
                    float c2L = p0L - 2.5f * p1L + 2.0f * p2L - 0.5f * p3L;
                    float c3L = 0.5f * (p3L - p0L) + 1.5f * (p1L - p2L);
                    preL = c0L + f * (c1L + f * (c2L + f * c3L));

                    float p0R = pre_delay_buffer_r_[idx0];
                    float p1R = pre_delay_buffer_r_[idx1];
                    float p2R = pre_delay_buffer_r_[idx2];
                    float p3R = pre_delay_buffer_r_[idx3];
                    float c0R = p1R;
                    float c1R = 0.5f * (p2R - p0R);
                    float c2R = p0R - 2.5f * p1R + 2.0f * p2R - 0.5f * p3R;
                    float c3R = 0.5f * (p3R - p0R) + 1.5f * (p1R - p2R);
                    preR = c0R + f * (c1R + f * (c2R + f * c3R));
                }
            } else if (d > 0.0) {
                float f = static_cast<float>(d);
                size_t idx0 = pre_delay_write_;
                size_t idx1 = (pre_delay_write_ + PRE_DELAY_CAPACITY - 1) & PRE_DELAY_MASK;
                preL = (1.0f - f) * pre_delay_buffer_l_[idx0] + f * pre_delay_buffer_l_[idx1];
                preR = (1.0f - f) * pre_delay_buffer_r_[idx0] + f * pre_delay_buffer_r_[idx1];
            } else {
                preL = pre_delay_buffer_l_[pre_delay_write_];
                preR = pre_delay_buffer_r_[pre_delay_write_];
            }
        } else {
            current_delay_samples_ = 0.0;
        }

        pre_delay_write_ = (pre_delay_write_ + 1) & PRE_DELAY_MASK;

        // 3. Stereo input rotation / cross-coupling (orthonormal energy-preserving matrix)
        constexpr float rotC = 0.9394f;
        constexpr float rotS = 0.3429f;
        float rotL = rotC * preL + rotS * preR;
        float rotR = rotS * preL + rotC * preR;

        // 4. Input Allpass diffusion stage (Early Diffusion)
        float diffL = allpass_l_[0].process(rotL);
        float diffR = allpass_r_[0].process(rotR);
        diffL = allpass_l_[1].process(diffL);
        diffR = allpass_r_[1].process(diffR);

        // Early reflection component from early diffusion
        float earlyRefL = 0.35f * diffL;
        float earlyRefR = 0.35f * diffR;

        // 5. Parallel Comb filters with individual T60 decay and HF feedback damping
        float combSumL = 0.0f;
        float combSumR = 0.0f;
        for (size_t c = 0; c < NUM_COMBS; ++c) {
            combSumL += combs_l_[c].process(diffL);
            combSumR += combs_r_[c].process(diffR);
        }

        // Scale comb accumulation (1 / sqrt(8))
        constexpr float combScale = 0.353553f;
        combSumL *= combScale;
        combSumR *= combScale;

        // 6. Output Allpass diffusion stages (canonical Schroeder, |H(e^jw)| == 1)
        float outDiffL = allpass_l_[2].process(combSumL);
        float outDiffR = allpass_r_[2].process(combSumR);
        outDiffL = allpass_l_[3].process(outDiffL);
        outDiffR = allpass_r_[3].process(outDiffR);

        // Sum early reflections + late diffuse tail
        float revL = earlyRefL + outDiffL;
        float revR = earlyRefR + outDiffR;

        // 7. Stereo Decorrelation & Width Control (Mid / Side matrix)
        float mid = 0.5f * (revL + revR);
        float side = 0.5f * (revR - revL) * width;
        float wetL = mid - side;
        float wetR = mid + side;

        // 8. Reverb Output LPF (High Cut filter)
        out_lp_state_l_ += out_lp_coeff_ * (wetL - out_lp_state_l_);
        out_lp_state_r_ += out_lp_coeff_ * (wetR - out_lp_state_r_);
        wetL = sanitize(out_lp_state_l_);
        wetR = sanitize(out_lp_state_r_);

        // 9. Final Dry/Wet output mixing
        outL[n] = sanitize(xL * dryGain + wetL * wetGain);
        if (outChannels > 1) {
            outR[n] = sanitize(xR * dryGain + wetR * wetGain);
        }
    }
}

} // namespace audiocore
