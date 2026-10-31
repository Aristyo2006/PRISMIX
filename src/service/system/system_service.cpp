#include "system_service.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <thread>
#include <unordered_map>

#if defined(_WIN32)
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace prismix::system {

namespace {

int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string trim(std::string value) {
    const auto start = value.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    const auto end = value.find_last_not_of(" \t\r\n");
    value = value.substr(start, end - start + 1);
    return value;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string executable_path(const char* name, const std::vector<std::string>& candidates) {
    for (const auto& candidate : candidates) {
#if !defined(_WIN32)
        if (access(candidate.c_str(), X_OK) == 0) return candidate;
#endif
    }
    return name;
}

const std::string& nmcli_path() {
    static const std::string path = executable_path(
        "nmcli", {"/usr/bin/nmcli", "/usr/local/bin/nmcli", "/bin/nmcli"});
    return path;
}

const std::string& systemctl_path() {
    static const std::string path = executable_path(
        "systemctl", {"/usr/bin/systemctl", "/bin/systemctl"});
    return path;
}

bool validate_ssid(const std::string& ssid) {
    if (ssid.empty() || ssid.size() > 64) return false;
    if (ssid.front() == '-') return false;
    for (unsigned char c : ssid) {
        if (c < 0x20 || c == 0x7f) return false;
    }
    return true;
}

bool validate_wifi_password(const std::string& password) {
    if (password.empty()) return true;
    if (password.size() < 8 || password.size() > 63) return false;
    for (unsigned char c : password) {
        if (c < 0x20 || c == 0x7f) return false;
    }
    return true;
}

bool starts_with(const std::string& value, const std::string& prefix) {
    return value.size() >= prefix.size() &&
           value.compare(0, prefix.size(), prefix) == 0;
}

struct InterfaceAddresses {
    std::string ipv4;
    std::string ipv6;
    std::string fallback_ipv4;
    std::string fallback_ipv6;
    std::string fallback_interface;
};

void collect_interfaces(InterfaceAddresses& out) {
#if !defined(_WIN32)
    struct ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return;
    for (struct ifaddrs* it = list; it; it = it->ifa_next) {
        if (!it->ifa_addr || !(it->ifa_flags & IFF_UP)) continue;
        const int family = it->ifa_addr->sa_family;
        if (family != AF_INET && family != AF_INET6) continue;
        if (it->ifa_flags & IFF_LOOPBACK) continue;

        char text[INET6_ADDRSTRLEN] = {};
        if (family == AF_INET) {
            const auto* addr = reinterpret_cast<const sockaddr_in*>(it->ifa_addr);
            if (!inet_ntop(AF_INET, &addr->sin_addr, text, sizeof(text))) continue;
            if (starts_with(text, "169.254.")) continue;
            if (!out.fallback_ipv4.empty()) {
                // Prefer a private LAN address over other routable addresses.
                const std::string candidate(text);
                const bool candidate_private =
                    starts_with(candidate, "10.") || starts_with(candidate, "192.168.") ||
                    (starts_with(candidate, "172.") && candidate.size() > 6);
                const bool current_private =
                    starts_with(out.fallback_ipv4, "10.") ||
                    starts_with(out.fallback_ipv4, "192.168.") ||
                    (starts_with(out.fallback_ipv4, "172.") && out.fallback_ipv4.size() > 6);
                if (!candidate_private && current_private) continue;
            }
            out.fallback_ipv4 = text;
            if (out.fallback_interface.empty()) out.fallback_interface = it->ifa_name;
        } else {
            const auto* addr = reinterpret_cast<const sockaddr_in6*>(it->ifa_addr);
            if (IN6_IS_ADDR_LINKLOCAL(&addr->sin6_addr)) continue;
            if (!inet_ntop(AF_INET6, &addr->sin6_addr, text, sizeof(text))) continue;
            if (out.fallback_ipv6.empty()) out.fallback_ipv6 = text;
        }
    }
    freeifaddrs(list);
#endif
}

void addresses_for_interface(const std::string& ifname, InterfaceAddresses& out) {
#if !defined(_WIN32)
    if (ifname.empty()) return;
    struct ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return;
    for (struct ifaddrs* it = list; it; it = it->ifa_next) {
        if (!it->ifa_addr || !(it->ifa_flags & IFF_UP)) continue;
        if (!it->ifa_name || ifname != it->ifa_name) continue;
        const int family = it->ifa_addr->sa_family;
        if (family != AF_INET && family != AF_INET6) continue;
        char text[INET6_ADDRSTRLEN] = {};
        if (family == AF_INET) {
            const auto* addr = reinterpret_cast<const sockaddr_in*>(it->ifa_addr);
            if (!inet_ntop(AF_INET, &addr->sin_addr, text, sizeof(text))) continue;
            if (out.ipv4.empty() && !starts_with(text, "169.254.")) out.ipv4 = text;
        } else {
            const auto* addr = reinterpret_cast<const sockaddr_in6*>(it->ifa_addr);
            if (IN6_IS_ADDR_LINKLOCAL(&addr->sin6_addr)) continue;
            if (!inet_ntop(AF_INET6, &addr->sin6_addr, text, sizeof(text))) continue;
            if (out.ipv6.empty()) out.ipv6 = text;
        }
    }
    freeifaddrs(list);
#endif
}

} // namespace

SystemService& SystemService::instance() {
    static SystemService service;
    return service;
}

SystemService::~SystemService() {
    std::thread worker;
    {
        std::lock_guard<std::mutex> lock(scan_mutex_);
        if (scan_thread_.joinable()) worker = std::move(scan_thread_);
    }
    if (worker.joinable()) worker.join();
}

void SystemService::setCommandRunnerForTesting(CommandRunner runner) {
    std::lock_guard<std::mutex> lock(mutex_);
    test_runner_ = std::move(runner);
    cached_at_ms_ = 0;
}

void SystemService::resetCommandRunnerForTesting() {
    std::lock_guard<std::mutex> lock(mutex_);
    test_runner_ = {};
    cached_at_ms_ = 0;
}

std::vector<std::string> split_nmcli_line(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    bool escaped = false;
    for (char c : line) {
        if (escaped) {
            current.push_back(c);
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == ':') {
            fields.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(c);
    }
    fields.push_back(current);
    return fields;
}

std::vector<WifiNetwork> parse_nmcli_wifi_list(const std::string& output) {
    std::vector<WifiNetwork> by_ssid;
    std::unordered_map<std::string, std::size_t> index;
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty()) continue;
        const auto fields = split_nmcli_line(line);
        if (fields.size() < 4) continue;
        WifiNetwork network;
        const std::string in_use = trim(fields[0]);
        network.active = in_use == "*" || lower(in_use) == "yes";
        network.ssid = trim(fields[1]);
        if (network.ssid.empty()) continue;
        try {
            network.signal_percent = std::stoi(trim(fields[2]));
        } catch (...) {
            network.signal_percent = -1;
        }
        network.security = trim(fields[3]);
        if (network.security == "--") network.security.clear();
        network.secured = !network.security.empty();
        const auto found = index.find(network.ssid);
        if (found == index.end()) {
            index[network.ssid] = by_ssid.size();
            by_ssid.push_back(network);
        } else {
            WifiNetwork& existing = by_ssid[found->second];
            if (network.active) existing = network;
            else if (network.signal_percent > existing.signal_percent) {
                const bool was_active = existing.active;
                existing = network;
                existing.active = was_active;
            }
        }
    }
    std::sort(by_ssid.begin(), by_ssid.end(), [](const WifiNetwork& a, const WifiNetwork& b) {
        if (a.active != b.active) return a.active;
        return a.signal_percent > b.signal_percent;
    });
    return by_ssid;
}

CommandResult SystemService::run_command(const std::vector<std::string>& args,
                                         int timeoutMs,
                                         const std::string* stdin_data) const {
    CommandRunner runner;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        runner = test_runner_;
    }
    if (runner) return runner(args);
    if (args.empty()) return {};

    CommandResult result;
#if defined(_WIN32)
    (void)timeoutMs;
    (void)result;
    return result;
#else
    // A closed stdin pipe must not terminate the mixer process with SIGPIPE.
    std::signal(SIGPIPE, SIG_IGN);
    int out_pipe[2] = {-1, -1};
    int err_pipe[2] = {-1, -1};
    int in_pipe[2] = {-1, -1};
    if (pipe(out_pipe) != 0) return result;
    if (pipe(err_pipe) != 0) {
        close(out_pipe[0]);
        close(out_pipe[1]);
        return result;
    }
    if (stdin_data && pipe(in_pipe) != 0) {
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);
        return result;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);
        if (stdin_data) {
            close(in_pipe[0]);
            close(in_pipe[1]);
        }
        return result;
    }
    if (pid == 0) {
        close(out_pipe[0]);
        close(err_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[1]);
        close(err_pipe[1]);
        if (stdin_data) {
            close(in_pipe[1]);
            dup2(in_pipe[0], STDIN_FILENO);
            close(in_pipe[0]);
        }

        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);
        if (!args[0].empty() && args[0].find('/') != std::string::npos) {
            execv(args[0].c_str(), argv.data());
        } else {
            execvp(args[0].c_str(), argv.data());
        }
        _exit(127);
    }

    close(out_pipe[1]);
    close(err_pipe[1]);
    if (stdin_data) {
        close(in_pipe[0]);
        const char* data = stdin_data->data();
        std::size_t remaining = stdin_data->size();
        while (remaining > 0) {
            const ssize_t written = write(in_pipe[1], data, remaining);
            if (written > 0) {
                data += written;
                remaining -= static_cast<std::size_t>(written);
            } else if (written < 0 && errno != EINTR) {
                break;
            }
        }
        close(in_pipe[1]);
    }
    fcntl(out_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(err_pipe[0], F_SETFL, O_NONBLOCK);

    bool out_open = true;
    bool err_open = true;
    bool exited = false;
    int status = 0;
    const int64_t deadline = now_ms() + std::max(100, timeoutMs);
    auto last_activity = now_ms();

    while (out_open || err_open || !exited) {
        struct pollfd fds[2];
        int count = 0;
        if (out_open) fds[count++] = {out_pipe[0], POLLIN, 0};
        if (err_open) fds[count++] = {err_pipe[0], POLLIN, 0};
        poll(fds, static_cast<nfds_t>(count), 40);

        auto drain = [&](int fd, std::string& target, bool& open) {
            std::array<char, 2048> buf{};
            for (;;) {
                const ssize_t n = read(fd, buf.data(), buf.size());
                if (n > 0) {
                    if (target.size() < 256 * 1024) target.append(buf.data(), static_cast<std::size_t>(n));
                    last_activity = now_ms();
                    continue;
                }
                if (n == 0) {
                    close(fd);
                    open = false;
                } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                    close(fd);
                    open = false;
                }
                break;
            }
        };
        if (out_open) drain(out_pipe[0], result.output, out_open);
        if (err_open) drain(err_pipe[0], result.errorText, err_open);

        if (!exited) {
            const pid_t waited = waitpid(pid, &status, WNOHANG);
            if (waited == pid) exited = true;
        }

        const int64_t now = now_ms();
        if (!exited && now - deadline > 0) {
            result.timedOut = true;
            kill(pid, SIGTERM);
            for (int i = 0; i < 10; ++i) {
                if (waitpid(pid, &status, WNOHANG) == pid) {
                    exited = true;
                    break;
                }
                usleep(20000);
            }
            if (!exited) {
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
                exited = true;
            }
            break;
        }
        if (exited && !out_open && !err_open) break;
        if (exited && now - last_activity > 250) {
            // The child closed its output but a descriptor has not reported EOF.
            if (out_open) { close(out_pipe[0]); out_open = false; }
            if (err_open) { close(err_pipe[0]); err_open = false; }
            break;
        }
    }

    if (out_open) close(out_pipe[0]);
    if (err_open) close(err_pipe[0]);
    if (exited && !result.timedOut) {
        result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
    result.launched = !result.timedOut && (exited || result.exitCode >= 0);
    result.output = trim(result.output);
    result.errorText = trim(result.errorText);
    return result;
#endif
}

