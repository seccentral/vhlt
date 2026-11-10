# AGENTS.md

## Project

Vluzacn ZHLT (VHLT) is a set of Half-Life compilation tools: `hlcsg`, `hlbsp`, `hlvis`, `hlrad`, and `ripent`. The repository also contains shared common code, templates, and documentation.

**Agent context:** read [`docs/llms.txt`](docs/llms.txt) first. It is the complete agent reference: pipeline and file flow, layout, feature macros, limits, core APIs, concurrency, gotchas, and the release checklist. Keep it current when the build, limits, APIs or rules change.

## Build

Primary build system: CMake (3.16 or newer) with Ninja and Zig 0.17.1 as the only compiler, for both Windows and Linux. Do not use or add MinGW GCC, MSVC or any other toolchain. If `.env` sets `CMAKE` or `ZIG`, use those paths (see `.env.example`). `.env` is git-ignored and holds secrets, so never commit it or print its values.

```sh
cmake --preset windows && cmake --build --preset windows   # build/windows
cmake --preset linux   && cmake --build --preset linux     # build/linux (cross-compiled)
cmake --build build/windows --target docs                  # needs Doxygen
cmake --build build/windows --target package               # ZIP
```

Outputs are written to `build/<preset>/bin/`, `build/<preset>/docs/html/`, and `build/<preset>/*.zip`.

## Coding Standards

- Prefer modern, safe C++ practices while preserving existing behavior.
- Use bounded string operations (`safe_snprintf`, `safe_strncpy`, `safe_strncat`) instead of raw `strcpy`/`sprintf`/`strcat`.
- Validate array bounds and counts before indexing or allocating.
- Initialize all variables and capacity fields.
- Avoid unbounded stack allocations; use heap allocation with size checks.
- Guard platform-specific and compiler-specific directives (`#ifdef _MSC_VER`, `#ifdef SYSTEM_WIN32`).
- Keep changes minimal and focused; do not change function behavior unless fixing a defect.
- Update documentation and comments when behavior changes.

## Documentation

- Public API and shared helpers should have Doxygen comments.
- User-facing options must be documented in `docs/user/manual.md` and kept in sync with tool help output.
- Keep `README.md` accurate for build and usage instructions.
- Keep `docs/llms.txt` accurate for agents (limits, macros, APIs, gates, gotchas).
- Regenerate docs with the `docs` target after API or option changes.

## Gates

Before committing, all of the following must pass:

1. Configure and build both presets: `windows` and `linux` (all five tools each)
2. Zero compiler warnings; both presets build with `-Werror` (`VHLT_WERROR`, ON by default)
3. Docs: `cmake --build build/windows --target docs`
4. Smoke test: each Windows tool runs with no arguments and prints its version banner (exit code 1 is expected)
5. Package: `cmake --build build/<preset> --target package` for both presets
6. Lint: configure with `-DVHLT_CLANG_TIDY=ON` and build (clang-tidy, checks in `.clang-tidy`)
7. Map regression: `tools/regress.ps1` matches the recorded baseline (assets live outside the repo)
8. LSP: clangd supported via `.clangd` and `build/windows/compile_commands.json`
9. Working tree reviewed for accidental artifacts

## Outputs

- Binaries: `build/windows/bin/*.exe`, `build/linux/bin/*`
- Documentation: `build/windows/docs/html/index.html`
- Packages: `build/windows/vhlt-<version>-win64.zip`, `build/linux/vhlt-<version>-Linux.zip`
- Audits: `audits/<date>/`

## Changelog Lifecycle

- Every user-visible change must be recorded in `CHANGELOG.md` under `[Unreleased]`.
- On release, move `[Unreleased]` entries into a new version section and update the version in `CMakeLists.txt`, `common/cmdlib.h`, and `docs/api/Doxyfile.in`.
- Create an annotated git tag `v<major>.<minor>.<patch>`.
- Commit and tag only after all gates pass.

## Audit Cleanup

- Individual audit reports may be consolidated; the consolidated report is the canonical record.
- Store audit artifacts under `audits/<date>/`.
- Do not leave temporary build artifacts (`build/`, `buildwarn.txt`, `*.o`) in the repository.
