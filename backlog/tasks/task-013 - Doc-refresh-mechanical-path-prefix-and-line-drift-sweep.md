---
id: TASK-013
title: 'Doc refresh: mechanical path-prefix and line-drift sweep'
status: To Do
assignee: []
created_date: '2026-08-19 03:26'
labels: []
milestone: m-3
dependencies: []
documentation:
  - backlog/docs/doc-audit-2026-08-18.md
priority: low
ordinal: 13000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Sweep every remaining bare src/foo.cpp citation in docs/** to its post-physical-layout src/<lib>/foo.cpp home, and refresh drifted line numbers where cited (security-notes.md's interpre.cpp citations drifted -135..-194; fight.cpp/db_players.cpp/db_world.cpp citations drifted 60-200+ lines). Flat shared headers (utils.h, db.h, pkill.h, protos.h, interpre.h) are correctly unprefixed — leave them. The STALE class in backlog/docs/doc-audit-2026-08-18.md enumerates the files; substance was verified correct in the audit, so this is citation repair, not content review.

## Why
Source: the 2026-08-18 documentation audit (backlog/docs/doc-audit-2026-08-18.md), STALE class. Split from task-012 because it is mechanical and an order of magnitude larger (every docs/systems and docs/data-formats file); doing it separately keeps the substantive fixes reviewable. Consider doing it after (or together with) task-014's citation checker so the sweep ends with a green gate proving completeness.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Zero bare pre-layout src/*.cpp citations remain in docs/** (grep-verified; flat shared headers exempt)
- [ ] #2 Line-number citations in security-notes.md and data-formats refreshed against current code
- [ ] #3 If task-014 has landed, its checker passes clean over docs/**
<!-- AC:END -->
