# Chapter 17, build-along route: every step as a small, self-contained snapshot.
#
# Every step folder holds
#   minidaw.h            the model up to this step (earlier steps solved, this step's functions
#                        only declared)
#   check.h              the step's self-check (main); tests only what this step adds
#   starter/stepNN.cpp   the functions YOU write, with TODOs  -> l17_stepNN_starter  (build only)
#   solution/stepNN.cpp  a worked solution                   -> l17_stepNN_solution (ctest, label "solution")

find_package(Threads REQUIRED)

function(l17_add_step id dir)
    dawlab_add_starter(l17_${id}_starter ${dir}/starter/${id}.cpp ${dir}/minidaw.h ${dir}/check.h)
    dawlab_add_solution(l17_${id}_solution ${dir}/solution/${id}.cpp ${dir}/minidaw.h ${dir}/check.h)
    target_link_libraries(l17_${id}_starter PRIVATE Threads::Threads)
    target_link_libraries(l17_${id}_solution PRIVATE Threads::Threads)
endfunction()

l17_add_step(step01 step01-input-ring)
l17_add_step(step02 step02-placement)
