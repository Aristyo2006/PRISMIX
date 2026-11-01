#pragma once

#include <string>
#include <vector>
#include <functional>

namespace Prismix {
namespace Cloud {

struct PresetInfo {
    std::string id;
    std::string name;
    std::string description;
    int version = 1;
    std::string created_at;
};

class SupabaseClient {
public:
    using ListCallback = std::function<void(bool success, const std::vector<PresetInfo>& presets, const std::string& error)>;
    using GetConfigCallback = std::function<void(bool success, const std::string& config_json, const std::string& error)>;
    using ActionCallback = std::function<void(bool success, const std::string& error)>;

    // Fetch list of presets (metadata only: id, name, description, created_at)
    static void list_presets_async(ListCallback callback);

    // Fetch full config JSON of a specific preset by its UUID
    static void get_preset_config_async(const std::string& preset_id, GetConfigCallback callback);

    // Save a new preset to Supabase
    static void save_preset_async(const std::string& name, const std::string& description, const std::string& config_json, ActionCallback callback);

    // Update / overwrite an existing preset in Supabase (HTTP PATCH)
    static void update_preset_async(const std::string& preset_id, const std::string& name, const std::string& config_json, ActionCallback callback);

    // Delete a preset from Supabase
    static void delete_preset_async(const std::string& preset_id, ActionCallback callback);
};

} // namespace Cloud
} // namespace Prismix

