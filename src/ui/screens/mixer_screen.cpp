#include "mixer_screen.h"
#include "../theme/theme.h"
#include "../widgets/instrument.h"
#include "../../model/mixer_state.h"
#include <cstdio>
#include <algorithm>
#include <utility>

namespace {
void select_channel(lv_event_t* e) {
    const int index = (int)(intptr_t)lv_event_get_user_data(e);
    g_mixer_state.selected_channel = index;
    for (int i = 0; i < NUM_CHANNELS; ++i) g_mixer_state.channels[i].select = i == index;
}
void reset_fader(lv_event_t* e) {
    auto* value = static_cast<float*>(lv_event_get_user_data(e));
    if (value) *value = .86f;
}
lv_obj_t* strip(lv_obj_t* screen, int i, int x, int width, bool master) {
    ChannelState* state = master ? &g_mixer_state.master : &g_mixer_state.channels[i];
    auto* p = instrument_panel(screen, x, 52, width, 914, master ? "STEREO MASTER" : "INPUT CHANNEL");
    char text[64];
    std::snprintf(text, sizeof(text), master ? "MAIN L/R" : "CH %02d   %s", master ? 0 : i + 1, state->name);
    instrument_label(p, text, 8, 32, &ac_font_16, master ? COLOR_CYAN : COLOR_TEXT);
    instrument_label(p, state->input_label, 8, 57, &ac_font_12, COLOR_TEXT2);
    auto* clip_flag=instrument_label(p, state->clip ? "CLIP" : "OK", width - 50, 35, &ac_font_12, state->clip ? COLOR_RED : COLOR_GREEN);
    lv_timer_create([](lv_timer_t* t) {
        auto* b=static_cast<std::pair<bool*,lv_obj_t*>*>(lv_timer_get_user_data(t));
        lv_label_set_text(b->second,*b->first?"CLIP":"OK"); lv_obj_set_style_text_color(b->second,*b->first?COLOR_RED:COLOR_GREEN,0);
    },100,new std::pair<bool*,lv_obj_t*>{&state->clip,clip_flag});
    instrument_label(p, "PEAK", 8, 91, &ac_font_12, COLOR_TEXT2);
    instrument_meter(p, &state->peak_l, &state->clip, 44, 86, 25, 142, &state->peak_hold_l, &state->rms_l);
    instrument_meter(p, &state->peak_r, &state->clip, 77, 86, 25, 142, &state->peak_hold_r, &state->rms_r);
    instrument_label(p, "L", 49, 232, &ac_font_12, COLOR_TEXT2);
    instrument_label(p, "R", 82, 232, &ac_font_12, COLOR_TEXT2);
    instrument_knob(p, "GAIN", &state->gain, 0.0f, 1.0f, .5f, "dB", 105, 91, 64);
    instrument_label(p, "-60     -40     -20      -6     0 dB", 9, 259, &ac_font_12, COLOR_TEXT2);
    auto* fader = lv_obj_create(p); lv_obj_set_pos(fader, 61, 282); lv_obj_set_size(fader, 48, 410);
    lv_obj_remove_flag(fader, LV_OBJ_FLAG_SCROLLABLE); lv_obj_add_flag(fader, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(fader, LV_OPA_TRANSP, 0); lv_obj_set_style_border_width(fader, 0, 0);
    lv_obj_set_user_data(fader, &state->fader); lv_obj_add_event_cb(fader, [](lv_event_t* ev) {
        auto* v = static_cast<float*>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(ev))));
        if (!v) return;
        if (lv_event_get_code(ev) == LV_EVENT_DRAW_MAIN) {
            lv_layer_t* layer = lv_event_get_layer(ev); lv_area_t a{}; lv_obj_get_coords(static_cast<lv_obj_t*>(lv_event_get_target(ev)), &a);
            lv_draw_rect_dsc_t track; lv_draw_rect_dsc_init(&track); track.bg_color=lv_color_hex(0x141719); track.bg_opa=LV_OPA_COVER; track.border_color=COLOR_EDGE; track.border_width=1;
            lv_area_t tr{a.x1+18,a.y1+4,a.x2-18,a.y2-4}; lv_draw_rect(layer,&track,&tr);
            const int y=a.y2-(int)(*v*lv_area_get_height(&a));
            lv_draw_rect_dsc_t cap; lv_draw_rect_dsc_init(&cap); cap.bg_color=COLOR_CONTROL; cap.bg_opa=LV_OPA_COVER; cap.border_color=COLOR_HIGHLIGHT; cap.border_width=2; cap.radius=2;
            lv_area_t ca{a.x1+2,y-17,a.x2-2,y+17}; lv_draw_rect(layer,&cap,&ca);
            lv_draw_rect_dsc_t line; lv_draw_rect_dsc_init(&line); line.bg_color=COLOR_CYAN; line.bg_opa=LV_OPA_COVER;
            lv_area_t la{ca.x1+6,y-1,ca.x2-6,y+1}; lv_draw_rect(layer,&line,&la); return;
        }
        if (lv_event_get_code(ev) == LV_EVENT_PRESSING) {
            lv_point_t d{}; lv_indev_get_vect(lv_indev_active(), &d); *v = std::clamp(*v - d.y / 410.0f, 0.0f, 1.0f);
        } else if (lv_event_get_code(ev) == LV_EVENT_DOUBLE_CLICKED) *v = .86f;
        else return;
        lv_obj_invalidate(static_cast<lv_obj_t*>(lv_event_get_target(ev)));
    }, LV_EVENT_ALL, nullptr);
    lv_obj_add_event_cb(fader, reset_fader, LV_EVENT_DOUBLE_CLICKED, &state->fader);
    instrument_label(p, "0", 17, 310, &ac_font_12, COLOR_TEXT2);
    instrument_label(p, "-12", 13, 372, &ac_font_12, COLOR_TEXT2);
    instrument_label(p, "-24", 13, 445, &ac_font_12, COLOR_TEXT2);
    instrument_label(p, "-40", 13, 520, &ac_font_12, COLOR_TEXT2);
    instrument_label(p, "-60", 13, 594, &ac_font_12, COLOR_TEXT2);
    auto* value = lv_label_create(p); lv_label_set_text(value, "0.0 dB"); lv_obj_align(value, LV_ALIGN_TOP_MID, 15, 702);
    lv_obj_set_style_text_font(value, &ac_font_20, 0); lv_obj_set_style_text_color(value, COLOR_TEXT, 0);
    lv_timer_create([](lv_timer_t* t) {
        auto* pair = static_cast<std::pair<float*,lv_obj_t*>*>(lv_timer_get_user_data(t));
        const float value=*pair->first;
        const float db=value<=.86f ? -60.0f+value*(60.0f/.86f) : (value-.86f)*(10.0f/.14f);
        lv_label_set_text_fmt(pair->second, "%+.1f dB", db);
    }, 100, new std::pair<float*,lv_obj_t*>{&state->fader,value});
    instrument_knob(p, "PAN", &state->pan, 0, 1, .5f, "L/C/R", 33, 735, 64);
    instrument_button(p, "MUTE", 112, 755, width - 120, 38, &state->mute);
    instrument_button(p, "SOLO", 112, 803, width - 120, 38, &state->solo);
    if (!master) {
        auto* select = instrument_button(p, "SELECT", 8, 851, width - 16, 42, &state->select);
        lv_obj_add_event_cb(select, select_channel, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    } else instrument_label(p, "BUS 01-08", 12, 861, &ac_font_14, COLOR_CYAN);
    return p;
}
}

lv_obj_t* mixer_screen_create(void) {
    auto* screen = lv_obj_create(lv_screen_active()); lv_obj_set_pos(screen,0,0); lv_obj_set_size(screen,1920,1080); lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, COLOR_BG, 0); lv_obj_set_style_pad_all(screen, 0, 0);
    const int width = 202, gap = 7;
    for (int i = 0; i < NUM_CHANNELS; ++i) strip(screen, i, 8 + i * (width + gap), width, false);
    strip(screen, 8, 8 + NUM_CHANNELS * (width + gap), 202, true);
    return screen;
}
