#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <functiondiscoverykeys_devpkey.h>
#include <avrt.h>
#include <ksmedia.h>

#include "backend/wasapi_backend.hpp"
#include "audiocore/audio_engine.hpp"
#include "audiocore/sample_rate_converter.hpp"

#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <array>
#include <chrono>
#include <mutex>

namespace audiocore {

namespace {

template <typename T>
inline void safeRelease(T*& ptr) {
    if (ptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

inline void safeCloseHandle(HANDLE& handle) {
    if (handle && handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
        handle = nullptr;
    }
}

std::string wideToUtf8(const wchar_t* wstr) {
    if (!wstr) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &result[0], len, nullptr, nullptr);
    return result;
}

// Human readable text for the WASAPI HRESULTs that matter for stream bring-up.
std::string hrToString(HRESULT hr) {
    const char* name = nullptr;
    switch (static_cast<uint32_t>(hr)) {
        case 0x88890001: name = "AUDCLNT_E_NOT_INITIALIZED"; break;
        case 0x88890002: name = "AUDCLNT_E_ALREADY_INITIALIZED"; break;
        case 0x88890003: name = "AUDCLNT_E_WRONG_ENDPOINT_TYPE"; break;
        case 0x88890004: name = "AUDCLNT_E_DEVICE_INVALIDATED"; break;
        case 0x88890005: name = "AUDCLNT_E_NOT_STOPPED"; break;
        case 0x88890006: name = "AUDCLNT_E_BUFFER_TOO_LARGE"; break;
        case 0x88890007: name = "AUDCLNT_E_OUT_OF_ORDER"; break;
        case 0x88890008: name = "AUDCLNT_E_UNSUPPORTED_FORMAT"; break;
        case 0x88890009: name = "AUDCLNT_E_INVALID_SIZE"; break;
        case 0x8889000A: name = "AUDCLNT_E_DEVICE_IN_USE"; break;
        case 0x8889000B: name = "AUDCLNT_E_BUFFER_OPERATION_PENDING"; break;
        case 0x8889000C: name = "AUDCLNT_E_THREAD_NOT_REGISTERED"; break;
        case 0x8889000E: name = "AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED"; break;
        case 0x8889000F: name = "AUDCLNT_E_ENDPOINT_CREATE_FAILED"; break;
        case 0x88890010: name = "AUDCLNT_E_SERVICE_NOT_RUNNING"; break;
        case 0x88890011: name = "AUDCLNT_E_EVENTHANDLE_NOT_EXPECTED"; break;
        case 0x88890012: name = "AUDCLNT_E_EXCLUSIVE_MODE_ONLY"; break;
        case 0x88890013: name = "AUDCLNT_E_BUFDURATION_PERIOD_NOT_EQUAL"; break;
        case 0x88890014: name = "AUDCLNT_E_EVENTHANDLE_NOT_SET"; break;
        case 0x88890015: name = "AUDCLNT_E_INCORRECT_BUFFER_SIZE"; break;
        case 0x88890016: name = "AUDCLNT_E_BUFFER_SIZE_ERROR"; break;
        case 0x88890017: name = "AUDCLNT_E_CPUUSAGE_EXCEEDED"; break;
        case 0x88890019: name = "AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED"; break;
        case 0x88890021: name = "AUDCLNT_E_INVALID_DEVICE_PERIOD"; break;
        default: break;
    }
    std::ostringstream ss;
    ss << "0x" << std::hex << std::uppercase << static_cast<uint32_t>(hr);
    if (name) ss << " (" << name << ")";
    return ss.str();
}

std::wstring utf8ToWide(const std::string& str) {
    if (str.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring result(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], len);
    return result;
}

enum class SampleFormat {
    Float32,
    Int16,
    Int24In32,
    Int24Packed,
    Int32,
    Unsupported
};

// Static GUID definitions for broad compiler compatibility
static const GUID kSubtypePCM =
    { 0x00000001, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };
static const GUID kSubtypeIEEEFloat =
    { 0x00000003, 0x0000, 0x0010, { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 } };

SampleFormat determineFormat(const WAVEFORMATEX* wfx) {
    if (!wfx) return SampleFormat::Unsupported;
    if (wfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        if (wfx->wBitsPerSample == 32) return SampleFormat::Float32;
    } else if (wfx->wFormatTag == WAVE_FORMAT_PCM) {
        if (wfx->wBitsPerSample == 16) return SampleFormat::Int16;
        if (wfx->wBitsPerSample == 24) return SampleFormat::Int24Packed;
        if (wfx->wBitsPerSample == 32) return SampleFormat::Int32;
    } else if (wfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wfx);
        if (IsEqualGUID(ext->SubFormat, kSubtypeIEEEFloat)) {
            if (wfx->wBitsPerSample == 32) return SampleFormat::Float32;
        } else if (IsEqualGUID(ext->SubFormat, kSubtypePCM)) {
            if (wfx->wBitsPerSample == 16) return SampleFormat::Int16;
            if (wfx->wBitsPerSample == 24) return SampleFormat::Int24Packed;
            if (wfx->wBitsPerSample == 32) {
                if (ext->Samples.wValidBitsPerSample == 24) return SampleFormat::Int24In32;
                return SampleFormat::Int32;
            }
        }
    }
    return SampleFormat::Unsupported;
}

inline float clampSample(float v) noexcept {
    if (v > 1.0f) return 1.0f;
    if (v < -1.0f) return -1.0f;
    return v;
}

// ==============================================================================
// Preallocated Realtime-Safe AudioRingBuffer
// ==============================================================================
class AudioRingBuffer {
public:
    AudioRingBuffer() = default;

    void configure(uint32_t channels, uint32_t capacityFrames) {
        channels_ = (channels > 0) ? channels : 2;
        capacityFrames_ = capacityFrames;
        storage_.assign(static_cast<size_t>(channels_) * capacityFrames_, 0.0f);
        writePos_.store(0, std::memory_order_relaxed);
        readPos_.store(0, std::memory_order_relaxed);
    }

    uint32_t availableRead() const noexcept {
        size_t w = writePos_.load(std::memory_order_acquire);
        size_t r = readPos_.load(std::memory_order_relaxed);
        if (w >= r) return static_cast<uint32_t>(w - r);
        return static_cast<uint32_t>(capacityFrames_ - (r - w));
    }

    uint32_t availableWrite() const noexcept {
        uint32_t avail = availableRead();
        return (capacityFrames_ > avail + 1) ? (capacityFrames_ - 1 - avail) : 0;
    }

    uint32_t writePlanar(const AudioBuffer& source, uint32_t frames) noexcept {
        uint32_t toWrite = std::min(frames, availableWrite());
        if (toWrite == 0) return 0;

        size_t w = writePos_.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < toWrite; ++i) {
            size_t idx = (w + i) % capacityFrames_;
            for (uint32_t ch = 0; ch < channels_; ++ch) {
                float val = (ch < source.getChannelCount()) ? source.getChannelData(ch)[i] : 0.0f;
                storage_[idx * channels_ + ch] = val;
            }
        }
        writePos_.store((w + toWrite) % capacityFrames_, std::memory_order_release);
        return toWrite;
    }

    uint32_t writeSilence(uint32_t frames) noexcept {
        uint32_t toWrite = std::min(frames, availableWrite());
        if (toWrite == 0) return 0;

        size_t w = writePos_.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < toWrite; ++i) {
            size_t idx = (w + i) % capacityFrames_;
            for (uint32_t ch = 0; ch < channels_; ++ch) {
                storage_[idx * channels_ + ch] = 0.0f;
            }
        }
        writePos_.store((w + toWrite) % capacityFrames_, std::memory_order_release);
        return toWrite;
    }

