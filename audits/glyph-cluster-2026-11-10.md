# Codebase Analysis Findings — Glyph Cluster (2026-11-10)

Scope: static review of repository sources, build/configuration, CI, docs, and local workspace state in `<repo>`. No source fixes applied.

## High-impact issues

- **Unsafe string macros on MinGW/old MSVC** (`common/cmdlib.h:607-619`): `strcpy_s`/`sprintf_s` are redefined to raw `strcpy`/`sprintf` and deprecation warnings disabled. Call sites still using these macros (e.g. `common/cmdlib.cpp:408`, `common/scriplib.cpp:51,85`, `hlcsg/qcsg.cpp:1658`, `hlcsg/wadcfg.cpp:316`) lose bounds checking on that toolchain.
- **Unbounded extension-copy API** (`common/cmdlib.h:708`, `common/cmdlib.cpp:253-273,389-408`): `ExtractFileExtension(path, dest)` takes no destination size and copies via `memcpy`/`strcpy_s`; every caller must guarantee capacity, creating a latent overflow surface.
- **Parser/input hardening gaps**:
  - `hlvis/vis.cpp:872-912` (`InitVisBlock`): `while(!feof(fp))` combined with unchecked `fscanf` results; malformed/truncated `.vis` data can leave fields stale or drive incorrect iteration despite partial clamping.
  - `hlbsp/qbsp.cpp:1133-1226`, `common/bspfile.cpp:811-826,1679-1694`, multiple `hlcsg/*` paths, and `ripent/ripent.cpp:640-760` contain many unchecked/weak `fscanf`/`sscanf`/formatted reads (inconsistent width/return handling), leaving crafted or corrupt map/BSP/text inputs as a risk area.
  - `hlcsg/map.cpp:1221`: raw `sprintf(versionstring, ...)` instead of bounded formatting.
- **Assertions/safety checks largely disabled in shipped builds**: `CMakeLists.txt:33,42` and `Makefile:81` force `NDEBUG`; `common/hlassert.h` makes `hlassert` a no-op outside debug builds, so defensive `hlassert`/`assume` checks do not protect release binaries.

## Memory/resource and robustness concerns

- **Allocation/error handling is inconsistent** across core code (many `malloc`/`calloc`/`realloc`/`new` sites, e.g. `hlvis/flow.cpp:538,614,837,902,968,995`, `hlrad/lerp.cpp`, `hlrad/lightmap.cpp`, `common/blockmem.cpp`) with limited null/result checking in numerous paths; `audits/2026-11-10/consolidated.md` indicates this is systemic rather than isolated.
- **Custom/legacy memory and threading models** (`common/blockmem.cpp`, `common/threads.cpp`, extensive global state in tool modules) increase leak/UAF/race risk and hinder verification; existing audit notes record concurrency/leak/TODO debt (e.g. FASTFIND race comment in `hlcsg/brush.cpp`, numerous TODO/HACK markers).
- **Stack/portability helpers**: `common/win32fix.h:14-16` maps `alloca` to `_alloca`; `common/cmdlib.h:625-631` retains legacy alloca macro paths, making platform-specific regressions easier.

## Build, CI, and repository hygiene

- **Build artifacts present in workspace**: `build/` contains caches, generated CMake files, objects, and binaries (`build/bin/*.exe`, `build/CMakeFiles/**`). `.gitignore:3` ignores `build/` and `git` does not track it, but its presence conflicts with stated gate/hygiene expectations and can confuse local verification.
- **CI coverage is minimal** (`.github/workflows/.github.yml`): Ubuntu/macOS only; configure+build only. No Windows job, no `docs`/`package` targets, no smoke tests, and no lint/typecheck-equivalent, leaving primary MinGW/Windows, packaging, and docs paths unguarded.
- **Dual build systems diverge**: `Makefile` and `CMakeLists.txt` differ in flags/layout/behavior (`Makefile` forces `-O2 -DNDEBUG`, uses custom release dirs, and advises ignoring warnings), creating drift risk and ambiguity around “all gates pass”.
- **Configuration/header assumptions**: several files include `config.h` under `HAVE_CONFIG_H` (`common/cmdlib.h:621`, `common/threads.cpp:8`, etc.) but no `config.h` is in-tree; correctness depends on build-provided defines. Legacy platform/version checks in `common/cmdlib.h` reflect historical multi-target assumptions.

## Documentation and process consistency

- **Doc path/reference drift**: `AGENTS.md` states the version must be updated in `docs/Doxyfile`, but the actual file is `docs/api/Doxyfile` (`docs/api/Doxyfile:51`); CMake invokes `docs/api/Doxyfile` (`CMakeLists.txt:196`). This can mislead release/gate work.
- **Documentation completeness/sync risk**: AGENTS requires `docs/user/manual.md` to stay in sync with tool help; the manual appears to document a subset and was not exhaustively cross-checked against all tool options in this pass. Doxygen sets `EXTRACT_ALL=YES` and `WARN_IF_UNDOCUMENTED=NO` (`docs/api/Doxyfile:551,930`), so missing API docs do not fail generation.
- **Versioning/debug labeling quirks**: `common/cmdlib.h:14-17` reports `ZHLT_VERSIONSTRING "v3.4 dbg"` in debug vs `"v3.5.1"` in release, while project/package metadata target 3.5.1 (`CMakeLists.txt:4,207`, `docs/api/Doxyfile:51`), which may confuse diagnostics.

## Relation to existing audits

- `audits/2026-11-10/consolidated.md` and `audits/2026-11-10/handling.md` already catalog broad security/memory/build/design issues (many marked fixed/mitigated/accepted). This report emphasizes issues still visible in current sources/config (unsafe macro behavior, unchecked parsing patterns, forced `NDEBUG`, CI/hygiene/doc-process gaps) rather than re-listing every historical finding.
- Smoke check performed during analysis: `build/bin/hlcsg.exe` and `build/bin/ripent.exe` printed expected version banners.
