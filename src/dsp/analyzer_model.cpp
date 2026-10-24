#include "analyzer_model.h"
#include <cmath>
#include <algorithm>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

const std::array<float, 31> kIso31Freqs = {
    20.0f, 25.0f, 31.5f, 40.0f, 50.0f, 63.0f, 80.0f, 100.0f, 125.0f, 160.0f,
    200.0f, 250.0f, 315.0f, 400.0f, 500.0f, 630.0f, 800.0f, 1000.0f, 1250.0f, 1600.0f,
    2000.0f, 2500.0f, 3150.0f, 4000.0f, 5000.0f, 6300.0f, 8000.0f, 10000.0f, 12500.0f, 16000.0f,
    20000.0f
};

const std::array<const char*, 31> kIso31Labels = {
    "20", "25", "31", "40", "50", "63", "80", "100", "125", "160",
    "200", "250", "315", "400", "500", "630", "800", "1k", "1.2k", "1.6k",
    "2k", "2.5k", "3.1k", "4k", "5k", "6.3k", "8k", "10k", "12.5k", "16k",
    "20k"
};

// In-place Radix-2 Cooley-Tukey FFT with exact double-precision angle calculations
void computeRadix2Fft(std::vector<float>& real, std::vector<float>& imag, size_t n) {
    // Bit reversal permutation
    size_t j = 0;
    for (size_t i = 0; i < n - 1; ++i) {
        if (i < j) {
            std::swap(real[i], real[j]);
            std::swap(imag[i], imag[j]);
        }
        size_t k = n >> 1;
        while (k > 0 && k <= j) {
            j -= k;
            k >>= 1;
        }
        j += k;
    }

    // Butterfly stages
    for (size_t len = 2; len <= n; len <<= 1) {
        const double angle = -2.0 * M_PI / static_cast<double>(len);
        for (size_t i = 0; i < n; i += len) {
            for (size_t k = 0; k < len / 2; ++k) {
                const double th = angle * static_cast<double>(k);
                const float w_real = static_cast<float>(std::cos(th));
                const float w_imag = static_cast<float>(std::sin(th));

                const float u_real = real[i + k];
                const float u_imag = imag[i + k];
                const float v_real = real[i + k + len / 2] * w_real - imag[i + k + len / 2] * w_imag;
                const float v_imag = real[i + k + len / 2] * w_imag + imag[i + k + len / 2] * w_real;

                real[i + k] = u_real + v_real;
                imag[i + k] = u_imag + v_imag;
                real[i + k + len / 2] = u_real - v_real;
                imag[i + k + len / 2] = u_imag - v_imag;
            }
        }
    }
}

} // namespace

const std::array<float, 31>& RtaAnalyzer::get_iso_frequencies_31() {
    return kIso31Freqs;
}

const std::array<const char*, 31>& RtaAnalyzer::get_iso_labels_31() {
    return kIso31Labels;
}

RtaAnalyzer::RtaAnalyzer() {
    init_tables();
    clear_peaks();
}

void RtaAnalyzer::init_tables() {
    ring_accumulator_.assign(FFT_SIZE, 0.0f);
    work_pcm_.assign(FFT_SIZE, 0.0f);
    work_real_.assign(FFT_SIZE, 0.0f);
    work_imag_.assign(FFT_SIZE, 0.0f);
    hann_window_.assign(FFT_SIZE, 0.0f);

    window_sum_ = 0.0;
    for (size_t i = 0; i < FFT_SIZE; ++i) {
        hann_window_[i] = 0.5f * (1.0f - static_cast<float>(std::cos(2.0 * M_PI * i / (FFT_SIZE - 1))));
        window_sum_ += hann_window_[i];
    }

    diagnostics_.sampleRate = 48000.0;
    diagnostics_.fftSize = FFT_SIZE;
    diagnostics_.binSpacing = static_cast<float>(48000.0 / FFT_SIZE);
    diagnostics_.hopSize = HOP_SIZE;
    diagnostics_.windowType = "Hann";
    diagnostics_.displayFloorDbfs = DISPLAY_FLOOR_DB;
}

