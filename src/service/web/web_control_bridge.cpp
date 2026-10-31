#include "web_control_bridge.h"
#include "../../model/mixer_state.h"
#include "../../model/routing_state.h"
#include "../../model/mixer_scene.h"
#include "../../model/plugin_model.h"
#include "../../model/pedalboard_preset.h"
#include "../../service/audio_service.h"
#include "../../service/patchstorage/patchstorage_service.h"
#include "../../service/security/security_service.h"
#include "../../backend/mock_audio_backend.h"
#include "../../ui/ui_audio_bridge.h"
#include "../../ui/theme/theme_model.h"

#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <cctype>
#include <cstdio>

namespace prismix::web {

namespace {

std::string escape_json(const std::string& s) {
    std::ostringstream o;
    for (char c : s) {
        switch (c) {
            case '"': o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\b': o << "\\b"; break;
            case '\f': o << "\\f"; break;
            case '\n': o << "\\n"; break;
            case '\r': o << "\\r"; break;
            case '\t': o << "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) <= 0x1f) {
                    o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                } else {
                    o << c;
                }
        }
    }
    return o.str();
}

bool is_safe_name(const std::string& name) {
    if (name.empty() || name.length() > 64) return false;
    if (name.find("..") != std::string::npos) return false;
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) return false;
    for (char c : name) {
        if (static_cast<unsigned char>(c) < 32 || c == 127) return false;
    }
    return true;
}

bool has_key(const std::string& json, const std::string& key) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return false;
    pos = json.find(':', pos + pattern.length());
    return pos != std::string::npos;
}

std::string extract_string(const std::string& json, const std::string& key) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + pattern.length());
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return "";
    size_t end = pos + 1;
    std::string out;
    while (end < json.length()) {
        if (json[end] == '\\' && end + 1 < json.length()) {
            out += json[end + 1];
            end += 2;
            continue;
        }
        if (json[end] == '"') break;
        out += json[end];
        end++;
    }
    return out;
}

float extract_float(const std::string& json, const std::string& key, float def = 0.0f) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return def;
    pos = json.find(':', pos + pattern.length());
    if (pos == std::string::npos) return def;
    while (pos < json.length() && (json[pos] == ':' || json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) pos++;
    try {
        return std::stof(json.substr(pos));
    } catch (...) {
        return def;
    }
}

int extract_int(const std::string& json, const std::string& key, int def = 0) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return def;
    pos = json.find(':', pos + pattern.length());
    if (pos == std::string::npos) return def;
    while (pos < json.length() && (json[pos] == ':' || json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) pos++;
    try {
        return std::stoi(json.substr(pos));
    } catch (...) {
        return def;
    }
}

bool extract_bool(const std::string& json, const std::string& key, bool def = false) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return def;
    pos = json.find(':', pos + pattern.length());
    if (pos == std::string::npos) return def;
    size_t tpos = json.find("true", pos);
    if (tpos != std::string::npos && tpos < pos + 12) return true;
    size_t fpos = json.find("false", pos);
    if (fpos != std::string::npos && fpos < pos + 12) return false;
    size_t one_pos = json.find('1', pos);
    if (one_pos != std::string::npos && one_pos < pos + 6) return true;
    return def;
}

ChannelState* resolve_channel(int id) {
    if (id == 0 || id == -1 || id == 100) return &g_mixer_state.master;
    if (id >= 101 && id <= 104) return mixer_get_aux_bus(id - 101);
    return mixer_find_channel_by_id(id);
}

float fader_to_db(float fader) {
    fader = std::clamp(fader, 0.0f, 1.0f);
    return (fader <= 0.86f)
        ? -60.0f + fader * (60.0f / 0.86f)
        : (fader - 0.86f) * (10.0f / 0.14f);
}

void sync_channel_to_dsp(ChannelState* ch) {
    if (!ch) return;
    if (isMasterChannel(ch)) {
        const float masterDb = fader_to_db(ch->fader);
        audio_service().setGainDb(masterDb);
        audio_service().setPan((ch->pan - 0.5f) * 2.0f);
        auto masterCfg = ui_make_strip_config_for_channel(*ch);
        masterCfg.format = AudioStripFormat::Stereo;
        masterCfg.routeToMain = true;
        audio_service().updateLiveMasterBusDsp(masterCfg);
    } else if (isAuxBusChannel(ch)) {
        ui_sync_channel_fx_to_dsp(ch);
    } else {
        const float faderDb = fader_to_db(ch->fader);
        const float gainKnobDb = (std::clamp(ch->gain, 0.0f, 1.0f) - 0.5f) * 24.0f;
        const float totalGainDb = std::clamp(faderDb + gainKnobDb, -60.0f, 12.0f);
        audio_service().updateLiveStrip(static_cast<uint32_t>(ch->id), totalGainDb, (ch->pan - 0.5f) * 2.0f, ch->mute, ch->solo);
        ui_sync_channel_fx_to_dsp(ch);
    }
}

void write_float_array(std::ostringstream& ss, const float* arr, size_t count) {
    ss << "[";
    for (size_t i = 0; i < count; ++i) {
        if (i > 0) ss << ",";
        float v = std::isfinite(arr[i]) ? arr[i] : -90.0f;
        ss << v;
    }
    ss << "]";
}

void write_bool_array(std::ostringstream& ss, const bool* arr, size_t count) {
    ss << "[";
    for (size_t i = 0; i < count; ++i) {
        if (i > 0) ss << ",";
        ss << (arr[i] ? "true" : "false");
    }
    ss << "]";
}

void write_int_array(std::ostringstream& ss, const int* arr, size_t count) {
    ss << "[";
    for (size_t i = 0; i < count; ++i) {
        if (i > 0) ss << ",";
        ss << arr[i];
    }
    ss << "]";
}

