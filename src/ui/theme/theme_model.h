#pragma once

#include <cstdint>
#include <string>

// -----------------------------------------------------------------------------
// PRISMIX Semantic Theme Presets & Color Roles
// -----------------------------------------------------------------------------

enum class ThemePreset : uint8_t {
    Graphite = 0,       // Default: Dark charcoal, neutral grey, neutral silver
    LexiconInspired,    // Dark bronze-grey, champagne accents, warm ivory LCD
    StudioBlue,         // Dark graphite, muted blue selection, neutral grey
    Monochrome,         // Black, charcoal, pure neutral grey, high contrast
    Custom,             // User-configured custom semantic colors
    Count
};

enum class ThemeColorRole : uint8_t {
    Background = 0,         // Main background
    Panel,                  // Main panels and chassis surface
    ControlSurface,         // Raised control surfaces, button bodies
    RecessedSurface,        // Recessed surfaces, LCD background
    StandardBorder,         // Standard borders and subtle outlines
    SelectedBorder,         // Selected control border / active focus highlight
    TextPrimary,            // Primary text, readout values, high contrast
    TextSecondary,          // Secondary technical labels, telemetry
    KnobRing,               // Outer rotary ring, scale markings
    KnobPointer,            // Rotary control pointer indicator
    AccentPrimary,          // Primary decorative accent (champagne/blue/silver)
    AccentSecondary,        // Secondary accent / telemetry steel cyan
    Count
};

struct ThemeColors {
    uint32_t background = 0x171817;
    uint32_t panel = 0x242522;
    uint32_t control_surface = 0x30312D;
    uint32_t recessed_surface = 0x111210;
    uint32_t standard_border = 0x454641;
    uint32_t selected_border = 0xBFC2BA;
    uint32_t text_primary = 0xD6D6D0;
    uint32_t text_secondary = 0xA3A49D;
    uint32_t knob_ring = 0x777970;
    uint32_t knob_pointer = 0xD0D1C9;
    uint32_t accent_primary = 0xBFC2BA;
    uint32_t accent_secondary = 0x5EA8B6;

    uint32_t get_role(ThemeColorRole role) const;
    void set_role(ThemeColorRole role, uint32_t hex);
};

struct ThemeConfig {
    uint32_t version = 1;
    ThemePreset active_preset = ThemePreset::Graphite;
    ThemeColors custom_colors;
};

// Information and metadata helpers
const char* theme_role_name(ThemeColorRole role);
const char* theme_role_key(ThemeColorRole role);
const char* theme_preset_name(ThemePreset preset);
const char* theme_preset_description(ThemePreset preset);
ThemeColors theme_get_preset_colors(ThemePreset preset);

// Color parsing & formatting
bool parse_hex_color(const std::string& str, uint32_t& out_hex);
std::string format_hex_color(uint32_t hex);

// Serialization & persistence
bool save_theme_config(const ThemeConfig& config, const std::string& path, std::string* error = nullptr);
bool load_theme_config(const std::string& path, ThemeConfig& config, std::string* error = nullptr);
std::string default_theme_config_path();

// Runtime state accessors
ThemeConfig& theme_get_config();
ThemeColors theme_get_active_colors();
uint32_t theme_get_color_hex(ThemeColorRole role);

// Actions
void theme_set_preset(ThemePreset preset);
void theme_set_custom_color(ThemeColorRole role, uint32_t hex);
void theme_reset_to_preset(ThemePreset preset);
void theme_reset_all_defaults();
void theme_init_and_load();

