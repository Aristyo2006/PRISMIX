#pragma once

#include <string>
#include <cstdint>

namespace prismix::web {

enum class WebCommandType : uint8_t {
    Unknown = 0,
    // Selection & Channel Lifecycle
    SelectChannel,
    AddChannel,
    DeleteChannel,
    RenameChannel,
    // Strip & Input Stage
    SetChannelFader,
    SetChannelReturnGain,
    SetChannelPan,
    SetChannelMute,
    SetChannelSolo,
    SetChannelGain,
    SetChannelPolarity,
    SetChannelPhantom,
    SetChannelHpfEnabled,
    SetChannelHpfFreq,
    SetChannelFormat,
    SetAuxSendLevel,
    SetAuxSendEnabled,
    SetAuxSendPreFader,
    SetSpeakerDelay,
    // EQ (PEQ + GEQ) & RTA
    SetEqBypass,
    SetPeqBand,
    FlattenPeq,
    SetGeqMode,
    SetGeqBypass,
    SetGeqBand,
    FlattenGeq,
    SetRtaConfig,
    // Dynamics (Gate, Compressor, Limiter, Sidechain)
    SetGateEnabled,
    SetGateParam,
    SetCompBypass,
    SetCompParam,
    SetLimiterEnabled,
    SetLimiterParam,
    SetSidechain,
    // FX Rack & Presets & PatchStorage
    SetPluginParameter,
    SetSlotBypass,
    AddPlugin,
    RemovePlugin,
    MovePlugin,
    ApplyPreset,
    SavePreset,
    PatchStorageBrowse,
    PatchStorageInstall,
    // Routing Matrix
    SetInputSource,
    SetOutputRoute,
    SetChannelBus,
    SetBusMainLr,
    SetOutputPatch,
    // System, Audio Engine, Scene & Theme
    SetAudioMode,
    StartAudio,
    StopAudio,
    RefreshDevices,
    SetAudioConfig,
    SetGeneratorSignal,
    SaveScene,
    LoadScene,
    SetThemePreset,
    SetThemeColor,
    SaveTheme
};

struct WebCommand {
    WebCommandType type = WebCommandType::Unknown;
    int channel_id = 1;
    int slot_index = 0;
    int to_slot_index = 0;
    int band_index = 0;
    int aux_index = 0;
    int bus_index = 0;
    int int_value = 0;
    int int_value2 = -1;
    std::string param_id;
    float param_value = 0.0f;
    float float_value2 = 0.0f;
    float float_value3 = 0.0f;
    bool bool_value = false;
    bool has_freq = false;
    bool has_gain = false;
    bool has_q = false;
    bool has_enabled = false;
    bool has_filter_type = false;
    int filter_type = 1;
    std::string uri;
    std::string name;
    std::string preset_name;
    std::string author;
    std::string description;
    std::string capture_device;
    std::string render_device;
    uint32_t sample_rate = 0;
    uint32_t buffer_frames = 0;
};

struct CommandResult {
    bool success = false;
    std::string error;
    uint64_t state_version = 0;
};

} // namespace prismix::web
