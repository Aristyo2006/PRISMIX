#include "channel_screen.h"
#include "../theme/theme.h"
#include "../widgets/instrument.h"
#include "../assets/rack_textures.h"
#include "../ui.h"
#include "../../model/mixer_state.h"
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace {

struct ChannelMasterLiveCtx {
    ChannelState* ch = nullptr;
    lv_timer_t* timer = nullptr;
    lv_obj_t* comp_gr_label = nullptr;
    lv_obj_t* lim_gr_label = nullptr;
    lv_obj_t* delay_l_dist_label = nullptr;
    lv_obj_t* delay_r_dist_label = nullptr;
    lv_obj_t* delay_knob_r = nullptr;
    float last_delay_l = -1.0f;
    float last_delay_r = -1.0f;
    bool last_delay_link = false;
};

void channel_master_timer_cb(lv_timer_t* t) {
    auto* ctx = static_cast<ChannelMasterLiveCtx*>(lv_timer_get_user_data(t));
    if (!ctx || !ctx->ch) return;
    auto& ch = *ctx->ch;

    // 1. Enforce Stereo Link on Speaker Delay if active
    if (ch.speaker_delay_link) {
        if (ch.speaker_delay_l_ms != ctx->last_delay_l) {
            ch.speaker_delay_r_ms = ch.speaker_delay_l_ms;
            if (ctx->delay_knob_r) {
                instrument_knob_set_value(ctx->delay_knob_r, ch.speaker_delay_r_ms);
            }
        }
    }
    ctx->last_delay_l = ch.speaker_delay_l_ms;
    ctx->last_delay_r = ch.speaker_delay_r_ms;
    ctx->last_delay_link = ch.speaker_delay_link;

    // 2. Update Delay Distance (Compact: meters & feet)
    constexpr float kSpeedOfSoundMps = 343.0f;
    constexpr float kMetersToFeet = 3.28084f;

    if (ctx->delay_l_dist_label) {
        const float distM_L = (ch.speaker_delay_l_ms * 0.001f) * kSpeedOfSoundMps;
        const float distFt_L = distM_L * kMetersToFeet;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "LEFT:   %5.2f ms  (%.2f m / %.1f ft)",
                      ch.speaker_delay_l_ms, distM_L, distFt_L);
        lv_label_set_text(ctx->delay_l_dist_label, buf);
    }
    if (ctx->delay_r_dist_label) {
        const float distM_R = (ch.speaker_delay_r_ms * 0.001f) * kSpeedOfSoundMps;
        const float distFt_R = distM_R * kMetersToFeet;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "RIGHT:  %5.2f ms  (%.2f m / %.1f ft)",
                      ch.speaker_delay_r_ms, distM_R, distFt_R);
        lv_label_set_text(ctx->delay_r_dist_label, buf);
    }

    // 3. Update Master Bus Dynamics Telemetry
    if (ctx->comp_gr_label) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "COMP GR: %+.1f dB", ch.comp_gr);
        lv_label_set_text(ctx->comp_gr_label, buf);
        lv_obj_set_style_text_color(ctx->comp_gr_label,
            ch.comp_gr < -0.1f ? COLOR_ORANGE : COLOR_TEXT2, 0);
    }
    if (ctx->lim_gr_label) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "LIMITER GR: %+.1f dB", ch.limiter_gr_db);
        lv_label_set_text(ctx->lim_gr_label, buf);
        lv_obj_set_style_text_color(ctx->lim_gr_label,
            ch.limiter_gr_db < -0.1f ? COLOR_RED : COLOR_TEXT2, 0);
    }
}

