#include "ui.h"

#include "theme/theme.h"
#include "screens/mixer_screen.h"
#include "screens/channel_screen.h"
#include "screens/eq_screen.h"
#include "screens/dynamics_screen.h"
#include "screens/fx_screen.h"
#include "screens/routing_screen.h"
#include "screens/system_screen.h"
#include "widgets/instrument.h"
#include "widgets/audio_setup_dialog.h"
#include "widgets/system_dialogs.h"
#include "assets/rack_textures.h"
#include "ui_audio_bridge.h"
#include "../model/mixer_state.h"
#include "../service/audio_service.h"
#include "../backend/mock_audio_backend.h"

static lv_obj_t* channel_status{};
static lv_obj_t* engine_status{};
static lv_obj_t* status_badge{};
static lv_obj_t* status_badge_label{};
static lv_obj_t* start_stop_btn{};
static lv_obj_t* start_stop_label{};
static lv_obj_t* audio_setup_btn{};
static lv_obj_t* s_top_bar{};
static lv_obj_t* s_bottom_bar{};

void ui_update_status_bar() {
    if (!channel_status) return;
    if (g_mixer_state.channels.empty()) {
        lv_label_set_text(channel_status, "NO INPUT CHANNELS  /  ADD A CHANNEL TO BEGIN");
    } else if (g_mixer_state.selected_channel == -1) {
        lv_label_set_text(channel_status, "SELECTED: MASTER / MAIN L/R");
    } else if (g_mixer_state.selected_channel >= 0 && g_mixer_state.selected_channel < (int)g_mixer_state.channels.size()) {
        auto* ch = mixer_get_channel(g_mixer_state.selected_channel);
        if (ch) {
            lv_label_set_text_fmt(channel_status, "SELECTED: CH %02d / %s", g_mixer_state.selected_channel + 1, ch->name);
        }
    }
}

