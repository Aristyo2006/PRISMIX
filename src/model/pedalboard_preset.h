#pragma once

#include "plugin_model.h"
#include <string>
#include <vector>
#include <memory>

namespace prismix::pedalboard {

struct PresetSlotData {
    std::string uri;
    bool bypassed = false;
    std::vector<std::pair<std::string, float>> parameters;
};

struct PedalboardPreset {
    std::string name;
    std::string author;
    std::string description;
    std::vector<PresetSlotData> slots;
};

class PedalboardPresetManager {
public:
    static PedalboardPresetManager& instance();

    void scanPresets(const std::string& directory = "assets/presets");
    const std::vector<PedalboardPreset>& getPresets() const { return presets_; }

    const PedalboardPreset* findPreset(const std::string& name) const;
    bool savePreset(const std::string& filepath, const PedalboardPreset& preset);
    bool loadPreset(const std::string& filepath, PedalboardPreset& out_preset);

    // Apply preset to a rack
    bool applyPresetToRack(const PedalboardPreset& preset, PluginRack& rack);

    // Capture current rack into a preset
    PedalboardPreset captureRack(const std::string& name, const PluginRack& rack,
                                 const std::string& author = "PRISMIX User",
                                 const std::string& desc = "");

private:
    PedalboardPresetManager();
    void initializeFactoryPresets();

    std::vector<PedalboardPreset> presets_;
};

} // namespace prismix::pedalboard
