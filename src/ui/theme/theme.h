#pragma once
#include "lvgl.h"

// Colors
#define COLOR_BG        lv_color_hex(0x080A0C)
#define COLOR_PANEL     lv_color_hex(0x15181C)
#define COLOR_RECESSED  lv_color_hex(0x090B0D)
#define COLOR_CONTROL   lv_color_hex(0x23272B)
#define COLOR_EDGE      lv_color_hex(0x3B4045)
#define COLOR_HIGHLIGHT lv_color_hex(0x555B60)
#define COLOR_TEXT      lv_color_hex(0xE8E8E8)
#define COLOR_TEXT2     lv_color_hex(0x8A9096)

#define COLOR_CYAN      lv_color_hex(0x00E5FF)
#define COLOR_GREEN     lv_color_hex(0x00E676)
#define COLOR_YELLOW    lv_color_hex(0xFFEA00)
#define COLOR_RED       lv_color_hex(0xFF1744)
#define COLOR_ORANGE    lv_color_hex(0xFF9100)

// Fonts
#ifdef __cplusplus
extern "C" {
#endif
LV_FONT_DECLARE(ac_font_12);
LV_FONT_DECLARE(ac_font_14);
LV_FONT_DECLARE(ac_font_16);
LV_FONT_DECLARE(ac_font_20);
LV_FONT_DECLARE(ac_font_28);
LV_FONT_DECLARE(ac_font_40);
#ifdef __cplusplus
}
#endif

void theme_init(void);
