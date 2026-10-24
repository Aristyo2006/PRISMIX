#pragma once
#include <array>
#include <vector>
#include <cstdint>
#include <string>
#include <memory>
#include "plugin_model.h"
#include "../dsp/geq_model.h"

constexpr int MIN_CHANNELS = 0;
constexpr int MAX_CHANNELS = 32;

// Sentinel for per-slot dynamics telemetry that has not been measured yet
// (engine stopped / slot never processed). Never displayed as a live value.
constexpr uint8_t GATE_STATE_UNAVAILABLE = 0xFF;

// Per-slot Brickwall Limiter gain-reduction telemetry (dB, <= 0) is only valid
// while a realtime engine is processing the slot; +1.0 dB cannot occur as a real
// measurement and marks the value as unavailable (engine stopped).
constexpr float LIMITER_GR_UNAVAILABLE = 1.0f;

enum class ChannelOutputRoute : uint8_t { None = 0, MainLR = 1, Aux = 2, Omni = 3 };
enum class ChannelFormat : uint8_t { Mono = 0, Stereo = 1 };
enum class ChannelType : uint8_t { Input = 0, Aux = 1, Group = 2, Master = 3 };

// DCA Control Group Architecture (Extension point for control grouping)
struct DcaGroup {
    int id = 1;
    char name[32] = "DCA 01";
    float fader = 0.86f; // 0 dB unity at 0.86
    bool mute = false;
    uint32_t channel_mask = 0; // Bitmask of assigned channel IDs/indices
    uint8_t assigned_count = 0;
};

// Group Bus Architecture (Extension point for subgroup summing & processing)
struct GroupBus {
    int id = 1;
    char name[32] = "GROUP 01";
    ChannelFormat format = ChannelFormat::Stereo;
    float fader = 0.86f;
    float pan = 0.5f;
    bool mute = false;
    bool solo = false;
    bool route_to_main = true;
    float peak_l = -90.0f, peak_r = -90.0f;
    float rms_l = -90.0f, rms_r = -90.0f;
    uint32_t member_mask = 0;
};

struct ChannelState {
    int id = 1;
    ChannelType type = ChannelType::Input;
    ChannelFormat format = ChannelFormat::Mono;
    float fader = 0.86f; // 0..1, logarithmic fader law; 0 dB at 0.86
    float return_gain = 0.86f; // 0..1, return level for AUX buses into Main L/R
    float pan = 0.5f;    // 0..1, center at 0.5 (Pan for Mono, Balance for Stereo)
    float gain = 0.5f;   // 0..1, 0 dB at 0.5
    bool mute = false, solo = false, select = false, clip = false;
    bool route_to_main = true; // Whether bus/channel routes into Main L/R
    // Channel Input Meter (Pre-DSP: measured at physical/buffer entry before HPF, Gate, EQ, Comp, Limiter)
    float input_peak_l = -90.0f, input_peak_r = -90.0f;
    float input_rms_l = -90.0f, input_rms_r = -90.0f;
    float input_peak_hold_l = -90.0f, input_peak_hold_r = -90.0f;
    bool input_clip = false;

    // Channel Output Meter (Post-DSP, Pre-Fader: measured after HPF, Gate, EQ, Comp, Limiter)
    float peak_l = -90.0f, peak_r = -90.0f, rms_l = -90.0f, rms_r = -90.0f;
    float peak_hold_l = -90.0f, peak_hold_r = -90.0f;
    float post_fader_peak_l = -90.0f, post_fader_peak_r = -90.0f;

    // FX Rack Dedicated Meter Taps (PRE-FX INPUT immediately before fxSlots[0..7],
    // POST-FX OUTPUT immediately after fxSlots[0..7], before Fader/Limiter)
    float fx_in_peak_l = -90.0f, fx_in_peak_r = -90.0f;
    float fx_in_rms_l = -90.0f, fx_in_rms_r = -90.0f;
    float fx_in_peak_hold_l = -90.0f, fx_in_peak_hold_r = -90.0f;
    bool fx_in_clip = false;
    float fx_out_peak_l = -90.0f, fx_out_peak_r = -90.0f;
    float fx_out_rms_l = -90.0f, fx_out_rms_r = -90.0f;
    float fx_out_peak_hold_l = -90.0f, fx_out_peak_hold_r = -90.0f;
    bool fx_out_clip = false;

