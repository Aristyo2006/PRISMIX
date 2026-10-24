#include "routing_state.h"
#include <algorithm>

RoutingMatrix g_routing_matrix;

namespace {
const char* kInputSources[16] = {
    "ANALOG IN 01", "ANALOG IN 02", "ANALOG IN 03", "ANALOG IN 04",
    "ANALOG IN 05", "ANALOG IN 06", "ANALOG IN 07", "ANALOG IN 08",
    "USB PLAY 01",  "USB PLAY 02",  "USB PLAY 03",  "USB PLAY 04",
    "AES/EBU L",    "AES/EBU R",    "SIG GEN SINE", "SIG GEN PINK"
};

const char* kOutputDests[8] = {
    "ANALOG OUT 1 (MAIN L)", "ANALOG OUT 2 (MAIN R)",
    "ANALOG OUT 3 (BUS 1)",  "ANALOG OUT 4 (BUS 2)",
    "ANALOG OUT 5 (AUX 1)",  "ANALOG OUT 6 (AUX 2)",
    "ANALOG OUT 7 (CUE L)",  "ANALOG OUT 8 (CUE R)"
};

const char* kBuses[8] = {
    "BUS 01 (DRUMS)", "BUS 02 (MUSIC)", "BUS 03 (VOCALS)", "BUS 04 (EFFECTS)",
    "BUS 05 (STREAM)", "BUS 06 (RECORD)", "BUS 07 (MON L)", "BUS 08 (MON R)"
};
}

RoutingMatrix::RoutingMatrix() {
    // Start with an empty console: channel strips and their routes are added by the user.
    for (size_t i = 0; i < NUM_CHANNELS; ++i) {
        channel_input_sources_[i] = -1;
        channel_main_lr_[i] = false;
        channel_bus_matrix_[i].fill(false);
    }
    for (size_t b = 0; b < NUM_BUSES; ++b) {
        bus_main_lr_[b] = true;
    }
    // Default output patch
    output_patch_[0] = 8; // MAIN L
    output_patch_[1] = 9; // MAIN R
    output_patch_[2] = 0; // BUS 1
    output_patch_[3] = 1; // BUS 2
    output_patch_[4] = 10; // AUX 1
    output_patch_[5] = 11; // AUX 2
    output_patch_[6] = 8; // CUE L
    output_patch_[7] = 9; // CUE R
}

int RoutingMatrix::get_channel_input_source(size_t ch) const {
    if (ch < NUM_CHANNELS) return channel_input_sources_[ch];
    return -1;
}

void RoutingMatrix::set_channel_input_source(size_t ch, int input_source) {
    if (ch < NUM_CHANNELS) channel_input_sources_[ch] = std::max(-1, input_source);
}

bool RoutingMatrix::get_channel_bus(size_t ch, size_t bus) const {
    if (ch < NUM_CHANNELS && bus < NUM_BUSES) {
        return channel_bus_matrix_[ch][bus];
    }
    return false;
}

void RoutingMatrix::set_channel_bus(size_t ch, size_t bus, bool connected) {
    if (ch < NUM_CHANNELS && bus < NUM_BUSES) {
        channel_bus_matrix_[ch][bus] = connected;
    }
}

void RoutingMatrix::toggle_channel_bus(size_t ch, size_t bus) {
    if (ch < NUM_CHANNELS && bus < NUM_BUSES) {
        channel_bus_matrix_[ch][bus] = !channel_bus_matrix_[ch][bus];
    }
}

bool RoutingMatrix::get_channel_main_lr(size_t ch) const {
    if (ch < NUM_CHANNELS) return channel_main_lr_[ch];
    return false;
}

void RoutingMatrix::set_channel_main_lr(size_t ch, bool connected) {
    if (ch < NUM_CHANNELS) channel_main_lr_[ch] = connected;
}

bool RoutingMatrix::get_bus_main_lr(size_t bus) const {
    if (bus < NUM_BUSES) return bus_main_lr_[bus];
    return false;
}

void RoutingMatrix::set_bus_main_lr(size_t bus, bool connected) {
    if (bus < NUM_BUSES) bus_main_lr_[bus] = connected;
}

int RoutingMatrix::get_output_patch(size_t out_port) const {
    if (out_port < NUM_OUTPUTS) return output_patch_[out_port];
    return 0;
}

void RoutingMatrix::set_output_patch(size_t out_port, int source_id) {
    if (out_port < NUM_OUTPUTS) {
        output_patch_[out_port] = source_id;
    }
}

const char* RoutingMatrix::get_input_source_name(int idx) {
    if (idx < 0) return "UNASSIGNED";
    if (idx < 16) return kInputSources[idx];
    return "UNKNOWN";
}

const char* RoutingMatrix::get_output_destination_name(size_t idx) {
    if (idx < 8) return kOutputDests[idx];
    return "UNKNOWN";
}

const char* RoutingMatrix::get_internal_bus_name(size_t idx) {
    if (idx < 8) return kBuses[idx];
    return "UNKNOWN";
}

