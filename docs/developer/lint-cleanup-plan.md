# Lint Cleanup Plan (clang-tidy)

**Goal:** zero clang-tidy findings across all five tools, with **no change to compiled map output**, then make findings fail the lint build (`WarningsAsErrors: '*'`).

**Estimate:** 12–20 hours of agent time. Batch 0 is expected to remove a large share of the analyzer findings, which are false positives from functions the analyzer doesn't know never return. Re-estimate after batch 0.

## Rules for this work

- **Read first:** [`docs/llms.txt`](../llms.txt) and [`AGENTS.md`](../../AGENTS.md). All their rules apply:
  - Build with Zig, CMake and Ninja only.
  - Use the `safe_*` string helpers.
  - Keep changes minimal.
  - Format every change (`format-check`), and add CHANGELOG entries.
- **No change for valid input.** Compiled maps must stay byte-identical. Where a finding is a real bug, fix it deliberately, add a CHANGELOG *Fixed* or *Security* entry, and explain it in the commit.
- **Fix the code, don't hide the finding.** There are two exceptions, and both need a written reason:
  1. **Checks listed in batch 4:** disable them in `.clang-tidy`, with a comment explaining why.
  2. **Single proven false positives:** suppress with `// NOLINTNEXTLINE(<check>): <reason>`, naming exactly one check. A bare `NOLINT` without a check name is not allowed. Keep these rare, and list each one in your batch report.
- **Respect `#ifdef` configurations.** Code may be used under other feature macros (`common/cmdlib.h`). Don't edit compiled-out code beyond what a finding needs.
- **Before changing a function's signature or behavior,** find its references (LSP `findReferences`, or `rg` if no language server is available) and update every caller.
- **One batch at a time:** after each batch, run the gates below, commit using the batch name, and report.

## Tools

- **clang-tidy 22.1.8** (`pip install clang-tidy==22.1.8`). It must match Zig 0.17.1's bundled clang (22.1.8); a newer clang-tidy fails on Zig's built-in headers.
- **CMake wiring:** CMake passes Zig's system include paths and `_LIBCPP_*` macros to clang-tidy automatically when `VHLT_CLANG_TIDY=ON` (see `CMakeLists.txt`).
- **Checks:** configured in `.clang-tidy`: `clang-analyzer-*`, `bugprone-*`, `performance-*`, `portability-*`, with a few style checks excluded.

## Measuring

```sh
cmake -S . -B build/lint --preset windows -DVHLT_CLANG_TIDY=ON
cmake --build build/lint 2>&1 | tee build/lint-all.txt            # full run, ~5-10 min
# unique findings (file:line:check), then counts per check:
rg -o '^\S+?:\d+:\d+: (warning|error): .*\[[\w.,-]+\]$' build/lint-all.txt --no-filename \
  | sed -E 's/:[0-9]+: (warning|error): .*\[/ [/' | sort -u > build/lint-sites.txt
rg -o '\[[\w.,-]+\]$' build/lint-sites.txt | sort | uniq -c | sort -rn
```

To re-lint one file after editing it, touch it and rebuild its target, for example `cmake --build build/lint --target hlrad`. Headers are reported once per translation unit, so count **unique sites**, not raw lines.

### Baseline

Before Batch 0: **510 unique findings, 0 errors** (measured 2026-11-10 at `b7881a9`, clang-tidy 22.1.8).
After Batch 0 (`Error` and `hlassume_fail` marked `[[noreturn]]`): **395 unique findings, 0 errors**.
After Batch 1 (real defects): **333 unique findings, 0 errors**.

The table below is the remaining set after Batch 1.

| Check | Sites | Main files |
|---|---:|---|
| `bugprone-unchecked-string-to-number-conversion` | 147 | qrad.cpp 68, map.cpp 20, qbsp.cpp 15, vis.cpp 14 |
| `bugprone-suspicious-string-compare` | 55 | map.cpp 17, qrad.cpp 14, qcsg.cpp 10, bspfile.cpp 4 |
| `bugprone-implicit-widening-of-multiplication-result` | 37 | loadtextures.cpp 9, lightmap.cpp 8, bspfile.cpp 5, vismatrixutil.cpp 3 |
| `clang-analyzer-security.insecureAPI.strcpy` | 17 | cmdlinecfg.cpp 12, log.cpp 2, bspfile.cpp 2, cmdlib.cpp 1 |
| `bugprone-assignment-in-if-condition` | 16 | qrad.cpp 7, brush.cpp 3 |
| `bugprone-multi-level-implicit-pointer-conversion` | 15 | outside.cpp 3, writebsp.cpp 3, qrad.cpp 2, brush.cpp 2 |
| `bugprone-switch-missing-default-case` | 7 | solidbsp.cpp 2 |
| `bugprone-branch-clone` | 7 | qrad.cpp 2, qbsp.cpp 2 |
| `performance-enum-size` | 6 | lightmap.cpp 2 |
| `clang-analyzer-deadcode.DeadStores` | 6 | bspfile.cpp 2 |
| `bugprone-undefined-memory-manipulation` | 4 | hlcsg/map.cpp 4 (`brush_t`) |
| `bugprone-terminating-continue` | 4 | hlrad/lightmap.cpp 4 |
| `bugprone-integer-division` | 4 | lightmap.cpp 2, brush.cpp 1, brushunion.cpp 1 |
| `clang-analyzer-optin.performance.Padding` | 2 | scriplib.cpp 1, qradutil.cpp 1 |
| `bugprone-exception-escape` | 2 | qrad.cpp 1, qcsg.cpp 1 |
| `bugprone-throwing-static-initialization` | 2 | qcsg.cpp 1, map.cpp 1 |
| `bugprone-signed-char-misuse` | 1 | transparency.cpp 1 |
| `performance-no-int-to-ptr` | 1 | threads.cpp 1 |

