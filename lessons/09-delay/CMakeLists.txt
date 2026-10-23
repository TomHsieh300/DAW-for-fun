# 第 09 章：Delay 與 Echo
# 本章新增的 dsp header：dsp/include/dawlab/dsp/FractionalDelayLine.h、Delay.h
# 依賴：第 01 章 SmoothedValue.h、第 02 章 OnePole.h

# ---- 範例 ----------------------------------------------------------------
dawlab_add_example(l09_interpolation_compare examples/interpolation_compare.cpp)
dawlab_add_example(l09_pingpong_tempo        examples/pingpong_tempo.cpp)
dawlab_add_example(l09_doppler_sweep         examples/doppler_sweep.cpp)
dawlab_add_example(l09_comb_filter           examples/comb_filter.cpp)

# ---- 練習起始碼（只建置、不跑測試）-------------------------------------
dawlab_add_starter(l09_ex3_starter exercises/starter/ex3_allpass_interpolation.cpp)
dawlab_add_starter(l09_ex4_starter exercises/starter/ex4_tape_delay.cpp)
dawlab_add_starter(l09_ex5_starter exercises/starter/ex5_multitap.cpp)
dawlab_add_starter(l09_ex6_starter exercises/starter/ex6_crossfade_delay.cpp)

# ---- 練習解答（含 self-check，ctest label: solution）-------------------
# 練習 1、7 是計算題：解答程式把 solutions/README.md 裡的數字實際跑一次驗證
dawlab_add_solution(l09_ex1_solution solutions/ex1_tempo_math.cpp)
dawlab_add_solution(l09_ex3_solution solutions/ex3_allpass_interpolation.cpp)
dawlab_add_solution(l09_ex4_solution solutions/ex4_tape_delay.cpp)
dawlab_add_solution(l09_ex5_solution solutions/ex5_multitap.cpp)
dawlab_add_solution(l09_ex6_solution solutions/ex6_crossfade_delay.cpp)
dawlab_add_solution(l09_ex7_solution solutions/ex7_comb_math.cpp)