    uint32_t writePlanarStereo(const float* ch0, const float* ch1, uint32_t frames) noexcept {
        uint32_t toWrite = std::min(frames, availableWrite());
        if (toWrite == 0) return 0;

        size_t w = writePos_.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < toWrite; ++i) {
            size_t idx = (w + i) % capacityFrames_;
            storage_[idx * channels_ + 0] = ch0 ? ch0[i] : 0.0f;
            if (channels_ > 1) {
                storage_[idx * channels_ + 1] = ch1 ? ch1[i] : (ch0 ? ch0[i] : 0.0f);
            }
        }
        writePos_.store((w + toWrite) % capacityFrames_, std::memory_order_release);
        return toWrite;
    }

    uint32_t readPlanar(AudioBuffer& dest, uint32_t frames) noexcept {
        uint32_t toRead = std::min(frames, availableRead());
        size_t r = readPos_.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < toRead; ++i) {
            size_t idx = (r + i) % capacityFrames_;
            for (uint32_t ch = 0; ch < channels_; ++ch) {
                float val = storage_[idx * channels_ + ch];
                if (ch < dest.getChannelCount()) {
                    dest.getChannelData(ch)[i] = val;
                }
            }
        }
        if (toRead < frames) {
            for (uint32_t ch = 0; ch < dest.getChannelCount(); ++ch) {
                std::memset(dest.getChannelData(ch) + toRead, 0, (frames - toRead) * sizeof(float));
            }
        }
        readPos_.store((r + toRead) % capacityFrames_, std::memory_order_release);
        return toRead;
    }

    // --- Render Converters (RingBuffer -> Interleaved WASAPI Buffer) ---
    uint32_t popToInterleavedFloat32(float* dest, uint32_t frames) noexcept {
        uint32_t toRead = std::min(frames, availableRead());
        size_t r = readPos_.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < toRead; ++i) {
            size_t idx = (r + i) % capacityFrames_;
            for (uint32_t ch = 0; ch < channels_; ++ch) {
                dest[i * channels_ + ch] = clampSample(storage_[idx * channels_ + ch]);
            }
        }
        if (toRead < frames) {
            std::memset(dest + (toRead * channels_), 0, (frames - toRead) * channels_ * sizeof(float));
        }
        readPos_.store((r + toRead) % capacityFrames_, std::memory_order_release);
        return toRead;
    }

    uint32_t popToInterleavedInt16(int16_t* dest, uint32_t frames) noexcept {
        uint32_t toRead = std::min(frames, availableRead());
        size_t r = readPos_.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < toRead; ++i) {
            size_t idx = (r + i) % capacityFrames_;
            for (uint32_t ch = 0; ch < channels_; ++ch) {
                float s = clampSample(storage_[idx * channels_ + ch]);
                dest[i * channels_ + ch] = static_cast<int16_t>(s * 32767.0f);
            }
        }
        if (toRead < frames) {
            std::memset(dest + (toRead * channels_), 0, (frames - toRead) * channels_ * sizeof(int16_t));
        }
        readPos_.store((r + toRead) % capacityFrames_, std::memory_order_release);
        return toRead;
    }

    uint32_t popToInterleavedInt24In32(int32_t* dest, uint32_t frames) noexcept {
        uint32_t toRead = std::min(frames, availableRead());
        size_t r = readPos_.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < toRead; ++i) {
            size_t idx = (r + i) % capacityFrames_;
            for (uint32_t ch = 0; ch < channels_; ++ch) {
                float s = clampSample(storage_[idx * channels_ + ch]);
                dest[i * channels_ + ch] = static_cast<int32_t>(s * 8388607.0f) << 8;
                dest[i * channels_ + ch] = static_cast<int32_t>(s * 8388607.0f) * 256;
            }
        }
        if (toRead < frames) {
            std::memset(dest + (toRead * channels_), 0, (frames - toRead) * channels_ * sizeof(int32_t));
        }
        readPos_.store((r + toRead) % capacityFrames_, std::memory_order_release);
        return toRead;
    }

    uint32_t popToInterleavedInt24Packed(uint8_t* dest, uint32_t frames) noexcept {
        uint32_t toRead = std::min(frames, availableRead());
        size_t r = readPos_.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < toRead; ++i) {
            size_t idx = (r + i) % capacityFrames_;
            for (uint32_t ch = 0; ch < channels_; ++ch) {
                float s = clampSample(storage_[idx * channels_ + ch]);
                int32_t val = static_cast<int32_t>(s * 8388607.0f);
                size_t offset = (static_cast<size_t>(i) * channels_ + ch) * 3;
                dest[offset + 0] = static_cast<uint8_t>(val & 0xFF);
                dest[offset + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
                dest[offset + 2] = static_cast<uint8_t>((val >> 16) & 0xFF);
            }
        }
        if (toRead < frames) {
            std::memset(dest + (toRead * channels_ * 3), 0, (frames - toRead) * channels_ * 3);
        }
        readPos_.store((r + toRead) % capacityFrames_, std::memory_order_release);
        return toRead;
    }

    uint32_t popToInterleavedInt32(int32_t* dest, uint32_t frames) noexcept {
        uint32_t toRead = std::min(frames, availableRead());
        size_t r = readPos_.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < toRead; ++i) {
            size_t idx = (r + i) % capacityFrames_;
            for (uint32_t ch = 0; ch < channels_; ++ch) {
                float s = clampSample(storage_[idx * channels_ + ch]);
                dest[i * channels_ + ch] = static_cast<int32_t>(s * 2147483647.0f);
                double v = std::floor(static_cast<double>(s) * 2147483647.0 + 0.5);
                if (v > 2147483647.0) v = 2147483647.0;
                if (v < -2147483648.0) v = -2147483648.0;
                dest[i * channels_ + ch] = static_cast<int32_t>(v);
            }
        }
        if (toRead < frames) {
            std::memset(dest + (toRead * channels_), 0, (frames - toRead) * channels_ * sizeof(int32_t));
        }
        readPos_.store((r + toRead) % capacityFrames_, std::memory_order_release);
        return toRead;
    }

    // --- Capture Converters (Interleaved WASAPI Buffer -> RingBuffer) ---
    uint32_t pushFromInterleavedFloat32(const float* src, uint32_t frames, uint32_t srcChannels, float gain) noexcept {
        uint32_t toWrite = std::min(frames, availableWrite());
        if (toWrite == 0) return 0;

        size_t w = writePos_.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < toWrite; ++i) {
            size_t idx = (w + i) % capacityFrames_;
            for (uint32_t ch = 0; ch < channels_; ++ch) {
                float val = (ch < srcChannels) ? (src[i * srcChannels + ch] * gain) : 0.0f;
                storage_[idx * channels_ + ch] = val;
            }
        }
        writePos_.store((w + toWrite) % capacityFrames_, std::memory_order_release);
        return toWrite;
    }

    uint32_t pushFromInterleavedInt16(const int16_t* src, uint32_t frames, uint32_t srcChannels, float gain) noexcept {
        uint32_t toWrite = std::min(frames, availableWrite());
        if (toWrite == 0) return 0;
        constexpr float kNorm16 = 1.0f / 32768.0f;

        size_t w = writePos_.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < toWrite; ++i) {
            size_t idx = (w + i) % capacityFrames_;
            for (uint32_t ch = 0; ch < channels_; ++ch) {
                float val = (ch < srcChannels) ? (static_cast<float>(src[i * srcChannels + ch]) * kNorm16 * gain) : 0.0f;
                storage_[idx * channels_ + ch] = val;
            }
        }
        writePos_.store((w + toWrite) % capacityFrames_, std::memory_order_release);
        return toWrite;
    }

    uint32_t pushFromInterleavedInt24In32(const int32_t* src, uint32_t frames, uint32_t srcChannels, float gain) noexcept {
        uint32_t toWrite = std::min(frames, availableWrite());
        if (toWrite == 0) return 0;
        constexpr float kNorm24 = 1.0f / 8388608.0f;

        size_t w = writePos_.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < toWrite; ++i) {
            size_t idx = (w + i) % capacityFrames_;
            for (uint32_t ch = 0; ch < channels_; ++ch) {
                float val = (ch < srcChannels) ? (static_cast<float>(src[i * srcChannels + ch] >> 8) * kNorm24 * gain) : 0.0f;
                storage_[idx * channels_ + ch] = val;
            }
        }
        writePos_.store((w + toWrite) % capacityFrames_, std::memory_order_release);
        return toWrite;
    }

    uint32_t pushFromInterleavedInt24Packed(const uint8_t* src, uint32_t frames, uint32_t srcChannels, float gain) noexcept {
        uint32_t toWrite = std::min(frames, availableWrite());
        if (toWrite == 0) return 0;
        constexpr float kNorm24 = 1.0f / 8388608.0f;

        size_t w = writePos_.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < toWrite; ++i) {
            size_t idx = (w + i) % capacityFrames_;
            for (uint32_t ch = 0; ch < channels_; ++ch) {
                float val = 0.0f;
                if (ch < srcChannels) {
                    size_t offset = (static_cast<size_t>(i) * srcChannels + ch) * 3;
                    int32_t ival = (static_cast<int32_t>(src[offset + 0])) |
                                   (static_cast<int32_t>(src[offset + 1]) << 8) |
                                   (static_cast<int32_t>(static_cast<int8_t>(src[offset + 2])) << 16);
                    val = static_cast<float>(ival) * kNorm24 * gain;
                }
                storage_[idx * channels_ + ch] = val;
            }
        }
        writePos_.store((w + toWrite) % capacityFrames_, std::memory_order_release);
        return toWrite;
    }

    uint32_t pushFromInterleavedInt32(const int32_t* src, uint32_t frames, uint32_t srcChannels, float gain) noexcept {
        uint32_t toWrite = std::min(frames, availableWrite());
        if (toWrite == 0) return 0;
        constexpr float kNorm32 = 1.0f / 2147483648.0f;

        size_t w = writePos_.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < toWrite; ++i) {
            size_t idx = (w + i) % capacityFrames_;
            for (uint32_t ch = 0; ch < channels_; ++ch) {
                float val = (ch < srcChannels) ? (static_cast<float>(src[i * srcChannels + ch]) * kNorm32 * gain) : 0.0f;
                storage_[idx * channels_ + ch] = val;
            }
        }
        writePos_.store((w + toWrite) % capacityFrames_, std::memory_order_release);
        return toWrite;
    }

    void clear() noexcept {
        writePos_.store(0, std::memory_order_relaxed);
        readPos_.store(0, std::memory_order_relaxed);
        std::fill(storage_.begin(), storage_.end(), 0.0f);
    }

private:
    uint32_t channels_{2};
    uint32_t capacityFrames_{0};
    std::vector<float> storage_;
    std::atomic<size_t> writePos_{0};
    std::atomic<size_t> readPos_{0};
};

// Rolling timing buffer for lock-free percentile calculation
class TimingTracker {
public:
    static constexpr size_t kCapacity = 2048;

    TimingTracker() {
        buffer_.fill(0);
    }

    void record(uint32_t valueUs) noexcept {
        size_t idx = head_.fetch_add(1, std::memory_order_relaxed) % kCapacity;
        buffer_[idx] = valueUs;
    }

    void getPercentiles(uint32_t& p95, uint32_t& p99) const {
        std::vector<uint32_t> copy(kCapacity);
        for (size_t i = 0; i < kCapacity; ++i) {
            copy[i] = buffer_[i];
        }
        std::sort(copy.begin(), copy.end());
        size_t nonZeroStart = 0;
        while (nonZeroStart < kCapacity && copy[nonZeroStart] == 0) {
            nonZeroStart++;
        }
        size_t count = kCapacity - nonZeroStart;
        if (count == 0) {
            p95 = p99 = 0;
            return;
        }
        size_t i95 = nonZeroStart + static_cast<size_t>(count * 0.95);
        size_t i99 = nonZeroStart + static_cast<size_t>(count * 0.99);
        if (i95 >= kCapacity) i95 = kCapacity - 1;
        if (i99 >= kCapacity) i99 = kCapacity - 1;
        p95 = copy[i95];
        p99 = copy[i99];
    }

    void reset() {
        buffer_.fill(0);
        head_.store(0, std::memory_order_relaxed);
    }

private:
    std::array<uint32_t, kCapacity> buffer_;
    std::atomic<size_t> head_{0};
};

inline void decodeAndMapToStereo(
    const BYTE* src,
    uint32_t frames,
    uint32_t srcChannels,
    SampleFormat format,
    float gain,
    float* outL,
    float* outR) noexcept
{
    if (srcChannels == 1) {
        switch (format) {
            case SampleFormat::Float32: {
                const float* f = reinterpret_cast<const float*>(src);
                for (uint32_t i = 0; i < frames; ++i) {
                    float s = f[i] * gain;
                    outL[i] = s;
                    outR[i] = s;
                }
                break;
            }
            case SampleFormat::Int16: {
                constexpr float kNorm16 = 1.0f / 32768.0f;
                const int16_t* s16 = reinterpret_cast<const int16_t*>(src);
                for (uint32_t i = 0; i < frames; ++i) {
                    float s = static_cast<float>(s16[i]) * kNorm16 * gain;
                    outL[i] = s;
                    outR[i] = s;
                }
                break;
            }
            case SampleFormat::Int24In32: {
                constexpr float kNorm24 = 1.0f / 8388608.0f;
                const int32_t* s32 = reinterpret_cast<const int32_t*>(src);
                for (uint32_t i = 0; i < frames; ++i) {
                    float s = static_cast<float>(s32[i] >> 8) * kNorm24 * gain;
                    outL[i] = s;
                    outR[i] = s;
                }
                break;
            }
            case SampleFormat::Int24Packed: {
                constexpr float kNorm24 = 1.0f / 8388608.0f;
                for (uint32_t i = 0; i < frames; ++i) {
                    size_t off = static_cast<size_t>(i) * 3;
                    int32_t ival = (static_cast<int32_t>(src[off + 0])) |
                                   (static_cast<int32_t>(src[off + 1]) << 8) |
                                   (static_cast<int32_t>(static_cast<int8_t>(src[off + 2])) << 16);
                    float s = static_cast<float>(ival) * kNorm24 * gain;
                    outL[i] = s;
                    outR[i] = s;
                }
                break;
            }
            case SampleFormat::Int32: {
                constexpr float kNorm32 = 1.0f / 2147483648.0f;
                const int32_t* s32 = reinterpret_cast<const int32_t*>(src);
                for (uint32_t i = 0; i < frames; ++i) {
                    float s = static_cast<float>(s32[i]) * kNorm32 * gain;
                    outL[i] = s;
                    outR[i] = s;
                }
                break;
            }
            default:
                std::memset(outL, 0, frames * sizeof(float));
                std::memset(outR, 0, frames * sizeof(float));
                break;
        }
    } else {
        switch (format) {
            case SampleFormat::Float32: {
                const float* f = reinterpret_cast<const float*>(src);
                for (uint32_t i = 0; i < frames; ++i) {
                    outL[i] = f[i * srcChannels + 0] * gain;
                    outR[i] = f[i * srcChannels + 1] * gain;
                }
                break;
            }
            case SampleFormat::Int16: {
                constexpr float kNorm16 = 1.0f / 32768.0f;
                const int16_t* s16 = reinterpret_cast<const int16_t*>(src);
                for (uint32_t i = 0; i < frames; ++i) {
                    outL[i] = static_cast<float>(s16[i * srcChannels + 0]) * kNorm16 * gain;
                    outR[i] = static_cast<float>(s16[i * srcChannels + 1]) * kNorm16 * gain;
                }
                break;
            }
            case SampleFormat::Int24In32: {
                constexpr float kNorm24 = 1.0f / 8388608.0f;
                const int32_t* s32 = reinterpret_cast<const int32_t*>(src);
                for (uint32_t i = 0; i < frames; ++i) {
                    outL[i] = static_cast<float>(s32[i * srcChannels + 0] >> 8) * kNorm24 * gain;
                    outR[i] = static_cast<float>(s32[i * srcChannels + 1] >> 8) * kNorm24 * gain;
                }
                break;
            }
            case SampleFormat::Int24Packed: {
                constexpr float kNorm24 = 1.0f / 8388608.0f;
                for (uint32_t i = 0; i < frames; ++i) {
                    size_t off0 = (static_cast<size_t>(i) * srcChannels + 0) * 3;
                    size_t off1 = (static_cast<size_t>(i) * srcChannels + 1) * 3;
                    int32_t ival0 = (static_cast<int32_t>(src[off0 + 0])) |
                                    (static_cast<int32_t>(src[off0 + 1]) << 8) |
                                    (static_cast<int32_t>(static_cast<int8_t>(src[off0 + 2])) << 16);
                    int32_t ival1 = (static_cast<int32_t>(src[off1 + 0])) |
                                    (static_cast<int32_t>(src[off1 + 1]) << 8) |
                                    (static_cast<int32_t>(static_cast<int8_t>(src[off1 + 2])) << 16);
                    outL[i] = static_cast<float>(ival0) * kNorm24 * gain;
                    outR[i] = static_cast<float>(ival1) * kNorm24 * gain;
                }
                break;
            }
            case SampleFormat::Int32: {
                constexpr float kNorm32 = 1.0f / 2147483648.0f;
                const int32_t* s32 = reinterpret_cast<const int32_t*>(src);
                for (uint32_t i = 0; i < frames; ++i) {
                    outL[i] = static_cast<float>(s32[i * srcChannels + 0]) * kNorm32 * gain;
                    outR[i] = static_cast<float>(s32[i * srcChannels + 1]) * kNorm32 * gain;
                }
                break;
            }
            default:
                std::memset(outL, 0, frames * sizeof(float));
                std::memset(outR, 0, frames * sizeof(float));
                break;
        }
    }
}

} // anonymous namespace

