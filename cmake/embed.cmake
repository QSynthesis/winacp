# Converts the tables in DIRECTORY into C++ arrays written to OUTPUT, for use by src/winacp.cpp.
#
#     cmake -DDIRECTORY=<tables> -DOUTPUT=<tables.cpp> -P embed.cmake
#
# Implemented in plain CMake so that the build requires no tools beyond a compiler. The code page
# of each table is taken from its file name, cp<number>.bin; src/winacp.cpp verifies that it
# matches the code page recorded in the table header.

file(GLOB _files "${DIRECTORY}/cp*.bin")

set(_pages)
foreach(_file IN LISTS _files)
    get_filename_component(_name "${_file}" NAME_WE)
    string(REGEX REPLACE "^cp" "" _page "${_name}")
    if(NOT _page MATCHES "^[0-9]+$")
        message(FATAL_ERROR "${_file}: file name does not match cp<number>.bin")
    endif()
    list(APPEND _pages ${_page})
    set(_file_${_page} "${_file}")
endforeach()
list(SORT _pages COMPARE NATURAL)

set(_out "// Generated from tables/ by cmake/embed.cmake. Do not edit.\n\n")
string(APPEND _out "#include \"tables.h\"\n\nnamespace winacp::detail {\n\n    namespace {\n\n")
foreach(_page IN LISTS _pages)
    file(READ "${_file_${_page}}" _hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _bytes "${_hex}")
    # Sixteen bytes per line, to keep the generated file readable.
    string(REGEX REPLACE "((0x..,){16})" "\\1\n            " _bytes "${_bytes}")
    string(APPEND _out "        const unsigned char cp${_page}[] = {\n            ${_bytes}\n        };\n\n")
endforeach()
string(APPEND _out "    }\n\n    const Blob blobs[] = {\n")
foreach(_page IN LISTS _pages)
    string(APPEND _out "        {${_page}, cp${_page}, sizeof(cp${_page})},\n")
endforeach()
list(LENGTH _pages _count)
string(APPEND _out "    };\n\n    const std::size_t blobCount = ${_count};\n\n}\n")

# Write only if the content changed, to avoid unnecessary recompilation.
if(EXISTS "${OUTPUT}")
    file(READ "${OUTPUT}" _old)
    if(_old STREQUAL _out)
        return()
    endif()
endif()
file(WRITE "${OUTPUT}" "${_out}")
