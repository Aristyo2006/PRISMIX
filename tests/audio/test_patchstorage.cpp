#include <iostream>
#include <cassert>
#include <cstring>
#include <vector>
#include <filesystem>
#include <fstream>
#include <memory>
#include <zlib.h>

#include "service/patchstorage/patchstorage_types.h"
#include "service/patchstorage/patchstorage_json.h"
#include "service/patchstorage/patchstorage_client.h"
#include "service/patchstorage/lv2_installer.h"
#include "service/patchstorage/patchstorage_service.h"
#include "dsp/lv2/lv2_host.h"
#include "model/plugin_model.h"
#include "model/mixer_state.h"

using namespace prismix::patchstorage;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
            return false; \
        } \
    } while (0)

// Helper to write a 512-byte tar header
void write_tar_header(std::ostream& out,
                      const std::string& filename,
                      uint64_t size,
                      char typeflag = '0',
                      const std::string& linkname = "") {
    char block[512];
    std::memset(block, 0, sizeof(block));

    std::strncpy(block, filename.c_str(), 99);
    std::snprintf(block + 100, 8, "%07o", 0755);
    std::snprintf(block + 108, 8, "%07o", 1000);
    std::snprintf(block + 116, 8, "%07o", 1000);
    std::snprintf(block + 124, 12, "%011llo", static_cast<unsigned long long>(size));
    std::snprintf(block + 136, 12, "%011o", 0);
    std::memset(block + 148, ' ', 8); // Checksum field initialized to spaces
    block[156] = typeflag;
    if (!linkname.empty()) {
        std::strncpy(block + 157, linkname.c_str(), 99);
    }
    std::memcpy(block + 257, "ustar\0", 6);
    std::memcpy(block + 263, "00", 2);

    // Calculate checksum
    unsigned int sum = 0;
    for (int i = 0; i < 512; ++i) {
        sum += static_cast<unsigned char>(block[i]);
    }
    std::snprintf(block + 148, 8, "%06o", sum);
    block[148 + 7] = ' '; // tar checksum standard formatting

    out.write(block, 512);
}

// Helper to create a minimal 64-bit ELF binary
std::vector<uint8_t> create_dummy_elf(uint16_t machine = 62, uint8_t elf_class = 2) {
    std::vector<uint8_t> buf(64, 0);
    buf[0] = 0x7f; buf[1] = 'E'; buf[2] = 'L'; buf[3] = 'F';
    buf[4] = elf_class; // 1 = 32-bit, 2 = 64-bit
    buf[5] = 1;         // 1 = Little endian
    buf[6] = 1;         // Version 1

    // e_type = 3 (ET_DYN shared object)
    buf[16] = 3; buf[17] = 0;

    // e_machine at offset 18
    buf[18] = static_cast<uint8_t>(machine & 0xFF);
    buf[19] = static_cast<uint8_t>((machine >> 8) & 0xFF);

    return buf;
}

// Helper to create a gzipped tar archive
bool create_test_tar_gz(const std::filesystem::path& tar_path,
                       const std::vector<std::pair<std::string, std::vector<uint8_t>>>& files) {
    gzFile gz = gzopen(tar_path.string().c_str(), "wb");
    if (!gz) return false;

    for (const auto& item : files) {
        const std::string& name = item.first;
        const auto& data = item.second;
        char typeflag = (name.back() == '/') ? '5' : '0';

        char block[512];
        std::memset(block, 0, sizeof(block));
        std::strncpy(block, name.c_str(), 99);
        std::snprintf(block + 100, 8, "%07o", 0755);
        std::snprintf(block + 108, 8, "%07o", 1000);
        std::snprintf(block + 116, 8, "%07o", 1000);
        std::snprintf(block + 124, 12, "%011llo", static_cast<unsigned long long>(data.size()));
        std::snprintf(block + 136, 12, "%011o", 0);
        std::memset(block + 148, ' ', 8);
        block[156] = typeflag;
        std::memcpy(block + 257, "ustar\0", 6);
        std::memcpy(block + 263, "00", 2);

        unsigned int sum = 0;
        for (int i = 0; i < 512; ++i) sum += static_cast<unsigned char>(block[i]);
        std::snprintf(block + 148, 8, "%06o", sum);
        block[148 + 7] = ' ';

        gzwrite(gz, block, 512);

        if (!data.empty()) {
            gzwrite(gz, data.data(), static_cast<unsigned int>(data.size()));
            uint64_t pad = (512 - (data.size() % 512)) % 512;
            if (pad > 0) {
                char pad_buf[512];
                std::memset(pad_buf, 0, pad);
                gzwrite(gz, pad_buf, static_cast<unsigned int>(pad));
            }
        }
    }

    // Two 512-byte zero blocks at end of tar
    char zero_blocks[1024];
    std::memset(zero_blocks, 0, sizeof(zero_blocks));
    gzwrite(gz, zero_blocks, sizeof(zero_blocks));

    gzclose(gz);
    return true;
}