// ==============================================================================
// WasapiBackend::Impl
// ==============================================================================

struct WasapiBackend::Impl {
    AudioEngine* engine{nullptr};

    // User-configured parameters
    double requestedSampleRate{48000.0};
    uint32_t requestedBlockSize{128};
    uint32_t requestedChannels{2};
    WasapiShareMode shareMode{WasapiShareMode::Shared};
    WasapiStreamMode streamMode{WasapiStreamMode::PlaybackOnly};
    bool eventDriven{true};

    // Actual negotiated hardware parameters
    double actualSampleRate{48000.0};
    uint32_t actualChannels{2};
    uint32_t actualBufferSizeFrames{0};
    uint32_t bitDepth{32};
    SampleFormat renderFormatType{SampleFormat::Unsupported};
    SampleFormat captureFormatType{SampleFormat::Unsupported};
    double actualPeriodicityMs{0.0};
    double reportedLatencyMs{0.0};

    // Device IDs and Friendly Names
    std::string selectedPlaybackDeviceId;
    std::string selectedCaptureDeviceId;
    std::string selectedLoopbackDeviceId;
    std::string activePlaybackDeviceName{"Default Playback Endpoint"};
    std::string activeCaptureDeviceName{"Default Capture Endpoint"};
    std::string activeLoopbackDeviceName{"Default Render Endpoint (Loopback)"};

    // Audio Source Type & Channel Mapping
    AudioSourceType sourceType{AudioSourceType::FilePlayback};
    int channelMappingMode{0};

    // Streaming SRC and preallocated capture buffers
    SampleRateConverter captureSrc;
    std::vector<float> capturePreMapCh0;
    std::vector<float> capturePreMapCh1;
    std::vector<float> captureSrcOutCh0;
    std::vector<float> captureSrcOutCh1;

    // Device Removal / Invalidation state
    std::atomic<bool> deviceInvalidated{false};
    std::string lastErrorMessage;
    uint32_t lastErrorCode{0};
    DeviceChangeCallback deviceChangeCallback;
    std::mutex callbackMutex;

    // Safety guard for feedback
    bool safeFeedbackGuard{true};
    float maxLinearInputGain{0.05f}; // -26 dB safe default

    // Streaming thread & control
    std::atomic<bool> initialized{false};
    std::atomic<bool> running{false};
    std::thread audioThread;
    HANDLE hAudioThreadNative{nullptr};
    std::atomic<int32_t> currentProcessorId{-1};

    // Windows Audio COM interfaces - Render
    IMMDeviceEnumerator* pEnumerator{nullptr};
    IMMDevice* pRenderDevice{nullptr};
    IAudioClient* pRenderClient{nullptr};
    IAudioRenderClient* pRenderService{nullptr};
    WAVEFORMATEX* pRenderFormat{nullptr};
    HANDLE hRenderEvent{nullptr};

    // Windows Audio COM interfaces - Capture
    IMMDevice* pCaptureDevice{nullptr};
    IAudioClient* pCaptureClient{nullptr};
    IAudioCaptureClient* pCaptureService{nullptr};
    WAVEFORMATEX* pCaptureFormat{nullptr};
    HANDLE hCaptureEvent{nullptr};

    // Notification client
    struct EndpointNotificationClient;
    EndpointNotificationClient* pNotifyClient{nullptr};

    // Preallocated Adapter Ring Buffers
    static constexpr uint32_t kRingBufferCapacity = 16384;
    AudioRingBuffer renderRingBuffer;
    AudioRingBuffer captureRingBuffer;

    // Preallocated internal engine block buffers
    uint32_t activeEngineBlockSize{128};
    AudioBuffer engineBlockIn;
    AudioBuffer engineBlockOut;

    // Diagnostics Telemetry (Atomics)
    std::atomic<uint64_t> totalCallbacks{0};
    std::atomic<uint64_t> totalFramesRequested{0};
    std::atomic<uint64_t> totalFramesProcessed{0};
    std::atomic<uint64_t> totalFramesSubmitted{0};
    std::atomic<uint32_t> frameCountMismatches{0};
    std::atomic<uint32_t> renderUnderruns{0};
    std::atomic<uint32_t> renderErrors{0};
    std::atomic<uint32_t> captureOverruns{0};
    std::atomic<uint32_t> captureErrors{0};
    std::atomic<uint32_t> bufferDiscontinuities{0};
    std::atomic<uint32_t> getBufferFailures{0};
    std::atomic<uint32_t> releaseBufferFailures{0};
    std::atomic<uint32_t> deadlineMisses{0};
    std::atomic<uint32_t> deviceInvalidationCount{0};
    std::atomic<uint32_t> streamRestartCount{0};
    std::atomic<uint32_t> sampleRateMismatches{0};
    std::atomic<uint32_t> channelMismatches{0};
    std::atomic<uint32_t> formatMismatches{0};

    // Microsecond timing telemetry
    std::atomic<uint64_t> minCallbackTimeUs{UINT64_MAX};
    std::atomic<uint64_t> maxCallbackTimeUs{0};
    std::atomic<uint64_t> totalCallbackTimeUs{0};

    std::atomic<uint64_t> minDspTimeUs{UINT64_MAX};
    std::atomic<uint64_t> maxDspTimeUs{0};
    std::atomic<uint64_t> totalDspTimeUs{0};

    std::atomic<uint64_t> totalSubmissionTimeUs{0};
    std::atomic<uint64_t> maxSubmissionTimeUs{0};

    // Capture & Render timing breakdown
    std::atomic<uint64_t> totalFramesCaptured{0};
    std::atomic<uint64_t> minCaptureTimeUs{UINT64_MAX};
    std::atomic<uint64_t> maxCaptureTimeUs{0};
    std::atomic<uint64_t> totalCaptureTimeUs{0};

    std::atomic<uint64_t> minRenderTimeUs{UINT64_MAX};
    std::atomic<uint64_t> maxRenderTimeUs{0};
    std::atomic<uint64_t> totalRenderTimeUs{0};

    std::atomic<uint32_t> nanCount{0};
    std::atomic<uint32_t> infCount{0};

    std::atomic<uint64_t> totalWakeupJitterUs{0};
    std::atomic<uint64_t> maxWakeupJitterUs{0};

    std::atomic<uint64_t> countProcessTime{0};

    TimingTracker callbackTimingTracker;
    TimingTracker dspTimingTracker;
    TimingTracker captureTimingTracker;
    TimingTracker renderTimingTracker;

    bool mmcssRegistered{false};
    uint32_t mmcssErrorCode{0};

    Impl() = default;

    ~Impl() {
        cleanup();
    }

    void resetDiagnostics() {
        totalCallbacks.store(0);
        totalFramesRequested.store(0);
        totalFramesProcessed.store(0);
        totalFramesSubmitted.store(0);
        totalFramesCaptured.store(0);
        frameCountMismatches.store(0);
        renderUnderruns.store(0);
        renderErrors.store(0);
        captureOverruns.store(0);
        captureErrors.store(0);
        bufferDiscontinuities.store(0);
        getBufferFailures.store(0);
        releaseBufferFailures.store(0);
        deadlineMisses.store(0);
        deviceInvalidationCount.store(0);
        sampleRateMismatches.store(0);
        channelMismatches.store(0);
        formatMismatches.store(0);
        nanCount.store(0);
        infCount.store(0);

        minCallbackTimeUs.store(UINT64_MAX);
        maxCallbackTimeUs.store(0);
        totalCallbackTimeUs.store(0);

        minDspTimeUs.store(UINT64_MAX);
        maxDspTimeUs.store(0);
        totalDspTimeUs.store(0);

        minCaptureTimeUs.store(UINT64_MAX);
        maxCaptureTimeUs.store(0);
        totalCaptureTimeUs.store(0);

        minRenderTimeUs.store(UINT64_MAX);
        maxRenderTimeUs.store(0);
        totalRenderTimeUs.store(0);

        totalSubmissionTimeUs.store(0);
        maxSubmissionTimeUs.store(0);

        totalWakeupJitterUs.store(0);
        maxWakeupJitterUs.store(0);

        countProcessTime.store(0);

        callbackTimingTracker.reset();
        dspTimingTracker.reset();
        captureTimingTracker.reset();
        renderTimingTracker.reset();
    }

    void recordTiming(uint64_t cbUs, uint64_t dspUs, uint64_t renUs, uint64_t capUs, uint64_t jitterUs, double blockDeadlineMs) {
        countProcessTime.fetch_add(1, std::memory_order_relaxed);

        // Callback times
        totalCallbackTimeUs.fetch_add(cbUs, std::memory_order_relaxed);
        uint64_t cMin = minCallbackTimeUs.load(std::memory_order_relaxed);
        while (cbUs < cMin && !minCallbackTimeUs.compare_exchange_weak(cMin, cbUs, std::memory_order_relaxed)) {}
        uint64_t cMax = maxCallbackTimeUs.load(std::memory_order_relaxed);
        while (cbUs > cMax && !maxCallbackTimeUs.compare_exchange_weak(cMax, cbUs, std::memory_order_relaxed)) {}

        // DSP times
        totalDspTimeUs.fetch_add(dspUs, std::memory_order_relaxed);
        uint64_t dMin = minDspTimeUs.load(std::memory_order_relaxed);
        while (dspUs < dMin && !minDspTimeUs.compare_exchange_weak(dMin, dspUs, std::memory_order_relaxed)) {}
        uint64_t dMax = maxDspTimeUs.load(std::memory_order_relaxed);
        while (dspUs > dMax && !maxDspTimeUs.compare_exchange_weak(dMax, dspUs, std::memory_order_relaxed)) {}

        // Render times
        totalRenderTimeUs.fetch_add(renUs, std::memory_order_relaxed);
        totalSubmissionTimeUs.fetch_add(renUs, std::memory_order_relaxed);
        uint64_t rMin = minRenderTimeUs.load(std::memory_order_relaxed);
        while (renUs < rMin && !minRenderTimeUs.compare_exchange_weak(rMin, renUs, std::memory_order_relaxed)) {}
        uint64_t rMax = maxRenderTimeUs.load(std::memory_order_relaxed);
        while (renUs > rMax && !maxRenderTimeUs.compare_exchange_weak(rMax, renUs, std::memory_order_relaxed)) {}
        maxSubmissionTimeUs.store(rMax, std::memory_order_relaxed);

        // Capture times
        totalCaptureTimeUs.fetch_add(capUs, std::memory_order_relaxed);
        uint64_t capMin = minCaptureTimeUs.load(std::memory_order_relaxed);
        while (capUs < capMin && !minCaptureTimeUs.compare_exchange_weak(capMin, capUs, std::memory_order_relaxed)) {}
        uint64_t capMax = maxCaptureTimeUs.load(std::memory_order_relaxed);
        while (capUs > capMax && !maxCaptureTimeUs.compare_exchange_weak(capMax, capUs, std::memory_order_relaxed)) {}

        // Jitter
        totalWakeupJitterUs.fetch_add(jitterUs, std::memory_order_relaxed);
        uint64_t jMax = maxWakeupJitterUs.load(std::memory_order_relaxed);
        while (jitterUs > jMax && !maxWakeupJitterUs.compare_exchange_weak(jMax, jitterUs, std::memory_order_relaxed)) {}

        callbackTimingTracker.record(static_cast<uint32_t>(cbUs));
        dspTimingTracker.record(static_cast<uint32_t>(dspUs));
        captureTimingTracker.record(static_cast<uint32_t>(capUs));
        renderTimingTracker.record(static_cast<uint32_t>(renUs));

        if ((static_cast<double>(cbUs) / 1000.0) > blockDeadlineMs) {
            deadlineMisses.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void cleanup() {
        if (running.load(std::memory_order_relaxed)) {
            stopStreaming();
        }

        unregisterNotificationClient();

        if (pRenderFormat) {
            CoTaskMemFree(pRenderFormat);
            pRenderFormat = nullptr;
        }
        if (pCaptureFormat) {
            CoTaskMemFree(pCaptureFormat);
            pCaptureFormat = nullptr;
        }

        safeCloseHandle(hRenderEvent);
        safeCloseHandle(hCaptureEvent);

        safeRelease(pRenderService);
        safeRelease(pRenderClient);
        safeRelease(pRenderDevice);

        safeRelease(pCaptureService);
        safeRelease(pCaptureClient);
        safeRelease(pCaptureDevice);

        safeRelease(pEnumerator);

        initialized.store(false, std::memory_order_relaxed);
    }

    bool initCom() {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    }

    bool initDeviceEnumerator() {
        if (pEnumerator) return true;
        initCom();
        HRESULT hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            reinterpret_cast<void**>(&pEnumerator));
        if (SUCCEEDED(hr) && pEnumerator) {
            registerNotificationClient();
            return true;
        }
        return false;
    }

    struct EndpointNotificationClient : public IMMNotificationClient {
    public:
        explicit EndpointNotificationClient(WasapiBackend::Impl* impl)
            : impl_(impl) {}

        STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
            if (riid == __uuidof(IUnknown) || riid == __uuidof(IMMNotificationClient)) {
                *ppv = static_cast<IMMNotificationClient*>(this);
                AddRef();
                return S_OK;
            }
            *ppv = nullptr;
            return E_NOINTERFACE;
        }

        STDMETHODIMP_(ULONG) AddRef() override {
            return refCount_.fetch_add(1, std::memory_order_relaxed) + 1;
        }

        STDMETHODIMP_(ULONG) Release() override {
            ULONG count = refCount_.fetch_sub(1, std::memory_order_relaxed) - 1;
            if (count == 0) {
                delete this;
            }
            return count;
        }

        STDMETHODIMP OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState) override {
            if (impl_ && pwstrDeviceId) {
                std::string id = wideToUtf8(pwstrDeviceId);
                if ((id == impl_->selectedPlaybackDeviceId || id == impl_->selectedCaptureDeviceId) &&
                    dwNewState != DEVICE_STATE_ACTIVE) {
                    impl_->deviceInvalidated.store(true, std::memory_order_release);
                    impl_->deviceInvalidationCount.fetch_add(1, std::memory_order_relaxed);
                }
                std::lock_guard<std::mutex> lock(impl_->callbackMutex);
                if (impl_->deviceChangeCallback) {
                    impl_->deviceChangeCallback("DeviceStateChanged", id);
                }
            }
            return S_OK;
        }

