#include "compressor_model.h"
#include <algorithm>
#include <cmath>

float compressor_gain_reduction_db(float input, float threshold, float ratio, float knee, bool enabled) noexcept {
    if (!enabled) return 0.0f;
    ratio=std::max(1.0f,ratio); knee=std::max(0.0f,knee);
    const float over=input-threshold, slope=1.0f-1.0f/ratio;
    if(knee<=0.0f || over>=knee*.5f) return std::max(0.0f,over*slope);
    if(over<=-knee*.5f) return 0.0f;
    const float x=over+knee*.5f;
    return slope*x*x/(2.0f*knee);
}

float compressor_envelope(float current, float target, float attack_ms, float release_ms, float elapsed_ms) noexcept {
    const float time_ms=target<current?std::max(.1f,attack_ms):std::max(1.0f,release_ms);
    const float alpha=1.0f-std::exp(-std::max(0.0f,elapsed_ms)/time_ms);
    return current+(target-current)*alpha;
}
