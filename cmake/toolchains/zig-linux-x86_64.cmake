# Toolchain: Zig (bundled Clang) cross-compiling for 64-bit Linux.
# glibc 2.28 is the minimum runtime version (most distributions from 2018 on).
# Usage: cmake --preset linux   (or -DCMAKE_TOOLCHAIN_FILE=<this file>)
# Zig is located on PATH; override with -DZIG_EXECUTABLE=<path to zig>.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
include("${CMAKE_CURRENT_LIST_DIR}/zig-common.cmake")
vhlt_zig_toolchain(x86_64-linux-gnu.2.28)
