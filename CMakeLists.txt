cmake_minimum_required(VERSION 3.22)

# Must be set before project(): universal Mac build (Apple Silicon + Intel)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

project(GroundShake VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is downloaded automatically the first time you configure.
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(GroundShake
    COMPANY_NAME                "WOLLER"
    BUNDLE_ID                   com.woller.groundshake
    PRODUCT_NAME                "Ground Shake"
    PLUGIN_MANUFACTURER_CODE    Wolr
    PLUGIN_CODE                 Gsk1
    FORMATS                     VST3 AU Standalone
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_CATEGORIES             Fx Analyzer
    AU_MAIN_TYPE                kAudioUnitType_Effect)

target_sources(GroundShake PRIVATE
    Source/Analysis.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(GroundShake PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(GroundShake
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Small command-line tool that runs the reference analysis on audio files.
# Used to check BPM / downbeat detection; not needed for the plugin itself.
option(GS_BUILD_ANALYSIS_TEST "Build the analysis test tool" OFF)
if(GS_BUILD_ANALYSIS_TEST)
    juce_add_console_app(GroundShakeAnalysisTest PRODUCT_NAME "GroundShakeAnalysisTest")
    target_sources(GroundShakeAnalysisTest PRIVATE Source/Analysis.cpp Tools/AnalysisTest.cpp)
    target_compile_definitions(GroundShakeAnalysisTest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1)
    target_link_libraries(GroundShakeAnalysisTest PRIVATE
        juce::juce_audio_formats juce::juce_dsp juce::juce_recommended_config_flags)

    # end-to-end test: plugin processor + editor driven by a fake host, saves a screenshot
    juce_add_gui_app(GroundShakeRenderTest PRODUCT_NAME "GroundShakeRenderTest")
    target_sources(GroundShakeRenderTest PRIVATE
        Source/Analysis.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Tools/RenderTest.cpp)
    target_compile_definitions(GroundShakeRenderTest PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_MP3AUDIOFORMAT=1
        JucePlugin_Name="Ground Shake" JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(GroundShakeRenderTest PRIVATE
        juce::juce_audio_utils juce::juce_dsp juce::juce_recommended_config_flags)
endif()
