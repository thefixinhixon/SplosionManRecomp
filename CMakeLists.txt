# splosionman - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(splosionman LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Linux needs SSSE3/SSE4.1 for the SDK's memory.cpp (same as condemned2)
if(NOT WIN32)
    add_compile_options(-mssse3 -msse4.1)
endif()

include(generated/rexglue.cmake)

# Sources
set(SPLOSIONMAN_SOURCES
    src/main.cpp
    src/stubs.cpp
    src/splosionman_app.cpp
    src/options_menu.cpp
    src/recomp_options.cpp
    src/script_fixes.cpp
)

if(WIN32)
    add_executable(splosionman WIN32 ${SPLOSIONMAN_SOURCES})
else()
    add_executable(splosionman ${SPLOSIONMAN_SOURCES})
endif()

rexglue_setup_target(splosionman)

# imgui headers ship inside the SDK tree but aren't added to the include
# path by rexglue_setup_target; add them explicitly (same as condemned2).
if(REXSDK_DIR)
    target_include_directories(splosionman PRIVATE
        "${REXSDK_DIR}/thirdparty/imgui"
    )
endif()

# Dear ImGui is compiled into rexruntime (through rexui). On Windows the exe
# must use that one copy: linking the imgui OBJECT library here as well gives
# splosionman.exe a second ImGui with its own GImGui context that nothing ever
# sets, so the first ImGui call from game code (startup hint, F1 options menu,
# FPS overlay) dereferences a null context and crashes (v2.2, issue #2). With
# WINDOWS_EXPORT_ALL_SYMBOLS on rexruntime the ImGui functions resolve through
# its import library instead; the game only includes imgui.h (functions, no
# data symbols), so no dllimport is needed. On Linux the dynamic linker merges
# the two copies, so the link stays as it was there.
if(NOT WIN32)
    if(TARGET imgui::imgui)
        target_link_libraries(splosionman PRIVATE imgui::imgui)
    elseif(TARGET imgui)
        target_link_libraries(splosionman PRIVATE imgui)
    endif()
endif()
