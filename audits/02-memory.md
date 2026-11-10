# Memory Management and Lifetime Audit

Scope: `common/`, `hlcsg/`, `hlbsp/`, `hlvis/`, `hlrad/`, `ripent/`, `template/` in `<repo>`.

## Summary

The codebase uses a mixture of C++ `new`/`delete`, C `malloc`/`calloc`/`realloc`/`free`, Win32 `GlobalAlloc`/`GlobalLock` (via `AllocBlock`/`FreeBlock`), and raw fixed-size global buffers. Most allocation families are paired correctly, but several lifetime defects exist: an incorrect `memcpy` size in `Winding::resize`, uninitialized capacity fields in several `Winding` constructors, unchecked/leaky allocations, unsafe compatibility macros that turn bounded string calls into unbounded ones, and a number of compiler-reported uninitialized reads. The most serious issue is heap corruption in `Winding::resize`, which is reachable whenever a winding grows past its initial capacity.

Overall risk: **Medium-High** for long-running/complex compiles; **Low** for ordinary small maps, but undefined behavior is present.

## Method

- Enumerated source files in the requested directories.
- Searched for allocation APIs (`new`, `delete`, `malloc`, `calloc`, `realloc`, `free`, `Alloc`, `AllocBlock`, `Free`, `FreeBlock`), raw global buffers, and string operations.
- Reviewed the central allocator (`common/blockmem.cpp`), `Winding` (`common/winding.cpp`), reference-counting templates (`template/`), and tool-specific allocation/free paths.
- Built the project with MinGW `-Wall` and collected uninitialized-read and signedness warnings.
- No code was changed.

## Findings

### F1. `Winding::resize` copies the wrong number of bytes
- Severity: Critical
- File: `common/winding.cpp:1274-1284`
- Description: `memcpy(newpoints, m_Points, m_NumPoints)` passes an element count where a byte count is required. `m_NumPoints` is the number of `vec3_t` elements, but `memcpy`'s third argument is bytes. Only a fraction of the old array is copied; the remainder of `newpoints` is uninitialized, and subsequent use reads uninitialized memory. The assignment to `m_NumPoints` is also performed before the copy, further distorting the count when shrinking.
- Evidence:
  ```cpp
  vec3_t* newpoints = new vec3_t[newsize];
  m_NumPoints = qmin(newsize, m_NumPoints);
  memcpy(newpoints, m_Points, m_NumPoints);
  delete[] m_Points;
  m_Points = newpoints;
  m_MaxPoints = newsize;
  ```
- Recommendation: Save the old count before resizing, then copy `oldNumPoints * sizeof(vec3_t)` bytes. For example:
  ```cpp
  UINT32 oldNumPoints = m_NumPoints;
  m_NumPoints = qmin(newsize, oldNumPoints);
  memcpy(newpoints, m_Points, oldNumPoints * sizeof(vec3_t));
  ```

### F2. Several `Winding` constructors leave `m_MaxPoints` uninitialized
- Severity: High
- File: `common/winding.cpp:319` (`initFromPlane`), `common/winding.cpp:383` (`Winding(const vec3_t, vec_t)`), `common/winding.cpp:388` (`Winding(const dface_t&)`), `common/winding.cpp:425` (`Winding(const dplane_t&)`)
- Description: These constructors allocate `m_Points` and set `m_NumPoints`, but never initialize `m_MaxPoints`. `addPoint`/`insertPoint`/`resize` rely on `m_MaxPoints` to decide when growth is required. An uninitialized `m_MaxPoints` can make the class skip required reallocation and write past the end of `m_Points`, or trigger reallocation with a garbage capacity.
- Evidence:
  ```cpp
  m_NumPoints = 4;
  m_Points = new vec3_t[m_NumPoints];
  // m_MaxPoints never assigned
  ```
- Recommendation: Initialize `m_MaxPoints` in every constructor and in `initFromPlane`, using the allocated capacity (rounded up to a multiple of 4 where appropriate).

