# Consolidated Audit Report — VHLT (Vluzacn ZHLT 3.5.0)

## Title and Scope

**Title:** Consolidated Audit Report — VHLT (Vluzacn Half-Life Tools)

**Scope:** The VHLT toolset and shared common library in this repository, covering `common/`, `hlcsg/`, `hlbsp/`, `hlvis/`, `hlrad/`, `ripent/`, `template/`, plus build/CI/configuration files (`CMakeLists.txt`, `Makefile`, `.github/workflows/`). Six specialized audits (security/input handling, memory/lifetime, concurrency/thread safety, build/portability/hygiene, design/maintainability/documentation, code correctness/quality) were synthesized. No source code was changed by any audit or by this report.

**Audience:** Maintainers and contributors planning hardening, refactoring, and release work.

## Executive Summary

The codebase builds successfully (MinGW GCC 15.2.0, CMake/Ninja, ~60,545 LOC across 82 files) but carries substantial technical debt and latent defects. Overall risk is **Medium-High for untrusted input and long/complex compiles**, **Low-Medium for ordinary trusted-author workflows**. The most serious confirmed defects are:

1. **Heap corruption in `Winding::resize`** (`common/winding.cpp:1274-1284`) — copies `m_NumPoints` elements as if they were bytes; corrupts windings whenever they grow.
2. **Critical-section double-init/free** and **POSIX mutex lifecycle bugs** in the thread pool (`common/threads.cpp`) — crashes/leaks on repeated runs, especially NETVIS builds.
3. **Unbounded `.vis` parsing** (`hlvis/vis.cpp:872-899`) — `fscanf("%s")` into 64-byte fields plus unchecked counts/indices; directly exploitable by a malicious `.vis` file.
4. **`strcpy_s`/`sprintf_s` redefined as unbounded `strcpy`/`sprintf`** on MinGW/old MSVC (`common/cmdlib.h:607-616`) — silently disables the "safe" string macros that most of the codebase relies on.
5. **`safe_strncat` and `safe_snprintf` do not actually detect overflow** (`common/cmdlib.cpp`) — the safety helpers themselves are buggy.
6. **Pervasive mutable global state** (254 file-scope globals, 123 `extern` declarations) and **no automated tests**, making refactor and regression detection effectively impossible.
7. **1,119 compiler warnings** on a clean build, with the Makefile instructing developers to ignore them.

The common library is the highest-leverage fix target: every tool depends on `cmdlib`, `winding`, `bspfile`, and `threads`.

## Audit Coverage

Source reports synthesized (all in `audits/`):

| # | File | Focus | Findings |
|---|------|-------|----------|
| 1 | `01-security.md` | Security & unsafe input handling | 14 (F1–F14) |
| 2 | `02-memory.md` | Memory management & lifetime | 15 (F1–F15) |
| 3 | `03-concurrency.md` | Concurrency & thread safety | 24 (C1–C4, H1–H6, M1–M9, L1–L5) |
| 4 | `04-build-portability.md` | Build system, portability, hygiene | 25 (H1–H6, M1–M9, L1–L10) |
| 5 | `05-design-docs.md` | Design, maintainability, documentation | 11 (numbered 1–11) |
| 6 | `06-code-quality.md` | Code correctness & general quality | 17 (CQ-01–CQ-17) |

Total raw findings: ~106. Duplicates across audits (e.g. `Winding::resize`, `safe_strncat`, `strcpy_s` macros, texlight `sscanf`, hull-name `sprintf`, `brush_t` `memcpy`) are merged in the table below.

## Cross-Cutting Themes

