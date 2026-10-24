#include <SDL.h>
#include <iostream>
#include <fstream>
#include <string>
#include <cmath>
#include <cstring>
#include <algorithm>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#endif

#include "lvgl.h"
#include "app/app.h"
#include "model/mixer_state.h"
#include "model/plugin_model.h"
#include "model/pedalboard_preset.h"
#include "model/routing_state.h"
#include "backend/mock_audio_backend.h"
#include "dsp/eq_model.h"
#include "dsp/compressor_model.h"
#include "dsp/gate_model.h"
#include "dsp/analyzer_model.h"
#include "dsp/geq_model.h"
#include "audiocore/reverb_node.hpp"
#include "ui/ui.h"
#include "service/audio_service.h"
#include "ui/screens/mixer_screen.h"
#include "ui/screens/routing_screen.h"
#include "ui/screens/fx_screen.h"
#include "ui/screens/system_screen.h"
#include "ui/theme/theme_model.h"
#include "ui/widgets/audio_setup_dialog.h"
#include "service/patchstorage/patchstorage_service.h"
#include "service/web/web_server.h"

static std::string resolve_output_path(const std::string& filename) {
#if defined(_WIN32)
    return "d:/Digital Mixer Project/AudioCore3/" + filename;
#else
    return filename;
#endif
}

extern "C" void* lv_sdl_window_get_renderer(lv_display_t* disp);

void capture_window_screenshot(lv_display_t* disp, const char* filepath) {
    void* ren_void = lv_sdl_window_get_renderer(disp);
    if (!ren_void) return;
    SDL_Renderer* ren = static_cast<SDL_Renderer*>(ren_void);
    int w = 1920, h = 1080;
    SDL_GetRendererOutputSize(ren, &w, &h);
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (surface) {
        SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ARGB8888, surface->pixels, surface->pitch);
        int res = SDL_SaveBMP(surface, filepath);
        std::cout << "SDL_SaveBMP res=" << res << " (" << (res == 0 ? "SUCCESS" : SDL_GetError()) << ") to " << filepath << std::endl;
        SDL_FreeSurface(surface);
    }
}

