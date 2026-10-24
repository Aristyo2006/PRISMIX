#include "web_control_bridge.h"
#include "../../model/mixer_state.h"
#include "../../model/plugin_model.h"
#include "../../model/pedalboard_preset.h"
#include "../../service/audio_service.h"
#include "../../ui/ui_audio_bridge.h"

#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>
#include <cctype>

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

// Lightweight JSON property extractor for command parsing
std::string extract_string(const std::string& json, const std::string& key) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + pattern.length());
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return "";
    size_t end = json.find('"', pos + 1);
    if (end == std::string::npos) return "";
    return json.substr(pos + 1, end - pos - 1);
}

float extract_float(const std::string& json, const std::string& key, float def = 0.0f) {
    std::string pattern = "\"" + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return def;
    pos = json.find(':', pos + pattern.length());
    if (pos == std::string::npos) return def;
    while (pos < json.length() && (json[pos] == ':' || json[pos] == ' ' || json[pos] == '\t')) pos++;
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
    while (pos < json.length() && (json[pos] == ':' || json[pos] == ' ' || json[pos] == '\t')) pos++;
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
    if (json.find("true", pos) != std::string::npos && json.find("true", pos) < pos + 10) return true;
    if (json.find("false", pos) != std::string::npos && json.find("false", pos) < pos + 10) return false;
    return def;
}

