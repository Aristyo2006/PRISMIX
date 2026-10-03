#include "dynamics_screen.h"
#include "../theme/theme.h"
#include "../widgets/instrument.h"
#include "../../model/mixer_state.h"
#include <array>
#include <cmath>
#include <algorithm>
#include <utility>

namespace {
struct DynamicsView { ChannelState* ch; lv_obj_t* chart; lv_obj_t* gr_label; };
void update_curve(lv_timer_t* t) {
    auto* v = static_cast<DynamicsView*>(lv_timer_get_user_data(t));
    std::array<float,49> y{};
    auto& c = *v->ch;
    for (int i=0; i<49; ++i) {
        const float in = -48.0f + i*1.0f;
        const float knee = std::max(.1f,c.comp_knee);
        const float over = in - c.comp_thresh;
        float out = in;
        if (!c.comp_bypass && over > -knee*.5f) {
            const float compressed = c.comp_thresh + std::max(0.0f, over) / std::max(1.0f,c.comp_ratio);
            out = over < knee*.5f ? in + (compressed-in) * std::clamp((over+knee*.5f)/knee,0.0f,1.0f) : compressed;
            out += c.comp_makeup;
        }
        y[i] = std::clamp(out, -48.0f, 0.0f);
    }
    instrument_chart_series(v->chart, y.data(), (int)y.size(), -48, 0, COLOR_CYAN);
    lv_label_set_text_fmt(v->gr_label, "%5.1f dB", c.comp_gr);
}
}

