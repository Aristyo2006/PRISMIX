#include "mock_audio_backend.h"
#include "../model/mixer_state.h"
#include "../dsp/compressor_model.h"
#include "../dsp/gate_model.h"
#include <algorithm>
#include <cmath>

MockAudioBackend g_backend;

void MockAudioBackend::init() {
    mixer_state_init();
}

void MockAudioBackend::update() {
    static float phase = 0.0f;
    phase += 0.055f;
    float sum = 0.0f;
    for (int i = 0; i < NUM_CHANNELS; ++i) {
        auto& ch = g_mixer_state.channels[i];
        const float wave = 0.5f + 0.5f * std::sin(phase * (0.81f + i * 0.113f) + i * 1.7f);
        const float transient = 0.5f + 0.5f * std::sin(phase * (1.37f + i * .071f));
        const float input = -54.0f + wave * 31.0f + transient * 8.0f;
        const float sc_tilt=ch.sidechain_on ? std::clamp((ch.sidechain_low-80.0f)/100.0f,-6.0f,6.0f)-std::clamp((ch.sidechain_high-8000.0f)/3000.0f,-6.0f,6.0f) : 0.0f;
        const float detector=input+(ch.fader-.86f)*50.0f+sc_tilt;
        const float reduction=std::min(24.0f,compressor_gain_reduction_db(detector,ch.comp_thresh,ch.comp_ratio,ch.comp_knee,!ch.comp_bypass));
        ch.comp_gr=compressor_envelope(ch.comp_gr,-reduction,ch.comp_attack,ch.comp_release,50.0f);
        ch.comp_input=detector; float level=detector+ch.comp_gr+ch.comp_makeup; ch.comp_output=level;
        update_gate_state(ch,input,50.0f); level=gate_output_db(level,ch);
        if (ch.mute) level = -90.0f;
        ch.peak_l = std::clamp(level + .8f * std::sin(phase * 1.91f + i), -90.0f, 3.0f);
        ch.peak_r = std::clamp(level - .7f * std::sin(phase * 1.63f + i), -90.0f, 3.0f);
        ch.rms_l += ((ch.peak_l - 5.0f) - ch.rms_l) * .16f;
        ch.rms_r += ((ch.peak_r - 5.0f) - ch.rms_r) * .16f;
        ch.clip = ch.peak_l >= 0.0f || ch.peak_r >= 0.0f;
        ch.peak_hold_l=std::max(ch.peak_l,ch.peak_hold_l-.35f);
        ch.peak_hold_r=std::max(ch.peak_r,ch.peak_hold_r-.35f);
        sum += std::max(ch.peak_l, ch.peak_r);
    }
    auto& main = g_mixer_state.master;
    const float main_level = sum / NUM_CHANNELS - 6.0f + std::sin(phase * .47f) * 1.4f;
    main.peak_l = std::clamp(main_level, -90.0f, 3.0f);
    main.peak_r = std::clamp(main_level - std::sin(phase * .59f), -90.0f, 3.0f);
    main.rms_l += ((main.peak_l - 4.5f) - main.rms_l) * .12f;
    main.rms_r += ((main.peak_r - 4.5f) - main.rms_r) * .12f;
    main.peak_hold_l=std::max(main.peak_l,main.peak_hold_l-.35f);
    main.peak_hold_r=std::max(main.peak_r,main.peak_hold_r-.35f);
    main.clip = main.peak_l >= 0.0f || main.peak_r >= 0.0f;
}
