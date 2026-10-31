#include "system_screen.h"
#include "../theme/theme.h"
#include "../theme/theme_model.h"
#include "../widgets/instrument.h"
#include "../assets/rack_textures.h"
#include "../ui_audio_bridge.h"
#include "../widgets/system_dialogs.h"
#include "../../service/security/security_service.h"
#include "../../service/system/system_service.h"
#include "../../service/web/web_server.h"
#include "../../backend/mock_audio_backend.h"
#include "../../model/mixer_state.h"
#include "../../model/audio_setup_config.h"
#include "../../service/audio_service.h"
#include "audiocore/biquad_filter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

void persistSetup() {
    std::string error;
    ui_save_persisted_audio_setup(&error);
}

static int s_system_mode = 0; // 0 = Audio Engine & DSP, 1 = Appearance & Theme, 2 = System & Network

struct SystemScreenView {
    // Mode toggle
    int active_mode = 0;
    lv_obj_t* mode_audio_btn = nullptr;
    lv_obj_t* mode_theme_btn = nullptr;
    lv_obj_t* mode_system_btn = nullptr;
    lv_obj_t* audio_container = nullptr;
    lv_obj_t* theme_container = nullptr;
    lv_obj_t* system_container = nullptr;

    // Audio Engine & DSP view objects
    lv_obj_t* mode = nullptr;
    lv_obj_t* capture = nullptr;
    lv_obj_t* render = nullptr;
    lv_obj_t* start = nullptr;
    lv_obj_t* state = nullptr;
    lv_obj_t* requested = nullptr;
    lv_obj_t* negotiated = nullptr;
    lv_obj_t* metrics = nullptr;
    lv_obj_t* error = nullptr;
    lv_obj_t* gain = nullptr;
    lv_obj_t* gain_value = nullptr;
    lv_obj_t* pan = nullptr;
    lv_obj_t* pan_value = nullptr;
    lv_obj_t* bypass = nullptr;
    lv_obj_t* peq = nullptr;
    lv_obj_t* compressor = nullptr;
    lv_obj_t* simulationControls[4]{};
    lv_timer_t* timer = nullptr;
    bool deviceOptionsSet = false;
    std::string displayedCaptureDeviceId;
    std::string displayedRenderDeviceId;
    bool captureMissingSavedDevice = false;
    bool renderMissingSavedDevice = false;
    bool pendingSimulation = false;

    // Theme & Appearance Editor view objects
    lv_obj_t* preset_btns[5]{};
    lv_obj_t* active_preset_badge = nullptr;
    lv_obj_t* save_theme_btn = nullptr;
    lv_obj_t* save_status_lbl = nullptr;
    lv_obj_t* role_cards[12]{};
    lv_obj_t* role_swatches[12]{};
    lv_obj_t* role_hex_labels[12]{};
    ThemeColorRole selected_role = ThemeColorRole::SelectedBorder;

    lv_obj_t* editing_role_title = nullptr;
    lv_obj_t* active_swatch = nullptr;
    lv_obj_t* hex_textarea = nullptr;
    lv_obj_t* hex_apply_btn = nullptr;
    lv_obj_t* hex_status_lbl = nullptr;

    lv_obj_t* slider_r = nullptr;
    lv_obj_t* slider_g = nullptr;
    lv_obj_t* slider_b = nullptr;
    lv_obj_t* val_r_lbl = nullptr;
    lv_obj_t* val_g_lbl = nullptr;
    lv_obj_t* val_b_lbl = nullptr;

    // Live Hardware Preview objects
    lv_obj_t* preview_panel = nullptr;
    lv_obj_t* preview_btn_norm = nullptr;
    lv_obj_t* preview_btn_act = nullptr;
    lv_obj_t* preview_btn_mute = nullptr;
    lv_obj_t* preview_knob_group = nullptr;
    float preview_knob_val = 0.65f;
    lv_obj_t* preview_lcd_box = nullptr;
    lv_obj_t* preview_lcd_line1 = nullptr;
    lv_obj_t* preview_lcd_line2 = nullptr;
    lv_obj_t* preview_txt_primary = nullptr;
    lv_obj_t* preview_txt_secondary = nullptr;
    lv_obj_t* preview_accent1_pill = nullptr;
    lv_obj_t* preview_accent2_pill = nullptr;

    // System & Network view objects
    lv_obj_t* sys_security_state = nullptr;
    lv_obj_t* sys_access_mode = nullptr;
    lv_obj_t* sys_pin_btn = nullptr;
    lv_obj_t* sys_pin_btn_label = nullptr;
    lv_obj_t* sys_lock_btn = nullptr;
    lv_obj_t* sys_lan_btn = nullptr;
    lv_obj_t* sys_lan_btn_label = nullptr;
    lv_obj_t* sys_lan_state = nullptr;
    lv_obj_t* sys_power_state = nullptr;
    lv_obj_t* sys_restart_btn = nullptr;
    lv_obj_t* sys_shutdown_btn = nullptr;
    lv_obj_t* sys_network_nm = nullptr;
    lv_obj_t* sys_network_connection = nullptr;
    lv_obj_t* sys_network_ssid = nullptr;
    lv_obj_t* sys_network_signal = nullptr;
    lv_obj_t* sys_network_ipv4 = nullptr;
    lv_obj_t* sys_network_device = nullptr;
    lv_obj_t* sys_scan_btn = nullptr;
    lv_obj_t* sys_network_note = nullptr;
    lv_obj_t* sys_web_url = nullptr;
    lv_obj_t* sys_web_mode = nullptr;
    lv_obj_t* sys_web_bind = nullptr;
    lv_obj_t* sys_web_note = nullptr;
    uint32_t last_network_tick = 0;
    uint32_t last_power_tick = 0;
    uint32_t last_web_tick = 0;
    uint32_t last_security_tick = 0;
    uint64_t last_network_version = 0;
};

static SystemScreenView* s_active_system_view = nullptr;

float toDb(float linear) {
    if (linear <= 1.0e-6f) return -120.0f;
    return 20.0f * std::log10(linear);
}

std::string deviceOptions(const std::vector<AudioDeviceChoice>& devices, const std::string& savedId) {
    std::string options = "DEFAULT ENDPOINT";
    bool savedFound = savedId.empty();
    for (const auto& device : devices) savedFound = savedFound || device.id == savedId;
    if (!savedFound) options += "\nUNAVAILABLE SAVED DEVICE: " + savedId;
    for (const auto& device : devices) {
        options += '\n';
        options += device.name.empty() ? device.id : device.name;
        options += "  [" + std::to_string(device.channels) + (device.isCapture ? " inputs]" : " outputs]");
        options += "  [" + device.id + "]";
        if (device.isDefault) options += "  [DEFAULT]";
    }
    return options;
}

