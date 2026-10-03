#include "instrument.h"
#include "../theme/theme.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
struct KnobBinding { float* value; float lo, hi, initial; const char* unit; lv_obj_t* readout; lv_obj_t* arc; lv_obj_t* face; };
struct ButtonBinding { bool* value; lv_obj_t* label; const char* caption; };
struct MeterBinding { float* peak; bool* clip; lv_obj_t* object; float* hold; float* rms; int lit=-1, held=-1, rms_lit=-1; bool clipped=false; };
struct ChartBinding { int count; int min_y; int max_y; int reference_count=0; lv_color_t color; lv_color_t reference_color; float values[64]{}; float reference[64]{}; float* threshold=nullptr; float* knee=nullptr; float* input=nullptr; float* output=nullptr; };

void paint_button(lv_obj_t* obj, ButtonBinding* b) {
    const bool on = b->value && *b->value;
    lv_obj_set_style_bg_color(obj, on ? lv_color_hex(0x63400B) : COLOR_CONTROL, 0);
    lv_obj_set_style_border_color(obj, on ? COLOR_ORANGE : COLOR_EDGE, 0);
    lv_obj_set_style_text_color(b->label, on ? COLOR_YELLOW : COLOR_TEXT2, 0);
    lv_label_set_text_fmt(b->label, "%s%s", b->caption, on ? "  ON" : "");
}
void button_event(lv_event_t* e) {
    auto* b = static_cast<ButtonBinding*>(lv_event_get_user_data(e));
    auto* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (!b || !b->value) return;
    *b->value = !*b->value;
    paint_button(obj, b);
}
void knob_event(lv_event_t* e) {
    auto* b = static_cast<KnobBinding*>(lv_event_get_user_data(e));
    if (!b) return;
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
        auto* arc = static_cast<lv_obj_t*>(lv_event_get_target(e));
        *b->value = b->lo + (b->hi - b->lo) * (lv_arc_get_value(arc) / 100.0f);
        lv_label_set_text_fmt(b->readout, "%.1f %s", *b->value, b->unit); lv_obj_invalidate(b->face);
    } else if (lv_event_get_code(e) == LV_EVENT_DOUBLE_CLICKED) {
        *b->value = b->initial;
        lv_arc_set_value(static_cast<lv_obj_t*>(lv_event_get_target(e)), static_cast<int>((b->initial - b->lo) * 100.0f / (b->hi - b->lo))); lv_obj_invalidate(b->face);
    }
}
}

lv_obj_t* instrument_panel(lv_obj_t* parent, int x, int y, int w, int h, const char* title) {
    auto* panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y); lv_obj_set_size(panel, w, h);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(panel, COLOR_PANEL, 0);
    lv_obj_set_style_border_color(panel, COLOR_EDGE, 0);
    lv_obj_set_style_border_width(panel, 1, 0); lv_obj_set_style_radius(panel, 1, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    auto* title_obj = lv_label_create(panel);
    lv_label_set_text(title_obj, title); lv_obj_set_pos(title_obj, 12, 8);
    lv_obj_set_style_text_font(title_obj, &ac_font_14, 0);
    lv_obj_set_style_text_color(title_obj, COLOR_TEXT2, 0);
    return panel;
}

lv_obj_t* instrument_label(lv_obj_t* parent, const char* text, int x, int y, const lv_font_t* font, lv_color_t color) {
    auto* label = lv_label_create(parent); lv_label_set_text(label, text); lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_font(label, font ? font : &ac_font_14, 0);
    lv_obj_set_style_text_color(label, color, 0); return label;
}

lv_obj_t* instrument_button(lv_obj_t* parent, const char* text, int x, int y, int w, int h, bool* value) {
    auto* btn = lv_button_create(parent); lv_obj_set_pos(btn, x, y); lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, 2, 0); lv_obj_set_style_bg_color(btn, COLOR_CONTROL, 0);
    lv_obj_set_style_border_color(btn, COLOR_EDGE, 0); lv_obj_set_style_border_width(btn, 1, 0);
    auto* label = lv_label_create(btn); lv_label_set_text(label, text); lv_obj_center(label);
    lv_obj_set_style_text_font(label, &ac_font_14, 0); lv_obj_set_style_text_color(label, COLOR_TEXT2, 0);
    if (value) {
        auto* binding = new ButtonBinding{value, label, text};
        paint_button(btn, binding);
        lv_obj_add_event_cb(btn, button_event, LV_EVENT_CLICKED, binding);
    }
    return btn;
}

