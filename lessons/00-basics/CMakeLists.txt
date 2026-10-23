# 第 00 章：數位音訊基礎與開發環境
# 本章沒有新的 dsp header，只用 common/ 的共用工具。

# 範例（ctest label: example）
dawlab_add_example(l00_test_signals      examples/test_signals.cpp)
dawlab_add_example(l00_block_processing  examples/block_processing.cpp)
dawlab_add_example(l00_wav_info          examples/wav_info.cpp)
dawlab_add_example(l00_db_math           examples/db_math.cpp)

# 練習起始碼（只建置、不跑測試）
dawlab_add_starter(l00_ex4_starter       exercises/starter/ex4_normalize.cpp)
dawlab_add_starter(l00_ex5_starter       exercises/starter/ex5_downmix.cpp)
dawlab_add_starter(l00_ex6_starter       exercises/starter/ex6_fade.cpp)
dawlab_add_starter(l00_ex7_starter       exercises/starter/ex7_interleave.cpp)

# 練習解答（ctest label: solution）
dawlab_add_solution(l00_ex4_solution     solutions/ex4_normalize.cpp)
dawlab_add_solution(l00_ex5_solution     solutions/ex5_downmix.cpp)
dawlab_add_solution(l00_ex6_solution     solutions/ex6_fade.cpp)
dawlab_add_solution(l00_ex7_solution     solutions/ex7_interleave.cpp)