1. **Safety helpers are not safe.** `safe_strncat`, `safe_snprintf`, and the `strcpy_s`/`sprintf_s` macros are buggy or no-ops on common toolchains. Fixing helpers first yields broad leverage.
2. **Bounds/count validation is missing at parser boundaries.** `.vis`, WAD, map, BSP, and entity parsers trust counts and copy fixed-size buffers without size checks (security + memory + code-quality overlap).
3. **Shared mutable global state + manual locking.** Globals (`g_*`) are read/written by workers with inconsistent locking; thread pool has lifecycle defects. Concurrency and design audits agree this is the structural root cause.
4. **Allocation discipline is inconsistent.** Mix of `new`/`malloc`/`GlobalAlloc`, many unchecked results, `realloc` overwriting original pointers, known leaks. Memory + design audits overlap.
5. **Build hygiene masks defects.** `NDEBUG` always defined, assertions compiled out, 704 `unknown-pragmas` warnings, `alloca` redefinition, no CI tests, Makefile says to ignore warnings. Build + design + code-quality audits overlap.
6. **No tests / no regression safety net.** 0 test files; CI only compiles. Any fix is high-risk without golden/smoke tests.
7. **Long functions and conditional-compilation sprawl** (8,109 `#ifdef` lines, 81 blocks of 100+ lines) make targeted fixes error-prone.
8. **Documentation/setup is stale** (VS2005 references, "ignore warnings" guidance, no license file).

## Prioritized Findings Table

Severity: **Critical** > **High** > **Medium** > **Low**. Location format `file:line`. Duplicates merged; original IDs noted in summaries where helpful.

