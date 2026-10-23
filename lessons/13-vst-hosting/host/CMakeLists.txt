# dawlab_plugin_host：可重用的 VST3 hosting 靜態函式庫（第 14 章的 DAW 也用它）
#
# 結構：
#   dawlab_plugin_host_juce  STATIC：把需要的 JUCE modules（juce_audio_utils 以下全部）編譯一次。
#                            JUCE 的 module 是「帶原始碼的 INTERFACE library」，每個連結它的 target
#                            都會重新編譯一份 JUCE；把它們集中在這個 library，本章十幾個執行檔就只需編譯一次。
#                            它把 JUCE 的 include 路徑與 compile definitions 以 INTERFACE 轉交給使用者。
#   dawlab_plugin_host       STATIC：PluginHost / HostedPlugin / PluginWindow。
#                            PUBLIC 連結 dawlab_plugin_host_juce（= 所需 JUCE modules）與 dawlab::dsp。
#
# 使用者（例如第 14 章）只要：
#   target_link_libraries(my_app PRIVATE dawlab_plugin_host)
# 就能 #include <dawlab/host/PluginHost.h> 與任何 <juce_xxx/juce_xxx.h>（juce_audio_utils 以下的 module）。

add_library(dawlab_plugin_host_juce STATIC)
target_link_libraries(dawlab_plugin_host_juce
    PRIVATE
        juce::juce_audio_utils          # 會一併帶入 audio_processors(+headless)、audio_devices、audio_formats、gui_extra、gui_basics…
    PUBLIC
        juce::juce_recommended_config_flags)
target_compile_definitions(dawlab_plugin_host_juce
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_PLUGINHOST_VST3=1          # 開啟 VST3 hosting（juce::VST3PluginFormat）
        JUCE_PLUGINHOST_VST=0
        JUCE_PLUGINHOST_AU=0
        JUCE_PLUGINHOST_LADSPA=0
        JUCE_PLUGINHOST_LV2=0
        JUCE_USE_FLAC=0
        JUCE_USE_OGGVORBIS=0
    INTERFACE
        # 轉交「連同 JUCE modules 在內」的所有 compile definitions（例如 JUCE_MODULE_AVAILABLE_xxx=1）
        $<TARGET_PROPERTY:dawlab_plugin_host_juce,COMPILE_DEFINITIONS>)
target_include_directories(dawlab_plugin_host_juce
    INTERFACE
        $<TARGET_PROPERTY:dawlab_plugin_host_juce,INCLUDE_DIRECTORIES>)
set_target_properties(dawlab_plugin_host_juce PROPERTIES FOLDER "13-vst-hosting")

add_library(dawlab_plugin_host STATIC
    include/dawlab/host/PluginHost.h
    src/PluginHost.cpp
    src/HostedPlugin.cpp
    src/PluginWindow.cpp)
add_library(dawlab::plugin_host ALIAS dawlab_plugin_host)
target_include_directories(dawlab_plugin_host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(dawlab_plugin_host PUBLIC dawlab_plugin_host_juce dawlab::dsp)
dawlab_set_warnings(dawlab_plugin_host)   # /W4 只套在我們自己的原始碼上（JUCE 原始碼在 dawlab_plugin_host_juce）
set_target_properties(dawlab_plugin_host PROPERTIES FOLDER "13-vst-hosting")
