cmake_minimum_required(VERSION 3.22)
project(GitBassRig VERSION 0.1.0)

# JUCE als Submodul:  git submodule add https://github.com/juce-framework/JUCE.git JUCE
add_subdirectory(JUCE)

juce_add_plugin(GitBassRig
    COMPANY_NAME "DeinName"
    PLUGIN_MANUFACTURER_CODE Dnme
    PLUGIN_CODE Gbr1
    FORMATS VST3 Standalone
    PRODUCT_NAME "GitBassRig"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE)

target_sources(GitBassRig PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_features(GitBassRig PRIVATE cxx_std_17)

# Quelltext ist UTF-8 (Umlaute in Beschriftungen)
target_compile_options(GitBassRig PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

target_compile_definitions(GitBassRig PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(GitBassRig
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