### F3. `Winding::operator=` is not self-assignment safe
- Severity: Medium
- File: `common/winding.cpp:282-293`
- Description: `operator=` deletes `m_Points` and then reads from `other.m_Points`. If `this == &other`, it frees the source data before copying it, causing use-after-free and heap corruption.
- Evidence:
  ```cpp
  Winding& Winding::operator=(const Winding& other)
  {
      delete[] m_Points;
      m_NumPoints = other.m_NumPoints;
      ...
      memcpy(m_Points, other.m_Points, sizeof(vec3_t) * m_NumPoints);
  }
  ```
- Recommendation: Add a self-assignment guard (`if (this == &other) return *this;`) and allocate the new buffer before freeing the old one.

### F4. Win32 allocator families are distinct and must never be mixed
- Severity: Medium
- File: `common/blockmem.cpp:17-75` (Win32 `AllocBlock`/`FreeBlock` vs `Alloc`/`Free`)
- Description: On Win32, `AllocBlock` uses `GlobalAlloc`/`GlobalLock` and `FreeBlock` uses `GlobalHandle`/`GlobalUnlock`/`GlobalFree`, while `Alloc` uses `calloc` and `Free` uses `free`. Passing a pointer from one family to the other's free function is undefined behavior. The codebase generally pairs them, but the two families are easy to confuse, and `FreeBlock(NULL)` calls `GlobalHandle(NULL)`, which may produce warnings or invalid-handle handling.
- Evidence:
  ```cpp
  void* AllocBlock(...) { h = GlobalAlloc(GMEM_FIXED | GMEM_ZEROINIT, size); ... pointer = GlobalLock(h); }
  bool  Free(void* pointer) { HeapCheck(); free(pointer); return true; }
  ```
- Recommendation: Make the two families unmistakably distinct (for example rename to `AllocGlobal`/`FreeGlobal`) and assert pairing in debug builds; check for NULL before calling `GlobalHandle`.

### F5. `strcpy_s`/`sprintf_s` are redefined as unbounded functions
- Severity: High
- File: `common/cmdlib.h:607-616`
- Description: On MinGW and older MSVC, `strcpy_s` and `sprintf_s` are undefined and then redefined to plain `strcpy`/`sprintf`. Call sites that appear to be bounded therefore lose all bounds checking on those builds, while `_MSC_VER >= 1400` builds disable the corresponding deprecation warnings. This affects many fixed-size buffers throughout the tools.
- Evidence:
  ```cpp
  #define strcpy_s strcpy //--vluzacn
  #define sprintf_s sprintf //--vluzacn
  ```
- Recommendation: Provide real bounded implementations or remove these macros and fix call sites; do not disable deprecation warnings globally.

### F6. Fixed-size global buffers with unbounded string operations
- Severity: Medium
- Files: `common/cmdlinecfg.cpp:87,213-223,272,292-327`; `common/log.cpp:925,931`; `hlrad/loadtextures.cpp:234,255,301,307`; `hlrad/qrad.cpp:5790`; `ripent/ripent.cpp:567,618,740`
- Description: Numerous `strcat`, `strcpy`, and `sprintf` calls write into fixed buffers (`cmdline`, `filepath`, `words`, `token`, `tex->name`, `g_source`, `info[...].name`) without verifying remaining capacity. Because F5 removes bounds checking on common toolchains, overlong input can overflow these buffers.
- Evidence:
  ```cpp
  strcat(cmdline, params);
  strcpy(g_source, mapname_from_arg);
  strcpy(info[i].name, ofs >= 0 ? ... : "\rTEXTUREMISSING");
  ```
- Recommendation: Replace unbounded calls with capacity-aware helpers (`safe_strncpy`, `safe_snprintf`) and pass the destination size; validate input lengths before copying.

### F7. `hlrad` lightmap code uses uninitialized temporaries on some paths
- Severity: Medium
- File: `hlrad/lightmap.cpp:183,185,1891-1892,4447,5771,5811`
- Description: Compiler analysis reports `edge`, `edgenext`, `bestpos[0..1]`, `bests`, `bestt`, `add[0..2]`, `lastoffset`, and `lastoffset2` may be used uninitialized. These are in lighting/geometry paths and can propagate indeterminate values into light calculations or out-of-bounds indexing.
- Evidence:
  ```
  hlrad/lightmap.cpp:183:46: warning: 'edge' may be used uninitialized
  hlrad/lightmap.cpp:185:54: warning: 'edgenext' may be used uninitialized
  hlrad/lightmap.cpp:1891:16: warning: 'bestpos[0]' may be used uninitialized
  ```