lv_obj_t* instrument_knob(lv_obj_t* parent, const char* name, float* value, float minimum, float maximum, float initial, const char* unit, int x, int y, int size) {
    auto* group = lv_obj_create(parent); lv_obj_set_pos(group, x, y); lv_obj_set_size(group, size + 10, size + 54);
    lv_obj_remove_flag(group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(group, LV_OPA_TRANSP, 0); lv_obj_set_style_border_width(group, 0, 0); lv_obj_set_style_pad_all(group, 0, 0);
    auto* name_label = lv_label_create(group); lv_label_set_text(name_label, name); lv_obj_align(name_label, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_text_font(name_label, &ac_font_12, 0); lv_obj_set_style_text_color(name_label, COLOR_TEXT2, 0);
    auto* arc = lv_arc_create(group); lv_obj_set_size(arc, size, size); lv_obj_align(arc, LV_ALIGN_TOP_MID, 0, 18);
    lv_arc_set_rotation(arc, 135); lv_arc_set_bg_angles(arc, 0, 270); lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, static_cast<int>(((*value - minimum) / (maximum - minimum)) * 100.0f));
    lv_obj_set_style_arc_color(arc, COLOR_RECESSED, LV_PART_MAIN); lv_obj_set_style_arc_width(arc, 7, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, COLOR_CYAN, LV_PART_INDICATOR); lv_obj_set_style_arc_width(arc, 7, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB); lv_obj_set_style_border_width(arc, 0, LV_PART_KNOB);
    auto* readout = lv_label_create(group); lv_label_set_text_fmt(readout, "%.1f %s", *value, unit); lv_obj_align(readout, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(readout, &ac_font_14, 0); lv_obj_set_style_text_color(readout, COLOR_TEXT, 0);
    auto* face=lv_obj_create(group); lv_obj_set_size(face,size-16,size-16); lv_obj_center(face);
    lv_obj_set_pos(face,5,26); lv_obj_remove_flag(face,LV_OBJ_FLAG_SCROLLABLE); lv_obj_remove_flag(face,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(face,LV_OPA_TRANSP,0); lv_obj_set_style_border_width(face,0,0); lv_obj_set_style_pad_all(face,0,0);
    auto* binding = new KnobBinding{value, minimum, maximum, initial, unit, readout, arc, face};
    lv_obj_set_user_data(face,binding);
    lv_obj_add_event_cb(face,[](lv_event_t* e) {
        if(lv_event_get_code(e)!=LV_EVENT_DRAW_MAIN) return;
        auto* b=static_cast<KnobBinding*>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e))));
        lv_area_t a{}; lv_obj_get_coords(static_cast<lv_obj_t*>(lv_event_get_target(e)),&a);
        const int cx=(a.x1+a.x2)/2, cy=(a.y1+a.y2)/2, r=lv_area_get_width(&a)/2-2;
        auto* layer=lv_event_get_layer(e); lv_draw_line_dsc_t tick; lv_draw_line_dsc_init(&tick); tick.color=COLOR_HIGHLIGHT;
        const float val=lv_arc_get_value(b->arc)/100.0f;
        for(int i=0;i<11;++i) {
            const float angle=(135.0f+i*27.0f)*3.14159265f/180.0f;
            tick.width=(i==0||i==5||i==10)?2:1;
            tick.p1.x=cx+(int)(std::cos(angle)*(r-1)); tick.p1.y=cy+(int)(std::sin(angle)*(r-1));
            tick.p2.x=cx+(int)(std::cos(angle)*(r-5)); tick.p2.y=cy+(int)(std::sin(angle)*(r-5)); lv_draw_line(layer,&tick);
        }
        lv_draw_rect_dsc_t body; lv_draw_rect_dsc_init(&body); body.bg_color=COLOR_CONTROL; body.bg_opa=LV_OPA_COVER; body.border_color=COLOR_EDGE; body.border_width=2; body.radius=LV_RADIUS_CIRCLE;
        lv_area_t disc{cx-r+6,cy-r+6,cx+r-6,cy+r-6}; lv_draw_rect(layer,&body,&disc);
        const float angle=(135.0f+val*270.0f)*3.14159265f/180.0f; lv_draw_line_dsc_t pointer; lv_draw_line_dsc_init(&pointer); pointer.color=COLOR_CYAN; pointer.width=3;
        pointer.p1.x=cx; pointer.p1.y=cy; pointer.p2.x=cx+(int)(std::cos(angle)*(r-11)); pointer.p2.y=cy+(int)(std::sin(angle)*(r-11)); lv_draw_line(layer,&pointer);
    },LV_EVENT_DRAW_MAIN,nullptr);
    lv_obj_add_event_cb(arc, knob_event, LV_EVENT_VALUE_CHANGED, binding);
    lv_obj_add_event_cb(arc, knob_event, LV_EVENT_DOUBLE_CLICKED, binding);
    return group;
}

