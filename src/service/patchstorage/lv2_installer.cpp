#include "lv2_installer.h"
#include "../../dsp/lv2/lv2_host.h"
#include "../../model/plugin_model.h"
#include "../../model/mixer_state.h"

#include <zlib.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
#include <random>
#include <cstring>
#include <cstdlib>

#if defined(__has_include) && __has_include(<elf.h>)
#include <elf.h>
#else
#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'
#define ELFCLASS32 1
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define EM_386 3
#define EM_ARM 40
#define EM_X86_64 62
#define EM_AARCH64 183
#endif

namespace prismix::patchstorage {

namespace {

uint64_t parse_octal(const char* str, size_t max_len) {
    uint64_t val = 0;
    size_t i = 0;
    while (i < max_len && (str[i] == ' ' || str[i] == '\0')) i++;
    while (i < max_len && str[i] >= '0' && str[i] <= '7') {
        val = (val << 3) + (str[i] - '0');
        i++;
    }
    return val;
}

bool is_safe_relpath(const std::string& path) {
    if (path.empty()) return false;
    if (path[0] == '/' || path[0] == '\\') return false;
    if (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':') return false;

    size_t start = 0;
    while (start < path.size()) {
        size_t end = path.find_first_of("/\\", start);
        if (end == std::string::npos) end = path.size();
        std::string seg = path.substr(start, end - start);
        if (seg == "..") return false;
        start = end + 1;
    }
    return true;
}

std::string get_tar_full_name(const char* name, const char* prefix) {
    std::string full;
    if (prefix && prefix[0] != '\0') {
        full.append(prefix, strnlen(prefix, 155));
        full.push_back('/');
    }
    if (name) {
        full.append(name, strnlen(name, 100));
    }
    return full;
}

std::filesystem::path get_default_lv2_user_dir() {
    const char* home = std::getenv("HOME");
    if (home && *home) {
        return std::filesystem::path(home) / ".lv2";
    }
    return std::filesystem::current_path() / "assets" / "lv2";
}

std::filesystem::path get_default_staging_base() {
    const char* home = std::getenv("HOME");
    if (home && *home) {
        return std::filesystem::path(home) / ".lv2" / ".patchstorage_staging";
    }
    return std::filesystem::temp_directory_path() / "prismix_staging";
}

} // namespace

Lv2Installer& Lv2Installer::instance() {
    static Lv2Installer inst;
    return inst;
}

Lv2Installer::Lv2Installer() {
}

void Lv2Installer::set_custom_lv2_dir(const std::string& path) {
    custom_lv2_dir_ = path;
}

void Lv2Installer::set_custom_staging_dir(const std::string& path) {
    custom_staging_dir_ = path;
}

std::filesystem::path Lv2Installer::get_target_lv2_dir() const {
    if (!custom_lv2_dir_.empty()) return custom_lv2_dir_;
    return get_default_lv2_user_dir();
}

std::filesystem::path Lv2Installer::get_staging_dir() const {
    if (!custom_staging_dir_.empty()) return custom_staging_dir_;
    return get_default_staging_base();
}

ElfInfo Lv2Installer::inspect_elf_binary(const std::filesystem::path& binary_path,
                                        HostArch target_arch) {
    ElfInfo info;
    std::ifstream file(binary_path, std::ios::binary);
    if (!file.is_open()) {
        info.error_message = "Cannot open binary file: " + binary_path.string();
        return info;
    }

    char header[64];
    file.read(header, sizeof(header));
    if (file.gcount() < 24) {
        info.error_message = "File too small to be an ELF binary";
        return info;
    }

    // Check magic
    if (static_cast<uint8_t>(header[0]) != ELFMAG0 ||
        header[1] != ELFMAG1 ||
        header[2] != ELFMAG2 ||
        header[3] != ELFMAG3) {
        info.error_message = "Not a valid ELF binary (bad magic bytes)";
        return info;
    }

    info.is_valid_elf = true;
    info.elf_class = static_cast<uint8_t>(header[4]); // 1 = 32-bit, 2 = 64-bit
    info.endianness = static_cast<uint8_t>(header[5]); // 1 = Little-endian

    // e_machine is 2 bytes at offset 18
    if (info.endianness == 1) { // Little-endian
        info.machine = static_cast<uint8_t>(header[18]) | (static_cast<uint16_t>(static_cast<uint8_t>(header[19])) << 8);
    } else { // Big-endian
        info.machine = (static_cast<uint16_t>(static_cast<uint8_t>(header[18])) << 8) | static_cast<uint8_t>(header[19]);
    }

    switch (info.machine) {
        case 62: // EM_X86_64
            info.arch_name = "x86_64 (linux-amd64)";
            info.is_compatible_with_host = (target_arch == HostArch::X86_64 && info.elf_class == 2);
            break;
        case 183: // EM_AARCH64
            info.arch_name = "aarch64 (rpi-aarch64)";
            info.is_compatible_with_host = (target_arch == HostArch::AArch64 && info.elf_class == 2);
            break;
        case 40: // EM_ARM
            info.arch_name = "armhf (patchbox-os-arm32)";
            info.is_compatible_with_host = (target_arch == HostArch::Arm32 && info.elf_class == 1);
            break;
        case 3: // EM_386
            info.arch_name = "i386 (x86-32)";
            info.is_compatible_with_host = false;
            break;
        default:
            info.arch_name = "Unknown architecture (machine id " + std::to_string(info.machine) + ")";
            info.is_compatible_with_host = false;
            break;
    }

    if (!info.is_compatible_with_host) {
        info.error_message = "Binary architecture '" + info.arch_name + "' does not match host system '" +
                             host_arch_to_string(target_arch) + "'";
    }

    return info;
}

ArchiveValidationResult Lv2Installer::validate_tar_archive(const std::filesystem::path& archive_path,
                                                          uint64_t max_total_size,
                                                          uint64_t max_single_file_size,
                                                          size_t max_files) {
    ArchiveValidationResult res;
    gzFile gz = gzopen(archive_path.string().c_str(), "rb");
    if (!gz) {
        res.error_message = "Could not open archive: " + archive_path.string();
        return res;
    }

    char block[512];
    int zero_blocks = 0;

    while (true) {
        int bytes = gzread(gz, block, 512);
        if (bytes == 0) break;
        if (bytes < 512) {
            res.error_message = "Corrupted archive: truncated tar block";
            gzclose(gz);
            return res;
        }

        bool is_zero = true;
        for (int i = 0; i < 512; ++i) {
            if (block[i] != 0) { is_zero = false; break; }
        }
        if (is_zero) {
            zero_blocks++;
            if (zero_blocks >= 2) break;
            continue;
        }
        zero_blocks = 0;

        char typeflag = block[156];
        std::string full_name = get_tar_full_name(block, block + 345);
        if (full_name.empty()) continue;

        // Path safety inspection
        if (!is_safe_relpath(full_name)) {
            res.error_message = "Security rejection: illegal path traversal or absolute path detected: '" + full_name + "'";
            gzclose(gz);
            return res;
        }

        // Symlink safety inspection
        if (typeflag == '2' || typeflag == '1') {
            std::string linkname(block + 157, strnlen(block + 157, 100));
            if (!linkname.empty() && !is_safe_relpath(linkname)) {
                res.error_message = "Security rejection: illegal symlink target: '" + linkname + "'";
                gzclose(gz);
                return res;
            }
        }

        uint64_t file_size = parse_octal(block + 124, 12);
        if (file_size > max_single_file_size) {
            res.error_message = "Archive contains file exceeding size limit (" +
                                std::to_string(file_size) + " bytes > " +
                                std::to_string(max_single_file_size) + " bytes): " + full_name;
            gzclose(gz);
            return res;
        }

        res.total_uncompressed_bytes += file_size;
        if (res.total_uncompressed_bytes > max_total_size) {
            res.error_message = "Decompression bomb protection: total uncompressed archive size exceeds limit (" +
                                std::to_string(res.total_uncompressed_bytes) + " bytes)";
            gzclose(gz);
            return res;
        }

        res.file_count++;
        if (res.file_count > max_files) {
            res.error_message = "Archive contains too many entries (exceeds limit of " + std::to_string(max_files) + ")";
            gzclose(gz);
            return res;
        }

        res.entries.push_back(full_name);

        // Detect bundle root folder
        auto slash_pos = full_name.find_first_of("/\\");
        std::string root_seg = (slash_pos != std::string::npos) ? full_name.substr(0, slash_pos) : full_name;
        if (root_seg.size() >= 4 && root_seg.rfind(".lv2") == root_seg.size() - 4) {
            if (res.detected_bundle_folder.empty()) {
                res.detected_bundle_folder = root_seg;
            }
        }

        // Skip file payload blocks
        uint64_t pad = (512 - (file_size % 512)) % 512;
        uint64_t to_skip = file_size + pad;
        while (to_skip > 0) {
            unsigned int chunk = static_cast<unsigned int>(std::min<uint64_t>(to_skip, sizeof(block)));
            int read_bytes = gzread(gz, block, chunk);
            if (read_bytes <= 0) break;
            to_skip -= read_bytes;
        }
    }

    gzclose(gz);
    res.is_safe = true;
    return res;
}

bool Lv2Installer::extract_tar_archive(const std::filesystem::path& archive_path,
                                     const std::filesystem::path& dest_dir,
                                     std::string& out_error) {
    std::error_code ec;
    std::filesystem::create_directories(dest_dir, ec);
    if (ec) {
        out_error = "Could not create destination directory: " + ec.message();
        return false;
    }

    gzFile gz = gzopen(archive_path.string().c_str(), "rb");
    if (!gz) {
        out_error = "Could not open archive: " + archive_path.string();
        return false;
    }

    char block[512];
    int zero_blocks = 0;

    while (true) {
        int bytes = gzread(gz, block, 512);
        if (bytes == 0) break;
        if (bytes < 512) {
            out_error = "Corrupted archive during extraction";
            gzclose(gz);
            return false;
        }

        bool is_zero = true;
        for (int i = 0; i < 512; ++i) {
            if (block[i] != 0) { is_zero = false; break; }
        }
        if (is_zero) {
            zero_blocks++;
            if (zero_blocks >= 2) break;
            continue;
        }
        zero_blocks = 0;

        char typeflag = block[156];
        std::string full_name = get_tar_full_name(block, block + 345);
        if (full_name.empty()) continue;

        if (!is_safe_relpath(full_name)) {
            out_error = "Illegal path during extraction: " + full_name;
            gzclose(gz);
            return false;
        }

        std::filesystem::path target_file = dest_dir / full_name;
        uint64_t file_size = parse_octal(block + 124, 12);

        if (typeflag == '5' || full_name.back() == '/') {
            // Directory
            std::filesystem::create_directories(target_file, ec);
        } else if (typeflag == '2') {
            // Symlink
            std::string linkname(block + 157, strnlen(block + 157, 100));
            std::filesystem::create_directories(target_file.parent_path(), ec);
            if (is_safe_relpath(linkname)) {
                std::filesystem::create_symlink(linkname, target_file, ec);
            }
        } else {
            // Regular file
            std::filesystem::create_directories(target_file.parent_path(), ec);
            std::ofstream out(target_file, std::ios::binary);
            if (!out.is_open()) {
                out_error = "Could not create target file: " + target_file.string();
                gzclose(gz);
                return false;
            }

            uint64_t remaining = file_size;
            while (remaining > 0) {
                unsigned int chunk = static_cast<unsigned int>(std::min<uint64_t>(remaining, sizeof(block)));
                int read_bytes = gzread(gz, block, chunk);
                if (read_bytes <= 0) break;
                out.write(block, read_bytes);
                remaining -= read_bytes;
            }
            out.close();

            // Skip padding
            uint64_t pad = (512 - (file_size % 512)) % 512;
            if (pad > 0) {
                gzread(gz, block, static_cast<unsigned int>(pad));
            }
        }
    }

    gzclose(gz);
    return true;
}

InstallResult Lv2Installer::install_archive(const std::filesystem::path& archive_path,
                                          HostArch target_arch) {
    InstallResult result;

    // 1. Validate archive safety (reject path traversal, bombs)
    ArchiveValidationResult val = validate_tar_archive(archive_path);
    if (!val.is_safe) {
        result.error_message = val.error_message;
        return result;
    }

    // 2. Prepare staging directory
    std::filesystem::path staging_base = get_staging_dir();
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path stage_dir = staging_base / ("extract_" + std::to_string(now));

    std::error_code ec;
    std::filesystem::create_directories(stage_dir, ec);

    // 3. Extract to staging
    std::string extract_err;
    if (!extract_tar_archive(archive_path, stage_dir, extract_err)) {
        result.error_message = "Extraction failed: " + extract_err;
        std::filesystem::remove_all(stage_dir, ec);
        return result;
    }

    // 4. Locate bundle folder in staging
    std::filesystem::path bundle_path;
    if (!val.detected_bundle_folder.empty() && std::filesystem::exists(stage_dir / val.detected_bundle_folder, ec)) {
        bundle_path = stage_dir / val.detected_bundle_folder;
    } else {
        // Search staging directory for any *.lv2 folder
        for (const auto& entry : std::filesystem::recursive_directory_iterator(stage_dir, ec)) {
            if (entry.is_directory() && entry.path().extension() == ".lv2") {
                bundle_path = entry.path();
                break;
            }
        }
    }

    if (bundle_path.empty()) {
        // If no .lv2 directory, check if manifest.ttl is directly in stage_dir
        if (std::filesystem::exists(stage_dir / "manifest.ttl", ec)) {
            std::string bundle_name = archive_path.stem().string();
            if (bundle_name.rfind(".tar") != std::string::npos) {
                bundle_name = bundle_name.substr(0, bundle_name.rfind(".tar"));
            }
            if (bundle_name.rfind(".lv2") == std::string::npos) {
                bundle_name += ".lv2";
            }
            std::filesystem::path wrap_dir = staging_base / ("wrap_" + std::to_string(now)) / bundle_name;
            std::filesystem::create_directories(wrap_dir, ec);
            for (const auto& entry : std::filesystem::directory_iterator(stage_dir, ec)) {
                std::filesystem::rename(entry.path(), wrap_dir / entry.path().filename(), ec);
            }
            bundle_path = wrap_dir;
        } else {
            result.error_message = "Archive does not contain an LV2 bundle (manifest.ttl missing)";
            std::filesystem::remove_all(stage_dir, ec);
            return result;
        }
    }

    // 5. Verify manifest.ttl exists in bundle
    if (!std::filesystem::exists(bundle_path / "manifest.ttl", ec)) {
        result.error_message = "Invalid bundle: manifest.ttl is missing inside " + bundle_path.filename().string();
        std::filesystem::remove_all(stage_dir, ec);
        return result;
    }

    // 6. Inspect binary ELF headers for architecture match
    bool found_binary = false;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(bundle_path, ec)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension();
        if (ext == ".so" || ext == ".dylib" || ext == ".dll") {
            found_binary = true;
            if (ext == ".so") {
                ElfInfo elf = inspect_elf_binary(entry.path(), target_arch);
                if (!elf.is_compatible_with_host) {
                    result.error_message = "Incompatible plugin binary: " + elf.error_message;
                    std::filesystem::remove_all(stage_dir, ec);
                    return result;
                }
            }
        }
    }

