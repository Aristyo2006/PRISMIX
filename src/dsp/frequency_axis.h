#pragma once
#include <cmath>
#include <algorithm>

// Unified logarithmic frequency axis mapping utility
// Provides mathematical conversion between frequency in Hz and normalized / pixel coordinates.
// Formula: u(f) = ln(f / fMin) / ln(fMax / fMin) = log10(f / fMin) / log10(fMax / fMin)
//          x(f) = xMin + u(f) * (xMax - xMin)
class FrequencyAxis {
public:
    static constexpr float MIN_FREQ = 20.0f;
    static constexpr float MAX_FREQ = 20000.0f;

    // Convert frequency in Hz to normalized coordinate u in [0.0, 1.0]
    static inline float frequency_to_norm(float freq, float fMin = MIN_FREQ, float fMax = MAX_FREQ) {
        if (fMin <= 0.0f) fMin = MIN_FREQ;
        if (fMax <= fMin) fMax = fMin + 1.0f;
        if (freq <= 0.0f || !std::isfinite(freq)) return 0.0f;
        const float clampedFreq = std::clamp(freq, fMin, fMax);
        const float norm = std::log(clampedFreq / fMin) / std::log(fMax / fMin);
        return std::clamp(norm, 0.0f, 1.0f);
    }

    // Inverse: Convert normalized coordinate u in [0.0, 1.0] to frequency in Hz
    static inline float norm_to_frequency(float norm, float fMin = MIN_FREQ, float fMax = MAX_FREQ) {
        if (fMin <= 0.0f) fMin = MIN_FREQ;
        if (fMax <= fMin) fMax = fMin + 1.0f;
        if (!std::isfinite(norm)) return fMin;
        const float clampedNorm = std::clamp(norm, 0.0f, 1.0f);
        return fMin * std::pow(fMax / fMin, clampedNorm);
    }

    // Convert frequency in Hz directly to pixel coordinate x given plotting range [xMin, xMax]
    static inline float frequency_to_x(float freq, float xMin, float xMax, float fMin = MIN_FREQ, float fMax = MAX_FREQ) {
        const float u = frequency_to_norm(freq, fMin, fMax);
        return xMin + u * (xMax - xMin);
    }

    // Inverse: Convert pixel coordinate x to frequency in Hz given plotting range [xMin, xMax]
    static inline float x_to_frequency(float x, float xMin, float xMax, float fMin = MIN_FREQ, float fMax = MAX_FREQ) {
        if (xMax <= xMin || !std::isfinite(x)) return fMin;
        const float u = std::clamp((x - xMin) / (xMax - xMin), 0.0f, 1.0f);
        return norm_to_frequency(u, fMin, fMax);
    }
};

