# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed
- ripent: `-textureexport` no longer marks textures that reference an external WAD as missing (regression in 3.5.2).
- hlcsg: reverted the 3.5.2 `IPlaneEquiv` change that treated opposite-facing planes as duplicates and could drop faces from expanded hulls.
- hlrad: the `GetPhongNormal` degenerate-geometry guard is now scale-invariant.

### Security
- Reject BSP files smaller than the BSP header.
- ripent: range-check the WAD `infotableofs` before using it in size arithmetic.

### Changed
- `MAX_MAP_PORTALS` is defined once in `common/bspfile.h` and shared by hlbsp and hlvis.

### Documentation
- README: links to the manual, developer guide and changelog; lists the package and lint targets, preset, install layout, CI and smoke behaviour; marks the Makefile as POSIX-only.
- Developer guide: `Doxyfile.in`, CI and clangd notes, thread clamp, and shared input-validation helpers.
- AGENTS.md: package gate, smoke exit code, CI version check in the release checklist.
- Manual: `-threads` range.
- Added `docs/llms.txt`, a complete agent reference linked from AGENTS.md and README.
- Added `.env.example` (`GITEA_ACCESS_TOKEN`, optional `CMAKE` path) and documented how to select the CMake to use.

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
