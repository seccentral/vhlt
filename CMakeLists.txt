cmake_minimum_required(VERSION 3.16)

project(vhlt
    VERSION 3.5.4
    LANGUAGES CXX
)

include(GNUInstallDirs)

# Reasonable default for single-config generators (Makefiles/Ninja).
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ----------------------------------------------------------------------
# Linting: clang-tidy runs on every translation unit during the build,
# with the real per-tool defines (checks configured in .clang-tidy).
# ----------------------------------------------------------------------
option(VHLT_CLANG_TIDY "Run clang-tidy on every source file during the build" OFF)
if(VHLT_CLANG_TIDY)
    find_program(CLANG_TIDY_EXECUTABLE clang-tidy REQUIRED)
    # Zig adds its libc/libc++ include paths and libc++ configuration macros
    # internally, so they are not in the compile commands clang-tidy sees.
    # Ask the Zig driver for them once and pass them as extra arguments.
    set(_vhlt_empty "${CMAKE_BINARY_DIR}/vhlt_tidy_probe.cpp")
    file(WRITE "${_vhlt_empty}" "")
    # CMake keeps the zig executable in CMAKE_CXX_COMPILER and the list's
    # mandatory arguments ("c++ -target <triple>") in CMAKE_CXX_COMPILER_ARG1.
    separate_arguments(_vhlt_zig_args NATIVE_COMMAND "${CMAKE_CXX_COMPILER_ARG1}")
    execute_process(
        COMMAND "${CMAKE_CXX_COMPILER}" ${_vhlt_zig_args} -E -v -x c++ "${_vhlt_empty}"
        OUTPUT_QUIET ERROR_VARIABLE _vhlt_zig_verbose)
    string(REGEX MATCHALL " -D [^ \r\n]+| -isystem \"[^\"]+\"" _vhlt_zig_flags "${_vhlt_zig_verbose}")
    set(_vhlt_tidy_args "")
    foreach(_flag IN LISTS _vhlt_zig_flags)
        string(REGEX REPLACE "^ -D (.*)$" "-D\\1" _flag "${_flag}")
        string(REGEX REPLACE "^ -isystem \"(.*)\"$" "-isystem\\1" _flag "${_flag}")
        string(REPLACE "\\\\" "/" _flag "${_flag}")
        list(APPEND _vhlt_tidy_args "--extra-arg=${_flag}")
    endforeach()
    list(LENGTH _vhlt_tidy_args _vhlt_tidy_count)
    message(STATUS "clang-tidy: ${_vhlt_tidy_count} system flags taken from the Zig driver")
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXECUTABLE}" ${_vhlt_tidy_args})
endif()

# The project is warning-clean; keep it that way.
option(VHLT_WERROR "Treat compiler warnings as errors" ON)

# hlrad-ng: next-generation hlrad with byte-identical output to hlrad.
# Off by default while under development; see docs/research/hlrad-optimizations.md.
option(VHLT_HLRAD_NG "Build the next-generation hlrad" OFF)
# Compile hlrad-ng with the profiling counters from hlrad-ng/profile.{h,cpp}.
option(VHLT_HLRAD_NG_PROFILE "Build hlrad-ng with profiling counters" OFF)

