#pragma once

#include "patchstorage_types.h"
#include "patchstorage_client.h"
#include "lv2_installer.h"

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <filesystem>
#include <functional>
#include <atomic>

namespace prismix::patchstorage {

struct BrowseState {
    int current_page{1};
    int total_pages{1};
    int total_items{0};
    std::string query;
    int target_id{0};      // 0 = all targets
    int category_id{0};    // 0 = all categories
    std::string orderby;   // empty = API default ordering (best match)
    std::string order;
    bool is_loading{false};
    std::string status_message;
    std::vector<PatchStorageItem> items;
    std::vector<PatchCategory> categories;  // real taxonomy from /categories
    bool categories_loaded{false};
    int generation{0};     // bumped whenever the catalog result set changes
};

class PatchStorageService {
public:
    static PatchStorageService& instance();

    PatchStorageService();
    ~PatchStorageService();

    void start();
    void stop();

    void set_client(std::unique_ptr<PatchStorageClient> client);
    PatchStorageClient* get_client() const { return client_.get(); }

    // User actions (queued asynchronously, safe to call from UI)
    void request_browse(int page = 1,
                        const std::string& query = "",
                        int target_id = 0,
                        int category_id = 0,
                        const std::string& orderby = "",
                        const std::string& order = "");
    void request_browse(int page,
                        const std::string& query,
                        bool compatible_only);

    // Fetches the real PatchStorage category taxonomy once (LV2 platform).
    void request_categories();

    void request_install(const PatchStorageItem& item);

    void request_uninstall(int patch_id, const std::string& bundle_name);

    // Artwork pipeline: downloads (once), decodes on the worker thread and keeps
    // a small bounded cache. Safe to call every render; deduplicated by state.
    void request_artwork(const PatchStorageItem& item);

    // Decoded artwork for a patch id, or nullptr when not ready. The UI keeps
    // the returned shared_ptr for as long as a card/image is on screen, so
    // service-side eviction can never dangle a displayed image.
    std::shared_ptr<const ArtworkImage> get_artwork(int patch_id);
    ArtworkState get_artwork_state(int patch_id);

    // Patch ids currently displayed by the grid (eviction protection).
    void set_visible_artwork(const std::vector<int>& patch_ids);

    // Allows previously failed artwork downloads to be retried (REFRESH).
    void reset_artwork_failures();

    // Fast non-blocking state polling for LVGL UI
    bool poll_state(BrowseState& out_state,
                    std::unordered_map<int, PatchStatus>& out_statuses);

    // Check if state has changed since last poll
    bool has_pending_ui_update() const { return state_dirty_.load(); }

    // Refresh installation states of all cached items against current disk state
    void refresh_local_statuses();

    // Queues the above on the service worker (safe from the UI thread, e.g.
    // right after a plugin was added to / removed from a rack).
    void request_status_refresh();

private:
    void worker_loop();
    void queue_task(std::function<void()> task);

    void load_artwork_task(int patch_id, std::string url);
    void evict_artwork_locked();
    std::filesystem::path artwork_dir() const;

    std::unique_ptr<PatchStorageClient> client_;

    std::atomic<bool> running_{false};
    std::thread worker_thread_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::queue<std::function<void()>> task_queue_;

    mutable std::mutex state_mutex_;
    BrowseState browse_state_;
    std::unordered_map<int, PatchStatus> patch_statuses_;
    std::atomic<bool> state_dirty_{true};

    bool categories_requested_{false};

    std::unordered_map<int, std::shared_ptr<const ArtworkImage>> artwork_cache_;
    std::unordered_map<int, ArtworkState> artwork_states_;
    std::vector<int> artwork_lru_;
    std::unordered_set<int> visible_artwork_ids_;
    int artwork_loads_since_prune_{0};
};

} // namespace prismix::patchstorage

