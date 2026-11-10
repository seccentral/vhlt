# Vluzacn ZHLT

[![Build Status](https://github.com/FreeSlave/vhlt/actions/workflows/.github.yml/badge.svg?branch=master)](https://github.com/FreeSlave/vhlt/actions/workflows/.github.yml)

Custom Zoner's Half-Life Compilation Tools created by vluzacn. This repository also adds support for determining the number of threads on POSIX systems, a CMake build, packaging, and input-validation hardening (see [CHANGELOG.md](CHANGELOG.md)).

The project builds five command-line tools:

- `hlcsg` - construct solid geometry (CSG) from MAP files
- `hlbsp` - generate BSP trees and portal data
- `hlvis` - compute visibility (PVS)
- `hlrad` - compute lighting/radiosity
- `ripent` - edit entity and texture data in compiled BSP files

## Documentation

- [User manual](docs/user/manual.md) - command-line options for every tool
- [Developer guide](docs/developer/developer.md) - layout, entry points, shared modules
- [Changelog](CHANGELOG.md)
- [docs/llms.txt](docs/llms.txt) - complete reference for AI coding agents
- API reference - generated with Doxygen (see below)
- Audit reports - [`audits/`](audits/)

## Building

### With CMake (recommended)

```sh
cmake -S . -B build
cmake --build build
```

On Windows with MinGW:

```sh
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
```

A `default` preset (Release, `build/`) is also provided: `cmake --preset default && cmake --build --preset default`.

Binaries are written to `build/bin/`.

Other targets:

| Target | Command | Output |
|--------|---------|--------|
| Single tool | `cmake --build build --target hlcsg` | `build/bin/hlcsg` |
| API docs (requires Doxygen) | `cmake --build build --target docs` | `build/docs/html/index.html` |
| ZIP package | `cmake --build build --target package` | `build/vhlt-<version>-<platform>.zip` |
| Lint (requires cppcheck) | `cmake --build build --target lint` | console |

The ZIP contains the five tools, `Terms of Use.txt`, the user manual, and the HTML API docs if the `docs` target was built first.

`build/compile_commands.json` is generated for clangd (configured by `.clangd`).

### With Make (POSIX only)

The `Makefile` is a legacy alternative for Linux/macOS; it hard-codes POSIX definitions and does not build docs or packages.

```sh
make
make USER_FLAGS="-static-libstdc++"
```

## Install

```sh
cmake --install build --prefix <dir>
```

This installs the tools into `<dir>/bin` and the license and docs into `<dir>/share/doc/vhlt`.

## Verifying a build

Running any tool without arguments prints its version banner (for example `hlcsg v3.5.2 VL35 64-bit`) followed by usage, and exits with code 1. CI ([`.github/workflows/.github.yml`](.github/workflows/.github.yml)) builds on Linux, macOS and Windows, generates the docs, and checks each banner.

## Notes

- The tools are derived from Zoner's Half-Life Compilation Tools (v3.4).
- Please make sure the default maximum stack size on your computer is more than 4 MB before running the tools.
- `-threads` accepts 1 to 64; larger values (or auto-detected CPU counts above 64) are clamped.
- When compiling with g++, `-fno-strict-aliasing` is required (the CMake build sets it). Do not change the feature macro definitions in `common/cmdlib.h`; the code was written and checked under those assumptions.
- Malformed or oversized input files (BSP, WAD, `.prt`, `.inc`) are rejected with an error rather than processed.
