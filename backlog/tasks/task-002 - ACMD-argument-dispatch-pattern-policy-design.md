---
id: TASK-002
title: ACMD-argument dispatch-pattern policy design
status: In Progress
assignee: []
created_date: '2026-08-19 02:06'
updated_date: '2026-08-21 19:29'
labels: []
milestone: m-0
dependencies: []
documentation:
  - docs/superpowers/room-resolve-playbook.md
priority: high
ordinal: 2000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Design the classification policy for the ACMD-argument-ch dispatch-pattern class: sites where the room id comes from a ch handed in by command dispatch, so per-site proof is the wrong shape. Owner ruling in R2 formally deferred this class to R3+ with the instruction to design it with its own mini-census and a standing invariant test. R2 left 6 rows / 8 sites in this class (5 OLC sites across 4 rows + 3 weather_to_char sites across 2 rows).

## Why
Source: owner ruling recorded in Wave R2 (see AGENTS.md's RR Wave R2 paragraph and docs/superpowers/room-resolve-playbook.md's 'Dispatch-pattern deferral, awaiting the R3+ policy design' section). The playbook warns that many of src/combat/'s and src/script/'s do_*/ASPELL bodies are exactly this class, so the policy must exist before or alongside R3 or the same deferral multiplies across the two biggest remaining tiers.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 Dedicated mini-census enumerating the dispatch-pattern class tree-wide (not just the R2 leftovers)
- [x] #2 Policy designed and owner-ruled: either a proof kind with a standing invariant test, or a recorded permanent disposition
- [ ] #3 The R2-deferred 6 rows / 8 sites dispositioned under the new policy; ledger and MAXIMUM_TODO_COUNT updated accordingly
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-08-21 (RR Wave R3 Task 4, docs fold-in at commit 8dea8bc1) — status: NOT Done; AC#1/#2 ticked, AC#3 substantially done but held open on one clause.

AC#1: the dedicated tree-wide mini-census is .superpowers/sdd/2026-08-21-rr3-combat/rr3-dispatch-census.md (Task 0, Agent C) — it bounded the class at 106 rows / 375 sites (52% of all remaining TODO sites), found that no dispatcher checked placement (command_interpreter checks position only; issue_command's 148 callers check nothing; the skills[].spell_pointer doors check nothing; shape_center bypasses even the position check), enumerated the two live NOWHERE producers reachable by dispatch (P1, the login/rent-load window; P5, the char_to_room(X, location_of(Y)) leaks) and recorded five overturns, two of which changed this task's own scope (weather_to_char is really caller-contract; mobact's one_mobile_activity is entry-guard).

AC#2: owner ruling R3-O-1 adopted policy (A), dispatch-invariant — a sixth citation-required PROOF_KIND whose entry set is CLOSED by a marker-anchored 12-row dispatch-entry registry that tools/room_resolve_census.py --check asserts in two directions, backed by 24 new self-test directions (28 -> 52 named directions overall) and 18 standing unplaced/placed discriminator tests, one pair per entry point. Coordinator ruling R3-C-7 then added the adjacency rule and multi-literal registry cells after Task 1c STOPped on do_cast. The policy design document is .superpowers/sdd/2026-08-21-rr3-combat/rr3-policy-design.md; the as-built account is docs/superpowers/specs/2026-08-21-rr3-combat-design.md sections 5-11.

AC#3: all 6 R2-deferred rows / 8 sites are dispositioned. The 2 weather_to_char rows (3 sites) landed caller-contract under ruling R3-C-4 in Task 3p (dfd25825); the 4 OLC rows (5 sites) landed PROVEN / dispatch-invariant in Task 3e (400ab2e0) — do_shape on command_interpreter's pre-dispatch tripwire, the three shape_center_* callees on shape_center's own entry, every caller of every callee re-grepped at HEAD. src/olc/ now has zero TODO rows. The clause still open is 'and MAXIMUM_TODO_COUNT updated accordingly': the ceiling literal is 585 while the --check-derived TODO site-sum is 579, because this wave's standing rule reserves the lowering to the controller at finalization. AC#3 closes at T5 with that one edit; nothing else is owed on this task.
<!-- SECTION:NOTES:END -->
