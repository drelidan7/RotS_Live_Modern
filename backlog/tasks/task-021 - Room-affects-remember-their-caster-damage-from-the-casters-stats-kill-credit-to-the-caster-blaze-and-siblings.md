---
id: TASK-021
title: >-
  Room affects remember their caster: damage from the caster's stats, kill
  credit to the caster (blaze and siblings)
status: To Do
assignee: []
created_date: '2026-08-22 17:44'
labels: []
milestone: m-0
dependencies:
  - TASK-020
priority: medium
ordinal: 21000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Today a room affect (blaze, room-poison, haze, mist of baazunga -- every `ROOMAFF_SPELL` written by `affect_to_room`) keeps no record of who cast it: `affected_type` (types.h:700) carries only type/duration/time_phase/modifier/location/bitvector, and `affect_update_room` (limits.cpp:1493-1501) re-casts the spell every tick with the OCCUPANT as both caster and victim (`spell_pointer(tmpch, "", SPELL_TYPE_SPELL, tmpch, ...)`). Consequences: the per-tick damage is computed from the victim's own caster level (a level-30 mage's blaze burns a level-5 victim as a level-5 spell, and the cast-time `modifier = level` is never used for damage); every blaze-tick kill is a self-kill (`killer == victim`), so the mage gets no kill credit or exp, a player victim takes `raw_kill`'s "died to a player" branch (hp/4 restore) because the killer is themselves, and pkill/exploit bookkeeping sees nobody; and the firestorm keeps burning identically after the caster dies or leaves.

Design the caster-preservation so the stored identity can never dangle (the TASK-018/019/020 lesson): store the caster's `abs_number` (plus a snapshot of the cast-time level/power already in `modifier`), never a raw `char_data*`, and resolve it through `char_exists()` at each tick; when the caster no longer exists, fall back to the current behavior (occupant-as-caster) explicitly, or to a "no credit" arm -- an owner ruling on which. The DoT char affects that self-damage (`point_update`'s `damage(i, i, 5, SPELL_POISON)` at limits.cpp:759 and the asphyxiation arm) have the same credit gap and can ride the same field if the owner wants them in scope.

## Why
Source: owner request 2026-08-22 after the TASK-020 analysis ("allow Blaze and other similar spells to preserve the caster information when it was cast, so the caster's information can be used for damage and so the original caster can get kill credit if their spell is what kills the target"). Depends on TASK-020: the affect-engine walk must be safe against deaths before kills inside it become a feature rather than a crash.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 affected_type (or a room-affect sidecar) records the caster's abs_number and cast-time power at affect_to_room time; no raw char_data* is stored
- [ ] #2 affect_update_room re-casts with the resolved original caster (char_exists) as caster and the occupant as victim; the fallback when the caster is gone is owner-ruled and tested
- [ ] #3 Blaze tick damage uses the cast-time level (modifier), not the victim's; a kill by a blaze tick credits the original caster (exp/group_gain, pkill/exploit path, 'died to a player' semantics) -- each pinned by a test
- [ ] #4 The other ROOMAFF_SPELL spells (room-poison, haze, mist) and, if ruled in scope, the self-damaging DoT affects are converted or explicitly excluded with a reason
- [ ] #5 Boot goldens + seed42 golden byte-identical or regenerated with the behavior change named in the commit; ASan+UBSan clean; ledger/ceiling re-derived for any moved resolver site
<!-- AC:END -->
