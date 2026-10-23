# 第 04 章：Compressor
# 範例（ctest label: example）
dawlab_add_example(l04_transfer_curve  examples/transfer_curve.cpp)
dawlab_add_example(l04_step_response   examples/step_response.cpp)
dawlab_add_example(l04_drum_loop       examples/drum_loop.cpp)
dawlab_add_example(l04_stereo_link     examples/stereo_link.cpp)
dawlab_add_example(l04_robustness      examples/robustness.cpp)

# 練習起始碼（只建置、不跑測試）
dawlab_add_starter(l04_ex3_starter     exercises/starter/ex3_parallel.cpp)
dawlab_add_starter(l04_ex4_starter     exercises/starter/ex4_sidechain_hpf.cpp)
dawlab_add_starter(l04_ex5_starter     exercises/starter/ex5_auto_makeup.cpp)
dawlab_add_starter(l04_ex6_starter     exercises/starter/ex6_feedback.cpp)
dawlab_add_starter(l04_ex7_starter     exercises/starter/ex7_rms_window.cpp)

# 練習解答（ctest label: solution）
dawlab_add_solution(l04_ex2_solution   solutions/ex2_calculations.cpp)
dawlab_add_solution(l04_ex3_solution   solutions/ex3_parallel.cpp)
dawlab_add_solution(l04_ex4_solution   solutions/ex4_sidechain_hpf.cpp)
dawlab_add_solution(l04_ex5_solution   solutions/ex5_auto_makeup.cpp)
dawlab_add_solution(l04_ex6_solution   solutions/ex6_feedback.cpp)
dawlab_add_solution(l04_ex7_solution   solutions/ex7_rms_window.cpp)
