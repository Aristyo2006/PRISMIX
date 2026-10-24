#include "audiocore/afs_node.hpp"
#include "audiocore/audio_buffer.hpp"
#include <algorithm>
#include <cmath>
#include <complex>

namespace audiocore {

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {
inline float sanitize(float x) noexcept {
    return (std::abs(x) < 1e-15f) ? 0.0f : x;
}

// In-place Radix-2 Cooley-Tukey FFT implementation
void computeFft(std::vector<float>& real, std::vector<float>& imag, size_t n) {
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
        double angle = -2.0 * M_PI / static_cast<double>(len);
        float wlen_real = static_cast<float>(std::cos(angle));
        float wlen_imag = static_cast<float>(std::sin(angle));

        for (size_t i = 0; i < n; i += len) {
            float w_real = 1.0f;
            float w_imag = 0.0f;
            for (size_t k = 0; k < len / 2; ++k) {
                float u_real = real[i + k];
                float u_imag = imag[i + k];
                float v_real = real[i + k + len / 2] * w_real - imag[i + k + len / 2] * w_imag;
                float v_imag = real[i + k + len / 2] * w_imag + imag[i + k + len / 2] * w_real;

                real[i + k] = u_real + v_real;
                imag[i + k] = u_imag + v_imag;
                real[i + k + len / 2] = u_real - v_real;
                imag[i + k + len / 2] = u_imag - v_imag;

                float next_w_real = w_real * wlen_real - w_imag * wlen_imag;
                float next_w_imag = w_real * wlen_imag + w_imag * wlen_real;
                w_real = next_w_real;
                w_imag = next_w_imag;
            }
        }
    }
}
} // namespace

void AfsNode::BiquadNotch::updateCoefficients(double sampleRate) noexcept {
    if (state == AfsNotchState::Inactive || currentDepthDb >= -0.05f) {
        // Flat unity passthrough
        b0 = 1.0f; b1 = 0.0f; b2 = 0.0f;
        a1 = 0.0f; a2 = 0.0f;
        return;
    }

    // Peaking EQ Notch Filter Design
    const double fs = sampleRate > 0.0 ? sampleRate : 48000.0;
    const double f0 = std::clamp(static_cast<double>(freqHz), 20.0, fs * 0.48);
    const double A = std::pow(10.0, static_cast<double>(currentDepthDb) / 40.0); // Cut gain
    const double w0 = 2.0 * M_PI * f0 / fs;
    const double alpha = std::sin(w0) / (2.0 * std::clamp(static_cast<double>(q), 1.0, 50.0));

    const double a0 = 1.0 + alpha / A;
    const double invA0 = 1.0 / a0;

    b0 = static_cast<float>((1.0 + alpha * A) * invA0);
    b1 = static_cast<float>((-2.0 * std::cos(w0)) * invA0);
    b2 = static_cast<float>((1.0 - alpha * A) * invA0);
    a1 = static_cast<float>((-2.0 * std::cos(w0)) * invA0);
    a2 = static_cast<float>((1.0 - alpha / A) * invA0);
}

AfsNode::AfsNode(double sampleRate)
    : sample_rate_(sampleRate > 0.0 ? sampleRate : 48000.0)
{
    analysis_fifo_.assign(FIFO_CAPACITY, 0.0f);
    fft_in_real_.assign(FFT_SIZE, 0.0f);
    fft_in_imag_.assign(FFT_SIZE, 0.0f);
    fft_window_.assign(FFT_SIZE, 0.0f);

    // Initialize Hann window
    for (size_t i = 0; i < FFT_SIZE; ++i) {
        fft_window_[i] = 0.5f * (1.0f - static_cast<float>(std::cos(2.0 * M_PI * i / (FFT_SIZE - 1))));
    }

    for (size_t i = 0; i < MAX_NOTCHES; ++i) {
        notches_[i].state = AfsNotchState::Inactive;
        notches_[i].resetState();
        notches_[i].updateCoefficients(sample_rate_);
    }
}

AfsNode::~AfsNode() {
    stopAsyncWorker();
}

