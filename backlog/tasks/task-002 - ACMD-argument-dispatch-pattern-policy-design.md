---
id: TASK-002
title: ACMD-argument dispatch-pattern policy design
status: To Do
assignee: []
created_date: '2026-08-19 02:06'
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
- [ ] #1 Dedicated mini-census enumerating the dispatch-pattern class tree-wide (not just the R2 leftovers)
- [ ] #2 Policy designed and owner-ruled: either a proof kind with a standing invariant test, or a recorded permanent disposition
- [ ] #3 The R2-deferred 6 rows / 8 sites dispositioned under the new policy; ledger and MAXIMUM_TODO_COUNT updated accordingly
<!-- AC:END -->
