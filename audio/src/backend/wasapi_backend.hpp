#pragma once

#include "audiocore/audio_backend.hpp"
#include "audiocore/audio_buffer.hpp"
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <functional>

namespace audiocore {

/**
 * @brief WASAPI Share Mode selection.
 */
enum class WasapiShareMode {
    Shared,
    Exclusive
};

/**
 * @brief Information about an available WASAPI audio endpoint.
 */
struct WasapiDeviceInfo {
    std::string id;
    std::string name;
    std::string friendlyName;
    bool isDefault{false};
    bool isCapture{false};
    bool isLoopback{false};
    uint32_t channels{0};
    uint32_t defaultSampleRate{48000};
    std::vector<uint32_t> supportedSampleRates;
    double minDevicePeriodMs{0.0};
    double defaultDevicePeriodMs{0.0};
    bool isAvailable{true};
};

/**
 * @brief WASAPI streaming operational mode.
 */
enum class WasapiStreamMode {
    PlaybackOnly,
    CaptureOnly,
    Duplex
};

/**
 * @brief Audio Core Audio Source Type.
 */
enum class AudioSourceType {
    FilePlayback,       // Mode A: Audio File Playback -> DSP -> WASAPI Output
    AudioInterface,     // Mode B: Audio Interface Input -> DSP -> WASAPI Output
    SystemLoopback      // Mode C: Windows System Loopback -> DSP -> WASAPI Output
};

/**
 * @brief Native Windows WASAPI AudioBackend implementation.
 *
 * Provides low-latency, event-driven audio streaming with hardware endpoint
 * enumeration, automatic format negotiation, shared and exclusive mode support,
 * device removal handling via IMMNotificationClient, and realtime telemetry.
 *
 * Realtime Safety:
 * - Zero dynamic allocations in the audio thread.
 * - Platform-independent headers via Pimpl.
 * - Thread priority boosted via AvSetMmThreadCharacteristics ("Pro Audio").
 * - Event-driven callback synchronization.
 */
class WasapiBackend : public AudioBackend {
public:
    WasapiBackend();
    ~WasapiBackend() override;

    // Non-copyable, movable
    WasapiBackend(const WasapiBackend&) = delete;
    WasapiBackend& operator=(const WasapiBackend&) = delete;
    WasapiBackend(WasapiBackend&&) noexcept;
    WasapiBackend& operator=(WasapiBackend&&) noexcept;

    // --- AudioBackend Interface ---
    AudioBackendType getBackendType() const noexcept override { return AudioBackendType::WASAPI; }

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

    // --- WASAPI-specific Device & Stream Configuration ---
    static std::vector<WasapiDeviceInfo> enumerateDevices(bool capture = false);

    bool setPlaybackDevice(const std::string& deviceId);
    bool setCaptureDevice(const std::string& deviceId);
    bool setPlaybackDeviceId(const std::string& deviceId) { return setPlaybackDevice(deviceId); }
    bool setCaptureDeviceId(const std::string& deviceId) { return setCaptureDevice(deviceId); }
    // Select one physical capture channel per logical mono strip. Each source
    // is duplicated to an internal stereo pair so existing stereo FX nodes can
    // process that strip independently before the main mix.
    bool setCaptureChannelMap(const std::vector<uint32_t>& physicalChannels);

    void setStreamMode(WasapiStreamMode mode);
    WasapiStreamMode getStreamMode() const;

    void setShareMode(WasapiShareMode mode);
    WasapiShareMode getShareMode() const;

    void setEventDriven(bool enabled);
    bool isEventDriven() const;

    bool setRequestedSampleRate(double rate);
    double getRequestedSampleRate() const;

    bool setRequestedBlockSize(uint32_t frames);
    uint32_t getRequestedBlockSize() const;

    bool validateConfiguration(std::string* outDetails = nullptr);
    bool validateConfiguration(double sampleRate, uint32_t blockSize, WasapiShareMode shareMode, std::string* outDetails = nullptr);

