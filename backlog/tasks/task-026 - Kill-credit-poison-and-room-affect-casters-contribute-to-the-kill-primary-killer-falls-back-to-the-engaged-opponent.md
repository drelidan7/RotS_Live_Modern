---
id: TASK-026
title: >-
  Kill credit: poison and room-affect casters contribute to the kill; primary
  killer falls back to the engaged opponent
status: In Progress
assignee: []
created_date: '2026-08-23 10:05'
labels: []
milestone: m-3
dependencies: [TASK-021]
priority: medium
ordinal: 26000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Rider on TASK-021's branch (`fix/task-020-021-room-affects`), owner-requested 2026-08-23. Two
defects in the kill-credit model as TASK-021 left it: (1) a poison/room-tick death with NO resolvable
caster reaches `die(victim, NULL)` even when the victim is actively fighting, which skips
`pkill_create` (the engaged player loses the PK record the pre-branch path gave them through
`pkill_opponents()`'s combat_list walk) and flips the victim onto the harsher non-PK death penalty;
(2) the PK record is built solely from `combat_list`, so a poisoner or an out-of-room caster whose
tick kills never appears in it, and a lone poisoner's kill records zero opponents.

Design (approved in chat): primary killer = recorded caster, else `victim->specials.fighting`, else
nobody (death TYPE axis); contributor set = everyone fighting the victim ∪ `resolve_poisoner(victim)`
(participation, even when someone else lands the kill) ∪ the killing tick's caster (room affects:
tick-kill only, no participation memory — owner ruling), deduplicated, pet→master redirected, victim
and immortals excluded; `pkill_create` takes that set and pkill.cpp's three `combat_list` walks
iterate it; `die()`'s poison early-out becomes "contributor set empty → no record".
<!-- SECTION:DESCRIPTION:END -->

## Why
Owner, 2026-08-23, after asking who gets credit for a casterless poison death mid-fight (answer:
nobody, and the victim is penalised as a non-PK death) and how mob-vs-player death is decided
(last hit for the type; everyone engaged for the PK record). The six owner-stated cases are the
acceptance criteria. Inventory item 10 on TASK-021's FLAGGED BEHAVIOR-CHANGE INVENTORY.

## Acceptance Criteria
- [ ] Player poison kills a non-fighting victim → player death, poisoner gets the PK record.
- [ ] Mob poison kills a non-fighting victim → mob death (EXPLOIT_MOBDEATH, stat penalty).
- [ ] Player poison/tick kills a victim fighting other players, caster out of room → player death; caster AND the engaged players in the record.
- [ ] Mob poison kills a victim fighting players → mob death; engaged players still get the record.
- [ ] Poisoned by player A, killed by player B's blow → A in the record as a contributor.
- [ ] Sourceless poison kills an engaged victim → type and credit follow `specials.fighting`.
- [ ] Sourceless poison, not fighting → unchanged (nobody arm, no record).
- [ ] Red-first tests for each; ASan clean; all standing gates; smoke-account, rots64, i386 battery at finalization.