// Build dedicated hardware detail panel for Main L/R master bus
lv_obj_t* create_master_channel_view(lv_obj_t* s, ChannelState& ch) {
    auto* ctx = new ChannelMasterLiveCtx{};
    ctx->ch = &ch;

    // LEFT PANEL: Master Bus Identity, Level/Bal, Output Meters, Routing Status
    auto* in = instrument_panel(s, 16, 56, 320, 864, "MASTER");
    ui::rack_skin::add_faceplate_corner_screws(in, 320, 864, 6);

    instrument_label(in, ch.name, 16, 38, &ac_font_28, COLOR_CYAN);
    instrument_label(in, "STEREO BUS", 16, 72, &ac_font_12, COLOR_TEXT2);

    // Master Gain & Balance Knobs (Cleanly separated from meter header)
    instrument_knob(in, "MASTER GAIN", &ch.gain, 0.0f, 1.0f, 0.5f, "dB", 32, 98, 72);
    instrument_knob(in, "BALANCE", &ch.pan, 0.0f, 1.0f, 0.5f, "L/C/R", 164, 98, 72);

    // Stereo Output Meters
    instrument_label(in, "OUTPUT LEVEL (L / R)", 24, 230, &ac_font_12, COLOR_TEXT2);
    instrument_meter(in, &ch.peak_l, &ch.clip, 60, 256, 46, 567, &ch.peak_hold_l, &ch.rms_l);
    instrument_meter(in, &ch.peak_r, &ch.clip, 130, 256, 46, 567, &ch.peak_hold_r, &ch.rms_r);

    // Calibrated dBFS Silkscreen Scale (height 567 px -> 9 px / dB)
    static const struct { int y_offset; const char* txt; } kDbScale[] = {
        { 0, "+3" }, { 27, " 0" }, { 81, "-6" }, { 135, "-12" },
        { 189, "-18" }, { 243, "-24" }, { 351, "-36" }, { 459, "-48" }, { 567, "-60" }
    };
    for (const auto& item : kDbScale) {
        instrument_label(in, item.txt, 184, 256 + item.y_offset, &ac_font_12,
                         item.y_offset <= 27 ? COLOR_ORANGE : COLOR_TEXT2);
    }

    // RIGHT PANEL: Master Signal Architecture & Dedicated Processing Panels
    auto* path = instrument_panel(s, 352, 56, 1544, 864, "MASTER");
    ui::rack_skin::add_faceplate_corner_screws(path, 1544, 864, 6);

    // =========================================================================
    // COMPRESSOR
    // =========================================================================
    auto* comp_panel = instrument_panel(path, 16, 44, 744, 384, "COMPRESSOR");
    ui::rack_skin::add_faceplate_corner_screws(comp_panel, 744, 384, 4);

    instrument_knob(comp_panel, "THRESH", &ch.comp_thresh, -60.0f, 0.0f, -18.0f, "dB", 24, 66, 76);
    instrument_knob(comp_panel, "RATIO", &ch.comp_ratio, 1.0f, 20.0f, 4.0f, ":1", 140, 66, 76);
    instrument_knob(comp_panel, "ATTACK", &ch.comp_attack, 0.1f, 100.0f, 10.0f, "ms", 256, 66, 76);
    instrument_knob(comp_panel, "RELEASE", &ch.comp_release, 10.0f, 1000.0f, 80.0f, "ms", 372, 66, 76);
    instrument_knob(comp_panel, "KNEE", &ch.comp_knee, 0.0f, 18.0f, 6.0f, "dB", 488, 66, 76);
    instrument_knob(comp_panel, "MAKEUP", &ch.comp_makeup, -12.0f, 18.0f, 0.0f, "dB", 604, 66, 76);

    instrument_button(comp_panel, "COMP BYPASS", 32, 246, 320, 54, &ch.comp_bypass);

    auto* comp_gr_box = lv_obj_create(comp_panel);
    lv_obj_set_pos(comp_gr_box, 372, 246);
    lv_obj_set_size(comp_gr_box, 340, 54);
    lv_obj_remove_flag(comp_gr_box, LV_OBJ_FLAG_SCROLLABLE);
    ui::rack_skin::apply_recessed_display(comp_gr_box, lv_color_hex(0x090D12), COLOR_EDGE);
    lv_obj_set_style_pad_all(comp_gr_box, 4, 0);

    ctx->comp_gr_label = instrument_label(comp_gr_box, "COMP GR: +0.0 dB", 16, 16, &ac_font_16, COLOR_TEXT);

    // =========================================================================
    // LIMITER
    // =========================================================================
    auto* lim_panel = instrument_panel(path, 16, 444, 744, 396, "LIMITER");
    ui::rack_skin::add_faceplate_corner_screws(lim_panel, 744, 396, 4);

    instrument_knob(lim_panel, "CEILING", &ch.limiter_ceiling_db, -24.0f, 0.0f, -0.1f, "dBFS", 60, 110, 84);
    instrument_knob(lim_panel, "RELEASE", &ch.limiter_release_ms, 1.0f, 500.0f, 50.0f, "ms", 216, 110, 84);

    instrument_button(lim_panel, "LIMITER ENABLE", 372, 110, 340, 54, &ch.limiter_enabled);

    auto* lim_gr_box = lv_obj_create(lim_panel);
    lv_obj_set_pos(lim_gr_box, 372, 184);
    lv_obj_set_size(lim_gr_box, 340, 54);
    lv_obj_remove_flag(lim_gr_box, LV_OBJ_FLAG_SCROLLABLE);
    ui::rack_skin::apply_recessed_display(lim_gr_box, lv_color_hex(0x090D12), COLOR_EDGE);
    lv_obj_set_style_pad_all(lim_gr_box, 4, 0);

    ctx->lim_gr_label = instrument_label(lim_gr_box, "LIMITER GR: +0.0 dB", 16, 16, &ac_font_16, COLOR_TEXT);

    // =========================================================================
    // SPEAKER DELAY
    // =========================================================================
    auto* del_panel = instrument_panel(path, 776, 44, 752, 384, "SPEAKER DELAY");
    ui::rack_skin::add_faceplate_corner_screws(del_panel, 752, 384, 4);

    instrument_button(del_panel, "DELAY ENABLE", 32, 56, 330, 48, &ch.speaker_delay_enabled);
    instrument_button(del_panel, "STEREO LINK", 390, 56, 330, 48, &ch.speaker_delay_link);

    instrument_knob(del_panel, "LEFT DELAY", &ch.speaker_delay_l_ms, 0.0f, 100.0f, 0.0f, "ms", 110, 130, 78);
    ctx->delay_knob_r = instrument_knob(del_panel, "RIGHT DELAY", &ch.speaker_delay_r_ms, 0.0f, 100.0f, 0.0f, "ms", 470, 130, 78);

    auto* dist_box = lv_obj_create(del_panel);
    lv_obj_set_pos(dist_box, 32, 276);
    lv_obj_set_size(dist_box, 688, 64);
    lv_obj_remove_flag(dist_box, LV_OBJ_FLAG_SCROLLABLE);
    ui::rack_skin::apply_recessed_display(dist_box, lv_color_hex(0x06090D), COLOR_EDGE);
    lv_obj_set_style_pad_all(dist_box, 6, 0);

    ctx->delay_l_dist_label = instrument_label(dist_box, "LEFT:   0.00 ms  (0.00 m / 0.0 ft)", 24, 20, &ac_font_16, COLOR_TEXT);
    ctx->delay_r_dist_label = instrument_label(dist_box, "RIGHT:  0.00 ms  (0.00 m / 0.0 ft)", 368, 20, &ac_font_16, COLOR_TEXT);

    // =========================================================================
    // EQ
    // =========================================================================
    auto* eq_panel = instrument_panel(path, 776, 444, 752, 396, "EQ");
    ui::rack_skin::add_faceplate_corner_screws(eq_panel, 752, 396, 4);

    instrument_button(eq_panel, "EQ BYPASS", 32, 170, 330, 56, &ch.eq_bypass);

    auto* btn_eq_tab = instrument_button(eq_panel, "OPEN EQUALIZER", 390, 170, 330, 56, nullptr);
    ui::rack_skin::apply_hardware_button(btn_eq_tab, false, COLOR_CYAN, lv_color_hex(0x182B36), lv_color_hex(0x1B222A));
    lv_obj_add_event_cb(btn_eq_tab, [](lv_event_t*) {
        ui_nav_to_tab(2);
    }, LV_EVENT_CLICKED, nullptr);

    // Start Live Telemetry Timer
    ctx->timer = lv_timer_create(channel_master_timer_cb, 50, ctx);
    channel_master_timer_cb(ctx->timer); // Initial paint

    lv_obj_add_event_cb(s, [](lv_event_t* e) {
        auto* c = static_cast<ChannelMasterLiveCtx*>(lv_event_get_user_data(e));
        if (c) {
            if (c->timer) {
                lv_timer_delete(c->timer);
                c->timer = nullptr;
            }
            delete c;
        }
    }, LV_EVENT_DELETE, ctx);

    return s;
}

