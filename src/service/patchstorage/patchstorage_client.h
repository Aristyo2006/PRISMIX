#pragma once

#include "patchstorage_types.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

namespace prismix::patchstorage {

struct HttpResponse {
    int status_code{0};
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    std::string error_message;
};

class IHttpExecutor {
public:
    virtual ~IHttpExecutor() = default;
    virtual HttpResponse get(const std::string& url, int timeout_seconds = 15) = 0;
    virtual bool download(const std::string& url,
                          const std::string& dest_path,
                          int timeout_seconds = 60,
                          std::string* error = nullptr) = 0;
};

class CurlHttpExecutor : public IHttpExecutor {
public:
    HttpResponse get(const std::string& url, int timeout_seconds = 15) override;
    bool download(const std::string& url,
                  const std::string& dest_path,
                  int timeout_seconds = 60,
                  std::string* error = nullptr) override;
};

struct FetchResult {
    bool success{false};
    int current_page{1};
    int total_pages{1};
    int total_items{0};
    std::vector<PatchStorageItem> items;
    std::string error_message;
};

class PatchStorageClient {
public:
    explicit PatchStorageClient(std::shared_ptr<IHttpExecutor> executor = nullptr);
    ~PatchStorageClient() = default;

    void set_executor(std::shared_ptr<IHttpExecutor> executor) {
        executor_ = std::move(executor);
    }

    FetchResult fetch_patches(int page = 1,
                              int per_page = 15,
                              const std::string& query = "",
                              int target_id = 0,
                              int category_id = 0,
                              const std::string& orderby = "",
                              const std::string& order = "desc");

    bool fetch_categories(std::vector<PatchCategory>& out_categories,
                          std::string& out_error);

    bool fetch_patch_details(int patch_id,
                             PatchStorageItem& out_patch,
                             std::string& out_error);

    bool download_patch_file(const std::string& download_url,
                             const std::string& destination_file,
                             std::string& out_error);

    static bool open_in_browser(const std::string& url);

private:
    std::shared_ptr<IHttpExecutor> executor_;
};

} // namespace prismix::patchstorage