void AfsNode::setSampleRate(double sampleRate) noexcept {
    if (sampleRate > 0.0 && sampleRate != sample_rate_) {
        sample_rate_ = sampleRate;
        std::lock_guard<std::mutex> lock(notch_mutex_);
        for (auto& notch : notches_) {
            notch.updateCoefficients(sample_rate_);
        }
    }
}

void AfsNode::setEnabled(bool enabled) noexcept {
    enabled_.store(enabled, std::memory_order_release);
    if (!enabled) {
        clearFilters();
    }
}

void AfsNode::setSensitivity(float sensitivity) noexcept {
    sensitivity_ = std::clamp(sensitivity, 0.0f, 1.0f);
}

void AfsNode::setMaxNotches(size_t maxNotches) noexcept {
    max_notches_ = std::clamp(maxNotches, size_t{1}, MAX_NOTCHES);
}

void AfsNode::setMaxDepthDb(float depthDb) noexcept {
    max_depth_db_ = std::clamp(depthDb, -24.0f, -6.0f);
}

void AfsNode::setQ(float q) noexcept {
    q_ = std::clamp(q, 5.0f, 40.0f);
}

void AfsNode::setAttackMs(float attackMs) noexcept {
    attack_ms_ = std::clamp(attackMs, 5.0f, 200.0f);
}

void AfsNode::setHoldMs(float holdMs) noexcept {
    hold_ms_ = std::clamp(holdMs, 100.0f, 10000.0f);
}

void AfsNode::setReleaseMs(float releaseMs) noexcept {
    release_ms_ = std::clamp(releaseMs, 100.0f, 5000.0f);
}

void AfsNode::clearFilters() noexcept {
    std::lock_guard<std::mutex> lock(notch_mutex_);
    for (auto& notch : notches_) {
        notch.state = AfsNotchState::Inactive;
        notch.targetDepthDb = 0.0f;
        notch.currentDepthDb = 0.0f;
        notch.resetState();
        notch.updateCoefficients(sample_rate_);
    }
    candidates_.clear();
}

size_t AfsNode::getActiveNotchCount() const noexcept {
    std::lock_guard<std::mutex> lock(notch_mutex_);
    size_t count = 0;
    for (const auto& notch : notches_) {
        if (notch.state != AfsNotchState::Inactive) {
            count++;
        }
    }
    return count;
}

std::vector<AfsNotchInfo> AfsNode::getNotchSnapshots() const {
    std::lock_guard<std::mutex> lock(notch_mutex_);
    std::vector<AfsNotchInfo> result;
    for (size_t i = 0; i < max_notches_; ++i) {
        const auto& n = notches_[i];
        if (n.state != AfsNotchState::Inactive) {
            AfsNotchInfo info;
            info.id = i + 1;
            info.frequencyHz = n.freqHz;
            info.depthDb = n.currentDepthDb;
            info.q = n.q;
            info.state = n.state;
            info.ageBlocks = n.ageBlocks;
            result.push_back(info);
        }
    }
    return result;
}

const char* AfsNode::getStatusString() const noexcept {
    if (!enabled_.load(std::memory_order_relaxed)) return "DISABLED";
    if (bypassed_.load(std::memory_order_relaxed)) return "BYPASS";

    size_t activeCount = 0;
    for (const auto& n : notches_) {
        if (n.state == AfsNotchState::Active || n.state == AfsNotchState::Holding) activeCount++;
    }
    if (activeCount > 0) return "ACTIVE";
    if (!candidates_.empty()) return "CANDIDATE";
    return "MONITORING";
}

void AfsNode::reset() {
    fifo_write_pos_.store(0, std::memory_order_relaxed);
    fifo_read_pos_.store(0, std::memory_order_relaxed);
    std::fill(analysis_fifo_.begin(), analysis_fifo_.end(), 0.0f);
    clearFilters();
}

void AfsNode::startAsyncWorker() {
    if (analyzer_running_.load(std::memory_order_relaxed)) return;
    analyzer_running_.store(true, std::memory_order_release);
    analyzer_thread_ = std::thread([this]() {
        while (analyzer_running_.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            if (!analyzer_running_.load(std::memory_order_relaxed)) break;
            size_t available = (fifo_write_pos_.load(std::memory_order_acquire) + FIFO_CAPACITY -
                                fifo_read_pos_.load(std::memory_order_relaxed)) % FIFO_CAPACITY;
            if (available >= FFT_SIZE) {
                stepAnalysis();
            }
        }
    });
}