// ---------------- Test 1: JSON Parser ----------------
bool test_json_parser() {
    std::cout << "[Test 1] Testing PatchStorage JSON Parser..." << std::endl;

    const std::string sample_patch_json = R"({
        "id": 198094,
        "uids": ["https://rpowell5064.github.io/guitaramp-suite/practice"],
        "url": "https://patchstorage.com/hex-chain-scratch-pad/",
        "created_at": "2026-10-08T04:18:40+00:00",
        "slug": "hex-chain-scratch-pad",
        "title": "Hex Chain Scratch Pad",
        "excerpt": "Four-track looper and drum machine",
        "content": "Full plugin description with sound examples",
        "revision": "1.48",
        "author": {
            "id": 18836,
            "name": "rpowell5064",
            "slug": "rpowell5064",
            "avatar_url": "https://avatar.jpg"
        },
        "license": {
            "id": 4181,
            "name": "GPL v3",
            "slug": "gpl-v3"
        },
        "categories": [
            {"id": 117, "name": "Utility", "slug": "utility"}
        ],
        "targets": [
            {"id": 8279, "name": "linux-amd64", "slug": "linux-amd64"},
            {"id": 8280, "name": "rpi-aarch64", "slug": "rpi-aarch64"}
        ],
        "files": [
            {
                "id": 198091,
                "filename": "practice.lv2.tar.gz",
                "url": "https://patchstorage.com/api/beta/patches/198094/files/198091/download/",
                "filesize": 1754501,
                "target": {"id": 8279, "name": "linux-amd64", "slug": "linux-amd64"}
            },
            {
                "id": 198093,
                "filename": "practice-arm.lv2.tar.gz",
                "url": "https://patchstorage.com/api/beta/patches/198094/files/198093/download/",
                "filesize": 1750597,
                "target": {"id": 8280, "name": "rpi-aarch64", "slug": "rpi-aarch64"}
            }
        ]
    })";

    PatchStorageItem item;
    std::string err;
    bool ok = parse_single_patch(sample_patch_json, item, &err);
    TEST_ASSERT(ok, "Failed to parse single patch: " + err);
    TEST_ASSERT(item.id == 198094, "Item ID mismatch");
    TEST_ASSERT(item.title == "Hex Chain Scratch Pad", "Title mismatch");
    TEST_ASSERT(item.author.name == "rpowell5064", "Author mismatch");
    TEST_ASSERT(item.license_name == "GPL v3", "License mismatch");
    TEST_ASSERT(item.uids.size() == 1, "UIDs size mismatch");
    TEST_ASSERT(item.targets.size() == 2, "Targets size mismatch");
    TEST_ASSERT(item.files.size() == 2, "Files size mismatch");
    TEST_ASSERT(item.files[0].filesize == 1754501, "Filesize mismatch");
    TEST_ASSERT(item.files[0].target.id == 8279, "Target ID mismatch");

    // Test list of patches
    std::string list_json = "[" + sample_patch_json + "]";
    std::vector<PatchStorageItem> items;
    ok = parse_patches_list(list_json, items, &err);
    TEST_ASSERT(ok, "Failed to parse patches list: " + err);
    TEST_ASSERT(items.size() == 1, "Items size mismatch");

    // Test malformed JSON
    std::string bad_json = "{\"id\": 123, \"title\": unquoted }";
    JsonValue val = parse_json(bad_json, &err);
    TEST_ASSERT(!err.empty(), "Parser should report error for malformed JSON");

    std::cout << "  ✓ JSON parser passed\n";
    return true;
}

