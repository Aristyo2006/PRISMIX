#include "app.h"
#include "../ui/ui.h"
#include "../backend/mock_audio_backend.h"
#include "../service/audio_service.h"
#include "../model/mixer_state.h"
#include "../model/mixer_scene.h"
#include "../model/routing_state.h"
#include "../ui/ui_audio_bridge.h"
#include "../dsp/eq_model.h"
#include "lvgl.h"
#include <algorithm>
#include <cmath>
#include <string>

static lv_timer_t* backend_timer;
static std::string savedSceneFingerprint;
static std::string pendingSceneFingerprint;
static uint32_t sceneChangeTick = 0;
static bool sceneFingerprintInitialized = false;

static void append_channel_fingerprint(std::string& fingerprint, const ChannelState& channel) {
    fingerprint += std::to_string(channel.id) + "|" + channel.name + "|" +
        std::to_string(static_cast<int>(channel.format)) + "|" +
        std::to_string(channel.input_source_index) + "|" +
        std::to_string(channel.input_source_index_r) + "|" +
        std::to_string(channel.fader) + "|" +
        std::to_string(channel.return_gain) + "|" +
        std::to_string(channel.gain) + "|" + std::to_string(channel.pan) + "|" +
        std::to_string(channel.mute) + "|" + std::to_string(channel.solo) + "|" +
        std::to_string(channel.route_to_main) + "|" +
        std::to_string(static_cast<int>(channel.output_route)) + "|" +
        std::to_string(channel.output_route_index) + "|" +
        std::to_string(channel.polarity_inverted) + "|" + std::to_string(channel.hpf_on) + "|" +
        std::to_string(channel.hpf_frequency) + "|" + std::to_string(channel.eq_bypass) + "|" +
        std::to_string(channel.geq_mode_active) + "|" + std::to_string(channel.geq_bypass) + "|" +
        std::to_string(channel.comp_bypass) + "|" + std::to_string(channel.comp_thresh) + "|" +
        std::to_string(channel.comp_ratio) + "|" + std::to_string(channel.comp_attack) + "|" +
        std::to_string(channel.comp_release) + "|" + std::to_string(channel.comp_knee) + "|" +
        std::to_string(channel.comp_makeup) + "|" + std::to_string(channel.gate_on) + "|" +
        std::to_string(channel.gate_thresh) + "|" + std::to_string(channel.gate_range) + "|" +
        std::to_string(channel.gate_attack) + "|" + std::to_string(channel.gate_hold) + "|" +
        std::to_string(channel.gate_release) + "|" +
        std::to_string(channel.limiter_enabled) + "|" +
        std::to_string(channel.limiter_ceiling_db) + "|" +
        std::to_string(channel.limiter_release_ms) + "|";
    for (size_t a = 0; a < 4; ++a) {
        fingerprint += std::to_string(channel.sends[a]) + "," +
            std::to_string(channel.send_enabled[a]) + "," +
            std::to_string(channel.send_pre_fader[a]) + ";";
    }
    for (size_t band = 0; band < 4; ++band) {
        fingerprint += std::to_string(channel.eq_freq[band]) + "," + std::to_string(channel.eq_gain[band]) + "," +
            std::to_string(channel.eq_q[band]) + "," + std::to_string(channel.eq_enabled[band]) + "," +
            std::to_string(channel.eq_filter_type[band]) + ";";
    }
    for (float gain : channel.geq.get_gains()) fingerprint += std::to_string(gain) + ",";
    for (size_t slot = 0; slot < channel.fx_rack.count(); ++slot) {
        const auto* fx = channel.fx_rack.get_slot(slot);
        if (!fx) continue;
        fingerprint += fx->get_uri() + "|" + std::to_string(fx->is_bypassed()) + "|" +
            std::to_string(fx->is_enabled()) + "|";
        for (size_t i = 0; i < fx->parameter_count(); ++i) {
            const auto* parameter = fx->get_parameter(i);
            if (parameter) fingerprint += parameter->id + "=" + std::to_string(parameter->current_val) + ";";
        }
    }
    fingerprint += "\n";
}

static std::string mixer_scene_fingerprint() {
    std::string fingerprint;
    fingerprint.reserve((g_mixer_state.channels.size() + 5) * 256);
    for (const auto& channel : g_mixer_state.channels) {
        append_channel_fingerprint(fingerprint, channel);
        if (channel.id >= 1 && channel.id <= static_cast<int>(RoutingMatrix::NUM_CHANNELS)) {
            fingerprint += "main_lr=" +
                std::to_string(g_routing_matrix.get_channel_main_lr(static_cast<size_t>(channel.id - 1))) + "\n";
        }
    }
    append_channel_fingerprint(fingerprint, g_mixer_state.master);
    for (const auto& aux : g_mixer_state.aux_buses) {
        append_channel_fingerprint(fingerprint, aux);
    }
    return fingerprint;
}

