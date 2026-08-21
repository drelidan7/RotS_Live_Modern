# RR Wave R3 — `src/combat/` classification + the dispatch-pattern policy (TASK-001 + TASK-002)

**Date:** 2026-08-21
**Branch:** `arch/rr3-combat` (off master @`62188453`, 1862 tests; ledger 95 combat rows / 186
sites, all `TODO`; `MAXIMUM_TODO_COUNT` 716)
**Program authority:** `docs/superpowers/specs/2026-07-31-room-resolve-retirement-design.md`;
recipes: `docs/superpowers/room-resolve-playbook.md`. Where they disagree, the program spec wins.
**Owner rulings 2026-08-21 (recorded in `.superpowers/sdd/2026-08-21-rr3-combat/rr3-rulings.md`):**
TASK-002 folded into this wave and ruled BEFORE classification (R3-O-0); policy (A)
`dispatch-invariant` (R3-O-1); APPLY_SPELL-window rows stay TODO, bundled with RR-O-1 (R3-O-2);
`CAN_SEE`/`get_char_room_vis` caller-pinned + stayed-TODO (R3-O-3); `olog_hai::get_random_target`
stays TODO, owner-punted (R3-O-4).
**Review posture:** per-task reviews + the standing dual adversarial whole-branch review at
finalization (this wave lands the program's first production-tier policy, so the owner-flag
clause of program spec §7 applies). Merge is the owner's call.

## 1. What Task 0 measured (re-derived; `.superpowers/sdd/2026-08-21-rr3-combat/rr3-census-{a,b}.md`,
`rr3-dispatch-census.md`)

| Bucket | Rows | Sites |
|---|---|---|
| combat in scope | 95 | 186 |
| drainable on existing proof kinds (HIGH) | ~37 | ~79 |
| MEDIUM caller-contract / occupant-chain (B) | 6 | 14 |
| DISPATCH-PATTERN | 45 | 84 |
| — of which APPLY_SPELL-window-reachable (stay TODO, R3-O-2) | 11 | 16 |
| NOWHERE-REACHABLE → GUARDED (`spell_blink` :944; `raw_kill` root for `death_cry`/`get_corpse_desc`) | 3 | 5 |
| scale-flagged (`CAN_SEE` :578/:580 half, `get_char_room_vis`) | 2 | 3 |
| owner-punted (`get_random_target`) | 1 | 2 |

Advisory overturn rate: 14/42 (A) and 11/14 advisories (B) — `--advise` is a search hint only in
this tier. Controller-verified defects in LANDED content: the `report_zone_power` PROVEN row
enumerates 4 `spell_pointer` doors; there are 6, and the missed `entity_lifecycle.cpp:2440` door
runs at NOWHERE on the login path (row reopened, R3-C-3); every R2 proof's "in-range via M-1
(placement.cpp:369-395)" cites the ScopedRenderLocation precondition, not an in-range argument
(prose corrected once, R3-C-2).

## 2. The policy (R3-O-1): `dispatch-invariant`

**Guards.** One tripwire at each dispatcher's dispatch statement, on the ACTOR argument:
`if (location_of(ch) == NOWHERE) { mudlog(<globally unique string>); return; }` — the
`db_world.cpp:2083` idiom, so the string joins R-final's measured-zero sweep. Entry set:
`command_interpreter` (interpre.cpp ~:1115), `issue_command` (combat_hooks.cpp ~:64),
`shape_center` (shapemob.cpp ~:2364), `do_cast`'s dispatch (spell_pa.cpp ~:883), `do_use`
(act_othe.cpp ~:805/:825), `cast_mass_spell` (mystic.cpp ~:1005), `activate_char_special` /
`activate_obj_special` (interpre.cpp ~:1197/:1230), `mobile_activity`'s PC/virt arm
(mobact.cpp ~:69). Already guarded, cited as-is: `special()` :1263, `one_mobile_activity` :91,
`affect_update_room` :1493 (occupant-chain). **Deliberately NOT guarded:** `affect_modify`'s
APPLY_SPELL arm (entity_lifecycle.cpp:2440/:2442) — it runs at NOWHERE by design in the login
window (P1); rows reachable through it are the R3-O-2 sub-class.
Zero expected behavior change: census P7 found no live path delivering an unplaced actor to any
of these statements. Boot golden byte-identical is therefore a hard gate on the guard commit.

