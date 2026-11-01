// ui_cloud_dialog.cpp
// PRISMIX Cloud Preset Manager Dialog (LVGL UI)
// Fixed: ASCII-only labels, textarea keyboard focus, button pressed states

#include "ui_cloud_dialog.h"
#include "supabase_client.h"
#include "scene_serializer.h"
#include "cloud_config.h"
#include "../ui/theme/theme.h"
#include "../ui/ui.h"

#include <vector>
#include <string>
#include <mutex>
#include <cstdio>
#include <algorithm>
#include <functional>

namespace Prismix {
namespace Cloud {

namespace {

static lv_obj_t* g_cloud_dialog_backdrop = nullptr;

enum class CloudEvent {
    None,
    ListFetched,
    ConfigFetched,
    PresetSaved,
    PresetUpdated,
    PresetDeleted,
    ErrorOccurred
};

struct CloudDialogContext {
    lv_obj_t* backdrop     = nullptr;
    lv_obj_t* dialog       = nullptr;
    lv_obj_t* preset_list  = nullptr;
    lv_obj_t* name_input   = nullptr;
    lv_obj_t* recall_btn   = nullptr;
    lv_obj_t* delete_btn   = nullptr;
    lv_obj_t* status_label = nullptr;
    lv_timer_t* update_timer = nullptr;

    std::vector<PresetInfo>  presets;
    std::vector<lv_obj_t*>  preset_buttons;
    int selected_idx = -1;

    std::mutex mtx;
    CloudEvent  pending_event = CloudEvent::None;
    std::string pending_msg;
    std::vector<PresetInfo> fetched_presets;
    std::string fetched_config_json;