## Gates after every batch

1. **Lint:** a full lint run. The unique-finding count must strictly decrease, with no new check names.
2. **Build:** clean build of both presets (`windows`, `linux`) with zero compiler warnings (`-Werror` is on).
3. **Format:** `cmake --build build/windows --target format-check` passes.
4. **Smoke:** each `build/windows/bin/*.exe` prints `v<version>` (exit code 1 is expected).
5. **Map regression:** the output must match the baseline exactly.

   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\regress.ps1 `
     -MapDir  "C:\Users\<user>\AppData\Local\Temp\opencode\vhlt-regress\prepared" `
     -LightsRad "C:\Users\<user>\AppData\Local\Temp\opencode\vhlt-regress\prepared\lights.rad" `
     -WadDir  "G:\SteamLibrary\steamapps\common\Half-Life\valve" `
     -OutDir  "C:\Users\<user>\AppData\Local\Temp\opencode\vhlt-regress\regress"
   ```

   These assets are in a temp directory. If they're missing, **stop and ask the user**; don't record a new baseline from modified code.
6. **Working tree:** `git status` shows only the intended files. Commit, but don't push.

## Batches

### Batch 0: Teach the analyzer what never returns, then re-measure (1–2 h)

Most `NullDereference`, `uninitialized.*`, `UndefinedBinaryOperatorResult` and `NullArg` findings follow a pattern like `if (!p) Error(...); p->x`. The analyzer assumes `Error()` can return.

1. **Mark the exiting functions.** Confirm by reading the code that `Error()` (and `Fatal()`, if it always exits) **never returns**: every path ends in `exit()`. Then mark the declarations in `common/log.h` with `[[noreturn]]`, alongside the existing `CDECL FORMAT_PRINTF(...)`, and check that both presets still build without warnings.
2. **Split `hlassume`.** `hlassume(bool, msgid)` returns when the condition is true, so it can't be `[[noreturn]]`. Split it into an `inline` wrapper, `if (!exp) hlassume_fail(msgid);`, plus a `[[noreturn]]` `hlassume_fail`, keeping the message and the exit behavior identical.
3. **Re-measure** and update the baseline table in this file with the new counts. The other batches work on whatever remains.

### Batch 1: Findings that indicate real defects (4–6 h)

Investigate each remaining site. For every one, decide in writing whether it is a real bug or a false positive.

- **Bounds and arithmetic:** `security.ArrayBound`, `core.UndefinedBinaryOperatorResult`, `core.uninitialized.Assign`/`Branch`, `core.DivideZero`, `core.BitwiseShift` (`compress.h`), `core.CallAndMessage`, `core.NonNullParamChecker`.
  - Real bug: add the missing bounds check or initialization.
  - Proven false positive: use one `NOLINTNEXTLINE` with the reason.
- **NULL-pointer handling:** `core.NullDereference` and `unix.cstring.NullArg` that survive batch 0.
  - If the value can really be NULL (a failed lookup or a NULL from a file), handle it with `Error(...)` and a clear message.
  - Otherwise add an `hlassume` or an explicit check that documents the invariant.
- **Resource handling:**
  - `bugprone-suspicious-realloc-usage`: assign `realloc` to a temporary, check it, then commit it to the original pointer.
  - `unix.Stream`: check `fopen` results, and close every opened stream on all paths.
  - `unix.Malloc` / `cplusplus.NewDeleteLeaks`: fix the leak or the missing check.
  - `unix.StdCLibraryFunctions` (`transfers.cpp`) and `unix.Errno` (`hullfile.cpp`): check the return values the analyzer flags.
- **Unsafe calls:**
  - `bugprone-unsafe-functions` (`hullfile.cpp`): replace the call with its bounded equivalent.
  - `optin.portability.UnixAPI`: usually `malloc(0)`/`calloc(0)`. Guard the zero case where it can occur, keeping today's behavior for non-zero sizes.

### Batch 2: String and number handling (3–4 h)

- **`insecureAPI.strcpy` (17), mostly `common/cmdlinecfg.cpp`:** replace with `safe_strncpy(dest, src, sizeof(dest))` or `safe_snprintf`. Where `dest` is a pointer parameter, thread its size through, finding references for every caller.
- **`bugprone-suspicious-string-compare` (55):** these are implicit-bool uses of `strcmp`/`strcasecmp`/`strncasecmp`. Rewrite them as explicit `== 0` / `!= 0`. This must not change meaning: `!strcmp(a, b)` becomes `strcmp(a, b) == 0`.
- **`bugprone-unchecked-string-to-number-conversion` (141):** `atoi`/`atof` on input that may be invalid. Apply this policy:
  - **Command-line options** (most of `qrad.cpp`, `qcsg.cpp`, `qbsp.cpp`, `vis.cpp` `main`):
    - Add **one** shared helper pair in `common/`, for example `bool ParseIntArg(const char* s, int* out)` and `ParseFloatArg`, using `strtol`/`strtod` with end-pointer and range checks.
    - Make invalid values `Error("invalid value '%s' for %s")`.
    - This is a user-visible change: document it in the CHANGELOG (*Changed*) and the manual. Valid inputs must parse exactly as before.
  - **Map, entity and data-file values** (`map.cpp`, `bspfile.cpp`, `IntForKey`/`FloatForKey`, `.rad`, `.wad.cfg`): maps depend on the current lenient parsing (`atoi("")` is `0`). **Don't make these strict.** Route them through the existing central helpers where possible, and suppress the remaining few central call sites with `NOLINTNEXTLINE(bugprone-unchecked-string-to-number-conversion): lenient by design, maps rely on atoi semantics`.
  - **`hlvis/zones.cpp`:** check first whether it is compiled in (`ZHLT_ZONES`). Don't edit it if it isn't.

### Batch 3: Mechanical clarity fixes (2–3 h)

- **`bugprone-implicit-widening-of-multiplication-result` (37):** cast one operand to the wide type **before** multiplying, for example `(size_t)w * h`. This only changes results that would overflow `int`; note any site where that could happen with real input.
- **`bugprone-assignment-in-if-condition` (16):** move the assignment out of the condition, or make the intent explicit with a separate statement. Keep the evaluation order identical.
- **`bugprone-multi-level-implicit-pointer-conversion` (15):** add the explicit `(void*)` cast that documents the intent, typically for `free`/`memcpy` of `T**`.
- **`bugprone-switch-missing-default-case` (7):** add `default: break;`, or `default: Error(...)` if the unhandled value is impossible and the code would otherwise misbehave.
- **`clang-analyzer-deadcode.DeadStores` (6):** remove the dead store. Check that its right-hand side has no side effects.
- **`bugprone-integer-division` (4):** if the truncation is intended, make it explicit (for example `(float)(a / b)` with a comment). If it isn't, fix it, which is a behavior change: CHANGELOG entry and regression check.
- **`bugprone-terminating-continue` (4), `lightmap.cpp`:** usually `do { … continue; } while (0)`. Replace `continue` with `break` only if that is provably identical; otherwise restructure.
- **`bugprone-branch-clone` (7):** merge identical branches, unless they differ under another `#ifdef` configuration. In that case, keep them and add `NOLINTNEXTLINE(bugprone-branch-clone): branches differ under <MACRO>`.
- **`bugprone-signed-char-misuse` (1), `performance-no-int-to-ptr` (1):** fix with an explicit `(unsigned char)` cast, or `NOLINTNEXTLINE` with a reason for the deliberate thread-argument cast in `threads.cpp`.

