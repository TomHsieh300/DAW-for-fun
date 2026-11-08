# Chapter 17: Recording - from the audio input to a clip on the timeline
#
# Like chapters 15 and 16, the chapter starts with small self-contained snapshots (steps/), one per
# idea, and then brings them into a copy of chapter 16's engine and application. No JUCE needed for
# the steps.

add_subdirectory(steps)

# ---- the chapter-17 engine (header-only, namespace dawlab::engine; Step 6) ------------------
# A copy of chapter 16's engine with recording. It uses the same include paths (<dawlab/engine/...>),
# so it must not be linked together with another chapter's engine; the chapters never share a target.
add_library(dawlab_engine17 INTERFACE)
add_library(dawlab::engine17 ALIAS dawlab_engine17)
target_include_directories(dawlab_engine17 INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/engine/include)
target_link_libraries(dawlab_engine17 INTERFACE dawlab::dsp)

# Engine tests (ctest label: test). All but the last two are chapter 16's tests, unchanged: without
# an input the engine must do exactly what it did.
find_package(Threads REQUIRED)
function(l17_add_test target)
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE dawlab::dsp dawlab::engine17 Threads::Threads)
    dawlab_set_warnings(${target})
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES
        LABELS "test"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        TIMEOUT 300)
endfunction()

l17_add_test(l17_test_timeline          tests/test_timeline.cpp)
l17_add_test(l17_test_mixer             tests/test_mixer.cpp)
l17_add_test(l17_test_effects           tests/test_effects.cpp)
l17_add_test(l17_test_commands          tests/test_commands.cpp)
l17_add_test(l17_test_pdc               tests/test_pdc.cpp)
l17_add_test(l17_test_block_invariance  tests/test_block_invariance.cpp)
l17_add_test(l17_test_realtime_safety   tests/test_realtime_safety.cpp)
l17_add_test(l17_test_demo_song         tests/test_demo_song.cpp)
l17_add_test(l17_test_track_format      tests/test_track_format.cpp)
l17_add_test(l17_test_track_pool        tests/test_track_pool.cpp)
target_compile_definitions(l17_test_track_pool PRIVATE DAWLAB_ENGINE_MAX_TRACKS=16)
l17_add_test(l17_test_automation        tests/test_automation.cpp)
l17_add_test(l17_test_recording         tests/test_recording.cpp)
l17_add_test(l17_test_recording_threads tests/test_recording_threads.cpp)

# The application's project file: text in, text out, no JUCE, so it is tested here and runs in
# every build, not only with -Juce.
l17_add_test(l17_test_project_format    tests/test_project_format.cpp app/Source/ProjectFormat.cpp)
target_include_directories(l17_test_project_format PRIVATE app/Source)

# ---- Step 7: the JUCE application "DawLab 17" ----------------------------------------------------
# Needs -DDAWLAB_WITH_JUCE=ON (build.ps1 -Juce). Without JUCE the chapter stops here.
if(NOT DAWLAB_WITH_JUCE)
    message(STATUS "17-recording: the JUCE app (DawLab17) is skipped (use -Juce)")
    return()
endif()

add_subdirectory(app)   # targets: l17_daw_app (static library), DawLab17 (the application)
