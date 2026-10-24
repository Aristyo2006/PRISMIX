#include "audio_setup_dialog.h"
#include "../ui_audio_bridge.h"
#include "../theme/theme.h"
#include "../assets/rack_textures.h"
#include "../../service/audio_service.h"
#include <vector>
#include <string>
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace {

static lv_obj_t* g_setup_dialog_backdrop = nullptr;

struct AudioSetupDialogContext {
    lv_obj_t* backdrop = nullptr;
    lv_obj_t* dialog = nullptr;
    lv_obj_t* capture_dropdown = nullptr;
    lv_obj_t* render_dropdown = nullptr;
    lv_obj_t* sample_rate_dropdown = nullptr;
    lv_obj_t* buffer_size_dropdown = nullptr;
    lv_obj_t* latency_label = nullptr;
    lv_obj_t* status_label = nullptr;

    std::vector<AudioDeviceChoice> capture_devices;
    std::vector<AudioDeviceChoice> render_devices;

    std::string selected_capture_id;
    std::string selected_render_id;
    uint32_t selected_sample_rate = 48000;
    uint32_t selected_buffer_frames = 128;

    bool closed = false;
};

const uint32_t kSampleRates[] = {44100, 48000, 96000};
const uint32_t kBufferSizes[] = {64, 128, 256, 512, 1024};

void update_latency_label(AudioSetupDialogContext* ctx) {
    if (!ctx || !ctx->latency_label) return;
    const uint32_t rate = ctx->selected_sample_rate > 0 ? ctx->selected_sample_rate : 48000;
    const uint32_t frames = ctx->selected_buffer_frames > 0 ? ctx->selected_buffer_frames : 128;
    const float latencyMs = (static_cast<float>(frames) * 1000.0f) / static_cast<float>(rate);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "Calculated Period Latency: %.2f ms (%u frames @ %.1f kHz)",
                  latencyMs, frames, static_cast<float>(rate) / 1000.0f);
    lv_label_set_text(ctx->latency_label, buf);
}