void AfsNode::stopAsyncWorker() {
    if (!analyzer_running_.load(std::memory_order_relaxed)) return;
    analyzer_running_.store(false, std::memory_order_release);
    if (analyzer_thread_.joinable()) {
        analyzer_thread_.join();
    }
}

bool AfsNode::hasPendingAnalysisWindow() const noexcept {
    const size_t available = (fifo_write_pos_.load(std::memory_order_acquire) + FIFO_CAPACITY -
                              fifo_read_pos_.load(std::memory_order_relaxed)) % FIFO_CAPACITY;
    return available >= FFT_SIZE;
}

void AfsNode::stepAnalysis() {
    std::lock_guard<std::mutex> lock(analysis_mutex_);
    size_t rPos = fifo_read_pos_.load(std::memory_order_relaxed);
    // Copy FFT_SIZE samples from circular FIFO
    for (size_t i = 0; i < FFT_SIZE; ++i) {
        fft_in_real_[i] = analysis_fifo_[(rPos + i) % FIFO_CAPACITY];
    }
    // Advance read pointer by hop size (FFT_SIZE / 2 = 1024 samples for 50% overlap)
    fifo_read_pos_.store((rPos + 1024) % FIFO_CAPACITY, std::memory_order_release);

    runSpectralAnalysis(fft_in_real_.data());
}

