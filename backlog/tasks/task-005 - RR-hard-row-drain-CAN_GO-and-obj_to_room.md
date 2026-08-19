---
id: TASK-005
title: 'RR hard-row drain: CAN_GO and obj_to_room'
status: To Do
assignee: []
created_date: '2026-08-19 02:07'
labels: []
milestone: m-0
dependencies: []
priority: medium
ordinal: 5000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Drain the two R2-refused hard rows: CAN_GO (scale-flagged, 42 tree-wide call sites) and obj_to_room (medium-confidence refusal — 20 callers across 3 provenance patterns; R2's rule was 'no medium-confidence proofs land'). Each needs either a caller-by-caller proof campaign or a guard, done at a confidence level R2 could not afford.

## Why
Source: Wave R2's stayed-TODO taxonomy (AGENTS.md RR Wave R2 paragraph; docs/superpowers/room-resolve-playbook.md stayed-TODO section). These rows block the flip wave's zero-TODO precondition but were deliberately refused in R2 rather than landed at medium confidence — the refusal reasons are the spec for what a proper drain must overcome.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 CAN_GO's 42 call sites dispositioned (proof, guard, or a macro-level redesign that makes per-site proof unnecessary)
- [ ] #2 obj_to_room's 3 provenance patterns each proven or guarded at full confidence; ledger updated
- [ ] #3 MAXIMUM_TODO_COUNT lowered by the measured drain; gates green both hosts
<!-- AC:END -->
