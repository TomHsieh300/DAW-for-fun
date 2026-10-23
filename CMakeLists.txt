cmake_minimum_required(VERSION 3.22)

project(DawLab
    VERSION 0.1.0
    DESCRIPTION "C++ audio DSP 教材：從 gain / filter / EQ / dynamics / saturation 到 VST hosting 與 8 軌 DAW"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 所有執行檔集中放在 build/bin（多組態 generator 會再分 Debug/Release）
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(MSVC)
    # 原始碼含繁體中文註解，MSVC 必須用 /utf-8 才不會亂碼或誤判
    add_compile_options(/utf-8)
endif()

option(DAWLAB_WITH_JUCE "建置需要 JUCE 的章節（13 VST hosting、14 DAW app）" OFF)
set(DAWLAB_JUCE_TAG "9.0.4" CACHE STRING "FetchContent 要下載的 JUCE 版本（git tag）")
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE/CMakeLists.txt)
    set(_juce_default ${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE)
else()
    set(_juce_default "")
endif()
set(DAWLAB_JUCE_DIR "${_juce_default}" CACHE PATH "本機 JUCE 原始碼路徑；留空則用 FetchContent 從 GitHub 下載")
set(DAWLAB_ONLY "" CACHE STRING "只建置指定章節（以分號分隔資料夾名，例如 02-filter;03-eq）；留空 = 全部")

enable_testing()
include(cmake/DawLab.cmake)

# ---- 共用函式庫 ----------------------------------------------------------
# dawlab::common : AudioBuffer、WAV 讀寫、測試訊號、分析工具、self-check
add_library(dawlab_common INTERFACE)
add_library(dawlab::common ALIAS dawlab_common)
target_include_directories(dawlab_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/common/include)

# dawlab::dsp : 每一章逐步長出來的 DSP 元件（header-only）
add_library(dawlab_dsp INTERFACE)
add_library(dawlab::dsp ALIAS dawlab_dsp)
target_include_directories(dawlab_dsp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/dsp/include)
target_link_libraries(dawlab_dsp INTERFACE dawlab_common)

# 共用工具本身的測試
_dawlab_add_program(common_selftest common common/tests/test_common.cpp)

# ---- JUCE（只有 13、14 需要）-------------------------------------------
if(DAWLAB_WITH_JUCE)
    if(DAWLAB_JUCE_DIR)
        add_subdirectory(${DAWLAB_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
    else()
        include(FetchContent)
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG        ${DAWLAB_JUCE_TAG}
            GIT_SHALLOW    ON)
        FetchContent_MakeAvailable(JUCE)
    endif()
endif()

# ---- 章節 ----------------------------------------------------------------
set(DAWLAB_LESSONS
    00-basics
    01-gain
    02-filter
    03-eq
    04-compressor
    05-limiter
    06-saturation
    07-distortion
    08-gate-expander
    09-delay
    10-modulation
    11-metering-fft
    12-reverb
    13-vst-hosting
    14-daw-8track)

foreach(lesson IN LISTS DAWLAB_LESSONS)
    set(_dir ${CMAKE_CURRENT_SOURCE_DIR}/lessons/${lesson})
    if(NOT EXISTS ${_dir}/CMakeLists.txt)
        continue()
    endif()
    if(DAWLAB_ONLY AND NOT lesson IN_LIST DAWLAB_ONLY)
        continue()
    endif()
    add_subdirectory(${_dir})
endforeach()
