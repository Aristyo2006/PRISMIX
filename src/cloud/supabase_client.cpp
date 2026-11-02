#include "supabase_client.h"
#include "cloud_config.h"
#include "json_helper.hpp"

#include <thread>
#include <sstream>
#include <iostream>
#include <algorithm>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#endif

namespace Prismix {
namespace Cloud {

namespace {

std::wstring to_wstring(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), NULL, 0);
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.length()), &wstr[0], size_needed);
    return wstr;
}

struct HttpResponse {
    int status_code = 0;
    std::string body;
    std::string error;
};

HttpResponse perform_http_request(const std::string& verb, const std::string& path_and_query, const std::string& post_body = "") {
    HttpResponse response;

#if defined(_WIN32)
    HINTERNET hSession = WinHttpOpen(
        L"PRISMIX-Cloud/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );

    if (!hSession) {
        response.error = "Failed to initialize WinHTTP session (Error: " + std::to_string(GetLastError()) + ")";
        return response;
    }

    // Set reasonable timeouts (Resolve: 5s, Connect: 8s, Send: 10s, Receive: 15s)
    WinHttpSetTimeouts(hSession, 5000, 8000, 10000, 15000);

    std::wstring wHost = to_wstring(SUPABASE_HOST);
    HINTERNET hConnect = WinHttpConnect(hSession, wHost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        response.error = "Failed to connect to Supabase host (Error: " + std::to_string(GetLastError()) + ")";
        WinHttpCloseHandle(hSession);
        return response;
    }

    std::wstring wVerb = to_wstring(verb);
    std::wstring wPath = to_wstring(path_and_query);

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        wVerb.c_str(),
        wPath.c_str(),
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE // Required for HTTPS
    );

    if (!hRequest) {
        response.error = "Failed to create WinHTTP request (Error: " + std::to_string(GetLastError()) + ")";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return response;
    }

    // Build headers
    std::wstring wKey = to_wstring(SUPABASE_KEY);
    std::wstring headers = L"apikey: " + wKey + L"\r\nAuthorization: Bearer " + wKey + L"\r\nAccept: application/json\r\n";
    if (verb == "POST" || verb == "PUT" || verb == "PATCH") {
        headers += L"Content-Type: application/json\r\nPrefer: return=representation\r\n";
    }

    BOOL bResults = WinHttpSendRequest(
        hRequest,
        headers.c_str(),
        static_cast<DWORD>(headers.length()),
        post_body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)post_body.c_str(),
        static_cast<DWORD>(post_body.length()),
        static_cast<DWORD>(post_body.length()),
        0
    );

    if (bResults) {
        bResults = WinHttpReceiveResponse(hRequest, NULL);
    }

    if (bResults) {
        DWORD dwStatusCode = 0;
        DWORD dwSize = sizeof(dwStatusCode);
        WinHttpQueryHeaders(
            hRequest,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &dwStatusCode,
            &dwSize,
            WINHTTP_NO_HEADER_INDEX
        );
        response.status_code = static_cast<int>(dwStatusCode);

        // Read response body
        DWORD dwDownloaded = 0;
        std::vector<char> buffer;
        do {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize)) break;
            if (dwSize == 0) break;

            buffer.resize(dwSize + 1);
            if (WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) {
                response.body.append(buffer.data(), dwDownloaded);
            }
        } while (dwSize > 0);
    } else {
        response.error = "WinHTTP request failed or timed out (Error: " + std::to_string(GetLastError()) + ")";
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
#else
    response.error = "WinHTTP is only supported on Windows.";
#endif

    return response;
}

} // namespace

