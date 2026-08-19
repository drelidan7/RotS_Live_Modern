---
id: m-1
title: "LocationSystem"
---

## Description

Location as a real abstraction: reads and writes behind the Placement API, representation
swapped to private handles, enforced by the fail-closed `LocationReadCensus` gate. The
char-side program is COMPLETE (LS-1..LS-3b + follow-up, PRs #21-#29, 2026-07-24..30); the
milestone stays open only for LS-4, the optional object-side (`obj_data::in_room`) symmetric
campaign. Full narrative: `backlog/docs/arc-locationsystem.md`.

**What it buys:** location semantics (light, occupant chains, zone power, the rnum/vnum
distinction) live in one place instead of ~1200 raw call sites, and the completed program
already paid for itself in real fixed defects (rnum-persisting saves, load-room riders,
follower placement). LS-4 would extend the same guarantee to objects, whose `in_room` is
still a raw public int.

**Provenance:** owner-directed program (David / drelidan7) — spec
`docs/superpowers/specs/2026-07-23-locationsystem-program-design.md`, executed under the
owner's 2026-07-24 autonomous grants (auto-merge on green CI, dual adversarial reviews;
recorded in project memory `locationsystem-autonomous-grants`). LS-4 was suggested at the
program close-out as one of two optional follow-on campaigns and awaits the owner's
go/no-go (that decision is AC #1 of TASK-007).
