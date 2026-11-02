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

# Glob sources (app, ui, model, backend, dsp)
file(GLOB_RECURSE AUDIOCORE_SOURCES CONFIGURE_DEPENDS
    "src/*.cpp"
    "src/*.c"
)
list(FILTER AUDIOCORE_SOURCES EXCLUDE REGEX "/src/service/audio_service\\.cpp$")

add_executable(AudioCore ${AUDIOCORE_SOURCES})

# Include directories
target_include_directories(AudioCore PRIVATE "src")
target_include_directories(AudioCore PRIVATE "config")

# Link LVGL
target_link_libraries(AudioCore PRIVATE lvgl)
target_link_libraries(AudioCore PRIVATE SDL2::SDL2 SDL2::SDL2main)
if(WIN32)
    target_link_libraries(AudioCore PRIVATE winhttp)
endif()

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
add_library(audiocore_ui_service src/service/audio_service.cpp)
target_include_directories(audiocore_ui_service PUBLIC "src")
target_link_libraries(audiocore_ui_service PUBLIC audiocore)
target_compile_features(audiocore_ui_service PUBLIC cxx_std_20)
target_link_libraries(AudioCore PRIVATE audiocore_ui_service)
add_subdirectory(tests/audio)
add_subdirectory(tools/dsp_test_bench)
