#include "audiocore/eq_node.hpp"
#include "audiocore/audio_buffer.hpp"
#include <algorithm>

namespace audiocore {

ParametricEQNode::ParametricEQNode(uint32_t numBands, double sampleRate)
    : sample_rate_(sampleRate > 0.0 ? sampleRate : 48000.0)
{
    bands_.resize(std::max(1u, numBands));

    // Default configuration for 4 standard bands
    if (bands_.size() >= 4) {
        bands_[0].type = FilterType::LowShelf;
        bands_[0].frequency = 100.0;
        bands_[0].q = 0.7071;
        bands_[0].gainDb = 0.0;

        bands_[1].type = FilterType::Peaking;
        bands_[1].frequency = 1000.0;
        bands_[1].q = 1.0;
        bands_[1].gainDb = 0.0;

        bands_[2].type = FilterType::Peaking;
        bands_[2].frequency = 3500.0;
        bands_[2].q = 1.0;
        bands_[2].gainDb = 0.0;

        bands_[3].type = FilterType::HighShelf;
        bands_[3].frequency = 10000.0;
        bands_[3].q = 0.7071;
        bands_[3].gainDb = 0.0;
    }

    for (auto& band : bands_) {
        band.filter.configure(band.type, sample_rate_, band.frequency, band.q, band.gainDb);
    }
}

void ParametricEQNode::setSampleRate(double sampleRate) noexcept {
    if (sampleRate > 0.0 && sampleRate != sample_rate_) {
        sample_rate_ = sampleRate;
        for (auto& band : bands_) {
            band.filter.configure(band.type, sample_rate_, band.frequency, band.q, band.gainDb);
        }
    }
}

void ParametricEQNode::setBand(uint32_t band, FilterType type, double freqHz, double Q, double gainDb) {
    if (band < bands_.size()) {
        bands_[band].type = type;
        bands_[band].frequency = freqHz;
        bands_[band].q = Q;
        bands_[band].gainDb = gainDb;
        bands_[band].filter.configure(type, sample_rate_, freqHz, Q, gainDb);
    }
}

void ParametricEQNode::setBandGainDb(uint32_t band, double gainDb) {
    if (band < bands_.size()) {
        bands_[band].gainDb = gainDb;
        bands_[band].filter.configure(bands_[band].type, sample_rate_, bands_[band].frequency, bands_[band].q, gainDb);
    }
}

void ParametricEQNode::setBandFrequency(uint32_t band, double freqHz) {
    if (band < bands_.size()) {
        bands_[band].frequency = freqHz;
        bands_[band].filter.configure(bands_[band].type, sample_rate_, freqHz, bands_[band].q, bands_[band].gainDb);
    }
}

void ParametricEQNode::setBandQ(uint32_t band, double Q) {
    if (band < bands_.size()) {
        bands_[band].q = Q;
        bands_[band].filter.configure(bands_[band].type, sample_rate_, bands_[band].frequency, Q, bands_[band].gainDb);
    }
}

void ParametricEQNode::setBandType(uint32_t band, FilterType type) {
    if (band < bands_.size()) {
        bands_[band].type = type;
        bands_[band].filter.configure(type, sample_rate_, bands_[band].frequency, bands_[band].q, bands_[band].gainDb);
    }
}

void ParametricEQNode::setBandEnabled(uint32_t band, bool enabled) {
    if (band < bands_.size()) {
        bands_[band].enabled = enabled;
    }
}

double ParametricEQNode::getBandGainDb(uint32_t band) const noexcept {
    if (band < bands_.size()) return bands_[band].gainDb;
    return 0.0;
}

double ParametricEQNode::getBandFrequency(uint32_t band) const noexcept {
    if (band < bands_.size()) return bands_[band].frequency;
    return 1000.0;
}

double ParametricEQNode::getBandQ(uint32_t band) const noexcept {
    if (band < bands_.size()) return bands_[band].q;
    return 0.7071;
}

FilterType ParametricEQNode::getBandType(uint32_t band) const noexcept {
    if (band < bands_.size()) return bands_[band].type;
    return FilterType::Bypass;
}

bool ParametricEQNode::isBandEnabled(uint32_t band) const noexcept {
    if (band < bands_.size()) return bands_[band].enabled;
    return false;
}

void ParametricEQNode::reset() {
    for (auto& band : bands_) {
        band.filter.reset();
    }
}

void ParametricEQNode::process(
    const AudioBuffer& input,
    AudioBuffer& output,
    uint32_t frames)
{
    const uint32_t inChannels = input.getChannelCount();
    const uint32_t outChannels = output.getChannelCount();
    const uint32_t actualFrames = std::min(frames, std::min(input.getFrameCount(), output.getFrameCount()));

    if (inChannels == 0 || outChannels == 0 || actualFrames == 0) return;

    const bool bypassed = bypassed_.load(std::memory_order_relaxed);
    const uint32_t processChannels = std::min(inChannels, std::min(outChannels, 2u));

    // First, copy input to output if not in-place
    if (&input != &output) {
        output.copyFrom(input, actualFrames);
    }

    if (bypassed) {
        return;
    }

    // Cascade through all active filter bands in-place in output buffer
    for (auto& band : bands_) {
        if (!band.enabled || band.type == FilterType::Bypass) continue;

        // Skip processing if gain is 0 for peaking/shelves
        if ((band.type == FilterType::Peaking || band.type == FilterType::LowShelf || band.type == FilterType::HighShelf) &&
            std::abs(band.gainDb) < 1e-4) {
            continue;
        }

        for (uint32_t ch = 0; ch < processChannels; ++ch) {
            float* chData = output.getChannelData(ch);
            band.filter.processBlock(ch, chData, chData, actualFrames);
        }
    }
}

} // namespace audiocore

