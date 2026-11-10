cmake_minimum_required(VERSION 3.16)

project(vhlt
    VERSION 3.5.2
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
    template
    common
)

# ----------------------------------------------------------------------
# Helper for adding one of the ZHLT tools
# ----------------------------------------------------------------------
function(add_vhlt_tool target)
    cmake_parse_arguments(TOOL "" "" "SOURCES;EXTRA_DEFINITIONS" ${ARGN})

    add_executable(${target}
        ${VHLT_COMMON_SOURCES}
        ${TOOL_SOURCES}
    )

    target_include_directories(${target} PRIVATE
        ${VHLT_COMMON_INCLUDE_DIRS}
        ${target}
    )

    target_compile_definitions(${target} PRIVATE
        ${VHLT_PLATFORM_DEFINITIONS}
        ${TOOL_EXTRA_DEFINITIONS}
    )

    if(MSVC)
        target_compile_options(${target} PRIVATE /W3)
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wno-deprecated-declarations
            -fno-strict-aliasing
        )
        if(NOT WIN32)
            target_compile_options(${target} PRIVATE -pthread)
            target_link_options(${target} PRIVATE -pthread)
        endif()
    endif()

    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )

    install(TARGETS ${target}
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
endfunction()

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/Terms of Use.txt"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/docs/user/manual.md"
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
    add_custom_target(docs
        COMMAND "${DOXYGEN_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/docs/api/Doxyfile"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/docs/api"
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
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

# ----------------------------------------------------------------------
# Linting
# ----------------------------------------------------------------------
find_program(CPPCHECK_EXECUTABLE cppcheck)
if(CPPCHECK_EXECUTABLE)
    add_custom_target(lint
        COMMAND "${CPPCHECK_EXECUTABLE}" -j 8 --enable=warning,performance,portability
            --std=c++11 --quiet
            "${CMAKE_CURRENT_SOURCE_DIR}/common"
            "${CMAKE_CURRENT_SOURCE_DIR}/hlcsg"
            "${CMAKE_CURRENT_SOURCE_DIR}/hlbsp"
            "${CMAKE_CURRENT_SOURCE_DIR}/hlvis"
            "${CMAKE_CURRENT_SOURCE_DIR}/hlrad"
            "${CMAKE_CURRENT_SOURCE_DIR}/ripent"
            "${CMAKE_CURRENT_SOURCE_DIR}/template"
        COMMENT "Running cppcheck (multicore)"
        VERBATIM
    )
endif()
