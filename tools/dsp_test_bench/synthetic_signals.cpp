#include "synthetic_signals.hpp"
#include <cmath>
#include <algorithm>

namespace testbench {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

const char* SyntheticSignals::signalTypeToString(ReferenceSignalType type) noexcept {
    switch (type) {
        case ReferenceSignalType::Silence: return "Silence (0.0)";
        case ReferenceSignalType::Dc: return "DC (+0.5)";
        case ReferenceSignalType::Sine1kHz: return "1 kHz Sine (0 dBFS, 1.0 peak)";
        case ReferenceSignalType::SineMinus6dB: return "1 kHz Sine (-6 dBFS, ~0.501 peak)";
        case ReferenceSignalType::Impulse: return "Unit Impulse (1.0 peak @ frame 0)";
        case ReferenceSignalType::WhiteNoise: return "Deterministic White Noise";
        case ReferenceSignalType::StereoIndependent: return "Stereo Independent (L=440Hz, R=1kHz)";
        default: return "Unknown";
    }
}

void SyntheticSignals::generate(
    ReferenceSignalType type,
    audiocore::AudioBuffer& outBuffer,
    double sampleRate,
    uint32_t channels,
    uint32_t frames)
{
    outBuffer.resize(channels, frames);
    outBuffer.clear();

    switch (type) {
        case ReferenceSignalType::Silence:
            generateSilence(outBuffer, frames);
            break;
        case ReferenceSignalType::Dc:
            generateDc(outBuffer, 0.5f, frames);
            break;
        case ReferenceSignalType::Sine1kHz:
            generateSine(outBuffer, sampleRate, 1000.0, 1.0f, frames);
            break;
        case ReferenceSignalType::SineMinus6dB:
            generateSineMinus6dB(outBuffer, sampleRate, 1000.0, frames);
            break;
        case ReferenceSignalType::Impulse:
            generateImpulse(outBuffer, 1.0f, frames);
            break;
        case ReferenceSignalType::WhiteNoise:
            generateWhiteNoise(outBuffer, 0.5f, frames);
            break;
        case ReferenceSignalType::StereoIndependent:
            generateStereoIndependent(outBuffer, sampleRate, frames);
            break;
    }
}

void SyntheticSignals::generateSilence(audiocore::AudioBuffer& buf, uint32_t frames) {
    buf.clear(frames);
}

void SyntheticSignals::generateDc(audiocore::AudioBuffer& buf, float dcValue, uint32_t frames) {
    const uint32_t actualFrames = std::min(frames, buf.getFrameCount());
    for (uint32_t ch = 0; ch < buf.getChannelCount(); ++ch) {
        float* data = buf.getChannelData(ch);
        for (uint32_t i = 0; i < actualFrames; ++i) {
            data[i] = dcValue;
        }
    }
}

void SyntheticSignals::generateSine(
    audiocore::AudioBuffer& buf,
    double sampleRate,
    double freqHz,
    float amplitude,
    uint32_t frames)
{
    const uint32_t actualFrames = std::min(frames, buf.getFrameCount());
    const double phaseInc = 2.0 * kPi * freqHz / sampleRate;
    for (uint32_t i = 0; i < actualFrames; ++i) {
        float s = static_cast<float>(std::sin(i * phaseInc)) * amplitude;
        for (uint32_t ch = 0; ch < buf.getChannelCount(); ++ch) {
            buf.getChannelData(ch)[i] = s;
        }
    }
}

void SyntheticSignals::generateSineMinus6dB(
    audiocore::AudioBuffer& buf,
    double sampleRate,
    double freqHz,
    uint32_t frames)
{
    // -6 dBFS linear amplitude = 10^(-6/20) = 0.50118723362727228
    constexpr float kMinus6DbLinear = 0.5011872336f;
    generateSine(buf, sampleRate, freqHz, kMinus6DbLinear, frames);
}

void SyntheticSignals::generateImpulse(
    audiocore::AudioBuffer& buf,
    float amplitude,
    uint32_t frames)
{
    const uint32_t actualFrames = std::min(frames, buf.getFrameCount());
    buf.clear(actualFrames);
    if (actualFrames > 0) {
        for (uint32_t ch = 0; ch < buf.getChannelCount(); ++ch) {
            buf.getChannelData(ch)[0] = amplitude;
        }
    }
}

void SyntheticSignals::generateWhiteNoise(
    audiocore::AudioBuffer& buf,
    float amplitude,
    uint32_t frames,
    uint32_t seed)
{
    const uint32_t actualFrames = std::min(frames, buf.getFrameCount());
    uint32_t lcg = seed;
    for (uint32_t i = 0; i < actualFrames; ++i) {
        for (uint32_t ch = 0; ch < buf.getChannelCount(); ++ch) {
            lcg = lcg * 1664525u + 1013904223u;
            float norm = (static_cast<float>(lcg) / 4294967296.0f) * 2.0f - 1.0f;
            buf.getChannelData(ch)[i] = norm * amplitude;
        }
    }
}

void SyntheticSignals::generateStereoIndependent(
    audiocore::AudioBuffer& buf,
    double sampleRate,
    uint32_t frames)
{
    const uint32_t actualFrames = std::min(frames, buf.getFrameCount());
    const double phaseIncL = 2.0 * kPi * 440.0 / sampleRate;
    const double phaseIncR = 2.0 * kPi * 1000.0 / sampleRate;

    float* dataL = buf.getChannelData(0);
    float* dataR = (buf.getChannelCount() > 1) ? buf.getChannelData(1) : nullptr;

    for (uint32_t i = 0; i < actualFrames; ++i) {
        if (dataL) dataL[i] = static_cast<float>(std::sin(i * phaseIncL)) * 0.5f;
        if (dataR) dataR[i] = static_cast<float>(std::sin(i * phaseIncR)) * 0.25f;
    }
}

} // namespace testbench

