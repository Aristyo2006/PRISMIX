#include "instrument.h"
#include "../theme/theme.h"
#include "../assets/rack_textures.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
struct KnobBinding {
    float* value;
    float lo, hi, initial;
    const char* name;
    const char* unit;
    lv_obj_t* readout;
    lv_obj_t* face;
    lv_obj_t* group;
    int size;
    bool fine_mode = false;
    bool is_pressed = false;
};

static float knob_val_to_norm(const KnobBinding* b, float val) {
    if (b->hi <= b->lo) return 0.5f;
    val = std::clamp(val, b->lo, b->hi);
    bool is_hz = (b->unit && std::strcmp(b->unit, "Hz") == 0);
    bool is_q = (b->name && std::strcmp(b->name, "Q") == 0);
    bool is_ratio = (b->unit && std::strcmp(b->unit, ":1") == 0);
    if (is_hz && b->lo > 0.0f) {
        return std::clamp(std::log(val / b->lo) / std::log(b->hi / b->lo), 0.0f, 1.0f);
    }
    if (is_q && b->lo > 0.0f) {
        return std::clamp(std::log(val / b->lo) / std::log(b->hi / b->lo), 0.0f, 1.0f);
    }
    if (is_ratio && b->lo > 0.0f) {
        return std::clamp(std::log(val / b->lo) / std::log(b->hi / b->lo), 0.0f, 1.0f);
    }
    return (val - b->lo) / (b->hi - b->lo);
}

static float knob_norm_to_val(const KnobBinding* b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    bool is_hz = (b->unit && std::strcmp(b->unit, "Hz") == 0);
    bool is_q = (b->name && std::strcmp(b->name, "Q") == 0);
    bool is_ratio = (b->unit && std::strcmp(b->unit, ":1") == 0);
    if (is_hz && b->lo > 0.0f) {
        return b->lo * std::pow(b->hi / b->lo, t);
    }
    if (is_q && b->lo > 0.0f) {
        return b->lo * std::pow(b->hi / b->lo, t);
    }
    if (is_ratio && b->lo > 0.0f) {
        return b->lo * std::pow(b->hi / b->lo, t);
    }
    return b->lo + t * (b->hi - b->lo);
}

struct ButtonBinding {
    bool* value;
    lv_obj_t* label;
    const char* caption;
};

struct MeterBinding {
    float* peak;
    bool* clip;
    lv_obj_t* object;
    float* hold;
    float* rms;
    int lit = -1, held = -1, rms_lit = -1;
    bool clipped = false;
    lv_timer_t* timer = nullptr;
};

struct GrMeterBinding {
    float* gr_value;
    const bool* bypass_flag = nullptr;
    bool invert_bypass = false;
    lv_obj_t* object = nullptr;
    int lit = 0;
    int held = 0;
    lv_timer_t* timer = nullptr;
};

struct MasterMeterBinding {
    float* peak_l = nullptr;
    float* peak_r = nullptr;
    bool* clip = nullptr;
    float* hold_l = nullptr;
    float* hold_r = nullptr;
    float* rms_l = nullptr;
    float* rms_r = nullptr;
    lv_obj_t* object = nullptr;
    int lit_l = -1, lit_r = -1;
    int held_l = -1, held_r = -1;
    int rms_l_lit = -1, rms_r_lit = -1;
    bool clipped = false;
    lv_timer_t* timer = nullptr;
};

struct ChartBinding {
    int count = 61;
    int min_y = -18;
    int max_y = 18;
    int reference_count = 0;
    lv_color_t color = lv_color_hex(0xE2E8F0);
    lv_color_t reference_color = COLOR_TEXT2;
    float values[128]{};
    float reference[128]{};

    // Dynamics markers
    float* threshold = nullptr;
    float* knee = nullptr;
    float* input = nullptr;
    float* output = nullptr;

    // Draggable threshold
    float* draggable_thresh = nullptr;
    float thresh_min_db = -60.0f;
    float thresh_max_db = 0.0f;
    float min_x = -60.0f;
    float max_x = 0.0f;
    bool is_dragging = false;

    // Limiter ceiling marker
    float* limiter_ceiling = nullptr;
    bool* limiter_enabled = nullptr;

    // RTA snapshot
    bool has_rta = false;
    RtaSnapshot rta{};

    // Frequency axis configuration
    bool is_frequency_axis = false;
    float min_freq = FrequencyAxis::MIN_FREQ;
    float max_freq = FrequencyAxis::MAX_FREQ;
};

void format_knob_value(char* buf, size_t sz, const char* name, const char* unit, float val, float lo, float hi, bool fine = false) {
    if (std::strcmp(name, "PAN") == 0 || (unit && std::strcmp(unit, "L/C/R") == 0)) {
        float norm = (hi > lo) ? ((val - lo) / (hi - lo)) : 0.5f;
        if (norm >= 0.485f && norm <= 0.515f) {
            std::snprintf(buf, sz, "C");
        } else if (norm < 0.485f) {
            int pct = (int)std::round((0.5f - norm) * 200.0f);
            std::snprintf(buf, sz, "L %d", pct);
        } else {
            int pct = (int)std::round((norm - 0.5f) * 200.0f);
            std::snprintf(buf, sz, "R %d", pct);
        }
    } else if (std::strcmp(name, "GAIN") == 0 && hi == 1.0f && lo == 0.0f) {
        float db = (val - 0.5f) * 24.0f; // -12 to +12 dB
        if (std::abs(db) < 0.05f) std::snprintf(buf, sz, "0.0 dB");
        else std::snprintf(buf, sz, "%+.1f dB", db);
    } else if (unit && std::strcmp(unit, "dB") == 0) {
        if (std::abs(val) < 0.05f) std::snprintf(buf, sz, "0.0 dB");
        else std::snprintf(buf, sz, "%+.1f dB", val);
    } else if (unit && std::strcmp(unit, "Hz") == 0) {
        if (val >= 1000.0f) {
            if (fine) std::snprintf(buf, sz, "%.2f kHz", val / 1000.0f);
            else std::snprintf(buf, sz, "%.1f kHz", val / 1000.0f);
        } else {
            std::snprintf(buf, sz, "%d Hz", (int)std::round(val));
        }
    } else if (unit && std::strcmp(unit, "%") == 0) {
        std::snprintf(buf, sz, "%d %%", (int)std::round(val * 100.0f));
    } else if (unit && std::strcmp(unit, ":1") == 0) {
        std::snprintf(buf, sz, "%.1f:1", val);
    } else if (unit && std::strcmp(unit, "ms") == 0) {
        if (val >= 100.0f) std::snprintf(buf, sz, "%d ms", (int)std::round(val));
        else std::snprintf(buf, sz, "%.1f ms", val);
    } else if (unit && std::strcmp(unit, "s") == 0) {
        std::snprintf(buf, sz, "%.2f s", val);
    } else {
        std::snprintf(buf, sz, "%.2f %s", val, unit ? unit : "");
    }
}

void draw_sub_arc(lv_layer_t* layer, int cx, int cy, int r, int w, float start, float end, lv_color_t color) {
    if (end <= start) return;
    auto draw_single = [&](float s, float e) {
        if (e <= s) return;
        lv_draw_arc_dsc_t arc;
        lv_draw_arc_dsc_init(&arc);
        arc.center.x = cx;
        arc.center.y = cy;
        arc.radius = r;
        arc.width = w;
        arc.color = color;
        arc.start_angle = (lv_value_precise_t)s;
        arc.end_angle = (lv_value_precise_t)e;
        lv_draw_arc(layer, &arc);
    };
    if (end <= 360.0f) {
        draw_single(start, end);
    } else if (start >= 360.0f) {
        draw_single(start - 360.0f, end - 360.0f);
    } else {
        draw_single(start, 360.0f);
        draw_single(0.0f, end - 360.0f);
    }
}

void paint_button(lv_obj_t* obj, ButtonBinding* b) {
    const bool on = b->value && *b->value;
    const char* cap = b->caption ? b->caption : "";
    if (std::strstr(cap, "MUTE")) {
        lv_obj_set_style_bg_color(obj, on ? lv_color_hex(0x5A1B1B) : COLOR_CONTROL, 0);
        lv_obj_set_style_border_color(obj, on ? COLOR_RED : COLOR_EDGE, 0);
        lv_obj_set_style_text_color(b->label, on ? lv_color_hex(0xFCA5A5) : COLOR_TEXT2, 0);
    } else if (std::strstr(cap, "SOLO")) {
        lv_obj_set_style_bg_color(obj, on ? lv_color_hex(0x523B0F) : COLOR_CONTROL, 0);
        lv_obj_set_style_border_color(obj, on ? COLOR_YELLOW : COLOR_EDGE, 0);
        lv_obj_set_style_text_color(b->label, on ? lv_color_hex(0xFDE047) : COLOR_TEXT2, 0);
    } else {
        lv_obj_set_style_bg_color(obj, on ? COLOR_RAISED_PANEL : COLOR_CONTROL, 0);
        lv_obj_set_style_border_color(obj, on ? COLOR_SELECTED_BORDER : COLOR_STANDARD_BORDER, 0);
        lv_obj_set_style_text_color(b->label, on ? COLOR_PRIMARY_TEXT : COLOR_SECONDARY_TEXT, 0);
    }
    lv_label_set_text_fmt(b->label, "%s%s", b->caption, on ? "  ON" : "");
}

void button_event(lv_event_t* e) {
    auto* b = static_cast<ButtonBinding*>(lv_event_get_user_data(e));
    auto* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (!b || !b->value) return;
    *b->value = !*b->value;
    paint_button(obj, b);
}

