#include "theme_model.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {
ThemeConfig s_themeConfig;
bool s_initialized = false;
void (*s_changeCallback)() = nullptr;

void notify_change() {
    if (s_changeCallback) {
        s_changeCallback();
    }
}
} // namespace

uint32_t ThemeColors::get_role(ThemeColorRole role) const {
    switch (role) {
        case ThemeColorRole::Background:      return background;
        case ThemeColorRole::Panel:           return panel;
        case ThemeColorRole::ControlSurface:  return control_surface;
        case ThemeColorRole::RecessedSurface: return recessed_surface;
        case ThemeColorRole::StandardBorder:  return standard_border;
        case ThemeColorRole::SelectedBorder:  return selected_border;
        case ThemeColorRole::TextPrimary:     return text_primary;
        case ThemeColorRole::TextSecondary:   return text_secondary;
        case ThemeColorRole::KnobRing:        return knob_ring;
        case ThemeColorRole::KnobPointer:     return knob_pointer;
        case ThemeColorRole::AccentPrimary:   return accent_primary;
        case ThemeColorRole::AccentSecondary: return accent_secondary;
        default:                              return 0x000000;
    }
}

void ThemeColors::set_role(ThemeColorRole role, uint32_t hex) {
    hex &= 0xFFFFFF;
    switch (role) {
        case ThemeColorRole::Background:      background = hex; break;
        case ThemeColorRole::Panel:           panel = hex; break;
        case ThemeColorRole::ControlSurface:  control_surface = hex; break;
        case ThemeColorRole::RecessedSurface: recessed_surface = hex; break;
        case ThemeColorRole::StandardBorder:  standard_border = hex; break;
        case ThemeColorRole::SelectedBorder:  selected_border = hex; break;
        case ThemeColorRole::TextPrimary:     text_primary = hex; break;
        case ThemeColorRole::TextSecondary:   text_secondary = hex; break;
        case ThemeColorRole::KnobRing:        knob_ring = hex; break;
        case ThemeColorRole::KnobPointer:     knob_pointer = hex; break;
        case ThemeColorRole::AccentPrimary:   accent_primary = hex; break;
        case ThemeColorRole::AccentSecondary: accent_secondary = hex; break;
        default: break;
    }
}

const char* theme_role_name(ThemeColorRole role) {
    switch (role) {
        case ThemeColorRole::Background:      return "Main Background";
        case ThemeColorRole::Panel:           return "Panel Background";
        case ThemeColorRole::ControlSurface:  return "Raised Control Surface";
        case ThemeColorRole::RecessedSurface: return "Recessed Surface";
        case ThemeColorRole::StandardBorder:  return "Standard Border";
        case ThemeColorRole::SelectedBorder:  return "Selected Border / Highlight";
        case ThemeColorRole::TextPrimary:     return "Primary Text";
        case ThemeColorRole::TextSecondary:   return "Secondary Text";
        case ThemeColorRole::KnobRing:        return "Knob Outer Ring";
        case ThemeColorRole::KnobPointer:     return "Knob Pointer";
        case ThemeColorRole::AccentPrimary:   return "Primary Accent";
        case ThemeColorRole::AccentSecondary: return "Secondary Accent";
        default:                              return "Unknown Role";
    }
}

const char* theme_role_key(ThemeColorRole role) {
    switch (role) {
        case ThemeColorRole::Background:      return "BACKGROUND";
        case ThemeColorRole::Panel:           return "PANEL";
        case ThemeColorRole::ControlSurface:  return "CONTROL_SURFACE";
        case ThemeColorRole::RecessedSurface: return "RECESSED_SURFACE";
        case ThemeColorRole::StandardBorder:  return "STANDARD_BORDER";
        case ThemeColorRole::SelectedBorder:  return "SELECTED_BORDER";
        case ThemeColorRole::TextPrimary:     return "TEXT_PRIMARY";
        case ThemeColorRole::TextSecondary:   return "TEXT_SECONDARY";
        case ThemeColorRole::KnobRing:        return "KNOB_RING";
        case ThemeColorRole::KnobPointer:     return "KNOB_POINTER";
        case ThemeColorRole::AccentPrimary:   return "ACCENT_PRIMARY";
        case ThemeColorRole::AccentSecondary: return "ACCENT_SECONDARY";
        default:                              return "UNKNOWN";
    }
}

