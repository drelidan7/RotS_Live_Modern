---
id: TASK-027
title: 'pkill_valid_killer: NPC arm uses || where && was meant'
status: To Do
assignee: []
created_date: '2026-08-23 11:20'
labels: []
milestone: m-3
dependencies: []
priority: low
ordinal: 27000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
`src/app/pkill.cpp::pkill_valid_killer` reads `if (!MOB_FLAGGED(killer, MOB_ORC_FRIEND) || !MOB_FLAGGED(killer, MOB_PET)) return 0;` — a mob must carry BOTH flags to be a valid pkiller, which no mob does, so the orc-follower branch below it (and its "leader not engaged" rule) is dead. Surely `&&` was meant (either flag qualifies). Found by the TASK-026 implementer; left untouched there because changing it alters live PK records.
<!-- SECTION:DESCRIPTION:END -->

## Why
Source: TASK-026 implementer concern 3 (2026-08-23). Deciding `||` vs `&&` is a gameplay ruling (do orc-friend pets earn PK records for an absent leader?), so it needs the owner and a red-first test, not a drive-by fix.

## Acceptance Criteria
- [ ] Owner rules on the intended semantics.
- [ ] Red-first test pinning the ruled behaviour; pkill records for orc-friend/pet kills verified.
