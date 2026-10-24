#include "audiocore/geq_node.hpp"
#include "audiocore/audio_buffer.hpp"
#include <cmath>
#include <algorithm>

namespace audiocore {

constexpr double GraphicEQNode::kCenterFrequencies[kNumBands];

GraphicEQNode::GraphicEQNode(double sampleRate)
    : sample_rate_(sampleRate > 0.0 ? sampleRate : 48000.0)
{
    band_gains_.fill(0.0f);
    updateAllCoefficients();
    reset();
}

void GraphicEQNode::setSampleRate(double sampleRate) noexcept {
    if (sampleRate > 0.0 && sampleRate != sample_rate_) {
        sample_rate_ = sampleRate;
        updateAllCoefficients();
    }
}

double GraphicEQNode::getBandFrequency(size_t band) noexcept {
    if (band < kNumBands) {
        return kCenterFrequencies[band];
    }
    return 1000.0;
}

void GraphicEQNode::setBandGainDb(size_t band, float gainDb) noexcept {
    if (band >= kNumBands) return;
    band_gains_[band] = std::clamp(gainDb, -12.0f, 12.0f);
    updateBandCoefficients(band);
}

float GraphicEQNode::getBandGainDb(size_t band) const noexcept {
    if (band < kNumBands) {
        return band_gains_[band];
    }
    return 0.0f;
}

void GraphicEQNode::resetToFlat() noexcept {
    for (size_t b = 0; b < kNumBands; ++b) {
        band_gains_[b] = 0.0f;
        updateBandCoefficients(b);
    }
}

size_t GraphicEQNode::getActiveBandCount() const noexcept {
    size_t count = 0;
    for (size_t b = 0; b < kNumBands; ++b) {
        if (std::abs(band_gains_[b]) >= 0.001f) {
            count++;
        }
    }
    return count;
}

void GraphicEQNode::updateBandCoefficients(size_t band) noexcept {
    if (band >= kNumBands) return;
    const double freq = kCenterFrequencies[band];
    const double gain = static_cast<double>(band_gains_[band]);
    filters_[band].configure(FilterType::Peaking, sample_rate_, freq, kBandQ, gain);
}

void GraphicEQNode::updateAllCoefficients() noexcept {
    for (size_t b = 0; b < kNumBands; ++b) {
        updateBandCoefficients(b);
    }
}

void GraphicEQNode::reset() {
    for (auto& filter : filters_) {
        filter.reset();
    }
}

void GraphicEQNode::process(
    const AudioBuffer& input,
    AudioBuffer& output,
    uint32_t frames)
{
    const uint32_t inChannels = input.getChannelCount();
    const uint32_t outChannels = output.getChannelCount();
    const uint32_t actualFrames = std::min(frames, std::min(input.getFrameCount(), output.getFrameCount()));

    if (inChannels == 0 || outChannels == 0 || actualFrames == 0) return;

    if (bypassed_.load(std::memory_order_relaxed)) {
        if (&input != &output) {
            for (uint32_t ch = 0; ch < outChannels; ++ch) {
                uint32_t inCh = (ch < inChannels) ? ch : (inChannels - 1);
                std::copy_n(input.getChannelData(inCh), actualFrames, output.getChannelData(ch));
            }
        }
        return;
    }

    // Copy input to output buffer if not in-place
    if (&input != &output) {
        for (uint32_t ch = 0; ch < outChannels; ++ch) {
            uint32_t inCh = (ch < inChannels) ? ch : (inChannels - 1);
            std::copy_n(input.getChannelData(inCh), actualFrames, output.getChannelData(ch));
        }
    }

    // Process each channel through the active band filters in cascade
    const uint32_t procChannels = std::min(2u, outChannels);
    for (uint32_t ch = 0; ch < procChannels; ++ch) {
        float* channelData = output.getChannelData(ch);
        for (size_t b = 0; b < kNumBands; ++b) {
            if (std::abs(band_gains_[b]) >= 0.001f) {
                filters_[b].processBlock(ch, channelData, channelData, actualFrames);
            }
        }
    }

    // Clear any extra output channels beyond stereo
    for (uint32_t ch = 2; ch < outChannels; ++ch) {
        output.clear(actualFrames);
    }
}

} // namespace audiocore

