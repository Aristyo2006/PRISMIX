#include <audiocore/alignment_delay_node.hpp>
#include <audiocore/audio_buffer.hpp>
#include <audiocore/audio_graph.hpp>
#include <audiocore/audio_engine.hpp>
#include <audiocore/meter_node.hpp>
#include <audiocore/mixer_node.hpp>
#include <audiocore/gain_node.hpp>
#include <audiocore/pan_node.hpp>
#include <audiocore/eq_node.hpp>
#include <audiocore/gate_node.hpp>
#include <audiocore/geq_node.hpp>
#include <audiocore/compressor_node.hpp>
#include <audiocore/delay_node.hpp>
#include <audiocore/reverb_node.hpp>
#include <audiocore/afs_node.hpp>
#include <audiocore/limiter_node.hpp>

#include <iostream>
#include <vector>
#include <cmath>
#include <chrono>
#include <cassert>
#include <iomanip>
#include <algorithm>

namespace {

int g_passCount = 0;
int g_failCount = 0;

void report(const char* testId, const char* name, bool pass, const std::string& detail = "") {
    if (pass) {
        std::cout << "  [PASS] " << testId << " - " << name;
        if (!detail.empty()) std::cout << " (" << detail << ")";
        std::cout << "\n";
        g_passCount++;
    } else {
        std::cout << "  [FAIL] " << testId << " - " << name;
        if (!detail.empty()) std::cout << " [ERROR: " << detail << "]";
        std::cout << "\n";
        g_failCount++;
    }
}

// Helper to find peak sample index in a single-channel buffer stream
int64_t findImpulsePeakIndex(audiocore::AlignmentDelayNode& node, float delayMs, size_t channel,
                             double sampleRate, uint32_t blockSize, uint32_t totalBlocks) {
    node.clearBuffers();
    node.setChannelDelayMs(channel, delayMs, true); // Snap for deterministic test

    audiocore::AudioBuffer inBuf(2, blockSize);
    audiocore::AudioBuffer outBuf(2, blockSize);

    int64_t peakIndex = -1;
    float maxVal = 0.0f;

    for (uint32_t b = 0; b < totalBlocks; ++b) {
        inBuf.clear(blockSize);
        if (b == 0) {
            inBuf.getChannelData(static_cast<uint32_t>(channel))[0] = 1.0f;
        }

        node.process(inBuf, outBuf, blockSize);

        const float* outData = outBuf.getChannelData(static_cast<uint32_t>(channel));
        for (uint32_t i = 0; i < blockSize; ++i) {
            if (std::abs(outData[i]) > maxVal) {
                maxVal = std::abs(outData[i]);
                peakIndex = static_cast<int64_t>(b * blockSize + i);
            }
        }
    }
    return peakIndex;
}

} // namespace

