# AGENTS.md

## Project

Vluzacn ZHLT (VHLT) is a set of Half-Life compilation tools: `hlcsg`, `hlbsp`, `hlvis`, `hlrad`, and `ripent`. The repository also contains shared common code, templates, and documentation.

**Agent context:** read [`docs/llms.txt`](docs/llms.txt) first. It is the complete agent reference: pipeline and file flow, layout, feature macros, limits, core APIs, concurrency, gotchas, and the release checklist. Keep it current when the build, limits, APIs or rules change.

## Build

Primary build system: CMake (3.16 or newer). If `.env` sets `CMAKE`, use that path in place of `cmake` below; see `.env.example`. `.env` is git-ignored and holds secrets, so never commit it or print its values.

```sh
cmake -S . -B build -G "MinGW Makefiles"   # Windows/MinGW
cmake --build build
```

On Linux/macOS:

```sh
cmake -S . -B build
cmake --build build
```

Generate documentation:

```sh
cmake --build build --target docs
```

Package deliverables (ZIP):

```sh
cmake --build build --target package
```

Outputs are written to `build/bin/`, `build/docs/html/`, and `build/*.zip`.

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

1. Configure: `cmake -S . -B build`
2. Build: `cmake --build build` (all five tools)
3. Docs: `cmake --build build --target docs`
4. Smoke test: each tool runs with no arguments and prints its version banner (exit code 1 is expected)
5. Package: `cmake --build build --target package`
6. Lint: `cmake --build build --target lint` (cppcheck, multicore)
7. LSP: clangd supported via `.clangd` and `build/compile_commands.json`
8. Working tree reviewed for accidental artifacts

## Outputs

- Binaries: `build/bin/{hlcsg,hlbsp,hlvis,hlrad,ripent}`
- Documentation: `build/docs/html/index.html`
- Package: `build/vhlt-<version>-<platform>.zip`
- Audits: `audits/<date>/`

## Changelog Lifecycle

- Every user-visible change must be recorded in `CHANGELOG.md` under `[Unreleased]`.
- On release, move `[Unreleased]` entries into a new version section and update the version in `CMakeLists.txt`, `common/cmdlib.h`, `docs/api/Doxyfile.in`, and the smoke-test version check in `.github/workflows/.github.yml`.
- Create an annotated git tag `v<major>.<minor>.<patch>`.
- Commit and tag only after all gates pass.

## Audit Cleanup

- Individual audit reports may be consolidated; the consolidated report is the canonical record.
- Store audit artifacts under `audits/<date>/`.
- Do not leave temporary build artifacts (`build/`, `buildwarn.txt`, `*.o`) in the repository.
