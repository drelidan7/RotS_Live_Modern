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

---

# As-built (Wave R3 Task 4, written at `6c2bc034`)

Every figure in this section was re-derived at that commit by driving
`tools/room_resolve_census.py`'s own `parse_ledger()` over the checked-in ledger, by
`ctest --preset macos-arm64 -N`, or by `git show <sha>:docs/superpowers/room-resolve-ledger.md`
per commit — never copied from a task report. Where a report's figure disagreed, the report
is corrected here and the disagreement is named.

## 5. Baseline correction

This document's header says the branch is off master `62188453` with **1862 tests**. The real
count at that commit is **1865**: Wave R1's own measurement (1862) predates three tests that
entered master afterwards. Task 1a flagged it, Task 2p flagged it independently, and it is
corrected here once — the wave's test chain below starts at 1865, not 1862.

## 6. What the wave actually moved

Ledger totals, `parse_ledger()`-derived at `62188453` and at `6c2bc034`:

| Class | Base rows/sites | HEAD rows/sites |
|---|---|---|
| TODO | 271 / 716 | 200 / 579 |
| PROVEN | 38 / 83 | 111 / 215 |
| GUARDED | 1 / 3 | 4 / 8 |
| TEST-FIXTURE | 120 / 401 | 122 / 412 |
| DECL | 32 / 43 | 32 / 43 |
| RESOLVER-IMPL | 7 / 33 | 7 / 33 |
| **Total** | **469 / 1279** | **476 / 1290** |

`PROVEN` by kind at HEAD: `entry-guard` 45 rows / 85 sites, `dispatch-invariant` 30 / 56,
`caller-contract` 20 / 48, `loop-bound` 9 / 14, `dominating-resolve` 7 / 12. The
`dispatch-invariant` column did not exist before this wave.

### 6a. The behavior-change inventory (added at the Task 5-fix round)

Section 2 says "zero expected behavior change", and for the boot golden and the whole test
suite that is measured and true. It is not the whole statement, and this inventory is what the
wave should have carried from the start (whole-branch review-1, finding m-7).

1. **Three deliberate `GUARDED` changes**, each red-first with an in-body positive control and
   each already inventoried in section 7 and its ledger row: `mage.cpp:944`'s `!fail &&` term
   in `spell_blink`; `fight.cpp:927` in `death_cry`; the leading
   `location_of(ch) != NOWHERE &&` term at `fight.cpp:567` in `get_corpse_desc`.
2. **A changed FAILURE MODE for an unplaced PLAYER, under a known-live producer.** Census P7
   found no live path delivering an unplaced actor to a tripwire, but that is a statement about
   the DISPATCHERS, not about producer P5 (`char_to_room(X, location_of(Y))` with `Y` itself
   unplaced — 8 sites, `act_wiz.cpp:360`/`:373`/`:1426`, `mystic.cpp:1682`, `mage.cpp:830`,
   `spec_pro.cpp:446`/`:2881`/`:3446`), which `rr3-dispatch-census.md:621` classifies "YES --
   persistent" and live. If P5 fires for a PLAYER, the pre-R3 behavior was that the player kept
   acting, degrading onto room 0; the post-R3 behavior is that `command_interpreter`'s `:1119`
   tripwire refuses EVERY command that player types -- including `quit` -- with a `LEVEL_IMPL`
   mudlog and no message to the player. That is the better posture, and it is where RR-O-1 is
   heading, but it is a player-facing change under a bug class known to be live and it belongs
   on this list rather than inside "zero expected behavior change". The same producer's NPC
   half is caught by `mobact.cpp:106`, which pre-dates the wave.
3. **Nothing else.** Every other statement this wave added is a refusal on a path census P7
   measured as unreachable: the abort-probe build of all twelve tripwires produced failures
   only in fixtures that were dispatching at NOWHERE as a test convenience, and both boot
   goldens plus the seed42 characterization golden are byte-identical throughout.

