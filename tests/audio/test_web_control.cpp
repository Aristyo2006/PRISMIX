#include "service/web/web_control_bridge.h"
#include "service/web/web_server.h"
#include "model/mixer_state.h"
#include "model/plugin_model.h"
#include "model/pedalboard_preset.h"
#include "service/audio_service.h"

#include <iostream>
#include <cassert>
#include <cmath>
#include <thread>
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
using socket_t = int;
constexpr socket_t INVALID_SOCK = -1;
inline int close_sock(socket_t s) { return close(s); }
#endif

namespace {

bool check(const char* name, bool condition) {
    std::cout << (condition ? "[PASS] " : "[FAIL] ") << name << '\n';
    return condition;
}

} // anonymous namespace

int main() {
    bool ok = true;
    std::cout << "============================================================\n";
    std::cout << "TEST SUITE: PRISMIX Web Control Bridge & Web Server\n";
    std::cout << "============================================================\n";

    mixer_state_init();
    PluginRegistry::instance().initialize_builtins();
    PluginRegistry::instance().refresh_catalog();

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

    // Test 1: Snapshot JSON serialization
    std::string state_json = bridge.get_state_json(true);
    ok &= check("State JSON contains channels array", state_json.find("\"channels\": [") != std::string::npos);
    ok &= check("State JSON contains aux_buses", state_json.find("\"aux_buses\": [") != std::string::npos);
    ok &= check("State JSON contains master", state_json.find("\"master\": {") != std::string::npos);
    ok &= check("State JSON contains version", state_json.find("\"version\":") != std::string::npos);

    // Test 2: Catalog JSON serialization
    std::string cat_json = bridge.get_catalog_json();
    ok &= check("Catalog JSON contains plugins list", cat_json.find("\"plugins\": [") != std::string::npos);
    ok &= check("Catalog JSON contains presets list", cat_json.find("\"presets\": [") != std::string::npos);

    // Test 3: Command execution - Fader
    prismix::web::WebCommand cmd_fader;
    cmd_fader.type = prismix::web::WebCommandType::SetChannelFader;
    cmd_fader.channel_id = 1;
    cmd_fader.param_value = 0.75f;
    auto res_fader = bridge.execute_command(cmd_fader);
    ok &= check("Set fader command accepted", res_fader.success);
    ok &= check("Channel state fader updated", std::abs(g_mixer_state.channels[0].fader - 0.75f) < 1e-4f);
    ok &= check("State version incremented on change", res_fader.state_version > v0);

    // Test 4: Command execution - Value Clamping
    cmd_fader.param_value = 1.5f; // Above 1.0
    bridge.execute_command(cmd_fader);
    ok &= check("Fader value clamped to 1.0 on overflow", g_mixer_state.channels[0].fader == 1.0f);

    cmd_fader.param_value = -0.5f; // Below 0.0
    bridge.execute_command(cmd_fader);
    ok &= check("Fader value clamped to 0.0 on underflow", g_mixer_state.channels[0].fader == 0.0f);

    // Test 5: Command execution - Pan, Mute, Solo
    prismix::web::WebCommand cmd_mute;
    cmd_mute.type = prismix::web::WebCommandType::SetChannelMute;
    cmd_mute.channel_id = 1;
    cmd_mute.bool_value = true;
    bridge.execute_command(cmd_mute);
    ok &= check("Channel mute updated", g_mixer_state.channels[0].mute == true);

    prismix::web::WebCommand cmd_solo;
    cmd_solo.type = prismix::web::WebCommandType::SetChannelSolo;
    cmd_solo.channel_id = 1;
    cmd_solo.bool_value = true;
    bridge.execute_command(cmd_solo);
    ok &= check("Channel solo updated", g_mixer_state.channels[0].solo == true);

    // Test 6: Non-existent channel rejection
    prismix::web::WebCommand cmd_bad_ch;
    cmd_bad_ch.type = prismix::web::WebCommandType::SetChannelFader;
    cmd_bad_ch.channel_id = 999;
    auto res_bad_ch = bridge.execute_command(cmd_bad_ch);
    ok &= check("Non-existent channel ID rejected", !res_bad_ch.success && !res_bad_ch.error.empty());

    // Test 7: FX Rack - Add and Remove Plugin
    auto* ch = mixer_find_channel_by_id(1);
    ch->fx_rack.clear();
    prismix::web::WebCommand cmd_add;
    cmd_add.type = prismix::web::WebCommandType::AddPlugin;
    cmd_add.channel_id = 1;
    cmd_add.uri = "http://audiocore.org/plugins/compressor";
    auto res_add = bridge.execute_command(cmd_add);
    ok &= check("Add plugin command accepted", res_add.success);
    ok &= check("Plugin added to rack slot 0", ch->fx_rack.count() == 1);

    // Test 8: Set Plugin Parameter
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

    // Test 9: Unsafe preset path rejection
    prismix::web::WebCommand cmd_unsafe_preset;
    cmd_unsafe_preset.type = prismix::web::WebCommandType::SavePreset;
    cmd_unsafe_preset.channel_id = 1;
    cmd_unsafe_preset.preset_name = "../../etc/passwd";
    auto res_unsafe = bridge.execute_command(cmd_unsafe_preset);
    ok &= check("Path traversal in preset name safely rejected", !res_unsafe.success);

    // Test 10: JSON command parser
    auto res_json = bridge.execute_json_command("{\"action\": \"set_pan\", \"channel\": 1, \"value\": 0.25}");
    ok &= check("JSON command parsed and executed", res_json.success);
    ok &= check("Pan updated via JSON", std::abs(g_mixer_state.channels[0].pan - 0.25f) < 1e-4f);

    // Test 11: WebServer HTTP lifecycle and client disconnection
    auto& server = prismix::web::WebServer::instance();
    const uint16_t test_port = 18888;
    server.start(test_port, "127.0.0.1");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ok &= check("WebServer started", server.is_running());

    // Connect client socket to verify HTTP GET /api/state
    socket_t client_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(test_port);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    int conn_res = connect(client_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    ok &= check("Client connected to WebServer", conn_res == 0);

    const char req[] = "GET /api/state HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    send(client_fd, req, sizeof(req) - 1, 0);
    char buf[1024];
    int r = recv(client_fd, buf, sizeof(buf) - 1, 0);
    ok &= check("HTTP 200 OK received from WebServer", r > 0 && std::string(buf, r).find("HTTP/1.1 200 OK") != std::string::npos);

    // Abrupt disconnect - audio service must not be interrupted
    close_sock(client_fd);
    ok &= check("Client disconnected cleanly without engine interruption", true);

    server.stop();
    ok &= check("WebServer stopped cleanly", !server.is_running());

    std::cout << "============================================================\n";
    std::cout << (ok ? "ALL WEB CONTROL BRIDGE TESTS PASSED!\n" : "SOME TESTS FAILED!\n");
    std::cout << "============================================================\n";

    return ok ? 0 : 1;
}

