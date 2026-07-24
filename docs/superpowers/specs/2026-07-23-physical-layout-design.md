# Physical Layout Alignment ("the filesystem catches up to the libraries") — Design

**Date:** 2026-07-23 · **Branch:** `arch/physical-layout`, off master @`c793e87` (fp-interiors
merged; nine libraries settled, combat DEFER 0). · **Owner decisions (2026-07-23):** layout
`src/<lib>/*.cpp` — sources and PRIVATE headers directly in the library directory, beside the
existing `include/` trees (NOT a nested `<lib>/src/`); the app-tier remainder moves to
`src/app/` in the same wave (every file gets an explicit home); **merge-when-green grant**
(pure mechanical moves, object-code-identical, gated by the full battery + six CI jobs +
whole-branch review).

## Problem / decision

Nine logical libraries exist only in `src/CMakeLists.txt` source lists; all `.cpp` files sit
flat in `src/`. Three libraries (`platform`, `core`, `persist`) already carry physical
directories for their PUBLIC headers (`src/<lib>/include/rots/<lib>/*.h`, proper
`target_include_directories`, 319 `#include "rots/..."` references) — this wave completes the
established convention: each library's sources and private headers move into `src/<lib>/`,
and the app tier moves into `src/app/`, so membership is visible in the filesystem and a
misplaced file is a visible anomaly rather than a build-list subtlety. Timing is deliberate:
membership only stabilized this week (DEFER 0, fp-interiors merged) — moving earlier would
have meant double-moves; moving now is a pure catch-up.

**Every move is content-identical (`git mv` + build-list path edit): zero object-code change,
zero behavior change, zero test-count change (baseline 1583).** Git history survives via
rename detection.

## Scope

- All nine library source sets move: `src/platform/`, `src/core/`, `src/entity/`,
  `src/persist/`, `src/world/`, `src/combat/`, `src/pathfind/`, `src/script/`, `src/olc/`.
- The app-tier remainder (every production `.cpp` in no library) moves to `src/app/`.
- **Private-header rule (owner-set: private headers move with their sources):** a header moves
  into `src/<lib>/` iff its non-test includers are all TUs of that one library; test-tree
  includers are satisfied by adding the library directories to the test targets' include
  paths. A header included by more than one library, or by app + a library (the hook/seam
  headers — e.g. `combat_hooks.h`, `entity_hooks.h`, `output_seam.h` — are expected cross-tier
  by design), is NOT private: it stays flat in `src/` this wave. Headers whose non-test
  includers are all app-tier TUs move to `src/app/` by the same rule. The T0 census computes
  the full classification from actual `#include` graphs — no hand sorting.
- Public headers: the existing `src/<lib>/include/rots/<lib>/` trees are UNTOUCHED; no new
  public-header migration this wave (that remains the incremental per-need convention).
