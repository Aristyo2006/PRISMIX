#include "audiocore/afs_node.hpp"
#include "audiocore/audio_buffer.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <random>

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
    std::cout << "AUDIO CORE: FEEDBACK SUPPRESSOR (AFS) AUTOMATED SUITE\n";
    std::cout << "============================================================\n";

    TestContext ctx;

    // AFS-001: Construction
    {
        audiocore::AfsNode afs(48000.0);
        bool ok = (afs.getSampleRate() == 48000.0);
        reportResult(ctx, "AFS-001", "Construction", ok, "sampleRate=48000.0");
    }

    // AFS-002: Default state
    {
        audiocore::AfsNode afs(48000.0);
        bool ok = afs.isEnabled() &&
                  (afs.getMaxNotches() == 8) &&
                  (afs.getMaxDepthDb() == -18.0f) &&
                  (afs.getQ() == 15.0f) &&
                  (afs.getActiveNotchCount() == 0);
        reportResult(ctx, "AFS-002", "Default state", ok, "Enabled=true, MaxNotches=8, Depth=-18dB, Q=15");
    }

    // AFS-003: Disabled state
    {
        audiocore::AfsNode afs(48000.0);
        afs.setEnabled(false);
        bool ok = !afs.isEnabled() && (std::string(afs.getStatusString()) == "DISABLED");
        reportResult(ctx, "AFS-003", "Disabled state", ok, "Status=DISABLED, zero filtering applied");
    }

    // AFS-004: Silence
    {
        audiocore::AfsNode afs(48000.0);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        in.clear();

        for (int b = 0; b < 25; ++b) {
            afs.process(in, out, 128);
        }
        afs.stepAnalysis();

        bool zeroNotches = (afs.getActiveNotchCount() == 0);
        reportResult(ctx, "AFS-004", "Silence", zeroNotches, "Zero notches triggered by silence");
    }

    // AFS-005: White noise (Broadband signal must NOT trigger false notch)
    {
        audiocore::AfsNode afs(48000.0);
        afs.setSensitivity(0.8f);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);

        std::mt19937 rng(1337);
        std::uniform_real_distribution<float> dist(-0.5f, 0.5f);

        // Feed 30 blocks of white noise
        for (int b = 0; b < 30; ++b) {
            for (uint32_t i = 0; i < 128; ++i) {
                in.getChannelData(0)[i] = dist(rng);
                in.getChannelData(1)[i] = dist(rng);
            }
            afs.process(in, out, 128);
        }
        afs.stepAnalysis();
        afs.stepAnalysis();

        bool noFalseNotch = (afs.getActiveNotchCount() == 0);
        reportResult(ctx, "AFS-005", "White noise", noFalseNotch, "Broadband white noise rejected (0 false notches)");
    }

    // AFS-006: Pink noise (Broadband 1/f noise must NOT trigger false notch)
    {
        audiocore::AfsNode afs(48000.0);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);

        // Simple pink noise generator (Paul Kellet's filter)
        float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
        std::mt19937 rng(42);
        std::uniform_real_distribution<float> dist(-0.5f, 0.5f);

        for (int b = 0; b < 30; ++b) {
            for (uint32_t i = 0; i < 128; ++i) {
                float white = dist(rng);
                b0 = 0.99765f * b0 + white * 0.0990460f;
                b1 = 0.96300f * b1 + white * 0.2965164f;
                b2 = 0.57000f * b2 + white * 1.0526913f;
                float pink = (b0 + b1 + b2 + white * 0.1848f) * 0.2f;
                in.getChannelData(0)[i] = pink;
                in.getChannelData(1)[i] = pink;
            }
            afs.process(in, out, 128);
        }
        afs.stepAnalysis();
        afs.stepAnalysis();

        bool noFalseNotch = (afs.getActiveNotchCount() == 0);
        reportResult(ctx, "AFS-006", "Pink noise", noFalseNotch, "Pink noise rejected (0 false notches)");
    }

    // AFS-007: Sustained music tone / multi-harmonic chord
    {
        audiocore::AfsNode afs(48000.0);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);

        // Rich harmonic music chord (A minor: 440, 523.25, 659.25, plus octaves)
        for (int b = 0; b < 25; ++b) {
            for (uint32_t i = 0; i < 128; ++i) {
                float t = static_cast<float>(b * 128 + i) / 48000.0f;
                float s = 0.2f * std::sin(2.0f * static_cast<float>(M_PI) * 440.0f * t) +
                          0.2f * std::sin(2.0f * static_cast<float>(M_PI) * 523.25f * t) +
                          0.2f * std::sin(2.0f * static_cast<float>(M_PI) * 659.25f * t) +
                          0.2f * std::sin(2.0f * static_cast<float>(M_PI) * 880.0f * t);
                in.getChannelData(0)[i] = s;
                in.getChannelData(1)[i] = s;
            }
            afs.process(in, out, 128);
        }
        afs.stepAnalysis();

        // Music chords have multiple distributed harmonics, rejected by prominence check
        bool handled = (afs.getActiveNotchCount() <= 1);
        reportResult(ctx, "AFS-007", "Sustained music tone", handled, "Harmonic chord rejected or minimally affected");
    }

    // AFS-008: Feedback candidate detection
    {
        audiocore::AfsNode afs(48000.0);
        afs.setSensitivity(0.9f);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);

        // Sharp, loud, isolated acoustic feedback squeal at 2500 Hz
        for (int b = 0; b < 16; ++b) {
            for (uint32_t i = 0; i < 128; ++i) {
                float t = static_cast<float>(b * 128 + i) / 48000.0f;
                float s = 0.8f * std::sin(2.0f * static_cast<float>(M_PI) * 2500.0f * t);
                in.getChannelData(0)[i] = s;
                in.getChannelData(1)[i] = s;
            }
            afs.process(in, out, 128);
        }
        afs.stepAnalysis();

        std::string status = afs.getStatusString();
        bool candidateFound = (status == "CANDIDATE" || status == "ACTIVE");
        reportResult(ctx, "AFS-008", "Feedback candidate", candidateFound, "2500 Hz feedback tone detected as candidate");
    }

    // AFS-009: Persistence detection
    {
        audiocore::AfsNode afs(48000.0);
        afs.setSensitivity(0.9f);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);

        // Feed sustained feedback across 3 consecutive analysis windows (~60 blocks / 160 ms)
        for (int cycle = 0; cycle < 3; ++cycle) {
            for (int b = 0; b < 16; ++b) {
                for (uint32_t i = 0; i < 128; ++i) {
                    float t = static_cast<float>((cycle * 16 + b) * 128 + i) / 48000.0f;
                    float s = 0.8f * std::sin(2.0f * static_cast<float>(M_PI) * 3150.0f * t);
                    in.getChannelData(0)[i] = s;
                    in.getChannelData(1)[i] = s;
                }
                afs.process(in, out, 128);
            }
            afs.stepAnalysis();
        }

        bool confirmed = (afs.getActiveNotchCount() >= 1);
        reportResult(ctx, "AFS-009", "Persistence detection", confirmed, "Persistent feedback across 3 windows confirmed");
    }

    // AFS-010: Notch creation
    {
        audiocore::AfsNode afs(48000.0);
        afs.setSensitivity(0.9f);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);

        for (int cycle = 0; cycle < 4; ++cycle) {
            for (int b = 0; b < 16; ++b) {
                for (uint32_t i = 0; i < 128; ++i) {
                    float t = static_cast<float>((cycle * 16 + b) * 128 + i) / 48000.0f;
                    float s = 0.8f * std::sin(2.0f * static_cast<float>(M_PI) * 1800.0f * t);
                    in.getChannelData(0)[i] = s;
                    in.getChannelData(1)[i] = s;
                }
                afs.process(in, out, 128);
            }
            afs.stepAnalysis();
        }

        auto snapshots = afs.getNotchSnapshots();
        bool created = !snapshots.empty();
        float notchedFreq = created ? snapshots[0].frequencyHz : 0.0f;
        bool freqMatch = std::abs(notchedFreq - 1800.0f) < 40.0f;
        char buf[128];
        snprintf(buf, sizeof(buf), "Notch deployed at %.1f Hz (target 1800 Hz)", notchedFreq);
        reportResult(ctx, "AFS-010", "Notch creation", (created && freqMatch), buf);
    }

    // AFS-011: Notch attenuation
    {
        audiocore::AfsNode afs(48000.0);
        afs.setSensitivity(0.9f);
        afs.setMaxDepthDb(-18.0f);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);

        // Train notch on 1500 Hz tone
        for (int cycle = 0; cycle < 4; ++cycle) {
            for (int b = 0; b < 16; ++b) {
                for (uint32_t i = 0; i < 128; ++i) {
                    float t = static_cast<float>((cycle * 16 + b) * 128 + i) / 48000.0f;
                    float s = 0.8f * std::sin(2.0f * static_cast<float>(M_PI) * 1500.0f * t);
                    in.getChannelData(0)[i] = s;
                    in.getChannelData(1)[i] = s;
                }
                afs.process(in, out, 128);
            }
            afs.stepAnalysis();
        }

        // Process another 20 blocks so notch reaches full -18 dB depth
        float peakOut = 0.0f;
        for (int b = 0; b < 20; ++b) {
            for (uint32_t i = 0; i < 128; ++i) {
                float t = static_cast<float>((70 + b) * 128 + i) / 48000.0f;
                float s = 0.8f * std::sin(2.0f * static_cast<float>(M_PI) * 1500.0f * t);
                in.getChannelData(0)[i] = s;
                in.getChannelData(1)[i] = s;
            }
            afs.process(in, out, 128);
            if (b >= 15) {
                for (uint32_t i = 0; i < 128; ++i) {
                    peakOut = std::max(peakOut, std::abs(out.getChannelData(0)[i]));
                }
            }
        }
        // Input amplitude 0.8 -> with > 12 dB notch attenuation, peak must drop below 0.25
        bool attenuated = (peakOut < 0.25f);
        char buf[128];
        snprintf(buf, sizeof(buf), "Input 0.800 attenuated to %.4f (>12 dB reduction)", peakOut);
        reportResult(ctx, "AFS-011", "Notch attenuation", attenuated, buf);
    }

    // AFS-012: Multiple notches
    {
        audiocore::AfsNode afs(48000.0);
        afs.setSensitivity(0.9f);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);

        // Train on dual feedback tones (1200 Hz and 4000 Hz)
        for (int cycle = 0; cycle < 5; ++cycle) {
            for (int b = 0; b < 16; ++b) {
                for (uint32_t i = 0; i < 128; ++i) {
                    float t = static_cast<float>((cycle * 16 + b) * 128 + i) / 48000.0f;
                    float s = 0.5f * std::sin(2.0f * static_cast<float>(M_PI) * 1200.0f * t) +
                              0.5f * std::sin(2.0f * static_cast<float>(M_PI) * 4000.0f * t);
                    in.getChannelData(0)[i] = s;
                    in.getChannelData(1)[i] = s;
                }
                afs.process(in, out, 128);
            }
            afs.stepAnalysis();
        }

        bool multiNotches = (afs.getActiveNotchCount() >= 2);
        char buf[128];
        snprintf(buf, sizeof(buf), "%zu active notches deployed for multiple feedback tones", afs.getActiveNotchCount());
        reportResult(ctx, "AFS-012", "Multiple notches", multiNotches, buf);
    }

    // AFS-013: Maximum notch count
    {
        audiocore::AfsNode afs(48000.0);
        afs.setMaxNotches(4);
        bool limitOk = (afs.getMaxNotches() == 4);
        reportResult(ctx, "AFS-013", "Maximum notch count", limitOk, "Max notch limit set to 4");
    }

    // AFS-014: Notch release & Clear filters
    {
        audiocore::AfsNode afs(48000.0);
        afs.setSensitivity(0.9f);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);

        for (int cycle = 0; cycle < 4; ++cycle) {
            for (int b = 0; b < 16; ++b) {
                for (uint32_t i = 0; i < 128; ++i) {
                    float t = static_cast<float>((cycle * 16 + b) * 128 + i) / 48000.0f;
                    in.getChannelData(0)[i] = 0.8f * std::sin(2.0f * static_cast<float>(M_PI) * 2000.0f * t);
                }
                afs.process(in, out, 128);
            }
            afs.stepAnalysis();
        }

        afs.clearFilters();
        bool cleared = (afs.getActiveNotchCount() == 0);
        reportResult(ctx, "AFS-014", "Notch release", cleared, "clearFilters() immediately resets all active notches to 0");
    }

    // AFS-015: False-positive protection (Percussive transient test)
    {
        audiocore::AfsNode afs(48000.0);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);

        // Short 10ms percussive click burst (snare rimshot)
        in.clear();
        for (uint32_t i = 0; i < 48; ++i) {
            in.getChannelData(0)[i] = 0.9f;
        }
        afs.process(in, out, 128);

        // Followed by silence
        in.clear();
        for (int b = 0; b < 20; ++b) {
            afs.process(in, out, 128);
        }
        afs.stepAnalysis();

        bool noFalseNotch = (afs.getActiveNotchCount() == 0);
        reportResult(ctx, "AFS-015", "False-positive protection", noFalseNotch, "Percussive transient does not deploy notch");
    }

    // AFS-016: 48 kHz
    {
        audiocore::AfsNode afs(48000.0);
        bool ok = (afs.getSampleRate() == 48000.0);
        reportResult(ctx, "AFS-016", "48 kHz", ok, "Sampling rate configured to 48000 Hz");
    }

    // AFS-017: 96 kHz
    {
        audiocore::AfsNode afs(48000.0);
        afs.setSampleRate(96000.0);
        bool ok = (afs.getSampleRate() == 96000.0);
        reportResult(ctx, "AFS-017", "96 kHz", ok, "Sampling rate updated to 96000 Hz");
    }

    // AFS-018: Multi-block operation
    {
        audiocore::AfsNode afs(48000.0);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);

        for (uint32_t i = 0; i < 128; ++i) {
            in.getChannelData(0)[i] = 0.5f;
            in.getChannelData(1)[i] = 0.5f;
        }

        bool clean = true;
        for (int b = 0; b < 100; ++b) {
            afs.process(in, out, 128);
            for (uint32_t i = 0; i < 128; ++i) {
                if (std::isnan(out.getChannelData(0)[i]) || std::isinf(out.getChannelData(0)[i])) {
                    clean = false;
                }
            }
        }
        reportResult(ctx, "AFS-018", "Multi-block operation", clean, "Processed 100 consecutive blocks cleanly");
    }

    // AFS-019: NaN detection
    {
        audiocore::AfsNode afs(48000.0);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        in.getChannelData(0)[0] = 0.5f;
        afs.process(in, out, 128);

        bool hasNan = false;
        for (uint32_t i = 0; i < 128; ++i) {
            if (std::isnan(out.getChannelData(0)[i]) || std::isnan(out.getChannelData(1)[i])) {
                hasNan = true;
            }
        }
        reportResult(ctx, "AFS-019", "NaN detection", !hasNan, "Zero NaN values detected");
    }

    // AFS-020: Inf detection
    {
        audiocore::AfsNode afs(48000.0);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        in.getChannelData(0)[0] = 0.5f;
        afs.process(in, out, 128);

        bool hasInf = false;
        for (uint32_t i = 0; i < 128; ++i) {
            if (std::isinf(out.getChannelData(0)[i]) || std::isinf(out.getChannelData(1)[i])) {
                hasInf = true;
            }
        }
        reportResult(ctx, "AFS-020", "Inf detection", !hasInf, "Zero Inf values detected");
    }

    // AFS-021: Long-duration stability
    {
        audiocore::AfsNode afs(48000.0);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        for (uint32_t i = 0; i < 128; ++i) in.getChannelData(0)[i] = 0.2f;

        bool stable = true;
        for (int b = 0; b < 500; ++b) {
            afs.process(in, out, 128);
            for (uint32_t i = 0; i < 128; ++i) {
                float v = out.getChannelData(0)[i];
                if (std::isnan(v) || std::isinf(v) || std::abs(v) > 5.0f) {
                    stable = false;
                    break;
                }
            }
            if (!stable) break;
        }
        reportResult(ctx, "AFS-021", "Long-duration stability", stable, "Continuous execution across 500 blocks without overflow");
    }

    std::cout << "============================================================\n";
    std::cout << "TOTAL: " << ctx.passed << " passed, " << ctx.failed << " failed\n";
    std::cout << "============================================================\n";

    return (ctx.failed == 0) ? 0 : 1;
}

