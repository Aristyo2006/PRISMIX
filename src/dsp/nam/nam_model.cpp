#include "nam_model.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>

namespace prismix::nam {

namespace {

// Lightweight non-allocating / simple JSON scanner
struct JsonParser {
    const std::string& src;
    size_t pos = 0;

    void skipWhitespace() {
        while (pos < src.size() && (std::isspace(static_cast<unsigned char>(src[pos])) || src[pos] == ',' || src[pos] == ':')) {
            ++pos;
        }
    }

    bool hasMore() const { return pos < src.size(); }

    std::string parseString() {
        skipWhitespace();
        if (pos >= src.size() || src[pos] != '"') return "";
        ++pos;
        size_t start = pos;
        while (pos < src.size() && src[pos] != '"') {
            if (src[pos] == '\\' && pos + 1 < src.size()) pos += 2;
            else ++pos;
        }
        std::string res = src.substr(start, pos - start);
        if (pos < src.size() && src[pos] == '"') ++pos;
        return res;
    }

    double parseNumber() {
        skipWhitespace();
        size_t start = pos;
        if (pos < src.size() && (src[pos] == '-' || src[pos] == '+')) ++pos;
        while (pos < src.size() && (std::isdigit(static_cast<unsigned char>(src[pos])) || src[pos] == '.' || src[pos] == 'e' || src[pos] == 'E' || src[pos] == '-' || src[pos] == '+')) {
            ++pos;
        }
        if (pos > start) {
            try {
                return std::stod(src.substr(start, pos - start));
            } catch (...) {
                return 0.0;
            }
        }
        return 0.0;
    }

    bool parseBool() {
        skipWhitespace();
        if (src.compare(pos, 4, "true") == 0) {
            pos += 4;
            return true;
        }
        if (src.compare(pos, 5, "false") == 0) {
            pos += 5;
            return false;
        }
        return false;
    }

    bool seekKey(const std::string& key) {
        std::string pattern = "\"" + key + "\"";
        size_t found = src.find(pattern, pos);
        if (found == std::string::npos) return false;
        pos = found + pattern.size();
        skipWhitespace();
        return true;
    }

