# AudioCore Engine CMakeLists.txt
cmake_minimum_required(VERSION 3.20)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    project(AudioCoreEngine VERSION 1.0.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    if(MSVC)
        add_compile_options(/utf-8 /W3)
        add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    endif()
endif()

add_library(audiocore
    src/audio_engine.cpp
    src/audio_buffer.cpp
    src/audio_graph.cpp
    src/gain_node.cpp
    src/parameter.cpp
    src/parameter_smoother.cpp
    src/pan_node.cpp
    src/mixer_node.cpp
    src/biquad_filter.cpp
    src/eq_node.cpp
    src/compressor_node.cpp
    src/limiter_node.cpp
    src/meter_node.cpp
    src/sample_rate_converter.cpp
    src/gate_node.cpp
    src/geq_node.cpp
    src/delay_node.cpp
    src/reverb_node.cpp
    src/afs_node.cpp
    src/alignment_delay_node.cpp
    src/signal_generator_node.cpp
)

if(WIN32)
    target_sources(audiocore PRIVATE
        src/backend/wasapi_backend.cpp
    )
    target_link_libraries(audiocore PUBLIC
        ole32
        avrt
        uuid
    )
else()
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(JACK QUIET jack)
    endif()

    if(JACK_FOUND)
        target_sources(audiocore PRIVATE
            src/backend/jack_backend.cpp
        )
        target_include_directories(audiocore PUBLIC ${JACK_INCLUDE_DIRS})
        target_link_libraries(audiocore PUBLIC ${JACK_LIBRARIES})
        target_compile_definitions(audiocore PUBLIC AUDIOCORE_HAS_JACK=1)
    else()
        find_library(JACK_LIB jack)
        if(JACK_LIB)
            target_sources(audiocore PRIVATE
                src/backend/jack_backend.cpp
            )
            target_link_libraries(audiocore PUBLIC ${JACK_LIB})
            target_compile_definitions(audiocore PUBLIC AUDIOCORE_HAS_JACK=1)
        endif()
    endif()
endif()

target_include_directories(audiocore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(audiocore
    PUBLIC cxx_std_17
)

if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    # If built standalone, build tests here
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../tests/audio ${CMAKE_CURRENT_BINARY_DIR}/tests)
endif()

