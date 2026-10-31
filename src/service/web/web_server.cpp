#include "web_server.h"
#include "web_control_bridge.h"
#include "web_assets.h"
#include "../security/security_service.h"
#include "../system/system_service.h"
#include "../audio_service.h"
#include "../../ui/ui_audio_bridge.h"
#include "../../backend/mock_audio_backend.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
constexpr socket_t INVALID_SOCK = INVALID_SOCKET;
inline int close_sock(socket_t s) { return closesocket(s); }
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
using socket_t = int;
constexpr socket_t INVALID_SOCK = -1;
inline int close_sock(socket_t s) { return close(s); }
#endif

namespace prismix::web {

namespace {

constexpr std::size_t kMaxHeaderBytes = 32 * 1024;
constexpr std::size_t kMaxBodyBytes = 64 * 1024;

struct HttpRequest {
    std::string method;
    std::string path;
    std::string query;
    std::map<std::string, std::string> headers;
    std::string body;
    std::string client_ip;
    bool loopback = false;
};

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string json_escape(const std::string& value) {
    std::ostringstream out;
    for (unsigned char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c <= 0x1f) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(c);
                } else {
                    out << static_cast<char>(c);
                }
        }
    }
    return out.str();
}

void append_utf8(std::string& out, unsigned int cp) {
    if (cp <= 0x7f) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7ff) {
        out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else if (cp <= 0xffff) {
        out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else if (cp <= 0x10ffff) {
        out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
}

bool parse_json_string(const std::string& body, const std::string& key,
                       std::string& out, bool* found = nullptr) {
    if (found) *found = false;
    const std::string needle = "\"" + key + "\"";
    std::size_t pos = 0;
    while ((pos = body.find(needle, pos)) != std::string::npos) {
        const std::size_t before = pos;
        pos += needle.size();
        bool key_boundary = true;
        if (before > 0) {
            const unsigned char prev = static_cast<unsigned char>(body[before - 1]);
            if (prev != '{' && prev != ',' && prev != ':' && !std::isspace(prev)) key_boundary = false;
        }
        if (!key_boundary) continue;
        while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) ++pos;
        if (pos >= body.size() || body[pos] != ':') continue;
        ++pos;
        while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) ++pos;
        if (pos >= body.size() || body[pos] != '"') continue;
        ++pos;
        out.clear();
        bool closed = false;
        while (pos < body.size()) {
            const char c = body[pos++];
            if (c == '"') {
                closed = true;
                break;
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (pos >= body.size()) break;
            const char esc = body[pos++];
            switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (pos + 4 > body.size()) break;
                    unsigned int cp = 0;
                    bool valid = true;
                    for (int i = 0; i < 4; ++i) {
                        const char h = body[pos + static_cast<std::size_t>(i)];
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= static_cast<unsigned int>(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned int>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned int>(h - 'A' + 10);
                        else { valid = false; break; }
                    }
                    if (valid) {
                        pos += 4;
                        append_utf8(out, cp);
                    }
                    break;
                }
                default: out.push_back(esc); break;
            }
        }
        if (closed) {
            if (found) *found = true;
            return true;
        }
    }
    return false;
}

bool parse_json_bool(const std::string& body, const std::string& key,
                     bool& out, bool* found = nullptr) {
    if (found) *found = false;
    const std::string needle = "\"" + key + "\"";
    std::size_t pos = body.find(needle);
    if (pos == std::string::npos) return false;
    pos += needle.size();
    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) ++pos;
    if (pos >= body.size() || body[pos] != ':') return false;
    ++pos;
    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) ++pos;
    if (body.compare(pos, 4, "true") == 0) {
        out = true;
        if (found) *found = true;
        return true;
    }
    if (body.compare(pos, 5, "false") == 0) {
        out = false;
        if (found) *found = true;
        return true;
    }
    return false;
}

bool is_loopback_address(const std::string& address) {
    return address == "127.0.0.1" || address == "::1" ||
           address == "::ffff:127.0.0.1" || address.rfind("127.", 0) == 0;
}

std::string reason_phrase(int code) {
    switch (code) {
        case 200: return "OK";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 409: return "Conflict";
        case 423: return "Locked";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        default: return "Error";
    }
}

