#include "dynamics_screen.h"
#include "../theme/theme.h"
#include "../widgets/instrument.h"
#include "../../model/mixer_state.h"
#include <array>
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace {
struct DynamicsView {
    ChannelState* ch = nullptr;
    lv_obj_t* target_label = nullptr;
    lv_obj_t* comp_chart = nullptr;
    lv_obj_t* gate_chart = nullptr;
    lv_obj_t* comp_thresh_knob = nullptr;
    lv_obj_t* gate_thresh_knob = nullptr;
    lv_obj_t* gr_val_label = nullptr;
    lv_obj_t* gate_status_badge = nullptr;
    lv_obj_t* gate_thresh_label = nullptr;
    lv_obj_t* gate_led = nullptr;
    lv_obj_t* key_source_btn = nullptr;
    lv_obj_t* lim_pre_label = nullptr;
    lv_obj_t* lim_post_label = nullptr;
    lv_obj_t* lim_gr_label = nullptr;
    lv_timer_t* anim_timer = nullptr;
};

const char* kKeySources[10] = {
    "KEY: SELF", "KEY: CH 01", "KEY: CH 02", "KEY: CH 03", "KEY: CH 04",
    "KEY: CH 05", "KEY: CH 06", "KEY: CH 07", "KEY: CH 08", "KEY: AUX"
};

void update_comp_curve(DynamicsView* v) {
    if (!v || !v->ch || !v->comp_chart || lv_obj_has_flag(v->comp_chart, LV_OBJ_FLAG_HIDDEN)) return;
    auto& c = *v->ch;
    std::array<float, 61> comp_curve{};
    for (int i = 0; i < 61; ++i) {
        const float pt_in = -60.0f + i * 1.0f; // -60 dB to 0 dB
        const float over = pt_in - c.comp_thresh;
        float out_db = pt_in;

        if (!c.comp_bypass) {
            if (c.comp_knee <= 0.001f) {
                if (over <= 0.0f) {
                    out_db = pt_in;
                } else {
                    out_db = c.comp_thresh + over / std::max(1.0f, c.comp_ratio);
                }
            } else {
                const float knee = c.comp_knee;
                if (over <= -knee * 0.5f) {
                    out_db = pt_in;
                } else if (over >= knee * 0.5f) {
                    out_db = c.comp_thresh + over / std::max(1.0f, c.comp_ratio);
                } else {
                    float delta = over + knee * 0.5f;
                    float slope_diff = (1.0f / std::max(1.0f, c.comp_ratio)) - 1.0f;
                    out_db = pt_in + slope_diff * (delta * delta) / (2.0f * knee);
                }
            }
            out_db += c.comp_makeup;
        }
        if (c.limiter_enabled) {
            out_db = std::min(out_db, c.limiter_ceiling_db);
        }
        comp_curve[i] = std::clamp(out_db, -60.0f, 12.0f);
    }
    instrument_chart_series(v->comp_chart, comp_curve.data(), (int)comp_curve.size(), -60, 12, lv_color_hex(0xE2E8F0));
}

void update_gate_curve(DynamicsView* v) {
    if (!v || !v->ch || !v->gate_chart || lv_obj_has_flag(v->gate_chart, LV_OBJ_FLAG_HIDDEN)) return;
    auto& c = *v->ch;
    std::array<float, 61> gate_curve{};
    for (int i = 0; i < 61; ++i) {
        const float pt_in = -80.0f + i * (80.0f / 60.0f); // -80 dB to 0 dB
        float out_db = pt_in;

        if (c.gate_on) {
            if (pt_in < c.gate_thresh) {
                out_db = std::max(-90.0f, pt_in + c.gate_range);
            } else if (pt_in < c.gate_thresh + 2.0f) {
                float t_norm = (pt_in - c.gate_thresh) / 2.0f;
                float closed_val = std::max(-90.0f, pt_in + c.gate_range);
                out_db = closed_val + (pt_in - closed_val) * t_norm;
            } else {
                out_db = pt_in;
            }
        }
        gate_curve[i] = std::clamp(out_db, -80.0f, 0.0f);
    }
    instrument_chart_series(v->gate_chart, gate_curve.data(), (int)gate_curve.size(), -80, 0, COLOR_GREEN);
}

void update_dynamics_view(lv_timer_t* t) {
    auto* v = static_cast<DynamicsView*>(lv_timer_get_user_data(t));
    if (!v) return;

    ChannelState* active_ch = mixer_get_selected_channel();
    if (!active_ch) active_ch = mixer_get_channel(0);
    if (!active_ch) active_ch = &g_mixer_state.master;
    v->ch = active_ch;
    auto& c = *v->ch;

    // Master bus gate is always hard-disabled
    if (isMasterChannel(&c)) {
        c.gate_on = false;
        c.gate_gr_db = 0.0f;
        c.gate_state = 2; // Open
        c.gate_open = true;
    }

    if (v->target_label) {
        char target_txt[64];
        std::snprintf(target_txt, sizeof(target_txt), "TARGET: %s (%s)",
                      c.name, c.format == ChannelFormat::Stereo ? "STEREO" : "MONO");
        lv_label_set_text(v->target_label, target_txt);
    }

    // 1. Calculate live operating point coordinate for Compressor
    const float in_db = c.comp_input;
    float comp_marker_y = in_db;
    if (!c.comp_bypass) {
        const float over = in_db - c.comp_thresh;
        if (c.comp_knee <= 0.001f) {
            if (over <= 0.0f) {
                comp_marker_y = in_db;
            } else {
                comp_marker_y = c.comp_thresh + over / std::max(1.0f, c.comp_ratio);
            }
        } else {
            const float knee = c.comp_knee;
            if (over <= -knee * 0.5f) {
                comp_marker_y = in_db;
            } else if (over >= knee * 0.5f) {
                comp_marker_y = c.comp_thresh + over / std::max(1.0f, c.comp_ratio);
            } else {
                float delta = over + knee * 0.5f;
                float slope_diff = (1.0f / std::max(1.0f, c.comp_ratio)) - 1.0f;
                comp_marker_y = in_db + slope_diff * (delta * delta) / (2.0f * knee);
            }
        }
        comp_marker_y += c.comp_makeup;
    }
    if (c.limiter_enabled) {
        comp_marker_y = std::min(comp_marker_y, c.limiter_ceiling_db);
    }
    c.comp_curve_output = std::clamp(comp_marker_y, -60.0f, 12.0f);

    // 2. Update Compressor Transfer Curve
    update_comp_curve(v);

    // 3. Update Gate Transfer Curve only if gate_chart is active
    if (v->gate_chart && !lv_obj_has_flag(v->gate_chart, LV_OBJ_FLAG_HIDDEN)) {
        const float gate_in_db = c.gate_detector_db;
        float gate_marker_y = gate_in_db;
        if (c.gate_on) {
            if (gate_in_db < c.gate_thresh) {
                gate_marker_y = std::max(-90.0f, gate_in_db + c.gate_range);
            } else if (gate_in_db < c.gate_thresh + 2.0f) {
                float t_norm = (gate_in_db - c.gate_thresh) / 2.0f;
                float closed_val = std::max(-90.0f, gate_in_db + c.gate_range);
                gate_marker_y = closed_val + (gate_in_db - closed_val) * t_norm;
            } else {
                gate_marker_y = gate_in_db;
            }
        }
        c.gate_curve_output = std::clamp(gate_marker_y, -80.0f, 0.0f);
        update_gate_curve(v);
    }

    // 4. Update Gain Reduction readout
    if (v->gr_val_label) {
        lv_label_set_text_fmt(v->gr_val_label, "%5.1f dB", c.comp_gr);
    }

    // 6. Update Gate Status badge & detector LED reflecting actual GateState machine
    if (v->gate_status_badge) {
        if (!c.gate_on) {
            lv_label_set_text(v->gate_status_badge, "BYPASS");
            lv_obj_set_style_text_color(v->gate_status_badge, COLOR_TEXT2, 0);
        } else {
            switch (c.gate_state) {
                case 1: // Attack / Opening
                    lv_label_set_text(v->gate_status_badge, "ATTACK");
                    lv_obj_set_style_text_color(v->gate_status_badge, COLOR_YELLOW, 0);
                    break;
                case 2: // Open
                    lv_label_set_text(v->gate_status_badge, "OPEN");
                    lv_obj_set_style_text_color(v->gate_status_badge, COLOR_GREEN, 0);
                    break;
                case 3: // Hold
                    lv_label_set_text(v->gate_status_badge, "HOLD");
                    lv_obj_set_style_text_color(v->gate_status_badge, lv_color_hex(0x8A9BB0), 0);
                    break;
                case 4: // Release / Closing
                    lv_label_set_text(v->gate_status_badge, "RELEASE");
                    lv_obj_set_style_text_color(v->gate_status_badge, lv_color_hex(0xE08030), 0);
                    break;
                case 0: // Closed
                default:
                    lv_label_set_text(v->gate_status_badge, "CLOSED");
                    lv_obj_set_style_text_color(v->gate_status_badge, COLOR_ORANGE, 0);
                    break;
            }
        }
    }

    if (v->gate_thresh_label) {
        char t_buf[32];
        std::snprintf(t_buf, sizeof(t_buf), "%.1f dB", c.gate_thresh);
        lv_label_set_text(v->gate_thresh_label, t_buf);
    }

    if (v->gate_led) {
        lv_color_t led_col;
        if (!c.gate_on) {
            led_col = lv_color_hex(0x283038);
        } else {
            switch (c.gate_state) {
                case 1: led_col = COLOR_YELLOW; break;
                case 2: led_col = COLOR_GREEN; break;
                case 3: led_col = lv_color_hex(0x8A9BB0); break;
                case 4: led_col = lv_color_hex(0xE08030); break;
                case 0:
                default: led_col = COLOR_ORANGE; break;
            }
        }
        lv_obj_set_style_bg_color(v->gate_led, led_col, 0);
        lv_obj_set_style_border_color(v->gate_led, led_col, 0);
    }

    // 7. Update Brickwall Limiter Pre/Post Peak & Gain Reduction telemetry
    const float pre_peak = std::max(c.pre_limiter_peak_l, c.pre_limiter_peak_r);
    const float post_peak = std::max(c.peak_l, c.peak_r);
    if (v->lim_pre_label) {
        if (pre_peak <= -89.0f) {
            lv_label_set_text(v->lim_pre_label, "-inf dB");
        } else {
            lv_label_set_text_fmt(v->lim_pre_label, "%+5.1f dB", pre_peak);
        }
    }
    if (v->lim_post_label) {
        if (post_peak <= -89.0f) {
            lv_label_set_text(v->lim_post_label, "-inf dB");
        } else {
            lv_label_set_text_fmt(v->lim_post_label, "%+5.1f dB", post_peak);
        }
    }
    if (v->lim_gr_label) {
        if (!c.limiter_enabled) {
            lv_label_set_text(v->lim_gr_label, "BYPASS");
            lv_obj_set_style_text_color(v->lim_gr_label, COLOR_TEXT2, 0);
        } else {
            lv_label_set_text_fmt(v->lim_gr_label, "%5.1f dB", c.limiter_gr_db);
            lv_obj_set_style_text_color(v->lim_gr_label, c.limiter_gr_db < -0.2f ? COLOR_ORANGE : COLOR_GREEN, 0);
        }
    }
}
} // namespace

