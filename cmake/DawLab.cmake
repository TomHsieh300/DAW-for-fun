# 章節用的 CMake 輔助函式
#
#   dawlab_add_example(<target> <sources...>)   範例程式：建置並登記為 ctest（label: example）
#   dawlab_add_solution(<target> <sources...>)  練習解答：建置並登記為 ctest（label: solution）
#   dawlab_add_starter(<target> <sources...>)   練習起始碼：只建置、不跑測試（裡面留有 TODO）
#   dawlab_add_test(<target> <sources...>)      元件回歸測試（lessons/NN-name/tests/*.cpp）：建置並登記為 ctest（label: test）
#
# 所有 target 都連結 dawlab::dsp（含 dawlab::common），測試的工作目錄是 build 資料夾，
# 所以程式輸出的 WAV 會在 build/dawlab_output/ 底下。

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
