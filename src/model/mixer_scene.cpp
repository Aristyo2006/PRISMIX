#include "mixer_scene.h"

#include "mixer_state.h"
#include "routing_state.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <set>
#include <utility>
#include <vector>

namespace {
struct SavedPlugin {
    std::string uri;
    bool bypassed = false;
    bool enabled = true;
    std::vector<std::pair<std::string, float>> parameters;
};

struct SavedStrip {
    int id = 0;
    int format = 0; // 0: Mono, 1: Stereo
    std::string name;
    std::string inputLabel;
    int inputSource = -1;
    int inputSourceR = -1;
    float fader = 0.86f;
    float returnGain = 0.86f;
    float gain = 0.5f;
    float pan = 0.5f;
    bool mute = false;
    bool solo = false;
    bool routeToMain = true;
    int outputRoute = static_cast<int>(ChannelOutputRoute::MainLR);
    int outputRouteIndex = 0;
    bool polarityInverted = false;
    bool hpfOn = true;
    float hpfFrequency = 80.0f;
    bool eqBypass = false;
    bool geqMode = false;
    bool geqBypass = false;
    std::array<float, 4> eqFrequency{80.0f, 350.0f, 2500.0f, 10000.0f};
    std::array<float, 4> eqGain{};
    std::array<float, 4> eqQ{0.7f, 1.0f, 1.0f, 0.7f};
    std::array<int, 4> eqType{0, 1, 1, 2};
    std::array<bool, 4> eqEnabled{true, true, true, true};
    bool compBypass = false;
    float compThreshold = -18.0f;
    float compRatio = 4.0f;
    float compAttack = 10.0f;
    float compRelease = 80.0f;
    float compKnee = 6.0f;
    float compMakeup = 0.0f;
    bool limiterEnabled = false;
    float limiterCeilingDb = -0.1f;
    float limiterReleaseMs = 50.0f;
    bool gateOn = true;
    float gateThreshold = -48.0f;
    float gateRange = -60.0f;
    float gateAttack = 5.0f;
    float gateHold = 50.0f;
    float gateRelease = 120.0f;
    std::array<float, 4> sends{0.0f, 0.0f, 0.0f, 0.0f};
    std::array<bool, 4> sendEnabled{true, true, true, true};
    std::array<bool, 4> sendPreFader{false, false, false, false};
    std::array<float, GraphicEqModel::NUM_BANDS> geqGain{};
    std::vector<SavedPlugin> plugins;
};

bool fail(std::string* error, const char* message) {
    if (error) *error = message;
    return false;
}

void write_strip(std::ostream& out, const ChannelState& ch) {
    out << ch.id << ' ' << std::quoted(std::string(ch.name)) << ' '
        << std::quoted(std::string(ch.input_label)) << ' '
        << ch.input_source_index << ' ' << ch.fader << ' ' << ch.gain << ' ' << ch.pan << ' '
        << ch.mute << ' ' << ch.solo << ' '
        << static_cast<int>(ch.output_route) << ' ' << ch.output_route_index << ' '
        << ch.polarity_inverted << ' ' << ch.hpf_on << ' ' << ch.hpf_frequency << ' '
        << ch.eq_bypass << ' ' << ch.geq_mode_active << ' ' << ch.geq_bypass << '\n';
    for (size_t band = 0; band < 4; ++band)
        out << ch.eq_freq[band] << ' ' << ch.eq_gain[band] << ' ' << ch.eq_q[band] << ' '
            << ch.eq_enabled[band] << ' ' << ch.eq_filter_type[band] << '\n';
    out << ch.comp_bypass << ' ' << ch.comp_thresh << ' ' << ch.comp_ratio << ' '
        << ch.comp_attack << ' ' << ch.comp_release << ' ' << ch.comp_knee << ' ' << ch.comp_makeup << '\n'
        << ch.gate_on << ' ' << ch.gate_thresh << ' ' << ch.gate_range << ' '
        << ch.gate_attack << ' ' << ch.gate_hold << ' ' << ch.gate_release << '\n';
    for (float gain : ch.geq.get_gains()) out << gain << ' ';
    out << '\n' << ch.fx_rack.count() << '\n';
    for (size_t slot = 0; slot < ch.fx_rack.count(); ++slot) {
        const auto* fx = ch.fx_rack.get_slot(slot);
        if (!fx) continue;
        out << std::quoted(fx->get_uri()) << ' ' << fx->is_bypassed() << ' ' << fx->is_enabled() << ' '
            << fx->parameter_count() << '\n';
        for (size_t i = 0; i < fx->parameter_count(); ++i) {
            const auto* parameter = fx->get_parameter(i);
            if (parameter) out << std::quoted(parameter->id) << ' ' << parameter->current_val << '\n';
        }
    }
    out << static_cast<int>(ch.format) << ' ' << ch.input_source_index_r << ' '
        << ch.return_gain << ' ' << ch.route_to_main << ' '
        << ch.limiter_enabled << ' ' << ch.limiter_ceiling_db << ' ' << ch.limiter_release_ms << '\n';
    for (size_t a = 0; a < 4; ++a) {
        out << ch.sends[a] << ' ' << ch.send_enabled[a] << ' ' << ch.send_pre_fader[a] << ' ';
    }
    out << '\n';
}

bool read_strip(std::istream& in, unsigned version, SavedStrip& strip, bool isBus, std::string* error) {
    if (!(in >> strip.id >> std::quoted(strip.name)))
        return fail(error, "Mixer scene strip entry is incomplete.");
    if (version >= 2 && !(in >> std::quoted(strip.inputLabel)))
        return fail(error, "Mixer scene input label is incomplete.");
    if (!(in >> strip.inputSource >> strip.fader >> strip.gain >> strip.pan >> strip.mute >> strip.solo))
        return fail(error, "Mixer scene strip controls are incomplete.");
    if (version == 1) {
        bool main = false;
        if (!(in >> main)) return fail(error, "Mixer scene routing entry is incomplete.");
        strip.outputRoute = main ? static_cast<int>(ChannelOutputRoute::MainLR)
                                 : static_cast<int>(ChannelOutputRoute::None);
    } else if (!(in >> strip.outputRoute >> strip.outputRouteIndex)) {
        return fail(error, "Mixer scene output route entry is incomplete.");
    }
    size_t pluginCount = 0;
    if (version >= 3) {
        if (!(in >> strip.polarityInverted >> strip.hpfOn >> strip.hpfFrequency >>
              strip.eqBypass >> strip.geqMode >> strip.geqBypass))
            return fail(error, "Mixer scene preamp or EQ settings are incomplete.");
        for (size_t band = 0; band < 4; ++band) {
            if (!(in >> strip.eqFrequency[band] >> strip.eqGain[band] >> strip.eqQ[band] >>
                  strip.eqEnabled[band] >> strip.eqType[band]))
                return fail(error, "Mixer scene parametric EQ band is incomplete.");
        }
        if (!(in >> strip.compBypass >> strip.compThreshold >> strip.compRatio >> strip.compAttack >>
              strip.compRelease >> strip.compKnee >> strip.compMakeup >> strip.gateOn >>
              strip.gateThreshold >> strip.gateRange >> strip.gateAttack >> strip.gateHold >> strip.gateRelease))
            return fail(error, "Mixer scene dynamics settings are incomplete.");
        for (float& gain : strip.geqGain)
            if (!(in >> gain)) return fail(error, "Mixer scene graphic EQ settings are incomplete.");
        if (!(in >> pluginCount) || pluginCount > PluginRack::MAX_SLOTS)
            return fail(error, "Mixer scene FX rack size is invalid.");
        for (size_t pluginIndex = 0; pluginIndex < pluginCount; ++pluginIndex) {
            SavedPlugin plugin;
            size_t parameterCount = 0;
            if (!(in >> std::quoted(plugin.uri) >> plugin.bypassed >> plugin.enabled >> parameterCount) ||
                plugin.uri.empty() || parameterCount > 64)
                return fail(error, "Mixer scene FX rack entry is incomplete.");
            if (!PluginRegistry::instance().find_metadata(plugin.uri))
                return fail(error, "Mixer scene references an unknown effect.");
            for (size_t p = 0; p < parameterCount; ++p) {
                std::string id;
                float value = 0.0f;
                if (!(in >> std::quoted(id) >> value) || id.empty() || !std::isfinite(value))
                    return fail(error, "Mixer scene effect parameter is malformed.");
                plugin.parameters.emplace_back(std::move(id), value);
            }
            strip.plugins.push_back(std::move(plugin));
        }
    } else {
        SavedPlugin plugin;
        size_t parameterCount = 0;
        if (!(in >> std::quoted(plugin.uri) >> plugin.bypassed >> parameterCount))
            return fail(error, "Mixer scene insert entry is incomplete.");
        if (parameterCount > 64) return fail(error, "Mixer scene insert parameter count is invalid.");
        if (!plugin.uri.empty()) {
            if (!PluginRegistry::instance().find_metadata(plugin.uri))
                return fail(error, "Mixer scene references an unknown effect.");
            for (size_t p = 0; p < parameterCount; ++p) {
                std::string id;
                float value = 0.0f;
                if (!(in >> std::quoted(id) >> value) || id.empty() || !std::isfinite(value))
                    return fail(error, "Mixer scene effect parameter is malformed.");
                plugin.parameters.emplace_back(std::move(id), value);
            }
            strip.plugins.push_back(std::move(plugin));
        } else if (parameterCount != 0) {
            return fail(error, "Mixer scene has effect parameters without an effect.");
        }
    }
    if (version >= 4) {
        if (!(in >> strip.format >> strip.inputSourceR >> strip.returnGain >> strip.routeToMain >>
              strip.limiterEnabled >> strip.limiterCeilingDb >> strip.limiterReleaseMs))
            return fail(error, "Mixer scene v4 format/limiter settings are incomplete.");
        for (size_t a = 0; a < 4; ++a) {
            if (!(in >> strip.sends[a] >> strip.sendEnabled[a] >> strip.sendPreFader[a]))
                return fail(error, "Mixer scene v4 AUX send settings are incomplete.");
        }
        if ((strip.format != 0 && strip.format != 1) || strip.inputSourceR < -1 ||
            !std::isfinite(strip.returnGain) || strip.returnGain < 0.0f || strip.returnGain > 1.0f ||
            !std::isfinite(strip.limiterCeilingDb) || strip.limiterCeilingDb < -60.0f || strip.limiterCeilingDb > 0.0f ||
            !std::isfinite(strip.limiterReleaseMs) || strip.limiterReleaseMs < 1.0f || strip.limiterReleaseMs > 2000.0f)
            return fail(error, "Mixer scene v4 format/limiter values are out of range.");
        for (size_t a = 0; a < 4; ++a) {
            if (!std::isfinite(strip.sends[a]) || strip.sends[a] < 0.0f || strip.sends[a] > 1.0f)
                return fail(error, "Mixer scene v4 AUX send level is out of range.");
        }
    }
    if (!isBus) {
        if (strip.id < 1 || strip.id > MAX_CHANNELS)
            return fail(error, "Mixer scene strip ID is out of range.");
    }
    if (strip.name.empty() || strip.name.size() > 31 || strip.inputLabel.size() > 31 ||
        strip.inputSource < -1 ||
        strip.outputRoute < static_cast<int>(ChannelOutputRoute::None) ||
        strip.outputRoute > static_cast<int>(ChannelOutputRoute::Omni) || strip.outputRouteIndex < 0 ||
        !std::isfinite(strip.fader) || strip.fader < 0.0f || strip.fader > 1.0f ||
        !std::isfinite(strip.gain) || strip.gain < 0.0f || strip.gain > 1.0f ||
        !std::isfinite(strip.pan) || strip.pan < 0.0f || strip.pan > 1.0f)
        return fail(error, "Mixer scene strip values are out of range.");
    if (version >= 3) {
        if (!std::isfinite(strip.hpfFrequency) || strip.hpfFrequency < 20.0f || strip.hpfFrequency > 20000.0f ||
            !std::isfinite(strip.compThreshold) || !std::isfinite(strip.compRatio) || strip.compRatio < 1.0f || strip.compRatio > 20.0f ||
            !std::isfinite(strip.compAttack) || !std::isfinite(strip.compRelease) ||
            !std::isfinite(strip.compKnee) || !std::isfinite(strip.compMakeup) ||
            !std::isfinite(strip.gateThreshold) || !std::isfinite(strip.gateRange) ||
            !std::isfinite(strip.gateAttack) || !std::isfinite(strip.gateHold) || !std::isfinite(strip.gateRelease))
            return fail(error, "Mixer scene DSP settings are out of range.");
        for (size_t band = 0; band < 4; ++band)
            if (!std::isfinite(strip.eqFrequency[band]) || !std::isfinite(strip.eqGain[band]) ||
                !std::isfinite(strip.eqQ[band]) || strip.eqType[band] < 0 || strip.eqType[band] > 4)
                return fail(error, "Mixer scene parametric EQ settings are out of range.");
        for (float gain : strip.geqGain)
            if (!std::isfinite(gain) || gain < -12.0f || gain > 12.0f)
                return fail(error, "Mixer scene graphic EQ settings are out of range.");
    }
    return true;
}

void apply_saved_strip(const SavedStrip& saved, ChannelState& channel) {
    channel.id = saved.id;
    channel.format = saved.format == 1 ? ChannelFormat::Stereo : ChannelFormat::Mono;
    std::snprintf(channel.name, sizeof(channel.name), "%s", saved.name.c_str());
    channel.input_source_index = saved.inputSource;
    channel.input_source_index_r = saved.inputSourceR;
    std::snprintf(channel.input_label, sizeof(channel.input_label), "%s",
                  saved.inputLabel.empty() ? RoutingMatrix::get_input_source_name(saved.inputSource)
                                           : saved.inputLabel.c_str());
    channel.output_route = static_cast<ChannelOutputRoute>(saved.outputRoute);
    channel.output_route_index = saved.outputRouteIndex;
    channel.return_gain = saved.returnGain;
    channel.route_to_main = saved.routeToMain;
    channel.polarity_inverted = saved.polarityInverted;
    channel.hpf_on = saved.hpfOn;
    channel.hpf_frequency = saved.hpfFrequency;
    channel.eq_bypass = saved.eqBypass;
    channel.geq_mode_active = saved.geqMode;
    channel.geq_bypass = saved.geqBypass;
    channel.eq_freq = saved.eqFrequency;
    channel.eq_gain = saved.eqGain;
    channel.eq_q = saved.eqQ;
    channel.eq_enabled = saved.eqEnabled;
    channel.eq_filter_type = saved.eqType;
    for (size_t band = 0; band < 4; ++band)
        channel.eq_shelf[band] = saved.eqType[band] == 0 || saved.eqType[band] == 2;
    channel.comp_bypass = saved.compBypass;
    channel.comp_thresh = saved.compThreshold;
    channel.comp_ratio = saved.compRatio;
    channel.comp_attack = saved.compAttack;
    channel.comp_release = saved.compRelease;
    channel.comp_knee = saved.compKnee;
    channel.comp_makeup = saved.compMakeup;
    channel.limiter_enabled = saved.limiterEnabled;
    channel.limiter_ceiling_db = saved.limiterCeilingDb;
    channel.limiter_release_ms = saved.limiterReleaseMs;
    channel.gate_on = isMasterChannel(&channel) ? false : saved.gateOn;
    if (isMasterChannel(&channel)) {
        channel.gate_open = true;
        channel.gate_gr_db = 0.0f;
    }
    channel.gate_thresh = saved.gateThreshold;
    channel.gate_range = saved.gateRange;
    channel.gate_attack = saved.gateAttack;
    channel.gate_hold = saved.gateHold;
    channel.gate_release = saved.gateRelease;
    channel.sends = saved.sends;
    channel.send_enabled = saved.sendEnabled;
    channel.send_pre_fader = saved.sendPreFader;
    channel.geq.set_gains(saved.geqGain);
    channel.geq.set_bypass(saved.geqBypass);
    channel.fader = saved.fader;
    channel.gain = saved.gain;
    channel.pan = saved.pan;
    channel.mute = saved.mute;
    channel.solo = saved.solo;
    channel.fx_rack.clear();
    for (const auto& savedFx : saved.plugins) {
        if (!channel.fx_rack.add_plugin(savedFx.uri)) continue;
        auto* fx = channel.fx_rack.get_slot(channel.fx_rack.count() - 1);
        fx->set_bypassed(savedFx.bypassed);
        fx->set_enabled(savedFx.enabled);
        for (const auto& parameter : savedFx.parameters)
            fx->set_parameter_value(parameter.first, parameter.second);
    }
}
}

