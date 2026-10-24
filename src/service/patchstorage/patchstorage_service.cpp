#include "patchstorage_service.h"
#include "patchstorage_artwork.h"
#include <iostream>
#include <chrono>
#include <filesystem>

namespace prismix::patchstorage {

PatchStorageService& PatchStorageService::instance() {
    static PatchStorageService s_instance;
    return s_instance;
}

PatchStorageService::PatchStorageService() {
    client_ = std::make_unique<PatchStorageClient>();
    start();
}

PatchStorageService::~PatchStorageService() {
    stop();
}

void PatchStorageService::start() {
    if (running_.exchange(true)) return;

    worker_thread_ = std::thread([this]() {
        worker_loop();
    });
}

void PatchStorageService::stop() {
    if (!running_.exchange(false)) return;

    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        // Clear pending tasks
        std::queue<std::function<void()>> empty;
        std::swap(task_queue_, empty);
    }
    queue_cv_.notify_all();

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void PatchStorageService::set_client(std::unique_ptr<PatchStorageClient> client) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    client_ = std::move(client);
}

void PatchStorageService::queue_task(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        task_queue_.push(std::move(task));
    }
    queue_cv_.notify_one();
}

void PatchStorageService::worker_loop() {
    while (running_) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this]() {
                return !running_ || !task_queue_.empty();
            });

            if (!running_) break;
            if (task_queue_.empty()) continue;

            task = std::move(task_queue_.front());
            task_queue_.pop();
        }

        if (task) {
            try {
                task();
            } catch (const std::exception& e) {
                std::cerr << "[PatchStorageService] Task exception: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[PatchStorageService] Unknown task exception" << std::endl;
            }
        }
    }
}

void PatchStorageService::request_browse(int page,
                                         const std::string& query,
                                         int target_id,
                                         int category_id,
                                         const std::string& orderby,
                                         const std::string& order) {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        browse_state_.is_loading = true;
        browse_state_.current_page = page;
        browse_state_.query = query;
        browse_state_.status_message = "Fetching LV2 catalog from PatchStorage...";
        state_dirty_.store(true);
    }

    queue_task([this, page, query, target_id, category_id, orderby, order]() {
        FetchResult res = client_->fetch_patches(page, 15, query, target_id, category_id, orderby, order);

        std::lock_guard<std::mutex> lock(state_mutex_);
        browse_state_.is_loading = false;
        if (res.success) {
            browse_state_.current_page = res.current_page;
            browse_state_.total_pages = res.total_pages;
            browse_state_.total_items = res.total_items;
            browse_state_.items = std::move(res.items);
            browse_state_.status_message = "Connected to PatchStorage (LV2 #8046) • " +
                                           std::to_string(browse_state_.total_items) + " plugins found";
            refresh_local_statuses();
        } else {
            browse_state_.status_message = "Offline or API Error: " + res.error_message + " (Check network or View on Web)";
        }
        state_dirty_.store(true);
    });
}

void PatchStorageService::request_browse(int page,
                                         const std::string& query,
                                         bool compatible_only) {
    HostArch host_arch = detect_current_host_arch();
    int target_id = compatible_only ? get_compatible_target_id_for_host(host_arch) : 0;
    request_browse(page, query, target_id, 0, "", "");
}

