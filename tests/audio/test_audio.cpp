#include "audiocore/audio_engine.hpp"
#include "audiocore/audio_buffer.hpp"
#include "audiocore/audio_node.hpp"
#include "audiocore/audio_graph.hpp"
#include "audiocore/gain_node.hpp"
#include "audiocore/parameter.hpp"
#include <iostream>
#include <cmath>
#include <string>

namespace {

int g_pass_count = 0;
int g_fail_count = 0;

void test_check(const std::string& test_name, bool condition) {
    if (condition) {
        std::cout << "  [PASS] " << test_name << std::endl;
        ++g_pass_count;
    } else {
        std::cerr << "  [FAIL] " << test_name << std::endl;
        ++g_fail_count;
    }
}

bool approx_equal(float a, float b, float epsilon = 0.0001f) {
    return std::fabs(a - b) <= epsilon;
}

} // namespace

int main() {
    std::cout << "============================================================" << std::endl;
    std::cout << "AUDIO CORE: CORE AUDIO ENGINE UNIT TEST SUITE" << std::endl;
    std::cout << "============================================================" << std::endl;

    // ------------------------------------------------------------
    // TEST 1: AudioBuffer creation & dimensions
    // ------------------------------------------------------------
    std::cout << "\n--- TEST 1: AudioBuffer Creation ---" << std::endl;
    {
        audiocore::AudioBuffer buffer(2, 128);
        test_check("Channel count matches 2", buffer.getChannelCount() == 2);
        test_check("Frame count matches 128", buffer.getFrameCount() == 128);
        test_check("Buffer is not empty", !buffer.isEmpty());
    }

    // ------------------------------------------------------------
    // TEST 2: AudioBuffer planar channel access
    // ------------------------------------------------------------
    std::cout << "\n--- TEST 2: AudioBuffer Channel Access ---" << std::endl;
    {
        audiocore::AudioBuffer buffer(2, 128);
        float* ch0 = buffer.getChannelData(0);
        float* ch1 = buffer.getChannelData(1);

        test_check("Channel 0 pointer is non-null", ch0 != nullptr);
        test_check("Channel 1 pointer is non-null", ch1 != nullptr);
        test_check("Channels have distinct non-overlapping pointers", ch0 != ch1);
        test_check("Pointer difference matches frame count", (ch1 - ch0) == 128);

        // Operator [] access
        test_check("operator[] matches getChannelData(0)", buffer[0] == ch0);
        test_check("operator[] matches getChannelData(1)", buffer[1] == ch1);

        // Write and read
        ch0[0] = 0.75f;
        ch1[127] = -0.5f;
        test_check("Readback sample ch0[0]", buffer[0][0] == 0.75f);
        test_check("Readback sample ch1[127]", buffer[1][127] == -0.5f);

        // Clear buffer
        buffer.clear();
        test_check("Clear zeroes ch0[0]", buffer[0][0] == 0.0f);
        test_check("Clear zeroes ch1[127]", buffer[1][127] == 0.0f);
    }

    // ------------------------------------------------------------
    // TEST 3: GainNode unity gain (0 dB -> linear 1.0, Output ≈ 1.0)
    // ------------------------------------------------------------
    std::cout << "\n--- TEST 3: GainNode Unity Gain (0 dB) ---" << std::endl;
    {
        audiocore::GainNode gain(0.0f);
        test_check("GainNode initial dB is 0 dB", approx_equal(gain.getGainDb(), 0.0f));
        test_check("Linear gain is 1.0", approx_equal(gain.getLinearGain(), 1.0f));

        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);

        // Fill input with 1.0
        for (uint32_t c = 0; c < 2; ++c) {
            for (uint32_t i = 0; i < 128; ++i) {
                in[c][i] = 1.0f;
            }
        }

        gain.process(in, out, 128);

        test_check("Output[0][0] ≈ 1.0 at 0 dB", approx_equal(out[0][0], 1.0f));
        test_check("Output[1][64] ≈ 1.0 at 0 dB", approx_equal(out[1][64], 1.0f));
    }

    // ------------------------------------------------------------
    // TEST 4: GainNode mute (mute -> linear 0.0, Output = 0.0)
    // ------------------------------------------------------------
    std::cout << "\n--- TEST 4: GainNode Mute ---" << std::endl;
    {
        audiocore::GainNode gain(0.0f);
        gain.setMuted(true);
        test_check("GainNode isMuted is true", gain.isMuted());

        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        for (uint32_t c = 0; c < 2; ++c) {
            for (uint32_t i = 0; i < 128; ++i) in[c][i] = 1.0f;
        }

        gain.process(in, out, 128);

        test_check("Output[0][0] == 0.0 when muted", out[0][0] == 0.0f);
        test_check("Output[1][100] == 0.0 when muted", out[1][100] == 0.0f);

        // Unmute restores gain
        gain.setMuted(false);
        gain.process(in, out, 128);
        test_check("Output unmuted returns to 1.0", approx_equal(out[0][0], 1.0f));
    }

    // ------------------------------------------------------------
    // TEST 5: GainNode -6 dB (Output ≈ 0.501187)
    // ------------------------------------------------------------
    std::cout << "\n--- TEST 5: GainNode -6 dB ---" << std::endl;
    {
        audiocore::GainNode gain;
        gain.setGainDb(-6.0f);

        const float expected_linear = std::pow(10.0f, -6.0f / 20.0f); // ≈ 0.50118723
        test_check("getGainDb() returns -6.0 dB", approx_equal(gain.getGainDb(), -6.0f));
        test_check("Linear gain matches 10^(-6/20) ≈ 0.501187", approx_equal(gain.getLinearGain(), expected_linear));

        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);
        for (uint32_t c = 0; c < 2; ++c) {
            for (uint32_t i = 0; i < 128; ++i) in[c][i] = 1.0f;
        }

        gain.process(in, out, 128);

        test_check("Input 1.0 at -6 dB produces Output ≈ 0.501187", approx_equal(out[0][0], 0.501187f));
        test_check("Channel 1 at -6 dB produces Output ≈ 0.501187", approx_equal(out[1][63], 0.501187f));
    }

    // ------------------------------------------------------------
    // TEST 6: Stereo Processing
    // ------------------------------------------------------------
    std::cout << "\n--- TEST 6: Stereo Independent Channel Processing ---" << std::endl;
    {
        audiocore::GainNode gain(-6.0f);
        audiocore::AudioBuffer in(2, 128);
        audiocore::AudioBuffer out(2, 128);

        // Channel 0 has positive 1.0, Channel 1 has negative 1.0
        for (uint32_t i = 0; i < 128; ++i) {
            in[0][i] = 1.0f;
            in[1][i] = -1.0f;
        }

        gain.process(in, out, 128);

        test_check("Left channel scaled correctly", approx_equal(out[0][50], 0.501187f));
        test_check("Right channel scaled correctly (negative)", approx_equal(out[1][50], -0.501187f));
    }

    // ------------------------------------------------------------
    // TEST 7: AudioEngine Initialization & Lifecycle
    // ------------------------------------------------------------
    std::cout << "\n--- TEST 7: AudioEngine Initialization ---" << std::endl;
    {
        audiocore::AudioEngine engine;
        test_check("Engine initial state is Unconfigured", engine.getState() == audiocore::EngineState::Unconfigured);
        test_check("Engine is not running initially", !engine.isRunning());

        bool cfg_ok = engine.configure(48000.0, 128, 2);
        test_check("engine.configure(48000, 128, 2) succeeds", cfg_ok);
        test_check("Sample rate is 48000", engine.getSampleRate() == 48000.0);
        test_check("Block size is 128", engine.getBlockSize() == 128);
        test_check("Channels is 2", engine.getChannelCount() == 2);
        test_check("Engine state is Stopped after configure", engine.getState() == audiocore::EngineState::Stopped);

        bool start_ok = engine.start();
        test_check("engine.start() succeeds", start_ok);
        test_check("engine.isRunning() is true", engine.isRunning());

        engine.stop();
        test_check("engine.stop() transitions to Stopped", engine.getState() == audiocore::EngineState::Stopped);
        test_check("engine.isRunning() is false after stop", !engine.isRunning());
    }

    // ------------------------------------------------------------
    // TEST 8: AudioGraph Processing & Chaining
    // ------------------------------------------------------------
    std::cout << "\n--- TEST 8: AudioGraph Processing ---" << std::endl;
    {
        audiocore::AudioEngine engine;
        engine.configure(48000.0, 128, 2);
        engine.start();

        // Default pipeline installed in configure: Input -> GainNode -> Output
        auto gain_node = std::dynamic_pointer_cast<audiocore::GainNode>(engine.getGraph().getNode(0));
        test_check("Initial AudioGraph has GainNode", gain_node != nullptr);

        if (gain_node) {
            gain_node->setGainDb(-6.0f);
        }

        audiocore::AudioBuffer input(2, 128);
        audiocore::AudioBuffer output(2, 128);
        for (uint32_t c = 0; c < 2; ++c) {
            for (uint32_t i = 0; i < 128; ++i) input[c][i] = 1.0f;
        }

        engine.process(input, output, 128);

        test_check("Engine processes input through graph: Output ≈ 0.501187",
                   approx_equal(output[0][0], 0.501187f));
        test_check("Stereo output right channel ≈ 0.501187",
                   approx_equal(output[1][127], 0.501187f));

        engine.stop();
    }

    // ------------------------------------------------------------
    // TEST 9: In-place Processing
    // ------------------------------------------------------------
    std::cout << "\n--- TEST 9: In-place Processing ---" << std::endl;
    {
        audiocore::GainNode gain(-6.0f);
        audiocore::AudioBuffer buf(2, 128);
        for (uint32_t c = 0; c < 2; ++c) {
            for (uint32_t i = 0; i < 128; ++i) buf[c][i] = 1.0f;
        }

        // In-place: &input == &output
        gain.process(buf, buf, 128);
        test_check("In-place processing succeeds (buf ≈ 0.501187)",
                   approx_equal(buf[0][0], 0.501187f));
    }

    // ------------------------------------------------------------
    // TEST 10: Parameter Abstraction
    // ------------------------------------------------------------
    std::cout << "\n--- TEST 10: Parameter Thread-Safety & Mapping ---" << std::endl;
    {
        audiocore::Parameter param("Volume", 0.0f, -60.0f, 12.0f, "dB");
        test_check("Parameter name is Volume", param.getName() == "Volume");
        test_check("Parameter unit is dB", param.getUnit() == "dB");
        test_check("Parameter default is 0.0", approx_equal(param.getValue(), 0.0f));

        param.setValue(-30.0f);
        test_check("Parameter value is -30.0", approx_equal(param.getValue(), -30.0f));

        // Normalized mapping (-30 is halfway between -60 and 0? No, range is -60..+12 = 72)
        // Normalized = (-30 - (-60)) / 72 = 30 / 72 = 0.416666
        test_check("Normalized value is within [0, 1]", param.getNormalized() >= 0.0f && param.getNormalized() <= 1.0f);

        param.setNormalized(1.0f);
        test_check("Normalized 1.0 maps to max (+12 dB)", approx_equal(param.getValue(), 12.0f));

        param.setNormalized(0.0f);
        test_check("Normalized 0.0 maps to min (-60 dB)", approx_equal(param.getValue(), -60.0f));
    }

    std::cout << "\n============================================================" << std::endl;
    std::cout << "TEST RESULTS: " << g_pass_count << " PASSED, " << g_fail_count << " FAILED" << std::endl;
    std::cout << "============================================================" << std::endl;

    return (g_fail_count == 0) ? 0 : 1;
}

