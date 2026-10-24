#include "web_server.h"
#include "web_control_bridge.h"
#include "web_assets.h"

#include <iostream>
#include <thread>
#include <atomic>
#include <vector>
#include <mutex>
#include <cstring>
#include <sstream>
#include <chrono>

#if defined(_WIN32)
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
constexpr socket_t INVALID_SOCK = INVALID_SOCKET;
inline int close_sock(socket_t s) { return closesocket(s); }
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
using socket_t = int;
constexpr socket_t INVALID_SOCK = -1;
inline int close_sock(socket_t s) { return close(s); }
#endif

namespace prismix::web {

struct WebServer::Impl {
    std::atomic<bool> running{false};
    uint16_t port{8888};
    std::string bind_addr{"0.0.0.0"};
    socket_t server_fd{INVALID_SOCK};
    std::thread thread;

    std::mutex clients_mtx;
    std::vector<socket_t> sse_clients;

    void broadcast_sse_event(uint64_t version) {
        std::lock_guard<std::mutex> lock(clients_mtx);
        if (sse_clients.empty()) return;

        std::string msg = "data: {\"event\":\"state_changed\",\"version\":" + std::to_string(version) + "}\n\n";
        auto it = sse_clients.begin();
        while (it != sse_clients.end()) {
            socket_t client = *it;
#if defined(_WIN32)
            int n = send(client, msg.c_str(), static_cast<int>(msg.length()), 0);
#else
            int n = send(client, msg.c_str(), msg.length(), MSG_NOSIGNAL);
#endif
            if (n <= 0) {
                close_sock(client);
                it = sse_clients.erase(it);
            } else {
                ++it;
            }
        }
    }

    void handle_http_client(socket_t client_fd) {
        char buffer[4096];
        int bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_read <= 0) {
            close_sock(client_fd);
            return;
        }
        buffer[bytes_read] = '\0';
        std::string req(buffer, bytes_read);

        std::istringstream req_stream(req);
        std::string method, path, proto;
        req_stream >> method >> path >> proto;

        // Strip query string for path matching
        std::string clean_path = path;
        size_t q_pos = clean_path.find('?');
        if (q_pos != std::string::npos) {
            clean_path = clean_path.substr(0, q_pos);
        }

        // CORS headers for all responses
        const std::string cors = 
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type\r\n";

        if (method == "OPTIONS") {
            std::string resp = "HTTP/1.1 204 No Content\r\n" + cors + "Content-Length: 0\r\n\r\n";
            send(client_fd, resp.c_str(), static_cast<int>(resp.length()), 0);
            close_sock(client_fd);
            return;
        }

        if (method == "GET") {
            if (clean_path == "/" || clean_path == "/index.html") {
                const std::string& html = get_web_ui_html();
                std::ostringstream ss;
                ss << "HTTP/1.1 200 OK\r\n"
                   << "Content-Type: text/html; charset=utf-8\r\n"
                   << "Content-Length: " << html.length() << "\r\n"
                   << cors
                   << "Connection: close\r\n\r\n"
                   << html;
                std::string resp = ss.str();
                send(client_fd, resp.c_str(), static_cast<int>(resp.length()), 0);
                close_sock(client_fd);
                return;
            }
            if (clean_path == "/api/state") {
                std::string json = WebControlBridge::instance().get_state_json(true);
                std::ostringstream ss;
                ss << "HTTP/1.1 200 OK\r\n"
                   << "Content-Type: application/json; charset=utf-8\r\n"
                   << "Content-Length: " << json.length() << "\r\n"
                   << cors
                   << "Connection: close\r\n\r\n"
                   << json;
                std::string resp = ss.str();
                send(client_fd, resp.c_str(), static_cast<int>(resp.length()), 0);
                close_sock(client_fd);
                return;
            }
            if (clean_path == "/api/catalog") {
                std::string json = WebControlBridge::instance().get_catalog_json();
                std::ostringstream ss;
                ss << "HTTP/1.1 200 OK\r\n"
                   << "Content-Type: application/json; charset=utf-8\r\n"
                   << "Content-Length: " << json.length() << "\r\n"
                   << cors
                   << "Connection: close\r\n\r\n"
                   << json;
                std::string resp = ss.str();
                send(client_fd, resp.c_str(), static_cast<int>(resp.length()), 0);
                close_sock(client_fd);
                return;
            }
            if (clean_path == "/api/events") {
                // Server-Sent Events stream
                std::ostringstream ss;
                ss << "HTTP/1.1 200 OK\r\n"
                   << "Content-Type: text/event-stream; charset=utf-8\r\n"
                   << "Cache-Control: no-cache\r\n"
                   << "Connection: keep-alive\r\n"
                   << cors
                   << "\r\n"
                   << "data: {\"event\":\"connected\",\"version\":"
                   << WebControlBridge::instance().get_state_version() << "}\n\n";
                std::string resp = ss.str();
                send(client_fd, resp.c_str(), static_cast<int>(resp.length()), 0);

                // Add to active SSE clients list (keeps connection open)
                {
                    std::lock_guard<std::mutex> lock(clients_mtx);
                    sse_clients.push_back(client_fd);
                }
                return;
            }
        }

