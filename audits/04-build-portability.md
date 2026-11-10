# Audit: Build System, Portability, and Code Hygiene

Scope: `<repo>` (CMake, Makefile, all source). No source changes were made. A clean CMake/Ninja build on the current host produced **1,119 warnings** and no errors.

## Summary

| Severity | Count | Theme |
|---|---:|---|
| High | 6 | Fragile/non-portable build logic and configuration |
| Medium | 9 | Platform assumptions, always-on optimization/debug flags, warning suppression hygiene |
| Low | 10 | Dead/unused code, inconsistent macros, stale CI, minor hygiene |

## Build results (baseline)

- Toolchain: MinGW GCC 15.2.0, CMake/Ninja, Windows host.
- Warnings by category:

| Warning category | Count |
|---|---:|
| `unknown-pragmas` (ignored `#pragma warning`) | 704 |
| `alloca` redefined (MinGW) | 94 |
| `sign-compare` | 125 |
| `maybe-uninitialized` | 45 |
| `parentheses` | 43 |
| `unused-variable` | 31 |
| `write-strings` | 28 |
| `unused-but-set-variable` | 12 |
| `misleading-indentation` | 10 |
| `dangling-else` | 6 |
| `class-memaccess` (`memcpy`/`memset` on non-trivial `brush_t`) | 6 |
| `aggressive-loop-optimizations` | 5 |
| `catch-value=` | 4 |
| `switch` | 3 |
| `unused-function` | 1 |

## High severity

### H1. CMake hardcodes platform/header availability instead of detecting it
- **File:line**: `CMakeLists.txt:38-49`
- **Evidence**: Non-Windows builds always receive `HAVE_FCNTL_H`, `HAVE_PTHREAD_H`, `HAVE_SYS_RESOURCE_H`, `HAVE_SYS_STAT_H`, `HAVE_SYS_TIME_H`, `HAVE_UNISTD_H`, `STDC_HEADERS`, and `NDEBUG`, regardless of target platform. `threads.cpp`, `cmdlib.cpp`, `filelib.cpp`, etc. gate code on these macros.
- **Impact**: A platform missing one of these headers will not be detected; the build will fail or silently use the wrong code path.
- **Recommendation**: Use `check_include_file`/`check_symbol_exists` and propagate results, or document that only Linux/macOS/Windows are supported. At minimum, split macOS/BSD assumptions from Linux.

### H2. `NDEBUG` and `-O2` are forced for every configuration
- **File:line**: `CMakeLists.txt:32,41,95-105`
- **Evidence**: `NDEBUG` is in `VHLT_PLATFORM_DEFINITIONS` for both platforms, and MSVC/Unix compile options always include `/O2` or `-O2`. `cmdlib.h:70` uses `_DEBUG` to choose the version string, so debug builds can never report a debug version.
- **Impact**: Debug builds are optimized and assertions disabled, making diagnosis difficult; sanitizer/debug workflows are misleading.
- **Recommendation**: Remove `NDEBUG` from the platform definitions and use generator expressions (`$<$<CONFIG:Release>:-O2>`), or define it only for Release.

### H3. MinGW `alloca` redefinition
- **File:line**: `common/win32fix.h:14` (definition), triggered in 94 translation units
- **Evidence**: Build log: `warning: 'alloca' redefined`. On MinGW, `win32fix.h` defines `alloca _alloca` while the MinGW headers already define `alloca`.
- **Impact**: 94 warnings on the primary Windows toolchain; risk of macro mismatch if `_alloca` and the builtin differ.
- **Recommendation**: Guard the macro with `#ifndef alloca` or remove it on MinGW and rely on the compiler builtin.

### H4. Unguarded MSVC `#pragma warning` directives
- **File:line**: `common/boundingbox.h:6`, `common/winding.cpp:1`, `hlbsp/outside.cpp:1`, `hlrad/transparency.cpp:1`, `hlcsg/wadinclude.cpp:1`, plus `common/cmdlib.h` (guarded block ~`cmdlib.h:689`)
- **Evidence**: 704 `unknown-pragmas` warnings. Several pragma directives are emitted unconditionally, even on GCC/Clang.
- **Impact**: Noise on every non-MSVC build; some warnings may mask real issues.
- **Recommendation**: Wrap all `#pragma warning(...)` in `#ifdef _MSC_VER`.

### H5. No Windows CI coverage
- **File:line**: `.github/workflows/.github.yml:10-12`
- **Evidence**: Matrix only `ubuntu-latest` and `macos-latest`, with `g++` and `clang++`; no Windows/MSVC or MinGW job. `actions/checkout@v2` is also obsolete.
- **Impact**: The Windows/MinGW path (which produces the `alloca` redefinition and other warnings) is never exercised in CI.
- **Recommendation**: Add a Windows matrix entry (MinGW and/or MSVC) and update checkout to v4.

