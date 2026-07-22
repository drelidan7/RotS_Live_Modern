# Spec-Pair Wave (`spec_pro.cpp` → `spec_ass.cpp` — the combat row's last two TUs) — Design

**Date:** 2026-07-21 · **Branch:** `arch/spec-pair`, off master @`01d1dc8` (spell-family closure
merged by the owner). · **Program context:** **Wave B of the owner-approved combat-row completion
program**, executed under the owner's explicit 2026-07-21 grants: **autonomous-spec-gate** (this
spec is written against Wave A's as-built per the charter in
`docs/superpowers/specs/2026-07-21-spell-family-closure-design.md` §Wave B, with no owner review
gate) and **merge-when-green**. When both TUs land, the combat DEFER list reaches **0** and the
combat row is DONE.

## Problem / evidence

The last two DEFER TUs are a gated pair: `spec_ass.cpp`'s combat-peer=39 is dominated by
`spec_pro.cpp` references, so spec_pro must resolve first (or jointly). Both playbook rows
predate five merged waves; the T0 census re-derives everything. Known structure:

- **`spec_pro.cpp`** (combat-peer=17 pre-Wave-A — shrinks now that `spell_pa`/`mage`/`ranger`
  are in-lib): the big command-driver spec-proc TU. Its former "no seam" blockers are largely
  gone: `command_interpreter` has a registered `script_hooks.h` hook (l4-seed);
  `find_first_step` is a library symbol (`rots_pathfind`); `add_exploit_record` has the persist
  dispatch; `waiting_list` lives in `clerics.cpp` (in-lib for L3, downward for L4). Still open:
  `_cmd_info` (interpreter table — no seam; disposition needed), the ~dozen `do_*` up-calls
  re-verified against the 29-cell table (spec_pro is exactly the TU class that can surface a
  call site outside the table — new cells only on confirmed real shapes), `_buf` retirement.
- **`spec_ass.cpp`**: the spec-proc ASSIGNMENT registrar. It already self-registers
  `virt_program_number` and `virt_assignmob` into `script_hooks.h` (rider cells 1 and 2) — those
  registrations' legality depends on the pair's landing tier (see below). Its three **registrar
  edges** — `gen_board` (boards.cpp), `postmaster` (mail.cpp), `receptionist` (objsave.cpp) —
  are fn-ptr references INTO app-tier subsystems, the **pre-authorized new seam family**: a
  lookup/registry hook where each owning app TU registers its spec-proc fn-ptr at boot and
  spec_ass resolves by key. Pre-authorization covers EXACTLY this 3-edge shape; a 4th registrar
  edge or a different coupling class is an auto-STOP to the owner.
- **The tier question (T0 adjudicates, the mobact precedent — decided at promotion with fresh
  closure evidence, not inherited):** `rots_combat` (L3) vs the L4 band (`rots_script`).
  Evidence cuts both ways and the census settles it: the `virt_*` dispatch cells live in
  `script_hooks.h`/`rots_script`, and `spec_ass`'s SETTER calls into them are downward only from
  L4-or-above (from L3 they are upward calls needing inversion); `find_first_step` is downward
  from either tier; mob-AI (`mobact.cpp`, `rots_script`) is the primary dispatcher of the
  `virt_*` cells, and "the driver homes with the engine it invokes" placed mobact in
  `rots_script`. The spec-procs are invoked THROUGH those L4-owned dispatch cells, which leans
  L4 — but the census's `nm` closure is the ground truth, per the playbook. Whatever tier wins,
  the no-bidirectional-links invariant and the existing certified order are unchanged; NO new
  library is anticipated (membership goes to an existing target; new linkcheck only if a new
  library were forced, which would be an auto-STOP for owner review since the charter doesn't
  cover one).
- **Pair structure:** joint commit ONLY if the census finds a genuine spec_pro↔spec_ass cycle;
  otherwise spec_pro promotes first (its own commit), then spec_ass (the trio-wave standalone
  precedent). The rider gate has ONE slot left (2 of ≤3); a new same-shape `void*` dispatcher
  edge may consume it with its own discriminator pair; a second, or any different shape, is an
  auto-STOP.

## Decision (charter-derived, autonomous)

Playbook task shape: **T0** census (read-only; the tier adjudication + registrar-family design +
pair-structure ruling) → **T1** seams consumer-free (the 3-edge registrar family + whatever T0
surfaces; every new test file flat-paired SAME COMMIT — the BINDING owner rule carries forward)
→ **T2** `spec_pro.cpp` conversions → **T3** `spec_ass.cpp` conversions → **T4** membership
commit(s) per the pair ruling → **T5a** docs + the Wave-A hygiene batch (combat_hooks.h:130
stale dismount comment; color.cpp:33 + visibility.cpp:1187 stale reader-scan snapshots; dead
decls spell_pa.cpp:34/:55 + ranger.cpp:54; the Wave-A i386 battery fold-in + the census
86-vs-76 clarifying parenthetical) → **T5b** finalization (battery → PR → the pre-authorized
merge). **Mandatory brief lesson (Wave A's `is_target_valid` near-miss): a symbol whose
promotion commit IS the membership commit can never have its conversion "deferred to the
membership task" — every upward edge converts in T2/T3.**

## Verification

Per-task: macOS arm64 + `rots64` builds, full ctest, characterization + boot goldens both hosts,
`string_view_census --check`, ASan on any test-file change; flat-Makefile parity BINDING and
same-commit. Finalization: sequential i386 battery, monolithic reconciliation (baseline 1487 +
wave delta; ctest-only linkcheck delta stays 9 unless a linkcheck is added, which the charter
does not anticipate), six blocking CI jobs, then the pre-authorized ff-merge. Spec-proc paths
are boot-golden-light (assignment runs at boot, but proc BODIES fire in play): the coverage-gap
rule is expected to fire for the registrar family and any relocated proc helpers.

## Risks

- **The registrar family is a genuinely new seam taxonomy** — pre-authorized at exactly 3 edges;
  its design (key scheme, default behavior on unregistered lookup — loud tripwire, never a
  silent null spec-proc) lands consumer-free in T1 with discriminator pairs per edge.
- **The tier ruling could surface a split** (spec_pro one tier, spec_ass another) — legal only
  if every cross-edge runs downward after both land; the census must check both orderings; an
  irreducible bidirectional pair across tiers is an auto-STOP (charter covers same-tier and
  spec_pro-below-spec_ass outcomes only).
- **`_cmd_info`** has no seam and no pre-authorized taxonomy; if the census cannot disposition
  it as RELOCATE/accessor/existing-hook, that is an auto-STOP (anticipated fallback: the
  `command_interpreter` hook's existing shape covers the dispatch use; a table READ may need a
  small accessor — known shapes).
- DEFER 0 tempts scope creep (e.g., promoting big_brother or boards while "we're here") — out
  of scope; the wave ends with exactly the two TUs landed.

## Out of scope

Any TU beyond `spec_pro.cpp`/`spec_ass.cpp`; promoting boards/mail/objsave (they stay app-tier
registrants); behavior changes (byte-verbatim + seam conversions only); Stage 2 LocationSystem;
rots_commands census; int→double.

## Process

Subagent-driven: Sonnet implementers, Opus census/heavy reviews, Fable whole-branch. Briefs/
reports/census in `.superpowers/sdd/` (`sp-` prefix, never committed). Python byte-edits for all
existing `.cpp`/`.h`. Docker gates synchronous. i386 battery finalization-only. The owner's
program grants cover this wave's spec/plan authorship and the CI-green merge; every auto-STOP
named above still halts for the owner.
