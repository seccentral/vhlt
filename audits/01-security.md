# Security and Unsafe Input Handling Audit

Scope: `common/`, `hlcsg/`, `hlbsp/`, `hlvis/`, `hlrad/`, `ripent/` in `<repo>`.

## Summary

The codebase is a set of native level-compiler tools that parse untrusted map, BSP, WAD, and configuration files. Most modern code paths use bounded helpers (`safe_snprintf`, `safe_strncpy`), but a number of legacy or conditional paths still use unbounded C string operations, unbounded `fscanf`/`sscanf` conversions, unchecked array indexing, and stack allocation based on attacker-controlled counts. The highest-risk issues are in `.vis` parsing (`hlvis`), entity serialization (`common/bspfile.cpp`), and the redefinition of the supposedly safe `strcpy_s`/`sprintf_s` macros. Several apparent safety checks are also macro-conditional, so the secure behavior depends on compile-time flags remaining enabled.

Overall risk: **Medium-High** for processing untrusted input files; **Low** for normal trusted-author workflows.

## Method

- Enumerated source files in the requested directories.
- Searched for unsafe string and input APIs: `strcpy`, `strcat`, `sprintf`, `sscanf`, `fscanf`, `gets`, `alloca`, `system`, `popen`, `exec`, `malloc`/`calloc` with arithmetic.
- Reviewed the central helpers in `common/cmdlib.cpp`, `common/cmdlib.h`, `common/filelib.cpp`, and `common/hlassert.h`.
- Inspected parsing paths for external files: maps, BSPs, WADs, `.vis`, extents, wad configs, language files, hull files, and command-line/param files.
- Checked buffer sizes and array bounds around each unsafe call.
- No code was changed.

## Findings

### F1. Safe-string macros are redefined as unsafe functions on some toolchains
- Severity: High
- File: `common/cmdlib.h:607-616`
- Description: On MinGW or older MSVC, `strcpy_s` and `sprintf_s` are undefined and redefined to plain `strcpy`/`sprintf`. Code that appears to use bounded "safe" APIs therefore loses bounds checking on those builds. The same header also disables deprecation warnings for unsafe CRT functions (`common/cmdlib.h:618-619`).
- Evidence:
  ```c
  #define strcpy_s strcpy //--vluzacn
  #define sprintf_s sprintf //--vluzacn
  ```
- Recommendation: Remove these compatibility macros or provide real bounded implementations; compile with `_CRT_SECURE_NO_WARNINGS` disabled and fix call sites instead.

### F2. `safe_strncat` can overflow its destination
- Severity: High
- File: `common/cmdlib.cpp:555-569`
- Description: `safe_strncat(dest, src, count)` calls `strncat(dest, src, count)` even though `count` is the total destination capacity. `strncat` appends up to `count` bytes after the existing contents, so a non-empty destination can be overflowed. It then truncates at `count - 1`, but only after the unsafe append.
- Evidence:
  ```c
  strncat(dest, src, count);
  dest[count - 1] = 0;
  ```
- Recommendation: Compute remaining capacity as `count - strlen(dest) - 1` and use `strncat(dest, src, remaining)`; return false on truncation.

### F3. `ExtractFilePath`/`ExtractFile`/`ExtractFileBase`/`ExtractFileExtension` have no destination-size parameter
- Severity: Medium
- File: `common/cmdlib.cpp:195-255`
- Description: These helpers copy path components into caller-provided buffers using `memcpy` without knowing the destination capacity. Callers must manually size buffers; any mismatch results in overflow. This pattern is used widely with `_MAX_PATH` buffers and long external paths.
- Evidence:
  ```c
  memcpy(dest,path,directory_pos+1);
  dest[directory_pos+1] = 0;
  ```
- Recommendation: Add a `dest_size` parameter to all Extract* functions and enforce it internally.

### F4. Entity serialization can overflow a fixed 4096-byte line buffer
- Severity: High
- File: `common/bspfile.cpp:1812-1822`
- Description: `UnparseEntities()` formats each key/value pair with `sprintf(line, "\"%s\" \"%s\"\n", ep->key, ep->value)` into `char line[MAXTOKEN]` (4096 bytes). `MAX_KEY` is 128 and `MAX_VAL` is 4096, so a maximum-size value plus key can exceed the 4096-byte buffer before the later `end > buf + MAX_MAP_ENTSTRING` check runs.
- Evidence:
  ```c
  char            line[MAXTOKEN];
  ...
  sprintf(line, "\"%s\" \"%s\"\n", ep->key, ep->value);
  strcat(end, line);
  ```