void RtaAnalyzer::clear_peaks() {
    bands_31_.fill(DISPLAY_FLOOR_DB);
    peak_31_.fill(DISPLAY_FLOOR_DB);
    bands_63_.fill(DISPLAY_FLOOR_DB);
    peak_63_.fill(DISPLAY_FLOOR_DB);
    fft_bins_.fill(DISPLAY_FLOOR_DB);
    fft_peak_.fill(DISPLAY_FLOOR_DB);
    std::fill(ring_accumulator_.begin(), ring_accumulator_.end(), 0.0f);
    ring_write_pos_ = 0;
    accumulated_since_hop_ = 0;
    ring_full_ = false;
    diagnostics_.peakFrequencyHz = 0.0f;
    diagnostics_.peakAmplitudeDbfs = DISPLAY_FLOOR_DB;
}

void RtaAnalyzer::process_pcm(const float* samples, size_t count, double sampleRate) {
    if (!samples || count == 0) return;
    if (sampleRate > 0.0) {
        diagnostics_.sampleRate = sampleRate;
        diagnostics_.binSpacing = static_cast<float>(sampleRate / FFT_SIZE);
    }
    diagnostics_.sourceChannel = channel_id_;
    diagnostics_.tapPoint = source_;

    // Accumulate incoming planar float samples into ring buffer
    for (size_t i = 0; i < count; ++i) {
        ring_accumulator_[ring_write_pos_] = samples[i];
        ring_write_pos_ = (ring_write_pos_ + 1) % FFT_SIZE;
        if (ring_write_pos_ == 0) ring_full_ = true;
    }
    accumulated_since_hop_ += count;

    // Run FFT calculation when at least HOP_SIZE samples accumulated (or initially full)
    if (accumulated_since_hop_ >= HOP_SIZE || (count >= FFT_SIZE && !ring_full_)) {
        accumulated_since_hop_ = 0;
        compute_fft_and_bands(sampleRate > 0.0 ? sampleRate : 48000.0);
    }
}

void RtaAnalyzer::process_silence() {
    if (frozen_) return;

    float decay = 0.30f;
    if (speed_ == RtaSpeed::FAST) decay = 0.60f;
    else if (speed_ == RtaSpeed::SLOW) decay = 0.12f;

    for (size_t i = 0; i < 31; ++i) {
        bands_31_[i] = std::max(DISPLAY_FLOOR_DB, bands_31_[i] - decay);
        peak_31_[i] = std::max(DISPLAY_FLOOR_DB, peak_31_[i] - decay * 0.8f);
    }
    for (size_t i = 0; i < 63; ++i) {
        bands_63_[i] = std::max(DISPLAY_FLOOR_DB, bands_63_[i] - decay);
        peak_63_[i] = std::max(DISPLAY_FLOOR_DB, peak_63_[i] - decay * 0.8f);
    }
    for (size_t i = 0; i < 64; ++i) {
        fft_bins_[i] = std::max(DISPLAY_FLOOR_DB, fft_bins_[i] - decay);
        fft_peak_[i] = std::max(DISPLAY_FLOOR_DB, fft_peak_[i] - decay * 0.8f);
    }
    diagnostics_.hasValidPcm = false;
    diagnostics_.peakFrequencyHz = 0.0f;
    diagnostics_.peakAmplitudeDbfs = DISPLAY_FLOOR_DB;
}