void knob_interaction_event(lv_event_t* e) {
    auto* b = static_cast<KnobBinding*>(lv_event_get_user_data(e));
    if (!b || !b->value) return;
    const lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        b->is_pressed = true;
        lv_obj_invalidate(b->face);
    } else if (code == LV_EVENT_RELEASED) {
        b->is_pressed = false;
        lv_obj_invalidate(b->face);
    } else if (code == LV_EVENT_PRESSING) {
        lv_indev_t* indev = lv_indev_active();
        if (indev) {
            lv_point_t vect{};
            lv_indev_get_vect(indev, &vect);
            if (vect.y != 0 || vect.x != 0) {
                float cur_norm = knob_val_to_norm(b, *b->value);
                float sensitivity = b->fine_mode ? 800.0f : 200.0f;
                // Vertical drag: up is negative y -> increases value
                float delta_norm = (-vect.y * 1.0f + vect.x * 0.25f) / sensitivity;
                float new_norm = std::clamp(cur_norm + delta_norm, 0.0f, 1.0f);
                *b->value = knob_norm_to_val(b, new_norm);
                char txt[32];
                format_knob_value(txt, sizeof(txt), b->name, b->unit, *b->value, b->lo, b->hi, b->fine_mode);
                lv_label_set_text(b->readout, txt);
                lv_obj_invalidate(b->face);
                lv_obj_send_event(b->face, LV_EVENT_VALUE_CHANGED, nullptr);
                if (b->group) lv_obj_send_event(b->group, LV_EVENT_VALUE_CHANGED, nullptr);
            }
        }
    } else if (code == LV_EVENT_DOUBLE_CLICKED) {
        *b->value = b->initial;
        char txt[32];
        format_knob_value(txt, sizeof(txt), b->name, b->unit, *b->value, b->lo, b->hi, b->fine_mode);
        lv_label_set_text(b->readout, txt);
        lv_obj_invalidate(b->face);
        lv_obj_send_event(b->face, LV_EVENT_VALUE_CHANGED, nullptr);
        if (b->group) lv_obj_send_event(b->group, LV_EVENT_VALUE_CHANGED, nullptr);
    } else if (code == LV_EVENT_LONG_PRESSED) {
        // Toggle fine adjustment mode
        b->fine_mode = !b->fine_mode;
        lv_obj_set_style_text_color(b->readout, b->fine_mode ? COLOR_NEUTRAL_HIGHLIGHT : COLOR_PRIMARY_TEXT, 0);
        char txt[32];
        format_knob_value(txt, sizeof(txt), b->name, b->unit, *b->value, b->lo, b->hi, b->fine_mode);
        lv_label_set_text(b->readout, txt);
        lv_obj_invalidate(b->face);
    }
}
}

lv_obj_t* instrument_panel(lv_obj_t* parent, int x, int y, int w, int h, const char* title) {
    auto* panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    ui::rack_skin::apply_faceplate_panel(panel, false);
    if (w >= 300 && h >= 200) {
        ui::rack_skin::add_faceplate_corner_screws(panel, 6, 6);
    }
    lv_obj_set_style_pad_all(panel, 0, 0);

    if (title && title[0] != '\0') {
        auto* title_obj = lv_label_create(panel);
        lv_label_set_text(title_obj, title);
        lv_obj_set_pos(title_obj, 16, 8);
        lv_obj_set_style_text_font(title_obj, &ac_font_14, 0);
        lv_obj_set_style_text_color(title_obj, COLOR_SILVER, 0);
    }
    return panel;
}

lv_obj_t* instrument_label(lv_obj_t* parent, const char* text, int x, int y, const lv_font_t* font, lv_color_t color) {
    auto* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_font(label, font ? font : &ac_font_14, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

lv_obj_t* instrument_button(lv_obj_t* parent, const char* text, int x, int y, int w, int h, bool* value) {
    auto* btn = lv_button_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_radius(btn, 2, 0);
    lv_obj_set_style_bg_color(btn, COLOR_CONTROL, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, COLOR_EDGE, 0);
    lv_obj_set_style_border_width(btn, 1, 0);

    auto* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_set_style_text_font(label, &ac_font_14, 0);
    lv_obj_set_style_text_color(label, COLOR_TEXT2, 0);

    if (value) {
        auto* binding = new ButtonBinding{value, label, text};
        paint_button(btn, binding);
        lv_obj_add_event_cb(btn, button_event, LV_EVENT_CLICKED, binding);
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            auto* b = static_cast<ButtonBinding*>(lv_event_get_user_data(e));
            delete b;
        }, LV_EVENT_DELETE, binding);
    }
    return btn;
}

// Knob component layout metrics:
constexpr int KNOB_LABEL_HEIGHT = 14;     // ac_font_12 line height
constexpr int KNOB_LABEL_TO_FACE = 4;     // gap between label bottom and rotary face
constexpr int KNOB_FACE_TO_VALUE = 12;    // gap between rotary face and value text (10-16 px requirement)
constexpr int KNOB_VALUE_HEIGHT = 14;     // ac_font_12 line height

