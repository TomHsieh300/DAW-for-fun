# Chapter 20, build-along route: every step as a small, self-contained snapshot.
#
# Every step folder holds
#   minidaw.h            the model up to this step (earlier steps solved, this step's functions
#                        only declared)
#   check.h              the step's self-check (main); tests only what this step adds
#   starter/stepNN.cpp   the functions YOU write, with TODOs  -> l20_stepNN_starter  (build only)
#   solution/stepNN.cpp  a worked solution                   -> l20_stepNN_solution (ctest, label "solution")
#
# tempo_map.h is chapter 19's tempo map, solved: every step starts from it.

function(l20_add_step id dir)
    dawlab_add_starter(l20_${id}_starter ${dir}/starter/${id}.cpp ${dir}/minidaw.h ${dir}/check.h tempo_map.h)
    dawlab_add_solution(l20_${id}_solution ${dir}/solution/${id}.cpp ${dir}/minidaw.h ${dir}/check.h tempo_map.h)
endfunction()

l20_add_step(step01 step01-warp-map)
