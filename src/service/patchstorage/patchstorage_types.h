#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace prismix::patchstorage {

constexpr int PLATFORM_ID_LV2 = 8046;
constexpr int TARGET_ID_LINUX_AMD64 = 8279;
constexpr int TARGET_ID_RPI_AARCH64 = 8280;
constexpr int TARGET_ID_PATCHBOX_ARM32 = 8278;

enum class HostArch {
    Unknown,
    X86_64,
    AArch64,
    Arm32
};

inline const char* host_arch_to_string(HostArch arch) {
    switch (arch) {
        case HostArch::X86_64: return "x86_64 (linux-amd64)";
        case HostArch::AArch64: return "aarch64 (rpi-aarch64)";
        case HostArch::Arm32: return "armhf (patchbox-os-arm32)";
        default: return "Unknown";
    }
}

inline HostArch detect_current_host_arch() {
#if defined(__x86_64__) || defined(_M_X64)
    return HostArch::X86_64;
#elif defined(__aarch64__) || defined(_M_ARM64)
    return HostArch::AArch64;
#elif defined(__arm__) || defined(_M_ARM)
    return HostArch::Arm32;
#else
    return HostArch::Unknown;
#endif
}

inline int get_compatible_target_id_for_host(HostArch arch) {
    switch (arch) {
        case HostArch::X86_64: return TARGET_ID_LINUX_AMD64;
        case HostArch::AArch64: return TARGET_ID_RPI_AARCH64;
        case HostArch::Arm32: return TARGET_ID_PATCHBOX_ARM32;
        default: return 0;
    }
}

inline bool is_target_id_compatible(int target_id, HostArch host_arch) {
    int expected = get_compatible_target_id_for_host(host_arch);
    return expected != 0 && target_id == expected;
}

struct PatchTarget {
    int id{0};
    std::string name;
    std::string slug;
};

struct PatchFile {
    int id{0};
    std::string filename;
    std::string url;
    uint64_t filesize{0};
    PatchTarget target;
};

struct PatchAuthor {
    int id{0};
    std::string name;
    std::string slug;
    std::string avatar_url;
};

struct PatchCategory {
    int id{0};
    std::string name;
    std::string slug;
};

struct PatchStorageItem {
    int id{0};
    std::vector<std::string> uids;
    std::string url;
    std::string created_at;
    std::string updated_at;
    std::string slug;
    std::string title;
    std::string excerpt;
    std::string content;
    std::string revision;
    std::string artwork_url;            // PatchStorage "artwork.url" (full size, when present)
    std::string artwork_thumbnail_url;  // PatchStorage "artwork.thumbnail_url" (preferred for the grid)
    std::string source_code_url;
    PatchAuthor author;
    std::string license_name;
    std::vector<PatchCategory> categories;
    std::vector<PatchTarget> targets;
    std::vector<PatchFile> files;

    bool is_compatible_with_host(HostArch arch) const {
        for (const auto& t : targets) {
            if (is_target_id_compatible(t.id, arch)) return true;
        }
        for (const auto& f : files) {
            if (is_target_id_compatible(f.target.id, arch)) return true;
        }
        return false;
    }

    const PatchFile* find_file_for_host(HostArch arch) const {
        for (const auto& f : files) {
            if (is_target_id_compatible(f.target.id, arch)) return &f;
        }
        return nullptr;
    }
};

enum class PatchInstallState {
    NotInstalled,
    Downloading,
    Installing,
    Validating,
    Installed,
    Incompatible,
    Error
};

// State of the locally cached artwork for a catalog item
enum class ArtworkState : uint8_t {
    None = 0,     // not requested (no artwork metadata, or not visible yet)
    Loading,      // download/decode queued on the service worker
    Ready,        // decoded pixels available via get_artwork()
    Failed        // download or decode failed; placeholder stays
};

// Decoded artwork, already converted to LVGL XRGB8888/ARGB8888 byte order
// (B, G, R, X per pixel). Owned by the service, handed to the UI by shared_ptr.
struct ArtworkImage {
    int patch_id{0};
    int width{0};
    int height{0};
    std::vector<uint8_t> pixels;  // width * height * 4 bytes, BGRA order
};

struct PatchStatus {
    PatchInstallState state{PatchInstallState::NotInstalled};
    std::string message;
    std::string installed_bundle_name;
    std::string installed_bundle_path;
    std::string installed_plugin_uri;
    float download_progress{0.0f}; // 0.0 to 1.0
    bool loaded_in_rack{false};    // installed AND referenced by a live PRISMIX rack slot
};

} // namespace prismix::patchstorage

