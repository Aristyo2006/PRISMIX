#include "mixer_state.h"

#include <array>
MixerState g_mixer_state;

namespace {
struct ChannelNames {
    const char* name;
    const char* input;
};
constexpr std::array<ChannelNames, NUM_CHANNELS> names{{
    {"VOCAL", "IN 01"}, {"GUITAR", "IN 02"}, {"KEYS", "IN 03"}, {"BASS", "IN 04"},
    {"DRUMS", "IN 05"}, {"SYNTH", "IN 06"}, {"AUX", "USB 01"}, {"FX RETURN", "FX L/R"}
}};
}

void mixer_state_init() {
    g_mixer_state = MixerState{};
    for (int i = 0; i < NUM_CHANNELS; ++i) {
        g_mixer_state.channels[i].name = names[i].name;
        g_mixer_state.channels[i].input_label = names[i].input;
        g_mixer_state.channels[i].bus_assign[i] = true;
    }
    g_mixer_state.master.name = "MAIN L/R";
    g_mixer_state.master.input_label = "STEREO BUS";
    g_mixer_state.channels[0].select = true;
}
