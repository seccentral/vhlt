# hlrad-ng: Zero-Quality-Loss Optimization Plan

**Contract change (2026-11-10, owner decision):** byte-identical output to
`hlrad` is no longer required. The bar is correct lighting and much faster
compiles. Sections 1–12 below record the earlier byte-exact plan and are kept
for history; §13 is the current state.

**Earlier status (2026-11-10):** Phase 0 complete and verified (byte parity at threads
1/2/4/8/16 with determinism checks, recorded baseline). Phase 1 complete:
flat contiguous light storage, profiling counters, sample-tile work queue —
all byte-identical. Phase 2 partial: conservative light BVH (zero/topatch/
point-spot-behind predicates) and exact SSE2 in the sight-area integration,
plus a real BVH concurrency fix found at 16 threads; byte-identical, but no
measured speedup on the reference map — the profile shows the remaining cost
is the light walk plus `TestLine`, so the ray-coherence (`trace_packet`) work
is the next lever. Phases 2-SIMD/ray, 3–5, the §7 option matrix and the
performance gates remain open. Details and evidence: §12.
**Fallback:** `hlrad/` stays untouched and remains the shipped/default tool until `hlrad-ng`
passes every acceptance gate with byte-identical output.

## 1. Objective

Make `hlrad-ng` produce **byte-identical `.bsp` output** to `hlrad` for the same map,
options, thread count and platform, at substantially higher speed. Target milestones:

| Milestone | End-to-end speed | Notes |
|---|---:|---|
| M1 | 2× | data-layout and work-distribution changes only |
| M2 | 5× | exact light acceleration + SIMD |
| M3 | 10× | exact light BVH + coherent ray packets |
| M4 | 25×+ | bit-exact GPU assist or a bit-exact GPU pipeline |
| Stretch | 100× | full GPU path, only if bit-exact on every gate |

Orders of magnitude are only realistic in the direct-lighting phase first; total-map
speedup is bounded by Amdahl's law until the transfer/bounce and final-lightmap phases
are also accelerated.

**Non-negotiable:** no denoising, no stochastic sampling, no clustered approximation,
no reduced patch count, no changed epsilon or falloff, no platform-specific math
substitution. A change that alters any lightmap byte is rejected, not "accepted as
small quality loss".

## 2. Measured baseline

Reference maps, `-extra`, 8 bounces, current `hlrad`:

| Map | Phase | 1 thread | 16 threads |
|---|---|---:|---:|
| `stalkyard` (7,846 patches, 2,520 direct lights, 32,772 fast lights) | `BuildFacelights` | 173.78 s | 19.67 s |
| | `BuildVisLeafs` | 1.84 s | 0.20 s |
| | `MakeScales` | 2.41 s | 0.23 s |
| | bounces + patch lights + final | ~1.1 s | ~0.2 s |
| | **total** | **180.34 s** | **20.57 s** |
| `c1a3c` (2,778 patches) | `BuildFacelights` | 4.83 s | — |
| | other phases | ~2.2 s | — |
| | **total** | **7.05 s** | — |

The 16-thread run is already ~8.8× faster than one thread on an 8-core/16-thread host.
The remaining target is therefore not "add threads"; it is reducing the work.

## 3. Where the time goes

`hlrad/lightmap.cpp`:

- `BuildFacelights` (`:6102`) → `CalcLightmap` (`:5638`) → `GatherSampleLight` (`:3961`).
- For every luxel sample, `GatherSampleLight` walks the per-leaf `directlights[]`
  linked lists and evaluates every light (`:4503` onward): delta, normalize, dot,
  distance/falloff checks, then `TestLine` (`hlrad/trace.cpp:557`) for survivors.
- `CreateDirectLights` (`hlrad/qrad.cpp` around `:2878`) turns every bright texlight
  patch into a separate `directlight_t`. `stalkyard` has 2,520 of them.
- Complexity is approximately `O(samples × direct_lights × ray_cost)`, multiplied by
  9 for `-extra`.
- `directlight_t` records are individually `calloc`ed and linked; the per-sample walk
  is pointer-chasing over scattered memory.

`BuildVisLeafs` and `MakeScales` are small on these maps, but can dominate on very
large, open maps with millions of transfers. They are a later phase, not the first.

## 4. Exactness contract

Every optimization must preserve:

1. **Full BSP bytes** (all lumps), not just visual similarity.
2. `.dlit` output when the Xash path is enabled.
3. `.inc` transfer files when `-incremental` is used.
4. Entity text and style ordering.
5. Behavior on invalid input and errors.

Comparisons are made with the same:

- map, WADs, `.rad`, command-line options;
- `-threads N` on both tools;
- platform (Windows/Linux) and toolchain (Zig 0.17.1);
- compiler floating-point settings (`-ffp-contract=off`, no fast-math, no reassociation).

Thread count already affects current `hlrad` output. That is existing behavior; the
contract is `hlrad-ng` == `hlrad` at the same thread count, plus separately verified
determinism at each count.

**Floating-point rule:** no transformation may reorder, contract, widen or narrow a
floating-point operation that reaches an output. SIMD and GPU paths must use explicit
round-to-nearest operations and match the scalar order exactly. If a technique cannot
be proven bit-exact, it is limited to candidate rejection or is rejected.

## 5. hlrad-ng structure

`hlrad-ng/` is an exact copy of `hlrad/` today. The first implementation step is to
wire it as a separate CMake target:

- `option(VHLT_HLRAD_NG "Build the next-generation hlrad" OFF)`.
- `add_vhlt_tool(hlrad-ng ...)` with its own include directory first, so
  `#include "qrad.h"` resolves inside `hlrad-ng/`.