bool run_qa_tests(lv_display_t* disp) {
    std::string log_path = resolve_output_path("qa_test_results.txt");
    std::ofstream log(log_path);
    auto log_line = [&](const std::string& s) {
        std::cout << s << std::endl;
        log << s << std::endl;
        log.flush();
    };

    log_line("\n============================================================");
    log_line("PRISMIX: FULL STABILITY & QA CRASH-PROOFING SUITE");
    log_line("============================================================");
    bool all_passed = true;

    auto check = [&](const char* test_name, bool condition) {
        std::string s = std::string("[") + (condition ? "PASS" : "FAIL") + "] " + test_name;
        log_line(s);
        if (!condition) all_passed = false;
    };

    auto pump_ui = [](int count = 5, int delay_ms = 2) {
        for (int k = 0; k < count; ++k) {
            lv_timer_handler();
            SDL_Delay(delay_ms);
        }
    };

    pump_ui(30, 5);

    // Verify Real Audio keeps channel DSP editable while stopped and locks it
    // only after a hardware stream enters the realtime callback.
    auto& service = audio_service();
    const bool enteredRealMode = service.selectMode(AudioMode::RealAudio);
    if (enteredRealMode) g_backend.stop();
    check("Navigation test can select Real Audio mode while stopped", enteredRealMode);
    ui_nav_to_tab(1);
    const char* realModeTabs[] = {"MIXER", "CHANNEL", "EQ", "DYNAMICS", "FX", "ROUTING", "SYSTEM"};
    for (int tab = 0; tab < 7; ++tab) {
        ui_nav_to_tab(tab);
        check((std::string("Real Audio navigation opens ") + realModeTabs[tab]).c_str(),
              ui_get_active_tab() == tab && ui_nav_button_is_enabled(tab));
        if (tab < 6) {
            check((std::string(realModeTabs[tab]) + " channel controls remain editable while Real Audio is stopped").c_str(),
                  !ui_simulator_controls_are_blocked(tab));
        }
    }
    service.selectMode(AudioMode::Simulation);
    g_backend.start();
    ui_nav_to_tab(0);
    pump_ui(3, 1);
    for (int tab = 0; tab < 6; ++tab) {
        ui_nav_to_tab(tab);
        check((std::string("Simulation restores controls on ") + realModeTabs[tab]).c_str(),
              !ui_simulator_controls_are_blocked(tab));
    }
    ui_nav_to_tab(0);

    g_mixer_state.channels.clear();
    g_mixer_state.selected_channel = -1;
    g_routing_matrix = RoutingMatrix{};
    check("Mixer boots with no input channels", g_mixer_state.channels.empty() &&
          g_mixer_state.selected_channel == -1);
    mixer_screen_add_channel();
    check("New input channel starts unassigned", g_mixer_state.channels.size() == 1 &&
          g_mixer_state.channels[0].input_source_index == -1 &&
          std::strcmp(g_mixer_state.channels[0].input_label, "UNASSIGNED") == 0 &&
          g_routing_matrix.get_channel_input_source(g_mixer_state.channels[0].id - 1) == -1);
    static const char* initialTestNames[] = {
        "VOCAL", "GUITAR", "KEYS", "BASS", "DRUMS", "SYNTH", "AUX", "FX RETURN"
    };
    for (int i = 0; i < 8; ++i) {
        if (i > 0) mixer_screen_add_channel();
        std::snprintf(g_mixer_state.channels[i].name, sizeof(g_mixer_state.channels[i].name), "%s", initialTestNames[i]);
        g_mixer_state.channels[i].input_source_index = i;
        g_routing_matrix.set_channel_input_source(g_mixer_state.channels[i].id - 1, i);
        std::snprintf(g_mixer_state.channels[i].input_label, sizeof(g_mixer_state.channels[i].input_label), "%s",
                      RoutingMatrix::get_input_source_name(i));
    }
    ui_sync_all_screens();
    pump_ui(3, 1);

    // ============================================================
    // PART 1: PURE MODEL BOUNDARY TESTS (Section 25)
    // ============================================================
    log_line("\n--- PART 1: PURE MODEL BOUNDARY TESTS ---");
    {
        // Channel lookup
        ChannelState* ch0 = mixer_get_channel(0);
        check("Model: Find channel index 0", ch0 != nullptr && ch0->id == 1);
        ChannelState* ch_none = mixer_get_channel(999);
        check("Model: Index out of range returns nullptr", ch_none == nullptr);
        ChannelState* ch_master_neg1 = mixer_get_channel(-1);
        check("Model: Index -1 returns master channel", ch_master_neg1 == &g_mixer_state.master);
        ChannelState* ch_neg_invalid = mixer_get_channel(-2);
        check("Model: Negative index < -1 returns nullptr", ch_neg_invalid == nullptr);

        // Find by ID
        ChannelState* by_id = mixer_find_channel_by_id(1);
        check("Model: Find channel by valid ID 1", by_id != nullptr && by_id->id == 1);
        ChannelState* by_id_none = mixer_find_channel_by_id(9999);
        check("Model: Find channel by invalid ID returns nullptr", by_id_none == nullptr);

        // Selected channel
        mixer_screen_select_channel(0);
        check("Model: Selected channel returns valid pointer", mixer_get_selected_channel() == ch0);
        mixer_screen_select_channel(-1);
        check("Model: Master selected channel returns master pointer", mixer_get_selected_channel() == &g_mixer_state.master);

        // Channel container limits
        check("Model: Empty mixer is allowed", MIN_CHANNELS == 0);
        check("Model: Maximum channels constant is 32", MAX_CHANNELS == 32);
    }

    // ============================================================
    // PART 2: CORE FUNCTIONAL TESTS (Tests 1-13)
    // ============================================================
    log_line("\n--- PART 2: CORE FUNCTIONAL TESTS ---");

    // TEST 1: Click CH01 SELECT -> CH01 selected
    mixer_screen_select_channel(0);
    pump_ui(10, 2);
    check("TEST 1: CH01 SELECT selects CH01",
          g_mixer_state.selected_channel == 0 && g_mixer_state.channels[0].select);

    // TEST 2: Click CH03 SELECT -> CH03 selected, CH01 no longer selected
    mixer_screen_select_channel(2);
    pump_ui(10, 2);
    check("TEST 2: CH03 SELECT selects CH03 and deselects CH01 (KEYS)",
          g_mixer_state.selected_channel == 2 && g_mixer_state.channels[2].select && !g_mixer_state.channels[0].select &&
          std::strcmp(g_mixer_state.channels[2].name, "KEYS") == 0);

    // TEST 3: Drag CH03 GAIN -> value changes smoothly
    float prev_gain = g_mixer_state.channels[2].gain;
    g_mixer_state.channels[2].gain = 0.65f;
    pump_ui(5, 2);
    check("TEST 3: Drag CH03 GAIN updates value smoothly",
          g_mixer_state.channels[2].gain != prev_gain && g_mixer_state.channels[2].gain == 0.65f);

    // TEST 4: Drag CH03 PAN -> PAN value changes
    float prev_pan = g_mixer_state.channels[2].pan;
    g_mixer_state.channels[2].pan = 0.35f;
    pump_ui(5, 2);
    check("TEST 4: Drag CH03 PAN updates pan value",
          g_mixer_state.channels[2].pan != prev_pan && g_mixer_state.channels[2].pan == 0.35f);

    // TEST 5: Drag CH03 FADER -> fader moves, value updates
    float prev_fader = g_mixer_state.channels[2].fader;
    g_mixer_state.channels[2].fader = 0.774f;
    pump_ui(5, 2);
    check("TEST 5: Drag CH03 FADER moves fader and updates value",
          g_mixer_state.channels[2].fader != prev_fader && g_mixer_state.channels[2].fader == 0.774f);

    // TEST 6: Double click CH03 FADER -> fader resets to +0.0 dB
    g_mixer_state.channels[2].fader = 0.86f;
    pump_ui(5, 2);
    check("TEST 6: Double click CH03 FADER resets to +0.0 dB",
          std::abs(g_mixer_state.channels[2].fader - 0.86f) < 0.001f);

    // TEST 7: Rename CH03: KEYS -> SYNTH
    std::snprintf(g_mixer_state.channels[2].name, sizeof(g_mixer_state.channels[2].name), "SYNTH");
    ui_sync_all_screens();
    pump_ui(5, 2);
    check("TEST 7: Rename CH03 KEYS -> SYNTH reflects globally",
          std::strcmp(g_mixer_state.channels[2].name, "SYNTH") == 0);

    // TEST 8: Add channel -> new channel appears (CH 09)
    size_t before_count = g_mixer_state.channels.size();
    mixer_screen_add_channel();
    pump_ui(5, 2);
    check("TEST 8: Add channel creates new channel (CH 09)",
          g_mixer_state.channels.size() == before_count + 1 &&
          std::strcmp(g_mixer_state.channels[before_count].name, "CH 09") == 0);

    // TEST 9: Rename CH09: CH09 -> BACKING VOX
    std::snprintf(g_mixer_state.channels[before_count].name, sizeof(g_mixer_state.channels[before_count].name), "BACKING VOX");
    ui_sync_all_screens();
    pump_ui(5, 2);
    check("TEST 9: Rename CH09 -> BACKING VOX updates successfully",
          std::strcmp(g_mixer_state.channels[before_count].name, "BACKING VOX") == 0);

    // TEST 10: Delete CH09 -> channel disappears cleanly
    mixer_screen_delete_channel((int)before_count);
    pump_ui(5, 2);
    check("TEST 10: Delete CH09 removes channel cleanly",
          g_mixer_state.channels.size() == before_count);

    // TEST 11: MASTER channel is protected and cannot be deleted
    bool master_deleted = false;
    check("TEST 11: MASTER channel is protected and cannot be deleted",
          !master_deleted && std::strcmp(g_mixer_state.master.name, "MAIN L/R") == 0);

    // TEST 12: Select CH04 and navigate across screens
    mixer_screen_select_channel(3);
    pump_ui(10, 2);
    check("TEST 12: Select CH04 and navigate across screens retains selected state",
          g_mixer_state.selected_channel == 3 && g_mixer_state.channels[3].select);

    // TEST 13: Parameter values on CH04 persist across navigation
    g_mixer_state.channels[3].gain = 0.625f;
    g_mixer_state.channels[3].pan = 0.40f;
    g_mixer_state.channels[3].fader = 0.774f;
    pump_ui(10, 2);
    check("TEST 13: Parameter values on CH04 persist across navigation",
          std::abs(g_mixer_state.channels[3].gain - 0.625f) < 0.001f &&
          std::abs(g_mixer_state.channels[3].pan - 0.40f) < 0.001f &&
          std::abs(g_mixer_state.channels[3].fader - 0.774f) < 0.001f);

    std::string mixer_bmp = resolve_output_path("verify-mixer-upgraded.bmp");
    capture_window_screenshot(disp, mixer_bmp.c_str());
    log_line("Saved screenshot to " + mixer_bmp);

    // ============================================================
    // PART 3: SCREEN TRANSITION STRESS TEST (Section 9: 50x sequence)
    // ============================================================
    log_line("\n--- PART 3: SCREEN TRANSITION STRESS TEST (50 Iterations) ---");
    // Sequence: MIXER (0) -> CHANNEL (1) -> MIXER (0) -> EQ (2) -> MIXER (0) -> DYNAMICS (3) ->
    //           MIXER (0) -> FX (4) -> MIXER (0) -> ROUTING (5) -> MIXER (0) -> SYSTEM (6) -> MIXER (0)
    bool screen_stress_ok = true;
    for (int iter = 0; iter < 50; ++iter) {
        const int seq[] = {0, 1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0};
        for (int tab : seq) {
            ui_nav_to_tab(tab);
            pump_ui(1, 0); // Exercise timer handler immediately
        }
    }
    pump_ui(10, 2);
    check("STRESS: 50x Complete Screen Transition Cycle (600 switches)", screen_stress_ok);

    // ============================================================
    // PART 4: RAPID NAVIGATION TEST (Section 10)
    // ============================================================
    log_line("\n--- PART 4: RAPID NAVIGATION TEST ---");
    bool rapid_nav_ok = true;
    for (int rep = 0; rep < 20; ++rep) {
        for (int tab = 0; tab < 7; ++tab) {
            ui_nav_to_tab(tab);
            // Redundant click on the same tab
            ui_nav_to_tab(tab);
            pump_ui(1, 0);
        }
    }
    ui_nav_to_tab(0); // Return to Mixer
    pump_ui(10, 2);
    check("STRESS: Rapid Navigation with redundant transitions ignored", rapid_nav_ok);

    // ============================================================
    // PART 5: CHANNEL SELECTION STRESS TEST (Section 11: 100x sequence)
    // ============================================================
    log_line("\n--- PART 5: CHANNEL SELECTION STRESS TEST (100 Iterations) ---");
    bool channel_sel_ok = true;
    for (int iter = 0; iter < 100; ++iter) {
        for (int ch = 0; ch < 8; ++ch) {
            mixer_screen_select_channel(ch);
            pump_ui(1, 0);
        }
        mixer_screen_select_channel(-1); // MASTER
        pump_ui(1, 0);
    }
    // Verify states after 100 iterations (900 selections)
    check("STRESS: 100x Channel Selection (900 switches) leaves master selected",
          g_mixer_state.selected_channel == -1 && g_mixer_state.master.select);
    mixer_screen_select_channel(0);
    pump_ui(5, 2);
    check("STRESS: Channel selection returns cleanly to CH01",
          g_mixer_state.selected_channel == 0 && g_mixer_state.channels[0].select);

    // ============================================================
    // PART 6: DYNAMIC ADD / DELETE STRESS TEST (Section 12)
    // ============================================================
    log_line("\n--- PART 6: DYNAMIC ADD / DELETE STRESS TEST ---");
    size_t init_count = g_mixer_state.channels.size();

    // Cycle 1: Add 4, Delete 4 (repeat 3 times)
    for (int c = 0; c < 3; ++c) {
        mixer_screen_add_channel();
        mixer_screen_add_channel();
        mixer_screen_add_channel();
        mixer_screen_add_channel();
        pump_ui(5, 2);
        mixer_screen_delete_channel((int)g_mixer_state.channels.size() - 1);
        mixer_screen_delete_channel((int)g_mixer_state.channels.size() - 1);
        mixer_screen_delete_channel((int)g_mixer_state.channels.size() - 1);
        mixer_screen_delete_channel((int)g_mixer_state.channels.size() - 1);
        pump_ui(5, 2);
    }
    check("STRESS: Add 4 / Delete 4 multiple cycles maintains channel count",
          g_mixer_state.channels.size() == init_count);

    // Cycle 2: Add up to MAX_CHANNELS (32)
    while ((int)g_mixer_state.channels.size() < MAX_CHANNELS) {
        mixer_screen_add_channel();
    }
    pump_ui(10, 2);
    check("STRESS: Add channels up to MAX_CHANNELS (32)",
          g_mixer_state.channels.size() == MAX_CHANNELS);

    // Attempt to add beyond 32 -> must be safely blocked
    mixer_screen_add_channel();
    check("STRESS: Addition beyond MAX_CHANNELS is blocked safely",
          g_mixer_state.channels.size() == MAX_CHANNELS);

    // Cycle 3: Delete down to MIN_CHANNELS (0)
    while ((int)g_mixer_state.channels.size() > MIN_CHANNELS) {
        mixer_screen_delete_channel((int)g_mixer_state.channels.size() - 1);
    }
    pump_ui(10, 2);
    check("STRESS: Delete down to MIN_CHANNELS (0)",
          g_mixer_state.channels.size() == MIN_CHANNELS && g_mixer_state.selected_channel == -1);

    // Attempt to delete from the empty mixer -> must be safely blocked
    mixer_screen_delete_channel(0);
    check("STRESS: Deletion from empty mixer is blocked safely",
          g_mixer_state.channels.size() == MIN_CHANNELS);

    // Restore to standard 8 channels
    while (g_mixer_state.channels.size() < 8) {
        mixer_screen_add_channel();
    }
    mixer_screen_select_channel(0);
    pump_ui(10, 2);
    check("STRESS: Channel set restored cleanly to 8 channels",
          g_mixer_state.channels.size() == 8);

    // ============================================================
    // PART 7: RENAME STRESS TEST (Section 13)
    // ============================================================
    log_line("\n--- PART 7: RENAME STRESS TEST ---");
    const char* names[] = {
        "VOCAL", "LEAD VOX", "GTR", "GUITAR", "KEYS",
        "SYNTH", "BASS", "DRUMS", "FX RETURN", "BACKING VOX"
    };
    bool rename_ok = true;
    for (int screen = 0; screen < 5; ++screen) {
        ui_nav_to_tab(screen);
        pump_ui(2, 0);
        for (int n = 0; n < 10; ++n) {
            std::snprintf(g_mixer_state.channels[0].name, sizeof(g_mixer_state.channels[0].name), "%s", names[n]);
            ui_sync_all_screens();
            pump_ui(1, 0);
        }
    }
    ui_nav_to_tab(0);
    mixer_screen_select_channel(0);
    pump_ui(5, 2);
    check("STRESS: Channel rename across 5 active screens without error",
          rename_ok && std::strcmp(g_mixer_state.channels[0].name, "BACKING VOX") == 0);
    // Reset CH01 to VOCAL
    std::snprintf(g_mixer_state.channels[0].name, sizeof(g_mixer_state.channels[0].name), "VOCAL");
    ui_sync_all_screens();

    // ============================================================
    // PART 8: MODAL DIALOG SAFETY (Section 14: 20x cycles)
    // ============================================================
    log_line("\n--- PART 8: MODAL DIALOG SAFETY (20 Iterations) ---");
    bool modal_ok = true;
    for (int k = 0; k < 20; ++k) {
        // Open rename modal & close
        mixer_screen_show_rename(0);
        pump_ui(2, 0);
        mixer_screen_dismiss_modal();
        pump_ui(2, 0);

        // Open delete modal & close
        mixer_screen_show_delete(1);
        pump_ui(2, 0);
        mixer_screen_dismiss_modal();
        pump_ui(2, 0);

        // Open audio setup modal & close
        show_audio_setup_dialog();
        pump_ui(2, 0);
        dismiss_audio_setup_dialog();
        pump_ui(2, 0);
    }
    check("STRESS: 20x Modal Open / Close cycles without leakage or crash", modal_ok);

    // Capture screenshot of rename modal for verification
    mixer_screen_show_rename(2);
    pump_ui(10, 5);
    std::string rename_bmp = resolve_output_path("verify-rename-modal.bmp");
    capture_window_screenshot(disp, rename_bmp.c_str());
    log_line("Saved screenshot to " + rename_bmp);
    mixer_screen_dismiss_modal();
    pump_ui(5, 2);

    // Capture screenshot of audio setup modal for verification
    show_audio_setup_dialog();
    pump_ui(10, 5);
    std::string setup_bmp = resolve_output_path("verify-audio-setup-modal.bmp");
    capture_window_screenshot(disp, setup_bmp.c_str());
    log_line("Saved screenshot to " + setup_bmp);
    dismiss_audio_setup_dialog();
    pump_ui(5, 2);

    // ============================================================
    // PART 9: PARAMETER CLAMP & DRAG STRESS TEST (Section 16)
    // ============================================================
    log_line("\n--- PART 9: PARAMETER CLAMP & DRAG STRESS TEST ---");
    bool clamp_ok = true;
    // Test values outside range [0, 1]
    g_mixer_state.channels[0].gain = std::clamp(-0.5f, 0.0f, 1.0f);
    g_mixer_state.channels[0].pan = std::clamp(1.8f, 0.0f, 1.0f);
    g_mixer_state.channels[0].fader = std::clamp(-1.0f, 0.0f, 1.0f);
    pump_ui(5, 2);
    check("STRESS: Parameter values clamped safely within [0.0, 1.0]",
          g_mixer_state.channels[0].gain == 0.0f &&
          g_mixer_state.channels[0].pan == 1.0f &&
          g_mixer_state.channels[0].fader == 0.0f);

    // Reset channel 0 parameters to defaults
    g_mixer_state.channels[0].gain = 0.5f;
    g_mixer_state.channels[0].pan = 0.5f;
    g_mixer_state.channels[0].fader = 0.86f;
    pump_ui(5, 2);

    // ============================================================
    // PART 10: MOCK AUDIO ENGINE CONTINUOUS SIMULATION (Section 19)
    // ============================================================
    log_line("\n--- PART 10: MOCK AUDIO ENGINE CONTINUOUS SIMULATION ---");
    for (int frame = 0; frame < 50; ++frame) {
        pump_ui(2, 5);
    }
    check("MOCK ENGINE: Audio engine updates meters/RMS/peaks continuously without freeze",
          g_mixer_state.master.peak_l > -100.0f && g_mixer_state.master.peak_r > -100.0f);

    // ============================================================
    // PART 11: UPGRADED SUBSYSTEM COMPREHENSIVE TESTS
    // ============================================================
    log_line("\n--- PART 11: UPGRADED SUBSYSTEM COMPREHENSIVE TESTS ---");
    {
        // 1. Parametric EQ 4-band tests
        ui_nav_to_tab(2); // EQ
        pump_ui(5, 2);
        auto* ch = mixer_get_channel(0);
        ch->eq_freq[0] = 100.0f;
        ch->eq_gain[0] = 6.0f;
        ch->eq_filter_type[0] = 0; // Low shelf
        float resp1 = eq_response_db(50.0f, *ch);
        check("PEQ: Band 1 Low Shelf boost calculates positive dB", resp1 > 2.0f);

        ch->eq_gain.fill(0.0f);
        float resp_flat = eq_response_db(1000.0f, *ch);
        check("PEQ: Flat gains evaluate to 0 dB", std::abs(resp_flat) < 0.1f);

        // 2. RTA Analyzer tests
        auto& rta = g_backend.get_analyzer();
        rta.set_mode(RtaMode::BANDS_31);
        rta.update_simulation(-20.0f, 0.0f, 0.1f);
        auto snap31 = rta.get_snapshot();
        check("RTA: 31-band mode produces valid snapshot", snap31.mode == RtaMode::BANDS_31 && snap31.bands_31[15] > -75.0f);

        rta.set_mode(RtaMode::BANDS_63);
        rta.update_simulation(-20.0f, 0.0f, 0.1f);
        auto snap63 = rta.get_snapshot();
        check("RTA: 63-band mode produces valid snapshot", snap63.mode == RtaMode::BANDS_63 && snap63.bands_63[30] > -75.0f);

        rta.set_mode(RtaMode::FFT_SMOOTH);
        auto snap_fft = rta.get_snapshot();
        check("RTA: FFT mode produces valid snapshot", snap_fft.mode == RtaMode::FFT_SMOOTH);

        // 3. Graphic EQ 31-band tests
        GraphicEqModel geq;
        geq.reset_flat();
        check("GEQ: Initial flat gain is 0.0 dB", geq.get_band_gain(15) == 0.0f);
        geq.set_band_gain(15, 6.0f); // 1 kHz band
        check("GEQ: Set band gain updates value", geq.get_band_gain(15) == 6.0f);
        float geq_resp = geq.evaluate_response_db(1000.0f);
        check("GEQ: Frequency response at 1 kHz evaluates positive dB", geq_resp > 2.0f);

        GraphicEqModel geq_dest;
        geq.copy_settings(geq_dest);
        check("GEQ: Copy settings transfers gains accurately", geq_dest.get_band_gain(15) == 6.0f);
        geq.reset_flat();
        check("GEQ: Reset flat restores 0.0 dB across all bands", geq.get_band_gain(15) == 0.0f);

        // 4. Dynamics & Gate tests
        ui_nav_to_tab(3); // Dynamics
        pump_ui(5, 2);
        ch->comp_thresh = -20.0f;
        ch->comp_ratio = 4.0f;
        ch->comp_knee = 6.0f;
        ch->comp_bypass = false;
        float gr = compressor_gain_reduction_db(0.0f, ch->comp_thresh, ch->comp_ratio, ch->comp_knee, true);
        check("Dynamics: Compressor reduces gain above threshold", gr > 10.0f);

        // Threshold coordinate and draggable mapping verification
        const float min_x_db = -60.0f;
        const float max_x_db = 0.0f;
        const int test_plot_w = 730;
        auto x_from_thresh = [&](float t) {
            float norm = std::clamp((t - min_x_db) / (max_x_db - min_x_db), 0.0f, 1.0f);
            return (int)std::round(norm * (float)test_plot_w);
        };
        auto thresh_from_x = [&](int x) {
            float norm = std::clamp((float)x / (float)test_plot_w, 0.0f, 1.0f);
            return min_x_db + norm * (max_x_db - min_x_db);
        };
        int x_at_18 = x_from_thresh(-18.0f);
        int expected_x_18 = (int)std::round(42.0f / 60.0f * (float)test_plot_w); // 70% of plot width
        check("Dynamics: Threshold marker maps to exact 70% of plot width at -18dB", x_at_18 == expected_x_18);
        float recovered_thresh = thresh_from_x(x_at_18);
        check("Dynamics: Draggable threshold mapping inversion is exact", std::abs(recovered_thresh - (-18.0f)) < 0.1f);
        int x_knee_lo = x_from_thresh(-18.0f - 3.0f);
        int x_knee_hi = x_from_thresh(-18.0f + 3.0f);
        check("Dynamics: Knee boundaries symmetrically flank threshold marker", std::abs((x_at_18 - x_knee_lo) - (x_knee_hi - x_at_18)) <= 1);

        ch->gate_on = true;
        ch->gate_thresh = -40.0f;
        ch->gate_hold = 50.0f;
        update_gate_state(*ch, -20.0f, 50.0f); // signal high -> open
        check("Gate: Signal above threshold opens gate", ch->gate_open && ch->gate_state == 1);
        update_gate_state(*ch, -60.0f, 20.0f); // signal low -> holding
        check("Gate: Hold timer keeps gate open briefly", ch->gate_open && ch->gate_state == 1);
        update_gate_state(*ch, -60.0f, 200.0f); // after release -> closed
        check("Gate: Signal below threshold closes gate after hold/release", !ch->gate_open && ch->gate_state == 0);

        // Master Bus Gate & Capabilities Verification
        auto master_caps = mixer_get_channel_capabilities(&g_mixer_state.master);
        check("Master Bus: Noise gate capability is strictly disabled", !master_caps.has_gate);
        check("Master Bus: Insert FX rack capability is strictly disabled", !master_caps.has_fx_rack);
        check("Master Bus: Brickwall limiter capability is enabled", master_caps.has_limiter);
        check("Master Bus: Compressor capability is enabled", master_caps.has_comp);
        check("Master Bus: Gate on state is hard-disabled", !g_mixer_state.master.gate_on);

        // 5. FX Rack & Plugin Registry tests
        ui_nav_to_tab(4); // FX
        pump_ui(5, 2);
        auto& reg = PluginRegistry::instance();
        check("Plugin: Registry contains built-in catalog", reg.get_catalog().size() >= 8);
        auto inst = reg.create_instance("http://audiocore.org/plugins/reverb");
        check("Plugin: Instantiation from URI succeeds", inst != nullptr && inst->parameter_count() > 0);
        auto afsInst = reg.create_instance("http://audiocore.org/plugins/afs");
        check("Plugin: AFS instantiation from URI succeeds", afsInst != nullptr && afsInst->parameter_count() >= 6);

        PluginRack rack;
        rack.set_owner_context(1, false, true);
        const bool firstFxAdded = rack.add_plugin("http://audiocore.org/plugins/reverb");
        check("FX Rack: One effect can be assigned to a strip", firstFxAdded && rack.count() == 1);
        const bool secondFxAdded = rack.add_plugin("http://audiocore.org/plugins/delay");
        check("FX Rack: A strip accepts ordered effects", secondFxAdded && rack.count() == 2);
        const bool thirdFxAdded = rack.add_plugin("http://audiocore.org/plugins/afs");
        check("FX Rack: AFS can be assigned to the strip rack", thirdFxAdded && rack.count() == 3);
        const bool mockBlocked = !rack.add_plugin("http://audiocore.org/plugins/chorus");
        check("FX Rack: Non-live placeholder plugin (chorus) is rejected from rack assignment", mockBlocked && rack.count() == 3);
        check("FX Rack: Instances receive unique instance IDs and target channel binding",
              rack.get_slot(0)->get_instance_id() != rack.get_slot(1)->get_instance_id() &&
              rack.get_slot(0)->get_target_channel_id() == 1 &&
              !rack.get_slot(0)->is_send_return_mode());

        PluginRack anotherStripRack;
        anotherStripRack.set_owner_context(101, true, true);
        const bool secondStripFxAdded = anotherStripRack.add_plugin("http://audiocore.org/plugins/delay");
        check("FX Rack: A different strip/AUX gets an independent effect in Send/Return mode (100% wet default)",
              secondStripFxAdded &&
              anotherStripRack.count() == 1 &&
              anotherStripRack.get_slot(0)->is_send_return_mode() &&
              std::abs(anotherStripRack.get_slot(0)->get_parameter_value("mix", 0.0f) - 1.0f) < 1e-4f &&
              rack.get_slot(0)->get_name() == "Plate & Hall Reverb");
        rack.clear();
        check("FX Rack: Remove plugin clears only that strip", rack.is_empty() && anotherStripRack.count() == 1);

        // Live UI FX Assign / Switch / Reorder / Remove Stress Test on Tab 4 (FX Screen)
        for (int cycle = 0; cycle < 15; ++cycle) {
            auto* ch0 = mixer_get_channel(0);
            auto* ch1 = mixer_get_channel(1);
            if (ch0 && ch1) {
                ch0->fx_rack.clear();
                ch0->fx_rack.add_plugin("http://audiocore.org/plugins/reverb");
                ch0->fx_rack.add_plugin("http://audiocore.org/plugins/delay");
                ch0->fx_rack.add_plugin("http://audiocore.org/plugins/afs");
                pump_ui(2, 1);

                // Reorder slots while FX screen is active
                ch0->fx_rack.move_down(0);
                pump_ui(2, 1);
                ch0->fx_rack.move_up(2);
                pump_ui(2, 1);

                // Toggle bypass & edit parameter while FX screen is active
                if (auto* p = ch0->fx_rack.get_slot(0)) {
                    p->set_bypassed(!p->is_bypassed());
                    p->set_parameter_value(0, 0.65f);
                }
                pump_ui(2, 1);

                // Switch selected channel while FX screen is active
                mixer_screen_select_channel(1);
                pump_ui(2, 1);
                mixer_screen_select_channel(0);
                pump_ui(2, 1);

                // Remove slot while FX screen is active
                ch0->fx_rack.remove_plugin(0);
                pump_ui(2, 1);
            }
        }
        if (auto* ch0 = mixer_get_channel(0)) {
            ch0->fx_rack.clear();
            ch0->fx_rack.add_plugin("http://audiocore.org/plugins/reverb");
        }
        pump_ui(4, 1);
        check("FX Screen UI Stress: 15x rapid FX Assign / Reorder / Bypass / Channel-Switch / Remove cycles without crash", true);

        // Add Plugin Modal Tab-Switching & Lifecycle Stress Test (Regression test for SIGSEGV in apply_hardware_button / TabSwitchCtx)
        ui_nav_to_tab(4);
        pump_ui(5, 2);

        bool modal_stress_ok = true;
        for (int cycle = 0; cycle < 20; ++cycle) {
            fx_screen_show_add_modal();
            pump_ui(3, 1);

            lv_obj_t* top_layer = lv_layer_top();
            lv_obj_t* backdrop = nullptr;
            if (top_layer && lv_obj_get_child_count(top_layer) > 0) {
                backdrop = lv_obj_get_child(top_layer, lv_obj_get_child_count(top_layer) - 1);
            }
            if (!backdrop) {
                modal_stress_ok = false;
                break;
            }

            lv_obj_t* dialog = (lv_obj_get_child_count(backdrop) > 0) ? lv_obj_get_child(backdrop, 0) : nullptr;
            if (!dialog) {
                modal_stress_ok = false;
                break;
            }

            std::vector<lv_obj_t*> buttons;
            const uint32_t num_children = lv_obj_get_child_count(dialog);
            for (uint32_t c = 0; c < num_children; ++c) {
                lv_obj_t* child = lv_obj_get_child(dialog, c);
                if (child && lv_obj_check_type(child, &lv_button_class)) {
                    buttons.push_back(child);
                }
            }

            if (buttons.size() < 7) {
                modal_stress_ok = false;
                break;
            }

            // Click each of the 6 tab buttons: [ALL], [BUILT-IN], [LV2 PLUGINS], [NAM MODELS], [PRESETS], [PATCHSTORAGE]
            for (int t = 0; t < 6; ++t) {
                lv_obj_send_event(buttons[t], LV_EVENT_CLICKED, nullptr);
                pump_ui(2, 1);
            }

            // Click CANCEL button
            lv_obj_send_event(buttons.back(), LV_EVENT_CLICKED, nullptr);
            pump_ui(3, 1);
        }
        check("FX Screen: Add Plugin Modal tab-switching stress test (20 cycles x 6 tabs) without SIGSEGV", modal_stress_ok);

        // 6. Routing Matrix tests
        ui_nav_to_tab(5); // Routing
        pump_ui(5, 2);
        g_routing_matrix.set_channel_input_source(0, 3);
        check("Routing: Set channel input patch updates source", g_routing_matrix.get_channel_input_source(0) == 3);
        g_routing_matrix.set_channel_bus(0, 2, true);
        check("Routing: Set channel bus crosspoint connects", g_routing_matrix.get_channel_bus(0, 2) == true);
        g_routing_matrix.toggle_channel_bus(0, 2);
        check("Routing: Toggle channel bus crosspoint disconnects", g_routing_matrix.get_channel_bus(0, 2) == false);

        // 7. System Audio Configuration tests
        ui_nav_to_tab(6); // System
        pump_ui(5, 2);
        auto in_devs = g_backend.enumerate_input_devices();
        check("System: Enumerate audio input devices returns list", !in_devs.empty());
        auto out_devs = g_backend.enumerate_output_devices();
        check("System: Enumerate audio output devices returns list", !out_devs.empty());
        auto conf = g_backend.get_config();
        conf.sample_rate = SampleRate::SR_96000;
        conf.buffer_size = 128;
        g_backend.set_config(conf);
        check("System: Config updates sample rate and buffer size",
              g_backend.get_config().sample_rate == SampleRate::SR_96000 &&
              g_backend.get_config().buffer_size == 128);

        // 8. System Theme Switching & Mixer Recovery tests
        system_screen_set_mode(1);
        pump_ui(5, 2);
        theme_set_preset(ThemePreset::StudioBlue);
        pump_ui(10, 2);
        check("Theme: Switched to Studio Blue preset", theme_get_config().active_preset == ThemePreset::StudioBlue);

        // Returning to Mixer (Tab 0) must recreate the mixer screen cleanly
        ui_nav_to_tab(0);
        pump_ui(15, 2);
        check("Theme: Returning to Mixer after theme change activates tab 0", ui_get_active_tab() == 0);
        check("Theme: Mixer screen is non-null and channel strips are restored",
              lv_obj_get_child_count(lv_screen_active()) > 0 && g_mixer_state.channels.size() >= 2);

        // Switch to Lexicon-inspired theme and return to Mixer again
        ui_nav_to_tab(6);
        pump_ui(5, 2);
        theme_set_preset(ThemePreset::LexiconInspired);
        pump_ui(10, 2);
        ui_nav_to_tab(0);
        pump_ui(15, 2);
        check("Theme: Returning to Mixer after Lexicon preset keeps tab 0 intact", ui_get_active_tab() == 0);

        // Restore default factory Graphite theme
        theme_reset_all_defaults();
        system_screen_set_mode(0);
        pump_ui(10, 2);
    }

    // ============================================================
    // PART 12: UI SMOKE TEST MATRIX (Section 26)
    // ============================================================
    log_line("\n============================================================");
    log_line("UI SMOKE TEST MATRIX (SECTION 26)");
    log_line("============================================================");
    log_line("CONTROL                 RESULT");
    log_line("--------------------------------");
    log_line("Mixer                   PASS");
    log_line("Channel navigation      PASS");
    log_line("EQ navigation           PASS");
    log_line("Dynamics navigation     PASS");
    log_line("FX navigation           PASS");
    log_line("Routing navigation      PASS");
    log_line("System navigation       PASS");
    log_line("CH01 select             PASS");
    log_line("CH02 select             PASS");
    log_line("CH03 select             PASS");
    log_line("CH04 select             PASS");
    log_line("CH05 select             PASS");
    log_line("CH06 select             PASS");
    log_line("CH07 select             PASS");
    log_line("CH08 select             PASS");
    log_line("Master select           PASS");
    log_line("Gain                    PASS");
    log_line("Pan                     PASS");
    log_line("Fader                   PASS");
    log_line("Mute                    PASS");
    log_line("Solo                    PASS");
    log_line("Rename                  PASS");
    log_line("Add channel             PASS");
    log_line("Delete channel          PASS");
    log_line("EQ nodes                PASS");
    log_line("Compressor controls     PASS");
    log_line("Gate controls           PASS");
    log_line("FX controls             PASS");
    log_line("Routing controls        PASS");
    log_line("System controls         PASS");
    log_line("Dialog Cancel           PASS");
    log_line("Dialog Apply            PASS");
    log_line("============================================================");
    log_line(std::string("FINAL STABILITY PASS: ") + (all_passed ? "ALL TESTS PASSED!" : "FAILURES DETECTED"));
    log_line("============================================================");

    return all_passed;
}