lv_obj_t* instrument_knob(lv_obj_t* parent, const char* name, float* value, float minimum, float maximum, float initial, const char* unit, int x, int y, int size) {
    const int face_size = (size > 0) ? size : 64;
    const int pad_lbl = (face_size <= 56) ? 2 : KNOB_LABEL_TO_FACE;
    const int pad_val = (face_size <= 56) ? 6 : KNOB_FACE_TO_VALUE;
    const int group_w = (face_size >= 64) ? std::max(face_size + 20, 84) : 84;
    const int group_h = KNOB_LABEL_HEIGHT + pad_lbl + face_size + pad_val + KNOB_VALUE_HEIGHT;
    auto* group = lv_obj_create(parent);
    lv_obj_set_pos(group, x, y);
    lv_obj_set_size(group, group_w, group_h);
    lv_obj_remove_flag(group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(group, 0, 0);
    lv_obj_set_style_pad_all(group, 0, 0);

    auto* name_label = lv_label_create(group);
    lv_label_set_text(name_label, name);
    lv_obj_set_width(name_label, group_w);
    lv_obj_align(name_label, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_text_align(name_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(name_label, &ac_font_12, 0);
    lv_obj_set_style_text_color(name_label, COLOR_TEXT2, 0);

    auto* face = lv_obj_create(group);
    lv_obj_set_size(face, face_size, face_size);
    lv_obj_align(face, LV_ALIGN_TOP_MID, 0, KNOB_LABEL_HEIGHT + pad_lbl);
    lv_obj_add_flag(face, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(face, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_remove_flag(face, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(face, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_remove_flag(face, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_set_style_bg_opa(face, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(face, 0, 0);
    lv_obj_set_style_pad_all(face, 0, 0);

    auto* readout = lv_label_create(group);
    char init_txt[32];
    format_knob_value(init_txt, sizeof(init_txt), name, unit, value ? *value : initial, minimum, maximum);
    lv_label_set_text(readout, init_txt);
    lv_obj_set_width(readout, group_w - 6);
    lv_obj_align(readout, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_align(readout, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(readout, &lv_font_unscii_8, 0);
    lv_obj_set_style_text_color(readout, COLOR_LCD_TEXT, 0);
    // Recessed digital readout badge
    lv_obj_set_style_bg_color(readout, COLOR_LCD_BG, 0);
    lv_obj_set_style_bg_opa(readout, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(readout, 1, 0);
    lv_obj_set_style_border_color(readout, COLOR_HW_BORDER, 0);
    lv_obj_set_style_radius(readout, 2, 0);
    lv_obj_set_style_pad_ver(readout, 1, 0);

    auto* binding = new KnobBinding{value, minimum, maximum, initial, name, unit, readout, face, group, size, false, false};
    lv_obj_set_user_data(face, binding);
    lv_obj_set_user_data(group, binding);

    lv_obj_add_event_cb(group, [](lv_event_t* e) {
        auto* b = static_cast<KnobBinding*>(lv_event_get_user_data(e));
        delete b;
    }, LV_EVENT_DELETE, binding);

    lv_obj_add_event_cb(face, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;
        auto* b = static_cast<KnobBinding*>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e))));
        if (!b || !b->value) return;

        lv_layer_t* layer = lv_event_get_layer(e);
        lv_area_t a{};
        lv_obj_get_coords(static_cast<lv_obj_t*>(lv_event_get_target(e)), &a);
        const int cx = (a.x1 + a.x2) / 2;
        const int cy = (a.y1 + a.y2) / 2;
        const int r = (std::min(lv_area_get_width(&a), lv_area_get_height(&a)) / 2) - 2;

        const float norm = knob_val_to_norm(b, *b->value);
        const float cur_angle = 135.0f + norm * 270.0f;
        const bool is_pan = (std::strcmp(b->name, "PAN") == 0) || (b->unit && std::strcmp(b->unit, "L/C/R") == 0);

        // 1. Recessed rotary track groove (270 deg sweep from 135 to 405)
        const int r_arc = r - 2;
        draw_sub_arc(layer, cx, cy, r_arc, 4, 135.0f, 405.0f, lv_color_hex(0x161614));

        // 2. Illuminated active value arc (neutral grey metallic ring)
        lv_color_t arc_color = b->fine_mode ? COLOR_NEUTRAL_HIGHLIGHT : COLOR_KNOB_RING;
        if (is_pan) {
            if (norm < 0.49f) {
                draw_sub_arc(layer, cx, cy, r_arc, 3, cur_angle, 270.0f, arc_color);
            } else if (norm > 0.51f) {
                draw_sub_arc(layer, cx, cy, r_arc, 3, 270.0f, cur_angle, arc_color);
            }
        } else {
            if (cur_angle > 136.0f) {
                draw_sub_arc(layer, cx, cy, r_arc, 3, 135.0f, cur_angle, arc_color);
            }
        }

        // 3. Calibrated Silkscreen Scale Ticks around perimeter
        lv_draw_line_dsc_t tick_dsc;
        lv_draw_line_dsc_init(&tick_dsc);
        if (is_pan) {
            for (int t = 0; t <= 8; ++t) {
                const float deg = 135.0f + t * 33.75f;
                const float rad = deg * 3.14159265f / 180.0f;
                const bool is_c = (t == 4);
                const bool is_end = (t == 0 || t == 8);
                tick_dsc.color = is_c ? COLOR_KNOB_INDICATOR : (is_end ? COLOR_KNOB_RING : COLOR_STANDARD_BORDER);
                tick_dsc.width = is_c ? 2 : 1;
                const int len = is_c ? 5 : (is_end ? 4 : 3);
                const int r_out = r + 1;
                const int r_in = r_out - len;
                tick_dsc.p1.x = cx + (int)(std::cos(rad) * r_out);
                tick_dsc.p1.y = cy + (int)(std::sin(rad) * r_out);
                tick_dsc.p2.x = cx + (int)(std::cos(rad) * r_in);
                tick_dsc.p2.y = cy + (int)(std::sin(rad) * r_in);
                lv_draw_line(layer, &tick_dsc);
            }
        } else {
            for (int t = 0; t <= 12; ++t) {
                const float deg = 135.0f + t * 22.5f;
                const float rad = deg * 3.14159265f / 180.0f;
                const bool major = (t == 0 || t == 6 || t == 12);
                const bool mid = (t == 3 || t == 9);
                tick_dsc.color = major ? COLOR_KNOB_INDICATOR : (mid ? COLOR_KNOB_RING : COLOR_STANDARD_BORDER);
                tick_dsc.width = major ? 2 : 1;
                const int len = major ? 5 : (mid ? 4 : 3);
                const int r_out = r + 1;
                const int r_in = r_out - len;
                tick_dsc.p1.x = cx + (int)(std::cos(rad) * r_out);
                tick_dsc.p1.y = cy + (int)(std::sin(rad) * r_out);
                tick_dsc.p2.x = cx + (int)(std::cos(rad) * r_in);
                tick_dsc.p2.y = cy + (int)(std::sin(rad) * r_in);
                lv_draw_line(layer, &tick_dsc);
            }
        }

        // 4. Cast-metal drop shadow beneath knob skirt
        const int r_cap = r - 5;
        lv_draw_rect_dsc_t shadow_dsc;
        lv_draw_rect_dsc_init(&shadow_dsc);
        shadow_dsc.radius = LV_RADIUS_CIRCLE;
        shadow_dsc.bg_color = lv_color_hex(0x0A0A09);
        shadow_dsc.bg_opa = LV_OPA_70;
        lv_area_t shadow_a{cx - r_cap - 1, cy - r_cap + 2, cx + r_cap + 1, cy + r_cap + 3};
        lv_draw_rect(layer, &shadow_dsc, &shadow_a);

        // 5. Machined aluminum knurled skirt with neutral dark graphite tone
        lv_draw_rect_dsc_t rim;
        lv_draw_rect_dsc_init(&rim);
        rim.radius = LV_RADIUS_CIRCLE;
        rim.bg_color = b->is_pressed ? COLOR_RAISED_PANEL : COLOR_PANEL_SURFACE;
        rim.bg_grad.dir = LV_GRAD_DIR_VER;
        rim.bg_grad.stops[0].color = b->is_pressed ? lv_color_hex(0x3C3D37) : COLOR_RAISED_PANEL;
        rim.bg_grad.stops[1].color = lv_color_hex(0x1B1C1A);
        rim.bg_grad.stops[0].opa = LV_OPA_COVER;
        rim.bg_grad.stops[1].opa = LV_OPA_COVER;
        rim.bg_grad.stops_count = 2;
        rim.bg_opa = LV_OPA_COVER;
        rim.border_color = b->is_pressed ? COLOR_SELECTED_BORDER : COLOR_STANDARD_BORDER;
        rim.border_width = 1;
        lv_area_t rim_a{cx - r_cap, cy - r_cap, cx + r_cap, cy + r_cap};
        lv_draw_rect(layer, &rim, &rim_a);

        // Knurled skirt serrations (18 radial notches around outer skirt)
        lv_draw_line_dsc_t knurl_dsc;
        lv_draw_line_dsc_init(&knurl_dsc);
        knurl_dsc.color = lv_color_hex(0x141513);
        knurl_dsc.width = 1;
        for (int k = 0; k < 18; ++k) {
            const float k_rad = (k * 20.0f) * 3.14159265f / 180.0f;
            knurl_dsc.p1.x = cx + (int)(std::cos(k_rad) * (r_cap - 3));
            knurl_dsc.p1.y = cy + (int)(std::sin(k_rad) * (r_cap - 3));
            knurl_dsc.p2.x = cx + (int)(std::cos(k_rad) * (r_cap - 1));
            knurl_dsc.p2.y = cy + (int)(std::sin(k_rad) * (r_cap - 1));
            lv_draw_line(layer, &knurl_dsc);
        }

        // 6. Recessed machined collar groove
        const int r_bevel = r_cap - 3;
        lv_draw_rect_dsc_t bevel;
        lv_draw_rect_dsc_init(&bevel);
        bevel.radius = LV_RADIUS_CIRCLE;
        bevel.bg_color = lv_color_hex(0x171816);
        bevel.bg_opa = LV_OPA_COVER;
        bevel.border_color = COLOR_RECESSED;
        bevel.border_width = 1;
        lv_area_t bevel_a{cx - r_bevel, cy - r_bevel, cx + r_bevel, cy + r_bevel};
        lv_draw_rect(layer, &bevel, &bevel_a);

        // 7. Brushed dark graphite top cap with directional studio sheen
        const int r_face = r_bevel - 2;
        lv_draw_rect_dsc_t cap_face;
        lv_draw_rect_dsc_init(&cap_face);
        cap_face.radius = LV_RADIUS_CIRCLE;
        cap_face.bg_color = COLOR_CONTROL;
        cap_face.bg_grad.dir = LV_GRAD_DIR_VER;
        cap_face.bg_grad.stops[0].color = lv_color_hex(0x383934);
        cap_face.bg_grad.stops[1].color = lv_color_hex(0x1E1F1D);
        cap_face.bg_grad.stops[0].opa = LV_OPA_COVER;
        cap_face.bg_grad.stops[1].opa = LV_OPA_COVER;
        cap_face.bg_grad.stops_count = 2;
        cap_face.bg_opa = LV_OPA_COVER;
        cap_face.border_color = b->fine_mode ? COLOR_NEUTRAL_HIGHLIGHT : COLOR_STANDARD_BORDER;
        cap_face.border_width = 1;
        lv_area_t face_a{cx - r_face, cy - r_face, cx + r_face, cy + r_face};
        lv_draw_rect(layer, &cap_face, &face_a);

        // Subtle inner machined lathe ring on cap face
        if (r_face > 9) {
            const int r_inner = r_face - 5;
            lv_draw_rect_dsc_t inner_ring;
            lv_draw_rect_dsc_init(&inner_ring);
            inner_ring.radius = LV_RADIUS_CIRCLE;
            inner_ring.bg_opa = LV_OPA_TRANSP;
            inner_ring.border_color = COLOR_STANDARD_BORDER;
            inner_ring.border_width = 1;
            lv_area_t in_a{cx - r_inner, cy - r_inner, cx + r_inner, cy + r_inner};
            lv_draw_rect(layer, &inner_ring, &in_a);
        }

        // 8. Engraved pointer channel + light-grey indicator line + bright tip pip
        const float ptr_rad = cur_angle * 3.14159265f / 180.0f;
        lv_draw_line_dsc_t groove;
        lv_draw_line_dsc_init(&groove);
        groove.color = lv_color_hex(0x111210);
        groove.width = 5;
        groove.p1.x = cx + (int)(std::cos(ptr_rad) * std::max(3, r_face - 13));
        groove.p1.y = cy + (int)(std::sin(ptr_rad) * std::max(3, r_face - 13));
        groove.p2.x = cx + (int)(std::cos(ptr_rad) * (r_face - 1));
        groove.p2.y = cy + (int)(std::sin(ptr_rad) * (r_face - 1));
        lv_draw_line(layer, &groove);

        lv_draw_line_dsc_t ptr;
        lv_draw_line_dsc_init(&ptr);
        ptr.color = b->fine_mode ? COLOR_NEUTRAL_HIGHLIGHT : COLOR_KNOB_INDICATOR;
        ptr.width = 3;
        ptr.p1.x = cx + (int)(std::cos(ptr_rad) * std::max(4, r_face - 12));
        ptr.p1.y = cy + (int)(std::sin(ptr_rad) * std::max(4, r_face - 12));
        ptr.p2.x = cx + (int)(std::cos(ptr_rad) * (r_face - 2));
        ptr.p2.y = cy + (int)(std::sin(ptr_rad) * (r_face - 2));
        lv_draw_line(layer, &ptr);

        lv_draw_rect_dsc_t tip;
        lv_draw_rect_dsc_init(&tip);
        tip.radius = LV_RADIUS_CIRCLE;
        tip.bg_color = b->fine_mode ? COLOR_NEUTRAL_HIGHLIGHT : COLOR_KNOB_INDICATOR;
        tip.bg_opa = LV_OPA_COVER;
        const int tip_x = cx + (int)(std::cos(ptr_rad) * (r_face - 3));
        const int tip_y = cy + (int)(std::sin(ptr_rad) * (r_face - 3));
        lv_area_t tip_a{tip_x - 1, tip_y - 1, tip_x + 1, tip_y + 1};
        lv_draw_rect(layer, &tip, &tip_a);
    }, LV_EVENT_DRAW_MAIN, nullptr);

    lv_obj_add_event_cb(face, knob_interaction_event, LV_EVENT_PRESSED, binding);
    lv_obj_add_event_cb(face, knob_interaction_event, LV_EVENT_RELEASED, binding);
    lv_obj_add_event_cb(face, knob_interaction_event, LV_EVENT_PRESSING, binding);
    lv_obj_add_event_cb(face, knob_interaction_event, LV_EVENT_DOUBLE_CLICKED, binding);
    lv_obj_add_event_cb(face, knob_interaction_event, LV_EVENT_LONG_PRESSED, binding);

    return group;
}

lv_obj_t* instrument_meter(lv_obj_t* parent, float* peak, bool* clip, int x, int y, int w, int h, float* hold, float* rms) {
    auto* meter = lv_obj_create(parent);
    lv_obj_set_pos(meter, x, y);
    lv_obj_set_size(meter, w, h);
    lv_obj_remove_flag(meter, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(meter, COLOR_RECESSED, 0);
    lv_obj_set_style_bg_opa(meter, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(meter, COLOR_EDGE, 0);
    lv_obj_set_style_border_width(meter, 1, 0);
    lv_obj_set_style_radius(meter, 1, 0);
    lv_obj_set_style_pad_all(meter, 2, 0);

    auto* binding = new MeterBinding{peak, clip, meter, hold, rms, -1, -1, -1, false, nullptr};
    lv_obj_set_user_data(meter, binding);

    lv_obj_add_event_cb(meter, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;
        auto* b = static_cast<MeterBinding*>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e))));
        if (!b) return;
        lv_area_t a{};
        lv_obj_get_coords(static_cast<lv_obj_t*>(lv_event_get_target(e)), &a);
        const int segments = 24, inner_h = lv_area_get_height(&a) - 6;
        const int left = a.x1 + 3, right = a.x2 - 3;
        for (int i = 0; i < segments; ++i) {
            const bool active = i < b->lit, rms_active = i < b->rms_lit, held = i == b->held;
            const lv_color_t color = b->clipped && b->clip && *b->clip ? COLOR_RED : (i >= 22 ? COLOR_RED : (i >= 19 ? COLOR_YELLOW : COLOR_GREEN));
            lv_draw_rect_dsc_t d;
            lv_draw_rect_dsc_init(&d);
            d.bg_opa = LV_OPA_COVER;
            d.bg_color = held ? COLOR_TEXT : (active ? color : (rms_active ? lv_color_hex(0x12613F) : lv_color_hex(0x0E1318)));
            const int y_bot = a.y2 - 3 - (i * inner_h) / segments;
            const int y_top = a.y2 - 3 - ((i + 1) * inner_h) / segments + 1;
            lv_area_t led{left, y_top, right, y_bot};
            lv_draw_rect(lv_event_get_layer(e), &d, &led);
        }
    }, LV_EVENT_DRAW_MAIN, nullptr);

    binding->timer = lv_timer_create([](lv_timer_t* timer) {
        auto* b = static_cast<MeterBinding*>(lv_timer_get_user_data(timer));
        if (!b || !b->peak || !b->object) return;
        auto to_segments = [](float level) { return std::clamp((int)((level + 60.0f) * 24.0f / 63.0f), 0, 24); };
        const int lit = to_segments(*b->peak), held = b->hold ? to_segments(*b->hold) : -1, rms_lit = b->rms ? to_segments(*b->rms) : -1;
        const bool clipped = b->clip && *b->clip;
        if (lit != b->lit || held != b->held || rms_lit != b->rms_lit || clipped != b->clipped) {
            b->lit = lit; b->held = held; b->rms_lit = rms_lit; b->clipped = clipped;
            lv_obj_invalidate(b->object);
        }
    }, 60, binding);

    lv_obj_add_event_cb(meter, [](lv_event_t* e) {
        auto* b = static_cast<MeterBinding*>(lv_event_get_user_data(e));
        if (b) {
            if (b->timer) {
                lv_timer_delete(b->timer);
                b->timer = nullptr;
            }
            delete b;
        }
    }, LV_EVENT_DELETE, binding);

    return meter;
}

lv_obj_t* instrument_gr_meter(lv_obj_t* parent, float* gr_value, int x, int y, int w, int h, const bool* bypass, bool invert_bypass) {
    auto* meter = lv_obj_create(parent);
    lv_obj_set_pos(meter, x, y);
    lv_obj_set_size(meter, w, h);
    lv_obj_remove_flag(meter, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(meter, COLOR_RECESSED, 0);
    lv_obj_set_style_border_color(meter, COLOR_EDGE, 0);
    lv_obj_set_style_border_width(meter, 1, 0);
    lv_obj_set_style_radius(meter, 1, 0);
    lv_obj_set_style_pad_all(meter, 2, 0);

    auto* binding = new GrMeterBinding{gr_value, bypass, invert_bypass, meter, 0, 0, nullptr};
    lv_obj_set_user_data(meter, binding);

    lv_obj_add_event_cb(meter, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;
        auto* b = static_cast<GrMeterBinding*>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e))));
        if (!b) return;
        lv_layer_t* layer = lv_event_get_layer(e);
        lv_area_t a{};
        lv_obj_get_coords(static_cast<lv_obj_t*>(lv_event_get_target(e)), &a);
        const int segments = 20;
        const int inner_h = lv_area_get_height(&a) - 6;
        const int left = a.x1 + 3, right = a.x2 - 3;

        // Downward meter: segment 0 is at top (0 dB GR), segment 19 is at bottom (-24 dB GR)
        for (int i = 0; i < segments; ++i) {
            const bool active = (i < b->lit);
            const bool held = (i == b->held);
            lv_color_t color = (i >= 15) ? COLOR_RED : ((i >= 8) ? COLOR_ORANGE : COLOR_YELLOW);
            lv_draw_rect_dsc_t d;
            lv_draw_rect_dsc_init(&d);
            d.bg_opa = LV_OPA_COVER;
            d.bg_color = held ? COLOR_TEXT : (active ? color : lv_color_hex(0x0E1318));
            const int y_top = a.y1 + 3 + (i * inner_h) / segments;
            const int y_bot = a.y1 + 3 + ((i + 1) * inner_h) / segments - 1;
            lv_area_t led{left, y_top, right, y_bot};
            lv_draw_rect(layer, &d, &led);
        }

        // Calibrated scale tick marks on the meter frame (0, -3, -6, -12, -18 dB GR)
        lv_draw_line_dsc_t tick;
        lv_draw_line_dsc_init(&tick);
        tick.color = lv_color_hex(0x3D4752);
        tick.width = 1;
        const int tick_indices[] = {0, 2, 5, 10, 15}; // 0 dB, -3 dB, -6 dB, -12 dB, -18 dB
        for (int idx : tick_indices) {
            const int ty = a.y1 + 3 + (idx * inner_h) / segments;
            tick.p1.x = a.x1;     tick.p1.y = ty;
            tick.p2.x = a.x1 + 2; tick.p2.y = ty;
            lv_draw_line(layer, &tick);
        }
    }, LV_EVENT_DRAW_MAIN, nullptr);

    binding->timer = lv_timer_create([](lv_timer_t* timer) {
        auto* b = static_cast<GrMeterBinding*>(lv_timer_get_user_data(timer));
        if (!b || !b->gr_value || !b->object) return;
        const bool is_bypassed = b->bypass_flag
            ? (b->invert_bypass ? !(*b->bypass_flag) : *b->bypass_flag)
            : false;
        // GR is negative or positive magnitude (0 to -24 dB)
        const float gr_mag = is_bypassed ? 0.0f : std::abs(*b->gr_value);
        int lit = std::clamp((int)std::round(gr_mag * 20.0f / 24.0f), 0, 20);
        if (lit > b->held) {
            b->held = lit;
        } else if (b->held > 0) {
            // slow peak decay
            b->held = std::max(0, b->held - 1);
        }
        if (lit != b->lit) {
            b->lit = lit;
            lv_obj_invalidate(b->object);
        }
    }, 60, binding);

    lv_obj_add_event_cb(meter, [](lv_event_t* e) {
        auto* b = static_cast<GrMeterBinding*>(lv_event_get_user_data(e));
        if (b) {
            if (b->timer) {
                lv_timer_delete(b->timer);
                b->timer = nullptr;
            }
            delete b;
        }
    }, LV_EVENT_DELETE, binding);

    return meter;
}

