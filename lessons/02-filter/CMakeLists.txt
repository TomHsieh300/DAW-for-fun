# Chapter 02: Filter -- from one-pole to biquad

# Examples (ctest label: example)
dawlab_add_example(l02_filter_response    examples/filter_response.cpp)
dawlab_add_example(l02_impulse_response   examples/impulse_response.cpp)
dawlab_add_example(l02_filter_sweep       examples/filter_sweep.cpp)
dawlab_add_example(l02_resonance          examples/resonance.cpp)
dawlab_add_example(l02_direct_forms       examples/direct_forms.cpp)
dawlab_add_example(l02_cutoff_modulation  examples/cutoff_modulation.cpp)

# Exercise starter code (build only)
dawlab_add_starter(l02_ex2_starter exercises/starter/ex2_dc_blocker.cpp)
dawlab_add_starter(l02_ex3_starter exercises/starter/ex3_butterworth4.cpp)
dawlab_add_starter(l02_ex4_starter exercises/starter/ex4_bandpass_cascade.cpp)
dawlab_add_starter(l02_ex5_starter exercises/starter/ex5_pole_stability.cpp)
dawlab_add_starter(l02_ex6_starter exercises/starter/ex6_moving_average.cpp)

# Exercise solutions (ctest label: solution)
dawlab_add_solution(l02_ex2_solution solutions/ex2_dc_blocker.cpp)
dawlab_add_solution(l02_ex3_solution solutions/ex3_butterworth4.cpp)
dawlab_add_solution(l02_ex4_solution solutions/ex4_bandpass_cascade.cpp)
dawlab_add_solution(l02_ex5_solution solutions/ex5_pole_stability.cpp)
dawlab_add_solution(l02_ex6_solution solutions/ex6_moving_average.cpp)
