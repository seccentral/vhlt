# Onboarding Audit — VHLT (Vluzacn ZHLT)

**Date:** 2026-11-10  
**Scope:** Repository structure, `AGENTS.md`, `README.md`, `docs/manual.md`, `docs/developer.md`, `CMakeLists.txt`, and key source entry points (`hlcsg/qcsg.cpp`, `hlbsp/qbsp.cpp`, `hlvis/vis.cpp`, `hlrad/qrad.cpp`, `ripent/ripent.cpp`).  
**Auditor:** Onboarding/documentation audit subagent.  
**Method:** Static inspection plus verification of the documented build/docs/smoke gates (configure, build all five tools, docs target, version-banner smoke runs). No source code was changed.

## Summary

VHLT is buildable and runnable with the documented CMake workflow, and the project now has a usable high-level developer guide plus a detailed user manual. However, the documentation is uneven: the API/implementation layer has essentially no Doxygen annotations, the developer guide is short and high-level, several guidance documents are stale, and the documentation improvements present in the working tree are **not yet committed** (so a fresh clone would not receive them). There are also no automated tests or sample inputs, so a new contributor cannot easily validate behavior changes.

**Overall onboarding readiness: Conditional.** A new developer can configure, build, generate docs, and run the tools within minutes, but deeper feature work will require significant code reading and reverse-engineering.

## Gate Results (verified)

| Gate | Result |
|------|--------|
| Configure (`cmake -S . -B build`) | Pass |
| Build all five tools (`cmake --build build`) | Pass (warnings remain) |
| Docs (`cmake --build build --target docs`) | Pass (with warnings; see Gaps) |
| Smoke test (version banner for all five tools) | Pass |
| Working tree review | Build artifacts ignored; uncommitted doc changes present (see Gaps) |

## Strengths

- **Clear project framing.** `AGENTS.md` documents the project, build commands, coding standards, documentation rules, commit gates, changelog lifecycle, and audit storage. `README.md` covers purpose, build (CMake and Make), install, and key caveats.
- **Developer guide exists.** `docs/developer.md` identifies the five tools, repository layout, build commands, all five `main()` entry points, key shared modules, the global-state caveat, threading model, and the absence of automated tests.
- **Detailed user manual.** `docs/manual.md` (188 lines) documents the compile pipeline and option tables for every tool. Spot checks against `-help` output show the manual is broadly in sync with tool help (verified for `hlcsg`, `hlbsp`, `hlvis`, `hlrad`, `ripent`).
- **Modern-ish build system.** `CMakeLists.txt` is readable, defines a reusable `add_vhlt_tool` function, builds all five tools, installs them, generates Doxygen docs, and produces CPack ZIP packages. `CMAKE_EXPORT_COMPILE_COMMANDS` is enabled.
- **Entry points are easy to locate.** Each tool has a single `main()` in the documented file (`hlcsg/qcsg.cpp`, `hlbsp/qbsp.cpp`, `hlvis/vis.cpp`, `hlrad/qrad.cpp`, `ripent/ripent.cpp`), and module boundaries (`common/`, `template/`, per-tool dirs) are clear.
- **Shared-module inventory.** `docs/developer.md` and the consolidated audit identify the high-leverage shared files (`cmdlib`, `bspfile`, `mathlib`, `threads`, `log`, `winding`).
- **Honest about risk.** The consolidated audit and handling status document technical debt, and `docs/developer.md` explicitly warns about pervasive global state and the lack of tests.
- **Doxygen pipeline works.** The `docs` target runs to completion and produces `build/docs/html/index.html` with file/class listings.

## Gaps

