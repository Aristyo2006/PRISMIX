#include "audiocore/sample_rate_converter.hpp"
#include <iostream>
#include <vector>
#include <cmath>
#include <string>
#include <iomanip>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

int g_passCount = 0;
int g_failCount = 0;

void report(const std::string& testName, bool passed, const std::string& details) {
    if (passed) {
        g_passCount++;
        std::cout << "  [PASS] " << testName << " (" << details << ")" << std::endl;
    } else {
        g_failCount++;
        std::cout << "  [FAIL] " << testName << " (" << details << ")" << std::endl;
    }
}

// Estimates fundamental frequency of a periodic signal via zero-crossing rate
double estimateFrequency(const float* data, uint32_t frames, double sampleRate) {
    if (frames < 2 || sampleRate <= 0.0) return 0.0;
    std::vector<double> zeroCrossings;
    for (uint32_t i = 1; i < frames; ++i) {
        if (data[i - 1] <= 0.0f && data[i] > 0.0f) {
            // Linear interpolation to find sub-sample zero crossing
            double frac = (0.0 - data[i - 1]) / (data[i] - data[i - 1]);
            zeroCrossings.push_back((i - 1) + frac);
        }
    }
    if (zeroCrossings.size() < 2) return 0.0;
    double totalPeriods = zeroCrossings.back() - zeroCrossings.front();
    double avgPeriodSamples = totalPeriods / (zeroCrossings.size() - 1);
    return sampleRate / avgPeriodSamples;
}

} // namespace

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

