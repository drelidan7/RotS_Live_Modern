---
id: TASK-028
title: >-
  Self-damage poison sites credit the victim as killer, diverging from the
  tick paths' null-credit fallback
status: To Do
assignee: []
created_date: '2026-08-23 12:10'
labels: []
milestone: m-3
dependencies: [TASK-026]
priority: low
ordinal: 28000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Three equip/unequip poison sites (`src/combat/fight.cpp` `poison_removal_hook_impl` ~2318 and the two
`damage(character, character, 5, SPELL_POISON, 0)` siblings ~3350/~3416) go through `damage()`'s
forwarder, which credits `attacker ? attacker : victim`, so `killer == victim` and a PC victim takes
`raw_kill`'s lenient `died_to_player` hp/4 arm. The tick paths (`limits.cpp:765`/`:1397`,
`room_affect_tick.cpp:122`) pass a real `nullptr` credit, which TASK-026 now resolves to the engaged
opponent (or nobody), so the same death takes the harsh stat-penalty arm whenever that opponent is a
mob. Contributors are consistent (victim excluded either way); only the death-TYPE arm diverges.
Paired quirk to fix at the same time, both call sites together: the pet→master redirect in
`kill_contributors` (`fight.cpp` ~1013) and `damage_credited` (~2280) compares
`location_of(master) == location_of(pet)`, which is also true when both are NOWHERE, and reads
`candidate->master` without registry validation (faithful to the pre-existing idiom).
<!-- SECTION:DESCRIPTION:END -->

## Why
Source: TASK-026 task review, OBSERVATION-8 and MINOR-6/7 (2026-08-23). The divergence predates
TASK-026 (it arrived with TASK-021's null credit) and TASK-026 narrows it; closing it means ruling
whether self-inflicted poison deaths should resolve through the same fallback (`resolve_poisoner` →
engaged opponent → nobody) as the ticks. Gameplay ruling needed; red-first test; both goldens.

## Acceptance Criteria
- [ ] Owner rules on the self-poison death arm.
- [ ] The three self-damage sites and the tick sites resolve credit identically; test per shape.
- [ ] Pet redirect NOWHERE guard fixed at both call sites together, or explicitly ruled a non-issue.
