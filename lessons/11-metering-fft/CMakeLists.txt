# 第 11 章：Metering 與 FFT 頻譜分析
# 本章新增的 header：dsp/include/dawlab/dsp/Fft.h、LevelMeter.h、LoudnessMeter.h、SpectrumAnalyzer.h

# ---- 範例（ctest label: example）----
dawlab_add_example(l11_fft_basics          examples/fft_basics.cpp)
dawlab_add_example(l11_window_leakage      examples/window_leakage.cpp)
dawlab_add_example(l11_level_meter         examples/level_meter_timeline.cpp)
dawlab_add_example(l11_loudness            examples/loudness_verify.cpp)
dawlab_add_example(l11_spectrum_analyzer   examples/spectrum_analyzer.cpp)

# ---- 練習起始碼（只建置、不跑測試；執行會印出 FAIL 是預期的）----
dawlab_add_starter(l11_ex3_starter         exercises/starter/ex3_true_peak.cpp)
dawlab_add_starter(l11_ex4_starter         exercises/starter/ex4_vu_meter.cpp)
dawlab_add_starter(l11_ex5_starter         exercises/starter/ex5_loudness_range.cpp)
dawlab_add_starter(l11_ex6_starter         exercises/starter/ex6_fft_convolution.cpp)
dawlab_add_starter(l11_ex7_starter         exercises/starter/ex7_correlation_meter.cpp)

# ---- 練習解答（ctest label: solution）----
dawlab_add_solution(l11_ex3_solution       solutions/ex3_true_peak.cpp)
dawlab_add_solution(l11_ex4_solution       solutions/ex4_vu_meter.cpp)
dawlab_add_solution(l11_ex5_solution       solutions/ex5_loudness_range.cpp)
dawlab_add_solution(l11_ex6_solution       solutions/ex6_fft_convolution.cpp)
dawlab_add_solution(l11_ex7_solution       solutions/ex7_correlation_meter.cpp)