- Recommendation: Initialize all loop accumulators and "best" variables before use, or restructure the loops so every used variable is assigned on all paths; add assertions for the expected counts.

### F8. `hlrad/qrad.cpp` uses uninitialized accumulators
- Severity: Medium
- File: `hlrad/qrad.cpp:738,2364`
- Description: `bestpoint[0..1]` and `light_origin[0..1]` are reported as possibly uninitialized. These feed patch/light origin calculations.
- Evidence:
  ```
  hlrad/qrad.cpp:738:16: warning: 'bestpoint[0]' may be used uninitialized
  hlrad/qrad.cpp:2364:21: warning: 'light_origin[0]' may be used uninitialized
  ```
- Recommendation: Initialize these arrays at declaration and ensure assignment occurs before use.

### F9. `ripent` may use uninitialized backup variables
- Severity: Low
- File: `ripent/ripent.cpp:801-802`
- Description: `bak_entdatasize` and `bak_dentdata` are reported as possibly uninitialized. They are only used under `RIPENT_TEXTURE`/`g_parse`, but the compiler cannot prove the guard matches initialization.
- Evidence:
  ```
  ripent/ripent.cpp:801:32: warning: 'bak_entdatasize' may be used uninitialized
  ripent/ripent.cpp:802:30: warning: 'bak_dentdata' may be used uninitialized
  ```
- Recommendation: Initialize both to zero/NULL at declaration.

### F10. Many allocations are not null-checked
- Severity: Medium
- Files: `hlrad/lightmap.cpp:75,77,571,852-873,1423,1556,1723,1727,3444-3484,3730-3803,5092,6134,6140,6149-6156,7407,7688,7799,7807,8438,8730-8736`; `hlrad/loadtextures.cpp:21,72,134,236,279,319,355,605,626,634,886,1149,1153,1343,1397`; `hlvis/vis.cpp:245,604,1138-1145,1928-1929,2011`; `hlrad/qrad.cpp:1408,1451,1487,1550,5772`; `hlrad/sparse.cpp:135,177,193,218,551,646`; `hlrad/trace.cpp:302,747,774-776`; `hlrad/transparency.cpp:68,117,171,348`; `hlbsp/qbsp.cpp:474` (faces); `hlcsg/*` (`malloc`/`realloc` sites)
- Description: Numerous `malloc`, `calloc`, `realloc`, and `new` results are used without checking for `NULL`. On allocation failure the tools dereference null pointers instead of failing gracefully. Some sites use `hlassume`, but many do not.
- Evidence:
  ```cpp
  t = (intersecttest_t *)malloc(sizeof(intersecttest_t));
  t->clipplanes = (dplane_t *)malloc(f->numedges * sizeof(dplane_t));
  ```
- Recommendation: Centralize allocation through helpers that check for failure and call `Error`/`hlassume`; check `realloc` results before overwriting the original pointer.

### F11. `realloc` results are used without preserving the original pointer on failure
- Severity: Medium
- Files: `hlrad/lerp.cpp:2950,2955,3014,3142`; `hlrad/sparse.cpp:551`; `hlrad/transparency.cpp:68,117,348`; `hlrad/vismatrix.cpp:207`; `hlcsg/textures.cpp:427,909`; `hlrad/qrad.cpp:2074`
- Description: Several `realloc` calls assign directly to the pointer being resized. If `realloc` returns `NULL`, the original allocation is leaked and the pointer becomes null; subsequent code may dereference it. In `hlrad/lerp.cpp` the result is assigned, but failure is only detected by `hlassume` after the old pointer may already be gone.
- Evidence:
  ```cpp
  trian->walls = (lerpWall_t*)realloc(trian->walls, sizeof(lerpWall_t) * (...));
  hlassume(trian->walls != NULL, assume_NoMemory);
  ```
