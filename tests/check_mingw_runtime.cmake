# Verifies that the MinGW build does not need MinGW DLLs on PATH.
#
# Run by CTest as:
#   cmake -DEXE=<exe> -DSTATIC_RUNTIME=ON -P check_mingw_runtime.cmake
#
# Two checks:
#   1. objdump -p must not list libstdc++-6 / libgcc_s_seh-1 / libwinpthread-1
#      among the imported DLLs when static runtime was requested.
#   2. The executable must actually run with MinGW stripped from PATH.

if(NOT DEFINED EXE)
    message(FATAL_ERROR "EXE must be set to the hyperfinder executable")
endif()
if(NOT DEFINED STATIC_RUNTIME)
    message(FATAL_ERROR "STATIC_RUNTIME must be set to ON or OFF")
endif()

execute_process(COMMAND objdump -p "${EXE}" OUTPUT_VARIABLE dump)
string(REGEX MATCHALL "DLL Name: ([^\n]+)" dll_matches "${dump}")
set(imported)
foreach(match ${dll_matches})
    string(REGEX REPLACE "DLL Name: " "" name "${match}")
    list(APPEND imported "${name}")
endforeach()
message(STATUS "imported DLLs: ${imported}")

set(forbidden "libstdc\\+\\+-6" "libgcc_s_seh-1" "libwinpthread-1")
foreach(bad ${forbidden})
    if(dump MATCHES "${bad}")
        if(STATIC_RUNTIME)
            message(FATAL_ERROR
                "static runtime was requested, but ${bad}.dll is still imported")
        else()
            message(STATUS "note: ${bad}.dll imported (static runtime off)")
        endif()
    endif()
endforeach()

# The real test: launch with PATH reduced to the OS directories only. If the
# binary still pulls in a MinGW DLL, the loader fails and the exit code is
# non-zero (the historical 0xC000007B failure).
execute_process(
    COMMAND ${CMAKE_COMMAND} -E env "PATH=C:/Windows/System32;C:/Windows"
            "${EXE}" --version
    RESULT_VARIABLE code
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
if(NOT "${code}" STREQUAL "0")
    message(FATAL_ERROR
        "executable failed with no MinGW on PATH (exit ${code})\nstderr: ${err}")
endif()
message(STATUS "ran successfully with no MinGW on PATH: ${out}")
