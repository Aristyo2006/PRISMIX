#include "wav_io.hpp"
#include <fstream>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <sstream>

namespace testbench {

namespace {

#pragma pack(push, 1)
struct ChunkHeader {
    char id[4];
    uint32_t size;
};

struct WavFmtChunkBody {
    uint16_t audioFormat;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
};
#pragma pack(pop)

constexpr uint16_t WAVE_FORMAT_PCM = 1;
constexpr uint16_t WAVE_FORMAT_IEEE_FLOAT = 3;
constexpr uint16_t WAVE_FORMAT_EXTENSIBLE = 0xFFFE;

// SubFormat GUID for PCM: {00000001-0000-0010-8000-00aa00389b71}
// SubFormat GUID for Float: {00000003-0000-0010-8000-00aa00389b71}
const uint8_t GUID_PCM_TAIL[14] = {
    0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71
};

inline float clampSample(float s) noexcept {
    if (s > 1.0f) return 1.0f;
    if (s < -1.0f) return -1.0f;
    return s;
}

} // namespace

const char* WavIo::resultToString(WavIoResult result) noexcept {
    switch (result) {
        case WavIoResult::Success: return "Success";
        case WavIoResult::FileNotFound: return "File not found";
        case WavIoResult::FileOpenError: return "Cannot open file for read/write";
        case WavIoResult::FileTooSmall: return "File is too small to be a valid WAV";
        case WavIoResult::InvalidRiffHeader: return "Invalid RIFF identifier in header";
        case WavIoResult::InvalidWaveHeader: return "Invalid WAVE format marker";
        case WavIoResult::MissingFmtChunk: return "Missing 'fmt ' chunk in WAV";
        case WavIoResult::MissingDataChunk: return "Missing 'data' chunk in WAV";
        case WavIoResult::UnsupportedAudioFormat: return "Unsupported audio compression format (only uncompressed PCM/Float supported)";
        case WavIoResult::UnsupportedChannels: return "Unsupported channel count (only mono and stereo supported)";
        case WavIoResult::UnsupportedBitDepth: return "Unsupported bit depth (supported: 16, 24, 32-bit PCM and 32-bit Float)";
        case WavIoResult::UnsupportedSampleRate: return "Unsupported or invalid sample rate";
        case WavIoResult::CorruptDataChunk: return "Corrupted data payload chunk";
        case WavIoResult::WriteError: return "Disk write error";
        default: return "Unknown error";
    }
}

WavIoResult WavIo::readWav(
    const std::string& filePath,
    audiocore::AudioBuffer& outBuffer,
    WavMetadata& outMeta)
{
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return WavIoResult::FileNotFound;
    }

    file.seekg(0, std::ios::end);
    std::streampos fileSize = file.tellg();
    file.seekg(0, std::ios::beg);

    if (fileSize < 44) {
        return WavIoResult::FileTooSmall;
    }

    char riffId[4];
    uint32_t riffSize = 0;
    char waveId[4];

    file.read(riffId, 4);
    file.read(reinterpret_cast<char*>(&riffSize), 4);
    file.read(waveId, 4);

    if (std::memcmp(riffId, "RIFF", 4) != 0) {
        return WavIoResult::InvalidRiffHeader;
    }
    if (std::memcmp(waveId, "WAVE", 4) != 0) {
        return WavIoResult::InvalidWaveHeader;
    }

    bool foundFmt = false;
    bool foundData = false;

    WavFmtChunkBody fmtBody{};
    uint16_t effectiveFormat = 0;
    std::streampos dataChunkPos = 0;
    uint32_t dataChunkSize = 0;

