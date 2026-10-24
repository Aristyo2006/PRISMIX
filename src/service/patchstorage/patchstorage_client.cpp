#include "patchstorage_client.h"
#include "patchstorage_json.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <filesystem>

namespace prismix::patchstorage {

namespace {

std::string url_encode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (char c : value) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::uppercase << (static_cast<int>(static_cast<unsigned char>(c)) & 0xFF);
        }
    }

    return escaped.str();
}

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

} // namespace

// ---------------- CurlHttpExecutor ----------------

HttpResponse CurlHttpExecutor::get(const std::string& url, int timeout_seconds) {
    HttpResponse resp;

    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path tmp_hdr = std::filesystem::temp_directory_path() / ("prismix_hdr_" + std::to_string(now) + ".tmp");

    std::string cmd = "curl -s -S -L --max-time " + std::to_string(timeout_seconds) +
                      " -D \"" + tmp_hdr.string() + "\" \"" + url + "\" 2>&1";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        resp.error_message = "Failed to spawn curl subprocess";
        return resp;
    }

    char buffer[4096];
    std::ostringstream out;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        out << buffer;
    }
    int exit_code = pclose(pipe);

    // Read headers file
    std::ifstream hdr_file(tmp_hdr);
    if (hdr_file.is_open()) {
        std::string line;
        while (std::getline(hdr_file, line)) {
            // Trim \r
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            if (line.rfind("HTTP/", 0) == 0) {
                // Status line: HTTP/1.1 200 OK
                auto space1 = line.find(' ');
                if (space1 != std::string::npos) {
                    auto space2 = line.find(' ', space1 + 1);
                    std::string code_str = (space2 != std::string::npos)
                        ? line.substr(space1 + 1, space2 - space1 - 1)
                        : line.substr(space1 + 1);
                    try {
                        resp.status_code = std::stoi(code_str);
                    } catch (...) {}
                }
            } else {
                auto colon = line.find(':');
                if (colon != std::string::npos) {
                    std::string key = to_lower(line.substr(0, colon));
                    std::string val = line.substr(colon + 1);
                    // Trim leading spaces from val
                    while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) {
                        val.erase(val.begin());
                    }
                    resp.headers[key] = val;
                }
            }
        }
        hdr_file.close();
        std::error_code ec;
        std::filesystem::remove(tmp_hdr, ec);
    }

    if (exit_code != 0 && resp.status_code == 0) {
        resp.error_message = "Curl request failed: " + out.str();
    } else {
        resp.body = out.str();
    }

    return resp;
}

bool CurlHttpExecutor::download(const std::string& url,
                               const std::string& dest_path,
                               int timeout_seconds,
                               std::string* error) {
    std::error_code ec;
    std::filesystem::path dest(dest_path);
    std::filesystem::create_directories(dest.parent_path(), ec);

    std::string cmd = "curl -s -S -L --fail --max-time " + std::to_string(timeout_seconds) +
                      " -o \"" + dest_path + "\" \"" + url + "\" 2>&1";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        if (error) *error = "Failed to launch curl download process";
        return false;
    }

    char buffer[1024];
    std::ostringstream err_stream;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        err_stream << buffer;
    }
    int exit_code = pclose(pipe);

    if (exit_code != 0) {
        if (error) *error = "Download failed (curl exit " + std::to_string(exit_code) + "): " + err_stream.str();
        return false;
    }

    if (!std::filesystem::exists(dest_path, ec) || std::filesystem::file_size(dest_path, ec) == 0) {
        if (error) *error = "Downloaded file is missing or empty";
        return false;
    }

    return true;
}

// ---------------- PatchStorageClient ----------------

PatchStorageClient::PatchStorageClient(std::shared_ptr<IHttpExecutor> executor)
    : executor_(std::move(executor)) {
    if (!executor_) {
        executor_ = std::make_shared<CurlHttpExecutor>();
    }
}

