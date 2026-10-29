cmake_minimum_required(VERSION 3.12)
project(FastFileFinder LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# نیازمندی به Threads برای کار با Thread Pool
find_package(Threads REQUIRED)

include_directories(include)

add_executable(FastFileFinder 
    src/main.cpp 
    src/thread_pool.cpp 
    src/file_searcher.cpp
)

target_link_libraries(FastFileFinder PRIVATE Threads::Threads)