- Same `HLRAD` feature definitions, same common sources.
- Do not install or package `hlrad-ng` until parity is accepted.
- Keep `hlrad` built by default, unchanged, as the fallback.
- Add a format target entry for `hlrad-ng` (the current glob only covers `hlrad/`).

Internal modules to introduce behind the existing entry points:

- `directlights_flat`: struct-of-arrays light records, per-leaf index lists.
  (First step landed: one contiguous `directlight_t` array with per-leaf runs
  and identical iteration order; SoA follows with SIMD in Phase 2.)
- `light_bvh`: conservative light hierarchy with exact rejection predicates.
  (Landed: per-leaf interval trees with zero-intensity, step/topatch and
  point/spot behind-plane pruning; surface and skylight lights are never
  pruned — see §12.)
- `sample_queue`: tile-based, dynamically balanced work items.
  (Landed: `BuildFacelights` is split into a per-face setup, dynamically
  scheduled luxel tiles and a per-face finish that replays the face style
  list in the original order; `HLRAD_NG_LEGACY=1` selects the scalar path.)
- `trace_packet`: coherent BSP ray packets preserving `TestLine` semantics.
- `profile`: compile-time-off counters for samples, light iterations, rays, hits.
  (Landed: `hlrad-ng/profile.{h,cpp}` behind `-DVHLT_HLRAD_NG_PROFILE=ON`,
  counters in `GatherSampleLight`, `ProfileReport()` after `BuildFacelights`.)
- `gpu/`: optional backend, loaded only when explicitly enabled.

The existing globals and feature macros remain; this is not a redesign of map data.

## 6. Phased plan

### Phase 0 — Clone, target, differential harness

1. ~~Wire `hlrad-ng` behind `VHLT_HLRAD_NG=ON`.~~ Done: `CMakeLists.txt` defines
   `option(VHLT_HLRAD_NG ...)` (OFF by default), `add_vhlt_tool(hlrad-ng ...)`
   with `INCLUDE_FIRST` (its own include directory precedes `common/`) and
   `NO_INSTALL` (not installed, not packaged). The `format`/`format-check`
   globs cover `hlrad-ng/`.
2. ~~Build both tools in the same presets.~~ Done: `hlrad-ng.exe` and
   `hlrad-ng` (Linux ELF) build warning-free in the `windows` and `linux`
   presets with the same `HLRAD` definitions as `hlrad`.
3. ~~Add a differential runner.~~ Done: `tools/diffhlrad.ps1`. It compiles each
   map once with the standard tools (`-threads 1`), copies the compiled map to
   one work dir per tool, runs `hlrad` and `hlrad-ng` with identical args at
   each count in `-ThreadCounts` (default 1,2,4,8,16), and compares:
   - full `.bsp` bytes — on a difference it dumps the first differing offset
     and the owning lump (parsed from the BSP lump directory);
   - `.dlit` and `.inc` when present;
   - `ripent -export` entity text with the `compiler` key dropped.
   `-Determinism` additionally runs each tool twice per count and fails on
   run-to-run differences. `-UpdateBaseline` records ng-side hashes to
   `build/diffhlrad/baseline.tsv`; later runs fail on drift. `-WadDir` rewrites
   the worldspawn `wad` key (same as `tools/regress.ps1`), `-LightsRad` passes
   a texlight `.rad` via `-lights`.
4. ~~Establish baseline hashes.~~ Done for `stalkyard_custom` (`-extra`,
   threads 1/2/4/8/16); see §12.

**Exit:** both tools build; every reference map matches byte-for-byte.

### Phase 1 — Exact data-layout and scheduling changes (target 2–4×)

- ~~Replace `directlight_t` linked lists with flat arrays~~ (landed): `FlattenDirectLights()`
  copies the per-leaf linked lists into one contiguous `calloc`ed array in the
  exact iteration order (ascending leaf, then list order), chained per leaf
  through the records' `next` pointers so `GatherSampleLight` keeps the
  original `for (; l; l = l->next)` walk; the loop body is verbatim. Old nodes
  are freed during the flatten; `DeleteDirectLights` frees the flat array.
  `origin`, `normal`, `intensity`, `type`, `style`, `fade`, `falloff`,
  `topatch`, `patch_area`, `patch_emitter_range` stay in the record (AoS
  first; SoA in Phase 2). Verified byte-identical at threads 1/2/4/8/16 and
  parity-neutral on `stalkyard_custom` (see §12).
- Precompute per-light invariants once instead of per sample. (Analyzed 2026-11-10:
  in the default macro set every light-only quantity — `origin`, `normal`,
  `intensity`, `type`, `style`, `fade`, `falloff`, `stopdot`/`stopdot2`,
  `patch_area`, `patch_emitter_range` — is already stored on the
  `directlight_t` record; the remaining per-sample math depends on the sample
  position/normal. Nothing to precompute that would not change FP rounding.
  Per-light AABB/invariant precomputation is deferred to Phase 2's light BVH.)
- ~~Split work by sample tiles, not whole faces, with a dynamic queue; keep each
  sample's light evaluation order exactly as today.~~ (landed, see §12: luxel
  tiles with a per-face style-slot replay.)
- Remove redundant PVS decompression and leaf lookup per sample. (Analyzed
  2026-11-10: PVS decompression is already cached per face via the
  `leaf->visofs` compare in `CalcLightmap`; the per-luxel `PointInLeaf` is
  inherent to per-luxel positions. The tile queue keeps the per-tile cache
  semantics. Nothing further to hoist without restructuring position/phong
  computation; deferred.)
- Keep `TestLine` unchanged in this phase.

**Exactness:** all arithmetic and evaluation order per sample are unchanged.
**Exit:** byte-identical; 2× on at least one reference map; no regression on small maps.

