---
id: TASK-008
title: Unify player persistence on account-native JSON
status: To Do
assignee: []
created_date: '2026-08-19 02:07'
labels: []
milestone: m-2
dependencies: []
documentation:
  - docs/superpowers/specs/2026-07-22-fp-interiors-design.md
priority: low
ordinal: 8000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Retire save_player's line-oriented text path so ALL characters persist through account-native JSON (account::write_account_character_file). Today two live paths exist (AGENTS.md 'Runtime Data and Persistence'): account-native characters use JSON; non-account characters still use the text format — current behavior, not a migration decoder.

## Why
Source: the FP program's Phase 2 gate (memory double-precision-combat-deferred, 2026-07-23: 'Phase 2 (double STORAGE) stays deferred until all player data is account-native JSON'; spec docs/superpowers/specs/2026-07-22-fp-interiors-design.md). This unification is the named prerequisite — and has standalone value (one save path, one schema). Owner decision needed on forced account-linking or a JSON path for unlinked characters.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Owner ruling on the unlinked-character strategy (force-link, parallel JSON path, or migration-on-login)
- [ ] #2 All character saves flow through JSON; the text path becomes a read-only migration decoder or is retired outright
- [ ] #3 make smoke-account passes (mandatory for any account/login change) plus the standing gates; migration is lazy-safe like the exploit converter
<!-- AC:END -->
