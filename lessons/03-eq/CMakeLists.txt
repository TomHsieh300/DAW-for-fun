# Chapter 03: Parametric EQ
# Target names are prefixed with l03_ (see docs/authoring-guide.md section 4.4)

# Examples (ctest label: example)
dawlab_add_example(l03_eq_curve           examples/eq_curve.cpp)
dawlab_add_example(l03_eq_presets         examples/eq_presets.cpp)
dawlab_add_example(l03_eq_automation      examples/eq_automation.cpp)
dawlab_add_example(l03_eq_phase_cramping  examples/eq_phase_cramping.cpp)

# Starter code for the programming exercises (build only, not run as tests; printing FAIL when executed is expected)
dawlab_add_starter(l03_ex2_starter  exercises/starter/ex2_q_bandwidth.cpp)
dawlab_add_starter(l03_ex3_starter  exercises/starter/ex3_cascade_sum.cpp)
dawlab_add_starter(l03_ex4_starter  exercises/starter/ex4_graphic_eq.cpp)
dawlab_add_starter(l03_ex5_starter  exercises/starter/ex5_tilt_eq.cpp)
dawlab_add_starter(l03_ex6_starter  exercises/starter/ex6_switchable_highcut.cpp)

# Solutions to the programming exercises (ctest label: solution)
dawlab_add_solution(l03_ex2_solution  solutions/ex2_q_bandwidth.cpp)
dawlab_add_solution(l03_ex3_solution  solutions/ex3_cascade_sum.cpp)
dawlab_add_solution(l03_ex4_solution  solutions/ex4_graphic_eq.cpp)
dawlab_add_solution(l03_ex5_solution  solutions/ex5_tilt_eq.cpp)
dawlab_add_solution(l03_ex6_solution  solutions/ex6_switchable_highcut.cpp)

# Regression test for ParametricEq (contract test: curve = measurement, smoothing time, smoothing 0, type changes, ...; ctest label: test)
dawlab_add_test(l03_test_parametric_eq  tests/parametric_eq_test.cpp)
