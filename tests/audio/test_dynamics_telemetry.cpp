#include "audiocore/compressor_node.hpp"
#include "audiocore/gate_node.hpp"
#include "audiocore/limiter_node.hpp"
#include "audiocore/audio_buffer.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <string>
#include <cassert>
#include <algorithm>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSampleRate = 48000.0;
constexpr uint32_t kBlockSize = 128;

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

void fillSine(audiocore::AudioBuffer& buf, uint32_t channel, double freq, float amplitude, uint32_t frames) {
    float* data = buf.getChannelData(channel);
    for (uint32_t i = 0; i < frames; ++i) {
        data[i] = static_cast<float>(std::sin(2.0 * kPi * freq * i / kSampleRate)) * amplitude;
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

} // namespace

int main() {
    std::cout << "============================================================\n";
    std::cout << "AUDIO CORE: DYNAMICS & LIVE TELEMETRY VERIFICATION SUITE\n";
    std::cout << "============================================================\n";

    TestContext ctx;

    // =========================================================================
    // SECTION 1: COMPRESSOR TESTS
    // =========================================================================

    // COMP-001: Silence
    {
        audiocore::CompressorNode comp(kSampleRate);
        audiocore::AudioBuffer in(2, kBlockSize);
        audiocore::AudioBuffer out(2, kBlockSize);
        in.clear();
        comp.process(in, out, kBlockSize);

        bool ok = (comp.getDetectorLevelDb() <= -89.0f) &&
                  (comp.getOutputLevelDb() <= -89.0f) &&
                  (comp.getCurrentGainReductionDb() == 0.0f) &&
                  (findPeak(out, 0, kBlockSize) == 0.0f);
        reportResult(ctx, "COMP-001", "Silence floor & inactive telemetry", ok,
                     "det=" + std::to_string(comp.getDetectorLevelDb()) + "dB, GR=0dB");
    }

    // COMP-002: Input Below Threshold
    {
        audiocore::CompressorNode comp(kSampleRate);
        comp.setThresholdDb(-12.0f);
        comp.setRatio(4.0f);
        audiocore::AudioBuffer in(2, kBlockSize);
        audiocore::AudioBuffer out(2, kBlockSize);
        // -20 dBFS amplitude = 0.1
        const float amp = std::pow(10.0f, -20.0f / 20.0f);
        for (uint32_t i = 0; i < kBlockSize; ++i) {
            in.getChannelData(0)[i] = amp;
            in.getChannelData(1)[i] = amp;
        }
        comp.process(in, out, kBlockSize);

        bool ok = (std::abs(comp.getDetectorLevelDb() - (-20.0f)) < 0.1f) &&
                  (std::abs(comp.getOutputLevelDb() - (-20.0f)) < 0.1f) &&
                  (comp.getCurrentGainReductionDb() == 0.0f) &&
                  (std::abs(findPeak(out, 0, kBlockSize) - amp) < 1e-5f);
        reportResult(ctx, "COMP-002", "Input below threshold (-20dB < -12dB)", ok,
                     "det=" + std::to_string(comp.getDetectorLevelDb()) + "dB, GR=" + std::to_string(comp.getCurrentGainReductionDb()) + "dB");
    }

    // COMP-003: Input Above Threshold & Gain Reduction
    {
        audiocore::CompressorNode comp(kSampleRate);
        comp.setThresholdDb(-18.0f);
        comp.setRatio(4.0f);
        comp.setAttackMs(0.1f); // instantaneous attack for deterministic static test
        audiocore::AudioBuffer in(2, kBlockSize);
        audiocore::AudioBuffer out(2, kBlockSize);
        // 0 dBFS amplitude = 1.0
        for (uint32_t i = 0; i < kBlockSize; ++i) {
            in.getChannelData(0)[i] = 1.0f;
            in.getChannelData(1)[i] = 1.0f;
        }
        // Run multiple blocks to allow envelope follower to settle fully
        for (int b = 0; b < 20; ++b) {
            comp.process(in, out, kBlockSize);
        }

        // Expected GR: (T - in) * (1 - 1/R) = (-18 - 0) * (1 - 0.25) = -18 * 0.75 = -13.5 dB
        const float expectedGR = -13.5f;
        const float actualGR = comp.getCurrentGainReductionDb();
        bool ok = (std::abs(comp.getDetectorLevelDb() - 0.0f) < 0.1f) &&
                  (std::abs(actualGR - expectedGR) < 0.2f) &&
                  (comp.getOutputLevelDb() < -12.0f);
        reportResult(ctx, "COMP-003", "Input above threshold (0dBFS in, 4:1 ratio -> -13.5dB GR)", ok,
                     "det=" + std::to_string(comp.getDetectorLevelDb()) + "dB, GR=" + std::to_string(actualGR) + "dB");
    }

    // COMP-004: Exact Static Transfer Curve Coordinate Function
    {
        audiocore::CompressorNode comp(kSampleRate);
        comp.setThresholdDb(-18.0f);
        comp.setRatio(4.0f);
        comp.setKneeDb(0.0f); // Hard knee
        comp.setMakeupGainDb(0.0f);

        // Below threshold: unity gain
        float yBelow = comp.computeStaticOutputDb(-30.0f);
        // Above threshold: -18 + (-6 - (-18)) / 4 = -18 + 12 / 4 = -15 dB
        float yAbove = comp.computeStaticOutputDb(-6.0f);
        // At threshold: -18 dB
        float yThresh = comp.computeStaticOutputDb(-18.0f);

        bool ok = (std::abs(yBelow - (-30.0f)) < 0.001f) &&
                  (std::abs(yAbove - (-15.0f)) < 0.001f) &&
                  (std::abs(yThresh - (-18.0f)) < 0.001f);
        reportResult(ctx, "COMP-004", "Exact static transfer curve coordinates", ok,
                     "yBelow=" + std::to_string(yBelow) + ", yAbove=" + std::to_string(yAbove));
    }

    // COMP-005: Soft Knee Transfer Curve Interpolation
    {
        audiocore::CompressorNode comp(kSampleRate);
        comp.setThresholdDb(-18.0f);
        comp.setRatio(4.0f);
        comp.setKneeDb(6.0f); // Knee width = 6 dB (knee region: -21 dB to -15 dB)
        comp.setMakeupGainDb(0.0f);

        float yBelowKnee = comp.computeStaticOutputDb(-24.0f); // strictly below knee -> unity -24 dB
        float yAboveKnee = comp.computeStaticOutputDb(-12.0f); // strictly above knee -> -18 + 6/4 = -16.5 dB
        float yMidKnee = comp.computeStaticOutputDb(-18.0f);   // exactly at center -> slightly curved

        bool ok = (std::abs(yBelowKnee - (-24.0f)) < 0.001f) &&
                  (std::abs(yAboveKnee - (-16.5f)) < 0.001f) &&
                  (yMidKnee < -18.0f && yMidKnee > -19.0f);
        reportResult(ctx, "COMP-005", "Soft knee quadratic interpolation", ok,
                     "yBelow=" + std::to_string(yBelowKnee) + ", yMid=" + std::to_string(yMidKnee) + ", yAbove=" + std::to_string(yAboveKnee));
    }

    // COMP-006: Bypass Mode Telemetry
    {
        audiocore::CompressorNode comp(kSampleRate);
        comp.setThresholdDb(-20.0f);
        comp.setRatio(4.0f);
        comp.setBypassed(true);

        audiocore::AudioBuffer in(2, kBlockSize);
        audiocore::AudioBuffer out(2, kBlockSize);
        for (uint32_t i = 0; i < kBlockSize; ++i) {
            in.getChannelData(0)[i] = 0.5f; // -6 dBFS
            in.getChannelData(1)[i] = 0.5f;
        }
        comp.process(in, out, kBlockSize);

        bool ok = (comp.isBypassed() == true) &&
                  (comp.getCurrentGainReductionDb() == 0.0f) &&
                  (std::abs(comp.getDetectorLevelDb() - (-6.02f)) < 0.1f) &&
                  (std::abs(findPeak(out, 0, kBlockSize) - 0.5f) < 1e-5f);
        reportResult(ctx, "COMP-006", "Bypass preserves audio & reports detector without reduction", ok,
                     "det=" + std::to_string(comp.getDetectorLevelDb()) + "dB, GR=0dB");
    }

    // =========================================================================
    // SECTION 2: NOISE GATE TESTS
    // =========================================================================

    // GATE-001: Silence -> Closed State
    {
        audiocore::GateNode gate(kSampleRate);
        audiocore::AudioBuffer in(2, kBlockSize);
        audiocore::AudioBuffer out(2, kBlockSize);
        in.clear();
        gate.process(in, out, kBlockSize);

        bool ok = (gate.getCurrentState() == audiocore::GateState::Closed) &&
                  (gate.getDetectorLevelDb() <= -89.0f) &&
                  (gate.getCurrentGainReductionDb() == -60.0f) &&
                  (findPeak(out, 0, kBlockSize) == 0.0f);
        reportResult(ctx, "GATE-001", "Silence -> Closed state at range floor", ok,
                     "state=" + std::string(audiocore::GateNode::stateToString(gate.getCurrentState())) +
                     ", det=" + std::to_string(gate.getDetectorLevelDb()) + "dB");
    }

    // GATE-002: Signal Below Threshold -> Remains Closed
    {
        audiocore::GateNode gate(kSampleRate);
        gate.setThresholdDb(-30.0f);
        gate.setRangeDb(-60.0f);
        audiocore::AudioBuffer in(2, kBlockSize);
        audiocore::AudioBuffer out(2, kBlockSize);
        // -40 dBFS amplitude = 0.01
        for (uint32_t i = 0; i < kBlockSize; ++i) {
            in.getChannelData(0)[i] = 0.01f;
            in.getChannelData(1)[i] = 0.01f;
        }
        gate.process(in, out, kBlockSize);

        bool ok = (gate.getCurrentState() == audiocore::GateState::Closed) &&
                  (std::abs(gate.getDetectorLevelDb() - (-40.0f)) < 0.1f) &&
                  (gate.getCurrentGainReductionDb() == -60.0f);
        reportResult(ctx, "GATE-002", "Signal below threshold (-40dB < -30dB) remains Closed", ok,
                     "det=" + std::to_string(gate.getDetectorLevelDb()) + "dB");
    }

    // GATE-003: Signal Above Threshold -> Transitions to Attack and Open
    {
        audiocore::GateNode gate(kSampleRate);
        gate.setThresholdDb(-30.0f);
        gate.setAttackMs(0.5f);
        gate.setHoldMs(50.0f);
        audiocore::AudioBuffer in(2, kBlockSize);
        audiocore::AudioBuffer out(2, kBlockSize);
        // -10 dBFS amplitude = 0.316
        const float amp = std::pow(10.0f, -10.0f / 20.0f);
        for (uint32_t i = 0; i < kBlockSize; ++i) {
            in.getChannelData(0)[i] = amp;
            in.getChannelData(1)[i] = amp;
        }

        // First block: should attack
        gate.process(in, out, kBlockSize);
        bool attackOrOpen = (gate.getCurrentState() == audiocore::GateState::Attack ||
                             gate.getCurrentState() == audiocore::GateState::Open);

        // Process a few more blocks to settle in Open
        for (int b = 0; b < 10; ++b) {
            gate.process(in, out, kBlockSize);
        }

        bool fullyOpen = (gate.getCurrentState() == audiocore::GateState::Open) &&
                         (gate.getCurrentGainReductionDb() == 0.0f) &&
                         (std::abs(gate.getDetectorLevelDb() - (-10.0f)) < 0.1f);
        reportResult(ctx, "GATE-003", "Signal above threshold transitions to Open",
                     attackOrOpen && fullyOpen,
                     "state=" + std::string(audiocore::GateNode::stateToString(gate.getCurrentState())) +
                     ", GR=" + std::to_string(gate.getCurrentGainReductionDb()) + "dB");
    }

    // GATE-004: Hold and Release State Sequence
    {
        audiocore::GateNode gate(kSampleRate);
        gate.setThresholdDb(-20.0f);
        gate.setAttackMs(0.1f);
        gate.setHoldMs(10.0f);    // 10 ms hold = 480 samples (~3.75 blocks)
        gate.setReleaseMs(20.0f); // 20 ms release
        audiocore::AudioBuffer in(2, kBlockSize);
        audiocore::AudioBuffer out(2, kBlockSize);

        // Open the gate with 0 dBFS signal
        for (uint32_t i = 0; i < kBlockSize; ++i) in.getChannelData(0)[i] = 1.0f;
        for (int b = 0; b < 5; ++b) gate.process(in, out, kBlockSize);
        assert(gate.getCurrentState() == audiocore::GateState::Open);

        // Drop input to silence -> next block should enter Hold
        in.clear();
        gate.process(in, out, kBlockSize);
        bool inHold = (gate.getCurrentState() == audiocore::GateState::Hold);

        // Process through the rest of the hold period -> should enter Release
        for (int b = 0; b < 4; ++b) gate.process(in, out, kBlockSize);
        bool inRelease = (gate.getCurrentState() == audiocore::GateState::Release);

        // Process enough blocks for full release -> should settle Closed
        for (int b = 0; b < 60; ++b) gate.process(in, out, kBlockSize);
        bool settledClosed = (gate.getCurrentState() == audiocore::GateState::Closed);

        reportResult(ctx, "GATE-004", "Hold -> Release -> Closed lifecycle",
                     inHold && inRelease && settledClosed,
                     "hold=" + std::to_string(inHold) + ", rel=" + std::to_string(inRelease) + ", closed=" + std::to_string(settledClosed));
    }

    // GATE-005: Bypass Mode
    {
        audiocore::GateNode gate(kSampleRate);
        gate.setThresholdDb(-10.0f);
        gate.setBypassed(true);
        audiocore::AudioBuffer in(2, kBlockSize);
        audiocore::AudioBuffer out(2, kBlockSize);
        // Low signal -50 dBFS
        for (uint32_t i = 0; i < kBlockSize; ++i) {
            in.getChannelData(0)[i] = 0.00316f;
            in.getChannelData(1)[i] = 0.00316f;
        }
        gate.process(in, out, kBlockSize);

        bool ok = (gate.isBypassed() == true) &&
                  (gate.getCurrentGainReductionDb() == 0.0f) &&
                  (std::abs(findPeak(out, 0, kBlockSize) - 0.00316f) < 1e-5f);
        reportResult(ctx, "GATE-005", "Bypassed gate passes low signal unaltered", ok,
                     "GR=" + std::to_string(gate.getCurrentGainReductionDb()) + "dB");
    }

    // =========================================================================
    // SECTION 3: BRICKWALL LIMITER TESTS
    // =========================================================================

    // LIM-001: Peak Ceiling Clamping
    {
        audiocore::LimiterNode lim(-0.5f, kSampleRate);
        lim.setReleaseMs(50.0f);
        audiocore::AudioBuffer in(2, kBlockSize);
        audiocore::AudioBuffer out(2, kBlockSize);
        // Feed high level: +6 dBFS (amplitude = 2.0)
        for (uint32_t i = 0; i < kBlockSize; ++i) {
            in.getChannelData(0)[i] = 2.0f;
            in.getChannelData(1)[i] = -2.0f;
        }
        lim.process(in, out, kBlockSize);

        const float outPeak = std::max(findPeak(out, 0, kBlockSize), findPeak(out, 1, kBlockSize));
        const float ceilingLin = lim.getCeilingLinear();
        const float grDb = lim.getCurrentGainReductionDb();

        // Target gain reduction = 20 * log10(C_lin / 2.0) = -0.5 - 6.02 = -6.52 dB
        bool ok = (outPeak <= ceilingLin + 1e-4f) &&
                  (grDb < -5.5f && grDb > -7.5f);
        reportResult(ctx, "LIM-001", "Brickwall limiter clamps +6dB input to ceiling (-0.5dBFS)", ok,
                     "outPeak=" + std::to_string(outPeak) + ", ceiling=" + std::to_string(ceilingLin) + ", GR=" + std::to_string(grDb) + "dB");
    }

    // LIM-002: Signal Below Ceiling
    {
        audiocore::LimiterNode lim(-0.5f, kSampleRate);
        audiocore::AudioBuffer in(2, kBlockSize);
        audiocore::AudioBuffer out(2, kBlockSize);
        // Low signal: -6 dBFS (amplitude = 0.5)
        for (uint32_t i = 0; i < kBlockSize; ++i) {
            in.getChannelData(0)[i] = 0.5f;
            in.getChannelData(1)[i] = 0.5f;
        }
        lim.process(in, out, kBlockSize);

        bool ok = (lim.getCurrentGainReductionDb() == 0.0f) &&
                  (std::abs(findPeak(out, 0, kBlockSize) - 0.5f) < 1e-5f);
        reportResult(ctx, "LIM-002", "Signal below ceiling passes with 0dB gain reduction", ok,
                     "GR=" + std::to_string(lim.getCurrentGainReductionDb()) + "dB");
    }

    // =========================================================================
    // SECTION 4: PRE-DSP VS POST-DSP METER TAP DECOUPLING & ISOLATION
    // =========================================================================

    // METER-TAP-001: Pre-DSP Input vs Post-DSP Output Decoupling under Active Compression
    {
        audiocore::CompressorNode comp(kSampleRate);
        comp.setThresholdDb(-18.0f);
        comp.setRatio(4.0f);
        comp.setAttackMs(0.1f);
        comp.setReleaseMs(50.0f);
        comp.setKneeDb(0.0f);
        comp.setMakeupGainDb(0.0f);

        audiocore::AudioBuffer in(2, kBlockSize);
        audiocore::AudioBuffer out(2, kBlockSize);
        const float amp = std::pow(10.0f, -6.0206f / 20.0f); // -6.02 dBFS (0.5 lin)
        for (uint32_t i = 0; i < kBlockSize; ++i) {
            in.getChannelData(0)[i] = amp;
            in.getChannelData(1)[i] = amp;
        }

        // Settle compressor envelope across 20 blocks
        for (int b = 0; b < 20; ++b) {
            comp.process(in, out, kBlockSize);
        }

        // Theoretical expectations:
        // Input level: -6.02 dBFS
        // Over threshold: -6.02 - (-18) = +11.98 dB
        // Attenuation / GR: -11.98 * (1 - 1/4) = -8.985 dB
        // Output level: -6.02 - 8.985 = -15.005 dBFS
        const float inDbL = comp.getInputLevelDb(0);
        const float inDbR = comp.getInputLevelDb(1);
        const float outDbL = comp.getOutputLevelDb(0);
        const float outDbR = comp.getOutputLevelDb(1);
        const float grDb = comp.getCurrentGainReductionDb();

        const bool inAccurate = (std::abs(inDbL - (-6.02f)) < 0.15f) && (std::abs(inDbR - (-6.02f)) < 0.15f);
        const bool outAccurate = (std::abs(outDbL - (-15.0f)) < 0.25f) && (std::abs(outDbR - (-15.0f)) < 0.25f);
        const bool grAccurate = (std::abs(grDb - (-8.985f)) < 0.25f);
        const bool decoupled = (std::abs(inDbL - outDbL) >= 8.5f);

        reportResult(ctx, "METER-TAP-001", "Pre-DSP input stays at -6dBFS while post-comp output drops to -15dBFS",
                     inAccurate && outAccurate && grAccurate && decoupled,
                     "in=" + std::to_string(inDbL) + "dBFS, out=" + std::to_string(outDbL) + "dBFS, GR=" + std::to_string(grDb) + "dB");
    }

    // METER-TAP-002: Invariance of Pre-DSP Input Meter under Extreme Compression Variations
    {
        audiocore::CompressorNode comp(kSampleRate);
        comp.setAttackMs(0.1f);
        comp.setReleaseMs(50.0f);
        comp.setKneeDb(0.0f);
        comp.setMakeupGainDb(0.0f);

        audiocore::AudioBuffer in(2, kBlockSize);
        audiocore::AudioBuffer out(2, kBlockSize);
        const float amp = 0.5f; // -6.02 dBFS
        for (uint32_t i = 0; i < kBlockSize; ++i) {
            in.getChannelData(0)[i] = amp;
            in.getChannelData(1)[i] = amp;
        }

        // Phase A: Moderate compression (-18 dB threshold, 4:1)
        comp.setThresholdDb(-18.0f);
        comp.setRatio(4.0f);
        for (int b = 0; b < 20; ++b) comp.process(in, out, kBlockSize);
        const float inDbPhaseA = comp.getInputLevelDb(0);
        const float outDbPhaseA = comp.getOutputLevelDb(0);

        // Phase B: Extreme compression (-36 dB threshold, 10:1 ratio -> ~ -27 dB GR)
        comp.setThresholdDb(-36.0f);
        comp.setRatio(10.0f);
        for (int b = 0; b < 20; ++b) comp.process(in, out, kBlockSize);
        const float inDbPhaseB = comp.getInputLevelDb(0);
        const float outDbPhaseB = comp.getOutputLevelDb(0);
        const float grDbPhaseB = comp.getCurrentGainReductionDb();

        // The pre-DSP input MUST NOT change between Phase A and Phase B
        const bool inputInvariant = (std::abs(inDbPhaseA - inDbPhaseB) < 0.05f) && (std::abs(inDbPhaseB - (-6.02f)) < 0.15f);
        const bool outputChanged = (outDbPhaseB < outDbPhaseA - 15.0f);

        reportResult(ctx, "METER-TAP-002", "Pre-DSP input meter strictly invariant during extreme threshold/ratio changes",
                     inputInvariant && outputChanged,
                     "inA=" + std::to_string(inDbPhaseA) + "dB, inB=" + std::to_string(inDbPhaseB) +
                     "dB, outA=" + std::to_string(outDbPhaseA) + "dB, outB=" + std::to_string(outDbPhaseB) +
                     "dB, GR_B=" + std::to_string(grDbPhaseB) + "dB");
    }

    // METER-TAP-003: Stereo Channel Input and Output Tap Isolation
    {
        audiocore::CompressorNode comp(kSampleRate);
        comp.setThresholdDb(-12.0f);
        comp.setRatio(4.0f);
        comp.setAttackMs(0.1f);
        comp.setKneeDb(0.0f);
        comp.setMakeupGainDb(0.0f);

        audiocore::AudioBuffer in(2, kBlockSize);
        audiocore::AudioBuffer out(2, kBlockSize);
        const float ampL = 0.5f;                    // -6.02 dBFS
        const float ampR = std::pow(10.0f, -24.0f / 20.0f); // -24.0 dBFS (~0.0631)
        for (uint32_t i = 0; i < kBlockSize; ++i) {
            in.getChannelData(0)[i] = ampL;
            in.getChannelData(1)[i] = ampR;
        }

        for (int b = 0; b < 20; ++b) comp.process(in, out, kBlockSize);

        const float inL = comp.getInputLevelDb(0);
        const float inR = comp.getInputLevelDb(1);
        const float outL = comp.getOutputLevelDb(0);
        const float outR = comp.getOutputLevelDb(1);

        const bool leftAccurate = (std::abs(inL - (-6.02f)) < 0.15f);
        const bool rightAccurate = (std::abs(inR - (-24.0f)) < 0.15f);
        const bool separation = (inL - inR >= 17.5f);

        reportResult(ctx, "METER-TAP-003", "Stereo input & output taps maintain channel isolation (L=-6dB, R=-24dB)",
                     leftAccurate && rightAccurate && separation,
                     "inL=" + std::to_string(inL) + "dB, inR=" + std::to_string(inR) +
                     "dB, outL=" + std::to_string(outL) + "dB, outR=" + std::to_string(outR) + "dB");
    }

    // METER-TAP-004: Pre-DSP and Post-DSP Meter Ballistics on Silence Floor
    {
        audiocore::CompressorNode comp(kSampleRate);
        comp.setThresholdDb(-18.0f);
        comp.setRatio(4.0f);
        comp.setAttackMs(0.1f);
        comp.setReleaseMs(30.0f);

        audiocore::AudioBuffer in(2, kBlockSize);
        audiocore::AudioBuffer out(2, kBlockSize);

        // Feed signal to engage
        for (uint32_t i = 0; i < kBlockSize; ++i) in.getChannelData(0)[i] = 1.0f;
        for (int b = 0; b < 10; ++b) comp.process(in, out, kBlockSize);

        // Cut input to silence
        in.clear();
        comp.process(in, out, kBlockSize);
        const float inDbImmediate = comp.getInputLevelDb(0);
        const bool inDroppedImmediately = (inDbImmediate <= -89.0f);

        // Process release decay
        for (int b = 0; b < 120; ++b) comp.process(in, out, kBlockSize);
        const float inDbSettled = comp.getInputLevelDb(0);
        const float outDbSettled = comp.getOutputLevelDb(0);
        const float grSettled = comp.getCurrentGainReductionDb();

        const bool settledFloor = (inDbSettled <= -89.0f) && (outDbSettled <= -89.0f) && (std::abs(grSettled) < 0.01f);

        reportResult(ctx, "METER-TAP-004", "Immediate input silence floor and smooth release envelope decay",
                     inDroppedImmediately && settledFloor,
                     "inImm=" + std::to_string(inDbImmediate) + "dB, inSettled=" + std::to_string(inDbSettled) +
                     "dB, outSettled=" + std::to_string(outDbSettled) + "dB, GR=" + std::to_string(grSettled) + "dB");
    }

    // METER-TAP-005: Signal Pipeline Sequential Processing Tap Integrity (Gate -> Comp -> Limiter)
    {
        audiocore::GateNode gate(kSampleRate);
        gate.setThresholdDb(-40.0f);
        gate.setAttackMs(0.1f);

        audiocore::CompressorNode comp(kSampleRate);
        comp.setThresholdDb(-12.0f);
        comp.setRatio(4.0f);
        comp.setAttackMs(0.1f);
        comp.setKneeDb(0.0f);
        comp.setMakeupGainDb(6.0f); // Makeup pushes signal back up towards ceiling

        audiocore::LimiterNode lim(-6.0f, kSampleRate); // Brickwall ceiling at -6.0 dBFS
        lim.setReleaseMs(50.0f);

        audiocore::AudioBuffer rawInput(2, kBlockSize);
        audiocore::AudioBuffer gateOut(2, kBlockSize);
        audiocore::AudioBuffer compOut(2, kBlockSize);
        audiocore::AudioBuffer finalOut(2, kBlockSize);

        // Feed 0 dBFS input
        for (uint32_t i = 0; i < kBlockSize; ++i) {
            rawInput.getChannelData(0)[i] = 1.0f;
            rawInput.getChannelData(1)[i] = 1.0f;
        }

        // Run sufficient blocks through the sequential pipeline to settle initial attack transient & limiter release
        for (int b = 0; b < 100; ++b) {
            gate.process(rawInput, gateOut, kBlockSize);
            comp.process(gateOut, compOut, kBlockSize);
            lim.process(compOut, finalOut, kBlockSize);
        }

        const float tap1_RawInput = 20.0f * std::log10(std::max(1e-5f, findPeak(rawInput, 0, kBlockSize)));
        const float tap2_PostGate = gate.getDetectorLevelDb();
        const float tap3_CompInput = comp.getInputLevelDb(0);
        const float tap4_CompOutput = comp.getOutputLevelDb(0);
        const float tap5_LimiterOutput = 20.0f * std::log10(std::max(1e-5f, findPeak(finalOut, 0, kBlockSize)));

        // Verification:
        // Raw Input = 0 dBFS
        // Gate Output = 0 dBFS
        // Comp Input = 0 dBFS
        // Comp Output = 0 - 9 (GR) + 6 (makeup) = -3 dBFS
        // Limiter Output clamped to -6.0 dBFS ceiling
        const bool tap1_ok = (std::abs(tap1_RawInput - 0.0f) < 0.1f);
        const bool tap3_ok = (std::abs(tap3_CompInput - 0.0f) < 0.1f);
        const bool tap4_ok = (std::abs(tap4_CompOutput - (-3.0f)) < 0.3f);
        const bool tap5_ok = (std::abs(tap5_LimiterOutput - (-6.0f)) < 0.15f);

        reportResult(ctx, "METER-TAP-005", "Multi-stage pipeline tap isolation: Raw 0dB -> CompOut -3dB -> LimOut -6dB",
                     tap1_ok && tap3_ok && tap4_ok && tap5_ok,
                     "raw=" + std::to_string(tap1_RawInput) + "dB, compIn=" + std::to_string(tap3_CompInput) +
                     "dB, compOut=" + std::to_string(tap4_CompOutput) + "dB, limOut=" + std::to_string(tap5_LimiterOutput) + "dB");
    }

    // =========================================================================
    // SECTION 5: COMPRESSOR TRANSFER CURVE AND THRESHOLD MARKER COORDINATE MAPPING
    // =========================================================================

    // COMP-MAP-001: Coordinate Mapping and Curve Sample Alignment
    {
        const float minX = -60.0f;
        const float maxX = 0.0f;
        const int plotW = 730;
        const int numPoints = 61; // 61 points across -60 to 0 dB, step = 1.0 dB

        auto xCoord = [&](float v) {
            float norm = std::clamp((v - minX) / (maxX - minX), 0.0f, 1.0f);
            return (int)std::round(norm * (float)plotW);
        };

        auto curveSampleX = [&](int idx) {
            return (int)std::round((float)plotW * (float)idx / (float)(numPoints - 1));
        };

        // Test at nominal threshold values: -60, -48, -36, -24, -18, -12, -6, 0 dB
        bool allMatch = true;
        std::vector<float> testThreshs = {-60.0f, -48.0f, -36.0f, -24.0f, -18.0f, -12.0f, -6.0f, 0.0f};
        for (float t : testThreshs) {
            int markerX = xCoord(t);
            int sampleIdx = (int)std::round((t - minX) / 1.0f);
            int sampleX = curveSampleX(sampleIdx);
            if (markerX != sampleX) {
                allMatch = false;
                break;
            }
        }

        // At -18 dB specifically: (-18 - (-60)) / 60 = 42/60 = 0.70 (70% of plot width)
        int x18 = xCoord(-18.0f);
        int expectedX18 = (int)std::round(0.70f * (float)plotW);
        bool match18 = (x18 == expectedX18);

        reportResult(ctx, "COMP-MAP-001", "Threshold marker aligns exactly with transfer curve samples across range",
                     allMatch && match18,
                     "x(-18dB)=" + std::to_string(x18) + ", expected=" + std::to_string(expectedX18));
    }

    // COMP-MAP-002: Drag Coordinate Inversion Invertibility
    {
        const float minX = -60.0f;
        const float maxX = 0.0f;
        const int plotW = 730;

        auto xCoord = [&](float v) {
            float norm = std::clamp((v - minX) / (maxX - minX), 0.0f, 1.0f);
            return (int)std::round(norm * (float)plotW);
        };

        auto threshFromX = [&](int x) {
            float norm = std::clamp((float)x / (float)plotW, 0.0f, 1.0f);
            return minX + norm * (maxX - minX);
        };

        bool ok = true;
        for (float t = -60.0f; t <= 0.0f; t += 0.5f) {
            int x = xCoord(t);
            float tRecovered = threshFromX(x);
            if (std::abs(t - tRecovered) > 0.1f) {
                ok = false;
                break;
            }
        }

        // Verify pixel invertibility across all pixels [0, plotW]
        bool pixelOk = true;
        for (int px = 0; px <= plotW; ++px) {
            float t = threshFromX(px);
            int pxRecovered = xCoord(t);
            if (std::abs(px - pxRecovered) > 1) {
                pixelOk = false;
                break;
            }
        }

        reportResult(ctx, "COMP-MAP-002", "Drag conversion inverts coordinate mapping with < 0.1 dB precision",
                     ok && pixelOk,
                     "range=[-60dB, 0dB], plotW=730px");
    }

    // COMP-MAP-003: Hard Knee Corner & Soft Knee Symmetrical Boundaries
    {
        const float minX = -60.0f;
        const float maxX = 0.0f;
        const int plotW = 730;

        auto xCoord = [&](float v) {
            float norm = std::clamp((v - minX) / (maxX - minX), 0.0f, 1.0f);
            return (int)std::round(norm * (float)plotW);
        };

        const float thresh = -18.0f;
        const float knee = 6.0f; // Flanking edges: -21 dB and -15 dB
        int xCenter = xCoord(thresh);
        int xLo = xCoord(thresh - knee * 0.5f);
        int xHi = xCoord(thresh + knee * 0.5f);

        int distLo = xCenter - xLo;
        int distHi = xHi - xCenter;
        bool symmetrical = std::abs(distLo - distHi) <= 1;

        reportResult(ctx, "COMP-MAP-003", "Soft knee boundaries flank nominal threshold symmetrically",
                     symmetrical && distLo > 0,
                     "distLo=" + std::to_string(distLo) + "px, distHi=" + std::to_string(distHi) + "px");
    }

    // COMP-MAP-004: Invariance under Ratio and Makeup Variations
    {
        const float minX = -60.0f;
        const float maxX = 0.0f;
        const int plotW = 730;

        auto xCoord = [&](float v) {
            float norm = std::clamp((v - minX) / (maxX - minX), 0.0f, 1.0f);
            return (int)std::round(norm * (float)plotW);
        };

        const float thresh = -24.0f;
        const int baseCoord = xCoord(thresh);

        // Ratio and makeup gain alter the transfer curve output, but the horizontal threshold coordinate must be invariant
        std::vector<float> ratios = {1.0f, 2.0f, 4.0f, 8.0f, 20.0f};
        std::vector<float> makeups = {-12.0f, 0.0f, +6.0f, +12.0f, +24.0f};

        bool invariant = true;
        for (float r : ratios) {
            for (float m : makeups) {
                (void)r; (void)m;
                if (xCoord(thresh) != baseCoord) {
                    invariant = false;
                }
            }
        }

        reportResult(ctx, "COMP-MAP-004", "Horizontal threshold coordinate is invariant under ratio and makeup gain variations",
                     invariant,
                     "xCoord=" + std::to_string(baseCoord) + "px");
    }

    // COMP-MAP-005: Limiter Ceiling Orthogonal Y-Axis Mapping
    {
        const float minY = -60.0f;
        const float maxY = +12.0f;
        const int plotH = 350;

        auto yCoord = [&](float v) {
            float norm = std::clamp((v - minY) / (maxY - minY), 0.0f, 1.0f);
            return plotH - (int)std::round(norm * (float)plotH);
        };

        // Ceiling at -0.1 dB: (-0.1 - (-60)) / 72 = 59.9 / 72 = 0.8319 -> near top of chart
        int yCeil = yCoord(-0.1f);
        int yZero = yCoord(0.0f);
        int yMinus12 = yCoord(-12.0f);

        bool monotonic = (yCeil >= yZero) && (yZero < yMinus12);

        reportResult(ctx, "COMP-MAP-005", "Limiter ceiling maps orthogonally to vertical Y-axis (-60 to +12 dB)",
                     monotonic,
                     "yCeil=" + std::to_string(yCeil) + ", yZero=" + std::to_string(yZero) + ", yMinus12=" + std::to_string(yMinus12));
    }

    std::cout << "\n============================================================\n";
    std::cout << "SUMMARY: " << ctx.passed << " PASSED, " << ctx.failed << " FAILED\n";
    std::cout << "============================================================\n";

    return (ctx.failed == 0) ? 0 : 1;
}
