# Creates a one-file tree and checks the summary on stderr: the singular forms
# ("1 match, 1 file, 1 directory") and the human-readable duration ("0.5 ms" or
# "1.24 s"), which must stay on stderr and out of stdout.

if(NOT DEFINED EXE OR NOT DEFINED TREE)
    message(FATAL_ERROR "EXE and TREE must be set")
endif()

file(REMOVE_RECURSE "${TREE}")
file(MAKE_DIRECTORY "${TREE}")
file(WRITE "${TREE}/only.txt" "x")

execute_process(
    COMMAND "${EXE}" "only" "${TREE}"
    OUTPUT_VARIABLE stdout_text
    ERROR_VARIABLE stderr_text
    RESULT_VARIABLE exit_code
)
if(NOT exit_code EQUAL 0)
    message(FATAL_ERROR "exit code ${exit_code}; stderr: ${stderr_text}")
endif()

if(NOT stderr_text MATCHES "1 match, 1 file, 1 directory scanned in [0-9]+\\.[0-9]+ (ms|s)")
    message(FATAL_ERROR "unexpected summary on stderr: '${stderr_text}'")
endif()

# The summary must not leak onto stdout.
if(stdout_text MATCHES "scanned in")
    message(FATAL_ERROR "summary appeared on stdout: '${stdout_text}'")
endif()
