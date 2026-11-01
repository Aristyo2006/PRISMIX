#pragma once

#include <string>

namespace Prismix {
namespace Cloud {

// Supabase Project Credentials
inline constexpr const char* SUPABASE_HOST = "wyohmfeekxknveznrhft.supabase.co";
inline constexpr const char* SUPABASE_URL = "https://wyohmfeekxknveznrhft.supabase.co";
inline constexpr const char* SUPABASE_KEY = "sb_publishable_kCpr57RkPaQy_WIOa2Ivqw_eXweieB5";

// Supabase REST Table & Path
inline constexpr const char* PRESETS_TABLE = "prismix_presets";
inline constexpr const char* REST_API_PATH = "/rest/v1/prismix_presets";

// Schema Version
inline constexpr int CURRENT_PRESET_SCHEMA_VERSION = 1;

} // namespace Cloud
} // namespace Prismix