    char name[32] = "INPUT";
    char input_label[32] = "IN 1";
    int input_source_index = -1;
    int input_source_index_r = -1; // Right channel physical source for Stereo channels (-1 for Mono)
    ChannelOutputRoute output_route = ChannelOutputRoute::MainLR;
    int output_route_index = 0;

    // Parametric EQ
    std::array<float, 4> eq_gain{0.0f, 0.0f, 0.0f, 0.0f};
    std::array<float, 4> eq_freq{80.0f, 350.0f, 2500.0f, 10000.0f};
    std::array<float, 4> eq_q{0.7f, 1.0f, 1.0f, 0.7f};
    std::array<bool, 4> eq_enabled{true, true, true, true};
    std::array<bool, 4> eq_shelf{true, false, false, true};
    std::array<int, 4> eq_filter_type{0, 1, 1, 2}; // 0:Low Shelf, 1:Bell, 2:High Shelf, 3:HPF, 4:LPF
    bool eq_bypass = false;

    // Graphic EQ
    GraphicEqModel geq;
    bool geq_mode_active = false;
    bool geq_bypass = false;

    // Compressor
    float comp_thresh = -18.0f, comp_ratio = 4.0f;
    float comp_attack = 10.0f, comp_release = 80.0f, comp_knee = 6.0f, comp_makeup = 0.0f;
    float comp_gr = 0.0f;
    bool comp_bypass = false;
    float comp_input = -90.0f, comp_output = -90.0f, comp_output_r = -90.0f;
    float comp_output_peak_hold_l = -90.0f, comp_output_peak_hold_r = -90.0f;
    float comp_output_rms_l = -90.0f, comp_output_rms_r = -90.0f;
    float comp_curve_output = -90.0f; // Exact transfer curve output coordinate for live marker

    // Brickwall Limiter (Master, AUX, or Channel protection)
    bool limiter_enabled = false;
    float limiter_ceiling_db = -0.1f;
    float limiter_release_ms = 50.0f;
    float limiter_gr_db = 0.0f;
    float pre_limiter_peak_l = -90.0f, pre_limiter_peak_r = -90.0f;

    // Gate
    bool gate_on = true, gate_open = true;
    int gate_state = 0; // 0: Closed, 1: Attack, 2: Open, 3: Hold, 4: Release
    float gate_thresh = -48.0f, gate_attack = 5.0f, gate_hold = 50.0f, gate_release = 120.0f, gate_range = -60.0f;
    float gate_timer_ms = 0.0f;
    int gate_key_source = 0; // 0: Self, 1..8: Ch 1..8, 9: Aux
    float gate_gr_db = 0.0f;
    float gate_detector_db = -90.0f;
    float gate_curve_output = -90.0f; // Exact gate curve output coordinate for live marker

    // Sidechain
    float sidechain_low = 80.0f, sidechain_high = 8000.0f;
    bool sidechain_on = false;

    // Preamp / routing
    bool polarity_inverted = false, hpf_on = true, phantom_on = false;
    float hpf_frequency = 80.0f;
    std::array<bool, 8> bus_assign{};
    std::array<float, 4> sends{0.0f, 0.0f, 0.0f, 0.0f};
    std::array<bool, 4> send_enabled{true, true, true, true};
    std::array<bool, 4> send_pre_fader{false, false, false, false};

    // Speaker Delay / Output Alignment (Dedicated Master Output Processing)
    bool speaker_delay_enabled = false;
    bool speaker_delay_link = true;
    float speaker_delay_l_ms = 0.0f; // 0.0 ms to 100.0 ms (up to 500.0 ms max)
    float speaker_delay_r_ms = 0.0f;

    // Automatic Feedback Suppressor (AFS) live telemetry
    struct AfsNotchUiState {
        uint8_t id = 0;
        float frequency_hz = 1000.0f;
        float depth_db = 0.0f;
        float q = 15.0f;
        uint8_t state = 0; // 0: Inactive, 1: Candidate, 2: Active, 3: Holding, 4: Releasing
    };
    bool afs_assigned = false;
    uint8_t afs_status = 0; // 0: DISABLED, 1: BYPASS, 2: MONITORING, 3: CANDIDATE, 4: ACTIVE
    uint8_t afs_active_notch_count = 0;
    uint8_t afs_max_notches = 8;
    std::array<AfsNotchUiState, 8> afs_notches{};

    // FX Rack per-slot gain reduction telemetry (dB, <= 0), indexed by rack slot
    std::array<float, 8> fx_slot_gr_db{};

