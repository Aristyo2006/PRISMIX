# AudioCore CMakeLists.txt
cmake_minimum_required(VERSION 3.20)
project(AudioCore VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8 /W3)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# Module path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(MSVC)
    # Fetch SDL2 prebuilt for MSVC
    include(FetchSDL2)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
    if(NOT TARGET SDL2::SDL2)
        add_library(SDL2::SDL2 ALIAS PkgConfig::SDL2)
    endif()
    if(NOT TARGET SDL2::SDL2main)
        add_library(SDL2::SDL2main INTERFACE IMPORTED)
    endif()
endif()

# Configure LVGL options before add_subdirectory
set(LV_BUILD_CONF_DIR "${CMAKE_CURRENT_SOURCE_DIR}/config" CACHE STRING "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)

# Add LVGL
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/lvgl/CMakeLists.txt")
    add_subdirectory(lib/lvgl)
else()
    include(FetchContent)
    FetchContent_Declare(
        lvgl
        GIT_REPOSITORY https://github.com/lvgl/lvgl.git
        GIT_TAG c016f72d4c125098287be5e83c0f1abed4706ee5
    )
    FetchContent_MakeAvailable(lvgl)
endif()

# We need lvgl target to link against SDL2
target_link_libraries(lvgl PUBLIC SDL2::SDL2)
target_include_directories(lvgl PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/config")

# Find ZLIB for archive decompression and validation
find_package(ZLIB REQUIRED)

# PIN hashing uses libxcrypt's memory-hard yescrypt through the platform's
# crypt(3) implementation. No home-grown cryptography is used.
pkg_check_modules(XCRYPT REQUIRED IMPORTED_TARGET libxcrypt)
find_package(Threads REQUIRED)

# Glob sources (app, ui, model, backend, dsp)
file(GLOB_RECURSE AUDIOCORE_SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
    "src/*.c"
)
list(FILTER AUDIOCORE_SOURCES EXCLUDE REGEX "/src/service/audio_service\\.cpp$")
list(FILTER AUDIOCORE_SOURCES EXCLUDE REGEX "/src/service/patchstorage/.*\\.cpp$")
list(FILTER AUDIOCORE_SOURCES EXCLUDE REGEX "/src/service/web/.*\\.cpp$")
list(FILTER AUDIOCORE_SOURCES EXCLUDE REGEX "/src/service/security/.*\\.cpp$")
list(FILTER AUDIOCORE_SOURCES EXCLUDE REGEX "/src/service/system/.*\\.cpp$")
list(FILTER AUDIOCORE_SOURCES EXCLUDE REGEX "/src/dsp/lv2/.*\\.cpp$")
list(FILTER AUDIOCORE_SOURCES EXCLUDE REGEX "/src/dsp/nam/.*\\.cpp$")
list(FILTER AUDIOCORE_SOURCES EXCLUDE REGEX "/src/model/pedalboard_preset\\.cpp$")

add_executable(AudioCore ${AUDIOCORE_SOURCES})

# Include directories
target_include_directories(AudioCore PRIVATE "src")
target_include_directories(AudioCore PRIVATE "config")

# Link LVGL
target_link_libraries(AudioCore PRIVATE lvgl)
target_link_libraries(AudioCore PRIVATE SDL2::SDL2 SDL2::SDL2main)

# Subsystem Windows for GUI (so we don't get a console window in Release)
if(MSVC)
    set_target_properties(AudioCore PROPERTIES WIN32_EXECUTABLE ON)
endif()

# Copy SDL2.dll to output directory after build (Windows only)
if(WIN32 AND DEFINED SDL2_DLL_PATH)
    add_custom_command(TARGET AudioCore POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SDL2_DLL_PATH}"
        "$<TARGET_FILE_DIR:AudioCore>/"
    )
endif()

# ==============================================================================
# AUDIO CORE ENGINE MODULE & TESTS
# ==============================================================================
enable_testing()
add_subdirectory(audio)

add_library(prismix_plugin_host STATIC
    src/dsp/lv2/lv2_manifest_parser.cpp
    src/dsp/lv2/lv2_host.cpp
    src/dsp/lv2/lv2_node.cpp
    src/dsp/nam/nam_model.cpp
    src/dsp/nam/nam_node.cpp
    src/model/pedalboard_preset.cpp
)
target_include_directories(prismix_plugin_host PUBLIC "src")
target_link_libraries(prismix_plugin_host PUBLIC audiocore ${CMAKE_DL_LIBS})
target_compile_features(prismix_plugin_host PUBLIC cxx_std_20)

add_library(prismix_patchstorage STATIC
    src/service/patchstorage/patchstorage_json.cpp
    src/service/patchstorage/patchstorage_client.cpp
    src/service/patchstorage/lv2_installer.cpp
    src/service/patchstorage/patchstorage_service.cpp
    src/service/patchstorage/patchstorage_artwork.cpp
)
target_include_directories(prismix_patchstorage PUBLIC "src")
target_link_libraries(prismix_patchstorage PUBLIC audiocore prismix_plugin_host ZLIB::ZLIB ${CMAKE_DL_LIBS})
target_compile_features(prismix_patchstorage PUBLIC cxx_std_20)

add_library(audiocore_ui_service src/service/audio_service.cpp)
target_include_directories(audiocore_ui_service PUBLIC "src")
target_link_libraries(audiocore_ui_service PUBLIC audiocore prismix_plugin_host prismix_patchstorage ${CMAKE_DL_LIBS})
target_compile_features(audiocore_ui_service PUBLIC cxx_std_20)

add_library(prismix_security STATIC
    src/service/security/security_service.cpp
)
target_include_directories(prismix_security PUBLIC "src")
target_link_libraries(prismix_security PUBLIC PkgConfig::XCRYPT Threads::Threads)
target_compile_features(prismix_security PUBLIC cxx_std_20)

add_library(prismix_system STATIC
    src/service/system/system_service.cpp
)
target_include_directories(prismix_system PUBLIC "src")
target_link_libraries(prismix_system PUBLIC prismix_security Threads::Threads)
target_compile_features(prismix_system PUBLIC cxx_std_20)

add_library(prismix_web_service STATIC
    src/service/web/web_control_bridge.cpp
    src/service/web/web_server.cpp
    src/service/web/web_assets.cpp
)
target_include_directories(prismix_web_service PUBLIC "src")
target_link_libraries(prismix_web_service PUBLIC audiocore prismix_plugin_host prismix_patchstorage audiocore_ui_service prismix_security prismix_system ZLIB::ZLIB ${CMAKE_DL_LIBS})
if(WIN32)
    target_link_libraries(prismix_web_service PUBLIC ws2_32)
endif()
target_compile_features(prismix_web_service PUBLIC cxx_std_20)

target_link_libraries(AudioCore PRIVATE audiocore_ui_service prismix_plugin_host prismix_patchstorage prismix_web_service prismix_security prismix_system ZLIB::ZLIB ${CMAKE_DL_LIBS})
if(WIN32)
    target_link_libraries(AudioCore PRIVATE ws2_32)
endif()

# Build in-tree LV2 test plugin
add_library(prismix_overdrive MODULE assets/lv2/prismix_overdrive.lv2/prismix_overdrive.cpp)
set_target_properties(prismix_overdrive PROPERTIES
    PREFIX ""
    OUTPUT_NAME "prismix_overdrive"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/assets/lv2/prismix_overdrive.lv2"
)
target_include_directories(prismix_overdrive PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/lv2")
target_compile_features(prismix_overdrive PRIVATE cxx_std_17)

if(UNIX)
    add_custom_command(TARGET AudioCore POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink
            $<TARGET_FILE_NAME:AudioCore>
            $<TARGET_FILE_DIR:AudioCore>/prismix
        COMMENT "Generating prismix symlink for AudioCore"
    )
endif()
add_subdirectory(tests/audio)
add_subdirectory(tools/dsp_test_bench)
