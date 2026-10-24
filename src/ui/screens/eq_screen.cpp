#include "eq_screen.h"
#include "../theme/theme.h"
#include "../widgets/instrument.h"
#include "../../model/mixer_state.h"
#include "../../dsp/eq_model.h"
#include "../../dsp/analyzer_model.h"
#include "../../dsp/frequency_axis.h"
#include "../../backend/mock_audio_backend.h"
#include <array>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <vector>

namespace {

static GraphicEqModel s_geq_clipboard;
static bool s_geq_has_clipboard = false;

struct EqView {
    ChannelState* channel = nullptr;
    lv_obj_t* peq_container = nullptr;
    lv_obj_t* geq_container = nullptr;
    lv_obj_t* peq_btn = nullptr;
    lv_obj_t* geq_btn = nullptr;

    // PEQ graph & nodes
    lv_obj_t* chart = nullptr;
    lv_obj_t* nodes[4]{};
    int active_band = 0;
    lv_timer_t* timer = nullptr;

    // Band select buttons in right panel
    lv_obj_t* band_select_btns[4]{};
    lv_obj_t* filter_type_btns[4]{};

    // Knobs in right panel (need refresh when nodes dragged)
    lv_obj_t* freq_knobs[4]{};
    lv_obj_t* gain_knobs[4]{};
    lv_obj_t* q_knobs[4]{};

    // GEQ controls
    lv_obj_t* geq_chart = nullptr;
    std::vector<lv_obj_t*> geq_sliders;
    std::vector<lv_obj_t*> geq_labels;

    bool in_geq_mode = false;
};

const char* get_filter_type_name(int type) {
    switch (type) {
        case 0: return "LOW SHELF";
        case 1: return "BELL";
        case 2: return "HIGH SHELF";
        case 3: return "HPF (18dB)";
        case 4: return "LPF (18dB)";
        default: return "BELL";
    }
}

void update_eq_curve(lv_timer_t* t) {
    auto* v = static_cast<EqView*>(lv_timer_get_user_data(t));
    if (!v || !v->channel) return;

    if (!v->in_geq_mode) {
        if (!v->chart || lv_obj_has_flag(v->chart, LV_OBJ_FLAG_HIDDEN)) return;

        // 1. Evaluate 4-band parametric response across 61 log-spaced frequencies using shared mapping
        std::array<float, 61> curve{};
        for (int n = 0; n < 61; ++n) {
            const float hz = FrequencyAxis::norm_to_frequency((float)n / 60.0f);
            curve[n] = eq_response_db(hz, *v->channel);
        }

        // 2. Feed RTA spectrum
        auto rta_snap = g_backend.get_analyzer().get_snapshot();
        instrument_chart_rta_spectrum(v->chart, rta_snap);

        // 3. Draw EQ curve
        instrument_chart_series(v->chart, curve.data(), (int)curve.size(), -18, 18, lv_color_hex(0xE2E8F0));

        // 4. Update the 4 draggable node positions on the graph using unified FrequencyAxis
        lv_area_t plot_coords{};
        lv_obj_get_coords(lv_obj_get_parent(v->chart), &plot_coords);

        lv_area_t plot_area{};
        instrument_chart_get_plot_area(v->chart, &plot_area);

        const int xMin = plot_area.x1 - plot_coords.x1;
        const int xMax = plot_area.x2 - plot_coords.x1;
        const int yMin = plot_area.y1 - plot_coords.y1;
        const int yMax = plot_area.y2 - plot_coords.y1;

        for (int b = 0; b < 4; ++b) {
            if (!v->nodes[b]) continue;
            float freq = std::clamp(v->channel->eq_freq[b], 20.0f, 20000.0f);
            float gain = std::clamp(v->channel->eq_gain[b], -18.0f, 18.0f);

            float nx = FrequencyAxis::frequency_to_x(freq, (float)xMin, (float)xMax);
            float ny = (float)yMax - ((gain - (-18.0f)) / 36.0f) * (float)(yMax - yMin);

            lv_obj_set_pos(v->nodes[b], (int)std::round(nx) - 14, (int)std::round(ny) - 14);

            // Active band highlighting
            const bool is_act = (b == v->active_band);
            lv_obj_set_style_border_color(v->nodes[b], is_act ? lv_color_hex(0xF0F4F8) : lv_color_hex(0x283038), 0);
            lv_obj_set_style_border_width(v->nodes[b], is_act ? 2 : 1, 0);
            lv_obj_set_style_outline_color(v->nodes[b], lv_color_hex(0x3E4B59), 0);
            lv_obj_set_style_outline_width(v->nodes[b], is_act ? 2 : 0, 0);
        }
    } else {
        // In GEQ mode: update GEQ response preview
        if (!v->geq_chart || lv_obj_has_flag(v->geq_chart, LV_OBJ_FLAG_HIDDEN)) return;
        std::array<float, 61> geq_curve{};
        for (int n = 0; n < 61; ++n) {
            const float hz = FrequencyAxis::norm_to_frequency((float)n / 60.0f);
            geq_curve[n] = v->channel->geq.evaluate_response_db(hz);
        }
        auto rta_snap = g_backend.get_analyzer().get_snapshot();
        instrument_chart_rta_spectrum(v->geq_chart, rta_snap);
        instrument_chart_series(v->geq_chart, geq_curve.data(), (int)geq_curve.size(), -18, 18, COLOR_GREEN);
    }
}

void select_active_band(EqView* v, int band) {
    if (!v) return;
    v->active_band = std::clamp(band, 0, 3);
    for (int b = 0; b < 4; ++b) {
        if (v->band_select_btns[b]) {
            const bool sel = (b == v->active_band);
            lv_obj_set_style_border_color(v->band_select_btns[b], sel ? COLOR_SELECTED_BORDER : COLOR_EDGE, 0);
            lv_obj_set_style_border_width(v->band_select_btns[b], sel ? 2 : 1, 0);
            lv_obj_set_style_bg_color(v->band_select_btns[b], sel ? COLOR_RAISED_PANEL : COLOR_CONTROL, 0);
            if (lv_obj_get_child_count(v->band_select_btns[b]) > 0) {
                lv_obj_set_style_text_color(lv_obj_get_child(v->band_select_btns[b], 0), sel ? COLOR_PRIMARY_TEXT : COLOR_TEXT2, 0);
            }
        }
    }
}

void sync_eq_band_ui_from_state(EqView* v, int band) {
    if (!v || !v->channel || band < 0 || band >= 4) return;
    if (v->freq_knobs[band]) instrument_knob_refresh(v->freq_knobs[band]);
    if (v->gain_knobs[band]) instrument_knob_refresh(v->gain_knobs[band]);
    if (v->q_knobs[band]) instrument_knob_refresh(v->q_knobs[band]);
    if (v->filter_type_btns[band] && lv_obj_get_child_count(v->filter_type_btns[band]) > 0) {
        lv_label_set_text(lv_obj_get_child(v->filter_type_btns[band], 0),
                          get_filter_type_name(v->channel->eq_filter_type[band]));
    }
}

void sync_all_eq_ui_from_state(EqView* v) {
    if (!v) return;
    for (int b = 0; b < 4; ++b) {
        sync_eq_band_ui_from_state(v, b);
    }
}

void drag_eq_node(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_PRESSING) return;
    struct NodeData { EqView* view; int band; };
    auto* nd = static_cast<NodeData*>(lv_event_get_user_data(e));
    if (!nd || !nd->view || !nd->view->channel) return;