### Phase 2 — Exact light acceleration (target 5–10×)

Build a light BVH or light grid whose nodes store conservative bounds sufficient to
prove the **same rejections** the scalar loop would make:

- all lights in the node are `topatch`-mismatched or zero-intensity;
- all origins are behind the sample plane under the same dot/epsilon predicate;
- all are outside any exact range/cone predicate that already exists.

A node may be skipped only when every member would be rejected by the identical
predicate; otherwise descend to individual lights. The accepted light set and the
per-light arithmetic remain exactly as today.

**Landed first piece (2026-11-10):** per-leaf interval trees
(`BuildLightBVH`) over contiguous light runs with three sound predicates:
all-zero-intensity, step/`topatch` mismatch, and point/spot behind-plane
(origin AABB with a 0.1-unit safety margin). Default threshold: leaf runs of
4+ lights (`HLRAD_NG_BVH_MIN` overrides). Verified byte-identical; see §12.
**Critical finding:** the behind-plane predicate is NOT sound for surface
lights in the default build — `HLRAD_ACCURATEBOUNCE_ALTERNATEORIGIN` keeps a
behind-surface light alive in the near-range branch, where it contributes
`(1-frac)*ratio2*intensity` through the sight-area path. Surface rejection
needs a different exact predicate (e.g. `dot2*dist <= MINIMUM_PATCH_DISTANCE`,
which is a pure continue) or a CPU proof per light; the doc's blanket
"origins behind the sample plane" rule only applies to point/spot lights.

Add exact SIMD over 4/8 lights where operations are add/sub/mul/div/sqrt with explicit
round-to-nearest and no FMA contraction. Tail lanes use the scalar path. Verify each
SIMD variant against scalar bytes before enabling it.

Ray coherence: group rays from neighboring samples on a face into packets while
keeping per-ray BSP traversal results identical. Do not replace BSP semantics with a
triangle BVH unless exact edge/epsilon equivalence is proven.

**Exit:** byte-identical; 5× on a direct-bound reference map; scalar fallback remains
available and identical.

### Phase 3 — GPU assist, exact only (target 10–25×)

GPU is allowed only where it can be proven bit-exact.

**Safe first use:** GPU candidate rejection/culling using integer comparisons and
conservative bounds, producing the same candidate light set that the CPU would accept.
Shading remains on the CPU. This avoids GPU float differences and can remove most of
the 2,520-light scan.

**Full GPU direct lighting:** possible only with:

- explicit IEEE round-to-nearest add/mul/div/sqrt intrinsics;
- no FMA contraction, no fast-math, no reassociation;
- the same evaluation order per sample;
- `pow`/`log`/trig either avoided, matched exactly, or handled by a CPU fallback;
- bitwise validation per platform and GPU vendor.

If any output byte differs, the GPU path is disabled for that map/feature and the
scalar path is used. A GPU result is never "close enough".

Backend options under the current build rule (Zig only):

- **OpenCL C kernels compiled at runtime** — no build-time compiler dependency,
  portable across NVIDIA/AMD/Intel, loaded dynamically.
- **Vulkan compute / VK_KHR_ray_query** with precompiled SPIR-V — portable but adds a
  shader-compiler dependency.
- **CUDA/OptiX** — fastest, but requires `nvcc` and NVIDIA hardware; it would violate
  the "Zig only" build rule unless kept as an optional externally built backend.

**Exit:** byte-identical on all gates; GPU assist demonstrably faster than Phase 2;
automatic fallback on unsupported hardware/features.

### Phase 4 — Transfer and bounce phase (only if profiling justifies it)

For large maps where `MakeScales`/`BuildVisLeafs`/`BounceLight` dominate:

- CSR sparse transfer matrix with better locality.
- GPU SpMV only with a deterministic accumulation order (fixed per-row order) that
  matches the CPU; parallel reductions that change addition order are not allowed.
- Keep `.inc` file format and compression identical.

**Exit:** byte-identical `.bsp` and `.inc`; measured gain on transfer-bound maps.

### Phase 5 — Full pipeline review

Only after Phases 1–4:

- revisit `FinalLightFace`, `CalcLightmap`, interpolation and blur;
- verify Amdahl limits with profiling;
- decide whether a bit-exact full-GPU pipeline is feasible or whether the project
  stops at the CPU/GPU-assist ceiling.

## 7. Verification matrix

Maps:

- `c1a3c` (small, func_detail + texlights);
- `c4a2b` (info_compile_parameters);
- `stalkyard` (large direct-light case);
- at least one large open map with a heavy transfer matrix.

Option matrix per map:

- defaults, `-extra`, `-bounce 1`, `-bounce 8`, `-fast`;
- `-vismatrix normal`, `sparse`, `off`;
- `-rgbtransfers` on/off;
- `-incremental`;
- texlight styles, opaque/translucent surfaces, `-blur`;
- `-threads 1`, `2`, `4`, `8`, `16`.

For every combination:

1. Run `hlrad` and `hlrad-ng` on identical copies.
2. Compare full `.bsp` bytes, not only the lighting lump.
3. Compare `.dlit`, `.inc`, and `ripent -export` entity text with the compiler date
   normalized.
4. Record first-difference offset and owning lump on failure.

Add small randomized maps (boxes, moving lights, overlapping brushes) to exercise
falloff, styles and edges. Keep the reference assets outside the repository.

## 8. Performance gates

- Phase 1: ≥2× on at least one map, no map slower.
- Phase 2: ≥5× on `stalkyard`-class direct-bound maps, total time.
- Phase 3: ≥10× total on direct-bound maps with byte-identical output.
- Each phase: memory use no worse than `hlrad`; no new correctness failures.
- Report phase timings (`BuildFacelights`, `BuildVisLeafs`, `MakeScales`, bounces,
  final) and total wall time, at fixed thread counts.

