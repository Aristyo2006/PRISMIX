#include "../../src/model/audio_setup_config.h"

#include <cstdio>
#include <iostream>

namespace {
bool check(bool ok, const char* message) {
    if (!ok) std::cerr << "FAIL: " << message << '\n';
    return ok;
}
}

int main() {
    bool ok = true;
    AudioSetupConfig config;
    config.captureDeviceId = "capture-stable-id";
    config.renderDeviceId = "render-stable-id";
    config.channels[0].logicalId = 42;
    config.channels[0].name = "Vocal Pair";
    config.channels[0].gainDb = -3.5f;
    const AudioTopologyCapabilities stereo{};
    ok &= check(validate_audio_setup(config, stereo).valid, "valid stereo topology accepted");

    AudioSetupConfig twoStrips = config;
    twoStrips.channels.push_back(SetupChannel{});
    ok &= check(!validate_audio_setup(twoStrips, stereo).valid, "unsupported independent strip count rejected");
    AudioSetupConfig badInput = config;
    badInput.channels[0].inputRight = 2;
    ok &= check(!validate_audio_setup(badInput, stereo).valid, "out-of-range input mapping rejected");
    AudioSetupConfig duplicate = config;
    duplicate.channels[0].inputRight = duplicate.channels[0].inputLeft;
    ok &= check(!validate_audio_setup(duplicate, stereo).valid, "duplicate stereo input mapping rejected");
    AudioSetupConfig badOutput = config;
    badOutput.channels[0].outputRight = 2;
    ok &= check(!validate_audio_setup(badOutput, stereo).valid, "out-of-range output mapping rejected");
    AudioSetupConfig reversedRoute = config;
    reversedRoute.channels[0].inputLeft = 1;
    reversedRoute.channels[0].inputRight = 0;
    ok &= check(!validate_audio_setup(reversedRoute, stereo).valid, "unimplemented remapping rejected");
    AudioSetupConfig muted = config;
    muted.channels[0].muted = true;
    ok &= check(!validate_audio_setup(muted, stereo).valid, "unimplemented channel mute rejected");
    AudioSetupConfig unsupportedRate = config;
    unsupportedRate.requestedSampleRate = 44100;
    ok &= check(!validate_audio_setup(unsupportedRate, stereo).valid, "unimplemented requested sample rate rejected");
    AudioSetupConfig noStereoOutput = config;
    ok &= check(!validate_audio_setup(noStereoOutput, AudioTopologyCapabilities{2, 1, 1, false, true, false}).valid,
                "Main L/R rejected without two physical outputs");

    const std::string path = "audio_setup_test.cfg";
    std::string error;
    ok &= check(save_audio_setup(config, path, &error), "versioned config saved");
    AudioSetupConfig loaded;
    ok &= check(load_audio_setup(path, loaded, &error), "saved config loaded");
    ok &= check(loaded.captureDeviceId == config.captureDeviceId && loaded.renderDeviceId == config.renderDeviceId &&
                loaded.channels.size() == 1 && loaded.channels[0].logicalId == 42 &&
                loaded.channels[0].name == "Vocal Pair" && loaded.channels[0].gainDb == -3.5f,
                "stable device ids and mixer settings round-trip");
    std::remove(path.c_str());

    std::cout << (ok ? "All audio setup configuration tests passed.\n" : "Audio setup configuration tests failed.\n");
    return ok ? 0 : 1;
}
