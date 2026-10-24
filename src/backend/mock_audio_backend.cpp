#include "mock_audio_backend.h"
#include "../model/mixer_state.h"
#include "../dsp/compressor_model.h"
#include "../dsp/gate_model.h"
#include "../dsp/eq_model.h"
#include "../service/audio_service.h"
#include "../ui/ui_audio_bridge.h"
#include "audiocore/audio_buffer.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_map>

MockAudioBackend g_backend;

namespace {
std::unordered_map<int, std::unique_ptr<StripInsertDspNode>> g_simFxNodes;
double g_simSampleCounter = 0.0;

float simToDb(float amp) {
    return amp <= 1.0e-6f ? -90.0f : std::clamp(20.0f * std::log10(amp), -90.0f, 6.0f);
}

void clearChannelMeters(ChannelState& ch) {
    ch.input_peak_l = ch.input_peak_r = ch.input_rms_l = ch.input_rms_r = -90.0f;
    ch.input_peak_hold_l = ch.input_peak_hold_r = -90.0f;
    ch.input_clip = false;
    ch.fx_in_peak_l = ch.fx_in_peak_r = ch.fx_in_rms_l = ch.fx_in_rms_r = -90.0f;
    ch.fx_in_peak_hold_l = ch.fx_in_peak_hold_r = -90.0f;
    ch.fx_in_clip = false;
    ch.fx_out_peak_l = ch.fx_out_peak_r = ch.fx_out_rms_l = ch.fx_out_rms_r = -90.0f;
    ch.fx_out_peak_hold_l = ch.fx_out_peak_hold_r = -90.0f;
    ch.fx_out_clip = false;
    ch.peak_l = ch.peak_r = ch.rms_l = ch.rms_r = -90.0f;
    ch.peak_hold_l = ch.peak_hold_r = -90.0f;
    ch.post_fader_peak_l = ch.post_fader_peak_r = -90.0f;
    ch.pre_limiter_peak_l = ch.pre_limiter_peak_r = -90.0f;
    ch.comp_gr = 0.0f;
    ch.clip = false;
    // No engine is measuring -> stamp FX rack slot telemetry as explicitly unavailable
    // (the Gate UI shows its "no telemetry" state instead of stale or invented values)
    ch.fx_slot_gr_db = {};
    ch.fx_slot_gate_gr_db = {};
    ch.fx_slot_gate_detector_db = {};
    ch.fx_slot_gate_state = {
        GATE_STATE_UNAVAILABLE, GATE_STATE_UNAVAILABLE, GATE_STATE_UNAVAILABLE, GATE_STATE_UNAVAILABLE,
        GATE_STATE_UNAVAILABLE, GATE_STATE_UNAVAILABLE, GATE_STATE_UNAVAILABLE, GATE_STATE_UNAVAILABLE};
    ch.fx_slot_limiter_gr_db = {
        LIMITER_GR_UNAVAILABLE, LIMITER_GR_UNAVAILABLE, LIMITER_GR_UNAVAILABLE, LIMITER_GR_UNAVAILABLE,
        LIMITER_GR_UNAVAILABLE, LIMITER_GR_UNAVAILABLE, LIMITER_GR_UNAVAILABLE, LIMITER_GR_UNAVAILABLE};
}

void runRealFxRackPcm(ChannelState& ch, float preFxDbL, float preFxDbR,
                      GeneratorSignal sigType, float sweepFreq, int seedOffset) {
    constexpr uint32_t kFrames = 128;
    constexpr double kFs = 48000.0;

    AudioStripConfig cfg = ui_make_strip_config_for_channel(ch);
    cfg.logicalId = static_cast<uint32_t>(ch.id);
    // Disable pre-FX strip dynamics/EQ inside the FX-only node because preFxDbL/R
    // already represents the signal arriving at the FX Rack input tap.
    cfg.polarityInverted = false;
    cfg.hpfEnabled = false;
    cfg.gateEnabled = false;
    cfg.peqBypassed = true;
    cfg.geqBypassed = true;
    cfg.compressorEnabled = false;
    cfg.limiterEnabled = false;

    auto it = g_simFxNodes.find(ch.id);
    if (it == g_simFxNodes.end()) {
        std::vector<AudioStripConfig> single{cfg};
        it = g_simFxNodes.emplace(ch.id, std::make_unique<StripInsertDspNode>(single, kFs, kFrames)).first;
    } else {
        it->second->updateStripDsp(cfg);
    }

    audiocore::AudioBuffer inBuf(2, kFrames), outBuf(2, kFrames);
    inBuf.clear(kFrames);
    outBuf.clear(kFrames);

    const bool isStereo = (ch.format == ChannelFormat::Stereo);
    const float ampL = (preFxDbL > -89.0f) ? std::pow(10.0f, std::clamp(preFxDbL, -89.0f, 6.0f) / 20.0f) : 0.0f;
    const float ampR = (isStereo && preFxDbR > -89.0f) ? std::pow(10.0f, std::clamp(preFxDbR, -89.0f, 6.0f) / 20.0f) : 0.0f;

    if (ampL > 0.0f || ampR > 0.0f) {
        const double baseFrame = g_simSampleCounter + seedOffset * 173.0;
        for (uint32_t n = 0; n < kFrames; ++n) {
            const double t = (baseFrame + n) / kFs;
            float unitL = 0.0f;
            float unitR = 0.0f;
            switch (sigType) {
                case GeneratorSignal::SINE_1KHZ:
                    unitL = static_cast<float>(std::sin(2.0 * M_PI * 1000.0 * t));
                    unitR = static_cast<float>(std::sin(2.0 * M_PI * 1000.0 * t + 0.35));
                    break;
                case GeneratorSignal::MULTI_TONE:
                    unitL = static_cast<float>(0.55 * std::sin(2.0 * M_PI * 1000.0 * t) +
                                               0.30 * std::sin(2.0 * M_PI * 3000.0 * t) +
                                               0.15 * std::sin(2.0 * M_PI * 10000.0 * t));
                    unitR = static_cast<float>(0.55 * std::sin(2.0 * M_PI * 1000.0 * t + 0.25) +
                                               0.30 * std::sin(2.0 * M_PI * 3000.0 * t + 0.50) +
                                               0.15 * std::sin(2.0 * M_PI * 10000.0 * t + 0.75));
                    break;
                case GeneratorSignal::SWEEP_LOG:
                    unitL = static_cast<float>(std::sin(2.0 * M_PI * sweepFreq * t));
                    unitR = static_cast<float>(std::sin(2.0 * M_PI * sweepFreq * t + 0.2));
                    break;
                case GeneratorSignal::WHITE_NOISE:
                case GeneratorSignal::PINK_NOISE:
                default:
                    unitL = static_cast<float>(0.50 * std::sin(2.0 * M_PI * 220.0 * t) +
                                               0.30 * std::sin(2.0 * M_PI * 880.0 * t + 0.7) +
                                               0.20 * std::sin(2.0 * M_PI * 3520.0 * t + 1.3));
                    unitR = static_cast<float>(0.50 * std::sin(2.0 * M_PI * 220.0 * t + 0.4) +
                                               0.30 * std::sin(2.0 * M_PI * 880.0 * t + 1.1) +
                                               0.20 * std::sin(2.0 * M_PI * 3520.0 * t + 1.9));
                    break;
            }
            inBuf[0][n] = ampL * unitL;
            inBuf[1][n] = ampR * unitR;
        }
    }

    it->second->process(inBuf, outBuf, kFrames);
    const auto telVec = it->second->telemetry();
    if (!telVec.empty()) {
        const auto& tel = telVec[0];
        const bool telStereo = (tel.channelCount >= 2);
        ch.fx_in_peak_l = simToDb(tel.fxInputPeak[0]);
        ch.fx_in_peak_r = telStereo ? simToDb(tel.fxInputPeak[1]) : -90.0f;
        ch.fx_in_rms_l = simToDb(tel.fxInputRms[0]);
        ch.fx_in_rms_r = telStereo ? simToDb(tel.fxInputRms[1]) : -90.0f;
        ch.fx_in_peak_hold_l = std::max(ch.fx_in_peak_l, ch.fx_in_peak_hold_l - 0.35f);
        ch.fx_in_peak_hold_r = telStereo ? std::max(ch.fx_in_peak_r, ch.fx_in_peak_hold_r - 0.35f) : -90.0f;
        ch.fx_in_clip = ch.fx_in_peak_l >= 0.0f || ch.fx_in_peak_r >= 0.0f;

        ch.fx_out_peak_l = simToDb(tel.fxOutputPeak[0]);
        ch.fx_out_peak_r = telStereo ? simToDb(tel.fxOutputPeak[1]) : -90.0f;
        ch.fx_out_rms_l = simToDb(tel.fxOutputRms[0]);
        ch.fx_out_rms_r = telStereo ? simToDb(tel.fxOutputRms[1]) : -90.0f;
        ch.fx_out_peak_hold_l = std::max(ch.fx_out_peak_l, ch.fx_out_peak_hold_l - 0.35f);
        ch.fx_out_peak_hold_r = telStereo ? std::max(ch.fx_out_peak_r, ch.fx_out_peak_hold_r - 0.35f) : -90.0f;
        ch.fx_out_clip = ch.fx_out_peak_l >= 0.0f || ch.fx_out_peak_r >= 0.0f;

        ch.afs_assigned = tel.afsAssigned;
        ch.afs_status = tel.afsState;
        ch.afs_active_notch_count = tel.afsActiveNotchCount;
        ch.afs_max_notches = tel.afsMaxNotches;
        for (size_t k = 0; k < 8; ++k) {
            ch.afs_notches[k].id = tel.afsNotches[k].id;
            ch.afs_notches[k].frequency_hz = tel.afsNotches[k].frequencyHz;
            ch.afs_notches[k].depth_db = tel.afsNotches[k].depthDb;
            ch.afs_notches[k].q = tel.afsNotches[k].q;
            ch.afs_notches[k].state = tel.afsNotches[k].state;
        }
        ch.fx_slot_gr_db = tel.fxSlotGrDb;
        ch.fx_slot_gate_gr_db = tel.fxSlotGateGrDb;
        ch.fx_slot_gate_detector_db = tel.fxSlotGateDetectorDb;
        ch.fx_slot_gate_state = tel.fxSlotGateState;
        ch.fx_slot_limiter_gr_db = tel.fxSlotLimiterGrDb;
    }
}
} // namespace

