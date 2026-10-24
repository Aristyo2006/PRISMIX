#include "theme.h"
#include "../assets/rack_textures.h"
#include "../ui.h"

extern void theme_register_change_callback(void (*cb)());

static lv_style_t style_screen;

void theme_update_screen_style(void) {
    lv_style_set_bg_color(&style_screen, COLOR_BG);
    lv_style_set_text_color(&style_screen, COLOR_TEXT);
    lv_obj_report_style_change(&style_screen);
}

static void on_theme_model_changed() {
    theme_update_screen_style();
    ui::rack_skin::regenerate_textures_for_theme();
    ui_refresh_theme();
}

void theme_init(void) {
    theme_init_and_load();
    theme_register_change_callback(on_theme_model_changed);

    lv_style_init(&style_screen);
    lv_style_set_bg_color(&style_screen, COLOR_BG);
    lv_style_set_bg_opa(&style_screen, LV_OPA_COVER);
    lv_style_set_text_color(&style_screen, COLOR_TEXT);
    lv_style_set_text_font(&style_screen, &ac_font_14);

    lv_obj_add_style(lv_screen_active(), &style_screen, 0);
}
