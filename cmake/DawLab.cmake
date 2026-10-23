# CMake helper functions for the chapters
#
#   dawlab_add_example(<target> <sources...>)   Example program: built and registered as a ctest (label: example)
#   dawlab_add_solution(<target> <sources...>)  Exercise solution: built and registered as a ctest (label: solution)
#   dawlab_add_starter(<target> <sources...>)   Exercise starter code: built only, not run as a test (it contains TODOs)
#   dawlab_add_test(<target> <sources...>)      Component regression test (lessons/NN-name/tests/*.cpp): built and registered as a ctest (label: test)
#
# Every target links dawlab::dsp (which includes dawlab::common). Tests run with the build folder as
# their working directory, so the WAV files a program writes end up under build/dawlab_output/.

function(dawlab_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

function(_dawlab_add_program target label)
    add_executable(${target} ${ARGN})
    target_link_libraries(${target} PRIVATE dawlab::dsp)
    dawlab_set_warnings(${target})
    if(label)
        add_test(NAME ${target} COMMAND ${target})
        set_tests_properties(${target} PROPERTIES
            LABELS "${label}"
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            TIMEOUT 120)
    endif()
endfunction()

function(dawlab_add_example target)
    _dawlab_add_program(${target} example ${ARGN})
endfunction()

function(dawlab_add_solution target)
    _dawlab_add_program(${target} solution ${ARGN})
endfunction()

function(dawlab_add_starter target)
    _dawlab_add_program(${target} "" ${ARGN})
endfunction()

function(dawlab_add_test target)
    _dawlab_add_program(${target} test ${ARGN})
endfunction()
