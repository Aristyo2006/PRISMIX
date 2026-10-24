#include "ui_audio_bridge.h"
#include "../model/mixer_state.h"
#include "../model/routing_state.h"
#include "../backend/mock_audio_backend.h"
#include "audiocore/biquad_filter.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace {

AudioSetupConfig g_persistedSetup;
bool g_persistedSetupLoaded = false;

float mixerFaderDb(float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    if (value <= 0.86f) return -60.0f + value * (60.0f / 0.86f);
    return (value - 0.86f) * (10.0f / 0.14f);
}

} // anonymous namespace

AudioSetupConfig& ui_get_persisted_audio_setup() {
    if (!g_persistedSetupLoaded) {
        ui_load_persisted_audio_setup();
    }
    return g_persistedSetup;
}

bool ui_save_persisted_audio_setup(std::string* errorOut) {
    return save_audio_setup(g_persistedSetup, default_audio_setup_path(), errorOut);
}

void ui_load_persisted_audio_setup() {
    std::string err;
    if (!load_audio_setup(default_audio_setup_path(), g_persistedSetup, &err)) {
        g_persistedSetup = AudioSetupConfig{};
    }
    g_persistedSetupLoaded = true;
    auto& service = audio_service();
    if (!g_persistedSetup.captureDeviceId.empty()) {
        service.selectCaptureDevice(g_persistedSetup.captureDeviceId);
    }
    if (!g_persistedSetup.renderDeviceId.empty()) {
        service.selectRenderDevice(g_persistedSetup.renderDeviceId);
    }
    if (g_persistedSetup.requestedSampleRate > 0 && g_persistedSetup.requestedBufferFrames > 0) {
        service.setRequestedAudioConfig(g_persistedSetup.requestedSampleRate, g_persistedSetup.requestedBufferFrames);
    }
}

void ui_ensure_default_channel_assignments() {
    if (g_mixer_state.channels.empty()) return;
    bool anyAssigned = false;
    for (const auto& ch : g_mixer_state.channels) {
        if (ch.input_source_index >= 0) {
            anyAssigned = true;
            break;
        }
    }
    if (!anyAssigned) {
        // Automatically assign first 2 channels to physical inputs 1 & 2
        if (g_mixer_state.channels.size() >= 1) {
            auto& ch0 = g_mixer_state.channels[0];
            ch0.input_source_index = 0;
            std::snprintf(ch0.input_label, sizeof(ch0.input_label), "IN 1");
            ch0.output_route = ChannelOutputRoute::MainLR;
            g_routing_matrix.set_channel_input_source(ch0.id - 1, 0);
            g_routing_matrix.set_channel_main_lr(static_cast<size_t>(ch0.id - 1), true);
        }
        if (g_mixer_state.channels.size() >= 2) {
            auto& ch1 = g_mixer_state.channels[1];
            ch1.input_source_index = 1;
            std::snprintf(ch1.input_label, sizeof(ch1.input_label), "IN 2");
            ch1.output_route = ChannelOutputRoute::MainLR;
            g_routing_matrix.set_channel_input_source(ch1.id - 1, 1);
            g_routing_matrix.set_channel_main_lr(static_cast<size_t>(ch1.id - 1), true);
        }
    }
}

