#pragma once
#include <array>
#include <cstddef>

class GraphicEqModel {
public:
    static constexpr size_t NUM_BANDS = 31;
    static constexpr float MIN_GAIN_DB = -12.0f;
    static constexpr float MAX_GAIN_DB = 12.0f;

    GraphicEqModel();

    void set_band_gain(size_t band, float gain_db);
    float get_band_gain(size_t band) const;

    void set_bypass(bool bypass) { bypass_ = bypass; }
    bool is_bypassed() const { return bypass_; }

    void reset_flat();
    void copy_settings(GraphicEqModel& destination) const;

    const std::array<float, NUM_BANDS>& get_gains() const { return gains_; }
    void set_gains(const std::array<float, NUM_BANDS>& gains) { gains_ = gains; }

    // Evaluates the combined frequency response in dB at a given frequency
    float evaluate_response_db(float freq_hz) const;

    static const std::array<float, NUM_BANDS>& get_frequencies();
    static const std::array<const char*, NUM_BANDS>& get_labels();

private:
    std::array<float, NUM_BANDS> gains_{};
    bool bypass_ = false;
};