| # | Sev | Category | Location | Summary |
|---|-----|----------|----------|---------|
| 1 | Critical | Memory/Quality | `common/winding.cpp:1274-1284` | `Winding::resize` passes element count as byte count to `memcpy`; heap corruption on growth (SEC-adjacent; MEM-F1/CQ-01). |
| 2 | Critical | Concurrency | `common/threads.cpp:356-457` (Win32) | Critical section initialized twice / double-deleted in NETVIS builds (C2). |
| 3 | Critical | Concurrency | `common/threads.cpp:597-650` (POSIX) | Mutex freed without `pthread_mutex_destroy`; attr never destroyed; repeated runs leak (C3). |
| 4 | Critical | Concurrency | `common/cmdlib.cpp:50-58` | `I_FloatTime()` static base time init is data race (C1). |
| 5 | Critical | Concurrency | `common/threads.cpp:399-426` | `CreateThread` failure path reads uninitialized `threadhandle[i]` (C4). |
| 6 | Critical | Security | `hlvis/vis.cpp:872-899` | `.vis` parsing: unbounded `fscanf("%s")` into 64-byte fields, unchecked counts/indices (SEC-F5/F12). |
| 7 | Critical | Design | `common/bspfile.h:329-398`, `hlrad/qrad.h` | Pervasive mutable global state; 254 globals, 123 externs; non-reentrant (Design-1). |
| 8 | Critical | Memory | `common/winding.cpp:319,383,388,425` | `Winding` constructors leave `m_MaxPoints` uninitialized (MEM-F2/CQ-05). |
| 9 | High | Security | `common/cmdlib.h:607-616` | `strcpy_s`/`sprintf_s` redefined as unbounded `strcpy`/`sprintf` on MinGW/old MSVC (SEC-F1/MEM-F5). |
| 10 | High | Security/Quality | `common/cmdlib.cpp:555-569` | `safe_strncat` overflows destination (SEC-F2/CQ-04). |
| 11 | High | Security/Quality | `common/cmdlib.cpp:529-548` | `safe_snprintf` misses truncation (`>=` vs `==`, -1) (CQ-03). |
| 12 | High | Security | `common/bspfile.cpp:1812-1822` | Entity serialization `sprintf` into 4096-byte `line` can overflow (SEC-F4). |
| 13 | High | Security | `hlvis/flow.cpp:532` | `alloca` size from portal point counts; stack overflow/overflow (SEC-F6; Build-M7). |
| 14 | High | Security | `hlrad/loadtextures.cpp:234,255,301,307`; `ripent/ripent.cpp:567,618,740`; `hlcsg/autowad.cpp:44,70` | WAD texture names copied with `strcpy`/`strcpy_s` without guaranteed termination (SEC-F7). |
| 15 | High | Security | `hlvis/vis.cpp:876-899` | `InitVisBlock` counts/loops unvalidated (SEC-F12, part of F5). |
| 16 | High | Quality | `common/bspfile.cpp:278-286` | Texinfo endian conversion indexes `vecs[0][4..7]`, OOB write (CQ-02). |
| 17 | High | Concurrency | `common/threads.cpp:320-346` (Win32), `597-614` (POSIX) | `ThreadLock` reads `threaded`/`my_mutex` unsynchronized (H1). |
| 18 | High | Concurrency | `common/threads.cpp:252-253,348,350,216` | Plain globals `g_numthreads`, `dispatch`, `workcount`, `q_entry`, `workfunction` (H2). |
| 19 | High | Concurrency | `hlcsg/brush.cpp:176-268` (FASTFIND), `:40-53` (default) | `FindIntPlane()` plane table races; explicit "BUG" comment (H3). |
| 20 | High | Concurrency | `hlcsg/textures.cpp:58-85,104-136` | `texmap_*` counters/arrays inconsistent locking (H4). |
| 21 | High | Concurrency | `hlvis/flow.cpp:1741-1755` | `MaxDistVis()` mutates portal visbits without lock in non-NEW path (H5). |
| 22 | High | Concurrency | `common/threads.cpp:37-43` | Shared `dispatch`, `workcount`, `threaded`, `threadtimes[]` (H6). |
| 23 | High | Build | `CMakeLists.txt:38-49` | Hardcoded platform/header availability (H1). |
| 24 | High | Build | `CMakeLists.txt:32,41,95-105` | `NDEBUG` and `-O2` forced for all configs (H2). |
| 25 | High | Build | `common/win32fix.h:14` | MinGW `alloca` redefinition (94 warnings) (H3). |
| 26 | High | Build | `common/boundingbox.h:6`, `common/winding.cpp:1`, `hlbsp/outside.cpp:1`, `hlrad/transparency.cpp:1`, `hlcsg/wadinclude.cpp:1`, `common/cmdlib.h:~689` | Unguarded MSVC pragmas (704 warnings) (H4). |
| 27 | High | Build | `.github/workflows/.github.yml:10-12` | No Windows CI; obsolete checkout@v2 (H5). |
| 28 | High | Build | `Makefile` (throughout) | Unix-only Makefile (H6). |
| 29 | High | Design | `hlrad/lightmap.cpp:297` (`PairEdges`, 8,884 lines); `hlvis/vis.cpp:345` (`GetNextPortal`, 1,739); `hlcsg/qcsg.cpp:1465` (`LoadWadValue`, 1,614); `hlrad/loadtextures.cpp:349` (`LoadTextures`, 1,160); `hlrad/qrad.cpp:4848` (`main`, 1,071) | Extremely long functions (Design-2). |
| 30 | High | Design | `common/cmdlib.h` (272 `#define`, 260+ toggles) | Excessive conditional compilation; 8,109 `#ifdef` lines (Design-3). |
| 31 | High | Design | repo-wide | No automated tests; CI only compiles (Design-4). |
| 32 | High | Design | `common/blockmem.cpp`; `common/hlassert.h`; `common/filelib.cpp:70-90` | Weak error handling; assertions compiled out in release (Design-5). |
| 33 | Medium | Security | `common/cmdlib.cpp:195-255` | `ExtractFilePath`/`ExtractFile`/etc. lack dest-size param (SEC-F3). |
| 34 | Medium | Security | `hlcsg/wadinclude.cpp:138-140`; `common/cmdlinecfg.cpp:271-272` | Overlapping `snprintf` src/dest; `strcat` into `_MAX_PATH` (SEC-F8). |
| 35 | Medium | Security | `hlbsp/qbsp.cpp:1762,1768,1861,1866` | Unbounded `sprintf` for map-derived filenames (SEC-F9/CQ-10). |
| 36 | Medium | Security | `hlcsg/qcsg.cpp:2646-2648`; `hlrad/qrad.cpp:5790`; `hlvis/vis.cpp:860,1007`; `hlbsp/qbsp.cpp` | Unsafe `#else` branches behind macros (SEC-F10). |
| 37 | Medium | Security | `hlcsg/qcsg.cpp:2646`; `hlcsg/wadpath.cpp:112-174`; `hlcsg/wadcfg.cpp` | Path traversal via map/WAD paths (SEC-F11). |
| 38 | Medium | Security | `hlvis/flow.cpp:532`; `hlcsg/textures.cpp:470-471` | Unchecked integer multiplication in alloc sizes; post-increment bounds check (SEC-F13). |
| 39 | Medium | Security/Quality | `hlrad/qrad.cpp:575` | Unbounded `sscanf` into `_MAX_PATH` buffer (SEC-F14/CQ-11). |
| 40 | Medium | Memory | `common/winding.cpp:282-293` | `Winding::operator=` not self-assignment safe (MEM-F3/CQ-06). |
| 41 | Medium | Memory | `common/blockmem.cpp:17-75` | Two distinct allocator families easily confused; `FreeBlock(NULL)` (MEM-F4). |
| 42 | Medium | Memory | `common/cmdlinecfg.cpp:87,213-223,272,292-327`; `common/log.cpp:925,931`; `hlrad/loadtextures.cpp:234,255,301,307`; `hlrad/qrad.cpp:5790`; `ripent/ripent.cpp:567,618,740` | Fixed buffers with unbounded string ops (MEM-F6). |
| 43 | Medium | Memory | `hlrad/lightmap.cpp:183,185,1891-1892,4447,5771,5811`; `hlrad/qrad.cpp:738,2364` | Uninitialized temporaries/accumulators (MEM-F7/F8). |
| 44 | Medium | Memory | Many files (see 02-memory F10/F11) | Allocations not null-checked; `realloc` overwrites original pointer (MEM-F10/F11). |
| 45 | Medium | Quality | `hlbsp/brink.cpp:140-167,191-215` | Search results used after assertion-only guards; uninitialized `found` (CQ-07). |
| 46 | Medium | Quality | `common/bspfile.cpp:876-920` | `DoAllocBlock` may use uninitialized `x`/`y` (CQ-08). |
| 47 | Medium | Quality | `common/filelib.cpp:70-98` | `getfiledata` can compute negative/large read size (CQ-09). |
| 48 | Medium | Concurrency | `common/threads.cpp:305-318` (Win32) | Thread cap 32 vs `MAX_THREADS` 64 (M1). |
| 49 | Medium | Concurrency | `common/threads.cpp:688-695` (POSIX) | Fixed 4 MiB thread stack; overflow risk (M2). |
| 50 | Medium | Concurrency | `common/threads.cpp:396-457` (Win32) | Thread handles never `CloseHandle`d (M3). |
| 51 | Medium | Concurrency | `common/threads.cpp:320-346` (Win32) | `ThreadLock` recursion accounting inconsistent (M4). |
| 52 | Medium | Concurrency | `common/log.cpp:477-483` | `PrintOnce()` non-atomic counter (M5). |
| 53 | Medium | Concurrency | `common/log.cpp` (all logging) | Unsynchronized shared logging I/O (M6). |
| 54 | Medium | Concurrency | `hlvis/vis.cpp:85,677` | Static `totalvis` (M7). |
| 55 | Medium | Concurrency | `hlrad/vismatrixutil.cpp` and hlrad modules | Inconsistent locking of shared patch/transfer globals (M8). |
| 56 | Medium | Concurrency | `common/threads.cpp:628-644` (POSIX) | Unsynchronized `if (!my_mutex)` init (M9). |
| 57 | Medium | Build | `template/basictypes.h:46-52` | `INT64`/`UINT64` undefined on POSIX (M1). |
| 58 | Medium | Build | `CMakeLists.txt:22-26,38-39` | Inconsistent platform version selection (M2). |
| 59 | Medium | Build | `CMakeLists.txt:34` | Unused `WIN32` definition (M3). |
| 60 | Medium | Build | `common/log.cpp:43`; `hlrad/qrad.cpp:4855`; `ripent/ripent.cpp:941,944` (~28 sites) | String literals assigned to `char*` (M4). |
| 61 | Medium | Build/Quality | `hlcsg/map.cpp:37,41,95,728,1348,1359,1363` | `memcpy` on non-trivially-copyable `brush_t` (Build-M5/CQ-13). |
| 62 | Medium | Build/Quality | project-wide (125 sign-compare, 45 maybe-uninitialized) | Warning debt hides defects (Build-M6/CQ-17). |
| 63 | Medium | Build | `hlvis/flow.cpp` | `alloca` for temp storage (Build-M7). |
| 64 | Medium | Build | `common/cmdlib.h` (~60-560) | Feature-flag combinatorial matrix; dead paths (Build-M8). |
| 65 | Medium | Build | `common/cmdlib.h:621-623` | `HAVE_CONFIG_H` path never generated (Build-M9). |
| 66 | Medium | Design | `common/bspfile.h` (limits); `common/threads.h:12` | Magic numbers/undocumented limits (Design-6). |
| 67 | Medium | Design | project-wide | Inconsistent naming/style (Design-7). |
| 68 | Medium | Design | `hlcsg/brush.cpp:322,1024,1097,1363`; `hlbsp/bsp5.h`; etc. | Known leaks/TODOs (146 markers) (Design-8). |
| 69 | Medium | Design | `Terms of Use.txt`; `Makefile`; `common/cmdlib.h` comments | Misleading/outdated docs (Design-9). |
| 70 | Medium | Design | `docs/Doxyfile`; `common/*.h` | Sparse API documentation (Design-10). |
| 71 | Low | Memory | `hlbsp/outside.cpp:298-301`; `hlcsg/brush.cpp:322,1024,1097,1363` | Known small leaks (MEM-F12). |
| 72 | Low | Memory | `template/ReferencePtr.h:118-133`; `template/ReferenceArray.h`; `template/ReferenceCounter.h:96-111,126-130` | Reference-counting template lifetime bugs (MEM-F13). |
| 73 | Low | Memory | `hlvis/zones.h:45-49` | Unchecked `new[]` in `Zones` (MEM-F14). |
| 74 | Low | Memory | `common/bspfile.cpp:30-95`; `hlvis/vis.cpp:1138-1145`; `hlrad/qrad.cpp` globals | Global buffers rely on conditional `hlassume` (MEM-F15). |
| 75 | Low | Memory | `ripent/ripent.cpp:801-802` | Uninitialized backup vars (MEM-F9). |
| 76 | Low | Security | `hlcsg/textures.cpp:470-471` | Post-increment before bounds check (SEC-F13 subset). |
| 77 | Low | Quality | `common/mathlib.h:68` | Malformed `VecAddVector` macro (unused) (CQ-12). |
| 78 | Low | Quality | `common/cmdlib.cpp:132` | Pointer relational comparison with null (CQ-14). |
| 79 | Low | Quality | `common/filelib.cpp:107-118` | `q_filelength` uses `int` (CQ-15). |
| 80 | Low | Quality | `common/cmdlib.cpp:153-167` | `FlipSlashes` infinite-loop risk if chars equal (CQ-16). |
| 81 | Low | Concurrency | `hlbsp/solidbsp.cpp:33-51` | Unprotected progress counters (L1). |
| 82 | Low | Concurrency | `hlbsp/brink.cpp:732-733` | Non-thread-safe static `randcounter` (L2). |
| 83 | Low | Concurrency | `common/threads.cpp:743-813` (SINGLE_THREADED) | Single-threaded build semantics differ (L3). |
| 84 | Low | Concurrency | `common/threads.cpp:309-317` (Win32) | `g_numthreads == -1` runs zero work (L4). |
| 85 | Low | Concurrency | `hlvis/vis.cpp` NETVIS client loops | Busy-wait on globals (L5). |
| 86 | Low | Build | `common/TimeCounter.h`; `template/BaseMath.h`, `EndianMath.h`, `Reference*.h` | Unused/dead headers (L1). |
| 87 | Low | Build | widespread | `ZHLT_NETVIS` never built (L2). |
| 88 | Low | Build | `common/boundingbox.h:6`; `hlcsg/csg.h:8` | Unguarded pragmas (L3). |
| 89 | Low | Build | `common/cmdlib.h:625-631` (`#if 0`); `common/win32fix.h:14` | `alloca` macro rarely used (L4). |
| 90 | Low | Build | `common/cmdlib.h:74-82` | `VERSION_OTHER` never produced (L5). |
| 91 | Low | Build | `common/cmdlib.h:16-20` | `ZHLT_VERSIONSTRING` debug/release mismatch (L6). |
| 92 | Low | Build | `Makefile` object rules | `printusage` as prerequisite (L7). |
| 93 | Low | Build | `Makefile` `COMMON_FLAGS` | Ignores `CXXFLAGS`/`LDFLAGS` (L8). |
| 94 | Low | Build | `CMakeLists.txt:140-148` | Doxygen deprecated variable form (L9). |
| 95 | Low | Build | `CMakeLists.txt` (top-level) | No `CMAKE_EXPORT_COMPILE_COMMANDS` (L10). |
| 96 | Low | Design | CI/CMake | No static analysis/sanitizers/formatting (Design-11). |

