# Final Consolidated Audit — VHLT 3.5.1 (2026-11-10)

**Audience:** maintainers and contributors planning the next hardening release.

**Scope:** all sources (`common/`, `template/`, `hlcsg/`, `hlbsp/`, `hlvis/`, `hlrad/`, `ripent/`), build (CMake, Makefile, CPack), CI, docs, and repo hygiene at HEAD (`9a7d371` plus the onboarding commits).

**Sources merged:**

| Key | Report | Notes |
|---|---|---|
| **C** | [`audits/claude-opus-5-5-1157-2026-11-10.md`](claude-opus-5-5-1157-2026-11-10.md) | Six-way subsystem review. It checks the prior "Fixed" claims and covers a scratch MinGW 64-bit build and the smoke gate. |
| **G** | [`audits/glyph-cluster-2026-11-10.md`](glyph-cluster-2026-11-10.md) | Static review, with emphasis on unsafe macros, parsing, `NDEBUG`, CI and docs. |
| Prior | [`audits/2026-11-10/consolidated.md`](2026-11-10/consolidated.md), [`handling.md`](2026-11-10/handling.md) | The earlier audit and its fix-status record. This report corrects several of its "Fixed" claims (§3). |

Every **G**-only item was spot-checked against the source before merging. Severities were reconciled to the higher of the two reports unless the code showed otherwise; downgrades are noted.

---

## 1. Executive summary

- **Status:** the tools build cleanly on MinGW 64-bit (373 warnings, none of them format or bounds class) and all five print a v3.5.1 banner.
- **Risk:**
  - **High** for untrusted or corrupt inputs (BSP, WAD, `.prt`, `.inc`, `.rad`, map files).
  - **High** for `-threads` above 64 and for hosts with more than 64 CPUs.
  - **Medium** for ordinary trusted-author workflows.
- **Main problems:**
  1. **The thread pool overflows its stack arrays** whenever the thread count exceeds 64. On Linux under a cpuset it can also run **zero** threads, silently skipping all work.
  2. **File-format readers trust their headers.** `CopyLump`, the WAD lump tables and `disksize`, miptex dimensions, the `.prt` leaf/portal counts and the `.inc` transfer sizes are not validated. Each gives a heap or global overflow from a crafted file.
  3. **The "safe" string macros are unsafe on the primary toolchain.** `strcpy_s` becomes `strcpy` on MinGW, and live overflows remain at `common/scriplib.cpp:51` and `hlcsg/qcsg.cpp:1658`. Both reports found this independently.
  4. **Several earlier fixes are incomplete, regressed, or landed in compiled-out code** (§3). The prior `handling.md` overstates the hardening achieved.
  5. **Safety nets are disabled.** `NDEBUG` is forced in every configuration, so `hlassert` is a no-op. CI does not build Windows and runs no docs, smoke or package gates.

---

## 2. Cross-cutting themes

1. **Header-trusting parsers.** Counts, offsets and sizes read from files are used for allocation and indexing without bounds or sign checks. A negative `int` size that passes a `<= max` check and is then cast to `size_t` in `fread` is the single most repeated bug pattern (H4, H9, M6 and ripent).
2. **Off-by-one boundaries.** Recurring forms are `>` instead of `>=` (H11, M7, `qbsp.cpp` `ReadSurfs`) and `arr[n] = arr[0]` wrap-around writes into arrays sized exactly to the maximum (M3, in two copies of the winding code).
3. **Unsafe compatibility macros** (`strcpy_s`/`sprintf_s` → raw) and size-less APIs (`ExtractFileExtension`).
4. **32-bit arithmetic in size calculations.** `int` products (`vismatrix`, miptex, `lmcache`), `long`/`unsigned long` being 32-bit on Win64 (`q_filelength`, `AllocBlock`).
5. **Fixes applied to dead code.** Default macros in `common/cmdlib.h` (`HLCSG_AUTOWAD_NEW`, `HLCSG_WADCFG_NEW`, `HLVIS_MAXDIST_NEW`, `HLRAD_OPAQUE_NODE`, …) compile out large regions. Verify that a code path is live before fixing it.
6. **Option and entity keys that silently do nothing** (hlbsp `-threads`, hlcsg `hullfile`, hlrad `smooth`).
7. **Process gaps:** forced `NDEBUG`, minimal CI, no tests, doc-path drift.

---

## 3. Corrections to prior "Fixed" status

