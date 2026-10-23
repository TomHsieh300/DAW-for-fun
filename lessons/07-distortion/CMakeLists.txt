# 第 07 章：Distortion —— hard clip、foldback、fuzz、rectify、bitcrusher 與 amp chain
#
# 範例（ctest label: example）
dawlab_add_example(l07_distortion_types      examples/distortion_types.cpp)
dawlab_add_example(l07_amp_chain             examples/amp_chain.cpp)
dawlab_add_example(l07_bitcrusher_sweep      examples/bitcrusher_sweep.cpp)
dawlab_add_example(l07_oversampling_aliasing examples/oversampling_aliasing.cpp)

# 練習起始碼（只建置、不跑測試）
dawlab_add_starter(l07_ex3_starter exercises/starter/ex3_wavefolder.cpp)
dawlab_add_starter(l07_ex4_starter exercises/starter/ex4_cab_sim.cpp)
dawlab_add_starter(l07_ex5_starter exercises/starter/ex5_auto_gain.cpp)

# 練習解答（ctest label: solution）
dawlab_add_solution(l07_ex3_solution solutions/ex3_wavefolder.cpp)
dawlab_add_solution(l07_ex4_solution solutions/ex4_cab_sim.cpp)
dawlab_add_solution(l07_ex5_solution solutions/ex5_auto_gain.cpp)
