# Chapter 14, stage 2: the JUCE application "DawLab 8" (Steps 11-21).
#
#   l14_daw_app   STATIC library with every app class except main(): audio host, session model,
#                 look-and-feel, transport bar, arrangement, mixer, parameter panels, VST3 inserts,
#                 file loading, export, meters, smoke test. The JUCE exercises link it too.
#   DawLab8       the GUI application (product name "DawLab 8") = Main.cpp + l14_daw_app.
#
# Executable: <build>/bin/<Config>/DawLab 8.exe
#   "DawLab 8.exe"                       normal start (default audio output device)
#   "DawLab 8.exe" --smoke-test          no audio device: demo song, checks, 2 s offline render to
#                                        dawlab_output/l14_app_smoke.wav, exit code 0 = all passed
#   "DawLab 8.exe" --snapshot <file.png> render the main window (demo song, playhead at bar 2) to PNG
#
# JUCE modules are not compiled again here: the chapter-13 host library (dawlab_plugin_host)
# compiles juce_audio_utils and everything below it once (dawlab_plugin_host_juce) and passes the
# include paths and compile definitions on. If chapter 13 is not part of this build
# (build.ps1 -Only 14-daw-8track), we add just its host/ folder here.

if(NOT TARGET dawlab_plugin_host)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../13-vst-hosting/host ${CMAKE_BINARY_DIR}/l14_plugin_host)
endif()

set(L14_APP_SOURCES
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
    Source/Session.h
    Source/Session.cpp
    Source/SmokeTest.h
    Source/SmokeTest.cpp
    Source/TransportBar.h
    Source/TransportBar.cpp
    Source/VstInsert.h
    Source/VstInsert.cpp)

# When chapter 13 is built in the same tree, its DawLab Compressor and DawLab Limiter appear in the
# insert menu, and the smoke test loads both (section 5b; the limiter reports a latency, so it
# tests PDC with a real VST3). Chapter 13 sets L13_COMPRESSOR_VST3 / L13_LIMITER_VST3 only in its
# own directory scope, so we rebuild the paths from the targets' artefact folders; $<CONFIG>
# becomes Debug / Release at build time.
set(L14_CH13_DEFINITIONS "")
set(L14_CH13_PLUGIN_TARGETS "")
set(_l14_ch13_artefacts "${CMAKE_BINARY_DIR}/lessons/13-vst-hosting/plugin")
if(TARGET DawLabCompressor_VST3)
    list(APPEND L14_CH13_DEFINITIONS
        "DAWLAB_L13_COMPRESSOR_VST3=\"${_l14_ch13_artefacts}/DawLabCompressor_artefacts/$<CONFIG>/VST3/DawLabCompressor.vst3\"")
    list(APPEND L14_CH13_PLUGIN_TARGETS DawLabCompressor_VST3)
endif()
if(TARGET DawLabLimiter_VST3)
    list(APPEND L14_CH13_DEFINITIONS
        "DAWLAB_L13_LIMITER_VST3=\"${_l14_ch13_artefacts}/DawLabLimiter_artefacts/$<CONFIG>/VST3/DawLabLimiter.vst3\"")
    list(APPEND L14_CH13_PLUGIN_TARGETS DawLabLimiter_VST3)
endif()

# l14_add_app_library(<name> [definitions...]): the app classes as a static library.
# Exercise 12 builds a second copy with DAWLAB_ENGINE_NUM_BUSES=3: the engine and the GUI follow
# that one constant.
function(l14_add_app_library name)
    add_library(${name} STATIC ${L14_APP_SOURCES})
    target_include_directories(${name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Source)
    target_link_libraries(${name} PUBLIC dawlab_plugin_host dawlab::engine)
    target_compile_definitions(${name} PUBLIC ${ARGN} PRIVATE ${L14_CH13_DEFINITIONS})
    set_target_properties(${name} PROPERTIES FOLDER "14-daw-8track")
    dawlab_set_warnings(${name})
    if(MSVC)
        target_compile_options(${name} PRIVATE /bigobj)
    endif()
endfunction()

l14_add_app_library(l14_daw_app)
l14_add_app_library(l14_daw_app_3bus DAWLAB_ENGINE_NUM_BUSES=3)

juce_add_gui_app(DawLab8
    PRODUCT_NAME "DawLab 8"
    COMPANY_NAME "DawLab"
    VERSION 1.0.0)
target_sources(DawLab8 PRIVATE Source/Main.cpp)
target_link_libraries(DawLab8 PRIVATE l14_daw_app)
target_compile_definitions(DawLab8 PRIVATE
    JUCE_APPLICATION_NAME_STRING="DawLab 8"
    JUCE_APPLICATION_VERSION_STRING="1.0.0")
set_target_properties(DawLab8 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    FOLDER "14-daw-8track")
dawlab_set_warnings(DawLab8)
# The smoke test loads chapter 13's bundles: build them first when they are part of this tree.
if(L14_CH13_PLUGIN_TARGETS)
    add_dependencies(DawLab8 ${L14_CH13_PLUGIN_TARGETS})
endif()

# Automated checks of the application (no audio device needed).
add_test(NAME l14_app_smoke_test COMMAND DawLab8 --smoke-test)
set_tests_properties(l14_app_smoke_test PROPERTIES
    LABELS "test"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    TIMEOUT 600)
add_test(NAME l14_app_snapshot COMMAND DawLab8 --snapshot dawlab_output/l14_app_snapshot.png)
set_tests_properties(l14_app_snapshot PROPERTIES
    LABELS "example"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    TIMEOUT 600)
