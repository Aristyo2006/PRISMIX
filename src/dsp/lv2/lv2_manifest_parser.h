#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace prismix::lv2 {

enum class PortDirection : uint8_t {
    Input,
    Output
};

enum class PortType : uint8_t {
    Audio,
    Control,
    Other
};

struct Lv2PortMetadata {
    uint32_t index = 0;
    std::string symbol;
    std::string name;
    PortDirection direction = PortDirection::Input;
    PortType type = PortType::Control;
    float default_value = 0.0f;
    float min_value = 0.0f;
    float max_value = 1.0f;
    bool is_toggled = false;
    bool is_integer = false;
    bool is_sample_rate = false;
    bool reports_latency = false;
    std::string unit;
};

struct Lv2ParsedPlugin {
    std::string uri;
    std::string name;
    std::string category;
    std::string author;
    std::string description;
    std::string binary_rel_path;
    std::string see_also_rel_path;
    std::vector<std::string> required_features;
    std::vector<std::string> optional_features;
    std::vector<Lv2PortMetadata> ports;

    // Derived port counts
    uint32_t audio_inputs = 0;
    uint32_t audio_outputs = 0;
    uint32_t control_inputs = 0;
    uint32_t control_outputs = 0;
};

class Lv2ManifestParser {
public:
    static bool parseManifest(const std::string& manifest_content,
                             std::vector<Lv2ParsedPlugin>& out_plugins);

    static bool parsePluginTtl(const std::string& ttl_content,
                              Lv2ParsedPlugin& plugin);
};

} // namespace prismix::lv2
