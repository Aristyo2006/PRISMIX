#pragma once
#include <array>
#include <vector>
#include <cstdint>
#include <string>

enum class RtaMode {
    BANDS_31 = 0,
    BANDS_63 = 1,
    FFT_SMOOTH = 2
};

enum class RtaSource {
    PRE_EQ = 0,
    POST_EQ = 1
};

enum class RtaSpeed {
    FAST = 0,
    MEDIUM = 1,
    SLOW = 2
};

struct RtaDiagnostics {
    double sampleRate = 48000.0;
    uint32_t fftSize = 4096;
    float binSpacing = 11.71875f;
    uint32_t hopSize = 1024;
    std::string windowType = "Hann";
    uint32_t sourceChannel = 0;
    RtaSource tapPoint = RtaSource::POST_EQ;
    uint32_t accumulatedFrames = 0;
    uint64_t sequenceNumber = 0;
    float peakFrequencyHz = 0.0f;
    float peakAmplitudeDbfs = -72.0f;
    float dcMean = 0.0f;
    float pcmRms = 0.0f;
    float pcmPeak = 0.0f;
    uint32_t finiteSampleCount = 0;
    float displayFloorDbfs = -72.0f;
    uint32_t droppedBlocks = 0;
    uint32_t overflowCount = 0;
    bool hasValidPcm = false;
};

struct RtaSnapshot {
    RtaMode mode = RtaMode::BANDS_31;
    RtaSource source = RtaSource::POST_EQ;
    bool frozen = false;
    std::array<float, 31> bands_31{};
    std::array<float, 31> peak_hold_31{};
    std::array<float, 63> bands_63{};
    std::array<float, 63> peak_hold_63{};
    std::array<float, 64> fft_bins{};
    std::array<float, 64> fft_peak_hold{};
    RtaDiagnostics diagnostics{};
};

// Abstract AudioSource interface for feeding audio frames to analyzers
class AudioSource {
public:
    virtual ~AudioSource() = default;
    virtual const char* get_source_name() const = 0;
    virtual void get_audio_block(float* buffer, size_t num_frames, int channel_index) = 0;
};

// RTA Analyzer class that calculates 31-band, 63-band and FFT spectrum data from real PCM
class RtaAnalyzer {
public:
    static constexpr size_t FFT_SIZE = 4096;
    static constexpr size_t HOP_SIZE = 1024;
    static constexpr float MIN_DB = -72.0f;
    static constexpr float MAX_DB = 12.0f;
    static constexpr float DISPLAY_FLOOR_DB = -72.0f;

    RtaAnalyzer();
    ~RtaAnalyzer() = default;

    void set_mode(RtaMode mode) { mode_ = mode; }
    RtaMode get_mode() const { return mode_; }

    void set_source(RtaSource src) { source_ = src; }
    RtaSource get_source() const { return source_; }

    void set_speed(RtaSpeed spd) { speed_ = spd; }
    RtaSpeed get_speed() const { return speed_; }

    void set_freeze(bool freeze) { frozen_ = freeze; }
    bool is_frozen() const { return frozen_; }

    void set_channel_id(uint32_t ch_id) { channel_id_ = ch_id; }
    uint32_t get_channel_id() const { return channel_id_; }

    void clear_peaks();

    // Primary Production Interface: process real normalized float PCM blocks
    void process_pcm(const float* samples, size_t count, double sampleRate = 48000.0);

    // Decay spectrum when stream is stopped or idle
    void process_silence();

    // Diagnostics telemetry
    RtaDiagnostics get_diagnostics() const { return diagnostics_; }

    // Backward-compatible simulation wrapper (for tests/QA: feeds real PCM into process_pcm)
    void update_simulation(float input_level_db, float eq_offset_db, float phase_dt);
    void update_simulation(float input_level_db, float phase_dt, float (*eq_eval_fn)(float hz, void* user) = nullptr, void* user_data = nullptr);

    // Get a copy of the current RTA state for rendering
    RtaSnapshot get_snapshot() const;

    // Standard ISO 31-band center frequencies
    static const std::array<float, 31>& get_iso_frequencies_31();
    static const std::array<const char*, 31>& get_iso_labels_31();

private:
    void compute_fft_and_bands(double sampleRate);
    void init_tables();

    RtaMode mode_ = RtaMode::BANDS_31;
    RtaSource source_ = RtaSource::POST_EQ;
    RtaSpeed speed_ = RtaSpeed::MEDIUM;
    bool frozen_ = false;
    uint32_t channel_id_ = 0;

    std::array<float, 31> bands_31_{};
    std::array<float, 31> peak_31_{};
    std::array<float, 63> bands_63_{};
    std::array<float, 63> peak_63_{};
    std::array<float, 64> fft_bins_{};
    std::array<float, 64> fft_peak_{};

    // Realtime ring accumulator & work buffers
    std::vector<float> ring_accumulator_;
    size_t ring_write_pos_ = 0;
    size_t accumulated_since_hop_ = 0;
    bool ring_full_ = false;

    std::vector<float> work_pcm_;
    std::vector<float> work_real_;
    std::vector<float> work_imag_;
    std::vector<float> hann_window_;
    double window_sum_ = 0.0;

    RtaDiagnostics diagnostics_{};
    float sim_phase_ = 0.0f;
};