- Recommendation: Use `safe_snprintf(line, sizeof(line), ...)` and abort/truncate if it does not fit; validate entity value lengths when parsing.

### F5. `.vis` blocker parsing is unbounded and lacks array bounds
- Severity: Critical
- File: `hlvis/vis.cpp:872-899`
- Description: `InitVisBlock()` reads a `.vis` file with `fscanf(fp, "%s\n", ...)` into 64-byte fields (`name[64]`, `blocknames[..][64]`) with no width limit. It also increments `x` and loops `num_blocks`/`num_sides` without validating them against `MAX_VISBLOCKERS` or `MAX_PORTALS_ON_LEAF`, allowing out-of-bounds writes and stack/heap corruption from a malicious `.vis` file.
- Evidence:
  ```c
  fscanf(fp, "%s\n", g_visblockers[x].name);
  fscanf(fp, "%s\n", g_visblockers[x].blocknames[i]);
  x++;
  ```
- Recommendation: Use width-limited conversions (`%63s`), validate counts before loops, and bounds-check `x` against `MAX_VISBLOCKERS`.

### F6. Stack allocation size depends on portal point counts
- Severity: High
- File: `hlvis/flow.cpp:532`
- Description: `alloca(sizeof(plane_t) * prevstack->source->numpoints * prevstack->pass->numpoints)` can overflow the integer multiplication or allocate an excessive stack frame from malformed/very large portal data. `RVIS_LEVEL_2` is enabled by default.
- Evidence:
  ```c
  stack.clipPlane = (plane_t*)alloca(sizeof(plane_t) * prevstack->source->numpoints * prevstack->pass->numpoints);
  ```
- Recommendation: Replace `alloca` with heap allocation, validate counts, and use checked multiplication.

### F7. External WAD texture names are copied without ensuring source termination
- Severity: High
- File: `hlrad/loadtextures.cpp:234,255,301,307`; `ripent/ripent.cpp:567,618,740`; `hlcsg/autowad.cpp:44,70`
- Description: `strcpy`/`strcpy_s` is used to copy 16-byte WAD texture names from untrusted WAD data into fixed 16-byte buffers. Although some call sites force a terminator afterward, the copy itself can overflow if the source is not NUL-terminated. On old toolchains, `strcpy_s` is only `strcpy` (see F1), so no bounds check occurs.
- Evidence:
  ```c
  strcpy(tex->name, header->name);
  tex->name[16 - 1] = '\0';
  ```
- Recommendation: Use `memcpy` plus explicit truncation/termination, or bounded copies with width limits, when reading WAD headers.

### F8. Texture path construction can overflow and uses overlapping `snprintf` source/destination
- Severity: Medium
- File: `hlcsg/wadinclude.cpp:138-140`; `common/cmdlinecfg.cpp:271-272`
- Description: `HandleWadinclude()` calls `safe_snprintf(szTmpWad, 1024, "%s%s;", szTmpWad, ...)` with the destination also used as a source, which is undefined behavior for `snprintf`. `ParseParamFile()` calls `ExtractFilePath(tmp, filepath)` followed by `strcat(filepath, paramfilename)`; if the extracted path is long, the append overflows `_MAX_PATH`.
- Evidence:
  ```c
  safe_snprintf(szTmpWad, 1024, "%s%s;", szTmpWad, tmp);
  ```
  ```c
  ExtractFilePath (tmp, filepath);
  strcat (filepath, paramfilename);
  ```
- Recommendation: Build into a separate temporary buffer; validate lengths before appending.

### F9. Map-name-derived filenames are built with unbounded `sprintf` in some paths
- Severity: Medium
- File: `hlbsp/qbsp.cpp:1762,1768,1861,1866`
- Description: `ProcessFile()` and related code use `sprintf(name, "%s.p%i", filename, i)` into `char name[_MAX_PATH]`. If `filename`/`g_Mapname` is close to `_MAX_PATH` characters, the suffix causes an overflow. Similar patterns exist in `hlrad` debug paths, though some use `_MAX_PATH+20` slack.
- Evidence:
  ```c
  sprintf(name, "%s.p%i", filename, i);
  ```
- Recommendation: Replace these with `safe_snprintf(name, sizeof(name), ...)`.