## Top Critical Issues with Details

### 1. `Winding::resize` heap corruption — `common/winding.cpp:1274-1284`
- **Severity:** Critical
- **Category:** Memory / Code Quality
- **Detail:** `memcpy(newpoints, m_Points, m_NumPoints)` passes an element count where bytes are required, and `m_NumPoints` is reduced before the copy. Only a fraction of the array is copied; the rest is uninitialized. Reachable whenever a winding grows past initial capacity; affects geometry throughout all tools.
- **Recommendation:** Save `oldNumPoints`, reduce after copy, copy `oldNumPoints * sizeof(vec3_t)` bytes.

### 2. Thread-pool critical section lifecycle — `common/threads.cpp`
- **Severity:** Critical
- **Category:** Concurrency
- **Detail:** Win32: `threads_InitCrit()` initializes an already-initialized critical section in NETVIS builds (double init/delete). POSIX: mutex freed without `pthread_mutex_destroy`, attribute leaked, `if (!my_mutex)` init race. Repeated runs corrupt/leak.
- **Recommendation:** Make init idempotent/reference-counted; destroy mutex/attr properly; reuse one long-lived mutex.

### 3. `I_FloatTime()` data race — `common/cmdlib.cpp:50-58`
- **Severity:** Critical
- **Category:** Concurrency
- **Detail:** Static `secbase` initialized with unsynchronized check-then-set; called from worker paths.
- **Recommendation:** Initialize base time once before threading or use a thread-safe clock.

