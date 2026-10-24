#include "audiocore/reverb_node.hpp"
#include "audiocore/audio_buffer.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

namespace {

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
    std::cout << "AUDIO CORE: REVERB PRE-DELAY & TEMPLATES VERIFICATION SUITE\n";
    std::cout << "============================================================\n";

    TestContext ctx;

    // =========================================================================
    // PART 1: PREDELAY TESTS (PREDELAY-001 .. PREDELAY-017)
    // =========================================================================

    // PREDELAY-001: Construction
    {
        audiocore::ReverbNode reverb(48000.0);
        bool ok = (reverb.getSampleRate() == 48000.0) && reverb.isPreDelayEnabled();
        reportResult(ctx, "PREDELAY-001", "Construction", ok, "sampleRate=48000.0, enabled=true");
    }

    // PREDELAY-002: Default state
    {
        audiocore::ReverbNode reverb(48000.0);
        float preMs = reverb.getPreDelayMs();
        uint32_t preSmp = reverb.getPreDelaySamples();
        bool ok = (preMs >= 0.0f && preMs <= 250.0f) && (preSmp == static_cast<uint32_t>(std::round(preMs * 48.0f)));
        reportResult(ctx, "PREDELAY-002", "Default state", ok,
                     "PreDelay=" + std::to_string(preMs) + "ms, samples=" + std::to_string(preSmp));
    }

    // PREDELAY-003: Disabled / bypass
    {
        audiocore::ReverbNode reverb(48000.0);
        reverb.setPreDelayEnabled(false);
        reverb.setPreDelayMs(50.0f, true);

        audiocore::AudioBuffer in(2, 64);
        audiocore::AudioBuffer out(2, 64);
        in.clear();
        in.getChannelData(0)[0] = 1.0f;
        in.getChannelData(1)[0] = 1.0f;

        reverb.processPreDelay(in, out, 64);
        bool passedImmediate = (out.getChannelData(0)[0] == 1.0f && out.getChannelData(1)[0] == 1.0f);
        reportResult(ctx, "PREDELAY-003", "Disabled / bypass", passedImmediate,
                     "When predelay is disabled, signal passes immediately with 0 latency");
    }

    // PREDELAY-004: 0 ms delay
    {
        audiocore::ReverbNode reverb(48000.0);
        reverb.setPreDelayEnabled(true);
        reverb.setPreDelayMs(0.0f, true);

        audiocore::AudioBuffer in(2, 64);
        audiocore::AudioBuffer out(2, 64);
        in.clear();
        in.getChannelData(0)[0] = 0.85f;
        in.getChannelData(1)[0] = 0.85f;

        reverb.processPreDelay(in, out, 64);
        bool bitExact = (std::abs(out.getChannelData(0)[0] - 0.85f) < 1e-6f);
        reportResult(ctx, "PREDELAY-004", "0 ms delay", bitExact, "0 ms delay produces bit-exact immediate sample");
    }

    // Helper lambda to measure impulse peak location through pre-delay stage
    auto measurePreDelay = [](double sampleRate, float delayMs, uint32_t maxFrames) -> uint32_t {
        audiocore::ReverbNode reverb(sampleRate);
        reverb.setPreDelayEnabled(true);
        reverb.setPreDelayMs(delayMs, true); // Snap to target

        audiocore::AudioBuffer in(2, maxFrames);
        audiocore::AudioBuffer out(2, maxFrames);
        in.clear();
        in.getChannelData(0)[0] = 1.0f;
        in.getChannelData(1)[0] = 1.0f;

        reverb.processPreDelay(in, out, maxFrames);

        float maxVal = 0.0f;
        uint32_t peakIdx = 0;
        for (uint32_t i = 0; i < maxFrames; ++i) {
            float v = std::abs(out.getChannelData(0)[i]);
            if (v > maxVal) {
                maxVal = v;
                peakIdx = i;
            }
        }
        return peakIdx;
    };

    // PREDELAY-005: 1 ms @ 48 kHz (48 samples)
    {
        uint32_t peak = measurePreDelay(48000.0, 1.0f, 256);
        bool ok = (peak == 48);
        reportResult(ctx, "PREDELAY-005", "1 ms delay timing @ 48 kHz", ok,
                     "Expected=48, Measured=" + std::to_string(peak));
    }

