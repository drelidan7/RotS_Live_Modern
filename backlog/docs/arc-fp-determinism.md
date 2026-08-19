# Arc: FP Determinism

## Intent
Cross-platform bit-identical combat math. Phase 1 unified the FP regime (SSE, no x87/fast-math);
fp-interiors converted the four core combat formula families to double interiors behind a single
`rots::fp::to_game_int` boundary while keeping int storage. The arc's remaining ambition is
Phase 2 — double *storage* — which waits on all player data being account-native JSON so the
schema bump happens in exactly one format.

## Stories
- As a player, I want combat math to come out identical on every server platform, so that game
  balance does not depend on which host the game runs on.
- As the game's owner, I want fractional stat gains to carry across ticks and relogins (Phase 2),
  so that slow-accruing bonuses stop being rounded away.

## Specs & decisions
- docs/superpowers/specs/2026-07-15-phase1-fp-unification-design.md — FP regime unification.
- docs/superpowers/specs/2026-07-22-fp-interiors-design.md — double interiors, int storage
  (Option C, "Phase 3 without Phase 2"); As-built section records the shipped shape.
- docs/BUILD.md "FP determinism" — the standing policy (to_game_int, grep-clean, sqrt-in-policy).

## Tasks
- TASK-008 — Unify player persistence on account-native JSON (the Phase 2 gate; standalone value)
- TASK-009 — FP Phase 2: double-precision combat storage (blocked on TASK-008)

## How it unfolded
- 2026-07-15: Phase 1 FP unification landed (deterministic SSE regime pinned in build flags and
  docs/BUILD.md).
- 2026-07-23: fp-interiors merged (PR #19, master @`c793e879`) — recalc_abilities, the OB/PB/DB
  trio, hit()+natural_attack_dam, and get_weapon_damage converted to double interiors with one
  lround boundary per site; seed42 golden byte-identical; expected drift was a single repointed
  assertion. Phase 2 storage explicitly deferred on the all-JSON gate.
