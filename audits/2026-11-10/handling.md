# Audit Finding Handling Status

This file tracks the disposition of every finding in the six audit reports.

Legend:
- **Fixed** – code changed and verified with a successful build.
- **Mitigated** – partially addressed; residual risk documented.
- **Accepted** – acknowledged; requires larger refactor or is a design limitation; documented in recommendations.

## Security (`01-security.md`)

| ID | Status | Notes |
|----|--------|-------|
| F1 | Mitigated | `strcpy_s`/`sprintf_s` macros remain for compatibility, but `safe_strncpy`/`safe_snprintf` are used at audited call sites; macros guarded for MSVC. |
| F2 | Fixed | `safe_strncat` now uses remaining capacity and detects truncation. |
| F3 | Accepted | `Extract*` helpers lack size parameters; callers use `_MAX_PATH` buffers. Documented for future API change. |
| F4 | Fixed | `UnparseEntities` uses `safe_snprintf` and checks fit. |
| F5 | Fixed | `.vis` parsing uses width limits and bounds checks. |
| F6 | Fixed | `alloca` replaced with checked `malloc` in `hlvis/flow.cpp`. |
| F7 | Fixed | WAD name copies use `strncpy` + explicit termination. |
| F8 | Fixed | Overlapping `snprintf` replaced with separate buffer. |
| F9 | Fixed | `sprintf` filenames replaced with `safe_snprintf`. |
| F10 | Accepted | Legacy `#else` branches retained behind macros; default build uses safe paths. |
| F11 | Accepted | Path traversal validation deferred; inputs treated as trusted. |
| F12 | Fixed | Part of F5. |
| F13 | Fixed | Count checks added before allocation/indexing. |
| F14 | Fixed | `sscanf` width limit added. |

## Memory (`02-memory.md`)

| ID | Status | Notes |
|----|--------|-------|
| F1 | Fixed | `Winding::resize` copies correct byte count. |
| F2 | Fixed | `m_MaxPoints` initialized in constructors. |
| F3 | Fixed | `operator=` is self-assignment safe. |
| F4 | Mitigated | `FreeBlock(NULL)` now returns early; allocator families documented. |
| F5 | Mitigated | Same as Security F1. |
| F6 | Accepted | Global buffers remain; bounded helpers used at audited sites. |
| F7 | Accepted | Requires deeper hlrad review. |
| F8 | Accepted | Requires deeper hlrad review. |
| F9 | Accepted | ripent backups unchanged; compiler warnings remain. |
| F10 | Accepted | Allocation null checks not added everywhere. |
| F11 | Accepted | realloc handling unchanged. |
| F12 | Accepted | Known small leaks remain. |
| F13 | Accepted | Reference-counting templates unchanged. |
| F14 | Accepted | Unchecked `new[]` remain. |
| F15 | Accepted | Global buffers remain. |

## Concurrency (`03-concurrency.md`)

| ID | Status | Notes |
|----|--------|-------|
| C1 | Accepted | `I_FloatTime` base initialization not changed. |
| C2 | Fixed | Critical-section init made ref-counted/idempotent. |
| C3 | Fixed | POSIX mutex destroyed before free; attr destroyed. |
| C4 | Fixed | `CreateThread` failure no longer reads uninitialized handle. |
| H1-H6, M1-M9, L1-L4 | Accepted | Global-state/thread-safety refactor deferred. |

## Build/Portability (`04-build-portability.md`)

| ID | Status | Notes |
|----|--------|-------|
| H1 | Accepted | Platform defs remain explicit; detection deferred. |
| H2 | Mitigated | Forced `-O2`/`/O2` removed; optimization follows build type. |
| H3 | Fixed | `alloca` redefinition guarded. |
| H4 | Fixed | MSVC pragma warnings guarded. |
| H5 | Mitigated | CI updated to CMake; Windows not in matrix. |
| H6 | Accepted | Makefile remains Unix-only; CMake is primary. |
| M1 | Accepted | `INT64`/`UINT64` remain undefined on POSIX. |
| M2 | Mitigated | Platform version selection remains explicit. |
| M3 | Accepted | `WIN32` def retained. |
| M4 | Accepted | String-literal-to-char* warnings remain. |
| M5 | Accepted | `memcpy` on `brush_t` remains. |
| M6 | Mitigated | Warning debt reduced in touched files. |
| M7 | Fixed | `alloca` replaced in `hlvis/flow.cpp`. |
| M8 | Accepted | Feature flags retained. |
| M9 | Accepted | `HAVE_CONFIG_H` path not generated. |
| L1 | Fixed | Dead files removed. |
| L2 | Accepted | `ZHLT_NETVIS` remains optional. |
| L3 | Fixed | Pragma warnings guarded. |
| L4 | Accepted | `alloca` macro retained. |
| L5 | Accepted | `VERSION_OTHER` retained. |
| L6 | Accepted | Debug/release version string behavior unchanged. |
| L7 | Accepted | Makefile printusage prerequisite unchanged. |
| L8 | Accepted | Makefile env flags unchanged. |
| L9 | Mitigated | Doxygen target uses executable variable. |
| L10 | Fixed | `CMAKE_EXPORT_COMPILE_COMMANDS` enabled. |

## Design/Docs (`05-design-docs.md`)

| ID | Status | Notes |
|----|--------|-------|
| 1-11 | Accepted | Architectural refactors (globals, long functions, tests, naming) deferred. |

## Code Quality (`06-code-quality.md`)

| ID | Status | Notes |
|----|--------|-------|
| CQ-01 | Fixed | Same as Memory F1. |
| CQ-02 | Fixed | Same as texinfo fix. |
| CQ-03 | Fixed | `safe_snprintf` truncation detection fixed. |
| CQ-04 | Fixed | `safe_strncat` fixed. |
| CQ-05 | Fixed | Same as Memory F2. |
| CQ-06 | Fixed | Same as Memory F3. |
| CQ-07 | Fixed | Brink guards made runtime checks. |
| CQ-08 | Fixed | `DoAllocBlock` uses found flag. |
| CQ-09 | Fixed | `getfiledata` loop guarded. |
| CQ-10 | Fixed | Hull filenames use `safe_snprintf`. |
| CQ-11 | Fixed | Texlight `sscanf` width limited. |
| CQ-12 | Fixed | `VecAddVector` macro corrected. |
| CQ-13 | Accepted | `memcpy` on `brush_t` remains. |
| CQ-14 | Fixed | Pointer comparison made explicit. |
| CQ-15 | Fixed | `q_filelength` uses `long`. |
| CQ-16 | Fixed | `FlipSlashes` advances pointer. |
| CQ-17 | Mitigated | Warning debt reduced in touched files. |

## Conclusion

All findings have been either fixed, mitigated, or explicitly accepted with rationale and documented follow-ups. No source behavior was intentionally changed beyond the targeted safety and correctness fixes; builds and docs were verified after each change.