lv_obj_t* instrument_meter(lv_obj_t* parent, float* peak, bool* clip, int x, int y, int w, int h, float* hold, float* rms) {
    auto* meter = lv_obj_create(parent); lv_obj_set_pos(meter, x, y); lv_obj_set_size(meter, w, h);
    lv_obj_remove_flag(meter, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(meter, COLOR_RECESSED, 0); lv_obj_set_style_border_color(meter, COLOR_EDGE, 0);
    lv_obj_set_style_border_width(meter, 1, 0); lv_obj_set_style_radius(meter, 1, 0); lv_obj_set_style_pad_all(meter, 2, 0);
    auto* binding = new MeterBinding{peak, clip, meter, hold, rms};
    lv_obj_set_user_data(meter, binding);
    lv_obj_add_event_cb(meter, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;
        auto* b = static_cast<MeterBinding*>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e))));
        lv_area_t a{}; lv_obj_get_coords(static_cast<lv_obj_t*>(lv_event_get_target(e)), &a);
        const int segments = 24, inner_h = lv_area_get_height(&a)-6, step=std::max(2,inner_h/segments), led_h=std::max(2,step-1);
        const int left=a.x1+3, right=a.x2-3;
        for(int i=0;i<segments;++i) {
            const bool active=i<b->lit, rms_active=i<b->rms_lit, held=i==b->held;
            const lv_color_t color=b->clipped&&*b->clip?COLOR_RED:(i>=22?COLOR_RED:(i>=19?COLOR_YELLOW:COLOR_GREEN));
            lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d); d.bg_opa=LV_OPA_COVER;
            d.bg_color=held?COLOR_TEXT:(active?color:(rms_active?lv_color_hex(0x12613F):COLOR_CONTROL));
            lv_area_t led{left,a.y2-3-(i+1)*step,right,a.y2-3-(i+1)*step+led_h};
            lv_draw_rect(lv_event_get_layer(e),&d,&led);
        }
    },LV_EVENT_DRAW_MAIN,nullptr);
    lv_timer_create([](lv_timer_t* timer) {
        auto* b = static_cast<MeterBinding*>(lv_timer_get_user_data(timer));
        if (!b || !b->peak) return;
        auto to_segments=[](float level){return std::clamp((int)((level+60.0f)*24.0f/63.0f),0,24);};
        const int lit=to_segments(*b->peak), held=b->hold?to_segments(*b->hold):-1, rms_lit=b->rms?to_segments(*b->rms):-1;
        const bool clipped=b->clip&&*b->clip;
        if(lit!=b->lit||held!=b->held||rms_lit!=b->rms_lit||clipped!=b->clipped) { b->lit=lit; b->held=held; b->rms_lit=rms_lit; b->clipped=clipped; lv_obj_invalidate(b->object); }
    }, 60, binding);
    return meter;
}