        if (method == "POST") {
            if (clean_path == "/api/command") {
                size_t body_pos = req.find("\r\n\r\n");
                std::string body;
                if (body_pos != std::string::npos) {
                    body = req.substr(body_pos + 4);
                }
                auto result = WebControlBridge::instance().execute_json_command(body);
                std::ostringstream ss;
                ss << "HTTP/1.1 200 OK\r\n"
                   << "Content-Type: application/json; charset=utf-8\r\n"
                   << cors
                   << "Connection: close\r\n\r\n"
                   << "{\"success\":" << (result.success ? "true" : "false")
                   << ",\"version\":" << result.state_version
                   << (result.error.empty() ? "" : ",\"error\":\"" + result.error + "\"")
                   << "}";
                std::string resp = ss.str();
                send(client_fd, resp.c_str(), static_cast<int>(resp.length()), 0);
                close_sock(client_fd);
                return;
            }
        }

        // 404 fallback
        std::string not_found = "HTTP/1.1 404 Not Found\r\nContent-Length: 13\r\n\r\n404 Not Found";
        send(client_fd, not_found.c_str(), static_cast<int>(not_found.length()), 0);
        close_sock(client_fd);
    }

    void run_server_loop() {
#if defined(_WIN32)
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd == INVALID_SOCK) {
            std::cerr << "[WebServer] Failed to create socket\n";
            return;
        }

        int opt = 1;
#if defined(_WIN32)
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif
#endif

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = inet_addr(bind_addr.c_str());

        if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            std::cerr << "[WebServer] Failed to bind to " << bind_addr << ":" << port << "\n";
            close_sock(server_fd);
            server_fd = INVALID_SOCK;
            return;
        }

        if (listen(server_fd, 16) < 0) {
            std::cerr << "[WebServer] Failed to listen\n";
            close_sock(server_fd);
            server_fd = INVALID_SOCK;
            return;
        }

        std::cout << "[WebServer] PRISMIX Web Control Server listening on http://"
                  << (bind_addr == "0.0.0.0" ? "127.0.0.1" : bind_addr) << ":" << port << "\n";

        // Register callback with WebControlBridge for instant event broadcasts
        WebControlBridge::instance().set_state_change_callback([this](uint64_t v) {
            this->broadcast_sse_event(v);
        });

        auto last_ping = std::chrono::steady_clock::now();

        while (running.load()) {
#if defined(_WIN32)
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(server_fd, &read_fds);
            timeval tv{0, 200000}; // 200 ms
            int ready = select(0, &read_fds, nullptr, nullptr, &tv);
#else
            struct pollfd pfd{};
            pfd.fd = server_fd;
            pfd.events = POLLIN;
            int ready = poll(&pfd, 1, 200); // 200 ms timeout
#endif
            if (ready > 0) {
                sockaddr_in client_addr{};
                socklen_t client_len = sizeof(client_addr);
                socket_t client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
                if (client_fd != INVALID_SOCK) {
                    handle_http_client(client_fd);
                }
            }

            // Periodic SSE keepalive comment (: ping\n\n) every 5 seconds
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_ping).count() >= 5) {
                last_ping = now;
                std::lock_guard<std::mutex> lock(clients_mtx);
                if (!sse_clients.empty()) {
                    const char ping_msg[] = ": ping\n\n";
                    auto it = sse_clients.begin();
                    while (it != sse_clients.end()) {
                        socket_t c = *it;
#if defined(_WIN32)
                        int n = send(c, ping_msg, (int)sizeof(ping_msg) - 1, 0);
#else
                        int n = send(c, ping_msg, sizeof(ping_msg) - 1, MSG_NOSIGNAL);
#endif
                        if (n <= 0) {
                            close_sock(c);
                            it = sse_clients.erase(it);
                        } else {
                            ++it;
                        }
                    }
                }
            }
        }

        // Cleanup
        {
            std::lock_guard<std::mutex> lock(clients_mtx);
            for (socket_t c : sse_clients) {
                close_sock(c);
            }
            sse_clients.clear();
        }

        if (server_fd != INVALID_SOCK) {
            close_sock(server_fd);
            server_fd = INVALID_SOCK;
        }

#if defined(_WIN32)
        WSACleanup();
#endif
    }
};

WebServer& WebServer::instance() {
    static WebServer s_instance;
    return s_instance;
}

WebServer::WebServer() : impl_(std::make_unique<Impl>()) {}

WebServer::~WebServer() {
    stop();
}

bool WebServer::start(uint16_t port, const std::string& bind_addr) {
    if (impl_->running.load()) return true;
    impl_->port = port;
    impl_->bind_addr = bind_addr;
    impl_->running.store(true);
    impl_->thread = std::thread([this]() {
        impl_->run_server_loop();
    });
    return true;
}

void WebServer::stop() {
    if (!impl_->running.load()) return;
    impl_->running.store(false);
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }
}

bool WebServer::is_running() const {
    return impl_->running.load();
}

uint16_t WebServer::port() const {
    return impl_->port;
}

} // namespace prismix::web