// ---------------- Test 2: Architecture & Target Compatibility ----------------
bool test_architecture_compatibility() {
    std::cout << "[Test 2] Testing Architecture & Target Compatibility..." << std::endl;

    HostArch detected = detect_current_host_arch();
    TEST_ASSERT(detected == HostArch::X86_64, "Current test machine must be detected as x86_64");

    TEST_ASSERT(is_target_id_compatible(8279, HostArch::X86_64), "8279 must be compatible with X86_64");
    TEST_ASSERT(!is_target_id_compatible(8280, HostArch::X86_64), "8280 (aarch64) must be incompatible with X86_64");
    TEST_ASSERT(!is_target_id_compatible(8278, HostArch::X86_64), "8278 (arm32) must be incompatible with X86_64");

    // Test dummy ELF validation
    auto x86_elf = create_dummy_elf(62, 2); // EM_X86_64, 64-bit
    auto arm_elf = create_dummy_elf(183, 2); // EM_AARCH64, 64-bit

    std::filesystem::path tmp_dir = std::filesystem::temp_directory_path() / "prismix_elf_test";
    std::filesystem::create_directories(tmp_dir);

    std::filesystem::path x86_path = tmp_dir / "plugin_x86_64.so";
    std::ofstream f1(x86_path, std::ios::binary);
    f1.write(reinterpret_cast<const char*>(x86_elf.data()), x86_elf.size());
    f1.close();

    std::filesystem::path arm_path = tmp_dir / "plugin_arm64.so";
    std::ofstream f2(arm_path, std::ios::binary);
    f2.write(reinterpret_cast<const char*>(arm_elf.data()), arm_elf.size());
    f2.close();

    ElfInfo info_x86 = Lv2Installer::inspect_elf_binary(x86_path, HostArch::X86_64);
    TEST_ASSERT(info_x86.is_valid_elf, "x86 dummy ELF should be valid");
    TEST_ASSERT(info_x86.is_compatible_with_host, "x86 dummy ELF should be compatible with x86_64 host");

    ElfInfo info_arm = Lv2Installer::inspect_elf_binary(arm_path, HostArch::X86_64);
    TEST_ASSERT(info_arm.is_valid_elf, "arm dummy ELF should be valid");
    TEST_ASSERT(!info_arm.is_compatible_with_host, "arm dummy ELF must NOT be compatible with x86_64 host");
    TEST_ASSERT(!info_arm.error_message.empty(), "Should contain incompatibility explanation");

    std::error_code ec;
    std::filesystem::remove_all(tmp_dir, ec);

    std::cout << "  ✓ Architecture compatibility and ELF inspection passed\n";
    return true;
}

