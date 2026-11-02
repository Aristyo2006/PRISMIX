#include "mixer_screen.h"
#include "../theme/theme.h"
#include "../widgets/instrument.h"
#include "../assets/rack_textures.h"
#include "../../model/mixer_state.h"
#include "../../model/audio_setup_config.h"
#include "../../model/routing_state.h"
#include "../../service/audio_service.h"
#include "../ui.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>
#include <string>

namespace {

constexpr int STRIP_WIDTH = 202;
constexpr int STRIP_GAP = 6;
constexpr int STRIP_HEIGHT = 914;

struct StripUI {
    int index; // -1 for master, 0..N-1 for input channels
    lv_obj_t* panel = nullptr;
    lv_obj_t* sec_label = nullptr;
    lv_obj_t* title_label = nullptr;
    lv_obj_t* input_label = nullptr;
    lv_obj_t* clip_flag = nullptr;
    lv_obj_t* fader_obj = nullptr;
    lv_obj_t* fader_label = nullptr;
    lv_obj_t* select_btn = nullptr;
    lv_obj_t* ren_btn = nullptr;
    lv_obj_t* del_btn = nullptr;

    // Meter bridge
    lv_obj_t* gr_hdr = nullptr;
    lv_obj_t* peak_hdr = nullptr;
    lv_obj_t* gr_meter = nullptr;
    lv_obj_t* meter_l = nullptr;       // Mono level meter or Stereo L meter
    lv_obj_t* meter_r = nullptr;       // Stereo R meter (nullptr when mono)
    lv_obj_t* gr_ftr = nullptr;
    lv_obj_t* meter_l_ftr = nullptr;   // "LEVEL" when mono, "L" when stereo
    lv_obj_t* meter_r_ftr = nullptr;   // nullptr when mono, "R" when stereo
    lv_obj_t* master_meter = nullptr;  // Unified stereo meter with center scale (Master only)

    // Controls
    lv_obj_t* gain_knob = nullptr;
    lv_obj_t* fmt_badge = nullptr;
    lv_obj_t* limiter_btn = nullptr;
    lv_obj_t* limiter_badge = nullptr;

    ChannelState* state = nullptr;
    ChannelFormat current_format = ChannelFormat::Mono;
};

lv_obj_t* g_screen = nullptr;
lv_obj_t* g_scroll_container = nullptr;
lv_obj_t* g_empty_container = nullptr;
lv_obj_t* g_add_card = nullptr;
lv_obj_t* g_add_label = nullptr;
lv_obj_t* g_empty_hint = nullptr;
std::vector<StripUI> g_input_strips;
StripUI g_master_strip{};

// Forward declarations
void update_strip_visual_selection(StripUI& s, bool is_selected);
void select_channel(int index);
void show_rename_modal(int index);
void show_delete_modal(int index);
void show_extension_info_modal(const char* title_text, const char* body_text);
void add_channel();
void show_channel_setup_modal(int channelId, bool createNew);
void create_channel();
void delete_channel(int index);
StripUI createChannelStrip(int index);
void updateChannelStrip(int index);
void syncChannelMeterFormat(StripUI& s);
void reposition_add_card();
StripUI build_strip_ui(lv_obj_t* parent, int index, int x, int y, int width, bool is_master);

void sync_selection_visuals(void) {
    if (g_screen != nullptr) {
        for (auto& s : g_input_strips) {
            update_strip_visual_selection(s, s.index == g_mixer_state.selected_channel);
        }
        update_strip_visual_selection(g_master_strip, g_mixer_state.selected_channel == -1);
    }
}

void select_channel(int index) {
    if (index < -1 || index >= (int)g_mixer_state.channels.size()) index = -1;
    g_mixer_state.selected_channel = index;
    g_mixer_state.selected_aux_bus = -1;
    const int num_ch = (int)g_mixer_state.channels.size();
    for (int i = 0; i < num_ch; ++i) {
        g_mixer_state.channels[i].select = (i == index);
    }
    g_mixer_state.master.select = (index == -1);

    sync_selection_visuals();

    ui_sync_all_screens();
}

void update_strip_visual_selection(StripUI& s, bool is_selected) {
    if (!s.panel) return;
    ui::rack_skin::apply_channel_strip_panel(s.panel, is_selected);

    if (s.title_label) {
        lv_obj_set_style_text_color(s.title_label, is_selected ? COLOR_TEXT : COLOR_SILVER, 0);
    }

    if (s.select_btn) {
        ui::rack_skin::apply_hardware_button(s.select_btn, is_selected, COLOR_SELECTED_BORDER, COLOR_RAISED_PANEL, COLOR_CONTROL);
        lv_obj_t* label = lv_obj_get_child(s.select_btn, 0);
        if (label) {
            lv_obj_set_style_text_color(label, is_selected ? COLOR_PRIMARY_TEXT : COLOR_SECONDARY_TEXT, 0);
        }
    }
}



static lv_obj_t* g_active_modal = nullptr;

void show_extension_info_modal(const char* title_text, const char* body_text) {
    if (g_active_modal) {
        lv_obj_delete(g_active_modal);
        g_active_modal = nullptr;
    }
    auto* backdrop = lv_obj_create(lv_layer_top());
    g_active_modal = backdrop;
    lv_obj_set_size(backdrop, 1920, 1080);
    lv_obj_align(backdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(backdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(backdrop, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_60, 0);
    lv_obj_set_style_border_width(backdrop, 0, 0);

    auto* dialog = lv_obj_create(backdrop);
    lv_obj_set_size(dialog, 620, 320);
    lv_obj_align(dialog, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);
    ui::rack_skin::apply_chassis_panel(dialog, COLOR_ACCENT);
    ui::rack_skin::add_faceplate_corner_screws(dialog, 620, 320, 6);
    lv_obj_set_style_pad_all(dialog, 20, 0);

    auto* title = lv_label_create(dialog);
    lv_label_set_text(title, title_text);
    lv_obj_set_pos(title, 8, 8);
    lv_obj_set_style_text_font(title, &ac_font_16, 0);
    lv_obj_set_style_text_color(title, COLOR_TEXT, 0);

    auto* desc = lv_label_create(dialog);
    lv_label_set_text(desc, body_text);
    lv_obj_set_pos(desc, 8, 48);
    lv_obj_set_width(desc, 564);
    lv_label_set_long_mode(desc, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(desc, &ac_font_12, 0);
    lv_obj_set_style_text_color(desc, COLOR_SILVER, 0);

    auto* close_btn = lv_button_create(dialog);
    lv_obj_set_size(close_btn, 130, 42);
    lv_obj_align(close_btn, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
    ui::rack_skin::apply_hardware_button(close_btn, false, COLOR_SELECTED_BORDER, COLOR_RAISED_PANEL, COLOR_CONTROL);
    auto* close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, "DISMISS");
    lv_obj_center(close_lbl);
    lv_obj_set_style_text_font(close_lbl, &ac_font_14, 0);
    lv_obj_set_style_text_color(close_lbl, COLOR_SILVER, 0);
    lv_obj_add_event_cb(close_btn, [](lv_event_t* e) {
        auto* bd = static_cast<lv_obj_t*>(lv_event_get_user_data(e));
        if (bd) lv_obj_delete(bd);
        g_active_modal = nullptr;
    }, LV_EVENT_CLICKED, backdrop);

    lv_obj_add_event_cb(backdrop, [](lv_event_t*) {
        g_active_modal = nullptr;
    }, LV_EVENT_DELETE, nullptr);
}

void show_rename_modal(int index) {
    if (index < 0 || index >= (int)g_mixer_state.channels.size()) return;

    if (g_active_modal) {
        lv_obj_delete(g_active_modal);
        g_active_modal = nullptr;
    }

    // Dark backdrop on lv_layer_top (moderate dimming)
    auto* backdrop = lv_obj_create(lv_layer_top());
    g_active_modal = backdrop;
    lv_obj_set_size(backdrop, 1920, 1080);
    lv_obj_align(backdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(backdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(backdrop, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_50, 0);
    lv_obj_set_style_border_width(backdrop, 0, 0);

    // Modal dialog box (centered above keyboard)
    auto* dialog = lv_obj_create(backdrop);
    lv_obj_set_size(dialog, 520, 210);
    lv_obj_align(dialog, LV_ALIGN_CENTER, 0, -140);
    lv_obj_remove_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);
    ui::rack_skin::apply_chassis_panel(dialog, COLOR_ACCENT);
    ui::rack_skin::add_faceplate_corner_screws(dialog, 520, 210, 6);
    lv_obj_set_style_pad_all(dialog, 16, 0);

    // Title
    auto* title = lv_label_create(dialog);
    lv_label_set_text(title, "RENAME CHANNEL");
    lv_obj_set_pos(title, 8, 4);
    lv_obj_set_style_text_font(title, &ac_font_16, 0);
    lv_obj_set_style_text_color(title, COLOR_TEXT, 0);

    // Subtitle
    auto* sub = lv_label_create(dialog);
    lv_label_set_text(sub, "NAME");
    lv_obj_set_pos(sub, 8, 38);
    lv_obj_set_style_text_font(sub, &ac_font_12, 0);
    lv_obj_set_style_text_color(sub, COLOR_TEXT2, 0);

    // Textarea
    auto* ta = lv_textarea_create(dialog);
    lv_obj_set_pos(ta, 8, 62);
    lv_obj_set_size(ta, 472, 42);
    lv_textarea_set_max_length(ta, 16);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_text(ta, g_mixer_state.channels[index].name);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x0F1316), 0);
    lv_obj_set_style_border_color(ta, COLOR_HIGHLIGHT, 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_text_color(ta, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(ta, &ac_font_16, 0);

    // Touchscreen keyboard positioned naturally below dialog
    auto* kb = lv_keyboard_create(backdrop);
    lv_obj_set_size(kb, 880, 280);
    lv_obj_align(kb, LV_ALIGN_CENTER, 0, 150);
    lv_keyboard_set_textarea(kb, ta);
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x161A1F), 0);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(kb, COLOR_EDGE, 0);
    lv_obj_set_style_border_width(kb, 1, 0);
    lv_obj_set_style_radius(kb, 3, 0);

    // High-contrast industrial console keys
    lv_obj_set_style_bg_color(kb, lv_color_hex(0x222A32), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_border_color(kb, lv_color_hex(0x3A4754), LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 1, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, COLOR_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_text_font(kb, &ac_font_16, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 3, LV_PART_ITEMS);

    struct RenameContext {
        int idx;
        lv_obj_t* ta;
        lv_obj_t* backdrop;
        bool closed = false;
    };
    auto* ctx = new RenameContext{index, ta, backdrop, false};

    lv_obj_add_event_cb(backdrop, [](lv_event_t* e) {
        g_active_modal = nullptr;
        auto* c = static_cast<RenameContext*>(lv_event_get_user_data(e));
        delete c;
    }, LV_EVENT_DELETE, ctx);

    auto apply_fn = [](RenameContext* c) {
        if (!c || c->closed) return;
        c->closed = true;
        const char* raw = lv_textarea_get_text(c->ta);
        if (raw) {
            std::string s(raw);
            size_t start = s.find_first_not_of(" \t\r\n");
            size_t end = s.find_last_not_of(" \t\r\n");
            if (start != std::string::npos && end != std::string::npos) {
                std::string trimmed = s.substr(start, end - start + 1);
                if (!trimmed.empty() && c->idx >= 0 && c->idx < (int)g_mixer_state.channels.size()) {
                    std::snprintf(g_mixer_state.channels[c->idx].name, sizeof(g_mixer_state.channels[c->idx].name), "%s", trimmed.c_str());
                    if (c->idx < (int)g_input_strips.size() && g_input_strips[c->idx].title_label) {
                        char txt[64];
                        std::snprintf(txt, sizeof(txt), "CH %02d   %s", c->idx + 1, g_mixer_state.channels[c->idx].name);
                        lv_label_set_text(g_input_strips[c->idx].title_label, txt);
                    }
                    if (c->idx == g_mixer_state.selected_channel) {
                        ui_update_status_bar();
                    }
                    ui_sync_all_screens();
                }
            }
        }
        lv_obj_delete(c->backdrop);
    };

    // Cancel Button
    auto* cancel_btn = lv_button_create(dialog);
    lv_obj_set_size(cancel_btn, 110, 40);
    lv_obj_set_pos(cancel_btn, 246, 126);
    ui::rack_skin::apply_hardware_button(cancel_btn, false, COLOR_EDGE, COLOR_CONTROL, lv_color_hex(0x181D23));
    auto* cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "CANCEL");
    lv_obj_center(cancel_lbl);
    lv_obj_set_style_text_font(cancel_lbl, &ac_font_14, 0);
    lv_obj_set_style_text_color(cancel_lbl, COLOR_TEXT2, 0);
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t* e) {
        auto* c = static_cast<RenameContext*>(lv_event_get_user_data(e));
        if (!c || c->closed) return;
        c->closed = true;
        lv_obj_delete(c->backdrop);
    }, LV_EVENT_CLICKED, ctx);

    // Apply Button
    auto* apply_btn = lv_button_create(dialog);
    lv_obj_set_size(apply_btn, 110, 40);
    lv_obj_set_pos(apply_btn, 370, 126);
    ui::rack_skin::apply_hardware_button(apply_btn, false, COLOR_SELECTED_BORDER, COLOR_RAISED_PANEL, COLOR_CONTROL);
    auto* apply_lbl = lv_label_create(apply_btn);
    lv_label_set_text(apply_lbl, "APPLY");
    lv_obj_center(apply_lbl);
    lv_obj_set_style_text_font(apply_lbl, &ac_font_14, 0);
    lv_obj_set_style_text_color(apply_lbl, COLOR_SILVER, 0);
    lv_obj_add_event_cb(apply_btn, [](lv_event_t* e) {
        auto* c = static_cast<RenameContext*>(lv_event_get_user_data(e));
        auto fn = reinterpret_cast<void(*)(RenameContext*)>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e))));
        fn(c);
    }, LV_EVENT_CLICKED, ctx);
    lv_obj_set_user_data(apply_btn, (void*)+apply_fn);

    // Enter / Ready on textarea or keyboard applies
    lv_obj_add_event_cb(ta, [](lv_event_t* e) {
        auto* c = static_cast<RenameContext*>(lv_event_get_user_data(e));
        auto fn = reinterpret_cast<void(*)(RenameContext*)>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e))));
        fn(c);
    }, LV_EVENT_READY, ctx);
    lv_obj_set_user_data(ta, (void*)+apply_fn);

    lv_obj_add_event_cb(kb, [](lv_event_t* e) {
        auto* c = static_cast<RenameContext*>(lv_event_get_user_data(e));
        auto fn = reinterpret_cast<void(*)(RenameContext*)>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(e))));
        fn(c);
    }, LV_EVENT_READY, ctx);
    lv_obj_add_event_cb(kb, [](lv_event_t* e) {
        auto* c = static_cast<RenameContext*>(lv_event_get_user_data(e));
        if (!c || c->closed) return;
        c->closed = true;
        lv_obj_delete(c->backdrop);
    }, LV_EVENT_CANCEL, ctx);
    lv_obj_set_user_data(kb, (void*)+apply_fn);
}

