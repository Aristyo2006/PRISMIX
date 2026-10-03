#pragma once
#include "lvgl.h"

lv_obj_t* instrument_panel(lv_obj_t* parent, int x, int y, int w, int h, const char* title);
lv_obj_t* instrument_label(lv_obj_t* parent, const char* text, int x, int y, const lv_font_t* font = nullptr, lv_color_t color = lv_color_white());
lv_obj_t* instrument_button(lv_obj_t* parent, const char* text, int x, int y, int w, int h, bool* value = nullptr);
lv_obj_t* instrument_knob(lv_obj_t* parent, const char* name, float* value, float minimum, float maximum, float initial, const char* unit, int x, int y, int size = 78);
lv_obj_t* instrument_meter(lv_obj_t* parent, float* peak, bool* clip, int x, int y, int w, int h, float* hold = nullptr, float* rms = nullptr);
lv_obj_t* instrument_chart(lv_obj_t* parent, int x, int y, int w, int h, int count = 61);
void instrument_chart_series(lv_obj_t* chart, const float* samples, int count, int min_y, int max_y, lv_color_t color);
void instrument_chart_reference(lv_obj_t* chart, const float* samples, int count, lv_color_t color);
void instrument_chart_dynamics_markers(lv_obj_t* chart, float* threshold, float* knee, float* input, float* output);
void instrument_style_chart(lv_obj_t* chart, int min_y, int max_y);
