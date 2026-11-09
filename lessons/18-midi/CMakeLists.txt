# Chapter 18: MIDI - notes in musical time, an instrument, and a keyboard
#
# Like chapters 15 to 17, the chapter starts with small self-contained snapshots (steps/), one per
# idea, and then brings them into a copy of chapter 17's engine and application. No JUCE needed for
# the steps.

add_subdirectory(steps)

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
