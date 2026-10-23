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
