# 第 05 章：Limiter
#
# 範例（ctest label: example）
dawlab_add_example(l05_compressor_vs_limiter examples/compressor_vs_limiter.cpp)
dawlab_add_example(l05_drum_limiting         examples/drum_limiting.cpp)
dawlab_add_example(l05_latency_impulse       examples/latency_impulse.cpp)
dawlab_add_example(l05_ceiling_stress        examples/ceiling_stress.cpp)

# 練習起始碼（只建置、不跑測試）
dawlab_add_starter(l05_ex3_starter exercises/starter/ex3_sliding_window_min.cpp)
dawlab_add_starter(l05_ex4_starter exercises/starter/ex4_true_peak_meter.cpp)
dawlab_add_starter(l05_ex5_starter exercises/starter/ex5_soft_clipper.cpp)
dawlab_add_starter(l05_ex6_starter exercises/starter/ex6_fuzz_test.cpp)

# 練習解答（ctest label: solution）
dawlab_add_solution(l05_ex3_solution solutions/ex3_sliding_window_min.cpp)
dawlab_add_solution(l05_ex4_solution solutions/ex4_true_peak_meter.cpp)
dawlab_add_solution(l05_ex5_solution solutions/ex5_soft_clipper.cpp)
dawlab_add_solution(l05_ex6_solution solutions/ex6_fuzz_test.cpp)