    // FX Rack per-slot Expander / Gate telemetry (real engine measurements).
    // State code: 0 Closed, 1 Attack, 2 Open, 3 Hold, 4 Release, 255 = unavailable.
    std::array<float, 8> fx_slot_gate_gr_db{};
    std::array<float, 8> fx_slot_gate_detector_db{};
    std::array<uint8_t, 8> fx_slot_gate_state = {
        GATE_STATE_UNAVAILABLE, GATE_STATE_UNAVAILABLE, GATE_STATE_UNAVAILABLE, GATE_STATE_UNAVAILABLE,
        GATE_STATE_UNAVAILABLE, GATE_STATE_UNAVAILABLE, GATE_STATE_UNAVAILABLE, GATE_STATE_UNAVAILABLE};

    // FX Rack per-slot Brickwall Limiter gain reduction (real engine measurements).
    std::array<float, 8> fx_slot_limiter_gr_db = {
        LIMITER_GR_UNAVAILABLE, LIMITER_GR_UNAVAILABLE, LIMITER_GR_UNAVAILABLE, LIMITER_GR_UNAVAILABLE,
        LIMITER_GR_UNAVAILABLE, LIMITER_GR_UNAVAILABLE, LIMITER_GR_UNAVAILABLE, LIMITER_GR_UNAVAILABLE};

    // DCA & Group Bus assignment metadata (architecture extension points)
    uint8_t dca_assign_mask = 0;   // Bitmask of assigned DCA groups (DCA 1..8)
    int group_bus_assign = -1;     // Assigned group bus index (-1: direct Main L/R)

    // Effects rack
    PluginRack fx_rack;
};

class ChannelContainer {
    std::vector<std::unique_ptr<ChannelState>> items_;
public:
    struct Iterator {
        using iterator_category = std::random_access_iterator_tag;
        using value_type = ChannelState;
        using difference_type = std::ptrdiff_t;
        using pointer = ChannelState*;
        using reference = ChannelState&;

        std::vector<std::unique_ptr<ChannelState>>::iterator it_;
        Iterator() = default;
        Iterator(std::vector<std::unique_ptr<ChannelState>>::iterator it) : it_(it) {}

        ChannelState& operator*() const { return **it_; }
        ChannelState* operator->() const { return it_->get(); }
        Iterator& operator++() { ++it_; return *this; }
        Iterator operator++(int) { Iterator tmp = *this; ++it_; return tmp; }
        Iterator& operator--() { --it_; return *this; }
        Iterator operator--(int) { Iterator tmp = *this; --it_; return tmp; }
        Iterator operator+(difference_type n) const { return Iterator(it_ + n); }
        Iterator operator-(difference_type n) const { return Iterator(it_ - n); }
        difference_type operator-(const Iterator& other) const { return it_ - other.it_; }
        bool operator==(const Iterator& other) const { return it_ == other.it_; }
        bool operator!=(const Iterator& other) const { return it_ != other.it_; }
    };

    struct ConstIterator {
        using iterator_category = std::random_access_iterator_tag;
        using value_type = const ChannelState;
        using difference_type = std::ptrdiff_t;
        using pointer = const ChannelState*;
        using reference = const ChannelState&;

        std::vector<std::unique_ptr<ChannelState>>::const_iterator it_;
        ConstIterator() = default;
        ConstIterator(std::vector<std::unique_ptr<ChannelState>>::const_iterator it) : it_(it) {}

        const ChannelState& operator*() const { return **it_; }
        const ChannelState* operator->() const { return it_->get(); }
        ConstIterator& operator++() { ++it_; return *this; }
        ConstIterator operator++(int) { ConstIterator tmp = *this; ++it_; return tmp; }
        ConstIterator& operator--() { --it_; return *this; }
        ConstIterator operator--(int) { ConstIterator tmp = *this; --it_; return tmp; }
        ConstIterator operator+(difference_type n) const { return ConstIterator(it_ + n); }
        ConstIterator operator-(difference_type n) const { return ConstIterator(it_ - n); }
        difference_type operator-(const ConstIterator& other) const { return it_ - other.it_; }
        bool operator==(const ConstIterator& other) const { return it_ == other.it_; }
        bool operator!=(const ConstIterator& other) const { return it_ != other.it_; }
    };

    size_t size() const noexcept { return items_.size(); }
    bool empty() const noexcept { return items_.empty(); }
    void clear() { items_.clear(); }
    void reserve(size_t n) { items_.reserve(n); }
    ChannelState& front() { return *items_.front(); }
    const ChannelState& front() const { return *items_.front(); }
    ChannelState& back() { return *items_.back(); }
    const ChannelState& back() const { return *items_.back(); }