CommandResult SystemService::run_nmcli(const std::vector<std::string>& args,
                                       int timeoutMs) const {
    std::vector<std::string> command;
    command.reserve(args.size() + 1);
    command.push_back(nmcli_path());
    command.insert(command.end(), args.begin(), args.end());
    return run_command(command, timeoutMs);
}

NetworkInfo SystemService::get_network_info(bool force_refresh) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!force_refresh && cached_at_ms_ > 0 && now_ms() - cached_at_ms_ < 2000) {
            return cached_network_;
        }
    }

    NetworkInfo info;
    InterfaceAddresses all_addresses;
    collect_interfaces(all_addresses);

    const auto general = run_nmcli({"-t", "-f", "RUNNING,STATE", "general", "status"}, 2500);
    if (general.launched && general.exitCode == 0) {
        const auto fields = split_nmcli_line(general.output);
        info.network_manager = fields.size() >= 2 && trim(fields[0]) == "running";
        if (fields.size() >= 2) info.nm_state = trim(fields[1]);
    }

    if (info.network_manager) {
        const auto devices = run_nmcli(
            {"-t", "-f", "DEVICE,TYPE,STATE,CONNECTION", "device", "status"}, 3000);
        if (devices.launched && devices.exitCode == 0) {
            std::vector<std::vector<std::string>> parsed;
            std::istringstream stream(devices.output);
            std::string line;
            while (std::getline(stream, line)) {
                const auto fields = split_nmcli_line(line);
                if (fields.size() >= 4) parsed.push_back(fields);
            }
            auto choose = [&](auto predicate) -> std::vector<std::string> {
                for (const auto& fields : parsed) {
                    if (predicate(fields)) return fields;
                }
                return {};
            };
            auto type_is = [](const std::string& type, const char* value) {
                return lower(type) == value;
            };
            auto connected = [&](const std::vector<std::string>& fields) {
                return lower(fields[2]).find("connected") != std::string::npos ||
                       lower(fields[2]) == "activated";
            };
            auto is_wifi = [&](const std::vector<std::string>& fields) {
                return type_is(fields[1], "wifi") || type_is(fields[1], "802-11-wireless");
            };
            auto is_phys = [&](const std::vector<std::string>& fields) {
                return !type_is(fields[1], "loopback") && !type_is(fields[1], "tun") &&
                       !type_is(fields[1], "bridge");
            };

            for (const auto& fields : parsed) {
                if (is_wifi(fields)) info.wifi_hardware = true;
            }

            auto selected = choose([&](const auto& fields) {
                return is_phys(fields) && is_wifi(fields) && connected(fields);
            });
            if (selected.empty()) {
                selected = choose([&](const auto& fields) {
                    return is_phys(fields) && connected(fields);
                });
            }
            if (selected.empty()) {
                selected = choose([&](const auto& fields) {
                    return is_phys(fields) && is_wifi(fields);
                });
            }
            if (selected.empty()) {
                selected = choose([&](const auto& fields) {
                    return is_phys(fields);
                });
            }

            if (!selected.empty()) {
                info.interface_name = selected[0];
                info.connection_type = selected[1];
                info.device_state = selected[2];
                info.connection_name = selected[3];
                info.connected = lower(selected[2]).find("connected") != std::string::npos ||
                                 lower(selected[2]) == "activated";
                info.wifi = type_is(selected[1], "wifi") ||
                            type_is(selected[1], "802-11-wireless");
                InterfaceAddresses addresses;
                addresses_for_interface(info.interface_name, addresses);
                info.ipv4 = addresses.ipv4;
                info.ipv6 = addresses.ipv6;
                if (info.ipv4.empty()) info.ipv4 = all_addresses.fallback_ipv4;
            }
        }

        if (info.wifi) {
            const auto wifi = run_nmcli(
                {"-t", "-f", "IN-USE,SSID,SIGNAL,SECURITY", "device", "wifi", "list"},
                5000);
            if (wifi.launched && wifi.exitCode == 0) {
                const auto networks = parse_nmcli_wifi_list(wifi.output);
                for (const auto& network : networks) {
                    if (network.active) {
                        info.ssid = network.ssid;
                        info.signal_percent = network.signal_percent;
                        break;
                    }
                }
                if (info.ssid.empty() && !info.connection_name.empty()) {
                    info.ssid = info.connection_name;
                }
            }
        }
    } else {
        info.error = "NetworkManager is not available";
    }

    if (info.interface_name.empty()) {
        info.interface_name = all_addresses.fallback_interface;
        info.ipv4 = all_addresses.fallback_ipv4;
        info.ipv6 = all_addresses.fallback_ipv6;
        if (!info.interface_name.empty()) {
            info.connection_type = "network";
            info.connection_name = info.interface_name;
            info.device_state = "connected";
            info.connected = true;
        }
    }
    if (info.ipv4.empty()) info.ipv4 = all_addresses.fallback_ipv4;
    if (info.ipv6.empty()) info.ipv6 = all_addresses.fallback_ipv6;
    if (info.nm_state.empty() && info.connected) info.nm_state = "connected";

    {
        std::lock_guard<std::mutex> lock(mutex_);
        cached_network_ = info;
        cached_at_ms_ = now_ms();
    }
    return info;
}