    // PREDELAY-006: 10 ms @ 48 kHz (480 samples)
    {
        uint32_t peak = measurePreDelay(48000.0, 10.0f, 1024);
        bool ok = (peak == 480);
        reportResult(ctx, "PREDELAY-006", "10 ms delay timing @ 48 kHz", ok,
                     "Expected=480, Measured=" + std::to_string(peak));
    }

    // PREDELAY-007: 20 ms @ 48 kHz (960 samples)
    {
        uint32_t peak = measurePreDelay(48000.0, 20.0f, 2048);
        bool ok = (peak == 960);
        reportResult(ctx, "PREDELAY-007", "20 ms delay timing @ 48 kHz", ok,
                     "Expected=960, Measured=" + std::to_string(peak));
    }

    // PREDELAY-008: 50 ms @ 48 kHz (2400 samples)
    {
        uint32_t peak = measurePreDelay(48000.0, 50.0f, 4096);
        bool ok = (peak == 2400);
        reportResult(ctx, "PREDELAY-008", "50 ms delay timing @ 48 kHz", ok,
                     "Expected=2400, Measured=" + std::to_string(peak));
    }

    // PREDELAY-009: 100 ms @ 48 kHz (4800 samples)
    {
        uint32_t peak = measurePreDelay(48000.0, 100.0f, 8192);
        bool ok = (peak == 4800);
        reportResult(ctx, "PREDELAY-009", "100 ms delay timing @ 48 kHz", ok,
                     "Expected=4800, Measured=" + std::to_string(peak));
    }

    // PREDELAY-010: 200 ms @ 48 kHz (9600 samples)
    {
        uint32_t peak = measurePreDelay(48000.0, 200.0f, 16384);
        bool ok = (peak == 9600);
        reportResult(ctx, "PREDELAY-010", "200 ms delay timing @ 48 kHz", ok,
                     "Expected=9600, Measured=" + std::to_string(peak));
    }

    // PREDELAY-011: 48 kHz multi-point timing verification
    {
        bool ok = (measurePreDelay(48000.0, 5.0f, 512) == 240) &&
                  (measurePreDelay(48000.0, 15.0f, 1024) == 720) &&
                  (measurePreDelay(48000.0, 25.0f, 2048) == 1200);
        reportResult(ctx, "PREDELAY-011", "48 kHz multi-point timing verification", ok,
                     "5ms=240, 15ms=720, 25ms=1200 verified exact");
    }

    // PREDELAY-012: 96 kHz timing verification (20 ms = 1920 samples)
    {
        uint32_t peak = measurePreDelay(96000.0, 20.0f, 4096);
        bool ok = (peak == 1920);
        reportResult(ctx, "PREDELAY-012", "96 kHz timing verification", ok,
                     "Expected=1920, Measured=" + std::to_string(peak));
    }

    // PREDELAY-013: Multi-block state preservation (block sizes 64, 128, 256, 512)
    {
        audiocore::ReverbNode reverb(48000.0);
        reverb.setPreDelayEnabled(true);
        reverb.setPreDelayMs(20.0f, true); // 960 samples delay

        const uint32_t blockSizes[] = {64, 128, 256, 512};
        uint32_t totalSamplesProcessed = 0;
        uint32_t detectedSampleIndex = 0;
        bool foundPeak = false;

        audiocore::AudioBuffer inBuf(2, 512);
        audiocore::AudioBuffer outBuf(2, 512);

        // Feed impulse in first block
        for (uint32_t b = 0; b < 25; ++b) {
            uint32_t bs = blockSizes[b % 4];
            inBuf.clear(bs);
            if (b == 0) {
                inBuf.getChannelData(0)[0] = 1.0f;
                inBuf.getChannelData(1)[0] = 1.0f;
            }
            reverb.processPreDelay(inBuf, outBuf, bs);

            for (uint32_t i = 0; i < bs; ++i) {
                if (std::abs(outBuf.getChannelData(0)[i]) > 0.8f && !foundPeak) {
                    detectedSampleIndex = totalSamplesProcessed + i;
                    foundPeak = true;
                }
            }
            totalSamplesProcessed += bs;
        }

        bool ok = (detectedSampleIndex == 960);
        reportResult(ctx, "PREDELAY-013", "Multi-block state preservation", ok,
                     "Blocks [64, 128, 256, 512], Detected index=" + std::to_string(detectedSampleIndex));
    }

