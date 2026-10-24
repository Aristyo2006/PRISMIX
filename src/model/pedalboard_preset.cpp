#include "pedalboard_preset.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>

namespace prismix::pedalboard {

PedalboardPresetManager& PedalboardPresetManager::instance() {
    static PedalboardPresetManager mgr;
    return mgr;
}

PedalboardPresetManager::PedalboardPresetManager() {
    initializeFactoryPresets();
    scanPresets("assets/presets");
}

void PedalboardPresetManager::initializeFactoryPresets() {
    // 1. British Lead Stack
    {
        PedalboardPreset p;
        p.name = "British Lead Stack";
        p.author = "PRISMIX MODEP";
        p.description = "Gate -> Overdrive -> NAM British Lead -> Delay -> Reverb";
        p.slots = {
            {"http://audiocore.org/plugins/gate", false, {{"thresh", -50.0f}, {"range", -60.0f}, {"attack", 2.0f}, {"hold", 20.0f}, {"release", 80.0f}}},
            {"http://prismix.audio/plugins/overdrive", false, {{"drive", 0.45f}, {"tone", 0.60f}, {"level", 1.0f}}},
            {"nam:assets/nam/british_lead_lite.nam", false, {{"in_gain", 0.0f}, {"out_level", -2.0f}}},
            {"http://audiocore.org/plugins/delay", false, {{"time_l", 350.0f}, {"time_r", 500.0f}, {"feedback", 0.30f}, {"mix", 0.20f}}},
            {"http://audiocore.org/plugins/reverb", false, {{"decay", 1.8f}, {"mix", 0.22f}, {"room_type", 1.0f}}}
        };
        presets_.push_back(p);
    }

    // 2. Clean Tweed Ambience
    {
        PedalboardPreset p;
        p.name = "Clean Tweed Ambience";
        p.author = "PRISMIX MODEP";
        p.description = "Compressor -> NAM Clean Tweed -> EQ -> Reverb";
        p.slots = {
            {"http://audiocore.org/plugins/compressor", false, {{"thresh", -16.0f}, {"ratio", 3.0f}, {"attack", 15.0f}, {"release", 100.0f}}},
            {"nam:assets/nam/clean_tweed_lite.nam", false, {{"in_gain", 0.0f}, {"out_level", 0.0f}}},
            {"http://audiocore.org/plugins/eq", false, {{"low_freq", 100.0f}, {"low_gain", 1.5f}, {"high_freq", 8000.0f}, {"high_gain", 2.0f}}},
            {"http://audiocore.org/plugins/reverb", false, {{"decay", 2.8f}, {"mix", 0.35f}, {"room_type", 2.0f}}}
        };
        presets_.push_back(p);
    }

    // 3. Broadcast Vocal Strip
    {
        PedalboardPreset p;
        p.name = "Broadcast Vocal Strip";
        p.author = "PRISMIX Console";
        p.description = "Gate -> EQ -> Compressor -> AFS -> Limiter";
        p.slots = {
            {"http://audiocore.org/plugins/gate", false, {{"thresh", -48.0f}, {"range", -60.0f}, {"attack", 5.0f}, {"release", 120.0f}}},
            {"http://audiocore.org/plugins/eq", false, {{"low_freq", 80.0f}, {"low_gain", -2.0f}, {"mid2_freq", 3000.0f}, {"mid2_gain", 2.5f}}},
            {"http://audiocore.org/plugins/compressor", false, {{"thresh", -18.0f}, {"ratio", 4.0f}, {"attack", 10.0f}, {"release", 80.0f}}},
            {"http://audiocore.org/plugins/afs", false, {{"sensitivity", 0.80f}, {"max_notches", 8.0f}, {"max_depth", -18.0f}}},
            {"http://audiocore.org/plugins/limiter", false, {{"ceiling", -0.5f}, {"release", 50.0f}}}
        };
        presets_.push_back(p);
    }
}

const PedalboardPreset* PedalboardPresetManager::findPreset(const std::string& name) const {
    for (const auto& p : presets_) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

void PedalboardPresetManager::scanPresets(const std::string& directory) {
    std::error_code ec;
    std::filesystem::path dir(directory);
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) return;

    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() == ".prxp") {
            PedalboardPreset preset;
            if (loadPreset(entry.path().string(), preset)) {
                if (!findPreset(preset.name)) {
                    presets_.push_back(std::move(preset));
                }
            }
        }
    }
}