- Recommendation: Use a temporary pointer, check it, and only then replace the original; free the original only if replacement succeeds.

### F12. Known small leaks in BSP/hlcsg paths
- Severity: Low
- Files: `hlbsp/outside.cpp:298-301`; `hlcsg/brush.cpp:322,1024,1097,1363`
- Description: `outside.cpp` explicitly notes leaked memory when a node does not touch any interior leaf. `hlcsg/brush.cpp` marks several `bface_t` allocations with "TODO: This leaks". These are not correctness issues for a single process run but prevent clean repeated/in-process use and confuse leak detectors.
- Evidence:
  ```cpp
  // Here leaks memory. --vluzacn
  bface_t* new_face = (bface_t*)Alloc(sizeof(bface_t)); // TODO: This leaks
  ```
- Recommendation: Track ownership of these objects and free them when their owning brush/node is destroyed, or document them as intentional persistent allocations.

### F13. Reference-counting templates have lifetime bugs
- Severity: Low
- Files: `template/ReferencePtr.h:118-133`; `template/ReferenceArray.h` (same pattern); `template/ReferenceCounter.h:96-111,126-130`
- Description: These templates are not used by the main tools, but contain defects: `operator=(DATA_T*)` has convoluted ownership transfer logic; `ReferenceCounter::add`/`sub` (single-threaded) return the post-update value instead of the original value documented by the interface; `read()` in multithreaded Win32 mode reads `m_atom` without an interlocked operation. `Release()` assumes `m_pData` is non-null.
- Evidence:
  ```cpp
  inline int ReferenceCounter::add(int amt) { m_atom += amt; return m_atom; }
  inline int ReferenceCounter::read() const { return m_atom; } // Win32 multithreaded path
  ```
- Recommendation: Do not use these templates without review; if retained, fix return-value semantics, make `read()` atomic, and simplify assignment ownership.

### F14. `hlvis` `Zones` and `Winding` use unchecked `new[]`
- Severity: Low
- File: `hlvis/zones.h:45-49`
- Description: `Zones` allocates `m_ZoneVisMatrix`, `m_ZonePtrs`, and `m_ZoneBounds` with `new[]` without handling `std::bad_alloc`. On failure this throws rather than producing a controlled error, unlike the `hlassume`-based allocation paths.
- Evidence:
  ```cpp
  m_ZoneVisMatrix = new bool[m_ZoneCount * m_ZoneCount];
  m_ZonePtrs = new bool*[m_ZoneCount];
  m_ZoneBounds = new BoundingBox[m_ZoneCount];
  ```
- Recommendation: Wrap allocations in try/catch or use checked allocation helpers and call `Error` on failure.

### F15. Global/static buffers are not zero-initialized or bounds-checked at runtime
- Severity: Low
- Files: `common/bspfile.cpp:30-95` (large global arrays such as `g_dmodels`, `g_dleafs`, `g_dplanes`); `hlvis/vis.cpp:1138-1145`; `hlrad/qrad.cpp` global patch/light arrays
- Description: Large global buffers are zero-initialized by the linker, but sizes such as `g_max_map_miptex` and `g_max_map_lightdata` are runtime-configurable. Accesses rely on `hlassume` checks that are macro/conditional and may be disabled. Missing null/bounds checks can lead to out-of-range access when limits are raised.
- Evidence:
  ```cpp
  g_dtexdata = (byte*)AllocBlock(g_max_map_miptex);
  hlassume(g_dtexdata != NULL, assume_NoMemory);
  ```
- Recommendation: Keep `hlassume` checks enabled in all builds, validate configured limits against compiled maxima, and centralize array indexing.

## Notes / Limitations

- This audit was static; no dynamic leak detector (Valgrind/ASan) was available on the build toolchain used.
- The project defines `NDEBUG`, so `assert`-based checks are compiled out; `hlassume` remains the effective runtime check.
- Many findings are latent and only manifest on allocation failure, malformed input, or unusual compile settings.
