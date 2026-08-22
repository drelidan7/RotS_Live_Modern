---
id: TASK-019
title: >-
  spell_earthquake: caster's own crack-fall can free the caster mid-loop
  (mage.cpp fall loop)
status: Done
assignee: []
created_date: '2026-08-22 17:20'
updated_date: '2026-08-22 17:24'
labels: []
milestone: m-0
dependencies: []
priority: high
ordinal: 19000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Found while reviewing TASK-018's fix against spell_earthquake as the precedent. Earthquake's damage loop excludes the caster (`tmpch != caster`), but its crack/fall loop does not: on the coin flip `!number(0, 1)` the caster is moved into the crevice and takes fall damage via `apply_spell_damage(caster, caster, ...)` INSIDE the occupant loop. A lethal fall runs damage() -> die() -> raw_kill() -> extract_char(), which free_char()s an NPC caster (or re-places a player) — and the loop then continues to later occupants with `new_saves_spell(caster, ...)`, `act(..., caster, ...)` and `apply_spell_damage(caster, ...)` on the dead caster. Same defect class as TASK-018, rarer (needs the crack, the coin flip, and lethal fall damage).

## Why
Source: owner review of TASK-018 (2026-08-22) — "fix it with the same general principles": the caster's own fall is deferred to AFTER every other occupant has resolved, so a freed or relocated caster is unreachable by construction and the rest of the room still falls. The ledger's spell_earthquake rows are PROVEN on the caster's entry guard; that proof is incomplete for the fall loop's later iterations until this lands.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 Red-first test drives the real fall/damage/die/raw_kill pipeline with the extract_char seam stubbed (the TASK-018 fixture shape) and fails against the unfixed loop on ordering
- [x] #2 Fix: the caster's own fall (and its damage) is processed after every other occupant; the non-caster path's behavior and RNG draw order are unchanged
- [x] #3 Boot goldens + seed42 golden byte-identical; ASan+UBSan clean on the mage suite; ledger rows for spell_earthquake updated and MAXIMUM_TODO_COUNT re-derived
<!-- AC:END -->

## Notes

2026-08-22: fixed on fix/task-018-spell-fireball-uaf alongside TASK-018. AC#2 precision: the caster's landing-save draw stays at its original point in the loop, so no occupant's RNG sequence changes; only the caster's fall (and its damage) moves to the end. The test witnesses ORDER via the extract_char stub (bystander location at extraction).