void write_channel_json(std::ostringstream& ss, const ChannelState& ch, int list_index, bool include_meters, const char* indent = "    ", bool leading_indent = true) {
    const auto caps = mixer_get_channel_capabilities(&ch);
    ss << (leading_indent ? indent : "") << "{\n";
    ss << indent << "  \"index\": " << list_index << ",\n";
    ss << indent << "  \"id\": " << ch.id << ",\n";
    ss << indent << "  \"name\": \"" << escape_json(ch.name) << "\",\n";
    ss << indent << "  \"input_label\": \"" << escape_json(ch.input_label) << "\",\n";
    ss << indent << "  \"type\": " << static_cast<int>(ch.type) << ",\n";
    ss << indent << "  \"format\": " << static_cast<int>(ch.format) << ",\n";
    ss << indent << "  \"input_source_index\": " << ch.input_source_index << ",\n";
    ss << indent << "  \"input_source_index_r\": " << ch.input_source_index_r << ",\n";
    ss << indent << "  \"output_route\": " << static_cast<int>(ch.output_route) << ",\n";
    ss << indent << "  \"output_route_index\": " << ch.output_route_index << ",\n";
    ss << indent << "  \"route_to_main\": " << (ch.route_to_main ? "true" : "false") << ",\n";
    ss << indent << "  \"fader\": " << ch.fader << ",\n";
    ss << indent << "  \"return_gain\": " << ch.return_gain << ",\n";
    ss << indent << "  \"pan\": " << ch.pan << ",\n";
    ss << indent << "  \"gain\": " << ch.gain << ",\n";
    ss << indent << "  \"mute\": " << (ch.mute ? "true" : "false") << ",\n";
    ss << indent << "  \"solo\": " << (ch.solo ? "true" : "false") << ",\n";
    ss << indent << "  \"select\": " << (ch.select ? "true" : "false") << ",\n";
    ss << indent << "  \"polarity_inverted\": " << (ch.polarity_inverted ? "true" : "false") << ",\n";
    ss << indent << "  \"phantom_on\": " << (ch.phantom_on ? "true" : "false") << ",\n";
    ss << indent << "  \"hpf_on\": " << (ch.hpf_on ? "true" : "false") << ",\n";
    ss << indent << "  \"hpf_frequency\": " << ch.hpf_frequency << ",\n";

    // Sends
    ss << indent << "  \"sends\": "; write_float_array(ss, ch.sends.data(), 4); ss << ",\n";
    ss << indent << "  \"send_enabled\": "; write_bool_array(ss, ch.send_enabled.data(), 4); ss << ",\n";
    ss << indent << "  \"send_pre_fader\": "; write_bool_array(ss, ch.send_pre_fader.data(), 4); ss << ",\n";

    // PEQ
    ss << indent << "  \"eq_bypass\": " << (ch.eq_bypass ? "true" : "false") << ",\n";
    ss << indent << "  \"eq_freq\": "; write_float_array(ss, ch.eq_freq.data(), 4); ss << ",\n";
    ss << indent << "  \"eq_gain\": "; write_float_array(ss, ch.eq_gain.data(), 4); ss << ",\n";
    ss << indent << "  \"eq_q\": "; write_float_array(ss, ch.eq_q.data(), 4); ss << ",\n";
    ss << indent << "  \"eq_enabled\": "; write_bool_array(ss, ch.eq_enabled.data(), 4); ss << ",\n";
    ss << indent << "  \"eq_filter_type\": "; write_int_array(ss, ch.eq_filter_type.data(), 4); ss << ",\n";

    // GEQ
    ss << indent << "  \"geq_mode_active\": " << (ch.geq_mode_active ? "true" : "false") << ",\n";
    ss << indent << "  \"geq_bypass\": " << (ch.geq_bypass ? "true" : "false") << ",\n";
    ss << indent << "  \"geq_gains\": "; write_float_array(ss, ch.geq.get_gains().data(), 31); ss << ",\n";

    // Gate
    ss << indent << "  \"gate_on\": " << (ch.gate_on ? "true" : "false") << ",\n";
    ss << indent << "  \"gate_open\": " << (ch.gate_open ? "true" : "false") << ",\n";
    ss << indent << "  \"gate_state\": " << ch.gate_state << ",\n";
    ss << indent << "  \"gate_thresh\": " << ch.gate_thresh << ",\n";
    ss << indent << "  \"gate_range\": " << ch.gate_range << ",\n";
    ss << indent << "  \"gate_attack\": " << ch.gate_attack << ",\n";
    ss << indent << "  \"gate_hold\": " << ch.gate_hold << ",\n";
    ss << indent << "  \"gate_release\": " << ch.gate_release << ",\n";
    ss << indent << "  \"gate_key_source\": " << ch.gate_key_source << ",\n";
    ss << indent << "  \"gate_gr_db\": " << ch.gate_gr_db << ",\n";
    ss << indent << "  \"gate_detector_db\": " << ch.gate_detector_db << ",\n";

    // Compressor
    ss << indent << "  \"comp_bypass\": " << (ch.comp_bypass ? "true" : "false") << ",\n";
    ss << indent << "  \"comp_thresh\": " << ch.comp_thresh << ",\n";
    ss << indent << "  \"comp_ratio\": " << ch.comp_ratio << ",\n";
    ss << indent << "  \"comp_attack\": " << ch.comp_attack << ",\n";
    ss << indent << "  \"comp_release\": " << ch.comp_release << ",\n";
    ss << indent << "  \"comp_knee\": " << ch.comp_knee << ",\n";
    ss << indent << "  \"comp_makeup\": " << ch.comp_makeup << ",\n";
    ss << indent << "  \"comp_gr\": " << ch.comp_gr << ",\n";
    ss << indent << "  \"comp_input\": " << ch.comp_input << ",\n";
    ss << indent << "  \"comp_output\": " << ch.comp_output << ",\n";

    // Limiter
    ss << indent << "  \"limiter_enabled\": " << (ch.limiter_enabled ? "true" : "false") << ",\n";
    ss << indent << "  \"limiter_ceiling_db\": " << ch.limiter_ceiling_db << ",\n";
    ss << indent << "  \"limiter_release_ms\": " << ch.limiter_release_ms << ",\n";
    ss << indent << "  \"limiter_gr\": " << ch.limiter_gr_db << ",\n";
    ss << indent << "  \"limiter_gr_db\": " << ch.limiter_gr_db << ",\n";

    // Sidechain & Speaker Delay
    ss << indent << "  \"sidechain_on\": " << (ch.sidechain_on ? "true" : "false") << ",\n";
    ss << indent << "  \"sidechain_low\": " << ch.sidechain_low << ",\n";
    ss << indent << "  \"sidechain_high\": " << ch.sidechain_high << ",\n";
    ss << indent << "  \"speaker_delay_enabled\": " << (ch.speaker_delay_enabled ? "true" : "false") << ",\n";
    ss << indent << "  \"speaker_delay_link\": " << (ch.speaker_delay_link ? "true" : "false") << ",\n";
    ss << indent << "  \"speaker_delay_l_ms\": " << ch.speaker_delay_l_ms << ",\n";
    ss << indent << "  \"speaker_delay_r_ms\": " << ch.speaker_delay_r_ms << ",\n";

    // Capabilities
    ss << indent << "  \"capabilities\": {"
       << "\"has_preamp\":" << (caps.has_preamp ? "true" : "false") << ","
       << "\"has_hpf\":" << (caps.has_hpf ? "true" : "false") << ","
       << "\"has_gate\":" << (caps.has_gate ? "true" : "false") << ","
       << "\"has_peq\":" << (caps.has_peq ? "true" : "false") << ","
       << "\"has_geq\":" << (caps.has_geq ? "true" : "false") << ","
       << "\"has_comp\":" << (caps.has_comp ? "true" : "false") << ","
       << "\"has_limiter\":" << (caps.has_limiter ? "true" : "false") << ","
       << "\"has_fx_rack\":" << (caps.has_fx_rack ? "true" : "false") << ","
       << "\"has_aux_sends\":" << (caps.has_aux_sends ? "true" : "false") << ","
       << "\"has_bus_assign\":" << (caps.has_bus_assign ? "true" : "false") << ","
       << "\"has_speaker_delay\":" << (caps.has_speaker_delay ? "true" : "false") << ","
       << "\"is_master\":" << (caps.is_master ? "true" : "false") << ","
       << "\"is_aux\":" << (caps.is_aux ? "true" : "false") << ","
       << "\"is_stereo\":" << (caps.is_stereo ? "true" : "false") << "},\n";

    if (include_meters) {
        ss << indent << "  \"peak_l\": " << ch.peak_l << ",\n";
        ss << indent << "  \"peak_r\": " << ch.peak_r << ",\n";
        ss << indent << "  \"rms_l\": " << ch.rms_l << ",\n";
        ss << indent << "  \"rms_r\": " << ch.rms_r << ",\n";
        ss << indent << "  \"peak_hold_l\": " << ch.peak_hold_l << ",\n";
        ss << indent << "  \"peak_hold_r\": " << ch.peak_hold_r << ",\n";
        ss << indent << "  \"input_peak_l\": " << ch.input_peak_l << ",\n";
        ss << indent << "  \"input_peak_r\": " << ch.input_peak_r << ",\n";
        ss << indent << "  \"fx_in_peak_l\": " << ch.fx_in_peak_l << ",\n";
        ss << indent << "  \"fx_in_peak_r\": " << ch.fx_in_peak_r << ",\n";
        ss << indent << "  \"fx_out_peak_l\": " << ch.fx_out_peak_l << ",\n";
        ss << indent << "  \"fx_out_peak_r\": " << ch.fx_out_peak_r << ",\n";
        ss << indent << "  \"post_fader_peak_l\": " << ch.post_fader_peak_l << ",\n";
        ss << indent << "  \"post_fader_peak_r\": " << ch.post_fader_peak_r << ",\n";
        ss << indent << "  \"pre_limiter_peak_l\": " << ch.pre_limiter_peak_l << ",\n";
        ss << indent << "  \"pre_limiter_peak_r\": " << ch.pre_limiter_peak_r << ",\n";
        ss << indent << "  \"clip\": " << (ch.clip ? "true" : "false") << ",\n";
        ss << indent << "  \"input_clip\": " << (ch.input_clip ? "true" : "false") << ",\n";
    } else {
        ss << indent << "  \"peak_l\": -90.0,\n";
        ss << indent << "  \"peak_r\": -90.0,\n";
    }

    ss << indent << "  \"fx_rack_count\": " << ch.fx_rack.count() << ",\n";
    ss << indent << "  \"fx_rack\": [\n";
    for (size_t s = 0; s < ch.fx_rack.count(); ++s) {
        const auto* inst = ch.fx_rack.get_slot(s);
        if (!inst) continue;
        if (s > 0) ss << ",\n";
        ss << indent << "    {\n";
        ss << indent << "      \"slot\": " << s << ",\n";
        ss << indent << "      \"uri\": \"" << escape_json(inst->get_uri()) << "\",\n";
        ss << indent << "      \"name\": \"" << escape_json(inst->get_name()) << "\",\n";
        ss << indent << "      \"category\": \"" << escape_json(inst->get_category()) << "\",\n";
        ss << indent << "      \"source_type\": " << static_cast<int>(inst->get_source_type()) << ",\n";
        ss << indent << "      \"bypassed\": " << (inst->is_bypassed() ? "true" : "false") << ",\n";
        ss << indent << "      \"enabled\": " << (inst->is_enabled() ? "true" : "false") << ",\n";
        ss << indent << "      \"gr_db\": " << (s < 8 ? ch.fx_slot_gr_db[s] : 0.0f) << ",\n";
        ss << indent << "      \"limiter_gr_db\": "
           << (s < 8 ? ch.fx_slot_limiter_gr_db[s] : LIMITER_GR_UNAVAILABLE) << ",\n";
        ss << indent << "      \"parameters\": [\n";
        for (size_t p = 0; p < inst->parameter_count(); ++p) {
            const auto* param = inst->get_parameter(p);
            if (!param) continue;
            if (p > 0) ss << ",\n";
            ss << indent << "        {\n";
            ss << indent << "          \"id\": \"" << escape_json(param->id) << "\",\n";
            ss << indent << "          \"name\": \"" << escape_json(param->name) << "\",\n";
            ss << indent << "          \"unit\": \"" << escape_json(param->unit) << "\",\n";
            ss << indent << "          \"value\": " << param->current_val << ",\n";
            ss << indent << "          \"min\": " << param->min_val << ",\n";
            ss << indent << "          \"max\": " << param->max_val << ",\n";
            ss << indent << "          \"default\": " << param->default_val << ",\n";
            ss << indent << "          \"step\": " << param->step << ",\n";
            ss << indent << "          \"is_toggle\": " << (param->is_toggle ? "true" : "false") << "\n";
            ss << indent << "        }";
        }
        ss << "\n" << indent << "      ]\n";
        ss << indent << "    }";
    }
    ss << "\n" << indent << "  ]\n";
    ss << indent << "}";
}

} // anonymous namespace

WebControlBridge& WebControlBridge::instance() {
    static WebControlBridge s_instance;
    return s_instance;
}

WebControlBridge::WebControlBridge() = default;

uint64_t WebControlBridge::get_state_version() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_version_;
}

void WebControlBridge::bump_state_version() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_version_++;
    if (change_cb_) {
        change_cb_(state_version_);
    }
}

void WebControlBridge::set_state_change_callback(StateChangeCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    change_cb_ = std::move(cb);
}

