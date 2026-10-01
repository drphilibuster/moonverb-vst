cmake_minimum_required(VERSION 3.22)
project(MoonVerb VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# JUCE (AGPLv3 / commercial). Fetched at configure time, pinned. Point MOONVERB_JUCE_DIR at a local checkout to build offline.
set(MOONVERB_JUCE_DIR "" CACHE PATH "Use this JUCE checkout instead of fetching one")
if(MOONVERB_JUCE_DIR)
  add_subdirectory(${MOONVERB_JUCE_DIR} juce EXCLUDE_FROM_ALL)
else()
  include(FetchContent)
  FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 9.0.3 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(juce)
endif()

# The Z80 core is plain C.
add_library(moonverb_z80 STATIC src/vendor/z80/z80.c)
target_include_directories(moonverb_z80 PUBLIC src/vendor/z80)
set_target_properties(moonverb_z80 PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(APPLE)
  set(MOONVERB_FORMATS VST3 AU Standalone)
else()
  set(MOONVERB_FORMATS VST3 Standalone)
endif()

juce_add_plugin(MoonVerb
  COMPANY_NAME "Moon Technologies"
  BUNDLE_ID com.moontechnologies.moonverb
  PLUGIN_MANUFACTURER_CODE Mnte
  PLUGIN_CODE Mvrb
  FORMATS ${MOONVERB_FORMATS}
  PRODUCT_NAME "MoonVerb"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  MICROPHONE_PERMISSION_ENABLED TRUE
  MICROPHONE_PERMISSION_TEXT "MoonVerb's standalone app processes the audio input."
  VST3_CATEGORIES Fx Reverb
  AU_MAIN_TYPE kAudioUnitType_MusicEffect
  COPY_PLUGIN_AFTER_BUILD FALSE
  VERSION ${PROJECT_VERSION})

set(MOONVERB_SOURCES
  src/PluginProcessor.cpp src/PluginProcessor.h
  src/PluginEditor.cpp src/PluginEditor.h
  src/ui/FrontPanel.h src/ui/FrontPanel.cpp)

target_sources(MoonVerb PRIVATE ${MOONVERB_SOURCES})
target_include_directories(MoonVerb PRIVATE src src/core)
target_compile_definitions(MoonVerb PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(MoonVerb
  PRIVATE moonverb_z80 juce::juce_audio_utils juce::juce_audio_processors
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)

# Headless check of the whole plugin against your own ROMs (see tests/README): `cmake --build build --target moonverb_plugin_test`
juce_add_console_app(moonverb_plugin_test PRODUCT_NAME moonverb_plugin_test)
target_sources(moonverb_plugin_test PRIVATE tests/plugin_test.cpp ${MOONVERB_SOURCES})
target_include_directories(moonverb_plugin_test PRIVATE src src/core)
target_compile_definitions(moonverb_plugin_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_WINRT_MIDI=0)
target_link_libraries(moonverb_plugin_test PRIVATE moonverb_z80 juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
set_target_properties(moonverb_plugin_test PROPERTIES EXCLUDE_FROM_ALL TRUE)

# Loads the *installed* VST3 / AU the way a DAW does: `cmake --build build --target moonverb_host_test`
juce_add_console_app(moonverb_host_test PRODUCT_NAME moonverb_host_test)
target_sources(moonverb_host_test PRIVATE tests/host_test.cpp)
target_compile_definitions(moonverb_host_test PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=1)
target_link_libraries(moonverb_host_test PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
set_target_properties(moonverb_host_test PROPERTIES EXCLUDE_FROM_ALL TRUE)
