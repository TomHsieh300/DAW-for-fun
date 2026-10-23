# Chapter 12: Reverb -- Algorithmic (FDN) and Convolution
#
# Examples (ctest label: example)
dawlab_add_example(l12_comb_rt60           examples/comb_rt60.cpp)
dawlab_add_example(l12_reverb_impulse      examples/reverb_impulse.cpp)
dawlab_add_example(l12_convolution_reverb  examples/convolution_reverb.cpp)
dawlab_add_example(l12_send_return         examples/send_return.cpp)

# Exercise starter code (build only, no tests run)
dawlab_add_starter(l12_ex3_starter         exercises/starter/ex3_early_reflections.cpp)
dawlab_add_starter(l12_ex4_starter         exercises/starter/ex4_fdn_modulation.cpp)
dawlab_add_starter(l12_ex5_starter         exercises/starter/ex5_gated_reverb.cpp)
dawlab_add_starter(l12_ex6_starter         exercises/starter/ex6_rt60_estimator.cpp)

# Exercise solutions (ctest label: solution)
dawlab_add_solution(l12_ex3_solution       solutions/ex3_early_reflections.cpp)
dawlab_add_solution(l12_ex4_solution       solutions/ex4_fdn_modulation.cpp)
dawlab_add_solution(l12_ex5_solution       solutions/ex5_gated_reverb.cpp)
dawlab_add_solution(l12_ex6_solution       solutions/ex6_rt60_estimator.cpp)
