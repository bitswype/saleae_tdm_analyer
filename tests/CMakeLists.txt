# Test harness and benchmarks for the TDM Low Level Analyzer.
#
# Links analyzer sources against the SDK testlib (mock implementations)
# instead of the real SDK shared library.  This allows the analyzer's
# WorkerThread to run standalone without Logic 2.

set(SDK_INCLUDE_DIR ${analyzersdk_SOURCE_DIR}/include)
set(TESTLIB_DIR     ${analyzersdk_SOURCE_DIR}/testlib)

# SDK testlib: mock implementations of Analyzer, AnalyzerChannelData,
# AnalyzerResults, AnalyzerSettings, etc.
set(TESTLIB_SOURCES
    ${TESTLIB_DIR}/AnalyzerStubs.cpp
    ${TESTLIB_DIR}/HelperStubs.cpp
    ${TESTLIB_DIR}/MockChannelData.cpp
    ${TESTLIB_DIR}/MockResults.cpp
    ${TESTLIB_DIR}/MockSimulatedChannelDescriptor.cpp
    ${TESTLIB_DIR}/SettingsStubs.cpp
    ${TESTLIB_DIR}/StreamHelpers.cpp
    ${TESTLIB_DIR}/TestInstance.cpp
)

# TDM analyzer sources (compiled against testlib stubs, not the SDK .so)
set(ANALYZER_SOURCES
    ${PROJECT_SOURCE_DIR}/src/TdmAnalyzer.cpp
    ${PROJECT_SOURCE_DIR}/src/TdmAnalyzerResults.cpp
    ${PROJECT_SOURCE_DIR}/src/TdmAnalyzerSettings.cpp
    ${PROJECT_SOURCE_DIR}/src/TdmSimulationDataGenerator.cpp
)

add_executable(tdm_benchmark
    tdm_benchmark.cpp
    extra_stubs.cpp
    ${TESTLIB_SOURCES}
    ${ANALYZER_SOURCES}
)

target_include_directories(tdm_benchmark PRIVATE
    ${SDK_INCLUDE_DIR}
    ${TESTLIB_DIR}
    ${PROJECT_SOURCE_DIR}/src
)

target_compile_definitions(tdm_benchmark PRIVATE LOGIC2)

add_executable(tdm_correctness
    tdm_correctness.cpp
    extra_stubs.cpp
    ${TESTLIB_SOURCES}
    ${ANALYZER_SOURCES}
)

target_include_directories(tdm_correctness PRIVATE
    ${SDK_INCLUDE_DIR}
    ${TESTLIB_DIR}
    ${PROJECT_SOURCE_DIR}/src
)

target_compile_definitions(tdm_correctness PRIVATE LOGIC2)

# The SDK testlib's SettingsStubs.cpp uses std::runtime_error without
# including <stdexcept>.  GCC 13+ no longer transitively provides it.
# Force-include the header to work around the upstream bug.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set_source_files_properties(${TESTLIB_DIR}/SettingsStubs.cpp
        PROPERTIES COMPILE_FLAGS "-include stdexcept")
elseif(MSVC)
    set_source_files_properties(${TESTLIB_DIR}/SettingsStubs.cpp
        PROPERTIES COMPILE_FLAGS "/FI stdexcept")
endif()
