#include "ui.h"
#include "theme/theme.h"
#include "screens/mixer_screen.h"
#include "screens/channel_screen.h"
#include "screens/eq_screen.h"
#include "screens/dynamics_screen.h"
#include "screens/fx_screen.h"
#include "screens/routing_screen.h"
#include "screens/system_screen.h"
#include "widgets/instrument.h"
#include "../model/mixer_state.h"

namespace {
lv_obj_t* screens[7]{};
lv_obj_t* nav_buttons[7]{};
lv_obj_t* channel_status{};
int active_tab = 0;
const char* tabs[] = {"MIXER", "CHANNEL", "EQ", "DYNAMICS", "FX", "ROUTING", "SYSTEM"};

void paint_nav() {
    for (int i=0; i<7; ++i) {
        const bool selected = i == active_tab;
        lv_obj_set_style_bg_color(nav_buttons[i], selected ? lv_color_hex(0x283238) : COLOR_PANEL, 0);
        lv_obj_set_style_border_color(nav_buttons[i], selected ? COLOR_CYAN : COLOR_EDGE, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(nav_buttons[i],0), selected ? COLOR_CYAN : COLOR_TEXT2, 0);
    }
}
void nav_event(lv_event_t* e) {
    active_tab = (int)(intptr_t)lv_event_get_user_data(e);
    for (int i=0;i<7;++i) {
        if (i==active_tab) lv_obj_remove_flag(screens[i],LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(screens[i],LV_OBJ_FLAG_HIDDEN);
    }
    paint_nav();
}
void status_event(lv_timer_t*) {
    auto& ch = g_mixer_state.channels[g_mixer_state.selected_channel];
    lv_label_set_text_fmt(channel_status,"SELECTED  CH %02d  /  %s",g_mixer_state.selected_channel+1,ch.name);
}
}

void ui_init(void) {
    theme_init();
    screens[0] = mixer_screen_create(); screens[1] = channel_screen_create();
    screens[2] = eq_screen_create(); screens[3] = dynamics_screen_create();
    screens[4] = fx_screen_create(); screens[5] = routing_screen_create();
    screens[6] = system_screen_create();
    for (int i=1;i<7;++i) lv_obj_add_flag(screens[i],LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(screens[0],LV_OBJ_FLAG_HIDDEN);

    auto* top = lv_obj_create(lv_layer_top()); lv_obj_set_size(top,1920,44); lv_obj_align(top,LV_ALIGN_TOP_MID,0,0);
    lv_obj_remove_flag(top,LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_style_bg_color(top,COLOR_PANEL,0);
    lv_obj_set_style_bg_opa(top,LV_OPA_COVER,0);
    lv_obj_set_style_border_color(top,COLOR_EDGE,0); lv_obj_set_style_border_width(top,1,0); lv_obj_set_style_radius(top,0,0); lv_obj_set_style_pad_all(top,0,0);
    instrument_label(top,"AUDIO CORE",14,11,&ac_font_16,COLOR_TEXT);
    instrument_label(top,"DIGITAL MIXER   /   48 kHz   /   24-bit   /   ENGINE MOCK",154,14,&ac_font_12,COLOR_TEXT2);
    channel_status=instrument_label(top,"SELECTED  CH 01  /  VOCAL",1250,14,&ac_font_12,COLOR_CYAN);
    instrument_label(top,"READY    CPU 12%    XRUN 0",1650,14,&ac_font_12,COLOR_GREEN);
    lv_timer_create(status_event,250,nullptr);

    auto* bottom=lv_obj_create(lv_layer_top()); lv_obj_set_size(bottom,1920,60); lv_obj_align(bottom,LV_ALIGN_BOTTOM_MID,0,0);
    lv_obj_remove_flag(bottom,LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_style_bg_color(bottom,COLOR_PANEL,0);
    lv_obj_set_style_bg_opa(bottom,LV_OPA_COVER,0);
    lv_obj_set_style_border_color(bottom,COLOR_EDGE,0); lv_obj_set_style_border_width(bottom,1,0); lv_obj_set_style_radius(bottom,0,0); lv_obj_set_style_pad_all(bottom,0,0);
    const int gap=8, width=260, start=(1920-(7*width+6*gap))/2;
    for(int i=0;i<7;++i) {
        nav_buttons[i]=lv_button_create(bottom); lv_obj_set_size(nav_buttons[i],width,48); lv_obj_set_pos(nav_buttons[i],start+i*(width+gap),6);
        lv_obj_set_style_radius(nav_buttons[i],1,0); lv_obj_set_style_bg_color(nav_buttons[i],COLOR_PANEL,0);
        lv_obj_set_style_border_color(nav_buttons[i],COLOR_EDGE,0); lv_obj_set_style_border_width(nav_buttons[i],1,0);
        auto* label=lv_label_create(nav_buttons[i]); lv_label_set_text(label,tabs[i]); lv_obj_center(label);
        lv_obj_set_style_text_font(label,&ac_font_16,0); lv_obj_set_style_text_color(label,COLOR_TEXT2,0);
        lv_obj_add_event_cb(nav_buttons[i],nav_event,LV_EVENT_CLICKED,(void*)(intptr_t)i);
    }
    paint_nav();
}
