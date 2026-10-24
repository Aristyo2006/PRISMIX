#pragma once
#include "lvgl.h"
#include "../../dsp/analyzer_model.h"
#include "../../dsp/frequency_axis.h"

lv_obj_t* instrument_panel(lv_obj_t* parent, int x, int y, int w, int h, const char* title);
lv_obj_t* instrument_label(lv_obj_t* parent, const char* text, int x, int y, const lv_font_t* font = nullptr, lv_color_t color = lv_color_white());
lv_obj_t* instrument_button(lv_obj_t* parent, const char* text, int x, int y, int w, int h, bool* value = nullptr);
lv_obj_t* instrument_knob(lv_obj_t* parent, const char* name, float* value, float minimum, float maximum, float initial, const char* unit, int x, int y, int size = 78);
void instrument_knob_refresh(lv_obj_t* knob_group);
void instrument_knob_set_value(lv_obj_t* knob_group, float val);
lv_obj_t* instrument_meter(lv_obj_t* parent, float* peak, bool* clip, int x, int y, int w, int h, float* hold = nullptr, float* rms = nullptr);
lv_obj_t* instrument_gr_meter(lv_obj_t* parent, float* gr_value, int x, int y, int w, int h, const bool* bypass = nullptr, bool invert_bypass = false);
lv_obj_t* instrument_master_meter(lv_obj_t* parent, float* peak_l, float* peak_r, bool* clip, int x, int y, int w, int h, float* hold_l = nullptr, float* hold_r = nullptr, float* rms_l = nullptr, float* rms_r = nullptr);
lv_obj_t* instrument_limiter_gr_meter(lv_obj_t* parent, float* gr_db, const bool* enabled, int x, int y, int w, int h);

lv_obj_t* instrument_chart(lv_obj_t* parent, int x, int y, int w, int h, int count = 61);
void instrument_chart_series(lv_obj_t* chart, const float* samples, int count, int min_y, int max_y, lv_color_t color);
void instrument_chart_reference(lv_obj_t* chart, const float* samples, int count, lv_color_t color);
void instrument_chart_dynamics_markers(lv_obj_t* chart, float* threshold, float* knee, float* input, float* output);
void instrument_chart_draggable_threshold(lv_obj_t* chart, float* threshold, float min_db, float max_db);
void instrument_chart_set_x_axis(lv_obj_t* chart, float min_x, float max_x);
void instrument_chart_limiter_ceiling(lv_obj_t* chart, float* ceiling, bool* enabled = nullptr);
void instrument_chart_rta_spectrum(lv_obj_t* chart, const RtaSnapshot& snapshot);
void instrument_chart_get_plot_area(lv_obj_t* chart, lv_area_t* out_area);
void instrument_chart_set_frequency_axis(lv_obj_t* chart, bool enable, float min_freq = 20.0f, float max_freq = 20000.0f);
void instrument_style_chart(lv_obj_t* chart, int min_y, int max_y);
