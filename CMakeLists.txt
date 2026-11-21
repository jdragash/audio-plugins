cmake_minimum_required(VERSION 3.22)

# Jake's audio plugins. Each plugin lives in plugins/<name>/ with its own
# version, changelog and tests. Code shared between plugins lives in shared/.
#
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # every plugin
#   cmake -S . -B build -G Ninja -DPLUGINS=puppy-chow         # just one (or a ;-list)
project(AudioPlugins LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

# Build a universal binary (Apple Silicon + Intel) on macOS unless told otherwise.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

enable_testing()

include(cmake/JUCE.cmake)   # the one copy of JUCE every plugin uses
add_subdirectory(shared)

set(PLUGINS "" CACHE STRING "Plugins to build, by folder name under plugins/ (empty = all)")
if(PLUGINS)
    set(selected ${PLUGINS})
else()
    file(GLOB plugin_dirs RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}/plugins
        ${CMAKE_CURRENT_SOURCE_DIR}/plugins/*/CMakeLists.txt)
    list(TRANSFORM plugin_dirs REPLACE "/CMakeLists.txt$" "")
    list(SORT plugin_dirs)
    set(selected ${plugin_dirs})
endif()

foreach(plugin IN LISTS selected)
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/plugins/${plugin}/CMakeLists.txt)
        message(FATAL_ERROR "No plugin called '${plugin}' in plugins/")
    endif()
    message(STATUS "Building plugin: ${plugin}")
    add_subdirectory(plugins/${plugin})
endforeach()
