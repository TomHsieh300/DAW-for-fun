# 第 03 章：Parametric EQ
# target 名稱以 l03_ 為前綴（見 docs/authoring-guide.md 4.4）

# 範例（ctest label: example）
dawlab_add_example(l03_eq_curve           examples/eq_curve.cpp)
dawlab_add_example(l03_eq_presets         examples/eq_presets.cpp)
dawlab_add_example(l03_eq_automation      examples/eq_automation.cpp)
dawlab_add_example(l03_eq_phase_cramping  examples/eq_phase_cramping.cpp)

# 實作題起始碼（只建置、不跑測試；執行會印 FAIL 是預期的）
dawlab_add_starter(l03_ex2_starter  exercises/starter/ex2_q_bandwidth.cpp)
dawlab_add_starter(l03_ex3_starter  exercises/starter/ex3_cascade_sum.cpp)
dawlab_add_starter(l03_ex4_starter  exercises/starter/ex4_graphic_eq.cpp)
dawlab_add_starter(l03_ex5_starter  exercises/starter/ex5_tilt_eq.cpp)
dawlab_add_starter(l03_ex6_starter  exercises/starter/ex6_switchable_highcut.cpp)

# 實作題解答（ctest label: solution）
dawlab_add_solution(l03_ex2_solution  solutions/ex2_q_bandwidth.cpp)
dawlab_add_solution(l03_ex3_solution  solutions/ex3_cascade_sum.cpp)
dawlab_add_solution(l03_ex4_solution  solutions/ex4_graphic_eq.cpp)
dawlab_add_solution(l03_ex5_solution  solutions/ex5_tilt_eq.cpp)
dawlab_add_solution(l03_ex6_solution  solutions/ex6_switchable_highcut.cpp)

# ParametricEq 的回歸測試（合約測試：曲線 = 實測、平滑時間、smoothing 0、換 type……；ctest label: test）
dawlab_add_test(l03_test_parametric_eq  tests/parametric_eq_test.cpp)
