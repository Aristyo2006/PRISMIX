#pragma once

#include <string>
#include <memory>
#include <cstdint>

namespace prismix::web {

class WebServer {
public:
    static WebServer& instance();

    bool start(uint16_t port = 8888, const std::string& bind_addr = "0.0.0.0");
    void stop();
    bool is_running() const;
    uint16_t port() const;

private:
    WebServer();
    ~WebServer();

    WebServer(const WebServer&) = delete;
    WebServer& operator=(const WebServer&) = delete;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace prismix::web