    bool closed = false;
};

// ---------------------------------------------------------------------------
// Helper: apply pressed visual state to a button with a given base bg color
// ---------------------------------------------------------------------------
static void apply_button_pressed_style(lv_obj_t* btn,
                                       lv_color_t pressed_bg,
                                       lv_color_t pressed_border,
                                       int border_width = 2)
{
    lv_obj_set_style_bg_color(btn, pressed_bg,     LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa (btn, LV_OPA_COVER,   LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, pressed_border, LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, border_width,   LV_STATE_PRESSED);
}

// ---------------------------------------------------------------------------
// Reusable Confirmation Popup inside dialog
// ---------------------------------------------------------------------------
static void show_confirmation_popup(lv_obj_t* parent,
                                    const char* title_text,
                                    const char* msg_text,
                                    const char* confirm_btn_text,
                                    lv_color_t confirm_color,
                                    lv_color_t confirm_pressed_color,
                                    std::function<void()> on_confirm)
{
    if (!parent) return;

    // Overlay over the dialog
    auto* overlay = lv_obj_create(parent);
    lv_obj_set_size(overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 4, 0);
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    // Confirmation box
    auto* box = lv_obj_create(overlay);
    lv_obj_set_size(box, 460, 200);
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, COLOR_PANEL, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(box, confirm_color, 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_radius(box, 4, 0);
    lv_obj_set_style_pad_all(box, 20, 0);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    auto* title_lbl = lv_label_create(box);
    lv_label_set_text(title_lbl, title_text);
    lv_obj_align(title_lbl, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_text_font(title_lbl, &ac_font_16, 0);
    lv_obj_set_style_text_color(title_lbl, confirm_color, 0);

    // Message
    auto* msg_lbl = lv_label_create(box);
    lv_label_set_text(msg_lbl, msg_text);
    lv_obj_set_width(msg_lbl, 410);
    lv_obj_set_style_text_font(msg_lbl, &ac_font_14, 0);
    lv_obj_set_style_text_color(msg_lbl, COLOR_TEXT, 0);
    lv_obj_align(msg_lbl, LV_ALIGN_TOP_LEFT, 0, 36);

    // Buttons Container
    // 1. Confirm Button
    auto* ok_btn = lv_button_create(box);
    lv_obj_set_size(ok_btn, 195, 40);
    lv_obj_align(ok_btn, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(ok_btn, lv_color_hex(0x281C10), 0);
    lv_obj_set_style_border_color(ok_btn, confirm_color, 0);
    lv_obj_set_style_border_width(ok_btn, 1, 0);
    lv_obj_set_style_radius(ok_btn, 3, 0);
    apply_button_pressed_style(ok_btn, confirm_pressed_color, confirm_color, 2);

    auto* ok_lbl = lv_label_create(ok_btn);
    lv_label_set_text(ok_lbl, confirm_btn_text);
    lv_obj_center(ok_lbl);
    lv_obj_set_style_text_font(ok_lbl, &ac_font_12, 0);
    lv_obj_set_style_text_color(ok_lbl, confirm_color, 0);

    // 2. Cancel Button
    auto* cancel_btn = lv_button_create(box);
    lv_obj_set_size(cancel_btn, 195, 40);
    lv_obj_align(cancel_btn, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(cancel_btn, COLOR_CONTROL, 0);
    lv_obj_set_style_border_color(cancel_btn, COLOR_EDGE, 0);
    lv_obj_set_style_border_width(cancel_btn, 1, 0);
    lv_obj_set_style_radius(cancel_btn, 3, 0);
    apply_button_pressed_style(cancel_btn, lv_color_hex(0x3A3A4A), COLOR_CYAN, 1);

    auto* cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "CANCEL");
    lv_obj_center(cancel_lbl);
    lv_obj_set_style_text_font(cancel_lbl, &ac_font_12, 0);
    lv_obj_set_style_text_color(cancel_lbl, COLOR_TEXT2, 0);

    struct ConfirmHolder {
        std::function<void()> cb;
        lv_obj_t* overlay_to_del;
    };
    auto* holder = new ConfirmHolder{std::move(on_confirm), overlay};

    lv_obj_add_event_cb(ok_btn, [](lv_event_t* e) {
        auto* h = static_cast<ConfirmHolder*>(lv_event_get_user_data(e));
        if (h) {
            if (h->cb) h->cb();
            lv_obj_delete(h->overlay_to_del);
            delete h;
        }
    }, LV_EVENT_CLICKED, holder);

    lv_obj_add_event_cb(cancel_btn, [](lv_event_t* e) {
        auto* h = static_cast<ConfirmHolder*>(lv_event_get_user_data(e));
        if (h) {
            lv_obj_delete(h->overlay_to_del);
            delete h;
        }
    }, LV_EVENT_CLICKED, holder);
}

// ---------------------------------------------------------------------------
// Selection highlight for preset list items
// ---------------------------------------------------------------------------
void update_selection_styles(CloudDialogContext* ctx) {
    if (!ctx) return;
    for (size_t i = 0; i < ctx->preset_buttons.size(); ++i) {
        lv_obj_t* btn = ctx->preset_buttons[i];
        if (!btn) continue;
        const bool sel = (static_cast<int>(i) == ctx->selected_idx);
        lv_obj_set_style_bg_color(btn, sel ? lv_color_hex(0x24323D) : COLOR_CONTROL, 0);
        lv_obj_set_style_border_color(btn, sel ? COLOR_CYAN : COLOR_EDGE, 0);
        lv_obj_set_style_border_width(btn, sel ? 2 : 1, 0);
    }
}

// ---------------------------------------------------------------------------
// Rebuild scrollable preset list
// ---------------------------------------------------------------------------
void rebuild_preset_list_ui(CloudDialogContext* ctx) {
    if (!ctx || !ctx->preset_list) return;

    lv_obj_clean(ctx->preset_list);
    ctx->preset_buttons.clear();
    ctx->selected_idx = -1;

    if (ctx->presets.empty()) {
        auto* empty_lbl = lv_label_create(ctx->preset_list);
        lv_label_set_text(empty_lbl,
            "No presets found in cloud database.\n"
            "Save your current mixer configuration below!");
        lv_obj_set_style_text_color(empty_lbl, COLOR_TEXT2, 0);
        lv_obj_set_style_text_font(empty_lbl, &ac_font_14, 0);
        lv_obj_set_style_pad_all(empty_lbl, 20, 0);
        return;
    }

    for (size_t i = 0; i < ctx->presets.size(); ++i) {
        const auto& p = ctx->presets[i];
        auto* btn = lv_button_create(ctx->preset_list);
        lv_obj_set_size(btn, LV_PCT(100), 52);
        lv_obj_set_style_bg_color(btn, COLOR_CONTROL, 0);
        lv_obj_set_style_border_color(btn, COLOR_EDGE, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
        lv_obj_set_style_radius(btn, 3, 0);
        lv_obj_set_style_pad_hor(btn, 16, 0);
        lv_obj_set_style_pad_ver(btn, 6, 0);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_SCROLL_CHAIN_VER);

        // Pressed state for list items
        apply_button_pressed_style(btn, lv_color_hex(0x2A4050), COLOR_CYAN, 2);

        // Preset name label
        auto* name_lbl = lv_label_create(btn);
        lv_label_set_text(name_lbl, p.name.c_str());
        lv_obj_align(name_lbl, LV_ALIGN_LEFT_MID, 0, -8);
        lv_obj_set_style_text_font(name_lbl, &ac_font_14, 0);
        lv_obj_set_style_text_color(name_lbl, COLOR_TEXT, 0);

        // Date / version subtitle
        auto* info_lbl = lv_label_create(btn);
        std::string date_str = p.created_at;
        if (date_str.size() > 19) date_str = date_str.substr(0, 19);
        char buf[128];
        std::snprintf(buf, sizeof(buf), "v%d | Created: %s", p.version, date_str.c_str());
        lv_label_set_text(info_lbl, buf);
        lv_obj_align(info_lbl, LV_ALIGN_LEFT_MID, 0, 10);
        lv_obj_set_style_text_font(info_lbl, &ac_font_12, 0);
        lv_obj_set_style_text_color(info_lbl, COLOR_TEXT2, 0);

        // Selection click handler
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            auto* c      = static_cast<CloudDialogContext*>(lv_event_get_user_data(e));
            auto* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
            for (size_t idx = 0; idx < c->preset_buttons.size(); ++idx) {
                if (c->preset_buttons[idx] == target) {
                    c->selected_idx = static_cast<int>(idx);
                    break;
                }
            }
            update_selection_styles(c);
        }, LV_EVENT_CLICKED, ctx);

        ctx->preset_buttons.push_back(btn);
    }

    // Auto-select first item
    if (!ctx->presets.empty()) {
        ctx->selected_idx = 0;
        update_selection_styles(ctx);
    }
}

// ---------------------------------------------------------------------------
// Trigger an async refresh from Supabase
// ---------------------------------------------------------------------------
void trigger_refresh(CloudDialogContext* ctx) {
    if (!ctx) return;
    if (ctx->status_label) {
        lv_label_set_text(ctx->status_label, "Fetching presets from cloud...");
        lv_obj_set_style_text_color(ctx->status_label, COLOR_CYAN, 0);
    }

    SupabaseClient::list_presets_async([ctx](bool success,
                                             const std::vector<PresetInfo>& list,
                                             const std::string& err) {
        std::lock_guard<std::mutex> lock(ctx->mtx);
        if (success) {
            ctx->fetched_presets = list;
            ctx->pending_event   = CloudEvent::ListFetched;
        } else {
            ctx->pending_msg   = err;
            ctx->pending_event = CloudEvent::ErrorOccurred;
        }
    });
}

// ---------------------------------------------------------------------------
// 50ms timer: poll thread-safe event queue and update UI
// ---------------------------------------------------------------------------
void dialog_timer_cb(lv_timer_t* timer) {
    auto* ctx = static_cast<CloudDialogContext*>(lv_timer_get_user_data(timer));
    if (!ctx || ctx->closed) return;

    CloudEvent ev = CloudEvent::None;
    std::string msg;
    std::vector<PresetInfo> fetched;
    std::string config_json;

    {
        std::lock_guard<std::mutex> lock(ctx->mtx);
        ev                  = ctx->pending_event;
        ctx->pending_event  = CloudEvent::None;
        msg                 = ctx->pending_msg;
        fetched             = std::move(ctx->fetched_presets);
        config_json         = std::move(ctx->fetched_config_json);
    }

    switch (ev) {
        case CloudEvent::ListFetched: {
            ctx->presets = std::move(fetched);
            rebuild_preset_list_ui(ctx);
            if (ctx->status_label) {
                char buf[128];
                std::snprintf(buf, sizeof(buf),
                    "Synced %zu preset(s) from cloud.", ctx->presets.size());
                lv_label_set_text(ctx->status_label, buf);
                lv_obj_set_style_text_color(ctx->status_label, COLOR_GREEN, 0);
            }
            break;
        }
        case CloudEvent::ConfigFetched: {
            std::string err;
            if (deserialize_json_to_mixer_state(config_json, &err)) {
                // Instantly re-render the active screen (faders, knobs, mute buttons)
                ui_refresh_after_preset_recall();

                if (ctx->status_label) {
                    lv_label_set_text(ctx->status_label,
                        "OK: Preset recalled and applied to mixer successfully!");
                    lv_obj_set_style_text_color(ctx->status_label, COLOR_GREEN, 0);
                }
            } else {
                if (ctx->status_label) {
                    char buf[256];
                    std::snprintf(buf, sizeof(buf), "ERROR: Recall failed: %s", err.c_str());
                    lv_label_set_text(ctx->status_label, buf);
                    lv_obj_set_style_text_color(ctx->status_label, COLOR_RED, 0);
                }
            }
            break;
        }
        case CloudEvent::PresetSaved: {
            if (ctx->name_input)
                lv_textarea_set_text(ctx->name_input, "");
            if (ctx->status_label) {
                lv_label_set_text(ctx->status_label,
                    "OK: Preset saved to Supabase successfully!");
                lv_obj_set_style_text_color(ctx->status_label, COLOR_GREEN, 0);
            }
            trigger_refresh(ctx);
            break;
        }
        case CloudEvent::PresetUpdated: {
            if (ctx->status_label) {
                lv_label_set_text(ctx->status_label,
                    "OK: Preset updated/overwritten in Supabase successfully!");
                lv_obj_set_style_text_color(ctx->status_label, COLOR_GREEN, 0);
            }
            trigger_refresh(ctx);
            break;
        }
        case CloudEvent::PresetDeleted: {
            if (ctx->status_label) {
                lv_label_set_text(ctx->status_label, "OK: Preset deleted from cloud.");
                lv_obj_set_style_text_color(ctx->status_label, COLOR_GREEN, 0);
            }
            trigger_refresh(ctx);
            break;
        }
        case CloudEvent::ErrorOccurred: {
            if (ctx->status_label) {
                char buf[512];
                std::snprintf(buf, sizeof(buf), "ERROR: %s", msg.c_str());
                lv_label_set_text(ctx->status_label, buf);
                lv_obj_set_style_text_color(ctx->status_label, COLOR_RED, 0);
            }
            break;
        }
        case CloudEvent::None:
        default:
            break;
    }
}

} // anonymous namespace

// ===========================================================================
//  show_cloud_preset_dialog()
// ===========================================================================
void show_cloud_preset_dialog() {
    if (g_cloud_dialog_backdrop) {
        dismiss_cloud_preset_dialog();
    }

    auto* ctx = new CloudDialogContext();

    // ------------------------------------------------------------------
    // 1. Semi-transparent backdrop
    // ------------------------------------------------------------------
    auto* backdrop = lv_obj_create(lv_layer_top());
    ctx->backdrop  = backdrop;
    g_cloud_dialog_backdrop = backdrop;
    lv_obj_set_size(backdrop, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(backdrop, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_70, 0);
    lv_obj_set_style_border_width(backdrop, 0, 0);
    lv_obj_set_style_radius(backdrop, 0, 0);
    lv_obj_remove_flag(backdrop, LV_OBJ_FLAG_SCROLLABLE);

    // Cleanup on delete
    lv_obj_add_event_cb(backdrop, [](lv_event_t* e) {
        g_cloud_dialog_backdrop = nullptr;
        auto* c = static_cast<CloudDialogContext*>(lv_event_get_user_data(e));
        if (c) {
            c->closed = true;
            if (c->update_timer) {
                lv_timer_delete(c->update_timer);
                c->update_timer = nullptr;
            }
            delete c;
        }
    }, LV_EVENT_DELETE, ctx);

    // ------------------------------------------------------------------
    // 2. Dialog frame
    // ------------------------------------------------------------------
    auto* dialog = lv_obj_create(backdrop);
    ctx->dialog  = dialog;
    lv_obj_set_size(dialog, 840, 560);
    lv_obj_align(dialog, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(dialog, COLOR_PANEL, 0);
    lv_obj_set_style_bg_opa(dialog, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(dialog, COLOR_CYAN, 0);
    lv_obj_set_style_border_width(dialog, 1, 0);
    lv_obj_set_style_radius(dialog, 4, 0);
    lv_obj_set_style_pad_all(dialog, 24, 0);
    lv_obj_remove_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);

    // ------------------------------------------------------------------
    // 3. Header title
    // ------------------------------------------------------------------
    auto* title = lv_label_create(dialog);
    lv_label_set_text(title, "PRISMIX  /  CLOUD PRESET MANAGER");
    lv_obj_set_pos(title, 0, 0);
    lv_obj_set_style_text_font(title, &ac_font_16, 0);
    lv_obj_set_style_text_color(title, COLOR_CYAN, 0);

    // Status badge
    auto* badge = lv_obj_create(dialog);
    lv_obj_set_pos(badge, 620, 0);
    lv_obj_set_size(badge, 172, 28);
    lv_obj_set_style_bg_color(badge, lv_color_hex(0x0C2B1D), 0);
    lv_obj_set_style_border_color(badge, COLOR_GREEN, 0);
    lv_obj_set_style_border_width(badge, 1, 0);
    lv_obj_set_style_radius(badge, 3, 0);
    lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    auto* badge_lbl = lv_label_create(badge);
    lv_label_set_text(badge_lbl, "SUPABASE ONLINE");
    lv_obj_center(badge_lbl);
    lv_obj_set_style_text_font(badge_lbl, &ac_font_12, 0);
    lv_obj_set_style_text_color(badge_lbl, COLOR_GREEN, 0);

    auto* subtitle = lv_label_create(dialog);
    lv_label_set_text(subtitle,
        "Save and restore console configurations directly to/from your Supabase cloud database.");
    lv_obj_set_pos(subtitle, 0, 24);
    lv_obj_set_style_text_font(subtitle, &ac_font_12, 0);
    lv_obj_set_style_text_color(subtitle, COLOR_TEXT2, 0);

    // Divider line
    auto* div = lv_obj_create(dialog);
    lv_obj_set_pos(div, 0, 46);
    lv_obj_set_size(div, 792, 1);
    lv_obj_set_style_bg_color(div, COLOR_EDGE, 0);
    lv_obj_set_style_border_width(div, 0, 0);

    // ------------------------------------------------------------------
    // 4. Section label + REFRESH button
    // ------------------------------------------------------------------
    auto* list_title = lv_label_create(dialog);
    lv_label_set_text(list_title, "CLOUD PRESETS:");
    lv_obj_set_pos(list_title, 0, 56);
    lv_obj_set_style_text_font(list_title, &ac_font_12, 0);
    lv_obj_set_style_text_color(list_title, COLOR_TEXT2, 0);

    auto* ref_btn = lv_button_create(dialog);
    lv_obj_set_pos(ref_btn, 420, 52);
    lv_obj_set_size(ref_btn, 110, 26);
    lv_obj_set_style_bg_color(ref_btn, COLOR_CONTROL, 0);
    lv_obj_set_style_border_color(ref_btn, COLOR_EDGE, 0);
    lv_obj_set_style_border_width(ref_btn, 1, 0);
    lv_obj_set_style_radius(ref_btn, 2, 0);
    // Pressed state
    apply_button_pressed_style(ref_btn, lv_color_hex(0x2A2A3A), COLOR_CYAN, 1);

    auto* ref_lbl = lv_label_create(ref_btn);
    lv_label_set_text(ref_lbl, "REFRESH");
    lv_obj_center(ref_lbl);
    lv_obj_set_style_text_font(ref_lbl, &ac_font_12, 0);
    lv_obj_set_style_text_color(ref_lbl, COLOR_TEXT, 0);
    lv_obj_add_event_cb(ref_btn, [](lv_event_t* e) {
        auto* c = static_cast<CloudDialogContext*>(lv_event_get_user_data(e));
        trigger_refresh(c);
    }, LV_EVENT_CLICKED, ctx);

    // ------------------------------------------------------------------
    // 5. Scrollable preset list box (with PRISMIX Cyan-themed scrollbar)
    // ------------------------------------------------------------------
    auto* list = lv_obj_create(dialog);
    ctx->preset_list = list;
    lv_obj_set_pos(list, 0, 84);
    lv_obj_set_size(list, 530, 280);
    lv_obj_set_style_bg_color(list, COLOR_RECESSED, 0);
    lv_obj_set_style_border_color(list, COLOR_EDGE, 0);
    lv_obj_set_style_border_width(list, 1, 0);
    lv_obj_set_style_radius(list, 3, 0);
    lv_obj_set_style_pad_all(list, 6, 0);
    lv_obj_set_style_pad_right(list, 10, 0); // Leave room for vertical scrollbar
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 6, 0);

    // Explicit vertical scroll & auto scrollbar
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    // PRISMIX Theme Scrollbar (Cyan thumb with dark slate glow)
    lv_obj_set_style_bg_color(list, COLOR_CYAN, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(list, LV_OPA_60, LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(list, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(list, 6, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(list, 2, LV_PART_SCROLLBAR);

    // ------------------------------------------------------------------
    // 6. RECALL PRESET button
    // ------------------------------------------------------------------
    auto* recall_btn = lv_button_create(dialog);
    ctx->recall_btn  = recall_btn;
    lv_obj_set_pos(recall_btn, 550, 84);
    lv_obj_set_size(recall_btn, 242, 48);
    lv_obj_set_style_bg_color(recall_btn, lv_color_hex(0x0C2B38), 0);
    lv_obj_set_style_border_color(recall_btn, COLOR_CYAN, 0);
    lv_obj_set_style_border_width(recall_btn, 1, 0);
    lv_obj_set_style_radius(recall_btn, 3, 0);
    // Pressed state (darker cyan tint)
    apply_button_pressed_style(recall_btn, lv_color_hex(0x1A4D64), COLOR_CYAN, 2);

    auto* rec_lbl = lv_label_create(recall_btn);
    lv_label_set_text(rec_lbl, "RECALL PRESET");
    lv_obj_center(rec_lbl);
    lv_obj_set_style_text_font(rec_lbl, &ac_font_14, 0);
    lv_obj_set_style_text_color(rec_lbl, COLOR_CYAN, 0);

    lv_obj_add_event_cb(recall_btn, [](lv_event_t* e) {
        auto* c = static_cast<CloudDialogContext*>(lv_event_get_user_data(e));
        if (c->selected_idx < 0 ||
            c->selected_idx >= static_cast<int>(c->presets.size()))
        {
            if (c->status_label) {
                lv_label_set_text(c->status_label,
                    "! Please select a preset from the list first.");
                lv_obj_set_style_text_color(c->status_label, COLOR_YELLOW, 0);
            }
            return;
        }
        const auto& p = c->presets[c->selected_idx];
        if (c->status_label) {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                "Downloading '%s' from cloud...", p.name.c_str());
            lv_label_set_text(c->status_label, buf);
            lv_obj_set_style_text_color(c->status_label, COLOR_CYAN, 0);
        }
        SupabaseClient::get_preset_config_async(p.id,
            [c](bool success, const std::string& config_json, const std::string& err) {
                std::lock_guard<std::mutex> lock(c->mtx);
                if (success) {
                    c->fetched_config_json = config_json;
                    c->pending_event       = CloudEvent::ConfigFetched;
                } else {
                    c->pending_msg   = err;
                    c->pending_event = CloudEvent::ErrorOccurred;
                }
            });
    }, LV_EVENT_CLICKED, ctx);

    // ------------------------------------------------------------------
    // 7. UPDATE / OVERWRITE PRESET button (HTTP PATCH)
    // ------------------------------------------------------------------
    auto* upd_btn = lv_button_create(dialog);
    lv_obj_set_pos(upd_btn, 550, 140);
    lv_obj_set_size(upd_btn, 242, 46);
    lv_obj_set_style_bg_color(upd_btn, lv_color_hex(0x28200C), 0);
    lv_obj_set_style_border_color(upd_btn, COLOR_ORANGE, 0);
    lv_obj_set_style_border_width(upd_btn, 1, 0);
    lv_obj_set_style_radius(upd_btn, 3, 0);
    apply_button_pressed_style(upd_btn, lv_color_hex(0x4A3812), COLOR_ORANGE, 2);

    auto* upd_lbl = lv_label_create(upd_btn);
    lv_label_set_text(upd_lbl, "OVERWRITE / UPDATE");
    lv_obj_center(upd_lbl);
    lv_obj_set_style_text_font(upd_lbl, &ac_font_12, 0);
    lv_obj_set_style_text_color(upd_lbl, COLOR_ORANGE, 0);

    lv_obj_add_event_cb(upd_btn, [](lv_event_t* e) {
        auto* c = static_cast<CloudDialogContext*>(lv_event_get_user_data(e));
        if (c->selected_idx < 0 ||
            c->selected_idx >= static_cast<int>(c->presets.size()))
        {
            if (c->status_label) {
                lv_label_set_text(c->status_label,
                    "! Please select a preset from the list to update/overwrite.");
                lv_obj_set_style_text_color(c->status_label, COLOR_YELLOW, 0);
            }
            return;
        }
        const auto& p = c->presets[c->selected_idx];

        const char* input_name = c->name_input ? lv_textarea_get_text(c->name_input) : nullptr;
        std::string target_name = (input_name && std::string(input_name).size() > 0)
                                    ? std::string(input_name)
                                    : p.name;

        char confirm_msg[256];
        std::snprintf(confirm_msg, sizeof(confirm_msg),
            "Are you sure you want to overwrite '%s' with your current live mixer configuration?",
            p.name.c_str());

        std::string preset_id = p.id;
        show_confirmation_popup(
            c->dialog,
            "CONFIRM OVERWRITE",
            confirm_msg,
            "YES, OVERWRITE",
            COLOR_ORANGE,
            lv_color_hex(0x4A3812),
            [c, preset_id, target_name]() {
                std::string config_json = serialize_mixer_state_to_json();
                if (c->status_label) {
                    char buf[256];
                    std::snprintf(buf, sizeof(buf), "Updating '%s' on Supabase...", target_name.c_str());
                    lv_label_set_text(c->status_label, buf);
                    lv_obj_set_style_text_color(c->status_label, COLOR_CYAN, 0);
                }

                SupabaseClient::update_preset_async(
                    preset_id, target_name, config_json,
                    [c](bool success, const std::string& err) {
                        std::lock_guard<std::mutex> lock(c->mtx);
                        if (success) {
                            c->pending_event = CloudEvent::PresetUpdated;
                        } else {
                            c->pending_msg   = err;
                            c->pending_event = CloudEvent::ErrorOccurred;
                        }
                    });
            });
    }, LV_EVENT_CLICKED, ctx);

    // ------------------------------------------------------------------
    // 8. DELETE PRESET button
    // ------------------------------------------------------------------
    auto* del_btn   = lv_button_create(dialog);
    ctx->delete_btn = del_btn;
    lv_obj_set_pos(del_btn, 550, 194);
    lv_obj_set_size(del_btn, 242, 42);
    lv_obj_set_style_bg_color(del_btn, COLOR_CONTROL, 0);
    lv_obj_set_style_border_color(del_btn, lv_color_hex(0x5A2222), 0);
    lv_obj_set_style_border_width(del_btn, 1, 0);
    lv_obj_set_style_radius(del_btn, 3, 0);
    // Pressed state (dark red)
    apply_button_pressed_style(del_btn, lv_color_hex(0x3D1A1A), lv_color_hex(0xCC4444), 2);

    auto* del_lbl = lv_label_create(del_btn);
    lv_label_set_text(del_lbl, "DELETE PRESET");
    lv_obj_center(del_lbl);
    lv_obj_set_style_text_font(del_lbl, &ac_font_12, 0);
    lv_obj_set_style_text_color(del_lbl, COLOR_RED, 0);

    lv_obj_add_event_cb(del_btn, [](lv_event_t* e) {
        auto* c = static_cast<CloudDialogContext*>(lv_event_get_user_data(e));
        if (c->selected_idx < 0 ||
            c->selected_idx >= static_cast<int>(c->presets.size()))
        {
            if (c->status_label) {
                lv_label_set_text(c->status_label,
                    "! Please select a preset from the list to delete.");
                lv_obj_set_style_text_color(c->status_label, COLOR_YELLOW, 0);
            }
            return;
        }
        const auto& p = c->presets[c->selected_idx];

        char confirm_msg[256];
        std::snprintf(confirm_msg, sizeof(confirm_msg),
            "Are you sure you want to permanently delete preset '%s' from cloud database?",
            p.name.c_str());

        std::string preset_id = p.id;
        std::string preset_name = p.name;
        show_confirmation_popup(
            c->dialog,
            "CONFIRM DELETE",
            confirm_msg,
            "YES, DELETE",
            COLOR_RED,
            lv_color_hex(0x3D1A1A),
            [c, preset_id, preset_name]() {
                if (c->status_label) {
                    char buf[256];
                    std::snprintf(buf, sizeof(buf), "Deleting '%s' from cloud...", preset_name.c_str());
                    lv_label_set_text(c->status_label, buf);
                    lv_obj_set_style_text_color(c->status_label, COLOR_YELLOW, 0);
                }
                SupabaseClient::delete_preset_async(
                    preset_id,
                    [c](bool success, const std::string& err) {
                        std::lock_guard<std::mutex> lock(c->mtx);
                        if (success) {
                            c->pending_event = CloudEvent::PresetDeleted;
                        } else {
                            c->pending_msg   = err;
                            c->pending_event = CloudEvent::ErrorOccurred;
                        }
                    });
            });
    }, LV_EVENT_CLICKED, ctx);

    // ------------------------------------------------------------------
    // 8. Save section label
    // ------------------------------------------------------------------
    auto* save_label = lv_label_create(dialog);
    lv_label_set_text(save_label, "SAVE CURRENT MIXER AS NEW CLOUD PRESET:");
    lv_obj_set_pos(save_label, 0, 376);
    lv_obj_set_style_text_font(save_label, &ac_font_12, 0);
    lv_obj_set_style_text_color(save_label, COLOR_TEXT2, 0);

    // ------------------------------------------------------------------
    // 9. Preset name TEXTAREA  (with keyboard focus support)
    // ------------------------------------------------------------------
    auto* ta      = lv_textarea_create(dialog);
    ctx->name_input = ta;
    lv_obj_set_pos(ta, 0, 398);
    lv_obj_set_size(ta, 530, 42);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta,
        "Enter preset name (e.g. Band Soundcheck Live)...");
    lv_obj_set_style_bg_color(ta, COLOR_CONTROL, 0);
    lv_obj_set_style_border_color(ta, COLOR_EDGE, 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_radius(ta, 3, 0);
    lv_obj_set_style_text_font(ta, &ac_font_14, 0);
    lv_obj_set_style_text_color(ta, COLOR_TEXT, 0);
    // Focused border highlight so user knows it is active
    lv_obj_set_style_border_color(ta, COLOR_CYAN,  LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(ta, 2,           LV_STATE_FOCUSED);

    // --- Keyboard focus: find SDL/keypad indev and attach to a group ---
    {
        lv_group_t* kb_group = lv_group_create();
        lv_group_add_obj(kb_group, ta);

        // Walk registered input devices looking for a keypad (keyboard)
        lv_indev_t* indev = lv_indev_get_next(nullptr);
        while (indev) {
            if (lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD) {
                lv_indev_set_group(indev, kb_group);
                break;
            }
            indev = lv_indev_get_next(indev);
        }

        // Clicking textarea moves LVGL focus to it so keystrokes arrive
        lv_obj_add_event_cb(ta, [](lv_event_t* ev) {
            lv_group_focus_obj(lv_event_get_target_obj(ev));
        }, LV_EVENT_CLICKED, nullptr);
    }

    // ------------------------------------------------------------------
    // 10. SAVE TO CLOUD button
    // ------------------------------------------------------------------
    auto* save_btn = lv_button_create(dialog);
    lv_obj_set_pos(save_btn, 550, 398);
    lv_obj_set_size(save_btn, 242, 42);
    lv_obj_set_style_bg_color(save_btn, lv_color_hex(0x133824), 0);
    lv_obj_set_style_border_color(save_btn, COLOR_GREEN, 0);
    lv_obj_set_style_border_width(save_btn, 1, 0);
    lv_obj_set_style_radius(save_btn, 3, 0);
    // Pressed state (darker green)
    apply_button_pressed_style(save_btn, lv_color_hex(0x215C38), COLOR_GREEN, 2);

    auto* save_lbl = lv_label_create(save_btn);
    lv_label_set_text(save_lbl, "SAVE TO CLOUD");
    lv_obj_center(save_lbl);
    lv_obj_set_style_text_font(save_lbl, &ac_font_14, 0);
    lv_obj_set_style_text_color(save_lbl, COLOR_GREEN, 0);

    lv_obj_add_event_cb(save_btn, [](lv_event_t* e) {
        auto* c = static_cast<CloudDialogContext*>(lv_event_get_user_data(e));
        const char* name = lv_textarea_get_text(c->name_input);
        if (!name || std::string(name).empty()) {
            if (c->status_label) {
                lv_label_set_text(c->status_label,
                    "! Please enter a preset name before saving.");
                lv_obj_set_style_text_color(c->status_label, COLOR_YELLOW, 0);
            }
            return;
        }
        std::string config_json = serialize_mixer_state_to_json();
        if (c->status_label) {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "Uploading '%s' to Supabase...", name);
            lv_label_set_text(c->status_label, buf);
            lv_obj_set_style_text_color(c->status_label, COLOR_CYAN, 0);
        }
        SupabaseClient::save_preset_async(
            name, "PRISMIX Console Preset", config_json,
            [c](bool success, const std::string& err) {
                std::lock_guard<std::mutex> lock(c->mtx);
                if (success) {
                    c->pending_event = CloudEvent::PresetSaved;
                } else {
                    c->pending_msg   = err;
                    c->pending_event = CloudEvent::ErrorOccurred;
                }
            });
    }, LV_EVENT_CLICKED, ctx);

    // ------------------------------------------------------------------
    // 11. Status label
    // ------------------------------------------------------------------
    auto* status     = lv_label_create(dialog);
    ctx->status_label = status;
    lv_obj_set_pos(status, 0, 470);
    lv_label_set_text(status,
        "Ready. Select a preset to recall, or save your current mixer scene.");
    lv_obj_set_style_text_font(status, &ac_font_12, 0);
    lv_obj_set_style_text_color(status, COLOR_TEXT2, 0);

    // ------------------------------------------------------------------
    // 12. CLOSE button
    // ------------------------------------------------------------------
    auto* close_btn = lv_button_create(dialog);
    lv_obj_set_pos(close_btn, 660, 460);
    lv_obj_set_size(close_btn, 132, 40);
    lv_obj_set_style_bg_color(close_btn, COLOR_CONTROL, 0);
    lv_obj_set_style_border_color(close_btn, COLOR_EDGE, 0);
    lv_obj_set_style_border_width(close_btn, 1, 0);
    lv_obj_set_style_radius(close_btn, 2, 0);
    // Pressed state
    apply_button_pressed_style(close_btn, lv_color_hex(0x3A3A4A), COLOR_CYAN, 1);

    auto* close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, "CLOSE");
    lv_obj_center(close_lbl);
    lv_obj_set_style_text_font(close_lbl, &ac_font_14, 0);
    lv_obj_set_style_text_color(close_lbl, COLOR_TEXT2, 0);
    lv_obj_add_event_cb(close_btn, [](lv_event_t*) {
        dismiss_cloud_preset_dialog();
    }, LV_EVENT_CLICKED, nullptr);

    // ------------------------------------------------------------------
    // 13. Start 50ms background timer + initial cloud fetch
    // ------------------------------------------------------------------
    ctx->update_timer = lv_timer_create(dialog_timer_cb, 50, ctx);
    trigger_refresh(ctx);
}

// ===========================================================================
void dismiss_cloud_preset_dialog() {
    if (g_cloud_dialog_backdrop) {
        lv_obj_delete(g_cloud_dialog_backdrop);
        g_cloud_dialog_backdrop = nullptr;
    }
}

bool is_cloud_preset_dialog_open() {
    return g_cloud_dialog_backdrop != nullptr;
}

} // namespace Cloud
} // namespace Prismix
