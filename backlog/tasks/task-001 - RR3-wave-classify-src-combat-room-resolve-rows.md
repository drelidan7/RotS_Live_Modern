---
id: TASK-001
title: 'RR3 wave: classify src/combat/ room-resolve rows'
status: In Progress
assignee: []
created_date: '2026-08-19 02:06'
updated_date: '2026-08-21 19:29'
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
- [x] #1 Task-0-style read-only mini-census of src/combat/ rows written before any classification, with advisory-overturn discipline (premises verified against code, not the census's own guesses)
- [x] #2 Every in-scope row classified PROVEN/GUARDED or stayed-TODO with an enumerated reason in docs/superpowers/room-resolve-ledger.md; no medium-confidence proofs land
- [ ] #3 MAXIMUM_TODO_COUNT lowered by the measured (--check-derived, not hand-computed) drained-site count
- [x] #4 Any GUARDED behavior change lands red-first-tested with boot goldens byte-identical before and after
- [ ] #5 Standing gates green both hosts (macOS native + rots64), censuses exit 0; AGENTS.md/BUILD.md fold-ins and the playbook cost-table row updated
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-08-21 (RR Wave R3 Task 4, docs fold-in at commit 8dea8bc1) — status: NOT Done; AC#1/#2/#4 ticked, AC#3 and AC#5 remain, both blocked on T5 finalization only.

Measured at HEAD (parse_ledger()/ctest -N, not copied from a report): 95 rows / 186 sites in scope became 100 rows after five mixed-class splits; 130 combat sites drained, 29 rows / 56 sites stay TODO across eight enumerated categories; ledger-wide TODO 716 -> 579 sites; ctest 1865 -> 1890 (+25); 12 dispatch tripwires across 9 entry-point names plus 3 red-first GUARDED behavior changes, native boot golden byte-identical at every commit.

AC#3 is PARTLY met: MAXIMUM_TODO_COUNT was lowered twice from --check-derived figures (716 -> 717 when ruling R3-C-3 reopened report_zone_power, then 717 -> 636 and 636 -> 585 at the two integration points). It stands at 585 while the derived TODO site-sum is 579 (Task 1c drained 1 and Task 3e drained 5 after the last lowering). By this wave's own standing rule the controller lowers the literal once at finalization, --check-derived — so AC#3 closes at T5, not here.

AC#5 is PARTLY met: the AGENTS.md / BUILD.md fold-ins and the playbook cost-table row + estimation note are DONE (this task, commits 461e3f0f/8dea8bc1/a9c71a92), and all three censuses exit 0. What remains is the both-hosts half: the rots64 leg is measured only at the T2p+T3p integration commit f4336565 (0 warnings, 1886/1886, boot golden matches), not at final HEAD. Still owed at T5: the final-HEAD rots64 leg, make smoke-account (MANDATORY — do_cast/do_use/command_interpreter and the raw_kill/death_cry path are all touched), the i386 battery, the six blocking CI jobs, and the dual adversarial whole-branch review.
<!-- SECTION:NOTES:END -->
