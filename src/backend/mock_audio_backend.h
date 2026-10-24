#pragma once
#include "audio_backend.h"
#include "../dsp/analyzer_model.h"
#include <string>
#include <vector>

enum class GeneratorSignal {
    SINE_1KHZ = 0,
    PINK_NOISE = 1,
    WHITE_NOISE = 2,
    MULTI_TONE = 3,
    SWEEP_LOG = 4
};

class MockAudioBackend : public AudioBackend {
public:
    MockAudioBackend();
    ~MockAudioBackend() override = default;

    BackendType get_type() const override { return BackendType::MOCK; }
    const char* get_name() const override { return "Mock Audio Engine Simulator"; }

    bool initialize(const AudioDeviceConfig& config) override;
    bool start() override;
    void stop() override;
    bool is_running() const override { return running_; }

    std::vector<std::string> enumerate_input_devices() override;
    std::vector<std::string> enumerate_output_devices() override;

    AudioDeviceConfig get_config() const override { return config_; }
    void set_config(const AudioDeviceConfig& config) override { config_ = config; }

    void update_telemetry() override;

    void init();
    void update();

    // Signal generator controls
    void set_generator_signal(GeneratorSignal sig) { signal_type_ = sig; }
    GeneratorSignal get_generator_signal() const { return signal_type_; }
    const char* get_generator_signal_name() const;

    // RTA Analyzer instance
    RtaAnalyzer& get_analyzer() { return analyzer_; }
    const RtaAnalyzer& get_analyzer() const { return analyzer_; }

private:
    AudioDeviceConfig config_{};
    bool running_ = true;
    GeneratorSignal signal_type_ = GeneratorSignal::PINK_NOISE;
    RtaAnalyzer analyzer_{};
    float phase_ = 0.0f;
    float sweep_freq_ = 20.0f;
};

extern MockAudioBackend g_backend;
