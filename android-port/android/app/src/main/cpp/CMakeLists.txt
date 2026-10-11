# 'Splosion Man - Android native build (arm64-v8a).
#
# Layout (relative to this file):
#   <root>/sdk    - ReXGlue SDK source (house-patched + Android patch set)
#   <root>/game   - SplosionManRecomp game project (generated/ committed,
#                   assets/default.xex deliberately ABSENT: compile the
#                   committed generated tree, never run codegen here)
#   <root>/android/app/src/main/cpp - this file + Android-only sources
#
# Produces libmain.so (game + SDL + entry point). librexruntime.so and
# librexgpu-xenos.so are built by the SDK subproject and packaged by AGP
# alongside it.
cmake_minimum_required(VERSION 3.22.1)
project(splosionman_android LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

get_filename_component(TP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
set(REXSDK_DIR "${TP_ROOT}/sdk" CACHE PATH "ReXGlue SDK source tree")
set(GAME_DIR "${TP_ROOT}/game" CACHE PATH "'Splosion Man game project")

# Android build options: no FidelityFX (desktop upscaling path), no tests,
# no Tracy (keep the APK lean and the link simple).
set(REXGLUE_ENABLE_FIDELITYFX OFF CACHE BOOL "" FORCE)
set(REXGLUE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(REXGLUE_ENABLE_TRACY OFF CACHE BOOL "" FORCE)
set(REXGLUE_ENABLE_PERF_COUNTERS OFF CACHE BOOL "" FORCE)

add_subdirectory("${REXSDK_DIR}" "${CMAKE_BINARY_DIR}/rexglue-sdk")

# --- Recompiled guest code (committed generated tree) -------------------
include("${GAME_DIR}/generated/default/sources.cmake")
add_library(splosionman_recomp OBJECT ${GENERATED_SOURCES})
target_include_directories(splosionman_recomp PRIVATE
    "${GAME_DIR}"
    "${GAME_DIR}/src"
    "${GAME_DIR}/generated/default"
)
target_link_libraries(splosionman_recomp PRIVATE rex::runtime)
target_precompile_headers(splosionman_recomp PRIVATE
    "${GAME_DIR}/generated/default/splosionman_pch.h")

# --- Host application (libmain.so) --------------------------------------
add_library(main SHARED
    "${GAME_DIR}/src/main.cpp"
    "${GAME_DIR}/src/stubs.cpp"
    "${REXSDK_DIR}/src/ui/rex_app.cpp"
    android_main.cpp
    touch_controls.cpp
    asset_wizard.cpp
    stfs_package.cpp
    sha1.cpp
)
target_include_directories(main PRIVATE
    "${GAME_DIR}"
    "${GAME_DIR}/src"
    "${GAME_DIR}/generated/default"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${REXSDK_DIR}/thirdparty/imgui"
)
# rexglue_configure_target() normally provides this for host apps (the SDK
# root only sets it inside its own directory tree).
target_compile_definitions(main PRIVATE REXGLUE_BUILD_CONFIG="$<CONFIG>")
target_link_libraries(main PRIVATE
    splosionman_recomp
    rex::runtime
)
if(TARGET imgui::imgui)
    target_link_libraries(main PRIVATE imgui::imgui)
elseif(TARGET imgui)
    target_link_libraries(main PRIVATE imgui)
endif()
if(TARGET rex::ui)
    target_link_libraries(main PRIVATE rex::ui)
endif()
if(TARGET SDL3::SDL3)
    target_link_libraries(main PRIVATE SDL3::SDL3)
endif()

# The Xenos GPU plugin is dlopen()ed at runtime, never linked - but it must
# be built and packaged next to libmain.so.
if(TARGET rexgpu-xenos)
    add_dependencies(main rexgpu-xenos)
endif()

rexglue_apply_target_settings(main)
