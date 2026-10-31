cmake_minimum_required(VERSION 3.21)

project(bemotion
    VERSION 0.1.0
    DESCRIPTION "Free, open source video editor with a focus on 2D animation"
    LANGUAGES C CXX)

# The pre-release part of the version ("beta.2"; empty for a release). With it the version reads
# 0.1.0-beta.2: in the app, the package names, the release tag (v0.1.0-beta.2) and the updater's
# comparisons (core/Updates.h). tools/package-*.sh read this line.
set(BEMOTION_VERSION_SUFFIX "beta.2")
if(BEMOTION_VERSION_SUFFIX)
    set(BEMOTION_FULL_VERSION "${PROJECT_VERSION}-${BEMOTION_VERSION_SUFFIX}")
else()
    set(BEMOTION_FULL_VERSION "${PROJECT_VERSION}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(BEMOTION_BUILD_TESTS "Build the core unit tests" ON)

include(cmake/Dependencies.cmake)

add_subdirectory(libs/bestyle) # the look, reusable by other Qt apps
add_subdirectory(src)

if(BEMOTION_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