### F10. Unsafe fallback branches remain behind compile-time macros
- Severity: Medium
- File: `hlcsg/qcsg.cpp:2646-2648` (safe branch active); latent unsafe branches at `hlrad/qrad.cpp:5790`, `hlvis/vis.cpp:860,1007`, `hlbsp/qbsp.cpp:1762` area
- Description: Several files contain `#else` branches using `strcpy`, `DefaultExtension`, and unbounded path handling. These are disabled by macros in the default configuration, but the security of the build depends on those macros remaining enabled. A configuration change or partial rebuild can silently re-enable unsafe behavior.
- Evidence:
  ```c
  #else
      strcpy(g_source, mapname_from_arg);
      StripExtension(g_source);
      DefaultExtension(g_source, ".bsp");
  #endif
  ```
- Recommendation: Delete or guard the legacy branches, or make the safe helper path the only implementation.

### F11. Untrusted map/WAD paths are not validated (path traversal / arbitrary file access)
- Severity: Medium
- File: `hlcsg/qcsg.cpp:2646`; `hlcsg/wadpath.cpp:112-174`; `hlcsg/wadcfg.cpp`
- Description: Map names and WAD paths from command-line arguments, entity keyvalues (`wad`), and wadcfg files are used directly to open/read/write files. A crafted map name such as `../../target` can cause outputs or reads outside the intended directory. There is no allow-listing, canonicalization, or rejection of traversal sequences.
- Evidence:
  ```c
  safe_strncpy(g_Mapname, mapname_from_arg, _MAX_PATH);
  ```
  ```c
  pszWadPaths = ValueForKey(&g_entities[0], "wad");
  ```
- Recommendation: Validate/basename map names and canonicalize WAD paths before use; restrict output filenames to a safe character set.

### F12. `InitVisBlock` loops and counts are not validated
- Severity: High (part of F5, listed separately for clarity)
- File: `hlvis/vis.cpp:876-899`
- Description: `num_blocks` and `num_sides` are read from the file and used directly as loop bounds and array indices. Negative or very large values can cause out-of-bounds access or excessive work even before the `%s` overflow.
- Evidence:
  ```c
  fscanf(fp, "%d\n", &num_blocks);
  for(i = 0; i < num_blocks; i++) { ... }
  fscanf(fp, "%d\n", &num_sides);
  for(i = 0; i < num_sides; i++) { ... }
  ```
- Recommendation: Reject negative/out-of-range counts before use.

### F13. Integer multiplication in `alloca` and allocation sites is unchecked
- Severity: Medium
- File: `hlvis/flow.cpp:532`; `hlcsg/textures.cpp:470-471`
- Description: Several size calculations multiply untrusted or large counts without overflow checks. `textures.cpp` increments `nTexFiles` before asserting `nTexFiles < MAX_TEXFILES`, so a failed assertion happens after the prior indexing; with assertions disabled, the next iteration can write past the array.
- Evidence:
  ```c
  nTexFiles++;
  hlassume(nTexFiles < MAX_TEXFILES, assume_MAX_TEXFILES);
  ```
- Recommendation: Perform bounds checks before indexing and use checked arithmetic for allocation sizes.

### F14. `sscanf` into fixed buffers without width limits
- Severity: Medium
- File: `hlrad/qrad.cpp:575`
- Description: Texlight parsing uses `sscanf(scan, "%s %f %f %f %f", szTexlight, ...)` where `scan` is 4096 bytes and `szTexlight` is `_MAX_PATH` bytes. A token longer than `_MAX_PATH` overflows `szTexlight`.
- Evidence:
  ```c
  argCnt = sscanf(scan, "%s %f %f %f %f", szTexlight, &r, &g, &b, &i);
  ```
- Recommendation: Use a width limit (`%4095s` or smaller) matching the destination.

## Recommendations

1. Remove or neutralize the `strcpy_s`/`sprintf_s` compatibility macros in `common/cmdlib.h`; provide real bounded implementations or fix all call sites.
2. Fix `safe_strncat` to account for the existing destination length.
3. Add destination-size parameters to all `Extract*` path helpers.
4. Replace remaining `strcpy`, `strcat`, `sprintf`, and unbounded `fscanf`/`sscanf` calls with bounded equivalents, especially in `hlvis/vis.cpp`, `common/bspfile.cpp`, and `hlbsp/qbsp.cpp`.
5. Validate counts and indices before loops and allocations in file parsers (`.vis`, BSP, WAD).
6. Replace `alloca` in `hlvis/flow.cpp` with heap allocation and checked sizes.
7. Remove legacy unsafe `#else` branches or make safe helpers unconditional.
8. Validate or canonicalize external filenames/map names to prevent path traversal and arbitrary file reads/writes.
9. Enable compiler hardening (`/GS`, ASLR, DEP) and run fuzzing on map/BSP/WAD/vis parsers.