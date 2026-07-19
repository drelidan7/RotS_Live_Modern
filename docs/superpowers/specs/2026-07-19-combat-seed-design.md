# Combat-Library Seed (rots_combat, minimal) + Deferral Riders — Design

Date: 2026-07-19. Status: approved-in-dialogue (owner), pre-implementation.
Wave branch: `arch/combat-seed` off master @6d9d629 (post placement-seam merge).
Parent spec: `docs/superpowers/specs/2026-07-16-library-architecture-design.md` §3 (the
16-TU `rots_combat` row) and §10. Evidence: `.superpowers/sdd/combat-census.md`
(planning census, master @6d9d629).

## Problem / evidence

The parent spec sketches `rots_combat` (L3 peer) at 16 TUs. The census verdicts: **4 SEED-CLEAN**
(`skill_timer`, `battle_mage_handler`, `weapon_master_handler`, `wild_fighting_handler` — fully
closed over L0 platform + L1 core incl. the existing output seam + L2 entity + intra-subset
references), 1 caveated (`profs`), **11 DEFER**. Key census facts:

- **Output does NOT block combat membership**: `output_seam` (L1) already forwards
  `send_to_char`×2 / `vsend_to_char` / `act` / track+untrack_mage downward. Uncovered comm
  surface (`send_to_room*`, `send_to_all`, `break_spell`, `abort_delay`, `complete_delay`,
  txt-pool, `descriptor_list`, color globals) is only reached by DEFER-tier TUs.
- **The dominant blocker for the big TUs** (`fight`, `mobact`, `ranger`, `mystic`, `spec_pro`, …)
  is the app-side `handler.cpp`/`utility.cpp` remainder (visibility family, equip wrappers) plus
  up-calls into commands (`do_hit`/`do_move`/`do_flee`) — a command-dispatch seam and/or further
  remainder shrinkage are prerequisites, deliberately NOT this wave.
- `do_cast` is already in-row (`spell_pa`); command entry INTO combat is downward and fine.

## Decision (owner-approved)

**Minimal 4-TU seed**: stand up `rots_combat` STATIC = {`skill_timer.cpp`,
`battle_mage_handler.cpp`, `weapon_master_handler.cpp`, `wild_fighting_handler.cpp`} +
`CombatLayerAcyclicity` whole-archive linkcheck — the fourth domain library, seeded exactly as
`rots_world` was (3 of ~15). Zero new seams. The library grows in later waves as blockers fall.

Plus two riders (placement-seam deferral cleanups):
1. **Time quartet → `rots_core`**: `real_time_passed`, `mud_time_passed`, `day_to_str`, `age`
   move from `utility.cpp` to an L1 core TU (census-verified correctly-homed: they use
   `time_info_data`/`month_name[]`, both L1). Completes the placement-seam T5 deferral.
2. **NumberedName shared header**: extract the `NumberedName` type from `handler.h` into a small
   shared header reachable from platform's include path, then complete the deferred
   `parse_numbered_name` (→ platform) and `get_char` (→ entity) moves from placement-seam T4.

Rejected for this wave: the poison-notification hook (`obj_from_char`/`extract_obj`) — census
confirms those calls are already downward (L2); the hook only becomes relevant when combat-tier
code inverts entity→combat edges, i.e. the ambitious wave. Stays on the backlog. Also rejected:
`profs` (+caveat) and the ~8-forwarder output-seam extension — build them when a TU needs them.

## Changes

1. **CMake**: `ROTS_COMBAT_SOURCES` {the 4 TUs}; `add_library(rots_combat STATIC)` +
   `RotS::combat` alias; PUBLIC deps per the persist/world precedent (RotS::entity RotS::core
   RotS::platform rots_build_flags — plus RotS::persist/RotS::world ONLY if the linkcheck proves a
   sanctioned L3-peer edge exists; census expects none). TUs leave `ROTS_SERVER_SOURCES`; ageland
   links `RotS::combat`; ageland_tests compiles the sources per the established mechanism.
2. **`rots_combat_linkcheck`** mirroring the five existing linkchecks (both force-load spellings,
   LINK_DEPENDS, minimal main, `CombatLayerAcyclicity` ctest, root-Makefile hand-list entry).
   STOP contract on any undefined symbol (cascades expected-possible; controller adjudicates).
   Note: the spec-handler TUs register themselves into entity hooks pre-boot (existing
   registrations in comm.cpp) — registration call sites stay app-side; the registered
   implementations move into the lib (legal: app calls down into both).
