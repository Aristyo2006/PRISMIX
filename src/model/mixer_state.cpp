#include "mixer_state.h"
#include <cstdio>

MixerState g_mixer_state;

void mixer_state_init() {
    g_mixer_state = MixerState{};
    g_mixer_state.channels.clear();
    g_mixer_state.channels.reserve(64);

    g_mixer_state.master = ChannelState{};
    g_mixer_state.master.id = 0;
    g_mixer_state.master.type = ChannelType::Master;
    g_mixer_state.master.format = ChannelFormat::Stereo;
    g_mixer_state.master.limiter_enabled = true;
    g_mixer_state.master.limiter_ceiling_db = -0.1f;
    g_mixer_state.master.limiter_release_ms = 50.0f;
    g_mixer_state.master.gate_on = false;
    g_mixer_state.master.hpf_on = false;
    std::snprintf(g_mixer_state.master.name, sizeof(g_mixer_state.master.name), "MAIN L/R");
    std::snprintf(g_mixer_state.master.input_label, sizeof(g_mixer_state.master.input_label), "STEREO BUS");
    g_mixer_state.master.input_peak_l = g_mixer_state.master.input_peak_r = -90.0f;
    g_mixer_state.master.input_rms_l = g_mixer_state.master.input_rms_r = -90.0f;
    g_mixer_state.master.input_peak_hold_l = g_mixer_state.master.input_peak_hold_r = -90.0f;
    g_mixer_state.master.peak_l = g_mixer_state.master.peak_r = -90.0f;
    g_mixer_state.master.rms_l = g_mixer_state.master.rms_r = -90.0f;
    g_mixer_state.master.peak_hold_l = g_mixer_state.master.peak_hold_r = -90.0f;
    g_mixer_state.master.pre_limiter_peak_l = g_mixer_state.master.pre_limiter_peak_r = -90.0f;
    g_mixer_state.master.fx_rack.set_owner_context(0, false, true);

    for (size_t a = 0; a < g_mixer_state.aux_buses.size(); ++a) {
        auto& aux = g_mixer_state.aux_buses[a];
        aux = ChannelState{};
        aux.id = 101 + static_cast<int>(a);
        aux.type = ChannelType::Aux;
        aux.format = ChannelFormat::Stereo;
        aux.fader = 0.86f;
        aux.return_gain = 0.86f;
        aux.pan = 0.5f;
        aux.route_to_main = true;
        aux.output_route = ChannelOutputRoute::MainLR;
        aux.output_route_index = static_cast<int>(a);
        aux.gate_on = false;
        aux.hpf_on = false;
        aux.comp_bypass = true;
        aux.limiter_enabled = true;
        aux.limiter_ceiling_db = -0.1f;
        aux.limiter_release_ms = 50.0f;
        std::snprintf(aux.name, sizeof(aux.name), "AUX %02zu", a + 1);
        std::snprintf(aux.input_label, sizeof(aux.input_label), "AUX SEND BUS %02zu", a + 1);
        aux.input_peak_l = aux.input_peak_r = -90.0f;
        aux.input_rms_l = aux.input_rms_r = -90.0f;
        aux.input_peak_hold_l = aux.input_peak_hold_r = -90.0f;
        aux.peak_l = aux.peak_r = -90.0f;
        aux.rms_l = aux.rms_r = -90.0f;
        aux.peak_hold_l = aux.peak_hold_r = -90.0f;
        aux.pre_limiter_peak_l = aux.pre_limiter_peak_r = -90.0f;
        aux.fx_rack.set_owner_context(aux.id, true, true);
    }

    for (size_t d = 0; d < g_mixer_state.dca_groups.size(); ++d) {
        auto& dca = g_mixer_state.dca_groups[d];
        dca.id = static_cast<int>(d + 1);
        std::snprintf(dca.name, sizeof(dca.name), "DCA %02zu", d + 1);
        dca.fader = 0.86f;
        dca.mute = false;
        dca.channel_mask = 0;
        dca.assigned_count = 0;
    }

    for (size_t g = 0; g < g_mixer_state.group_buses.size(); ++g) {
        auto& grp = g_mixer_state.group_buses[g];
        grp.id = static_cast<int>(g + 1);
        std::snprintf(grp.name, sizeof(grp.name), "GRP %02zu", g + 1);
        grp.format = ChannelFormat::Stereo;
        grp.fader = 0.86f;
        grp.pan = 0.5f;
        grp.mute = false;
        grp.solo = false;
        grp.route_to_main = true;
        grp.peak_l = grp.peak_r = -90.0f;
        grp.rms_l = grp.rms_r = -90.0f;
        grp.member_mask = 0;
    }

    g_mixer_state.selected_channel = -1;
    g_mixer_state.selected_aux_bus = -1;
    g_mixer_state.master.select = true;
}

ChannelState* mixer_get_channel(int index) {
    if (index == -1) return &g_mixer_state.master;
    if (index >= 0 && index < (int)g_mixer_state.channels.size()) {
        return g_mixer_state.channels.get(index);
    }
    return nullptr;
}

ChannelState* mixer_get_aux_bus(int aux_index) {
    if (aux_index >= 0 && aux_index < (int)g_mixer_state.aux_buses.size()) {
        return &g_mixer_state.aux_buses[static_cast<size_t>(aux_index)];
    }
    return nullptr;
}

ChannelState* mixer_find_channel_by_id(int id) {
    if (id == 0 || id == -1 || id == 100) return &g_mixer_state.master;
    if (id >= 101 && id <= 104) return &g_mixer_state.aux_buses[static_cast<size_t>(id - 101)];
    for (size_t i = 0; i < g_mixer_state.channels.size(); ++i) {
        if (g_mixer_state.channels[i].id == id) {
            return g_mixer_state.channels.get(i);
        }
    }
    return nullptr;
}

ChannelState* mixer_get_selected_channel() {
    if (g_mixer_state.selected_aux_bus >= 0 &&
        g_mixer_state.selected_aux_bus < (int)g_mixer_state.aux_buses.size()) {
        return &g_mixer_state.aux_buses[static_cast<size_t>(g_mixer_state.selected_aux_bus)];
    }
    ChannelState* ch = mixer_get_channel(g_mixer_state.selected_channel);
    if (ch) return ch;
    if (!g_mixer_state.channels.empty()) {
        return g_mixer_state.channels.get(0);
    }
    return &g_mixer_state.master;
}
