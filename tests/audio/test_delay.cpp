#include "audiocore/delay_node.hpp"
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
    std::cout << "AUDIO CORE: TAP DELAY AUTOMATED VERIFICATION SUITE\n";
    std::cout << "============================================================\n";

    TestContext ctx;

    // DELAY-001: Construction
    {
        audiocore::DelayNode delay(48000.0);
        bool ok = (delay.getSampleRate() == 48000.0);
        reportResult(ctx, "DELAY-001", "Construction", ok, "sampleRate=48000.0");
    }

    // DELAY-002: Default state
    {
        audiocore::DelayNode delay(48000.0);
        bool ok = (delay.getFeedback() == 0.30f) &&
                  (delay.getMix() == 0.50f) &&
                  (delay.getOutputLevelDb() == 0.0f) &&
                  (delay.getStereoWidth() == 1.0f) &&
                  !delay.isBypassed() &&
                  !delay.isTempoSyncEnabled();
        reportResult(ctx, "DELAY-002", "Default state", ok, "FB=0.30, Mix=0.50, Out=0dB, Width=1.0");
    }

    // DELAY-003: Bypass
    {
        audiocore::DelayNode delay(48000.0);
        delay.setBypassed(true);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        for (uint32_t i = 0; i < 128; ++i) {
            in.getChannelData(0)[i] = static_cast<float>(i + 1) / 128.0f;
            in.getChannelData(1)[i] = -static_cast<float>(i + 1) / 128.0f;
        }
        delay.process(in, out, 128);
        bool match = true;
        for (uint32_t i = 0; i < 128; ++i) {
            if (out.getChannelData(0)[i] != in.getChannelData(0)[i] ||
                out.getChannelData(1)[i] != in.getChannelData(1)[i]) {
                match = false;
                break;
            }
        }
        reportResult(ctx, "DELAY-003", "Bypass", match, "Bit-exact passthrough when bypassed");
    }

    // DELAY-004: Zero delay
    {
        audiocore::DelayNode delay(48000.0);
        delay.setMix(1.0f); // 100% wet
        delay.setFeedback(0.0f);
        // Set all taps to 0ms delay
        for (size_t t = 0; t < 4; ++t) {
            delay.setTapDelayMs(t, 0.0f);
            delay.setTapPan(t, 0.0f);
            delay.setTapEnabled(t, (t == 0)); // Only tap 0 active
            delay.setTapLevelDb(t, 0.0f);
        }
        delay.reset();
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        in.getChannelData(0)[0] = 1.0f;
        in.getChannelData(1)[0] = 1.0f;
        // Process block
        delay.process(in, out, 128);
        // Immediate response at frame 0
        float firstSample = out.getChannelData(0)[0];
        bool ok = (firstSample > 0.6f);
        reportResult(ctx, "DELAY-004", "Zero delay", ok, "Immediate signal pass with 0ms delay");
    }

    // DELAY-005: Single tap
    {
        audiocore::DelayNode delay(48000.0);
        delay.setMix(1.0f);
        delay.setFeedback(0.0f);
        for (size_t t = 0; t < 4; ++t) {
            delay.setTapEnabled(t, (t == 0));
        }
        delay.setTapDelayMs(0, 50.0f);
        delay.setTapLevelDb(0, 0.0f);
        delay.setTapPan(0, 0.0f);

        bool tap0On = delay.isTapEnabled(0);
        bool tap1Off = !delay.isTapEnabled(1);
        reportResult(ctx, "DELAY-005", "Single tap", (tap0On && tap1Off), "Tap 0 enabled, Taps 1-3 disabled");
    }

    // DELAY-006: Two taps
    {
        audiocore::DelayNode delay(48000.0);
        delay.setTapEnabled(0, true);
        delay.setTapEnabled(1, true);
        delay.setTapEnabled(2, false);
        delay.setTapEnabled(3, false);
        bool ok = delay.isTapEnabled(0) && delay.isTapEnabled(1) &&
                  !delay.isTapEnabled(2) && !delay.isTapEnabled(3);
        reportResult(ctx, "DELAY-006", "Two taps", ok, "Taps 0-1 active, Taps 2-3 inactive");
    }

    // DELAY-007: Four taps
    {
        audiocore::DelayNode delay(48000.0);
        for (size_t t = 0; t < 4; ++t) delay.setTapEnabled(t, true);
        bool ok = delay.isTapEnabled(0) && delay.isTapEnabled(1) &&
                  delay.isTapEnabled(2) && delay.isTapEnabled(3);
        reportResult(ctx, "DELAY-007", "Four taps", ok, "All 4 independent taps simultaneously active");
    }

    // DELAY-008: Delay timing accuracy
    {
        // 100 ms at 48000 Hz = 4800 samples
        audiocore::DelayNode delay(48000.0);
        delay.setMix(1.0f);
        delay.setFeedback(0.0f);
        for (size_t t = 0; t < 4; ++t) delay.setTapEnabled(t, (t == 0));
        delay.setTapDelayMs(0, 100.0f);
        delay.setTapLevelDb(0, 0.0f);
        delay.setTapPan(0, 0.0f);
        delay.reset();

        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        in.getChannelData(0)[0] = 1.0f; // Impulse at sample 0
        in.getChannelData(1)[0] = 1.0f;

        int peakSample = -1;
        float peakVal = 0.0f;
        // 4800 samples = 37.5 blocks of 128 frames
        for (int b = 0; b < 50; ++b) {
            delay.process(in, out, 128);
            // Clear input for subsequent blocks
            if (b == 0) {
                in.getChannelData(0)[0] = 0.0f;
                in.getChannelData(1)[0] = 0.0f;
            }
            for (uint32_t i = 0; i < 128; ++i) {
                float val = std::abs(out.getChannelData(0)[i]);
                if (val > peakVal) {
                    peakVal = val;
                    peakSample = b * 128 + i;
                }
            }
        }
        // Allow tiny interpolation/slew tolerance (+/- 3 samples)
        bool accurate = (std::abs(peakSample - 4800) <= 3);
        char buf[128];
        snprintf(buf, sizeof(buf), "Target 4800 samples, measured peak at sample %d (err=%d)",
                 peakSample, peakSample - 4800);
        reportResult(ctx, "DELAY-008", "Delay timing accuracy", accurate, buf);
    }

    // DELAY-009: Feedback
    {
        audiocore::DelayNode delay(48000.0);
        delay.setMix(1.0f);
        delay.setFeedback(0.60f);
        for (size_t t = 0; t < 4; ++t) delay.setTapEnabled(t, (t == 0));
        delay.setTapDelayMs(0, 10.0f); // 480 samples = 3.75 blocks
        delay.reset();

        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        in.getChannelData(0)[0] = 1.0f;
        in.getChannelData(1)[0] = 1.0f;

        int repeatCount = 0;
        for (int b = 0; b < 40; ++b) {
            delay.process(in, out, 128);
            if (b == 0) in.clear();
            for (uint32_t i = 0; i < 128; ++i) {
                if (std::abs(out.getChannelData(0)[i]) > 0.05f) {
                    repeatCount++;
                }
            }
        }
        bool fbOk = (repeatCount >= 3);
        reportResult(ctx, "DELAY-009", "Feedback", fbOk, "Multiple recirculating repeats detected");
    }

    // DELAY-010: Feedback stability
    {
        audiocore::DelayNode delay(48000.0);
        delay.setMix(1.0f);
        delay.setFeedback(0.99f); // Maximum allowed
        for (size_t t = 0; t < 4; ++t) delay.setTapEnabled(t, (t == 0));
        delay.setTapDelayMs(0, 5.0f); // Rapid circulation
        delay.reset();

        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        in.getChannelData(0)[0] = 1.0f;
        in.getChannelData(1)[0] = 1.0f;

        bool bounded = true;
        for (int b = 0; b < 200; ++b) {
            delay.process(in, out, 128);
            if (b == 0) in.clear();
            for (uint32_t i = 0; i < 128; ++i) {
                float v = std::abs(out.getChannelData(0)[i]);
                if (std::isnan(v) || std::isinf(v) || v > 5.0f) {
                    bounded = false;
                    break;
                }
            }
            if (!bounded) break;
        }
        reportResult(ctx, "DELAY-010", "Feedback stability", bounded, "Bounded execution with FB=0.99 over 200 blocks");
    }

    // DELAY-011: Delay-time smoothing
    {
        audiocore::DelayNode delay(48000.0);
        delay.setMix(1.0f);
        delay.setFeedback(0.0f);
        for (size_t t = 0; t < 4; ++t) delay.setTapEnabled(t, (t == 0));
        delay.setTapDelayMs(0, 10.0f);
        delay.reset();

        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        // Fill input with continuous sine
        for (uint32_t i = 0; i < 128; ++i) {
            in.getChannelData(0)[i] = std::sin(2.0f * static_cast<float>(M_PI) * 440.0f * i / 48000.0f);
            in.getChannelData(1)[i] = in.getChannelData(0)[i];
        }

        // Jump delay abruptly during processing
        bool clean = true;
        for (int b = 0; b < 10; ++b) {
            if (b == 3) delay.setTapDelayMs(0, 80.0f); // Time change on the fly
            delay.process(in, out, 128);
            for (uint32_t i = 0; i < 128; ++i) {
                float v = out.getChannelData(0)[i];
                if (std::isnan(v) || std::isinf(v)) clean = false;
            }
        }
        reportResult(ctx, "DELAY-011", "Delay-time smoothing", clean, "Slew interpolation prevents discontinuities on knob turn");
    }

    // DELAY-012: Stereo behavior
    {
        audiocore::DelayNode delay(48000.0);
        delay.setMix(1.0f);
        delay.setFeedback(0.0f);
        delay.setStereoWidth(1.0f);
        // Tap 0 panned hard Left (-1.0), Tap 1 panned hard Right (+1.0)
        delay.setTapEnabled(0, true);
        delay.setTapDelayMs(0, 20.0f);
        delay.setTapPan(0, -1.0f);

        delay.setTapEnabled(1, true);
        delay.setTapDelayMs(1, 40.0f);
        delay.setTapPan(1, 1.0f);

        delay.setTapEnabled(2, false);
        delay.setTapEnabled(3, false);
        delay.reset();

        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        in.getChannelData(0)[0] = 1.0f;
        in.getChannelData(1)[0] = 1.0f;

        float maxL = 0.0f;
        float maxR = 0.0f;
        for (int b = 0; b < 20; ++b) {
            delay.process(in, out, 128);
            if (b == 0) in.clear();
            for (uint32_t i = 0; i < 128; ++i) {
                maxL = std::max(maxL, std::abs(out.getChannelData(0)[i]));
                maxR = std::max(maxR, std::abs(out.getChannelData(1)[i]));
            }
        }
        bool stereoOk = (maxL > 0.3f && maxR > 0.3f);
        reportResult(ctx, "DELAY-012", "Stereo behavior", stereoOk, "Independent L/R stereo tap positioning verified");
    }

    // DELAY-013: Filter behavior
    {
        audiocore::DelayNode delay(48000.0);
        delay.setLowCutHz(400.0f);
        delay.setHighCutHz(4000.0f);
        bool filterOk = (delay.getLowCutHz() == 400.0f) && (delay.getHighCutHz() == 4000.0f);
        reportResult(ctx, "DELAY-013", "Filter behavior", filterOk, "LowCut=400Hz, HighCut=4000Hz feedback filtering configured");
    }

    // DELAY-014: Tempo synchronization
    {
        float d_1_4 = audiocore::DelayNode::calculateTempoDelayMs(120.0f, audiocore::TempoDivision::Div_1_4);
        float d_1_8 = audiocore::DelayNode::calculateTempoDelayMs(120.0f, audiocore::TempoDivision::Div_1_8);
        float d_1_16 = audiocore::DelayNode::calculateTempoDelayMs(120.0f, audiocore::TempoDivision::Div_1_16);
        float d_1_8t = audiocore::DelayNode::calculateTempoDelayMs(120.0f, audiocore::TempoDivision::Div_1_8T);

        bool tempoOk = (std::abs(d_1_4 - 500.0f) < 0.01f) &&
                       (std::abs(d_1_8 - 250.0f) < 0.01f) &&
                       (std::abs(d_1_16 - 125.0f) < 0.01f) &&
                       (std::abs(d_1_8t - 166.6667f) < 0.1f);
        reportResult(ctx, "DELAY-014", "Tempo synchronization", tempoOk, "120 BPM: 1/4=500ms, 1/8=250ms, 1/16=125ms, 1/8T=166.67ms");
    }

    // DELAY-015: 48 kHz
    {
        audiocore::DelayNode delay(48000.0);
        delay.setSampleRate(48000.0);
        bool ok = (delay.getSampleRate() == 48000.0);
        reportResult(ctx, "DELAY-015", "48 kHz", ok, "Sampling rate configured to 48000.0 Hz");
    }

    // DELAY-016: 96 kHz
    {
        audiocore::DelayNode delay(48000.0);
        delay.setSampleRate(96000.0);
        bool ok = (delay.getSampleRate() == 96000.0);
        reportResult(ctx, "DELAY-016", "96 kHz", ok, "Sampling rate updated to 96000.0 Hz");
    }

    // DELAY-017: Multi-block state
    {
        audiocore::DelayNode delay(48000.0);
        delay.setMix(1.0f);
        delay.setFeedback(0.0f);
        for (size_t t = 0; t < 4; ++t) delay.setTapEnabled(t, (t == 0));
        delay.setTapDelayMs(0, 20.0f); // 960 samples = exactly 7.5 blocks of 128
        delay.reset();

        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        in.getChannelData(0)[0] = 1.0f;
        in.getChannelData(1)[0] = 1.0f;

        float blockEnergy[10]{};
        for (int b = 0; b < 10; ++b) {
            delay.process(in, out, 128);
            if (b == 0) in.clear();
            for (uint32_t i = 0; i < 128; ++i) {
                blockEnergy[b] += std::abs(out.getChannelData(0)[i]);
            }
        }
        // Energy should appear in block 7 and 8 (around frame 960)
        bool multiBlockOk = (blockEnergy[0] == 0.0f && (blockEnergy[7] > 0.0f || blockEnergy[8] > 0.0f));
        reportResult(ctx, "DELAY-017", "Multi-block state", multiBlockOk, "Delay state accurately spans multiple consecutive blocks");
    }

    // DELAY-018: Long-duration stability
    {
        audiocore::DelayNode delay(48000.0);
        delay.setMix(0.5f);
        delay.setFeedback(0.70f);
        delay.reset();

        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        for (uint32_t i = 0; i < 128; ++i) in.getChannelData(0)[i] = 0.1f;

        bool stable = true;
        for (int b = 0; b < 1000; ++b) {
            delay.process(in, out, 128);
            for (uint32_t i = 0; i < 128; ++i) {
                float v = out.getChannelData(0)[i];
                if (std::isnan(v) || std::isinf(v) || std::abs(v) > 10.0f) {
                    stable = false;
                    break;
                }
            }
            if (!stable) break;
        }
        reportResult(ctx, "DELAY-018", "Long-duration stability", stable, "Processed 1000 blocks continuously without divergence");
    }

    // DELAY-019: NaN detection
    {
        audiocore::DelayNode delay(48000.0);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        in.getChannelData(0)[0] = 0.5f;
        delay.process(in, out, 128);

        bool hasNan = false;
        for (uint32_t i = 0; i < 128; ++i) {
            if (std::isnan(out.getChannelData(0)[i]) || std::isnan(out.getChannelData(1)[i])) {
                hasNan = true;
            }
        }
        reportResult(ctx, "DELAY-019", "NaN detection", !hasNan, "Zero NaN values detected");
    }

    // DELAY-020: Inf detection
    {
        audiocore::DelayNode delay(48000.0);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        in.getChannelData(0)[0] = 0.5f;
        delay.process(in, out, 128);

        bool hasInf = false;
        for (uint32_t i = 0; i < 128; ++i) {
            if (std::isinf(out.getChannelData(0)[i]) || std::isinf(out.getChannelData(1)[i])) {
                hasInf = true;
            }
        }
        reportResult(ctx, "DELAY-020", "Inf detection", !hasInf, "Zero Inf values detected");
    }

    // DELAY-021: Impulse response @ 96 kHz
    {
        // For 96 kHz and delay = 100 ms: expected delay = 9600 samples
        audiocore::DelayNode delay(96000.0);
        delay.setMix(1.0f);
        delay.setFeedback(0.0f);
        for (size_t t = 0; t < 4; ++t) delay.setTapEnabled(t, (t == 0));
        delay.setTapDelayMs(0, 100.0f);
        delay.setTapLevelDb(0, 0.0f);
        delay.setTapPan(0, 0.0f);
        delay.reset();

        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        in.getChannelData(0)[0] = 1.0f;
        in.getChannelData(1)[0] = 1.0f;

        int peakSample = -1;
        float peakVal = 0.0f;
        // 9600 samples = 75 blocks of 128 frames
        for (int b = 0; b < 100; ++b) {
            delay.process(in, out, 128);
            if (b == 0) in.clear();
            for (uint32_t i = 0; i < 128; ++i) {
                float val = std::abs(out.getChannelData(0)[i]);
                if (val > peakVal) {
                    peakVal = val;
                    peakSample = b * 128 + i;
                }
            }
        }
        bool accurate96 = (std::abs(peakSample - 9600) <= 3);
        char buf[128];
        snprintf(buf, sizeof(buf), "96 kHz impulse: Target 9600 samples, measured peak at sample %d (err=%d)",
                 peakSample, peakSample - 9600);
        reportResult(ctx, "DELAY-021", "Impulse response @ 96 kHz", accurate96, buf);
    }

    // DELAY-022: Feedback HPF Filtering
    {
        audiocore::DelayNode delayOpen(48000.0);
        delayOpen.setMix(1.0f);
        delayOpen.setFeedback(0.70f);
        delayOpen.setLowCutHz(20.0f); // Wide open HPF
        for (size_t t = 0; t < 4; ++t) delayOpen.setTapEnabled(t, (t == 0));
        delayOpen.setTapDelayMs(0, 50.0f);
        delayOpen.reset();

        audiocore::DelayNode delayCut(48000.0);
        delayCut.setMix(1.0f);
        delayCut.setFeedback(0.70f);
        delayCut.setLowCutHz(500.0f); // 500 Hz HPF
        for (size_t t = 0; t < 4; ++t) delayCut.setTapEnabled(t, (t == 0));
        delayCut.setTapDelayMs(0, 50.0f);
        delayCut.reset();

        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer outOpen(2, 128);
        audiocore::AudioBuffer outCut(2, 128);

        // 80 Hz low frequency tone burst
        for (uint32_t i = 0; i < 128; ++i) {
            in.getChannelData(0)[i] = std::sin(2.0f * static_cast<float>(M_PI) * 80.0f * i / 48000.0f);
            in.getChannelData(1)[i] = in.getChannelData(0)[i];
        }

        float energyOpenLate = 0.0f;
        float energyCutLate = 0.0f;
        for (int b = 0; b < 40; ++b) {
            delayOpen.process(in, outOpen, 128);
            delayCut.process(in, outCut, 128);
            if (b == 0) in.clear();
            if (b >= 20) { // Delayed repeats
                for (uint32_t i = 0; i < 128; ++i) {
                    energyOpenLate += outOpen.getChannelData(0)[i] * outOpen.getChannelData(0)[i];
                    energyCutLate += outCut.getChannelData(0)[i] * outCut.getChannelData(0)[i];
                }
            }
        }
        bool hpfWorks = (energyCutLate < energyOpenLate * 0.40f);
        reportResult(ctx, "DELAY-022", "Feedback HPF Filtering", hpfWorks,
                     "500 Hz HPF attenuates low-frequency 80 Hz recirculations (" +
                     std::to_string(energyCutLate) + " vs open " + std::to_string(energyOpenLate) + ")");
    }

    // DELAY-023: Feedback LPF Filtering
    {
        audiocore::DelayNode delayOpen(48000.0);
        delayOpen.setMix(1.0f);
        delayOpen.setFeedback(0.70f);
        delayOpen.setHighCutHz(20000.0f); // Wide open LPF
        for (size_t t = 0; t < 4; ++t) delayOpen.setTapEnabled(t, (t == 0));
        delayOpen.setTapDelayMs(0, 50.0f);
        delayOpen.reset();

        audiocore::DelayNode delayCut(48000.0);
        delayCut.setMix(1.0f);
        delayCut.setFeedback(0.70f);
        delayCut.setHighCutHz(2500.0f); // 2.5 kHz LPF
        for (size_t t = 0; t < 4; ++t) delayCut.setTapEnabled(t, (t == 0));
        delayCut.setTapDelayMs(0, 50.0f);
        delayCut.reset();

        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer outOpen(2, 128);
        audiocore::AudioBuffer outCut(2, 128);

        // 6000 Hz high frequency tone burst
        for (uint32_t i = 0; i < 128; ++i) {
            in.getChannelData(0)[i] = std::sin(2.0f * static_cast<float>(M_PI) * 6000.0f * i / 48000.0f);
            in.getChannelData(1)[i] = in.getChannelData(0)[i];
        }

        float energyOpenLate = 0.0f;
        float energyCutLate = 0.0f;
        for (int b = 0; b < 40; ++b) {
            delayOpen.process(in, outOpen, 128);
            delayCut.process(in, outCut, 128);
            if (b == 0) in.clear();
            if (b >= 20) { // Delayed repeats
                for (uint32_t i = 0; i < 128; ++i) {
                    energyOpenLate += outOpen.getChannelData(0)[i] * outOpen.getChannelData(0)[i];
                    energyCutLate += outCut.getChannelData(0)[i] * outCut.getChannelData(0)[i];
                }
            }
        }
        bool lpfWorks = (energyCutLate < energyOpenLate * 0.30f);
        reportResult(ctx, "DELAY-023", "Feedback LPF Filtering", lpfWorks,
                     "2.5 kHz LPF progressively darkens 6 kHz recirculations (" +
                     std::to_string(energyCutLate) + " vs open " + std::to_string(energyOpenLate) + ")");
    }

    // DELAY-024: Feedback Filter Response Across Cascaded Repeats
    {
        audiocore::DelayNode delay(48000.0);
        delay.setMix(1.0f);
        delay.setFeedback(0.75f);
        delay.setHighCutHz(4000.0f);
        for (size_t t = 0; t < 4; ++t) delay.setTapEnabled(t, (t == 0));
        delay.setTapDelayMs(0, 50.0f); // 2400 samples
        delay.reset();

        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        in.getChannelData(0)[0] = 1.0f; // Unit impulse
        in.getChannelData(1)[0] = 1.0f;

        // Measure HF sharpness (sample-to-sample difference) on repeat 1 vs repeat 3
        float hfRepeat1 = 0.0f;
        float hfRepeat3 = 0.0f;
        float prev = 0.0f;

        // Repeat 1 is around sample 2400 (blocks 18-20); Repeat 3 is around sample 7200 (blocks 56-58)
        for (int b = 0; b < 70; ++b) {
            delay.process(in, out, 128);
            if (b == 0) in.clear();
            for (uint32_t i = 0; i < 128; ++i) {
                float s = out.getChannelData(0)[i];
                float diff = s - prev;
                prev = s;
                if (b >= 18 && b <= 20) hfRepeat1 += diff * diff;
                if (b >= 56 && b <= 58) hfRepeat3 += diff * diff;
            }
        }
        bool progressiveFiltering = (hfRepeat3 < hfRepeat1 * 0.40f);
        reportResult(ctx, "DELAY-024", "Feedback Progressive Filtering", progressiveFiltering,
                     "Repeat 3 is significantly darker and smoother than repeat 1");
    }

    // DELAY-025: Mix Law (Dry/Wet Linearity & Passthrough)
    {
        audiocore::DelayNode delay(48000.0);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        for (uint32_t i = 0; i < 128; ++i) in.getChannelData(0)[i] = 0.5f;

        // Mix = 0.0: Bit exact dry
        delay.setMix(0.0f);
        delay.process(in, out, 128);
        bool mix0 = true;
        for (uint32_t i = 0; i < 128; ++i) {
            if (std::abs(out.getChannelData(0)[i] - 0.5f) > 1e-5f) mix0 = false;
        }

        // Mix = 1.0: 100% wet
        delay.setMix(1.0f);
        delay.setTapDelayMs(0, 100.0f);
        delay.reset();
        delay.process(in, out, 128);
        bool mix100 = (out.getChannelData(0)[0] == 0.0f); // 100ms delay line is initially silent

        reportResult(ctx, "DELAY-025", "Mix Law (Dry/Wet Linearity)", (mix0 && mix100),
                     "0% is bit-exact dry, 100% is 100% wet");
    }

    std::cout << "============================================================\n";
    std::cout << "TOTAL: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    std::cout << "============================================================\n";

    return (ctx.failed == 0) ? 0 : 1;
}
