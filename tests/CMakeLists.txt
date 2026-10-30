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

# ---------------------------------------------------------------------------
# End-to-end tests: run the real executable and check what a user would see.
#
# The fixture is a fixed tree created at configure time, so every expected
# count below is a literal. cli_harness.cmake runs one case and asserts on the
# exit code, stdout and stderr.
# ---------------------------------------------------------------------------
set(HYPERFINDER_CLI_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/cli_fixture")

execute_process(
    COMMAND ${CMAKE_COMMAND}
            -DFIXTURE=${HYPERFINDER_CLI_FIXTURE}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/make_cli_fixture.cmake
    RESULT_VARIABLE hyperfinder_fixture_result
)
if(NOT hyperfinder_fixture_result EQUAL 0)
    message(FATAL_ERROR "could not create the CLI test fixture")
endif()

# Every case is a call to this function. ARGS is a list of arguments; a '|' is
# used as the separator when they are handed to the script.
function(hyperfinder_cli_test name)
    cmake_parse_arguments(CASE ""
        "CODE;STDOUT;STDOUT_EXACT;EMPTY_STDOUT;UNEXPECT_STDOUT;STDERR;EMPTY_STDERR"
        "ARGS"
        ${ARGN})

    string(REPLACE ";" "|" packed_args "${CASE_ARGS}")

    set(definitions
        "-DEXE=$<TARGET_FILE:hyperfinder>"
        "-DARGS=${packed_args}"
        "-DEXPECT_CODE=${CASE_CODE}"
    )
    if(CASE_STDOUT)
        list(APPEND definitions "-DEXPECT_STDOUT=${CASE_STDOUT}")
    endif()
    if(CASE_STDOUT_EXACT)
        list(APPEND definitions "-DEXPECT_STDOUT_EXACT=${CASE_STDOUT_EXACT}")
    endif()
    if(CASE_UNEXPECT_STDOUT)
        list(APPEND definitions "-DUNEXPECT_STDOUT=${CASE_UNEXPECT_STDOUT}")
    endif()
    if(CASE_EMPTY_STDOUT)
        list(APPEND definitions "-DEXPECT_EMPTY_STDOUT=1")
    endif()
    if(CASE_STDERR)
        list(APPEND definitions "-DEXPECT_STDERR=${CASE_STDERR}")
    endif()
    if(CASE_EMPTY_STDERR)
        list(APPEND definitions "-DEXPECT_EMPTY_STDERR=1")
    endif()

    add_test(NAME ${name}
        COMMAND ${CMAKE_COMMAND} ${definitions}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_harness.cmake)
endfunction()

# The fixture holds ten visible files (four of them non-ASCII) and two hidden
# ones; see make_cli_fixture.cmake.

# --help and --version go to stdout and succeed.
hyperfinder_cli_test(cli_help
    CODE 0 ARGS --help
    STDOUT "--max-depth" EMPTY_STDERR)

hyperfinder_cli_test(cli_version
    CODE 0 ARGS --version
    STDOUT "^hyperfinder [0-9]+\\.[0-9]+" EMPTY_STDERR)

# Usage errors go to stderr with exit code 2.
hyperfinder_cli_test(cli_missing_query
    CODE 2 ARGS ""
    EMPTY_STDOUT STDERR "Try 'hyperfinder --help'")

hyperfinder_cli_test(cli_unknown_option
    CODE 2 ARGS --frobnicate|needle
    EMPTY_STDOUT STDERR "frobnicate")

hyperfinder_cli_test(cli_missing_root
    CODE 2 ARGS needle|${HYPERFINDER_CLI_FIXTURE}/does-not-exist
    EMPTY_STDOUT STDERR "not a directory")

# Exit codes: 0 found, 1 nothing found.
hyperfinder_cli_test(cli_finds_a_file
    CODE 0 ARGS a.txt|${HYPERFINDER_CLI_FIXTURE}
    STDOUT "a\\.txt")

hyperfinder_cli_test(cli_reports_no_match
    CODE 1 ARGS zzzznope|${HYPERFINDER_CLI_FIXTURE}
    EMPTY_STDOUT STDERR "0 matches")

# --count prints only the number, and the summary stays on stderr.
hyperfinder_cli_test(cli_count
    CODE 0 ARGS --count|txt|${HYPERFINDER_CLI_FIXTURE}
    STDOUT_EXACT 7 STDERR "scanned in")

hyperfinder_cli_test(cli_count_everything
    CODE 0 ARGS --count|.|${HYPERFINDER_CLI_FIXTURE}
    STDOUT_EXACT 10)

hyperfinder_cli_test(cli_count_only_prints_a_number
    CODE 0 ARGS --count|txt|${HYPERFINDER_CLI_FIXTURE}
    STDOUT_EXACT 7 UNEXPECT_STDOUT "scanned")

# Filters.
hyperfinder_cli_test(cli_max_depth_0
    CODE 0 ARGS --count|--max-depth|0|txt|${HYPERFINDER_CLI_FIXTURE}
    STDOUT_EXACT 5)

hyperfinder_cli_test(cli_max_depth_1
    CODE 0 ARGS --count|--max-depth|1|txt|${HYPERFINDER_CLI_FIXTURE}
    STDOUT_EXACT 6)

hyperfinder_cli_test(cli_ext
    CODE 0 ARGS --count|--ext|log|.|${HYPERFINDER_CLI_FIXTURE}
    STDOUT_EXACT 2)

hyperfinder_cli_test(cli_exclude
    CODE 0 ARGS --count|--exclude|sub|txt|${HYPERFINDER_CLI_FIXTURE}
    STDOUT_EXACT 5)

hyperfinder_cli_test(cli_hidden_skipped_by_default
    CODE 1 ARGS --count|hidden|${HYPERFINDER_CLI_FIXTURE}
    STDOUT_EXACT 0)

hyperfinder_cli_test(cli_hidden_included
    CODE 0 ARGS --count|--hidden|hidden|${HYPERFINDER_CLI_FIXTURE}
    STDOUT_EXACT 2)

# UTF-8 round trip: Persian, accented and emoji names. The literals live in the
# script because a non-ASCII argument does not survive CTest's command line on
# Windows.
add_test(NAME cli_unicode_names
    COMMAND ${CMAKE_COMMAND}
            -DEXE=$<TARGET_FILE:hyperfinder>
            -DFIXTURE=${HYPERFINDER_CLI_FIXTURE}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_unicode_case.cmake)

# --color=never must not emit an escape sequence even when forced on a pipe.
string(ASCII 27 hyperfinder_escape)
hyperfinder_cli_test(cli_color_never
    CODE 0 ARGS --color=never|a.txt|${HYPERFINDER_CLI_FIXTURE}
    STDOUT "a\\.txt" UNEXPECT_STDOUT "${hyperfinder_escape}")

# ---------------------------------------------------------------------------
# Step -1: a MinGW build must not need MinGW DLLs on PATH.
# Only meaningful where we actually use the MinGW toolchain (CI uses MSVC, so
# this guards the local builds and any MinGW CI run).
# ---------------------------------------------------------------------------
if(MINGW AND HYPERFINDER_STATIC_RUNTIME)
    add_test(NAME cli_static_runtime
        COMMAND ${CMAKE_COMMAND}
                -DEXE=$<TARGET_FILE:hyperfinder>
                -DSTATIC_RUNTIME=${HYPERFINDER_STATIC_RUNTIME}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_mingw_runtime.cmake)
endif()

