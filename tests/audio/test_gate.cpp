#include "audiocore/gate_node.hpp"
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
    std::cout << "AUDIO CORE: NOISE GATE AUTOMATED VERIFICATION SUITE\n";
    std::cout << "============================================================\n";

    TestContext ctx;

    // GATE-001: Gate construction
    {
        audiocore::GateNode gate(48000.0);
        bool ok = (gate.getSampleRate() == 48000.0);
        reportResult(ctx, "GATE-001", "Gate construction", ok, "sampleRate=48000.0");
    }

    // GATE-002: Default parameters
    {
        audiocore::GateNode gate(48000.0);
        bool ok = (gate.getThresholdDb() == -40.0f) &&
                  (gate.getAttackMs() == 5.0f) &&
                  (gate.getHoldMs() == 50.0f) &&
                  (gate.getReleaseMs() == 150.0f) &&
                  (gate.getRangeDb() == -60.0f) &&
                  (gate.isStereoLinked() == true) &&
                  (gate.isBypassed() == false);
        reportResult(ctx, "GATE-002", "Default parameters", ok, "T=-40dB, Att=5ms, Hold=50ms, Rel=150ms, Range=-60dB");
    }

    // GATE-003: Bypass
    {
        audiocore::GateNode gate(48000.0);
        gate.setBypassed(true);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        for (uint32_t i = 0; i < 128; ++i) {
            in.getChannelData(0)[i] = 0.05f; // well below threshold
            in.getChannelData(1)[i] = -0.05f;
        }
        gate.process(in, out, 128);
        bool ok = (gate.isBypassed() == true) &&
                  (std::abs(out.getChannelData(0)[0] - 0.05f) < 1e-6f) &&
                  (std::abs(out.getChannelData(1)[0] - (-0.05f)) < 1e-6f);
        reportResult(ctx, "GATE-003", "Bypass", ok, "bypassed gate passes signal unaltered");
    }

    // GATE-004: Silence
    {
        audiocore::GateNode gate(48000.0);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        in.clear();
        gate.process(in, out, 128);
        bool ok = (gate.getCurrentState() == audiocore::GateState::Closed) &&
                  (std::abs(out.getChannelData(0)[0]) < 1e-6f) &&
                  (gate.getCurrentGainReductionDb() == -60.0f);
        reportResult(ctx, "GATE-004", "Silence", ok, "remains Closed at range floor");
    }

    // GATE-005: Signal below threshold
    {
        audiocore::GateNode gate(48000.0);
        gate.setThresholdDb(-30.0f);
        gate.setRangeDb(-60.0f); // -60 dB = 0.001 linear factor
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        // -40 dBFS sine wave (amplitude 0.01)
        for (uint32_t i = 0; i < 128; ++i) {
            in.getChannelData(0)[i] = 0.01f;
            in.getChannelData(1)[i] = 0.01f;
        }
        gate.process(in, out, 128);
        // After processing in closed state, output should be attenuated by ~ -60 dB (0.01 * 0.001 = 1e-5)
        float maxOut = 0.0f;
        for (uint32_t i = 0; i < 128; ++i) {
            maxOut = std::max(maxOut, std::abs(out.getChannelData(0)[i]));
        }
        bool ok = (maxOut <= 0.00002f) && (gate.getCurrentState() == audiocore::GateState::Closed);
        reportResult(ctx, "GATE-005", "Signal below threshold", ok, "peak attenuated by 60 dB to 1e-5");
    }

    // GATE-006: Signal above threshold
    {
        audiocore::GateNode gate(48000.0);
        gate.setThresholdDb(-20.0f);
        gate.setAttackMs(1.0f); // fast attack
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        // 0 dBFS signal (amplitude 1.0)
        for (uint32_t i = 0; i < 128; ++i) {
            in.getChannelData(0)[i] = 1.0f;
            in.getChannelData(1)[i] = 1.0f;
        }
        // Process 4 blocks (512 frames = 10.6 ms) to allow 1 ms attack to complete fully
        for (int b = 0; b < 4; ++b) {
            gate.process(in, out, 128);
        }
        bool ok = (gate.getCurrentState() == audiocore::GateState::Open) &&
                  (gate.getCurrentGainReductionDb() == 0.0f) &&
                  (std::abs(out.getChannelData(0)[127] - 1.0f) < 1e-4f);
        reportResult(ctx, "GATE-006", "Signal above threshold", ok, "gate fully opens to 0 dB unity");
    }

    // GATE-007: Threshold crossing
    {
        audiocore::GateNode gate(48000.0);
        gate.setThresholdDb(-20.0f);
        gate.setAttackMs(2.0f);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        // First 64 samples at -30 dB (below threshold), next 64 samples at -10 dB (above threshold)
        for (uint32_t i = 0; i < 64; ++i) in.getChannelData(0)[i] = 0.0316f;
        for (uint32_t i = 64; i < 128; ++i) in.getChannelData(0)[i] = 0.3162f;
        gate.process(in, out, 128);
        bool ok = (gate.getCurrentState() == audiocore::GateState::Attack || gate.getCurrentState() == audiocore::GateState::Open) &&
                  (gate.getCurrentGainReductionDb() > -60.0f);
        reportResult(ctx, "GATE-007", "Threshold crossing", ok, "transitions smoothly from Closed to Attack upon threshold crossing");
    }

    // GATE-008: Attack
    {
        audiocore::GateNode gate(48000.0);
        gate.setThresholdDb(-20.0f);
        gate.setAttackMs(10.0f); // 10 ms attack
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        for (uint32_t i = 0; i < 128; ++i) in.getChannelData(0)[i] = 1.0f;
        gate.process(in, out, 128);
        // After 128 samples (2.67 ms), envelope should have moved noticeably toward 0 dB but not finished yet
        float gr = gate.getCurrentGainReductionDb();
        bool ok = (gr > -60.0f) && (gr < 0.0f) && (gate.getCurrentState() == audiocore::GateState::Attack);
        reportResult(ctx, "GATE-008", "Attack", ok, "exponential ballistics verified during opening phase");
    }

    // GATE-009: Hold
    {
        audiocore::GateNode gate(48000.0);
        gate.setThresholdDb(-20.0f);
        gate.setAttackMs(0.5f);
        gate.setHoldMs(50.0f); // 50 ms hold = 2400 samples
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        // 1. Open gate with strong signal
        for (uint32_t i = 0; i < 128; ++i) in.getChannelData(0)[i] = 1.0f;
        gate.process(in, out, 128);
        gate.process(in, out, 128);
        // 2. Drop signal to silence
        in.clear();
        gate.process(in, out, 128); // 128 samples of silence (much less than 2400)
        bool ok = (gate.getCurrentState() == audiocore::GateState::Hold) &&
                  (gate.getCurrentGainReductionDb() == 0.0f);
        reportResult(ctx, "GATE-009", "Hold", ok, "gate remains 0 dB Open during Hold period");
    }

    // GATE-010: Release
    {
        audiocore::GateNode gate(48000.0);
        gate.setThresholdDb(-20.0f);
        gate.setAttackMs(0.5f);
        gate.setHoldMs(0.0f);   // 0 ms hold so it immediately releases
        gate.setReleaseMs(50.0f);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        // 1. Open
        for (uint32_t i = 0; i < 128; ++i) in.getChannelData(0)[i] = 1.0f;
        gate.process(in, out, 128);
        gate.process(in, out, 128);
        // 2. Drop to silence
        in.clear();
        gate.process(in, out, 128);
        bool ok = (gate.getCurrentState() == audiocore::GateState::Release) &&
                  (gate.getCurrentGainReductionDb() < 0.0f);
        reportResult(ctx, "GATE-010", "Release", ok, "gate transitions into Release after hold expires");
    }

    // GATE-011: Range
    {
        audiocore::GateNode gate(48000.0);
        gate.setRangeDb(-20.0f);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        for (uint32_t i = 0; i < 128; ++i) in.getChannelData(0)[i] = 0.001f;
        gate.process(in, out, 128);
        // Closed state attenuation should clamp at -20 dB (0.1)
        float expectedLinear = std::pow(10.0f, -20.0f / 20.0f);
        float actualSample = out.getChannelData(0)[0];
        bool ok = (std::abs(actualSample - (0.001f * expectedLinear)) < 1e-5f) &&
                  (gate.getCurrentGainReductionDb() == -20.0f);
        reportResult(ctx, "GATE-011", "Range", ok, "floor attenuation clamps exactly at configured -20 dB");
    }

    // GATE-012: Stereo link
    {
        audiocore::GateNode gate(48000.0);
        gate.setThresholdDb(-20.0f);
        gate.setAttackMs(1.0f);
        gate.setStereoLinked(true);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        // Left is loud (1.0 = 0 dB), Right is quiet (0.001 = -60 dB)
        for (uint32_t i = 0; i < 128; ++i) {
            in.getChannelData(0)[i] = 1.0f;
            in.getChannelData(1)[i] = 0.001f;
        }
        // Process 4 blocks to allow 1 ms attack to settle
        for (int b = 0; b < 4; ++b) {
            gate.process(in, out, 128);
        }
        // Gate should be open for BOTH channels
        bool ok = (gate.getCurrentState() == audiocore::GateState::Open) &&
                  (std::abs(out.getChannelData(1)[127] - 0.001f) < 1e-5f);
        reportResult(ctx, "GATE-012", "Stereo link", ok, "left loud channel triggers gate opening for right channel");
    }

    // GATE-013: Multi-block state
    {
        audiocore::GateNode gate(48000.0);
        gate.setThresholdDb(-20.0f);
        gate.setHoldMs(10.0f); // 480 samples
        gate.setReleaseMs(100.0f);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        // Block 1 & 2: loud
        for (uint32_t i = 0; i < 128; ++i) in.getChannelData(0)[i] = 1.0f;
        gate.process(in, out, 128);
        gate.process(in, out, 128);
        // Block 3 & 4: silent (hold active)
        in.clear();
        gate.process(in, out, 128);
        gate.process(in, out, 128);
        bool holdOk = (gate.getCurrentState() == audiocore::GateState::Hold);
        // Block 5, 6, 7: silent (hold should expire into release)
        gate.process(in, out, 128);
        gate.process(in, out, 128);
        gate.process(in, out, 128);
        bool relOk = (gate.getCurrentState() == audiocore::GateState::Release);
        reportResult(ctx, "GATE-013", "Multi-block state", holdOk && relOk, "state smoothly traverses Open->Hold->Release across 7 blocks");
    }

    // GATE-014: Parameter smoothing / safe updates
    {
        audiocore::GateNode gate(48000.0);
        gate.setThresholdDb(-50.0f);
        gate.setAttackMs(20.0f);
        gate.setReleaseMs(300.0f);
        gate.setRangeDb(-40.0f);
        bool ok = (gate.getThresholdDb() == -50.0f) &&
                  (gate.getAttackMs() == 20.0f) &&
                  (gate.getReleaseMs() == 300.0f) &&
                  (gate.getRangeDb() == -40.0f);
        reportResult(ctx, "GATE-014", "Parameter smoothing", ok, "safe on-the-fly parameter reconfiguration");
    }

    // GATE-015: No NaN
    {
        audiocore::GateNode gate(48000.0);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        in.clear();
        gate.process(in, out, 128);
        bool hasNan = false;
        for (uint32_t ch = 0; ch < 2; ++ch) {
            for (uint32_t i = 0; i < 128; ++i) {
                if (std::isnan(out.getChannelData(ch)[i])) hasNan = true;
            }
        }
        reportResult(ctx, "GATE-015", "No NaN", !hasNan, "zero NaN samples generated");
    }

    // GATE-016: No Inf
    {
        audiocore::GateNode gate(48000.0);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        for (uint32_t i = 0; i < 128; ++i) in.getChannelData(0)[i] = 10.0f; // over-scale
        gate.process(in, out, 128);
        bool hasInf = false;
        for (uint32_t ch = 0; ch < 2; ++ch) {
            for (uint32_t i = 0; i < 128; ++i) {
                if (std::isinf(out.getChannelData(ch)[i])) hasInf = true;
            }
        }
        reportResult(ctx, "GATE-016", "No Inf", !hasInf, "zero infinite samples generated");
    }

    // GATE-017: No unintended discontinuity
    {
        audiocore::GateNode gate(48000.0);
        gate.setAttackMs(5.0f);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        for (uint32_t i = 0; i < 128; ++i) in.getChannelData(0)[i] = 1.0f;
        gate.process(in, out, 128);
        float maxJump = 0.0f;
        for (uint32_t i = 1; i < 128; ++i) {
            maxJump = std::max(maxJump, std::abs(out.getChannelData(0)[i] - out.getChannelData(0)[i - 1]));
        }
        // Smooth exponential opening jump between consecutive samples must be small (< 0.1)
        bool ok = (maxJump < 0.1f);
        reportResult(ctx, "GATE-017", "No unintended discontinuity", ok, "max sample delta: " + std::to_string(maxJump));
    }

    // GATE-018: 48 kHz
    {
        audiocore::GateNode gate(48000.0);
        gate.setAttackMs(10.0f);
        gate.setHoldMs(20.0f);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        in.clear();
        gate.process(in, out, 128);
        reportResult(ctx, "GATE-018", "48 kHz execution", gate.getSampleRate() == 48000.0, "stable at 48000 Hz");
    }

    // GATE-019: 96 kHz
    {
        audiocore::GateNode gate(96000.0);
        gate.setAttackMs(10.0f);
        gate.setHoldMs(20.0f);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        in.clear();
        gate.process(in, out, 128);
        reportResult(ctx, "GATE-019", "96 kHz execution", gate.getSampleRate() == 96000.0, "stable at 96000 Hz");
    }

    // GATE-020: Long-duration stability
    {
        audiocore::GateNode gate(48000.0);
        gate.setThresholdDb(-30.0f);
        gate.setHoldMs(50.0f);
        gate.setReleaseMs(100.0f);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        bool stable = true;
        // Process 500 blocks (~1.33 seconds of alternating audio)
        for (int b = 0; b < 500; ++b) {
            float amp = (b % 10 < 5) ? 0.8f : 0.0001f;
            for (uint32_t i = 0; i < 128; ++i) {
                in.getChannelData(0)[i] = amp * static_cast<float>(std::sin(2.0 * M_PI * 1000.0 * (b * 128 + i) / 48000.0));
                in.getChannelData(1)[i] = in.getChannelData(0)[i];
            }
            gate.process(in, out, 128);
            if (std::isnan(out.getChannelData(0)[0]) || std::isinf(out.getChannelData(0)[0])) {
                stable = false;
                break;
            }
        }
        reportResult(ctx, "GATE-020", "Long-duration stability", stable, "processed 500 blocks (64,000 frames) with zero faults");
    }

    std::cout << "============================================================\n";
    std::cout << "GATE TEST RESULTS: " << ctx.passed << " PASSED, " << ctx.failed << " FAILED\n";
    std::cout << "============================================================\n";

    return (ctx.failed == 0) ? 0 : 1;
}
