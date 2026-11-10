# Warning Cleanup Plan

**Goal:** zero compiler warnings in both builds (`windows` and `linux` presets, Zig 0.17.1, `-Wall`), with **no change to compiled map output**, then keep it there with `-Werror`.

**Estimate:** 9–13 hours of agent time for the current `-Wall` set (batches 0–5). Raising to `-Wextra` (batch 6) is a separate decision, to be estimated after measuring it.

**Status (2026-11-10):** complete. Clean Windows and Linux builds report zero warnings, and `VHLT_WERROR` (ON by default) adds `-Werror`. This document remains the record of the original 154-site baseline and the batch decisions.

## Rules for this work

- Read [`docs/llms.txt`](../llms.txt) and [`AGENTS.md`](../../AGENTS.md) first. Every rule there applies: CMake, Ninja and Zig only; `safe_*` string helpers; minimal changes; CHANGELOG entries.
- **No behavior change for valid input.** A warning fix must not change what the tools compute. Where a warning reveals a real bug (batch 1), fix the bug deliberately, note it in `CHANGELOG.md` under *Fixed*, and explain it in the commit.
- **Never silence a warning with a flag or a pragma.** Fix the code. The only flags allowed are those already in `cmake/toolchains/zig-common.cmake`.
- **Respect `#ifdef` configurations.** A variable that looks unused may be used under another feature macro (see `common/cmdlib.h`). Wrap it in the same `#ifdef` instead of deleting it, and don't edit compiled-out code beyond what the warning needs.
- Before changing a function's signature (for example `char*` → `const char*`), find all its references (LSP `findReferences`, or `rg` if no language server is available) and update every caller.
- Work one batch at a time. After each batch, run the gates below, then commit with the batch name.

## Measuring

```sh
cmake --preset windows && cmake --build --preset windows 2>&1 | tee build/windows-build.txt
cmake --preset linux   && cmake --build --preset linux   2>&1 | tee build/linux-build.txt
rg -o '\[-W[a-z0-9-]+\]' build/windows-build.txt build/linux-build.txt --no-filename | sort | uniq -c | sort -rn
```

Reconfigure from an empty `build/` (`rm -rf build`) for a full count. An incremental build only reports the files it recompiled. Headers are counted once per translation unit, so count *unique sites* (`file:line`), not raw lines.

### Baseline (2026-11-10, clean build)

154 unique warning sites: 149 in the Windows build and 132 in the Linux build, mostly shared.

| Flag | Sites | Where |
|------|------:|-------|
| `-Wlogical-op-parentheses` | 30 | qcsg.cpp 6, tjunc.cpp 4, surfaces.cpp 4, brink.cpp 3, lerp.cpp 3, qradutil.cpp 2, cmdlinecfg.cpp 2, bspfile.cpp, flow.cpp, writebsp.cpp, brush.cpp, loadtextures.cpp, lightmap.cpp |
| `-Wunknown-pragmas` | 30 | vismatrixutil.cpp 6, vismatrix.cpp 3, flow.cpp 3, threads.cpp 3, vis.cpp 3, qrad.cpp 3, sparse.cpp 3, qrad.h 2, outside.cpp, csg.h, transparency.cpp, wadinclude.cpp |
| `-Wunused-variable` | 30 | lightmap.cpp 5, solidbsp.cpp 4, qrad.cpp 3, flow.cpp 3, loadtextures.cpp 2, vismatrixutil.cpp 2, qradutil.cpp 2, vismatrix.cpp 2, sparse.cpp 2, qcsg.cpp, wadpath.cpp, surfaces.cpp, tjunc.cpp, nomatrix.cpp |
| `-Wwritable-strings` | 24 | ripent.cpp 7, qrad.cpp 4, qcsg.cpp 4, vis.cpp 4, qbsp.cpp 4, log.cpp |
| `-Wunused-but-set-variable` | 11 | vismatrixutil.cpp 2, lightmap.cpp 2, qrad.cpp 2, wadcfg.cpp 2, bspfile.cpp, map.cpp, qcsg.cpp |
| `-Wnontrivial-memcall` | 6 | hlcsg/map.cpp 6 (`brush_t`) |
| `-Wimplicit-const-int-float-conversion` | 5 | lightmap.cpp 4 (736, 737, 9071, 9074), flow.cpp 1609 |
| `-Wformat-security` | 4 | common/log.cpp 462, 613, 723; ripent/ripent.cpp 391 |
| `-Wunused-const-variable` | 3 | lightmap.cpp 2, messages.cpp |
| `-Wformat` | 2 | hlrad/lightmap.cpp 135, 805 (Linux) |
| `-Wdangling-else` | 2 | hlrad/trace.cpp 51, common/cmdlinecfg.cpp 204 |
| `-Wfortify-source` | 1 | **hlrad/qrad.cpp 575** |
| `-Wformat-extra-args` | 1 | hlrad/qrad.cpp 4472 |
| `-Wmisleading-indentation` | 1 | common/scriplib.cpp 51 |
| `-Wswitch` | 1 | hlcsg/brush.cpp 667 |
| `-Wparentheses-equality` | 1 | hlbsp/solidbsp.cpp 1738 |
| `-Wunneeded-internal-declaration` | 1 | hlbsp/writebsp.cpp |
| `-Wunused-function` | 1 | hlrad/lightmap.cpp |

