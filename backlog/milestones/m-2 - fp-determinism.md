---
id: m-2
title: "FP Determinism"
---

## Description

Cross-platform bit-identical combat math, finished end to end: the deterministic FP regime
and double math interiors are live; what remains is Phase 2 — double *storage* — and its
named prerequisite, unifying player persistence on account-native JSON. Full narrative:
`backlog/docs/arc-fp-determinism.md`.

**What it buys:** combat outcomes independent of host platform (already delivered by
Phase 1 + fp-interiors), plus — once Phase 2 lands — fractional stat carry across ticks and
relogins, so slow-accruing bonuses stop being rounded away at every boundary. The
persistence-unification prerequisite has standalone value: one save path and one schema
instead of today's JSON/text split.

**Provenance:** owner-requested (David / drelidan7) — the original ask was double-precision
combat math; the owner reviewed and fast-forward merged fp-interiors as PR #19 (master
@`c793e879`, 2026-07-23) with Phase 2 explicitly deferred "until all player data is
account-native JSON" (spec `docs/superpowers/specs/2026-07-22-fp-interiors-design.md`
As-built section; project memory `double-precision-combat-deferred`).