void PatchStorageService::request_install(const PatchStorageItem& item) {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        PatchStatus& st = patch_statuses_[item.id];
        st.state = PatchInstallState::Downloading;
        st.download_progress = 0.1f;
        st.message = "Preparing installation...";
        state_dirty_.store(true);
    }

    queue_task([this, item]() {
        HostArch host_arch = detect_current_host_arch();

        // 1. Ensure we have detailed patch information with files
        PatchStorageItem target_item = item;
        if (target_item.files.empty()) {
            std::string fetch_err;
            if (!client_->fetch_patch_details(item.id, target_item, fetch_err)) {
                std::lock_guard<std::mutex> lock(state_mutex_);
                PatchStatus& st = patch_statuses_[item.id];
                st.state = PatchInstallState::Error;
                st.message = "Failed to fetch patch details: " + fetch_err;
                state_dirty_.store(true);
                return;
            }
        }

        // 2. Locate compatible target file
        const PatchFile* file_to_install = target_item.find_file_for_host(host_arch);
        if (!file_to_install) {
            std::lock_guard<std::mutex> lock(state_mutex_);
            PatchStatus& st = patch_statuses_[item.id];
            st.state = PatchInstallState::Incompatible;
            st.message = "No binary available for host " + std::string(host_arch_to_string(host_arch));
            state_dirty_.store(true);
            return;
        }

        // 3. Download package
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            PatchStatus& st = patch_statuses_[item.id];
            st.state = PatchInstallState::Downloading;
            st.download_progress = 0.35f;
            st.message = "Downloading " + file_to_install->filename + "...";
            state_dirty_.store(true);
        }

        std::filesystem::path staging_dir = Lv2Installer::instance().get_staging_dir();
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        std::filesystem::path archive_file = staging_dir / ("patch_" + std::to_string(item.id) + "_" + std::to_string(now) + ".tar.gz");

        std::string dl_err;
        if (!client_->download_patch_file(file_to_install->url, archive_file.string(), dl_err)) {
            std::lock_guard<std::mutex> lock(state_mutex_);
            PatchStatus& st = patch_statuses_[item.id];
            st.state = PatchInstallState::Error;
            st.message = "Download error: " + dl_err;
            state_dirty_.store(true);
            return;
        }

        // 4. Validate & Install
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            PatchStatus& st = patch_statuses_[item.id];
            st.state = PatchInstallState::Validating;
            st.download_progress = 0.75f;
            st.message = "Validating binary headers and security...";
            state_dirty_.store(true);
        }

        InstallResult inst_res = Lv2Installer::instance().install_archive(archive_file, host_arch);

        // Remove downloaded archive
        std::error_code ec;
        std::filesystem::remove(archive_file, ec);

        std::lock_guard<std::mutex> lock(state_mutex_);
        PatchStatus& st = patch_statuses_[item.id];
        if (inst_res.success) {
            st.state = PatchInstallState::Installed;
            st.download_progress = 1.0f;
            st.installed_bundle_name = inst_res.installed_bundle_name;
            st.installed_bundle_path = inst_res.installed_bundle_path;
            if (!inst_res.discovered_plugin_uris.empty()) {
                st.installed_plugin_uri = inst_res.discovered_plugin_uris.front();
            }
            st.message = "Installed successfully: " + inst_res.installed_bundle_name;
        } else {
            st.state = PatchInstallState::Error;
            st.message = inst_res.error_message;
        }
        state_dirty_.store(true);
    });
}

void PatchStorageService::request_uninstall(int patch_id, const std::string& bundle_name) {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        PatchStatus& st = patch_statuses_[patch_id];
        st.state = PatchInstallState::Installing; // transient removing state
        st.message = "Uninstalling bundle...";
        state_dirty_.store(true);
    }

    queue_task([this, patch_id, bundle_name]() {
        std::string err;
        bool ok = Lv2Installer::instance().uninstall_bundle(bundle_name, err);

        std::lock_guard<std::mutex> lock(state_mutex_);
        PatchStatus& st = patch_statuses_[patch_id];
        if (ok) {
            st.state = PatchInstallState::NotInstalled;
            st.installed_bundle_name.clear();
            st.installed_bundle_path.clear();
            st.installed_plugin_uri.clear();
            st.message = "Uninstalled " + bundle_name;
        } else {
            st.state = PatchInstallState::Error;
            st.message = err;
        }
        state_dirty_.store(true);
    });
}

void PatchStorageService::refresh_local_statuses() {
    HostArch host_arch = detect_current_host_arch();
    for (const auto& item : browse_state_.items) {
        PatchStatus& st = patch_statuses_[item.id];
        // If not in active transaction
        if (st.state == PatchInstallState::Downloading ||
            st.state == PatchInstallState::Installing ||
            st.state == PatchInstallState::Validating) {
            continue;
        }

        std::string bundle_name;
        std::string plugin_uri;
        if (Lv2Installer::instance().is_patch_installed(item, &bundle_name, &plugin_uri)) {
            st.state = PatchInstallState::Installed;
            st.installed_bundle_name = bundle_name;
            st.installed_plugin_uri = plugin_uri;
            st.message = "Installed locally";
        } else if (!item.is_compatible_with_host(host_arch)) {
            st.state = PatchInstallState::Incompatible;
            st.message = "Incompatible with host architecture";
        } else {
            st.state = PatchInstallState::NotInstalled;
            st.message = "Available for download";
        }
    }
}

bool PatchStorageService::poll_state(BrowseState& out_state,
                                    std::unordered_map<int, PatchStatus>& out_statuses) {
    bool was_dirty = state_dirty_.exchange(false);
    std::lock_guard<std::mutex> lock(state_mutex_);
    out_state = browse_state_;
    out_statuses = patch_statuses_;
    return was_dirty;
}

