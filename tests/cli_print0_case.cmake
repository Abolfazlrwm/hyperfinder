# Runs the executable with --print0 and checks the raw bytes: the record must be
# the matching path followed by exactly one NUL byte, with no newline anywhere.
#
# The normal harness works on text, and a NUL byte does not survive that, so this
# case inspects the output file in hexadecimal instead.

if(NOT DEFINED EXE OR NOT DEFINED FIXTURE)
    message(FATAL_ERROR "EXE and FIXTURE must be set")
endif()

set(out "${CMAKE_CURRENT_BINARY_DIR}/print0_out.bin")
execute_process(
    COMMAND "${EXE}" --print0 a.txt "${FIXTURE}"
    OUTPUT_FILE "${out}"
    RESULT_VARIABLE exit_code
    ERROR_VARIABLE stderr_text
)
if(NOT exit_code EQUAL 0)
    message(FATAL_ERROR "exit code ${exit_code}; stderr: ${stderr_text}")
endif()

file(READ "${out}" hex HEX)
string(LENGTH "${hex}" length)
if(length LESS 2)
    message(FATAL_ERROR "no output")
endif()

# The record is a path followed by one NUL, so the last byte must be 00.
math(EXPR tail_offset "${length} - 2")
string(SUBSTRING "${hex}" ${tail_offset} 2 tail)
if(NOT tail STREQUAL "00")
    message(FATAL_ERROR "output did not end with a NUL byte (tail='${tail}')")
endif()

# A NUL-delimited record must not contain a newline byte (0x0a). Compare pairs so
# an 0a that only appears inside another byte's hex is not mistaken for one.
math(EXPR pairs "${length} / 2")
math(EXPR last_index "${pairs} - 1")
foreach(index RANGE ${last_index})
    math(EXPR offset "${index} * 2")
    string(SUBSTRING "${hex}" ${offset} 2 byte)
    if(byte STREQUAL "0a")
        message(FATAL_ERROR "found a newline byte in --print0 output")
    endif()
endforeach()

# It should be the a.txt record we asked for.
string(FIND "${hex}" "612e747874" position)  # "a.txt" in ASCII hex
if(position EQUAL -1)
    message(FATAL_ERROR "output does not contain 'a.txt'")
endif()
