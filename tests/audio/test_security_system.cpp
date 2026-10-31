#include "service/security/security_service.h"
#include "service/system/system_service.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

bool check(const char* name, bool condition) {
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << name << '\n';
    return condition;
}

std::string temporary_path(const char* filename) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
#if defined(_WIN32)
    const unsigned long pid = 0;
#else
    const unsigned long pid = static_cast<unsigned long>(getpid());
#endif
    const auto dir = std::filesystem::temp_directory_path() /
                     ("prismix-security-test-" + std::to_string(pid) + "-" +
                      std::to_string(stamp));
    std::filesystem::create_directories(dir);
    return (dir / filename).string();
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

bool contains_arg(const std::vector<std::string>& args, const std::string& value) {
    return std::find(args.begin(), args.end(), value) != args.end();
}

bool contains_text(const std::vector<std::string>& args, const std::string& value) {
    return std::any_of(args.begin(), args.end(), [&](const std::string& arg) {
        return arg.find(value) != std::string::npos;
    });
}

bool test_security(const std::string& config_path) {
    using prismix::security::SecurityService;
    bool ok = true;

    std::filesystem::remove(config_path);
    SecurityService security(config_path);

    ok &= check("PIN policy accepts exactly six digits",
                SecurityService::isValidPin("123456"));
    ok &= check("PIN policy accepts exactly twelve digits",
                SecurityService::isValidPin("123456789012"));
    ok &= check("PIN policy rejects five digits",
                !SecurityService::isValidPin("12345"));
    ok &= check("PIN policy rejects thirteen digits",
                !SecurityService::isValidPin("1234567890123"));
    ok &= check("PIN policy rejects non-digit characters",
                !SecurityService::isValidPin("12345a") &&
                !SecurityService::isValidPin("123 45") &&
                !SecurityService::isValidPin("12345\n"));
    ok &= check("PIN policy rejects an empty value",
                !SecurityService::isValidPin(""));

    std::string error;
    ok &= check("PIN setup rejects mismatched confirmation",
                !security.setupPin("123456", "654321", error));
    ok &= check("PIN setup succeeds with a valid six-digit PIN",
                security.setupPin("123456", "123456", error));
    ok &= check("PIN setup is reflected by the shared authority",
                security.isPinConfigured());

    const std::string stored = read_file(config_path);
    ok &= check("Persisted PIN is a yescrypt hash", stored.find("pin_hash=$y$") != std::string::npos);
    ok &= check("Persisted configuration never contains the plaintext PIN",
                stored.find("123456") == std::string::npos);
#if !defined(_WIN32)
    struct stat st {};
    ok &= check("Persisted PIN file exists", stat(config_path.c_str(), &st) == 0);
    if (stat(config_path.c_str(), &st) == 0) {
        ok &= check("Persisted PIN file is owner read/write only",
                    (st.st_mode & 0777) == 0600);
    }
#endif

    const std::string session = security.createSession();
    ok &= check("Session token is 256 bits of random hex",
                session.size() == 64 &&
                std::all_of(session.begin(), session.end(), [](unsigned char c) {
                    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                }));
    ok &= check("New session validates", security.validateSession(session));
    security.revokeSession(session);
    ok &= check("Revoked session no longer validates",
                !security.validateSession(session));

    security.setSessionTtlForTesting(1);
    const std::string expiring = security.createSession();
    ok &= check("Short-lived session starts valid", security.validateSession(expiring));
    std::this_thread::sleep_for(std::chrono::milliseconds(1250));
    ok &= check("Expired session is rejected by the authority",
                !security.validateSession(expiring));
    security.setSessionTtlForTesting(8 * 60 * 60);

    security.resetRateLimitsForTesting();
    int retry = 0;
    for (int attempt = 0; attempt < 4; ++attempt) {
        ok &= check(("Incorrect PIN attempt " + std::to_string(attempt + 1) +
                     " is rejected").c_str(),
                    !security.verifyPin("000000", "test-client", error, retry));
        ok &= check("Free attempts are not temporarily locked out", retry == 0);
    }
    ok &= check("Fifth incorrect attempt triggers a temporary lockout",
                !security.verifyPin("000000", "test-client", error, retry) &&
                retry > 0);
    ok &= check("The correct PIN is also blocked during lockout",
                !security.verifyPin("123456", "test-client", error, retry) &&
                retry > 0);
    ok &= check("Lockout applies per client key",
                security.verifyPin("123456", "other-client", error, retry));
    security.resetRateLimitsForTesting();

    const std::string preLockSession = security.createSession();
    ok &= check("Mixer can be locked by the authority",
                security.lockMixer(error) && security.isMixerLocked());
    ok &= check("Locking invalidates existing sessions",
                !security.validateSession(preLockSession) &&
                security.activeSessionCount() == 0);
    ok &= check("Persisted lock state is written",
                read_file(config_path).find("mixer_locked=1") != std::string::npos);
    retry = 0;
    ok &= check("Wrong PIN cannot unlock the mixer",
                !security.unlockMixer("000000", "test-client", error, retry));
    security.resetRateLimitsForTesting();
    ok &= check("Correct PIN unlocks the mixer",
                security.unlockMixer("123456", "test-client", error, retry) &&
                !security.isMixerLocked());
    ok &= check("Unlock persists the cleared lock state",
                read_file(config_path).find("mixer_locked=0") != std::string::npos);

    ok &= check("LAN access is refused without a PIN on a fresh authority", [] {
        const std::string path = temporary_path("no-pin.conf");
        SecurityService fresh(path);
        std::string local_error;
        const bool result = fresh.setLanAccess(true, local_error);
        std::filesystem::remove(path);
        return !result;
    }());
    ok &= check("LAN access can be enabled when a PIN exists",
                security.setLanAccess(true, error) && security.isLanAccessEnabled());
    ok &= check("LAN policy is persisted",
                read_file(config_path).find("lan_access=1") != std::string::npos);
    ok &= check("LAN access can be disabled again",
                security.setLanAccess(false, error) && !security.isLanAccessEnabled());

    security.resetRateLimitsForTesting();
    retry = 0;
    for (int attempt = 0; attempt < 4; ++attempt) {
        const bool changed = security.changePin("000000", "654321", "654321",
                                                "pin-change-client", error, retry);
        ok &= check("Wrong current PIN cannot change the PIN", !changed && retry == 0);
    }
    ok &= check("PIN change shares the login rate limiter",
                !security.changePin("000000", "654321", "654321",
                                    "pin-change-client", error, retry) &&
                retry > 0);
    ok &= check("Correct current PIN is blocked during PIN-change lockout",
                !security.changePin("123456", "654321", "654321",
                                    "pin-change-client", error, retry) &&
                retry > 0);
    security.resetRateLimitsForTesting();
    const std::string oldSession = security.createSession();
    ok &= check("PIN change succeeds after rate-limit recovery",
                security.changePin("123456", "654321", "654321",
                                   "pin-change-client", error, retry));
    ok &= check("PIN change invalidates all existing sessions",
                !security.validateSession(oldSession) &&
                security.activeSessionCount() == 0);
    int newRetry = 0;
    ok &= check("Old PIN no longer verifies",
                !security.verifyPin("123456", "reload-client", error, newRetry));
    security.resetRateLimitsForTesting();
    ok &= check("New PIN verifies through the shared authority",
                security.verifyPin("654321", "reload-client", error, newRetry));

    const std::string cookie = SecurityService::makeSessionCookie("token-value", 90);
    ok &= check("Session cookie is HttpOnly", cookie.find("HttpOnly") != std::string::npos);
    ok &= check("Session cookie is SameSite=Strict",
                cookie.find("SameSite=Strict") != std::string::npos);
    ok &= check("Session cookie is path-scoped",
                cookie.find("Path=/") != std::string::npos);
    ok &= check("Session cookie expires",
                cookie.find("Max-Age=90") != std::string::npos);
    const std::string expired = SecurityService::makeExpiredSessionCookie();
    ok &= check("Expired session cookie clears the browser value",
                expired.find("prismix_session=;") != std::string::npos &&
                expired.find("Max-Age=0") != std::string::npos);

    ok &= check("Lock state persists across authority reload", [&] {
        std::string local_error;
        if (!security.lockMixer(local_error)) return false;
        SecurityService reloaded(config_path);
        const bool loaded = reloaded.isPinConfigured() && reloaded.isMixerLocked();
        int local_retry = 0;
        const bool unlocked = reloaded.unlockMixer("654321", "reload-client",
                                                   local_error, local_retry);
        return loaded && unlocked;
    }());

    std::filesystem::remove(config_path);
    std::filesystem::remove(std::filesystem::path(config_path).parent_path());
    return ok;
}