        STDMETHODIMP OnDeviceAdded(LPCWSTR pwstrDeviceId) override {
            if (impl_ && pwstrDeviceId) {
                std::string id = wideToUtf8(pwstrDeviceId);
                std::lock_guard<std::mutex> lock(impl_->callbackMutex);
                if (impl_->deviceChangeCallback) {
                    impl_->deviceChangeCallback("DeviceAdded", id);
                }
            }
            return S_OK;
        }

        STDMETHODIMP OnDeviceRemoved(LPCWSTR pwstrDeviceId) override {
            if (impl_ && pwstrDeviceId) {
                std::string id = wideToUtf8(pwstrDeviceId);
                if (id == impl_->selectedPlaybackDeviceId || id == impl_->selectedCaptureDeviceId) {
                    impl_->deviceInvalidated.store(true, std::memory_order_release);
                    impl_->deviceInvalidationCount.fetch_add(1, std::memory_order_relaxed);
                }
                std::lock_guard<std::mutex> lock(impl_->callbackMutex);
                if (impl_->deviceChangeCallback) {
                    impl_->deviceChangeCallback("DeviceRemoved", id);
                }
            }
            return S_OK;
        }

        STDMETHODIMP OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDefaultDeviceId) override {
            if (impl_ && pwstrDefaultDeviceId) {
                std::string id = wideToUtf8(pwstrDefaultDeviceId);
                std::lock_guard<std::mutex> lock(impl_->callbackMutex);
                if (impl_->deviceChangeCallback) {
                    impl_->deviceChangeCallback("DefaultDeviceChanged", id);
                }
            }
            return S_OK;
        }

        STDMETHODIMP OnPropertyValueChanged(LPCWSTR /*pwstrDeviceId*/, const PROPERTYKEY /*key*/) override {
            return S_OK;
        }

    private:
        WasapiBackend::Impl* impl_{nullptr};
        std::atomic<ULONG> refCount_{1};
    };

    void registerNotificationClient() {
        if (pEnumerator && !pNotifyClient) {
            pNotifyClient = new EndpointNotificationClient(this);
            pEnumerator->RegisterEndpointNotificationCallback(pNotifyClient);
        }
    }

    void unregisterNotificationClient() {
        if (pEnumerator && pNotifyClient) {
            pEnumerator->UnregisterEndpointNotificationCallback(pNotifyClient);
            safeRelease(pNotifyClient);
        }
    }

    bool setupRenderClient() {
        if (!initDeviceEnumerator()) {
            lastErrorMessage = "Failed to initialize MMDeviceEnumerator.";
            lastErrorCode = E_FAIL;
            return false;
        }

        HRESULT hr = S_OK;
        if (!selectedPlaybackDeviceId.empty()) {
            std::wstring wId = utf8ToWide(selectedPlaybackDeviceId);
            hr = pEnumerator->GetDevice(wId.c_str(), &pRenderDevice);
        } else {
            hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pRenderDevice);
        }

        if (FAILED(hr) || !pRenderDevice) {
            lastErrorMessage = "Failed to get render audio endpoint.";
            lastErrorCode = hr;
            return false;
        }

        IPropertyStore* pProps = nullptr;
        if (SUCCEEDED(pRenderDevice->OpenPropertyStore(STGM_READ, &pProps)) && pProps) {
            PROPVARIANT varName;
            PropVariantInit(&varName);
            if (SUCCEEDED(pProps->GetValue(PKEY_Device_FriendlyName, &varName)) && varName.pwszVal) {
                activePlaybackDeviceName = wideToUtf8(varName.pwszVal);
            }
            PropVariantClear(&varName);
            safeRelease(pProps);
        }

        hr = pRenderDevice->Activate(
            __uuidof(IAudioClient),
            CLSCTX_ALL,
            nullptr,
            reinterpret_cast<void**>(&pRenderClient));
        if (FAILED(hr) || !pRenderClient) {
            lastErrorMessage = "Failed to activate IAudioClient on render device.";
            lastErrorCode = hr;
            return false;
        }

        REFERENCE_TIME defaultPeriodHns = 0;
        REFERENCE_TIME minPeriodHns = 0;
        pRenderClient->GetDevicePeriod(&defaultPeriodHns, &minPeriodHns);

        if (shareMode == WasapiShareMode::Exclusive) {
            // --- Exclusive Mode Initialization ---
            return setupRenderExclusive(minPeriodHns);
        } else {
            // --- Shared Mode Initialization ---
            return setupRenderShared(defaultPeriodHns);
        }
    }

    bool setupRenderShared(REFERENCE_TIME defaultPeriodHns) {
        if (pRenderFormat) {
            CoTaskMemFree(pRenderFormat);
            pRenderFormat = nullptr;
        }

        HRESULT hr = pRenderClient->GetMixFormat(&pRenderFormat);
        if (FAILED(hr) || !pRenderFormat) {
            lastErrorMessage = "Failed to get shared mix format.";
            lastErrorCode = hr;
            return false;
        }

        actualSampleRate = static_cast<double>(pRenderFormat->nSamplesPerSec);
        actualChannels = static_cast<uint32_t>(pRenderFormat->nChannels);
        bitDepth = static_cast<uint32_t>(pRenderFormat->wBitsPerSample);
        renderFormatType = determineFormat(pRenderFormat);

        REFERENCE_TIME bufferDuration = static_cast<REFERENCE_TIME>(
            (static_cast<double>(requestedBlockSize) / actualSampleRate) * 10000000.0);
        if (bufferDuration < 50000) bufferDuration = 50000; // minimum 5ms

        DWORD streamFlags = eventDriven ? AUDCLNT_STREAMFLAGS_EVENTCALLBACK : 0;

        hr = pRenderClient->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            streamFlags,
            bufferDuration,
            0,
            pRenderFormat,
            nullptr);

        if (FAILED(hr)) {
            lastErrorMessage = "IAudioClient::Initialize (Shared) failed.";
            lastErrorMessage = "IAudioClient::Initialize (Shared) failed: " + hrToString(hr)
                + " [fmt " + std::to_string(pRenderFormat->nSamplesPerSec) + " Hz, "
                + std::to_string(pRenderFormat->nChannels) + " ch, "
                + std::to_string(pRenderFormat->wBitsPerSample) + " bit, tag "
                + std::to_string(pRenderFormat->wFormatTag) + ", bufferDuration "
                + std::to_string(bufferDuration) + " hns]";
            lastErrorCode = hr;
            return false;
        }

        if (eventDriven) {
            hRenderEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            if (!hRenderEvent) {
                lastErrorMessage = "Failed to create render event handle.";
                lastErrorCode = GetLastError();
                return false;
            }
            hr = pRenderClient->SetEventHandle(hRenderEvent);
            if (FAILED(hr)) {
                lastErrorMessage = "SetEventHandle failed.";
                lastErrorCode = hr;
                return false;
            }
        }

        hr = pRenderClient->GetBufferSize(&actualBufferSizeFrames);
        if (FAILED(hr)) return false;

        REFERENCE_TIME latencyHns = 0;
        if (SUCCEEDED(pRenderClient->GetStreamLatency(&latencyHns)) && latencyHns > 0) {
            reportedLatencyMs = static_cast<double>(latencyHns) / 10000.0;
        } else {
            reportedLatencyMs = (static_cast<double>(actualBufferSizeFrames) / actualSampleRate) * 1000.0;
        }
        actualPeriodicityMs = static_cast<double>(defaultPeriodHns) / 10000.0;

        hr = pRenderClient->GetService(
            __uuidof(IAudioRenderClient),
            reinterpret_cast<void**>(&pRenderService));
        if (FAILED(hr) || !pRenderService) {
            lastErrorMessage = "GetService(IAudioRenderClient) failed.";
            lastErrorCode = hr;
            return false;
        }

        return true;
    }

    bool setupRenderExclusive(REFERENCE_TIME minPeriodHns) {
        // Probe supported candidate formats
        std::vector<WORD> bitsList = {32, 24, 16};
        WAVEFORMATEXTENSIBLE wfe{};
        wfe.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        wfe.Format.nChannels = static_cast<WORD>(requestedChannels > 0 ? requestedChannels : 2);
        wfe.Format.nSamplesPerSec = static_cast<DWORD>(requestedSampleRate > 0.0 ? requestedSampleRate : 48000.0);
        wfe.Format.cbSize = 22;

        bool formatFound = false;
        for (WORD bits : bitsList) {
            wfe.Format.wBitsPerSample = bits;
            wfe.Format.nBlockAlign = (wfe.Format.nChannels * bits) / 8;
            wfe.Format.nAvgBytesPerSec = wfe.Format.nSamplesPerSec * wfe.Format.nBlockAlign;
            wfe.Samples.wValidBitsPerSample = bits;
            wfe.dwChannelMask = (wfe.Format.nChannels == 1) ? SPEAKER_FRONT_CENTER : (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);
            wfe.SubFormat = (bits == 32) ? kSubtypeIEEEFloat : kSubtypePCM;

            HRESULT hrSupp = pRenderClient->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, reinterpret_cast<WAVEFORMATEX*>(&wfe), nullptr);
            if (hrSupp == S_OK) {
                formatFound = true;
                break;
            }
        }

        if (!formatFound) {
            lastErrorMessage = "Requested format unsupported in Exclusive mode (AUDCLNT_E_UNSUPPORTED_FORMAT).";
            lastErrorCode = AUDCLNT_E_UNSUPPORTED_FORMAT;
            return false;
        }

        if (pRenderFormat) {
            CoTaskMemFree(pRenderFormat);
            pRenderFormat = nullptr;
        }
        pRenderFormat = reinterpret_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(WAVEFORMATEXTENSIBLE)));
        std::memcpy(pRenderFormat, &wfe, sizeof(WAVEFORMATEXTENSIBLE));

        actualSampleRate = static_cast<double>(pRenderFormat->nSamplesPerSec);
        actualChannels = static_cast<uint32_t>(pRenderFormat->nChannels);
        bitDepth = static_cast<uint32_t>(pRenderFormat->wBitsPerSample);
        renderFormatType = determineFormat(pRenderFormat);

        REFERENCE_TIME bufferDuration = static_cast<REFERENCE_TIME>(
            (static_cast<double>(requestedBlockSize) / actualSampleRate) * 10000000.0);
        if (bufferDuration < minPeriodHns) bufferDuration = minPeriodHns;

        DWORD streamFlags = eventDriven ? AUDCLNT_STREAMFLAGS_EVENTCALLBACK : 0;
        REFERENCE_TIME periodicity = eventDriven ? bufferDuration : 0;

        HRESULT hr = pRenderClient->Initialize(
            AUDCLNT_SHAREMODE_EXCLUSIVE,
            streamFlags,
            bufferDuration,
            periodicity,
            pRenderFormat,
            nullptr);

        if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
            UINT32 alignedFrames = 0;
            pRenderClient->GetBufferSize(&alignedFrames);
            bufferDuration = static_cast<REFERENCE_TIME>((static_cast<double>(alignedFrames) / actualSampleRate) * 10000000.0);
            periodicity = eventDriven ? bufferDuration : 0;

            safeRelease(pRenderClient);
            pRenderDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&pRenderClient));
            hr = pRenderClient->Initialize(
                AUDCLNT_SHAREMODE_EXCLUSIVE,
                streamFlags,
                bufferDuration,
                periodicity,
                pRenderFormat,
                nullptr);
        }

        if (FAILED(hr)) {
            if (hr == AUDCLNT_E_DEVICE_IN_USE) {
                lastErrorMessage = "Audio endpoint in use exclusively by another application (AUDCLNT_E_DEVICE_IN_USE).";
            } else if (hr == AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED) {
                lastErrorMessage = "Exclusive mode disabled in Windows Sound Properties (AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED).";
            } else {
                lastErrorMessage = "IAudioClient::Initialize (Exclusive) failed.";
            }
            lastErrorCode = hr;
            return false;
        }

        if (eventDriven) {
            hRenderEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            if (!hRenderEvent) return false;
            hr = pRenderClient->SetEventHandle(hRenderEvent);
            if (FAILED(hr)) return false;
        }

        hr = pRenderClient->GetBufferSize(&actualBufferSizeFrames);
        if (FAILED(hr)) return false;

        reportedLatencyMs = (static_cast<double>(actualBufferSizeFrames) / actualSampleRate) * 1000.0;
        actualPeriodicityMs = static_cast<double>(bufferDuration) / 10000.0;

        hr = pRenderClient->GetService(
            __uuidof(IAudioRenderClient),
            reinterpret_cast<void**>(&pRenderService));
        if (FAILED(hr) || !pRenderService) return false;

        return true;
    }

    bool setupCaptureClient() {
        if (streamMode == WasapiStreamMode::PlaybackOnly && sourceType == AudioSourceType::FilePlayback) {
            return true;
        }

        // Loopback in Exclusive Mode is strictly UNSUPPORTED by Windows Core Audio
        if (sourceType == AudioSourceType::SystemLoopback && shareMode == WasapiShareMode::Exclusive) {
            lastErrorMessage = "WASAPI Loopback capture is UNSUPPORTED in Exclusive Mode (Windows Core Audio architectural restriction). Loopback requires Shared Mode.";
            lastErrorCode = 0x88890018; // AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED
            lastErrorCode = 0x8889000E; // AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED
            return false;
        }

        if (!initDeviceEnumerator()) return false;

        HRESULT hr = S_OK;
        if (sourceType == AudioSourceType::SystemLoopback) {
            // System Loopback captures an eRender endpoint!
            if (!selectedLoopbackDeviceId.empty()) {
                std::wstring wId = utf8ToWide(selectedLoopbackDeviceId);
                hr = pEnumerator->GetDevice(wId.c_str(), &pCaptureDevice);
            } else {
                hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pCaptureDevice);
            }
        } else {
            // Audio Interface captures an eCapture endpoint
            if (!selectedCaptureDeviceId.empty()) {
                std::wstring wId = utf8ToWide(selectedCaptureDeviceId);
                hr = pEnumerator->GetDevice(wId.c_str(), &pCaptureDevice);
            } else {
                hr = pEnumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &pCaptureDevice);
            }
        }

        if (FAILED(hr) || !pCaptureDevice) {
            if (sourceType != AudioSourceType::FilePlayback) {
                lastErrorMessage = (sourceType == AudioSourceType::SystemLoopback)
                    ? "Selected loopback render endpoint could not be opened."
                    : "Selected capture device endpoint could not be opened.";
                lastErrorCode = static_cast<uint32_t>(hr);
                return false;
            }
            std::cerr << "[WASAPI] Notice: No capture device available. Continuing with PlaybackOnly.\n";
            streamMode = WasapiStreamMode::PlaybackOnly;
            return true;
        }

        IPropertyStore* pProps = nullptr;
        if (SUCCEEDED(pCaptureDevice->OpenPropertyStore(STGM_READ, &pProps)) && pProps) {
            PROPVARIANT varName;
            PropVariantInit(&varName);
            if (SUCCEEDED(pProps->GetValue(PKEY_Device_FriendlyName, &varName)) && varName.pwszVal) {
                if (sourceType == AudioSourceType::SystemLoopback) {
                    activeLoopbackDeviceName = wideToUtf8(varName.pwszVal);
                } else {
                    activeCaptureDeviceName = wideToUtf8(varName.pwszVal);
                }
            }
            PropVariantClear(&varName);
            safeRelease(pProps);
        }

        hr = pCaptureDevice->Activate(
            __uuidof(IAudioClient),
            CLSCTX_ALL,
            nullptr,
            reinterpret_cast<void**>(&pCaptureClient));
        if (FAILED(hr) || !pCaptureClient) {
            lastErrorMessage = "Failed to activate IAudioClient on capture endpoint.";
            lastErrorCode = static_cast<uint32_t>(hr);
            return false;
        }

        if (pCaptureFormat) {
            CoTaskMemFree(pCaptureFormat);
            pCaptureFormat = nullptr;
        }
        hr = pCaptureClient->GetMixFormat(&pCaptureFormat);
        if (FAILED(hr) || !pCaptureFormat) {
            lastErrorMessage = "Failed to get mix format on capture endpoint.";
            lastErrorCode = static_cast<uint32_t>(hr);
            return false;
        }

        captureFormatType = determineFormat(pCaptureFormat);

        REFERENCE_TIME bufferDuration = static_cast<REFERENCE_TIME>(
            (static_cast<double>(actualBufferSizeFrames) / actualSampleRate) * 10000000.0);

        DWORD streamFlags = 0;
        if (sourceType == AudioSourceType::SystemLoopback) {
            streamFlags |= AUDCLNT_STREAMFLAGS_LOOPBACK;
        }
        if (eventDriven) {
            streamFlags |= AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
        }

        hr = pCaptureClient->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            streamFlags,
            bufferDuration,
            0,
            pCaptureFormat,
            nullptr);
        if (FAILED(hr)) {
            lastErrorMessage = "Failed to initialize IAudioClient in capture/loopback mode.";
            lastErrorCode = static_cast<uint32_t>(hr);
            return false;
        }

        if (eventDriven) {
            hCaptureEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            if (hCaptureEvent) {
                pCaptureClient->SetEventHandle(hCaptureEvent);
            }
        }

        hr = pCaptureClient->GetService(
            __uuidof(IAudioCaptureClient),
            reinterpret_cast<void**>(&pCaptureService));
        if (FAILED(hr) || !pCaptureService) {
            lastErrorMessage = "Failed to obtain IAudioCaptureClient.";
            lastErrorCode = static_cast<uint32_t>(hr);
            return false;
        }

        // Configure streaming SRC if capture sample rate != engine sample rate
        double capRate = static_cast<double>(pCaptureFormat->nSamplesPerSec);
        if (std::abs(capRate - actualSampleRate) > 1.0) {
            captureSrc.configure(capRate, actualSampleRate, 2, SrcQuality::SincStandard);
        } else {
            captureSrc.configure(actualSampleRate, actualSampleRate, 2, SrcQuality::SincStandard);
        }

        return true;
    }

    uint32_t popFromRingBufferToRender(BYTE* pRenderData, uint32_t frames) {
        switch (renderFormatType) {
            case SampleFormat::Float32:
                return renderRingBuffer.popToInterleavedFloat32(reinterpret_cast<float*>(pRenderData), frames);
            case SampleFormat::Int16:
                return renderRingBuffer.popToInterleavedInt16(reinterpret_cast<int16_t*>(pRenderData), frames);
            case SampleFormat::Int24In32:
                return renderRingBuffer.popToInterleavedInt24In32(reinterpret_cast<int32_t*>(pRenderData), frames);
            case SampleFormat::Int24Packed:
                return renderRingBuffer.popToInterleavedInt24Packed(reinterpret_cast<uint8_t*>(pRenderData), frames);
            case SampleFormat::Int32:
                return renderRingBuffer.popToInterleavedInt32(reinterpret_cast<int32_t*>(pRenderData), frames);
            default:
                std::memset(pRenderData, 0, frames * pRenderFormat->nBlockAlign);
                return frames;
        }
    }

    void processCapturePacket(const BYTE* pCapData, uint32_t frames, DWORD flags, float gain) {
        if (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) {
            bufferDiscontinuities.fetch_add(1, std::memory_order_relaxed);
        }
        uint32_t srcCh = pCaptureFormat ? pCaptureFormat->nChannels : actualChannels;
        double capRate = pCaptureFormat ? static_cast<double>(pCaptureFormat->nSamplesPerSec) : actualSampleRate;

        if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
            uint32_t outFrames = (captureSrc.getStatus() == SrcStatus::Active)
                ? SampleRateConverter::calculateOutputFrames(frames, capRate, actualSampleRate)
                : frames;
            captureRingBuffer.writeSilence(outFrames);
            totalFramesCaptured.fetch_add(frames, std::memory_order_relaxed);
            return;
        }

        totalFramesCaptured.fetch_add(frames, std::memory_order_relaxed);

        if (capturePreMapCh0.size() < frames) {
            capturePreMapCh0.resize(frames * 2, 0.0f);
            capturePreMapCh1.resize(frames * 2, 0.0f);
        }

        decodeAndMapToStereo(pCapData, frames, srcCh, captureFormatType, gain, capturePreMapCh0.data(), capturePreMapCh1.data());

        if (captureSrc.getStatus() == SrcStatus::Active) {
            uint32_t expectedOut = SampleRateConverter::calculateOutputFrames(frames, capRate, actualSampleRate);
            if (captureSrcOutCh0.size() < expectedOut + 64) {
                captureSrcOutCh0.resize((expectedOut + 64) * 2, 0.0f);
                captureSrcOutCh1.resize((expectedOut + 64) * 2, 0.0f);
            }

            const float* inPtrs[2] = { capturePreMapCh0.data(), capturePreMapCh1.data() };
            float* outPtrs[2] = { captureSrcOutCh0.data(), captureSrcOutCh1.data() };
            uint32_t consumed = 0;
            uint32_t produced = captureSrc.processStreaming(inPtrs, frames, consumed, outPtrs, expectedOut);

            uint32_t written = captureRingBuffer.writePlanarStereo(captureSrcOutCh0.data(), captureSrcOutCh1.data(), produced);
            if (written < produced) {
                captureOverruns.fetch_add(1, std::memory_order_relaxed);
            }
        } else {
            uint32_t written = captureRingBuffer.writePlanarStereo(capturePreMapCh0.data(), capturePreMapCh1.data(), frames);
            if (written < frames) {
                captureOverruns.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    void runAudioLoop() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        DWORD taskIndex = 0;
        HANDLE hAvrt = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
        if (!hAvrt) {
            hAvrt = AvSetMmThreadCharacteristicsW(L"Audio", &taskIndex);
        }
        if (hAvrt) {
            mmcssRegistered = true;
            mmcssErrorCode = 0;
        } else {
            mmcssRegistered = false;
            mmcssErrorCode = GetLastError();
        }

        uint32_t blockSz = activeEngineBlockSize;

        // Pre-roll: Fill render buffer to initial hardware capacity
        while (renderRingBuffer.availableRead() < actualBufferSizeFrames) {
            if (pCaptureService && streamMode != WasapiStreamMode::PlaybackOnly) {
                captureRingBuffer.readPlanar(engineBlockIn, blockSz);
            } else {
                engineBlockIn.clear(blockSz);
            }

            if (engine) {
                engine->process(engineBlockIn, engineBlockOut, blockSz);
            } else {
                engineBlockOut.clear(blockSz);
            }
            renderRingBuffer.writePlanar(engineBlockOut, blockSz);
            totalFramesProcessed.fetch_add(blockSz, std::memory_order_relaxed);
        }

        // Prime hardware buffer with initial silence
        BYTE* pInitData = nullptr;
        if (SUCCEEDED(pRenderService->GetBuffer(actualBufferSizeFrames, &pInitData))) {
            pRenderService->ReleaseBuffer(actualBufferSizeFrames, AUDCLNT_BUFFERFLAGS_SILENT);
        }

        pRenderClient->Start();
        if (pCaptureClient && streamMode != WasapiStreamMode::PlaybackOnly) {
            pCaptureClient->Start();
        }

        auto lastWakeTime = std::chrono::steady_clock::now();
        const double expectedPeriodUs = (static_cast<double>(activeEngineBlockSize) / actualSampleRate) * 1e6;

        while (running.load(std::memory_order_relaxed)) {
            DWORD waitRes = WaitForSingleObject(hRenderEvent, 200);
            if (!running.load(std::memory_order_relaxed)) break;

            if (waitRes == WAIT_OBJECT_0) {
                auto tWake = std::chrono::steady_clock::now();
                currentProcessorId.store(static_cast<int32_t>(GetCurrentProcessorNumber()), std::memory_order_relaxed);
                totalCallbacks.fetch_add(1, std::memory_order_relaxed);

                uint64_t actualIntervalUs = std::chrono::duration_cast<std::chrono::microseconds>(tWake - lastWakeTime).count();
                uint64_t jitterUs = (actualIntervalUs > expectedPeriodUs)
                    ? static_cast<uint64_t>(actualIntervalUs - expectedPeriodUs)
                    : static_cast<uint64_t>(expectedPeriodUs - actualIntervalUs);
                lastWakeTime = tWake;

                UINT32 padding = 0;
                HRESULT hrPad = pRenderClient->GetCurrentPadding(&padding);
                if (FAILED(hrPad)) {
                    if (hrPad == AUDCLNT_E_DEVICE_INVALIDATED) {
                        deviceInvalidated.store(true, std::memory_order_release);
                        deviceInvalidationCount.fetch_add(1, std::memory_order_relaxed);
                        lastErrorMessage = "Audio device was unplugged or invalidated by system (AUDCLNT_E_DEVICE_INVALIDATED).";
                        break;
                    }
                    renderErrors.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }

                UINT32 framesNeeded = actualBufferSizeFrames - padding;
                if (framesNeeded == 0) continue;
                totalFramesRequested.fetch_add(framesNeeded, std::memory_order_relaxed);

                BYTE* pRenderData = nullptr;
                HRESULT hr = pRenderService->GetBuffer(framesNeeded, &pRenderData);
                if (FAILED(hr) || !pRenderData) {
                    if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
                        deviceInvalidated.store(true, std::memory_order_release);
                        deviceInvalidationCount.fetch_add(1, std::memory_order_relaxed);
                        lastErrorMessage = "Render device invalidated during GetBuffer.";
                        break;
                    }
                    getBufferFailures.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }

                // Step 1: Read all available capture packets
                auto tCapStart = std::chrono::steady_clock::now();
                if (pCaptureService && streamMode != WasapiStreamMode::PlaybackOnly) {
                    UINT32 packetFrames = 0;
                    while (SUCCEEDED(pCaptureService->GetNextPacketSize(&packetFrames)) && packetFrames > 0) {
                        BYTE* pCapData = nullptr;
                        UINT32 framesRead = 0;
                        DWORD flags = 0;
                        if (SUCCEEDED(pCaptureService->GetBuffer(&pCapData, &framesRead, &flags, nullptr, nullptr)) && pCapData) {
                            float gain = safeFeedbackGuard ? maxLinearInputGain : 1.0f;
                            processCapturePacket(pCapData, framesRead, flags, gain);
                            pCaptureService->ReleaseBuffer(framesRead);
                        } else {
                            break;
                        }
                    }
                }
                auto tCapEnd = std::chrono::steady_clock::now();
                uint64_t capDurationUs = std::chrono::duration_cast<std::chrono::microseconds>(tCapEnd - tCapStart).count();

                // Step 2: Adapter Loop - Run Engine in EXACT activeEngineBlockSize chunks
                auto tDspStart = std::chrono::steady_clock::now();
                uint64_t dspDurationUs = 0;

                while (renderRingBuffer.availableRead() < framesNeeded) {
                    if (pCaptureService && streamMode != WasapiStreamMode::PlaybackOnly) {
                        captureRingBuffer.readPlanar(engineBlockIn, blockSz);
                    } else {
                        engineBlockIn.clear(blockSz);
                    }

                    if (engine) {
                        engine->process(engineBlockIn, engineBlockOut, blockSz);
                    } else {
                        engineBlockOut.clear(blockSz);
                    }

                    renderRingBuffer.writePlanar(engineBlockOut, blockSz);
                    totalFramesProcessed.fetch_add(blockSz, std::memory_order_relaxed);
                }

                auto tDspEnd = std::chrono::steady_clock::now();
                dspDurationUs = std::chrono::duration_cast<std::chrono::microseconds>(tDspEnd - tDspStart).count();

                // Step 3: Pop exactly framesNeeded from renderRingBuffer to pRenderData
                auto tRenStart = std::chrono::steady_clock::now();
                uint32_t framesPopped = popFromRingBufferToRender(pRenderData, framesNeeded);
                if (framesPopped < framesNeeded) {
                    renderUnderruns.fetch_add(1, std::memory_order_relaxed);
                }

                // Sample Integrity verification (NaN / Inf protection)
                if (renderFormatType == SampleFormat::Float32 && pRenderData) {
                    float* pF = reinterpret_cast<float*>(pRenderData);
                    uint32_t totalSamples = framesNeeded * actualChannels;
                    for (uint32_t s = 0; s < totalSamples; ++s) {
                        float v = pF[s];
                        if (std::isnan(v)) {
                            nanCount.fetch_add(1, std::memory_order_relaxed);
                            pF[s] = 0.0f;
                        } else if (std::isinf(v)) {
                            infCount.fetch_add(1, std::memory_order_relaxed);
                            pF[s] = (v > 0.0f) ? 1.0f : -1.0f;
                        }
                    }
                }

                hr = pRenderService->ReleaseBuffer(framesNeeded, 0);
                if (FAILED(hr)) {
                    releaseBufferFailures.fetch_add(1, std::memory_order_relaxed);
                } else {
                    totalFramesSubmitted.fetch_add(framesNeeded, std::memory_order_relaxed);
                }

                if (framesPopped != framesNeeded) {
                    frameCountMismatches.fetch_add(1, std::memory_order_relaxed);
                }

                auto tEnd = std::chrono::steady_clock::now();
                uint64_t renDurationUs = std::chrono::duration_cast<std::chrono::microseconds>(tEnd - tRenStart).count();
                uint64_t totalCallbackUs = std::chrono::duration_cast<std::chrono::microseconds>(tEnd - tWake).count();

                double deadlineMs = (static_cast<double>(framesNeeded) / actualSampleRate) * 1000.0;
                recordTiming(totalCallbackUs, dspDurationUs, renDurationUs, capDurationUs, jitterUs, deadlineMs);
            }
        }

        pRenderClient->Stop();
        if (pCaptureClient) {
            pCaptureClient->Stop();
        }

        if (hAvrt) {
            AvRevertMmThreadCharacteristics(hAvrt);
        }

        CoUninitialize();
    }

    bool startStreaming() {
        if (!initialized.load(std::memory_order_relaxed)) return false;
        if (running.load(std::memory_order_relaxed)) return true;

        if (engine) {
            if (engine->getBlockSize() > 0) {
                activeEngineBlockSize = engine->getBlockSize();
            }
            if (std::abs(engine->getSampleRate() - actualSampleRate) > 1.0) {
                sampleRateMismatches.fetch_add(1, std::memory_order_relaxed);
            }
            if (engine->getChannelCount() != actualChannels) {
                channelMismatches.fetch_add(1, std::memory_order_relaxed);
            }
        }

        engineBlockIn.resize(actualChannels, activeEngineBlockSize);
        engineBlockOut.resize(actualChannels, activeEngineBlockSize);

        renderRingBuffer.clear();
        captureRingBuffer.clear();

        resetDiagnostics();
        deviceInvalidated.store(false, std::memory_order_relaxed);

        running.store(true, std::memory_order_release);
        audioThread = std::thread([this]() {
#if defined(_WIN32)
            HANDLE curThread = GetCurrentThread();
            HANDLE curProc = GetCurrentProcess();
            DuplicateHandle(curProc, curThread, curProc, &hAudioThreadNative,
                            THREAD_QUERY_LIMITED_INFORMATION, FALSE, 0);
#endif
            runAudioLoop();
#if defined(_WIN32)
            if (hAudioThreadNative) {
                CloseHandle(hAudioThreadNative);
                hAudioThreadNative = nullptr;
            }
#endif
            currentProcessorId.store(-1, std::memory_order_relaxed);
        });
        return true;
    }

    void stopStreaming() {
        if (running.load(std::memory_order_relaxed)) {
            running.store(false, std::memory_order_release);
            if (hRenderEvent) {
                SetEvent(hRenderEvent);
            }
            if (audioThread.joinable()) {
                audioThread.join();
            }
            currentProcessorId.store(-1, std::memory_order_relaxed);
        }
    }
};

