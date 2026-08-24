---
id: TASK-030
title: 'msdp_room_update_impl: placed-character early-return looks inverted'
status: To Do
assignee: []
created_date: '2026-08-24 01:05'
labels: []
milestone: m-3
dependencies: []
priority: medium
ordinal: 30000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
While adding TASK-025's null-desc guard, msdp_room_update_impl (src/app/act_move.cpp) was observed to early-return when location_of(ch) >= 0 -- i.e. for every PLACED character -- so the MSDP room-name/room-vnum update below it appears unreachable for exactly the characters it exists to serve, and would only run for an unplaced character (where room_of would then resolve a negative id). Either the comparison is inverted (should be < 0, a real player-facing MSDP bug) or MSDP room updates are deliberately disabled and the dead block should say so.

## Why

Filed 2026-08-23 from TASK-025 implementation (recorded in that task's Implementation Notes): the function was hit while debugging the new spell_summon body test's SIGSEGV. Not fixed there -- separate concern, needs its own repro (an MSDP-negotiated client shape) and red-first test.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Determine whether the >= 0 early-return is inverted or an intentional disable; record the verdict with git-history evidence
- [ ] #2 If inverted: fix red-first with a test driving msdp_room_update through the seam; if intentional: replace the dead block with a dated comment
- [ ] #3 Boot goldens byte-identical; censuses green
<!-- AC:END -->
