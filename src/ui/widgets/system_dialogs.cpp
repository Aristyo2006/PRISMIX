#include "system_dialogs.h"

#include "../theme/theme.h"
#include "../assets/rack_textures.h"
#include "../../service/security/security_service.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

namespace prismix::ui {

namespace {

lv_obj_t* g_active_dialog = nullptr;
lv_obj_t* g_lock_overlay = nullptr;

constexpr int kScreenWidth = 1920;
constexpr int kScreenHeight = 1080;

bool all_digits(const std::string& value) {
    if (value.empty()) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return c >= '0' && c <= '9';
    });
}

lv_obj_t* make_label(lv_obj_t* parent, const char* text, int x, int y,
                     const lv_font_t* font, lv_color_t color) {
    auto* label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

lv_obj_t* make_button(lv_obj_t* parent, const char* text, int x, int y, int w, int h,
                      lv_color_t accent, const char* symbol = nullptr) {
    auto* button = lv_button_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, w, h);
    lv_obj_set_style_radius(button, 2, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    ::ui::rack_skin::apply_hardware_button(button, COLOR_CONTROL, accent, COLOR_PRIMARY_TEXT);
    auto* label = lv_label_create(button);
    if (symbol && symbol[0] != '\0') {
        lv_label_set_text_fmt(label, "%s  %s", symbol, text);
    } else {
        lv_label_set_text(label, text);
    }
    lv_obj_center(label);
    lv_obj_set_style_text_font(label, &ac_font_14, 0);
    lv_obj_set_style_text_color(label, COLOR_PRIMARY_TEXT, 0);
    return button;
}

lv_obj_t* make_backdrop() {
    auto* backdrop = lv_obj_create(lv_layer_top());
    lv_obj_set_pos(backdrop, 0, 0);
    lv_obj_set_size(backdrop, kScreenWidth, kScreenHeight);
    lv_obj_remove_flag(backdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(backdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(backdrop, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_60, 0);
    lv_obj_set_style_border_width(backdrop, 0, 0);
    lv_obj_set_style_radius(backdrop, 0, 0);
    lv_obj_set_style_pad_all(backdrop, 0, 0);
    return backdrop;
}

lv_obj_t* make_panel(lv_obj_t* backdrop, int width, int height, int y_offset,
                     lv_color_t accent) {
    auto* panel = lv_obj_create(backdrop);
    lv_obj_set_size(panel, width, height);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, y_offset);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    ::ui::rack_skin::apply_faceplate_panel(panel, true);
    lv_obj_set_style_bg_color(panel, COLOR_PANEL_SURFACE, 0);
    lv_obj_set_style_border_color(panel, accent, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    return panel;
}

void style_textarea(lv_obj_t* textarea) {
    lv_obj_set_style_bg_color(textarea, COLOR_RECESSED, 0);
    lv_obj_set_style_bg_opa(textarea, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(textarea, COLOR_STANDARD_BORDER, 0);
    lv_obj_set_style_border_width(textarea, 1, 0);
    lv_obj_set_style_radius(textarea, 1, 0);
    lv_obj_set_style_text_color(textarea, COLOR_PRIMARY_TEXT, 0);
    lv_obj_set_style_text_font(textarea, &ac_font_28, 0);
    lv_obj_set_style_pad_left(textarea, 14, 0);
    lv_obj_set_style_pad_right(textarea, 14, 0);
    lv_obj_set_style_text_color(textarea, COLOR_TEXT2, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_text_opa(textarea, LV_OPA_60, LV_PART_TEXTAREA_PLACEHOLDER);
    lv_obj_set_style_border_color(textarea, COLOR_SELECTED_BORDER, LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(textarea, 2, LV_STATE_FOCUSED);
}

lv_obj_t* make_pin_field(lv_obj_t* parent, int y, const char* placeholder) {
    auto* field = lv_textarea_create(parent);
    lv_obj_set_pos(field, 32, y);
    lv_obj_set_size(field, 696, 56);
    lv_textarea_set_one_line(field, true);
    lv_textarea_set_password_mode(field, true);
    lv_textarea_set_password_bullet(field, "*");
    lv_textarea_set_max_length(field, 12);
    lv_textarea_set_accepted_chars(field, "0123456789");
    lv_textarea_set_placeholder_text(field, placeholder);
    style_textarea(field);
    return field;
}

void close_active_dialog() {
    if (!g_active_dialog) return;
    lv_obj_t* dialog = g_active_dialog;
    g_active_dialog = nullptr;
    lv_obj_delete(dialog);
}

void set_pin_status(lv_obj_t* status, const std::string& text, bool error) {
    if (!status) return;
    lv_label_set_text(status, text.c_str());
    lv_obj_set_style_text_color(status, error ? COLOR_RED : COLOR_SECONDARY_TEXT, 0);
}

struct PinDialogContext {
    PinPurpose purpose = PinPurpose::Unlock;
    lv_obj_t* backdrop = nullptr;
    lv_obj_t* panel = nullptr;
    lv_obj_t* current_field = nullptr;
    lv_obj_t* new_field = nullptr;
    lv_obj_t* confirm_field = nullptr;
    lv_obj_t* status = nullptr;
    lv_obj_t* keyboard = nullptr;
    std::function<void(bool)> done;
    bool busy = false;
};

void submit_pin_dialog(PinDialogContext* ctx);

void clear_pin_fields(PinDialogContext* ctx) {
    if (!ctx) return;
    if (ctx->current_field) lv_textarea_set_text(ctx->current_field, "");
    if (ctx->new_field) lv_textarea_set_text(ctx->new_field, "");
    if (ctx->confirm_field) lv_textarea_set_text(ctx->confirm_field, "");
}

void finish_pin_dialog(PinDialogContext* ctx, bool success) {
    if (!ctx) return;
    auto done = ctx->done;
    const bool was_unlock = ctx->purpose == PinPurpose::Unlock;
    clear_pin_fields(ctx);
    lv_obj_t* backdrop = ctx->backdrop;
    if (g_active_dialog == backdrop) g_active_dialog = nullptr;
    lv_obj_delete(backdrop);
    if (success && was_unlock) security_lock_overlay_hide();
    if (done) done(success);
}

void submit_pin_dialog(PinDialogContext* ctx) {
    if (!ctx || ctx->busy) return;
    ctx->busy = true;
    set_pin_status(ctx->status, "VERIFYING PIN...", false);

    auto& security = prismix::security::SecurityService::instance();
    std::string error;
    bool success = false;

    if (ctx->purpose == PinPurpose::Unlock) {
        const std::string pin = ctx->current_field
            ? std::string(lv_textarea_get_text(ctx->current_field)) : std::string{};
        if (!all_digits(pin) || !prismix::security::SecurityService::isValidPin(pin)) {
            error = "Enter the 6 to 12 digit console PIN.";
        } else {
            int retry_after = 0;
            success = security.unlockMixer(pin, "lvgl-local", error, retry_after);
            if (!success && error.empty()) {
                error = retry_after > 0
                    ? "Too many attempts. Try again in " + std::to_string(retry_after) + " seconds."
                    : "The PIN could not be verified.";
            }
        }
    } else {
        const std::string current = ctx->current_field
            ? std::string(lv_textarea_get_text(ctx->current_field)) : std::string{};
        const std::string next = ctx->new_field
            ? std::string(lv_textarea_get_text(ctx->new_field)) : std::string{};
        const std::string confirm = ctx->confirm_field
            ? std::string(lv_textarea_get_text(ctx->confirm_field)) : std::string{};

        if (ctx->purpose == PinPurpose::Setup) {
            success = security.setupPin(next, confirm, error);
        } else {
            if (!prismix::security::SecurityService::isValidPin(current)) {
                error = "Enter the current 6 to 12 digit PIN.";
            } else {
                int retry_after = 0;
                success = prismix::security::SecurityService::instance().changePin(
                    current, next, confirm, "lvgl-local", error, retry_after);
                if (!success && error.empty()) {
                    error = retry_after > 0
                        ? "Too many attempts. Try again in " + std::to_string(retry_after) + " seconds."
                        : "The PIN could not be changed.";
                }
            }
        }
    }

    if (!success) {
        ctx->busy = false;
        set_pin_status(ctx->status, error.empty() ? "The security operation failed." : error, true);
        return;
    }
    finish_pin_dialog(ctx, true);
}

void pin_dialog_delete_cb(lv_event_t* event) {
    auto* ctx = static_cast<PinDialogContext*>(lv_event_get_user_data(event));
    if (g_active_dialog == (ctx ? ctx->backdrop : nullptr)) g_active_dialog = nullptr;
    delete ctx;
}

void show_pin_dialog(PinPurpose purpose, std::function<void(bool)> done) {
    security_dialog_close_all();

    auto* ctx = new PinDialogContext{};
    ctx->purpose = purpose;
    ctx->done = std::move(done);

    auto* backdrop = make_backdrop();
    ctx->backdrop = backdrop;
    g_active_dialog = backdrop;

    const bool setup = purpose == PinPurpose::Setup;
    const bool change = purpose == PinPurpose::Change;
    const bool unlock = purpose == PinPurpose::Unlock;
    const int panel_height = unlock ? 360 : (setup ? 470 : 520);
    const int panel_offset = unlock ? -220 : (setup ? -160 : -180);

    auto* panel = make_panel(backdrop, 760, panel_height, panel_offset,
                             unlock ? COLOR_ACCENT : COLOR_SELECTED_BORDER);
    ctx->panel = panel;

    const char* title = unlock ? "UNLOCK MIXER"
                       : setup ? "SET UP CONSOLE PIN"
                       : "CHANGE CONSOLE PIN";
    make_label(panel, title, 32, 24, &ac_font_28, COLOR_PRIMARY_TEXT);

    const char* subtitle = unlock
        ? "The audio engine is still running. Enter the mixer PIN to restore console control."
        : (setup
            ? "Use 6 to 12 digits. The PIN is verified by the shared PRISMIX security service and stored only as a salted hash."
            : "Confirm the current PIN, then enter and repeat the new 6 to 12 digit PIN.");
    auto* subtitle_label = make_label(panel, subtitle, 32, 62, &ac_font_12, COLOR_SECONDARY_TEXT);
    lv_obj_set_width(subtitle_label, 696);
    lv_label_set_long_mode(subtitle_label, LV_LABEL_LONG_WRAP);

    int field_y = 118;
    if (change) {
        make_label(panel, "CURRENT PIN", 32, field_y, &ac_font_12, COLOR_SECONDARY_TEXT);
        ctx->current_field = make_pin_field(panel, field_y + 24, "6-12 DIGITS");
        field_y += 82;
    }
    if (unlock) {
        make_label(panel, "CONSOLE PIN", 32, field_y, &ac_font_12, COLOR_SECONDARY_TEXT);
        ctx->current_field = make_pin_field(panel, field_y + 24, "6-12 DIGITS");
        field_y += 88;
    } else {
        make_label(panel, setup ? "NEW PIN" : "NEW PIN", 32, field_y,
                   &ac_font_12, COLOR_SECONDARY_TEXT);
        ctx->new_field = make_pin_field(panel, field_y + 24, "6-12 DIGITS");
        field_y += 82;
        make_label(panel, "CONFIRM NEW PIN", 32, field_y, &ac_font_12, COLOR_SECONDARY_TEXT);
        ctx->confirm_field = make_pin_field(panel, field_y + 24, "REPEAT PIN");
        field_y += 82;
    }

    ctx->status = make_label(panel, "", 32, field_y + 4, &ac_font_12, COLOR_SECONDARY_TEXT);
    lv_obj_set_width(ctx->status, 696);
    lv_label_set_long_mode(ctx->status, LV_LABEL_LONG_WRAP);

    const int footer_y = panel_height - 78;
    auto* cancel = make_button(panel, "CANCEL", 32, footer_y, 210, 52,
                               COLOR_STANDARD_BORDER);
    lv_obj_add_event_cb(cancel, [](lv_event_t* event) {
        auto* ctx = static_cast<PinDialogContext*>(lv_event_get_user_data(event));
        if (!ctx) return;
        finish_pin_dialog(ctx, false);
    }, LV_EVENT_CLICKED, ctx);

    const char* action = unlock ? "UNLOCK" : (setup ? "SAVE PIN" : "CHANGE PIN");
    auto* confirm = make_button(panel, action, 760 - 32 - 320, footer_y, 320, 52,
                                unlock ? COLOR_ACCENT : COLOR_SELECTED_BORDER);
    lv_obj_add_event_cb(confirm, [](lv_event_t* event) {
        submit_pin_dialog(static_cast<PinDialogContext*>(lv_event_get_user_data(event)));
    }, LV_EVENT_CLICKED, ctx);

    auto* keyboard = lv_keyboard_create(backdrop);
    ctx->keyboard = keyboard;
    lv_obj_set_size(keyboard, 840, 300);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, -50);
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_set_style_bg_color(keyboard, COLOR_RECESSED, 0);
    lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(keyboard, COLOR_STANDARD_BORDER, 0);
    lv_obj_set_style_border_width(keyboard, 1, 0);
    lv_obj_set_style_text_color(keyboard, COLOR_PRIMARY_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(keyboard, COLOR_CONTROL, LV_PART_ITEMS);
    lv_obj_set_style_border_color(keyboard, COLOR_STANDARD_BORDER, LV_PART_ITEMS);
    lv_obj_add_event_cb(keyboard, [](lv_event_t* event) {
        submit_pin_dialog(static_cast<PinDialogContext*>(lv_event_get_user_data(event)));
    }, LV_EVENT_READY, ctx);
    lv_obj_t* first = ctx->current_field ? ctx->current_field : ctx->new_field;
    if (first) {
        lv_keyboard_set_textarea(keyboard, first);
        lv_group_t* group = lv_group_get_default();
        if (group) lv_group_focus_obj(first);
    }
    auto focus_field = [](lv_event_t* event) {
        auto* ctx = static_cast<PinDialogContext*>(lv_event_get_user_data(event));
        auto* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
        if (ctx && ctx->keyboard && target) lv_keyboard_set_textarea(ctx->keyboard, target);
    };
    if (ctx->current_field) {
        lv_obj_add_event_cb(ctx->current_field, focus_field, LV_EVENT_CLICKED, ctx);
        lv_obj_add_event_cb(ctx->current_field, focus_field, LV_EVENT_FOCUSED, ctx);
    }
    if (ctx->new_field) {
        lv_obj_add_event_cb(ctx->new_field, focus_field, LV_EVENT_CLICKED, ctx);
        lv_obj_add_event_cb(ctx->new_field, focus_field, LV_EVENT_FOCUSED, ctx);
    }
    if (ctx->confirm_field) {
        lv_obj_add_event_cb(ctx->confirm_field, focus_field, LV_EVENT_CLICKED, ctx);
        lv_obj_add_event_cb(ctx->confirm_field, focus_field, LV_EVENT_FOCUSED, ctx);
    }

    lv_obj_add_event_cb(backdrop, pin_dialog_delete_cb, LV_EVENT_DELETE, ctx);
}

struct ConfirmDialogContext {
    lv_obj_t* backdrop = nullptr;
    std::function<void()> on_confirm;
};

void confirm_dialog_delete_cb(lv_event_t* event) {
    auto* ctx = static_cast<ConfirmDialogContext*>(lv_event_get_user_data(event));
    if (g_active_dialog == (ctx ? ctx->backdrop : nullptr)) g_active_dialog = nullptr;
    delete ctx;
}

void show_confirm_dialog(const std::string& title, const std::string& message,
                         const std::string& confirm_label, bool destructive,
                         std::function<void()> on_confirm) {
    security_dialog_close_all();
    auto* ctx = new ConfirmDialogContext{};
    ctx->on_confirm = std::move(on_confirm);
    auto* backdrop = make_backdrop();
    ctx->backdrop = backdrop;
    g_active_dialog = backdrop;

    const lv_color_t accent = destructive ? COLOR_RED : COLOR_SELECTED_BORDER;
    auto* panel = make_panel(backdrop, 780, 390, -100, accent);

    make_label(panel, title.c_str(), 32, 26, &ac_font_28,
               destructive ? COLOR_RED : COLOR_PRIMARY_TEXT);
    auto* body = make_label(panel, message.c_str(), 32, 84, &ac_font_16, COLOR_PRIMARY_TEXT);
    lv_obj_set_width(body, 716);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);

    auto* cancel = make_button(panel, "CANCEL", 32, 300, 220, 56,
                               COLOR_STANDARD_BORDER);
    lv_obj_add_event_cb(cancel, [](lv_event_t* event) {
        auto* ctx = static_cast<ConfirmDialogContext*>(lv_event_get_user_data(event));
        if (!ctx) return;
        ctx->on_confirm = {};
        lv_obj_t* backdrop = ctx->backdrop;
        if (g_active_dialog == backdrop) g_active_dialog = nullptr;
        lv_obj_delete(backdrop);
    }, LV_EVENT_CLICKED, ctx);

    auto* confirm = make_button(panel, confirm_label.c_str(), 780 - 32 - 330, 300, 330, 56,
                                accent);
    lv_obj_add_event_cb(confirm, [](lv_event_t* event) {
        auto* ctx = static_cast<ConfirmDialogContext*>(lv_event_get_user_data(event));
        if (!ctx) return;
        auto action = ctx->on_confirm;
        ctx->on_confirm = {};
        lv_obj_t* backdrop = ctx->backdrop;
        if (g_active_dialog == backdrop) g_active_dialog = nullptr;
        lv_obj_delete(backdrop);
        if (action) action();
    }, LV_EVENT_CLICKED, ctx);

    lv_obj_add_event_cb(backdrop, confirm_dialog_delete_cb, LV_EVENT_DELETE, ctx);
}

struct MessageDialogContext {
    lv_obj_t* backdrop = nullptr;
};

void message_dialog_delete_cb(lv_event_t* event) {
    auto* ctx = static_cast<MessageDialogContext*>(lv_event_get_user_data(event));
    if (g_active_dialog == (ctx ? ctx->backdrop : nullptr)) g_active_dialog = nullptr;
    delete ctx;
}

void show_message_dialog(const std::string& title, const std::string& message) {
    security_dialog_close_all();
    auto* ctx = new MessageDialogContext{};
    auto* backdrop = make_backdrop();
    ctx->backdrop = backdrop;
    g_active_dialog = backdrop;

    auto* panel = make_panel(backdrop, 760, 340, -80, COLOR_STANDARD_BORDER);
    make_label(panel, title.c_str(), 32, 26, &ac_font_28, COLOR_PRIMARY_TEXT);
    auto* body = make_label(panel, message.c_str(), 32, 88, &ac_font_16, COLOR_PRIMARY_TEXT);
    lv_obj_set_width(body, 696);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);

    auto* close = make_button(panel, "CLOSE", 760 - 32 - 220, 260, 220, 52,
                              COLOR_SELECTED_BORDER);
    lv_obj_add_event_cb(close, [](lv_event_t*) {
        security_dialog_close_all();
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_add_event_cb(backdrop, message_dialog_delete_cb, LV_EVENT_DELETE, ctx);
}

void lock_overlay_delete_cb(lv_event_t*) {
    g_lock_overlay = nullptr;
}

void show_lock_overlay_impl() {
    if (g_lock_overlay) return;
    security_dialog_close_all();

    auto* overlay = lv_obj_create(lv_layer_top());
    g_lock_overlay = overlay;
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_size(overlay, kScreenWidth, kScreenHeight);
    lv_obj_remove_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x050706), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_90, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);

    auto* panel = make_panel(overlay, 820, 430, 0, COLOR_RED);
    make_label(panel, "MIXER LOCKED", 40, 46, &ac_font_40, COLOR_RED);
    auto* subtitle = make_label(panel,
        "Console controls are disabled until the mixer PIN is verified by the security service.",
        40, 112, &ac_font_16, COLOR_PRIMARY_TEXT);
    lv_obj_set_width(subtitle, 740);
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_WRAP);

    make_label(panel, "AUDIO ENGINE CONTINUES", 40, 204, &ac_font_20, COLOR_GREEN);
    make_label(panel, "Mix state and DSP are preserved. No channel is muted and audio is not stopped.",
               40, 240, &ac_font_14, COLOR_SECONDARY_TEXT);

    auto* unlock = make_button(panel, "UNLOCK MIXER", 250, 312, 320, 62, COLOR_ACCENT);
    lv_obj_add_event_cb(unlock, [](lv_event_t*) {
        security_dialog_show_pin(PinPurpose::Unlock);
    }, LV_EVENT_CLICKED, nullptr);

    lv_group_t* group = lv_group_get_default();
    if (group) lv_group_focus_obj(unlock);

    lv_obj_add_event_cb(overlay, lock_overlay_delete_cb, LV_EVENT_DELETE, nullptr);
}

void hide_lock_overlay_impl() {
    if (!g_lock_overlay) return;
    lv_obj_t* overlay = g_lock_overlay;
    g_lock_overlay = nullptr;
    lv_obj_delete(overlay);
}

} // namespace

void security_dialog_show_pin(PinPurpose purpose, std::function<void(bool)> done) {
    show_pin_dialog(purpose, std::move(done));
}

void security_dialog_show_confirm(const std::string& title, const std::string& message,
                                  const std::string& confirm_label, bool destructive,
                                  std::function<void()> on_confirm) {
    show_confirm_dialog(title, message, confirm_label, destructive, std::move(on_confirm));
}

void security_dialog_show_message(const std::string& title, const std::string& message) {
    show_message_dialog(title, message);
}

void security_dialog_close_all() {
    close_active_dialog();
}

void security_lock_overlay_sync() {
    const bool locked = prismix::security::SecurityService::instance().isMixerLocked();
    if (locked && !g_lock_overlay) {
        show_lock_overlay_impl();
    } else if (!locked && g_lock_overlay) {
        hide_lock_overlay_impl();
    }
}

void security_lock_overlay_show() {
    show_lock_overlay_impl();
}

void security_lock_overlay_hide() {
    hide_lock_overlay_impl();
}

bool security_lock_overlay_visible() {
    return g_lock_overlay != nullptr;
}

} // namespace prismix::ui
