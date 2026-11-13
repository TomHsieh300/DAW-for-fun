cmake_minimum_required(VERSION 3.22)

project(DawLab
    VERSION 0.1.0
    DESCRIPTION "C++ audio DSP course: from gain / filter / EQ / dynamics / saturation to VST hosting and a DAW with automation"
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

option(DAWLAB_WITH_JUCE "Build the chapters that need JUCE (13 VST hosting, 14-16 DAW apps)" OFF)
set(DAWLAB_JUCE_TAG "9.0.4" CACHE STRING "JUCE version (git tag) for FetchContent to download")
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE/CMakeLists.txt)
    set(_juce_default ${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE)
else()
    set(_juce_default "")
endif()
set(DAWLAB_JUCE_DIR "${_juce_default}" CACHE PATH "Path to a local JUCE source tree; leave empty to download from GitHub with FetchContent")
set(DAWLAB_ONLY "" CACHE STRING "Build only the listed chapters (semicolon-separated folder names, e.g. 02-filter;03-eq, or the groups juce and engine); empty = all")

# A sanitizer for every target, e.g. "thread" (ThreadSanitizer: data races between the UI and the
# audio thread), "address" or "undefined". GCC and Clang only: MSVC has no ThreadSanitizer.
set(DAWLAB_SANITIZE "" CACHE STRING "Build with -fsanitize=<value> (thread, address, undefined); empty = none")
if(DAWLAB_SANITIZE)
    if(MSVC)
        message(FATAL_ERROR "DAWLAB_SANITIZE needs GCC or Clang; on Windows use WSL (scripts/build.sh --sanitize ${DAWLAB_SANITIZE})")
    endif()
    add_compile_options(-fsanitize=${DAWLAB_SANITIZE} -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=${DAWLAB_SANITIZE})
endif()

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
# ... and for the rules every effect in dsp/ shares
_dawlab_add_program(dsp_selftest dsp dsp/tests/test_dsp.cpp)

# ---- JUCE (only chapters 13-20 need it) ----------------------------------
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
# Every folder of lessons/ with a CMakeLists.txt, in the order of their numbers. Nobody lists them by hand: a new
# chapter is a new folder, and the build, the scripts and the CI take it from there.
file(GLOB _lesson_lists RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/lessons ${CMAKE_CURRENT_SOURCE_DIR}/lessons/*/CMakeLists.txt)
set(DAWLAB_LESSONS "")
foreach(_list IN LISTS _lesson_lists)
    get_filename_component(_lesson ${_list} DIRECTORY)
    list(APPEND DAWLAB_LESSONS ${_lesson})
endforeach()
list(SORT DAWLAB_LESSONS)

# DAWLAB_ONLY takes folder names, and the names of two groups that follow from what a chapter has:
#   juce     the chapters whose CMakeLists.txt builds something with JUCE (13 and up)
#   engine   the chapters with a DAW engine of their own, an engine/ folder (14 and up)
foreach(lesson IN LISTS DAWLAB_LESSONS)
    set(_dir ${CMAKE_CURRENT_SOURCE_DIR}/lessons/${lesson})
    if(DAWLAB_ONLY)
        set(_names ${lesson})
        file(READ ${_dir}/CMakeLists.txt _lesson_cmake)
        if(_lesson_cmake MATCHES "DAWLAB_WITH_JUCE")
            list(APPEND _names juce)
        endif()
        if(IS_DIRECTORY ${_dir}/engine)
            list(APPEND _names engine)
        endif()
        set(_wanted FALSE)
        foreach(_name IN LISTS _names)
            if(_name IN_LIST DAWLAB_ONLY)
                set(_wanted TRUE)
            endif()
        endforeach()
        if(NOT _wanted)
            continue()
        endif()
    endif()
    add_subdirectory(${_dir})
endforeach()