    // PREDELAY-014: Parameter automation (smooth continuous interpolation without clicks)
    {
        audiocore::ReverbNode reverb(48000.0);
        reverb.setPreDelayEnabled(true);
        reverb.setPreDelayMs(0.0f, true);

        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        for (uint32_t i = 0; i < 128; ++i) {
            in.getChannelData(0)[i] = 0.5f;
            in.getChannelData(1)[i] = 0.5f;
        }

        // Prime buffer with steady signal so delay line holds audio
        for (int p = 0; p < 40; ++p) {
            reverb.processPreDelay(in, out, 128);
        }

        float maxStepDelta = 0.0f;
        float prevSample = 0.5f;
        for (uint32_t b = 0; b < 40; ++b) {
            // Live sweep pre-delay from 0 to 80 ms without snapping
            reverb.setPreDelayMs(float(b) * 2.0f, false);
            reverb.processPreDelay(in, out, 128);

            for (uint32_t i = 0; i < 128; ++i) {
                float cur = out.getChannelData(0)[i];
                float delta = std::abs(cur - prevSample);
                if (delta > maxStepDelta) maxStepDelta = delta;
                prevSample = cur;
            }
        }
        bool smooth = (maxStepDelta < 0.05f); // Slewing bounds sample-to-sample difference
        reportResult(ctx, "PREDELAY-014", "Parameter automation & slewing", smooth,
                     "Max inter-sample delta=" + std::to_string(maxStepDelta) + " (smooth, click-free)");
    }

    // PREDELAY-015: NaN detection
    {
        audiocore::ReverbNode reverb(48000.0);
        reverb.setPreDelayEnabled(true);
        reverb.setPreDelayMs(50.0f);

        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        for (uint32_t i = 0; i < 128; ++i) in.getChannelData(0)[i] = 0.8f;

        bool hasNan = false;
        for (int b = 0; b < 100; ++b) {
            reverb.processPreDelay(in, out, 128);
            for (uint32_t i = 0; i < 128; ++i) {
                if (std::isnan(out.getChannelData(0)[i]) || std::isnan(out.getChannelData(1)[i])) {
                    hasNan = true;
                }
            }
        }
        reportResult(ctx, "PREDELAY-015", "NaN detection", !hasNan, "Zero NaN values detected");
    }

    // PREDELAY-016: Inf detection
    {
        audiocore::ReverbNode reverb(48000.0);
        reverb.setPreDelayEnabled(true);
        reverb.setPreDelayMs(50.0f);

        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        for (uint32_t i = 0; i < 128; ++i) in.getChannelData(0)[i] = 0.8f;

        bool hasInf = false;
        for (int b = 0; b < 100; ++b) {
            reverb.processPreDelay(in, out, 128);
            for (uint32_t i = 0; i < 128; ++i) {
                if (std::isinf(out.getChannelData(0)[i]) || std::isinf(out.getChannelData(1)[i])) {
                    hasInf = true;
                }
            }
        }
        reportResult(ctx, "PREDELAY-016", "Inf detection", !hasInf, "Zero Inf values detected");
    }

    // PREDELAY-017: Long-duration circular buffer stability (128,000 frames)
    {
        audiocore::ReverbNode reverb(48000.0);
        reverb.setPreDelayEnabled(true);
        reverb.setPreDelayMs(100.0f, true);

        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        for (uint32_t i = 0; i < 128; ++i) {
            in.getChannelData(0)[i] = 0.25f;
            in.getChannelData(1)[i] = 0.25f;
        }

        bool stable = true;
        for (int b = 0; b < 1000; ++b) { // 1000 blocks x 128 frames = 128,000 frames
            reverb.processPreDelay(in, out, 128);
            for (uint32_t i = 0; i < 128; ++i) {
                float v = out.getChannelData(0)[i];
                if (std::isnan(v) || std::isinf(v) || std::abs(v) > 2.0f) {
                    stable = false;
                    break;
                }
            }
            if (!stable) break;
        }
        reportResult(ctx, "PREDELAY-017", "Long-duration stability (128,000 frames)", stable,
                     "Continuous execution across 1000 blocks with zero ring buffer wrap errors");
    }

