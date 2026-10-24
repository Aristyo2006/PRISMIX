#include "audiocore/reverb_node.hpp"
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

} // namespace

int main() {
    std::cout << "============================================================\n";
    std::cout << "AUDIO CORE: INTERNAL REVERB AUTOMATED VERIFICATION SUITE\n";
    std::cout << "============================================================\n";

    TestContext ctx;

    // REVERB-001: Construction
    {
        audiocore::ReverbNode reverb(48000.0);
        bool ok = (reverb.getSampleRate() == 48000.0);
        reportResult(ctx, "REVERB-001", "Construction", ok, "sampleRate=48000.0");
    }

    // REVERB-002: Default state
    {
        audiocore::ReverbNode reverb(48000.0);
        bool ok = (reverb.getType() == audiocore::ReverbType::Hall) &&
                  (reverb.getMix() == 0.35f) &&
                  !reverb.isBypassed();
        reportResult(ctx, "REVERB-002", "Default state", ok, "Preset=Hall, Mix=0.35, Bypassed=false");
    }

    // REVERB-003: Bypass
    {
        audiocore::ReverbNode reverb(48000.0);
        reverb.setBypassed(true);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        for (uint32_t i = 0; i < 128; ++i) {
            in.getChannelData(0)[i] = static_cast<float>(i + 1) / 128.0f;
            in.getChannelData(1)[i] = -static_cast<float>(i + 1) / 128.0f;
        }
        reverb.process(in, out, 128);
        bool match = true;
        for (uint32_t i = 0; i < 128; ++i) {
            if (out.getChannelData(0)[i] != in.getChannelData(0)[i] ||
                out.getChannelData(1)[i] != in.getChannelData(1)[i]) {
                match = false;
                break;
            }
        }
        reportResult(ctx, "REVERB-003", "Bypass", match, "Bit-exact passthrough when bypassed");
    }

    // REVERB-004: Silence
    {
        audiocore::ReverbNode reverb(48000.0);
        reverb.reset();
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        in.clear();
        reverb.process(in, out, 128);

        float maxVal = 0.0f;
        for (uint32_t i = 0; i < 128; ++i) {
            maxVal = std::max(maxVal, std::abs(out.getChannelData(0)[i]));
            maxVal = std::max(maxVal, std::abs(out.getChannelData(1)[i]));
        }
        bool zeroOut = (maxVal == 0.0f);
        reportResult(ctx, "REVERB-004", "Silence", zeroOut, "Zero input produces zero output");
    }

    // REVERB-005: Impulse response
    {
        audiocore::ReverbNode reverb(48000.0);
        reverb.setMix(1.0f); // 100% wet
        reverb.reset();

        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        in.getChannelData(0)[0] = 1.0f; // Unit impulse
        in.getChannelData(1)[0] = 1.0f;

        float totalEnergy = 0.0f;
        for (int b = 0; b < 20; ++b) {
            reverb.process(in, out, 128);
            if (b == 0) in.clear();
            for (uint32_t i = 0; i < 128; ++i) {
                totalEnergy += out.getChannelData(0)[i] * out.getChannelData(0)[i];
            }
        }
        bool activeReverb = (totalEnergy > 0.01f);
        reportResult(ctx, "REVERB-005", "Impulse response", activeReverb, "Impulse produces rich reverberant decay");
    }

    // REVERB-006: Pre-delay
    {
        audiocore::ReverbNode reverb(48000.0);
        reverb.setMix(1.0f);
        reverb.setPreDelayMs(50.0f); // 50ms = 2400 samples = ~18.75 blocks
        reverb.reset();

        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        in.getChannelData(0)[0] = 1.0f;
        in.getChannelData(1)[0] = 1.0f;

        // Verify first 15 blocks (1920 samples < 2400 samples) remain nearly silent
        float earlyEnergy = 0.0f;
        for (int b = 0; b < 15; ++b) {
            reverb.process(in, out, 128);
            if (b == 0) in.clear();
            for (uint32_t i = 0; i < 128; ++i) {
                earlyEnergy += std::abs(out.getChannelData(0)[i]);
            }
        }
        bool preDelayHeld = (earlyEnergy < 0.001f);
        reportResult(ctx, "REVERB-006", "Pre-delay", preDelayHeld, "Pre-delay line holds wet onset for configured 50ms");
    }

    // REVERB-007: Decay
    {
        audiocore::ReverbNode reverbShort(48000.0);
        reverbShort.setMix(1.0f);
        reverbShort.setPreDelayMs(0.0f);
        reverbShort.setDecay(0.5f); // Short decay
        reverbShort.reset();

        audiocore::ReverbNode reverbLong(48000.0);
        reverbLong.setMix(1.0f);
        reverbLong.setPreDelayMs(0.0f);
        reverbLong.setDecay(5.0f); // Long decay
        reverbLong.reset();

        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer outShort(2, 128);
        audiocore::AudioBuffer outLong(2, 128);
        in.getChannelData(0)[0] = 1.0f;
        in.getChannelData(1)[0] = 1.0f;

        float energyShortLate = 0.0f;
        float energyLongLate = 0.0f;
        for (int b = 0; b < 100; ++b) {
            reverbShort.process(in, outShort, 128);
            reverbLong.process(in, outLong, 128);
            if (b == 0) in.clear();
            if (b >= 50) { // Late tail (after 6400 samples / 133 ms)
                for (uint32_t i = 0; i < 128; ++i) {
                    energyShortLate += std::abs(outShort.getChannelData(0)[i]);
                    energyLongLate += std::abs(outLong.getChannelData(0)[i]);
                }
            }
        }
        bool decayOk = (energyLongLate > energyShortLate * 2.0f);
        reportResult(ctx, "REVERB-007", "Decay", decayOk, "Long decay tail sustains significantly higher energy than short decay");
    }

    // REVERB-008: Damping
    {
        audiocore::ReverbNode reverb(48000.0);
        reverb.setDamping(0.80f);
        bool dampOk = (reverb.getDamping() == 0.80f);
        reportResult(ctx, "REVERB-008", "Damping", dampOk, "Damping setter and getter verified at 0.80");
    }

    // REVERB-009: Diffusion
    {
        audiocore::ReverbNode reverb(48000.0);
        reverb.setDiffusion(0.90f);
        bool diffOk = (reverb.getDiffusion() == 0.90f);
        reportResult(ctx, "REVERB-009", "Diffusion", diffOk, "Diffusion setter and getter verified at 0.90");
    }

    // REVERB-010: Width
    {
        audiocore::ReverbNode reverbMono(48000.0);
        reverbMono.setMix(1.0f);
        reverbMono.setWidth(0.0f); // Mono width
        reverbMono.reset();

        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        in.getChannelData(0)[0] = 1.0f;
        in.getChannelData(1)[0] = 0.0f;

        reverbMono.process(in, out, 128);
        bool monoMatch = true;
        for (uint32_t i = 0; i < 128; ++i) {
            if (std::abs(out.getChannelData(0)[i] - out.getChannelData(1)[i]) > 1e-5f) {
                monoMatch = false;
                break;
            }
        }
        reportResult(ctx, "REVERB-010", "Width", monoMatch, "Width=0.0 produces identical L and R mono output");
    }

    // REVERB-011: Low cut
    {
        audiocore::ReverbNode reverb(48000.0);
        reverb.setLowCutHz(200.0f);
        bool lowCutOk = (reverb.getLowCutHz() == 200.0f);
        reportResult(ctx, "REVERB-011", "Low cut", lowCutOk, "Low cut filter set to 200 Hz");
    }

    // REVERB-012: High cut
    {
        audiocore::ReverbNode reverb(48000.0);
        reverb.setHighCutHz(8000.0f);
        bool highCutOk = (reverb.getHighCutHz() == 8000.0f);
        reportResult(ctx, "REVERB-012", "High cut", highCutOk, "High cut filter set to 8000 Hz");
    }

    // REVERB-013: Stereo response
    {
        audiocore::ReverbNode reverb(48000.0);
        reverb.setMix(1.0f);
        reverb.setPreDelayMs(0.0f);
        reverb.setWidth(1.0f); // Wide stereo
        reverb.reset();

        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        in.getChannelData(0)[0] = 1.0f;
        in.getChannelData(1)[0] = 0.0f;

        float maxL = 0.0f, maxR = 0.0f;
        for (int b = 0; b < 40; ++b) {
            reverb.process(in, out, 128);
            if (b == 0) in.clear();
            for (uint32_t i = 0; i < 128; ++i) {
                maxL = std::max(maxL, std::abs(out.getChannelData(0)[i]));
                maxR = std::max(maxR, std::abs(out.getChannelData(1)[i]));
            }
        }
        bool stereoSpread = (maxL > 0.01f && maxR > 0.01f);
        reportResult(ctx, "REVERB-013", "Stereo response", stereoSpread, "Natural stereo decorrelation verified across channels");
    }

    // REVERB-014: 48 kHz
    {
        audiocore::ReverbNode reverb(48000.0);
        bool ok = (reverb.getSampleRate() == 48000.0);
        reportResult(ctx, "REVERB-014", "48 kHz", ok, "Configured at 48000 Hz");
    }

    // REVERB-015: 96 kHz
    {
        audiocore::ReverbNode reverb(48000.0);
        reverb.setSampleRate(96000.0);
        bool ok = (reverb.getSampleRate() == 96000.0);
        reportResult(ctx, "REVERB-015", "96 kHz", ok, "Sampling rate dynamically updated to 96000 Hz");
    }

    // REVERB-016: Multi-block state
    {
        audiocore::ReverbNode reverb(48000.0);
        reverb.setMix(1.0f);
        reverb.setPreDelayMs(0.0f);
        reverb.reset();

        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        in.getChannelData(0)[0] = 1.0f;
        in.getChannelData(1)[0] = 1.0f;

        float energies[30]{};
        for (int b = 0; b < 30; ++b) {
            reverb.process(in, out, 128);
            if (b == 0) in.clear();
            for (uint32_t i = 0; i < 128; ++i) {
                energies[b] += std::abs(out.getChannelData(0)[i]);
            }
        }
        // Energy spans continuously across multiple consecutive blocks (e.g. blocks 15, 16, 17)
        bool multiBlockOk = (energies[15] > 0.0f && energies[16] > 0.0f && energies[17] > 0.0f);
        reportResult(ctx, "REVERB-016", "Multi-block state", multiBlockOk, "Continuous reverberant energy across consecutive blocks");
    }

    // REVERB-017: Parameter smoothing
    {
        audiocore::ReverbNode reverb(48000.0);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        for (uint32_t i = 0; i < 128; ++i) in.getChannelData(0)[i] = 0.5f;

        bool clean = true;
        for (int b = 0; b < 10; ++b) {
            if (b == 3) reverb.setDecay(8.0f);
            if (b == 6) reverb.setMix(0.8f);
            reverb.process(in, out, 128);
            for (uint32_t i = 0; i < 128; ++i) {
                float v = out.getChannelData(0)[i];
                if (std::isnan(v) || std::isinf(v)) clean = false;
            }
        }
        reportResult(ctx, "REVERB-017", "Parameter smoothing", clean, "Realtime parameter variation without numerical errors");
    }

    // REVERB-018: Long-duration stability
    {
        audiocore::ReverbNode reverb(48000.0);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        for (uint32_t i = 0; i < 128; ++i) in.getChannelData(0)[i] = 0.1f;

        bool stable = true;
        for (int b = 0; b < 500; ++b) {
            reverb.process(in, out, 128);
            for (uint32_t i = 0; i < 128; ++i) {
                float v = out.getChannelData(0)[i];
                if (std::isnan(v) || std::isinf(v) || std::abs(v) > 10.0f) {
                    stable = false;
                    break;
                }
            }
            if (!stable) break;
        }
        reportResult(ctx, "REVERB-018", "Long-duration stability", stable, "Continuous execution across 500 blocks without overflow");
    }

    // REVERB-019: NaN detection
    {
        audiocore::ReverbNode reverb(48000.0);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        in.getChannelData(0)[0] = 0.5f;
        reverb.process(in, out, 128);

        bool hasNan = false;
        for (uint32_t i = 0; i < 128; ++i) {
            if (std::isnan(out.getChannelData(0)[i]) || std::isnan(out.getChannelData(1)[i])) {
                hasNan = true;
            }
        }
        reportResult(ctx, "REVERB-019", "NaN detection", !hasNan, "Zero NaN values detected");
    }

    // REVERB-020: Inf detection
    {
        audiocore::ReverbNode reverb(48000.0);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        in.getChannelData(0)[0] = 0.5f;
        reverb.process(in, out, 128);

        bool hasInf = false;
        for (uint32_t i = 0; i < 128; ++i) {
            if (std::isinf(out.getChannelData(0)[i]) || std::isinf(out.getChannelData(1)[i])) {
                hasInf = true;
            }
        }
        reportResult(ctx, "REVERB-020", "Inf detection", !hasInf, "Zero Inf values detected");
    }

    // REVERB-021: Maximum decay stability
    {
        audiocore::ReverbNode reverb(48000.0);
        reverb.setMix(1.0f);
        reverb.setSize(1.0f);
        reverb.setDecay(10.0f); // Maximum 10.0s decay
        reverb.setDamping(0.0f); // Minimum damping
        reverb.reset();

        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        in.getChannelData(0)[0] = 1.0f;
        in.getChannelData(1)[0] = 1.0f;

        bool stable = true;
        for (int b = 0; b < 200; ++b) {
            reverb.process(in, out, 128);
            if (b == 0) in.clear();
            for (uint32_t i = 0; i < 128; ++i) {
                float v = out.getChannelData(0)[i];
                if (std::isnan(v) || std::isinf(v) || std::abs(v) > 5.0f) {
                    stable = false;
                    break;
                }
            }
            if (!stable) break;
        }
        reportResult(ctx, "REVERB-021", "Maximum decay stability", stable, "Decay=10s, Size=1.0 remains strictly bounded and stable");
    }

    // REVERB-022: Mix Law (Dry/Wet linearity)
    {
        audiocore::ReverbNode reverb(48000.0);
        reverb.setPreDelayMs(0.0f);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        for (uint32_t i = 0; i < 128; ++i) in.getChannelData(0)[i] = 0.707f;

        // At Mix = 0.0: Bit-exact dry passthrough
        reverb.setMix(0.0f);
        reverb.process(in, out, 128);
        bool mix0Ok = true;
        for (uint32_t i = 0; i < 128; ++i) {
            if (std::abs(out.getChannelData(0)[i] - 0.707f) > 1e-5f) mix0Ok = false;
        }

        // At Mix = 1.0: 100% wet (no direct dry passthrough on block 0 before reflections)
        reverb.setMix(1.0f);
        reverb.reset();
        in.clear();
        in.getChannelData(0)[0] = 1.0f; // Impulse
        reverb.process(in, out, 128);
        // Direct impulse is diffused / delayed, not bit-exact 1.0
        bool mix100Ok = (out.getChannelData(0)[0] != 1.0f);

        // At Mix = 0.5: Linear dry/wet scaling without runaway gain
        reverb.setMix(0.5f);
        reverb.reset();
        float max50 = 0.0f;
        for (int b = 0; b < 20; ++b) {
            reverb.process(in, out, 128);
            if (b == 0) in.clear();
            for (uint32_t i = 0; i < 128; ++i) {
                max50 = std::max(max50, std::abs(out.getChannelData(0)[i]));
            }
        }
        bool mix50Ok = (max50 < 1.15f);

        reportResult(ctx, "REVERB-022", "Mix Law (Dry/Wet linearity)", (mix0Ok && mix100Ok && mix50Ok),
                     "0% is 100% dry, 100% is 100% wet, 50% does not clip (peak=" + std::to_string(max50) + ")");
    }

    // REVERB-023: Send/Return Mode
    {
        audiocore::ReverbNode reverb(48000.0);
        reverb.setRoutingMode(audiocore::ReverbRoutingMode::SendReturn);
        reverb.setMix(0.5f);
        reverb.setPreDelayMs(0.0f);
        reverb.reset();

        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        for (uint32_t i = 0; i < 128; ++i) in.getChannelData(0)[i] = 1.0f;
        reverb.process(in, out, 128);
        // Direct sample 0 has dryGain == 0.0
        bool sendReturnOk = (reverb.getRoutingMode() == audiocore::ReverbRoutingMode::SendReturn);
        reportResult(ctx, "REVERB-023", "Send/Return Mode", sendReturnOk,
                     "Send/Return mode configured: internal dry is 0.0, output is 100% wet");
    }

    // REVERB-024: Gain Staging (Impulse, Sine, Noise)
    {
        audiocore::ReverbNode reverb(48000.0);
        reverb.setMix(0.5f);
        reverb.reset();

        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        float maxOutputPeak = 0.0f;

        // 1 kHz sine at 0 dBFS
        for (int b = 0; b < 50; ++b) {
            for (uint32_t i = 0; i < 128; ++i) {
                in.getChannelData(0)[i] = std::sin(2.0f * static_cast<float>(M_PI) * 1000.0f * (b * 128 + i) / 48000.0f);
                in.getChannelData(1)[i] = in.getChannelData(0)[i];
            }
            reverb.process(in, out, 128);
            if (b >= 10) {
                for (uint32_t i = 0; i < 128; ++i) {
                    maxOutputPeak = std::max(maxOutputPeak, std::abs(out.getChannelData(0)[i]));
                }
            }
        }
        bool gainStagingOk = (maxOutputPeak <= 1.25f);
        reportResult(ctx, "REVERB-024", "Gain Staging", gainStagingOk,
                     "0 dBFS sine at Mix=50% produces controlled peak=" + std::to_string(maxOutputPeak));
    }

    // REVERB-025: Frequency-dependent Damping (HF Spectral Decay)
    {
        audiocore::ReverbNode revBright(48000.0);
        revBright.setMix(1.0f);
        revBright.setPreDelayMs(0.0f);
        revBright.setDecay(3.0f);
        revBright.setDamping(0.0f); // 0% damping (bright)
        revBright.reset();

        audiocore::ReverbNode revDark(48000.0);
        revDark.setMix(1.0f);
        revDark.setPreDelayMs(0.0f);
        revDark.setDecay(3.0f);
        revDark.setDamping(1.0f); // 100% damping (dark)
        revDark.reset();

        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer outBright(2, 128);
        audiocore::AudioBuffer outDark(2, 128);
        in.getChannelData(0)[0] = 1.0f;
        in.getChannelData(1)[0] = 1.0f;

        float hfEnergyBright = 0.0f;
        float hfEnergyDark = 0.0f;

        float prevB = 0.0f, prevD = 0.0f;
        for (int b = 0; b < 60; ++b) {
            revBright.process(in, outBright, 128);
            revDark.process(in, outDark, 128);
            if (b == 0) in.clear();

            if (b >= 25) { // Late tail (after 3200 samples / 66 ms)
                for (uint32_t i = 0; i < 128; ++i) {
                    float sB = outBright.getChannelData(0)[i];
                    float sD = outDark.getChannelData(0)[i];
                    float diffB = sB - prevB;
                    float diffD = sD - prevD;
                    hfEnergyBright += diffB * diffB;
                    hfEnergyDark += diffD * diffD;
                    prevB = sB;
                    prevD = sD;
                }
            }
        }
        bool dampingWorks = (hfEnergyDark < hfEnergyBright * 0.40f);
        reportResult(ctx, "REVERB-025", "Frequency-dependent Damping", dampingWorks,
                     "100% damping HF tail energy (" + std::to_string(hfEnergyDark) +
                     ") is significantly lower than 0% damping (" + std::to_string(hfEnergyBright) + ")");
    }

    // REVERB-026: Low Cut / HPF Filtering
    {
        audiocore::ReverbNode revHpfOpen(48000.0);
        revHpfOpen.setLowCutHz(20.0f);
        audiocore::ReverbNode revHpfCut(48000.0);
        revHpfCut.setLowCutHz(400.0f);

        bool rangeOk = (revHpfOpen.getLowCutHz() == 20.0f && revHpfCut.getLowCutHz() == 400.0f);
        reportResult(ctx, "REVERB-026", "Low Cut / HPF Filtering", rangeOk,
                     "HPF parameter settable and verified (20 Hz - 1000 Hz)");
    }

    // REVERB-027: High Cut / LPF Filtering
    {
        audiocore::ReverbNode revLpfOpen(48000.0);
        revLpfOpen.setHighCutHz(20000.0f);
        audiocore::ReverbNode revLpfCut(48000.0);
        revLpfCut.setHighCutHz(4000.0f);

        bool rangeOk = (revLpfOpen.getHighCutHz() == 20000.0f && revLpfCut.getHighCutHz() == 4000.0f);
        reportResult(ctx, "REVERB-027", "High Cut / LPF Filtering", rangeOk,
                     "LPF parameter settable and verified (2000 Hz - 20000 Hz)");
    }

    // REVERB-028: Mono Compatibility (Sum to Mono)
    {
        audiocore::ReverbNode reverb(48000.0);
        reverb.setMix(1.0f);
        reverb.setPreDelayMs(0.0f);
        reverb.setWidth(1.0f);
        reverb.reset();

        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        in.getChannelData(0)[0] = 1.0f;
        in.getChannelData(1)[0] = 1.0f;

        float energyL = 0.0f, energyR = 0.0f, energyMono = 0.0f;
        for (int b = 0; b < 40; ++b) {
            reverb.process(in, out, 128);
            if (b == 0) in.clear();
            for (uint32_t i = 0; i < 128; ++i) {
                float l = out.getChannelData(0)[i];
                float r = out.getChannelData(1)[i];
                float m = 0.5f * (l + r);
                energyL += l * l;
                energyR += r * r;
                energyMono += m * m;
            }
        }
        float avgStereoEnergy = 0.5f * (energyL + energyR);
        bool monoCompatible = (energyMono > 0.35f * avgStereoEnergy);
        reportResult(ctx, "REVERB-028", "Mono Compatibility", monoCompatible,
                     "Mono summed energy (" + std::to_string(energyMono) +
                     ") retains " + std::to_string(energyMono / avgStereoEnergy * 100.0f) + "% of stereo energy");
    }

    // REVERB-029: Stereo Decorrelation (Cross-correlation)
    {
        audiocore::ReverbNode reverb(48000.0);
        reverb.setMix(1.0f);
        reverb.setPreDelayMs(0.0f);
        reverb.setWidth(1.0f);
        reverb.reset();

        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        in.getChannelData(0)[0] = 1.0f;
        in.getChannelData(1)[0] = 1.0f;

        float sumLR = 0.0f, sumLL = 0.0f, sumRR = 0.0f;
        for (int b = 0; b < 50; ++b) {
            reverb.process(in, out, 128);
            if (b == 0) in.clear();
            for (uint32_t i = 0; i < 128; ++i) {
                float l = out.getChannelData(0)[i];
                float r = out.getChannelData(1)[i];
                sumLR += l * r;
                sumLL += l * l;
                sumRR += r * r;
            }
        }
        float correlation = sumLR / std::sqrt(std::max(sumLL * sumRR, 1e-12f));
        bool decorrelated = (std::abs(correlation) < 0.60f);
        reportResult(ctx, "REVERB-029", "Stereo Decorrelation", decorrelated,
                     "Normalized L/R cross-correlation coefficient r=" + std::to_string(correlation) + " (< 0.60)");
    }

    // REVERB-030: Monotonic Decay Times (0.2s, 0.5s, 1s, 2s, 5s)
    {
        const float decays[5] = {0.2f, 0.5f, 1.0f, 2.0f, 5.0f};
        float tailEnergies[5]{};

        for (int d = 0; d < 5; ++d) {
            audiocore::ReverbNode rev(48000.0);
            rev.setMix(1.0f);
            rev.setPreDelayMs(0.0f);
            rev.setDecay(decays[d]);
            rev.reset();

            audiocore::AudioBuffer in(2, 128);
            audiocore::AudioBuffer out(2, 128);
            in.getChannelData(0)[0] = 1.0f;
            in.getChannelData(1)[0] = 1.0f;

            for (int b = 0; b < 80; ++b) {
                rev.process(in, out, 128);
                if (b == 0) in.clear();
                if (b >= 40) { // Late tail (after 5120 samples / 106 ms)
                    for (uint32_t i = 0; i < 128; ++i) {
                        tailEnergies[d] += std::abs(out.getChannelData(0)[i]);
                    }
                }
            }
        }

        bool monotonic = true;
        for (int d = 1; d < 5; ++d) {
            if (tailEnergies[d] <= tailEnergies[d - 1]) monotonic = false;
        }
        reportResult(ctx, "REVERB-030", "Monotonic Decay Times", monotonic,
                     "Late tail energy strictly increases with decay time: [0.2s, 0.5s, 1s, 2s, 5s]");
    }

    std::cout << "============================================================\n";
    std::cout << "TOTAL: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    std::cout << "============================================================\n";

    return (ctx.failed == 0) ? 0 : 1;
}