## 9. Risks and mitigations

| Risk | Mitigation |
|---|---|
| Compiler contracts FMA or auto-vectorizes differently | `-ffp-contract=off`; inspect generated code; compare bytes after every change |
| SIMD changes rounding | explicit RN operations, same order, scalar tail; reject on first diff |
| GPU float differences | GPU only for exact candidate culling first; strict IEEE intrinsics for shading; fallback |
| GPU vendor/platform differences | separate Windows/Linux baselines; disable GPU when parity fails |
| `pow`/`log`/`sqrt` differ across libm/GPU | keep transcendental paths on CPU or match exactly; no approximations |
| Thread scheduling changes accumulation order | per-sample light *values* are independent, but the face style list (`f_styles`) is order-dependent shared state; the tile queue replays its append order serially per face (§12, step C) |
| Light BVH changes accepted set | conservative bounds only; prove skip predicates; exact scalar fallback |
| ray/BVH semantics differ from BSP at edges | keep BSP traversal semantics; validate edge cases; no triangle-BVH substitution without proof |
| Build rule forbids CUDA/nvcc | OpenCL runtime kernels or Vulkan SPIR-V; CUDA only as optional external backend |
| Parity passes on one map only | option matrix + random maps + both platforms |

## 10. Work estimate

| Phase | Estimate |
|---|---:|
| 0 — target + differential harness | 0.5–1 day |
| 1 — exact data layout/scheduling | 2–4 days |
| 2 — exact light acceleration + SIMD | 1–2 weeks |
| 3 — GPU assist / exact GPU direct | 3–6 weeks |
| 4 — transfer/bounce | 1–2 weeks, only if profiling justifies it |
| Continuous verification | throughout |

Realistic first result: **2–5×** from Phase 1/2 with no quality change. Orders of
magnitude require Phase 3 and a bit-exact GPU path; if that cannot be proven, the
project stops at the exact CPU ceiling rather than shipping approximate lighting.

## 11. Immediate next step

Phase 0 and Phase 1 are complete and verified; Phase 2's first piece (the
conservative light BVH) is landed and byte-verified (see §12). Next:

1. Extend the verification matrix: `-bounce 1/8`, `-vismatrix normal/sparse/off`,
   `-rgbtransfers`, `-incremental`, `-blur`, texlight styles — per §7.
2. Phase 2 continues with the measured levers from §12 step B: coherent ray
   packets (`trace_packet`) around `TestLine` and the texlight sight-area
   path; exact SIMD over point/spot lights; exact surface-light rejection
   predicates (e.g. `dot2*dist <= MINIMUM_PATCH_DISTANCE`) still need a
   per-light- normal proof before they can join the BVH.

Until then, `hlrad` remains the default and the fallback.

## 12. Implementation log

Reference: `stalkyard_custom.map` (J.A.C.K copy, 395,081 bytes) with
`stalkyard_custom.rad` texlights and the Half-Life valve WADs. All runs:
`-extra -lights lights.rad`, Windows build, same thread count on both tools.

### Phase 0 — 2026-11-10

- `CMakeLists.txt`: `VHLT_HLRAD_NG` (OFF), `add_vhlt_tool(hlrad-ng)` with
  `INCLUDE_FIRST` + `NO_INSTALL`, `format` glob covers `hlrad-ng/`.
- Both presets build `hlrad-ng` warning-free (`-Werror`).
- `tools/diffhlrad.ps1` landed (thread matrix, determinism switch, first-diff
  offset + lump reporting, baseline TSV, timings).
- `matrix-1` (pure copy, determinism on): 5/5 byte parity, bsp
  `c3cf87acbdacef…` at every thread count; hlrad and hlrad-ng each
  self-consistent at every count. Baseline recorded in
  `build/diffhlrad/baseline.tsv`.

### Phase 1, step A (flat light storage) — 2026-11-10

`hlrad-ng/lightmap.cpp` only:

- `FlattenDirectLights()`: after `CreateDirectLights()` finishes mutating the
  per-leaf linked lists (including the sky-fix move), it copies them into one
  contiguous `calloc`ed `directlight_t` array in the exact iteration order
  (ascending leaf, then list order), chained within each leaf through the
  records' `next` pointers (`g_directlights_head[]` holds the per-leaf heads).
  Old nodes are freed during the copy; `sunnormals` arrays move into the flat
  records and are freed by `DeleteDirectLights`.
- `GatherSampleLight` keeps the original `for (; l; l = l->next)` head over
  the chained flat records; the loop body is verbatim, so per-sample
  arithmetic and light order are unchanged. `TestLine` untouched.
- `DeleteDirectLights` frees the flat array.

Verified with the differential harness (`matrix-2`): 5/5 byte parity at
threads 1/2/4/8/16, bsp hash unchanged (`c3cf87acbdacef…`).

Timing (wall, `matrix-2`, single run per count; the t16 row ran concurrently
with an unrelated build and is not representative):

| threads | hlrad | hlrad-ng |
|---:|---:|---:|
| 1 | 34.04 s | 35.21 s |
| 2 | 17.46 s | 17.99 s |
| 4 | 9.22 s | 9.58 s |
| 8 | 5.57 s | 6.21 s |
| 16 | 4.13 s | (6.51 s, concurrent load) |

`matrix-2` used the index-run variant of the walk, which measured +3.8% on an
interleaved A/B (3 × t1 each, quiet machine: hlrad 33.49 s avg vs hlrad-ng
34.75 s avg, identical hashes). The extra per-leaf load (run begin + end
instead of one head pointer) was the cost, so the walk now chains the records
through their `next` pointers within each contiguous run and keeps the
original `for (; l; l = l->next)` head — one load per leaf, one per light,
over contiguous memory.