bool send_all(socket_t fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
#if defined(_WIN32)
        const int n = send(fd, data.data() + sent,
                           static_cast<int>(std::min<std::size_t>(data.size() - sent, 65536)), 0);
#else
        const ssize_t n = send(fd, data.data() + sent,
                               std::min<std::size_t>(data.size() - sent, 65536), MSG_NOSIGNAL);
#endif
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

void send_response(socket_t fd, int code, const std::string& body,
                   const std::string& content_type,
                   const std::vector<std::string>& extra_headers = {}) {
    std::ostringstream head;
    head << "HTTP/1.1 " << code << ' ' << reason_phrase(code) << "\r\n"
         << "Content-Type: " << content_type << "\r\n"
         << "Content-Length: " << body.size() << "\r\n"
         << "Cache-Control: no-store\r\n"
         << "X-Content-Type-Options: nosniff\r\n"
         << "X-Frame-Options: DENY\r\n"
         << "Referrer-Policy: no-referrer\r\n"
         << "Content-Security-Policy: default-src 'self'; img-src 'self' data:; "
            "style-src 'self' 'unsafe-inline'; script-src 'self' 'unsafe-inline'; "
            "connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'\r\n"
         << "Permissions-Policy: camera=(), microphone=(), geolocation=()\r\n"
         << "Connection: close\r\n";
    for (const auto& header : extra_headers) head << header << "\r\n";
    head << "\r\n";
    const std::string header = head.str();
    send_all(fd, header + body);
}

void send_json(socket_t fd, int code, const std::string& body,
               const std::vector<std::string>& extra_headers = {}) {
    send_response(fd, code, body, "application/json; charset=utf-8", extra_headers);
}

void send_error(socket_t fd, int code, const std::string& error,
                const std::string& extra_json = {},
                const std::vector<std::string>& extra_headers = {}) {
    std::string body = "{\"success\":false,\"error\":\"" + json_escape(error) + "\"";
    if (!extra_json.empty()) body += "," + extra_json;
    body += "}";
    send_json(fd, code, body, extra_headers);
}

std::string cookie_header_value(const HttpRequest& req) {
    const auto it = req.headers.find("cookie");
    return it == req.headers.end() ? std::string{} : it->second;
}

std::string security_status_json(const std::string& session_token, bool authenticated,
                                 const std::string& error = {}) {
    auto& security = prismix::security::SecurityService::instance();
    std::ostringstream out;
    out << "{\"configured\":" << (security.isPinConfigured() ? "true" : "false")
        << ",\"locked\":" << (security.isMixerLocked() ? "true" : "false")
        << ",\"authenticated\":" << (authenticated ? "true" : "false")
        << ",\"pin_required\":" << (security.isPinConfigured() ? "true" : "false")
        << ",\"lan_access\":" << (security.isLanAccessEnabled() ? "true" : "false")
        << ",\"session_remaining\":"
        << (authenticated ? security.sessionRemainingSeconds(session_token) : 0)
        << ",\"error\":\"" << json_escape(error) << "\"}";
    return out.str();
}

} // namespace

struct WebServer::Impl {
    std::atomic<bool> running{false};
    uint16_t port{8888};
    std::string bind_addr{"127.0.0.1"};
    std::string actual_bind{"127.0.0.1"};
    socket_t server_fd{INVALID_SOCK};
    std::thread thread;

    mutable std::mutex config_mtx;
    std::string pending_bind;
    bool rebind_requested = false;

    struct SseClient {
        socket_t fd = INVALID_SOCK;
        std::string session_token;
    };

    std::mutex clients_mtx;
    std::vector<SseClient> sse_clients;

    bool sse_session_valid(const std::string& token) const {
        auto& security = prismix::security::SecurityService::instance();
        if (!security.isPinConfigured()) return true;
        return security.validateSession(token);
    }

    void broadcast_sse_event(uint64_t version) {
        std::lock_guard<std::mutex> lock(clients_mtx);
        if (sse_clients.empty()) return;
        const std::string msg =
            "data: {\"event\":\"state_changed\",\"version\":" +
            std::to_string(version) + "}\n\n";
        auto it = sse_clients.begin();
        while (it != sse_clients.end()) {
            if (!sse_session_valid(it->session_token)) {
                close_sock(it->fd);
                it = sse_clients.erase(it);
                continue;
            }
#if defined(_WIN32)
            const int n = send(it->fd, msg.c_str(), static_cast<int>(msg.length()), 0);
#else
            const int n = send(it->fd, msg.c_str(), msg.length(), MSG_NOSIGNAL);
#endif
            if (n <= 0) {
                close_sock(it->fd);
                it = sse_clients.erase(it);
            } else {
                ++it;
            }
        }
    }

    void broadcast_security_event() {
        std::lock_guard<std::mutex> lock(clients_mtx);
        const char msg[] = "data: {\"event\":\"security_changed\"}\n\n";
        auto it = sse_clients.begin();
        while (it != sse_clients.end()) {
            if (!sse_session_valid(it->session_token)) {
                close_sock(it->fd);
                it = sse_clients.erase(it);
                continue;
            }
#if defined(_WIN32)
            const int n = send(it->fd, msg, static_cast<int>(sizeof(msg) - 1), 0);
#else
            const int n = send(it->fd, msg, sizeof(msg) - 1, MSG_NOSIGNAL);
#endif
            if (n <= 0) {
                close_sock(it->fd);
                it = sse_clients.erase(it);
            } else {
                ++it;
            }
        }
    }

