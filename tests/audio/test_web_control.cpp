#include "service/web/web_control_bridge.h"
#include "service/web/web_server.h"
#include "model/mixer_state.h"
#include "model/routing_state.h"
#include "model/plugin_model.h"
#include "model/pedalboard_preset.h"
#include "backend/mock_audio_backend.h"
#include "ui/theme/theme_model.h"
#include "service/audio_service.h"
#include "service/security/security_service.h"

#include <iostream>
#include <cassert>
#include <cmath>
#include <thread>
#include <chrono>
#include <filesystem>

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
using socket_t = int;
constexpr socket_t INVALID_SOCK = -1;
inline int close_sock(socket_t s) { return close(s); }
#endif

namespace {

bool check(const char* name, bool condition) {
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << name << '\n';
    return condition;
}

std::string http_request(uint16_t port, const std::string& raw_req) {
    socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCK) return "";
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        close_sock(fd);
        return "";
    }
    send(fd, raw_req.data(), static_cast<int>(raw_req.size()), 0);
    std::string resp;
    char buf[4096];
    int r = 0;
    while ((r = recv(fd, buf, sizeof(buf), 0)) > 0) {
        resp.append(buf, static_cast<size_t>(r));
    }
    close_sock(fd);
    return resp;
}

std::string header_value(const std::string& response, const std::string& name) {
    const std::string prefix = name + ": ";
    const std::size_t start = response.find(prefix);
    if (start == std::string::npos) return {};
    const std::size_t end = response.find("\r\n", start);
    return response.substr(start + prefix.size(), end - start - prefix.size());
}

std::string session_cookie_value(const std::string& response) {
    const std::string cookie = header_value(response, "Set-Cookie");
    const std::string prefix = "prismix_session=";
    if (cookie.rfind(prefix, 0) != 0) return {};
    const std::size_t end = cookie.find(';', prefix.size());
    return cookie.substr(prefix.size(), end - prefix.size());
}

std::string http_post(uint16_t port, const std::string& path, const std::string& body,
                      const std::string& cookie = {}) {
    std::string request = "POST " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\n" +
        "Content-Type: application/json\r\nContent-Length: " +
        std::to_string(body.size()) + "\r\n";
    if (!cookie.empty()) request += "Cookie: prismix_session=" + cookie + "\r\n";
    request += "Connection: close\r\n\r\n" + body;
    return http_request(port, request);
}

std::string http_get(uint16_t port, const std::string& path,
                     const std::string& cookie = {}) {
    std::string request = "GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\n";
    if (!cookie.empty()) request += "Cookie: prismix_session=" + cookie + "\r\n";
    request += "Connection: close\r\n\r\n";
    return http_request(port, request);
}

} // anonymous namespace

