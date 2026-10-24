#include "audio_setup_config.h"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <set>
#include <cstdlib>
#include <filesystem>

SetupValidation validate_audio_setup(const AudioSetupConfig& config,
                                     const AudioTopologyCapabilities& caps) {
    auto fail = [](const char* message) { return SetupValidation{false, message}; };
    if (config.version != 1) return fail("Unsupported audio setup configuration version.");
    if (config.requestedSampleRate != caps.requestedSampleRate ||
        config.requestedBufferFrames != caps.requestedBufferFrames)
        return fail("Requested sample rate or buffer size is unsupported by the current stereo service.");
    if (config.channels.empty() || config.channels.size() > caps.maxIndependentStrips)
        return fail("Requested logical channel count exceeds the engine's independent strip limit.");
    if (caps.renderChannels < 2)
        return fail("The current Main L/R route requires at least two render channels.");
    std::set<uint32_t> ids;
    std::set<uint32_t> usedInputs;
    for (const auto& channel : config.channels) {
        if (channel.logicalId == 0 || !ids.insert(channel.logicalId).second)
            return fail("Logical channel identifiers must be nonzero and unique.");
        if (channel.name.empty() || channel.name.size() > 31)
            return fail("Channel names must contain 1 to 31 characters.");
        if (!channel.enabled || channel.muted)
            return fail("The current stereo demo does not implement channel disable or mute.");
        if (!std::isfinite(channel.gainDb) || channel.gainDb < -60.0f || channel.gainDb > 12.0f ||
            !std::isfinite(channel.pan) || channel.pan < -1.0f || channel.pan > 1.0f)
            return fail("Channel gain or pan is outside its supported range.");
        if (channel.mode == SetupChannelMode::Mono) {
            if (!caps.supportsMonoStrip) return fail("The current engine does not support mono channel strips.");
            if (channel.inputLeft >= caps.captureChannels || channel.outputLeft >= caps.renderChannels)
                return fail("Mono channel mapping references a channel that does not exist.");
        } else {
            if (!caps.supportsStereoStrip) return fail("The current engine does not support stereo channel strips.");
            if (channel.inputLeft >= caps.captureChannels || channel.inputRight >= caps.captureChannels ||
                channel.outputLeft >= caps.renderChannels || channel.outputRight >= caps.renderChannels)
                return fail("Stereo channel mapping references a channel that does not exist.");
            if (channel.inputLeft == channel.inputRight || channel.outputLeft == channel.outputRight)
                return fail("Stereo left and right mappings must use distinct physical channels.");
            if (channel.inputLeft != 0 || channel.inputRight != 1 ||
                channel.outputLeft != 0 || channel.outputRight != 1)
                return fail("The current stereo graph is fixed to capture inputs 1/2 and render outputs 1/2.");
        }
        if (channel.enabled) {
            if (!usedInputs.insert(channel.inputLeft).second && !caps.supportsDuplicateInputs)
                return fail("Duplicate physical input assignments are unsupported by the active graph.");
            if (channel.mode == SetupChannelMode::Stereo &&
                !usedInputs.insert(channel.inputRight).second && !caps.supportsDuplicateInputs)
                return fail("Duplicate physical input assignments are unsupported by the active graph.");
        }
    }
    return {true, {}};
}

bool save_audio_setup(const AudioSetupConfig& config, const std::string& path, std::string* error) {
    const AudioTopologyCapabilities current{};
    const auto validation = validate_audio_setup(config, current);
    if (!validation.valid) {
        if (error) *error = validation.error;
        return false;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error) *error = "Could not open audio setup file for writing.";
        return false;
    }
    out << "AUDIOCORE_SETUP " << config.version << '\n'
        << std::quoted(config.captureDeviceId) << ' ' << std::quoted(config.renderDeviceId) << '\n'
        << config.requestedSampleRate << ' ' << config.requestedBufferFrames << '\n'
        << config.channels.size() << '\n';
    out << std::setprecision(9);
    for (const auto& c : config.channels) {
        out << c.logicalId << ' ' << std::quoted(c.name) << ' ' << static_cast<unsigned>(c.mode) << ' '
            << c.inputLeft << ' ' << c.inputRight << ' ' << c.enabled << ' '
            << c.outputLeft << ' ' << c.outputRight << ' ' << c.gainDb << ' ' << c.pan << ' ' << c.muted << ' '
            << c.dspBypass << ' ' << c.peqEnabled << ' ' << c.compressorEnabled << '\n';
    }
    if (!out) {
        if (error) *error = "Could not write the complete audio setup file.";
        return false;
    }
    return true;
}

bool load_audio_setup(const std::string& path, AudioSetupConfig& config, std::string* error) {
    auto fail = [&](const char* message) {
        if (error) *error = message;
        return false;
    };
    std::ifstream in(path, std::ios::binary);
    if (!in) return fail("Could not open audio setup file.");
    AudioSetupConfig candidate;
    std::string magic;
    if (!(in >> magic >> candidate.version) || magic != "AUDIOCORE_SETUP")
        return fail("Audio setup file header is invalid.");
    if (candidate.version != 1) return fail("Audio setup file version is not supported.");
    if (!(in >> std::quoted(candidate.captureDeviceId) >> std::quoted(candidate.renderDeviceId) >>
          candidate.requestedSampleRate >> candidate.requestedBufferFrames))
        return fail("Audio setup file is incomplete or malformed.");
    size_t count = 0;
    if (!(in >> count) || count == 0 || count > 64) return fail("Audio setup channel count is invalid.");
    candidate.channels.clear();
    for (size_t i = 0; i < count; ++i) {
        SetupChannel c;
        unsigned mode = 0;
        if (!(in >> c.logicalId >> std::quoted(c.name) >> mode >> c.inputLeft >> c.inputRight >> c.enabled >>
              c.outputLeft >> c.outputRight >> c.gainDb >> c.pan >> c.muted >> c.dspBypass >> c.peqEnabled >>
              c.compressorEnabled) || mode > 1)
            return fail("Audio setup channel record is malformed.");
        c.mode = static_cast<SetupChannelMode>(mode);
        candidate.channels.push_back(std::move(c));
    }
    in >> std::ws;
    if (!in.eof()) return fail("Audio setup file has unexpected trailing data.");
    const auto validation = validate_audio_setup(candidate, AudioTopologyCapabilities{});
    if (!validation.valid) {
        if (error) *error = validation.error;
        return false;
    }
    config = std::move(candidate);
    return true;
}

std::string default_audio_setup_path() {
    const char* appData = std::getenv("APPDATA");
    std::filesystem::path base = appData && *appData ? std::filesystem::path(appData)
                                                     : std::filesystem::current_path();
    base /= "AudioCore";
    std::error_code ec;
    std::filesystem::create_directories(base, ec);
    return (base / "audio_setup_v1.cfg").string();
}
