---
id: TASK-026
title: >-
  Kill credit: poison and room-affect casters contribute to the kill; primary
  killer falls back to the engaged opponent
status: Done
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
- [x] Player poison kills a non-fighting victim → player death, poisoner gets the PK record. — `DieContributorRecord.PlayerPoisonOnANonFightingVictimRecordsThePoisoner`
- [x] Mob poison kills a non-fighting victim → mob death (EXPLOIT_MOBDEATH, stat penalty). — `DieContributorRecord.MobPoisonOnANonFightingVictimIsAMobDeath`, which after fix round 1 also pins the real XP delta (15000 − 100 − 1000): the retired early-out returned before die()'s mob-death arm, so this class of death now loses ten times the experience it did.
- [x] Player poison/tick kills a victim fighting other players, caster out of room → player death; caster AND the engaged players in the record. — `DieContributorRecord.ARemoteCastersTickRecordsBothTheCasterAndTheEngagedPlayers`
- [x] Mob poison kills a victim fighting players → mob death; engaged players still get the record. — `DieContributorRecord.MobPoisonOnAnEngagedVictimStillRecordsTheEngagedPlayers`
- [x] Poisoned by player A, killed by player B's blow → A in the record as a contributor. — `DieContributorRecord.APoisonerContributesEvenWhenSomebodyElseLandsTheBlow`
- [x] Sourceless poison kills an engaged victim → type and credit follow `specials.fighting`. — `SourcelessKillCredit.FallsBackToTheEngagedPlayerOpponent` / `…FallsBackToTheEngagedMobOpponent`, plus `…TheEngagedOpponentCollectsTheKillsExperience` (fix round 1) for the credit being a real killer, not a label
- [x] Sourceless poison, not fighting → unchanged (nobody arm, no record). — `SourcelessKillCredit.CreditsNobodyWhenTheVictimIsNotFightingAnybody` (positive control, passes before and after) and `DieContributorRecord.NobodyTookPartSoNoRecordIsCreated`
- [x] Red-first tests for each; ASan clean; all standing gates; smoke-account, rots64, i386 battery at finalization. — red-first where the change was observable against the old body (2 of the SourcelessKillCredit trio, 2 of the six DieContributorRecord cases); the rest are new-API/TDD tests proven non-vacuous by 16 sabotages, each RED-naming its own test. macOS ctest 1959 → 1981, ASan 1981/1981, native boot golden matches, seed42 golden unchanged, all three censuses exit 0 at every commit. **rots64, `make smoke-account` and the i386 battery are the controller's finalization legs and are NOT run here.**

## Implementation notes

Five commits on `fix/task-020-021-room-affects`: the engaged-opponent fallback in
`damage_credited()`; `rots::combat::kill_contributor_list` / `kill_contributors()`
(`combat_hooks.h` + `fight.cpp`); the `pkill_create` seam widened to carry the set, with
pkill.cpp's three walks iterating it; `die()`'s early-out re-based on "did anybody take part";
docs. Full account: `.superpowers/sdd/2026-08-22-room-affect-caster-snapshot/task-8-report.md`
and docs/BUILD.md's FLAGGED BEHAVIOR-CHANGE INVENTORY item 10.

Fix round 1 (`99f68ea0` tests, plus a docs commit) closed both reviews' APPROVED-with-disclosure
rulings without changing shipped logic: three tests pinning the accepted consequences (immortals
out of `pkill_weight`'s denominator, the engaged opponent collecting the kill's experience, an
immortal's kill writing no record), the AC#2 XP-delta assertion, and the one-way-engagement
comment. 1981 → 1984.
