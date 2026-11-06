cmake_minimum_required(VERSION 3.22)

project(Spectra VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

# Point this at your JUCE checkout:  cmake -DJUCE_DIR=/path/to/JUCE ..
set(JUCE_DIR "$ENV{HOME}/JUCE" CACHE PATH "Path to JUCE")
add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)

juce_add_plugin(Spectra
    COMPANY_NAME                "Shreyas"
    BUNDLE_ID                   "com.shreyas.spectra"
    PLUGIN_MANUFACTURER_CODE    Shry
    PLUGIN_CODE                 Spct
    FORMATS                     VST3
    PRODUCT_NAME                "Spectra"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     TRUE
    AU_MAIN_TYPE                kAudioUnitType_Effect
    VST3_CATEGORIES             Fx Spatial Reverb)

target_sources(Spectra PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/SpectralEngine.cpp)

target_compile_definitions(Spectra PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Spectra
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Offline DSP checks (not built by default):  cmake --build build --target SpectraTests && ./build/SpectraTests_artefacts/Release/SpectraTests
juce_add_console_app(SpectraTests PRODUCT_NAME "SpectraTests")
set_target_properties(SpectraTests PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(SpectraTests PRIVATE
    tests/SpectraTests.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/SpectralEngine.cpp)
target_compile_definitions(SpectraTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(SpectraTests PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags)