void show_delete_modal(int index) {
    if ((int)g_mixer_state.channels.size() <= MIN_CHANNELS) return;
    if (index < 0 || index >= (int)g_mixer_state.channels.size()) return;

    if (g_active_modal) {
        lv_obj_delete(g_active_modal);
        g_active_modal = nullptr;
    }

    // Dark backdrop
    auto* backdrop = lv_obj_create(lv_layer_top());
    g_active_modal = backdrop;
    lv_obj_add_event_cb(backdrop, [](lv_event_t*) { g_active_modal = nullptr; }, LV_EVENT_DELETE, nullptr);
    lv_obj_set_size(backdrop, 1920, 1080);
    lv_obj_align(backdrop, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(backdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(backdrop, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_70, 0);
    lv_obj_set_style_border_width(backdrop, 0, 0);

    // Modal dialog box
    auto* dialog = lv_obj_create(backdrop);
    lv_obj_set_size(dialog, 460, 210);
    lv_obj_center(dialog);
    lv_obj_remove_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);
    ui::rack_skin::apply_chassis_panel(dialog, COLOR_RED);
    ui::rack_skin::add_faceplate_corner_screws(dialog, 460, 210, 6);
    lv_obj_set_style_pad_all(dialog, 16, 0);

    auto* title = lv_label_create(dialog);
    lv_label_set_text(title, "DELETE CHANNEL");
    lv_obj_set_pos(title, 20, 16);
    lv_obj_set_style_text_font(title, &ac_font_16, 0);
    lv_obj_set_style_text_color(title, COLOR_RED, 0);

    char msg[128];
    std::snprintf(msg, sizeof(msg), "Delete CH %02d / %s?\n\nThis will remove the channel and its signal routing.",
                  index + 1, g_mixer_state.channels[index].name);
    auto* msg_label = lv_label_create(dialog);
    lv_label_set_text(msg_label, msg);
    lv_obj_set_pos(msg_label, 20, 52);
    lv_obj_set_style_text_font(msg_label, &ac_font_14, 0);
    lv_obj_set_style_text_color(msg_label, COLOR_TEXT, 0);

    struct DeleteContext {
        int idx;
        lv_obj_t* backdrop;
        bool closed = false;
    };
    auto* ctx = new DeleteContext{index, backdrop, false};

    // CANCEL button
    auto* cancel_btn = lv_button_create(dialog);
    lv_obj_set_size(cancel_btn, 110, 40);
    lv_obj_set_pos(cancel_btn, 190, 145);
    ui::rack_skin::apply_hardware_button(cancel_btn, false, COLOR_EDGE, COLOR_CONTROL, lv_color_hex(0x181D23));
    auto* cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "CANCEL");
    lv_obj_center(cancel_lbl);
    lv_obj_set_style_text_font(cancel_lbl, &ac_font_12, 0);
    lv_obj_set_style_text_color(cancel_lbl, COLOR_TEXT2, 0);
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t* e) {
        auto* c = static_cast<DeleteContext*>(lv_event_get_user_data(e));
        if (!c || c->closed) return;
        c->closed = true;
        lv_obj_delete(c->backdrop);
        delete c;
    }, LV_EVENT_CLICKED, ctx);

    // DELETE button
    auto* del_btn = lv_button_create(dialog);
    lv_obj_set_size(del_btn, 110, 40);
    lv_obj_set_pos(del_btn, 316, 145);
    ui::rack_skin::apply_hardware_button(del_btn, false, COLOR_RED, lv_color_hex(0x5A1818), lv_color_hex(0x3B1010));
    auto* del_lbl = lv_label_create(del_btn);
    lv_label_set_text(del_lbl, "DELETE");
    lv_obj_center(del_lbl);
    lv_obj_set_style_text_font(del_lbl, &ac_font_12, 0);
    lv_obj_set_style_text_color(del_lbl, lv_color_hex(0xFCA5A5), 0);
    lv_obj_add_event_cb(del_btn, [](lv_event_t* e) {
        auto* c = static_cast<DeleteContext*>(lv_event_get_user_data(e));
        if (!c || c->closed) return;
        const int idx = c->idx;
        c->closed = true;
        lv_obj_delete(c->backdrop);
        delete c;
        delete_channel(idx);
    }, LV_EVENT_CLICKED, ctx);
}

