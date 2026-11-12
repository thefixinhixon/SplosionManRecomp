# SplosionManRecomp launcher: finds or imports the game data, holds the game
# options and starts the game. Qt 6 Widgets, C++20. MIT licensed (LICENSE).
cmake_minimum_required(VERSION 3.21)
project(SplosionManRecompLauncher LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

set(SMR_VERSION "dev" CACHE STRING "Version shown in the launcher")
option(SMR_LAUNCHER_TOOLS "Also build the stfs_tool test program" OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
if(UNIX AND NOT APPLE)
    find_package(Qt6 6.5 REQUIRED COMPONENTS DBus)
endif()

add_library(smr_stfs STATIC src/stfs.cpp src/stfs.h)
target_link_libraries(smr_stfs PUBLIC Qt6::Core)

set(SOURCES
    src/main.cpp
    src/art.cpp src/art.h
    src/gamedata.cpp src/gamedata.h
    src/launch.cpp src/launch.h
    src/mainwindow.cpp src/mainwindow.h
    src/options.cpp src/options.h
    src/theme.cpp src/theme.h)
if(WIN32)
    list(APPEND SOURCES resources/app.rc)
endif()

add_executable(SplosionManRecomp WIN32 ${SOURCES})
target_compile_definitions(SplosionManRecomp PRIVATE SMR_VERSION="${SMR_VERSION}")
target_link_libraries(SplosionManRecomp PRIVATE smr_stfs Qt6::Widgets)
if(UNIX AND NOT APPLE)
    target_link_libraries(SplosionManRecomp PRIVATE Qt6::DBus)
endif()

if(SMR_LAUNCHER_TOOLS)
    add_executable(stfs_tool tests/stfs_tool.cpp)
    target_link_libraries(stfs_tool PRIVATE smr_stfs)
endif()
