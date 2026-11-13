# Chapter 16: Automation - parameters that change over time
#
# Like chapter 15, the chapter starts with small self-contained snapshots (steps/), one per idea,
# and then brings them into a copy of chapter 15's engine and application. No JUCE needed for the
# steps.

add_subdirectory(steps)

# ---- exercises: starters (build only) and solutions (ctest label: solution) -------------------
# Exercises 3-5 build on the models of the steps; Exercise 2 checks its numbers in a program.
# Exercises 1 and 6 are answered in solutions/README.md.
dawlab_add_solution(l16_ex2_solution solutions/ex2_automation_numbers.cpp)
dawlab_add_starter(l16_ex3_starter   exercises/starter/ex3_curve_shape.cpp)
dawlab_add_solution(l16_ex3_solution solutions/ex3_curve_shape.cpp)
dawlab_add_starter(l16_ex4_starter   exercises/starter/ex4_trim.cpp)
dawlab_add_solution(l16_ex4_solution solutions/ex4_trim.cpp)
dawlab_add_starter(l16_ex5_starter   exercises/starter/ex5_clipboard.cpp)
dawlab_add_solution(l16_ex5_solution solutions/ex5_clipboard.cpp)

# ---- the chapter-16 engine (header-only, namespace dawlab::engine; Step 5) ------------------
# A copy of chapter 15's engine with automation lanes. It uses the same include paths
# (<dawlab/engine/...>), so it must not be linked together with another chapter's engine; the
# chapters never share a target.
add_library(dawlab_engine16 INTERFACE)
add_library(dawlab::engine16 ALIAS dawlab_engine16)
target_include_directories(dawlab_engine16 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/engine/include)
target_link_libraries(dawlab_engine16 INTERFACE dawlab::dsp)

# Engine tests (ctest label: test). All but the last are chapter 15's tests, unchanged: without a
# curve the engine must do exactly what it did.
find_package(Threads REQUIRED)
function(l16_add_test target)
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE dawlab::dsp dawlab::engine16 Threads::Threads)
    dawlab_set_warnings(${target})
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES
        LABELS "test"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        TIMEOUT ${DAWLAB_LONG_TIMEOUT})
endfunction()

l16_add_test(l16_test_timeline          tests/test_timeline.cpp)
l16_add_test(l16_test_mixer             tests/test_mixer.cpp)
l16_add_test(l16_test_effects           tests/test_effects.cpp)
l16_add_test(l16_test_commands          tests/test_commands.cpp)
l16_add_test(l16_test_pdc               tests/test_pdc.cpp)
l16_add_test(l16_test_block_invariance  tests/test_block_invariance.cpp)
l16_add_test(l16_test_realtime_safety   tests/test_realtime_safety.cpp)
l16_add_test(l16_test_demo_song         tests/test_demo_song.cpp)
l16_add_test(l16_test_track_format      tests/test_track_format.cpp)
l16_add_test(l16_test_track_pool        tests/test_track_pool.cpp)
target_compile_definitions(l16_test_track_pool PRIVATE DAWLAB_ENGINE_MAX_TRACKS=16)
l16_add_test(l16_test_automation        tests/test_automation.cpp)

# The application's project file: text in, text out, no JUCE, so it is tested here and runs in
# every build, not only with -Juce.
l16_add_test(l16_test_project_format    tests/test_project_format.cpp app/Source/ProjectFormat.cpp)
target_include_directories(l16_test_project_format PRIVATE app/Source)

# ---- Step 6: the JUCE application "DawLab 16" ----------------------------------------------------
# Needs -DDAWLAB_WITH_JUCE=ON (build.ps1 -Juce). Without JUCE the chapter stops here.
if(NOT DAWLAB_WITH_JUCE)
    message(STATUS "16-automation: the JUCE app (DawLab16) is skipped (use -Juce)")
    return()
endif()

add_subdirectory(app)   # targets: l16_daw_app (static library), DawLab16 (the application)

# ---- optional exercises 7-9 (the application): starters build only, solutions are ctests -------
# JUCE console programs linked against the app library: they build the real MainComponent in its
# offline mode, so they need no audio device and no clicking (chapter 15's l15_add_juce_program).
function(l16_add_juce_program target label)
    juce_add_console_app(${target} PRODUCT_NAME ${target})
    target_sources(${target} PRIVATE ${ARGN})
    target_link_libraries(${target} PRIVATE l16_daw_app)
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        FOLDER "16-automation")
    dawlab_set_warnings(${target})
    if(label)
        add_test(NAME ${target} COMMAND ${target})
        set_tests_properties(${target} PROPERTIES
            LABELS "${label}"
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            TIMEOUT ${DAWLAB_LONG_TIMEOUT})
    endif()
endfunction()

l16_add_juce_program(l16_ex7_starter  ""       exercises/starter/ex7_line_tool.cpp)
l16_add_juce_program(l16_ex7_solution solution solutions/ex7_line_tool.cpp)
l16_add_juce_program(l16_ex8_starter  ""       exercises/starter/ex8_follow_touch.cpp)
l16_add_juce_program(l16_ex8_solution solution solutions/ex8_follow_touch.cpp)
l16_add_juce_program(l16_ex9_starter  ""       exercises/starter/ex9_lane_menu.cpp)
l16_add_juce_program(l16_ex9_solution solution solutions/ex9_lane_menu.cpp)