    while (file.tellg() < fileSize) {
        ChunkHeader chunk;
        if (!file.read(reinterpret_cast<char*>(&chunk), sizeof(ChunkHeader))) {
            break;
        }

        if (std::memcmp(chunk.id, "fmt ", 4) == 0) {
            if (chunk.size < sizeof(WavFmtChunkBody)) {
                return WavIoResult::MissingFmtChunk;
            }

            file.read(reinterpret_cast<char*>(&fmtBody), sizeof(WavFmtChunkBody));
            effectiveFormat = fmtBody.audioFormat;

            if (fmtBody.audioFormat == WAVE_FORMAT_EXTENSIBLE && chunk.size >= 40) {
                uint16_t cbSize = 0;
                uint16_t validBits = 0;
                uint32_t channelMask = 0;
                uint8_t subFormat[16] = {0};

                file.read(reinterpret_cast<char*>(&cbSize), 2);
                file.read(reinterpret_cast<char*>(&validBits), 2);
                file.read(reinterpret_cast<char*>(&channelMask), 4);
                file.read(reinterpret_cast<char*>(subFormat), 16);

                uint16_t subCode = static_cast<uint16_t>(subFormat[0] | (subFormat[1] << 8));
                if (subCode == 1) {
                    effectiveFormat = WAVE_FORMAT_PCM;
                } else if (subCode == 3) {
                    effectiveFormat = WAVE_FORMAT_IEEE_FLOAT;
                }

                // Skip any remaining bytes in fmt chunk
                if (chunk.size > 40) {
                    file.seekg(chunk.size - 40, std::ios::cur);
                }
            } else {
                // Skip extra bytes in standard fmt chunk if chunk.size > 16
                if (chunk.size > sizeof(WavFmtChunkBody)) {
                    file.seekg(chunk.size - sizeof(WavFmtChunkBody), std::ios::cur);
                }
            }
            foundFmt = true;
        } else if (std::memcmp(chunk.id, "data", 4) == 0) {
            foundData = true;
            dataChunkPos = file.tellg();
            dataChunkSize = chunk.size;
            file.seekg(chunk.size, std::ios::cur);
        } else {
            // Skip other ancillary chunks (LIST, INFO, JUNK, etc.)
            file.seekg(chunk.size, std::ios::cur);
        }

        // Align to word boundary
        if (chunk.size % 2 != 0) {
            file.seekg(1, std::ios::cur);
        }
    }

    if (!foundFmt) return WavIoResult::MissingFmtChunk;
    if (!foundData) return WavIoResult::MissingDataChunk;

    // Validate format constraints
    if (effectiveFormat != WAVE_FORMAT_PCM && effectiveFormat != WAVE_FORMAT_IEEE_FLOAT) {
        return WavIoResult::UnsupportedAudioFormat;
    }

    if (fmtBody.numChannels < 1 || fmtBody.numChannels > 2) {
        return WavIoResult::UnsupportedChannels;
    }

    if (fmtBody.sampleRate < 8000 || fmtBody.sampleRate > 192000) {
        return WavIoResult::UnsupportedSampleRate;
    }

    if (effectiveFormat == WAVE_FORMAT_PCM) {
        if (fmtBody.bitsPerSample != 16 && fmtBody.bitsPerSample != 24 && fmtBody.bitsPerSample != 32) {
            return WavIoResult::UnsupportedBitDepth;
        }
    } else if (effectiveFormat == WAVE_FORMAT_IEEE_FLOAT) {
        if (fmtBody.bitsPerSample != 32) {
            return WavIoResult::UnsupportedBitDepth;
        }
    }

    const uint32_t bytesPerFrame = fmtBody.numChannels * (fmtBody.bitsPerSample / 8);
    if (bytesPerFrame == 0 || (dataChunkSize % bytesPerFrame != 0 && dataChunkSize < bytesPerFrame)) {
        return WavIoResult::CorruptDataChunk;
    }

    const uint32_t totalFrames = dataChunkSize / bytesPerFrame;
    if (totalFrames == 0) {
        return WavIoResult::CorruptDataChunk;
    }

    // Populate metadata
    outMeta.sampleRate = fmtBody.sampleRate;
    outMeta.channels = fmtBody.numChannels;
    outMeta.bitsPerSample = fmtBody.bitsPerSample;
    outMeta.audioFormat = effectiveFormat;
    outMeta.totalFrames = totalFrames;
    outMeta.durationSeconds = static_cast<double>(totalFrames) / static_cast<double>(fmtBody.sampleRate);

