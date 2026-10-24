#include "plugin_model.h"
#include "../dsp/lv2/lv2_host.h"
#include "../dsp/nam/nam_model.h"
#include <atomic>
#include <cmath>
#include <filesystem>

namespace {
std::atomic<uint64_t> g_next_plugin_instance_id{1};
} // namespace

PluginInstance::PluginInstance(const PluginMetadata& meta)
    : instance_id_(g_next_plugin_instance_id.fetch_add(1, std::memory_order_relaxed)),
      meta_(meta),
      params_(meta.parameters),
      bypassed_(false),
      enabled_(true) {
}

PluginParameter* PluginInstance::get_parameter(size_t index) {
    if (index < params_.size()) return &params_[index];
    return nullptr;
}

const PluginParameter* PluginInstance::get_parameter(size_t index) const {
    if (index < params_.size()) return &params_[index];
    return nullptr;
}

PluginParameter* PluginInstance::find_parameter(const std::string& id) {
    for (auto& p : params_) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

const PluginParameter* PluginInstance::find_parameter(const std::string& id) const {
    for (const auto& p : params_) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

void PluginInstance::set_parameter_value(size_t index, float val) {
    if (index < params_.size()) {
        params_[index].current_val = std::clamp(val, params_[index].min_val, params_[index].max_val);
    }
}

void PluginInstance::set_parameter_value(const std::string& id, float val) {
    auto* p = find_parameter(id);
    if (p) {
        p->current_val = std::clamp(val, p->min_val, p->max_val);
    }
}

float PluginInstance::get_parameter_value(size_t index) const {
    if (index < params_.size()) {
        return params_[index].current_val;
    }
    return 0.0f;
}

float PluginInstance::get_parameter_value(const std::string& id, float fallback) const {
    const auto* p = find_parameter(id);
    return p ? p->current_val : fallback;
}

// ---------------- PluginRack ----------------

PluginRack::PluginRack(const PluginRack& other)
    : owner_channel_id_(other.owner_channel_id_),
      owner_is_aux_bus_(other.owner_is_aux_bus_),
      owner_is_stereo_(other.owner_is_stereo_) {
    slots_.reserve(other.slots_.size());
    for (const auto& source : other.slots_) {
        if (!source) continue;
        auto copy = PluginRegistry::instance().create_instance(source->get_uri());
        if (!copy) continue;
        copy->set_target_channel_id(source->get_target_channel_id());
        copy->set_send_return_mode(source->is_send_return_mode());
        copy->set_bypassed(source->is_bypassed());
        copy->set_enabled(source->is_enabled());
        for (size_t i = 0; i < source->parameter_count() && i < copy->parameter_count(); ++i) {
            copy->set_parameter_value(i, source->get_parameter_value(i));
        }
        slots_.push_back(std::move(copy));
    }
}

PluginRack& PluginRack::operator=(const PluginRack& other) {
    if (this == &other) return *this;
    PluginRack copy(other);
    owner_channel_id_ = copy.owner_channel_id_;
    owner_is_aux_bus_ = copy.owner_is_aux_bus_;
    owner_is_stereo_ = copy.owner_is_stereo_;
    slots_.swap(copy.slots_);
    return *this;
}

PluginInstance* PluginRack::get_slot(size_t index) {
    if (index < slots_.size()) return slots_[index].get();
    return nullptr;
}

const PluginInstance* PluginRack::get_slot(size_t index) const {
    if (index < slots_.size()) return slots_[index].get();
    return nullptr;
}

bool PluginRack::add_plugin(const std::string& uri) {
    if (slots_.size() >= MAX_SLOTS) return false;
    const auto* meta = PluginRegistry::instance().find_metadata(uri);
    if (!meta || !meta->is_live_dsp) return false;
    if (owner_is_stereo_ && !meta->supports_stereo) return false;
    if (!owner_is_stereo_ && !meta->supports_mono) return false;

    auto inst = PluginRegistry::instance().create_instance(uri);
    if (!inst) return false;
    inst->set_target_channel_id(owner_channel_id_);
    if (owner_is_aux_bus_ && meta->prefers_send_return_on_aux) {
        inst->set_send_return_mode(true);
        inst->set_parameter_value("mix", 1.0f);
    } else {
        inst->set_send_return_mode(false);
    }
    slots_.push_back(std::move(inst));
    return true;
}

bool PluginRack::add_instance(std::shared_ptr<PluginInstance> inst) {
    if (slots_.size() >= MAX_SLOTS || !inst) return false;
    inst->set_target_channel_id(owner_channel_id_);
    slots_.push_back(std::move(inst));
    return true;
}

bool PluginRack::remove_plugin(size_t index) {
    if (index < slots_.size()) {
        slots_.erase(slots_.begin() + index);
        return true;
    }
    return false;
}

bool PluginRack::move_up(size_t index) {
    if (index > 0 && index < slots_.size()) {
        std::swap(slots_[index], slots_[index - 1]);
        return true;
    }
    return false;
}

bool PluginRack::move_down(size_t index) {
    if (index + 1 < slots_.size()) {
        std::swap(slots_[index], slots_[index + 1]);
        return true;
    }
    return false;
}

void PluginRack::clear() {
    slots_.clear();
}

// ---------------- PluginRegistry ----------------

PluginRegistry& PluginRegistry::instance() {
    static PluginRegistry reg;
    return reg;
}

PluginRegistry::PluginRegistry() {
    refresh_catalog();
}

void PluginRegistry::initialize_builtins() {
    catalog_.clear();

    // 1. Studio Compressor
    {
        PluginMetadata m;
        m.uri = "http://audiocore.org/plugins/compressor";
        m.name = "Studio Compressor";
        m.category = "Dynamics";
        m.author = "AudioCore DSP";
        m.description = "VCA-style precision peak and RMS compressor with adjustable knee";
        m.is_live_dsp = true;
        m.prefers_send_return_on_aux = false;
        m.parameters = {
            {"thresh", "Threshold", "dB", -60.0f, 0.0f, -18.0f, -18.0f, 0.5f, false},
            {"ratio", "Ratio", ":1", 1.0f, 20.0f, 4.0f, 4.0f, 0.1f, false},
            {"attack", "Attack", "ms", 0.1f, 100.0f, 10.0f, 10.0f, 0.5f, false},
            {"release", "Release", "ms", 10.0f, 1000.0f, 80.0f, 80.0f, 5.0f, false},
            {"knee", "Knee", "dB", 0.0f, 24.0f, 6.0f, 6.0f, 0.5f, false},
            {"makeup", "Makeup", "dB", -12.0f, 24.0f, 0.0f, 0.0f, 0.5f, false}
        };
        catalog_.push_back(m);
    }

    // 2. 4-Band Parametric EQ
    {
        PluginMetadata m;
        m.uri = "http://audiocore.org/plugins/eq";
        m.name = "Parametric EQ";
        m.category = "EQ";
        m.author = "AudioCore DSP";
        m.description = "4-band fully parametric channel equalizer with shelf modes";
        m.is_live_dsp = true;
        m.prefers_send_return_on_aux = false;
        m.parameters = {
            {"low_freq", "Low Freq", "Hz", 20.0f, 500.0f, 80.0f, 80.0f, 1.0f, false},
            {"low_gain", "Low Gain", "dB", -18.0f, 18.0f, 0.0f, 0.0f, 0.5f, false},
            {"mid1_freq", "Mid1 Freq", "Hz", 100.0f, 2000.0f, 400.0f, 400.0f, 5.0f, false},
            {"mid1_gain", "Mid1 Gain", "dB", -18.0f, 18.0f, 0.0f, 0.0f, 0.5f, false},
            {"mid2_freq", "Mid2 Freq", "Hz", 400.0f, 8000.0f, 2500.0f, 2500.0f, 10.0f, false},
            {"mid2_gain", "Mid2 Gain", "dB", -18.0f, 18.0f, 0.0f, 0.0f, 0.5f, false},
            {"high_freq", "High Freq", "Hz", 2000.0f, 20000.0f, 10000.0f, 10000.0f, 50.0f, false},
            {"high_gain", "High Gain", "dB", -18.0f, 18.0f, 0.0f, 0.0f, 0.5f, false}
        };
        catalog_.push_back(m);
    }

    // 3. Plate & Hall Reverb
    {
        PluginMetadata m;
        m.uri = "http://audiocore.org/plugins/reverb";
        m.name = "Plate & Hall Reverb";
        m.category = "Reverb";
        m.author = "AudioCore DSP";
        m.description.clear();
        m.is_live_dsp = true;
        m.prefers_send_return_on_aux = true;
        m.parameters = {
            {"decay", "Decay Time", "s", 0.2f, 12.0f, 2.2f, 2.2f, 0.1f, false},
            {"predelay", "Pre Delay", "ms", 0.0f, 250.0f, 20.0f, 20.0f, 1.0f, false},
            {"damping", "High Damp", "%", 0.0f, 1.0f, 0.45f, 0.45f, 0.05f, false},
            {"width", "Stereo Width", "%", 0.0f, 1.0f, 0.85f, 0.85f, 0.05f, false},
            {"mix", "Dry/Wet Mix", "%", 0.0f, 1.0f, 0.30f, 0.30f, 0.01f, false},
            {"low_cut", "Input HPF", "Hz", 20.0f, 1000.0f, 100.0f, 100.0f, 5.0f, false},
            {"high_cut", "Output LPF", "Hz", 1000.0f, 20000.0f, 10000.0f, 10000.0f, 100.0f, false},
            {"diffusion", "Diffusion", "%", 0.0f, 1.0f, 0.75f, 0.75f, 0.05f, false},
            {"room_type", "Room Preset", "", 0.0f, 3.0f, 1.0f, 1.0f, 1.0f, false}
        };
        catalog_.push_back(m);
    }

    // 4. Stereo Delay
    {
        PluginMetadata m;
        m.uri = "http://audiocore.org/plugins/delay";
        m.name = "Stereo Echo Delay";
        m.category = "Delay";
        m.author = "AudioCore DSP";
        m.description = "Dual-channel stereo delay with tempo sync, note divisions, and ping-pong crossfeed";
        m.is_live_dsp = true;
        m.prefers_send_return_on_aux = true;
        m.parameters = {
            {"time_l", "Delay Time L", "ms", 10.0f, 2000.0f, 320.0f, 320.0f, 5.0f, false},
            {"time_r", "Delay Time R", "ms", 10.0f, 2000.0f, 480.0f, 480.0f, 5.0f, false},
            {"feedback", "Feedback", "%", 0.0f, 0.95f, 0.35f, 0.35f, 0.01f, false},
            {"tone", "High Cut", "Hz", 500.0f, 16000.0f, 6500.0f, 6500.0f, 50.0f, false},
            {"mix", "Dry/Wet Mix", "%", 0.0f, 1.0f, 0.25f, 0.25f, 0.01f, false},
            {"low_cut", "Low Cut", "Hz", 20.0f, 1000.0f, 80.0f, 80.0f, 5.0f, false},
            {"width", "Stereo Width", "%", 0.0f, 1.0f, 0.85f, 0.85f, 0.05f, false},
            {"sync_en", "Tempo Sync", "", 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, true},
            {"bpm", "Tempo BPM", "BPM", 30.0f, 300.0f, 120.0f, 120.0f, 1.0f, false},
            {"sync_div", "Sync Div", "", 0.0f, 6.0f, 2.0f, 2.0f, 1.0f, false},
            {"link_lr", "Link L/R", "", 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, true}
        };
        catalog_.push_back(m);
    }

    // 5. Auto Feedback Suppressor (AFS)
    {
        PluginMetadata m;
        m.uri = "http://audiocore.org/plugins/afs";
        m.name = "Auto Feedback Suppressor";
        m.category = "AFS";
        m.author = "AudioCore DSP";
        m.description = "Realtime narrowband feedback detector with 8 precision notch filters";
        m.is_live_dsp = true;
        m.prefers_send_return_on_aux = false;
        m.parameters = {
            {"sensitivity", "Sensitivity", "%", 0.0f, 1.0f, 0.75f, 0.75f, 0.05f, false},
            {"max_notches", "Max Notches", "", 1.0f, 16.0f, 8.0f, 8.0f, 1.0f, false},
            {"max_depth", "Notch Depth", "dB", -24.0f, -6.0f, -18.0f, -18.0f, 0.5f, false},
            {"q", "Notch Q", "", 5.0f, 40.0f, 15.0f, 15.0f, 1.0f, false},
            {"attack", "Attack", "ms", 5.0f, 200.0f, 20.0f, 20.0f, 5.0f, false},
            {"hold", "Hold Time", "ms", 100.0f, 10000.0f, 3000.0f, 3000.0f, 100.0f, false},
            {"release", "Release", "ms", 100.0f, 5000.0f, 1000.0f, 1000.0f, 100.0f, false},
            {"clear_seq", "Clear Sequence", "", 0.0f, 1000000.0f, 0.0f, 0.0f, 1.0f, false}
        };
        catalog_.push_back(m);
    }

    // 6. Brickwall Limiter
    {
        PluginMetadata m;
        m.uri = "http://audiocore.org/plugins/limiter";
        m.name = "Brickwall Limiter";
        m.category = "Limiter";
        m.author = "AudioCore DSP";
        // Marketing prose intentionally removed from this processor's presentation;
        // only operational parameter values are surfaced (see rack slot summary).
        m.description = "";
        m.is_live_dsp = true;
        m.prefers_send_return_on_aux = false;
        m.parameters = {
            {"threshold", "Threshold", "dB", -24.0f, 0.0f, -0.5f, -0.5f, 0.1f, false},
            {"ceiling", "Ceiling", "dB", -12.0f, 0.0f, -0.1f, -0.1f, 0.1f, false},
            {"release", "Release", "ms", 5.0f, 500.0f, 50.0f, 50.0f, 1.0f, false},
            {"lookahead", "Lookahead", "ms", 0.0f, 10.0f, 2.0f, 2.0f, 0.1f, false}
        };
        catalog_.push_back(m);
    }

    // 7. Expander / Gate
    {
        PluginMetadata m;
        m.uri = "http://audiocore.org/plugins/gate";
        m.name = "Expander / Gate";
        m.category = "Dynamics";
        m.author = "AudioCore DSP";
        m.description = "Fast downward expander and noise gate with hold timer";
        m.is_live_dsp = true;
        m.prefers_send_return_on_aux = false;
        m.parameters = {
            {"thresh", "Threshold", "dB", -80.0f, 0.0f, -48.0f, -48.0f, 0.5f, false},
            {"range", "Range", "dB", -80.0f, 0.0f, -60.0f, -60.0f, 1.0f, false},
            {"attack", "Attack", "ms", 0.1f, 100.0f, 5.0f, 5.0f, 0.5f, false},
            {"hold", "Hold", "ms", 0.0f, 500.0f, 50.0f, 50.0f, 5.0f, false},
            {"release", "Release", "ms", 10.0f, 1000.0f, 120.0f, 120.0f, 5.0f, false}
        };
        catalog_.push_back(m);
    }

    // 8. Stereo Chorus (non-live catalog entry)
    {
        PluginMetadata m;
        m.uri = "http://audiocore.org/plugins/chorus";
        m.name = "Stereo Chorus";
        m.category = "Modulation";
        m.author = "AudioCore DSP";
        m.description = "Multi-voice chorus with quadrature LFO and stereo spread";
        m.is_live_dsp = false;
        m.parameters = {
            {"rate", "Mod Rate", "Hz", 0.05f, 10.0f, 0.65f, 0.65f, 0.05f, false},
            {"depth", "Mod Depth", "%", 0.0f, 1.0f, 0.45f, 0.45f, 0.01f, false},
            {"predelay", "Voice Delay", "ms", 1.0f, 50.0f, 12.0f, 12.0f, 0.5f, false},
            {"feedback", "Feedback", "%", 0.0f, 0.90f, 0.20f, 0.20f, 0.01f, false},
            {"mix", "Mix", "%", 0.0f, 1.0f, 0.35f, 0.35f, 0.01f, false}
        };
        catalog_.push_back(m);
    }

    // 9. Tube Saturation (non-live catalog entry)
    {
        PluginMetadata m;
        m.uri = "http://audiocore.org/plugins/saturation";
        m.name = "Tube Saturation";
        m.category = "Saturation";
        m.author = "AudioCore DSP";
        m.description = "Triode harmonics and soft clipping warmth with tone tilt";
        m.is_live_dsp = false;
        m.parameters = {
            {"drive", "Drive", "dB", 0.0f, 36.0f, 8.0f, 8.0f, 0.5f, false},
            {"bias", "Bias", "%", -1.0f, 1.0f, 0.0f, 0.0f, 0.05f, false},
            {"warmth", "Warmth", "%", 0.0f, 1.0f, 0.5f, 0.5f, 0.01f, false},
            {"output", "Output Trim", "dB", -18.0f, 12.0f, -4.0f, -4.0f, 0.5f, false},
            {"mix", "Dry/Wet Mix", "%", 0.0f, 1.0f, 0.50f, 0.50f, 0.01f, false}
        };
        catalog_.push_back(m);
    }
}

const PluginMetadata* PluginRegistry::find_metadata(const std::string& uri) const {
    for (const auto& m : catalog_) {
        if (m.uri == uri) return &m;
    }
    // Resilient matching for NAM models across relative directory prefixes
    for (const auto& m : catalog_) {
        if (m.source_type == PluginSourceType::NamLite) {
            std::filesystem::path p1(m.uri);
            std::filesystem::path p2(uri);
            if (p1.filename() == p2.filename() || p1.stem() == p2.stem()) {
                return &m;
            }
        }
    }
    return nullptr;
}

std::shared_ptr<PluginInstance> PluginRegistry::create_instance(const std::string& uri) const {
    const auto* m = find_metadata(uri);
    if (m) {
        return std::make_shared<PluginInstance>(*m);
    }
    return nullptr;
}

void PluginRegistry::refresh_catalog() {
    catalog_.clear();
    initialize_builtins();
    discover_lv2_plugins();
    std::string nam_dir = std::filesystem::exists("assets/nam") ? "assets/nam" : "../../assets/nam";
    discover_nam_models(nam_dir);
}

void PluginRegistry::discover_lv2_plugins() {
    using namespace prismix::lv2;
    Lv2Host::instance().refreshCatalog();
    const auto& discovered = Lv2Host::instance().getDiscoveredPlugins();
    for (const auto& dp : discovered) {
        PluginMetadata m;
        m.uri = dp.metadata.uri;
        m.name = dp.metadata.name.empty() ? dp.metadata.uri : dp.metadata.name;
        m.category = "LV2 / " + (dp.metadata.category.empty() ? std::string("Effect") : dp.metadata.category);
        m.author = dp.metadata.author;
        m.description = dp.metadata.description.empty() ? "Native LV2 plugin hosted from bundle" : dp.metadata.description;
        m.source_type = PluginSourceType::Lv2;
        m.is_live_dsp = dp.is_compatible;
        m.supports_mono = (dp.metadata.audio_inputs >= 1);
        m.supports_stereo = true;
        m.audio_inputs = dp.metadata.audio_inputs;
        m.audio_outputs = dp.metadata.audio_outputs;
        m.meets_realtime_budget = true;
        m.validation_error = dp.incompatibility_reason;

        for (const auto& port : dp.metadata.ports) {
            if (port.type == PortType::Control && port.direction == PortDirection::Input) {
                PluginParameter p;
                p.id = port.symbol;
                p.name = port.name;
                p.unit = port.unit;
                p.min_val = port.min_value;
                p.max_val = port.max_value;
                p.default_val = port.default_value;
                p.current_val = port.default_value;
                p.is_toggle = port.is_toggled;
                m.parameters.push_back(p);
            }
        }
        catalog_.push_back(m);
    }
}

void PluginRegistry::discover_nam_models(const std::string& directory) {
    using namespace prismix::nam;
    std::error_code ec;
    std::filesystem::path dir(directory);
    if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) return;

    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() == ".nam") {
            auto model = NamModel::loadFromFile(entry.path().string());
            if (model) {
                PluginMetadata m;
                m.uri = "nam:" + entry.path().string();
                m.name = model->metadata().name;
                m.category = "NAM / Amp Model";
                m.author = "Neural Amp Modeler";
                m.description = (model->architecture() == Architecture::Linear)
                    ? "Linear FIR Cabinet/Tone IR"
                    : "Lite Fast LSTM Neural Amp Model";
                m.source_type = PluginSourceType::NamLite;
                m.is_live_dsp = model->isValid() && model->metadata().meets_realtime_budget;
                m.supports_mono = true;
                m.supports_stereo = true;
                m.model_file_path = entry.path().string();
                m.estimated_mflops = model->metadata().estimated_mflops;
                m.meets_realtime_budget = model->metadata().meets_realtime_budget;
                m.validation_error = model->metadata().validation_error;
                m.parameters = {
                    {"in_gain", "Input Gain", "dB", -24.0f, 24.0f, 0.0f, 0.0f, 0.5f, false},
                    {"out_level", "Output Level", "dB", -24.0f, 24.0f, 0.0f, 0.0f, 0.5f, false}
                };
                catalog_.push_back(m);
            }
        }
    }
}

