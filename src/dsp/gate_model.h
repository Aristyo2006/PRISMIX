#pragma once
struct ChannelState;
void update_gate_state(ChannelState& channel, float input_db, float elapsed_ms) noexcept;
float gate_output_db(float input_db, const ChannelState& channel) noexcept;
