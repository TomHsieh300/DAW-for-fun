# 第 06 章：Saturation 與 aliasing
#
# 範例（ctest label: example）
dawlab_add_example(l06_harmonic_analysis    examples/harmonic_analysis.cpp)
dawlab_add_example(l06_aliasing_demo        examples/aliasing_demo.cpp)
dawlab_add_example(l06_oversampler_response examples/oversampler_response.cpp)
dawlab_add_example(l06_saturate_music       examples/saturate_music.cpp)

# 練習起始碼（只建置、不跑測試）
dawlab_add_starter(l06_ex4_starter exercises/starter/ex4_chebyshev_exciter.cpp)
dawlab_add_starter(l06_ex5_starter exercises/starter/ex5_aligned_dry_wet.cpp)
dawlab_add_starter(l06_ex6_starter exercises/starter/ex6_adaa.cpp)
dawlab_add_starter(l06_ex7_starter exercises/starter/ex7_oversampling_cost.cpp)

# 練習解答（ctest label: solution）
dawlab_add_solution(l06_ex2_solution solutions/ex2_cubic_harmonics.cpp)
dawlab_add_solution(l06_ex3_solution solutions/ex3_alias_frequencies.cpp)
dawlab_add_solution(l06_ex4_solution solutions/ex4_chebyshev_exciter.cpp)
dawlab_add_solution(l06_ex5_solution solutions/ex5_aligned_dry_wet.cpp)
dawlab_add_solution(l06_ex6_solution solutions/ex6_adaa.cpp)
dawlab_add_solution(l06_ex7_solution solutions/ex7_oversampling_cost.cpp)
