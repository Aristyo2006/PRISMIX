#pragma once

// -----------------------------------------------------------------------------
// PatchStorage catalog presentation helpers.
//
// Pure, LVGL-free helpers shared by the browser UI and the unit tests:
//  - grid column computation for the responsive plugin-card grid
//  - client-side "State" filter matching (applies to the current result page)
//  - category -> fallback artwork placeholder mapping
//  - verified sort / target option tables for the API
//
// All values here were verified against the live PatchStorage beta API
// (https://patchstorage.com/api/beta). Nothing in this header talks to the
// network, the filesystem or LVGL.
// -----------------------------------------------------------------------------

#include "patchstorage_types.h"

#include <string>
#include <vector>

namespace prismix::patchstorage {

// -----------------------------------------------------------------------------
// Grid layout
// -----------------------------------------------------------------------------

// Columns for the card grid given the measured content width of the scroll
// container. Goal: 3 columns on desktop, 2 at medium widths, 1 when narrow.
constexpr int kGridMinCardWidth = 284;
constexpr int kGridGap = 12;
constexpr int kGridMaxColumns = 3;

inline int compute_grid_columns(int content_w,
                                int min_card_w = kGridMinCardWidth,
                                int gap = kGridGap,
                                int max_cols = kGridMaxColumns) {
    if (content_w <= 0 || min_card_w <= 0) return 1;
    int cols = (content_w + gap) / (min_card_w + gap);
    if (cols < 1) cols = 1;
    if (cols > max_cols) cols = max_cols;
    return cols;
}

// -----------------------------------------------------------------------------
// Client-side state filter (applied to the items of the current page)
// -----------------------------------------------------------------------------

enum class PatchStateFilter {
    Any = 0,
    Installed,
    NotInstalled,
    HostCompatible,
    Incompatible,
};

inline const char* state_filter_label(PatchStateFilter f) {
    switch (f) {
        case PatchStateFilter::Installed: return "INSTALLED";
        case PatchStateFilter::NotInstalled: return "NOT INSTALLED";
        case PatchStateFilter::HostCompatible: return "COMPATIBLE";
        case PatchStateFilter::Incompatible: return "UNSUPPORTED";
        default: return "ANY";
    }
}

inline bool matches_state_filter(const PatchStorageItem& item,
                                 const PatchStatus& status,
                                 HostArch host_arch,
                                 PatchStateFilter filter) {
    switch (filter) {
        case PatchStateFilter::Installed:
            return status.state == PatchInstallState::Installed;
        case PatchStateFilter::NotInstalled:
            return status.state == PatchInstallState::NotInstalled;
        case PatchStateFilter::HostCompatible:
            return item.is_compatible_with_host(host_arch);
        case PatchStateFilter::Incompatible:
            return !item.is_compatible_with_host(host_arch);
        case PatchStateFilter::Any:
        default:
            return true;
    }
}

// -----------------------------------------------------------------------------
// Category -> fallback artwork placeholder mapping
// -----------------------------------------------------------------------------

enum class ArtworkPlaceholder {
    Generic = 0,
    Synth,
    Effect,
    Utility,
    Sampler,
    Sequencer,
    Composition,
    Sound,
    Video,
    Game,
};

inline ArtworkPlaceholder placeholder_for_category_slug(std::string slug) {
    for (char& c : slug) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    if (slug == "synthesizer" || slug == "synth") return ArtworkPlaceholder::Synth;
    if (slug == "effect" || slug == "effects") return ArtworkPlaceholder::Effect;
    if (slug == "utility") return ArtworkPlaceholder::Utility;
    if (slug == "sampler") return ArtworkPlaceholder::Sampler;
    if (slug == "sequencer") return ArtworkPlaceholder::Sequencer;
    if (slug == "composition") return ArtworkPlaceholder::Composition;
    if (slug == "sound") return ArtworkPlaceholder::Sound;
    if (slug == "video") return ArtworkPlaceholder::Video;
    if (slug == "game") return ArtworkPlaceholder::Game;
    return ArtworkPlaceholder::Generic;
}

inline ArtworkPlaceholder placeholder_for_categories(const std::vector<PatchCategory>& cats) {
    for (const auto& c : cats) {
        if (!c.slug.empty()) {
            ArtworkPlaceholder p = placeholder_for_category_slug(c.slug);
            if (p != ArtworkPlaceholder::Generic) return p;
        }
        if (!c.name.empty()) {
            ArtworkPlaceholder p = placeholder_for_category_slug(c.name);
            if (p != ArtworkPlaceholder::Generic) return p;
        }
    }
    return ArtworkPlaceholder::Generic;
}

inline const char* placeholder_caption(ArtworkPlaceholder p, const std::vector<PatchCategory>& /*cats*/) {
    switch (p) {
        case ArtworkPlaceholder::Synth: return "SYNTHESIZER";
        case ArtworkPlaceholder::Effect: return "EFFECT";
        case ArtworkPlaceholder::Utility: return "UTILITY";
        case ArtworkPlaceholder::Sampler: return "SAMPLER";
        case ArtworkPlaceholder::Sequencer: return "SEQUENCER";
        case ArtworkPlaceholder::Composition: return "COMPOSITION";
        case ArtworkPlaceholder::Sound: return "SOUND";
        case ArtworkPlaceholder::Video: return "VIDEO";
        case ArtworkPlaceholder::Game: return "GAME";
        default: return "LV2 PLUGIN";
    }
}

// Short uppercase summary of the item's real target list (for unsupported badges).
inline std::string targets_summary(const PatchStorageItem& item) {
    std::string out;
    auto append = [&out](const std::string& name) {
        if (name.empty()) return;
        if (!out.empty()) out += ", ";
        out += name;
    };
    for (const auto& t : item.targets) append(t.name);
    if (out.empty()) {
        for (const auto& f : item.files) append(f.target.name);
    }
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return out;
}

// Category display name (first real category, or "LV2" when absent).
inline std::string primary_category_name(const PatchStorageItem& item) {
    if (!item.categories.empty() && !item.categories.front().name.empty()) {
        return item.categories.front().name;
    }
    return "LV2";
}

// -----------------------------------------------------------------------------
// Verified API option tables
// -----------------------------------------------------------------------------

// Sort orders accepted by the live API:
//   orderby is one of: author, date, id, modified, relevance, slug, title,
//   view_count, like_count, download_count   (created_at/updated_at rejected)
struct SortOption {
    const char* label;
    const char* orderby;
    const char* order;
};

inline const std::vector<SortOption>& sort_options() {
    static const std::vector<SortOption> opts = {
        {"BEST MATCH", "", ""},
        {"NEWEST", "date", "desc"},
        {"MOST DOWNLOADED", "download_count", "desc"},
        {"MOST LIKED", "like_count", "desc"},
        {"MOST VIEWED", "view_count", "desc"},
        {"NAME (A-Z)", "title", "asc"},
    };
    return opts;
}

// Target ids verified against the LV2 platform (platforms=8046) catalog.
struct TargetOption {
    const char* label;
    int id;
};

inline const std::vector<TargetOption>& target_options() {
    static const std::vector<TargetOption> opts = {
        {"ALL TARGETS", 0},
        {"LINUX AMD64", TARGET_ID_LINUX_AMD64},
        {"RPI AARCH64", TARGET_ID_RPI_AARCH64},
        {"PATCHBOX ARM32", TARGET_ID_PATCHBOX_ARM32},
    };
    return opts;
}

// Index of the target option matching the given host (0 = ALL when unknown).
inline int default_target_index_for_host(HostArch arch) {
    int host_id = get_compatible_target_id_for_host(arch);
    const auto& opts = target_options();
    for (size_t i = 0; i < opts.size(); ++i) {
        if (opts[i].id == host_id) return static_cast<int>(i);
    }
    return 0;
}

} // namespace prismix::patchstorage
