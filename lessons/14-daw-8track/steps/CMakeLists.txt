# Chapter 14, build-along route: Steps 1-10 of the engine as small, self-contained snapshots.
#
# Every step folder holds
#   minidaw.h            the whole mini engine up to this step (earlier steps solved, this
#                        step's functions only declared)
#   check.h              the step's self-check (main); tests only what this step adds
#   starter/stepNN.cpp   the functions YOU write, with TODOs  -> l14_stepNN_starter  (build only)
#   solution/stepNN.cpp  a worked solution                   -> l14_stepNN_solution (ctest, label "solution")
#
# The snapshots need only dawlab::dsp (chapters 01-12) - not the finished engine (dawlab::engine),
# which is the reference route of this chapter (engine/, app/).

function(l14_add_step id dir)
    dawlab_add_starter(l14_${id}_starter ${dir}/starter/${id}.cpp ${dir}/minidaw.h ${dir}/check.h)
    dawlab_add_solution(l14_${id}_solution ${dir}/solution/${id}.cpp ${dir}/minidaw.h ${dir}/check.h)
endfunction()

l14_add_step(step01 step01-skeleton)
l14_add_step(step02 step02-transport)
l14_add_step(step03 step03-clips)
l14_add_step(step04 step04-mixer)
l14_add_step(step05 step05-insert-effects)
l14_add_step(step06 step06-sends-buses)
l14_add_step(step07 step07-commands)
l14_add_step(step08 step08-pdc)
l14_add_step(step09 step09-offline-render)
l14_add_step(step10 step10-realtime-safety)
