# AUDIO CORE DSP TEST BENCH CMakeLists.txt
cmake_minimum_required(VERSION 3.20)

add_executable(dsp_test_bench
    wav_io.cpp
    synthetic_signals.cpp
    benchmark_metrics.cpp
    profiler.cpp
    correctness_tests.cpp
    report_exporter.cpp
    self_test.cpp
    cpu_telemetry.cpp
    cpu_workload_benchmark.cpp
    mixer_control_state.cpp
    test_bench_ui.cpp
    main.cpp
)

target_include_directories(dsp_test_bench PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/audio/include
)

target_link_libraries(dsp_test_bench PRIVATE audiocore)
target_compile_features(dsp_test_bench PRIVATE cxx_std_17)

if(WIN32)
    target_link_libraries(dsp_test_bench PRIVATE
        comdlg32
        user32
        gdi32
        comctl32
    )
endif()

# Register automated self-test and correctness suite in CTest
add_test(NAME DspTestBenchSelfTest COMMAND dsp_test_bench --self-test)
add_test(NAME DspTestBenchCorrectness COMMAND dsp_test_bench --correctness)

