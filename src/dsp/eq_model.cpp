#include "eq_model.h"
#include "../model/mixer_state.h"
#include <algorithm>
#include <cmath>

float eq_single_band_response_db(float hz, int b, const ChannelState& ch) noexcept {
    if (b < 0 || b >= 4 || !ch.eq_enabled[b]) return 0.0f;
    const float freq = std::clamp(ch.eq_freq[b], 20.0f, 20000.0f);
    const float q = std::clamp(ch.eq_q[b], 0.1f, 20.0f);
    const float gain = std::clamp(ch.eq_gain[b], -18.0f, 18.0f);
    const int type = ch.eq_filter_type[b];
    hz = std::max(10.0f, hz);

    switch (type) {
        case 0: { // Low Shelf
            const float r = hz / freq;
            const float slope = std::max(0.4f, q * 1.5f);
            return gain / (1.0f + std::pow(r, 2.0f * slope));
        }
        case 1: { // Bell / Peaking EQ (Gaussian peak in logarithmic octaves)
            const float oct = std::log2(hz / freq);
            const float bw_oct = 1.0f / std::max(0.2f, q);
            const float sigma = bw_oct / 1.665f; // FWHM to standard deviation
            const float z = oct / std::max(0.01f, sigma);
            return gain * std::exp(-0.5f * z * z);
        }
        case 2: { // High Shelf
            const float r = freq / hz;
            const float slope = std::max(0.4f, q * 1.5f);
            return gain / (1.0f + std::pow(r, 2.0f * slope));
        }
        case 3: { // HPF 18 dB/octave (3rd-Order Butterworth High-Pass)
            // |H(f)|^2 = 1 / (1 + (fc / f)^6)
            // Gain_dB = -10 * log10(1 + (fc / f)^6)
            const float ratio = freq / hz;
            const float r6 = std::pow(ratio, 6.0f);
            const float mag_sq = 1.0f / (1.0f + r6);
            return 10.0f * std::log10(std::max(1e-6f, mag_sq));
        }
        case 4: { // LPF 18 dB/octave (3rd-Order Butterworth Low-Pass)
            // |H(f)|^2 = 1 / (1 + (f / fc)^6)
            // Gain_dB = -10 * log10(1 + (f / fc)^6)
            const float ratio = hz / freq;
            const float r6 = std::pow(ratio, 6.0f);
            const float mag_sq = 1.0f / (1.0f + r6);
            return 10.0f * std::log10(std::max(1e-6f, mag_sq));
        }
        default: {
            const float oct = std::log2(hz / freq);
            const float bw_oct = 1.0f / std::max(0.2f, q);
            const float sigma = bw_oct / 1.665f;
            const float z = oct / std::max(0.01f, sigma);
            return gain * std::exp(-0.5f * z * z);
        }
    }
}

float eq_response_db(float hz, const ChannelState& ch) noexcept {
    if (ch.eq_bypass) return 0.0f;
    hz = std::max(10.0f, hz);
    float db = 0.0f;
    for (int b = 0; b < 4; ++b) {
        db += eq_single_band_response_db(hz, b, ch);
    }
    return std::clamp(db, -36.0f, 24.0f);
}