    if (!found_binary) {
        result.error_message = "Bundle does not contain any executable plugin binary (*.so)";
        std::filesystem::remove_all(stage_dir, ec);
        return result;
    }

    // 7. Atomic placement into target LV2 directory
    std::filesystem::path target_lv2_dir = get_target_lv2_dir();
    std::filesystem::create_directories(target_lv2_dir, ec);

    std::string bundle_folder_name = bundle_path.filename().string();
    std::filesystem::path final_dest = target_lv2_dir / bundle_folder_name;

    // Non-silent conflict handling: backup existing if present
    std::filesystem::path backup_path = staging_base / ("backup_" + bundle_folder_name + "_" + std::to_string(now));
    bool had_existing = false;
    if (std::filesystem::exists(final_dest, ec)) {
        had_existing = true;
        std::filesystem::rename(final_dest, backup_path, ec);
        if (ec) {
            result.error_message = "Cannot overwrite existing bundle: " + ec.message();
            std::filesystem::remove_all(stage_dir, ec);
            return result;
        }
    }

    // Move to final destination
    std::filesystem::rename(bundle_path, final_dest, ec);
    if (ec) {
        // Rollback
        if (had_existing) {
            std::error_code rb_ec;
            std::filesystem::rename(backup_path, final_dest, rb_ec);
        }
        result.error_message = "Failed to place bundle into " + final_dest.string() + ": " + ec.message();
        std::filesystem::remove_all(stage_dir, ec);
        return result;
    }

