---
id: TASK-022
title: >-
  spell_blaze: room-arm save-bonus precedence bug and pre-guard caster
  dereference
status: To Do
assignee: []
created_date: '2026-08-22 18:19'
labels: []
milestone: m-0
dependencies: []
priority: low
ordinal: 22000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Two non-crash defects found during the TASK-020 analysis, split out of that crash fix on purpose (TASK-020 AC#3): fixing the first changes blaze's room-arm save rolls, which deserves its own red-first test rather than riding a walk-safety fix.

1. mage.cpp, spell_blaze room arm: `new_saves_spell(caster, tmpch, save_bonus + tmpch == caster ? 3 : 0)` parses as `((save_bonus + tmpch) == caster) ? 3 : 0` -- pointer arithmetic compared to a pointer (never equal in practice), so the specialization save bonus from get_save_bonus() is DROPPED and 0 is passed. Intended: `save_bonus + (tmpch == caster ? 3 : 0)`; note the caster term is dead anyway because is_friendly_taget(caster, caster) skips the caster two lines earlier.
2. mage.cpp, spell_blaze entry: `int level = get_mage_caster_level(caster);` dereferences caster before the `if (!caster) return;` guard (and the victim arm's `caster ? caster : victim` also implies a null caster was once possible). No dispatcher passes a null caster today (dispatch census, RR Wave R3), so this is a lying guard rather than a live crash; either hoist the guard or delete the dead null-handling and say so.

## Why
Source: TASK-020 analysis (2026-08-22). Low priority: no crash, no golden impact known; the save-bonus fix is a small balance change (fire-spec casters' blaze room saves get their -2) that should land with a test and a named behavior change.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Room-arm save bonus passes save_bonus + (tmpch == caster ? 3 : 0); a test with spec'd caster/victim pins the bonus reaching new_saves_spell
- [ ] #2 The null-caster handling in spell_blaze is made consistent (guard hoisted or dead arms removed) with the reason stated
- [ ] #3 Boot goldens + seed42 golden byte-identical; ASan clean on the mage suite
<!-- AC:END -->
