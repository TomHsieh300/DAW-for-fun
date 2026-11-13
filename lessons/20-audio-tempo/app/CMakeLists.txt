# Chapter 20, Step 6: the JUCE application "DawLab 20" - a copy of chapter 19's "DawLab 19" (a tempo lane, MIDI
# files, instruments on tracks), to which Step 6 adds audio clips that follow the tempo.
#
#   l20_daw_app   STATIC library with every app class except main() (as l14_daw_app in chapter 14)
#   DawLab20      the GUI application (product name "DawLab 20") = Main.cpp + l20_daw_app.
#
# Executable: <build>/bin/<Config>/DawLab 20.exe
#   "DawLab 20.exe"                       normal start (default audio output device)
#   "DawLab 20.exe" --smoke-test          no audio device: demo song, checks, 2 s offline render to
#                                         dawlab_output/l20_app_smoke.wav, exit code 0 = all passed
#   "DawLab 20.exe" --snapshot <file.png> render the main window (demo song, playhead at bar 2) to PNG
#   "DawLab 20.exe" --device-test ...     on the real device: record, hold/recover, latency (DeviceTest.h)
#
# JUCE modules are not compiled again here: the chapter-13 host library (dawlab_plugin_host)
# compiles them once. If neither chapter 13 nor chapter 14 is part of this build
# (build.ps1 -Only 15-daw-growing), we add just chapter 13's host/ folder here.
#
# The app links dawlab::engine20, never dawlab::engine: both use the include paths
# <dawlab/engine/...>, so one target must not see both (chapter 15's CMakeLists.txt).

if(NOT TARGET dawlab_plugin_host)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../13-vst-hosting/host ${CMAKE_BINARY_DIR}/l20_plugin_host)
endif()

set(L20_APP_SOURCES
    Source/AppCommands.h
    Source/AudioHost.h
    Source/AudioHost.cpp
    Source/ArrangementView.h
    Source/ArrangementView.cpp
    Source/DawLookAndFeel.h
    Source/DeviceTest.h
    Source/DeviceTest.cpp
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
    Source/MidiModel.h
    Source/MidiRecording.h
    Source/MidiRecording.cpp
    Source/MediaLoader.h
    Source/MediaLoader.cpp
    Source/Meters.h
    Source/Meters.cpp
    Source/MixerView.h
    Source/MixerView.cpp
    Source/ParameterPanel.h
    Source/ParameterPanel.cpp
    Source/PianoRoll.h
    Source/PianoRoll.cpp
    Source/TempoLane.h
    Source/TempoLane.cpp
    Source/MidiFile.h
    Source/MidiFile.cpp
    Source/MidiFileIO.h
    Source/MidiFileIO.cpp
    Source/TempoThinning.h
    Source/PluginInstrument.h
    Source/PluginInstrument.cpp
    Source/SynthPanel.h
    Source/SynthPanel.cpp
    Source/ProjectFormat.h
    Source/ProjectFormat.cpp
    Source/ProjectLimits.h
    Source/ProjectIO.h
    Source/ProjectIO.cpp
    Source/Session.h
    Source/Session.cpp
    Source/TakeRecorder.h
    Source/TakeRecorder.cpp
    Source/SmokeTest.h
    Source/SmokeTest.cpp
    Source/TransportBar.h
    Source/TransportBar.cpp
    Source/VstInsert.h
    Source/VstInsert.cpp)

# Chapter 13's DawLab Compressor and DawLab Limiter, when chapter 13 is built in the same tree
# (see chapter 14's app/CMakeLists.txt for why the paths are rebuilt here).
set(L20_CH13_DEFINITIONS "")
set(L20_CH13_PLUGIN_TARGETS "")
set(_l20_ch13_artefacts "${CMAKE_BINARY_DIR}/lessons/13-vst-hosting/plugin")
if(TARGET DawLabCompressor_VST3)
    list(APPEND L20_CH13_DEFINITIONS
        "DAWLAB_L13_COMPRESSOR_VST3=\"${_l20_ch13_artefacts}/DawLabCompressor_artefacts/$<CONFIG>/VST3/DawLabCompressor.vst3\"")
    list(APPEND L20_CH13_PLUGIN_TARGETS DawLabCompressor_VST3)
endif()
if(TARGET DawLabLimiter_VST3)
    list(APPEND L20_CH13_DEFINITIONS
        "DAWLAB_L13_LIMITER_VST3=\"${_l20_ch13_artefacts}/DawLabLimiter_artefacts/$<CONFIG>/VST3/DawLabLimiter.vst3\"")
    list(APPEND L20_CH13_PLUGIN_TARGETS DawLabLimiter_VST3)
endif()

add_library(l20_daw_app STATIC ${L20_APP_SOURCES})
target_include_directories(l20_daw_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Source)
target_link_libraries(l20_daw_app PUBLIC dawlab_plugin_host dawlab::engine20)
target_compile_definitions(l20_daw_app PRIVATE ${L20_CH13_DEFINITIONS})
set_target_properties(l20_daw_app PROPERTIES FOLDER "20-audio-tempo")
dawlab_set_warnings(l20_daw_app)
if(MSVC)
    target_compile_options(l20_daw_app PRIVATE /bigobj)
endif()

juce_add_gui_app(DawLab20
    PRODUCT_NAME "DawLab 20"
    COMPANY_NAME "DawLab"
    VERSION 1.0.0
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "DawLab 20 records from the audio input.")
target_sources(DawLab20 PRIVATE Source/Main.cpp)
target_link_libraries(DawLab20 PRIVATE l20_daw_app)
target_compile_definitions(DawLab20 PRIVATE
    JUCE_APPLICATION_NAME_STRING="DawLab 20"
    JUCE_APPLICATION_VERSION_STRING="1.0.0")
set_target_properties(DawLab20 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    FOLDER "20-audio-tempo")
dawlab_set_warnings(DawLab20)
if(L20_CH13_PLUGIN_TARGETS)
    add_dependencies(DawLab20 ${L20_CH13_PLUGIN_TARGETS})
endif()

# Automated checks of the application (no audio device needed).
add_test(NAME l20_app_smoke_test COMMAND DawLab20 --smoke-test)
set_tests_properties(l20_app_smoke_test PROPERTIES
    LABELS "test"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    TIMEOUT 600)
add_test(NAME l20_app_snapshot COMMAND DawLab20 --snapshot dawlab_output/l20_app_snapshot.png)
set_tests_properties(l20_app_snapshot PROPERTIES
    LABELS "example"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    TIMEOUT 600)
