# Toolchain: Zig (bundled Clang) targeting 64-bit Windows.
# Usage: cmake --preset windows   (or -DCMAKE_TOOLCHAIN_FILE=<this file>)
# Zig is located on PATH; override with -DZIG_EXECUTABLE=<path to zig>.
include("${CMAKE_CURRENT_LIST_DIR}/zig-common.cmake")
vhlt_zig_toolchain(x86_64-windows-gnu)
