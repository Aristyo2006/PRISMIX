#pragma once
#include "lvgl.h"

lv_obj_t* mixer_screen_create(void);
void mixer_screen_select_channel(int index);
void mixer_screen_show_rename(int index);
void mixer_screen_show_delete(int index);
void mixer_screen_add_channel(void);
void mixer_screen_show_channel_setup(int channel_id);
void mixer_screen_delete_channel(int index);
void mixer_screen_refresh_channel_by_id(int id);
void mixer_screen_dismiss_modal(void);
void mixer_screen_sync_after_recall(void);
