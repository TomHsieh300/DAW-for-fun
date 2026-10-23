# The two plugins of this chapter. Each juce_add_plugin produces:
#   <name>            shared-code static library (our processor / editor + JUCE modules)
#   <name>_VST3       VST3 bundle: <build>/lessons/13-vst-hosting/plugin/<name>_artefacts/<Config>/VST3/<name>.vst3
#   <name>_Standalone standalone executable (JUCE wraps the plugin in a windowed app with audio settings, so you can audition it without a DAW)
#
# COPY_PLUGIN_AFTER_BUILD FALSE: never install automatically into C:\Program Files\Common Files\VST3.
# To use the plugins in a DAW such as Reaper, copy them manually as described in the README section "Handing the plugins to other DAWs".

set(_l13_plugin_defs
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_FLAC=0
    JUCE_USE_OGGVORBIS=0)

# Enable /W4 only for our own sources (the JUCE module sources are in the same target and are excluded)
function(_l13_strict_warnings)
    if(MSVC)
        set_source_files_properties(${ARGN} PROPERTIES COMPILE_OPTIONS "/W4;/permissive-")
    else()
        set_source_files_properties(${ARGN} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
    endif()
endfunction()

# ---- DawLabCompressor(chapter 04 Compressor + chapter 01 Gain)---------------------------
juce_add_plugin(DawLabCompressor
    COMPANY_NAME "DawLab"
    PLUGIN_MANUFACTURER_CODE Dwlb        # 4 characters, at least one uppercase
    PLUGIN_CODE Dcmp                     # 4 characters, exactly one uppercase; must be unique within a manufacturer
    FORMATS VST3 Standalone
    PRODUCT_NAME "DawLabCompressor"
    DESCRIPTION "DawLab chapter 13 compressor"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Dynamics
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

set(_comp_sources
    compressor/CompressorProcessor.cpp
    compressor/CompressorEditor.cpp)
target_sources(DawLabCompressor PRIVATE ${_comp_sources}
    compressor/CompressorProcessor.h
    compressor/CompressorEditor.h)
target_compile_definitions(DawLabCompressor PUBLIC ${_l13_plugin_defs})
target_link_libraries(DawLabCompressor
    PRIVATE
        juce::juce_audio_utils
        dawlab::dsp
    PUBLIC
        juce::juce_recommended_config_flags)
_l13_strict_warnings(${_comp_sources})

# ---- DawLabLimiter(chapter 05 Limiter, reports latency)----------------------------------
juce_add_plugin(DawLabLimiter
    COMPANY_NAME "DawLab"
    PLUGIN_MANUFACTURER_CODE Dwlb
    PLUGIN_CODE Dlim
    FORMATS VST3 Standalone
    PRODUCT_NAME "DawLabLimiter"
    DESCRIPTION "DawLab chapter 13 lookahead limiter"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Dynamics Mastering
    COPY_PLUGIN_AFTER_BUILD FALSE
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE)

set(_lim_sources limiter/LimiterProcessor.cpp)
target_sources(DawLabLimiter PRIVATE ${_lim_sources} limiter/LimiterProcessor.h)
target_compile_definitions(DawLabLimiter PUBLIC ${_l13_plugin_defs})
target_link_libraries(DawLabLimiter
    PRIVATE
        juce::juce_audio_utils
        dawlab::dsp
    PUBLIC
        juce::juce_recommended_config_flags)
_l13_strict_warnings(${_lim_sources})

foreach(_t DawLabCompressor DawLabCompressor_VST3 DawLabCompressor_Standalone
           DawLabLimiter DawLabLimiter_VST3 DawLabLimiter_Standalone)
    if(TARGET ${_t})
        set_target_properties(${_t} PROPERTIES FOLDER "13-vst-hosting/plugins")
    endif()
endforeach()

# VST3 bundle paths for the examples / tests ($<CONFIG> becomes Debug / Release at build time)
set(L13_COMPRESSOR_VST3
    "${CMAKE_CURRENT_BINARY_DIR}/DawLabCompressor_artefacts/$<CONFIG>/VST3/DawLabCompressor.vst3" PARENT_SCOPE)
set(L13_LIMITER_VST3
    "${CMAKE_CURRENT_BINARY_DIR}/DawLabLimiter_artefacts/$<CONFIG>/VST3/DawLabLimiter.vst3" PARENT_SCOPE)
