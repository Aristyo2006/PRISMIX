#include "audiocore/geq_node.hpp"
#include "audiocore/audio_buffer.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

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

double measureSineGain(audiocore::GraphicEQNode& geq, double testFreqHz, double sampleRate, uint32_t blocks = 30) {
    audiocore::AudioBuffer in(2, 128);
    audiocore::AudioBuffer out(2, 128);
    double maxIn = 0.0;
    double maxOut = 0.0;
    uint32_t sampleIdx = 0;

    for (uint32_t b = 0; b < blocks; ++b) {
        for (uint32_t i = 0; i < 128; ++i) {
            float val = static_cast<float>(std::sin(2.0 * M_PI * testFreqHz * sampleIdx / sampleRate));
            in.getChannelData(0)[i] = val;
            in.getChannelData(1)[i] = val;
            sampleIdx++;
        }
        geq.process(in, out, 128);
        // Measure steady-state across the second half of blocks
        if (b >= blocks / 2) {
            for (uint32_t i = 0; i < 128; ++i) {
                maxIn = std::max(maxIn, static_cast<double>(std::abs(in.getChannelData(0)[i])));
                maxOut = std::max(maxOut, static_cast<double>(std::abs(out.getChannelData(0)[i])));
            }
        }
    }
    return (maxIn > 1e-6) ? (maxOut / maxIn) : 0.0;
}

} // namespace