### 4. CreateThread failure reads uninitialized memory — `common/threads.cpp:399-426`
- **Severity:** Critical
- **Category:** Concurrency
- **Detail:** On failure, diagnostic reads `threadhandle[i]` before assignment.
- **Recommendation:** Log `i`/`hThread` only; fail fast.

### 5. Unbounded `.vis` parsing — `hlvis/vis.cpp:872-899`
- **Severity:** Critical
- **Category:** Security
- **Detail:** `fscanf(fp, "%s\n", ...)` into 64-byte fields with no width; counts (`num_blocks`, `num_sides`) and index `x` unvalidated against `MAX_VISBLOCKERS`/`MAX_PORTALS_ON_LEAF`. Malicious `.vis` can corrupt heap/stack.
- **Recommendation:** Use `%63s`, validate counts before loops, bounds-check `x`.

### 6. Safety macros disabled on common toolchains — `common/cmdlib.h:607-616`
- **Severity:** High (listed here due to cross-cutting impact)
- **Category:** Security / Memory
- **Detail:** On MinGW/old MSVC, `strcpy_s`/`sprintf_s` are `#define`d to plain `strcpy`/`sprintf`, and deprecation warnings disabled. "Safe" call sites lose bounds checking on the primary Windows toolchain.
- **Recommendation:** Remove macros; provide real bounded impls or fix call sites; stop globally disabling deprecation warnings.