**Proof kind.** `PROOF_KINDS` gains `dispatch-invariant` (citation-required). A row's proof cites
(i) the registry entry (entry point file:line) that guards its actor parameter, (ii) the actor
parameter by name, (iii) the body's exhaustive direct-caller list with each extra door proven or
the row split (e.g. `do_mental` ← `do_hit`/`perform_violence`). The in-range half cites R3-C-2's
wording.

**Closure (so an 8th dispatcher cannot inherit the proof).** A marker-anchored
`<!-- ROOM-RESOLVE-DISPATCH-ENTRIES -->` table in the ledger lists every entry point
(file · function · guard literal); `--check` asserts each listed body contains its guard literal
AND that every occurrence of the fn-ptr dispatch spellings (`command_pointer)(`,
`g_command_table[`, `spell_pointer)(`/`.spell_pointer(`, `activate_char_special(`,
`activate_obj_special(`, `shape_center(`) lies inside a registered entry or a row-classified
site. Self-tested in sabotage directions (guard deleted; entry removed from table; new dispatch
spelling added). The location-state registry (`check_registry_consistency`) is the precedent.

**Caller-count pins (R3-O-3, implemented as counts not tokens).** `CAN_SEE(` and
`get_char_room_vis(` tree-wide production caller counts are checked-in literals the gate asserts;
a new caller fails `--check` without minting ~146 new rows (literal token-pinning would have
raised the ceiling by that much — recorded here as the deliberate deviation).

**Stayed-TODO taxonomy grows by three:** `APPLY_SPELL-window` (R3-O-2), `owner-punted`
(R3-O-4), and the existing `scale-flagged` now covering `CAN_SEE`/`get_char_room_vis`.

## 3. Tasks

- **T0 — census** (done, read-only).
- **T1a — gate vocabulary** (tool + ledger prose + self-tests; zero production code):
  `dispatch-invariant` kind, the dispatch-entry registry + tokens + closed-world check, the
  caller-count pins, the R3-C-2 prose correction, the three new taxonomy entries,
  `report_zone_power` flipped back to TODO (ceiling 716 → 717, `--check`-derived).
- **T1b — the guards** (production + tests): the entry-point guards, one
  unplaced/placed test pair per entry point (~+12), the 10 `CombatHooksDispatch` + 2
  `spec_pro_tests` NOWHERE-convenience fixtures re-placed, registry rows filled with real line
  numbers, boot golden byte-identical, ASan. `make smoke-account` at finalization (do_cast/do_use
  touched; cheap insurance).
- **T2 — mage/mystic/spell_pa** (census A): proofs for the 51 drainable sites, the `spell_blink`
  GUARDED (+1 test), the 35 − 16 = 19 dispatch sites as `dispatch-invariant`, 16 stay TODO
  (`APPLY_SPELL-window`), `spell_summon` victim half stays TODO (medium-confidence refusal) unless
  the implementer can close it HIGH.
- **T3 — fight/limits/clerics/olog_hai/ranger/visibility** (census B): proofs for the 28 HIGH
  sites; the MEDIUM rows (`die`, `exp_with_modifiers`, `affect_update_person`, `point_update`'s
  object half, `on_windblast_hit` occupant-chain) land only if raised to HIGH, else stay TODO
  with reason; `raw_kill`-root GUARDED (+2 tests); 49 dispatch sites as `dispatch-invariant`;
  `CAN_SEE` split; `get_random_target` stays TODO (R3-O-4). Two R2-deferred `weather_to_char`
  rows ride here as `caller-contract` (R3-C-4).
- **T4 — docs fold-in**: BUILD.md RR subsection, AGENTS.md `tools/` entry (measured numbers),
  playbook (new kind recipe + R3 cost-table row + the overturn-rate correction + pitfalls from
  A/B), this doc's as-built section, arc doc + journal, TASK-001/002 closed.
- **T5 — finalization**: `rots64` + both boot goldens, `make smoke-account`, i386 battery
  (`i386-battery` skill), six blocking CI jobs, dual adversarial whole-branch review, PR.
  Merge is the owner's call.

## 4. Standing rules for every task
Per-commit gates (macOS native): build, `ctest --preset macos-arm64`, both censuses `--check`,
native boot golden; ASan on any task touching a test file; seed42 golden unchanged. Every
line number re-read before citation; every count re-derived; `MAXIMUM_TODO_COUNT` only ever
`--check`-derived. No medium-confidence proofs land. `make format` in its own commit or
disclosed. Controller runs the `rots64` leg at tranche ends (CADENCE AMENDMENT precedent).
