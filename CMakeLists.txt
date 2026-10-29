cmake_minimum_required(VERSION 3.12)
project(hyperfinder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Threads is required for the thread pool.
find_package(Threads REQUIRED)

include_directories(include)

add_executable(hyperfinder
    src/main.cpp
    src/thread_pool.cpp
    src/file_searcher.cpp
)

target_link_libraries(hyperfinder PRIVATE Threads::Threads)