FetchResult PatchStorageClient::fetch_patches(int page,
                                            int per_page,
                                            const std::string& query,
                                            int target_id,
                                            int category_id,
                                            const std::string& orderby,
                                            const std::string& order) {
    FetchResult result;
    result.current_page = std::max(1, page);

    std::string url = "https://patchstorage.com/api/beta/patches?platforms=" +
                      std::to_string(PLATFORM_ID_LV2) +
                      "&page=" + std::to_string(result.current_page) +
                      "&per_page=" + std::to_string(std::max(1, per_page));

    if (!query.empty()) {
        url += "&search=" + url_encode(query);
    }
    if (target_id > 0) {
        url += "&targets=" + std::to_string(target_id);
    }
    if (category_id > 0) {
        url += "&categories=" + std::to_string(category_id);
    }
    if (!orderby.empty()) {
        url += "&orderby=" + url_encode(orderby);
        url += "&order=" + url_encode(order.empty() ? std::string("desc") : order);
    }

    HttpResponse resp = executor_->get(url, 15);
    if (resp.status_code != 200) {
        result.error_message = resp.error_message.empty()
            ? "HTTP " + std::to_string(resp.status_code)
            : resp.error_message;
        return result;
    }

    // Extract pagination headers
    auto it_total = resp.headers.find("x-wp-total");
    if (it_total != resp.headers.end()) {
        try { result.total_items = std::stoi(it_total->second); } catch (...) {}
    }
    auto it_pages = resp.headers.find("x-wp-totalpages");
    if (it_pages != resp.headers.end()) {
        try { result.total_pages = std::stoi(it_pages->second); } catch (...) {}
    }

    std::string parse_err;
    if (!parse_patches_list(resp.body, result.items, &parse_err)) {
        result.error_message = "JSON parse error: " + parse_err;
        return result;
    }

    if (result.total_items == 0 && !result.items.empty()) {
        result.total_items = static_cast<int>(result.items.size());
    }
    if (result.total_pages == 0) {
        result.total_pages = 1;
    }

    result.success = true;
    return result;
}

bool PatchStorageClient::fetch_categories(std::vector<PatchCategory>& out_categories,
                                          std::string& out_error) {
    std::string url = "https://patchstorage.com/api/beta/categories?platforms=" +
                      std::to_string(PLATFORM_ID_LV2) + "&per_page=100";
    HttpResponse resp = executor_->get(url, 15);
    if (resp.status_code != 200) {
        out_error = resp.error_message.empty()
            ? "HTTP " + std::to_string(resp.status_code)
            : resp.error_message;
        return false;
    }
    return parse_categories_list(resp.body, out_categories, &out_error);
}

bool PatchStorageClient::fetch_patch_details(int patch_id,
                                            PatchStorageItem& out_patch,
                                            std::string& out_error) {
    std::string url = "https://patchstorage.com/api/beta/patches/" + std::to_string(patch_id);
    HttpResponse resp = executor_->get(url, 15);
    if (resp.status_code != 200) {
        out_error = resp.error_message.empty()
            ? "HTTP " + std::to_string(resp.status_code)
            : resp.error_message;
        return false;
    }

    return parse_single_patch(resp.body, out_patch, &out_error);
}

bool PatchStorageClient::download_patch_file(const std::string& download_url,
                                            const std::string& destination_file,
                                            std::string& out_error) {
    return executor_->download(download_url, destination_file, 60, &out_error);
}

bool PatchStorageClient::open_in_browser(const std::string& url) {
    std::string target_url = url.empty() ? "https://patchstorage.com/platform/lv2/" : url;

#if defined(_WIN32)
    std::string cmd = "start \"\" \"" + target_url + "\"";
#elif defined(__APPLE__)
    std::string cmd = "open \"" + target_url + "\" &";
#else
    std::string cmd = "xdg-open \"" + target_url + "\" >/dev/null 2>&1 &";
#endif

    int ret = std::system(cmd.c_str());
    return ret == 0;
}

} // namespace prismix::patchstorage

