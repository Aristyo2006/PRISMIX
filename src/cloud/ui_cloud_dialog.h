#pragma once

#include "lvgl.h"

namespace Prismix {
namespace Cloud {

// Opens the PRISMIX Cloud Preset Manager modal dialog.
void show_cloud_preset_dialog();

// Closes the dialog if currently visible.
void dismiss_cloud_preset_dialog();

// Checks if the dialog is currently open.
bool is_cloud_preset_dialog_open();

} // namespace Cloud
} // namespace Prismix

