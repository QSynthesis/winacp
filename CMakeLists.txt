cmake_minimum_required(VERSION 3.19)

project(winacp
    VERSION 1.0.0
    LANGUAGES CXX
    DESCRIPTION "Conversion between UTF-16 and the Windows ANSI code pages, identical to that of Windows"
)

option(WINACP_BUILD_TESTS "Build the tests" OFF)
option(WINACP_BUILD_GENERATOR "Build the table generator (Windows only)" OFF)
option(WINACP_INSTALL "Generate installation rules" ON)

include(GNUInstallDirs)
include(GenerateExportHeader)

# Sources are encoded in UTF-8. Without /utf-8, MSVC decodes them in the active code page of the
# host, which corrupts non-ASCII comments and may cause the following line to be discarded.
if(MSVC)
    add_compile_options(/utf-8)
endif()

# ----------------------------------
# Embedded tables
# ----------------------------------
file(GLOB _tables CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tables/cp*.bin)
set(_embedded ${CMAKE_CURRENT_BINARY_DIR}/tables.cpp)
add_custom_command(
    OUTPUT ${_embedded}
    COMMAND ${CMAKE_COMMAND}
        -DDIRECTORY=${CMAKE_CURRENT_SOURCE_DIR}/tables
        -DOUTPUT=${_embedded}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
    DEPENDS ${_tables} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
    COMMENT "Embedding the code page tables"
    VERBATIM
)

# ----------------------------------
# Library
# ----------------------------------
add_library(winacp
    include/winacp/winacp.h
    src/tables.h
    src/winacp.cpp
    ${_embedded}
)
add_library(winacp::winacp ALIAS winacp)

generate_export_header(winacp
    EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/include/winacp/winacp_export.h
)
if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(winacp PUBLIC WINACP_STATIC_DEFINE)
endif()

target_include_directories(winacp
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_compile_features(winacp PUBLIC cxx_std_17)

# Position-independent code is enabled for the static library as well, so that it can be linked
# into a shared library.
set_target_properties(winacp PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

# ----------------------------------
# Table generator
# ----------------------------------
if(WINACP_BUILD_GENERATOR)
    if(NOT WIN32)
        message(FATAL_ERROR "The table generator requires the Windows API and is supported on Windows only.")
    endif()
    add_executable(winacp-generate tools/generate/main.cpp)
    target_compile_features(winacp-generate PRIVATE cxx_std_17)
endif()

# ----------------------------------
# Tests
# ----------------------------------
if(WINACP_BUILD_TESTS)
    enable_testing()

    add_executable(test_winacp tests/test_winacp.cpp tests/check.h)
    target_link_libraries(test_winacp PRIVATE winacp::winacp)
    add_test(NAME test_winacp COMMAND test_winacp)

    # Verification of the tables against the Windows API; Windows only.
    if(WIN32)
        add_executable(test_against_windows tests/test_against_windows.cpp tests/check.h)
        target_link_libraries(test_against_windows PRIVATE winacp::winacp)
        add_test(NAME test_against_windows COMMAND test_against_windows)
    endif()

    # Without incremental linking. After a table changed, an incremental link of MSVC 14.44 has
    # been observed to retain the previous tables from the static library, so that the tests
    # passed against data that was no longer in the tables.
    if(MSVC)
        foreach(_test test_winacp test_against_windows)
            if(TARGET ${_test})
                set_property(TARGET ${_test} APPEND_STRING PROPERTY LINK_FLAGS " /INCREMENTAL:NO")
            endif()
        endforeach()
    endif()
endif()

# ----------------------------------
# Install
# ----------------------------------
if(WINACP_INSTALL)
    include(CMakePackageConfigHelpers)

    install(TARGETS winacp
        EXPORT winacpTargets
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )
    install(FILES
        include/winacp/winacp.h
        ${CMAKE_CURRENT_BINARY_DIR}/include/winacp/winacp_export.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/winacp
    )
    install(EXPORT winacpTargets
        FILE winacpTargets.cmake
        NAMESPACE winacp::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/winacp
    )
    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/winacpConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/winacpConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/winacp
    )
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/winacpConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/winacpConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/winacpConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/winacp
    )
endif()
