#include "audiocore/audio_buffer.hpp"

namespace audiocore {

AudioBuffer::AudioBuffer()
    : channels_(0)
    , frames_(0)
{
}

AudioBuffer::AudioBuffer(uint32_t channels, uint32_t frames)
    : channels_(channels)
    , frames_(frames)
{
    resize(channels, frames);
}

AudioBuffer::AudioBuffer(const AudioBuffer& other)
    : channels_(other.channels_)
    , frames_(other.frames_)
    , storage_(other.storage_)
{
    rebuildPointers();
}

AudioBuffer::AudioBuffer(AudioBuffer&& other) noexcept
    : channels_(other.channels_)
    , frames_(other.frames_)
    , storage_(std::move(other.storage_))
{
    other.channels_ = 0;
    other.frames_ = 0;
    other.channel_ptrs_.clear();
    rebuildPointers();
}

AudioBuffer& AudioBuffer::operator=(const AudioBuffer& other) {
    if (this != &other) {
        channels_ = other.channels_;
        frames_ = other.frames_;
        storage_ = other.storage_;
        rebuildPointers();
    }
    return *this;
}

AudioBuffer& AudioBuffer::operator=(AudioBuffer&& other) noexcept {
    if (this != &other) {
        channels_ = other.channels_;
        frames_ = other.frames_;
        storage_ = std::move(other.storage_);
        other.channels_ = 0;
        other.frames_ = 0;
        other.channel_ptrs_.clear();
        rebuildPointers();
    }
    return *this;
}

void AudioBuffer::resize(uint32_t channels, uint32_t frames) {
    channels_ = channels;
    frames_ = frames;

    const size_t total_samples = static_cast<size_t>(channels) * static_cast<size_t>(frames);
    storage_.resize(total_samples, 0.0f);
    rebuildPointers();
}

void AudioBuffer::clear() noexcept {
    if (!storage_.empty()) {
        std::memset(storage_.data(), 0, storage_.size() * sizeof(float));
    }
}

void AudioBuffer::clear(uint32_t frames) noexcept {
    const uint32_t count = std::min(frames, frames_);
    if (count == 0 || channels_ == 0) return;

    for (uint32_t ch = 0; ch < channels_; ++ch) {
        std::memset(channel_ptrs_[ch], 0, count * sizeof(float));
    }
}

void AudioBuffer::copyFrom(const AudioBuffer& source, uint32_t frames) noexcept {
    if (this == &source) return;

    const uint32_t copy_channels = std::min(channels_, source.channels_);
    const uint32_t max_copy = std::min(frames_, source.frames_);
    const uint32_t copy_frames = (frames > 0) ? std::min(frames, max_copy) : max_copy;

    if (copy_frames > 0) {
        for (uint32_t ch = 0; ch < copy_channels; ++ch) {
            std::memcpy(channel_ptrs_[ch], source.channel_ptrs_[ch], copy_frames * sizeof(float));
        }
    }

    // Zero any extra channels in this buffer
    for (uint32_t ch = copy_channels; ch < channels_; ++ch) {
        std::memset(channel_ptrs_[ch], 0, copy_frames * sizeof(float));
    }
}

void AudioBuffer::rebuildPointers() noexcept {
    channel_ptrs_.resize(channels_);
    if (channels_ == 0 || frames_ == 0 || storage_.empty()) {
        return;
    }

    float* base = storage_.data();
    for (uint32_t ch = 0; ch < channels_; ++ch) {
        channel_ptrs_[ch] = base + static_cast<size_t>(ch) * frames_;
    }
}

} // namespace audiocore

