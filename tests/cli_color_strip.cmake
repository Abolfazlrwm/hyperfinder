# Confirms --color=always adds only ANSI escapes: removing every SGR sequence
# from the coloured output yields exactly the --color=never output.
#
# Run by CTest as:
#   cmake -DEXE=<exe> -DFIXTURE=<dir> -P cli_color_strip.cmake

if(NOT DEFINED EXE OR NOT DEFINED FIXTURE)
    message(FATAL_ERROR "EXE and FIXTURE must be set")
endif()

string(ASCII 27 esc)
set(args a.txt "${FIXTURE}")

execute_process(
    COMMAND "${EXE}" --color=always ${args}
    OUTPUT_VARIABLE colored
    RESULT_VARIABLE code_always)
execute_process(
    COMMAND "${EXE}" --color=never ${args}
    OUTPUT_VARIABLE plain
    RESULT_VARIABLE code_never)

if(NOT "${code_always}" STREQUAL "0" OR NOT "${code_never}" STREQUAL "0")
    message(FATAL_ERROR "colour runs failed: always=${code_always} never=${code_never}")
endif()

# Strip SGR sequences: ESC [ <params> m (bold green and reset both use this).
string(REGEX REPLACE "${esc}\\[[0-9;]*m" "" stripped "${colored}")

if(NOT "${stripped}" STREQUAL "${plain}")
    message(FATAL_ERROR
        "stripping colour escapes did not yield the plain output\n"
        "plain:   '${plain}'\nstripped: '${stripped}'")
endif()
