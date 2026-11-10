# Chapter 20: Audio that follows the tempo - warp markers, varispeed, and a time-stretcher
#
# Like chapters 15 to 19, the chapter starts with small self-contained snapshots (steps/), one per
# idea, and then brings them into a copy of chapter 19's engine and application. No JUCE needed for
# the steps.

add_subdirectory(steps)

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
