---
id: TASK-020
title: >-
  affect_update: a blaze-tick death frees the affected_list node the walk
  already saved as next
status: To Do
assignee: []
created_date: '2026-08-22 17:40'
labels: []
milestone: m-0
dependencies: []
priority: high
ordinal: 20000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Root cause of the historic 'blaze crashes' (analysis 2026-08-22, TASK-018 follow-up). affect_update() (limits.cpp:1563) walks the global affected_list with a pre-saved `tmplist2 = tmplist->next`. affect_update_room() re-casts the room's blaze on each occupant with caster == victim == occupant; a lethal tick runs damage() -> die() -> raw_kill(), which strips every affect of the dead character (fight.cpp:976), and affect_remove()'s tail then removes the character's own affected_list node via from_list_to_pool() -- which free()s it (entity_lifecycle.cpp:1204-1226). pool_to_list() inserts at the head, so the node right behind the blaze room's is whoever acquired their first affect just before the cast: typically the mage or a target standing in the blaze. If that is the dying occupant, affect_update's next iteration dereferences freed memory. Blaze is the only room affect whose re-cast deals substantial direct damage (room-poison does 5/tick and is reachable the same way; haze and mist deal none). NOT the TASK-018/019 class: blaze's own body is safe (the caster is skipped; the victim arm ends at the damage call).

Side findings (non-crash): mage.cpp blaze room loop `save_bonus + tmpch == caster ? 3 : 0` is a precedence bug (pointer arithmetic compared to a pointer, intended `save_bonus + (tmpch == caster ? 3 : 0)`); `get_mage_caster_level(caster)` runs before blaze's `if (!caster) return;`.

## Why
Source: owner request after TASK-018/019 ("there are historic crashes with blaze"). The walk-invalidation is the same principle the two prior fixes addressed (a cascade during an iteration destroys what the iteration still holds), one layer up in the affect engine; until it is closed, any affect-carrying character dying inside a room-affect tick can crash the server.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Red-first test orders affected_list as [blaze room, affected low-hp occupant], forces the 1-in-13 re-cast roll, runs affect_update(), and fails against the current code (ASan use-after-free on the freed node, or a deterministic witness)
- [ ] #2 Fix keeps affect_update's walk valid across deaths/removals triggered inside affect_update_room/affect_update_person without changing which entries get updated on a tick
- [ ] #3 The blaze precedence bug and the pre-guard caster deref are fixed in the same change or split into their own task with a reason
- [ ] #4 Boot goldens + seed42 golden byte-identical; ASan+UBSan clean on the touched suites; ledger/ceiling re-derived if any resolver site moves
<!-- AC:END -->
