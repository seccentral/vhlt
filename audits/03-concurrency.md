# Concurrency and Thread Safety Audit

Scope: `common/threads.cpp`, all callers of the threading API, and shared mutable/global/static state across the VHLT tools.  
Method: static inspection of source/build definitions (no code changes).

## Summary

The codebase uses a hand-rolled thread pool (`RunThreadsOn` / `RunThreadsOnIndividual`) with a single process-global mutex (`ThreadLock`/`ThreadUnlock`). Worker functions share large amounts of global state (`g_*` arrays/counters, portal/patch structures, plane tables, texture maps, logging state). Locking is applied inconsistently: some critical sections are protected, many reads/writes of shared state are not, and several platform-specific implementations have lifecycle and reentrancy defects. The default builds do not define `ZHLT_NETVIS` or `HLCSG_FASTFIND`, but the issues below include both default and conditionally compiled paths.

## Findings

### Critical

| ID | Severity | Location | Evidence | Recommendation |
|---|---|---|---|---|
| C1 | Critical | `common/cmdlib.cpp:50-58` | POSIX `I_FloatTime()` uses `static int secbase` with unsynchronized check-then-set and no atomicity. It is called from worker paths and `threads.cpp`. | Make the base time initialization thread-safe (e.g. compute base once before threading, or use an atomic flag / `std::call_once`), or replace with a thread-safe clock (`clock_gettime`). |
| C2 | Critical | `common/threads.cpp:356-365`, `396-457`; `hlvis/vis.cpp:1798`, `2055` (ZHLT_NETVIS builds) | `threads_InitCrit()` calls `InitializeCriticalSection(&crit)` unconditionally. In NETVIS builds `vis.cpp` calls it at startup and `RunThreadsOn()` calls it again for every run, re-initializing an already initialized critical section. `threads_UninitCrit()` is likewise called by both, producing double delete. | Make initialization idempotent/reference-counted, or remove the extra calls in `vis.cpp` and let `RunThreadsOn()` own the critical section lifecycle. |
| C3 | Critical | `common/threads.cpp:597-650` (POSIX) | `threads_UninitCrit()` does `Free(my_mutex)` without `pthread_mutex_destroy()` and sets `my_mutex = NULL`; `threads_InitCrit()` allocates/initializes a new mutex each run. Repeated runs leak mutex objects and may free initialized mutex memory. `pthread_mutexattr_t` is never destroyed. | Destroy the mutex with `pthread_mutex_destroy()` before freeing, destroy the attribute, and reuse a single long-lived mutex instead of allocating/freeing per run. |
| C4 | Critical | `common/threads.cpp:399-426` (Win32) | On `CreateThread` failure, the diagnostic uses `threadhandle[i]`, which has not been assigned for the failed index (uninitialized local array). | Initialize `threadhandle[i]` (or log `hThread`/`i` only), and fail fast without reading uninitialized memory. |

### High

| ID | Severity | Location | Evidence | Recommendation |
|---|---|---|---|---|
| H1 | High | `common/threads.cpp:320-346` (Win32), `597-614` (POSIX) | `ThreadLock()` reads the shared `threaded` flag (and `my_mutex` pointer on POSIX) without synchronization while the main thread writes them during `RunThreadsOn()`. `q_entry` and `workfunction` are also published as plain globals. | Make these state variables atomic/volatile or, better, pass them through thread arguments/context instead of globals. Ensure publication happens-before thread creation (already intended) and document it. |
| H2 | High | `common/threads.cpp:252-253`, `348`, `350`, `216` | `g_numthreads`, `dispatch`, `workcount`, `q_entry`, `workfunction` are plain globals mutated by the main thread and read by workers. | Use atomic types or confine mutations to the main thread before thread creation and document happens-before. |
| H3 | High | `hlcsg/brush.cpp:176-268` (HLCSG_FASTFIND path), `:40-53` (default path) | `FindIntPlane()` reads and mutates `g_nummapplanes`/`g_mapplanes` with at best partial locking. The fast path explicitly comments `// BUG: there might be some multithread issue`. Readers iterate without a lock while another thread appends planes. | Protect all reads/writes of the plane table with `ThreadLock`, or build per-thread local plane sets and merge, or make the table append-only with atomic counters and memory barriers. |
| H4 | High | `hlcsg/textures.cpp:58-85`, `104-136` | `texmap_store`/`texmap_retrieve` and the `texmap64_*` helpers access `numtexmap`/`tex_max64` and arrays with inconsistent locking; retrievers read counters without locks. | Make counters atomic or guard every access with the same mutex; avoid mixed locked/unlocked access. |
| H5 | High | `hlvis/flow.cpp:1741-1755` (non-`HLVIS_MAXDIST_NEW`) | `MaxDistVis()` mutates `l->portals[k]->visbits`/`mightsee` and `tl->portals[...]->visbits`/`mightsee` without `ThreadLock()` (the `HLVIS_MAXDIST_NEW` path does lock). | Use the locked path consistently, or lock around all portal visbit mutations. |
| H6 | High | `common/threads.cpp:37-43` | `dispatch`, `workcount`, `oldf`, `pacifier`, `threaded`, `threadtimes[]` are plain shared globals. `GetThreadWork()` is the only synchronized accessor, but `threaded` is read in `ThreadLock()` without synchronization. | Convert `threaded` and related flags to atomic types; keep all counter access inside `ThreadLock()`. |

