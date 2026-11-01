#pragma once

#include <string>

namespace Prismix {
namespace Cloud {

/**
 * Serializes the current active mixer state (g_mixer_state) into a JSON string.
 */
std::string serialize_mixer_state_to_json();

/**
 * Deserializes a JSON string and applies the configuration into g_mixer_state and g_routing_matrix.
 * Also synchronizes live DSP strips if the audio service is running.
 */
bool deserialize_json_to_mixer_state(const std::string& json_str, std::string* error = nullptr);

} // namespace Cloud
} // namespace Prismix

