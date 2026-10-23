# Chapter 00: Digital Audio Fundamentals and the Development Environment
# This chapter adds no new dsp headers; it only uses the shared utilities in common/.

# Examples (ctest label: example)
dawlab_add_example(l00_test_signals      examples/test_signals.cpp)
dawlab_add_example(l00_block_processing  examples/block_processing.cpp)
dawlab_add_example(l00_wav_info          examples/wav_info.cpp)
dawlab_add_example(l00_db_math           examples/db_math.cpp)

# Exercise starter code (built only, not run as tests)
dawlab_add_starter(l00_ex4_starter       exercises/starter/ex4_normalize.cpp)
dawlab_add_starter(l00_ex5_starter       exercises/starter/ex5_downmix.cpp)
dawlab_add_starter(l00_ex6_starter       exercises/starter/ex6_fade.cpp)
dawlab_add_starter(l00_ex7_starter       exercises/starter/ex7_interleave.cpp)

# Exercise solutions (ctest label: solution)
dawlab_add_solution(l00_ex4_solution     solutions/ex4_normalize.cpp)
dawlab_add_solution(l00_ex5_solution     solutions/ex5_downmix.cpp)
dawlab_add_solution(l00_ex6_solution     solutions/ex6_fade.cpp)
dawlab_add_solution(l00_ex7_solution     solutions/ex7_interleave.cpp)