// Build standard physical/logical input preamp & processing strip view
lv_obj_t* create_input_channel_view(lv_obj_t* s, ChannelState& ch) {
    // Left Panel: Input / Preamp Stage (Genuine Input Channels only)
    auto* in = instrument_panel(s, 16, 56, 300, 864,
                                ch.format == ChannelFormat::Stereo ? "STEREO INPUT / PREAMP" : "MONO INPUT / PREAMP");
    ui::rack_skin::add_faceplate_corner_screws(in, 300, 864, 6);

    instrument_label(in, ch.name, 16, 45, &ac_font_28, COLOR_CYAN);
    char in_fmt_buf[64];
    std::snprintf(in_fmt_buf, sizeof(in_fmt_buf), "%s  [%s]",
                  ch.input_label, ch.format == ChannelFormat::Stereo ? "2-CH STEREO" : "1-CH MONO");
    instrument_label(in, in_fmt_buf, 16, 82, &ac_font_14, COLOR_TEXT2);

    instrument_knob(in, "GAIN", &ch.gain, 0.0f, 1.0f, 0.5f, "dB", 100, 116, 80);
    instrument_knob(in, "HPF FREQ", &ch.hpf_frequency, 20.0f, 500.0f, 80.0f, "Hz", 100, 248, 76);
    instrument_button(in, "POLARITY", 24, 378, 252, 44, &ch.polarity_inverted);
    instrument_button(in, "HPF ENABLE", 24, 432, 252, 44, &ch.hpf_on);

    if (ch.format == ChannelFormat::Mono) {
        instrument_label(in, "INPUT LEVEL", 24, 490, &ac_font_12, COLOR_TEXT2);
        instrument_meter(in, &ch.input_peak_l, &ch.input_clip, 85, 518, 60, 202, &ch.input_peak_hold_l, &ch.input_rms_l);
    } else {
        instrument_label(in, "INPUT METERS (L / R)", 24, 490, &ac_font_12, COLOR_TEXT2);
        instrument_meter(in, &ch.input_peak_l, &ch.input_clip, 55, 518, 46, 202, &ch.input_peak_hold_l, &ch.input_rms_l);
        instrument_meter(in, &ch.input_peak_r, &ch.input_clip, 125, 518, 46, 202, &ch.input_peak_hold_r, &ch.input_rms_r);
    }
    instrument_button(in, "48 V PHANTOM", 24, 752, 252, 44, &ch.phantom_on);

    // Right Panel: Channel Processing / Signal Flow
    auto* path = instrument_panel(s, 336, 56, 1556, 864, "CHANNEL PROCESSING / SIGNAL FLOW");
    ui::rack_skin::add_faceplate_corner_screws(path, 1556, 864, 6);

    const char* names[] = {"INPUT", "GATE", "EQ", "COMP", "INSERT", "FX SENDS",
                           ch.format == ChannelFormat::Stereo ? "BALANCE" : "PAN", "MAIN L/R"};

    for (int i = 0; i < 8; ++i) {
        const int x = 20 + i * 190;
        auto* stage = instrument_panel(path, x, 104, 164, 190, names[i]);
        instrument_label(stage, i == 0 ? "PREAMP" : (i == 7 ? "STEREO" : "ACTIVE"), 15, 49, &ac_font_14, i == 7 ? COLOR_GREEN : COLOR_TEXT2);
        instrument_label(stage, i == 0 ? ch.input_label : (ch.format == ChannelFormat::Stereo ? "STEREO PATH" : "MONO PATH"),
                         15, 79, &ac_font_12, COLOR_TEXT2);

        if (i == 1) {
            instrument_button(stage, "GATE ENABLE", 12, 118, 138, 42, &ch.gate_on);
        } else if (i == 2) {
            instrument_button(stage, "EQ BYPASS", 12, 118, 138, 42, &ch.eq_bypass);
        } else if (i == 3) {
            instrument_button(stage, "COMP BYPASS", 12, 118, 138, 42, &ch.comp_bypass);
        } else if (i == 7) {
            instrument_button(stage, "LIMITER ON", 12, 118, 138, 42, &ch.limiter_enabled);
        } else {
            instrument_button(stage, "ACTIVE", 12, 118, 138, 42, nullptr);
        }
    }

    instrument_label(path, "CHANNEL TRIM", 25, 365, &ac_font_14, COLOR_TEXT2);
    instrument_knob(path, ch.format == ChannelFormat::Stereo ? "BAL" : "PAN",
                    &ch.pan, 0.0f, 1.0f, 0.5f, "L/C/R", 25, 405, 76);
    instrument_knob(path, "FADER", &ch.fader, 0.0f, 1.0f, 0.86f, "dB", 145, 405, 76);

    instrument_label(path, "OUTPUT ASSIGN", 390, 365, &ac_font_14, COLOR_TEXT2);
    for (int i = 0; i < 8; ++i) {
        auto* bus = instrument_button(path, "BUS", 390 + i * 132, 415, 112, 48, &ch.bus_assign[i]);
        if (lv_obj_get_child_count(bus) > 0) {
            lv_label_set_text_fmt(lv_obj_get_child(bus, 0), "BUS %02d", i + 1);
        }
    }

    instrument_label(path, "TAP POINTS", 390, 500, &ac_font_14, COLOR_TEXT2);
    instrument_label(path, "PRE EQ     POST EQ     PRE FADER (AUX)     POST FADER (AUX/MAIN)", 390, 532, &ac_font_16, COLOR_TEXT);
    instrument_label(path, "SOURCE      PREAMP      FILTER      DYNAMICS      INSERT      SENDS      PAN/BAL      OUTPUT", 390, 572, &ac_font_12, COLOR_TEXT2);

    instrument_label(path, "AUX SENDS (LEVEL + ENABLE + PRE/POST-FADER TAP)", 390, 622, &ac_font_14, COLOR_CYAN);
    static const char* aux_labels[4] = {"AUX 01", "AUX 02", "AUX 03", "AUX 04"};
    for (int i = 0; i < 4; ++i) {
        const int ax = 390 + i * 260;
        instrument_knob(path, aux_labels[i], &ch.sends[i], 0.0f, 1.0f, ch.sends[i], "%", ax, 654, 68);
        instrument_button(path, "SEND ON", ax + 96, 672, 128, 36, &ch.send_enabled[i]);
        instrument_button(path, "PRE-FADER", ax + 96, 718, 128, 36, &ch.send_pre_fader[i]);
    }

    return s;
}

} // namespace

lv_obj_t* channel_screen_create(void) {
    auto* s = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(s, 0, 0);
    lv_obj_set_size(s, 1920, 1080);
    lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s, COLOR_BG, 0);

    ChannelState* ch_ptr = mixer_get_selected_channel();
    if (!ch_ptr) {
        ch_ptr = mixer_get_channel(0);
        if (!ch_ptr) ch_ptr = &g_mixer_state.master;
    }

    if (isMasterChannel(ch_ptr)) {
        return create_master_channel_view(s, *ch_ptr);
    } else {
        return create_input_channel_view(s, *ch_ptr);
    }
}