    bool parseArrayOfFloats(std::vector<float>& out) {
        skipWhitespace();
        if (pos >= src.size() || src[pos] != '[') return false;
        ++pos;
        while (pos < src.size()) {
            skipWhitespace();
            if (pos < src.size() && src[pos] == ']') {
                ++pos;
                break;
            }
            float val = static_cast<float>(parseNumber());
            out.push_back(val);
        }
        return true;
    }
};

} // namespace

std::shared_ptr<NamModel> NamModel::loadFromFile(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        auto m = std::make_shared<NamModel>();
        m->metadata_.file_path = filepath;
        m->metadata_.validation_error = "Could not open file: " + filepath;
        return m;
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    auto m = loadFromJsonString(ss.str(), filepath);
    m->metadata_.file_path = filepath;
    return m;
}

std::shared_ptr<NamModel> NamModel::loadFromJsonString(const std::string& json_str,
                                                      const std::string& name_hint) {
    auto model = std::make_shared<NamModel>();
    auto& meta = model->metadata_;
    meta.name = name_hint.empty() ? "NAM Lite Model" : name_hint;

    // Remove directory prefix if name_hint was a path
    auto last_slash = meta.name.find_last_of("/\\");
    if (last_slash != std::string::npos) {
        meta.name = meta.name.substr(last_slash + 1);
    }
    auto dot_nam = meta.name.rfind(".nam");
    if (dot_nam != std::string::npos) {
        meta.name = meta.name.substr(0, dot_nam);
    }

    JsonParser p{json_str, 0};

    // 1. Version
    if (p.seekKey("version")) {
        meta.version = p.parseString();
    } else {
        meta.version = "0.5.0";
    }

    // 2. Architecture
    p.pos = 0;
    std::string arch_str;
    if (p.seekKey("architecture")) {
        arch_str = p.parseString();
    }

    if (arch_str == "Linear") {
        meta.architecture = Architecture::Linear;
    } else if (arch_str == "LSTM") {
        meta.architecture = Architecture::LSTM;
    } else if (arch_str == "WaveNet") {
        meta.architecture = Architecture::WaveNet;
    } else {
        meta.architecture = Architecture::Unknown;
        meta.validation_error = "Unsupported or missing architecture: " + arch_str;
        return model;
    }

    // 3. Expected sample rate
    p.pos = 0;
    if (p.seekKey("sample_rate")) {
        meta.expected_sample_rate = static_cast<uint32_t>(p.parseNumber());
    } else {
        meta.expected_sample_rate = 48000;
    }

    // Parse config and weights based on architecture
    if (meta.architecture == Architecture::Linear) {
        p.pos = 0;
        if (p.seekKey("config")) {
            size_t config_start = p.pos;
            if (p.seekKey("receptive_field")) {
                model->linear_.receptive_field = static_cast<uint32_t>(p.parseNumber());
            }
            p.pos = config_start;
            if (p.seekKey("bias")) {
                model->linear_.has_bias = p.parseBool();
            }
        }
        if (model->linear_.receptive_field == 0) {
            model->linear_.receptive_field = 512; // default
        }

        p.pos = 0;
        if (p.seekKey("weights")) {
            p.parseArrayOfFloats(model->linear_.weights);
        }

        if (model->linear_.weights.empty()) {
            meta.validation_error = "Linear model contains no weights";
            return model;
        }

        if (model->linear_.has_bias && model->linear_.weights.size() > model->linear_.receptive_field) {
            model->linear_.bias = model->linear_.weights.back();
            model->linear_.weights.pop_back();
        }

        model->linear_.receptive_field = static_cast<uint32_t>(model->linear_.weights.size());
        meta.estimated_mflops = static_cast<uint32_t>((2ULL * model->linear_.receptive_field * meta.expected_sample_rate) / 1000000ULL);
        meta.meets_realtime_budget = (model->linear_.receptive_field <= 4096);
        if (!meta.meets_realtime_budget) {
            meta.validation_error = "Receptive field (" + std::to_string(model->linear_.receptive_field) + ") exceeds realtime budget";
        }
    }
    else if (meta.architecture == Architecture::LSTM) {
        p.pos = 0;
        if (p.seekKey("config")) {
            size_t cfg_pos = p.pos;
            if (p.seekKey("hidden_size")) {
                model->lstm_.hidden_size = static_cast<uint32_t>(p.parseNumber());
            }
            p.pos = cfg_pos;
            if (p.seekKey("num_layers")) {
                model->lstm_.num_layers = static_cast<uint32_t>(p.parseNumber());
            }
        }
        if (model->lstm_.hidden_size == 0) model->lstm_.hidden_size = 16;
        if (model->lstm_.num_layers == 0) model->lstm_.num_layers = 1;

        std::vector<float> all_weights;
        p.pos = 0;
        if (p.seekKey("weights")) {
            p.parseArrayOfFloats(all_weights);
        }

        const uint32_t H = model->lstm_.hidden_size;
        // Check budget: Hidden size > 32 exceeds low-power ARM64 realtime budget for digital mixer
        if (H > 32 || model->lstm_.num_layers > 1) {
            meta.meets_realtime_budget = false;
            meta.validation_error = "LSTM hidden_size=" + std::to_string(H) + " layers=" +
                                   std::to_string(model->lstm_.num_layers) + " exceeds low-CPU realtime budget";
            return model;
        }

        const size_t w_ih_size = 4 * H * 1;
        const size_t w_hh_size = 4 * H * H;
        const size_t bias_ih_size = 4 * H;
        const size_t bias_hh_size = 4 * H;
        const size_t w_out_size = H;
        const size_t expected_total = w_ih_size + w_hh_size + bias_ih_size + bias_hh_size + w_out_size + 1;

        if (all_weights.size() < expected_total) {
            meta.validation_error = "LSTM weights count (" + std::to_string(all_weights.size()) +
                                   ") is less than expected (" + std::to_string(expected_total) + ")";
            return model;
        }

        size_t offset = 0;
        model->lstm_.w_ih.assign(all_weights.begin() + offset, all_weights.begin() + offset + w_ih_size);
        offset += w_ih_size;
        model->lstm_.w_hh.assign(all_weights.begin() + offset, all_weights.begin() + offset + w_hh_size);
        offset += w_hh_size;
        model->lstm_.bias_ih.assign(all_weights.begin() + offset, all_weights.begin() + offset + bias_ih_size);
        offset += bias_ih_size;
        model->lstm_.bias_hh.assign(all_weights.begin() + offset, all_weights.begin() + offset + bias_hh_size);
        offset += bias_hh_size;
        model->lstm_.w_out.assign(all_weights.begin() + offset, all_weights.begin() + offset + w_out_size);
        offset += w_out_size;
        model->lstm_.b_out = all_weights[offset];

        meta.estimated_mflops = static_cast<uint32_t>((2ULL * (w_ih_size + w_hh_size + w_out_size) * meta.expected_sample_rate) / 1000000ULL);
        meta.meets_realtime_budget = true;
    }
    else if (meta.architecture == Architecture::WaveNet) {
        p.pos = 0;
        if (p.seekKey("config")) {
            if (p.seekKey("num_channels")) {
                model->wavenet_.num_channels = static_cast<uint32_t>(p.parseNumber());
            }
        }
        if (model->wavenet_.num_channels == 0) model->wavenet_.num_channels = 4;

        if (model->wavenet_.num_channels > 8) {
            meta.meets_realtime_budget = false;
            meta.validation_error = "WaveNet num_channels=" + std::to_string(model->wavenet_.num_channels) +
                                   " exceeds low-power STB realtime budget";
            return model;
        }

        meta.estimated_mflops = 45; // Approx for 4-channel lite WaveNet
        meta.meets_realtime_budget = true;
    }

    return model;
}

} // namespace prismix::nam
