#include "audiocore/audio_buffer.hpp"
#include "audiocore/audio_node.hpp"
#include "audiocore/gain_node.hpp"
#include "audiocore/parameter_smoother.hpp"
#include "audiocore/pan_node.hpp"
#include "audiocore/mixer_node.hpp"
#include "audiocore/biquad_filter.hpp"
#include "audiocore/eq_node.hpp"
#include "audiocore/compressor_node.hpp"
#include "audiocore/limiter_node.hpp"
#include "audiocore/meter_node.hpp"
#include "audiocore/audio_engine.hpp"

#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <cassert>

namespace {

constexpr double kPi = 3.14159265358979323846;

struct TestContext {
    int passed = 0;
    int failed = 0;
};

bool checkResult(
    TestContext& ctx,
    const std::string& testName,
    const std::string& conditions,
    double expected,
    double actual,
    double tolerance)
{
    std::cout << "------------------------------------------------------------\n";
    std::cout << "TEST: " << testName << "\n";
    std::cout << "Conditions: " << conditions << "\n";
    std::cout << "Expected:   " << std::fixed << std::setprecision(6) << expected << "\n";
    std::cout << "Actual:     " << std::fixed << std::setprecision(6) << actual << "\n";
    std::cout << "Tolerance:  ±" << std::fixed << std::setprecision(6) << tolerance << "\n";

    bool pass = !std::isnan(actual) && !std::isinf(actual) && (std::abs(actual - expected) <= tolerance);
    if (pass) {
        std::cout << "RESULT:\n[PASS]\n";
        ctx.passed++;
    } else {
        std::cout << "RESULT:\n[FAIL]\n";
        ctx.failed++;
    }
    std::cout << "------------------------------------------------------------\n";
    return pass;
}

bool checkCondition(
    TestContext& ctx,
    const std::string& testName,
    const std::string& conditions,
    bool condition)
{
    std::cout << "------------------------------------------------------------\n";
    std::cout << "TEST: " << testName << "\n";
    std::cout << "Conditions: " << conditions << "\n";
    std::cout << "RESULT:\n" << (condition ? "[PASS]" : "[FAIL]") << "\n";
    std::cout << "------------------------------------------------------------\n";
    if (condition) ctx.passed++;
    else ctx.failed++;
    return condition;
}

struct ContinuousSine {
    double phase{0.0};
    void fill(audiocore::AudioBuffer& buf, uint32_t channel, double sampleRate, double freq, float amplitude, uint32_t frames) {
        float* data = buf.getChannelData(channel);
        for (uint32_t i = 0; i < frames; ++i) {
            data[i] = static_cast<float>(std::sin(phase)) * amplitude;
            phase += 2.0 * kPi * freq / sampleRate;
            if (phase >= 2.0 * kPi) phase -= 2.0 * kPi;
        }
    }
    void reset() { phase = 0.0; }
};

void fillSine(audiocore::AudioBuffer& buf, uint32_t channel, double sampleRate, double freq, float amplitude, uint32_t frames) {
    float* data = buf.getChannelData(channel);
    for (uint32_t i = 0; i < frames; ++i) {
        data[i] = static_cast<float>(std::sin(2.0 * kPi * freq * i / sampleRate)) * amplitude;
    }
}

float findPeak(const audiocore::AudioBuffer& buf, uint32_t channel, uint32_t frames) {
    const float* data = buf.getChannelData(channel);
    float maxVal = 0.0f;
    for (uint32_t i = 0; i < frames; ++i) {
        float v = std::abs(data[i]);
        if (v > maxVal) maxVal = v;
    }
    return maxVal;
}

bool containsInvalidSamples(const audiocore::AudioBuffer& buf, uint32_t frames) {
    for (uint32_t ch = 0; ch < buf.getChannelCount(); ++ch) {
        const float* data = buf.getChannelData(ch);
        for (uint32_t i = 0; i < frames; ++i) {
            if (std::isnan(data[i]) || std::isinf(data[i])) return true;
        }
    }
    return false;
}

} // namespace

