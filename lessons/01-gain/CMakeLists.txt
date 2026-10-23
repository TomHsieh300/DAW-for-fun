# 第 01 章：Gain、參數平滑與 Pan
# target 名稱以 l01_ 為前綴（見 docs/authoring-guide.md 4.4）

# 範例（ctest label: example）
dawlab_add_example(l01_zipper_noise     examples/zipper_noise.cpp)
dawlab_add_example(l01_pan_law          examples/pan_law.cpp)
dawlab_add_example(l01_mixing_headroom  examples/mixing_headroom.cpp)

# 實作題起始碼（只建置、不跑測試）
dawlab_add_starter(l01_ex3_starter      exercises/starter/ex3_exp_smoother.cpp)
dawlab_add_starter(l01_ex4_starter      exercises/starter/ex4_fader_taper.cpp)
dawlab_add_starter(l01_ex5_starter      exercises/starter/ex5_multiplicative_ramp.cpp)
dawlab_add_starter(l01_ex6_starter      exercises/starter/ex6_mute_solo.cpp)
dawlab_add_starter(l01_ex7_starter      exercises/starter/ex7_stereo_width.cpp)

# 實作題解答（ctest label: solution）
dawlab_add_solution(l01_ex3_solution    solutions/ex3_exp_smoother.cpp)
dawlab_add_solution(l01_ex4_solution    solutions/ex4_fader_taper.cpp)
dawlab_add_solution(l01_ex5_solution    solutions/ex5_multiplicative_ramp.cpp)
dawlab_add_solution(l01_ex6_solution    solutions/ex6_mute_solo.cpp)
dawlab_add_solution(l01_ex7_solution    solutions/ex7_stereo_width.cpp)