static void autosave_mixer_scene() {
    const std::string current = mixer_scene_fingerprint();
    if (!sceneFingerprintInitialized) {
        savedSceneFingerprint = current;
        sceneFingerprintInitialized = true;
        return;
    }
    if (current == savedSceneFingerprint) {
        pendingSceneFingerprint.clear();
        return;
    }
    if (current != pendingSceneFingerprint) {
        pendingSceneFingerprint = current;
        sceneChangeTick = lv_tick_get();
        return;
    }
    if (lv_tick_elaps(sceneChangeTick) < 750) return;
    std::string error;
    if (save_mixer_scene(default_mixer_scene_path(), &error)) {
        savedSceneFingerprint = current;
    }
    pendingSceneFingerprint.clear();
}

static void backend_timer_cb(lv_timer_t* timer) {
    autosave_mixer_scene();
    const auto audio = audio_service().snapshot();
    if (audio.mode == AudioMode::Simulation) {
        g_backend.update();
    } else {
        for (const auto& channel : g_mixer_state.channels) {
            const float faderVal = std::clamp(channel.fader, 0.0f, 1.0f);
            const float faderDb = faderVal <= 0.86f
                ? -60.0f + faderVal * (60.0f / 0.86f)
                : (faderVal - 0.86f) * (10.0f / 0.14f);
            const float gainKnobDb = (std::clamp(channel.gain, 0.0f, 1.0f) - 0.5f) * 24.0f;
            const float totalGainDb = std::clamp(faderDb + gainKnobDb, -60.0f, 12.0f);
            const float panVal = std::clamp((channel.pan - 0.5f) * 2.0f, -1.0f, 1.0f);
            audio_service().updateLiveStrip(static_cast<uint32_t>(channel.id), totalGainDb, panVal, channel.mute, channel.solo);
            auto stripCfg = ui_make_strip_config_for_channel(channel);
            if (channel.id >= 1 && channel.id <= static_cast<int>(RoutingMatrix::NUM_CHANNELS)) {
                stripCfg.routeToMain = g_routing_matrix.get_channel_main_lr(static_cast<size_t>(channel.id - 1));
            }
            audio_service().updateLiveStripDsp(stripCfg);
        }
        for (size_t a = 0; a < 4; ++a) {
            auto auxCfg = ui_make_strip_config_for_channel(g_mixer_state.aux_buses[a]);
            auxCfg.routeToMain = g_mixer_state.aux_buses[a].route_to_main;
            audio_service().updateLiveAuxBusDsp(a, auxCfg);
        }
        {
            auto masterCfg = ui_make_strip_config_for_channel(g_mixer_state.master);
            masterCfg.format = AudioStripFormat::Stereo;
            masterCfg.routeToMain = true;
            audio_service().updateLiveMasterBusDsp(masterCfg);
        }

        for (auto& channel : g_mixer_state.channels) {
            channel.input_peak_l = channel.input_peak_r = -90.0f;
            channel.input_rms_l = channel.input_rms_r = -90.0f;
            channel.input_clip = false;
            channel.fx_in_peak_l = channel.fx_in_peak_r = -90.0f;
            channel.fx_in_rms_l = channel.fx_in_rms_r = -90.0f;
            channel.fx_in_clip = false;
            channel.fx_out_peak_l = channel.fx_out_peak_r = -90.0f;
            channel.fx_out_rms_l = channel.fx_out_rms_r = -90.0f;
            channel.fx_out_clip = false;
            channel.peak_l = channel.peak_r = -90.0f;
            channel.rms_l = channel.rms_r = -90.0f;
            channel.post_fader_peak_l = channel.post_fader_peak_r = -90.0f;
            channel.pre_limiter_peak_l = channel.pre_limiter_peak_r = -90.0f;
            channel.clip = false;
        }
        auto toDb = [](float level) {
            return level <= 1.0e-6f ? -90.0f : 20.0f * std::log10(level);
        };
        auto& master = g_mixer_state.master;

        auto copyAfsTelemetry = [](ChannelState& ch, bool assigned, uint8_t state,
                                   uint8_t activeCount, uint8_t maxNotches,
                                   const std::array<AfsNotchTelemetry, 8>& notches) {
            ch.afs_assigned = assigned;
            ch.afs_status = state;
            ch.afs_active_notch_count = activeCount;
            ch.afs_max_notches = maxNotches;
            for (size_t k = 0; k < 8; ++k) {
                ch.afs_notches[k].id = notches[k].id;
                ch.afs_notches[k].frequency_hz = notches[k].frequencyHz;
                ch.afs_notches[k].depth_db = notches[k].depthDb;
                ch.afs_notches[k].q = notches[k].q;
                ch.afs_notches[k].state = notches[k].state;
            }
        };
        auto syncIdleAfsFromRack = [](ChannelState& ch) {
            ch.afs_assigned = false;
            ch.afs_status = 0;
            ch.afs_active_notch_count = 0;
            ch.afs_max_notches = 8;
            ch.afs_notches = {};
            ch.fx_slot_gr_db = {};
            ch.fx_slot_gate_gr_db = {};
            ch.fx_slot_gate_detector_db = {};
            ch.fx_slot_gate_state = {
                GATE_STATE_UNAVAILABLE, GATE_STATE_UNAVAILABLE, GATE_STATE_UNAVAILABLE, GATE_STATE_UNAVAILABLE,
                GATE_STATE_UNAVAILABLE, GATE_STATE_UNAVAILABLE, GATE_STATE_UNAVAILABLE, GATE_STATE_UNAVAILABLE};
            ch.fx_slot_limiter_gr_db = {
                LIMITER_GR_UNAVAILABLE, LIMITER_GR_UNAVAILABLE, LIMITER_GR_UNAVAILABLE, LIMITER_GR_UNAVAILABLE,
                LIMITER_GR_UNAVAILABLE, LIMITER_GR_UNAVAILABLE, LIMITER_GR_UNAVAILABLE, LIMITER_GR_UNAVAILABLE};
            for (size_t s = 0; s < ch.fx_rack.count(); ++s) {
                if (const auto* inst = ch.fx_rack.get_slot(s)) {
                    if (inst->get_uri() == "http://audiocore.org/plugins/afs") {
                        ch.afs_assigned = true;
                        ch.afs_status = inst->is_bypassed() ? 1 : 2;
                        ch.afs_max_notches = static_cast<uint8_t>(
                            std::clamp(std::round(inst->get_parameter_value("max_notches", 8.0f)), 1.0f, 16.0f));
                        break;
                    }
                }
            }
        };

        const bool streaming = (audio.state == AudioServiceState::Streaming);
        if (streaming) {
            for (const auto& meter : audio.strips) {
                auto* channel = mixer_find_channel_by_id(static_cast<int>(meter.logicalId));
                if (!channel) continue;
                const bool isStereo = (meter.channelCount >= 2);
                // Channel Input (Pre-DSP: physical/buffer entry before HPF, Gate, EQ, Comp, Limiter)
                channel->input_peak_l = std::clamp(toDb(meter.inputPeak[0]), -90.0f, 6.0f);
                channel->input_peak_r = isStereo ? std::clamp(toDb(meter.inputPeak[1]), -90.0f, 6.0f) : -90.0f;
                channel->input_rms_l = std::clamp(toDb(meter.inputRms[0]), -90.0f, 6.0f);
                channel->input_rms_r = isStereo ? std::clamp(toDb(meter.inputRms[1]), -90.0f, 6.0f) : -90.0f;
                channel->input_peak_hold_l = std::max(channel->input_peak_l, channel->input_peak_hold_l - 0.2f);
                channel->input_peak_hold_r = isStereo ? std::max(channel->input_peak_r, channel->input_peak_hold_r - 0.2f) : -90.0f;
                channel->input_clip = channel->input_peak_l >= 0.0f || channel->input_peak_r >= 0.0f;

                // Dedicated FX Rack Taps (PRE-FX INPUT immediately before fxSlots[0..7], POST-FX OUTPUT immediately after fxSlots[0..7])
                channel->fx_in_peak_l = std::clamp(toDb(meter.fxInputPeak[0]), -90.0f, 6.0f);
                channel->fx_in_peak_r = isStereo ? std::clamp(toDb(meter.fxInputPeak[1]), -90.0f, 6.0f) : -90.0f;
                channel->fx_in_rms_l = std::clamp(toDb(meter.fxInputRms[0]), -90.0f, 6.0f);
                channel->fx_in_rms_r = isStereo ? std::clamp(toDb(meter.fxInputRms[1]), -90.0f, 6.0f) : -90.0f;
                channel->fx_in_peak_hold_l = std::max(channel->fx_in_peak_l, channel->fx_in_peak_hold_l - 0.2f);
                channel->fx_in_peak_hold_r = isStereo ? std::max(channel->fx_in_peak_r, channel->fx_in_peak_hold_r - 0.2f) : -90.0f;
                channel->fx_in_clip = channel->fx_in_peak_l >= 0.0f || channel->fx_in_peak_r >= 0.0f;

                channel->fx_out_peak_l = std::clamp(toDb(meter.fxOutputPeak[0]), -90.0f, 6.0f);
                channel->fx_out_peak_r = isStereo ? std::clamp(toDb(meter.fxOutputPeak[1]), -90.0f, 6.0f) : -90.0f;
                channel->fx_out_rms_l = std::clamp(toDb(meter.fxOutputRms[0]), -90.0f, 6.0f);
                channel->fx_out_rms_r = isStereo ? std::clamp(toDb(meter.fxOutputRms[1]), -90.0f, 6.0f) : -90.0f;
                channel->fx_out_peak_hold_l = std::max(channel->fx_out_peak_l, channel->fx_out_peak_hold_l - 0.2f);
                channel->fx_out_peak_hold_r = isStereo ? std::max(channel->fx_out_peak_r, channel->fx_out_peak_hold_r - 0.2f) : -90.0f;
                channel->fx_out_clip = channel->fx_out_peak_l >= 0.0f || channel->fx_out_peak_r >= 0.0f;

                // Channel Output (Post-DSP, Pre-Fader: after HPF, Gate, EQ, Comp, Limiter)
                channel->peak_l = std::clamp(toDb(meter.outputPeak[0]), -90.0f, 6.0f);
                channel->peak_r = isStereo ? std::clamp(toDb(meter.outputPeak[1]), -90.0f, 6.0f) : -90.0f;
                channel->rms_l = std::clamp(toDb(meter.outputRms[0]), -90.0f, 6.0f);
                channel->rms_r = isStereo ? std::clamp(toDb(meter.outputRms[1]), -90.0f, 6.0f) : -90.0f;
                channel->peak_hold_l = std::max(channel->peak_l, channel->peak_hold_l - 0.2f);
                channel->peak_hold_r = isStereo ? std::max(channel->peak_r, channel->peak_hold_r - 0.2f) : -90.0f;
                channel->clip = channel->peak_l >= 0.0f || channel->peak_r >= 0.0f;

                // Channel Post-Fader Output
                channel->post_fader_peak_l = std::clamp(toDb(meter.postFaderPeak[0]), -90.0f, 6.0f);
                channel->post_fader_peak_r = isStereo ? std::clamp(toDb(meter.postFaderPeak[1]), -90.0f, 6.0f) : -90.0f;
                channel->pre_limiter_peak_l = std::clamp(toDb(meter.preLimiterPeak[0]), -90.0f, 12.0f);
                channel->pre_limiter_peak_r = isStereo ? std::clamp(toDb(meter.preLimiterPeak[1]), -90.0f, 12.0f) : -90.0f;

                // Compressor & Gate Telemetry
                channel->comp_gr = meter.gainReductionDb;
                channel->comp_input = meter.compressorDetectorDb;
                channel->comp_output = meter.compressorOutputDb;
                channel->comp_output_r = meter.compressorOutputDbR;
                channel->comp_output_peak_hold_l = std::max(channel->comp_output, channel->comp_output_peak_hold_l - 0.2f);
                channel->comp_output_peak_hold_r = std::max(channel->comp_output_r, channel->comp_output_peak_hold_r - 0.2f);
                channel->comp_output_rms_l = std::max(-90.0f, channel->comp_output - 3.0f);
                channel->comp_output_rms_r = std::max(-90.0f, channel->comp_output_r - 3.0f);
                channel->limiter_gr_db = meter.limiterGainReductionDb;
                channel->gate_gr_db = meter.gateGainReductionDb;
                channel->gate_detector_db = meter.gateDetectorDb;
                channel->gate_state = meter.gateState;
                channel->gate_open = (meter.gateState == 2 || meter.gateState == 3);
                copyAfsTelemetry(*channel, meter.afsAssigned, meter.afsState,
                                 meter.afsActiveNotchCount, meter.afsMaxNotches, meter.afsNotches);
                channel->fx_slot_gr_db = meter.fxSlotGrDb;
                channel->fx_slot_gate_gr_db = meter.fxSlotGateGrDb;
                channel->fx_slot_gate_detector_db = meter.fxSlotGateDetectorDb;
                channel->fx_slot_gate_state = meter.fxSlotGateState;
                channel->fx_slot_limiter_gr_db = meter.fxSlotLimiterGrDb;
            }
            for (size_t a = 0; a < 4; ++a) {
                auto& aux = g_mixer_state.aux_buses[a];
                const auto& m = audio.auxBuses[a];
                const bool auxStereo = (m.channelCount >= 2);
                // AUX Bus Input (Pre-DSP)
                aux.input_peak_l = std::clamp(toDb(m.inputPeak[0]), -90.0f, 6.0f);
                aux.input_peak_r = auxStereo ? std::clamp(toDb(m.inputPeak[1]), -90.0f, 6.0f) : -90.0f;
                aux.input_rms_l = std::clamp(toDb(m.inputRms[0]), -90.0f, 6.0f);
                aux.input_rms_r = auxStereo ? std::clamp(toDb(m.inputRms[1]), -90.0f, 6.0f) : -90.0f;
                aux.input_peak_hold_l = std::max(aux.input_peak_l, aux.input_peak_hold_l - 0.2f);
                aux.input_peak_hold_r = auxStereo ? std::max(aux.input_peak_r, aux.input_peak_hold_r - 0.2f) : -90.0f;
                aux.input_clip = aux.input_peak_l >= 0.0f || aux.input_peak_r >= 0.0f;

                // AUX Bus FX Rack Taps
                aux.fx_in_peak_l = std::clamp(toDb(m.fxInputPeak[0]), -90.0f, 6.0f);
                aux.fx_in_peak_r = auxStereo ? std::clamp(toDb(m.fxInputPeak[1]), -90.0f, 6.0f) : -90.0f;
                aux.fx_in_rms_l = std::clamp(toDb(m.fxInputRms[0]), -90.0f, 6.0f);
                aux.fx_in_rms_r = auxStereo ? std::clamp(toDb(m.fxInputRms[1]), -90.0f, 6.0f) : -90.0f;
                aux.fx_in_peak_hold_l = std::max(aux.fx_in_peak_l, aux.fx_in_peak_hold_l - 0.2f);
                aux.fx_in_peak_hold_r = auxStereo ? std::max(aux.fx_in_peak_r, aux.fx_in_peak_hold_r - 0.2f) : -90.0f;
                aux.fx_in_clip = aux.fx_in_peak_l >= 0.0f || aux.fx_in_peak_r >= 0.0f;

                aux.fx_out_peak_l = std::clamp(toDb(m.fxOutputPeak[0]), -90.0f, 6.0f);
                aux.fx_out_peak_r = auxStereo ? std::clamp(toDb(m.fxOutputPeak[1]), -90.0f, 6.0f) : -90.0f;
                aux.fx_out_rms_l = std::clamp(toDb(m.fxOutputRms[0]), -90.0f, 6.0f);
                aux.fx_out_rms_r = auxStereo ? std::clamp(toDb(m.fxOutputRms[1]), -90.0f, 6.0f) : -90.0f;
                aux.fx_out_peak_hold_l = std::max(aux.fx_out_peak_l, aux.fx_out_peak_hold_l - 0.2f);
                aux.fx_out_peak_hold_r = auxStereo ? std::max(aux.fx_out_peak_r, aux.fx_out_peak_hold_r - 0.2f) : -90.0f;
                aux.fx_out_clip = aux.fx_out_peak_l >= 0.0f || aux.fx_out_peak_r >= 0.0f;

                // AUX Bus Output (Post-DSP)
                aux.peak_l = std::clamp(toDb(m.outputPeak[0]), -90.0f, 6.0f);
                aux.peak_r = auxStereo ? std::clamp(toDb(m.outputPeak[1]), -90.0f, 6.0f) : -90.0f;
                aux.rms_l = std::clamp(toDb(m.outputRms[0]), -90.0f, 6.0f);
                aux.rms_r = auxStereo ? std::clamp(toDb(m.outputRms[1]), -90.0f, 6.0f) : -90.0f;
                aux.post_fader_peak_l = aux.peak_l;
                aux.post_fader_peak_r = aux.peak_r;
                aux.pre_limiter_peak_l = std::clamp(toDb(m.preLimiterPeak[0]), -90.0f, 12.0f);
                aux.pre_limiter_peak_r = auxStereo ? std::clamp(toDb(m.preLimiterPeak[1]), -90.0f, 12.0f) : -90.0f;
                aux.peak_hold_l = std::max(aux.peak_l, aux.peak_hold_l - 0.2f);
                aux.peak_hold_r = auxStereo ? std::max(aux.peak_r, aux.peak_hold_r - 0.2f) : -90.0f;
                aux.clip = aux.peak_l >= 0.0f || aux.peak_r >= 0.0f;
                aux.comp_gr = m.gainReductionDb;
                aux.comp_input = m.compressorDetectorDb;
                aux.comp_output = m.compressorOutputDb;
                aux.comp_output_r = m.compressorOutputDbR;
                aux.comp_output_peak_hold_l = std::max(aux.comp_output, aux.comp_output_peak_hold_l - 0.2f);
                aux.comp_output_peak_hold_r = std::max(aux.comp_output_r, aux.comp_output_peak_hold_r - 0.2f);
                aux.gate_gr_db = m.gateGainReductionDb;
                aux.gate_detector_db = m.gateDetectorDb;
                aux.gate_state = m.gateState;
                aux.gate_open = (m.gateState == 2 || m.gateState == 3);
                aux.limiter_gr_db = m.limiterGainReductionDb;
                copyAfsTelemetry(aux, m.afsAssigned, m.afsState,
                                 m.afsActiveNotchCount, m.afsMaxNotches, m.afsNotches);
                aux.fx_slot_gr_db = m.fxSlotGrDb;
                aux.fx_slot_gate_gr_db = m.fxSlotGateGrDb;
                aux.fx_slot_gate_detector_db = m.fxSlotGateDetectorDb;
                aux.fx_slot_gate_state = m.fxSlotGateState;
                aux.fx_slot_limiter_gr_db = m.fxSlotLimiterGrDb;
            }
            // Master Bus Input (Pre-DSP, Summed Bus Signal)
            master.input_peak_l = std::clamp(toDb(audio.inputPeak[0]), -90.0f, 6.0f);
            master.input_peak_r = std::clamp(toDb(audio.inputPeak[1]), -90.0f, 6.0f);
            master.input_rms_l = std::clamp(toDb(audio.inputRms[0]), -90.0f, 6.0f);
            master.input_rms_r = std::clamp(toDb(audio.inputRms[1]), -90.0f, 6.0f);
            master.input_peak_hold_l = std::max(master.input_peak_l, master.input_peak_hold_l - 0.2f);
            master.input_peak_hold_r = std::max(master.input_peak_r, master.input_peak_hold_r - 0.2f);
            master.input_clip = master.input_peak_l >= 0.0f || master.input_peak_r >= 0.0f;

            // Master Bus FX Rack Taps
            master.fx_in_peak_l = std::clamp(toDb(audio.masterFxInputPeak[0]), -90.0f, 6.0f);
            master.fx_in_peak_r = std::clamp(toDb(audio.masterFxInputPeak[1]), -90.0f, 6.0f);
            master.fx_in_rms_l = std::clamp(toDb(audio.masterFxInputRms[0]), -90.0f, 6.0f);
            master.fx_in_rms_r = std::clamp(toDb(audio.masterFxInputRms[1]), -90.0f, 6.0f);
            master.fx_in_peak_hold_l = std::max(master.fx_in_peak_l, master.fx_in_peak_hold_l - 0.2f);
            master.fx_in_peak_hold_r = std::max(master.fx_in_peak_r, master.fx_in_peak_hold_r - 0.2f);
            master.fx_in_clip = master.fx_in_peak_l >= 0.0f || master.fx_in_peak_r >= 0.0f;

            master.fx_out_peak_l = std::clamp(toDb(audio.masterFxOutputPeak[0]), -90.0f, 6.0f);
            master.fx_out_peak_r = std::clamp(toDb(audio.masterFxOutputPeak[1]), -90.0f, 6.0f);
            master.fx_out_rms_l = std::clamp(toDb(audio.masterFxOutputRms[0]), -90.0f, 6.0f);
            master.fx_out_rms_r = std::clamp(toDb(audio.masterFxOutputRms[1]), -90.0f, 6.0f);
            master.fx_out_peak_hold_l = std::max(master.fx_out_peak_l, master.fx_out_peak_hold_l - 0.2f);
            master.fx_out_peak_hold_r = std::max(master.fx_out_peak_r, master.fx_out_peak_hold_r - 0.2f);
            master.fx_out_clip = master.fx_out_peak_l >= 0.0f || master.fx_out_peak_r >= 0.0f;

            // Master Pre-Limiter & Final Output
            master.pre_limiter_peak_l = std::clamp(toDb(audio.preLimiterPeak[0]), -90.0f, 12.0f);
            master.pre_limiter_peak_r = std::clamp(toDb(audio.preLimiterPeak[1]), -90.0f, 12.0f);
            master.peak_l = std::clamp(toDb(audio.outputPeak[0]), -90.0f, 6.0f);
            master.peak_r = std::clamp(toDb(audio.outputPeak[1]), -90.0f, 6.0f);
            master.post_fader_peak_l = master.peak_l;
            master.post_fader_peak_r = master.peak_r;
            master.rms_l = std::clamp(toDb(audio.outputRms[0]), -90.0f, 6.0f);
            master.rms_r = std::clamp(toDb(audio.outputRms[1]), -90.0f, 6.0f);
            master.peak_hold_l = std::max(master.peak_l, master.peak_hold_l - 0.2f);
            master.peak_hold_r = std::max(master.peak_r, master.peak_hold_r - 0.2f);
            master.clip = master.peak_l >= 0.0f || master.peak_r >= 0.0f;
            master.comp_gr = audio.compressorGainReductionDb;
            master.comp_input = audio.masterCompressorDetectorDb;
            master.comp_output = audio.masterCompressorOutputDb;
            master.comp_output_r = audio.masterCompressorOutputDbR;
            master.comp_output_peak_hold_l = std::max(master.comp_output, master.comp_output_peak_hold_l - 0.2f);
            master.comp_output_peak_hold_r = std::max(master.comp_output_r, master.comp_output_peak_hold_r - 0.2f);
            master.gate_gr_db = audio.masterGateGainReductionDb;
            master.gate_detector_db = audio.masterGateDetectorDb;
            master.gate_state = audio.masterGateState;
            master.gate_open = (audio.masterGateState == 2 || audio.masterGateState == 3);
            master.limiter_gr_db = audio.limiterGainReductionDb;
            copyAfsTelemetry(master, audio.masterAfsAssigned, audio.masterAfsState,
                             audio.masterAfsActiveNotchCount, audio.masterAfsMaxNotches, audio.masterAfsNotches);
        } else {
            // Engine stopped: clear or reset stale dynamics telemetry to silent floor
            for (auto& ch : g_mixer_state.channels) {
                ch.input_peak_l = ch.input_peak_r = -90.0f;
                ch.input_rms_l = ch.input_rms_r = -90.0f;
                ch.input_peak_hold_l = ch.input_peak_hold_r = -90.0f;
                ch.input_clip = false;
                ch.fx_in_peak_l = ch.fx_in_peak_r = -90.0f;
                ch.fx_in_rms_l = ch.fx_in_rms_r = -90.0f;
                ch.fx_in_peak_hold_l = ch.fx_in_peak_hold_r = -90.0f;
                ch.fx_in_clip = false;
                ch.fx_out_peak_l = ch.fx_out_peak_r = -90.0f;
                ch.fx_out_rms_l = ch.fx_out_rms_r = -90.0f;
                ch.fx_out_peak_hold_l = ch.fx_out_peak_hold_r = -90.0f;
                ch.fx_out_clip = false;
                ch.peak_l = ch.peak_r = -90.0f;
                ch.rms_l = ch.rms_r = -90.0f;
                ch.peak_hold_l = ch.peak_hold_r = -90.0f;
                ch.clip = false;
                ch.post_fader_peak_l = ch.post_fader_peak_r = -90.0f;
                ch.pre_limiter_peak_l = ch.pre_limiter_peak_r = -90.0f;
                ch.comp_gr = 0.0f;
                ch.comp_input = -90.0f;
                ch.comp_output = -90.0f;
                ch.comp_output_r = -90.0f;
                ch.comp_output_peak_hold_l = ch.comp_output_peak_hold_r = -90.0f;
                ch.gate_gr_db = ch.gate_on ? ch.gate_range : 0.0f;
                ch.gate_detector_db = -90.0f;
                ch.gate_state = ch.gate_on ? 0 : 2;
                ch.gate_open = !ch.gate_on;
                ch.limiter_gr_db = 0.0f;
                syncIdleAfsFromRack(ch);
            }
            for (auto& aux : g_mixer_state.aux_buses) {
                aux.input_peak_l = aux.input_peak_r = -90.0f;
                aux.input_rms_l = aux.input_rms_r = -90.0f;
                aux.input_peak_hold_l = aux.input_peak_hold_r = -90.0f;
                aux.input_clip = false;
                aux.fx_in_peak_l = aux.fx_in_peak_r = -90.0f;
                aux.fx_in_rms_l = aux.fx_in_rms_r = -90.0f;
                aux.fx_in_peak_hold_l = aux.fx_in_peak_hold_r = -90.0f;
                aux.fx_in_clip = false;
                aux.fx_out_peak_l = aux.fx_out_peak_r = -90.0f;
                aux.fx_out_rms_l = aux.fx_out_rms_r = -90.0f;
                aux.fx_out_peak_hold_l = aux.fx_out_peak_hold_r = -90.0f;
                aux.fx_out_clip = false;
                aux.peak_l = aux.peak_r = -90.0f;
                aux.rms_l = aux.rms_r = -90.0f;
                aux.peak_hold_l = aux.peak_hold_r = -90.0f;
                aux.clip = false;
                aux.post_fader_peak_l = aux.post_fader_peak_r = -90.0f;
                aux.pre_limiter_peak_l = aux.pre_limiter_peak_r = -90.0f;
                aux.comp_gr = 0.0f;
                aux.comp_input = -90.0f;
                aux.comp_output = -90.0f;
                aux.comp_output_r = -90.0f;
                aux.comp_output_peak_hold_l = aux.comp_output_peak_hold_r = -90.0f;
                aux.gate_gr_db = aux.gate_on ? aux.gate_range : 0.0f;
                aux.gate_detector_db = -90.0f;
                aux.gate_state = aux.gate_on ? 0 : 2;
                aux.gate_open = !aux.gate_on;
                aux.limiter_gr_db = 0.0f;
                syncIdleAfsFromRack(aux);
            }
            master.input_peak_l = master.input_peak_r = -90.0f;
            master.input_rms_l = master.input_rms_r = -90.0f;
            master.input_peak_hold_l = master.input_peak_hold_r = -90.0f;
            master.input_clip = false;
            master.fx_in_peak_l = master.fx_in_peak_r = -90.0f;
            master.fx_in_rms_l = master.fx_in_rms_r = -90.0f;
            master.fx_in_peak_hold_l = master.fx_in_peak_hold_r = -90.0f;
            master.fx_in_clip = false;
            master.fx_out_peak_l = master.fx_out_peak_r = -90.0f;
            master.fx_out_rms_l = master.fx_out_rms_r = -90.0f;
            master.fx_out_peak_hold_l = master.fx_out_peak_hold_r = -90.0f;
            master.fx_out_clip = false;
            master.peak_l = master.peak_r = -90.0f;
            master.rms_l = master.rms_r = -90.0f;
            master.peak_hold_l = master.peak_hold_r = -90.0f;
            master.clip = false;
            master.post_fader_peak_l = master.post_fader_peak_r = -90.0f;
            master.pre_limiter_peak_l = master.pre_limiter_peak_r = -90.0f;
            master.comp_gr = 0.0f;
            master.comp_input = -90.0f;
            master.comp_output = -90.0f;
            master.comp_output_r = -90.0f;
            master.comp_output_peak_hold_l = master.comp_output_peak_hold_r = -90.0f;
            master.gate_on = false;
            master.gate_gr_db = 0.0f;
            master.gate_detector_db = -90.0f;
            master.gate_state = 2;
            master.gate_open = true;
            master.limiter_gr_db = 0.0f;
            syncIdleAfsFromRack(master);
        }

        // Update RTA Analyzer for the active channel in Real Audio mode
        ChannelState* sel_ch = mixer_get_selected_channel();
        if (!sel_ch && !g_mixer_state.channels.empty()) sel_ch = &g_mixer_state.channels[0];
        const uint32_t targetId = sel_ch ? static_cast<uint32_t>(sel_ch->id) : 0u;
        const auto tapSrc = g_backend.get_analyzer().get_source();
        audio_service().setAnalyzerTap(targetId, static_cast<uint8_t>(tapSrc));
        g_backend.get_analyzer().set_channel_id(targetId);

        // Pull real PCM frames from audio_service ring buffer into production analyzer
        std::array<float, 4096> pullBuf{};
        size_t pulled = 0;
        const double rate = audio.actualSampleRate > 0.0 ? audio.actualSampleRate : 48000.0;
        while (audio_service().analyzerRingBuffer().available() > 0) {
            size_t chunk = audio_service().analyzerRingBuffer().pull(pullBuf.data(), pullBuf.size());
            if (chunk == 0) break;
            g_backend.get_analyzer().process_pcm(pullBuf.data(), chunk, rate);
            pulled += chunk;
        }
        if (pulled == 0 && audio.state == AudioServiceState::Streaming) {
            g_backend.get_analyzer().process_silence();
        }
    }
    (void)timer;
}

void app_init(void) {
    g_backend.init();
    std::string sceneError;
    load_mixer_scene(default_mixer_scene_path(), &sceneError);
    savedSceneFingerprint = mixer_scene_fingerprint();
    sceneFingerprintInitialized = true;
    g_backend.start();
    ui_init();
    
    backend_timer = lv_timer_create(backend_timer_cb, 50, NULL);
}