    // Success: cleanup backup and staging
    if (had_existing) {
        std::filesystem::remove_all(backup_path, ec);
    }
    std::filesystem::remove_all(stage_dir, ec);

    result.success = true;
    result.installed_bundle_name = bundle_folder_name;
    result.installed_bundle_path = final_dest.string();

    // 8. Refresh discovery catalog
    prismix::lv2::Lv2Host::instance().refreshCatalog();
    PluginRegistry::instance().refresh_catalog();

    // Query discovered URIs from this bundle
    const auto& discovered = prismix::lv2::Lv2Host::instance().getDiscoveredPlugins();
    for (const auto& dp : discovered) {
        if (dp.bundle_path.find(bundle_folder_name) != std::string::npos) {
            result.discovered_plugin_uris.push_back(dp.metadata.uri);
        }
    }

    return result;
}

bool Lv2Installer::is_bundle_in_active_rack(const std::string& bundle_name_or_path) {
    if (bundle_name_or_path.empty()) return false;

    // Check all channels
    for (size_t c = 0; c < g_mixer_state.channels.size(); ++c) {
        const ChannelState* ch = g_mixer_state.channels.get(c);
        if (!ch) continue;
        for (size_t i = 0; i < ch->fx_rack.count(); ++i) {
            const auto* inst = ch->fx_rack.get_slot(i);
            if (!inst) continue;
            std::string uri = inst->get_uri();
            const auto* dp = prismix::lv2::Lv2Host::instance().findPlugin(uri);
            if (dp && dp->bundle_path.find(bundle_name_or_path) != std::string::npos) {
                return true;
            }
            if (uri.find(bundle_name_or_path) != std::string::npos) {
                return true;
            }
        }
    }

    // Check AUX buses
    for (const auto& aux : g_mixer_state.aux_buses) {
        for (size_t i = 0; i < aux.fx_rack.count(); ++i) {
            const auto* inst = aux.fx_rack.get_slot(i);
            if (!inst) continue;
            std::string uri = inst->get_uri();
            const auto* dp = prismix::lv2::Lv2Host::instance().findPlugin(uri);
            if (dp && dp->bundle_path.find(bundle_name_or_path) != std::string::npos) {
                return true;
            }
            if (uri.find(bundle_name_or_path) != std::string::npos) {
                return true;
            }
        }
    }

    // Check master channel
    for (size_t i = 0; i < g_mixer_state.master.fx_rack.count(); ++i) {
        const auto* inst = g_mixer_state.master.fx_rack.get_slot(i);
        if (!inst) continue;
        std::string uri = inst->get_uri();
        const auto* dp = prismix::lv2::Lv2Host::instance().findPlugin(uri);
        if (dp && dp->bundle_path.find(bundle_name_or_path) != std::string::npos) {
            return true;
        }
        if (uri.find(bundle_name_or_path) != std::string::npos) {
            return true;
        }
    }

    return false;
}

