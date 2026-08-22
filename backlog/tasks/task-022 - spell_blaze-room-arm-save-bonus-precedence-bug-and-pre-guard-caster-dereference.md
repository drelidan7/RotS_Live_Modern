---
id: TASK-022
title: >-
  spell_blaze: room-arm save-bonus precedence bug and pre-guard caster
  dereference
status: To Do
assignee: []
created_date: '2026-08-22 18:19'
updated_date: '2026-08-22 23:24'
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
- [ ] #2 The null-caster handling is made consistent (guard hoisted or dead arms removed) with the reason stated, in ALL THREE functions that carry the shape -- spell_blaze (mage.cpp) and, per the 2026-08-22 note below, spell_haze and spell_poison (mystic.cpp), whose `if (!victim && !obj && !(caster->specials.fighting))` also runs before their own `if (!caster) return;`
- [ ] #3 Boot goldens + seed42 golden byte-identical; ASan clean on the mage and mystic suites
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-08-22 (scope widened, TASK-021 Task 6 finding F1): item 2 above -- "a caster dereference ahead of the function's own `if (!caster)` guard" -- is not unique to spell_blaze. `spell_haze` (mystic.cpp) and `spell_poison` (mystic.cpp) have the identical shape: `if (!victim && !obj && !(caster->specials.fighting))` runs BEFORE their `if (!caster) return;`. TASK-021 left both untouched on purpose (its `caster_snapshot::capture()` was placed AFTER each null test, so no new window opened) and folds them into this task instead of filing a third: the fix is the same decision in all three places -- hoist the guard, or delete the dead null-handling and say which dispatcher could ever pass null. No dispatcher does today (RR Wave R3's dispatch census), so all three are lying guards rather than live crashes. AC#2 now covers `spell_blaze`, `spell_haze` and `spell_poison`, and whichever way it is answered the answer should be the same in all three.
<!-- SECTION:NOTES:END -->