bool PedalboardPresetManager::applyPresetToRack(const PedalboardPreset& preset, PluginRack& rack) {
    rack.clear();
    for (const auto& s : preset.slots) {
        if (!rack.add_plugin(s.uri)) {
            continue;
        }
        size_t idx = rack.count() - 1;
        auto* inst = rack.get_slot(idx);
        if (!inst) continue;

        inst->set_bypassed(s.bypassed);
        for (const auto& param : s.parameters) {
            inst->set_parameter_value(param.first, param.second);
        }
    }
    return rack.count() > 0;
}

PedalboardPreset PedalboardPresetManager::captureRack(const std::string& name,
                                                     const PluginRack& rack,
                                                     const std::string& author,
                                                     const std::string& desc) {
    PedalboardPreset p;
    p.name = name;
    p.author = author;
    p.description = desc;

    for (size_t i = 0; i < rack.count(); ++i) {
        const auto* inst = rack.get_slot(i);
        if (!inst) continue;

        PresetSlotData s;
        s.uri = inst->get_uri();
        s.bypassed = inst->is_bypassed();
        for (size_t k = 0; k < inst->parameter_count(); ++k) {
            const auto* param = inst->get_parameter(k);
            if (param) {
                s.parameters.emplace_back(param->id, param->current_val);
            }
        }
        p.slots.push_back(std::move(s));
    }
    return p;
}

bool PedalboardPresetManager::savePreset(const std::string& filepath, const PedalboardPreset& preset) {
    std::ofstream out(filepath);
    if (!out.is_open()) return false;

    out << "NAME=" << preset.name << "\n";
    out << "AUTHOR=" << preset.author << "\n";
    out << "DESC=" << preset.description << "\n";
    out << "SLOT_COUNT=" << preset.slots.size() << "\n";

    for (size_t i = 0; i < preset.slots.size(); ++i) {
        const auto& s = preset.slots[i];
        out << "SLOT_" << i << "_URI=" << s.uri << "\n";
        out << "SLOT_" << i << "_BYPASS=" << (s.bypassed ? "1" : "0") << "\n";
        out << "SLOT_" << i << "_PARAMS=" << s.parameters.size() << "\n";
        for (const auto& p : s.parameters) {
            out << "P_" << p.first << "=" << p.second << "\n";
        }
    }

    if (findPreset(preset.name) == nullptr) {
        presets_.push_back(preset);
    }
    return true;
}

bool PedalboardPresetManager::loadPreset(const std::string& filepath, PedalboardPreset& out_preset) {
    std::ifstream in(filepath);
    if (!in.is_open()) return false;

    std::string line;
    int current_slot = -1;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        if (key == "NAME") out_preset.name = val;
        else if (key == "AUTHOR") out_preset.author = val;
        else if (key == "DESC") out_preset.description = val;
        else if (key.find("SLOT_") == 0) {
            auto idx_end = key.find('_', 5);
            if (idx_end != std::string::npos) {
                int slot_idx = std::stoi(key.substr(5, idx_end - 5));
                std::string field = key.substr(idx_end + 1);
                while (static_cast<int>(out_preset.slots.size()) <= slot_idx) {
                    out_preset.slots.emplace_back();
                }
                if (field == "URI") out_preset.slots[slot_idx].uri = val;
                else if (field == "BYPASS") out_preset.slots[slot_idx].bypassed = (val == "1");
                current_slot = slot_idx;
            }
        } else if (key.find("P_") == 0 && current_slot >= 0 && current_slot < static_cast<int>(out_preset.slots.size())) {
            std::string param_id = key.substr(2);
            float param_val = std::stof(val);
            out_preset.slots[current_slot].parameters.emplace_back(param_id, param_val);
        }
    }

    return !out_preset.name.empty() && !out_preset.slots.empty();
}

} // namespace prismix::pedalboard