    // --- Status & Negotiated Telemetry ---
    std::string getActivePlaybackDeviceName() const;
    std::string getActiveCaptureDeviceName() const;
    std::string getActivePlaybackDeviceId() const;
    std::string getActiveCaptureDeviceId() const;

    uint32_t getActualBufferSizeFrames() const;
    uint32_t getBufferSizeFrames() const { return getActualBufferSizeFrames(); }
    uint32_t getBitDepth() const;
    std::string getFormatDescription() const;
    std::string getCaptureFormatDescription() const;
    double getActualPeriodicityMs() const;
    double getReportedLatencyMs() const;
    double getNominalBufferDurationMs() const;
    double getEstimatedLatencyMs() const;

    bool isDeviceInvalidated() const;
    std::string getLastErrorMessage() const;
    uint32_t getLastErrorCode() const;

    // --- Audio Thread Telemetry Access ---
    void* getAudioThreadHandle() const;
    int32_t getCurrentProcessorNumber() const;

    // --- Audio Source Selection & Loopback Routing ---
    void setAudioSourceType(AudioSourceType type);
    AudioSourceType getAudioSourceType() const;

    static std::vector<WasapiDeviceInfo> enumerateLoopbackEndpoints();

    bool setLoopbackDevice(const std::string& deviceId);
    bool setLoopbackDeviceId(const std::string& deviceId) { return setLoopbackDevice(deviceId); }
    std::string getLoopbackDeviceId() const;
    std::string getActiveLoopbackDeviceName() const;

    bool checkFeedbackRisk(std::string* outWarning = nullptr) const;

    void setChannelMappingMode(int mode);
    int getChannelMappingMode() const;

    double getCaptureSampleRate() const;
    uint32_t getCaptureChannels() const;
    uint32_t getCaptureBufferSizeFrames() const;

    double getClockDriftPpm() const;
    double getBufferOccupancyPercent() const;

    /**
     * @brief Configure safety feedback guard for duplex/capture testing.
     * When enabled, prevents acoustic howl by attenuating input or enforcing safe gain.
     */
    void setSafeFeedbackGuard(bool enabled, float maxLinearGain = 0.05f);
    bool isSafeFeedbackGuardEnabled() const;

    // --- Device Removal / Notification Callback ---
    using DeviceChangeCallback = std::function<void(const std::string& eventType, const std::string& deviceId)>;
    void setDeviceChangeCallback(DeviceChangeCallback cb);

    // --- Diagnostic Telemetry & Statistics ---
    struct Diagnostics {
        // Counts
        uint64_t totalCallbacks{0};
        uint64_t totalFramesRequested{0};
        uint64_t totalFramesProcessed{0};
        uint64_t totalFramesSubmitted{0};
        uint32_t frameCountMismatches{0};

        // Errors & XRUNs
        uint32_t renderUnderruns{0};
        uint32_t renderErrors{0};
        uint32_t captureOverruns{0};
        uint32_t captureUnderflows{0};
        uint32_t captureErrors{0};
        uint32_t eventWaitErrors{0};
        uint32_t eventWaitTimeouts{0};
        uint32_t requestedBufferFrames{0};
        uint32_t actualRenderBufferFrames{0};
        uint32_t actualCaptureBufferFrames{0};
        uint32_t renderChannels{0};
        uint32_t captureChannels{0};
        uint32_t renderBitDepth{0};
        uint32_t captureBitDepth{0};
        double requestedSampleRate{0.0};
        double actualRenderSampleRate{0.0};
        double actualCaptureSampleRate{0.0};
        double actualRenderPeriodMs{0.0};
        double actualCapturePeriodMs{0.0};
        double endpointDefaultPeriodMs{0.0};
        double captureClockCorrectionPpm{0.0};
        uint32_t bufferDiscontinuities{0};
        uint32_t getBufferFailures{0};
        uint32_t releaseBufferFailures{0};
        uint32_t deadlineMisses{0};
        uint32_t deviceInvalidations{0};
        uint32_t streamRestartCount{0};