| Prior item | Corrected status | Evidence (C unless noted) |
|---|---|---|
| Security F1 / Memory F5 (`strcpy_s` macros "Mitigated") | **Open, High** | Live unbounded copies at `common/scriplib.cpp:51` and `hlcsg/qcsg.cpp:1658` (C, G). |
| F4 `UnparseEntities` | **Incomplete** | An off-by-one plus `strcat` before the check write up to 3 bytes past `g_dentdata` (`common/bspfile.cpp:1816-1838`). Valid long epairs now fail with "Entity text too long". |
| F14 / CQ-11 texlight `sscanf` | **Incomplete, High** | `%4095s` into a 260-byte `_MAX_PATH` buffer on Windows (`hlrad/qrad.cpp:564-575`). |
| F5/F12 `.vis` parsing | **Incomplete, dead code** | `num_sides` is clamped to 512 against 256 `planes[]`. The `fscanf`/`feof` returns are unchecked (C, G). The code is compiled out by `HLVIS_MAXDIST_NEW`. |
| F6 `alloca` → `malloc` (hlvis) | **Regression** | Heap allocation in the innermost `RecursiveLeafFlow` frame (a plausible slowdown). Degenerate portals now abort (`hlvis/flow.cpp:533-600`). |
| F7 / F8 WAD name copies, overlapping `snprintf` (hlcsg) | **Fixed in dead code only** | `autowad.cpp` and `wadinclude.cpp` are compiled out. The same pattern remains (dead) at `wadcfg.cpp:522` and `textures.cpp:399`. |
| C2 critical-section refcount | **Incomplete** | The Win32 uninit path can call `DeleteCriticalSection` twice (`common/threads.cpp:368-379`). |
| MEM-F4 `FreeBlock(NULL)` | **Incomplete** | Fixed on Win32 only (`common/blockmem.cpp:135-142`). |
| CQ-15 `q_filelength` | **Incomplete** | `long` is 32-bit on Win64. `LoadFile` ignores the -1 return, so `SafeRead(-1)` follows (`common/filelib.cpp:107-125, 198-204`). |
| MEM-F2 `m_MaxPoints` | **Incomplete (latent)** | `Winding::Chop`/`Clip` do not update it. There are no callers today. |
| Winding resize, `operator=`, `safe_strncat`/`safe_snprintf`, C3, C4, CQ-10, hlrad F7 | Confirmed fixed | — |

---

## 4. Prioritized findings

Severity order is **Critical > High > Medium > Low**. **Src:** C = Claude, G = Glyph, C+G = both.

### High

| # | Src | Location | Defect → impact |
|---|---|---|---|
| H1 | C | `common/threads.cpp:383-384, 415, 606-614, 692, 729` | `g_numthreads` is never clamped to `MAX_THREADS` (64). `-threads >64`, or a POSIX host with more than 64 CPUs, overflows the stack arrays. |
| H2 | C | `common/bspfile.cpp:409-428` | `CopyLump` does not check `ofs`/`length` against the file size or destination capacity. A crafted BSP overflows global arrays in every tool. |
| H3 | C+G | `common/cmdlib.h:607-619` → `common/scriplib.cpp:51`, `hlcsg/qcsg.cpp:1658` | `strcpy_s` is a raw `strcpy` on MinGW. A long `$include` overflows `filename[_MAX_PATH]`; a classname over 127 chars overflows a stack buffer. |
| H4 | C | `hlcsg/textures.cpp:596-668` | A negative WAD `disksize` passes the size check, and `fread(SIZE_MAX)` then overflows `g_dtexdata`. |
| H5 | C | `hlcsg/textures.cpp:419-432` | `numlumps` is unvalidated. The `realloc` size can overflow and its result is unchecked. A negative count leaves a NULL table with a positive count. |
| H6 | C | `hlcsg/qcsg.cpp:157-160`, `:2689` | The map's `texdata` key raises `g_max_map_miptex` after allocation, so `WriteMiptex` overflows the heap. |
| H7 | C | `hlcsg/map.cpp:1344-1360` | A map whose first entity is a func_group produces a negative `memmove` size and corrupts memory. |
| H8 | C | `hlrad/loadtextures.cpp:263-287` | Miptex dimension products wrap to 0, giving `malloc(0)` followed by a huge copy, then a divide-by-zero at `:409`. |
| H9 | C+G | `ripent/ripent.cpp:668-683, 703-724, 749-751` | Texture import trusts `numlumps`, `infotableofs`, `nummiptex` and `disksize`, so the heap overflows. (G flagged the weak reads at `:640-760`.) |
| H10 | C | `hlvis/flow.cpp:1176, 1193` | `g_numportals` is never checked against `MAX_PORTALS` before the 64 KB stack `portalsee[]`, so large maps overflow every worker's stack. |
| H11 | C | `hlvis/vis.cpp:1235` | `>` should be `>=`, so a `.prt` leaf index writes one past `g_leafs`. |
| H12 | C | `hlrad/qrad.cpp:564-575` | Texlight `%4095s` into 260 bytes on Windows. |
| H13 | C+G | `CMakeLists.txt:33, 41`, `Makefile:81`, `common/hlassert.h` | `NDEBUG` is forced in every configuration, so `hlassert` and `assert` never run. Several checks that the code relies on are inert (e.g. `hlrad/lightmap.cpp:337, 344`). |

