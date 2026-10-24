#pragma once

#include "audiocore/audio_buffer.hpp"
#include "audiocore/audio_node.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace audiocore { class MeterNode; }

enum class AudioMode : uint8_t { Simulation, RealAudio };
enum class AudioServiceState : uint8_t { Stopped, Initializing, Streaming, Stopping, Error };

enum class StereoControlId : uint8_t {
    GainDb,
    Pan,
    DspBypass,
    PeqEnabled,
    CompressorEnabled
};

struct StereoDemoControls {
    float gainDb = 0.0f;
    float pan = 0.0f;
    bool bypass = false;
    bool peqEnabled = true;
    bool compressorEnabled = true;
};

struct AudioDeviceChoice {
    std::string id;
    std::string name;
    bool isDefault = false;
    uint32_t channels = 0;
    bool isCapture = false;
};

enum class AudioOutputRoute : uint8_t { None = 0, MainLR = 1, Aux = 2, Omni = 3 };
enum class AudioStripFormat : uint8_t { Mono = 0, Stereo = 1 };

struct StripFxParameter {
    std::string id;
    float value = 0.0f;
};

struct AudioStripFxConfig {
    std::string uri;
    bool bypassed = false;
    std::vector<StripFxParameter> parameters;
};

struct AudioStripConfig {
    uint32_t logicalId = 0;
    std::string name;
    AudioStripFormat format = AudioStripFormat::Mono;
    uint32_t captureChannel = 0;
    uint32_t captureChannelRight = 1;
    float gainDb = 0.0f;
    float returnGainDb = 0.0f;
    float pan = 0.0f;
    bool muted = false;
    bool solo = false;
    bool routeToMain = true;
    AudioOutputRoute outputRoute = AudioOutputRoute::MainLR;
    uint32_t outputRouteIndex = 0;
    bool polarityInverted = false;
    bool hpfEnabled = true;
    float hpfFrequency = 80.0f;
    bool peqBypassed = false;
    std::array<float, 4> eqFrequency{80.0f, 350.0f, 2500.0f, 10000.0f};
    std::array<float, 4> eqGain{};
    std::array<float, 4> eqQ{0.7f, 1.0f, 1.0f, 0.7f};
    std::array<uint8_t, 4> eqType{2, 1, 1, 3}; // FilterType: LowShelf, Peaking, HighShelf
    std::array<bool, 4> eqBandEnabled{true, true, true, true};
    bool geqMode = false;
    bool geqBypassed = false;
    std::array<float, 31> geqGain{};
    bool compressorEnabled = true;
    float compressorThresholdDb = -18.0f;
    float compressorRatio = 4.0f;
    float compressorAttackMs = 10.0f;
    float compressorReleaseMs = 80.0f;
    float compressorKneeDb = 6.0f;
    float compressorMakeupDb = 0.0f;
    bool gateEnabled = true;
    float gateThresholdDb = -48.0f;
    float gateRangeDb = -60.0f;
    float gateAttackMs = 5.0f;
    float gateHoldMs = 50.0f;
    float gateReleaseMs = 120.0f;
    bool limiterEnabled = false;
    float limiterCeilingDb = -0.3f;
    float limiterReleaseMs = 50.0f;
    std::array<float, 4> auxSendLevel{};
    std::array<bool, 4> auxSendEnabled{true, true, true, true};
    std::array<bool, 4> auxSendPreFader{false, false, false, false};
    bool speakerDelayEnabled = false;
    bool speakerDelayLink = true;
    float speakerDelayMsL = 0.0f;
    float speakerDelayMsR = 0.0f;
    std::vector<AudioStripFxConfig> fxRack;
};

using AudioBusConfig = AudioStripConfig;

struct AfsNotchTelemetry {
    uint8_t id = 0;
    float frequencyHz = 1000.0f;
    float depthDb = 0.0f;
    float q = 15.0f;
    uint8_t state = 0; // 0: Inactive, 1: Candidate, 2: Active, 3: Holding, 4: Releasing
};