// ---------------- Test 3: Tar Security Validation ----------------
bool test_tar_archive_security() {
    std::cout << "[Test 3] Testing Tar Archive Security Validation..." << std::endl;

    std::filesystem::path tmp_dir = std::filesystem::temp_directory_path() / "prismix_tar_sec_test";
    std::filesystem::create_directories(tmp_dir);

    // 1. Normal safe archive
    std::filesystem::path safe_tar = tmp_dir / "safe.tar.gz";
    std::vector<std::pair<std::string, std::vector<uint8_t>>> safe_files = {
        {"myplugin.lv2/", {}},
        {"myplugin.lv2/manifest.ttl", {'@', 'p', 'r', 'e', 'f', 'i', 'x', ' '}},
        {"myplugin.lv2/myplugin.so", create_dummy_elf(62, 2)}
    };
    TEST_ASSERT(create_test_tar_gz(safe_tar, safe_files), "Failed to create safe test tar");

    ArchiveValidationResult res_safe = Lv2Installer::validate_tar_archive(safe_tar);
    TEST_ASSERT(res_safe.is_safe, "Safe archive should pass validation: " + res_safe.error_message);
    TEST_ASSERT(res_safe.file_count == 3, "Entry count mismatch");
    TEST_ASSERT(res_safe.detected_bundle_folder == "myplugin.lv2", "Bundle folder detection mismatch: '" + res_safe.detected_bundle_folder + "'");

    // 2. Malicious archive with path traversal: ../evil.txt
    std::filesystem::path evil_tar = tmp_dir / "evil_traversal.tar.gz";
    std::vector<std::pair<std::string, std::vector<uint8_t>>> evil_files = {
        {"myplugin.lv2/manifest.ttl", {'a'}},
        {"myplugin.lv2/../../evil.txt", {'b', 'a', 'd'}}
    };
    TEST_ASSERT(create_test_tar_gz(evil_tar, evil_files), "Failed to create evil test tar");

    ArchiveValidationResult res_evil = Lv2Installer::validate_tar_archive(evil_tar);
    TEST_ASSERT(!res_evil.is_safe, "Malicious archive with traversal MUST be rejected");
    TEST_ASSERT(res_evil.error_message.find("path traversal") != std::string::npos,
                "Error message should explicitly mention path traversal");

    // 3. Malicious archive with absolute path: /etc/evil
    std::filesystem::path abs_tar = tmp_dir / "evil_abs.tar.gz";
    std::vector<std::pair<std::string, std::vector<uint8_t>>> abs_files = {
        {"/etc/passwd", {'x'}}
    };
    TEST_ASSERT(create_test_tar_gz(abs_tar, abs_files), "Failed to create abs path test tar");

    ArchiveValidationResult res_abs = Lv2Installer::validate_tar_archive(abs_tar);
    TEST_ASSERT(!res_abs.is_safe, "Archive with absolute path MUST be rejected");

    // 4. Archive with single file exceeding limit
    ArchiveValidationResult res_bomb = Lv2Installer::validate_tar_archive(safe_tar, 1024 * 1024, 10, 500);
    TEST_ASSERT(!res_bomb.is_safe, "Archive with file exceeding max size must be rejected");

    std::error_code ec;
    std::filesystem::remove_all(tmp_dir, ec);

    std::cout << "  ✓ Tar archive security validation passed\n";
    return true;
}

// ---------------- Test 4: Mock HTTP Client & Pagination ----------------
class MockHttpExecutor : public IHttpExecutor {
public:
    HttpResponse get(const std::string& url, int timeout_seconds) override {
        (void)timeout_seconds;
        HttpResponse resp;
        resp.status_code = 200;

        if (url.find("/patches?") != std::string::npos) {
            resp.headers["x-wp-total"] = "309";
            resp.headers["x-wp-totalpages"] = "31";
            resp.body = R"([
                {
                    "id": 1001,
                    "title": "Mock Reverb LV2",
                    "slug": "mock-reverb",
                    "author": {"name": "AudioCore Team"},
                    "targets": [{"id": 8279, "name": "linux-amd64"}]
                }
            ])";
        } else if (url.find("/patches/1001") != std::string::npos) {
            resp.body = R"({
                "id": 1001,
                "title": "Mock Reverb LV2",
                "slug": "mock-reverb",
                "files": [
                    {
                        "id": 501,
                        "filename": "mock_reverb.lv2.tar.gz",
                        "url": "https://patchstorage.com/api/beta/patches/1001/files/501/download/",
                        "target": {"id": 8279, "name": "linux-amd64"}
                    }
                ]
            })";
        }
        return resp;
    }

    bool download(const std::string& url,
                  const std::string& dest_path,
                  int timeout_seconds,
                  std::string* error) override {
        (void)url; (void)timeout_seconds; (void)error;
        // Create a dummy tar.gz as download payload
        std::vector<std::pair<std::string, std::vector<uint8_t>>> files = {
            {"mock_reverb.lv2/", {}},
            {"mock_reverb.lv2/manifest.ttl", {'@', 'p', 'r', 'e', 'f', 'i', 'x', ' '}},
            {"mock_reverb.lv2/mock_reverb.so", create_dummy_elf(62, 2)}
        };
        return create_test_tar_gz(dest_path, files);
    }
};