Second interleaved A/B after the change (3 × t1 each, quiet machine):
hlrad 33.78 s avg vs hlrad-ng 33.84 s avg (+0.2%, noise), byte-identical
hashes every run. Step A is therefore parity-neutral on this map and is the
layout foundation for the Phase 2 SoA/SIMD and light-BVH work.

`matrix-3` (final binaries, `-Determinism` on): 5/5 byte parity at threads
1/2/4/8/16, ng hashes unchanged from the Phase 0 baseline, determinism holds
for both tools at every count, and wall times are parity-neutral:

| threads | hlrad | hlrad-ng |
|---:|---:|---:|
| 1 | 34.22 s | 34.27 s |
| 2 | 17.40 s | 17.60 s |
| 4 | 9.29 s | 9.36 s |
| 8 | 5.59 s | 5.63 s |
| 16 | 4.14 s | 4.18 s |

### Phase 1, step C (sample-tile work queue) — 2026-11-10

`BuildFacelights` is restructured into per-face setup (`BuildFacelightsSetup`),
dynamically scheduled luxel tiles (`BuildFacelightsNGTile`, 128 luxels each,
`GetThreadWork`-pulled), and a per-face finish (`BuildFacelightsNGFinish`) that
replays the face style list in the original first-appearance order, then runs
the unchanged post pass. `CalcLightmap`'s luxel body was extracted verbatim as
`CalcLightmapLuxel` (caller supplies the styles list, the sample slot and the
per-tile pvs cache). Two findings worth recording:

1. The doc §9 assumption "per-sample work is independent" is wrong in this
   codebase: the face style list (`f_styles`) is mutable shared state whose
   contents depend on luxel order. The tile path works around it by having
   each luxel accumulate keyed by style number (the default macro set already
   does this inside `GatherSampleLight`) into a per-luxel list, and by
   replaying the append order serially per face in the finish phase. Because
   appends never move existing entries, each style's face slot is stable, and
   the replay is exact.
2. `RunThreadsOn` passes the *thread index* to the work function, which must
   loop `GetThreadWork()` itself (`RunThreadsOnIndividual` hands out work-item
   indices). The tile worker therefore loops internally.

The scalar path stays available and identical: `HLRAD_NG_LEGACY=1` selects the
original per-face flow (Setup + `CalcLightmap` + Post), and non-default macro
sets keep the original `BuildFacelights` structure. Verification: see §12.

### Phase 1, step B (profiling counters) — 2026-11-10

`hlrad-ng/profile.{h,cpp}` behind `-DVHLT_HLRAD_NG_PROFILE=ON`: counters for
`GatherSampleLight` calls, leaf checks, light records visited, lights reaching
the switch, `TestLine` calls and occlusions, printed by `ProfileReport()`
right after `BuildFacelights`. Compiles clean on both presets (with and
without the macro); zero cost when off.

First measurement (`stalkyard_custom`, `-extra`, 1 thread):

| counter | value | per sample |
|---|---:|---:|
| GatherSampleLight calls | 1,036,555 | — |
| leaf checks | 756,685,150 | 730 |
| light records visited | 49,904,117 | 48.1 |
| lights reached switch | 47,548,056 | 45.9 |
| TestLine calls | 26,079,513 | 25.2 |
| occluded | 14,769,843 | 14.2 |

Read-out: PVS already cuts 2,520 lights to ~48 per sample (98%), and 95% of
those reach the switch, so Phase 2's rejection predicates (dot/cone) will not
help much here; the per-sample costs are the 730-leaf walk, ~25 `TestLine`
BSP rays (57% occluded) and the texlight sight-area math. That steers Phase 2
toward ray coherence/trace_packet and the sight-area path, and keeps the
light BVH as a secondary lever. Per-sample counters also make the tile-based
`sample_queue` work measurable.

#### Landing notes

Landing log: the initial implementation passed byte parity at 1 thread
immediately after fixing three implementation defects found by the harness
and by targeted instrumentation:

1. `CalcLightmapLuxel` receives the pvs buffer as a pointer, so the original
   `sizeof(pvs)` became 8; `DecompressVis` overflowed. Fixed by passing the
   buffer length explicitly.
2. The setup/post split ran `CalcLightmap`/post for `TEX_SPECIAL` faces whose
   setup returned early; the legacy wrapper now mirrors the original flow.
   (Superseded by the lmcache-NULL guard in the tile path.)
3. The tile worker treated its argument as the work-item index, but
   `RunThreadsOn` passes the thread index; the worker now loops
   `GetThreadWork()` internally.
4. The first working version replayed every luxel's slot mapping through a
   temporary copy (+10% wall time on the reference map at 1 thread). The
   finish phase now skips the copy when the local→face slot mapping is the
   identity (the common single-style case), making the queue parity-neutral
   on the reference map (t1 33.9 s, t16 4.1 s, byte-identical).

`matrix-5` (final binaries, `-Determinism` on, threads 1/2/4/8/16): 5/5 byte
parity, ng hashes unchanged from the Phase 0 baseline, determinism holds, and
timing is parity-neutral within run-to-run noise:

| threads | hlrad | hlrad-ng |
|---:|---:|---:|
| 1 | 33.97 s | 35.05 s |
| 2 | 17.80 s | 16.89 s |
| 4 | 9.95 s | 10.71 s |
| 8 | 5.45 s | 5.60 s |
| 16 | 3.93 s | 4.11 s |

