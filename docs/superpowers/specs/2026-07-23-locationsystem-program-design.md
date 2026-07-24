# LocationSystem Program ("Stage 1 campaign + Stage 2 swap") — Design

**Date:** 2026-07-23 · **Baseline:** master @`5db2b9e` (physical layout merged; nine libraries
physically homed; 1583 tests). · **Parent:** `2026-07-16-library-architecture-design.md` §7
(Placement / Containment / Equipment; the Stage 1/Stage 2 staging and the access-site scale).
· **Owner decisions (2026-07-23):** FULL PROGRAM approved (both stages, three waves);
**merge grants split by risk** — merge-when-green for the two Stage 1 conversion waves,
OWNER-MERGES for the Stage 2 swap wave; **NOWHERE policy = STRICT EQUIVALENCE** (the swap is a
pure refactor: every absence-observing site behaves identically, characterization-pinned;
zero observable gameplay change).

## Problem / decision

The placement-seam wave centralized location MUTATION in `rots_entity` and seeded the Stage 1
read APIs (`location_of`/`set_location`/`is_in_room` in `handler.h`/`placement.cpp`, the
`occupants()` range — landed "unused"). The READ surface never converted: **810 raw
`->in_room` sites, 585 `world[...]` lookups, ~104 `next_in_room` traversals** remain across
the game (fresh counts at `5db2b9e`; the parent spec's 754/576/104 were pre-layout-wave
estimates — each wave's census re-derives its own exact numbers). Until those reads go
through the APIs, the representation cannot change and the `NOWHERE`/`world[NOWHERE]` hazard
class stays live. This program finishes §7's location arc: encapsulate every read (Stage 1,
two waves), then swap the representation (Stage 2, one wave).

## Program shape (three waves, each with its own spec-level census, plan, and finalization)

**Wave LS-1 — library-tier read conversion (branch `arch/ls1-library-reads`,
merge-when-green).** Scope: every raw location-read site inside the six source-bearing
libraries — `rots_entity`, `rots_world`, `rots_combat`, `rots_script`, `rots_olc`,
`rots_pathfind` (`rots_platform`/`rots_core`/`rots_persist` are expected near-zero; the census
confirms). T0 census classifies EVERY site: MECHANICAL (a pure read → `location_of(ch)` /
`room_by_id(id)` / `occupants(room)` range-for) vs FLAGGED (write-through idioms, `NOWHERE`
comparisons feeding control flow, manual `next_in_room` splices, aliasing/pointer arithmetic —
each flagged site gets an explicit per-site disposition, never a silent force-fit).
Conversions land in per-library batches (tier order), standing dual-host gates per batch,
goldens byte-identical throughout — reads are representation-neutral, so ANY drift = a bug.

**Wave LS-2 — app-tier read conversion (branch `arch/ls2-app-reads`, merge-when-green).**
Same machinery over `src/app/` (the `act_*` family, `comm.cpp`, `interpre.cpp`, the
`handler.cpp`/`utility.cpp` remainder, …) plus `src/tests/` fixtures that poke `in_room`
directly (test edits → the standing ASan rule). **Exit criterion (mechanically verified):**
raw `->in_room` / `world[...]` / `next_in_room` access exists ONLY inside `rots_entity`'s
placement internals (an allow-listed file set the census names; a grep gate proves it and
becomes a standing check so regressions can't creep back).

**Wave LS-3 — the Stage 2 swap (branch `arch/ls3-locationsystem`, OWNER MERGES).**
A `LocationSystem` owned by `rots_entity` maps `char_data*` → room id and room id → occupant
sequence; `char_data` sheds `in_room`/`next_in_room` (or keeps only a private handle — the
wave's T0 rules it); "no location" = absent from the map; `NOWHERE` and the `world[NOWHERE]`
indexing hazard retire. **Strict equivalence (owner-set):** before the swap lands, every
absence-observing path is characterization-pinned to today's behavior (T0 enumerates every
`NOWHERE` comparison; paired tests assert identical outcomes under the map); zero observable
gameplay change; boot and combat goldens byte-identical. `rots_convert` links
`rots_core`+`rots_persist` and never instantiates the `LocationSystem` (the parent spec's
"character with no location" principle). A perf gate (occupant-iteration microbenchmark vs
the intrusive list, plus the boot-time delta) must pass before the PR; the PR presents the
equivalence evidence and the perf numbers; **the owner merges.**

## Constraints (program-wide)

- Stage 1 waves are ZERO-BEHAVIOR-CHANGE: goldens never regenerate; any drift = bug. The
  APIs wrap today's representation (zero-cost inlines); no signature outside the placement
  system changes; struct layouts untouched until LS-3.
- Test counts move only by characterization/equivalence/coverage additions, censused per
  wave (no frozen count — the standing coverage-gap rule applies when a conversion touches
  previously-untested live code).
- Standing cadence per wave: per-batch dual-host gates (macOS arm64 + `rots64`, both boot
  goldens, nine linkchecks, `ConvertEquivalence` 17/17, string-view census), ASan on
  test-file changes, i386 battery + six blocking CI jobs + Fable whole-branch review at
  finalization. Python byte-edits for all existing-file changes; the conversion editing style
  follows the physical-layout wave's bounded-edit lesson (no whole-file regex).
- Layer discipline: the APIs live in `rots_entity` (L2), already linked by every consumer
  tier. No membership/linkcheck changes expected in LS-1/LS-2; LS-3 stays inside
  `rots_entity` by design. Any surfaced coupling follows the playbook; unexpected = STOP.
- STOPs to the owner: a flagged-site class with no clean disposition; any Stage-1 golden
  drift traced to a conversion; an LS-3 equivalence break that cannot be pinned; a perf-gate
  failure at LS-3.
- Process: subagent-driven per wave (Sonnet implementers, Opus census/heavy reviews, Fable
  whole-branch); scratch prefixes `ls1-`/`ls2-`/`ls3-` in `.superpowers/sdd/`; docker
  synchronous with explicit 600000 ms timeouts (the standing stall lesson).

## Risks

- **Hidden writes masquerading as reads** (e.g. `ch->in_room = x` buried near reads, or code
  caching `in_room` across a mutation): the census classifies every site's read/write role;
  writes route through `set_location`/the existing mutation core, never silently.
- **`next_in_room` traversals** (~104) are the trickiest mechanical class — each becomes an
  `occupants()` range-for; the census flags any traversal that mutates the chain mid-walk
  (must use the mutation-safe idiom the placement core already provides, or be FLAGGED).
- **`world[...]` writes vs reads**: `world[id]` appears in both roles; only reads convert to
  `room_by_id` in Stage 1 — the census separates them.
- **Perf at LS-3**: the map swap changes iteration locality; the perf gate exists precisely
  so this is measured, not assumed. Stage 1 has zero perf risk (same representation).
- **Scale**: ~1,400 sites across two waves is the largest mechanical campaign yet; the
  physical-layout wave's tranche pattern (sequential batches, per-batch gates, strategic
  mid-wave CI pushes) carries over directly.

## Success criteria

1. After LS-2: the grep gate proves no raw location access outside the placement allow-list.
2. After LS-3: `char_data` has no `in_room`/`next_in_room` fields; `NOWHERE` is gone from the
   tree; all goldens byte-identical; perf gate passed; owner merged on presented evidence.
3. Every gate green at every step: the program never leaves the tree in a state where a
   revert is harder than a fix-forward.