bool test_mock_client_and_pagination() {
    std::cout << "[Test 4] Testing Client & Pagination Parsing..." << std::endl;

    auto mock_exec = std::make_shared<MockHttpExecutor>();
    PatchStorageClient client(mock_exec);

    FetchResult res = client.fetch_patches(1, 10, "reverb", 8279);
    TEST_ASSERT(res.success, "Fetch patches should succeed with mock executor");
    TEST_ASSERT(res.total_items == 309, "Total items should be 309 from header");
    TEST_ASSERT(res.total_pages == 31, "Total pages should be 31 from header");
    TEST_ASSERT(res.items.size() == 1, "Parsed items size mismatch");
    TEST_ASSERT(res.items[0].id == 1001, "Item ID mismatch");

    PatchStorageItem detail;
    std::string err;
    bool ok = client.fetch_patch_details(1001, detail, err);
    TEST_ASSERT(ok, "Fetch patch details should succeed: " + err);
    TEST_ASSERT(detail.files.size() == 1, "Detailed files array size mismatch");
    TEST_ASSERT(detail.files[0].id == 501, "File ID mismatch");

    std::cout << "  ✓ Client & pagination parsing passed\n";
    return true;
}

// ---------------- Test 5: End-to-End Atomic Installation & Rollback ----------------
bool test_atomic_install_and_rollback() {
    std::cout << "[Test 5] Testing End-to-End Atomic Installation & Rollback..." << std::endl;

    std::filesystem::path root_test = std::filesystem::temp_directory_path() / "prismix_e2e_install_test";
    std::filesystem::path custom_lv2 = root_test / "lv2_dest";
    std::filesystem::path custom_stage = root_test / "staging";
    std::filesystem::create_directories(custom_lv2);
    std::filesystem::create_directories(custom_stage);

    Lv2Installer::instance().set_custom_lv2_dir(custom_lv2.string());
    Lv2Installer::instance().set_custom_staging_dir(custom_stage.string());

    // 1. Install valid bundle
    std::filesystem::path archive_path = root_test / "test_synth.lv2.tar.gz";
    std::vector<std::pair<std::string, std::vector<uint8_t>>> files = {
        {"test_synth.lv2/", {}},
        {"test_synth.lv2/manifest.ttl", {'@', 'p', 'r', 'e', 'f', 'i', 'x', ' '}},
        {"test_synth.lv2/test_synth.so", create_dummy_elf(62, 2)}
    };
    TEST_ASSERT(create_test_tar_gz(archive_path, files), "Failed to generate test archive");

    InstallResult res = Lv2Installer::instance().install_archive(archive_path, HostArch::X86_64);
    TEST_ASSERT(res.success, "Installation should succeed: " + res.error_message);
    TEST_ASSERT(res.installed_bundle_name == "test_synth.lv2", "Installed bundle name mismatch");
    TEST_ASSERT(std::filesystem::exists(custom_lv2 / "test_synth.lv2"), "Installed bundle must exist in destination");
    TEST_ASSERT(std::filesystem::exists(custom_lv2 / "test_synth.lv2" / "manifest.ttl"), "manifest.ttl must exist");

    // 2. Conflict replacement: install updated version of same bundle
    std::vector<std::pair<std::string, std::vector<uint8_t>>> updated_files = {
        {"test_synth.lv2/", {}},
        {"test_synth.lv2/manifest.ttl", {'v', '2', '.', '0'}},
        {"test_synth.lv2/test_synth.so", create_dummy_elf(62, 2)}
    };
    TEST_ASSERT(create_test_tar_gz(archive_path, updated_files), "Failed to generate updated archive");

    InstallResult res_up = Lv2Installer::instance().install_archive(archive_path, HostArch::X86_64);
    TEST_ASSERT(res_up.success, "Re-installation over existing bundle should succeed safely");
    TEST_ASSERT(std::filesystem::exists(custom_lv2 / "test_synth.lv2" / "manifest.ttl"), "manifest.ttl must exist");

    // 3. Incompatible binary rejection (e.g. ARM binary on x86 host)
    std::filesystem::path arm_archive = root_test / "arm_synth.lv2.tar.gz";
    std::vector<std::pair<std::string, std::vector<uint8_t>>> arm_files = {
        {"arm_synth.lv2/", {}},
        {"arm_synth.lv2/manifest.ttl", {'a'}},
        {"arm_synth.lv2/arm_synth.so", create_dummy_elf(183, 2)} // EM_AARCH64
    };
    TEST_ASSERT(create_test_tar_gz(arm_archive, arm_files), "Failed to generate arm archive");

    InstallResult res_arm = Lv2Installer::instance().install_archive(arm_archive, HostArch::X86_64);
    TEST_ASSERT(!res_arm.success, "ARM binary installation on x86 host must fail");
    TEST_ASSERT(res_arm.error_message.find("Incompatible plugin binary") != std::string::npos,
                "Error message should explain incompatible binary architecture");
    TEST_ASSERT(!std::filesystem::exists(custom_lv2 / "arm_synth.lv2"), "Incompatible bundle must NOT be installed");

    std::error_code ec;
    std::filesystem::remove_all(root_test, ec);

    // Reset default paths
    Lv2Installer::instance().set_custom_lv2_dir("");
    Lv2Installer::instance().set_custom_staging_dir("");

    std::cout << "  ✓ Atomic installation and conflict rollback passed\n";
    return true;
}

