# Code Correctness, Best Practices, and General Quality Audit

Scope: `common/`, `hlcsg/`, `hlbsp/`, `hlvis/`, `hlrad/`, `ripent/`, `template/` in `<repo>`.
Method: Static inspection plus a clean CMake/Ninja build (MinGW GCC 15.2.0), which produced 1,119 warnings and no errors. No code was changed.
Date: 2026-11-10

## Summary

The code compiles, but it contains a number of concrete logic and correctness defects: an out-of-bounds write in BSP texinfo endian conversion, a byte-count error in `Winding::resize`, incorrect truncation handling in `safe_snprintf`, an unsafe `strncat` wrapper, uninitialized "best candidate" variables that are only guarded by debug-only assertions, and several unbounded string operations. Many additional issues are exposed by compiler warnings (125 sign-compare, 45 maybe-uninitialized, 43 parentheses, 10 assignment-as-truth, 6 dangling-else, 10 misleading-indentation, 5 aggressive-loop-optimization). The highest-risk defects are in shared helpers (`common/cmdlib.cpp`, `common/winding.cpp`, `common/bspfile.cpp`) because every tool depends on them.

## Findings

### CQ-01. `Winding::resize` copies the wrong number of bytes
- Severity: Critical
- File: `common/winding.cpp:1274-1284`
- Evidence:
  ```cpp
  vec3_t* newpoints = new vec3_t[newsize];
  m_NumPoints = qmin(newsize, m_NumPoints);
  memcpy(newpoints, m_Points, m_NumPoints);
  delete[] m_Points;
  ```
  `memcpy`'s third argument is bytes, but `m_NumPoints` is an element count. Only a fraction of the old array is copied; the rest of `newpoints` is uninitialized. `m_NumPoints` is also reduced before the copy, so the count used for copying is already wrong.
- Recommendation: Save the old count, reduce `m_NumPoints` afterward, and copy `oldNumPoints * sizeof(vec3_t)` bytes.

### CQ-02. BSP texinfo endian conversion writes out of bounds
- Severity: High
- File: `common/bspfile.cpp:278-286`
- Evidence:
  ```cpp
  for (j = 0; j < 8; j++)
  {
      g_texinfo[i].vecs[0][j] = LittleFloat(g_texinfo[i].vecs[0][j]);
  }
  ```
  `vecs` is declared as `float vecs[2][4]` (`common/bspfile.h:262`). Indexing `vecs[0][j]` for `j = 4..7` writes past the end of `vecs[0]` and into `vecs[1]`. This is undefined behavior and corrupts the second texture vector on big-endian targets.
- Recommendation: Use two nested loops, converting `vecs[0][0..3]` and `vecs[1][0..3]` separately.

### CQ-03. `safe_snprintf` does not detect all truncation
- Severity: High
- File: `common/cmdlib.cpp:529-548`
- Evidence:
  ```cpp
  amt = vsnprintf(dest, count, args, argptr);
  ...
  if (amt == count)
  {
      dest[count - 1] = 0;
      return false;
  }
  ```
  `vsnprintf` returns the number of characters that would have been written. Truncation is indicated by `amt >= count`, not `amt == count`; a longer result returns a value greater than `count` and is incorrectly reported as success. On older MSVC, `vsnprintf` may return `-1` on truncation, which is also missed.
- Recommendation: Treat `amt < 0 || (size_t)amt >= count` as truncation, explicitly null-terminate, and return false.

### CQ-04. `safe_strncat` can overflow its destination
- Severity: High
- File: `common/cmdlib.cpp:555-569`
- Evidence:
  ```cpp
  strncat(dest, src, count);
  dest[count - 1] = 0;
  ```
  `strncat` appends up to `count` bytes after the existing contents, but `count` is the total destination capacity. A non-empty destination can therefore be overflowed before the final truncation.
- Recommendation: Compute `remaining = count - strlen(dest) - 1` and call `strncat(dest, src, remaining)`; return false when truncation occurs.

