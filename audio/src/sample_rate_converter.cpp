#include "audiocore/sample_rate_converter.hpp"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <stdexcept>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace audiocore {

SampleRateConverter::SampleRateConverter() {
    reset();
}

bool SampleRateConverter::configure(
    double inSampleRate,
    double outSampleRate,
    uint32_t channels,
    SrcQuality quality)
{
    if (inSampleRate <= 0.0 || outSampleRate <= 0.0 || channels == 0) {
        status_ = SrcStatus::Unsupported;
        return false;
    }

    inSampleRate_ = inSampleRate;
    outSampleRate_ = outSampleRate;
    channels_ = channels;
    quality_ = quality;
    ratio_ = outSampleRate_ / inSampleRate_;
    invRatio_ = inSampleRate_ / outSampleRate_;

    // Check for identical sample rates (Bypass)
    if (std::abs(inSampleRate_ - outSampleRate_) < 1e-4 && !clockAdaptationEnabled_) {
        status_ = SrcStatus::Bypass;
        polyphaseTable_.clear();
        historyBuffers_.assign(channels_, std::vector<float>(64, 0.0f));
        reset();
        return true;
    }

    status_ = SrcStatus::Active;

    // Determine base filter half-width based on quality
    int32_t baseM = 16;
    switch (quality_) {
        case SrcQuality::Linear:
            baseM = 1;
            break;
        case SrcQuality::CubicHermite:
            baseM = 2;
            break;
        case SrcQuality::SincStandard:
            baseM = 16;
            break;
        case SrcQuality::SincHigh:
            baseM = 32;
            break;
    }

    // When downsampling, widen filter in input domain to suppress aliasing below output Nyquist
    if (ratio_ < 1.0) {
        int32_t downsampledM = static_cast<int32_t>(std::ceil(baseM / ratio_));
        filterHalfWidth_ = std::clamp(downsampledM, baseM, 64);
    } else {
        filterHalfWidth_ = baseM;
    }

    filterTaps_ = filterHalfWidth_ * 2;
    numPhases_ = 512;

    buildPolyphaseTable();

    // Prepare streaming history buffers
    size_t histCap = static_cast<size_t>(filterTaps_ * 2);
    historyBuffers_.assign(channels_, std::vector<float>(histCap, 0.0f));
    reset();

    return true;
}

bool SampleRateConverter::setStreamingOutputRate(double outSampleRate) noexcept {
    if (!clockAdaptationEnabled_ || outSampleRate <= 0.0 || inSampleRate_ <= 0.0 ||
        status_ == SrcStatus::Unconfigured || status_ == SrcStatus::Unsupported) {
        return false;
    }
    outSampleRate_ = outSampleRate;
    ratio_ = outSampleRate_ / inSampleRate_;
    invRatio_ = 1.0 / ratio_;
    status_ = SrcStatus::Active;
    return true;
}

void SampleRateConverter::reset() {
    fractionalPhase_ = 0.0;
    historyAvailable_ = 0;
    for (auto& buf : historyBuffers_) {
        std::fill(buf.begin(), buf.end(), 0.0f);
    }
}

void SampleRateConverter::buildPolyphaseTable() {
    polyphaseTable_.resize(numPhases_ * filterTaps_);

    // Anti-aliasing cutoff factor
    // For upsampling (ratio >= 1.0), Nyquist is at 0.5 of input rate (use 0.475 for tight transition).
    // For downsampling (ratio < 1.0), Nyquist is at 0.5 * ratio of input rate.
    double cutoff = (ratio_ < 1.0) ? (0.475 * ratio_) : 0.475;

    // Blackman-Harris 4-term window coefficients
    constexpr double a0 = 0.35875;
    constexpr double a1 = 0.48829;
    constexpr double a2 = 0.14128;
    constexpr double a3 = 0.01168;

    const double halfW = static_cast<double>(filterHalfWidth_);
    const double twoHalfW = 2.0 * halfW;

    for (uint32_t p = 0; p < numPhases_; ++p) {
        double mu = static_cast<double>(p) / static_cast<double>(numPhases_);
        float* row = &polyphaseTable_[p * filterTaps_];
        double sum = 0.0;

        for (int32_t j = 0; j < filterTaps_; ++j) {
            double tau = static_cast<double>(j - filterHalfWidth_) - mu;

            // Sinc calculation
            double z = 2.0 * cutoff * tau;
            double sincVal = (std::abs(z) < 1e-9) ? 1.0 : (std::sin(M_PI * z) / (M_PI * z));

            // Window calculation
            double u = (tau + halfW) / twoHalfW;
            double w = 0.0;
            if (u >= 0.0 && u <= 1.0) {
                w = a0 - a1 * std::cos(2.0 * M_PI * u)
                       + a2 * std::cos(4.0 * M_PI * u)
                       - a3 * std::cos(6.0 * M_PI * u);
            }

            double h = 2.0 * cutoff * sincVal * w;
            row[j] = static_cast<float>(h);
            sum += h;
        }

        // DC gain normalization (ensures exact 0 dB passband gain for all phases)
        if (std::abs(sum) > 1e-9) {
            float invSum = static_cast<float>(1.0 / sum);
            for (int32_t j = 0; j < filterTaps_; ++j) {
                row[j] *= invSum;
            }
        }
    }
}

