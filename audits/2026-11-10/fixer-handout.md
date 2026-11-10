# Fixer Handout — VHLT hardening pass (from audit 2026-11-10)

You are fixing confirmed defects in VHLT, a set of legacy C++ Half-Life map compile tools (`hlcsg`, `hlbsp`, `hlvis`, `hlrad`, `ripent` plus the shared `common/`).

The full evidence is in `audits/2026-11-10/final-audit.md`. Read §2 (themes) and §3 (corrections to prior fixes) of it first. IDs below (H1, M3, …) refer to that file.

## Rules (mandatory)

**Tooling:**
- The shell is Git Bash. Prefix **every** shell command with `rtk`.
- Use LSP (`findReferences`, `goToDefinition`, `incomingCalls`) for navigation. **Before changing any function, run `findReferences` on it.** Do not grep for definitions.

**Code changes:**
- Follow `AGENTS.md`. Use `safe_snprintf`, `safe_strncpy` and `safe_strncat` from `common/cmdlib.h`. Never add raw `strcpy`, `sprintf` or `strcat`.
- Keep changes minimal (YAGNI). Do not refactor globals, restructure modules, or "clean up" nearby code.
- Do not duplicate validation logic (DRY). Where several readers need the same check, add one small helper in `common/` and reuse it.
- Preserve behaviour for valid inputs. On invalid input, call `Error(...)` with a clear message; that is the codebase's convention, and `Error` does not return.

**Before each fix:**
- **Confirm the code is compiled in.** Check the feature macros in `common/cmdlib.h`. `hlcsg/autowad.cpp`, `hlcsg/wadinclude.cpp`, `hlcsg/wadcfg.cpp` after line ~133, and the `.vis` parser in `hlvis/vis.cpp` are dead code. Do not spend effort there.
- Re-read the cited lines. Line numbers may have drifted. If a finding is wrong or already fixed, skip it and say so in your report.

**Workflow:**
- Work one batch at a time. Before applying a batch, present its diff for approval.
- After each batch, run the gates below.
- Do **not** commit, tag or push unless explicitly told to.

## Gates (after every batch)

1. `rtk cmake -S . -B build -G "MinGW Makefiles"`, then `rtk cmake --build build -j`. All five tools must build. The warning count must not go above the baseline of 373, and no new warning may be added in the lines you touched.
2. `rtk cmake --build build --target docs`.
3. Each of `build/bin/{hlcsg,hlbsp,hlvis,hlrad,ripent}.exe` run with no arguments must print `v3.5.1`. **rc=1 is expected**, because the banner comes from the usage path.
4. Run `rtk git status` and confirm there are no stray artifacts. `build/` is git-ignored.

## Batches (in order)

### Batch 1 — thread pool (`common/threads.cpp`)
- **H1:** clamp `g_numthreads` to `[1, MAX_THREADS]` in one place that covers both the `-threads` and the auto-detect paths (for example `ThreadSetDefault` plus the start of `RunThreadsOn`). Emit a `Warning` when clamping.
- **M1 (`:562`):** iterate `i < CPU_SETSIZE` and count `CPU_ISSET`, or simply use `CPU_COUNT(&cs)`. Never return 0; fall back to 1.
- **M2 (`:657-739`):** treat a non-zero `pthread_*` return as failure. Don't `pthread_join` threads that failed to start.
- **Low:** check `sysctlbyname`'s return (`:584-588`); print the `HANDLE` with `%p` (`:461, 472`).
- **Acceptance:**
  - `hlvis -threads 200 <map>` (or any tool) clamps and warns instead of crashing.
  - The default build behaves the same as before.

