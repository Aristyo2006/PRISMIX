#include "routing_screen.h"
#include "../theme/theme.h"
#include "../widgets/instrument.h"
#include "../../model/mixer_state.h"
#include "../../model/routing_state.h"
#include "mixer_screen.h"
#include <cstdio>
#include <algorithm>
#include <string>

namespace {

struct RoutingView {
    ChannelState* ch = nullptr;
    int active_mode = 0; // 0: Signal Flow, 1: Bus Matrix
    lv_obj_t* mode_flow_btn = nullptr;
    lv_obj_t* mode_matrix_btn = nullptr;
    lv_obj_t* flow_container = nullptr;
    lv_obj_t* matrix_container = nullptr;
    lv_obj_t* in_source_btn = nullptr;
    lv_obj_t* main_lr_btn = nullptr;
};

void update_routing_mode_view(RoutingView* v) {
    if (!v) return;
    if (v->active_mode == 0) {
        lv_obj_remove_flag(v->flow_container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(v->matrix_container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(v->mode_flow_btn, COLOR_RAISED_PANEL, 0);
        lv_obj_set_style_border_color(v->mode_flow_btn, COLOR_SELECTED_BORDER, 0);
        lv_obj_set_style_border_width(v->mode_flow_btn, 2, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(v->mode_flow_btn, 0), COLOR_PRIMARY_TEXT, 0);

        lv_obj_set_style_bg_color(v->mode_matrix_btn, COLOR_CONTROL, 0);
        lv_obj_set_style_border_color(v->mode_matrix_btn, COLOR_STANDARD_BORDER, 0);
        lv_obj_set_style_border_width(v->mode_matrix_btn, 1, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(v->mode_matrix_btn, 0), COLOR_SECONDARY_TEXT, 0);
    } else {
        lv_obj_add_flag(v->flow_container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(v->matrix_container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(v->mode_flow_btn, COLOR_CONTROL, 0);
        lv_obj_set_style_border_color(v->mode_flow_btn, COLOR_STANDARD_BORDER, 0);
        lv_obj_set_style_border_width(v->mode_flow_btn, 1, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(v->mode_flow_btn, 0), COLOR_SECONDARY_TEXT, 0);

        lv_obj_set_style_bg_color(v->mode_matrix_btn, COLOR_RAISED_PANEL, 0);
        lv_obj_set_style_border_color(v->mode_matrix_btn, COLOR_SELECTED_BORDER, 0);
        lv_obj_set_style_border_width(v->mode_matrix_btn, 2, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(v->mode_matrix_btn, 0), COLOR_PRIMARY_TEXT, 0);
    }
}

static int s_routing_mode = 0;

} // namespace

void routing_screen_set_mode(int mode) {
    s_routing_mode = mode;
}

lv_obj_t* routing_screen_create(void) {
    auto* s = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(s, 0, 0);
    lv_obj_set_size(s, 1920, 1080);
    lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s, COLOR_BG, 0);

    ChannelState* ch_ptr = mixer_get_selected_channel();
    if (!ch_ptr) ch_ptr = mixer_get_channel(0);
    if (!ch_ptr) ch_ptr = &g_mixer_state.master;
    ChannelState& ch = *ch_ptr;

    auto* view = new RoutingView{};
    view->ch = &ch;
    view->active_mode = s_routing_mode;

    // Top Mode Toggle Bar
    auto* top_bar = lv_obj_create(s);
    lv_obj_set_pos(top_bar, 16, 48);
    lv_obj_set_size(top_bar, 1888, 44);
    lv_obj_remove_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(top_bar, COLOR_PANEL, 0);
    lv_obj_set_style_border_color(top_bar, COLOR_EDGE, 0);
    lv_obj_set_style_border_width(top_bar, 1, 0);
    lv_obj_set_style_radius(top_bar, 1, 0);
    lv_obj_set_style_pad_all(top_bar, 4, 0);

    view->mode_flow_btn = lv_button_create(top_bar);
    lv_obj_set_size(view->mode_flow_btn, 240, 34);
    lv_obj_set_pos(view->mode_flow_btn, 4, 1);
    lv_obj_set_style_radius(view->mode_flow_btn, 1, 0);
    auto* fl_lbl = lv_label_create(view->mode_flow_btn);
    lv_label_set_text(fl_lbl, "CHANNEL SIGNAL FLOW");
    lv_obj_center(fl_lbl);
    lv_obj_set_style_text_font(fl_lbl, &ac_font_14, 0);

    view->mode_matrix_btn = lv_button_create(top_bar);
    lv_obj_set_size(view->mode_matrix_btn, 240, 34);
    lv_obj_set_pos(view->mode_matrix_btn, 252, 1);
    lv_obj_set_style_radius(view->mode_matrix_btn, 1, 0);
    auto* mx_lbl = lv_label_create(view->mode_matrix_btn);
    lv_label_set_text(mx_lbl, "BUS & OUTPUT MATRIX");
    lv_obj_center(mx_lbl);
    lv_obj_set_style_text_font(mx_lbl, &ac_font_14, 0);

    char title_buf[64];
    std::snprintf(title_buf, sizeof(title_buf), "SELECTED CHANNEL: CH %02d  %s", ch.id, ch.name);
    instrument_label(top_bar, title_buf, 520, 10, &ac_font_14, COLOR_CYAN);
    instrument_label(top_bar, "LIVE: CAPTURE INPUT > STRIP FX > MAIN L/R", 1120, 10, &ac_font_12, COLOR_TEXT2);

    // =========================================================================
    // 1. SIGNAL FLOW VIEW CONTAINER
    // =========================================================================
    view->flow_container = lv_obj_create(s);
    lv_obj_set_pos(view->flow_container, 0, 94);
    lv_obj_set_size(view->flow_container, 1920, 920);
    lv_obj_remove_flag(view->flow_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(view->flow_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(view->flow_container, 0, 0);
    lv_obj_set_style_pad_all(view->flow_container, 0, 0);

    auto* flow_panel = instrument_panel(view->flow_container, 16, 8, 1888, 866, "END-TO-END SIGNAL PATH / DIGITAL CONSOLE TOPOLOGY");

    // Signal path stages cards arranged horizontally
    struct FlowStage {
        const char* name;
        const char* sub;
        const char* detail;
    };
    static const FlowStage stages[7] = {
        {"1. INPUT PATCH", "PHYSICAL / DIGITAL", "PREAMP ADC"},
        {"2. PREAMP / HPF", "ANALOG GAIN TRIM", "48V / POL / HPF"},
        {"3. STRIP FX", "8 INSERT SLOTS", "ORDERED RACK"},
        {"4. PARAMETRIC EQ", "4-BAND PEQ", "RTA SPECTRUM"},
        {"5. DYNAMICS", "GATE & VCA COMP", "GAIN REDUCTION"},
        {"6. PAN & FADER", "VCA LEVEL TRIM", "SOLO & MUTE"},
        {"7. SUMMING BUSES", "8 BUSES + AUX", "STEREO MAIN L/R"}
    };

    const int stage_w = 254;
    const int gap = 12;

    for (int i = 0; i < 7; ++i) {
        int sx = 20 + i * (stage_w + gap);

        auto* stage_card = lv_obj_create(flow_panel);
        lv_obj_set_pos(stage_card, sx, 52);
        lv_obj_set_size(stage_card, stage_w, 240);
        lv_obj_remove_flag(stage_card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(stage_card, COLOR_PANEL_SURFACE, 0);
        lv_obj_set_style_border_color(stage_card, COLOR_EDGE, 0);
        lv_obj_set_style_border_width(stage_card, 1, 0);
        lv_obj_set_style_radius(stage_card, 2, 0);
        lv_obj_set_style_pad_all(stage_card, 10, 0);

        instrument_label(stage_card, stages[i].name, 8, 6, &ac_font_14, COLOR_CYAN);
        instrument_label(stage_card, stages[i].sub, 8, 28, &ac_font_12, COLOR_TEXT2);
        instrument_label(stage_card, stages[i].detail, 8, 48, &ac_font_12, COLOR_TEXT);

        if (i == 0) {
            // Interactive Input Patch Selector
            auto* src_btn = lv_button_create(stage_card);
            lv_obj_set_pos(src_btn, 8, 120);
            lv_obj_set_size(src_btn, 218, 44);
            lv_obj_set_style_radius(src_btn, 1, 0);
            lv_obj_set_style_bg_color(src_btn, COLOR_CONTROL, 0);
            auto* sl = lv_label_create(src_btn);
            lv_label_set_text(sl, ch.input_label);
            lv_obj_center(sl);
            lv_obj_set_style_text_font(sl, &ac_font_12, 0);
            lv_obj_set_style_text_color(sl, COLOR_PRIMARY_TEXT, 0);

            lv_obj_add_event_cb(src_btn, [](lv_event_t* e) {
                const int channelId = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
                mixer_screen_show_channel_setup(channelId);
            }, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(ch.id)));

            instrument_label(stage_card, "SELECT DEVICE INPUT", 8, 172, &ac_font_12, COLOR_TEXT2);
        } else if (i == 1) {
            instrument_label(stage_card, ch.hpf_on ? "HPF: 80 Hz ON" : "HPF: OFF", 8, 120, &ac_font_14, ch.hpf_on ? COLOR_GREEN : COLOR_TEXT2);
            instrument_label(stage_card, ch.phantom_on ? "48V: ACTIVE" : "48V: OFF", 8, 146, &ac_font_12, ch.phantom_on ? COLOR_RED : COLOR_TEXT2);
            instrument_label(stage_card, ch.polarity_inverted ? "POL: INVERT" : "POL: NORMAL", 8, 172, &ac_font_12, COLOR_TEXT2);
        } else if (i == 2) {
            char ins_txt[64];
            std::snprintf(ins_txt, sizeof(ins_txt), "STRIP FX: %s", ch.fx_rack.is_empty() ? "UNASSIGNED" : "1 / 1 ASSIGNED");
            instrument_label(stage_card, ins_txt, 8, 120, &ac_font_14, COLOR_CYAN);
            instrument_label(stage_card, "INSERT POINT: POST-HPF", 8, 146, &ac_font_12, COLOR_TEXT2);
        } else if (i == 3) {
            instrument_label(stage_card, ch.eq_bypass ? "STATUS: BYPASS" : "STATUS: ACTIVE", 8, 120, &ac_font_14, ch.eq_bypass ? COLOR_ORANGE : COLOR_GREEN);
            instrument_label(stage_card, "LOW / MID1 / MID2 / HIGH", 8, 146, &ac_font_12, COLOR_TEXT2);
        } else if (i == 4) {
            instrument_label(stage_card, ch.comp_bypass ? "COMP: BYPASS" : "COMP: ACTIVE", 8, 120, &ac_font_14, ch.comp_bypass ? COLOR_ORANGE : COLOR_GREEN);
            instrument_label(stage_card, ch.gate_on ? "GATE: ON" : "GATE: OFF", 8, 146, &ac_font_12, ch.gate_on ? COLOR_GREEN : COLOR_TEXT2);
        } else if (i == 5) {
            char f_str[32];
            std::snprintf(f_str, sizeof(f_str), "FADER: %d %%", (int)(ch.fader * 100.0f));
            instrument_label(stage_card, f_str, 8, 120, &ac_font_14, COLOR_TEXT);
            instrument_label(stage_card, ch.mute ? "MUTE: MUTED" : "MUTE: UNMUTED", 8, 146, &ac_font_12, ch.mute ? COLOR_RED : COLOR_TEXT2);
        } else if (i == 6) {
            auto* ml_btn = instrument_button(stage_card, "SET OUTPUT ROUTE", 8, 120, 218, 44, nullptr);
            lv_obj_add_event_cb(ml_btn, [](lv_event_t* e) {
                const int channelId = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
                mixer_screen_show_channel_setup(channelId);
            }, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(ch.id)));
            const char* routeName = ch.output_route == ChannelOutputRoute::MainLR ? "MAIN L/R" :
                ch.output_route == ChannelOutputRoute::Aux ? "AUX BUS" :
                ch.output_route == ChannelOutputRoute::Omni ? "OMNI OUTPUT" : "UNROUTED";
            instrument_label(stage_card, routeName, 8, 172, &ac_font_12, COLOR_TEXT2);
        }
    }

    // Lower Section of Flow Panel: Channel Bus Assignment Matrix
    auto* sub_panel = instrument_panel(flow_panel, 20, 316, 1848, 524, "CHANNEL BUS ASSIGNMENTS & AUX SENDS");

    instrument_label(sub_panel, "ASSIGN CHANNEL TO SUBGROUPS / BUSES (1-8):", 24, 48, &ac_font_14, COLOR_CYAN);

    int ch_idx = std::max(0, ch.id - 1);
    for (int b = 0; b < 8; ++b) {
        int bx = 24 + b * 224;
        bool connected = g_routing_matrix.get_channel_bus(ch_idx, b);

        char b_lbl[32];
        std::snprintf(b_lbl, sizeof(b_lbl), "BUS %02d", b + 1);
        auto* bus_btn = instrument_button(sub_panel, b_lbl, bx, 80, 204, 52, nullptr);
        lv_obj_set_style_bg_color(bus_btn, connected ? COLOR_RAISED_PANEL : COLOR_CONTROL, 0);
        lv_obj_set_style_border_color(bus_btn, connected ? COLOR_CYAN : COLOR_EDGE, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(bus_btn, 0), connected ? COLOR_CYAN : COLOR_TEXT2, 0);

        struct BusCtx { int ch_i; int bus_i; lv_obj_t* btn; };
        auto* bc = new BusCtx{ch_idx, b, bus_btn};
        lv_obj_add_event_cb(bus_btn, [](lv_event_t* e) {
            auto* c = static_cast<BusCtx*>(lv_event_get_user_data(e));
            if (!c) return;
            g_routing_matrix.toggle_channel_bus(c->ch_i, c->bus_i);
            bool is_conn = g_routing_matrix.get_channel_bus(c->ch_i, c->bus_i);
            lv_obj_set_style_bg_color(c->btn, is_conn ? COLOR_RAISED_PANEL : COLOR_CONTROL, 0);
            lv_obj_set_style_border_color(c->btn, is_conn ? COLOR_CYAN : COLOR_EDGE, 0);
            lv_obj_set_style_text_color(lv_obj_get_child(c->btn, 0), is_conn ? COLOR_CYAN : COLOR_TEXT2, 0);
        }, LV_EVENT_CLICKED, bc);
        lv_obj_add_event_cb(bus_btn, [](lv_event_t* e) {
            auto* c = static_cast<BusCtx*>(lv_event_get_user_data(e));
            delete c;
        }, LV_EVENT_DELETE, bc);
    }

    instrument_label(sub_panel, "CHANNEL AUX SENDS (1-4) / TAP POINT:", 24, 150, &ac_font_14, COLOR_CYAN);
    for (int a = 0; a < 4; ++a) {
        const int ax = 24 + a * 188;
        char a_name[32];
        std::snprintf(a_name, sizeof(a_name), "AUX %02d SEND", a + 1);
        instrument_knob(sub_panel, a_name, &ch.sends[a], 0.0f, 1.0f, ch.sends[a], "%", ax + 34, 182, 72);
        instrument_button(sub_panel, "SEND ON", ax + 10, 302, 148, 34, &ch.send_enabled[a]);
        instrument_button(sub_panel, "PRE-FADER", ax + 10, 344, 148, 34, &ch.send_pre_fader[a]);
    }

    instrument_label(sub_panel, "TAP: PRE-FADER (INDEPENDENT OF STRIP FADER)  /  POST-FADER (FOLLOWS STRIP FADER)", 24, 394, &ac_font_12, COLOR_TEXT2);
    instrument_label(sub_panel, "DAG TOPOLOGY: INPUT STRIPS -> AUX BUSES (1-4) -> STEREO MASTER BUS (CYCLE-FREE)", 24, 418, &ac_font_12, COLOR_GREEN);

    instrument_label(sub_panel, "AUXILIARY BUS MASTERS (AUX 01 - AUX 04)  /  FORMAT, FADER, RETURN & METERS:", 804, 150, &ac_font_14, COLOR_CYAN);
    for (int a = 0; a < 4; ++a) {
        auto& aux = g_mixer_state.aux_buses[static_cast<size_t>(a)];
        const int bx = 804 + a * 256;
        auto* aux_card = lv_obj_create(sub_panel);
        lv_obj_set_pos(aux_card, bx, 178);
        lv_obj_set_size(aux_card, 246, 326);
        lv_obj_remove_flag(aux_card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(aux_card, COLOR_PANEL_SURFACE, 0);
        lv_obj_set_style_border_color(aux_card, g_mixer_state.selected_aux_bus == a ? COLOR_SELECTED_BORDER : COLOR_STANDARD_BORDER, 0);
        lv_obj_set_style_border_width(aux_card, g_mixer_state.selected_aux_bus == a ? 2 : 1, 0);
        lv_obj_set_style_radius(aux_card, 2, 0);
        lv_obj_set_style_pad_all(aux_card, 8, 0);

        instrument_label(aux_card, aux.name, 6, 4, &ac_font_14, COLOR_CYAN);

        // Format toggle button (MONO / STEREO)
        auto* fmt_btn = instrument_button(aux_card, aux.format == ChannelFormat::Stereo ? "STEREO" : "MONO",
                                          118, 0, 108, 28, nullptr);
        struct AuxFmtCtx { ChannelState* aux; lv_obj_t* btn; };
        auto* afc = new AuxFmtCtx{&aux, fmt_btn};
        lv_obj_add_event_cb(fmt_btn, [](lv_event_t* e) {
            auto* c = static_cast<AuxFmtCtx*>(lv_event_get_user_data(e));
            if (!c || !c->aux) return;
            c->aux->format = (c->aux->format == ChannelFormat::Stereo) ? ChannelFormat::Mono : ChannelFormat::Stereo;
            if (lv_obj_get_child_count(c->btn) > 0) {
                lv_label_set_text(lv_obj_get_child(c->btn, 0),
                                  c->aux->format == ChannelFormat::Stereo ? "STEREO" : "MONO");
            }
        }, LV_EVENT_CLICKED, afc);
        lv_obj_add_event_cb(fmt_btn, [](lv_event_t* e) {
            delete static_cast<AuxFmtCtx*>(lv_event_get_user_data(e));
        }, LV_EVENT_DELETE, afc);

        instrument_knob(aux_card, "FADER", &aux.fader, 0.0f, 1.0f, 0.86f, "dB", 6, 42, 60);
        instrument_knob(aux_card, "RETURN", &aux.return_gain, 0.0f, 1.0f, 0.86f, "dB", 84, 42, 60);

        instrument_meter(aux_card, &aux.peak_l, &aux.clip, 168, 42, 24, 118, &aux.peak_hold_l, &aux.rms_l);
        instrument_meter(aux_card, &aux.peak_r, &aux.clip, 198, 42, 24, 118, &aux.peak_hold_r, &aux.rms_r);

        instrument_button(aux_card, "MUTE", 6, 178, 106, 34, &aux.mute);
        instrument_button(aux_card, "TO MAIN", 120, 178, 106, 34, &aux.route_to_main);

        const char* sel_txt = (g_mixer_state.selected_aux_bus == a) ? "ACTIVE BUS TARGET" : "SELECT FOR EQ/DYN/FX";
        auto* sel_btn = instrument_button(aux_card, sel_txt, 6, 222, 220, 36, nullptr);
        if (g_mixer_state.selected_aux_bus == a) {
            lv_obj_set_style_border_color(sel_btn, COLOR_CYAN, 0);
            lv_obj_set_style_border_width(sel_btn, 2, 0);
        }
        lv_obj_add_event_cb(sel_btn, [](lv_event_t* e) {
            const int auxIdx = static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
            if (g_mixer_state.selected_aux_bus == auxIdx) {
                g_mixer_state.selected_aux_bus = -1;
            } else {
                g_mixer_state.selected_aux_bus = auxIdx;
            }
        }, LV_EVENT_CLICKED, reinterpret_cast<void*>(static_cast<intptr_t>(a)));
    }

    // =========================================================================
    // 2. BUS & OUTPUT MATRIX VIEW CONTAINER
    // =========================================================================
    view->matrix_container = lv_obj_create(s);
    lv_obj_set_pos(view->matrix_container, 0, 94);
    lv_obj_set_size(view->matrix_container, 1920, 920);
    lv_obj_remove_flag(view->matrix_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(view->matrix_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(view->matrix_container, 0, 0);
    lv_obj_set_style_pad_all(view->matrix_container, 0, 0);

    auto* matrix_panel = instrument_panel(view->matrix_container, 16, 8, 1888, 866, "COMPLETE CHANNEL → BUS CROSSPOINT MATRIX (8x8 + MAIN L/R)");

    instrument_label(matrix_panel, "SOURCE CHANNEL", 24, 48, &ac_font_12, COLOR_TEXT2);
    for (int col = 0; col < 8; ++col) {
        char col_hdr[32];
        std::snprintf(col_hdr, sizeof(col_hdr), "BUS %02d", col + 1);
        instrument_label(matrix_panel, col_hdr, 220 + col * 170, 48, &ac_font_12, COLOR_TEXT2);
    }
    instrument_label(matrix_panel, "MAIN L/R", 1600, 48, &ac_font_12, COLOR_CYAN);

    const int num_rows = std::min((int)g_mixer_state.channels.size(), 8);
    for (int row = 0; row < num_rows; ++row) {
        auto* c = mixer_get_channel(row);
        if (!c) continue;
        const int routeSlot = c->id - 1;
        char row_name[64];
        std::snprintf(row_name, sizeof(row_name), "CH %02d  %s", row + 1, c->name);
        instrument_label(matrix_panel, row_name, 24, 86 + row * 66, &ac_font_14, COLOR_TEXT);

        for (int col = 0; col < 8; ++col) {
            bool conn = g_routing_matrix.get_channel_bus(routeSlot, col);
            auto* cp_btn = lv_button_create(matrix_panel);
            lv_obj_set_pos(cp_btn, 210 + col * 170, 78 + row * 66);
            lv_obj_set_size(cp_btn, 130, 46);
            lv_obj_set_style_radius(cp_btn, 1, 0);
            lv_obj_set_style_bg_color(cp_btn, conn ? COLOR_RAISED_PANEL : COLOR_CONTROL, 0);
            lv_obj_set_style_border_color(cp_btn, conn ? COLOR_CYAN : COLOR_EDGE, 0);
            lv_obj_set_style_border_width(cp_btn, 1, 0);

            auto* l = lv_label_create(cp_btn);
            char l_str[16];
            std::snprintf(l_str, sizeof(l_str), "%02d", col + 1);
            lv_label_set_text(l, l_str);
            lv_obj_center(l);
            lv_obj_set_style_text_font(l, &ac_font_14, 0);
            lv_obj_set_style_text_color(l, conn ? COLOR_CYAN : COLOR_TEXT2, 0);

            struct MatCtx { int r; int c; lv_obj_t* btn; };
            auto* mc = new MatCtx{routeSlot, col, cp_btn};
            lv_obj_add_event_cb(cp_btn, [](lv_event_t* e) {
                auto* m = static_cast<MatCtx*>(lv_event_get_user_data(e));
                if (!m) return;
                g_routing_matrix.toggle_channel_bus(m->r, m->c);
                bool is_c = g_routing_matrix.get_channel_bus(m->r, m->c);
                lv_obj_set_style_bg_color(m->btn, is_c ? COLOR_RAISED_PANEL : COLOR_CONTROL, 0);
                lv_obj_set_style_border_color(m->btn, is_c ? COLOR_CYAN : COLOR_EDGE, 0);
                lv_obj_set_style_text_color(lv_obj_get_child(m->btn, 0), is_c ? COLOR_CYAN : COLOR_TEXT2, 0);
            }, LV_EVENT_CLICKED, mc);
            lv_obj_add_event_cb(cp_btn, [](lv_event_t* e) {
                auto* m = static_cast<MatCtx*>(lv_event_get_user_data(e));
                delete m;
            }, LV_EVENT_DELETE, mc);
        }

        // Main L/R direct assign
        bool is_m = g_routing_matrix.get_channel_main_lr(routeSlot);
        auto* ml_btn = lv_button_create(matrix_panel);
        lv_obj_set_pos(ml_btn, 1590, 78 + row * 66);
        lv_obj_set_size(ml_btn, 140, 46);
        lv_obj_set_style_radius(ml_btn, 1, 0);
        lv_obj_set_style_bg_color(ml_btn, is_m ? COLOR_RAISED_PANEL : COLOR_CONTROL, 0);
        lv_obj_set_style_border_color(ml_btn, is_m ? COLOR_CYAN : COLOR_EDGE, 0);
        lv_obj_set_style_border_width(ml_btn, 1, 0);

        auto* mll = lv_label_create(ml_btn);
        lv_label_set_text(mll, is_m ? "MAIN ON" : "MAIN OFF");
        lv_obj_center(mll);
        lv_obj_set_style_text_font(mll, &ac_font_12, 0);
        lv_obj_set_style_text_color(mll, is_m ? COLOR_CYAN : COLOR_TEXT2, 0);

        struct MlRowCtx { int row; lv_obj_t* btn; };
        auto* mrc = new MlRowCtx{routeSlot, ml_btn};
        lv_obj_add_event_cb(ml_btn, [](lv_event_t* e) {
            auto* m = static_cast<MlRowCtx*>(lv_event_get_user_data(e));
            if (!m) return;
            bool cur = !g_routing_matrix.get_channel_main_lr(m->row);
            g_routing_matrix.set_channel_main_lr(m->row, cur);
            lv_obj_set_style_bg_color(m->btn, cur ? COLOR_RAISED_PANEL : COLOR_CONTROL, 0);
            lv_obj_set_style_border_color(m->btn, cur ? COLOR_CYAN : COLOR_EDGE, 0);
            lv_label_set_text(lv_obj_get_child(m->btn, 0), cur ? "MAIN ON" : "MAIN OFF");
            lv_obj_set_style_text_color(lv_obj_get_child(m->btn, 0), cur ? COLOR_CYAN : COLOR_TEXT2, 0);
        }, LV_EVENT_CLICKED, mrc);
        lv_obj_add_event_cb(ml_btn, [](lv_event_t* e) {
            auto* m = static_cast<MlRowCtx*>(lv_event_get_user_data(e));
            delete m;
        }, LV_EVENT_DELETE, mrc);
    }

    instrument_label(matrix_panel, "ACTIVE CROSSPOINTS ARE ILLUMINATED IN CYAN  /  CLICK TO ADD OR REMOVE ROUTE", 24, 760, &ac_font_12, COLOR_TEXT2);

    // Initial mode state
    update_routing_mode_view(view);

    lv_obj_add_event_cb(view->mode_flow_btn, [](lv_event_t* e) {
        auto* v = static_cast<RoutingView*>(lv_event_get_user_data(e));
        if (v) {
            s_routing_mode = 0;
            v->active_mode = 0;
            update_routing_mode_view(v);
        }
    }, LV_EVENT_CLICKED, view);

    lv_obj_add_event_cb(view->mode_matrix_btn, [](lv_event_t* e) {
        auto* v = static_cast<RoutingView*>(lv_event_get_user_data(e));
        if (v) {
            s_routing_mode = 1;
            v->active_mode = 1;
            update_routing_mode_view(v);
        }
    }, LV_EVENT_CLICKED, view);

    lv_obj_add_event_cb(s, [](lv_event_t* e) {
        auto* v = static_cast<RoutingView*>(lv_event_get_user_data(e));
        delete v;
    }, LV_EVENT_DELETE, view);

    return s;
}
