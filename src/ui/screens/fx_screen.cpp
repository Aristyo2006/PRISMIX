#include "fx_screen.h"
#include "../theme/theme.h"
#include "../widgets/instrument.h"
#include <array>

lv_obj_t* fx_screen_create(void) {
    auto* s = lv_obj_create(lv_screen_active()); lv_obj_set_pos(s,0,0); lv_obj_set_size(s,1920,1080); lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_style_bg_color(s, COLOR_BG, 0);
    static bool on[4]{}; static std::array<float,4> send{.3f,.2f,.15f,.1f};
    static std::array<float,4> mix{.25f,.2f,.3f,.2f}; static std::array<float,4> time{1.8f,320,.45f,.25f};
    static bool bypass[4]{};
    const char* names[] = {"REVERB / HALL", "DELAY / STEREO", "CHORUS / MOD", "FLANGER / MOD"};
    const char* param[] = {"DECAY", "TIME", "RATE", "RATE"};
    const char* units[] = {"s", "ms", "Hz", "Hz"};
    for (int i=0;i<4;++i) {
        int x=16+i*472;
        auto* p=instrument_panel(s,x,58,456,840,names[i]);
        instrument_button(p,"EFFECT",18,48,180,44,&on[i]);
        instrument_label(p,"STEREO PROCESSOR  /  BUS INSERT",220,62,&ac_font_12,COLOR_TEXT2);
        instrument_knob(p,"SEND",&send[i],0,1,.25f,"dB",36,146,92);
        instrument_knob(p,"MIX",&mix[i],0,1,.25f,"%",184,146,92);
        instrument_knob(p,param[i],&time[i],i==0?.1f:(i==1?1.0f:.05f),i==0?12.0f:(i==1?2000.0f:10.0f),time[i],units[i],332,146,92);
        if (i==1 || i==3) {
            static std::array<float,4> feedback{.25f,.35f,.2f,.3f};
            instrument_knob(p,"FEEDBACK",&feedback[i],0,.95f,.3f,"%",36,334,92);
        } else if (i==0) {
            static float pre_delay=24.0f; instrument_knob(p,"PRE DELAY",&pre_delay,0,250,24,"ms",36,334,92);
        } else {
            static float depth=.35f; instrument_knob(p,"DEPTH",&depth,0,1,.35f,"%",36,334,92);
        }
        instrument_label(p,"RETURN LEVEL",36,517,&ac_font_14,COLOR_TEXT2);
        auto* meter=lv_bar_create(p); lv_obj_set_pos(meter,36,550); lv_obj_set_size(meter,370,26); lv_bar_set_range(meter,0,100); lv_bar_set_value(meter,34+i*9,LV_ANIM_OFF);
        lv_obj_set_style_bg_color(meter,COLOR_RECESSED,LV_PART_MAIN); lv_obj_set_style_bg_color(meter,COLOR_GREEN,LV_PART_INDICATOR); lv_obj_set_style_radius(meter,1,LV_PART_MAIN); lv_obj_set_style_radius(meter,1,LV_PART_INDICATOR);
        instrument_label(p,"INSERT POINT   POST FADER",36,612,&ac_font_14,COLOR_TEXT2);
        instrument_button(p,"BYPASS",36,679,180,44,&bypass[i]);
        auto* tap=instrument_button(p,"TAP / SYNC",232,679,180,44,nullptr);
        struct TapBinding { float* value; float tapped; };
        lv_obj_add_event_cb(tap,[](lv_event_t* e) { auto* b=static_cast<TapBinding*>(lv_event_get_user_data(e)); *b->value=b->tapped; },LV_EVENT_CLICKED,new TapBinding{&time[i],i==0?2.0f:(i==1?500.0f:.5f)});
    }
    return s;
}
