# Chapter 15: Growing the DAW - tracks you can add, projects you can save, edits you can undo
#
# The chapter starts where chapter 14 ends and replaces its fixed sizes step by step. Like
# chapter 14's build-along route, every step is a small self-contained snapshot (steps/).
# Step 6 brings Steps 1-3 into a copy of chapter 14's engine (engine/), no JUCE needed; Step 7
# brings that engine into a copy of chapter 14's application (app/), built with -Juce only.

add_subdirectory(steps)

# ---- exercises: starters (build only) and solutions (ctest label: solution) -------------------
# Exercises 3-5 build on the model of Steps 4 and 5 (exercises/model.h); Exercise 2 checks its
# numbers with the engine's CpuMeter. Exercises 1 and 6 are answered in solutions/README.md.
dawlab_add_solution(l15_ex2_solution solutions/ex2_cpu_budget.cpp)
target_include_directories(l15_ex2_solution PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/engine/include)
dawlab_add_starter(l15_ex3_starter   exercises/starter/ex3_duplicate_track.cpp)
dawlab_add_solution(l15_ex3_solution solutions/ex3_duplicate_track.cpp)
dawlab_add_starter(l15_ex4_starter   exercises/starter/ex4_format3.cpp)
dawlab_add_solution(l15_ex4_solution solutions/ex4_format3.cpp)
dawlab_add_starter(l15_ex5_starter   exercises/starter/ex5_byte_budget.cpp)
dawlab_add_solution(l15_ex5_solution solutions/ex5_byte_budget.cpp)

# ---- the chapter-15 engine (header-only, namespace dawlab::engine; Step 6) ------------------
# A copy of chapter 14's engine whose 8 fixed tracks became a pool of kMaxTracks slots. It uses
# the same include paths (<dawlab/engine/...>), so it must not be linked together with
# dawlab::engine; the two chapters never share a target.
add_library(dawlab_engine15 INTERFACE)
add_library(dawlab::engine15 ALIAS dawlab_engine15)
target_include_directories(dawlab_engine15 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/engine/include)
target_link_libraries(dawlab_engine15 INTERFACE dawlab::dsp)

# Engine tests (ctest label: test). The first nine are chapter 14's tests, ported only so far as
# tracks must now be added before they play (tests/TestTracks.h); they must pass unchanged in
# substance. The pool tests use a small pool so that they can fill it.
find_package(Threads REQUIRED)
function(l15_add_test target)
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE dawlab::dsp dawlab::engine15 Threads::Threads)
    dawlab_set_warnings(${target})
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES
        LABELS "test"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        TIMEOUT 300)
endfunction()

l15_add_test(l15_test_timeline          tests/test_timeline.cpp)
l15_add_test(l15_test_mixer             tests/test_mixer.cpp)
l15_add_test(l15_test_effects           tests/test_effects.cpp)
l15_add_test(l15_test_commands          tests/test_commands.cpp)
l15_add_test(l15_test_pdc               tests/test_pdc.cpp)
l15_add_test(l15_test_block_invariance  tests/test_block_invariance.cpp)
l15_add_test(l15_test_realtime_safety   tests/test_realtime_safety.cpp)
l15_add_test(l15_test_demo_song         tests/test_demo_song.cpp)
l15_add_test(l15_test_track_format      tests/test_track_format.cpp)

# The pool itself (Step 6), with a pool small enough to fill.
l15_add_test(l15_test_track_pool        tests/test_track_pool.cpp)
target_compile_definitions(l15_test_track_pool PRIVATE DAWLAB_ENGINE_MAX_TRACKS=16)

# The application's project file (Step 8): text in, text out, no JUCE, so it is tested here and
# runs in every build, not only with -Juce.
l15_add_test(l15_test_project_format    tests/test_project_format.cpp app/Source/ProjectFormat.cpp)
target_include_directories(l15_test_project_format PRIVATE app/Source)

# ---- Step 7: the JUCE application "DawLab 15" ----------------------------------------------------
# Needs -DDAWLAB_WITH_JUCE=ON (build.ps1 -Juce). Without JUCE the chapter stops here.
if(NOT DAWLAB_WITH_JUCE)
    message(STATUS "15-daw-growing: the JUCE app (DawLab15) is skipped (use -Juce)")
    return()
endif()

add_subdirectory(app)   # targets: l15_daw_app (static library), DawLab15 (the application)

# ---- optional exercises 7-9 (the application): starters build only, solutions are ctests -------
# JUCE console programs linked against the app library: they build the real MainComponent in its
# offline mode, so they need no audio device and no clicking (chapter 14's l14_add_juce_program).
function(l15_add_juce_program target label)
    juce_add_console_app(${target} PRODUCT_NAME ${target})
    target_sources(${target} PRIVATE ${ARGN})
    target_link_libraries(${target} PRIVATE l15_daw_app)
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        FOLDER "15-daw-growing")
    dawlab_set_warnings(${target})
    if(label)
        add_test(NAME ${target} COMMAND ${target})
        set_tests_properties(${target} PROPERTIES
            LABELS "${label}"
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            TIMEOUT 300)
    endif()
endfunction()

l15_add_juce_program(l15_ex7_starter  ""       exercises/starter/ex7_reorder_tracks.cpp)
l15_add_juce_program(l15_ex7_solution solution solutions/ex7_reorder_tracks.cpp)
l15_add_juce_program(l15_ex8_starter  ""       exercises/starter/ex8_recent_and_autosave.cpp)
l15_add_juce_program(l15_ex8_solution solution solutions/ex8_recent_and_autosave.cpp)
l15_add_juce_program(l15_ex9_starter  ""       exercises/starter/ex9_relink_audio.cpp)
l15_add_juce_program(l15_ex9_solution solution solutions/ex9_relink_audio.cpp)