namespace {
lv_obj_t* screens[7]{};
lv_obj_t* nav_buttons[7]{};
int active_tab = 0;
const char* tabs[] = {"MIXER", "CHANNEL", "EQ", "DYNAMICS", "FX", "ROUTING", "SYSTEM"};

bool liveControlsLocked() {
    // Realtime DSP mixer allows continuous interactive mixing while live streaming
    return false;
}

void paint_nav() {
    for (int i = 0; i < 7; ++i) {
        const bool selected = (i == active_tab);
        if (nav_buttons[i]) {
            lv_obj_remove_state(nav_buttons[i], LV_STATE_DISABLED);
            lv_obj_set_style_bg_color(nav_buttons[i], selected ? COLOR_RAISED_PANEL : COLOR_PANEL_SURFACE, 0);
            lv_obj_set_style_border_color(nav_buttons[i], selected ? COLOR_SELECTED_BORDER : COLOR_STANDARD_BORDER, 0);
            lv_obj_set_style_border_width(nav_buttons[i], selected ? 2 : 1, 0);
            if (lv_obj_get_child_count(nav_buttons[i]) > 0) {
                lv_obj_set_style_text_color(lv_obj_get_child(nav_buttons[i], 0),
                    selected ? COLOR_PRIMARY_TEXT : COLOR_SECONDARY_TEXT, 0);
            }
        }
    }
}

void nav_event(lv_event_t* e) {
    const int new_tab = (int)(intptr_t)lv_event_get_user_data(e);
    ui_nav_to_tab(new_tab);
}

void status_event(lv_timer_t*) {
    prismix::ui::security_lock_overlay_sync();
    ui_update_status_bar();
    paint_nav();
    const auto snapshot = audio_service().snapshot();

    // 1. Update Status Badge
    if (status_badge && status_badge_label) {
        if (snapshot.state == AudioServiceState::Streaming) {
            lv_obj_set_style_bg_color(status_badge, lv_color_hex(0x0E3622), 0);
            lv_obj_set_style_border_color(status_badge, lv_color_hex(0x1B7A42), 0);
            lv_obj_set_style_text_color(status_badge_label, COLOR_GREEN, 0);
            lv_label_set_text(status_badge_label, "● RUNNING");
        } else if (snapshot.state == AudioServiceState::Initializing) {
            lv_obj_set_style_bg_color(status_badge, lv_color_hex(0x3B3012), 0);
            lv_obj_set_style_border_color(status_badge, lv_color_hex(0x7A601B), 0);
            lv_obj_set_style_text_color(status_badge_label, COLOR_ORANGE, 0);
            lv_label_set_text(status_badge_label, "● STARTING");
        } else if (snapshot.state == AudioServiceState::Error) {
            lv_obj_set_style_bg_color(status_badge, lv_color_hex(0x3D1212), 0);
            lv_obj_set_style_border_color(status_badge, lv_color_hex(0x852020), 0);
            lv_obj_set_style_text_color(status_badge_label, COLOR_RED, 0);
            lv_label_set_text(status_badge_label, "● ERROR");
        } else {
            lv_obj_set_style_bg_color(status_badge, lv_color_hex(0x22262B), 0);
            lv_obj_set_style_border_color(status_badge, lv_color_hex(0x3A424A), 0);
            lv_obj_set_style_text_color(status_badge_label, lv_color_hex(0x9AA0A6), 0);
            lv_label_set_text(status_badge_label, "● STOPPED");
        }
    }

    // 2. Update Active Stream Summary
    if (engine_status) {
        if (snapshot.mode == AudioMode::Simulation) {
            const auto config = g_backend.get_config();
            lv_label_set_text_fmt(engine_status, "SIMULATION  •  %d Hz  •  Mock Audio Engine  •  CPU %.1f%%  •  XRUN %u",
                                  static_cast<int>(config.sample_rate), config.cpu_load, config.xruns);
            lv_obj_set_style_text_color(engine_status, COLOR_TEXT2, 0);
        } else if (snapshot.state == AudioServiceState::Streaming) {
            const char* devName = snapshot.actualCaptureDevice.empty() ? "JACK" : snapshot.actualCaptureDevice.c_str();
            const float latMs = (static_cast<float>(snapshot.actualBufferFrames) * 1000.0f) /
                                (snapshot.actualSampleRate > 0 ? static_cast<float>(snapshot.actualSampleRate) : 48000.0f);
            lv_label_set_text_fmt(engine_status, "%s  •  %.0f Hz  •  %u frames (%.1f ms)  •  DSP %llu us  •  XRUN %u",
                                  devName, snapshot.actualSampleRate, snapshot.actualBufferFrames,
                                  latMs, static_cast<unsigned long long>(snapshot.dspTimeUs), snapshot.underruns);
            lv_obj_set_style_text_color(engine_status, COLOR_TEXT, 0);
        } else if (snapshot.state == AudioServiceState::Error) {
            lv_label_set_text_fmt(engine_status, "JACK Error: %s",
                                  snapshot.error.empty() ? "Backend initialization failed" : snapshot.error.c_str());
            lv_obj_set_style_text_color(engine_status, COLOR_RED, 0);
        } else {
            const auto& setup = ui_get_persisted_audio_setup();
            const char* dev = setup.captureDeviceId.empty() ? "System Default" : setup.captureDeviceId.c_str();
            lv_label_set_text_fmt(engine_status, "Native JACK  •  %s  •  %u Hz  •  %u frames  •  Ready",
                                  dev, setup.requestedSampleRate > 0 ? setup.requestedSampleRate : 48000,
                                  setup.requestedBufferFrames > 0 ? setup.requestedBufferFrames : 128);
            lv_obj_set_style_text_color(engine_status, COLOR_TEXT2, 0);
        }
    }

    // 3. Update Start/Stop Button
    if (start_stop_btn && start_stop_label) {
        if (snapshot.state == AudioServiceState::Streaming) {
            lv_obj_set_style_bg_color(start_stop_btn, lv_color_hex(0x3B1616), 0);
            lv_obj_set_style_border_color(start_stop_btn, COLOR_RED, 0);
            lv_obj_set_style_text_color(start_stop_label, COLOR_RED, 0);
            lv_label_set_text(start_stop_label, "■ STOP AUDIO");
            lv_obj_remove_state(start_stop_btn, LV_STATE_DISABLED);
        } else if (snapshot.state == AudioServiceState::Initializing) {
            lv_obj_set_style_bg_color(start_stop_btn, lv_color_hex(0x3B3012), 0);
            lv_obj_set_style_border_color(start_stop_btn, COLOR_ORANGE, 0);
            lv_obj_set_style_text_color(start_stop_label, COLOR_ORANGE, 0);
            lv_label_set_text(start_stop_label, "⏳ STARTING...");
            lv_obj_add_state(start_stop_btn, LV_STATE_DISABLED);
        } else {
            lv_obj_set_style_bg_color(start_stop_btn, lv_color_hex(0x133824), 0);
            lv_obj_set_style_border_color(start_stop_btn, COLOR_GREEN, 0);
            lv_obj_set_style_text_color(start_stop_label, COLOR_GREEN, 0);
            lv_label_set_text(start_stop_label, "▶ START AUDIO");
            lv_obj_remove_state(start_stop_btn, LV_STATE_DISABLED);
        }
    }
}
} // anonymous namespace

