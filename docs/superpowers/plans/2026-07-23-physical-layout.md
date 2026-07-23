# Physical Layout Alignment Wave Implementation Plan (`arch/physical-layout`)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the nine logical libraries visible in the filesystem. Every library's `.cpp`
sources and its census-classified PRIVATE headers move into `src/<lib>/` (beside the existing
`src/<lib>/include/rots/<lib>/` public-header trees, NOT a nested `<lib>/src/`), and the app-tier
remainder moves into `src/app/`. Every move is `git mv` + build-list path edit: **content
byte-identical, zero object-code change, zero behavior change, zero test-count change (baseline
1583)**. This is a pure catch-up — membership stabilized last week (combat DEFER 0, fp-interiors
merged), so the files can finally sit where their build-list membership already says they belong.

**Architecture:** Census first (T0, Opus, read-only: the `#include` graph → per-header
private/shared classification, the per-library move manifests, the flat-glob/tooling inventory, and
the exact VPATH/`-I` design for BOTH build systems + the `nm` object-identity verification design);
then build-system enablement consumer-free (T1, Sonnet: VPATH/`-I` scaffolding + tooling fixes
BEFORE any move, proven by a no-move green build — the flags are inert while files are still flat);
then per-library `git mv` commits in tier order (T2, Sonnet, one commit per library); then the
app-tier move to `src/app/` (T3, Sonnet, 2-3 commits per the census manifest); then docs (T4,
Sonnet); then finalization — i386 battery, Fable whole-branch review, **merge-when-green (owner
grant)** (T5). Spec: `docs/superpowers/specs/2026-07-23-physical-layout-design.md`. Branch:
`arch/physical-layout` off master @`c793e87` (fp-interiors merged), plan/HEAD `3e91121`.

**Tech Stack:** C++20, CMake presets + flat Makefiles (`src/Makefile` game build, `src/tests/Makefile`
monolithic runner), GoogleTest, `git mv` for every move, `nm` for object-code-identity sampling,
Python 3 byte-edits for `CMakeLists.txt`/`Makefile`/`.md` build+doc files. No new library, no new
seam header, no new linkcheck, no new test — the nine `*LayerAcyclicity` linkchecks and the 1583
test count are invariants, not deltas.

## Architecture note — what actually changes, and why it is safe

- **Source lists (CMake):** each of the nine `ROTS_<LIB>_SOURCES` lists in `src/CMakeLists.txt`
  currently holds bare filenames (`config.cpp`) resolved against `CMAKE_CURRENT_SOURCE_DIR` (`src/`).
  After a file moves to `src/core/config.cpp`, its list entry becomes `core/config.cpp`. `ageland`,
  `ageland_tests`, and `rots_convert` pick the change up automatically (they expand the same list
  variables). `ROTS_TEST_SOURCES` (`tests/*.cpp`) and the `tests/*_linkcheck_main.cpp` files DO NOT
  change — `src/tests/` never moves.