### Batch 4: Configuration decisions (0.5 h)

Disable these in `.clang-tidy`, with a one-line comment for each:

- **`performance-enum-size` and `clang-analyzer-optin.performance.Padding`:** changing enum widths or struct layout would change memory layout for no gain in this code base.
- **`bugprone-exception-escape` and `bugprone-throwing-static-initialization`:** the tools have no exception handling strategy, and an out-of-memory `std::string` at startup ending the process is acceptable.
- **`bugprone-undefined-memory-manipulation` (`brush_t`):** the raw byte copies are intentional and documented in `hlcsg/csg.h` (warning cleanup batch 4). Either disable the check, with that reason, or use four `NOLINTNEXTLINE` lines.

### Batch 5: Lock it in (0.5 h)

1. Confirm a full lint run reports **0** findings.
2. Set `WarningsAsErrors: '*'` in `.clang-tidy`, so a new finding fails the lint build.
3. Update `docs/llms.txt` (gate wording, "zero findings"), `AGENTS.md` (gate 6: "lint: zero findings") and `README.md` if needed.
4. Add a CHANGELOG *Changed* entry.
5. Update this file's status line: complete, with the date, the final counts, and the number of `NOLINT` suppressions with their checks.

## Report back after each batch

- The finding count before and after, by check.
- Every site judged a **real bug**, with `file:line`, a one-sentence description and the fix.
- Every **suppression** added (`file:line`, check, reason) and every check disabled.
- Any behavior change, such as strict CLI number parsing, and its CHANGELOG entry.
- Gate results, including the regression output.