MockAudioBackend::MockAudioBackend() {
    config_.backend_type = BackendType::MOCK;
    config_.input_device = "SDL2 Simulated Mic In (Stereo)";
    config_.output_device = "SDL2 Simulated Speakers / DirectSound";
    config_.sample_rate = SampleRate::SR_48000;
    config_.bit_depth = BitDepth::BIT_24;
    config_.buffer_size = 256;
    config_.is_active = true;
    config_.cpu_load = 12.0f;
    config_.xruns = 0;
}

bool MockAudioBackend::initialize(const AudioDeviceConfig& config) {
    config_ = config;
    return true;
}

bool MockAudioBackend::start() {
    running_ = true;
    config_.is_active = true;
    return true;
}

void MockAudioBackend::stop() {
    running_ = false;
    config_.is_active = false;
    for (auto& ch : g_mixer_state.channels) clearChannelMeters(ch);
    for (auto& aux : g_mixer_state.aux_buses) clearChannelMeters(aux);
    clearChannelMeters(g_mixer_state.master);
    for (auto& kv : g_simFxNodes) {
        if (kv.second) kv.second->reset();
    }
}

std::vector<std::string> MockAudioBackend::enumerate_input_devices() {
    return {
        "SDL2 Simulated Mic In (Stereo)",
        "Windows WASAPI Default Capture",
        "Virtual Audio Cable (Line 1)",
        "Mock Multichannel In (8 Channels)",
        "Internal Generator (Pink / Sine / Sweep)"
    };
}

