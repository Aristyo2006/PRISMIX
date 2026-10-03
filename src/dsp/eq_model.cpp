#include "eq_model.h"
#include "../model/mixer_state.h"
#include <algorithm>
#include <cmath>

float eq_response_db(float hz,const ChannelState& ch) noexcept {
    if(ch.eq_bypass) return 0.0f;
    hz=std::max(1.0f,hz); float db=0.0f;
    for(int b=0;b<4;++b) if(ch.eq_enabled[b]) {
        const float freq=std::max(20.0f,ch.eq_freq[b]);
        const float q=std::max(.1f,ch.eq_q[b]);
        if(ch.eq_shelf[b]&&b==0) db+=ch.eq_gain[b]/(1.0f+std::pow(hz/freq,q*2.0f));
        else if(ch.eq_shelf[b]&&b==3) db+=ch.eq_gain[b]/(1.0f+std::pow(freq/hz,q*2.0f));
        else { const float oct=std::log2(hz/freq); db+=ch.eq_gain[b]*std::exp(-.5f*oct*oct*q); }
    }
    return std::clamp(db,-12.0f,12.0f);
}
