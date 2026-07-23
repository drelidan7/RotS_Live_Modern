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
