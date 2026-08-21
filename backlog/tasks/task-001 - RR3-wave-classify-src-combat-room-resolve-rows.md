---
id: TASK-001
title: 'RR3 wave: classify src/combat/ room-resolve rows'
status: In Progress
assignee: []
created_date: '2026-08-19 02:06'
updated_date: '2026-08-21 15:14'
labels: []
milestone: m-0
dependencies: []
documentation:
  - docs/superpowers/room-resolve-playbook.md
  - docs/superpowers/specs/2026-07-31-room-resolve-retirement-design.md
priority: high
ordinal: 1000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Run the next classification wave of the room-resolve retirement program over src/combat/ (~95 rows / 186 sites per the playbook's estimation note — a Task-0-style mini-census is still mandatory and re-derives the real numbers). Follow docs/superpowers/room-resolve-playbook.md: proof-kind recipes, the pitfalls list, the GUARDED procedure, the stayed-TODO taxonomy.

## Why
Source: docs/superpowers/specs/2026-07-31-room-resolve-retirement-design.md and docs/superpowers/room-resolve-playbook.md (its 'Estimation note for R3' section, ~95 rows / 186 sites). Wave R2 (small tiers, merged to master 2026-08-01 @143f78fa) drained 72 of 83 in-scope sites and wrote the playbook explicitly so R3 (src/combat/) could reuse it. src/combat/ is the largest remaining library tier; the program's end state — retiring room_data::operator[]'s room-0 fallback to an abort — needs every TODO row proven or guarded first.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Task-0-style read-only mini-census of src/combat/ rows written before any classification, with advisory-overturn discipline (premises verified against code, not the census's own guesses)
- [ ] #2 Every in-scope row classified PROVEN/GUARDED or stayed-TODO with an enumerated reason in docs/superpowers/room-resolve-ledger.md; no medium-confidence proofs land
- [ ] #3 MAXIMUM_TODO_COUNT lowered by the measured (--check-derived, not hand-computed) drained-site count
- [ ] #4 Any GUARDED behavior change lands red-first-tested with boot goldens byte-identical before and after
- [ ] #5 Standing gates green both hosts (macOS native + rots64), censuses exit 0; AGENTS.md/BUILD.md fold-ins and the playbook cost-table row updated
<!-- AC:END -->