    select_active_band(nd->view, nd->band);

    auto* indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p{};
    lv_indev_get_point(indev, &p);

    lv_area_t plot_area{};
    instrument_chart_get_plot_area(nd->view->chart, &plot_area);
    const int cw = lv_area_get_width(&plot_area);
    const int ch = lv_area_get_height(&plot_area);
    if (cw <= 0 || ch <= 0) return;

    float new_freq = FrequencyAxis::x_to_frequency((float)p.x, (float)plot_area.x1, (float)plot_area.x2);
    float fy = std::clamp((float)(p.y - plot_area.y1) / (float)(ch - 1), 0.0f, 1.0f);
    float new_gain = 18.0f - 36.0f * fy;

    nd->view->channel->eq_freq[nd->band] = std::clamp(new_freq, 20.0f, 20000.0f);
    nd->view->channel->eq_gain[nd->band] = std::clamp(new_gain, -18.0f, 18.0f);
    sync_eq_band_ui_from_state(nd->view, nd->band);
}

void graph_direct_drag(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_PRESSING) return;
    auto* view = static_cast<EqView*>(lv_event_get_user_data(e));
    if (!view || !view->channel) return;
    auto* indev = lv_indev_active();
    if (!indev) return;
    lv_point_t p{};
    lv_indev_get_point(indev, &p);

    auto* chart_obj = static_cast<lv_obj_t*>(lv_event_get_target(e));
    lv_area_t plot_area{};
    instrument_chart_get_plot_area(chart_obj, &plot_area);
    const int32_t w = lv_area_get_width(&plot_area);
    const int32_t h = lv_area_get_height(&plot_area);
    if (w <= 0 || h <= 0) return;

    float new_freq = FrequencyAxis::x_to_frequency((float)p.x, (float)plot_area.x1, (float)plot_area.x2);
    float fy = std::clamp((float)(p.y - plot_area.y1) / (float)(h - 1), 0.0f, 1.0f);
    float new_gain = 18.0f - 36.0f * fy;

    auto& ch = *view->channel;
    const int act = std::clamp(view->active_band, 0, 3);
    ch.eq_freq[act] = std::clamp(new_freq, 20.0f, 20000.0f);
    ch.eq_gain[act] = std::clamp(new_gain, -18.0f, 18.0f);
    sync_eq_band_ui_from_state(view, act);
}

} // namespace