// ---------------- Test 6: Uninstallation & Active Rack Lock ----------------
bool test_uninstallation_and_rack_lock() {
    std::cout << "[Test 6] Testing Uninstallation & Active Rack Lock..." << std::endl;

    std::filesystem::path root_test = std::filesystem::temp_directory_path() / "prismix_uninstall_test";
    std::filesystem::path custom_lv2 = root_test / "lv2_dest";
    std::filesystem::create_directories(custom_lv2);
    Lv2Installer::instance().set_custom_lv2_dir(custom_lv2.string());

    // Create an installed bundle folder on disk
    std::filesystem::path bundle = custom_lv2 / "locked_delay.lv2";
    std::filesystem::create_directories(bundle);
    std::ofstream f(bundle / "manifest.ttl");
    f << "@prefix lv2: <http://lv2plug.in/ns/lv2core#> .\n";
    f.close();

    TEST_ASSERT(Lv2Installer::instance().is_bundle_installed("locked_delay.lv2"), "Bundle should be detected as installed");

    // 1. Simulate active plugin in Channel 1's rack
    if (g_mixer_state.channels.empty()) {
        ChannelState ch_state;
        ch_state.id = 1;
        std::strncpy(ch_state.name, "Ch 1", sizeof(ch_state.name));
        g_mixer_state.channels.push_back(ch_state);
    }
    ChannelState* ch = g_mixer_state.channels.get(0);

    PluginMetadata meta;
    meta.uri = "http://example.org/plugins/locked_delay.lv2";
    meta.name = "Locked Delay";
    meta.source_type = PluginSourceType::Lv2;
    meta.is_live_dsp = true;

    auto inst = std::make_shared<PluginInstance>(meta);
    ch->fx_rack.add_instance(inst);

    // 2. Attempt uninstallation while locked in active rack
    std::string err;
    bool ok = Lv2Installer::instance().uninstall_bundle("locked_delay.lv2", err);
    TEST_ASSERT(!ok, "Uninstallation of active plugin MUST be rejected");
    TEST_ASSERT(err.find("active in FX Rack") != std::string::npos, "Error should report active in FX rack");
    TEST_ASSERT(std::filesystem::exists(bundle), "Bundle folder must NOT be deleted when locked");

    // 3. Remove plugin from rack and retry uninstallation
    ch->fx_rack.clear();
    ok = Lv2Installer::instance().uninstall_bundle("locked_delay.lv2", err);
    TEST_ASSERT(ok, "Uninstallation should succeed when not in active rack: " + err);
    TEST_ASSERT(!std::filesystem::exists(bundle), "Bundle folder must be deleted after uninstallation");

    std::error_code ec;
    std::filesystem::remove_all(root_test, ec);
    Lv2Installer::instance().set_custom_lv2_dir("");

    std::cout << "  ✓ Uninstallation and active rack lock passed\n";
    return true;
}

int main() {
    std::cout << "=========================================================\n";
    std::cout << "PRISMIX PATCHSTORAGE CATALOG & INSTALLATION TEST SUITE\n";
    std::cout << "=========================================================\n";

    if (!test_json_parser()) return 1;
    if (!test_architecture_compatibility()) return 1;
    if (!test_tar_archive_security()) return 1;
    if (!test_mock_client_and_pagination()) return 1;
    if (!test_atomic_install_and_rollback()) return 1;
    if (!test_uninstallation_and_rack_lock()) return 1;

    std::cout << "=========================================================\n";
    std::cout << "ALL PATCHSTORAGE TESTS PASSED SUCCESSFULLY!\n";
    std::cout << "=========================================================\n";
    return 0;
}
