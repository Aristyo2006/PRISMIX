#pragma once

void ui_init(void);
void ui_update_status_bar(void);
void ui_on_channel_changed(void);
void ui_sync_all_screens(void);
void ui_refresh_after_preset_recall(void);
void ui_nav_to_tab(int tab_index);
int ui_get_active_tab(void);
bool ui_nav_button_is_enabled(int tab_index);
bool ui_simulator_controls_are_blocked(int tab_index);
