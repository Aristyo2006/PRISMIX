#pragma once

#include "audiocore/audio_backend.hpp"
#include "audiocore/audio_buffer.hpp"
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <atomic>
#include <functional>

namespace audiocore {

/**
 * @brief Information about an available JACK audio device / port group.
 */
struct JackDeviceInfo {
    std::string id;
    std::string name;
    std::string friendlyName;
    bool isDefault{false};
    bool isCapture{false};
    uint32_t channels{0};
    uint32_t sampleRate{48000};
    bool isAvailable{true};
};

/**
 * @brief Native Linux JACK AudioBackend implementation.
 *
 * Provides realtime-safe JACK audio client integration with dynamic port registration,
 * sample rate / buffer size adaptation, XRUN tracking, and nonblocking telemetry.
 *
 * Realtime Safety:
 * - process() executes inside JACK's realtime thread with ZERO dynamic allocations.
 * - Zero file, JSON, network I/O, and zero blocking mutex acquisitions in process().
 * - Audio transfers between JACK 32-bit float buffers and AudioCore AudioBuffer are bounded and lock-free.
 * - Thread-safe atomic counters for telemetry (XRUNs, callback counts, DSP time).
 */
class JackBackend : public AudioBackend {
public:
    JackBackend();
    ~JackBackend() override;

    // Non-copyable, non-movable
    JackBackend(const JackBackend&) = delete;
    JackBackend& operator=(const JackBackend&) = delete;

    // --- AudioBackend Interface ---
    AudioBackendType getBackendType() const noexcept override { return AudioBackendType::JACK; }

    bool initialize(
        double sampleRate = 48000.0,
        uint32_t blockSize = 128,
        uint32_t channels = 2) override;

    bool start() override;
    void stop() override;

    bool isRunning() const override;
    double getSampleRate() const override;
    uint32_t getBlockSize() const override;
    uint32_t getChannels() const override;

    // --- JACK Configuration & Device Management ---
    static std::vector<JackDeviceInfo> enumerateDevices(bool capture = false);

    bool setClientName(const std::string& name);
    std::string getClientName() const;

    bool setCaptureDevice(const std::string& deviceId);
    bool setPlaybackDevice(const std::string& deviceId);
    bool setPlaybackDeviceId(const std::string& deviceId) { return setPlaybackDevice(deviceId); }
    bool setCaptureDeviceId(const std::string& deviceId) { return setCaptureDevice(deviceId); }

    // Physical capture channel mapping for console strips
    bool setCaptureChannelMap(const std::vector<uint32_t>& physicalChannels);

    // Compatibility setters matching AudioService expectations
    void setAudioSourceType(int /*type*/) {}
    void setStreamMode(int /*mode*/) {}
    void setShareMode(int /*mode*/) {}
    void setEventDriven(bool /*enabled*/) {}
    void setSafeFeedbackGuard(bool enabled, float maxGain = 0.05f);
    bool isSafeFeedbackGuardEnabled() const;

    bool setRequestedSampleRate(double rate);
    double getRequestedSampleRate() const;
    bool setRequestedBlockSize(uint32_t frames);
    uint32_t getRequestedBlockSize() const;

    // Port connection control
    void setAutoConnect(bool enabled) { autoConnect_ = enabled; }
    bool isAutoConnect() const { return autoConnect_; }

    // --- Status & Telemetry ---
    std::string getActivePlaybackDeviceName() const;
    std::string getActiveCaptureDeviceName() const;
    std::string getActivePlaybackDeviceId() const;
    std::string getActiveCaptureDeviceId() const;

    uint32_t getActualBufferSizeFrames() const;
    uint32_t getBufferSizeFrames() const { return getActualBufferSizeFrames(); }
    uint32_t getBitDepth() const { return 32; } // JACK internal is always 32-bit float
    std::string getFormatDescription() const;
    std::string getCaptureFormatDescription() const;

    uint32_t getCaptureChannels() const;

    std::string getLastErrorMessage() const;

    // Realtime metrics for lock-free UI telemetry
    struct RealtimeMetrics {
        uint64_t averageDspTimeUs{0};
        uint32_t renderUnderruns{0}; // XRUNs reported by JACK
        uint32_t deadlineMisses{0};
        bool captureOperational{false};
        bool dspOperational{false};
        bool renderOperational{false};
    };

    RealtimeMetrics getRealtimeMetrics() const noexcept;
    void resetDiagnostics();

    // Internal process entry point called by JACK C callback
    int processCallback(uint32_t nframes) noexcept;
    int bufferSizeCallback(uint32_t nframes) noexcept;
    int sampleRateCallback(uint32_t nframes) noexcept;
    int xrunCallback() noexcept;
    void shutdownCallback() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool autoConnect_{true};
};

} // namespace audiocore