ChannelState* resolve_channel(int id) {
    if (id == 0 || id == 100) return &g_mixer_state.master;
    if (id >= 101 && id <= 104) return mixer_get_aux_bus(id - 101);
    return mixer_find_channel_by_id(id);
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

    ss << "{\n";
    ss << "  \"version\": " << state_version_ << ",\n";
    ss << "  \"selected_channel\": " << g_mixer_state.selected_channel << ",\n";
    ss << "  \"selected_aux_bus\": " << g_mixer_state.selected_aux_bus << ",\n";

    // Channels array
    ss << "  \"channels\": [\n";
    for (size_t i = 0; i < g_mixer_state.channels.size(); ++i) {
        const auto* ch = g_mixer_state.channels.get(i);
        if (!ch) continue;
        if (i > 0) ss << ",\n";
        ss << "    {\n";
        ss << "      \"id\": " << ch->id << ",\n";
        ss << "      \"name\": \"" << escape_json(ch->name) << "\",\n";
        ss << "      \"type\": " << static_cast<int>(ch->type) << ",\n";
        ss << "      \"format\": " << static_cast<int>(ch->format) << ",\n";
        ss << "      \"fader\": " << ch->fader << ",\n";
        ss << "      \"pan\": " << ch->pan << ",\n";
        ss << "      \"gain\": " << ch->gain << ",\n";
        ss << "      \"mute\": " << (ch->mute ? "true" : "false") << ",\n";
        ss << "      \"solo\": " << (ch->solo ? "true" : "false") << ",\n";
        if (include_meters) {
            ss << "      \"peak_l\": " << ch->peak_l << ",\n";
            ss << "      \"peak_r\": " << ch->peak_r << ",\n";
            ss << "      \"fx_in_peak_l\": " << ch->fx_in_peak_l << ",\n";
            ss << "      \"fx_in_peak_r\": " << ch->fx_in_peak_r << ",\n";
            ss << "      \"fx_out_peak_l\": " << ch->fx_out_peak_l << ",\n";
            ss << "      \"fx_out_peak_r\": " << ch->fx_out_peak_r << ",\n";
        }

        // FX Rack
        ss << "      \"fx_rack\": [\n";
        for (size_t s = 0; s < ch->fx_rack.count(); ++s) {
            const auto* inst = ch->fx_rack.get_slot(s);
            if (!inst) continue;
            if (s > 0) ss << ",\n";
            ss << "        {\n";
            ss << "          \"slot\": " << s << ",\n";
            ss << "          \"uri\": \"" << escape_json(inst->get_uri()) << "\",\n";
            ss << "          \"name\": \"" << escape_json(inst->get_name()) << "\",\n";
            ss << "          \"category\": \"" << escape_json(inst->get_category()) << "\",\n";
            ss << "          \"source_type\": " << static_cast<int>(inst->get_source_type()) << ",\n";
            ss << "          \"bypassed\": " << (inst->is_bypassed() ? "true" : "false") << ",\n";
            ss << "          \"enabled\": " << (inst->is_enabled() ? "true" : "false") << ",\n";
            ss << "          \"parameters\": [\n";
            for (size_t p = 0; p < inst->parameter_count(); ++p) {
                const auto* param = inst->get_parameter(p);
                if (!param) continue;
                if (p > 0) ss << ",\n";
                ss << "            {\n";
                ss << "              \"id\": \"" << escape_json(param->id) << "\",\n";
                ss << "              \"name\": \"" << escape_json(param->name) << "\",\n";
                ss << "              \"unit\": \"" << escape_json(param->unit) << "\",\n";
                ss << "              \"value\": " << param->current_val << ",\n";
                ss << "              \"min\": " << param->min_val << ",\n";
                ss << "              \"max\": " << param->max_val << ",\n";
                ss << "              \"default\": " << param->default_val << ",\n";
                ss << "              \"step\": " << param->step << ",\n";
                ss << "              \"is_toggle\": " << (param->is_toggle ? "true" : "false") << "\n";
                ss << "            }";
            }
            ss << "\n          ]\n";
            ss << "        }";
        }
        ss << "\n      ]\n";
        ss << "    }";
    }
    ss << "\n  ],\n";

    // AUX buses array
    ss << "  \"aux_buses\": [\n";
    for (size_t a = 0; a < 4; ++a) {
        const auto& aux = g_mixer_state.aux_buses[a];
        if (a > 0) ss << ",\n";
        ss << "    {\n";
        ss << "      \"id\": " << aux.id << ",\n";
        ss << "      \"name\": \"" << escape_json(aux.name) << "\",\n";
        ss << "      \"fader\": " << aux.fader << ",\n";
        ss << "      \"return_gain\": " << aux.return_gain << ",\n";
        ss << "      \"pan\": " << aux.pan << ",\n";
        ss << "      \"mute\": " << (aux.mute ? "true" : "false") << ",\n";
        ss << "      \"solo\": " << (aux.solo ? "true" : "false") << ",\n";
        if (include_meters) {
            ss << "      \"peak_l\": " << aux.peak_l << ",\n";
            ss << "      \"peak_r\": " << aux.peak_r << ",\n";
        }
        ss << "      \"fx_rack_count\": " << aux.fx_rack.count() << "\n";
        ss << "    }";
    }
    ss << "\n  ],\n";

    // Master bus
    const auto& m = g_mixer_state.master;
    ss << "  \"master\": {\n";
    ss << "    \"id\": " << m.id << ",\n";
    ss << "    \"name\": \"" << escape_json(m.name) << "\",\n";
    ss << "    \"fader\": " << m.fader << ",\n";
    ss << "    \"mute\": " << (m.mute ? "true" : "false") << ",\n";
    ss << "    \"comp_gr\": " << m.comp_gr << ",\n";
    ss << "    \"limiter_gr\": " << m.limiter_gr_db << ",\n";
    ss << "    \"speaker_delay_enabled\": " << (m.speaker_delay_enabled ? "true" : "false") << ",\n";
    if (include_meters) {
        ss << "    \"peak_l\": " << m.peak_l << ",\n";
        ss << "    \"peak_r\": " << m.peak_r << "\n";
    } else {
        ss << "    \"peak_l\": 0.0,\n";
        ss << "    \"peak_r\": 0.0\n";
    }
    ss << "  }\n";
    ss << "}\n";

    return ss.str();
}

std::string WebControlBridge::get_catalog_json() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream ss;
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
        ss << "      \"param_count\": " << p.parameters.size() << "\n";
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
    ss << "\n  ]\n";
    ss << "}\n";

    return ss.str();
}

