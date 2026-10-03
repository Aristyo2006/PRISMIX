#include "system_screen.h"
#include "../theme/theme.h"
#include "../widgets/instrument.h"
#include "../../model/mixer_state.h"

lv_obj_t* system_screen_create(void) {
    auto* s=lv_obj_create(lv_screen_active()); lv_obj_set_pos(s,0,0); lv_obj_set_size(s,1920,1080); lv_obj_remove_flag(s,LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_style_bg_color(s,COLOR_BG,0);
    auto* engine=instrument_panel(s,16,56,600,850,"ENGINE STATUS / SIMULATOR");
    instrument_label(engine,"MOCK ENGINE",24,54,&ac_font_28,COLOR_GREEN);
    instrument_label(engine,"READY   /   SIGNAL MODEL ACTIVE",24,102,&ac_font_16,COLOR_TEXT);
    instrument_label(engine,"NO AUDIO HARDWARE IS CONNECTED",24,144,&ac_font_14,COLOR_ORANGE);
    auto* reset=instrument_button(engine,"RESET PEAKS",24,214,260,48,nullptr);
    lv_obj_add_event_cb(reset,[](lv_event_t* e) {
        (void)e; for(auto& c:g_mixer_state.channels) { c.peak_l=-90.0f; c.peak_r=-90.0f; c.peak_hold_l=-90.0f; c.peak_hold_r=-90.0f; c.clip=false; }
        g_mixer_state.master.peak_l=-90.0f; g_mixer_state.master.peak_r=-90.0f; g_mixer_state.master.peak_hold_l=-90.0f; g_mixer_state.master.peak_hold_r=-90.0f; g_mixer_state.master.clip=false;
    },LV_EVENT_CLICKED,nullptr);
    instrument_label(engine,"DEVICE",24,314,&ac_font_14,COLOR_TEXT2); instrument_label(engine,"Desktop simulator / SDL2",210,314,&ac_font_16,COLOR_TEXT);
    instrument_label(engine,"SAMPLE RATE",24,370,&ac_font_14,COLOR_TEXT2); instrument_label(engine,"48 kHz",210,370,&ac_font_16,COLOR_TEXT);
    instrument_label(engine,"WORD LENGTH",24,426,&ac_font_14,COLOR_TEXT2); instrument_label(engine,"24-bit",210,426,&ac_font_16,COLOR_TEXT);
    instrument_label(engine,"BUFFER",24,482,&ac_font_14,COLOR_TEXT2); instrument_label(engine,"256 samples / simulated",210,482,&ac_font_16,COLOR_TEXT);
    instrument_label(engine,"JACK",24,558,&ac_font_14,COLOR_TEXT2); instrument_label(engine,"NOT CONNECTED",210,558,&ac_font_16,COLOR_ORANGE);
    instrument_label(engine,"ALSA",24,614,&ac_font_14,COLOR_TEXT2); instrument_label(engine,"NOT CONNECTED",210,614,&ac_font_16,COLOR_ORANGE);
    instrument_label(engine,"ROUTE MONITOR",24,708,&ac_font_14,COLOR_TEXT2); instrument_label(engine,"8 IN / 8 BUS / 2 OUT",210,708,&ac_font_16,COLOR_TEXT);
    auto* perf=instrument_panel(s,628,56,620,850,"PERFORMANCE / SIMULATED TELEMETRY");
    instrument_label(perf,"CPU LOAD",30,66,&ac_font_14,COLOR_TEXT2);
    auto* cpu=lv_bar_create(perf); lv_obj_set_pos(cpu,30,100); lv_obj_set_size(cpu,548,34); lv_bar_set_range(cpu,0,100); lv_bar_set_value(cpu,12,LV_ANIM_OFF);
    lv_obj_set_style_bg_color(cpu,COLOR_RECESSED,LV_PART_MAIN); lv_obj_set_style_bg_color(cpu,COLOR_GREEN,LV_PART_INDICATOR);
    instrument_label(perf,"12%",30,150,&ac_font_20,COLOR_TEXT);
    instrument_label(perf,"XRUN COUNT",30,236,&ac_font_14,COLOR_TEXT2); instrument_label(perf,"0   /   SIMULATION",30,272,&ac_font_28,COLOR_GREEN);
    instrument_label(perf,"TEMPERATURE",30,366,&ac_font_14,COLOR_TEXT2); instrument_label(perf,"42 °C   /   NOMINAL",30,402,&ac_font_20,COLOR_TEXT);
    instrument_label(perf,"ACTIVE CHANNELS",30,496,&ac_font_14,COLOR_TEXT2); instrument_label(perf,"08 INPUT + MAIN L/R",30,532,&ac_font_20,COLOR_TEXT);
    instrument_label(perf,"DISPLAY",30,626,&ac_font_14,COLOR_TEXT2); instrument_label(perf,"1920 x 1080 / LVGL + SDL2",30,662,&ac_font_16,COLOR_TEXT);
    auto* about=instrument_panel(s,1276,56,616,850,"AUDIO CORE / SYSTEM INFORMATION");
    instrument_label(about,"DIGITAL MIXING CONSOLE",24,62,&ac_font_20,COLOR_CYAN);
    instrument_label(about,"INTERFACE BUILD   1.0.0",24,116,&ac_font_14,COLOR_TEXT);
    instrument_label(about,"UI FRAMEWORK",24,200,&ac_font_14,COLOR_TEXT2); instrument_label(about,"LVGL 9.4.0",24,234,&ac_font_16,COLOR_TEXT);
    instrument_label(about,"DESKTOP BACKEND",24,292,&ac_font_14,COLOR_TEXT2); instrument_label(about,"SDL2  /  NATIVE WINDOWS",24,326,&ac_font_16,COLOR_TEXT);
    instrument_label(about,"AUDIO ENGINE",24,384,&ac_font_14,COLOR_TEXT2); instrument_label(about,"MOCK SIGNAL GENERATOR",24,418,&ac_font_16,COLOR_TEXT);
    instrument_label(about,"ARCHITECTURE",24,496,&ac_font_14,COLOR_TEXT2); instrument_label(about,"LVGL -> UI STATE -> MOCK BACKEND",24,530,&ac_font_16,COLOR_TEXT);
    instrument_label(about,"FUTURE TARGET",24,604,&ac_font_14,COLOR_TEXT2); instrument_label(about,"CONTROL API -> JACK -> ALSA -> S905X",24,638,&ac_font_14,COLOR_TEXT);
    instrument_label(about,"MOCK VALUES ARE NOT MEASURED AUDIO",24,746,&ac_font_12,COLOR_ORANGE);
    return s;
}