## Gates after every batch

1. Clean configure and build of both presets; the warning count must strictly decrease, with no new flags.
2. Smoke test: each `build/windows/bin/*.exe` prints `v<version>` (exit code 1 is expected).
3. Regression check (batch 0): the reference maps must produce byte-identical outputs.
4. `rtk git status` is clean apart from the intended files. Commit.

## Batches

### Batch 0: Regression harness (2–3 h)

The repository has no tests, so build a check that compiled output is unchanged.

1. Ask the user for 2–3 representative `.map` files and the WADs they use: one small, one large with func_detail and texlights, and one using `info_compile_parameters`. Keep them **outside the repository** (they are third-party assets).
2. Write a script (`tools/regress.sh` or `.ps1`). For each map it should:
   - Run `hlcsg`, `hlbsp`, `hlvis -full` and `hlrad -extra`, all with **`-threads 1`** for deterministic output.
   - Save `sha256` of the `.bsp`, `.prt` and the lumps (`ripent -export` for the entity text).
3. The `.bsp` contains a `compiler` key with `__DATE__` (`HLCSG_LOGVERSION`). Compare builds made on the same day, or remove that key from both `.ent` exports before comparing.
4. Record the hashes from the **current** binaries as the reference before changing any code.

### Batch 1: Warnings that are real bugs (1–2 h)

Fix the defect, not just the warning. Each fix gets a CHANGELOG *Fixed* or *Security* entry.

| Site | Problem | Fix |
|------|---------|-----|
| `hlrad/qrad.cpp:575` (`-Wfortify-source`) | `sscanf` `%4095s` into a 260-byte `_MAX_PATH` buffer on Windows. This is a **stack overflow from a long texture name in `lights.rad`**, audit item H12, still open. | Size the buffer `MAXTOKEN`, or derive the width from `sizeof(buf) - 1`. |
| `common/log.cpp:462, 613, 723`, `ripent/ripent.cpp:391` (`-Wformat-security`) | A non-literal string is passed as the format. A `%` in a message or path becomes undefined behavior. | Pass `"%s", str`. |
| `hlrad/lightmap.cpp:135, 805` (`-Wformat`) | `%d` with `ptrdiff_t` (64-bit) on Linux. | Use `%td`, or cast to `int` if the value is bounded (check the bound). |
| `hlrad/qrad.cpp:4472` (`-Wformat-extra-args`) | Extra `printf` argument; the message is probably missing a specifier. | Read the intent; add the specifier or drop the argument. |
| `hlrad/lightmap.cpp:9071, 9074` (`-Wimplicit-const-int-float-conversion`) | `INT_MAX` compared as `float` (2147483648), so the float→int conversion that follows can overflow (undefined behavior). | Clamp against `(float)INT_MAX` correctly (for example `>= 2147483648.0f`), keeping results for in-range values identical. |
| `hlrad/lightmap.cpp:736-737`, `hlvis/flow.cpp:1609` | Sentinel constants (±99999999, 9999999999) aren't exactly representable as `float`. | Write them as float literals (`1e8f`, `1e10f`). The value doesn't change, so output doesn't change. |

