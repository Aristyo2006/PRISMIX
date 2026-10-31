#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace prismix::system {

struct CommandResult {
    bool launched = false;
    bool timedOut = false;
    int exitCode = -1;
    std::string output;
    std::string errorText;
};

struct NetworkInfo {
    bool network_manager = false;
    bool wifi_hardware = false;
    bool connected = false;
    bool wifi = false;
    std::string nm_state;
    std::string interface_name;
    std::string connection_type;
    std::string connection_name;
    std::string device_state;
    std::string ssid;
    std::string ipv4;
    std::string ipv6;
    int signal_percent = -1;
    std::string error;
};

struct WifiNetwork {
    std::string ssid;
    int signal_percent = -1;
    std::string security;
    bool active = false;
    bool secured = false;
};

struct WifiScanResult {
    bool network_manager = false;
    bool wifi_hardware = false;
    bool success = false;
    std::string error;
    std::vector<WifiNetwork> networks;
};

struct PowerStatus {
    bool available = false;
    bool restart = false;
    bool shutdown = false;
    std::string integration;
    std::string reason;
};

struct WebAccessInfo {
    std::string scheme;
    std::string bind_address;
    uint16_t port = 0;
    bool lan_enabled = false;
    bool local_only = true;
    bool address_available = false;
    std::string url;
    std::string local_url;
    std::string lan_url;
};

class SystemService {
public:
    static SystemService& instance();
    ~SystemService();

    SystemService(const SystemService&) = delete;
    SystemService& operator=(const SystemService&) = delete;

    NetworkInfo get_network_info(bool force_refresh = false);

    // Performs a blocking NetworkManager scan. Kept public for tests and for
    // the asynchronous worker below.
    WifiScanResult scan_wifi();

    // UI/server callers use the asynchronous scan so an in-flight NetworkManager
    // scan never stalls HTTP handling or the LVGL event loop. `force` starts a
    // fresh scan even when a previous result is cached.
    void start_wifi_scan(bool force = false);
    WifiScanResult wifi_scan_snapshot(bool& scanning);
    bool connect_wifi(const std::string& ssid, const std::string& password,
                      std::string& error);

    PowerStatus get_power_status() const;
    bool request_power(const std::string& operation, std::string& error);

    WebAccessInfo get_web_access(uint16_t port, const std::string& bind_address,
                                 bool lan_enabled);

    // Test seams for OS operations. They never expose arbitrary browser input:
    // the runner still receives only fixed commands produced by allow-listed
    // operations.
    using CommandRunner = std::function<CommandResult(const std::vector<std::string>&)>;
    void setCommandRunnerForTesting(CommandRunner runner);
    void resetCommandRunnerForTesting();

private:
    SystemService() = default;

    CommandResult run_command(const std::vector<std::string>& args, int timeoutMs,
                              const std::string* stdin_data = nullptr) const;
    CommandResult run_nmcli(const std::vector<std::string>& args, int timeoutMs) const;

    mutable std::mutex mutex_;
    NetworkInfo cached_network_;
    int64_t cached_at_ms_ = 0;
    CommandRunner test_runner_;

    std::mutex scan_mutex_;
    std::thread scan_thread_;
    WifiScanResult scan_result_;
    bool scan_running_ = false;
    bool scan_started_ = false;
};

// Splits an nmcli terse line on unescaped colons and unescapes `\:` / `\\`.
std::vector<std::string> split_nmcli_line(const std::string& line);
std::vector<WifiNetwork> parse_nmcli_wifi_list(const std::string& output);

} // namespace prismix::system
