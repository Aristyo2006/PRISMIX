#include "eq_screen.h"
#include "../theme/theme.h"
#include "../widgets/instrument.h"
#include "../../model/mixer_state.h"
#include "../../dsp/eq_model.h"
#include <array>
#include <cmath>
#include <algorithm>

namespace {
struct EqView { ChannelState* channel; lv_obj_t* chart; lv_obj_t* node; int active = 0; };
struct BandBinding { EqView* view; int band; };
void draw_eq(lv_timer_t* t) {
    auto* v = static_cast<EqView*>(lv_timer_get_user_data(t));
    std::array<float,61> curve{};
    for (int n=0; n<61; ++n) {
        const float hz = 20.0f * std::pow(1000.0f, n / 60.0f);
        curve[n] = eq_response_db(hz,*v->channel);
    }
    instrument_chart_series(v->chart, curve.data(), (int)curve.size(), -12, 12, COLOR_CYAN);
    const float fx=std::log(std::max(20.0f,v->channel->eq_freq[v->active])/20.0f)/std::log(1000.0f);
    const float fy=(12.0f-v->channel->eq_gain[v->active])/24.0f;
    lv_obj_set_pos(v->node,54+(int)(fx*1014.0f)-8,52+(int)(fy*752.0f)-8);
}
void graph_drag(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_PRESSING) return;
    auto* view = static_cast<EqView*>(lv_event_get_user_data(e));
    lv_point_t p{}; lv_indev_get_point(lv_indev_active(), &p);
    lv_area_t a{}; lv_obj_get_coords(static_cast<lv_obj_t*>(lv_event_get_target(e)), &a);
    const float x = std::clamp((p.x-a.x1)/(float)lv_area_get_width(&a), 0.0f, 1.0f);
    const float y = std::clamp((p.y-a.y1)/(float)lv_area_get_height(&a), 0.0f, 1.0f);
    auto& ch = *view->channel;
    ch.eq_freq[view->active] = 20.0f * std::pow(1000.0f, x);
    ch.eq_gain[view->active] = 12.0f - 24.0f*y;
}
}

lv_obj_t* eq_screen_create(void) {
    auto* s = lv_obj_create(lv_screen_active()); lv_obj_set_pos(s,0,0); lv_obj_set_size(s,1920,1080); lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_style_bg_color(s, COLOR_BG, 0);
    auto& ch = g_mixer_state.channels[g_mixer_state.selected_channel];
    auto* plot = instrument_panel(s, 16, 54, 1090, 866, "4-BAND PARAMETRIC EQUALIZER   /   20 Hz - 20 kHz");
    instrument_label(plot, "+12", 9, 58, &ac_font_12, COLOR_TEXT2); instrument_label(plot, "0", 25, 444, &ac_font_12, COLOR_TEXT2); instrument_label(plot, "-12", 9, 824, &ac_font_12, COLOR_TEXT2);
    instrument_label(plot, "20 Hz      50       100                 500        1k                  5k       10k      20k", 48, 824, &ac_font_12, COLOR_TEXT2);
    auto* chart = instrument_chart(plot, 54, 52, 1014, 752, 61); instrument_style_chart(chart, -12, 12);
    auto* node=lv_obj_create(plot); lv_obj_set_size(node,16,16); lv_obj_set_style_radius(node,LV_RADIUS_CIRCLE,0);
    lv_obj_set_style_bg_color(node,COLOR_CYAN,0); lv_obj_set_style_border_color(node,COLOR_TEXT,0); lv_obj_set_style_border_width(node,2,0);
    lv_obj_remove_flag(node,LV_OBJ_FLAG_CLICKABLE); lv_obj_remove_flag(node,LV_OBJ_FLAG_SCROLLABLE);
    auto* view = new EqView{&ch, chart, node, 0}; lv_obj_add_event_cb(chart, graph_drag, LV_EVENT_PRESSING, view);
    lv_timer_create(draw_eq, 100, view);
    auto* ctl = instrument_panel(s, 1124, 54, 780, 866, "BAND PARAMETERS / DRAG NODE TO EDIT");
    static const char* names[] = {"LOW", "LOW MID", "HIGH MID", "HIGH"};
    for (int b=0; b<4; ++b) {
        const int x = 18 + b*190;
        auto* select = instrument_button(ctl, names[b], x, 54, 166, 46, nullptr);
        auto* binding = new BandBinding{view, b};
        lv_obj_add_event_cb(select, [](lv_event_t* e) { auto* b=static_cast<BandBinding*>(lv_event_get_user_data(e)); b->view->active=b->band; }, LV_EVENT_CLICKED, binding);
        instrument_knob(ctl, "FREQ", &ch.eq_freq[b], 20, 20000, ch.eq_freq[b], "Hz", x+40, 120, 64);
        instrument_knob(ctl, "GAIN", &ch.eq_gain[b], -12, 12, 0, "dB", x+40, 266, 64);
        instrument_knob(ctl, "Q", &ch.eq_q[b], .1f, 10, 1, "Q", x+40, 412, 64);
        instrument_button(ctl, "BAND IN", x+15, 565, 136, 42, &ch.eq_enabled[b]);
        instrument_button(ctl, b == 0 ? "LOW SHELF" : (b == 3 ? "HIGH SHELF" : "BELL"), x+15, 620, 136, 42, &ch.eq_shelf[b]);
    }
    instrument_label(ctl, "PARAMETRIC  /  STEREO LINK  /  24 dB OCT", 20, 728, &ac_font_14, COLOR_TEXT2);
    instrument_button(ctl, "EQ BYPASS", 20, 786, 180, 46, &ch.eq_bypass);
    auto* flat=instrument_button(ctl, "FLAT", 214, 786, 160, 46, nullptr);
    lv_obj_add_event_cb(flat,[](lv_event_t* e) { auto* c=static_cast<ChannelState*>(lv_event_get_user_data(e)); c->eq_gain.fill(0.0f); },LV_EVENT_CLICKED,&ch);
    return s;
}