### Medium

| ID | Severity | Location | Evidence | Recommendation |
|---|---|---|---|---|
| M1 | Medium | `common/threads.cpp:305-318` (Win32 `ThreadSetDefault`) | `g_numthreads` clamped to a maximum of 32; `MAX_THREADS` is 64. On systems with >32 CPUs, threads are silently capped. | Raise/align the cap with `MAX_THREADS` or make it configurable. |
| M2 | Medium | `common/threads.cpp:688-695` (POSIX) | Fixed thread stack size of `0x400000` (4 MiB) via `pthread_attr_setstacksize`. Deep recursion in vis/rad can overflow; Makefile even warns stack must exceed 4 MB. | Increase or make configurable; document minimum stack requirement; consider not overriding stack size. |
| M3 | Medium | `common/threads.cpp:396-457` (Win32) | Thread handles are never `CloseHandle`d, leaking kernel handles over repeated runs. | Close each handle after `WaitForSingleObject`. |
| M4 | Medium | `common/threads.cpp:320-346` (Win32) | `ThreadLock()` recursion check uses `enter` incremented after entry; recursive locking warns but `EnterCriticalSection` is recursive, so `enter` accounting can become inconsistent with `ThreadUnlock()`. | Implement a proper recursive lock or document that recursive `ThreadLock()` is forbidden; optionally use a recursive mutex. |
| M5 | Medium | `common/log.cpp:477-483` | `PrintOnce()` uses a non-atomic `static int count` across threads; races can allow multiple prints or lost updates. | Make `count` atomic or guard with the thread mutex. |
| M6 | Medium | `common/log.cpp` (all logging) | `CompileLog`, `fatal`, and file handles are shared; worker threads call `Log`/`Verbose`/`Developer` while other threads may be writing. I/O is not synchronized. | Serialize logging with `ThreadLock` or a dedicated log mutex; avoid calling logging while holding other locks to prevent deadlocks. |
| M7 | Medium | `hlvis/vis.cpp:85`, `677` | `static int totalvis` is incremented in `LeafFlow()` (currently called sequentially, but static shared state is not thread-safe if ever parallelized). | Make it atomic or local/accumulated per thread. |
| M8 | Medium | `hlrad/vismatrixutil.cpp` and `hlrad` modules | Many globals (`g_transfer_data_bytes`, `g_total_transfer`, patch arrays) are mutated by workers; some are locked, others rely on per-patch ownership. Inconsistent locking makes races likely if work partitioning changes. | Audit each worker for shared writes; convert shared counters to atomics; document which fields are thread-private. |
| M9 | Medium | `common/threads.cpp:628-644` (POSIX) | `threads_InitCrit()` checks `if (!my_mutex)` without synchronization; if called concurrently it could allocate two mutexes (one leaked). | Initialize once in main thread before any worker thread, or guard with a once-flag. |

### Low

| ID | Severity | Location | Evidence | Recommendation |
|---|---|---|---|---|
| L1 | Low | `hlbsp/solidbsp.cpp:33-51` | `g_numProcessed`/`g_numReported` static progress counters are unprotected. | Make atomic or keep single-threaded only. |
| L2 | Low | `hlbsp/brink.cpp:732-733` | Function-local `static int randcounter` is not thread-safe if the containing function is ever called concurrently. | Make it thread-local or atomic. |
| L3 | Low | `common/threads.cpp:743-813` (SINGLE_THREADED) | Single-threaded build defines no-op locks and runs `func(0)` only; `GetThreadWork`/dispatch semantics differ from threaded builds. | Keep tests covering both builds; avoid assuming threaded behavior in single-threaded mode. |
| L4 | Low | `common/threads.cpp:309-317` (Win32) | If `g_numthreads == -1` and `ThreadSetDefault()` is not called, `RunThreadsOn()` loops zero times and work is never executed. | Validate/initialize `g_numthreads` before running threads. |
| L5 | Low | `hlvis/vis.cpp` NETVIS client loops | Client loops busy-wait on globals (`g_clientid`, `g_bsp_downloaded`, etc.) with `NetvisSleep`; correctness depends on memory visibility. | Use atomics/volatile for flags shared with network threads, or proper synchronization. |

## Notes

- The threading API assumes all shared mutable state is protected manually by callers. There is no RAII lock guard; every `ThreadLock()` must be paired with `ThreadUnlock()`, including on early returns. Several functions (e.g. `FindIntPlane` paths) have multiple unlock sites, increasing the risk of imbalance.
- `ThreadLock()` is not recursive on POSIX (default `pthread_mutex_t`). Any nested locking will deadlock on POSIX even if it appears to work on Win32.
- Detached threads are not used; all `pthread_create`/`CreateThread` handles are joined/waited. The main lifecycle issue is handle/resource leakage and double init, not detached execution.