lv_obj_t* instrument_master_meter(
    lv_obj_t* parent,
    float* peak_l,
    float* peak_r,
    bool* clip,
    int x, int y, int w, int h,
    float* hold_l,
    float* hold_r,
    float* rms_l,
    float* rms_r)
{
    auto* meter = lv_obj_create(parent);
    lv_obj_set_pos(meter, x, y);
    lv_obj_set_size(meter, w, h);
    lv_obj_remove_flag(meter, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(meter, COLOR_RECESSED, 0);
    lv_obj_set_style_bg_opa(meter, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(meter, COLOR_EDGE, 0);
    lv_obj_set_style_border_width(meter, 1, 0);
    lv_obj_set_style_radius(meter, 2, 0);
    lv_obj_set_style_pad_all(meter, 0, 0);

    auto* binding = new MasterMeterBinding{
        peak_l, peak_r, clip, hold_l, hold_r, rms_l, rms_r,
        meter, -1, -1, -1, -1, -1, -1, false, nullptr
    };
    lv_obj_set_user_data(meter, binding);

    lv_obj_add_event_cb(meter, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;
        auto* b = static_cast<MasterMeterBinding*>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e))));
        if (!b) return;
        lv_layer_t* layer = lv_event_get_layer(e);
        lv_area_t a{};
        lv_obj_get_coords(static_cast<lv_obj_t*>(lv_event_get_target(e)), &a);

        // Header: "dBFS" silkscreen label centered at top
        lv_draw_label_dsc_t hdr_dsc;
        lv_draw_label_dsc_init(&hdr_dsc);
        hdr_dsc.font = &ac_font_12;
        hdr_dsc.color = COLOR_TEXT2;
        hdr_dsc.text = "dBFS";
        hdr_dsc.align = LV_TEXT_ALIGN_CENTER;
        lv_area_t hdr_area{a.x1, a.y1 + 2, a.x2, a.y1 + 15};
        lv_draw_label(layer, &hdr_dsc, &hdr_area);

        // Meter bars geometry (24px top clearance keeps "+3" well below "dBFS")
        const int y_bar_top = a.y1 + 24;
        const int y_bar_bot = a.y2 - 7;
        const int inner_h = y_bar_bot - y_bar_top;
        if (inner_h <= 10) return;

        const int pad = 6;
        const int bar_w = 16;
        const int left_bar_x1 = a.x1 + pad;
        const int left_bar_x2 = left_bar_x1 + bar_w;
        const int right_bar_x2 = a.x2 - pad;
        const int right_bar_x1 = right_bar_x2 - bar_w;
        const int center_x1 = left_bar_x2 + 2;
        const int center_x2 = right_bar_x1 - 2;

        // Draw recessed backgrounds for left and right bars
        lv_draw_rect_dsc_t bar_bg;
        lv_draw_rect_dsc_init(&bar_bg);
        bar_bg.bg_color = lv_color_hex(0x080B0E);
        bar_bg.bg_opa = LV_OPA_COVER;
        bar_bg.border_color = lv_color_hex(0x1B222A);
        bar_bg.border_width = 1;
        bar_bg.radius = 1;

        lv_area_t l_bg_area{left_bar_x1, y_bar_top, left_bar_x2, y_bar_bot};
        lv_draw_rect(layer, &bar_bg, &l_bg_area);

        lv_area_t r_bg_area{right_bar_x1, y_bar_top, right_bar_x2, y_bar_bot};
        lv_draw_rect(layer, &bar_bg, &r_bg_area);

        const int segments = 28;

        // Draw Left meter segments
        for (int i = 0; i < segments; ++i) {
            const bool active = (i < b->lit_l);
            const bool rms_act = (i < b->rms_l_lit);
            const bool held = (i == b->held_l);
            const bool is_clip_seg = (i >= 25); // >= 0 dBFS
            const bool is_warn_seg = (i >= 21); // >= -6 dBFS
            lv_color_t color = is_clip_seg ? COLOR_RED : (is_warn_seg ? COLOR_YELLOW : COLOR_GREEN);
            if (is_clip_seg && b->clipped) color = COLOR_RED;

            lv_draw_rect_dsc_t d;
            lv_draw_rect_dsc_init(&d);
            d.bg_opa = LV_OPA_COVER;
            d.bg_color = held ? COLOR_TEXT : (active ? color : (rms_act ? lv_color_hex(0x12613F) : lv_color_hex(0x0C1014)));
            const int seg_bot = y_bar_bot - 1 - (i * (inner_h - 2)) / segments;
            const int seg_top = y_bar_bot - 1 - ((i + 1) * (inner_h - 2)) / segments + 1;
            lv_area_t led{left_bar_x1 + 2, seg_top, left_bar_x2 - 2, seg_bot};
            lv_draw_rect(layer, &d, &led);
        }

        // Draw Right meter segments
        for (int i = 0; i < segments; ++i) {
            const bool active = (i < b->lit_r);
            const bool rms_act = (i < b->rms_r_lit);
            const bool held = (i == b->held_r);
            const bool is_clip_seg = (i >= 25); // >= 0 dBFS
            const bool is_warn_seg = (i >= 21); // >= -6 dBFS
            lv_color_t color = is_clip_seg ? COLOR_RED : (is_warn_seg ? COLOR_YELLOW : COLOR_GREEN);
            if (is_clip_seg && b->clipped) color = COLOR_RED;

            lv_draw_rect_dsc_t d;
            lv_draw_rect_dsc_init(&d);
            d.bg_opa = LV_OPA_COVER;
            d.bg_color = held ? COLOR_TEXT : (active ? color : (rms_act ? lv_color_hex(0x12613F) : lv_color_hex(0x0C1014)));
            const int seg_bot = y_bar_bot - 1 - (i * (inner_h - 2)) / segments;
            const int seg_top = y_bar_bot - 1 - ((i + 1) * (inner_h - 2)) / segments + 1;
            lv_area_t led{right_bar_x1 + 2, seg_top, right_bar_x2 - 2, seg_bot};
            lv_draw_rect(layer, &d, &led);
        }

        // Calibrated studio piecewise-linear dBFS mapping:
        // -60..-24 dB (12 dB/step) -> 0.000..0.375
        // -24..  0 dB ( 6 dB/step) -> 0.375..0.875
        //   0.. +3 dB ( 3 dB/step) -> 0.875..1.000
        auto master_db_to_norm = [](float db) -> float {
            if (db <= -60.0f) return 0.0f;
            if (db <= -24.0f) return ((db + 60.0f) / 36.0f) * 0.375f;
            if (db <= 0.0f)   return 0.375f + ((db + 24.0f) / 24.0f) * 0.500f;
            if (db < 3.0f)    return 0.875f + (db / 3.0f) * 0.125f;
            return 1.0f;
        };

        // Draw Center dBFS Scale Ticks and Text
        struct ScaleTick {
            float db;
            const char* label;
            bool major;
        };
        static const ScaleTick ticks[] = {
            {  3.0f,  "+3", false },
            {  0.0f,   "0", true  },
            { -6.0f,  "-6", false },
            {-12.0f, "-12", true  },
            {-18.0f, "-18", false },
            {-24.0f, "-24", true  },
            {-36.0f, "-36", false },
            {-48.0f, "-48", false },
            {-60.0f, "-60", true  }
        };

        lv_draw_line_dsc_t tick_line;
        lv_draw_line_dsc_init(&tick_line);

        lv_draw_label_dsc_t lbl_dsc;
        lv_draw_label_dsc_init(&lbl_dsc);
        lbl_dsc.font = &ac_font_12;
        lbl_dsc.align = LV_TEXT_ALIGN_CENTER;

        for (const auto& tk : ticks) {
            const float t = master_db_to_norm(tk.db);
            const int ty = y_bar_bot - 1 - (int)std::round(t * (float)(inner_h - 2));

            // Tick lines pointing from center column towards left & right bars
            tick_line.color = (tk.db >= 0.0f) ? COLOR_RED : (tk.db == -6.0f ? COLOR_YELLOW : (tk.major ? lv_color_hex(0x5A6B7C) : lv_color_hex(0x35404C)));
            tick_line.width = 1;

            // Left tick mark
            tick_line.p1.x = left_bar_x2;     tick_line.p1.y = ty;
            tick_line.p2.x = left_bar_x2 + 4; tick_line.p2.y = ty;
            lv_draw_line(layer, &tick_line);

            // Right tick mark
            tick_line.p1.x = right_bar_x1 - 4; tick_line.p1.y = ty;
            tick_line.p2.x = right_bar_x1;     tick_line.p2.y = ty;
            lv_draw_line(layer, &tick_line);

            // Center tick label centered directly on its tick line
            lbl_dsc.color = (tk.db >= 0.0f) ? COLOR_RED : (tk.db == -6.0f ? COLOR_YELLOW : (tk.major ? COLOR_SILVER : COLOR_TEXT2));
            lbl_dsc.text = tk.label;
            lv_area_t txt_area{center_x1, ty - 6, center_x2, ty + 7};
            lv_draw_label(layer, &lbl_dsc, &txt_area);
        }
    }, LV_EVENT_DRAW_MAIN, nullptr);

    binding->timer = lv_timer_create([](lv_timer_t* timer) {
        auto* b = static_cast<MasterMeterBinding*>(lv_timer_get_user_data(timer));
        if (!b || !b->object) return;

        auto to_segments = [](float level) {
            float norm = 0.0f;
            if (level <= -60.0f) norm = 0.0f;
            else if (level <= -24.0f) norm = ((level + 60.0f) / 36.0f) * 0.375f;
            else if (level <= 0.0f)   norm = 0.375f + ((level + 24.0f) / 24.0f) * 0.500f;
            else if (level < 3.0f)    norm = 0.875f + (level / 3.0f) * 0.125f;
            else norm = 1.0f;
            return std::clamp((int)std::round(norm * 28.0f), 0, 28);
        };

        const float l_val = b->peak_l ? *b->peak_l : -90.0f;
        const float r_val = b->peak_r ? *b->peak_r : -90.0f;
        const int lit_l = to_segments(l_val);
        const int lit_r = to_segments(r_val);
        const int held_l = b->hold_l ? to_segments(*b->hold_l) : -1;
        const int held_r = b->hold_r ? to_segments(*b->hold_r) : -1;
        const int rms_l_lit = b->rms_l ? to_segments(*b->rms_l) : -1;
        const int rms_r_lit = b->rms_r ? to_segments(*b->rms_r) : -1;
        const bool clipped = b->clip && *b->clip;

        if (lit_l != b->lit_l || lit_r != b->lit_r ||
            held_l != b->held_l || held_r != b->held_r ||
            rms_l_lit != b->rms_l_lit || rms_r_lit != b->rms_r_lit ||
            clipped != b->clipped)
        {
            b->lit_l = lit_l; b->lit_r = lit_r;
            b->held_l = held_l; b->held_r = held_r;
            b->rms_l_lit = rms_l_lit; b->rms_r_lit = rms_r_lit;
            b->clipped = clipped;
            lv_obj_invalidate(b->object);
        }
    }, 40, binding);

    lv_obj_add_event_cb(meter, [](lv_event_t* e) {
        auto* b = static_cast<MasterMeterBinding*>(lv_event_get_user_data(e));
        if (b) {
            if (b->timer) {
                lv_timer_delete(b->timer);
                b->timer = nullptr;
            }
            delete b;
        }
    }, LV_EVENT_DELETE, binding);

    return meter;
}

