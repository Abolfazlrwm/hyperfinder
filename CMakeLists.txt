# 3.21 is the floor because CMakePresets.json uses format version 3, which
# CMake older than 3.21 cannot read.
cmake_minimum_required(VERSION 3.21)

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

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# ---------------------------------------------------------------------------
# Third-party dependencies
# ---------------------------------------------------------------------------
include(FetchContent)

# Fetched over git rather than as a tarball: the CMake bundled with some MinGW
# distributions has no CA trust store and fails the HTTPS download. That means
# the git on PATH has to have one too - on Windows put Git for Windows ahead of
# the WinLibs git.
set(CLI11_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CLI11_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CLI11_INSTALL OFF CACHE BOOL "" FORCE)
set(CLI11_PRECOMPILED OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.7.2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(cli11)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(HYPERFINDER_BUILD_TESTS "Build the test suite" ON)
option(HYPERFINDER_ENABLE_SANITIZERS "Build with the sanitizers below" OFF)
set(HYPERFINDER_SANITIZER "address,undefined" CACHE STRING
    "Which sanitizers to enable: address, thread, undefined, or address,undefined")

# ---------------------------------------------------------------------------
# Sanitizers
# ---------------------------------------------------------------------------
# AddressSanitizer and UndefinedBehaviorSanitizer work with GCC and Clang on
# Linux and macOS. MinGW ships none of the sanitizer runtimes (no libasan,
# libubsan or libtsan), so the option is rejected up front instead of failing
# at link time with "cannot find -lasan".
if(HYPERFINDER_ENABLE_SANITIZERS)
    if(MSVC)
        message(FATAL_ERROR
            "HYPERFINDER_ENABLE_SANITIZERS is not supported with MSVC; "
            "use GCC or Clang.")
    endif()
    if(NOT HYPERFINDER_SANITIZER MATCHES "^(address|thread|undefined|address,undefined)$")
        message(FATAL_ERROR
            "Unsupported HYPERFINDER_SANITIZER='${HYPERFINDER_SANITIZER}'. "
            "Use address, thread, undefined, or address,undefined.")
    endif()
    if(MINGW)
        message(FATAL_ERROR
            "MinGW does not ship the sanitizer runtimes, so "
            "HYPERFINDER_ENABLE_SANITIZERS cannot be used here. Build with GCC "
            "or Clang on Linux or macOS (the CI sanitizer job does exactly that).")
    endif()

    add_compile_options(-fsanitize=${HYPERFINDER_SANITIZER} -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=${HYPERFINDER_SANITIZER})
    message(STATUS "Sanitizers enabled: ${HYPERFINDER_SANITIZER}")
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
    src/cli.cpp
    src/file_searcher.cpp
    src/match.cpp
    src/thread_pool.cpp
)
add_library(hyperfinder::core ALIAS hyperfinder_core)
# Aliases are never exported, so the installed package would otherwise expose
# hyperfinder::hyperfinder_core. EXPORT_NAME makes the installed target
# hyperfinder::core, matching the alias used inside this build.
set_target_properties(hyperfinder_core PROPERTIES EXPORT_NAME core)

# CLI11 is header-only and used only from cli.cpp, so it stays PRIVATE and is
# wrapped in BUILD_INTERFACE: an installed static library has no CLI11 symbols
# left to resolve, and without the wrapper CMake insists on exporting the
# CLI11 target as part of our package.
target_compile_definitions(hyperfinder_core PRIVATE HYPERFINDER_VERSION="${PROJECT_VERSION}")
target_link_libraries(hyperfinder_core PRIVATE $<BUILD_INTERFACE:CLI11::CLI11>)

target_include_directories(hyperfinder_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_features(hyperfinder_core PUBLIC cxx_std_17)
target_link_libraries(hyperfinder_core PUBLIC Threads::Threads)
hyperfinder_set_warnings(hyperfinder_core)

# ---------------------------------------------------------------------------
# Command-line front end
# ---------------------------------------------------------------------------
add_executable(hyperfinder
    src/main.cpp
    src/output.cpp
)
target_link_libraries(hyperfinder PRIVATE hyperfinder::core)
# CommandLineToArgvW, used to read the UTF-16 command line on Windows.
if(WIN32)
    target_link_libraries(hyperfinder PRIVATE shell32)
endif()
hyperfinder_set_warnings(hyperfinder)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(HYPERFINDER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Install rules and an exported package config
# ---------------------------------------------------------------------------
# Everything goes into the standard GNU directories, so the default prefix
# (/usr/local) can be overridden with -DCMAKE_INSTALL_PREFIX. The export set
# carries the hyperfinder:: namespace, which means a downstream project does:
#
#     find_package(hyperfinder REQUIRED)
#     target_link_libraries(app PRIVATE hyperfinder::core)
#
# Both the library and the executable are installed, but only the library is
# exported: a downstream project links against hyperfinder::core and has no
# reason to reference the command-line tool as a build target.
install(
    TARGETS hyperfinder_core
    EXPORT hyperfinderTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(
    TARGETS hyperfinder
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/hyperfinder DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(
    EXPORT hyperfinderTargets
    FILE hyperfinderTargets.cmake
    NAMESPACE hyperfinder::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hyperfinder
)

configure_package_config_file(
    cmake/hyperfinderConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/hyperfinderConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hyperfinder
)

# SameMajorVersion: 0.x releases may break the API, so a consumer asking for
# 0.1 must not silently get 0.2.
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/hyperfinderConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/hyperfinderConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/hyperfinderConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hyperfinder
)
