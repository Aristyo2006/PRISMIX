#include "model/mixer_scene.h"
#include "model/mixer_state.h"
#include "model/routing_state.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {
bool check(const char* label, bool condition) {
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << label << '\n';
    return condition;
}
}

int main() {
    bool ok = true;
    const auto path = std::filesystem::temp_directory_path() / "audiocore_scene_roundtrip.cfg";
    mixer_state_init();
    g_routing_matrix = RoutingMatrix{};

    ChannelState vocal{};
    vocal.id = 1;
    std::snprintf(vocal.name, sizeof(vocal.name), "Lead Vocal");
    vocal.input_source_index = 2;
    vocal.fader = 0.72f;
    vocal.gain = 0.61f;
    vocal.pan = 0.38f;
    vocal.mute = true;
    vocal.polarity_inverted = true;
    vocal.hpf_on = true;
    vocal.hpf_frequency = 143.0f;
    vocal.eq_freq = {90.0f, 430.0f, 3100.0f, 12000.0f};
    vocal.eq_gain = {2.0f, -3.0f, 4.5f, -1.5f};
    vocal.eq_q = {0.8f, 1.2f, 0.9f, 0.7f};
    vocal.eq_enabled = {true, false, true, true};
    vocal.eq_filter_type = {0, 1, 1, 2};
    vocal.comp_thresh = -26.0f;
    vocal.comp_ratio = 7.0f;
    vocal.gate_on = false;
    vocal.geq_mode_active = true;
    vocal.geq_bypass = true;
    vocal.geq.set_band_gain(8, 5.0f);
    ok &= check("Built-in compressor can be assigned to a strip", vocal.fx_rack.add_plugin(
        "http://audiocore.org/plugins/compressor"));
    auto* compressor = vocal.fx_rack.get_slot(0);
    compressor->set_bypassed(true);
    compressor->set_parameter_value("thresh", -27.0f);
    ok &= check("FX rack accepts a second ordered processor", vocal.fx_rack.add_plugin(
        "http://audiocore.org/plugins/delay"));
    vocal.fx_rack.get_slot(1)->set_parameter_value("feedback", 0.52f);
    g_mixer_state.channels.push_back(vocal);
    g_routing_matrix.set_channel_input_source(0, 2);
    g_routing_matrix.set_channel_main_lr(0, true);

    ChannelState guitar{};
    guitar.id = 2;
    std::snprintf(guitar.name, sizeof(guitar.name), "Guitar");
    guitar.input_source_index = 23;
    std::snprintf(guitar.input_label, sizeof(guitar.input_label), "DEVICE IN 24");
    guitar.solo = true;
    guitar.output_route = ChannelOutputRoute::Omni;
    guitar.output_route_index = 7;
    guitar.format = ChannelFormat::Stereo;
    guitar.input_source_index_r = 24;
    guitar.sends = {0.25f, 0.50f, 0.75f, 0.0f};
    guitar.send_enabled = {true, true, false, true};
    guitar.send_pre_fader = {true, false, true, false};
    guitar.limiter_enabled = true;
    guitar.limiter_ceiling_db = -3.5f;
    guitar.limiter_release_ms = 95.0f;
    ok &= check("A second strip owns its own effect slot", guitar.fx_rack.add_plugin(
        "http://audiocore.org/plugins/reverb"));
    guitar.fx_rack.get_slot(0)->set_parameter_value("mix", 0.47f);
    g_mixer_state.channels.push_back(guitar);
    g_routing_matrix.set_channel_input_source(1, 5);
    g_routing_matrix.set_channel_main_lr(1, false);

    // Configure Master Bus and AUX Buses
    g_mixer_state.master.limiter_enabled = true;
    g_mixer_state.master.limiter_ceiling_db = -1.2f;
    g_mixer_state.master.limiter_release_ms = 42.0f;
    g_mixer_state.master.fx_rack.add_plugin("http://audiocore.org/plugins/limiter");
    g_mixer_state.master.fx_rack.get_slot(0)->set_parameter_value("ceiling", -1.2f);

    g_mixer_state.aux_buses[0].format = ChannelFormat::Mono;
    g_mixer_state.aux_buses[0].fader = 0.80f;
    g_mixer_state.aux_buses[0].return_gain = 0.65f;
    g_mixer_state.aux_buses[0].fx_rack.add_plugin("http://audiocore.org/plugins/reverb");
    g_mixer_state.aux_buses[0].fx_rack.get_slot(0)->set_parameter_value("mix", 1.0f);

    g_mixer_state.aux_buses[1].format = ChannelFormat::Stereo;
    g_mixer_state.aux_buses[1].fx_rack.add_plugin("http://audiocore.org/plugins/delay");
    g_mixer_state.aux_buses[1].fx_rack.get_slot(0)->set_parameter_value("feedback", 0.44f);

    std::string error;
    ok &= check("Scene saves to a local file", save_mixer_scene(path.string(), &error));
    mixer_state_init();
    ok &= check("Scene reloads", load_mixer_scene(path.string(), &error));
    ok &= check("Scene restores individually added strips and routing",
        g_mixer_state.channels.size() == 2 &&
        std::string(g_mixer_state.channels[0].name) == "Lead Vocal" &&
        g_mixer_state.channels[0].format == ChannelFormat::Mono &&
        g_mixer_state.channels[0].input_source_index == 2 &&
        g_mixer_state.channels[1].format == ChannelFormat::Stereo &&
        g_mixer_state.channels[1].input_source_index == 23 &&
        g_mixer_state.channels[1].input_source_index_r == 24 &&
        std::string(g_mixer_state.channels[1].input_label) == "DEVICE IN 24" &&
        g_routing_matrix.get_channel_main_lr(0) &&
        !g_routing_matrix.get_channel_main_lr(1) && g_mixer_state.channels[1].solo &&
        g_mixer_state.channels[1].output_route == ChannelOutputRoute::Omni &&
        g_mixer_state.channels[1].output_route_index == 7);

    const auto& loadedGuitar = g_mixer_state.channels[1];
    ok &= check("Scene restores per-channel AUX sends, pre/post tap points, and limiter state",
        std::abs(loadedGuitar.sends[0] - 0.25f) < 1e-5f &&
        std::abs(loadedGuitar.sends[1] - 0.50f) < 1e-5f &&
        std::abs(loadedGuitar.sends[2] - 0.75f) < 1e-5f &&
        loadedGuitar.send_enabled[0] && loadedGuitar.send_enabled[1] && !loadedGuitar.send_enabled[2] &&
        loadedGuitar.send_pre_fader[0] && !loadedGuitar.send_pre_fader[1] &&
        loadedGuitar.limiter_enabled && std::abs(loadedGuitar.limiter_ceiling_db + 3.5f) < 1e-5f &&
        std::abs(loadedGuitar.limiter_release_ms - 95.0f) < 1e-5f);

    ok &= check("Scene restores Master Bus and AUX Bus DSP / FX / Format state",
        g_mixer_state.master.limiter_enabled &&
        std::abs(g_mixer_state.master.limiter_ceiling_db + 1.2f) < 1e-5f &&
        std::abs(g_mixer_state.master.limiter_release_ms - 42.0f) < 1e-5f &&
        g_mixer_state.master.fx_rack.count() == 1 &&
        g_mixer_state.aux_buses[0].format == ChannelFormat::Mono &&
        std::abs(g_mixer_state.aux_buses[0].fader - 0.80f) < 1e-5f &&
        std::abs(g_mixer_state.aux_buses[0].return_gain - 0.65f) < 1e-5f &&
        g_mixer_state.aux_buses[0].fx_rack.count() == 1 &&
        g_mixer_state.aux_buses[0].fx_rack.get_slot(0)->get_uri() == "http://audiocore.org/plugins/reverb" &&
        g_mixer_state.aux_buses[1].format == ChannelFormat::Stereo &&
        g_mixer_state.aux_buses[1].fx_rack.count() == 1 &&
        std::abs(g_mixer_state.aux_buses[1].fx_rack.get_slot(0)->find_parameter("feedback")->current_val - 0.44f) < 1e-5f);

    const auto& loadedVocal = g_mixer_state.channels[0];
    ok &= check("Scene restores strip HPF, PEQ, compressor, gate and graphic EQ state",
        loadedVocal.polarity_inverted && loadedVocal.hpf_on && std::abs(loadedVocal.hpf_frequency - 143.0f) < 1e-5f &&
        std::abs(loadedVocal.eq_freq[2] - 3100.0f) < 1e-5f && std::abs(loadedVocal.eq_gain[2] - 4.5f) < 1e-5f &&
        !loadedVocal.eq_enabled[1] && loadedVocal.comp_thresh == -26.0f && loadedVocal.comp_ratio == 7.0f &&
        !loadedVocal.gate_on && loadedVocal.geq_mode_active && loadedVocal.geq_bypass &&
        std::abs(loadedVocal.geq.get_band_gain(8) - 5.0f) < 1e-5f);

    auto* loadedCompressor = g_mixer_state.channels[0].fx_rack.get_slot(0);
    auto* loadedDelay = g_mixer_state.channels[0].fx_rack.get_slot(1);
    auto* loadedReverb = g_mixer_state.channels[1].fx_rack.get_slot(0);
    ok &= check("Scene restores a separate FX insert and parameter state per strip",
        loadedCompressor && loadedCompressor->get_uri() == "http://audiocore.org/plugins/compressor" &&
        loadedCompressor->is_bypassed() &&
        std::abs(loadedCompressor->find_parameter("thresh")->current_val + 27.0f) < 1e-5f &&
        loadedReverb && loadedReverb->get_uri() == "http://audiocore.org/plugins/reverb" &&
        std::abs(loadedReverb->find_parameter("mix")->current_val - 0.47f) < 1e-5f &&
        loadedDelay && loadedDelay->get_uri() == "http://audiocore.org/plugins/delay" &&
        std::abs(loadedDelay->find_parameter("feedback")->current_val - 0.52f) < 1e-5f);

    auto independentCopy = g_mixer_state.channels[0];
    independentCopy.fx_rack.get_slot(0)->set_parameter_value("thresh", -5.0f);
    ok &= check("Copied channel FX parameters do not alias the original strip",
        std::abs(g_mixer_state.channels[0].fx_rack.get_slot(0)->find_parameter("thresh")->current_val + 27.0f) < 1e-5f);

    const auto legacyPath = std::filesystem::temp_directory_path() / "audiocore_scene_v2_compat.cfg";
    {
        std::ofstream legacy(legacyPath);
        legacy << "AUDIOCORE_MIXER_SCENE 2\n1\n"
               << "1 \"Legacy Strip\" \"DEVICE IN 01\" 0 0.86 0.5 0.5 0 0 1 0 \"\" 0 0\n";
    }
    ok &= check("Version 2 scenes remain loadable after the DSP scene upgrade",
        load_mixer_scene(legacyPath.string(), &error) && g_mixer_state.channels.size() == 1 &&
        std::string(g_mixer_state.channels[0].name) == "Legacy Strip" &&
        std::string(g_mixer_state.channels[0].input_label) == "DEVICE IN 01" &&
        std::abs(g_mixer_state.channels[0].hpf_frequency - 80.0f) < 1e-5f);

    std::filesystem::remove(path);
    std::filesystem::remove(legacyPath);
    return ok ? 0 : 1;
}