AudioStripConfig ui_make_strip_config_for_channel(const ChannelState& channel) {
    AudioStripConfig strip;
    strip.logicalId = static_cast<uint32_t>(channel.id);
    strip.name = channel.name;
    strip.format = (channel.format == ChannelFormat::Stereo)
        ? AudioStripFormat::Stereo : AudioStripFormat::Mono;
    strip.captureChannel = channel.input_source_index >= 0
        ? static_cast<uint32_t>(channel.input_source_index) : 0u;
    strip.captureChannelRight = channel.input_source_index_r >= 0
        ? static_cast<uint32_t>(channel.input_source_index_r) : (strip.captureChannel + 1u);
    const float gainKnobDb = (std::clamp(channel.gain, 0.0f, 1.0f) - 0.5f) * 24.0f;
    strip.gainDb = std::clamp(mixerFaderDb(channel.fader) + gainKnobDb, -60.0f, 12.0f);
    strip.returnGainDb = std::clamp(mixerFaderDb(channel.return_gain), -60.0f, 12.0f);
    strip.pan = std::clamp((channel.pan - 0.5f) * 2.0f, -1.0f, 1.0f);
    strip.muted = channel.mute;
    strip.solo = channel.solo;
    strip.outputRoute = static_cast<AudioOutputRoute>(channel.output_route);
    strip.outputRouteIndex = static_cast<uint32_t>(std::max(channel.output_route_index, 0));
    strip.routeToMain = channel.route_to_main && (strip.outputRoute == AudioOutputRoute::MainLR);
    strip.polarityInverted = channel.polarity_inverted;
    strip.hpfEnabled = channel.hpf_on;
    strip.hpfFrequency = channel.hpf_frequency;
    strip.peqBypassed = channel.eq_bypass;
    strip.eqFrequency = channel.eq_freq;
    strip.eqGain = channel.eq_gain;
    strip.eqQ = channel.eq_q;
    strip.eqBandEnabled = channel.eq_enabled;

    for (size_t band = 0; band < strip.eqType.size(); ++band) {
        switch (channel.eq_filter_type[band]) {
            case 0: strip.eqType[band] = static_cast<uint8_t>(audiocore::FilterType::LowShelf); break;
            case 1: strip.eqType[band] = static_cast<uint8_t>(audiocore::FilterType::Peaking); break;
            case 2: strip.eqType[band] = static_cast<uint8_t>(audiocore::FilterType::HighShelf); break;
            case 3: strip.eqType[band] = static_cast<uint8_t>(audiocore::FilterType::HighPass); break;
            case 4: strip.eqType[band] = static_cast<uint8_t>(audiocore::FilterType::LowPass); break;
            default: strip.eqType[band] = static_cast<uint8_t>(audiocore::FilterType::Peaking); break;
        }
    }

    strip.geqMode = channel.geq_mode_active;
    strip.geqBypassed = channel.geq_bypass || channel.eq_bypass;
    strip.geqGain = channel.geq.get_gains();
    strip.compressorEnabled = !channel.comp_bypass;
    strip.compressorThresholdDb = channel.comp_thresh;
    strip.compressorRatio = channel.comp_ratio;
    strip.compressorAttackMs = channel.comp_attack;
    strip.compressorReleaseMs = channel.comp_release;
    strip.compressorKneeDb = channel.comp_knee;
    strip.compressorMakeupDb = channel.comp_makeup;
    strip.gateEnabled = isMasterChannel(&channel) ? false : channel.gate_on;
    strip.gateThresholdDb = channel.gate_thresh;
    strip.gateRangeDb = channel.gate_range;
    strip.gateAttackMs = channel.gate_attack;
    strip.gateHoldMs = channel.gate_hold;
    strip.gateReleaseMs = channel.gate_release;
    strip.limiterEnabled = channel.limiter_enabled;
    strip.limiterCeilingDb = std::clamp(channel.limiter_ceiling_db, -24.0f, 0.0f);
    strip.limiterReleaseMs = std::clamp(channel.limiter_release_ms, 1.0f, 2000.0f);
    strip.speakerDelayEnabled = channel.speaker_delay_enabled;
    strip.speakerDelayLink = channel.speaker_delay_link;
    strip.speakerDelayMsL = std::clamp(channel.speaker_delay_l_ms, 0.0f, 500.0f);
    strip.speakerDelayMsR = std::clamp(channel.speaker_delay_r_ms, 0.0f, 500.0f);

    for (size_t a = 0; a < 4; ++a) {
        strip.auxSendLevel[a] = std::clamp(channel.sends[a], 0.0f, 1.0f);
        strip.auxSendEnabled[a] = channel.send_enabled[a];
        strip.auxSendPreFader[a] = channel.send_pre_fader[a];
    }

    for (size_t slot = 0; slot < channel.fx_rack.count(); ++slot) {
        const auto* fx = channel.fx_rack.get_slot(slot);
        if (fx) {
            AudioStripFxConfig effect;
            effect.uri = fx->get_uri();
            effect.bypassed = fx->is_bypassed() || !fx->is_enabled();
            for (size_t p = 0; p < fx->parameter_count(); ++p) {
                const auto* parameter = fx->get_parameter(p);
                if (parameter) effect.parameters.push_back({parameter->id, parameter->current_val});
            }
            strip.fxRack.push_back(std::move(effect));
        }
    }
    return strip;
}

