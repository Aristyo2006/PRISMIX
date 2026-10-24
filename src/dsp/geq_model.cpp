#include "geq_model.h"
#include <algorithm>
#include <cmath>

namespace {
const std::array<float, 31> kGeqFreqs = {
    20.0f, 25.0f, 31.5f, 40.0f, 50.0f, 63.0f, 80.0f, 100.0f, 125.0f, 160.0f,
    200.0f, 250.0f, 315.0f, 400.0f, 500.0f, 630.0f, 800.0f, 1000.0f, 1250.0f, 1600.0f,
    2000.0f, 2500.0f, 3150.0f, 4000.0f, 5000.0f, 6300.0f, 8000.0f, 10000.0f, 12500.0f, 16000.0f,
    20000.0f
};

const std::array<const char*, 31> kGeqLabels = {
    "20", "25", "31.5", "40", "50", "63", "80", "100", "125", "160",
    "200", "250", "315", "400", "500", "630", "800", "1k", "1.25k", "1.6k",
    "2k", "2.5k", "3.15k", "4k", "5k", "6.3k", "8k", "10k", "12.5k", "16k",
    "20k"
};
}

GraphicEqModel::GraphicEqModel() {
    reset_flat();
}

void GraphicEqModel::reset_flat() {
    gains_.fill(0.0f);
}

void GraphicEqModel::set_band_gain(size_t band, float gain_db) {
    if (band < NUM_BANDS) {
        gains_[band] = std::clamp(gain_db, MIN_GAIN_DB, MAX_GAIN_DB);
    }
}

float GraphicEqModel::get_band_gain(size_t band) const {
    if (band < NUM_BANDS) {
        return gains_[band];
    }
    return 0.0f;
}

void GraphicEqModel::copy_settings(GraphicEqModel& destination) const {
    destination.gains_ = gains_;
    destination.bypass_ = bypass_;
}

float GraphicEqModel::evaluate_response_db(float freq_hz) const {
    if (bypass_) return 0.0f;
    freq_hz = std::max(10.0f, freq_hz);
    float total_db = 0.0f;
    // 1/3 octave constant-Q filter summation model (Q ~ 4.3)
    constexpr float q = 4.318f;
    for (size_t i = 0; i < NUM_BANDS; ++i) {
        if (std::abs(gains_[i]) > 0.05f) {
            float center = kGeqFreqs[i];
            float oct = std::log2(freq_hz / center);
            total_db += gains_[i] * std::exp(-0.5f * oct * oct * (q * 0.7f));
        }
    }
    return std::clamp(total_db, -18.0f, 18.0f);
}

const std::array<float, 31>& GraphicEqModel::get_frequencies() {
    return kGeqFreqs;
}

const std::array<const char*, 31>& GraphicEqModel::get_labels() {
    return kGeqLabels;
}