WifiScanResult SystemService::scan_wifi() {
    WifiScanResult result;
    const NetworkInfo info = get_network_info(true);
    result.network_manager = info.network_manager;
    result.wifi_hardware = info.wifi_hardware;
    if (!info.network_manager) {
        result.error = "NetworkManager is not available on this device.";
        return result;
    }
    if (!info.wifi_hardware) {
        result.error = "No Wi-Fi hardware was detected.";
        return result;
    }
    const auto scan = run_nmcli(
        {"-t", "-f", "IN-USE,SSID,SIGNAL,SECURITY", "device", "wifi", "list", "--rescan", "yes"},
        20000);
    if (!scan.launched || scan.exitCode != 0) {
        result.error = scan.timedOut ? "The Wi-Fi scan timed out." :
                       "NetworkManager could not scan for Wi-Fi networks.";
        return result;
    }
    result.networks = parse_nmcli_wifi_list(scan.output);
    result.success = true;
    if (result.networks.empty()) {
        result.error = "No Wi-Fi networks were found.";
    }
    return result;
}

void SystemService::start_wifi_scan(bool force) {
    std::lock_guard<std::mutex> lock(scan_mutex_);
    if (scan_running_ || (scan_started_ && !force)) return;
    scan_started_ = true;
    scan_running_ = true;
    if (scan_thread_.joinable()) scan_thread_.join();
    scan_thread_ = std::thread([this]() {
        WifiScanResult result = scan_wifi();
        {
            std::lock_guard<std::mutex> guard(scan_mutex_);
            scan_result_ = result;
            scan_running_ = false;
        }
    });
}

