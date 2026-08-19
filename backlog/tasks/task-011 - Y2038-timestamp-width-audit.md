---
id: TASK-011
title: Y2038 timestamp-width audit
status: To Do
assignee: []
created_date: '2026-08-19 02:30'
labels: []
milestone: m-3
dependencies: []
documentation:
  - docs/superpowers/phase4-seed-64bit-cosmetic.md
priority: low
ordinal: 11000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Widen the int-typed epoch-seconds stores before they overflow in 2038: char_special2_data::retiredon (src/core/include/rots/core/types.h:684), pkill kill_time (src/pkill.h:17, carries an in-code XXX), crime_record::crime_time (src/db.h:222), plus the int locals fed from time(0) (historically limits.cpp/db.cpp — re-locate after the physical-layout moves) and ban.cpp's %d date parse. The original 'frozen on-disk binary layout' blocker has softened since the audit was written: live saves are JSON (Phase 2a), so widening is a JSON schema question; only the one-time legacy migration decoders keep the frozen int layout, by design.

## Why
Source: docs/superpowers/phase4-seed-64bit-cosmetic.md (Phase 2b Task 8's bounded 64-bit hazard audit, 2026-07-07 — tombstoned into this task 2026-08-18), grep (c): every listed store is a 'Y2038-class' hazard, deliberately deferred as unreachable at current runtime with the instruction 'Revisit for real ahead of 2038, or as part of a deliberate on-disk format version bump.' Field types re-verified still-int on 2026-08-18. The doc's other items are closed: greps (a)/(b) found nothing actionable; the MSSP uptime finding was adjudicated in code (protocol.cpp:2552 comment — absolute epoch is spec-correct, cast widened to %lld); the monolithic cross-suite pollution class was superseded by the test-tier migration (single shared test_world.h) and LS-3a's standing six-seed shuffle gate.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Every epoch-seconds store audited tree-wide (re-grep, not just the 2026 list) and widened to time_t/int64_t or explicitly ruled a non-store
- [ ] #2 JSON schemas carry the widened values; legacy one-time migration decoders keep their frozen int layouts untouched
- [ ] #3 Standing gates green both hosts incl. the 32-bit i386 battery (time_t width differs there — the i386 ABI constraint is part of the design, not an afterthought)
<!-- AC:END -->