void RtaAnalyzer::compute_fft_and_bands(double sampleRate) {
    if (frozen_) return;

    // Extract contiguous window of the latest FFT_SIZE frames from ring accumulator
    const size_t start = ring_write_pos_; // Oldest frame in circular buffer
    for (size_t i = 0; i < FFT_SIZE; ++i) {
        work_pcm_[i] = ring_accumulator_[(start + i) % FFT_SIZE];
    }

    // 1. PCM Diagnostics Verification before FFT
    double sum = 0.0;
    double sumSq = 0.0;
    float peak = 0.0f;
    uint32_t finiteCount = 0;

    for (size_t i = 0; i < FFT_SIZE; ++i) {
        const float val = work_pcm_[i];
        if (std::isfinite(val)) {
            finiteCount++;
            sum += val;
            sumSq += val * val;
            peak = std::max(peak, std::abs(val));
        }
    }

    diagnostics_.finiteSampleCount = finiteCount;
    diagnostics_.dcMean = static_cast<float>(sum / FFT_SIZE);
    diagnostics_.pcmPeak = peak;
    diagnostics_.pcmRms = static_cast<float>(std::sqrt(sumSq / FFT_SIZE));
    diagnostics_.sequenceNumber++;
    diagnostics_.hasValidPcm = (finiteCount == FFT_SIZE);

    // Silence detection: decay cleanly without executing FFT
    if (peak < 1e-6f || finiteCount < FFT_SIZE) {
        process_silence();
        return;
    }

    // 2. Apply Hann Window exactly once
    for (size_t i = 0; i < FFT_SIZE; ++i) {
        work_real_[i] = work_pcm_[i] * hann_window_[i];
        work_imag_[i] = 0.0f;
    }

    // 3. Compute Radix-2 Forward FFT
    computeRadix2Fft(work_real_, work_imag_, FFT_SIZE);

    // 4. Coherent Amplitude Spectrum Calculation (single-sided)
    // A[k] = 2 * |X[k]| / sum(w[n]) for interior positive frequencies
    const float invWinSum = static_cast<float>(1.0 / (window_sum_ > 0.0 ? window_sum_ : 1.0));
    std::vector<float> A(FFT_SIZE / 2 + 1);
    A[0] = std::abs(work_real_[0]) * invWinSum; // DC bin
    for (size_t k = 1; k < FFT_SIZE / 2; ++k) {
        const float mag = std::sqrt(work_real_[k] * work_real_[k] + work_imag_[k] * work_imag_[k]);
        A[k] = 2.0f * mag * invWinSum;
    }
    A[FFT_SIZE / 2] = std::abs(work_real_[FFT_SIZE / 2]) * invWinSum; // Nyquist

    // 5. Parabolic Peak Interpolation
    size_t peakBin = 1;
    float maxA = A[1];
    for (size_t k = 2; k < FFT_SIZE / 2; ++k) {
        if (A[k] > maxA) {
            maxA = A[k];
            peakBin = k;
        }
    }

    const float binSpacing = static_cast<float>(sampleRate / FFT_SIZE);
    if (maxA > 1e-4f) {
        const float alpha = 20.0f * std::log10(std::max(A[peakBin - 1], 1e-6f));
        const float beta  = 20.0f * std::log10(std::max(A[peakBin], 1e-6f));
        const float gamma = 20.0f * std::log10(std::max(A[peakBin + 1], 1e-6f));
        const float denom = alpha - 2.0f * beta + gamma;
        float delta = (std::abs(denom) > 1e-12f) ? 0.5f * (alpha - gamma) / denom : 0.0f;
        delta = std::clamp(delta, -0.7f, 0.7f);

        diagnostics_.peakFrequencyHz = (peakBin + delta) * binSpacing;
        diagnostics_.peakAmplitudeDbfs = beta - 0.25f * (alpha - gamma) * delta;
    } else {
        diagnostics_.peakFrequencyHz = 0.0f;
        diagnostics_.peakAmplitudeDbfs = DISPLAY_FLOOR_DB;
    }

    // 6. Smoothing time constants
    float alphaSmooth = 0.35f;
    float peakDecay = 0.30f;
    if (speed_ == RtaSpeed::FAST) {
        alphaSmooth = 0.65f;
        peakDecay = 0.60f;
    } else if (speed_ == RtaSpeed::SLOW) {
        alphaSmooth = 0.15f;
        peakDecay = 0.12f;
    }

    const float eps = 2.511886e-4f; // -72 dBFS linear floor

    // 7. Aggregate ISO 31-Band (1/3-octave power aggregation)
    for (size_t b = 0; b < 31; ++b) {
        const float fc = kIso31Freqs[b];
        const float flo = fc * std::pow(2.0f, -1.0f / 6.0f);
        const float fhi = fc * std::pow(2.0f, 1.0f / 6.0f);

        double pwrSum = 0.0;
        int count = 0;
        for (size_t k = 1; k < FFT_SIZE / 2; ++k) {
            const float f = k * binSpacing;
            if (f >= flo && f < fhi) {
                pwrSum += A[k] * A[k];
                count++;
            }
        }

        float bandAmp = 0.0f;
        if (count > 0) {
            bandAmp = static_cast<float>(std::sqrt(pwrSum));
        } else {
            const float kf = fc / binSpacing;
            const size_t k0 = std::clamp(static_cast<size_t>(kf), size_t(1), FFT_SIZE / 2 - 2);
            const float frac = kf - k0;
            bandAmp = A[k0] * (1.0f - frac) + A[k0 + 1] * frac;
        }

        const float targetDb = std::clamp(20.0f * std::log10(std::max(bandAmp, eps)), MIN_DB, MAX_DB);
        if (targetDb > bands_31_[b]) {
            bands_31_[b] += (targetDb - bands_31_[b]) * alphaSmooth;
        } else {
            bands_31_[b] += (targetDb - bands_31_[b]) * (alphaSmooth * 0.6f);
        }

        if (bands_31_[b] > peak_31_[b]) {
            peak_31_[b] = bands_31_[b];
        } else {
            peak_31_[b] = std::max(MIN_DB, peak_31_[b] - peakDecay);
        }
    }

    // 8. Aggregate 63-Band (1/6-octave logarithmic aggregation)
    for (size_t b = 0; b < 63; ++b) {
        const float fc = 20.0f * std::pow(1000.0f, static_cast<float>(b) / 62.0f);
        const float flo = fc * std::pow(1000.0f, -0.5f / 62.0f);
        const float fhi = fc * std::pow(1000.0f, +0.5f / 62.0f);

        double pwrSum = 0.0;
        int count = 0;
        for (size_t k = 1; k < FFT_SIZE / 2; ++k) {
            const float f = k * binSpacing;
            if (f >= flo && f < fhi) {
                pwrSum += A[k] * A[k];
                count++;
            }
        }

        float bandAmp = 0.0f;
        if (count > 0) {
            bandAmp = static_cast<float>(std::sqrt(pwrSum));
        } else {
            const float kf = fc / binSpacing;
            const size_t k0 = std::clamp(static_cast<size_t>(kf), size_t(1), FFT_SIZE / 2 - 2);
            const float frac = kf - k0;
            bandAmp = A[k0] * (1.0f - frac) + A[k0 + 1] * frac;
        }

        const float targetDb = std::clamp(20.0f * std::log10(std::max(bandAmp, eps)), MIN_DB, MAX_DB);
        if (targetDb > bands_63_[b]) {
            bands_63_[b] += (targetDb - bands_63_[b]) * alphaSmooth;
        } else {
            bands_63_[b] += (targetDb - bands_63_[b]) * (alphaSmooth * 0.6f);
        }

        if (bands_63_[b] > peak_63_[b]) {
            peak_63_[b] = bands_63_[b];
        } else {
            peak_63_[b] = std::max(MIN_DB, peak_63_[b] - peakDecay);
        }
    }

    // 9. Aggregate 64-FFT points (peak across band around center frequency)
    for (size_t b = 0; b < 64; ++b) {
        const float fc = 20.0f * std::pow(1000.0f, static_cast<float>(b) / 63.0f);
        const float flo = fc * std::pow(1000.0f, -0.5f / 63.0f);
        const float fhi = fc * std::pow(1000.0f, +0.5f / 63.0f);

        float maxBin = 0.0f;
        int count = 0;
        for (size_t k = 1; k < FFT_SIZE / 2; ++k) {
            const float f = k * binSpacing;
            if (f >= flo && f < fhi) {
                maxBin = std::max(maxBin, A[k]);
                count++;
            }
        }

        if (count == 0) {
            const float kf = fc / binSpacing;
            const size_t k0 = std::clamp(static_cast<size_t>(kf), size_t(1), FFT_SIZE / 2 - 2);
            const float frac = kf - k0;
            maxBin = A[k0] * (1.0f - frac) + A[k0 + 1] * frac;
        }

        const float targetDb = std::clamp(20.0f * std::log10(std::max(maxBin, eps)), MIN_DB, MAX_DB);
        if (targetDb > fft_bins_[b]) {
            fft_bins_[b] += (targetDb - fft_bins_[b]) * alphaSmooth;
        } else {
            fft_bins_[b] += (targetDb - fft_bins_[b]) * (alphaSmooth * 0.6f);
        }

        if (fft_bins_[b] > fft_peak_[b]) {
            fft_peak_[b] = fft_bins_[b];
        } else {
            fft_peak_[b] = std::max(MIN_DB, fft_peak_[b] - peakDecay);
        }
    }
}