CommandResult WebControlBridge::execute_command(const WebCommand& cmd) {
    std::lock_guard<std::mutex> lock(mutex_);
    CommandResult res;

    ChannelState* ch = resolve_channel(cmd.channel_id);
    if (!ch) {
        res.success = false;
        res.error = "Channel ID " + std::to_string(cmd.channel_id) + " not found";
        res.state_version = state_version_;
        return res;
    }

    switch (cmd.type) {
        case WebCommandType::SetChannelFader: {
            ch->fader = std::clamp(cmd.param_value, 0.0f, 1.0f);
            if (isMasterChannel(ch)) {
                audio_service().setGainDb((ch->fader - 0.86f) * (10.0f / 0.14f));
            } else if (!isAuxBusChannel(ch)) {
                const float gainDb = (ch->fader <= 0.86f)
                    ? -60.0f + ch->fader * (60.0f / 0.86f)
                    : (ch->fader - 0.86f) * (10.0f / 0.14f);
                audio_service().updateLiveStrip(ch->id, gainDb, (ch->pan - 0.5f) * 2.0f, ch->mute, ch->solo);
            }
            res.success = true;
            break;
        }

        case WebCommandType::SetChannelPan: {
            ch->pan = std::clamp(cmd.param_value, 0.0f, 1.0f);
            if (isMasterChannel(ch)) {
                audio_service().setPan((ch->pan - 0.5f) * 2.0f);
            } else if (!isAuxBusChannel(ch)) {
                const float gainDb = (ch->fader <= 0.86f)
                    ? -60.0f + ch->fader * (60.0f / 0.86f)
                    : (ch->fader - 0.86f) * (10.0f / 0.14f);
                audio_service().updateLiveStrip(ch->id, gainDb, (ch->pan - 0.5f) * 2.0f, ch->mute, ch->solo);
            }
            res.success = true;
            break;
        }

        case WebCommandType::SetChannelMute: {
            ch->mute = cmd.bool_value;
            if (!isMasterChannel(ch) && !isAuxBusChannel(ch)) {
                const float gainDb = (ch->fader <= 0.86f)
                    ? -60.0f + ch->fader * (60.0f / 0.86f)
                    : (ch->fader - 0.86f) * (10.0f / 0.14f);
                audio_service().updateLiveStrip(ch->id, gainDb, (ch->pan - 0.5f) * 2.0f, ch->mute, ch->solo);
            }
            res.success = true;
            break;
        }

        case WebCommandType::SetChannelSolo: {
            ch->solo = cmd.bool_value;
            if (!isMasterChannel(ch) && !isAuxBusChannel(ch)) {
                const float gainDb = (ch->fader <= 0.86f)
                    ? -60.0f + ch->fader * (60.0f / 0.86f)
                    : (ch->fader - 0.86f) * (10.0f / 0.14f);
                audio_service().updateLiveStrip(ch->id, gainDb, (ch->pan - 0.5f) * 2.0f, ch->mute, ch->solo);
            }
            res.success = true;
            break;
        }

        case WebCommandType::SetPluginParameter: {
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
    cmd.param_id = extract_string(json_str, "param_id");
    cmd.param_value = extract_float(json_str, "value", 0.0f);
    cmd.bool_value = extract_bool(json_str, "value", false);
    cmd.uri = extract_string(json_str, "uri");
    cmd.preset_name = extract_string(json_str, "preset");
    cmd.author = extract_string(json_str, "author");
    cmd.description = extract_string(json_str, "description");

    if (action == "set_fader") {
        cmd.type = WebCommandType::SetChannelFader;
    } else if (action == "set_pan") {
        cmd.type = WebCommandType::SetChannelPan;
    } else if (action == "set_mute") {
        cmd.type = WebCommandType::SetChannelMute;
    } else if (action == "set_solo") {
        cmd.type = WebCommandType::SetChannelSolo;
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

