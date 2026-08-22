---
id: TASK-018
title: >-
  spell_fireball: NPC self-fumble use-after-free (mage.cpp:1877 reads a freed
  caster)
status: Done
assignee: []
created_date: '2026-08-21 17:08'
updated_date: '2026-08-22 16:53'
labels: []
milestone: m-0
dependencies: []
priority: high
ordinal: 18000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Found by RR Wave R3's Task 2p (.superpowers/sdd/2026-08-21-rr3-combat/task-2p-report.md §4A) while classifying `src/combat/mage.cpp · spell_fireball · room_of(`. The orc-fumble arm (mage.cpp:1858-1862) executes `victim = caster;` and then calls `apply_spell_damage` on the caster itself; `damage()` → `die` → `raw_kill` → `extract_char` can relocate (flee) or, for an NPC caster, unlink AND `free_char` it — after which `:1877` reads `room_of(caster)` on freed memory. A live use-after-free, strictly worse than the NOWHERE-resolve the ledger row tracks. The ledger row stays TODO pending this fix.

## Why
Source: RR R3 census A overturn (2026-08-21). The RR program's ledger can only classify the site once the caster's lifetime across the damage call is settled; fixing it (capture the room id before the damage, or re-test the caster after) is a behavior-affecting change that needs its own red-first test, and is out of R3's classification charter (the LS-3a `specialized_mages` follow-up precedent).
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 Red-first test reproduces the NPC self-fumble path against the unfixed code (use ASan to witness the free)
- [x] #2 Fix lands with seed42 characterization + boot goldens byte-identical, or with a documented, regenerated golden if the fix is necessarily observable
- [x] #3 Ledger row src/combat/mage.cpp · spell_fireball · room_of( classified PROVEN or GUARDED and MAXIMUM_TODO_COUNT lowered --check-derived
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-08-22: fixed on fix/task-018-spell-fireball-uaf. AC#1 note — the literal free is NOT exercised by the test: a real extract_char() on the stack fixture would free() non-heap storage, so the test stubs the seam to the real unlink minus the free and witnesses the post-death resolve through the negative-room mudlog (deterministic, red-first); ASan+UBSan ran clean on the suite with the fix. The first two red attempts were fixture gaps in the death pipeline (no mob_index for an NPC caster), closed by a scoped one-entry table.
<!-- SECTION:NOTES:END -->

2026-08-22 (owner review): reordered per David's design note -- the fumble's self-hit now lands LAST, after the splash loop, so every other occupant still resolves (the first cut returned early on the caster's death and dropped the splash). spell_earthquake was checked as precedent: it never damages the caster at all (`tmpch != caster`).