- **Include resolution (the crux):** every moved TU still bare-quote-includes flat shared headers
  (`"utils.h"`, `"comm.h"`, `"db.h"`, `"handler.h"`, the hook/seam headers). Today those resolve via
  the compiler's implicit "directory of the including file" = `src/`. Once a TU lives in `src/<lib>/`,
  that implicit entry is `src/<lib>/`, so **`src/` must be added to every moving target's include
  path** (`ageland`, `rots_convert`, and each of the nine `rots_*` libraries). The private headers
  that move WITH a library resolve intra-lib via the moved TU's own "directory of including file"
  (`src/<lib>/`) — no extra path needed for those. **No `#include` DIRECTIVE changes** (that is the
  spec's hard rule); resolution is `-I`/VPATH only.
- **The `src/`-on-the-include-path hazard:** `src/limits.h` (a project header, player-rank constants)
  must NOT shadow the standard `<limits.h>`. `src/CMakeLists.txt`'s `ageland_tests` block already
  solves this for the test TUs with `-idirafter ${CMAKE_CURRENT_SOURCE_DIR}` (GNU-family: puts `src/`
  at the END of the merged search list, after the system dirs, so `#include_next`/`<semaphore>`/
  `<limits.h>` resolve to glibc first). T0 designs the identical `-idirafter src/` treatment for the
  moved PRODUCT TUs on GNU-family compilers. MSVC (which builds `ageland` + the libs, not the tests)
  has no `-idirafter`; T0 must design and `windows-msvc` CI must prove the MSVC include ordering — see
  Risks and the flagged uncertainty below.
- **Object-code identity:** a TU compiled from `src/core/config.cpp` produces the same object bytes as
  from `src/config.cpp` — same source, same flags, only the path differs. `nm` on a sampled moved
  object pre/post proves it per batch.

## Current-tree facts the census consumes (verified at HEAD `3e91121`)

Per-library `.cpp` counts from the `src/CMakeLists.txt` source lists (T0 owns the exact manifests
including the private headers that move with each; these counts are the `.cpp` skeleton):

| Tier order | Library | CMake list | `.cpp` count | Examples |
|---|---|---|---|---|
| 1 | `rots_platform` (L0) | `ROTS_PLATFORM_SOURCES` | 10 | `rots_net.cpp`, `rots_util.cpp`, `rots_log.cpp` |
| 2 | `rots_core` (L1) | `ROTS_CORE_SOURCES` | 3 | `config.cpp`, `consts.cpp`, `output_seam.cpp` |
| 3 | `rots_entity` (L2) | `ROTS_ENTITY_SOURCES` | 8 | `entity_lifecycle.cpp`, `char_utils.cpp`, `placement.cpp` |
| 4 | `rots_persist` (L3) | `ROTS_PERSIST_SOURCES` | 14 | `db_players.cpp`, `account_management.cpp`, `objects_json.cpp` |
| 5 | `rots_world` (L3) | `ROTS_WORLD_SOURCES` | 4 | `db_world.cpp`, `zone.cpp`, `zone_load.cpp`, `weather.cpp` |
| 6 | `rots_combat` (L3) | `ROTS_COMBAT_SOURCES` | 15 | `fight.cpp`, `visibility.cpp`, `spell_pa.cpp` |
| 7 | `rots_pathfind` (L4) | `ROTS_PATHFIND_SOURCES` | 1 | `graph.cpp` |
| 8 | `rots_script` (L4) | `ROTS_SCRIPT_SOURCES` | 7 | `mudlle.cpp`, `mobact.cpp`, `spec_pro.cpp` |
| 9 | `rots_olc` (L4) | `ROTS_OLC_SOURCES` | 7 | `shapemob.cpp`, `editor_hooks.cpp`, `shapescript.cpp` |
| — | app (`ageland`) | `ROTS_SERVER_SOURCES` | 30 | `comm.cpp`, `handler.cpp`, `interpre.cpp`, `utility.cpp` |

Total library `.cpp` = 69; app `.cpp` = 30; plus `convert_main.cpp` (a production `.cpp` in NO
library — `rots_convert`'s sole direct source). **T0 rules `convert_main.cpp`'s home** (expected
`src/app/`, so every production file has an explicit home per the owner decision). Private headers
move on top of these counts; the census computes the exact set from real `#include` graphs.

Key build-file locations T0/T1/T2 edit (verified):

- `src/CMakeLists.txt`: nine `ROTS_*_SOURCES` lists (`ROTS_PLATFORM_SOURCES`:95, `CORE`:134,
  `ENTITY`:163, `PERSIST`:189, `WORLD`:252, `COMBAT`:459, `PATHFIND`:497, `SCRIPT`:707, `OLC`:825),
  `ROTS_SERVER_SOURCES`:758, `rots_convert` `convert_main.cpp`:1224; existing include-dir calls
  `rots_platform PUBLIC platform/include`:988, `rots_core PUBLIC core/include`:1001, `rots_entity
  PRIVATE persist/include`:1018, `rots_persist PUBLIC persist/include`:1039, `ageland_tests PRIVATE
  core/include persist/include platform/include`:1472; the `ageland_tests` `-idirafter
  ${CMAKE_CURRENT_SOURCE_DIR}` / `-iquote .../tests` handling via `set_source_files_properties`:1462;
  `ROTS_TEST_SOURCES`:1252ff; nine `add_test(NAME *LayerAcyclicity …)` linkcheck blocks :1565-1905.
  **`ageland` has NO `target_include_directories` of its own today** (it reaches `core/include`/
  `persist/include`/`platform/include` transitively via the `RotS::*` PUBLIC links, and flat headers
  via "directory of including file"). T1 adds `src/` to `ageland` (and each library + `rots_convert`).
- `src/Makefile` (game build): `ALL_CPPFLAGS = $(DEPFLAGS) -Icore/include -Ipersist/include
  -Iplatform/include $(CPPFLAGS)`:48; `OBJNAMES` (bare basenames):58-64; the single pattern rule
  `$(OBJDIR)/%.o: %.cpp`:89; the `format:` target (`clang-format -i … *.cpp` / `*.h`):111-113. No
  `VPATH` today (everything is flat). Objects land in `$(OBJDIR)` by BASENAME, so **OBJNAMES stay
  bare** — only `vpath %.cpp <dirs>` + the `-idirafter .` include root are added.
- `src/tests/Makefile` (monolithic runner): `CXXFLAGS` carries `-I../core/include
  -I../persist/include -I../platform/include`:40; `TEST_CXXFLAGS = $(CXXFLAGS) -idirafter ..`:89;
  product objects (`OBJFILES`:94-100) are each built by an **explicit per-object rule hardcoding
  `../foo.cpp`** (~90 rules, lines 105-340, e.g. `account_management.o : ../account_management.cpp …`
  `$(CXX) -c $(CXXFLAGS) ../account_management.cpp`); test objects (`SRCS`:350) build via the
  `.cpp.o:` suffix rule:391. **This file is the wave's hardest parity surface** — see the T1
  monolithic-runner note.
- `tools/string_view_census.py`: discovers files by `rglob("*")` under `src/` (line 467/546) and
  skips `src/tests/` (line 475) — so it FINDS moved files automatically. BUT its `--check` mode keys
  every documented exception on the **owner path** (`columns[0]`, e.g. `src/rots_util.cpp`) matched
  against the file's repo-relative `display_path` (line 536/475). `docs/superpowers/string-view-
  exceptions.md` currently lists ~34 owner paths, many of them moving `.cpp` (e.g.
  `src/account_management.cpp` → `src/persist/…`, `src/rots_util.cpp` → `src/platform/…`,
  `src/entity_lifecycle.cpp` → `src/entity/…`, `src/interpre.cpp` → `src/app/…`) and several `.h`
  whose move depends on the census. **Every moved file with an exception must have its owner-path
  column updated in the SAME commit as the move, or `--check` fails.** T0 inventories the exact
  path→batch cross-reference; each T2/T3 commit applies its slice (a doc edit, no semantic change).
- Root `Makefile` (`make test`): builds named targets + the nine linkchecks by target name, runs
  `ctest`; NO per-`.cpp` path references — unaffected by moves. `scripts/i386-battery.sh`: references
  only the `src/tests` monolithic-runner directory and `make` targets, NO per-file paths —
  unaffected. `scripts/boot-golden.sh`: build+boot, no per-file paths — unaffected.
- Clang-format PostToolUse hook (`.claude/settings.json`): runs `clang-format -i` only on
  `Write|Edit` of a source file. **`git mv` does not trigger it** (not a Write/Edit), and no source
  CONTENT is edited this wave, so the hook never fires on a moved `.cpp`/`.h`. The root `.clang-format`
  applies recursively, so moved files stay covered. No hook change needed (T0 confirms).

## Global Constraints

- **`git mv` ONLY — content byte-identical.** Every file move is a `git mv`; not one byte of any
  `.cpp`/`.h` CONTENT changes this wave (CRLF profiles preserved trivially — `git mv` does not touch
  bytes). Git rename detection preserves history. A move that requires editing the file's content to
  build is a classification error → reclassify (see the fix-is-reclassify rule) or STOP.
- **Include-DIRECTIVE edits are FORBIDDEN** — resolution is via `-I`/VPATH only. The ONE exception:
  if T0 PROVES a specific directive must change (a header that genuinely cannot resolve any other
  way), it is listed explicitly as a census exception with evidence and its own line item; a silent
  directive edit is a review-blocking finding.
- **Zero object-code change (verified per batch).** For each move batch, `nm` on a sampled moved
  TU's object pre-move vs post-move must show identical symbol tables. T0 designs which TU per batch
  is sampled and the exact commands (`nm -C` on the built `.o`, before/after diff clean).
- **Test count FROZEN at 1583, both hosts.** No new test is expected (pure moves). ANY delta — up OR
  down — is a bug → STOP. `ConvertEquivalence` stays 17/17. The nine `*LayerAcyclicity` linkchecks
  stay nine and stay green.
- **Goldens: NOTHING regenerates.** Both boot goldens (`docs/superpowers/goldens/boot-log.golden`,
  via `scripts/boot-golden.sh verify`) stay byte-identical; the combat characterization goldens
  (`src/tests/goldens/`), `ConvertEquivalence`, and every `legacy_*_fixture.bin` are untouched. Any
  golden or boot drift = a real bug in a move → STOP. NEVER run `UPDATE_GOLDENS=1` this wave.
- **Flat-Makefile parity is BINDING and same-commit.** Every move batch that changes what
  `src/CMakeLists.txt` compiles must land the equivalent `src/Makefile` and `src/tests/Makefile`
  change (VPATH entries; the monolithic runner's per-object path resolution) in the SAME commit.
  This is the recurring gap class (seventh+ occurrence chronicled in AGENTS.md) — T1 front-loads the
  build-system work consumer-free precisely to shrink it, and the i386 battery's reconciliation
  method exists to catch any residue.
- **Per-task dual-host gates (both hosts, every task that builds).** macOS arm64 (`cmake --preset
  macos-arm64` + build + `ctest --preset macos-arm64`) AND `rots64` (`docker compose run --rm
  --pull never rots64 … cmake --preset linux-x64 && build && ctest`); both boot goldens byte-identical
  (`scripts/boot-golden.sh --native build/macos-arm64/ageland verify` and `scripts/boot-golden.sh
  --service rots64 verify`); all NINE `*LayerAcyclicity` linkchecks green; `ConvertEquivalence`
  17/17; `python3 tools/string_view_census.py --check` exit 0; ctest **1583/1583**. Docker gates run
  SYNCHRONOUSLY in subagents.
- **No ASan needed** (zero test-file CONTENT changes). The standing new/changed-test-file ASan rule
  (`cmake --preset macos-arm64-asan`) applies ONLY if a T0-surfaced edge forces a test file's
  include lines to change — T0 should design the include paths so it does NOT (test TUs already reach
  `src/` via `-idirafter ..` and reach moved private headers by adding the lib dirs to the test
  targets' include paths, NOT by editing includes). If a test-file content edit becomes unavoidable,
  that task runs the ASan gate.
- **Clean container configure after the first move batch.** Container volumes hold stale flat-layout
  objects (`rots-build-i386`/`rots-build-x64`); the standing stale-object rule — each container gate
  starts from a clean configure once files have moved. A stale object linking a moved TU's old path
  is a battery/gate failure, investigated as real, never tolerated.
- **`src/tests/` does NOT move**; the public `src/<lib>/include/rots/<lib>/` header trees are
  UNTOUCHED (no new public-header migration this wave); `src/Testing/` (CTest artifact dir), `bin/`,
  `lib/`, goldens, and scripts are untouched except the path-assumption fixes T0 flags.
- **Private-header rule (owner-set).** A header moves into `src/<lib>/` iff its non-test includers are
  ALL TUs of that one library. Test-tree includers are satisfied by adding the library dirs to the
  test targets' include paths, NOT by counting as includers. A header included by >1 library, or by
  app + a library (the cross-tier hook/seam headers — `combat_hooks.h`, `entity_hooks.h`,
  `output_seam.h`, `script_hooks.h`, `editor_hooks.h`, `persist_hooks.h`, `world_hooks.h`, plus the
  pervasive `utils.h`/`comm.h`/`db.h`/`handler.h`) is NOT private → it STAYS FLAT in `src/`. A header
  whose non-test includers are all app-tier TUs moves to `src/app/`. The T0 census computes the full
  classification from actual `#include` graphs — no hand sorting.
- **Fix-is-reclassify, not redesign.** A "private" header that surfaces a hidden cross-lib includer
  at its batch's build (a `*LayerAcyclicity` or ordinary link failure naming it) is reclassified to
  flat (`git mv` it back to `src/`, drop its list/path edit) — a same-batch local fix, not a STOP. A
  header that fits NOWHERE under the private-header rule (genuinely needs a directive change or a new
  home the rule does not describe) is a **STOP with evidence** to the owner.
- **Python byte-edits for the build/doc files** (`CMakeLists.txt`, both `Makefile`s, `.md`) — measure
  each file's line-ending profile before editing and preserve it. `git mv` is the only mechanism for
  the source moves themselves.
- **`-Wall -Wextra -Werror` clean** (`/W4 /WX` on MSVC); `-funsigned-char` / `/J`; deterministic-FP
  flags unchanged; no `rand()`/`random()`; `std::format` unchanged. This wave adds no code, so these
  are inherited invariants — a warning appearing after a move means the move changed a compile
  (impossible for a pure `git mv` + path edit) → investigate as a real regression.
- **i386 battery finalization-only** (`scripts/i386-battery.sh`, sequential, fresh-branch full run;
  per MEMORY 60-90+ min on this Apple Silicon host, can hang — never per-task, never concurrent). A
  monolithic-runner SIGSEGV is never tolerated (clean container rebuild first, then investigate).
- **STOP-and-adjudicate:** an include cycle no reclassify resolves; a header that fits no home; a
  directive that must change beyond a T0-listed exception; MSVC include-resolution divergence
  `windows-msvc` CI surfaces that no `-I` ordering fixes; any object-code, test-count, or golden
  drift. Named STOPs go to the owner despite the merge grant.
- **Merge authority: MERGE-WHEN-GREEN (owner grant, 2026-07-23).** Unlike fp-interiors, this wave
  DOES merge at the end: T5 pushes, requires the six blocking CI jobs green, then ff-merges and
  deletes the branch. The moves are pure mechanical, object-code-identical, and gated by the full
  battery + CI + whole-branch review.
- Baseline tests **1583** both hosts. Implementers **Sonnet**; T0 census + heavy reviews **Opus**;
  whole-branch review **Fable**. Scratch prefix **`pl-`** in `.superpowers/sdd/`, gitignored, never
  committed. Docker synchronous in subagents.

---

### Task 0: Include-graph census + classification + build-system design (read-only, Opus)

**Files:**
- Create: `.superpowers/sdd/pl-census.md` (gitignored scratch).

**Interfaces:**
- Consumes: `src/CMakeLists.txt`'s nine source lists + `ROTS_SERVER_SOURCES` + `rots_convert`; the
  actual `#include` graph of every production `.cpp`/`.h` under `src/` (excluding `src/tests/`); the
  spec's private-header rule and tier order.
- Produces: the per-header private/shared/app classification; the per-library and app move manifests
  (exact `.cpp` + private-`.h` lists); the tooling/flat-glob inventory with per-batch fix
  assignments; the exact VPATH/`-I` flag design for BOTH build systems and BOTH compiler families;
  the `nm` object-identity verification design; `convert_main.cpp`'s ruled home. Every later task
  cites this file.

- [ ] **Step 1: Build the base.** `cd src && cmake --preset macos-arm64 && cmake --build --preset
  macos-arm64 -j4 && ctest --preset macos-arm64`. Confirm the base: nine libraries, nine
  `*LayerAcyclicity` linkchecks, **1583** tests, `ConvertEquivalence` 17/17, HEAD `3e91121` + this
  plan commit. Record the baseline numbers.
- [ ] **Step 2: Build the include graph.** For every production `.cpp`/`.h` under `src/` (NOT
  `src/tests/`), enumerate its non-test includers (`grep -rn '#include "<header>"' src/` excluding
  `src/tests/`). Produce, per header, the set of libraries/app-tiers that include it. Record the raw
  graph in `pl-census.md`.
- [ ] **Step 3: Classify every header.** Apply the private-header rule mechanically:
  private-to-one-library (all non-test includers are that library's TUs) → moves with the library;
  cross-boundary (>1 library, or app + a library, or a hook/seam header) → **STAYS FLAT** in `src/`;
  app-only → moves to `src/app/`. Produce the definitive classification table. Explicitly list the
  headers that STAY FLAT (expected: `utils.h`, `comm.h`, `db.h`, `handler.h`, `interpre.h`,
  `spells.h`, `limits.h`, `protos.h`, and every `*_hooks.h`/seam header) with the includer evidence
  that keeps each flat. Flag any header the rule cannot place (STOP candidate).
- [ ] **Step 4: Per-library + app move manifests.** For each of the nine libraries (tier order) and
  the app tier, produce the exact list of `.cpp` files (from the CMake source lists) plus the
  private headers Step 3 assigned to it. Rule `convert_main.cpp`'s home (expected `src/app/`). These
  manifests are what T2/T3 `git mv`. State per-manifest whether any TU includes ZERO flat `src/`
  header (would not need `src/` on its path — informational; the include design still adds `src/`
  uniformly).
- [ ] **Step 5: The build-system include/VPATH design.** Design, precisely, for BOTH build systems
  and BOTH compiler families:
  - **CMake:** the `target_include_directories` addition that puts `src/` on each moving target's
    path (`ageland`, `rots_convert`, and each of the nine `rots_*` libraries). Mirror the existing
    `ageland_tests` solution — GNU-family `-idirafter ${CMAKE_CURRENT_SOURCE_DIR}` via
    `set_source_files_properties`/`COMPILE_OPTIONS` (NOT a plain `target_include_directories`, which
    would poison the angle-bracket path and shadow `<limits.h>`). Design the MSVC path (MSVC has no
    `-idirafter`; specify the exact `/I` or ordering that keeps `src/limits.h` from shadowing
    `<limits.h>` for angle includes, and note `windows-msvc` CI is the proof). Specify whether the
    per-library private-header dir needs its own entry (it does NOT — intra-lib includes resolve via
    the moved TU's own directory) and whether the test targets need the lib dirs added so test TUs
    reach moved private headers (they do — design that addition WITHOUT editing any test-file
    include line).
  - **`src/Makefile` (game build):** the `vpath %.cpp <dir>` lines for the nine lib dirs + `src/app/`
    (OBJNAMES stay bare — objects land in `$(OBJDIR)` by basename), and the `-idirafter .` include
    root added to `ALL_CPPFLAGS` (NOT `-I.`: the game build pulls `<semaphore>` via `comm.cpp`'s
    `<thread>` under g++14/C++20, the same `#include_next`/`<limits.h>` chain the test Makefile's
    `-idirafter ..` note documents). Confirm no two moving basenames collide (they don't).
  - **`src/tests/Makefile` (monolithic runner) — the hardest surface.** The ~90 explicit per-object
    rules hardcode `../foo.cpp`; a move breaks each. Design the restructure: **replace the redundant
    explicit per-object recipes with a `vpath %.cpp .. ../platform ../core ../entity ../persist
    ../world ../combat ../pathfind ../script ../olc ../app` + a generic product pattern rule** using
    a flag set that includes `-idirafter ..` (so a moved product TU reaches flat `src/` headers). The
    file's own comments state the hand rules are redundant (superseded by `-MMD -MP` auto-deps), so
    dropping them is safe and — critically — INERT for still-flat files (the pattern rule + `vpath ..`
    reproduces the same compile for a file still in `src/`). Rule whether product objects unify onto
    `TEST_CXXFLAGS` or a dedicated product-flags var; verify `-idirafter ..` is a no-op for a
    still-flat product TU (its own directory entry finds siblings first). Design this as a T1
    consumer-free restructure provable by a no-move green monolithic build.
- [ ] **Step 6: Tooling / flat-glob inventory + per-batch fix assignment.**
  - `tools/string_view_census.py`: confirm `rglob` finds moved files; produce the **exact
    owner-path → batch cross-reference** — for every row in `docs/superpowers/string-view-
    exceptions.md` whose `columns[0]` path names a file that moves, record its old path, new path,
    and the T2/T3 batch that must update it in-commit. (This is the census `--check` coupling — the
    single largest per-batch bookkeeping item.)
  - `src/Makefile` `format:` target: the `*.cpp`/`*.h` glob misses `src/<lib>/` after moves — assign
    its fix to T1 (broaden to cover the new dirs, e.g. `*.cpp */*.cpp` or a `find`).
  - Confirm root `Makefile`, `scripts/i386-battery.sh`, `scripts/boot-golden.sh`, CMakePresets, and
    the clang-format hook carry NO per-`.cpp` path assumption (verified: they don't) — record the
    confirmation so T4 can assert it.
  - Sweep for any OTHER flat-glob assumption (CI workflow files, other scripts) that enumerates
    `src/*.cpp`; assign any hit to T1 or its first affected batch.
- [ ] **Step 7: Object-code-identity verification design.** Name the ONE sampled TU per move batch
  (nine libraries + app sub-batches) and the exact `nm` command sequence: capture `nm -C` on the
  built object BEFORE the batch (from the flat build), `git mv` + list edit, rebuild, capture `nm -C`
  AFTER, diff → must be empty. Prefer a TU with rich symbols (e.g. `fight.cpp.o` for combat,
  `db_players.cpp.o` for persist) over a data-only TU. Specify where the object lives per build
  (`build/*/CMakeFiles/…` for CMake; `../intermediate/*.o` for the flat Makefile).
- [ ] **Step 8: Write the census.** Finalize `pl-census.md`: the classification table, the ten move
  manifests (nine libs + app, with `convert_main.cpp`'s home ruled), the STAY-FLAT header list with
  evidence, the full CMake + both-Makefile include/VPATH design (both compiler families), the
  tooling inventory with per-batch fix assignments (especially the string-view-exceptions path
  cross-reference), and the `nm` sampling plan. Flag any STOP candidate (a header fitting no home; a
  directive that must change) with evidence. No code/build changes this task.

---

### Task 1: Build-system enablement (consumer-free, Sonnet)

> All flags are added while files are STILL FLAT, so they are inert (a `vpath`/`-I` entry pointing at
> a dir that does not yet hold the file, and `-idirafter .`/`..` that a still-flat TU never needs,
> change no resolved header set). Proven by a full green build with ZERO files moved. Flat-parity is
> BINDING same-commit.

**Files:**
- Modify: `src/CMakeLists.txt` (add `src/` to `ageland`, `rots_convert`, and the nine `rots_*`
  library targets per T0's design; add the lib dirs to the test targets so test TUs will reach moved
  private headers; Python byte-edit).
- Modify: `src/Makefile` (add `vpath %.cpp <nine lib dirs + app>`; add `-idirafter .` to
  `ALL_CPPFLAGS`; broaden the `format:` glob per T0).
- Modify: `src/tests/Makefile` (the T0-designed restructure: `vpath %.cpp ..` + all target dirs, a
  generic product pattern rule with `-idirafter ..`, and removal of the redundant explicit per-object
  `../foo.cpp` rules).
- Modify: any other flat-glob tooling T0 assigned here (e.g. CI workflow globs, if flagged).

**Interfaces:**
- Consumes: `pl-census.md` Step 5/Step 6 (the exact flag sets and tooling fixes).
- Produces: an include/VPATH scaffold under which a file CAN live in `src/<lib>/` and still build in
  BOTH systems, verified inert against the still-flat tree.

- [ ] **Step 1:** Apply the CMake include-dir additions (Python byte-edit): `src/` onto `ageland`,
  `rots_convert`, and each `rots_*` library via the T0-designed GNU `-idirafter`/MSVC mechanism;
  the lib-dir additions onto `ageland_tests` (and any linkcheck target that will compile a moved
  private-header includer, per T0). Do NOT touch the existing `include/`-tree entries.
- [ ] **Step 2:** Apply the `src/Makefile` changes: `vpath %.cpp` for the nine lib dirs + `src/app/`;
  `-idirafter .` in `ALL_CPPFLAGS`; the broadened `format:` glob. OBJNAMES stay bare.
- [ ] **Step 3:** Apply the `src/tests/Makefile` restructure per T0 Step 5: add the `vpath %.cpp`
  search list, the generic product pattern rule with `-idirafter ..`, and remove the redundant
  explicit `../foo.cpp` per-object recipes. Verify the auto-dep (`-MMD -MP`) header tracking is
  preserved.
- [ ] **Step 4: No-move green build, BOTH hosts.** Full dual-host gates (macOS arm64 + `rots64`:
  build + ctest **1583/1583**), both boot goldens byte-identical, nine linkchecks, `ConvertEquivalence`
  17/17, `string_view_census.py --check` exit 0. ALSO build the flat monolithic runner green on
  `rots64` (`cd src/tests && make clean && make tests`) to prove the restructured Makefile compiles
  every product+test TU with zero files moved. Zero files have moved — this is the inertness proof.
- [ ] **Step 5:** Commit (build-system scaffold + tooling fixes, one logical commit; flat-parity is
  intrinsic — both build systems edited together). Grep-verify the CMake/both-Makefile changes are
  present and mutually consistent (same `src/` include intent, same VPATH dir set).

---

### Task 2: Per-library moves in tier order (Sonnet, one commit per library)

> Tier order (nine commits): **platform → core → entity → persist → world → combat → pathfind →
> script → olc.** Each commit is a `git mv` batch + `src/CMakeLists.txt` list-path edit + (if T0's
> Makefile design needs a per-batch VPATH touch — it should not, T1 front-loaded the full dir list)
> a flat-Makefile edit, + the string-view-exceptions owner-path updates for any moved file with a
> documented exception (same commit) + the `nm` object-identity sample. Full dual-host gates per
> commit. The FIRST commit (platform) is the first move → start each container gate from a CLEAN
> configure thereafter (stale-object rule).

**Files (per commit `<lib>`):**
- Move (`git mv`): `src/<file>.cpp` → `src/<lib>/<file>.cpp` for every `.cpp` in the manifest; each
  private header → `src/<lib>/<header>.h`.
- Modify: `src/CMakeLists.txt` (`ROTS_<LIB>_SOURCES` entries → `<lib>/<file>.cpp`; Python byte-edit).
- Modify (only if T0's design requires a per-batch flat-Makefile touch): `src/Makefile` /
  `src/tests/Makefile` — expected NONE, since T1 added the full `vpath` dir set up front; if a batch
  needs one, it is BINDING same-commit.
- Modify: `docs/superpowers/string-view-exceptions.md` (owner-path column for any moved file with an
  exception, per T0 Step 6's cross-reference; same commit).

**Interfaces:**
- Consumes: `pl-census.md` (the per-library manifest, the `nm` sample TU, the exceptions
  cross-reference).
- Produces: `src/<lib>/` populated with that library's sources + private headers; membership visible
  in the filesystem; object code identical.

- [ ] **Step 1 (per library, tier order):** `git mv` the manifest's `.cpp` + private headers into
  `src/<lib>/`. Edit the `ROTS_<LIB>_SOURCES` entries to `<lib>/…` paths (Python byte-edit). Apply any
  string-view-exceptions owner-path updates for moved files in THIS commit.
- [ ] **Step 2 (per library):** `nm` object-identity sample per T0 Step 7 — the named TU's object
  pre/post diff must be empty. Record the diff-clean evidence in the task report.
- [ ] **Step 3 (per library):** Full dual-host gates (macOS arm64 + `rots64`, CLEAN container
  configure): build + ctest **1583/1583**, both boot goldens byte-identical, **all nine
  `*LayerAcyclicity` linkchecks green**, `ConvertEquivalence` 17/17, `string_view_census.py --check`
  exit 0. Fix-is-reclassify on a surprise cross-lib includer (a linkcheck/link failure naming a
  header): `git mv` that header back to `src/`, drop its list/path edit, re-run — a same-commit local
  fix, NOT a STOP. A header fitting no home = STOP with evidence. Commit per library.
- [ ] **Step 4:** After all nine library commits, confirm `src/` no longer holds any library `.cpp`
  (only flat shared headers + the app-tier `.cpp` awaiting T3 + `convert_main.cpp` per its ruled
  home). Record the per-commit gate + `nm` evidence for the T5 report.

---

### Task 3: App-tier move to `src/app/` (Sonnet, 2-3 commits per census)

> The census's app manifest rules the split (a single commit if small; 2-3 grouped commits — e.g. by
> subsystem — if the ~30-file manifest is large). Same gates per commit. `convert_main.cpp` moves
> here if T0 ruled `src/app/` as its home (expected).

**Files (per commit):**
- Move (`git mv`): app `.cpp` (subset per the commit's grouping) → `src/app/…`; app-private headers
  (census-classified app-only) → `src/app/…`; `convert_main.cpp` → `src/app/` if so ruled.
- Modify: `src/CMakeLists.txt` (`ROTS_SERVER_SOURCES` entries → `app/…`; `rots_convert`'s
  `convert_main.cpp` → `app/convert_main.cpp` if moved; Python byte-edit).
- Modify: `docs/superpowers/string-view-exceptions.md` (owner-path updates for moved app files with
  exceptions — e.g. `src/interpre.cpp`, `src/protocol.cpp`; same commit).

**Interfaces:**
- Consumes: `pl-census.md` (app manifest + split + `convert_main.cpp` ruling + exceptions
  cross-reference + `nm` sample).
- Produces: `src/app/` populated; every production `.cpp` now has an explicit `src/<tier>/` home;
  `src/` holds only the flat cross-boundary shared headers.

- [ ] **Step 1:** `git mv` the commit's app `.cpp` + app-private headers into `src/app/`; edit
  `ROTS_SERVER_SOURCES` (and `rots_convert`) list paths; apply the commit's string-view-exceptions
  owner-path updates.
- [ ] **Step 2:** `nm` object-identity sample (T0-named app TU, e.g. `handler.cpp.o` or `comm.cpp.o`)
  — pre/post diff empty.
- [ ] **Step 3:** Full dual-host gates (clean container configure): build + ctest **1583/1583**, both
  boot goldens byte-identical, nine linkchecks green, `ConvertEquivalence` 17/17, census `--check`
  exit 0. Fix-is-reclassify on a surprise cross-lib/cross-tier includer of an app-private header.
  Commit per grouping.
- [ ] **Step 4:** After the app commits, confirm `src/` (top level) holds ONLY the flat shared headers
  T0 classified STAY-FLAT (no production `.cpp` left at `src/` top level; `src/tests/` and the
  `src/<lib>/include/` trees unchanged). Record for T5.

---

### Task 4: Docs (Sonnet)

**Files:**
- Modify: `docs/BUILD.md` — add a **"Physical layout"** section (the `src/<lib>/*.cpp` convention;
  the private-header-moves-with-its-library rule; the cross-boundary-header-stays-flat rule; the
  `src/`-on-the-include-path `-idirafter` mechanism and why it avoids the `<limits.h>` shadow; the
  `src/app/` app-tier home; `src/tests/` and the public `include/` trees explicitly out of scope).
  Update load-bearing current-state path references elsewhere in BUILD.md (e.g. "Library layering",
  "Container build isolation") where they name a moved file's `src/<file>.cpp` path as CURRENT;
  historical narration keeps old paths (house precedent).
- Modify: `AGENTS.md` — update the "Project Structure & Module Organization" `src/` bullet to describe
  the `src/<lib>/` layout; add a **SHORT** Testing-Guidelines chain entry for this wave as an
  explicit **zero-delta** entry (test count UNCHANGED at **1583** both hosts; the wave is pure moves,
  no membership/DEFER change — combat row stays DONE; note it moved the nine libraries' sources +
  app into `src/<lib>/`/`src/app/` and cite the finalization i386 numbers once T5 measures them, or
  mark "pending T5" until then). Match the house chain style (dense, per-wave, reconciled).
- Modify: `docs/superpowers/specs/2026-07-23-physical-layout-design.md` — append an **As-built**
  section (the final per-library/app manifests as landed; `convert_main.cpp`'s ruled home; any
  header reclassified-to-flat at its batch; the MSVC include-ordering resolution; any STOP raised and
  its disposition; the string-view-exceptions path-update count).
- Modify: load-bearing current-state path references in other docs the census flags (playbook, other
  specs) where they cite a moved file's OLD `src/<file>.cpp` path as CURRENT; historical narration is
  EXEMPT (house precedent — do not rewrite past-tense wave narration).

**Interfaces:**
- Consumes: the T2/T3 as-landed manifests and gate reports; T0's classification and tooling
  inventory; the T5 i386 numbers (or "pending T5").

- [ ] **Step 1:** Write the BUILD.md "Physical layout" section and the AGENTS.md project-structure +
  zero-delta chain entry. Python byte-edit any `.md` under a CRLF profile (measure first).
- [ ] **Step 2:** Sweep load-bearing current-state path references (BUILD.md, playbook, specs);
  update CURRENT-state citations to `src/<lib>/…`; leave historical narration untouched. Append the
  spec As-built section.
- [ ] **Step 3:** Gates: docs-only needs no build gate, but run `string_view_census.py --check` (exit
  0) and, if any `src` comment/build file was touched incidentally, the full dual-host gate once.
  Commit (docs; the As-built spec append may be a separate commit).

---

### Task 5: Finalization (i386 battery → Fable whole-branch review → CI-green PR → MERGE-WHEN-GREEN)

- [ ] **Step 1: i386 battery** — `scripts/i386-battery.sh` (sequential, fresh-branch full run; per
  MEMORY 60-90+ min on this Apple Silicon host, can hang — never concurrent). Because this is a
  first run at a moved HEAD, the container must configure CLEAN (stale flat-layout objects in the
  `rots-build-i386` volume would name old paths — the stale-object rule). Expect ctest **1583/1583**
  with the same 6 did-not-run skips; the monolithic reconciliation exact (1551 passed + 23 skipped =
  1574 of 1583 gtest-visible; the remaining 9 are the CMake-only linkchecks; 23 − 17 monolithic-only
  `PerRace/ConvertEquivalence.*` = the identical 6-test remainder both ways — the standing method).
  The restructured monolithic Makefile is the battery's specific target this wave: run 1 is the one
  place the flat-Makefile parity (VPATH + the dropped explicit rules) is proven end-to-end. A
  monolithic SIGSEGV = clean rebuild + investigate, never tolerate. Boot golden matches.
- [ ] **Step 2: Whole-branch review (Fable)** — verifies: every move is a pure `git mv` (no `.cpp`/
  `.h` CONTENT byte changed — `git log --stat`/`git show` per commit shows renames only); no
  `#include` DIRECTIVE changed (except any T0-listed proven exception); the `nm` samples are
  diff-clean per batch; test count is 1583 at every commit; goldens untouched; the private-header
  classification is sound (no cross-boundary header wrongly moved); the flat-Makefile restructure
  preserves the exact compiles; the string-view-exceptions path updates are complete (census
  `--check` clean). Doc-only fixes during the battery are OK; any `src`/build-file fix invalidates
  the battery → re-run Step 1.
- [ ] **Step 3: Push + PR + CI** — open the PR; require all six blocking CI jobs green (`legacy-32bit`,
  `linux-x64`, `sanitize-linux`, `macos-arm64`, `sanitize-macos`, `windows-msvc`; `clang-tidy-
  advisory` non-blocking). **`windows-msvc` is the load-bearing job this wave** — it is the only proof
  the MSVC include-resolution design (no `<limits.h>` shadow from `src/` on the path) holds. The PR
  body states this is a pure mechanical layout move, object-code-identical, zero behavior/test/golden
  change.
- [ ] **Step 4: MERGE-WHEN-GREEN (owner grant)** — once all six jobs are green, ff-merge to master and
  delete the `arch/physical-layout` branch.
- [ ] **Step 5: Bookkeeping** — ledger entry (physical layout landed; nine libraries + app now
  physically homed; test count unchanged 1583; combat DEFER stays 0); MEMORY update (library-split
  progress: physical layout DONE; next-frontier options unchanged — Stage 2 LocationSystem,
  rots_commands census, int→double storage). Report the merged wave to the owner.

---

## Risks (per spec §Risks, with this plan's mitigations)

- **Flat-Makefile parity (the recurring gap class).** Mitigated by T1 doing ALL build-system work
  FIRST, consumer-free, proven inert; the `src/tests/Makefile` monolithic-runner restructure (VPATH
  + generic pattern rule replacing ~90 hardcoded `../foo.cpp` rules) is the single largest edit and
  the highest-risk item — it is designed by T0, implemented and proven green (no-move) in T1, and
  re-proven end-to-end by the i386 battery's monolithic run in T5.
- **Include-resolution divergence (GNU vs MSVC).** The census keeps every cross-boundary header flat
  so no directive changes meaning; the `-idirafter src/` (GNU) design keeps `src/limits.h` from
  shadowing `<limits.h>`; MSVC's ordering is the FLAGGED UNCERTAINTY below, proven by `windows-msvc`
  CI at T5. A divergence no `-I` ordering fixes = STOP.
- **Stale container objects.** Clean configure per container after the first move batch (T2 platform
  commit onward), and a clean fresh-branch battery at T5.
- **Classification surprise** (a "private" header with a hidden cross-lib includer) → surfaces as a
  batch build/linkcheck failure → fix-is-reclassify-to-flat (same commit), not redesign. A header
  fitting no home = STOP with evidence.
- **string-view-exceptions path drift** → a moved file with a documented exception whose owner-path
  column is not updated in-commit fails `--check` → the per-batch gate catches it; T0's
  cross-reference is the checklist.

## Uncertainties flagged inside this plan (for the owner)

1. **MSVC include ordering for `src/` (top uncertainty).** GNU-family uses `-idirafter src/` to keep
   `src/limits.h` off the angle-bracket path (the `ageland_tests` precedent). MSVC has no
   `-idirafter`; whether `/I src` shadows `<limits.h>` for any product `.cpp`'s angle include is
   unproven until `windows-msvc` CI runs. T0 must design the MSVC mechanism explicitly; if no `/I`
   ordering both resolves the flat headers AND avoids the shadow, that is a STOP to the owner (a
   directive exception, or a narrower include strategy, would be needed). This is the wave's single
   most likely source of a late CI surprise.
2. **`src/tests/Makefile` restructure scope.** T0 must rule whether to (a) drop the ~90 redundant
   explicit `../foo.cpp` rules in favor of `vpath` + one product pattern rule (recommended — the
   file's own comments call the hand rules redundant; cleanest and inert), or (b) per-file edit each
   `../foo.cpp` → `../<lib>/foo.cpp` at each move (larger per-batch diff, breaks the "T1 front-loads,
   T2 is pure moves" shape). This plan recommends (a); T0 confirms and designs the exact flag var.
3. **`convert_main.cpp`'s home.** A production `.cpp` in no library. The owner decision ("every file
   gets an explicit home") implies `src/app/`; T0 rules it (and whether `rots_convert`'s CMake source
   entry becomes `app/convert_main.cpp`). Minor, but stated so no file is left flat by omission.
4. **Whether any test file's include lines must change.** The plan's design satisfies test-TU access
   to moved private headers by adding the lib dirs to the test targets' include paths, NOT by editing
   `#include` lines — so the ASan gate should not trigger. If T0 finds a private header a test file
   reaches only via a path that cannot be added without a directive edit, that test-file edit (and
   its ASan gate) becomes real — flagged so it is not a silent scope creep.