### CQ-05. `Winding` constructors leave `m_MaxPoints` uninitialized
- Severity: High
- File: `common/winding.cpp:319, 383, 388, 425`
- Evidence:
  ```cpp
  m_NumPoints = 4;
  m_Points = new vec3_t[m_NumPoints];
  // m_MaxPoints never assigned
  ```
  `addPoint`, `insertPoint`, and `resize` use `m_MaxPoints` to decide when growth is required. An uninitialized capacity can skip required reallocation and write past the allocated array.
- Recommendation: Initialize `m_MaxPoints` in every constructor and in `initFromPlane`, matching the allocated capacity.

### CQ-06. `Winding::operator=` is not self-assignment safe
- Severity: Medium
- File: `common/winding.cpp:282-293`
- Evidence:
  ```cpp
  delete[] m_Points;
  m_NumPoints = other.m_NumPoints;
  ...
  memcpy(m_Points, other.m_Points, sizeof(vec3_t) * m_NumPoints);
  ```
  If `this == &other`, the source buffer is freed before it is copied, causing use-after-free.
- Recommendation: Add a self-assignment guard and allocate the new buffer before freeing the old one.

### CQ-07. `Brink` search results are used after assertion-only guards
- Severity: Medium
- File: `hlbsp/brink.cpp:140-167, 191-215`
- Evidence:
  ```cpp
  int found;
  ...
  if (numfound == 0) { ... hlassume(false, assume_first); }
  ...
  bbrinknode_t *node = &(*b->nodes)[found];
  ```
  `hlassume` calls `Error` (which exits) only in normal builds, but `hlassert` is compiled out in release builds. If the search fails and execution continues, `found` is uninitialized and is used as an array index.
- Recommendation: Initialize `found = -1` and make the failure path unconditional (`Error` plus return), or convert the guard to a real runtime check that cannot be compiled out.

### CQ-08. `DoAllocBlock` may use uninitialized `x`/`y`
- Severity: Medium
- File: `common/bspfile.cpp:876-920`
- Evidence:
  ```cpp
  int x, y;
  ...
  if (j == w) { x = i; y = best = best2; }
  ...
  block->allocated[x + i] = best + h;
  ```
  If no candidate position fits, `x` remains uninitialized, yet the later `best + h <= BLOCK_HEIGHT` branch may still use `x` when `best` (initialized to `BLOCK_HEIGHT`) plus `h` happens to fit.
- Recommendation: Initialize `x = 0, y = 0` and track a `found` flag; only update the block when a candidate was found.

### CQ-09. `getfiledata` can compute a negative read size
- Severity: Medium
- File: `common/filelib.cpp:70-98`
- Evidence:
  ```cpp
  while ((bytesread = _read(handle, buffer, qmin(32 * 1024, buffersize - size))) > 0)
  ```
  If `size` exceeds `buffersize`, `buffersize - size` is negative and is then converted to a very large `size_t` by `qmin`, causing an oversized read.
- Recommendation: Guard the loop with `size < buffersize` and compute the remaining count as `buffersize - size` only when positive.

### CQ-10. Unbounded `sprintf` for hull file names
- Severity: Medium
- File: `hlbsp/qbsp.cpp:1762, 1768`
- Evidence:
  ```cpp
  sprintf(name, "%s.p%i", filename, i);
  sprintf(name, "%s.b%i", filename, i);
  ```
  `name` is `_MAX_PATH` bytes. A long `filename` can overflow it; nearby code already uses `safe_snprintf`.
- Recommendation: Replace both calls with `safe_snprintf(name, sizeof(name), ...)`.

### CQ-11. Unbounded `sscanf` for texlight definitions
- Severity: Medium
- File: `hlrad/qrad.cpp:575`
- Evidence:
  ```cpp
  argCnt = sscanf(scan, "%s %f %f %f %f", szTexlight, &r, &g, &b, &i);
  ```
  `scan` is up to 4096 bytes and `szTexlight` is `_MAX_PATH` bytes, with no width limit on `%s`.
- Recommendation: Use a width-limited conversion such as `%4095s` (or smaller, matching `szTexlight`).

