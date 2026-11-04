# Chapter 16, build-along route: every step as a small, self-contained snapshot.
#
# Every step folder holds
#   minidaw.h            the model up to this step (earlier steps solved, this step's functions
#                        only declared)
#   check.h              the step's self-check (main); tests only what this step adds
#   starter/stepNN.cpp   the functions YOU write, with TODOs  -> l16_stepNN_starter  (build only)
#   solution/stepNN.cpp  a worked solution                   -> l16_stepNN_solution (ctest, label "solution")

function(l16_add_step id dir)
    dawlab_add_starter(l16_${id}_starter ${dir}/starter/${id}.cpp ${dir}/minidaw.h ${dir}/check.h)
    dawlab_add_solution(l16_${id}_solution ${dir}/solution/${id}.cpp ${dir}/minidaw.h ${dir}/check.h)
endfunction()

l16_add_step(step01 step01-curve)
l16_add_step(step02 step02-playback)
