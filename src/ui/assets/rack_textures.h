#pragma once
#include "lvgl.h"

namespace ui::rack_skin {

// Initializes procedural brushed-metal tiles and hardware sprites (idempotent).
void init_textures();

// Regenerates procedural brushed tiles to match the active theme's colors
void regenerate_textures_for_theme();

// Seamless 64x32 dark anodized aluminum chassis texture tile.
const lv_image_dsc_t* chassis_brushed_tile();

// Seamless 64x32 gunmetal rack faceplate brushed texture tile.
const lv_image_dsc_t* faceplate_brushed_tile();

// Seamless 64x32 vintage grey brushed metal faceplate texture tile.
const lv_image_dsc_t* grey_faceplate_brushed_tile();

// Seamless 32x32 dark charcoal recessed bay texture tile.
const lv_image_dsc_t* recessed_bay_tile();

// Seamless 8x8 ARGB8888 dot-matrix mask texture tile for authentic physical graphic LCD displays.
const lv_image_dsc_t* lcd_dot_matrix_tile();

// 14x14 ARGB8888 recessed hex/Torx rack screw head sprite.
const lv_image_dsc_t* rack_screw_icon();

// Applies the physical graphic LCD dot-matrix mask and recessed edge shading across an LCD viewport.
void draw_lcd_dot_matrix_mask(lv_layer_t* layer, const lv_area_t* coords);

// Styles an LVGL object as a dark anodized rack chassis container.
void apply_chassis_panel(lv_obj_t* obj);
void apply_chassis_panel(lv_obj_t* obj, lv_color_t border_color);

// Styles an LVGL object as a brushed gunmetal hardware rack module faceplate
// with top bevel highlight, dark outer seam, and optional 4 corner mounting screws.
void apply_faceplate_panel(lv_obj_t* obj, bool add_corner_screws = true);
void apply_faceplate_panel(lv_obj_t* obj, lv_color_t border_color, lv_color_t bg_tint);

// Styles an LVGL object as a vintage grey brushed metal hardware rack faceplate.
void apply_grey_faceplate_panel(lv_obj_t* obj, bool add_corner_screws = true);
void apply_grey_faceplate_panel(lv_obj_t* obj, lv_color_t border_color, lv_color_t bg_tint);

// Styles an LVGL object as a compact physical console channel strip.
void apply_channel_strip_panel(lv_obj_t* obj, bool is_selected);

// Styles an LVGL object as a recessed dark-acrylic display / meter bay with inner shadow frame.
void apply_recessed_display(lv_obj_t* obj, lv_color_t accent_border = lv_color_hex(0x28323C));
void apply_recessed_display(lv_obj_t* obj, lv_color_t bg_color, lv_color_t accent_border);

// Styles an LVGL button as a physical machined rack pushbutton with tactile bevel.
void apply_hardware_button(lv_obj_t* btn, lv_color_t body_color, lv_color_t border_color, lv_color_t text_color);
void apply_hardware_button(lv_obj_t* btn, bool active, lv_color_t active_border, lv_color_t active_bg, lv_color_t inactive_bg);

// Creates a single 14x14 recessed rack screw at (x, y) or aligned on parent.
lv_obj_t* create_rack_screw(lv_obj_t* parent, lv_align_t align, int32_t x_ofs, int32_t y_ofs);
lv_obj_t* create_rack_screw(lv_obj_t* parent, int32_t x, int32_t y);

// Creates 4 corner mounting screws on a rack faceplate.
void add_faceplate_corner_screws(lv_obj_t* faceplate, int32_t inset_x = 6, int32_t inset_y = 6);
void add_faceplate_corner_screws(lv_obj_t* faceplate, int32_t w, int32_t h, int32_t inset);

// Creates a bezel-mounted hardware LED indicator (metallic bezel ring + lens).
lv_obj_t* create_bezel_led(lv_obj_t* parent, lv_color_t color, bool active);
lv_obj_t* create_bezel_led(lv_obj_t* parent, int32_t x, int32_t y, lv_color_t color, bool active);

// Updates an existing bezel LED indicator's color and active state.
void set_bezel_led_state(lv_obj_t* led, lv_color_t color, bool active);

} // namespace ui::rack_skin
