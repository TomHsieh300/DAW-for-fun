# dawlab_plugin_host: a reusable VST3 hosting static library (the chapter 14 DAW uses it too)
#
# Structure:
#   dawlab_plugin_host_juce  STATIC: compiles the required JUCE modules (juce_audio_utils and everything below it) once.
#                            A JUCE module is an "INTERFACE library that carries source code", so every target
#                            that links it would recompile JUCE; gathering them in this one library means the
#                            dozen-plus executables in this chapter compile JUCE only once.
#                            It forwards JUCE's include paths and compile definitions to users as INTERFACE.
#   dawlab_plugin_host       STATIC: PluginHost / HostedPlugin / PluginWindow.
#                            Links dawlab_plugin_host_juce (= the required JUCE modules) and dawlab::dsp as PUBLIC.
#
# A user (e.g. chapter 14) only needs:
#   target_link_libraries(my_app PRIVATE dawlab_plugin_host)
# and can then #include <dawlab/host/PluginHost.h> and any <juce_xxx/juce_xxx.h> (modules at or below juce_audio_utils).

add_library(dawlab_plugin_host_juce STATIC)
target_link_libraries(dawlab_plugin_host_juce
    PRIVATE
        juce::juce_audio_utils          # also pulls in audio_processors (+headless), audio_devices, audio_formats, gui_extra, gui_basics, ...
    PUBLIC
        juce::juce_recommended_config_flags)
target_compile_definitions(dawlab_plugin_host_juce
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_PLUGINHOST_VST3=1          # enables VST3 hosting (juce::VST3PluginFormat)
        JUCE_PLUGINHOST_VST=0
        JUCE_PLUGINHOST_AU=0
        JUCE_PLUGINHOST_LADSPA=0
        JUCE_PLUGINHOST_LV2=0
        JUCE_USE_FLAC=0
        JUCE_USE_OGGVORBIS=0
    INTERFACE
        # forward all compile definitions, including those of the JUCE modules (e.g. JUCE_MODULE_AVAILABLE_xxx=1)
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
dawlab_set_warnings(dawlab_plugin_host)   # /W4 applies only to our own sources (the JUCE sources live in dawlab_plugin_host_juce)
set_target_properties(dawlab_plugin_host PROPERTIES FOLDER "13-vst-hosting")