const char* theme_preset_name(ThemePreset preset) {
    switch (preset) {
        case ThemePreset::Graphite:        return "Graphite";
        case ThemePreset::LexiconInspired: return "Lexicon-inspired";
        case ThemePreset::StudioBlue:      return "Studio Blue";
        case ThemePreset::Monochrome:      return "Monochrome";
        case ThemePreset::Custom:          return "Custom";
        default:                           return "Graphite";
    }
}

const char* theme_preset_description(ThemePreset preset) {
    switch (preset) {
        case ThemePreset::Graphite:
            return "Neutral dark charcoal chassis, subtle grey borders & silver indicators.";
        case ThemePreset::LexiconInspired:
            return "Bronze-charcoal metal panels, restrained champagne accents & ivory LCD.";
        case ThemePreset::StudioBlue:
            return "Deep slate graphite surfaces, muted studio blue highlights & silver readouts.";
        case ThemePreset::Monochrome:
            return "High-contrast pure black and neutral greys with zero decorative color tint.";
        case ThemePreset::Custom:
            return "User-configured custom semantic colors saved to persistent local storage.";
        default:
            return "";
    }
}

ThemeColors theme_get_preset_colors(ThemePreset preset) {
    ThemeColors c;
    switch (preset) {
        case ThemePreset::Graphite:
            c.background        = 0x171817;
            c.panel             = 0x242522;
            c.control_surface   = 0x30312D;
            c.recessed_surface  = 0x111210;
            c.standard_border   = 0x454641;
            c.selected_border   = 0xBFC2BA;
            c.text_primary      = 0xD6D6D0;
            c.text_secondary    = 0xA3A49D;
            c.knob_ring         = 0x777970;
            c.knob_pointer      = 0xD0D1C9;
            c.accent_primary    = 0xBFC2BA;
            c.accent_secondary  = 0x5EA8B6;
            break;

        case ThemePreset::LexiconInspired:
            c.background        = 0x181816;
            c.panel             = 0x262522;
            c.control_surface   = 0x34332D;
            c.recessed_surface  = 0x10120F;
            c.standard_border   = 0x4A483E;
            c.selected_border   = 0xC6AE73; // warm champagne highlight
            c.text_primary      = 0xD8D0B4; // warm ivory LCD text
            c.text_secondary    = 0xA8A396;
            c.knob_ring         = 0x77756B;
            c.knob_pointer      = 0xD8D0B4;
            c.accent_primary    = 0xC6AE73;
            c.accent_secondary  = 0x5EA8B6;
            break;

        case ThemePreset::StudioBlue:
            c.background        = 0x14181D;
            c.panel             = 0x1E242C;
            c.control_surface   = 0x2B333E;
            c.recessed_surface  = 0x0E1216;
            c.standard_border   = 0x3D4A59;
            c.selected_border   = 0x4A90E2; // studio blue highlight
            c.text_primary      = 0xE0E6EC;
            c.text_secondary    = 0x94A4B4;
            c.knob_ring         = 0x5A6B7C;
            c.knob_pointer      = 0xD4DEE8;
            c.accent_primary    = 0x4A90E2;
            c.accent_secondary  = 0x5E9CB6;
            break;

        case ThemePreset::Monochrome:
            c.background        = 0x121212;
            c.panel             = 0x1E1E1E;
            c.control_surface   = 0x2C2C2C;
            c.recessed_surface  = 0x0A0A0A;
            c.standard_border   = 0x404040;
            c.selected_border   = 0xE0E0E0;
            c.text_primary      = 0xE8E8E8;
            c.text_secondary    = 0x888888;
            c.knob_ring         = 0x606060;
            c.knob_pointer      = 0xD8D8D8;
            c.accent_primary    = 0xCCCCCC;
            c.accent_secondary  = 0xAAAAAA;
            break;

        case ThemePreset::Custom:
            c = s_themeConfig.custom_colors;
            break;

        default:
            c = theme_get_preset_colors(ThemePreset::Graphite);
            break;
    }
    return c;
}

