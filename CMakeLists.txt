cmake_minimum_required(VERSION 3.22)

project(DawLab
    VERSION 0.1.0
    DESCRIPTION "C++ audio DSP course: from gain / filter / EQ / dynamics / saturation to VST hosting and an 8-track DAW"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Put all executables in build/bin (multi-config generators add a Debug/Release subfolder)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(MSVC)
    # Sources may contain non-ASCII characters; MSVC needs /utf-8 to read them correctly
    add_compile_options(/utf-8)
endif()

option(DAWLAB_WITH_JUCE "Build the chapters that need JUCE (13 VST hosting, 14 DAW app)" OFF)
set(DAWLAB_JUCE_TAG "9.0.4" CACHE STRING "JUCE version (git tag) for FetchContent to download")
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE/CMakeLists.txt)
    set(_juce_default ${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE)
else()
    set(_juce_default "")
endif()
set(DAWLAB_JUCE_DIR "${_juce_default}" CACHE PATH "Path to a local JUCE source tree; leave empty to download from GitHub with FetchContent")
set(DAWLAB_ONLY "" CACHE STRING "Build only the listed chapters (semicolon-separated folder names, e.g. 02-filter;03-eq); empty = all")

enable_testing()
include(cmake/DawLab.cmake)

# ---- Shared libraries ----------------------------------------------------
# dawlab::common : AudioBuffer, WAV I/O, test signals, analysis tools, self-check
add_library(dawlab_common INTERFACE)
add_library(dawlab::common ALIAS dawlab_common)
target_include_directories(dawlab_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/common/include)

# dawlab::dsp : the DSP components that grow chapter by chapter (header-only)
add_library(dawlab_dsp INTERFACE)
add_library(dawlab::dsp ALIAS dawlab_dsp)
target_include_directories(dawlab_dsp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/dsp/include)
target_link_libraries(dawlab_dsp INTERFACE dawlab_common)

# Tests for the shared utilities themselves
_dawlab_add_program(common_selftest common common/tests/test_common.cpp)

# ---- JUCE (only chapters 13 and 14 need it) ------------------------------
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

# ---- Chapters ------------------------------------------------------------
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
    14-daw-8track
    15-daw-growing)

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
