# Combat-Library Seed (minimal) + Deferral Riders Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stand up `rots_combat` (L3 STATIC, 4 SEED-CLEAN TUs) + `CombatLayerAcyclicity`, and complete the two placement-seam deferral riders (time quartet → rots_core; NumberedName header → finish the parse_numbered_name/get_char moves).

**Architecture:** Spec: `docs/superpowers/specs/2026-07-19-combat-seed-design.md`. Census: `.superpowers/sdd/combat-census.md` (authoritative per-TU evidence). Instruments: the four-times-proven library-seed pattern (rots_persist is the freshest full model; rots_world the freshest seed); verbatim relocation; a compatibility-include header extraction.

**Tech Stack:** C++20, CMake presets + flat Makefiles, GoogleTest, goldens, whole-archive linkchecks.

## Global Constraints

- **Zero behavior change for `ageland`.** Goldens byte-for-byte; ctest **1315 → 1316** both hosts (the +1 is CombatLayerAcyclicity; riders add no tests — census says the 4 TUs are covered by existing suites; Task 1 verifies and cites them).
- Branch `arch/combat-seed` off master @6d9d629. Verbatim moves; declarations stay in current headers except the ONE sanctioned extraction (NumberedName out of handler.h WITH a compatibility include so no caller changes); named deviations only.
- **STOP contract** on any linkcheck undefined symbol (cascades expected-possible; census says clean; controller adjudicates each).
- **Per-task gates:** macOS `macos-arm64` build + full ctest + native boot golden AND rots64 preset + ctest + boot golden (synchronous FOREGROUND docker, `--pull never`, timeout ≤600000 ms — NEVER background: two prior stall precedents). Census exit 0. nm single-definition per move. All linkchecks green (6 after Task 1). i386 battery at finalization only (controller).
- **Formatter hook:** ALL .cpp/.h edits via python byte-edits run through Bash (Write tool safe for .py/.md/CMake/shell only). Verify each touched file's line-ending convention FIRST and preserve it byte-exactly (utility.cpp mixed-CRLF; handler.h check before editing). `git diff --stat` + spot-check per file.
- Container build trees live in named volumes; host `build/` is host-preset-only. Top-level `build/` hands-off remains for the battery flow only.

---

### Task 1: rots_combat library + CombatLayerAcyclicity

