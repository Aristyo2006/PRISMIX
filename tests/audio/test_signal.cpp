#include "audiocore/audio_engine.hpp"
#include "audiocore/audio_buffer.hpp"
#include "audiocore/gain_node.hpp"
#include <iostream>
#include <cmath>
#include <vector>
#include <string>
#include <iomanip>

namespace {

constexpr double PI = 3.14159265358979323846;

void generateSilence(audiocore::AudioBuffer& buf, uint32_t frames) {
    buf.clear(frames);
}

void generateSine(
    audiocore::AudioBuffer& buf,
    uint32_t frames,
    double sampleRate,
    double freqHz,
    double amplitude,
    double* phaseInOut)
{
    double phase = phaseInOut ? *phaseInOut : 0.0;
    const double phaseInc = 2.0 * PI * freqHz / sampleRate;

    for (uint32_t i = 0; i < frames; ++i) {
        float sample = static_cast<float>(amplitude * std::sin(phase));
        for (uint32_t ch = 0; ch < buf.getChannelCount(); ++ch) {
            buf[ch][i] = sample;
        }
        phase += phaseInc;
        if (phase >= 2.0 * PI) {
            phase -= 2.0 * PI;
        }
    }

    if (phaseInOut) {
        *phaseInOut = phase;
    }
}

void generateImpulse(audiocore::AudioBuffer& buf, uint32_t frames, uint32_t impulseIndex, float amplitude) {
    buf.clear(frames);
    for (uint32_t ch = 0; ch < buf.getChannelCount(); ++ch) {
        if (impulseIndex < frames) {
            buf[ch][impulseIndex] = amplitude;
        }
    }
}

float calculatePeak(const audiocore::AudioBuffer& buf, uint32_t channel, uint32_t frames) {
    float peak = 0.0f;
    const float* data = buf.getChannelData(channel);
    for (uint32_t i = 0; i < frames; ++i) {
        float abs_val = std::fabs(data[i]);
        if (abs_val > peak) {
            peak = abs_val;
        }
    }
    return peak;
}

float calculateRms(const audiocore::AudioBuffer& buf, uint32_t channel, uint32_t frames) {
    if (frames == 0) return 0.0f;
    double sum_sq = 0.0;
    const float* data = buf.getChannelData(channel);
    for (uint32_t i = 0; i < frames; ++i) {
        sum_sq += static_cast<double>(data[i] * data[i]);
    }
    return static_cast<float>(std::sqrt(sum_sq / frames));
}

} // namespace