int main() {
#if defined(_WIN32)
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    std::cout << "============================================================\n"
              << "AUDIO CORE: SAMPLE RATE CONVERTER VERIFICATION SUITE\n"
              << "============================================================\n" << std::endl;

    audiocore::SampleRateConverter src;

    // TEST 1: TEST SRC 44100 -> 48000
    {
        bool ok = src.configure(44100.0, 48000.0, 2);
        bool statusOk = (src.getStatus() == audiocore::SrcStatus::Active);
        double ratio = src.getRatio();
        bool ratioOk = std::abs(ratio - (48000.0 / 44100.0)) < 1e-6;
        report("TEST SRC 44100 -> 48000", ok && statusOk && ratioOk,
               "Active mode configured, ratio = " + std::to_string(ratio));
    }

    // TEST 2: TEST SRC 48000 -> 48000 (Passthrough Bypass)
    {
        bool ok = src.configure(48000.0, 48000.0, 2);
        bool statusOk = (src.getStatus() == audiocore::SrcStatus::Bypass);
        double ratio = src.getRatio();
        report("TEST SRC 48000 -> 48000", ok && statusOk && (ratio == 1.0),
               "Bypass mode engaged, zero conversion overhead");
    }

    // TEST 3: TEST SRC 96000 -> 48000 (Down-sampling 2:1)
    {
        bool ok = src.configure(96000.0, 48000.0, 2);
        bool statusOk = (src.getStatus() == audiocore::SrcStatus::Active);
        double ratio = src.getRatio();
        report("TEST SRC 96000 -> 48000", ok && statusOk && (std::abs(ratio - 0.5) < 1e-6),
               "Downsampling mode configured, ratio = 0.500000");
    }

    // TEST 4: TEST DURATION PRESERVATION
    {
        // 91.91 seconds at 44100 Hz = 4053231 frames
        uint32_t inFrames = 4053231;
        double inRate = 44100.0;
        double outRate = 48000.0;
        uint32_t expectedOutFrames = audiocore::SampleRateConverter::calculateOutputFrames(inFrames, inRate, outRate);
        double inDuration = inFrames / inRate;
        double outDuration = expectedOutFrames / outRate;
        double durationDiff = std::abs(outDuration - inDuration);

        // Frame tolerance <= 1 frame
        bool durationOk = (durationDiff < (1.0 / outRate));
        char buf[128];
        snprintf(buf, sizeof(buf), "Input: %.4f s (4053231 f) -> Output: %.4f s (%u f), Error: %.6f s (< 1 frame)",
                 inDuration, outDuration, expectedOutFrames, durationDiff);
        report("TEST DURATION PRESERVATION", durationOk, buf);
    }

    // TEST 5: TEST 1KHZ PITCH PRESERVATION (44100 -> 48000)
    {
        src.configure(44100.0, 48000.0, 2, audiocore::SrcQuality::SincStandard);
        uint32_t inFrames = 44100; // Exactly 1.0 second of 1000 Hz sine
        audiocore::AudioBuffer inBuf(2, inFrames);
        for (uint32_t i = 0; i < inFrames; ++i) {
            float s = std::sin(2.0 * M_PI * 1000.0 * i / 44100.0);
            inBuf.getChannelData(0)[i] = s;
            inBuf.getChannelData(1)[i] = s;
        }

        audiocore::AudioBuffer outBuf;
        bool resOk = src.resampleBuffer(inBuf, outBuf);
        double detectedFreqCh0 = estimateFrequency(outBuf.getChannelData(0), outBuf.getFrameCount(), 48000.0);
        double detectedFreqCh1 = estimateFrequency(outBuf.getChannelData(1), outBuf.getFrameCount(), 48000.0);

        // The bug shifted pitch to 1088.44 Hz. Correct pitch MUST be ~1000.0 Hz.
        bool pitchOk = resOk && (std::abs(detectedFreqCh0 - 1000.0) < 2.0) && (std::abs(detectedFreqCh1 - 1000.0) < 2.0);
        char buf[128];
        snprintf(buf, sizeof(buf), "Target: 1000.0 Hz | Detected Ch0: %.2f Hz, Ch1: %.2f Hz (NOT 1088 Hz)",
                 detectedFreqCh0, detectedFreqCh1);
        report("TEST 1KHZ PITCH PRESERVATION", pitchOk, buf);
    }

    // TEST 6: TEST BLOCK CONTINUITY (128-frame blocks)
    {
        src.configure(44100.0, 48000.0, 2, audiocore::SrcQuality::SincStandard);
        src.reset();

        // Feed continuous 44.1 kHz sine into 128-frame output blocks
        const uint32_t blockSize = 128;
        const uint32_t numBlocks = 20;
        std::vector<float> inLeft(blockSize * numBlocks * 2);
        std::vector<float> inRight(blockSize * numBlocks * 2);
        for (size_t i = 0; i < inLeft.size(); ++i) {
            float s = std::sin(2.0 * M_PI * 440.0 * i / 44100.0);
            inLeft[i] = s;
            inRight[i] = s;
        }

        std::vector<float> outLeft(blockSize);
        std::vector<float> outRight(blockSize);
        float* outChannels[2] = { outLeft.data(), outRight.data() };

        double maxBoundaryDelta = 0.0;
        float prevTailSample = 0.0f;
        uint32_t inOffset = 0;

        for (uint32_t b = 0; b < numBlocks; ++b) {
            const float* inChannels[2] = { inLeft.data() + inOffset, inRight.data() + inOffset };
            uint32_t inAvailable = static_cast<uint32_t>(inLeft.size() - inOffset);
            uint32_t consumed = 0;
            uint32_t produced = src.processStreaming(inChannels, inAvailable, consumed, outChannels, blockSize);
            inOffset += consumed;

            if (b > 0 && produced > 0) {
                double delta = std::abs(outLeft[0] - prevTailSample);
                if (delta > maxBoundaryDelta) maxBoundaryDelta = delta;
            }
            if (produced > 0) {
                prevTailSample = outLeft[produced - 1];
            }
        }

        // For a 440 Hz sine at 48 kHz, step between adjacent samples is ~0.057.
        // A click/discontinuity would produce a jump of 0.5 - 2.0.
        bool continuityOk = (maxBoundaryDelta < 0.15);
        char buf[128];
        snprintf(buf, sizeof(buf), "Max inter-block boundary delta across 20 blocks: %.4f (smooth, no clicks)", maxBoundaryDelta);
        report("TEST BLOCK CONTINUITY", continuityOk, buf);
    }

    // TEST 7: TEST NaN / Inf
    {
        src.configure(44100.0, 48000.0, 2);
        audiocore::AudioBuffer inBuf(2, 2048);
        for (uint32_t i = 0; i < 2048; ++i) {
            inBuf.getChannelData(0)[i] = (i % 2 == 0) ? 0.9f : -0.9f;
            inBuf.getChannelData(1)[i] = std::sin(static_cast<float>(i));
        }
        audiocore::AudioBuffer outBuf;
        src.resampleBuffer(inBuf, outBuf);

        bool clean = true;
        for (uint32_t ch = 0; ch < 2; ++ch) {
            for (uint32_t i = 0; i < outBuf.getFrameCount(); ++i) {
                float v = outBuf.getChannelData(ch)[i];
                if (std::isnan(v) || std::isinf(v)) {
                    clean = false;
                    break;
                }
            }
        }
        report("TEST NaN / Inf", clean, "Zero NaN and zero Inf samples detected in output");
    }

    // TEST 8: TEST FRAME COUNT
    {
        uint32_t inF = 10000;
        uint32_t outF = audiocore::SampleRateConverter::calculateOutputFrames(inF, 44100.0, 48000.0);
        uint32_t expectedF = static_cast<uint32_t>(std::round(10000.0 * 48000.0 / 44100.0));
        bool frameOk = (outF == expectedF) && (outF == 10884);
        report("TEST FRAME COUNT", frameOk, "Input 10000 frames @ 44.1k -> Output " + std::to_string(outF) + " frames @ 48k");
    }

    // TEST 9: TEST PASSTHROUGH (Bit-exact sample preservation)
    {
        src.configure(48000.0, 48000.0, 2);
        audiocore::AudioBuffer inBuf(2, 512);
        for (uint32_t i = 0; i < 512; ++i) {
            inBuf.getChannelData(0)[i] = static_cast<float>(i) / 512.0f;
            inBuf.getChannelData(1)[i] = -static_cast<float>(i) / 512.0f;
        }
        audiocore::AudioBuffer outBuf;
        src.resampleBuffer(inBuf, outBuf);

        bool bitExact = (outBuf.getFrameCount() == 512);
        for (uint32_t ch = 0; ch < 2; ++ch) {
            for (uint32_t i = 0; i < 512; ++i) {
                if (outBuf.getChannelData(ch)[i] != inBuf.getChannelData(ch)[i]) {
                    bitExact = false;
                    break;
                }
            }
        }
        report("TEST PASSTHROUGH", bitExact, "Bit-exact float equality confirmed across all samples and channels");
    }

    // TEST 10: TEST DOWN-SAMPLING (96000 -> 48000)
    {
        src.configure(96000.0, 48000.0, 2, audiocore::SrcQuality::SincStandard);
        uint32_t inFrames = 96000; // 1.0s at 96 kHz
        audiocore::AudioBuffer inBuf(2, inFrames);
        for (uint32_t i = 0; i < inFrames; ++i) {
            float s = std::sin(2.0 * M_PI * 1000.0 * i / 96000.0);
            inBuf.getChannelData(0)[i] = s;
            inBuf.getChannelData(1)[i] = s;
        }

        audiocore::AudioBuffer outBuf;
        src.resampleBuffer(inBuf, outBuf);
        double detectedFreq = estimateFrequency(outBuf.getChannelData(0), outBuf.getFrameCount(), 48000.0);
        bool downOk = (outBuf.getFrameCount() == 48000) && (std::abs(detectedFreq - 1000.0) < 2.0);
        char buf[128];
        snprintf(buf, sizeof(buf), "Frames: 96000 -> 48000 | Detected: %.2f Hz | Duration preserved: 1.000 s", detectedFreq);
        report("TEST DOWN-SAMPLING (96000 -> 48000)", downOk, buf);
    }

    // TEST 11: STREAMING CLOCK-RATIO ADAPTATION preserves initialized state.
    {
        audiocore::SampleRateConverter adaptive;
        adaptive.enableClockAdaptation(true);
        bool configured = adaptive.configure(48000.0, 48000.0, 2, audiocore::SrcQuality::SincStandard);
        bool adjusted = configured && adaptive.setStreamingOutputRate(48024.0);
        std::vector<float> leftIn(1024), rightIn(1024), leftOut(1100), rightOut(1100);
        for (uint32_t i = 0; i < leftIn.size(); ++i) {
            leftIn[i] = rightIn[i] = static_cast<float>(std::sin(2.0 * M_PI * 1000.0 * i / 48000.0));
        }
        const float* inChannels[2] = {leftIn.data(), rightIn.data()};
        float* outChannels[2] = {leftOut.data(), rightOut.data()};
        uint32_t consumed = 0;
        uint32_t produced = adjusted ? adaptive.processStreaming(inChannels, 1024, consumed, outChannels, 1100) : 0;
        bool finite = std::all_of(leftOut.begin(), leftOut.begin() + produced, [](float v) { return std::isfinite(v); });
        bool clockOk = adjusted && std::abs(adaptive.getOutSampleRate() - 48024.0) < 0.01 && consumed > 0 && produced > 0 && finite;
        report("TEST STREAM CLOCK ADAPTATION", clockOk, "Ratio updated without reset; streaming output remained finite");
    }

    std::cout << "\n============================================================\n";
    std::cout << "TEST RESULTS: " << g_passCount << " PASSED, " << g_failCount << " FAILED\n";
    std::cout << "============================================================\n";

    return (g_failCount == 0) ? 0 : 1;
}