**Files:**
- Modify: `src/CMakeLists.txt` (ROTS_COMBAT_SOURCES {skill_timer.cpp, battle_mage_handler.cpp, weapon_master_handler.cpp, wild_fighting_handler.cpp}; add_library rots_combat STATIC + RotS::combat ALIAS; TUs leave ROTS_SERVER_SOURCES; ageland links RotS::combat; ageland_tests source pickup per the established mechanism; rots_combat_linkcheck + CombatLayerAcyclicity ctest — mirror rots_world_linkcheck exactly: both force-load spellings, LINK_DEPENDS, add_dependencies, minimal main per precedent)
- Modify: root `Makefile` (hand-maintained linkcheck list += rots_combat_linkcheck; update the explanatory comment's linkcheck enumeration)
- Verify-only: flat Makefiles (monolithic object lists — confirm no change needed, matching the rots_world precedent; report)

**Interfaces (Produces):** `RotS::combat` target; `CombatLayerAcyclicity` ctest name. PUBLIC deps: `RotS::entity RotS::core RotS::platform rots_build_flags` — add `RotS::persist`/`RotS::world` ONLY if the linkcheck exposes a sanctioned L3-peer edge (census expects none; STOP and report evidence if one appears rather than silently adding the dep).

- [ ] **Step 1:** CMake wiring per above. The spec-handler TUs' pre-boot registrations (comm.cpp/gtest_main call their register functions) stay app-side — verify the registration declarations' headers still resolve and the direction is app→lib (report the call sites).
- [ ] **Step 2:** Build rots_combat + rots_combat_linkcheck on macos-arm64. On ANY undefined symbol: STOP, collect the exact list, report BLOCKED for adjudication.
- [ ] **Step 3:** Negative-FAIL verification per the established precedent (transient upward-call experiment in a scratch build, reverted traceless; document method + output).
- [ ] **Step 4:** Cite the existing ctest suites covering the 4 TUs (grep test files; e.g. BattleMageHandler.*, SkillTimer.* — exact names in the report; if any TU has NO coverage, flag it for the standing coverage-gap rule rather than silently proceeding).
- [ ] **Step 5:** Full both-host gates (expect **1316/1316**, boot goldens byte-identical, census 0, all 6 linkchecks green).
- [ ] **Step 6:** Commit `feat: rots_combat static library (4-TU seed) + CombatLayerAcyclicity linkcheck`

### Task 2: Rider — time quartet → rots_core

**Files:**
- Modify: `src/utility.cpp` (loses real_time_passed, mud_time_passed, day_to_str, age + their placement-seam deferral comments), the chosen L1 core TU (+4 functions; expected home: `src/consts.cpp` co-located with `month_name[]` — implementer verifies against core's conventions and reports the choice + precedent; a NEW core TU requires ×4 wiring and must be justified), headers unchanged (declarations stay in `utils.h` — verify).

**Interfaces:** none new — public names/signatures unchanged.

- [ ] **Step 1:** MOVE the four verbatim (python byte-edits; utility.cpp mixed-CRLF discipline; the deferral comments are REMOVED not relocated — their content is superseded by this move; note the retirement in the commit body).
- [ ] **Step 2:** nm single-definition ×4; CoreLayerAcyclicity + all other linkchecks green locally.
- [ ] **Step 3:** Full both-host gates (1316/1316, goldens, census). Commit `refactor: time helpers rejoin rots_core (placement-seam deferral rider)`

### Task 3: Rider — NumberedName header + finish the two deferred moves

**Files:**
- Create: `src/core/include/rots/core/numbered_name.h` (the `NumberedName` struct extracted verbatim from `handler.h:84-90`, header guard/pragma per core-header convention, provenance comment)
- Modify: `src/handler.h` (struct replaced by `#include "rots/core/numbered_name.h"` — compatibility include, no caller changes; parse_numbered_name declaration stays), `src/handler.cpp` (loses parse_numbered_name + get_char and their deferral comments), platform text home `src/rots_util.cpp` (+parse_numbered_name), `src/entity_lifecycle.cpp` (+get_char), headers otherwise unchanged.

**Interfaces:** none new — `NumberedName` spelling/layout byte-identical; both functions keep names/signatures/declaring headers.

- [ ] **Step 1:** Header extraction (verify rots_platform's include path reaches rots/core headers — it must NOT: platform cannot include core. CHECK the actual constraint: parse_numbered_name moving to rots_util.cpp (L0) requires NumberedName visible from L0 — so the header must live where L0 may include it. If core's include tree is L1-only, the header belongs in the PLATFORM include tree or a tier-neutral location instead; resolve against how existing L0-visible shared types are homed, report the decision + precedent, and STOP if no clean precedent exists. This is the task's one design-sensitive point — the spec sketched core's tree but the L0 visibility question governs.)
- [ ] **Step 2:** The two moves verbatim per their placement-census rows (get_char: entity-pure, character_list+isname both L2-resident now; parse_numbered_name: pure text). Deferral comments retired.
- [ ] **Step 3:** nm ×2; PlatformLayerAcyclicity + EntityLayerAcyclicity green; full both-host gates (1316/1316, goldens, census). Commit `refactor: NumberedName shared header; parse_numbered_name/get_char complete their deferred moves`

### Task 4: Docs

- [ ] **Step 1:** BUILD.md: six-library layering picture (rots_combat membership + the DEFER-11 growth inventory with the census's blocker analysis: app-side handler/utility remainder + command-dispatch seam; output-seam finding recorded — combat output already forwards); parent spec §3 as-built note (4-of-16 seeded); AGENTS.md rots_combat one-liner + totals 1316 + rider completions (placement-seam deferral ledger updated: 2 of 3 clusters retired, poison-hook cluster remains).
- [ ] **Step 2:** Census 0; docs gates; commit `docs: combat-seed as-built (six libraries, rider completions, growth inventory)`

### Task 5: Finalization (controller-owned)

- [ ] **Step 1:** Full i386 battery.
- [ ] **Step 2:** Whole-branch review (most capable model) + fix wave if needed.
- [ ] **Step 3:** Push; PR against master. Merge = owner's call.

---

## Self-Review Notes

- **Spec coverage:** library+linkcheck (T1), both riders (T2/T3), poison-hook explicitly NOT scoped (spec Rejected list; T4 documents), docs (T4), finalization (T5).
- **Placeholder scan:** clean — the two implementer-verified decisions (time-quartet home TU; NumberedName header tier placement) carry explicit contracts, precedent requirements, and STOP conditions rather than TBDs.
- **Type consistency:** RotS::combat, CombatLayerAcyclicity, ROTS_COMBAT_SOURCES, numbered_name.h used consistently.
- **Risk register:** (1) hidden seed edges — T1 STOP contract; (2) NumberedName L0-visibility subtlety — T3 Step 1's explicit check (the spec's core-tree sketch may be wrong; the plan makes the tier question the deciding input); (3) registration direction — T1 Step 1 verification; (4) totals drift — every task pins 1316.