WifiScanResult SystemService::wifi_scan_snapshot(bool& scanning) {
    std::lock_guard<std::mutex> lock(scan_mutex_);
    scanning = scan_running_;
    return scan_result_;
}

bool SystemService::connect_wifi(const std::string& ssid, const std::string& password,
                                 std::string& error) {
    error.clear();
    if (!validate_ssid(ssid)) {
        error = "Enter a valid network name (up to 64 bytes).";
        return false;
    }
    if (!validate_wifi_password(password)) {
        error = "Wi-Fi passwords must contain 8 to 63 printable characters.";
        return false;
    }
    const NetworkInfo info = get_network_info(true);
    if (!info.network_manager) {
        error = "NetworkManager is not available on this device.";
        return false;
    }
    if (!info.wifi_hardware) {
        error = "No Wi-Fi hardware was detected.";
        return false;
    }

    // Never put the Wi-Fi password in argv: it would be visible through the
    // process table. For secured networks nmcli reads the secret from stdin via
    // --ask. Open networks need no secret and use the normal argument list.
    std::vector<std::string> args = {"-t", "device", "wifi", "connect", ssid};
    CommandResult result;
    if (!password.empty()) {
        args.insert(args.begin(), "--ask");
        std::vector<std::string> command;
        command.reserve(args.size() + 1);
        command.push_back(nmcli_path());
        command.insert(command.end(), args.begin(), args.end());
        const std::string input = password + "\n";
        result = run_command(command, 45000, &input);
    } else {
        result = run_nmcli(args, 45000);
    }
    if (!result.launched || result.exitCode != 0 || result.timedOut) {
        error = result.timedOut ? "The connection attempt timed out." :
                "NetworkManager could not activate the selected network.";
        return false;
    }
    // Verify the OS actually reports the new connection instead of trusting the
    // command's exit status alone.
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    const NetworkInfo after = get_network_info(true);
    if (!after.connected || (after.wifi && !after.ssid.empty() && after.ssid != ssid)) {
        error = "NetworkManager did not confirm the new connection.";
        return false;
    }
    return true;
}

