# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Documentation
- Added `docs/user/max-quality.md`, a final-quality compile preset that explains each setting's effect, default and cost. It is linked from the manual and README and installed in the release ZIP.

### Fixed
- Lint: `VHLT_CLANG_TIDY=ON` now passes Zig's system include paths and libc++ macros to clang-tidy, so system headers resolve. clang-tidy is pinned to 22.1.8 to match Zig 0.17.1's clang.
- hlrad: free face windings that do not become patches, clear the luxel flag array before use, initialize transfer decompression temporaries, and fix a 32-bit shift in the transfer bit helpers.
- hlbsp: do not read children of leaf clipnodes, free the surface-chain wrapper after `SolidBSP`, and stop with a clear error if a detail node has no `portalleaf`.
- hlcsg: stop after a plane-with-no-normal `Fatal`, avoid forming `g_texinfo[-1]`, free the `writewad` lump-info array, and guard zero-size allocations.
- hlvis: avoid division by zero when a map has no portal leaves.
- common/winding: do not read `sides[0]` for empty windings.
- Memory handling: commit `realloc` results through temporaries and guard zero-size allocations in bspfile, cmdlinecfg, brush, lightmap, loadtextures and vis.

## [3.5.3] - 2026-11-10

### Added
- Release binaries for Linux x86_64 (glibc 2.28+), built from the same sources and compiler as Windows.
- clang-format 23.1.5 style (`.clang-format`) with `format` and `format-check` targets. The whole codebase is formatted; the reformat commit is in `.git-blame-ignore-revs`. Compiled output is unchanged.
- `tools/regress.ps1` and `tools/regress.sh`: map-compile regression harness. It hashes `.bsp`, `.prt` and entity output of single-threaded compiles against a recorded baseline; the assets stay outside the repository.

### Fixed
- hlrad: the `-smooth2` defaults are logged without passing an unused format argument.
- hlrad: bad-face diagnostics print face indices with the correct type on 64-bit builds.
- hlrad/hlvis: distance sentinels are written as float literals, removing an implicit narrowing conversion (compiled output is unchanged).
- ripent: `-textureexport` no longer marks textures that reference an external WAD as missing (regression in 3.5.2).
- hlcsg: reverted the 3.5.2 `IPlaneEquiv` change that treated opposite-facing planes as duplicates and could drop faces from expanded hulls.
- hlrad: the `GetPhongNormal` degenerate-geometry guard is now scale-invariant.

### Security
- hlrad: texture names read from `lights.rad` are bounded by `MAXTOKEN`; a long name could overflow a `_MAX_PATH` stack buffer (audit H12).
- common log and ripent error paths pass messages as `%s` instead of using them as format strings, so a `%` in a message or path can no longer be interpreted as a format specifier.
- Reject BSP files smaller than the BSP header.
- ripent: range-check the WAD `infotableofs` before using it in size arithmetic.

### Changed
- clang-tidy findings are now errors (`WarningsAsErrors: '*'`); a full clean lint run reports zero findings.
- Command-line numeric options are parsed strictly: an invalid value stops the tool with `invalid value '<value>' for <option>` instead of silently becoming 0. Map, entity and data-file numbers keep their lenient parsing.
- Both presets now build with `-Werror` (CMake option `VHLT_WERROR`, ON by default); the 154 compiler warning sites in the cleanup baseline are fixed.
- `MAX_MAP_PORTALS` is defined once in `common/bspfile.h` and shared by hlbsp and hlvis.
- Build toolchain is now Zig 0.17.1 (bundled Clang) with Ninja, for Windows and Linux (cross-compiled, glibc 2.28+). Presets `windows` and `linux`; toolchain files in `cmake/toolchains/`. Release binaries are stripped, and Windows builds depend only on system DLLs.
- Lint uses clang-tidy (`-DVHLT_CLANG_TIDY=ON`, checks in `.clang-tidy`) instead of cppcheck.

### Removed
- MinGW GCC build instructions, the legacy POSIX `Makefile`, the MSVC compile options, and the GitHub Actions workflow (`.github/`).
- Unused headers: `template/` (`BaseMath.h`, `EndianMath.h`, `Reference*.h`) and `common/TimeCounter.h`. The one used header, `basictypes.h`, moved to `common/`.
- Individual audit reports already merged into `audits/2026-11-10/final-audit.md`. Root-level audit files moved under `audits/2026-11-10/`.

### Documentation
- README: Zig/Ninja requirements, `windows` and `linux` presets, targets, install layout, smoke behaviour, `-Werror` and clang-tidy.
- Developer guide: toolchain files, `Doxyfile.in`, clangd, thread clamp, shared input-validation helpers, regression harness.
- AGENTS.md: toolchain rule (Zig only), gates for both presets, zero warnings, map regression, package.
- Manual: `-threads` range.
- Added `docs/llms.txt`, a complete agent reference linked from AGENTS.md and README.
- Added `.env.example` (`GITEA_ACCESS_TOKEN`, optional `CMAKE` and `ZIG` paths).
- Added `docs/developer/warning-cleanup-plan.md` (completed): warning baseline and the batch decisions.
- Audit status (`audits/2026-11-10/handling.md`) corrected for H12, the reverted `IPlaneEquiv` change, and CI removal.

## [3.5.2] - 2026-11-10

### Added
- CMake build system (`CMakeLists.txt`, `CMakePresets.json`) as the primary build.
- Doxygen configuration and `docs` build target.
- CPack ZIP packaging (`cmake --build build --target package`).
- `AGENTS.md` with coding standards, gates, and lifecycle rules.
- `audits/` consolidated audit report.
- `docs/user/manual.md` and `docs/developer/developer.md` (docs reorganisation).
- Multicore cppcheck lint target and clangd support.

### Fixed
- Thread pool clamping on all platforms, POSIX affinity counting, and thread lifecycle handling.
- BSP lump validation (incl. clipnodes capacity), checksum, `AddLump` padding, and `GetTextureByNumber` bounds.
- Entity lump writer NUL-termination bounds checks.
- Shared miptex header validation used by hlrad and ripent texture readers.
- WAD/miptex validation and hlrad/ripent texture loading hardening.
- ripent WAD/.tex readers validate headers before allocating or reading.
- hlvis portal validation, allocation checks, and winding bounds.
- hlcsg texdata handling, worldspawn checks, plane equivalence, and hull file handling.
- hlrad transfer file validation (indices, run sums, allocation bounds).
- hlrad `-blur` clamped to a documented maximum; lmcache sizes computed in `size_t`.
- Phong normal degenerate-edge guard.
- Allocation checks and float-compression shift clamp in hlrad.
- Removed unconditional `NDEBUG` from CMake platform definitions.
- `strcpy_s`/`sprintf_s` compatibility macros removed; callers use safe helpers.

### Changed
- Tidied codebase and removed Visual Studio-era project files.
- Doxygen output follows the build directory via `configure_file`.
- CI matrix includes Windows and smokes the release version.
- `docs/api/Doxyfile` moved to `docs/api/Doxyfile.in`.

## [3.5.1] - 2026-11-10

### Fixed
- Heap corruption in `Winding::resize`.
- `safe_snprintf` truncation detection and `safe_strncat` overflow.
- Out-of-bounds BSP texinfo endian conversion.
- Unbounded `.vis` parsing and hull filename construction.
- Thread-pool critical section and POSIX mutex lifecycle issues.
- Various string-safety and parser hardening fixes.
