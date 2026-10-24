#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>

struct PluginParameter {
    std::string id;
    std::string name;
    std::string unit;
    float min_val = 0.0f;
    float max_val = 1.0f;
    float default_val = 0.5f;
    float current_val = 0.5f;
    float step = 0.01f;
    bool is_toggle = false;
};

enum class PluginSourceType : uint8_t {
    Builtin,
    Lv2,
    NamLite
};

struct PluginMetadata {
    std::string uri;          // e.g. "http://audiocore.org/plugins/reverb"
    std::string name;         // "Plate & Hall Reverb"
    std::string category;     // "Reverb", "Delay", "Dynamics", "EQ", "AFS", "Limiter", etc.
    std::string author;       // "AudioCore DSP"
    std::string description;  // Short description
    PluginSourceType source_type = PluginSourceType::Builtin;
    bool is_live_dsp = true;
    bool supports_mono = true;
    bool supports_stereo = true;
    bool prefers_send_return_on_aux = false;
    uint32_t audio_inputs = 2;
    uint32_t audio_outputs = 2;
    uint32_t estimated_mflops = 0;
    bool meets_realtime_budget = true;
    std::string model_file_path;
    std::string validation_error;
    std::vector<PluginParameter> parameters;
};

class PluginInstance {
public:
    explicit PluginInstance(const PluginMetadata& meta);
    virtual ~PluginInstance() = default;

    uint64_t get_instance_id() const { return instance_id_; }

    const std::string& get_uri() const { return meta_.uri; }
    const std::string& get_name() const { return meta_.name; }
    const std::string& get_category() const { return meta_.category; }
    const std::string& get_description() const { return meta_.description; }
    PluginSourceType get_source_type() const { return meta_.source_type; }
    bool is_live_dsp() const { return meta_.is_live_dsp; }
    bool supports_mono() const { return meta_.supports_mono; }
    bool supports_stereo() const { return meta_.supports_stereo; }
    bool prefers_send_return_on_aux() const { return meta_.prefers_send_return_on_aux; }
    const std::string& get_model_file_path() const { return meta_.model_file_path; }
    bool meets_realtime_budget() const { return meta_.meets_realtime_budget; }
    const std::string& get_validation_error() const { return meta_.validation_error; }

    int get_target_channel_id() const { return target_channel_id_; }
    void set_target_channel_id(int id) { target_channel_id_ = id; }

    bool is_send_return_mode() const { return send_return_mode_; }
    void set_send_return_mode(bool mode) { send_return_mode_ = mode; }

    bool is_bypassed() const { return bypassed_; }
    void set_bypassed(bool b) { bypassed_ = b; }

    bool is_enabled() const { return enabled_; }
    void set_enabled(bool e) { enabled_ = e; }

    size_t parameter_count() const { return params_.size(); }
    PluginParameter* get_parameter(size_t index);
    const PluginParameter* get_parameter(size_t index) const;
    PluginParameter* find_parameter(const std::string& id);
    const PluginParameter* find_parameter(const std::string& id) const;

    void set_parameter_value(size_t index, float val);
    void set_parameter_value(const std::string& id, float val);
    float get_parameter_value(size_t index) const;
    float get_parameter_value(const std::string& id, float fallback = 0.0f) const;

private:
    uint64_t instance_id_ = 0;
    int target_channel_id_ = -1;
    bool send_return_mode_ = false;
    PluginMetadata meta_;
    std::vector<PluginParameter> params_;
    bool bypassed_ = false;
    bool enabled_ = true;
};

class PluginRack {
public:
    // Each strip or bus owns an ordered, independently processed FX rack.
    static constexpr size_t MAX_SLOTS = 8;

    PluginRack() = default;
    ~PluginRack() = default;
    PluginRack(const PluginRack& other);
    PluginRack& operator=(const PluginRack& other);
    PluginRack(PluginRack&&) noexcept = default;
    PluginRack& operator=(PluginRack&&) noexcept = default;

    void set_owner_context(int channel_id, bool is_aux_bus, bool is_stereo) {
        owner_channel_id_ = channel_id;
        owner_is_aux_bus_ = is_aux_bus;
        owner_is_stereo_ = is_stereo;
        for (auto& slot : slots_) {
            if (slot) {
                slot->set_target_channel_id(channel_id);
                slot->set_send_return_mode(is_aux_bus && slot->prefers_send_return_on_aux());
            }
        }
    }
    int get_owner_channel_id() const { return owner_channel_id_; }
    bool is_owner_aux_bus() const { return owner_is_aux_bus_; }
    bool is_owner_stereo() const { return owner_is_stereo_; }

    size_t count() const { return slots_.size(); }
    bool is_empty() const { return slots_.empty(); }

    PluginInstance* get_slot(size_t index);
    const PluginInstance* get_slot(size_t index) const;

    bool add_plugin(const std::string& uri);
    bool add_instance(std::shared_ptr<PluginInstance> inst);
    bool remove_plugin(size_t index);
    bool move_up(size_t index);
    bool move_down(size_t index);
    void clear();

private:
    int owner_channel_id_ = -1;
    bool owner_is_aux_bus_ = false;
    bool owner_is_stereo_ = true;
    std::vector<std::shared_ptr<PluginInstance>> slots_;
};

class PluginRegistry {
public:
    static PluginRegistry& instance();

    void initialize_builtins();
    void refresh_catalog();
    void discover_lv2_plugins();
    void discover_nam_models(const std::string& directory = "assets/nam");

    const std::vector<PluginMetadata>& get_catalog() const { return catalog_; }
    const PluginMetadata* find_metadata(const std::string& uri) const;
    std::shared_ptr<PluginInstance> create_instance(const std::string& uri) const;

private:
    PluginRegistry();
    std::vector<PluginMetadata> catalog_;
};