struct StripAudioTelemetry {
    uint32_t logicalId = 0;
    uint32_t channelCount = 1;
    std::array<float, 2> inputPeak{};
    std::array<float, 2> inputRms{};
    std::array<float, 2> fxInputPeak{};
    std::array<float, 2> fxInputRms{};
    std::array<float, 2> fxOutputPeak{};
    std::array<float, 2> fxOutputRms{};
    std::array<float, 2> outputPeak{};
    std::array<float, 2> outputRms{};
    std::array<float, 2> postFaderPeak{};
    std::array<float, 2> preLimiterPeak{};
    float gainReductionDb = 0.0f;
    float gateGainReductionDb = 0.0f;
    float limiterGainReductionDb = 0.0f;
    float compressorDetectorDb = -90.0f;
    float compressorOutputDb = -90.0f;
    float compressorOutputDbR = -90.0f;
    float gateDetectorDb = -90.0f;
    uint8_t gateState = 0;
    bool afsAssigned = false;
    uint8_t afsState = 0; // 0: DISABLED, 1: BYPASS, 2: MONITORING, 3: CANDIDATE, 4: ACTIVE
    uint8_t afsActiveNotchCount = 0;
    uint8_t afsMaxNotches = 8;
    std::array<AfsNotchTelemetry, 8> afsNotches{};
    // Gain reduction (dB, <= 0) of each FX Rack slot hosting a dynamics processor.
    std::array<float, 8> fxSlotGrDb{};
    // Real Expander / Gate telemetry of each FX Rack slot hosting the gate:
    // gain reduction (dB, <= 0), detector level (dBFS), and GateState code.
    // GateState: 0 Closed, 1 Attack, 2 Open, 3 Hold, 4 Release.
    std::array<float, 8> fxSlotGateGrDb{};
    std::array<float, 8> fxSlotGateDetectorDb{};
    std::array<uint8_t, 8> fxSlotGateState{};
    // Real Brickwall Limiter gain-reduction telemetry (dB, <= 0) of each FX Rack
    // slot hosting the limiter.
    std::array<float, 8> fxSlotLimiterGrDb{};
};

struct AudioServiceSnapshot {
    AudioMode mode = AudioMode::Simulation;
    AudioServiceState state = AudioServiceState::Stopped;
    double actualSampleRate = 0.0;
    uint32_t actualBufferFrames = 0;
    uint32_t captureChannels = 0;
    uint32_t renderChannels = 0;
    uint64_t dspTimeUs = 0;
    uint32_t underruns = 0;
    std::array<float, 2> inputPeak{};
    std::array<float, 2> inputRms{};
    std::array<float, 2> masterFxInputPeak{};
    std::array<float, 2> masterFxInputRms{};
    std::array<float, 2> masterFxOutputPeak{};
    std::array<float, 2> masterFxOutputRms{};
    std::array<float, 2> preLimiterPeak{};
    std::array<float, 2> outputPeak{};
    std::array<float, 2> outputRms{};
    float compressorGainReductionDb = 0.0f;
    float limiterGainReductionDb = 0.0f;
    float masterCompressorDetectorDb = -90.0f;
    float masterCompressorOutputDb = -90.0f;
    float masterCompressorOutputDbR = -90.0f;
    float masterGateDetectorDb = -90.0f;
    float masterGateGainReductionDb = 0.0f;
    uint8_t masterGateState = 0;
    bool masterAfsAssigned = false;
    uint8_t masterAfsState = 0;
    uint8_t masterAfsActiveNotchCount = 0;
    uint8_t masterAfsMaxNotches = 8;
    std::array<AfsNotchTelemetry, 8> masterAfsNotches{};
    StereoDemoControls controls{};
    std::string actualFormat;
    std::string actualCaptureDevice;
    std::string actualRenderDevice;
    std::string captureDeviceId;
    std::string renderDeviceId;
    std::string error;
    std::vector<AudioDeviceChoice> captureDevices;
    std::vector<AudioDeviceChoice> renderDevices;
    std::vector<StripAudioTelemetry> strips;
    std::array<StripAudioTelemetry, 4> auxBuses{};
    bool devicesReady = false;
};

// A bounded single-producer/single-consumer control bridge. The UI is the
// producer; the audio graph drains commands at the start of its DSP block.
class StereoDemoDspNode final : public audiocore::AudioNode {
public:
    explicit StereoDemoDspNode(double sampleRate = 48000.0, uint32_t maxFrames = 128);
    ~StereoDemoDspNode() override;

    StereoDemoDspNode(const StereoDemoDspNode&) = delete;
    StereoDemoDspNode& operator=(const StereoDemoDspNode&) = delete;

    bool setGainDb(float value) noexcept;
    bool setPan(float value) noexcept;
    bool setDspBypass(bool enabled) noexcept;
    bool setPeqEnabled(bool enabled) noexcept;
    bool setCompressorEnabled(bool enabled) noexcept;
    StereoDemoControls requestedControls() const noexcept;
    StereoDemoControls appliedControls() const noexcept;
    uint32_t droppedCommandCount() const noexcept;
    float compressorGainReductionDb() const noexcept;
    float compressorDetectorDb() const noexcept;
    float compressorOutputDb() const noexcept;

