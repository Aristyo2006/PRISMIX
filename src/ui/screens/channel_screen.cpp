#include "channel_screen.h"
#include "../theme/theme.h"
#include "../widgets/instrument.h"
#include "../../model/mixer_state.h"

lv_obj_t* channel_screen_create(void) {
    auto* s = lv_obj_create(lv_screen_active()); lv_obj_set_pos(s,0,0); lv_obj_set_size(s,1920,1080); lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_style_bg_color(s, COLOR_BG, 0);
    auto& ch = g_mixer_state.channels[g_mixer_state.selected_channel];
    static bool stage_bypass[8]{};
    auto* in = instrument_panel(s, 16, 56, 300, 864, "INPUT / PREAMP");
    instrument_label(in, ch.name, 16, 45, &ac_font_28, COLOR_CYAN);
    instrument_label(in, ch.input_label, 16, 82, &ac_font_16, COLOR_TEXT2);
    instrument_knob(in, "GAIN", &ch.gain, 0, 1, .5f, "dB", 20, 144, 88);
    instrument_button(in, "POLARITY", 20, 278, 250, 48, &ch.polarity_inverted);
    instrument_button(in, "HPF  80 Hz", 20, 338, 250, 48, &ch.hpf_on);
    instrument_label(in, "INPUT METER", 18, 426, &ac_font_12, COLOR_TEXT2);
    instrument_meter(in, &ch.peak_l, &ch.clip, 55, 457, 46, 260, &ch.peak_hold_l, &ch.rms_l);
    instrument_meter(in, &ch.peak_r, &ch.clip, 125, 457, 46, 260, &ch.peak_hold_r, &ch.rms_r);
    instrument_button(in, "48 V PHANTOM", 20, 768, 250, 48, &ch.phantom_on);
    auto* path = instrument_panel(s, 336, 56, 1556, 864, "CHANNEL PROCESSING / SIGNAL FLOW");
    const char* names[] = {"INPUT", "GATE", "EQ", "COMP", "INSERT", "FX SENDS", "PAN", "MAIN L/R"};
    for (int i = 0; i < 8; ++i) {
        const int x = 20 + i * 190;
        auto* stage = instrument_panel(path, x, 104, 164, 190, names[i]);
        instrument_label(stage, i == 0 ? "PREAMP" : (i == 7 ? "STEREO" : "ACTIVE"), 15, 49, &ac_font_14, i == 7 ? COLOR_GREEN : COLOR_TEXT2);
        instrument_label(stage, i == 0 ? "IN 01" : "INSERT 01", 15, 79, &ac_font_12, COLOR_TEXT2);
        instrument_button(stage, i == 1 ? "GATE BYPASS" : (i == 2 ? "EQ BYPASS" : "BYPASS"), 12, 118, 138, 42, &stage_bypass[i]);
    }
    instrument_label(path, "CHANNEL TRIM", 25, 365, &ac_font_14, COLOR_TEXT2);
    instrument_knob(path, "PAN", &ch.pan, 0, 1, .5f, "L/C/R", 25, 405, 70);
    instrument_knob(path, "FADER", &ch.fader, 0, 1, .76f, "dB", 145, 405, 70);
    instrument_label(path, "OUTPUT ASSIGN", 390, 365, &ac_font_14, COLOR_TEXT2);
    for (int i = 0; i < 8; ++i) {
        auto* bus = instrument_button(path, "BUS", 390 + i * 132, 415, 112, 48, &ch.bus_assign[i]);
        lv_label_set_text_fmt(lv_obj_get_child(bus, 0), "BUS %02d", i + 1);
    }
    instrument_label(path, "TAP POINTS", 390, 518, &ac_font_14, COLOR_TEXT2);
    instrument_label(path, "PRE EQ     POST EQ     POST FADER     POST MUTE", 390, 556, &ac_font_16, COLOR_TEXT);
    instrument_label(path, "SOURCE      PREAMP      FILTER      DYNAMICS      INSERT      SENDS      PAN      OUTPUT", 390, 632, &ac_font_12, COLOR_TEXT2);
    instrument_label(path, "AUX SENDS", 390, 716, &ac_font_14, COLOR_TEXT2);
    for(int i=0;i<4;++i) instrument_knob(path,"BUS SEND",&ch.sends[i],0,1,.2f,"dB",390+i*170,742,62);
    return s;
}
