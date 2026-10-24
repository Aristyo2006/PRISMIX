#include "lv2_host.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdlib>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

namespace prismix::lv2 {

namespace {

void* loadSharedLibrary(const std::string& path) {
#if defined(_WIN32)
    return static_cast<void*>(LoadLibraryA(path.c_str()));
#else
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void* resolveSymbol(void* handle, const char* symbol_name) {
    if (!handle) return nullptr;
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(handle), symbol_name));
#else
    return dlsym(handle, symbol_name);
#endif
}

void freeSharedLibrary(void* handle) {
    if (!handle) return;
#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

std::string readFileContent(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::in | std::ios::binary);
    if (!f.is_open()) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

// ---------------- Lv2Instance ----------------

Lv2Instance::Lv2Instance(void* lib_handle,
                         const LV2_Descriptor* descriptor,
                         LV2_Handle instance_handle,
                         std::string uri,
                         std::vector<Lv2PortMetadata> ports)
    : lib_handle_(lib_handle),
      descriptor_(descriptor),
      instance_handle_(instance_handle),
      uri_(std::move(uri)),
      ports_(std::move(ports)),
      is_active_(false) {
}

Lv2Instance::~Lv2Instance() {
    cleanup();
}

Lv2Instance::Lv2Instance(Lv2Instance&& other) noexcept
    : lib_handle_(other.lib_handle_),
      descriptor_(other.descriptor_),
      instance_handle_(other.instance_handle_),
      uri_(std::move(other.uri_)),
      ports_(std::move(other.ports_)),
      is_active_(other.is_active_) {
    other.lib_handle_ = nullptr;
    other.descriptor_ = nullptr;
    other.instance_handle_ = nullptr;
    other.is_active_ = false;
}

Lv2Instance& Lv2Instance::operator=(Lv2Instance&& other) noexcept {
    if (this != &other) {
        cleanup();
        lib_handle_ = other.lib_handle_;
        descriptor_ = other.descriptor_;
        instance_handle_ = other.instance_handle_;
        uri_ = std::move(other.uri_);
        ports_ = std::move(other.ports_);
        is_active_ = other.is_active_;

        other.lib_handle_ = nullptr;
        other.descriptor_ = nullptr;
        other.instance_handle_ = nullptr;
        other.is_active_ = false;
    }
    return *this;
}

void Lv2Instance::cleanup() {
    if (instance_handle_ && descriptor_) {
        if (is_active_ && descriptor_->deactivate) {
            descriptor_->deactivate(instance_handle_);
            is_active_ = false;
        }
        if (descriptor_->cleanup) {
            descriptor_->cleanup(instance_handle_);
        }
        instance_handle_ = nullptr;
    }
    if (lib_handle_) {
        freeSharedLibrary(lib_handle_);
        lib_handle_ = nullptr;
    }
}

void Lv2Instance::connect_port(uint32_t port, void* data_location) {
    if (descriptor_ && descriptor_->connect_port && instance_handle_) {
        descriptor_->connect_port(instance_handle_, port, data_location);
    }
}

void Lv2Instance::activate() {
    if (descriptor_ && descriptor_->activate && instance_handle_ && !is_active_) {
        descriptor_->activate(instance_handle_);
        is_active_ = true;
    }
}

void Lv2Instance::run(uint32_t sample_count) {
    if (descriptor_ && descriptor_->run && instance_handle_) {
        descriptor_->run(instance_handle_, sample_count);
    }
}

void Lv2Instance::deactivate() {
    if (descriptor_ && descriptor_->deactivate && instance_handle_ && is_active_) {
        descriptor_->deactivate(instance_handle_);
        is_active_ = false;
    }
}

// ---------------- Lv2Host ----------------

Lv2Host& Lv2Host::instance() {
    static Lv2Host host;
    return host;
}

Lv2Host::Lv2Host() {
    // Standard LV2_PATH discovery
    const char* env_path = std::getenv("LV2_PATH");
    if (env_path && *env_path) {
        std::string paths = env_path;
        size_t start = 0;
#if defined(_WIN32)
        char sep = ';';
#else
        char sep = ':';
#endif
        while (start < paths.size()) {
            size_t end = paths.find(sep, start);
            if (end == std::string::npos) end = paths.size();
            std::string p = paths.substr(start, end - start);
            if (!p.empty()) search_paths_.push_back(p);
            start = end + 1;
        }
    }

    // Default search paths
    search_paths_.push_back("assets/lv2");
#if !defined(_WIN32)
    search_paths_.push_back("/usr/lib/lv2");
    search_paths_.push_back("/usr/local/lib/lv2");
    const char* home = std::getenv("HOME");
    if (home) {
        search_paths_.push_back(std::string(home) + "/.lv2");
    }
#endif

    refreshCatalog();
}

void Lv2Host::addSearchPath(const std::string& path) {
    if (std::find(search_paths_.begin(), search_paths_.end(), path) == search_paths_.end()) {
        search_paths_.push_back(path);
    }
}

void Lv2Host::clearSearchPaths() {
    search_paths_.clear();
}

bool Lv2Host::isFeatureSupported(const std::string& feature_uri) {
    if (feature_uri == LV2_CORE__hardRTCapable ||
        feature_uri == LV2_CORE__inPlaceBroken ||
        feature_uri == "http://lv2plug.in/ns/ext/urid#map" ||
        feature_uri == "http://lv2plug.in/ns/ext/worker#schedule") {
        return true;
    }
    return false;
}

void Lv2Host::refreshCatalog() {
    plugins_.clear();

    for (const auto& basePathStr : search_paths_) {
        std::error_code ec;
        std::filesystem::path basePath(basePathStr);
        if (!std::filesystem::exists(basePath, ec) || !std::filesystem::is_directory(basePath, ec)) {
            continue;
        }

        for (const auto& entry : std::filesystem::directory_iterator(basePath, ec)) {
            if (!entry.is_directory()) continue;
            auto bundlePath = entry.path();
            if (bundlePath.extension() != ".lv2") continue;

            auto manifestPath = bundlePath / "manifest.ttl";
            if (!std::filesystem::exists(manifestPath, ec)) continue;

            std::string manifestContent = readFileContent(manifestPath);
            std::vector<Lv2ParsedPlugin> parsedPlugins;
            if (!Lv2ManifestParser::parseManifest(manifestContent, parsedPlugins)) {
                continue;
            }

            for (auto& parsed : parsedPlugins) {
                DiscoveredLv2Plugin discovered;
                discovered.bundle_path = bundlePath.string();

                // Parse plugin TTL
                if (!parsed.see_also_rel_path.empty()) {
                    auto ttlPath = bundlePath / parsed.see_also_rel_path;
                    if (std::filesystem::exists(ttlPath, ec)) {
                        std::string ttlContent = readFileContent(ttlPath);
                        Lv2ManifestParser::parsePluginTtl(ttlContent, parsed);
                    }
                }

                // Resolve binary path
                if (!parsed.binary_rel_path.empty()) {
                    auto binPath = bundlePath / parsed.binary_rel_path;
                    discovered.binary_abs_path = binPath.string();
                }

                // Validate feature compatibility
                discovered.is_compatible = true;
                for (const auto& req : parsed.required_features) {
                    if (!isFeatureSupported(req)) {
                        discovered.is_compatible = false;
                        discovered.incompatibility_reason = "Unsupported required feature: " + req;
                        break;
                    }
                }

                if (discovered.is_compatible) {
                    if (parsed.audio_inputs == 0 || parsed.audio_outputs == 0) {
                        discovered.is_compatible = false;
                        discovered.incompatibility_reason = "No audio I/O ports found";
                    } else if (discovered.binary_abs_path.empty() || !std::filesystem::exists(discovered.binary_abs_path, ec)) {
                        discovered.is_compatible = false;
                        discovered.incompatibility_reason = "Plugin binary not found: " + parsed.binary_rel_path;
                    }
                }

                discovered.metadata = std::move(parsed);
                plugins_.push_back(std::move(discovered));
            }
        }
    }
}

const DiscoveredLv2Plugin* Lv2Host::findPlugin(const std::string& uri) const {
    for (const auto& p : plugins_) {
        if (p.metadata.uri == uri) return &p;
    }
    return nullptr;
}

std::unique_ptr<Lv2Instance> Lv2Host::instantiatePlugin(const std::string& uri,
                                                        double sample_rate) {
    const auto* info = findPlugin(uri);
    if (!info || !info->is_compatible) {
        return nullptr;
    }

    void* lib_handle = loadSharedLibrary(info->binary_abs_path);
    if (!lib_handle) {
        return nullptr;
    }

    auto desc_func = reinterpret_cast<LV2_Descriptor_Function>(
        resolveSymbol(lib_handle, "lv2_descriptor"));
    if (!desc_func) {
        freeSharedLibrary(lib_handle);
        return nullptr;
    }

    const LV2_Descriptor* descriptor = nullptr;
    for (uint32_t idx = 0; ; ++idx) {
        const auto* d = desc_func(idx);
        if (!d) break;
        if (d->URI && std::string(d->URI) == uri) {
            descriptor = d;
            break;
        }
    }

    if (!descriptor || !descriptor->instantiate) {
        freeSharedLibrary(lib_handle);
        return nullptr;
    }

    // Prepare standard host features
    static const LV2_Feature hard_rt_feature = { LV2_CORE__hardRTCapable, nullptr };
    static const LV2_Feature* const features[] = {
        &hard_rt_feature,
        nullptr
    };

    LV2_Handle handle = descriptor->instantiate(descriptor,
                                               sample_rate,
                                               info->bundle_path.c_str(),
                                               features);
    if (!handle) {
        freeSharedLibrary(lib_handle);
        return nullptr;
    }

    auto instance = std::make_unique<Lv2Instance>(
        lib_handle, descriptor, handle, uri, info->metadata.ports);
    instance->activate();
    return instance;
}

} // namespace prismix::lv2