bool test_system_network() {
    using namespace prismix::system;
    bool ok = true;

    const auto fields = split_nmcli_line("a:b\\:c:d\\\\e:");
    ok &= check("NMCLI terse parser splits unescaped colons", fields.size() == 4);
    ok &= check("NMCLI terse parser unescapes colons", fields.size() > 1 && fields[1] == "b:c");
    ok &= check("NMCLI terse parser unescapes backslashes",
                fields.size() > 2 && fields[2] == "d\\e");

    const std::string list_output =
        "*:Studio\\:5G:87:WPA2\n"
        " :Guest:42:--\n"
        " :Studio\\:5G:55:WPA2\n"
        " :Weak:20:WPA3\n";
    const auto parsed = parse_nmcli_wifi_list(list_output);
    ok &= check("Wi-Fi parser keeps one row per SSID", parsed.size() == 3);
    ok &= check("Wi-Fi parser puts the active network first",
                !parsed.empty() && parsed[0].active && parsed[0].ssid == "Studio:5G");
    ok &= check("Wi-Fi parser keeps the strongest duplicate signal",
                !parsed.empty() && parsed[0].signal_percent == 87);
    ok &= check("Wi-Fi parser marks secured networks",
                !parsed.empty() && parsed[0].secured && parsed[0].security == "WPA2");
    ok &= check("Wi-Fi parser treats an open network as unsecured",
                parsed.size() > 1 && parsed[1].ssid == "Guest" &&
                !parsed[1].secured && parsed[1].security.empty());
    ok &= check("Wi-Fi parser sorts remaining networks by signal",
                parsed.size() == 3 && parsed[1].signal_percent >= parsed[2].signal_percent);

    auto& service = SystemService::instance();
    service.resetCommandRunnerForTesting();
    std::vector<std::vector<std::string>> calls;
    service.setCommandRunnerForTesting(
        [&](const std::vector<std::string>& args) -> CommandResult {
            calls.push_back(args);
            CommandResult result;
            result.launched = true;
            result.exitCode = 0;
            const bool general_status = contains_arg(args, "general") &&
                                        contains_arg(args, "status");
            const bool device_status = contains_arg(args, "device") &&
                                       contains_arg(args, "status") &&
                                       !contains_arg(args, "wifi");
            const bool wifi_list = contains_arg(args, "wifi") &&
                                   contains_arg(args, "list");
            const bool wifi_connect = contains_arg(args, "wifi") &&
                                      contains_arg(args, "connect");
            if (general_status) {
                result.output = "running:connected\n";
            } else if (device_status) {
                result.output = "wlan0:wifi:connected:TestNet\n";
            } else if (wifi_list) {
                result.output = "*:TestNet:82:WPA2\n :OtherNet:37:WPA3\n";
            } else if (wifi_connect) {
                result.output = "Device 'wlan0' successfully activated.\n";
            }
            return result;
        });

    const auto network = service.get_network_info(true);
    ok &= check("Network status reports NetworkManager availability",
                network.network_manager && network.nm_state == "connected");
    ok &= check("Network status reports real Wi-Fi hardware/current connection",
                network.wifi_hardware && network.wifi && network.connected &&
                network.ssid == "TestNet" && network.signal_percent == 82);
    ok &= check("Network status identifies the interface and type",
                network.interface_name == "wlan0" &&
                network.connection_type == "wifi" &&
                network.connection_name == "TestNet");

    const auto scan = service.scan_wifi();
    ok &= check("Wi-Fi scan uses live NetworkManager data",
                scan.success && scan.network_manager && scan.wifi_hardware &&
                scan.networks.size() == 2);
    ok &= check("Wi-Fi scan exposes active SSID without credentials",
                !scan.networks.empty() && scan.networks[0].active &&
                scan.networks[0].ssid == "TestNet");

    service.start_wifi_scan(true);
    bool scanning = true;
    for (int attempt = 0; attempt < 100 && scanning; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const auto snapshot = service.wifi_scan_snapshot(scanning);
        if (!scanning) {
            ok &= check("Asynchronous Wi-Fi scan publishes results",
                        snapshot.success && snapshot.networks.size() == 2);
        }
    }
    ok &= check("Asynchronous Wi-Fi scan finishes without blocking the caller",
                !scanning);

    std::string connect_error;
    calls.clear();
    ok &= check("Wi-Fi connect rejects an invalid SSID before invoking nmcli",
                !service.connect_wifi("-bad-ssid", "secret-pass", connect_error) &&
                calls.empty());
    ok &= check("Wi-Fi connect rejects short passwords before invoking nmcli",
                !service.connect_wifi("TestNet", "short", connect_error) &&
                calls.empty());

    calls.clear();
    ok &= check("Wi-Fi connect succeeds only after NetworkManager confirms state",
                service.connect_wifi("TestNet", "super-secret-123", connect_error));
    const auto connect_call = std::find_if(calls.begin(), calls.end(),
        [](const std::vector<std::string>& args) {
            return contains_arg(args, "connect");
        });
    ok &= check("Wi-Fi connect uses non-interactive secret input",
                connect_call != calls.end() && contains_arg(*connect_call, "--ask"));
    ok &= check("Wi-Fi password is never placed in process arguments",
                connect_call != calls.end() &&
                !contains_text(*connect_call, "super-secret-123"));

    const auto local_access = service.get_web_access(18888, "127.0.0.1", false);
    ok &= check("Local Web access reports the exact loopback URL",
                local_access.url == "http://127.0.0.1:18888/" &&
                local_access.local_only && !local_access.address_available);
    const auto pending_access = service.get_web_access(18888, "127.0.0.1", true);
    ok &= check("LAN policy does not fabricate a LAN URL before rebinding",
                pending_access.local_only && pending_access.url ==
                "http://127.0.0.1:18888/");
    const auto lan_access = service.get_web_access(18888, "0.0.0.0", true);
    const auto info = service.get_network_info(false);
    if (info.ipv4.empty()) {
        ok &= check("No LAN URL is advertised without an IPv4 address",
                    lan_access.url.empty() && !lan_access.local_only);
    } else {
        ok &= check("LAN URL is built from the real IPv4 address",
                    lan_access.url == "http://" + info.ipv4 + ":18888/" &&
                    !lan_access.local_only && lan_access.address_available);
    }

    calls.clear();
    std::string power_error;
    ok &= check("Unsupported power operations are rejected before any command",
                !service.request_power("shell", power_error) && calls.empty());
    const auto power = service.get_power_status();
    if (power.available) {
        ok &= check("Power request uses only the fixed restart allowlist",
                    service.request_power("restart", power_error));
        const auto restart = std::find_if(calls.begin(), calls.end(),
            [](const std::vector<std::string>& args) {
                return contains_arg(args, "reboot");
            });
        ok &= check("Restart command includes --no-block and no shell",
                    restart != calls.end() && contains_arg(*restart, "--no-block") &&
                    !contains_arg(*restart, "sh") && !contains_arg(*restart, "-c"));
        calls.clear();
        ok &= check("Power request uses only the fixed shutdown allowlist",
                    service.request_power("shutdown", power_error));
        const auto shutdown = std::find_if(calls.begin(), calls.end(),
            [](const std::vector<std::string>& args) {
                return contains_arg(args, "poweroff");
            });
        ok &= check("Shutdown command includes --no-block",
                    shutdown != calls.end() && contains_arg(*shutdown, "--no-block"));
    } else {
        ok &= check("Unavailable power management is reported honestly",
                    !power.reason.empty());
    }

    service.resetCommandRunnerForTesting();
    return ok;
}

} // namespace

int main() {
    bool ok = true;
    std::cout << "============================================================\n";
    std::cout << "TEST SUITE: Shared PIN, Session, Network & Power Authority\n";
    std::cout << "============================================================\n";

    const std::string config_path = temporary_path("security.conf");
    ok &= test_security(config_path);
    ok &= test_system_network();

    std::cout << "============================================================\n";
    std::cout << (ok ? "ALL SECURITY & SYSTEM TESTS PASSED!\n" : "SOME TESTS FAILED!\n");
    std::cout << "============================================================\n";
    return ok ? 0 : 1;
}
