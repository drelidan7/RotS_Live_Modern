---
id: TASK-030
title: 'msdp_room_update_impl: placed-character early-return looks inverted'
status: Done
assignee: []
created_date: '2026-08-24 01:05'
updated_date: '2026-09-07 16:09'
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
- [x] #1 Determine whether the >= 0 early-return is inverted or an intentional disable; record the verdict with git-history evidence
- [x] #2 If inverted: fix red-first with a test driving msdp_room_update through the seam; if intentional: replace the dead block with a dated comment
- [x] #3 Boot goldens byte-identical; censuses green
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Implemented with TASK-015 N/M. Upstream commit48d5c4b2e1da258649d126aea3c1a586ac9c9413 (2026-07-24) explicitly fixes the inverted >=0 guard; its message confirms ROOM never updated during normal movement. This was a defect, not an intentional disable. Modern msdp_room_update_impl now validates the supplied actor's location and each copied exit before resolving, and uses that actor consistently through the existing output seam. Red-first RoomUpdateThroughRealOutputSeamReportsSuppliedActorsRoomOnce and RoomUpdateRejectsBothNegativeAndAboveWorldLocations cover real emitted packets, descriptor/actor mismatch and bounds; existing placed/unplaced tests were corrected. Final native/Linux/ASan full2194-test suites pass; both boot goldens match and all3censuses plus applicable self-tests exit0. Evidence is in TASK-015 doc-003 and /tmp/task015-final-closure-{native,linux}-boot.log. Changes remain local and uncommitted.
<!-- SECTION:NOTES:END -->

## Final Summary

<!-- SECTION:FINAL_SUMMARY:BEGIN -->
Confirmed and fixed the inverted MSDP room guard through Placement and the existing output seam. Real-wire regression tests, native/Linux/ASan suites, both boot goldens and all censuses pass. Implemented locally as part of TASK-015.
<!-- SECTION:FINAL_SUMMARY:END -->