### CQ-12. Malformed `VecAddVector` macro
- Severity: Low
- File: `common/mathlib.h:68`
- Evidence:
  ```cpp
  #define VecAddVector(a,b,c) { (c)[0]=(a)+(b)[0]; (c)[1]=(a)[(b)[1]; (c)[2]=(a)+(b)[2]; }
  ```
  The second term is missing a closing bracket: `(a)[(b)[1]` instead of `(a)+(b)[1]`. The macro is currently unused, so it does not break the build, but any future use will fail to compile (and the intended expression is almost certainly `(a)+(b)[1]`).
- Recommendation: Correct the macro to `(c)[1]=(a)+(b)[1];`.

### CQ-13. `memcpy` on non-trivially-copyable `brush_t`
- Severity: Low
- File: `hlcsg/map.cpp:37, 41, 95, 728, 1348, 1359, 1363`
- Evidence:
  ```cpp
  memcpy(newb, brush, sizeof(brush_t));
  ```
  `brush_t` contains `char* hullshapes[NUM_HULLS]` and nested structures; copying it as raw bytes performs a shallow copy of pointers and is technically undefined behavior. The compiler emits `-Wclass-memaccess` warnings for these sites.
- Recommendation: Replace raw byte copies with memberwise copy or value initialization, and document pointer ownership.

### CQ-14. Pointer relational comparison with null in `getFilePositions`
- Severity: Low
- File: `common/cmdlib.cpp:132`
- Evidence:
  ```cpp
  ptr = qmax(strrchr(path,'/'), strrchr(path,'\\'));
  ```
  `qmax` expands to a relational comparison between two pointers that may be null and non-null (or both null). Such comparisons are not defined by the C++ standard unless both point into the same array.
- Recommendation: Compare explicitly, for example `ptr = a ? (b && b > a ? b : a) : b;`.

### CQ-15. `q_filelength` uses `int` for file sizes
- Severity: Low
- File: `common/filelib.cpp:107-118`
- Evidence:
  ```cpp
  int pos;
  int end;
  ...
  return end;
  ```
  Files larger than `INT_MAX` bytes produce a negative or truncated length, which then drives undersized allocations and reads.
- Recommendation: Use `long`/`size_t` (or `std::streamoff`) and check for `ftell` failure (`-1L`).

### CQ-16. `FlipSlashes` relies on searched/replaced characters differing
- Severity: Low
- File: `common/cmdlib.cpp:153-167`
- Evidence:
  ```cpp
  while (ptr = strchr(ptr, '/'))
  {
      *ptr = SYSTEM_SLASH_CHAR;
  }
  ```
  The loop terminates only because the replacement character differs from the searched character. If the two ever become the same (for example through a future change to `SYSTEM_SLASH_CHAR`), this becomes an infinite loop. The pointer is also not advanced explicitly.
- Recommendation: Advance `ptr` after each replacement (`ptr++`) and make the termination condition independent of character inequality.

### CQ-17. Broad warning debt hides real defects
- Severity: Low
- Files: project-wide; representative categories from the build log
- Evidence: The clean build reports 1,119 warnings, including 125 sign-compare, 45 maybe-uninitialized, 43 parentheses, 10 assignment-as-truth, 6 dangling-else, 10 misleading-indentation, and 5 aggressive-loop-optimizations. The Makefile even instructs developers to ignore warnings, including uninitialized variables.
- Recommendation: Fix warnings in shared/math code first, then enable `-Werror` for at least `common/` and `mathlib.h`; remove "ignore warnings" guidance from the Makefile.

## Recommended Priorities

1. Fix CQ-01 (`Winding::resize`) and CQ-02 (texinfo conversion); both are memory-safety defects.
2. Fix `safe_snprintf`/`safe_strncat` (CQ-03, CQ-04) so the "safe" helpers actually are safe.
3. Initialize uninitialized temporaries and "best candidate" variables (CQ-05, CQ-07, CQ-08).
4. Replace remaining unbounded `sprintf`/`sscanf` calls in `hlbsp` and `hlrad` (CQ-10, CQ-11).
5. Make assertion guards real runtime checks where their results are used (CQ-07).
6. Correct the latent `VecAddVector` macro and shallow `brush_t` copies (CQ-12, CQ-13).
7. Reduce warning debt and remove ignore-warnings build guidance (CQ-17).