void style_dropdown(lv_obj_t* dd) {
    lv_obj_set_style_bg_color(dd, COLOR_CONTROL, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dd, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(dd, COLOR_EDGE, LV_PART_MAIN);
    lv_obj_set_style_border_width(dd, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(dd, 2, LV_PART_MAIN);
    lv_obj_set_style_text_color(dd, COLOR_TEXT, LV_PART_MAIN);
    lv_obj_set_style_text_font(dd, &ac_font_12, LV_PART_MAIN);

    lv_obj_t* list = lv_dropdown_get_list(dd);
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

    // When dropdown opens, ensure its popup list is reparented to lv_layer_top()
    // so it sits above the modal dialog and backdrop without transparency artifacts!
    lv_obj_add_event_cb(dd, [](lv_event_t* e) {
        auto* d = static_cast<lv_obj_t*>(lv_event_get_target(e));
        auto* l = lv_dropdown_get_list(d);
        if (l) {
            lv_obj_set_parent(l, lv_layer_top());
            lv_obj_move_to_index(l, -1);
            lv_obj_set_style_bg_color(l, lv_color_hex(0x181C20), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(l, LV_OPA_COVER, LV_PART_MAIN);
            lv_obj_set_style_border_color(l, COLOR_EDGE, LV_PART_MAIN);
            lv_obj_set_style_border_width(l, 1, LV_PART_MAIN);
            lv_obj_set_style_text_color(l, COLOR_TEXT, LV_PART_MAIN);
            lv_obj_set_style_text_font(l, &ac_font_12, LV_PART_MAIN);
        }
    }, LV_EVENT_READY, nullptr);
}

void populate_devices(AudioSetupDialogContext* ctx) {
    auto& service = audio_service();
    service.refreshDevices();
    const auto snapshot = service.snapshot();

    ctx->capture_devices = snapshot.captureDevices;
    ctx->render_devices = snapshot.renderDevices;

    if (ctx->selected_capture_id.empty() && !snapshot.captureDeviceId.empty()) {
        ctx->selected_capture_id = snapshot.captureDeviceId;
    }
    if (ctx->selected_render_id.empty() && !snapshot.renderDeviceId.empty()) {
        ctx->selected_render_id = snapshot.renderDeviceId;
    }

    // Capture dropdown options
    std::string capOptions = "[DEFAULT] System Default Endpoint";
    uint32_t selectedCapIndex = 0;
    for (size_t i = 0; i < ctx->capture_devices.size(); ++i) {
        const auto& dev = ctx->capture_devices[i];
        capOptions += "\n";
        capOptions += dev.name.empty() ? dev.id : dev.name;
        capOptions += "  [" + dev.id + "] (" + std::to_string(dev.channels) + " in)";
        if (dev.id == ctx->selected_capture_id) {
            selectedCapIndex = static_cast<uint32_t>(i + 1);
        }
    }
    lv_dropdown_set_options(ctx->capture_dropdown, capOptions.c_str());
    lv_dropdown_set_selected(ctx->capture_dropdown, selectedCapIndex);

    // Render dropdown options
    std::string renOptions = "[DEFAULT] System Default Endpoint";
    uint32_t selectedRenIndex = 0;
    for (size_t i = 0; i < ctx->render_devices.size(); ++i) {
        const auto& dev = ctx->render_devices[i];
        renOptions += "\n";
        renOptions += dev.name.empty() ? dev.id : dev.name;
        renOptions += "  [" + dev.id + "] (" + std::to_string(dev.channels) + " out)";
        if (dev.id == ctx->selected_render_id) {
            selectedRenIndex = static_cast<uint32_t>(i + 1);
        }
    }
    lv_dropdown_set_options(ctx->render_dropdown, renOptions.c_str());
    lv_dropdown_set_selected(ctx->render_dropdown, selectedRenIndex);
}

void apply_settings(AudioSetupDialogContext* ctx) {
    auto& setup = ui_get_persisted_audio_setup();
    setup.captureDeviceId = ctx->selected_capture_id;
    setup.renderDeviceId = ctx->selected_render_id;
    setup.requestedSampleRate = ctx->selected_sample_rate;
    setup.requestedBufferFrames = ctx->selected_buffer_frames;

    std::string saveErr;
    ui_save_persisted_audio_setup(&saveErr);

    auto& service = audio_service();
    service.selectCaptureDevice(ctx->selected_capture_id);
    service.selectRenderDevice(ctx->selected_render_id);
    service.setRequestedAudioConfig(ctx->selected_sample_rate, ctx->selected_buffer_frames);
}

} // anonymous namespace

bool is_audio_setup_dialog_open() {
    return g_setup_dialog_backdrop != nullptr;
}

void dismiss_audio_setup_dialog() {
    if (g_setup_dialog_backdrop) {
        lv_obj_delete(g_setup_dialog_backdrop);
        g_setup_dialog_backdrop = nullptr;
    }
}

void show_audio_setup_dialog() {
    if (g_setup_dialog_backdrop) return; // Already open

    auto* ctx = new AudioSetupDialogContext{};
    const auto& setup = ui_get_persisted_audio_setup();
    ctx->selected_capture_id = setup.captureDeviceId;
    ctx->selected_render_id = setup.renderDeviceId;
    ctx->selected_sample_rate = setup.requestedSampleRate > 0 ? setup.requestedSampleRate : 48000;
    ctx->selected_buffer_frames = setup.requestedBufferFrames > 0 ? setup.requestedBufferFrames : 128;

    // 1. Semi-transparent backdrop covering entire display
    auto* backdrop = lv_obj_create(lv_layer_top());
    g_setup_dialog_backdrop = backdrop;
    ctx->backdrop = backdrop;

    lv_obj_set_size(backdrop, 1920, 1080);
    lv_obj_set_pos(backdrop, 0, 0);
    lv_obj_set_style_bg_color(backdrop, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_70, 0);
    lv_obj_set_style_border_width(backdrop, 0, 0);
    lv_obj_set_style_radius(backdrop, 0, 0);
    lv_obj_remove_flag(backdrop, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_event_cb(backdrop, [](lv_event_t* e) {
        g_setup_dialog_backdrop = nullptr;
        auto* c = static_cast<AudioSetupDialogContext*>(lv_event_get_user_data(e));
        delete c;
    }, LV_EVENT_DELETE, ctx);

    // 2. Hardware dialog frame centered
    auto* dialog = lv_obj_create(backdrop);
    ctx->dialog = dialog;
    lv_obj_set_size(dialog, 760, 560);
    lv_obj_align(dialog, LV_ALIGN_CENTER, 0, 0);
    ui::rack_skin::apply_chassis_panel(dialog, COLOR_ACCENT);
    ui::rack_skin::add_faceplate_corner_screws(dialog, 760, 560, 6);
    lv_obj_set_style_pad_all(dialog, 24, 0);
    lv_obj_remove_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);

    // Header
    auto* title = lv_label_create(dialog);
    lv_label_set_text(title, "PRISMIX  /  HARDWARE AUDIO SETUP");
    lv_obj_set_pos(title, 0, 0);
    lv_obj_set_style_text_font(title, &ac_font_16, 0);
    lv_obj_set_style_text_color(title, COLOR_TEXT, 0);

    auto* subtitle = lv_label_create(dialog);
    lv_label_set_text(subtitle, "Select hardware audio interface, sample rate, and buffer period latency.");
    lv_obj_set_pos(subtitle, 0, 26);
    lv_obj_set_style_text_font(subtitle, &ac_font_12, 0);
    lv_obj_set_style_text_color(subtitle, COLOR_TEXT2, 0);

    // Divider
    auto* divider = lv_obj_create(dialog);
    lv_obj_set_pos(divider, 0, 48);
    lv_obj_set_size(divider, 712, 1);
    lv_obj_set_style_bg_color(divider, COLOR_EDGE, 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(divider, 0, 0);

    // Capture device
    auto* cap_label = lv_label_create(dialog);
    lv_label_set_text(cap_label, "CAPTURE DEVICE (INPUTS)");
    lv_obj_set_pos(cap_label, 0, 60);
    lv_obj_set_style_text_font(cap_label, &ac_font_12, 0);
    lv_obj_set_style_text_color(cap_label, COLOR_TEXT2, 0);

    ctx->capture_dropdown = lv_dropdown_create(dialog);
    lv_obj_set_pos(ctx->capture_dropdown, 0, 80);
    lv_obj_set_size(ctx->capture_dropdown, 712, 38);
    style_dropdown(ctx->capture_dropdown);
    lv_obj_add_event_cb(ctx->capture_dropdown, [](lv_event_t* e) {
        auto* c = static_cast<AudioSetupDialogContext*>(lv_event_get_user_data(e));
        auto* dd = static_cast<lv_obj_t*>(lv_event_get_target(e));
        const uint32_t idx = lv_dropdown_get_selected(dd);
        if (idx == 0 || idx > c->capture_devices.size()) {
            c->selected_capture_id = "";
        } else {
            c->selected_capture_id = c->capture_devices[idx - 1].id;
        }
    }, LV_EVENT_VALUE_CHANGED, ctx);

    // Render device
    auto* ren_label = lv_label_create(dialog);
    lv_label_set_text(ren_label, "PLAYBACK DEVICE (OUTPUTS)");
    lv_obj_set_pos(ren_label, 0, 130);
    lv_obj_set_style_text_font(ren_label, &ac_font_12, 0);
    lv_obj_set_style_text_color(ren_label, COLOR_TEXT2, 0);

    ctx->render_dropdown = lv_dropdown_create(dialog);
    lv_obj_set_pos(ctx->render_dropdown, 0, 150);
    lv_obj_set_size(ctx->render_dropdown, 712, 38);
    style_dropdown(ctx->render_dropdown);
    lv_obj_add_event_cb(ctx->render_dropdown, [](lv_event_t* e) {
        auto* c = static_cast<AudioSetupDialogContext*>(lv_event_get_user_data(e));
        auto* dd = static_cast<lv_obj_t*>(lv_event_get_target(e));
        const uint32_t idx = lv_dropdown_get_selected(dd);
        if (idx == 0 || idx > c->render_devices.size()) {
            c->selected_render_id = "";
        } else {
            c->selected_render_id = c->render_devices[idx - 1].id;
        }
    }, LV_EVENT_VALUE_CHANGED, ctx);

    // Sample Rate
    auto* sr_label = lv_label_create(dialog);
    lv_label_set_text(sr_label, "SAMPLE RATE");
    lv_obj_set_pos(sr_label, 0, 202);
    lv_obj_set_style_text_font(sr_label, &ac_font_12, 0);
    lv_obj_set_style_text_color(sr_label, COLOR_TEXT2, 0);

    ctx->sample_rate_dropdown = lv_dropdown_create(dialog);
    lv_obj_set_pos(ctx->sample_rate_dropdown, 0, 222);
    lv_obj_set_size(ctx->sample_rate_dropdown, 344, 38);
    style_dropdown(ctx->sample_rate_dropdown);
    lv_dropdown_set_options(ctx->sample_rate_dropdown, "44,100 Hz\n48,000 Hz (Standard)\n96,000 Hz (High-Res)");
    uint32_t srIdx = 1; // 48k default
    for (uint32_t i = 0; i < 3; ++i) {
        if (kSampleRates[i] == ctx->selected_sample_rate) srIdx = i;
    }
    lv_dropdown_set_selected(ctx->sample_rate_dropdown, srIdx);
    lv_obj_add_event_cb(ctx->sample_rate_dropdown, [](lv_event_t* e) {
        auto* c = static_cast<AudioSetupDialogContext*>(lv_event_get_user_data(e));
        auto* dd = static_cast<lv_obj_t*>(lv_event_get_target(e));
        const uint32_t idx = lv_dropdown_get_selected(dd);
        if (idx < 3) c->selected_sample_rate = kSampleRates[idx];
        update_latency_label(c);
    }, LV_EVENT_VALUE_CHANGED, ctx);

    // Buffer Size
    auto* bs_label = lv_label_create(dialog);
    lv_label_set_text(bs_label, "BUFFER SIZE (FRAMES)");
    lv_obj_set_pos(bs_label, 368, 202);
    lv_obj_set_style_text_font(bs_label, &ac_font_12, 0);
    lv_obj_set_style_text_color(bs_label, COLOR_TEXT2, 0);

    ctx->buffer_size_dropdown = lv_dropdown_create(dialog);
    lv_obj_set_pos(ctx->buffer_size_dropdown, 368, 222);
    lv_obj_set_size(ctx->buffer_size_dropdown, 344, 38);
    style_dropdown(ctx->buffer_size_dropdown);
    lv_dropdown_set_options(ctx->buffer_size_dropdown,
        "64 frames\n128 frames (Default)\n256 frames\n512 frames\n1024 frames");
    uint32_t bsIdx = 1; // 128 frames default
    for (uint32_t i = 0; i < 5; ++i) {
        if (kBufferSizes[i] == ctx->selected_buffer_frames) bsIdx = i;
    }
    lv_dropdown_set_selected(ctx->buffer_size_dropdown, bsIdx);
    lv_obj_add_event_cb(ctx->buffer_size_dropdown, [](lv_event_t* e) {
        auto* c = static_cast<AudioSetupDialogContext*>(lv_event_get_user_data(e));
        auto* dd = static_cast<lv_obj_t*>(lv_event_get_target(e));
        const uint32_t idx = lv_dropdown_get_selected(dd);
        if (idx < 5) c->selected_buffer_frames = kBufferSizes[idx];
        update_latency_label(c);
    }, LV_EVENT_VALUE_CHANGED, ctx);

    // Calculated Latency Banner
    auto* lat_box = lv_obj_create(dialog);
    lv_obj_set_pos(lat_box, 0, 276);
    lv_obj_set_size(lat_box, 712, 42);
    lv_obj_set_style_bg_color(lat_box, lv_color_hex(0x101418), 0);
    lv_obj_set_style_bg_opa(lat_box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(lat_box, COLOR_EDGE, 0);
    lv_obj_set_style_border_width(lat_box, 1, 0);
    lv_obj_set_style_radius(lat_box, 2, 0);
    lv_obj_remove_flag(lat_box, LV_OBJ_FLAG_SCROLLABLE);

    ctx->latency_label = lv_label_create(lat_box);
    lv_obj_align(ctx->latency_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(ctx->latency_label, &ac_font_12, 0);
    lv_obj_set_style_text_color(ctx->latency_label, COLOR_SILVER, 0);
    update_latency_label(ctx);

    // Diagnostics / Status Label
    ctx->status_label = lv_label_create(dialog);
    lv_obj_set_pos(ctx->status_label, 0, 332);
    lv_obj_set_width(ctx->status_label, 712);
    lv_label_set_long_mode(ctx->status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(ctx->status_label, &ac_font_12, 0);

    const auto snapshot = audio_service().snapshot();
    if (snapshot.state == AudioServiceState::Streaming) {
        lv_label_set_text_fmt(ctx->status_label, "● Real Audio is active: %.0f Hz, %u frames (%s)",
                              snapshot.actualSampleRate, snapshot.actualBufferFrames,
                              snapshot.actualCaptureDevice.empty() ? "JACK" : snapshot.actualCaptureDevice.c_str());
        lv_obj_set_style_text_color(ctx->status_label, COLOR_GREEN, 0);
    } else if (snapshot.state == AudioServiceState::Error) {
        lv_label_set_text_fmt(ctx->status_label, "⚠ Engine Error: %s",
                              snapshot.error.empty() ? "Unknown backend error" : snapshot.error.c_str());
        lv_obj_set_style_text_color(ctx->status_label, COLOR_RED, 0);
    } else {
        lv_label_set_text(ctx->status_label, "● Real Audio stopped. Configure parameters and click Apply or Start Audio.");
        lv_obj_set_style_text_color(ctx->status_label, COLOR_TEXT2, 0);
    }

    // Populate device dropdown options
    populate_devices(ctx);

    // Bottom Action Buttons
    // 1. Cancel
    auto* cancel_btn = lv_button_create(dialog);
    lv_obj_set_pos(cancel_btn, 0, 460);
    lv_obj_set_size(cancel_btn, 130, 44);
    ui::rack_skin::apply_hardware_button(cancel_btn, false, COLOR_EDGE, COLOR_CONTROL, lv_color_hex(0x181D23));
    auto* cancel_lbl = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_lbl, "CANCEL");
    lv_obj_center(cancel_lbl);
    lv_obj_set_style_text_font(cancel_lbl, &ac_font_14, 0);
    lv_obj_set_style_text_color(cancel_lbl, COLOR_TEXT2, 0);
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t*) {
        dismiss_audio_setup_dialog();
    }, LV_EVENT_CLICKED, nullptr);

    // 2. Apply
    auto* apply_btn = lv_button_create(dialog);
    lv_obj_set_pos(apply_btn, 310, 460);
    lv_obj_set_size(apply_btn, 170, 44);
    ui::rack_skin::apply_hardware_button(apply_btn, false, COLOR_ACCENT, lv_color_hex(0x223542), COLOR_CONTROL);
    auto* apply_lbl = lv_label_create(apply_btn);
    lv_label_set_text(apply_lbl, "APPLY SETTINGS");
    lv_obj_center(apply_lbl);
    lv_obj_set_style_text_font(apply_lbl, &ac_font_14, 0);
    lv_obj_set_style_text_color(apply_lbl, COLOR_SILVER, 0);
    lv_obj_add_event_cb(apply_btn, [](lv_event_t* e) {
        auto* c = static_cast<AudioSetupDialogContext*>(lv_event_get_user_data(e));
        apply_settings(c);
        dismiss_audio_setup_dialog();
    }, LV_EVENT_CLICKED, ctx);

    // 3. Apply & Start Audio
    auto* start_btn = lv_button_create(dialog);
    lv_obj_set_pos(start_btn, 492, 460);
    lv_obj_set_size(start_btn, 220, 44);
    ui::rack_skin::apply_hardware_button(start_btn, false, COLOR_GREEN, lv_color_hex(0x133824), lv_color_hex(0x0E2417));
    auto* start_lbl = lv_label_create(start_btn);
    lv_label_set_text(start_lbl, "▶ APPLY & START");
    lv_obj_center(start_lbl);
    lv_obj_set_style_text_font(start_lbl, &ac_font_14, 0);
    lv_obj_set_style_text_color(start_lbl, COLOR_GREEN, 0);
    lv_obj_add_event_cb(start_btn, [](lv_event_t* e) {
        auto* c = static_cast<AudioSetupDialogContext*>(lv_event_get_user_data(e));
        apply_settings(c);
        std::string err;
        if (!ui_start_real_audio(&err)) {
            if (c->status_label) {
                lv_label_set_text_fmt(c->status_label, "⚠ Start Failed: %s",
                                      err.empty() ? "Check JACK server and hardware" : err.c_str());
                lv_obj_set_style_text_color(c->status_label, COLOR_RED, 0);
            }
        } else {
            dismiss_audio_setup_dialog();
        }
    }, LV_EVENT_CLICKED, ctx);
}
