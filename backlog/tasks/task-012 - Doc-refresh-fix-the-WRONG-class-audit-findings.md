---
id: TASK-012
title: 'Doc refresh: fix the WRONG-class audit findings'
status: To Do
assignee: []
created_date: '2026-08-19 03:26'
labels: []
milestone: m-3
dependencies: []
documentation:
  - backlog/docs/doc-audit-2026-08-18.md
priority: medium
ordinal: 12000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Fix every substantively-wrong documentation claim from the 2026-08-18 three-agent audit (full findings: backlog/docs/doc-audit-2026-08-18.md). The big items: docs/systems/random-number-generation.md describes rand()/srand() as live when the mt19937/rots_rng migration shipped (worst single doc — rewrite); AGENTS.md's Dead/Unused Code section cites pre-layout paths and a fight.cpp:2755-2761 round-loop range that now lands inside hit()'s body; five docs describe combat_manager as compiled-but-dormant when it was deleted; ownership-map.md cites db.cpp/structs.h (nonexistent) with no staleness disclaimer; warrior-equipment-bis.md's source-of-truth block is wrong on all five combat-math homes; data-formats docs cite dead Crash_* writers and db.cpp function homes; wizset/shape docs cite structs.h; location-read-allowlist.md:47 says EIGHT-token (real: eleven). NOT in scope: the mechanical path-prefix sweep (task-013). Already fixed at audit time: README links/CI section, the repo formatter hook.

## Why
Source: the 2026-08-18 documentation audit (backlog/docs/doc-audit-2026-08-18.md), owner-requested holistic review. These are the findings where a reader following the doc would be actively misled — wrong files, deleted code described as live, or policy contradictions (the RNG doc contradicts AGENTS.md's own rots_rng rule). The operational tier stayed true because gates check it; this is the ungated tier's accumulated debt from the db.cpp/structs.h splits, per-wave relocations, and the physical-layout wave.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 docs/systems/random-number-generation.md rewritten to describe the shipped rots_rng reality
- [ ] #2 AGENTS.md Dead/Unused Code section: paths and the round-loop citation corrected to current src/<lib>/ homes
- [ ] #3 All five combat_manager compiled-but-dormant descriptions corrected to deleted (docs/README.md, combat-loop, specializations, weapons, magic-system)
- [ ] #4 ownership-map.md either re-cited to current homes or given an explicit staleness banner scoping it to its 2026-07-13 snapshot
- [ ] #5 warrior-equipment-bis.md source block, data-formats function homes, wizset/shape structs.h refs, and the allowlist EIGHT-token line corrected per the findings doc
<!-- AC:END -->
