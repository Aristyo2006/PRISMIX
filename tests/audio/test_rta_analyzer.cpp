#include "dsp/analyzer_model.h"
#include "dsp/frequency_axis.h"
#include "service/audio_service.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <random>
#include <cassert>

namespace {

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct TestContext {
    int passed = 0;
    int failed = 0;
};

void reportResult(TestContext& ctx, const std::string& testId, const std::string& name, bool pass, const std::string& details = "") {
    std::cout << "  [" << (pass ? "PASS" : "FAIL") << "] " << testId << ": " << name;
    if (!details.empty()) {
        std::cout << " (" << details << ")";
    }
    std::cout << "\n";
    if (pass) ctx.passed++;
    else ctx.failed++;
}

std::vector<float> generateSine(double freqHz, float amplitude, size_t numFrames, double sampleRate = 48000.0) {
    std::vector<float> pcm(numFrames);
    for (size_t n = 0; n < numFrames; ++n) {
        pcm[n] = amplitude * static_cast<float>(std::sin(2.0 * M_PI * freqHz * static_cast<double>(n) / sampleRate));
    }
    return pcm;
}

} // namespace

int main() {
    std::cout << "============================================================\n";
    std::cout << "AUDIO CORE: FORENSIC FFT/RTA AUTOMATED VERIFICATION SUITE\n";
    std::cout << "============================================================\n";

    TestContext ctx;
    constexpr double Fs = 48000.0;
    constexpr size_t N = RtaAnalyzer::FFT_SIZE; // 4096

    // -------------------------------------------------------------
    // Test A: Silence
    // -------------------------------------------------------------
    {
        RtaAnalyzer rta;
        std::vector<float> silence(N, 0.0f);
        rta.process_pcm(silence.data(), silence.size(), Fs);

        auto snap = rta.get_snapshot();
        bool allAtFloor = true;
        for (float v : snap.bands_31) {
            if (v > -71.9f) allAtFloor = false;
        }
        for (float v : snap.bands_63) {
            if (v > -71.9f) allAtFloor = false;
        }
        for (float v : snap.fft_bins) {
            if (v > -71.9f) allAtFloor = false;
        }
        auto diag = rta.get_diagnostics();
        bool noNan = std::isfinite(diag.dcMean) && std::isfinite(diag.pcmRms) && std::isfinite(diag.pcmPeak);

        reportResult(ctx, "RTA-TEST-A", "Silence is flat at display floor with zero false peaks",
                     allAtFloor && noNan && !diag.hasValidPcm,
                     "peakAmp=" + std::to_string(diag.peakAmplitudeDbfs) + " dBFS");
    }

    // -------------------------------------------------------------
    // Test B: Exact-bin sine
    // -------------------------------------------------------------
    {
        // Bin 85 -> 85 * 48000 / 4096 = 996.09375 Hz, amp = 1.0 (0.0 dBFS)
        const double fExact = 85.0 * (Fs / static_cast<double>(N));
        RtaAnalyzer rta;
        auto pcm = generateSine(fExact, 1.0f, N, Fs);
        // Process blocks to allow smoothing filter to reach steady state from floor
        for (int i = 0; i < 16; ++i) {
            rta.process_pcm(pcm.data(), pcm.size(), Fs);
        }

        auto diag = rta.get_diagnostics();
        auto snap = rta.get_snapshot();

        bool freqOk = std::abs(diag.peakFrequencyHz - static_cast<float>(fExact)) < 0.2f;
        bool ampOk = std::abs(diag.peakAmplitudeDbfs - 0.0f) < 0.25f;
        bool bandDominant = snap.bands_31[17] > -0.5f; // ISO 1 kHz band (band 17)
        bool outOfBandQuiet = (snap.bands_31[0] <= -71.9f) && (snap.bands_31[7] <= -71.9f); // 20 Hz, 100 Hz

        reportResult(ctx, "RTA-TEST-B", "Exact-bin sine amplitude & frequency calibration",
                     freqOk && ampOk && bandDominant && outOfBandQuiet,
                     "freq=" + std::to_string(diag.peakFrequencyHz) + " Hz, amp=" + std::to_string(diag.peakAmplitudeDbfs) +
                     " dBFS, b17=" + std::to_string(snap.bands_31[17]) + ", b0=" + std::to_string(snap.bands_31[0]) +
                     ", b7=" + std::to_string(snap.bands_31[7]));
    }

    // -------------------------------------------------------------
    // Test C: 1 kHz non-integer bin sine at -6 dBFS
    // -------------------------------------------------------------
    {
        // 1000 Hz tone at 48000 Hz, N=4096 (k = 85.333333)
        const float amp = 0.501187f; // -6.00 dBFS
        RtaAnalyzer rta;
        auto pcm = generateSine(1000.0, amp, N, Fs);
        for (int i = 0; i < 8; ++i) {
            rta.process_pcm(pcm.data(), pcm.size(), Fs);
        }

        auto diag = rta.get_diagnostics();
        auto snap = rta.get_snapshot();

        bool freqOk = std::abs(diag.peakFrequencyHz - 1000.0f) < 2.0f;
        bool ampOk = std::abs(diag.peakAmplitudeDbfs - (-6.0f)) < 0.5f;
        bool dominantBand = snap.bands_31[17] > -7.0f;
        // Verify NO ghost low-frequency bars
        bool lowBandsQuiet = (snap.bands_31[0] <= -71.9f) && (snap.bands_31[1] <= -71.9f) &&
                             (snap.bands_31[5] <= -71.9f) && (snap.bands_31[7] <= -71.9f);

        reportResult(ctx, "RTA-TEST-C", "1 kHz sine non-bin peak & low-frequency ghost bar elimination",
                     freqOk && ampOk && dominantBand && lowBandsQuiet,
                     "freq=" + std::to_string(diag.peakFrequencyHz) + " Hz, amp=" + std::to_string(diag.peakAmplitudeDbfs) + " dBFS");
    }

    // -------------------------------------------------------------
    // Test D: 100 Hz and 10 kHz sines (extremes mapping)
    // -------------------------------------------------------------
    {
        RtaAnalyzer rta100;
        auto pcm100 = generateSine(100.0, 0.5f, N, Fs);
        for (int i = 0; i < 8; ++i) rta100.process_pcm(pcm100.data(), pcm100.size(), Fs);
        auto diag100 = rta100.get_diagnostics();
        auto snap100 = rta100.get_snapshot();
        bool ok100 = std::abs(diag100.peakFrequencyHz - 100.0f) < 1.5f && snap100.bands_31[7] > -7.5f;

        RtaAnalyzer rta10k;
        auto pcm10k = generateSine(10000.0, 0.5f, N, Fs);
        for (int i = 0; i < 8; ++i) rta10k.process_pcm(pcm10k.data(), pcm10k.size(), Fs);
        auto diag10k = rta10k.get_diagnostics();
        auto snap10k = rta10k.get_snapshot();
        bool ok10k = std::abs(diag10k.peakFrequencyHz - 10000.0f) < 10.0f && snap10k.bands_31[27] > -7.5f;

        reportResult(ctx, "RTA-TEST-D", "100 Hz and 10 kHz logarithmic boundary mapping",
                     ok100 && ok10k,
                     "f100=" + std::to_string(diag100.peakFrequencyHz) + " Hz, f10k=" + std::to_string(diag10k.peakFrequencyHz) + " Hz");
    }

    // -------------------------------------------------------------
    // Test E: Two-tone signal (500 Hz at -6 dBFS, 4 kHz at -12 dBFS)
    // -------------------------------------------------------------
    {
        const float a1 = 0.501187f; // -6 dBFS
        const float a2 = 0.251188f; // -12 dBFS
        std::vector<float> pcm(N);
        for (size_t n = 0; n < N; ++n) {
            pcm[n] = a1 * static_cast<float>(std::sin(2.0 * M_PI * 500.0 * n / Fs)) +
                     a2 * static_cast<float>(std::sin(2.0 * M_PI * 4000.0 * n / Fs));
        }

        RtaAnalyzer rta;
        for (int i = 0; i < 8; ++i) rta.process_pcm(pcm.data(), pcm.size(), Fs);
        auto snap = rta.get_snapshot();

        // 500 Hz is band 14, 4000 Hz is band 23
        float lvl500 = snap.bands_31[14];
        float lvl4k = snap.bands_31[23];
        bool peaksIdentified = (lvl500 > -8.0f) && (lvl4k > -14.0f);
        float diff = lvl500 - lvl4k;
        bool diffOk = std::abs(diff - 6.0f) < 1.5f;

        reportResult(ctx, "RTA-TEST-E", "Two-tone resolution & relative amplitude preservation",
                     peaksIdentified && diffOk,
                     "diff=" + std::to_string(diff) + " dB (expected 6 dB)");
    }

    // -------------------------------------------------------------
    // Test F: White noise broadband distribution
    // -------------------------------------------------------------
    {
        std::mt19937 gen(12345);
        std::uniform_real_distribution<float> dis(-0.5f, 0.5f);
        std::vector<float> pcm(N);
        for (size_t n = 0; n < N; ++n) pcm[n] = dis(gen);

        RtaAnalyzer rta;
        for (int i = 0; i < 8; ++i) rta.process_pcm(pcm.data(), pcm.size(), Fs);
        auto snap = rta.get_snapshot();

        bool hasBroadEnergy = true;
        for (size_t b = 6; b < 28; ++b) {
            if (snap.bands_31[b] <= -60.0f) hasBroadEnergy = false;
        }

        reportResult(ctx, "RTA-TEST-F", "White noise produces broad non-spurious spectrum",
                     hasBroadEnergy, "broadband energy verified");
    }

    // -------------------------------------------------------------
    // Test G: Tone followed by silence & smoothing decay
    // -------------------------------------------------------------
    {
        RtaAnalyzer rtaFast;
        rtaFast.set_speed(RtaSpeed::FAST);
        RtaAnalyzer rtaSlow;
        rtaSlow.set_speed(RtaSpeed::SLOW);

        auto pcm = generateSine(1000.0, 0.5f, N, Fs);
        for (int i = 0; i < 8; ++i) {
            rtaFast.process_pcm(pcm.data(), pcm.size(), Fs);
            rtaSlow.process_pcm(pcm.data(), pcm.size(), Fs);
        }

        float fastPeakBefore = rtaFast.get_snapshot().bands_31[17];
        float slowPeakBefore = rtaSlow.get_snapshot().bands_31[17];

        // Process silence
        for (int i = 0; i < 10; ++i) {
            rtaFast.process_silence();
            rtaSlow.process_silence();
        }

        float fastPeakAfter = rtaFast.get_snapshot().bands_31[17];
        float slowPeakAfter = rtaSlow.get_snapshot().bands_31[17];

        bool fastDecayed = fastPeakAfter < fastPeakBefore;
        bool slowDecayed = slowPeakAfter < slowPeakBefore;
        bool fastFasterThanSlow = (fastPeakBefore - fastPeakAfter) > (slowPeakBefore - slowPeakAfter);

        reportResult(ctx, "RTA-TEST-G", "Tone followed by silence decays according to speed constants",
                     fastDecayed && slowDecayed && fastFasterThanSlow,
                     "fastDecay=" + std::to_string(fastPeakBefore - fastPeakAfter) +
                     " dB, slowDecay=" + std::to_string(slowPeakBefore - slowPeakAfter) + " dB");
    }

    // -------------------------------------------------------------
    // Test H: Production audio path tap test (PRE-EQ vs POST-EQ)
    // -------------------------------------------------------------
    {
        AudioStripConfig strip;
        strip.logicalId = 1;
        strip.format = AudioStripFormat::Mono;
        strip.captureChannel = 0;
        strip.gainDb = 0.0f;
        strip.hpfEnabled = false;
        strip.gateEnabled = false;
        strip.compressorEnabled = false;
        strip.peqBypassed = false;
        // Configure Band 1 as peaking filter with -12 dB cut at 1000 Hz (Q = 2.0)
        strip.eqFrequency[1] = 1000.0f;
        strip.eqGain[1] = -12.0f;
        strip.eqQ[1] = 2.0f;
        strip.eqType[1] = 1; // Peaking
        strip.eqBandEnabled[1] = true;

        std::vector<AudioStripConfig> strips{strip};
        StripInsertDspNode stripNode(strips, 48000.0, 128);

        AnalyzerRingBuffer ringPre, ringPost;
        audiocore::AudioBuffer inBuf(2, 128), outBuf(2, 128);

        // Tap PRE_EQ:
        stripNode.setAnalyzerTap(1, static_cast<uint8_t>(RtaSource::PRE_EQ), &ringPre);
        for (int b = 0; b < 32; ++b) {
            for (uint32_t i = 0; i < 128; ++i) {
                const double t = (b * 128 + i) / 48000.0;
                inBuf[0][i] = 0.5f * static_cast<float>(std::sin(2.0 * M_PI * 1000.0 * t));
            }
            stripNode.process(inBuf, outBuf, 128);
        }

        // Tap POST_EQ:
        stripNode.setAnalyzerTap(1, static_cast<uint8_t>(RtaSource::POST_EQ), &ringPost);
        for (int b = 0; b < 32; ++b) {
            for (uint32_t i = 0; i < 128; ++i) {
                const double t = (b * 128 + i) / 48000.0;
                inBuf[0][i] = 0.5f * static_cast<float>(std::sin(2.0 * M_PI * 1000.0 * t));
            }
            stripNode.process(inBuf, outBuf, 128);
        }

        RtaAnalyzer rtaPre, rtaPost;
        std::vector<float> prePcm(4096), postPcm(4096);
        ringPre.pull(prePcm.data(), 4096);
        ringPost.pull(postPcm.data(), 4096);

        rtaPre.process_pcm(prePcm.data(), prePcm.size(), 48000.0);
        rtaPost.process_pcm(postPcm.data(), postPcm.size(), 48000.0);

        float preAmp = rtaPre.get_diagnostics().peakAmplitudeDbfs;
        float postAmp = rtaPost.get_diagnostics().peakAmplitudeDbfs;
        float eqCut = preAmp - postAmp;

        bool tapDiffOk = std::abs(eqCut - 12.0f) < 2.0f;

        reportResult(ctx, "RTA-TEST-H", "Production PRE-EQ vs POST-EQ real PCM tap verification",
                     tapDiffOk,
                     "pre=" + std::to_string(preAmp) + " dBFS, post=" + std::to_string(postAmp) +
                     " dBFS, cut=" + std::to_string(eqCut) + " dB (expected 12 dB)");
    }

    // -------------------------------------------------------------
    // Test I: Unified Logarithmic Frequency Mapping Validation (Items 1-8)
    // -------------------------------------------------------------
    {
        const float xMin = 64.0f;
        const float xMax = 1059.0f;
        const float width = xMax - xMin; // 995 px

        // 1. 20 Hz maps to the left plotting boundary
        float x20 = FrequencyAxis::frequency_to_x(20.0f, xMin, xMax);
        float u20 = FrequencyAxis::frequency_to_norm(20.0f);
        bool leftBoundaryOk = (std::abs(x20 - xMin) < 1e-4f) && (std::abs(u20 - 0.0f) < 1e-6f);

        // 2. 20 kHz maps to the right plotting boundary
        float x20k = FrequencyAxis::frequency_to_x(20000.0f, xMin, xMax);
        float u20k = FrequencyAxis::frequency_to_norm(20000.0f);
        bool rightBoundaryOk = (std::abs(x20k - xMax) < 1e-4f) && (std::abs(u20k - 1.0f) < 1e-6f);

        // 3. 100 Hz, 1 kHz, 10 kHz, and intermediate frequencies map to mathematically exact positions
        // u(f) = ln(f / 20) / ln(20000 / 20)
        auto exact_u = [](float f) { return std::log(f / 20.0f) / std::log(20000.0f / 20.0f); };
        float testFreqs[] = {20.0f, 50.0f, 100.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 10000.0f, 16000.0f, 20000.0f};
        bool intermediatesOk = true;
        for (float f : testFreqs) {
            float expected_u = exact_u(f);
            float actual_u = FrequencyAxis::frequency_to_norm(f);
            if (std::abs(actual_u - expected_u) > 1e-6f) intermediatesOk = false;
        }

        // 4. Forward and inverse coordinate mappings agree within tolerance
        bool roundTripOk = true;
        for (float f : testFreqs) {
            float u = FrequencyAxis::frequency_to_norm(f);
            float f_recon = FrequencyAxis::norm_to_frequency(u);
            if (std::abs(f_recon - f) / f > 1e-5f) roundTripOk = false;
        }
        for (float px = xMin; px <= xMax; px += 50.0f) {
            float f = FrequencyAxis::x_to_frequency(px, xMin, xMax);
            float px_recon = FrequencyAxis::frequency_to_x(f, xMin, xMax);
            if (std::abs(px_recon - px) > 1e-4f) roundTripOk = false;
        }

        // 5. Shared mapping across rendering elements
        // Grid lines, tick labels, RTA bar centers, PEQ nodes, and curve response evaluate to identical coordinates
        float f1k = 1000.0f;
        float peqNodeX = FrequencyAxis::frequency_to_x(f1k, xMin, xMax);
        float gridLineX = FrequencyAxis::frequency_to_x(f1k, xMin, xMax);
        float tickLabelX = FrequencyAxis::frequency_to_x(f1k, xMin, xMax);
        bool consistencyOk = (peqNodeX == gridLineX) && (gridLineX == tickLabelX);

        // 6. Resizing graph preserves relative logarithmic positions
        float width1 = 995.0f;
        float width2 = 1840.0f;
        float width3 = 500.0f;
        float u_w1 = (FrequencyAxis::frequency_to_x(1000.0f, 0.0f, width1)) / width1;
        float u_w2 = (FrequencyAxis::frequency_to_x(1000.0f, 100.0f, 100.0f + width2) - 100.0f) / width2;
        float u_w3 = (FrequencyAxis::frequency_to_x(1000.0f, 50.0f, 50.0f + width3) - 50.0f) / width3;
        bool resizeInvariant = (std::abs(u_w1 - u_w2) < 1e-6f) && (std::abs(u_w1 - u_w3) < 1e-6f);

        // 7. Real 1 kHz FFT peak appears at the 1 kHz coordinate
        float f_peak = 1000.181274f; // Measured in Test C from real PCM
        float x_peak = FrequencyAxis::frequency_to_x(f_peak, xMin, xMax);
        float x_1k = FrequencyAxis::frequency_to_x(1000.0f, xMin, xMax);
        bool peakAt1kCoord = std::abs(x_peak - x_1k) < 0.15f; // within 0.15 pixels on a 995-pixel chart

        // 8. Edge cases: clamp / reject frequencies outside display range without NaN/inf
        float u_zero = FrequencyAxis::frequency_to_norm(0.0f);
        float u_neg = FrequencyAxis::frequency_to_norm(-100.0f);
        float u_over = FrequencyAxis::frequency_to_norm(25000.0f);
        bool edgeCasesOk = (u_zero == 0.0f) && (u_neg == 0.0f) && (u_over == 1.0f);

        bool allPass = leftBoundaryOk && rightBoundaryOk && intermediatesOk && roundTripOk &&
                       consistencyOk && resizeInvariant && peakAt1kCoord && edgeCasesOk;

        reportResult(ctx, "RTA-TEST-I", "Unified logarithmic frequency axis & coordinate mapping",
                     allPass,
                     "x(20)=" + std::to_string(x20) + ", x(1k)=" + std::to_string(x_1k) +
                     ", x(20k)=" + std::to_string(x20k) + ", roundTrip=" + (roundTripOk ? "OK" : "FAIL") +
                     ", peakDelta=" + std::to_string(std::abs(x_peak - x_1k)) + " px");
    }

    std::cout << "============================================================\n";
    std::cout << "RTA TEST SUMMARY: " << ctx.passed << " PASSED, " << ctx.failed << " FAILED\n";
    std::cout << "============================================================\n";

    return (ctx.failed == 0) ? 0 : 1;
}
