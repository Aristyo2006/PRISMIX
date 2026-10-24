#pragma once
#include "lvgl.h"
#include "theme_model.h"

// -----------------------------------------------------------------------------
// PRISMIX Centralized Semantic Hardware Theme Engine
// Evaluates active theme colors dynamically at runtime
// -----------------------------------------------------------------------------
inline lv_color_t theme_get_color(ThemeColorRole role) {
    return lv_color_hex(theme_get_color_hex(role));
}

// -----------------------------------------------------------------------------
// Core Semantic Hardware Roles
// -----------------------------------------------------------------------------
#define COLOR_BG                theme_get_color(ThemeColorRole::Background)
#define COLOR_CHASSIS           theme_get_color(ThemeColorRole::Panel)
#define COLOR_PANEL_SURFACE     theme_get_color(ThemeColorRole::Panel)
#define COLOR_RAISED_PANEL      theme_get_color(ThemeColorRole::ControlSurface)
#define COLOR_RECESSED          theme_get_color(ThemeColorRole::RecessedSurface)
#define COLOR_STANDARD_BORDER   theme_get_color(ThemeColorRole::StandardBorder)
#define COLOR_SUBTLE_SEPARATOR  theme_get_color(ThemeColorRole::StandardBorder)
#define COLOR_PRIMARY_TEXT      theme_get_color(ThemeColorRole::TextPrimary)
#define COLOR_SECONDARY_TEXT    theme_get_color(ThemeColorRole::TextSecondary)
#define COLOR_DISABLED_TEXT     theme_get_color(ThemeColorRole::TextSecondary)
#define COLOR_NEUTRAL_HIGHLIGHT theme_get_color(ThemeColorRole::SelectedBorder)
#define COLOR_KNOB_RING         theme_get_color(ThemeColorRole::KnobRing)
#define COLOR_KNOB_INDICATOR    theme_get_color(ThemeColorRole::KnobPointer)
#define COLOR_SELECTED_BORDER   theme_get_color(ThemeColorRole::SelectedBorder)

// -----------------------------------------------------------------------------
// Component & Theme Aliases
// -----------------------------------------------------------------------------
#define COLOR_PANEL             COLOR_PANEL_SURFACE
#define COLOR_METAL_PANEL       COLOR_PANEL_SURFACE
#define COLOR_GUNMETAL          COLOR_PANEL_SURFACE
#define COLOR_CONTROL           COLOR_RAISED_PANEL
#define COLOR_LCD_BG            COLOR_RECESSED
#define COLOR_LCD_TEXT          COLOR_PRIMARY_TEXT
#define COLOR_TEXT_PRIMARY      COLOR_PRIMARY_TEXT
#define COLOR_TEXT              COLOR_PRIMARY_TEXT
#define COLOR_TEXT_MUTED        COLOR_SECONDARY_TEXT
#define COLOR_TEXT2             COLOR_SECONDARY_TEXT
#define COLOR_SUBTLE_BORDER     COLOR_STANDARD_BORDER
#define COLOR_HW_BORDER         COLOR_STANDARD_BORDER
#define COLOR_EDGE              COLOR_STANDARD_BORDER
#define COLOR_SEPARATOR         COLOR_SUBTLE_SEPARATOR
#define COLOR_DISABLED          COLOR_DISABLED_TEXT
#define COLOR_SILVER            COLOR_KNOB_INDICATOR
#define COLOR_ACCENT            theme_get_color(ThemeColorRole::AccentPrimary)
#define COLOR_HIGHLIGHT         COLOR_NEUTRAL_HIGHLIGHT

#define COLOR_CHAMPAGNE         theme_get_color(ThemeColorRole::AccentPrimary)
#define COLOR_CHAMPAGNE_HI      COLOR_KNOB_INDICATOR
#define COLOR_MUTED_GOLD        COLOR_KNOB_RING

// -----------------------------------------------------------------------------
// Functional Indicators & Semantic Status Colors (strictly preserved)
// -----------------------------------------------------------------------------
#define COLOR_CYAN              theme_get_color(ThemeColorRole::AccentSecondary)
#define COLOR_GREEN             lv_color_hex(0x22C55E)  // Signal present / active RUNNING / Start Audio
#define COLOR_YELLOW            lv_color_hex(0xEAB308)  // Functional warning / amber (telemetry alerts only)
#define COLOR_RED               lv_color_hex(0xEF4444)  // Clipping / error / mute / destructive actions
#define COLOR_ORANGE            lv_color_hex(0xF97316)  // Bypass / caution

// Fonts
#ifdef __cplusplus
extern "C" {
#endif
// Clean Technical Sans-Serif (Section headings, buttons, general navigation)
LV_FONT_DECLARE(ac_font_12);
LV_FONT_DECLARE(ac_font_14);
LV_FONT_DECLARE(ac_font_16);
LV_FONT_DECLARE(ac_font_20);
LV_FONT_DECLARE(ac_font_28);
LV_FONT_DECLARE(ac_font_40);

// Graphic LCD Bitmap Typography (Unscii-8 family: presets, parameters, numeric readouts)
LV_FONT_DECLARE(lv_font_unscii_8);
LV_FONT_DECLARE(lv_font_unscii_16);
LV_FONT_DECLARE(ac_font_lcd_24);
#ifdef __cplusplus
}
#endif

void theme_init(void);
void theme_update_screen_style(void);
