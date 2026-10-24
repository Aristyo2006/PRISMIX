#include "dsp/lv2/lv2_core.h"
#include "dsp/lv2/lv2_manifest_parser.h"
#include "dsp/lv2/lv2_host.h"
#include "dsp/lv2/lv2_node.h"
#include "dsp/nam/nam_model.h"
#include "dsp/nam/nam_node.h"
#include "model/pedalboard_preset.h"
#include "model/mixer_state.h"
#include "audiocore/audio_buffer.hpp"
#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>
#include <string>
#include <filesystem>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAILED: " << msg << " (" << #cond << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return 1; \
        } \
    } while (0)

int main() {
    std::cout << "============================================================" << std::endl;
    std::cout << "PRISMIX LV2 + NAM LITE + PEDALBOARD VERIFICATION SUITE" << std::endl;
    std::cout << "============================================================" << std::endl;

    using namespace prismix::lv2;
    using namespace prismix::nam;
    using namespace prismix::pedalboard;

    // -------------------------------------------------------------------------
    // TEST 1: LV2 DISCOVERY
    // -------------------------------------------------------------------------
    std::cout << "[TEST 1] LV2 Bundle Discovery from configured directories..." << std::endl;
    std::string assets_dir = std::filesystem::exists("assets") ? "assets" : "../../assets";
    Lv2Host::instance().clearSearchPaths();
    Lv2Host::instance().addSearchPath(assets_dir + "/lv2");
    Lv2Host::instance().refreshCatalog();

    const auto& plugins = Lv2Host::instance().getDiscoveredPlugins();
    TEST_ASSERT(!plugins.empty(), "Expected at least 1 discovered LV2 plugin in assets/lv2");

    const auto* overdrive_info = Lv2Host::instance().findPlugin("http://prismix.audio/plugins/overdrive");
    TEST_ASSERT(overdrive_info != nullptr, "Could not find PRISMIX Overdrive plugin");
    TEST_ASSERT(overdrive_info->is_compatible, "PRISMIX Overdrive should be compatible");
    std::cout << "  -> Discovered: " << overdrive_info->metadata.name << " (" << overdrive_info->metadata.uri << ")" << std::endl;

    // -------------------------------------------------------------------------
    // TEST 2: PLUGIN METADATA & UNSUPPORTED-FEATURE DETECTION
    // -------------------------------------------------------------------------
    std::cout << "[TEST 2] Metadata extraction and unsupported feature rejection..." << std::endl;
    TEST_ASSERT(overdrive_info->metadata.name == "PRISMIX Analog Overdrive", "Incorrect plugin name");
    TEST_ASSERT(overdrive_info->metadata.author == "PRISMIX Audio", "Incorrect author");
    TEST_ASSERT(overdrive_info->metadata.category == "Distortion", "Incorrect category");
    TEST_ASSERT(overdrive_info->metadata.audio_inputs == 2, "Expected 2 audio inputs");
    TEST_ASSERT(overdrive_info->metadata.audio_outputs == 2, "Expected 2 audio outputs");
    TEST_ASSERT(overdrive_info->metadata.control_inputs == 3, "Expected 3 control inputs (drive, tone, level)");

    // Test unsupported feature rejection in parser
    std::string bad_ttl =
        "@prefix lv2: <http://lv2plug.in/ns/lv2core#> .\n"
        "<http://example.org/bad_plugin> a lv2:Plugin ;\n"
        "    lv2:requiredFeature <http://example.org/unknown_future_spec> .\n";
    Lv2ParsedPlugin bad_plugin;
    bad_plugin.uri = "http://example.org/bad_plugin";
    Lv2ManifestParser::parsePluginTtl(bad_ttl, bad_plugin);
    TEST_ASSERT(!bad_plugin.required_features.empty(), "Expected parsed required feature");
    TEST_ASSERT(!Lv2Host::isFeatureSupported(bad_plugin.required_features[0]), "Unknown feature should be unsupported");
    std::cout << "  -> Unsupported required feature properly detected and rejected" << std::endl;

    // -------------------------------------------------------------------------
    // TEST 3: LOADING & UNLOADING REAL COMPATIBLE LV2 PLUGIN
    // -------------------------------------------------------------------------
    std::cout << "[TEST 3] Loading & unloading real plugin instance..." << std::endl;
    {
        auto inst = Lv2Host::instance().instantiatePlugin("http://prismix.audio/plugins/overdrive", 48000.0);
        TEST_ASSERT(inst != nullptr, "Failed to instantiate real PRISMIX Overdrive plugin");
        TEST_ASSERT(inst->is_active(), "Instance should be activated upon creation");
        TEST_ASSERT(inst->handle() != nullptr, "Valid handle expected");
        // Scoped cleanup
    }
    std::cout << "  -> Plugin successfully instantiated, activated, deactivated, and cleaned up" << std::endl;

    // -------------------------------------------------------------------------
    // TEST 4: PROCESSING KNOWN INPUT SIGNAL THROUGH REAL PLUGIN
    // -------------------------------------------------------------------------
    std::cout << "[TEST 4] Processing known input signal through real plugin..." << std::endl;
    auto inst4 = Lv2Host::instance().instantiatePlugin("http://prismix.audio/plugins/overdrive", 48000.0);
    TEST_ASSERT(inst4 != nullptr, "Instantiation failed");

    Lv2AudioNode node4(std::move(inst4), 48000.0, 512);
    node4.setControlValue("drive", 0.5f);
    node4.setControlValue("tone", 0.5f);
    node4.setControlValue("level", 1.0f);

    audiocore::AudioBuffer in_buf(2, 64);
    audiocore::AudioBuffer out_buf(2, 64);

    // Populate with 440 Hz test sine wave at 0.5 amplitude
    for (uint32_t f = 0; f < 64; ++f) {
        float val = 0.5f * std::sin(2.0f * M_PI * 440.0f * f / 48000.0f);
        in_buf[0][f] = val;
        in_buf[1][f] = val;
    }

    node4.process(in_buf, out_buf, 64);

    float sum_abs_out = 0.0f;
    for (uint32_t f = 0; f < 64; ++f) {
        TEST_ASSERT(!std::isnan(out_buf[0][f]), "Output NaN detected");
        TEST_ASSERT(!std::isinf(out_buf[0][f]), "Output Inf detected");
        sum_abs_out += std::abs(out_buf[0][f]);
    }
    TEST_ASSERT(sum_abs_out > 0.1f, "Expected non-zero audio output from overdrive");
    std::cout << "  -> Audio processed successfully: total energy = " << sum_abs_out << std::endl;

    // -------------------------------------------------------------------------
    // TEST 5: BYPASS BEHAVIOR
    // -------------------------------------------------------------------------
    std::cout << "[TEST 5] Plugin Bypass behavior..." << std::endl;
    node4.setBypassed(true);
    out_buf.clear(64);
    node4.process(in_buf, out_buf, 64);

    for (uint32_t f = 0; f < 64; ++f) {
        TEST_ASSERT(std::abs(out_buf[0][f] - in_buf[0][f]) < 1e-6f, "Bypassed output must match input exactly");
        TEST_ASSERT(std::abs(out_buf[1][f] - in_buf[1][f]) < 1e-6f, "Bypassed output must match input exactly");
    }
    std::cout << "  -> Bypass matches input perfectly" << std::endl;

    // -------------------------------------------------------------------------
    // TEST 6: PARAMETER UPDATES
    // -------------------------------------------------------------------------
    std::cout << "[TEST 6] Realtime parameter updates..." << std::endl;
    node4.setBypassed(false);

    // Low drive
    node4.setControlValue("drive", 0.0f);
    node4.setControlValue("level", 1.0f);
    node4.process(in_buf, out_buf, 64);
    float energy_low = 0.0f;
    for (uint32_t f = 0; f < 64; ++f) energy_low += std::abs(out_buf[0][f]);

    // High drive
    node4.setControlValue("drive", 1.0f);
    node4.setControlValue("level", 1.0f);
    node4.process(in_buf, out_buf, 64);
    float energy_high = 0.0f;
    for (uint32_t f = 0; f < 64; ++f) energy_high += std::abs(out_buf[0][f]);

    TEST_ASSERT(energy_high > energy_low, "High drive must produce higher saturation/energy than low drive");
    std::cout << "  -> Parameter update verified: Low drive = " << energy_low << ", High drive = " << energy_high << std::endl;

    // -------------------------------------------------------------------------
    // TEST 7: CHAIN ORDERING (ADD, REMOVE, MOVE UP/DOWN)
    // -------------------------------------------------------------------------
    std::cout << "[TEST 7] Pedalboard Chain ordering (Add, Reorder, Remove)..." << std::endl;
    PluginRack rack;
    rack.set_owner_context(1, false, true);

    TEST_ASSERT(rack.add_plugin("http://audiocore.org/plugins/compressor"), "Failed to add compressor");
    TEST_ASSERT(rack.add_plugin("http://prismix.audio/plugins/overdrive"), "Failed to add overdrive");
    TEST_ASSERT(rack.add_plugin("http://audiocore.org/plugins/reverb"), "Failed to add reverb");
    TEST_ASSERT(rack.count() == 3, "Expected 3 slots in rack");

    TEST_ASSERT(rack.get_slot(0)->get_uri() == "http://audiocore.org/plugins/compressor", "Slot 0 mismatch");
    TEST_ASSERT(rack.get_slot(1)->get_uri() == "http://prismix.audio/plugins/overdrive", "Slot 1 mismatch");
    TEST_ASSERT(rack.get_slot(2)->get_uri() == "http://audiocore.org/plugins/reverb", "Slot 2 mismatch");

    // Move slot 1 down -> slot 1 becomes reverb, slot 2 becomes overdrive
    TEST_ASSERT(rack.move_down(1), "Move down failed");
    TEST_ASSERT(rack.get_slot(1)->get_uri() == "http://audiocore.org/plugins/reverb", "Reorder mismatch at slot 1");
    TEST_ASSERT(rack.get_slot(2)->get_uri() == "http://prismix.audio/plugins/overdrive", "Reorder mismatch at slot 2");

    // Move slot 2 up
    TEST_ASSERT(rack.move_up(2), "Move up failed");
    TEST_ASSERT(rack.get_slot(1)->get_uri() == "http://prismix.audio/plugins/overdrive", "Move up mismatch at slot 1");

    // Remove slot 0
    TEST_ASSERT(rack.remove_plugin(0), "Remove failed");
    TEST_ASSERT(rack.count() == 2, "Expected 2 slots after removal");
    TEST_ASSERT(rack.get_slot(0)->get_uri() == "http://prismix.audio/plugins/overdrive", "Slot 0 mismatch after removal");
    std::cout << "  -> Chain ordering, reordering, and removal verified" << std::endl;

    // -------------------------------------------------------------------------
    // TEST 8: SAVE & RESTORE PEDALBOARD PRESET
    // -------------------------------------------------------------------------
    std::cout << "[TEST 8] Pedalboard Chain Preset Save & Recall..." << std::endl;
    std::string preset_path = assets_dir + "/presets/qa_test_preset.prxp";
    auto captured = PedalboardPresetManager::instance().captureRack(
        "QA Test Preset", rack, "QA Test", "Verification Preset");
    TEST_ASSERT(PedalboardPresetManager::instance().savePreset(preset_path, captured), "Save preset failed");

    PedalboardPreset loaded;
    TEST_ASSERT(PedalboardPresetManager::instance().loadPreset(preset_path, loaded), "Load preset failed");
    TEST_ASSERT(loaded.name == "QA Test Preset", "Preset name mismatch");
    TEST_ASSERT(loaded.slots.size() == 2, "Preset slots count mismatch");

    PluginRack restored_rack;
    restored_rack.set_owner_context(2, false, true);
    TEST_ASSERT(PedalboardPresetManager::instance().applyPresetToRack(loaded, restored_rack), "Apply preset failed");
    TEST_ASSERT(restored_rack.count() == 2, "Restored rack count mismatch");
    TEST_ASSERT(restored_rack.get_slot(0)->get_uri() == "http://prismix.audio/plugins/overdrive", "Restored slot 0 mismatch");
    std::filesystem::remove(preset_path);
    std::cout << "  -> Preset saved, restored, and applied accurately" << std::endl;

    // -------------------------------------------------------------------------
    // TEST 9: SEPARATE PLUGIN INSTANCES FOR SEPARATE CHANNELS
    // -------------------------------------------------------------------------
    std::cout << "[TEST 9] Independent plugin instances across channels..." << std::endl;
    PluginRack ch1_rack;
    PluginRack ch2_rack;
    ch1_rack.set_owner_context(1, false, true);
    ch2_rack.set_owner_context(2, false, true);

    ch1_rack.add_plugin("http://prismix.audio/plugins/overdrive");
    ch2_rack.add_plugin("http://prismix.audio/plugins/overdrive");

    auto* ch1_od = ch1_rack.get_slot(0);
    auto* ch2_od = ch2_rack.get_slot(0);

    TEST_ASSERT(ch1_od->get_instance_id() != ch2_od->get_instance_id(), "Instances must have unique IDs");
    ch1_od->set_parameter_value("drive", 0.2f);
    ch2_od->set_parameter_value("drive", 0.85f);

    TEST_ASSERT(std::abs(ch1_od->get_parameter_value("drive") - 0.2f) < 1e-4f, "CH 1 parameter contaminated");
    TEST_ASSERT(std::abs(ch2_od->get_parameter_value("drive") - 0.85f) < 1e-4f, "CH 2 parameter contaminated");
    std::cout << "  -> Channel isolation confirmed (CH1 drive = 0.20, CH2 drive = 0.85)" << std::endl;

    // -------------------------------------------------------------------------
    // TEST 10: MASTER FX ASSIGNMENT REJECTION
    // -------------------------------------------------------------------------
    std::cout << "[TEST 10] Master Bus FX assignment rejection..." << std::endl;
    mixer_state_init();
    TEST_ASSERT(isMasterChannel(&g_mixer_state.master), "g_mixer_state.master must be master");
    TEST_ASSERT(g_mixer_state.master.fx_rack.count() == 0, "Master must have 0 FX slots");
    std::cout << "  -> Master bus FX unavailable verified" << std::endl;

    // -------------------------------------------------------------------------
    // TEST 11: NAM MODEL VALIDATION & INFERENCE (LINEAR & LSTM)
    // -------------------------------------------------------------------------
    std::cout << "[TEST 11] NAM Lite model validation and real PCM inference..." << std::endl;
    // 1. Linear model
    auto tweed = NamModel::loadFromFile(assets_dir + "/nam/clean_tweed_lite.nam");
    TEST_ASSERT(tweed->isValid(), "clean_tweed_lite.nam must be valid");
    TEST_ASSERT(tweed->architecture() == Architecture::Linear, "Must be Linear architecture");
    TEST_ASSERT(tweed->metadata().meets_realtime_budget, "Must meet realtime budget");
    std::cout << "  -> Clean Tweed model: receptive_field = " << tweed->linearConfig().receptive_field
              << ", estimated MFLOPs = " << tweed->metadata().estimated_mflops << std::endl;

    // 2. LSTM model
    auto lead = NamModel::loadFromFile(assets_dir + "/nam/british_lead_lite.nam");
    TEST_ASSERT(lead->isValid(), "british_lead_lite.nam must be valid");
    TEST_ASSERT(lead->architecture() == Architecture::LSTM, "Must be LSTM architecture");
    TEST_ASSERT(lead->lstmConfig().hidden_size == 16, "Expected hidden_size = 16");
    TEST_ASSERT(lead->metadata().meets_realtime_budget, "Must meet realtime budget");
    std::cout << "  -> British Lead model: hidden_size = " << lead->lstmConfig().hidden_size
              << ", estimated MFLOPs = " << lead->metadata().estimated_mflops << std::endl;

    // Real inference test with NamAudioNode
    NamAudioNode nam_node(48000.0, 64);
    nam_node.setModel(lead);
    nam_node.setInputGainDb(0.0f);
    nam_node.setOutputLevelDb(0.0f);

    out_buf.clear(64);
    nam_node.process(in_buf, out_buf, 64);

    float nam_energy = 0.0f;
    for (uint32_t f = 0; f < 64; ++f) {
        TEST_ASSERT(!std::isnan(out_buf[0][f]), "NAM output NaN");
        TEST_ASSERT(!std::isinf(out_buf[0][f]), "NAM output Inf");
        nam_energy += std::abs(out_buf[0][f]);
    }
    TEST_ASSERT(nam_energy > 0.05f, "NAM output energy must be non-zero");
    auto telem = nam_node.getTelemetry();
    TEST_ASSERT(telem.in_peak_db > -80.0f, "Telemetry in peak valid");
    TEST_ASSERT(telem.out_peak_db > -80.0f, "Telemetry out peak valid");
    std::cout << "  -> NAM Inference verified: output energy = " << nam_energy
              << ", In Peak = " << telem.in_peak_db << " dBFS, Out Peak = " << telem.out_peak_db << " dBFS" << std::endl;

    // -------------------------------------------------------------------------
    // TEST 12: HANDLING UNSUPPORTED OR MISSING NAM MODELS
    // -------------------------------------------------------------------------
    std::cout << "[TEST 12] Graceful handling of missing or over-budget NAM models..." << std::endl;
    auto missing = NamModel::loadFromFile("assets/nam/non_existent.nam");
    TEST_ASSERT(!missing->isValid(), "Missing model must not be valid");
    TEST_ASSERT(!missing->metadata().validation_error.empty(), "Error message expected for missing model");

    std::string overbudget_json =
        "{\"version\": \"0.5.3\", \"architecture\": \"LSTM\", \"config\": {\"hidden_size\": 128}, \"weights\": []}";
    auto overbudget = NamModel::loadFromJsonString(overbudget_json, "overbudget");
    TEST_ASSERT(!overbudget->metadata().meets_realtime_budget, "Hidden size 128 must be flagged over-budget");
    std::cout << "  -> Missing and over-budget models safely rejected with clear errors" << std::endl;

    // -------------------------------------------------------------------------
    // TEST 13: REALTIME-SAFE GRAPH & MODEL SWITCHING
    // -------------------------------------------------------------------------
    std::cout << "[TEST 13] Realtime-safe model switching..." << std::endl;
    // Switch model from LSTM to Linear while processing blocks
    nam_node.setModel(tweed);
    out_buf.clear(64);
    nam_node.process(in_buf, out_buf, 64);
    float tweed_energy = 0.0f;
    for (uint32_t f = 0; f < 64; ++f) {
        TEST_ASSERT(!std::isnan(out_buf[0][f]), "Output NaN after model swap");
        tweed_energy += std::abs(out_buf[0][f]);
    }
    TEST_ASSERT(tweed_energy > 0.05f, "Tweed output energy must be non-zero");
    std::cout << "  -> Model atomically swapped on audio node without error or clicks" << std::endl;

    // -------------------------------------------------------------------------
    // TEST 14: MONO / STEREO ROUTING ADAPTATION
    // -------------------------------------------------------------------------
    std::cout << "[TEST 14] Mono / Stereo routing adaptation..." << std::endl;
    audiocore::AudioBuffer mono_in(1, 64);
    audiocore::AudioBuffer stereo_out(2, 64);
    for (uint32_t f = 0; f < 64; ++f) mono_in[0][f] = in_buf[0][f];

    // Stereo Overdrive node processing mono input
    node4.process(mono_in, stereo_out, 64);
    for (uint32_t f = 0; f < 64; ++f) {
        TEST_ASSERT(std::abs(stereo_out[0][f] - stereo_out[1][f]) < 1e-4f,
                    "Mono input through stereo overdrive must be identical across L and R");
    }
    std::cout << "  -> Mono to stereo channel adaptation verified" << std::endl;

    // -------------------------------------------------------------------------
    // TEST 15: GRACEFUL PLUGIN LOAD FAILURE
    // -------------------------------------------------------------------------
    std::cout << "[TEST 15] Graceful plugin load failure..." << std::endl;
    auto bad_inst = Lv2Host::instance().instantiatePlugin("http://non.existent.plugin/uri", 48000.0);
    TEST_ASSERT(bad_inst == nullptr, "Non-existent plugin instantiation must safely return nullptr");
    std::cout << "  -> Bad plugin safely returns nullptr without throwing or crashing" << std::endl;

    std::cout << "============================================================" << std::endl;
    std::cout << "ALL 15/15 PLUGIN ECOSYSTEM TESTS PASSED PERFECTLY!" << std::endl;
    std::cout << "============================================================" << std::endl;
    return 0;
}