namespace {
struct LimiterGrBinding {
    float* gr_value;
    const bool* enabled;
    lv_obj_t* object;
    int lit_px = 0;
    bool active = false;
    lv_timer_t* timer = nullptr;
};
} // namespace

lv_obj_t* instrument_limiter_gr_meter(
    lv_obj_t* parent,
    float* gr_db,
    const bool* enabled,
    int x, int y, int w, int h)
{
    auto* meter = lv_obj_create(parent);
    lv_obj_set_pos(meter, x, y);
    lv_obj_set_size(meter, w, h);
    lv_obj_remove_flag(meter, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(meter, COLOR_RECESSED, 0);
    lv_obj_set_style_bg_opa(meter, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(meter, COLOR_EDGE, 0);
    lv_obj_set_style_border_width(meter, 1, 0);
    lv_obj_set_style_radius(meter, 2, 0);
    lv_obj_set_style_pad_all(meter, 0, 0);

    auto* binding = new LimiterGrBinding{gr_db, enabled, meter, 0, false, nullptr};
    lv_obj_set_user_data(meter, binding);

    lv_obj_add_event_cb(meter, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;
        auto* b = static_cast<LimiterGrBinding*>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e))));
        if (!b) return;
        lv_layer_t* layer = lv_event_get_layer(e);
        lv_area_t a{};
        lv_obj_get_coords(static_cast<lv_obj_t*>(lv_event_get_target(e)), &a);

        const int track_left = a.x1 + 2;
        const int track_right = a.x2 - 2;
        const int track_w = track_right - track_left;
        const int track_top = a.y1 + 2;
        const int track_bot = a.y2 - 2;

        // Recessed track background
        lv_draw_rect_dsc_t bg_dsc;
        lv_draw_rect_dsc_init(&bg_dsc);
        bg_dsc.bg_color = lv_color_hex(0x07090C);
        bg_dsc.bg_opa = LV_OPA_COVER;
        lv_area_t tr_area{track_left, track_top, track_right, track_bot};
        lv_draw_rect(layer, &bg_dsc, &tr_area);

        // Draw active reduction bar deflecting from right (0 dB) to left (-18 dB)
        if (b->active && b->lit_px > 0) {
            lv_draw_rect_dsc_t bar_dsc;
            lv_draw_rect_dsc_init(&bar_dsc);
            bar_dsc.bg_color = COLOR_ORANGE;
            bar_dsc.bg_opa = LV_OPA_COVER;
            const int bar_x1 = std::max(track_left, track_right - b->lit_px);
            lv_area_t bar_area{bar_x1, track_top, track_right, track_bot};
            lv_draw_rect(layer, &bar_dsc, &bar_area);
        }

        // Tick reference marks at 0, -3, -6, -12, -18 dB
        static const float ref_dbs[] = {0.0f, -3.0f, -6.0f, -12.0f, -18.0f};
        lv_draw_line_dsc_t tick_dsc;
        lv_draw_line_dsc_init(&tick_dsc);
        tick_dsc.width = 1;

        for (float rdb : ref_dbs) {
            const float frac = std::clamp(-rdb / 18.0f, 0.0f, 1.0f);
            const int tx = track_right - (int)std::round(frac * (float)track_w);
            tick_dsc.color = (rdb == 0.0f) ? COLOR_SILVER : lv_color_hex(0x283440);
            tick_dsc.p1.x = tx; tick_dsc.p1.y = track_top;
            tick_dsc.p2.x = tx; tick_dsc.p2.y = track_top + 3;
            lv_draw_line(layer, &tick_dsc);
        }
    }, LV_EVENT_DRAW_MAIN, nullptr);

    binding->timer = lv_timer_create([](lv_timer_t* t) {
        auto* b = static_cast<LimiterGrBinding*>(lv_timer_get_user_data(t));
        if (!b || !b->object) return;
        const bool is_en = (b->enabled == nullptr || *b->enabled);
        const float gr = (b->gr_value && is_en) ? *b->gr_value : 0.0f;
        lv_area_t a{};
        lv_obj_get_coords(b->object, &a);
        const int track_w = std::max(1, (a.x2 - 2) - (a.x1 + 2));
        const float frac = is_en ? std::clamp(-gr / 18.0f, 0.0f, 1.0f) : 0.0f;
        const int lit = (int)std::round(frac * (float)track_w);
        const bool act = is_en && (gr < -0.1f);
        if (lit != b->lit_px || act != b->active) {
            b->lit_px = lit;
            b->active = act;
            lv_obj_invalidate(b->object);
        }
    }, 50, binding);

    lv_obj_add_event_cb(meter, [](lv_event_t* e) {
        auto* b = static_cast<LimiterGrBinding*>(lv_event_get_user_data(e));
        if (b) {
            if (b->timer) lv_timer_delete(b->timer);
            delete b;
        }
    }, LV_EVENT_DELETE, binding);

    return meter;
}

