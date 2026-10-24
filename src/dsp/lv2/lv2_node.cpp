#include "lv2_node.h"
#include <algorithm>
#include <cstring>

namespace prismix::lv2 {

const std::string Lv2AudioNode::empty_uri_ = "";

Lv2AudioNode::Lv2AudioNode(std::unique_ptr<Lv2Instance> instance,
                           double /*sample_rate*/,
                           uint32_t max_block_size)
    : instance_(std::move(instance)) {
    if (!instance_) return;

    const auto& ports = instance_->ports();
    uint32_t max_port_idx = 0;
    for (const auto& p : ports) {
        if (p.index > max_port_idx) max_port_idx = p.index;
    }

    bindings_.resize(max_port_idx + 1);
    control_storage_.resize(max_port_idx + 1, 0.0f);
    atomic_controls_ = std::vector<std::atomic<float>>(max_port_idx + 1);

    mono_in_scratch_.resize(max_block_size, 0.0f);
    mono_out_scratch_.resize(max_block_size, 0.0f);

    for (const auto& p : ports) {
        bindings_[p.index].port_index = p.index;
        bindings_[p.index].type = p.type;
        bindings_[p.index].direction = p.direction;
        bindings_[p.index].symbol = p.symbol;
        bindings_[p.index].value = p.default_value;

        control_storage_[p.index] = p.default_value;
        atomic_controls_[p.index].store(p.default_value, std::memory_order_relaxed);
        symbol_to_index_[p.symbol] = p.index;

        if (p.type == PortType::Audio) {
            if (p.direction == PortDirection::Input) {
                audio_in_ports_.push_back(p.index);
            } else {
                audio_out_ports_.push_back(p.index);
            }
        } else if (p.type == PortType::Control) {
            // Connect control port location immediately
            instance_->connect_port(p.index, &control_storage_[p.index]);
        }
    }
}

void Lv2AudioNode::reset() {
    if (instance_) {
        instance_->deactivate();
        instance_->activate();
    }
}

void Lv2AudioNode::setControlValue(uint32_t port_index, float value) noexcept {
    if (port_index < atomic_controls_.size()) {
        atomic_controls_[port_index].store(value, std::memory_order_relaxed);
    }
}

void Lv2AudioNode::setControlValue(const std::string& symbol, float value) noexcept {
    auto it = symbol_to_index_.find(symbol);
    if (it != symbol_to_index_.end()) {
        setControlValue(static_cast<uint32_t>(it->second), value);
    }
}

float Lv2AudioNode::getControlValue(uint32_t port_index) const noexcept {
    if (port_index < atomic_controls_.size()) {
        return atomic_controls_[port_index].load(std::memory_order_relaxed);
    }
    return 0.0f;
}

float Lv2AudioNode::getControlValue(const std::string& symbol) const noexcept {
    auto it = symbol_to_index_.find(symbol);
    if (it != symbol_to_index_.end()) {
        return getControlValue(static_cast<uint32_t>(it->second));
    }
    return 0.0f;
}

void Lv2AudioNode::process(const audiocore::AudioBuffer& input,
                           audiocore::AudioBuffer& output,
                           uint32_t frames) {
    if (!instance_ || frames == 0) return;

    if (bypassed_.load(std::memory_order_relaxed)) {
        output.copyFrom(input, frames);
        return;
    }

    // Synchronize control inputs
    for (size_t i = 0; i < bindings_.size(); ++i) {
        if (bindings_[i].type == PortType::Control &&
            bindings_[i].direction == PortDirection::Input) {
            control_storage_[i] = atomic_controls_[i].load(std::memory_order_relaxed);
        }
    }

    const uint32_t in_channels = input.getChannelCount();
    const uint32_t out_channels = output.getChannelCount();
    const size_t num_audio_in = audio_in_ports_.size();
    const size_t num_audio_out = audio_out_ports_.size();

    if (num_audio_in == 0 || num_audio_out == 0) {
        output.copyFrom(input, frames);
        return;
    }

    // Connect Audio Inputs
    if (num_audio_in == 1) {
        if (in_channels >= 2) {
            // Downmix stereo to mono scratch
            const float* in_l = input.getChannelData(0);
            const float* in_r = input.getChannelData(1);
            float* scratch = mono_in_scratch_.data();
            for (uint32_t f = 0; f < frames; ++f) {
                scratch[f] = 0.5f * (in_l[f] + in_r[f]);
            }
            instance_->connect_port(audio_in_ports_[0], scratch);
        } else {
            instance_->connect_port(audio_in_ports_[0],
                                    const_cast<float*>(input.getChannelData(0)));
        }
    } else { // 2 or more audio inputs
        if (in_channels >= 2) {
            instance_->connect_port(audio_in_ports_[0],
                                    const_cast<float*>(input.getChannelData(0)));
            instance_->connect_port(audio_in_ports_[1],
                                    const_cast<float*>(input.getChannelData(1)));
        } else {
            // Mono input -> feed both channels
            instance_->connect_port(audio_in_ports_[0],
                                    const_cast<float*>(input.getChannelData(0)));
            instance_->connect_port(audio_in_ports_[1],
                                    const_cast<float*>(input.getChannelData(0)));
        }
    }

    // Connect Audio Outputs
    bool needs_mono_to_stereo_expansion = false;
    bool needs_stereo_to_mono_downmix = false;

    if (num_audio_out == 1) {
        if (out_channels >= 2) {
            needs_mono_to_stereo_expansion = true;
            instance_->connect_port(audio_out_ports_[0], mono_out_scratch_.data());
        } else {
            instance_->connect_port(audio_out_ports_[0], output.getChannelData(0));
        }
    } else { // 2 or more audio outputs
        if (out_channels >= 2) {
            instance_->connect_port(audio_out_ports_[0], output.getChannelData(0));
            instance_->connect_port(audio_out_ports_[1], output.getChannelData(1));
        } else {
            needs_stereo_to_mono_downmix = true;
            instance_->connect_port(audio_out_ports_[0], output.getChannelData(0));
            instance_->connect_port(audio_out_ports_[1], mono_out_scratch_.data());
        }
    }

    // Run plugin DSP
    instance_->run(frames);

    // Post-process channel layout adaptations if needed
    if (needs_mono_to_stereo_expansion) {
        const float* src = mono_out_scratch_.data();
        float* dst_l = output.getChannelData(0);
        float* dst_r = output.getChannelData(1);
        std::memcpy(dst_l, src, frames * sizeof(float));
        std::memcpy(dst_r, src, frames * sizeof(float));
    } else if (needs_stereo_to_mono_downmix) {
        float* out_l = output.getChannelData(0);
        const float* out_r = mono_out_scratch_.data();
        for (uint32_t f = 0; f < frames; ++f) {
            out_l[f] = 0.5f * (out_l[f] + out_r[f]);
        }
    }
}

} // namespace prismix::lv2