int main() {
    std::cout << "================================================================================\n";
    std::cout << ">>> RUNNING AUDIO CORE v1.2 OUTPUT TIME ALIGNMENT SUITE (ALIGN-001..026) <<<\n";
    std::cout << "================================================================================\n";

    // ALIGN-001 Construction
    {
        audiocore::AlignmentDelayNode node(2, 48000.0);
        bool ok = (node.getChannelCount() == 2) &&
                  (std::abs(node.getSampleRate() - 48000.0) < 1e-4) &&
                  !node.isBypassed() &&
                  !node.isStereoLinked();
        report("ALIGN-001", "Construction & Initial State", ok);
    }

    // ALIGN-002 Default state
    {
        audiocore::AlignmentDelayNode node(2, 48000.0);
        bool ok = true;
        for (size_t c = 0; c < 2; ++c) {
            if (node.getChannelDelayMs(c) != 0.0f) ok = false;
            if (node.getChannelDelaySamples(c) != 0) ok = false;
            if (!node.isChannelEnabled(c)) ok = false;
            if (node.isChannelMuted(c)) ok = false;
            if (node.isChannelPolarityInverted(c)) ok = false;
        }
        report("ALIGN-002", "Default Channel State (0 ms, unmuted, normal polarity)", ok);
    }

    // ALIGN-003 Disabled
    {
        audiocore::AlignmentDelayNode node(2, 48000.0);
        node.setChannelDelayMs(0, 50.0f, true);
        node.setChannelEnabled(0, false);

        audiocore::AudioBuffer inBuf(2, 128);
        audiocore::AudioBuffer outBuf(2, 128);
        inBuf.clear(128);
        inBuf.getChannelData(0)[0] = 1.0f;

        node.process(inBuf, outBuf, 128);
        // Should pass immediately at sample 0 when disabled
        bool ok = (std::abs(outBuf.getChannelData(0)[0] - 1.0f) < 1e-6);
        report("ALIGN-003", "Disabled State Passthrough", ok, "Disabled channel passes sample un-delayed");
    }

    // ALIGN-004 0 ms
    {
        audiocore::AlignmentDelayNode node(2, 48000.0);
        node.setChannelDelayMs(0, 0.0f, true);

        audiocore::AudioBuffer inBuf(2, 128);
        audiocore::AudioBuffer outBuf(2, 128);
        for (uint32_t i = 0; i < 128; ++i) {
            inBuf.getChannelData(0)[i] = static_cast<float>(std::sin(i * 0.1));
            inBuf.getChannelData(1)[i] = static_cast<float>(std::cos(i * 0.1));
        }

        node.process(inBuf, outBuf, 128);
        bool match = true;
        for (uint32_t i = 0; i < 128; ++i) {
            if (inBuf.getChannelData(0)[i] != outBuf.getChannelData(0)[i] ||
                inBuf.getChannelData(1)[i] != outBuf.getChannelData(1)[i]) {
                match = false;
                break;
            }
        }
        report("ALIGN-004", "0 ms Delay Bit-Exact Passthrough", match);
    }

    // ALIGN-005 1 ms (48 samples @ 48 kHz)
    {
        audiocore::AlignmentDelayNode node(2, 48000.0);
        int64_t pk = findImpulsePeakIndex(node, 1.0f, 0, 48000.0, 128, 5);
        bool ok = (pk == 48);
        report("ALIGN-005", "1 ms Delay Timing @ 48 kHz", ok, "Peak at sample 48 (expected 48)");
    }

    // ALIGN-006 10 ms (480 samples @ 48 kHz)
    {
        audiocore::AlignmentDelayNode node(2, 48000.0);
        int64_t pk = findImpulsePeakIndex(node, 10.0f, 0, 48000.0, 128, 10);
        bool ok = (pk == 480);
        report("ALIGN-006", "10 ms Delay Timing @ 48 kHz", ok, "Peak at sample 480 (expected 480)");
    }

    // ALIGN-007 50 ms (2400 samples @ 48 kHz)
    {
        audiocore::AlignmentDelayNode node(2, 48000.0);
        int64_t pk = findImpulsePeakIndex(node, 50.0f, 0, 48000.0, 128, 25);
        bool ok = (pk == 2400);
        report("ALIGN-007", "50 ms Delay Timing @ 48 kHz", ok, "Peak at sample 2400 (expected 2400)");
    }

    // ALIGN-008 80 ms (3840 samples @ 48 kHz)
    {
        audiocore::AlignmentDelayNode node(2, 48000.0);
        int64_t pk = findImpulsePeakIndex(node, 80.0f, 0, 48000.0, 128, 35);
        bool ok = (pk == 3840);
        report("ALIGN-008", "80 ms Delay Timing @ 48 kHz", ok, "Peak at sample 3840 (expected 3840)");
    }

    // ALIGN-009 100 ms (4800 samples @ 48 kHz)
    {
        audiocore::AlignmentDelayNode node(2, 48000.0);
        int64_t pk = findImpulsePeakIndex(node, 100.0f, 0, 48000.0, 128, 45);
        bool ok = (pk == 4800);
        report("ALIGN-009", "100 ms Delay Timing @ 48 kHz", ok, "Peak at sample 4800 (expected 4800)");
    }

    // ALIGN-010 500 ms maximum (24000 samples @ 48 kHz & clamping)
    {
        audiocore::AlignmentDelayNode node(2, 48000.0);
        node.setChannelDelayMs(0, 600.0f); // Should clamp to 500.0f
        bool clamped = (node.getChannelDelayMs(0) == 500.0f);
        int64_t pk = findImpulsePeakIndex(node, 500.0f, 0, 48000.0, 128, 200);
        bool ok = clamped && (pk == 24000);
        report("ALIGN-010", "500 ms Maximum Delay & Parameter Bounds Clamping", ok, "Peak at sample 24000");
    }

    // ALIGN-011 48 kHz timing validation
    {
        audiocore::AlignmentDelayNode node(2, 48000.0);
        bool p10 = (findImpulsePeakIndex(node, 10.0f, 0, 48000.0, 128, 10) == 480);
        bool p50 = (findImpulsePeakIndex(node, 50.0f, 0, 48000.0, 128, 25) == 2400);
        bool p80 = (findImpulsePeakIndex(node, 80.0f, 0, 48000.0, 128, 35) == 3840);
        bool ok = p10 && p50 && p80;
        report("ALIGN-011", "48 kHz Sample Rate Multi-Point Timing Verification", ok);
    }

    // ALIGN-012 96 kHz timing validation (80 ms = 7680 samples)
    {
        audiocore::AlignmentDelayNode node(2, 96000.0);
        int64_t pk = findImpulsePeakIndex(node, 80.0f, 0, 96000.0, 256, 35);
        bool ok = (pk == 7680);
        report("ALIGN-012", "96 kHz Sample Rate Timing Verification (80 ms = 7680 samples)", ok, "Peak at sample 7680");
    }

    // ALIGN-013 Fractional delay (23.73 ms @ 48 kHz = 1139.04 samples)
    {
        audiocore::AlignmentDelayNode node(2, 48000.0);
        node.setChannelDelayMs(0, 23.73f, true);
        double expectedSamples = 23.73 * 48.0; // 1139.04

        audiocore::AudioBuffer inBuf(2, 128);
        audiocore::AudioBuffer outBuf(2, 128);
        float peakVal = 0.0f;
        int64_t peakIdx = -1;

        for (uint32_t b = 0; b < 15; ++b) {
            inBuf.clear(128);
            if (b == 0) inBuf.getChannelData(0)[0] = 1.0f;
            node.process(inBuf, outBuf, 128);

            for (uint32_t i = 0; i < 128; ++i) {
                float val = outBuf.getChannelData(0)[i];
                if (val > peakVal) {
                    peakVal = val;
                    peakIdx = static_cast<int64_t>(b * 128 + i);
                }
            }
        }
        // Integer peak should occur at floor(1139.04) = 1139, with interpolation weight near 1.0
        bool ok = (peakIdx == 1139) && (peakVal > 0.90f);
        report("ALIGN-013", "Fractional Delay Interpolation (23.73 ms = 1139.04 samples)", ok,
               "Peak at index " + std::to_string(peakIdx) + ", amplitude=" + std::to_string(peakVal));
    }

    // ALIGN-014 Multi-block state (block sizes 64, 128, 256, 512)
    {
        bool ok = true;
        uint32_t blockSizes[4] = {64, 128, 256, 512};
        for (uint32_t bs : blockSizes) {
            audiocore::AlignmentDelayNode node(2, 48000.0);
            uint32_t blocks = (3840 / bs) + 5;
            int64_t pk = findImpulsePeakIndex(node, 80.0f, 0, 48000.0, bs, blocks);
            if (pk != 3840) {
                ok = false;
                break;
            }
        }
        report("ALIGN-014", "Multi-Block State Preservation (Blocks 64, 128, 256, 512)", ok,
               "Impulse crosses arbitrary block boundaries with 0 jitter");
    }

    // ALIGN-015 L/R independence (Main L = 80 ms, Main R = 0 ms)
    {
        audiocore::AlignmentDelayNode node(2, 48000.0);
        node.setChannelDelayMs(0, 80.0f, true);
        node.setChannelDelayMs(1, 0.0f, true);

        audiocore::AudioBuffer inBuf(2, 128);
        audiocore::AudioBuffer outBuf(2, 128);
        inBuf.clear(128);
        inBuf.getChannelData(0)[0] = 1.0f; // Left impulse
        inBuf.getChannelData(1)[0] = 1.0f; // Right impulse

        node.process(inBuf, outBuf, 128);

        // Block 0: R must emerge at sample 0, L must still be silent (waiting for 3840 samples)
        bool rImmediate = (std::abs(outBuf.getChannelData(1)[0] - 1.0f) < 1e-6);
        bool lSilent = (std::abs(outBuf.getChannelData(0)[0]) < 1e-6);

        // Process blocks 1..30 (sample 3840 is in block 30, index 0)
        for (uint32_t b = 1; b <= 30; ++b) {
            inBuf.clear(128);
            node.process(inBuf, outBuf, 128);
        }
        bool lDelayed = (std::abs(outBuf.getChannelData(0)[0] - 1.0f) < 1e-6); // Block 30, index 0 = sample 3840
        bool ok = rImmediate && lSilent && lDelayed;
        report("ALIGN-015", "Stereo Channel Independence (L=80ms, R=0ms, zero crosstalk)", ok);
    }

    // ALIGN-016 L/R linking
    {
        audiocore::AlignmentDelayNode node(2, 48000.0);
        node.setStereoLink(true);
        node.setChannelDelayMs(0, 45.0f);
        bool linked = (node.getChannelDelayMs(1) == 45.0f);

        node.setStereoLink(false);
        node.setChannelDelayMs(0, 20.0f);
        bool unlinked = (node.getChannelDelayMs(1) == 45.0f);
        bool ok = linked && unlinked;
        report("ALIGN-016", "Stereo Linking Parameter Sync", ok);
    }

    // ALIGN-017 Unity gain
    {
        audiocore::AlignmentDelayNode node(2, 48000.0);
        node.setChannelDelayMs(0, 80.0f, true);

        audiocore::AudioBuffer inBuf(2, 128);
        audiocore::AudioBuffer outBuf(2, 128);

        double inSumSq = 0.0;
        double outSumSq = 0.0;

        // Run continuous 1 kHz sine for 60 blocks (well past 30 blocks delay)
        for (uint32_t b = 0; b < 60; ++b) {
            for (uint32_t i = 0; i < 128; ++i) {
                float s = static_cast<float>(std::sin(2.0 * 3.1415926535 * 1000.0 * (b * 128 + i) / 48000.0) * 0.75);
                inBuf.getChannelData(0)[i] = s;
                inBuf.getChannelData(1)[i] = s;
            }
            node.process(inBuf, outBuf, 128);

            // Accumulate steady-state energy in last 20 blocks
            if (b >= 40) {
                for (uint32_t i = 0; i < 128; ++i) {
                    inSumSq += inBuf.getChannelData(0)[i] * inBuf.getChannelData(0)[i];
                    outSumSq += outBuf.getChannelData(0)[i] * outBuf.getChannelData(0)[i];
                }
            }
        }
        double rmsIn = std::sqrt(inSumSq);
        double rmsOut = std::sqrt(outSumSq);
        bool ok = (std::abs(rmsIn - rmsOut) / rmsIn < 0.001); // Within 0.1% unity gain
        report("ALIGN-017", "Unity Gain Verification (Zero level coloration)", ok);
    }

    // ALIGN-018 Polarity normal
    {
        audiocore::AlignmentDelayNode node(2, 48000.0);
        node.setChannelPolarity(0, false);
        node.setChannelDelayMs(0, 1.0f, true); // 48 samples

        audiocore::AudioBuffer inBuf(2, 128);
        audiocore::AudioBuffer outBuf(2, 128);
        inBuf.clear(128);
        inBuf.getChannelData(0)[0] = 0.85f;

        node.process(inBuf, outBuf, 128);
        bool ok = (std::abs(outBuf.getChannelData(0)[48] - 0.85f) < 1e-5);
        report("ALIGN-018", "Normal Polarity Response (+1.0 multiplier)", ok);
    }

    // ALIGN-019 Polarity inverted
    {
        audiocore::AlignmentDelayNode node(2, 48000.0);
        node.setChannelPolarity(0, true);
        node.setChannelDelayMs(0, 1.0f, true); // 48 samples

        audiocore::AudioBuffer inBuf(2, 128);
        audiocore::AudioBuffer outBuf(2, 128);
        inBuf.clear(128);
        inBuf.getChannelData(0)[0] = 0.85f;

        node.process(inBuf, outBuf, 128);
        bool ok = (std::abs(outBuf.getChannelData(0)[48] - (-0.85f)) < 1e-5);
        report("ALIGN-019", "Inverted Polarity Response (-1.0 multiplier)", ok);
    }

    // ALIGN-020 Mute
    {
        audiocore::AlignmentDelayNode node(2, 48000.0);
        node.setChannelDelayMs(0, 2.0f, true); // 96 samples
        node.setChannelMute(0, true);

        audiocore::AudioBuffer inBuf(2, 128);
        audiocore::AudioBuffer outBuf(2, 128);
        inBuf.clear(128);
        inBuf.getChannelData(0)[0] = 1.0f;

        node.process(inBuf, outBuf, 128);
        // While muted, output must be strictly 0.0
        bool zeroWhileMuted = true;
        for (uint32_t i = 0; i < 128; ++i) {
            if (outBuf.getChannelData(0)[i] != 0.0f) {
                zeroWhileMuted = false;
                break;
            }
        }

        // Unmute and send another pulse at sample 0, should emerge at sample 96
        node.setChannelMute(0, false);
        inBuf.clear(128);
        inBuf.getChannelData(0)[0] = 0.9f;
        node.process(inBuf, outBuf, 128);
        bool ok = zeroWhileMuted && (std::abs(outBuf.getChannelData(0)[96] - 0.9f) < 1e-5);
        report("ALIGN-020", "Output Mute & Seamless Buffer State Continuity", ok);
    }

    // ALIGN-021 Parameter automation
    {
        audiocore::AlignmentDelayNode node(2, 48000.0);
        audiocore::AudioBuffer inBuf(2, 128);
        audiocore::AudioBuffer outBuf(2, 128);

        float maxDelta = 0.0f;
        bool hasDiscontinuity = false;

        // Gradually automate delay from 10 ms to 100 ms while audio is flowing
        for (uint32_t b = 0; b < 100; ++b) {
            float targetMs = 10.0f + b * 0.9f;
            node.setChannelDelayMs(0, targetMs, false); // smooth slew

            for (uint32_t i = 0; i < 128; ++i) {
                inBuf.getChannelData(0)[i] = static_cast<float>(std::sin(i * 0.2) * 0.5);
                inBuf.getChannelData(1)[i] = static_cast<float>(std::sin(i * 0.2) * 0.5);
            }
            node.process(inBuf, outBuf, 128);

            for (uint32_t i = 1; i < 128; ++i) {
                float d = std::abs(outBuf.getChannelData(0)[i] - outBuf.getChannelData(0)[i - 1]);
                if (d > maxDelta) maxDelta = d;
                if (d > 0.40f) hasDiscontinuity = true; // No pop or cliff
            }
        }
        bool ok = !hasDiscontinuity;
        report("ALIGN-021", "Live Parameter Automation & Anti-Zipper Slewing", ok,
               "Max inter-sample slew delta=" + std::to_string(maxDelta));
    }

    // ALIGN-022 NaN detection
    {
        audiocore::AlignmentDelayNode node(2, 48000.0);
        audiocore::AudioBuffer inBuf(2, 128);
        audiocore::AudioBuffer outBuf(2, 128);
        node.setChannelDelayMs(0, 50.0f, true);

        bool nanFound = false;
        for (uint32_t b = 0; b < 50; ++b) {
            for (uint32_t i = 0; i < 128; ++i) {
                inBuf.getChannelData(0)[i] = static_cast<float>(std::sin(i * 0.5));
                inBuf.getChannelData(1)[i] = static_cast<float>(std::cos(i * 0.5));
            }
            node.process(inBuf, outBuf, 128);
            for (uint32_t i = 0; i < 128; ++i) {
                if (std::isnan(outBuf.getChannelData(0)[i]) || std::isnan(outBuf.getChannelData(1)[i])) {
                    nanFound = true;
                    break;
                }
            }
        }
        report("ALIGN-022", "Numerical Integrity: Zero NaN Samples", !nanFound);
    }

    // ALIGN-023 Inf detection
    {
        audiocore::AlignmentDelayNode node(2, 48000.0);
        audiocore::AudioBuffer inBuf(2, 128);
        audiocore::AudioBuffer outBuf(2, 128);
        node.setChannelDelayMs(0, 50.0f, true);

        bool infFound = false;
        for (uint32_t b = 0; b < 50; ++b) {
            for (uint32_t i = 0; i < 128; ++i) {
                inBuf.getChannelData(0)[i] = 1.0f;
                inBuf.getChannelData(1)[i] = -1.0f;
            }
            node.process(inBuf, outBuf, 128);
            for (uint32_t i = 0; i < 128; ++i) {
                if (std::isinf(outBuf.getChannelData(0)[i]) || std::isinf(outBuf.getChannelData(1)[i])) {
                    infFound = true;
                    break;
                }
            }
        }
        report("ALIGN-023", "Numerical Integrity: Zero Inf Samples", !infFound);
    }

    // ALIGN-024 Long-duration stability
    {
        audiocore::AlignmentDelayNode node(2, 48000.0);
        node.setChannelDelayMs(0, 120.0f, true);
        node.setChannelDelayMs(1, 80.0f, true);

        audiocore::AudioBuffer inBuf(2, 128);
        audiocore::AudioBuffer outBuf(2, 128);

        bool stable = true;
        // Process 1000 blocks (~128,000 samples, ~2.67s of streaming)
        for (uint32_t b = 0; b < 1000; ++b) {
            for (uint32_t i = 0; i < 128; ++i) {
                inBuf.getChannelData(0)[i] = static_cast<float>(std::sin((b * 128 + i) * 0.05) * 0.8);
                inBuf.getChannelData(1)[i] = static_cast<float>(std::cos((b * 128 + i) * 0.05) * 0.8);
            }
            node.process(inBuf, outBuf, 128);

            for (uint32_t i = 0; i < 128; ++i) {
                float v0 = std::abs(outBuf.getChannelData(0)[i]);
                float v1 = std::abs(outBuf.getChannelData(1)[i]);
                if (v0 > 1.05f || v1 > 1.05f || std::isnan(v0) || std::isnan(v1)) {
                    stable = false;
                    break;
                }
            }
            if (!stable) break;
        }
        report("ALIGN-024", "Long-Duration Circular Buffer Ring Stability", stable, "128,000 frames evaluated");
    }

    // ALIGN-025 Performance
    {
        audiocore::AlignmentDelayNode node(2, 48000.0);
        node.setChannelDelayMs(0, 80.0f, true);
        node.setChannelDelayMs(1, 40.0f, true);

        audiocore::AudioBuffer inBuf(2, 128);
        audiocore::AudioBuffer outBuf(2, 128);
        for (uint32_t i = 0; i < 128; ++i) {
            inBuf.getChannelData(0)[i] = 0.5f;
            inBuf.getChannelData(1)[i] = -0.5f;
        }

        const uint32_t N_ITER = 2000;
        auto t0 = std::chrono::high_resolution_clock::now();
        for (uint32_t iter = 0; iter < N_ITER; ++iter) {
            node.process(inBuf, outBuf, 128);
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        double totalUs = std::chrono::duration<double, std::micro>(t1 - t0).count();
        double avgUsPerBlock = totalUs / N_ITER;
        double budgetDeadlineUs = (128.0 / 48000.0) * 1000000.0; // 2666.7 us
        double cpuPercent = (avgUsPerBlock / budgetDeadlineUs) * 100.0;

        bool performant = (avgUsPerBlock < 15.0); // Ultra lightweight (< 15 us / block)
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << avgUsPerBlock << " us / block (" << cpuPercent << "% budget)";
        report("ALIGN-025", "Realtime CPU Efficiency & Speedup Factor", performant, ss.str());
    }

    // ALIGN-026 Full DSP integration (13-node pipeline)
    {
        audiocore::AudioEngine engine;
        engine.configure(48000.0, 128, 2);
        auto& graph = engine.getGraph();
        graph.clearNodes();

        auto inMeter = std::make_shared<audiocore::MeterNode>();
        auto mixer = std::make_shared<audiocore::MixerNode>(2);
        auto gain = std::make_shared<audiocore::GainNode>(0.0f);
        auto pan = std::make_shared<audiocore::PanNode>(0.0f, 48000.0);
        auto peq = std::make_shared<audiocore::ParametricEQNode>(4, 48000.0);
        auto gate = std::make_shared<audiocore::GateNode>(48000.0);
        auto geq = std::make_shared<audiocore::GraphicEQNode>(48000.0);
        auto comp = std::make_shared<audiocore::CompressorNode>(48000.0);
        auto delay = std::make_shared<audiocore::DelayNode>(48000.0);
        auto reverb = std::make_shared<audiocore::ReverbNode>(48000.0);
        auto afs = std::make_shared<audiocore::AfsNode>(48000.0);
        auto limiter = std::make_shared<audiocore::LimiterNode>(-1.0f, 48000.0);
        auto alignDelay = std::make_shared<audiocore::AlignmentDelayNode>(2, 48000.0);
        auto outMeter = std::make_shared<audiocore::MeterNode>();

        graph.addNode(inMeter);
        graph.addNode(mixer);
        graph.addNode(gain);
        graph.addNode(pan);
        graph.addNode(peq);
        graph.addNode(gate);
        graph.addNode(geq);
        graph.addNode(comp);
        graph.addNode(delay);
        graph.addNode(reverb);
        graph.addNode(afs);
        graph.addNode(limiter);
        graph.addNode(alignDelay); // Output Alignment Delay placed right before Output Meter!
        graph.addNode(outMeter);

        alignDelay->setChannelDelayMs(0, 80.0f, true);
        alignDelay->setChannelDelayMs(1, 0.0f, true);

        audiocore::AudioBuffer inBuf(2, 128);
        audiocore::AudioBuffer outBuf(2, 128);
        for (uint32_t i = 0; i < 128; ++i) {
            inBuf.getChannelData(0)[i] = static_cast<float>(std::sin(i * 0.1) * 0.5);
            inBuf.getChannelData(1)[i] = static_cast<float>(std::sin(i * 0.1) * 0.5);
        }

        bool ok = true;
        for (uint32_t b = 0; b < 20; ++b) {
            graph.process(inBuf, outBuf, 128);
            for (uint32_t ch = 0; ch < 2; ++ch) {
                for (uint32_t i = 0; i < 128; ++i) {
                    float s = outBuf.getChannelData(ch)[i];
                    if (std::isnan(s) || std::isinf(s)) {
                        ok = false;
                        break;
                    }
                }
            }
        }
        report("ALIGN-026", "Complete 14-Node Full DSP Pipeline Integration", ok,
               "Channel -> DSP -> Mix -> Limiter -> Output Alignment -> Output Meter");
    }

    std::cout << "================================================================================\n";
    std::cout << "ALIGNMENT SUITE RESULT: " << (g_failCount == 0 ? "PASS" : "FAIL")
              << " (" << g_passCount << " passed, " << g_failCount << " failed)\n";
    std::cout << "================================================================================\n";

    return (g_failCount == 0) ? 0 : 1;
}
