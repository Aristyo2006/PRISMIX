#pragma once

#include "../model/audio_setup_config.h"
#include "../service/audio_service.h"
#include <string>
#include <vector>

struct ChannelState;

// Ensures default channel assignments (CH 01 -> IN 1, CH 02 -> IN 2, routed to Main L/R)
// if channels are currently unassigned.
void ui_ensure_default_channel_assignments();

// Builds the live strip configuration vector for AudioService from current mixer state.
std::vector<AudioStripConfig> ui_make_live_strip_configuration(std::string& error);

// Builds an AudioStripConfig for an individual channel.
AudioStripConfig ui_make_strip_config_for_channel(const ChannelState& channel);

// Starts real audio processing via AudioService (sets console config, selects real audio mode, starts engine).
bool ui_start_real_audio(std::string* errorOut = nullptr);

// Stops real audio processing.
void ui_stop_real_audio();

// Access the global persisted audio setup configuration.
AudioSetupConfig& ui_get_persisted_audio_setup();
bool ui_save_persisted_audio_setup(std::string* errorOut = nullptr);
void ui_load_persisted_audio_setup();

// Synchronize a channel or aux bus's FX rack & DSP settings to AudioService in real time.
void ui_sync_channel_fx_to_dsp(ChannelState* ch);