bool Lv2Installer::uninstall_bundle(const std::string& bundle_name, std::string& out_error) {
    if (bundle_name.empty()) {
        out_error = "Bundle name cannot be empty";
        return false;
    }

    // Safety: Verify plugin is not currently loaded in any channel rack
    if (is_bundle_in_active_rack(bundle_name)) {
        out_error = "Cannot uninstall '" + bundle_name + "': plugin is active in FX Rack. Remove plugin from rack first.";
        return false;
    }

    std::filesystem::path target_dir = get_target_lv2_dir();
    std::filesystem::path bundle_path = target_dir / bundle_name;
    if (bundle_name.rfind(".lv2") == std::string::npos) {
        bundle_path = target_dir / (bundle_name + ".lv2");
    }

    std::error_code ec;
    if (!std::filesystem::exists(bundle_path, ec)) {
        out_error = "Bundle not found: " + bundle_path.string();
        return false;
    }

    std::filesystem::remove_all(bundle_path, ec);
    if (ec) {
        out_error = "Failed to remove bundle: " + ec.message();
        return false;
    }

    // Refresh discovery catalog
    prismix::lv2::Lv2Host::instance().refreshCatalog();
    PluginRegistry::instance().refresh_catalog();

    return true;
}

bool Lv2Installer::is_bundle_installed(const std::string& bundle_name) const {
    std::filesystem::path target_dir = get_target_lv2_dir();
    std::filesystem::path p1 = target_dir / bundle_name;
    std::filesystem::path p2 = target_dir / (bundle_name + ".lv2");
    std::error_code ec;
    return std::filesystem::exists(p1, ec) || std::filesystem::exists(p2, ec);
}

