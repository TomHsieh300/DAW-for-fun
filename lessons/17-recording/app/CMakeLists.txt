# Chapter 17, Step 7: the JUCE application "DawLab 17" - chapter 15's "DawLab 15" with automation:
# lanes under the tracks, points to draw and drag, Read / Touch / Latch / Write.
#
#   l17_daw_app   STATIC library with every app class except main() (as l14_daw_app in chapter 14)
#   DawLab17      the GUI application (product name "DawLab 17") = Main.cpp + l17_daw_app.
#
# Executable: <build>/bin/<Config>/DawLab 17.exe
#   "DawLab 17.exe"                       normal start (default audio output device)
#   "DawLab 17.exe" --smoke-test          no audio device: demo song, checks, 2 s offline render to
#                                         dawlab_output/l17_app_smoke.wav, exit code 0 = all passed
#   "DawLab 17.exe" --snapshot <file.png> render the main window (demo song, playhead at bar 2) to PNG
#
# JUCE modules are not compiled again here: the chapter-13 host library (dawlab_plugin_host)
# compiles them once. If neither chapter 13 nor chapter 14 is part of this build
# (build.ps1 -Only 15-daw-growing), we add just chapter 13's host/ folder here.
#
# The app links dawlab::engine17, never dawlab::engine: both use the include paths
# <dawlab/engine/...>, so one target must not see both (chapter 15's CMakeLists.txt).

if(NOT TARGET dawlab_plugin_host)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../13-vst-hosting/host ${CMAKE_BINARY_DIR}/l17_plugin_host)
endif()

set(L17_APP_SOURCES
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
    Source/Edits.h
    Source/History.h
    Source/History.cpp
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
    Source/ProjectIO.h
    Source/ProjectIO.cpp
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
set(L17_CH13_DEFINITIONS "")
set(L17_CH13_PLUGIN_TARGETS "")
set(_l17_ch13_artefacts "${CMAKE_BINARY_DIR}/lessons/13-vst-hosting/plugin")
if(TARGET DawLabCompressor_VST3)
    list(APPEND L17_CH13_DEFINITIONS
        "DAWLAB_L13_COMPRESSOR_VST3=\"${_l17_ch13_artefacts}/DawLabCompressor_artefacts/$<CONFIG>/VST3/DawLabCompressor.vst3\"")
    list(APPEND L17_CH13_PLUGIN_TARGETS DawLabCompressor_VST3)
endif()
if(TARGET DawLabLimiter_VST3)
    list(APPEND L17_CH13_DEFINITIONS
        "DAWLAB_L13_LIMITER_VST3=\"${_l17_ch13_artefacts}/DawLabLimiter_artefacts/$<CONFIG>/VST3/DawLabLimiter.vst3\"")
    list(APPEND L17_CH13_PLUGIN_TARGETS DawLabLimiter_VST3)
endif()

add_library(l17_daw_app STATIC ${L17_APP_SOURCES})
target_include_directories(l17_daw_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Source)
target_link_libraries(l17_daw_app PUBLIC dawlab_plugin_host dawlab::engine17)
target_compile_definitions(l17_daw_app PRIVATE ${L17_CH13_DEFINITIONS})
set_target_properties(l17_daw_app PROPERTIES FOLDER "17-recording")
dawlab_set_warnings(l17_daw_app)
if(MSVC)
    target_compile_options(l17_daw_app PRIVATE /bigobj)
endif()

juce_add_gui_app(DawLab17
    PRODUCT_NAME "DawLab 17"
    COMPANY_NAME "DawLab"
    VERSION 1.0.0)
target_sources(DawLab17 PRIVATE Source/Main.cpp)
target_link_libraries(DawLab17 PRIVATE l17_daw_app)
target_compile_definitions(DawLab17 PRIVATE
    JUCE_APPLICATION_NAME_STRING="DawLab 17"
    JUCE_APPLICATION_VERSION_STRING="1.0.0")
set_target_properties(DawLab17 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    FOLDER "17-recording")
dawlab_set_warnings(DawLab17)
if(L17_CH13_PLUGIN_TARGETS)
    add_dependencies(DawLab17 ${L17_CH13_PLUGIN_TARGETS})
endif()

# Automated checks of the application (no audio device needed).
add_test(NAME l17_app_smoke_test COMMAND DawLab17 --smoke-test)
set_tests_properties(l17_app_smoke_test PROPERTIES
    LABELS "test"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    TIMEOUT 600)
add_test(NAME l17_app_snapshot COMMAND DawLab17 --snapshot dawlab_output/l17_app_snapshot.png)
set_tests_properties(l17_app_snapshot PROPERTIES
    LABELS "example"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    TIMEOUT 600)
