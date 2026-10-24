#include "fx_screen.h"
#include "../theme/theme.h"
#include "../widgets/instrument.h"
#include "../assets/rack_textures.h"
#include "../ui_audio_bridge.h"
#include "../../model/mixer_state.h"
#include "../../model/plugin_model.h"
#include "../../model/pedalboard_preset.h"
#include "../../service/audio_service.h"
#include "../../dsp/frequency_axis.h"
#include "audiocore/reverb_node.hpp"
#include "audiocore/delay_node.hpp"
#include "../ui.h"
#include "../../service/patchstorage/patchstorage_service.h"
#include "../../service/patchstorage/patchstorage_types.h"
#include "../../service/patchstorage/patchstorage_client.h"
#include "../../service/patchstorage/patchstorage_presentation.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// Rolling history depth for the Expander / Gate live monitor strip chart
// (96 ticks @ 30 ms timer cadence => ~2.9 s measurement window)
constexpr int kGateHistoryPoints = 96;

struct FxScreenView {
    ChannelState* ch = nullptr;
    int target_mode = 0; // 0: Input Channel, 1..4: AUX 01..04
    int target_channel_id = 1;
    int selected_slot = 0;

    lv_obj_t* header_title_label = nullptr;
    lv_obj_t* rack_panel = nullptr;
    lv_obj_t* editor_panel = nullptr;
    lv_obj_t* slot_container = nullptr;
    lv_obj_t* editor_content = nullptr;
    lv_obj_t* add_modal = nullptr;
    lv_obj_t* target_btns[5]{};
    lv_obj_t* prev_ch_btn = nullptr;
    lv_obj_t* next_ch_btn = nullptr;

    // Live editor telemetry widgets (invalidated/updated by live_timer without DOM rebuild)
    lv_obj_t* visual_canvas = nullptr;
    lv_obj_t* io_meter_canvas = nullptr;
    lv_obj_t* io_readout_lbl = nullptr;

    // AFS live telemetry widgets
    lv_obj_t* afs_status_badge = nullptr;
    lv_obj_t* afs_status_label = nullptr;
    lv_obj_t* afs_count_label = nullptr;
    lv_obj_t* afs_notch_freq_lbl[8]{};
    lv_obj_t* afs_notch_depth_lbl[8]{};
    lv_obj_t* afs_notch_state_lbl[8]{};

    // Studio Compressor live telemetry & controls (Vintage VU Meter)
    lv_obj_t* comp_vu_canvas = nullptr;
    lv_obj_t* comp_gr_readout_lbl = nullptr;
    lv_obj_t* comp_curve_canvas = nullptr;
    lv_obj_t* comp_ratio_btns[6]{};
    float comp_needle_gr_db = 0.0f;
    std::string reverb_last_param = "decay";

    // Expander / Gate live telemetry (real per-slot engine measurements)
    lv_obj_t* gate_curve_canvas = nullptr;
    lv_obj_t* gate_monitor_canvas = nullptr;
    lv_obj_t* gate_state_led = nullptr;
    lv_obj_t* gate_state_lbl = nullptr;
    lv_obj_t* gate_level_lbl = nullptr;
    lv_obj_t* gate_curve_lbl = nullptr;
    float gate_hist_det_db[kGateHistoryPoints]{};
    float gate_hist_out_db[kGateHistoryPoints]{};
    uint8_t gate_hist_state[kGateHistoryPoints]{};
    int gate_hist_head = 0;
    int gate_hist_count = 0;

    // Brickwall Limiter vintage faceplate (real per-slot GR + rack I/O telemetry)
    lv_obj_t* limiter_in_canvas = nullptr;
    lv_obj_t* limiter_gr_canvas = nullptr;
    lv_obj_t* limiter_in_lbl = nullptr;
    lv_obj_t* limiter_out_lbl = nullptr;
    lv_obj_t* limiter_gr_lbl = nullptr;
    float limiter_needle_in_db = -90.0f;
    float limiter_needle_gr_db = 0.0f;

    // Deferred UI rebuild flags (executed safely via lv_async_call outside click handlers)
    bool async_scheduled = false;
    bool pending_close_modal = false;
    bool pending_rebuild_rack = false;
    bool pending_rebuild_editor = false;

    lv_timer_t* live_timer = nullptr;
    std::string last_param_fingerprint;
    std::string last_structure_signature;
};

void build_editor(FxScreenView* v);
void build_rack(FxScreenView* v);
void update_target_buttons(FxScreenView* v);
void schedule_ui_rebuild(FxScreenView* v, bool rebuild_rack, bool rebuild_editor, bool close_modal = false);

ChannelState* resolve_target_channel(FxScreenView* v) {
    if (!v) return nullptr;
    ChannelState* resolved = nullptr;
    if (v->target_mode >= 1 && v->target_mode <= 4) {
        resolved = &g_mixer_state.aux_buses[static_cast<size_t>(v->target_mode - 1)];
    } else {
        v->target_mode = 0;
        if (g_mixer_state.selected_channel >= 0) {
            if (auto* sel = mixer_get_channel(g_mixer_state.selected_channel)) {
                v->target_channel_id = sel->id;
            }
        }
        resolved = mixer_find_channel_by_id(v->target_channel_id);
        if (!resolved || resolved == &g_mixer_state.master || isAuxBusChannel(resolved)) {
            resolved = mixer_get_channel(g_mixer_state.selected_channel >= 0 ? g_mixer_state.selected_channel : 0);
        }
        if (!resolved && !g_mixer_state.channels.empty()) {
            resolved = mixer_get_channel(0);
        }
        if (!resolved && !g_mixer_state.aux_buses.empty()) {
            resolved = &g_mixer_state.aux_buses[0];
            v->target_mode = 1;
        }
        if (resolved) {
            v->target_channel_id = resolved->id;
        }
    }
    if (resolved && resolved != &g_mixer_state.master) {
        resolved->fx_rack.set_owner_context(
            resolved->id, isAuxBusChannel(resolved), resolved->format == ChannelFormat::Stereo);
    }
    v->ch = resolved;
    return resolved;
}

void sync_channel_fx_to_dsp(ChannelState* ch) {
    if (!ch || ch == &g_mixer_state.master) return;
    ch->fx_rack.set_owner_context(ch->id, isAuxBusChannel(ch), ch->format == ChannelFormat::Stereo);
    auto cfg = ui_make_strip_config_for_channel(*ch);
    if (isAuxBusChannel(ch)) {
        cfg.routeToMain = ch->route_to_main;
        audio_service().updateLiveAuxBusDsp(static_cast<size_t>(ch->id - 101), cfg);
    } else {
        audio_service().updateLiveStripDsp(cfg);
    }
}

std::string compute_structure_signature(const ChannelState* ch) {
    if (!ch) return "";
    std::string sig = std::to_string(ch->id) + ":" + std::to_string(static_cast<int>(ch->format)) + ":" +
                      std::to_string(ch->fx_rack.count()) + "|";
    for (size_t s = 0; s < ch->fx_rack.count(); ++s) {
        if (const auto* inst = ch->fx_rack.get_slot(s)) {
            sig += std::to_string(inst->get_instance_id()) + ":" + inst->get_uri() + ";";
        }
    }
    return sig;
}

std::string compute_rack_fingerprint(const ChannelState* ch) {
    if (!ch) return "";
    std::string fp = std::to_string(ch->id) + ":" + std::to_string(ch->fx_rack.count()) + "|";
    for (size_t s = 0; s < ch->fx_rack.count(); ++s) {
        const auto* inst = ch->fx_rack.get_slot(s);
        if (!inst) continue;
        fp += inst->get_uri() + ":" + std::to_string(inst->is_bypassed()) + ":";
        for (size_t p = 0; p < inst->parameter_count(); ++p) {
            fp += std::to_string(inst->get_parameter_value(p)) + ",";
        }
        fp += ";";
    }
    return fp;
}

void reset_live_editor_pointers(FxScreenView* v) {
    if (!v) return;
    v->visual_canvas = nullptr;
    v->io_meter_canvas = nullptr;
    v->io_readout_lbl = nullptr;
    v->afs_status_badge = nullptr;
    v->afs_status_label = nullptr;
    v->afs_count_label = nullptr;
    for (int i = 0; i < 8; ++i) {
        v->afs_notch_freq_lbl[i] = nullptr;
        v->afs_notch_depth_lbl[i] = nullptr;
        v->afs_notch_state_lbl[i] = nullptr;
    }
    v->comp_vu_canvas = nullptr;
    v->comp_gr_readout_lbl = nullptr;
    v->comp_curve_canvas = nullptr;
    for (int i = 0; i < 6; ++i) {
        v->comp_ratio_btns[i] = nullptr;
    }
    v->comp_needle_gr_db = 0.0f;
    v->gate_curve_canvas = nullptr;
    v->gate_monitor_canvas = nullptr;
    v->gate_state_led = nullptr;
    v->gate_state_lbl = nullptr;
    v->gate_level_lbl = nullptr;
    v->gate_curve_lbl = nullptr;
    v->gate_hist_head = 0;
    v->gate_hist_count = 0;
    v->limiter_in_canvas = nullptr;
    v->limiter_gr_canvas = nullptr;
    v->limiter_in_lbl = nullptr;
    v->limiter_out_lbl = nullptr;
    v->limiter_gr_lbl = nullptr;
    v->limiter_needle_in_db = -90.0f;
    v->limiter_needle_gr_db = 0.0f;
}

void deferred_rebuild_cb(void* user_data) {
    auto* v = static_cast<FxScreenView*>(user_data);
    if (!v) return;
    v->async_scheduled = false;

    if (v->pending_close_modal) {
        v->pending_close_modal = false;
        if (v->add_modal) {
            lv_obj_t* modal = v->add_modal;
            v->add_modal = nullptr;
            lv_obj_delete(modal);
        }
    }
    if (v->pending_rebuild_rack) {
        v->pending_rebuild_rack = false;
        build_rack(v);
    }
    if (v->pending_rebuild_editor) {
        v->pending_rebuild_editor = false;
        build_editor(v);
    }
}

void schedule_ui_rebuild(FxScreenView* v, bool rebuild_rack, bool rebuild_editor, bool close_modal) {
    if (!v) return;
    if (close_modal) v->pending_close_modal = true;
    if (rebuild_rack) v->pending_rebuild_rack = true;
    if (rebuild_editor) v->pending_rebuild_editor = true;
    if (!v->async_scheduled) {
        v->async_scheduled = true;
        lv_async_call(deferred_rebuild_cb, v);
    }
}

void update_target_buttons(FxScreenView* v) {
    ChannelState* ch = resolve_target_channel(v);
    if (!ch) return;

    int activeIdx = v->target_mode;
    for (int i = 0; i < 5; ++i) {
        if (!v->target_btns[i]) continue;
        const bool sel = (i == activeIdx);
        ui::rack_skin::apply_hardware_button(
            v->target_btns[i], sel, sel ? COLOR_SELECTED_BORDER : COLOR_STANDARD_BORDER,
            sel ? COLOR_RAISED_PANEL : COLOR_CONTROL, COLOR_CONTROL);
        if (lv_obj_get_child_count(v->target_btns[i]) > 0) {
            lv_obj_t* lbl = lv_obj_get_child(v->target_btns[i], 0);
            lv_obj_set_style_text_color(lbl, sel ? COLOR_PRIMARY_TEXT : COLOR_SECONDARY_TEXT, 0);
            if (i == 0) {
                if (!g_mixer_state.channels.empty() && v->target_mode == 0) {
                    lv_label_set_text_fmt(lbl, "CH %02d", ch->id);
                } else {
                    lv_label_set_text(lbl, "INPUT CH");
                }
            }
        }
    }
    if (v->header_title_label) {
        char title_buf[180];
        const char* mode_tag = isAuxBusChannel(ch)
            ? "AUX SEND/RETURN EFX BUS"
            : "CHANNEL INSERT RACK";
        std::snprintf(title_buf, sizeof(title_buf), "FX RACK:  %s  [%s  |  %s]   (%u / %u SLOTS)",
                      ch->name,
                      ch->format == ChannelFormat::Stereo ? "STEREO" : "MONO",
                      mode_tag,
                      static_cast<unsigned>(ch->fx_rack.count()),
                      static_cast<unsigned>(PluginRack::MAX_SLOTS));
        lv_label_set_text(v->header_title_label, title_buf);
    }
}

void populate_add_modal_list(lv_obj_t* list, FxScreenView* v, int tab) {
    lv_obj_clean(list);
    ChannelState* ch = resolve_target_channel(v);
    if (!v || !ch) return;

    int row = 0;
    lv_obj_update_layout(list);
    int w_avail = lv_obj_get_content_width(list);
    if (w_avail <= 0) w_avail = lv_obj_get_width(list) - 12;
    if (w_avail <= 0) w_avail = 920;
    const int item_w = std::max(240, w_avail - 8);

    // Pedalboard Presets (shown in PRESETS tab or ALL tab)
    if (tab == 4 || tab == 0) {
        const auto& presets = prismix::pedalboard::PedalboardPresetManager::instance().getPresets();
        for (const auto& preset : presets) {
            auto* btn = lv_button_create(list);
            lv_obj_set_size(btn, item_w, 46);
            lv_obj_set_pos(btn, 4, row++ * 50);
            ui::rack_skin::apply_hardware_button(btn, false, COLOR_GREEN, lv_color_hex(0x153522), lv_color_hex(0x1B232A));

            char btn_txt[200];
            std::snprintf(btn_txt, sizeof(btn_txt), "[PRESET]  %s   -   %s (%zu units)",
                          preset.name.c_str(), preset.description.c_str(), preset.slots.size());
            auto* l = lv_label_create(btn);
            lv_label_set_text(l, btn_txt);
            lv_obj_align(l, LV_ALIGN_LEFT_MID, 12, 0);
            lv_obj_set_style_text_font(l, &ac_font_14, 0);
            lv_obj_set_style_text_color(l, COLOR_GREEN, 0);

            struct PresetApplyCtx { FxScreenView* v; prismix::pedalboard::PedalboardPreset p; };
            auto* pac = new PresetApplyCtx{v, preset};
            lv_obj_add_event_cb(btn, [](lv_event_t* e) {
                auto* c = static_cast<PresetApplyCtx*>(lv_event_get_user_data(e));
                if (!c || !c->v) return;
                ChannelState* target = resolve_target_channel(c->v);
                if (target) {
                    prismix::pedalboard::PedalboardPresetManager::instance().applyPresetToRack(c->p, target->fx_rack);
                    c->v->selected_slot = 0;
                    sync_channel_fx_to_dsp(target);
                    c->v->last_param_fingerprint = compute_rack_fingerprint(target);
                }
                schedule_ui_rebuild(c->v, true, true, true);
            }, LV_EVENT_CLICKED, pac);

            lv_obj_add_event_cb(btn, [](lv_event_t* e) {
                delete static_cast<PresetApplyCtx*>(lv_event_get_user_data(e));
            }, LV_EVENT_DELETE, pac);
        }
    }

    // Plugins & Models
    if (tab != 4) {
        const auto& catalog = PluginRegistry::instance().get_catalog();
        for (const auto& meta : catalog) {
            if (tab == 1 && meta.source_type != PluginSourceType::Builtin) continue;
            if (tab == 2 && meta.source_type != PluginSourceType::Lv2) continue;
            if (tab == 3 && meta.source_type != PluginSourceType::NamLite) continue;
            if (!meta.is_live_dsp && tab != 0) continue;

            auto* btn = lv_button_create(list);
            lv_obj_set_size(btn, item_w, 46);
            lv_obj_set_pos(btn, 4, row++ * 50);

            lv_color_t accent = (meta.source_type == PluginSourceType::NamLite) ? COLOR_CYAN :
                                ((meta.source_type == PluginSourceType::Lv2) ? COLOR_CYAN : COLOR_NEUTRAL_HIGHLIGHT);
            lv_color_t bg = COLOR_CONTROL;

            ui::rack_skin::apply_hardware_button(btn, false, accent, bg, COLOR_PANEL_SURFACE);

            const char* type_tag = (meta.source_type == PluginSourceType::NamLite) ? "NAM" :
                                   ((meta.source_type == PluginSourceType::Lv2) ? "LV2" : "DSP");

            char btn_txt[220];
            if (meta.description.empty()) {
                std::snprintf(btn_txt, sizeof(btn_txt), "[%s]  %s", type_tag, meta.name.c_str());
            } else {
                std::snprintf(btn_txt, sizeof(btn_txt), "[%s]  %s   -   %s",
                              type_tag, meta.name.c_str(), meta.description.c_str());
            }
            auto* l = lv_label_create(btn);
            lv_label_set_text(l, btn_txt);
            lv_obj_align(l, LV_ALIGN_LEFT_MID, 12, 0);
            lv_obj_set_style_text_font(l, &ac_font_14, 0);
            lv_obj_set_style_text_color(l, accent, 0);

            if (!meta.meets_realtime_budget) {
                lv_obj_add_state(btn, LV_STATE_DISABLED);
                lv_obj_set_style_text_color(l, COLOR_TEXT2, 0);
            }

            struct AddCtx { FxScreenView* v; std::string uri; };
            auto* ac = new AddCtx{v, meta.uri};
            lv_obj_add_event_cb(btn, [](lv_event_t* e) {
                auto* c = static_cast<AddCtx*>(lv_event_get_user_data(e));
                if (!c || !c->v) return;
                FxScreenView* view = c->v;
                ChannelState* target_ch = resolve_target_channel(view);
                if (target_ch && target_ch->fx_rack.add_plugin(c->uri)) {
                    view->selected_slot = static_cast<int>(target_ch->fx_rack.count()) - 1;
                    sync_channel_fx_to_dsp(target_ch);
                    view->last_param_fingerprint = compute_rack_fingerprint(target_ch);
                }
                schedule_ui_rebuild(view, true, true, true);
            }, LV_EVENT_CLICKED, ac);

            lv_obj_add_event_cb(btn, [](lv_event_t* e) {
                delete static_cast<AddCtx*>(lv_event_get_user_data(e));
            }, LV_EVENT_DELETE, ac);
        }
    }
}

// ============================================================================
// PatchStorage LV2 browser - tab 5 of the Add-Plugin modal.
//
// Marketplace style card grid over the live PatchStorage beta API:
//   - responsive 3 / 2 / 1 column layout computed from the measured container
//     width so nothing is ever clipped at any dialog size;
//   - per-card artwork (worker-decoded PNGs from artwork.thumbnail_url) with
//     locally drawn category placeholder glyphs when art is missing or fails;
//   - prominent search plus verified filters (Sort / Target / Category) that
//     compose with each other and update results, count and pagination;
//   - a client-side State filter over the current page (installed / not
//     installed / host compatible / unsupported on host);
//   - details overlay showing only data the API actually returns;
//   - truthful install states (disk presence + host target compatibility +
//     live rack membership); Install is disabled only when provably
//     incompatible with this host and the reason is shown on the card.
// ============================================================================

namespace ps_catalog = prismix::patchstorage;

constexpr int kPsCardH = 280;
constexpr int kPsArtH = 156;
constexpr int kPsGap = ps_catalog::kGridGap;
constexpr int kPsRowH = kPsCardH + kPsGap;
constexpr int kPsDetailsW = 816;
constexpr int kPsDetailsH = 472;

// A single card artwork bay. Slots are owned by the ctx through unique_ptr so
// the lv_image_dsc_t address handed to LVGL stays stable for the object's
// whole lifetime, and the decoded pixels stay alive via shared_ptr.
struct PatchStorageArtSlot {
    lv_obj_t* frame{nullptr};
    int patch_id{0};
    bool applied{false};
    lv_image_dsc_t dsc{};
    std::shared_ptr<const ps_catalog::ArtworkImage> image;
    ps_catalog::ArtworkPlaceholder kind{ps_catalog::ArtworkPlaceholder::Generic};
    const char* caption{"LV2 PLUGIN"};
};

struct PatchStorageUiCtx {
    FxScreenView* view{nullptr};
    lv_obj_t* container{nullptr};
    lv_obj_t* search_ta{nullptr};
    lv_obj_t* status_lbl{nullptr};
    lv_obj_t* cards_box{nullptr};
    lv_obj_t* prev_btn{nullptr};
    lv_obj_t* next_btn{nullptr};
    lv_obj_t* page_lbl{nullptr};
    lv_obj_t* sort_dd{nullptr};
    lv_obj_t* target_dd{nullptr};
    lv_obj_t* cat_dd{nullptr};
    lv_obj_t* state_dd{nullptr};
    lv_obj_t* search_btn{nullptr};
    lv_obj_t* reset_btn{nullptr};
    lv_obj_t* refresh_btn{nullptr};
    lv_obj_t* web_btn{nullptr};
    lv_obj_t* filter_caps[4]{nullptr, nullptr, nullptr, nullptr};
    lv_obj_t* cat_cap{nullptr};
    lv_obj_t* state_cap{nullptr};
    lv_timer_t* poll_timer{nullptr};

    // Details overlay
    lv_obj_t* details_overlay{nullptr};
    lv_obj_t* details_body{nullptr};
    std::unique_ptr<PatchStorageArtSlot> details_slot;
    ps_catalog::PatchStorageItem details_item;
    int details_patch_id{-1};
    uint32_t details_sig{0};

    // Filter / paging state (indices into the verified option tables)
    std::string search_query;
    int sort_idx{0};
    int target_idx{1};
    int cat_idx{0};
    int state_idx{0};
    int current_page{1};
    int total_pages{1};
    int total_items{0};
    int page_item_count{0};
    int shown_items{0};

    ps_catalog::BrowseState last_state;
    std::unordered_map<int, ps_catalog::PatchStatus> last_statuses;
    uint32_t status_fp{0};
    int last_generation{-1};
    int rendered_generation{-1};
    bool last_categories_loaded{false};
    size_t last_categories_size{0};
    bool size_pending{false};

    std::vector<std::unique_ptr<PatchStorageArtSlot>> art_slots;
};

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static uint32_t ps_hash_mix(uint32_t h, uint32_t v) {
    h ^= v;
    h *= 16777619u;
    return h;
}

static uint32_t ps_hash_str(uint32_t h, const std::string& s) {
    for (unsigned char ch : s) h = ps_hash_mix(h, ch);
    return h;
}

static ps_catalog::PatchStatus ps_status_for(
    const std::unordered_map<int, ps_catalog::PatchStatus>& statuses, int patch_id) {
    auto it = statuses.find(patch_id);
    return it != statuses.end() ? it->second : ps_catalog::PatchStatus{};
}

// Changes whenever anything the grid shows (result set, install states)
// changed; drives the selective rebuild in the poll timer.
static uint32_t ps_status_fingerprint(const ps_catalog::BrowseState& state,
                                      const std::unordered_map<int, ps_catalog::PatchStatus>& statuses) {
    uint32_t h = 2166136261u;
    h = ps_hash_mix(h, static_cast<uint32_t>(state.generation));
    h = ps_hash_mix(h, state.is_loading ? 1u : 0u);
    h = ps_hash_mix(h, static_cast<uint32_t>(state.items.size()));
    for (const auto& item : state.items) {
        h = ps_hash_mix(h, static_cast<uint32_t>(item.id));
        auto it = statuses.find(item.id);
        if (it != statuses.end()) {
            h = ps_hash_mix(h, static_cast<uint32_t>(it->second.state));
            h = ps_hash_str(h, it->second.message);
            h = ps_hash_str(h, it->second.installed_bundle_name);
            h = ps_hash_mix(h, it->second.loaded_in_rack ? 1u : 0u);
            h = ps_hash_mix(h, static_cast<uint32_t>(it->second.download_progress * 100.0f));
        }
    }
    return h;
}

// ---------------------------------------------------------------------------
// Local drawing helpers for the fallback artwork glyphs
// ---------------------------------------------------------------------------

static void ps_draw_rect(lv_layer_t* layer, int x, int y, int w, int h, lv_color_t color,
                         int radius, int border_w, lv_color_t border) {
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = color;
    d.bg_opa = LV_OPA_COVER;
    d.radius = radius;
    d.border_color = border;
    d.border_width = border_w;
    lv_area_t a{x, y, x + w - 1, y + h - 1};
    lv_draw_rect(layer, &d, &a);
}

static void ps_draw_circle(lv_layer_t* layer, int cx, int cy, int r, lv_color_t color) {
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = color;
    d.bg_opa = LV_OPA_COVER;
    d.radius = LV_RADIUS_CIRCLE;
    lv_area_t a{cx - r, cy - r, cx + r, cy + r};
    lv_draw_rect(layer, &d, &a);
}

static void ps_draw_line(lv_layer_t* layer, int x1, int y1, int x2, int y2, lv_color_t color, int width) {
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = color;
    d.width = width;
    d.p1.x = x1;
    d.p1.y = y1;
    d.p2.x = x2;
    d.p2.y = y2;
    lv_draw_line(layer, &d);
}

static void ps_draw_triangle(lv_layer_t* layer, int x1, int y1, int x2, int y2, int x3, int y3,
                             lv_color_t color) {
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.color = color;
    d.opa = LV_OPA_COVER;
    d.p[0].x = x1;
    d.p[0].y = y1;
    d.p[1].x = x2;
    d.p[1].y = y2;
    d.p[2].x = x3;
    d.p[2].y = y3;
    lv_draw_triangle(layer, &d);
}

static void ps_draw_text(lv_layer_t* layer, const char* text, const lv_font_t* font, lv_color_t color,
                         const lv_area_t& area, lv_text_align_t align) {
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.color = color;
    d.font = font;
    d.align = align;
    d.text = text;
    lv_area_t a = area;
    lv_draw_label(layer, &d, &a);
}

// Draws a category-specific glyph plus caption centered in the given bay.
// Purely local drawing: no network, no decoding, no allocation.
static void ps_draw_placeholder_glyph(lv_layer_t* layer, const lv_area_t& box,
                                      ps_catalog::ArtworkPlaceholder kind, const char* caption) {
    const int cx = (box.x1 + box.x2) / 2;
    const int cy = (box.y1 + box.y2) / 2;

    const lv_color_t cyan = lv_color_hex(0x35C0D8);
    const lv_color_t green = lv_color_hex(0x3FAF6C);
    const lv_color_t amber = lv_color_hex(0xC08A3E);
    const lv_color_t steel = lv_color_hex(0x39434D);
    const lv_color_t dark = lv_color_hex(0x141A20);
    const lv_color_t bg = lv_color_hex(0x0B0D0F);
    const lv_color_t key_white = lv_color_hex(0xB9C2CB);
    const lv_color_t key_black = lv_color_hex(0x101519);

    lv_color_t primary = cyan;
    switch (kind) {
        case ps_catalog::ArtworkPlaceholder::Effect:
        case ps_catalog::ArtworkPlaceholder::Game:
            primary = amber;
            break;
        case ps_catalog::ArtworkPlaceholder::Utility:
        case ps_catalog::ArtworkPlaceholder::Sampler:
            primary = green;
            break;
        default:
            primary = cyan;
            break;
    }

    switch (kind) {
        case ps_catalog::ArtworkPlaceholder::Synth: {
            const int base = cy + 8;
            for (int i = 0; i < 7; ++i) {
                ps_draw_rect(layer, cx - 28 + i * 8, base - 22, 7, 22, key_white, 1, 0, steel);
            }
            static const int black_offsets[5] = {0, 1, 3, 4, 5};
            for (int i = 0; i < 5; ++i) {
                ps_draw_rect(layer, cx - 28 + black_offsets[i] * 8 + 5, base - 22, 4, 13, key_black, 0, 0, steel);
            }
            ps_draw_line(layer, cx - 30, cy - 16, cx - 15, cy - 26, primary, 2);
            ps_draw_line(layer, cx - 15, cy - 26, cx, cy - 8, primary, 2);
            ps_draw_line(layer, cx, cy - 8, cx + 14, cy - 26, primary, 2);
            ps_draw_line(layer, cx + 14, cy - 26, cx + 30, cy - 16, primary, 2);
            break;
        }
        case ps_catalog::ArtworkPlaceholder::Effect: {
            const int r = 22;
            ps_draw_rect(layer, cx - r, cy - r, r * 2, r * 2, dark, LV_RADIUS_CIRCLE, 2, primary);
            ps_draw_line(layer, cx, cy, cx + 12, cy - 12, primary, 3);
            for (int a = -60; a <= 60; a += 30) {
                const float rad = static_cast<float>(a - 90) * 0.017453292f;
                const int x1 = cx + static_cast<int>(std::cos(rad) * static_cast<float>(r + 4));
                const int y1 = cy + static_cast<int>(std::sin(rad) * static_cast<float>(r + 4));
                const int x2 = cx + static_cast<int>(std::cos(rad) * static_cast<float>(r + 10));
                const int y2 = cy + static_cast<int>(std::sin(rad) * static_cast<float>(r + 10));
                ps_draw_line(layer, x1, y1, x2, y2, steel, 2);
            }
            break;
        }
        case ps_catalog::ArtworkPlaceholder::Utility: {
            const int r = 20;
            for (int a = 0; a < 360; a += 45) {
                const float rad = static_cast<float>(a) * 0.017453292f;
                const int x1 = cx + static_cast<int>(std::cos(rad) * static_cast<float>(r - 6));
                const int y1 = cy + static_cast<int>(std::sin(rad) * static_cast<float>(r - 6));
                const int x2 = cx + static_cast<int>(std::cos(rad) * static_cast<float>(r + 8));
                const int y2 = cy + static_cast<int>(std::sin(rad) * static_cast<float>(r + 8));
                ps_draw_line(layer, x1, y1, x2, y2, primary, 4);
            }
            ps_draw_rect(layer, cx - r, cy - r, r * 2, r * 2, dark, LV_RADIUS_CIRCLE, 3, primary);
            ps_draw_circle(layer, cx, cy, 6, bg);
            break;
        }
        case ps_catalog::ArtworkPlaceholder::Sampler: {
            static const int heights[9] = {10, 20, 30, 42, 34, 22, 28, 14, 8};
            const int base = cy + 22;
            for (int i = 0; i < 9; ++i) {
                const int h = heights[i];
                const bool tall = h >= 28;
                ps_draw_rect(layer, cx - 32 + i * 7, base - h, 5, h, tall ? primary : steel, 1, 0, steel);
            }
            ps_draw_line(layer, cx - 34, base, cx + 34, base, steel, 2);
            break;
        }
        case ps_catalog::ArtworkPlaceholder::Sequencer: {
            for (int gx = 0; gx < 4; ++gx) {
                for (int gy = 0; gy < 4; ++gy) {
                    const bool on = (gx == 0 && gy == 1) || (gx == 2 && gy == 2) || (gx == 1 && gy == 3) ||
                                    (gx == 3 && gy == 0);
                    ps_draw_rect(layer, cx - 26 + gx * 14, cy - 26 + gy * 14, 11, 11, on ? primary : steel, 2, 0, steel);
                }
            }
            break;
        }
        case ps_catalog::ArtworkPlaceholder::Composition: {
            for (int i = -1; i <= 1; ++i) {
                ps_draw_line(layer, cx - 34, cy + i * 12, cx + 34, cy + i * 12, steel, 2);
            }
            ps_draw_circle(layer, cx - 12, cy + 12, 5, primary);
            ps_draw_line(layer, cx - 8, cy + 12, cx - 8, cy - 14, primary, 2);
            ps_draw_line(layer, cx - 8, cy - 14, cx + 2, cy - 10, primary, 2);
            ps_draw_circle(layer, cx + 12, cy, 5, primary);
            ps_draw_line(layer, cx + 16, cy, cx + 16, cy - 26, primary, 2);
            break;
        }
        case ps_catalog::ArtworkPlaceholder::Sound: {
            ps_draw_rect(layer, cx - 30, cy - 12, 12, 24, steel, 2, 0, steel);
            ps_draw_triangle(layer, cx - 18, cy - 12, cx - 18, cy + 12, cx + 2, cy + 26, primary);
            ps_draw_triangle(layer, cx - 18, cy - 12, cx + 2, cy - 26, cx + 2, cy + 26, primary);
            ps_draw_line(layer, cx + 10, cy - 8, cx + 10, cy + 8, primary, 2);
            ps_draw_line(layer, cx + 16, cy - 14, cx + 16, cy + 14, primary, 2);
            ps_draw_line(layer, cx + 22, cy - 20, cx + 22, cy + 20, steel, 2);
            break;
        }
        case ps_catalog::ArtworkPlaceholder::Video: {
            ps_draw_rect(layer, cx - 34, cy - 22, 68, 44, dark, 4, 2, primary);
            for (int i = 0; i < 4; ++i) {
                ps_draw_rect(layer, cx - 28 + i * 15, cy - 18, 7, 6, steel, 1, 0, steel);
                ps_draw_rect(layer, cx - 28 + i * 15, cy + 12, 7, 6, steel, 1, 0, steel);
            }
            ps_draw_triangle(layer, cx - 6, cy - 8, cx - 6, cy + 8, cx + 8, cy, primary);
            break;
        }
        case ps_catalog::ArtworkPlaceholder::Game: {
            ps_draw_rect(layer, cx - 8, cy - 26, 16, 52, steel, 4, 0, steel);
            ps_draw_rect(layer, cx - 26, cy - 8, 52, 16, steel, 4, 0, steel);
            ps_draw_circle(layer, cx, cy, 4, primary);
            ps_draw_circle(layer, cx + 26, cy - 10, 6, primary);
            ps_draw_circle(layer, cx + 34, cy + 4, 6, steel);
            break;
        }
        default: {
            ps_draw_rect(layer, cx - 30, cy - 26, 60, 44, dark, 5, 2, cyan);
            lv_area_t text_area{cx - 30, cy - 10, cx + 30, cy + 6};
            ps_draw_text(layer, "LV2", &lv_font_unscii_8, cyan, text_area, LV_TEXT_ALIGN_CENTER);
            ps_draw_circle(layer, cx - 12, cy + 32, 3, steel);
            ps_draw_circle(layer, cx, cy + 32, 3, steel);
            ps_draw_circle(layer, cx + 12, cy + 32, 3, steel);
            break;
        }
    }

    if (caption && caption[0] != '\0') {
        lv_area_t cap_area{box.x1, cy + 34, box.x2, cy + 50};
        ps_draw_text(layer, caption, &ac_font_12, COLOR_TEXT_MUTED, cap_area, LV_TEXT_ALIGN_CENTER);
    }
}

// DRAW_MAIN handler on an artwork bay: paints the placeholder while the real
// artwork is still loading / unavailable. Skipped once real art is applied.
static void ps_artwork_draw_cb(lv_event_t* e) {
    auto* slot = static_cast<PatchStorageArtSlot*>(lv_event_get_user_data(e));
    if (!slot || slot->applied) return;
    lv_layer_t* layer = lv_event_get_layer(e);
    lv_obj_t* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));
    if (!layer || !obj) return;
    lv_area_t coords;
    lv_obj_get_coords(obj, &coords);
    ps_draw_placeholder_glyph(layer, coords, slot->kind, slot->caption);
}

// Creates the LVGL image for a decoded artwork, letterboxed (contain) inside
// the bay so the aspect ratio is never distorted.
static bool ps_apply_artwork(PatchStorageArtSlot* slot) {
    if (!slot || slot->applied || !slot->frame || !slot->image) return false;
    const ps_catalog::ArtworkImage& img = *slot->image;
    if (img.width <= 0 || img.height <= 0 || img.pixels.empty()) return false;

    slot->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    slot->dsc.header.cf = LV_COLOR_FORMAT_XRGB8888;
    slot->dsc.header.w = static_cast<uint32_t>(img.width);
    slot->dsc.header.h = static_cast<uint32_t>(img.height);
    slot->dsc.header.stride = static_cast<uint32_t>(img.width * 4);
    slot->dsc.data_size = static_cast<uint32_t>(img.pixels.size());
    slot->dsc.data = img.pixels.data();

    lv_obj_t* img_obj = lv_image_create(slot->frame);
    lv_image_set_src(img_obj, &slot->dsc);
    lv_obj_remove_flag(img_obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(img_obj, 0, 0);
    lv_obj_set_size(img_obj, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(img_obj, 0, 0);
    lv_image_set_inner_align(img_obj, LV_IMAGE_ALIGN_CONTAIN);
    slot->applied = true;
    return true;
}

// Applies any artwork that finished decoding since the last tick, in place
// (no grid rebuild, scroll position preserved).
static void ps_update_artwork_slots(PatchStorageUiCtx* ctx) {
    if (!ctx) return;
    auto& svc = ps_catalog::PatchStorageService::instance();
    for (auto& up : ctx->art_slots) {
        PatchStorageArtSlot* slot = up.get();
        if (!slot || slot->applied || !slot->frame) continue;
        if (!slot->image) slot->image = svc.get_artwork(slot->patch_id);
        if (slot->image) ps_apply_artwork(slot);
    }
}

// ---------------------------------------------------------------------------
// Status badge
// ---------------------------------------------------------------------------

struct PsBadge {
    std::string text;
    lv_color_t color;
};

static PsBadge ps_badge_for(const ps_catalog::PatchStorageItem& item, const ps_catalog::PatchStatus& st,
                            bool host_compatible) {
    switch (st.state) {
        case ps_catalog::PatchInstallState::Downloading:
            return {"DOWNLOADING...", COLOR_ORANGE};
        case ps_catalog::PatchInstallState::Validating:
            return {"VALIDATING...", COLOR_CYAN};
        case ps_catalog::PatchInstallState::Installing:
            return {st.message.find("Uninstall") != std::string::npos ? "REMOVING..." : "INSTALLING...", COLOR_CYAN};
        case ps_catalog::PatchInstallState::Installed:
            return st.loaded_in_rack ? PsBadge{"LOADED IN PRISMIX", COLOR_CYAN} : PsBadge{"INSTALLED", COLOR_GREEN};
        case ps_catalog::PatchInstallState::Error:
            return {"FAILED - RETRY", COLOR_RED};
        default:
            break;
    }

    if (!host_compatible || st.state == ps_catalog::PatchInstallState::Incompatible) {
        const std::string targets = ps_catalog::targets_summary(item);
        return {targets.empty() ? std::string("NO BUILD FOR THIS HOST") : (targets + " ONLY"), COLOR_ORANGE};
    }
    return {"COMPATIBLE", COLOR_GREEN};
}

// ---------------------------------------------------------------------------
// Buttons and action handlers
// ---------------------------------------------------------------------------

static lv_obj_t* ps_button(lv_obj_t* parent, const char* text, int x, int y, int w, int h, bool accent,
                           bool enabled = true) {
    lv_obj_t* btn = lv_button_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    if (!enabled) {
        ui::rack_skin::apply_hardware_button(btn, false, COLOR_EDGE, COLOR_RECESSED, COLOR_RECESSED);
        lv_obj_add_state(btn, LV_STATE_DISABLED);
    } else if (accent) {
        ui::rack_skin::apply_hardware_button(btn, true, COLOR_CYAN, lv_color_hex(0x183848), lv_color_hex(0x1B232A));
    } else {
        ui::rack_skin::apply_hardware_button(btn, false, COLOR_EDGE, COLOR_CONTROL, lv_color_hex(0x20262D));
    }
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    lv_obj_set_style_text_font(lbl, &ac_font_12, 0);
    lv_obj_set_style_text_color(lbl, !enabled ? COLOR_TEXT_MUTED : (accent ? COLOR_CYAN : COLOR_TEXT2), 0);
    return btn;
}

struct PsInstallCtx {
    ps_catalog::PatchStorageItem item;
};

static void ps_attach_install(lv_obj_t* btn, const ps_catalog::PatchStorageItem& item) {
    auto* actx = new PsInstallCtx{item};
    lv_obj_add_event_cb(btn, [](lv_event_t* e) {
        auto* c = static_cast<PsInstallCtx*>(lv_event_get_user_data(e));
        if (c) ps_catalog::PatchStorageService::instance().request_install(c->item);
    }, LV_EVENT_CLICKED, actx);
    lv_obj_add_event_cb(btn, [](lv_event_t* e) {
        delete static_cast<PsInstallCtx*>(lv_event_get_user_data(e));
    }, LV_EVENT_DELETE, actx);
}

struct PsUninstallCtx {
    int patch_id;
    std::string bundle;
};

static void ps_attach_uninstall(lv_obj_t* btn, int patch_id, const std::string& bundle) {
    auto* uctx = new PsUninstallCtx{patch_id, bundle};
    lv_obj_add_event_cb(btn, [](lv_event_t* e) {
        auto* c = static_cast<PsUninstallCtx*>(lv_event_get_user_data(e));
        if (c) ps_catalog::PatchStorageService::instance().request_uninstall(c->patch_id, c->bundle);
    }, LV_EVENT_CLICKED, uctx);
    lv_obj_add_event_cb(btn, [](lv_event_t* e) {
        delete static_cast<PsUninstallCtx*>(lv_event_get_user_data(e));
    }, LV_EVENT_DELETE, uctx);
}

struct PsWebCtx {
    std::string url;
};

static void ps_attach_web(lv_obj_t* btn, const std::string& url) {
    auto* wctx = new PsWebCtx{url};
    lv_obj_add_event_cb(btn, [](lv_event_t* e) {
        auto* c = static_cast<PsWebCtx*>(lv_event_get_user_data(e));
        if (c && !c->url.empty()) ps_catalog::PatchStorageClient::open_in_browser(c->url);
    }, LV_EVENT_CLICKED, wctx);
    lv_obj_add_event_cb(btn, [](lv_event_t* e) {
        delete static_cast<PsWebCtx*>(lv_event_get_user_data(e));
    }, LV_EVENT_DELETE, wctx);
}

struct PsAddToRackCtx {
    FxScreenView* view;
    std::string uri;
};

static void ps_attach_add_to_rack(lv_obj_t* btn, FxScreenView* view, const std::string& uri) {
    auto* actx = new PsAddToRackCtx{view, uri};
    lv_obj_add_event_cb(btn, [](lv_event_t* e) {
        auto* c = static_cast<PsAddToRackCtx*>(lv_event_get_user_data(e));
        if (!c || !c->view) return;
        ChannelState* target = resolve_target_channel(c->view);
        if (target && !c->uri.empty()) {
            target->fx_rack.add_plugin(c->uri);
            c->view->selected_slot = static_cast<int>(target->fx_rack.count()) - 1;
            sync_channel_fx_to_dsp(target);
            c->view->last_param_fingerprint = compute_rack_fingerprint(target);
            // The plugin is now live in a rack slot; refresh catalog statuses so
            // the card can show the truthful "LOADED IN PRISMIX" state.
            ps_catalog::PatchStorageService::instance().request_status_refresh();
        }
        schedule_ui_rebuild(c->view, true, true, true);
    }, LV_EVENT_CLICKED, actx);
    lv_obj_add_event_cb(btn, [](lv_event_t* e) {
        delete static_cast<PsAddToRackCtx*>(lv_event_get_user_data(e));
    }, LV_EVENT_DELETE, actx);
}

// ---------------------------------------------------------------------------
// Grid rendering
// ---------------------------------------------------------------------------

static void ps_open_details(PatchStorageUiCtx* ctx, const ps_catalog::PatchStorageItem& item);

static void ps_add_status_chip(lv_obj_t* card, const PsBadge& badge, int card_w) {
    const int chip_w = std::min(card_w - 40, static_cast<int>(badge.text.size()) * 7 + 16);
    lv_obj_t* chip = lv_obj_create(card);
    lv_obj_set_pos(chip, 12, 8 + kPsArtH - 30);
    lv_obj_set_size(chip, chip_w, 22);
    lv_obj_remove_flag(chip, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_style_bg_color(chip, lv_color_hex(0x0B0D0F), 0);
    lv_obj_set_style_bg_opa(chip, 224, 0);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_border_color(chip, badge.color, 0);
    lv_obj_set_style_radius(chip, 3, 0);
    lv_obj_set_style_pad_all(chip, 0, 0);

    lv_obj_t* lbl = lv_label_create(chip);
    lv_label_set_text(lbl, badge.text.c_str());
    lv_obj_set_size(lbl, chip_w - 14, 16);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 7, 0);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_font(lbl, &ac_font_12, 0);
    lv_obj_set_style_text_color(lbl, badge.color, 0);
}

static void ps_build_card(PatchStorageUiCtx* ctx, const ps_catalog::PatchStorageItem& item,
                          const std::unordered_map<int, ps_catalog::PatchStatus>& statuses,
                          ps_catalog::HostArch host, int x, int y, int w) {
    const ps_catalog::PatchStatus st = ps_status_for(statuses, item.id);
    const bool host_compatible = item.is_compatible_with_host(host);

    lv_obj_t* card = lv_obj_create(ctx->cards_box);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, kPsCardH);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
    ui::rack_skin::apply_hardware_button(card, false, COLOR_STANDARD_BORDER, COLOR_PANEL_SURFACE, COLOR_CONTROL);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_radius(card, 4, 0);
    {
        char nm[32];
        std::snprintf(nm, sizeof(nm), "ps_card_%d", item.id);
        lv_obj_set_name(card, nm);
    }

    // Artwork bay: recessed frame, contain-fit art, category placeholder.
    lv_obj_t* frame = lv_obj_create(card);
    lv_obj_set_pos(frame, 8, 8);
    lv_obj_set_size(frame, w - 16, kPsArtH);
    ui::rack_skin::apply_recessed_display(frame, lv_color_hex(0x0B0D0F), lv_color_hex(0x232B33));
    lv_obj_remove_flag(frame, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_style_pad_all(frame, 0, 0);
    lv_obj_set_style_radius(frame, 3, 0);

    auto slot = std::make_unique<PatchStorageArtSlot>();
    slot->frame = frame;
    slot->patch_id = item.id;
    slot->kind = ps_catalog::placeholder_for_categories(item.categories);
    slot->caption = ps_catalog::placeholder_caption(slot->kind, item.categories);
    slot->image = ps_catalog::PatchStorageService::instance().get_artwork(item.id);
    lv_obj_add_event_cb(frame, ps_artwork_draw_cb, LV_EVENT_DRAW_MAIN, slot.get());
    if (slot->image) {
        ps_apply_artwork(slot.get());
    } else if (!item.artwork_thumbnail_url.empty() || !item.artwork_url.empty()) {
        if (ps_catalog::PatchStorageService::instance().get_artwork_state(item.id) == ps_catalog::ArtworkState::None) {
            ps_catalog::PatchStorageService::instance().request_artwork(item);
        }
    }
    ctx->art_slots.push_back(std::move(slot));

    // Identity
    lv_obj_t* title = lv_label_create(card);
    lv_label_set_text(title, item.title.empty() ? "Untitled LV2 Plugin" : item.title.c_str());
    lv_obj_set_pos(title, 10, 172);
    lv_obj_set_size(title, w - 20, 20);
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_font(title, &ac_font_14, 0);
    lv_obj_set_style_text_color(title, COLOR_PRIMARY_TEXT, 0);

    std::string author = item.author.name.empty() ? "COMMUNITY" : item.author.name;
    std::string meta = "BY " + author + "   -   " + ps_catalog::primary_category_name(item);
    lv_obj_t* meta_lbl = lv_label_create(card);
    lv_label_set_text(meta_lbl, meta.c_str());
    lv_obj_set_pos(meta_lbl, 10, 194);
    lv_obj_set_size(meta_lbl, w - 20, 16);
    lv_label_set_long_mode(meta_lbl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_font(meta_lbl, &ac_font_12, 0);
    lv_obj_set_style_text_color(meta_lbl, COLOR_TEXT_MUTED, 0);

    const std::string desc = item.excerpt.empty() ? std::string("No description provided.") : item.excerpt;
    lv_obj_t* desc_lbl = lv_label_create(card);
    lv_label_set_text(desc_lbl, desc.c_str());
    lv_obj_set_pos(desc_lbl, 10, 212);
    lv_obj_set_size(desc_lbl, w - 20, 34);
    lv_label_set_long_mode(desc_lbl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_font(desc_lbl, &ac_font_12, 0);
    lv_obj_set_style_text_color(desc_lbl, COLOR_TEXT2, 0);

    // Status chip over the bottom-left of the artwork.
    ps_add_status_chip(card, ps_badge_for(item, st, host_compatible), w);

    // Action row (right aligned): WEB always available; one state action left
    // of it. No card-wide button, no overlap with text.
    const int by = kPsCardH - 30;
    const int bh = 26;
    int bx = w - 8;

    lv_obj_t* web = ps_button(card, "WEB", bx - 52, by, 52, bh, false);
    ps_attach_web(web, item.url);
    bx -= 52 + 6;

    if (st.state == ps_catalog::PatchInstallState::Installed) {
        lv_obj_t* un = ps_button(card, "UNINSTALL", bx - 84, by, 84, bh, false);
        ps_attach_uninstall(un, item.id, st.installed_bundle_name);
        bx -= 84 + 6;
        const std::string uri =
            !st.installed_plugin_uri.empty() ? st.installed_plugin_uri : (item.uids.empty() ? std::string() : item.uids[0]);
        lv_obj_t* add = ps_button(card, "ADD TO RACK", bx - 96, by, 96, bh, true);
        ps_attach_add_to_rack(add, ctx->view, uri);
        bx -= 96 + 6;
    } else if (st.state == ps_catalog::PatchInstallState::Error) {
        lv_obj_t* retry = ps_button(card, "RETRY", bx - 68, by, 68, bh, true);
        ps_attach_install(retry, item);
        bx -= 68 + 6;
    } else if (st.state == ps_catalog::PatchInstallState::Downloading ||
               st.state == ps_catalog::PatchInstallState::Installing ||
               st.state == ps_catalog::PatchInstallState::Validating) {
        // Install in flight: the chip shows the phase, no duplicate action.
    } else if (!host_compatible || st.state == ps_catalog::PatchInstallState::Incompatible) {
        lv_obj_t* dis = ps_button(card, "INSTALL", bx - 76, by, 76, bh, false, false);
        lv_obj_set_name(dis, "ps_disabled_install");
        bx -= 76 + 6;
    } else {
        lv_obj_t* inst = ps_button(card, "INSTALL", bx - 76, by, 76, bh, true);
        ps_attach_install(inst, item);
        bx -= 76 + 6;
    }

    (void)bx;

    // Card click opens the details overlay (buttons do not bubble).
    struct PsCardClickCtx {
        PatchStorageUiCtx* ui;
        ps_catalog::PatchStorageItem item;
    };
    auto* cctx = new PsCardClickCtx{ctx, item};
    lv_obj_add_event_cb(card, [](lv_event_t* e) {
        auto* c = static_cast<PsCardClickCtx*>(lv_event_get_user_data(e));
        if (c && c->ui) ps_open_details(c->ui, c->item);
    }, LV_EVENT_CLICKED, cctx);
    lv_obj_add_event_cb(card, [](lv_event_t* e) {
        delete static_cast<PsCardClickCtx*>(lv_event_get_user_data(e));
    }, LV_EVENT_DELETE, cctx);
}

static const ps_catalog::PatchStateFilter kPsStateFilters[] = {
    ps_catalog::PatchStateFilter::Any,
    ps_catalog::PatchStateFilter::Installed,
    ps_catalog::PatchStateFilter::NotInstalled,
    ps_catalog::PatchStateFilter::HostCompatible,
    ps_catalog::PatchStateFilter::Incompatible,
};

static void ps_update_pagination_label(PatchStorageUiCtx* ctx) {
    if (!ctx || !ctx->page_lbl) return;
    char buf[192];
    if (ctx->state_idx == 0) {
        std::snprintf(buf, sizeof(buf), "PAGE %d / %d      %d PLUGINS IN CATALOG", ctx->current_page,
                      ctx->total_pages, ctx->total_items);
    } else {
        std::snprintf(buf, sizeof(buf), "PAGE %d / %d      %d PLUGINS      %d OF %d SHOWN ON PAGE (STATE: %s)",
                      ctx->current_page, ctx->total_pages, ctx->total_items, ctx->shown_items,
                      ctx->page_item_count, ps_catalog::state_filter_label(kPsStateFilters[ctx->state_idx]));
    }
    lv_label_set_text(ctx->page_lbl, buf);
}

static void ps_sync_pager_buttons(PatchStorageUiCtx* ctx) {
    if (!ctx) return;
    if (ctx->prev_btn) {
        if (ctx->current_page <= 1) lv_obj_add_state(ctx->prev_btn, LV_STATE_DISABLED);
        else lv_obj_remove_state(ctx->prev_btn, LV_STATE_DISABLED);
    }
    if (ctx->next_btn) {
        if (ctx->current_page >= ctx->total_pages) lv_obj_add_state(ctx->next_btn, LV_STATE_DISABLED);
        else lv_obj_remove_state(ctx->next_btn, LV_STATE_DISABLED);
    }
}

static void render_patchstorage_cards(PatchStorageUiCtx* ctx, const ps_catalog::BrowseState& state,
                                      const std::unordered_map<int, ps_catalog::PatchStatus>& statuses) {
    if (!ctx || !ctx->cards_box) return;

    ctx->current_page = std::max(1, state.current_page);
    ctx->total_pages = std::max(1, state.total_pages);
    ctx->total_items = state.total_items;
    ctx->page_item_count = static_cast<int>(state.items.size());

    if (ctx->status_lbl) {
        lv_label_set_text(ctx->status_lbl, state.status_message.empty() ? " " : state.status_message.c_str());
    }

    // Preserve the scroll position across status-only rebuilds of the same
    // result set; jump to the top when the result set itself changed.
    const bool same_result_set = (ctx->rendered_generation == state.generation);
    const int32_t prev_scroll = same_result_set ? lv_obj_get_scroll_y(ctx->cards_box) : 0;
    ctx->rendered_generation = state.generation;

    lv_obj_clean(ctx->cards_box);
    ctx->art_slots.clear();

    if (state.is_loading) {
        ctx->shown_items = 0;
        lv_obj_t* l = lv_label_create(ctx->cards_box);
        lv_label_set_text(l, "CONNECTING TO PATCHSTORAGE...");
        lv_obj_center(l);
        lv_obj_set_style_text_font(l, &ac_font_14, 0);
        lv_obj_set_style_text_color(l, COLOR_CYAN, 0);
        ps_update_pagination_label(ctx);
        ps_sync_pager_buttons(ctx);
        return;
    }

    if (state.items.empty()) {
        ctx->shown_items = 0;
        lv_obj_t* l = lv_label_create(ctx->cards_box);
        lv_label_set_text(l, "NO LV2 PLUGINS FOUND.\nADJUST THE SEARCH OR FILTERS, PRESS RESET, OR OPEN THE WEB CATALOG.");
        lv_obj_set_size(l, lv_obj_get_content_width(ctx->cards_box) - 20, 44);
        lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(l);
        lv_obj_set_style_text_font(l, &ac_font_12, 0);
        lv_obj_set_style_text_color(l, COLOR_TEXT_MUTED, 0);
        ps_update_pagination_label(ctx);
        ps_sync_pager_buttons(ctx);
        return;
    }

    const ps_catalog::HostArch host = ps_catalog::detect_current_host_arch();
    const int state_idx = std::max(0, std::min<int>(ctx->state_idx, 4));
    const ps_catalog::PatchStateFilter filter = kPsStateFilters[state_idx];

    std::vector<const ps_catalog::PatchStorageItem*> visible;
    visible.reserve(state.items.size());
    for (const auto& item : state.items) {
        if (ps_catalog::matches_state_filter(item, ps_status_for(statuses, item.id), host, filter)) {
            visible.push_back(&item);
        }
    }
    ctx->shown_items = static_cast<int>(visible.size());
    ps_update_pagination_label(ctx);
    ps_sync_pager_buttons(ctx);

    if (visible.empty()) {
        lv_obj_t* l = lv_label_create(ctx->cards_box);
        lv_label_set_text(l, "NO PLUGINS ON THIS PAGE MATCH THE STATE FILTER");
        lv_obj_set_style_text_font(l, &ac_font_14, 0);
        lv_obj_set_style_text_color(l, COLOR_TEXT2, 0);
        lv_obj_t* l2 = lv_label_create(ctx->cards_box);
        lv_label_set_text(l2, "STATE FILTERS APPLY TO THE CURRENT PAGE - USE PREV / NEXT OR RESET.");
        lv_obj_set_style_text_font(l2, &ac_font_12, 0);
        lv_obj_set_style_text_color(l2, COLOR_TEXT_MUTED, 0);
        lv_obj_align(l, LV_ALIGN_CENTER, 0, -12);
        lv_obj_align(l2, LV_ALIGN_CENTER, 0, 14);
        return;
    }

    const int content_w = lv_obj_get_content_width(ctx->cards_box);
    const int cols = ps_catalog::compute_grid_columns(content_w);
    const int card_w = std::max(200, (content_w - (cols - 1) * kPsGap) / cols);

    std::vector<int> visible_ids;
    visible_ids.reserve(visible.size());
    for (size_t i = 0; i < visible.size(); ++i) {
        const int col = static_cast<int>(i % static_cast<size_t>(cols));
        const int row = static_cast<int>(i / static_cast<size_t>(cols));
        ps_build_card(ctx, *visible[i], statuses, host, col * (card_w + kPsGap), row * kPsRowH, card_w);
        visible_ids.push_back(visible[i]->id);
    }
    if (!visible.empty()) {
        const int total_rows = static_cast<int>((visible.size() + static_cast<size_t>(cols) - 1) / static_cast<size_t>(cols));
        lv_obj_t* spacer = lv_obj_create(ctx->cards_box);
        lv_obj_set_pos(spacer, 0, total_rows * kPsRowH);
        lv_obj_set_size(spacer, 10, 24);
        lv_obj_add_flag(spacer, LV_OBJ_FLAG_HIDDEN);
    }
    ps_catalog::PatchStorageService::instance().set_visible_artwork(visible_ids);

    lv_obj_scroll_to_y(ctx->cards_box, prev_scroll, LV_ANIM_OFF);
}

// ---------------------------------------------------------------------------
// Details overlay
// ---------------------------------------------------------------------------

static uint32_t ps_details_sig(const ps_catalog::PatchStatus& st, bool host_compatible, bool art_ready) {
    uint32_t h = 7u;
    h = ps_hash_mix(h, static_cast<uint32_t>(st.state));
    h = ps_hash_str(h, st.message);
    h = ps_hash_str(h, st.installed_bundle_name);
    h = ps_hash_mix(h, st.loaded_in_rack ? 1u : 0u);
    h = ps_hash_mix(h, host_compatible ? 1u : 0u);
    h = ps_hash_mix(h, art_ready ? 1u : 0u);
    h = ps_hash_mix(h, static_cast<uint32_t>(st.download_progress * 100.0f));
    return h;
}

static void ps_close_details(PatchStorageUiCtx* ctx) {
    if (!ctx) return;
    if (ctx->details_overlay) {
        lv_obj_delete(ctx->details_overlay);
    }
    ctx->details_overlay = nullptr;
    ctx->details_body = nullptr;
    ctx->details_slot.reset();
    ctx->details_patch_id = -1;
    ctx->details_sig = 0;
}

static void ps_build_details_body(PatchStorageUiCtx* ctx, const ps_catalog::PatchStorageItem& item,
                                  const ps_catalog::PatchStatus& st, bool host_compatible) {
    if (!ctx || !ctx->details_body) return;
    lv_obj_t* panel = ctx->details_body;
    ctx->details_slot.reset();
    lv_obj_clean(panel);

    const bool installed = (st.state == ps_catalog::PatchInstallState::Installed);
    const bool busy = st.state == ps_catalog::PatchInstallState::Downloading ||
                      st.state == ps_catalog::PatchInstallState::Installing ||
                      st.state == ps_catalog::PatchInstallState::Validating;
    const bool is_error = st.state == ps_catalog::PatchInstallState::Error;

    // Artwork bay
    lv_obj_t* frame = lv_obj_create(panel);
    lv_obj_set_pos(frame, 16, 16);
    lv_obj_set_size(frame, 264, 168);
    ui::rack_skin::apply_recessed_display(frame, lv_color_hex(0x0B0D0F), lv_color_hex(0x232B33));
    lv_obj_remove_flag(frame, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_style_pad_all(frame, 0, 0);
    lv_obj_set_style_radius(frame, 3, 0);

    auto slot = std::make_unique<PatchStorageArtSlot>();
    slot->frame = frame;
    slot->patch_id = item.id;
    slot->kind = ps_catalog::placeholder_for_categories(item.categories);
    slot->caption = ps_catalog::placeholder_caption(slot->kind, item.categories);
    slot->image = ps_catalog::PatchStorageService::instance().get_artwork(item.id);
    lv_obj_add_event_cb(frame, ps_artwork_draw_cb, LV_EVENT_DRAW_MAIN, slot.get());
    if (slot->image) {
        ps_apply_artwork(slot.get());
    } else if (!item.artwork_thumbnail_url.empty() || !item.artwork_url.empty()) {
        if (ps_catalog::PatchStorageService::instance().get_artwork_state(item.id) == ps_catalog::ArtworkState::None) {
            ps_catalog::PatchStorageService::instance().request_artwork(item);
        }
    }
    const bool art_ready = slot->image != nullptr;
    ctx->details_slot = std::move(slot);

    // Identity
    lv_obj_t* title = lv_label_create(panel);
    lv_label_set_text(title, item.title.empty() ? "Untitled LV2 Plugin" : item.title.c_str());
    lv_obj_set_pos(title, 300, 12);
    lv_obj_set_size(title, 500, 26);
    lv_label_set_long_mode(title, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_font(title, &ac_font_20, 0);
    lv_obj_set_style_text_color(title, COLOR_PRIMARY_TEXT, 0);

    std::string identity = "BY ";
    identity += item.author.name.empty() ? std::string("COMMUNITY") : item.author.name;
    identity += "   -   ";
    identity += ps_catalog::primary_category_name(item);
    lv_obj_t* by_lbl = lv_label_create(panel);
    lv_label_set_text(by_lbl, identity.c_str());
    lv_obj_set_pos(by_lbl, 300, 44);
    lv_obj_set_size(by_lbl, 500, 16);
    lv_label_set_long_mode(by_lbl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_font(by_lbl, &ac_font_12, 0);
    lv_obj_set_style_text_color(by_lbl, COLOR_TEXT_MUTED, 0);

    // Info block: only fields the live API actually returns.
    std::string info;
    auto add_info = [&info](const char* label, const std::string& value) {
        if (!info.empty()) info += '\n';
        info += label;
        info += value;
    };
    std::string cats;
    for (const auto& c : item.categories) {
        if (!cats.empty()) cats += ", ";
        cats += c.name;
    }
    add_info("CATEGORY     ", cats.empty() ? "LV2 PLUGIN" : cats);
    if (!item.license_name.empty()) add_info("LICENSE      ", item.license_name);
    if (!item.revision.empty()) add_info("REVISION     ", item.revision);
    {
        const std::string targets = ps_catalog::targets_summary(item);
        add_info("TARGETS      ", targets.empty() ? "ALL TARGETS" : targets);
    }
    add_info("CATALOG      ", "PATCHSTORAGE LV2 (PLATFORM #8046)");

    lv_obj_t* info_lbl = lv_label_create(panel);
    lv_label_set_text(info_lbl, info.c_str());
    lv_obj_set_pos(info_lbl, 300, 64);
    lv_obj_set_size(info_lbl, 500, 96);
    lv_label_set_long_mode(info_lbl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_font(info_lbl, &ac_font_12, 0);
    lv_obj_set_style_text_color(info_lbl, COLOR_TEXT2, 0);

    // Status line (+ reason when errored / incompatible)
    const PsBadge badge = ps_badge_for(item, st, host_compatible);
    std::string status_text = "STATUS   ";
    status_text += badge.text;
    if (!st.message.empty() && (is_error || busy || !host_compatible)) {
        status_text += "   -   ";
        status_text += st.message.substr(0, 140);
    }
    lv_obj_t* st_lbl = lv_label_create(panel);
    lv_label_set_text(st_lbl, status_text.c_str());
    lv_obj_set_pos(st_lbl, 16, 196);
    lv_obj_set_size(st_lbl, 784, 16);
    lv_label_set_long_mode(st_lbl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_font(st_lbl, &ac_font_12, 0);
    lv_obj_set_style_text_color(st_lbl, badge.color, 0);

    int desc_y = 218;
    if (busy) {
        lv_obj_t* bar = lv_bar_create(panel);
        lv_obj_set_pos(bar, 16, 216);
        lv_obj_set_size(bar, 784, 8);
        lv_bar_set_range(bar, 0, 100);
        lv_bar_set_value(bar, static_cast<int32_t>(st.download_progress * 100.0f), LV_ANIM_OFF);
        lv_obj_set_style_bg_color(bar, COLOR_RECESSED, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(bar, COLOR_CYAN, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_radius(bar, 2, LV_PART_MAIN);
        lv_obj_set_style_radius(bar, 2, LV_PART_INDICATOR);
        desc_y = 236;
    }

    std::string desc = !item.content.empty() ? item.content : item.excerpt;
    if (desc.empty()) desc = "No description provided by the author.";
    if (desc.size() > 1200) desc.resize(1200);
    lv_obj_t* desc_lbl = lv_label_create(panel);
    lv_label_set_text(desc_lbl, desc.c_str());
    lv_obj_set_pos(desc_lbl, 16, desc_y);
    lv_obj_set_size(desc_lbl, 784, 400 - desc_y);
    lv_label_set_long_mode(desc_lbl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_font(desc_lbl, &ac_font_12, 0);
    lv_obj_set_style_text_color(desc_lbl, COLOR_TEXT_MUTED, 0);

    // Actions
    const int by = 424;
    const int bh = 28;
    int bx = 800;

    lv_obj_t* close_btn = ps_button(panel, "CLOSE", bx - 76, by, 76, bh, false);
    lv_obj_set_name(close_btn, "ps_details_close");
    lv_obj_add_event_cb(close_btn, [](lv_event_t* e) {
        auto* c = static_cast<PatchStorageUiCtx*>(lv_event_get_user_data(e));
        ps_close_details(c);
    }, LV_EVENT_CLICKED, ctx);
    bx -= 76 + 6;

    lv_obj_t* web = ps_button(panel, "WEB", bx - 56, by, 56, bh, false);
    ps_attach_web(web, item.url);
    bx -= 56 + 6;

    if (installed) {
        lv_obj_t* un = ps_button(panel, "UNINSTALL", bx - 96, by, 96, bh, false);
        ps_attach_uninstall(un, item.id, st.installed_bundle_name);
        bx -= 96 + 6;
        const std::string uri =
            !st.installed_plugin_uri.empty() ? st.installed_plugin_uri : (item.uids.empty() ? std::string() : item.uids[0]);
        lv_obj_t* add = ps_button(panel, "ADD TO RACK", bx - 118, by, 118, bh, true);
        ps_attach_add_to_rack(add, ctx->view, uri);
        bx -= 118 + 6;
    } else if (is_error) {
        lv_obj_t* retry = ps_button(panel, "RETRY INSTALL", bx - 118, by, 118, bh, true);
        ps_attach_install(retry, item);
        bx -= 118 + 6;
    } else if (!busy) {
        if (host_compatible && st.state != ps_catalog::PatchInstallState::Incompatible) {
            lv_obj_t* inst = ps_button(panel, "INSTALL", bx - 84, by, 84, bh, true);
            ps_attach_install(inst, item);
            bx -= 84 + 6;
        } else {
            lv_obj_t* dis = ps_button(panel, "INSTALL", bx - 84, by, 84, bh, false, false);
            lv_obj_set_name(dis, "ps_details_disabled_install");
            bx -= 84 + 6;
            std::string reason = "INSTALL DISABLED: NO BUILD FOR THIS HOST";
            const std::string targets = ps_catalog::targets_summary(item);
            if (!targets.empty()) reason += " (AVAILABLE: " + targets + ")";
            lv_obj_t* reason_lbl = lv_label_create(panel);
            lv_label_set_text(reason_lbl, reason.c_str());
            lv_obj_set_pos(reason_lbl, 16, by + 6);
            lv_obj_set_size(reason_lbl, 448, 16);
            lv_label_set_long_mode(reason_lbl, LV_LABEL_LONG_MODE_DOTS);
            lv_obj_set_style_text_font(reason_lbl, &ac_font_12, 0);
            lv_obj_set_style_text_color(reason_lbl, COLOR_ORANGE, 0);
        }
    }

    (void)bx;
    ctx->details_sig = ps_details_sig(st, host_compatible, art_ready);
}

static void ps_open_details(PatchStorageUiCtx* ctx, const ps_catalog::PatchStorageItem& item) {
    if (!ctx || !ctx->container) return;
    ps_close_details(ctx);
    ctx->details_item = item;
    ctx->details_patch_id = item.id;

    const int list_w = lv_obj_get_width(ctx->container);
    const int list_h = lv_obj_get_height(ctx->container);
    const int w = list_w > 200 ? list_w : 936;
    const int h = list_h > 200 ? list_h : 520;

    ctx->details_overlay = lv_obj_create(ctx->container);
    lv_obj_set_pos(ctx->details_overlay, 0, 0);
    lv_obj_set_size(ctx->details_overlay, w, h);
    lv_obj_set_name(ctx->details_overlay, "ps_details");
    lv_obj_remove_flag(ctx->details_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ctx->details_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(ctx->details_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(ctx->details_overlay, 190, 0);
    lv_obj_set_style_border_width(ctx->details_overlay, 0, 0);
    lv_obj_set_style_radius(ctx->details_overlay, 0, 0);
    lv_obj_set_style_pad_all(ctx->details_overlay, 0, 0);
    lv_obj_add_event_cb(ctx->details_overlay, [](lv_event_t* e) {
        auto* c = static_cast<PatchStorageUiCtx*>(lv_event_get_user_data(e));
        if (c && lv_event_get_target(e) == c->details_overlay) ps_close_details(c);
    }, LV_EVENT_CLICKED, ctx);

    lv_obj_t* panel = lv_obj_create(ctx->details_overlay);
    lv_obj_set_pos(panel, (w - kPsDetailsW) / 2, (h - kPsDetailsH) / 2);
    lv_obj_set_size(panel, kPsDetailsW, kPsDetailsH);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_set_style_radius(panel, 4, 0);
    ui::rack_skin::apply_faceplate_panel(panel, true);
    ctx->details_body = panel;

    const ps_catalog::PatchStatus st = ps_status_for(ctx->last_statuses, item.id);
    ps_build_details_body(ctx, item, st, item.is_compatible_with_host(ps_catalog::detect_current_host_arch()));
}

static void ps_update_details(PatchStorageUiCtx* ctx) {
    if (!ctx || !ctx->details_overlay || ctx->details_patch_id < 0) return;
    const ps_catalog::PatchStorageItem* item = nullptr;
    for (const auto& it : ctx->last_state.items) {
        if (it.id == ctx->details_patch_id) {
            ctx->details_item = it;
            item = &ctx->details_item;
            break;
        }
    }
    if (!item) item = &ctx->details_item;

    const ps_catalog::PatchStatus st = ps_status_for(ctx->last_statuses, item->id);
    const bool host_compatible = item->is_compatible_with_host(ps_catalog::detect_current_host_arch());
    const bool art_ready =
        ctx->details_slot ? (ctx->details_slot->image != nullptr)
                          : (ps_catalog::PatchStorageService::instance().get_artwork(item->id) != nullptr);
    if (ps_details_sig(st, host_compatible, art_ready) != ctx->details_sig) {
        ps_build_details_body(ctx, *item, st, host_compatible);
    }
}

// ---------------------------------------------------------------------------
// Dropdowns
// ---------------------------------------------------------------------------

static void ps_style_dropdown(lv_obj_t* dd) {
    lv_obj_set_style_bg_color(dd, COLOR_CONTROL, 0);
    lv_obj_set_style_text_color(dd, COLOR_PRIMARY_TEXT, 0);
    lv_obj_set_style_border_color(dd, COLOR_EDGE, 0);
    lv_obj_set_style_border_width(dd, 1, 0);
    lv_obj_set_style_radius(dd, 3, 0);
    lv_obj_set_style_text_font(dd, &ac_font_12, 0);
    lv_obj_set_style_pad_ver(dd, 4, 0);
    lv_obj_set_style_pad_hor(dd, 8, 0);
    lv_obj_set_style_text_align(dd, LV_TEXT_ALIGN_LEFT, 0);
}

static void ps_configure_dropdown(lv_obj_t* dd, const char* options, uint32_t selected) {
    lv_dropdown_set_options(dd, options);
    lv_dropdown_set_selected(dd, selected);
    ps_style_dropdown(dd);
    // Pre-create the popup list once so it can be themed deterministically
    // before the first user interaction, then close it again.
    lv_dropdown_open(dd);
    lv_obj_t* list = lv_dropdown_get_list(dd);
    if (list) {
        lv_obj_set_style_max_height(list, 320, 0);
        lv_obj_set_style_bg_color(list, COLOR_PANEL_SURFACE, 0);
        lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(list, COLOR_EDGE, 0);
        lv_obj_set_style_border_width(list, 1, 0);
        lv_obj_set_style_radius(list, 3, 0);
        lv_obj_set_style_text_font(list, &ac_font_12, 0);
        lv_obj_set_style_text_color(list, COLOR_PRIMARY_TEXT, 0);
        lv_obj_set_style_pad_all(list, 6, 0);
        lv_obj_set_style_bg_color(list, lv_color_hex(0x1B232A), LV_PART_SELECTED);
        lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_SELECTED);
        lv_obj_set_style_radius(list, 2, LV_PART_SELECTED);
        lv_obj_set_style_bg_color(list, lv_color_hex(0x183848), static_cast<lv_style_selector_t>(LV_PART_SELECTED) | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(list, COLOR_CYAN, static_cast<lv_style_selector_t>(LV_PART_SELECTED) | LV_STATE_CHECKED);
    }
    lv_dropdown_close(dd);
}

static lv_obj_t* ps_make_dropdown(lv_obj_t* parent, const char* name, int x, int y, int w,
                                  const std::string& options, uint32_t selected) {
    lv_obj_t* dd = lv_dropdown_create(parent);
    lv_obj_set_pos(dd, x, y);
    lv_obj_set_size(dd, w, 28);
    lv_obj_set_name(dd, name);
    ps_configure_dropdown(dd, options.c_str(), selected);
    return dd;
}

static void ps_refresh_category_options(PatchStorageUiCtx* ctx) {
    if (!ctx || !ctx->cat_dd) return;
    std::string opts = "ALL CATEGORIES";
    for (const auto& cat : ctx->last_state.categories) {
        opts += '\n';
        opts += cat.name;
    }
    const uint32_t sel =
        static_cast<uint32_t>(std::max(0, std::min<int>(ctx->cat_idx, static_cast<int>(ctx->last_state.categories.size()))));
    lv_dropdown_set_options(ctx->cat_dd, opts.c_str());
    lv_dropdown_set_selected(ctx->cat_dd, sel);
}

static const ps_catalog::SortOption& ps_sort_option(const PatchStorageUiCtx* ctx) {
    const auto& opts = ps_catalog::sort_options();
    const int idx = std::max(0, std::min<int>(ctx ? ctx->sort_idx : 0, static_cast<int>(opts.size()) - 1));
    return opts[static_cast<size_t>(idx)];
}

static int ps_target_id(const PatchStorageUiCtx* ctx) {
    const auto& opts = ps_catalog::target_options();
    const int idx = std::max(0, std::min<int>(ctx ? ctx->target_idx : 0, static_cast<int>(opts.size()) - 1));
    return opts[static_cast<size_t>(idx)].id;
}

static int ps_category_id(const PatchStorageUiCtx* ctx) {
    if (!ctx || ctx->cat_idx <= 0) return 0;
    const size_t i = static_cast<size_t>(ctx->cat_idx - 1);
    if (i >= ctx->last_state.categories.size()) return 0;
    return ctx->last_state.categories[i].id;
}

static void ps_browse(PatchStorageUiCtx* ctx, int page) {
    if (!ctx) return;
    if (ctx->search_ta) {
        const char* txt = lv_textarea_get_text(ctx->search_ta);
        ctx->search_query = txt ? txt : "";
    }
    const ps_catalog::SortOption& sort = ps_sort_option(ctx);
    ps_catalog::PatchStorageService::instance().request_browse(page, ctx->search_query, ps_target_id(ctx),
                                                                ps_category_id(ctx), sort.orderby, sort.order);
}

// ---------------------------------------------------------------------------
// Tab content
// ---------------------------------------------------------------------------

// Re-applies the parts of the layout that depend on the tab area size. Called
// on populate and whenever the container is resized; the grid itself is
// recomputed by the poll timer (size_pending) to avoid layout reentrancy.
static void ps_apply_layout(PatchStorageUiCtx* ctx) {
    if (!ctx || !ctx->container || !ctx->cards_box) return;
    const int content_w = lv_obj_get_width(ctx->container) - 12;
    const int content_h = lv_obj_get_height(ctx->container) - 12;
    if (content_w < 320 || content_h < 320) return;  // not laid out yet

    // Pager controls positioned at bottom of list
    const int pager_btn_w = 88;
    const int pager_btn_h = 28;
    const int pager_y = content_h - pager_btn_h - 4;

    auto place = [](lv_obj_t* o, int x, int y) {
        if (!o) return;
        lv_obj_set_style_align(o, LV_ALIGN_DEFAULT, 0);
        lv_obj_set_pos(o, x, y);
    };

    place(ctx->prev_btn, 4, pager_y);
    if (ctx->prev_btn) lv_obj_set_size(ctx->prev_btn, pager_btn_w, pager_btn_h);

    place(ctx->next_btn, content_w - pager_btn_w - 4, pager_y);
    if (ctx->next_btn) lv_obj_set_size(ctx->next_btn, pager_btn_w, pager_btn_h);

    const int page_lbl_x = 4 + pager_btn_w + 12;
    const int page_lbl_w = std::max(80, (content_w - pager_btn_w - 4) - page_lbl_x - 12);
    place(ctx->page_lbl, page_lbl_x, pager_y + 4);
    if (ctx->page_lbl) {
        lv_obj_set_size(ctx->page_lbl, page_lbl_w, 20);
        lv_obj_set_style_text_align(ctx->page_lbl, LV_TEXT_ALIGN_CENTER, 0);
    }

    // Primary content area (cards grid) strictly ends above pager controls
    const int cards_y = 92;
    const int cards_h = std::max(120, pager_y - 6 - cards_y);
    place(ctx->cards_box, 4, cards_y);
    lv_obj_set_size(ctx->cards_box, content_w - 8, cards_h);

    if (ctx->status_lbl) lv_obj_set_size(ctx->status_lbl, content_w - 16, 16);

    // Search row: the field shrinks first, the SEARCH button follows it.
    const int search_w = std::max(140, std::min(386, content_w - 96));
    if (ctx->search_ta) lv_obj_set_width(ctx->search_ta, search_w);
    if (ctx->search_btn) lv_obj_set_x(ctx->search_btn, search_w + 8);

    // Right hand controls: placed as an explicit right-to-left chain from the
    // measured content width (dropdown widths are the known constants below).
    place(ctx->web_btn, content_w - 4 - 140, 4);
    place(ctx->refresh_btn, content_w - 172 - 96, 4);
    const int state_dd_x = content_w - 154;          // STATE dropdown, 150 wide
    const int state_cap_x = state_dd_x - 8 - 78;     // right aligned caption
    const int cat_dd_x = state_cap_x - 8 - 150;      // CATEGORY dropdown, 150 wide
    const int cat_cap_x = cat_dd_x - 8 - 78;
    place(ctx->state_dd, state_dd_x, 42);
    place(ctx->cat_dd, cat_dd_x, 42);
    if (ctx->state_cap) {
        lv_obj_set_style_text_align(ctx->state_cap, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_width(ctx->state_cap, 78);
    }
    if (ctx->cat_cap) {
        lv_obj_set_style_text_align(ctx->cat_cap, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_width(ctx->cat_cap, 78);
    }
    place(ctx->state_cap, state_cap_x, 47);
    place(ctx->cat_cap, cat_cap_x, 47);

    // Narrow widths: drop the least critical controls instead of overlapping.
    const bool show_filters = content_w >= 860;  // captions + right-hand filters
    const bool show_reset = content_w >= 826;
    const bool show_refresh = content_w >= 746;
    const bool show_web = content_w >= 620;
    auto set_vis = [](lv_obj_t* o, bool visible) {
        if (!o) return;
        if (visible) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    };
    for (lv_obj_t* cap : ctx->filter_caps) set_vis(cap, show_filters);
    set_vis(ctx->cat_dd, show_filters);
    set_vis(ctx->state_dd, show_filters);
    set_vis(ctx->reset_btn, show_reset);
    set_vis(ctx->refresh_btn, show_refresh);
    set_vis(ctx->web_btn, show_web);

    ctx->size_pending = true;
}

static void populate_patchstorage_view(lv_obj_t* list, FxScreenView* v) {
    lv_obj_clean(list);
    if (!v) return;

    auto* ctx = new PatchStorageUiCtx();
    ctx->view = v;
    ctx->container = list;
    ctx->target_idx = ps_catalog::default_target_index_for_host(ps_catalog::detect_current_host_arch());

    // The tab is re-populated every time it is opened, so the ctx cleanup is
    // attached to a sentinel child: lv_obj_clean() (here or in the sibling tab
    // populator) deletes it and thereby the previous timer + ctx.
    lv_obj_t* guard = lv_obj_create(list);
    lv_obj_set_size(guard, 1, 1);
    lv_obj_add_flag(guard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(guard, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(guard, [](lv_event_t* e) {
        auto* c = static_cast<PatchStorageUiCtx*>(lv_event_get_user_data(e));
        if (!c) return;
        if (c->poll_timer) {
            lv_timer_delete(c->poll_timer);
            c->poll_timer = nullptr;
        }
        delete c;
    }, LV_EVENT_DELETE, ctx);

    // Snapshot the current service state (also seeds the category dropdown).
    {
        ps_catalog::BrowseState st;
        std::unordered_map<int, ps_catalog::PatchStatus> statuses;
        ps_catalog::PatchStorageService::instance().poll_state(st, statuses);
        ctx->last_state = std::move(st);
        ctx->last_statuses = std::move(statuses);
        ctx->last_generation = ctx->last_state.generation;
        ctx->last_categories_loaded = ctx->last_state.categories_loaded;
        ctx->last_categories_size = ctx->last_state.categories.size();
        ctx->status_fp = ps_status_fingerprint(ctx->last_state, ctx->last_statuses);
    }

    // --- Row 1: prominent search + non-dominant global actions ---
    ctx->search_ta = lv_textarea_create(list);
    lv_obj_set_pos(ctx->search_ta, 4, 4);
    lv_obj_set_size(ctx->search_ta, 386, 32);
    lv_obj_set_name(ctx->search_ta, "ps_search");
    lv_textarea_set_placeholder_text(ctx->search_ta, "Search the PatchStorage LV2 catalog...");
    lv_textarea_set_one_line(ctx->search_ta, true);
    lv_obj_set_style_text_font(ctx->search_ta, &ac_font_12, 0);
    lv_obj_set_style_bg_color(ctx->search_ta, COLOR_CONTROL, 0);
    lv_obj_set_style_text_color(ctx->search_ta, COLOR_PRIMARY_TEXT, 0);
    lv_obj_set_style_border_color(ctx->search_ta, COLOR_EDGE, 0);
    lv_obj_set_style_border_width(ctx->search_ta, 1, 0);
    lv_obj_set_style_radius(ctx->search_ta, 3, 0);
    lv_obj_set_style_pad_hor(ctx->search_ta, 8, 0);

    lv_obj_t* search_btn = ps_button(list, "SEARCH", 394, 4, 84, 32, true);
    ctx->search_btn = search_btn;
    lv_obj_set_name(search_btn, "ps_search_btn");
    lv_obj_add_event_cb(search_btn, [](lv_event_t* e) {
        auto* c = static_cast<PatchStorageUiCtx*>(lv_event_get_user_data(e));
        if (c) ps_browse(c, 1);
    }, LV_EVENT_CLICKED, ctx);
    lv_obj_add_event_cb(ctx->search_ta, [](lv_event_t* e) {
        auto* c = static_cast<PatchStorageUiCtx*>(lv_event_get_user_data(e));
        if (c) ps_browse(c, 1);
    }, LV_EVENT_READY, ctx);

    lv_obj_t* reset_btn = ps_button(list, "RESET", 482, 4, 76, 32, false);
    ctx->reset_btn = reset_btn;
    lv_obj_set_name(reset_btn, "ps_reset_btn");
    lv_obj_add_event_cb(reset_btn, [](lv_event_t* e) {
        auto* c = static_cast<PatchStorageUiCtx*>(lv_event_get_user_data(e));
        if (!c) return;
        if (c->search_ta) lv_textarea_set_text(c->search_ta, "");
        c->search_query.clear();
        c->sort_idx = 0;
        c->target_idx = ps_catalog::default_target_index_for_host(ps_catalog::detect_current_host_arch());
        c->cat_idx = 0;
        c->state_idx = 0;
        if (c->sort_dd) lv_dropdown_set_selected(c->sort_dd, 0);
        if (c->target_dd) lv_dropdown_set_selected(c->target_dd, static_cast<uint32_t>(c->target_idx));
        if (c->cat_dd) lv_dropdown_set_selected(c->cat_dd, 0);
        if (c->state_dd) lv_dropdown_set_selected(c->state_dd, 0);
        ps_browse(c, 1);
    }, LV_EVENT_CLICKED, ctx);

    lv_obj_t* refresh_btn = ps_button(list, "REFRESH", 692, 4, 96, 32, false);
    ctx->refresh_btn = refresh_btn;
    lv_obj_set_name(refresh_btn, "ps_refresh_btn");
    lv_obj_add_event_cb(refresh_btn, [](lv_event_t* e) {
        auto* c = static_cast<PatchStorageUiCtx*>(lv_event_get_user_data(e));
        if (!c) return;
        ps_catalog::PatchStorageService::instance().reset_artwork_failures();
        ps_catalog::PatchStorageService::instance().request_categories();
        ps_browse(c, c->current_page);
    }, LV_EVENT_CLICKED, ctx);

    lv_obj_t* web_cat_btn = ps_button(list, "VIEW WEB CATALOG", 792, 4, 140, 32, false);
    ctx->web_btn = web_cat_btn;
    lv_obj_set_name(web_cat_btn, "ps_web_btn");
    lv_obj_add_event_cb(web_cat_btn, [](lv_event_t*) {
        ps_catalog::PatchStorageClient::open_in_browser("https://patchstorage.com/platform/lv2/");
    }, LV_EVENT_CLICKED, nullptr);

    // --- Row 2: filters (only filters the API actually supports) ---
    auto make_caption = [&](const char* text, int x) {
        lv_obj_t* l = lv_label_create(list);
        lv_label_set_text(l, text);
        lv_obj_set_pos(l, x, 47);
        lv_obj_set_style_text_font(l, &ac_font_12, 0);
        lv_obj_set_style_text_color(l, COLOR_TEXT_MUTED, 0);
        return l;
    };
    ctx->filter_caps[0] = make_caption("SORT", 8);
    ctx->filter_caps[1] = make_caption("TARGET", 200);
    ctx->cat_cap = make_caption("CATEGORY", 440);
    ctx->filter_caps[2] = ctx->cat_cap;
    ctx->state_cap = make_caption("STATE", 668);
    ctx->filter_caps[3] = ctx->state_cap;

    std::string sort_opts;
    for (const auto& o : ps_catalog::sort_options()) {
        if (!sort_opts.empty()) sort_opts += '\n';
        sort_opts += o.label;
    }
    std::string target_opts;
    for (const auto& o : ps_catalog::target_options()) {
        if (!target_opts.empty()) target_opts += '\n';
        target_opts += o.label;
    }
    std::string cat_opts = "ALL CATEGORIES";
    for (const auto& cat : ctx->last_state.categories) {
        cat_opts += '\n';
        cat_opts += cat.name;
    }

    ctx->sort_dd = ps_make_dropdown(list, "ps_sort", 52, 42, 132, sort_opts, static_cast<uint32_t>(ctx->sort_idx));
    ctx->target_dd = ps_make_dropdown(list, "ps_target", 256, 42, 168, target_opts, static_cast<uint32_t>(ctx->target_idx));
    ctx->cat_dd = ps_make_dropdown(list, "ps_cat", 502, 42, 150, cat_opts, static_cast<uint32_t>(ctx->cat_idx));
    ctx->state_dd = ps_make_dropdown(list, "ps_state", 714, 42, 150,
                                     "ANY STATE\nINSTALLED\nNOT INSTALLED\nHOST COMPATIBLE\nUNSUPPORTED ON HOST",
                                     static_cast<uint32_t>(ctx->state_idx));

    lv_obj_add_event_cb(ctx->sort_dd, [](lv_event_t* e) {
        auto* c = static_cast<PatchStorageUiCtx*>(lv_event_get_user_data(e));
        if (!c || !c->sort_dd) return;
        c->sort_idx = static_cast<int>(lv_dropdown_get_selected(c->sort_dd));
        ps_browse(c, 1);
    }, LV_EVENT_VALUE_CHANGED, ctx);

    lv_obj_add_event_cb(ctx->target_dd, [](lv_event_t* e) {
        auto* c = static_cast<PatchStorageUiCtx*>(lv_event_get_user_data(e));
        if (!c || !c->target_dd) return;
        c->target_idx = static_cast<int>(lv_dropdown_get_selected(c->target_dd));
        ps_browse(c, 1);
    }, LV_EVENT_VALUE_CHANGED, ctx);

    lv_obj_add_event_cb(ctx->cat_dd, [](lv_event_t* e) {
        auto* c = static_cast<PatchStorageUiCtx*>(lv_event_get_user_data(e));
        if (!c || !c->cat_dd) return;
        c->cat_idx = static_cast<int>(lv_dropdown_get_selected(c->cat_dd));
        ps_browse(c, 1);
    }, LV_EVENT_VALUE_CHANGED, ctx);

    lv_obj_add_event_cb(ctx->state_dd, [](lv_event_t* e) {
        auto* c = static_cast<PatchStorageUiCtx*>(lv_event_get_user_data(e));
        if (!c || !c->state_dd) return;
        c->state_idx = static_cast<int>(lv_dropdown_get_selected(c->state_dd));
        // Pure client-side filter over the current page: no refetch, the
        // search + API filters stay untouched.
        render_patchstorage_cards(c, c->last_state, c->last_statuses);
    }, LV_EVENT_VALUE_CHANGED, ctx);

    // --- Status line ---
    ctx->status_lbl = lv_label_create(list);
    lv_label_set_text(ctx->status_lbl, "Connecting to PatchStorage (LV2 platform #8046)...");
    lv_obj_set_pos(ctx->status_lbl, 8, 74);
    lv_obj_set_size(ctx->status_lbl, 900, 16);
    lv_label_set_long_mode(ctx->status_lbl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_font(ctx->status_lbl, &ac_font_12, 0);
    lv_obj_set_style_text_color(ctx->status_lbl, COLOR_TEXT_MUTED, 0);

    // --- Card grid ---
    ctx->cards_box = lv_obj_create(list);
    lv_obj_set_pos(ctx->cards_box, 4, 92);
    lv_obj_set_size(ctx->cards_box, 900, 380);
    lv_obj_set_name(ctx->cards_box, "ps_cards");
    ui::rack_skin::apply_recessed_display(ctx->cards_box, lv_color_hex(0x090C10), COLOR_EDGE);
    lv_obj_set_style_pad_all(ctx->cards_box, 8, 0);
    lv_obj_add_event_cb(ctx->cards_box, [](lv_event_t* e) {
        auto* c = static_cast<PatchStorageUiCtx*>(lv_event_get_user_data(e));
        if (c) c->size_pending = true;
    }, LV_EVENT_SIZE_CHANGED, ctx);

    // Container resize (responsive dialogs / window changes): re-apply the
    // dependent geometry; the grid reflows on the next poll tick.
    lv_obj_add_event_cb(list, [](lv_event_t* e) {
        auto* c = static_cast<PatchStorageUiCtx*>(lv_event_get_user_data(e));
        ps_apply_layout(c);
    }, LV_EVENT_SIZE_CHANGED, ctx);

    // --- Pagination (kept clear of the modal's CANCEL button at bottom right) ---
    ctx->prev_btn = ps_button(list, "< PREV", 4, 476, 88, 26, false);
    lv_obj_set_name(ctx->prev_btn, "ps_prev_btn");
    lv_obj_add_event_cb(ctx->prev_btn, [](lv_event_t* e) {
        auto* c = static_cast<PatchStorageUiCtx*>(lv_event_get_user_data(e));
        if (c && c->current_page > 1) ps_browse(c, c->current_page - 1);
    }, LV_EVENT_CLICKED, ctx);

    ctx->page_lbl = lv_label_create(list);
    lv_label_set_text(ctx->page_lbl, "PAGE 1 / 1");
    lv_obj_set_pos(ctx->page_lbl, 104, 480);
    lv_obj_set_size(ctx->page_lbl, 574, 18);
    lv_label_set_long_mode(ctx->page_lbl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_style_text_font(ctx->page_lbl, &ac_font_12, 0);
    lv_obj_set_style_text_color(ctx->page_lbl, COLOR_TEXT2, 0);

    ctx->next_btn = ps_button(list, "NEXT >", 700, 476, 88, 26, false);
    lv_obj_set_name(ctx->next_btn, "ps_next_btn");
    lv_obj_add_event_cb(ctx->next_btn, [](lv_event_t* e) {
        auto* c = static_cast<PatchStorageUiCtx*>(lv_event_get_user_data(e));
        if (c && c->current_page < c->total_pages) ps_browse(c, c->current_page + 1);
    }, LV_EVENT_CLICKED, ctx);

    // --- Poll timer: selective rebuilds, in-place artwork, live details ---
    ctx->poll_timer = lv_timer_create([](lv_timer_t* t) {
        auto* c = static_cast<PatchStorageUiCtx*>(lv_timer_get_user_data(t));
        if (!c || !c->cards_box) return;

        ps_catalog::BrowseState st;
        std::unordered_map<int, ps_catalog::PatchStatus> statuses;
        ps_catalog::PatchStorageService::instance().poll_state(st, statuses);

        const int prev_generation = c->last_generation;
        const uint32_t prev_fp = c->status_fp;

        c->last_state = std::move(st);
        c->last_statuses = std::move(statuses);
        c->last_generation = c->last_state.generation;
        c->status_fp = ps_status_fingerprint(c->last_state, c->last_statuses);

        if (c->last_state.categories_loaded != c->last_categories_loaded ||
            c->last_state.categories.size() != c->last_categories_size) {
            c->last_categories_loaded = c->last_state.categories_loaded;
            c->last_categories_size = c->last_state.categories.size();
            ps_refresh_category_options(c);
        }

        if (prev_generation != c->last_generation || prev_fp != c->status_fp || c->size_pending) {
            c->size_pending = false;
            render_patchstorage_cards(c, c->last_state, c->last_statuses);
        }

        ps_update_artwork_slots(c);
        ps_update_details(c);
    }, 250, ctx);

    // --- Initial load ---
    ps_catalog::PatchStorageService::instance().request_categories();
    ps_apply_layout(ctx);

    if (ctx->last_state.items.empty() && !ctx->last_state.is_loading) {
        ps_browse(ctx, 1);
        ps_catalog::BrowseState st2;
        std::unordered_map<int, ps_catalog::PatchStatus> statuses2;
        ps_catalog::PatchStorageService::instance().poll_state(st2, statuses2);
        ctx->last_state = std::move(st2);
        ctx->last_statuses = std::move(statuses2);
        ctx->last_generation = ctx->last_state.generation;
        ctx->status_fp = ps_status_fingerprint(ctx->last_state, ctx->last_statuses);
    }
    render_patchstorage_cards(ctx, ctx->last_state, ctx->last_statuses);
}

void show_add_plugin_modal(FxScreenView* v) {
    if (!v) return;
    ChannelState* ch = resolve_target_channel(v);
    if (!ch) return;
    if (isMasterChannel(ch)) return;
    if (ch->fx_rack.count() >= PluginRack::MAX_SLOTS) return;

    v->pending_close_modal = false;

    if (v->add_modal) {
        lv_obj_t* old_modal = v->add_modal;
        v->add_modal = nullptr;
        lv_obj_delete(old_modal);
    }

    lv_display_t* disp = lv_display_get_default();
    const int32_t disp_w = disp ? lv_display_get_horizontal_resolution(disp) : 1024;
    const int32_t disp_h = disp ? lv_display_get_vertical_resolution(disp) : 768;

    auto* backdrop = lv_obj_create(lv_layer_top());
    v->add_modal = backdrop;
    lv_obj_set_size(backdrop, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(backdrop, 0, 0);
    lv_obj_remove_flag(backdrop, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(backdrop, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_70, 0);
    lv_obj_set_style_border_width(backdrop, 0, 0);

    lv_obj_add_event_cb(backdrop, [](lv_event_t* e) {
        auto* view = static_cast<FxScreenView*>(lv_event_get_user_data(e));
        if (view) view->add_modal = nullptr;
    }, LV_EVENT_DELETE, v);

    const int margin_x = std::clamp(disp_w / 32, 16, 28);
    const int margin_y = std::clamp(disp_h / 32, 16, 24);
    const int max_w = std::min(980, disp_w - 2 * margin_x);
    const int max_h = std::min(660, disp_h - 2 * margin_y);
    const int dialog_w = std::clamp(max_w, 360, 980);
    const int dialog_h = std::clamp(max_h, 300, 660);

    auto* dialog = lv_obj_create(backdrop);
    lv_obj_set_size(dialog, dialog_w, dialog_h);
    lv_obj_center(dialog);
    lv_obj_remove_flag(dialog, LV_OBJ_FLAG_SCROLLABLE);
    ui::rack_skin::apply_chassis_panel(dialog, COLOR_CYAN);
    lv_obj_set_style_border_width(dialog, 2, 0);
    lv_obj_set_style_pad_all(dialog, 16, 0);
    ui::rack_skin::add_faceplate_corner_screws(dialog, dialog_w, dialog_h, 6);

    char hdr_buf[128];
    std::snprintf(hdr_buf, sizeof(hdr_buf), "ASSIGN PROCESSOR OR PEDALBOARD PRESET TO %s", ch->name);
    instrument_label(dialog, hdr_buf, 16, 8, &ac_font_16, COLOR_CYAN);
    instrument_label(dialog, "BUILT-IN DSP  |  NATIVE LV2 HOSTING  |  NAM LITE NEURAL MODELS  |  PRESETS  |  PATCHSTORAGE",
                     16, 30, &ac_font_12, COLOR_TEXT2);

    // Filter Tabs (6 tabs including PatchStorage)
    static const char* kTabNames[6] = {"ALL", "BUILT-IN", "LV2 PLUGINS", "NAM MODELS", "PRESETS", "PATCHSTORAGE"};
    static const char* kTabNamesCompact[6] = {"ALL", "BUILT-IN", "LV2", "NAM", "PRESETS", "PATCHSTOR"};

    struct TabModalState {
        lv_obj_t* dialog{nullptr};
        lv_obj_t* list{nullptr};
        lv_obj_t* cancel_btn{nullptr};
        FxScreenView* v{nullptr};
        int current_tab{0};
        std::array<lv_obj_t*, 6> tab_btns{};
    };

    auto state = std::make_shared<TabModalState>();
    state->dialog = dialog;
    state->v = v;
    state->current_tab = 0;

    const int tab_gap = 6;
    const int tab_w = std::max(60, (dialog_w - 24 - 5 * tab_gap) / 6);
    const int tab_h = 34;
    const int tab_y = 52;

    struct TabBtnCtx {
        std::shared_ptr<TabModalState> state;
        int tab_index{0};
    };

    for (int t = 0; t < 6; ++t) {
        auto* tb = lv_button_create(dialog);
        state->tab_btns[t] = tb;
        lv_obj_set_pos(tb, 12 + t * (tab_w + tab_gap), tab_y);
        lv_obj_set_size(tb, tab_w, tab_h);
        lv_obj_set_style_pad_hor(tb, 4, 0);
        lv_obj_set_style_pad_ver(tb, 2, 0);
        const bool active = (t == state->current_tab);
        ui::rack_skin::apply_hardware_button(tb, active, COLOR_CYAN,
                                             active ? lv_color_hex(0x183848) : lv_color_hex(0x14181F),
                                             lv_color_hex(0x1B232A));
        auto* tl = lv_label_create(tb);
        const char* tab_label = (dialog_w < 680) ? kTabNamesCompact[t] : kTabNames[t];
        lv_label_set_text(tl, tab_label);
        lv_obj_center(tl);
        lv_obj_set_style_text_font(tl, &ac_font_12, 0);
        lv_obj_set_style_text_color(tl, active ? COLOR_CYAN : COLOR_TEXT2, 0);

        auto* tctx = new TabBtnCtx{state, t};
        lv_obj_add_event_cb(tb, [](lv_event_t* e) {
            auto* c = static_cast<TabBtnCtx*>(lv_event_get_user_data(e));
            if (!c || !c->state) return;
            auto s = c->state;
            s->current_tab = c->tab_index;
            for (int i = 0; i < 6; ++i) {
                lv_obj_t* btn_obj = s->tab_btns[i];
                if (btn_obj) {
                    const bool is_act = (i == s->current_tab);
                    ui::rack_skin::apply_hardware_button(
                        btn_obj, is_act, COLOR_CYAN,
                        is_act ? lv_color_hex(0x183848) : lv_color_hex(0x14181F),
                        lv_color_hex(0x1B232A));
                    if (lv_obj_get_child_count(btn_obj) > 0) {
                        lv_obj_t* child_lbl = lv_obj_get_child(btn_obj, 0);
                        if (child_lbl) {
                            lv_obj_set_style_text_color(child_lbl, is_act ? COLOR_CYAN : COLOR_TEXT2, 0);
                        }
                    }
                }
            }
            if (s->list && s->v) {
                if (s->current_tab == 5) {
                    populate_patchstorage_view(s->list, s->v);
                } else {
                    populate_add_modal_list(s->list, s->v, s->current_tab);
                }
            }
        }, LV_EVENT_CLICKED, tctx);

        lv_obj_add_event_cb(tb, [](lv_event_t* e) {
            delete static_cast<TabBtnCtx*>(lv_event_get_user_data(e));
        }, LV_EVENT_DELETE, tctx);
    }

    const int cancel_w = 120;
    const int cancel_h = 34;
    const int cancel_x = dialog_w - 16 - cancel_w;
    const int cancel_y = dialog_h - 10 - cancel_h;

    const int list_x = 12;
    const int list_y = tab_y + tab_h + 8;
    const int list_w = dialog_w - 24;
    const int list_h = std::max(160, cancel_y - 6 - list_y);

    auto* list = lv_obj_create(dialog);
    state->list = list;
    lv_obj_set_pos(list, list_x, list_y);
    lv_obj_set_size(list, list_w, list_h);
    ui::rack_skin::apply_recessed_display(list, lv_color_hex(0x0B0E12), COLOR_EDGE);
    lv_obj_set_style_pad_all(list, 6, 0);

    auto* cancel_btn = lv_button_create(dialog);
    state->cancel_btn = cancel_btn;
    lv_obj_set_size(cancel_btn, cancel_w, cancel_h);
    lv_obj_set_pos(cancel_btn, cancel_x, cancel_y);
    ui::rack_skin::apply_hardware_button(cancel_btn, false, COLOR_EDGE, COLOR_CONTROL, lv_color_hex(0x20262D));
    auto* cl = lv_label_create(cancel_btn);
    lv_label_set_text(cl, "CANCEL");
    lv_obj_center(cl);
    lv_obj_set_style_text_color(cl, COLOR_TEXT2, 0);
    lv_obj_add_event_cb(cancel_btn, [](lv_event_t* e) {
        auto* view = static_cast<FxScreenView*>(lv_event_get_user_data(e));
        if (view) {
            schedule_ui_rebuild(view, false, false, true);
        }
    }, LV_EVENT_CLICKED, v);

    // Responsive relayout on dialog resize
    lv_obj_add_event_cb(dialog, [](lv_event_t* e) {
        auto* s = static_cast<TabModalState*>(lv_event_get_user_data(e));
        if (!s || !s->dialog) return;
        const int dw = lv_obj_get_width(s->dialog);
        const int dh = lv_obj_get_height(s->dialog);
        const int t_gap = 6;
        const int t_w = std::max(60, (dw - 24 - 5 * t_gap) / 6);
        const int t_h = 34;
        const int t_y = 52;
        for (int t = 0; t < 6; ++t) {
            if (s->tab_btns[t]) {
                lv_obj_set_pos(s->tab_btns[t], 12 + t * (t_w + t_gap), t_y);
                lv_obj_set_size(s->tab_btns[t], t_w, t_h);
            }
        }
        const int c_w = 120;
        const int c_h = 34;
        const int c_x = dw - 16 - c_w;
        const int c_y = dh - 10 - c_h;
        if (s->cancel_btn) {
            lv_obj_set_pos(s->cancel_btn, c_x, c_y);
            lv_obj_set_size(s->cancel_btn, c_w, c_h);
        }
        if (s->list) {
            const int l_y = t_y + t_h + 8;
            lv_obj_set_pos(s->list, 12, l_y);
            lv_obj_set_size(s->list, dw - 24, std::max(160, c_y - 6 - l_y));
        }
    }, LV_EVENT_SIZE_CHANGED, state.get());

    if (state->current_tab == 5) {
        populate_patchstorage_view(list, v);
    } else {
        populate_add_modal_list(list, v, state->current_tab);
    }
}

void build_rack(FxScreenView* v) {
    ChannelState* ch = resolve_target_channel(v);
    if (!v || !ch || !v->slot_container) return;
    lv_obj_clean(v->slot_container);
    update_target_buttons(v);
    v->last_structure_signature = compute_structure_signature(ch);

    const size_t count = ch->fx_rack.count();
    if (count == 0) {
        v->selected_slot = 0;
    } else if (v->selected_slot < 0 || v->selected_slot >= static_cast<int>(count)) {
        v->selected_slot = static_cast<int>(count) - 1;
    }

    int cur_y = 6;

    // Input stage header (Recessed patchbay strip)
    auto* in_node = lv_obj_create(v->slot_container);
    lv_obj_set_pos(in_node, 12, cur_y);
    lv_obj_set_size(in_node, 608, 36);
    lv_obj_remove_flag(in_node, LV_OBJ_FLAG_SCROLLABLE);
    ui::rack_skin::apply_recessed_display(in_node, COLOR_LCD_BG, COLOR_SUBTLE_BORDER);
    ui::rack_skin::create_bezel_led(in_node, 12, 12, COLOR_CYAN, true);
    char in_txt[128];
    std::snprintf(in_txt, sizeof(in_txt), "RACK INPUT TAP:  %s (%s  |  %s)",
                  ch->name,
                  ch->input_label,
                  ch->format == ChannelFormat::Stereo ? "2-CH STEREO" : "1-CH MONO");
    instrument_label(in_node, in_txt, 30, 9, &ac_font_12, COLOR_TEXT_MUTED);
    cur_y += 44;

    for (size_t i = 0; i < count; ++i) {
        auto* inst = ch->fx_rack.get_slot(i);
        if (!inst) continue;

        const bool is_selected = (static_cast<int>(i) == v->selected_slot);
        const bool bypassed = inst->is_bypassed();

        auto* slot_card = lv_obj_create(v->slot_container);
        lv_obj_set_pos(slot_card, 12, cur_y);
        lv_obj_set_size(slot_card, 608, 82);
        lv_obj_remove_flag(slot_card, LV_OBJ_FLAG_SCROLLABLE);
        ui::rack_skin::apply_faceplate_panel(
            slot_card,
            is_selected ? COLOR_SELECTED_BORDER : COLOR_STANDARD_BORDER,
            bypassed ? COLOR_RECESSED : (is_selected ? COLOR_RAISED_PANEL : COLOR_PANEL_SURFACE));
        lv_obj_set_style_border_width(slot_card, is_selected ? 2 : 1, 0);
        lv_obj_set_style_pad_all(slot_card, 6, 0);

        // 1U Rack Ear Screws (Left & Right Mounting Flanges)
        ui::rack_skin::create_rack_screw(slot_card, 2, 4);
        ui::rack_skin::create_rack_screw(slot_card, 2, 56);
        ui::rack_skin::create_rack_screw(slot_card, 580, 4);
        ui::rack_skin::create_rack_screw(slot_card, 580, 56);

        char slot_hdr[112];
        const char* route_mode = (isAuxBusChannel(ch) && inst->prefers_send_return_on_aux())
            ? "SEND/RETURN WET"
            : "INSERT";
        std::snprintf(slot_hdr, sizeof(slot_hdr), "UNIT %02d  •  %s  •  #%llu  •  %s",
                      static_cast<int>(i) + 1,
                      inst->get_category().c_str(),
                      static_cast<unsigned long long>(inst->get_instance_id()),
                      route_mode);
        instrument_label(slot_card, slot_hdr, 22, 2, &ac_font_12, is_selected ? COLOR_PRIMARY_TEXT : COLOR_SECONDARY_TEXT);

        lv_color_t led_color = bypassed ? COLOR_ORANGE : COLOR_GREEN;
        ui::rack_skin::create_bezel_led(slot_card, 22, 26, led_color, true);

        auto* name_lbl = lv_label_create(slot_card);
        lv_label_set_text(name_lbl, inst->get_name().c_str());
        lv_obj_set_pos(name_lbl, 40, 23);
        lv_obj_set_style_text_font(name_lbl, &ac_font_16, 0);
        lv_obj_set_style_text_color(name_lbl, bypassed ? COLOR_SECONDARY_TEXT : (is_selected ? COLOR_PRIMARY_TEXT : COLOR_PRIMARY_TEXT), 0);

        // Quick parameter summary line
        char summary[112]{};
        if (inst->get_uri() == "http://audiocore.org/plugins/reverb") {
            const int rt = std::clamp(static_cast<int>(std::round(inst->get_parameter_value("room_type", 1.0f))), 0, 3);
            static const char* kRooms[4] = {"ROOM", "HALL", "PLATE", "AMBIENCE"};
            std::snprintf(summary, sizeof(summary), "%s  •  RT60 %.1fs  •  Pre %.0fms  •  Mix %.0f%%",
                          kRooms[rt],
                          inst->get_parameter_value("decay", 2.2f),
                          inst->get_parameter_value("predelay", 20.0f),
                          inst->get_parameter_value("mix", 0.30f) * 100.0f);
        } else if (inst->get_uri() == "http://audiocore.org/plugins/delay") {
            const bool sync = inst->get_parameter_value("sync_en", 0.0f) >= 0.5f;
            std::snprintf(summary, sizeof(summary), "%s  •  L %.0fms / R %.0fms  •  FB %.0f%%  •  Mix %.0f%%",
                          sync ? "SYNC" : "MANUAL",
                          inst->get_parameter_value("time_l", 320.0f),
                          inst->get_parameter_value("time_r", 480.0f),
                          inst->get_parameter_value("feedback", 0.35f) * 100.0f,
                          inst->get_parameter_value("mix", 0.25f) * 100.0f);
        } else if (inst->get_uri() == "http://audiocore.org/plugins/afs") {
            std::snprintf(summary, sizeof(summary), "Sens %.0f%%  •  Max %d Notches  •  Depth %.1fdB  •  Q %.0f",
                          inst->get_parameter_value("sensitivity", 0.75f) * 100.0f,
                          static_cast<int>(std::round(inst->get_parameter_value("max_notches", 8.0f))),
                          inst->get_parameter_value("max_depth", -18.0f),
                          inst->get_parameter_value("q", 15.0f));
        } else if (inst->get_uri() == "http://audiocore.org/plugins/limiter") {
            std::snprintf(summary, sizeof(summary), "Ceiling %.1f dB  •  Release %.0f ms  •  Lookahead %.1f ms",
                          inst->get_parameter_value("ceiling", -0.1f),
                          inst->get_parameter_value("release", 50.0f),
                          inst->get_parameter_value("lookahead", 2.0f));
        } else {
            std::snprintf(summary, sizeof(summary), "%s", inst->get_description().c_str());
        }
        instrument_label(slot_card, summary, 40, 48, &ac_font_12, COLOR_TEXT2);

        struct SlotCtx { FxScreenView* v; size_t idx; };
        auto* sc = new SlotCtx{v, i};
        lv_obj_add_event_cb(slot_card, [](lv_event_t* e) {
            auto* c = static_cast<SlotCtx*>(lv_event_get_user_data(e));
            if (!c || !c->v) return;
            FxScreenView* view = c->v;
            view->selected_slot = static_cast<int>(c->idx);
            schedule_ui_rebuild(view, true, true);
        }, LV_EVENT_CLICKED, sc);
        lv_obj_add_event_cb(slot_card, [](lv_event_t* e) {
            delete static_cast<SlotCtx*>(lv_event_get_user_data(e));
        }, LV_EVENT_DELETE, sc);

        // Move Up button
        auto* up_btn = lv_button_create(slot_card);
        lv_obj_set_pos(up_btn, 348, 20);
        lv_obj_set_size(up_btn, 36, 34);
        ui::rack_skin::apply_hardware_button(up_btn, false, COLOR_EDGE, COLOR_PANEL, lv_color_hex(0x161A20));
        auto* up_lbl = lv_label_create(up_btn);
        lv_label_set_text(up_lbl, "UP");
        lv_obj_center(up_lbl);
        lv_obj_set_style_text_font(up_lbl, &ac_font_12, 0);
        lv_obj_set_style_text_color(up_lbl, i > 0 ? COLOR_TEXT : COLOR_TEXT2, 0);
        if (i == 0) lv_obj_add_state(up_btn, LV_STATE_DISABLED);

        auto* uc = new SlotCtx{v, i};
        lv_obj_add_event_cb(up_btn, [](lv_event_t* e) {
            auto* c = static_cast<SlotCtx*>(lv_event_get_user_data(e));
            if (!c || !c->v) return;
            FxScreenView* view = c->v;
            const size_t idx = c->idx;
            ChannelState* target = resolve_target_channel(view);
            if (target && target->fx_rack.move_up(idx)) {
                view->selected_slot = static_cast<int>(idx) - 1;
                sync_channel_fx_to_dsp(target);
                schedule_ui_rebuild(view, true, true);
            }
        }, LV_EVENT_CLICKED, uc);
        lv_obj_add_event_cb(up_btn, [](lv_event_t* e) {
            delete static_cast<SlotCtx*>(lv_event_get_user_data(e));
        }, LV_EVENT_DELETE, uc);

        // Move Down button
        auto* dn_btn = lv_button_create(slot_card);
        lv_obj_set_pos(dn_btn, 388, 20);
        lv_obj_set_size(dn_btn, 36, 34);
        ui::rack_skin::apply_hardware_button(dn_btn, false, COLOR_EDGE, COLOR_PANEL, lv_color_hex(0x161A20));
        auto* dn_lbl = lv_label_create(dn_btn);
        lv_label_set_text(dn_lbl, "DN");
        lv_obj_center(dn_lbl);
        lv_obj_set_style_text_font(dn_lbl, &ac_font_12, 0);
        lv_obj_set_style_text_color(dn_lbl, (i + 1 < count) ? COLOR_TEXT : COLOR_TEXT2, 0);
        if (i + 1 >= count) lv_obj_add_state(dn_btn, LV_STATE_DISABLED);

        auto* dc = new SlotCtx{v, i};
        lv_obj_add_event_cb(dn_btn, [](lv_event_t* e) {
            auto* c = static_cast<SlotCtx*>(lv_event_get_user_data(e));
            if (!c || !c->v) return;
            FxScreenView* view = c->v;
            const size_t idx = c->idx;
            ChannelState* target = resolve_target_channel(view);
            if (target && target->fx_rack.move_down(idx)) {
                view->selected_slot = static_cast<int>(idx) + 1;
                sync_channel_fx_to_dsp(target);
                schedule_ui_rebuild(view, true, true);
            }
        }, LV_EVENT_CLICKED, dc);
        lv_obj_add_event_cb(dn_btn, [](lv_event_t* e) {
            delete static_cast<SlotCtx*>(lv_event_get_user_data(e));
        }, LV_EVENT_DELETE, dc);

        // Bypass button
        auto* bpass_btn = lv_button_create(slot_card);
        lv_obj_set_pos(bpass_btn, 430, 20);
        lv_obj_set_size(bpass_btn, 84, 34);
        ui::rack_skin::apply_hardware_button(
            bpass_btn, bypassed, COLOR_ORANGE, lv_color_hex(0x4E3209), lv_color_hex(0x161A20));
        auto* bpass_lbl = lv_label_create(bpass_btn);
        lv_label_set_text(bpass_lbl, bypassed ? "BYP ON" : "BYPASS");
        lv_obj_center(bpass_lbl);
        lv_obj_set_style_text_font(bpass_lbl, &ac_font_12, 0);
        lv_obj_set_style_text_color(bpass_lbl, bypassed ? COLOR_ORANGE : COLOR_TEXT2, 0);

        auto* bc = new SlotCtx{v, i};
        lv_obj_add_event_cb(bpass_btn, [](lv_event_t* e) {
            auto* c = static_cast<SlotCtx*>(lv_event_get_user_data(e));
            if (!c || !c->v) return;
            FxScreenView* view = c->v;
            const size_t idx = c->idx;
            ChannelState* target = resolve_target_channel(view);
            if (target) {
                if (auto* p = target->fx_rack.get_slot(idx)) {
                    p->set_bypassed(!p->is_bypassed());
                    sync_channel_fx_to_dsp(target);
                    view->last_param_fingerprint = compute_rack_fingerprint(target);
                    schedule_ui_rebuild(view, true, true);
                }
            }
        }, LV_EVENT_CLICKED, bc);
        lv_obj_add_event_cb(bpass_btn, [](lv_event_t* e) {
            delete static_cast<SlotCtx*>(lv_event_get_user_data(e));
        }, LV_EVENT_DELETE, bc);

        // Remove button
        auto* rm_btn = lv_button_create(slot_card);
        lv_obj_set_pos(rm_btn, 520, 20);
        lv_obj_set_size(rm_btn, 52, 34);
        ui::rack_skin::apply_hardware_button(rm_btn, true, COLOR_RED, lv_color_hex(0x3B151A), lv_color_hex(0x281215));
        auto* rl = lv_label_create(rm_btn);
        lv_label_set_text(rl, "DEL");
        lv_obj_center(rl);
        lv_obj_set_style_text_font(rl, &ac_font_12, 0);
        lv_obj_set_style_text_color(rl, COLOR_RED, 0);

        auto* rc = new SlotCtx{v, i};
        lv_obj_add_event_cb(rm_btn, [](lv_event_t* e) {
            auto* c = static_cast<SlotCtx*>(lv_event_get_user_data(e));
            if (!c || !c->v) return;
            FxScreenView* view = c->v;
            const size_t idx = c->idx;
            ChannelState* target = resolve_target_channel(view);
            if (target && target->fx_rack.remove_plugin(idx)) {
                if (view->selected_slot >= static_cast<int>(target->fx_rack.count())) {
                    view->selected_slot = std::max(0, static_cast<int>(target->fx_rack.count()) - 1);
                }
                sync_channel_fx_to_dsp(target);
                view->last_param_fingerprint = compute_rack_fingerprint(target);
                schedule_ui_rebuild(view, true, true);
            }
        }, LV_EVENT_CLICKED, rc);
        lv_obj_add_event_cb(rm_btn, [](lv_event_t* e) {
            delete static_cast<SlotCtx*>(lv_event_get_user_data(e));
        }, LV_EVENT_DELETE, rc);

        cur_y += 88;
    }

    if (count < PluginRack::MAX_SLOTS) {
        auto* add_btn = lv_button_create(v->slot_container);
        lv_obj_set_pos(add_btn, 12, cur_y);
        lv_obj_set_size(add_btn, count > 0 ? 300 : 608, 44);
        ui::rack_skin::apply_hardware_button(add_btn, false, COLOR_SUBTLE_BORDER, lv_color_hex(0x282622), COLOR_CONTROL);
        auto* al = lv_label_create(add_btn);
        lv_label_set_text(al, "+ ASSIGN FX / PRESET");
        lv_obj_center(al);
        lv_obj_set_style_text_font(al, &ac_font_14, 0);
        lv_obj_set_style_text_color(al, COLOR_TEXT_PRIMARY, 0);
        lv_obj_add_event_cb(add_btn, [](lv_event_t* e) {
            auto* view = static_cast<FxScreenView*>(lv_event_get_user_data(e));
            if (view) {
                show_add_plugin_modal(view);
            }
        }, LV_EVENT_CLICKED, v);

        if (count > 0) {
            auto* save_btn = lv_button_create(v->slot_container);
            lv_obj_set_pos(save_btn, 320, cur_y);
            lv_obj_set_size(save_btn, 146, 44);
            ui::rack_skin::apply_hardware_button(save_btn, false, COLOR_GREEN, lv_color_hex(0x153522), lv_color_hex(0x182028));
            auto* sl = lv_label_create(save_btn);
            lv_label_set_text(sl, "SAVE CHAIN");
            lv_obj_center(sl);
            lv_obj_set_style_text_font(sl, &ac_font_12, 0);
            lv_obj_set_style_text_color(sl, COLOR_GREEN, 0);
            lv_obj_add_event_cb(save_btn, [](lv_event_t* e) {
                auto* view = static_cast<FxScreenView*>(lv_event_get_user_data(e));
                if (!view) return;
                ChannelState* target = resolve_target_channel(view);
                if (!target || target->fx_rack.count() == 0) return;
                std::string preset_name = std::string(target->name) + " User Chain";
                auto preset = prismix::pedalboard::PedalboardPresetManager::instance().captureRack(
                    preset_name, target->fx_rack, "PRISMIX Live", "Saved user rack chain");
                std::string filename = "assets/presets/" + std::string(target->name) + "_chain.prxp";
                prismix::pedalboard::PedalboardPresetManager::instance().savePreset(filename, preset);
                schedule_ui_rebuild(view, true, true);
            }, LV_EVENT_CLICKED, v);

            auto* clr_btn = lv_button_create(v->slot_container);
            lv_obj_set_pos(clr_btn, 474, cur_y);
            lv_obj_set_size(clr_btn, 146, 44);
            ui::rack_skin::apply_hardware_button(clr_btn, false, COLOR_RED, lv_color_hex(0x3B1515), lv_color_hex(0x182028));
            auto* cl = lv_label_create(clr_btn);
            lv_label_set_text(cl, "CLEAR CHAIN");
            lv_obj_center(cl);
            lv_obj_set_style_text_font(cl, &ac_font_12, 0);
            lv_obj_set_style_text_color(cl, COLOR_RED, 0);
            lv_obj_add_event_cb(clr_btn, [](lv_event_t* e) {
                auto* view = static_cast<FxScreenView*>(lv_event_get_user_data(e));
                if (!view) return;
                ChannelState* target = resolve_target_channel(view);
                if (!target) return;
                target->fx_rack.clear();
                sync_channel_fx_to_dsp(target);
                view->selected_slot = 0;
                view->last_param_fingerprint = compute_rack_fingerprint(target);
                schedule_ui_rebuild(view, true, true);
            }, LV_EVENT_CLICKED, v);
        }
        cur_y += 52;
    }

    auto* out_node = lv_obj_create(v->slot_container);
    lv_obj_set_pos(out_node, 12, cur_y);
    lv_obj_set_size(out_node, 608, 36);
    lv_obj_remove_flag(out_node, LV_OBJ_FLAG_SCROLLABLE);
    ui::rack_skin::apply_recessed_display(out_node, COLOR_LCD_BG, COLOR_SUBTLE_BORDER);
    ui::rack_skin::create_bezel_led(out_node, 12, 12, COLOR_GREEN, true);
    const char* out_txt = isAuxBusChannel(ch)
        ? "RACK OUTPUT TAP -> AUX EFX RETURN -> MAIN L/R BUS"
        : (ch == &g_mixer_state.master
            ? "RACK OUTPUT TAP -> MASTER FADER -> BRICKWALL LIMITER"
            : "RACK OUTPUT TAP -> CHANNEL FADER / AUX SENDS -> MAIN L/R");
    instrument_label(out_node, out_txt, 30, 9, &ac_font_12, COLOR_GREEN);
}

// Helper to create a brushed-metal sub-module faceplate box inside the rack editor
lv_obj_t* create_rack_module_box(lv_obj_t* parent, int x, int y, int w, int h,
                                 const char* title, lv_color_t accent_color, lv_color_t bg_color) {
    auto* box = lv_obj_create(parent);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_size(box, w, h);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    ui::rack_skin::apply_faceplate_panel(box, accent_color, bg_color);
    lv_obj_set_style_pad_all(box, 0, 0);

    auto* hdr = lv_obj_create(box);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_size(hdr, w, 26);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(hdr, COLOR_CHASSIS, 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(hdr, accent_color, 0);
    lv_obj_set_style_border_width(hdr, 1, 0);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_opa(hdr, LV_OPA_60, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    ui::rack_skin::create_rack_screw(hdr, 5, 6);
    if (w >= 180) {
        ui::rack_skin::create_rack_screw(hdr, w - 19, 6);
    }
    instrument_label(hdr, title, 24, 5, &ac_font_12, COLOR_TEXT_PRIMARY);
    return box;
}

// Helper to create a vintage grey brushed-metal sub-module faceplate box inside the rack editor
lv_obj_t* create_grey_rack_module_box(lv_obj_t* parent, int x, int y, int w, int h,
                                      const char* title, lv_color_t accent_color = lv_color_hex(0x9EAEBD),
                                      lv_color_t bg_color = lv_color_hex(0x323840)) {
    auto* box = lv_obj_create(parent);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_size(box, w, h);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    ui::rack_skin::apply_grey_faceplate_panel(box, accent_color, bg_color);
    lv_obj_set_style_pad_all(box, 0, 0);

    auto* hdr = lv_obj_create(box);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_size(hdr, w, 26);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(0x1E232A), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(hdr, accent_color, 0);
    lv_obj_set_style_border_width(hdr, 1, 0);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_opa(hdr, LV_OPA_70, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    ui::rack_skin::create_rack_screw(hdr, 5, 6);
    if (w >= 180) {
        ui::rack_skin::create_rack_screw(hdr, w - 19, 6);
    }
    instrument_label(hdr, title, 24, 5, &ac_font_12, accent_color);
    return box;
}

lv_obj_t* place_param_knob(lv_obj_t* parent, PluginInstance* inst, const char* param_id,
                           const char* override_label, int cx, int y, int diameter = 78) {
    if (!parent || !inst) return nullptr;
    auto* param = inst->find_parameter(param_id);
    if (!param) return nullptr;
    return instrument_knob(parent,
                           override_label ? override_label : param->name.c_str(),
                           &param->current_val,
                           param->min_val,
                           param->max_val,
                           param->default_val,
                           param->unit.c_str(),
                           cx,
                           y,
                           diameter);
}

// 28-Segment Studio Hardware LED Bargraph Meter for PRE-FX INPUT and POST-FX OUTPUT
void build_io_meter_strip(FxScreenView* v, lv_obj_t* box, int x, int y, int w) {
    auto* meter_canvas = lv_obj_create(box);
    lv_obj_set_pos(meter_canvas, x, y);
    lv_obj_set_size(meter_canvas, w, 92);
    lv_obj_remove_flag(meter_canvas, LV_OBJ_FLAG_SCROLLABLE);
    ui::rack_skin::apply_recessed_display(meter_canvas, COLOR_LCD_BG, COLOR_HW_BORDER);
    lv_obj_set_style_pad_all(meter_canvas, 0, 0);
    v->io_meter_canvas = meter_canvas;

    // Engraved Left Labels inside meter bezel
    instrument_label(meter_canvas, "PRE-FX IN L", 8, 6, &ac_font_12, COLOR_CYAN);
    instrument_label(meter_canvas, "PRE-FX IN R", 8, 24, &ac_font_12, COLOR_CYAN);
    instrument_label(meter_canvas, "POST-FX OUT L", 8, 50, &ac_font_12, COLOR_GREEN);
    instrument_label(meter_canvas, "POST-FX OUT R", 8, 68, &ac_font_12, COLOR_GREEN);

    lv_obj_add_event_cb(meter_canvas, [](lv_event_t* e) {
        auto* view = static_cast<FxScreenView*>(lv_event_get_user_data(e));
        if (!view) return;
        ChannelState* target = resolve_target_channel(view);
        if (!target) return;

        lv_obj_t* obj = lv_event_get_target_obj(e);
        lv_layer_t* layer = lv_event_get_layer(e);
        lv_area_t coords;
        lv_obj_get_coords(obj, &coords);
        const int w_px = lv_area_get_width(&coords);
        const bool isStereo = (target->format == ChannelFormat::Stereo);

        const int bar_x0 = coords.x1 + 114;
        const int bar_x1 = coords.x2 - 26;
        const int avail_w = std::max(56, bar_x1 - bar_x0);
        constexpr int kSegments = 28;
        const int seg_step = avail_w / kSegments;
        const int seg_w = std::max(2, seg_step - 2);

        // Horizontal separator between PRE-FX INPUT and POST-FX OUTPUT
        lv_draw_line_dsc_t sep_dsc;
        lv_draw_line_dsc_init(&sep_dsc);
        sep_dsc.color = COLOR_SUBTLE_SEPARATOR;
        sep_dsc.width = 1;
        sep_dsc.p1 = {static_cast<lv_value_precise_t>(coords.x1 + 6), static_cast<lv_value_precise_t>(coords.y1 + 45)};
        sep_dsc.p2 = {static_cast<lv_value_precise_t>(coords.x2 - 6), static_cast<lv_value_precise_t>(coords.y1 + 45)};
        lv_draw_line(layer, &sep_dsc);

        // Draw 4 LED Bargraph Tracks:
        //   0: PRE-FX IN L
        //   1: PRE-FX IN R (active only if Stereo; unlit if Mono)
        //   2: POST-FX OUT L
        //   3: POST-FX OUT R (active only if Stereo; unlit if Mono)
        struct TrackData {
            int y_off;
            float peak_db;
            float rms_db;
            float hold_db;
            bool active_ch;
            bool clip;
        };
        const TrackData tracks[4] = {
            {7,  target->fx_in_peak_l,  target->fx_in_rms_l,  target->fx_in_peak_hold_l,  true,     target->fx_in_clip},
            {25, target->fx_in_peak_r,  target->fx_in_rms_r,  target->fx_in_peak_hold_r,  isStereo, target->fx_in_clip && isStereo},
            {51, target->fx_out_peak_l, target->fx_out_rms_l, target->fx_out_peak_hold_l, true,     target->fx_out_clip},
            {69, target->fx_out_peak_r, target->fx_out_rms_r, target->fx_out_peak_hold_r, isStereo, target->fx_out_clip && isStereo},
        };

        auto db_to_seg = [](float db) -> int {
            if (db <= -59.5f) return -1;
            const float norm = std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
            return std::clamp(static_cast<int>(std::floor(norm * (kSegments - 1) + 0.5f)), 0, kSegments - 1);
        };

        for (int t = 0; t < 4; ++t) {
            const auto& tr = tracks[t];
            const int sy1 = coords.y1 + tr.y_off;
            const int sy2 = sy1 + 11;
            const int peak_seg = tr.active_ch ? db_to_seg(tr.peak_db) : -1;
            const int rms_seg  = tr.active_ch ? db_to_seg(tr.rms_db)  : -1;
            const int hold_seg = tr.active_ch ? db_to_seg(tr.hold_db) : -1;

            for (int s = 0; s < kSegments; ++s) {
                const int sx1 = bar_x0 + s * seg_step;
                const int sx2 = sx1 + seg_w - 1;
                lv_area_t seg_area = {sx1, sy1, sx2, sy2};

                lv_color_t lit_col;
                lv_color_t dim_col;
                if (s >= 25) {        // -6..0 dBFS Red Overload Zone
                    lit_col = lv_color_hex(0xF23D3D);
                    dim_col = lv_color_hex(0x1E0D0F);
                } else if (s >= 19) { // -18..-6 dBFS Amber Headroom Zone
                    lit_col = lv_color_hex(0xF2B029);
                    dim_col = lv_color_hex(0x1D170B);
                } else {              // -60..-18 dBFS Nominal Signal Zone
                    lit_col = (t < 2) ? lv_color_hex(0x28C6EE) : lv_color_hex(0x25D86B);
                    dim_col = (t < 2) ? lv_color_hex(0x0B171D) : lv_color_hex(0x0B1911);
                }

                lv_draw_rect_dsc_t seg_dsc;
                lv_draw_rect_dsc_init(&seg_dsc);
                seg_dsc.radius = 1;

                if (tr.active_ch && s <= peak_seg) {
                    seg_dsc.bg_color = lit_col;
                    seg_dsc.bg_opa = (s <= rms_seg) ? LV_OPA_COVER : LV_OPA_80;
                    lv_draw_rect(layer, &seg_dsc, &seg_area);

                    // Top specular reflection on lit LED lens
                    lv_draw_rect_dsc_t hi_dsc;
                    lv_draw_rect_dsc_init(&hi_dsc);
                    hi_dsc.bg_color = lv_color_hex(0xFFFFFF);
                    hi_dsc.bg_opa = LV_OPA_30;
                    lv_area_t hi_area = {sx1, sy1, sx2, sy1 + 2};
                    lv_draw_rect(layer, &hi_dsc, &hi_area);
                } else if (tr.active_ch && s == hold_seg && hold_seg >= 0) {
                    seg_dsc.bg_color = lv_color_hex(0xEAF6FF);
                    seg_dsc.bg_opa = LV_OPA_COVER;
                    lv_draw_rect(layer, &seg_dsc, &seg_area);
                } else {
                    seg_dsc.bg_color = dim_col;
                    seg_dsc.bg_opa = tr.active_ch ? LV_OPA_COVER : LV_OPA_50;
                    lv_draw_rect(layer, &seg_dsc, &seg_area);
                }
            }

            // Right-edge CLIP LED indicator jewel
            const int cx1 = coords.x2 - 18;
            lv_area_t clip_area = {cx1, sy1 + 1, cx1 + 10, sy2 - 1};
            lv_draw_rect_dsc_t clip_dsc;
            lv_draw_rect_dsc_init(&clip_dsc);
            clip_dsc.radius = 2;
            clip_dsc.bg_color = tr.clip ? lv_color_hex(0xFF2A2A) : lv_color_hex(0x220E10);
            clip_dsc.bg_opa = LV_OPA_COVER;
            clip_dsc.border_color = tr.clip ? lv_color_hex(0xFF9090) : lv_color_hex(0x381A1E);
            clip_dsc.border_width = 1;
            lv_draw_rect(layer, &clip_dsc, &clip_area);
        }
    }, LV_EVENT_DRAW_MAIN, v);

    v->io_readout_lbl = instrument_label(
        box, "PRE-FX IN: -90.0 dB   |   POST-FX OUT: -90.0 dB", x, y + 96, &ac_font_12, COLOR_TEXT);
}

// Parameter metadata table
struct ReverbParamMeta {
    const char* id;
    const char* short_name;
    const char* full_name;
    float min_val;
    float max_val;
    float def_val;
    const char* unit;
};

static const ReverbParamMeta kReverbParams[8] = {
    {"decay",     "DECAY",    "DECAY TIME",   0.2f,    12.0f,   2.2f,    "s"},
    {"predelay",  "PRE-DLY",  "PRE-DELAY",    0.0f,    250.0f,  20.0f,   "ms"},
    {"diffusion", "DIFFUS",   "DIFFUSION",    0.0f,    1.0f,    0.75f,   "%"},
    {"damping",   "DAMP",     "HIGH DAMP",    0.0f,    1.0f,    0.45f,   "%"},
    {"low_cut",   "HPF",      "INPUT HPF",    20.0f,   1000.0f, 100.0f,  "Hz"},
    {"high_cut",  "LPF",      "OUTPUT LPF",   1000.0f, 20000.0f,10000.0f,"Hz"},
    {"width",     "WIDTH",    "STEREO WIDTH", 0.0f,    1.0f,    0.85f,   "%"},
    {"mix",       "MIX",      "DRY/WET MIX",  0.0f,    1.0f,    0.30f,   "%"}
};

static std::string format_reverb_val(const char* id, float val) {
    char buf[32];
    if (std::strcmp(id, "decay") == 0) {
        std::snprintf(buf, sizeof(buf), "%.2f s", val);
    } else if (std::strcmp(id, "predelay") == 0) {
        std::snprintf(buf, sizeof(buf), "%.1f ms", val);
    } else if (std::strcmp(id, "diffusion") == 0 || std::strcmp(id, "damping") == 0 ||
               std::strcmp(id, "width") == 0 || std::strcmp(id, "mix") == 0) {
        std::snprintf(buf, sizeof(buf), "%.0f %%", val * 100.0f);
    } else if (std::strcmp(id, "low_cut") == 0) {
        std::snprintf(buf, sizeof(buf), "%.0f Hz", val);
    } else if (std::strcmp(id, "high_cut") == 0) {
        if (val >= 1000.0f) {
            std::snprintf(buf, sizeof(buf), "%.1f kHz", val * 0.001f);
        } else {
            std::snprintf(buf, sizeof(buf), "%.0f Hz", val);
        }
    } else {
        std::snprintf(buf, sizeof(buf), "%.1f", val);
    }
    return std::string(buf);
}

// ============================================================================
// 1. REVERB RACK EDITOR (Lexicon PCM96 High-Contrast Backlit LCD Display)
// ============================================================================
void build_reverb_rack_editor(FxScreenView* v, ChannelState* ch, PluginInstance* inst) {
    // ------------------------------------------------------------------------
    // TOP: PRESETS MODULE (Simple hardware preset bar)
    // ------------------------------------------------------------------------
    auto* preset_bar = create_rack_module_box(
        v->editor_content, 16, 94, 1192, 58,
        "PRESETS",
        COLOR_SUBTLE_BORDER, COLOR_METAL_PANEL);

    static const char* kRoomNames[4] = {"ROOM", "HALL", "PLATE", "AMBIENCE"};
    const int activeRoom = std::clamp(
        static_cast<int>(std::round(inst->get_parameter_value("room_type", 1.0f))), 0, 3);

    for (int r = 0; r < 4; ++r) {
        auto* btn = lv_button_create(preset_bar);
        lv_obj_set_pos(btn, 14 + r * 236, 26);
        lv_obj_set_size(btn, 224, 26);
        const bool sel = (r == activeRoom);
        ui::rack_skin::apply_hardware_button(
            btn, sel, COLOR_SELECTED_BORDER, COLOR_RAISED_PANEL, COLOR_CONTROL);
        auto* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, kRoomNames[r]);
        lv_obj_center(lbl);
        lv_obj_set_style_text_font(lbl, &ac_font_12, 0);
        lv_obj_set_style_text_color(lbl, sel ? COLOR_PRIMARY_TEXT : COLOR_SECONDARY_TEXT, 0);

        struct PresetCtx { FxScreenView* v; int roomIdx; };
        auto* pc = new PresetCtx{v, r};
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            auto* c = static_cast<PresetCtx*>(lv_event_get_user_data(e));
            if (!c || !c->v) return;
            FxScreenView* view = c->v;
            const int roomIdx = c->roomIdx;
            ChannelState* target = resolve_target_channel(view);
            if (!target) return;
            if (auto* p = target->fx_rack.get_slot(view->selected_slot)) {
                const auto& tpl = audiocore::ReverbNode::getTemplate(
                    static_cast<audiocore::ReverbType>(roomIdx));
                p->set_parameter_value("room_type", static_cast<float>(roomIdx));
                p->set_parameter_value("predelay", tpl.preDelayMs);
                p->set_parameter_value("decay", tpl.decaySeconds);
                p->set_parameter_value("damping", tpl.damping);
                p->set_parameter_value("diffusion", tpl.diffusion);
                p->set_parameter_value("low_cut", tpl.lowCutHz);
                p->set_parameter_value("high_cut", tpl.highCutHz);
                p->set_parameter_value("width", tpl.width);
                if (!isAuxBusChannel(target)) {
                    p->set_parameter_value("mix", tpl.mix);
                }
                sync_channel_fx_to_dsp(target);
                view->last_param_fingerprint = compute_rack_fingerprint(target);
                schedule_ui_rebuild(view, true, true);
            }
        }, LV_EVENT_CLICKED, pc);
        lv_obj_add_event_cb(btn, [](lv_event_t* e) {
            delete static_cast<PresetCtx*>(lv_event_get_user_data(e));
        }, LV_EVENT_DELETE, pc);
    }

    // Routing mode action button (100% Wet Lock for Send/Return vs Standard Insert Mix)
    const bool isAux = isAuxBusChannel(ch);
    auto* wet_btn = lv_button_create(preset_bar);
    lv_obj_set_pos(wet_btn, 960, 26);
    lv_obj_set_size(wet_btn, 216, 26);
    const bool fullWet = inst->get_parameter_value("mix", 0.30f) >= 0.99f;
    ui::rack_skin::apply_hardware_button(
        wet_btn, fullWet, COLOR_GREEN, lv_color_hex(0x283818), COLOR_CONTROL);
    auto* wet_lbl = lv_label_create(wet_btn);
    lv_label_set_text(wet_lbl, isAux ? "AUX RETURN: 100% WET" : (fullWet ? "MIX: 100% WET" : "SET 100% WET"));
    lv_obj_center(wet_lbl);
    lv_obj_set_style_text_font(wet_lbl, &ac_font_12, 0);
    lv_obj_set_style_text_color(wet_lbl, fullWet ? COLOR_GREEN : COLOR_TEXT2, 0);
    lv_obj_add_event_cb(wet_btn, [](lv_event_t* e) {
        auto* view = static_cast<FxScreenView*>(lv_event_get_user_data(e));
        ChannelState* target = resolve_target_channel(view);
        if (!target) return;
        if (auto* p = target->fx_rack.get_slot(view->selected_slot)) {
            const float cur = p->get_parameter_value("mix", 0.30f);
            p->set_parameter_value("mix", cur >= 0.99f ? 0.30f : 1.0f);
            sync_channel_fx_to_dsp(target);
            view->last_param_fingerprint = compute_rack_fingerprint(target);
            schedule_ui_rebuild(view, true, true);
        }
    }, LV_EVENT_CLICKED, v);

    // ------------------------------------------------------------------------
    // MIDDLE LEFT: LEXICON PCM96 HIGH-CONTRAST BACKLIT LCD DISPLAY
    // ------------------------------------------------------------------------
    auto* pcm_box = create_rack_module_box(
        v->editor_content, 16, 160, 710, 252,
        "PCM96 GRAPHIC DISPLAY",
        COLOR_SUBTLE_BORDER, COLOR_METAL_PANEL);

    auto* canvas = lv_obj_create(pcm_box);
    lv_obj_set_pos(canvas, 10, 30);
    lv_obj_set_size(canvas, 690, 212);
    lv_obj_remove_flag(canvas, LV_OBJ_FLAG_SCROLLABLE);
    ui::rack_skin::apply_recessed_display(canvas, COLOR_LCD_BG, COLOR_SUBTLE_BORDER);
    lv_obj_set_style_pad_all(canvas, 0, 0);
    v->visual_canvas = canvas;

    lv_obj_add_event_cb(canvas, [](lv_event_t* e) {
        auto* view = static_cast<FxScreenView*>(lv_event_get_user_data(e));
        if (!view) return;
        ChannelState* target = resolve_target_channel(view);
        if (!target) return;
        const auto* p = target->fx_rack.get_slot(view->selected_slot);
        if (!p || p->get_uri() != "http://audiocore.org/plugins/reverb") return;

        lv_obj_t* obj = lv_event_get_target_obj(e);
        lv_layer_t* layer = lv_event_get_layer(e);
        lv_area_t coords;
        lv_obj_get_coords(obj, &coords);

        // 1. Deep Lexicon Backlit LCD Screen Base
        lv_draw_rect_dsc_t bg_dsc;
        lv_draw_rect_dsc_init(&bg_dsc);
        bg_dsc.radius = 4;
        bg_dsc.bg_color = COLOR_LCD_BG;
        bg_dsc.bg_opa = LV_OPA_COVER;
        bg_dsc.border_color = COLOR_SUBTLE_BORDER;
        bg_dsc.border_width = 1;
        lv_draw_rect(layer, &bg_dsc, &coords);

        // 2. Active Program / Preset Information (Genuine LCD Bitmap Typography: Warm Ivory)
        const int act_room = std::clamp(
            static_cast<int>(std::round(p->get_parameter_value("room_type", 1.0f))), 0, 3);
        static const struct PresetMeta {
            const char* code;
            const char* name;
        } kPresets[4] = {
            {"P01", "STUDIO ROOM"},
            {"P02", "CONCERT HALL"},
            {"P03", "VINTAGE PLATE"},
            {"P04", "VOCAL AMBIENCE"}
        };

        char prg_txt[64];
        std::snprintf(prg_txt, sizeof(prg_txt), "%s: %s", kPresets[act_room].code, kPresets[act_room].name);

        lv_draw_label_dsc_t prg_dsc;
        lv_draw_label_dsc_init(&prg_dsc);
        prg_dsc.font = &lv_font_unscii_16;
        prg_dsc.color = COLOR_PRIMARY_TEXT;
        prg_dsc.text = prg_txt;
        lv_area_t prg_a = {coords.x1 + 14, coords.y1 + 9, coords.x2 - 325, coords.y1 + 27};
        lv_draw_label(layer, &prg_dsc, &prg_a);

        // 3. Preset Soft-Keys on LCD Screen Header (Togglable)
        static const char* kTabLabels[4] = {"ROOM", "HALL", "PLATE", "AMB"};
        for (int t = 0; t < 4; ++t) {
            int bx = coords.x2 - 314 + t * 76;
            int by = coords.y1 + 6;
            lv_area_t ba = {bx, by, bx + 70, by + 22};
            const bool is_sel = (t == act_room);

            lv_draw_rect_dsc_t tab_dsc;
            lv_draw_rect_dsc_init(&tab_dsc);
            tab_dsc.radius = 2;
            tab_dsc.bg_color = is_sel ? COLOR_SELECTED_BORDER : COLOR_CHASSIS;
            tab_dsc.bg_opa = LV_OPA_COVER;
            tab_dsc.border_color = is_sel ? COLOR_KNOB_INDICATOR : COLOR_SUBTLE_SEPARATOR;
            tab_dsc.border_width = 1;
            lv_draw_rect(layer, &tab_dsc, &ba);

            lv_draw_label_dsc_t tl_dsc;
            lv_draw_label_dsc_init(&tl_dsc);
            tl_dsc.font = &ac_font_12;
            tl_dsc.color = is_sel ? COLOR_RECESSED : COLOR_SECONDARY_TEXT;
            tl_dsc.text = kTabLabels[t];
            tl_dsc.align = LV_TEXT_ALIGN_CENTER;
            lv_area_t tla = {bx, by + 3, bx + 70, by + 19};
            lv_draw_label(layer, &tl_dsc, &tla);
        }

        // 4. Center Parameter Edit Display (Updates immediately when knob is turned)
        lv_area_t ea = {coords.x1 + 10, coords.y1 + 34, coords.x2 - 10, coords.y1 + 124};
        lv_draw_rect_dsc_t ed_dsc;
        lv_draw_rect_dsc_init(&ed_dsc);
        ed_dsc.radius = 3;
        ed_dsc.bg_color = COLOR_RECESSED;
        ed_dsc.bg_opa = LV_OPA_COVER;
        ed_dsc.border_color = COLOR_STANDARD_BORDER;
        ed_dsc.border_width = 1;
        lv_draw_rect(layer, &ed_dsc, &ea);

        // Find active parameter
        int active_param_idx = 0;
        for (int i = 0; i < 8; ++i) {
            if (view->reverb_last_param == kReverbParams[i].id) {
                active_param_idx = i;
                break;
            }
        }
        const auto& cur_p = kReverbParams[active_param_idx];
        const float cur_val = p->get_parameter_value(cur_p.id, cur_p.def_val);
        const std::string cur_val_str = format_reverb_val(cur_p.id, cur_val);
        const float norm = std::clamp((cur_val - cur_p.min_val) / (cur_p.max_val - cur_p.min_val), 0.0f, 1.0f);
        const std::string min_str = format_reverb_val(cur_p.id, cur_p.min_val);
        const std::string max_str = format_reverb_val(cur_p.id, cur_p.max_val);

        // Parameter Edit Header
        char ed_hdr[64];
        std::snprintf(ed_hdr, sizeof(ed_hdr), "PARAMETER EDIT: %s", cur_p.full_name);
        lv_draw_label_dsc_t eh_dsc;
        lv_draw_label_dsc_init(&eh_dsc);
        eh_dsc.font = &ac_font_12;
        eh_dsc.color = COLOR_SECONDARY_TEXT;
        eh_dsc.text = ed_hdr;
        lv_area_t eha = {coords.x1 + 18, coords.y1 + 38, coords.x1 + 350, coords.y1 + 54};
        lv_draw_label(layer, &eh_dsc, &eha);

        // Large Digital Readout (Squared LCD Bitmap Typography: Warm Ivory)
        lv_draw_label_dsc_t val_dsc;
        lv_draw_label_dsc_init(&val_dsc);
        val_dsc.font = &ac_font_lcd_24;
        val_dsc.color = COLOR_PRIMARY_TEXT;
        val_dsc.text = cur_val_str.c_str();
        lv_area_t val_a = {coords.x1 + 18, coords.y1 + 56, coords.x1 + 210, coords.y1 + 92};
        lv_draw_label(layer, &val_dsc, &val_a);

        // 25-Segment Graphic LCD Bargraph geometry (uniform integer segment grid)
        const int bar_x = coords.x1 + 220;
        const int bar_y = coords.y1 + 56;
        constexpr int kBarSegs = 25;
        constexpr int kBarGap = 3;
        const int bar_w = (coords.x2 - 20) - bar_x;
        const int seg_w = (bar_w - (kBarSegs - 1) * kBarGap) / kBarSegs;
        const int bar_right = bar_x + kBarSegs * seg_w + (kBarSegs - 1) * kBarGap;
        const int lit_segs = std::clamp(static_cast<int>(std::round(norm * 25.0f)), 0, 25);

        // Parameter unit/range note (aligned with the bargraph scale markers row)
        char unit_note[64];
        std::snprintf(unit_note, sizeof(unit_note), "RANGE: %s - %s", min_str.c_str(), max_str.c_str());
        lv_draw_label_dsc_t un_dsc;
        lv_draw_label_dsc_init(&un_dsc);
        un_dsc.font = &ac_font_12;
        un_dsc.color = COLOR_SECONDARY_TEXT;
        un_dsc.text = unit_note;
        lv_area_t una = {coords.x1 + 18, bar_y + 30, coords.x1 + 210, bar_y + 46};
        lv_draw_label(layer, &un_dsc, &una);

        for (int s = 0; s < kBarSegs; ++s) {
            int sx = bar_x + s * (seg_w + kBarGap);
            lv_area_t sa = {sx, bar_y, sx + seg_w, bar_y + 22};
            const bool lit = (s < lit_segs);

            lv_draw_rect_dsc_t sd;
            lv_draw_rect_dsc_init(&sd);
            sd.radius = 1;
            sd.bg_color = lit ? COLOR_NEUTRAL_HIGHLIGHT : COLOR_RECESSED;
            sd.bg_opa = LV_OPA_COVER;
            sd.border_color = lit ? COLOR_KNOB_INDICATOR : COLOR_STANDARD_BORDER;
            sd.border_width = 1;
            lv_draw_rect(layer, &sd, &sa);
        }

        // Bargraph scale markers (aligned with the RANGE note row)
        char bar_info[64];
        std::snprintf(bar_info, sizeof(bar_info), "MIN: %s", min_str.c_str());

        lv_draw_label_dsc_t sc_dsc;
        lv_draw_label_dsc_init(&sc_dsc);
        sc_dsc.font = &ac_font_12;
        sc_dsc.color = COLOR_SECONDARY_TEXT;
        sc_dsc.text = bar_info;
        lv_area_t sc_min_a = {bar_x, bar_y + 30, bar_x + 120, bar_y + 46};
        lv_draw_label(layer, &sc_dsc, &sc_min_a);

        char pct_info[32];
        std::snprintf(pct_info, sizeof(pct_info), "VALUE: %.0f%%", norm * 100.0f);
        sc_dsc.text = pct_info;
        sc_dsc.align = LV_TEXT_ALIGN_CENTER;
        lv_area_t sc_pct_a = {(bar_x + bar_right) / 2 - 60, bar_y + 30, (bar_x + bar_right) / 2 + 60, bar_y + 46};
        lv_draw_label(layer, &sc_dsc, &sc_pct_a);

        char max_info[64];
        std::snprintf(max_info, sizeof(max_info), "MAX: %s", max_str.c_str());
        sc_dsc.text = max_info;
        sc_dsc.align = LV_TEXT_ALIGN_RIGHT;
        lv_area_t sc_max_a = {bar_right - 140, bar_y + 30, bar_right, bar_y + 46};
        lv_draw_label(layer, &sc_dsc, &sc_max_a);

        // 5. Bottom 8-Parameter Live Matrix (Genuine LCD Bitmap Typography: Warm Ivory Values)
        for (int k = 0; k < 8; ++k) {
            int row = k / 4;
            int col = k % 4;
            int tw = (coords.x2 - coords.x1 - 20 - 3 * 8) / 4;
            int tx = coords.x1 + 10 + col * (tw + 8);
            int ty = coords.y1 + 130 + row * 38;
            lv_area_t ta = {tx, ty, tx + tw, ty + 32};

            const bool is_act = (k == active_param_idx);
            float pval = p->get_parameter_value(kReverbParams[k].id, kReverbParams[k].def_val);
            std::string pv_str = format_reverb_val(kReverbParams[k].id, pval);

            lv_draw_rect_dsc_t cdsc;
            lv_draw_rect_dsc_init(&cdsc);
            cdsc.radius = 3;
            cdsc.bg_color = is_act ? COLOR_RAISED_PANEL : COLOR_RECESSED;
            cdsc.bg_opa = LV_OPA_COVER;
            cdsc.border_color = is_act ? COLOR_SELECTED_BORDER : COLOR_SUBTLE_SEPARATOR;
            cdsc.border_width = is_act ? 2 : 1;
            lv_draw_rect(layer, &cdsc, &ta);

            // Name (clean technical sans)
            lv_draw_label_dsc_t nl;
            lv_draw_label_dsc_init(&nl);
            nl.font = &ac_font_12;
            nl.color = is_act ? COLOR_PRIMARY_TEXT : COLOR_SECONDARY_TEXT;
            nl.text = kReverbParams[k].short_name;
            lv_area_t na = {tx + 6, ty + 9, tx + 62, ty + 25};
            lv_draw_label(layer, &nl, &na);

            // Value (LCD bitmap typography)
            lv_draw_label_dsc_t vl;
            lv_draw_label_dsc_init(&vl);
            vl.font = &lv_font_unscii_8;
            vl.color = is_act ? COLOR_NEUTRAL_HIGHLIGHT : COLOR_PRIMARY_TEXT;
            vl.text = pv_str.c_str();
            vl.align = LV_TEXT_ALIGN_RIGHT;
            lv_area_t va = {tx + 64, ty + 9, tx + tw - 6, ty + 25};
            lv_draw_label(layer, &vl, &va);
        }

        // 6. Draw physical LCD dot-matrix mask and recessed edge shadow
        ui::rack_skin::draw_lcd_dot_matrix_mask(layer, &coords);
    }, LV_EVENT_DRAW_MAIN, v);

    // Clicking top soft-tabs on LCD directly toggles presets
    lv_obj_add_event_cb(canvas, [](lv_event_t* e) {
        auto* view = static_cast<FxScreenView*>(lv_event_get_user_data(e));
        if (!view) return;
        ChannelState* target = resolve_target_channel(view);
        if (!target) return;
        auto* p = target->fx_rack.get_slot(view->selected_slot);
        if (!p) return;

        lv_indev_t* indev = lv_indev_active();
        if (!indev) return;
        lv_point_t pt{};
        lv_indev_get_point(indev, &pt);

        lv_obj_t* obj = lv_event_get_target_obj(e);
        lv_area_t coords;
        lv_obj_get_coords(obj, &coords);

        if (pt.y >= coords.y1 + 4 && pt.y <= coords.y1 + 32 && pt.x >= coords.x2 - 314 && pt.x <= coords.x2 - 10) {
            int clicked_room = std::clamp((pt.x - (coords.x2 - 314)) / 76, 0, 3);
            const auto& tpl = audiocore::ReverbNode::getTemplate(
                static_cast<audiocore::ReverbType>(clicked_room));
            p->set_parameter_value("room_type", static_cast<float>(clicked_room));
            p->set_parameter_value("predelay", tpl.preDelayMs);
            p->set_parameter_value("decay", tpl.decaySeconds);
            p->set_parameter_value("damping", tpl.damping);
            p->set_parameter_value("diffusion", tpl.diffusion);
            p->set_parameter_value("low_cut", tpl.lowCutHz);
            p->set_parameter_value("high_cut", tpl.highCutHz);
            p->set_parameter_value("width", tpl.width);
            if (!isAuxBusChannel(target)) {
                p->set_parameter_value("mix", tpl.mix);
            }
            sync_channel_fx_to_dsp(target);
            view->last_param_fingerprint = compute_rack_fingerprint(target);
            schedule_ui_rebuild(view, true, true);
        }
    }, LV_EVENT_CLICKED, v);

    // ------------------------------------------------------------------------
    // MIDDLE RIGHT: LEVELS MODULE (Clean audio telemetry)
    // ------------------------------------------------------------------------
    auto* meter_box = create_rack_module_box(
        v->editor_content, 738, 160, 470, 252,
        "LEVELS",
        COLOR_SUBTLE_BORDER, COLOR_METAL_PANEL);
    build_io_meter_strip(v, meter_box, 14, 38, 442);

    // ------------------------------------------------------------------------
    // BOTTOM: 3 ANALOG PARAMETER ENCODER GROUPS (TIME, TONE, MIX)
    // ------------------------------------------------------------------------
    auto place_reverb_knob = [&](lv_obj_t* parent, const char* param_id, const char* lbl, int cx, int y, int dia) {
        auto* k = place_param_knob(parent, inst, param_id, lbl, cx, y, dia);
        if (k) {
            struct ReverbKnobCtx {
                FxScreenView* view;
                const char* id;
            };
            auto* kctx = new ReverbKnobCtx{v, param_id};
            lv_obj_add_event_cb(k, [](lv_event_t* e) {
                auto* c = static_cast<ReverbKnobCtx*>(lv_event_get_user_data(e));
                if (!c || !c->view) return;
                c->view->reverb_last_param = c->id;
                if (c->view->visual_canvas) {
                    lv_obj_invalidate(c->view->visual_canvas);
                }
            }, LV_EVENT_VALUE_CHANGED, kctx);
            lv_obj_add_event_cb(k, [](lv_event_t* e) {
                delete static_cast<ReverbKnobCtx*>(lv_event_get_user_data(e));
            }, LV_EVENT_DELETE, kctx);
        }
    };

    auto* grp_time = create_rack_module_box(
        v->editor_content, 16, 420, 388, 386,
        "TIME", COLOR_SUBTLE_BORDER, COLOR_METAL_PANEL);
    place_reverb_knob(grp_time, "decay", "DECAY TIME", 51, 60, 86);
    place_reverb_knob(grp_time, "predelay", "PRE-DELAY", 231, 60, 86);
    place_reverb_knob(grp_time, "diffusion", "DIFFUSION", 141, 224, 86);

    auto* grp_tone = create_rack_module_box(
        v->editor_content, 418, 420, 388, 386,
        "TONE", COLOR_SUBTLE_BORDER, COLOR_METAL_PANEL);
    place_reverb_knob(grp_tone, "damping", "HIGH DAMP", 141, 60, 86);
    place_reverb_knob(grp_tone, "low_cut", "INPUT HPF", 51, 224, 86);
    place_reverb_knob(grp_tone, "high_cut", "OUTPUT LPF", 231, 224, 86);

    auto* grp_mix = create_rack_module_box(
        v->editor_content, 820, 420, 388, 386,
        "MIX", COLOR_SUBTLE_BORDER, COLOR_METAL_PANEL);
    place_reverb_knob(grp_mix, "width", "STEREO WIDTH", 141, 60, 86);
    place_reverb_knob(grp_mix, "mix", isAux ? "WET RETURN LVL" : "DRY/WET MIX", 141, 224, 86);
}

// ============================================================================
// 2. DELAY RACK EDITOR (Dual-Tap Tempo Echo & Ping-Pong Rack)
// ============================================================================
void build_delay_rack_editor(FxScreenView* v, ChannelState* ch, PluginInstance* inst) {
    auto* sync_bar = create_rack_module_box(
        v->editor_content, 16, 94, 1192, 66,
        "TEMPO SYNCHRONIZATION, NOTE DIVISION & L/R TAP LINKING",
        COLOR_STANDARD_BORDER, COLOR_PANEL_SURFACE);

    const bool syncEn = inst->get_parameter_value("sync_en", 0.0f) >= 0.5f;
    const bool linkLr = inst->get_parameter_value("link_lr", 0.0f) >= 0.5f;
    const int activeDiv = std::clamp(
        static_cast<int>(std::round(inst->get_parameter_value("sync_div", 2.0f))), 0, 6);

    // Tempo Sync Toggle
    auto* sync_btn = lv_button_create(sync_bar);
    lv_obj_set_pos(sync_btn, 12, 32);
    lv_obj_set_size(sync_btn, 136, 28);
    ui::rack_skin::apply_hardware_button(
        sync_btn, syncEn, COLOR_SELECTED_BORDER, COLOR_RAISED_PANEL, COLOR_CONTROL);
    auto* sync_lbl = lv_label_create(sync_btn);
    lv_label_set_text(sync_lbl, syncEn ? "TEMPO SYNC: ON" : "SYNC: MANUAL");
    lv_obj_center(sync_lbl);
    lv_obj_set_style_text_font(sync_lbl, &ac_font_12, 0);
    lv_obj_set_style_text_color(sync_lbl, syncEn ? COLOR_PRIMARY_TEXT : COLOR_SECONDARY_TEXT, 0);
    lv_obj_add_event_cb(sync_btn, [](lv_event_t* e) {
        auto* view = static_cast<FxScreenView*>(lv_event_get_user_data(e));
        ChannelState* target = resolve_target_channel(view);
        if (!target) return;
        if (auto* p = target->fx_rack.get_slot(view->selected_slot)) {
            const bool nextSync = p->get_parameter_value("sync_en", 0.0f) < 0.5f;
            p->set_parameter_value("sync_en", nextSync ? 1.0f : 0.0f);
            if (nextSync) {
                const float bpm = p->get_parameter_value("bpm", 120.0f);
                const int div = std::clamp(static_cast<int>(std::round(p->get_parameter_value("sync_div", 2.0f))), 0, 6);
                const float ms = audiocore::DelayNode::calculateTempoDelayMs(
                    bpm, static_cast<audiocore::TempoDivision>(div));
                p->set_parameter_value("time_l", ms);
                if (p->get_parameter_value("link_lr", 0.0f) >= 0.5f) {
                    p->set_parameter_value("time_r", ms);
                }
            }
            sync_channel_fx_to_dsp(target);
            view->last_param_fingerprint = compute_rack_fingerprint(target);
            schedule_ui_rebuild(view, true, true);
        }
    }, LV_EVENT_CLICKED, v);

    // Link L/R Toggle
    auto* link_btn = lv_button_create(sync_bar);
    lv_obj_set_pos(link_btn, 156, 32);
    lv_obj_set_size(link_btn, 130, 28);
    ui::rack_skin::apply_hardware_button(
        link_btn, linkLr, COLOR_CYAN, lv_color_hex(0x16384C), lv_color_hex(0x1B2229));
    auto* link_lbl = lv_label_create(link_btn);
    lv_label_set_text(link_lbl, linkLr ? "L/R LINKED" : "L/R DUAL TAP");
    lv_obj_center(link_lbl);
    lv_obj_set_style_text_font(link_lbl, &ac_font_12, 0);
    lv_obj_set_style_text_color(link_lbl, linkLr ? COLOR_CYAN : COLOR_TEXT2, 0);
    lv_obj_add_event_cb(link_btn, [](lv_event_t* e) {
        auto* view = static_cast<FxScreenView*>(lv_event_get_user_data(e));
        ChannelState* target = resolve_target_channel(view);
        if (!target) return;
        if (auto* p = target->fx_rack.get_slot(view->selected_slot)) {
            const bool nextLink = p->get_parameter_value("link_lr", 0.0f) < 0.5f;
            p->set_parameter_value("link_lr", nextLink ? 1.0f : 0.0f);
            if (nextLink) {
                p->set_parameter_value("time_r", p->get_parameter_value("time_l", 320.0f));
            }
            sync_channel_fx_to_dsp(target);
            view->last_param_fingerprint = compute_rack_fingerprint(target);
            schedule_ui_rebuild(view, true, true);
        }
    }, LV_EVENT_CLICKED, v);

    // 7 Musical Note Division Buttons
    static const char* kDivLabels[7] = {"1/1", "1/2", "1/4", "1/8", "1/8T", "1/16", "1/32"};
    for (int d = 0; d < 7; ++d) {
        auto* dbtn = lv_button_create(sync_bar);
        lv_obj_set_pos(dbtn, 304 + d * 82, 32);
        lv_obj_set_size(dbtn, 76, 28);
        const bool sel = (d == activeDiv);
        ui::rack_skin::apply_hardware_button(
            dbtn, sel, COLOR_SELECTED_BORDER, COLOR_RAISED_PANEL, COLOR_CONTROL);
        auto* dl = lv_label_create(dbtn);
        lv_label_set_text(dl, kDivLabels[d]);
        lv_obj_center(dl);
        lv_obj_set_style_text_font(dl, &ac_font_12, 0);
        lv_obj_set_style_text_color(dl, sel ? COLOR_PRIMARY_TEXT : COLOR_SECONDARY_TEXT, 0);

        struct DivCtx { FxScreenView* v; int divIdx; };
        auto* dc = new DivCtx{v, d};
        lv_obj_add_event_cb(dbtn, [](lv_event_t* e) {
            auto* c = static_cast<DivCtx*>(lv_event_get_user_data(e));
            if (!c || !c->v) return;
            FxScreenView* view = c->v;
            const int divIdx = c->divIdx;
            ChannelState* target = resolve_target_channel(view);
            if (!target) return;
            if (auto* p = target->fx_rack.get_slot(view->selected_slot)) {
                p->set_parameter_value("sync_div", static_cast<float>(divIdx));
                p->set_parameter_value("sync_en", 1.0f);
                const float bpm = p->get_parameter_value("bpm", 120.0f);
                const float ms = audiocore::DelayNode::calculateTempoDelayMs(
                    bpm, static_cast<audiocore::TempoDivision>(divIdx));
                p->set_parameter_value("time_l", ms);
                if (p->get_parameter_value("link_lr", 0.0f) >= 0.5f) {
                    p->set_parameter_value("time_r", ms);
                } else {
                    p->set_parameter_value("time_r", std::clamp(ms * 1.5f, 10.0f, 2000.0f));
                }
                sync_channel_fx_to_dsp(target);
                view->last_param_fingerprint = compute_rack_fingerprint(target);
                schedule_ui_rebuild(view, true, true);
            }
        }, LV_EVENT_CLICKED, dc);
        lv_obj_add_event_cb(dbtn, [](lv_event_t* e) {
            delete static_cast<DivCtx*>(lv_event_get_user_data(e));
        }, LV_EVENT_DELETE, dc);
    }

    // Topology quick presets on right of sync bar
    static const char* kModes[3] = {"PING-PONG", "SLAPBACK", "AMBIENT"};
    for (int m = 0; m < 3; ++m) {
        auto* mbtn = lv_button_create(sync_bar);
        lv_obj_set_pos(mbtn, 892 + m * 96, 32);
        lv_obj_set_size(mbtn, 90, 28);
        ui::rack_skin::apply_hardware_button(
            mbtn, false, COLOR_STANDARD_BORDER, COLOR_CONTROL, COLOR_PANEL_SURFACE);
        auto* ml = lv_label_create(mbtn);
        lv_label_set_text(ml, kModes[m]);
        lv_obj_center(ml);
        lv_obj_set_style_text_font(ml, &ac_font_12, 0);
        lv_obj_set_style_text_color(ml, COLOR_SECONDARY_TEXT, 0);

        struct ModeCtx { FxScreenView* v; int mode; };
        auto* mc = new ModeCtx{v, m};
        lv_obj_add_event_cb(mbtn, [](lv_event_t* e) {
            auto* c = static_cast<ModeCtx*>(lv_event_get_user_data(e));
            if (!c || !c->v) return;
            FxScreenView* view = c->v;
            const int mode = c->mode;
            ChannelState* target = resolve_target_channel(view);
            if (!target) return;
            if (auto* p = target->fx_rack.get_slot(view->selected_slot)) {
                p->set_parameter_value("sync_en", 0.0f);
                p->set_parameter_value("link_lr", 0.0f);
                if (mode == 0) { // Ping-Pong
                    p->set_parameter_value("time_l", 250.0f);
                    p->set_parameter_value("time_r", 500.0f);
                    p->set_parameter_value("feedback", 0.45f);
                    p->set_parameter_value("width", 1.0f);
                } else if (mode == 1) { // Slapback
                    p->set_parameter_value("time_l", 85.0f);
                    p->set_parameter_value("time_r", 115.0f);
                    p->set_parameter_value("feedback", 0.12f);
                    p->set_parameter_value("width", 0.65f);
                } else { // Ambient
                    p->set_parameter_value("time_l", 420.0f);
                    p->set_parameter_value("time_r", 630.0f);
                    p->set_parameter_value("feedback", 0.58f);
                    p->set_parameter_value("tone", 4800.0f);
                    p->set_parameter_value("width", 0.95f);
                }
                sync_channel_fx_to_dsp(target);
                view->last_param_fingerprint = compute_rack_fingerprint(target);
                schedule_ui_rebuild(view, true, true);
            }
        }, LV_EVENT_CLICKED, mc);
        lv_obj_add_event_cb(mbtn, [](lv_event_t* e) {
            delete static_cast<ModeCtx*>(lv_event_get_user_data(e));
        }, LV_EVENT_DELETE, mc);
    }

    // Dual-Tap Echo Timeline Visualizer
    auto* vis_box = create_rack_module_box(
        v->editor_content, 16, 168, 710, 242,
        "STEREO L/R ECHO TAP TIMELINE & FEEDBACK DECAY REPEATS (0 - 2000 MS)",
        COLOR_STANDARD_BORDER, COLOR_PANEL_SURFACE);

    auto* canvas = lv_obj_create(vis_box);
    lv_obj_set_pos(canvas, 10, 32);
    lv_obj_set_size(canvas, 690, 200);
    lv_obj_remove_flag(canvas, LV_OBJ_FLAG_SCROLLABLE);
    ui::rack_skin::apply_recessed_display(canvas, COLOR_RECESSED, COLOR_STANDARD_BORDER);
    lv_obj_set_style_pad_all(canvas, 0, 0);
    v->visual_canvas = canvas;

    lv_obj_add_event_cb(canvas, [](lv_event_t* e) {
        auto* view = static_cast<FxScreenView*>(lv_event_get_user_data(e));
        if (!view) return;
        ChannelState* target = resolve_target_channel(view);
        if (!target) return;
        const auto* p = target->fx_rack.get_slot(view->selected_slot);
        if (!p || p->get_uri() != "http://audiocore.org/plugins/delay") return;

        lv_obj_t* obj = lv_event_get_target_obj(e);
        lv_layer_t* layer = lv_event_get_layer(e);
        lv_area_t coords;
        lv_obj_get_coords(obj, &coords);
        const int w = lv_area_get_width(&coords);
        const int h = lv_area_get_height(&coords);
        const int midY = coords.y1 + h / 2;

        // Center divider (Top = Left Tap, Bottom = Right Tap)
        lv_draw_line_dsc_t center_dsc;
        lv_draw_line_dsc_init(&center_dsc);
        center_dsc.color = COLOR_SUBTLE_SEPARATOR;
        center_dsc.width = 1;
        center_dsc.p1 = {static_cast<lv_value_precise_t>(coords.x1), static_cast<lv_value_precise_t>(midY)};
        center_dsc.p2 = {static_cast<lv_value_precise_t>(coords.x2), static_cast<lv_value_precise_t>(midY)};
        lv_draw_line(layer, &center_dsc);

        const bool sync = p->get_parameter_value("sync_en", 0.0f) >= 0.5f;
        const bool link = p->get_parameter_value("link_lr", 0.0f) >= 0.5f;
        const float bpm = p->get_parameter_value("bpm", 120.0f);
        const int div = std::clamp(static_cast<int>(std::round(p->get_parameter_value("sync_div", 2.0f))), 0, 6);
        const float timeL = sync
            ? audiocore::DelayNode::calculateTempoDelayMs(bpm, static_cast<audiocore::TempoDivision>(div))
            : p->get_parameter_value("time_l", 320.0f);
        const float timeR = link ? timeL : p->get_parameter_value("time_r", 480.0f);
        const float fb = std::clamp(p->get_parameter_value("feedback", 0.35f), 0.0f, 0.95f);

        lv_draw_line_dsc_t tapL_dsc;
        lv_draw_line_dsc_init(&tapL_dsc);
        tapL_dsc.color = COLOR_CYAN;
        tapL_dsc.width = 4;

        lv_draw_line_dsc_t tapR_dsc;
        lv_draw_line_dsc_init(&tapR_dsc);
        tapR_dsc.color = COLOR_ORANGE;
        tapR_dsc.width = 4;

        float gain = 1.0f;
        for (int rep = 1; rep <= 8; ++rep) {
            float tL = timeL * rep;
            if (tL <= 2000.0f) {
                int xL = coords.x1 + 16 + static_cast<int>((tL / 2000.0f) * (w - 32));
                int barH = static_cast<int>(gain * (h / 2 - 14));
                tapL_dsc.p1 = {static_cast<lv_value_precise_t>(xL), static_cast<lv_value_precise_t>(midY - 2)};
                tapL_dsc.p2 = {static_cast<lv_value_precise_t>(xL), static_cast<lv_value_precise_t>(midY - 2 - barH)};
                lv_draw_line(layer, &tapL_dsc);
            }
            float tR = timeR * rep;
            if (tR <= 2000.0f) {
                int xR = coords.x1 + 16 + static_cast<int>((tR / 2000.0f) * (w - 32));
                int barH = static_cast<int>(gain * (h / 2 - 14));
                tapR_dsc.p1 = {static_cast<lv_value_precise_t>(xR), static_cast<lv_value_precise_t>(midY + 2)};
                tapR_dsc.p2 = {static_cast<lv_value_precise_t>(xR), static_cast<lv_value_precise_t>(midY + 2 + barH)};
                lv_draw_line(layer, &tapR_dsc);
            }
            gain *= fb;
            if (gain < 0.05f) break;
        }
    }, LV_EVENT_DRAW_MAIN, v);

    // Right Top Box: I/O Metering & Echo Readout
    auto* meter_box = create_rack_module_box(
        v->editor_content, 738, 168, 470, 242,
        "ECHO DELAY TELEMETRY & FEEDBACK PASSBAND",
        COLOR_STANDARD_BORDER, COLOR_PANEL_SURFACE);
    build_io_meter_strip(v, meter_box, 14, 34, 442);

    const float effTimeL = syncEn
        ? audiocore::DelayNode::calculateTempoDelayMs(
              inst->get_parameter_value("bpm", 120.0f),
              static_cast<audiocore::TempoDivision>(activeDiv))
        : inst->get_parameter_value("time_l", 320.0f);
    const float effTimeR = linkLr ? effTimeL : inst->get_parameter_value("time_r", 480.0f);

    char delay_info[180];
    std::snprintf(delay_info, sizeof(delay_info),
                  "TAP L: %.1f ms (CYAN)   |   TAP R: %.1f ms (ORANGE)\nFEEDBACK FILTER: %.0f Hz HPF  ->  %.0f Hz LPF\nTEMPO: %.0f BPM (%s)",
                  effTimeL, effTimeR,
                  inst->get_parameter_value("low_cut", 80.0f),
                  inst->get_parameter_value("tone", 6500.0f),
                  inst->get_parameter_value("bpm", 120.0f),
                  syncEn ? kDivLabels[activeDiv] : "FREE MS");
    instrument_label(meter_box, delay_info, 16, 160, &ac_font_12, COLOR_SECONDARY_TEXT);

    // Bottom 3 Grouped Parameter Modules
    auto* grp_time = create_rack_module_box(
        v->editor_content, 16, 420, 388, 386,
        "1. DELAY TIME L / R & TEMPO BPM", COLOR_STANDARD_BORDER, COLOR_PANEL_SURFACE);
    place_param_knob(grp_time, inst, "time_l", "DELAY TIME L", 44, 58, 86);
    place_param_knob(grp_time, inst, "time_r", "DELAY TIME R", 224, 58, 86);
    place_param_knob(grp_time, inst, "bpm", "TEMPO BPM", 134, 218, 82);

    auto* grp_fb = create_rack_module_box(
        v->editor_content, 418, 420, 388, 386,
        "2. FEEDBACK & DAMPING FILTERS", COLOR_STANDARD_BORDER, COLOR_PANEL_SURFACE);
    place_param_knob(grp_fb, inst, "feedback", "FEEDBACK", 134, 58, 86);
    place_param_knob(grp_fb, inst, "low_cut", "LOW CUT HPF", 44, 218, 82);
    place_param_knob(grp_fb, inst, "tone", "HIGH CUT LPF", 224, 218, 82);

    auto* grp_mix = create_rack_module_box(
        v->editor_content, 820, 420, 388, 386,
        "3. STEREO PING-PONG WIDTH & MIX", COLOR_STANDARD_BORDER, COLOR_PANEL_SURFACE);
    place_param_knob(grp_mix, inst, "width", "STEREO WIDTH", 134, 58, 86);
    place_param_knob(grp_mix, inst, "mix", isAuxBusChannel(ch) ? "WET RETURN LVL" : "DRY/WET MIX", 134, 218, 86);
}

// ============================================================================
// 3. AFS RACK EDITOR (Automatic Feedback Suppressor & 8-Notch Bank Analyzer)
// ============================================================================
void build_afs_rack_editor(FxScreenView* v, ChannelState* ch, PluginInstance* inst) {
    auto* top_bar = create_rack_module_box(
        v->editor_content, 16, 94, 1192, 62,
        "AUTOMATIC FEEDBACK SUPPRESSOR (AFS)  —  2048-PT FFT PROMINENCE DETECTOR & BIQUAD NOTCH BANK",
        COLOR_GREEN, lv_color_hex(0x151F1B));

    // Live Detector Status Badge
    auto* badge = lv_obj_create(top_bar);
    lv_obj_set_pos(badge, 14, 30);
    lv_obj_set_size(badge, 240, 26);
    lv_obj_remove_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
    ui::rack_skin::apply_recessed_display(badge, lv_color_hex(0x0B1A12), COLOR_GREEN);
    v->afs_status_badge = badge;

    v->afs_status_label = lv_label_create(badge);
    lv_label_set_text(v->afs_status_label, "DETECTOR: MONITORING");
    lv_obj_center(v->afs_status_label);
    lv_obj_set_style_text_font(v->afs_status_label, &ac_font_12, 0);
    lv_obj_set_style_text_color(v->afs_status_label, COLOR_GREEN, 0);

    // Active Notch Counter
    v->afs_count_label = instrument_label(top_bar, "ACTIVE NOTCHES: 0 / 8", 274, 35, &ac_font_14, COLOR_CYAN);

    // Sensitivity Mode Presets
    static const char* kSensNames[3] = {"SPEECH (85%)", "MUSIC (75%)", "CONSERVATIVE (55%)"};
    static const float kSensVals[3] = {0.85f, 0.75f, 0.55f};
    for (int m = 0; m < 3; ++m) {
        auto* sbtn = lv_button_create(top_bar);
        lv_obj_set_pos(sbtn, 500 + m * 156, 30);
        lv_obj_set_size(sbtn, 148, 26);
        ui::rack_skin::apply_hardware_button(
            sbtn, false, COLOR_GREEN, lv_color_hex(0x163826), lv_color_hex(0x1B2229));
        auto* sl = lv_label_create(sbtn);
        lv_label_set_text(sl, kSensNames[m]);
        lv_obj_center(sl);
        lv_obj_set_style_text_font(sl, &ac_font_12, 0);
        lv_obj_set_style_text_color(sl, COLOR_TEXT, 0);

        struct SensCtx { FxScreenView* v; float val; };
        auto* sc = new SensCtx{v, kSensVals[m]};
        lv_obj_add_event_cb(sbtn, [](lv_event_t* e) {
            auto* c = static_cast<SensCtx*>(lv_event_get_user_data(e));
            if (!c || !c->v) return;
            FxScreenView* view = c->v;
            const float val = c->val;
            ChannelState* target = resolve_target_channel(view);
            if (!target) return;
            if (auto* p = target->fx_rack.get_slot(view->selected_slot)) {
                p->set_parameter_value("sensitivity", val);
                sync_channel_fx_to_dsp(target);
                view->last_param_fingerprint = compute_rack_fingerprint(target);
                schedule_ui_rebuild(view, true, true);
            }
        }, LV_EVENT_CLICKED, sc);
        lv_obj_add_event_cb(sbtn, [](lv_event_t* e) {
            delete static_cast<SensCtx*>(lv_event_get_user_data(e));
        }, LV_EVENT_DELETE, sc);
    }

    // CLEAR / RESET NOTCHES button
    auto* clr_btn = lv_button_create(top_bar);
    lv_obj_set_pos(clr_btn, 984, 30);
    lv_obj_set_size(clr_btn, 192, 26);
    ui::rack_skin::apply_hardware_button(
        clr_btn, true, COLOR_RED, lv_color_hex(0x3B151A), lv_color_hex(0x281215));
    auto* cl = lv_label_create(clr_btn);
    lv_label_set_text(cl, "CLEAR / RESET NOTCHES");
    lv_obj_center(cl);
    lv_obj_set_style_text_font(cl, &ac_font_12, 0);
    lv_obj_set_style_text_color(cl, COLOR_RED, 0);
    lv_obj_add_event_cb(clr_btn, [](lv_event_t* e) {
        auto* view = static_cast<FxScreenView*>(lv_event_get_user_data(e));
        ChannelState* target = resolve_target_channel(view);
        if (!target) return;
        if (auto* p = target->fx_rack.get_slot(view->selected_slot)) {
            const float seq = p->get_parameter_value("clear_seq", 0.0f) + 1.0f;
            p->set_parameter_value("clear_seq", seq);
            target->afs_active_notch_count = 0;
            target->afs_notches = {};
            sync_channel_fx_to_dsp(target);
            view->last_param_fingerprint = compute_rack_fingerprint(target);
            schedule_ui_rebuild(view, false, true);
        }
    }, LV_EVENT_CLICKED, v);

    // 20 Hz - 20 kHz Logarithmic Notch Suppression Curve
    auto* vis_box = create_rack_module_box(
        v->editor_content, 16, 164, 680, 248,
        "20 HZ - 20 KHZ LOGARITHMIC NOTCH SUPPRESSION TRANSFER CURVE",
        COLOR_GREEN, lv_color_hex(0x151F1B));

    auto* canvas = lv_obj_create(vis_box);
    lv_obj_set_pos(canvas, 10, 32);
    lv_obj_set_size(canvas, 660, 206);
    lv_obj_remove_flag(canvas, LV_OBJ_FLAG_SCROLLABLE);
    ui::rack_skin::apply_recessed_display(canvas, lv_color_hex(0x050A08), lv_color_hex(0x223C30));
    lv_obj_set_style_pad_all(canvas, 0, 0);
    v->visual_canvas = canvas;

    lv_obj_add_event_cb(canvas, [](lv_event_t* e) {
        auto* view = static_cast<FxScreenView*>(lv_event_get_user_data(e));
        if (!view) return;
        ChannelState* target = resolve_target_channel(view);
        if (!target) return;

        lv_obj_t* obj = lv_event_get_target_obj(e);
        lv_layer_t* layer = lv_event_get_layer(e);
        lv_area_t coords;
        lv_obj_get_coords(obj, &coords);
        const int w = lv_area_get_width(&coords);
        const int h = lv_area_get_height(&coords);

        // Logarithmic frequency grid (100Hz, 1kHz, 10kHz)
        lv_draw_line_dsc_t grid_dsc;
        lv_draw_line_dsc_init(&grid_dsc);
        grid_dsc.color = lv_color_hex(0x16241E);
        grid_dsc.width = 1;
        for (float f : {100.0f, 1000.0f, 10000.0f}) {
            int gx = coords.x1 + static_cast<int>(FrequencyAxis::frequency_to_norm(f) * w);
            grid_dsc.p1 = {static_cast<lv_value_precise_t>(gx), static_cast<lv_value_precise_t>(coords.y1)};
            grid_dsc.p2 = {static_cast<lv_value_precise_t>(gx), static_cast<lv_value_precise_t>(coords.y2)};
            lv_draw_line(layer, &grid_dsc);
        }
        for (int g = 1; g < 4; ++g) {
            int gy = coords.y1 + (h * g) / 4;
            grid_dsc.p1 = {static_cast<lv_value_precise_t>(coords.x1), static_cast<lv_value_precise_t>(gy)};
            grid_dsc.p2 = {static_cast<lv_value_precise_t>(coords.x2), static_cast<lv_value_precise_t>(gy)};
            lv_draw_line(layer, &grid_dsc);
        }

        // Composite Notch Attenuation Curve (0 dB at top y1+16, -24 dB at bottom y2-12)
        lv_draw_line_dsc_t curve_dsc;
        lv_draw_line_dsc_init(&curve_dsc);
        curve_dsc.color = COLOR_GREEN;
        curve_dsc.width = 2;

        const uint8_t count = std::min<uint8_t>(target->afs_active_notch_count, 8);
        int prevX = coords.x1;
        int prevY = coords.y1 + 16;

        for (int px = 0; px < w; px += 3) {
            const float norm = static_cast<float>(px) / static_cast<float>(std::max(1, w - 1));
            const float freq = FrequencyAxis::norm_to_frequency(norm);
            float totalCutDb = 0.0f;
            for (uint8_t n = 0; n < count; ++n) {
                const auto& notch = target->afs_notches[n];
                if (notch.state == 0 || notch.depth_db >= -0.05f) continue;
                const float ratio = freq / std::max(20.0f, notch.frequency_hz);
                const float bw = (ratio - 1.0f / std::max(0.001f, ratio)) * std::max(5.0f, notch.q);
                const float bell = 1.0f / (1.0f + bw * bw);
                totalCutDb += notch.depth_db * bell;
            }
            totalCutDb = std::clamp(totalCutDb, -24.0f, 0.0f);
            const int x = coords.x1 + px;
            const int y = coords.y1 + 16 + static_cast<int>((-totalCutDb / 24.0f) * (h - 28));
            if (px > 0) {
                curve_dsc.p1 = {static_cast<lv_value_precise_t>(prevX), static_cast<lv_value_precise_t>(prevY)};
                curve_dsc.p2 = {static_cast<lv_value_precise_t>(x), static_cast<lv_value_precise_t>(y)};
                lv_draw_line(layer, &curve_dsc);
            }
            prevX = x;
            prevY = y;
        }

        // Vertical markers for active notches
        lv_draw_line_dsc_t mark_dsc;
        lv_draw_line_dsc_init(&mark_dsc);
        mark_dsc.color = COLOR_RED;
        mark_dsc.width = 1;
        for (uint8_t n = 0; n < count; ++n) {
            const auto& notch = target->afs_notches[n];
            if (notch.state == 0) continue;
            int nx = coords.x1 + static_cast<int>(FrequencyAxis::frequency_to_norm(notch.frequency_hz) * w);
            mark_dsc.p1 = {static_cast<lv_value_precise_t>(nx), static_cast<lv_value_precise_t>(coords.y1 + 16)};
            mark_dsc.p2 = {static_cast<lv_value_precise_t>(nx), static_cast<lv_value_precise_t>(coords.y2 - 8)};
            lv_draw_line(layer, &mark_dsc);
        }
    }, LV_EVENT_DRAW_MAIN, v);

    // 8-Slot Notch Filter Bank Table on Right
    auto* bank_box = create_rack_module_box(
        v->editor_content, 708, 164, 500, 248,
        "LIVE 8-NOTCH FILTER BANK STATUS (REALTIME TELEMETRY)",
        COLOR_GREEN, lv_color_hex(0x151F1B));

    instrument_label(bank_box, "SLOT      FREQUENCY         ATTENUATION         STATE",
                     16, 30, &ac_font_12, COLOR_TEXT2);

    for (int i = 0; i < 8; ++i) {
        const int ry = 50 + i * 23;
        char slot_id[16];
        std::snprintf(slot_id, sizeof(slot_id), "#%02d", i + 1);
        instrument_label(bank_box, slot_id, 16, ry, &ac_font_12, COLOR_CYAN);
        v->afs_notch_freq_lbl[i] = instrument_label(bank_box, "---- Hz", 90, ry, &ac_font_12, COLOR_TEXT2);
        v->afs_notch_depth_lbl[i] = instrument_label(bank_box, "0.0 dB", 236, ry, &ac_font_12, COLOR_TEXT2);
        v->afs_notch_state_lbl[i] = instrument_label(bank_box, "FREE", 380, ry, &ac_font_12, COLOR_TEXT2);
    }

    // Bottom 3 Grouped Parameter Modules + Live I/O Meter Strip
    auto* grp_det = create_rack_module_box(
        v->editor_content, 16, 420, 388, 386,
        "1. FEEDBACK DETECTOR & CAPACITY", COLOR_GREEN, lv_color_hex(0x151F1B));
    place_param_knob(grp_det, inst, "sensitivity", "SENSITIVITY", 44, 48, 82);
    place_param_knob(grp_det, inst, "max_notches", "MAX NOTCHES", 224, 48, 82);
    build_io_meter_strip(v, grp_det, 12, 232, 364);

    auto* grp_notch = create_rack_module_box(
        v->editor_content, 418, 420, 388, 386,
        "2. NOTCH FILTER GEOMETRY", COLOR_GREEN, lv_color_hex(0x151F1B));
    place_param_knob(grp_notch, inst, "max_depth", "NOTCH DEPTH", 44, 96, 86);
    place_param_knob(grp_notch, inst, "q", "NOTCH Q", 224, 96, 86);

    auto* grp_env = create_rack_module_box(
        v->editor_content, 820, 420, 388, 386,
        "3. NOTCH LIFECYCLE TIMING", COLOR_GREEN, lv_color_hex(0x151F1B));
    place_param_knob(grp_env, inst, "attack", "ATTACK", 44, 58, 82);
    place_param_knob(grp_env, inst, "hold", "HOLD TIME", 224, 58, 82);
    place_param_knob(grp_env, inst, "release", "RELEASE", 134, 218, 82);
    (void)ch;
}

// ============================================================================
// 4. NAM LITE NEURAL AMP MODELER RACK EDITOR
// ============================================================================
void build_nam_rack_editor(FxScreenView* v, ChannelState* ch, PluginInstance* inst) {
    auto* meter_box = create_rack_module_box(
        v->editor_content, 16, 94, 1192, 146,
        "NAM LITE NEURAL AMP MODELER & LIVE PCM INFERENCE TELEMETRY",
        COLOR_STANDARD_BORDER, COLOR_PANEL_SURFACE);
    build_io_meter_strip(v, meter_box, 16, 30, 620);

    char model_info[256];
    const bool valid = inst->meets_realtime_budget() && inst->get_validation_error().empty();
    std::snprintf(model_info, sizeof(model_info),
                  "MODEL: %s\nARCH: %s  |  RATE: 48.0 kHz\nSTATUS: %s",
                  inst->get_name().c_str(),
                  inst->get_description().c_str(),
                  valid ? "VALIDATED FOR REALTIME DSP (LOW CPU)" : inst->get_validation_error().c_str());
    instrument_label(meter_box, model_info, 670, 36, &ac_font_14, valid ? COLOR_CYAN : COLOR_ORANGE);

    auto* param_box = create_rack_module_box(
        v->editor_content, 16, 252, 1192, 554,
        "AMPLIFIER INFERENCE PARAMETERS & MODEL SELECTION", COLOR_STANDARD_BORDER, COLOR_PANEL_SURFACE);

    if (auto* in_p = inst->find_parameter("in_gain")) {
        instrument_knob(param_box, in_p->name.c_str(), &in_p->current_val,
                        in_p->min_val, in_p->max_val, in_p->default_val,
                        in_p->unit.c_str(), 180, 56, 82);
    }
    if (auto* out_p = inst->find_parameter("out_level")) {
        instrument_knob(param_box, out_p->name.c_str(), &out_p->current_val,
                        out_p->min_val, out_p->max_val, out_p->default_val,
                        out_p->unit.c_str(), 420, 56, 82);
    }

    auto* change_btn = lv_button_create(param_box);
    lv_obj_set_pos(change_btn, 680, 70);
    lv_obj_set_size(change_btn, 240, 44);
    ui::rack_skin::apply_hardware_button(change_btn, false, COLOR_CYAN, lv_color_hex(0x1A384A), lv_color_hex(0x182028));
    auto* cl = lv_label_create(change_btn);
    lv_label_set_text(cl, "CHANGE NAM MODEL");
    lv_obj_center(cl);
    lv_obj_set_style_text_color(cl, COLOR_CYAN, 0);
    lv_obj_add_event_cb(change_btn, [](lv_event_t* e) {
        auto* view = static_cast<FxScreenView*>(lv_event_get_user_data(e));
        if (view) show_add_plugin_modal(view);
    }, LV_EVENT_CLICKED, v);
    (void)ch;
}

// ============================================================================
// 5. STUDIO COMPRESSOR RACK EDITOR (Vintage VU Meter & Grey Brushed Aluminum)
// ============================================================================

// Non-linear deflection mapping for classic analog VU Gain Reduction scale:
// 0 dB sits at rest on the far right (308.0 deg).
// Light compression (-1 to -5 dB) has wide, clear visual spread.
// Heavy compression (-10 to -30 dB) compresses gracefully toward the left stop (228.0 deg).
static float comp_gr_to_angle(float gr_db) {
    const float mag = std::clamp(-gr_db, 0.0f, 30.0f);
    static const struct GrPoint { float db; float ang; } pts[] = {
        {0.0f,  308.0f},
        {1.0f,  300.0f},
        {2.0f,  292.5f},
        {3.0f,  285.5f},
        {4.0f,  279.0f},
        {5.0f,  273.0f},
        {7.0f,  263.0f},
        {10.0f, 253.0f},
        {15.0f, 243.0f},
        {20.0f, 237.0f},
        {30.0f, 228.0f}
    };
    for (size_t i = 0; i < sizeof(pts)/sizeof(pts[0]) - 1; ++i) {
        if (mag <= pts[i+1].db) {
            float t = (mag - pts[i].db) / (pts[i+1].db - pts[i].db);
            return pts[i].ang + t * (pts[i+1].ang - pts[i].ang);
        }
    }
    return 228.0f;
}

static inline float calculate_comp_out(float in_db, float thresh, float ratio, float knee, float makeup) {
    float gr = 0.0f;
    const float half_k = knee * 0.5f;
    if (knee > 0.05f && in_db > (thresh - half_k) && in_db < (thresh + half_k)) {
        float delta = in_db - thresh + half_k;
        gr = ((1.0f / ratio) - 1.0f) * (delta * delta) / (2.0f * knee);
    } else if (in_db >= (thresh + half_k)) {
        gr = (in_db - thresh) * ((1.0f / ratio) - 1.0f);
    }
    return in_db + gr + makeup;
}

void build_compressor_rack_editor(FxScreenView* v, ChannelState* ch, PluginInstance* inst) {
    // ------------------------------------------------------------------------
    // TOP LEFT: VINTAGE GAIN REDUCTION VU METER (586 x 350)
    // ------------------------------------------------------------------------
    auto* meter_box = create_grey_rack_module_box(
        v->editor_content, 16, 94, 586, 350,
        "GAIN REDUCTION VU METER",
        lv_color_hex(0x9EAEBD), lv_color_hex(0x323840));

    auto* vu_canvas = lv_obj_create(meter_box);
    lv_obj_set_pos(vu_canvas, 14, 34);
    lv_obj_set_size(vu_canvas, 558, 302);
    lv_obj_remove_flag(vu_canvas, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(vu_canvas, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(vu_canvas, 0, 0);
    lv_obj_set_style_pad_all(vu_canvas, 0, 0);
    v->comp_vu_canvas = vu_canvas;

    lv_obj_add_event_cb(vu_canvas, [](lv_event_t* e) {
        auto* view = static_cast<FxScreenView*>(lv_event_get_user_data(e));
        if (!view) return;
        lv_obj_t* obj = lv_event_get_target_obj(e);
        lv_layer_t* layer = lv_event_get_layer(e);
        lv_area_t coords;
        lv_obj_get_coords(obj, &coords);

        // 1. Heavy phenolic / dark cast iron outer bezel frame
        lv_draw_rect_dsc_t bezel_dsc;
        lv_draw_rect_dsc_init(&bezel_dsc);
        bezel_dsc.radius = 8;
        bezel_dsc.bg_color = lv_color_hex(0x13171B);
        bezel_dsc.bg_opa = LV_OPA_COVER;
        bezel_dsc.border_color = lv_color_hex(0x3E4650);
        bezel_dsc.border_width = 3;
        lv_draw_rect(layer, &bezel_dsc, &coords);

        // 2. Aged ivory / cream dial face plate
        lv_area_t dial_area = {coords.x1 + 6, coords.y1 + 6, coords.x2 - 6, coords.y2 - 6};
        lv_draw_rect_dsc_t dial_dsc;
        lv_draw_rect_dsc_init(&dial_dsc);
        dial_dsc.radius = 4;
        dial_dsc.bg_color = lv_color_hex(0xF4ECE1);
        dial_dsc.bg_opa = LV_OPA_COVER;
        dial_dsc.border_color = lv_color_hex(0x2A3036);
        dial_dsc.border_width = 1;
        lv_draw_rect(layer, &dial_dsc, &dial_area);

        // Warm incandescent overhead lamp glow at top of dial
        lv_area_t lamp_area = {coords.x1 + 8, coords.y1 + 8, coords.x2 - 8, coords.y1 + 86};
        lv_draw_rect_dsc_t lamp_dsc;
        lv_draw_rect_dsc_init(&lamp_dsc);
        lamp_dsc.radius = 4;
        lamp_dsc.bg_color = lv_color_hex(0xFFF7E6);
        lamp_dsc.bg_opa = LV_OPA_40;
        lv_draw_rect(layer, &lamp_dsc, &lamp_area);

        const int cx = coords.x1 + (coords.x2 - coords.x1) / 2;
        const int cy = coords.y1 + 284;
        const int r_scale = 194;
        const int r_inner = 184;

        // 3. Arcs
        // Red overload / heavy limiting zone arc (-30 to -20 dB, angles 228 to 237)
        lv_draw_arc_dsc_t red_arc;
        lv_draw_arc_dsc_init(&red_arc);
        red_arc.center.x = cx;
        red_arc.center.y = cy;
        red_arc.radius = r_scale + 3;
        red_arc.width = 4;
        red_arc.color = lv_color_hex(0xC62828);
        red_arc.start_angle = 228;
        red_arc.end_angle = 237;
        lv_draw_arc(layer, &red_arc);

        // Primary black calibrated scale baseline arc (angles 228 to 310)
        lv_draw_arc_dsc_t main_arc;
        lv_draw_arc_dsc_init(&main_arc);
        main_arc.center.x = cx;
        main_arc.center.y = cy;
        main_arc.radius = r_scale;
        main_arc.width = 2;
        main_arc.color = lv_color_hex(0x222222);
        main_arc.start_angle = 228;
        main_arc.end_angle = 310;
        lv_draw_arc(layer, &main_arc);

        // Secondary inner hairline arc
        lv_draw_arc_dsc_t inner_arc;
        lv_draw_arc_dsc_init(&inner_arc);
        inner_arc.center.x = cx;
        inner_arc.center.y = cy;
        inner_arc.radius = r_inner;
        inner_arc.width = 1;
        inner_arc.color = lv_color_hex(0x555555);
        inner_arc.start_angle = 228;
        inner_arc.end_angle = 310;
        lv_draw_arc(layer, &inner_arc);

        // 4. Major ticks with numerical markings
        static const struct CalibTick { float db; const char* lbl; } kTicks[] = {
            {0.0f,  "0"},
            {1.0f,  "1"},
            {2.0f,  "2"},
            {3.0f,  "3"},
            {4.0f,  "4"},
            {5.0f,  "5"},
            {7.0f,  "7"},
            {10.0f, "10"},
            {15.0f, "15"},
            {20.0f, "20"},
            {30.0f, "30"}
        };

        for (const auto& tk : kTicks) {
            float ang = comp_gr_to_angle(-tk.db);
            float rad = ang * static_cast<float>(M_PI / 180.0f);
            float ca = std::cos(rad);
            float sa = std::sin(rad);

            lv_draw_line_dsc_t t_dsc;
            lv_draw_line_dsc_init(&t_dsc);
            t_dsc.color = (tk.db >= 20.0f) ? lv_color_hex(0x9E1B1B) : lv_color_hex(0x181818);
            t_dsc.width = 2;
            t_dsc.p1 = {static_cast<lv_value_precise_t>(cx + (r_scale - 2) * ca),
                        static_cast<lv_value_precise_t>(cy + (r_scale - 2) * sa)};
            t_dsc.p2 = {static_cast<lv_value_precise_t>(cx + (r_scale + 9) * ca),
                        static_cast<lv_value_precise_t>(cy + (r_scale + 9) * sa)};
            lv_draw_line(layer, &t_dsc);

            // Number text
            lv_draw_label_dsc_t l_dsc;
            lv_draw_label_dsc_init(&l_dsc);
            l_dsc.font = &ac_font_12;
            l_dsc.color = (tk.db >= 20.0f) ? lv_color_hex(0x9E1B1B) : lv_color_hex(0x181818);
            l_dsc.text = tk.lbl;
            l_dsc.align = LV_TEXT_ALIGN_CENTER;
            const int tx = cx + static_cast<int>((r_scale - 18) * ca) - 10;
            const int ty = cy + static_cast<int>((r_scale - 18) * sa) - 7;
            lv_area_t ta = {tx, ty, tx + 20, ty + 14};
            lv_draw_label(layer, &l_dsc, &ta);
        }

        // Minor intermediate ticks
        static const float kMinorDb[] = {0.5f, 1.5f, 2.5f, 3.5f, 6.0f, 8.0f, 12.0f, 25.0f};
        for (float mdb : kMinorDb) {
            float ang = comp_gr_to_angle(-mdb);
            float rad = ang * static_cast<float>(M_PI / 180.0f);
            float ca = std::cos(rad);
            float sa = std::sin(rad);

            lv_draw_line_dsc_t m_dsc;
            lv_draw_line_dsc_init(&m_dsc);
            m_dsc.color = lv_color_hex(0x666666);
            m_dsc.width = 1;
            m_dsc.p1 = {static_cast<lv_value_precise_t>(cx + (r_scale - 1) * ca),
                        static_cast<lv_value_precise_t>(cy + (r_scale - 1) * sa)};
            m_dsc.p2 = {static_cast<lv_value_precise_t>(cx + (r_scale + 5) * ca),
                        static_cast<lv_value_precise_t>(cy + (r_scale + 5) * sa)};
            lv_draw_line(layer, &m_dsc);
        }

        // 5. Dial Silkscreen Typography
        lv_draw_label_dsc_t vu_lbl;
        lv_draw_label_dsc_init(&vu_lbl);
        vu_lbl.font = &ac_font_28;
        vu_lbl.color = lv_color_hex(0x181818);
        vu_lbl.text = "VU";
        vu_lbl.align = LV_TEXT_ALIGN_CENTER;
        lv_area_t vu_area = {cx - 40, cy - 128, cx + 40, cy - 98};
        lv_draw_label(layer, &vu_lbl, &vu_area);

        lv_draw_label_dsc_t gr_lbl;
        lv_draw_label_dsc_init(&gr_lbl);
        gr_lbl.font = &ac_font_12;
        gr_lbl.color = lv_color_hex(0x3C3C3C);
        gr_lbl.text = "GAIN REDUCTION dB";
        gr_lbl.align = LV_TEXT_ALIGN_CENTER;
        lv_area_t gr_area = {cx - 90, cy - 98, cx + 90, cy - 82};
        lv_draw_label(layer, &gr_lbl, &gr_area);
        // 6. Needle & Cast Shadow
        const float cur_ang = comp_gr_to_angle(view->comp_needle_gr_db);
        const float n_rad = cur_ang * static_cast<float>(M_PI / 180.0f);
        const float n_ca = std::cos(n_rad);
        const float n_sa = std::sin(n_rad);

        // Cast shadow line (offset by +3, +3)
        lv_draw_line_dsc_t sh_dsc;
        lv_draw_line_dsc_init(&sh_dsc);
        sh_dsc.color = lv_color_hex(0x9E9284);
        sh_dsc.width = 3;
        sh_dsc.p1 = {static_cast<lv_value_precise_t>(cx + 3), static_cast<lv_value_precise_t>(cy + 3)};
        sh_dsc.p2 = {static_cast<lv_value_precise_t>(cx + 3 + (r_scale + 8) * n_ca),
                     static_cast<lv_value_precise_t>(cy + 3 + (r_scale + 8) * n_sa)};
        lv_draw_line(layer, &sh_dsc);

        // Counterweight tail (backwards by 22 px)
        lv_draw_line_dsc_t tail_dsc;
        lv_draw_line_dsc_init(&tail_dsc);
        tail_dsc.color = lv_color_hex(0x181818);
        tail_dsc.width = 3;
        tail_dsc.p1 = {static_cast<lv_value_precise_t>(cx), static_cast<lv_value_precise_t>(cy)};
        tail_dsc.p2 = {static_cast<lv_value_precise_t>(cx - 22 * n_ca), static_cast<lv_value_precise_t>(cy - 22 * n_sa)};
        lv_draw_line(layer, &tail_dsc);

        // Counterweight disc
        lv_draw_rect_dsc_t cd_dsc;
        lv_draw_rect_dsc_init(&cd_dsc);
        cd_dsc.radius = 4;
        cd_dsc.bg_color = lv_color_hex(0x181818);
        cd_dsc.bg_opa = LV_OPA_COVER;
        int disck_x = cx - static_cast<int>(18 * n_ca);
        int disck_y = cy - static_cast<int>(18 * n_sa);
        lv_area_t cda = {disck_x - 4, disck_y - 4, disck_x + 4, disck_y + 4};
        lv_draw_rect(layer, &cd_dsc, &cda);

        // Main needle body (black from pivot to r_scale - 26)
        lv_draw_line_dsc_t n_dsc;
        lv_draw_line_dsc_init(&n_dsc);
        n_dsc.color = lv_color_hex(0x151515);
        n_dsc.width = 3;
        n_dsc.p1 = {static_cast<lv_value_precise_t>(cx), static_cast<lv_value_precise_t>(cy)};
        n_dsc.p2 = {static_cast<lv_value_precise_t>(cx + (r_scale - 26) * n_ca),
                    static_cast<lv_value_precise_t>(cy + (r_scale - 26) * n_sa)};
        lv_draw_line(layer, &n_dsc);

        // Needle high-visibility vermilion red pointer tip (from r_scale - 28 to r_scale + 12)
        lv_draw_line_dsc_t tip_dsc;
        lv_draw_line_dsc_init(&tip_dsc);
        tip_dsc.color = lv_color_hex(0xE62612);
        tip_dsc.width = 2;
        tip_dsc.p1 = {static_cast<lv_value_precise_t>(cx + (r_scale - 28) * n_ca),
                      static_cast<lv_value_precise_t>(cy + (r_scale - 28) * n_sa)};
        tip_dsc.p2 = {static_cast<lv_value_precise_t>(cx + (r_scale + 12) * n_ca),
                      static_cast<lv_value_precise_t>(cy + (r_scale + 12) * n_sa)};
        lv_draw_line(layer, &tip_dsc);

        // 7. Pivot center: outer gunmetal ring & black cap
        lv_draw_rect_dsc_t pring_dsc;
        lv_draw_rect_dsc_init(&pring_dsc);
        pring_dsc.radius = 18;
        pring_dsc.bg_color = lv_color_hex(0x282E34);
        pring_dsc.bg_opa = LV_OPA_COVER;
        pring_dsc.border_color = lv_color_hex(0x4A535E);
        pring_dsc.border_width = 2;
        lv_area_t pra = {cx - 18, cy - 18, cx + 18, cy + 18};
        lv_draw_rect(layer, &pring_dsc, &pra);

        lv_draw_rect_dsc_t pcap_dsc;
        lv_draw_rect_dsc_init(&pcap_dsc);
        pcap_dsc.radius = 12;
        pcap_dsc.bg_color = lv_color_hex(0x141618);
        pcap_dsc.bg_opa = LV_OPA_COVER;
        lv_area_t pca = {cx - 12, cy - 12, cx + 12, cy + 12};
        lv_draw_rect(layer, &pcap_dsc, &pca);

        // Top specular crescent highlight on pivot cap
        lv_draw_rect_dsc_t phl_dsc;
        lv_draw_rect_dsc_init(&phl_dsc);
        phl_dsc.radius = 8;
        phl_dsc.bg_color = lv_color_hex(0xFFFFFF);
        phl_dsc.bg_opa = LV_OPA_30;
        lv_area_t phla = {cx - 8, cy - 11, cx + 8, cy - 4};
        lv_draw_rect(layer, &phl_dsc, &phla);

        // 8. Curved glass specular reflection highlight
        lv_draw_arc_dsc_t glass_arc;
        lv_draw_arc_dsc_init(&glass_arc);
        glass_arc.center.x = cx;
        glass_arc.center.y = cy - 20;
        glass_arc.radius = 240;
        glass_arc.width = 6;
        glass_arc.color = lv_color_hex(0xFFFFFF);
        glass_arc.opa = LV_OPA_20;
        glass_arc.start_angle = 232;
        glass_arc.end_angle = 308;
        lv_draw_arc(layer, &glass_arc);
    }, LV_EVENT_DRAW_MAIN, v);

    // Digital GR Readout Badge (Bottom-Right of Meter Enclosure)
    auto* gr_badge = lv_obj_create(meter_box);
    lv_obj_set_pos(gr_badge, 376, 296);
    lv_obj_set_size(gr_badge, 186, 30);
    lv_obj_remove_flag(gr_badge, LV_OBJ_FLAG_SCROLLABLE);
    ui::rack_skin::apply_recessed_display(gr_badge, lv_color_hex(0x0C0F12), lv_color_hex(0x343E48));
    lv_obj_set_style_pad_all(gr_badge, 0, 0);

    v->comp_gr_readout_lbl = instrument_label(
        gr_badge, "GR: 0.0 dB (IDLE)", 8, 6, &ac_font_12, COLOR_TEXT2);

    // ------------------------------------------------------------------------
    // TOP RIGHT: DYNAMIC TELEMETRY & VCA TRANSFER FUNCTION (590 x 350)
    // ------------------------------------------------------------------------
    auto* telemetry_box = create_grey_rack_module_box(
        v->editor_content, 618, 94, 590, 350,
        "LEVELS & TRANSFER CURVE",
        lv_color_hex(0x9EAEBD), lv_color_hex(0x323840));

    // 28-Segment Hardware LED Bargraph for PRE-FX and POST-FX
    build_io_meter_strip(v, telemetry_box, 14, 32, 562);

    // Left half: Realtime VCA Transfer Curve Plot
    auto* curve_canvas = lv_obj_create(telemetry_box);
    lv_obj_set_pos(curve_canvas, 14, 156);
    lv_obj_set_size(curve_canvas, 276, 180);
    lv_obj_remove_flag(curve_canvas, LV_OBJ_FLAG_SCROLLABLE);
    ui::rack_skin::apply_recessed_display(curve_canvas, lv_color_hex(0x080B0E), lv_color_hex(0x2E3640));
    lv_obj_set_style_pad_all(curve_canvas, 0, 0);
    v->comp_curve_canvas = curve_canvas;

    lv_obj_add_event_cb(curve_canvas, [](lv_event_t* e) {
        auto* view = static_cast<FxScreenView*>(lv_event_get_user_data(e));
        if (!view) return;
        ChannelState* target = resolve_target_channel(view);
        if (!target) return;
        auto* inst_ptr = target->fx_rack.get_slot(view->selected_slot);
        if (!inst_ptr) return;

        lv_obj_t* obj = lv_event_get_target_obj(e);
        lv_layer_t* layer = lv_event_get_layer(e);
        lv_area_t coords;
        lv_obj_get_coords(obj, &coords);

        const float thresh = inst_ptr->get_parameter_value("thresh", -18.0f);
        const float ratio = inst_ptr->get_parameter_value("ratio", 4.0f);
        const float knee = inst_ptr->get_parameter_value("knee", 6.0f);
        const float makeup = inst_ptr->get_parameter_value("makeup", 0.0f);

        const int x0 = coords.x1 + 10;
        const int x1 = coords.x2 - 10;
        const int y0 = coords.y2 - 10;
        const int y1 = coords.y1 + 12;

        auto db_to_px = [&](float in_d, float out_d) -> lv_point_precise_t {
            float nx = std::clamp((in_d + 60.0f) / 60.0f, 0.0f, 1.0f);
            float ny = std::clamp((out_d + 60.0f) / 60.0f, 0.0f, 1.0f);
            lv_point_precise_t pt;
            pt.x = static_cast<lv_value_precise_t>(x0 + nx * (x1 - x0));
            pt.y = static_cast<lv_value_precise_t>(y0 - ny * (y0 - y1));
            return pt;
        };

        // Grid lines at -40 dB, -20 dB, 0 dB
        static const float kGridDbs[] = {-40.0f, -20.0f, 0.0f};
        for (float gdb : kGridDbs) {
            lv_draw_line_dsc_t gd;
            lv_draw_line_dsc_init(&gd);
            gd.color = lv_color_hex(0x161C24);
            gd.width = 1;
            // Vertical
            auto p_v1 = db_to_px(gdb, -60.0f);
            auto p_v2 = db_to_px(gdb, 0.0f);
            gd.p1 = p_v1; gd.p2 = p_v2;
            lv_draw_line(layer, &gd);
            // Horizontal
            auto p_h1 = db_to_px(-60.0f, gdb);
            auto p_h2 = db_to_px(0.0f, gdb);
            gd.p1 = p_h1; gd.p2 = p_h2;
            lv_draw_line(layer, &gd);
        }

        // 1:1 Unity reference line (faint blue)
        lv_draw_line_dsc_t ref_dsc;
        lv_draw_line_dsc_init(&ref_dsc);
        ref_dsc.color = lv_color_hex(0x232E3A);
        ref_dsc.width = 1;
        ref_dsc.p1 = db_to_px(-60.0f, -60.0f);
        ref_dsc.p2 = db_to_px(0.0f, 0.0f);
        lv_draw_line(layer, &ref_dsc);

        // VCA Non-linear Dynamic Transfer Curve
        constexpr int kSteps = 30;
        lv_point_precise_t prev_pt = db_to_px(-60.0f, calculate_comp_out(-60.0f, thresh, ratio, knee, makeup));
        for (int i = 1; i <= kSteps; ++i) {
            float in_d = -60.0f + (static_cast<float>(i) / kSteps) * 60.0f;
            float out_d = calculate_comp_out(in_d, thresh, ratio, knee, makeup);
            lv_point_precise_t curr_pt = db_to_px(in_d, out_d);

            lv_draw_line_dsc_t c_dsc;
            lv_draw_line_dsc_init(&c_dsc);
            c_dsc.color = COLOR_CYAN;
            c_dsc.width = 2;
            c_dsc.p1 = prev_pt;
            c_dsc.p2 = curr_pt;
            lv_draw_line(layer, &c_dsc);
            prev_pt = curr_pt;
        }

        // Threshold marker dashed line
        auto th_bot = db_to_px(thresh, -60.0f);
        auto th_top = db_to_px(thresh, calculate_comp_out(thresh, thresh, ratio, knee, makeup));
        lv_draw_line_dsc_t th_dsc;
        lv_draw_line_dsc_init(&th_dsc);
        th_dsc.color = COLOR_SELECTED_BORDER;
        th_dsc.width = 1;
        th_dsc.p1 = th_bot;
        th_dsc.p2 = th_top;
        lv_draw_line(layer, &th_dsc);

        // Live Operating Signal Dot
        const float in_peak = (target->format == ChannelFormat::Stereo)
            ? std::max(target->fx_in_peak_l, target->fx_in_peak_r)
            : target->fx_in_peak_l;
        if (in_peak > -59.0f) {
            float out_peak = calculate_comp_out(in_peak, thresh, ratio, knee, makeup);
            auto dot_pt = db_to_px(in_peak, out_peak);

            lv_draw_rect_dsc_t dot_dsc;
            lv_draw_rect_dsc_init(&dot_dsc);
            dot_dsc.radius = 4;
            dot_dsc.bg_color = COLOR_CYAN;
            dot_dsc.bg_opa = LV_OPA_COVER;
            dot_dsc.border_color = COLOR_PRIMARY_TEXT;
            dot_dsc.border_width = 1;
            lv_area_t dot_area = {dot_pt.x - 4, dot_pt.y - 4, dot_pt.x + 4, dot_pt.y + 4};
            lv_draw_rect(layer, &dot_dsc, &dot_area);
        }

        // Silkscreen labels
        lv_draw_label_dsc_t hdr_dsc;
        lv_draw_label_dsc_init(&hdr_dsc);
        hdr_dsc.font = &ac_font_12;
        hdr_dsc.color = lv_color_hex(0x8A96A4);
        hdr_dsc.text = "TRANSFER CURVE";
        lv_area_t ha = {coords.x1 + 8, coords.y1 + 4, coords.x1 + 180, coords.y1 + 18};
        lv_draw_label(layer, &hdr_dsc, &ha);
    }, LV_EVENT_DRAW_MAIN, v);

    // Right half: Presets Card
    auto* spec_card = lv_obj_create(telemetry_box);
    lv_obj_set_pos(spec_card, 302, 156);
    lv_obj_set_size(spec_card, 274, 180);
    lv_obj_remove_flag(spec_card, LV_OBJ_FLAG_SCROLLABLE);
    ui::rack_skin::apply_recessed_display(spec_card, lv_color_hex(0x10151B), lv_color_hex(0x2E3640));
    lv_obj_set_style_pad_all(spec_card, 6, 0);

    instrument_label(spec_card, "PRESETS", 12, 10, &ac_font_14, COLOR_CYAN);

    // 4 Curated Quick Presets
    struct ProfileDef {
        const char* name;
        float thresh;
        float ratio;
        float attack;
        float release;
        float knee;
        float makeup;
    };
    static const ProfileDef kProfiles[4] = {
        {"VOCAL",   -20.0f, 3.0f, 15.0f, 120.0f, 8.0f,  2.0f},
        {"DRUMS",   -14.0f, 4.0f, 30.0f, 80.0f,  4.0f,  3.0f},
        {"BASS",    -16.0f, 4.0f, 20.0f, 180.0f, 6.0f,  1.5f},
        {"ALL-IN",  -22.0f, 20.0f, 0.5f, 40.0f,  0.0f,  6.0f}
    };

    for (int p = 0; p < 4; ++p) {
        auto* p_btn = lv_button_create(spec_card);
        lv_obj_set_pos(p_btn, 12 + (p % 2) * 126, 44 + (p / 2) * 60);
        lv_obj_set_size(p_btn, 120, 50);
        ui::rack_skin::apply_hardware_button(p_btn, false, COLOR_CYAN, lv_color_hex(0x1B384A), lv_color_hex(0x182028));
        auto* pl = lv_label_create(p_btn);
        lv_label_set_text(pl, kProfiles[p].name);
        lv_obj_center(pl);
        lv_obj_set_style_text_font(pl, &ac_font_14, 0);
        lv_obj_set_style_text_color(pl, COLOR_CYAN, 0);

        struct ProfileCtx { FxScreenView* v; ProfileDef prof; };
        auto* pctx = new ProfileCtx{v, kProfiles[p]};
        lv_obj_add_event_cb(p_btn, [](lv_event_t* e) {
            auto* c = static_cast<ProfileCtx*>(lv_event_get_user_data(e));
            if (!c || !c->v) return;
            FxScreenView* view = c->v;
            ChannelState* target = resolve_target_channel(view);
            if (!target) return;
            if (auto* pi = target->fx_rack.get_slot(view->selected_slot)) {
                pi->set_parameter_value("thresh", c->prof.thresh);
                pi->set_parameter_value("ratio", c->prof.ratio);
                pi->set_parameter_value("attack", c->prof.attack);
                pi->set_parameter_value("release", c->prof.release);
                pi->set_parameter_value("knee", c->prof.knee);
                pi->set_parameter_value("makeup", c->prof.makeup);
                sync_channel_fx_to_dsp(target);
                view->last_param_fingerprint = compute_rack_fingerprint(target);
                schedule_ui_rebuild(view, false, true);
            }
        }, LV_EVENT_CLICKED, pctx);
        lv_obj_add_event_cb(p_btn, [](lv_event_t* e) {
            delete static_cast<ProfileCtx*>(lv_event_get_user_data(e));
        }, LV_EVENT_DELETE, pctx);
    }

    // ------------------------------------------------------------------------
    // BOTTOM: GREY BRUSHED METAL ANALOG CONTROLS (1192 x 354)
    // ------------------------------------------------------------------------
    auto* ctrl_box = create_grey_rack_module_box(
        v->editor_content, 16, 456, 1192, 354,
        "COMPRESSOR CONTROLS",
        lv_color_hex(0x8E9EAE), lv_color_hex(0x2D343C));

    // 1. DETECTION & THRESHOLD SUBMODULE (w: 236)
    auto* sub1 = create_grey_rack_module_box(
        ctrl_box, 14, 32, 236, 310, "THRESHOLD",
        lv_color_hex(0x8E9EAE), lv_color_hex(0x252B32));
    place_param_knob(sub1, inst, "thresh", "THRESHOLD", 64, 52, 88);

    // Fast threshold target buttons
    static const float kThreshPresets[4] = {-24.0f, -18.0f, -12.0f, -6.0f};
    static const char* kThreshLabels[4] = {"-24 dB", "-18 dB", "-12 dB", "-6 dB"};
    for (int i = 0; i < 4; ++i) {
        auto* tb = lv_button_create(sub1);
        lv_obj_set_pos(tb, 12 + (i % 2) * 108, 204 + (i / 2) * 32);
        lv_obj_set_size(tb, 102, 26);
        ui::rack_skin::apply_hardware_button(tb, false, COLOR_EDGE, COLOR_CONTROL, lv_color_hex(0x1B2128));
        auto* tl = lv_label_create(tb);
        lv_label_set_text(tl, kThreshLabels[i]);
        lv_obj_center(tl);
        lv_obj_set_style_text_font(tl, &ac_font_12, 0);
        lv_obj_set_style_text_color(tl, COLOR_TEXT2, 0);

        struct ThreshCtx { FxScreenView* v; float val; };
        auto* tctx = new ThreshCtx{v, kThreshPresets[i]};
        lv_obj_add_event_cb(tb, [](lv_event_t* e) {
            auto* c = static_cast<ThreshCtx*>(lv_event_get_user_data(e));
            if (!c || !c->v) return;
            ChannelState* target = resolve_target_channel(c->v);
            if (!target) return;
            if (auto* pi = target->fx_rack.get_slot(c->v->selected_slot)) {
                pi->set_parameter_value("thresh", c->val);
                sync_channel_fx_to_dsp(target);
                c->v->last_param_fingerprint = compute_rack_fingerprint(target);
                schedule_ui_rebuild(c->v, false, true);
            }
        }, LV_EVENT_CLICKED, tctx);
        lv_obj_add_event_cb(tb, [](lv_event_t* e) {
            delete static_cast<ThreshCtx*>(lv_event_get_user_data(e));
        }, LV_EVENT_DELETE, tctx);
    }

    // 2. RATIO (w: 340)
    auto* sub2 = create_grey_rack_module_box(
        ctrl_box, 260, 32, 340, 310, "RATIO",
        lv_color_hex(0x8E9EAE), lv_color_hex(0x252B32));
    place_param_knob(sub2, inst, "ratio", "RATIO", 116, 52, 88);

    // 1176-style pushbutton bank
    static const struct RatioItem { float val; const char* name; } kRatioItems[6] = {
        {2.0f,  "2 : 1"},
        {4.0f,  "4 : 1"},
        {8.0f,  "8 : 1"},
        {12.0f, "12 : 1"},
        {20.0f, "20 : 1"},
        {20.0f, "ALL IN"}
    };

    const float cur_ratio = inst->get_parameter_value("ratio", 4.0f);
    for (int r = 0; r < 6; ++r) {
        auto* rb = lv_button_create(sub2);
        lv_obj_set_pos(rb, 14 + (r % 3) * 105, 204 + (r / 3) * 32);
        lv_obj_set_size(rb, 100, 26);
        v->comp_ratio_btns[r] = rb;

        const bool is_act = (r == 5) ? (cur_ratio >= 19.9f) : (std::abs(cur_ratio - kRatioItems[r].val) < 0.2f);
        ui::rack_skin::apply_hardware_button(
            rb, is_act, COLOR_CYAN,
            is_act ? lv_color_hex(0x183848) : lv_color_hex(0x14181F),
            lv_color_hex(0x1B232A));

        auto* rl = lv_label_create(rb);
        lv_label_set_text(rl, kRatioItems[r].name);
        lv_obj_center(rl);
        lv_obj_set_style_text_font(rl, &ac_font_12, 0);
        lv_obj_set_style_text_color(rl, is_act ? COLOR_CYAN : COLOR_TEXT2, 0);

        struct RatioCtx { FxScreenView* v; float ratio; };
        auto* rctx = new RatioCtx{v, kRatioItems[r].val};
        lv_obj_add_event_cb(rb, [](lv_event_t* e) {
            auto* c = static_cast<RatioCtx*>(lv_event_get_user_data(e));
            if (!c || !c->v) return;
            ChannelState* target = resolve_target_channel(c->v);
            if (!target) return;
            if (auto* pi = target->fx_rack.get_slot(c->v->selected_slot)) {
                pi->set_parameter_value("ratio", c->ratio);
                sync_channel_fx_to_dsp(target);
                c->v->last_param_fingerprint = compute_rack_fingerprint(target);
                schedule_ui_rebuild(c->v, false, true);
            }
        }, LV_EVENT_CLICKED, rctx);
        lv_obj_add_event_cb(rb, [](lv_event_t* e) {
            delete static_cast<RatioCtx*>(lv_event_get_user_data(e));
        }, LV_EVENT_DELETE, rctx);
    }

    // 3. ENVELOPE TIME CONSTANTS (w: 290)
    auto* sub3 = create_grey_rack_module_box(
        ctrl_box, 610, 32, 290, 310, "ATTACK & RELEASE",
        lv_color_hex(0x8E9EAE), lv_color_hex(0x252B32));
    place_param_knob(sub3, inst, "attack", "ATTACK", 28, 52, 74);
    place_param_knob(sub3, inst, "release", "RELEASE", 168, 52, 74);

    // Attack Presets
    static const struct AttDef { float ms; const char* lbl; } kAtts[3] = {
        {1.0f,  "FAST 1ms"},
        {10.0f, "MED 10ms"},
        {40.0f, "SLOW 40ms"}
    };
    for (int i = 0; i < 3; ++i) {
        auto* ab = lv_button_create(sub3);
        lv_obj_set_pos(ab, 12 + i * 88, 204);
        lv_obj_set_size(ab, 84, 26);
        ui::rack_skin::apply_hardware_button(ab, false, COLOR_EDGE, COLOR_CONTROL, lv_color_hex(0x1B2128));
        auto* al = lv_label_create(ab);
        lv_label_set_text(al, kAtts[i].lbl);
        lv_obj_center(al);
        lv_obj_set_style_text_font(al, &ac_font_12, 0);
        lv_obj_set_style_text_color(al, COLOR_TEXT2, 0);

        struct AttCtx { FxScreenView* v; float ms; };
        auto* actx = new AttCtx{v, kAtts[i].ms};
        lv_obj_add_event_cb(ab, [](lv_event_t* e) {
            auto* c = static_cast<AttCtx*>(lv_event_get_user_data(e));
            if (!c || !c->v) return;
            ChannelState* target = resolve_target_channel(c->v);
            if (!target) return;
            if (auto* pi = target->fx_rack.get_slot(c->v->selected_slot)) {
                pi->set_parameter_value("attack", c->ms);
                sync_channel_fx_to_dsp(target);
                c->v->last_param_fingerprint = compute_rack_fingerprint(target);
                schedule_ui_rebuild(c->v, false, true);
            }
        }, LV_EVENT_CLICKED, actx);
        lv_obj_add_event_cb(ab, [](lv_event_t* e) {
            delete static_cast<AttCtx*>(lv_event_get_user_data(e));
        }, LV_EVENT_DELETE, actx);
    }

    // Release Presets
    static const struct RelDef { float ms; const char* lbl; } kRels[3] = {
        {50.0f,  "FAST 50ms"},
        {150.0f, "VOCAL 150"},
        {400.0f, "AUTO 400"}
    };
    for (int i = 0; i < 3; ++i) {
        auto* rb = lv_button_create(sub3);
        lv_obj_set_pos(rb, 12 + i * 88, 236);
        lv_obj_set_size(rb, 84, 26);
        ui::rack_skin::apply_hardware_button(rb, false, COLOR_EDGE, COLOR_CONTROL, lv_color_hex(0x1B2128));
        auto* rl = lv_label_create(rb);
        lv_label_set_text(rl, kRels[i].lbl);
        lv_obj_center(rl);
        lv_obj_set_style_text_font(rl, &ac_font_12, 0);
        lv_obj_set_style_text_color(rl, COLOR_TEXT2, 0);

        struct RelCtx { FxScreenView* v; float ms; };
        auto* rctx = new RelCtx{v, kRels[i].ms};
        lv_obj_add_event_cb(rb, [](lv_event_t* e) {
            auto* c = static_cast<RelCtx*>(lv_event_get_user_data(e));
            if (!c || !c->v) return;
            ChannelState* target = resolve_target_channel(c->v);
            if (!target) return;
            if (auto* pi = target->fx_rack.get_slot(c->v->selected_slot)) {
                pi->set_parameter_value("release", c->ms);
                sync_channel_fx_to_dsp(target);
                c->v->last_param_fingerprint = compute_rack_fingerprint(target);
                schedule_ui_rebuild(c->v, false, true);
            }
        }, LV_EVENT_CLICKED, rctx);
        lv_obj_add_event_cb(rb, [](lv_event_t* e) {
            delete static_cast<RelCtx*>(lv_event_get_user_data(e));
        }, LV_EVENT_DELETE, rctx);
    }

    // 4. KNEE & OUTPUT GAIN (w: 268)
    auto* sub4 = create_grey_rack_module_box(
        ctrl_box, 910, 32, 268, 310, "KNEE & MAKEUP",
        lv_color_hex(0x8E9EAE), lv_color_hex(0x252B32));
    place_param_knob(sub4, inst, "knee", "KNEE", 22, 52, 74);
    place_param_knob(sub4, inst, "makeup", "MAKEUP", 152, 52, 74);

    // Knee Hard / Soft buttons
    auto* hard_btn = lv_button_create(sub4);
    lv_obj_set_pos(hard_btn, 12, 204);
    lv_obj_set_size(hard_btn, 118, 26);
    ui::rack_skin::apply_hardware_button(hard_btn, false, COLOR_EDGE, COLOR_CONTROL, lv_color_hex(0x1B2128));
    auto* hl = lv_label_create(hard_btn);
    lv_label_set_text(hl, "HARD (0 dB)");
    lv_obj_center(hl);
    lv_obj_set_style_text_font(hl, &ac_font_12, 0);
    lv_obj_set_style_text_color(hl, COLOR_TEXT2, 0);
    lv_obj_add_event_cb(hard_btn, [](lv_event_t* e) {
        auto* view = static_cast<FxScreenView*>(lv_event_get_user_data(e));
        if (!view) return;
        ChannelState* target = resolve_target_channel(view);
        if (!target) return;
        if (auto* pi = target->fx_rack.get_slot(view->selected_slot)) {
            pi->set_parameter_value("knee", 0.0f);
            sync_channel_fx_to_dsp(target);
            view->last_param_fingerprint = compute_rack_fingerprint(target);
            schedule_ui_rebuild(view, false, true);
        }
    }, LV_EVENT_CLICKED, v);

    auto* soft_btn = lv_button_create(sub4);
    lv_obj_set_pos(soft_btn, 138, 204);
    lv_obj_set_size(soft_btn, 118, 26);
    ui::rack_skin::apply_hardware_button(soft_btn, false, COLOR_EDGE, COLOR_CONTROL, lv_color_hex(0x1B2128));
    auto* sl = lv_label_create(soft_btn);
    lv_label_set_text(sl, "SOFT (12 dB)");
    lv_obj_center(sl);
    lv_obj_set_style_text_font(sl, &ac_font_12, 0);
    lv_obj_set_style_text_color(sl, COLOR_TEXT2, 0);
    lv_obj_add_event_cb(soft_btn, [](lv_event_t* e) {
        auto* view = static_cast<FxScreenView*>(lv_event_get_user_data(e));
        if (!view) return;
        ChannelState* target = resolve_target_channel(view);
        if (!target) return;
        if (auto* pi = target->fx_rack.get_slot(view->selected_slot)) {
            pi->set_parameter_value("knee", 12.0f);
            sync_channel_fx_to_dsp(target);
            view->last_param_fingerprint = compute_rack_fingerprint(target);
            schedule_ui_rebuild(view, false, true);
        }
    }, LV_EVENT_CLICKED, v);

    // Reset Makeup button
    auto* rst_btn = lv_button_create(sub4);
    lv_obj_set_pos(rst_btn, 12, 236);
    lv_obj_set_size(rst_btn, 244, 26);
    ui::rack_skin::apply_hardware_button(rst_btn, false, COLOR_EDGE, COLOR_CONTROL, lv_color_hex(0x1B2128));
    auto* rl = lv_label_create(rst_btn);
    lv_label_set_text(rl, "RESET MAKEUP TO 0 dB");
    lv_obj_center(rl);
    lv_obj_set_style_text_font(rl, &ac_font_12, 0);
    lv_obj_set_style_text_color(rl, COLOR_TEXT2, 0);
    lv_obj_add_event_cb(rst_btn, [](lv_event_t* e) {
        auto* view = static_cast<FxScreenView*>(lv_event_get_user_data(e));
        if (!view) return;
        ChannelState* target = resolve_target_channel(view);
        if (!target) return;
        if (auto* pi = target->fx_rack.get_slot(view->selected_slot)) {
            pi->set_parameter_value("makeup", 0.0f);
            sync_channel_fx_to_dsp(target);
            view->last_param_fingerprint = compute_rack_fingerprint(target);
            schedule_ui_rebuild(view, false, true);
        }
    }, LV_EVENT_CLICKED, v);
}

// ============================================================================
// 6. EXPANDER / GATE RACK EDITOR (Hard-Threshold Downward Gate Workspace)
// ============================================================================

// Gate DSP state -> hardware indicator color (curve marker, monitor LED & timeline)
static lv_color_t gate_ui_state_color(uint8_t state) {
    switch (state) {
        case 1:  return COLOR_YELLOW;              // Attack / opening toward unity
        case 2:  return COLOR_GREEN;               // Open (unity pass-through)
        case 3:  return lv_color_hex(0x8A9BB0);    // Hold (hold timer running)
        case 4:  return lv_color_hex(0xE08030);    // Release / attenuating to range
        case 0:
        default: return COLOR_ORANGE;              // Closed at the range floor
    }
}

// Dashed silkscreen line used for reference markers (threshold / unity / live cursors)
static void draw_gate_dashed_line(lv_layer_t* layer, lv_point_precise_t p1, lv_point_precise_t p2,
                                  lv_color_t color, int width, int dash_px, int gap_px) {
    const float x1 = static_cast<float>(p1.x);
    const float y1 = static_cast<float>(p1.y);
    const float dx = static_cast<float>(p2.x) - x1;
    const float dy = static_cast<float>(p2.y) - y1;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len <= 1.0f) return;
    const float ux = dx / len;
    const float uy = dy / len;
    for (float t = 0.0f; t < len; t += static_cast<float>(dash_px + gap_px)) {
        const float t2 = std::min(t + static_cast<float>(dash_px), len);
        lv_draw_line_dsc_t d;
        lv_draw_line_dsc_init(&d);
        d.color = color;
        d.width = width;
        d.p1 = {static_cast<lv_value_precise_t>(x1 + ux * t), static_cast<lv_value_precise_t>(y1 + uy * t)};
        d.p2 = {static_cast<lv_value_precise_t>(x1 + ux * t2), static_cast<lv_value_precise_t>(y1 + uy * t2)};
        lv_draw_line(layer, &d);
    }
}

void build_gate_rack_editor(FxScreenView* v, ChannelState* ch, PluginInstance* inst) {
    // ------------------------------------------------------------------------
    // TOP: PRE-FX / POST-FX SIGNAL FLOW TELEMETRY (shared rack I/O strip)
    // ------------------------------------------------------------------------
    auto* meter_box = create_rack_module_box(
        v->editor_content, 16, 94, 1192, 146,
        "PROCESSOR SIGNAL FLOW & LIVE I/O LEVEL TELEMETRY",
        COLOR_CYAN, lv_color_hex(0x161D24));
    build_io_meter_strip(v, meter_box, 16, 30, 620);

    char mode_txt[180];
    std::snprintf(mode_txt, sizeof(mode_txt),
                  "TARGET: %s (%s)\nPROCESSOR: %s\nSTATUS: %s",
                  ch->name,
                  ch->format == ChannelFormat::Stereo ? "2-CH STEREO" : "1-CH MONO",
                  inst->get_name().c_str(),
                  inst->is_bypassed() ? "BYPASSED (UNITY PASSTHROUGH)" : "ACTIVE REALTIME DSP");
    instrument_label(meter_box, mode_txt, 670, 44, &ac_font_14, COLOR_CYAN);

    // ------------------------------------------------------------------------
    // LEFT: HARD-THRESHOLD GATE TRANSFER CURVE (straight plot of the engine law)
    // ------------------------------------------------------------------------
    auto* curve_box = create_grey_rack_module_box(
        v->editor_content, 16, 252, 660, 554,
        "GATE / EXPANDER TRANSFER CURVE",
        lv_color_hex(0x9EAEBD), lv_color_hex(0x2D343C));

    auto* curve_canvas = lv_obj_create(curve_box);
    lv_obj_set_pos(curve_canvas, 10, 30);
    lv_obj_set_size(curve_canvas, 640, 460);
    lv_obj_remove_flag(curve_canvas, LV_OBJ_FLAG_SCROLLABLE);
    ui::rack_skin::apply_recessed_display(curve_canvas, lv_color_hex(0x080B0E), lv_color_hex(0x2E3640));
    lv_obj_set_style_pad_all(curve_canvas, 0, 0);
    v->gate_curve_canvas = curve_canvas;

    lv_obj_add_event_cb(curve_canvas, [](lv_event_t* e) {
        auto* view = static_cast<FxScreenView*>(lv_event_get_user_data(e));
        if (!view) return;
        ChannelState* target = resolve_target_channel(view);
        if (!target) return;
        auto* inst_ptr = target->fx_rack.get_slot(view->selected_slot);
        const float thresh = inst_ptr ? inst_ptr->get_parameter_value("thresh", -48.0f) : -48.0f;
        const float range  = inst_ptr ? inst_ptr->get_parameter_value("range", -60.0f) : -60.0f;

        lv_obj_t* obj = lv_event_get_target_obj(e);
        lv_layer_t* layer = lv_event_get_layer(e);
        lv_area_t coords;
        lv_obj_get_coords(obj, &coords);

        // Exact static law of audiocore::GateNode::computeStaticOutputDb():
        // hard threshold (no knee, no hysteresis): out = in + range below the threshold,
        // unity above it, with the engine's hard -90 dB output floor.
        auto gate_static_out = [&](float in_db) -> float {
            if (in_db < thresh) return std::max(-90.0f, in_db + range);
            return in_db;
        };

        const int px0 = coords.x1 + 46;   // -80 dBFS input
        const int px1 = coords.x2 - 20;   //   0 dBFS input
        const int py0 = coords.y1 + 40;   //   0 dBFS output
        const int py1 = coords.y2 - 44;   // -90 dBFS output
        auto pt_of = [&](float in_db, float out_db) -> lv_point_precise_t {
            const float nx = std::clamp((in_db + 80.0f) / 80.0f, 0.0f, 1.0f);
            const float ny = std::clamp((out_db + 90.0f) / 90.0f, 0.0f, 1.0f);
            lv_point_precise_t pt;
            pt.x = static_cast<lv_value_precise_t>(px0 + nx * (px1 - px0));
            pt.y = static_cast<lv_value_precise_t>(py1 - ny * (py1 - py0));
            return pt;
        };

        // 1. Calibrated grid & axis scales (-80..0 dBFS in / -90..0 dBFS out)
        static const float kInGrid[] = {-80.0f, -60.0f, -40.0f, -20.0f, 0.0f};
        static const char* kInGridLbl[] = {"-80", "-60", "-40", "-20", "0"};
        for (int gi = 0; gi < 5; ++gi) {
            const int gx = px0 + static_cast<int>((kInGrid[gi] + 80.0f) / 80.0f * (px1 - px0));
            lv_draw_line_dsc_t gd;
            lv_draw_line_dsc_init(&gd);
            gd.color = lv_color_hex(0x18202A);
            gd.width = 1;
            gd.p1 = {static_cast<lv_value_precise_t>(gx), static_cast<lv_value_precise_t>(py0)};
            gd.p2 = {static_cast<lv_value_precise_t>(gx), static_cast<lv_value_precise_t>(py1)};
            lv_draw_line(layer, &gd);

            lv_draw_label_dsc_t ld;
            lv_draw_label_dsc_init(&ld);
            ld.font = &ac_font_12;
            ld.color = lv_color_hex(0x7C8794);
            ld.text = kInGridLbl[gi];
            ld.align = LV_TEXT_ALIGN_CENTER;
            lv_area_t la = {gx - 18, py1 + 8, gx + 18, py1 + 22};
            lv_draw_label(layer, &ld, &la);
        }

        static const float kOutGrid[] = {-90.0f, -60.0f, -30.0f, 0.0f};
        static const char* kOutGridLbl[] = {"-90", "-60", "-30", "0"};
        for (int gi = 0; gi < 4; ++gi) {
            const int gy = py1 - static_cast<int>((kOutGrid[gi] + 90.0f) / 90.0f * (py1 - py0));
            lv_draw_line_dsc_t gd;
            lv_draw_line_dsc_init(&gd);
            gd.color = lv_color_hex(0x18202A);
            gd.width = 1;
            gd.p1 = {static_cast<lv_value_precise_t>(px0), static_cast<lv_value_precise_t>(gy)};
            gd.p2 = {static_cast<lv_value_precise_t>(px1), static_cast<lv_value_precise_t>(gy)};
            lv_draw_line(layer, &gd);

            lv_draw_label_dsc_t ld;
            lv_draw_label_dsc_init(&ld);
            ld.font = &ac_font_12;
            ld.color = lv_color_hex(0x7C8794);
            ld.text = kOutGridLbl[gi];
            ld.align = LV_TEXT_ALIGN_RIGHT;
            lv_area_t la = {coords.x1 + 2, gy - 7, px0 - 6, gy + 7};
            lv_draw_label(layer, &ld, &la);
        }

        // 2. 1:1 unity reference (dashed) across the full input span
        draw_gate_dashed_line(layer, pt_of(-80.0f, -80.0f), pt_of(0.0f, 0.0f),
                              lv_color_hex(0x3A4856), 1, 6, 5);

        // 3. Threshold reference (vertical dashed) + engraved set-point label
        {
            lv_point_precise_t t_top = pt_of(thresh, 0.0f);
            lv_point_precise_t t_bot = pt_of(thresh, -90.0f);
            draw_gate_dashed_line(layer, t_top, t_bot, lv_color_hex(0xE08030), 1, 5, 4);

            char thr_txt[32];
            std::snprintf(thr_txt, sizeof(thr_txt), "THR %+.1f dB", thresh);
            lv_draw_label_dsc_t ld;
            lv_draw_label_dsc_init(&ld);
            ld.font = &ac_font_12;
            ld.color = lv_color_hex(0xE0A030);
            ld.text = thr_txt;
            const int thr_x = static_cast<int>(t_top.x);
            const bool right_side = (px1 - thr_x) > 90;
            lv_area_t la = right_side
                ? lv_area_t{thr_x + 5, py0 + 2, thr_x + 5 + 110, py0 + 16}
                : lv_area_t{thr_x - 115, py0 + 2, thr_x - 5, py0 + 16};
            lv_draw_label(layer, &ld, &la);
        }

        // 4. Range attenuation marker: vertical drop of the closed segment at the threshold
        {
            const float out_at_thresh = gate_static_out(thresh - 0.001f);
            lv_point_precise_t d_top = pt_of(thresh, thresh);
            lv_point_precise_t d_bot = pt_of(thresh, out_at_thresh);
            const int drop_px = static_cast<int>(d_bot.y - d_top.y);
            if (drop_px >= 6) {
                lv_draw_line_dsc_t dl;
                lv_draw_line_dsc_init(&dl);
                dl.color = lv_color_hex(0xE0A030);
                dl.width = 2;
                dl.p1 = d_top;
                dl.p2 = d_bot;
                lv_draw_line(layer, &dl);

                char rng_txt[40];
                std::snprintf(rng_txt, sizeof(rng_txt), "RANGE %.0f dB", range);
                lv_draw_label_dsc_t ld;
                lv_draw_label_dsc_init(&ld);
                ld.font = &ac_font_12;
                ld.color = lv_color_hex(0xC08A50);
                ld.text = rng_txt;
                const int mid_y = static_cast<int>((d_top.y + d_bot.y) * 0.5f);
                lv_area_t la = {static_cast<int>(d_top.x) + 5, mid_y - 7,
                                static_cast<int>(d_top.x) + 5 + 95, mid_y + 7};
                lv_draw_label(layer, &ld, &la);
            }
        }

        // 5. Gate transfer curve: exact engine law sampled across -80..0 dBFS
        {
            constexpr int kSteps = 96;
            const float step_db = 80.0f / kSteps;
            lv_point_precise_t prev = pt_of(-80.0f, gate_static_out(-80.0f));
            for (int i = 1; i <= kSteps; ++i) {
                const float in_db = -80.0f + static_cast<float>(i) * step_db;
                const float in_mid = in_db - step_db * 0.5f;
                lv_point_precise_t curr = pt_of(in_db, gate_static_out(in_db));
                const lv_color_t seg_col = (in_mid < thresh)
                    ? lv_color_hex(0xE0A030)   // attenuated below threshold
                    : COLOR_GREEN;             // unity pass region

                // Soft phosphor glow pass
                lv_draw_line_dsc_t glow;
                lv_draw_line_dsc_init(&glow);
                glow.color = seg_col;
                glow.width = 6;
                glow.opa = LV_OPA_30;
                glow.p1 = prev;
                glow.p2 = curr;
                lv_draw_line(layer, &glow);

                // Sharp trace pass
                lv_draw_line_dsc_t seg;
                lv_draw_line_dsc_init(&seg);
                seg.color = seg_col;
                seg.width = 2;
                seg.p1 = prev;
                seg.p2 = curr;
                lv_draw_line(layer, &seg);
                prev = curr;
            }
        }

        // 6. Live operating point from real per-slot engine telemetry:
        //    marker sits at (measured detector dB, detector + measured gain reduction)
        const int slot_idx = std::clamp(view->selected_slot, 0, 7);
        const uint8_t gate_state = target->fx_slot_gate_state[static_cast<size_t>(slot_idx)];
        if (gate_state != GATE_STATE_UNAVAILABLE) {
            const float det_db = std::clamp(target->fx_slot_gate_detector_db[static_cast<size_t>(slot_idx)], -90.0f, 0.0f);
            const float gr_db = std::min(0.0f, target->fx_slot_gate_gr_db[static_cast<size_t>(slot_idx)]);
            const lv_point_precise_t dot = pt_of(det_db, std::clamp(det_db + gr_db, -90.0f, 0.0f));
            const lv_color_t dot_col = gate_ui_state_color(gate_state);

            // Faint measurement cursor guides to both axes
            draw_gate_dashed_line(layer, dot, {dot.x, static_cast<lv_value_precise_t>(py1)}, lv_color_hex(0x2A3542), 1, 3, 5);
            draw_gate_dashed_line(layer, dot, {static_cast<lv_value_precise_t>(px0), dot.y}, lv_color_hex(0x2A3542), 1, 3, 5);

            lv_draw_rect_dsc_t ring;
            lv_draw_rect_dsc_init(&ring);
            ring.radius = LV_RADIUS_CIRCLE;
            ring.bg_color = dot_col;
            ring.bg_opa = LV_OPA_30;
            ring.border_color = dot_col;
            ring.border_width = 2;
            lv_area_t ring_a = {static_cast<int>(dot.x) - 8, static_cast<int>(dot.y) - 8,
                                static_cast<int>(dot.x) + 8, static_cast<int>(dot.y) + 8};
            lv_draw_rect(layer, &ring, &ring_a);

            lv_draw_rect_dsc_t core;
            lv_draw_rect_dsc_init(&core);
            core.radius = LV_RADIUS_CIRCLE;
            core.bg_color = dot_col;
            core.bg_opa = LV_OPA_COVER;
            core.border_color = lv_color_hex(0xF2F5F8);
            core.border_width = 1;
            lv_area_t core_a = {static_cast<int>(dot.x) - 4, static_cast<int>(dot.y) - 4,
                                static_cast<int>(dot.x) + 4, static_cast<int>(dot.y) + 4};
            lv_draw_rect(layer, &core, &core_a);
        }

        // 7. Silkscreen legend & axis title
        {
            auto draw_legend_item = [&](int& cursor_x, lv_color_t col, const char* text, bool dashed) {
                const int text_w = static_cast<int>(std::strlen(text)) * 7;
                const int item_w = 16 + 5 + text_w + 12;
                const int sw_x = cursor_x - item_w;
                const int sw_y = coords.y1 + 15;
                if (dashed) {
                    draw_gate_dashed_line(layer, {(lv_value_precise_t)sw_x, (lv_value_precise_t)sw_y},
                                          {(lv_value_precise_t)(sw_x + 16), (lv_value_precise_t)sw_y}, col, 2, 4, 3);
                } else {
                    lv_draw_line_dsc_t sw;
                    lv_draw_line_dsc_init(&sw);
                    sw.color = col;
                    sw.width = 3;
                    sw.p1 = {(lv_value_precise_t)sw_x, (lv_value_precise_t)sw_y};
                    sw.p2 = {(lv_value_precise_t)(sw_x + 16), (lv_value_precise_t)sw_y};
                    lv_draw_line(layer, &sw);
                }
                lv_draw_label_dsc_t ld;
                lv_draw_label_dsc_init(&ld);
                ld.font = &ac_font_12;
                ld.color = lv_color_hex(0x9AA6B2);
                ld.text = text;
                lv_area_t la = {sw_x + 21, coords.y1 + 8, sw_x + 21 + text_w + 8, coords.y1 + 22};
                lv_draw_label(layer, &ld, &la);
                cursor_x = sw_x - 8;
            };

            int lx = px1;
            draw_legend_item(lx, lv_color_hex(0x3A4856), "1:1 UNITY", true);
            draw_legend_item(lx, lv_color_hex(0xE08030), "THRESHOLD", true);
            draw_legend_item(lx, lv_color_hex(0xE0A030), "ATTEN (<THR)", false);
            draw_legend_item(lx, COLOR_GREEN, "PASS (>= THR)", false);

            lv_draw_label_dsc_t ax;
            lv_draw_label_dsc_init(&ax);
            ax.font = &ac_font_12;
            ax.color = lv_color_hex(0x7C8794);
            ax.text = "INPUT LEVEL dBFS";
            ax.align = LV_TEXT_ALIGN_CENTER;
            lv_area_t aa = {(px0 + px1) / 2 - 70, py1 + 24, (px0 + px1) / 2 + 70, py1 + 38};
            lv_draw_label(layer, &ax, &aa);
        }
    }, LV_EVENT_DRAW_MAIN, v);

    // Static parameter digest + engine-law caption under the curve
    v->gate_curve_lbl = instrument_label(curve_box, "", 14, 498, &ac_font_12, COLOR_TEXT2);
    instrument_label(curve_box,
        "CURVE = ENGINE LAW   |   DOT = LIVE OPERATING POINT (DETECTOR + GR)",
        14, 522, &ac_font_12, lv_color_hex(0x8A96A4));

    // ------------------------------------------------------------------------
    // RIGHT: FIVE-CONTROL BANK + LIVE GATE MONITOR (real engine measurements)
    // ------------------------------------------------------------------------
    auto* right_box = create_grey_rack_module_box(
        v->editor_content, 692, 252, 516, 554,
        "GATE / EXPANDER CONTROLS",
        lv_color_hex(0x8E9EAE), lv_color_hex(0x2D343C));

    // Only the five parameters the engine genuinely supports (no sidechain filter,
    // no hysteresis - the GateNode DSP implements neither).
    place_param_knob(right_box, inst, "thresh", "THRESHOLD", 13, 40, 78);
    place_param_knob(right_box, inst, "range", "RANGE", 111, 40, 78);
    place_param_knob(right_box, inst, "attack", "ATTACK", 209, 40, 78);
    place_param_knob(right_box, inst, "hold", "HOLD", 307, 40, 78);
    place_param_knob(right_box, inst, "release", "RELEASE", 405, 40, 78);

    auto* monitor_box = create_grey_rack_module_box(
        right_box, 10, 186, 496, 358,
        "GATE MONITOR - LIVE ENGINE MEASUREMENTS",
        lv_color_hex(0x9EAEBD), lv_color_hex(0x262C33));
    v->gate_state_led = ui::rack_skin::create_bezel_led(monitor_box, 458, 7, COLOR_ORANGE, false);

    auto* monitor_canvas = lv_obj_create(monitor_box);
    lv_obj_set_pos(monitor_canvas, 12, 30);
    lv_obj_set_size(monitor_canvas, 472, 262);
    lv_obj_remove_flag(monitor_canvas, LV_OBJ_FLAG_SCROLLABLE);
    ui::rack_skin::apply_recessed_display(monitor_canvas, lv_color_hex(0x080B0E), lv_color_hex(0x2E3640));
    lv_obj_set_style_pad_all(monitor_canvas, 0, 0);
    v->gate_monitor_canvas = monitor_canvas;

    lv_obj_add_event_cb(monitor_canvas, [](lv_event_t* e) {
        auto* view = static_cast<FxScreenView*>(lv_event_get_user_data(e));
        if (!view) return;
        ChannelState* target = resolve_target_channel(view);
        if (!target) return;
        auto* inst_ptr = target->fx_rack.get_slot(view->selected_slot);
        const float thresh = inst_ptr ? inst_ptr->get_parameter_value("thresh", -48.0f) : -48.0f;

        lv_obj_t* obj = lv_event_get_target_obj(e);
        lv_layer_t* layer = lv_event_get_layer(e);
        lv_area_t coords;
        lv_obj_get_coords(obj, &coords);

        const int gx0 = coords.x1 + 38;   // -90 dBFS / sample 0
        const int gx1 = coords.x2 - 10;   //   0 dBFS / newest sample
        const int gy0 = coords.y1 + 26;   //   0 dBFS
        const int gy1 = coords.y2 - 74;   // -90 dBFS (timeline strip sits below)
        const int gfx = gx1 - gx0;
        const int gfy = gy1 - gy0;
        const int strip_step = std::max(1, gfx / kGateHistoryPoints);

        auto pt_at = [&](int chrono_idx, float db) -> lv_point_precise_t {
            const float t = static_cast<float>(chrono_idx) / static_cast<float>(kGateHistoryPoints - 1);
            const float n = std::clamp((db + 90.0f) / 90.0f, 0.0f, 1.0f);
            lv_point_precise_t pt;
            pt.x = static_cast<lv_value_precise_t>(gx0 + t * gfx);
            pt.y = static_cast<lv_value_precise_t>(gy1 - n * gfy);
            return pt;
        };

        // 1. Grid + dB scale (-90..0 dBFS)
        static const float kDbGrid[] = {0.0f, -30.0f, -60.0f, -90.0f};
        static const char* kDbGridLbl[] = {"0", "-30", "-60", "-90"};
        for (int gi = 0; gi < 4; ++gi) {
            const int gy = gy1 - static_cast<int>((kDbGrid[gi] + 90.0f) / 90.0f * gfy);
            lv_draw_line_dsc_t gd;
            lv_draw_line_dsc_init(&gd);
            gd.color = lv_color_hex(0x18202A);
            gd.width = 1;
            gd.p1 = {static_cast<lv_value_precise_t>(gx0), static_cast<lv_value_precise_t>(gy)};
            gd.p2 = {static_cast<lv_value_precise_t>(gx1), static_cast<lv_value_precise_t>(gy)};
            lv_draw_line(layer, &gd);

            lv_draw_label_dsc_t ld;
            lv_draw_label_dsc_init(&ld);
            ld.font = &ac_font_12;
            ld.color = lv_color_hex(0x7C8794);
            ld.text = kDbGridLbl[gi];
            ld.align = LV_TEXT_ALIGN_RIGHT;
            lv_area_t la = {coords.x1 + 2, gy - 7, gx0 - 6, gy + 7};
            lv_draw_label(layer, &ld, &la);
        }

        // 2. Threshold reference (parameter set-point, drawn even before telemetry arrives)
        {
            const int ty = gy1 - static_cast<int>((std::clamp(thresh, -90.0f, 0.0f) + 90.0f) / 90.0f * gfy);
            draw_gate_dashed_line(layer, {(lv_value_precise_t)gx0, (lv_value_precise_t)ty},
                                  {(lv_value_precise_t)gx1, (lv_value_precise_t)ty}, lv_color_hex(0xE08030), 1, 5, 4);
            const bool measured_now =
                (target->fx_slot_gate_state[static_cast<size_t>(std::clamp(view->selected_slot, 0, 7))]
                 != GATE_STATE_UNAVAILABLE);
            if (measured_now) {
                lv_draw_label_dsc_t ld;
                lv_draw_label_dsc_init(&ld);
                ld.font = &ac_font_12;
                ld.color = lv_color_hex(0xE0A030);
                ld.text = "THRESHOLD";
                ld.align = LV_TEXT_ALIGN_RIGHT;
                lv_area_t la = {gx1 - 84, ty - 16, gx1 - 4, ty - 2};
                lv_draw_label(layer, &ld, &la);
            }
        }

        // 3. Live traces & state timeline - rendered strictly from measured telemetry
        const bool measured = (target->fx_slot_gate_state[static_cast<size_t>(std::clamp(view->selected_slot, 0, 7))]
                               != GATE_STATE_UNAVAILABLE);
        if (measured && view->gate_hist_count > 0) {
            const int first_chrono = kGateHistoryPoints - view->gate_hist_count;

            // Detector + output history polylines (newest sample pinned to the right edge)
            for (int i = 1; i < view->gate_hist_count; ++i) {
                const int idx_a = (view->gate_hist_head - view->gate_hist_count + i - 1 + kGateHistoryPoints * 2) % kGateHistoryPoints;
                const int idx_b = (idx_a + 1) % kGateHistoryPoints;

                lv_draw_line_dsc_t det_ln;
                lv_draw_line_dsc_init(&det_ln);
                det_ln.color = COLOR_CYAN;
                det_ln.width = 2;
                det_ln.p1 = pt_at(first_chrono + i - 1, view->gate_hist_det_db[idx_a]);
                det_ln.p2 = pt_at(first_chrono + i, view->gate_hist_det_db[idx_b]);
                lv_draw_line(layer, &det_ln);

                lv_draw_line_dsc_t out_ln;
                lv_draw_line_dsc_init(&out_ln);
                out_ln.color = COLOR_GREEN;
                out_ln.width = 1;
                out_ln.p1 = pt_at(first_chrono + i - 1, view->gate_hist_out_db[idx_a]);
                out_ln.p2 = pt_at(first_chrono + i, view->gate_hist_out_db[idx_b]);
                lv_draw_line(layer, &out_ln);
            }

            // State timeline strip (one hardware cell per 30 ms measurement tick)
            const int ty0 = gy1 + 8;
            const int ty1 = gy1 + 20;
            for (int i = 0; i < view->gate_hist_count; ++i) {
                const int idx = (view->gate_hist_head - view->gate_hist_count + i + kGateHistoryPoints * 2) % kGateHistoryPoints;
                const int cx = gx0 + (first_chrono + i) * strip_step;
                lv_draw_rect_dsc_t cell;
                lv_draw_rect_dsc_init(&cell);
                cell.radius = 1;
                cell.bg_color = gate_ui_state_color(view->gate_hist_state[idx]);
                cell.bg_opa = LV_OPA_80;
                lv_area_t ca = {cx, ty0, cx + strip_step - 2, ty1};
                lv_draw_rect(layer, &cell, &ca);
            }

            // State color legend under the timeline
            static const char* kStateNames[5] = {"CLOSED", "ATTACK", "OPEN", "HOLD", "RELEASE"};
            int sx = gx0;
            for (int st = 0; st < 5; ++st) {
                lv_draw_label_dsc_t ld;
                lv_draw_label_dsc_init(&ld);
                ld.font = &ac_font_12;
                ld.color = gate_ui_state_color(static_cast<uint8_t>(st));
                ld.text = kStateNames[st];
                const int tw = static_cast<int>(std::strlen(kStateNames[st])) * 7;
                lv_area_t la = {sx, ty1 + 6, sx + tw + 8, ty1 + 20};
                lv_draw_label(layer, &ld, &la);
                sx += tw + 18;
            }

            lv_draw_label_dsc_t cap;
            lv_draw_label_dsc_init(&cap);
            cap.font = &ac_font_12;
            cap.color = lv_color_hex(0x8A96A4);
            cap.text = "DETECTOR (CYAN) + POST-FX OUTPUT (GREEN)  |  WINDOW ~2.9 s";
            lv_area_t ca = {coords.x1 + 8, coords.y1 + 5, coords.x1 + 470, coords.y1 + 19};
            lv_draw_label(layer, &cap, &ca);
        } else {
            // Explicit unavailable state: the engine has not produced any measurement yet
            lv_draw_rect_dsc_t backing;
            lv_draw_rect_dsc_init(&backing);
            backing.radius = 6;
            backing.bg_color = lv_color_hex(0x0C1016);
            backing.bg_opa = LV_OPA_90;
            backing.border_color = lv_color_hex(0x2A323D);
            backing.border_width = 1;
            const int cy = (gy0 + gy1) / 2;
            lv_area_t ba = {gx0 + 12, cy - 44, gx1 - 12, cy + 48};
            lv_draw_rect(layer, &backing, &ba);

            lv_draw_label_dsc_t m1;
            lv_draw_label_dsc_init(&m1);
            m1.font = &ac_font_14;
            m1.color = COLOR_ORANGE;
            m1.text = "NO LIVE GATE TELEMETRY";
            m1.align = LV_TEXT_ALIGN_CENTER;
            lv_area_t a1 = {gx0, cy - 34, gx1, cy - 18};
            lv_draw_label(layer, &m1, &a1);

            lv_draw_label_dsc_t m2;
            lv_draw_label_dsc_init(&m2);
            m2.font = &ac_font_12;
            m2.color = lv_color_hex(0x9AA6B2);
            m2.text = "START REAL AUDIO OR THE SIMULATION ENGINE TO MEASURE";
            m2.align = LV_TEXT_ALIGN_CENTER;
            lv_area_t a2 = {gx0, cy - 8, gx1, cy + 6};
            lv_draw_label(layer, &m2, &a2);

            lv_draw_label_dsc_t m3;
            lv_draw_label_dsc_init(&m3);
            m3.font = &ac_font_12;
            m3.color = lv_color_hex(0x9AA6B2);
            m3.text = "DETECTOR LEVEL, STATE AND GAIN REDUCTION VALUES";
            m3.align = LV_TEXT_ALIGN_CENTER;
            lv_area_t a3 = {gx0, cy + 12, gx1, cy + 26};
            lv_draw_label(layer, &m3, &a3);
        }
    }, LV_EVENT_DRAW_MAIN, v);

    // Digital readout badge under the monitor canvas
    auto* readout = lv_obj_create(monitor_box);
    lv_obj_set_pos(readout, 12, 298);
    lv_obj_set_size(readout, 472, 48);
    lv_obj_remove_flag(readout, LV_OBJ_FLAG_SCROLLABLE);
    ui::rack_skin::apply_recessed_display(readout, lv_color_hex(0x0C0F12), lv_color_hex(0x343E48));
    lv_obj_set_style_pad_all(readout, 0, 0);
    v->gate_state_lbl = instrument_label(readout, "GATE STATE: --", 8, 4, &ac_font_14, COLOR_TEXT2);
    v->gate_level_lbl = instrument_label(readout, "", 8, 26, &ac_font_12, COLOR_LCD_TEXT);
}

// ============================================================================
// 6b. BRICKWALL LIMITER VINTAGE ANALOG MASTERING FACEPLATE (limiter-local)
// ============================================================================
// Limiter-local material palette. These colors apply ONLY to this editor; the
// shared PRISMIX theme and every other processor keep their styling untouched.
namespace limiter_skin {
constexpr uint32_t kFaceplate = 0x30312F; // dark charcoal faceplate
constexpr uint32_t kRecessed  = 0x1A1B19; // recessed panel
constexpr uint32_t kRaised    = 0x3B3C38; // raised control surface
constexpr uint32_t kBorder    = 0x555650; // neutral border / engraved line
constexpr uint32_t kLabelPri  = 0xE0DFD6; // primary labels
constexpr uint32_t kLabelSec  = 0xAAA99F; // secondary labels
constexpr uint32_t kKnobBody  = 0x171817; // matte-black knob body
constexpr uint32_t kKnobRing  = 0x85867F; // neutral silver-grey ring
constexpr uint32_t kKnobPtr   = 0xE0DFD6; // knob pointer
constexpr uint32_t kMeterBg   = 0x111210; // meter background
constexpr uint32_t kGreen     = 0x35C878; // signal green
constexpr uint32_t kAmber     = 0xD89A42; // warning amber
constexpr uint32_t kRed       = 0xD94A43; // clipping red
} // namespace limiter_skin

// Limiter-local arc rendering (sampled polyline; no shared widget changes)
void limiter_draw_arc(lv_layer_t* layer, int cx, int cy, int radius,
                      float a0, float a1, int width, lv_color_t color) {
    if (a1 < a0) std::swap(a0, a1);
    constexpr float kPi = 3.14159265f;
    const int segs = std::max(1, static_cast<int>(std::ceil((a1 - a0) / 3.0f)));
    for (int i = 0; i < segs; ++i) {
        const float s0 = (a0 + (a1 - a0) * (static_cast<float>(i) / segs)) * kPi / 180.0f;
        const float s1 = (a0 + (a1 - a0) * (static_cast<float>(i + 1) / segs)) * kPi / 180.0f;
        lv_draw_line_dsc_t d;
        lv_draw_line_dsc_init(&d);
        d.color = color;
        d.width = width;
        d.p1.x = cx + static_cast<int>(std::lround(std::cos(s0) * radius));
        d.p1.y = cy + static_cast<int>(std::lround(std::sin(s0) * radius));
        d.p2.x = cx + static_cast<int>(std::lround(std::cos(s1) * radius));
        d.p2.y = cy + static_cast<int>(std::lround(std::sin(s1) * radius));
        lv_draw_line(layer, &d);
    }
}

// Vintage needle + counterweight tail + machined pivot hub
void limiter_draw_needle(lv_layer_t* layer, int cx, int cy, float angle_deg, int length) {
    constexpr float kPi = 3.14159265f;
    const float rad = angle_deg * kPi / 180.0f;
    const float ca = std::cos(rad), sa = std::sin(rad);

    lv_draw_line_dsc_t tail;
    lv_draw_line_dsc_init(&tail);
    tail.color = lv_color_hex(0x8A8B84);
    tail.width = 2;
    tail.p1.x = cx - static_cast<int>(std::lround(ca * 12));
    tail.p1.y = cy - static_cast<int>(std::lround(sa * 12));
    tail.p2.x = cx;
    tail.p2.y = cy;
    lv_draw_line(layer, &tail);

    lv_draw_line_dsc_t ptr;
    lv_draw_line_dsc_init(&ptr);
    ptr.color = lv_color_hex(limiter_skin::kLabelPri);
    ptr.width = 3;
    ptr.p1.x = cx + static_cast<int>(std::lround(ca * 5));
    ptr.p1.y = cy + static_cast<int>(std::lround(sa * 5));
    ptr.p2.x = cx + static_cast<int>(std::lround(ca * length));
    ptr.p2.y = cy + static_cast<int>(std::lround(sa * length));
    lv_draw_line(layer, &ptr);

    lv_draw_rect_dsc_t hub;
    lv_draw_rect_dsc_init(&hub);
    hub.radius = LV_RADIUS_CIRCLE;
    hub.bg_color = lv_color_hex(limiter_skin::kRaised);
    hub.bg_opa = LV_OPA_COVER;
    hub.border_color = lv_color_hex(limiter_skin::kBorder);
    hub.border_width = 1;
    lv_area_t ha{cx - 7, cy - 7, cx + 7, cy + 7};
    lv_draw_rect(layer, &hub, &ha);

    lv_draw_rect_dsc_t pin;
    lv_draw_rect_dsc_init(&pin);
    pin.radius = LV_RADIUS_CIRCLE;
    pin.bg_color = lv_color_hex(limiter_skin::kKnobRing);
    pin.bg_opa = LV_OPA_COVER;
    lv_area_t pa{cx - 2, cy - 2, cx + 2, cy + 2};
    lv_draw_rect(layer, &pin, &pa);
}

// Faceplate module box in the limiter-local material treatment
lv_obj_t* create_limiter_module_box(lv_obj_t* parent, int x, int y, int w, int h, const char* title) {
    auto* box = lv_obj_create(parent);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_size(box, w, h);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(box, lv_color_hex(limiter_skin::kFaceplate), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(limiter_skin::kBorder), 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_border_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 6, 0);
    lv_obj_set_style_shadow_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);

    auto* hdr = lv_obj_create(box);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_size(hdr, w, 26);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(limiter_skin::kRecessed), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(hdr, lv_color_hex(limiter_skin::kBorder), 0);
    lv_obj_set_style_border_width(hdr, 1, 0);
    lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_opa(hdr, LV_OPA_70, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    ui::rack_skin::create_rack_screw(hdr, 5, 6);
    if (w >= 180) {
        ui::rack_skin::create_rack_screw(hdr, w - 19, 6);
    }
    instrument_label(hdr, title, 24, 5, &ac_font_12, lv_color_hex(limiter_skin::kLabelSec));
    return box;
}

// Matte-black rotary control with silver-grey outer ring, fine engraved scale
// marks, subtle machined depth and a white pointer. Drawn over the shared knob
// artwork; bindings and interaction behavior are completely unchanged.
void limiter_knob_draw_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;
    auto* param = static_cast<const PluginParameter*>(lv_event_get_user_data(e));
    if (!param) return;
    auto* face = static_cast<lv_obj_t*>(lv_event_get_target(e));
    lv_layer_t* layer = lv_event_get_layer(e);
    lv_area_t a{};
    lv_obj_get_coords(face, &a);
    const int cx = (a.x1 + a.x2) / 2;
    const int cy = (a.y1 + a.y2) / 2;
    const int r = (std::min(lv_area_get_width(&a), lv_area_get_height(&a)) / 2) - 2;
    constexpr float kPi = 3.14159265f;

    const float span = (param->max_val > param->min_val) ? (param->max_val - param->min_val) : 1.0f;
    const float norm = std::clamp((param->current_val - param->min_val) / span, 0.0f, 1.0f);
    const float angle = 135.0f + norm * 270.0f;

    // 1. Neutral silver-grey outer ring (covers the shared widget's scale ticks)
    lv_draw_rect_dsc_t ring;
    lv_draw_rect_dsc_init(&ring);
    ring.radius = LV_RADIUS_CIRCLE;
    ring.bg_color = lv_color_hex(limiter_skin::kKnobRing);
    ring.bg_opa = LV_OPA_COVER;
    lv_area_t ring_a{cx - r - 2, cy - r - 2, cx + r + 2, cy + r + 2};
    lv_draw_rect(layer, &ring, &ring_a);

    // 2. Fine engraved scale markings across the 270 deg sweep
    for (int t = 0; t <= 12; ++t) {
        const float deg = 135.0f + t * 22.5f;
        const float rad = deg * kPi / 180.0f;
        const bool major = (t == 0 || t == 6 || t == 12);
        lv_draw_line_dsc_t tick;
        lv_draw_line_dsc_init(&tick);
        tick.color = major ? lv_color_hex(limiter_skin::kFaceplate) : lv_color_hex(limiter_skin::kBorder);
        tick.width = major ? 2 : 1;
        const int r_out = r - 1;
        const int r_in = major ? (r - 8) : (r - 5);
        tick.p1.x = cx + static_cast<int>(std::lround(std::cos(rad) * r_out));
        tick.p1.y = cy + static_cast<int>(std::lround(std::sin(rad) * r_out));
        tick.p2.x = cx + static_cast<int>(std::lround(std::cos(rad) * r_in));
        tick.p2.y = cy + static_cast<int>(std::lround(std::sin(rad) * r_in));
        lv_draw_line(layer, &tick);
    }

    // 3. Recessed collar + matte-black control body (cover all shared artwork)
    lv_draw_rect_dsc_t body;
    lv_draw_rect_dsc_init(&body);
    body.radius = LV_RADIUS_CIRCLE;
    body.bg_color = lv_color_hex(limiter_skin::kMeterBg);
    body.bg_opa = LV_OPA_COVER;
    lv_area_t collar_a{cx - r + 9, cy - r + 9, cx + r - 9, cy + r - 9};
    lv_draw_rect(layer, &body, &collar_a);
    body.bg_color = lv_color_hex(limiter_skin::kKnobBody);
    lv_area_t body_a{cx - r + 11, cy - r + 11, cx + r - 11, cy + r - 11};
    lv_draw_rect(layer, &body, &body_a);

    // 4. Subtle mechanical depth: ring highlight / shade + soft body top sheen
    limiter_draw_arc(layer, cx, cy, r + 1, 205.0f, 335.0f, 1, lv_color_hex(0xA6A79E));
    limiter_draw_arc(layer, cx, cy, r + 1, 25.0f, 155.0f, 1, lv_color_hex(0x5B5C55));
    limiter_draw_arc(layer, cx, cy, r - 12, 215.0f, 325.0f, 1, lv_color_hex(0x262724));

    // 5. White pointer + machined centre hub
    lv_draw_line_dsc_t ptr;
    lv_draw_line_dsc_init(&ptr);
    ptr.color = lv_color_hex(limiter_skin::kKnobPtr);
    ptr.width = 3;
    const float prad = angle * kPi / 180.0f;
    ptr.p1.x = cx + static_cast<int>(std::lround(std::cos(prad) * 5));
    ptr.p1.y = cy + static_cast<int>(std::lround(std::sin(prad) * 5));
    ptr.p2.x = cx + static_cast<int>(std::lround(std::cos(prad) * (r - 14)));
    ptr.p2.y = cy + static_cast<int>(std::lround(std::sin(prad) * (r - 14)));
    lv_draw_line(layer, &ptr);

    lv_draw_rect_dsc_t hub;
    lv_draw_rect_dsc_init(&hub);
    hub.radius = LV_RADIUS_CIRCLE;
    hub.bg_color = lv_color_hex(limiter_skin::kRaised);
    hub.bg_opa = LV_OPA_COVER;
    hub.border_color = lv_color_hex(limiter_skin::kBorder);
    hub.border_width = 1;
    lv_area_t hub_a{cx - 6, cy - 6, cx + 6, cy + 6};
    lv_draw_rect(layer, &hub, &hub_a);
}

// Apply the limiter-local treatment to a shared knob group: engraved-style
// label, recessed digital readout badge and the matte/silver rotary redraw.
void apply_limiter_knob_skin(lv_obj_t* group, PluginInstance* inst, const char* param_id) {
    if (!group || !inst) return;
    if (auto* name = lv_obj_get_child(group, 0)) {
        lv_obj_set_style_text_color(name, lv_color_hex(limiter_skin::kLabelSec), 0);
    }
    if (auto* readout = lv_obj_get_child(group, 2)) {
        lv_obj_set_style_bg_color(readout, lv_color_hex(limiter_skin::kRecessed), 0);
        lv_obj_set_style_border_color(readout, lv_color_hex(limiter_skin::kBorder), 0);
        lv_obj_set_style_text_color(readout, lv_color_hex(limiter_skin::kLabelPri), 0);
    }
    if (auto* param = inst->find_parameter(param_id)) {
        if (auto* face = lv_obj_get_child(group, 1)) {
            lv_obj_add_event_cb(face, limiter_knob_draw_cb, LV_EVENT_DRAW_MAIN, param);
        }
    }
}

// Analog needle-meter face shared by the input peak and gain reduction meters:
// recessed black dial, graduated colored track, silkscreen legend and a
// galvanometer needle driven only by measured engine telemetry.
void limiter_meter_draw_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_DRAW_MAIN) return;
    auto* view = static_cast<FxScreenView*>(lv_event_get_user_data(e));
    if (!view) return;
    auto* obj = static_cast<lv_obj_t*>(lv_event_get_target(e));
    const bool is_input = (obj == view->limiter_in_canvas);
    ChannelState* ch = resolve_target_channel(view);
    if (!ch) return;
    const bool measured = view->selected_slot >= 0 && view->selected_slot < 8 &&
        !(ch->fx_slot_limiter_gr_db[static_cast<size_t>(view->selected_slot)] >= LIMITER_GR_UNAVAILABLE);

    lv_layer_t* layer = lv_event_get_layer(e);
    lv_area_t c{};
    lv_obj_get_coords(obj, &c);
    const int cx = c.x1 + lv_area_get_width(&c) / 2;
    const int cy = c.y2 - 24;
    const int r_scale = lv_area_get_height(&c) - 84;
    constexpr float kPi = 3.14159265f;

    // Recessed meter background
    lv_draw_rect_dsc_t face;
    lv_draw_rect_dsc_init(&face);
    face.radius = 4;
    face.bg_color = lv_color_hex(limiter_skin::kMeterBg);
    face.bg_opa = LV_OPA_COVER;
    face.border_color = lv_color_hex(limiter_skin::kFaceplate);
    face.border_width = 1;
    lv_area_t fa{c.x1 + 4, c.y1 + 4, c.x2 - 4, c.y2 - 4};
    lv_draw_rect(layer, &face, &fa);

    // Silkscreen legend
    lv_draw_label_dsc_t ld;
    lv_draw_label_dsc_init(&ld);
    ld.font = &ac_font_14;
    ld.color = lv_color_hex(limiter_skin::kLabelSec);
    ld.text = is_input ? "INPUT PEAK" : "GAIN REDUCTION";
    ld.align = LV_TEXT_ALIGN_CENTER;
    lv_area_t ta{c.x1, c.y1 + 12, c.x2, c.y1 + 28};
    lv_draw_label(layer, &ld, &ta);

    lv_draw_label_dsc_t ud;
    lv_draw_label_dsc_init(&ud);
    ud.font = &ac_font_12;
    ud.color = lv_color_hex(limiter_skin::kBorder);
    ud.text = is_input ? "dBFS" : "dB";
    ud.align = LV_TEXT_ALIGN_CENTER;
    lv_area_t ua{c.x1, c.y1 + 30, c.x2, c.y1 + 44};
    lv_draw_label(layer, &ud, &ua);

    if (!measured) {
        lv_draw_label_dsc_t nd;
        lv_draw_label_dsc_init(&nd);
        nd.font = &ac_font_12;
        nd.color = lv_color_hex(limiter_skin::kBorder);
        nd.text = "NO LIVE TELEMETRY";
        nd.align = LV_TEXT_ALIGN_CENTER;
        lv_area_t na{c.x1, c.y1 + 48, c.x2, c.y1 + 62};
        lv_draw_label(layer, &nd, &na);
    }

    // Scale calibration across a 120 deg sweep:
    //  - input peak: -36..+3 dBFS (linear)
    //  - gain reduction: 0..20 dB (compressed law, classic GR meter spread)
    auto ang_of = [&](float t) { return 210.0f + std::clamp(t, 0.0f, 1.0f) * 120.0f; };
    float v01 = 0.0f;
    float z_lo = 0.0f, z_hi = 0.0f;
    int mark_count = 0;
    const char* const* mark_lbl = nullptr;
    int minor_count = 0;
    int zero_index = -1;
    float mark_pos[9] = {};
    float minor_pos[5] = {};

    static const float kInMarkDb[9] = {-36.0f, -30.0f, -24.0f, -18.0f, -12.0f, -6.0f, -3.0f, 0.0f, 3.0f};
    static const char* const kInMarkLbl[9] = {"-36", "-30", "-24", "-18", "-12", "-6", "-3", "0", "+3"};
    static const float kInMinorDb[5] = {-33.0f, -27.0f, -21.0f, -15.0f, -9.0f};
    static const float kGrMarkDb[8] = {0.0f, 1.0f, 2.0f, 3.0f, 6.0f, 10.0f, 15.0f, 20.0f};
    static const char* const kGrMarkLbl[8] = {"0", "-1", "-2", "-3", "-6", "-10", "-15", "-20"};
    static const float kGrMinorDb[3] = {4.0f, 8.0f, 12.0f};

    if (is_input) {
        for (int i = 0; i < 9; ++i) mark_pos[i] = std::clamp((kInMarkDb[i] + 36.0f) / 39.0f, 0.0f, 1.0f);
        for (int i = 0; i < 5; ++i) minor_pos[i] = std::clamp((kInMinorDb[i] + 36.0f) / 39.0f, 0.0f, 1.0f);
        mark_count = 9; mark_lbl = kInMarkLbl;
        minor_count = 5;
        v01 = measured ? std::clamp((view->limiter_needle_in_db + 36.0f) / 39.0f, 0.0f, 1.0f) : 0.0f;
        z_lo = mark_pos[5];  // -6 dBFS
        z_hi = mark_pos[6];  // -3 dBFS
    } else {
        for (int i = 0; i < 8; ++i) mark_pos[i] = std::pow(std::clamp(kGrMarkDb[i] / 20.0f, 0.0f, 1.0f), 0.6f);
        for (int i = 0; i < 3; ++i) minor_pos[i] = std::pow(std::clamp(kGrMinorDb[i] / 20.0f, 0.0f, 1.0f), 0.6f);
        mark_count = 8; mark_lbl = kGrMarkLbl;
        minor_count = 3;
        zero_index = 0;
        v01 = measured ? std::pow(std::clamp(view->limiter_needle_gr_db / 20.0f, 0.0f, 1.0f), 0.6f) : 0.0f;
        z_lo = mark_pos[3];  // -3 dB reduction
        z_hi = mark_pos[5];  // -10 dB reduction
    }

    // Graduated colored track: signal green -> warning amber -> clipping red
    limiter_draw_arc(layer, cx, cy, r_scale, ang_of(0.0f), ang_of(z_lo), 3, lv_color_hex(limiter_skin::kGreen));
    limiter_draw_arc(layer, cx, cy, r_scale, ang_of(z_lo), ang_of(z_hi), 3, lv_color_hex(limiter_skin::kAmber));
    limiter_draw_arc(layer, cx, cy, r_scale, ang_of(z_hi), ang_of(1.0f), 3, lv_color_hex(limiter_skin::kRed));

    // Minor scale ticks
    for (int i = 0; i < minor_count; ++i) {
        const float rad = ang_of(minor_pos[i]) * kPi / 180.0f;
        lv_draw_line_dsc_t tk;
        lv_draw_line_dsc_init(&tk);
        tk.color = lv_color_hex(limiter_skin::kLabelSec);
        tk.width = 1;
        tk.p1.x = cx + static_cast<int>(std::lround(std::cos(rad) * (r_scale - 3)));
        tk.p1.y = cy + static_cast<int>(std::lround(std::sin(rad) * (r_scale - 3)));
        tk.p2.x = cx + static_cast<int>(std::lround(std::cos(rad) * (r_scale - 9)));
        tk.p2.y = cy + static_cast<int>(std::lround(std::sin(rad) * (r_scale - 9)));
        lv_draw_line(layer, &tk);
    }

    // Major ticks + engraved values (GR meter: emphasized zero reference)
    for (int i = 0; i < mark_count; ++i) {
        const float rad = ang_of(mark_pos[i]) * kPi / 180.0f;
        const bool zero = (i == zero_index);
        lv_draw_line_dsc_t tk;
        lv_draw_line_dsc_init(&tk);
        tk.color = lv_color_hex(zero ? limiter_skin::kLabelPri : limiter_skin::kLabelSec);
        tk.width = 2;
        const int r_out = r_scale - 3;
        const int r_in = zero ? (r_scale - 17) : (r_scale - 14);
        tk.p1.x = cx + static_cast<int>(std::lround(std::cos(rad) * r_out));
        tk.p1.y = cy + static_cast<int>(std::lround(std::sin(rad) * r_out));
        tk.p2.x = cx + static_cast<int>(std::lround(std::cos(rad) * r_in));
        tk.p2.y = cy + static_cast<int>(std::lround(std::sin(rad) * r_in));
        lv_draw_line(layer, &tk);

        lv_draw_label_dsc_t md;
        lv_draw_label_dsc_init(&md);
        md.font = &ac_font_12;
        md.color = lv_color_hex(zero ? limiter_skin::kLabelPri : limiter_skin::kLabelSec);
        md.text = mark_lbl[i];
        md.align = LV_TEXT_ALIGN_CENTER;
        const int lx = cx + static_cast<int>(std::lround(std::cos(rad) * (r_scale - 27)));
        const int ly = cy + static_cast<int>(std::lround(std::sin(rad) * (r_scale - 27)));
        lv_area_t la{lx - 24, ly - 8, lx + 24, ly + 8};
        lv_draw_label(layer, &md, &la);
    }

    // Galvanometer needle (rests at the zero reference when not measured)
    limiter_draw_needle(layer, cx, cy, ang_of(v01), r_scale - 8);
}

void build_limiter_rack_editor(FxScreenView* v, ChannelState* ch, PluginInstance* inst) {
    // ------------------------------------------------------------------------
    // A. MONITORING SECTION: analog input peak needle meter + dedicated
    //    graduated gain-reduction meter + real digital telemetry readouts
    // ------------------------------------------------------------------------
    auto* mon_box = create_limiter_module_box(v->editor_content, 16, 94, 1192, 330, "MONITORING");

    auto make_meter = [&](int x, int y) -> lv_obj_t* {
        auto* m = lv_obj_create(mon_box);
        lv_obj_set_pos(m, x, y);
        lv_obj_set_size(m, 556, 232);
        lv_obj_remove_flag(m, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(m, lv_color_hex(limiter_skin::kMeterBg), 0);
        lv_obj_set_style_bg_opa(m, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(m, lv_color_hex(limiter_skin::kBorder), 0);
        lv_obj_set_style_border_width(m, 2, 0);
        lv_obj_set_style_radius(m, 6, 0);
        lv_obj_set_style_pad_all(m, 0, 0);
        return m;
    };

    v->limiter_in_canvas = make_meter(24, 40);
    v->limiter_gr_canvas = make_meter(612, 40);
    lv_obj_add_event_cb(v->limiter_in_canvas, limiter_meter_draw_cb, LV_EVENT_DRAW_MAIN, v);
    lv_obj_add_event_cb(v->limiter_gr_canvas, limiter_meter_draw_cb, LV_EVENT_DRAW_MAIN, v);

    // Digital readout strip: measured rack I/O peaks and measured gain reduction
    auto* strip = lv_obj_create(mon_box);
    lv_obj_set_pos(strip, 24, 282);
    lv_obj_set_size(strip, 1144, 34);
    lv_obj_remove_flag(strip, LV_OBJ_FLAG_SCROLLABLE);
    ui::rack_skin::apply_recessed_display(strip, lv_color_hex(limiter_skin::kRecessed), lv_color_hex(limiter_skin::kBorder));
    lv_obj_set_style_pad_all(strip, 0, 0);
    v->limiter_in_lbl = instrument_label(strip, "IN PEAK  -- dBFS", 16, 13, &lv_font_unscii_8, lv_color_hex(limiter_skin::kLabelPri));
    v->limiter_out_lbl = instrument_label(strip, "OUT PEAK  -- dBFS", 336, 13, &lv_font_unscii_8, lv_color_hex(limiter_skin::kLabelPri));
    v->limiter_gr_lbl = instrument_label(strip, "GAIN REDUCTION  -- dB", 676, 13, &lv_font_unscii_8, lv_color_hex(limiter_skin::kLabelPri));
    instrument_label(strip, ch->format == ChannelFormat::Stereo ? "2-CH STEREO" : "1-CH MONO", 1010, 13,
                     &lv_font_unscii_8, lv_color_hex(limiter_skin::kLabelSec));

    // ------------------------------------------------------------------------
    // B. MAIN LIMITER CONTROLS: four matte rotary knobs on a raised control
    //    surface with engraved panel divisions (ranges/values/bindings kept)
    // ------------------------------------------------------------------------
    auto* ctrl_box = create_limiter_module_box(v->editor_content, 16, 436, 1192, 370, "LIMITER CONTROLS");

    auto* surface = lv_obj_create(ctrl_box);
    lv_obj_set_pos(surface, 0, 26);
    lv_obj_set_size(surface, 1192, 344);
    lv_obj_remove_flag(surface, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(surface, lv_color_hex(limiter_skin::kRaised), 0);
    lv_obj_set_style_bg_opa(surface, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(surface, 0, 0);
    lv_obj_set_style_radius(surface, 0, 0);
    lv_obj_set_style_pad_all(surface, 0, 0);

    for (int i = 1; i <= 3; ++i) {
        auto* groove = lv_obj_create(surface);
        lv_obj_set_pos(groove, i * 298, 0);
        lv_obj_set_size(groove, 1, 344);
        lv_obj_remove_flag(groove, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(groove, lv_color_hex(0x262724), 0);
        lv_obj_set_style_bg_opa(groove, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(groove, 0, 0);
        lv_obj_set_style_radius(groove, 0, 0);

        auto* edge = lv_obj_create(surface);
        lv_obj_set_pos(edge, i * 298 + 1, 0);
        lv_obj_set_size(edge, 1, 344);
        lv_obj_remove_flag(edge, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(edge, lv_color_hex(0x4A4B45), 0);
        lv_obj_set_style_bg_opa(edge, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(edge, 0, 0);
        lv_obj_set_style_radius(edge, 0, 0);
    }

    struct LimiterKnobSpec { const char* id; const char* label; int x; };
    static const LimiterKnobSpec kKnobs[4] = {
        {"threshold", "THRESHOLD", 85},
        {"ceiling",   "CEILING",   383},
        {"release",   "RELEASE",   681},
        {"lookahead", "LOOKAHEAD", 979},
    };
    for (int i = 0; i < 4; ++i) {
        auto* group = place_param_knob(ctrl_box, inst, kKnobs[i].id, kKnobs[i].label, kKnobs[i].x, 122, 108);
        apply_limiter_knob_skin(group, inst, kKnobs[i].id);
    }
}

// ============================================================================
// 7. STANDARD DYNAMICS / EQ / LIMITER / GATE / LV2 RACK EDITOR
// ============================================================================
void build_standard_rack_editor(FxScreenView* v, ChannelState* ch, PluginInstance* inst) {
    auto* meter_box = create_rack_module_box(
        v->editor_content, 16, 94, 1192, 146,
        "PROCESSOR SIGNAL FLOW & LIVE I/O LEVEL TELEMETRY",
        COLOR_CYAN, lv_color_hex(0x161D24));
    build_io_meter_strip(v, meter_box, 16, 30, 620);

    char mode_txt[180];
    std::snprintf(mode_txt, sizeof(mode_txt),
                  "TARGET: %s (%s)\nPROCESSOR: %s\nSTATUS: %s",
                  ch->name,
                  ch->format == ChannelFormat::Stereo ? "2-CH STEREO" : "1-CH MONO",
                  inst->get_name().c_str(),
                  inst->is_bypassed() ? "BYPASSED (UNITY PASSTHROUGH)" : "ACTIVE REALTIME DSP");
    instrument_label(meter_box, mode_txt, 670, 44, &ac_font_14, COLOR_CYAN);

    auto* param_box = create_rack_module_box(
        v->editor_content, 16, 252, 1192, 554,
        "RACK UNIT PARAMETER CONTROLS", COLOR_CYAN, lv_color_hex(0x161D24));

    const size_t param_count = inst->parameter_count();
    const int cols = 4;
    const int col_w = 286;
    const int row_h = 180;

    for (size_t p = 0; p < param_count; ++p) {
        auto* param = inst->get_parameter(p);
        if (!param) continue;
        int row = static_cast<int>(p) / cols;
        int col = static_cast<int>(p) % cols;
        int kx = 24 + col * col_w + (col_w - 96) / 2;
        int ky = 56 + row * row_h;
        instrument_knob(param_box,
                        param->name.c_str(),
                        &param->current_val,
                        param->min_val,
                        param->max_val,
                        param->default_val,
                        param->unit.c_str(),
                        kx,
                        ky,
                        82);
    }
}

void build_editor(FxScreenView* v) {
    ChannelState* ch = resolve_target_channel(v);
    if (!v || !ch || !v->editor_content) return;
    reset_live_editor_pointers(v);
    lv_obj_clean(v->editor_content);

    auto* inst = ch->fx_rack.get_slot(v->selected_slot);
    if (!inst) {
        auto* empty_lbl = lv_label_create(v->editor_content);
        lv_label_set_text(empty_lbl,
            "NO EFFECT ASSIGNED TO THIS RACK SLOT\n\n"
            "CLICK '+ ASSIGN FX TO THIS RACK BAY' ON THE LEFT TO LOAD REVERB, DELAY, AFS, EQ, OR DYNAMICS");
        lv_obj_align(empty_lbl, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_text_font(empty_lbl, &ac_font_16, 0);
        lv_obj_set_style_text_color(empty_lbl, COLOR_TEXT2, 0);
        return;
    }

    // Common Rack Unit Header Bar (Brushed Metal Faceplate Identification Strip)
    const std::string& uri = inst->get_uri();
    auto* info_box = lv_obj_create(v->editor_content);
    lv_obj_set_pos(info_box, 16, 10);
    lv_obj_set_size(info_box, 1192, 76);
    lv_obj_remove_flag(info_box, LV_OBJ_FLAG_SCROLLABLE);
    if (uri == "http://audiocore.org/plugins/compressor" || uri == "http://audiocore.org/plugins/gate") {
        ui::rack_skin::apply_grey_faceplate_panel(info_box, lv_color_hex(0x6E7B89), lv_color_hex(0x353D46));
    } else {
        ui::rack_skin::apply_faceplate_panel(info_box, lv_color_hex(0x36424E), lv_color_hex(0x181E25));
    }
    lv_obj_set_style_pad_all(info_box, 0, 0);
    ui::rack_skin::add_faceplate_corner_screws(info_box, 1192, 76, 4);

    const bool bypassed = inst->is_bypassed();
    ui::rack_skin::create_bezel_led(info_box, 26, 12, bypassed ? COLOR_ORANGE : COLOR_GREEN, true);
    instrument_label(info_box, inst->get_name().c_str(), 44, 6, &ac_font_20, COLOR_CYAN);
    char cat_uri[180];
    const char* route_str = (isAuxBusChannel(ch) && inst->prefers_send_return_on_aux())
        ? "AUX SEND/RETURN (100% WET)"
        : "INSERT PROCESSOR";
    std::snprintf(cat_uri, sizeof(cat_uri),
                  "SLOT %02d  |  CATEGORY: %s  |  INSTANCE #%llu  |  TARGET: %s (%s)",
                  v->selected_slot + 1,
                  inst->get_category().c_str(),
                  static_cast<unsigned long long>(inst->get_instance_id()),
                  ch->name,
                  route_str);
    instrument_label(info_box, cat_uri, 26, 33, &ac_font_12, COLOR_TEXT2);
    if (!inst->get_description().empty()) {
        instrument_label(info_box, inst->get_description().c_str(), 26, 51, &ac_font_12, COLOR_TEXT);
    }

    // Editor Bypass Button
    auto* byp_btn = lv_button_create(info_box);
    lv_obj_set_size(byp_btn, 148, 44);
    lv_obj_set_pos(byp_btn, 1018, 16);
    ui::rack_skin::apply_hardware_button(
        byp_btn, bypassed, COLOR_ORANGE, lv_color_hex(0x4E3209), lv_color_hex(0x1A2027));
    auto* byp_lbl = lv_label_create(byp_btn);
    lv_label_set_text(byp_lbl, bypassed ? "BYPASS ON" : "BYPASS OFF");
    lv_obj_center(byp_lbl);
    lv_obj_set_style_text_color(byp_lbl, bypassed ? COLOR_ORANGE : COLOR_TEXT2, 0);

    lv_obj_add_event_cb(byp_btn, [](lv_event_t* e) {
        auto* view = static_cast<FxScreenView*>(lv_event_get_user_data(e));
        if (!view) return;
        ChannelState* target = resolve_target_channel(view);
        if (!target) return;
        if (auto* p = target->fx_rack.get_slot(view->selected_slot)) {
            p->set_bypassed(!p->is_bypassed());
            sync_channel_fx_to_dsp(target);
            view->last_param_fingerprint = compute_rack_fingerprint(target);
            schedule_ui_rebuild(view, true, true);
        }
    }, LV_EVENT_CLICKED, v);

    // Dispatch to dedicated rack editor
    if (uri == "http://audiocore.org/plugins/reverb") {
        build_reverb_rack_editor(v, ch, inst);
    } else if (uri == "http://audiocore.org/plugins/delay") {
        build_delay_rack_editor(v, ch, inst);
    } else if (uri == "http://audiocore.org/plugins/afs") {
        build_afs_rack_editor(v, ch, inst);
    } else if (uri == "http://audiocore.org/plugins/compressor") {
        build_compressor_rack_editor(v, ch, inst);
    } else if (uri == "http://audiocore.org/plugins/gate") {
        build_gate_rack_editor(v, ch, inst);
    } else if (uri == "http://audiocore.org/plugins/limiter") {
        build_limiter_rack_editor(v, ch, inst);
    } else if (inst->get_source_type() == PluginSourceType::NamLite || uri.rfind("nam:", 0) == 0) {
        build_nam_rack_editor(v, ch, inst);
    } else {
        build_standard_rack_editor(v, ch, inst);
    }
}

void fx_live_timer_cb(lv_timer_t* t) {
    auto* v = static_cast<FxScreenView*>(lv_timer_get_user_data(t));
    if (!v) return;
    ChannelState* ch = resolve_target_channel(v);
    if (!ch) return;

    // 0. Detect external channel selection or rack slot structure changes
    const std::string current_sig = compute_structure_signature(ch);
    if (current_sig != v->last_structure_signature) {
        v->last_structure_signature = current_sig;
        v->last_param_fingerprint = compute_rack_fingerprint(ch);
        sync_channel_fx_to_dsp(ch);
        schedule_ui_rebuild(v, true, true);
        return;
    }

    // 1. Detect parameter knob changes & sync immediately to AudioService DSP
    const std::string current_fp = compute_rack_fingerprint(ch);
    if (current_fp != v->last_param_fingerprint) {
        v->last_param_fingerprint = current_fp;
        if (auto* inst = ch->fx_rack.get_slot(v->selected_slot)) {
            if (inst->get_uri() == "http://audiocore.org/plugins/delay" &&
                inst->get_parameter_value("link_lr", 0.0f) >= 0.5f) {
                inst->set_parameter_value("time_r", inst->get_parameter_value("time_l", 320.0f));
            }
        }
        sync_channel_fx_to_dsp(ch);
        if (v->visual_canvas) {
            lv_obj_invalidate(v->visual_canvas);
        }
    }

    // 2. Update Live PRE-FX INPUT & POST-FX OUTPUT Hardware Bargraph Meter
    if (v->io_meter_canvas) {
        lv_obj_invalidate(v->io_meter_canvas);
    }
    if (v->io_readout_lbl) {
        char buf[128];
        const bool isStereo = (ch->format == ChannelFormat::Stereo);
        const float inPeakMax = isStereo ? std::max(ch->fx_in_peak_l, ch->fx_in_peak_r) : ch->fx_in_peak_l;
        const float outPeakMax = isStereo ? std::max(ch->fx_out_peak_l, ch->fx_out_peak_r) : ch->fx_out_peak_l;
        const float deltaDb = (inPeakMax > -89.0f && outPeakMax > -89.0f) ? (outPeakMax - inPeakMax) : 0.0f;
        if (isStereo) {
            std::snprintf(buf, sizeof(buf),
                          "PRE-FX L/R: %.1f / %.1f dB  |  POST-FX L/R: %.1f / %.1f dB  (DIFF %+.1f dB)",
                          ch->fx_in_peak_l, ch->fx_in_peak_r,
                          ch->fx_out_peak_l, ch->fx_out_peak_r,
                          deltaDb);
        } else {
            std::snprintf(buf, sizeof(buf),
                          "PRE-FX (MONO): %.1f dB   |   POST-FX (MONO): %.1f dB   (DIFF %+.1f dB)",
                          ch->fx_in_peak_l, ch->fx_out_peak_l, deltaDb);
        }
        lv_label_set_text(v->io_readout_lbl, buf);
    }

    // 3. Update AFS live telemetry if AFS editor is active
    if (v->afs_status_label && v->afs_status_badge && v->afs_count_label) {
        static const char* kStateNames[5] = {
            "DETECTOR: DISABLED",
            "DETECTOR: BYPASSED",
            "DETECTOR: MONITORING",
            "DETECTOR: CANDIDATE DETECTED",
            "DETECTOR: ACTIVE SUPPRESSION"
        };
        const uint8_t st = std::min<uint8_t>(ch->afs_status, 4);
        lv_label_set_text(v->afs_status_label, kStateNames[st]);
        lv_color_t stColor = COLOR_GREEN;
        if (st == 0 || st == 1) stColor = COLOR_ORANGE;
        else if (st == 3) stColor = COLOR_ORANGE;
        else if (st == 4) stColor = COLOR_RED;
        lv_obj_set_style_border_color(v->afs_status_badge, stColor, 0);
        lv_obj_set_style_text_color(v->afs_status_label, stColor, 0);

        char cnt_buf[64];
        std::snprintf(cnt_buf, sizeof(cnt_buf), "ACTIVE NOTCHES: %u / %u",
                      static_cast<unsigned>(ch->afs_active_notch_count),
                      static_cast<unsigned>(std::max<uint8_t>(1, ch->afs_max_notches)));
        lv_label_set_text(v->afs_count_label, cnt_buf);

        static const char* kNotchStates[5] = {"FREE", "CANDIDATE", "ACTIVE", "HOLDING", "RELEASING"};
        for (int i = 0; i < 8; ++i) {
            if (!v->afs_notch_freq_lbl[i] || !v->afs_notch_depth_lbl[i] || !v->afs_notch_state_lbl[i]) continue;
            if (i < static_cast<int>(ch->afs_active_notch_count) && ch->afs_notches[i].state != 0) {
                const auto& n = ch->afs_notches[i];
                char fbuf[32], dbuf[32];
                std::snprintf(fbuf, sizeof(fbuf), "%.1f Hz", n.frequency_hz);
                std::snprintf(dbuf, sizeof(dbuf), "%.1f dB (Q%.0f)", n.depth_db, n.q);
                lv_label_set_text(v->afs_notch_freq_lbl[i], fbuf);
                lv_label_set_text(v->afs_notch_depth_lbl[i], dbuf);
                const uint8_t ns = std::min<uint8_t>(n.state, 4);
                lv_label_set_text(v->afs_notch_state_lbl[i], kNotchStates[ns]);
                lv_obj_set_style_text_color(v->afs_notch_freq_lbl[i], COLOR_TEXT, 0);
                lv_obj_set_style_text_color(v->afs_notch_depth_lbl[i], COLOR_CYAN, 0);
                lv_obj_set_style_text_color(v->afs_notch_state_lbl[i], ns == 2 ? COLOR_RED : COLOR_GREEN, 0);
            } else {
                lv_label_set_text(v->afs_notch_freq_lbl[i], "---- Hz");
                lv_label_set_text(v->afs_notch_depth_lbl[i], "0.0 dB");
                lv_label_set_text(v->afs_notch_state_lbl[i], "FREE");
                lv_obj_set_style_text_color(v->afs_notch_freq_lbl[i], COLOR_TEXT2, 0);
                lv_obj_set_style_text_color(v->afs_notch_depth_lbl[i], COLOR_TEXT2, 0);
                lv_obj_set_style_text_color(v->afs_notch_state_lbl[i], COLOR_TEXT2, 0);
            }
        }
        if (v->visual_canvas) {
            lv_obj_invalidate(v->visual_canvas);
        }
    }

    // 4. Update Studio Compressor Vintage VU Needle Ballistics & GR Telemetry
    if (v->comp_vu_canvas) {
        float target_gr = 0.0f;
        if (v->selected_slot >= 0 && v->selected_slot < 8) {
            target_gr = ch->fx_slot_gr_db[static_cast<size_t>(v->selected_slot)];
        }
        // Classic galvanometer ballistic smoothing:
        // Fast attack (~40% step per 30ms tick)
        // Damped mechanical needle return (~16% step per 30ms tick, ~300ms return)
        if (target_gr < v->comp_needle_gr_db) {
            v->comp_needle_gr_db += (target_gr - v->comp_needle_gr_db) * 0.40f;
        } else {
            v->comp_needle_gr_db += (target_gr - v->comp_needle_gr_db) * 0.16f;
        }
        if (std::abs(v->comp_needle_gr_db) < 0.05f) {
            v->comp_needle_gr_db = 0.0f;
        }
        lv_obj_invalidate(v->comp_vu_canvas);

        if (v->comp_gr_readout_lbl) {
            char gr_buf[64];
            if (std::abs(v->comp_needle_gr_db) < 0.05f) {
                std::snprintf(gr_buf, sizeof(gr_buf), "GR: 0.0 dB (IDLE)");
                lv_obj_set_style_text_color(v->comp_gr_readout_lbl, COLOR_TEXT2, 0);
            } else {
                std::snprintf(gr_buf, sizeof(gr_buf), "GR: %.1f dB", v->comp_needle_gr_db);
                lv_obj_set_style_text_color(v->comp_gr_readout_lbl, COLOR_CYAN, 0);
            }
            lv_label_set_text(v->comp_gr_readout_lbl, gr_buf);
        }
    }
    if (v->comp_curve_canvas) {
        lv_obj_invalidate(v->comp_curve_canvas);
    }

    // 5. Update Expander / Gate transfer curve, live monitor & state LED from real engine telemetry
    if (v->gate_curve_canvas || v->gate_monitor_canvas || v->gate_state_led) {
        const int slot_idx = std::clamp(v->selected_slot, 0, 7);
        const uint8_t gate_state = ch->fx_slot_gate_state[static_cast<size_t>(slot_idx)];
        const bool measured = (gate_state != GATE_STATE_UNAVAILABLE);
        auto* gate_inst = ch->fx_rack.get_slot(v->selected_slot);
        const bool gate_bypassed = gate_inst && gate_inst->is_bypassed();

        // Push measured samples into the monitor history (clear it when no engine is measuring)
        if (v->gate_monitor_canvas) {
            if (measured) {
                const float det_db = std::clamp(ch->fx_slot_gate_detector_db[static_cast<size_t>(slot_idx)], -90.0f, 0.0f);
                const float out_peak = (ch->format == ChannelFormat::Stereo)
                    ? std::max(ch->fx_out_peak_l, ch->fx_out_peak_r)
                    : ch->fx_out_peak_l;
                v->gate_hist_det_db[v->gate_hist_head] = det_db;
                v->gate_hist_out_db[v->gate_hist_head] = std::clamp(out_peak, -90.0f, 0.0f);
                v->gate_hist_state[v->gate_hist_head] = gate_state;
                v->gate_hist_head = (v->gate_hist_head + 1) % kGateHistoryPoints;
                if (v->gate_hist_count < kGateHistoryPoints) ++v->gate_hist_count;
            } else {
                v->gate_hist_count = 0;
                v->gate_hist_head = 0;
            }
            lv_obj_invalidate(v->gate_monitor_canvas);
        }
        if (v->gate_curve_canvas) {
            lv_obj_invalidate(v->gate_curve_canvas);
        }

        // Monitor bezel LED: live engine state color, dark when bypassed / not measured
        if (v->gate_state_led) {
            if (!measured) {
                ui::rack_skin::set_bezel_led_state(v->gate_state_led, lv_color_hex(0x39424C), false);
            } else if (gate_bypassed) {
                ui::rack_skin::set_bezel_led_state(v->gate_state_led, COLOR_ORANGE, false);
            } else {
                ui::rack_skin::set_bezel_led_state(v->gate_state_led, gate_ui_state_color(gate_state), true);
            }
        }

        // Digital readouts below the monitor canvas
        if (v->gate_state_lbl) {
            static const char* kStateNames[5] = {"CLOSED", "ATTACK", "OPEN", "HOLD", "RELEASE"};
            if (!measured) {
                lv_label_set_text(v->gate_state_lbl, "GATE STATE: NO TELEMETRY");
                lv_obj_set_style_text_color(v->gate_state_lbl, COLOR_ORANGE, 0);
            } else if (gate_bypassed) {
                lv_label_set_text(v->gate_state_lbl, "GATE STATE: BYPASSED");
                lv_obj_set_style_text_color(v->gate_state_lbl, COLOR_ORANGE, 0);
            } else {
                char st_buf[48];
                const uint8_t st = std::min<uint8_t>(gate_state, 4);
                std::snprintf(st_buf, sizeof(st_buf), "GATE STATE: %s", kStateNames[st]);
                lv_label_set_text(v->gate_state_lbl, st_buf);
                lv_obj_set_style_text_color(v->gate_state_lbl, gate_ui_state_color(st), 0);
            }
        }
        if (v->gate_level_lbl) {
            char lvl_buf[96];
            if (!measured) {
                std::snprintf(lvl_buf, sizeof(lvl_buf), "DETECTOR -- dB  |  GAIN REDUCTION -- dB (NOT MEASURED)");
            } else {
                const float gr_db = std::min(0.0f, ch->fx_slot_gate_gr_db[static_cast<size_t>(slot_idx)]);
                std::snprintf(lvl_buf, sizeof(lvl_buf), "DETECTOR %.1f dB  |  GAIN REDUCTION %.1f dB",
                              ch->fx_slot_gate_detector_db[static_cast<size_t>(slot_idx)], gr_db);
            }
            lv_label_set_text(v->gate_level_lbl, lvl_buf);
        }
        if (v->gate_curve_lbl && gate_inst) {
            char cf_buf[128];
            const float thresh = gate_inst->get_parameter_value("thresh", -48.0f);
            const float range = gate_inst->get_parameter_value("range", -60.0f);
            const float out_at_thr = std::max(-90.0f, thresh + range);
            std::snprintf(cf_buf, sizeof(cf_buf),
                          "THRESHOLD %+.1f dB   |   RANGE %+.1f dB   |   OUTPUT AT THRESHOLD %+.1f dB",
                          thresh, range, out_at_thr);
            lv_label_set_text(v->gate_curve_lbl, cf_buf);
        }
    }

    // 6. Update Brickwall Limiter vintage needle meters & digital readouts
    // (real per-slot gain reduction + rack I/O peak telemetry only)
    if (v->limiter_in_canvas || v->limiter_gr_canvas || v->limiter_in_lbl || v->limiter_out_lbl || v->limiter_gr_lbl) {
        const int slot_idx = std::clamp(v->selected_slot, 0, 7);
        const float gr_raw = ch->fx_slot_limiter_gr_db[static_cast<size_t>(slot_idx)];
        const bool measured = !(gr_raw >= LIMITER_GR_UNAVAILABLE);
        auto* lim_inst = ch->fx_rack.get_slot(v->selected_slot);
        const bool lim_bypassed = lim_inst && lim_inst->is_bypassed();
        const bool isStereo = (ch->format == ChannelFormat::Stereo);
        const float in_l = ch->fx_in_peak_l;
        const float in_r = isStereo ? ch->fx_in_peak_r : -90.0f;
        const float out_l = ch->fx_out_peak_l;
        const float out_r = isStereo ? ch->fx_out_peak_r : -90.0f;
        const float in_max = std::max(in_l, in_r);

        // Galvanometer ballistics: instant attack (peak), damped mechanical return
        const float in_target = measured ? std::clamp(in_max, -90.0f, 3.0f) : -90.0f;
        if (in_target > v->limiter_needle_in_db) {
            v->limiter_needle_in_db = in_target;
        } else {
            v->limiter_needle_in_db += (in_target - v->limiter_needle_in_db) * 0.22f;
        }
        if (std::abs(v->limiter_needle_in_db - in_target) < 0.05f) {
            v->limiter_needle_in_db = in_target;
        }

        const float gr_mag_target = measured ? std::clamp(-gr_raw, 0.0f, 20.0f) : 0.0f;
        if (gr_mag_target > v->limiter_needle_gr_db) {
            v->limiter_needle_gr_db += (gr_mag_target - v->limiter_needle_gr_db) * 0.50f;
        } else {
            v->limiter_needle_gr_db += (gr_mag_target - v->limiter_needle_gr_db) * 0.16f;
        }
        if (std::abs(v->limiter_needle_gr_db - gr_mag_target) < 0.03f) {
            v->limiter_needle_gr_db = gr_mag_target;
        }

        if (v->limiter_in_canvas) lv_obj_invalidate(v->limiter_in_canvas);
        if (v->limiter_gr_canvas) lv_obj_invalidate(v->limiter_gr_canvas);

        char lbuf[96];
        if (v->limiter_in_lbl) {
            if (!measured) {
                lv_label_set_text(v->limiter_in_lbl, "IN PEAK  -- dBFS");
            } else if (isStereo) {
                std::snprintf(lbuf, sizeof(lbuf), "IN PEAK  L %.1f  R %.1f dBFS", in_l, in_r);
                lv_label_set_text(v->limiter_in_lbl, lbuf);
            } else {
                std::snprintf(lbuf, sizeof(lbuf), "IN PEAK  %.1f dBFS", in_l);
                lv_label_set_text(v->limiter_in_lbl, lbuf);
            }
        }
        if (v->limiter_out_lbl) {
            if (!measured) {
                lv_label_set_text(v->limiter_out_lbl, "OUT PEAK  -- dBFS");
            } else if (isStereo) {
                std::snprintf(lbuf, sizeof(lbuf), "OUT PEAK  L %.1f  R %.1f dBFS", out_l, out_r);
                lv_label_set_text(v->limiter_out_lbl, lbuf);
            } else {
                std::snprintf(lbuf, sizeof(lbuf), "OUT PEAK  %.1f dBFS", out_l);
                lv_label_set_text(v->limiter_out_lbl, lbuf);
            }
        }
        if (v->limiter_gr_lbl) {
            if (!measured) {
                lv_label_set_text(v->limiter_gr_lbl, "GAIN REDUCTION  -- dB");
            } else if (lim_bypassed) {
                lv_label_set_text(v->limiter_gr_lbl, "GAIN REDUCTION  0.0 dB (BYPASS)");
            } else {
                std::snprintf(lbuf, sizeof(lbuf), "GAIN REDUCTION  %.1f dB", std::min(0.0f, gr_raw));
                lv_label_set_text(v->limiter_gr_lbl, lbuf);
            }
        }
    }
}

} // namespace

static FxScreenView* s_active_fx_view = nullptr;

lv_obj_t* fx_screen_create(void) {
    auto* s = lv_obj_create(lv_screen_active());
    lv_obj_set_pos(s, 0, 0);
    lv_obj_set_size(s, 1920, 1080);
    lv_obj_remove_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);

    ChannelState* ch_ptr = mixer_get_selected_channel();
    if (isMasterChannel(ch_ptr)) {
        auto* title = instrument_label(s, "FX UNAVAILABLE FOR MASTER", 0, 460, &ac_font_28, COLOR_TEXT);
        lv_obj_set_width(title, 1920);
        lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

        auto* sub = instrument_label(s, "Select an input channel or AUX to use FX.", 0, 506, &ac_font_16, COLOR_TEXT2);
        lv_obj_set_width(sub, 1920);
        lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);

        return s;
    }

    if (!ch_ptr) {
        ch_ptr = mixer_get_channel(0);
        if (!ch_ptr && !g_mixer_state.aux_buses.empty()) ch_ptr = &g_mixer_state.aux_buses[0];
    }

    auto* view = new FxScreenView{};
    view->ch = ch_ptr;
    if (ch_ptr && isAuxBusChannel(ch_ptr)) {
        view->target_mode = ch_ptr->id - 100;
    } else {
        view->target_mode = 0;
        view->target_channel_id = ch_ptr ? ch_ptr->id : 1;
    }
    view->selected_slot = 0;

    // Header Panel: Channel information, Rack status & Target Selector
    auto* header = lv_obj_create(s);
    lv_obj_set_pos(header, 16, 48);
    lv_obj_set_size(header, 1888, 44);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    ui::rack_skin::apply_chassis_panel(header, lv_color_hex(0x323A44));
    lv_obj_set_style_pad_all(header, 4, 0);
    ui::rack_skin::create_rack_screw(header, 4, 10);
    ui::rack_skin::create_rack_screw(header, 1866, 10);

    view->header_title_label = instrument_label(header, "", 24, 8, &ac_font_14, COLOR_CYAN);

    // Channel Prev / Next buttons for stepping through Input Strips
    view->prev_ch_btn = instrument_button(header, "<", 1030, 1, 36, 32, nullptr);
    view->next_ch_btn = instrument_button(header, ">", 1070, 1, 36, 32, nullptr);
    ui::rack_skin::apply_hardware_button(view->prev_ch_btn, false, COLOR_EDGE, COLOR_PANEL, lv_color_hex(0x1E242B));
    ui::rack_skin::apply_hardware_button(view->next_ch_btn, false, COLOR_EDGE, COLOR_PANEL, lv_color_hex(0x1E242B));

    lv_obj_add_event_cb(view->prev_ch_btn, [](lv_event_t* e) {
        auto* v = static_cast<FxScreenView*>(lv_event_get_user_data(e));
        if (!v || g_mixer_state.channels.empty()) return;
        v->target_mode = 0;
        g_mixer_state.selected_aux_bus = -1;
        int idx = std::max(0, g_mixer_state.selected_channel - 1);
        g_mixer_state.selected_channel = idx;
        if (auto* ch = mixer_get_channel(idx)) {
            v->target_channel_id = ch->id;
        }
        v->selected_slot = 0;
        schedule_ui_rebuild(v, true, true);
    }, LV_EVENT_CLICKED, view);

    lv_obj_add_event_cb(view->next_ch_btn, [](lv_event_t* e) {
        auto* v = static_cast<FxScreenView*>(lv_event_get_user_data(e));
        if (!v || g_mixer_state.channels.empty()) return;
        v->target_mode = 0;
        g_mixer_state.selected_aux_bus = -1;
        int idx = std::min(static_cast<int>(g_mixer_state.channels.size()) - 1,
                           std::max(0, g_mixer_state.selected_channel) + 1);
        g_mixer_state.selected_channel = idx;
        if (auto* ch = mixer_get_channel(idx)) {
            v->target_channel_id = ch->id;
        }
        v->selected_slot = 0;
        schedule_ui_rebuild(v, true, true);
    }, LV_EVENT_CLICKED, view);

    static const char* target_names[5] = {"INPUT CH", "AUX 01", "AUX 02", "AUX 03", "AUX 04"};
    for (int t = 0; t < 5; ++t) {
        view->target_btns[t] = instrument_button(header, target_names[t], 1114 + t * 148, 1, 142, 32, nullptr);
        struct TargetCtx { FxScreenView* v; int target; };
        auto* tc = new TargetCtx{view, t};
        lv_obj_add_event_cb(view->target_btns[t], [](lv_event_t* e) {
            auto* c = static_cast<TargetCtx*>(lv_event_get_user_data(e));
            if (!c || !c->v) return;
            FxScreenView* v = c->v;
            const int target = c->target;
            v->target_mode = target;
            if (target == 0) {
                g_mixer_state.selected_aux_bus = -1;
                if (g_mixer_state.selected_channel < 0 && !g_mixer_state.channels.empty()) {
                    g_mixer_state.selected_channel = 0;
                }
                if (auto* ch = mixer_get_channel(g_mixer_state.selected_channel)) {
                    v->target_channel_id = ch->id;
                }
            } else if (target >= 1 && target <= 4) {
                g_mixer_state.selected_aux_bus = target - 1;
            }
            v->selected_slot = 0;
            schedule_ui_rebuild(v, true, true);
        }, LV_EVENT_CLICKED, tc);
        lv_obj_add_event_cb(view->target_btns[t], [](lv_event_t* e) {
            delete static_cast<TargetCtx*>(lv_event_get_user_data(e));
        }, LV_EVENT_DELETE, tc);
    }

    // Left Panel: Insert Slots Rack Bay (646 x 860)
    view->rack_panel = instrument_panel(s, 16, 100, 646, 860, "EFFECT RACK BAY / SIGNAL FLOW");
    ui::rack_skin::apply_chassis_panel(view->rack_panel, lv_color_hex(0x323A44));
    ui::rack_skin::add_faceplate_corner_screws(view->rack_panel, 646, 860, 6);
    view->slot_container = lv_obj_create(view->rack_panel);
    lv_obj_set_pos(view->slot_container, 0, 36);
    lv_obj_set_size(view->slot_container, 644, 820);
    lv_obj_set_style_bg_opa(view->slot_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(view->slot_container, 0, 0);
    lv_obj_set_style_pad_all(view->slot_container, 0, 0);

    // Right Panel: Dedicated Rack Hardware Faceplate (1226 x 860)
    view->editor_panel = instrument_panel(s, 678, 100, 1226, 860, "DEDICATED RACK HARDWARE FACEPLATE");
    ui::rack_skin::apply_chassis_panel(view->editor_panel, lv_color_hex(0x323A44));
    ui::rack_skin::add_faceplate_corner_screws(view->editor_panel, 1226, 860, 6);
    view->editor_content = lv_obj_create(view->editor_panel);
    lv_obj_set_pos(view->editor_content, 0, 36);
    lv_obj_set_size(view->editor_content, 1224, 820);
    lv_obj_remove_flag(view->editor_content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(view->editor_content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(view->editor_content, 0, 0);
    lv_obj_set_style_pad_all(view->editor_content, 0, 0);

    build_rack(view);
    build_editor(view);
    view->last_param_fingerprint = compute_rack_fingerprint(view->ch);
    s_active_fx_view = view;
    view->live_timer = lv_timer_create(fx_live_timer_cb, 30, view);

    lv_obj_add_event_cb(s, [](lv_event_t* e) {
        auto* v = static_cast<FxScreenView*>(lv_event_get_user_data(e));
        if (v) {
            if (s_active_fx_view == v) s_active_fx_view = nullptr;
            lv_async_call_cancel(deferred_rebuild_cb, v);
            if (v->live_timer) {
                lv_timer_delete(v->live_timer);
                v->live_timer = nullptr;
            }
            if (v->add_modal) {
                lv_obj_t* modal = v->add_modal;
                v->add_modal = nullptr;
                lv_obj_delete(modal);
            }
            delete v;
        }
    }, LV_EVENT_DELETE, view);

    return s;
}

void fx_screen_show_add_modal(void) {
    if (s_active_fx_view) {
        show_add_plugin_modal(s_active_fx_view);
    }
}

void fx_screen_dismiss_modal(void) {
    if (s_active_fx_view && s_active_fx_view->add_modal) {
        lv_obj_t* modal = s_active_fx_view->add_modal;
        s_active_fx_view->add_modal = nullptr;
        lv_obj_delete(modal);
    }
}

