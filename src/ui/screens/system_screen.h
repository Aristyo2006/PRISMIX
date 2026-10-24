#pragma once
#include "lvgl.h"

lv_obj_t* system_screen_create(void);
void system_screen_set_mode(int mode);
void system_screen_refresh_theme(void);
