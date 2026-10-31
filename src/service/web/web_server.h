#pragma once

#include <string>
#include <memory>
#include <cstdint>

namespace prismix::web {

class WebServer {
public:
    static WebServer& instance();

    bool start(uint16_t port = 8888, const std::string& bind_addr = "127.0.0.1");
    void stop();
    bool is_running() const;
    uint16_t port() const;
    std::string bind_address() const;

    // Applies a listener rebind on the server thread. Used only by the
    // authenticated LAN-access setting; the next request completes before the
    // listener is replaced.
    bool request_bind_address(const std::string& bind_addr);

private:
    WebServer();
    ~WebServer();

    WebServer(const WebServer&) = delete;
    WebServer& operator=(const WebServer&) = delete;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace prismix::web