bool Lv2Installer::is_patch_installed(const PatchStorageItem& item,
                                     std::string* out_installed_bundle,
                                     std::string* out_installed_uri) const {
    // 1. Check by UIDs in Lv2Host
    for (const auto& uid : item.uids) {
        const auto* dp = prismix::lv2::Lv2Host::instance().findPlugin(uid);
        if (dp) {
            if (out_installed_bundle) *out_installed_bundle = std::filesystem::path(dp->bundle_path).filename().string();
            if (out_installed_uri) *out_installed_uri = dp->metadata.uri;
            return true;
        }
    }

    // 2. Check by filename stems in target lv2 dir
    for (const auto& f : item.files) {
        if (!f.filename.empty()) {
            std::string stem = f.filename;
            auto dot_lv2 = stem.find(".lv2");
            if (dot_lv2 != std::string::npos) {
                stem = stem.substr(0, dot_lv2 + 4);
                if (is_bundle_installed(stem)) {
                    if (out_installed_bundle) *out_installed_bundle = stem;
                    return true;
                }
            }
        }
    }

    // 3. Check by slug
    if (!item.slug.empty() && is_bundle_installed(item.slug)) {
        if (out_installed_bundle) *out_installed_bundle = item.slug + ".lv2";
        return true;
    }

    return false;
}

} // namespace prismix::patchstorage

