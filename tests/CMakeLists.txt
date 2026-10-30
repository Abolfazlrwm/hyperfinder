include(FetchContent)

# doctest is a single-header framework with a tiny CMake target. FetchContent
# keeps the repository free of vendored third-party code. Fetched over git
# rather than as a tarball: the CMake bundled with some MinGW distributions has
# no CA trust store and fails the HTTPS download.
# We provide our own main (doctest_main.cpp), and doctest's install rules are
# not wanted in a project that only consumes the header.
set(DOCTEST_WITH_MAIN_IN_STATIC_LIB OFF CACHE BOOL "" FORCE)
set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.5.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(doctest)

add_executable(hyperfinder_tests
    doctest_main.cpp
    temp_tree.cpp
    reference.cpp
    test_match.cpp
    test_thread_pool.cpp
    test_file_searcher.cpp
    test_filters.cpp
    test_cli.cpp
    test_stress.cpp
)
target_link_libraries(hyperfinder_tests PRIVATE hyperfinder::core doctest::doctest)
hyperfinder_set_warnings(hyperfinder_tests)

add_test(NAME hyperfinder_tests COMMAND hyperfinder_tests)
# The 200-run stress test is the slow part, especially under a sanitizer.
set_tests_properties(hyperfinder_tests PROPERTIES TIMEOUT 900)