void styleDropdown(lv_obj_t* dropdown) {
    lv_obj_set_style_bg_color(dropdown, COLOR_CONTROL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dropdown, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(dropdown, COLOR_EDGE, LV_PART_MAIN);
    lv_obj_set_style_border_width(dropdown, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(dropdown, 2, LV_PART_MAIN);
    lv_obj_set_style_text_color(dropdown, COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(dropdown, &ac_font_12, LV_PART_MAIN);

    lv_obj_t* list = lv_dropdown_get_list(dropdown);
    if (list) {
        lv_obj_set_style_bg_color(list, lv_color_hex(0x181C20), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_color(list, COLOR_EDGE, LV_PART_MAIN);
        lv_obj_set_style_border_width(list, 1, LV_PART_MAIN);
        lv_obj_set_style_radius(list, 3, LV_PART_MAIN);
        lv_obj_set_style_text_color(list, COLOR_TEXT, LV_PART_MAIN);
        lv_obj_set_style_text_font(list, &ac_font_12, LV_PART_MAIN);
        lv_obj_set_style_max_height(list, 260, LV_PART_MAIN);
        lv_obj_set_style_pad_all(list, 4, LV_PART_MAIN);

        // Highlight for selected option
        lv_obj_set_style_bg_color(list, lv_color_hex(0x283848), LV_PART_SELECTED);
        lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_SELECTED);
        lv_obj_set_style_text_color(list, COLOR_CYAN, LV_PART_SELECTED);
    }
}

void styleSlider(lv_obj_t* slider) {
    lv_obj_set_style_bg_color(slider, COLOR_RECESSED, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(slider, COLOR_EDGE, LV_PART_MAIN);
    lv_obj_set_style_border_width(slider, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, COLOR_CYAN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, COLOR_CONTROL, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_border_color(slider, COLOR_TEXT, LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, 1, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 5, LV_PART_KNOB);
}

void styleSwitch(lv_obj_t* sw) {
    lv_obj_set_size(sw, 54, 30);
    lv_obj_set_style_bg_color(sw, COLOR_CONTROL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(sw, COLOR_EDGE, LV_PART_MAIN);
    lv_obj_set_style_border_width(sw, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, COLOR_CYAN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sw, COLOR_TEXT, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, LV_PART_KNOB);
}

void style_mode_button(lv_obj_t* button, bool active) {
    if (!button) return;
    lv_obj_set_style_bg_color(button, active ? COLOR_RAISED_PANEL : COLOR_CONTROL, 0);
    lv_obj_set_style_border_color(button, active ? COLOR_SELECTED_BORDER : COLOR_STANDARD_BORDER, 0);
    lv_obj_set_style_border_width(button, active ? 2 : 1, 0);
    if (lv_obj_get_child_count(button) > 0) {
        lv_obj_set_style_text_color(lv_obj_get_child(button, 0),
                                    active ? COLOR_PRIMARY_TEXT : COLOR_SECONDARY_TEXT, 0);
    }
}

void update_system_mode_view(SystemScreenView* v) {
    if (!v) return;
    lv_obj_add_flag(v->audio_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(v->theme_container, LV_OBJ_FLAG_HIDDEN);
    if (v->system_container) lv_obj_add_flag(v->system_container, LV_OBJ_FLAG_HIDDEN);

    if (v->active_mode == 0) {
        lv_obj_remove_flag(v->audio_container, LV_OBJ_FLAG_HIDDEN);
    } else if (v->active_mode == 1) {
        lv_obj_remove_flag(v->theme_container, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (v->system_container) lv_obj_remove_flag(v->system_container, LV_OBJ_FLAG_HIDDEN);
    }

    style_mode_button(v->mode_audio_btn, v->active_mode == 0);
    style_mode_button(v->mode_theme_btn, v->active_mode == 1);
    style_mode_button(v->mode_system_btn, v->active_mode == 2);
}

void sync_theme_editor_controls(SystemScreenView* v);

lv_obj_t* make_system_button(lv_obj_t* parent, const char* text, int x, int y,
                                 int w, int h, lv_color_t accent) {
    auto* button = lv_button_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, w, h);
    lv_obj_set_style_radius(button, 2, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    ui::rack_skin::apply_hardware_button(button, COLOR_CONTROL, accent, COLOR_PRIMARY_TEXT);
    auto* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_set_style_text_font(label, &ac_font_14, 0);
    lv_obj_set_style_text_color(label, COLOR_PRIMARY_TEXT, 0);
    return button;
}

lv_obj_t* system_button_label(lv_obj_t* button) {
    return button && lv_obj_get_child_count(button) > 0 ? lv_obj_get_child(button, 0) : nullptr;
}

void set_button_enabled(lv_obj_t* button, bool enabled) {
    if (!button) return;
    if (enabled) lv_obj_remove_state(button, LV_STATE_DISABLED);
    else lv_obj_add_state(button, LV_STATE_DISABLED);
}

void refresh_system_access_controls(SystemScreenView* v) {
    if (!v) return;
    auto& security = prismix::security::SecurityService::instance();
    const bool configured = security.isPinConfigured();
    const bool locked = security.isMixerLocked();
    const bool lan = security.isLanAccessEnabled();

    if (v->sys_security_state) {
        lv_label_set_text(v->sys_security_state,
            !configured ? "PIN PROTECTION: NOT CONFIGURED"
            : locked ? "PIN PROTECTION: LOCKED"
                     : "PIN PROTECTION: ENABLED");
        lv_obj_set_style_text_color(v->sys_security_state,
            !configured ? COLOR_YELLOW : (locked ? COLOR_RED : COLOR_GREEN), 0);
    }
    if (v->sys_access_mode) {
        lv_label_set_text(v->sys_access_mode,
            !configured ? "LOCAL CONSOLE ACCESS  /  NO PIN"
            : lan ? "LAN ACCESS ENABLED  /  PIN REQUIRED"
                  : "LOCAL CONSOLE ACCESS  /  PIN PROTECTED");
        lv_obj_set_style_text_color(v->sys_access_mode,
            lan ? COLOR_CYAN : COLOR_SECONDARY_TEXT, 0);
    }
    if (v->sys_pin_btn_label) {
        lv_label_set_text(v->sys_pin_btn_label, configured ? "CHANGE PIN" : "SET UP PIN");
    }
    set_button_enabled(v->sys_lock_btn, configured && !locked);
    set_button_enabled(v->sys_lan_btn, configured || lan);
    if (v->sys_lan_btn_label) {
        lv_label_set_text(v->sys_lan_btn_label, lan ? "DISABLE LAN ACCESS" : "ENABLE LAN ACCESS");
    }
    if (v->sys_lan_state) {
        lv_label_set_text(v->sys_lan_state,
            lan ? "LAN ACCESS: ENABLED  /  HTTP - NO TLS"
                : "LAN ACCESS: DISABLED  /  LOCALHOST ONLY");
        lv_obj_set_style_text_color(v->sys_lan_state, lan ? COLOR_YELLOW : COLOR_SECONDARY_TEXT, 0);
    }
}

void refresh_system_power_controls(SystemScreenView* v) {
    if (!v) return;
    const auto power = prismix::system::SystemService::instance().get_power_status();
    if (v->sys_power_state) {
        if (power.available) {
            lv_label_set_text_fmt(v->sys_power_state, "SYSTEMD POWER INTEGRATION: READY  /  %s",
                                  power.integration.empty() ? "systemctl" : power.integration.c_str());
            lv_obj_set_style_text_color(v->sys_power_state, COLOR_GREEN, 0);
        } else {
            lv_label_set_text(v->sys_power_state,
                power.reason.empty() ? "SYSTEM POWER MANAGEMENT: UNAVAILABLE"
                                     : ("SYSTEM POWER MANAGEMENT: " + power.reason).c_str());
            lv_obj_set_style_text_color(v->sys_power_state, COLOR_YELLOW, 0);
        }
    }
    set_button_enabled(v->sys_restart_btn, power.available);
    set_button_enabled(v->sys_shutdown_btn, power.available);
}

void refresh_system_network_controls(SystemScreenView* v, bool force) {
    if (!v) return;
    const auto info = prismix::system::SystemService::instance().get_network_info(force);
    if (v->sys_network_nm) {
        lv_label_set_text_fmt(v->sys_network_nm, "NETWORKMANAGER: %s%s%s",
            info.network_manager ? "AVAILABLE" : "UNAVAILABLE",
            info.nm_state.empty() ? "" : "  /  ",
            info.nm_state.c_str());
        lv_obj_set_style_text_color(v->sys_network_nm,
            info.network_manager ? COLOR_GREEN : COLOR_YELLOW, 0);
    }
    if (v->sys_network_connection) {
        const std::string connection = info.connected
            ? (info.connection_name.empty() ? "CONNECTED" : info.connection_name)
            : "DISCONNECTED";
        lv_label_set_text_fmt(v->sys_network_connection, "CONNECTION: %s  /  %s",
                              connection.c_str(), info.connection_type.empty() ? "-" : info.connection_type.c_str());
    }
    if (v->sys_network_ssid) {
        lv_label_set_text_fmt(v->sys_network_ssid, "WI-FI SSID: %s",
                              info.wifi && !info.ssid.empty() ? info.ssid.c_str() : "-");
    }
    if (v->sys_network_signal) {
        if (info.wifi && info.signal_percent >= 0) {
            lv_label_set_text_fmt(v->sys_network_signal, "WI-FI SIGNAL: %d%%", info.signal_percent);
        } else {
            lv_label_set_text(v->sys_network_signal, "WI-FI SIGNAL: -");
        }
    }
    if (v->sys_network_ipv4) {
        lv_label_set_text_fmt(v->sys_network_ipv4, "IPV4: %s",
                              info.ipv4.empty() ? "-" : info.ipv4.c_str());
    }
    if (v->sys_network_device) {
        lv_label_set_text_fmt(v->sys_network_device, "INTERFACE: %s  /  %s",
            info.interface_name.empty() ? "-" : info.interface_name.c_str(),
            info.device_state.empty() ? "-" : info.device_state.c_str());
    }
    if (v->sys_network_note) {
        std::string note;
        if (!info.wifi_hardware) note = "No Wi-Fi hardware was detected. Wired networking may still be in use.";
        else if (!info.error.empty()) note = info.error;
        else note = "Wi-Fi credentials are sent only to the local NetworkManager service and are never displayed here.";
        lv_label_set_text(v->sys_network_note, note.c_str());
        lv_obj_set_style_text_color(v->sys_network_note,
            (!info.wifi_hardware || !info.error.empty()) ? COLOR_YELLOW : COLOR_SECONDARY_TEXT, 0);
    }
    set_button_enabled(v->sys_scan_btn, info.wifi_hardware);
}

void refresh_system_web_controls(SystemScreenView* v) {
    if (!v) return;
    auto& server = prismix::web::WebServer::instance();
    auto& security = prismix::security::SecurityService::instance();
    const auto access = prismix::system::SystemService::instance().get_web_access(
        server.port(), server.bind_address(), security.isLanAccessEnabled());

    if (v->sys_web_url) {
        lv_label_set_text(v->sys_web_url,
            access.url.empty() ? "NO LAN WEB UI ADDRESS" : access.url.c_str());
        lv_obj_set_style_text_color(v->sys_web_url,
            access.url.empty() ? COLOR_YELLOW : COLOR_CYAN, 0);
    }
    if (v->sys_web_mode) {
        lv_label_set_text(v->sys_web_mode,
            access.local_only ? "LOCALHOST ONLY" : "LAN ACCESS");
        lv_obj_set_style_text_color(v->sys_web_mode,
            access.local_only ? COLOR_SECONDARY_TEXT : COLOR_YELLOW, 0);
    }
    if (v->sys_web_bind) {
        lv_label_set_text_fmt(v->sys_web_bind, "BIND %s:%u  /  SCHEME %s  /  TRANSPORT HTTP - NO TLS",
            access.bind_address.c_str(), static_cast<unsigned>(access.port),
            access.scheme.c_str());
    }
    if (v->sys_web_note) {
        lv_label_set_text(v->sys_web_note,
            "HTTP provides no transport encryption. Enable LAN access only on a trusted network and share the address and PIN only with authorized operators.");
    }
}


lv_obj_t* g_wifi_scan_backdrop = nullptr;
lv_obj_t* g_wifi_connect_backdrop = nullptr;

void close_wifi_scan_modal() {
    if (!g_wifi_scan_backdrop) return;
    lv_obj_t* backdrop = g_wifi_scan_backdrop;
    g_wifi_scan_backdrop = nullptr;
    lv_obj_delete(backdrop);
}

struct WifiScanModalContext {
    lv_obj_t* backdrop = nullptr;
    lv_obj_t* list = nullptr;
    lv_obj_t* status = nullptr;
    lv_timer_t* timer = nullptr;
    bool rendered = false;
};

struct WifiRowInfo {
    std::string ssid;
    bool secured = false;
    bool active = false;
};

struct WifiConnectState {
    std::mutex mutex;
    bool done = false;
    bool success = false;
    std::string error;
};

struct WifiConnectDialogContext {
    lv_obj_t* backdrop = nullptr;
    std::string ssid;
    lv_obj_t* status = nullptr;
    lv_obj_t* connect_btn = nullptr;
    lv_obj_t* cancel_btn = nullptr;
    lv_obj_t* password = nullptr;
    lv_timer_t* timer = nullptr;
    std::shared_ptr<WifiConnectState> state;
};

void close_wifi_connect_dialog() {
    if (!g_wifi_connect_backdrop) return;
    lv_obj_t* backdrop = g_wifi_connect_backdrop;
    g_wifi_connect_backdrop = nullptr;
    lv_obj_delete(backdrop);
}

void show_wifi_connect_dialog(const std::string& ssid, bool secured, bool active);

void wifi_scan_modal_delete_cb(lv_event_t* event) {
    auto* ctx = static_cast<WifiScanModalContext*>(lv_event_get_user_data(event));
    if (ctx) {
        if (ctx->timer) lv_timer_delete(ctx->timer);
        if (g_wifi_scan_backdrop == ctx->backdrop) g_wifi_scan_backdrop = nullptr;
        delete ctx;
    }
}

void wifi_scan_poll_cb(lv_timer_t* timer) {
    auto* ctx = static_cast<WifiScanModalContext*>(lv_timer_get_user_data(timer));
    if (!ctx || !ctx->backdrop) return;
    bool scanning = false;
    const auto result = prismix::system::SystemService::instance().wifi_scan_snapshot(scanning);

    if (ctx->status) {
        if (scanning) {
            lv_label_set_text(ctx->status, "SCANNING NETWORKMANAGER... This can take a few seconds.");
            lv_obj_set_style_text_color(ctx->status, COLOR_SECONDARY_TEXT, 0);
        } else if (!result.success) {
            lv_label_set_text(ctx->status, result.error.empty() ? "Wi-Fi scan failed." : result.error.c_str());
            lv_obj_set_style_text_color(ctx->status, COLOR_YELLOW, 0);
        } else {
            lv_label_set_text_fmt(ctx->status, "%zu WI-FI NETWORK%s FOUND", result.networks.size(),
                                  result.networks.size() == 1 ? "" : "S");
            lv_obj_set_style_text_color(ctx->status, COLOR_SECONDARY_TEXT, 0);
        }
    }
    if (scanning || ctx->rendered) return;
    ctx->rendered = true;
    if (!ctx->list) return;
    lv_obj_clean(ctx->list);
    if (!result.networks.empty()) {
        const int row_height = 76;
        int y = 8;
        for (const auto& network : result.networks) {
            auto* row = lv_button_create(ctx->list);
            lv_obj_set_pos(row, 8, y);
            lv_obj_set_size(row, 1080, 64);
            lv_obj_set_style_radius(row, 1, 0);
            lv_obj_set_style_pad_all(row, 0, 0);
            ui::rack_skin::apply_hardware_button(row, network.active ? COLOR_RAISED_PANEL : COLOR_CONTROL,
                                                 network.active ? COLOR_GREEN : COLOR_STANDARD_BORDER,
                                                 COLOR_PRIMARY_TEXT);
            auto* info = new WifiRowInfo{network.ssid, network.secured, network.active};
            lv_obj_add_event_cb(row, [](lv_event_t* event) {
                auto* info = static_cast<WifiRowInfo*>(lv_event_get_user_data(event));
                if (!info) return;
                if (info->active) {
                    prismix::ui::security_dialog_show_message(
                        "WI-FI CONNECTION",
                        info->ssid + " is already the active Wi-Fi connection.");
                    return;
                }
                show_wifi_connect_dialog(info->ssid, info->secured, info->active);
            }, LV_EVENT_CLICKED, info);
            lv_obj_add_event_cb(row, [](lv_event_t* event) {
                delete static_cast<WifiRowInfo*>(lv_event_get_user_data(event));
            }, LV_EVENT_DELETE, info);

            auto* name = lv_label_create(row);
            lv_obj_set_pos(name, 18, 10);
            lv_label_set_text(name, network.ssid.c_str());
            lv_obj_set_style_text_font(name, &ac_font_16, 0);
            lv_obj_set_style_text_color(name, COLOR_PRIMARY_TEXT, 0);

            auto* meta = lv_label_create(row);
            lv_obj_set_pos(meta, 18, 34);
            lv_label_set_text_fmt(meta, "%s  /  %s  /  %d%%",
                network.active ? "ACTIVE" : (network.secured ? "SECURED" : "OPEN"),
                network.security.empty() ? "-" : network.security.c_str(),
                network.signal_percent);
            lv_obj_set_style_text_font(meta, &ac_font_12, 0);
            lv_obj_set_style_text_color(meta, COLOR_SECONDARY_TEXT, 0);

            auto* action = lv_label_create(row);
            lv_obj_align(action, LV_ALIGN_RIGHT_MID, -18, 0);
            lv_label_set_text(action, network.active ? "CONNECTED" : "CONNECT");
            lv_obj_set_style_text_font(action, &ac_font_12, 0);
            lv_obj_set_style_text_color(action, network.active ? COLOR_GREEN : COLOR_CYAN, 0);
            y += row_height;
        }
    } else {
        auto* empty = lv_label_create(ctx->list);
        lv_label_set_text(empty, result.success ? "No Wi-Fi networks were found." : "No Wi-Fi scan results are available.");
        lv_obj_set_style_text_font(empty, &ac_font_14, 0);
        lv_obj_set_style_text_color(empty, COLOR_SECONDARY_TEXT, 0);
        lv_obj_set_pos(empty, 20, 20);
    }
}

void show_wifi_scan_modal() {
    close_wifi_scan_modal();
    prismix::ui::security_dialog_close_all();

    auto* ctx = new WifiScanModalContext{};
    auto* backdrop = lv_obj_create(lv_layer_top());
    ctx->backdrop = backdrop;
    g_wifi_scan_backdrop = backdrop;
    lv_obj_set_pos(backdrop, 0, 0);
    lv_obj_set_size(backdrop, 1920, 1080);
    lv_obj_remove_flag(backdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(backdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(backdrop, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_60, 0);
    lv_obj_set_style_border_width(backdrop, 0, 0);
    lv_obj_set_style_radius(backdrop, 0, 0);

    auto* panel = lv_obj_create(backdrop);
    lv_obj_set_size(panel, 1200, 780);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    ui::rack_skin::apply_faceplate_panel(panel, true);
    lv_obj_set_style_bg_color(panel, COLOR_PANEL_SURFACE, 0);
    lv_obj_set_style_border_color(panel, COLOR_SELECTED_BORDER, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);

    auto* title = lv_label_create(panel);
    lv_obj_set_pos(title, 32, 24);
    lv_label_set_text(title, "WI-FI NETWORKS");
    lv_obj_set_style_text_font(title, &ac_font_28, 0);
    lv_obj_set_style_text_color(title, COLOR_PRIMARY_TEXT, 0);

    auto* explanation = lv_label_create(panel);
    lv_obj_set_pos(explanation, 32, 60);
    lv_obj_set_width(explanation, 1120);
    lv_label_set_long_mode(explanation, LV_LABEL_LONG_WRAP);
    lv_label_set_text(explanation,
        "Live NetworkManager scan. Selecting a secured network opens a local password entry dialog; credentials are never logged or returned.");
    lv_obj_set_style_text_font(explanation, &ac_font_12, 0);
    lv_obj_set_style_text_color(explanation, COLOR_SECONDARY_TEXT, 0);

    ctx->list = lv_obj_create(panel);
    lv_obj_set_pos(ctx->list, 32, 106);
    lv_obj_set_size(ctx->list, 1136, 502);
    lv_obj_set_style_bg_color(ctx->list, COLOR_RECESSED, 0);
    lv_obj_set_style_bg_opa(ctx->list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ctx->list, COLOR_STANDARD_BORDER, 0);
    lv_obj_set_style_border_width(ctx->list, 1, 0);
    lv_obj_set_style_radius(ctx->list, 1, 0);
    lv_obj_set_style_pad_all(ctx->list, 0, 0);
    lv_obj_set_scrollbar_mode(ctx->list, LV_SCROLLBAR_MODE_AUTO);

    ctx->status = lv_label_create(panel);
    lv_obj_set_pos(ctx->status, 32, 626);
    lv_obj_set_width(ctx->status, 1136);
    lv_label_set_text(ctx->status, "STARTING WI-FI SCAN...");
    lv_obj_set_style_text_font(ctx->status, &ac_font_14, 0);
    lv_obj_set_style_text_color(ctx->status, COLOR_SECONDARY_TEXT, 0);

    auto* refresh = make_system_button(panel, "REFRESH SCAN", 32, 684, 250, 56, COLOR_SELECTED_BORDER);
    lv_obj_add_event_cb(refresh, [](lv_event_t* event) {
        auto* ctx = static_cast<WifiScanModalContext*>(lv_event_get_user_data(event));
        if (!ctx) return;
        ctx->rendered = false;
        if (ctx->list) lv_obj_clean(ctx->list);
        prismix::system::SystemService::instance().start_wifi_scan(true);
    }, LV_EVENT_CLICKED, ctx);

    auto* close = make_system_button(panel, "CLOSE", 918, 684, 250, 56, COLOR_STANDARD_BORDER);
    lv_obj_add_event_cb(close, [](lv_event_t*) {
        close_wifi_scan_modal();
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_add_event_cb(backdrop, wifi_scan_modal_delete_cb, LV_EVENT_DELETE, ctx);
    ctx->timer = lv_timer_create(wifi_scan_poll_cb, 250, ctx);
    prismix::system::SystemService::instance().start_wifi_scan(true);
}

void wifi_connect_poll_cb(lv_timer_t* timer) {
    auto* ctx = static_cast<WifiConnectDialogContext*>(lv_timer_get_user_data(timer));
    if (!ctx || !ctx->state) return;
    bool done = false;
    bool success = false;
    std::string error;
    {
        std::lock_guard<std::mutex> lock(ctx->state->mutex);
        done = ctx->state->done;
        success = ctx->state->success;
        error = ctx->state->error;
    }
    if (!done) return;

    if (ctx->timer) {
        lv_timer_delete(ctx->timer);
        ctx->timer = nullptr;
    }
    lv_obj_t* backdrop = ctx->backdrop;
    if (g_wifi_connect_backdrop == backdrop) g_wifi_connect_backdrop = nullptr;
    lv_obj_delete(backdrop);
    if (success) {
        close_wifi_scan_modal();
        prismix::ui::security_dialog_show_message(
            "WI-FI CONNECTED",
            "NetworkManager accepted the connection. The network status will refresh automatically; the PRISMIX audio engine was not stopped.");
    } else {
        prismix::ui::security_dialog_show_message(
            "WI-FI CONNECTION FAILED",
            error.empty() ? "NetworkManager did not accept the connection." : error);
    }
}

void wifi_connect_dialog_delete_cb(lv_event_t* event) {
    auto* ctx = static_cast<WifiConnectDialogContext*>(lv_event_get_user_data(event));
    if (ctx) {
        if (ctx->timer) lv_timer_delete(ctx->timer);
        if (g_wifi_connect_backdrop == ctx->backdrop) g_wifi_connect_backdrop = nullptr;
        delete ctx;
    }
}

void show_wifi_connect_dialog(const std::string& ssid, bool secured, bool active) {
    if (active) {
        prismix::ui::security_dialog_show_message("WI-FI CONNECTION", ssid + " is already connected.");
        return;
    }
    close_wifi_connect_dialog();
    prismix::ui::security_dialog_close_all();

    auto* ctx = new WifiConnectDialogContext{};
    auto* backdrop = lv_obj_create(lv_layer_top());
    ctx->backdrop = backdrop;
    ctx->ssid = ssid;
    g_wifi_connect_backdrop = backdrop;
    lv_obj_set_pos(backdrop, 0, 0);
    lv_obj_set_size(backdrop, 1920, 1080);
    lv_obj_remove_flag(backdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(backdrop, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(backdrop, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_60, 0);
    lv_obj_set_style_border_width(backdrop, 0, 0);
    lv_obj_set_style_radius(backdrop, 0, 0);

    const int panel_height = secured ? 410 : 300;
    auto* panel = lv_obj_create(backdrop);
    lv_obj_set_size(panel, 820, panel_height);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, secured ? -160 : -40);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    ui::rack_skin::apply_faceplate_panel(panel, true);
    lv_obj_set_style_bg_color(panel, COLOR_PANEL_SURFACE, 0);
    lv_obj_set_style_border_color(panel, COLOR_SELECTED_BORDER, 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);

    auto* title = lv_label_create(panel);
    lv_obj_set_pos(title, 32, 24);
    lv_label_set_text(title, "CONNECT TO WI-FI");
    lv_obj_set_style_text_font(title, &ac_font_28, 0);
    lv_obj_set_style_text_color(title, COLOR_PRIMARY_TEXT, 0);

    auto* network = lv_label_create(panel);
    lv_obj_set_pos(network, 32, 70);
    lv_label_set_text_fmt(network, "%s  /  %s", ssid.c_str(), secured ? "SECURED" : "OPEN NETWORK");
    lv_obj_set_style_text_font(network, &ac_font_16, 0);
    lv_obj_set_style_text_color(network, COLOR_CYAN, 0);

    auto* note = lv_label_create(panel);
    lv_obj_set_pos(note, 32, 102);
    lv_obj_set_width(note, 756);
    lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
    lv_label_set_text(note,
        "The PRISMIX audio engine and DSP continue running. Changing networks can interrupt the Web UI if its LAN address changes.");
    lv_obj_set_style_text_font(note, &ac_font_12, 0);
    lv_obj_set_style_text_color(note, COLOR_SECONDARY_TEXT, 0);

    if (secured) {
        auto* label = lv_label_create(panel);
        lv_obj_set_pos(label, 32, 156);
        lv_label_set_text(label, "WI-FI PASSWORD");
        lv_obj_set_style_text_font(label, &ac_font_12, 0);
        lv_obj_set_style_text_color(label, COLOR_SECONDARY_TEXT, 0);

        ctx->password = lv_textarea_create(panel);
        lv_obj_set_pos(ctx->password, 32, 180);
        lv_obj_set_size(ctx->password, 756, 56);
        lv_textarea_set_one_line(ctx->password, true);
        lv_textarea_set_password_mode(ctx->password, true);
        lv_textarea_set_password_bullet(ctx->password, "*");
        lv_textarea_set_max_length(ctx->password, 63);
        lv_textarea_set_placeholder_text(ctx->password, "8 TO 63 PRINTABLE CHARACTERS");
        lv_obj_set_style_bg_color(ctx->password, COLOR_RECESSED, 0);
        lv_obj_set_style_bg_opa(ctx->password, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(ctx->password, COLOR_STANDARD_BORDER, 0);
        lv_obj_set_style_border_width(ctx->password, 1, 0);
        lv_obj_set_style_text_font(ctx->password, &ac_font_20, 0);
        lv_obj_set_style_text_color(ctx->password, COLOR_PRIMARY_TEXT, 0);
        lv_obj_set_style_pad_left(ctx->password, 12, 0);
        lv_obj_set_style_text_color(ctx->password, COLOR_TEXT2, LV_PART_TEXTAREA_PLACEHOLDER);
        lv_obj_set_style_text_opa(ctx->password, LV_OPA_60, LV_PART_TEXTAREA_PLACEHOLDER);
    }

    ctx->status = lv_label_create(panel);
    lv_obj_set_pos(ctx->status, 32, secured ? 248 : 156);
    lv_obj_set_width(ctx->status, 756);
    lv_label_set_long_mode(ctx->status, LV_LABEL_LONG_WRAP);
    lv_label_set_text(ctx->status, secured
        ? "The password is sent only to the local NetworkManager service."
        : "This network is open. No password will be requested.");
    lv_obj_set_style_text_font(ctx->status, &ac_font_12, 0);
    lv_obj_set_style_text_color(ctx->status, COLOR_SECONDARY_TEXT, 0);

    const int footer_y = panel_height - 78;
    ctx->cancel_btn = make_system_button(panel, "CANCEL", 32, footer_y, 210, 56, COLOR_STANDARD_BORDER);
    lv_obj_add_event_cb(ctx->cancel_btn, [](lv_event_t*) {
        close_wifi_connect_dialog();
    }, LV_EVENT_CLICKED, nullptr);

    ctx->connect_btn = make_system_button(panel, "CONNECT", 820 - 32 - 300, footer_y, 300, 56, COLOR_ACCENT);
    lv_obj_add_event_cb(ctx->connect_btn, [](lv_event_t* event) {
        auto* ctx = static_cast<WifiConnectDialogContext*>(lv_event_get_user_data(event));
        if (!ctx || ctx->state) return;
        std::string password;
        if (ctx->password) password = lv_textarea_get_text(ctx->password);
        if (ctx->password && (password.size() < 8 || password.size() > 63)) {
            lv_label_set_text(ctx->status, "Enter a Wi-Fi password containing 8 to 63 printable characters.");
            lv_obj_set_style_text_color(ctx->status, COLOR_RED, 0);
            return;
        }
        if (ctx->password) lv_textarea_set_text(ctx->password, "");
        ctx->state = std::make_shared<WifiConnectState>();
        auto state = ctx->state;
        auto* service = &prismix::system::SystemService::instance();
        const std::string connect_ssid = ctx->ssid;
        std::thread([state, service, connect_ssid, password]() {
            std::string error;
            const bool ok = service->connect_wifi(connect_ssid, password, error);
            std::lock_guard<std::mutex> lock(state->mutex);
            state->done = true;
            state->success = ok;
            state->error = error;
        }).detach();
        lv_obj_add_state(ctx->connect_btn, LV_STATE_DISABLED);
        lv_obj_add_state(ctx->cancel_btn, LV_STATE_DISABLED);
        lv_label_set_text(ctx->status, "CONNECTING TO WI-FI... NetworkManager can take several seconds.");
        lv_obj_set_style_text_color(ctx->status, COLOR_YELLOW, 0);
        ctx->timer = lv_timer_create(wifi_connect_poll_cb, 250, ctx);
        close_wifi_scan_modal();
    }, LV_EVENT_CLICKED, ctx);

    if (secured && ctx->password) {
        auto* keyboard = lv_keyboard_create(backdrop);
        lv_obj_set_size(keyboard, 840, 300);
        lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, -50);
        lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_keyboard_set_textarea(keyboard, ctx->password);
        lv_obj_set_style_bg_color(keyboard, COLOR_RECESSED, 0);
        lv_obj_set_style_bg_opa(keyboard, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(keyboard, COLOR_STANDARD_BORDER, 0);
        lv_obj_set_style_border_width(keyboard, 1, 0);
        lv_obj_set_style_text_color(keyboard, COLOR_PRIMARY_TEXT, LV_PART_ITEMS);
        lv_obj_set_style_bg_color(keyboard, COLOR_CONTROL, LV_PART_ITEMS);
        lv_obj_set_style_border_color(keyboard, COLOR_STANDARD_BORDER, LV_PART_ITEMS);
        lv_obj_add_event_cb(keyboard, [](lv_event_t* event) {
            auto* ctx = static_cast<WifiConnectDialogContext*>(lv_event_get_user_data(event));
            if (ctx && ctx->connect_btn) lv_obj_send_event(ctx->connect_btn, LV_EVENT_CLICKED, nullptr);
        }, LV_EVENT_READY, ctx);
    }

    lv_obj_add_event_cb(backdrop, wifi_connect_dialog_delete_cb, LV_EVENT_DELETE, ctx);
}



void request_system_power_action(const char* operation) {
    const std::string op = operation ? operation : "";
    if (op != "restart" && op != "shutdown") return;
    const auto power = prismix::system::SystemService::instance().get_power_status();
    if (!power.available) {
        prismix::ui::security_dialog_show_message(
            "SYSTEM POWER UNAVAILABLE",
            power.reason.empty() ? "System power management is not available on this host." : power.reason);
        return;
    }
    const bool restart = op == "restart";
    const std::string title = restart ? "CONFIRM SYSTEM RESTART" : "CONFIRM SYSTEM SHUTDOWN";
    const std::string message = restart
        ? "The PRISMIX audio engine will be stopped in an orderly manner, then the host will be asked to restart through systemd. The Web UI and console will become unavailable."
        : "The PRISMIX audio engine will be stopped in an orderly manner, then the host will be asked to power off through systemd. The Web UI and console will become unavailable immediately.";
    const std::string confirm = restart ? "RESTART HOST" : "SHUT DOWN HOST";
    prismix::ui::security_dialog_show_confirm(title, message, confirm, true, [op]() {
        const auto snapshot = audio_service().snapshot();
        const bool was_real_active =
            snapshot.mode == AudioMode::RealAudio &&
            (snapshot.state == AudioServiceState::Streaming ||
             snapshot.state == AudioServiceState::Initializing);
        const bool was_sim_running =
            snapshot.mode == AudioMode::Simulation && g_backend.is_running();

        if (was_real_active) ui_stop_real_audio();
        if (was_sim_running) g_backend.stop();

        std::string error;
        const bool accepted = prismix::system::SystemService::instance().request_power(op, error);
        if (!accepted) {
            if (was_sim_running) g_backend.start();
            if (was_real_active) {
                std::string restart_error;
                ui_start_real_audio(&restart_error);
            }
            prismix::ui::security_dialog_show_message(
                "POWER REQUEST FAILED",
                error.empty() ? "The operating system rejected the power request." : error);
            return;
        }
        prismix::ui::security_dialog_show_message(
            op == "restart" ? "RESTART REQUEST ACCEPTED" : "SHUTDOWN REQUEST ACCEPTED",
            "The orderly audio shutdown completed and systemd accepted the request. The host may become unavailable at any moment.");
    });
}

void request_lan_access_toggle() {
    auto& security = prismix::security::SecurityService::instance();
    const bool enable = !security.isLanAccessEnabled();
    if (enable && !security.isPinConfigured()) {
        prismix::ui::security_dialog_show_message(
            "PIN REQUIRED FOR LAN ACCESS",
            "Set up a console PIN before enabling LAN access. The Web UI remains localhost-only until then.");
        return;
    }
    const std::string title = enable ? "ENABLE LAN WEB ACCESS?" : "DISABLE LAN WEB ACCESS?";
    const std::string message = enable
        ? "The Web server will bind to 0.0.0.0 and become reachable from the local network. Every remote request requires the console PIN. This server uses HTTP without TLS, so enable LAN access only on a trusted network."
        : "The Web server will return to 127.0.0.1. Existing remote browser sessions will lose access immediately. The audio engine is not affected.";
    prismix::ui::security_dialog_show_confirm(title, message,
        enable ? "ENABLE LAN" : "USE LOCALHOST ONLY", enable, [enable]() {
            auto& security = prismix::security::SecurityService::instance();
            std::string error;
            if (!security.setLanAccess(enable, error)) {
                prismix::ui::security_dialog_show_message(
                    "LAN ACCESS FAILED",
                    error.empty() ? "The LAN access setting could not be saved." : error);
                return;
            }
            const std::string bind = enable ? "0.0.0.0" : "127.0.0.1";
            if (!prismix::web::WebServer::instance().request_bind_address(bind)) {
                prismix::ui::security_dialog_show_message(
                    "LAN ACCESS FAILED",
                    "The Web listener rejected the requested bind address.");
                return;
            }
            prismix::ui::security_dialog_show_message(
                enable ? "LAN ACCESS ENABLED" : "LOCALHOST ONLY",
                enable
                    ? "The Web listener is rebinding to 0.0.0.0. Remote access requires the console PIN. HTTP has no transport encryption."
                    : "The Web listener is rebinding to 127.0.0.1. Remote browser sessions have been disconnected.");
        });
}

void request_mixer_lock() {
    prismix::ui::security_dialog_show_confirm(
        "LOCK MIXER?",
        "Console and Web controls will be blocked until the PIN is verified. The audio engine continues running; no channel is muted and the DSP/mix state is preserved.",
        "LOCK MIXER", true, []() {
            auto& security = prismix::security::SecurityService::instance();
            std::string error;
            if (!security.lockMixer(error)) {
                prismix::ui::security_dialog_show_message(
                    "LOCK FAILED",
                    error.empty() ? "The mixer could not be locked." : error);
                return;
            }
            prismix::ui::security_lock_overlay_show();
        });
}


void updateScreen(lv_timer_t* timer) {
    auto* view = static_cast<SystemScreenView*>(lv_timer_get_user_data(timer));
    if (!view) return;

    if (view->active_mode == 2) {
        const uint32_t now = lv_tick_get();
        if (now - view->last_security_tick >= 500) {
            view->last_security_tick = now;
            refresh_system_access_controls(view);
        }
        if (now - view->last_power_tick >= 5000) {
            view->last_power_tick = now;
            refresh_system_power_controls(view);
        }
        if (now - view->last_web_tick >= 5000) {
            view->last_web_tick = now;
            refresh_system_web_controls(view);
        }
        if (now - view->last_network_tick >= 5000) {
            const bool force = now - view->last_network_tick >= 30000;
            view->last_network_tick = now;
            refresh_system_network_controls(view, force);
        }
    }

    auto& service = audio_service();
    const auto snapshot = service.snapshot();
    const bool real = (snapshot.mode == AudioMode::RealAudio);
    const bool active = (snapshot.state == AudioServiceState::Streaming);

    if (view->pendingSimulation && snapshot.state == AudioServiceState::Stopped) {
        view->pendingSimulation = false;
        if (service.selectMode(AudioMode::Simulation)) g_backend.start();
    }
    if (view->mode) {
        const uint32_t currentMode = (real ? 1 : 0);
        if (lv_dropdown_get_selected(view->mode) != currentMode) lv_dropdown_set_selected(view->mode, currentMode);
    }
    if (!view->deviceOptionsSet || view->displayedCaptureDeviceId != snapshot.captureDeviceId ||
        view->displayedRenderDeviceId != snapshot.renderDeviceId) {
        view->deviceOptionsSet = true;
        view->displayedCaptureDeviceId = snapshot.captureDeviceId;
        view->displayedRenderDeviceId = snapshot.renderDeviceId;

        bool foundCapture = snapshot.captureDeviceId.empty();
        for (const auto& dev : snapshot.captureDevices) foundCapture = foundCapture || dev.id == snapshot.captureDeviceId;
        view->captureMissingSavedDevice = !foundCapture;
        if (view->capture) {
            lv_dropdown_set_options(view->capture, deviceOptions(snapshot.captureDevices, snapshot.captureDeviceId).c_str());
            uint32_t chosen = 0;
            if (view->captureMissingSavedDevice) chosen = 1;
            else {
                for (uint32_t i = 0; i < snapshot.captureDevices.size(); ++i) {
                    if (snapshot.captureDevices[i].id == snapshot.captureDeviceId) chosen = i + 1;
                }
            }
            lv_dropdown_set_selected(view->capture, chosen);
        }

        bool foundRender = snapshot.renderDeviceId.empty();
        for (const auto& dev : snapshot.renderDevices) foundRender = foundRender || dev.id == snapshot.renderDeviceId;
        view->renderMissingSavedDevice = !foundRender;
        if (view->render) {
            lv_dropdown_set_options(view->render, deviceOptions(snapshot.renderDevices, snapshot.renderDeviceId).c_str());
            uint32_t chosen = 0;
            if (view->renderMissingSavedDevice) chosen = 1;
            else {
                for (uint32_t i = 0; i < snapshot.renderDevices.size(); ++i) {
                    if (snapshot.renderDevices[i].id == snapshot.renderDeviceId) chosen = i + 1;
                }
            }
            lv_dropdown_set_selected(view->render, chosen);
        }
    }

    if (view->start) {
        lv_obj_t* label = lv_obj_get_child(view->start, 0);
        if (active) {
            if (label) lv_label_set_text(label, "STOP REAL AUDIO");
            lv_obj_set_style_bg_color(view->start, lv_color_hex(0x5A1B1B), 0);
            lv_obj_set_style_border_color(view->start, COLOR_RED, 0);
            if (label) lv_obj_set_style_text_color(label, lv_color_hex(0xFCA5A5), 0);
        } else {
            if (label) lv_label_set_text(label, "START REAL AUDIO");
            lv_obj_set_style_bg_color(view->start, COLOR_CONTROL, 0);
            lv_obj_set_style_border_color(view->start, COLOR_EDGE, 0);
            if (label) lv_obj_set_style_text_color(label, COLOR_TEXT, 0);
        }
        if (snapshot.state == AudioServiceState::Initializing || snapshot.state == AudioServiceState::Stopping) {
            lv_obj_add_state(view->start, LV_STATE_DISABLED);
        } else {
            lv_obj_remove_state(view->start, LV_STATE_DISABLED);
        }
    }

    if (view->state) {
        const char* modeText = (real ? "REAL AUDIO" : "SIMULATION");
        const char* stText = (snapshot.state == AudioServiceState::Streaming ? "STREAMING" :
                              snapshot.state == AudioServiceState::Initializing ? "INITIALIZING" :
                              snapshot.state == AudioServiceState::Stopping ? "STOPPING" :
                              snapshot.state == AudioServiceState::Error ? "ERROR" : "STOPPED");
        lv_label_set_text_fmt(view->state, "BACKEND: %s    STATE: %s", modeText, stText);
        lv_obj_set_style_text_color(view->state,
            snapshot.state == AudioServiceState::Streaming ? COLOR_GREEN :
            snapshot.state == AudioServiceState::Error ? COLOR_RED : COLOR_CYAN, 0);
    }

    if (view->requested) {
        const auto& persisted = ui_get_persisted_audio_setup();
        lv_label_set_text_fmt(view->requested, "REQUESTED: %u Hz / %u frames / Native JACK",
                              persisted.requestedSampleRate, persisted.requestedBufferFrames);
    }

    if (view->negotiated) {
        if (real && snapshot.actualSampleRate > 0) {
            lv_label_set_text_fmt(view->negotiated, "ACTIVE JACK STREAM: %.0f Hz, %u frames buffer (%.1f ms)",
                                  snapshot.actualSampleRate, snapshot.actualBufferFrames,
                                  (static_cast<float>(snapshot.actualBufferFrames) * 1000.0f) / snapshot.actualSampleRate);
        } else if (real) {
            lv_label_set_text(view->negotiated, "ACTIVE JACK STREAM: (idle - not started)");
        } else {
            const auto config = g_backend.get_config();
            lv_label_set_text_fmt(view->negotiated, "SIMULATED FORMAT: %d Hz / %d-bit",
                                  static_cast<int>(config.sample_rate),
                                  config.bit_depth == BitDepth::BIT_16 ? 16 :
                                  config.bit_depth == BitDepth::BIT_24 ? 24 : 32);
        }
    }

    if (view->metrics) {
        if (active) {
            lv_label_set_text_fmt(view->metrics,
                "REALTIME METRICS\n"
                "INPUT PEAK     L %6.1f dBFS   R %6.1f dBFS\n"
                "INPUT RMS      L %6.1f dBFS   R %6.1f dBFS\n"
                "OUTPUT PEAK    L %6.1f dBFS   R %6.1f dBFS\n"
                "OUTPUT RMS     L %6.1f dBFS   R %6.1f dBFS\n"
                "COMPRESSOR GR  %6.1f dB\nDSP BLOCK TIME  %llu us\nRENDER UNDERRUNS %u",
                toDb(snapshot.inputPeak[0]), toDb(snapshot.inputPeak[1]),
                toDb(snapshot.inputRms[0]), toDb(snapshot.inputRms[1]),
                toDb(snapshot.outputPeak[0]), toDb(snapshot.outputPeak[1]),
                toDb(snapshot.outputRms[0]), toDb(snapshot.outputRms[1]),
                snapshot.compressorGainReductionDb,
                static_cast<unsigned long long>(snapshot.dspTimeUs), snapshot.underruns);
        } else if (real) {
            lv_label_set_text(view->metrics, "LIVE METERS: waiting for a real JACK stream");
        } else {
            const auto config = g_backend.get_config();
            lv_label_set_text_fmt(view->metrics,
                "SIMULATION TELEMETRY\nMOCK CPU LOAD   %.1f %%\nMOCK XRUNS      %u\n"
                "Meters follow the strips assigned in Simulation.", config.cpu_load, config.xruns);
        }
    }
    if (view->error) {
        lv_label_set_text(view->error, snapshot.error.empty() ? "" : snapshot.error.c_str());
    }

    const bool controlsEnabled = real;
    for (lv_obj_t* object : {view->gain, view->pan, view->bypass, view->peq, view->compressor}) {
        if (!object) continue;
        if (controlsEnabled) lv_obj_remove_state(object, LV_STATE_DISABLED);
        else lv_obj_add_state(object, LV_STATE_DISABLED);
    }
    for (lv_obj_t* object : {view->capture, view->render}) {
        if (!object) continue;
        if (!active && real) lv_obj_remove_state(object, LV_STATE_DISABLED);
        else lv_obj_add_state(object, LV_STATE_DISABLED);
    }
    for (lv_obj_t* object : view->simulationControls) {
        if (!object) continue;
        if (real) lv_obj_add_state(object, LV_STATE_DISABLED);
        else lv_obj_remove_state(object, LV_STATE_DISABLED);
    }
}

// -----------------------------------------------------------------------------
// Appearance & Theme Editor Synchronization
// -----------------------------------------------------------------------------
void sync_theme_editor_controls(SystemScreenView* v) {
    if (!v) return;
    const auto& cfg = theme_get_config();
    const auto activeColors = theme_get_active_colors();

    // 1. Update Preset Selection Highlights
    for (int p = 0; p < 5; ++p) {
        if (v->preset_btns[p]) {
            const bool is_active = (static_cast<int>(cfg.active_preset) == p);
            lv_obj_set_style_bg_color(v->preset_btns[p], is_active ? COLOR_RAISED_PANEL : COLOR_CONTROL, 0);
            lv_obj_set_style_border_color(v->preset_btns[p], is_active ? COLOR_SELECTED_BORDER : COLOR_STANDARD_BORDER, 0);
            lv_obj_set_style_border_width(v->preset_btns[p], is_active ? 2 : 1, 0);
            if (lv_obj_get_child_count(v->preset_btns[p]) > 0) {
                lv_obj_set_style_text_color(lv_obj_get_child(v->preset_btns[p], 0),
                    is_active ? COLOR_PRIMARY_TEXT : COLOR_SECONDARY_TEXT, 0);
            }
        }
    }

    // Update active preset badge
    if (v->active_preset_badge) {
        char p_str[64];
        std::snprintf(p_str, sizeof(p_str), "ACTIVE: %s",
                      theme_preset_name(cfg.active_preset));
        lv_label_set_text(v->active_preset_badge, p_str);
    }

    // 2. Update Role Selection Cards (swatches and hex labels)
    for (int r = 0; r < 12; ++r) {
        auto role = static_cast<ThemeColorRole>(r);
        uint32_t col = activeColors.get_role(role);
        if (v->role_swatches[r]) {
            lv_obj_set_style_bg_color(v->role_swatches[r], lv_color_hex(col), 0);
        }
        if (v->role_hex_labels[r]) {
            lv_label_set_text(v->role_hex_labels[r], format_hex_color(col).c_str());
        }
        if (v->role_cards[r]) {
            const bool is_sel = (v->selected_role == role);
            lv_obj_set_style_border_color(v->role_cards[r], is_sel ? COLOR_SELECTED_BORDER : COLOR_STANDARD_BORDER, 0);
            lv_obj_set_style_border_width(v->role_cards[r], is_sel ? 2 : 1, 0);
            lv_obj_set_style_bg_color(v->role_cards[r], is_sel ? COLOR_RAISED_PANEL : COLOR_CONTROL, 0);
        }
    }

    // 3. Update Active Role Editing Area
    uint32_t selCol = activeColors.get_role(v->selected_role);
    int r_val = (selCol >> 16) & 0xFF;
    int g_val = (selCol >> 8) & 0xFF;
    int b_val = selCol & 0xFF;

    if (v->editing_role_title) {
        char t_str[96];
        std::snprintf(t_str, sizeof(t_str), "EDITING: %s", theme_role_name(v->selected_role));
        lv_label_set_text(v->editing_role_title, t_str);
    }
    if (v->active_swatch) {
        lv_obj_set_style_bg_color(v->active_swatch, lv_color_hex(selCol), 0);
    }
    if (v->hex_textarea) {
        lv_textarea_set_text(v->hex_textarea, format_hex_color(selCol).c_str());
    }
    if (v->slider_r) lv_slider_set_value(v->slider_r, r_val, LV_ANIM_OFF);
    if (v->slider_g) lv_slider_set_value(v->slider_g, g_val, LV_ANIM_OFF);
    if (v->slider_b) lv_slider_set_value(v->slider_b, b_val, LV_ANIM_OFF);

    if (v->val_r_lbl) lv_label_set_text_fmt(v->val_r_lbl, "RED: %d", r_val);
    if (v->val_g_lbl) lv_label_set_text_fmt(v->val_g_lbl, "GREEN: %d", g_val);
    if (v->val_b_lbl) lv_label_set_text_fmt(v->val_b_lbl, "BLUE: %d", b_val);

    // 4. Update Live Hardware Preview Elements
    if (v->preview_panel) {
        ui::rack_skin::apply_faceplate_panel(v->preview_panel, COLOR_STANDARD_BORDER, COLOR_PANEL_SURFACE);
    }
    if (v->preview_btn_norm) {
        ui::rack_skin::apply_hardware_button(v->preview_btn_norm, false, COLOR_STANDARD_BORDER, COLOR_CONTROL, COLOR_RECESSED);
        if (lv_obj_get_child_count(v->preview_btn_norm) > 0) {
            lv_obj_set_style_text_color(lv_obj_get_child(v->preview_btn_norm, 0), COLOR_SECONDARY_TEXT, 0);
        }
    }
    if (v->preview_btn_act) {
        ui::rack_skin::apply_hardware_button(v->preview_btn_act, true, COLOR_SELECTED_BORDER, COLOR_RAISED_PANEL, COLOR_CONTROL);
        if (lv_obj_get_child_count(v->preview_btn_act) > 0) {
            lv_obj_set_style_text_color(lv_obj_get_child(v->preview_btn_act, 0), COLOR_PRIMARY_TEXT, 0);
        }
    }
    if (v->preview_knob_group) {
        instrument_knob_refresh(v->preview_knob_group);
    }
    if (v->preview_lcd_box) {
        ui::rack_skin::apply_recessed_display(v->preview_lcd_box, COLOR_RECESSED, COLOR_SELECTED_BORDER);
    }
    if (v->preview_lcd_line1) {
        lv_obj_set_style_text_color(v->preview_lcd_line1, COLOR_PRIMARY_TEXT, 0);
    }
    if (v->preview_lcd_line2) {
        lv_obj_set_style_text_color(v->preview_lcd_line2, COLOR_PRIMARY_TEXT, 0);
    }
    if (v->preview_txt_primary) {
        lv_obj_set_style_text_color(v->preview_txt_primary, COLOR_PRIMARY_TEXT, 0);
    }
    if (v->preview_txt_secondary) {
        lv_obj_set_style_text_color(v->preview_txt_secondary, COLOR_SECONDARY_TEXT, 0);
    }
    if (v->preview_accent1_pill) {
        lv_obj_set_style_border_color(v->preview_accent1_pill, COLOR_ACCENT, 0);
        if (lv_obj_get_child_count(v->preview_accent1_pill) > 0) {
            lv_obj_set_style_text_color(lv_obj_get_child(v->preview_accent1_pill, 0), COLOR_ACCENT, 0);
        }
    }
    if (v->preview_accent2_pill) {
        lv_obj_set_style_border_color(v->preview_accent2_pill, COLOR_CYAN, 0);
        if (lv_obj_get_child_count(v->preview_accent2_pill) > 0) {
            lv_obj_set_style_text_color(lv_obj_get_child(v->preview_accent2_pill, 0), COLOR_CYAN, 0);
        }
    }
}

} // anonymous namespace

void system_screen_set_mode(int mode) {
    s_system_mode = std::clamp(mode, 0, 2);
    if (s_active_system_view) {
        s_active_system_view->active_mode = s_system_mode;
        s_active_system_view->last_security_tick = 0;
        s_active_system_view->last_power_tick = 0;
        s_active_system_view->last_network_tick = 0;
        s_active_system_view->last_web_tick = 0;
        update_system_mode_view(s_active_system_view);
    }
}

void system_screen_refresh_theme() {
    if (s_active_system_view) {
        sync_theme_editor_controls(s_active_system_view);
    }
}

lv_obj_t* system_screen_create(void) {
    auto* screen = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(screen, 0, 0);
    lv_obj_set_size(screen, 1920, 1080);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, COLOR_BG, 0);

    auto* view = new SystemScreenView{};
    s_active_system_view = view;
    view->active_mode = s_system_mode;

    auto& service = audio_service();
    ui_load_persisted_audio_setup();
    const auto& persistedSetup = ui_get_persisted_audio_setup();
    service.refreshDevices();

    // -------------------------------------------------------------
    // TOP SUB-NAVIGATION TOGGLE BAR (Audio Engine vs UI Theme)
    // -------------------------------------------------------------
    auto* top_bar = lv_obj_create(screen);
    lv_obj_set_pos(top_bar, 16, 48);
    lv_obj_set_size(top_bar, 1888, 44);
    lv_obj_remove_flag(top_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(top_bar, COLOR_PANEL, 0);
    lv_obj_set_style_border_color(top_bar, COLOR_EDGE, 0);
    lv_obj_set_style_border_width(top_bar, 1, 0);
    lv_obj_set_style_radius(top_bar, 1, 0);
    lv_obj_set_style_pad_all(top_bar, 4, 0);

    view->mode_audio_btn = lv_button_create(top_bar);
    lv_obj_set_size(view->mode_audio_btn, 240, 34);
    lv_obj_set_pos(view->mode_audio_btn, 4, 1);
    lv_obj_set_style_radius(view->mode_audio_btn, 1, 0);
    auto* al = lv_label_create(view->mode_audio_btn);
    lv_label_set_text(al, "AUDIO ENGINE & DSP");
    lv_obj_center(al);
    lv_obj_set_style_text_font(al, &ac_font_14, 0);

    view->mode_theme_btn = lv_button_create(top_bar);
    lv_obj_set_size(view->mode_theme_btn, 240, 34);
    lv_obj_set_pos(view->mode_theme_btn, 252, 1);
    lv_obj_set_style_radius(view->mode_theme_btn, 1, 0);
    auto* tl = lv_label_create(view->mode_theme_btn);
    lv_label_set_text(tl, "APPEARANCE & UI THEME");
    lv_obj_center(tl);
    lv_obj_set_style_text_font(tl, &ac_font_14, 0);

    view->mode_system_btn = lv_button_create(top_bar);
    lv_obj_set_size(view->mode_system_btn, 240, 34);
    lv_obj_set_pos(view->mode_system_btn, 500, 1);
    lv_obj_set_style_radius(view->mode_system_btn, 1, 0);
    auto* sl = lv_label_create(view->mode_system_btn);
    lv_label_set_text(sl, "SYSTEM & NETWORK");
    lv_obj_center(sl);
    lv_obj_set_style_text_font(sl, &ac_font_14, 0);

    // =============================================================
    // 1. AUDIO ENGINE & DSP VIEW CONTAINER
    // =============================================================
    view->audio_container = lv_obj_create(screen);
    lv_obj_set_pos(view->audio_container, 0, 94);
    lv_obj_set_size(view->audio_container, 1920, 920);
    lv_obj_remove_flag(view->audio_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(view->audio_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(view->audio_container, 0, 0);
    lv_obj_set_style_pad_all(view->audio_container, 0, 0);

    // PANEL 1: AUDIO INTERFACE & BACKEND
#if defined(_WIN32)
    auto* backendPanel = instrument_panel(view->audio_container, 16, 8, 600, 866, "AUDIO BACKEND / WASAPI SHARED MODE");
#else
    auto* backendPanel = instrument_panel(view->audio_container, 16, 8, 600, 866, "AUDIO BACKEND / NATIVE JACK REALTIME");
#endif
    instrument_label(backendPanel, "BACKEND SELECTION", 24, 50, &ac_font_12, COLOR_TEXT2);
    view->mode = lv_dropdown_create(backendPanel);
    lv_obj_set_pos(view->mode, 24, 72);
    lv_obj_set_size(view->mode, 540, 44);
    styleDropdown(view->mode);
#if defined(_WIN32)
    lv_dropdown_set_options(view->mode, "SIMULATION\nREAL AUDIO / WASAPI SHARED");
#else
    lv_dropdown_set_options(view->mode, "SIMULATION\nREAL AUDIO / JACK REALTIME");
#endif
    lv_obj_add_event_cb(view->mode, [](lv_event_t* event) {
        auto* v = static_cast<SystemScreenView*>(lv_event_get_user_data(event));
        if (!v) return;
        const uint32_t selected = lv_dropdown_get_selected(static_cast<lv_obj_t*>(lv_event_get_target(event)));
        auto& backend = audio_service();
        if (selected == 1) {
            std::string err;
            if (!ui_start_real_audio(&err)) {
                if (v->error) lv_label_set_text(v->error, err.c_str());
                lv_dropdown_set_selected(v->mode, 0);
            }
        } else {
            const auto state = backend.snapshot().state;
            if (state == AudioServiceState::Streaming || state == AudioServiceState::Initializing ||
                state == AudioServiceState::Stopping) {
                v->pendingSimulation = true;
                backend.stop();
            } else if (backend.selectMode(AudioMode::Simulation)) {
                g_backend.start();
            }
        }
    }, LV_EVENT_VALUE_CHANGED, view);

    instrument_label(backendPanel, "CAPTURE DEVICE", 24, 140, &ac_font_12, COLOR_TEXT2);
    view->capture = lv_dropdown_create(backendPanel);
    lv_obj_set_pos(view->capture, 24, 162);
    lv_obj_set_size(view->capture, 540, 44);
    styleDropdown(view->capture);
    lv_dropdown_set_options(view->capture, "DEFAULT ENDPOINT\nLoading capture devices...");
    lv_obj_add_event_cb(view->capture, [](lv_event_t* event) {
        auto* v = static_cast<SystemScreenView*>(lv_event_get_user_data(event));
        auto* dd = static_cast<lv_obj_t*>(lv_event_get_target(event));
        const auto snapshot = audio_service().snapshot();
        const uint32_t selected = lv_dropdown_get_selected(dd);
        if (!v) return;
        if (v->captureMissingSavedDevice && selected == 1) return;
        const uint32_t offset = v->captureMissingSavedDevice ? 1u : 0u;
        const std::string id = selected > offset && selected <= snapshot.captureDevices.size() + offset
            ? snapshot.captureDevices[selected - 1 - offset].id : std::string{};
        if (audio_service().selectCaptureDevice(id)) {
            ui_get_persisted_audio_setup().captureDeviceId = id;
            persistSetup();
            if (v->captureMissingSavedDevice) {
                v->captureMissingSavedDevice = false;
                lv_dropdown_set_options(dd, deviceOptions(snapshot.captureDevices, "").c_str());
                uint32_t chosen = 0;
                for (uint32_t i = 0; i < snapshot.captureDevices.size(); ++i)
                    if (snapshot.captureDevices[i].id == id) chosen = i + 1;
                lv_dropdown_set_selected(dd, chosen);
            }
        }
    }, LV_EVENT_VALUE_CHANGED, view);

    instrument_label(backendPanel, "RENDER DEVICE", 24, 218, &ac_font_12, COLOR_TEXT2);
    view->render = lv_dropdown_create(backendPanel);
    lv_obj_set_pos(view->render, 24, 240);
    lv_obj_set_size(view->render, 540, 44);
    styleDropdown(view->render);
    lv_dropdown_set_options(view->render, "DEFAULT ENDPOINT\nLoading render devices...");
    lv_obj_add_event_cb(view->render, [](lv_event_t* event) {
        auto* v = static_cast<SystemScreenView*>(lv_event_get_user_data(event));
        auto* dd = static_cast<lv_obj_t*>(lv_event_get_target(event));
        const auto snapshot = audio_service().snapshot();
        const uint32_t selected = lv_dropdown_get_selected(dd);
        if (!v) return;
        if (v->renderMissingSavedDevice && selected == 1) return;
        const uint32_t offset = v->renderMissingSavedDevice ? 1u : 0u;
        const std::string id = selected > offset && selected <= snapshot.renderDevices.size() + offset
            ? snapshot.renderDevices[selected - 1 - offset].id : std::string{};
        if (audio_service().selectRenderDevice(id)) {
            ui_get_persisted_audio_setup().renderDeviceId = id;
            persistSetup();
            if (v->renderMissingSavedDevice) {
                v->renderMissingSavedDevice = false;
                lv_dropdown_set_options(dd, deviceOptions(snapshot.renderDevices, "").c_str());
                uint32_t chosen = 0;
                for (uint32_t i = 0; i < snapshot.renderDevices.size(); ++i)
                    if (snapshot.renderDevices[i].id == id) chosen = i + 1;
                lv_dropdown_set_selected(dd, chosen);
            }
        }
    }, LV_EVENT_VALUE_CHANGED, view);

    view->start = instrument_button(backendPanel, "START REAL AUDIO", 24, 306, 250, 52, nullptr);
    lv_obj_add_event_cb(view->start, [](lv_event_t* event) {
        auto* button = static_cast<lv_obj_t*>(lv_event_get_target(event));
        const auto state = audio_service().snapshot().state;
        if (state == AudioServiceState::Streaming) {
            ui_stop_real_audio();
        } else {
            std::string configError;
            if (!ui_start_real_audio(&configError)) {
                if (auto* v = static_cast<SystemScreenView*>(lv_event_get_user_data(event)); v && v->error)
                    lv_label_set_text(v->error, configError.c_str());
            }
        }
        lv_obj_add_state(button, LV_STATE_DISABLED);
    }, LV_EVENT_CLICKED, view);

    view->state = instrument_label(backendPanel, "BACKEND: SIMULATION    STATE: STOPPED", 24, 374, &ac_font_14, COLOR_CYAN);
    view->requested = instrument_label(backendPanel, "REQUESTED: 48,000 Hz / 128 frames / Native JACK", 24, 414, &ac_font_12, COLOR_TEXT2);
    view->negotiated = instrument_label(backendPanel, "SIMULATED FORMAT: 48000 Hz / 24-bit", 24, 448, &ac_font_12, COLOR_TEXT);
    lv_obj_set_width(view->negotiated, 540);
    lv_label_set_long_mode(view->negotiated, LV_LABEL_LONG_WRAP);

    view->error = instrument_label(backendPanel, "", 24, 520, &ac_font_12, COLOR_RED);
    lv_obj_set_width(view->error, 540);
    lv_label_set_long_mode(view->error, LV_LABEL_LONG_WRAP);

    // Offline Simulator Bench subsection
    instrument_label(backendPanel, "OFFLINE SIMULATION CONTROLS", 24, 620, &ac_font_12, COLOR_TEXT2);
    auto* signalButton = instrument_button(backendPanel, g_backend.get_generator_signal_name(), 24, 648, 540, 38, nullptr);
    view->simulationControls[0] = signalButton;
    lv_obj_add_event_cb(signalButton, [](lv_event_t* event) {
        const auto next = (static_cast<int>(g_backend.get_generator_signal()) + 1) % 5;
        g_backend.set_generator_signal(static_cast<GeneratorSignal>(next));
        lv_label_set_text(lv_obj_get_child(static_cast<lv_obj_t*>(lv_event_get_target(event)), 0),
                          g_backend.get_generator_signal_name());
    }, LV_EVENT_CLICKED, nullptr);

    auto* sampleRate = lv_dropdown_create(backendPanel);
    view->simulationControls[1] = sampleRate;
    lv_obj_set_pos(sampleRate, 24, 696);
    lv_obj_set_size(sampleRate, 540, 38);
    styleDropdown(sampleRate);
    lv_dropdown_set_options(sampleRate, "44.1 kHz\n48 kHz\n88.2 kHz\n96 kHz");
    const int sampleRateIndex[] = {44100, 48000, 88200, 96000};
    const auto mockConfig = g_backend.get_config();
    for (uint32_t i = 0; i < 4; ++i) {
        if (static_cast<int>(mockConfig.sample_rate) == sampleRateIndex[i]) lv_dropdown_set_selected(sampleRate, i);
    }
    lv_obj_add_event_cb(sampleRate, [](lv_event_t* event) {
        const int rates[] = {44100, 48000, 88200, 96000};
        auto config = g_backend.get_config();
        config.sample_rate = static_cast<SampleRate>(rates[lv_dropdown_get_selected(
            static_cast<lv_obj_t*>(lv_event_get_target(event)))]);
        g_backend.set_config(config);
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    auto* bufferSize = lv_dropdown_create(backendPanel);
    view->simulationControls[2] = bufferSize;
    lv_obj_set_pos(bufferSize, 24, 744);
    lv_obj_set_size(bufferSize, 170, 34);
    styleDropdown(bufferSize);
    lv_dropdown_set_options(bufferSize, "BUFFER 128\nBUFFER 256\nBUFFER 512\nBUFFER 1024");
    const int bufferValues[] = {128, 256, 512, 1024};
    for (uint32_t i = 0; i < 4; ++i) {
        if (mockConfig.buffer_size == bufferValues[i]) lv_dropdown_set_selected(bufferSize, i);
    }
    lv_obj_add_event_cb(bufferSize, [](lv_event_t* event) {
        const int buffers[] = {128, 256, 512, 1024};
        auto config = g_backend.get_config();
        config.buffer_size = buffers[lv_dropdown_get_selected(static_cast<lv_obj_t*>(lv_event_get_target(event)))];
        g_backend.set_config(config);
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    auto* bitDepth = lv_dropdown_create(backendPanel);
    view->simulationControls[3] = bitDepth;
    lv_obj_set_pos(bitDepth, 210, 744);
    lv_obj_set_size(bitDepth, 170, 34);
    styleDropdown(bitDepth);
    lv_dropdown_set_options(bitDepth, "16-bit mock\n24-bit mock\n32-bit mock");
    const int bitValues[] = {16, 24, 32};
    for (uint32_t i = 0; i < 3; ++i) {
        if (static_cast<int>(mockConfig.bit_depth) == bitValues[i]) lv_dropdown_set_selected(bitDepth, i);
    }
    lv_obj_add_event_cb(bitDepth, [](lv_event_t* event) {
        const int bits[] = {16, 24, 32};
        auto config = g_backend.get_config();
        config.bit_depth = static_cast<BitDepth>(bits[lv_dropdown_get_selected(
            static_cast<lv_obj_t*>(lv_event_get_target(event)))]);
        g_backend.set_config(config);
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    // PANEL 2: ENGINE TELEMETRY & LATENCY
    auto* meterPanel = instrument_panel(view->audio_container, 632, 8, 640, 866, "ENGINE TELEMETRY & LATENCY");
    instrument_label(meterPanel, "TELEMETRY SOURCE", 24, 52, &ac_font_12, COLOR_TEXT2);
    instrument_label(meterPanel, "Native JACK / ALSA atomic telemetry bridge", 24, 80, &ac_font_14, COLOR_CYAN);
    view->metrics = instrument_label(meterPanel, "SIMULATION TELEMETRY", 24, 148, &ac_font_16, COLOR_TEXT);
    lv_obj_set_width(view->metrics, 590);
    lv_label_set_long_mode(view->metrics, LV_LABEL_LONG_WRAP);

    auto* resetMeters = instrument_button(meterPanel, "RESET METERS", 24, 548, 220, 42, nullptr);
    lv_obj_add_event_cb(resetMeters, [](lv_event_t*) {
        for (auto& channel : g_mixer_state.channels) {
            channel.peak_l = channel.peak_r = -90.0f;
            channel.peak_hold_l = channel.peak_hold_r = -90.0f;
            channel.rms_l = channel.rms_r = -90.0f;
            channel.clip = false;
        }
        g_mixer_state.master.peak_l = g_mixer_state.master.peak_r = -90.0f;
        g_mixer_state.master.peak_hold_l = g_mixer_state.master.peak_hold_r = -90.0f;
        g_mixer_state.master.rms_l = g_mixer_state.master.rms_r = -90.0f;
        g_mixer_state.master.clip = false;
        g_backend.get_analyzer().clear_peaks();
    }, LV_EVENT_CLICKED, nullptr);

    // PANEL 3: MASTER OUTPUT DSP CONTROLS
    auto* controlPanel = instrument_panel(view->audio_container, 1288, 8, 616, 866, "MASTER OUTPUT DSP CONTROLS");
    instrument_label(controlPanel, "PROCESSING ORDER", 24, 52, &ac_font_12, COLOR_TEXT2);
    instrument_label(controlPanel, "MAIN L/R SUM -> MASTER GAIN / PAN -> PEQ / COMP -> OUTPUT", 24, 80, &ac_font_12, COLOR_CYAN);
    instrument_label(controlPanel, "GAIN  /  -60.0 to +12.0 dB", 24, 142, &ac_font_14, COLOR_TEXT2);
    view->gain = lv_slider_create(controlPanel);
    lv_obj_set_pos(view->gain, 24, 174);
    lv_obj_set_size(view->gain, 440, 28);
    styleSlider(view->gain);
    lv_slider_set_range(view->gain, 0, 720);
    lv_slider_set_value(view->gain, static_cast<int32_t>((persistedSetup.channels.front().gainDb + 60.0f) * 10.0f), LV_ANIM_OFF);
    char gainText[24];
    std::snprintf(gainText, sizeof(gainText), "%.1f dB", persistedSetup.channels.front().gainDb);
    view->gain_value = instrument_label(controlPanel, gainText, 478, 174, &ac_font_14, COLOR_TEXT);
    lv_obj_add_event_cb(view->gain, [](lv_event_t* event) {
        auto* v = static_cast<SystemScreenView*>(lv_event_get_user_data(event));
        const int value = lv_slider_get_value(static_cast<lv_obj_t*>(lv_event_get_target(event)));
        const float db = static_cast<float>(value) / 10.0f - 60.0f;
        audio_service().setGainDb(db);
        ui_get_persisted_audio_setup().channels.front().gainDb = db;
        persistSetup();
        lv_label_set_text_fmt(v->gain_value, "%.1f dB", db);
    }, LV_EVENT_VALUE_CHANGED, view);

    instrument_label(controlPanel, "PAN  /  -1.0 LEFT TO +1.0 RIGHT", 24, 232, &ac_font_14, COLOR_TEXT2);
    view->pan = lv_slider_create(controlPanel);
    lv_obj_set_pos(view->pan, 24, 264);
    lv_obj_set_size(view->pan, 440, 28);
    styleSlider(view->pan);
    lv_slider_set_range(view->pan, 0, 200);
    lv_slider_set_value(view->pan, static_cast<int32_t>((persistedSetup.channels.front().pan + 1.0f) * 100.0f), LV_ANIM_OFF);
    char panText[24];
    std::snprintf(panText, sizeof(panText), "%+.2f", persistedSetup.channels.front().pan);
    view->pan_value = instrument_label(controlPanel, panText, 478, 264, &ac_font_14, COLOR_TEXT);
    lv_obj_add_event_cb(view->pan, [](lv_event_t* event) {
        auto* v = static_cast<SystemScreenView*>(lv_event_get_user_data(event));
        const float pan = static_cast<float>(lv_slider_get_value(static_cast<lv_obj_t*>(lv_event_get_target(event)))) / 100.0f - 1.0f;
        audio_service().setPan(pan);
        ui_get_persisted_audio_setup().channels.front().pan = pan;
        persistSetup();
        lv_label_set_text_fmt(v->pan_value, "%+.2f", pan);
    }, LV_EVENT_VALUE_CHANGED, view);

    instrument_label(controlPanel, "ENGINE ENABLES", 24, 326, &ac_font_12, COLOR_TEXT2);
    view->bypass = lv_switch_create(controlPanel);
    lv_obj_set_pos(view->bypass, 24, 358);
    styleSwitch(view->bypass);
    if (persistedSetup.channels.front().dspBypass) lv_obj_add_state(view->bypass, LV_STATE_CHECKED);
    lv_obj_add_event_cb(view->bypass, [](lv_event_t* event) {
        const bool enabled = lv_obj_has_state(static_cast<lv_obj_t*>(lv_event_get_target(event)), LV_STATE_CHECKED);
        audio_service().setDspBypass(enabled);
        ui_get_persisted_audio_setup().channels.front().dspBypass = enabled;
        persistSetup();
    }, LV_EVENT_VALUE_CHANGED, view);
    instrument_label(controlPanel, "DSP BYPASS", 94, 358, &ac_font_14, COLOR_TEXT);

    view->peq = lv_switch_create(controlPanel);
    lv_obj_set_pos(view->peq, 24, 418);
    styleSwitch(view->peq);
    if (persistedSetup.channels.front().peqEnabled) lv_obj_add_state(view->peq, LV_STATE_CHECKED);
    lv_obj_add_event_cb(view->peq, [](lv_event_t* event) {
        const bool enabled = lv_obj_has_state(static_cast<lv_obj_t*>(lv_event_get_target(event)), LV_STATE_CHECKED);
        audio_service().setPeqEnabled(enabled);
        ui_get_persisted_audio_setup().channels.front().peqEnabled = enabled;
        persistSetup();
    }, LV_EVENT_VALUE_CHANGED, view);
    instrument_label(controlPanel, "PARAMETRIC EQ ENABLE", 94, 418, &ac_font_14, COLOR_TEXT);

    view->compressor = lv_switch_create(controlPanel);
    lv_obj_set_pos(view->compressor, 24, 478);
    styleSwitch(view->compressor);
    if (persistedSetup.channels.front().compressorEnabled) lv_obj_add_state(view->compressor, LV_STATE_CHECKED);
    lv_obj_add_event_cb(view->compressor, [](lv_event_t* event) {
        const bool enabled = lv_obj_has_state(static_cast<lv_obj_t*>(lv_event_get_target(event)), LV_STATE_CHECKED);
        audio_service().setCompressorEnabled(enabled);
        ui_get_persisted_audio_setup().channels.front().compressorEnabled = enabled;
        persistSetup();
    }, LV_EVENT_VALUE_CHANGED, view);
    instrument_label(controlPanel, "COMPRESSOR ENABLE", 94, 478, &ac_font_14, COLOR_TEXT);

    instrument_label(controlPanel, "ROUTING TOPOLOGY", 24, 558, &ac_font_12, COLOR_TEXT2);
    instrument_label(controlPanel, "INDEPENDENT STRIP PROCESSING -> MASTER SUMMING", 24, 588, &ac_font_12, COLOR_TEXT);

    // =============================================================
    // 2. APPEARANCE & UI THEME VIEW CONTAINER
    // =============================================================
    view->theme_container = lv_obj_create(screen);
    lv_obj_set_pos(view->theme_container, 0, 94);
    lv_obj_set_size(view->theme_container, 1920, 920);
    lv_obj_remove_flag(view->theme_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(view->theme_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(view->theme_container, 0, 0);
    lv_obj_set_style_pad_all(view->theme_container, 0, 0);

    // -------------------------------------------------------------
    // THEME PANEL 1: BUILT-IN PRESETS
    // -------------------------------------------------------------
    auto* presetPanel = instrument_panel(view->theme_container, 16, 8, 520, 866, "THEME PRESETS");

    static const ThemePreset kPresets[5] = {
        ThemePreset::Graphite,
        ThemePreset::LexiconInspired,
        ThemePreset::StudioBlue,
        ThemePreset::Monochrome,
        ThemePreset::Custom
    };

    static const char* kPresetLabels[5] = {
        "GRAPHITE (DEFAULT)",
        "LEXICON",
        "STUDIO BLUE",
        "MONOCHROME",
        "CUSTOM"
    };

    for (int p = 0; p < 5; ++p) {
        int py = 48 + p * 50;
        auto* pbtn = lv_button_create(presetPanel);
        lv_obj_set_pos(pbtn, 24, py);
        lv_obj_set_size(pbtn, 472, 42);
        lv_obj_set_style_radius(pbtn, 2, 0);
        view->preset_btns[p] = pbtn;

        auto* title = lv_label_create(pbtn);
        lv_label_set_text(title, kPresetLabels[p]);
        lv_obj_center(title);
        lv_obj_set_style_text_font(title, &ac_font_14, 0);

        struct PresetBtnCtx { SystemScreenView* v; ThemePreset preset; };
        auto* ctx = new PresetBtnCtx{view, kPresets[p]};
        lv_obj_add_event_cb(pbtn, [](lv_event_t* e) {
            auto* c = static_cast<PresetBtnCtx*>(lv_event_get_user_data(e));
            if (!c || !c->v) return;
            theme_set_preset(c->preset);
            if (c->v->save_status_lbl) lv_label_set_text(c->v->save_status_lbl, "");
        }, LV_EVENT_CLICKED, ctx);
        lv_obj_add_event_cb(pbtn, [](lv_event_t* e) {
            auto* c = static_cast<PresetBtnCtx*>(lv_event_get_user_data(e));
            delete c;
        }, LV_EVENT_DELETE, ctx);
    }

    view->active_preset_badge = instrument_label(presetPanel, "ACTIVE: GRAPHITE", 24, 308, &ac_font_14, COLOR_CYAN);

    // Save Theme action button
    view->save_theme_btn = instrument_button(presetPanel, "SAVE THEME", 24, 344, 472, 44, nullptr);
    lv_obj_set_style_bg_color(view->save_theme_btn, COLOR_CONTROL, 0);
    lv_obj_set_style_border_color(view->save_theme_btn, COLOR_SELECTED_BORDER, 0);
    lv_obj_set_style_border_width(view->save_theme_btn, 2, 0);
    auto* save_lbl = lv_obj_get_child(view->save_theme_btn, 0);
    if (save_lbl) {
        lv_obj_set_style_text_font(save_lbl, &ac_font_14, 0);
        lv_obj_set_style_text_color(save_lbl, COLOR_PRIMARY_TEXT, 0);
    }

    view->save_status_lbl = instrument_label(presetPanel, "", 24, 396, &ac_font_12, COLOR_GREEN);

    lv_obj_add_event_cb(view->save_theme_btn, [](lv_event_t* e) {
        auto* v = static_cast<SystemScreenView*>(lv_event_get_user_data(e));
        if (!v) return;
        std::string err;
        bool ok = save_theme_config(theme_get_config(), default_theme_config_path(), &err);
        if (ok) {
            if (v->save_status_lbl) {
                lv_label_set_text(v->save_status_lbl, "THEME SAVED TO CONFIG");
                lv_obj_set_style_text_color(v->save_status_lbl, COLOR_GREEN, 0);
            }
        } else {
            if (v->save_status_lbl) {
                lv_label_set_text(v->save_status_lbl, "SAVE FAILED");
                lv_obj_set_style_text_color(v->save_status_lbl, COLOR_RED, 0);
            }
        }
    }, LV_EVENT_CLICKED, view);

    // Reset to preset action button
    auto* resetPresetBtn = instrument_button(presetPanel, "RESTORE SELECTED PRESET", 24, 424, 472, 40, nullptr);
    lv_obj_add_event_cb(resetPresetBtn, [](lv_event_t* e) {
        auto* v = static_cast<SystemScreenView*>(lv_event_get_user_data(e));
        if (v) {
            theme_reset_to_preset(theme_get_config().active_preset);
            if (v->save_status_lbl) {
                lv_label_set_text(v->save_status_lbl, "RESTORED PRESET");
                lv_obj_set_style_text_color(v->save_status_lbl, COLOR_CYAN, 0);
            }
        }
    }, LV_EVENT_CLICKED, view);

    // Reset All action with safe modal confirmation
    auto* resetAllBtn = instrument_button(presetPanel, "RESET TO FACTORY GRAPHITE", 24, 474, 472, 40, nullptr);
    lv_obj_add_event_cb(resetAllBtn, [](lv_event_t* e) {
        auto* v = static_cast<SystemScreenView*>(lv_event_get_user_data(e));
        if (!v) return;

        // Modal backdrop
        auto* backdrop = lv_obj_create(lv_screen_active());
        lv_obj_set_pos(backdrop, 0, 0);
        lv_obj_set_size(backdrop, 1920, 1080);
        lv_obj_set_style_bg_color(backdrop, lv_color_hex(0x000000), 0);
        lv_obj_set_style_bg_opa(backdrop, LV_OPA_70, 0);
        lv_obj_set_style_border_width(backdrop, 0, 0);

        auto* dlg = instrument_panel(backdrop, 660, 400, 600, 200, "CONFIRM RESTORE ALL DEFAULTS");
        instrument_label(dlg, "Reset all custom colors back to factory Graphite?", 24, 48, &ac_font_14, COLOR_TEXT);

        auto* cancelBtn = instrument_button(dlg, "CANCEL", 280, 120, 130, 44, nullptr);
        lv_obj_add_event_cb(cancelBtn, [](lv_event_t* ev) {
            auto* bd = static_cast<lv_obj_t*>(lv_event_get_user_data(ev));
            if (bd) lv_obj_delete(bd);
        }, LV_EVENT_CLICKED, backdrop);

        auto* confirmBtn = instrument_button(dlg, "CONFIRM RESET", 430, 120, 146, 44, nullptr);
        lv_obj_set_style_bg_color(confirmBtn, lv_color_hex(0x5A1B1B), 0);
        lv_obj_set_style_border_color(confirmBtn, COLOR_RED, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(confirmBtn, 0), lv_color_hex(0xFCA5A5), 0);

        struct ResetModalCtx { lv_obj_t* bd; SystemScreenView* view; };
        auto* rmc = new ResetModalCtx{backdrop, v};
        lv_obj_add_event_cb(confirmBtn, [](lv_event_t* ev) {
            auto* c = static_cast<ResetModalCtx*>(lv_event_get_user_data(ev));
            if (c) {
                theme_reset_all_defaults();
                if (c->view && c->view->save_status_lbl) {
                    lv_label_set_text(c->view->save_status_lbl, "RESET TO FACTORY");
                    lv_obj_set_style_text_color(c->view->save_status_lbl, COLOR_CYAN, 0);
                }
                if (c->bd) lv_obj_delete(c->bd);
            }
        }, LV_EVENT_CLICKED, rmc);
        lv_obj_add_event_cb(confirmBtn, [](lv_event_t* ev) {
            auto* c = static_cast<ResetModalCtx*>(lv_event_get_user_data(ev));
            delete c;
        }, LV_EVENT_DELETE, rmc);
    }, LV_EVENT_CLICKED, view);

    // -------------------------------------------------------------
    // THEME PANEL 2: SEMANTIC COLOR ROLES
    // -------------------------------------------------------------
    auto* rolePanel = instrument_panel(view->theme_container, 552, 8, 720, 866, "COLOR ROLES");

    for (int r = 0; r < 12; ++r) {
        auto role = static_cast<ThemeColorRole>(r);
        const int col_idx = r % 2;
        const int row_idx = r / 2;
        const int rx = 24 + col_idx * 342;
        const int ry = 48 + row_idx * 48;

        auto* rcard = lv_button_create(rolePanel);
        lv_obj_set_pos(rcard, rx, ry);
        lv_obj_set_size(rcard, 330, 42);
        lv_obj_set_style_radius(rcard, 2, 0);
        lv_obj_set_style_pad_all(rcard, 6, 0);
        view->role_cards[r] = rcard;

        // Color Swatch square
        auto* swatch = lv_obj_create(rcard);
        lv_obj_set_pos(swatch, 6, 5);
        lv_obj_set_size(swatch, 20, 20);
        lv_obj_set_style_radius(swatch, 2, 0);
        lv_obj_set_style_border_color(swatch, lv_color_hex(0x555555), 0);
        lv_obj_set_style_border_width(swatch, 1, 0);
        lv_obj_remove_flag(swatch, LV_OBJ_FLAG_CLICKABLE);
        view->role_swatches[r] = swatch;

        // Role Name
        auto* rname = lv_label_create(rcard);
        lv_label_set_text(rname, theme_role_name(role));
        lv_obj_set_pos(rname, 34, 4);
        lv_obj_set_style_text_font(rname, &ac_font_12, 0);
        lv_obj_set_style_text_color(rname, COLOR_TEXT, 0);

        // Hex Readout
        auto* rhex = lv_label_create(rcard);
        lv_label_set_text(rhex, format_hex_color(theme_get_color_hex(role)).c_str());
        lv_obj_set_pos(rhex, 250, 4);
        lv_obj_set_style_text_font(rhex, &ac_font_12, 0);
        lv_obj_set_style_text_color(rhex, COLOR_CYAN, 0);
        view->role_hex_labels[r] = rhex;

        struct RoleCardCtx { SystemScreenView* v; ThemeColorRole role; };
        auto* rc_ctx = new RoleCardCtx{view, role};
        lv_obj_add_event_cb(rcard, [](lv_event_t* e) {
            auto* c = static_cast<RoleCardCtx*>(lv_event_get_user_data(e));
            if (!c || !c->v) return;
            c->v->selected_role = c->role;
            sync_theme_editor_controls(c->v);
        }, LV_EVENT_CLICKED, rc_ctx);
        lv_obj_add_event_cb(rcard, [](lv_event_t* e) {
            auto* c = static_cast<RoleCardCtx*>(lv_event_get_user_data(e));
            delete c;
        }, LV_EVENT_DELETE, rc_ctx);
    }

    // Lower Interactive Color Adjuster section
    view->editing_role_title = instrument_label(rolePanel, "EDITING: SELECTED BORDER", 24, 350, &ac_font_14, COLOR_CYAN);

    // Large Active Swatch Box
    view->active_swatch = lv_obj_create(rolePanel);
    lv_obj_set_pos(view->active_swatch, 24, 380);
    lv_obj_set_size(view->active_swatch, 84, 58);
    lv_obj_set_style_radius(view->active_swatch, 2, 0);
    lv_obj_set_style_border_color(view->active_swatch, COLOR_SELECTED_BORDER, 0);
    lv_obj_set_style_border_width(view->active_swatch, 2, 0);
    lv_obj_remove_flag(view->active_swatch, LV_OBJ_FLAG_CLICKABLE);

    // HEX Direct Input & Validation Controls
    instrument_label(rolePanel, "HEX", 124, 380, &ac_font_12, COLOR_TEXT2);
    view->hex_textarea = lv_textarea_create(rolePanel);
    lv_obj_set_pos(view->hex_textarea, 124, 400);
    lv_obj_set_size(view->hex_textarea, 170, 38);
    lv_textarea_set_one_line(view->hex_textarea, true);
    lv_textarea_set_max_length(view->hex_textarea, 8);
    lv_obj_set_style_text_font(view->hex_textarea, &ac_font_14, 0);
    lv_obj_set_style_bg_color(view->hex_textarea, COLOR_RECESSED, 0);
    lv_obj_set_style_border_color(view->hex_textarea, COLOR_STANDARD_BORDER, 0);
    lv_obj_set_style_text_color(view->hex_textarea, COLOR_PRIMARY_TEXT, 0);

    view->hex_apply_btn = instrument_button(rolePanel, "APPLY", 306, 400, 96, 38, nullptr);
    view->hex_status_lbl = instrument_label(rolePanel, "", 414, 410, &ac_font_12, COLOR_TEXT2);

    lv_obj_add_event_cb(view->hex_apply_btn, [](lv_event_t* e) {
        auto* v = static_cast<SystemScreenView*>(lv_event_get_user_data(e));
        if (!v || !v->hex_textarea) return;
        const char* txt = lv_textarea_get_text(v->hex_textarea);
        uint32_t val = 0;
        if (parse_hex_color(txt, val)) {
            theme_set_custom_color(v->selected_role, val);
            if (v->save_status_lbl) lv_label_set_text(v->save_status_lbl, "");
            lv_label_set_text_fmt(v->hex_status_lbl, "APPLIED: #%06X", val & 0xFFFFFF);
            lv_obj_set_style_text_color(v->hex_status_lbl, COLOR_GREEN, 0);
        } else {
            lv_label_set_text(v->hex_status_lbl, "INVALID HEX");
            lv_obj_set_style_text_color(v->hex_status_lbl, COLOR_RED, 0);
        }
    }, LV_EVENT_CLICKED, view);

    // Red Slider
    view->val_r_lbl = instrument_label(rolePanel, "RED: 191", 24, 460, &ac_font_12, COLOR_TEXT);
    view->slider_r = lv_slider_create(rolePanel);
    lv_obj_set_pos(view->slider_r, 24, 482);
    lv_obj_set_size(view->slider_r, 660, 22);
    styleSlider(view->slider_r);
    lv_slider_set_range(view->slider_r, 0, 255);
    lv_obj_set_style_bg_color(view->slider_r, lv_color_hex(0xEF4444), LV_PART_INDICATOR);

    // Green Slider
    view->val_g_lbl = instrument_label(rolePanel, "GREEN: 194", 24, 518, &ac_font_12, COLOR_TEXT);
    view->slider_g = lv_slider_create(rolePanel);
    lv_obj_set_pos(view->slider_g, 24, 540);
    lv_obj_set_size(view->slider_g, 660, 22);
    styleSlider(view->slider_g);
    lv_slider_set_range(view->slider_g, 0, 255);
    lv_obj_set_style_bg_color(view->slider_g, lv_color_hex(0x22C55E), LV_PART_INDICATOR);

    // Blue Slider
    view->val_b_lbl = instrument_label(rolePanel, "BLUE: 186", 24, 576, &ac_font_12, COLOR_TEXT);
    view->slider_b = lv_slider_create(rolePanel);
    lv_obj_set_pos(view->slider_b, 24, 598);
    lv_obj_set_size(view->slider_b, 660, 22);
    styleSlider(view->slider_b);
    lv_slider_set_range(view->slider_b, 0, 255);
    lv_obj_set_style_bg_color(view->slider_b, lv_color_hex(0x3B82F6), LV_PART_INDICATOR);

    auto on_rgb_slider = [](lv_event_t* e) {
        auto* v = static_cast<SystemScreenView*>(lv_event_get_user_data(e));
        if (!v || !v->slider_r || !v->slider_g || !v->slider_b) return;
        int r = lv_slider_get_value(v->slider_r);
        int g = lv_slider_get_value(v->slider_g);
        int b = lv_slider_get_value(v->slider_b);
        uint32_t hex = ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
        theme_set_custom_color(v->selected_role, hex);
        if (v->save_status_lbl) lv_label_set_text(v->save_status_lbl, "");
    };

    lv_obj_add_event_cb(view->slider_r, on_rgb_slider, LV_EVENT_VALUE_CHANGED, view);
    lv_obj_add_event_cb(view->slider_g, on_rgb_slider, LV_EVENT_VALUE_CHANGED, view);
    lv_obj_add_event_cb(view->slider_b, on_rgb_slider, LV_EVENT_VALUE_CHANGED, view);

    // -------------------------------------------------------------
    // THEME PANEL 3: LIVE HARDWARE PREVIEW
    // -------------------------------------------------------------
    auto* previewPanel = instrument_panel(view->theme_container, 1288, 8, 616, 866, "HARDWARE PREVIEW");

    // 1. Faceplate & Button Preview Card
    auto* prevCard1 = lv_obj_create(previewPanel);
    lv_obj_set_pos(prevCard1, 24, 48);
    lv_obj_set_size(prevCard1, 568, 110);
    lv_obj_remove_flag(prevCard1, LV_OBJ_FLAG_SCROLLABLE);
    view->preview_panel = prevCard1;
    ui::rack_skin::apply_faceplate_panel(prevCard1, true);

    instrument_label(prevCard1, "PUSHBUTTONS", 16, 10, &ac_font_12, COLOR_TEXT2);

    view->preview_btn_norm = lv_button_create(prevCard1);
    lv_obj_set_pos(view->preview_btn_norm, 16, 38);
    lv_obj_set_size(view->preview_btn_norm, 160, 44);
    auto* bn_lbl = lv_label_create(view->preview_btn_norm);
    lv_label_set_text(bn_lbl, "NORMAL");
    lv_obj_center(bn_lbl);
    lv_obj_set_style_text_font(bn_lbl, &ac_font_12, 0);

    view->preview_btn_act = lv_button_create(prevCard1);
    lv_obj_set_pos(view->preview_btn_act, 192, 38);
    lv_obj_set_size(view->preview_btn_act, 160, 44);
    auto* ba_lbl = lv_label_create(view->preview_btn_act);
    lv_label_set_text(ba_lbl, "SELECTED");
    lv_obj_center(ba_lbl);
    lv_obj_set_style_text_font(ba_lbl, &ac_font_12, 0);

    view->preview_btn_mute = lv_button_create(prevCard1);
    lv_obj_set_pos(view->preview_btn_mute, 368, 38);
    lv_obj_set_size(view->preview_btn_mute, 160, 44);
    ui::rack_skin::apply_hardware_button(view->preview_btn_mute, false, COLOR_RED, lv_color_hex(0x5A1B1B), COLOR_RECESSED);
    auto* bm_lbl = lv_label_create(view->preview_btn_mute);
    lv_label_set_text(bm_lbl, "MUTE");
    lv_obj_center(bm_lbl);
    lv_obj_set_style_text_font(bm_lbl, &ac_font_12, 0);
    lv_obj_set_style_text_color(bm_lbl, lv_color_hex(0xFCA5A5), 0);

    // 2. Rotary Knob & LCD Display Preview
    auto* prevCard2 = lv_obj_create(previewPanel);
    lv_obj_set_pos(prevCard2, 24, 174);
    lv_obj_set_size(prevCard2, 568, 160);
    lv_obj_remove_flag(prevCard2, LV_OBJ_FLAG_SCROLLABLE);
    ui::rack_skin::apply_chassis_panel(prevCard2);

    instrument_label(prevCard2, "DISPLAY & CONTROLS", 16, 10, &ac_font_12, COLOR_TEXT2);

    view->preview_knob_group = instrument_knob(prevCard2, "DRIVE", &view->preview_knob_val, 0.0f, 1.0f, 0.65f, "%", 20, 36, 68);

    // Recessed LCD Preview Box
    view->preview_lcd_box = lv_obj_create(prevCard2);
    lv_obj_set_pos(view->preview_lcd_box, 130, 36);
    lv_obj_set_size(view->preview_lcd_box, 418, 106);
    lv_obj_remove_flag(view->preview_lcd_box, LV_OBJ_FLAG_SCROLLABLE);
    ui::rack_skin::apply_recessed_display(view->preview_lcd_box, COLOR_RECESSED, COLOR_SELECTED_BORDER);

    // LCD Post-draw dot-matrix overlay
    lv_obj_add_event_cb(view->preview_lcd_box, [](lv_event_t* e) {
        auto* layer = lv_event_get_layer(e);
        lv_area_t coords;
        lv_obj_get_coords(static_cast<lv_obj_t*>(lv_event_get_target(e)), &coords);
        ui::rack_skin::draw_lcd_dot_matrix_mask(layer, &coords);
    }, LV_EVENT_DRAW_POST, nullptr);

    view->preview_lcd_line1 = lv_label_create(view->preview_lcd_box);
    lv_label_set_text(view->preview_lcd_line1, "P01: CONCERT HALL");
    lv_obj_set_pos(view->preview_lcd_line1, 10, 8);
    lv_obj_set_style_text_font(view->preview_lcd_line1, &lv_font_unscii_16, 0);

    view->preview_lcd_line2 = lv_label_create(view->preview_lcd_box);
    lv_label_set_text(view->preview_lcd_line2, "RT60: 2.20 s   PRE-DLY: 20 ms   MIX: 35%");
    lv_obj_set_pos(view->preview_lcd_line2, 10, 48);
    lv_obj_set_style_text_font(view->preview_lcd_line2, &lv_font_unscii_8, 0);

    // 3. Typography & Technical Telemetry Preview Card
    auto* prevCard3 = lv_obj_create(previewPanel);
    lv_obj_set_pos(prevCard3, 24, 350);
    lv_obj_set_size(prevCard3, 568, 180);
    lv_obj_remove_flag(prevCard3, LV_OBJ_FLAG_SCROLLABLE);
    ui::rack_skin::apply_chassis_panel(prevCard3);

    instrument_label(prevCard3, "INDICATORS", 16, 10, &ac_font_12, COLOR_TEXT2);

    view->preview_txt_primary = instrument_label(prevCard3, "-12.4 dBFS  •  48.0 kHz", 16, 36, &ac_font_14, COLOR_PRIMARY_TEXT);
    view->preview_txt_secondary = instrument_label(prevCard3, "CHANNEL INSERT  •  AUX 1-4", 16, 62, &ac_font_12, COLOR_SECONDARY_TEXT);

    // Primary Accent Badge
    view->preview_accent1_pill = lv_button_create(prevCard3);
    lv_obj_set_pos(view->preview_accent1_pill, 16, 90);
    lv_obj_set_size(view->preview_accent1_pill, 160, 32);
    lv_obj_set_style_bg_color(view->preview_accent1_pill, COLOR_CONTROL, 0);
    lv_obj_set_style_border_width(view->preview_accent1_pill, 1, 0);
    auto* a1_lbl = lv_label_create(view->preview_accent1_pill);
    lv_label_set_text(a1_lbl, "ACCENT 1");
    lv_obj_center(a1_lbl);
    lv_obj_set_style_text_font(a1_lbl, &ac_font_12, 0);

    // Secondary Accent Badge
    view->preview_accent2_pill = lv_button_create(prevCard3);
    lv_obj_set_pos(view->preview_accent2_pill, 188, 90);
    lv_obj_set_size(view->preview_accent2_pill, 160, 32);
    lv_obj_set_style_bg_color(view->preview_accent2_pill, COLOR_CONTROL, 0);
    lv_obj_set_style_border_width(view->preview_accent2_pill, 1, 0);
    auto* a2_lbl = lv_label_create(view->preview_accent2_pill);
    lv_label_set_text(a2_lbl, "ACCENT 2");
    lv_obj_center(a2_lbl);
    lv_obj_set_style_text_font(a2_lbl, &ac_font_12, 0);

    // Preserved Functional Status Indicators Preview
    instrument_label(prevCard3, "● RUNNING", 16, 138, &ac_font_12, COLOR_GREEN);
    instrument_label(prevCard3, "● BYPASS", 188, 138, &ac_font_12, COLOR_ORANGE);
    instrument_label(prevCard3, "● CLIPPING", 370, 138, &ac_font_12, COLOR_RED);


    // =============================================================
    // 3. SYSTEM & NETWORK VIEW CONTAINER
    // =============================================================
    view->system_container = lv_obj_create(screen);
    lv_obj_set_pos(view->system_container, 0, 94);
    lv_obj_set_size(view->system_container, 1920, 920);
    lv_obj_remove_flag(view->system_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(view->system_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(view->system_container, 0, 0);
    lv_obj_set_style_pad_all(view->system_container, 0, 0);

    // SYSTEM POWER
    auto* power_panel = instrument_panel(view->system_container, 16, 8, 932, 440,
                                         "SYSTEM POWER / HOST ACTIONS");
    instrument_label(power_panel, "ALLOWLISTED SYSTEMD OPERATIONS", 24, 52, &ac_font_12, COLOR_TEXT2);
    view->sys_power_state = instrument_label(power_panel, "SYSTEMD POWER INTEGRATION: CHECKING...",
                                             24, 80, &ac_font_14, COLOR_CYAN);
    lv_obj_set_width(view->sys_power_state, 884);
    lv_label_set_long_mode(view->sys_power_state, LV_LABEL_LONG_WRAP);
    auto* power_note = instrument_label(power_panel,
        "Restart and shutdown stop the audio engine in an orderly manner, then send only a fixed systemctl request. No arbitrary command execution is exposed.",
        24, 124, &ac_font_12, COLOR_TEXT2);
    lv_obj_set_width(power_note, 884);
    lv_label_set_long_mode(power_note, LV_LABEL_LONG_WRAP);

    view->sys_restart_btn = make_system_button(power_panel, "RESTART HOST", 24, 230, 420, 64, COLOR_ORANGE);
    lv_obj_add_event_cb(view->sys_restart_btn, [](lv_event_t*) {
        request_system_power_action("restart");
    }, LV_EVENT_CLICKED, nullptr);
    view->sys_shutdown_btn = make_system_button(power_panel, "SHUT DOWN HOST", 488, 230, 420, 64, COLOR_RED);
    lv_obj_add_event_cb(view->sys_shutdown_btn, [](lv_event_t*) {
        request_system_power_action("shutdown");
    }, LV_EVENT_CLICKED, nullptr);

    auto* power_warning = instrument_label(power_panel,
        "Each action requires a separate confirmation. Real restart/shutdown is never triggered by opening the dialog.",
        24, 322, &ac_font_12, COLOR_SECONDARY_TEXT);
    lv_obj_set_width(power_warning, 884);
    lv_label_set_long_mode(power_warning, LV_LABEL_LONG_WRAP);

    // ACCESS & SECURITY
    auto* access_panel = instrument_panel(view->system_container, 956, 8, 932, 440,
                                          "ACCESS & SECURITY / MIXER LOCK");
    view->sys_security_state = instrument_label(access_panel, "PIN PROTECTION: CHECKING...",
                                                24, 52, &ac_font_16, COLOR_CYAN);
    view->sys_access_mode = instrument_label(access_panel, "LOCAL CONSOLE ACCESS",
                                             24, 82, &ac_font_12, COLOR_TEXT2);
    lv_obj_set_width(view->sys_access_mode, 884);

    view->sys_pin_btn = make_system_button(access_panel, "SET UP PIN", 24, 124, 420, 58,
                                           COLOR_SELECTED_BORDER);
    view->sys_pin_btn_label = system_button_label(view->sys_pin_btn);
    lv_obj_add_event_cb(view->sys_pin_btn, [](lv_event_t*) {
        auto& security = prismix::security::SecurityService::instance();
        const auto purpose = security.isPinConfigured()
            ? prismix::ui::PinPurpose::Change
            : prismix::ui::PinPurpose::Setup;
        prismix::ui::security_dialog_show_pin(purpose, [](bool success) {
            if (success && s_active_system_view) {
                refresh_system_access_controls(s_active_system_view);
                s_active_system_view->last_security_tick = 0;
            }
        });
    }, LV_EVENT_CLICKED, nullptr);

    view->sys_lock_btn = make_system_button(access_panel, "LOCK MIXER", 488, 124, 420, 58,
                                            COLOR_RED);
    lv_obj_add_event_cb(view->sys_lock_btn, [](lv_event_t*) {
        request_mixer_lock();
    }, LV_EVENT_CLICKED, nullptr);

    view->sys_lan_state = instrument_label(access_panel,
        "LAN ACCESS: CHECKING...", 24, 206, &ac_font_14, COLOR_TEXT2);
    view->sys_lan_btn = make_system_button(access_panel, "ENABLE LAN ACCESS", 24, 244, 420, 58,
                                           COLOR_YELLOW);
    view->sys_lan_btn_label = system_button_label(view->sys_lan_btn);
    lv_obj_add_event_cb(view->sys_lan_btn, [](lv_event_t*) {
        request_lan_access_toggle();
    }, LV_EVENT_CLICKED, nullptr);

    auto* access_note = instrument_label(access_panel,
        "One authoritative PIN service protects both the LVGL console and Web UI. PINs are stored only as a salted yescrypt hash. LAN Web access uses HTTP and has no transport encryption.",
        24, 318, &ac_font_12, COLOR_SECONDARY_TEXT);
    lv_obj_set_width(access_note, 884);
    lv_label_set_long_mode(access_note, LV_LABEL_LONG_WRAP);

    // NETWORK
    auto* network_panel = instrument_panel(view->system_container, 16, 456, 932, 440,
                                           "NETWORK / NETWORKMANAGER");
    view->sys_network_nm = instrument_label(network_panel, "NETWORKMANAGER: CHECKING...",
                                            24, 52, &ac_font_14, COLOR_CYAN);
    view->sys_network_connection = instrument_label(network_panel, "CONNECTION: -",
                                                    24, 82, &ac_font_12, COLOR_TEXT);
    lv_obj_set_width(view->sys_network_connection, 884);
    view->sys_network_ssid = instrument_label(network_panel, "WI-FI SSID: -",
                                              24, 110, &ac_font_12, COLOR_TEXT);
    view->sys_network_signal = instrument_label(network_panel, "WI-FI SIGNAL: -",
                                                24, 138, &ac_font_12, COLOR_TEXT);
    view->sys_network_ipv4 = instrument_label(network_panel, "IPV4: -",
                                              24, 166, &ac_font_12, COLOR_TEXT);
    view->sys_network_device = instrument_label(network_panel, "INTERFACE: -",
                                                24, 194, &ac_font_12, COLOR_TEXT);
    lv_obj_set_width(view->sys_network_device, 884);

    view->sys_network_note = instrument_label(network_panel,
        "Querying live NetworkManager state...", 24, 230, &ac_font_12, COLOR_SECONDARY_TEXT);
    lv_obj_set_width(view->sys_network_note, 884);
    lv_label_set_long_mode(view->sys_network_note, LV_LABEL_LONG_WRAP);

    view->sys_scan_btn = make_system_button(network_panel, "SCAN WI-FI", 24, 330, 420, 58,
                                            COLOR_SELECTED_BORDER);
    lv_obj_add_event_cb(view->sys_scan_btn, [](lv_event_t*) {
        show_wifi_scan_modal();
    }, LV_EVENT_CLICKED, nullptr);

    auto* network_refresh = make_system_button(network_panel, "REFRESH NETWORK", 488, 330, 420, 58,
                                               COLOR_STANDARD_BORDER);
    lv_obj_add_event_cb(network_refresh, [](lv_event_t*) {
        if (s_active_system_view) {
            refresh_system_network_controls(s_active_system_view, true);
            refresh_system_web_controls(s_active_system_view);
        }
    }, LV_EVENT_CLICKED, nullptr);

    // WEB UI ACCESS
    auto* web_panel = instrument_panel(view->system_container, 956, 456, 932, 440,
                                       "WEB UI ACCESS / RUNTIME ADDRESS");
    instrument_label(web_panel, "ACTUAL LISTENER URL", 24, 52, &ac_font_12, COLOR_TEXT2);
    view->sys_web_url = instrument_label(web_panel, "http://127.0.0.1:8888/",
                                         24, 82, &ac_font_28, COLOR_CYAN);
    lv_obj_set_width(view->sys_web_url, 884);
    lv_label_set_long_mode(view->sys_web_url, LV_LABEL_LONG_DOT);

    view->sys_web_mode = instrument_label(web_panel, "LOCALHOST ONLY",
                                          24, 130, &ac_font_16, COLOR_SECONDARY_TEXT);
    view->sys_web_bind = instrument_label(web_panel, "BIND 127.0.0.1:8888  /  SCHEME http  /  TRANSPORT HTTP - NO TLS",
                                          24, 164, &ac_font_12, COLOR_TEXT);
    lv_obj_set_width(view->sys_web_bind, 884);
    view->sys_web_note = instrument_label(web_panel,
        "HTTP provides no transport encryption. Enable LAN access only on a trusted network and share the address and PIN only with authorized operators.",
        24, 204, &ac_font_12, COLOR_SECONDARY_TEXT);
    lv_obj_set_width(view->sys_web_note, 884);
    lv_label_set_long_mode(view->sys_web_note, LV_LABEL_LONG_WRAP);

    auto* web_hint = instrument_label(web_panel,
        "A QR code for this exact runtime URL is generated in the Web UI Access panel.",
        24, 322, &ac_font_12, COLOR_SECONDARY_TEXT);
    lv_obj_set_width(web_hint, 884);
    lv_label_set_long_mode(web_hint, LV_LABEL_LONG_WRAP);

    refresh_system_access_controls(view);
    refresh_system_power_controls(view);
    refresh_system_network_controls(view, true);
    refresh_system_web_controls(view);

    // Initial sync
    sync_theme_editor_controls(view);
    update_system_mode_view(view);

    // Mode Toggle Button Events
    lv_obj_add_event_cb(view->mode_audio_btn, [](lv_event_t* e) {
        auto* v = static_cast<SystemScreenView*>(lv_event_get_user_data(e));
        if (v) {
            s_system_mode = 0;
            v->active_mode = 0;
            update_system_mode_view(v);
        }
    }, LV_EVENT_CLICKED, view);

    lv_obj_add_event_cb(view->mode_theme_btn, [](lv_event_t* e) {
        auto* v = static_cast<SystemScreenView*>(lv_event_get_user_data(e));
        if (v) {
            s_system_mode = 1;
            v->active_mode = 1;
            update_system_mode_view(v);
        }
    }, LV_EVENT_CLICKED, view);

    lv_obj_add_event_cb(view->mode_system_btn, [](lv_event_t* e) {
        auto* v = static_cast<SystemScreenView*>(lv_event_get_user_data(e));
        if (v) {
            s_system_mode = 2;
            v->active_mode = 2;
            v->last_security_tick = 0;
            v->last_power_tick = 0;
            v->last_network_tick = 0;
            v->last_web_tick = 0;
            update_system_mode_view(v);
            refresh_system_access_controls(v);
            refresh_system_power_controls(v);
            refresh_system_network_controls(v, true);
            refresh_system_web_controls(v);
        }
    }, LV_EVENT_CLICKED, view);

    view->timer = lv_timer_create(updateScreen, 100, view);

    lv_obj_add_event_cb(screen, [](lv_event_t* event) {
        auto* v = static_cast<SystemScreenView*>(lv_event_get_user_data(event));
        if (v) {
            if (v->timer) lv_timer_delete(v->timer);
            if (s_active_system_view == v) s_active_system_view = nullptr;
            delete v;
        }
    }, LV_EVENT_DELETE, view);

    return screen;
}
