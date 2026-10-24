#pragma once

#include "audiocore/audio_buffer.hpp"
#include <cstdint>

namespace testbench {

enum class ReferenceSignalType {
    Silence,
    Dc,
    Sine1kHz,
    SineMinus6dB,
    Impulse,
    WhiteNoise,
    StereoIndependent
};

class SyntheticSignals {
public:
    static void generate(
        ReferenceSignalType type,
        audiocore::AudioBuffer& outBuffer,
        double sampleRate,
        uint32_t channels,
        uint32_t frames);

    static void generateSilence(audiocore::AudioBuffer& buf, uint32_t frames);
    static void generateDc(audiocore::AudioBuffer& buf, float dcValue, uint32_t frames);
    static void generateSine(audiocore::AudioBuffer& buf, double sampleRate, double freqHz, float amplitude, uint32_t frames);
    static void generateSineMinus6dB(audiocore::AudioBuffer& buf, double sampleRate, double freqHz, uint32_t frames);
    static void generateImpulse(audiocore::AudioBuffer& buf, float amplitude, uint32_t frames);
    static void generateWhiteNoise(audiocore::AudioBuffer& buf, float amplitude, uint32_t frames, uint32_t seed = 12345);
    static void generateStereoIndependent(audiocore::AudioBuffer& buf, double sampleRate, uint32_t frames);

    static const char* signalTypeToString(ReferenceSignalType type) noexcept;
};

} // namespace testbench

