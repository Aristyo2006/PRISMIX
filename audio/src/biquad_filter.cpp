#include "audiocore/biquad_filter.hpp"
#include <cmath>
#include <algorithm>

namespace audiocore {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

BiquadFilter::BiquadFilter() {
    reset();
    updateCoefficients();
}

void BiquadFilter::configure(FilterType type, double sampleRate, double freqHz, double Q, double gainDb) {
    type_ = type;
    sample_rate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    freq_hz_ = freqHz;
    q_ = Q > 0.01 ? Q : 0.7071;
    gain_db_ = gainDb;
    updateCoefficients();
}

void BiquadFilter::setSampleRate(double sampleRate) {
    if (sampleRate > 0.0 && sampleRate != sample_rate_) {
        sample_rate_ = sampleRate;
        updateCoefficients();
    }
}

void BiquadFilter::reset() noexcept {
    s1_[0] = s1_[1] = 0.0f;
    s2_[0] = s2_[1] = 0.0f;
}

void BiquadFilter::updateCoefficients() noexcept {
    if (type_ == FilterType::Bypass) {
        b0_ = 1.0f;
        b1_ = 0.0f;
        b2_ = 0.0f;
        a1_ = 0.0f;
        a2_ = 0.0f;
        return;
    }

    // Unity gain optimization for peaking and shelving
    if ((type_ == FilterType::Peaking || type_ == FilterType::LowShelf || type_ == FilterType::HighShelf) &&
        std::abs(gain_db_) < 1e-4) {
        b0_ = 1.0f;
        b1_ = 0.0f;
        b2_ = 0.0f;
        a1_ = 0.0f;
        a2_ = 0.0f;
        return;
    }

    // Clamp cutoff safely below Nyquist
    const double nyquist = sample_rate_ * 0.499;
    const double fc = std::max(10.0, std::min(freq_hz_, nyquist));
    const double q = std::max(0.05, std::min(q_, 50.0));

    const double omega = 2.0 * kPi * (fc / sample_rate_);
    const double cosOmega = std::cos(omega);
    const double sinOmega = std::sin(omega);
    const double alpha = sinOmega / (2.0 * q);
    const double A = std::pow(10.0, gain_db_ / 40.0); // sqrt of linear power gain

    double b0 = 1.0, b1 = 0.0, b2 = 0.0;
    double a0 = 1.0, a1 = 0.0, a2 = 0.0;

    switch (type_) {
        case FilterType::Peaking: {
            b0 = 1.0 + alpha * A;
            b1 = -2.0 * cosOmega;
            b2 = 1.0 - alpha * A;
            a0 = 1.0 + alpha / A;
            a1 = -2.0 * cosOmega;
            a2 = 1.0 - alpha / A;
            break;
        }

        case FilterType::LowShelf: {
            const double twoSqrtAAlpha = 2.0 * std::sqrt(A) * alpha;
            b0 = A * ((A + 1.0) - (A - 1.0) * cosOmega + twoSqrtAAlpha);
            b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosOmega);
            b2 = A * ((A + 1.0) - (A - 1.0) * cosOmega - twoSqrtAAlpha);
            a0 = (A + 1.0) + (A - 1.0) * cosOmega + twoSqrtAAlpha;
            a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosOmega);
            a2 = (A + 1.0) + (A - 1.0) * cosOmega - twoSqrtAAlpha;
            break;
        }

        case FilterType::HighShelf: {
            const double twoSqrtAAlpha = 2.0 * std::sqrt(A) * alpha;
            b0 = A * ((A + 1.0) + (A - 1.0) * cosOmega + twoSqrtAAlpha);
            b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosOmega);
            b2 = A * ((A + 1.0) + (A - 1.0) * cosOmega - twoSqrtAAlpha);
            a0 = (A + 1.0) - (A - 1.0) * cosOmega + twoSqrtAAlpha;
            a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cosOmega);
            a2 = (A + 1.0) - (A - 1.0) * cosOmega - twoSqrtAAlpha;
            break;
        }

        case FilterType::LowPass: {
            b0 = (1.0 - cosOmega) * 0.5;
            b1 = 1.0 - cosOmega;
            b2 = (1.0 - cosOmega) * 0.5;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cosOmega;
            a2 = 1.0 - alpha;
            break;
        }

        case FilterType::HighPass: {
            b0 = (1.0 + cosOmega) * 0.5;
            b1 = -(1.0 + cosOmega);
            b2 = (1.0 + cosOmega) * 0.5;
            a0 = 1.0 + alpha;
            a1 = -2.0 * cosOmega;
            a2 = 1.0 - alpha;
            break;
        }

        default:
            b0 = 1.0; b1 = 0.0; b2 = 0.0;
            a0 = 1.0; a1 = 0.0; a2 = 0.0;
            break;
    }

    if (std::abs(a0) > 1e-12) {
        const double invA0 = 1.0 / a0;
        b0_ = static_cast<float>(b0 * invA0);
        b1_ = static_cast<float>(b1 * invA0);
        b2_ = static_cast<float>(b2 * invA0);
        a1_ = static_cast<float>(a1 * invA0);
        a2_ = static_cast<float>(a2 * invA0);
    } else {
        b0_ = 1.0f; b1_ = 0.0f; b2_ = 0.0f;
        a1_ = 0.0f; a2_ = 0.0f;
    }
}

void BiquadFilter::processBlock(uint32_t channel, const float* in, float* out, uint32_t frames) noexcept {
    if (channel >= 2 || in == nullptr || out == nullptr) return;

    if (type_ == FilterType::Bypass) {
        if (in != out) {
            std::copy(in, in + frames, out);
        }
        return;
    }

    float s1 = s1_[channel];
    float s2 = s2_[channel];
    const float b0 = b0_;
    const float b1 = b1_;
    const float b2 = b2_;
    const float a1 = a1_;
    const float a2 = a2_;

    for (uint32_t i = 0; i < frames; ++i) {
        const float x = in[i];
        const float y = b0 * x + s1;
        s1 = b1 * x - a1 * y + s2;
        s2 = b2 * x - a2 * y;
        out[i] = y;
    }

    if (std::abs(s1) < 1e-15f) s1 = 0.0f;
    if (std::abs(s2) < 1e-15f) s2 = 0.0f;
    s1_[channel] = s1;
    s2_[channel] = s2;
}

} // namespace audiocore

