cmake_minimum_required(VERSION 3.15)
project(OrientalMultiFX VERSION 1.0.0)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(OrientalMultiFX
    COMPANY_NAME "MazzikaDSP"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    PLUGIN_MANUFACTURER_CODE Mzdp
    PLUGIN_CODE Omfx
    FORMATS VST3
    PRODUCT_NAME "Oriental Multi-Effect"
)

target_sources(OrientalMultiFX PRIVATE OrientalMultiFX.cpp)
target_compile_definitions(OrientalMultiFX PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(OrientalMultiFX PRIVATE juce::juce_audio_utils juce::juce_dsp)