std::string default_mixer_scene_path() {
    const char* appData = std::getenv("APPDATA");
    std::filesystem::path base = appData && *appData
        ? std::filesystem::path(appData) : std::filesystem::current_path();
    std::filesystem::path prismix_path = base / "PRISMIX" / "mixer_scene_v1.cfg";
    std::filesystem::path audiocore_path = base / "AudioCore" / "mixer_scene_v1.cfg";

    // If new PRISMIX session file exists, use it
    if (std::filesystem::exists(prismix_path)) {
        return prismix_path.string();
    }
    // Backward compatibility: If legacy AudioCore session file exists, fallback/migrate from it
    if (std::filesystem::exists(audiocore_path)) {
        return audiocore_path.string();
    }
    // Otherwise, create and use PRISMIX directory for fresh sessions
    std::error_code ec;
    std::filesystem::create_directories(base / "PRISMIX", ec);
    return prismix_path.string();
}

bool save_mixer_scene(const std::string& path, std::string* error) {
    if (g_mixer_state.channels.size() > MAX_CHANNELS)
        return fail(error, "Mixer strip count exceeds the scene format limit.");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return fail(error, "Could not open mixer scene file for writing.");
    out << "PRISMIX_MIXER_SCENE 4\n" << g_mixer_state.channels.size() << '\n';
    out << std::setprecision(9);
    for (const auto& ch : g_mixer_state.channels) {
        write_strip(out, ch);
    }
    write_strip(out, g_mixer_state.master);
    for (const auto& aux : g_mixer_state.aux_buses) {
        write_strip(out, aux);
    }
    if (!out) return fail(error, "Could not write the complete mixer scene.");
    return true;
}

