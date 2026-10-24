#pragma once

#include "patchstorage_types.h"
#include <string>
#include <vector>
#include <filesystem>
#include <cstdint>

namespace prismix::patchstorage {

struct ElfInfo {
    bool is_valid_elf{false};
    uint8_t elf_class{0};    // 1 = 32-bit, 2 = 64-bit
    uint8_t endianness{0};   // 1 = Little Endian, 2 = Big Endian
    uint16_t machine{0};     // 62 = x86_64, 183 = aarch64, 40 = arm
    std::string arch_name;
    bool is_compatible_with_host{false};
    std::string error_message;
};

struct ArchiveValidationResult {
    bool is_safe{false};
    size_t file_count{0};
    uint64_t total_uncompressed_bytes{0};
    std::vector<std::string> entries;
    std::string detected_bundle_folder;
    std::string error_message;
};

struct InstallResult {
    bool success{false};
    std::string installed_bundle_name;
    std::string installed_bundle_path;
    std::vector<std::string> discovered_plugin_uris;
    std::string error_message;
};

class Lv2Installer {
public:
    static Lv2Installer& instance();

    Lv2Installer();
    ~Lv2Installer() = default;

    // Configurable paths (useful for testing)
    void set_custom_lv2_dir(const std::string& path);
    void set_custom_staging_dir(const std::string& path);

    std::filesystem::path get_target_lv2_dir() const;
    std::filesystem::path get_staging_dir() const;

    // Architecture & ELF validation
    static ElfInfo inspect_elf_binary(const std::filesystem::path& binary_path,
                                      HostArch target_arch = detect_current_host_arch());

    // Tar archive security validation (rejects ../, absolute paths, bombs)
    static ArchiveValidationResult validate_tar_archive(const std::filesystem::path& archive_path,
                                                        uint64_t max_total_size = 250 * 1024 * 1024,
                                                        uint64_t max_single_file_size = 50 * 1024 * 1024,
                                                        size_t max_files = 500);

    // Safe extraction into staging folder
    static bool extract_tar_archive(const std::filesystem::path& archive_path,
                                   const std::filesystem::path& dest_dir,
                                   std::string& out_error);

    // End-to-end atomic installation of archive
    InstallResult install_archive(const std::filesystem::path& archive_path,
                                  HostArch target_arch = detect_current_host_arch());

    // Uninstall bundle (guarded by active rack usage check)
    bool uninstall_bundle(const std::string& bundle_name,
                          std::string& out_error);

    // Check if a bundle or plugin URI is currently active in any channel or master rack
    static bool is_bundle_in_active_rack(const std::string& bundle_name_or_path);

    // Helper to check if a bundle is installed in LV2 directory
    bool is_bundle_installed(const std::string& bundle_name) const;

    // Check installed state for PatchStorage item (by UIDs, filename, or slug)
    bool is_patch_installed(const PatchStorageItem& item,
                            std::string* out_installed_bundle = nullptr,
                            std::string* out_installed_uri = nullptr) const;

private:
    std::filesystem::path custom_lv2_dir_;
    std::filesystem::path custom_staging_dir_;
};

} // namespace prismix::patchstorage