3. **Rider 1**: time quartet → the appropriate existing L1 core TU (implementer verifies the home
   — likely alongside other time/const helpers in rots_core; ownership comments; declarations stay
   in `utils.h`).
4. **Rider 2**: `NumberedName` → new small header under core's include tree (or an existing
   suitable header — implementer verifies; `handler.h` includes it for compatibility so no caller
   changes); then `parse_numbered_name` → platform text home, `get_char` → `entity_lifecycle.cpp`,
   both verbatim per their placement-census rows; their placement-seam deferral comments retired.
5. **Docs**: BUILD.md (six-library picture; combat seed membership + growth deferrals), parent
   spec §3 as-built note, AGENTS.md one-liner + totals if tests change.

## Verification

- Zero behavior change: goldens byte-for-byte; ctest baseline **1315** both hosts → expect
  **1316** after the linkcheck lands (+ any rider-driven coverage per the standing coverage-gap
  rule — census flags none expected: the 4 TUs are already covered by existing suites
  [BattleMageHandler/SkillTimer/etc. tests exist]; verify and cite).
- Per-task both-host gates as established; nm single-definition per move; all 6 linkchecks green;
  full i386 battery + whole-branch review + six CI jobs at finalization.
- `CombatLayerAcyclicity` green is the wave's decisive check; positive-PASS + negative-FAIL
  verification per the established linkcheck precedent.

## Risks

1. Linkcheck cascade at seed time (hidden edges in the 4 TUs) — STOP contract; census says clean.
2. The spec-handler TUs' registration wiring spans lib/app — direction verified at planning
   (app registers down); reviewer re-verifies.
3. Riders touch `handler.h` (NumberedName extraction) — compatibility include keeps every caller
   unchanged; headers otherwise untouched.

## Out of scope

- `profs`, the output-seam extension, the command-dispatch seam, `fight.cpp`+the DEFER-11, the
  poison-notification hook, `fight_messages` storage move — all future combat-growth waves.
- The §7 call-site campaign and Stage 2 (unchanged backlog).

## As-built

Implemented as designed: the minimal 4-TU seed (`skill_timer.cpp`, `battle_mage_handler.cpp`,
`weapon_master_handler.cpp`, `wild_fighting_handler.cpp`) + `CombatLayerAcyclicity` landed in Task
1 with no linkcheck cascade (first-attempt green, unlike `rots_entity`'s two rounds and
`rots_world`'s four-edge cascade) and the PUBLIC dep set exactly as sketched
(`RotS::entity`/`RotS::core`/`RotS::platform`/`rots_build_flags`, no `RotS::persist`/`RotS::world`
— the census's "expects none" call was correct). One header-only deviation, not a STOP: `rots_combat`
also takes a PRIVATE `persist/include` (for `weapon_master_handler.cpp`'s `handler.h`→`RENT_CRASH`
macro reach), mirroring `rots_entity`'s identical line — no link-time dependency on `rots_persist`.

Both riders completed: the time quartet moved into `consts.cpp` (Task 2) exactly as this spec
sketched. **Rider 2 deviated from the sketch's tier**: this spec's "Changes" §4 sketched
`NumberedName` landing "under core's include tree"; Task 3's Step 1 design-sensitive check (per
the plan) found `rots_platform` has no include path into `rots/core/*` — `parse_numbered_name`
moves to `rots_util.cpp` (L0), which needs `NumberedName` visible from L0, so the type could not
live in `rots_core`'s (L1) include tree without breaking `PlatformLayerAcyclicity`. It landed at
`rots/platform/numbered_name.h` instead (PLATFORM tier), evidenced by a compile-path check (not
guesswork) and precedented by `rots/platform/log.h`'s identical L0-visibility shape. `handler.h`
compatibility-includes the new header, so no caller changed.

The standing coverage-gap rule (not sketched in this spec — it is a cross-wave rule triggered by
Task 1's citation step finding `skill_timer.cpp`/`wild_fighting_handler.cpp` had zero dedicated
tests) added a Task 3b coverage rider between the riders and docs: +27 tests (8 `SkillTimerTest.*`,
19 `WildFightingHandler.*`), ctest 1316 → **1343** both hosts + ASan.

The poison-notification hook (`obj_from_char`/`extract_obj`) stayed rejected, as designed — the
combat-seed census re-confirmed it is only an ambitious-wave concern (see `docs/BUILD.md`'s
`rots_combat` section). No STOP conditions fired; no risk from the "Risks" section above
materialized beyond the documented, non-blocking NumberedName tier deviation.
