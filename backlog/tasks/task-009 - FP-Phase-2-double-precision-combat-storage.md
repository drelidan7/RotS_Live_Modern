---
id: TASK-009
title: 'FP Phase 2: double-precision combat storage'
status: To Do
assignee: []
created_date: '2026-08-19 02:07'
labels: []
milestone: m-2
dependencies:
  - TASK-008
priority: low
ordinal: 9000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Store combat stats as doubles instead of ints — a pure storage/schema bump on the already-double math interiors that fp-interiors landed (merged PR #19, master @c793e879, 2026-07-23). The only waiting benefit is fractional carry across ticks/relogs.

## Why
Source: docs/superpowers/specs/2026-07-22-fp-interiors-design.md (Option C shipped 'Phase 3 without Phase 2'; Phase 2 explicitly deferred) and memory double-precision-combat-deferred. BLOCKED on task-008: the deferral condition is all player data being account-native JSON, so the schema bump happens in exactly one format.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Blocked-on gate confirmed: all player persistence is JSON (task-008 done)
- [ ] #2 Storage schema carries doubles; to_game_int boundary sites re-audited; FP-determinism policy (docs/BUILD.md) still holds on all platforms
- [ ] #3 Characterization goldens: intentional drift enumerated and regenerated with the change disclosed, per the standing goldens rule
<!-- AC:END -->