### 7. `safe_strncat` / `safe_snprintf` defects — `common/cmdlib.cpp`
- **Severity:** High
- **Category:** Security / Quality
- **Detail:** `safe_strncat` calls `strncat(dest, src, count)` (count = total capacity) then truncates; overflow occurs before truncation. `safe_snprintf` treats truncation as `amt == count` instead of `amt >= count` (and misses `-1` on old MSVC).
- **Recommendation:** Fix remaining-capacity math and truncation test (`amt < 0 || (size_t)amt >= count`).

### 8. Pervasive mutable global state + no tests
- **Severity:** Critical/High
- **Category:** Design
- **Detail:** 254 file-scope globals, 123 externs, `hlrad/qrad.h` alone 191 externs; 0 test files; CI only compiles. Makes all other fixes high-risk.
- **Recommendation:** Introduce context structs; add smoke/golden tests and a CI test job before large refactors.

## Consolidated Recommendations

### Immediate (correctness & safety, low risk)
1. Fix `Winding::resize` byte count and `m_MaxPoints` initialization (Memory/Quality #1, #8).
2. Fix `safe_strncat` and `safe_snprintf` truncation logic (#10, #11).
3. Remove/replace `strcpy_s`/`sprintf_s` macros; stop disabling deprecation warnings (#9).
4. Fix texinfo endian conversion OOB write (#16).
5. Fix `.vis` parser: width-limited `%s`, validated counts, bounds-checked index (#6, #15).
6. Fix thread-pool critical-section/mutex lifecycle and `I_FloatTime` race (#2, #3, #4, #5).
7. Fix unbounded `sprintf`/`sscanf` in `hlbsp`/`hlrad` (hull names, texlight) (#35, #39).
8. Guard `brush_t` `memcpy` UB and uninitialized "best candidate" variables (#45, #46, #61).

### Short-term (robustness & maintainability)
9. Add destination-size parameters to `Extract*` helpers; replace unbounded string ops in fixed buffers.
10. Validate/canonicalize map/WAD paths to prevent traversal; remove unsafe `#else` branches.
11. Replace `alloca` with heap allocation; check all `malloc`/`calloc`/`realloc` results; fix `realloc` pointer handling.
12. Initialize all accumulators/best-candidate variables; make assertion guards real runtime checks where results are used.
13. Fix build hygiene: stop forcing `NDEBUG`/`-O2` in all configs; guard MSVC pragmas; fix MinGW `alloca` redefinition; add Windows CI.
14. Add smoke tests invoking each tool on small maps + golden-file comparisons; add CI test job.
15. Fix concurrency: atomicize shared counters/flags (`threaded`, `g_numthreads`, `PrintOnce`, progress counters), close thread handles, raise stack cap or make configurable.

### Long-term (architecture & quality)
16. Reduce global state: introduce context/parameter structs for BSP data and tool state; document ownership/init order.
17. Split the largest functions (`PairEdges`, `GetNextPortal`, `LoadWadValue`, `LoadTextures`, `main`).
18. Remove obsolete feature toggles or convert to documented CMake options; delete dead netvis/xash paths or make buildable.
19. Adopt and document naming/style conventions; fix remaining warning debt; enable `-Werror` for `common/`.
20. Update README/Terms of Use for CMake/current compilers; add license file; improve Doxygen comments or reduce scope.
21. Add sanitizers in debug builds, static analysis, and formatting checks to CI.

## Appendix: Per-Audit Summary

### 01-security.md — Security and Unsafe Input Handling
- Risk: Medium-High (untrusted input), Low (trusted workflows).
- Key: safe-string macros redefined as unsafe (F1); `safe_strncat` overflow (F2); Extract* no size param (F3); entity serialization overflow (F4); `.vis` unbounded parsing (F5, Critical); `alloca` stack (F6); WAD name copies (F7); path/filename issues (F8–F11, F13–F14); unsafe `#else` branches (F10); integer mult (F13).
- Recs: remove macros; fix helpers; bounded parsers; validate counts; replace `alloca`; canonicalize paths; enable hardening + fuzzing.

### 02-memory.md — Memory Management and Lifetime
- Risk: Medium-High (complex compiles), Low (small maps).
- Key: `Winding::resize` wrong byte count (F1, Critical); uninitialized `m_MaxPoints` (F2); `operator=` self-assign (F3); allocator family confusion (F4); macro redefinition (F5); fixed-buffer unbounded ops (F6); uninitialized accumulators (F7/F8); unchecked allocs/realloc (F10/F11); known leaks (F12); template lifetime bugs (F13); unchecked `new[]` (F14); global buffers (F15).
- Notes: static audit only; `NDEBUG` disables `assert`; `hlassume` is effective check.

### 03-concurrency.md — Concurrency and Thread Safety
- Risk: High latent; issues in both default and conditional paths.
- Key: Critical: `I_FloatTime` race (C1), critical-section double init (C2), POSIX mutex lifecycle (C3), uninitialized threadhandle (C4). High: unsynchronized shared flags (H1/H2/H6), plane table races (H3), texmap races (H4), unlocked portal visbit mutation (H5). Medium: thread cap, stack size, handle leak, recursion accounting, logging races, static counters. Low: progress counters, randcounter, single-threaded semantics, NETVIS busy-wait.

### 04-build-portability.md — Build System, Portability, Hygiene
- Baseline: 1,119 warnings, 0 errors (MinGW GCC 15.2.0, CMake/Ninja).
- Key High: hardcoded platform headers (H1); forced `NDEBUG`/`-O2` (H2); MinGW `alloca` redef (H3); unguarded pragmas (H4); no Windows CI (H5); Unix-only Makefile (H6).
- Medium: undefined POSIX 64-bit types (M1); version selection (M2); unused `WIN32` (M3); string-literal-to-char* (M4); `memcpy` on non-trivial `brush_t` (M5); warning debt (M6); `alloca` (M7); feature-flag matrix (M8); unused config header (M9).
- Low: dead headers/files, netvis never built, pragma guards, alloca macro, `VERSION_OTHER`, version string, Makefile `printusage`/flags, Doxygen form, no compile commands.

### 05-design-docs.md — Design, Maintainability, Documentation
- Scale: 82 files, ~60,545 LOC; 123 externs (191 in `hlrad/qrad.h`); 254 globals; 81 blocks of 100+ lines (largest `PairEdges` 8,884 lines); 8,109 `#ifdef` lines; 729 `#define`; 0 tests; 0 Doxygen annotations.
- Findings: global state (Critical); long functions, excessive conditionals, no tests (High); error handling, magic numbers, naming, leaks/TODOs, stale docs, sparse API docs (Medium); CI quality (Low).
- Recs: smoke/golden tests first; document/reduce globals; split large functions; remove toggles; fix alloc checks; update docs; improve headers.

### 06-code-quality.md — Code Correctness and General Quality
- Build: 1,119 warnings, 0 errors.
- Key: `Winding::resize` byte count (CQ-01, Critical); texinfo endian OOB (CQ-02, High); `safe_snprintf` truncation (CQ-03, High); `safe_strncat` (CQ-04, High); uninitialized `m_MaxPoints` (CQ-05, High); `operator=` (CQ-06); brink search (CQ-07); `DoAllocBlock` (CQ-08); `getfiledata` (CQ-09); unbounded `sprintf`/`sscanf` (CQ-10/11); malformed macro (CQ-12); `brush_t` memcpy (CQ-13); pointer comparison (CQ-14); `q_filelength` int (CQ-15); `FlipSlashes` (CQ-16); warning debt (CQ-17).
- Priorities: fix resize + texinfo; fix safe helpers; init temporaries; replace unbounded calls; make guards real; fix macro/copies; reduce warnings.