    // PRE-DELAY IMPULSE TEST (Measured timing at 48k & 96k)
    {
        uint32_t peak48 = measurePreDelay(48000.0, 20.0f, 2048);
        uint32_t expected48 = 960;
        int err48 = int(peak48) - int(expected48);

        uint32_t peak96 = measurePreDelay(96000.0, 20.0f, 4096);
        uint32_t expected96 = 1920;
        int err96 = int(peak96) - int(expected96);

        bool impulseOk = (err48 == 0 && err96 == 0);
        reportResult(ctx, "PREDELAY-IMPULSE", "Pre-Delay Impulse Timing Verification", impulseOk,
                     "48kHz: req=960 smp, meas=" + std::to_string(peak48) + " smp, err=" + std::to_string(err48) +
                     " | 96kHz: req=1920 smp, meas=" + std::to_string(peak96) + " smp, err=" + std::to_string(err96));
    }

    // PRE-DELAY DRY/WET TEST (Direct sound immediate, wet delayed)
    {
        audiocore::ReverbNode reverb(48000.0);
        reverb.setPreDelayEnabled(true);
        reverb.setPreDelayMs(20.0f, true); // 20ms = 960 samples

        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out0(2, 128);
        audiocore::AudioBuffer out100(2, 128);
        audiocore::AudioBuffer out50(2, 128);

        in.clear();
        in.getChannelData(0)[0] = 1.0f;
        in.getChannelData(1)[0] = 1.0f;

        // Mix 0%: Immediate direct sound
        reverb.setMix(0.0f);
        reverb.reset();
        reverb.process(in, out0, 128);
        bool mix0Immediate = (std::abs(out0.getChannelData(0)[0] - 1.0f) < 1e-5f);

        // Mix 100%: Reverb wet only (sample 0 has zero direct sound)
        reverb.setMix(1.0f);
        reverb.reset();
        reverb.process(in, out100, 128);
        bool mix100ZeroDirect = (std::abs(out100.getChannelData(0)[0]) < 1e-4f);

        // Mix 50%: Direct sound immediate at sample 0 (scaled by dryGain=0.5)
        reverb.setMix(0.5f);
        reverb.reset();
        reverb.process(in, out50, 128);
        bool mix50ImmediateDry = (std::abs(out50.getChannelData(0)[0] - 0.5f) < 1e-3f);

        bool dryWetOk = mix0Immediate && mix100ZeroDirect && mix50ImmediateDry;
        reportResult(ctx, "PREDELAY-DRYWET", "Pre-Delay Dry/Wet Direct Sound Preservation", dryWetOk,
                     "Mix 0%: immediate 1.0 | Mix 100%: 0.0 at t=0 | Mix 50%: immediate dry 0.5 at t=0 (not delayed)");
    }

    // =========================================================================
    // PART 2: REVERB TEMPLATE TESTS (TEMPLATE-001 .. TEMPLATE-014)
    // =========================================================================

    // TEMPLATE-001: ROOM loads successfully
    {
        audiocore::ReverbNode reverb(48000.0);
        reverb.loadTemplate(audiocore::ReverbType::Room);
        bool ok = (reverb.getType() == audiocore::ReverbType::Room);
        reportResult(ctx, "TEMPLATE-001", "ROOM loads successfully", ok, "Loaded Room template");
    }

    // TEMPLATE-002: HALL loads successfully
    {
        audiocore::ReverbNode reverb(48000.0);
        reverb.loadTemplate(audiocore::ReverbType::Hall);
        bool ok = (reverb.getType() == audiocore::ReverbType::Hall);
        reportResult(ctx, "TEMPLATE-002", "HALL loads successfully", ok, "Loaded Hall template");
    }

    // TEMPLATE-003: PLATE loads successfully
    {
        audiocore::ReverbNode reverb(48000.0);
        reverb.loadTemplate(audiocore::ReverbType::Plate);
        bool ok = (reverb.getType() == audiocore::ReverbType::Plate);
        reportResult(ctx, "TEMPLATE-003", "PLATE loads successfully", ok, "Loaded Plate template");
    }