void AfsNode::runSpectralAnalysis(const float* timeSamples) {
    // Window input
    for (size_t i = 0; i < FFT_SIZE; ++i) {
        fft_in_real_[i] = timeSamples[i] * fft_window_[i];
        fft_in_imag_[i] = 0.0f;
    }

    computeFft(fft_in_real_, fft_in_imag_, FFT_SIZE);

    // Compute power spectrum (dBFS)
    constexpr size_t numBins = FFT_SIZE / 2;
    std::vector<float> magDb(numBins, -120.0f);
    for (size_t k = 1; k < numBins; ++k) {
        float p = (fft_in_real_[k] * fft_in_real_[k] + fft_in_imag_[k] * fft_in_imag_[k]) /
                  static_cast<float>(FFT_SIZE * FFT_SIZE);
        magDb[k] = 10.0f * std::log10(std::max(p, 1e-12f));
    }

    // Detection threshold mapped from sensitivity (-50 dBFS to -15 dBFS)
    const float thresholdDb = -50.0f + (1.0f - sensitivity_) * 35.0f;
    const double binFreq = sample_rate_ / static_cast<double>(FFT_SIZE);

    std::vector<float> detectedPeaks;

    // Scan for sharp, prominent narrowband peaks (between 50 Hz and 18 kHz)
    const size_t minScanBin = std::max(size_t{18}, static_cast<size_t>(std::ceil(50.0 / binFreq)));
    const size_t maxScanBin = std::min(numBins > 18 ? numBins - 18 : size_t{0}, static_cast<size_t>(std::floor(18000.0 / binFreq)));

    for (size_t k = minScanBin; k < maxScanBin; ++k) {
        float m = magDb[k];
        if (m < thresholdDb) continue;

        // Must be a local maximum
        if (m <= magDb[k - 1] || m <= magDb[k + 1]) continue;

        // Narrowband Prominence Check: compare with local floor across neighbors (+/- 6 to +/- 16 bins)
        float floorSum = 0.0f;
        int floorCount = 0;
        for (int offset = -16; offset <= 16; ++offset) {
            if (std::abs(offset) >= 6) {
                int idx = static_cast<int>(k) + offset;
                if (idx >= 0 && idx < static_cast<int>(numBins)) {
                    floorSum += magDb[static_cast<size_t>(idx)];
                    floorCount++;
                }
            }
        }
        if (floorCount == 0) continue;
        float localFloor = floorSum / static_cast<float>(floorCount);
        float prominence = m - localFloor;

        // Narrowband sinusoids have prominence >= 12 dB over surrounding floor
        if (prominence >= 12.0f) {
            // Parabolic interpolation for sub-bin frequency accuracy
            float alpha = magDb[k - 1];
            float beta = magDb[k];
            float gamma = magDb[k + 1];
            float denom = alpha - 2.0f * beta + gamma;
            float delta = (std::abs(denom) > 1e-6f) ? 0.5f * (alpha - gamma) / denom : 0.0f;

            float exactFreq = static_cast<float>((static_cast<double>(k) + delta) * binFreq);
            detectedPeaks.push_back(exactFreq);
        }
    }

    std::lock_guard<std::mutex> lock(notch_mutex_);

    // 1. Update Candidate Tracks with temporal persistence
    std::vector<CandidateTrack> nextCandidates;
    for (float peakFreq : detectedPeaks) {
        bool matched = false;
        for (auto& cand : candidates_) {
            // Frequency match within 2.0%
            if (std::abs(cand.freqHz - peakFreq) / cand.freqHz < 0.02f) {
                cand.persistenceCount++;
                cand.freqHz = 0.5f * (cand.freqHz + peakFreq);
                nextCandidates.push_back(cand);
                matched = true;
                break;
            }
        }
        if (!matched) {
            CandidateTrack newCand;
            newCand.freqHz = peakFreq;
            newCand.persistenceCount = 1;
            nextCandidates.push_back(newCand);
        }
    }
    candidates_ = std::move(nextCandidates);

    // 2. Deploy or update notches for confirmed feedback (persistenceCount >= 3)
    for (const auto& cand : candidates_) {
        if (cand.persistenceCount >= 3) {
            // Check if notch already active for this frequency
            bool exists = false;
            for (size_t i = 0; i < max_notches_; ++i) {
                if (notches_[i].state != AfsNotchState::Inactive &&
                    std::abs(notches_[i].freqHz - cand.freqHz) / notches_[i].freqHz < 0.03f) {
                    exists = true;
                    // Retrigger hold duration
                    notches_[i].holdCounter = static_cast<uint32_t>((hold_ms_ * 0.001f * sample_rate_) / 128.0f);
                    if (notches_[i].state != AfsNotchState::Active) {
                        notches_[i].state = AfsNotchState::Active;
                        notches_[i].targetDepthDb = max_depth_db_;
                    }
                    break;
                }
            }

            if (!exists) {
                // Find first inactive or oldest releasing notch
                int allocateIdx = -1;
                for (size_t i = 0; i < max_notches_; ++i) {
                    if (notches_[i].state == AfsNotchState::Inactive) {
                        allocateIdx = static_cast<int>(i);
                        break;
                    }
                }

                if (allocateIdx >= 0) {
                    auto& n = notches_[allocateIdx];
                    n.freqHz = cand.freqHz;
                    n.q = q_;
                    n.targetDepthDb = max_depth_db_;
                    n.currentDepthDb = 0.0f;
                    n.state = AfsNotchState::Active;
                    n.holdCounter = static_cast<uint32_t>((hold_ms_ * 0.001f * sample_rate_) / 128.0f);
                    n.ageBlocks = 0;
                    n.resetState();
                    n.updateCoefficients(sample_rate_);
                }
            }
        }
    }
}

