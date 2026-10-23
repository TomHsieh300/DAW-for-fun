# 第 10 章：Modulation —— Chorus、Flanger、Phaser、Tremolo

# 範例（ctest label: example）
dawlab_add_example(l10_lfo_shapes   examples/lfo_shapes.cpp)
dawlab_add_example(l10_chorus       examples/chorus_demo.cpp)
dawlab_add_example(l10_flanger      examples/flanger_demo.cpp)
dawlab_add_example(l10_phaser       examples/phaser_demo.cpp)
dawlab_add_example(l10_tremolo      examples/tremolo_demo.cpp)

# 練習：starter 只建置；solution 建置並登記為測試（ctest label: solution）
dawlab_add_starter(l10_ex3_starter    exercises/starter/ex3_vibrato.cpp)
dawlab_add_solution(l10_ex3_solution  solutions/ex3_vibrato.cpp)

dawlab_add_starter(l10_ex4_starter    exercises/starter/ex4_ringmod.cpp)
dawlab_add_solution(l10_ex4_solution  solutions/ex4_ringmod.cpp)

dawlab_add_starter(l10_ex5_starter    exercises/starter/ex5_stereo_chorus.cpp)
dawlab_add_solution(l10_ex5_solution  solutions/ex5_stereo_chorus.cpp)

dawlab_add_starter(l10_ex6_starter    exercises/starter/ex6_tempo_tremolo.cpp)
dawlab_add_solution(l10_ex6_solution  solutions/ex6_tempo_tremolo.cpp)

dawlab_add_starter(l10_ex8_starter    exercises/starter/ex8_staggered_phaser.cpp)
dawlab_add_solution(l10_ex8_solution  solutions/ex8_staggered_phaser.cpp)
