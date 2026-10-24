#pragma once
struct ChannelState;
float eq_response_db(float frequency_hz, const ChannelState& channel) noexcept;
float eq_single_band_response_db(float frequency_hz, int band, const ChannelState& channel) noexcept;