// ==============================================================================
// WasapiBackend Public API Implementation
// ==============================================================================

WasapiBackend::WasapiBackend()
    : impl_(std::make_unique<Impl>())
{
}

WasapiBackend::~WasapiBackend() = default;

WasapiBackend::WasapiBackend(WasapiBackend&&) noexcept = default;
WasapiBackend& WasapiBackend::operator=(WasapiBackend&&) noexcept = default;

bool WasapiBackend::initialize(
    double sampleRate,
    uint32_t blockSize,
    uint32_t channels)
{
    impl_->cleanup();

    impl_->requestedSampleRate = (sampleRate > 0.0) ? sampleRate : 48000.0;
    impl_->requestedBlockSize = (blockSize > 0) ? blockSize : 128;
    impl_->requestedChannels = (channels > 0) ? channels : 2;

    if (!impl_->setupRenderClient()) {
        std::cerr << "[WASAPI] Render client initialization failed: " << impl_->lastErrorMessage << "\n";
        return false;
    }

    if (!impl_->setupCaptureClient()) {
        if (impl_->sourceType != AudioSourceType::FilePlayback) {
            // A live source that cannot be opened must NOT degrade into a silent
            // "streaming" session. Tear down the partially built render path.
            if (impl_->lastErrorMessage.empty()) {
                impl_->lastErrorMessage = "Live capture source could not be initialized.";
            }
            impl_->lastErrorMessage = "Live source init failed: " + impl_->lastErrorMessage;
            std::cerr << "[WASAPI] " << impl_->lastErrorMessage << "\n";
            std::string msg = impl_->lastErrorMessage;
            uint32_t code = impl_->lastErrorCode;
            impl_->cleanup();
            impl_->lastErrorMessage = msg;
            impl_->lastErrorCode = code;
            return false;
        }
        std::cerr << "[WASAPI] Notice: Capture setup bypassed or defaulted.\n";
    }

    impl_->renderRingBuffer.configure(impl_->actualChannels, Impl::kRingBufferCapacity);
    impl_->captureRingBuffer.configure(impl_->actualChannels, Impl::kRingBufferCapacity);

    impl_->activeEngineBlockSize = impl_->requestedBlockSize;
    impl_->engineBlockIn.resize(impl_->actualChannels, impl_->activeEngineBlockSize);
    impl_->engineBlockOut.resize(impl_->actualChannels, impl_->activeEngineBlockSize);

    // Preallocate capture adapter & SRC planar scratch buffers
    impl_->capturePreMapCh0.assign(8192, 0.0f);
    impl_->capturePreMapCh1.assign(8192, 0.0f);
    impl_->captureSrcOutCh0.assign(8192, 0.0f);
    impl_->captureSrcOutCh1.assign(8192, 0.0f);

    impl_->initialized.store(true, std::memory_order_release);
    return true;
}