    ChannelState& operator[](size_t idx) { return *items_[idx]; }
    const ChannelState& operator[](size_t idx) const { return *items_[idx]; }
    ChannelState* get(size_t idx) { return idx < items_.size() ? items_[idx].get() : nullptr; }
    const ChannelState* get(size_t idx) const { return idx < items_.size() ? items_[idx].get() : nullptr; }

    void push_back(const ChannelState& ch) {
        items_.push_back(std::make_unique<ChannelState>(ch));
    }
    void pop_back() {
        if (!items_.empty()) items_.pop_back();
    }
    Iterator erase(Iterator it) {
        return Iterator(items_.erase(it.it_));
    }

    Iterator begin() { return Iterator(items_.begin()); }
    Iterator end() { return Iterator(items_.end()); }
    ConstIterator begin() const { return ConstIterator(items_.begin()); }
    ConstIterator end() const { return ConstIterator(items_.end()); }
};

struct MixerState {
    ChannelContainer channels;
    std::array<ChannelState, 4> aux_buses{};
    ChannelState master{};
    std::array<DcaGroup, 8> dca_groups{};
    std::array<GroupBus, 4> group_buses{};
    int selected_channel = -1; // 0..N-1 for input channels, -1 for master
    int selected_aux_bus = -1; // 0..3 when an AUX bus is actively inspected, -1 otherwise
};

extern MixerState g_mixer_state;
void mixer_state_init();

// Safe channel resolution helpers
ChannelState* mixer_get_channel(int index);
ChannelState* mixer_get_aux_bus(int aux_index);
ChannelState* mixer_find_channel_by_id(int id);
ChannelState* mixer_get_selected_channel();

// Channel capability descriptor
struct ChannelCapabilities {
    bool has_preamp = false;         // Physical analog preamp (gain, 48V, polarity)
    bool has_hpf = false;            // Channel HPF
    bool has_gate = false;           // Noise gate
    bool has_peq = false;            // Parametric EQ
    bool has_geq = false;            // Graphic EQ
    bool has_comp = false;           // Compressor
    bool has_limiter = false;        // Brickwall limiter
    bool has_fx_rack = false;        // Insert FX rack
    bool has_aux_sends = false;      // Sends to AUX 1..4
    bool has_bus_assign = false;     // Assign to Groups/Buses
    bool has_speaker_delay = false;  // Output alignment delay
    bool is_master = false;
    bool is_aux = false;
    bool is_stereo = false;
};

// Safe channel type inspectors
inline bool isMasterChannel(const ChannelState* ch) {
    if (!ch) return false;
    return ch->type == ChannelType::Master || ch->id == 0 || ch->id == 100 || ch == &g_mixer_state.master;
}

inline bool isAuxBusChannel(const ChannelState* ch) {
    if (!ch) return false;
    return ch->type == ChannelType::Aux || (ch->id >= 101 && ch->id <= 104);
}

inline bool isInputChannel(const ChannelState* ch) {
    if (!ch) return false;
    return !isMasterChannel(ch) && !isAuxBusChannel(ch);
}

inline ChannelCapabilities mixer_get_channel_capabilities(const ChannelState* ch) {
    ChannelCapabilities caps{};
    if (!ch) return caps;

    if (isMasterChannel(ch)) {
        caps.is_master = true;
        caps.is_stereo = true;
        caps.has_peq = true;
        caps.has_geq = true;
        caps.has_comp = true;
        caps.has_limiter = true;
        caps.has_fx_rack = false;
        caps.has_gate = false;
        caps.has_speaker_delay = true;
    } else if (isAuxBusChannel(ch)) {
        caps.is_aux = true;
        caps.is_stereo = true;
        caps.has_gate = true;
        caps.has_peq = true;
        caps.has_geq = true;
        caps.has_comp = true;
        caps.has_limiter = true;
        caps.has_fx_rack = true;
    } else {
        caps.is_stereo = (ch->format == ChannelFormat::Stereo);
        caps.has_preamp = true;
        caps.has_hpf = true;
        caps.has_gate = true;
        caps.has_peq = true;
        caps.has_geq = true;
        caps.has_comp = true;
        caps.has_limiter = true;
        caps.has_fx_rack = true;
        caps.has_aux_sends = true;
        caps.has_bus_assign = true;
    }
    return caps;
}

