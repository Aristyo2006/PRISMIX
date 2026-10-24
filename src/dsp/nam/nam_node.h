#pragma once

#include "audiocore/audio_node.hpp"
#include "audiocore/audio_buffer.hpp"
#include "nam_model.h"
#include <atomic>
#include <memory>
#include <vector>
#include <chrono>

namespace prismix::nam {

struct NamTelemetry {
    float in_peak_db = -90.0f;
    float out_peak_db = -90.0f;
    float cpu_time_us = 0.0f;
    bool is_clipping = false;
};

class NamAudioNode final : public audiocore::AudioNode {
public:
    explicit NamAudioNode(double sample_rate = 48000.0,
                          uint32_t max_block_size = 2048);
    ~NamAudioNode() override = default;

    void reset() override;

    void process(const audiocore::AudioBuffer& input,
                 audiocore::AudioBuffer& output,
                 uint32_t frames) override;

    // Model management (safe to call from control thread)
    void setModel(std::shared_ptr<NamModel> model);
    std::shared_ptr<NamModel> getModel() const;

    // Parameters
    void setInputGainDb(float db) noexcept { input_gain_db_.store(db, std::memory_order_relaxed); }
    float getInputGainDb() const noexcept { return input_gain_db_.load(std::memory_order_relaxed); }

    void setOutputLevelDb(float db) noexcept { output_level_db_.store(db, std::memory_order_relaxed); }
    float getOutputLevelDb() const noexcept { return output_level_db_.load(std::memory_order_relaxed); }

    void setBypassed(bool b) noexcept { bypassed_.store(b, std::memory_order_relaxed); }
    bool isBypassed() const noexcept { return bypassed_.load(std::memory_order_relaxed); }

    // Telemetry
    NamTelemetry getTelemetry() const noexcept;

private:
    void processLinear(const float* in, float* out, uint32_t frames, const LinearConfig& cfg);
    void processLstm(const float* in, float* out, uint32_t frames, const LstmConfig& cfg);

    double sample_rate_ = 48000.0;
    std::shared_ptr<NamModel> active_model_;
    mutable std::atomic<bool> model_updated_{false};

    std::atomic<float> input_gain_db_{0.0f};
    std::atomic<float> output_level_db_{0.0f};
    std::atomic<bool> bypassed_{false};

    // Telemetry atomics
    std::atomic<float> in_peak_db_{-90.0f};
    std::atomic<float> out_peak_db_{-90.0f};
    std::atomic<float> cpu_time_us_{0.0f};
    std::atomic<bool> is_clipping_{false};

    // Preallocated Linear FIR history
    static constexpr size_t MAX_FIR_TAPS = 4096;
    std::vector<float> fir_history_;
    size_t fir_write_pos_ = 0;

    // Preallocated LSTM states
    static constexpr size_t MAX_HIDDEN = 32;
    std::vector<float> lstm_hidden_;
    std::vector<float> lstm_cell_;
    std::vector<float> lstm_gates_; // 4 * MAX_HIDDEN

    // Preallocated mono scratch
    std::vector<float> mono_in_scratch_;
    std::vector<float> mono_out_scratch_;
};

} // namespace prismix::nam