bool WasapiBackend::start() {
    if (!impl_->initialized.load(std::memory_order_relaxed)) {
        if (!initialize()) {
            return false;
        }
    }
    impl_->engine = engine_;
    return impl_->startStreaming();
}

void WasapiBackend::stop() {
    impl_->stopStreaming();
}

bool WasapiBackend::isRunning() const {
    return impl_->running.load(std::memory_order_relaxed);
}

double WasapiBackend::getSampleRate() const {
    return impl_->actualSampleRate;
}

uint32_t WasapiBackend::getBlockSize() const {
    return impl_->activeEngineBlockSize;
}

uint32_t WasapiBackend::getChannels() const {
    return impl_->actualChannels;
}

std::vector<WasapiDeviceInfo> WasapiBackend::enumerateDevices(bool capture) {
    std::vector<WasapiDeviceInfo> devices;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool shouldUninit = SUCCEEDED(hr);

    IMMDeviceEnumerator* pEnum = nullptr;
    hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(&pEnum));

    if (SUCCEEDED(hr) && pEnum) {
        EDataFlow flow = capture ? eCapture : eRender;
        IMMDeviceCollection* pCollection = nullptr;
        hr = pEnum->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &pCollection);

        if (SUCCEEDED(hr) && pCollection) {
            UINT count = 0;
            pCollection->GetCount(&count);

            std::string defaultId;
            IMMDevice* pDefDev = nullptr;
            if (SUCCEEDED(pEnum->GetDefaultAudioEndpoint(flow, eConsole, &pDefDev)) && pDefDev) {
                LPWSTR pDefId = nullptr;
                if (SUCCEEDED(pDefDev->GetId(&pDefId)) && pDefId) {
                    defaultId = wideToUtf8(pDefId);
                    CoTaskMemFree(pDefId);
                }
                pDefDev->Release();
            }

            for (UINT i = 0; i < count; ++i) {
                IMMDevice* pDev = nullptr;
                if (SUCCEEDED(pCollection->Item(i, &pDev)) && pDev) {
                    WasapiDeviceInfo info;
                    info.isCapture = capture;
                    info.isAvailable = true;

                    LPWSTR pStrId = nullptr;
                    if (SUCCEEDED(pDev->GetId(&pStrId)) && pStrId) {
                        info.id = wideToUtf8(pStrId);
                        CoTaskMemFree(pStrId);
                    }

                    if (info.id == defaultId) {
                        info.isDefault = true;
                    }

                    IPropertyStore* pProps = nullptr;
                    if (SUCCEEDED(pDev->OpenPropertyStore(STGM_READ, &pProps)) && pProps) {
                        PROPVARIANT varName;
                        PropVariantInit(&varName);
                        if (SUCCEEDED(pProps->GetValue(PKEY_Device_FriendlyName, &varName)) && varName.pwszVal) {
                            info.name = wideToUtf8(varName.pwszVal);
                            info.friendlyName = info.name;
                        }
                        PropVariantClear(&varName);
                        pProps->Release();
                    }

                    IAudioClient* pAudioClient = nullptr;
                    if (SUCCEEDED(pDev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&pAudioClient))) && pAudioClient) {
                        WAVEFORMATEX* pFormat = nullptr;
                        if (SUCCEEDED(pAudioClient->GetMixFormat(&pFormat)) && pFormat) {
                            info.channels = pFormat->nChannels;
                            info.defaultSampleRate = pFormat->nSamplesPerSec;

                            // Probe common sample rates
                            std::vector<uint32_t> testRates = {44100, 48000, 88200, 96000, 176400, 192000};
                            WAVEFORMATEX testFmt = *pFormat;
                            for (uint32_t r : testRates) {
                                testFmt.nSamplesPerSec = r;
                                testFmt.nAvgBytesPerSec = r * testFmt.nBlockAlign;
                                WAVEFORMATEX* pClosest = nullptr;
                                if (pAudioClient->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, &testFmt, &pClosest) == S_OK) {
                                    info.supportedSampleRates.push_back(r);
                                }
                                if (pClosest) CoTaskMemFree(pClosest);
                            }
                            if (info.supportedSampleRates.empty()) {
                                info.supportedSampleRates.push_back(info.defaultSampleRate);
                            }

                            CoTaskMemFree(pFormat);
                        }

                        REFERENCE_TIME defPer = 0, minPer = 0;
                        if (SUCCEEDED(pAudioClient->GetDevicePeriod(&defPer, &minPer))) {
                            info.defaultDevicePeriodMs = static_cast<double>(defPer) / 10000.0;
                            info.minDevicePeriodMs = static_cast<double>(minPer) / 10000.0;
                        }

                        pAudioClient->Release();
                    }

                    devices.push_back(std::move(info));
                    pDev->Release();
                }
            }
            pCollection->Release();
        }
        pEnum->Release();
    }

    if (shouldUninit) {
        CoUninitialize();
    }

    return devices;
}