int main() {
    bool ok = true;
    std::cout << "============================================================\n";
    std::cout << "TEST SUITE: PRISMIX 7-Screen Web Console & Control Bridge\n";
    std::cout << "============================================================\n";

    const auto security_stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path security_config =
        std::filesystem::temp_directory_path() /
        ("prismix-web-security-" + std::to_string(security_stamp) + ".conf");
    std::filesystem::remove(security_config);
    auto& security = prismix::security::SecurityService::instance();
    security.initialize(security_config.string());

    mixer_state_init();
    theme_set_preset(ThemePreset::Graphite);
    PluginRegistry::instance().initialize_builtins();
    PluginRegistry::instance().refresh_catalog();
    g_backend.init();

    // Ensure at least 2 channels
    while (g_mixer_state.channels.size() < 2) {
        ChannelState ch{};
        ch.id = static_cast<int>(g_mixer_state.channels.size()) + 1;
        std::snprintf(ch.name, sizeof(ch.name), "CH %02d", ch.id);
        g_mixer_state.channels.push_back(ch);
    }

    auto& bridge = prismix::web::WebControlBridge::instance();
    const uint64_t v0 = bridge.get_state_version();
    ok &= check("Initial state version is valid", v0 >= 1);

    // Test 1: Snapshot JSON serialization (all 7 view domains)
    std::string state_json = bridge.get_state_json(true);
    ok &= check("State JSON contains channels array", state_json.find("\"channels\": [") != std::string::npos);
    ok &= check("State JSON contains aux_buses", state_json.find("\"aux_buses\": [") != std::string::npos);
    ok &= check("State JSON contains master", state_json.find("\"master\": {") != std::string::npos);
    ok &= check("State JSON contains routing matrix", state_json.find("\"routing\": {") != std::string::npos);
    ok &= check("State JSON contains system state", state_json.find("\"system\": {") != std::string::npos);
    ok &= check("State JSON contains theme settings", state_json.find("\"theme\": {") != std::string::npos);
    ok &= check("State JSON contains capabilities", state_json.find("\"capabilities\": {") != std::string::npos);
    ok &= check("State JSON contains version", state_json.find("\"version\":") != std::string::npos);

    // Test 2: High-Frequency Telemetry & RTA Spectrum JSON
    for (int i = 0; i < 8; ++i) {
        g_backend.update();
    }
    std::string telem_json = bridge.get_telemetry_json();
    ok &= check("Telemetry JSON contains channels", telem_json.find("\"channels\": [") != std::string::npos);
    ok &= check("Telemetry JSON contains rta object", telem_json.find("\"rta\": {") != std::string::npos);
    ok &= check("Telemetry JSON contains 31-band RTA", telem_json.find("\"bands_31\": [") != std::string::npos);
    ok &= check("Telemetry JSON contains 63-band RTA", telem_json.find("\"bands_63\": [") != std::string::npos);
    ok &= check("Telemetry JSON contains FFT bins", telem_json.find("\"fft_bins\": [") != std::string::npos);

    // Test 3: Catalog JSON serialization
    std::string cat_json = bridge.get_catalog_json();
    ok &= check("Catalog JSON contains plugins list", cat_json.find("\"plugins\": [") != std::string::npos);
    ok &= check("Catalog JSON contains presets list", cat_json.find("\"presets\": [") != std::string::npos);
    ok &= check("Catalog JSON contains GEQ ISO frequencies", cat_json.find("\"geq_frequencies\": [") != std::string::npos);
    ok &= check("Catalog JSON contains PatchStorage info", cat_json.find("\"patchstorage\": {") != std::string::npos);

    // Test 4: Command execution - Fader & Clamping
    prismix::web::WebCommand cmd_fader;
    cmd_fader.type = prismix::web::WebCommandType::SetChannelFader;
    cmd_fader.channel_id = 1;
    cmd_fader.param_value = 0.75f;
    auto res_fader = bridge.execute_command(cmd_fader);
    ok &= check("Set fader command accepted", res_fader.success);
    ok &= check("Channel state fader updated", std::abs(g_mixer_state.channels[0].fader - 0.75f) < 1e-4f);
    ok &= check("State version incremented on change", res_fader.state_version > v0);

    cmd_fader.param_value = 1.5f;
    bridge.execute_command(cmd_fader);
    ok &= check("Fader value clamped to 1.0 on overflow", g_mixer_state.channels[0].fader == 1.0f);

    cmd_fader.param_value = -0.5f;
    bridge.execute_command(cmd_fader);
    ok &= check("Fader value clamped to 0.0 on underflow", g_mixer_state.channels[0].fader == 0.0f);

    // Test 5: Channel Preamp, HPF, Aux Send, Speaker Delay
    ok &= check("Set input gain via JSON", bridge.execute_json_command("{\"action\":\"set_gain\",\"channel\":1,\"value\":0.8}").success);
    ok &= check("Input gain updated to 0.80", std::abs(g_mixer_state.channels[0].gain - 0.8f) < 1e-4f);
    ok &= check("Set 48V phantom via JSON", bridge.execute_json_command("{\"action\":\"set_phantom\",\"channel\":1,\"value\":true}").success);
    ok &= check("48V phantom enabled on CH 1", g_mixer_state.channels[0].phantom_on == true);
    ok &= check("Set HPF via JSON", bridge.execute_json_command("{\"action\":\"set_hpf_enabled\",\"channel\":1,\"value\":true}").success);
    ok &= check("Set HPF freq via JSON", bridge.execute_json_command("{\"action\":\"set_hpf_freq\",\"channel\":1,\"value\":110.0}").success);
    ok &= check("HPF freq updated to 110 Hz", std::abs(g_mixer_state.channels[0].hpf_frequency - 110.0f) < 1e-3f);
    ok &= check("Set AUX 2 send via JSON", bridge.execute_json_command("{\"action\":\"set_aux_send\",\"channel\":1,\"aux\":1,\"value\":0.65}").success);
    ok &= check("AUX 2 send updated", std::abs(g_mixer_state.channels[0].sends[1] - 0.65f) < 1e-4f);
    ok &= check("Set Master speaker delay via JSON", bridge.execute_json_command("{\"action\":\"set_speaker_delay\",\"channel\":0,\"enabled\":true,\"delay_l\":14.5,\"delay_r\":14.5}").success);
    ok &= check("Master speaker delay updated", g_mixer_state.master.speaker_delay_enabled && std::abs(g_mixer_state.master.speaker_delay_l_ms - 14.5f) < 1e-3f);

    // Test 6: PEQ, GEQ, and RTA Config commands
    ok &= check("Set PEQ band 1 via JSON", bridge.execute_json_command("{\"action\":\"set_peq_band\",\"channel\":1,\"band\":1,\"freq\":2500.0,\"gain\":4.5,\"q\":1.8,\"filter_type\":1,\"enabled\":true}").success);
    ok &= check("PEQ band 1 frequency and gain updated",
                std::abs(g_mixer_state.channels[0].eq_freq[1] - 2500.0f) < 1.0f &&
                std::abs(g_mixer_state.channels[0].eq_gain[1] - 4.5f) < 1e-3f);
    ok &= check("Set GEQ mode via JSON", bridge.execute_json_command("{\"action\":\"set_geq_mode\",\"channel\":1,\"value\":true}").success);
    ok &= check("GEQ mode enabled on CH 1", g_mixer_state.channels[0].geq_mode_active == true);
    ok &= check("Set GEQ band 15 via JSON", bridge.execute_json_command("{\"action\":\"set_geq_band\",\"channel\":1,\"band\":15,\"value\":-3.5}").success);
    ok &= check("GEQ band 15 gain updated", std::abs(g_mixer_state.channels[0].geq.get_gains()[15] - (-3.5f)) < 1e-3f);
    ok &= check("Set RTA config via JSON", bridge.execute_json_command("{\"action\":\"set_rta_config\",\"mode\":2,\"source\":0,\"freeze\":true}").success);

    // Test 7: Dynamics (Gate, Compressor, Limiter) & Master-Bus Restrictions
    ok &= check("Set Gate threshold on CH 1", bridge.execute_json_command("{\"action\":\"set_gate_param\",\"channel\":1,\"param_id\":\"thresh\",\"value\":-38.0}").success);
    ok &= check("CH 1 Gate threshold updated", std::abs(g_mixer_state.channels[0].gate_thresh - (-38.0f)) < 1e-3f);
    ok &= check("Set Compressor ratio on CH 1", bridge.execute_json_command("{\"action\":\"set_comp_param\",\"channel\":1,\"param_id\":\"ratio\",\"value\":6.0}").success);
    ok &= check("CH 1 Compressor ratio updated", std::abs(g_mixer_state.channels[0].comp_ratio - 6.0f) < 1e-3f);
    ok &= check("Set Master Limiter ceiling", bridge.execute_json_command("{\"action\":\"set_limiter_param\",\"channel\":0,\"param_id\":\"ceiling\",\"value\":-1.5}").success);
    ok &= check("Master Limiter ceiling updated", std::abs(g_mixer_state.master.limiter_ceiling_db - (-1.5f)) < 1e-3f);

    // Verify Master-Bus Restrictions (Gate and FX Rack disabled on Master)
    auto res_master_gate = bridge.execute_json_command("{\"action\":\"set_gate_enabled\",\"channel\":0,\"value\":true}");
    ok &= check("Master bus rejects Gate enable (capability restriction)", !res_master_gate.success);
    auto res_master_fx = bridge.execute_json_command("{\"action\":\"add_plugin\",\"channel\":0,\"uri\":\"http://audiocore.org/plugins/compressor\"}");
    ok &= check("Master bus rejects FX Rack plugin insertion (capability restriction)", !res_master_fx.success);

    // Test 8: FX Rack - Add, Parameter, Move, Remove
    auto* ch = mixer_find_channel_by_id(1);
    ch->fx_rack.clear();
    prismix::web::WebCommand cmd_add;
    cmd_add.type = prismix::web::WebCommandType::AddPlugin;
    cmd_add.channel_id = 1;
    cmd_add.uri = "http://audiocore.org/plugins/compressor";
    auto res_add = bridge.execute_command(cmd_add);
    ok &= check("Add plugin command accepted", res_add.success);
    ok &= check("Plugin added to rack slot 0", ch->fx_rack.count() == 1);

    prismix::web::WebCommand cmd_param;
    cmd_param.type = prismix::web::WebCommandType::SetPluginParameter;
    cmd_param.channel_id = 1;
    cmd_param.slot_index = 0;
    cmd_param.param_id = "thresh";
    cmd_param.param_value = -24.0f;
    auto res_param = bridge.execute_command(cmd_param);
    ok &= check("Set plugin parameter command accepted", res_param.success);
    if (auto* inst = ch->fx_rack.get_slot(0)) {
        ok &= check("Plugin parameter value updated in rack",
                    std::abs(inst->get_parameter_value("thresh") - (-24.0f)) < 1e-4f);
    }

    prismix::web::WebCommand cmd_add_limiter;
    cmd_add_limiter.type = prismix::web::WebCommandType::AddPlugin;
    cmd_add_limiter.channel_id = 1;
    cmd_add_limiter.uri = "http://audiocore.org/plugins/limiter";
    auto res_add_limiter = bridge.execute_command(cmd_add_limiter);
    ok &= check("Add FX limiter for GR snapshot serialization", res_add_limiter.success);
    const float savedFxLimiterGr = ch->fx_slot_limiter_gr_db[1];
    ch->fx_slot_limiter_gr_db[1] = -2.25f;
    const std::string fx_gr_state_json = bridge.get_state_json(true);
    ok &= check("FX rack JSON exposes the real limiter GR snapshot field",
                fx_gr_state_json.find("\"limiter_gr_db\": -2.2500") != std::string::npos);
    ch->fx_slot_limiter_gr_db[1] = savedFxLimiterGr;

    // Test 9: Unsafe preset path rejection
    prismix::web::WebCommand cmd_unsafe_preset;
    cmd_unsafe_preset.type = prismix::web::WebCommandType::SavePreset;
    cmd_unsafe_preset.channel_id = 1;
    cmd_unsafe_preset.preset_name = "../../etc/passwd";
    auto res_unsafe = bridge.execute_command(cmd_unsafe_preset);
    ok &= check("Path traversal in preset name safely rejected", !res_unsafe.success);

    // Test 10: Routing Matrix commands
    ok &= check("Set channel-to-bus crosspoint via JSON",
                bridge.execute_json_command("{\"action\":\"set_channel_bus\",\"channel\":1,\"bus\":2,\"value\":true}").success);
    ok &= check("Routing Matrix crosspoint updated",
                g_routing_matrix.get_channel_bus(0, 2) == true);
    ok &= check("Set output patchbay port via JSON",
                bridge.execute_json_command("{\"action\":\"set_output_patch\",\"output_port\":3,\"source_id\":2}").success);
    ok &= check("Output patchbay port 3 updated",
                g_routing_matrix.get_output_patch(3) == 2);

    // Test 11: Channel Lifecycle & Theme commands
    const size_t ch_before = g_mixer_state.channels.size();
    ok &= check("Add stereo channel via JSON", bridge.execute_json_command("{\"action\":\"add_channel\",\"name\":\"SYNTH L\",\"format\":1}").success);
    ok &= check("Channel count increased", g_mixer_state.channels.size() == ch_before + 1);
    int new_id = g_mixer_state.channels.back().id;
    ok &= check("Added channel uses requested name and stereo format",
                std::string(g_mixer_state.channels.back().name) == "SYNTH L" &&
                g_mixer_state.channels.back().format == ChannelFormat::Stereo);
    ok &= check("Rename channel via JSON",
                bridge.execute_json_command("{\"action\":\"rename_channel\",\"channel\":" + std::to_string(new_id) + ",\"name\":\"LEAD VOX\"}").success);
    ok &= check("Rename preserves the target channel ID and applies its name",
                mixer_find_channel_by_id(new_id) && std::string(mixer_find_channel_by_id(new_id)->name) == "LEAD VOX");
    ok &= check("Unsafe channel name rejected",
                !bridge.execute_json_command("{\"action\":\"rename_channel\",\"channel\":" + std::to_string(new_id) + ",\"name\":\"BAD..NAME\"}").success);
    ok &= check("Delete channel via JSON",
                bridge.execute_json_command("{\"action\":\"delete_channel\",\"channel\":" + std::to_string(new_id) + "}").success);
    ok &= check("Deleting selected channel chooses a remaining valid input",
                g_mixer_state.selected_channel >= 0 &&
                g_mixer_state.channels[static_cast<size_t>(g_mixer_state.selected_channel)].id == 2);
    ok &= check("Master channel delete rejected",
                !bridge.execute_json_command("{\"action\":\"delete_channel\",\"channel\":0}").success);
    ok &= check("AUX master delete rejected",
                !bridge.execute_json_command("{\"action\":\"delete_channel\",\"channel\":101}").success);

    bridge.execute_json_command("{\"action\":\"add_channel\",\"name\":\"THREE\"}");
    bridge.execute_json_command("{\"action\":\"add_channel\",\"name\":\"FOUR\"}");
    bridge.execute_json_command("{\"action\":\"add_channel\",\"name\":\"FIVE\"}");
    ok &= check("Select channel before deleting a preceding strip",
                bridge.execute_json_command("{\"action\":\"select_channel\",\"channel\":4}").success);
    ok &= check("Delete preceding strip", bridge.execute_json_command("{\"action\":\"delete_channel\",\"channel\":3}").success);
    ok &= check("Deletion preserves the selected channel by ID",
                g_mixer_state.selected_channel >= 0 &&
                g_mixer_state.channels[static_cast<size_t>(g_mixer_state.selected_channel)].id == 4);
    ok &= check("Deleting selected strip selects the next remaining input",
                bridge.execute_json_command("{\"action\":\"delete_channel\",\"channel\":4}").success &&
                g_mixer_state.channels[static_cast<size_t>(g_mixer_state.selected_channel)].id == 5);
    bridge.execute_json_command("{\"action\":\"delete_channel\",\"channel\":5}");

    ok &= check("Set theme preset via JSON", bridge.execute_json_command("{\"action\":\"set_theme_preset\",\"preset\":2}").success);
    ok &= check("Theme preset updated to StudioBlue",
                theme_get_config().active_preset == ThemePreset::StudioBlue);

    // Test 12: WebServer HTTP routes (/, /api/state, /api/telemetry, /api/catalog, /api/command)
    auto& server = prismix::web::WebServer::instance();
    const uint16_t test_port = 18889;
    server.start(test_port, "127.0.0.1");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ok &= check("WebServer started", server.is_running());

    std::string html_resp = http_request(test_port, "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    ok &= check("GET / returns 200 OK with 7-screen PRISMIX Web UI",
                html_resp.find("HTTP/1.1 200 OK") != std::string::npos &&
                html_resp.find("PRISMIX") != std::string::npos &&
                html_resp.find("DYNAMICS") != std::string::npos &&
                html_resp.find("ROUTING") != std::string::npos);

    std::string state_resp = http_request(test_port, "GET /api/state HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    ok &= check("GET /api/state returns 200 OK", state_resp.find("HTTP/1.1 200 OK") != std::string::npos);

    std::string telem_resp = http_request(test_port, "GET /api/telemetry HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
    ok &= check("GET /api/telemetry returns 200 OK with RTA data",
                telem_resp.find("HTTP/1.1 200 OK") != std::string::npos &&
                telem_resp.find("\"rta\":") != std::string::npos);

    std::string cmd_body = "{\"action\":\"set_pan\",\"channel\":1,\"value\":0.25}";
    std::string post_req = "POST /api/command HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\nContent-Length: " +
                           std::to_string(cmd_body.size()) + "\r\nConnection: close\r\n\r\n" + cmd_body;
    std::string cmd_resp = http_request(test_port, post_req);
    ok &= check("POST /api/command returns 200 OK and updates pan",
                cmd_resp.find("\"success\":true") != std::string::npos &&
                std::abs(g_mixer_state.channels[0].pan - 0.25f) < 1e-4f);

    ok &= check("Protected API responses carry strict security headers",
                state_resp.find("X-Content-Type-Options: nosniff") != std::string::npos &&
                state_resp.find("X-Frame-Options: DENY") != std::string::npos &&
                state_resp.find("Content-Security-Policy:") != std::string::npos &&
                state_resp.find("Cache-Control: no-store") != std::string::npos);

    // =============================================================
    // Authenticated Web access, lock enforcement and session lifecycle
    // =============================================================
    std::string setup_body = "{\"pin\":\"246810\",\"confirm_pin\":\"246810\"}";
    std::string setup_resp = http_post(test_port, "/api/auth/setup", setup_body);
    const std::string session_cookie = session_cookie_value(setup_resp);
    ok &= check("Local PIN setup succeeds and returns an authenticated session",
                setup_resp.find("HTTP/1.1 200 OK") != std::string::npos &&
                setup_resp.find("\"configured\":true") != std::string::npos &&
                !session_cookie.empty());
    ok &= check("Session cookie is HttpOnly, SameSite and path-scoped",
                setup_resp.find("HttpOnly") != std::string::npos &&
                setup_resp.find("SameSite=Strict") != std::string::npos &&
                setup_resp.find("Path=/") != std::string::npos);
    ok &= check("Session cookie has a finite lifetime",
                setup_resp.find("Max-Age=") != std::string::npos);
    ok &= check("Setup response never includes the plaintext PIN",
                setup_resp.find("246810") == std::string::npos);

    std::string unauth_state = http_get(test_port, "/api/state");
    ok &= check("Protected state API is blocked without a session",
                unauth_state.find("HTTP/1.1 401 Unauthorized") != std::string::npos);
    std::string auth_state = http_get(test_port, "/api/state", session_cookie);
    ok &= check("Protected state API accepts a valid session",
                auth_state.find("HTTP/1.1 200 OK") != std::string::npos);

    std::string unauth_system = http_get(test_port, "/api/system/status");
    ok &= check("System status is protected by the shared session",
                unauth_system.find("HTTP/1.1 401 Unauthorized") != std::string::npos);
    std::string auth_system = http_get(test_port, "/api/system/status", session_cookie);
    ok &= check("Authenticated system status reports honest runtime fields",
                auth_system.find("HTTP/1.1 200 OK") != std::string::npos &&
                auth_system.find("\"web_access\":") != std::string::npos &&
                auth_system.find("\"transport_secure\":false") != std::string::npos);

    std::string unauth_sse = http_get(test_port, "/api/events");
    ok &= check("Realtime SSE subscription is rejected without a session",
                unauth_sse.find("HTTP/1.1 401 Unauthorized") != std::string::npos);

    std::string unauth_lan = http_post(test_port, "/api/security/lan", "{\"enabled\":true}");
    ok &= check("LAN policy cannot be changed without a session",
                unauth_lan.find("HTTP/1.1 401 Unauthorized") != std::string::npos);

    std::string unsafe_power = http_post(test_port, "/api/system/power",
        "{\"operation\":\"shell\",\"confirm\":true}", session_cookie);
    ok &= check("Unknown power operations are rejected before execution",
                unsafe_power.find("HTTP/1.1 400 Bad Request") != std::string::npos);
    std::string unconfirmed_power = http_post(test_port, "/api/system/power",
        "{\"operation\":\"restart\",\"confirm\":false}", session_cookie);
    ok &= check("Power actions require explicit confirmation",
                unconfirmed_power.find("HTTP/1.1 400 Bad Request") != std::string::npos);

    std::string lock_resp = http_post(test_port, "/api/security/lock", "{}", session_cookie);
    ok &= check("Authenticated mixer lock succeeds",
                lock_resp.find("HTTP/1.1 200 OK") != std::string::npos &&
                security.isMixerLocked());
    ok &= check("Lock expires the browser session cookie",
                lock_resp.find("Max-Age=0") != std::string::npos);
    std::string locked_state = http_get(test_port, "/api/state", session_cookie);
    ok &= check("Locked session can no longer read mixer state",
                locked_state.find("HTTP/1.1 401 Unauthorized") != std::string::npos);

    prismix::web::WebCommand locked_cmd;
    locked_cmd.type = prismix::web::WebCommandType::SetChannelFader;
    locked_cmd.channel_id = 1;
    locked_cmd.param_value = 0.91f;
    const auto locked_result = bridge.execute_command(locked_cmd);
    ok &= check("Control layer rejects direct commands while the mixer is locked",
                !locked_result.success &&
                locked_result.error.find("locked") != std::string::npos);

    std::string unlock_resp = http_post(test_port, "/api/auth/unlock", "{\"pin\":\"246810\"}");
    const std::string unlocked_cookie = session_cookie_value(unlock_resp);
    ok &= check("Correct PIN unlocks the mixer and issues a fresh session",
                unlock_resp.find("HTTP/1.1 200 OK") != std::string::npos &&
                !unlocked_cookie.empty() && !security.isMixerLocked());
    std::string unlocked_state = http_get(test_port, "/api/state", unlocked_cookie);
    ok &= check("Fresh unlocked session reads mixer state",
                unlocked_state.find("HTTP/1.1 200 OK") != std::string::npos);

    std::string wrong_login;
    for (int attempt = 0; attempt < 5; ++attempt) {
        wrong_login = http_post(test_port, "/api/auth/login", "{\"pin\":\"000000\"}");
    }
    ok &= check("Repeated wrong logins are rate limited",
                wrong_login.find("HTTP/1.1 429 Too Many Requests") != std::string::npos ||
                wrong_login.find("HTTP/1.1 429") != std::string::npos);
    security.resetRateLimitsForTesting();

    std::string pin_change_resp = http_post(test_port, "/api/security/pin",
        "{\"current_pin\":\"246810\",\"new_pin\":\"135790\",\"confirm_pin\":\"135790\"}",
        unlocked_cookie);
    const std::string final_cookie = session_cookie_value(pin_change_resp);
    ok &= check("PIN change requires an authenticated session and reissues one",
                pin_change_resp.find("HTTP/1.1 200 OK") != std::string::npos &&
                !final_cookie.empty());
    ok &= check("PIN change invalidates the previous session",
                http_get(test_port, "/api/state", unlocked_cookie).find("HTTP/1.1 401") !=
                    std::string::npos);
    ok &= check("New session can read state after PIN change",
                http_get(test_port, "/api/state", final_cookie).find("HTTP/1.1 200 OK") !=
                    std::string::npos);
    ok &= check("PIN change response never includes either plaintext PIN",
                pin_change_resp.find("246810") == std::string::npos &&
                pin_change_resp.find("135790") == std::string::npos);

    std::string logout_resp = http_post(test_port, "/api/auth/logout", "{}", final_cookie);
    ok &= check("Logout revokes the active session",
                logout_resp.find("HTTP/1.1 200 OK") != std::string::npos &&
                http_get(test_port, "/api/state", final_cookie).find("HTTP/1.1 401") !=
                    std::string::npos);

    server.stop();
    ok &= check("WebServer stopped cleanly", !server.is_running());

    std::filesystem::remove(security_config);
    std::filesystem::remove(security_config.string() + ".tmp");

    std::cout << "============================================================\n";
    std::cout << (ok ? "ALL WEB CONTROL BRIDGE TESTS PASSED!\n" : "SOME TESTS FAILED!\n");
    std::cout << "============================================================\n";

    return ok ? 0 : 1;
}
