# Chapter 09: Delay and Echo
# dsp headers added in this chapter: dsp/include/dawlab/dsp/FractionalDelayLine.h, Delay.h
# Depends on: chapter 01 SmoothedValue.h, chapter 02 OnePole.h

# ---- Examples ------------------------------------------------------------
dawlab_add_example(l09_interpolation_compare examples/interpolation_compare.cpp)
dawlab_add_example(l09_pingpong_tempo        examples/pingpong_tempo.cpp)
dawlab_add_example(l09_doppler_sweep         examples/doppler_sweep.cpp)
dawlab_add_example(l09_comb_filter           examples/comb_filter.cpp)

# ---- Exercise starter code (built only, no tests) ------------------------
dawlab_add_starter(l09_ex3_starter exercises/starter/ex3_allpass_interpolation.cpp)
dawlab_add_starter(l09_ex4_starter exercises/starter/ex4_tape_delay.cpp)
dawlab_add_starter(l09_ex5_starter exercises/starter/ex5_multitap.cpp)
dawlab_add_starter(l09_ex6_starter exercises/starter/ex6_crossfade_delay.cpp)

# ---- Exercise solutions (with self-checks, ctest label: solution) --------
# Exercises 1 and 7 are calculation problems: the solution programs actually run the
# numbers in solutions/README.md to verify them
dawlab_add_solution(l09_ex1_solution solutions/ex1_tempo_math.cpp)
dawlab_add_solution(l09_ex3_solution solutions/ex3_allpass_interpolation.cpp)
dawlab_add_solution(l09_ex4_solution solutions/ex4_tape_delay.cpp)
dawlab_add_solution(l09_ex5_solution solutions/ex5_multitap.cpp)
dawlab_add_solution(l09_ex6_solution solutions/ex6_crossfade_delay.cpp)
dawlab_add_solution(l09_ex7_solution solutions/ex7_comb_math.cpp)
