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
    test_json.cpp
    test_stress.cpp
)
target_link_libraries(hyperfinder_tests PRIVATE hyperfinder::core hyperfinder_runtime doctest::doctest)
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
    # EMPTY_STDOUT / EMPTY_STDERR are boolean flags, not values: a bare keyword
    # marks the assertion. Treating them as one-value keywords made them swallow
    # the next token, so "EMPTY_STDOUT STDERR <text>" silently dropped the
    # stderr check.
    cmake_parse_arguments(CASE
        "EMPTY_STDOUT;EMPTY_STDERR"
        "CODE;STDOUT;STDOUT_EXACT;UNEXPECT_STDOUT;STDERR;ENV"
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
    if(CASE_ENV)
        list(APPEND definitions "-DENV_SPEC=${CASE_ENV}")
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

# --color=always forces the escape sequences even though CTest pipes stdout.
hyperfinder_cli_test(cli_color_always
    CODE 0 ARGS --color=always|a.txt|${HYPERFINDER_CLI_FIXTURE}
    STDOUT "${hyperfinder_escape}")

# NO_COLOR disables colour regardless of the requested mode.
hyperfinder_cli_test(cli_color_no_color_env
    CODE 0 ARGS a.txt|${HYPERFINDER_CLI_FIXTURE}
    ENV "NO_COLOR=1" UNEXPECT_STDOUT "${hyperfinder_escape}")

# --color=auto stays plain when stdout is not a terminal (i.e. piped).
hyperfinder_cli_test(cli_color_auto_piped
    CODE 0 ARGS --color=auto|a.txt|${HYPERFINDER_CLI_FIXTURE}
    UNEXPECT_STDOUT "${hyperfinder_escape}")

# ---------------------------------------------------------------------------
# Step 1: matching modes
# ---------------------------------------------------------------------------
hyperfinder_cli_test(cli_glob
    CODE 0 ARGS --count|--glob|*.txt|${HYPERFINDER_CLI_FIXTURE}
    STDOUT_EXACT 7)

hyperfinder_cli_test(cli_regex
    CODE 0 ARGS --count|--regex|^a.*|${HYPERFINDER_CLI_FIXTURE}
    STDOUT_EXACT 1)

hyperfinder_cli_test(cli_invalid_regex
    CODE 2 ARGS --regex|*abc|${HYPERFINDER_CLI_FIXTURE}
    EMPTY_STDOUT STDERR "error:")

# ---------------------------------------------------------------------------
# Step 2: size and modification-time filters
#
# Every fixture file is tiny (25-26 bytes, depending on whether CMake writes LF
# or CRLF) and was written at configure time. The sizes below are deliberately
# far from that boundary so the counts are identical on every platform; the
# exact inclusive boundary is covered by the C++ unit tests instead.
# ---------------------------------------------------------------------------
hyperfinder_cli_test(cli_min_size_excludes_small
    CODE 1 ARGS --count|--min-size|1k|.|${HYPERFINDER_CLI_FIXTURE}
    STDOUT_EXACT 0)

hyperfinder_cli_test(cli_max_size_keeps_all
    CODE 0 ARGS --count|--max-size|1k|.|${HYPERFINDER_CLI_FIXTURE}
    STDOUT_EXACT 10)

hyperfinder_cli_test(cli_min_size_keeps_all
    CODE 0 ARGS --count|--min-size|1|.|${HYPERFINDER_CLI_FIXTURE}
    STDOUT_EXACT 10)

hyperfinder_cli_test(cli_max_size_excludes_all
    CODE 1 ARGS --count|--max-size|1|.|${HYPERFINDER_CLI_FIXTURE}
    STDOUT_EXACT 0)

hyperfinder_cli_test(cli_newer_than_past_date
    CODE 0 ARGS --count|--newer-than|2000-01-01|.|${HYPERFINDER_CLI_FIXTURE}
    STDOUT_EXACT 10)

hyperfinder_cli_test(cli_newer_than_future_date
    CODE 1 ARGS --count|--newer-than|2999-01-01|.|${HYPERFINDER_CLI_FIXTURE}
    STDOUT_EXACT 0)

hyperfinder_cli_test(cli_invalid_size
    CODE 2 ARGS --min-size|abc|needle|${HYPERFINDER_CLI_FIXTURE}
    EMPTY_STDOUT STDERR "size")

hyperfinder_cli_test(cli_invalid_newer_than
    CODE 2 ARGS --newer-than|7x|needle|${HYPERFINDER_CLI_FIXTURE}
    EMPTY_STDOUT STDERR "unit")

# ---------------------------------------------------------------------------
# Step 3: content search
#
# Every fixture file contains "hyperfinder test fixture".
# ---------------------------------------------------------------------------
hyperfinder_cli_test(cli_content_finds_all
    CODE 0 ARGS --count|--content|hyperfinder|${HYPERFINDER_CLI_FIXTURE}
    STDOUT_EXACT 10)

hyperfinder_cli_test(cli_content_reports_line_and_text
    CODE 0 ARGS --content|hyperfinder|${HYPERFINDER_CLI_FIXTURE}
    STDOUT "a\\.txt:1:hyperfinder test fixture")

hyperfinder_cli_test(cli_content_no_match
    CODE 1 ARGS --count|--content|zzzabsent|${HYPERFINDER_CLI_FIXTURE}
    STDOUT_EXACT 0)

hyperfinder_cli_test(cli_content_is_case_insensitive
    CODE 0 ARGS --count|--content|HYPERFINDER|${HYPERFINDER_CLI_FIXTURE}
    STDOUT_EXACT 10)

hyperfinder_cli_test(cli_content_case_sensitive
    CODE 1 ARGS --count|--case-sensitive|--content|HYPERFINDER|${HYPERFINDER_CLI_FIXTURE}
    STDOUT_EXACT 0)

# ---------------------------------------------------------------------------
# Step 4: output formats
# ---------------------------------------------------------------------------
hyperfinder_cli_test(cli_json_name
    CODE 0 ARGS --json|a.txt|${HYPERFINDER_CLI_FIXTURE}
    STDOUT "\"path\":\".*a\\.txt\"")

hyperfinder_cli_test(cli_json_content
    CODE 0 ARGS --json|--content|hyperfinder|${HYPERFINDER_CLI_FIXTURE}
    STDOUT "\"line\":1,\"text\":\"hyperfinder")

hyperfinder_cli_test(cli_count_json_rejected
    CODE 2 ARGS --count|--json|.|${HYPERFINDER_CLI_FIXTURE}
    EMPTY_STDOUT STDERR "cannot be combined")

# --print0 writes NUL-terminated records; the harness works on text, so a small
# script checks the raw bytes instead.
add_test(NAME cli_print0
    COMMAND ${CMAKE_COMMAND}
            -DEXE=$<TARGET_FILE:hyperfinder>
            -DFIXTURE=${HYPERFINDER_CLI_FIXTURE}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_print0_case.cmake)

# ---------------------------------------------------------------------------
# Step 6: CLI polish. The summary uses singular/plural correctly and a
# human-readable duration, and it stays on stderr.
# ---------------------------------------------------------------------------
add_test(NAME cli_summary_singular
    COMMAND ${CMAKE_COMMAND}
            -DEXE=$<TARGET_FILE:hyperfinder>
            -DTREE=${CMAKE_CURRENT_BINARY_DIR}/summary_tree
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_summary_case.cmake)

# Removing every escape from the --color=always output yields the --color=never
# output byte for byte.
add_test(NAME cli_color_strip
    COMMAND ${CMAKE_COMMAND}
            -DEXE=$<TARGET_FILE:hyperfinder>
            -DFIXTURE=${HYPERFINDER_CLI_FIXTURE}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cli_color_strip.cmake)

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
    # The test binary links the same runtime, so `ctest` works from a shell with
    # no MinGW on PATH. --count makes doctest list the case count and exit.
    add_test(NAME cli_static_runtime_tests
        COMMAND ${CMAKE_COMMAND}
                -DEXE=$<TARGET_FILE:hyperfinder_tests>
                -DARGS=--count
                -DSTATIC_RUNTIME=${HYPERFINDER_STATIC_RUNTIME}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/check_mingw_runtime.cmake)
endif()

