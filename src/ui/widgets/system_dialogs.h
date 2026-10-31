#pragma once

#include "lvgl.h"

#include <functional>
#include <string>

namespace prismix::ui {

enum class PinPurpose {
    Setup,
    Change,
    Unlock,
};

// Reusable, touch-friendly security and confirmation dialogs rendered above the
// normal console. Only one dialog is active at a time.
void security_dialog_show_pin(PinPurpose purpose, std::function<void(bool)> done = {});
void security_dialog_show_confirm(const std::string& title, const std::string& message,
                                  const std::string& confirm_label, bool destructive,
                                  std::function<void()> on_confirm);
void security_dialog_show_message(const std::string& title, const std::string& message);
void security_dialog_close_all();

// Full-screen lock gate. It blocks pointer/keyboard focus on the console while
// the audio engine and all DSP/mix state continue running unchanged.
void security_lock_overlay_sync();
void security_lock_overlay_show();
void security_lock_overlay_hide();
bool security_lock_overlay_visible();

} // namespace prismix::ui