void RtaAnalyzer::update_simulation(float input_level_db, float eq_offset_db, float phase_dt) {
    auto dummy_eq = [](float, void* u) -> float {
        return *static_cast<float*>(u);
    };
    update_simulation(input_level_db, phase_dt, dummy_eq, &eq_offset_db);
}

void RtaAnalyzer::update_simulation(float input_level_db, float phase_dt,
                                    float (*eq_eval_fn)(float hz, void* user), void* user_data) {
    if (frozen_) return;
    sim_phase_ += phase_dt;

    if (input_level_db <= -70.0f) {
        process_silence();
        return;
    }

    // Synthesize real PCM test block (1024 frames) representing actual signal
    const float linearAmp = std::pow(10.0f, std::clamp(input_level_db, -70.0f, 6.0f) / 20.0f);
    std::vector<float> pcm(1024);
    constexpr double fs = 48000.0;

    // Multitone with frequencies at 100 Hz, 1 kHz, 5 kHz shaped by real EQ response
    for (size_t n = 0; n < 1024; ++n) {
        const double t = static_cast<double>(n) / fs;
        float s = 0.0f;

        const float freqs[3] = {100.0f, 1000.0f, 5000.0f};
        const float amps[3] = {0.35f, 0.50f, 0.25f};

        for (int m = 0; m < 3; ++m) {
            float g = 1.0f;
            if (source_ == RtaSource::POST_EQ && eq_eval_fn) {
                const float eqDb = eq_eval_fn(freqs[m], user_data);
                g = std::pow(10.0f, eqDb / 20.0f);
            }
            s += amps[m] * g * static_cast<float>(std::sin(2.0 * M_PI * freqs[m] * t + sim_phase_ * (m + 1)));
        }
        pcm[n] = s * linearAmp;
    }

    process_pcm(pcm.data(), pcm.size(), fs);
}

RtaSnapshot RtaAnalyzer::get_snapshot() const {
    RtaSnapshot s;
    s.mode = mode_;
    s.source = source_;
    s.frozen = frozen_;
    s.bands_31 = bands_31_;
    s.peak_hold_31 = peak_31_;
    s.bands_63 = bands_63_;
    s.peak_hold_63 = peak_63_;
    s.fft_bins = fft_bins_;
    s.fft_peak_hold = fft_peak_;
    s.diagnostics = diagnostics_;
    return s;
}