### Medium

| # | Src | Location | Defect → impact |
|---|---|---|---|
| M1 | C | `common/threads.cpp:562` | The Linux affinity loop uses `CPU_COUNT` instead of `CPU_SETSIZE`, so a cpuset gives 0 threads and the output is silently corrupt. |
| M2 | C | `common/threads.cpp:657-739` | `pthread_*` failures are compared with -1 and never detected, so `pthread_join` runs on an uninitialized handle. |
| M3 | C | `common/winding.cpp:905-1102`; `hlvis/flow.cpp:62-110` | A 128-point winding writes `sides`/`dists[numpoints]` one past the stack arrays. `Winding(dface_t)` takes an unbounded `numedges`. |
| M4 | C | `common/bspfile.cpp:2006-2010` | `GetTextureByNumber` reads `g_dtexdata[-1]` for missing textures. |
| M5 | C | `hlrad/qrad.cpp:706, 1421, 1464, 1500, 1564` | Unchecked `dataofs[i]` (-1 or corrupt) causes out-of-bounds reads of the miptex name. |
| M6 | C | `ripent/ripent.cpp:531-627` | Export does not validate `dataofs`, and an empty texture lump gives `SafeWrite(-4)`. |
| M7 | C | `hlcsg/brush.cpp:2056` | `>` should be `>=`, so a hull-shape brush with about 130 faces overflows the stack. |
| M8 | C | `hlvis/vis.cpp:235-246, 1146-1153, 1231, 1240` | No lower bound on portal counts and unchecked `calloc`, leading to a NULL deref or over-read. |
| M9 | C | `hlrad/transfers.cpp:140-203` | `.inc` sizes and indices are trusted. `AllocBlock(unsigned long)` truncates on Win64, and the file format records no compression or rgb settings. |
| M10 | C | `hlrad/vismatrix.cpp:426` | `int` overflow for 46k–65k patches gives a bogus out-of-memory error. |
| M11 | C | `hlrad/lightmap.cpp:847-858, 5526` | The `lmcache` uses about 1 GB with `-extra` and 16 threads. `-blur` is unbounded. |
| M12 | C (plausible) | `hlrad/lightmap.cpp:5434-5435, 9024` | `GetPhongNormal` divides with no guard, and the NaN → int cast is undefined, giving black or overbright luxels. |
| M13 | C | `common/bspfile.cpp:547-561` via `ripent/ripent.cpp:1273` | `-import` rewrites the `.bsp` in place with no backup or atomic rename. |
| M14 | C | `ripent/ripent.cpp:89` | `isspace(signed char)`: bytes ≥ 0x80 are undefined behaviour. |
| M15 | C | `hlvis/flow.cpp:533-600` | Hot-path `malloc` regression (§3). |
| M16 | G | `common/cmdlib.h:708`, `common/cmdlib.cpp:253-273, 389-408` | `ExtractFileExtension` and the other `Extract*` helpers take no destination size; current callers pass `_MAX_PATH` buffers, so the risk is latent. This is prior F3, re-raised. |
| M17 | C+G | `.github/workflows/.github.yml` | `checkout@v2`. No Windows/MinGW job (the only platform with `VERSION_64BIT`). No docs, smoke or package gates, which AGENTS.md requires. |
| M18 | C | `docs/api/Doxyfile:77` | `OUTPUT_DIRECTORY` is relative to the source tree and ignores the build directory. |
| M19 | G | systemic (`hlvis/flow.cpp:538, 614, 837, 902, 968, 995`, hlrad, `common/blockmem.cpp`) | Allocation results are checked inconsistently across `malloc`/`new`/`AllocBlock` families. Specific unchecked sites are listed in L-Robustness. |

### Low

