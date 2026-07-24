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

## Wave LS-1 As-built

Branch `arch/ls1-library-reads`, baseline master @`5db2b9e` (1583 tests). Merged as designed
(merge-when-green); the combat row stays DONE (no library-membership or DEFER change). Full
process record: `.superpowers/sdd/ls1-census.md` (T0, three post-review amendments), `ls1-task-
{1,1b,2,3}-report.md` (implementation), `ls1-global-constraints.md` (the plan). This section is
the reconciled, load-bearing summary; the task reports are authoritative for byte-level detail.

### The census's own self-correction: three amendments, not a first-pass-perfect design

T0's census was reviewed before T2 began converting, and caught real gaps in its own first pass —
recorded here because they shaped the recipe every tranche then executed, not as after-the-fact
narration:

- **Amendment 1 — Flag Family F ("find-first-break" walks).** The original Step 4 flag taxonomy
  (save-next mutating walks, manual splices, peek-ahead, in-room-as-cursor) missed a fifth class:
  a `next_in_room` walk that `break`s on a match and then reads the found pointer *after* the loop*.
  These ARE mechanically convertible to a range-for, but not naively — the found variable is scoped
  inside a naive range-for, so the conversion needs an explicit `found = nullptr` pre-init before the
  loop. The review found the exact UB hazard this protects against: `spec_pro.cpp`'s
  `vampire_killer` declared `victim`/`victim2` **uninitialized**, relying on the raw for-loop's own
  init-expression to leave them at `nullptr` on the empty-room path — a naive range-for conversion
  would read uninitialized memory there, a bug the goldens would not have caught (the fixed rooms in
  question are usually occupied). A full six-library sweep found exactly 10 Family-F sites (all in
  `combat`/`script`); T2 applied the pre-init recipe verbatim at each.
- **Amendment 2 — the CENSUS CONTRACT.** The review found the census's own per-file/per-token tables
  were a classification *guide*, not an exhaustive line ledger — a heuristic classifier undercounted
  in places (most notably the self-room `world[X->in_room]` count, corrected from ~130 to ~154-161
  sites, which *strengthened* rather than weakened the `room_of(ch)` justification). The binding
  contract this amendment set: T2 re-enumerates every file with a fresh grep at conversion time and
  classifies by the census's *rules* (which are exhaustive and authoritative), never by trusting the
  per-line counts; the T3 gate is the fail-closed backstop that makes an undercount unable to ship
  silently. Every tranche report confirms fresh re-greps found small deltas (never a missed rule).
- **Amendment 3 — the dot-access token.** Reference-parameter code (`const char_data& character`)
  reads `character.in_room`, invisible to an `->in_room` grep — a real miss the census's own T0 pass
  made, caught first at tranche A (`char_utils.cpp:1017`'s `can_see`, fixed in Task 1b). The tracked
  token set widened to include `\.in_room` (word-boundary, excluding `was_in_room`) for every
  subsequent re-grep and for the T3 gate itself.

### The `occupants()`-was-TU-local discovery, and Task 1b

T1 (API completion) landed only `room_of(ch)`, following the census's Step 5 ruling that the const-
`occupants()` overload was unjustified ("zero counted const-room walks"). Tranche A's `world` batch
then discovered a wave-level blocker the census had not anticipated: `occupant_range`/`occupants()`
(the range the conversion recipe names as the target for every `next_in_room` walk) had **no
declaration in any header** — it was defined entirely inside `placement.cpp`, unreachable from any
other translation unit. This is the flip side of the placement-seam wave's own comment on the range:
it had "landed unused," and nothing in Stage 1 up to that point had needed to call it from outside its
home file. Two `zone.cpp` Family-F sites tranche A found (also missed by the census's own claim that
Family F existed only in `combat`/`script`) made the gap concrete: they were genuinely convertible but
blocked on this header-exposure problem.

Task 1b (a T1b-style follow-up, not originally planned as a separate task) closed it: moved
`occupant_range`/`occupants()` verbatim into `handler.h` (byte-identical bodies, diff-verified during
construction) so every one of `handler.h`'s 59 consumers can reach it, and — because tranche A's own
conversion work surfaced a real const-room walk (`char_utils_combat.cpp`'s `get_engaged_characters`,
which takes a `const room_data&`) the census's "zero counted const-room walks" claim had missed —
added the `const_occupant_range`/`occupants(const room_data*)` overload in the same task, retrofitting
the four call sites tranche A had left blocked. This is the wave's one closed feedback loop that
crossed task boundaries: T1 scoped correctly against what the census knew at T1's own start, and T1b
absorbed exactly what T2's early conversion work discovered T1 had not anticipated.

### Two misclassified walks caught and fixed during conversion (not census-listed)

Beyond the routine per-file re-grep deltas Amendment 2 anticipated, two `next_in_room` walks in
tranche C were misclassified by the census's original Step 4 sweep as simple (no mid-walk mutation)
when they were actually first-match-wins guards needing the same disposition care as a Family-F site:
`script/mobact.cpp`'s aggressive-mobs walk and `script/script.cpp`'s `trigger_room_event`. Both were
caught by T2's own body-read-before-convert discipline (the standing re-verify instruction Amendment
2's contract requires), converted correctly, and — for `mobact.cpp`, since mob AI is a
regression-sensitive path — regression-tested explicitly rather than relying solely on the boot/seed42
goldens as the neutrality witness.

### Per-tranche batch list, as landed (see AGENTS.md's chain entry for the full commit-by-commit test
delta)