# ----------------------------------------------------------------------
# Formatting: clang-format with the style in .clang-format.
#   format        rewrite every source file in place
#   format-check  fail if any file is not formatted (gate)
# ----------------------------------------------------------------------
find_program(CLANG_FORMAT_EXECUTABLE clang-format)
if(CLANG_FORMAT_EXECUTABLE)
    file(GLOB_RECURSE VHLT_FORMAT_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/common/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/common/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/hlcsg/*.cpp"  "${CMAKE_CURRENT_SOURCE_DIR}/hlcsg/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/hlbsp/*.cpp"  "${CMAKE_CURRENT_SOURCE_DIR}/hlbsp/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/hlvis/*.cpp"  "${CMAKE_CURRENT_SOURCE_DIR}/hlvis/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/hlrad/*.cpp"  "${CMAKE_CURRENT_SOURCE_DIR}/hlrad/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/hlrad-ng/*.cpp"  "${CMAKE_CURRENT_SOURCE_DIR}/hlrad-ng/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/ripent/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/ripent/*.h")
    add_custom_target(format
        COMMAND "${CLANG_FORMAT_EXECUTABLE}" -i ${VHLT_FORMAT_SOURCES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Formatting sources with clang-format"
        VERBATIM)
    add_custom_target(format-check
        COMMAND "${CLANG_FORMAT_EXECUTABLE}" --dry-run --Werror ${VHLT_FORMAT_SOURCES}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Checking source formatting with clang-format"
        VERBATIM)
endif()

# ----------------------------------------------------------------------
# Platform-specific configuration
# ----------------------------------------------------------------------
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(VHLT_VERSION_MACRO VERSION_64BIT)
else()
    set(VHLT_VERSION_MACRO VERSION_32BIT)
endif()

if(WIN32)
    set(VHLT_PLATFORM_DEFINITIONS
        ${VHLT_VERSION_MACRO}
        SYSTEM_WIN32
        STDC_HEADERS
        WIN32
        _CONSOLE
    )
else()
    set(VHLT_PLATFORM_DEFINITIONS
        VERSION_LINUX
        SYSTEM_POSIX
        STDC_HEADERS
        HAVE_FCNTL_H
        HAVE_PTHREAD_H
        HAVE_SYS_RESOURCE_H
        HAVE_SYS_STAT_H
        HAVE_SYS_TIME_H
        HAVE_UNISTD_H
    )
endif()

set(VHLT_COMMON_SOURCES
    common/blockmem.cpp
    common/bspfile.cpp
    common/cmdlib.cpp
    common/cmdlinecfg.cpp
    common/filelib.cpp
    common/log.cpp
    common/mathlib.cpp
    common/messages.cpp
    common/scriplib.cpp
    common/threads.cpp
    common/winding.cpp
)

set(VHLT_COMMON_INCLUDE_DIRS
    common
)

# ----------------------------------------------------------------------
# Helper for adding one of the ZHLT tools
# ----------------------------------------------------------------------
function(add_vhlt_tool target)
    cmake_parse_arguments(TOOL "NO_INSTALL;INCLUDE_FIRST" "" "SOURCES;EXTRA_DEFINITIONS" ${ARGN})

    add_executable(${target}
        ${VHLT_COMMON_SOURCES}
        ${TOOL_SOURCES}
    )

    if(TOOL_INCLUDE_FIRST)
        set(_vhlt_tool_includes ${target} ${VHLT_COMMON_INCLUDE_DIRS})
    else()
        set(_vhlt_tool_includes ${VHLT_COMMON_INCLUDE_DIRS} ${target})
    endif()
    target_include_directories(${target} PRIVATE ${_vhlt_tool_includes})

    target_compile_definitions(${target} PRIVATE
        ${VHLT_PLATFORM_DEFINITIONS}
        ${TOOL_EXTRA_DEFINITIONS}
    )

    target_compile_options(${target} PRIVATE
        -Wall
        -Wno-deprecated-declarations
        -fno-strict-aliasing
    )
    if(VHLT_WERROR)
        target_compile_options(${target} PRIVATE -Werror)
    endif()
    if(NOT WIN32)
        target_compile_options(${target} PRIVATE -pthread)
        target_link_options(${target} PRIVATE -pthread)
    endif()

    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    if(NOT TOOL_NO_INSTALL)
        install(TARGETS ${target}
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        )
    endif()
endfunction()

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/Terms of Use.txt"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/docs/user/manual.md"
              "${CMAKE_CURRENT_SOURCE_DIR}/docs/user/max-quality.md"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}")

# ----------------------------------------------------------------------
# Tools
# ----------------------------------------------------------------------
add_vhlt_tool(hlcsg
    SOURCES
        hlcsg/ansitoutf8.cpp
        hlcsg/autowad.cpp
        hlcsg/brush.cpp
        hlcsg/brushunion.cpp
        hlcsg/hullfile.cpp
        hlcsg/map.cpp
        hlcsg/properties.cpp
        hlcsg/qcsg.cpp
        hlcsg/textures.cpp
        hlcsg/wadcfg.cpp
        hlcsg/wadinclude.cpp
        hlcsg/wadpath.cpp
    EXTRA_DEFINITIONS
        HLCSG
        DOUBLEVEC_T
)

add_vhlt_tool(hlbsp
    SOURCES
        hlbsp/brink.cpp
        hlbsp/merge.cpp
        hlbsp/outside.cpp
        hlbsp/portals.cpp
        hlbsp/qbsp.cpp
        hlbsp/solidbsp.cpp
        hlbsp/surfaces.cpp
        hlbsp/tjunc.cpp
        hlbsp/writebsp.cpp
    EXTRA_DEFINITIONS
        HLBSP
        DOUBLEVEC_T
)

add_vhlt_tool(hlvis
    SOURCES
        hlvis/flow.cpp
        hlvis/vis.cpp
        hlvis/zones.cpp
    EXTRA_DEFINITIONS
        HLVIS
)

add_vhlt_tool(hlrad
    SOURCES
        hlrad/compress.cpp
        hlrad/lerp.cpp
        hlrad/lightmap.cpp
        hlrad/loadtextures.cpp
        hlrad/mathutil.cpp
        hlrad/nomatrix.cpp
        hlrad/qrad.cpp
        hlrad/qradutil.cpp
        hlrad/sparse.cpp
        hlrad/trace.cpp
        hlrad/transfers.cpp
        hlrad/transparency.cpp
        hlrad/vismatrix.cpp
        hlrad/vismatrixutil.cpp
    EXTRA_DEFINITIONS
        HLRAD
)

# hlrad-ng is an exact copy of hlrad today; it must keep producing
# byte-identical output while being optimized (docs/research/hlrad-optimizations.md).
# Not installed or packaged until parity is accepted.
if(VHLT_HLRAD_NG)
    add_vhlt_tool(hlrad-ng
        NO_INSTALL
        INCLUDE_FIRST
        SOURCES
            hlrad-ng/compress.cpp
            hlrad-ng/lerp.cpp
            hlrad-ng/lightmap.cpp
            hlrad-ng/loadtextures.cpp
            hlrad-ng/mathutil.cpp
            hlrad-ng/nomatrix.cpp
            hlrad-ng/profile.cpp
            hlrad-ng/qrad.cpp
            hlrad-ng/qradutil.cpp
            hlrad-ng/sparse.cpp
            hlrad-ng/trace.cpp
            hlrad-ng/transfers.cpp
            hlrad-ng/transparency.cpp
            hlrad-ng/vismatrix.cpp
            hlrad-ng/vismatrixutil.cpp
        EXTRA_DEFINITIONS
            HLRAD
    )
    if(VHLT_HLRAD_NG_PROFILE)
        target_compile_definitions(hlrad-ng PRIVATE HLRAD_NG_PROFILE)
    endif()
endif()

add_vhlt_tool(ripent
    SOURCES
        ripent/ripent.cpp
    EXTRA_DEFINITIONS
        RIPENT
)

# ----------------------------------------------------------------------
# Documentation
# ----------------------------------------------------------------------
find_package(Doxygen)
if(DOXYGEN_FOUND)
    set(VHLT_DOCS_OUTPUT_DIR "${CMAKE_BINARY_DIR}/docs")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/docs/api/Doxyfile.in"
        "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
        @ONLY
    )
    add_custom_target(docs
        COMMAND "${DOXYGEN_EXECUTABLE}" "${CMAKE_CURRENT_BINARY_DIR}/Doxyfile"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/docs/api"
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
    install(DIRECTORY "${VHLT_DOCS_OUTPUT_DIR}/html/"
            DESTINATION "${CMAKE_INSTALL_DOCDIR}/html"
            OPTIONAL)
endif()

# ----------------------------------------------------------------------
# Packaging
# ----------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "vhlt")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "Vluzacn")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Vluzacn ZHLT - Half-Life Compilation Tools")
set(CPACK_GENERATOR "ZIP")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/Terms of Use.txt")
include(CPack)