**Behaviour (silent no-ops / logic) — C**
- **`-threads` ignored in hlbsp.** A local declaration hides the global (`hlbsp/qbsp.cpp:1918`).
- **`hullfile` key never applied in hlcsg.** It is applied after `LoadHullfile` runs, and its `if` is always true (`hlcsg/qcsg.cpp:166-169`).
- **`smooth` key ignored in hlrad.** Copy-paste bug (`hlrad/qrad.cpp:386-390`).
- **`IPlaneEquiv` reversed check never runs.** Its second loop copies the first (`hlcsg/brush.cpp:986-996`).
- **Wrong texture checksum length.** It uses `g_numedges` (`common/bspfile.cpp:521`).
- **`MaxDistVis` uses `&&` where it should use `||`** (`hlvis/flow.cpp:1581-1587`).
- **Dead-code copy-paste bug.** `p2` where `p1` is meant (`hlrad/mathutil.cpp:519`).
- **`-inc` reruns skip `CreateFinalStyleArrays`** (`hlrad/vismatrix.cpp:661`, `hlrad/sparse.cpp:941`).
- **hlbsp/hlvis count mismatch (plausible).** NULL-winding portals are counted but not written (`hlbsp/portals.cpp:192, 292-301`).

**Bounds — C**
- **Writes before the limit check:** `hlbsp/writebsp.cpp:381-382` and `hlbsp/surfaces.cpp:397-405`.
- **`ReadSurfs`/`ReadBrushes` validation:** off-by-one checks, no negative rejection, unvalidated `planenum`, and `%u` read into `int` (`hlbsp/qbsp.cpp:1160-1280`). G also flags weak reads at `:1133-1226`.
- **`mightsee` one-byte overrun** at 32,705 or more leaves (`hlvis/vis.h:117`).
- **Divide by zero when the leaf count is 0** (`hlvis/vis.cpp:818, 1059`).
- **Token parsing:** `scriplib` reads before the `end_p` check, and a token of exactly `MAXTOKEN` writes one past the end (`common/scriplib.cpp:163-245`).
- **`+`/`-` texture names left uninitialized** (`hlcsg/textures.cpp:697-715`).
- **`AddLump` over-reads by up to 3 bytes** (`common/bspfile.cpp:540`).
- **Overlapping `strncpy`** (`hlrad/loadtextures.cpp:234, 343`).
- **Dead-code write one past the end** (`hlcsg/qcsg.cpp:2386`).

**Robustness and resources — C**
- **Unchecked `fopen`:** `hlcsg/hullfile.cpp:43-48`.
- **Unchecked allocations:** `hlrad/lightmap.cpp:6175`, `hlrad/qrad.cpp:1408-1550` and `hlrad/trace.cpp:747-776`.
- **Leaks:** `hlrad/transfers.cpp:220-233` (`tRGBData`), sun normals (`hlrad/lightmap.cpp:3444-3484`), and the Xash direction arrays.
- **Unchecked options:** `-texdata`/`-lightdata` range checks are missing (`hlcsg/qcsg.cpp:2474`, `ripent/ripent.cpp:1063, 1079`).
- **ripent file handling:**
  - Unconditional `.tex`/`.ent` deletes (`:528, :779`).
  - `malloc(0)` gives a false out-of-memory error.
  - Lump names may not be NUL-terminated.
- **Undefined behaviour:**
  - `compress.h:195-197` has a shift of 32 or more, in the default rgb-transfer path.
  - `HANDLE` is printed with `%08X`.
  - `(int)b1 << 24` overflows a signed int (`common/cmdlib.cpp:447, 504`).
  - `scriplib.cpp:294` passes a `char` to `%s`.
- **Threading:** unlocked `stylewarningcount`, and `rand()` in threaded code (`hlrad/lightmap.cpp`).
- **Deep recursion (plausible):** recursion depth on default 1 MB Windows thread stacks (`hlvis/flow.cpp:374, 644`).
- **Big-endian hosts:** WAD `lumpinfo.size` and the `.wa_` header are never byte-swapped.

**Style and portability — G (downgraded after verification)**
- **Constant-string copies (Low, style only).** `hlcsg/map.cpp:1221` (`sprintf` of a compile-time constant into 128 bytes) and `common/scriplib.cpp:85` (`strcpy_s` of a literal) are both fixed-length, so they cannot overflow. Convert them for consistency.
- **Dead-code macro use.** `hlcsg/wadcfg.cpp:316` (`strcpy_s`) is compiled out.
- **Legacy `alloca` macro paths.** `common/win32fix.h:14-16` and `common/cmdlib.h:625-631` remain.
- **`HAVE_CONFIG_H` includes a `config.h` that is not in the tree.** `common/cmdlib.h:621`, `common/threads.cpp:8`. Correctness depends on build-provided defines.