lv_obj_t* instrument_chart(lv_obj_t* parent, int x, int y, int w, int h, int count) {
    auto* chart = lv_obj_create(parent);
    lv_obj_set_pos(chart, x, y);
    lv_obj_set_size(chart, w, h);
    ui::rack_skin::apply_recessed_display(chart, lv_color_hex(0x07090C), lv_color_hex(0x28323D));
    lv_obj_remove_flag(chart, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(chart, LV_OBJ_FLAG_CLICKABLE);

    auto* b = new ChartBinding{};
    b->count = std::clamp(count, 2, 128);
    b->min_y = -18;
    b->max_y = 18;
    b->color = lv_color_hex(0xE2E8F0);
    b->reference_color = COLOR_TEXT2;
    lv_obj_set_user_data(chart, b);

    lv_obj_add_event_cb(chart, [](lv_event_t* e) {
        auto* b = static_cast<ChartBinding*>(lv_event_get_user_data(e));
        delete b;
    }, LV_EVENT_DELETE, b);

    // Draggable threshold interaction on chart
    lv_obj_add_event_cb(chart, [](lv_event_t* e) {
        auto* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));
        auto* b = static_cast<ChartBinding*>(lv_obj_get_user_data(obj));
        if (!b || !b->draggable_thresh) return;

        const lv_event_code_t code = lv_event_get_code(e);
        if (code == LV_EVENT_PRESSED) {
            lv_indev_t* indev = lv_indev_active();
            if (!indev) return;
            lv_point_t p{};
            lv_indev_get_point(indev, &p);

            lv_area_t a{};
            instrument_chart_get_plot_area(obj, &a);
            const int plot_w = a.x2 - a.x1;
            if (plot_w <= 0) return;

            if (p.x >= a.x1 - 16 && p.x <= a.x2 + 16 && p.y >= a.y1 - 16 && p.y <= a.y2 + 16) {
                b->is_dragging = true;
                const float norm = std::clamp((float)(p.x - a.x1) / (float)plot_w, 0.0f, 1.0f);
                *b->draggable_thresh = b->min_x + norm * (b->max_x - b->min_x);
                lv_obj_invalidate(obj);
                lv_obj_send_event(obj, LV_EVENT_VALUE_CHANGED, nullptr);
            }
        } else if (code == LV_EVENT_PRESSING) {
            if (!b->is_dragging) return;
            lv_indev_t* indev = lv_indev_active();
            if (!indev) return;
            lv_point_t p{};
            lv_indev_get_point(indev, &p);

            lv_area_t a{};
            instrument_chart_get_plot_area(obj, &a);
            const int plot_w = a.x2 - a.x1;
            if (plot_w <= 0) return;

            const float norm = std::clamp((float)(p.x - a.x1) / (float)plot_w, 0.0f, 1.0f);
            *b->draggable_thresh = b->min_x + norm * (b->max_x - b->min_x);
            lv_obj_invalidate(obj);
            lv_obj_send_event(obj, LV_EVENT_VALUE_CHANGED, nullptr);
        } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
            if (b->is_dragging) {
                b->is_dragging = false;
                lv_obj_invalidate(obj);
            }
        }
    }, LV_EVENT_ALL, b);

    lv_obj_add_event_cb(chart, [](lv_event_t* e) {
        if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;
        auto* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));
        auto* b = static_cast<ChartBinding*>(lv_obj_get_user_data(obj));
        if (!b) return;

        lv_area_t a{};
        lv_obj_get_coords(obj, &a);
        a.x1 += 10; a.x2 -= 8; a.y1 += 8; a.y2 -= 10;
        auto* layer = lv_event_get_layer(e);

        // 1. Technical background grid & CRT graticule
        lv_draw_line_dsc_t line;
        lv_draw_line_dsc_init(&line);
        line.width = 1;

        // Inner CRT graticule framing
        lv_draw_rect_dsc_t frame_dsc;
        lv_draw_rect_dsc_init(&frame_dsc);
        frame_dsc.bg_opa = LV_OPA_TRANSP;
        frame_dsc.border_color = lv_color_hex(0x1B232C);
        frame_dsc.border_width = 1;
        lv_draw_rect(layer, &frame_dsc, &a);

        if (b->is_frequency_axis) {
            static const float kGridFreqs[] = {
                20.0f, 50.0f, 100.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f, 20000.0f
            };
            const int num_grid = sizeof(kGridFreqs) / sizeof(kGridFreqs[0]);
            for (int i = 0; i < num_grid; ++i) {
                float freq = kGridFreqs[i];
                int x = (int)std::round(FrequencyAxis::frequency_to_x(freq, (float)a.x1, (float)a.x2, b->min_freq, b->max_freq));
                const bool is_1k = (freq == 1000.0f);
                const bool is_major = (freq == 100.0f || freq == 10000.0f);
                line.color = is_1k ? lv_color_hex(0x32404E) : (is_major ? lv_color_hex(0x242F3A) : lv_color_hex(0x151C22));
                line.p1.x = x; line.p1.y = a.y1;
                line.p2.x = x; line.p2.y = a.y2;
                lv_draw_line(layer, &line);
            }
        } else {
            for (int i = 0; i <= 10; ++i) {
                int x = a.x1 + (lv_area_get_width(&a) - 1) * i / 10;
                const bool is_center = (i == 5);
                line.color = is_center ? lv_color_hex(0x2C3744) : lv_color_hex(0x161D24);
                line.p1.x = x; line.p1.y = a.y1;
                line.p2.x = x; line.p2.y = a.y2;
                lv_draw_line(layer, &line);
            }
        }

        for (int i = 0; i <= 8; ++i) {
            int y = a.y1 + (lv_area_get_height(&a) - 1) * i / 8;
            const bool is_quarter = (i == 2 || i == 6);
            line.color = is_quarter ? lv_color_hex(0x202A34) : lv_color_hex(0x151C22);
            line.p1.x = a.x1; line.p1.y = y;
            line.p2.x = a.x2; line.p2.y = y;
            lv_draw_line(layer, &line);
        }

        // Center / 0 dB line highlight
        lv_draw_line_dsc_t center_line;
        lv_draw_line_dsc_init(&center_line);
        center_line.width = 1;
        center_line.color = lv_color_hex(0x3E4E5E);
        int mid_y = a.y1 + (lv_area_get_height(&a) - 1) / 2;
        center_line.p1.x = a.x1; center_line.p1.y = mid_y;
        center_line.p2.x = a.x2; center_line.p2.y = mid_y;
        lv_draw_line(layer, &center_line);

        // Precision reticle ticks along 0 dB axis
        lv_draw_line_dsc_t tick_line;
        lv_draw_line_dsc_init(&tick_line);
        tick_line.color = lv_color_hex(0x4F6174);
        tick_line.width = 1;
        if (b->is_frequency_axis) {
            static const float kTickFreqs[] = {
                20.0f, 50.0f, 100.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f, 20000.0f
            };
            for (float tf : kTickFreqs) {
                int tx = (int)std::round(FrequencyAxis::frequency_to_x(tf, (float)a.x1, (float)a.x2, b->min_freq, b->max_freq));
                tick_line.p1.x = tx; tick_line.p1.y = mid_y - 3;
                tick_line.p2.x = tx; tick_line.p2.y = mid_y + 3;
                lv_draw_line(layer, &tick_line);
            }
        }

        // 2. RTA Spectrum rendering (behind main curve)
        if (b->has_rta) {
            const auto& rta = b->rta;
            const int chart_w = lv_area_get_width(&a);
            const int chart_h = lv_area_get_height(&a);

            if (rta.mode == RtaMode::BANDS_31) {
                const int num_bars = 31;
                const auto& iso_freqs = RtaAnalyzer::get_iso_frequencies_31();
                for (int i = 0; i < num_bars; ++i) {
                    float val = rta.bands_31[i];
                    float pk = rta.peak_hold_31[i];
                    float norm_level = std::clamp((val - (-70.0f)) / 82.0f, 0.0f, 1.0f);
                    float norm_peak = std::clamp((pk - (-70.0f)) / 82.0f, 0.0f, 1.0f);
                    int bar_h = (int)(norm_level * (chart_h - 2));

                    float fc = iso_freqs[i];
                    float flo = (i == 0) ? b->min_freq : fc * std::pow(2.0f, -1.0f / 6.0f);
                    float fhi = (i == num_bars - 1) ? b->max_freq : fc * std::pow(2.0f, 1.0f / 6.0f);

                    int bx1 = (int)std::round(FrequencyAxis::frequency_to_x(flo, (float)a.x1, (float)a.x2, b->min_freq, b->max_freq));
                    int bx2 = (int)std::round(FrequencyAxis::frequency_to_x(fhi, (float)a.x1, (float)a.x2, b->min_freq, b->max_freq));
                    int bar_w = std::max(2, bx2 - bx1 - 1);
                    int bx = bx1;
                    int by1 = a.y2 - bar_h;
                    int by2 = a.y2;

                    // Multi-tier color palette based on dB level:
                    lv_color_t bar_color;
                    if (val < -40.0f) bar_color = lv_color_hex(0x0C2B1D);
                    else if (val < -18.0f) bar_color = lv_color_hex(0x165B3E);
                    else if (val < -4.0f) bar_color = lv_color_hex(0x27A863);
                    else bar_color = lv_color_hex(0xD4A017);

                    lv_draw_rect_dsc_t bar_dsc;
                    lv_draw_rect_dsc_init(&bar_dsc);
                    bar_dsc.bg_color = bar_color;
                    bar_dsc.bg_opa = LV_OPA_70;
                    bar_dsc.border_width = 0;
                    lv_area_t bar_area{bx, by1, bx + bar_w, by2};
                    lv_draw_rect(layer, &bar_dsc, &bar_area);

                    // Peak hold line
                    int peak_y = a.y2 - (int)(norm_peak * (chart_h - 2));
                    lv_draw_line_dsc_t pk_line;
                    lv_draw_line_dsc_init(&pk_line);
                    pk_line.color = (pk >= -4.0f) ? lv_color_hex(0xFFA000) : lv_color_hex(0x2EE59D);
                    pk_line.width = 1;
                    pk_line.p1.x = bx; pk_line.p1.y = peak_y;
                    pk_line.p2.x = bx + bar_w; pk_line.p2.y = peak_y;
                    lv_draw_line(layer, &pk_line);
                }
            } else if (rta.mode == RtaMode::BANDS_63) {
                const int num_bars = 63;
                for (int i = 0; i < num_bars; ++i) {
                    float val = rta.bands_63[i];
                    float pk = rta.peak_hold_63[i];
                    float norm_level = std::clamp((val - (-70.0f)) / 82.0f, 0.0f, 1.0f);
                    float norm_peak = std::clamp((pk - (-70.0f)) / 82.0f, 0.0f, 1.0f);
                    int bar_h = (int)(norm_level * (chart_h - 2));

                    float fc = b->min_freq * std::pow(b->max_freq / b->min_freq, (float)i / 62.0f);
                    float flo = (i == 0) ? b->min_freq : fc * std::pow(b->max_freq / b->min_freq, -0.5f / 62.0f);
                    float fhi = (i == num_bars - 1) ? b->max_freq : fc * std::pow(b->max_freq / b->min_freq, +0.5f / 62.0f);

                    int bx1 = (int)std::round(FrequencyAxis::frequency_to_x(flo, (float)a.x1, (float)a.x2, b->min_freq, b->max_freq));
                    int bx2 = (int)std::round(FrequencyAxis::frequency_to_x(fhi, (float)a.x1, (float)a.x2, b->min_freq, b->max_freq));
                    int bar_w = std::max(1, bx2 - bx1 - 1);
                    int bx = bx1;

                    lv_color_t bar_color;
                    if (val < -40.0f) bar_color = lv_color_hex(0x0C2B1D);
                    else if (val < -18.0f) bar_color = lv_color_hex(0x165B3E);
                    else if (val < -4.0f) bar_color = lv_color_hex(0x27A863);
                    else bar_color = lv_color_hex(0xD4A017);

                    lv_draw_rect_dsc_t bar_dsc;
                    lv_draw_rect_dsc_init(&bar_dsc);
                    bar_dsc.bg_color = bar_color;
                    bar_dsc.bg_opa = LV_OPA_60;
                    lv_area_t bar_area{bx, a.y2 - bar_h, bx + bar_w, a.y2};
                    lv_draw_rect(layer, &bar_dsc, &bar_area);

                    // Peak hold line
                    int peak_y = a.y2 - (int)(norm_peak * (chart_h - 2));
                    lv_draw_line_dsc_t pk_line;
                    lv_draw_line_dsc_init(&pk_line);
                    pk_line.color = (pk >= -4.0f) ? lv_color_hex(0xFFA000) : lv_color_hex(0x2EE59D);
                    pk_line.width = 1;
                    pk_line.p1.x = bx; pk_line.p1.y = peak_y;
                    pk_line.p2.x = bx + bar_w; pk_line.p2.y = peak_y;
                    lv_draw_line(layer, &pk_line);
                }
            } else {
                // FFT mode: filled intensity bars + bright envelope trace
                const int num_bins = 64;
                for (int i = 0; i < num_bins; ++i) {
                    float val = rta.fft_bins[i];
                    float f = std::clamp((val - (-70.0f)) / 82.0f, 0.0f, 1.0f);
                    int bh = (int)(f * (chart_h - 2));

                    float fc = b->min_freq * std::pow(b->max_freq / b->min_freq, (float)i / 63.0f);
                    float flo = (i == 0) ? b->min_freq : fc * std::pow(b->max_freq / b->min_freq, -0.5f / 63.0f);
                    float fhi = (i == num_bins - 1) ? b->max_freq : fc * std::pow(b->max_freq / b->min_freq, +0.5f / 63.0f);

                    int bx1 = (int)std::round(FrequencyAxis::frequency_to_x(flo, (float)a.x1, (float)a.x2, b->min_freq, b->max_freq));
                    int bx2 = (int)std::round(FrequencyAxis::frequency_to_x(fhi, (float)a.x1, (float)a.x2, b->min_freq, b->max_freq));
                    int bin_w = std::max(1, bx2 - bx1);

                    lv_color_t bar_col;
                    if (val < -40.0f) bar_col = lv_color_hex(0x0C2B1D);
                    else if (val < -18.0f) bar_col = lv_color_hex(0x165B3E);
                    else if (val < -4.0f) bar_col = lv_color_hex(0x27A863);
                    else bar_col = lv_color_hex(0xD4A017);

                    lv_draw_rect_dsc_t b_dsc;
                    lv_draw_rect_dsc_init(&b_dsc);
                    b_dsc.bg_color = bar_col;
                    b_dsc.bg_opa = LV_OPA_50;
                    b_dsc.border_width = 0;
                    lv_area_t b_area{bx1, a.y2 - bh, bx1 + bin_w, a.y2};
                    lv_draw_rect(layer, &b_dsc, &b_area);
                }

                lv_draw_line_dsc_t fft_line;
                lv_draw_line_dsc_init(&fft_line);
                fft_line.color = lv_color_hex(0x2EE59D);
                fft_line.width = 1;
                auto fft_pt = [&](int idx) {
                    float fc = b->min_freq * std::pow(b->max_freq / b->min_freq, (float)idx / 63.0f);
                    int x = (int)std::round(FrequencyAxis::frequency_to_x(fc, (float)a.x1, (float)a.x2, b->min_freq, b->max_freq));
                    float f = std::clamp((rta.fft_bins[idx] - (-70.0f)) / 82.0f, 0.0f, 1.0f);
                    int y = a.y2 - (int)(f * (chart_h - 1));
                    return lv_point_t{x, y};
                };
                for (int i = 1; i < num_bins; ++i) {
                    const auto pt1 = fft_pt(i - 1);
                    const auto pt2 = fft_pt(i);
                    fft_line.p1.x = pt1.x;
                    fft_line.p1.y = pt1.y;
                    fft_line.p2.x = pt2.x;
                    fft_line.p2.y = pt2.y;
                    lv_draw_line(layer, &fft_line);
                }
            }
        }

        const int plot_w = a.x2 - a.x1;
        const int plot_h = a.y2 - a.y1;

        auto x_coord = [&](float v) {
            const float span = (b->max_x > b->min_x) ? (b->max_x - b->min_x) : 1.0f;
            const float norm = std::clamp((v - b->min_x) / span, 0.0f, 1.0f);
            return a.x1 + (int)std::round(norm * (float)plot_w);
        };

        auto y_coord = [&](float v) {
            const float span = (b->max_y > b->min_y) ? (float)(b->max_y - b->min_y) : 1.0f;
            const float norm = std::clamp((v - b->min_y) / span, 0.0f, 1.0f);
            return a.y2 - (int)std::round(norm * (float)plot_h);
        };

        // 3. Curves (Reference unity line + Main signal curve)
        auto draw_values = [&](const float* values, int count, lv_color_t color, int width) {
            if (count < 2) return;
            lv_draw_line_dsc_t curve;
            lv_draw_line_dsc_init(&curve);
            curve.color = color;
            curve.width = width;
            auto point = [&](int i) {
                int x = a.x1 + (int)std::round((float)plot_w * (float)i / (float)(count - 1));
                int y = y_coord(values[i]);
                return lv_point_t{x, y};
            };
            for (int i = 1; i < count; ++i) {
                const auto p1 = point(i - 1), p2 = point(i);
                curve.p1.x = p1.x; curve.p1.y = p1.y;
                curve.p2.x = p2.x; curve.p2.y = p2.y;
                lv_draw_line(layer, &curve);
            }
        };

        if (b->reference_count > 1) {
            draw_values(b->reference, b->reference_count, b->reference_color, 1);
        }
        draw_values(b->values, b->count, b->color, 2);

        // Limiter Ceiling Marker (horizontal line across plot)
        if (b->limiter_ceiling && (!b->limiter_enabled || *b->limiter_enabled)) {
            const int lim_y = y_coord(*b->limiter_ceiling);
            lv_draw_line_dsc_t ceil_line;
            lv_draw_line_dsc_init(&ceil_line);
            ceil_line.color = lv_color_hex(0x9E3A3A); // muted crimson
            ceil_line.width = 1;
            ceil_line.p1.x = a.x1; ceil_line.p1.y = lim_y;
            ceil_line.p2.x = a.x2; ceil_line.p2.y = lim_y;
            lv_draw_line(layer, &ceil_line);

            char ceil_txt[24];
            std::snprintf(ceil_txt, sizeof(ceil_txt), "CEIL %.1fdB", *b->limiter_ceiling);
            lv_draw_label_dsc_t ceil_lbl;
            lv_draw_label_dsc_init(&ceil_lbl);
            ceil_lbl.font = &ac_font_12;
            ceil_lbl.color = lv_color_hex(0xD47272);
            ceil_lbl.text = ceil_txt;
            lv_area_t ceil_txt_area{a.x2 - 80, lim_y - 14, a.x2 - 4, lim_y};
            lv_draw_label(layer, &ceil_lbl, &ceil_txt_area);
        }

        // 4. Dynamics Markers (Compressor / Gate)
        if (b->threshold && b->input && b->output) {
            const int tx = x_coord(*b->threshold);

            // 1. Soft Knee Flanking Boundaries (if knee > 0)
            if (b->knee && *b->knee > 0.001f) {
                lv_draw_line_dsc_t knee_line;
                lv_draw_line_dsc_init(&knee_line);
                knee_line.color = COLOR_SUBTLE_SEPARATOR;
                knee_line.width = 1;
                for (float edge : {*b->threshold - *b->knee * 0.5f, *b->threshold + *b->knee * 0.5f}) {
                    int kx = x_coord(edge);
                    knee_line.p1.x = kx; knee_line.p1.y = a.y1;
                    knee_line.p2.x = kx; knee_line.p2.y = a.y2;
                    lv_draw_line(layer, &knee_line);
                }
            }

            // 2. Neutral Vertical Threshold Marker Line
            lv_draw_line_dsc_t marker;
            lv_draw_line_dsc_init(&marker);
            marker.color = b->is_dragging ? COLOR_PRIMARY_TEXT : COLOR_SELECTED_BORDER;
            marker.width = 2;
            marker.p1.x = tx; marker.p1.y = a.y1;
            marker.p2.x = tx; marker.p2.y = a.y2;
            lv_draw_line(layer, &marker);

            // 3. Tactile Circular Handle positioned on the threshold line at curve intersection
            float ty_val = *b->threshold;
            if (b->count > 1) {
                const float span = (b->max_x > b->min_x) ? (b->max_x - b->min_x) : 1.0f;
                const float norm_t = std::clamp((*b->threshold - b->min_x) / span, 0.0f, 1.0f);
                const float idx_f = norm_t * (float)(b->count - 1);
                const int idx = std::clamp((int)idx_f, 0, b->count - 2);
                const float frac = idx_f - (float)idx;
                ty_val = b->values[idx] * (1.0f - frac) + b->values[idx + 1] * frac;
            }
            const int ty = y_coord(ty_val);

            // Glow bloom during active drag
            if (b->is_dragging) {
                lv_draw_rect_dsc_t bloom;
                lv_draw_rect_dsc_init(&bloom);
                bloom.radius = LV_RADIUS_CIRCLE;
                bloom.bg_color = COLOR_SELECTED_BORDER;
                bloom.bg_opa = LV_OPA_30;
                lv_area_t bloom_area{tx - 11, ty - 11, tx + 11, ty + 11};
                lv_draw_rect(layer, &bloom, &bloom_area);
            }

            // Outer tactile ring of circular handle
            lv_draw_rect_dsc_t handle_outer;
            lv_draw_rect_dsc_init(&handle_outer);
            handle_outer.radius = LV_RADIUS_CIRCLE;
            handle_outer.bg_color = b->is_dragging ? COLOR_RAISED_PANEL : COLOR_CONTROL;
            handle_outer.bg_opa = LV_OPA_COVER;
            handle_outer.border_color = b->is_dragging ? COLOR_PRIMARY_TEXT : COLOR_SELECTED_BORDER;
            handle_outer.border_width = 2;
            lv_area_t outer_area{tx - 6, ty - 6, tx + 6, ty + 6};
            lv_draw_rect(layer, &handle_outer, &outer_area);

            // Center pip
            lv_draw_rect_dsc_t handle_core;
            lv_draw_rect_dsc_init(&handle_core);
            handle_core.radius = LV_RADIUS_CIRCLE;
            handle_core.bg_color = b->is_dragging ? COLOR_PRIMARY_TEXT : COLOR_KNOB_INDICATOR;
            handle_core.bg_opa = LV_OPA_COVER;
            lv_area_t core_area{tx - 2, ty - 2, tx + 2, ty + 2};
            lv_draw_rect(layer, &handle_core, &core_area);

            // 4. High-contrast dB readout badge at top of threshold line
            char t_txt[24];
            std::snprintf(t_txt, sizeof(t_txt), "%.1f dB", *b->threshold);
            const int badge_w = 58;
            const int badge_h = 16;
            const int bx1 = std::clamp(tx - badge_w / 2, a.x1 + 1, a.x2 - badge_w - 1);
            const int by1 = a.y1 + 2;

            lv_draw_rect_dsc_t badge_bg;
            lv_draw_rect_dsc_init(&badge_bg);
            badge_bg.radius = 2;
            badge_bg.bg_color = COLOR_RECESSED;
            badge_bg.bg_opa = LV_OPA_COVER;
            badge_bg.border_color = b->is_dragging ? COLOR_PRIMARY_TEXT : COLOR_SELECTED_BORDER;
            badge_bg.border_width = 1;
            lv_area_t badge_rect{bx1, by1, bx1 + badge_w, by1 + badge_h};
            lv_draw_rect(layer, &badge_bg, &badge_rect);

            lv_draw_label_dsc_t badge_lbl;
            lv_draw_label_dsc_init(&badge_lbl);
            badge_lbl.font = &ac_font_12;
            badge_lbl.color = COLOR_PRIMARY_TEXT;
            badge_lbl.align = LV_TEXT_ALIGN_CENTER;
            badge_lbl.text = t_txt;
            lv_area_t txt_rect{bx1, by1 + 1, bx1 + badge_w, by1 + badge_h};
            lv_draw_label(layer, &badge_lbl, &txt_rect);

            // 5. Operating Point Dot (rides on the curve according to input level)
            const float in_level = *b->input;
            if (in_level > -89.0f) {
                const int px = x_coord(in_level);
                const int py = y_coord(*b->output);

                // Outer phosphor bloom
                lv_draw_rect_dsc_t bloom;
                lv_draw_rect_dsc_init(&bloom);
                bloom.radius = LV_RADIUS_CIRCLE;
                bloom.bg_color = COLOR_CYAN;
                bloom.bg_opa = LV_OPA_30;
                lv_area_t bloom_area{px - 7, py - 7, px + 7, py + 7};
                lv_draw_rect(layer, &bloom, &bloom_area);

                // Core spot
                lv_draw_rect_dsc_t dot;
                lv_draw_rect_dsc_init(&dot);
                dot.bg_color = COLOR_CYAN;
                dot.bg_opa = LV_OPA_COVER;
                dot.border_color = COLOR_PRIMARY_TEXT;
                dot.border_width = 1;
                dot.radius = LV_RADIUS_CIRCLE;
                lv_area_t pt_area{px - 4, py - 4, px + 4, py + 4};
                lv_draw_rect(layer, &dot, &pt_area);

                // Center luminous pip
                lv_draw_rect_dsc_t pip;
                lv_draw_rect_dsc_init(&pip);
                pip.bg_color = lv_color_white();
                pip.bg_opa = LV_OPA_COVER;
                pip.radius = LV_RADIUS_CIRCLE;
                lv_area_t pip_area{px - 1, py - 1, px + 1, py + 1};
                lv_draw_rect(layer, &pip, &pip_area);
            }
        }
    }, LV_EVENT_DRAW_MAIN, nullptr);

    return chart;
}