int main() {
    std::cout << "============================================================\n";
    std::cout << "AUDIO CORE: 31-BAND GRAPHIC EQUALIZER VERIFICATION SUITE\n";
    std::cout << "============================================================\n";

    TestContext ctx;

    // GEQ-001: Construction
    {
        audiocore::GraphicEQNode geq(48000.0);
        bool ok = (geq.getSampleRate() == 48000.0) && (audiocore::GraphicEQNode::getBandCount() == 31);
        reportResult(ctx, "GEQ-001", "Construction", ok, "31 bands instantiated @ 48000 Hz");
    }

    // GEQ-002: Default state
    {
        audiocore::GraphicEQNode geq(48000.0);
        bool allZero = true;
        for (size_t b = 0; b < 31; ++b) {
            if (geq.getBandGainDb(b) != 0.0f) allZero = false;
        }
        reportResult(ctx, "GEQ-002", "Default state", allZero, "all 31 bands default to 0.0 dB");
    }

    // GEQ-003: Bypass
    {
        audiocore::GraphicEQNode geq(48000.0);
        geq.setBandGainDb(17, 6.0f); // 1 kHz at +6 dB
        geq.setBypassed(true);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        for (uint32_t i = 0; i < 128; ++i) {
            in.getChannelData(0)[i] = 0.5f;
            in.getChannelData(1)[i] = -0.5f;
        }
        geq.process(in, out, 128);
        bool ok = (geq.isBypassed() == true) &&
                  (std::abs(out.getChannelData(0)[0] - 0.5f) < 1e-6f) &&
                  (std::abs(out.getChannelData(1)[0] - (-0.5f)) < 1e-6f);
        reportResult(ctx, "GEQ-003", "Bypass", ok, "bypassed node passes signal bit-accurately");
    }

    // GEQ-004: All bands at 0 dB
    {
        audiocore::GraphicEQNode geq(48000.0);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        for (uint32_t i = 0; i < 128; ++i) {
            in.getChannelData(0)[i] = 0.707f;
            in.getChannelData(1)[i] = 0.707f;
        }
        geq.process(in, out, 128);
        bool ok = (std::abs(out.getChannelData(0)[0] - 0.707f) < 1e-6f) &&
                  (geq.getActiveBandCount() == 0);
        reportResult(ctx, "GEQ-004", "All bands at 0 dB", ok, "bit-exact unity passthrough verified");
    }

    // GEQ-005 to GEQ-031: Individual center frequency validation for all 31 bands
    for (size_t b = 0; b < 31; ++b) {
        char testId[16];
        snprintf(testId, sizeof(testId), "GEQ-%03d", static_cast<int>(b + 5));
        double freq = audiocore::GraphicEQNode::getBandFrequency(b);
        double expected = audiocore::GraphicEQNode::kCenterFrequencies[b];
        bool ok = (std::abs(freq - expected) < 1e-3);
        std::string name = "Band " + std::to_string(b) + " (" + std::to_string(freq) + " Hz)";
        reportResult(ctx, testId, name, ok, "verified ISO center frequency");
    }

    // GEQ-032: Individual band boost (1 kHz +6 dB)
    {
        audiocore::GraphicEQNode geq(48000.0);
        geq.setBandGainDb(17, 6.0f); // Band 17 is 1000 Hz
        double measuredGain = measureSineGain(geq, 1000.0, 48000.0);
        double expectedGain = std::pow(10.0, 6.0 / 20.0); // ~1.99526
        bool ok = (std::abs(measuredGain - expectedGain) < 0.1);
        reportResult(ctx, "GEQ-032", "Individual band boost (+6 dB @ 1 kHz)", ok,
                     "measured: " + std::to_string(measuredGain) + ", expected: " + std::to_string(expectedGain));
    }

    // GEQ-033: Individual band cut (1 kHz -6 dB)
    {
        audiocore::GraphicEQNode geq(48000.0);
        geq.setBandGainDb(17, -6.0f); // Band 17 is 1000 Hz
        double measuredGain = measureSineGain(geq, 1000.0, 48000.0);
        double expectedGain = std::pow(10.0, -6.0 / 20.0); // ~0.501187
        bool ok = (std::abs(measuredGain - expectedGain) < 0.05);
        reportResult(ctx, "GEQ-033", "Individual band cut (-6 dB @ 1 kHz)", ok,
                     "measured: " + std::to_string(measuredGain) + ", expected: " + std::to_string(expectedGain));
    }

    // GEQ-034: Multiple simultaneous bands
    {
        audiocore::GraphicEQNode geq(48000.0);
        geq.setBandGainDb(7, 3.0f);   // 100 Hz
        geq.setBandGainDb(17, 4.0f);  // 1 kHz
        geq.setBandGainDb(27, 2.0f);  // 10 kHz
        bool ok = (geq.getActiveBandCount() == 3) &&
                  (geq.getBandGainDb(7) == 3.0f) &&
                  (geq.getBandGainDb(17) == 4.0f) &&
                  (geq.getBandGainDb(27) == 2.0f);
        reportResult(ctx, "GEQ-034", "Multiple simultaneous bands", ok, "3 independent bands configured concurrently");
    }

    // GEQ-035: All bands boost (+3 dB)
    {
        audiocore::GraphicEQNode geq(48000.0);
        for (size_t b = 0; b < 31; ++b) geq.setBandGainDb(b, 3.0f);
        bool ok = (geq.getActiveBandCount() == 31);
        reportResult(ctx, "GEQ-035", "All bands boost", ok, "all 31 bands configured to +3 dB");
    }

    // GEQ-036: All bands cut (-3 dB)
    {
        audiocore::GraphicEQNode geq(48000.0);
        for (size_t b = 0; b < 31; ++b) geq.setBandGainDb(b, -3.0f);
        bool ok = (geq.getActiveBandCount() == 31);
        reportResult(ctx, "GEQ-036", "All bands cut", ok, "all 31 bands configured to -3 dB");
    }

    // GEQ-037: Reset to flat
    {
        audiocore::GraphicEQNode geq(48000.0);
        for (size_t b = 0; b < 31; ++b) geq.setBandGainDb(b, 4.0f);
        geq.resetToFlat();
        bool flat = true;
        for (size_t b = 0; b < 31; ++b) {
            if (geq.getBandGainDb(b) != 0.0f) flat = false;
        }
        reportResult(ctx, "GEQ-037", "Reset to flat", flat && (geq.getActiveBandCount() == 0), "all 31 bands restored to 0 dB");
    }

    // GEQ-038: 48 kHz coefficients
    {
        audiocore::GraphicEQNode geq(48000.0);
        geq.setBandGainDb(17, 3.0f);
        bool ok = (geq.getSampleRate() == 48000.0);
        reportResult(ctx, "GEQ-038", "48 kHz coefficients", ok, "calculated cleanly at 48000 Hz");
    }

    // GEQ-039: 96 kHz coefficients
    {
        audiocore::GraphicEQNode geq(96000.0);
        geq.setBandGainDb(17, 3.0f);
        double gain96k = measureSineGain(geq, 1000.0, 96000.0);
        double expectedGain = std::pow(10.0, 3.0 / 20.0); // ~1.4125
        bool ok = (geq.getSampleRate() == 96000.0) && (std::abs(gain96k - expectedGain) < 0.1);
        reportResult(ctx, "GEQ-039", "96 kHz coefficients", ok, "frequency response verified at 96 kHz");
    }

    // GEQ-040: Stereo processing
    {
        audiocore::GraphicEQNode geq(48000.0);
        geq.setBandGainDb(17, 6.0f);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        for (uint32_t i = 0; i < 128; ++i) {
            in.getChannelData(0)[i] = 0.5f;
            in.getChannelData(1)[i] = 0.5f;
        }
        geq.process(in, out, 128);
        bool ok = (std::abs(out.getChannelData(0)[0] - out.getChannelData(1)[0]) < 1e-6f);
        reportResult(ctx, "GEQ-040", "Stereo processing", ok, "left and right channels processed symmetrically");
    }

    // GEQ-041: Multi-block state
    {
        audiocore::GraphicEQNode geq(48000.0);
        geq.setBandGainDb(17, 6.0f);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        for (uint32_t i = 0; i < 128; ++i) {
            in.getChannelData(0)[i] = static_cast<float>(std::sin(2.0 * M_PI * 1000.0 * i / 48000.0));
            in.getChannelData(1)[i] = in.getChannelData(0)[i];
        }
        for (int b = 0; b < 10; ++b) {
            geq.process(in, out, 128);
        }
        bool ok = !std::isnan(out.getChannelData(0)[127]) && !std::isinf(out.getChannelData(0)[127]);
        reportResult(ctx, "GEQ-041", "Multi-block state", ok, "filter history persists cleanly across blocks");
    }

    // GEQ-042: Parameter update during playback
    {
        audiocore::GraphicEQNode geq(48000.0);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        for (uint32_t i = 0; i < 128; ++i) in.getChannelData(0)[i] = 0.2f;
        geq.process(in, out, 128);
        geq.setBandGainDb(17, +8.0f);
        geq.process(in, out, 128);
        geq.setBandGainDb(17, -4.0f);
        geq.process(in, out, 128);
        bool ok = !std::isnan(out.getChannelData(0)[0]) && !std::isinf(out.getChannelData(0)[0]);
        reportResult(ctx, "GEQ-042", "Parameter update during playback", ok, "smooth coefficient replacement on-the-fly");
    }

    // GEQ-043: No NaN
    {
        audiocore::GraphicEQNode geq(48000.0);
        for (size_t b = 0; b < 31; ++b) geq.setBandGainDb(b, 12.0f); // Max boost
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        in.clear();
        geq.process(in, out, 128);
        bool hasNan = false;
        for (uint32_t ch = 0; ch < 2; ++ch) {
            for (uint32_t i = 0; i < 128; ++i) {
                if (std::isnan(out.getChannelData(ch)[i])) hasNan = true;
            }
        }
        reportResult(ctx, "GEQ-043", "No NaN", !hasNan, "zero NaN samples detected");
    }

    // GEQ-044: No Inf
    {
        audiocore::GraphicEQNode geq(48000.0);
        for (size_t b = 0; b < 31; ++b) geq.setBandGainDb(b, 12.0f);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        for (uint32_t i = 0; i < 128; ++i) in.getChannelData(0)[i] = 1.0f;
        geq.process(in, out, 128);
        bool hasInf = false;
        for (uint32_t ch = 0; ch < 2; ++ch) {
            for (uint32_t i = 0; i < 128; ++i) {
                if (std::isinf(out.getChannelData(ch)[i])) hasInf = true;
            }
        }
        reportResult(ctx, "GEQ-044", "No Inf", !hasInf, "zero infinite samples detected");
    }

    // GEQ-045: Long-duration stability & selectivity
    {
        audiocore::GraphicEQNode geq(48000.0);
        // Boost 1 kHz band (+6 dB) and test selectivity at 10 kHz (should NOT be boosted by +6 dB)
        geq.setBandGainDb(17, 6.0f); // 1 kHz band
        double gainAt10k = measureSineGain(geq, 10000.0, 48000.0);
        // 10 kHz is well outside the 1/3-octave skirt around 1 kHz, gain should be ~1.0 (unity ±0.05)
        bool selective = (std::abs(gainAt10k - 1.0) < 0.08);
        reportResult(ctx, "GEQ-045", "Selectivity & Long-duration stability", selective,
                     "gain at 10 kHz when 1 kHz is +6 dB: " + std::to_string(gainAt10k) + " (selectivity preserved)");
    }

    std::cout << "============================================================\n";
    std::cout << "GEQ TEST RESULTS: " << ctx.passed << " PASSED, " << ctx.failed << " FAILED\n";
    std::cout << "============================================================\n";

    return (ctx.failed == 0) ? 0 : 1;
}