void reposition_add_card() {
    const int count = (int)g_input_strips.size();
    if (g_empty_container) {
        if (count == 0) {
            lv_obj_remove_flag(g_empty_container, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(g_empty_container, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (g_add_card) {
        if (count == 0) {
            lv_obj_add_flag(g_add_card, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_remove_flag(g_add_card, LV_OBJ_FLAG_HIDDEN);
            const int x = 8 + count * (STRIP_WIDTH + STRIP_GAP);
            lv_obj_set_size(g_add_card, 130, STRIP_HEIGHT);
            lv_obj_set_pos(g_add_card, x, 0);
        }
    }
}

void syncChannelMeterFormat(StripUI& s) {
    if (!s.state || !s.panel || s.index < 0) return;
    if (s.current_format == s.state->format) return;

    if (s.state->format == ChannelFormat::Mono) {
        // Transition to Mono layout:
        // Safely remove redundant Right channel meter & footer
        if (s.meter_r) {
            lv_obj_delete(s.meter_r);
            s.meter_r = nullptr;
        }
        if (s.meter_r_ftr) {
            lv_obj_delete(s.meter_r_ftr);
            s.meter_r_ftr = nullptr;
        }
        // Reposition Compressor GR meter
        if (s.gr_meter) {
            lv_obj_set_pos(s.gr_meter, 20, 86);
            lv_obj_set_size(s.gr_meter, 16, 142);
        }
        if (s.gr_hdr) lv_obj_set_pos(s.gr_hdr, 20, 70);
        if (s.gr_ftr) lv_obj_set_pos(s.gr_ftr, 20, 232);

        // Compact Single Left meter (24px width, not stretched!)
        if (s.meter_l) {
            lv_obj_set_pos(s.meter_l, 48, 86);
            lv_obj_set_size(s.meter_l, 24, 142);
            lv_obj_invalidate(s.meter_l);
        }
        if (s.meter_l_ftr) {
            lv_obj_set_pos(s.meter_l_ftr, 42, 232);
            lv_label_set_text(s.meter_l_ftr, "LEVEL");
        }
        if (s.peak_hdr) {
            lv_obj_set_pos(s.peak_hdr, 48, 70);
        }
        if (s.sec_label) {
            lv_label_set_text(s.sec_label, "MONO INPUT");
        }
        s.current_format = ChannelFormat::Mono;
    } else {
        // Transition to Stereo layout:
        if (s.gr_meter) {
            lv_obj_set_pos(s.gr_meter, 14, 86);
            lv_obj_set_size(s.gr_meter, 16, 142);
        }
        if (s.gr_hdr) lv_obj_set_pos(s.gr_hdr, 14, 70);
        if (s.gr_ftr) lv_obj_set_pos(s.gr_ftr, 14, 232);

        // Compact Left meter (24px width)
        if (s.meter_l) {
            lv_obj_set_pos(s.meter_l, 36, 86);
            lv_obj_set_size(s.meter_l, 24, 142);
            lv_obj_invalidate(s.meter_l);
        }
        if (s.meter_l_ftr) {
            lv_obj_set_pos(s.meter_l_ftr, 44, 232);
            lv_label_set_text(s.meter_l_ftr, "L");
        }
        // Recreate Right meter if missing
        if (!s.meter_r) {
            s.meter_r = instrument_meter(s.panel, &s.state->peak_r, &s.state->clip, 64, 86, 24, 142, &s.state->peak_hold_r, &s.state->rms_r);
        }
        if (!s.meter_r_ftr) {
            s.meter_r_ftr = instrument_label(s.panel, "R", 72, 232, &ac_font_12, COLOR_TEXT2);
        }
        if (s.peak_hdr) {
            lv_obj_set_pos(s.peak_hdr, 46, 70);
        }
        if (s.sec_label) {
            lv_label_set_text(s.sec_label, "STEREO INPUT");
        }
        s.current_format = ChannelFormat::Stereo;
    }
}

void updateChannelStrip(int index) {
    if (index < 0 || index >= (int)g_input_strips.size()) return;
    auto& s = g_input_strips[index];
    s.state = &g_mixer_state.channels[index];
    syncChannelMeterFormat(s);
    if (s.title_label) {
        char text[64];
        std::snprintf(text, sizeof(text), "CH %02d   %s", index + 1, s.state->name);
        lv_label_set_text(s.title_label, text);
    }
    if (s.input_label) {
        lv_label_set_text(s.input_label, s.state->input_label);
    }
    if (s.fmt_badge) {
        const bool isSt = (s.state->format == ChannelFormat::Stereo);
        lv_label_set_text(s.fmt_badge, isSt ? "STEREO" : "MONO");
        lv_obj_set_style_text_color(s.fmt_badge, isSt ? COLOR_SILVER : COLOR_TEXT2, 0);
    }
    update_strip_visual_selection(s, index == g_mixer_state.selected_channel);
}

void removeChannelStrip(int index) {
    if (index < 0 || index >= (int)g_input_strips.size()) return;
    if (g_input_strips[index].panel) {
        lv_obj_delete(g_input_strips[index].panel);
    }
    g_input_strips.erase(g_input_strips.begin() + index);
}

void delete_channel(int index) {
    if ((int)g_mixer_state.channels.size() <= MIN_CHANNELS) return;
    if (index < 0 || index >= (int)g_mixer_state.channels.size()) return;

    const int routeSlot = g_mixer_state.channels[index].id - 1;
    if (routeSlot >= 0 && routeSlot < (int)RoutingMatrix::NUM_CHANNELS) {
        g_routing_matrix.set_channel_input_source(routeSlot, -1);
        g_routing_matrix.set_channel_main_lr(routeSlot, false);
        for (size_t bus = 0; bus < RoutingMatrix::NUM_BUSES; ++bus)
            g_routing_matrix.set_channel_bus(routeSlot, bus, false);
    }

    // Cleanly delete all input strips (timers & bindings auto-clean via LV_EVENT_DELETE)
    for (auto& s : g_input_strips) {
        if (s.panel) lv_obj_delete(s.panel);
    }
    g_input_strips.clear();

    // Erase channel from model
    g_mixer_state.channels.erase(g_mixer_state.channels.begin() + index);

    // Update selected channel
    int cur_sel = g_mixer_state.selected_channel;
    if (cur_sel == index) {
        cur_sel = std::min(index, (int)g_mixer_state.channels.size() - 1);
    } else if (cur_sel > index) {
        cur_sel = cur_sel - 1;
    }
    g_mixer_state.selected_channel = cur_sel;

    // Recreate strips for all remaining channels
    const int num_ch = (int)g_mixer_state.channels.size();
    for (int i = 0; i < num_ch; ++i) {
        createChannelStrip(i);
    }
    reposition_add_card();
    if (num_ch == 0) {
        if (g_add_label) lv_label_set_text(g_add_label, "ADD INPUT");
        if (g_empty_hint) lv_obj_clear_flag(g_empty_hint, LV_OBJ_FLAG_HIDDEN);
    }

    select_channel(g_mixer_state.selected_channel);
    ui_update_status_bar();
    ui_sync_all_screens();
}

StripUI createChannelStrip(int index) {
    ChannelState* state = &g_mixer_state.channels[index];
    const int x = 8 + index * (STRIP_WIDTH + STRIP_GAP);
    StripUI s = build_strip_ui(g_scroll_container, index, x, 0, STRIP_WIDTH, false);
    g_input_strips.push_back(s);
    reposition_add_card();
    return s;
}

void persistEndpointChoice(bool capture, const std::string& id) {
    AudioSetupConfig setup;
    load_audio_setup(default_audio_setup_path(), setup);
    if (capture) setup.captureDeviceId = id;
    else setup.renderDeviceId = id;
    std::string error;
    save_audio_setup(setup, default_audio_setup_path(), &error);
}

std::string endpointOptions(const std::vector<AudioDeviceChoice>& devices, bool capture) {
    std::string options;
    for (const auto& device : devices) {
        if (!options.empty()) options += '\n';
        options += device.name.empty() ? device.id : device.name;
        options += "  [" + std::to_string(device.channels) + (capture ? " inputs]" : " outputs]");
        if (device.isDefault) options += "  [DEFAULT]";
    }
    return options.empty() ? "No matching audio endpoints" : options;
}

int selectedEndpoint(const std::vector<AudioDeviceChoice>& devices, const std::string& selectedId) {
    for (size_t i = 0; i < devices.size(); ++i) if (devices[i].id == selectedId) return static_cast<int>(i);
    for (size_t i = 0; i < devices.size(); ++i) if (devices[i].isDefault) return static_cast<int>(i);
    return devices.empty() ? -1 : 0;
}

struct ChannelSetupModal {
    bool createNew = false;
    int channelId = 0;
    ChannelFormat format = ChannelFormat::Mono;
    lv_obj_t* backdrop = nullptr;
    lv_obj_t* formatMonoBtn = nullptr;
    lv_obj_t* formatStereoBtn = nullptr;
    lv_obj_t* captureDevice = nullptr;
    lv_obj_t* captureInput = nullptr;
    lv_obj_t* renderDevice = nullptr;
    lv_obj_t* outputRoute = nullptr;
    lv_obj_t* detail = nullptr;
    lv_obj_t* error = nullptr;
    lv_timer_t* refreshTimer = nullptr;
    std::vector<AudioDeviceChoice> captureDevices;
    std::vector<AudioDeviceChoice> renderDevices;
    bool populated = false;
};

bool channelUsesPhysicalInput(const ChannelState& existing, int physicalInput) {
    if (physicalInput < 0) return false;
    if (existing.input_source_index == physicalInput) return true;
    if (existing.format == ChannelFormat::Stereo && existing.input_source_index_r == physicalInput) return true;
    return false;
}

void updateFormatSelectorButtons(ChannelSetupModal* view) {
    if (!view) return;
    const bool isMono = (view->format == ChannelFormat::Mono);
    if (view->formatMonoBtn) {
        lv_obj_set_style_bg_color(view->formatMonoBtn, isMono ? COLOR_RAISED_PANEL : COLOR_CONTROL, 0);
        lv_obj_set_style_border_color(view->formatMonoBtn, isMono ? COLOR_SELECTED_BORDER : COLOR_STANDARD_BORDER, 0);
        lv_obj_set_style_border_width(view->formatMonoBtn, isMono ? 2 : 1, 0);
        if (lv_obj_get_child_count(view->formatMonoBtn) > 0) {
            lv_obj_set_style_text_color(lv_obj_get_child(view->formatMonoBtn, 0), isMono ? COLOR_PRIMARY_TEXT : COLOR_SECONDARY_TEXT, 0);
        }
    }
    if (view->formatStereoBtn) {
        lv_obj_set_style_bg_color(view->formatStereoBtn, !isMono ? COLOR_RAISED_PANEL : COLOR_CONTROL, 0);
        lv_obj_set_style_border_color(view->formatStereoBtn, !isMono ? COLOR_SELECTED_BORDER : COLOR_STANDARD_BORDER, 0);
        lv_obj_set_style_border_width(view->formatStereoBtn, !isMono ? 2 : 1, 0);
        if (lv_obj_get_child_count(view->formatStereoBtn) > 0) {
            lv_obj_set_style_text_color(lv_obj_get_child(view->formatStereoBtn, 0), !isMono ? COLOR_PRIMARY_TEXT : COLOR_SECONDARY_TEXT, 0);
        }
    }
}

void updateCaptureInputs(ChannelSetupModal* view, int preferredInput = 0) {
    if (!view || !view->captureInput) return;
    auto& service = audio_service();
    const auto snapshot = service.snapshot();
    const int deviceIndex = selectedEndpoint(snapshot.captureDevices, snapshot.captureDeviceId);
    if (deviceIndex < 0) {
        lv_dropdown_set_options(view->captureInput, "No capture inputs available");
        lv_obj_add_state(view->captureInput, LV_STATE_DISABLED);
        return;
    }
    const auto& device = snapshot.captureDevices[static_cast<size_t>(deviceIndex)];
    std::string options;
    uint32_t optionCount = 0;
    if (view->format == ChannelFormat::Stereo) {
        if (device.channels < 2) {
            lv_dropdown_set_options(view->captureInput, "Stereo channel requires >= 2 physical capture channels");
            lv_obj_add_state(view->captureInput, LV_STATE_DISABLED);
            return;
        }
        for (uint32_t channel = 0; channel + 1 < device.channels; ++channel) {
            if (!options.empty()) options += '\n';
            char label[72];
            std::snprintf(label, sizeof(label), "STEREO PAIR %02u-%02u  (IN %02u L / IN %02u R)",
                          channel + 1, channel + 2, channel + 1, channel + 2);
            options += label;
            ++optionCount;
        }
    } else {
        for (uint32_t channel = 0; channel < device.channels; ++channel) {
            if (!options.empty()) options += '\n';
            char label[64];
            std::snprintf(label, sizeof(label), "SOUNDCARD INPUT %02u  (MONO)", channel + 1);
            options += label;
            ++optionCount;
        }
    }
    lv_dropdown_set_options(view->captureInput, options.empty() ? "No exposed capture channels" : options.c_str());
    lv_obj_remove_state(view->captureInput, LV_STATE_DISABLED);
    if (optionCount > 0) {
        lv_dropdown_set_selected(view->captureInput,
            std::min<uint32_t>(static_cast<uint32_t>(std::max(preferredInput, 0)), optionCount - 1));
    }
    if (view->detail) {
        lv_label_set_text_fmt(view->detail, "%s exposes %u capture inputs and %u render outputs (%s mode).",
            device.name.c_str(), device.channels,
            snapshot.renderDevices.empty() ? 0u : snapshot.renderDevices[
                static_cast<size_t>(std::max(selectedEndpoint(snapshot.renderDevices, snapshot.renderDeviceId), 0))].channels,
            view->format == ChannelFormat::Stereo ? "STEREO 2-CH" : "MONO 1-CH");
    }
}

void updateOutputRoutes(ChannelSetupModal* view, ChannelOutputRoute preferredRoute, int preferredIndex) {
    if (!view || !view->outputRoute) return;
    const auto snapshot = audio_service().snapshot();
    const int deviceIndex = selectedEndpoint(snapshot.renderDevices, snapshot.renderDeviceId);
    const uint32_t channels = deviceIndex < 0 ? 0 : snapshot.renderDevices[static_cast<size_t>(deviceIndex)].channels;
    std::vector<std::pair<ChannelOutputRoute, int>> mapping;
    std::string options;
    auto addOption = [&](const std::string& label, ChannelOutputRoute route, int index) {
        if (!options.empty()) options += '\n';
        options += label;
        mapping.emplace_back(route, index);
    };
    if (channels >= 2) addOption("MAIN L/R  (OUTPUTS 01-02)", ChannelOutputRoute::MainLR, 0);
    const uint32_t auxCount = channels > 2 ? channels - 2 : 0;
    for (uint32_t aux = 0; aux < auxCount; ++aux) {
        char label[80];
        std::snprintf(label, sizeof(label), "AUX %u BUS  ->  OUTPUT %02u", aux + 1, aux + 3);
        addOption(label, ChannelOutputRoute::Aux, static_cast<int>(aux));
    }
    for (uint32_t physical = 2; physical < channels; ++physical) {
        char label[64];
        std::snprintf(label, sizeof(label), "OMNI OUT %02u  (DIRECT)", physical + 1);
        addOption(label, ChannelOutputRoute::Omni, static_cast<int>(physical));
    }
    if (options.empty()) options = "Selected device exposes no routable outputs";
    lv_dropdown_set_options(view->outputRoute, options.c_str());
    if (mapping.empty()) {
        lv_obj_add_state(view->outputRoute, LV_STATE_DISABLED);
        return;
    }
    lv_obj_remove_state(view->outputRoute, LV_STATE_DISABLED);
    uint32_t selected = 0;
    for (size_t i = 0; i < mapping.size(); ++i)
        if (mapping[i].first == preferredRoute && mapping[i].second == preferredIndex) selected = static_cast<uint32_t>(i);
    lv_dropdown_set_selected(view->outputRoute, selected);
}

bool decodeSelectedOutput(const ChannelSetupModal* view, ChannelOutputRoute& route, int& index) {
    if (!view || !view->outputRoute) return false;
    const auto snapshot = audio_service().snapshot();
    const int deviceIndex = selectedEndpoint(snapshot.renderDevices, snapshot.renderDeviceId);
    if (deviceIndex < 0) return false;
    const uint32_t channels = snapshot.renderDevices[static_cast<size_t>(deviceIndex)].channels;
    std::vector<std::pair<ChannelOutputRoute, int>> mapping;
    if (channels >= 2) mapping.emplace_back(ChannelOutputRoute::MainLR, 0);
    const uint32_t auxCount = channels > 2 ? channels - 2 : 0;
    for (uint32_t aux = 0; aux < auxCount; ++aux) mapping.emplace_back(ChannelOutputRoute::Aux, static_cast<int>(aux));
    for (uint32_t physical = 2; physical < channels; ++physical)
        mapping.emplace_back(ChannelOutputRoute::Omni, static_cast<int>(physical));
    const uint32_t selected = lv_dropdown_get_selected(view->outputRoute);
    if (selected >= mapping.size()) return false;
    route = mapping[selected].first;
    index = mapping[selected].second;
    return true;
}

void populateChannelSetupModal(ChannelSetupModal* view) {
    if (!view || view->populated) return;
    const auto snapshot = audio_service().snapshot();
    if (!snapshot.devicesReady) return;
    view->captureDevices = snapshot.captureDevices;
    view->renderDevices = snapshot.renderDevices;
    lv_dropdown_set_options(view->captureDevice, endpointOptions(view->captureDevices, true).c_str());
    lv_dropdown_set_options(view->renderDevice, endpointOptions(view->renderDevices, false).c_str());
    const int capture = selectedEndpoint(view->captureDevices, snapshot.captureDeviceId);
    const int render = selectedEndpoint(view->renderDevices, snapshot.renderDeviceId);
    if (capture >= 0) lv_dropdown_set_selected(view->captureDevice, static_cast<uint32_t>(capture));
    if (render >= 0) lv_dropdown_set_selected(view->renderDevice, static_cast<uint32_t>(render));
    view->populated = true;
    int input = 0;
    ChannelOutputRoute route = ChannelOutputRoute::MainLR;
    int routeIndex = 0;
    if (!view->createNew) {
        if (auto* channel = mixer_find_channel_by_id(view->channelId)) {
            view->format = channel->format;
            input = std::max(channel->input_source_index, 0);
            route = channel->output_route;
            routeIndex = channel->output_route_index;
        }
    } else {
        const int captureIndex = selectedEndpoint(snapshot.captureDevices, snapshot.captureDeviceId);
        if (captureIndex >= 0) {
            const uint32_t inputCount = snapshot.captureDevices[static_cast<size_t>(captureIndex)].channels;
            for (uint32_t candidate = 0; candidate < inputCount; ++candidate) {
                if (view->format == ChannelFormat::Stereo && candidate + 1 >= inputCount) break;
                const bool inUse = std::any_of(g_mixer_state.channels.begin(), g_mixer_state.channels.end(),
                    [candidate, view](const ChannelState& existing) {
                        if (channelUsesPhysicalInput(existing, static_cast<int>(candidate))) return true;
                        if (view->format == ChannelFormat::Stereo &&
                            channelUsesPhysicalInput(existing, static_cast<int>(candidate + 1))) return true;
                        return false;
                    });
                if (!inUse) {
                    input = static_cast<int>(candidate);
                    break;
                }
            }
        }
    }
    updateFormatSelectorButtons(view);
    updateCaptureInputs(view, input);
    updateOutputRoutes(view, route, routeIndex);
    if (view->refreshTimer) {
        lv_timer_delete(view->refreshTimer);
        view->refreshTimer = nullptr;
    }
}

void selectCaptureEndpoint(lv_event_t* event) {
    auto* view = static_cast<ChannelSetupModal*>(lv_event_get_user_data(event));
    if (!view) return;
    const uint32_t selected = lv_dropdown_get_selected(static_cast<lv_obj_t*>(lv_event_get_target(event)));
    if (selected >= view->captureDevices.size()) return;
    const auto& device = view->captureDevices[selected];
    if (audio_service().selectCaptureDevice(device.id)) {
        persistEndpointChoice(true, device.id);
        updateCaptureInputs(view, 0);
    }
}

void selectRenderEndpoint(lv_event_t* event) {
    auto* view = static_cast<ChannelSetupModal*>(lv_event_get_user_data(event));
    if (!view) return;
    const uint32_t selected = lv_dropdown_get_selected(static_cast<lv_obj_t*>(lv_event_get_target(event)));
    if (selected >= view->renderDevices.size()) return;
    const auto& device = view->renderDevices[selected];
    if (audio_service().selectRenderDevice(device.id)) {
        persistEndpointChoice(false, device.id);
        updateOutputRoutes(view, ChannelOutputRoute::MainLR, 0);
    }
}

void commitChannelSetup(ChannelSetupModal* view) {
    if (!view || !view->populated) return;
    const auto snapshot = audio_service().snapshot();
    const int captureIndex = selectedEndpoint(snapshot.captureDevices, snapshot.captureDeviceId);
    if (captureIndex < 0 || snapshot.captureDevices[static_cast<size_t>(captureIndex)].channels == 0) {
        lv_label_set_text(view->error, "Select a capture endpoint with at least one exposed input.");
        return;
    }
    const uint32_t endpointChannels = snapshot.captureDevices[static_cast<size_t>(captureIndex)].channels;
    if (view->format == ChannelFormat::Stereo && endpointChannels < 2) {
        lv_label_set_text(view->error, "Stereo channel requires at least 2 physical capture channels.");
        return;
    }
    ChannelOutputRoute outputRoute;
    int outputRouteIndex = 0;
    if (!decodeSelectedOutput(view, outputRoute, outputRouteIndex)) {
        lv_label_set_text(view->error, "Select an output route exposed by the render endpoint.");
        return;
    }
    const uint32_t input = lv_dropdown_get_selected(view->captureInput);
    const int leftInput = static_cast<int>(input);
    const int rightInput = (view->format == ChannelFormat::Stereo) ? (leftInput + 1) : -1;
    if (leftInput < 0 || static_cast<uint32_t>(leftInput) >= endpointChannels ||
        (view->format == ChannelFormat::Stereo && static_cast<uint32_t>(rightInput) >= endpointChannels)) {
        lv_label_set_text(view->error, "The selected capture input is not available on this endpoint.");
        return;
    }

    auto* channel = view->createNew ? nullptr : mixer_find_channel_by_id(view->channelId);
    const bool inputAlreadyAssigned = std::any_of(g_mixer_state.channels.begin(), g_mixer_state.channels.end(),
        [leftInput, rightInput, channel, view](const ChannelState& existing) {
            if (existing.id == (channel ? channel->id : 0)) return false;
            if (channelUsesPhysicalInput(existing, leftInput)) return true;
            if (view->format == ChannelFormat::Stereo && channelUsesPhysicalInput(existing, rightInput)) return true;
            return false;
        });
    if (inputAlreadyAssigned) {
        lv_label_set_text(view->error,
            view->format == ChannelFormat::Stereo
                ? "One or both channels of this stereo pair are already assigned."
                : "This physical input is already assigned. Choose a different input.");
        return;
    }
    if (!channel && view->createNew) {
        if (static_cast<int>(g_mixer_state.channels.size()) >= MAX_CHANNELS) {
            lv_label_set_text(view->error, "The mixer has reached its strip limit.");
            return;
        }
        create_channel();
        channel = g_mixer_state.channels.get(g_mixer_state.channels.size() - 1);
    }
    if (!channel) {
        lv_label_set_text(view->error, "Failed to resolve target channel.");
        return;
    }
    channel->format = view->format;
    channel->input_source_index = leftInput;
    channel->input_source_index_r = rightInput;
    if (view->format == ChannelFormat::Stereo) {
        std::snprintf(channel->input_label, sizeof(channel->input_label), "IN %02d-%02d [ST]",
                      leftInput + 1, rightInput + 1);
    } else {
        std::snprintf(channel->input_label, sizeof(channel->input_label), "IN %02d [M]", leftInput + 1);
    }
    channel->output_route = outputRoute;
    channel->output_route_index = outputRouteIndex;
    channel->route_to_main = (outputRoute == ChannelOutputRoute::MainLR);
    g_routing_matrix.set_channel_input_source(static_cast<size_t>(channel->id - 1), leftInput);
    g_routing_matrix.set_channel_main_lr(static_cast<size_t>(channel->id - 1), outputRoute == ChannelOutputRoute::MainLR);
    mixer_screen_refresh_channel_by_id(channel->id);
    ui_sync_all_screens();
    lv_obj_delete(view->backdrop);
}

void show_channel_setup_modal(int channelId, bool createNew) {
    if (createNew && static_cast<int>(g_mixer_state.channels.size()) >= MAX_CHANNELS) return;
    auto* view = new ChannelSetupModal{};
    view->createNew = createNew;
    view->channelId = channelId;
    if (!createNew) {
        if (auto* existing = mixer_find_channel_by_id(channelId)) {
            view->format = existing->format;
        }
    }
    auto& service = audio_service();
    service.refreshDevices();
    auto* backdrop = lv_obj_create(lv_layer_top());
    view->backdrop = backdrop;
    g_active_modal = backdrop;
    lv_obj_set_size(backdrop, 1920, 1080);
    lv_obj_center(backdrop);
    lv_obj_remove_flag(backdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(backdrop, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_70, 0);
    lv_obj_set_style_border_width(backdrop, 0, 0);
    auto* dialog = lv_obj_create(backdrop);
    lv_obj_set_size(dialog, 760, 670);
    lv_obj_center(dialog);
    lv_obj_remove_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);
    ui::rack_skin::apply_chassis_panel(dialog, COLOR_ACCENT);
    ui::rack_skin::add_faceplate_corner_screws(dialog, 760, 670, 6);
    lv_obj_set_style_pad_all(dialog, 18, 0);
    instrument_label(dialog, createNew ? "ADD CHANNEL / SELECT FORMAT, INPUT & OUTPUT" : "CHANNEL PATCH / FORMAT, INPUT & OUTPUT", 16, 8,
                     &ac_font_16, COLOR_TEXT);
    instrument_label(dialog, "1. Select MONO (1 input) or STEREO (2 inputs L/R) format before assigning physical channels.", 16, 36,
                     &ac_font_12, COLOR_TEXT2);

    // Format selector row (MONO vs STEREO)
    instrument_label(dialog, "CHANNEL FORMAT (MONO / STEREO)", 16, 62, &ac_font_12, COLOR_SILVER);
    view->formatMonoBtn = instrument_button(dialog, "MONO CHANNEL  (1 INPUT  /  PAN)", 16, 84, 336, 40, nullptr);
    view->formatStereoBtn = instrument_button(dialog, "STEREO CHANNEL  (2 INPUTS L/R  /  BALANCE)", 370, 84, 336, 40, nullptr);
    updateFormatSelectorButtons(view);

    lv_obj_add_event_cb(view->formatMonoBtn, [](lv_event_t* e) {
        auto* v = static_cast<ChannelSetupModal*>(lv_event_get_user_data(e));
        if (!v) return;
        v->format = ChannelFormat::Mono;
        updateFormatSelectorButtons(v);
        updateCaptureInputs(v, 0);
    }, LV_EVENT_CLICKED, view);

    lv_obj_add_event_cb(view->formatStereoBtn, [](lv_event_t* e) {
        auto* v = static_cast<ChannelSetupModal*>(lv_event_get_user_data(e));
        if (!v) return;
        v->format = ChannelFormat::Stereo;
        updateFormatSelectorButtons(v);
        updateCaptureInputs(v, 0);
    }, LV_EVENT_CLICKED, view);

    const char* labels[] = {"CAPTURE DEVICE / AVAILABLE INPUTS", "CHANNEL INPUT SOURCE(S)", "RENDER DEVICE / AVAILABLE OUTPUTS", "OUTPUT ROUTE"};
    lv_obj_t** controls[] = {&view->captureDevice, &view->captureInput, &view->renderDevice, &view->outputRoute};
    const int rowY[] = {136, 224, 312, 400};
    for (size_t row = 0; row < 4; ++row) {
        instrument_label(dialog, labels[row], 16, rowY[row], &ac_font_12, COLOR_TEXT2);
        *controls[row] = lv_dropdown_create(dialog);
        lv_obj_set_pos(*controls[row], 16, rowY[row] + 22);
        lv_obj_set_size(*controls[row], 690, 46);
        lv_obj_set_style_bg_color(*controls[row], COLOR_CONTROL, LV_PART_MAIN);
        lv_obj_set_style_border_color(*controls[row], COLOR_EDGE, LV_PART_MAIN);
        lv_obj_set_style_border_width(*controls[row], 1, LV_PART_MAIN);
        lv_obj_set_style_text_color(*controls[row], COLOR_TEXT, LV_PART_MAIN);
        lv_obj_set_style_text_font(*controls[row], &ac_font_12, LV_PART_MAIN);

        lv_obj_t* list = lv_dropdown_get_list(*controls[row]);
        if (list) {
            lv_obj_set_style_bg_color(list, lv_color_hex(0x181C20), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_color(list, COLOR_EDGE, LV_PART_MAIN);
            lv_obj_set_style_border_width(list, 1, LV_PART_MAIN);
            lv_obj_set_style_radius(list, 3, LV_PART_MAIN);
            lv_obj_set_style_text_color(list, COLOR_TEXT, LV_PART_MAIN);
            lv_obj_set_style_text_font(list, &ac_font_12, LV_PART_MAIN);
            lv_obj_set_style_max_height(list, 260, LV_PART_MAIN);
            lv_obj_set_style_pad_all(list, 4, LV_PART_MAIN);
            lv_obj_set_style_bg_color(list, lv_color_hex(0x283848), LV_PART_SELECTED);
            lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_SELECTED);
            lv_obj_set_style_text_color(list, COLOR_CYAN, LV_PART_SELECTED);
        }
        lv_obj_add_event_cb(*controls[row], [](lv_event_t* e) {
            auto* d = static_cast<lv_obj_t*>(lv_event_get_target(e));
            auto* l = lv_dropdown_get_list(d);
            if (l) {
                lv_obj_set_parent(l, lv_layer_top());
                lv_obj_move_to_index(l, -1);
                lv_obj_set_style_bg_color(l, lv_color_hex(0x181C20), LV_PART_MAIN);
                lv_obj_set_style_bg_opa(l, LV_OPA_COVER, LV_PART_MAIN);
            }
        }, LV_EVENT_READY, nullptr);
    }
    lv_dropdown_set_options(view->captureDevice, "Loading capture devices...");
    lv_dropdown_set_options(view->captureInput, "Waiting for endpoint channels...");
    lv_dropdown_set_options(view->renderDevice, "Loading render devices...");
    lv_dropdown_set_options(view->outputRoute, "Waiting for endpoint channels...");
    view->detail = instrument_label(dialog, "Waiting for audio endpoint enumeration...", 16, 496,
                                    &ac_font_12, COLOR_GREEN);
    view->error = instrument_label(dialog, "", 16, 522, &ac_font_12, COLOR_RED);
    lv_obj_set_width(view->error, 690);
    auto* cancel = instrument_button(dialog, "CANCEL", 370, 576, 150, 42, nullptr);
    auto* confirm = instrument_button(dialog, createNew ? "ADD CHANNEL" : "APPLY PATCH", 536, 576, 170, 42, nullptr);
    lv_obj_add_event_cb(cancel, [](lv_event_t* event) {
        auto* v = static_cast<ChannelSetupModal*>(lv_event_get_user_data(event));
        if (v) lv_obj_delete(v->backdrop);
    }, LV_EVENT_CLICKED, view);
    lv_obj_add_event_cb(confirm, [](lv_event_t* event) {
        commitChannelSetup(static_cast<ChannelSetupModal*>(lv_event_get_user_data(event)));
    }, LV_EVENT_CLICKED, view);
    lv_obj_add_event_cb(view->captureDevice, selectCaptureEndpoint, LV_EVENT_VALUE_CHANGED, view);
    lv_obj_add_event_cb(view->renderDevice, selectRenderEndpoint, LV_EVENT_VALUE_CHANGED, view);
    lv_obj_add_event_cb(backdrop, [](lv_event_t* event) {
        g_active_modal = nullptr;
        auto* v = static_cast<ChannelSetupModal*>(lv_event_get_user_data(event));
        if (v) {
            if (v->refreshTimer) lv_timer_delete(v->refreshTimer);
            delete v;
        }
    }, LV_EVENT_DELETE, view);
    view->refreshTimer = lv_timer_create([](lv_timer_t* timer) {
        populateChannelSetupModal(static_cast<ChannelSetupModal*>(lv_timer_get_user_data(timer)));
    }, 100, view);
}

void add_channel() { show_channel_setup_modal(0, true); }

void create_channel() {
    if ((int)g_mixer_state.channels.size() >= MAX_CHANNELS) return;

    ChannelState ch{};
    const int new_index = (int)g_mixer_state.channels.size();
    for (int candidate = 1; candidate <= MAX_CHANNELS; ++candidate) {
        if (!mixer_find_channel_by_id(candidate)) {
            ch.id = candidate;
            break;
        }
    }
    if (ch.id <= 0) return;
    std::snprintf(ch.name, sizeof(ch.name), "CH %02d", new_index + 1);
    std::snprintf(ch.input_label, sizeof(ch.input_label), "UNASSIGNED");
    ch.format = ChannelFormat::Mono;
    ch.input_source_index = -1;
    ch.input_source_index_r = -1;
    ch.output_route = ChannelOutputRoute::MainLR;
    ch.output_route_index = 0;
    ch.gain = 0.5f;
    ch.pan = 0.5f;
    ch.fader = 0.86f;
    ch.mute = false;
    ch.solo = false;
    ch.select = false;
    ch.clip = false;
    ch.peak_l = ch.peak_r = ch.rms_l = ch.rms_r = -90.0f;
    ch.peak_hold_l = ch.peak_hold_r = -90.0f;

    g_mixer_state.channels.push_back(ch);
    createChannelStrip(new_index);
    if (g_add_label) lv_label_set_text(g_add_label, "ADD CH");
    if (g_empty_hint) lv_obj_add_flag(g_empty_hint, LV_OBJ_FLAG_HIDDEN);
    select_channel(new_index);

    // Scroll to show the newly added strip
    if (g_scroll_container) {
        const int target_x = 8 + new_index * (STRIP_WIDTH + STRIP_GAP);
        lv_obj_scroll_to_x(g_scroll_container, target_x - 1200, LV_ANIM_OFF);
    }
}

StripUI build_strip_ui(lv_obj_t* parent, int index, int x, int y, int width, bool is_master) {
    ChannelState* state = is_master ? &g_mixer_state.master : &g_mixer_state.channels[index];
    StripUI s{};
    s.index = is_master ? -1 : index;
    s.state = state;

    const char* header_title = is_master
        ? "STEREO MASTER"
        : (state->format == ChannelFormat::Stereo ? "STEREO INPUT" : "MONO INPUT");
    auto* p = instrument_panel(parent, x, y, width, STRIP_HEIGHT, nullptr);
    s.panel = p;
    lv_obj_add_flag(p, LV_OBJ_FLAG_CLICKABLE);
    ui::rack_skin::apply_channel_strip_panel(p, is_master ? (g_mixer_state.selected_channel == -1) : (g_mixer_state.selected_channel == index));
    ui::rack_skin::create_rack_screw(p, 4, 4);
    ui::rack_skin::create_rack_screw(p, width - 18, 4);
    ui::rack_skin::create_rack_screw(p, 4, STRIP_HEIGHT - 18);
    ui::rack_skin::create_rack_screw(p, width - 18, STRIP_HEIGHT - 18);

    // Section title
    auto* sec_lbl = instrument_label(p, header_title, 12, 8, &ac_font_12, COLOR_TEXT2);
    s.sec_label = sec_lbl;

    // Click channel body / header to select
    lv_obj_add_event_cb(p, [](lv_event_t* e) {
        if (lv_event_get_target(e) != lv_event_get_current_target(e)) return;
        const int idx = (int)(intptr_t)lv_event_get_user_data(e);
        select_channel(idx);
    }, LV_EVENT_CLICKED, (void*)(intptr_t)s.index);

    // Title label
    char text[64];
    if (is_master) {
        std::snprintf(text, sizeof(text), "MAIN L/R");
    } else {
        std::snprintf(text, sizeof(text), "CH %02d   %s", index + 1, state->name);
    }
    s.title_label = instrument_label(p, text, 8, 26, &ac_font_16, COLOR_TEXT);
    lv_obj_add_flag(s.title_label, LV_OBJ_FLAG_CLICKABLE);

    // Clicking channel name selects channel; double clicking name triggers rename
    lv_obj_add_event_cb(s.title_label, [](lv_event_t* e) {
        const int idx = (int)(intptr_t)lv_event_get_user_data(e);
        if (idx >= 0) {
            select_channel(idx);
        } else {
            select_channel(-1);
        }
    }, LV_EVENT_CLICKED, (void*)(intptr_t)s.index);

    if (!is_master) {
        lv_obj_add_event_cb(s.title_label, [](lv_event_t* e) {
            const int idx = (int)(intptr_t)lv_event_get_user_data(e);
            if (idx >= 0) show_rename_modal(idx);
        }, LV_EVENT_DOUBLE_CLICKED, (void*)(intptr_t)s.index);
    }

    // Clip indicator
    s.clip_flag = instrument_label(p, state->clip ? "CLIP" : "OK", width - 46, 28, &ac_font_12, state->clip ? COLOR_RED : COLOR_GREEN);
    struct ClipBinding { bool* clip; lv_obj_t* label; lv_timer_t* timer; };
    auto* cb = new ClipBinding{&state->clip, s.clip_flag, nullptr};
    cb->timer = lv_timer_create([](lv_timer_t* t) {
        auto* b = static_cast<ClipBinding*>(lv_timer_get_user_data(t));
        if (!b || !b->clip || !b->label) return;
        lv_label_set_text(b->label, *b->clip ? "CLIP" : "OK");
        lv_obj_set_style_text_color(b->label, *b->clip ? COLOR_RED : COLOR_GREEN, 0);
    }, 100, cb);
    lv_obj_add_event_cb(s.clip_flag, [](lv_event_t* e) {
        auto* b = static_cast<ClipBinding*>(lv_event_get_user_data(e));
        if (b) {
            if (b->timer) lv_timer_delete(b->timer);
            delete b;
        }
    }, LV_EVENT_DELETE, cb);

    // Input label (clickable to open setup/patching dialog)
    if (is_master) {
        s.input_label = instrument_label(p, state->input_label, 8, 44, &ac_font_12, COLOR_SILVER);
    } else {
        s.input_label = instrument_label(p, state->input_label, 8, 48, &ac_font_12, COLOR_SILVER);
        lv_obj_add_flag(s.input_label, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(s.input_label, [](lv_event_t* e) {
            const int chId = (int)(intptr_t)lv_event_get_user_data(e);
            if (chId > 0) mixer_screen_show_channel_setup(chId);
        }, LV_EVENT_CLICKED, (void*)(intptr_t)state->id);
    }

    // Action buttons on input channels (REN & DEL)
    if (!is_master) {
        s.ren_btn = lv_button_create(p);
        lv_obj_set_pos(s.ren_btn, 108, 46);
        lv_obj_set_size(s.ren_btn, 40, 22);
        ui::rack_skin::apply_hardware_button(s.ren_btn, false, COLOR_EDGE, COLOR_CONTROL, lv_color_hex(0x181D23));
        auto* ren_lbl = lv_label_create(s.ren_btn);
        lv_label_set_text(ren_lbl, "REN");
        lv_obj_center(ren_lbl);
        lv_obj_set_style_text_font(ren_lbl, &ac_font_12, 0);
        lv_obj_set_style_text_color(ren_lbl, COLOR_TEXT2, 0);
        lv_obj_add_event_cb(s.ren_btn, [](lv_event_t* e) {
            const int idx = (int)(intptr_t)lv_event_get_user_data(e);
            show_rename_modal(idx);
        }, LV_EVENT_CLICKED, (void*)(intptr_t)index);

        s.del_btn = lv_button_create(p);
        lv_obj_set_pos(s.del_btn, 152, 46);
        lv_obj_set_size(s.del_btn, 40, 22);
        ui::rack_skin::apply_hardware_button(s.del_btn, false, lv_color_hex(0x7A2424), lv_color_hex(0x3B1515), lv_color_hex(0x281010));
        auto* del_lbl = lv_label_create(s.del_btn);
        lv_label_set_text(del_lbl, "DEL");
        lv_obj_center(del_lbl);
        lv_obj_set_style_text_font(del_lbl, &ac_font_12, 0);
        lv_obj_set_style_text_color(del_lbl, lv_color_hex(0xE26A6A), 0);
        lv_obj_add_event_cb(s.del_btn, [](lv_event_t* e) {
            const int idx = (int)(intptr_t)lv_event_get_user_data(e);
            show_delete_modal(idx);
        }, LV_EVENT_CLICKED, (void*)(intptr_t)index);
    }

    // Meters section
    if (is_master) {
        // Main Level Meter Area (Left Column: x=12..98, width=86)
        s.master_meter = instrument_master_meter(
            p,
            &state->peak_l,
            &state->peak_r,
            &state->clip,
            12, 64, 86, 190,
            &state->peak_hold_l,
            &state->peak_hold_r,
            &state->rms_l,
            &state->rms_r
        );
        s.meter_l = nullptr;
        s.meter_r = nullptr;
        s.meter_l_ftr = instrument_label(p, "L", 22, 258, &ac_font_12, COLOR_TEXT2);
        s.meter_r_ftr = instrument_label(p, "R", 83, 258, &ac_font_12, COLOR_TEXT2);

        // Master Controls Area (Right Column: x=111..195, width=84)
        s.gain_knob = instrument_knob(p, "GAIN", &state->gain, 0.0f, 1.0f, 0.5f, "dB", 111, 66, 52);

        // Limiter enable button (clearly separated from meter & knob)
        s.limiter_btn = instrument_button(p, "LIMITER", 111, 160, 84, 24, &state->limiter_enabled);

        // Limiter Reduction Section
        auto* lim_red_lbl = instrument_label(p, "LIMITER GR", 111, 189, &ac_font_12, COLOR_TEXT2);
        lv_obj_set_width(lim_red_lbl, 84);
        lv_obj_set_style_text_align(lim_red_lbl, LV_TEXT_ALIGN_CENTER, 0);

        // Dedicated Horizontal Limiter Gain Reduction Meter Bar
        s.gr_meter = instrument_limiter_gr_meter(p, &state->limiter_gr_db, &state->limiter_enabled, 111, 205, 84, 12);

        // Digital Limiter status readout badge
        auto* lim_lbl = instrument_label(p, "LIM BYP", 111, 223, &ac_font_12, COLOR_TEXT2);
        s.limiter_badge = lim_lbl;
        lv_obj_set_width(lim_lbl, 84);
        lv_obj_set_style_text_align(lim_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_bg_color(lim_lbl, lv_color_hex(0x0B0E13), 0);
        lv_obj_set_style_bg_opa(lim_lbl, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(lim_lbl, 1, 0);
        lv_obj_set_style_border_color(lim_lbl, lv_color_hex(0x27303B), 0);
        lv_obj_set_style_radius(lim_lbl, 2, 0);
        lv_obj_set_style_pad_ver(lim_lbl, 1, 0);

        struct LimStripBinding { ChannelState* st; lv_obj_t* label; lv_timer_t* timer; };
        auto* lsb = new LimStripBinding{state, lim_lbl, nullptr};
        lsb->timer = lv_timer_create([](lv_timer_t* t) {
            auto* b = static_cast<LimStripBinding*>(lv_timer_get_user_data(t));
            if (!b || !b->st || !b->label) return;
            if (!b->st->limiter_enabled) {
                lv_label_set_text(b->label, "LIM BYP");
                lv_obj_set_style_text_color(b->label, COLOR_TEXT2, 0);
            } else {
                lv_label_set_text_fmt(b->label, "GR %.1fdB", b->st->limiter_gr_db);
                lv_obj_set_style_text_color(b->label, b->st->limiter_gr_db < -0.5f ? COLOR_ORANGE : COLOR_GREEN, 0);
            }
        }, 100, lsb);
        lv_obj_add_event_cb(lim_lbl, [](lv_event_t* e) {
            auto* b = static_cast<LimStripBinding*>(lv_event_get_user_data(e));
            if (b) {
                if (b->timer) lv_timer_delete(b->timer);
                delete b;
            }
        }, LV_EVENT_DELETE, lsb);
    } else {
        s.current_format = state->format;

        if (state->format == ChannelFormat::Mono) {
            // Mono Presentation: Compact single meter alongside Compressor GR meter
            s.gr_hdr = instrument_label(p, "GR", 20, 70, &ac_font_12, COLOR_TEXT2);
            s.gr_meter = instrument_gr_meter(p, &state->comp_gr, 20, 86, 16, 142, &state->comp_bypass, false);
            s.gr_ftr = instrument_label(p, "GR", 20, 232, &ac_font_12, COLOR_TEXT2);

            s.peak_hdr = instrument_label(p, "PEAK", 48, 70, &ac_font_12, COLOR_TEXT2);
            s.meter_l = instrument_meter(p, &state->peak_l, &state->clip, 48, 86, 24, 142, &state->peak_hold_l, &state->rms_l);
            s.meter_r = nullptr;
            s.meter_l_ftr = instrument_label(p, "LEVEL", 42, 232, &ac_font_12, COLOR_TEXT2);
            s.meter_r_ftr = nullptr;
        } else {
            // Stereo Presentation: Dual compact meters side-by-side alongside Compressor GR meter
            s.gr_hdr = instrument_label(p, "GR", 14, 70, &ac_font_12, COLOR_TEXT2);
            s.gr_meter = instrument_gr_meter(p, &state->comp_gr, 14, 86, 16, 142, &state->comp_bypass, false);
            s.gr_ftr = instrument_label(p, "GR", 14, 232, &ac_font_12, COLOR_TEXT2);

            s.peak_hdr = instrument_label(p, "PEAK", 46, 70, &ac_font_12, COLOR_TEXT2);
            s.meter_l = instrument_meter(p, &state->peak_l, &state->clip, 36, 86, 24, 142, &state->peak_hold_l, &state->rms_l);
            s.meter_r = instrument_meter(p, &state->peak_r, &state->clip, 64, 86, 24, 142, &state->peak_hold_r, &state->rms_r);
            s.meter_l_ftr = instrument_label(p, "L", 44, 232, &ac_font_12, COLOR_TEXT2);
            s.meter_r_ftr = instrument_label(p, "R", 72, 232, &ac_font_12, COLOR_TEXT2);
        }

        // Gain dial centered in the reclaimed control space (x=104)
        s.gain_knob = instrument_knob(p, "GAIN", &state->gain, 0.0f, 1.0f, 0.5f, "dB", 104, 80, 64);

        auto* fmt_lbl = instrument_label(p, state->format == ChannelFormat::Stereo ? "STEREO" : "MONO",
                                         104, 202, &ac_font_12,
                                         state->format == ChannelFormat::Stereo ? COLOR_SILVER : COLOR_TEXT2);
        s.fmt_badge = fmt_lbl;
        lv_obj_set_width(fmt_lbl, 84);
        lv_obj_set_style_text_align(fmt_lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_bg_color(fmt_lbl, lv_color_hex(0x0B0E13), 0);
        lv_obj_set_style_bg_opa(fmt_lbl, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(fmt_lbl, 1, 0);
        lv_obj_set_style_border_color(fmt_lbl, lv_color_hex(0x27303B), 0);
        lv_obj_set_style_radius(fmt_lbl, 2, 0);
        lv_obj_set_style_pad_ver(fmt_lbl, 2, 0);

        struct FmtStripBinding { int index; ChannelState* st; lv_obj_t* label; lv_timer_t* timer; };
        auto* fsb = new FmtStripBinding{index, state, fmt_lbl, nullptr};
        fsb->timer = lv_timer_create([](lv_timer_t* t) {
            auto* b = static_cast<FmtStripBinding*>(lv_timer_get_user_data(t));
            if (!b || !b->st || !b->label) return;
            const bool isSt = (b->st->format == ChannelFormat::Stereo);
            lv_label_set_text(b->label, isSt ? "STEREO" : "MONO");
            lv_obj_set_style_text_color(b->label, isSt ? COLOR_SILVER : COLOR_TEXT2, 0);
            if (b->index >= 0 && b->index < (int)g_input_strips.size()) {
                syncChannelMeterFormat(g_input_strips[b->index]);
            }
        }, 200, fsb);
        lv_obj_add_event_cb(fmt_lbl, [](lv_event_t* e) {
            auto* b = static_cast<FmtStripBinding*>(lv_event_get_user_data(e));
            if (b) {
                if (b->timer) lv_timer_delete(b->timer);
                delete b;
            }
        }, LV_EVENT_DELETE, fsb);
    }

    // Fader scale markings
    instrument_label(p, "+10", 10, 284, &ac_font_12, COLOR_TEXT2);
    instrument_label(p, "0", 17, 338, &ac_font_12, COLOR_SILVER);
    instrument_label(p, "-6", 13, 402, &ac_font_12, COLOR_TEXT2);
    instrument_label(p, "-12", 10, 466, &ac_font_12, COLOR_TEXT2);
    instrument_label(p, "-24", 10, 532, &ac_font_12, COLOR_TEXT2);
    instrument_label(p, "-40", 10, 596, &ac_font_12, COLOR_TEXT2);
    instrument_label(p, "-60", 10, 660, &ac_font_12, COLOR_TEXT2);
    instrument_label(p, "-inf", 10, 688, &ac_font_12, COLOR_TEXT2);

    // Tactile vertical fader
    auto* fader = lv_obj_create(p);
    s.fader_obj = fader;
    lv_obj_set_pos(fader, 56, 282);
    lv_obj_set_size(fader, 56, 410);
    lv_obj_remove_flag(fader, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(fader, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
    lv_obj_remove_flag(fader, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
    lv_obj_add_flag(fader, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(fader, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_set_style_bg_opa(fader, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(fader, 0, 0);
    lv_obj_set_style_pad_all(fader, 0, 0);
    lv_obj_set_user_data(fader, &state->fader);

    lv_obj_add_event_cb(fader, [](lv_event_t* ev) {
        auto* v = static_cast<float*>(lv_obj_get_user_data(static_cast<lv_obj_t*>(lv_event_get_target(ev))));
        if (!v) return;
        const lv_event_code_t code = lv_event_get_code(ev);

        if (code == LV_EVENT_DRAW_MAIN) {
            lv_layer_t* layer = lv_event_get_layer(ev);
            lv_area_t a{};
            lv_obj_get_coords(static_cast<lv_obj_t*>(lv_event_get_target(ev)), &a);
            const int cx = (a.x1 + a.x2) / 2;
            const int total_h = lv_area_get_height(&a);

            // 1. Recessed track slot
            lv_draw_rect_dsc_t track;
            lv_draw_rect_dsc_init(&track);
            track.bg_color = lv_color_hex(0x0A0D10);
            track.bg_opa = LV_OPA_COVER;
            track.border_color = lv_color_hex(0x20262C);
            track.border_width = 1;
            track.radius = 1;
            lv_area_t tr{cx - 4, a.y1 + 4, cx + 4, a.y2 - 4};
            lv_draw_rect(layer, &track, &tr);

            // Inner slot shadow
            lv_draw_line_dsc_t slot_sh;
            lv_draw_line_dsc_init(&slot_sh);
            slot_sh.color = lv_color_hex(0x050709);
            slot_sh.width = 1;
            slot_sh.p1.x = cx - 3; slot_sh.p1.y = a.y1 + 5;
            slot_sh.p2.x = cx - 3; slot_sh.p2.y = a.y2 - 5;
            lv_draw_line(layer, &slot_sh);

            // 2. 0 dB reference line on track (at value = 0.86f)
            const int y_0db = a.y2 - (int)(0.86f * total_h);
            lv_draw_line_dsc_t ref_0db;
            lv_draw_line_dsc_init(&ref_0db);
            ref_0db.color = COLOR_SILVER;
            ref_0db.width = 2;
            ref_0db.p1.x = cx - 8; ref_0db.p1.y = y_0db;
            ref_0db.p2.x = cx + 8; ref_0db.p2.y = y_0db;
            lv_draw_line(layer, &ref_0db);

            // 3. Fader cap position
            const int cap_y = a.y2 - (int)(*v * total_h);

            // Subtle active track indication below cap
            if (cap_y < a.y2 - 6) {
                lv_draw_rect_dsc_t act_slot;
                lv_draw_rect_dsc_init(&act_slot);
                act_slot.bg_color = lv_color_hex(0x1A2530);
                act_slot.bg_opa = LV_OPA_COVER;
                lv_area_t as{cx - 2, cap_y + 14, cx + 2, a.y2 - 5};
                lv_draw_rect(layer, &act_slot, &as);
            }

            // 4. Multi-layered tactile hardware fader cap
            // Bottom shadow
            lv_draw_rect_dsc_t sh;
            lv_draw_rect_dsc_init(&sh);
            sh.bg_color = lv_color_hex(0x06080A);
            sh.bg_opa = LV_OPA_60;
            lv_area_t sha{cx - 18, cap_y + 14, cx + 18, cap_y + 18};
            lv_draw_rect(layer, &sh, &sha);

            // Outer cap body
            lv_draw_rect_dsc_t cap;
            lv_draw_rect_dsc_init(&cap);
            cap.bg_color = lv_color_hex(0x22272D);
            cap.bg_opa = LV_OPA_COVER;
            cap.border_color = lv_color_hex(0x3C454E);
            cap.border_width = 1;
            cap.radius = 2;
            lv_area_t ca{cx - 20, cap_y - 15, cx + 20, cap_y + 15};
            lv_draw_rect(layer, &cap, &ca);

            // Top highlight bevel line
            lv_draw_line_dsc_t top_hi;
            lv_draw_line_dsc_init(&top_hi);
            top_hi.color = lv_color_hex(0x525E6A);
            top_hi.width = 1;
            top_hi.p1.x = cx - 18; top_hi.p1.y = cap_y - 14;
            top_hi.p2.x = cx + 18; top_hi.p2.y = cap_y - 14;
            lv_draw_line(layer, &top_hi);

            // Bottom shadow bevel line
            lv_draw_line_dsc_t bot_sh;
            lv_draw_line_dsc_init(&bot_sh);
            bot_sh.color = lv_color_hex(0x101316);
            bot_sh.width = 1;
            bot_sh.p1.x = cx - 18; bot_sh.p1.y = cap_y + 14;
            bot_sh.p2.x = cx + 18; bot_sh.p2.y = cap_y + 14;
            lv_draw_line(layer, &bot_sh);

            // Recessed center finger grip
            lv_draw_rect_dsc_t grip;
            lv_draw_rect_dsc_init(&grip);
            grip.bg_color = lv_color_hex(0x14171A);
            grip.bg_opa = LV_OPA_COVER;
            lv_area_t ga{cx - 14, cap_y - 6, cx + 14, cap_y + 6};
            lv_draw_rect(layer, &grip, &ga);

            // Center position indicator line (soft silver with white center)
            lv_draw_rect_dsc_t pos_line;
            lv_draw_rect_dsc_init(&pos_line);
            pos_line.bg_color = COLOR_SILVER;
            pos_line.bg_opa = LV_OPA_COVER;
            lv_area_t la{cx - 15, cap_y - 1, cx + 15, cap_y + 1};
            lv_draw_rect(layer, &pos_line, &la);

            lv_draw_rect_dsc_t center_pip;
            lv_draw_rect_dsc_init(&center_pip);
            center_pip.bg_color = COLOR_TEXT;
            center_pip.bg_opa = LV_OPA_COVER;
            lv_area_t pa{cx - 2, cap_y - 1, cx + 2, cap_y + 1};
            lv_draw_rect(layer, &center_pip, &pa);

            return;
        }

        if (code == LV_EVENT_PRESSING) {
            auto* indev = lv_indev_active();
            if (indev) {
                lv_point_t d{};
                lv_indev_get_vect(indev, &d);
                *v = std::clamp(*v - d.y / 410.0f, 0.0f, 1.0f);
                lv_obj_invalidate(static_cast<lv_obj_t*>(lv_event_get_target(ev)));
            }
        } else if (code == LV_EVENT_PRESSED) {
            auto* indev = lv_indev_active();
            if (indev) {
                lv_point_t p_pt{};
                lv_indev_get_point(indev, &p_pt);
                lv_area_t a{};
                auto* target = static_cast<lv_obj_t*>(lv_event_get_target(ev));
                lv_obj_get_coords(target, &a);
                const int32_t h = lv_area_get_height(&a);
                if (h > 0) {
                    const float new_v = (float)(a.y2 - p_pt.y) / (float)h;
                    *v = std::clamp(new_v, 0.0f, 1.0f);
                    lv_obj_invalidate(target);
                }
            }
        } else if (code == LV_EVENT_DOUBLE_CLICKED) {
            *v = 0.86f; // Reset to 0.0 dB
            lv_obj_invalidate(static_cast<lv_obj_t*>(lv_event_get_target(ev)));
        }
    }, LV_EVENT_ALL, nullptr);

    // Fader readout label
    auto* value_label = lv_label_create(p);
    s.fader_label = value_label;
    lv_label_set_text(value_label, "0.0 dB");
    lv_obj_align(value_label, LV_ALIGN_TOP_MID, 15, 696);
    lv_obj_set_style_text_font(value_label, &ac_font_20, 0);
    lv_obj_set_style_text_color(value_label, COLOR_TEXT, 0);

    struct FaderTimerBinding { float* fader; lv_obj_t* label; lv_timer_t* timer; };
    auto* ftb = new FaderTimerBinding{&state->fader, value_label, nullptr};
    ftb->timer = lv_timer_create([](lv_timer_t* t) {
        auto* b = static_cast<FaderTimerBinding*>(lv_timer_get_user_data(t));
        if (!b || !b->fader || !b->label) return;
        const float val = *b->fader;
        const float db = val <= 0.86f ? -60.0f + val * (60.0f / 0.86f) : (val - 0.86f) * (10.0f / 0.14f);
        if (val <= 0.005f) {
            lv_label_set_text(b->label, "-inf dB");
        } else if (std::abs(db) < 0.05f) {
            lv_label_set_text(b->label, "0.0 dB");
        } else {
            lv_label_set_text_fmt(b->label, "%+.1f dB", db);
        }
    }, 100, ftb);

    lv_obj_add_event_cb(value_label, [](lv_event_t* e) {
        auto* b = static_cast<FaderTimerBinding*>(lv_event_get_user_data(e));
        if (b) {
            if (b->timer) {
                lv_timer_delete(b->timer);
                b->timer = nullptr;
            }
            delete b;
        }
    }, LV_EVENT_DELETE, ftb);

    // Pan / Balance control
    instrument_knob(p, (is_master || state->format == ChannelFormat::Stereo) ? "BAL" : "PAN",
                    &state->pan, 0.0f, 1.0f, 0.5f, "L/C/R", 24, 728, 64);

    // MUTE & SOLO
    instrument_button(p, "MUTE", 112, 750, width - 120, 38, &state->mute);
    instrument_button(p, "SOLO", 112, 796, width - 120, 38, &state->solo);

    // Bottom section: SELECT button and Master actions
    if (!is_master) {
        s.select_btn = instrument_button(p, "SELECT", 8, 851, width - 16, 42, nullptr);
        lv_obj_add_event_cb(s.select_btn, [](lv_event_t* e) {
            const int idx = (int)(intptr_t)lv_event_get_user_data(e);
            select_channel(idx);
        }, LV_EVENT_CLICKED, (void*)(intptr_t)index);
    } else {
        // Master strip bottom: SELECT button & + ADD CH button
        s.select_btn = instrument_button(p, "SELECT", 8, 851, 92, 42, nullptr);
        lv_obj_add_event_cb(s.select_btn, [](lv_event_t* e) {
            (void)e;
            select_channel(-1);
        }, LV_EVENT_CLICKED, nullptr);

        auto* add_btn = instrument_button(p, "+ ADD", 106, 851, 92, 42, nullptr);
        lv_obj_add_event_cb(add_btn, [](lv_event_t* e) {
            (void)e;
            add_channel();
        }, LV_EVENT_CLICKED, nullptr);
    }

    update_strip_visual_selection(s, is_master ? (g_mixer_state.selected_channel == -1) : (g_mixer_state.selected_channel == index));

    return s;
}

} // namespace

lv_obj_t* mixer_screen_create(void) {
    g_input_strips.clear();
    g_empty_container = nullptr;
    g_add_card = nullptr;
    g_add_label = nullptr;
    g_empty_hint = nullptr;

    auto* screen = lv_obj_create(lv_screen_active());
    g_screen = screen;
    lv_obj_set_pos(screen, 0, 0);
    lv_obj_set_size(screen, 1920, 1080);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, COLOR_BG, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);

    lv_obj_add_event_cb(screen, [](lv_event_t*) {
        g_screen = nullptr;
        g_scroll_container = nullptr;
        g_empty_container = nullptr;
        g_add_card = nullptr;
        g_add_label = nullptr;
        g_empty_hint = nullptr;
        g_input_strips.clear();
        g_master_strip = {};
    }, LV_EVENT_DELETE, nullptr);

    // Horizontal scroll container for input channels
    auto* scroll = lv_obj_create(screen);
    g_scroll_container = scroll;
    lv_obj_set_pos(scroll, 8, 52);
    lv_obj_set_size(scroll, 1686, STRIP_HEIGHT + 4);
    lv_obj_set_scroll_dir(scroll, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_opa(scroll, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(scroll, 0, 0);
    lv_obj_set_style_pad_all(scroll, 0, 0);

    // Build input channel strips inside scroll container
    const int num_ch = (int)g_mixer_state.channels.size();
    for (int i = 0; i < num_ch; ++i) {
        createChannelStrip(i);
    }

    // 1. Dedicated Empty Console Faceplate Panel
    const int ec_width = 640;
    const int ec_height = 420;
    auto* empty_container = lv_obj_create(scroll);
    g_empty_container = empty_container;
    lv_obj_set_size(empty_container, ec_width, ec_height);
    lv_obj_set_pos(empty_container, (1686 - ec_width) / 2, (STRIP_HEIGHT + 4 - ec_height) / 2);
    lv_obj_remove_flag(empty_container, LV_OBJ_FLAG_SCROLLABLE);
    ui::rack_skin::apply_faceplate_panel(empty_container, false);
    ui::rack_skin::add_faceplate_corner_screws(empty_container, ec_width, ec_height, 6);
    lv_obj_set_style_pad_all(empty_container, 28, 0);

    // Title: NO CHANNELS CONFIGURED
    auto* empty_title = lv_label_create(empty_container);
    lv_label_set_text(empty_title, "NO CHANNELS CONFIGURED");
    lv_obj_align(empty_title, LV_ALIGN_TOP_MID, 0, 14);
    lv_obj_set_style_text_font(empty_title, &ac_font_20, 0);
    lv_obj_set_style_text_color(empty_title, COLOR_TEXT, 0);

    // Subtitle
    auto* empty_sub = lv_label_create(empty_container);
    lv_label_set_text(empty_sub, "The mixer starts empty. Add your first input channel to configure physical\nrouting, preamps, and channel DSP processing.");
    lv_obj_align(empty_sub, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_set_style_text_font(empty_sub, &ac_font_12, 0);
    lv_obj_set_style_text_align(empty_sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(empty_sub, COLOR_TEXT2, 0);

    // Primary Action Button: + ADD CHANNEL
    auto* add_ch_btn = lv_button_create(empty_container);
    lv_obj_set_size(add_ch_btn, 280, 52);
    lv_obj_align(add_ch_btn, LV_ALIGN_TOP_MID, 0, 116);
    ui::rack_skin::apply_hardware_button(add_ch_btn, false, COLOR_SELECTED_BORDER, COLOR_RAISED_PANEL, COLOR_CONTROL);
    auto* add_ch_lbl = lv_label_create(add_ch_btn);
    lv_label_set_text(add_ch_lbl, "+ ADD CHANNEL");
    lv_obj_center(add_ch_lbl);
    lv_obj_set_style_text_font(add_ch_lbl, &ac_font_16, 0);
    lv_obj_set_style_text_color(add_ch_lbl, COLOR_SILVER, 0);
    lv_obj_add_event_cb(add_ch_btn, [](lv_event_t*) {
        add_channel();
    }, LV_EVENT_CLICKED, nullptr);

    // Divider line
    auto* divider = lv_obj_create(empty_container);
    lv_obj_set_size(divider, 560, 1);
    lv_obj_align(divider, LV_ALIGN_TOP_MID, 0, 200);
    lv_obj_set_style_bg_color(divider, COLOR_EDGE, 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(divider, 0, 0);

    // Secondary Extension Points Header
    auto* ext_hdr = lv_label_create(empty_container);
    lv_label_set_text(ext_hdr, "CONSOLE ARCHITECTURE EXTENSION POINTS");
    lv_obj_align(ext_hdr, LV_ALIGN_TOP_MID, 0, 222);
    lv_obj_set_style_text_font(ext_hdr, &ac_font_12, 0);
    lv_obj_set_style_text_color(ext_hdr, COLOR_TEXT2, 0);

    // Secondary Buttons row: + ADD AUX, + ADD GROUP BUS, + ADD DCA
    const int btn_y = 256;
    auto* add_aux_btn = lv_button_create(empty_container);
    lv_obj_set_size(add_aux_btn, 170, 42);
    lv_obj_set_pos(add_aux_btn, 25, btn_y);
    ui::rack_skin::apply_hardware_button(add_aux_btn, false, COLOR_EDGE, COLOR_CONTROL, lv_color_hex(0x181D23));
    auto* aux_lbl = lv_label_create(add_aux_btn);
    lv_label_set_text(aux_lbl, "+ ADD AUX");
    lv_obj_center(aux_lbl);
    lv_obj_set_style_text_font(aux_lbl, &ac_font_14, 0);
    lv_obj_set_style_text_color(aux_lbl, COLOR_TEXT2, 0);
    lv_obj_add_event_cb(add_aux_btn, [](lv_event_t*) {
        show_extension_info_modal("AUX BUS ROUTING ARCHITECTURE",
            "PRISMIX provides 4 stereo Aux Send/Return buses (AUX 01-04).\n\n"
            "• Sends: Pre-fade or post-fade strip taps routed to aux mix buses.\n"
            "• Returns: Dedicated return trim, stereo limiter, and FX insert rack.\n"
            "• Matrix Routing: Assign aux master outputs to physical hardware jacks in the ROUTING tab.");
    }, LV_EVENT_CLICKED, nullptr);

    auto* add_grp_btn = lv_button_create(empty_container);
    lv_obj_set_size(add_grp_btn, 180, 42);
    lv_obj_set_pos(add_grp_btn, 205, btn_y);
    ui::rack_skin::apply_hardware_button(add_grp_btn, false, COLOR_EDGE, COLOR_CONTROL, lv_color_hex(0x181D23));
    auto* grp_lbl = lv_label_create(add_grp_btn);
    lv_label_set_text(grp_lbl, "+ ADD GROUP BUS");
    lv_obj_center(grp_lbl);
    lv_obj_set_style_text_font(grp_lbl, &ac_font_14, 0);
    lv_obj_set_style_text_color(grp_lbl, COLOR_TEXT2, 0);
    lv_obj_add_event_cb(add_grp_btn, [](lv_event_t*) {
        show_extension_info_modal("GROUP BUS ARCHITECTURE",
            "PRISMIX provides 4 stereo Subgroup Summing buses (GROUP 01-04).\n\n"
            "• Assignment: Channel strips can route directly to Subgroups for stem processing.\n"
            "• Master Trim & Pan: Group faders control overall stem level into Main L/R.\n"
            "• Group Routing: Configured via channel strip routing badges and the ROUTING matrix.");
    }, LV_EVENT_CLICKED, nullptr);

    auto* add_dca_btn = lv_button_create(empty_container);
    lv_obj_set_size(add_dca_btn, 170, 42);
    lv_obj_set_pos(add_dca_btn, 395, btn_y);
    ui::rack_skin::apply_hardware_button(add_dca_btn, false, COLOR_EDGE, COLOR_CONTROL, lv_color_hex(0x181D23));
    auto* dca_lbl = lv_label_create(add_dca_btn);
    lv_label_set_text(dca_lbl, "+ ADD DCA");
    lv_obj_center(dca_lbl);
    lv_obj_set_style_text_font(dca_lbl, &ac_font_14, 0);
    lv_obj_set_style_text_color(dca_lbl, COLOR_TEXT2, 0);
    lv_obj_add_event_cb(add_dca_btn, [](lv_event_t*) {
        show_extension_info_modal("DCA GROUPS ARCHITECTURE",
            "PRISMIX provides 8 Digitally Controlled Amplifier groups (DCA 01-08).\n\n"
            "• Control: DCAs adjust assigned channel fader levels proportionally without audio summing.\n"
            "• Mute Grouping: DCA mute pushbuttons mute all assigned channel paths simultaneously.\n"
            "• Architecture: Supported in the MixerState data model as clean architectural extension points.");
    }, LV_EVENT_CLICKED, nullptr);

    // 2. Trailing Channel Strip Add Card (displayed when channels are populated)
    auto* add_card = lv_obj_create(scroll);
    g_add_card = add_card;
    lv_obj_remove_flag(add_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(add_card, LV_OBJ_FLAG_CLICKABLE);
    ui::rack_skin::apply_channel_strip_panel(add_card, false);
    ui::rack_skin::create_rack_screw(add_card, 4, 4);
    ui::rack_skin::create_rack_screw(add_card, 130 - 18, 4);
    ui::rack_skin::create_rack_screw(add_card, 4, STRIP_HEIGHT - 18);
    ui::rack_skin::create_rack_screw(add_card, 130 - 18, STRIP_HEIGHT - 18);

    auto* plus_lbl = lv_label_create(add_card);
    lv_label_set_text(plus_lbl, "+");
    lv_obj_align(plus_lbl, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_text_font(plus_lbl, &ac_font_28, 0);
    lv_obj_set_style_text_color(plus_lbl, COLOR_SILVER, 0);

    g_add_label = lv_label_create(add_card);
    lv_label_set_text(g_add_label, "ADD CH");
    lv_obj_align(g_add_label, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_text_font(g_add_label, &ac_font_12, 0);
    lv_obj_set_style_text_color(g_add_label, COLOR_TEXT2, 0);

    lv_obj_add_event_cb(add_card, [](lv_event_t* e) {
        (void)e;
        add_channel();
    }, LV_EVENT_CLICKED, nullptr);

    reposition_add_card();

    // Docked Master strip on the right (permanently fixed, protected)
    g_master_strip = build_strip_ui(screen, -1, 1702, 52, 208, true);

    // Apply initial visual selection
    sync_selection_visuals();

    return screen;
}

void mixer_screen_select_channel(int index) { select_channel(index); }
void mixer_screen_show_rename(int index) { show_rename_modal(index); }
void mixer_screen_show_delete(int index) { show_delete_modal(index); }
void mixer_screen_add_channel(void) { create_channel(); }
void mixer_screen_show_channel_setup(int channel_id) {
    show_channel_setup_modal(channel_id, channel_id <= 0);
}
void mixer_screen_delete_channel(int index) { delete_channel(index); }
void mixer_screen_refresh_channel_by_id(int id) {
    for (int i = 0; i < (int)g_mixer_state.channels.size(); ++i) {
        if (g_mixer_state.channels[i].id == id) {
            updateChannelStrip(i);
            return;
        }
    }
}
void mixer_screen_dismiss_modal(void) {
    if (g_active_modal) {
        lv_obj_delete(g_active_modal);
        g_active_modal = nullptr;
    }
}
void mixer_screen_sync_selection_visuals(void) {
    sync_selection_visuals();
}
