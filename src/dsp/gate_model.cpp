#include "gate_model.h"
#include "../model/mixer_state.h"
#include <algorithm>

void update_gate_state(ChannelState& ch, float input_db, float elapsed_ms) noexcept {
    if (!ch.gate_on) {
        ch.gate_open = true;
        ch.gate_state = 1; // Open
        ch.gate_timer_ms = 0.0f;
        return;
    }

    if (input_db > ch.gate_thresh) {
        // Signal is above threshold -> opening
        if (!ch.gate_open) {
            ch.gate_timer_ms += elapsed_ms;
            ch.gate_state = 2; // Transitioning to open
            if (ch.gate_timer_ms >= ch.gate_attack) {
                ch.gate_open = true;
                ch.gate_state = 1; // Fully Open
                ch.gate_timer_ms = 0.0f;
            }
        } else {
            ch.gate_state = 1; // Fully Open
            ch.gate_timer_ms = 0.0f;
        }
    } else {
        // Signal is below threshold -> holding or closing
        if (ch.gate_open) {
            ch.gate_timer_ms += elapsed_ms;
            if (ch.gate_timer_ms < ch.gate_hold) {
                ch.gate_state = 1; // Still holding open
            } else if (ch.gate_timer_ms < (ch.gate_hold + ch.gate_release)) {
                ch.gate_state = 2; // Transitioning to closed
            } else {
                ch.gate_open = false;
                ch.gate_state = 0; // Fully Closed
                ch.gate_timer_ms = 0.0f;
            }
        } else {
            ch.gate_state = 0; // Fully Closed
            ch.gate_timer_ms = 0.0f;
        }
    }
}

float gate_output_db(float input_db, const ChannelState& ch) noexcept {
    if (!ch.gate_on || ch.gate_open) return input_db;
    return std::max(-90.0f, input_db + ch.gate_range);
}
