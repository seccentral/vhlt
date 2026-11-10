# Vluzacn ZHLT

[![Build Status](https://github.com/FreeSlave/vhlt/actions/workflows/.github.yml/badge.svg?branch=master)](https://github.com/FreeSlave/vhlt/actions/workflows/.github.yml)

Custom Zoner's Half-Life Compilation Tools created by vluzacn. This repository also adds support for determining the number of threads on POSIX systems.

The project builds five command-line tools:

- `hlcsg` - construct solid geometry (CSG) from MAP files
- `hlbsp` - generate BSP trees and portal data
- `hlvis` - compute visibility (PVS)
- `hlrad` - compute lighting/radiosity
- `ripent` - edit entity data in compiled BSP files

## Building

### With CMake (recommended)

```sh
cmake -S . -B build
cmake --build build
```

On Windows with MinGW, if CMake does not auto-detect a compiler:

```sh
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
```

Build a single tool:

```sh
cmake --build build --target hlcsg
```

Generate API documentation (requires Doxygen):

```sh
cmake --build build --target docs
```

The generated HTML documentation is written to `build/docs/html`.

### With Make

```sh
make
```

Pass additional flags to make:

```sh
make USER_FLAGS="your flags"
```

Example:

```sh
make USER_FLAGS="-static-libstdc++"
```

## Install

CMake install support is available:

```sh
cmake --install build
```

By default this installs the five tools into the configured binary directory.

## Notes

- The tools are derived from Zoner's Half-Life Compilation Tools (v3.4).
- Please make sure the default maximum stack size on your computer is more than 4 MB before running the tools.
- When compiling with g++, `-fno-strict-aliasing` is required. All macro definitions should not be changed, because the code was written and checked under those assumptions.
