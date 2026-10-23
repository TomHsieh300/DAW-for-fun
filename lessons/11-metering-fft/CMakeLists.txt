# Chapter 11: Metering and FFT spectrum analysis
# Headers added in this chapter: dsp/include/dawlab/dsp/Fft.h, LevelMeter.h, LoudnessMeter.h, SpectrumAnalyzer.h

# ---- Examples (ctest label: example) ----
dawlab_add_example(l11_fft_basics          examples/fft_basics.cpp)
dawlab_add_example(l11_window_leakage      examples/window_leakage.cpp)
dawlab_add_example(l11_level_meter         examples/level_meter_timeline.cpp)
dawlab_add_example(l11_loudness            examples/loudness_verify.cpp)
dawlab_add_example(l11_spectrum_analyzer   examples/spectrum_analyzer.cpp)

# ---- Exercise starter code (build only, not tested; printing FAIL when run is expected) ----
dawlab_add_starter(l11_ex3_starter         exercises/starter/ex3_true_peak.cpp)
dawlab_add_starter(l11_ex4_starter         exercises/starter/ex4_vu_meter.cpp)
dawlab_add_starter(l11_ex5_starter         exercises/starter/ex5_loudness_range.cpp)
dawlab_add_starter(l11_ex6_starter         exercises/starter/ex6_fft_convolution.cpp)
dawlab_add_starter(l11_ex7_starter         exercises/starter/ex7_correlation_meter.cpp)

# ---- Exercise solutions (ctest label: solution) ----
dawlab_add_solution(l11_ex3_solution       solutions/ex3_true_peak.cpp)
dawlab_add_solution(l11_ex4_solution       solutions/ex4_vu_meter.cpp)
dawlab_add_solution(l11_ex5_solution       solutions/ex5_loudness_range.cpp)
dawlab_add_solution(l11_ex6_solution       solutions/ex6_fft_convolution.cpp)
dawlab_add_solution(l11_ex7_solution       solutions/ex7_correlation_meter.cpp)
