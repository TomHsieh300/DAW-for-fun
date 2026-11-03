# Chapter 15, Step 7: the JUCE application "DawLab 15" - chapter 14's "DawLab 8" on the track
# pool of Step 6: tracks are added and deleted, the arrangement and the mixer scroll.
#
#   l15_daw_app   STATIC library with every app class except main() (as l14_daw_app in chapter 14)
#   DawLab15      the GUI application (product name "DawLab 15") = Main.cpp + l15_daw_app.
#
# Executable: <build>/bin/<Config>/DawLab 15.exe
#   "DawLab 15.exe"                       normal start (default audio output device)
#   "DawLab 15.exe" --smoke-test          no audio device: demo song, checks, 2 s offline render to
#                                         dawlab_output/l15_app_smoke.wav, exit code 0 = all passed
#   "DawLab 15.exe" --snapshot <file.png> render the main window (demo song, playhead at bar 2) to PNG
#
# JUCE modules are not compiled again here: the chapter-13 host library (dawlab_plugin_host)
# compiles them once. If neither chapter 13 nor chapter 14 is part of this build
# (build.ps1 -Only 15-daw-growing), we add just chapter 13's host/ folder here.
#
# The app links dawlab::engine15, never dawlab::engine: both use the include paths
# <dawlab/engine/...>, so one target must not see both (chapter 15's CMakeLists.txt).

if(NOT TARGET dawlab_plugin_host)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../13-vst-hosting/host ${CMAKE_BINARY_DIR}/l15_plugin_host)
endif()

set(L15_APP_SOURCES
    Source/AppCommands.h
    Source/AudioHost.h
    Source/AudioHost.cpp
    Source/ArrangementView.h
    Source/ArrangementView.cpp
    Source/DawLookAndFeel.h
    Source/DawLookAndFeel.cpp
    Source/EffectMenu.h
    Source/EffectMenu.cpp
    Source/Exporter.h
    Source/Exporter.cpp
    Source/MainComponent.h
    Source/MainComponent.cpp
    Source/MediaLoader.h
    Source/MediaLoader.cpp
    Source/Meters.h
    Source/Meters.cpp
    Source/MixerView.h
    Source/MixerView.cpp
    Source/ParameterPanel.h
    Source/ParameterPanel.cpp
    Source/ProjectFormat.h
    Source/ProjectFormat.cpp
    Source/Session.h
    Source/Session.cpp
    Source/SmokeTest.h
    Source/SmokeTest.cpp
    Source/TransportBar.h
    Source/TransportBar.cpp
    Source/VstInsert.h
    Source/VstInsert.cpp)

# Chapter 13's DawLab Compressor and DawLab Limiter, when chapter 13 is built in the same tree
# (see chapter 14's app/CMakeLists.txt for why the paths are rebuilt here).
set(L15_CH13_DEFINITIONS "")
set(L15_CH13_PLUGIN_TARGETS "")
set(_l15_ch13_artefacts "${CMAKE_BINARY_DIR}/lessons/13-vst-hosting/plugin")
if(TARGET DawLabCompressor_VST3)
    list(APPEND L15_CH13_DEFINITIONS
        "DAWLAB_L13_COMPRESSOR_VST3=\"${_l15_ch13_artefacts}/DawLabCompressor_artefacts/$<CONFIG>/VST3/DawLabCompressor.vst3\"")
    list(APPEND L15_CH13_PLUGIN_TARGETS DawLabCompressor_VST3)
endif()
if(TARGET DawLabLimiter_VST3)
    list(APPEND L15_CH13_DEFINITIONS
        "DAWLAB_L13_LIMITER_VST3=\"${_l15_ch13_artefacts}/DawLabLimiter_artefacts/$<CONFIG>/VST3/DawLabLimiter.vst3\"")
    list(APPEND L15_CH13_PLUGIN_TARGETS DawLabLimiter_VST3)
endif()

add_library(l15_daw_app STATIC ${L15_APP_SOURCES})
target_include_directories(l15_daw_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Source)
target_link_libraries(l15_daw_app PUBLIC dawlab_plugin_host dawlab::engine15)
target_compile_definitions(l15_daw_app PRIVATE ${L15_CH13_DEFINITIONS})
set_target_properties(l15_daw_app PROPERTIES FOLDER "15-daw-growing")
dawlab_set_warnings(l15_daw_app)
if(MSVC)
    target_compile_options(l15_daw_app PRIVATE /bigobj)
endif()

juce_add_gui_app(DawLab15
    PRODUCT_NAME "DawLab 15"
    COMPANY_NAME "DawLab"
    VERSION 1.0.0)
target_sources(DawLab15 PRIVATE Source/Main.cpp)
target_link_libraries(DawLab15 PRIVATE l15_daw_app)
target_compile_definitions(DawLab15 PRIVATE
    JUCE_APPLICATION_NAME_STRING="DawLab 15"
    JUCE_APPLICATION_VERSION_STRING="1.0.0")
set_target_properties(DawLab15 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    FOLDER "15-daw-growing")
dawlab_set_warnings(DawLab15)
if(L15_CH13_PLUGIN_TARGETS)
    add_dependencies(DawLab15 ${L15_CH13_PLUGIN_TARGETS})
endif()

# Automated checks of the application (no audio device needed).
add_test(NAME l15_app_smoke_test COMMAND DawLab15 --smoke-test)
set_tests_properties(l15_app_smoke_test PROPERTIES
    LABELS "test"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    TIMEOUT 600)
add_test(NAME l15_app_snapshot COMMAND DawLab15 --snapshot dawlab_output/l15_app_snapshot.png)
set_tests_properties(l15_app_snapshot PROPERTIES
    LABELS "example"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    TIMEOUT 600)