    void process(const audiocore::AudioBuffer& input,
                 audiocore::AudioBuffer& output,
                 uint32_t frames) override;
    void reset() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class AnalyzerRingBuffer {
public:
    static constexpr size_t CAPACITY = 16384;
    static constexpr size_t MASK = CAPACITY - 1;

    void push(const float* data, size_t count) noexcept {
        if (!data || count == 0) return;
        const size_t wr = write_head_.load(std::memory_order_relaxed);
        for (size_t i = 0; i < count; ++i) {
            buffer_[(wr + i) & MASK] = data[i];
        }
        write_head_.store(wr + count, std::memory_order_release);
    }

    size_t pull(float* dest, size_t maxCount) noexcept {
        if (!dest || maxCount == 0) return 0;
        const size_t wr = write_head_.load(std::memory_order_acquire);
        const size_t rd = read_tail_.load(std::memory_order_relaxed);
        const size_t available = wr - rd;
        const size_t toRead = std::min(available, maxCount);
        for (size_t i = 0; i < toRead; ++i) {
            dest[i] = buffer_[(rd + i) & MASK];
        }
        read_tail_.store(rd + toRead, std::memory_order_release);
        return toRead;
    }

    size_t available() const noexcept {
        const size_t wr = write_head_.load(std::memory_order_acquire);
        const size_t rd = read_tail_.load(std::memory_order_relaxed);
        return wr - rd;
    }

    void reset() noexcept {
        write_head_.store(0, std::memory_order_relaxed);
        read_tail_.store(0, std::memory_order_relaxed);
    }

private:
    std::array<float, CAPACITY> buffer_{};
    std::atomic<size_t> write_head_{0};
    std::atomic<size_t> read_tail_{0};
};

class StripInsertDspNode final : public audiocore::AudioNode {
public:
    StripInsertDspNode(const std::vector<AudioStripConfig>& strips, double sampleRate, uint32_t maxFrames);
    ~StripInsertDspNode() override;
    void process(const audiocore::AudioBuffer& input, audiocore::AudioBuffer& output,
                 uint32_t frames) override;
    void reset() override;
    std::vector<StripAudioTelemetry> telemetry() const;
    void updateStripDsp(const AudioStripConfig& config) noexcept;
    uint32_t stripBufferChannels(size_t index) const noexcept;
    void setAnalyzerTap(uint32_t channelId, uint8_t tapSource, AnalyzerRingBuffer* ring) noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class StripMainMixDspNode final : public audiocore::AudioNode {
public:
    StripMainMixDspNode(const std::vector<AudioStripConfig>& strips,
                        std::shared_ptr<StereoDemoDspNode> masterProcessor,
                        uint32_t renderChannels, uint32_t maxFrames,
                        double sampleRate = 48000.0);
    ~StripMainMixDspNode() override;
    void process(const audiocore::AudioBuffer& input, audiocore::AudioBuffer& output,
                 uint32_t frames) override;
    void reset() override;
    std::shared_ptr<audiocore::MeterNode> outputMeter() const;
    void updateStripByLogicalId(uint32_t logicalId, float gainDb, float pan, bool muted, bool solo) noexcept;
    void updateStripConfig(const AudioStripConfig& config) noexcept;
    void updateAuxBusConfig(size_t auxIndex, const AudioBusConfig& config) noexcept;
    void updateMasterBusConfig(const AudioBusConfig& config) noexcept;
    StripAudioTelemetry masterTelemetry() const noexcept;
    std::array<StripAudioTelemetry, 4> auxTelemetry() const noexcept;
    std::array<float, 2> stripPostFaderPeak(uint32_t logicalId) const noexcept;
    void setAnalyzerTap(uint32_t channelId, uint8_t tapSource, AnalyzerRingBuffer* ring) noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Owns one AudioEngine and its WASAPI/JACK backend. UI callers submit lifecycle
// requests; a worker owns endpoint setup/teardown and publishes UI snapshots.
class AudioService {
public:
    AudioService();
    ~AudioService();
    AudioService(const AudioService&) = delete;
    AudioService& operator=(const AudioService&) = delete;

    bool selectMode(AudioMode mode) noexcept;
    bool selectCaptureDevice(const std::string& deviceId);
    bool selectRenderDevice(const std::string& deviceId);
    bool setConsoleConfiguration(const std::vector<AudioStripConfig>& strips);
    bool setAuxBusConfigurations(const std::array<AudioBusConfig, 4>& auxBuses);
    bool setMasterBusConfiguration(const AudioBusConfig& masterBus);
    void refreshDevices();
    bool startRealAudio();
    void stop();
    bool setGainDb(float value) noexcept;
    bool setPan(float value) noexcept;
    bool setDspBypass(bool enabled) noexcept;
    bool setPeqEnabled(bool enabled) noexcept;
    bool setCompressorEnabled(bool enabled) noexcept;
    bool setRequestedAudioConfig(uint32_t sampleRate, uint32_t bufferFrames);
    uint32_t getRequestedSampleRate() const noexcept;
    uint32_t getRequestedBufferFrames() const noexcept;
    void updateLiveStrip(uint32_t logicalId, float gainDb, float pan, bool muted, bool solo);
    void updateLiveStripDsp(const AudioStripConfig& config);
    void updateLiveAuxBusDsp(size_t auxIndex, const AudioBusConfig& config);
    void updateLiveMasterBusDsp(const AudioBusConfig& config);
    void setAnalyzerTap(uint32_t logicalChannelId, uint8_t tapSource) noexcept;
    AnalyzerRingBuffer& analyzerRingBuffer() noexcept;
    AudioServiceSnapshot snapshot() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

AudioService& audio_service();
const char* audio_service_state_name(AudioServiceState state) noexcept;