std::vector<AudioStripConfig> ui_make_live_strip_configuration(std::string& error) {
    ui_ensure_default_channel_assignments();

    std::vector<AudioStripConfig> strips;
    const auto snapshot = audio_service().snapshot();
    const AudioDeviceChoice* captureDevice = nullptr;
    const AudioDeviceChoice* renderDevice = nullptr;

    for (const auto& device : snapshot.captureDevices) {
        if (device.id == snapshot.captureDeviceId) { captureDevice = &device; break; }
        if (!captureDevice && device.isDefault) captureDevice = &device;
    }
    if (!captureDevice && !snapshot.captureDevices.empty()) captureDevice = &snapshot.captureDevices.front();

    for (const auto& device : snapshot.renderDevices) {
        if (device.id == snapshot.renderDeviceId) { renderDevice = &device; break; }
        if (!renderDevice && device.isDefault) renderDevice = &device;
    }
    if (!renderDevice && !snapshot.renderDevices.empty()) renderDevice = &snapshot.renderDevices.front();

    const uint32_t maxCapCh = captureDevice ? captureDevice->channels : 2u;
    const uint32_t maxRenCh = renderDevice ? renderDevice->channels : 2u;

    std::set<uint32_t> usedInputs;
    strips.reserve(g_mixer_state.channels.size());

    for (const auto& channel : g_mixer_state.channels) {
        if (channel.input_source_index < 0) continue;
        const uint32_t capCh = static_cast<uint32_t>(channel.input_source_index);
        // Only configure channels mapped to physical inputs exposed by the current interface
        if (capCh >= maxCapCh) {
            continue;
        }
        // Each live strip must have unique physical capture channel(s)
        if (usedInputs.count(capCh)) {
            continue;
        }

        AudioStripConfig strip = ui_make_strip_config_for_channel(channel);
        strip.captureChannel = capCh;
        if (channel.format == ChannelFormat::Stereo) {
            const uint32_t capChR = channel.input_source_index_r >= 0
                ? static_cast<uint32_t>(channel.input_source_index_r)
                : (capCh + 1u);
            if (capChR >= maxCapCh || capChR == capCh || usedInputs.count(capChR)) {
                continue;
            }
            usedInputs.insert(capCh);
            usedInputs.insert(capChR);
            strip.captureChannelRight = capChR;
        } else {
            usedInputs.insert(capCh);
        }

        if (channel.id >= 1 && channel.id <= static_cast<int>(RoutingMatrix::NUM_CHANNELS)) {
            strip.routeToMain = g_routing_matrix.get_channel_main_lr(static_cast<size_t>(channel.id - 1));
        }

        // Output routing validation & fallback to Main L/R
        const uint32_t outputChannel = strip.outputRoute == AudioOutputRoute::Aux
            ? 2 + strip.outputRouteIndex : strip.outputRouteIndex;
        const uint32_t requiredChannels = strip.outputRoute == AudioOutputRoute::MainLR ? 2 : 1;
        if (strip.outputRoute == AudioOutputRoute::None ||
            outputChannel >= maxRenCh ||
            requiredChannels > maxRenCh - outputChannel ||
            (strip.outputRoute == AudioOutputRoute::Omni && outputChannel < 2)) {
            strip.outputRoute = AudioOutputRoute::MainLR;
            strip.routeToMain = true;
        }

        strips.push_back(std::move(strip));
    }

    if (strips.empty() && !g_mixer_state.channels.empty()) {
        // Auto-assign CH 01 to IN 1 (capture 0) and CH 02 to IN 2 (capture 1) if unassigned
        if (g_mixer_state.channels.size() >= 1 && maxCapCh >= 1) {
            auto& ch0 = g_mixer_state.channels[0];
            if (ch0.input_source_index < 0) {
                ch0.format = ChannelFormat::Mono;
                ch0.input_source_index = 0;
                std::snprintf(ch0.input_label, sizeof(ch0.input_label), "IN 1");
            }
            ch0.output_route = ChannelOutputRoute::MainLR;
            g_routing_matrix.set_channel_input_source(ch0.id - 1, ch0.input_source_index);
            g_routing_matrix.set_channel_main_lr(static_cast<size_t>(ch0.id - 1), true);

            AudioStripConfig strip = ui_make_strip_config_for_channel(ch0);
            strips.push_back(std::move(strip));
        }
        if (g_mixer_state.channels.size() >= 2 && maxCapCh >= 2) {
            auto& ch1 = g_mixer_state.channels[1];
            if (ch1.input_source_index < 0) {
                ch1.format = ChannelFormat::Mono;
                ch1.input_source_index = 1;
                std::snprintf(ch1.input_label, sizeof(ch1.input_label), "IN 2");
            }
            ch1.output_route = ChannelOutputRoute::MainLR;
            g_routing_matrix.set_channel_input_source(ch1.id - 1, ch1.input_source_index);
            g_routing_matrix.set_channel_main_lr(static_cast<size_t>(ch1.id - 1), true);

            AudioStripConfig strip = ui_make_strip_config_for_channel(ch1);
            strips.push_back(std::move(strip));
        }
    }

    if (strips.empty()) {
        error = "No channels could be mapped to active hardware inputs.";
    }

    return strips;
}