- `src/tests/` does NOT move (the monolithic runner's relative golden paths depend on it);
  `bin/`, `lib/`, goldens, scripts untouched except path-assumption fixes T0 flags.

**Out of scope:** any code/behavior change; include-DIRECTIVE rewrites (resolution is handled
by include paths, not by editing 4000 include lines); public-header migration; renaming files;
test-tree reorganization; the `Testing/` CTest artifact dir.

## Mechanics

- Moves: `git mv` only — file content byte-identical (CRLF profiles trivially preserved).
- CMake: per-library source-list path edits; each library target (and the app/test targets)
  gains the include directories the census says its TUs need (its own dir; `src/` stays on
  every target's path so still-flat shared headers resolve; test targets gain the lib dirs).
- Flat Makefiles (parity BINDING, same-commit as each move batch): `src/Makefile` gains
  `VPATH`/`vpath %.cpp` entries per new directory plus the `-I` flags matching CMake;
  `src/tests/Makefile` gains the same `-I` set; the `format` target's `*.cpp` glob and any
  other flat-glob assumption T0 flags are fixed to cover the new directories.
- Tooling: `tools/string_view_census.py`, clang-format hooks, and any script with a flat-glob
  assumption are inventoried by T0 and fixed in the same commit as the first move they'd miss.
- Container volumes hold stale flat-layout objects: each container gate starts from a clean
  configure after the first move batch (the standing stale-object rule).

## Verification

- **Object-code identity:** for each move batch, `nm` on a sampled moved TU's object pre/post
  (identical symbol tables; the compile is the same bytes from a new path).
- Standing per-batch gates, both hosts: macOS arm64 + `rots64` full builds + ctest 1583/1583,
  both boot goldens byte-identical, all nine linkchecks, `ConvertEquivalence` 17/17,
  `string_view_census.py --check` exit 0. No ASan needed (zero test-file content changes)
  unless a test file's include lines are touched — then the standing rule applies.
- Finalization: the full i386 battery (fresh-branch run; reconciliation exact at 1583/9/23−17
  per the standing method) + all six blocking CI jobs (`windows-msvc` proves MSVC include
  resolution) + Fable whole-branch review.
- Goldens: NOTHING regenerates. Any golden or boot drift = a real bug in the move.

## Task shape

T0 census (read-only: the include graph → private/shared classification per header; the
per-library move manifests; the flat-glob/tooling inventory; the VPATH/-I design for both
build systems) → T1 build-system enablement (VPATH scaffolding, `-I` sets, tooling fixes —
consumer-free where possible, proven by a no-move green build) → T2 per-library move commits
in tier order (platform → core → entity → persist → world → combat → pathfind → script → olc;
one commit per library, gates per commit) → T3 the app-tier move (`src/app/`, may split into
2-3 commits if the census's manifest is large) → T4 docs (BUILD.md layout section, AGENTS.md
project-structure section + chain entry, playbook/spec path touch-ups per the as-built rule:
historical narration keeps old paths, load-bearing current-state references update) → T5
finalization (battery → whole-branch review → PR → **MERGE WHEN GREEN, owner grant**).

## Risks

- **Flat-Makefile parity** is the recurring gap class (seventh+ occurrence chronicled in
  AGENTS.md) — mitigated by T1 doing the build-system work FIRST, consumer-free, and by the
  battery's reconciliation method which exists precisely to catch it.
- **Include resolution divergence** (GNU vs MSVC quoted-include search): the census
  classification keeps every cross-boundary header flat, so no include directive changes
  meaning; `windows-msvc` CI proves it.
- **Stale doc paths:** T4's sweep updates load-bearing references; historical narration is
  exempt by house precedent.
- **Stale container objects:** clean configure per container after the first batch.
- An include cycle or classification surprise (a "private" header with a hidden cross-lib
  includer) surfaces as a build failure at the offending batch — fix is reclassify-to-flat,
  not redesign; a header that CANNOT be placed under the rule is a STOP with evidence.

## Process

Subagent-driven: Sonnet implementers (mechanical), Opus census + reviews, Fable whole-branch.
Scratch prefix `pl-` in `.superpowers/sdd/`, never committed. Python byte-edits for existing
build files; `git mv` for moves. Docker synchronous in subagents; battery finalization-only.
**Owner grants: merge-when-green (2026-07-23).** Baseline tests 1583 — the count must not
move at any point in this wave.

## As-built (2026-07-23)

All nine libraries plus the app tier physically homed as designed: 100 build-listed `.cpp` + 6
`account_management_*.cpp` persist unity fragments + `convert_main.cpp` moved via `git mv`; every
`#include` directive left byte-identical except the one sanctioned exception below. Top-level
`src/*.cpp` is now empty; 58 headers stay flat at top-level `src/` under the private-header rule.

**Per-library/app manifests as landed** (all match the T0 census's Step 4 manifests exactly):
`src/platform/` 10 `.cpp` + `clock.h`; `src/core/` 3 `.cpp`; `src/entity/` 8 `.cpp` +
`environment_utils.h`; `src/persist/` 14 build-listed `.cpp` + 6 unity fragments +
`legacy_salvage.h`/`stopwatch.h`; `src/world/` 4 `.cpp`; `src/combat/` 15 `.cpp`;
`src/pathfind/` 1 `.cpp`; `src/script/` 7 `.cpp`; `src/olc/` 7 `.cpp`; `src/app/` 30 build-listed
`.cpp` + `convert_main.cpp` + `delayed_command_interpreter.h`/`savebench.h`/`wait_functions.h`.

**`convert_main.cpp`'s ruled home:** `src/app/convert_main.cpp` (its `rots_convert` build-list
entry updated in the same commit as the app-tier move) — app-tier by nature (a top-level
`main()` driver), its includes (`db.h`, `platform_compat.h`, `utils.h`) all flat, so it resolves
via the standard two-flag mechanism from its new directory. Every production `.cpp` now has an
explicit `src/<tier>/` home, per the owner's decision.

**One header reclassified mid-wave:** `mob_csv_extract.h`, originally classified a mover to
`src/app/` (T0's Step 2/3 table), was reclassified to **stay flat** during T1's review —
`src/tests/rots_asprintf_tests.cpp:2` bare-quote-includes it as `"../mob_csv_extract.h"`, a
`"../"`-prefixed directive no include-path change can satisfy once the header moves (only test
includers are exempt from counting under the private-header rule, and this one is a real,
unfixable-by-`-I` obstacle, not merely an includer to route around). Only `mob_csv_extract.cpp`
moved, to `src/app/`; the header stayed at top-level `src/`. This is the wave's one true
fix-is-reclassify event at the census-review level (before any batch build); zero
build-triggered fix-is-reclassify events occurred during any of the ten `git mv` batches
themselves (T2/T3's own "Fix-is-reclassify events" sections each report none) — every other
header landed exactly where T0's census placed it on the first attempt.

**The GNU-family include mechanism evolved twice past the plan's `-idirafter`-only sketch,**
both discovered empirically mid-wave, both escalated rather than silently patched (forbidden
silent directive/mechanism edits):
1. `-idirafter <src>` alone (the plan's original design) put `src/` at the very end of the
   search list. Building the core batch broke `src/core/consts.cpp`'s bare `#include "db.h"` on
   macOS: it fell through to Xcode's own legacy `usr/include/db.h` (a Berkeley-DB compat header)
   before ever reaching `-idirafter`'s tail-position `src/db.h`. Not reproducible on `rots64`
   (Debian trixie has no such system header) — an AppleClang-SDK-specific hazard that would also
   have hit the entity batch's `db.h` includers.
2. The controller-approved fix — swap to `-iquote <src>` alone (front-of-list) — fixed `db.h` but
   broke a *different*, still-flat TU (`comm.cpp`) on `rots64`: libstdc++'s `<semaphore>` (via
   `<thread>`) does `#include_next <limits.h>` to reach glibc's real `/usr/include/limits.h`, and
   GCC's `#include_next` continuation position is sensitive to a directory's presence anywhere in
   the *quote* list (a documented GCC quirk, not a plain search-order effect) — under `-iquote`
   alone it landed on `src/`'s own project header (then still named `limits.h`) instead of
   glibc's, breaking every TU pulling in `<semaphore>`.

Both regressions were reproduced with raw compiler invocations outside CMake before proposing a
fix. The verified resolution — **both flags on the same directory** (`-iquote <src> -idirafter
<src>`) — landed as commit `b878b1b`: `-iquote`'s earlier position wins quote-form resolution
(fixes `db.h`); `-idirafter`'s tail position keeps the `#include_next` chain system-first (fixes
the `<semaphore>` regression). Verified clean on both `rots64` g++14 and native macOS AppleClang.

**The `player_limits.h` rename (owner-sanctioned, 2026-07-23) — the wave's one content-touching
commit (`314283e`).** A parallel MSVC include-resolution analysis (`.superpowers/sdd/pl-msvc-
collision-analysis.md`) found `src/limits.h` vs. the UCRT `<limits.h>` is **deterministic**
breakage under the MSVC `INCLUDE`-environment-append mechanism: MSVC has no `-idirafter`
analogue, so any directory carrying `src/limits.h` (needed for the moved TUs' quote-includes of
flat headers) is searched ahead of the system UCRT for angle-bracket resolution — no ordering
avoids it. The owner approved renaming `src/limits.h` → `src/player_limits.h` as the wave's one
authorized exception, killing the whole collision class at once (the MSVC shadow, the GCC
`#include_next` quirk above, and any future macOS SDK interaction). 23 directive sites updated
mechanically (19 bare `#include "limits.h"` + 4 test `#include "../limits.h"`; a dispatched
estimate of 24 was verified down to 23 by direct grep before executing — a discrepancy noted and
resolved by evidence, not by silently reconciling to the dispatched number). The 5 OLC
`#include <limits.h>` angle-bracket sites (`shapeobj.cpp`, `shaperom.cpp`, `shapescript.cpp`,
`shapemob.cpp`, `shapezon.cpp`) were left untouched — they correctly mean the system header.
`limits.cpp` kept its name (only the header moved). ASan ran on this commit specifically (the
standing new/changed-test-file-include rule fired, since 4 test files' include lines changed):
1583/1583, clean.

**The MSVC mechanism (T1, empirically unprovable on this host, proven by strategic CI pushes):**
`windows-msvc`'s CMake preset converted from the Visual Studio generator to **Ninja**
(`"architecture": {"strategy": "external"}`, since Ninja has no `-A`), plus a `Set up MSVC
environment` CI step (`ilammy/msvc-dev-cmd@v1`) running `vcvars` before configure, plus
`"environment": {"INCLUDE": "$penv{INCLUDE};<src abs>"}` in the preset — appending `src/` to the
*end* of the inherited `INCLUDE` environment variable, true env-search-order semantics (searched
after every `/I`, after the vcvars-populated UCRT/STL dirs). `/external:I` was evaluated and
rejected: it is searched *before* system `INCLUDE`, so it would still shadow `<limits.h>` even
post-rename. Proven green by two strategic mid-wave CI pushes (preset conversion on the
still-flat tree, then again after the first moved-TU batch + the rename), ahead of the normal T5
CI gate.

**The `clock_tests.o : ../clock.h` stale-prerequisite CI catch (commit `8995025`):** T1's
`src/tests/Makefile` restructure deleted the file's ~89 explicit per-object product rules but
missed one stray **test**-object prerequisite-only line pinning `clock_tests.o` to the
now-moved `../clock.h`; caught by the strategic i386 CI push (a plain `../clock.h` no longer
exists once `clock.h` moves to `src/platform/`) and fixed as a same-class, same-commit-scoped
fix — not a redesign of the pattern-rule restructure itself, which remained correct.

**Commit list (chain order):** scaffold `38121ee` (T1, consumer-free, inert); platform `77641e7`;
`player_limits.h` rename `314283e`; two-flag scaffold revision `b878b1b`; core `e1cf786`; entity
`1e582af`; persist `58eb2e6`; `clock_tests.o` prerequisite fix `8995025`; world `3044847`; combat
`64a71dd`; pathfind `6b9b0bf`; script `e80219b`; olc `11f618d`; app `43264f7`.

**Verification, frozen throughout:** ctest **1583/1583** both hosts (macOS arm64, `rots64`) at
every one of the wave's move/mechanism commits; `nm -C --defined-only` symbol-table diff empty
for every batch's sampled moved TU (e.g. `interpre.cpp`/`convert_main.cpp` for the app batch);
both boot goldens byte-identical throughout; `ConvertEquivalence` 17/17 throughout;
`string_view_census.py --check` exit 0 throughout, with the owner-path exceptions-doc rows for
every moved file with a documented exception updated in the same commit as its move (per-batch
row counts verified against the T0 cross-reference before editing — e.g. persist's batch updated
its full row slice, the app batch updated 7 rows across `db_boot.cpp`/`interpre.cpp`/
`protocol.cpp`); ASan run once, on the `player_limits.h` rename commit (the only commit touching
test-file include lines) — 1583/1583 clean. Goldens were never regenerated. Test count never
moved from 1583 at any point in the wave — a true zero-delta wave. The i386 finalization battery
is scoped to T5 and is **pending** as of this docs task (T4); this section will not be amended
with invented numbers — see AGENTS.md's physical-layout chain entry, which will be updated at T5
once the battery runs.
