#pragma once

#include "lvgl.h"

// Shows the modal dialog for configuring Audio devices, sample rate, and buffer size.
void show_audio_setup_dialog();

// Dismisses the modal dialog if open.
void dismiss_audio_setup_dialog();

// Returns true if audio setup dialog is currently open.
bool is_audio_setup_dialog_open();