lv_obj_t* dynamics_screen_create(void) {
    auto* s = lv_obj_create(lv_screen_active()); lv_obj_set_pos(s,0,0); lv_obj_set_size(s,1920,1080); lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_style_bg_color(s, COLOR_BG, 0);
    auto& ch = g_mixer_state.channels[g_mixer_state.selected_channel];
    auto* comp = instrument_panel(s, 12, 54, 522, 866, "COMPRESSOR / VCA");
    instrument_button(comp, "COMP BYPASS", 350, 12, 150, 38, &ch.comp_bypass);
    instrument_knob(comp, "THRESHOLD", &ch.comp_thresh, -60, 0, -18, "dB", 22, 76, 80);
    instrument_knob(comp, "RATIO", &ch.comp_ratio, 1, 20, 4, ":1", 186, 76, 80);
    instrument_knob(comp, "ATTACK", &ch.comp_attack, .1f, 100, 10, "ms", 350, 76, 80);
    instrument_knob(comp, "RELEASE", &ch.comp_release, 10, 1000, 80, "ms", 22, 255, 80);
    instrument_knob(comp, "KNEE", &ch.comp_knee, 0, 24, 6, "dB", 186, 255, 80);
    instrument_knob(comp, "MAKEUP", &ch.comp_makeup, -12, 24, 0, "dB", 350, 255, 80);
    instrument_label(comp, "INPUT", 30, 477, &ac_font_12, COLOR_TEXT2);
    instrument_meter(comp, &ch.peak_l, &ch.clip, 45, 506, 44, 278, &ch.peak_hold_l, &ch.rms_l);
    instrument_meter(comp, &ch.peak_r, &ch.clip, 103, 506, 44, 278, &ch.peak_hold_r, &ch.rms_r);
    instrument_label(comp, "OUTPUT", 185, 477, &ac_font_12, COLOR_TEXT2);
    instrument_meter(comp, &ch.comp_output, &ch.clip, 200, 506, 44, 278);
    instrument_meter(comp, &ch.comp_output, &ch.clip, 258, 506, 44, 278);
    instrument_label(comp, "GAIN REDUCTION", 325, 477, &ac_font_12, COLOR_TEXT2);
    auto* gr = lv_label_create(comp); lv_label_set_text(gr, "  0.0 dB"); lv_obj_set_pos(gr, 340, 521);
    lv_obj_set_style_text_font(gr, &ac_font_28, 0); lv_obj_set_style_text_color(gr, COLOR_YELLOW, 0);
    instrument_label(comp, "GR HOLD  /  PEAK  /  AUTO MAKEUP OFF", 323, 585, &ac_font_12, COLOR_TEXT2);
    auto* graph_panel = instrument_panel(s, 548, 54, 790, 866, "TRANSFER CHARACTERISTIC / INPUT dB  →  OUTPUT dB");
    auto* chart = instrument_chart(graph_panel, 22, 49, 742, 660, 49); instrument_style_chart(chart, -48, 0);
    std::array<float,49> unity{}; for(int i=0;i<49;++i) unity[i]=-48.0f+i;
    instrument_chart_reference(chart,unity.data(),(int)unity.size(),COLOR_TEXT2);
    instrument_chart_dynamics_markers(chart,&ch.comp_thresh,&ch.comp_knee,&ch.comp_input,&ch.comp_output);
    instrument_label(graph_panel, "1:1", 24, 720, &ac_font_12, COLOR_TEXT2);
    instrument_label(graph_panel, "-48 dB", 24, 774, &ac_font_12, COLOR_TEXT2);
    instrument_label(graph_panel, "INPUT LEVEL   -48                         -24                         0 dB", 140, 774, &ac_font_12, COLOR_TEXT2);
    instrument_label(graph_panel, "THRESHOLD  /  KNEE  /  ATTACK-RELEASE ENVELOPE", 70, 823, &ac_font_12, COLOR_TEXT2);
    auto* right = instrument_panel(s, 1352, 54, 552, 866, "NOISE GATE / SIDECHAIN");
    instrument_button(right, "GATE", 18, 48, 238, 42, &ch.gate_on);
    instrument_label(right, "GATE STATUS", 286, 52, &ac_font_12, COLOR_TEXT2);
    auto* gate_status = lv_label_create(right); lv_label_set_text(gate_status, "OPEN"); lv_obj_set_pos(gate_status, 286, 72);
    lv_obj_set_style_text_font(gate_status, &ac_font_20, 0); lv_obj_set_style_text_color(gate_status, COLOR_GREEN, 0);
    instrument_knob(right, "THRESHOLD", &ch.gate_thresh, -80, 0, -48, "dB", 26, 112, 72);
    instrument_knob(right, "ATTACK", &ch.gate_attack, .1f, 100, 5, "ms", 186, 112, 72);
    instrument_knob(right, "RELEASE", &ch.gate_release, 10, 1000, 120, "ms", 346, 112, 72);
    instrument_knob(right, "RANGE", &ch.gate_range, -80, 0, -60, "dB", 26, 286, 72);
    auto* gate_bar = instrument_panel(right, 186, 304, 328, 120, "GATE ACTIVITY");
    instrument_label(gate_bar, "INPUT LEVEL", 18, 48, &ac_font_12, COLOR_TEXT2);
    instrument_meter(gate_bar, &ch.peak_l, &ch.clip, 116, 48, 26, 58);
    instrument_label(gate_bar, "THRESHOLD  -48 dB", 158, 53, &ac_font_12, COLOR_TEXT);
    instrument_button(right, "SIDECHAIN", 18, 476, 238, 42, &ch.sidechain_on);
    instrument_label(right, "FILTER", 286, 480, &ac_font_12, COLOR_TEXT2);
    instrument_knob(right, "LOW CUT", &ch.sidechain_low, 20, 500, 80, "Hz", 26, 548, 72);
    instrument_knob(right, "HIGH CUT", &ch.sidechain_high, 500, 20000, 8000, "Hz", 186, 548, 72);
    instrument_label(right, "SIDECHAIN FILTER  /  HPF + LPF", 350, 600, &ac_font_12, COLOR_TEXT2);
    auto* view = new DynamicsView{&ch, chart, gr}; lv_timer_create(update_curve, 80, view);
    lv_timer_create([](lv_timer_t* t) {
        auto* pair = static_cast<std::pair<ChannelState*, lv_obj_t*>*>(lv_timer_get_user_data(t));
        lv_label_set_text(pair->second, pair->first->gate_open ? "OPEN" : "CLOSED");
        lv_obj_set_style_text_color(pair->second, pair->first->gate_open ? COLOR_GREEN : COLOR_ORANGE, 0);
    }, 80, new std::pair<ChannelState*,lv_obj_t*>{&ch,gate_status});
    return s;
}
