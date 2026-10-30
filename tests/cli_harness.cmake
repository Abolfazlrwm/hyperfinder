# Runs the real executable once and checks what a user would see.
#
# Invoked by CTest as:
#   cmake -DEXE=<exe> -DARGS=<a|b|c> -DEXPECT_CODE=<n> [-D...] -P cli_harness.cmake
#
# ARGS uses '|' instead of ';' because ';' is CMake's list separator and would
# split the arguments before they reach add_test.

if(NOT DEFINED EXE)
    message(FATAL_ERROR "EXE must be set")
endif()
if(NOT DEFINED EXPECT_CODE)
    message(FATAL_ERROR "EXPECT_CODE must be set")
endif()

set(arguments)
if(DEFINED ARGS AND NOT "${ARGS}" STREQUAL "")
    string(REPLACE "|" ";" arguments "${ARGS}")
endif()

# An optional NAME=VALUE environment override for the child, used to simulate
# NO_COLOR without changing the test runner's own environment.
set(command "${EXE}" ${arguments})
if(DEFINED ENV_SPEC AND NOT "${ENV_SPEC}" STREQUAL "")
    set(command ${CMAKE_COMMAND} -E env "${ENV_SPEC}" ${command})
endif()

execute_process(
    COMMAND ${command}
    OUTPUT_VARIABLE stdout_text
    ERROR_VARIABLE stderr_text
    RESULT_VARIABLE exit_code
)

# Trailing whitespace is irrelevant for the "exactly this number" checks.
string(STRIP "${stdout_text}" stdout_stripped)

set(failures)

if(NOT "${exit_code}" STREQUAL "${EXPECT_CODE}")
    list(APPEND failures "exit code was '${exit_code}', expected '${EXPECT_CODE}'")
endif()

if(DEFINED EXPECT_STDOUT AND NOT "${stdout_text}" MATCHES "${EXPECT_STDOUT}")
    list(APPEND failures "stdout did not match '${EXPECT_STDOUT}'")
endif()

if(DEFINED UNEXPECT_STDOUT AND "${stdout_text}" MATCHES "${UNEXPECT_STDOUT}")
    list(APPEND failures "stdout unexpectedly matched '${UNEXPECT_STDOUT}'")
endif()

if(DEFINED EXPECT_STDOUT_EXACT AND NOT "${stdout_stripped}" STREQUAL "${EXPECT_STDOUT_EXACT}")
    list(APPEND failures "stdout was '${stdout_stripped}', expected '${EXPECT_STDOUT_EXACT}'")
endif()

if(DEFINED EXPECT_EMPTY_STDOUT AND NOT "${stdout_text}" STREQUAL "")
    list(APPEND failures "stdout was expected to be empty, got '${stdout_text}'")
endif()

if(DEFINED EXPECT_STDERR AND NOT "${stderr_text}" MATCHES "${EXPECT_STDERR}")
    list(APPEND failures "stderr did not match '${EXPECT_STDERR}'")
endif()

if(DEFINED EXPECT_EMPTY_STDERR AND NOT "${stderr_text}" STREQUAL "")
    list(APPEND failures "stderr was expected to be empty, got '${stderr_text}'")
endif()

if(failures)
    string(REPLACE ";" "\n  - " detail "${failures}")
    message(FATAL_ERROR
        "CLI case failed:\n  - ${detail}\n"
        "arguments: ${arguments}\n"
        "--- stdout ---\n${stdout_text}\n"
        "--- stderr ---\n${stderr_text}")
endif()
