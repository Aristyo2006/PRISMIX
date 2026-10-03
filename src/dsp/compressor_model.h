#pragma once

float compressor_gain_reduction_db(float input_db, float threshold_db, float ratio, float knee_db, bool enabled) noexcept;
float compressor_envelope(float current_gr_db, float target_gr_db, float attack_ms, float release_ms, float elapsed_ms) noexcept;
