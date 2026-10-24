#include "audio_backend.h"

// ---------------- JACK Backend Implementation ----------------
JackBackend::JackBackend() {
    config_.backend_type = BackendType::JACK;
    config_.input_device = "system:capture_1, system:capture_2";
    config_.output_device = "system:playback_1, system:playback_2";
    config_.sample_rate = SampleRate::SR_48000;
    config_.bit_depth = BitDepth::FLOAT_32;
    config_.buffer_size = 128;
    config_.is_active = false;
    config_.cpu_load = 4.2f;
    config_.xruns = 0;
}

bool JackBackend::initialize(const AudioDeviceConfig& config) {
    config_ = config;
    config_.backend_type = BackendType::JACK;
    return true;
}

bool JackBackend::start() {
    running_ = true;
    config_.is_active = true;
    return true;
}

void JackBackend::stop() {
    running_ = false;
    config_.is_active = false;
}

std::vector<std::string> JackBackend::enumerate_input_devices() {
    return {
        "system:capture_1,2 (Stereo Mic/Line)",
        "system:capture_3,4 (Aux In)",
        "ardour:Audio/out 1,2",
        "puredata:output0,1",
        "jack_rack:out_1,2"
    };
}

std::vector<std::string> JackBackend::enumerate_output_devices() {
    return {
        "system:playback_1,2 (Main Speakers)",
        "system:playback_3,4 (Headphones)",
        "system:playback_5,6 (Studio Mon)",
        "ardour:Audio/in 1,2",
        "japa:in_1,2"
    };
}

void JackBackend::update_telemetry() {
    // In real JACK mode, jack_cpu_load(client) and jack_get_xrun_delayed_usecs() will be read here
    if (running_) {
        config_.cpu_load = 5.8f;
    }
}

// ---------------- ALSA Backend Implementation ----------------
AlsaBackend::AlsaBackend() {
    config_.backend_type = BackendType::ALSA;
    config_.input_device = "hw:0,0 [snd_bcm2835]";
    config_.output_device = "hw:0,0 [snd_bcm2835]";
    config_.sample_rate = SampleRate::SR_48000;
    config_.bit_depth = BitDepth::BIT_24;
    config_.buffer_size = 256;
    config_.is_active = false;
    config_.cpu_load = 6.4f;
    config_.xruns = 0;
}

bool AlsaBackend::initialize(const AudioDeviceConfig& config) {
    config_ = config;
    config_.backend_type = BackendType::ALSA;
    return true;
}

bool AlsaBackend::start() {
    running_ = true;
    config_.is_active = true;
    return true;
}

void AlsaBackend::stop() {
    running_ = false;
    config_.is_active = false;
}

std::vector<std::string> AlsaBackend::enumerate_input_devices() {
    return {
        "hw:0,0 [I2S Master ADC (S905X/Pi)]",
        "hw:1,0 [USB Audio Codec Capture]",
        "hw:CARD=Core,DEV=0 [Analog Front IN]"
    };
}

std::vector<std::string> AlsaBackend::enumerate_output_devices() {
    return {
        "hw:0,0 [I2S Master DAC (S905X/Pi)]",
        "hw:1,0 [USB Audio Codec Playback]",
        "hw:CARD=Core,DEV=0 [Analog Line OUT]"
    };
}

void AlsaBackend::update_telemetry() {
    if (running_) {
        config_.cpu_load = 7.1f;
    }
}