(The tile queue's win is dynamic load balancing, which this map's uniformly
small faces do not exercise; the infrastructure is in place for the §7
option-matrix maps.)

### Phase 2, step A (conservative light BVH) — 2026-11-10

`hlrad-ng/lightmap.cpp`:

- `BuildLightBVH()` builds one balanced interval tree over each leaf's
  contiguous light run (leaf runs of 4+ lights by default; `HLRAD_NG_BVH_MIN`
  overrides, `0` disables). Nodes store the origin AABB of point/spot lights
  and flags: any skylight, all topatch, all non-topatch, all zero-intensity.
- `MarkLightBVH()` runs once per leaf per `GatherSampleLight` call and collects
  the prunable ranges in a small per-call stack list; the leaf walk (now
  index-based over the contiguous run) jumps pruned ranges and otherwise
  evaluates the surviving lights in the original order, with the body
  unchanged. No state is shared between threads.
- Predicates, all proven against the default macro set
  (`HLRAD_ACCURATEBOUNCE_ALTERNATEORIGIN`, `HLRAD_ACCURATEBOUNCE_TEXLIGHT`,
  `HLRAD_ALLOWZEROBRIGHTNESS`, `HLRAD_GatherPatchLight`):
  1. all zero-intensity → each light hits the `ALLOWZEROBRIGHTNESS` continue;
  2. all `topatch` mismatched for the call's step → each hits the
     `GatherPatchLight` step continue;
  3. all point/spot and the origin AABB fully behind the sample plane
     (`max dot(origin-pos, normal) <= -0.1`, margin covering the float error
     of the bound) → each light's normalized dot is below
     `NORMAL_EPSILON` and hits the behind-surface continue.
  Skylight- and surface-containing nodes are never pruned (skylights use a
  different predicate chain; behind-surface lights can still contribute via
  the near-range sight-area path).

Verified byte-identical on `stalkyard_custom` (`-extra`, 1 thread) against the
recorded baseline `17C19287828C…` with the BVH forced on every leaf
(`HLRAD_NG_BVH_MIN=1`, 38.11 s — tree overhead on 1–2 light leaves dominates)
and with the default 4-light threshold (34.51 s, parity-neutral). The
reference map's dense leaves are few and point/spot lights are ~6% of the
walk, so the BVH is neutral here; it exists for point-light-heavy maps. The
bigger Phase 2 lever remains the measured `TestLine`/sight-area cost (§12,
step B).

### Phase 2, step B (exact SIMD + BVH concurrency fix) — 2026-11-10

`hlrad-ng/mathutil.cpp`: `CalcSightArea` and `CalcSightArea_SpotLight` evaluate
four sky directions per SSE2 lane. Each lane's dot products use the same IEEE
operations in the same order as the scalar `DotProduct` ((x+y)+z), the same
strict comparisons, and surviving lanes accumulate area in index order, so the
result is bit-identical; a scalar tail handles the last <4 directions and the
whole loop falls back to scalar code on non-x86_64 builds. Lint suppression is
scoped (`NOLINTBEGIN/END(portability-simd-intrinsics)`) because a portable
fallback is present.

Also fixed a real concurrency bug found by testing the light BVH at 16 threads
(previous matrices predated it): the BVH's pruned-range bookkeeping used a
global sample id plus shared skip arrays, which raced between workers. The
pruned ranges are now collected in a small per-call stack list (sorted, capped
at 64 ranges; overflow simply disables pruning for that leaf), so no state is
shared between threads. Verified at 16 threads: two consecutive runs both match
the baseline hash exactly.

Result on `stalkyard_custom`: byte-identical, but no speedup — the phase
timings are unchanged (tile phase ≈ 1.4 s at 16 threads, same as before). The
profile read-out is confirmed: `CalcSightArea` is not the hot path on this map;
the remaining cost is the light walk plus `TestLine`, i.e. the ray-coherence
work (`trace_packet`) and the per-light scalar math, not the sight-area
integration. The SIMD remains available for texlight-heavy maps where the
near-range sight-area path dominates.

An exact SSE2 batch pre-rejection over 4 consecutive lights (point/spot plane
test plus the surface `MINIMUM_PATCH_DISTANCE` predicate) was prototyped and
removed: it did not pass byte parity and showed no gain before that, so it is
not part of the build.

## 13. Packet ray tracer (2026-11-10)

Reference: `stalkyard_custom.map` with `stalkyard_custom.rad`, `-extra`,
Windows build, 8-core/16-thread host.

### What changed

- `hlrad-ng/tracepacket.h`, `hlrad-ng/trace.cpp`: a packet tracer over the
  world BSP. Up to 8 rays that share a start point are walked through the tree
  together, with the plane rules of `TestLine_r` applied per ray. Each ray
  keeps a parameter interval on its original segment instead of moving its end
  points at every split. The walk is iterative with an explicit stack; trees
  deeper than the stack use the scalar walk. The 8-wide build needs AVX2
  (checked at run time), the 4-wide build runs everywhere, and both give the
  same output.
- Sky visibility (`TraceSkyVisibility`) is traced once per point and shared by
  every `light_environment`. The sky normals are grouped along a Morton curve
  so the rays of a packet point the same way. Sky and sun rays stop just
  outside the world bounds instead of 131,072 units away.
- Sun rays are traced in packets per skylight (`TraceSunRays`); light rays are
  collected and traced in packets, then added in the order the lights were met
  (`FlushPendingLights`).
- The per-sample light walk visits only leaves that hold lights.
- `profile.{h,cpp}` now has real timers (`PROFILE_SCOPE`) besides counters.

### Measured

| | hlrad | hlrad-ng |
|---|---:|---:|
| total, 1 thread | 33.7 s | 17.6 s |
| total, 16 threads | 3.79 s | 2.21 s |

