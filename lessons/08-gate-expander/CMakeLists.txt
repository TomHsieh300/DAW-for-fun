# Chapter 08: Gate, Expander, De-esser, and Transient Shaper
#
# Examples (ctest label: example)
dawlab_add_example(l08_gate_drum_noise        examples/gate_drum_noise.cpp)
dawlab_add_example(l08_expander_vs_gate_curve examples/expander_vs_gate_curve.cpp)
dawlab_add_example(l08_deesser_sibilance      examples/deesser_sibilance.cpp)
dawlab_add_example(l08_transient_shaper_drums examples/transient_shaper_drums.cpp)

# Exercise starter code (build only, no tests are run)
dawlab_add_starter(l08_ex4_starter exercises/starter/ex4_sidechain_gate.cpp)
dawlab_add_starter(l08_ex5_starter exercises/starter/ex5_upward_expander.cpp)
dawlab_add_starter(l08_ex6_starter exercises/starter/ex6_lookahead_gate.cpp)
dawlab_add_starter(l08_ex7_starter exercises/starter/ex7_splitband_deesser.cpp)
dawlab_add_starter(l08_ex8_starter exercises/starter/ex8_transient_shaper.cpp)

# Exercise solutions (ctest label: solution)
dawlab_add_solution(l08_ex4_solution solutions/ex4_sidechain_gate.cpp)
dawlab_add_solution(l08_ex5_solution solutions/ex5_upward_expander.cpp)
dawlab_add_solution(l08_ex6_solution solutions/ex6_lookahead_gate.cpp)
dawlab_add_solution(l08_ex7_solution solutions/ex7_splitband_deesser.cpp)
dawlab_add_solution(l08_ex8_solution solutions/ex8_transient_shaper.cpp)
