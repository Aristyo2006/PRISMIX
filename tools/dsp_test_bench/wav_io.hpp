#pragma once

#include "audiocore/audio_buffer.hpp"
#include <string>
#include <cstdint>
#include <vector>

namespace testbench {

enum class WavIoResult {
    Success = 0,
    FileNotFound,
    FileOpenError,
    FileTooSmall,
    InvalidRiffHeader,
    InvalidWaveHeader,
    MissingFmtChunk,
    MissingDataChunk,
    UnsupportedAudioFormat,
    UnsupportedChannels,
    UnsupportedBitDepth,
    UnsupportedSampleRate,
    CorruptDataChunk,
    WriteError
};

enum class WavExportFormat {
    Pcm16 = 16,
    Pcm24 = 24,
    Pcm32 = 32,
    Float32 = 33
};

struct WavMetadata {
    uint32_t sampleRate{0};
    uint16_t channels{0};
    uint16_t bitsPerSample{0};
    uint16_t audioFormat{0}; // 1 = PCM, 3 = IEEE Float, 0xFFFE = Extensible
    uint32_t totalFrames{0};
    double durationSeconds{0.0};
    std::string formatDescription;
};

class WavIo {
public:
    static WavIoResult readWav(
        const std::string& filePath,
        audiocore::AudioBuffer& outBuffer,
        WavMetadata& outMeta);

    static WavIoResult writeWav(
        const std::string& filePath,
        const audiocore::AudioBuffer& inBuffer,
        uint32_t sampleRate,
        uint32_t framesToWrite,
        WavExportFormat exportFormat = WavExportFormat::Float32);

    static const char* resultToString(WavIoResult result) noexcept;
};

} // namespace testbench

