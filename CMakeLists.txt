cmake_minimum_required(VERSION 3.22)

project(PuppyChow VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

# Build a universal binary (Apple Silicon + Intel) on macOS unless told otherwise.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

# Plugin formats to build. AU only exists on macOS.
if(APPLE)
    set(PUPPYCHOW_FORMATS AU VST3 Standalone)
else()
    set(PUPPYCHOW_FORMATS VST3 Standalone)
endif()

juce_add_plugin(PuppyChow
    COMPANY_NAME            "Jake Dragash"
    BUNDLE_ID               "com.jdragash.puppychow"
    PLUGIN_MANUFACTURER_CODE Jdrg
    PLUGIN_CODE             Pchw
    PRODUCT_NAME            "Puppy Chow"
    FORMATS                 ${PUPPYCHOW_FORMATS}
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    AU_MAIN_TYPE            kAudioUnitType_Effect
    VST3_CATEGORIES         Fx Pitch
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(PuppyChow PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp)

target_include_directories(PuppyChow PRIVATE src)

target_compile_definitions(PuppyChow PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(PuppyChow
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