### Batch 2 — shared readers and strings (`common/`)
- **H2 (`bspfile.cpp:409-428`, `CopyLump`):** reject `ofs < 0`, `length < 0`, `ofs + length > filesize`, `length % size != 0`, and `length/size > destination max`. Pass in the destination capacity. Check every call site with `findReferences`.
- **§3 F4 (`bspfile.cpp:1816-1838`):** in `UnparseEntities`, check remaining capacity *before* each `strcat`/`memcpy`, allowing for the NUL. Size `line[]` to fit the largest epair `ParseEpair` accepts (key + value + 6) so valid long values work again.
- **`AddLump` (`bspfile.cpp:540`):** pad from a zeroed local instead of reading past the source.
- **M4 (`bspfile.cpp:2006`):** in `GetTextureByNumber`, return NULL or a safe default when `miptex` or `ofs` is out of range or `-1`. Check its callers.
- **Checksum (`bspfile.cpp:521`):** use `g_texdatasize`, not `g_numedges`.
- **H3:** in `common/cmdlib.h:607-619`, stop mapping `strcpy_s`/`sprintf_s` to the raw functions on MinGW and old MSVC. Convert the live callers (`scriplib.cpp:51, 85`, `cmdlib.cpp:408`, `hlcsg/qcsg.cpp:1658`, plus any others `findReferences` turns up) to `safe_strncpy`/`safe_snprintf` with `sizeof(dest)`. If removing the macro breaks a dead-code branch, guard it rather than editing the dead logic.
- **M3 (`winding.cpp:905-1102`):** size `dists[]`/`sides[]` to `MAX_POINTS_ON_WINDING + 1`, or reject `m_NumPoints >= MAX_POINTS_ON_WINDING`. Validate `numedges` in `Winding(const dface_t&)`.
- **`filelib.cpp:107-125, 198-204`:** make `LoadFile` reject the `-1` length. Use a 64-bit-safe type (`long long` with `_ftelli64`/`ftello`) or reject anything above `INT_MAX`.
- **`blockmem.cpp:135-142`:** make `FreeBlock(NULL)` return early on POSIX too.
- **`threads.cpp:368-379`:** on Win32, only `DeleteCriticalSection` on a 1→0 transition.
- **`scriplib.cpp:163-245`:** add bounds checks against `end_p` and for a token of exactly `MAXTOKEN`.
- **Acceptance:** a BSP truncated to half its size, and one with an oversized entity lump, each fail with a clear `Error` in `ripent -export` and `hlrad`, not a crash.

### Batch 3 — WAD/miptex parsing (one shared pattern)
Write **one** helper in `common/` (for example `ValidateMiptexHeader(width, height, offsets, available_bytes)`) and use it everywhere below.
- **H4 (`hlcsg/textures.cpp:596-668`):** reject `disksize < 0`.
- **H5 (`hlcsg/textures.cpp:419-432`):** validate `0 <= numlumps <= reasonable max`. Compute the size in `size_t`, check the `realloc` result into a temporary pointer, and make an `fseek` failure fatal.
- **H8 (`hlrad/loadtextures.cpp:263-287`):** bound width and height (each > 0, a multiple of 16, at most some max such as 4096). Use `size_t` arithmetic for the mip sizes.
- **H9 (`ripent/ripent.cpp:668-683, 703-724, 749-751`):** validate `numlumps`, `infotableofs`, `nummiptex` and `disksize` against the file and buffer sizes *before* indexing or reading.
- **M5 (`hlrad/qrad.cpp:706, 1421, 1464, 1500, 1564`) and M6 (`ripent/ripent.cpp:531-627`):** skip a `dataofs[i]` that is -1 or out of range, the way `LoadTextures` (`hlrad/loadtextures.cpp:367`) already does. Handle `g_texdatasize == 0`.
- **`loadtextures.cpp:234/343`:** fix the overlapping `strncpy` (source and destination are the same).
- **Acceptance:** a WAD with `disksize = -1`, one with `numlumps = 0x7fffffff`, and a miptex with width 524288 each produce an `Error` in hlcsg, hlrad and ripent.

