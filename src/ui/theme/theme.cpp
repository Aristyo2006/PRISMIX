#include "theme.h"

static lv_style_t style_screen;

void theme_init(void) {
    lv_style_init(&style_screen);
    lv_style_set_bg_color(&style_screen, COLOR_BG);
    lv_style_set_bg_opa(&style_screen, LV_OPA_COVER);
    lv_style_set_text_color(&style_screen, COLOR_TEXT);
    lv_style_set_text_font(&style_screen, &ac_font_14);

    lv_obj_add_style(lv_screen_active(), &style_screen, 0);
}
