# Arc: Room-Resolve Retirement

## Intent
Prove, site by site, that every room id handed to a `room_data::operator[]`-reaching spelling is
in-range before dereference — then retire the operator's silent room-0 fallback to a hard abort.
The fallback has masked bad-room bugs for decades; the program converts that silence into either a
proof, a guard, or a crash at the defect.

## Stories
- As the game's owner, I want an out-of-range room id to fail loudly at its source, so that
  world-file and logic defects surface in testing instead of teleporting players to room 0.
- As a maintainer, I want a ratcheted census (`tools/room_resolve_census.py`) that makes any new
  unproven resolver site a build failure, so that the burndown can only move forward.
- As a builder/OLC user, I want malformed zone-file room references skipped with a log instead of
  silently resolving to room 0, so that world bugs are visible and harmless (landed in R2's
  `reset_zone` guards).

## Specs & decisions
- docs/superpowers/specs/2026-07-31-room-resolve-retirement-design.md — program spec; pending
  owner ruling RR-O-1 (§2a) blocks only the final flip wave.
- docs/superpowers/specs/2026-08-01-rr2-small-tiers-design.md — Wave R2 (entity/world/pathfind/olc).
- docs/superpowers/specs/2026-08-21-rr3-combat-design.md — Wave R3 (src/combat/ + the
  dispatch-pattern policy, TASK-001 + TASK-002); its as-built sections 5-11 carry the measured
  per-task chain, the deviations, the stayed-TODO inventory and the R4 design inputs.
- .superpowers/sdd/2026-08-21-rr3-combat/rr3-rulings.md — coordinator rulings R3-C-1..7 and owner
  rulings R3-O-1..4, standing for the whole wave.
- docs/superpowers/room-resolve-playbook.md — classification playbook R3+ reuses (proof kinds,
  pitfalls, GUARDED procedure, stayed-TODO taxonomy, R3/R4 cost estimation).
- docs/superpowers/room-resolve-ledger.md — the living classification ledger.

## Tasks
- TASK-001 — RR3 wave: classify src/combat/ room-resolve rows
- TASK-002 — ACMD-argument dispatch-pattern policy design
- TASK-003 — RR4 wave: classify src/script/ room-resolve rows
- TASK-004 — RR app-tier classification wave
- TASK-005 — RR hard-row drain: CAN_GO and obj_to_room
- TASK-006 — RR flip wave: retire room_data::operator[] room-0 fallback
- TASK-018 — spell_fireball NPC self-fumble use-after-free (mage.cpp:1877 reads a freed caster)
  — an out-of-charter defect Wave R3's census A turned up while classifying that row; filed
  rather than fixed, and the row stays TODO because of it

## How it unfolded
- 2026-08-01: Wave R1 merged (master @`3e963a32`, rebase-style, PR #30). Stood up
  `tools/room_resolve_census.py` (17 tokens, site-sum ratchet `MAXIMUM_TODO_COUNT=788`) and the
  461-row / 1273-site ledger; zero production C++. Dual whole-branch review findings W-1..9/F-1..7
  closed in the same wave.
- 2026-08-01: Wave R2 merged (master @`143f78fa`, PR #31). Small tiers classified: 72 of 83
  in-scope sites drained (69 PROVEN + 3 GUARDED), ceiling 788 → 716; three real `reset_zone`
  defects fixed red-first with boot goldens byte-identical; the classification playbook written.
  9 rows / 11 sites stayed TODO under recorded rulings (RR-O-1 hold, the dispatch-pattern class
  deferred by owner ruling to R3+ policy design, CAN_GO scale-flag, obj_to_room refusal).
- 2026-08-21: Wave R3 classification and fold-ins complete on `arch/rr3-combat`; **T5
  finalization and merge still pending** (TASK-001 and TASK-002 both stay In Progress). The wave
  folded TASK-002 into TASK-001 by owner ruling and settled the policy BEFORE any classification
  task, which is what let the largest remaining tier drain at all. Measured at the docs commit,
  re-derived from the tool and the ledger rather than from any task report:
  - **Policy.** Owner ruling R3-O-1 adopted `dispatch-invariant`, the program's sixth proof kind
    and the first whose evidence lives entirely outside the row's own function: the DISPATCHER
    guards the actor. 14 tripwire statements across 9 entry-point names (12 as the wave first
    landed; the Task 5-fix round added `do_use`'s two R3-C-7 adjacency tripwires), closed by a
    12-row dispatch-entry registry the gate checks in two directions, 18 standing discriminator
    tests (one unplaced/placed pair per entry) and 24 new self-test directions at the time
    (28 → 52; the two review-fix rounds took it to 76).
  - **Drain.** 138 sites: 130 of `src/combat/`'s 186, plus the 5 OLC sites and 3
    `weather_to_char` sites Wave R2 had deferred pending exactly this policy — `src/olc/` is now
    at zero TODO. Ledger-wide TODO 716 → 579 sites (271 → 200 rows); `MAXIMUM_TODO_COUNT`
    716 → 717 → 636 → 585, every step `--check`-derived, with the last lowering to 579 reserved
    for the controller at T5.
  - **Refusals.** 29 rows / 56 sites stay TODO in combat, every one with an enumerated reason,
    across eight categories — three of them new and named this wave (`APPLY_SPELL-window`,
    `intervening-relocation`, `owner-punted`). The wave's own "no medium-confidence proofs land"
    rule accounts for 19 of those 56 sites.
  - **Real defects.** Three GUARDED behavior changes, each red-first with an in-body positive
    control (`spell_blink`'s already-failed blink no longer resolves NOWHERE; an unplaced dying
    character's death cry no longer broadcasts into room 0's neighbours; an unplaced corpse no
    longer inherits room 0's "floating here" wording), plus TASK-018 filed for a use-after-free
    census A found while overturning `spell_fireball`'s HIGH-confidence classification.
  - **What it cost.** 23 classification/production commits over 11 task-units, +25 tests
    (1865 → 1890), and **two whole repair TASKS** — T1c (the entry guard did not dominate
    `target_parser`) and T1d (implementing ruling R3-C-7's adjacency answer after T1c STOPped on
    `do_cast`, where three readers of the same 430 lines found two, then three statements that
    can relocate the actor between guard and dispatch). The playbook's R2-derived projection
    that R2's 13% stayed-TODO rate was "very likely a floor" for combat is superseded by the
    measured 30%; its warning that the dispatch policy was a PREREQUISITE for R3 held exactly.