std::string WebControlBridge::get_state_json(bool include_meters) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(4);

    const ChannelState* sel_ch = mixer_get_selected_channel();
    int selected_id = sel_ch ? sel_ch->id : 0;

    ss << "{\n";
    ss << "  \"version\": " << state_version_ << ",\n";
    ss << "  \"selected_channel\": " << g_mixer_state.selected_channel << ",\n";
    ss << "  \"selected_aux_bus\": " << g_mixer_state.selected_aux_bus << ",\n";
    ss << "  \"selected_id\": " << selected_id << ",\n";

    // Input channels array
    ss << "  \"channels\": [\n";
    for (size_t i = 0; i < g_mixer_state.channels.size(); ++i) {
        const auto* ch = g_mixer_state.channels.get(i);
        if (!ch) continue;
        if (i > 0) ss << ",\n";
        write_channel_json(ss, *ch, static_cast<int>(i), include_meters, "    ");
    }
    ss << "\n  ],\n";

    // AUX buses array
    ss << "  \"aux_buses\": [\n";
    for (size_t a = 0; a < 4; ++a) {
        if (a > 0) ss << ",\n";
        write_channel_json(ss, g_mixer_state.aux_buses[a], static_cast<int>(a), include_meters, "    ");
    }
    ss << "\n  ],\n";

    // Master bus
    ss << "  \"master\": ";
    write_channel_json(ss, g_mixer_state.master, -1, include_meters, "  ", false);
    ss << ",\n";

    // Routing Matrix state
    ss << "  \"routing\": {\n";
    ss << "    \"input_sources\": [";
    for (size_t i = 0; i < RoutingMatrix::NUM_INPUTS; ++i) {
        if (i > 0) ss << ",";
        ss << "\"" << escape_json(RoutingMatrix::get_input_source_name(static_cast<int>(i))) << "\"";
    }
    ss << "],\n";
    ss << "    \"internal_buses\": [";
    for (size_t i = 0; i < RoutingMatrix::NUM_BUSES; ++i) {
        if (i > 0) ss << ",";
        ss << "\"" << escape_json(RoutingMatrix::get_internal_bus_name(i)) << "\"";
    }
    ss << "],\n";
    ss << "    \"output_destinations\": [";
    for (size_t i = 0; i < RoutingMatrix::NUM_OUTPUTS; ++i) {
        if (i > 0) ss << ",";
        ss << "\"" << escape_json(RoutingMatrix::get_output_destination_name(i)) << "\"";
    }
    ss << "],\n";
    ss << "    \"channel_inputs\": [";
    for (size_t c = 0; c < RoutingMatrix::NUM_CHANNELS; ++c) {
        if (c > 0) ss << ",";
        ss << g_routing_matrix.get_channel_input_source(c);
    }
    ss << "],\n";
    ss << "    \"channel_main_lr\": [";
    for (size_t c = 0; c < RoutingMatrix::NUM_CHANNELS; ++c) {
        if (c > 0) ss << ",";
        ss << (g_routing_matrix.get_channel_main_lr(c) ? "true" : "false");
    }
    ss << "],\n";
    ss << "    \"channel_bus_matrix\": [\n";
    for (size_t c = 0; c < RoutingMatrix::NUM_CHANNELS; ++c) {
        if (c > 0) ss << ",\n";
        ss << "      [";
        for (size_t b = 0; b < RoutingMatrix::NUM_BUSES; ++b) {
            if (b > 0) ss << ",";
            ss << (g_routing_matrix.get_channel_bus(c, b) ? "true" : "false");
        }
        ss << "]";
    }
    ss << "\n    ],\n";
    ss << "    \"bus_main_lr\": [";
    for (size_t b = 0; b < RoutingMatrix::NUM_BUSES; ++b) {
        if (b > 0) ss << ",";
        ss << (g_routing_matrix.get_bus_main_lr(b) ? "true" : "false");
    }
    ss << "],\n";
    ss << "    \"output_patches\": [";
    for (size_t o = 0; o < RoutingMatrix::NUM_OUTPUTS; ++o) {
        if (o > 0) ss << ",";
        ss << g_routing_matrix.get_output_patch(o);
    }
    ss << "]\n";
    ss << "  },\n";

    // System & Audio Service state
    const auto audio_snap = audio_service().snapshot();
    const auto mock_cfg = g_backend.get_config();
    const auto& analyzer = g_backend.get_analyzer();
    const auto& theme_cfg = theme_get_config();
    const auto active_colors = theme_get_active_colors();

    ss << "  \"system\": {\n";
    ss << "    \"audio_mode\": " << static_cast<int>(audio_snap.mode) << ",\n";
    ss << "    \"service_state\": " << static_cast<int>(audio_snap.state) << ",\n";
    ss << "    \"service_state_name\": \"" << escape_json(audio_service_state_name(audio_snap.state)) << "\",\n";
    ss << "    \"sim_running\": " << (g_backend.is_running() ? "true" : "false") << ",\n";
    ss << "    \"generator_signal\": " << static_cast<int>(g_backend.get_generator_signal()) << ",\n";
    ss << "    \"generator_signal_name\": \"" << escape_json(g_backend.get_generator_signal_name()) << "\",\n";
    ss << "    \"requested_sample_rate\": " << audio_service().getRequestedSampleRate() << ",\n";
    ss << "    \"requested_buffer_frames\": " << audio_service().getRequestedBufferFrames() << ",\n";
    ss << "    \"actual_sample_rate\": " << (audio_snap.actualSampleRate > 0 ? audio_snap.actualSampleRate : static_cast<double>(mock_cfg.sample_rate)) << ",\n";
    ss << "    \"actual_buffer_frames\": " << (audio_snap.actualBufferFrames > 0 ? audio_snap.actualBufferFrames : static_cast<uint32_t>(mock_cfg.buffer_size)) << ",\n";
    ss << "    \"cpu_load\": " << mock_cfg.cpu_load << ",\n";
    ss << "    \"dsp_time_us\": " << audio_snap.dspTimeUs << ",\n";
    ss << "    \"underruns\": " << (audio_snap.mode == AudioMode::RealAudio ? audio_snap.underruns : mock_cfg.xruns) << ",\n";
    ss << "    \"capture_device_id\": \"" << escape_json(audio_snap.captureDeviceId) << "\",\n";
    ss << "    \"render_device_id\": \"" << escape_json(audio_snap.renderDeviceId) << "\",\n";
    ss << "    \"actual_capture_device\": \"" << escape_json(audio_snap.actualCaptureDevice.empty() ? mock_cfg.input_device : audio_snap.actualCaptureDevice) << "\",\n";
    ss << "    \"actual_render_device\": \"" << escape_json(audio_snap.actualRenderDevice.empty() ? mock_cfg.output_device : audio_snap.actualRenderDevice) << "\",\n";
    ss << "    \"error\": \"" << escape_json(audio_snap.error) << "\",\n";
    ss << "    \"capture_devices\": [";
    for (size_t i = 0; i < audio_snap.captureDevices.size(); ++i) {
        const auto& d = audio_snap.captureDevices[i];
        if (i > 0) ss << ",";
        ss << "{\"id\":\"" << escape_json(d.id) << "\",\"name\":\"" << escape_json(d.name) << "\",\"channels\":" << d.channels << ",\"is_default\":" << (d.isDefault ? "true" : "false") << "}";
    }
    ss << "],\n";
    ss << "    \"render_devices\": [";
    for (size_t i = 0; i < audio_snap.renderDevices.size(); ++i) {
        const auto& d = audio_snap.renderDevices[i];
        if (i > 0) ss << ",";
        ss << "{\"id\":\"" << escape_json(d.id) << "\",\"name\":\"" << escape_json(d.name) << "\",\"channels\":" << d.channels << ",\"is_default\":" << (d.isDefault ? "true" : "false") << "}";
    }
    ss << "],\n";
    ss << "    \"rta_mode\": " << static_cast<int>(analyzer.get_mode()) << ",\n";
    ss << "    \"rta_source\": " << static_cast<int>(analyzer.get_source()) << ",\n";
    ss << "    \"rta_speed\": " << static_cast<int>(analyzer.get_speed()) << ",\n";
    ss << "    \"rta_frozen\": " << (analyzer.is_frozen() ? "true" : "false") << ",\n";
    ss << "    \"theme\": {\n";
    ss << "      \"active_preset\": " << static_cast<int>(theme_cfg.active_preset) << ",\n";
    ss << "      \"preset_name\": \"" << escape_json(theme_preset_name(theme_cfg.active_preset)) << "\",\n";
    ss << "      \"colors\": {\n";
    for (int r = 0; r < static_cast<int>(ThemeColorRole::Count); ++r) {
        auto role = static_cast<ThemeColorRole>(r);
        if (r > 0) ss << ",\n";
        ss << "        \"" << escape_json(theme_role_key(role)) << "\": \""
           << escape_json(format_hex_color(active_colors.get_role(role))) << "\"";
    }
    ss << "\n      }\n";
    ss << "    }\n";
    ss << "  }\n";
    ss << "}\n";

    return ss.str();
}

std::string WebControlBridge::get_telemetry_json() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);

    const auto audio_snap = audio_service().snapshot();
    const auto mock_cfg = g_backend.get_config();
    const auto rta_snap = g_backend.get_analyzer().get_snapshot();

    ss << "{\n";
    ss << "  \"version\": " << state_version_ << ",\n";
    ss << "  \"audio_mode\": " << static_cast<int>(audio_snap.mode) << ",\n";
    ss << "  \"service_state\": " << static_cast<int>(audio_snap.state) << ",\n";
    ss << "  \"sim_running\": " << (g_backend.is_running() ? "true" : "false") << ",\n";
    ss << "  \"cpu_load\": " << mock_cfg.cpu_load << ",\n";
    ss << "  \"dsp_time_us\": " << audio_snap.dspTimeUs << ",\n";
    ss << "  \"underruns\": " << (audio_snap.mode == AudioMode::RealAudio ? audio_snap.underruns : mock_cfg.xruns) << ",\n";

    ss << "  \"channels\": [";
    for (size_t i = 0; i < g_mixer_state.channels.size(); ++i) {
        const auto* ch = g_mixer_state.channels.get(i);
        if (!ch) continue;
        if (i > 0) ss << ",";
        ss << "{\"id\":" << ch->id
           << ",\"in_l\":" << ch->input_peak_l
           << ",\"in_r\":" << ch->input_peak_r
           << ",\"out_l\":" << ch->peak_l
           << ",\"out_r\":" << ch->peak_r
           << ",\"post_l\":" << ch->post_fader_peak_l
           << ",\"post_r\":" << ch->post_fader_peak_r
           << ",\"hold_l\":" << ch->peak_hold_l
           << ",\"hold_r\":" << ch->peak_hold_r
           << ",\"fx_in_l\":" << ch->fx_in_peak_l
           << ",\"fx_in_r\":" << ch->fx_in_peak_r
           << ",\"fx_out_l\":" << ch->fx_out_peak_l
           << ",\"fx_out_r\":" << ch->fx_out_peak_r
           << ",\"comp_gr\":" << ch->comp_gr
           << ",\"comp_in\":" << ch->comp_input
           << ",\"comp_out\":" << ch->comp_output
           << ",\"gate_gr\":" << ch->gate_gr_db
           << ",\"gate_det\":" << ch->gate_detector_db
           << ",\"gate_state\":" << ch->gate_state
           << ",\"lim_gr\":" << ch->limiter_gr_db
           << ",\"clip\":" << (ch->clip ? "true" : "false")
           << ",\"in_clip\":" << (ch->input_clip ? "true" : "false") << "}";
    }
    ss << "],\n";

    ss << "  \"aux_buses\": [";
    for (size_t a = 0; a < 4; ++a) {
        const auto& aux = g_mixer_state.aux_buses[a];
        if (a > 0) ss << ",";
        ss << "{\"id\":" << aux.id
           << ",\"in_l\":" << aux.input_peak_l
           << ",\"in_r\":" << aux.input_peak_r
           << ",\"out_l\":" << aux.peak_l
           << ",\"out_r\":" << aux.peak_r
           << ",\"hold_l\":" << aux.peak_hold_l
           << ",\"hold_r\":" << aux.peak_hold_r
           << ",\"comp_gr\":" << aux.comp_gr
           << ",\"lim_gr\":" << aux.limiter_gr_db
           << ",\"clip\":" << (aux.clip ? "true" : "false") << "}";
    }
    ss << "],\n";

    const auto& m = g_mixer_state.master;
    ss << "  \"master\": {"
       << "\"id\":" << m.id
       << ",\"in_l\":" << m.input_peak_l
       << ",\"in_r\":" << m.input_peak_r
       << ",\"out_l\":" << m.peak_l
       << ",\"out_r\":" << m.peak_r
       << ",\"hold_l\":" << m.peak_hold_l
       << ",\"hold_r\":" << m.peak_hold_r
       << ",\"pre_lim_l\":" << m.pre_limiter_peak_l
       << ",\"pre_lim_r\":" << m.pre_limiter_peak_r
       << ",\"comp_gr\":" << m.comp_gr
       << ",\"comp_in\":" << m.comp_input
       << ",\"comp_out\":" << m.comp_output
       << ",\"lim_gr\":" << m.limiter_gr_db
       << ",\"clip\":" << (m.clip ? "true" : "false") << "},\n";

    ss << "  \"rta\": {\n";
    ss << "    \"mode\": " << static_cast<int>(rta_snap.mode) << ",\n";
    ss << "    \"source\": " << static_cast<int>(rta_snap.source) << ",\n";
    ss << "    \"frozen\": " << (rta_snap.frozen ? "true" : "false") << ",\n";
    ss << "    \"channel_id\": " << rta_snap.diagnostics.sourceChannel << ",\n";
    ss << "    \"has_valid_pcm\": " << (rta_snap.diagnostics.hasValidPcm ? "true" : "false") << ",\n";
    ss << "    \"sample_rate\": " << rta_snap.diagnostics.sampleRate << ",\n";
    ss << "    \"fft_size\": " << rta_snap.diagnostics.fftSize << ",\n";
    ss << "    \"peak_freq_hz\": " << rta_snap.diagnostics.peakFrequencyHz << ",\n";
    ss << "    \"peak_amp_db\": " << rta_snap.diagnostics.peakAmplitudeDbfs << ",\n";
    ss << "    \"sequence\": " << rta_snap.diagnostics.sequenceNumber << ",\n";
    ss << "    \"bands_31\": "; write_float_array(ss, rta_snap.bands_31.data(), 31); ss << ",\n";
    ss << "    \"peak_31\": "; write_float_array(ss, rta_snap.peak_hold_31.data(), 31); ss << ",\n";
    ss << "    \"bands_63\": "; write_float_array(ss, rta_snap.bands_63.data(), 63); ss << ",\n";
    ss << "    \"peak_63\": "; write_float_array(ss, rta_snap.peak_hold_63.data(), 63); ss << ",\n";
    ss << "    \"fft_bins\": "; write_float_array(ss, rta_snap.fft_bins.data(), 64); ss << ",\n";
    ss << "    \"fft_peak\": "; write_float_array(ss, rta_snap.fft_peak_hold.data(), 64); ss << "\n";
    ss << "  }\n";
    ss << "}\n";

    return ss.str();
}

