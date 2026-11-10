# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- CMake build system (`CMakeLists.txt`, `CMakePresets.json`) as the primary build.
- Doxygen configuration and `docs` build target.
- CPack ZIP packaging (`cmake --build build --target package`).
- `AGENTS.md` with coding standards, gates, and lifecycle rules.
- `audits/` consolidated audit report.
- `docs/user/manual.md` and `docs/developer/developer.md`.
- Multicore cppcheck lint target and clangd support.

### Fixed
- Thread pool clamping, POSIX affinity counting, and thread lifecycle handling.
- BSP lump validation, checksum, `AddLump` padding, and `GetTextureByNumber` bounds.
- WAD/miptex validation and hlrad/ripent texture loading hardening.
- hlvis portal validation and winding bounds.
- hlcsg texdata handling, worldspawn checks, and hull file handling.
- hlrad transfer and vismatrix integer safety.
- Removed unconditional `NDEBUG` from CMake platform definitions.

### Changed
- Tidied codebase and removed Visual Studio-era project files.
- Improved MinGW/64-bit compatibility.

## [3.5.1] - 2026-11-10

### Fixed
- Heap corruption in `Winding::resize`.
- `safe_snprintf` truncation detection and `safe_strncat` overflow.
- Out-of-bounds BSP texinfo endian conversion.
- Unbounded `.vis` parsing and hull filename construction.
- Thread-pool critical section and POSIX mutex lifecycle issues.
- Various string-safety and parser hardening fixes.

### Added
- Audit handling status document.

## [3.5.0] - 2026-11-10

### Added
- Port to CMake.
- Documentation generation via Doxygen.
- `CMakePresets.json`.

### Changed
- Removed Visual Studio solution/project files.
- Tidied source formatting and removed dead files.

## [3.4.0] - original

### Notes
- Original Vluzacn ZHLT v3.4 codebase with POSIX threading support.