bool WasapiBackend::setPlaybackDevice(const std::string& deviceId) {
    if (impl_->running.load(std::memory_order_relaxed)) return false;
    impl_->selectedPlaybackDeviceId = deviceId;
    return true;
}

bool WasapiBackend::setCaptureDevice(const std::string& deviceId) {
    if (impl_->running.load(std::memory_order_relaxed)) return false;
    impl_->selectedCaptureDeviceId = deviceId;
    return true;
}

void WasapiBackend::setStreamMode(WasapiStreamMode mode) {
    if (impl_->running.load(std::memory_order_relaxed)) return;
    impl_->streamMode = mode;
}

WasapiStreamMode WasapiBackend::getStreamMode() const {
    return impl_->streamMode;
}

void WasapiBackend::setShareMode(WasapiShareMode mode) {
    if (impl_->running.load(std::memory_order_relaxed)) return;
    impl_->shareMode = mode;
}

WasapiShareMode WasapiBackend::getShareMode() const {
    return impl_->shareMode;
}

void WasapiBackend::setEventDriven(bool enabled) {
    if (impl_->running.load(std::memory_order_relaxed)) return;
    impl_->eventDriven = enabled;
}

bool WasapiBackend::isEventDriven() const {
    return impl_->eventDriven;
}

bool WasapiBackend::setRequestedSampleRate(double rate) {
    if (impl_->running.load(std::memory_order_relaxed)) return false;
    if (rate <= 0.0) return false;
    impl_->requestedSampleRate = rate;
    return true;
}

double WasapiBackend::getRequestedSampleRate() const {
    return impl_->requestedSampleRate;
}

bool WasapiBackend::setRequestedBlockSize(uint32_t frames) {
    if (impl_->running.load(std::memory_order_relaxed)) return false;
    if (frames == 0) return false;
    impl_->requestedBlockSize = frames;
    return true;
}

uint32_t WasapiBackend::getRequestedBlockSize() const {
    return impl_->requestedBlockSize;
}

void WasapiBackend::setAudioSourceType(AudioSourceType type) {
    if (impl_->running.load(std::memory_order_relaxed)) return;
    impl_->sourceType = type;
    if (type == AudioSourceType::FilePlayback) {
        impl_->streamMode = WasapiStreamMode::PlaybackOnly;
    } else {
        impl_->streamMode = WasapiStreamMode::Duplex;
    }
}

AudioSourceType WasapiBackend::getAudioSourceType() const {
    return impl_->sourceType;
}

std::vector<WasapiDeviceInfo> WasapiBackend::enumerateLoopbackEndpoints() {
    auto list = enumerateDevices(false);
    for (auto& item : list) {
        item.isLoopback = true;
    }
    return list;
}

bool WasapiBackend::setLoopbackDevice(const std::string& deviceId) {
    if (impl_->running.load(std::memory_order_relaxed)) return false;
    impl_->selectedLoopbackDeviceId = deviceId;
    return true;
}

std::string WasapiBackend::getLoopbackDeviceId() const {
    return impl_->selectedLoopbackDeviceId;
}

std::string WasapiBackend::getActiveLoopbackDeviceName() const {
    return impl_->activeLoopbackDeviceName;
}

bool WasapiBackend::checkFeedbackRisk(std::string* outWarning) const {
    if (impl_->sourceType != AudioSourceType::SystemLoopback) {
        return false;
    }
    bool isMatch = false;
    if (impl_->selectedLoopbackDeviceId.empty() && impl_->selectedPlaybackDeviceId.empty()) {
        isMatch = true;
    } else if (!impl_->selectedLoopbackDeviceId.empty() && !impl_->selectedPlaybackDeviceId.empty() &&
               impl_->selectedLoopbackDeviceId == impl_->selectedPlaybackDeviceId) {
        isMatch = true;
    }
    if (isMatch) {
        if (outWarning) {
            *outWarning = "POTENTIAL FEEDBACK LOOP: System Loopback capture endpoint matches the active Playback Render endpoint! "
                          "Capturing and rendering to the same endpoint causes exponential feedback recursion. "
                          "Select an alternate playback device or confirm safe attenuation.";
        }
        return true;
    }
    return false;
}

void WasapiBackend::setChannelMappingMode(int mode) {
    impl_->channelMappingMode = mode;
}

int WasapiBackend::getChannelMappingMode() const {
    return impl_->channelMappingMode;
}

double WasapiBackend::getCaptureSampleRate() const {
    if (impl_->pCaptureFormat) {
        return static_cast<double>(impl_->pCaptureFormat->nSamplesPerSec);
    }
    return impl_->actualSampleRate;
}

uint32_t WasapiBackend::getCaptureChannels() const {
    if (impl_->pCaptureFormat) {
        return impl_->pCaptureFormat->nChannels;
    }
    return impl_->actualChannels;
}

uint32_t WasapiBackend::getCaptureBufferSizeFrames() const {
    return impl_->actualBufferSizeFrames;
}

double WasapiBackend::getClockDriftPpm() const {
    if (impl_->sourceType == AudioSourceType::FilePlayback || !impl_->pCaptureService) {
        return 0.0;
    }
    uint64_t capFrames = impl_->totalFramesCaptured.load(std::memory_order_relaxed);
    uint64_t renFrames = impl_->totalFramesSubmitted.load(std::memory_order_relaxed);
    if (renFrames < 4800) return 0.0;

    double capRate = (impl_->pCaptureFormat) ? static_cast<double>(impl_->pCaptureFormat->nSamplesPerSec) : impl_->actualSampleRate;
    double renRate = impl_->actualSampleRate;

    double expectedCapFrames = static_cast<double>(renFrames) * (capRate / renRate);
    if (expectedCapFrames <= 0.0) return 0.0;
    double driftFrames = static_cast<double>(capFrames) - expectedCapFrames;
    return (driftFrames / expectedCapFrames) * 1e6;
}

double WasapiBackend::getBufferOccupancyPercent() const {
    uint32_t avail = impl_->captureRingBuffer.availableRead();
    return (static_cast<double>(avail) / Impl::kRingBufferCapacity) * 100.0;
}

bool WasapiBackend::validateConfiguration(std::string* outDetails) {
    std::ostringstream ss;
    ss << "Config Validation: ";
    ss << "Source=";
    if (impl_->sourceType == AudioSourceType::FilePlayback) ss << "AudioFile";
    else if (impl_->sourceType == AudioSourceType::AudioInterface) ss << "AudioInterface";
    else ss << "SystemLoopback";

    ss << ", Mode=" << (impl_->shareMode == WasapiShareMode::Exclusive ? "Exclusive" : "Shared");
    ss << ", RequestedRate=" << impl_->requestedSampleRate << " Hz";
    ss << ", RequestedBlock=" << impl_->requestedBlockSize << " frames";

    if (impl_->sourceType == AudioSourceType::SystemLoopback && impl_->shareMode == WasapiShareMode::Exclusive) {
        ss << " -> UNSUPPORTED: WASAPI System Loopback is not supported in Exclusive Mode by Windows Core Audio.";
        if (outDetails) *outDetails = ss.str();
        return false;
    }

    if (impl_->shareMode == WasapiShareMode::Exclusive) {
        auto devs = enumerateDevices(false);
        if (devs.empty()) {
            ss << " -> FAILED: No output devices available.";
            if (outDetails) *outDetails = ss.str();
            return false;
        }
        ss << " -> OK (Hardware support verified during initialize)";
    } else {
        std::string warn;
        if (checkFeedbackRisk(&warn)) {
            ss << " -> WARNING: " << warn;
        } else {
            ss << " -> OK (Shared mode engine negotiation enabled)";
        }
    }

    if (outDetails) *outDetails = ss.str();
    return true;
}

bool WasapiBackend::validateConfiguration(double sampleRate, uint32_t blockSize, WasapiShareMode shareMode, std::string* outDetails) {
    setRequestedSampleRate(sampleRate);
    setRequestedBlockSize(blockSize);
    setShareMode(shareMode);
    return validateConfiguration(outDetails);
}

std::string WasapiBackend::getActivePlaybackDeviceName() const {
    return impl_->activePlaybackDeviceName;
}

std::string WasapiBackend::getActiveCaptureDeviceName() const {
    return impl_->activeCaptureDeviceName;
}

uint32_t WasapiBackend::getActualBufferSizeFrames() const {
    return impl_->actualBufferSizeFrames;
}

