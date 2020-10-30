cmake_minimum_required (VERSION 3.11)
project(i2s_analyzer)

add_definitions( -DLOGIC2 )

# enable generation of compile_commands.json, helpful for IDEs to locate include files.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# custom CMake Modules are located in the cmake directory.
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

include(ExternalAnalyzerSDK)

set(SOURCES 
src/I2sAnalyzer.cpp
src/I2sAnalyzer.h
src/I2sAnalyzerResults.cpp
src/I2sAnalyzerResults.h
src/I2sAnalyzerSettings.cpp
src/I2sAnalyzerSettings.h
src/I2sSimulationDataGenerator.cpp
src/I2sSimulationDataGenerator.h
)

add_analyzer_plugin(i2s_analyzer SOURCES ${SOURCES})
