#include "lv2_core.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>

namespace {

struct OverdrivePlugin {
    double sample_rate = 48000.0;

    // Port data pointers
    const float* in_l = nullptr;
    const float* in_r = nullptr;
    float* out_l = nullptr;
    float* out_r = nullptr;
    const float* drive = nullptr;
    const float* tone = nullptr;
    const float* level = nullptr;

    // Filter states
    float filter_state_l = 0.0f;
    float filter_state_r = 0.0f;
};

LV2_Handle instantiate(const LV2_Descriptor* /*descriptor*/,
                       double sample_rate,
                       const char* /*bundle_path*/,
                       const LV2_Feature* const* /*features*/) {
    auto* plugin = new (std::nothrow) OverdrivePlugin();
    if (plugin) {
        plugin->sample_rate = sample_rate > 0.0 ? sample_rate : 48000.0;
    }
    return static_cast<LV2_Handle>(plugin);
}

void connect_port(LV2_Handle instance, uint32_t port, void* data_location) {
    auto* plugin = static_cast<OverdrivePlugin*>(instance);
    if (!plugin) return;

    switch (port) {
        case 0: plugin->in_l = static_cast<const float*>(data_location); break;
        case 1: plugin->in_r = static_cast<const float*>(data_location); break;
        case 2: plugin->out_l = static_cast<float*>(data_location); break;
        case 3: plugin->out_r = static_cast<float*>(data_location); break;
        case 4: plugin->drive = static_cast<const float*>(data_location); break;
        case 5: plugin->tone = static_cast<const float*>(data_location); break;
        case 6: plugin->level = static_cast<const float*>(data_location); break;
        default: break;
    }
}

void activate(LV2_Handle instance) {
    auto* plugin = static_cast<OverdrivePlugin*>(instance);
    if (plugin) {
        plugin->filter_state_l = 0.0f;
        plugin->filter_state_r = 0.0f;
    }
}

void run(LV2_Handle instance, uint32_t sample_count) {
    auto* plugin = static_cast<OverdrivePlugin*>(instance);
    if (!plugin) return;

    const float* in_l = plugin->in_l;
    const float* in_r = plugin->in_r;
    float* out_l = plugin->out_l;
    float* out_r = plugin->out_r;

    if (!in_l || !out_l) return;

    const float drive_val = plugin->drive ? std::clamp(*plugin->drive, 0.0f, 1.0f) : 0.3f;
    const float tone_val = plugin->tone ? std::clamp(*plugin->tone, 0.0f, 1.0f) : 0.5f;
    const float level_val = plugin->level ? std::clamp(*plugin->level, 0.0f, 2.0f) : 1.0f;

    // Gain boost from drive
    const float gain = 1.0f + drive_val * 9.0f;

    // Tone filter coefficient: tone 0.0 = dark (1 kHz), tone 1.0 = bright (12 kHz)
    const float cutoff_hz = 1000.0f + tone_val * 11000.0f;
    const float alpha = std::clamp(static_cast<float>(2.0 * M_PI * cutoff_hz / plugin->sample_rate), 0.01f, 0.99f);

    auto saturate = [](float x) -> float {
        // Soft asymmetric saturation curve
        float v = x;
        if (v > 1.5f) v = 1.5f;
        if (v < -1.5f) v = -1.5f;
        return v / (1.0f + std::abs(v) * 0.7f);
    };

    for (uint32_t i = 0; i < sample_count; ++i) {
        // Left channel
        float x_l = in_l[i] * gain;
        float sat_l = saturate(x_l);
        plugin->filter_state_l += alpha * (sat_l - plugin->filter_state_l);
        out_l[i] = plugin->filter_state_l * level_val;

        // Right channel
        if (in_r && out_r) {
            float x_r = in_r[i] * gain;
            float sat_r = saturate(x_r);
            plugin->filter_state_r += alpha * (sat_r - plugin->filter_state_r);
            out_r[i] = plugin->filter_state_r * level_val;
        }
    }
}

void deactivate(LV2_Handle instance) {
    auto* plugin = static_cast<OverdrivePlugin*>(instance);
    if (plugin) {
        plugin->filter_state_l = 0.0f;
        plugin->filter_state_r = 0.0f;
    }
}

void cleanup(LV2_Handle instance) {
    auto* plugin = static_cast<OverdrivePlugin*>(instance);
    delete plugin;
}

const void* extension_data(const char* /*uri*/) {
    return nullptr;
}

static const LV2_Descriptor g_descriptor = {
    "http://prismix.audio/plugins/overdrive",
    instantiate,
    connect_port,
    activate,
    run,
    deactivate,
    cleanup,
    extension_data
};

} // namespace

#if defined(_WIN32)
#define LV2_SYMBOL_EXPORT __declspec(dllexport)
#else
#define LV2_SYMBOL_EXPORT __attribute__((visibility("default")))
#endif

extern "C" LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) {
    if (index == 0) return &g_descriptor;
    return nullptr;
}