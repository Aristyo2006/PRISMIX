#pragma once

#include "audiocore/audio_node.hpp"
#include "audiocore/audio_buffer.hpp"
#include "lv2_host.h"
#include <vector>
#include <atomic>
#include <memory>
#include <unordered_map>

namespace prismix::lv2 {

class Lv2AudioNode final : public audiocore::AudioNode {
public:
    explicit Lv2AudioNode(std::unique_ptr<Lv2Instance> instance,
                          double sample_rate = 48000.0,
                          uint32_t max_block_size = 2048);
    ~Lv2AudioNode() override = default;

    void reset() override;

    void process(const audiocore::AudioBuffer& input,
                 audiocore::AudioBuffer& output,
                 uint32_t frames) override;

    void setBypassed(bool b) noexcept { bypassed_.store(b, std::memory_order_relaxed); }
    bool isBypassed() const noexcept { return bypassed_.load(std::memory_order_relaxed); }

    void setControlValue(uint32_t port_index, float value) noexcept;
    void setControlValue(const std::string& symbol, float value) noexcept;
    float getControlValue(uint32_t port_index) const noexcept;
    float getControlValue(const std::string& symbol) const noexcept;

    const Lv2Instance* getInstance() const { return instance_.get(); }
    const std::string& getUri() const { return instance_ ? instance_->get_uri() : empty_uri_; }

private:
    std::unique_ptr<Lv2Instance> instance_;
    std::atomic<bool> bypassed_{false};

    // Preallocated port mapping structures
    struct PortBinding {
        uint32_t port_index = 0;
        PortType type = PortType::Control;
        PortDirection direction = PortDirection::Input;
        std::string symbol;
        float value = 0.0f; // Audio thread local mirror or location
    };

    std::vector<PortBinding> bindings_;
    std::vector<float> control_storage_;
    std::vector<std::atomic<float>> atomic_controls_;
    std::unordered_map<std::string, size_t> symbol_to_index_;

    std::vector<uint32_t> audio_in_ports_;
    std::vector<uint32_t> audio_out_ports_;

    // Preallocated mono scratch buffer to avoid allocations in process()
    std::vector<float> mono_in_scratch_;
    std::vector<float> mono_out_scratch_;

    static const std::string empty_uri_;
};

} // namespace prismix::lv2