**Sites drained: 138** (716 base + 1 reopened by R3-C-3 = 717, less 579 at HEAD) —
**130 in `src/combat/`** (95 rows / 186 sites in scope, now 100 rows after five mixed-class
splits, with 29 rows / 56 sites still `TODO`), **5 in `src/olc/`** (Task 3e's four R2-deferred
rows, which take `src/olc/`'s `TODO` count to zero) and **3 in `src/world/weather.cpp`**
(the two R2-deferred `weather_to_char` rows, ruling R3-C-4).

## 7. The measured per-task chain

`TODO` site-sum is the `parse_ledger()` figure at each commit; `ceiling` is
`MAXIMUM_TODO_COUNT` in the tool at that commit; tests are `ctest --preset macos-arm64`
totals from each task's own gate section, ending at the **1890** this section's own
`ctest -N` re-measured.

| Task | Commits | TODO sites | Ceiling | Tests |
|---|---|---|---|---|
| (base) | `62188453` | 716 | 716 | 1865 |
| T1a — gate vocabulary | `94f63085`..`a2ffde41` | 716 → **717** (`report_zone_power` reopened, R3-C-3) | 716 → **717** | 1865 |
| T1b — the nine guards | `ae0c1c92`, `d7cd3ff5` | 717 | 717 | 1865 → **1883** (+18) |
| T2p — census A proofs | `e0943d3d`, `87f515ba` | 717 → **668** (−49: 48 PROVEN + 1 GUARDED) | 717 | +1 |
| T3p — census B proofs | `dfd25825`..`d6174d8e` | 668 → **636** (−32: 28 PROVEN + 4 GUARDED) | 717 | +2 |
| (integration) | `f4336565` | 636 | 717 → **636** | — |
| T2d — census A dispatch rows | `a987e568` | 636 → **617** (−19) | 636 | +0 |
| T3d — census B dispatch rows | `08434154` | 617 → **585** (−32) | 636 | +0 |
| (integration) | `8ec65a46` | 585 | 636 → **585** | — |
| T1c — guard move + `target_from_word` | `f45c7e82`..`8fc82d39` | 585 → **584** (−1) | 585 | +1 |
| T1d — R3-C-7 adjacency repair | `ec63ddb8`..`e0c40ab8` | 584 | 585 | +3 |
| T3e — the four OLC rows | `400ab2e0` | 584 → **579** (−5) | 585 | +0 |
| T4 — docs fold-in | `6c2bc034`.. | 579 | 585 | +0 |

Test delta for the wave: **1865 → 1890, +25** (T1b +18, T2p +1, T3p +2, T1c +1, T1d +3).
Skips **76** (macOS), unchanged throughout — no task touched a POSIX/32-bit/env-gated test.
`MAXIMUM_TODO_COUNT` is **585** at HEAD and the `--check`-derived value is **579**; the
controller lowers it once at T5, `--check`-derived, as this wave's standing rule requires.

## 8. The coordinator rulings, as they landed

Full text in `.superpowers/sdd/2026-08-21-rr3-combat/rr3-rulings.md`.

- **R3-C-1 — a `dominating-resolve` needs a PRODUCING resolve.** An earlier *unguarded*
  `room_of(x)` proves nothing (it is total and degrades silently). Consumed twice: it made
  `spell_locate_living`'s filter-shaped producer admissible (T2p), and it is why `do_cast`'s
  own `room_of(ch)` at `spell_pa.cpp:505` is explicitly NOT offered as a proof of the `:933`
  dispatch.
- **R3-C-2 — the in-range half's standing citation.** `placement.cpp:369-395` is
  `ScopedRenderLocation`'s banner, not an in-range argument. The standing wording cites the
  WRITE side (`char_to_room` at `placement.cpp:548`/`:564` and
  `would_break_the_absence_invariant` at `:410-420`, enforced `:427`/`:440`) plus append-only
  allocation. R2's rows are deliberately not rewritten; the prose section supersedes their
  boilerplate. **All 54 Wave R3 rows were normalized onto one spelling at T4** (T2p/T3p had
  copied the ruling's own drafted `:369-391`, which T1a corrected after they branched).
- **R3-C-3 — `report_zone_power` reopened.** Its Wave R2 proof enumerated "the SOLE FOUR"
  `skills[].spell_pointer` doors; there are six, and the missed
  `entity_lifecycle.cpp:2440` door runs at NOWHERE on the login path. Flipped back to `TODO`
  (ceiling 716 → 717) and it is still `TODO` at HEAD — it is an `APPLY_SPELL-window`-shaped
  row in `src/world/`, not part of this wave's drain.
- **R3-C-4 — two R2-deferred `weather_to_char` rows leave the dispatch class** as
  `caller-contract` (one production caller, guarded at `act_info.cpp:2218`). Landed by T3p.
- **R3-C-5 — target-derived ids are never covered by an actor policy.** The tree's
  `location_of(A) == location_of(B)` equality checks all pass when BOTH are NOWHERE. It bit
  twice for real: `spell_summon`'s victim half stayed `TODO`, and `cast_mass_spell`'s T1b
  test had to put BOTH caster and group member at NOWHERE or it would have passed with the
  guard deleted.
- **R3-C-6 — advisories in `src/combat/` are a coin flip.** Census A overturned 14 of 42
  (33%), census B 11 of 14 (79%).
- **R3-C-7 — the adjacency rule** (answering T1c's STOP, below): a `dispatch-invariant` row
  inherits an entry's guard only if nothing between that guard and the dispatch can run code
  with the actor as a participant. The structural answer is ADJACENCY, and an entry may
  therefore carry MORE THAN ONE guard literal.

Owner rulings R3-O-1..4 landed as specified in section 2 above, with one addition worth
recording: R3-O-2's `APPLY_SPELL-window` class is **10 rows / 14 sites**, not the 11/16 this
document and the ledger prose carried from census A's summary sentence (section 10).

## 9. Deviations from this document's own plan

1. **Caller-count pins instead of token rows (R3-O-3).** Section 2 records this as
   deliberate; the measured cost it avoided is now known: `CAN_SEE(` has **86** production
   call sites and `get_char_room_vis(` **41**, so pinning the names as scanned tokens would
   have minted ~127 rows and raised the ceiling by the same amount for zero proof value.
   `PINNED_CALLER_COUNTS` fails `--check` on drift in either direction instead.
2. **The registry's multi-literal cell (R3-C-7, T1d).** Section 2 specified "ONE guard at
   each dispatcher's dispatch statement". Three entries needed two:
   `command_interpreter` (`interpre.cpp:1119` + `:1175`), `do_cast` (`spell_pa.cpp:499` +
   `:928`) and `cast_mass_spell` (`mystic.cpp:1010` + `:1046`). The `Guard literal` cell now
   holds several literals separated by ` || `, parsed structurally (anchored on the backticks)
   because `one_mobile_activity`'s single literal contains a real C `||` operator that a
   naive split would tear into two halves that each still match. The tripwire family is
   **12 statements across 9 entry-point names**, all sharing one message string per name, so
   R-final's measured-zero sweep (which greps per NAME) is unaffected.
3. **T1b's `spec_pro_tests` re-fixture premise was OVERTURNED, not deferred.** The task brief
   asked for `spec_pro_tests.cpp:106`/`:1174` to be re-placed on the grounds that the
   `issue_command` guard would silently stop them exercising their bodies. Three independent
   checks said otherwise (`handle_pracs` reaches no dispatcher at all; `:1174` is a
   destructor line; and an abort-probe build of all nine guards produced exactly five aborts,
   all `CombatHooksDispatch`). The fixtures were left alone.
4. **T3p's GUARDED shape: per-function guards at the point of use, not "one guard at
   `raw_kill`'s entry".** Not implementable as briefed — `get_corpse_desc`'s read is two
   frames below `raw_kill` behind a required return value, and an entry-evaluated flag would
   be stale by `:982` because `call_special`/`stop_riding`/the `affect_remove` loop run in
   between. Landed as one guard per function (`fight.cpp:927` in `death_cry`, a leading
   `location_of(ch) != NOWHERE &&` term at `fight.cpp:567` in `get_corpse_desc`), each
   red-first with an in-body positive control.
5. **T1c STOPped, and T1d repaired the wave's own policy.** Reading `do_cast` from its `:499`
   guard to its dispatch found **two** statements that can relocate `ch` —
   `complete_delay(ch)` (`spell_pa.cpp:516`, mainline: every prepared-spell cast reaches it,
   and it re-enters `command_interpreter` at `comm.cpp:2837` and from there arbitrary spec
   procs) and `appear(ch)` (`:721`, `affect_from_char` → `affect_total` → `affect_modify`'s
   APPLY_SPELL arm). T1d's own full read of `:499`..`:933` found a **third** the STOP had not
   named: `check_hallucinate(ch, tar_char)` at `spell_pa.cpp:895`, one statement above the
   dispatch, whose `AFF_HALLUCINATE` arms reach the same `affect_total` chain. R3-C-7's
   adjacency answer closed all three structurally rather than by exhaustion. The same audit
   found `cast_mass_spell` needed an in-loop tripwire (its three spells all reach
   `affect_total` on a victim, and the leader is a member of its own group) and that the
   other six entries were already adjacent.
6. **The `cast_mass_spell` entry guard at `mystic.cpp:1010` is redundant for refusal and was
   KEPT** (controller decision, T1d §4's disclosure): with the in-loop guard at `:1046`
   present, neutering `:1010` leaves all three of that entry's tests green. It stays as the
   loop's stated precondition and remains pinned by the registry's downward literal check.
   R4 may simplify it to a single literal per entry.

## 10. The stayed-TODO inventory for `src/combat/` — 29 rows / 56 sites

Re-derived at `6c2bc034` from the ledger's own combat rows; the categories come from the task
reports that refused each row.

| Category | Rows | Sites | Rows |
|---|---|---|---|
| `APPLY_SPELL-window` (R3-O-2) | 10 | 14 | `mage.cpp` — `spell_beacon·room_of(` (2), `spell_black_arrow·SUN_PENALTY(` (1), `spell_dark_bolt·SUN_PENALTY(` (1), `spell_freeze·EXIT(` (2), `spell_lightning_bolt·OUTSIDE(` (1), `spell_lightning_strike·OUTSIDE(` (1), `spell_lightning_strike·room_of(` (1), `spell_mist_of_baazunga·room_of(` (3), `spell_searing_darkness·SUN_PENALTY(` (1), `spell_spear_of_darkness·SUN_PENALTY(` (1) |
| Unproven extra door (T3d) | 5 | 13 | `fight.cpp·find_door·EXIT(` (8) and, inheriting it, `visibility.cpp·generic_find·room_of(` (1) + `visibility.cpp·get_obj_vis·room_of(` (1) — the `shop_keeper` HOST door; `clerics.cpp·do_mental·room_of(` (1) — `perform_violence`; `fight.cpp·perform_drop·room_of(` (2) — `script.cpp:1077` |
| Medium-confidence refusal | 8 | 19 | `fight.cpp·die·room_of(` (1), `fight.cpp·exp_with_modifiers·room_of(` (2), `limits.cpp·affect_update_person·room_of(` (1), `limits.cpp·point_update·room_by_id_total(` (3), `mage.cpp·different_zone·room_by_id_total(` (2), `mage.cpp·spell_summon·room_of(` (2, victim half), `ranger.cpp·on_windblast_hit·EXIT(` (5), `ranger.cpp·on_windblast_hit·room_of(` (3) |
| `scale-flagged` (R3-O-3) | 2 | 3 | `visibility.cpp·CAN_SEE·room_of(` (2, the `:578`/`:580` half) and `visibility.cpp·get_char_room_vis·room_of(` (1) |
| `intervening-relocation` (T1c) | 1 | 2 | `visibility.cpp·target_from_word·EXIT(` — its `do_cast` door (`spell_pa.cpp:624`) sits BETWEEN `do_cast`'s two tripwires |
| Descriptor premise (T2d) | 1 | 2 | `spell_pa.cpp·do_sense_magic·room_by_id_total(` — the `character` half needs "a `CON_PLYNG` descriptor has a placed character" |
| `owner-punted` (R3-O-4) | 1 | 2 | `olog_hai.cpp·olog_hai::get_random_target·room_of(` |
| TASK-018 (out-of-charter defect) | 1 | 1 | `mage.cpp·spell_fireball·room_of(` — census A rated it HIGH `entry-guard`; T2p OVERTURNED it (the orc-fumble arm sets `victim = caster`, so the NPC-death path frees the caster before `:1877`, a live use-after-free, filed as TASK-018) |
| **Total** | **29** | **56** | |

**A census-arithmetic correction, recorded because both this document and the ledger prose
carried it.** Section 2's "of which APPLY_SPELL-window-reachable: 11 rows / 16 sites" comes
from census A's summary SENTENCE. Counting that census's own sub-table gives **10 rows / 14
sites** (T2d's finding, re-derived here from the landed rows); the eleventh row is
`do_sense_magic`, whose C-2 column reads "n/a" and which stays `TODO` on the CON_PLYNG
premise instead. The stayed-`TODO` TOTAL is unaffected — only the labelling was wrong.

## 11. Design inputs for R4 (`src/script/`) and the eventual app tier

Recorded, not widened — each is a policy-shaped gap this wave measured and deliberately did
not close.

1. **A SPECIAL *host* is never covered by the `activate_char_special` entry.** That entry
   guards `victim` (parameter 2, the acting character), explicitly NOT `character` (the host
   mob) — `interpre.h:139`'s declaration and all six production call sites were read to
   confirm it. `src/app/shop.cpp` then calls four ACMD bodies DIRECTLY with a host:
   `do_unlock`/`do_open` at `shop.cpp:92`/`:93` and `:133`/`:134`, `do_close`/`do_lock` at
   `:123`/`:124`, inside `closing_time` (`:83`) and `opening_time` (`:128`), whose only
   callers are `shop.cpp:581`/`:589` inside `SPECIAL(shop_keeper)` (`:543`), where
   `keeper = host;` (`:555`). `shop_keeper` has no guard of its own. **Cost/benefit measured:
   one guard on `shop_keeper`'s host would drain 3 rows / 10 sites** (`find_door` +
   `generic_find` + `get_obj_vis`) — the cheapest remaining ratio in this tier.
2. **M-4's direct doors bypass both SPECIAL invokers — TWO functions, SIX arms.**
   `game_types::delayed_command_interpreter::run` dispatches a spec-proc body three ways,
   always with `m_character` as the host and `victim = NULL`, never routing through
   `activate_char_special` at all: the fn-ptr read at `delayed_command_interpreter.cpp:45`
   and called at `:47`; the `get_special_function(function_number)` lookup at `:50` called
   at `:51`; and `intelligent(...)` at `:53`. `complete_delay_impl`
   (`src/app/comm.cpp`) is the same class, also three ways: `(*mob_index[ch->nr].func)(...)`
   at `:2830` behind its `:2829` presence test, the `virt_program_number(...)` lookup at
   `:2832` called at `:2833`, and `intelligent(...)` at `:2835`. Any R4 policy for spec-proc
   bodies has to name every one of them. All six are now VISIBLE to the gate rather than
   invisible to it: they
   carry seven pinned keys in `DISPATCH_TOKEN_EXEMPT_SITES` (category `STOP`) whose reason
   text reads "M-4 direct
   door, R4 design input", so a reviewer meets them in the script instead of having to
   rediscover them. Registering them instead would assert they have a guard; R3 wrote none.
   **Recorded correction (Task 5-fix2, re-verification M-5):** the Task 5-fix round named
   each function by its `mob_index[].func` arm alone — the `store_prog_number` and
   `special_prog_number` arms of both, four dispatches in total, were unnamed here and
   untokened by the gate until this round.
3. **`do_sense_magic`'s CON_PLYNG premise.** `spell_pa.cpp:132`'s two sites read
   `character = player->character` for every `CON_PLYNG` descriptor in
   `get_descriptor_list_head()`'s list. "A `CON_PLYNG` descriptor has a placed character" is
   an invariant nothing in this program has proven; it is the natural home for whatever
   premise the app tier writes for `descriptor_list` walks. The row is not splittable (both
   sites share one physical line, one function and one token).
4. **The redundant `cast_mass_spell` entry guard** (deviation 6 above) is a live example of
   the shape R4 will hit whenever an entry needs both an early and a pre-dispatch tripwire:
   once the adjacent one exists, the early one may stop being independently test-pinned. The
   registry's downward literal check is what keeps it honest in the meantime.
5. **`target_from_word`'s `EXIT(` row** shows the residual the adjacency rule leaves: a site
   reached through a door that sits BETWEEN an entry's two tripwires. Closing it needs a
   third tripwire immediately above `spell_pa.cpp:624` — a production change no ruling has
   authorized.
