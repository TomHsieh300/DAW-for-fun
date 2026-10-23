# 第 13 章：VST3 plugin —— 自己做一個，再自己 host（需要 JUCE）
#
# 建置：.\scripts\build.ps1 -Only 13-vst-hosting -Juce
#
#   plugin/   DawLabCompressor、DawLabLimiter（juce_add_plugin：VST3 + Standalone）
#   host/     dawlab_plugin_host 靜態函式庫（PluginHost / HostedPlugin / PluginWindow，第 14 章也用）
#   examples/ l13_scan_plugins、l13_offline_host、l13_state_roundtrip（ctest label: example）、l13_gui_host（GUI，不跑測試）
#   exercises/starter、solutions/  練習 4–7（JUCE console app）
#
# 執行檔輸出在 <build>/bin/<Config>/（跟其他章節一樣）；VST3 bundle 在
# <build>/lessons/13-vst-hosting/plugin/<名稱>_artefacts/<Config>/VST3/<名稱>.vst3

if(NOT DAWLAB_WITH_JUCE)
    message(STATUS "13-vst-hosting：需要 JUCE，已略過（請用 -DDAWLAB_WITH_JUCE=ON 或 build.ps1 -Juce）")
    return()
endif()

add_subdirectory(plugin)   # 會設定 L13_COMPRESSOR_VST3 / L13_LIMITER_VST3（bundle 路徑）
add_subdirectory(host)

# l13_add_juce_program(<target> <label> <source>)
#   建立 JUCE console app，連結 dawlab_plugin_host，並把本章 plugin 的路徑編進程式
#   （DAWLAB_L13_COMPRESSOR_VST3 / DAWLAB_L13_LIMITER_VST3），所以不帶參數也能執行。
#   label 為 example / solution 時登記成 ctest；空字串（starter）只建置。
function(l13_add_juce_program target label source)
    juce_add_console_app(${target} PRODUCT_NAME ${target})
    target_sources(${target} PRIVATE ${source})
    target_link_libraries(${target} PRIVATE dawlab_plugin_host)
    target_compile_definitions(${target} PRIVATE
        "DAWLAB_L13_COMPRESSOR_VST3=\"${L13_COMPRESSOR_VST3}\""
        "DAWLAB_L13_LIMITER_VST3=\"${L13_LIMITER_VST3}\"")
    # 跟其他章節一樣放在 <build>/bin/<Config>/
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        FOLDER "13-vst-hosting")
    dawlab_set_warnings(${target})
    # 執行前 plugin 必須已經 build 好
    add_dependencies(${target} DawLabCompressor_VST3 DawLabLimiter_VST3)
    if(label)
        add_test(NAME ${target} COMMAND ${target})
        set_tests_properties(${target} PROPERTIES
            LABELS "${label}"
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            TIMEOUT 300)
    endif()
endfunction()

# ---- 範例（ctest label: example）------------------------------------------
l13_add_juce_program(l13_scan_plugins      example examples/scan_plugins.cpp)
l13_add_juce_program(l13_offline_host      example examples/offline_host.cpp)
l13_add_juce_program(l13_state_roundtrip   example examples/state_roundtrip.cpp)

# GUI host：開視窗、經 AudioDeviceManager + AudioProcessorPlayer 播放。不登記成 ctest。
juce_add_gui_app(l13_gui_host PRODUCT_NAME "l13_gui_host")
target_sources(l13_gui_host PRIVATE examples/gui_host.cpp)
target_link_libraries(l13_gui_host PRIVATE dawlab_plugin_host)
target_compile_definitions(l13_gui_host PRIVATE
    "DAWLAB_L13_COMPRESSOR_VST3=\"${L13_COMPRESSOR_VST3}\""
    "DAWLAB_L13_LIMITER_VST3=\"${L13_LIMITER_VST3}\""
    JUCE_APPLICATION_NAME_STRING="l13_gui_host"
    JUCE_APPLICATION_VERSION_STRING="1.0.0")
set_target_properties(l13_gui_host PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    FOLDER "13-vst-hosting")
dawlab_set_warnings(l13_gui_host)
add_dependencies(l13_gui_host DawLabCompressor_VST3 DawLabLimiter_VST3)

# ---- 練習起始碼（只建置、不跑測試）-----------------------------------------
l13_add_juce_program(l13_ex4_starter  "" exercises/starter/ex4_automation.cpp)
l13_add_juce_program(l13_ex5_starter  "" exercises/starter/ex5_preset_file.cpp)
l13_add_juce_program(l13_ex6_starter  "" exercises/starter/ex6_graph_chain.cpp)
l13_add_juce_program(l13_ex7_starter  "" exercises/starter/ex7_bypass_crossfade.cpp)

# ---- 練習解答（ctest label: solution）-------------------------------------
l13_add_juce_program(l13_ex4_solution solution solutions/ex4_automation.cpp)
l13_add_juce_program(l13_ex5_solution solution solutions/ex5_preset_file.cpp)
l13_add_juce_program(l13_ex6_solution solution solutions/ex6_graph_chain.cpp)
l13_add_juce_program(l13_ex7_solution solution solutions/ex7_bypass_crossfade.cpp)