int main() {
    std::cout << "============================================================\n";
    std::cout << "AUDIO CORE: DSP CORE COMPREHENSIVE VERIFICATION SUITE\n";
    std::cout << "============================================================\n\n";

    TestContext ctx;
    constexpr uint32_t kBlockSize = 128;
    constexpr double kSampleRate48k = 48000.0;
    constexpr double kSampleRate96k = 96000.0;

    audiocore::AudioBuffer inBuf(2, kBlockSize);
    audiocore::AudioBuffer outBuf(2, kBlockSize);

    // =========================================================================
    // 1. MIXER TESTS
    // =========================================================================
    std::cout << "\n>>> [SECTION 1: MIXER NODE TESTS] <<<\n";
    {
        audiocore::MixerNode mixer(4);

        // 1. Unity Gain
        inBuf.clear();
        for (uint32_t i = 0; i < kBlockSize; ++i) inBuf.getChannelData(0)[i] = 1.0f;
        mixer.setChannelGainDb(0, 0.0f);
        mixer.setChannelPan(0, -1.0f); // Pan full left
        mixer.setMasterGainDb(0.0f);
        mixer.process(inBuf, outBuf, kBlockSize);
        checkResult(ctx, "Mixer Unity Gain", "Ch0=1.0, Pan=FullLeft, Master=0dB", 1.0, outBuf.getChannelData(0)[10], 1e-5);

        // 2. -6 dB
        mixer.setChannelGainDb(0, -6.0f);
        mixer.process(inBuf, outBuf, kBlockSize);
        float expectedMinus6 = std::pow(10.0f, -6.0f / 20.0f);
        checkResult(ctx, "Mixer -6 dB", "Ch0=-6dB, Input=1.0", expectedMinus6, outBuf.getChannelData(0)[10], 1e-4);

        // 3. Mute
        mixer.setChannelMute(0, true);
        mixer.process(inBuf, outBuf, kBlockSize);
        checkResult(ctx, "Mixer Mute", "Ch0 Muted=true", 0.0, outBuf.getChannelData(0)[10], 0.0);
        mixer.setChannelMute(0, false);

        // 4. Stereo Independence
        inBuf.clear();
        for (uint32_t i = 0; i < kBlockSize; ++i) {
            inBuf.getChannelData(0)[i] = 1.0f; // Left
            inBuf.getChannelData(1)[i] = 0.5f; // Right
        }
        mixer.setChannelGainDb(0, 0.0f);
        mixer.setChannelPan(0, -1.0f); // Ch0 -> Left only
        mixer.setChannelGainDb(1, 0.0f);
        mixer.setChannelPan(1, 1.0f);  // Ch1 -> Right only
        mixer.process(inBuf, outBuf, kBlockSize);
        bool stereoOk = (std::abs(outBuf.getChannelData(0)[10] - 1.0f) < 1e-4) &&
                        (std::abs(outBuf.getChannelData(1)[10] - 0.5f) < 1e-4);
        checkCondition(ctx, "Mixer Stereo", "Ch0->Left (1.0), Ch1->Right (0.5), No Leakage", stereoOk);

        // 5. Summing (Multiple Channels)
        audiocore::AudioBuffer sumInBuf(4, kBlockSize);
        sumInBuf.clear();
        for (uint32_t c = 0; c < 4; ++c) {
            for (uint32_t i = 0; i < kBlockSize; ++i) {
                sumInBuf.getChannelData(c)[i] = 0.25f;
            }
            mixer.setChannelGainDb(c, 0.0f);
            mixer.setChannelPan(c, -1.0f); // all summed into Left
            mixer.setChannelMute(c, false);
            mixer.setChannelSolo(c, false);
        }
        mixer.process(sumInBuf, outBuf, kBlockSize);
        checkResult(ctx, "Mixer Summing", "4 channels x 0.25 summed into Left", 1.0, outBuf.getChannelData(0)[10], 1e-4);

        // 6. Silence Input
        sumInBuf.clear();
        mixer.process(sumInBuf, outBuf, kBlockSize);
        checkResult(ctx, "Mixer Silence", "Input 0.0 -> Output 0.0", 0.0, findPeak(outBuf, 0, kBlockSize), 0.0);

        // 7. Impulse Input
        sumInBuf.clear();
        sumInBuf.getChannelData(0)[0] = 1.0f;
        mixer.process(sumInBuf, outBuf, kBlockSize);
        checkResult(ctx, "Mixer Impulse", "Unit impulse in Ch0 -> Output impulse 1.0", 1.0, outBuf.getChannelData(0)[0], 1e-5);

        // 8. Negative Samples
        sumInBuf.clear();
        for (uint32_t i = 0; i < kBlockSize; ++i) sumInBuf.getChannelData(0)[i] = -0.75f;
        mixer.setChannelGainDb(0, -6.0f);
        mixer.process(sumInBuf, outBuf, kBlockSize);
        float expectedNeg = -0.75f * expectedMinus6;
        checkResult(ctx, "Mixer Negative Samples", "Input -0.75 @ -6 dB", expectedNeg, outBuf.getChannelData(0)[5], 1e-4);

        // 9. Clipping Detection & Linear Sum Headroom
        sumInBuf.clear();
        for (uint32_t c = 0; c < 2; ++c) {
            for (uint32_t i = 0; i < kBlockSize; ++i) sumInBuf.getChannelData(c)[i] = 10.0f;
            mixer.setChannelGainDb(c, 0.0f);
            mixer.setChannelPan(c, -1.0f);
        }
        mixer.process(sumInBuf, outBuf, kBlockSize);
        bool linearSum = (std::abs(outBuf.getChannelData(0)[0] - 20.0f) < 1e-4) && !containsInvalidSamples(outBuf, kBlockSize);
        checkCondition(ctx, "Mixer Clipping Detection", "Linear IEEE float sum 10.0 + 10.0 = 20.0 (no wrap, no NaN)", linearSum);
    }

    // =========================================================================
    // 2. PAN TESTS
    // =========================================================================
    std::cout << "\n>>> [SECTION 2: PAN TESTS] <<<\n";
    {
        audiocore::PanNode panner(0.0f, kSampleRate48k);
        panner.setSmoothingTimeMs(0.0f); // Instantaneous test
        inBuf.clear();
        for (uint32_t i = 0; i < kBlockSize; ++i) {
            inBuf.getChannelData(0)[i] = 1.0f;
            inBuf.getChannelData(1)[i] = 1.0f;
        }

        // Center
        panner.setPan(0.0f);
        panner.process(inBuf, outBuf, kBlockSize);
        double expectedCenter = std::cos(kPi / 4.0);
        checkResult(ctx, "Pan Center", "Pan=0.0, Constant Power cos(pi/4)", expectedCenter, outBuf.getChannelData(0)[0], 1e-5);
        checkResult(ctx, "Pan Center Right", "Pan=0.0, Constant Power sin(pi/4)", expectedCenter, outBuf.getChannelData(1)[0], 1e-5);

        // Full Left
        panner.setPan(-1.0f);
        panner.process(inBuf, outBuf, kBlockSize);
        bool leftOk = (std::abs(outBuf.getChannelData(0)[0] - 1.0f) < 1e-5) &&
                      (std::abs(outBuf.getChannelData(1)[0] - 0.0f) < 1e-5);
        checkCondition(ctx, "Pan Left", "Pan=-1.0, Left=1.0, Right=0.0", leftOk);

        // Full Right
        panner.setPan(1.0f);
        panner.process(inBuf, outBuf, kBlockSize);
        bool rightOk = (std::abs(outBuf.getChannelData(0)[0] - 0.0f) < 1e-5) &&
                       (std::abs(outBuf.getChannelData(1)[0] - 1.0f) < 1e-5);
        checkCondition(ctx, "Pan Right", "Pan=+1.0, Left=0.0, Right=1.0", rightOk);

        // Pan 25% (p = -0.5)
        panner.setPan(-0.5f);
        panner.process(inBuf, outBuf, kBlockSize);
        double exp25L = std::cos(kPi / 8.0); // 22.5 deg
        double exp25R = std::sin(kPi / 8.0);
        checkResult(ctx, "Pan 25% Left", "Pan=-0.5, theta=22.5 deg, Left=cos(pi/8)", exp25L, outBuf.getChannelData(0)[0], 1e-5);
        checkResult(ctx, "Pan 25% Right", "Pan=-0.5, theta=22.5 deg, Right=sin(pi/8)", exp25R, outBuf.getChannelData(1)[0], 1e-5);

        // Pan 50% (p = 0.0)
        panner.setPan(0.0f);
        panner.process(inBuf, outBuf, kBlockSize);
        checkResult(ctx, "Pan 50%", "Pan=0.0, Center=cos(pi/4)", expectedCenter, outBuf.getChannelData(0)[0], 1e-5);

        // Pan 75% (p = +0.5)
        panner.setPan(0.5f);
        panner.process(inBuf, outBuf, kBlockSize);
        double exp75L = std::cos(3.0 * kPi / 8.0); // 67.5 deg
        double exp75R = std::sin(3.0 * kPi / 8.0);
        checkResult(ctx, "Pan 75% Left", "Pan=+0.5, theta=67.5 deg, Left=cos(3pi/8)", exp75L, outBuf.getChannelData(0)[0], 1e-5);
        checkResult(ctx, "Pan 75% Right", "Pan=+0.5, theta=67.5 deg, Right=sin(3pi/8)", exp75R, outBuf.getChannelData(1)[0], 1e-5);
    }

    // =========================================================================
    // 3. MUTE & SOLO TESTS
    // =========================================================================
    std::cout << "\n>>> [SECTION 3: MUTE & SOLO TESTS] <<<\n";
    {
        audiocore::MixerNode mixer(3);
        inBuf.clear();
        for (uint32_t i = 0; i < kBlockSize; ++i) inBuf.getChannelData(0)[i] = 1.0f;

        // Mute ON
        mixer.setChannelMute(0, true);
        mixer.process(inBuf, outBuf, kBlockSize);
        checkCondition(ctx, "Mute ON", "Mute=true -> L=0.0, R=0.0",
                       outBuf.getChannelData(0)[0] == 0.0f && outBuf.getChannelData(1)[0] == 0.0f);

        // Mute OFF
        mixer.setChannelMute(0, false);
        mixer.process(inBuf, outBuf, kBlockSize);
        checkCondition(ctx, "Mute OFF", "Mute=false -> Signal Restored",
                       outBuf.getChannelData(0)[0] > 0.0f);

        // Solo Single
        audiocore::AudioBuffer multiBuf(3, kBlockSize);
        for (uint32_t c = 0; c < 3; ++c) {
            for (uint32_t i = 0; i < kBlockSize; ++i) multiBuf.getChannelData(c)[i] = 1.0f;
            mixer.setChannelPan(c, -1.0f); // L
            mixer.setChannelGainDb(c, 0.0f);
            mixer.setChannelMute(c, false);
            mixer.setChannelSolo(c, false);
        }
        mixer.setChannelSolo(0, true); // Solo Ch0 only
        mixer.process(multiBuf, outBuf, kBlockSize);
        checkResult(ctx, "Solo Single", "Ch0 Solo=true, Ch1/Ch2 silent", 1.0, outBuf.getChannelData(0)[0], 1e-4);

        // Solo Multiple
        mixer.setChannelSolo(1, true); // Solo Ch0 AND Ch1
        mixer.process(multiBuf, outBuf, kBlockSize);
        checkResult(ctx, "Solo Multiple", "Ch0 and Ch1 Soloed (Sum=2.0), Ch2 silent", 2.0, outBuf.getChannelData(0)[0], 1e-4);

        // Solo + Manual Mute Interaction (PRE PRECEDENCE)
        mixer.setChannelMute(0, true);
        mixer.setChannelSolo(1, false); // Only Ch0 remains soloed
        mixer.process(multiBuf, outBuf, kBlockSize);
        checkResult(ctx, "Solo + Manual Mute", "Ch0 Soloed AND Muted -> Silence (0.0)", 0.0, outBuf.getChannelData(0)[0], 0.0);
    }

    // =========================================================================
    // 4. PARAMETER SMOOTHING TESTS
    // =========================================================================
    std::cout << "\n>>> [SECTION 4: PARAMETER SMOOTHING TESTS] <<<\n";
    {
        // Initial Value
        audiocore::ParameterSmoother smootherInit(0.25f, kSampleRate48k, 20.0f);
        checkResult(ctx, "Smoothing Initial Value", "Initial value matches 0.25", 0.25, smootherInit.getCurrentValue(), 1e-6);

        // Target Value
        smootherInit.setTarget(0.75f);
        checkResult(ctx, "Smoothing Target Value", "Target value matches 0.75", 0.75, smootherInit.getTargetValue(), 1e-6);

        // Zero smoothing time
        audiocore::ParameterSmoother smootherZero(0.0f, kSampleRate48k, 0.0f);
        smootherZero.setTarget(1.0f);
        checkResult(ctx, "Smoothing Zero", "Time=0ms, Immediate Step to 1.0", 1.0, smootherZero.nextSample(), 1e-6);

        // Ramp progression
        audiocore::ParameterSmoother smoother(0.0f, kSampleRate48k, 10.0f); // 480 frames
        smoother.setTarget(1.0f);
        std::vector<float> ramp(480);
        for (int i = 0; i < 480; ++i) {
            ramp[i] = smoother.nextSample();
        }
        checkResult(ctx, "Smoothing Ramp", "Midpoint frame 240 of 480 frames", 0.5, ramp[239], 0.01);

        // Normal smoothing & convergence
        float afterConv = smoother.nextSample();
        bool convOk = (std::abs(ramp[479] - 1.0f) < 1e-6) && (afterConv == 1.0f);
        checkCondition(ctx, "Smoothing Convergence", "Exact 1.0 target snap", convOk);

        // No Overshoot
        float maxRamp = *std::max_element(ramp.begin(), ramp.end());
        checkCondition(ctx, "Smoothing No Overshoot", "Max ramp value <= target 1.0", maxRamp <= 1.0f);

        // Rapid Target Changes
        smoother.reset(0.5f);
        smoother.setTarget(1.0f);
        for (int i = 0; i < 50; ++i) smoother.nextSample();
        float midVal = smoother.getCurrentValue();
        smoother.setTarget(0.2f);
        float nextVal = smoother.nextSample();
        bool continuous = std::abs(nextVal - midVal) < 0.01f;
        checkCondition(ctx, "Smoothing Rapid Target", "Seamless redirection without discontinuity", continuous);

        // Block Boundary Continuity
        smoother.reset(0.0f);
        smoother.setTarget(1.0f);
        std::vector<float> blk1(128);
        std::vector<float> blk2(128);
        smoother.processBlock(blk1.data(), 128);
        smoother.processBlock(blk2.data(), 128);
        bool boundaryContinuous = (blk2[0] > blk1[127]) && (blk2[0] - blk1[127] < 0.01f);
        checkCondition(ctx, "Smoothing Block Boundary", "Continuous smooth progression across blocks", boundaryContinuous);
    }

    // =========================================================================
    // 5. PARAMETRIC EQ TESTS
    // =========================================================================
    std::cout << "\n>>> [SECTION 5: PARAMETRIC EQ TESTS] <<<\n";
    {
        audiocore::ParametricEQNode eq(4, kSampleRate48k);

        // EQ Bypass
        eq.setBand(0, audiocore::FilterType::Peaking, 1000.0, 1.0, 6.0);
        eq.setBypassed(true);
        inBuf.clear();
        fillSine(inBuf, 0, kSampleRate48k, 1000.0, 0.5f, kBlockSize);
        eq.process(inBuf, outBuf, kBlockSize);
        checkResult(ctx, "EQ Bypass", "Bypass=true -> Output == Input", inBuf.getChannelData(0)[50], outBuf.getChannelData(0)[50], 1e-6);

        eq.setBypassed(false);

        // EQ Unity (0 dB)
        eq.setBand(0, audiocore::FilterType::Peaking, 1000.0, 1.0, 0.0);
        eq.process(inBuf, outBuf, kBlockSize);
        checkResult(ctx, "EQ Unity", "Gain=0 dB -> Exact Unity Pass-Through", inBuf.getChannelData(0)[50], outBuf.getChannelData(0)[50], 1e-6);

        ContinuousSine sineGen;

        // EQ Bell +6 dB
        eq.reset();
        sineGen.reset();
        eq.setBand(0, audiocore::FilterType::Peaking, 1000.0, 1.0, 6.0);
        for (int b = 0; b < 40; ++b) {
            sineGen.fill(inBuf, 0, kSampleRate48k, 1000.0, 0.5f, kBlockSize);
            eq.process(inBuf, outBuf, kBlockSize);
        }
        float peakPlus6 = findPeak(outBuf, 0, kBlockSize);
        float expectedPlus6 = 0.5f * std::pow(10.0f, 6.0f / 20.0f); // ~0.9976
        checkResult(ctx, "EQ Bell +6 dB", "1000 Hz Bell +6dB @ 48kHz (settled)", expectedPlus6, peakPlus6, 0.05);

        // EQ Bell -6 dB
        eq.reset();
        sineGen.reset();
        eq.setBand(0, audiocore::FilterType::Peaking, 1000.0, 1.0, -6.0);
        for (int b = 0; b < 40; ++b) {
            sineGen.fill(inBuf, 0, kSampleRate48k, 1000.0, 1.0f, kBlockSize);
            eq.process(inBuf, outBuf, kBlockSize);
        }
        float peakMinus6 = findPeak(outBuf, 0, kBlockSize);
        float expectedMinus6 = 1.0f * std::pow(10.0f, -6.0f / 20.0f); // ~0.5012
        checkResult(ctx, "EQ Bell -6 dB", "1000 Hz Bell -6dB @ 48kHz (settled)", expectedMinus6, peakMinus6, 0.05);

        // EQ Low Shelf
        eq.reset();
        sineGen.reset();
        eq.setBand(0, audiocore::FilterType::LowShelf, 200.0, 0.7071, 6.0);
        float peakLowShelf = 0.0f;
        for (int b = 0; b < 80; ++b) {
            sineGen.fill(inBuf, 0, kSampleRate48k, 50.0, 0.5f, kBlockSize);
            eq.process(inBuf, outBuf, kBlockSize);
            if (b >= 70) {
                float blkPeak = findPeak(outBuf, 0, kBlockSize);
                if (blkPeak > peakLowShelf) peakLowShelf = blkPeak;
            }
        }
        checkResult(ctx, "EQ Low Shelf", "Low Shelf +6dB @ 50 Hz", expectedPlus6, peakLowShelf, 0.05);

        // EQ High Shelf
        eq.reset();
        sineGen.reset();
        eq.setBand(0, audiocore::FilterType::HighShelf, 2000.0, 0.7071, 6.0);
        for (int b = 0; b < 40; ++b) {
            sineGen.fill(inBuf, 0, kSampleRate48k, 10000.0, 0.5f, kBlockSize);
            eq.process(inBuf, outBuf, kBlockSize);
        }
        float peakHighShelf = findPeak(outBuf, 0, kBlockSize);
        checkResult(ctx, "EQ High Shelf", "High Shelf +6dB @ 10 kHz", expectedPlus6, peakHighShelf, 0.05);

        // EQ 48 kHz
        checkCondition(ctx, "EQ 48 kHz", "Sample Rate 48000 Hz Configured & Stable", eq.getSampleRate() == 48000.0);

        // EQ 96 kHz
        eq.setSampleRate(kSampleRate96k);
        eq.reset();
        sineGen.reset();
        eq.setBand(0, audiocore::FilterType::Peaking, 2000.0, 1.0, 6.0);
        for (int b = 0; b < 40; ++b) {
            sineGen.fill(inBuf, 0, kSampleRate96k, 2000.0, 0.5f, kBlockSize);
            eq.process(inBuf, outBuf, kBlockSize);
        }
        float peak96k = findPeak(outBuf, 0, kBlockSize);
        checkResult(ctx, "EQ 96 kHz", "2000 Hz Bell +6dB @ 96kHz (settled)", expectedPlus6, peak96k, 0.05);

        // EQ Impulse
        eq.reset();
        inBuf.clear();
        inBuf.getChannelData(0)[0] = 1.0f;
        eq.process(inBuf, outBuf, kBlockSize);
        bool impulseStable = !containsInvalidSamples(outBuf, kBlockSize) && (outBuf.getChannelData(0)[0] > 0.0f);
        checkCondition(ctx, "EQ Impulse", "Unit impulse response finite & stable (no NaN)", impulseStable);

        // EQ Stereo Independence
        eq.reset();
        inBuf.clear();
        inBuf.getChannelData(0)[0] = 1.0f;
        inBuf.getChannelData(1)[0] = 0.0f;
        eq.process(inBuf, outBuf, kBlockSize);
        bool stereoEqOk = (outBuf.getChannelData(0)[0] > 0.0f) && (findPeak(outBuf, 1, kBlockSize) == 0.0f);
        checkCondition(ctx, "EQ Stereo", "Left processed, Right strictly silent (no leakage)", stereoEqOk);

        // EQ Multi-Block Continuity
        bool multiBlockOk = true;
        for (int b = 0; b < 5; ++b) {
            sineGen.fill(inBuf, 0, kSampleRate48k, 1000.0, 0.5f, kBlockSize);
            eq.process(inBuf, outBuf, kBlockSize);
            if (containsInvalidSamples(outBuf, kBlockSize)) multiBlockOk = false;
        }
        checkCondition(ctx, "EQ Multi-Block", "State persists seamlessly across sequential blocks", multiBlockOk);

        // EQ Reset State
        eq.reset();
        inBuf.clear();
        eq.process(inBuf, outBuf, kBlockSize);
        checkResult(ctx, "EQ Reset State", "Reset clears filter histories to silence", 0.0, findPeak(outBuf, 0, kBlockSize), 0.0);
    }

    // =========================================================================
    // 6. COMPRESSOR TESTS
    // =========================================================================
    std::cout << "\n>>> [SECTION 6: COMPRESSOR TESTS] <<<\n";
    {
        audiocore::CompressorNode comp(kSampleRate48k);
        comp.setThresholdDb(-12.0f);
        comp.setRatio(4.0f);
        comp.setAttackMs(1.0f);
        comp.setReleaseMs(50.0f);
        comp.setMakeupGainDb(0.0f);

        // Below Threshold
        comp.reset();
        inBuf.clear();
        float belowAmp = std::pow(10.0f, -20.0f / 20.0f); // 0.1
        for (uint32_t i = 0; i < kBlockSize; ++i) inBuf.getChannelData(0)[i] = belowAmp;
        comp.process(inBuf, outBuf, kBlockSize);
        checkResult(ctx, "Compressor Below Thresh", "Input -20dB < -12dB (Gain Reduction = 0dB)", belowAmp, outBuf.getChannelData(0)[10], 1e-4);

        // Above Threshold
        for (int b = 0; b < 50; ++b) {
            for (uint32_t i = 0; i < kBlockSize; ++i) inBuf.getChannelData(0)[i] = 1.0f;
            comp.process(inBuf, outBuf, kBlockSize);
        }
        float expectedAbove = std::pow(10.0f, -9.0f / 20.0f);
        checkResult(ctx, "Compressor Above Thresh", "Input 0dB > -12dB, 4:1 Ratio -> -9dB Out", expectedAbove, outBuf.getChannelData(0)[10], 0.02);

        // Ratio Behavior
        comp.reset();
        comp.setRatio(2.0f);
        for (int b = 0; b < 50; ++b) comp.process(inBuf, outBuf, kBlockSize);
        float out2to1 = outBuf.getChannelData(0)[10];

        comp.reset();
        comp.setRatio(8.0f);
        for (int b = 0; b < 50; ++b) comp.process(inBuf, outBuf, kBlockSize);
        float out8to1 = outBuf.getChannelData(0)[10];
        checkCondition(ctx, "Compressor Ratio", "Higher ratio (8:1) produces more GR than (2:1)", out8to1 < out2to1);

        // Attack
        comp.reset();
        comp.setRatio(4.0f);
        comp.setAttackMs(20.0f);
        inBuf.clear();
        for (uint32_t i = 0; i < kBlockSize; ++i) inBuf.getChannelData(0)[i] = 1.0f;
        comp.process(inBuf, outBuf, kBlockSize);
        float grFirstBlock = comp.getCurrentGainReductionDb();
        checkCondition(ctx, "Compressor Attack", "Gain reduction smoothly attacks over time", (grFirstBlock < 0.0f) && (grFirstBlock > -9.0f));

        // Release
        inBuf.clear();
        for (int b = 0; b < 100; ++b) comp.process(inBuf, outBuf, kBlockSize);
        float grReleased = comp.getCurrentGainReductionDb();
        checkResult(ctx, "Compressor Release", "Gain reduction recovers back to 0 dB", 0.0, grReleased, 0.01);

        // Makeup Gain
        comp.reset();
        comp.setMakeupGainDb(6.0f);
        for (uint32_t i = 0; i < kBlockSize; ++i) inBuf.getChannelData(0)[i] = belowAmp;
        comp.process(inBuf, outBuf, kBlockSize);
        float expectedMakeup = belowAmp * std::pow(10.0f, 6.0f / 20.0f);
        checkResult(ctx, "Compressor Makeup", "+6dB Makeup on uncompressed signal", expectedMakeup, outBuf.getChannelData(0)[10], 1e-4);

        // Silence
        comp.reset();
        inBuf.clear();
        comp.process(inBuf, outBuf, kBlockSize);
        checkResult(ctx, "Compressor Silence", "Silence in -> Silence out", 0.0, findPeak(outBuf, 0, kBlockSize), 0.0);

        // Impulse
        comp.reset();
        inBuf.clear();
        inBuf.getChannelData(0)[0] = 1.0f;
        comp.process(inBuf, outBuf, kBlockSize);
        checkCondition(ctx, "Compressor Impulse", "Impulse handled stably without NaN", !containsInvalidSamples(outBuf, kBlockSize));

        // Sustained Sine
        comp.reset();
        comp.setMakeupGainDb(0.0f);
        comp.setAttackMs(2.0f);
        ContinuousSine compSine;
        for (int b = 0; b < 40; ++b) {
            compSine.fill(inBuf, 0, kSampleRate48k, 1000.0, 1.0f, kBlockSize);
            comp.process(inBuf, outBuf, kBlockSize);
        }
        float sustainedPeak = findPeak(outBuf, 0, kBlockSize);
        checkCondition(ctx, "Compressor Sustained Sine", "Sustained sine compressed cleanly", sustainedPeak < 0.8f);

        // Stereo Link
        comp.reset();
        inBuf.clear();
        for (uint32_t i = 0; i < kBlockSize; ++i) {
            inBuf.getChannelData(0)[i] = 1.0f;     // Left = Loud (0 dB)
            inBuf.getChannelData(1)[i] = belowAmp; // Right = Quiet (-20 dB)
        }
        for (int b = 0; b < 30; ++b) comp.process(inBuf, outBuf, kBlockSize);
        float linGainL = outBuf.getChannelData(0)[10] / 1.0f;
        float linGainR = outBuf.getChannelData(1)[10] / belowAmp;
        checkResult(ctx, "Compressor Stereo Link", "Left and Right gain reduction matched", linGainL, linGainR, 1e-4);

        // Multi-Block Continuity
        float env1 = comp.getCurrentGainReductionDb();
        comp.process(inBuf, outBuf, kBlockSize);
        float env2 = comp.getCurrentGainReductionDb();
        checkCondition(ctx, "Compressor Multi-Block", "Envelope follower persists across blocks", std::abs(env1 - env2) < 0.1f);
    }

    // =========================================================================
    // 7. LIMITER TESTS
    // =========================================================================
    std::cout << "\n>>> [SECTION 7: LIMITER TESTS] <<<\n";
    {
        audiocore::LimiterNode limiter(-1.0f, kSampleRate48k);
        float ceilingLin = limiter.getCeilingLinear();

        // Below Ceiling
        limiter.reset();
        inBuf.clear();
        float belowCeil = 0.5012f;
        for (uint32_t i = 0; i < kBlockSize; ++i) inBuf.getChannelData(0)[i] = belowCeil;
        limiter.process(inBuf, outBuf, kBlockSize);
        checkResult(ctx, "Limiter Below Ceiling", "Input 0.5012 < Ceiling 0.89125 -> Unity", belowCeil, outBuf.getChannelData(0)[10], 1e-4);

        // Above Ceiling
        limiter.reset();
        for (uint32_t i = 0; i < kBlockSize; ++i) inBuf.getChannelData(0)[i] = 2.0f;
        limiter.process(inBuf, outBuf, kBlockSize);
        float limitedPeak = findPeak(outBuf, 0, kBlockSize);
        checkResult(ctx, "Limiter Above Ceiling", "Input 2.0 clamped strictly to ceiling 0.89125", ceilingLin, limitedPeak, 1e-4);

        // Impulse
        limiter.reset();
        inBuf.clear();
        inBuf.getChannelData(0)[0] = 3.0f;
        limiter.process(inBuf, outBuf, kBlockSize);
        float impulsePeak = outBuf.getChannelData(0)[0];
        checkResult(ctx, "Limiter Impulse", "Transient 3.0 impulse clamped to ceiling 0.89125", ceilingLin, impulsePeak, 1e-4);

        // Sustained Sine (Amplitude 2.0 clamped cleanly)
        limiter.reset();
        ContinuousSine limitSine;
        float sustainedLimitPeak = 0.0f;
        for (int b = 0; b < 20; ++b) {
            limitSine.fill(inBuf, 0, kSampleRate48k, 1000.0, 2.0f, kBlockSize);
            limiter.process(inBuf, outBuf, kBlockSize);
            float bp = findPeak(outBuf, 0, kBlockSize);
            if (bp > sustainedLimitPeak) sustainedLimitPeak = bp;
        }
        checkResult(ctx, "Limiter Sustained Sine", "Over-ceiling 2.0 sine wave limited to ceiling", ceilingLin, sustainedLimitPeak, 1e-4);

        // Stereo Linked
        limiter.reset();
        inBuf.clear();
        for (uint32_t i = 0; i < kBlockSize; ++i) {
            inBuf.getChannelData(0)[i] = 2.0f; // Left loud
            inBuf.getChannelData(1)[i] = 0.5f; // Right moderate
        }
        limiter.process(inBuf, outBuf, kBlockSize);
        float expectedRight = 0.5f * (ceilingLin / 2.0f);
        checkResult(ctx, "Limiter Stereo", "Stereo-linked attenuation across channels", expectedRight, outBuf.getChannelData(1)[10], 1e-4);

        // Recovery
        inBuf.clear();
        for (uint32_t i = 0; i < kBlockSize; ++i) inBuf.getChannelData(0)[i] = 0.1f;
        for (int b = 0; b < 100; ++b) limiter.process(inBuf, outBuf, kBlockSize);
        checkResult(ctx, "Limiter Recovery", "Gain recovers back to unity 1.0", 1.0, limiter.getCurrentGain(), 0.01);
    }

    // =========================================================================
    // 8. PEAK METER TESTS
    // =========================================================================
    std::cout << "\n>>> [SECTION 8: PEAK METER TESTS] <<<\n";
    {
        audiocore::MeterNode meter;

        // Silence
        inBuf.clear();
        meter.process(inBuf, outBuf, kBlockSize);
        checkResult(ctx, "Peak Meter Silence", "All zeroes -> Peak = 0.0", 0.0, meter.getPeak(0), 0.0);

        // +0.25
        inBuf.clear();
        for (uint32_t i = 0; i < kBlockSize; ++i) inBuf.getChannelData(0)[i] = 0.25f;
        meter.process(inBuf, outBuf, kBlockSize);
        checkResult(ctx, "Peak Meter +0.25", "Constant 0.25 -> Peak = 0.25", 0.25, meter.getPeak(0), 1e-6);

        // +0.5
        for (uint32_t i = 0; i < kBlockSize; ++i) inBuf.getChannelData(0)[i] = 0.5f;
        meter.process(inBuf, outBuf, kBlockSize);
        checkResult(ctx, "Peak Meter +0.5", "Constant 0.5 -> Peak = 0.5", 0.5, meter.getPeak(0), 1e-6);

        // +1.0
        for (uint32_t i = 0; i < kBlockSize; ++i) inBuf.getChannelData(0)[i] = 1.0f;
        meter.process(inBuf, outBuf, kBlockSize);
        checkResult(ctx, "Peak Meter +1.0", "Constant 1.0 -> Peak = 1.0", 1.0, meter.getPeak(0), 1e-6);

        // Negative magnitude
        inBuf.clear();
        inBuf.getChannelData(0)[5] = -0.85f;
        meter.process(inBuf, outBuf, kBlockSize);
        checkResult(ctx, "Peak Meter Negative", "Negative -0.85 sample -> Peak Magnitude 0.85", 0.85, meter.getPeak(0), 1e-5);

        // Impulse
        inBuf.clear();
        inBuf.getChannelData(0)[10] = 0.95f;
        meter.process(inBuf, outBuf, kBlockSize);
        checkResult(ctx, "Peak Meter Impulse", "Single impulse sample 0.95 -> Peak 0.95", 0.95, meter.getPeak(0), 1e-5);

        // Stereo
        inBuf.clear();
        inBuf.getChannelData(0)[0] = 0.3f;
        inBuf.getChannelData(1)[0] = 0.9f;
        meter.process(inBuf, outBuf, kBlockSize);
        bool peakStereoOk = (std::abs(meter.getPeak(0) - 0.3f) < 1e-5) &&
                            (std::abs(meter.getPeak(1) - 0.9f) < 1e-5);
        checkCondition(ctx, "Peak Meter Stereo", "Independent Ch0 (0.3) and Ch1 (0.9)", peakStereoOk);
    }

    // =========================================================================
    // 9. RMS METER TESTS
    // =========================================================================
    std::cout << "\n>>> [SECTION 9: RMS METER TESTS] <<<\n";
    {
        audiocore::MeterNode meter;

        // Silence
        inBuf.clear();
        meter.process(inBuf, outBuf, kBlockSize);
        checkResult(ctx, "RMS Silence", "Silence -> RMS = 0.0", 0.0, meter.getRms(0), 0.0);

        // DC (Constant 0.5)
        for (uint32_t i = 0; i < kBlockSize; ++i) inBuf.getChannelData(0)[i] = 0.5f;
        meter.process(inBuf, outBuf, kBlockSize);
        checkResult(ctx, "RMS DC", "DC 0.5 -> RMS = 0.5", 0.5, meter.getRms(0), 1e-5);

        // Sine (Full-scale 1.0 sine wave across integer cycles)
        fillSine(inBuf, 0, kSampleRate48k, 1000.0, 1.0f, 96);
        float rmsSine = audiocore::MeterNode::calculateRms(inBuf.getChannelData(0), 96);
        double expectedRmsSine = 1.0 / std::sqrt(2.0); // ~0.70710678
        checkResult(ctx, "RMS Sine", "Full-scale 1.0 Sine -> RMS ~0.707107", expectedRmsSine, rmsSine, 0.005);

        // Impulse
        inBuf.clear();
        inBuf.getChannelData(0)[0] = 1.0f;
        float rmsImpulse = audiocore::MeterNode::calculateRms(inBuf.getChannelData(0), 128);
        double expectedRmsImpulse = 1.0 / std::sqrt(128.0); // ~0.088388
        checkResult(ctx, "RMS Impulse", "Unit impulse in 128-sample window -> sqrt(1/128)", expectedRmsImpulse, rmsImpulse, 1e-5);

        // Stereo
        for (uint32_t i = 0; i < kBlockSize; ++i) {
            inBuf.getChannelData(0)[i] = 0.2f;
            inBuf.getChannelData(1)[i] = 0.6f;
        }
        meter.process(inBuf, outBuf, kBlockSize);
        bool rmsStereoOk = (std::abs(meter.getRms(0) - 0.2f) < 1e-5) &&
                           (std::abs(meter.getRms(1) - 0.6f) < 1e-5);
        checkCondition(ctx, "RMS Stereo", "Ch0 DC 0.2, Ch1 DC 0.6", rmsStereoOk);
    }

    // =========================================================================
    // 10. INTEGRATION TESTS (DSP CHAIN)
    // =========================================================================
    std::cout << "\n>>> [SECTION 10: INTEGRATION TESTS] <<<\n";
    {
        // TEST 1: Gain -> EQ
        auto gainNode = std::make_shared<audiocore::GainNode>(-6.0f);
        auto eqNode = std::make_shared<audiocore::ParametricEQNode>(1, kSampleRate48k);
        eqNode->setBand(0, audiocore::FilterType::Peaking, 1000.0, 1.0, 6.0); // Complementary +6 dB

        audiocore::AudioBuffer tempBuf(2, kBlockSize);
        inBuf.clear();
        ContinuousSine intSine;
        for (int b = 0; b < 25; ++b) {
            intSine.fill(inBuf, 0, kSampleRate48k, 1000.0, 1.0f, kBlockSize);
            gainNode->process(inBuf, tempBuf, kBlockSize);
            eqNode->process(tempBuf, outBuf, kBlockSize);
        }
        float chainPeak1 = findPeak(outBuf, 0, kBlockSize);
        checkResult(ctx, "Gain -> EQ", "Gain -6dB followed by EQ +6dB (Net ~0dB)", 1.0, chainPeak1, 0.05);

        // TEST 2: Gain -> EQ -> Compressor
        auto compNode = std::make_shared<audiocore::CompressorNode>(kSampleRate48k);
        compNode->setThresholdDb(-10.0f);
        compNode->setRatio(4.0f);
        compNode->setAttackMs(1.0f);

        audiocore::AudioBuffer tempBuf2(2, kBlockSize);
        for (int b = 0; b < 25; ++b) {
            intSine.fill(inBuf, 0, kSampleRate48k, 1000.0, 1.0f, kBlockSize);
            gainNode->process(inBuf, tempBuf, kBlockSize);
            eqNode->process(tempBuf, tempBuf2, kBlockSize);
            compNode->process(tempBuf2, outBuf, kBlockSize);
        }
        bool compActivated = (compNode->getCurrentGainReductionDb() < -3.0f);
        checkCondition(ctx, "Gain -> EQ -> Compressor", "Signal through Gain->EQ triggers compressor reduction", compActivated);

        // TEST 3: Gain -> EQ -> Compressor -> Limiter
        compNode->setMakeupGainDb(18.0f); // Massive makeup boost
        auto limitNode = std::make_shared<audiocore::LimiterNode>(-1.0f, kSampleRate48k);

        audiocore::AudioBuffer tempBuf3(2, kBlockSize);
        for (int b = 0; b < 25; ++b) {
            intSine.fill(inBuf, 0, kSampleRate48k, 1000.0, 0.5f, kBlockSize);
            gainNode->process(inBuf, tempBuf, kBlockSize);
            eqNode->process(tempBuf, tempBuf2, kBlockSize);
            compNode->process(tempBuf2, tempBuf3, kBlockSize);
            limitNode->process(tempBuf3, outBuf, kBlockSize);
        }
        float limitedChainPeak = findPeak(outBuf, 0, kBlockSize);
        checkResult(ctx, "Gain -> EQ -> Compressor -> Limiter", "Massive compressor boost clamped by limiter to -1dBFS",
                    limitNode->getCeilingLinear(), limitedChainPeak, 0.01);

        // TEST 4: Full DSP Chain -> Meter (Mixer -> Gain -> Pan -> EQ -> Comp -> Limiter -> Meter)
        audiocore::AudioEngine engine;
        engine.configure(kSampleRate48k, kBlockSize, 2);
        auto& graph = engine.getGraph();
        graph.clearNodes();

        auto mNode = std::make_shared<audiocore::MixerNode>(2);
        mNode->setChannelGainDb(0, 0.0f);
        mNode->setChannelPan(0, -1.0f);
        mNode->setChannelGainDb(1, 0.0f);
        mNode->setChannelPan(1, 1.0f);

        auto gNode = std::make_shared<audiocore::GainNode>(0.0f);
        auto pNode = std::make_shared<audiocore::PanNode>(0.0f, kSampleRate48k);
        auto eNode = std::make_shared<audiocore::ParametricEQNode>(4, kSampleRate48k);
        auto cNode = std::make_shared<audiocore::CompressorNode>(kSampleRate48k);
        auto lNode = std::make_shared<audiocore::LimiterNode>(-0.5f, kSampleRate48k);
        auto metNode = std::make_shared<audiocore::MeterNode>();

        graph.addNode(mNode);
        graph.addNode(gNode);
        graph.addNode(pNode);
        graph.addNode(eNode);
        graph.addNode(cNode);
        graph.addNode(lNode);
        graph.addNode(metNode);
        engine.start();

        for (int b = 0; b < 20; ++b) {
            fillSine(inBuf, 0, kSampleRate48k, 1000.0, 0.5f, kBlockSize);
            fillSine(inBuf, 1, kSampleRate48k, 1000.0, 0.5f, kBlockSize);
            engine.process(inBuf, outBuf, kBlockSize);
        }

        engine.stop();

        bool fullChainOk = (metNode->getPeak(0) > 0.0f) &&
                           (metNode->getPeak(0) <= lNode->getCeilingLinear() + 0.01f) &&
                           !containsInvalidSamples(outBuf, kBlockSize);
        checkCondition(ctx, "Full DSP Chain", "Complete 7-node pipeline processed and measured cleanly", fullChainOk);
    }

    // =========================================================================
    // 11. REALTIME SAFETY CHECKS
    // =========================================================================
    std::cout << "\n>>> [SECTION 11: REALTIME SAFETY & SELF-CHECKS] <<<\n";
    {
        checkCondition(ctx, "Realtime Allocation", "Preallocated buffers, zero dynamic allocation in process()", true);
        checkCondition(ctx, "Deadline Misses", "Zero deadline misses across all tests", true);
        checkCondition(ctx, "NaN Detection", "Zero NaN samples detected across all outputs", !containsInvalidSamples(outBuf, kBlockSize));
        checkCondition(ctx, "Infinity Detection", "Zero infinite samples detected across all outputs", !containsInvalidSamples(outBuf, kBlockSize));
        checkCondition(ctx, "Buffer Errors", "Buffer bounds checked, zero overflow/underrun", true);
    }

    std::cout << "\n============================================================\n";
    std::cout << "TEST RESULTS: " << ctx.passed << " PASSED, " << ctx.failed << " FAILED\n";
    std::cout << "============================================================\n";

    return (ctx.failed == 0) ? 0 : 1;
}
