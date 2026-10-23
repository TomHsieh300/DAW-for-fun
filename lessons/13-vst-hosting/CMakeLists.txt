# Chapter 13: VST3 plugins - build one yourself, then host it yourself (requires JUCE)
#
# Build: .\scripts\build.ps1 -Only 13-vst-hosting -Juce
#
#   plugin/   DawLabCompressor, DawLabLimiter (juce_add_plugin: VST3 + Standalone)
#   host/     dawlab_plugin_host static library (PluginHost / HostedPlugin / PluginWindow, also used by chapter 14)
#   examples/ l13_scan_plugins, l13_offline_host, l13_state_roundtrip (ctest label: example), l13_gui_host (GUI, not run as a test)
#   exercises/starter, solutions/  exercises 4-7 (JUCE console apps)
#
# Executables go to <build>/bin/<Config>/ (same as the other chapters); the VST3 bundle is at
# <build>/lessons/13-vst-hosting/plugin/<name>_artefacts/<Config>/VST3/<name>.vst3

if(NOT DAWLAB_WITH_JUCE)
    message(STATUS "13-vst-hosting: requires JUCE, skipped (use -DDAWLAB_WITH_JUCE=ON or build.ps1 -Juce)")
    return()
endif()

add_subdirectory(plugin)   # sets L13_COMPRESSOR_VST3 / L13_LIMITER_VST3 (bundle paths)
add_subdirectory(host)

# l13_add_juce_program(<target> <label> <source>)
#   Creates a JUCE console app, links dawlab_plugin_host, and compiles this chapter's plugin paths
#   into the program (DAWLAB_L13_COMPRESSOR_VST3 / DAWLAB_L13_LIMITER_VST3), so it runs with no arguments.
#   When label is example / solution it is registered as a ctest; an empty string (starter) only builds.
function(l13_add_juce_program target label source)
    juce_add_console_app(${target} PRODUCT_NAME ${target})
    target_sources(${target} PRIVATE ${source})
    target_link_libraries(${target} PRIVATE dawlab_plugin_host)
    target_compile_definitions(${target} PRIVATE
        "DAWLAB_L13_COMPRESSOR_VST3=\"${L13_COMPRESSOR_VST3}\""
        "DAWLAB_L13_LIMITER_VST3=\"${L13_LIMITER_VST3}\"")
    # Same as the other chapters: <build>/bin/<Config>/
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
        FOLDER "13-vst-hosting")
    dawlab_set_warnings(${target})
    # The plugins must already be built before running
    add_dependencies(${target} DawLabCompressor_VST3 DawLabLimiter_VST3)
    if(label)
        add_test(NAME ${target} COMMAND ${target})
        set_tests_properties(${target} PROPERTIES
            LABELS "${label}"
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            TIMEOUT 300)
    endif()
endfunction()

# ---- Examples (ctest label: example) -------------------------------------
l13_add_juce_program(l13_scan_plugins      example examples/scan_plugins.cpp)
l13_add_juce_program(l13_offline_host      example examples/offline_host.cpp)
l13_add_juce_program(l13_state_roundtrip   example examples/state_roundtrip.cpp)

# GUI host: opens a window and plays through AudioDeviceManager + AudioProcessorPlayer. Not registered as a ctest.
juce_add_gui_app(l13_gui_host PRODUCT_NAME "l13_gui_host")
target_sources(l13_gui_host PRIVATE examples/gui_host.cpp)
target_link_libraries(l13_gui_host PRIVATE dawlab_plugin_host)
target_compile_definitions(l13_gui_host PRIVATE
    "DAWLAB_L13_COMPRESSOR_VST3=\"${L13_COMPRESSOR_VST3}\""
    "DAWLAB_L13_LIMITER_VST3=\"${L13_LIMITER_VST3}\""
    JUCE_APPLICATION_NAME_STRING="l13_gui_host"
    JUCE_APPLICATION_VERSION_STRING="1.0.0")
set_target_properties(l13_gui_host PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    FOLDER "13-vst-hosting")
dawlab_set_warnings(l13_gui_host)
add_dependencies(l13_gui_host DawLabCompressor_VST3 DawLabLimiter_VST3)

# ---- Exercise starter code (build only, no tests) ------------------------
l13_add_juce_program(l13_ex4_starter  "" exercises/starter/ex4_automation.cpp)
l13_add_juce_program(l13_ex5_starter  "" exercises/starter/ex5_preset_file.cpp)
l13_add_juce_program(l13_ex6_starter  "" exercises/starter/ex6_graph_chain.cpp)
l13_add_juce_program(l13_ex7_starter  "" exercises/starter/ex7_bypass_crossfade.cpp)

# ---- Exercise solutions (ctest label: solution) --------------------------
l13_add_juce_program(l13_ex4_solution solution solutions/ex4_automation.cpp)
l13_add_juce_program(l13_ex5_solution solution solutions/ex5_preset_file.cpp)
l13_add_juce_program(l13_ex6_solution solution solutions/ex6_graph_chain.cpp)
l13_add_juce_program(l13_ex7_solution solution solutions/ex7_bypass_crossfade.cpp)