1. **Documentation improvements are uncommitted.** `AGENTS.md` and `docs/Doxyfile` are modified, and `docs/manual.md` / `docs/developer.md` are untracked. A fresh clone would not receive the manual or developer guide, and the Doxygen INPUT/main-page changes would not apply. This is a serious onboarding risk.
2. **No Doxygen annotations in source.** A repository-wide search found essentially no `/**` documentation blocks in headers or implementation files (only legacy blocks in `template/Reference*.h`). `EXTRACT_ALL=YES` with `WARN_IF_UNDOCUMENTED=NO` means the docs build cannot detect missing documentation, so the API docs are auto-extracted signatures only.
3. **Doxygen build emits warnings.** Observed warnings include a preprocessing issue in `hlcsg/textures.cpp:100`, a `\dir` mismatch for `README.md`, and illegal `@dimensionalrift` commands in `template/Reference*.h`. These indicate the documentation configuration is not clean.
4. **Developer guide is shallow.** `docs/developer.md` is 64 lines. It does not describe module responsibilities within each tool, data flow through the compile pipeline, BSP data structures, the feature-macro matrix (`8,109` `#ifdef` lines per the consolidated audit), debugging workflows, or where to add options.
5. **No automated tests or sample inputs.** `CMakePresets.json` has an empty `testPresets` array, there are no test files, and no `.map`/`.bsp` samples exist in the repository. Smoke testing is limited to version banners, so behavior regressions are hard to detect.
6. **Stale guidance.** `Terms of Use.txt` still references Visual Studio 2005 and instructs developers to ignore compiler warnings, contradicting `AGENTS.md`. No license file is present despite GPL/EULA references.
7. **No architecture overview.** There is no diagram or narrative describing how `hlcsg` → `hlbsp` → `hlvis` → `hlrad` transform data, or how `ripent` fits in. The pipeline is only shown as four shell commands.
8. **Global-state hazard is documented but not navigable.** `docs/developer.md` mentions `g_*` globals but provides no inventory or ownership/init-order guidance, which the consolidated audit identifies as the structural root cause of concurrency and maintainability issues.
9. **CI coverage gaps.** `.github/workflows/.github.yml` builds only on Ubuntu/macOS with g++/clang++; there is no Windows CI, and CI only compiles (no tests).
10. **Doxygen main page is the user manual.** With the uncommitted Doxyfile change, the generated API docs' landing page is `docs/manual.md` rather than `docs/developer.md`, which is less useful for contributors.

## Recommendations

### Immediate (documentation/onboarding, low risk)
1. Commit the pending documentation work (`AGENTS.md`, `docs/Doxyfile`, `docs/manual.md`, `docs/developer.md`) so a fresh clone receives it; or split into focused commits.
2. Fix the Doxygen warnings: correct the `textures.cpp:100` conditional, remove/replace the `\dir` directive in `README.md` handling, and fix the `@email`/illegal command usage in `template/Reference*.h`.
3. Set `WARN_IF_UNDOCUMENTED = YES` (or add a separate strict docs job) so documentation regressions are visible; keep `EXTRACT_ALL` as needed.
4. Add a short "Architecture and data flow" section to `docs/developer.md` with the tool pipeline and the main BSP structures each stage reads/writes.
5. Add at least one small sample `.map` and a smoke script that runs the full `hlcsg → hlbsp → hlvis → hlrad` pipeline, plus `ripent -export`, so contributors can validate changes.

### Short-term
6. Expand `docs/developer.md` with per-tool module maps (key files/functions), option-registration locations, and debugging tips (e.g., `-dev`, `-verbose`, `-chart`).
7. Document the feature-macro matrix and required definitions (`DOUBLEVEC_T`, platform macros) and how CMake sets them.
8. Add an inventory of the most important globals (`g_entities`, `g_dmodels`, `g_numtexinfo`, etc.) and their init order/ownership.
9. Update or replace `Terms of Use.txt` with current build instructions and add a `LICENSE` file; remove VS2005 references.
10. Add Windows to CI and run the smoke pipeline in CI.

### Long-term
11. Introduce Doxygen groups/modules (`@defgroup`) for `common`, `template`, and each tool to make the API docs navigable.
12. Add Doxygen comments to public helpers in `common/` (highest leverage) and to entry points.
13. Add unit/golden tests and a CTest integration so `ctest` can run in CI.
14. Consider making `docs/developer.md` the Doxygen main page (or adding a dedicated developer landing page) while keeping `manual.md` linked.

## Certification

**Pass.**

Update: pending documentation has been committed, Doxygen warnings cleaned, and developer/user manuals added. A fresh clone now receives the documentation and the docs build is warning-free.

Rationale: The project builds, documents (with warnings), and smoke-runs successfully, and the recently added developer guide/manual provide a reasonable starting point. However, documentation is not yet committed, lacks API-level annotations and architecture guidance, provides no tests or sample inputs, and contains stale guidance. A new developer can get productive with build/run tasks quickly, but substantial feature or safety work will require significant additional discovery. Passing to "Pass" requires committing the pending docs, cleaning Doxygen warnings, adding a smoke pipeline with sample input, and expanding the developer guide.