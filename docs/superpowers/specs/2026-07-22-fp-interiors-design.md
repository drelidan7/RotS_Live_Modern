# Combat-Math Double Interiors ("Phase 3 without Phase 2") — Design

**Date:** 2026-07-22 · **Branch:** `feat/fp-interiors`, off master @`6aed40c` (combat row closed).
· **Predecessors:** the Phase 1 FP unification (`2026-07-15-phase1-fp-unification-design.md` —
SSE on every build incl. shipping i386, `-ffp-contract=off`, `fp_policy.h`, the cross-matrix
golden proof) and the Option C plan recorded in the double-precision deferral decision
(2026-07-15). · **Owner decisions (2026-07-22):** core-combat scope; uniform `std::lround`
boundary policy with NO exception table; paired-reference verification; **merge is the owner's —
no merge-when-green grant for this wave** (a gameplay-affecting change; the PR presents the
transcript diff for owner review).

## Problem / decision

Option C's Phase 2 (double-precision STORAGE) is blocked until all player data is
account-native JSON — `char_ability_data`/`char_point_data` are shared between live `char_data`
and on-disk `char_file_u`, so a stored-type change drags a three-path persistence migration.
**This wave decouples the math from the storage:** every field, struct, and storage path stays
`int`; the *interiors* of the core-combat formula chains compute in `double`; each result lands
back in its int destination through exactly ONE `std::lround` at the application boundary.
Per-step integer truncation (the source of the known "~1 HP" max-HP artifact and the broader
downward bias) is replaced by full-precision evaluation with a single unbiased rounding.
Outcomes intentionally change — this is the deliberate balance improvement Phase 3 always was,
delivered early and "close enough" until the serialization pass lands. Phase 2 later becomes a
pure storage/schema bump on top of already-double math (fractional carry across ticks/relogs is
the only benefit that waits for it); nothing in this wave is throwaway.

**Uniform rounding policy (owner-set):** `std::lround` at every boundary, no exceptions. Because
storage stays int, display/cost/death logic only ever receives ints — the old Phase 2 exception
table (display HP up, mana/move down; costs up) has nothing to apply to. Death stays `<= 0` on
int HP, unchanged.

## Scope (owner-set: core combat)

Four formula families convert; the T0 census enumerates their exact functions, call graphs, and
every boundary site:

1. **Max-HP/vitals recalc** — the `profs.cpp` recalc chain (the known ~1 HP truncation artifact
   named in the Option C record).
2. **The OB/PB/DB rating trio** — `get_real_OB`/`get_real_parry` (`visibility.cpp`, L3) and
   `get_real_dodge` (`char_utils_combat.cpp`, L2), plus the contributing sub-chains the census
   maps (spec-handler adjustments, stat/encumbrance terms).
3. **The `fight.cpp::hit()` damage formula** — the LIVE formula (strength folded *inside* the
   random factor). Per the repository's standing dead-code warnings, the deleted
   `combat_manager` variant is explicitly NOT a reference for any of this work.
4. **The speed/energy-regen chain** (owner addition, 2026-07-22 — combat pacing, the same tier
   as the rating trio): `utils::get_energy_regen` (`char_utils_combat.cpp:165` ff), whose
   `str_speed`/`null_speed` harmonic-mean combine stacks integer divisions
   (`2*20*2500000 / (weight * (bulk+3))`, then `1000000 / (1000000/str_speed +
   1000000/null_speed²)`) — per-step truncation compounding at its worst in-scope; boundary at
   the existing int return, consumed by the `fight.cpp:2938` pulse loop. The census maps any
   sibling speed helpers in the same chain.

**Out of scope:** saves/skill-checks/spell-power (threshold comparisons where truncation rarely
flips outcomes — a possible follow-on with live-play evidence); ALL storage/struct/persistence
types; display formatting; any formula outside the four families; behavior changes beyond the
precision change itself.

**Determinism constraint (hard):** the FP-determinism policy bans transcendentals, x87, and
`long double` in the combat path. T0 must prove the converted chains use only `+ - * /` and
comparisons; a formula that genuinely needs a transcendental is a STOP to the owner. All
randomness stays `rots_rng`; integer draws convert to double AFTER drawing, so the RNG stream
(same seed → same draw sequence) is unchanged.

## Mechanics

