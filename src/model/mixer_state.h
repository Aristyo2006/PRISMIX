#pragma once
#include <array>
#include <cstdint>

constexpr int NUM_CHANNELS = 8;

struct ChannelState {
    float fader = 0.86f; // 0..1, logarithmic fader law; 0 dB at 0.86
    float pan = 0.5f;    // 0..1, center at 0.5
    float gain = 0.5f;
    bool mute = false, solo = false, select = false, clip = false;
    float peak_l = -30.0f, peak_r = -32.0f, rms_l = -38.0f, rms_r = -40.0f;
    float peak_hold_l = -30.0f, peak_hold_r = -32.0f;
    const char* name = "INPUT";
    const char* input_label = "IN 1";
    std::array<float, 4> eq_gain{0.0f, 0.0f, 0.0f, 0.0f};
    std::array<float, 4> eq_freq{80.0f, 350.0f, 2500.0f, 10000.0f};
    std::array<float, 4> eq_q{0.7f, 1.0f, 1.0f, 0.7f};
    float comp_thresh = -18.0f, comp_ratio = 4.0f;
    float comp_attack = 10.0f, comp_release = 80.0f, comp_knee = 6.0f, comp_makeup = 0.0f;
    float comp_gr = 0.0f;
    bool comp_bypass = false, gate_on = true, gate_open = true;
    float gate_thresh = -48.0f, gate_attack = 5.0f, gate_release = 120.0f, gate_range = -60.0f;
    float gate_timer_ms = 0.0f;
    float comp_input = -24.0f, comp_output = -24.0f;
    bool eq_bypass = false, polarity_inverted = false, hpf_on = true, phantom_on = false;
    std::array<bool, 4> eq_enabled{true, true, true, true};
    std::array<bool, 4> eq_shelf{true, false, false, true};
    std::array<bool, 8> bus_assign{};
    std::array<float, 4> sends{0.25f,0.20f,0.15f,0.10f};
    float sidechain_low = 80.0f, sidechain_high = 8000.0f;
    bool sidechain_on = false;
};

struct MixerState {
    std::array<ChannelState, NUM_CHANNELS> channels{};
    ChannelState master{};
    int selected_channel = 0;
};

extern MixerState g_mixer_state;
void mixer_state_init();
