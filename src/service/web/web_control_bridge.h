#pragma once

#include "web_control_types.h"
#include <string>
#include <memory>
#include <functional>
#include <mutex>

struct ChannelState;

namespace prismix::web {

class WebControlBridge {
public:
    static WebControlBridge& instance();

    // State inspection
    uint64_t get_state_version() const;
    void bump_state_version();

    std::string get_state_json(bool include_meters = true) const;
    std::string get_catalog_json() const;

    // Command execution
    CommandResult execute_command(const WebCommand& cmd);
    CommandResult execute_json_command(const std::string& json_str);

    // Event listeners
    using StateChangeCallback = std::function<void(uint64_t new_version)>;
    void set_state_change_callback(StateChangeCallback cb);

    // Concurrency control for thread-safe state access
    std::mutex& mutex() { return mutex_; }

private:
    WebControlBridge();
    ~WebControlBridge() = default;

    WebControlBridge(const WebControlBridge&) = delete;
    WebControlBridge& operator=(const WebControlBridge&) = delete;

    mutable std::mutex mutex_;
    uint64_t state_version_ = 1;
    StateChangeCallback change_cb_;
};

} // namespace prismix::web

