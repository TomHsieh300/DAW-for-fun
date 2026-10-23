# 本章的兩個 plugin。每個 juce_add_plugin 會產生：
#   <name>            shared code 靜態函式庫（我們的 processor / editor + JUCE modules）
#   <name>_VST3       VST3 bundle：<build>/lessons/13-vst-hosting/plugin/<name>_artefacts/<Config>/VST3/<name>.vst3
#   <name>_Standalone 獨立執行檔（JUCE 幫 plugin 包一個有 audio 設定的視窗程式，方便不開 DAW 就試聽）
#
# COPY_PLUGIN_AFTER_BUILD FALSE：絕對不要自動安裝到 C:\Program Files\Common Files\VST3。
# 要給 Reaper 等 DAW 使用時，請照 README「把 plugin 交給其他 DAW」一節手動複製。

set(_l13_plugin_defs
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_FLAC=0
    JUCE_USE_OGGVORBIS=0)

# 只對我們自己的原始碼開 /W4（JUCE module 的原始碼也在同一個 target 裡，不套用）
function(_l13_strict_warnings)
    if(MSVC)
        set_source_files_properties(${ARGN} PROPERTIES COMPILE_OPTIONS "/W4;/permissive-")
    else()
        set_source_files_properties(${ARGN} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
    endif()
endfunction()

# ---- DawLabCompressor（第 04 章 Compressor + 第 01 章 Gain）---------------------------
juce_add_plugin(DawLabCompressor
    COMPANY_NAME "DawLab"
    PLUGIN_MANUFACTURER_CODE Dwlb        # 4 個字元、至少一個大寫
    PLUGIN_CODE Dcmp                     # 4 個字元、恰好一個大寫；同一個廠商內不可重複
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

# ---- DawLabLimiter（第 05 章 Limiter，會回報 latency）----------------------------------
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

# 給範例 / 測試用的 VST3 bundle 路徑（$<CONFIG> 會在 build 時換成 Debug / Release）
set(L13_COMPRESSOR_VST3
    "${CMAKE_CURRENT_BINARY_DIR}/DawLabCompressor_artefacts/$<CONFIG>/VST3/DawLabCompressor.vst3" PARENT_SCOPE)
set(L13_LIMITER_VST3
    "${CMAKE_CURRENT_BINARY_DIR}/DawLabLimiter_artefacts/$<CONFIG>/VST3/DawLabLimiter.vst3" PARENT_SCOPE)
