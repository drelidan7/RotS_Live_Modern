---
id: TASK-003
title: 'RR4 wave: classify src/script/ room-resolve rows'
status: To Do
assignee: []
created_date: '2026-08-19 02:06'
labels: []
milestone: m-0
dependencies: []
documentation:
  - docs/superpowers/room-resolve-playbook.md
priority: medium
ordinal: 3000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Classification wave over src/script/ (~44 rows / 118 sites per the playbook's estimation note; mini-census re-derives). Same procedure and gates as RR3.

## Why
Source: docs/superpowers/specs/2026-07-31-room-resolve-retirement-design.md and the playbook's R4 estimation note. Program order runs library tiers before the app tier; script is the second of the two large remaining library tiers. Expected to lean on the dispatch-pattern policy (task-002) since ASPELL/do_* bodies dominate.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Task-0-style mini-census written first; every in-scope row classified or stayed-TODO with recorded reason
- [ ] #2 MAXIMUM_TODO_COUNT lowered by the measured drained-site count; ledger, AGENTS.md/BUILD.md, and playbook cost table updated
- [ ] #3 Standing gates green both hosts; any GUARDED change red-first with byte-identical boot goldens
<!-- AC:END -->