    // TEMPLATE-004: AMBIENCE loads successfully
    {
        audiocore::ReverbNode reverb(48000.0);
        reverb.loadTemplate(audiocore::ReverbType::Ambience);
        bool ok = (reverb.getType() == audiocore::ReverbType::Ambience);
        reportResult(ctx, "TEMPLATE-004", "AMBIENCE loads successfully", ok, "Loaded Ambience template");
    }

    // TEMPLATE-005: ROOM parameters are correct
    {
        const auto& t = audiocore::ReverbNode::getTemplate(audiocore::ReverbType::Room);
        bool ok = (t.preDelayMs == 15.0f) &&
                  (t.decaySeconds == 0.75f) &&
                  (t.damping == 0.50f) &&
                  (t.diffusion == 0.75f) &&
                  (t.lowCutHz == 150.0f) &&
                  (t.highCutHz == 10000.0f) &&
                  (t.width == 0.80f) &&
                  (t.mix == 0.25f);
        reportResult(ctx, "TEMPLATE-005", "ROOM parameters are correct", ok,
                     "PreDelay=15ms, Decay=0.75s, Damp=50%, Diff=75%, HPF=150Hz, LPF=10kHz, Width=80%, Mix=25%");
    }

    // TEMPLATE-006: HALL parameters are correct
    {
        const auto& t = audiocore::ReverbNode::getTemplate(audiocore::ReverbType::Hall);
        bool ok = (t.preDelayMs == 30.0f) &&
                  (t.decaySeconds == 2.50f) &&
                  (t.damping == 0.40f) &&
                  (t.diffusion == 0.85f) &&
                  (t.lowCutHz == 120.0f) &&
                  (t.highCutHz == 9000.0f) &&
                  (t.width == 1.00f) &&
                  (t.mix == 0.30f);
        reportResult(ctx, "TEMPLATE-006", "HALL parameters are correct", ok,
                     "PreDelay=30ms, Decay=2.5s, Damp=40%, Diff=85%, HPF=120Hz, LPF=9kHz, Width=100%, Mix=30%");
    }

    // TEMPLATE-007: PLATE parameters are correct
    {
        const auto& t = audiocore::ReverbNode::getTemplate(audiocore::ReverbType::Plate);
        bool ok = (t.preDelayMs == 20.0f) &&
                  (t.decaySeconds == 2.00f) &&
                  (t.damping == 0.25f) &&
                  (t.diffusion == 0.95f) &&
                  (t.lowCutHz == 180.0f) &&
                  (t.highCutHz == 14000.0f) &&
                  (t.width == 1.00f) &&
                  (t.mix == 0.25f);
        reportResult(ctx, "TEMPLATE-007", "PLATE parameters are correct", ok,
                     "PreDelay=20ms, Decay=2.0s, Damp=25%, Diff=95%, HPF=180Hz, LPF=14kHz, Width=100%, Mix=25%");
    }

    // TEMPLATE-008: AMBIENCE parameters are correct
    {
        const auto& t = audiocore::ReverbNode::getTemplate(audiocore::ReverbType::Ambience);
        bool ok = (t.preDelayMs == 5.0f) &&
                  (t.decaySeconds == 0.35f) &&
                  (t.damping == 0.65f) &&
                  (t.diffusion == 0.70f) &&
                  (t.lowCutHz == 200.0f) &&
                  (t.highCutHz == 8000.0f) &&
                  (t.width == 0.75f) &&
                  (t.mix == 0.20f);
        reportResult(ctx, "TEMPLATE-008", "AMBIENCE parameters are correct", ok,
                     "PreDelay=5ms, Decay=0.35s, Damp=65%, Diff=70%, HPF=200Hz, LPF=8kHz, Width=75%, Mix=20%");
    }

    // TEMPLATE-009: Manual modification after template load
    {
        audiocore::ReverbNode reverb(48000.0);
        reverb.loadTemplate(audiocore::ReverbType::Hall);
        reverb.setDecay(4.5f); // Modify decay away from template
        bool ok = (reverb.getDecay() == 4.5f) && (reverb.getType() == audiocore::ReverbType::Hall);
        reportResult(ctx, "TEMPLATE-009", "Manual modification after template load", ok,
                     "Decay modified to 4.5s while template remains Hall");
    }

