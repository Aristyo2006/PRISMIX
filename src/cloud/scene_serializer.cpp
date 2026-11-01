#include "scene_serializer.h"
#include "json_helper.hpp"
#include "cloud_config.h"

#include "../model/mixer_state.h"
#include "../model/routing_state.h"
#include "../service/audio_service.h"
#include "../ui/ui_audio_bridge.h"

#include <cmath>
#include <cstdio>
#include <algorithm>

namespace Prismix {
namespace Cloud {

namespace {

using JsonVal = Prismix::Json::Value;

JsonVal serialize_strip(const ChannelState& ch) {
    JsonVal v = JsonVal::make_object();
    v.set("id", ch.id);
    v.set("format", static_cast<int>(ch.format));
    v.set("name", std::string(ch.name));
    v.set("input_label", std::string(ch.input_label));
    v.set("input_source_index", ch.input_source_index);
    v.set("input_source_index_r", ch.input_source_index_r);
    v.set("fader", ch.fader);
    v.set("return_gain", ch.return_gain);
    v.set("gain", ch.gain);
    v.set("pan", ch.pan);
    v.set("mute", ch.mute);
    v.set("solo", ch.solo);
    v.set("route_to_main", ch.route_to_main);
    v.set("output_route", static_cast<int>(ch.output_route));
    v.set("output_route_index", ch.output_route_index);
    v.set("polarity_inverted", ch.polarity_inverted);
    v.set("hpf_on", ch.hpf_on);
    v.set("hpf_frequency", ch.hpf_frequency);

    // Parametric EQ
    v.set("eq_bypass", ch.eq_bypass);
    JsonVal eq_arr = JsonVal::make_array();
    for (size_t b = 0; b < 4; ++b) {
        JsonVal b_obj = JsonVal::make_object();
        b_obj.set("freq", ch.eq_freq[b]);
        b_obj.set("gain", ch.eq_gain[b]);
        b_obj.set("q", ch.eq_q[b]);
        b_obj.set("enabled", ch.eq_enabled[b]);
        b_obj.set("filter_type", ch.eq_filter_type[b]);
        eq_arr.push_back(b_obj);
    }
    v.set("eq_bands", eq_arr);

    // Graphic EQ
    v.set("geq_mode_active", ch.geq_mode_active);
    v.set("geq_bypass", ch.geq_bypass);
    JsonVal geq_arr = JsonVal::make_array();
    for (float g : ch.geq.get_gains()) {
        geq_arr.push_back(g);
    }
    v.set("geq_gains", geq_arr);

    // Dynamics (Compressor)
    JsonVal comp = JsonVal::make_object();
    comp.set("bypass", ch.comp_bypass);
    comp.set("threshold", ch.comp_thresh);
    comp.set("ratio", ch.comp_ratio);
    comp.set("attack", ch.comp_attack);
    comp.set("release", ch.comp_release);
    comp.set("knee", ch.comp_knee);
    comp.set("makeup", ch.comp_makeup);
    v.set("compressor", comp);

    // Limiter
    JsonVal lim = JsonVal::make_object();
    lim.set("enabled", ch.limiter_enabled);
    lim.set("ceiling_db", ch.limiter_ceiling_db);
    lim.set("release_ms", ch.limiter_release_ms);
    v.set("limiter", lim);

    // Gate
    JsonVal gate = JsonVal::make_object();
    gate.set("on", ch.gate_on);
    gate.set("threshold", ch.gate_thresh);
    gate.set("range", ch.gate_range);
    gate.set("attack", ch.gate_attack);
    gate.set("hold", ch.gate_hold);
    gate.set("release", ch.gate_release);
    v.set("gate", gate);

    // AUX Sends
    JsonVal sends_arr = JsonVal::make_array();
    for (size_t a = 0; a < 4; ++a) {
        JsonVal s = JsonVal::make_object();
        s.set("level", ch.sends[a]);
        s.set("enabled", ch.send_enabled[a]);
        s.set("pre_fader", ch.send_pre_fader[a]);
        sends_arr.push_back(s);
    }
    v.set("sends", sends_arr);

    // FX Rack
    JsonVal fx_arr = JsonVal::make_array();
    for (size_t s = 0; s < ch.fx_rack.count(); ++s) {
        const auto* fx = ch.fx_rack.get_slot(s);
        if (!fx) continue;
        JsonVal slot = JsonVal::make_object();
        slot.set("uri", fx->get_uri());
        slot.set("bypassed", fx->is_bypassed());
        slot.set("enabled", fx->is_enabled());

        JsonVal params = JsonVal::make_object();
        for (size_t p = 0; p < fx->parameter_count(); ++p) {
            const auto* param = fx->get_parameter(p);
            if (param) params.set(param->id, param->current_val);
        }
        slot.set("parameters", params);
        fx_arr.push_back(slot);
    }
    v.set("fx_rack", fx_arr);

    return v;
}

void deserialize_strip(const JsonVal& v, ChannelState& ch) {
    ch.id = v.get_int("id", ch.id);
    ch.format = v.get_int("format", 0) == 1 ? ChannelFormat::Stereo : ChannelFormat::Mono;
    
    std::string name = v.get_string("name", ch.name);
    std::snprintf(ch.name, sizeof(ch.name), "%s", name.c_str());

    std::string in_label = v.get_string("input_label", ch.input_label);
    std::snprintf(ch.input_label, sizeof(ch.input_label), "%s", in_label.c_str());

    ch.input_source_index = v.get_int("input_source_index", -1);
    ch.input_source_index_r = v.get_int("input_source_index_r", -1);
    ch.fader = std::clamp(v.get_float("fader", 0.86f), 0.0f, 1.0f);
    ch.return_gain = std::clamp(v.get_float("return_gain", 0.86f), 0.0f, 1.0f);
    ch.gain = std::clamp(v.get_float("gain", 0.5f), 0.0f, 1.0f);
    ch.pan = std::clamp(v.get_float("pan", 0.5f), 0.0f, 1.0f);
    ch.mute = v.get_bool("mute", false);
    ch.solo = v.get_bool("solo", false);
    ch.route_to_main = v.get_bool("route_to_main", true);
    ch.output_route = static_cast<ChannelOutputRoute>(v.get_int("output_route", static_cast<int>(ChannelOutputRoute::MainLR)));
    ch.output_route_index = v.get_int("output_route_index", 0);
    ch.polarity_inverted = v.get_bool("polarity_inverted", false);
    ch.hpf_on = v.get_bool("hpf_on", true);
    ch.hpf_frequency = std::clamp(v.get_float("hpf_frequency", 80.0f), 20.0f, 20000.0f);

    // Parametric EQ
    ch.eq_bypass = v.get_bool("eq_bypass", false);
    const auto& eq_arr = v.get("eq_bands");
    if (eq_arr.is_array()) {
        for (size_t b = 0; b < 4 && b < eq_arr.size(); ++b) {
            const auto& b_obj = eq_arr[b];
            ch.eq_freq[b] = b_obj.get_float("freq", ch.eq_freq[b]);
            ch.eq_gain[b] = b_obj.get_float("gain", ch.eq_gain[b]);
            ch.eq_q[b] = b_obj.get_float("q", ch.eq_q[b]);
            ch.eq_enabled[b] = b_obj.get_bool("enabled", true);
            ch.eq_filter_type[b] = b_obj.get_int("filter_type", ch.eq_filter_type[b]);
            ch.eq_shelf[b] = (ch.eq_filter_type[b] == 0 || ch.eq_filter_type[b] == 2);
        }
    }

    // Graphic EQ
    ch.geq_mode_active = v.get_bool("geq_mode_active", false);
    ch.geq_bypass = v.get_bool("geq_bypass", false);
    const auto& geq_arr = v.get("geq_gains");
    if (geq_arr.is_array()) {
        std::array<float, GraphicEqModel::NUM_BANDS> gains{};
        for (size_t i = 0; i < GraphicEqModel::NUM_BANDS && i < geq_arr.size(); ++i) {
            gains[i] = geq_arr[i].as_float(0.0f);
        }
        ch.geq.set_gains(gains);
    }
    ch.geq.set_bypass(ch.geq_bypass);

    // Compressor
    const auto& comp = v.get("compressor");
    if (comp.is_object()) {
        ch.comp_bypass = comp.get_bool("bypass", false);
        ch.comp_thresh = comp.get_float("threshold", -18.0f);
        ch.comp_ratio = comp.get_float("ratio", 4.0f);
        ch.comp_attack = comp.get_float("attack", 10.0f);
        ch.comp_release = comp.get_float("release", 80.0f);
        ch.comp_knee = comp.get_float("knee", 6.0f);
        ch.comp_makeup = comp.get_float("makeup", 0.0f);
    }

    // Limiter
    const auto& lim = v.get("limiter");
    if (lim.is_object()) {
        ch.limiter_enabled = lim.get_bool("enabled", false);
        ch.limiter_ceiling_db = lim.get_float("ceiling_db", -0.1f);
        ch.limiter_release_ms = lim.get_float("release_ms", 50.0f);
    }

    // Gate
    const auto& gate = v.get("gate");
    if (gate.is_object()) {
        ch.gate_on = gate.get_bool("on", true);
        ch.gate_thresh = gate.get_float("threshold", -48.0f);
        ch.gate_range = gate.get_float("range", -60.0f);
        ch.gate_attack = gate.get_float("attack", 5.0f);
        ch.gate_hold = gate.get_float("hold", 50.0f);
        ch.gate_release = gate.get_float("release", 120.0f);
    }

    // AUX Sends
    const auto& sends_arr = v.get("sends");
    if (sends_arr.is_array()) {
        for (size_t a = 0; a < 4 && a < sends_arr.size(); ++a) {
            const auto& s = sends_arr[a];
            ch.sends[a] = std::clamp(s.get_float("level", 0.0f), 0.0f, 1.0f);
            ch.send_enabled[a] = s.get_bool("enabled", true);
            ch.send_pre_fader[a] = s.get_bool("pre_fader", false);
        }
    }

    // FX Rack
    ch.fx_rack.clear();
    const auto& fx_arr = v.get("fx_rack");
    if (fx_arr.is_array()) {
        for (size_t s = 0; s < fx_arr.size(); ++s) {
            const auto& slot = fx_arr[s];
            std::string uri = slot.get_string("uri");
            if (uri.empty()) continue;
            if (ch.fx_rack.add_plugin(uri)) {
                auto* fx = ch.fx_rack.get_slot(ch.fx_rack.count() - 1);
                if (fx) {
                    fx->set_bypassed(slot.get_bool("bypassed", false));
                    fx->set_enabled(slot.get_bool("enabled", true));
                    const auto& params = slot.get("parameters");
                    if (params.is_object()) {
                        for (const auto& p : params.obj_val) {
                            fx->set_parameter_value(p.first, p.second.as_float());
                        }
                    }
                }
            }
        }
    }
}

} // namespace

std::string serialize_mixer_state_to_json() {
    JsonVal root = JsonVal::make_object();
    root.set("app", "PRISMIX");
    root.set("schema_version", CURRENT_PRESET_SCHEMA_VERSION);

    // Channels
    JsonVal ch_arr = JsonVal::make_array();
    for (const auto& ch : g_mixer_state.channels) {
        ch_arr.push_back(serialize_strip(ch));
    }
    root.set("channels", ch_arr);

    // Master
    root.set("master", serialize_strip(g_mixer_state.master));

    // Aux Buses
    JsonVal aux_arr = JsonVal::make_array();
    for (const auto& aux : g_mixer_state.aux_buses) {
        aux_arr.push_back(serialize_strip(aux));
    }
    root.set("aux_buses", aux_arr);

    return root.dump(2);
}

bool deserialize_json_to_mixer_state(const std::string& json_str, std::string* error) {
    if (json_str.empty()) {
        if (error) *error = "JSON string is empty.";
        return false;
    }

    JsonVal root = Prismix::Json::parse(json_str);
    if (!root.is_object()) {
        if (error) *error = "Invalid JSON root object.";
        return false;
    }

    // Check if configuration is nested inside a "config" wrapper (from Supabase row)
    const JsonVal& config = root.has("config") ? root.get("config") : root;
    if (!config.is_object()) {
        if (error) *error = "Missing scene config object.";
        return false;
    }

    // Restore channels in-place to avoid invalidating pointer bindings used by UI timers
    const auto& ch_arr = config.get("channels");
    if (ch_arr.is_array()) {
        for (size_t i = 0; i < ch_arr.size(); ++i) {
            if (i < g_mixer_state.channels.size()) {
                // In-place update existing channel without altering pointer addresses
                deserialize_strip(ch_arr[i], g_mixer_state.channels[i]);
            } else {
                ChannelState new_ch{};
                deserialize_strip(ch_arr[i], new_ch);
                g_mixer_state.channels.push_back(new_ch);
            }

            const auto& ch = g_mixer_state.channels[i];
            if (ch.id >= 1 && ch.id <= static_cast<int>(RoutingMatrix::NUM_CHANNELS)) {
                size_t ch_idx = static_cast<size_t>(ch.id - 1);
                g_routing_matrix.set_channel_input_source(ch_idx, ch.input_source_index);
                g_routing_matrix.set_channel_main_lr(ch_idx, ch.output_route == ChannelOutputRoute::MainLR);
            }
        }

        // Trim excess channels if preset has fewer channels than current state
        while (g_mixer_state.channels.size() > ch_arr.size()) {
            g_mixer_state.channels.pop_back();
        }
    }

    // Restore master
    if (config.has("master")) {
        deserialize_strip(config.get("master"), g_mixer_state.master);
        g_mixer_state.master.id = 0;
        g_mixer_state.master.format = ChannelFormat::Stereo;
    }

    // Restore aux buses
    const auto& aux_arr = config.get("aux_buses");
    if (aux_arr.is_array()) {
        for (size_t a = 0; a < 4 && a < aux_arr.size(); ++a) {
            deserialize_strip(aux_arr[a], g_mixer_state.aux_buses[a]);
            g_mixer_state.aux_buses[a].id = 101 + static_cast<int>(a);
        }
    }

    // Synchronize to live Audio DSP engine if active
    const auto audio = audio_service().snapshot();
    if (audio.mode == AudioMode::RealAudio) {
        for (const auto& ch : g_mixer_state.channels) {
            const float faderVal = std::clamp(ch.fader, 0.0f, 1.0f);
            const float faderDb = faderVal <= 0.86f
                ? -60.0f + faderVal * (60.0f / 0.86f)
                : (faderVal - 0.86f) * (10.0f / 0.14f);
            const float gainKnobDb = (std::clamp(ch.gain, 0.0f, 1.0f) - 0.5f) * 24.0f;
            const float totalGainDb = std::clamp(faderDb + gainKnobDb, -60.0f, 12.0f);
            const float panVal = std::clamp((ch.pan - 0.5f) * 2.0f, -1.0f, 1.0f);

            audio_service().updateLiveStrip(static_cast<uint32_t>(ch.id), totalGainDb, panVal, ch.mute, ch.solo);
            auto stripCfg = ui_make_strip_config_for_channel(ch);
            if (ch.id >= 1 && ch.id <= static_cast<int>(RoutingMatrix::NUM_CHANNELS)) {
                stripCfg.routeToMain = g_routing_matrix.get_channel_main_lr(static_cast<size_t>(ch.id - 1));
            }
            audio_service().updateLiveStripDsp(stripCfg);
        }
    }

    return true;
}

} // namespace Cloud
} // namespace Prismix

