# Shared setup for the Zig toolchain files. Zig ships Clang plus libc headers
# for every target, so one compiler builds Windows and Linux binaries.
function(vhlt_zig_toolchain target)
    find_program(ZIG_EXECUTABLE zig)
    if(NOT ZIG_EXECUTABLE)
        message(FATAL_ERROR "zig not found on PATH; pass -DZIG_EXECUTABLE=<path to zig>")
    endif()
    # Compiler as a list: executable followed by mandatory arguments.
    set(CMAKE_CXX_COMPILER "${ZIG_EXECUTABLE}" c++ -target ${target} PARENT_SCOPE)
    set(CMAKE_CXX_COMPILER_TARGET ${target} PARENT_SCOPE)
    # Zig turns -Wdate-time into an error; the version banners use __DATE__.
    set(CMAKE_CXX_FLAGS_INIT "-Wno-date-time" PARENT_SCOPE)
    # Release binaries ship without debug symbols.
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE_INIT "-s" PARENT_SCOPE)
endfunction()

# Never search host paths for target libraries or headers.
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