**Build, docs, hygiene — C+G**
- **Two build systems diverge.** `Makefile` vs CMake: the Makefile forces `-O2 -DNDEBUG`, is POSIX-only, advises ignoring warnings, and always rebuilds through the phony `printusage`.
- **CPack** ships no license (`Terms of Use.txt` is not installed), manual or HTML docs (`CMakeLists.txt:205-213`).
- **Doc-path drift:** `AGENTS.md:74` and `docs/developer/developer.md:18, 58` still reference `docs/Doxyfile` and `docs/manual.md`.
- **Doxygen** sets `WARN_IF_UNDOCUMENTED = NO` and `EXTRACT_ALL = YES` (`docs/api/Doxyfile:551, 930`), so missing API docs never fail.
- **Manual sync.** `docs/user/manual.md` has not been cross-checked against every tool's help output.
- **Version strings.** The debug string is `"v3.4 dbg"` (`common/cmdlib.h:14`). Release versions are consistent at 3.5.1.
- **CHANGELOG.** `[Unreleased]` duplicates 3.5.0 items, and the docs reorganisation (`9a7d371`) is not recorded.
- **`.gitignore`** lacks `*.o` and `buildwarn.txt`. The local `build/` directory is ignored and untracked, so it is informational only.
- **README** has no links to the docs and doesn't mention the `package` target.
- **Smoke gate.** The tools exit with rc=1 when printing the banner, because there is no `-version` flag. Gate scripts must tolerate this, or a flag should be added.

---

## 5. Build and verification evidence (C)

- **Build:** MinGW 64-bit Release configures and builds all five tools, with 373 warnings.
  - sign-compare 125, unknown-pragmas 77, maybe-uninitialized 36, parentheses 33, unused-variable 31, write-strings 28, others under 15.
  - There are none of the format, stringop-overflow, array-bounds or sizeof-pointer-memaccess classes.
- **Actionable warnings:** `-Wrestrict` at `hlrad/loadtextures.cpp:234`, and `-Wclass-memaccess` on `brush_t` (`hlcsg/map.cpp:37, 95, 728, 1348, 1359, 1363`).
- **Docs:** Doxygen resolves every INPUT path with 0 warnings.
- **Smoke:** all five tools print `<tool> v3.5.1 VL35 64-bit`. G confirmed the same for hlcsg and ripent.

## 6. Recommended remediation order

1. **Thread pool:** clamp to `MAX_THREADS`, fix the affinity loop and the pthread error checks (H1, M1, M2). These are small and low-risk, and they affect every tool.
2. **Shared readers:** validate `CopyLump` (H2), harden `UnparseEntities`/`AddLump`/`GetTextureByNumber` (§3, M4), and remove the unsafe `strcpy_s` mapping by routing callers to `safe_strncpy` (H3).
3. **WAD/miptex parsing** in hlcsg, hlrad and ripent, with one shared validation pattern: reject negative or oversized sizes and counts, use `size_t` arithmetic, and check results (H4, H5, H8, H9, M5, M6).
4. **hlvis portal loading:** `MAX_PORTALS` check, `>=`, lower bounds, and the `ChopWinding`/winding `+1` sizing (H10, H11, M3, M8).
5. **hlcsg map semantics:** the `texdata` ordering, the worldspawn assumption, the hull-shape off-by-one, and the remaining `strcpy_s` (H6, H7, M7).
6. **hlrad:** texlight width (H12), `.inc` validation (M9), `size_t` vismatrix sizing (M10), and the `-blur` cap (M11).
7. **Build and CI:** make `NDEBUG` per configuration, add a Windows job plus docs/smoke/package gates, fix the Doxyfile output path, and package the license and docs (H13, M17, M18).
8. **Low-severity items** in batches per subsystem. Update `audits/2026-11-10/handling.md` statuses to match §3.

Before changing anything:
- Confirm the code path is compiled in with the default macros. Several prior fixes landed in dead code.
- Record user-visible fixes in `CHANGELOG.md` `[Unreleased]`.

## 7. Limitations

- Static review only. No fuzzing, sanitizer runs or real-map regression compiles were done, and the repository has no tests.
- Coverage gaps:
  - `hlrad/lightmap.cpp:4080-4800` was only skimmed.
  - Compiled-out legacy paths were not deep-read.
  - `docs/user/manual.md` was not exhaustively diffed against help output.
- Items marked **plausible** need a reproducer before they are fixed.
