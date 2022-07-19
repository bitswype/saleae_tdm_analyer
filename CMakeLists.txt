cmake_minimum_required (VERSION 3.11)
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