void AfsNode::process(
    const AudioBuffer& input,
    AudioBuffer& output,
    uint32_t frames)
{
    if (frames == 0) return;

    if (bypassed_.load(std::memory_order_relaxed) || !enabled_.load(std::memory_order_relaxed)) {
        if (&input != &output) {
            output.copyFrom(input, frames);
        }
        return;
    }

    const uint32_t inChannels = input.getChannelCount();
    const uint32_t outChannels = output.getChannelCount();
    const float* inL = input.getChannelData(0);
    const float* inR = (inChannels > 1) ? input.getChannelData(1) : inL;
    float* outL = output.getChannelData(0);
    float* outR = (outChannels > 1) ? output.getChannelData(1) : outL;

    // 1. Lock-free enqueue of incoming frames into analysis FIFO
    size_t wPos = fifo_write_pos_.load(std::memory_order_relaxed);
    for (uint32_t i = 0; i < frames; ++i) {
        analysis_fifo_[wPos] = 0.5f * (sanitize(inL[i]) + sanitize(inR[i]));
        wPos = (wPos + 1 < FIFO_CAPACITY) ? (wPos + 1) : 0;
    }
    fifo_write_pos_.store(wPos, std::memory_order_release);

    // Copy input to output as processing start
    for (uint32_t i = 0; i < frames; ++i) {
        outL[i] = sanitize(inL[i]);
        if (outChannels > 1) outR[i] = sanitize(inR[i]);
    }

    // 2. Cascade through active biquad notch filters (Direct Form II Transposed)
    // Slew rate constants per block
    const float attackSlew = (max_depth_db_ / (attack_ms_ * 0.001f * static_cast<float>(sample_rate_))) * static_cast<float>(frames);
    const float releaseSlew = (-max_depth_db_ / (release_ms_ * 0.001f * static_cast<float>(sample_rate_))) * static_cast<float>(frames);

    for (size_t n = 0; n < max_notches_; ++n) {
        auto& notch = notches_[n];
        if (notch.state == AfsNotchState::Inactive) continue;

        // Smooth notch depth ramping and dynamic parameter updates
        if (notch.state == AfsNotchState::Active || notch.state == AfsNotchState::Holding) {
            bool coeffDirty = false;
            if (std::abs(notch.q - q_) > 1e-3f) {
                notch.q = q_;
                coeffDirty = true;
            }
            if (std::abs(notch.targetDepthDb - max_depth_db_) > 1e-3f) {
                notch.targetDepthDb = max_depth_db_;
                coeffDirty = true;
            }
            if (notch.currentDepthDb > notch.targetDepthDb) {
                notch.currentDepthDb = std::max(notch.targetDepthDb, notch.currentDepthDb + attackSlew);
                coeffDirty = true;
            } else if (notch.currentDepthDb < notch.targetDepthDb) {
                notch.currentDepthDb = std::min(notch.targetDepthDb, notch.currentDepthDb - attackSlew);
                coeffDirty = true;
            }
            if (coeffDirty) {
                notch.updateCoefficients(sample_rate_);
            }
        }

        if (notch.state == AfsNotchState::Active) {
            if (notch.holdCounter > 0) {
                notch.holdCounter--;
            } else {
                notch.state = AfsNotchState::Holding;
                notch.holdCounter = static_cast<uint32_t>((hold_ms_ * 0.001f * sample_rate_) / frames);
            }
        } else if (notch.state == AfsNotchState::Holding) {
            if (notch.holdCounter > 0) {
                notch.holdCounter--;
            } else {
                notch.state = AfsNotchState::Releasing;
                notch.targetDepthDb = 0.0f;
            }
        } else if (notch.state == AfsNotchState::Releasing) {
            if (notch.currentDepthDb < 0.0f) {
                notch.currentDepthDb = std::min(0.0f, notch.currentDepthDb + releaseSlew);
                notch.updateCoefficients(sample_rate_);
            }
            if (notch.currentDepthDb >= -0.05f) {
                notch.state = AfsNotchState::Inactive;
                notch.currentDepthDb = 0.0f;
                notch.updateCoefficients(sample_rate_);
            }
        }

        notch.ageBlocks++;

        // Process block through this biquad notch filter (DF2T)
        const float b0 = notch.b0;
        const float b1 = notch.b1;
        const float b2 = notch.b2;
        const float a1 = notch.a1;
        const float a2 = notch.a2;

        float s1_l = notch.s1_l;
        float s2_l = notch.s2_l;
        float s1_r = notch.s1_r;
        float s2_r = notch.s2_r;

        for (uint32_t i = 0; i < frames; ++i) {
            float xL = outL[i];
            float yL = b0 * xL + s1_l;
            s1_l = b1 * xL - a1 * yL + s2_l;
            s2_l = b2 * xL - a2 * yL;
            outL[i] = sanitize(yL);

            if (outChannels > 1) {
                float xR = outR[i];
                float yR = b0 * xR + s1_r;
                s1_r = b1 * xR - a1 * yR + s2_r;
                s2_r = b2 * xR - a2 * yR;
                outR[i] = sanitize(yR);
            }
        }

        notch.s1_l = sanitize(s1_l);
        notch.s2_l = sanitize(s2_l);
        notch.s1_r = sanitize(s1_r);
        notch.s2_r = sanitize(s2_r);
    }
}

} // namespace audiocore
