---
id: TASK-023
title: >-
  Test fixture: ScopedZoneTableOwner is one zone slot short under
  recalc_zone_power (count vs inclusive index)
status: To Do
assignee: []
created_date: '2026-08-22 20:47'
labels: []
milestone: m-3
dependencies: []
priority: low
ordinal: 23000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Found during TASK-021 Task 4 (fight_credit_tests.cpp) under ASan: `src/tests/test_placement.h:403-417`'s `ScopedZoneTableOwner` allocates `new zone_data[1]` and sets `top_of_zone_table = 1` under the COUNT convention (`zone_load.cpp:59` rejects `znum >= top_of_zone_table`), but `db_world.cpp:2144`'s `recalc_zone_power()` loops `for (tmp = 0; tmp <= top_of_zone_table; tmp++)` writing `zone_table[tmp]` under the INCLUSIVE-INDEX convention -- a heap-buffer-overflow whenever a test pairs the shared fixture with a death (raw_kill -> group_gain/exp) or any recalc_zone_power path. Not live today: occupant_order_tests.cpp uses its own `top = 0` fixture and documents the trap at its lines 94-105; fight_credit_tests.cpp and room_affect_tick_tests.cpp carry file-local two-slot fixtures. The tree therefore has three zone-table fixtures and the shared one is the broken one.

## Why
Source: TASK-021 Task 4 review (2026-08-22, finding I-1). The next author who pairs the shared fixture with point_update()/raw_kill reintroduces an ASan-only failure. Fix: either make the fixture allocate top+1 slots (documenting the two conventions) or reconcile db_world.cpp:2144's loop with the count convention -- the latter is a production change that needs the boot golden and a census re-derivation, so decide which with the zone_load.cpp/db_world.cpp convention evidence in hand; then fold the two file-local fixtures back onto the shared one.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 ScopedZoneTableOwner (or recalc_zone_power) fixed so the shared fixture survives recalc_zone_power under ASan, with a test that pairs the fixture with a death
- [ ] #2 fight_credit_tests.cpp and room_affect_tick_tests.cpp file-local two-slot fixtures replaced by the shared one
- [ ] #3 Boot golden byte-identical if db_world.cpp changes; ASan clean
<!-- AC:END -->
