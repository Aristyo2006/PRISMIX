#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <memory>

enum class BackendType {
    MOCK = 0,
    JACK = 1,
    ALSA = 2,
    WASAPI = 3,
    ASIO = 4
};

enum class SampleRate {
    SR_44100 = 44100,
    SR_48000 = 48000,
    SR_88200 = 88200,
    SR_96000 = 96000
};

enum class BitDepth {
    BIT_16 = 16,
    BIT_24 = 24,
    FLOAT_32 = 32
};

struct AudioDeviceConfig {
    BackendType backend_type = BackendType::MOCK;
    std::string input_device = "MOCK SIMULATOR IN";
    std::string output_device = "MOCK SIMULATOR OUT";
    SampleRate sample_rate = SampleRate::SR_48000;
    BitDepth bit_depth = BitDepth::BIT_24;
    int buffer_size = 256;
    bool is_active = false;
    float cpu_load = 12.0f;
    uint32_t xruns = 0;
};

// Abstract AudioBackend class
class AudioBackend {
public:
    virtual ~AudioBackend() = default;

    virtual BackendType get_type() const = 0;
    virtual const char* get_name() const = 0;

    virtual bool initialize(const AudioDeviceConfig& config) = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool is_running() const = 0;

    virtual std::vector<std::string> enumerate_input_devices() = 0;
    virtual std::vector<std::string> enumerate_output_devices() = 0;

    virtual AudioDeviceConfig get_config() const = 0;
    virtual void set_config(const AudioDeviceConfig& config) = 0;

    // Periodic telemetry update called by UI or simulator timer
    virtual void update_telemetry() = 0;
};

// JACK Backend Abstraction (prepared for Linux/JACK integration)
class JackBackend : public AudioBackend {
public:
    JackBackend();
    ~JackBackend() override = default;

    BackendType get_type() const override { return BackendType::JACK; }
    const char* get_name() const override { return "JACK Audio Connection Kit"; }

    bool initialize(const AudioDeviceConfig& config) override;
    bool start() override;
    void stop() override;
    bool is_running() const override { return running_; }

    std::vector<std::string> enumerate_input_devices() override;
    std::vector<std::string> enumerate_output_devices() override;

    AudioDeviceConfig get_config() const override { return config_; }
    void set_config(const AudioDeviceConfig& config) override { config_ = config; }

    void update_telemetry() override;

private:
    AudioDeviceConfig config_{};
    bool running_ = false;
};

// ALSA Backend Abstraction (prepared for ARM64 / Raspberry Pi / S905X integration)
class AlsaBackend : public AudioBackend {
public:
    AlsaBackend();
    ~AlsaBackend() override = default;

    BackendType get_type() const override { return BackendType::ALSA; }
    const char* get_name() const override { return "ALSA Advanced Linux Sound"; }

    bool initialize(const AudioDeviceConfig& config) override;
    bool start() override;
    void stop() override;
    bool is_running() const override { return running_; }

    std::vector<std::string> enumerate_input_devices() override;
    std::vector<std::string> enumerate_output_devices() override;

    AudioDeviceConfig get_config() const override { return config_; }
    void set_config(const AudioDeviceConfig& config) override { config_ = config; }

    void update_telemetry() override;

private:
    AudioDeviceConfig config_{};
    bool running_ = false;
};

