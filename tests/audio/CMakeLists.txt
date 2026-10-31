# Tests for AudioCore Engine
cmake_minimum_required(VERSION 3.20)

enable_testing()

add_executable(test_audio test_audio.cpp)
target_link_libraries(test_audio PRIVATE audiocore)
target_compile_features(test_audio PRIVATE cxx_std_17)

add_executable(test_signal test_signal.cpp)
target_link_libraries(test_signal PRIVATE audiocore)
target_compile_features(test_signal PRIVATE cxx_std_17)

add_executable(test_dsp test_dsp.cpp)
target_link_libraries(test_dsp PRIVATE audiocore)
target_compile_features(test_dsp PRIVATE cxx_std_17)

add_executable(test_src test_src.cpp)
target_link_libraries(test_src PRIVATE audiocore)
target_compile_features(test_src PRIVATE cxx_std_17)

add_executable(test_gate test_gate.cpp)
target_link_libraries(test_gate PRIVATE audiocore)
target_compile_features(test_gate PRIVATE cxx_std_17)

add_executable(test_geq test_geq.cpp)
target_link_libraries(test_geq PRIVATE audiocore)
target_compile_features(test_geq PRIVATE cxx_std_17)

add_executable(test_delay test_delay.cpp)
target_link_libraries(test_delay PRIVATE audiocore)
target_compile_features(test_delay PRIVATE cxx_std_17)

add_executable(test_reverb test_reverb.cpp)
target_link_libraries(test_reverb PRIVATE audiocore)
target_compile_features(test_reverb PRIVATE cxx_std_17)

add_executable(test_afs test_afs.cpp)
target_link_libraries(test_afs PRIVATE audiocore)
target_compile_features(test_afs PRIVATE cxx_std_17)

add_executable(test_alignment test_alignment.cpp)
target_link_libraries(test_alignment PRIVATE audiocore)
target_compile_features(test_alignment PRIVATE cxx_std_17)

add_executable(test_reverb_templates test_reverb_templates.cpp)
target_link_libraries(test_reverb_templates PRIVATE audiocore)
target_compile_features(test_reverb_templates PRIVATE cxx_std_17)

add_executable(test_dynamics_telemetry test_dynamics_telemetry.cpp)
target_link_libraries(test_dynamics_telemetry PRIVATE audiocore)
target_compile_features(test_dynamics_telemetry PRIVATE cxx_std_17)

add_test(NAME CoreAudioTests COMMAND test_audio)
add_test(NAME SignalPipelineTests COMMAND test_signal)
add_test(NAME DspCoreTests COMMAND test_dsp)
add_test(NAME DynamicsTelemetryTests COMMAND test_dynamics_telemetry)
add_test(NAME SampleRateConverterTests COMMAND test_src)
add_test(NAME GateCoreTests COMMAND test_gate)
add_test(NAME GeqCoreTests COMMAND test_geq)
add_test(NAME DelayCoreTests COMMAND test_delay)
add_test(NAME ReverbCoreTests COMMAND test_reverb)
add_test(NAME AfsCoreTests COMMAND test_afs)
add_test(NAME AlignmentCoreTests COMMAND test_alignment)
add_test(NAME ReverbTemplateTests COMMAND test_reverb_templates)

add_executable(test_security_system test_security_system.cpp)
target_include_directories(test_security_system PRIVATE ../../src)
target_link_libraries(test_security_system PRIVATE prismix_system Threads::Threads)
target_compile_features(test_security_system PRIVATE cxx_std_20)
add_test(NAME SecuritySystemAuthorityTests COMMAND test_security_system)

if(WIN32)
    add_executable(test_wasapi test_wasapi.cpp)
    target_link_libraries(test_wasapi PRIVATE audiocore)
    target_compile_features(test_wasapi PRIVATE cxx_std_17)

    add_executable(test_wasapi_live test_wasapi_live.cpp)
    target_link_libraries(test_wasapi_live PRIVATE audiocore)
    target_compile_features(test_wasapi_live PRIVATE cxx_std_17)

    add_executable(test_wasapi_debug test_wasapi_debug.cpp)
    target_link_libraries(test_wasapi_debug PRIVATE audiocore)
    target_compile_features(test_wasapi_debug PRIVATE cxx_std_17)

    add_executable(test_wasapi_device test_wasapi_device.cpp)
    target_link_libraries(test_wasapi_device PRIVATE audiocore)
    target_compile_features(test_wasapi_device PRIVATE cxx_std_17)

    add_executable(test_wasapi_loopback test_wasapi_loopback.cpp)
    target_link_libraries(test_wasapi_loopback PRIVATE audiocore)
    target_compile_features(test_wasapi_loopback PRIVATE cxx_std_17)

    add_test(NAME WasapiBackendTests COMMAND test_wasapi)
    add_test(NAME WasapiDeviceTests COMMAND test_wasapi_device)
    add_test(NAME WasapiLoopbackTests COMMAND test_wasapi_loopback)
endif()