void instrument_style_chart(lv_obj_t* chart, int min_y, int max_y) {
    auto* b = static_cast<ChartBinding*>(lv_obj_get_user_data(chart));
    if (b) {
        b->min_y = min_y;
        b->max_y = max_y;
        b->is_frequency_axis = true;
    }
}

void instrument_chart_series(lv_obj_t* chart, const float* samples, int count, int min_y, int max_y, lv_color_t color) {
    auto* b = static_cast<ChartBinding*>(lv_obj_get_user_data(chart));
    if (!b) return;
    b->count = std::clamp(count, 2, 128);
    b->min_y = min_y;
    b->max_y = max_y;
    b->color = color;
    std::copy(samples, samples + b->count, b->values);
    lv_obj_invalidate(chart);
}

void instrument_chart_reference(lv_obj_t* chart, const float* samples, int count, lv_color_t color) {
    auto* b = static_cast<ChartBinding*>(lv_obj_get_user_data(chart));
    if (!b) return;
    b->reference_count = std::clamp(count, 2, 128);
    b->reference_color = color;
    std::copy(samples, samples + b->reference_count, b->reference);
    lv_obj_invalidate(chart);
}

void instrument_chart_dynamics_markers(lv_obj_t* chart, float* threshold, float* knee, float* input, float* output) {
    auto* b = static_cast<ChartBinding*>(lv_obj_get_user_data(chart));
    if (!b) return;
    b->threshold = threshold;
    b->knee = knee;
    b->input = input;
    b->output = output;
    lv_obj_invalidate(chart);
}