    void disconnect_sse_clients() {
        std::lock_guard<std::mutex> lock(clients_mtx);
        for (const auto& client : sse_clients) close_sock(client.fd);
        sse_clients.clear();
    }

    void disconnect_sse_client(const std::string& session_token) {
        if (session_token.empty()) return;
        std::lock_guard<std::mutex> lock(clients_mtx);
        auto it = sse_clients.begin();
        while (it != sse_clients.end()) {
            if (it->session_token == session_token) {
                close_sock(it->fd);
                it = sse_clients.erase(it);
            } else {
                ++it;
            }
        }
    }

    std::string current_bind() const {
        std::lock_guard<std::mutex> lock(config_mtx);
        return actual_bind;
    }

    void request_bind(const std::string& address) {
        std::lock_guard<std::mutex> lock(config_mtx);
        pending_bind = address;
        rebind_requested = true;
    }

    bool open_listener(const std::string& address, std::string& error) {
        socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd == INVALID_SOCK) {
            error = "socket creation failed";
            return false;
        }
        int opt = 1;
#if defined(_WIN32)
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#ifdef SO_REUSEPORT
        setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
#endif
#endif
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (inet_pton(AF_INET, address.c_str(), &addr.sin_addr) != 1) {
            close_sock(fd);
            error = "invalid bind address";
            return false;
        }
        if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            close_sock(fd);
            error = "bind failed";
            return false;
        }
        if (listen(fd, 16) < 0) {
            close_sock(fd);
            error = "listen failed";
            return false;
        }
        server_fd = fd;
        {
            std::lock_guard<std::mutex> lock(config_mtx);
            actual_bind = address;
            pending_bind.clear();
            rebind_requested = false;
        }
        std::cout << "[WebServer] PRISMIX Web Control Server listening on http://"
                  << (address == "0.0.0.0" ? "0.0.0.0" : address) << ":" << port << "\n";
        return true;
    }

    void apply_pending_bind() {
        std::string requested;
        {
            std::lock_guard<std::mutex> lock(config_mtx);
            if (!rebind_requested) return;
            requested = pending_bind;
            rebind_requested = false;
        }
        if (requested.empty() || requested == current_bind()) return;
        if (server_fd != INVALID_SOCK) {
            close_sock(server_fd);
            server_fd = INVALID_SOCK;
        }
        std::string error;
        if (open_listener(requested, error)) return;
        std::cerr << "[WebServer] Failed to bind " << requested << ": " << error << "\n";
        const std::string previous = current_bind();
        if (open_listener(previous, error)) {
            std::cerr << "[WebServer] Restored listener on " << previous << "\n";
        } else {
            std::cerr << "[WebServer] Failed to restore listener: " << error << "\n";
            running.store(false);
        }
    }

    bool read_request(socket_t client_fd, const std::string& client_ip,
                      HttpRequest& request) {
#if !defined(_WIN32)
        struct timeval timeout{5, 0};
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
#endif
        std::string raw;
        char buffer[4096];
        std::size_t header_end = std::string::npos;
        std::size_t content_length = 0;
        while (raw.size() < kMaxHeaderBytes + kMaxBodyBytes) {
            const int n = recv(client_fd, buffer, sizeof(buffer), 0);
            if (n <= 0) break;
            raw.append(buffer, static_cast<std::size_t>(n));
            header_end = raw.find("\r\n\r\n");
            if (header_end == std::string::npos) continue;
            if (header_end > kMaxHeaderBytes) return false;
            const std::string headers = raw.substr(0, header_end + 4);
            const auto lower_headers = lower(headers);
            const std::size_t cl = lower_headers.find("content-length:");
            if (cl != std::string::npos) {
                const std::size_t line_end = lower_headers.find("\r\n", cl);
                const std::string value = lower_headers.substr(cl + 15,
                    (line_end == std::string::npos ? lower_headers.size() : line_end) - (cl + 15));
                try {
                    content_length = static_cast<std::size_t>(std::stoul(value));
                } catch (...) {
                    return false;
                }
                if (content_length > kMaxBodyBytes) return false;
            }
            if (raw.size() >= header_end + 4 + content_length) break;
        }
        if (header_end == std::string::npos) return false;
        const std::string header_block = raw.substr(0, header_end);
        std::istringstream lines(header_block);
        std::string line;
        if (!std::getline(lines, line)) return false;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream request_line(line);
        request_line >> request.method >> request.path >> std::ws;
        if (request.method.empty() || request.path.empty()) return false;
        const std::size_t query = request.path.find('?');
        if (query != std::string::npos) {
            request.query = request.path.substr(query + 1);
            request.path = request.path.substr(0, query);
        }
        while (std::getline(lines, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const std::size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            const std::string key = lower(line.substr(0, colon));
            std::size_t start = colon + 1;
            while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start]))) ++start;
            request.headers[key] = line.substr(start);
        }
        request.body = raw.substr(header_end + 4, content_length);
        request.client_ip = client_ip;
        request.loopback = is_loopback_address(client_ip);
        return true;
    }

    void handle_http_client(socket_t client_fd, const std::string& client_ip) {
        HttpRequest req;
        if (!read_request(client_fd, client_ip, req)) {
            send_error(client_fd, 400, "Malformed HTTP request.");
            close_sock(client_fd);
            return;
        }

        auto& security = prismix::security::SecurityService::instance();
        const std::string session_token =
            prismix::security::SecurityService::cookieValue(cookie_header_value(req), "prismix_session");
        const bool authenticated = security.validateSession(session_token);
        const bool pin_configured = security.isPinConfigured();
        const bool locked = security.isMixerLocked();

        auto require_auth = [&]() -> bool {
            if (!pin_configured) {
                if (!req.loopback) {
                    send_error(client_fd, 403, "Local access only until a mixer PIN is configured.");
                    return false;
                }
                return true;
            }
            if (!authenticated) {
                send_error(client_fd, 401, "Authentication required.",
                           "\"auth_required\":true",
                           {"Set-Cookie: " + prismix::security::SecurityService::makeExpiredSessionCookie()});
                return false;
            }
            return true;
        };

        if (req.method == "OPTIONS") {
            send_response(client_fd, 204, "", "text/plain", {"Allow: GET, POST, OPTIONS"});
            close_sock(client_fd);
            return;
        }

        if (req.method == "GET") {
            if (req.path == "/" || req.path == "/index.html") {
                send_response(client_fd, 200, get_web_ui_html(),
                              "text/html; charset=utf-8");
                close_sock(client_fd);
                return;
            }
            if (req.path == "/favicon.ico") {
                send_response(client_fd, 204, "", "image/x-icon");
                close_sock(client_fd);
                return;
            }
            if (req.path == "/api/auth/status") {
                std::string json = security_status_json(session_token,
                    !pin_configured || authenticated);
                json.pop_back();
                json += ",\"bind_address\":\"" + json_escape(current_bind()) +
                        "\",\"port\":" + std::to_string(port) + "}";
                send_json(client_fd, 200, json);
                close_sock(client_fd);
                return;
            }
            if (req.path == "/api/security/status") {
                if (!require_auth()) { close_sock(client_fd); return; }
                std::string json = security_status_json(session_token, true);
                json.pop_back();
                json += ",\"bind_address\":\"" + json_escape(current_bind()) +
                        "\",\"port\":" + std::to_string(port) + "}";
                send_json(client_fd, 200, json);
                close_sock(client_fd);
                return;
            }
            if (req.path == "/api/state" || req.path == "/api/telemetry" ||
                req.path == "/api/catalog" || req.path == "/api/system/status" ||
                req.path == "/api/system/networks" || req.path == "/api/events") {
                if (!require_auth()) { close_sock(client_fd); return; }
            }

            if (req.path == "/api/state") {
                send_json(client_fd, 200,
                          WebControlBridge::instance().get_state_json(true));
                close_sock(client_fd);
                return;
            }
            if (req.path == "/api/telemetry") {
                send_json(client_fd, 200,
                          WebControlBridge::instance().get_telemetry_json());
                close_sock(client_fd);
                return;
            }
            if (req.path == "/api/catalog") {
                send_json(client_fd, 200,
                          WebControlBridge::instance().get_catalog_json());
                close_sock(client_fd);
                return;
            }
            if (req.path == "/api/system/status") {
                const auto status = prismix::system::SystemService::instance().get_power_status();
                const bool force_network = req.query.find("refresh=1") != std::string::npos;
                const auto network = prismix::system::SystemService::instance().get_network_info(force_network);
                const auto web_access = prismix::system::SystemService::instance().get_web_access(
                    port, current_bind(), security.isLanAccessEnabled());
                std::ostringstream out;
                out << "{\"success\":true"
                    << ",\"power\":{\"available\":" << (status.available ? "true" : "false")
                    << ",\"restart\":" << (status.restart ? "true" : "false")
                    << ",\"shutdown\":" << (status.shutdown ? "true" : "false")
                    << ",\"integration\":\"" << json_escape(status.integration) << "\""
                    << ",\"reason\":\"" << json_escape(status.reason) << "\"}"
                    << ",\"network\":{\"network_manager\":" << (network.network_manager ? "true" : "false")
                    << ",\"wifi_hardware\":" << (network.wifi_hardware ? "true" : "false")
                    << ",\"connected\":" << (network.connected ? "true" : "false")
                    << ",\"wifi\":" << (network.wifi ? "true" : "false")
                    << ",\"interface\":\"" << json_escape(network.interface_name) << "\""
                    << ",\"type\":\"" << json_escape(network.connection_type) << "\""
                    << ",\"connection\":\"" << json_escape(network.connection_name) << "\""
                    << ",\"device_state\":\"" << json_escape(network.device_state) << "\""
                    << ",\"ssid\":\"" << json_escape(network.ssid) << "\""
                    << ",\"signal_percent\":" << network.signal_percent
                    << ",\"ipv4\":\"" << json_escape(network.ipv4) << "\""
                    << ",\"ipv6\":\"" << json_escape(network.ipv6) << "\""
                    << ",\"error\":\"" << json_escape(network.error) << "\"}"
                    << ",\"web_access\":{\"scheme\":\"" << json_escape(web_access.scheme) << "\""
                    << ",\"bind_address\":\"" << json_escape(web_access.bind_address) << "\""
                    << ",\"port\":" << web_access.port
                    << ",\"lan_enabled\":" << (web_access.lan_enabled ? "true" : "false")
                    << ",\"local_only\":" << (web_access.local_only ? "true" : "false")
                    << ",\"address_available\":" << (web_access.address_available ? "true" : "false")
                    << ",\"url\":\"" << json_escape(web_access.url) << "\""
                    << ",\"local_url\":\"" << json_escape(web_access.local_url) << "\""
                    << ",\"lan_url\":\"" << json_escape(web_access.lan_url) << "\""
                    << ",\"transport_secure\":false}"
                    << ",\"security\":{\"configured\":" << (security.isPinConfigured() ? "true" : "false")
                    << ",\"locked\":" << (security.isMixerLocked() ? "true" : "false")
                    << ",\"lan_access\":" << (security.isLanAccessEnabled() ? "true" : "false")
                    << ",\"sessions\":" << security.activeSessionCount()
                    << ",\"session_remaining\":" << security.sessionRemainingSeconds(session_token) << "}"
                    << "}";
                send_json(client_fd, 200, out.str());
                close_sock(client_fd);
                return;
            }
            if (req.path == "/api/system/networks") {
                const bool force_scan = req.query.find("refresh=1") != std::string::npos;
                prismix::system::SystemService::instance().start_wifi_scan(force_scan);
                bool scanning = false;
                const auto scan = prismix::system::SystemService::instance().wifi_scan_snapshot(scanning);
                std::ostringstream out;
                out << "{\"success\":" << (scan.success ? "true" : "false")
                    << ",\"scanning\":" << (scanning ? "true" : "false")
                    << ",\"network_manager\":" << (scan.network_manager ? "true" : "false")
                    << ",\"wifi_hardware\":" << (scan.wifi_hardware ? "true" : "false")
                    << ",\"error\":\"" << json_escape(scan.error) << "\""
                    << ",\"networks\":[";
                for (std::size_t i = 0; i < scan.networks.size(); ++i) {
                    const auto& network = scan.networks[i];
                    if (i > 0) out << ',';
                    out << "{\"ssid\":\"" << json_escape(network.ssid) << "\""
                        << ",\"signal_percent\":" << network.signal_percent
                        << ",\"security\":\"" << json_escape(network.security) << "\""
                        << ",\"active\":" << (network.active ? "true" : "false")
                        << ",\"secured\":" << (network.secured ? "true" : "false") << "}";
                }
                out << "]}";
                send_json(client_fd, 200, out.str());
                close_sock(client_fd);
                return;
            }
            if (req.path == "/api/events") {
                std::ostringstream head;
                head << "HTTP/1.1 200 OK\r\n"
                     << "Content-Type: text/event-stream; charset=utf-8\r\n"
                     << "Cache-Control: no-cache\r\n"
                     << "X-Accel-Buffering: no\r\n"
                     << "Connection: keep-alive\r\n\r\n"
                     << "data: {\"event\":\"connected\",\"version\":"
                     << WebControlBridge::instance().get_state_version() << "}\n\n";
                if (!send_all(client_fd, head.str())) {
                    close_sock(client_fd);
                    return;
                }
                {
                    std::lock_guard<std::mutex> lock(clients_mtx);
                    sse_clients.push_back(SseClient{client_fd, session_token});
                }
                return;
            }
        }

        if (req.method == "POST") {
            const auto content_type = req.headers.find("content-type");
            const bool json_body = content_type != req.headers.end() &&
                lower(content_type->second).rfind("application/json", 0) == 0;
            if (!json_body) {
                send_error(client_fd, 400, "A JSON request body is required.");
                close_sock(client_fd);
                return;
            }

            if (req.path == "/api/auth/login") {
                if (!pin_configured) {
                    send_error(client_fd, 409, "No mixer PIN is configured.");
                    close_sock(client_fd);
                    return;
                }
                if (locked) {
                    send_error(client_fd, 423, "The mixer is locked. Unlock with the configured PIN.",
                               "\"locked\":true");
                    close_sock(client_fd);
                    return;
                }
                std::string pin;
                if (!parse_json_string(req.body, "pin", pin) || pin.empty()) {
                    send_error(client_fd, 400, "Enter the PIN.");
                    close_sock(client_fd);
                    return;
                }
                std::string error;
                int retry = 0;
                if (!security.verifyPin(pin, req.client_ip, error, retry)) {
                    if (retry > 0) {
                        send_error(client_fd, 429, error,
                                   "\"retry_after\":" + std::to_string(retry));
                    } else {
                        send_error(client_fd, 401, error);
                    }
                    close_sock(client_fd);
                    return;
                }
                const std::string token = security.createSession();
                if (token.empty()) {
                    send_error(client_fd, 500, "Could not create a secure session.");
                    close_sock(client_fd);
                    return;
                }
                send_json(client_fd, 200, "{\"success\":true,\"authenticated\":true,\"locked\":false}",
                          {"Set-Cookie: " + prismix::security::SecurityService::makeSessionCookie(
                               token, security.sessionRemainingSeconds(token))});
                close_sock(client_fd);
                return;
            }

            if (req.path == "/api/auth/setup") {
                if (pin_configured) {
                    send_error(client_fd, 409, "A mixer PIN is already configured.");
                    close_sock(client_fd);
                    return;
                }
                if (!req.loopback) {
                    send_error(client_fd, 403, "PIN setup is available only on the local console.");
                    close_sock(client_fd);
                    return;
                }
                std::string pin;
                std::string confirm;
                if (!parse_json_string(req.body, "pin", pin) ||
                    !parse_json_string(req.body, "confirm_pin", confirm)) {
                    send_error(client_fd, 400, "Enter and confirm the new PIN.");
                    close_sock(client_fd);
                    return;
                }
                std::string error;
                if (!security.setupPin(pin, confirm, error)) {
                    send_error(client_fd, 400, error);
                    close_sock(client_fd);
                    return;
                }
                const std::string token = security.createSession();
                disconnect_sse_clients();
                send_json(client_fd, 200,
                          "{\"success\":true,\"configured\":true,\"authenticated\":true}",
                          {"Set-Cookie: " + prismix::security::SecurityService::makeSessionCookie(
                               token, security.sessionRemainingSeconds(token))});
                close_sock(client_fd);
                return;
            }

            if (req.path == "/api/auth/unlock") {
                if (!pin_configured) {
                    send_error(client_fd, 409, "No mixer PIN is configured.");
                    close_sock(client_fd);
                    return;
                }
                std::string pin;
                if (!parse_json_string(req.body, "pin", pin) || pin.empty()) {
                    send_error(client_fd, 400, "Enter the PIN.");
                    close_sock(client_fd);
                    return;
                }
                std::string error;
                int retry = 0;
                if (!security.unlockMixer(pin, req.client_ip, error, retry)) {
                    if (retry > 0) {
                        send_error(client_fd, 429, error,
                                   "\"retry_after\":" + std::to_string(retry));
                    } else {
                        send_error(client_fd, 401, error);
                    }
                    close_sock(client_fd);
                    return;
                }
                const std::string token = security.createSession();
                broadcast_security_event();
                send_json(client_fd, 200, "{\"success\":true,\"authenticated\":true,\"locked\":false}",
                          {"Set-Cookie: " + prismix::security::SecurityService::makeSessionCookie(
                               token, security.sessionRemainingSeconds(token))});
                close_sock(client_fd);
                return;
            }

            if (req.path == "/api/auth/logout" ||
                req.path == "/api/security/logout") {
                if (!require_auth()) { close_sock(client_fd); return; }
                security.revokeSession(session_token);
                disconnect_sse_client(session_token);
                broadcast_security_event();
                send_json(client_fd, 200, "{\"success\":true,\"authenticated\":false}",
                          {"Set-Cookie: " + prismix::security::SecurityService::makeExpiredSessionCookie()});
                close_sock(client_fd);
                return;
            }

            if (req.path == "/api/security/lock") {
                if (!require_auth()) { close_sock(client_fd); return; }
                std::string error;
                if (!security.lockMixer(error)) {
                    send_error(client_fd, 400, error);
                    close_sock(client_fd);
                    return;
                }
                disconnect_sse_clients();
                send_json(client_fd, 200, "{\"success\":true,\"locked\":true}",
                          {"Set-Cookie: " + prismix::security::SecurityService::makeExpiredSessionCookie()});
                close_sock(client_fd);
                return;
            }

            if (req.path == "/api/security/pin") {
                if (!require_auth()) { close_sock(client_fd); return; }
                std::string current_pin;
                std::string new_pin;
                std::string confirm_pin;
                if (!parse_json_string(req.body, "current_pin", current_pin) ||
                    !parse_json_string(req.body, "new_pin", new_pin) ||
                    !parse_json_string(req.body, "confirm_pin", confirm_pin)) {
                    send_error(client_fd, 400, "Enter the current PIN and confirm the new PIN.");
                    close_sock(client_fd);
                    return;
                }
                std::string error;
                int retry = 0;
                if (!security.changePin(current_pin, new_pin, confirm_pin,
                                        req.client_ip, error, retry)) {
                    if (retry > 0) {
                        send_error(client_fd, 429, error,
                                   "\"retry_after\":" + std::to_string(retry));
                    } else {
                        send_error(client_fd, 400, error);
                    }
                    close_sock(client_fd);
                    return;
                }
                const std::string token = security.createSession();
                disconnect_sse_clients();
                broadcast_security_event();
                send_json(client_fd, 200, "{\"success\":true,\"configured\":true}",
                          {"Set-Cookie: " + prismix::security::SecurityService::makeSessionCookie(
                               token, security.sessionRemainingSeconds(token))});
                close_sock(client_fd);
                return;
            }

            if (req.path == "/api/security/lan") {
                if (!require_auth()) { close_sock(client_fd); return; }
                bool enabled = false;
                bool found = false;
                if (!parse_json_bool(req.body, "enabled", enabled, &found) || !found) {
                    send_error(client_fd, 400, "Specify whether LAN access should be enabled.");
                    close_sock(client_fd);
                    return;
                }
                std::string error;
                if (!security.setLanAccess(enabled, error)) {
                    send_error(client_fd, 400, error);
                    close_sock(client_fd);
                    return;
                }
                const std::string desired_bind = enabled ? "0.0.0.0" : "127.0.0.1";
                request_bind(desired_bind);
                broadcast_security_event();
                send_json(client_fd, 200,
                          "{\"success\":true,\"lan_access\":" +
                              std::string(enabled ? "true" : "false") +
                              ",\"bind_pending\":true,\"bind_address\":\"" +
                              json_escape(desired_bind) + "\"}");
                close_sock(client_fd);
                return;
            }

            if (req.path == "/api/system/power") {
                if (!require_auth()) { close_sock(client_fd); return; }
                std::string operation;
                bool confirmed = false;
                bool confirm_found = false;
                if (!parse_json_string(req.body, "operation", operation) ||
                    !parse_json_bool(req.body, "confirm", confirmed, &confirm_found) ||
                    !confirm_found || !confirmed) {
                    send_error(client_fd, 400, "Explicit confirmation is required.");
                    close_sock(client_fd);
                    return;
                }
                if (operation != "restart" && operation != "shutdown") {
                    send_error(client_fd, 400, "Unsupported power operation.");
                    close_sock(client_fd);
                    return;
                }
                const auto power = prismix::system::SystemService::instance().get_power_status();
                if (!power.available) {
                    send_error(client_fd, 503,
                               power.reason.empty() ? "System power management is unavailable." : power.reason);
                    close_sock(client_fd);
                    return;
                }

                const auto snapshot = audio_service().snapshot();
                const bool was_real_active =
                    snapshot.mode == AudioMode::RealAudio &&
                    (snapshot.state == AudioServiceState::Streaming ||
                     snapshot.state == AudioServiceState::Initializing);
                const bool was_sim_running =
                    snapshot.mode == AudioMode::Simulation && g_backend.is_running();
                if (was_real_active) ui_stop_real_audio();
                if (was_sim_running) g_backend.stop();

                std::string error;
                const bool accepted =
                    prismix::system::SystemService::instance().request_power(operation, error);
                if (!accepted) {
                    if (was_sim_running) g_backend.start();
                    if (was_real_active) {
                        std::string restart_error;
                        ui_start_real_audio(&restart_error);
                    }
                    send_error(client_fd, 503, error);
                    close_sock(client_fd);
                    return;
                }
                send_json(client_fd, 202,
                          "{\"success\":true,\"accepted\":true,\"pending\":true,"
                          "\"operation\":\"" + json_escape(operation) + "\"}");
                close_sock(client_fd);
                return;
            }

            if (req.path == "/api/system/wifi/connect") {
                if (!require_auth()) { close_sock(client_fd); return; }
                std::string ssid;
                std::string password;
                bool found = false;
                if (!parse_json_string(req.body, "ssid", ssid, &found) || !found || ssid.empty()) {
                    send_error(client_fd, 400, "Select a Wi-Fi network.");
                    close_sock(client_fd);
                    return;
                }
                parse_json_string(req.body, "password", password);
                std::string error;
                if (!prismix::system::SystemService::instance().connect_wifi(ssid, password, error)) {
                    send_error(client_fd, 502, error);
                    close_sock(client_fd);
                    return;
                }
                send_json(client_fd, 200,
                          "{\"success\":true,\"connected\":true,\"ssid\":\"" +
                              json_escape(ssid) + "\"}");
                close_sock(client_fd);
                return;
            }

            if (req.path == "/api/command") {
                if (!require_auth()) { close_sock(client_fd); return; }
                if (locked) {
                    send_error(client_fd, 423, "The mixer is locked.",
                               "\"locked\":true");
                    close_sock(client_fd);
                    return;
                }
                auto result = WebControlBridge::instance().execute_json_command(req.body);
                std::ostringstream out;
                out << "{\"success\":" << (result.success ? "true" : "false")
                    << ",\"version\":" << result.state_version
                    << (result.error.empty() ? "" : ",\"error\":\"" + json_escape(result.error) + "\"")
                    << "}";
                send_json(client_fd, result.success ? 200 : 400, out.str());
                close_sock(client_fd);
                return;
            }
        }

        send_error(client_fd, 404, "Not found.");
        close_sock(client_fd);
    }

    void run_server_loop() {
#if defined(_WIN32)
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
        std::string bind_error;
        if (!open_listener(bind_addr, bind_error)) {
            std::cerr << "[WebServer] Failed to bind to " << bind_addr << ":"
                      << port << " (" << bind_error << ")\n";
            running.store(false);
            return;
        }

        WebControlBridge::instance().set_state_change_callback([this](uint64_t v) {
            this->broadcast_sse_event(v);
        });

        auto last_ping = std::chrono::steady_clock::now();
        while (running.load()) {
            apply_pending_bind();

            if (server_fd == INVALID_SOCK) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
#if defined(_WIN32)
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(server_fd, &read_fds);
            timeval tv{0, 200000};
            const int ready = select(0, &read_fds, nullptr, nullptr, &tv);
#else
            struct pollfd pfd{};
            pfd.fd = server_fd;
            pfd.events = POLLIN;
            const int ready = poll(&pfd, 1, 200);
#endif
            if (ready > 0) {
                sockaddr_in client_addr{};
                socklen_t client_len = sizeof(client_addr);
                const socket_t client_fd = accept(server_fd,
                    reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);
                if (client_fd != INVALID_SOCK) {
                    char ip[INET_ADDRSTRLEN] = {};
                    inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
                    handle_http_client(client_fd, ip);
                }
            }

            const auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_ping).count() >= 5) {
                last_ping = now;
                std::lock_guard<std::mutex> lock(clients_mtx);
                if (!sse_clients.empty()) {
                    const char ping_msg[] = ": ping\n\n";
                    auto it = sse_clients.begin();
                    while (it != sse_clients.end()) {
                        if (!sse_session_valid(it->session_token)) {
                            close_sock(it->fd);
                            it = sse_clients.erase(it);
                            continue;
                        }
#if defined(_WIN32)
                        const int n = send(it->fd, ping_msg, static_cast<int>(sizeof(ping_msg) - 1), 0);
#else
                        const int n = send(it->fd, ping_msg, sizeof(ping_msg) - 1, MSG_NOSIGNAL);
#endif
                        if (n <= 0) {
                            close_sock(it->fd);
                            it = sse_clients.erase(it);
                        } else {
                            ++it;
                        }
                    }
                }
            }
        }

        disconnect_sse_clients();
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
    static WebServer server;
    return server;
}

WebServer::WebServer() : impl_(std::make_unique<Impl>()) {}

WebServer::~WebServer() {
    stop();
}

bool WebServer::start(uint16_t port, const std::string& bind_addr) {
    if (impl_->running.load()) return true;
    impl_->port = port;
    impl_->bind_addr = bind_addr;
    {
        std::lock_guard<std::mutex> lock(impl_->config_mtx);
        impl_->actual_bind = bind_addr;
        impl_->pending_bind.clear();
        impl_->rebind_requested = false;
    }
    impl_->running.store(true);
    impl_->thread = std::thread([this]() { impl_->run_server_loop(); });
    return true;
}

void WebServer::stop() {
    impl_->running.store(false);
    if (impl_->thread.joinable()) impl_->thread.join();
}

bool WebServer::is_running() const {
    return impl_->running.load();
}

uint16_t WebServer::port() const {
    return impl_->port;
}

std::string WebServer::bind_address() const {
    return impl_->current_bind();
}

bool WebServer::request_bind_address(const std::string& bind_addr) {
    if (bind_addr != "127.0.0.1" && bind_addr != "0.0.0.0") return false;
    impl_->request_bind(bind_addr);
    return true;
}

} // namespace prismix::web
