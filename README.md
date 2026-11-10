# Vluzacn ZHLT

Custom Zoner's Half-Life Compilation Tools created by vluzacn. This repository also adds support for determining the number of threads on POSIX systems, a CMake build with a single open-source compiler (Zig) for Windows and Linux, packaging, and input-validation hardening (see [CHANGELOG.md](CHANGELOG.md)).

The project builds five command-line tools:

- `hlcsg` - construct solid geometry (CSG) from MAP files
- `hlbsp` - generate BSP trees and portal data
- `hlvis` - compute visibility (PVS)
- `hlrad` - compute lighting/radiosity
- `ripent` - edit entity and texture data in compiled BSP files

## Documentation

- [User manual](docs/user/manual.md) - command-line options for every tool
- [Developer guide](docs/developer/developer.md) - layout, entry points, shared modules
- [Warning cleanup plan](docs/developer/warning-cleanup-plan.md) (completed)
- [Lint cleanup plan](docs/developer/lint-cleanup-plan.md)
- [Changelog](CHANGELOG.md)
- [docs/llms.txt](docs/llms.txt) - complete reference for AI coding agents
- API reference - generated with Doxygen (see below)
- Audit reports - [`audits/`](audits/)

## Building

### Requirements

| Tool | Version | Purpose |
|------|---------|---------|
| [CMake](https://cmake.org/download/) | 3.16 or newer | build configuration and packaging |
| [Ninja](https://github.com/ninja-build/ninja/releases) | any recent | build runner |
| [Zig](https://ziglang.org/download/) | 0.17.1 | C++ compiler (bundled Clang) for Windows and Linux |
| [Doxygen](https://www.doxygen.nl/download.html) | optional | API docs |
| clang-tidy | 22.1.8 (`pip install clang-tidy==22.1.8`), the same version as Zig 0.17.1's bundled clang | lint |
| clang-format | 23.1.5 (`pip install clang-format==23.1.5`) | code formatting (required for contributions) |

`cmake`, `ninja` and `zig` must be on `PATH`. Otherwise pass `-DZIG_EXECUTABLE=<path>` on the first configure, and call CMake by its full path. `.env.example` lists the optional `CMAKE` and `ZIG` paths; copy it to `.env`, which is git-ignored.

### Presets

```sh
cmake --preset windows && cmake --build --preset windows   # build/windows/bin/*.exe
cmake --preset linux   && cmake --build --preset linux     # build/linux/bin/* (cross-compiled)
```

Both presets build Release binaries with Zig, from either Windows or Linux. Linux binaries need glibc 2.28 or newer (most distributions from 2018 on). The toolchain files are in [`cmake/toolchains/`](cmake/toolchains/). Warnings are errors by default; configure with `-DVHLT_WERROR=OFF` for a diagnostic build that tolerates them.

Other targets (shown for the `windows` preset; use `build/linux` for Linux):

| Target | Command | Output |
|--------|---------|--------|
| Single tool | `cmake --build build/windows --target hlcsg` | `build/windows/bin/hlcsg.exe` |
| API docs (requires Doxygen) | `cmake --build build/windows --target docs` | `build/windows/docs/html/index.html` |
| ZIP package | `cmake --build build/windows --target package` | `build/windows/vhlt-<version>-win64.zip` |
| Format sources | `cmake --build build/windows --target format` | sources rewritten in place |
| Check formatting | `cmake --build build/windows --target format-check` | fails if any file is unformatted |

The style is in `.clang-format`. The bulk reformat commit is listed in `.git-blame-ignore-revs`; run `git config blame.ignoreRevsFile .git-blame-ignore-revs` once so `git blame` skips it.

Lint with clang-tidy by configuring a separate build directory with `-DVHLT_CLANG_TIDY=ON`, for example `cmake -S . -B build/lint --preset windows -DVHLT_CLANG_TIDY=ON && cmake --build build/lint`. Every file is then checked during the build with its real defines; the checks are in `.clang-tidy`. CMake asks the Zig driver for its system include paths and libc++ macros and passes them to clang-tidy. clang-tidy must match Zig's clang version (22.1.8 for Zig 0.17.1), otherwise Zig's built-in headers fail to parse.

The ZIP contains the five tools, `Terms of Use.txt`, the user manual, and the HTML API docs if the `docs` target was built first.

`compile_commands.json` is generated in each build directory for clangd (configured by `.clangd`).

## Install

```sh
cmake --install build/windows --prefix <dir>
```

This installs the tools into `<dir>/bin` and the license and docs into `<dir>/share/doc/vhlt`.

## Verifying a build

Running any tool without arguments prints its version banner (for example `hlcsg v3.5.3 VL35 64-bit`) followed by usage, and exits with code 1.

## Notes

- The tools are derived from Zoner's Half-Life Compilation Tools (v3.4).
- Please make sure the default maximum stack size on your computer is more than 4 MB before running the tools.
- `-threads` accepts 1 to 64; larger values (or auto-detected CPU counts above 64) are clamped.
- `-fno-strict-aliasing` is required; the CMake build sets it. Do not change the feature macro definitions in `common/cmdlib.h`; the code was written and checked under those assumptions.
- Malformed or oversized input files (BSP, WAD, `.prt`, `.inc`) are rejected with an error rather than processed.
