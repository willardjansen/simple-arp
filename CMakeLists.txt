cmake_minimum_required(VERSION 3.22)

# Both of these have to be set before project(), or the first compiler check bakes in
# the host architecture and the universal build silently becomes single-architecture.
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build a universal binary")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

project(SimpleArp VERSION 1.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is cloned in beside the project rather than vendored, so a clean checkout -- CI,
# or anyone building this for the first time -- has to fetch it. Pinned, because a
# floating JUCE version is a build that breaks on someone else's machine and not yours.
set(SIMPLEARP_JUCE_TAG "9.0.1" CACHE STRING "JUCE version to fetch when none is present")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    message(STATUS "Using the JUCE checkout in ${CMAKE_CURRENT_SOURCE_DIR}/JUCE")
    add_subdirectory(JUCE)
else()
    message(STATUS "No local JUCE; fetching ${SIMPLEARP_JUCE_TAG}")
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${SIMPLEARP_JUCE_TAG}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(SimpleArp
    COMPANY_NAME            "Willard Jansen"
    BUNDLE_ID               "com.willardjansen.simplearp"
    PLUGIN_MANUFACTURER_CODE Wljn
    PLUGIN_CODE              Sarp
    FORMATS                  VST3 Standalone
    IS_SYNTH                 TRUE
    IS_MIDI_EFFECT           FALSE
    NEEDS_MIDI_INPUT         TRUE
    NEEDS_MIDI_OUTPUT        TRUE
    VST3_CATEGORIES          "Instrument"
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD  FALSE
    PRODUCT_NAME             "Simple Arp")

target_sources(SimpleArp PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(SimpleArp PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(SimpleArp
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

#==============================================================================
# Headless test harness for the arpeggiator core.
juce_add_console_app(ArpTest PRODUCT_NAME "ArpTest")

target_sources(ArpTest PRIVATE
    Tests/ArpTest.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(ArpTest PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JucePlugin_Name="Simple Arp")

target_link_libraries(ArpTest
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
