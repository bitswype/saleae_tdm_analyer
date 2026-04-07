cmake_minimum_required (VERSION 3.13)
project(tdm_analyzer)

add_definitions( -DLOGIC2 )

# enable generation of compile_commands.json, helpful for IDEs to locate include files.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# custom CMake Modules are located in the cmake directory.
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

include(ExternalAnalyzerSDK)

set(SOURCES 
src/TdmAnalyzer.cpp
src/TdmAnalyzer.h
src/TdmAnalyzerResults.cpp
src/TdmAnalyzerResults.h
src/TdmAnalyzerSettings.cpp
src/TdmAnalyzerSettings.h
src/TdmSimulationDataGenerator.cpp
src/TdmSimulationDataGenerator.h
)

add_analyzer_plugin(tdm_analyzer SOURCES ${SOURCES})

option(BUILD_TESTS "Build test harness and benchmarks" OFF)
option(ENABLE_PROFILING "Enable compile-time performance instrumentation" OFF)
option(ENABLE_BENCHMARK_TIMING "Write decode timing to %USERPROFILE%\\tdm_benchmark_timing.json on analyzer destruction" OFF)
option(DISABLE_FAST_BITS "Disable GetNextDataBit optimization (for A/B benchmarking)" OFF)

if(ENABLE_BENCHMARK_TIMING)
    target_compile_definitions(tdm_analyzer PRIVATE TDM_BENCHMARK_TIMING)
endif()
if(DISABLE_FAST_BITS)
    target_compile_definitions(tdm_analyzer PRIVATE TDM_DISABLE_FAST_BITS)
endif()
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