1. **T1** — `room_of(ch)` (consumer-free, TDD). Commit `61a97fc`.
2. **Tranche A** — `entity` (six convertible TUs; `placement.cpp`/`containment.cpp` allow-listed),
   `persist` (`db_players.cpp`, ruled into LS-1 scope per Discrepancy 1 — its two reads would
   otherwise strand LS-2's exit criterion), `world` (`db_world.cpp`/`weather.cpp`/`zone.cpp`, the
   read/write-adjacency-care batch), plus the `weather.cpp` coverage rider. Commits `f71f5c5`/
   `d6436bd`/`4d86b61`/`cf863a9`.
3. **T1b** — the `occupants()` public-home move + const overload + four blocked-site retrofits
   (two `zone.cpp` Family-F sites, the `char_utils_combat.cpp` const-room walk, the `char_utils.cpp`
   dot-access site). Commits `295db7e`/`5d69121`.
4. **Tranche B (combat)** — `fight.cpp`, `mage.cpp`, `ranger.cpp` each their own sub-commit (per the
   census's density-driven batching), then `limits`/`mystic`/`olog_hai`/`spell_pa`/`clerics`/
   `visibility` grouped, then the controller-review-caught `spell_terror` coverage-gap fix
   (`mystic_tests.cpp`, new file). Commits `59ef752`/`ef4f36d`/`e18c86d`/`1eee40c`/`23b9e2b`.
5. **Tranche C** — `pathfind` (`graph.cpp`, macro-dense), `script/spec_pro.cpp` its own commit (the
   census's densest single TU, all six Family-F sites incl. the three UNINIT traps), `script/
   mobact.cpp` its own sub-commit, `mudlle.cpp`+`script.cpp` grouped, then the five `shape*.cpp` OLC
   editors. Commits `7a8d782`/`2984e3a`/`d21ed46`/`e40c009`/`c61de2f`.
6. **T3** — the `LocationReadCensus` gate: an annotation sweep (46 new `LS1-ALLOW` lines across 15
   files, closing every un-annotated raw site the prior tranches had correctly left raw but not yet
   labeled) then the gate script itself, `ctest`-registered plus flat-Makefile-wired. Commits
   `c661cd3`/`c42eb89`.

### The gate and the macro boundary

`tools/location_read_census.py` is the completeness backstop the CENSUS CONTRACT (Amendment 2)
promised: it scans `src/{entity,persist,world,combat,pathfind,script,olc}/*.cpp` for the four
tracked tokens against comment/string-masked text, allow-listing the two representation-owner files
whole-file and any `// LS1-ALLOW: <reason>` annotated line against an eight-reason authorized list
(the census's seven plus T3's own narrowly-justified `resolver-impl` addition for `db_world.cpp`'s
three resolver-implementation lines — documented in the task-3 report as a deliberate, visible
extension rather than a silent one). Self-tested both directions (an injected un-annotated raw line
trips `--check`; an injected line with a bogus reason also trips it, proving the reason-prefix
validation is load-bearing). It is `ctest` #1618 on every preset and wired into the flat
`src/tests/Makefile`'s `tests` recipe.

**Recorded prominently for LS-2/LS-3 (KNOWN BOUNDARY, not an oversight):** the gate scans `.cpp`
bodies only — it cannot see raw reads hiding behind `src/utils.h` macros (`EXIT`/`OUTSIDE`/
`IS_WATER`/`SUN_PENALTY` and similar expand to `world[(ch)->in_room]` at their call sites), roughly
90 additional sites census-sanctioned out of LS-1's charter. LS-3's representation swap must convert
those macro bodies directly; until then, a macro call site reads as clean to this gate while still
touching the raw representation one level down. `zone_table[...]` (~201 sites) remains explicitly out
of both LS-1's and LS-2's charter (Discrepancy 2) — `zone_by_id()` exists as its resolver, but the
program's tracked triple and every exit criterion are `->in_room`/`world[...]`/`next_in_room` only.

### Reconciled chain

1583 → T1 +2 = 1585 → tranche A +8 = 1593 → T1b +4 = 1597 → tranche B +5 = 1602 → tranche C +15 =
1617 → T3 +1 = **1618** both hosts (macOS native, `rots64`), ASan clean at every test-touching task,
`ConvertEquivalence` 17/17 and `python3 tools/string_view_census.py --check` exit 0 throughout, both
boot goldens and the seed42 characterization golden byte-identical at every commit. Skips carried
forward unchanged from the physical-layout wave: 75 (macOS) / 77 (`rots64`). The i386 finalization
battery is PENDING T5 — no numbers are recorded here until it is measured, per the standing
no-invented-numbers rule.