Lightmaps against hlrad: only the lighting lump differs, in 1 byte of 281,946,
by one level. With `HLRAD_NG_NOPACKETS=1` the output equals hlrad's byte for
byte, so every difference comes from the packet kernel's rounding. Output is
identical at 1 and 16 threads and with `HLRAD_NG_NOAVX2=1`.

### Where the time goes now

`-DVHLT_HLRAD_NG_PROFILE=ON`, 1 thread, share of direct-lighting time:

| section | share |
|---|---:|
| packet kernel (sky, sun and light rays) | 43% |
| rest of `GatherSampleLight` (per-light math) | about 31% |
| sample setup in `CalcLightmapLuxel` | about 19% |
| rest of the per-face post pass | about 6% |

The kernel visits 41 tree nodes per packet at about 52 cycles each. Most of
those nodes are the descent from the root to the leaf of the start point,
which every packet repeats.

### Next levers, by expected gain

1. Wider packets (64 rays) so the shared descent is paid once per 64 rays, or
   a surface BVH that skips empty space.
2. A GPU backend (Vulkan compute) for the same rays.
3. The per-light math and the per-sample setup, which are now as large as the
   rays.

## 14. Max-quality preset, lock removal, bounces and the GPU backend (2026-11-10)

Reference: `stalkyard_custom.map`, 16 threads, RTX 2080. "Max quality" is the
preset of `docs/user/max-quality.md`: `-extra -bounce 16 -chop 16 -texchop 8
-dlight 1 -compress 0 -vismatrix normal` (57,470 patches, 226M transfers).

### Measured, back to back

| preset | hlrad | hlrad-ng |
|---|---:|---:|
| default (`-extra`) | 4.1-4.3 s | 2.5 s |
| max quality | 95.4 s | 21.9-22.4 s (22.8-25.7 s with `HLRAD_NG_GPU=0`) |

Lightmaps against hlrad: 2 bytes of 281,946 differ on the default preset and
12 on max quality, each by one level. GPU on and GPU off give identical files.

### What changed

- `vismatrix.cpp`: the vis bit of a visible patch pair was set under the
  global `ThreadLock`, about 113 million times; that serialized
  `BuildVisLeafs`. It is now `__atomic_fetch_or`. This alone took the phase
  from 45 s to 8.4 s and left the output byte-identical.
- `vismatrix.cpp`, `sparse.cpp`: pairs traced from the patch's own origin are
  collected and traced as packets (`FlushPendingPairs`); 8.4 s to about 4.5 s.
- `qrad.cpp` `GatherLight`: a per-bounce table of what each patch emits
  (`BuildBounceEmitters`) turns a transfer into one multiply-add when the
  emitter sends style 0 only and there are no per-pair opaque styles; other
  cases take the original path. 16 bounces: about 7 s to about 1.4 s.
- `gpu.{h,cpp}`, `gpusky.comp`: Vulkan compute backend. The sky visibility of
  every patch is computed in one batch before `BuildFacelights`
  (`PrecomputePatchSkyVisibility`), used from 16,384 patches up. On the
  reference map: 57,470 patches (about 470 million rays) in 2.0-2.7 s,
  against about 4 s for the same rays on 16 CPU threads. With
  `HLRAD_NG_GPU_CHECK=1`, 0 of 6.47 million sampled rays differed from the CPU
  tracer, and the Vulkan validation layer reported nothing.

### Where the time goes now (max quality, quiet machine, 19-22 s total)

`MakeScales` about 5 s, `BuildVisLeafs` about 4.5 s, per-sample direct
lighting about 3.5 s, GPU sky about 2 s, post pass about 1 s, bounces about
1.4 s, the rest about 1.5 s.

### Findings worth keeping

- The GPU walk of the BSP is only about twice as fast as 16 CPU threads on
  this hardware; a per-ray tree walk diverges badly on a GPU. A large GPU gain
  needs hardware ray tracing over real surfaces (`VK_KHR_ray_query`) or moving
  whole phases (`MakeScales`) to the GPU, not this kernel run harder.
- With the max-quality patch count the sparse vis matrix makes `MakeScales`
  take 34 s against 5 s for `-vismatrix normal`; the outputs are identical.
- Run timings back to back: the same binary varied by up to 40% across this
  session with machine load.

## 15. Town map: patch limit, light styles, spread sun (2026-11-11)

Second test map: `testmap_town` (10,602 faces, 3 light styles, a
`light_environment` with `_spread 90`, texlights). With the max-quality
preset it has 111,371 patches, 12,058 direct lights and 1.3 billion transfers.

### Fixed

- `-vismatrix normal` stopped with "Exceeded MAX_PATCHES" above 65,535 patches
  because the matrix bit index was 32-bit. It is `size_t` now, the matrix is
  allocated with `calloc` (`AllocBlock` takes a 32-bit size on Win64), and
  `MAX_VISMATRIX_PATCHES` equals `MAX_PATCHES`.
- Wrong light styles on multi-style maps, from the sample-tile queue of
  section 12: `BuildFacelightsNGFinish` remapped a luxel's local style slots
  to the face's slots without clearing the slots it moved out of, so a
  luxel's light could stay behind under another style. The earlier byte
  parity runs could not see it: the stalkyard map has one style. Against
  hlrad on the town map, default settings: 1,544 faces with a different style
  set before the fix, 2 after; 94 of 1,107,972 lightmap bytes differ, by one
  level. **Test every change on a multi-style map.**

### Faster

- Bounces: the emitter table holds up to three styles besides style 0 per
  patch (`bounceemitter_t`). 16 bounces: 107 s to 15 s.
  `HLRAD_NG_NOFASTBOUNCE=1` selects the original path for comparison (1 face
  and 27 bytes differ on the town map).