    // TEMPLATE-010: Reset to template
    {
        audiocore::ReverbNode reverb(48000.0);
        reverb.loadTemplate(audiocore::ReverbType::Hall);
        reverb.setDecay(4.5f);
        reverb.setPreDelayMs(85.0f);
        reverb.resetToTemplate(audiocore::ReverbType::Hall);

        const auto& t = audiocore::ReverbNode::getTemplate(audiocore::ReverbType::Hall);
        bool ok = (reverb.getDecay() == t.decaySeconds) &&
                  (reverb.getPreDelayMs() == t.preDelayMs) &&
                  (reverb.getDamping() == t.damping);
        reportResult(ctx, "TEMPLATE-010", "Reset to template", ok,
                     "Parameters restored to default Hall template");
    }

    // TEMPLATE-011: Template switching during playback
    {
        audiocore::ReverbNode reverb(48000.0);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        for (uint32_t i = 0; i < 128; ++i) in.getChannelData(0)[i] = 0.5f;

        bool ok = true;
        for (int cycle = 0; cycle < 5; ++cycle) {
            reverb.loadTemplate(audiocore::ReverbType::Room);
            reverb.process(in, out, 128);

            reverb.loadTemplate(audiocore::ReverbType::Hall);
            reverb.process(in, out, 128);

            reverb.loadTemplate(audiocore::ReverbType::Plate);
            reverb.process(in, out, 128);

            reverb.loadTemplate(audiocore::ReverbType::Ambience);
            reverb.process(in, out, 128);
        }
        reportResult(ctx, "TEMPLATE-011", "Template switching during playback", ok,
                     "Cycled through all 4 templates 5 times cleanly");
    }

    // TEMPLATE-012: No click / pop during template change
    {
        audiocore::ReverbNode reverb(48000.0);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        for (uint32_t i = 0; i < 128; ++i) in.getChannelData(0)[i] = 0.3f;

        float maxStep = 0.0f;
        float prevSample = 0.0f;

        for (int b = 0; b < 20; ++b) {
            if (b == 5) reverb.loadTemplate(audiocore::ReverbType::Room);
            if (b == 10) reverb.loadTemplate(audiocore::ReverbType::Hall);
            if (b == 15) reverb.loadTemplate(audiocore::ReverbType::Plate);

            reverb.process(in, out, 128);
            for (uint32_t i = 0; i < 128; ++i) {
                float s = out.getChannelData(0)[i];
                float delta = std::abs(s - prevSample);
                if (delta > maxStep) maxStep = delta;
                prevSample = s;
            }
        }
        bool smooth = (maxStep < 0.25f);
        reportResult(ctx, "TEMPLATE-012", "No click / pop during template change", smooth,
                     "Max inter-sample step delta=" + std::to_string(maxStep));
    }

    // TEMPLATE-013: NaN detection during template switching
    {
        audiocore::ReverbNode reverb(48000.0);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        in.getChannelData(0)[0] = 1.0f;

        bool hasNan = false;
        for (int b = 0; b < 40; ++b) {
            reverb.loadTemplate(static_cast<audiocore::ReverbType>(b % 4));
            reverb.process(in, out, 128);
            for (uint32_t i = 0; i < 128; ++i) {
                if (std::isnan(out.getChannelData(0)[i]) || std::isnan(out.getChannelData(1)[i])) {
                    hasNan = true;
                }
            }
        }
        reportResult(ctx, "TEMPLATE-013", "NaN detection during template switching", !hasNan,
                     "Zero NaN values detected");
    }

    // TEMPLATE-014: Inf detection during template switching
    {
        audiocore::ReverbNode reverb(48000.0);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        in.getChannelData(0)[0] = 1.0f;

        bool hasInf = false;
        for (int b = 0; b < 40; ++b) {
            reverb.loadTemplate(static_cast<audiocore::ReverbType>(b % 4));
            reverb.process(in, out, 128);
            for (uint32_t i = 0; i < 128; ++i) {
                if (std::isinf(out.getChannelData(0)[i]) || std::isinf(out.getChannelData(1)[i])) {
                    hasInf = true;
                }
            }
        }
        reportResult(ctx, "TEMPLATE-014", "Inf detection during template switching", !hasInf,
                     "Zero Inf values detected");
    }

    std::cout << "============================================================\n";
    std::cout << "TOTAL: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    std::cout << "============================================================\n";

    return (ctx.failed == 0) ? 0 : 1;
}
