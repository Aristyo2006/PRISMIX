#pragma once

#include "lv2_core.h"
#include "lv2_manifest_parser.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>

namespace prismix::lv2 {

struct DiscoveredLv2Plugin {
    Lv2ParsedPlugin metadata;
    std::string bundle_path;
    std::string binary_abs_path;
    bool is_compatible = true;
    std::string incompatibility_reason;
};

class Lv2Instance {
public:
    Lv2Instance(void* lib_handle,
                const LV2_Descriptor* descriptor,
                LV2_Handle instance_handle,
                std::string uri,
                std::vector<Lv2PortMetadata> ports);
    ~Lv2Instance();

    // Disable copy
    Lv2Instance(const Lv2Instance&) = delete;
    Lv2Instance& operator=(const Lv2Instance&) = delete;

    // Allow move
    Lv2Instance(Lv2Instance&& other) noexcept;
    Lv2Instance& operator=(Lv2Instance&& other) noexcept;

    const std::string& get_uri() const { return uri_; }
    const LV2_Descriptor* descriptor() const { return descriptor_; }
    LV2_Handle handle() const { return instance_handle_; }
    const std::vector<Lv2PortMetadata>& ports() const { return ports_; }

    void connect_port(uint32_t port, void* data_location);
    void activate();
    void run(uint32_t sample_count);
    void deactivate();

    bool is_active() const { return is_active_; }

private:
    void cleanup();

    void* lib_handle_ = nullptr;
    const LV2_Descriptor* descriptor_ = nullptr;
    LV2_Handle instance_handle_ = nullptr;
    std::string uri_;
    std::vector<Lv2PortMetadata> ports_;
    bool is_active_ = false;
};

class Lv2Host {
public:
    static Lv2Host& instance();

    // Bundle scanning and discovery
    void addSearchPath(const std::string& path);
    void clearSearchPaths();
    void refreshCatalog();

    const std::vector<DiscoveredLv2Plugin>& getDiscoveredPlugins() const {
        return plugins_;
    }

    const DiscoveredLv2Plugin* findPlugin(const std::string& uri) const;

    // Plugin instantiation (MUST be called outside the audio thread)
    std::unique_ptr<Lv2Instance> instantiatePlugin(const std::string& uri,
                                                   double sample_rate);

    // Check feature support
    static bool isFeatureSupported(const std::string& feature_uri);

private:
    Lv2Host();
    ~Lv2Host() = default;

    std::vector<std::string> search_paths_;
    std::vector<DiscoveredLv2Plugin> plugins_;
};

} // namespace prismix::lv2