### Batch 4 — hlvis portal loading (`hlvis/`)
- **H10:** in `LoadPortals`, reject `g_numportals < 0` and `g_numportals > MAX_PORTALS`, and reject `g_portalleafs <= 0` or above `MAX_MAP_LEAFS`. Add the matching `MAX_PORTALS` check in hlbsp `WritePortalfile` so the error appears at bsp time.
- **H11 (`vis.cpp:1235`):** change `>` to `>=`.
- **M8 (`vis.cpp:1231, 1240, 235-246, 1146-1153`):** require `3 <= numpoints <= MAX_POINTS_ON_WINDING`, and check the `calloc` results.
- **M3 (`flow.cpp:62-110`, `ChopWinding`):** apply the same `+1` sizing as Batch 2.
- **M15 (`flow.cpp:533-600`):** replace the per-frame `malloc` with a fixed-size local array of `2*MAX_POINTS_ON_WINDING` planes, which is bounded and safe on the stack. Restore tolerance of `clipPlaneCount == 0` (skip, don't `Error`).
- **Low:** fix `flow.cpp:1581` (`&&` → `||`). Guard `vis.cpp:818, 1059` against a 0 divisor. Size `vis.h:117` `mightsee` to `(MAX_MAP_LEAFS+63)/64*8`.
- **Acceptance:** a `.prt` with a leaf index equal to the leaf count, or with 2-point portals, fails cleanly. Vis output on a normal map is byte-identical to the pre-change output. If you have a test map, compare the vis lump.

### Batch 5 — hlcsg map semantics (`hlcsg/`)
- **H6 (`qcsg.cpp:157-160` vs `:2689`):** either apply the `texdata` key before `dtexdata_init()`, or reallocate `g_dtexdata` when the key raises the limit. Pick whichever is the smaller change.
- **H7 (`map.cpp:1344-1360`):** call `Error` if entity 0 is not `worldspawn`, before the merge.
- **M7 (`brush.cpp:2056`):** change `>` to `>=`.
- **Low:**
  - `qcsg.cpp:166-169`: test `*ValueForKey(...)`. Note the ordering issue with `LoadHullfile`; fix it only if it's a one-line move.
  - `brush.cpp:986-996`: make the second loop test the reversed plane (`p2 + p1`).
  - `hullfile.cpp:43-48`: check the `fopen` result.
  - `textures.cpp:697-715`: zero-fill `name` before the copy.
  - `qcsg.cpp:2474`: range-check `-texdata`.

### Batch 6 — hlrad (`hlrad/`)
- **H12 (`qrad.cpp:564-575`):** make `szTexlight` hold `MAXTOKEN` bytes, or derive the `sscanf` width from `sizeof(szTexlight)-1` (for example by building the format string with `safe_snprintf`).
- **M9 (`transfers.cpp:140-203`):** validate `iIndex`/`iData` against the file size, check each `index + size` against `g_num_patches`, and check the run sum against `iData`. Reject the file (fall back to a recompute, as the existing `FailedRead` path does) instead of erroring. Also free `tRGBData` on `FailedRead` (`:220-233`).
- **M10 (`vismatrix.cpp:426`):** compute in `size_t`.
- **M11:** clamp `-blur` to a documented maximum. Document it in `docs/user/manual.md` and the help text, and compute the `lmcache` size in `size_t`.
- **Low:**
  - `qrad.cpp:386-390`: use `flTmp` for the `smooth` key.
  - Check the `malloc` results at `lightmap.cpp:6175`, `qrad.cpp:1408-1550` and `trace.cpp:747-776`.
  - Free `sunnormals`/`sunnormalweights` in `DeleteDirectLights`.
  - `compress.h:195-197`: clamp the shift to 31 or less.
- **M12 (`GetPhongNormal`):** guard the denominators with an epsilon and fall back to the face normal. This one is marked **plausible**, so add the guard only and don't change the math.

### Batch 7 — build, CI, docs
- **H13:** remove `NDEBUG` from the unconditional platform definitions in `CMakeLists.txt:33, 41`. Let CMake's per-configuration defaults handle it (Release defines it, Debug doesn't). Verify that a Debug build compiles.
- **M17 (`.github/workflows/.github.yml`):**
  - Bump `actions/checkout` to v4.
  - Add a `windows-latest` job using MinGW (or MSVC if MinGW setup is too heavy).
  - Add a docs step and a smoke step that accepts rc=1 and greps for the version.
- **M18:** make the Doxygen output follow the build directory. Configure the Doxyfile with `configure_file`, or pass `OUTPUT_DIRECTORY` through the docs target.
- **CPack:**
  - Install `Terms of Use.txt`, `docs/user/manual.md` and the HTML docs if they were built.
  - Fix the stale paths in `AGENTS.md:74` (`docs/api/Doxyfile`) and `docs/developer/developer.md:18, 58`.
  - Change `common/cmdlib.h:14` from `"v3.4 dbg"` to `"v3.5.1 dbg"`.
  - Add `*.o` and `buildwarn.txt` to `.gitignore`.

## Out of scope — do not touch
- Compiled-out code (see Rules), unless a macro removal in Batch 2 forces a guard.
- `template/Reference*.h` and the other unused templates.
- The global-state architecture, the long functions and the `#ifdef` sprawl.
- The FASTFIND race and the `texmap` locking in hlcsg, which were previously accepted.
- Big-endian support.
- Low items not listed above, unless a fix is a single line in a function you are already editing.

## Deliverables
1. One batch at a time: show the diff, apply it, run the gates, and report the results (build output summary, warning count, smoke output).
2. Add `CHANGELOG.md` `[Unreleased]` entries under "Fixed"/"Security"/"Changed", one line per user-visible fix. Also record the docs reorganisation from `9a7d371`, which is missing.
3. Add a "2026-11-10 follow-up" section to `audits/2026-11-10/handling.md` mapping each ID above to Fixed, Skipped (with reason) or Not-reproducible.
4. A final report listing what was changed, what was skipped and why, and any finding you judged incorrect.
