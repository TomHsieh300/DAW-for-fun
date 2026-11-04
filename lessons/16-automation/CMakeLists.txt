# Chapter 16: Automation - parameters that change over time
#
# Like chapter 15, the chapter starts with small self-contained snapshots (steps/), one per idea,
# and then brings them into a copy of chapter 15's engine and application. No JUCE needed for the
# steps.

add_subdirectory(steps)

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
        TIMEOUT 300)
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
