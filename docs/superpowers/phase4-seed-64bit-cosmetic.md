# Phase 4 seed list: 64-bit hazard audit cosmetic/deferred hits (TOMBSTONE)

This document was Phase 2b Task 8's deferred-items list (2026-07-07). Its tracking
function moved to `backlog/` on 2026-08-18; this tombstone records where each section
went. Full content history: `git log -- docs/superpowers/phase4-seed-64bit-cosmetic.md`.

| Section | Disposition (verified against code, 2026-08-18) |
|---|---|
| Grep (a) `%d` fed a `long` | No actionable hits at audit time; the sprintf sites have since largely converted to `std::format` (Phase 4). Closed. |
| Grep (b) pointer↔int casts | Zero hits at audit time. Closed. |
| Grep (c) Y2038-class `int` timestamp stores (`retiredon`, `kill_time`, `crime_time`, int locals, `ban.cpp` date parse) | Still live (fields re-verified still `int`). Tracked as **backlog TASK-011 — Y2038 timestamp-width audit**. |
| Grep (c) MSSP uptime narrowing + the related absolute-vs-elapsed finding | Adjudicated in code: `src/app/protocol.cpp` (`GetMSSP_Uptime`) now documents that the absolute boot epoch is MSSP-spec-correct and the cast was widened to `%lld`. Closed. |
| Standalone monolithic `ageland_tests` cross-suite state pollution | Superseded: `ensure_test_world_room()` now has a single shared implementation (`src/tests/test_world.h`), the test-tier migration (LS-3a T3) centralized world-state fixtures, and the six-seed shuffle gate has held 0 crashes / 0 failures as a standing per-wave gate since LS-3a batch 0/T1. Per ruling R-D7 the shuffle gate remains a DELTA gate — no clean bill is claimed, and any new monolithic crash is investigated as real (AGENTS.md). |

Work items live in `backlog/` (see AGENTS.md "Task tracking").
