# Final Report — 2026-11-10 Fixer Handout Follow-up

This report covers the second verification/fixing pass over
`audits/fixer-handout-2026-11-10.md`. The first pass (commits `92c397d`,
`8837bb5`, `d0198d9`) implemented most of the handout; this pass closed the
remaining gaps found by re-reading every ID against the code at HEAD.

## What was changed (this pass)

| Area | Change |
|------|--------|
| H1 (threads) | POSIX `RunThreadsOn` now clamps `g_numthreads` to `[1, MAX_THREADS]` with a `Warning`, matching the Win32 path. `-threads 200` can no longer overflow the stack thread arrays on any platform. |
| H2 (CopyLump) | Added `LUMP_CLIPNODES` (and Xash2 `CLIPNODES2/3`) destination capacity checks; `ofs + length > filesize` computed in 64-bit so it cannot overflow `int`. |
| §3 F4 (UnparseEntities) | Capacity checks now allow for the NUL terminator on every `strcat`/`memcpy` (no more 1-byte overrun before `Error`). |
| H3 | Removed the `strcpy_s`/`sprintf_s` compatibility macros entirely (no remaining callers, including dead branches). |
| `LoadFile` | Now rejects lengths above `INT_MAX` in addition to `-1`; length kept as `long` until validated. |
| Batch 3 rule (DRY) | Added `ValidateMiptexHeader(width, height, offsets, available_bytes, strict_offsets)` in `common/`; used by `hlrad/loadtextures.cpp` and both ripent texture paths. |
| H9 | ripent WAD reader validates `numlumps`, `infotableofs`, texdata size and the lump table against the file size *before* writing to `g_dtexdata` or reading; per-lump `filepos` validated. `.tex` reader validates `nummiptex` before indexing; lump `disksize` bounded against remaining buffer. |
| M6 | ripent `WriteTextures` treats `ofs` >= `g_texdatasize` (or with a truncated header) as a missing texture instead of reading out of bounds. |
| M3 (flow) | `ChopWinding` `dists[]`/`sides[]` sized `MAX_POINTS_ON_WINDING + 1`. |
| M8 | `LoadPortals` and `NewWinding` check their `calloc` results and `Error` on OOM. |
| M9 | `readtransfers` validates `iIndex` against the file size, every `{index, size}` run against `g_num_patches`, and the run sum against `iData`; corrupt files fall back to `FailedRead` (recompute) and `tRGBData` is freed there. |
| M11 | `-blur` clamped to `[1.0, 16.0]` with a warning; maximum documented in `docs/user/manual.md` and the help text; lmcache sizes computed in `size_t`. |
| M12 | `GetPhongNormal` guards both denominators with `NORMAL_EPSILON` and falls back to the face normal on degenerate edges. |
| Low: brush.cpp | The second `IPlaneEquiv` loop now tests the reversed normal (`p2 + p1`). |
| Low: textures.cpp | `AddAnimatingTextures` zero-fills `name` and copies the fixed 16-byte miptex name instead of reading a possibly unterminated string. |
| Low: hlrad | `flTmp` is used for the entity `smooth` key; malloc results checked at `lightmap.cpp:6175`, `qrad.cpp:1416/1462/1501/1567`, `trace.cpp:747-776`; float-compression shifts clamped to 31. |
| M18 | Doxygen config moved to `docs/api/Doxyfile.in`; CMake generates it with `configure_file` so `OUTPUT_DIRECTORY` follows the build directory. |
| CPack | HTML docs installed into the package when they have been built; `AGENTS.md` path updated to `docs/api/Doxyfile.in`. |
| M17/CI | Smoke step now greps the released version (`v3.5.2`), matching the bumped binaries. |

## Skipped

Nothing. Every handout ID was implemented; no finding was judged incorrect
or not reproducible during this pass.

## Verification

- Clean build: all five tools compile; 370 warnings (baseline: 373).
- `docs` target: generates `build/docs/html/index.html` from the generated Doxyfile.
- Smoke: all five tools print `v3.5.2` (rc=1 is the expected usage path).
- Lint target completes.
- Package: `cmake --build build --target package` produces the release ZIP.
