#pragma once

#include <string>
#include <cstdint>

namespace prismix::web {

enum class WebCommandType : uint8_t {
    Unknown = 0,
    SetChannelFader,
    SetChannelPan,
    SetChannelMute,
    SetChannelSolo,
    SetChannelGain,
    SetPluginParameter,
    SetSlotBypass,
    AddPlugin,
    RemovePlugin,
    MovePlugin,
    ApplyPreset,
    SavePreset
};

struct WebCommand {
    WebCommandType type = WebCommandType::Unknown;
    int channel_id = 1;
    int slot_index = 0;
    int to_slot_index = 0;
    std::string param_id;
    float param_value = 0.0f;
    bool bool_value = false;
    std::string uri;
    std::string preset_name;
    std::string author;
    std::string description;
};

struct CommandResult {
    bool success = false;
    std::string error;
    uint64_t state_version = 0;
};

} // namespace prismix::web

