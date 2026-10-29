cmake_minimum_required(VERSION 3.16)
project(hyperfinder VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

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
