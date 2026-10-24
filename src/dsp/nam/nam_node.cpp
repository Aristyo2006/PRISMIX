#include "nam_node.h"
#include <cmath>
#include <algorithm>
#include <cstring>

namespace prismix::nam {

namespace {

inline float fast_sigmoid(float x) noexcept {
    return 1.0f / (1.0f + std::exp(-x));
}

inline float db_to_linear(float db) noexcept {
    return db <= -90.0f ? 0.0f : std::pow(10.0f, db / 20.0f);
}

inline float linear_to_db(float lin) noexcept {
    return lin <= 1e-5f ? -100.0f : 20.0f * std::log10(lin);
}

} // namespace

NamAudioNode::NamAudioNode(double sample_rate, uint32_t max_block_size)
    : sample_rate_(sample_rate > 0.0 ? sample_rate : 48000.0) {
    fir_history_.resize(MAX_FIR_TAPS, 0.0f);
    lstm_hidden_.resize(MAX_HIDDEN, 0.0f);
    lstm_cell_.resize(MAX_HIDDEN, 0.0f);
    lstm_gates_.resize(4 * MAX_HIDDEN, 0.0f);

    mono_in_scratch_.resize(max_block_size, 0.0f);
    mono_out_scratch_.resize(max_block_size, 0.0f);
}

void NamAudioNode::reset() {
    std::fill(fir_history_.begin(), fir_history_.end(), 0.0f);
    fir_write_pos_ = 0;
    std::fill(lstm_hidden_.begin(), lstm_hidden_.end(), 0.0f);
    std::fill(lstm_cell_.begin(), lstm_cell_.end(), 0.0f);
    std::fill(lstm_gates_.begin(), lstm_gates_.end(), 0.0f);
}

void NamAudioNode::setModel(std::shared_ptr<NamModel> model) {
    active_model_ = std::move(model);
    reset();
}

std::shared_ptr<NamModel> NamAudioNode::getModel() const {
    return active_model_;
}

NamTelemetry NamAudioNode::getTelemetry() const noexcept {
    NamTelemetry t;
    t.in_peak_db = in_peak_db_.load(std::memory_order_relaxed);
    t.out_peak_db = out_peak_db_.load(std::memory_order_relaxed);
    t.cpu_time_us = cpu_time_us_.load(std::memory_order_relaxed);
    t.is_clipping = is_clipping_.load(std::memory_order_relaxed);
    return t;
}

void NamAudioNode::processLinear(const float* in, float* out, uint32_t frames, const LinearConfig& cfg) {
    const size_t N = cfg.receptive_field;
    if (N == 0 || cfg.weights.size() < N) {
        std::memcpy(out, in, frames * sizeof(float));
        return;
    }

    const float* w = cfg.weights.data();
    const float bias = cfg.has_bias ? cfg.bias : 0.0f;
    float* hist = fir_history_.data();

    for (uint32_t f = 0; f < frames; ++f) {
        hist[fir_write_pos_] = in[f];

        float sum = bias;
        size_t read_idx = fir_write_pos_;
        for (size_t k = 0; k < N; ++k) {
            sum += w[k] * hist[read_idx];
            read_idx = (read_idx == 0) ? (N - 1) : (read_idx - 1);
        }
        out[f] = sum;

        fir_write_pos_ = (fir_write_pos_ + 1) % N;
    }
}

void NamAudioNode::processLstm(const float* in, float* out, uint32_t frames, const LstmConfig& cfg) {
    const uint32_t H = cfg.hidden_size;
    if (H == 0 || H > MAX_HIDDEN) {
        std::memcpy(out, in, frames * sizeof(float));
        return;
    }

    const float* w_ih = cfg.w_ih.data();
    const float* w_hh = cfg.w_hh.data();
    const float* b_ih = cfg.bias_ih.data();
    const float* b_hh = cfg.bias_hh.data();
    const float* w_out = cfg.w_out.data();
    const float b_out = cfg.b_out;

    float* h = lstm_hidden_.data();
    float* c = lstm_cell_.data();
    float* gates = lstm_gates_.data();

    for (uint32_t f = 0; f < frames; ++f) {
        const float x = in[f];

        // 1. Matrix multiplications for all 4 gates (input, forget, cell, output)
        for (uint32_t i = 0; i < 4 * H; ++i) {
            float sum = w_ih[i] * x + b_ih[i] + b_hh[i];
            const float* w_hh_row = &w_hh[i * H];
            for (uint32_t j = 0; j < H; ++j) {
                sum += w_hh_row[j] * h[j];
            }
            gates[i] = sum;
        }

        // 2. Non-linear activations & cell update
        for (uint32_t k = 0; k < H; ++k) {
            float in_gate = fast_sigmoid(gates[k]);
            float forget_gate = fast_sigmoid(gates[H + k]);
            float cell_cand = std::tanh(gates[2 * H + k]);
            float out_gate = fast_sigmoid(gates[3 * H + k]);

            c[k] = forget_gate * c[k] + in_gate * cell_cand;
            h[k] = out_gate * std::tanh(c[k]);
        }

        // 3. Output dense projection
        float y = b_out;
        for (uint32_t k = 0; k < H; ++k) {
            y += w_out[k] * h[k];
        }
        out[f] = y;
    }
}

void NamAudioNode::process(const audiocore::AudioBuffer& input,
                           audiocore::AudioBuffer& output,
                           uint32_t frames) {
    if (frames == 0) return;

    if (bypassed_.load(std::memory_order_relaxed)) {
        output.copyFrom(input, frames);
        return;
    }

    const auto start_time = std::chrono::steady_clock::now();

    const uint32_t in_ch = input.getChannelCount();
    const uint32_t out_ch = output.getChannelCount();

    const float in_gain = db_to_linear(input_gain_db_.load(std::memory_order_relaxed));
    const float out_level = db_to_linear(output_level_db_.load(std::memory_order_relaxed));

    // Downmix to mono scratch buffer
    float* scratch_in = mono_in_scratch_.data();
    float* scratch_out = mono_out_scratch_.data();

    float in_peak = 0.0f;
    if (in_ch >= 2) {
        const float* l = input.getChannelData(0);
        const float* r = input.getChannelData(1);
        for (uint32_t f = 0; f < frames; ++f) {
            float s = 0.5f * (l[f] + r[f]) * in_gain;
            scratch_in[f] = s;
            float abs_s = std::abs(s);
            if (abs_s > in_peak) in_peak = abs_s;
        }
    } else {
        const float* l = input.getChannelData(0);
        for (uint32_t f = 0; f < frames; ++f) {
            float s = l[f] * in_gain;
            scratch_in[f] = s;
            float abs_s = std::abs(s);
            if (abs_s > in_peak) in_peak = abs_s;
        }
    }
    in_peak_db_.store(linear_to_db(in_peak), std::memory_order_relaxed);

    // Run inference if active model exists and is valid
    if (active_model_ && active_model_->isValid()) {
        const auto arch = active_model_->architecture();
        if (arch == Architecture::Linear) {
            processLinear(scratch_in, scratch_out, frames, active_model_->linearConfig());
        } else if (arch == Architecture::LSTM) {
            processLstm(scratch_in, scratch_out, frames, active_model_->lstmConfig());
        } else {
            std::memcpy(scratch_out, scratch_in, frames * sizeof(float));
        }
    } else {
        // Passthrough with gain if no model loaded
        std::memcpy(scratch_out, scratch_in, frames * sizeof(float));
    }

    // Apply output level, check clipping, and write to output buffer
    float out_peak = 0.0f;
    bool clip = false;
    for (uint32_t f = 0; f < frames; ++f) {
        float s = scratch_out[f] * out_level;
        scratch_out[f] = s;
        float abs_s = std::abs(s);
        if (abs_s > out_peak) out_peak = abs_s;
        if (abs_s >= 1.0f) clip = true;
    }
    out_peak_db_.store(linear_to_db(out_peak), std::memory_order_relaxed);
    is_clipping_.store(clip, std::memory_order_relaxed);

    if (out_ch >= 2) {
        float* l = output.getChannelData(0);
        float* r = output.getChannelData(1);
        std::memcpy(l, scratch_out, frames * sizeof(float));
        std::memcpy(r, scratch_out, frames * sizeof(float));
    } else {
        float* l = output.getChannelData(0);
        std::memcpy(l, scratch_out, frames * sizeof(float));
    }

    const auto end_time = std::chrono::steady_clock::now();
    const float duration_us = static_cast<float>(
        std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time).count());
    cpu_time_us_.store(duration_us, std::memory_order_relaxed);
}

} // namespace prismix::nam
