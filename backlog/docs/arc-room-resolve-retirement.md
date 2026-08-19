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
