#include "routing_screen.h"
#include "../theme/theme.h"
#include "../widgets/instrument.h"
#include "../../model/mixer_state.h"
#include <array>
#include <cstdio>

namespace {
struct RouteSelector { int* value; const char* prefix; };
void cycle_route(lv_event_t* e) {
    auto* r=static_cast<RouteSelector*>(lv_event_get_user_data(e)); *r->value=(*r->value+1)%8;
    lv_label_set_text_fmt(lv_obj_get_child(static_cast<lv_obj_t*>(lv_event_get_target(e)),0),"%s %02d",r->prefix,*r->value+1);
}
}

lv_obj_t* routing_screen_create(void) {
    auto* s=lv_obj_create(lv_screen_active()); lv_obj_set_pos(s,0,0); lv_obj_set_size(s,1920,1080); lv_obj_remove_flag(s,LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_style_bg_color(s,COLOR_BG,0);
    static int input=0, channel=0, bus=0, output=0; static bool matrix[8][8]{};
    auto* simple=instrument_panel(s,16,56,528,850,"SIMPLE ROUTING / SOURCE TO DESTINATION");
    auto add_selector=[&](const char* prefix,int* value,int y) {
        auto* btn=instrument_button(simple,"",28,y,456,58,nullptr);
        lv_label_set_text_fmt(lv_obj_get_child(btn,0),"%s %02d",prefix,*value+1);
        auto* binding=new RouteSelector{value,prefix}; lv_obj_add_event_cb(btn,cycle_route,LV_EVENT_CLICKED,binding);
    };
    instrument_label(simple,"SELECT A ROUTE STAGE; TAP TO CYCLE",28,56,&ac_font_12,COLOR_TEXT2);
    add_selector("INPUT",&input,104); add_selector("CHANNEL",&channel,190); add_selector("BUS",&bus,276); add_selector("OUTPUT",&output,362);
    instrument_label(simple,"SIGNAL PATH",28,474,&ac_font_14,COLOR_TEXT2);
    instrument_label(simple,"INPUT  →  CHANNEL  →  BUS  →  OUTPUT",28,510,&ac_font_16,COLOR_CYAN);
    instrument_button(simple,"PATCH ROUTE",28,604,456,58,nullptr);
    instrument_label(simple,"ROUTING IS A MOCK STATE; NO DEVICE IS CONNECTED",28,710,&ac_font_12,COLOR_TEXT2);
    auto* matrix_panel=instrument_panel(s,562,56,1330,850,"ADVANCED PATCH MATRIX / CHANNEL → BUS");
    instrument_label(matrix_panel,"SOURCE",20,51,&ac_font_12,COLOR_TEXT2);
    for(int col=0;col<8;++col) instrument_label(matrix_panel,"BUS",160+col*142,51,&ac_font_12,COLOR_TEXT2);
    for(int row=0;row<8;++row) {
        char name[20]; std::snprintf(name,sizeof(name),"CH %02d  %s",row+1,g_mixer_state.channels[row].name);
        instrument_label(matrix_panel,name,20,101+row*78,&ac_font_14,COLOR_TEXT);
        for(int col=0;col<8;++col) {
            if(row==col) matrix[row][col]=true;
            auto* b=instrument_button(matrix_panel,"",150+col*142,88+row*78,94,46,&matrix[row][col]);
            lv_label_set_text_fmt(lv_obj_get_child(b,0),"%02d",col+1);
        }
    }
    instrument_label(matrix_panel,"ACTIVE CROSSPOINTS ARE SHOWN IN AMBER   /   TAP TO ADD OR REMOVE",20,755,&ac_font_12,COLOR_TEXT2);
    return s;
}