lv_obj_t* eq_screen_create(void) {
    auto* s = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(s, 0, 0);
    lv_obj_set_size(s, 1920, 1080);
    lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s, COLOR_BG, 0);

    ChannelState* ch_ptr = mixer_get_selected_channel();
    if (!ch_ptr) ch_ptr = mixer_get_channel(0);
    if (!ch_ptr) ch_ptr = &g_mixer_state.master;
    ChannelState& ch = *ch_ptr;

    auto* view = new EqView{};
    view->channel = &ch;
    view->in_geq_mode = ch.geq_mode_active;

    // Top Mode Toggle Bar (PARAMETRIC EQ vs GRAPHIC EQ 31-BAND)
    auto* top_bar = lv_obj_create(s);
    lv_obj_set_pos(top_bar, 16, 48);
    lv_obj_set_size(top_bar, 1888, 44);
    lv_obj_remove_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(top_bar, COLOR_PANEL, 0);
    lv_obj_set_style_border_color(top_bar, COLOR_EDGE, 0);
    lv_obj_set_style_border_width(top_bar, 1, 0);
    lv_obj_set_style_radius(top_bar, 1, 0);
    lv_obj_set_style_pad_all(top_bar, 4, 0);

    view->peq_btn = lv_button_create(top_bar);
    lv_obj_set_size(view->peq_btn, 220, 34);
    lv_obj_set_pos(view->peq_btn, 4, 1);
    lv_obj_set_style_radius(view->peq_btn, 1, 0);
    auto* peq_lbl = lv_label_create(view->peq_btn);
    lv_label_set_text(peq_lbl, "PARAMETRIC EQ");
    lv_obj_center(peq_lbl);
    lv_obj_set_style_text_font(peq_lbl, &ac_font_14, 0);

    view->geq_btn = lv_button_create(top_bar);
    lv_obj_set_size(view->geq_btn, 220, 34);
    lv_obj_set_pos(view->geq_btn, 232, 1);
    lv_obj_set_style_radius(view->geq_btn, 1, 0);
    auto* geq_lbl = lv_label_create(view->geq_btn);
    lv_label_set_text(geq_lbl, "GRAPHIC EQ (31-BAND)");
    lv_obj_center(geq_lbl);
    lv_obj_set_style_text_font(geq_lbl, &ac_font_14, 0);

    char ch_name_hdr[96];
    std::snprintf(ch_name_hdr, sizeof(ch_name_hdr), "ACTIVE: %s  /  %s", ch.name, ch.input_label);
    instrument_label(top_bar, ch_name_hdr, 500, 10, &ac_font_14, COLOR_SILVER);

    // =========================================================================
    // PEQ CONTAINER
    // =========================================================================
    view->peq_container = lv_obj_create(s);
    lv_obj_set_pos(view->peq_container, 0, 94);
    lv_obj_set_size(view->peq_container, 1920, 920);
    lv_obj_remove_flag(view->peq_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(view->peq_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(view->peq_container, 0, 0);
    lv_obj_set_style_pad_all(view->peq_container, 0, 0);

    // Left Area: 4-Band Parametric EQ Graph + RTA
    auto* plot = instrument_panel(view->peq_container, 16, 8, 1090, 866, "4-BAND PARAMETRIC EQUALIZER + REAL-TIME ANALYZER (RTA)");
    instrument_label(plot, "+18", 9, 58, &ac_font_12, COLOR_TEXT2);
    instrument_label(plot, "+12", 9, 180, &ac_font_12, COLOR_TEXT2);
    instrument_label(plot, "0 dB", 9, 420, &ac_font_12, COLOR_TEXT2);
    instrument_label(plot, "-12", 9, 660, &ac_font_12, COLOR_TEXT2);
    instrument_label(plot, "-18", 9, 780, &ac_font_12, COLOR_TEXT2);
    auto* chart = instrument_chart(plot, 54, 52, 1014, 746, 61);
    instrument_style_chart(chart, -18, 18);
    view->chart = chart;

    lv_obj_add_event_cb(chart, graph_direct_drag, LV_EVENT_PRESSING, view);

    // Frequency Tick Labels (20 Hz, 50, 100, 250, 500, 1k, 2k, 4k, 8k, 16k, 20k)
    // Placed using the unified FrequencyAxis mapping to match vertical grid lines, PEQ nodes, and RTA
    struct FreqTick {
        float freq;
        const char* text;
    };
    static const FreqTick kFreqTicks[] = {
        {20.0f, "20 Hz"},
        {50.0f, "50"},
        {100.0f, "100"},
        {250.0f, "250"},
        {500.0f, "500"},
        {1000.0f, "1k"},
        {2000.0f, "2k"},
        {4000.0f, "4k"},
        {8000.0f, "8k"},
        {16000.0f, "16k"},
        {20000.0f, "20k"}
    };

    const int plot_x_min = 54 + 10;                     // chart x (54) + inner pad (10) = 64
    const int plot_x_max = 54 + 1014 - 1 - 8;           // chart x (54) + chart w (1014) - 1 - right pad (8) = 1059
    const int num_ticks = sizeof(kFreqTicks) / sizeof(kFreqTicks[0]);

    for (int i = 0; i < num_ticks; ++i) {
        const auto& tick = kFreqTicks[i];
        float x = FrequencyAxis::frequency_to_x(tick.freq, (float)plot_x_min, (float)plot_x_max);

        auto* lbl = lv_label_create(plot);
        lv_label_set_text(lbl, tick.text);
        lv_obj_set_style_text_font(lbl, &ac_font_12, 0);
        lv_obj_set_style_text_color(lbl, (tick.freq == 1000.0f) ? COLOR_SILVER : COLOR_TEXT2, 0);

        lv_point_t size{};
        lv_text_get_size(&size, tick.text, &ac_font_12, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);

        int lx = (int)std::round(x - size.x / 2.0f);
        if (i == 0) {
            lx = plot_x_min; // Align left edge of 20 Hz label with plot border
        } else if (i == num_ticks - 1) {
            lx = plot_x_max - size.x; // Align right edge of 20 kHz label with plot border
        } else if (i == num_ticks - 2) {
            // Keep 16k label from colliding with 20k
            int max_16k_x = (plot_x_max - 22) - size.x - 2;
            if (lx > max_16k_x) lx = max_16k_x;
        }

        lv_obj_set_pos(lbl, lx, 804);
    }

    // 4 Draggable Interactive Nodes
    static const lv_color_t kNodeColors[4] = {
        lv_color_hex(0x566B78), // Band 1: calibrated studio slate blue/silver
        lv_color_hex(0x2E7D4E), // Band 2: studio green
        lv_color_hex(0xC68A24), // Band 3: studio amber
        lv_color_hex(0xB84A28)  // Band 4: studio brick red
    };
    static const char* kNodeLabels[4] = {"1", "2", "3", "4"};

    for (int b = 0; b < 4; ++b) {
        auto* node = lv_obj_create(plot);
        lv_obj_set_size(node, 28, 28);
        lv_obj_set_style_radius(node, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(node, kNodeColors[b], 0);
        lv_obj_set_style_bg_opa(node, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(node, (b == 0) ? lv_color_hex(0xF0F4F8) : lv_color_hex(0x283038), 0);
        lv_obj_set_style_border_width(node, (b == 0) ? 2 : 1, 0);
        if (b == 0) {
            lv_obj_set_style_outline_color(node, lv_color_hex(0x3E4B59), 0);
            lv_obj_set_style_outline_width(node, 2, 0);
        }
        lv_obj_remove_flag(node, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(node, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(node, LV_OBJ_FLAG_PRESS_LOCK);

        auto* num_lbl = lv_label_create(node);
        lv_label_set_text(num_lbl, kNodeLabels[b]);
        lv_obj_center(num_lbl);
        lv_obj_set_style_text_font(num_lbl, &ac_font_12, 0);
        lv_obj_set_style_text_color(num_lbl, COLOR_TEXT, 0);

        struct NodeData { EqView* view; int band; };
        auto* nd = new NodeData{view, b};
        lv_obj_set_user_data(node, nd);
        lv_obj_add_event_cb(node, [](lv_event_t* e) {
            auto* d = static_cast<NodeData*>(lv_event_get_user_data(e));
            delete d;
        }, LV_EVENT_DELETE, nd);

        lv_obj_add_event_cb(node, [](lv_event_t* e) {
            auto* d = static_cast<NodeData*>(lv_event_get_user_data(e));
            if (d && d->view) select_active_band(d->view, d->band);
        }, LV_EVENT_CLICKED, nd);

        lv_obj_add_event_cb(node, drag_eq_node, LV_EVENT_PRESSING, nd);

        view->nodes[b] = node;
    }

    // RTA Toolbar beneath graph
    auto* rta_bar = lv_obj_create(plot);
    lv_obj_set_pos(rta_bar, 54, 824);
    lv_obj_set_size(rta_bar, 1014, 34);
    lv_obj_remove_flag(rta_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(rta_bar, lv_color_hex(0x13171B), 0);
    lv_obj_set_style_border_color(rta_bar, COLOR_EDGE, 0);
    lv_obj_set_style_border_width(rta_bar, 1, 0);
    lv_obj_set_style_radius(rta_bar, 1, 0);
    lv_obj_set_style_pad_all(rta_bar, 0, 0);

    instrument_label(rta_bar, "RTA:", 12, 10, &ac_font_12, COLOR_TEXT2);

    auto add_rta_btn = [&](const char* lbl, int x, int w, auto click_fn) {
        auto* b = lv_button_create(rta_bar);
        lv_obj_set_pos(b, x, 3);
        lv_obj_set_size(b, w, 28);
        lv_obj_set_style_radius(b, 1, 0);
        lv_obj_set_style_bg_color(b, COLOR_CONTROL, 0);
        lv_obj_set_style_border_color(b, COLOR_EDGE, 0);
        lv_obj_set_style_border_width(b, 1, 0);
        auto* l = lv_label_create(b);
        lv_label_set_text(l, lbl);
        lv_obj_center(l);
        lv_obj_set_style_text_font(l, &ac_font_12, 0);
        lv_obj_set_style_text_color(l, COLOR_TEXT, 0);
        lv_obj_add_event_cb(b, click_fn, LV_EVENT_CLICKED, nullptr);
        return b;
    };

    add_rta_btn("31-BAND", 50, 76, [](lv_event_t*) { g_backend.get_analyzer().set_mode(RtaMode::BANDS_31); });
    add_rta_btn("63-BAND", 132, 76, [](lv_event_t*) { g_backend.get_analyzer().set_mode(RtaMode::BANDS_63); });
    add_rta_btn("FFT", 214, 62, [](lv_event_t*) { g_backend.get_analyzer().set_mode(RtaMode::FFT_SMOOTH); });
    add_rta_btn("PRE-EQ", 286, 72, [](lv_event_t*) { g_backend.get_analyzer().set_source(RtaSource::PRE_EQ); });
    add_rta_btn("POST-EQ", 364, 78, [](lv_event_t*) { g_backend.get_analyzer().set_source(RtaSource::POST_EQ); });
    add_rta_btn("FAST", 452, 58, [](lv_event_t*) { g_backend.get_analyzer().set_speed(RtaSpeed::FAST); });
    add_rta_btn("MED", 516, 54, [](lv_event_t*) { g_backend.get_analyzer().set_speed(RtaSpeed::MEDIUM); });
    add_rta_btn("SLOW", 576, 58, [](lv_event_t*) { g_backend.get_analyzer().set_speed(RtaSpeed::SLOW); });

    auto* freeze_btn = add_rta_btn("FREEZE", 646, 74, [](lv_event_t* e) {
        bool f = !g_backend.get_analyzer().is_frozen();
        g_backend.get_analyzer().set_freeze(f);
        auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
        lv_obj_set_style_bg_color(btn, f ? COLOR_RAISED_PANEL : COLOR_CONTROL, 0);
        lv_obj_set_style_border_color(btn, f ? COLOR_SELECTED_BORDER : COLOR_EDGE, 0);
    });
    (void)freeze_btn;

    add_rta_btn("CLEAR", 726, 66, [](lv_event_t*) { g_backend.get_analyzer().clear_peaks(); });

    // Right Panel: 4-Band Parameter Controls
    auto* ctl = instrument_panel(view->peq_container, 1124, 8, 780, 866, "BAND PARAMETERS  /  TOUCH NODE OR KNOB TO ADJUST");
    static const char* band_names[] = {"1: LOW", "2: LOW MID", "3: HIGH MID", "4: HIGH"};

    constexpr int EQ_COL_WIDTH = 188;
    constexpr int EQ_KNOB_WIDTH = 88;
    constexpr int EQ_KNOB_SIZE = 68;

    // Configurable layout constants for vertical rhythm:
    constexpr int EQ_KNOB_TO_VALUE_GAP = 12;        // 10-16 px standard (handled inside knob component)
    constexpr int EQ_VALUE_TO_NEXT_LABEL_GAP = 26;   // 20-28 px standard
    constexpr int EQ_VALUE_TO_BUTTON_GAP = 28;       // 20-28 px standard
    constexpr int EQ_BUTTON_GAP = 12;

    constexpr int EQ_HEADER_BTN_Y = 48;
    constexpr int EQ_HEADER_BTN_H = 42;

    constexpr int EQ_FREQ_Y = 110;
    constexpr int EQ_GAIN_Y = 248; // EQ_FREQ_Y + 112 + EQ_VALUE_TO_NEXT_LABEL_GAP
    constexpr int EQ_Q_Y    = 386; // EQ_GAIN_Y + 112 + EQ_VALUE_TO_NEXT_LABEL_GAP
    constexpr int EQ_BAND_IN_Y = 526; // EQ_Q_Y + 112 + EQ_VALUE_TO_BUTTON_GAP
    constexpr int EQ_FILTER_TYPE_Y = 580; // EQ_BAND_IN_Y + 42 + EQ_BUTTON_GAP

    const int x_start = 14;

    for (int b = 0; b < 4; ++b) {
        const int col_x = x_start + b * EQ_COL_WIDTH;

        // Band select header button (168px wide, centered in 188px column)
        const int hdr_x = col_x + (EQ_COL_WIDTH - 168) / 2;
        auto* select_btn = instrument_button(ctl, band_names[b], hdr_x, EQ_HEADER_BTN_Y, 168, EQ_HEADER_BTN_H, nullptr);
        view->band_select_btns[b] = select_btn;
        struct BSel { EqView* v; int band; };
        auto* bs = new BSel{view, b};
        lv_obj_add_event_cb(select_btn, [](lv_event_t* e) {
            auto* s = static_cast<BSel*>(lv_event_get_user_data(e));
            if (s && s->v) select_active_band(s->v, s->band);
        }, LV_EVENT_CLICKED, bs);
        lv_obj_add_event_cb(select_btn, [](lv_event_t* e) {
            auto* s = static_cast<BSel*>(lv_event_get_user_data(e));
            delete s;
        }, LV_EVENT_DELETE, bs);

        // FREQ, GAIN, Q Knobs (centered in column)
        const int knob_x = col_x + (EQ_COL_WIDTH - EQ_KNOB_WIDTH) / 2;
        view->freq_knobs[b] = instrument_knob(ctl, "FREQ", &ch.eq_freq[b], 20.0f, 20000.0f, ch.eq_freq[b], "Hz", knob_x, EQ_FREQ_Y, EQ_KNOB_SIZE);
        view->gain_knobs[b] = instrument_knob(ctl, "GAIN", &ch.eq_gain[b], -18.0f, 18.0f, 0.0f, "dB", knob_x, EQ_GAIN_Y, EQ_KNOB_SIZE);
        view->q_knobs[b] = instrument_knob(ctl, "Q", &ch.eq_q[b], 0.1f, 20.0f, 1.0f, "Q", knob_x, EQ_Q_Y, EQ_KNOB_SIZE);

        // BAND IN button (150px wide, centered in column)
        const int btn_x = col_x + (EQ_COL_WIDTH - 150) / 2;
        instrument_button(ctl, "BAND IN", btn_x, EQ_BAND_IN_Y, 150, 42, &ch.eq_enabled[b]);

        // FILTER TYPE cycle button (150px wide, centered in column)
        auto* type_btn = instrument_button(ctl, get_filter_type_name(ch.eq_filter_type[b]), btn_x, EQ_FILTER_TYPE_Y, 150, 42, nullptr);
        view->filter_type_btns[b] = type_btn;

        struct FTypeCtx { EqView* v; int band; lv_obj_t* btn; };
        auto* fc = new FTypeCtx{view, b, type_btn};
        lv_obj_add_event_cb(type_btn, [](lv_event_t* e) {
            auto* c = static_cast<FTypeCtx*>(lv_event_get_user_data(e));
            if (!c || !c->v || !c->v->channel) return;
            int cur = c->v->channel->eq_filter_type[c->band];
            if (c->band == 0) {
                // Low: Low Shelf (0) -> Bell (1) -> HPF (3)
                cur = (cur == 0) ? 1 : ((cur == 1) ? 3 : 0);
            } else if (c->band == 3) {
                // High: High Shelf (2) -> Bell (1) -> LPF (4)
                cur = (cur == 2) ? 1 : ((cur == 1) ? 4 : 2);
            } else {
                // Mids: Bell (1) -> Low Shelf (0) -> High Shelf (2)
                cur = (cur == 1) ? 0 : ((cur == 0) ? 2 : 1);
            }
            c->v->channel->eq_filter_type[c->band] = cur;
            c->v->channel->eq_shelf[c->band] = (cur == 0 || cur == 2);
            if (lv_obj_get_child_count(c->btn) > 0) {
                lv_label_set_text(lv_obj_get_child(c->btn, 0), get_filter_type_name(cur));
            }
        }, LV_EVENT_CLICKED, fc);
        lv_obj_add_event_cb(type_btn, [](lv_event_t* e) {
            auto* c = static_cast<FTypeCtx*>(lv_event_get_user_data(e));
            delete c;
        }, LV_EVENT_DELETE, fc);
    }

    instrument_button(ctl, "EQ BYPASS", 24, 770, 180, 48, &ch.eq_bypass);

    auto* flat_btn = instrument_button(ctl, "FLAT ALL", 224, 770, 160, 48, nullptr);
    struct ActionCtx { EqView* v; };
    auto* flat_act = new ActionCtx{view};
    lv_obj_add_event_cb(flat_btn, [](lv_event_t* e) {
        auto* a = static_cast<ActionCtx*>(lv_event_get_user_data(e));
        if (a && a->v && a->v->channel) {
            a->v->channel->eq_gain.fill(0.0f);
            sync_all_eq_ui_from_state(a->v);
        }
    }, LV_EVENT_CLICKED, flat_act);
    lv_obj_add_event_cb(flat_btn, [](lv_event_t* e) {
        delete static_cast<ActionCtx*>(lv_event_get_user_data(e));
    }, LV_EVENT_DELETE, flat_act);

    auto* reset_btn = instrument_button(ctl, "RESET DEFAULT", 404, 770, 180, 48, nullptr);
    auto* reset_act = new ActionCtx{view};
    lv_obj_add_event_cb(reset_btn, [](lv_event_t* e) {
        auto* a = static_cast<ActionCtx*>(lv_event_get_user_data(e));
        if (a && a->v && a->v->channel) {
            auto* c = a->v->channel;
            c->eq_gain.fill(0.0f);
            c->eq_freq = {80.0f, 350.0f, 2500.0f, 10000.0f};
            c->eq_q = {0.7f, 1.0f, 1.0f, 0.7f};
            c->eq_enabled = {true, true, true, true};
            c->eq_filter_type = {0, 1, 1, 2};
            c->eq_bypass = false;
            sync_all_eq_ui_from_state(a->v);
        }
    }, LV_EVENT_CLICKED, reset_act);
    lv_obj_add_event_cb(reset_btn, [](lv_event_t* e) {
        delete static_cast<ActionCtx*>(lv_event_get_user_data(e));
    }, LV_EVENT_DELETE, reset_act);

    // =========================================================================
    // GEQ CONTAINER (31-Band Graphic EQ)
    // =========================================================================
    view->geq_container = lv_obj_create(s);
    lv_obj_set_pos(view->geq_container, 0, 94);
    lv_obj_set_size(view->geq_container, 1920, 920);
    lv_obj_remove_flag(view->geq_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(view->geq_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(view->geq_container, 0, 0);
    lv_obj_set_style_pad_all(view->geq_container, 0, 0);

    auto* geq_panel = instrument_panel(view->geq_container, 16, 8, 1888, 866, "31-BAND 1/3-OCTAVE GRAPHIC EQUALIZER (ISO STANDARD)");

    // GEQ Response Preview Chart
    view->geq_chart = instrument_chart(geq_panel, 24, 46, 1840, 180, 61);
    instrument_style_chart(view->geq_chart, -18, 18);
    instrument_label(geq_panel, "+12 dB", 1840, 52, &ac_font_12, COLOR_TEXT2);
    instrument_label(geq_panel, "0 dB", 1840, 130, &ac_font_12, COLOR_TEXT2);
    instrument_label(geq_panel, "-12 dB", 1840, 208, &ac_font_12, COLOR_TEXT2);

    // 31 Vertical Sliders
    const auto& geq_labels = GraphicEqModel::get_labels();
    const int start_x = 32;
    const int slider_spacing = 58;

    for (size_t i = 0; i < 31; ++i) {
        const int sx = start_x + (int)i * slider_spacing;

        // Vertical Slider for band gain (-12 dB to +12 dB)
        auto* slider = lv_slider_create(geq_panel);
        lv_obj_set_pos(slider, sx + 12, 246);
        lv_obj_set_size(slider, 16, 440);
        lv_slider_set_range(slider, -120, 120); // 0.1 dB resolution
        lv_slider_set_value(slider, (int32_t)(ch.geq.get_band_gain(i) * 10.0f), LV_ANIM_OFF);

        // Styling: console fader slot
        lv_obj_set_style_bg_color(slider, lv_color_hex(0x0C0E10), LV_PART_MAIN);
        lv_obj_set_style_border_color(slider, lv_color_hex(0x2B3238), LV_PART_MAIN);
        lv_obj_set_style_border_width(slider, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(slider, 2, LV_PART_MAIN);

        lv_obj_set_style_bg_color(slider, lv_color_hex(0x64748B), LV_PART_INDICATOR);
        lv_obj_set_style_radius(slider, 1, LV_PART_INDICATOR);

        // Tactile console fader knob
        lv_obj_set_style_bg_color(slider, lv_color_hex(0x38424E), LV_PART_KNOB);
        lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
        lv_obj_set_style_bg_grad_color(slider, lv_color_hex(0x1E242C), LV_PART_KNOB);
        lv_obj_set_style_bg_grad_dir(slider, LV_GRAD_DIR_VER, LV_PART_KNOB);
        lv_obj_set_style_border_color(slider, lv_color_hex(0x8A9BB0), LV_PART_KNOB);
        lv_obj_set_style_border_width(slider, 1, LV_PART_KNOB);
        lv_obj_set_style_pad_hor(slider, 6, LV_PART_KNOB);
        lv_obj_set_style_pad_ver(slider, 2, LV_PART_KNOB);
        lv_obj_set_style_radius(slider, 2, LV_PART_KNOB);

        // Value text below
        char v_str[16];
        float g_val = ch.geq.get_band_gain(i);
        std::snprintf(v_str, sizeof(v_str), "%+.1f", g_val);
        auto* val_lbl = lv_label_create(geq_panel);
        lv_label_set_text(val_lbl, v_str);
        lv_obj_set_pos(val_lbl, sx + 2, 696);
        lv_obj_set_style_text_font(val_lbl, &ac_font_12, 0);
        lv_obj_set_style_text_color(val_lbl, COLOR_TEXT, 0);

        // Frequency Label below
        auto* f_lbl = lv_label_create(geq_panel);
        lv_label_set_text(f_lbl, geq_labels[i]);
        lv_obj_set_pos(f_lbl, sx + 4, 718);
        lv_obj_set_style_text_font(f_lbl, &ac_font_12, 0);
        lv_obj_set_style_text_color(f_lbl, (i == 15) ? COLOR_SILVER : COLOR_TEXT2, 0);

        struct GeqSliderCtx { ChannelState* ch; size_t band; lv_obj_t* readout; };
        auto* sc = new GeqSliderCtx{&ch, i, val_lbl};
        lv_obj_add_event_cb(slider, [](lv_event_t* e) {
            auto* c = static_cast<GeqSliderCtx*>(lv_event_get_user_data(e));
            if (!c || !c->ch) return;
            auto* sl = static_cast<lv_obj_t*>(lv_event_get_target(e));
            float db = (float)lv_slider_get_value(sl) / 10.0f;
            c->ch->geq.set_band_gain(c->band, db);
            char s[16];
            std::snprintf(s, sizeof(s), "%+.1f", db);
            lv_label_set_text(c->readout, s);
        }, LV_EVENT_VALUE_CHANGED, sc);

        // Double click slider to reset to 0 dB
        lv_obj_add_event_cb(slider, [](lv_event_t* e) {
            auto* c = static_cast<GeqSliderCtx*>(lv_event_get_user_data(e));
            if (!c || !c->ch) return;
            auto* sl = static_cast<lv_obj_t*>(lv_event_get_target(e));
            c->ch->geq.set_band_gain(c->band, 0.0f);
            lv_slider_set_value(sl, 0, LV_ANIM_OFF);
            lv_label_set_text(c->readout, "0.0");
        }, LV_EVENT_DOUBLE_CLICKED, sc);

        lv_obj_add_event_cb(slider, [](lv_event_t* e) {
            auto* c = static_cast<GeqSliderCtx*>(lv_event_get_user_data(e));
            delete c;
        }, LV_EVENT_DELETE, sc);

        view->geq_sliders.push_back(slider);
        view->geq_labels.push_back(val_lbl);
    }

    // GEQ Control Buttons Bar
    auto* geq_bypass = instrument_button(geq_panel, "GEQ BYPASS", 32, 770, 160, 46, &ch.geq_bypass);
    lv_obj_add_event_cb(geq_bypass, [](lv_event_t* event) {
        auto* channel = static_cast<ChannelState*>(lv_event_get_user_data(event));
        if (channel) channel->geq.set_bypass(channel->geq_bypass);
    }, LV_EVENT_CLICKED, &ch);

    auto* geq_flat = instrument_button(geq_panel, "FLAT", 204, 770, 140, 46, nullptr);
    lv_obj_add_event_cb(geq_flat, [](lv_event_t* e) {
        auto* v = static_cast<EqView*>(lv_event_get_user_data(e));
        if (!v || !v->channel) return;
        v->channel->geq.reset_flat();
        for (size_t b = 0; b < v->geq_sliders.size(); ++b) {
            lv_slider_set_value(v->geq_sliders[b], 0, LV_ANIM_OFF);
            if (b < v->geq_labels.size()) lv_label_set_text(v->geq_labels[b], "0.0");
        }
    }, LV_EVENT_CLICKED, view);

    auto* geq_copy = instrument_button(geq_panel, "COPY", 356, 770, 130, 46, nullptr);
    lv_obj_add_event_cb(geq_copy, [](lv_event_t* e) {
        auto* v = static_cast<EqView*>(lv_event_get_user_data(e));
        if (v && v->channel) {
            v->channel->geq.copy_settings(s_geq_clipboard);
            s_geq_has_clipboard = true;
        }
    }, LV_EVENT_CLICKED, view);

    auto* geq_paste = instrument_button(geq_panel, "PASTE", 498, 770, 130, 46, nullptr);
    lv_obj_add_event_cb(geq_paste, [](lv_event_t* e) {
        auto* v = static_cast<EqView*>(lv_event_get_user_data(e));
        if (v && v->channel && s_geq_has_clipboard) {
            s_geq_clipboard.copy_settings(v->channel->geq);
            for (size_t b = 0; b < v->geq_sliders.size(); ++b) {
                float db = v->channel->geq.get_band_gain(b);
                lv_slider_set_value(v->geq_sliders[b], (int32_t)(db * 10.0f), LV_ANIM_OFF);
                char str[16];
                std::snprintf(str, sizeof(str), "%+.1f", db);
                if (b < v->geq_labels.size()) lv_label_set_text(v->geq_labels[b], str);
            }
        }
    }, LV_EVENT_CLICKED, view);

    // Initial mode state display
    auto sync_mode_visibility = [view]() {
        if (!view->in_geq_mode) {
            lv_obj_remove_flag(view->peq_container, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(view->geq_container, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(view->peq_btn, COLOR_RAISED_PANEL, 0);
            lv_obj_set_style_border_color(view->peq_btn, COLOR_SELECTED_BORDER, 0);
            lv_obj_set_style_border_width(view->peq_btn, 2, 0);
            lv_obj_set_style_text_color(lv_obj_get_child(view->peq_btn, 0), COLOR_PRIMARY_TEXT, 0);

            lv_obj_set_style_bg_color(view->geq_btn, COLOR_CONTROL, 0);
            lv_obj_set_style_border_color(view->geq_btn, COLOR_STANDARD_BORDER, 0);
            lv_obj_set_style_border_width(view->geq_btn, 1, 0);
            lv_obj_set_style_text_color(lv_obj_get_child(view->geq_btn, 0), COLOR_SECONDARY_TEXT, 0);
        } else {
            lv_obj_add_flag(view->peq_container, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(view->geq_container, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(view->peq_btn, COLOR_CONTROL, 0);
            lv_obj_set_style_border_color(view->peq_btn, COLOR_STANDARD_BORDER, 0);
            lv_obj_set_style_border_width(view->peq_btn, 1, 0);
            lv_obj_set_style_text_color(lv_obj_get_child(view->peq_btn, 0), COLOR_SECONDARY_TEXT, 0);

            lv_obj_set_style_bg_color(view->geq_btn, COLOR_RAISED_PANEL, 0);
            lv_obj_set_style_border_color(view->geq_btn, COLOR_SELECTED_BORDER, 0);
            lv_obj_set_style_border_width(view->geq_btn, 2, 0);
            lv_obj_set_style_text_color(lv_obj_get_child(view->geq_btn, 0), COLOR_PRIMARY_TEXT, 0);
        }
    };
    sync_mode_visibility();

    struct ModeBtnCtx { EqView* view; decltype(sync_mode_visibility) sync_fn; };
    auto* ctx = new ModeBtnCtx{view, sync_mode_visibility};
    lv_obj_add_event_cb(s, [](lv_event_t* e) {
        auto* c = static_cast<ModeBtnCtx*>(lv_event_get_user_data(e));
        delete c;
    }, LV_EVENT_DELETE, ctx);

    lv_obj_add_event_cb(view->peq_btn, [](lv_event_t* e) {
        auto* c = static_cast<ModeBtnCtx*>(lv_event_get_user_data(e));
        if (c && c->view && c->view->channel) {
            c->view->in_geq_mode = false;
            c->view->channel->geq_mode_active = false;
            c->sync_fn();
        }
    }, LV_EVENT_CLICKED, ctx);

    lv_obj_add_event_cb(view->geq_btn, [](lv_event_t* e) {
        auto* c = static_cast<ModeBtnCtx*>(lv_event_get_user_data(e));
        if (c && c->view && c->view->channel) {
            c->view->in_geq_mode = true;
            c->view->channel->geq_mode_active = true;
            c->sync_fn();
        }
    }, LV_EVENT_CLICKED, ctx);

    select_active_band(view, 0);
    view->timer = lv_timer_create(update_eq_curve, 60, view);

    lv_obj_add_event_cb(s, [](lv_event_t* e) {
        auto* v = static_cast<EqView*>(lv_event_get_user_data(e));
        if (v) {
            if (v->timer) {
                lv_timer_delete(v->timer);
                v->timer = nullptr;
            }
            delete v;
        }
    }, LV_EVENT_DELETE, view);

    return s;
}