bool parse_hex_color(const std::string& str, uint32_t& out_hex) {
    if (str.empty()) return false;
    size_t start = 0;
    if (str[0] == '#') start = 1;
    else if (str.size() >= 2 && str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) start = 2;

    std::string hex = str.substr(start);
    if (hex.size() != 6) return false;

    for (char c : hex) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    }

    char* end = nullptr;
    unsigned long val = std::strtoul(hex.c_str(), &end, 16);
    if (!end || *end != '\0' || val > 0xFFFFFF) return false;

    out_hex = static_cast<uint32_t>(val);
    return true;
}

std::string format_hex_color(uint32_t hex) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%06X", hex & 0xFFFFFF);
    return std::string(buf);
}

std::string default_theme_config_path() {
    const char* appData = std::getenv("APPDATA");
    std::filesystem::path base = (appData && *appData) ? std::filesystem::path(appData)
                                                       : std::filesystem::current_path();
    base /= "AudioCore";
    std::error_code ec;
    std::filesystem::create_directories(base, ec);
    return (base / "ui_theme_v1.cfg").string();
}

bool save_theme_config(const ThemeConfig& config, const std::string& path, std::string* error) {
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::path(path).parent_path();
    if (!dir.empty()) {
        std::filesystem::create_directories(dir, ec);
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        if (error) *error = "Could not open theme configuration file for writing.";
        return false;
    }

    out << "PRISMIX_THEME " << config.version << "\n";
    out << "PRESET " << static_cast<int>(config.active_preset) << "\n";
    out << "BACKGROUND " << format_hex_color(config.custom_colors.background) << "\n";
    out << "PANEL " << format_hex_color(config.custom_colors.panel) << "\n";
    out << "CONTROL_SURFACE " << format_hex_color(config.custom_colors.control_surface) << "\n";
    out << "RECESSED_SURFACE " << format_hex_color(config.custom_colors.recessed_surface) << "\n";
    out << "STANDARD_BORDER " << format_hex_color(config.custom_colors.standard_border) << "\n";
    out << "SELECTED_BORDER " << format_hex_color(config.custom_colors.selected_border) << "\n";
    out << "TEXT_PRIMARY " << format_hex_color(config.custom_colors.text_primary) << "\n";
    out << "TEXT_SECONDARY " << format_hex_color(config.custom_colors.text_secondary) << "\n";
    out << "KNOB_RING " << format_hex_color(config.custom_colors.knob_ring) << "\n";
    out << "KNOB_POINTER " << format_hex_color(config.custom_colors.knob_pointer) << "\n";
    out << "ACCENT_PRIMARY " << format_hex_color(config.custom_colors.accent_primary) << "\n";
    out << "ACCENT_SECONDARY " << format_hex_color(config.custom_colors.accent_secondary) << "\n";

    if (!out) {
        if (error) *error = "Failed to write theme configuration data.";
        return false;
    }
    return true;
}

bool load_theme_config(const std::string& path, ThemeConfig& config, std::string* error) {
    std::ifstream in(path);
    if (!in) {
        if (error) *error = "Theme configuration file not found.";
        return false;
    }

    std::string magic;
    uint32_t version = 0;
    if (!(in >> magic >> version) || magic != "PRISMIX_THEME") {
        if (error) *error = "Invalid theme configuration header.";
        return false;
    }
    if (version != 1) {
        if (error) *error = "Unsupported theme configuration version.";
        return false;
    }

    ThemeConfig candidate;
    candidate.version = version;
    candidate.active_preset = ThemePreset::Graphite;
    candidate.custom_colors = theme_get_preset_colors(ThemePreset::Graphite);

    std::string key, val;
    while (in >> key >> val) {
        if (key == "PRESET") {
            try {
                int p = std::stoi(val);
                if (p >= 0 && p < static_cast<int>(ThemePreset::Count)) {
                    candidate.active_preset = static_cast<ThemePreset>(p);
                }
            } catch (...) {}
        } else {
            uint32_t hex = 0;
            if (parse_hex_color(val, hex)) {
                if (key == "BACKGROUND") candidate.custom_colors.background = hex;
                else if (key == "PANEL") candidate.custom_colors.panel = hex;
                else if (key == "CONTROL_SURFACE") candidate.custom_colors.control_surface = hex;
                else if (key == "RECESSED_SURFACE") candidate.custom_colors.recessed_surface = hex;
                else if (key == "STANDARD_BORDER") candidate.custom_colors.standard_border = hex;
                else if (key == "SELECTED_BORDER") candidate.custom_colors.selected_border = hex;
                else if (key == "TEXT_PRIMARY") candidate.custom_colors.text_primary = hex;
                else if (key == "TEXT_SECONDARY") candidate.custom_colors.text_secondary = hex;
                else if (key == "KNOB_RING") candidate.custom_colors.knob_ring = hex;
                else if (key == "KNOB_POINTER") candidate.custom_colors.knob_pointer = hex;
                else if (key == "ACCENT_PRIMARY") candidate.custom_colors.accent_primary = hex;
                else if (key == "ACCENT_SECONDARY") candidate.custom_colors.accent_secondary = hex;
            }
        }
    }

    config = candidate;
    return true;
}

