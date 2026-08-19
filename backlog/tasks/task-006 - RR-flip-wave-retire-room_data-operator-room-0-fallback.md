---
id: TASK-006
title: 'RR flip wave: retire room_data::operator[] room-0 fallback'
status: To Do
assignee: []
created_date: '2026-08-19 02:07'
labels: []
milestone: m-0
dependencies:
  - TASK-001
  - TASK-002
  - TASK-003
  - TASK-004
  - TASK-005
documentation:
  - docs/superpowers/specs/2026-07-31-room-resolve-retirement-design.md
priority: medium
ordinal: 6000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
The program's terminal wave: with every production row PROVEN/GUARDED, flip room_data::operator[]'s out-of-range behavior from the room-0 fallback (plus negative-room mudlog) to a hard abort. Includes dispositioning the char_to_room row held under RR-O-1 (its stay-TODO 'by design' preserves operator[]'s negative-room mudlog — the flip design must replace that diagnostic).

## Why
Source: docs/superpowers/specs/2026-07-31-room-resolve-retirement-design.md §2a. Owner ruling RR-O-1 is PENDING and blocks exactly this wave (AGENTS.md: 'pending owner ruling RR-O-1 §2a blocks only the program's final flip wave, not R1'). BLOCKED until RR-O-1 is obtained and every TODO row is drained (tasks 001-005).

## Blocked on
Owner ruling RR-O-1 (§2a of the program spec) — a decision only the owner can make; ask with full context (spec §2a + current ledger state), never from a summary.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Owner ruling RR-O-1 obtained and recorded in the program spec
- [ ] #2 MAXIMUM_TODO_COUNT at 0 (or the owner-ruled residual) before the flip lands
- [ ] #3 operator[] fallback replaced per the ruling; char_to_room's mudlog diagnostic preserved or deliberately superseded
- [ ] #4 Full finalization battery: i386, both hosts, boot goldens, six CI jobs, dual adversarial whole-branch review per standing grants
<!-- AC:END -->
