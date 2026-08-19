---
id: TASK-007
title: 'LS-4: obj_data::in_room object-side location swap'
status: To Do
assignee: []
created_date: '2026-08-19 02:07'
labels: []
milestone: m-1
dependencies: []
documentation:
  - docs/superpowers/specs/2026-07-23-locationsystem-program-design.md
priority: low
ordinal: 7000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Optional follow-on campaign to the completed LocationSystem program: give obj_data's location the same treatment char_data got in LS-1..LS-3b — reads through an API, then a private-handle representation swap. obj_data::in_room is still a raw public int (src/core/include/rots/core/object.h:165, verified 2026-08-18). Needs its own census before any conversion.

## Why
Source: the LocationSystem program's close-out (memory library-split-progress, 2026-08-01: 'Remaining after that: two OPTIONAL campaigns: operator[] room-0 fallback retirement ... and obj_data::in_room (LS-4)' — the first became the RR program; this is the second). The char-side program proved the census-gate-swap method over four waves; the object side is the symmetric debt. OPTIONAL: no owner commitment yet — confirm scope with the owner before standing up a wave.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Owner go/no-go obtained before any wave brief is written
- [ ] #2 LS-1-style census of obj_data::in_room readers/writers, with its own gate token plan
- [ ] #3 If green-lit: reads behind an API, then the representation swap, per the LS program's staged pattern with the standing verification cadence
<!-- AC:END -->