ThemeConfig& theme_get_config() {
    if (!s_initialized) {
        theme_init_and_load();
    }
    return s_themeConfig;
}

ThemeColors theme_get_active_colors() {
    auto& cfg = theme_get_config();
    if (cfg.active_preset == ThemePreset::Custom) {
        return cfg.custom_colors;
    }
    return theme_get_preset_colors(cfg.active_preset);
}

uint32_t theme_get_color_hex(ThemeColorRole role) {
    return theme_get_active_colors().get_role(role);
}

void theme_set_preset(ThemePreset preset) {
    if (preset >= ThemePreset::Count) return;
    auto& cfg = theme_get_config();
    cfg.active_preset = preset;
    if (preset != ThemePreset::Custom) {
        // Populate custom_colors with the chosen preset as starting baseline
        cfg.custom_colors = theme_get_preset_colors(preset);
    }
    std::string err;
    save_theme_config(cfg, default_theme_config_path(), &err);
    notify_change();
}

void theme_set_custom_color(ThemeColorRole role, uint32_t hex) {
    if (role >= ThemeColorRole::Count) return;
    auto& cfg = theme_get_config();
    if (cfg.active_preset != ThemePreset::Custom) {
        // Copy current preset colors to custom before editing
        cfg.custom_colors = theme_get_preset_colors(cfg.active_preset);
        cfg.active_preset = ThemePreset::Custom;
    }
    cfg.custom_colors.set_role(role, hex);
    std::string err;
    save_theme_config(cfg, default_theme_config_path(), &err);
    notify_change();
}

void theme_reset_to_preset(ThemePreset preset) {
    if (preset >= ThemePreset::Count || preset == ThemePreset::Custom) {
        preset = ThemePreset::Graphite;
    }
    auto& cfg = theme_get_config();
    cfg.active_preset = preset;
    cfg.custom_colors = theme_get_preset_colors(preset);
    std::string err;
    save_theme_config(cfg, default_theme_config_path(), &err);
    notify_change();
}

void theme_reset_all_defaults() {
    auto& cfg = theme_get_config();
    cfg.version = 1;
    cfg.active_preset = ThemePreset::Graphite;
    cfg.custom_colors = theme_get_preset_colors(ThemePreset::Graphite);
    std::string err;
    save_theme_config(cfg, default_theme_config_path(), &err);
    notify_change();
}

void theme_register_change_callback(void (*cb)()) {
    s_changeCallback = cb;
}

void theme_init_and_load() {
    s_themeConfig.version = 1;
    s_themeConfig.active_preset = ThemePreset::Graphite;
    s_themeConfig.custom_colors = theme_get_preset_colors(ThemePreset::Graphite);

    std::string err;
    std::string path = default_theme_config_path();
    ThemeConfig loaded;
    if (load_theme_config(path, loaded, &err)) {
        s_themeConfig = loaded;
    } else {
        // Fall back to Graphite and save
        save_theme_config(s_themeConfig, path, nullptr);
    }
    s_initialized = true;
}