lv_obj_t* instrument_chart(lv_obj_t* parent, int x, int y, int w, int h, int count) {
    auto* chart = lv_obj_create(parent); lv_obj_set_pos(chart, x, y); lv_obj_set_size(chart, w, h);
    lv_obj_set_style_bg_color(chart, COLOR_RECESSED, 0); lv_obj_set_style_border_color(chart, COLOR_EDGE, 0);
    lv_obj_set_style_border_width(chart, 1, 0); lv_obj_set_style_radius(chart, 0, 0);
    lv_obj_add_flag(chart, LV_OBJ_FLAG_CLICKABLE);
    auto* b=new ChartBinding{}; b->count=std::clamp(count,2,64); b->min_y=-1; b->max_y=1; b->color=COLOR_CYAN; b->reference_color=COLOR_TEXT2;
    lv_obj_set_user_data(chart,b);
    lv_obj_add_event_cb(chart,[](lv_event_t* e) {
        if(lv_event_get_code(e)!=LV_EVENT_DRAW_MAIN) return;
        auto* obj=static_cast<lv_obj_t*>(lv_event_get_target(e)); auto* b=static_cast<ChartBinding*>(lv_obj_get_user_data(obj));
        lv_area_t a{}; lv_obj_get_coords(obj,&a); a.x1+=10; a.x2-=8; a.y1+=8; a.y2-=10;
        auto* layer=lv_event_get_layer(e); lv_draw_line_dsc_t line; lv_draw_line_dsc_init(&line); line.width=1; line.color=lv_color_hex(0x282D31);
        for(int i=0;i<=10;++i) { int x=a.x1+(lv_area_get_width(&a)-1)*i/10; line.p1.x=x; line.p1.y=a.y1; line.p2.x=x; line.p2.y=a.y2; lv_draw_line(layer,&line); }
        for(int i=0;i<=8;++i) { int y=a.y1+(lv_area_get_height(&a)-1)*i/8; line.p1.x=a.x1; line.p1.y=y; line.p2.x=a.x2; line.p2.y=y; lv_draw_line(layer,&line); }
        auto draw_values=[&](const float* values,int count,lv_color_t color,int width) {
            if(count<2) return; lv_draw_line_dsc_t curve; lv_draw_line_dsc_init(&curve); curve.color=color; curve.width=width;
            auto point=[&](int i) { int x=a.x1+(lv_area_get_width(&a)-1)*i/(count-1); float f=std::clamp((values[i]-b->min_y)/(float)(b->max_y-b->min_y),0.0f,1.0f); int y=a.y2-(int)(f*(lv_area_get_height(&a)-1)); return lv_point_t{x,y}; };
            for(int i=1;i<count;++i) { const auto p1=point(i-1),p2=point(i); curve.p1.x=p1.x; curve.p1.y=p1.y; curve.p2.x=p2.x; curve.p2.y=p2.y; lv_draw_line(layer,&curve); }
        };
        if(b->reference_count>1) draw_values(b->reference,b->reference_count,b->reference_color,1);
        draw_values(b->values,b->count,b->color,2);
        if(b->threshold&&b->knee&&b->input&&b->output) {
            auto x_coord=[&](float v){return a.x1+(int)(std::clamp((v-b->min_y)/(float)(b->max_y-b->min_y),0.0f,1.0f)*(lv_area_get_width(&a)-1));};
            lv_draw_line_dsc_t marker; lv_draw_line_dsc_init(&marker); marker.color=COLOR_ORANGE; marker.width=2;
            const int tx=x_coord(*b->threshold); marker.p1.x=tx; marker.p1.y=a.y1; marker.p2.x=tx; marker.p2.y=a.y2; lv_draw_line(layer,&marker);
            marker.color=lv_color_hex(0x66502C); marker.width=1;
            for(float edge : {*b->threshold-*b->knee*.5f,*b->threshold+*b->knee*.5f}) { int x=x_coord(edge); marker.p1.x=x; marker.p1.y=a.y1; marker.p2.x=x; marker.p2.y=a.y2; lv_draw_line(layer,&marker); }
            const int px=x_coord(*b->input); const float yf=std::clamp((*b->output-b->min_y)/(float)(b->max_y-b->min_y),0.0f,1.0f); const int py=a.y2-(int)(yf*(lv_area_get_height(&a)-1));
            lv_draw_rect_dsc_t dot; lv_draw_rect_dsc_init(&dot); dot.bg_color=COLOR_YELLOW; dot.bg_opa=LV_OPA_COVER; dot.border_color=COLOR_TEXT; dot.border_width=1; dot.radius=LV_RADIUS_CIRCLE;
            lv_area_t point{px-5,py-5,px+5,py+5}; lv_draw_rect(layer,&dot,&point);
        }
    },LV_EVENT_DRAW_MAIN,nullptr);
    return chart;
}
void instrument_style_chart(lv_obj_t* chart, int min_y, int max_y) {
    auto* b=static_cast<ChartBinding*>(lv_obj_get_user_data(chart)); b->min_y=min_y; b->max_y=max_y;
}
void instrument_chart_series(lv_obj_t* chart, const float* samples, int count, int min_y, int max_y, lv_color_t color) {
    auto* b=static_cast<ChartBinding*>(lv_obj_get_user_data(chart)); b->count=std::clamp(count,2,64); b->min_y=min_y; b->max_y=max_y; b->color=color;
    std::copy(samples,samples+b->count,b->values); lv_obj_invalidate(chart);
}
void instrument_chart_reference(lv_obj_t* chart,const float* samples,int count,lv_color_t color) {
    auto* b=static_cast<ChartBinding*>(lv_obj_get_user_data(chart)); b->reference_count=std::clamp(count,2,64); b->reference_color=color;
    std::copy(samples,samples+b->reference_count,b->reference); lv_obj_invalidate(chart);
}
void instrument_chart_dynamics_markers(lv_obj_t* chart,float* threshold,float* knee,float* input,float* output) {
    auto* b=static_cast<ChartBinding*>(lv_obj_get_user_data(chart)); b->threshold=threshold; b->knee=knee; b->input=input; b->output=output; lv_obj_invalidate(chart);
}
