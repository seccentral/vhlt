# Design, Maintainability, and Documentation Audit

**Scope:** `<repo>` (Vluzacn ZHLT / VHLT 3.5.0)  
**Angle:** Software design, maintainability, documentation  
**Method:** Static inspection of source, build files, and repository metadata. No code was changed.  
**Date:** 2026-11-10

## Summary

The codebase is a legacy C/C++ BSP compiler suite with five command-line tools sharing a large common library. It builds successfully with the provided CMake/Make setup, but maintainability is poor: extensive global state, very long functions, thousands of preprocessor branches, almost no automated tests, and sparse documentation. The most serious risks are the inability to reason about or safely refactor shared mutable global state and the absence of any regression tests.

Key metrics:
- 82 source/header files, ~60,545 LOC.
- 123 unique `extern` declarations in headers; `hlrad/qrad.h` alone has 191 `extern` declarations.
- 254 file-scope global variable definitions in `.cpp` files.
- 81 brace-delimited blocks of 100+ lines, including `PairEdges` at 8,884 lines.
- 8,109 preprocessor conditional lines and 729 `#define` directives.
- 0 test files, 0 test presets, CI only compiles.
- 0 Doxygen `@param`/`@return`/`@brief` annotations; only incidental `/**` markers.

## Findings

### 1. Pervasive mutable global state
**Severity:** Critical  
**Evidence:**
- `common/bspfile.h:329-398` declares dozens of global arrays and counters (`g_nummodels`, `g_dmodels`, `g_numleafs`, `g_dleafs`, `g_numplanes`, `g_dplanes`, etc.).
- `common/log.h` exposes `g_Program`, `g_Mapname`, `g_developer`, `g_verbose`, `g_log`, `g_clientid`.
- `hlrad/qrad.h` has 191 `extern` declarations, including `g_face_patches`, `g_patches`, `g_textures`.
- `common/threads.h` exposes `g_numthreads`, `g_threadpriority`.

**Impact:** Functions depend on implicit global state, making them non-reentrant, hard to test, and unsafe for concurrent use. Refactoring or adding features risks subtle cross-tool contamination because all tools compile the same common sources.

**Recommendation:** Introduce context/parameter structs for BSP data and tool state; pass them explicitly. At minimum, document ownership and initialization order of globals.

### 2. Extremely long functions
**Severity:** High  
**Evidence:**
- `hlrad/lightmap.cpp:297` `PairEdges` spans 8,884 lines.
- `hlvis/vis.cpp:345` `GetNextPortal` spans 1,739 lines.
- `hlcsg/qcsg.cpp:1465` `LoadWadValue` spans 1,614 lines.
- `hlrad/loadtextures.cpp:349` `LoadTextures` spans 1,160 lines.
- `hlrad/qrad.cpp:4848` `main` spans 1,071 lines.
- `hlcsg/map.cpp:841` `ParseMapEntity` spans 677 lines.
- `hlvis/flow.cpp:1160` `BasePortalVis` spans 613 lines.
- `common/bspfile.cpp:1499` `ParseEntity` spans 527 lines.
- `hlbsp/brink.cpp:678` `SplitTreeLeaf` spans 471 lines.
- `hlbsp/qbsp.cpp:1886` `main` spans 418 lines.

**Impact:** Single functions mix parsing, allocation, computation, logging, and control flow. They are difficult to review, test, or modify safely.

**Recommendation:** Split into focused helper functions with explicit inputs/outputs. Keep `main` limited to argument parsing and orchestration.

### 3. Excessive conditional compilation
**Severity:** High  
**Evidence:**
- 8,109 conditional preprocessor lines across `.cpp`/`.h`.
- `common/cmdlib.h` contains 272 `#define` directives and roughly 260+ feature toggles (`ZHLT_*`, `HLCSG_*`, `HLBSP_*`, `HLRAD_*`), many nested.
- Examples: `common/cmdlib.h:1-260` feature matrix; toggles such as `ZHLT_DETAILBRUSH`, `HLBSP_BRINKHACK`, `HLCSG_WATERBACKFACE_FIX`.
- `common/bspfile.h:366-372` conditionally changes `g_texinfo` array size based on `HLCSG_HLBSP_REDUCETEXTURE`.

**Impact:** Behavior depends on undocumented compile-time flags. Many flags are commented "should never turn on," yet remain in source. Testing all configurations is impractical; dead code paths accumulate.

**Recommendation:** Remove obsolete toggles or move them to documented build options. Replace `#ifdef` feature switches with runtime/configuration where feasible.

### 4. No automated tests
**Severity:** High  
**Evidence:**
- No `*test*` or `*spec*` files in repository.
- `CMakePresets.json` has `"testPresets": []`.
- `.github/workflows/.github.yml` only configures and builds; no test step.
- `Terms of Use.txt` states warnings should be ignored, indicating expected noisy builds.

**Impact:** Regressions in geometry, visibility, and lighting cannot be detected automatically. Refactoring is high-risk.

**Recommendation:** Add at least smoke tests invoking each tool on small sample maps and golden-file comparisons for deterministic outputs. Add a CI test job.

### 5. Missing or weak error handling
**Severity:** High  
**Evidence:**
- `common/blockmem.cpp` (POSIX and Win32): `Alloc` returns `calloc(1, size)` without checking for `NULL`; `AllocBlock` likewise returns `calloc` without null check (except optional `hlassume` under `HLRAD_HLASSUMENOMEMORY`).
- `common/hlassert.h`: in release builds `hlassert(exp)` expands to nothing, so assertions are compiled out.
- `common/filelib.cpp:70-90` `getfiledata` silently returns partial size and only warns if `buffersize != size`.
- `common/filelib.cpp:171-184` `SafeRead`/`SafeWrite` call `Error` on mismatch but do not validate pointers or counts.