### Batch 2: Control-flow ambiguity (2–3 h)

Review each site before editing. These can hide real logic errors.

- **`-Wdangling-else` (2):** add braces that make the **current** binding explicit (the `else` belongs to the innermost `if`). If the indentation suggests a different intent, note it in the batch report; don't change it.
- **`-Wmisleading-indentation` (1), `scriplib.cpp:51`:** fix the indentation or braces. `Error()` does not return.
- **`-Wlogical-op-parentheses` (30):** add parentheses around the `&&` sub-expression to match current precedence (`&&` binds tighter than `||`). Check each one: if the intent looks different, list it in the report and don't change behavior.
- **`-Wparentheses-equality` (1):** remove the extra parentheses, or make an intended assignment explicit.
- **`-Wswitch` (1), `brush.cpp:667`:** add explicit `case` labels or a `default:` with the current fall-through behavior (do nothing) for `plane_anyx`/`plane_anyy`/`plane_anyz`.

### Batch 3: Mechanical cleanup (2–3 h)

- **`-Wunknown-pragmas` (30):** these are MSVC-only `#pragma warning(...)` lines; MSVC isn't supported. Delete them after confirming each is `#pragma warning`. If any is `#pragma omp` or `#pragma pack`, stop and report it.
- **`-Wwritable-strings` (24):** string literals assigned to `char*`. Change the variable or parameter to `const char*`, finding references for every changed signature. Never cast the warning away.
- **`-Wunused-variable` (30) / `-Wunused-but-set-variable` (11):** delete the variable if it is unused in **every** feature-macro configuration. Otherwise move it inside the matching `#ifdef`. Keep any side effects of its initializer.
- **`-Wunused-const-variable` (3), `-Wunused-function` (1), `-Wunneeded-internal-declaration` (1):** delete, or wrap in the `#ifdef` that uses them.

### Batch 4: `brush_t` memory calls (1 h)

- **`-Wnontrivial-memcall` (6), `hlcsg/map.cpp`:** `memset`/`memcpy`/`memmove` on `brush_t`, which is not trivially copyable.
  - Find out what makes it non-trivial, for example a member with a constructor.
  - If raw byte copy is still correct, which it is for the current layout if no member owns resources, make that explicit with a `static_cast<void*>` on the destination and a comment explaining why.
  - Otherwise use assignment or a loop.
  - Output must stay byte-identical.

### Batch 5: Lock it in (0.5 h)

1. Confirm zero warnings on clean builds of both presets.
2. Add `option(VHLT_WERROR "Treat warnings as errors" ON)` to `CMakeLists.txt`, adding `-Werror` to the tool compile options when it is ON.
3. Update the baseline line in `docs/llms.txt` and the gates in `AGENTS.md` ("zero warnings, `-Werror` on").
4. Add a CHANGELOG *Changed* entry.

### Batch 6 (optional, decide after measuring): `-Wextra`

Measure first: configure with `-DCMAKE_CXX_FLAGS=-Wextra` in a scratch build directory and count. Expect mostly `-Wsign-compare` and `-Wunused-parameter`. Sign-compare fixes in geometry and lighting loops need care, because a cast can change behavior for negative values. Report the counts and an estimate to the user before starting.

## Report back after each batch

- The warning count before and after, by flag.
- Every site where the warning looked like a real bug or the intent was unclear, with `file:line` and a sentence on what you did or left alone.
- The gate results, including the regression hashes.