if(NOT WIN32)
    add_executable(test_jack test_jack.cpp)
    target_link_libraries(test_jack PRIVATE audiocore)
    target_compile_features(test_jack PRIVATE cxx_std_17)
    add_test(NAME JackBackendTests COMMAND test_jack)
endif()

if(TARGET audiocore_ui_service)
    add_executable(test_audio_service test_audio_service.cpp)
    target_link_libraries(test_audio_service PRIVATE audiocore_ui_service)
    target_compile_features(test_audio_service PRIVATE cxx_std_20)
    add_test(NAME AudioServiceControlBridgeTests COMMAND test_audio_service)

    add_executable(test_rta_analyzer
        test_rta_analyzer.cpp
        ../../src/dsp/analyzer_model.cpp)
    target_include_directories(test_rta_analyzer PRIVATE ../../src)
    target_link_libraries(test_rta_analyzer PRIVATE audiocore_ui_service)
    target_compile_features(test_rta_analyzer PRIVATE cxx_std_20)
    add_test(NAME RtaAnalyzerTests COMMAND test_rta_analyzer)
endif()

add_executable(test_audio_setup_config
    test_audio_setup_config.cpp
    ../../src/model/audio_setup_config.cpp)
target_include_directories(test_audio_setup_config PRIVATE ../../src)
target_compile_features(test_audio_setup_config PRIVATE cxx_std_20)
add_test(NAME AudioSetupConfigurationTests COMMAND test_audio_setup_config)

add_executable(test_mixer_scene
    test_mixer_scene.cpp
    ../../src/model/mixer_scene.cpp
    ../../src/model/mixer_state.cpp
    ../../src/model/plugin_model.cpp
    ../../src/model/routing_state.cpp
    ../../src/dsp/geq_model.cpp)
target_include_directories(test_mixer_scene PRIVATE ../../src)
target_link_libraries(test_mixer_scene PRIVATE prismix_plugin_host ${CMAKE_DL_LIBS})
target_compile_features(test_mixer_scene PRIVATE cxx_std_20)
add_test(NAME MixerScenePersistenceTests COMMAND test_mixer_scene)

add_executable(test_lv2_nam_pedalboard
    test_lv2_nam_pedalboard.cpp
    ../../src/model/mixer_state.cpp
    ../../src/model/plugin_model.cpp
    ../../src/model/routing_state.cpp
    ../../src/dsp/geq_model.cpp)
target_include_directories(test_lv2_nam_pedalboard PRIVATE ../../src)
target_link_libraries(test_lv2_nam_pedalboard PRIVATE prismix_plugin_host audiocore ${CMAKE_DL_LIBS})
target_compile_features(test_lv2_nam_pedalboard PRIVATE cxx_std_20)
add_test(NAME Lv2NamPedalboardTests COMMAND test_lv2_nam_pedalboard)
set_tests_properties(Lv2NamPedalboardTests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_executable(test_ui_theme_config
    test_ui_theme_config.cpp
    ../../src/ui/theme/theme_model.cpp)
target_include_directories(test_ui_theme_config PRIVATE ../../src)
target_compile_features(test_ui_theme_config PRIVATE cxx_std_20)
add_test(NAME UiThemeConfigurationTests COMMAND test_ui_theme_config)

add_executable(test_patchstorage
    test_patchstorage.cpp
    ../../src/model/mixer_state.cpp
    ../../src/model/plugin_model.cpp
    ../../src/model/routing_state.cpp
    ../../src/dsp/geq_model.cpp)
target_include_directories(test_patchstorage PRIVATE ../../src)
target_link_libraries(test_patchstorage PRIVATE prismix_patchstorage prismix_plugin_host audiocore ${CMAKE_DL_LIBS} ZLIB::ZLIB)
target_compile_features(test_patchstorage PRIVATE cxx_std_20)
add_test(NAME PatchStorageCatalogAndInstallTests COMMAND test_patchstorage)
set_tests_properties(PatchStorageCatalogAndInstallTests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

add_executable(test_web_control
    test_web_control.cpp
    ../../src/model/mixer_state.cpp
    ../../src/model/mixer_scene.cpp
    ../../src/model/plugin_model.cpp
    ../../src/model/routing_state.cpp
    ../../src/dsp/geq_model.cpp
    ../../src/ui/ui_audio_bridge.cpp
    ../../src/ui/theme/theme_model.cpp
    ../../src/backend/mock_audio_backend.cpp
    ../../src/dsp/eq_model.cpp
    ../../src/dsp/compressor_model.cpp
    ../../src/dsp/gate_model.cpp
    ../../src/dsp/analyzer_model.cpp
    ../../src/model/audio_setup_config.cpp)
target_include_directories(test_web_control PRIVATE ../../src)
target_link_libraries(test_web_control PRIVATE prismix_web_service prismix_patchstorage prismix_plugin_host audiocore_ui_service audiocore ZLIB::ZLIB ${CMAKE_DL_LIBS})
if(WIN32)
    target_link_libraries(test_web_control PRIVATE ws2_32)
endif()
target_compile_features(test_web_control PRIVATE cxx_std_20)
add_test(NAME WebControlBridgeTests COMMAND test_web_control)
set_tests_properties(WebControlBridgeTests PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

