# Chapter 18: MIDI - notes in musical time, an instrument, and a keyboard
#
# Like chapters 15 to 17, the chapter starts with small self-contained snapshots (steps/), one per
# idea, and then brings them into a copy of chapter 17's engine and application. No JUCE needed for
# the steps.

add_subdirectory(steps)

# ---- exercises: starters (build only) and solutions (ctest label: solution) -------------------
# Exercises 2, 3 and 5 build on Step 1's model (exercises/model.h), Exercise 6 on Step 4's voice; Exercise 4
# stands alone; Exercise 2 checks its numbers in a program. Exercises 1 and 7 are answered in solutions/README.md.
dawlab_add_solution(l18_ex2_solution solutions/ex2_midi_numbers.cpp)
dawlab_add_starter(l18_ex3_starter   exercises/starter/ex3_smf.cpp)
dawlab_add_solution(l18_ex3_solution solutions/ex3_smf.cpp)
dawlab_add_starter(l18_ex4_starter   exercises/starter/ex4_tempo_map.cpp)
dawlab_add_solution(l18_ex4_solution solutions/ex4_tempo_map.cpp)
dawlab_add_starter(l18_ex5_starter   exercises/starter/ex5_arpeggiator.cpp)
dawlab_add_solution(l18_ex5_solution solutions/ex5_arpeggiator.cpp)
dawlab_add_starter(l18_ex6_starter   exercises/starter/ex6_mono_synth.cpp)
dawlab_add_solution(l18_ex6_solution solutions/ex6_mono_synth.cpp)

# ---- the chapter-18 engine (header-only, namespace dawlab::engine; Step 7) ------------------
# A copy of chapter 17's engine with instruments. It uses the same include paths (<dawlab/engine/...>),
# so it must not be linked together with another chapter's engine; the chapters never share a target.
add_library(dawlab_engine18 INTERFACE)
add_library(dawlab::engine18 ALIAS dawlab_engine18)
target_include_directories(dawlab_engine18 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/engine/include)
target_link_libraries(dawlab_engine18 INTERFACE dawlab::dsp)

# Engine tests (ctest label: test): chapter 17's tests, unchanged.
find_package(Threads REQUIRED)
function(l18_add_test target)
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE dawlab::dsp dawlab::engine18 Threads::Threads)
    dawlab_set_warnings(${target})
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES
        LABELS "test"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        TIMEOUT 300)
endfunction()

l18_add_test(l18_test_timeline          tests/test_timeline.cpp)
l18_add_test(l18_test_mixer             tests/test_mixer.cpp)
l18_add_test(l18_test_effects           tests/test_effects.cpp)
l18_add_test(l18_test_commands          tests/test_commands.cpp)
l18_add_test(l18_test_pdc               tests/test_pdc.cpp)
l18_add_test(l18_test_block_invariance  tests/test_block_invariance.cpp)
l18_add_test(l18_test_realtime_safety   tests/test_realtime_safety.cpp)
l18_add_test(l18_test_demo_song         tests/test_demo_song.cpp)
l18_add_test(l18_test_track_format      tests/test_track_format.cpp)
l18_add_test(l18_test_track_pool        tests/test_track_pool.cpp)
target_compile_definitions(l18_test_track_pool PRIVATE DAWLAB_ENGINE_MAX_TRACKS=16)
l18_add_test(l18_test_automation        tests/test_automation.cpp)
l18_add_test(l18_test_recording         tests/test_recording.cpp)
l18_add_test(l18_test_recording_threads tests/test_recording_threads.cpp)
# Chapter 18: instrument tracks (the last test, new).
l18_add_test(l18_test_midi              tests/test_midi.cpp)

# The application's project file: text in, text out, no JUCE, so it is tested here and runs in
# every build, not only with -Juce.
l18_add_test(l18_test_project_format    tests/test_project_format.cpp app/Source/ProjectFormat.cpp)
target_include_directories(l18_test_project_format PRIVATE app/Source)

# ---- Step 8: the JUCE application "DawLab 18" ----------------------------------------------------
# Needs -DDAWLAB_WITH_JUCE=ON (build.ps1 -Juce). Without JUCE the chapter stops here.
if(NOT DAWLAB_WITH_JUCE)
    message(STATUS "18-midi: the JUCE app (DawLab18) is skipped (use -Juce)")
    return()
endif()

add_subdirectory(app)   # targets: l18_daw_app (static library), DawLab18 (the application)

# ---- optional exercises 8-12 (the application): starters build only, solutions are ctests ------
# JUCE console programs linked against the app library: they build the real MainComponent in its
# offline mode, so they need no audio device, no MIDI keyboard and no clicking (chapter 17's
# l17_add_juce_program).
function(l18_add_juce_program target label)
    juce_add_console_app(${target} PRODUCT_NAME ${target})
    target_sources(${target} PRIVATE ${ARGN})
    target_link_libraries(${target} PRIVATE l18_daw_app)
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        FOLDER "18-midi")
    dawlab_set_warnings(${target})
    if(label)
        add_test(NAME ${target} COMMAND ${target})
        set_tests_properties(${target} PROPERTIES
            LABELS "${label}"
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            TIMEOUT 300)
    endif()
endfunction()

l18_add_juce_program(l18_ex8_starter   ""       exercises/starter/ex8_synth_panel.cpp)
l18_add_juce_program(l18_ex8_solution  solution solutions/ex8_synth_panel.cpp)
l18_add_juce_program(l18_ex9_starter   ""       exercises/starter/ex9_midi_files.cpp)
l18_add_juce_program(l18_ex9_solution  solution solutions/ex9_midi_files.cpp)
l18_add_juce_program(l18_ex10_starter  ""       exercises/starter/ex10_computer_keyboard.cpp)
l18_add_juce_program(l18_ex10_solution solution solutions/ex10_computer_keyboard.cpp)
l18_add_juce_program(l18_ex11_starter  ""       exercises/starter/ex11_note_editor.cpp)
l18_add_juce_program(l18_ex11_solution solution solutions/ex11_note_editor.cpp)
l18_add_juce_program(l18_ex12_starter  ""       exercises/starter/ex12_plugin_instrument.cpp)
l18_add_juce_program(l18_ex12_solution solution solutions/ex12_plugin_instrument.cpp)
# Exercise 12 checks that chapter 13's DawLab Compressor (an effect) is refused as an instrument, when it is built.
if(TARGET DawLabCompressor_VST3)
    foreach(_t l18_ex12_starter l18_ex12_solution)
        target_compile_definitions(${_t} PRIVATE
            "DAWLAB_L13_COMPRESSOR_VST3=\"${CMAKE_BINARY_DIR}/lessons/13-vst-hosting/plugin/DawLabCompressor_artefacts/$<CONFIG>/VST3/DawLabCompressor.vst3\"")
        add_dependencies(${_t} DawLabCompressor_VST3)
    endforeach()
endif()