uint32_t WasapiBackend::getBitDepth() const {
    return impl_->bitDepth;
}

std::string WasapiBackend::getFormatDescription() const {
    switch (impl_->renderFormatType) {
        case SampleFormat::Float32:
            return "32-bit IEEE Float (Interleaved)";
        case SampleFormat::Int16:
            return "16-bit PCM Integer (Interleaved)";
        case SampleFormat::Int24In32:
            return "24-bit PCM in 32-bit container (Interleaved)";
        case SampleFormat::Int24Packed:
            return "24-bit PCM packed (Interleaved)";
        case SampleFormat::Int32:
            return "32-bit PCM Integer (Interleaved)";
        default:
            return "Unknown / Unsupported";
    }
}

double WasapiBackend::getActualPeriodicityMs() const {
    return impl_->actualPeriodicityMs;
}

double WasapiBackend::getReportedLatencyMs() const {
    return impl_->reportedLatencyMs;
}

double WasapiBackend::getNominalBufferDurationMs() const {
    if (impl_->actualSampleRate <= 0.0) return 0.0;
    return (static_cast<double>(impl_->requestedBlockSize) / impl_->actualSampleRate) * 1000.0;
}

double WasapiBackend::getEstimatedLatencyMs() const {
    if (impl_->actualSampleRate <= 0.0) return 0.0;
    return (static_cast<double>(impl_->actualBufferSizeFrames) / impl_->actualSampleRate) * 1000.0;
}

bool WasapiBackend::isDeviceInvalidated() const {
    return impl_->deviceInvalidated.load(std::memory_order_relaxed);
}

std::string WasapiBackend::getLastErrorMessage() const {
    return impl_->lastErrorMessage;
}

uint32_t WasapiBackend::getLastErrorCode() const {
    return impl_->lastErrorCode;
}

void* WasapiBackend::getAudioThreadHandle() const {
    return impl_->hAudioThreadNative;
}

int32_t WasapiBackend::getCurrentProcessorNumber() const {
    return impl_->currentProcessorId.load(std::memory_order_relaxed);
}

void WasapiBackend::setSafeFeedbackGuard(bool enabled, float maxLinearGain) {
    impl_->safeFeedbackGuard = enabled;
    impl_->maxLinearInputGain = std::clamp(maxLinearGain, 0.0f, 1.0f);
}

bool WasapiBackend::isSafeFeedbackGuardEnabled() const {
    return impl_->safeFeedbackGuard;
}

void WasapiBackend::setDeviceChangeCallback(DeviceChangeCallback cb) {
    std::lock_guard<std::mutex> lock(impl_->callbackMutex);
    impl_->deviceChangeCallback = std::move(cb);
}

WasapiBackend::Diagnostics WasapiBackend::getDiagnostics() const {
    Diagnostics d;
    d.totalCallbacks = impl_->totalCallbacks.load(std::memory_order_relaxed);
    d.totalFramesRequested = impl_->totalFramesRequested.load(std::memory_order_relaxed);
    d.totalFramesProcessed = impl_->totalFramesProcessed.load(std::memory_order_relaxed);
    d.totalFramesSubmitted = impl_->totalFramesSubmitted.load(std::memory_order_relaxed);
    d.frameCountMismatches = impl_->frameCountMismatches.load(std::memory_order_relaxed);

    d.renderUnderruns = impl_->renderUnderruns.load(std::memory_order_relaxed);
    d.renderErrors = impl_->renderErrors.load(std::memory_order_relaxed);
    d.captureOverruns = impl_->captureOverruns.load(std::memory_order_relaxed);
    d.captureErrors = impl_->captureErrors.load(std::memory_order_relaxed);
    d.bufferDiscontinuities = impl_->bufferDiscontinuities.load(std::memory_order_relaxed);
    d.getBufferFailures = impl_->getBufferFailures.load(std::memory_order_relaxed);
    d.releaseBufferFailures = impl_->releaseBufferFailures.load(std::memory_order_relaxed);
    d.deadlineMisses = impl_->deadlineMisses.load(std::memory_order_relaxed);
    d.deviceInvalidations = impl_->deviceInvalidationCount.load(std::memory_order_relaxed);
    d.streamRestartCount = impl_->streamRestartCount.load(std::memory_order_relaxed);

    d.sampleRateMismatches = impl_->sampleRateMismatches.load(std::memory_order_relaxed);
    d.channelMismatches = impl_->channelMismatches.load(std::memory_order_relaxed);
    d.formatMismatches = impl_->formatMismatches.load(std::memory_order_relaxed);

    uint64_t cnt = impl_->countProcessTime.load(std::memory_order_relaxed);

    // Callback Timing
    d.minCallbackTimeUs = impl_->minCallbackTimeUs.load(std::memory_order_relaxed);
    d.maxCallbackTimeUs = impl_->maxCallbackTimeUs.load(std::memory_order_relaxed);
    d.avgCallbackTimeUs = (cnt > 0) ? (impl_->totalCallbackTimeUs.load(std::memory_order_relaxed) / cnt) : 0;
    d.minProcessTimeUs = d.minCallbackTimeUs;
    d.avgProcessTimeUs = d.avgCallbackTimeUs;
    d.maxProcessTimeUs = d.maxCallbackTimeUs;
    impl_->callbackTimingTracker.getPercentiles(reinterpret_cast<uint32_t&>(d.p95CallbackTimeUs),
                                               reinterpret_cast<uint32_t&>(d.p99CallbackTimeUs));

    // Capture Timing
    d.totalFramesCaptured = impl_->totalFramesCaptured.load(std::memory_order_relaxed);
    d.minCaptureTimeUs = impl_->minCaptureTimeUs.load(std::memory_order_relaxed);
    d.maxCaptureTimeUs = impl_->maxCaptureTimeUs.load(std::memory_order_relaxed);
    d.avgCaptureTimeUs = (cnt > 0) ? (impl_->totalCaptureTimeUs.load(std::memory_order_relaxed) / cnt) : 0;
    impl_->captureTimingTracker.getPercentiles(reinterpret_cast<uint32_t&>(d.p95CaptureTimeUs),
                                              reinterpret_cast<uint32_t&>(d.p99CaptureTimeUs));

    // Render Timing
    d.minRenderTimeUs = impl_->minRenderTimeUs.load(std::memory_order_relaxed);
    d.maxRenderTimeUs = impl_->maxRenderTimeUs.load(std::memory_order_relaxed);
    d.avgRenderTimeUs = (cnt > 0) ? (impl_->totalRenderTimeUs.load(std::memory_order_relaxed) / cnt) : 0;
    impl_->renderTimingTracker.getPercentiles(reinterpret_cast<uint32_t&>(d.p95RenderTimeUs),
                                             reinterpret_cast<uint32_t&>(d.p99RenderTimeUs));

    // DSP Timing
    d.minDspTimeUs = impl_->minDspTimeUs.load(std::memory_order_relaxed);
    d.maxDspTimeUs = impl_->maxDspTimeUs.load(std::memory_order_relaxed);
    d.avgDspTimeUs = (cnt > 0) ? (impl_->totalDspTimeUs.load(std::memory_order_relaxed) / cnt) : 0;
    impl_->dspTimingTracker.getPercentiles(reinterpret_cast<uint32_t&>(d.p95DspTimeUs),
                                          reinterpret_cast<uint32_t&>(d.p99DspTimeUs));

    // Submission & Jitter
    d.avgSubmissionTimeUs = (cnt > 0) ? (impl_->totalSubmissionTimeUs.load(std::memory_order_relaxed) / cnt) : 0;
    d.maxSubmissionTimeUs = impl_->maxSubmissionTimeUs.load(std::memory_order_relaxed);

    d.avgWakeupJitterUs = (cnt > 0) ? (impl_->totalWakeupJitterUs.load(std::memory_order_relaxed) / cnt) : 0;
    d.maxWakeupJitterUs = impl_->maxWakeupJitterUs.load(std::memory_order_relaxed);

    d.periodDurationMs = (impl_->actualSampleRate > 0.0 && impl_->activeEngineBlockSize > 0)
        ? ((static_cast<double>(impl_->activeEngineBlockSize) / impl_->actualSampleRate) * 1000.0) : 0.0;

    d.blockDeadlineMs = (impl_->actualSampleRate > 0.0 && impl_->actualBufferSizeFrames > 0)
        ? ((static_cast<double>(impl_->actualBufferSizeFrames) / impl_->actualSampleRate) * 1000.0) : 0.0;

    double periodUs = d.periodDurationMs * 1000.0;
    if (periodUs > 0.0) {
        d.realtimeUtilizationPercent = (static_cast<double>(d.avgDspTimeUs) / periodUs) * 100.0;
        d.maxRealtimeUtilizationPercent = (static_cast<double>(d.maxDspTimeUs) / periodUs) * 100.0;
    }

    d.clockDriftPpm = getClockDriftPpm();
    d.bufferOccupancyPercent = getBufferOccupancyPercent();
    d.nanCount = impl_->nanCount.load(std::memory_order_relaxed);
    d.infCount = impl_->infCount.load(std::memory_order_relaxed);
    d.loopbackActive = (impl_->sourceType == AudioSourceType::SystemLoopback && impl_->running.load(std::memory_order_relaxed));
    d.feedbackRiskDetected = checkFeedbackRisk();

    d.mmcssRegistered = impl_->mmcssRegistered;
    d.mmcssErrorCode = impl_->mmcssErrorCode;
    d.lastHresult = impl_->lastErrorCode;
    d.lastErrorMessage = impl_->lastErrorMessage;
    return d;
}

void WasapiBackend::resetDiagnostics() {
    impl_->resetDiagnostics();
}

std::vector<WasapiBackend::BenchmarkResult> WasapiBackend::runBenchmarkMatrix(uint32_t testBlocksPerConfig) {
    std::vector<BenchmarkResult> results;
    std::vector<double> rates = {44100.0, 48000.0, 96000.0};
    std::vector<uint32_t> buffers = {64, 128, 256, 512};

    // Save active configuration
    bool wasRunning = isRunning();
    double oldRate = impl_->requestedSampleRate;
    uint32_t oldBlock = impl_->requestedBlockSize;

    if (wasRunning) {
        stop();
    }

    for (double r : rates) {
        for (uint32_t b : buffers) {
            BenchmarkResult res;
            res.sampleRate = r;
            res.requestedBuffer = b;

            bool initOk = initialize(r, b, 2);
            if (!initOk) {
                res.status = "UNSUPPORTED";
                res.actualBuffer = 0;
                res.notes = getLastErrorMessage();
                results.push_back(res);
                continue;
            }

            res.actualBuffer = getActualBufferSizeFrames();

            if (!start()) {
                res.status = "FAIL";
                res.notes = "Stream failed to start";
                results.push_back(res);
                continue;
            }

            // Stream for requested blocks
            uint64_t startCallbacks = impl_->totalCallbacks.load(std::memory_order_relaxed);
            uint32_t timeoutMs = static_cast<uint32_t>(((static_cast<double>(b * testBlocksPerConfig) / r) * 1000.0) + 1500.0);
            auto t0 = std::chrono::steady_clock::now();

            while (isRunning()) {
                uint64_t cur = impl_->totalCallbacks.load(std::memory_order_relaxed) - startCallbacks;
                if (cur >= testBlocksPerConfig) break;

                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
                if (elapsed > timeoutMs) break;

                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            auto diag = getDiagnostics();
            stop();

            res.avgCallbackUs = static_cast<double>(diag.avgCallbackTimeUs);
            res.p95CallbackUs = static_cast<double>(diag.p95CallbackTimeUs);
            res.p99CallbackUs = static_cast<double>(diag.p99CallbackTimeUs);
            res.maxCallbackUs = static_cast<double>(diag.maxCallbackTimeUs);
            res.utilizationPct = diag.realtimeUtilizationPercent;
            res.underruns = diag.renderUnderruns;
            res.deadlineMisses = diag.deadlineMisses;

            if (diag.renderUnderruns == 0 && diag.deadlineMisses == 0) {
                res.status = "PASS";
                res.notes = "Stable streaming, zero underruns";
            } else {
                res.status = "FAIL";
                std::ostringstream ss;
                ss << "Underruns=" << diag.renderUnderruns << ", DeadlineMisses=" << diag.deadlineMisses;
                res.notes = ss.str();
            }

            results.push_back(res);
        }
    }

    // Restore original configuration
    initialize(oldRate, oldBlock, 2);
    if (wasRunning) {
        start();
    }

    return results;
}

} // namespace audiocore
