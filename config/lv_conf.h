/**
 * AudioCore - LVGL configuration (LVGL v9.4.0)
 *
 * Only options that differ from LVGL defaults are set here.
 * Everything else falls back to lv_conf_internal.h defaults.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

/*====================
   COLOR / DISPLAY
 *====================*/
#define LV_COLOR_DEPTH 32

/*====================
   MEMORY
 *====================*/
/* Use the C library allocator: no fixed pool limit on desktop. */
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_CLIB
#define LV_USE_STDLIB_STRING    LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_CLIB

/*====================
   HAL
 *====================*/
#define LV_DEF_REFR_PERIOD  16      /* ~60 Hz refresh */
#define LV_DPI_DEF          160

/*====================
   RENDERING
 *====================*/
#define LV_USE_DRAW_SW 1
#define LV_DRAW_SW_COMPLEX 1
#define LV_DRAW_LAYER_SIMPLE_BUF_SIZE (64 * 1024)
#define LV_DRAW_SW_DRAW_UNIT_CNT 1

/*====================
   FEATURES
 *====================*/
#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1

#define LV_USE_FLOAT 0
#define LV_USE_OBJ_ID 0
#define LV_USE_SYSMON 0
#define LV_USE_PERF_MONITOR 0

/*====================
   FONTS
 *====================*/
/* Built-in Montserrat kept as last-resort fallback only.
 * AudioCore uses generated IBM Plex Sans Condensed fonts (src/ui/fonts). */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

/*====================
   WIDGETS (only what is used)
 *====================*/
#define LV_USE_LABEL 1
#define LV_USE_ARC 1
#define LV_USE_BAR 1
#define LV_USE_BUTTON 1
#define LV_USE_CHART 1
#define LV_USE_SWITCH 1
#define LV_USE_SLIDER 1
#define LV_USE_SCALE 1
#define LV_LABEL_TEXT_SELECTION 0
#define LV_LABEL_LONG_TXT_HINT 1

/*====================
   THEMES
 *====================*/
#define LV_USE_THEME_DEFAULT 0
#define LV_USE_THEME_SIMPLE 0
#define LV_USE_THEME_MONO 0

/*====================
   DEVICES - SDL2
 *====================*/
#define LV_USE_SDL              1
#define LV_SDL_INCLUDE_PATH     <SDL.h>
#define LV_SDL_RENDER_MODE      LV_DISPLAY_RENDER_MODE_DIRECT
#define LV_SDL_BUF_COUNT        1
#define LV_SDL_ACCELERATED      1
#define LV_SDL_FULLSCREEN       0
#define LV_SDL_DIRECT_EXIT      1
#define LV_SDL_MOUSEWHEEL_MODE  LV_SDL_MOUSEWHEEL_MODE_ENCODER

/*====================
   BUILD
 *====================*/
#define LV_BUILD_EXAMPLES 0
#define LV_BUILD_DEMOS 0

#endif /* LV_CONF_H */
