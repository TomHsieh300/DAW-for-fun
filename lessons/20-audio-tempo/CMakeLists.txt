# Chapter 20: Audio that follows the tempo - warp markers, varispeed, and a time-stretcher
#
# Like chapters 15 to 19, the chapter starts with small self-contained snapshots (steps/), one per
# idea, and then brings them into a copy of chapter 19's engine and application. No JUCE needed for
# the steps.

add_subdirectory(steps)

# ---- exercises: starters (build only) and solutions (ctest label: solution) -------------------
# Exercises 3 to 5 build on the steps' models (steps/warp_map.h, exercises/wsola.h); Exercise 2 checks its
# numbers in a program. Exercises 1 and 6 are answered in solutions/README.md.
dawlab_add_solution(l20_ex2_solution solutions/ex2_numbers.cpp)
dawlab_add_starter(l20_ex3_starter   exercises/starter/ex3_fewest_markers.cpp)
dawlab_add_solution(l20_ex3_solution solutions/ex3_fewest_markers.cpp)
dawlab_add_starter(l20_ex4_starter   exercises/starter/ex4_fine_fit.cpp)
dawlab_add_solution(l20_ex4_solution solutions/ex4_fine_fit.cpp)
dawlab_add_starter(l20_ex5_starter   exercises/starter/ex5_declick.cpp)
dawlab_add_solution(l20_ex5_solution solutions/ex5_declick.cpp)

# ---- the chapter-20 engine (header-only, namespace dawlab::engine; Step 5) ------------------
# A copy of chapter 19's engine with audio clips that follow the tempo (Step 5). It uses the same include paths
# (<dawlab/engine/...>), so it must not be linked together with another chapter's engine; the chapters never
# share a target.
add_library(dawlab_engine20 INTERFACE)
add_library(dawlab::engine20 ALIAS dawlab_engine20)
target_include_directories(dawlab_engine20 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/engine/include)
target_link_libraries(dawlab_engine20 INTERFACE dawlab::dsp)

# Engine tests (ctest label: test): chapter 19's tests.
find_package(Threads REQUIRED)
function(l20_add_test target)
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE dawlab::dsp dawlab::engine20 Threads::Threads)
    dawlab_set_warnings(${target})
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES
        LABELS "test"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        TIMEOUT 300)
endfunction()

l20_add_test(l20_test_timeline          tests/test_timeline.cpp)
l20_add_test(l20_test_mixer             tests/test_mixer.cpp)
l20_add_test(l20_test_effects           tests/test_effects.cpp)
l20_add_test(l20_test_commands          tests/test_commands.cpp)
l20_add_test(l20_test_pdc               tests/test_pdc.cpp)
l20_add_test(l20_test_block_invariance  tests/test_block_invariance.cpp)
l20_add_test(l20_test_realtime_safety   tests/test_realtime_safety.cpp)
l20_add_test(l20_test_demo_song         tests/test_demo_song.cpp)
l20_add_test(l20_test_track_format      tests/test_track_format.cpp)
l20_add_test(l20_test_track_pool        tests/test_track_pool.cpp)
target_compile_definitions(l20_test_track_pool PRIVATE DAWLAB_ENGINE_MAX_TRACKS=16)
l20_add_test(l20_test_automation        tests/test_automation.cpp)
l20_add_test(l20_test_recording         tests/test_recording.cpp)
l20_add_test(l20_test_recording_threads tests/test_recording_threads.cpp)
l20_add_test(l20_test_midi              tests/test_midi.cpp)
l20_add_test(l20_test_tempo             tests/test_tempo.cpp)
l20_add_test(l20_test_warp              tests/test_warp.cpp)
# test_warp stretches audio on a worker thread: seconds in a normal build, a minute or two under ThreadSanitizer.
set_tests_properties(l20_test_warp PROPERTIES TIMEOUT 900)

# The application's project file: text in, text out, no JUCE, so it is tested here and runs in
# every build, not only with -Juce.
l20_add_test(l20_test_project_format    tests/test_project_format.cpp app/Source/ProjectFormat.cpp)
target_include_directories(l20_test_project_format PRIVATE app/Source)
# The same for Standard MIDI Files.
l20_add_test(l20_test_midi_file         tests/test_midi_file.cpp app/Source/MidiFile.cpp)
target_include_directories(l20_test_midi_file PRIVATE app/Source)
# And for the tempos of an imported file: the fewest points within a tolerance.
l20_add_test(l20_test_tempo_thinning    tests/test_tempo_thinning.cpp)
target_include_directories(l20_test_tempo_thinning PRIVATE app/Source)

# ---- Step 6: the JUCE application "DawLab 20" ---------------------------------------------
# Needs -DDAWLAB_WITH_JUCE=ON (build.ps1 -Juce). Without JUCE the chapter stops here.
if(NOT DAWLAB_WITH_JUCE)
    message(STATUS "20-audio-tempo: the JUCE app (DawLab20) is skipped (use -Juce)")
    return()
endif()

add_subdirectory(app)   # targets: l20_daw_app (static library), DawLab20 (the application)

# ---- optional exercises 7-9 (the application): starters build only, solutions are ctests ------
# JUCE console programs linked against the app library: they build the real MainComponent in its
# offline mode, so they need no audio device and no clicking (chapter 19's l19_add_juce_program).
function(l20_add_juce_program target label)
    juce_add_console_app(${target} PRODUCT_NAME ${target})
    target_sources(${target} PRIVATE ${ARGN})
    target_link_libraries(${target} PRIVATE l20_daw_app)
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        FOLDER "20-audio-tempo")
    dawlab_set_warnings(${target})
    if(label)
        add_test(NAME ${target} COMMAND ${target})
        set_tests_properties(${target} PROPERTIES
            LABELS "${label}"
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            TIMEOUT 300)
    endif()
endfunction()

l20_add_juce_program(l20_ex7_starter   ""       exercises/starter/ex7_follow_as_recorded.cpp)
l20_add_juce_program(l20_ex7_solution  solution solutions/ex7_follow_as_recorded.cpp)
l20_add_juce_program(l20_ex8_starter   ""       exercises/starter/ex8_quantize_audio.cpp)
l20_add_juce_program(l20_ex8_solution  solution solutions/ex8_quantize_audio.cpp)
l20_add_juce_program(l20_ex9_starter   ""       exercises/starter/ex9_loop_fits.cpp)
l20_add_juce_program(l20_ex9_solution  solution solutions/ex9_loop_fits.cpp)