uint32_t SampleRateConverter::calculateOutputFrames(
    uint32_t inputFrames,
    double inSampleRate,
    double outSampleRate) noexcept
{
    if (inSampleRate <= 0.0 || outSampleRate <= 0.0 || inputFrames == 0) {
        return 0;
    }
    if (std::abs(inSampleRate - outSampleRate) < 1e-4) {
        return inputFrames;
    }
    double expected = static_cast<double>(inputFrames) * (outSampleRate / inSampleRate);
    return static_cast<uint32_t>(std::round(expected));
}

bool SampleRateConverter::resampleBuffer(const AudioBuffer& input, AudioBuffer& output) {
    if (status_ == SrcStatus::Unconfigured || status_ == SrcStatus::Unsupported) {
        return false;
    }

    const uint32_t inFrames = input.getFrameCount();
    const uint32_t chCount = std::min(channels_, input.getChannelCount());

    // 1. Bypass mode: Direct bit-exact passthrough
    if (status_ == SrcStatus::Bypass) {
        output.resize(channels_, inFrames);
        for (uint32_t ch = 0; ch < chCount; ++ch) {
            std::memcpy(output.getChannelData(ch), input.getChannelData(ch), inFrames * sizeof(float));
        }
        // Zero any extra unpopulated channels
        for (uint32_t ch = chCount; ch < channels_; ++ch) {
            std::memset(output.getChannelData(ch), 0, inFrames * sizeof(float));
        }
        totalInFramesProcessed_ += inFrames;
        totalOutFramesProcessed_ += inFrames;
        return true;
    }

    // 2. Active mode: Polyphase windowed-sinc conversion
    const uint32_t outFrames = calculateOutputFrames(inFrames, inSampleRate_, outSampleRate_);
    output.resize(channels_, outFrames);
    if (outFrames == 0 || inFrames == 0) {
        return true;
    }

    const double step = invRatio_;
    const int32_t halfW = filterHalfWidth_;
    const int32_t taps = filterTaps_;
    const uint32_t P = numPhases_;
    const int32_t maxInIdx = static_cast<int32_t>(inFrames) - 1;

    for (uint32_t n = 0; n < outFrames; ++n) {
        double t = static_cast<double>(n) * step;
        int32_t k = static_cast<int32_t>(std::floor(t));
        double frac = t - static_cast<double>(k);
        int32_t phase = std::clamp(static_cast<int32_t>(frac * P), 0, static_cast<int32_t>(P - 1));
        const float* filterCoeffs = &polyphaseTable_[phase * taps];

        for (uint32_t ch = 0; ch < chCount; ++ch) {
            const float* inData = input.getChannelData(ch);
            float acc = 0.0f;

            for (int32_t j = 0; j < taps; ++j) {
                int32_t idx = k + (j - halfW);
                idx = std::clamp(idx, 0, maxInIdx);
                acc += inData[idx] * filterCoeffs[j];
            }

            if (std::isnan(acc) || std::isinf(acc)) {
                acc = 0.0f;
            }
            output.getChannelData(ch)[n] = acc;
        }

        for (uint32_t ch = chCount; ch < channels_; ++ch) {
            output.getChannelData(ch)[n] = 0.0f;
        }
    }

    totalInFramesProcessed_ += inFrames;
    totalOutFramesProcessed_ += outFrames;
    return true;
}

