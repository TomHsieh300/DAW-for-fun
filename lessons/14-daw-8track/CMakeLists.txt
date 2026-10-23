# Chapter 14: Capstone - build an 8-track DAW step by step
#
# Stage 1 (this file): the pure C++ engine library and everything that tests or demonstrates
# it. No JUCE needed; builds with the default (non -Juce) build.
# Stage 2 (the JUCE app, app/) and the JUCE exercises are at the end of this file; they are only
# built with -DDAWLAB_WITH_JUCE=ON (build.ps1 -Juce).

# ---- the engine library (header-only, namespace dawlab::engine) -----------------------------
add_library(dawlab_engine INTERFACE)
add_library(dawlab::engine ALIAS dawlab_engine)
target_include_directories(dawlab_engine INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/engine/include)
target_link_libraries(dawlab_engine INTERFACE dawlab::dsp)

# Unit tests of the engine (ctest label: test). Same warning level as everything else.
function(l14_add_test target)
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE dawlab::dsp dawlab::engine)
    dawlab_set_warnings(${target})
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES
        LABELS "test"
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
        TIMEOUT 300)
endfunction()

# ---- examples (ctest label: example) ----------------------------------------------------------
dawlab_add_example(l14_hello_engine        examples/hello_engine.cpp)        # Step 1
dawlab_add_example(l14_transport_clips     examples/transport_clips.cpp)     # Steps 2-3
dawlab_add_example(l14_effect_catalog      examples/effect_catalog.cpp)      # Step 5
dawlab_add_example(l14_pdc_demo            examples/pdc_demo.cpp)            # Step 8
dawlab_add_example(l14_render_song         examples/render_song.cpp)         # Steps 4, 6, 9
dawlab_add_example(l14_realtime_sim        examples/realtime_sim.cpp)        # Steps 7, 10
foreach(t l14_hello_engine l14_transport_clips l14_effect_catalog l14_pdc_demo l14_render_song l14_realtime_sim)
    target_link_libraries(${t} PRIVATE dawlab::engine)
endforeach()

# ---- engine tests (ctest label: test) --------------------------------------------------------
l14_add_test(l14_test_timeline          tests/test_timeline.cpp)
l14_add_test(l14_test_mixer             tests/test_mixer.cpp)
l14_add_test(l14_test_effects           tests/test_effects.cpp)
l14_add_test(l14_test_commands          tests/test_commands.cpp)
l14_add_test(l14_test_pdc               tests/test_pdc.cpp)
l14_add_test(l14_test_block_invariance  tests/test_block_invariance.cpp)
l14_add_test(l14_test_realtime_safety   tests/test_realtime_safety.cpp)
l14_add_test(l14_test_demo_song         tests/test_demo_song.cpp)        # DemoSong.h == the Step 9 song

# ---- build-along route: Steps 1-10 as small snapshots (l14_stepNN_starter / _solution) -------
add_subdirectory(steps)

# ---- exercises: starters (build only) and solutions (ctest label: solution) -------------------
dawlab_add_starter(l14_ex3_starter          exercises/starter/ex3_automation.cpp)
dawlab_add_starter(l14_ex4_starter          exercises/starter/ex4_project_json.cpp)
dawlab_add_starter(l14_ex5_starter          exercises/starter/ex5_clip_crossfade.cpp)
dawlab_add_solution(l14_ex2_solution        solutions/ex2_timing_and_pdc.cpp)
dawlab_add_solution(l14_ex3_solution        solutions/ex3_automation.cpp)
dawlab_add_solution(l14_ex4_solution        solutions/ex4_project_json.cpp)
dawlab_add_solution(l14_ex5_solution        solutions/ex5_clip_crossfade.cpp)
foreach(t l14_ex3_starter l14_ex4_starter l14_ex5_starter
          l14_ex2_solution l14_ex3_solution l14_ex4_solution l14_ex5_solution)
    target_link_libraries(${t} PRIVATE dawlab::engine)
endforeach()

