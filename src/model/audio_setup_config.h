#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class SetupChannelMode : uint8_t { Mono = 0, Stereo = 1 };

struct SetupChannel {
    uint32_t logicalId = 1;
    std::string name = "STEREO 1";
    SetupChannelMode mode = SetupChannelMode::Stereo;
    uint32_t inputLeft = 0;
    uint32_t inputRight = 1;
    bool enabled = true;
    uint32_t outputLeft = 0;
    uint32_t outputRight = 1;
    float gainDb = 0.0f;
    float pan = 0.0f;
    bool muted = false;
    bool dspBypass = false;
    bool peqEnabled = true;
    bool compressorEnabled = true;
};

struct AudioSetupConfig {
    uint32_t version = 1;
    std::string captureDeviceId;
    std::string renderDeviceId;
    uint32_t requestedSampleRate = 48000;
    uint32_t requestedBufferFrames = 128;
    std::vector<SetupChannel> channels{{}};
};

struct AudioTopologyCapabilities {
    uint32_t captureChannels = 2;
    uint32_t renderChannels = 2;
    uint32_t maxIndependentStrips = 1;
    bool supportsMonoStrip = false;
    bool supportsStereoStrip = true;
    bool supportsDuplicateInputs = false;
    uint32_t requestedSampleRate = 48000;
    uint32_t requestedBufferFrames = 128;
};

struct SetupValidation {
    bool valid = false;
    std::string error;
};

SetupValidation validate_audio_setup(const AudioSetupConfig& config,
                                     const AudioTopologyCapabilities& capabilities);
bool save_audio_setup(const AudioSetupConfig& config, const std::string& path,
                      std::string* error = nullptr);
bool load_audio_setup(const std::string& path, AudioSetupConfig& config,
                     std::string* error = nullptr);
std::string default_audio_setup_path();