int main() {
    std::cout << "============================================================" << std::endl;
    std::cout << "AUDIO CORE: TEST SIGNAL GENERATOR & DSP PIPELINE VERIFICATION" << std::endl;
    std::cout << "============================================================" << std::endl;

    const double sampleRate = 48000.0;
    const uint32_t blockSize = 128;
    const uint32_t channels = 2;

    audiocore::AudioEngine engine;
    if (!engine.configure(sampleRate, blockSize, channels)) {
        std::cerr << "Failed to configure engine!" << std::endl;
        return 1;
    }

    if (!engine.start()) {
        std::cerr << "Failed to start engine!" << std::endl;
        return 1;
    }

    auto gainNode = std::dynamic_pointer_cast<audiocore::GainNode>(engine.getGraph().getNode(0));
    if (!gainNode) {
        std::cerr << "No GainNode found in engine graph!" << std::endl;
        return 1;
    }

    audiocore::AudioBuffer input(channels, blockSize);
    audiocore::AudioBuffer output(channels, blockSize);

    bool all_ok = true;

    // -----------------------------------------------------------------
    // 1. SILENCE GENERATION & PROCESSING
    // -----------------------------------------------------------------
    std::cout << "\n[1] Testing Silence Signal Pipeline..." << std::endl;
    generateSilence(input, blockSize);
    gainNode->setGainDb(0.0f);
    engine.process(input, output, blockSize);

    float silencePeakCh0 = calculatePeak(output, 0, blockSize);
    float silencePeakCh1 = calculatePeak(output, 1, blockSize);
    std::cout << "    Output Silence Peak Ch0: " << silencePeakCh0
              << " | Ch1: " << silencePeakCh1 << std::endl;
    if (silencePeakCh0 == 0.0f && silencePeakCh1 == 0.0f) {
        std::cout << "    -> PASS: Silence accurately produced 0.0" << std::endl;
    } else {
        std::cerr << "    -> FAIL: Non-zero values in silence output!" << std::endl;
        all_ok = false;
    }

    // -----------------------------------------------------------------
    // 2. SINE WAVE GENERATION (1000 Hz, 48000 Hz SR) -> 0 dB
    // -----------------------------------------------------------------
    std::cout << "\n[2] Testing 1000 Hz Sine Wave Pipeline at 0 dB (Unity)..." << std::endl;
    double phase = 0.0;
    gainNode->setGainDb(0.0f);

    float maxSinePeakCh0 = 0.0f;
    float maxSinePeakCh1 = 0.0f;
    // Process 4 blocks (512 samples) to traverse several complete 1 kHz cycles
    for (int block = 0; block < 4; ++block) {
        generateSine(input, blockSize, sampleRate, 1000.0, 1.0, &phase);
        engine.process(input, output, blockSize);

        maxSinePeakCh0 = std::max(maxSinePeakCh0, calculatePeak(output, 0, blockSize));
        maxSinePeakCh1 = std::max(maxSinePeakCh1, calculatePeak(output, 1, blockSize));
    }

    std::cout << "    Sine Wave (0 dB) Max Peak Ch0: " << std::fixed << std::setprecision(6) << maxSinePeakCh0
              << " | Ch1: " << maxSinePeakCh1 << std::endl;
    if (std::fabs(maxSinePeakCh0 - 1.0f) < 0.005f && std::fabs(maxSinePeakCh1 - 1.0f) < 0.005f) {
        std::cout << "    -> PASS: 1000 Hz Sine wave at 0 dB retains ~1.0 peak" << std::endl;
    } else {
        std::cerr << "    -> FAIL: Peak amplitude mismatch for 0 dB sine!" << std::endl;
        all_ok = false;
    }

    // -----------------------------------------------------------------
    // 3. SINE WAVE GENERATION -> -6 dB GAIN
    // -----------------------------------------------------------------
    std::cout << "\n[3] Testing 1000 Hz Sine Wave Pipeline at -6 dB..." << std::endl;
    gainNode->setGainDb(-6.0f);
    phase = 0.0;

    float maxSinePeak6Ch0 = 0.0f;
    float maxSinePeak6Ch1 = 0.0f;
    for (int block = 0; block < 4; ++block) {
        generateSine(input, blockSize, sampleRate, 1000.0, 1.0, &phase);
        engine.process(input, output, blockSize);

        maxSinePeak6Ch0 = std::max(maxSinePeak6Ch0, calculatePeak(output, 0, blockSize));
        maxSinePeak6Ch1 = std::max(maxSinePeak6Ch1, calculatePeak(output, 1, blockSize));
    }

    const float expected6DbPeak = std::pow(10.0f, -6.0f / 20.0f); // ≈ 0.501187
    std::cout << "    Sine Wave (-6 dB) Max Peak Ch0: " << maxSinePeak6Ch0
              << " | Expected: " << expected6DbPeak << std::endl;
    if (std::fabs(maxSinePeak6Ch0 - expected6DbPeak) < 0.005f &&
        std::fabs(maxSinePeak6Ch1 - expected6DbPeak) < 0.005f) {
        std::cout << "    -> PASS: 1000 Hz Sine wave at -6 dB attenuated to ~0.501187" << std::endl;
    } else {
        std::cerr << "    -> FAIL: Peak amplitude mismatch for -6 dB sine!" << std::endl;
        all_ok = false;
    }

    // -----------------------------------------------------------------
    // 4. SINE WAVE GENERATION -> MUTE
    // -----------------------------------------------------------------
    std::cout << "\n[4] Testing 1000 Hz Sine Wave Pipeline with Mute..." << std::endl;
    gainNode->setMuted(true);
    phase = 0.0;

    generateSine(input, blockSize, sampleRate, 1000.0, 1.0, &phase);
    engine.process(input, output, blockSize);

    float mutedPeakCh0 = calculatePeak(output, 0, blockSize);
    float mutedPeakCh1 = calculatePeak(output, 1, blockSize);
    std::cout << "    Muted Output Peak Ch0: " << mutedPeakCh0
              << " | Ch1: " << mutedPeakCh1 << std::endl;
    if (mutedPeakCh0 == 0.0f && mutedPeakCh1 == 0.0f) {
        std::cout << "    -> PASS: Mute zeroes sine output completely" << std::endl;
    } else {
        std::cerr << "    -> FAIL: Mute failed to zero output!" << std::endl;
        all_ok = false;
    }

    // -----------------------------------------------------------------
    // 5. IMPULSE GENERATION & RESPONSE
    // -----------------------------------------------------------------
    std::cout << "\n[5] Testing Unit Impulse Signal at -6 dB..." << std::endl;
    gainNode->setMuted(false);
    gainNode->setGainDb(-6.0f);

    generateImpulse(input, blockSize, 0, 1.0f); // Impulse at sample 0
    engine.process(input, output, blockSize);

    float impulseCh0Sample0 = output[0][0];
    float impulseCh0Sample1 = output[0][1];
    float impulseCh1Sample0 = output[1][0];

    std::cout << "    Impulse Response Ch0[0]: " << impulseCh0Sample0
              << " | Ch0[1]: " << impulseCh0Sample1
              << " | Expected: " << expected6DbPeak << std::endl;

    if (std::fabs(impulseCh0Sample0 - expected6DbPeak) < 0.0001f &&
        std::fabs(impulseCh1Sample0 - expected6DbPeak) < 0.0001f &&
        impulseCh0Sample1 == 0.0f) {
        std::cout << "    -> PASS: Impulse sample accurately scaled to 0.501187, subsequent samples 0.0" << std::endl;
    } else {
        std::cerr << "    -> FAIL: Impulse response incorrect!" << std::endl;
        all_ok = false;
    }

    engine.stop();

    std::cout << "\n============================================================" << std::endl;
    std::cout << "SIGNAL PIPELINE VERIFICATION: " << (all_ok ? "ALL TESTS PASSED" : "FAILED") << std::endl;
    std::cout << "============================================================" << std::endl;

    return all_ok ? 0 : 1;
}