- `fp_policy.h` gains ONE named boundary helper — `rots::fp::to_game_int(double)` (implemented
  as `std::lround`, with the policy comment) — and every boundary site calls it; no bare
  `lround`/casts at call sites, so the policy greps cleanly and future audits are mechanical.
- Conversions are in-place formula rewrites: int locals/intermediates → `double`, integer
  literals → double literals where they participate in the chain, the final assignment through
  the helper. Function signatures that RETURN ratings (e.g. `get_real_OB`) keep their int return
  type at the public boundary (callers are int-world) — the census rules per function whether
  the double chain ends inside the function (int return = the boundary) or a double-returning
  internal variant is worth it; default is the simplest: boundary at the existing signature.
- Layer discipline unchanged: the four families live in `rots_combat`/`rots_entity`/app files
  already converted by prior waves; no membership, seam, or linkcheck changes are expected. Any
  surfaced coupling follows the standing playbook rules.

## Verification (owner-set: paired reference + regen)

- **Paired int references:** each converted formula's OLD integer implementation is preserved
  TEST-ONLY (`*_int_reference`, in the test tree — never linked into the game) beside paired
  tests asserting the double result stays within a derived per-formula bound of the reference
  (T0 derives each bound from the formula's truncation-count; a transcription error blows the
  bound), plus exact-value tests at hand-computed points for the new math.
- **External oracle:** where formulas correspond, cross-check against the sibling C# depot's
  `RotS.Rules.Faithful`/`RotS.Rules.Modern` pair (documented in the Option C record).
- **Goldens:** the combat characterization goldens regenerate ONCE, intentionally, via
  `UPDATE_GOLDENS`, with the change declared in the commit message per the standing rule. The
  32-bit `legacy_*_fixture.bin` goldens are untouched (no persistence change). **Boot goldens
  are expected UNCHANGED** (no combat at boot) — boot-golden drift = a bug in the change.
- **Transcript diff:** a same-seed old-vs-new combat-smoke transcript diff
  (`ROTS_RNG_SEED`/`tools/combat_smoke.py`, capture rung) is produced at finalization and
  attached to the PR — informational, reviewed by the whole-branch reviewer AND the owner; the
  locked pre-wave baseline is the reference the harness was built to serve.
- Standard cadence: per-task dual-host gates + ASan on new/changed test files + flat-Makefile
  parity (binding, same-commit) + the finalization i386 battery (the Phase 1 determinism work is
  what makes the i386 leg meaningful for double math) + all six blocking CI jobs.

## Task shape

T0 formula census (read-only: sites, call graphs, boundary inventory, per-formula delta bounds,
the no-transcendentals proof) → T1 `to_game_int` helper + paired-reference scaffolding
(consumer-free; the int references extracted byte-verbatim into the test tree while the live
code is untouched) → T2 conversions per family (task split per census cost; each family's
paired tests go green in the same task) → T3 the golden regen + transcript-diff production and
review → T4 docs (BUILD.md FP section, the Option C record's status, AGENTS.md test chain) →
T5 finalization (battery → PR **presenting the transcript diff; owner merges**).

## Risks

- **A mis-transcribed formula is the top risk** — mitigated structurally by the paired bounds,
  the exact-value points, and the external oracle; residually by the transcript-diff review.
- **Threshold interactions:** even in-scope chains feed int comparisons downstream (e.g. a
  rating compared to a roll). The single-rounding change CAN flip marginal outcomes — that is
  the intended balance change, but T0 flags any downstream comparison whose semantics look
  asymmetric (e.g. `>` vs `>=` at exact ties) for explicit review rather than silent inheritance.
- **Golden-regen discipline:** exactly one regen commit, late (T3), so every prior task's gates
  run against unchanged goldens and every subsequent task would catch accidental second drift.
- **i386/qemu FP:** Phase 1 proved SSE determinism cross-platform; the battery re-proves it
  against the regenerated goldens — a mismatch there is a real finding, never tolerable.

## Process

Subagent-driven: Sonnet implementers, Opus census/heavy reviews, Fable whole-branch (the
whole-branch review explicitly reviews the TRANSCRIPT DIFF, not just code). Briefs/reports/
census in `.superpowers/sdd/` (`fpi-` prefix, never committed). Python byte-edits for all
existing `.cpp`/`.h`. Docker synchronous in subagents; battery finalization-only. **No merge
authority: the wave ends at a CI-green PR + transcript diff for the owner's own review and
merge.**
