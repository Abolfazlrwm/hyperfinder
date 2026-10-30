# Creates the directory tree the end-to-end tests run against.
#
# Run at configure time:
#   cmake -DFIXTURE=<absolute directory> -P make_cli_fixture.cmake
#
# The tree is deliberately small and fixed, so every expected count below is a
# literal in tests/CMakeLists.txt rather than a value computed at run time.
# Non-ASCII names (Persian, accented, emoji) are part of it: they are what the
# UTF-8 argument and output paths have to survive.

if(NOT DEFINED FIXTURE)
    message(FATAL_ERROR "FIXTURE must be set to the directory to create")
endif()

file(REMOVE_RECURSE "${FIXTURE}")
file(MAKE_DIRECTORY "${FIXTURE}/sub/deep")
file(MAKE_DIRECTORY "${FIXTURE}/sub/.hiddendir")

# Ten visible files plus two hidden ones. Hidden means a leading dot here,
# which both platforms agree on.
set(visible_files
    "a.txt"
    "b.log"
    "c.TXT"
    "notes.md"
    "علی.txt"
    "café.txt"
    "🚀.txt"
    "sub/d.txt"
    "sub/e.log"
    "sub/deep/g.txt"
)

set(hidden_files
    ".hidden.txt"
    "sub/.hiddendir/hidden-deep.txt"
)

foreach(name IN LISTS visible_files hidden_files)
    file(WRITE "${FIXTURE}/${name}" "hyperfinder test fixture\n")
endforeach()
