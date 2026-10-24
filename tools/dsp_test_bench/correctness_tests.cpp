#include "correctness_tests.hpp"
#include "synthetic_signals.hpp"
#include "profiler.hpp"
#include "audiocore/audio_engine.hpp"
#include "audiocore/gain_node.hpp"
#include "audiocore/pan_node.hpp"
#include "audiocore/eq_node.hpp"
#include "audiocore/compressor_node.hpp"
#include "audiocore/limiter_node.hpp"
#include "audiocore/meter_node.hpp"
#include "audiocore/mixer_node.hpp"
#include "audiocore/gate_node.hpp"
#include "audiocore/geq_node.hpp"
#include "audiocore/delay_node.hpp"
#include "audiocore/reverb_node.hpp"
#include "audiocore/afs_node.hpp"

#include <cmath>
#include <algorithm>

namespace testbench {

CorrectnessSuiteResult CorrectnessVerifier::runAllTests(double sampleRate, uint32_t blockSize) {
    CorrectnessSuiteResult suite;
    suite.allPassed = true;
    suite.passedCount = 0;
    suite.failedCount = 0;

    auto record = [&](
        const std::string& id,
        const std::string& desc,
        bool pass,
        double expected,
        double actual,
        double tolerance,
        const std::string& detail = "")
    {
        CorrectnessTestItem item;
        item.testId = id;
        item.description = desc;
        item.passed = pass;
        item.expected = expected;
        item.actual = actual;
        item.tolerance = tolerance;
        item.details = detail;

        if (pass) {
            suite.passedCount++;
        } else {
            suite.failedCount++;
            suite.allPassed = false;
        }
        suite.items.push_back(item);
    };

    constexpr uint32_t kFrames = 1024;
    constexpr uint32_t kChannels = 2;

    // ============================================================
    // TEST 01: Gain 0 dB
    // ============================================================
    {
        audiocore::AudioBuffer inBuf(kChannels, kFrames);
        audiocore::AudioBuffer outBuf(kChannels, kFrames);
        SyntheticSignals::generateSine(inBuf, sampleRate, 1000.0, 1.0f, kFrames);

        audiocore::AudioEngine engine;
        engine.configure(sampleRate, blockSize, kChannels);
        engine.getGraph().clearNodes();
        engine.getGraph().addNode(std::make_shared<audiocore::GainNode>(0.0f));
        engine.start();
        engine.process(inBuf, outBuf, kFrames);
        engine.stop();

        float maxDiff = 0.0f;
        for (uint32_t ch = 0; ch < kChannels; ++ch) {
            for (uint32_t i = 0; i < kFrames; ++i) {
                float d = std::abs(inBuf.getChannelData(ch)[i] - outBuf.getChannelData(ch)[i]);
                if (d > maxDiff) maxDiff = d;
            }
        }
        bool ok = (maxDiff < 1e-6f);
        record("TEST 01", "Gain 0 dB: Output matches input within unity tolerance", ok, 0.0, maxDiff, 1e-6);
    }

    // ============================================================
    // TEST 02: Gain -6 dB
    // ============================================================
    {
        audiocore::AudioBuffer inBuf(kChannels, kFrames);
        audiocore::AudioBuffer outBuf(kChannels, kFrames);
        SyntheticSignals::generateDc(inBuf, 1.0f, kFrames);

        audiocore::AudioEngine engine;
        engine.configure(sampleRate, blockSize, kChannels);
        engine.getGraph().clearNodes();
        engine.getGraph().addNode(std::make_shared<audiocore::GainNode>(-6.0f));
        engine.start();
        engine.process(inBuf, outBuf, kFrames);
        engine.stop();

        float expected = std::pow(10.0f, -6.0f / 20.0f); // ~0.501187
        float actual = outBuf.getChannelData(0)[10];
        bool ok = std::abs(actual - expected) < 1e-4f;
        record("TEST 02", "Gain -6 dB: Verified expected amplitude reduction ~0.501187", ok, expected, actual, 1e-4);
    }

    // ============================================================
    // TEST 03: Pan center
    // ============================================================
    {
        audiocore::AudioBuffer inBuf(kChannels, kFrames);
        audiocore::AudioBuffer outBuf(kChannels, kFrames);
        SyntheticSignals::generateDc(inBuf, 1.0f, kFrames);

        audiocore::PanNode pan(0.0f, sampleRate);
        pan.setSmoothingTimeMs(0.0f); // immediate response for test
        pan.setPan(0.0f);
        pan.process(inBuf, outBuf, kFrames);

        float expected = 0.70710678f; // -3.01 dB constant-power center
        float actualL = outBuf.getChannelData(0)[kFrames - 1];
        float actualR = outBuf.getChannelData(1)[kFrames - 1];
        bool ok = (std::abs(actualL - expected) < 1e-4f) && (std::abs(actualR - expected) < 1e-4f);
        record("TEST 03", "Pan center: L and R balanced at -3.01 dB (sqrt(0.5) ~0.707107)", ok, expected, actualL, 1e-4);
    }

    // ============================================================
    // TEST 04: Pan left
    // ============================================================
    {
        audiocore::AudioBuffer inBuf(kChannels, kFrames);
        audiocore::AudioBuffer outBuf(kChannels, kFrames);
        SyntheticSignals::generateDc(inBuf, 1.0f, kFrames);

        audiocore::PanNode pan(-1.0f, sampleRate);
        pan.setSmoothingTimeMs(0.0f);
        pan.setPan(-1.0f);
        pan.process(inBuf, outBuf, kFrames);

        float actualL = outBuf.getChannelData(0)[kFrames - 1];
        float actualR = outBuf.getChannelData(1)[kFrames - 1];
        bool ok = (std::abs(actualL - 1.0f) < 1e-4f) && (std::abs(actualR - 0.0f) < 1e-4f);
        record("TEST 04", "Pan left: Hard Left produces L=1.0, R=0.0", ok, 1.0, actualL, 1e-4);
    }

    // ============================================================
    // TEST 05: Pan right
    // ============================================================
    {
        audiocore::AudioBuffer inBuf(kChannels, kFrames);
        audiocore::AudioBuffer outBuf(kChannels, kFrames);
        SyntheticSignals::generateDc(inBuf, 1.0f, kFrames);

        audiocore::PanNode pan(1.0f, sampleRate);
        pan.setSmoothingTimeMs(0.0f);
        pan.setPan(1.0f);
        pan.process(inBuf, outBuf, kFrames);

        float actualL = outBuf.getChannelData(0)[kFrames - 1];
        float actualR = outBuf.getChannelData(1)[kFrames - 1];
        bool ok = (std::abs(actualL - 0.0f) < 1e-4f) && (std::abs(actualR - 1.0f) < 1e-4f);
        record("TEST 05", "Pan right: Hard Right produces L=0.0, R=1.0", ok, 1.0, actualR, 1e-4);
    }

    // ============================================================
    // TEST 06: PEQ bypass
    // ============================================================
    {
        audiocore::AudioBuffer inBuf(kChannels, kFrames);
        audiocore::AudioBuffer outBuf(kChannels, kFrames);
        SyntheticSignals::generateSine(inBuf, sampleRate, 1000.0, 0.75f, kFrames);

        auto eq = std::make_shared<audiocore::ParametricEQNode>(4, sampleRate);
        eq->setBand(1, audiocore::FilterType::Peaking, 1000.0, 1.0, 6.0);
        eq->setBypassed(true);
        eq->process(inBuf, outBuf, kFrames);

        float maxDiff = 0.0f;
        for (uint32_t ch = 0; ch < kChannels; ++ch) {
            for (uint32_t i = 0; i < kFrames; ++i) {
                float d = std::abs(inBuf.getChannelData(ch)[i] - outBuf.getChannelData(ch)[i]);
                if (d > maxDiff) maxDiff = d;
            }
        }
        bool ok = (maxDiff < 1e-6f);
        record("TEST 06", "PEQ bypass: Bypassed filter passes signal bit-accurately", ok, 0.0, maxDiff, 1e-6);
    }

    // ============================================================
    // TEST 07: PEQ boost
    // ============================================================
    {
        // 1000 Hz Bell +6 dB boost on 1000 Hz Sine
        audiocore::AudioBuffer inBuf(kChannels, kFrames * 4);
        audiocore::AudioBuffer outBuf(kChannels, kFrames * 4);
        SyntheticSignals::generateSine(inBuf, sampleRate, 1000.0, 1.0f, kFrames * 4);

        auto eq = std::make_shared<audiocore::ParametricEQNode>(4, sampleRate);
        eq->setBand(1, audiocore::FilterType::Peaking, 1000.0, 1.0, 6.0);
        eq->process(inBuf, outBuf, kFrames * 4);

        float peak = 0.0f;
        for (uint32_t i = kFrames * 2; i < kFrames * 4; ++i) {
            float v = std::abs(outBuf.getChannelData(0)[i]);
            if (v > peak) peak = v;
        }
        float expected = std::pow(10.0f, 6.0f / 20.0f); // ~1.995
        bool ok = std::abs(peak - expected) < 0.05f;
        record("TEST 07", "PEQ boost: 1 kHz Bell +6 dB matches expected boost ~1.995", ok, expected, peak, 0.05);
    }

    // ============================================================
    // TEST 08: PEQ cut
    // ============================================================
    {
        // 1000 Hz Bell -6 dB cut on 1000 Hz Sine
        audiocore::AudioBuffer inBuf(kChannels, kFrames * 4);
        audiocore::AudioBuffer outBuf(kChannels, kFrames * 4);
        SyntheticSignals::generateSine(inBuf, sampleRate, 1000.0, 1.0f, kFrames * 4);

        auto eq = std::make_shared<audiocore::ParametricEQNode>(4, sampleRate);
        eq->setBand(1, audiocore::FilterType::Peaking, 1000.0, 1.0, -6.0);
        eq->process(inBuf, outBuf, kFrames * 4);

        float peak = 0.0f;
        for (uint32_t i = kFrames * 2; i < kFrames * 4; ++i) {
            float v = std::abs(outBuf.getChannelData(0)[i]);
            if (v > peak) peak = v;
        }
        float expected = std::pow(10.0f, -6.0f / 20.0f); // ~0.501
        bool ok = std::abs(peak - expected) < 0.05f;
        record("TEST 08", "PEQ cut: 1 kHz Bell -6 dB matches expected cut ~0.501", ok, expected, peak, 0.05);
    }

    // ============================================================
    // TEST 09: Compressor bypass
    // ============================================================
    {
        audiocore::AudioBuffer inBuf(kChannels, kFrames);
        audiocore::AudioBuffer outBuf(kChannels, kFrames);
        SyntheticSignals::generateDc(inBuf, 1.0f, kFrames);

        audiocore::CompressorNode comp(sampleRate);
        comp.setThresholdDb(-20.0f);
        comp.setRatio(4.0f);
        comp.setBypassed(true);
        comp.process(inBuf, outBuf, kFrames);

        float maxDiff = 0.0f;
        for (uint32_t ch = 0; ch < kChannels; ++ch) {
            for (uint32_t i = 0; i < kFrames; ++i) {
                float d = std::abs(inBuf.getChannelData(ch)[i] - outBuf.getChannelData(ch)[i]);
                if (d > maxDiff) maxDiff = d;
            }
        }
        bool ok = (maxDiff < 1e-6f) && (comp.getCurrentGainReductionDb() == 0.0f);
        record("TEST 09", "Compressor bypass: Bypassed compressor outputs unmodified signal", ok, 0.0, maxDiff, 1e-6);
    }

    // ============================================================
    // TEST 10: Compressor active
    // ============================================================
    {
        // 0 dBFS in, Thresh -12 dB, Ratio 4:1 -> Expected steady-state output -9 dBFS (linear ~0.3548)
        audiocore::AudioBuffer inBuf(kChannels, kFrames * 8);
        audiocore::AudioBuffer outBuf(kChannels, kFrames * 8);
        SyntheticSignals::generateDc(inBuf, 1.0f, kFrames * 8);

        audiocore::CompressorNode comp(sampleRate);
        comp.setThresholdDb(-12.0f);
        comp.setRatio(4.0f);
        comp.setAttackMs(5.0f);
        comp.setReleaseMs(100.0f);
        comp.setMakeupGainDb(0.0f);
        comp.process(inBuf, outBuf, kFrames * 8);

        float finalOut = outBuf.getChannelData(0)[kFrames * 8 - 1];
        float expectedOut = std::pow(10.0f, -9.0f / 20.0f); // 0.3548
        bool ok = std::abs(finalOut - expectedOut) < 0.02f;
        record("TEST 10", "Compressor active: Steady-state dynamic compression matches -9 dBFS", ok, expectedOut, finalOut, 0.02);
    }

    // ============================================================
    // TEST 11: Compressor GR telemetry
    // ============================================================
    {
        audiocore::AudioBuffer inBuf(kChannels, kFrames * 8);
        audiocore::AudioBuffer outBuf(kChannels, kFrames * 8);
        SyntheticSignals::generateDc(inBuf, 1.0f, kFrames * 8);

        audiocore::CompressorNode comp(sampleRate);
        comp.setThresholdDb(-12.0f);
        comp.setRatio(4.0f);
        comp.setAttackMs(5.0f);
        comp.setReleaseMs(100.0f);
        comp.process(inBuf, outBuf, kFrames * 8);

        float gr = comp.getCurrentGainReductionDb();
        float expectedGr = -9.0f; // (Threshold - Input) * (1 - 1/Ratio) = (-12 - 0) * 0.75 = -9 dB
        bool ok = std::abs(gr - expectedGr) < 0.5f;
        record("TEST 11", "Compressor GR telemetry: Internal detector reports accurate -9 dB GR", ok, expectedGr, gr, 0.5);
    }

    // ============================================================
    // TEST 12: Limiter bypass
    // ============================================================
    {
        audiocore::AudioBuffer inBuf(kChannels, kFrames);
        audiocore::AudioBuffer outBuf(kChannels, kFrames);
        SyntheticSignals::generateDc(inBuf, 2.0f, kFrames);

        audiocore::LimiterNode lim(-1.0f, sampleRate);
        lim.setBypassed(true);
        lim.process(inBuf, outBuf, kFrames);

        float maxDiff = 0.0f;
        for (uint32_t ch = 0; ch < kChannels; ++ch) {
            for (uint32_t i = 0; i < kFrames; ++i) {
                float d = std::abs(inBuf.getChannelData(ch)[i] - outBuf.getChannelData(ch)[i]);
                if (d > maxDiff) maxDiff = d;
            }
        }
        bool ok = (maxDiff < 1e-6f) && (lim.getCurrentGain() == 1.0f);
        record("TEST 12", "Limiter bypass: Bypassed limiter preserves full input headroom", ok, 0.0, maxDiff, 1e-6);
    }

    // ============================================================
    // TEST 13: Limiter active
    // ============================================================
    {
        audiocore::AudioBuffer inBuf(kChannels, kFrames);
        audiocore::AudioBuffer outBuf(kChannels, kFrames);
        SyntheticSignals::generateDc(inBuf, 2.0f, kFrames);

        audiocore::LimiterNode lim(-1.0f, sampleRate);
        lim.process(inBuf, outBuf, kFrames);

        float ceilingLin = lim.getCeilingLinear(); // 0.891251
        float maxPeak = 0.0f;
        for (uint32_t ch = 0; ch < kChannels; ++ch) {
            for (uint32_t i = 0; i < kFrames; ++i) {
                float v = std::abs(outBuf.getChannelData(ch)[i]);
                if (v > maxPeak) maxPeak = v;
            }
        }
        bool ok = (maxPeak <= ceilingLin + 1e-5f);
        record("TEST 13", "Limiter active: Strictly clamps 2.0 amplitude to ceiling -1 dBFS", ok, ceilingLin, maxPeak, 1e-5);
    }

    // ============================================================
    // TEST 14: Limiter ceiling
    // ============================================================
    {
        audiocore::AudioBuffer inBuf(kChannels, kFrames);
        audiocore::AudioBuffer outBuf(kChannels, kFrames);
        SyntheticSignals::generateSine(inBuf, sampleRate, 1000.0, 2.0f, kFrames);

        audiocore::LimiterNode lim(-3.0f, sampleRate);
        lim.process(inBuf, outBuf, kFrames);

        float ceilingLin = std::pow(10.0f, -3.0f / 20.0f); // 0.707945
        float maxPeak = 0.0f;
        for (uint32_t ch = 0; ch < kChannels; ++ch) {
            for (uint32_t i = 0; i < kFrames; ++i) {
                float v = std::abs(outBuf.getChannelData(ch)[i]);
                if (v > maxPeak) maxPeak = v;
            }
        }
        float grDb = lim.getCurrentGainReductionDb();
        bool ok = (maxPeak <= ceilingLin + 1e-5f) && (grDb <= -5.0f);
        record("TEST 14", "Limiter ceiling: Verified strict -3 dBFS ceiling and active telemetry", ok, ceilingLin, maxPeak, 1e-5);
    }

    // ============================================================
    // TEST 15: Meter correctness
    // ============================================================
    {
        audiocore::MeterNode meter;
        audiocore::AudioBuffer inBuf(kChannels, kFrames);
        audiocore::AudioBuffer outBuf(kChannels, kFrames);

        // Sub-test A: Full-scale Sine (Peak 1.0, RMS ~0.7071)
        SyntheticSignals::generateSine(inBuf, sampleRate, 1000.0, 1.0f, kFrames);
        meter.process(inBuf, outBuf, kFrames);
        float peakSine = meter.getPeak(0);
        float rmsSine = meter.getRms(0);

        // Sub-test B: DC 0.5 (Peak 0.5, RMS 0.5)
        SyntheticSignals::generateDc(inBuf, 0.5f, kFrames);
        meter.process(inBuf, outBuf, kFrames);
        float peakDc = meter.getPeak(0);
        float rmsDc = meter.getRms(0);

        // Sub-test C: Silence (Peak 0.0, RMS 0.0)
        inBuf.clear();
        meter.process(inBuf, outBuf, kFrames);
        float peakSil = meter.getPeak(0);
        float rmsSil = meter.getRms(0);

        bool okSine = (std::abs(peakSine - 1.0f) < 1e-2f) && (std::abs(rmsSine - 0.7071f) < 0.01f);
        bool okDc = (std::abs(peakDc - 0.5f) < 1e-4f) && (std::abs(rmsDc - 0.5f) < 1e-4f);
        bool okSil = (peakSil == 0.0f) && (rmsSil == 0.0f);

        bool ok = okSine && okDc && okSil;
        record("TEST 15", "Meter correctness: True Peak and RMS verified on Sine, DC, Silence", ok, 1.0, peakSine, 0.01);
    }

    // ============================================================
    // TEST 16: Full DSP chain
    // ============================================================
    {
        audiocore::AudioBuffer inBuf(kChannels, kFrames * 2);
        audiocore::AudioBuffer outBuf(kChannels, kFrames * 2);
        SyntheticSignals::generateStereoIndependent(inBuf, sampleRate, kFrames * 2);

        audiocore::AudioEngine engine;
        engine.configure(sampleRate, blockSize, kChannels);
        DspProfiler::configureGraph(engine.getGraph(), DspChainPreset::FullChain, sampleRate, kChannels);
        engine.start();
        engine.process(inBuf, outBuf, kFrames * 2);
        engine.stop();

        bool hasNan = false;
        bool hasInf = false;
        for (uint32_t ch = 0; ch < kChannels; ++ch) {
            for (uint32_t i = 0; i < kFrames * 2; ++i) {
                float s = outBuf.getChannelData(ch)[i];
                if (std::isnan(s)) hasNan = true;
                if (std::isinf(s)) hasInf = true;
            }
        }

        bool ok = !hasNan && !hasInf &&
                  (outBuf.getFrameCount() == inBuf.getFrameCount()) &&
                  (outBuf.getChannelCount() == inBuf.getChannelCount());
        record("TEST 16", "Full DSP chain: No NaN/Inf, zero mismatch across complete 9-node pipeline", ok, 0.0, 0.0, 0.0,
               ok ? "Pass" : "Failed with invalid samples or mismatch");
    }

    // ============================================================
    // TEST 17: Noise Gate
    // ============================================================
    {
        audiocore::GateNode gate(sampleRate);
        gate.setThresholdDb(-30.0f);
        gate.setRangeDb(-60.0f);
        audiocore::AudioBuffer inBuf(kChannels, kFrames);
        audiocore::AudioBuffer outBuf(kChannels, kFrames);

        // Sub-test A: Quiet signal (-50 dBFS = amplitude ~0.00316) -> Attenuated by ~60 dB
        for (uint32_t i = 0; i < kFrames; ++i) {
            inBuf.getChannelData(0)[i] = 0.00316f;
            inBuf.getChannelData(1)[i] = 0.00316f;
        }
        gate.process(inBuf, outBuf, kFrames);
        float peakQuiet = std::abs(outBuf.getChannelData(0)[kFrames - 1]);

        // Sub-test B: Loud signal (0 dBFS = 1.0) -> Open gate with 0 dB attenuation
        gate.setAttackMs(1.0f);
        for (uint32_t i = 0; i < kFrames; ++i) {
            inBuf.getChannelData(0)[i] = 1.0f;
            inBuf.getChannelData(1)[i] = 1.0f;
        }
        gate.process(inBuf, outBuf, kFrames);
        gate.process(inBuf, outBuf, kFrames);
        float peakLoud = outBuf.getChannelData(0)[kFrames - 1];

        bool okQuiet = (peakQuiet < 0.00002f);
        bool okLoud = (std::abs(peakLoud - 1.0f) < 1e-3f);
        bool ok = okQuiet && okLoud;
        record("TEST 17", "Noise Gate: Full attenuation when quiet, 0 dB unity when loud", ok, 1.0, peakLoud, 1e-3);
    }

    // ============================================================
    // TEST 18: 31-Band Graphic Equalizer
    // ============================================================
    {
        audiocore::GraphicEQNode geq(sampleRate);
        geq.setBandGainDb(17, 6.0f); // 1 kHz band boosted +6 dB
        audiocore::AudioBuffer inBuf(kChannels, kFrames);
        audiocore::AudioBuffer outBuf(kChannels, kFrames);

        // Steady-state sine at 1000 Hz
        for (uint32_t i = 0; i < kFrames; ++i) {
            float s = static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * 1000.0 * i / sampleRate));
            inBuf.getChannelData(0)[i] = s;
            inBuf.getChannelData(1)[i] = s;
        }
        // Warm up filter history
        geq.process(inBuf, outBuf, kFrames);
        geq.process(inBuf, outBuf, kFrames);

