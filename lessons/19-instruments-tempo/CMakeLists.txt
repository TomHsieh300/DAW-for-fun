# Chapter 19: Instruments and tempo - a tempo map, instrument latency, and instruments in the app
#
# Like chapters 15 to 18, the chapter starts with small self-contained snapshots (steps/), one per
# idea, and then brings them into a copy of chapter 18's engine and application. No JUCE needed for
# the steps.

add_subdirectory(steps)

# ---- exercises: starters (build only) and solutions (ctest label: solution) -------------------
# Exercises 2, 4 and 5 build on Step 1's map (steps/step02-transport/minidaw.h has it solved); Exercise 3 stands
# alone; Exercise 2 checks its numbers in a program. Exercises 1 and 6 are answered in solutions/README.md.
dawlab_add_solution(l19_ex2_solution solutions/ex2_tempo_numbers.cpp)
dawlab_add_starter(l19_ex3_starter   exercises/starter/ex3_meter_map.cpp)
dawlab_add_solution(l19_ex3_solution solutions/ex3_meter_map.cpp)
dawlab_add_starter(l19_ex4_starter   exercises/starter/ex4_beats_to_map.cpp)
dawlab_add_solution(l19_ex4_solution solutions/ex4_beats_to_map.cpp)
dawlab_add_starter(l19_ex5_starter   exercises/starter/ex5_thin_tempo.cpp)
dawlab_add_solution(l19_ex5_solution solutions/ex5_thin_tempo.cpp)

# ---- the chapter-19 engine (header-only, namespace dawlab::engine; Step 4) ------------------
# A copy of chapter 18's engine with a tempo map and instrument latency. It uses the same include paths
# (<dawlab/engine/...>), so it must not be linked together with another chapter's engine; the chapters never
# share a target.
add_library(dawlab_engine19 INTERFACE)
add_library(dawlab::engine19 ALIAS dawlab_engine19)
target_include_directories(dawlab_engine19 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/engine/include)
target_link_libraries(dawlab_engine19 INTERFACE dawlab::dsp)

# Engine tests (ctest label: test): chapter 18's tests.
find_package(Threads REQUIRED)
function(l19_add_test target)
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE dawlab::dsp dawlab::engine19 Threads::Threads)
    dawlab_set_warnings(${target})
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES
        LABELS "test"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        TIMEOUT ${DAWLAB_LONG_TIMEOUT})
endfunction()

l19_add_test(l19_test_timeline          tests/test_timeline.cpp)
l19_add_test(l19_test_mixer             tests/test_mixer.cpp)
l19_add_test(l19_test_effects           tests/test_effects.cpp)
l19_add_test(l19_test_commands          tests/test_commands.cpp)
l19_add_test(l19_test_pdc               tests/test_pdc.cpp)
l19_add_test(l19_test_block_invariance  tests/test_block_invariance.cpp)
l19_add_test(l19_test_realtime_safety   tests/test_realtime_safety.cpp)
l19_add_test(l19_test_demo_song         tests/test_demo_song.cpp)
l19_add_test(l19_test_track_format      tests/test_track_format.cpp)
l19_add_test(l19_test_track_pool        tests/test_track_pool.cpp)
target_compile_definitions(l19_test_track_pool PRIVATE DAWLAB_ENGINE_MAX_TRACKS=16)
l19_add_test(l19_test_automation        tests/test_automation.cpp)
l19_add_test(l19_test_recording         tests/test_recording.cpp)
l19_add_test(l19_test_recording_threads tests/test_recording_threads.cpp)
l19_add_test(l19_test_midi              tests/test_midi.cpp)
l19_add_test(l19_test_tempo             tests/test_tempo.cpp)

# The application's project file: text in, text out, no JUCE, so it is tested here and runs in
# every build, not only with -Juce.
l19_add_test(l19_test_project_format    tests/test_project_format.cpp app/Source/ProjectFormat.cpp)
target_include_directories(l19_test_project_format PRIVATE app/Source)
# The same for Standard MIDI Files (chapter 19, Step 5).
l19_add_test(l19_test_midi_file         tests/test_midi_file.cpp app/Source/MidiFile.cpp)
target_include_directories(l19_test_midi_file PRIVATE app/Source)
# And for the tempos of an imported file: the fewest points within a tolerance (Exercises 5 and 9 in the app).
l19_add_test(l19_test_tempo_thinning    tests/test_tempo_thinning.cpp)
target_include_directories(l19_test_tempo_thinning PRIVATE app/Source)

# ---- Steps 5 and 6: the JUCE application "DawLab 19" ---------------------------------------------
# Needs -DDAWLAB_WITH_JUCE=ON (build.ps1 -Juce). Without JUCE the chapter stops here.
if(NOT DAWLAB_WITH_JUCE)
    message(STATUS "19-instruments-tempo: the JUCE app (DawLab19) is skipped (use -Juce)")
    return()
endif()

add_subdirectory(app)   # targets: l19_daw_app (static library), DawLab19 (the application)

# ---- optional exercises 7-9 (the application): starters build only, solutions are ctests ------
# JUCE console programs linked against the app library: they build the real MainComponent in its
# offline mode, so they need no audio device and no clicking (chapter 18's l18_add_juce_program).
function(l19_add_juce_program target label)
    juce_add_console_app(${target} PRODUCT_NAME ${target})
    target_sources(${target} PRIVATE ${ARGN})
    target_link_libraries(${target} PRIVATE l19_daw_app)
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        FOLDER "19-instruments-tempo")
    dawlab_set_warnings(${target})
    if(label)
        add_test(NAME ${target} COMMAND ${target})
        set_tests_properties(${target} PROPERTIES
            LABELS "${label}"
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            TIMEOUT ${DAWLAB_LONG_TIMEOUT})
    endif()
endfunction()

l19_add_juce_program(l19_ex7_starter   ""       exercises/starter/ex7_click_track.cpp)
l19_add_juce_program(l19_ex7_solution  solution solutions/ex7_click_track.cpp)
l19_add_juce_program(l19_ex8_starter   ""       exercises/starter/ex8_tap_tempo.cpp)
l19_add_juce_program(l19_ex8_solution  solution solutions/ex8_tap_tempo.cpp)
l19_add_juce_program(l19_ex9_starter   ""       exercises/starter/ex9_better_import.cpp)
l19_add_juce_program(l19_ex9_solution  solution solutions/ex9_better_import.cpp)