    std::ostringstream oss;
    oss << (effectiveFormat == WAVE_FORMAT_IEEE_FLOAT ? "IEEE Float" : "PCM") << " "
        << fmtBody.bitsPerSample << "-bit, "
        << fmtBody.numChannels << " ch, "
        << fmtBody.sampleRate << " Hz";
    outMeta.formatDescription = oss.str();

    // Resize destination buffer
    outBuffer.resize(fmtBody.numChannels, totalFrames);
    outBuffer.clear();

    // Read payload
    file.seekg(dataChunkPos, std::ios::beg);
    std::vector<uint8_t> rawData(dataChunkSize);
    if (!file.read(reinterpret_cast<char*>(rawData.data()), dataChunkSize)) {
        return WavIoResult::CorruptDataChunk;
    }

    const uint8_t* ptr = rawData.data();
    const uint32_t numChannels = fmtBody.numChannels;

    if (effectiveFormat == WAVE_FORMAT_PCM) {
        if (fmtBody.bitsPerSample == 16) {
            const int16_t* s16 = reinterpret_cast<const int16_t*>(ptr);
            for (uint32_t i = 0; i < totalFrames; ++i) {
                for (uint32_t ch = 0; ch < numChannels; ++ch) {
                    int16_t sample = s16[i * numChannels + ch];
                    outBuffer.getChannelData(ch)[i] = static_cast<float>(sample) / 32768.0f;
                }
            }
        } else if (fmtBody.bitsPerSample == 24) {
            for (uint32_t i = 0; i < totalFrames; ++i) {
                for (uint32_t ch = 0; ch < numChannels; ++ch) {
                    size_t byteIdx = (i * numChannels + ch) * 3;
                    uint32_t b0 = ptr[byteIdx + 0];
                    uint32_t b1 = ptr[byteIdx + 1];
                    uint32_t b2 = ptr[byteIdx + 2];
                    int32_t val = static_cast<int32_t>((b0) | (b1 << 8) | (b2 << 16));
                    // Sign extend 24-bit to 32-bit
                    if (val & 0x800000) {
                        val |= 0xFF000000;
                    }
                    outBuffer.getChannelData(ch)[i] = static_cast<float>(val) / 8388608.0f;
                }
            }
        } else if (fmtBody.bitsPerSample == 32) {
            const int32_t* s32 = reinterpret_cast<const int32_t*>(ptr);
            for (uint32_t i = 0; i < totalFrames; ++i) {
                for (uint32_t ch = 0; ch < numChannels; ++ch) {
                    int32_t sample = s32[i * numChannels + ch];
                    outBuffer.getChannelData(ch)[i] = static_cast<float>(sample) / 2147483648.0f;
                }
            }
        }
    } else if (effectiveFormat == WAVE_FORMAT_IEEE_FLOAT) {
        const float* f32 = reinterpret_cast<const float*>(ptr);
        for (uint32_t i = 0; i < totalFrames; ++i) {
            for (uint32_t ch = 0; ch < numChannels; ++ch) {
                outBuffer.getChannelData(ch)[i] = f32[i * numChannels + ch];
            }
        }
    }

    return WavIoResult::Success;
}