bool load_mixer_scene(const std::string& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return fail(error, "Mixer scene file was not found.");
    std::string magic;
    unsigned version = 0;
    size_t count = 0;
    if (!(in >> magic >> version) ||
        (magic != "PRISMIX_MIXER_SCENE" && magic != "AUDIOCORE_MIXER_SCENE") ||
        version < 1 || version > 4)
        return fail(error, "Mixer scene header or version is invalid.");
    if (!(in >> count) || count > MAX_CHANNELS) return fail(error, "Mixer scene strip count is invalid.");

    std::vector<SavedStrip> strips;
    strips.reserve(count);
    std::set<int> ids;
    for (size_t i = 0; i < count; ++i) {
        SavedStrip strip;
        if (!read_strip(in, version, strip, false, error)) return false;
        if (!ids.insert(strip.id).second)
            return fail(error, "Mixer scene strip IDs must be unique.");
        strips.push_back(std::move(strip));
    }

    SavedStrip savedMaster;
    std::array<SavedStrip, 4> savedAux{};
    bool hasBusData = false;
    if (version >= 4) {
        in >> std::ws;
        if (!in.eof()) {
            if (!read_strip(in, version, savedMaster, true, error)) return false;
            for (size_t a = 0; a < 4; ++a) {
                if (!read_strip(in, version, savedAux[a], true, error)) return false;
            }
            hasBusData = true;
        }
    }

    in >> std::ws;
    if (!in.eof()) return fail(error, "Mixer scene has trailing data.");

    mixer_state_init();
    g_routing_matrix = RoutingMatrix{};
    for (const auto& saved : strips) {
        ChannelState channel{};
        apply_saved_strip(saved, channel);
        g_mixer_state.channels.push_back(channel);
        g_routing_matrix.set_channel_input_source(static_cast<size_t>(saved.id - 1), saved.inputSource);
        g_routing_matrix.set_channel_main_lr(static_cast<size_t>(saved.id - 1),
            channel.output_route == ChannelOutputRoute::MainLR);
    }
    if (hasBusData) {
        apply_saved_strip(savedMaster, g_mixer_state.master);
        g_mixer_state.master.id = 0;
        g_mixer_state.master.format = ChannelFormat::Stereo;
        for (size_t a = 0; a < 4; ++a) {
            apply_saved_strip(savedAux[a], g_mixer_state.aux_buses[a]);
            g_mixer_state.aux_buses[a].id = 101 + static_cast<int>(a);
        }
    }
    return true;
}
