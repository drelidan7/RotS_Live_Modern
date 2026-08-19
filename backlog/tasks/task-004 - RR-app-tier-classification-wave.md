---
id: TASK-004
title: RR app-tier classification wave
status: To Do
assignee: []
created_date: '2026-08-19 02:07'
labels: []
milestone: m-0
dependencies: []
documentation:
  - docs/superpowers/room-resolve-playbook.md
priority: medium
ordinal: 4000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Classify the remaining src/app/ (and flat/shared-header) room-resolve TODO rows — the largest and last tier. Budget the extra per-site work of the two-room-macro recipe: the playbook records that the do_look/do_exits two-room-macro pattern is exclusively an app-tier concern and neither R3 nor R4 will have exercised it.

## Why
Source: docs/superpowers/specs/2026-07-31-room-resolve-retirement-design.md (program order: library tiers, then app) and docs/superpowers/room-resolve-playbook.md (the two-room-macro pitfall section, which explicitly assigns its cost to 'the eventual src/app/ wave'). This wave drains the bulk of the remaining ~700-site TODO ceiling and is the last classification wave before the flip.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Task-0-style mini-census first; every remaining production TODO row classified or stayed-TODO with recorded reason
- [ ] #2 Two-room-macro (do_look/do_exits-class) sites handled per the playbook's dedicated recipe
- [ ] #3 MAXIMUM_TODO_COUNT lowered to the measured residual; ledger and docs fold-ins updated; gates green both hosts
<!-- AC:END -->