PowerStatus SystemService::get_power_status() const {
    PowerStatus status;
#if !defined(_WIN32)
    const bool systemd = access("/run/systemd/system", F_OK) == 0;
    const bool systemctl = access(systemctl_path().c_str(), X_OK) == 0;
    status.available = systemd && systemctl;
    status.restart = status.available;
    status.shutdown = status.available;
    status.integration = "systemd";
    if (!systemd) status.reason = "systemd is not running on this device.";
    else if (!systemctl) status.reason = "systemctl is not available.";
#else
    status.integration = "unsupported";
    status.reason = "System power operations are not supported by this build.";
#endif
    return status;
}

bool SystemService::request_power(const std::string& operation, std::string& error) {
    error.clear();
    if (operation != "restart" && operation != "shutdown") {
        error = "Unsupported power operation.";
        return false;
    }
    const PowerStatus status = get_power_status();
    if (!status.available) {
        error = status.reason.empty() ? "System power management is unavailable." : status.reason;
        return false;
    }
    const std::vector<std::string> args = {
        systemctl_path(), operation == "restart" ? "reboot" : "poweroff", "--no-block"};
    const auto result = run_command(args, 8000);
    if (!result.launched || result.exitCode != 0 || result.timedOut) {
        error = result.timedOut ? "The operating system did not accept the power request in time." :
                "The operating system rejected the power request (exit " +
                std::to_string(result.exitCode) + ").";
        return false;
    }
    return true;
}

WebAccessInfo SystemService::get_web_access(uint16_t port,
                                            const std::string& bind_address,
                                            bool lan_enabled) {
    WebAccessInfo access;
    access.scheme = "http";
    access.bind_address = bind_address;
    access.port = port;
    access.lan_enabled = lan_enabled;
    access.local_url = "http://127.0.0.1:" + std::to_string(port) + "/";

    const NetworkInfo network = get_network_info(false);
    // The Web listener is IPv4-only. Never advertise an IPv6 URL that the
    // runtime cannot actually serve.
    const std::string address = network.ipv4;
    if (lan_enabled && !address.empty() && bind_address != "127.0.0.1" &&
        bind_address != "localhost") {
        access.lan_url = "http://" + address + ":" + std::to_string(port) + "/";
        access.url = access.lan_url;
        access.local_only = false;
        access.address_available = true;
    } else if (lan_enabled && address.empty() &&
               bind_address != "127.0.0.1" && bind_address != "localhost") {
        access.local_only = false;
        access.address_available = false;
        access.url.clear();
    } else {
        access.url = access.local_url;
        access.local_only = true;
        access.address_available = false;
    }
    return access;
}

} // namespace prismix::system