- Spread suns: `TraceCoherentOrder` sorts a light's sun normals once
  (`directlight_t::sunorder`), so `TraceSunRays` fills packets with rays that
  point the same way. Direct lighting: 301 s to 253 s.

### Measured (16 threads)

| town map | hlrad | hlrad-ng |
|---|---:|---:|
| default (`-extra`) | 6 m 38 s | 3 m 04 s |
| max quality | stops: Exceeded MAX_PATCHES | 6 m 04 s |

### Where the time goes (max quality, about 6 minutes)

Per-sample direct lighting about 250 s, patch visibility about 50 s,
`MakeScales` about 32 s, bounces 15 s, GPU sky 4 s.

Profile of the direct lighting (profiling build, 16 threads, shares only):
sun rays 69%, light rays 22%, sky rays 3%. `_spread 90` makes the sun a dome
of about 8,000 directions that is traced for every lightmap sample, and each
sample also sees about 3,400 texlight patches.

### Next lever

The sun dome per sample. Ideas, none built: prove whole blocks of directions
visible with one walk of their bounding pyramid through the BSP and trace
only the blocks that touch geometry; skip samples whose leaf sees no sky
leaf in the PVS; packets wider than 8.

## 16. Ray queries, GPU radiosity and the one-line log (2026-11-11)

Reference machine as before (Ryzen 7 2700X, RTX 2080), max-quality preset,
16 threads. The town map changed since section 15 (10,569 faces, 111,324
patches, a sun with `_spread 10`); its baseline at the start of this section
was 209 s, the reference map's 20.3 s.

### Measured

| max quality | start of section | GPU off (`HLRAD_NG_GPU=0`) | GPU |
|---|---:|---:|---:|
| reference map (57,442 patches) | 20.3 s | 17.7 s | 5.6 s |
| town map (111,324 patches) | 209 s | not timed | 23.6 s |

Default preset (`-extra`): reference map 2.1 s, town map 7.5 s with the GPU
(25 s without).

Town map, per phase:

| phase | before | now |
|---|---:|---:|
| direct lighting (patches and samples) | 98 s | 6.0 s |
| patch visibility | 53 s | 2.3 s |
| transfers | 34 s | 0.9 s |
| 16 bounces | 15 s | 9.2 s |
| bounced light on lightmaps | 3.1 s | 3.5 s |
| GPU start, scene | - | 0.5 s |

### What changed

- **Hardware ray tracing.** A device with `VK_KHR_ray_query` traces against
  triangles. `trace.cpp` `BuildSceneTriangles` cuts every node plane down to
  its node's region and pushes it through both subtrees; the pieces that end
  up between two leaves of different contents are the scene (sky pieces last,
  so the primitive number says "sky"). Pieces are grown by 1/512 unit plus a
  millionth of their largest coordinate so they overlap. The town map is
  22,000 triangles. Devices without ray queries run the same shaders with the
  BSP walk; that build is about as fast as 16 CPU threads on this machine.
- **Direct lighting** (`gpudirect.comp`): one invocation gathers one point:
  sun over its spread directions, sky over the sky normals, and every light
  in the leaves the point's leaf sees. Texlight patches closer than their
  emitter range go to a pair list and the CPU adds them with the patch shape.
  The CPU still finds the sample positions and runs the per-face post pass.
- **Patch visibility** (`gpuvis.comp`): one invocation handles one patch
  against 32 others. Pairs closer than an emitter range (plus one unit) come
  back as bits and are tested on the CPU with `GetAlternateOrigin`.
- **Radiosity** (`gpurad.comp`): the visibility shader writes a full matrix
  on the device; the radiosity shader counts each row, fills in the 16-bit
  transfer factors (the form factor of `MakeScales` for pairs that are not
  close) and gathers every bounce. The close pairs keep their transfers on
  the CPU. The town map holds 1.55 billion factors in 4.4 GB of video memory;
  a map that does not fit uses the CPU path.
- **CPU path**: `MakeScalesBlocks` reads the half matrix in blocks of 32
  patches instead of one bit per row (town map: 34 s to 7.5 s), bounces
  gather through per-style arrays (15 s to 9 s), and the default sparse
  setting uses the dense matrix up to 1 GB.
- **Log**: one line per phase.

### Checked

- BSP-walk shaders against the CPU path: 17 of 1.1 million lightmap bytes
  differ on the town map, by one level.
- Ray-query shaders against the CPU path, town map at default settings: 1.2%
  of the bytes differ, nearly all by one level; a handful of luxels at shadow
  edges differ by up to 21. About 280 faces gain or lose a style map whose
  brightest value is 1 or 2.
- GPU radiosity against the CPU transfer path: 55 bytes differ on the town
  map, by one level.
- With `HLRAD_NG_GPU=0` the reference map compiles to the same bytes as
  before this section.
- Both maps were looked at in the engine by the owner after the direct
  lighting moved to the GPU.

### Where the time goes now (town map, 23.6 s)

Bounces 9.2 s, direct lighting 6 s, bounced light on lightmaps 3.5 s, patch
visibility 2.3 s, everything else under 1 s each.

### Next levers

1. Bounces: each one reads 1.55 billion factors and, per factor, what the
   emitter sends in every style in use; it is bound by memory traffic on the
   device as it was on the CPU. Packing the emitted light smaller, or
   stopping when a bounce adds nothing visible, would help.
2. Direct lighting: the CPU half (sample positions, per-face post pass) and
   the GPU half are not overlapped; the GPU waits while the CPU works.
3. `AddPatchLights` (bounced light on lightmaps) is untouched CPU code.