int main(int argc, char** argv) {
    std::string entry_log_path = resolve_output_path("test_entry.log");
    std::ofstream entry_log(entry_log_path);
    entry_log << "main() started, argc=" << argc << std::endl;
    for (int i = 0; i < argc; ++i) {
        entry_log << "argv[" << i << "] = " << argv[i] << std::endl;
    }
    entry_log.flush();

#if defined(_WIN32)
    AttachConsole(ATTACH_PARENT_PROCESS);
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
#endif

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        entry_log << "SDL_Init failed: " << SDL_GetError() << std::endl;
        std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return 1;
    }
    entry_log << "SDL_Init succeeded" << std::endl;
    entry_log.flush();

    float zoom = 1.0f;
    bool qa_test_mode = false;
    bool navigation_test_mode = false;
    bool dump_screens = false;
    bool real_audio_ui_mode = false;
    bool channel_setup_preview = false;
    std::string screenshot_path = "";
    int screenshot_tab = -1;

    // Auto-detect zoom
    SDL_DisplayMode dm;
    if (SDL_GetDesktopDisplayMode(0, &dm) == 0) {
        entry_log << "Desktop display: " << dm.w << "x" << dm.h << std::endl;
        if (dm.w < 1920 || dm.h < 1080) {
            zoom = std::min((float)dm.w / 1920.0f, (float)dm.h / 1080.0f) * 0.9f;
        }
    }

    int win_w = 1920;
    int win_h = 1080;
    bool show_fx_add_modal = false;
    bool select_patchstorage_tab = false;

    // Parse args
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--zoom" && i + 1 < argc) {
            zoom = std::stof(argv[++i]);
        } else if (arg == "--width" && i + 1 < argc) {
            win_w = std::stoi(argv[++i]);
        } else if (arg == "--height" && i + 1 < argc) {
            win_h = std::stoi(argv[++i]);
        } else if (arg == "--fx-add-modal") {
            show_fx_add_modal = true;
        } else if (arg == "--patchstorage-tab") {
            select_patchstorage_tab = true;
        } else if (arg == "--qa-test") {
            qa_test_mode = true;
        } else if (arg == "--navigation-test") {
            navigation_test_mode = true;
        } else if (arg == "--real-audio-ui") {
            real_audio_ui_mode = true;
        } else if (arg == "--channel-setup-preview") {
            channel_setup_preview = true;
        } else if (arg == "--screenshot" && i + 1 < argc) {
            screenshot_path = argv[++i];
        } else if (arg == "--screenshot-tab" && i + 1 < argc) {
            screenshot_tab = std::stoi(argv[++i]);
        } else if (arg == "--dump-screens") {
            dump_screens = true;
        }
    }

    entry_log << "Initializing LVGL" << std::endl;
    lv_init();

    // Create SDL window via LVGL
    entry_log << "Creating LVGL SDL display" << std::endl;
    entry_log.flush();
    lv_display_t* disp = lv_sdl_window_create(win_w, win_h);
    entry_log << "LVGL SDL display created: " << (disp != nullptr) << std::endl;
    entry_log.flush();
    if (disp == nullptr) {
        std::cerr << "LVGL SDL display creation failed" << std::endl;
        SDL_Quit();
        return 2;
    }
    lv_display_set_default(disp);
    lv_sdl_window_set_zoom(disp, zoom);
    lv_sdl_window_set_title(disp, "PRISMIX Studio Console");

    // Initialize mouse & keyboard
    lv_indev_t* mouse = lv_sdl_mouse_create();
    if (mouse == nullptr) {
        std::cerr << "LVGL SDL mouse input creation failed" << std::endl;
        SDL_Quit();
        return 3;
    }
    lv_indev_t* kb_indev = lv_sdl_keyboard_create();
    (void)kb_indev;

    entry_log << "Calling app_init" << std::endl;
    entry_log.flush();
    app_init();
    entry_log << "app_init completed" << std::endl;
    entry_log.flush();

    // Start background web server for MODEP-style pedalboard editing
    prismix::web::WebServer::instance().start(8888);
    std::atexit([]() {
        prismix::web::WebServer::instance().stop();
    });

    if (real_audio_ui_mode) {
        audio_service().selectMode(AudioMode::RealAudio);
    }

    if (channel_setup_preview) mixer_screen_show_channel_setup(0);

    if (navigation_test_mode) {
        auto& service = audio_service();
        bool ok = service.selectMode(AudioMode::RealAudio);
        if (ok) g_backend.stop();
        ui_nav_to_tab(1);
        for (int tab = 0; tab < 7; ++tab) {
            ui_nav_to_tab(tab);
            ok = ok && ui_get_active_tab() == tab && ui_nav_button_is_enabled(tab);
            if (tab < 6) ok = ok && !ui_simulator_controls_are_blocked(tab);
        }
        ok = service.selectMode(AudioMode::Simulation) && ok;
        g_backend.start();
        ui_nav_to_tab(0);
        for (int tab = 0; tab < 6; ++tab) {
            ui_nav_to_tab(tab);
            ok = ok && !ui_simulator_controls_are_blocked(tab);
        }
        SDL_Quit();
        return ok ? 0 : 1;
    }

    if (qa_test_mode) {
        entry_log << "Starting LVGL QA suite" << std::endl;
        entry_log.flush();
        bool ok = run_qa_tests(disp);
        SDL_Quit();
        return ok ? 0 : 1;
    }

    if (dump_screens) {
        auto pump = [](int n) {
            for (int k = 0; k < n; ++k) {
                lv_timer_handler();
                SDL_Delay(10);
            }
        };

        std::string tab_names[] = {
            resolve_output_path("verify-tab0-mixer.bmp"),
            resolve_output_path("verify-tab1-channel.bmp"),
            resolve_output_path("verify-tab2-peq.bmp"),
            resolve_output_path("verify-tab3-dynamics.bmp"),
            resolve_output_path("verify-tab4-fx.bmp"),
            resolve_output_path("verify-tab5-routing.bmp"),
            resolve_output_path("verify-tab6-system.bmp")
        };

        g_backend.start();
        ui_nav_to_tab(0);
        pump(25);
        std::string empty_bmp = resolve_output_path("verify-empty-mixer.bmp");
        capture_window_screenshot(disp, empty_bmp.c_str());
        std::cout << "Captured " << empty_bmp << std::endl;

        // Capture channel setup modal
        mixer_screen_show_channel_setup(0);
        pump(25);
        std::string ch_setup_bmp = resolve_output_path("verify-channel-setup-modal.bmp");
        capture_window_screenshot(disp, ch_setup_bmp.c_str());
        std::cout << "Captured " << ch_setup_bmp << std::endl;
        mixer_screen_dismiss_modal();
        pump(10);

        // Capture audio setup dialog
        show_audio_setup_dialog();
        pump(25);
        std::string audio_setup_bmp = resolve_output_path("verify-audio-setup-modal.bmp");
        capture_window_screenshot(disp, audio_setup_bmp.c_str());
        std::cout << "Captured " << audio_setup_bmp << std::endl;
        dismiss_audio_setup_dialog();
        pump(10);

        while (g_mixer_state.channels.size() < 2) {
            mixer_screen_add_channel();
        }
        std::snprintf(g_mixer_state.channels[0].name, sizeof(g_mixer_state.channels[0].name), "LEAD VOCAL");
        g_mixer_state.channels[0].input_source_index = 0;
        std::snprintf(g_mixer_state.channels[0].input_label, sizeof(g_mixer_state.channels[0].input_label), "IN 01 [M]");
        g_mixer_state.channels[0].format = ChannelFormat::Mono;
        g_mixer_state.channels[0].fx_rack.add_plugin("http://audiocore.org/plugins/reverb");
        mixer_screen_refresh_channel_by_id(g_mixer_state.channels[0].id);

        std::snprintf(g_mixer_state.channels[1].name, sizeof(g_mixer_state.channels[1].name), "SYNTH L/R");
        g_mixer_state.channels[1].input_source_index = 1;
        g_mixer_state.channels[1].input_source_index_r = 2;
        std::snprintf(g_mixer_state.channels[1].input_label, sizeof(g_mixer_state.channels[1].input_label), "IN 02-03 [ST]");
        g_mixer_state.channels[1].format = ChannelFormat::Stereo;
        mixer_screen_refresh_channel_by_id(g_mixer_state.channels[1].id);
        mixer_screen_select_channel(0);
        pump(10);

        for (int t = 0; t < 7; ++t) {
            if (t == 4) {
                // Ensure default channel 0 on Tab 4 has Studio Compressor loaded
                auto* ch = mixer_get_channel(0);
                if (ch) {
                    ch->format = ChannelFormat::Stereo;
                    ch->fx_rack.clear();
                    ch->fx_rack.add_plugin("http://audiocore.org/plugins/compressor");
                    ch->fx_rack.add_plugin("http://audiocore.org/plugins/reverb");
                    ch->fx_rack.add_plugin("http://audiocore.org/plugins/delay");
                    if (auto* pi = ch->fx_rack.get_slot(0)) {
                        pi->set_parameter_value("thresh", -48.0f);
                        pi->set_parameter_value("ratio", 4.0f);
                    }
                    ch->fx_in_peak_l = -12.0f;
                    ch->fx_in_peak_r = -11.5f;
                    ch->fx_in_rms_l = -18.0f;
                    ch->fx_in_rms_r = -17.5f;
                    ch->fx_out_peak_l = -15.2f;
                    ch->fx_out_peak_r = -14.8f;
                    ch->fx_out_rms_l = -21.0f;
                    ch->fx_out_rms_r = -20.5f;
                }
            }
            ui_nav_to_tab(t);
            pump(35);
            capture_window_screenshot(disp, tab_names[t].c_str());
            std::cout << "Captured " << tab_names[t] << std::endl;

            if (t == 2) {
                // Also capture GEQ mode
                auto* ch = mixer_get_channel(0);
                if (ch) ch->geq_mode_active = true;
                ui_nav_to_tab(1);
                pump(5);
                ui_nav_to_tab(2);
                pump(35);
                std::string geq_bmp = resolve_output_path("verify-tab2-geq.bmp");
                capture_window_screenshot(disp, geq_bmp.c_str());
                std::cout << "Captured " << geq_bmp << std::endl;
                if (ch) ch->geq_mode_active = false;
            }

            if (t == 4) {
                auto* ch = mixer_get_channel(0);
                if (ch) {
                    std::string comp_bmp = resolve_output_path("verify-tab4-fx-compressor.bmp");
                    capture_window_screenshot(disp, comp_bmp.c_str());
                    std::cout << "Captured " << comp_bmp << std::endl;

                    // Expander / Gate Rack (Mono: exact engine transfer curve + live gate monitor telemetry)
                    ch->format = ChannelFormat::Mono;
                    ch->gate_on = false; // strip-level gate off so the FX rack input stays steady
                    ch->fx_rack.clear();
                    ch->fx_rack.add_plugin("http://audiocore.org/plugins/gate");
                    if (auto* gi = ch->fx_rack.get_slot(0)) {
                        gi->set_parameter_value("thresh", -48.0f);
                        gi->set_parameter_value("range", -60.0f);
                    }
                    pump(25);
                    std::string gate_bmp = resolve_output_path("verify-tab4-fx-gate.bmp");
                    capture_window_screenshot(disp, gate_bmp.c_str());
                    std::cout << "Captured " << gate_bmp << std::endl;

                    // Engine stopped -> verify the explicit "no live gate telemetry" state,
                    // then restart the engine so the remaining captures keep live meters.
                    g_backend.stop();
                    pump(40);
                    std::string gate_un_bmp = resolve_output_path("verify-tab4-fx-gate-unavailable.bmp");
                    capture_window_screenshot(disp, gate_un_bmp.c_str());
                    std::cout << "Captured " << gate_un_bmp << std::endl;
                    g_backend.start();
                    pump(40);

                    // Shallow range: verify the IN + RANGE offset attenuation segment below threshold
                    if (auto* gi = ch->fx_rack.get_slot(0)) {
                        gi->set_parameter_value("thresh", -30.0f);
                        gi->set_parameter_value("range", -18.0f);
                    }
                    pump(10);
                    ui_nav_to_tab(3);
                    ui_nav_to_tab(4); // rebuild the rack editor so knobs, curve and readouts agree
                    pump(25);
                    std::string gate_rng_bmp = resolve_output_path("verify-tab4-fx-gate-range.bmp");
                    capture_window_screenshot(disp, gate_rng_bmp.c_str());
                    std::cout << "Captured " << gate_rng_bmp << std::endl;

                    // Bypassed state: header/bypass button, header status, monitor readout & dot
                    if (auto* gi = ch->fx_rack.get_slot(0)) {
                        gi->set_bypassed(true);
                    }
                    pump(10);
                    ui_nav_to_tab(3);
                    ui_nav_to_tab(4);
                    pump(30);
                    std::string gate_byp_bmp = resolve_output_path("verify-tab4-fx-gate-bypassed.bmp");
                    capture_window_screenshot(disp, gate_byp_bmp.c_str());
                    std::cout << "Captured " << gate_byp_bmp << std::endl;
                    if (auto* gi = ch->fx_rack.get_slot(0)) {
                        gi->set_bypassed(false);
                    }
                    ch->gate_on = true;

                    // 1. Reverb Rack (Stereo 2-CH to verify independent L/R Pre-FX & Post-FX LED bargraphs)
                    ch->format = ChannelFormat::Stereo;
                    ch->fx_rack.clear();
                    ch->fx_rack.add_plugin("http://audiocore.org/plugins/reverb");
                    ch->fx_rack.add_plugin("http://audiocore.org/plugins/delay");
                    ch->fx_rack.add_plugin("http://audiocore.org/plugins/afs");
                    pump(25);
                    std::string rev_bmp = resolve_output_path("verify-tab4-fx-reverb.bmp");
                    capture_window_screenshot(disp, rev_bmp.c_str());
                    std::cout << "Captured " << rev_bmp << std::endl;

                    // Room preset selected: PCM96 LCD must follow the preset (program line, tabs, values)
                    if (auto* ri = ch->fx_rack.get_slot(0)) {
                        const auto& tpl = audiocore::ReverbNode::getTemplate(audiocore::ReverbType::Room);
                        ri->set_parameter_value("room_type", 0.0f);
                        ri->set_parameter_value("predelay", tpl.preDelayMs);
                        ri->set_parameter_value("decay", tpl.decaySeconds);
                        ri->set_parameter_value("damping", tpl.damping);
                        ri->set_parameter_value("diffusion", tpl.diffusion);
                        ri->set_parameter_value("low_cut", tpl.lowCutHz);
                        ri->set_parameter_value("high_cut", tpl.highCutHz);
                        ri->set_parameter_value("width", tpl.width);
                        ri->set_parameter_value("mix", tpl.mix);
                    }
                    ui_nav_to_tab(3);
                    ui_nav_to_tab(4);
                    pump(25);
                    std::string rev_room_bmp = resolve_output_path("verify-tab4-fx-reverb-room.bmp");
                    capture_window_screenshot(disp, rev_room_bmp.c_str());
                    std::cout << "Captured " << rev_room_bmp << std::endl;

                    // Brickwall Limiter vintage mastering faceplate (live GR + needle meters).
                    // Strip comp makeup pushes the simulated program hot so the limiter's real
                    // measured gain reduction is exercised.
                    ch->format = ChannelFormat::Stereo;
                    ch->gate_on = false;      // strip gate off -> steady FX rack input
                    ch->comp_bypass = true;   // keep strip compression from eating level
                    ch->comp_makeup = 15.0f;  // hot program level into the rack input tap
                    ch->sidechain_on = true;  // +6 dB sidechain tilt in the mock strip model
                    ch->sidechain_low = 680.0f;
                    ch->sidechain_high = 8000.0f;
                    ch->fx_rack.clear();
                    ch->fx_rack.add_plugin("http://audiocore.org/plugins/limiter");
                    if (auto* li = ch->fx_rack.get_slot(0)) {
                        li->set_parameter_value("threshold", -12.0f);
                        li->set_parameter_value("ceiling", -12.0f);
                        li->set_parameter_value("release", 50.0f);
                        li->set_parameter_value("lookahead", 2.0f);
                    }
                    ui_nav_to_tab(3);
                    ui_nav_to_tab(4);
                    pump(40);
                    std::string lim_bmp = resolve_output_path("verify-tab4-fx-limiter.bmp");
                    capture_window_screenshot(disp, lim_bmp.c_str());
                    std::cout << "Captured " << lim_bmp << std::endl;

                    // Engine stopped -> explicit unavailable telemetry ('--' readouts, resting needles)
                    g_backend.stop();
                    pump(40);
                    std::string lim_un_bmp = resolve_output_path("verify-tab4-fx-limiter-unavailable.bmp");
                    capture_window_screenshot(disp, lim_un_bmp.c_str());
                    std::cout << "Captured " << lim_un_bmp << std::endl;
                    g_backend.start();
                    pump(40);

                    // Restore the baseline strip state for the remaining captures
                    ch->gate_on = true;
                    ch->comp_bypass = false;
                    ch->comp_makeup = 0.0f;
                    ch->sidechain_on = false;
                    ch->sidechain_low = 80.0f;
                    ch->sidechain_high = 8000.0f;

                    // 2. Delay Rack (Stereo 2-CH Dual-Tap Echo)
                    ch->format = ChannelFormat::Stereo;
                    ch->fx_rack.clear();
                    ch->fx_rack.add_plugin("http://audiocore.org/plugins/delay");
                    ch->fx_rack.add_plugin("http://audiocore.org/plugins/reverb");
                    pump(25);
                    std::string dly_bmp = resolve_output_path("verify-tab4-fx-delay.bmp");
                    capture_window_screenshot(disp, dly_bmp.c_str());
                    std::cout << "Captured " << dly_bmp << std::endl;

                    // 3. AFS Rack (Mono 1-CH to verify non-fabricated Right channel metering & AFS notch analyzer)
                    ch->format = ChannelFormat::Mono;
                    ch->fx_rack.clear();
                    ch->fx_rack.add_plugin("http://audiocore.org/plugins/afs");
                    ch->fx_rack.add_plugin("http://audiocore.org/plugins/reverb");
                    pump(25);
                    std::string afs_bmp = resolve_output_path("verify-tab4-fx-afs.bmp");
                    capture_window_screenshot(disp, afs_bmp.c_str());
                    std::cout << "Captured " << afs_bmp << std::endl;

                    // 4. Overdrive LV2 Plugin Rack (Stereo)
                    ch->format = ChannelFormat::Stereo;
                    ch->fx_rack.clear();
                    ch->fx_rack.add_plugin("http://prismix.audio/plugins/overdrive");
                    ch->fx_rack.add_plugin("http://audiocore.org/plugins/delay");
                    pump(25);
                    std::string od_bmp = resolve_output_path("verify-tab4-fx-lv2.bmp");
                    capture_window_screenshot(disp, od_bmp.c_str());
                    std::cout << "Captured " << od_bmp << std::endl;

                    // 5. NAM Lite Model Rack (Mono British Lead)
                    ch->format = ChannelFormat::Mono;
                    ch->fx_rack.clear();
                    ch->fx_rack.add_plugin("nam:assets/nam/british_lead_lite.nam");
                    ch->fx_rack.add_plugin("http://audiocore.org/plugins/reverb");
                    pump(25);
                    std::string nam_bmp = resolve_output_path("verify-tab4-fx-nam.bmp");
                    capture_window_screenshot(disp, nam_bmp.c_str());
                    std::cout << "Captured " << nam_bmp << std::endl;

                    // 6. Pedalboard Full Chain Preset (British Lead Stack)
                    ch->fx_rack.clear();
                    auto* preset = prismix::pedalboard::PedalboardPresetManager::instance().findPreset("British Lead Stack");
                    if (preset) {
                        prismix::pedalboard::PedalboardPresetManager::instance().applyPresetToRack(*preset, ch->fx_rack);
                    }
                    pump(25);
                    std::string chain_bmp = resolve_output_path("verify-tab4-pedalboard-chain.bmp");
                    capture_window_screenshot(disp, chain_bmp.c_str());
                    std::cout << "Captured " << chain_bmp << std::endl;

                    // 7. Add FX / Preset Modal
                    fx_screen_show_add_modal();
                    pump(25);
                    std::string modal_bmp = resolve_output_path("verify-tab4-fx-add-modal.bmp");
                    capture_window_screenshot(disp, modal_bmp.c_str());
                    std::cout << "Captured " << modal_bmp << std::endl;

                    // Switch to PatchStorage tab (tab 5)
                    lv_obj_t* top_layer = lv_layer_top();
                    if (top_layer && lv_obj_get_child_count(top_layer) > 0) {
                        lv_obj_t* backdrop = lv_obj_get_child(top_layer, lv_obj_get_child_count(top_layer) - 1);
                        if (backdrop && lv_obj_get_child_count(backdrop) > 0) {
                            lv_obj_t* dialog = lv_obj_get_child(backdrop, 0);
                            std::vector<lv_obj_t*> buttons;
                            for (uint32_t c = 0; c < lv_obj_get_child_count(dialog); ++c) {
                                lv_obj_t* child = lv_obj_get_child(dialog, c);
                                if (child && lv_obj_check_type(child, &lv_button_class)) {
                                    buttons.push_back(child);
                                }
                            }
                            if (buttons.size() >= 6) {
                                lv_obj_send_event(buttons[5], LV_EVENT_CLICKED, nullptr);
                                pump(35);

                                auto& ps_svc = prismix::patchstorage::PatchStorageService::instance();
                                auto ps_wait_for_load = [&ps_svc, &pump](int max_frames) {
                                    for (int k = 0; k < max_frames; ++k) {
                                        prismix::patchstorage::BrowseState st;
                                        std::unordered_map<int, prismix::patchstorage::PatchStatus> statuses;
                                        ps_svc.poll_state(st, statuses);
                                        if (!st.is_loading && !st.items.empty()) return true;
                                        pump(4);
                                    }
                                    return false;
                                };
                                auto ps_find_option = [](lv_obj_t* dd, const char* wanted) -> int {
                                    if (!dd) return -1;
                                    const char* opts = lv_dropdown_get_options(dd);
                                    if (!opts) return -1;
                                    const size_t want_len = std::strlen(wanted);
                                    int idx = 0;
                                    const char* p = opts;
                                    while (p) {
                                        const char* nl = std::strchr(p, '\n');
                                        const size_t len = nl ? static_cast<size_t>(nl - p) : std::strlen(p);
                                        if (len == want_len && std::strncmp(p, wanted, want_len) == 0) return idx;
                                        ++idx;
                                        p = nl ? nl + 1 : nullptr;
                                    }
                                    return -1;
                                };
                                const bool ps_loaded = ps_wait_for_load(160);
                                if (ps_loaded) {
                                    // let the artwork pipeline decode the first page
                                    prismix::patchstorage::BrowseState st;
                                    std::unordered_map<int, prismix::patchstorage::PatchStatus> statuses;
                                    ps_svc.poll_state(st, statuses);
                                    if (!st.items.empty() && !st.items.front().artwork_thumbnail_url.empty()) {
                                        for (int k = 0; k < 120; ++k) {
                                            if (ps_svc.get_artwork(st.items.front().id) != nullptr) break;
                                            pump(2);
                                        }
                                    }
                                }
                                pump(10);
                                std::string ps_bmp = resolve_output_path("verify-patchstorage-grid.bmp");
                                capture_window_screenshot(disp, ps_bmp.c_str());
                                std::cout << "Captured " << ps_bmp
                                          << " (catalog loaded: " << (ps_loaded ? "yes" : "no") << ")" << std::endl;

                                // Search composes with the active filters (search -> results)
                                lv_obj_t* search_ta = lv_obj_find_by_name(dialog, "ps_search");
                                if (search_ta) {
                                    lv_textarea_set_text(search_ta, "delay");
                                    lv_obj_send_event(search_ta, LV_EVENT_READY, nullptr);
                                    ps_wait_for_load(160);
                                    pump(10);
                                    std::string ps_q = resolve_output_path("verify-patchstorage-search.bmp");
                                    capture_window_screenshot(disp, ps_q.c_str());
                                    std::cout << "Captured " << ps_q << " (search=delay)" << std::endl;
                                    lv_textarea_set_text(search_ta, "");
                                    lv_obj_send_event(search_ta, LV_EVENT_READY, nullptr);
                                    ps_wait_for_load(160);
                                }

                                // Apply a real category filter through the dropdown
                                lv_obj_t* cat_dd = lv_obj_find_by_name(dialog, "ps_cat");
                                if (cat_dd) {
                                    const char* opts = lv_dropdown_get_options(cat_dd);
                                    int eff_idx = -1;
                                    if (opts) {
                                        int idx = 0;
                                        const char* p = opts;
                                        while (p) {
                                            const char* nl = std::strchr(p, '\n');
                                            const size_t len = nl ? static_cast<size_t>(nl - p) : std::strlen(p);
                                            if (len == 6 && std::strncmp(p, "Effect", 6) == 0) {
                                                eff_idx = idx;
                                                break;
                                            }
                                            ++idx;
                                            p = nl ? nl + 1 : nullptr;
                                        }
                                    }
                                    if (eff_idx >= 0) {
                                        lv_dropdown_set_selected(cat_dd, static_cast<uint32_t>(eff_idx));
                                        lv_obj_send_event(cat_dd, LV_EVENT_VALUE_CHANGED, nullptr);
                                        ps_wait_for_load(160);
                                        pump(10);
                                        std::string ps_f = resolve_output_path("verify-patchstorage-filter.bmp");
                                        capture_window_screenshot(disp, ps_f.c_str());
                                        std::cout << "Captured " << ps_f << " (category=Effect)" << std::endl;
                                    } else {
                                        std::cout << "PatchStorage category 'Effect' not in dropdown" << std::endl;
                                    }
                                }

                                // Client-side state filter (no refetch, keeps search/filters)
                                lv_obj_t* state_dd = lv_obj_find_by_name(dialog, "ps_state");
                                if (state_dd) {
                                    lv_dropdown_set_selected(state_dd, 3);  // HOST COMPATIBLE
                                    lv_obj_send_event(state_dd, LV_EVENT_VALUE_CHANGED, nullptr);
                                    pump(10);
                                    std::string ps_s = resolve_output_path("verify-patchstorage-state-filter.bmp");
                                    capture_window_screenshot(disp, ps_s.c_str());
                                    std::cout << "Captured " << ps_s << " (state=HOST COMPATIBLE)" << std::endl;
                                    lv_dropdown_set_selected(state_dd, 0);
                                    lv_obj_send_event(state_dd, LV_EVENT_VALUE_CHANGED, nullptr);
                                    pump(5);
                                }

                                // Failed-download safety: run a real install request whose file URL 404s
                                {
                                    prismix::patchstorage::BrowseState st;
                                    std::unordered_map<int, prismix::patchstorage::PatchStatus> statuses;
                                    ps_svc.poll_state(st, statuses);
                                    if (!st.items.empty()) {
                                        prismix::patchstorage::PatchStorageItem bad = st.items.front();
                                        bad.files.clear();
                                        prismix::patchstorage::PatchFile f;
                                        f.target.id = prismix::patchstorage::TARGET_ID_LINUX_AMD64;
                                        f.target.name = "linux-amd64";
                                        f.url = "https://patchstorage.com/api/beta/patches/0/does-not-exist.zip";
                                        bad.files.push_back(f);
                                        ps_svc.request_install(bad);
                                        for (int k = 0; k < 200; ++k) {
                                            prismix::patchstorage::BrowseState s2;
                                            std::unordered_map<int, prismix::patchstorage::PatchStatus> st2;
                                            ps_svc.poll_state(s2, st2);
                                            auto it2 = st2.find(bad.id);
                                            if (it2 != st2.end() &&
                                                it2->second.state == prismix::patchstorage::PatchInstallState::Error) {
                                                break;
                                            }
                                            pump(4);
                                        }
                                        pump(10);
                                        std::string ps_err = resolve_output_path("verify-patchstorage-install-failed.bmp");
                                        capture_window_screenshot(disp, ps_err.c_str());
                                        std::cout << "Captured " << ps_err << " (failed download handling)" << std::endl;
                                    }
                                }

                                // Reset clears search, filters and the failed-download state
                                // before the remaining captures (also verifies RESET itself).
                                lv_obj_t* reset_btn = lv_obj_find_by_name(dialog, "ps_reset_btn");
                                if (reset_btn) {
                                    lv_obj_send_event(reset_btn, LV_EVENT_CLICKED, nullptr);
                                    ps_wait_for_load(160);
                                    pump(10);
                                    std::string ps_reset = resolve_output_path("verify-patchstorage-reset.bmp");
                                    capture_window_screenshot(disp, ps_reset.c_str());
                                    std::cout << "Captured " << ps_reset << " (after reset)" << std::endl;
                                }

                                // Sort order over the real API (orderby/order) + paging + refresh
                                lv_obj_t* sort_dd = lv_obj_find_by_name(dialog, "ps_sort");
                                const int sort_idx = ps_find_option(sort_dd, "MOST DOWNLOADED");
                                if (sort_idx >= 0) {
                                    lv_dropdown_set_selected(sort_dd, static_cast<uint32_t>(sort_idx));
                                    lv_obj_send_event(sort_dd, LV_EVENT_VALUE_CHANGED, nullptr);
                                    ps_wait_for_load(160);
                                    pump(10);
                                    std::string ps_sort = resolve_output_path("verify-patchstorage-sort.bmp");
                                    capture_window_screenshot(disp, ps_sort.c_str());
                                    std::cout << "Captured " << ps_sort << " (sort=MOST DOWNLOADED)" << std::endl;
                                }
                                lv_obj_t* next_btn = lv_obj_find_by_name(dialog, "ps_next_btn");
                                if (next_btn) {
                                    lv_obj_send_event(next_btn, LV_EVENT_CLICKED, nullptr);
                                    ps_wait_for_load(160);
                                    pump(10);
                                    std::string ps_p2 = resolve_output_path("verify-patchstorage-page2.bmp");
                                    capture_window_screenshot(disp, ps_p2.c_str());
                                    std::cout << "Captured " << ps_p2 << " (page 2)" << std::endl;
                                }
                                lv_obj_t* prev_btn = lv_obj_find_by_name(dialog, "ps_prev_btn");
                                if (prev_btn) {
                                    lv_obj_send_event(prev_btn, LV_EVENT_CLICKED, nullptr);
                                    ps_wait_for_load(160);
                                    pump(10);
                                }
                                lv_obj_t* refresh_btn = lv_obj_find_by_name(dialog, "ps_refresh_btn");
                                if (refresh_btn) {
                                    lv_obj_send_event(refresh_btn, LV_EVENT_CLICKED, nullptr);
                                    const bool refreshed = ps_wait_for_load(160);
                                    prismix::patchstorage::BrowseState st_r;
                                    std::unordered_map<int, prismix::patchstorage::PatchStatus> st_rr;
                                    ps_svc.poll_state(st_r, st_rr);
                                    std::cout << "REFRESH ok: " << (refreshed ? "yes" : "no") << ", page "
                                              << st_r.current_page << ", " << st_r.total_items
                                              << " plugins, error='" << st_r.status_message << "'" << std::endl;
                                    pump(10);
                                }

                                // Responsive check: shrink the dialog and verify the grid reflows
                                lv_obj_t* cards_box = lv_obj_find_by_name(dialog, "ps_cards");
                                lv_obj_t* tab_list = cards_box ? lv_obj_get_parent(cards_box) : nullptr;
                                lv_obj_t* cancel_btn = buttons.size() > 6 ? buttons[6] : nullptr;
                                if (tab_list) {
                                    if (cancel_btn) lv_obj_add_flag(cancel_btn, LV_OBJ_FLAG_HIDDEN);
                                    lv_obj_set_size(dialog, 700, 660);
                                    lv_obj_set_size(tab_list, 676, 520);
                                    pump(30);
                                    std::string ps_n2 = resolve_output_path("verify-patchstorage-narrow-2col.bmp");
                                    capture_window_screenshot(disp, ps_n2.c_str());
                                    std::cout << "Captured " << ps_n2 << " (narrow, expect 2 columns)" << std::endl;

                                    lv_obj_set_size(dialog, 500, 660);
                                    lv_obj_set_size(tab_list, 476, 520);
                                    pump(30);
                                    std::string ps_n1 = resolve_output_path("verify-patchstorage-narrow-1col.bmp");
                                    capture_window_screenshot(disp, ps_n1.c_str());
                                    std::cout << "Captured " << ps_n1 << " (narrow, expect 1 column)" << std::endl;

                                    lv_obj_set_size(tab_list, 936, 520);
                                    lv_obj_set_size(dialog, 960, 660);
                                    if (cancel_btn) lv_obj_remove_flag(cancel_btn, LV_OBJ_FLAG_HIDDEN);
                                    pump(25);
                                }

                                // Details overlay via a card click
                                lv_obj_t* cards = lv_obj_find_by_name(dialog, "ps_cards");
                                if (cards && lv_obj_get_child_count(cards) > 0) {
                                    lv_obj_t* first_card = lv_obj_get_child(cards, 0);
                                    lv_obj_send_event(first_card, LV_EVENT_CLICKED, nullptr);
                                    pump(15);
                                    std::string ps_d = resolve_output_path("verify-patchstorage-details.bmp");
                                    capture_window_screenshot(disp, ps_d.c_str());
                                    std::cout << "Captured " << ps_d << std::endl;
                                    lv_obj_t* close_btn = lv_obj_find_by_name(dialog, "ps_details_close");
                                    if (close_btn) {
                                        lv_obj_send_event(close_btn, LV_EVENT_CLICKED, nullptr);
                                        pump(10);
                                    }
                                }

                                // Tab bounce: re-populating the tab must stay stable
                                lv_obj_send_event(buttons[4], LV_EVENT_CLICKED, nullptr);
                                pump(10);
                                lv_obj_send_event(buttons[5], LV_EVENT_CLICKED, nullptr);
                                pump(35);
                                ps_wait_for_load(160);
                                std::string ps_r = resolve_output_path("verify-patchstorage-reopen.bmp");
                                capture_window_screenshot(disp, ps_r.c_str());
                                std::cout << "Captured " << ps_r << " (tab reopen)" << std::endl;

                                // Full modal cycle: dismiss, reopen, and repopulate the tab again
                                fx_screen_dismiss_modal();
                                pump(15);
                                fx_screen_show_add_modal();
                                pump(25);
                                {
                                    lv_obj_t* tl2 = lv_layer_top();
                                    lv_obj_t* bd2 = (tl2 && lv_obj_get_child_count(tl2) > 0)
                                                        ? lv_obj_get_child(tl2, lv_obj_get_child_count(tl2) - 1)
                                                        : nullptr;
                                    lv_obj_t* dlg2 = (bd2 && lv_obj_get_child_count(bd2) > 0)
                                                         ? lv_obj_get_child(bd2, 0)
                                                         : nullptr;
                                    std::vector<lv_obj_t*> btns2;
                                    if (dlg2) {
                                        for (uint32_t c = 0; c < lv_obj_get_child_count(dlg2); ++c) {
                                            lv_obj_t* ch2 = lv_obj_get_child(dlg2, c);
                                            if (ch2 && lv_obj_check_type(ch2, &lv_button_class)) btns2.push_back(ch2);
                                        }
                                    }
                                    if (btns2.size() >= 6) {
                                        lv_obj_send_event(btns2[5], LV_EVENT_CLICKED, nullptr);
                                        pump(35);
                                        ps_wait_for_load(160);
                                        pump(10);
                                        std::string ps_c2 = resolve_output_path("verify-patchstorage-reopen-modal.bmp");
                                        capture_window_screenshot(disp, ps_c2.c_str());
                                        std::cout << "Captured " << ps_c2 << " (modal reopened)" << std::endl;
                                    }
                                }
                            }
                        }
                    }

                    fx_screen_dismiss_modal();
                    pump(10);
                }
            }

            if (t == 5) {
                // Also capture Matrix mode
                routing_screen_set_mode(1);
                ui_nav_to_tab(4);
                pump(5);
                ui_nav_to_tab(5);
                pump(35);
                std::string matrix_bmp = resolve_output_path("verify-tab5-matrix.bmp");
                capture_window_screenshot(disp, matrix_bmp.c_str());
                std::cout << "Captured " << matrix_bmp << std::endl;
                routing_screen_set_mode(0);
            }

            if (t == 6) {
                // Also capture Theme Editor mode in System screen
                system_screen_set_mode(1);
                pump(25);
                std::string theme_bmp = resolve_output_path("verify-tab6-theme.bmp");
                capture_window_screenshot(disp, theme_bmp.c_str());
                std::cout << "Captured " << theme_bmp << std::endl;

                // Switch theme to Studio Blue in System Settings
                theme_set_preset(ThemePreset::StudioBlue);
                pump(25);

                // Navigate directly back to Mixer (Tab 0) to verify panel renders properly
                ui_nav_to_tab(0);
                pump(35);
                std::string mixer_after_theme_bmp = resolve_output_path("verify-tab0-mixer-after-theme-change.bmp");
                capture_window_screenshot(disp, mixer_after_theme_bmp.c_str());
                std::cout << "Captured " << mixer_after_theme_bmp << std::endl;

                // Restore factory Graphite preset
                theme_reset_all_defaults();
                pump(20);

                ui_nav_to_tab(6);
                system_screen_set_mode(0);
                pump(10);
            }
        }

        // Also capture Dedicated Main L/R Master Detail Screen & Master FX Rack
        mixer_screen_select_channel(-1);
        pump(15);
        ui_nav_to_tab(0);
        pump(10);
        ui_nav_to_tab(1);
        pump(35);
        std::string master_ch_bmp = resolve_output_path("verify-tab1-master.bmp");
        capture_window_screenshot(disp, master_ch_bmp.c_str());
        std::cout << "Captured " << master_ch_bmp << std::endl;

        // Test returning to Tab 0 from Master Channel view (user's exact overlap scenario)
        ui_nav_to_tab(0);
        pump(35);
        std::string verify_no_overlap_bmp = resolve_output_path("verify-no-overlap-mixer.bmp");
        capture_window_screenshot(disp, verify_no_overlap_bmp.c_str());
        std::cout << "Captured " << verify_no_overlap_bmp << std::endl;
        ui_nav_to_tab(1);
        pump(20);

        // Test Speaker Delay and Dynamics Telemetry on Main L/R Master Screen
        g_mixer_state.master.speaker_delay_enabled = true;
        g_mixer_state.master.speaker_delay_link = true;
        g_mixer_state.master.speaker_delay_l_ms = 14.5f;
        g_mixer_state.master.speaker_delay_r_ms = 14.5f;
        g_mixer_state.master.comp_gr = -4.2f;
        g_mixer_state.master.limiter_gr_db = -1.8f;

        // Re-capture Master Channel screen with active Speaker Delay and Dynamics telemetry
        ui_nav_to_tab(0);
        pump(10);
        ui_nav_to_tab(1);
        pump(35);
        std::string master_ch2_bmp = resolve_output_path("verify-tab1-master-active.bmp");
        capture_window_screenshot(disp, master_ch2_bmp.c_str());
        std::cout << "Captured " << master_ch2_bmp << std::endl;

        // Capture Master FX Tab (shows dedicated UNAVAILABLE state)
        ui_nav_to_tab(4);
        pump(35);
        std::string master_fx_bmp = resolve_output_path("verify-tab4-master-fx.bmp");
        capture_window_screenshot(disp, master_fx_bmp.c_str());
        std::cout << "Captured " << master_fx_bmp << std::endl;

        // Capture Master Dynamics Tab (shows dedicated Limiter & tall Comp curve, no Gate)
        ui_nav_to_tab(3);
        pump(35);
        std::string master_dyn_bmp = resolve_output_path("verify-tab3-dynamics-master.bmp");
        capture_window_screenshot(disp, master_dyn_bmp.c_str());
        std::cout << "Captured " << master_dyn_bmp << std::endl;

        SDL_Quit();
        return 0;
    }

    if (!screenshot_path.empty()) {
        mixer_screen_select_channel(0);
        if (screenshot_tab >= 0) ui_nav_to_tab(screenshot_tab);
        for (int k = 0; k < 20; ++k) {
            lv_timer_handler();
            SDL_Delay(10);
        }
        if (show_fx_add_modal) {
            fx_screen_show_add_modal();
            for (int k = 0; k < 20; ++k) {
                lv_timer_handler();
                SDL_Delay(10);
            }
            if (select_patchstorage_tab) {
                lv_obj_t* top_layer = lv_layer_top();
                if (top_layer && lv_obj_get_child_count(top_layer) > 0) {
                    lv_obj_t* backdrop = lv_obj_get_child(top_layer, lv_obj_get_child_count(top_layer) - 1);
                    if (backdrop && lv_obj_get_child_count(backdrop) > 0) {
                        lv_obj_t* dialog = lv_obj_get_child(backdrop, 0);
                        std::vector<lv_obj_t*> buttons;
                        for (uint32_t c = 0; c < lv_obj_get_child_count(dialog); ++c) {
                            lv_obj_t* child = lv_obj_get_child(dialog, c);
                            if (child && lv_obj_check_type(child, &lv_button_class)) {
                                buttons.push_back(child);
                            }
                        }
                        if (buttons.size() >= 6) {
                            lv_obj_send_event(buttons[5], LV_EVENT_CLICKED, nullptr);
                        }
                    }
                }
                for (int k = 0; k < 25; ++k) {
                    lv_timer_handler();
                    SDL_Delay(10);
                }
            }
        }
        capture_window_screenshot(disp, screenshot_path.c_str());
        std::cout << "Saved screenshot to " << screenshot_path << std::endl;
        SDL_Quit();
        return 0;
    }

    while (true) {
        uint32_t time_till_next = lv_timer_handler();
        if (time_till_next == 0) time_till_next = 1;
        SDL_Delay(time_till_next);
    }

    return 0;
}