void SupabaseClient::list_presets_async(ListCallback callback) {
    std::thread([callback = std::move(callback)]() {
        std::string path = std::string(REST_API_PATH) + "?select=id,name,description,version,created_at&order=created_at.desc";
        HttpResponse resp = perform_http_request("GET", path);

        if (resp.status_code >= 200 && resp.status_code < 300) {
            std::vector<PresetInfo> presets;
            Prismix::Json::Value root = Prismix::Json::parse(resp.body);
            if (root.is_array()) {
                for (size_t i = 0; i < root.size(); ++i) {
                    const auto& item = root[i];
                    PresetInfo p;
                    p.id = item.get_string("id");
                    p.name = item.get_string("name", "Untitled");
                    p.description = item.get_string("description");
                    p.version = item.get_int("version", 1);
                    p.created_at = item.get_string("created_at");
                    presets.push_back(std::move(p));
                }
            }
            if (callback) callback(true, presets, "");
        } else {
            std::string err = resp.error.empty() ? ("HTTP " + std::to_string(resp.status_code) + ": " + resp.body) : resp.error;
            if (callback) callback(false, {}, err);
        }
    }).detach();
}

void SupabaseClient::get_preset_config_async(const std::string& preset_id, GetConfigCallback callback) {
    std::thread([preset_id, callback = std::move(callback)]() {
        std::string path = std::string(REST_API_PATH) + "?id=eq." + preset_id + "&select=config";
        HttpResponse resp = perform_http_request("GET", path);

        if (resp.status_code >= 200 && resp.status_code < 300) {
            Prismix::Json::Value root = Prismix::Json::parse(resp.body);
            if (root.is_array() && root.size() > 0) {
                const auto& item = root[0];
                const auto& cfg = item.get("config");
                std::string cfg_str = cfg.dump();
                if (callback) callback(true, cfg_str, "");
                return;
            }
            if (callback) callback(false, "", "Preset not found in database.");
        } else {
            std::string err = resp.error.empty() ? ("HTTP " + std::to_string(resp.status_code) + ": " + resp.body) : resp.error;
            if (callback) callback(false, "", err);
        }
    }).detach();
}

void SupabaseClient::save_preset_async(const std::string& name, const std::string& description, const std::string& config_json, ActionCallback callback) {
    std::thread([name, description, config_json, callback = std::move(callback)]() {
        Prismix::Json::Value body = Prismix::Json::Value::make_object();
        body.set("name", name);
        body.set("description", description);
        body.set("version", CURRENT_PRESET_SCHEMA_VERSION);
        body.set("config", Prismix::Json::parse(config_json));

        std::string payload = body.dump();
        HttpResponse resp = perform_http_request("POST", REST_API_PATH, payload);

        if (resp.status_code == 200 || resp.status_code == 201 || resp.status_code == 204) {
            if (callback) callback(true, "");
        } else {
            std::string err = resp.error.empty() ? ("HTTP " + std::to_string(resp.status_code) + ": " + resp.body) : resp.error;
            if (callback) callback(false, err);
        }
    }).detach();
}

void SupabaseClient::update_preset_async(const std::string& preset_id, const std::string& name, const std::string& config_json, ActionCallback callback) {
    std::thread([preset_id, name, config_json, callback = std::move(callback)]() {
        std::string path = std::string(REST_API_PATH) + "?id=eq." + preset_id;
        Prismix::Json::Value body = Prismix::Json::Value::make_object();
        if (!name.empty()) {
            body.set("name", name);
        }
        body.set("version", CURRENT_PRESET_SCHEMA_VERSION);
        body.set("config", Prismix::Json::parse(config_json));

        std::string payload = body.dump();
        HttpResponse resp = perform_http_request("PATCH", path, payload);

        if (resp.status_code >= 200 && resp.status_code < 300) {
            if (callback) callback(true, "");
        } else {
            std::string err = resp.error.empty() ? ("HTTP " + std::to_string(resp.status_code) + ": " + resp.body) : resp.error;
            if (callback) callback(false, err);
        }
    }).detach();
}

void SupabaseClient::delete_preset_async(const std::string& preset_id, ActionCallback callback) {
    std::thread([preset_id, callback = std::move(callback)]() {
        std::string path = std::string(REST_API_PATH) + "?id=eq." + preset_id;
        HttpResponse resp = perform_http_request("DELETE", path);

        if (resp.status_code >= 200 && resp.status_code < 300) {
            if (callback) callback(true, "");
        } else {
            std::string err = resp.error.empty() ? ("HTTP " + std::to_string(resp.status_code) + ": " + resp.body) : resp.error;
            if (callback) callback(false, err);
        }
    }).detach();
}

} // namespace Cloud
} // namespace Prismix

