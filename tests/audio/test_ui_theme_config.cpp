#include "../../src/ui/theme/theme_model.h"

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

namespace {
bool check(bool ok, const char* message) {
    if (!ok) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return ok;
}
} // namespace

int main() {
    bool ok = true;
    std::cout << "Starting PRISMIX UI Theme Configuration Tests...\n";

    // -------------------------------------------------------------------------
    // 1. Preset Palette Definitions & Distinct Colors
    // -------------------------------------------------------------------------
    const auto graphite = theme_get_preset_colors(ThemePreset::Graphite);
    const auto lexicon  = theme_get_preset_colors(ThemePreset::LexiconInspired);
    const auto studio   = theme_get_preset_colors(ThemePreset::StudioBlue);
    const auto mono     = theme_get_preset_colors(ThemePreset::Monochrome);

    ok &= check(graphite.background == 0x171817, "Graphite background is 0x171817");
    ok &= check(graphite.selected_border == 0xBFC2BA, "Graphite selected border is 0xBFC2BA");
    ok &= check(graphite.text_primary == 0xD6D6D0, "Graphite text primary is 0xD6D6D0");

    ok &= check(lexicon.selected_border == 0xC6AE73, "Lexicon-inspired uses champagne accent 0xC6AE73");
    ok &= check(lexicon.text_primary == 0xD8D0B4, "Lexicon-inspired uses warm ivory LCD text 0xD8D0B4");
    ok &= check(lexicon.recessed_surface == 0x10120F, "Lexicon-inspired uses dark recessed LCD 0x10120F");

    ok &= check(studio.selected_border == 0x4A90E2, "Studio Blue uses blue selection 0x4A90E2");
    ok &= check(mono.background == 0x121212, "Monochrome uses high contrast 0x121212");
    ok &= check(mono.selected_border == 0xE0E0E0, "Monochrome uses neutral light grey border 0xE0E0E0");

    // Verify all presets are unique
    ok &= check(graphite.selected_border != lexicon.selected_border, "Graphite and Lexicon have distinct borders");
    ok &= check(graphite.selected_border != studio.selected_border, "Graphite and Studio Blue have distinct borders");
    ok &= check(graphite.selected_border != mono.selected_border, "Graphite and Monochrome have distinct borders");

    // -------------------------------------------------------------------------
    // 2. Hex Parsing & Formatting Validation
    // -------------------------------------------------------------------------
    uint32_t parsed = 0;
    ok &= check(parse_hex_color("#171817", parsed) && parsed == 0x171817, "Parse #171817");
    ok &= check(parse_hex_color("0x4A90E2", parsed) && parsed == 0x4A90E2, "Parse 0x4A90E2");
    ok &= check(parse_hex_color("C6AE73", parsed) && parsed == 0xC6AE73, "Parse C6AE73 without prefix");
    ok &= check(parse_hex_color("#bfc2ba", parsed) && parsed == 0xBFC2BA, "Parse lowercase #bfc2ba");
    ok &= check(format_hex_color(0xBFC2BA) == "#BFC2BA", "Format 0xBFC2BA to #BFC2BA");
    ok &= check(format_hex_color(0x000000) == "#000000", "Format 0x000000 to #000000");

    // Invalid hex cases
    ok &= check(!parse_hex_color("", parsed), "Reject empty string");
    ok &= check(!parse_hex_color("#12345", parsed), "Reject 5-digit hex");
    ok &= check(!parse_hex_color("#1234567", parsed), "Reject 7-digit hex");
    ok &= check(!parse_hex_color("#ZZZZZZ", parsed), "Reject non-hex characters");
    ok &= check(!parse_hex_color("not_a_color", parsed), "Reject random string");

    // -------------------------------------------------------------------------
    // 3. Semantic Role Mapping (12 Roles)
    // -------------------------------------------------------------------------
    ThemeColors testColors = graphite;
    for (int r = 0; r < static_cast<int>(ThemeColorRole::Count); ++r) {
        auto role = static_cast<ThemeColorRole>(r);
        const char* name = theme_role_name(role);
        const char* key  = theme_role_key(role);
        ok &= check(name != nullptr && name[0] != '\0', "Role has valid name");
        ok &= check(key != nullptr && key[0] != '\0', "Role has valid key");

        // Test get and set
        testColors.set_role(role, 0x123400 + r);
        ok &= check(testColors.get_role(role) == (0x123400u + r), "get_role matches set_role for role");
    }
    ok &= check(static_cast<int>(ThemeColorRole::Count) == 12, "Exactly 12 semantic color roles defined");

    // -------------------------------------------------------------------------
    // 4. Serialization & Round-Trip
    // -------------------------------------------------------------------------
    const std::string testPath = "theme_test_config.cfg";
    ThemeConfig cfgOut;
    cfgOut.version = 1;
    cfgOut.active_preset = ThemePreset::Custom;
    cfgOut.custom_colors.background       = 0x1A1B1C;
    cfgOut.custom_colors.panel            = 0x2A2B2C;
    cfgOut.custom_colors.control_surface  = 0x3A3B3C;
    cfgOut.custom_colors.recessed_surface = 0x0A0B0C;
    cfgOut.custom_colors.standard_border  = 0x4A4B4C;
    cfgOut.custom_colors.selected_border  = 0x5A5B5C;
    cfgOut.custom_colors.text_primary     = 0xDADBDC;
    cfgOut.custom_colors.text_secondary   = 0x9A9B9C;
    cfgOut.custom_colors.knob_ring        = 0x7A7B7C;
    cfgOut.custom_colors.knob_pointer     = 0xEAEBEC;
    cfgOut.custom_colors.accent_primary   = 0x6A6B6C;
    cfgOut.custom_colors.accent_secondary = 0x8A8B8C;

    std::string err;
    ok &= check(save_theme_config(cfgOut, testPath, &err), "Save custom theme config");

    ThemeConfig cfgIn;
    ok &= check(load_theme_config(testPath, cfgIn, &err), "Load custom theme config");
    ok &= check(cfgIn.version == 1, "Loaded version is 1");
    ok &= check(cfgIn.active_preset == ThemePreset::Custom, "Loaded active preset is Custom");
    ok &= check(cfgIn.custom_colors.background == 0x1A1B1C, "Loaded background matches");
    ok &= check(cfgIn.custom_colors.panel == 0x2A2B2C, "Loaded panel matches");
    ok &= check(cfgIn.custom_colors.control_surface == 0x3A3B3C, "Loaded control_surface matches");
    ok &= check(cfgIn.custom_colors.recessed_surface == 0x0A0B0C, "Loaded recessed_surface matches");
    ok &= check(cfgIn.custom_colors.standard_border == 0x4A4B4C, "Loaded standard_border matches");
    ok &= check(cfgIn.custom_colors.selected_border == 0x5A5B5C, "Loaded selected_border matches");
    ok &= check(cfgIn.custom_colors.text_primary == 0xDADBDC, "Loaded text_primary matches");
    ok &= check(cfgIn.custom_colors.text_secondary == 0x9A9B9C, "Loaded text_secondary matches");
    ok &= check(cfgIn.custom_colors.knob_ring == 0x7A7B7C, "Loaded knob_ring matches");
    ok &= check(cfgIn.custom_colors.knob_pointer == 0xEAEBEC, "Loaded knob_pointer matches");
    ok &= check(cfgIn.custom_colors.accent_primary == 0x6A6B6C, "Loaded accent_primary matches");
    ok &= check(cfgIn.custom_colors.accent_secondary == 0x8A8B8C, "Loaded accent_secondary matches");

    std::remove(testPath.c_str());

    // -------------------------------------------------------------------------
    // 5. Invalid Configuration Fallback
    // -------------------------------------------------------------------------
    const std::string badPath = "theme_bad_config.cfg";
    {
        std::ofstream bad(badPath);
        bad << "CORRUPT_HEADER 99\n";
        bad << "PRESET 999\n";
    }
    ThemeConfig fallbackCfg;
    ok &= check(!load_theme_config(badPath, fallbackCfg, &err), "Corrupted header rejected");
    std::remove(badPath.c_str());

    // Non-existent file
    ok &= check(!load_theme_config("non_existent_file_12345.cfg", fallbackCfg, &err), "Nonexistent file fails cleanly");

    // -------------------------------------------------------------------------
    // 6. Runtime Preset Switching & Reset Logic
    // -------------------------------------------------------------------------
    theme_reset_all_defaults();
    ok &= check(theme_get_config().active_preset == ThemePreset::Graphite, "Reset all defaults sets Graphite");
    ok &= check(theme_get_color_hex(ThemeColorRole::SelectedBorder) == 0xBFC2BA, "Active border is Graphite silver");

    theme_set_preset(ThemePreset::StudioBlue);
    ok &= check(theme_get_config().active_preset == ThemePreset::StudioBlue, "Switched to Studio Blue");
    ok &= check(theme_get_color_hex(ThemeColorRole::SelectedBorder) == 0x4A90E2, "Active border is Studio Blue");

    theme_set_custom_color(ThemeColorRole::SelectedBorder, 0xFF5500);
    ok &= check(theme_get_config().active_preset == ThemePreset::Custom, "Setting custom color switches to Custom preset");
    ok &= check(theme_get_color_hex(ThemeColorRole::SelectedBorder) == 0xFF5500, "Active border is now custom 0xFF5500");

    theme_reset_to_preset(ThemePreset::LexiconInspired);
    ok &= check(theme_get_config().active_preset == ThemePreset::LexiconInspired, "Reset to Lexicon sets Lexicon");
    ok &= check(theme_get_color_hex(ThemeColorRole::SelectedBorder) == 0xC6AE73, "Active border is Lexicon champagne");

    theme_reset_all_defaults();
    ok &= check(theme_get_config().active_preset == ThemePreset::Graphite, "Reset all defaults restores Graphite");

    std::cout << (ok ? "All PRISMIX UI Theme Configuration tests passed (100%).\n"
                     : "Some PRISMIX UI Theme Configuration tests FAILED!\n");
    return ok ? 0 : 1;
}