static lv_obj_t* create_screen_for_tab(int tab) {
    switch (tab) {
        case 0: return mixer_screen_create();
        case 1: return channel_screen_create();
        case 2: return eq_screen_create();
        case 3: return dynamics_screen_create();
        case 4: return fx_screen_create();
        case 5: return routing_screen_create();
        case 6: return system_screen_create();
        default: return nullptr;
    }
}

void ui_nav_to_tab(int new_tab) {
    if (new_tab < 0 || new_tab >= 7) return;
    if (new_tab == active_tab) return; // Redundant transition ignored

    // 1. Update active_tab immediately
    const int prev_tab = active_tab;
    active_tab = new_tab;

    // 2. Hide previous screen
    if (screens[prev_tab]) {
        lv_obj_add_flag(screens[prev_tab], LV_OBJ_FLAG_HIDDEN);
    }

    // 3. Instantiate or update target screen
    if (new_tab == 0) {
        // Tab 0 (MIXER) is recreated if deleted (e.g. following a theme reload),
        // or reused with selection visuals updated without full screen recreation.
        if (!screens[0]) {
            screens[0] = mixer_screen_create();
        } else {
            mixer_screen_sync_selection_visuals();
        }
    } else {
        // Tabs 1-6 track the currently selected channel strip or dynamic hardware state
        if (screens[new_tab]) {
            lv_obj_delete(screens[new_tab]);
            screens[new_tab] = nullptr;
        }
        screens[new_tab] = create_screen_for_tab(new_tab);
    }

    // 4. STRICT INVARIANT: Ensure ALL screens other than active_tab are hidden,
    // and ONLY active_tab is visible. This guarantees no accidental overlapping screens.
    for (int i = 0; i < 7; ++i) {
        if (screens[i]) {
            if (i == active_tab) {
                lv_obj_remove_flag(screens[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(screens[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    paint_nav();
}

int ui_get_active_tab(void) {
    return active_tab;
}

bool ui_nav_button_is_enabled(int tab) {
    return tab >= 0 && tab < 7 && nav_buttons[tab] &&
           !lv_obj_has_state(nav_buttons[tab], LV_STATE_DISABLED) &&
           lv_obj_has_flag(nav_buttons[tab], LV_OBJ_FLAG_CLICKABLE);
}

bool ui_simulator_controls_are_blocked(int tab) {
    (void)tab;
    return false;
}

void ui_on_channel_changed() {
    ui_update_status_bar();
    // Refresh currently visible channel-dependent screen
    if (active_tab >= 1 && active_tab <= 5) {
        if (screens[active_tab]) {
            lv_obj_delete(screens[active_tab]);
            screens[active_tab] = nullptr;
        }
        screens[active_tab] = create_screen_for_tab(active_tab);
        if (screens[active_tab]) {
            lv_obj_remove_flag(screens[active_tab], LV_OBJ_FLAG_HIDDEN);
        }
        // Strict invariant: Ensure all other screens remain hidden
        for (int i = 0; i < 7; ++i) {
            if (i != active_tab && screens[i]) {
                lv_obj_add_flag(screens[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

void ui_sync_all_screens() {
    ui_on_channel_changed();
}

void ui_refresh_theme() {
    if (s_top_bar) {
        lv_obj_set_style_bg_color(s_top_bar, COLOR_PANEL, 0);
        lv_obj_set_style_border_color(s_top_bar, COLOR_STANDARD_BORDER, 0);
    }
    if (s_bottom_bar) {
        lv_obj_set_style_bg_color(s_bottom_bar, COLOR_PANEL, 0);
        lv_obj_set_style_border_color(s_bottom_bar, COLOR_STANDARD_BORDER, 0);
    }
    if (audio_setup_btn) {
        lv_obj_set_style_bg_color(audio_setup_btn, COLOR_CONTROL, 0);
        lv_obj_set_style_border_color(audio_setup_btn, COLOR_STANDARD_BORDER, 0);
    }

    paint_nav();
    ui_update_status_bar();

    // Recreate or refresh currently visible screen
    if (active_tab == 6) {
        system_screen_refresh_theme();
    } else {
        if (screens[active_tab]) {
            lv_obj_delete(screens[active_tab]);
            screens[active_tab] = nullptr;
        }
        screens[active_tab] = create_screen_for_tab(active_tab);
        if (screens[active_tab]) {
            lv_obj_remove_flag(screens[active_tab], LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Invalidate cached inactive screens so they are cleanly created on navigation
    for (int i = 0; i < 7; ++i) {
        if (i != active_tab && screens[i]) {
            lv_obj_delete(screens[i]);
            screens[i] = nullptr;
        }
    }

    // Strict invariant: Ensure all other screens remain hidden
    for (int i = 0; i < 7; ++i) {
        if (screens[i]) {
            if (i == active_tab) {
                lv_obj_remove_flag(screens[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(screens[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    lv_obj_invalidate(lv_screen_active());
}

void ui_init(void) {
    theme_init();
    ui_load_persisted_audio_setup();
    ui_ensure_default_channel_assignments();

    active_tab = 0;
    screens[0] = mixer_screen_create();
    for (int i = 1; i < 7; ++i) {
        screens[i] = nullptr;
    }
    if (screens[0]) lv_obj_remove_flag(screens[0], LV_OBJ_FLAG_HIDDEN);

    // Persistent Top Status Bar
    auto* top = lv_obj_create(lv_layer_top());
    s_top_bar = top;
    lv_obj_set_size(top, 1920, 44);
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_remove_flag(top, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(top, COLOR_PANEL, 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(top, COLOR_EDGE, 0);
    lv_obj_set_style_border_width(top, 1, 0);
    lv_obj_set_style_radius(top, 0, 0);
    lv_obj_set_style_pad_all(top, 0, 0);

    // Left 1: Brand Logo
    instrument_label(top, "PRISMIX", 16, 11, &ac_font_16, COLOR_TEXT);

    // Left 2: Engine Status Badge
    status_badge = lv_obj_create(top);
    lv_obj_set_pos(status_badge, 136, 7);
    lv_obj_set_size(status_badge, 110, 30);
    lv_obj_set_style_bg_color(status_badge, lv_color_hex(0x22262B), 0);
    lv_obj_set_style_bg_opa(status_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(status_badge, lv_color_hex(0x3A424A), 0);
    lv_obj_set_style_border_width(status_badge, 1, 0);
    lv_obj_set_style_radius(status_badge, 3, 0);
    lv_obj_set_style_pad_all(status_badge, 0, 0);
    lv_obj_remove_flag(status_badge, LV_OBJ_FLAG_SCROLLABLE);

    status_badge_label = lv_label_create(status_badge);
    lv_label_set_text(status_badge_label, "● STOPPED");
    lv_obj_center(status_badge_label);
    lv_obj_set_style_text_font(status_badge_label, &ac_font_12, 0);
    lv_obj_set_style_text_color(status_badge_label, lv_color_hex(0x9AA0A6), 0);

    // Center: Stream Summary
    engine_status = instrument_label(top, "SIMULATION  •  48,000 Hz  •  Mock Audio Engine", 260, 14, &ac_font_12, COLOR_TEXT2);

    // Right 1: Selected Channel Readout
    channel_status = instrument_label(top, "SELECTED: CH 01 / VOCAL", 1160, 14, &ac_font_12, COLOR_SILVER);

    // Right 2: Audio Setup Modal Button
    audio_setup_btn = lv_button_create(top);
    lv_obj_set_pos(audio_setup_btn, 1540, 6);
    lv_obj_set_size(audio_setup_btn, 150, 32);
    lv_obj_set_style_bg_color(audio_setup_btn, COLOR_CONTROL, 0);
    lv_obj_set_style_bg_opa(audio_setup_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(audio_setup_btn, COLOR_EDGE, 0);
    lv_obj_set_style_border_width(audio_setup_btn, 1, 0);
    lv_obj_set_style_radius(audio_setup_btn, 2, 0);
    lv_obj_set_style_pad_all(audio_setup_btn, 0, 0);
    auto* setup_btn_lbl = lv_label_create(audio_setup_btn);
    lv_label_set_text(setup_btn_lbl, "⚙ AUDIO SETUP");
    lv_obj_center(setup_btn_lbl);
    lv_obj_set_style_text_font(setup_btn_lbl, &ac_font_12, 0);
    lv_obj_set_style_text_color(setup_btn_lbl, COLOR_SILVER, 0);
    lv_obj_add_event_cb(audio_setup_btn, [](lv_event_t*) {
        show_audio_setup_dialog();
    }, LV_EVENT_CLICKED, nullptr);

    // Right 3: Prominent Start/Stop Audio Action Button
    start_stop_btn = lv_button_create(top);
    lv_obj_set_pos(start_stop_btn, 1704, 6);
    lv_obj_set_size(start_stop_btn, 190, 32);
    lv_obj_set_style_bg_color(start_stop_btn, lv_color_hex(0x133824), 0);
    lv_obj_set_style_bg_opa(start_stop_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(start_stop_btn, COLOR_GREEN, 0);
    lv_obj_set_style_border_width(start_stop_btn, 1, 0);
    lv_obj_set_style_radius(start_stop_btn, 2, 0);
    lv_obj_set_style_pad_all(start_stop_btn, 0, 0);
    start_stop_label = lv_label_create(start_stop_btn);
    lv_label_set_text(start_stop_label, "▶ START AUDIO");
    lv_obj_center(start_stop_label);
    lv_obj_set_style_text_font(start_stop_label, &ac_font_12, 0);
    lv_obj_set_style_text_color(start_stop_label, COLOR_GREEN, 0);
    lv_obj_add_event_cb(start_stop_btn, [](lv_event_t*) {
        const auto state = audio_service().snapshot().state;
        if (state == AudioServiceState::Streaming || state == AudioServiceState::Initializing) {
            ui_stop_real_audio();
        } else {
            std::string err;
            if (!ui_start_real_audio(&err)) {
                // If start fails, open Audio Setup modal so user can adjust device or buffer
                show_audio_setup_dialog();
            }
        }
    }, LV_EVENT_CLICKED, nullptr);

    lv_timer_create(status_event, 100, nullptr);

    // Persistent Bottom Navigation Bar
    auto* bottom = lv_obj_create(lv_layer_top());
    s_bottom_bar = bottom;
    lv_obj_set_size(bottom, 1920, 60);
    lv_obj_align(bottom, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_remove_flag(bottom, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(bottom, COLOR_PANEL, 0);
    lv_obj_set_style_bg_opa(bottom, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(bottom, COLOR_EDGE, 0);
    lv_obj_set_style_border_width(bottom, 1, 0);
    lv_obj_set_style_radius(bottom, 0, 0);
    lv_obj_set_style_pad_all(bottom, 0, 0);
    const int gap = 8, width = 260, start = (1920 - (7 * width + 6 * gap)) / 2;
    for (int i = 0; i < 7; ++i) {
        nav_buttons[i] = lv_button_create(bottom);
        lv_obj_set_size(nav_buttons[i], width, 48);
        lv_obj_set_pos(nav_buttons[i], start + i * (width + gap), 6);
        lv_obj_set_style_radius(nav_buttons[i], 1, 0);
        lv_obj_set_style_bg_color(nav_buttons[i], COLOR_PANEL, 0);
        lv_obj_set_style_border_color(nav_buttons[i], COLOR_EDGE, 0);
        lv_obj_set_style_border_width(nav_buttons[i], 1, 0);
        auto* label = lv_label_create(nav_buttons[i]);
        lv_label_set_text(label, tabs[i]);
        lv_obj_center(label);
        lv_obj_set_style_text_font(label, &ac_font_16, 0);
        lv_obj_set_style_text_color(label, COLOR_TEXT2, 0);
        lv_obj_add_event_cb(nav_buttons[i], nav_event, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    }
    paint_nav();
    prismix::ui::security_lock_overlay_sync();
}
