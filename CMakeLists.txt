cmake_minimum_required(VERSION 3.16)

project(hyperfinder
    VERSION 0.1.0
    DESCRIPTION "Multi-threaded file finder"
    HOMEPAGE_URL "https://github.com/Abolfazlrwm/hyperfinder"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Single-config generators (Makefiles, Ninja) leave CMAKE_BUILD_TYPE empty when
# the user does not pass one, which silently produces an unoptimised binary
# with no debug information either. Default to Release so that a plain
# "cmake -S . -B build && cmake --build build" does the obvious thing.
# Multi-config generators (Visual Studio, Xcode) choose the configuration at
# build time instead and are left alone.
get_property(HYPERFINDER_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT HYPERFINDER_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING
        "Build type: Debug, Release, RelWithDebInfo or MinSizeRel" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
    message(STATUS "No build type specified, defaulting to Release")
endif()

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(HYPERFINDER_BUILD_TESTS "Build the test suite" ON)
set(HYPERFINDER_SANITIZER "" CACHE STRING
    "Build with a sanitizer: address, thread, undefined, or address,undefined")

# ---------------------------------------------------------------------------
# Sanitizers
# ---------------------------------------------------------------------------
# AddressSanitizer and UndefinedBehaviorSanitizer work with GCC and Clang on
# Linux and macOS. MinGW ships none of the sanitizer runtimes (no libasan,
# libubsan or libtsan), so the option is rejected up front instead of failing
# at link time with "cannot find -lasan".
if(HYPERFINDER_SANITIZER)
    if(MSVC)
        message(FATAL_ERROR
            "HYPERFINDER_SANITIZER is not supported with MSVC; use GCC or Clang.")
    endif()
    if(NOT HYPERFINDER_SANITIZER MATCHES "^(address|thread|undefined|address,undefined)$")
        message(FATAL_ERROR
            "Unsupported HYPERFINDER_SANITIZER='${HYPERFINDER_SANITIZER}'. "
            "Use address, thread, undefined, or address,undefined.")
    endif()
    if(MINGW)
        message(FATAL_ERROR
            "MinGW does not ship the sanitizer runtimes, so HYPERFINDER_SANITIZER "
            "cannot be used here. Build with GCC or Clang on Linux or macOS "
            "(the CI sanitizer job does exactly that).")
    endif()

    add_compile_options(-fsanitize=${HYPERFINDER_SANITIZER} -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=${HYPERFINDER_SANITIZER})
    message(STATUS "Sanitizer enabled: ${HYPERFINDER_SANITIZER}")
endif()

# ---------------------------------------------------------------------------
# Strict warnings: the project is expected to build without a single one.
# ---------------------------------------------------------------------------
function(hyperfinder_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# Core library: everything except the command-line front end. The tests link
# against this, so they exercise exactly the code the executable ships.
# ---------------------------------------------------------------------------
add_library(hyperfinder_core STATIC
    src/file_searcher.cpp
    src/thread_pool.cpp
)
target_include_directories(hyperfinder_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(hyperfinder_core PUBLIC Threads::Threads)
hyperfinder_set_warnings(hyperfinder_core)

# ---------------------------------------------------------------------------
# Command-line front end
# ---------------------------------------------------------------------------
add_executable(hyperfinder src/main.cpp)
target_link_libraries(hyperfinder PRIVATE hyperfinder_core)
hyperfinder_set_warnings(hyperfinder)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(HYPERFINDER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
