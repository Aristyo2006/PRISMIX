#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace prismix::nam {

enum class Architecture : uint8_t {
    Unknown,
    Linear,  // FIR / Cabinet / Tone stack
    LSTM,    // 1-layer Fast / Lite LSTM (embedded target)
    WaveNet  // Dilated ConvNet (Lite/Nano)
};

struct LinearConfig {
    uint32_t receptive_field = 0;
    bool has_bias = false;
    float bias = 0.0f;
    std::vector<float> weights;
};

struct LstmConfig {
    uint32_t num_layers = 1;
    uint32_t input_size = 1;
    uint32_t hidden_size = 16;

    // Weight matrices
    // Shapes: W_ih: (4*H, 1), W_hh: (4*H, H), bias: (4*H)
    // Output: W_out: (1, H), b_out: (1)
    std::vector<float> w_ih;
    std::vector<float> w_hh;
    std::vector<float> bias_ih;
    std::vector<float> bias_hh;
    std::vector<float> w_out;
    float b_out = 0.0f;
};

struct DilatedLayerConfig {
    uint32_t in_channels = 0;
    uint32_t out_channels = 0;
    uint32_t dilation = 1;
    uint32_t kernel_size = 3;
    std::vector<float> conv_weight; // (2 * out_channels, in_channels, kernel_size)
    std::vector<float> conv_bias;   // (2 * out_channels)
    std::vector<float> out_weight;  // (out_channels, out_channels)
    std::vector<float> out_bias;    // (out_channels)
};

struct WaveNetConfig {
    uint32_t num_channels = 4;
    std::vector<DilatedLayerConfig> layers;
    std::vector<float> head_weight;
    float head_bias = 0.0f;
};

struct NamModelMetadata {
    std::string name;
    std::string author;
    std::string description;
    std::string file_path;
    std::string version;
    Architecture architecture = Architecture::Unknown;
    uint32_t expected_sample_rate = 48000;
    uint32_t estimated_mflops = 0;
    bool meets_realtime_budget = true;
    std::string validation_error;
};

class NamModel {
public:
    NamModel() = default;
    ~NamModel() = default;

    static std::shared_ptr<NamModel> loadFromFile(const std::string& filepath);
    static std::shared_ptr<NamModel> loadFromJsonString(const std::string& json_str,
                                                        const std::string& name_hint = "");

    const NamModelMetadata& metadata() const { return metadata_; }
    Architecture architecture() const { return metadata_.architecture; }
    bool isValid() const { return metadata_.validation_error.empty() && metadata_.architecture != Architecture::Unknown; }

    const LinearConfig& linearConfig() const { return linear_; }
    const LstmConfig& lstmConfig() const { return lstm_; }
    const WaveNetConfig& wavenetConfig() const { return wavenet_; }

private:
    NamModelMetadata metadata_;
    LinearConfig linear_;
    LstmConfig lstm_;
    WaveNetConfig wavenet_;
};

} // namespace prismix::nam