**Impact:** Memory allocation failures and malformed input may cause null dereferences or truncated processing instead of clean failure.

**Recommendation:** Check all allocation results and fail with `Error`/`Fatal`. Keep lightweight assertions in release or validate invariants explicitly.

### 6. Magic numbers and undocumented limits
**Severity:** Medium  
**Evidence:**
- `common/bspfile.h:15-120`: many limits such as `MAX_MAP_MODELS 512`, `MAX_MAP_PLANES 32768`, `MAX_MAP_LEAFS 8192`, `MAX_MAP_VISIBILITY 0x800000`, `DEFAULT_MAX_MAP_MIPTEX 0x2000000`, `MAX_TEXTURE_SIZE 348972`.
- `common/threads.h:12`: `MAX_THREADS 64`.
- `common/filelib.cpp:83`: read chunk size `32 * 1024`.
- `common/bspfile.h:89`: `MAX_TEXTURE_SIZE 348972` with comment "arbitrary."

**Impact:** Tuning or porting requires editing scattered constants; some values are engine-specific and not clearly justified.

**Recommendation:** Group limits into a documented configuration section with rationale and engine compatibility notes. Replace inline literals with named constants.

### 7. Inconsistent naming and style
**Severity:** Medium  
**Evidence:**
- Mixed casing: `ParseMapEntity`, `safe_snprintf`, `VectorCopy`, `GetThreadWork`.
- Globals use `g_` prefix inconsistently; some file-scope globals omit it (e.g., `common/vis.cpp`-style `totalvis`, `num_visleafs` in `hlbsp/portals.cpp`).
- Macros are uppercase (`MAX_THREADS`), functions mixed, variables camelCase/snake_case.
- `common/Makefile` notes warnings such as uninitialized variables, assignment-as-truth, signed/unsigned comparisons.

**Impact:** Reading and grepping code is harder; automated refactoring is riskier.

**Recommendation:** Adopt and document a naming convention (e.g., `g_` for globals, PascalCase for functions, snake_case for locals) and apply incrementally.

### 8. Known memory leaks and TODOs
**Severity:** Medium  
**Evidence:**
- `hlcsg/brush.cpp:322,1024,1097,1363`: `// TODO: This leaks` on `Alloc` of `bface_t`.
- `hlbsp/bsp5.h`: `pts[MAXEDGES] // FIXME: change to use winding_t`.
- `hlcsg/csg.h`: `noclip // !!!FIXME: should be a flag bitfield`.
- `hlbsp/brink.cpp`: `// TODO: we should consider corners...`.
- `common/winding.h`: `// TODO: FIX THIS STUPID SHIT (MAX_POINTS_ON_WINDING)`.
- 146 TODO/FIXME/XXX/HACK/BUG markers total.

**Impact:** Known defects are documented in code but not tracked or resolved; leaks accumulate in long-running or large-map compiles.

**Recommendation:** Convert TODOs into tracked issues with owners. Fix or remove leak-prone allocations; consider an arena allocator.

### 9. Misleading/outdated comments and documentation
**Severity:** Medium  
**Evidence:**
- `Terms of Use.txt`: references Visual Studio 2005, GPL/Valve/iD licenses, and email contacts; no license file exists in repository.
- `Makefile` tells developers to ignore compiler warnings, including uninitialized variables and suspicious conversions.
- `common/cmdlib.h` comments such as `// Very Important !!` and `//should never turn on` provide little rationale.
- `common/bspfile.h:23` `MAX_MAP_PLANES` TODO says it "can be larger" without specifying safe bounds.

**Impact:** New contributors receive inaccurate setup guidance and may ignore real warnings.

**Recommendation:** Rewrite `Terms of Use.txt`/README setup section for CMake and current compilers. Remove "ignore warnings" guidance; fix warnings instead.

### 10. Sparse API documentation
**Severity:** Medium  
**Evidence:**
- Doxygen is configured (`docs/Doxyfile`, `INPUT` includes all source dirs), but only 4 incidental `/**` markers exist; 0 `@param`/`@return`/`@brief`.
- `EXTRACT_ALL = YES` masks missing docs by extracting everything.
- README documents build commands but not tool CLI options, input/output formats, or examples.

**Impact:** Generated API docs are essentially an index with no semantic descriptions. Users must read source or legacy docs.

**Recommendation:** Add concise module/function comments for public headers (`common/*.h`), or reduce Doxygen scope to avoid implying documentation exists. Add a usage section to README.

### 11. Build/CI does not validate quality
**Severity:** Low  
**Evidence:**
- `CMakeLists.txt` enables `-Wall` but also `-Wno-deprecated-declarations`; Makefile lists warnings to ignore.
- No static analysis, sanitizers, or formatting checks in CI.
- `CMakePresets.json` has no test presets.

**Impact:** Regressions and undefined behavior may ship unnoticed.

**Recommendation:** Add optional sanitizers in debug builds and a lint/format check; keep warning suppressions narrowly scoped.

## Recommended Priorities

1. Add smoke/golden tests and a CI test job.  
2. Document and reduce global state.  
3. Split the largest functions (`PairEdges`, `GetNextPortal`, `LoadWadValue`, `LoadTextures`, `main`).  
4. Remove obsolete feature toggles and document remaining ones.  
5. Fix allocation checks and avoid compiling out all assertions.  
6. Update README/Terms of Use and license files.  
7. Improve header documentation and Doxygen usefulness.
PY