        uint32_t sampleRateMismatches{0};
        uint32_t channelMismatches{0};
        uint32_t formatMismatches{0};

        // Timing (microseconds)
        uint64_t minCallbackTimeUs{UINT64_MAX};
        uint64_t avgCallbackTimeUs{0};
        uint64_t maxCallbackTimeUs{0};
        uint64_t p95CallbackTimeUs{0};
        uint64_t p99CallbackTimeUs{0};

        // Capture Timing (microseconds)
        uint64_t totalFramesCaptured{0};
        uint64_t totalCaptureFramesDropped{0};
        uint64_t minCaptureTimeUs{UINT64_MAX};
        uint64_t avgCaptureTimeUs{0};
        uint64_t maxCaptureTimeUs{0};
        uint64_t p95CaptureTimeUs{0};
        uint64_t p99CaptureTimeUs{0};

        // Render Timing (microseconds)
        uint64_t minRenderTimeUs{UINT64_MAX};
        uint64_t avgRenderTimeUs{0};
        uint64_t maxRenderTimeUs{0};
        uint64_t p95RenderTimeUs{0};
        uint64_t p99RenderTimeUs{0};

        // Backward compatibility aliases
        uint64_t minProcessTimeUs{UINT64_MAX};
        uint64_t avgProcessTimeUs{0};
        uint64_t maxProcessTimeUs{0};

        uint64_t minDspTimeUs{UINT64_MAX};
        uint64_t avgDspTimeUs{0};
        uint64_t maxDspTimeUs{0};
        uint64_t p95DspTimeUs{0};
        uint64_t p99DspTimeUs{0};

        uint64_t avgSubmissionTimeUs{0};
        uint64_t maxSubmissionTimeUs{0};

        uint64_t avgWakeupJitterUs{0};
        uint64_t maxWakeupJitterUs{0};

        double periodDurationMs{0.0};
        double engineBlockDurationMs{0.0};
        double renderBufferDurationMs{0.0};
        double blockDeadlineMs{0.0};
        double measuredEventPeriodMs{0.0};
        double realtimeUtilizationPercent{0.0};
        double maxRealtimeUtilizationPercent{0.0};

        double clockDriftPpm{0.0};
        bool clockDriftMeasurementValid{false};
        double bufferOccupancyPercent{0.0};
        uint32_t nanCount{0};
        uint32_t infCount{0};
        bool loopbackActive{false};
        bool feedbackRiskDetected{false};
        bool captureOperational{false};
        bool dspOperational{false};
        bool renderOperational{false};

        bool mmcssRegistered{false};
        uint32_t mmcssErrorCode{0};

        uint32_t lastHresult{0};
        std::string lastErrorMessage;
    };

    struct RealtimeMetrics {
        uint64_t averageDspTimeUs{0};
        uint32_t renderUnderruns{0};
        uint32_t deadlineMisses{0};
        bool captureOperational{false};
        bool dspOperational{false};
        bool renderOperational{false};
    };

    Diagnostics getDiagnostics() const;
    // Lock-free counters for live UI telemetry. Unlike Diagnostics this does
    // not copy strings or inspect mutable endpoint names while streaming.
    RealtimeMetrics getRealtimeMetrics() const noexcept;
    void resetDiagnostics();

    // --- Automated Benchmark Matrix ---
    struct BenchmarkResult {
        double sampleRate{48000.0};
        double actualSampleRate{0.0};
        uint32_t requestedBuffer{128};
        uint32_t actualBuffer{0};
        std::string status{"NOT TESTED"}; // PASS, FAIL, UNSUPPORTED, NOT TESTED
        double avgCallbackUs{0.0};
        double p95CallbackUs{0.0};
        double p99CallbackUs{0.0};
        double maxCallbackUs{0.0};
        double utilizationPct{0.0};
        uint32_t underruns{0};
        uint32_t deadlineMisses{0};
        std::string notes;
    };

    std::vector<BenchmarkResult> runBenchmarkMatrix(uint32_t testBlocksPerConfig = 200);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace audiocore