std::vector<std::string> MockAudioBackend::enumerate_output_devices() {
    return {
        "SDL2 Simulated Speakers / DirectSound",
        "Windows WASAPI Default Playback",
        "Realtek High Definition Audio",
        "Headphones (Realtek Audio)",
        "Virtual Audio Cable (Line 1)"
    };
}

void MockAudioBackend::update_telemetry() {
    config_.cpu_load = 11.5f + 1.5f * std::sin(phase_ * 0.2f);
}

const char* MockAudioBackend::get_generator_signal_name() const {
    switch (signal_type_) {
        case GeneratorSignal::SINE_1KHZ: return "1 kHz SINE WAVE";
        case GeneratorSignal::PINK_NOISE: return "PINK NOISE (-3 dB/oct)";
        case GeneratorSignal::WHITE_NOISE: return "WHITE NOISE (FLAT)";
        case GeneratorSignal::MULTI_TONE: return "MULTI-TONE (1k/3k/10k)";
        case GeneratorSignal::SWEEP_LOG: return "LOG SWEEP (20Hz-20kHz)";
        default: return "PINK NOISE";
    }
}

void MockAudioBackend::init() {
    mixer_state_init();
    g_simFxNodes.clear();
    analyzer_.clear_peaks();
}

void MockAudioBackend::update() {
    if (!running_) return;

    phase_ += 0.055f;
    g_simSampleCounter += 128.0;
    sweep_freq_ = 20.0f * std::pow(1000.0f, std::fmod(phase_ * 0.05f, 1.0f));

    const int num_ch = (int)g_mixer_state.channels.size();
    float sum = 0.0f;

    for (int i = 0; i < num_ch; ++i) {
        auto& ch = g_mixer_state.channels[i];

        if (ch.input_source_index < 0) {
            clearChannelMeters(ch);
            runRealFxRackPcm(ch, -90.0f, -90.0f, signal_type_, sweep_freq_, i);
            continue;
        }

        // Base signal synthesis based on selected generator signal
        float wave = 0.0f;
        switch (signal_type_) {
            case GeneratorSignal::SINE_1KHZ:
                wave = 0.5f + 0.5f * std::sin(phase_ * 3.14159f * 1.5f + i * 0.5f);
                break;
            case GeneratorSignal::PINK_NOISE:
                wave = 0.5f + 0.25f * std::sin(phase_ * 0.81f + i * 1.7f)
                            + 0.25f * std::sin(phase_ * 1.37f + i * 0.071f);
                break;
            case GeneratorSignal::WHITE_NOISE:
                wave = 0.5f + 0.35f * std::sin(phase_ * 2.8f + i * 2.1f)
                            + 0.15f * std::sin(phase_ * 4.9f + i * 1.1f);
                break;
            case GeneratorSignal::MULTI_TONE:
                wave = 0.5f + 0.2f * std::sin(phase_ * 1.0f + i)
                            + 0.15f * std::sin(phase_ * 3.0f + i * 2.0f)
                            + 0.15f * std::sin(phase_ * 10.0f + i * 3.0f);
                break;
            case GeneratorSignal::SWEEP_LOG:
                wave = 0.5f + 0.5f * std::sin(phase_ * (sweep_freq_ / 50.0f));
                break;
        }

        const float transient = 0.5f + 0.5f * std::sin(phase_ * (1.37f + i * 0.071f));
        const float input = -54.0f + wave * 31.0f + transient * 8.0f;
        const bool isStereo = (ch.format == ChannelFormat::Stereo);

        // Channel Input Measurement (Pre-DSP)
        ch.input_peak_l = std::clamp(input, -90.0f, 6.0f);
        ch.input_peak_r = isStereo ? std::clamp(input - 1.5f, -90.0f, 6.0f) : -90.0f;
        ch.input_rms_l = std::max(-90.0f, ch.input_peak_l - 4.5f);
        ch.input_rms_r = isStereo ? std::max(-90.0f, ch.input_peak_r - 4.5f) : -90.0f;
        ch.input_peak_hold_l = std::max(ch.input_peak_l, ch.input_peak_hold_l - 0.35f);
        ch.input_peak_hold_r = isStereo ? std::max(ch.input_peak_r, ch.input_peak_hold_r - 0.35f) : -90.0f;
        ch.input_clip = ch.input_peak_l >= 0.0f || ch.input_peak_r >= 0.0f;

        // Sidechain filter simulation
        const float sc_tilt = ch.sidechain_on ?
            std::clamp((ch.sidechain_low - 80.0f) / 100.0f, -6.0f, 6.0f) -
            std::clamp((ch.sidechain_high - 8000.0f) / 3000.0f, -6.0f, 6.0f) : 0.0f;

        const float detector = input + sc_tilt;

        // Gate state (before compressor & FX rack)
        ch.gate_detector_db = input;
        update_gate_state(ch, input, 50.0f);
        const float post_gate = gate_output_db(detector, ch);
        ch.gate_gr_db = (!ch.gate_on || ch.gate_open) ? 0.0f : ch.gate_range;

        // Compressor gain reduction
        const float reduction = std::min(24.0f, compressor_gain_reduction_db(
            post_gate, ch.comp_thresh, ch.comp_ratio, ch.comp_knee, !ch.comp_bypass));
        ch.comp_gr = compressor_envelope(ch.comp_gr, -reduction, ch.comp_attack, ch.comp_release, 50.0f);
        ch.comp_input = post_gate;
        float levelL = post_gate + ch.comp_gr + ch.comp_makeup;
        float levelR = isStereo ? (levelL - 1.5f) : -90.0f;
        ch.comp_output = levelL;
        ch.comp_output_r = isStereo ? levelR : levelL;
        ch.comp_output_peak_hold_l = std::max(ch.comp_output, ch.comp_output_peak_hold_l - 0.35f);
        ch.comp_output_peak_hold_r = std::max(ch.comp_output_r, ch.comp_output_peak_hold_r - 0.35f);
        ch.comp_output_rms_l = std::max(-90.0f, ch.comp_output - 4.5f);
        ch.comp_output_rms_r = std::max(-90.0f, ch.comp_output_r - 4.5f);

        // Process real 128-frame PCM through FX Rack slots (measures fx_in_* and fx_out_*)
        runRealFxRackPcm(ch, levelL, levelR, signal_type_, sweep_freq_, i);

        float postFxL = ch.fx_out_peak_l;
        float postFxR = isStereo ? ch.fx_out_peak_r : postFxL;
        if (ch.mute) {
            postFxL = -90.0f;
            postFxR = -90.0f;
        }

        ch.pre_limiter_peak_l = std::clamp(postFxL, -90.0f, 6.0f);
        ch.pre_limiter_peak_r = isStereo ? std::clamp(postFxR, -90.0f, 6.0f) : -90.0f;
        if (ch.limiter_enabled && std::max(ch.pre_limiter_peak_l, ch.pre_limiter_peak_r) > ch.limiter_ceiling_db) {
            ch.limiter_gr_db = ch.limiter_ceiling_db - std::max(ch.pre_limiter_peak_l, ch.pre_limiter_peak_r);
            ch.peak_l = std::min(ch.pre_limiter_peak_l, ch.limiter_ceiling_db);
            ch.peak_r = isStereo ? std::min(ch.pre_limiter_peak_r, ch.limiter_ceiling_db) : -90.0f;
        } else {
            ch.limiter_gr_db = 0.0f;
            ch.peak_l = ch.pre_limiter_peak_l;
            ch.peak_r = isStereo ? ch.pre_limiter_peak_r : -90.0f;
        }
        const float fader_db = (ch.fader - 0.86f) * 40.0f;
        ch.post_fader_peak_l = std::clamp(ch.peak_l + fader_db, -90.0f, 6.0f);
        ch.post_fader_peak_r = isStereo ? std::clamp(ch.peak_r + fader_db, -90.0f, 6.0f) : -90.0f;
        ch.rms_l = ch.fx_out_rms_l;
        ch.rms_r = isStereo ? ch.fx_out_rms_r : -90.0f;
        ch.clip = ch.peak_l >= 0.0f || ch.peak_r >= 0.0f;
        ch.peak_hold_l = std::max(ch.peak_l, ch.peak_hold_l - 0.35f);
        ch.peak_hold_r = isStereo ? std::max(ch.peak_r, ch.peak_hold_r - 0.35f) : -90.0f;
        sum += std::max(ch.peak_l, ch.peak_r);
    }

    // AUX buses 0..3
    float aux_return_sum = 0.0f;
    for (size_t a = 0; a < 4; ++a) {
        auto& aux = g_mixer_state.aux_buses[a];
        const bool auxStereo = (aux.format == ChannelFormat::Stereo);
        float send_power = 0.0f;
        for (int i = 0; i < num_ch; ++i) {
            const auto& ch = g_mixer_state.channels[i];
            if (ch.input_source_index < 0 || !ch.send_enabled[a] || ch.sends[a] <= 0.0f) continue;
            if (!ch.send_pre_fader[a] && ch.mute) continue;
            const float src_db = ch.send_pre_fader[a] ? ch.fx_out_peak_l : ch.post_fader_peak_l;
            if (src_db > -89.0f) {
                send_power += std::pow(10.0f, src_db / 20.0f) * ch.sends[a];
            }
        }
        float aux_in_lvl = send_power > 1e-5f ? (20.0f * std::log10(send_power)) : -90.0f;
        aux.input_peak_l = std::clamp(aux_in_lvl, -90.0f, 6.0f);
        aux.input_peak_r = auxStereo ? std::clamp(aux_in_lvl - 0.8f, -90.0f, 6.0f) : -90.0f;
        aux.input_rms_l = std::max(-90.0f, aux.input_peak_l - 4.5f);
        aux.input_rms_r = auxStereo ? std::max(-90.0f, aux.input_peak_r - 4.5f) : -90.0f;
        aux.input_peak_hold_l = std::max(aux.input_peak_l, aux.input_peak_hold_l - 0.35f);
        aux.input_peak_hold_r = auxStereo ? std::max(aux.input_peak_r, aux.input_peak_hold_r - 0.35f) : -90.0f;
        aux.input_clip = aux.input_peak_l >= 0.0f || aux.input_peak_r >= 0.0f;

        const float aux_red = std::min(24.0f, compressor_gain_reduction_db(
            aux_in_lvl, aux.comp_thresh, aux.comp_ratio, aux.comp_knee, !aux.comp_bypass));
        aux.comp_gr = compressor_envelope(aux.comp_gr, -aux_red, aux.comp_attack, aux.comp_release, 50.0f);
        float aux_pre_fx_l = (aux_in_lvl > -89.0f) ? (aux_in_lvl + aux.comp_gr + aux.comp_makeup) : -90.0f;
        float aux_pre_fx_r = auxStereo ? (aux_pre_fx_l - 0.8f) : -90.0f;
        aux.comp_output = aux_pre_fx_l;
        aux.comp_output_r = auxStereo ? aux_pre_fx_r : aux_pre_fx_l;

        // Run real FX Rack PCM on AUX bus before AUX fader/mute/limiter
        runRealFxRackPcm(aux, aux_pre_fx_l, aux_pre_fx_r, signal_type_, sweep_freq_, 101 + static_cast<int>(a));

        const float aux_fader_db = (aux.fader - 0.86f) * 50.0f;
        float aux_post_fader_l = (!aux.mute && aux.fx_out_peak_l > -89.0f) ? (aux.fx_out_peak_l + aux_fader_db) : -90.0f;
        float aux_post_fader_r = (!aux.mute && auxStereo && aux.fx_out_peak_r > -89.0f) ? (aux.fx_out_peak_r + aux_fader_db) : -90.0f;
        aux.pre_limiter_peak_l = std::clamp(aux_post_fader_l, -90.0f, 6.0f);
        aux.pre_limiter_peak_r = auxStereo ? std::clamp(aux_post_fader_r, -90.0f, 6.0f) : -90.0f;
        if (aux.limiter_enabled && aux.pre_limiter_peak_l > aux.limiter_ceiling_db) {
            aux.limiter_gr_db = aux.limiter_ceiling_db - aux.pre_limiter_peak_l;
            aux.peak_l = std::min(aux.pre_limiter_peak_l, aux.limiter_ceiling_db);
            aux.peak_r = auxStereo ? std::min(aux.pre_limiter_peak_r, aux.limiter_ceiling_db) : -90.0f;
        } else {
            aux.limiter_gr_db = 0.0f;
            aux.peak_l = aux.pre_limiter_peak_l;
            aux.peak_r = auxStereo ? aux.pre_limiter_peak_r : -90.0f;
        }
        aux.post_fader_peak_l = aux.peak_l;
        aux.post_fader_peak_r = aux.peak_r;
        aux.rms_l = std::max(-90.0f, aux.peak_l - 4.5f);
        aux.rms_r = auxStereo ? std::max(-90.0f, aux.peak_r - 4.5f) : -90.0f;
        aux.peak_hold_l = std::max(aux.peak_l, aux.peak_hold_l - 0.35f);
        aux.peak_hold_r = auxStereo ? std::max(aux.peak_r, aux.peak_hold_r - 0.35f) : -90.0f;
        aux.clip = aux.peak_l >= 0.0f || aux.peak_r >= 0.0f;
        if (!aux.mute && aux.route_to_main && aux.peak_l > -85.0f) {
            aux_return_sum += 1.5f;
        }
    }

    // Master bus
    auto& main = g_mixer_state.master;
    main.format = ChannelFormat::Stereo;
    float main_in_lvl = num_ch > 0
        ? (sum / num_ch) - 6.0f + aux_return_sum + std::sin(phase_ * 0.47f) * 1.4f
        : -90.0f;
    main.input_peak_l = std::clamp(main_in_lvl, -90.0f, 6.0f);
    main.input_peak_r = std::clamp(main_in_lvl - std::sin(phase_ * 0.59f), -90.0f, 6.0f);
    main.input_rms_l = std::max(-90.0f, main.input_peak_l - 4.5f);
    main.input_rms_r = std::max(-90.0f, main.input_peak_r - 4.5f);
    main.input_peak_hold_l = std::max(main.input_peak_l, main.input_peak_hold_l - 0.35f);
    main.input_peak_hold_r = std::max(main.input_peak_r, main.input_peak_hold_r - 0.35f);
    main.input_clip = main.input_peak_l >= 0.0f || main.input_peak_r >= 0.0f;

    const float main_red = std::min(24.0f, compressor_gain_reduction_db(
        main_in_lvl, main.comp_thresh, main.comp_ratio, main.comp_knee, !main.comp_bypass));
    main.comp_gr = compressor_envelope(main.comp_gr, -main_red, main.comp_attack, main.comp_release, 50.0f);
    float main_pre_fx_l = main_in_lvl + main.comp_gr + main.comp_makeup;
    float main_pre_fx_r = main_pre_fx_l - std::sin(phase_ * 0.59f);
    main.comp_output = main_pre_fx_l;
    main.comp_output_r = main_pre_fx_r;

    // Run real FX Rack PCM on Master bus before Master fader/mute/limiter
    runRealFxRackPcm(main, main_pre_fx_l, main_pre_fx_r, signal_type_, sweep_freq_, 100);

    const float main_fader_db = (main.fader - 0.86f) * 50.0f;
    float main_post_fader_l = (!main.mute && main.fx_out_peak_l > -89.0f) ? (main.fx_out_peak_l + main_fader_db) : -90.0f;
    float main_post_fader_r = (!main.mute && main.fx_out_peak_r > -89.0f) ? (main.fx_out_peak_r + main_fader_db) : -90.0f;

    main.pre_limiter_peak_l = std::clamp(main_post_fader_l, -90.0f, 6.0f);
    main.pre_limiter_peak_r = std::clamp(main_post_fader_r, -90.0f, 6.0f);
    const float max_pre_lim = std::max(main.pre_limiter_peak_l, main.pre_limiter_peak_r);
    if (main.limiter_enabled && max_pre_lim > main.limiter_ceiling_db) {
        main.limiter_gr_db = main.limiter_ceiling_db - max_pre_lim;
        main.peak_l = std::min(main.pre_limiter_peak_l, main.limiter_ceiling_db);
        main.peak_r = std::min(main.pre_limiter_peak_r, main.limiter_ceiling_db);
    } else {
        main.limiter_gr_db = 0.0f;
        main.peak_l = main.pre_limiter_peak_l;
        main.peak_r = main.pre_limiter_peak_r;
    }
    main.post_fader_peak_l = main.peak_l;
    main.post_fader_peak_r = main.peak_r;
    main.rms_l = main.fx_out_rms_l;
    main.rms_r = main.fx_out_rms_r;
    main.peak_hold_l = std::max(main.peak_l, main.peak_hold_l - 0.35f);
    main.peak_hold_r = std::max(main.peak_r, main.peak_hold_r - 0.35f);
    main.clip = main.peak_l >= 0.0f || main.peak_r >= 0.0f;

    // Update RTA Analyzer with real PCM generated from the selected signal generator
    ChannelState* sel_ch = mixer_get_selected_channel();
    if (!sel_ch) sel_ch = mixer_get_channel(0);
    const uint32_t targetId = sel_ch ? static_cast<uint32_t>(sel_ch->id) : 0u;
    analyzer_.set_channel_id(targetId);

    if (sel_ch && sel_ch->peak_l > -85.0f && num_ch > 0) {
        std::vector<float> mockPcm(1024);
        constexpr double fs = 48000.0;
        const float amp = std::pow(10.0f, std::clamp(sel_ch->peak_l, -70.0f, 6.0f) / 20.0f);

        auto eval_eq = [&](float freq) -> float {
            if (analyzer_.get_source() == RtaSource::POST_EQ && sel_ch) {
                return std::pow(10.0f, eq_response_db(freq, *sel_ch) / 20.0f);
            }
            return 1.0f;
        };

        switch (signal_type_) {
            case GeneratorSignal::SINE_1KHZ: {
                const float g = eval_eq(1000.0f);
                for (size_t n = 0; n < 1024; ++n) {
                    mockPcm[n] = amp * g * static_cast<float>(std::sin(2.0 * M_PI * 1000.0 * (phase_ * 100.0 + n) / fs));
                }
                break;
            }
            case GeneratorSignal::MULTI_TONE: {
                const float g1 = eval_eq(1000.0f);
                const float g2 = eval_eq(3000.0f);
                const float g3 = eval_eq(10000.0f);
                for (size_t n = 0; n < 1024; ++n) {
                    const double t = (phase_ * 100.0 + n) / fs;
                    mockPcm[n] = amp * (0.5f * g1 * static_cast<float>(std::sin(2.0 * M_PI * 1000.0 * t)) +
                                        0.3f * g2 * static_cast<float>(std::sin(2.0 * M_PI * 3000.0 * t)) +
                                        0.2f * g3 * static_cast<float>(std::sin(2.0 * M_PI * 10000.0 * t)));
                }
                break;
            }
            case GeneratorSignal::SWEEP_LOG: {
                const float g = eval_eq(sweep_freq_);
                for (size_t n = 0; n < 1024; ++n) {
                    mockPcm[n] = amp * g * static_cast<float>(std::sin(2.0 * M_PI * sweep_freq_ * (phase_ * 100.0 + n) / fs));
                }
                break;
            }
            case GeneratorSignal::WHITE_NOISE:
            case GeneratorSignal::PINK_NOISE:
            default: {
                static uint32_t rngState = 123456789;
                static float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
                for (size_t n = 0; n < 1024; ++n) {
                    rngState = rngState * 1664525u + 1013904223u;
                    float white = static_cast<float>(static_cast<int32_t>(rngState)) / 2147483648.0f;
                    float val = white;
                    if (signal_type_ == GeneratorSignal::PINK_NOISE) {
                        b0 = 0.99765f * b0 + white * 0.0990460f;
                        b1 = 0.96300f * b1 + white * 0.2965164f;
                        b2 = 0.57000f * b2 + white * 1.0526913f;
                        val = (b0 + b1 + b2 + white * 0.1848f) * 0.25f;
                    }
                    mockPcm[n] = amp * val;
                }
                break;
            }
        }
        analyzer_.process_pcm(mockPcm.data(), mockPcm.size(), fs);
    } else {
        analyzer_.process_silence();
    }
    update_telemetry();
}