WavIoResult WavIo::writeWav(
    const std::string& filePath,
    const audiocore::AudioBuffer& inBuffer,
    uint32_t sampleRate,
    uint32_t framesToWrite,
    WavExportFormat exportFormat)
{
    std::ofstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        return WavIoResult::FileOpenError;
    }

    const uint16_t numChannels = static_cast<uint16_t>(inBuffer.getChannelCount());
    if (numChannels == 0 || numChannels > 2) {
        return WavIoResult::UnsupportedChannels;
    }

    const uint32_t actualFrames = std::min(framesToWrite, inBuffer.getFrameCount());
    const uint16_t bitsPerSample = (exportFormat == WavExportFormat::Float32) ? 32 : static_cast<uint16_t>(exportFormat);
    const uint16_t audioFormat = (exportFormat == WavExportFormat::Float32) ? WAVE_FORMAT_IEEE_FLOAT : WAVE_FORMAT_PCM;
    const uint16_t blockAlign = numChannels * (bitsPerSample / 8);
    const uint32_t byteRate = sampleRate * blockAlign;
    const uint32_t dataBytes = actualFrames * blockAlign;
    const uint32_t riffChunkSize = 36 + dataBytes;

    // 1. RIFF Header
    file.write("RIFF", 4);
    file.write(reinterpret_cast<const char*>(&riffChunkSize), 4);
    file.write("WAVE", 4);

    // 2. fmt Chunk
    file.write("fmt ", 4);
    uint32_t fmtSize = 16;
    file.write(reinterpret_cast<const char*>(&fmtSize), 4);

    WavFmtChunkBody fmt{};
    fmt.audioFormat = audioFormat;
    fmt.numChannels = numChannels;
    fmt.sampleRate = sampleRate;
    fmt.byteRate = byteRate;
    fmt.blockAlign = blockAlign;
    fmt.bitsPerSample = bitsPerSample;
    file.write(reinterpret_cast<const char*>(&fmt), sizeof(fmt));

    // 3. data Chunk
    file.write("data", 4);
    file.write(reinterpret_cast<const char*>(&dataBytes), 4);

    // Write payload
    if (exportFormat == WavExportFormat::Float32) {
        std::vector<float> interleaved(actualFrames * numChannels);
        for (uint32_t i = 0; i < actualFrames; ++i) {
            for (uint32_t ch = 0; ch < numChannels; ++ch) {
                interleaved[i * numChannels + ch] = inBuffer.getChannelData(ch)[i];
            }
        }
        file.write(reinterpret_cast<const char*>(interleaved.data()), interleaved.size() * sizeof(float));
    } else if (exportFormat == WavExportFormat::Pcm16) {
        std::vector<int16_t> interleaved(actualFrames * numChannels);
        for (uint32_t i = 0; i < actualFrames; ++i) {
            for (uint32_t ch = 0; ch < numChannels; ++ch) {
                float s = clampSample(inBuffer.getChannelData(ch)[i]);
                int32_t val = static_cast<int32_t>(std::lrint(s * 32767.0f));
                val = std::max(-32768, std::min(32767, val));
                interleaved[i * numChannels + ch] = static_cast<int16_t>(val);
            }
        }
        file.write(reinterpret_cast<const char*>(interleaved.data()), interleaved.size() * sizeof(int16_t));
    } else if (exportFormat == WavExportFormat::Pcm24) {
        std::vector<uint8_t> interleaved(actualFrames * numChannels * 3);
        size_t idx = 0;
        for (uint32_t i = 0; i < actualFrames; ++i) {
            for (uint32_t ch = 0; ch < numChannels; ++ch) {
                float s = clampSample(inBuffer.getChannelData(ch)[i]);
                int32_t val = static_cast<int32_t>(std::lrint(s * 8388607.0f));
                val = std::max(-8388608, std::min(8388607, val));
                interleaved[idx + 0] = static_cast<uint8_t>(val & 0xFF);
                interleaved[idx + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
                interleaved[idx + 2] = static_cast<uint8_t>((val >> 16) & 0xFF);
                idx += 3;
            }
        }
        file.write(reinterpret_cast<const char*>(interleaved.data()), interleaved.size());
    } else if (exportFormat == WavExportFormat::Pcm32) {
        std::vector<int32_t> interleaved(actualFrames * numChannels);
        for (uint32_t i = 0; i < actualFrames; ++i) {
            for (uint32_t ch = 0; ch < numChannels; ++ch) {
                float s = clampSample(inBuffer.getChannelData(ch)[i]);
                int64_t val = static_cast<int64_t>(std::llround(static_cast<double>(s) * 2147483647.0));
                val = std::max<int64_t>(-2147483648LL, std::min<int64_t>(2147483647LL, val));
                interleaved[i * numChannels + ch] = static_cast<int32_t>(val);
            }
        }
        file.write(reinterpret_cast<const char*>(interleaved.data()), interleaved.size() * sizeof(int32_t));
    }

    if (!file.good()) {
        return WavIoResult::WriteError;
    }

    return WavIoResult::Success;
}

} // namespace testbench

