---
id: TASK-014
title: 'doc_citation_census: advisory gate for doc file references'
status: To Do
assignee: []
created_date: '2026-08-19 03:26'
labels: []
milestone: m-3
dependencies: []
documentation:
  - backlog/docs/doc-audit-2026-08-18.md
priority: low
ordinal: 14000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
A tools/doc_citation_census.py in the house census style: extract src/... path citations from docs/** (and optionally backlog/docs/), verify each cited file exists in the tree, and report misses. Advisory ctest entry (non-blocking, like clang-tidy-advisory) — prose line numbers drift legitimately with every wave, so the gate checks path existence only, not line accuracy. Should have a --self-test like its three siblings.

## Why
Source: the 2026-08-18 documentation audit's root-cause conclusion (backlog/docs/doc-audit-2026-08-18.md): every doc tier with a machine gate stayed true through three restructurings; every tier without one rotted. This extends the repository's proven census pattern to the citation layer — it would have caught essentially the entire audit's findings the day each drift landed. Suggested during the audit review; owner deferred skills/agents work but approved capturing the doc-refresh program.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Checker extracts and verifies src/ path citations across docs/**; misses reported with doc file:line
- [ ] #2 Wired as an advisory (non-blocking) ctest entry plus a flat-Makefile hook, matching the sibling censuses' dual wiring
- [ ] #3 --self-test with fixture-driven directions, including a moved-file miss and a flat-header pass
<!-- AC:END -->