### H6. Makefile is Unix-only
- **File:line**: `Makefile` throughout (e.g. `rm -rf`, `mkdir -p`, `cp`)
- **Evidence**: The Makefile uses Unix shell commands and hardcodes Linux macros (`VERSION_LINUX`, `SYSTEM_POSIX`).
- **Impact**: Not usable on Windows without a POSIX environment; divergent from CMake feature set.
- **Recommendation**: Either document Makefile as Linux-only (it already says so) or remove it in favor of CMake. If retained, honor `CXXFLAGS`/`LDFLAGS` and avoid `printusage` as a prerequisite of compile rules.

## Medium severity

### M1. `INT64`/`UINT64` are undefined on POSIX
- **File:line**: `template/basictypes.h:46-52` (POSIX branch leaves `INT64`/`UINT64` commented out); consumers in `template/EndianMath.h`
- **Evidence**: `EndianMath.h` uses `INT64`/`UINT64` extensively. `EndianMath.h` is not currently included by any source, so this does not break the build.
- **Impact**: Any future use of `EndianMath.h` on POSIX will fail to compile; the header is effectively non-portable.
- **Recommendation**: Define `INT64`/`UINT64` as `long long`/`unsigned long long` on POSIX, or delete the unused header.

### M2. Inconsistent platform version selection
- **File:line**: `CMakeLists.txt:22-26,38-39`
- **Evidence**: On non-Windows, `VHLT_VERSION_MACRO` (`VERSION_32BIT`/`VERSION_64BIT`) is computed but only `VERSION_LINUX` is defined; `cmdlib.h:74-82` treats `VERSION_LINUX` as its own platform class.
- **Impact**: 64-bit Linux is not represented as 64-bit in `PLATFORM_VERSIONSTRING`; logic is confusing but currently functional.
- **Recommendation**: Clarify whether Linux should also define `VERSION_64BIT`/`VERSION_32BIT`, and simplify the macro set.

### M3. `WIN32` compile definition is unused and inconsistent
- **File:line**: `CMakeLists.txt:34`; only consumers are the unused `template/Reference*.h`
- **Evidence**: `WIN32` is added on Windows, but source code keys off `SYSTEM_WIN32`/`_WIN32`. Makefile does not define `WIN32`.
- **Impact**: Misleading configuration; potential macro collisions with the Windows SDK.
- **Recommendation**: Remove `WIN32` from `VHLT_PLATFORM_DEFINITIONS`.

### M4. String literals assigned to `char*`
- **File:line**: `common/log.cpp:43` (`g_Program`), `hlrad/qrad.cpp:4855`, `ripent/ripent.cpp:941,944`, and ~28 total sites
- **Evidence**: Build warnings `ISO C++ forbids converting a string constant to 'char*'`.
- **Impact**: Deprecated conversion; undefined behavior if the string is ever modified.
- **Recommendation**: Change `g_Program` (and similar) to `const char*` (update `common/log.h:extern` accordingly) and adjust call sites.

### M5. `memcpy`/`memset`/`memmove` on non-trivial `brush_t`
- **File:line**: `hlcsg/map.cpp:37,95,728,1348,1359,1363`
- **Evidence**: Build warnings `writing to an object of non-trivially copyable type 'brush_t'`.
- **Impact**: Technically undefined behavior; may break under stricter optimizers.
- **Recommendation**: Replace raw byte copies with copy-assignment or value initialization, or make `brush_t` trivially copyable if intended.

### M6. Maybe-uninitialized and sign-compare warnings
- **File:line**: representative `hlrad/qrad.cpp:738`, `hlrad/qrad.cpp:2364`, `hlrad/qrad.cpp:3274`; 125 sign-compare sites across the codebase
- **Evidence**: Build log categories `maybe-uninitialized` (45) and `sign-compare` (125).
- **Impact**: Potential latent bugs, especially in radiosity loops; noise hides real issues.
- **Recommendation**: Enable `-Werror` only after fixing these, or at least fix radiosity/math hot paths.

### M7. `hlvis/vis.cpp` uses `alloca` for temporary stack storage
- **File:line**: `hlvis/flow.cpp` (single use found)
- **Evidence**: `stack.clipPlane = (plane_t*)alloca(...)`.
- **Impact**: Stack overflow risk on large inputs; non-portable allocation.
- **Recommendation**: Use a `std::vector`/scoped buffer or increase stack size documentation (README already warns about 4 MB stack).