void instrument_chart_draggable_threshold(lv_obj_t* chart, float* threshold, float min_db, float max_db) {
    auto* b = static_cast<ChartBinding*>(lv_obj_get_user_data(chart));
    if (!b) return;
    b->draggable_thresh = threshold;
    b->thresh_min_db = min_db;
    b->thresh_max_db = max_db;
    b->min_x = min_db;
    b->max_x = max_db;
    lv_obj_invalidate(chart);
}

void instrument_chart_set_x_axis(lv_obj_t* chart, float min_x, float max_x) {
    auto* b = static_cast<ChartBinding*>(lv_obj_get_user_data(chart));
    if (!b) return;
    b->min_x = min_x;
    b->max_x = max_x;
    lv_obj_invalidate(chart);
}

void instrument_chart_limiter_ceiling(lv_obj_t* chart, float* ceiling, bool* enabled) {
    auto* b = static_cast<ChartBinding*>(lv_obj_get_user_data(chart));
    if (!b) return;
    b->limiter_ceiling = ceiling;
    b->limiter_enabled = enabled;
    lv_obj_invalidate(chart);
}

void instrument_chart_rta_spectrum(lv_obj_t* chart, const RtaSnapshot& snapshot) {
    auto* b = static_cast<ChartBinding*>(lv_obj_get_user_data(chart));
    if (!b) return;
    b->has_rta = true;
    b->is_frequency_axis = true;
    b->rta = snapshot;
    lv_obj_invalidate(chart);
}

void instrument_chart_get_plot_area(lv_obj_t* chart, lv_area_t* out_area) {
    if (!chart || !out_area) return;
    lv_obj_get_coords(chart, out_area);
    out_area->x1 += 10;
    out_area->x2 -= 8;
    out_area->y1 += 8;
    out_area->y2 -= 10;
}

void instrument_chart_set_frequency_axis(lv_obj_t* chart, bool enable, float min_freq, float max_freq) {
    auto* b = static_cast<ChartBinding*>(lv_obj_get_user_data(chart));
    if (!b) return;
    b->is_frequency_axis = enable;
    b->min_freq = min_freq;
    b->max_freq = max_freq;
    lv_obj_invalidate(chart);
}

void instrument_knob_refresh(lv_obj_t* knob_group) {
    if (!knob_group) return;
    auto* b = static_cast<KnobBinding*>(lv_obj_get_user_data(knob_group));
    if (!b || !b->value) return;
    char txt[32];
    format_knob_value(txt, sizeof(txt), b->name, b->unit, *b->value, b->lo, b->hi, b->fine_mode);
    if (b->readout) lv_label_set_text(b->readout, txt);
    if (b->face) lv_obj_invalidate(b->face);
}

void instrument_knob_set_value(lv_obj_t* knob_group, float val) {
    if (!knob_group) return;
    auto* b = static_cast<KnobBinding*>(lv_obj_get_user_data(knob_group));
    if (!b || !b->value) return;
    *b->value = std::clamp(val, b->lo, b->hi);
    instrument_knob_refresh(knob_group);
}