lv_obj_t* dynamics_screen_create(void) {
    auto* s = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(s, 0, 0);
    lv_obj_set_size(s, 1920, 1080);
    lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s, COLOR_BG, 0);

    ChannelState* ch_ptr = mixer_get_selected_channel();
    if (!ch_ptr) ch_ptr = mixer_get_channel(0);
    if (!ch_ptr) ch_ptr = &g_mixer_state.master;
    ChannelState& ch = *ch_ptr;
    const bool is_master = isMasterChannel(ch_ptr);

    if (is_master) {
        ch.gate_on = false;
        ch.gate_open = true;
        ch.gate_gr_db = 0.0f;
    }

    auto* view = new DynamicsView{};
    view->ch = &ch;
    view->target_label = nullptr;

    // =========================================================================
    // 1. LEFT PANEL: COMPRESSOR CONTROLS & METERS
    // =========================================================================
    auto* comp = instrument_panel(s, 12, 54, 520, 866, "COMPRESSOR");
    instrument_button(comp, "COMP BYPASS", 340, 8, 160, 36, &ch.comp_bypass);

    // Knobs (Row 1)
    view->comp_thresh_knob = instrument_knob(comp, "THRESH", &ch.comp_thresh, -60.0f, 0.0f, -18.0f, "dB", 24, 66, 76);
    auto* k_ratio = instrument_knob(comp, "RATIO", &ch.comp_ratio, 1.0f, 20.0f, 4.0f, ":1", 184, 66, 76);
    instrument_knob(comp, "ATTACK", &ch.comp_attack, 0.1f, 100.0f, 10.0f, "ms", 344, 66, 76);

    // Knobs (Row 2)
    instrument_knob(comp, "RELEASE", &ch.comp_release, 10.0f, 1000.0f, 80.0f, "ms", 24, 216, 76);
    auto* k_knee = instrument_knob(comp, "KNEE", &ch.comp_knee, 0.0f, 24.0f, 6.0f, "dB", 184, 216, 76);
    auto* k_makeup = instrument_knob(comp, "MAKEUP", &ch.comp_makeup, -12.0f, 24.0f, 0.0f, "dB", 344, 216, 76);

    // Pre-DSP Input vs Post-Compressor Output Meters
    instrument_label(comp, "INPUT", 34, 430, &ac_font_12, COLOR_TEXT2);
    instrument_meter(comp, &ch.input_peak_l, &ch.input_clip, 30, 456, 38, 300, &ch.input_peak_hold_l, &ch.input_rms_l);
    instrument_meter(comp, &ch.input_peak_r, &ch.input_clip, 74, 456, 38, 300, &ch.input_peak_hold_r, &ch.input_rms_r);

    instrument_label(comp, "OUTPUT", 164, 430, &ac_font_12, COLOR_TEXT2);
    instrument_meter(comp, &ch.comp_output, &ch.clip, 160, 456, 38, 300, &ch.comp_output_peak_hold_l, &ch.comp_output_rms_l);
    instrument_meter(comp, &ch.comp_output_r, &ch.clip, 204, 456, 38, 300, &ch.comp_output_peak_hold_r, &ch.comp_output_rms_r);

    // Dedicated Gain Reduction Meter with Downward Scale
    instrument_label(comp, "GAIN REDUCTION", 314, 430, &ac_font_12, COLOR_ORANGE);
    instrument_gr_meter(comp, &ch.comp_gr, 330, 456, 44, 300, &ch.comp_bypass, false);

    // Scale markings for GR Meter
    instrument_label(comp, "0 dB", 384, 456, &ac_font_12, COLOR_TEXT2);
    instrument_label(comp, "-3", 384, 492, &ac_font_12, COLOR_TEXT2);
    instrument_label(comp, "-6", 384, 528, &ac_font_12, COLOR_TEXT2);
    instrument_label(comp, "-12", 384, 600, &ac_font_12, COLOR_TEXT2);
    instrument_label(comp, "-18", 384, 672, &ac_font_12, COLOR_TEXT2);
    instrument_label(comp, "-24", 384, 740, &ac_font_12, COLOR_TEXT2);

    auto* gr_val = lv_label_create(comp);
    lv_label_set_text(gr_val, "  0.0 dB");
    lv_obj_set_pos(gr_val, 314, 774);
    lv_obj_set_style_text_font(gr_val, &ac_font_28, 0);
    lv_obj_set_style_text_color(gr_val, COLOR_CYAN, 0);
    view->gr_val_label = gr_val;

    // =========================================================================
    // 2. CENTER PANEL: TRANSFER CURVES
    // =========================================================================
    auto* graph_panel = instrument_panel(s, 544, 54, 796, 866, "TRANSFER CURVES");

    std::array<float, 61> comp_unity{};
    for (int i = 0; i < 61; ++i) comp_unity[i] = -60.0f + i * 1.0f;

    if (is_master) {
        // Master: Dedicated full-height Compressor / Limiter transfer curve. No gate curve!
        instrument_label(graph_panel, "COMP / LIMITER CURVE", 24, 36, &ac_font_14, COLOR_SILVER);
        auto* comp_chart = instrument_chart(graph_panel, 24, 62, 748, 740, 61);
        instrument_style_chart(comp_chart, -60, 12);
        instrument_chart_draggable_threshold(comp_chart, &ch.comp_thresh, -60.0f, 0.0f);
        instrument_chart_limiter_ceiling(comp_chart, &ch.limiter_ceiling_db, &ch.limiter_enabled);
        view->comp_chart = comp_chart;
        view->gate_chart = nullptr;

        instrument_chart_reference(comp_chart, comp_unity.data(), (int)comp_unity.size(), COLOR_TEXT2);
        instrument_chart_dynamics_markers(comp_chart, &ch.comp_thresh, &ch.comp_knee, &ch.comp_input, &ch.comp_curve_output);

        instrument_label(graph_panel, "-60 dB", 24, 812, &ac_font_12, COLOR_TEXT2);
        instrument_label(graph_panel, "-30 dB", 380, 812, &ac_font_12, COLOR_TEXT2);
        instrument_label(graph_panel, "0 dB", 730, 812, &ac_font_12, COLOR_TEXT2);
    } else {
        // Input / Aux: Dual Curves (Compressor on top, Noise Gate on bottom)
        instrument_label(graph_panel, "COMP / LIMITER CURVE", 24, 36, &ac_font_14, COLOR_SILVER);
        auto* comp_chart = instrument_chart(graph_panel, 24, 60, 748, 350, 61);
        instrument_style_chart(comp_chart, -60, 12);
        instrument_chart_draggable_threshold(comp_chart, &ch.comp_thresh, -60.0f, 0.0f);
        instrument_chart_limiter_ceiling(comp_chart, &ch.limiter_ceiling_db, &ch.limiter_enabled);
        view->comp_chart = comp_chart;

        instrument_chart_reference(comp_chart, comp_unity.data(), (int)comp_unity.size(), COLOR_TEXT2);
        instrument_chart_dynamics_markers(comp_chart, &ch.comp_thresh, &ch.comp_knee, &ch.comp_input, &ch.comp_curve_output);

        instrument_label(graph_panel, "-60 dB", 24, 416, &ac_font_12, COLOR_TEXT2);
        instrument_label(graph_panel, "-30 dB", 380, 416, &ac_font_12, COLOR_TEXT2);
        instrument_label(graph_panel, "0 dB", 730, 416, &ac_font_12, COLOR_TEXT2);

        instrument_label(graph_panel, "GATE CURVE", 24, 446, &ac_font_14, COLOR_SILVER);
        auto* gate_chart = instrument_chart(graph_panel, 24, 470, 748, 340, 61);
        instrument_style_chart(gate_chart, -80, 0);
        instrument_chart_draggable_threshold(gate_chart, &ch.gate_thresh, -80.0f, 0.0f);
        view->gate_chart = gate_chart;

        std::array<float, 61> gate_unity{};
        for (int i = 0; i < 61; ++i) gate_unity[i] = -80.0f + i * (80.0f / 60.0f);
        instrument_chart_reference(gate_chart, gate_unity.data(), (int)gate_unity.size(), COLOR_TEXT2);
        instrument_chart_dynamics_markers(gate_chart, &ch.gate_thresh, nullptr, &ch.gate_detector_db, &ch.gate_curve_output);

        instrument_label(graph_panel, "-80 dB", 24, 816, &ac_font_12, COLOR_TEXT2);
        instrument_label(graph_panel, "-40 dB", 380, 816, &ac_font_12, COLOR_TEXT2);
        instrument_label(graph_panel, "0 dB", 730, 816, &ac_font_12, COLOR_TEXT2);
    }

    // =========================================================================
    // 3. RIGHT PANEL: LIMITER (MASTER) vs NOISE GATE / LIMITER (INPUT / AUX)
    // =========================================================================
    lv_obj_t* k_lim_ceil = nullptr;
    lv_obj_t* k_grange = nullptr;

    if (is_master) {
        // Dedicated Limiter Bay for Master Bus — No Gate controls whatsoever
        auto* right = instrument_panel(s, 1352, 54, 552, 866, "LIMITER");

        instrument_button(right, "LIMITER ON", 32, 54, 220, 48, &ch.limiter_enabled);

        k_lim_ceil = instrument_knob(right, "CEILING", &ch.limiter_ceiling_db, -24.0f, 0.0f, -0.1f, "dBFS", 40, 130, 84);
        instrument_knob(right, "RELEASE", &ch.limiter_release_ms, 1.0f, 500.0f, 50.0f, "ms", 180, 130, 84);

        instrument_label(right, "PRE-LIM PEAK", 40, 280, &ac_font_12, COLOR_TEXT2);
        view->lim_pre_label = instrument_label(right, "-inf dB", 40, 304, &ac_font_20, COLOR_TEXT);

        instrument_label(right, "POST-LIM OUT", 180, 280, &ac_font_12, COLOR_TEXT2);
        view->lim_post_label = instrument_label(right, "-inf dB", 180, 304, &ac_font_20, COLOR_TEXT);

        instrument_label(right, "TOTAL GAIN REDUCTION", 40, 380, &ac_font_12, COLOR_TEXT2);
        view->lim_gr_label = instrument_label(right, "  0.0 dB", 40, 410, &ac_font_28, COLOR_GREEN);

        // Limiter GR Meter
        instrument_label(right, "LIM GR", 340, 54, &ac_font_12, COLOR_ORANGE);
        instrument_gr_meter(right, &ch.limiter_gr_db, 340, 80, 44, 400);

        // Calibrated scale marks
        instrument_label(right, "0 dB", 394, 80, &ac_font_12, COLOR_TEXT2);
        instrument_label(right, "-3", 394, 120, &ac_font_12, COLOR_TEXT2);
        instrument_label(right, "-6", 394, 180, &ac_font_12, COLOR_TEXT2);
        instrument_label(right, "-12", 394, 280, &ac_font_12, COLOR_TEXT2);
        instrument_label(right, "-18", 394, 380, &ac_font_12, COLOR_TEXT2);
        instrument_label(right, "-24", 394, 470, &ac_font_12, COLOR_TEXT2);

        view->gate_status_badge = nullptr;
        view->gate_thresh_knob = nullptr;
        view->gate_thresh_label = nullptr;
        view->gate_led = nullptr;
        view->key_source_btn = nullptr;
    } else {
        // Input / Aux: Gate + Sidechain + Limiter
        auto* right = instrument_panel(s, 1352, 54, 552, 866, "NOISE GATE / LIMITER");
        instrument_button(right, "GATE ENABLE", 18, 38, 220, 36, &ch.gate_on);

        instrument_label(right, "GATE STATE:", 260, 40, &ac_font_12, COLOR_TEXT2);
        auto* gate_status = lv_label_create(right);
        lv_label_set_text(gate_status, "OPEN");
        lv_obj_set_pos(gate_status, 260, 58);
        lv_obj_set_style_text_font(gate_status, &ac_font_20, 0);
        lv_obj_set_style_text_color(gate_status, COLOR_GREEN, 0);
        view->gate_status_badge = gate_status;

        // Gate Knobs (5 controls: Threshold, Range, Attack, Hold, Release)
        view->gate_thresh_knob = instrument_knob(right, "THRESH", &ch.gate_thresh, -80.0f, 0.0f, -48.0f, "dB", 22, 86, 68);
        k_grange = instrument_knob(right, "RANGE", &ch.gate_range, -80.0f, 0.0f, -60.0f, "dB", 186, 86, 68);
        instrument_knob(right, "ATTACK", &ch.gate_attack, 0.1f, 100.0f, 5.0f, "ms", 350, 86, 68);

        instrument_knob(right, "HOLD", &ch.gate_hold, 0.0f, 500.0f, 50.0f, "ms", 22, 214, 68);
        instrument_knob(right, "RELEASE", &ch.gate_release, 10.0f, 1000.0f, 120.0f, "ms", 186, 214, 68);

        // Gate Activity Sub-Panel
        auto* gate_bar = instrument_panel(right, 346, 214, 186, 148, "DETECTOR");
        instrument_label(gate_bar, "INPUT", 14, 32, &ac_font_12, COLOR_TEXT2);
        instrument_meter(gate_bar, &ch.gate_detector_db, &ch.clip, 12, 50, 34, 86);

        instrument_label(gate_bar, "THRESH", 60, 32, &ac_font_12, COLOR_TEXT2);
        auto* thresh_lbl = lv_label_create(gate_bar);
        lv_obj_set_pos(thresh_lbl, 60, 50);
        lv_obj_set_style_text_font(thresh_lbl, &ac_font_14, 0);
        lv_obj_set_style_text_color(thresh_lbl, COLOR_PRIMARY_TEXT, 0);
        char init_t[32];
        std::snprintf(init_t, sizeof(init_t), "%.1f dB", ch.gate_thresh);
        lv_label_set_text(thresh_lbl, init_t);
        view->gate_thresh_label = thresh_lbl;

        instrument_label(gate_bar, "STATE", 60, 82, &ac_font_12, COLOR_TEXT2);
        auto* led = lv_obj_create(gate_bar);
        lv_obj_set_pos(led, 60, 102);
        lv_obj_set_size(led, 18, 18);
        lv_obj_set_style_radius(led, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(led, COLOR_GREEN, 0);
        lv_obj_set_style_bg_opa(led, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(led, COLOR_GREEN, 0);
        lv_obj_set_style_border_width(led, 1, 0);
        view->gate_led = led;

        // Sidechain Section
        instrument_label(right, "SIDECHAIN KEY / FILTER", 18, 372, &ac_font_14, COLOR_SILVER);
        instrument_button(right, "SIDECHAIN IN", 18, 396, 220, 36, &ch.sidechain_on);

        auto* key_btn = instrument_button(right, kKeySources[ch.gate_key_source % 10], 254, 396, 276, 36, nullptr);
        view->key_source_btn = key_btn;
        struct KeyCtx { ChannelState* ch; lv_obj_t* btn; };
        auto* kc = new KeyCtx{&ch, key_btn};
        lv_obj_add_event_cb(key_btn, [](lv_event_t* e) {
            auto* c = static_cast<KeyCtx*>(lv_event_get_user_data(e));
            if (!c || !c->ch) return;
            c->ch->gate_key_source = (c->ch->gate_key_source + 1) % 10;
            if (lv_obj_get_child_count(c->btn) > 0) {
                lv_label_set_text(lv_obj_get_child(c->btn, 0), kKeySources[c->ch->gate_key_source % 10]);
            }
        }, LV_EVENT_CLICKED, kc);
        lv_obj_add_event_cb(key_btn, [](lv_event_t* e) {
            auto* c = static_cast<KeyCtx*>(lv_event_get_user_data(e));
            delete c;
        }, LV_EVENT_DELETE, kc);

        instrument_knob(right, "HPF", &ch.sidechain_low, 20.0f, 500.0f, 80.0f, "Hz", 26, 442, 64);
        instrument_knob(right, "LPF", &ch.sidechain_high, 500.0f, 20000.0f, 8000.0f, "Hz", 186, 442, 64);

        // Brickwall Limiter Sub-Panel
        auto* lim_panel = instrument_panel(right, 14, 582, 520, 268, "BRICKWALL LIMITER");
        instrument_button(lim_panel, "LIMITER ON", 14, 38, 128, 40, &ch.limiter_enabled);
        k_lim_ceil = instrument_knob(lim_panel, "CEILING", &ch.limiter_ceiling_db, -24.0f, 0.0f, -0.1f, "dB", 156, 38, 68);
        instrument_knob(lim_panel, "RELEASE", &ch.limiter_release_ms, 5.0f, 500.0f, 50.0f, "ms", 256, 38, 68);

        instrument_label(lim_panel, "LIM GR", 350, 34, &ac_font_12, COLOR_ORANGE);
        instrument_gr_meter(lim_panel, &ch.limiter_gr_db, 356, 54, 28, 182);

        instrument_label(lim_panel, "PRE-LIM PEAK", 400, 44, &ac_font_12, COLOR_TEXT2);
        view->lim_pre_label = instrument_label(lim_panel, "-inf dB", 400, 64, &ac_font_16, COLOR_TEXT);

        instrument_label(lim_panel, "POST-LIM OUT", 400, 104, &ac_font_12, COLOR_TEXT2);
        view->lim_post_label = instrument_label(lim_panel, "-inf dB", 400, 124, &ac_font_16, COLOR_TEXT);

        instrument_label(lim_panel, "GAIN RED.", 400, 164, &ac_font_12, COLOR_TEXT2);
        view->lim_gr_label = instrument_label(lim_panel, "  0.0 dB", 400, 184, &ac_font_20, COLOR_GREEN);
    }

    // Synchronize draggable threshold markers with knobs
    lv_obj_add_event_cb(view->comp_chart, [](lv_event_t* e) {
        auto* v = static_cast<DynamicsView*>(lv_event_get_user_data(e));
        if (!v) return;
        update_comp_curve(v);
        if (v->comp_thresh_knob) {
            instrument_knob_refresh(v->comp_thresh_knob);
        }
    }, LV_EVENT_VALUE_CHANGED, view);

    if (view->gate_chart) {
        lv_obj_add_event_cb(view->gate_chart, [](lv_event_t* e) {
            auto* v = static_cast<DynamicsView*>(lv_event_get_user_data(e));
            if (!v) return;
            update_gate_curve(v);
            if (v->gate_thresh_knob) {
                instrument_knob_refresh(v->gate_thresh_knob);
            }
            if (v->gate_thresh_label && v->ch) {
                char t_buf[32];
                std::snprintf(t_buf, sizeof(t_buf), "%.1f dB", v->ch->gate_thresh);
                lv_label_set_text(v->gate_thresh_label, t_buf);
            }
        }, LV_EVENT_VALUE_CHANGED, view);
    }

    // Turning knobs immediately updates the transfer curves and markers without waiting for timer
    for (auto* k : {view->comp_thresh_knob, k_ratio, k_knee, k_makeup, k_lim_ceil}) {
        if (!k) continue;
        lv_obj_add_event_cb(k, [](lv_event_t* e) {
            auto* v = static_cast<DynamicsView*>(lv_event_get_user_data(e));
            if (!v) return;
            update_comp_curve(v);
            if (v->comp_chart) lv_obj_invalidate(v->comp_chart);
        }, LV_EVENT_VALUE_CHANGED, view);
    }

    for (auto* k : {view->gate_thresh_knob, k_grange}) {
        if (!k) continue;
        lv_obj_add_event_cb(k, [](lv_event_t* e) {
            auto* v = static_cast<DynamicsView*>(lv_event_get_user_data(e));
            if (!v) return;
            update_gate_curve(v);
            if (v->gate_thresh_label && v->ch) {
                char t_buf[32];
                std::snprintf(t_buf, sizeof(t_buf), "%.1f dB", v->ch->gate_thresh);
                lv_label_set_text(v->gate_thresh_label, t_buf);
            }
            if (v->gate_chart) lv_obj_invalidate(v->gate_chart);
        }, LV_EVENT_VALUE_CHANGED, view);
    }

    // Timer for dynamics curves & meters
    view->anim_timer = lv_timer_create(update_dynamics_view, 60, view);

    lv_obj_add_event_cb(s, [](lv_event_t* e) {
        auto* v = static_cast<DynamicsView*>(lv_event_get_user_data(e));
        if (v) {
            if (v->anim_timer) {
                lv_timer_delete(v->anim_timer);
                v->anim_timer = nullptr;
            }
            delete v;
        }
    }, LV_EVENT_DELETE, view);

    return s;
}