bool ui_start_real_audio(std::string* errorOut) {
    auto& service = audio_service();
    const auto snapshot = service.snapshot();
    if (snapshot.state == AudioServiceState::Streaming || snapshot.state == AudioServiceState::Initializing) {
        return true;
    }

    std::string configError;
    const auto strips = ui_make_live_strip_configuration(configError);
    if (!configError.empty()) {
        if (errorOut) *errorOut = configError;
        return false;
    }
    if (!service.setConsoleConfiguration(strips)) {
        if (errorOut) *errorOut = "Strip configuration rejected: check unique inputs and processor ranges.";
        return false;
    }

    std::array<AudioBusConfig, 4> auxConfigs{};
    for (size_t a = 0; a < 4; ++a) {
        auxConfigs[a] = ui_make_strip_config_for_channel(g_mixer_state.aux_buses[a]);
        auxConfigs[a].routeToMain = g_mixer_state.aux_buses[a].route_to_main;
    }
    service.setAuxBusConfigurations(auxConfigs);

    AudioBusConfig masterCfg = ui_make_strip_config_for_channel(g_mixer_state.master);
    masterCfg.format = AudioStripFormat::Stereo;
    masterCfg.routeToMain = true;
    service.setMasterBusConfiguration(masterCfg);

    if (!service.selectMode(AudioMode::RealAudio)) {
        if (errorOut) *errorOut = "Failed to switch audio mode to Real Audio.";
        return false;
    }

    g_backend.stop();

    auto& setup = ui_get_persisted_audio_setup();
    if (!setup.channels.empty()) {
        const auto& ch = setup.channels.front();
        service.setGainDb(ch.gainDb);
        service.setPan(ch.pan);
        service.setDspBypass(ch.dspBypass);
        service.setPeqEnabled(ch.peqEnabled);
        service.setCompressorEnabled(ch.compressorEnabled);
    }

    if (!service.startRealAudio()) {
        if (errorOut) *errorOut = "Real audio start failed: check device and JACK server status.";
        return false;
    }

    return true;
}

void ui_stop_real_audio() {
    auto& service = audio_service();
    service.stop();
}

void ui_sync_channel_fx_to_dsp(ChannelState* ch) {
    if (!ch || ch == &g_mixer_state.master) return;
    ch->fx_rack.set_owner_context(ch->id, isAuxBusChannel(ch), ch->format == ChannelFormat::Stereo);
    auto cfg = ui_make_strip_config_for_channel(*ch);
    if (isAuxBusChannel(ch)) {
        cfg.routeToMain = ch->route_to_main;
        audio_service().updateLiveAuxBusDsp(static_cast<size_t>(ch->id - 101), cfg);
    } else {
        audio_service().updateLiveStripDsp(cfg);
    }
}