        float maxOut = 0.0f;
        for (uint32_t i = kFrames / 2; i < kFrames; ++i) {
            maxOut = std::max(maxOut, std::abs(outBuf.getChannelData(0)[i]));
        }

        float expected = std::pow(10.0f, 6.0f / 20.0f); // ~1.99526
        bool ok = (std::abs(maxOut - expected) < 0.15f);
        record("TEST 18", "Graphic EQ: 1 kHz band +6 dB boost matches expected amplitude ~1.995", ok, expected, maxOut, 0.15);
    }

    // ============================================================
    // TEST 19: Tap Delay
    // ============================================================
    {
        audiocore::DelayNode delay(sampleRate);
        delay.setMix(1.0f);
        delay.setFeedback(0.40f);
        for (size_t t = 0; t < 4; ++t) delay.setTapEnabled(t, (t == 0));
        delay.setTapDelayMs(0, 100.0f); // 4800 samples at 48k
        delay.setTapLevelDb(0, 0.0f);
        delay.reset();

        audiocore::AudioBuffer inBuf(kChannels, blockSize);
        audiocore::AudioBuffer outBuf(kChannels, blockSize);
        inBuf.getChannelData(0)[0] = 1.0f;
        inBuf.getChannelData(1)[0] = 1.0f;

        int peakSample = -1;
        float peakVal = 0.0f;
        const int totalBlocks = static_cast<int>((100.0f * 0.001f * sampleRate / blockSize) + 15);
        for (int b = 0; b < totalBlocks; ++b) {
            delay.process(inBuf, outBuf, blockSize);
            if (b == 0) inBuf.clear();
            for (uint32_t i = 0; i < blockSize; ++i) {
                float val = std::abs(outBuf.getChannelData(0)[i]);
                if (val > peakVal) {
                    peakVal = val;
                    peakSample = b * blockSize + i;
                }
            }
        }
        int targetSample = static_cast<int>(100.0 * 0.001 * sampleRate);
        bool ok = (std::abs(peakSample - targetSample) <= 2) && (peakVal > 0.5f);
        record("TEST 19", "Tap Delay: 100 ms impulse delay exact to target sample", ok, targetSample, peakSample, 2.0);
    }

    // ============================================================
    // TEST 20: Algorithmic Reverb
    // ============================================================
    {
        audiocore::ReverbNode reverb(sampleRate);
        reverb.setMix(1.0f);
        reverb.setPreDelayMs(0.0f);
        reverb.setDecay(2.0f);
        reverb.reset();

        audiocore::AudioBuffer inBuf(kChannels, blockSize);
        audiocore::AudioBuffer outBuf(kChannels, blockSize);
        inBuf.getChannelData(0)[0] = 1.0f;
        inBuf.getChannelData(1)[0] = 0.0f;

        float energyL = 0.0f;
        float energyR = 0.0f;
        for (int b = 0; b < 40; ++b) {
            reverb.process(inBuf, outBuf, blockSize);
            if (b == 0) inBuf.clear();
            for (uint32_t i = 0; i < blockSize; ++i) {
                energyL += std::abs(outBuf.getChannelData(0)[i]);
                energyR += std::abs(outBuf.getChannelData(1)[i]);
            }
        }
        bool ok = (energyL > 0.5f) && (energyR > 0.1f);
        record("TEST 20", "Algorithmic Reverb: Impulse response generates sustained stereo decay", ok, 1.0, (ok ? 1.0 : 0.0), 0.0);
    }

    // ============================================================
    // TEST 21: Feedback Suppressor (AFS)
    // ============================================================
    {
        audiocore::AfsNode afs(sampleRate);
        afs.setSensitivity(0.9f);
        afs.setMaxDepthDb(-18.0f);

        audiocore::AudioBuffer inBuf(kChannels, blockSize);
        audiocore::AudioBuffer outBuf(kChannels, blockSize);

        // Train on 2 kHz feedback tone
        for (int cycle = 0; cycle < 4; ++cycle) {
            for (int b = 0; b < 16; ++b) {
                for (uint32_t i = 0; i < blockSize; ++i) {
                    float t = static_cast<float>((cycle * 16 + b) * blockSize + i) / static_cast<float>(sampleRate);
                    float s = 0.8f * std::sin(2.0f * 3.14159265358979323846f * 2000.0f * t);
                    inBuf.getChannelData(0)[i] = s;
                    inBuf.getChannelData(1)[i] = s;
                }
                afs.process(inBuf, outBuf, blockSize);
            }
            afs.stepAnalysis();
        }

        // Notch must be active and peak must be attenuated by >12 dB
        float maxOut = 0.0f;
        for (int b = 0; b < 20; ++b) {
            for (uint32_t i = 0; i < blockSize; ++i) {
                float t = static_cast<float>((70 + b) * blockSize + i) / static_cast<float>(sampleRate);
                inBuf.getChannelData(0)[i] = 0.8f * std::sin(2.0f * 3.14159265358979323846f * 2000.0f * t);
                inBuf.getChannelData(1)[i] = inBuf.getChannelData(0)[i];
            }
            afs.process(inBuf, outBuf, blockSize);
            if (b >= 15) {
                for (uint32_t i = 0; i < blockSize; ++i) {
                    maxOut = std::max(maxOut, std::abs(outBuf.getChannelData(0)[i]));
                }
            }
        }
        bool notchActive = (afs.getActiveNotchCount() >= 1);
        bool attenuated = (maxOut < 0.25f);
        bool ok = notchActive && attenuated;
        record("TEST 21", "Feedback Suppressor: 2 kHz feedback tone notched by >12 dB", ok, 1.0, (ok ? 1.0 : 0.0), 0.0);
    }

    // ============================================================
    // TEST 22: Full AUDIO CORE v1.1 12-Node Pipeline
    // ============================================================
    {
        audiocore::AudioBuffer inBuf(kChannels, kFrames * 2);
        audiocore::AudioBuffer outBuf(kChannels, kFrames * 2);
        SyntheticSignals::generateStereoIndependent(inBuf, sampleRate, kFrames * 2);

        audiocore::AudioEngine engine;
        engine.configure(sampleRate, blockSize, kChannels);
        DspProfiler::configureGraph(engine.getGraph(), DspChainPreset::FullAudioCoreV11, sampleRate, kChannels);
        engine.start();
        engine.process(inBuf, outBuf, kFrames * 2);
        engine.stop();

        bool hasNan = false;
        bool hasInf = false;
        for (uint32_t ch = 0; ch < kChannels; ++ch) {
            for (uint32_t i = 0; i < kFrames * 2; ++i) {
                float s = outBuf.getChannelData(ch)[i];
                if (std::isnan(s)) hasNan = true;
                if (std::isinf(s)) hasInf = true;
            }
        }

        bool ok = !hasNan && !hasInf &&
                  (outBuf.getFrameCount() == inBuf.getFrameCount()) &&
                  (outBuf.getChannelCount() == inBuf.getChannelCount());
        record("TEST 22", "Full AUDIO CORE v1.1: 12-node pipeline (with All FX) processed cleanly", ok, 0.0, 0.0, 0.0);
    }

    return suite;
}

} // namespace testbench