### M8. Feature-flag macros create a combinatorial build matrix
- **File:line**: `common/cmdlib.h` (~60-560)
- **Evidence**: Hundreds of `#define`/`#ifdef` feature toggles, most always on; `ZHLT_NETVIS` and `ZHLT_XASH` paths are never enabled by the build systems.
- **Impact**: Large amounts of dead code are compiled out; maintenance and review burden; untested paths.
- **Recommendation**: Remove or document disabled features (`ZHLT_NETVIS`, `ZHLT_XASH`, `ZHLT_PROGRESSFILE`, etc.) and consider splitting optional features into CMake options.

### M9. `HAVE_CONFIG_H` path is never generated
- **File:line**: `common/cmdlib.h:621-623`
- **Evidence**: `#ifdef HAVE_CONFIG_H #include "config.h" #endif`, but neither CMake nor Makefile generates `config.h` or defines `HAVE_CONFIG_H`.
- **Impact**: The config-header mechanism is dead; harmless but misleading.
- **Recommendation**: Either implement config-header generation in CMake or remove the block.

## Low severity

### L1. Unused/dead headers and files
- **File:line**: `common/TimeCounter.h` (0 references), `template/BaseMath.h`, `template/EndianMath.h`, `template/ReferenceArray.h`, `template/ReferenceCounter.h`, `template/ReferenceObject.h`, `template/ReferencePtr.h` (0-1 references)
- **Evidence**: Include scan shows these headers are not included by any compiled source.
- **Recommendation**: Remove unused headers or move them to an `experimental/` directory.

### L2. `ZHLT_NETVIS` code is never built
- **File:line**: widespread (`common/cmdlib.h`, `hlvis/vis.cpp`, `hlvis/flow.cpp`, etc.)
- **Evidence**: `ZHLT_NETVIS` is never defined by CMake or Makefile.
- **Recommendation**: Either remove the dead netvis code path or add a CMake option to enable it.

### L3. Unguarded `#pragma warning` in headers
- **File:line**: `common/boundingbox.h:6`, `hlcsg/csg.h:8`
- **Evidence**: Pragmas appear before/without `_MSC_VER` guards.
- **Recommendation**: Guard all MSVC pragmas.

### L4. `alloca` macro is defined but rarely used
- **File:line**: `common/cmdlib.h:625-631` (`#if 0` block), `common/win32fix.h:14`
- **Evidence**: Only one call site in `hlvis/flow.cpp`.
- **Recommendation**: Keep a single portable definition or replace the call site.

### L5. `VERSION_OTHER` is a supported but never produced macro
- **File:line**: `common/cmdlib.h:74-82`
- **Evidence**: Build systems define `VERSION_32BIT`, `VERSION_64BIT`, or `VERSION_LINUX`; `VERSION_OTHER` is accepted but never emitted.
- **Recommendation**: Remove the dead branch or document how to select it.

### L6. `ZHLT_VERSIONSTRING` debug/release mismatch
- **File:line**: `common/cmdlib.h:16-20`
- **Evidence**: Uses `_DEBUG`, but `NDEBUG` is always defined (see H2).
- **Recommendation**: Fix alongside H2.

### L7. Makefile `printusage` is a prerequisite of compile rules
- **File:line**: `Makefile` object rules (`$(HLCSG_CPPFILES:...): ... printusage`)
- **Evidence**: `printusage` runs `head -n 35 Makefile` for every incremental build.
- **Recommendation**: Remove `printusage` from prerequisite lists; keep it in `all`.

### L8. Makefile does not honor standard environment flags
- **File:line**: `Makefile` `COMMON_FLAGS`
- **Evidence**: Only `USER_FLAGS` is appended; `CXXFLAGS`/`LDFLAGS` are ignored.
- **Recommendation**: Append `$(CXXFLAGS)` and use `$(LDFLAGS)` at link time.

### L9. Doxygen target uses deprecated variable form
- **File:line**: `CMakeLists.txt:140-148`
- **Evidence**: Uses `DOXYGEN_FOUND`/`DOXYGEN_EXECUTABLE` variables.
- **Recommendation**: Use the imported `Doxygen::doxygen` target for modern CMake.

### L10. CMake does not export compile commands
- **File:line**: `CMakeLists.txt` (top-level)
- **Evidence**: No `CMAKE_EXPORT_COMPILE_COMMANDS`.
- **Recommendation**: Enable it for tooling/IDE support.

## Suggested priority order

1. H2 + H4 (make debug builds meaningful and remove pragma noise).
2. H3 (MinGW `alloca`).
3. H1 + M1/M2 (platform/header detection and POSIX type definitions).
4. M4 + M5 (const-correctness and trivial-copyability).
5. L1 + M8 (dead code cleanup).
6. H5 (CI coverage).

## Notes

- The codebase builds successfully on the tested configuration; all items above are hygiene/portability improvements, not build blockers.
- Feature macros in `common/cmdlib.h` are intentionally conservative; changing them can alter numerical results and should be validated against reference output.