void PatchStorageService::request_categories() {
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (categories_requested_) return;
        categories_requested_ = true;
    }
    queue_task([this]() {
        std::vector<PatchCategory> cats;
        std::string err;
        if (client_->fetch_categories(cats, err)) {
            std::lock_guard<std::mutex> lock(state_mutex_);
            browse_state_.categories = std::move(cats);
            browse_state_.categories_loaded = true;
            state_dirty_.store(true);
        }
    });
}

void PatchStorageService::request_status_refresh() {
    queue_task([this]() {
        std::lock_guard<std::mutex> lock(state_mutex_);
        refresh_local_statuses();
        state_dirty_.store(true);
    });
}

void PatchStorageService::reset_artwork_failures() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    for (auto& [id, st] : artwork_states_) {
        if (st == ArtworkState::Failed) {
            st = ArtworkState::None;
        }
    }
    state_dirty_.store(true);
}

void PatchStorageService::set_visible_artwork(const std::vector<int>& patch_ids) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    visible_artwork_ids_.clear();
    for (int id : patch_ids) visible_artwork_ids_.insert(id);
}

std::shared_ptr<const ArtworkImage> PatchStorageService::get_artwork(int patch_id) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto it = artwork_cache_.find(patch_id);
    if (it != artwork_cache_.end()) return it->second;
    return nullptr;
}

ArtworkState PatchStorageService::get_artwork_state(int patch_id) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto it = artwork_states_.find(patch_id);
    if (it != artwork_states_.end()) return it->second;
    return ArtworkState::None;
}

std::filesystem::path PatchStorageService::artwork_dir() const {
    std::filesystem::path p = std::filesystem::temp_directory_path() / "prismix" / "artwork_cache";
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p;
}

void PatchStorageService::evict_artwork_locked() {
    constexpr size_t kMaxMemArtwork = 60;
    while (artwork_cache_.size() > kMaxMemArtwork && !artwork_lru_.empty()) {
        int victim = artwork_lru_.front();
        artwork_lru_.erase(artwork_lru_.begin());
        if (visible_artwork_ids_.count(victim) == 0) {
            artwork_cache_.erase(victim);
            if (artwork_states_[victim] == ArtworkState::Ready) {
                artwork_states_[victim] = ArtworkState::None;
            }
        }
    }
}

void PatchStorageService::request_artwork(const PatchStorageItem& item) {
    std::string url = !item.artwork_thumbnail_url.empty() ? item.artwork_thumbnail_url : item.artwork_url;
    if (url.empty()) return;
    int pid = item.id;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (artwork_states_[pid] != ArtworkState::None) return;
        artwork_states_[pid] = ArtworkState::Loading;
    }
    queue_task([this, pid, url]() {
        load_artwork_task(pid, url);
    });
}

void PatchStorageService::load_artwork_task(int patch_id, std::string url) {
    std::filesystem::path dir = artwork_dir();
    std::filesystem::path raw_file = artwork::cache_file_for(dir, patch_id, ".rgb");
    std::vector<uint8_t> pixels;
    int w = 0, h = 0;
    if (artwork::load_raw_cache(raw_file, w, h, pixels)) {
        auto img = std::make_shared<ArtworkImage>();
        img->width = w;
        img->height = h;
        img->pixels = std::move(pixels);
        std::lock_guard<std::mutex> lock(state_mutex_);
        artwork_cache_[patch_id] = img;
        artwork_states_[patch_id] = ArtworkState::Ready;
        artwork_lru_.push_back(patch_id);
        evict_artwork_locked();
        state_dirty_.store(true);
        return;
    }

    std::filesystem::path png_file = artwork::cache_file_for(dir, patch_id, ".png");
    std::string err;
    if (!client_->download_patch_file(url, png_file.string(), err)) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        artwork_states_[patch_id] = ArtworkState::Failed;
        state_dirty_.store(true);
        return;
    }

    if (!artwork::decode_png_file_to_pixels(png_file, artwork::kArtworkMaxDimension, w, h, pixels, err)) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        artwork_states_[patch_id] = ArtworkState::Failed;
        state_dirty_.store(true);
        return;
    }

    artwork::save_raw_cache(raw_file, w, h, pixels, err);
    auto img = std::make_shared<ArtworkImage>();
    img->width = w;
    img->height = h;
    img->pixels = std::move(pixels);
    std::lock_guard<std::mutex> lock(state_mutex_);
    artwork_cache_[patch_id] = img;
    artwork_states_[patch_id] = ArtworkState::Ready;
    artwork_lru_.push_back(patch_id);
    evict_artwork_locked();
    state_dirty_.store(true);
}

} // namespace prismix::patchstorage
