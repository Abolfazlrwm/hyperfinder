# Checks that non-ASCII names survive the whole round trip: a UTF-8 query on the
# command line, the UTF-8 bytes the executable prints back.
#
# Two details make this script more than a regex check:
#
# * The literals live here instead of arriving as -D arguments. CTest builds a
#   command line for each test, and on Windows a non-ASCII argument does not
#   survive that trip. CMake does read this file as UTF-8, and execute_process
#   hands the arguments to CreateProcessW as wide strings.
# * The output is captured to a file and compared as hex. That asserts the exact
#   UTF-8 bytes and does not depend on how CMake decodes the child's output.

if(NOT DEFINED EXE OR NOT DEFINED FIXTURE)
    message(FATAL_ERROR "EXE and FIXTURE must be set")
endif()

set(persian "علی")
set(accented "café")
set(emoji "🚀")

function(check_query label query expected_hex)
    set(capture "${FIXTURE}/../unicode_${label}.out")

    execute_process(
        COMMAND "${EXE}" "${query}" "${FIXTURE}"
        OUTPUT_FILE "${capture}"
        ERROR_VARIABLE err
        RESULT_VARIABLE code
    )

    if(NOT "${code}" STREQUAL "0")
        message(FATAL_ERROR "${label}: exit code '${code}', expected 0\nstderr: ${err}")
    endif()

    file(READ "${capture}" bytes HEX)
    string(TOLOWER "${bytes}" bytes)
    file(REMOVE "${capture}")

    string(FIND "${bytes}" "${expected_hex}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR
            "${label}: the UTF-8 bytes for '${query}' were not in the output\n"
            "expected hex: ${expected_hex}\nactual hex:   ${bytes}")
    endif()
endfunction()

# "علی.txt", "café.txt" and "🚀.txt" as UTF-8, then ".txt" and the line feed.
check_query("persian" "${persian}" "d8b9d984db8c2e747874")
check_query("accented" "${accented}" "636166c3a92e747874")
check_query("emoji" "${emoji}" "f09f9a802e747874")

# The Persian query must match exactly one file, not everything.
execute_process(
    COMMAND "${EXE}" "--count" "${persian}" "${FIXTURE}"
    OUTPUT_VARIABLE count_text
    RESULT_VARIABLE count_code
)
string(STRIP "${count_text}" count_text)
if(NOT "${count_text}" STREQUAL "1" OR NOT "${count_code}" STREQUAL "0")
    message(FATAL_ERROR "Persian --count: got '${count_text}' (exit '${count_code}'), expected 1")
endif()