uint32_t SampleRateConverter::processStreaming(
    const float* const* inChannels,
    uint32_t inFrames,
    uint32_t& inFramesConsumed,
    float* const* outChannels,
    uint32_t outFrames)
{
    if (status_ == SrcStatus::Unconfigured || status_ == SrcStatus::Unsupported) {
        inFramesConsumed = 0;
        return 0;
    }

    if (status_ == SrcStatus::Bypass) {
        uint32_t count = std::min(inFrames, outFrames);
        for (uint32_t ch = 0; ch < channels_; ++ch) {
            std::memcpy(outChannels[ch], inChannels[ch], count * sizeof(float));
        }
        inFramesConsumed = count;
        totalInFramesProcessed_ += count;
        totalOutFramesProcessed_ += count;
        return count;
    }

    const double step = invRatio_;
    const int32_t halfW = filterHalfWidth_;
    const int32_t taps = filterTaps_;
    const uint32_t P = numPhases_;
    const size_t histSize = historyBuffers_[0].size();

    double t = fractionalPhase_;
    uint32_t produced = 0;
    int32_t lastK = 0;

    for (uint32_t n = 0; n < outFrames; ++n) {
        int32_t k = static_cast<int32_t>(std::floor(t));
        // Check if we require samples beyond current available inFrames
        if (k + halfW >= static_cast<int32_t>(inFrames)) {
            break;
        }

        double frac = t - static_cast<double>(k);
        int32_t phase = std::clamp(static_cast<int32_t>(frac * P), 0, static_cast<int32_t>(P - 1));
        const float* filterCoeffs = &polyphaseTable_[phase * taps];

        for (uint32_t ch = 0; ch < channels_; ++ch) {
            float acc = 0.0f;

            for (int32_t j = 0; j < taps; ++j) {
                int32_t idx = k + (j - halfW);
                float val = 0.0f;
                if (idx < 0) {
                    // Pull from history buffer
                    int32_t hIdx = static_cast<int32_t>(histSize) + idx;
                    if (hIdx >= 0 && hIdx < static_cast<int32_t>(histSize)) {
                        val = historyBuffers_[ch][hIdx];
                    }
                } else if (idx < static_cast<int32_t>(inFrames)) {
                    val = inChannels[ch][idx];
                }
                acc += val * filterCoeffs[j];
            }

            if (std::isnan(acc) || std::isinf(acc)) acc = 0.0f;
            outChannels[ch][n] = acc;
        }

        produced++;
        lastK = k;
        t += step;
    }

    // Update fractional phase and consume count
    int32_t consumedK = (produced > 0) ? (lastK + 1) : 0;
    inFramesConsumed = std::clamp(static_cast<uint32_t>(consumedK), 0u, inFrames);
    fractionalPhase_ = t - static_cast<double>(consumedK);

    // Save trailing consumed samples into history buffer for seamless block-to-block continuity
    for (uint32_t ch = 0; ch < channels_; ++ch) {
        auto& hBuf = historyBuffers_[ch];
        if (inFramesConsumed >= histSize) {
            // Replace full history with the most recent consumed samples
            const float* tailSrc = &inChannels[ch][inFramesConsumed - histSize];
            std::memcpy(hBuf.data(), tailSrc, histSize * sizeof(float));
        } else if (inFramesConsumed > 0) {
            // Shift existing history left and append new consumed samples
            size_t keep = histSize - inFramesConsumed;
            std::memmove(hBuf.data(), hBuf.data() + inFramesConsumed, keep * sizeof(float));
            std::memcpy(hBuf.data() + keep, inChannels[ch], inFramesConsumed * sizeof(float));
        }
    }
    historyAvailable_ = static_cast<uint32_t>(histSize);

    totalInFramesProcessed_ += inFramesConsumed;
    totalOutFramesProcessed_ += produced;
    return produced;
}

bool SampleRateConverter::resample(
    const AudioBuffer& input,
    double inSampleRate,
    AudioBuffer& output,
    double outSampleRate,
    SrcQuality quality)
{
    SampleRateConverter converter;
    if (!converter.configure(inSampleRate, outSampleRate, input.getChannelCount(), quality)) {
        return false;
    }
    return converter.resampleBuffer(input, output);
}

SrcStats SampleRateConverter::getStats() const noexcept {
    SrcStats s;
    s.totalInFrames = totalInFramesProcessed_;
    s.totalOutFrames = totalOutFramesProcessed_;
    s.inSampleRate = inSampleRate_;
    s.outSampleRate = outSampleRate_;
    s.ratio = ratio_;
    s.durationInSec = (inSampleRate_ > 0.0) ? (static_cast<double>(s.totalInFrames) / inSampleRate_) : 0.0;
    s.durationOutSec = (outSampleRate_ > 0.0) ? (static_cast<double>(s.totalOutFrames) / outSampleRate_) : 0.0;
    s.durationErrorSec = std::abs(s.durationOutSec - s.durationInSec);

    uint32_t expectedFrames = calculateOutputFrames(static_cast<uint32_t>(s.totalInFrames), inSampleRate_, outSampleRate_);
    s.frameErrorCount = static_cast<int64_t>(s.totalOutFrames) - static_cast<int64_t>(expectedFrames);
    return s;
}

const char* SampleRateConverter::statusToString(SrcStatus status) noexcept {
    switch (status) {
        case SrcStatus::Unconfigured: return "UNCONFIGURED";
        case SrcStatus::Bypass:       return "BYPASS";
        case SrcStatus::Active:       return "ACTIVE";
        case SrcStatus::Unsupported:  return "UNAVAILABLE";
        default:                      return "UNKNOWN";
    }
}

const char* SampleRateConverter::qualityToString(SrcQuality quality) noexcept {
    switch (quality) {
        case SrcQuality::Linear:       return "Linear (Draft)";
        case SrcQuality::CubicHermite: return "Cubic Hermite";
        case SrcQuality::SincStandard: return "Windowed Sinc (32 Taps)";
        case SrcQuality::SincHigh:     return "Windowed Sinc High (64 Taps)";
        default:                       return "Unknown";
    }
}

} // namespace audiocore
