# Puppy Chow: a polyphonic octave pedal. Built from the repo root's CMakeLists.txt.
# The version here is the plugin's version; releases are tagged puppy-chow-vX.Y.Z.
project(PuppyChow VERSION 0.4.0 LANGUAGES C CXX)

# AU codes, bundle id and parameter ids must never change, or saved sessions
# and presets stop finding the plugin and its settings.

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

# The interface is a drawn web page (src/ui/index.html) shown in a web view.
juce_add_binary_data(PuppyChowUI
    NAMESPACE PuppyChowUI
    HEADER_NAME BinaryData.h
    SOURCES src/ui/index.html)

target_compile_definitions(PuppyChow PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

if(NOT MSVC)
    # Lets the compiler process several filter bands per instruction.
    target_compile_options(PuppyChow PRIVATE -fno-math-errno)
endif()

target_link_libraries(PuppyChow
    PRIVATE
        PuppyChowUI
        shared_dsp
        juce::juce_gui_extra
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Linux builds (cloud checks only) need WebKitGTK for the web view.
if(UNIX AND NOT APPLE)
    target_link_libraries(PuppyChow PRIVATE juce::pkgconfig_JUCE_BROWSER_LINUX_DEPS)
endif()

# Offline engine tests. Plain C++, no JUCE, so they run anywhere:
#   cmake --build build --target PuppyChowEngineTests && ctest --test-dir build -R puppy-chow
add_executable(PuppyChowEngineTests tests/engine_tests.cpp)
target_include_directories(PuppyChowEngineTests PRIVATE src)
if(NOT MSVC)
    target_compile_options(PuppyChowEngineTests PRIVATE -O3 -fno-math-errno)
endif()
add_test(NAME puppy-chow.engine COMMAND PuppyChowEngineTests)

# Host tests: the real plugin processor driven like a DAW would (several
# instances, odd block sizes, state copies). Built from the plugin sources so
# it needs no plugin host.
juce_add_console_app(PuppyChowHostTests PRODUCT_NAME "PuppyChowHostTests")
target_sources(PuppyChowHostTests PRIVATE
    tests/host_tests.cpp
    src/PluginProcessor.cpp
    src/PluginEditor.cpp)
target_include_directories(PuppyChowHostTests PRIVATE src)
target_compile_definitions(PuppyChowHostTests PRIVATE
    JucePlugin_Name="Puppy Chow"
    JucePlugin_VersionString="${PROJECT_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)
if(NOT MSVC)
    target_compile_options(PuppyChowHostTests PRIVATE -fno-math-errno)
endif()
target_link_libraries(PuppyChowHostTests PRIVATE
    shared_dsp
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)
add_test(NAME puppy-chow.host COMMAND PuppyChowHostTests)

# macOS only: loads the installed Audio Unit the way Logic does and runs
# several instances side by side. Not part of ctest because it needs the
# plugin installed first. See tests/au_host_check.cpp.
if(APPLE)
    juce_add_console_app(PuppyChowAUHostCheck PRODUCT_NAME "PuppyChowAUHostCheck")
    target_sources(PuppyChowAUHostCheck PRIVATE tests/au_host_check.cpp)
    target_compile_definitions(PuppyChowAUHostCheck PRIVATE
        JUCE_PLUGINHOST_AU=1
        JUCE_PLUGINHOST_VST3=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(PuppyChowAUHostCheck PRIVATE
        juce::juce_audio_processors
        juce::juce_gui_basics
        juce::juce_recommended_config_flags
        "-framework CoreAudioKit")
endif()