std::string WebControlBridge::get_catalog_json() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(4);
    ss << "{\n";

    // Plugins list from PluginRegistry
    const auto& catalog = PluginRegistry::instance().get_catalog();
    ss << "  \"plugins\": [\n";
    for (size_t i = 0; i < catalog.size(); ++i) {
        const auto& p = catalog[i];
        if (i > 0) ss << ",\n";
        ss << "    {\n";
        ss << "      \"uri\": \"" << escape_json(p.uri) << "\",\n";
        ss << "      \"name\": \"" << escape_json(p.name) << "\",\n";
        ss << "      \"category\": \"" << escape_json(p.category) << "\",\n";
        ss << "      \"author\": \"" << escape_json(p.author) << "\",\n";
        ss << "      \"description\": \"" << escape_json(p.description) << "\",\n";
        ss << "      \"source_type\": " << static_cast<int>(p.source_type) << ",\n";
        ss << "      \"supports_mono\": " << (p.supports_mono ? "true" : "false") << ",\n";
        ss << "      \"supports_stereo\": " << (p.supports_stereo ? "true" : "false") << ",\n";
        ss << "      \"param_count\": " << p.parameters.size() << ",\n";
        ss << "      \"parameters\": [";
        for (size_t k = 0; k < p.parameters.size(); ++k) {
            const auto& prm = p.parameters[k];
            if (k > 0) ss << ",";
            ss << "{\"id\":\"" << escape_json(prm.id) << "\","
               << "\"name\":\"" << escape_json(prm.name) << "\","
               << "\"unit\":\"" << escape_json(prm.unit) << "\","
               << "\"min\":" << prm.min_val << ","
               << "\"max\":" << prm.max_val << ","
               << "\"default\":" << prm.default_val << ","
               << "\"step\":" << prm.step << ","
               << "\"is_toggle\":" << (prm.is_toggle ? "true" : "false") << "}";
        }
        ss << "]\n";
        ss << "    }";
    }
    ss << "\n  ],\n";

    // Pedalboard Presets from PedalboardPresetManager
    const auto& presets = prismix::pedalboard::PedalboardPresetManager::instance().getPresets();
    ss << "  \"presets\": [\n";
    for (size_t i = 0; i < presets.size(); ++i) {
        const auto& pr = presets[i];
        if (i > 0) ss << ",\n";
        ss << "    {\n";
        ss << "      \"name\": \"" << escape_json(pr.name) << "\",\n";
        ss << "      \"author\": \"" << escape_json(pr.author) << "\",\n";
        ss << "      \"description\": \"" << escape_json(pr.description) << "\",\n";
        ss << "      \"slots_count\": " << pr.slots.size() << "\n";
        ss << "    }";
    }
    ss << "\n  ],\n";

    // ISO 31-band frequencies and labels
    const auto& iso_freqs = GraphicEqModel::get_frequencies();
    const auto& iso_labels = GraphicEqModel::get_labels();
    ss << "  \"geq_frequencies\": "; write_float_array(ss, iso_freqs.data(), 31); ss << ",\n";
    ss << "  \"geq_labels\": [";
    for (size_t i = 0; i < 31; ++i) {
        if (i > 0) ss << ",";
        ss << "\"" << escape_json(iso_labels[i]) << "\"";
    }
    ss << "],\n";

    // PatchStorage catalog state
    prismix::patchstorage::BrowseState ps_state;
    std::unordered_map<int, prismix::patchstorage::PatchStatus> ps_statuses;
    prismix::patchstorage::PatchStorageService::instance().poll_state(ps_state, ps_statuses);
    ss << "  \"patchstorage\": {\n";
    ss << "    \"current_page\": " << ps_state.current_page << ",\n";
    ss << "    \"total_pages\": " << ps_state.total_pages << ",\n";
    ss << "    \"total_items\": " << ps_state.total_items << ",\n";
    ss << "    \"query\": \"" << escape_json(ps_state.query) << "\",\n";
    ss << "    \"is_loading\": " << (ps_state.is_loading ? "true" : "false") << ",\n";
    ss << "    \"status_message\": \"" << escape_json(ps_state.status_message) << "\",\n";
    ss << "    \"items\": [\n";
    for (size_t i = 0; i < ps_state.items.size(); ++i) {
        const auto& item = ps_state.items[i];
        int st_code = 0;
        std::string st_msg;
        std::string st_uri;
        auto it = ps_statuses.find(item.id);
        if (it != ps_statuses.end()) {
            st_code = static_cast<int>(it->second.state);
            st_msg = it->second.message;
            st_uri = it->second.installed_plugin_uri;
        }
        bool compat = item.is_compatible_with_host(prismix::patchstorage::detect_current_host_arch());
        if (i > 0) ss << ",\n";
        ss << "      {\n";
        ss << "        \"id\": " << item.id << ",\n";
        ss << "        \"title\": \"" << escape_json(item.title) << "\",\n";
        ss << "        \"excerpt\": \"" << escape_json(item.excerpt) << "\",\n";
        ss << "        \"author\": \"" << escape_json(item.author.name) << "\",\n";
        ss << "        \"license\": \"" << escape_json(item.license_name) << "\",\n";
        ss << "        \"version\": \"" << escape_json(item.revision) << "\",\n";
        ss << "        \"compatible\": " << (compat ? "true" : "false") << ",\n";
        ss << "        \"page_url\": \"" << escape_json(item.url) << "\",\n";
        ss << "        \"install_state\": " << st_code << ",\n";
        ss << "        \"installed_uri\": \"" << escape_json(st_uri) << "\",\n";
        ss << "        \"install_message\": \"" << escape_json(st_msg) << "\"\n";
        ss << "      }";
    }
    ss << "\n    ]\n";
    ss << "  }\n";
    ss << "}\n";

    return ss.str();
}

