---
id: m-0
title: "Room-Resolve Retirement"
---

## Description

Prove every room id reaching `room_data::operator[]` in-range before dereference, then retire
the operator's silent room-0 fallback to a hard abort. Full narrative: `backlog/docs/arc-room-resolve-retirement.md`.

**What it buys:** the fallback has masked bad-room defects for decades — a bad id quietly
teleports players/objects to room 0 instead of failing. Finishing this converts every such
defect into a proof, a guard, or a loud crash at the source, and the census ratchet
(`tools/room_resolve_census.py`, `MAXIMUM_TODO_COUNT`) makes regression a build failure. R2
already cashed out three real `reset_zone` bugs this way.

**Provenance:** owner-approved program (David / drelidan7). It is the first of the two
follow-on campaigns identified at the LocationSystem program's close-out (2026-07-30); the
owner ruled it a proof-driven burndown-to-abort — recorded in
`docs/superpowers/specs/2026-07-31-room-resolve-retirement-design.md` (its owner-ruling
section; pending ruling RR-O-1 §2a blocks only the final flip wave). Waves R1/R2 merged
2026-08-01 (PRs #30/#31).