CommandResult WebControlBridge::execute_command(const WebCommand& cmd) {
    CommandResult res;
    if (prismix::security::SecurityService::instance().isMixerLocked()) {
        res.success = false;
        res.error = "The mixer is locked. Unlock it with the configured PIN.";
        res.state_version = get_state_version();
        return res;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // 1. Handle system/global commands that do not require a valid channel pointer
    switch (cmd.type) {
        case WebCommandType::SelectChannel: {
            if (cmd.channel_id == 0 || cmd.channel_id == -1 || cmd.channel_id == 100) {
                g_mixer_state.selected_channel = -1;
                g_mixer_state.selected_aux_bus = -1;
                for (auto& c : g_mixer_state.channels) c.select = false;
                for (auto& a : g_mixer_state.aux_buses) a.select = false;
                g_mixer_state.master.select = true;
                g_backend.get_analyzer().set_channel_id(0);
                res.success = true;
            } else if (cmd.channel_id >= 101 && cmd.channel_id <= 104) {
                int aux_idx = cmd.channel_id - 101;
                g_mixer_state.selected_aux_bus = aux_idx;
                for (auto& c : g_mixer_state.channels) c.select = false;
                for (int a = 0; a < 4; ++a) g_mixer_state.aux_buses[a].select = (a == aux_idx);
                g_mixer_state.master.select = false;
                g_backend.get_analyzer().set_channel_id(static_cast<uint32_t>(cmd.channel_id));
                res.success = true;
            } else {
                int found_idx = -1;
                for (size_t i = 0; i < g_mixer_state.channels.size(); ++i) {
                    if (g_mixer_state.channels[i].id == cmd.channel_id) {
                        found_idx = static_cast<int>(i);
                        break;
                    }
                }
                if (found_idx < 0) {
                    res.success = false;
                    res.error = "Channel ID " + std::to_string(cmd.channel_id) + " not found";
                    res.state_version = state_version_;
                    return res;
                }
                g_mixer_state.selected_channel = found_idx;
                g_mixer_state.selected_aux_bus = -1;
                for (size_t i = 0; i < g_mixer_state.channels.size(); ++i) {
                    g_mixer_state.channels[i].select = (static_cast<int>(i) == found_idx);
                }
                for (auto& a : g_mixer_state.aux_buses) a.select = false;
                g_mixer_state.master.select = false;
                g_backend.get_analyzer().set_channel_id(static_cast<uint32_t>(cmd.channel_id));
                res.success = true;
            }
            state_version_++;
            res.state_version = state_version_;
            if (change_cb_) change_cb_(state_version_);
            return res;
        }

        case WebCommandType::AddChannel: {
            if (static_cast<int>(g_mixer_state.channels.size()) >= MAX_CHANNELS) {
                res.success = false;
                res.error = "Maximum channel count (32) reached";
                res.state_version = state_version_;
                return res;
            }
            ChannelState ch{};
            int new_index = static_cast<int>(g_mixer_state.channels.size());
            for (int candidate = 1; candidate <= MAX_CHANNELS; ++candidate) {
                if (!mixer_find_channel_by_id(candidate)) {
                    ch.id = candidate;
                    break;
                }
            }
            if (ch.id <= 0) {
                res.success = false;
                res.error = "No available channel IDs";
                res.state_version = state_version_;
                return res;
            }
            if (!cmd.name.empty() && is_safe_name(cmd.name)) {
                std::snprintf(ch.name, sizeof(ch.name), "%s", cmd.name.c_str());
            } else {
                std::snprintf(ch.name, sizeof(ch.name), "CH %02d", new_index + 1);
            }
            ch.format = (cmd.int_value == 1) ? ChannelFormat::Stereo : ChannelFormat::Mono;
            ch.input_source_index = (cmd.int_value2 >= -1 && cmd.int_value2 < static_cast<int>(RoutingMatrix::NUM_INPUTS))
                ? cmd.int_value2 : -1;
            ch.input_source_index_r = (ch.format == ChannelFormat::Stereo && ch.input_source_index >= 0)
                ? std::min(ch.input_source_index + 1, static_cast<int>(RoutingMatrix::NUM_INPUTS) - 1) : -1;
            if (ch.input_source_index >= 0) {
                std::snprintf(ch.input_label, sizeof(ch.input_label), "%s",
                              RoutingMatrix::get_input_source_name(ch.input_source_index));
            } else {
                std::snprintf(ch.input_label, sizeof(ch.input_label), "UNASSIGNED");
            }
            ch.output_route = ChannelOutputRoute::MainLR;
            ch.output_route_index = 0;
            ch.route_to_main = true;
            ch.gain = 0.5f;
            ch.pan = 0.5f;
            ch.fader = 0.86f;
            ch.fx_rack.set_owner_context(ch.id, false, ch.format == ChannelFormat::Stereo);
            g_routing_matrix.set_channel_input_source(static_cast<size_t>(ch.id - 1), ch.input_source_index);
            g_routing_matrix.set_channel_main_lr(static_cast<size_t>(ch.id - 1), true);

            g_mixer_state.channels.push_back(ch);
            g_mixer_state.selected_channel = new_index;
            g_mixer_state.selected_aux_bus = -1;
            for (int i = 0; i <= new_index; ++i) {
                g_mixer_state.channels[static_cast<size_t>(i)].select = (i == new_index);
            }
            g_mixer_state.master.select = false;
            res.success = true;
            state_version_++;
            res.state_version = state_version_;
            if (change_cb_) change_cb_(state_version_);
            return res;
        }

        case WebCommandType::DeleteChannel: {
            if (cmd.channel_id <= 0 || cmd.channel_id >= 100) {
                res.success = false;
                res.error = "Master and AUX buses are protected and cannot be deleted";
                res.state_version = state_version_;
                return res;
            }
            int found_idx = -1;
            for (size_t i = 0; i < g_mixer_state.channels.size(); ++i) {
                if (g_mixer_state.channels[i].id == cmd.channel_id) {
                    found_idx = static_cast<int>(i);
                    break;
                }
            }
            if (found_idx < 0) {
                res.success = false;
                res.error = "Channel not found";
                res.state_version = state_version_;
                return res;
            }
            const int selected_channel_id =
                g_mixer_state.selected_channel >= 0 &&
                g_mixer_state.selected_channel < static_cast<int>(g_mixer_state.channels.size())
                    ? g_mixer_state.channels[static_cast<size_t>(g_mixer_state.selected_channel)].id
                    : -1;
            const int selected_aux_bus = g_mixer_state.selected_aux_bus;
            const bool master_was_selected = selected_channel_id < 0 && selected_aux_bus < 0 && g_mixer_state.master.select;
            g_routing_matrix.set_channel_input_source(static_cast<size_t>(cmd.channel_id - 1), -1);
            g_mixer_state.channels.erase(g_mixer_state.channels.begin() + found_idx);

            if (selected_channel_id == cmd.channel_id) {
                g_mixer_state.selected_aux_bus = -1;
                if (g_mixer_state.channels.empty()) {
                    g_mixer_state.selected_channel = -1;
                } else {
                    g_mixer_state.selected_channel = std::min(found_idx, static_cast<int>(g_mixer_state.channels.size()) - 1);
                }
            } else if (selected_aux_bus >= 0 && selected_aux_bus < static_cast<int>(g_mixer_state.aux_buses.size())) {
                g_mixer_state.selected_channel = -1;
                g_mixer_state.selected_aux_bus = selected_aux_bus;
            } else if (master_was_selected || selected_channel_id < 0) {
                g_mixer_state.selected_channel = -1;
                g_mixer_state.selected_aux_bus = -1;
            } else {
                g_mixer_state.selected_aux_bus = -1;
                g_mixer_state.selected_channel = -1;
                for (size_t i = 0; i < g_mixer_state.channels.size(); ++i) {
                    if (g_mixer_state.channels[i].id == selected_channel_id) {
                        g_mixer_state.selected_channel = static_cast<int>(i);
                        break;
                    }
                }
            }
            for (size_t i = 0; i < g_mixer_state.channels.size(); ++i) {
                g_mixer_state.channels[i].select = (static_cast<int>(i) == g_mixer_state.selected_channel);
            }
            for (size_t i = 0; i < g_mixer_state.aux_buses.size(); ++i) {
                g_mixer_state.aux_buses[i].select = (static_cast<int>(i) == g_mixer_state.selected_aux_bus);
            }
            g_mixer_state.master.select = g_mixer_state.selected_channel < 0 && g_mixer_state.selected_aux_bus < 0;
            const uint32_t analyzer_channel_id = g_mixer_state.selected_aux_bus >= 0
                ? static_cast<uint32_t>(101 + g_mixer_state.selected_aux_bus)
                : (g_mixer_state.selected_channel >= 0
                    ? static_cast<uint32_t>(g_mixer_state.channels[static_cast<size_t>(g_mixer_state.selected_channel)].id)
                    : 0U);
            g_backend.get_analyzer().set_channel_id(analyzer_channel_id);
            res.success = true;
            state_version_++;
            res.state_version = state_version_;
            if (change_cb_) change_cb_(state_version_);
            return res;
        }

        case WebCommandType::SetRtaConfig: {
            auto& analyzer = g_backend.get_analyzer();
            if (cmd.int_value >= 0 && cmd.int_value <= 2) {
                analyzer.set_mode(static_cast<RtaMode>(cmd.int_value));
            }
            if (cmd.int_value2 >= 0 && cmd.int_value2 <= 1) {
                analyzer.set_source(static_cast<RtaSource>(cmd.int_value2));
            }
            if (cmd.bus_index >= 0 && cmd.bus_index <= 2) {
                analyzer.set_speed(static_cast<RtaSpeed>(cmd.bus_index));
            }
            if (cmd.has_enabled) {
                analyzer.set_freeze(cmd.bool_value);
            }
            if (cmd.has_gain) {
                analyzer.clear_peaks();
            }
            res.success = true;
            state_version_++;
            res.state_version = state_version_;
            if (change_cb_) change_cb_(state_version_);
            return res;
        }

        case WebCommandType::SetBusMainLr: {
            if (cmd.bus_index < 0 || cmd.bus_index >= static_cast<int>(RoutingMatrix::NUM_BUSES)) {
                res.success = false;
                res.error = "Invalid bus index";
                res.state_version = state_version_;
                return res;
            }
            g_routing_matrix.set_bus_main_lr(static_cast<size_t>(cmd.bus_index), cmd.bool_value);
            res.success = true;
            state_version_++;
            res.state_version = state_version_;
            if (change_cb_) change_cb_(state_version_);
            return res;
        }

        case WebCommandType::SetOutputPatch: {
            if (cmd.bus_index < 0 || cmd.bus_index >= static_cast<int>(RoutingMatrix::NUM_OUTPUTS)) {
                res.success = false;
                res.error = "Invalid output port index";
                res.state_version = state_version_;
                return res;
            }
            if (cmd.int_value < 0 || cmd.int_value > 21) {
                res.success = false;
                res.error = "Invalid output patch source ID";
                res.state_version = state_version_;
                return res;
            }
            g_routing_matrix.set_output_patch(static_cast<size_t>(cmd.bus_index), cmd.int_value);
            res.success = true;
            state_version_++;
            res.state_version = state_version_;
            if (change_cb_) change_cb_(state_version_);
            return res;
        }

        case WebCommandType::SetAudioMode: {
            AudioMode mode = (cmd.int_value == 1) ? AudioMode::RealAudio : AudioMode::Simulation;
            if (!audio_service().selectMode(mode)) {
                res.success = false;
                res.error = "Failed to switch audio mode";
                res.state_version = state_version_;
                return res;
            }
            if (mode == AudioMode::Simulation) {
                audio_service().stop();
                g_backend.start();
            } else {
                g_backend.stop();
            }
            res.success = true;
            state_version_++;
            res.state_version = state_version_;
            if (change_cb_) change_cb_(state_version_);
            return res;
        }

        case WebCommandType::StartAudio: {
            const auto snap = audio_service().snapshot();
            if (snap.mode == AudioMode::Simulation) {
                g_backend.start();
                res.success = true;
            } else {
                std::string err;
                if (!ui_start_real_audio(&err)) {
                    res.success = false;
                    res.error = err.empty() ? "Failed to start real audio engine" : err;
                    res.state_version = state_version_;
                    return res;
                }
                res.success = true;
            }
            state_version_++;
            res.state_version = state_version_;
            if (change_cb_) change_cb_(state_version_);
            return res;
        }

        case WebCommandType::StopAudio: {
            const auto snap = audio_service().snapshot();
            if (snap.mode == AudioMode::Simulation) {
                g_backend.stop();
            } else {
                ui_stop_real_audio();
            }
            res.success = true;
            state_version_++;
            res.state_version = state_version_;
            if (change_cb_) change_cb_(state_version_);
            return res;
        }

        case WebCommandType::RefreshDevices: {
            audio_service().refreshDevices();
            res.success = true;
            state_version_++;
            res.state_version = state_version_;
            if (change_cb_) change_cb_(state_version_);
            return res;
        }

        case WebCommandType::SetAudioConfig: {
            auto& setup = ui_get_persisted_audio_setup();
            if (!cmd.capture_device.empty()) {
                audio_service().selectCaptureDevice(cmd.capture_device == "default" ? "" : cmd.capture_device);
                setup.captureDeviceId = (cmd.capture_device == "default" ? "" : cmd.capture_device);
            }
            if (!cmd.render_device.empty()) {
                audio_service().selectRenderDevice(cmd.render_device == "default" ? "" : cmd.render_device);
                setup.renderDeviceId = (cmd.render_device == "default" ? "" : cmd.render_device);
            }
            uint32_t sr = cmd.sample_rate > 0 ? cmd.sample_rate : audio_service().getRequestedSampleRate();
            uint32_t bf = cmd.buffer_frames > 0 ? cmd.buffer_frames : audio_service().getRequestedBufferFrames();
            if (cmd.sample_rate > 0 || cmd.buffer_frames > 0) {
                if (!audio_service().setRequestedAudioConfig(sr, bf)) {
                    res.success = false;
                    res.error = "Unsupported sample rate or buffer frame size";
                    res.state_version = state_version_;
                    return res;
                }
                setup.requestedSampleRate = sr;
                setup.requestedBufferFrames = bf;
                auto mcfg = g_backend.get_config();
                if (sr == 44100) mcfg.sample_rate = SampleRate::SR_44100;
                else if (sr == 96000) mcfg.sample_rate = SampleRate::SR_96000;
                else mcfg.sample_rate = SampleRate::SR_48000;
                mcfg.buffer_size = static_cast<int>(bf);
                g_backend.set_config(mcfg);
            }
            ui_save_persisted_audio_setup(nullptr);
            res.success = true;
            state_version_++;
            res.state_version = state_version_;
            if (change_cb_) change_cb_(state_version_);
            return res;
        }

        case WebCommandType::SetGeneratorSignal: {
            if (cmd.int_value < 0 || cmd.int_value > 4) {
                res.success = false;
                res.error = "Invalid generator signal index (0..4)";
                res.state_version = state_version_;
                return res;
            }
            g_backend.set_generator_signal(static_cast<GeneratorSignal>(cmd.int_value));
            res.success = true;
            state_version_++;
            res.state_version = state_version_;
            if (change_cb_) change_cb_(state_version_);
            return res;
        }

        case WebCommandType::SaveScene: {
            std::string err;
            if (!save_mixer_scene(default_mixer_scene_path(), &err)) {
                res.success = false;
                res.error = err.empty() ? "Failed to save mixer scene" : err;
                res.state_version = state_version_;
                return res;
            }
            res.success = true;
            state_version_++;
            res.state_version = state_version_;
            if (change_cb_) change_cb_(state_version_);
            return res;
        }

        case WebCommandType::LoadScene: {
            std::string err;
            if (!load_mixer_scene(default_mixer_scene_path(), &err)) {
                res.success = false;
                res.error = err.empty() ? "Failed to load mixer scene" : err;
                res.state_version = state_version_;
                return res;
            }
            for (auto& ch : g_mixer_state.channels) sync_channel_to_dsp(&ch);
            for (auto& aux : g_mixer_state.aux_buses) sync_channel_to_dsp(&aux);
            sync_channel_to_dsp(&g_mixer_state.master);
            res.success = true;
            state_version_++;
            res.state_version = state_version_;
            if (change_cb_) change_cb_(state_version_);
            return res;
        }

        case WebCommandType::SetThemePreset: {
            if (cmd.int_value < 0 || cmd.int_value >= static_cast<int>(ThemePreset::Count)) {
                res.success = false;
                res.error = "Invalid theme preset index";
                res.state_version = state_version_;
                return res;
            }
            theme_set_preset(static_cast<ThemePreset>(cmd.int_value));
            save_theme_config(theme_get_config(), default_theme_config_path(), nullptr);
            res.success = true;
            state_version_++;
            res.state_version = state_version_;
            if (change_cb_) change_cb_(state_version_);
            return res;
        }

        case WebCommandType::SetThemeColor: {
            if (cmd.int_value < 0 || cmd.int_value >= static_cast<int>(ThemeColorRole::Count)) {
                res.success = false;
                res.error = "Invalid theme color role";
                res.state_version = state_version_;
                return res;
            }
            uint32_t hex = 0;
            if (!parse_hex_color(cmd.name, hex)) {
                res.success = false;
                res.error = "Invalid hex color string (expected #RRGGBB)";
                res.state_version = state_version_;
                return res;
            }
            theme_set_custom_color(static_cast<ThemeColorRole>(cmd.int_value), hex);
            save_theme_config(theme_get_config(), default_theme_config_path(), nullptr);
            res.success = true;
            state_version_++;
            res.state_version = state_version_;
            if (change_cb_) change_cb_(state_version_);
            return res;
        }

        case WebCommandType::SaveTheme: {
            std::string err;
            if (!save_theme_config(theme_get_config(), default_theme_config_path(), &err)) {
                res.success = false;
                res.error = err.empty() ? "Failed to save theme config" : err;
                res.state_version = state_version_;
                return res;
            }
            res.success = true;
            state_version_++;
            res.state_version = state_version_;
            if (change_cb_) change_cb_(state_version_);
            return res;
        }

        case WebCommandType::PatchStorageBrowse: {
            auto& ps = prismix::patchstorage::PatchStorageService::instance();
            ps.start();
            int page = std::max(1, cmd.int_value);
            ps.request_browse(page, cmd.name, 0, cmd.int_value2 > 0 ? cmd.int_value2 : 0);
            res.success = true;
            state_version_++;
            res.state_version = state_version_;
            if (change_cb_) change_cb_(state_version_);
            return res;
        }

        case WebCommandType::PatchStorageInstall: {
            auto& ps = prismix::patchstorage::PatchStorageService::instance();
            prismix::patchstorage::BrowseState st;
            std::unordered_map<int, prismix::patchstorage::PatchStatus> statuses;
            ps.poll_state(st, statuses);
            bool found = false;
            for (const auto& item : st.items) {
                if (item.id == cmd.int_value) {
                    ps.request_install(item);
                    found = true;
                    break;
                }
            }
            if (!found) {
                res.success = false;
                res.error = "Patch ID not found in active catalog page";
                res.state_version = state_version_;
                return res;
            }
            res.success = true;
            state_version_++;
            res.state_version = state_version_;
            if (change_cb_) change_cb_(state_version_);
            return res;
        }

        default:
            break;
    }

    // 2. Resolve target channel for channel-scoped commands
    ChannelState* ch = resolve_channel(cmd.channel_id);
    if (!ch) {
        res.success = false;
        res.error = "Channel ID " + std::to_string(cmd.channel_id) + " not found";
        res.state_version = state_version_;
        return res;
    }
    const auto caps = mixer_get_channel_capabilities(ch);

    switch (cmd.type) {
        case WebCommandType::RenameChannel: {
            if (!is_safe_name(cmd.name)) {
                res.success = false;
                res.error = "Invalid channel name";
                res.state_version = state_version_;
                return res;
            }
            std::snprintf(ch->name, sizeof(ch->name), "%s", cmd.name.c_str());
            sync_channel_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::SetChannelFader: {
            if (!std::isfinite(cmd.param_value)) {
                res.success = false;
                res.error = "Invalid fader value";
                res.state_version = state_version_;
                return res;
            }
            ch->fader = std::clamp(cmd.param_value, 0.0f, 1.0f);
            sync_channel_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::SetChannelReturnGain: {
            ch->return_gain = std::clamp(cmd.param_value, 0.0f, 1.0f);
            sync_channel_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::SetChannelPan: {
            if (!std::isfinite(cmd.param_value)) {
                res.success = false;
                res.error = "Invalid pan value";
                res.state_version = state_version_;
                return res;
            }
            ch->pan = std::clamp(cmd.param_value, 0.0f, 1.0f);
            sync_channel_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::SetChannelGain: {
            if (!std::isfinite(cmd.param_value)) {
                res.success = false;
                res.error = "Invalid gain value";
                res.state_version = state_version_;
                return res;
            }
            ch->gain = std::clamp(cmd.param_value, 0.0f, 1.0f);
            sync_channel_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::SetChannelMute: {
            ch->mute = cmd.bool_value;
            sync_channel_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::SetChannelSolo: {
            ch->solo = cmd.bool_value;
            sync_channel_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::SetChannelPolarity: {
            if (!caps.has_preamp) {
                res.success = false;
                res.error = "Polarity invert is only supported on input channels";
                res.state_version = state_version_;
                return res;
            }
            ch->polarity_inverted = cmd.bool_value;
            sync_channel_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::SetChannelPhantom: {
            if (!caps.has_preamp) {
                res.success = false;
                res.error = "48V Phantom power is only supported on input channels";
                res.state_version = state_version_;
                return res;
            }
            ch->phantom_on = cmd.bool_value;
            res.success = true;
            break;
        }

        case WebCommandType::SetChannelHpfEnabled: {
            if (!caps.has_hpf) {
                res.success = false;
                res.error = "HPF is not supported on this bus";
                res.state_version = state_version_;
                return res;
            }
            ch->hpf_on = cmd.bool_value;
            sync_channel_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::SetChannelHpfFreq: {
            if (!caps.has_hpf) {
                res.success = false;
                res.error = "HPF is not supported on this bus";
                res.state_version = state_version_;
                return res;
            }
            ch->hpf_frequency = std::clamp(cmd.param_value, 20.0f, 400.0f);
            sync_channel_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::SetChannelFormat: {
            if (caps.is_master || caps.is_aux) {
                res.success = false;
                res.error = "Bus format is fixed to Stereo";
                res.state_version = state_version_;
                return res;
            }
            ch->format = (cmd.int_value == 1) ? ChannelFormat::Stereo : ChannelFormat::Mono;
            if (ch->format == ChannelFormat::Stereo && ch->input_source_index >= 0 && ch->input_source_index_r < 0) {
                ch->input_source_index_r = std::min(ch->input_source_index + 1, static_cast<int>(RoutingMatrix::NUM_INPUTS) - 1);
            }
            sync_channel_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::SetAuxSendLevel: {
            if (!caps.has_aux_sends || cmd.aux_index < 0 || cmd.aux_index >= 4) {
                res.success = false;
                res.error = "Invalid AUX send target";
                res.state_version = state_version_;
                return res;
            }
            ch->sends[static_cast<size_t>(cmd.aux_index)] = std::clamp(cmd.param_value, 0.0f, 1.0f);
            sync_channel_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::SetAuxSendEnabled: {
            if (!caps.has_aux_sends || cmd.aux_index < 0 || cmd.aux_index >= 4) {
                res.success = false;
                res.error = "Invalid AUX send target";
                res.state_version = state_version_;
                return res;
            }
            ch->send_enabled[static_cast<size_t>(cmd.aux_index)] = cmd.bool_value;
            sync_channel_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::SetAuxSendPreFader: {
            if (!caps.has_aux_sends || cmd.aux_index < 0 || cmd.aux_index >= 4) {
                res.success = false;
                res.error = "Invalid AUX send target";
                res.state_version = state_version_;
                return res;
            }
            ch->send_pre_fader[static_cast<size_t>(cmd.aux_index)] = cmd.bool_value;
            sync_channel_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::SetSpeakerDelay: {
            if (!caps.has_speaker_delay) {
                res.success = false;
                res.error = "Speaker alignment delay is only supported on Master bus";
                res.state_version = state_version_;
                return res;
            }
            if (cmd.has_enabled) ch->speaker_delay_enabled = cmd.bool_value;
            if (cmd.has_filter_type) ch->speaker_delay_link = (cmd.filter_type != 0);
            if (cmd.has_freq) {
                ch->speaker_delay_l_ms = std::clamp(cmd.param_value, 0.0f, 500.0f);
                if (ch->speaker_delay_link) ch->speaker_delay_r_ms = ch->speaker_delay_l_ms;
            }
            if (cmd.has_gain) {
                ch->speaker_delay_r_ms = std::clamp(cmd.float_value2, 0.0f, 500.0f);
                if (ch->speaker_delay_link) ch->speaker_delay_l_ms = ch->speaker_delay_r_ms;
            }
            sync_channel_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::SetEqBypass: {
            ch->eq_bypass = cmd.bool_value;
            sync_channel_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::SetPeqBand: {
            if (cmd.band_index < 0 || cmd.band_index >= 4) {
                res.success = false;
                res.error = "Invalid PEQ band index (0..3)";
                res.state_version = state_version_;
                return res;
            }
            const size_t b = static_cast<size_t>(cmd.band_index);
            if (cmd.has_freq && std::isfinite(cmd.param_value)) {
                ch->eq_freq[b] = std::clamp(cmd.param_value, 20.0f, 20000.0f);
            }
            if (cmd.has_gain && std::isfinite(cmd.float_value2)) {
                ch->eq_gain[b] = std::clamp(cmd.float_value2, -18.0f, 18.0f);
            }
            if (cmd.has_q && std::isfinite(cmd.float_value3)) {
                ch->eq_q[b] = std::clamp(cmd.float_value3, 0.1f, 20.0f);
            }
            if (cmd.has_enabled) {
                ch->eq_enabled[b] = cmd.bool_value;
            }
            if (cmd.has_filter_type) {
                ch->eq_filter_type[b] = std::clamp(cmd.filter_type, 0, 4);
                ch->eq_shelf[b] = (ch->eq_filter_type[b] == 0 || ch->eq_filter_type[b] == 2);
            }
            sync_channel_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::FlattenPeq: {
            ch->eq_gain = {0.0f, 0.0f, 0.0f, 0.0f};
            ch->eq_freq = {80.0f, 350.0f, 2500.0f, 10000.0f};
            ch->eq_q = {0.7f, 1.0f, 1.0f, 0.7f};
            ch->eq_enabled = {true, true, true, true};
            ch->eq_filter_type = {0, 1, 1, 2};
            ch->eq_bypass = false;
            sync_channel_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::SetGeqMode: {
            ch->geq_mode_active = cmd.bool_value;
            sync_channel_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::SetGeqBypass: {
            ch->geq_bypass = cmd.bool_value;
            ch->geq.set_bypass(cmd.bool_value);
            sync_channel_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::SetGeqBand: {
            if (cmd.band_index < 0 || cmd.band_index >= 31 || !std::isfinite(cmd.param_value)) {
                res.success = false;
                res.error = "Invalid GEQ band index (0..30)";
                res.state_version = state_version_;
                return res;
            }
            ch->geq.set_band_gain(static_cast<size_t>(cmd.band_index), std::clamp(cmd.param_value, -12.0f, 12.0f));
            sync_channel_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::FlattenGeq: {
            ch->geq.reset_flat();
            sync_channel_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::SetGateEnabled: {
            if (!caps.has_gate || isMasterChannel(ch)) {
                res.success = false;
                res.error = "Noise Gate is prohibited on the Master bus";
                res.state_version = state_version_;
                return res;
            }
            ch->gate_on = cmd.bool_value;
            sync_channel_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::SetGateParam: {
            if (!caps.has_gate || isMasterChannel(ch)) {
                res.success = false;
                res.error = "Noise Gate is prohibited on the Master bus";
                res.state_version = state_version_;
                return res;
            }
            if (!std::isfinite(cmd.param_value)) {
                res.success = false;
                res.error = "Invalid gate parameter value";
                res.state_version = state_version_;
                return res;
            }
            if (cmd.param_id == "thresh") ch->gate_thresh = std::clamp(cmd.param_value, -80.0f, 0.0f);
            else if (cmd.param_id == "range") ch->gate_range = std::clamp(cmd.param_value, -80.0f, 0.0f);
            else if (cmd.param_id == "attack") ch->gate_attack = std::clamp(cmd.param_value, 0.5f, 100.0f);
            else if (cmd.param_id == "hold") ch->gate_hold = std::clamp(cmd.param_value, 5.0f, 1000.0f);
            else if (cmd.param_id == "release") ch->gate_release = std::clamp(cmd.param_value, 10.0f, 2000.0f);
            else if (cmd.param_id == "key_source") ch->gate_key_source = std::clamp(static_cast<int>(cmd.param_value), 0, 9);
            else {
                res.success = false;
                res.error = "Unknown gate parameter: " + cmd.param_id;
                res.state_version = state_version_;
                return res;
            }
            sync_channel_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::SetCompBypass: {
            ch->comp_bypass = cmd.bool_value;
            sync_channel_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::SetCompParam: {
            if (!std::isfinite(cmd.param_value)) {
                res.success = false;
                res.error = "Invalid compressor parameter value";
                res.state_version = state_version_;
                return res;
            }
            if (cmd.param_id == "thresh") ch->comp_thresh = std::clamp(cmd.param_value, -60.0f, 0.0f);
            else if (cmd.param_id == "ratio") ch->comp_ratio = std::clamp(cmd.param_value, 1.0f, 20.0f);
            else if (cmd.param_id == "attack") ch->comp_attack = std::clamp(cmd.param_value, 0.5f, 200.0f);
            else if (cmd.param_id == "release") ch->comp_release = std::clamp(cmd.param_value, 10.0f, 2000.0f);
            else if (cmd.param_id == "knee") ch->comp_knee = std::clamp(cmd.param_value, 0.0f, 24.0f);
            else if (cmd.param_id == "makeup") ch->comp_makeup = std::clamp(cmd.param_value, 0.0f, 24.0f);
            else {
                res.success = false;
                res.error = "Unknown compressor parameter: " + cmd.param_id;
                res.state_version = state_version_;
                return res;
            }
            sync_channel_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::SetLimiterEnabled: {
            ch->limiter_enabled = cmd.bool_value;
            sync_channel_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::SetLimiterParam: {
            if (!std::isfinite(cmd.param_value)) {
                res.success = false;
                res.error = "Invalid limiter parameter value";
                res.state_version = state_version_;
                return res;
            }
            if (cmd.param_id == "ceiling") ch->limiter_ceiling_db = std::clamp(cmd.param_value, -24.0f, 0.0f);
            else if (cmd.param_id == "release") ch->limiter_release_ms = std::clamp(cmd.param_value, 5.0f, 1000.0f);
            else {
                res.success = false;
                res.error = "Unknown limiter parameter: " + cmd.param_id;
                res.state_version = state_version_;
                return res;
            }
            sync_channel_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::SetSidechain: {
            if (cmd.has_enabled) ch->sidechain_on = cmd.bool_value;
            if (cmd.has_freq && std::isfinite(cmd.param_value)) ch->sidechain_low = std::clamp(cmd.param_value, 20.0f, 2000.0f);
            if (cmd.has_gain && std::isfinite(cmd.float_value2)) ch->sidechain_high = std::clamp(cmd.float_value2, 1000.0f, 20000.0f);
            sync_channel_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::SetPluginParameter: {
            if (!caps.has_fx_rack || isMasterChannel(ch)) {
                res.success = false;
                res.error = "Insert FX Rack is disabled on the Master bus";
                res.state_version = state_version_;
                return res;
            }
            if (cmd.slot_index < 0 || static_cast<size_t>(cmd.slot_index) >= ch->fx_rack.count()) {
                res.success = false;
                res.error = "Invalid slot index: " + std::to_string(cmd.slot_index);
                res.state_version = state_version_;
                return res;
            }
            auto* inst = ch->fx_rack.get_slot(static_cast<size_t>(cmd.slot_index));
            if (!inst) {
                res.success = false;
                res.error = "No plugin in slot " + std::to_string(cmd.slot_index);
                res.state_version = state_version_;
                return res;
            }
            auto* p = inst->find_parameter(cmd.param_id);
            if (!p) {
                res.success = false;
                res.error = "Parameter '" + cmd.param_id + "' not found on plugin " + inst->get_name();
                res.state_version = state_version_;
                return res;
            }
            if (!std::isfinite(cmd.param_value)) {
                res.success = false;
                res.error = "Invalid parameter value (non-finite)";
                res.state_version = state_version_;
                return res;
            }
            p->current_val = std::clamp(cmd.param_value, p->min_val, p->max_val);
            ui_sync_channel_fx_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::SetSlotBypass: {
            if (!caps.has_fx_rack || isMasterChannel(ch)) {
                res.success = false;
                res.error = "Insert FX Rack is disabled on the Master bus";
                res.state_version = state_version_;
                return res;
            }
            if (cmd.slot_index < 0 || static_cast<size_t>(cmd.slot_index) >= ch->fx_rack.count()) {
                res.success = false;
                res.error = "Invalid slot index: " + std::to_string(cmd.slot_index);
                res.state_version = state_version_;
                return res;
            }
            auto* inst = ch->fx_rack.get_slot(static_cast<size_t>(cmd.slot_index));
            if (!inst) {
                res.success = false;
                res.error = "No plugin in slot";
                res.state_version = state_version_;
                return res;
            }
            inst->set_bypassed(cmd.bool_value);
            ui_sync_channel_fx_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::AddPlugin: {
            if (!caps.has_fx_rack || isMasterChannel(ch)) {
                res.success = false;
                res.error = "Insert FX Rack is disabled on the Master bus";
                res.state_version = state_version_;
                return res;
            }
            if (cmd.uri.empty()) {
                res.success = false;
                res.error = "Plugin URI cannot be empty";
                res.state_version = state_version_;
                return res;
            }
            if (ch->fx_rack.count() >= PluginRack::MAX_SLOTS) {
                res.success = false;
                res.error = "FX Rack is full (maximum 8 slots)";
                res.state_version = state_version_;
                return res;
            }
            if (!ch->fx_rack.add_plugin(cmd.uri)) {
                res.success = false;
                res.error = "Failed to instantiate plugin: " + cmd.uri;
                res.state_version = state_version_;
                return res;
            }
            ui_sync_channel_fx_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::RemovePlugin: {
            if (!caps.has_fx_rack || isMasterChannel(ch)) {
                res.success = false;
                res.error = "Insert FX Rack is disabled on the Master bus";
                res.state_version = state_version_;
                return res;
            }
            if (cmd.slot_index < 0 || static_cast<size_t>(cmd.slot_index) >= ch->fx_rack.count()) {
                res.success = false;
                res.error = "Invalid slot index";
                res.state_version = state_version_;
                return res;
            }
            if (!ch->fx_rack.remove_plugin(static_cast<size_t>(cmd.slot_index))) {
                res.success = false;
                res.error = "Failed to remove plugin from slot " + std::to_string(cmd.slot_index);
                res.state_version = state_version_;
                return res;
            }
            ui_sync_channel_fx_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::MovePlugin: {
            if (!caps.has_fx_rack || isMasterChannel(ch)) {
                res.success = false;
                res.error = "Insert FX Rack is disabled on the Master bus";
                res.state_version = state_version_;
                return res;
            }
            const size_t count = ch->fx_rack.count();
            if (cmd.slot_index < 0 || static_cast<size_t>(cmd.slot_index) >= count ||
                cmd.to_slot_index < 0 || static_cast<size_t>(cmd.to_slot_index) >= count) {
                res.success = false;
                res.error = "Move indices out of bounds";
                res.state_version = state_version_;
                return res;
            }
            if (cmd.slot_index < cmd.to_slot_index) {
                for (int s = cmd.slot_index; s < cmd.to_slot_index; ++s) ch->fx_rack.move_down(static_cast<size_t>(s));
            } else if (cmd.slot_index > cmd.to_slot_index) {
                for (int s = cmd.slot_index; s > cmd.to_slot_index; --s) ch->fx_rack.move_up(static_cast<size_t>(s));
            }
            ui_sync_channel_fx_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::ApplyPreset: {
            if (!caps.has_fx_rack || isMasterChannel(ch)) {
                res.success = false;
                res.error = "Insert FX Rack is disabled on the Master bus";
                res.state_version = state_version_;
                return res;
            }
            if (!is_safe_name(cmd.preset_name)) {
                res.success = false;
                res.error = "Invalid preset name";
                res.state_version = state_version_;
                return res;
            }
            const auto* preset = prismix::pedalboard::PedalboardPresetManager::instance().findPreset(cmd.preset_name);
            if (!preset) {
                res.success = false;
                res.error = "Preset not found: " + cmd.preset_name;
                res.state_version = state_version_;
                return res;
            }
            if (!prismix::pedalboard::PedalboardPresetManager::instance().applyPresetToRack(*preset, ch->fx_rack)) {
                res.success = false;
                res.error = "Failed to apply preset to rack";
                res.state_version = state_version_;
                return res;
            }
            ui_sync_channel_fx_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::SavePreset: {
            if (!caps.has_fx_rack || isMasterChannel(ch)) {
                res.success = false;
                res.error = "Insert FX Rack is disabled on the Master bus";
                res.state_version = state_version_;
                return res;
            }
            if (!is_safe_name(cmd.preset_name)) {
                res.success = false;
                res.error = "Invalid preset name (unsafe characters or too long)";
                res.state_version = state_version_;
                return res;
            }
            if (ch->fx_rack.count() == 0) {
                res.success = false;
                res.error = "Cannot save an empty rack as a preset";
                res.state_version = state_version_;
                return res;
            }
            std::string author = cmd.author.empty() ? "PRISMIX User" : cmd.author;
            auto preset = prismix::pedalboard::PedalboardPresetManager::instance().captureRack(
                cmd.preset_name, ch->fx_rack, author, cmd.description);
            std::string filename = "assets/presets/" + cmd.preset_name + ".prxp";
            if (!prismix::pedalboard::PedalboardPresetManager::instance().savePreset(filename, preset)) {
                res.success = false;
                res.error = "Failed to write preset file";
                res.state_version = state_version_;
                return res;
            }
            res.success = true;
            break;
        }

        case WebCommandType::SetInputSource: {
            if (!isInputChannel(ch)) {
                res.success = false;
                res.error = "Input source assignment is only valid on input channels";
                res.state_version = state_version_;
                return res;
            }
            if (cmd.int_value < -1 || cmd.int_value >= static_cast<int>(RoutingMatrix::NUM_INPUTS)) {
                res.success = false;
                res.error = "Input source index out of range (-1..15)";
                res.state_version = state_version_;
                return res;
            }
            ch->input_source_index = cmd.int_value;
            if (cmd.int_value2 >= -1 && cmd.int_value2 < static_cast<int>(RoutingMatrix::NUM_INPUTS)) {
                ch->input_source_index_r = cmd.int_value2;
            } else if (ch->format == ChannelFormat::Stereo && ch->input_source_index >= 0) {
                ch->input_source_index_r = std::min(ch->input_source_index + 1, static_cast<int>(RoutingMatrix::NUM_INPUTS) - 1);
            }
            if (ch->input_source_index >= 0) {
                std::snprintf(ch->input_label, sizeof(ch->input_label), "%s",
                              RoutingMatrix::get_input_source_name(ch->input_source_index));
            } else {
                std::snprintf(ch->input_label, sizeof(ch->input_label), "UNASSIGNED");
            }
            if (ch->id >= 1 && ch->id <= static_cast<int>(RoutingMatrix::NUM_CHANNELS)) {
                g_routing_matrix.set_channel_input_source(static_cast<size_t>(ch->id - 1), ch->input_source_index);
            }
            sync_channel_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::SetOutputRoute: {
            if (isMasterChannel(ch)) {
                res.success = false;
                res.error = "Master bus output route is fixed to hardware Main L/R";
                res.state_version = state_version_;
                return res;
            }
            if (cmd.has_freq) {
                int r = std::clamp(cmd.int_value, 0, 3);
                ch->output_route = static_cast<ChannelOutputRoute>(r);
            }
            if (cmd.has_gain) {
                ch->output_route_index = std::max(0, cmd.int_value2);
            }
            if (cmd.has_enabled) {
                ch->route_to_main = cmd.bool_value;
                if (isInputChannel(ch) && ch->id >= 1 && ch->id <= static_cast<int>(RoutingMatrix::NUM_CHANNELS)) {
                    g_routing_matrix.set_channel_main_lr(static_cast<size_t>(ch->id - 1), cmd.bool_value);
                }
            } else {
                ch->route_to_main = (ch->output_route == ChannelOutputRoute::MainLR);
                if (isInputChannel(ch) && ch->id >= 1 && ch->id <= static_cast<int>(RoutingMatrix::NUM_CHANNELS)) {
                    g_routing_matrix.set_channel_main_lr(static_cast<size_t>(ch->id - 1), ch->route_to_main);
                }
            }
            sync_channel_to_dsp(ch);
            res.success = true;
            break;
        }

        case WebCommandType::SetChannelBus: {
            if (!isInputChannel(ch) || ch->id < 1 || ch->id > static_cast<int>(RoutingMatrix::NUM_CHANNELS)) {
                res.success = false;
                res.error = "Channel bus matrix only applies to input channels 1..32";
                res.state_version = state_version_;
                return res;
            }
            if (cmd.bus_index < 0 || cmd.bus_index >= static_cast<int>(RoutingMatrix::NUM_BUSES)) {
                res.success = false;
                res.error = "Invalid bus index (0..7)";
                res.state_version = state_version_;
                return res;
            }
            g_routing_matrix.set_channel_bus(static_cast<size_t>(ch->id - 1), static_cast<size_t>(cmd.bus_index), cmd.bool_value);
            ch->bus_assign[static_cast<size_t>(cmd.bus_index)] = cmd.bool_value;
            res.success = true;
            break;
        }

        default: {
            res.success = false;
            res.error = "Unknown web command type";
            res.state_version = state_version_;
            return res;
        }
    }

    state_version_++;
    res.state_version = state_version_;
    if (change_cb_) {
        change_cb_(state_version_);
    }
    return res;
}

CommandResult WebControlBridge::execute_json_command(const std::string& json_str) {
    WebCommand cmd;
    std::string action = extract_string(json_str, "action");
    cmd.channel_id = extract_int(json_str, "channel", 1);
    cmd.slot_index = extract_int(json_str, "slot", 0);
    cmd.to_slot_index = extract_int(json_str, "to_slot", 0);
    cmd.band_index = extract_int(json_str, "band", 0);
    cmd.aux_index = extract_int(json_str, "aux", 0);
    cmd.bus_index = extract_int(json_str, "bus", 0);
    cmd.param_id = extract_string(json_str, "param_id");
    cmd.param_value = extract_float(json_str, "value", 0.0f);
    cmd.bool_value = extract_bool(json_str, "value", false);
    cmd.uri = extract_string(json_str, "uri");
    cmd.name = extract_string(json_str, "name");
    cmd.preset_name = extract_string(json_str, "preset");
    cmd.author = extract_string(json_str, "author");
    cmd.description = extract_string(json_str, "description");

    if (action == "select_channel") {
        cmd.type = WebCommandType::SelectChannel;
    } else if (action == "add_channel") {
        cmd.type = WebCommandType::AddChannel;
        cmd.int_value = extract_int(json_str, "format", 0);
        cmd.int_value2 = extract_int(json_str, "input_source", -1);
    } else if (action == "delete_channel") {
        cmd.type = WebCommandType::DeleteChannel;
    } else if (action == "rename_channel") {
        cmd.type = WebCommandType::RenameChannel;
    } else if (action == "set_fader") {
        cmd.type = WebCommandType::SetChannelFader;
    } else if (action == "set_return_gain") {
        cmd.type = WebCommandType::SetChannelReturnGain;
    } else if (action == "set_pan") {
        cmd.type = WebCommandType::SetChannelPan;
    } else if (action == "set_gain") {
        cmd.type = WebCommandType::SetChannelGain;
    } else if (action == "set_mute") {
        cmd.type = WebCommandType::SetChannelMute;
    } else if (action == "set_solo") {
        cmd.type = WebCommandType::SetChannelSolo;
    } else if (action == "set_polarity") {
        cmd.type = WebCommandType::SetChannelPolarity;
    } else if (action == "set_phantom") {
        cmd.type = WebCommandType::SetChannelPhantom;
    } else if (action == "set_hpf_enabled") {
        cmd.type = WebCommandType::SetChannelHpfEnabled;
    } else if (action == "set_hpf_freq") {
        cmd.type = WebCommandType::SetChannelHpfFreq;
    } else if (action == "set_format") {
        cmd.type = WebCommandType::SetChannelFormat;
        cmd.int_value = extract_int(json_str, "value", 0);
    } else if (action == "set_aux_send") {
        cmd.type = WebCommandType::SetAuxSendLevel;
    } else if (action == "set_aux_send_enabled") {
        cmd.type = WebCommandType::SetAuxSendEnabled;
    } else if (action == "set_aux_send_pre") {
        cmd.type = WebCommandType::SetAuxSendPreFader;
    } else if (action == "set_speaker_delay") {
        cmd.type = WebCommandType::SetSpeakerDelay;
        cmd.channel_id = 0;
        cmd.has_enabled = has_key(json_str, "enabled");
        cmd.bool_value = extract_bool(json_str, "enabled", false);
        cmd.has_filter_type = has_key(json_str, "link");
        cmd.filter_type = extract_bool(json_str, "link", true) ? 1 : 0;
        cmd.has_freq = has_key(json_str, "delay_l");
        cmd.param_value = extract_float(json_str, "delay_l", 0.0f);
        cmd.has_gain = has_key(json_str, "delay_r");
        cmd.float_value2 = extract_float(json_str, "delay_r", 0.0f);
    } else if (action == "set_eq_bypass") {
        cmd.type = WebCommandType::SetEqBypass;
    } else if (action == "set_peq_band") {
        cmd.type = WebCommandType::SetPeqBand;
        cmd.has_freq = has_key(json_str, "freq");
        cmd.param_value = extract_float(json_str, "freq", 1000.0f);
        cmd.has_gain = has_key(json_str, "gain");
        cmd.float_value2 = extract_float(json_str, "gain", 0.0f);
        cmd.has_q = has_key(json_str, "q");
        cmd.float_value3 = extract_float(json_str, "q", 1.0f);
        cmd.has_enabled = has_key(json_str, "enabled");
        cmd.bool_value = extract_bool(json_str, "enabled", true);
        cmd.has_filter_type = has_key(json_str, "filter_type");
        cmd.filter_type = extract_int(json_str, "filter_type", 1);
    } else if (action == "flatten_peq") {
        cmd.type = WebCommandType::FlattenPeq;
    } else if (action == "set_geq_mode") {
        cmd.type = WebCommandType::SetGeqMode;
    } else if (action == "set_geq_bypass") {
        cmd.type = WebCommandType::SetGeqBypass;
    } else if (action == "set_geq_band") {
        cmd.type = WebCommandType::SetGeqBand;
    } else if (action == "flatten_geq") {
        cmd.type = WebCommandType::FlattenGeq;
    } else if (action == "set_rta_config") {
        cmd.type = WebCommandType::SetRtaConfig;
        cmd.int_value = has_key(json_str, "mode") ? extract_int(json_str, "mode", -1) : -1;
        cmd.int_value2 = has_key(json_str, "source") ? extract_int(json_str, "source", -1) : -1;
        cmd.bus_index = has_key(json_str, "speed") ? extract_int(json_str, "speed", -1) : -1;
        cmd.has_enabled = has_key(json_str, "freeze");
        cmd.bool_value = extract_bool(json_str, "freeze", false);
        cmd.has_gain = extract_bool(json_str, "clear_peaks", false);
    } else if (action == "set_gate_enabled") {
        cmd.type = WebCommandType::SetGateEnabled;
    } else if (action == "set_gate_param") {
        cmd.type = WebCommandType::SetGateParam;
    } else if (action == "set_comp_bypass") {
        cmd.type = WebCommandType::SetCompBypass;
    } else if (action == "set_comp_param") {
        cmd.type = WebCommandType::SetCompParam;
    } else if (action == "set_limiter_enabled") {
        cmd.type = WebCommandType::SetLimiterEnabled;
    } else if (action == "set_limiter_param") {
        cmd.type = WebCommandType::SetLimiterParam;
    } else if (action == "set_sidechain") {
        cmd.type = WebCommandType::SetSidechain;
        cmd.has_enabled = has_key(json_str, "enabled");
        cmd.bool_value = extract_bool(json_str, "enabled", false);
        cmd.has_freq = has_key(json_str, "low");
        cmd.param_value = extract_float(json_str, "low", 80.0f);
        cmd.has_gain = has_key(json_str, "high");
        cmd.float_value2 = extract_float(json_str, "high", 8000.0f);
    } else if (action == "set_plugin_param") {
        cmd.type = WebCommandType::SetPluginParameter;
    } else if (action == "set_slot_bypass") {
        cmd.type = WebCommandType::SetSlotBypass;
    } else if (action == "add_plugin") {
        cmd.type = WebCommandType::AddPlugin;
    } else if (action == "remove_plugin") {
        cmd.type = WebCommandType::RemovePlugin;
    } else if (action == "move_plugin") {
        cmd.type = WebCommandType::MovePlugin;
    } else if (action == "apply_preset") {
        cmd.type = WebCommandType::ApplyPreset;
    } else if (action == "save_preset") {
        cmd.type = WebCommandType::SavePreset;
    } else if (action == "patchstorage_browse") {
        cmd.type = WebCommandType::PatchStorageBrowse;
        cmd.int_value = extract_int(json_str, "page", 1);
        cmd.name = extract_string(json_str, "query");
        cmd.int_value2 = extract_int(json_str, "category_id", 0);
    } else if (action == "patchstorage_install") {
        cmd.type = WebCommandType::PatchStorageInstall;
        cmd.int_value = extract_int(json_str, "patch_id", 0);
    } else if (action == "set_input_source") {
        cmd.type = WebCommandType::SetInputSource;
        cmd.int_value = extract_int(json_str, "source", -1);
        cmd.int_value2 = extract_int(json_str, "source_r", -1);
    } else if (action == "set_output_route") {
        cmd.type = WebCommandType::SetOutputRoute;
        cmd.has_freq = has_key(json_str, "route");
        cmd.int_value = extract_int(json_str, "route", 1);
        cmd.has_gain = has_key(json_str, "route_index");
        cmd.int_value2 = extract_int(json_str, "route_index", 0);
        cmd.has_enabled = has_key(json_str, "route_to_main");
        cmd.bool_value = extract_bool(json_str, "route_to_main", true);
    } else if (action == "set_channel_bus") {
        cmd.type = WebCommandType::SetChannelBus;
    } else if (action == "set_bus_main_lr") {
        cmd.type = WebCommandType::SetBusMainLr;
    } else if (action == "set_output_patch") {
        cmd.type = WebCommandType::SetOutputPatch;
        cmd.bus_index = extract_int(json_str, "output_port", 0);
        cmd.int_value = extract_int(json_str, "source_id", 0);
    } else if (action == "set_audio_mode") {
        cmd.type = WebCommandType::SetAudioMode;
        cmd.int_value = extract_int(json_str, "mode", 0);
    } else if (action == "start_audio") {
        cmd.type = WebCommandType::StartAudio;
    } else if (action == "stop_audio") {
        cmd.type = WebCommandType::StopAudio;
    } else if (action == "refresh_devices") {
        cmd.type = WebCommandType::RefreshDevices;
    } else if (action == "set_audio_config") {
        cmd.type = WebCommandType::SetAudioConfig;
        cmd.capture_device = extract_string(json_str, "capture_device");
        cmd.render_device = extract_string(json_str, "render_device");
        cmd.sample_rate = static_cast<uint32_t>(std::max(0, extract_int(json_str, "sample_rate", 0)));
        cmd.buffer_frames = static_cast<uint32_t>(std::max(0, extract_int(json_str, "buffer_frames", 0)));
    } else if (action == "set_generator_signal") {
        cmd.type = WebCommandType::SetGeneratorSignal;
        cmd.int_value = extract_int(json_str, "signal", 1);
    } else if (action == "save_scene") {
        cmd.type = WebCommandType::SaveScene;
    } else if (action == "load_scene") {
        cmd.type = WebCommandType::LoadScene;
    } else if (action == "set_theme_preset") {
        cmd.type = WebCommandType::SetThemePreset;
        cmd.int_value = extract_int(json_str, "preset", 0);
    } else if (action == "set_theme_color") {
        cmd.type = WebCommandType::SetThemeColor;
        cmd.int_value = extract_int(json_str, "role", 0);
        cmd.name = extract_string(json_str, "hex");
    } else if (action == "save_theme") {
        cmd.type = WebCommandType::SaveTheme;
    } else {
        CommandResult err;
        err.success = false;
        err.error = "Unrecognized action '" + action + "'";
        err.state_version = get_state_version();
        return err;
    }

    return execute_command(cmd);
}

} // namespace prismix::web
