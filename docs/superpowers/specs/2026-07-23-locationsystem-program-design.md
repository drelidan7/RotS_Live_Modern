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

> **AMENDED 2026-07-27 (wave LS-3a, T5, ruling R-C3):** two clauses of criterion 2 — "`NOWHERE` is
> gone from the tree" and "the `world[NOWHERE]` indexing hazard retires" — are FALSE as written.
> They are left standing above as the planning-time record; the corrected criteria are in the
> **Wave LS-3a As-built** section at the end of this document.

## Wave LS-1 As-built

Branch `arch/ls1-library-reads`, baseline master @`5db2b9e` (1583 tests). Merges when green at T5
(pending as of this docs task); the combat row stays DONE (no library-membership or DEFER change). Full
process record: `.superpowers/sdd/ls1-census.md` (T0, three post-review amendments), `ls1-task-
{1,1b,2,3}-report.md` (implementation), `ls1-global-constraints.md` (the plan). This section is
the reconciled, load-bearing summary; the task reports are authoritative for byte-level detail.

### The census's own self-correction: three amendments, not a first-pass-perfect design

T0's census was reviewed before T2 began converting, and caught real gaps in its own first pass —
recorded here because they shaped the recipe every tranche then executed, not as after-the-fact
narration:

- **Amendment 1 — Flag Family F ("find-first-break" walks).** The original Step 4 flag taxonomy
  (save-next mutating walks, manual splices, peek-ahead, in-room-as-cursor) missed a fifth class:
  a `next_in_room` walk that `break`s on a match and then reads the found pointer *after* the loop.
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

## Wave LS-2 As-built

Branch `arch/ls2-app-reads`, baseline master @`cbde50c` (LS-1 merged, PR #21, 1618 tests). Merges
when green at T7 (pending as of this docs task); the combat row stays DONE (no library-membership or
DEFER change — LS-2 never touches `rots_combat`/`rots_script`/`rots_olc` membership). Full process
record: `.superpowers/sdd/ls2-census.md` (T0, eleven post-review amendments), `ls2-census-{a1,a2,b,c}.md`
(the four source censuses), `ls2-census-review.md` (the adversarial census review — 2 BLOCKER / 6
IMPORTANT / 7 MINOR), `ls2-task-{1,3a,3b,3c,3d,4,5}-report.md` (implementation — T2 has no standalone
report; its commit message `a1e7673` is its record), `ls2-task-3a-review.md` (the T3a implementation's
own adversarial review — 0 BLOCKER / 5 IMPORTANT / 9 MINOR), `ls2-global-constraints.md` (the plan,
rulings R1-R11 plus the CADENCE AMENDMENT). This section is the reconciled, load-bearing summary; the
task reports are authoritative for byte-level detail.

### The scope amendment: `src/tests` deferred to LS-3a, not converted (R2)

The program spec's own LS-2 charter (above, "Program shape") named `src/tests/` fixtures that poke
`in_room` directly as in scope, alongside `src/app/`. T0's census measured what that would actually
cost: **517 token occurrences in the test tier, of which only 25 lines (4.8%) are genuine convertible
reads** — the other ~95% is *fixture construction*: 150 char-location writes, 76 occupant-chain
writes, 86 `world[]` assignment targets. LS-2 is a reads-only wave; fixture construction is a *writes*
problem, and writes are LS-3's charter, not LS-2's. Two independent censuses (B and C) reached the
same conclusion from different angles and both explicitly rejected a middle path — annotate the
~288-301 write-only sites without building a helper, so the gate could go green over the test tier
without doing the conversion work. The rejection *reason*, not merely the rejection, matters: census C
found the resulting green gate would be **actively misleading** — `ScopedTestWorld::room()` hides 45
occupant-chain accesses that carry **no tracked token at all**, so annotating every visible raw site
would still leave a gate reading "clean" over a tier where a real fifth of the occupant-chain surface
stays invisible to it by construction. The right fix — census C's two-entry-point test helper
(`test_set_location` + `ScopedRoomOccupants`), which the tree had already invented ad hoc twice
(`spell_pa_tests.cpp:36`, `protocol_tests.cpp:313`) — belongs in the same wave as the production write
conversion it exists to serve, not split across two waves. **Ruling R2**: `src/tests` defers to LS-3a
in full; LS-2's exit criterion is scoped to production code only, and the deferral is a recorded,
gate-visible boundary — exactly as LS-1 recorded the `utils.h` boundary this wave closes (below) — not
a silent gap. T5 implements the deferral as a named `DEFERRED_DIRS` module constant with a visible
per-run notice, never a ledger row (a ledger row would assert the tier is a permanent representation
owner, which it is not).

### The `utils.h` macro boundary closes (R1)

LS-1 explicitly deferred this as a KNOWN boundary: the gate scans `.cpp` bodies, not header macro
definitions, so a macro's raw `world[]` expansion at any of its call sites reads as clean to a
grep-based gate. T0's census (agent B) measured the real shape: **9 macros total, 7 with a
token-bearing line, 282 real call sites tree-wide (181 app + 101 library + 0 tests)** — `EXIT` alone
accounts for 250 of them. T2 (commit `a1e7673`) converted all seven: `IS_DARK`, `IS_SUNLIT_EXIT`,
`IS_SHADOWY_EXIT`, `SUN_PENALTY`, `OUTSIDE`, `EXIT`, `IS_WATER`. `IS_LIGHT`/`IS_SUNLIT` needed no
edit — census-review finding A-5 (originally MI-5) corrected the census's own "nine macros" framing:
`IS_LIGHT` is `(!IS_DARK(room))` and `IS_SUNLIT` delegates to `IS_LIGHT`, so both inherit the fix
through `IS_DARK` without ever referencing `world` themselves; `IS_LIGHT` additionally has zero live
call sites tree-wide.

Every `world[X]` inside the seven bodies became exactly **one** resolver call — deliberately **no
hoisting**. `room_data::operator[]` mudlogs (and can `exit(0)`) on an out-of-range index, so collapsing
a macro's N reads into one cached local would silently drop N-1 mudlogs, an observable behavior change
a zero-behavior-change wave cannot make. `SUN_PENALTY` still resolves three times after conversion
(once via its nested `OUTSIDE`, twice directly), exactly as before. Resolver choice is
`room_by_id_total`/`room_of` only — none of the nine macros bounds-checks its argument, so the
graceful-fallback resolver is the only behavior-preserving choice (see "The `room_by_id` ban,
reconfirmed" below).

The fix landed as **two forward declarations** (`room_by_id_total`, `room_of`) transcribed
byte-identically from `handler.h:78`/`:94` into `utils.h`, not `#include "handler.h"` — `utils.h` has
75 consumers, and an include would add transitive weight and make the L2 Stage-1 API visible from L1
`src/core/consts.cpp`. Of the 18 TUs calling an affected macro, 17 already included `handler.h`
transitively; only `src/app/protocol.cpp` did not (one `OUTSIDE()` call at `:2932`), and it needed no
edit — `comm.h:19` already includes `utils.h`, so the forward declaration reaches it there. (The T3a
implementer briefly added a redundant `#include "handler.h"` to `protocol.cpp` anyway, on a premise
that had already stopped being true once T2 landed; the T3a adversarial review's F1 finding caught it
and it was reverted in `694123d` — see "The census's own self-correction" below for the review's full
tally.) A census-review finding (A-3, originally IM-3) sharpened R1's own blocker analysis: the 282
call sites become **hook-dispatched** through `entity_hooks.h` into `rots_world` and `abort()` if
`register_world_resolver_hooks()` has not run — risk is nil today (both registration points and the
resolver-stub fixture were verified), but the review's re-derivation covered all nine macros where R1's
own supporting grep had covered only four; the *evidence* was amended, the ruling was not. `nm`/grep
verification (ruling R11) confirmed `rots_convert`'s one direct source, `convert_main.cpp`, has zero
live macro call sites (all twelve hits sit inside its own block comment), so the newly hook-dispatched
resolvers never land on the CMake-only `rots_convert` target's link surface.

### The census's own self-correction: eleven amendments, not three

T0's census was reviewed before T2 began converting, exactly as LS-1's was — but where LS-1's review
took three amendments, LS-2's adversarial review (`.superpowers/sdd/ls2-census-review.md`) returned
**2 BLOCKER / 6 IMPORTANT / 7 MINOR** and forced **eleven** amendments (A-1 through A-11,
`.superpowers/sdd/ls2-census.md`'s own "Amendments" section), while still finding **no behavioural
defect** — every per-site disposition the reviewer re-derived by hand was behavior-preserving. The
load-bearing ones:

- **BL-1 — the plan's own task chain and the census's batch table numbered the same work two
  different ways**, and the plan's checklist still commanded the test-tier work R2 had just ruled out
  of scope (a stray "T4 — test-tier conversions" line, and T5 listed twice). This is a subagent-driven
  wave where every task brief cites a task number; a subagent briefed from the unfixed checklist would
  have executed census C's 25 read conversions plus ~288 annotations across 25 test files — precisely
  the work R2 forbids. Fixed by renumbering `ls2-global-constraints.md`'s task chain to the single
  authoritative numbering ("Architecture (task chain)": T2 = `utils.h` macros, T3a-d = the app-tier
  tranches, T4 = LS-1 inherited test debt only, with an explicit "no `src/tests` location conversions
  this wave" line) and declaring it authoritative over the census's own (now-renumbered) batch table.
- **BL-2 — R3's own gate-extension ruling was self-contradictory: it demanded `src/tests` be
  excluded, but supplied no exclusion mechanism**, and its "no build-file edit" ruling would have left
  the omission unfixable at the two call sites. The measured cost of shipping the census's own
  supplied `source_files()` snippet as written: `src/tests` = 388 live token lines / 414 occurrences /
  27 files, which would have turned `LocationReadCensus` red on every preset the moment the scan
  widened. Fixed by ruling the exact mechanism (a named `DEFERRED_DIRS` module constant, not a
  hard-coded invisible exclusion) and requiring the visible per-run notice T5 implemented.
- **A-1 (was IM-1) — the forced-declaration-deletion list was short by one**: `act_info.cpp:1040`'s
  `struct char_data* i;` in `list_char_to_char`, used only inside the walk R8 converts. Missed by
  both source censuses (A1 and A2); without it, T3b would have failed `-Werror` outright. Count
  corrected 10 → 11.
- **A-5 (was MI-5) — "seven macros need edits, not nine"** — the `IS_LIGHT`/`IS_SUNLIT` correction
  described above, which changed the scope of T2's own commit before it landed.

Six more amendments (A-2 through A-4, A-6 through A-10) are recorded verbatim in
`.superpowers/sdd/ls2-census.md`; A-11 is a process note (T1 landed while the review was still
running — deliberate, and the review independently verified R8 correct at both sites rather than
treating the overlap as a defect). The T3a implementation itself then received its **own** adversarial
review (`.superpowers/sdd/ls2-task-3a-review.md`, 0 BLOCKER / 5 IMPORTANT / 9 MINOR, no behavior
change found) — five findings (F1-F5) closed across later commits: F1/F2 immediately (`694123d`),
F3/F4/F5 folded into T4's three fix commits (`6801bb1`/`b096229`/`305ee4a`) — see "Two confirmed
census defects" and the reconciled chain below.

### Four `in_room` fields, two of them newly named this wave (R10, R5)

The tree has **four** distinct fields textually matching `in_room`, and LS-2 is the wave that named
the last two:

1. `char_data::in_room` — the subject of the whole LocationSystem program.
2. `obj_data::in_room` — an object's location; out of LS-1/LS-2's char-location charter (there is no
   `location_of(obj)`); every hit stays raw, annotated `// LS1-ALLOW: obj-location`.
3. `shop_data::in_room` (`shop.cpp:58`) — a **shop VNUM**, not a character or object location at all
   (R10, confirmed by census A2's Finding C). All five of `shop.cpp`'s dot-access hits (`:588`/`:594`/
   `:600`/`:606`/`:682`) are this field; they convert to nothing and get the wave's third new
   annotation reason, `// LS1-ALLOW: not-a-location`.
4. `char_data::was_in_room` (`src/core/include/rots/core/character.h:333`, `/* storage of location
   for linkdead people */`) — a **second, parallel location store** no prior census had named (R5,
   found by census B). It is invisible to every gate token by construction (the gate's regex
   deliberately excludes it). Out of LS-2's scope entirely; recorded as a named LS-3 input — the
   representation swap cannot retire `char_data::in_room` without a ruling on what happens to this
   sibling field.

### Two gcc/clang divergences, and one MSVC hazard avoided pre-emptively

The macOS-only implementer gate (per the CADENCE AMENDMENT, below) cannot see everything the
`rots64`/CI compilers see:

- **Block-scope `extern struct room_data world;` unused after conversion.** T3a's conversion of
  `protocol.cpp::broadcast_weather_msdp_update` removed the last `world[]` use in that function's
  scope, leaving its block-scope `extern struct room_data world;` (`protocol.cpp:2909`) unused. gcc
  treats this as `-Wunused-variable`; clang does not — so the macOS `-Werror` build stayed clean while
  the `rots64` build (gcc 14.2) failed, caught only by the tranche-end container gate. Fixed forward
  in `b24a149`. Only **block-scope** declarations are affected — gcc does not warn on unused
  **file-scope** externs, and the tree's ~45 other `extern struct room_data world;` declarations are
  all file-scope and correctly stay untouched (T3c re-verified this explicitly for `act_move.cpp:36`,
  whose file-wide `world[]` use count also dropped to zero without triggering the rule). This class is
  binding for every remaining tranche going forward: after converting a file, grep it for an indented
  `extern struct room_data world;` and check whether any `world` object use survives in that scope.
- **`comm.cpp:2710`'s `occupants_from` conversion renamed its loop variable rather than shadowing.**
  `act_impl`'s original bare walk was `for (; to; to = to->next_in_room)`; the mechanical conversion
  would be `for (auto* to : rots::entity::occupants_from(to))`, which compiles cleanly under this
  repo's GNU-family flags (`-Wshadow` is deliberately not part of the `-Wall -Wextra -Werror` set) but
  trips MSVC `/W4`'s C4456 ("declaration hides previous local declaration"), which `/WX` turns into a
  hard error on the `windows-msvc` CI job — one of the six blocking jobs. T3d renamed the loop
  variable to `recipient` throughout the body (7 occurrences) instead, avoiding the hazard at the cost
  of a slightly larger diff; the range-for's initializer expression is evaluated in the enclosing
  scope before the new variable exists, so the rename changes nothing about which `to` the walk starts
  from.

### `occupants_from(head)`: one factory beats a two-site allow-reason (R8)

T1 (commit `62c8a12`, consumer-free/TDD, following LS-1's own `room_of` shape) added
`rots::entity::occupants_from(char_data* head)` to `handler.h` — a second `occupant_range`
constructor seeding the walk directly at an arbitrary occupant, reusing the existing iterator
byte-for-byte. Two production sites needed it: `act_info.cpp:1043`'s `list_char_to_char` (A-7 flagged
this as a registered `combat_hooks` hook body, not merely a caller, with live `mage.cpp` consumers —
so the batch converting it had to be seed42-gated) and `comm.cpp:2710`'s `act_impl` (a polymorphic
head: a room's `people` list for `TO_ROOM`/`TO_NOTVICT`, but `vict_obj`/`ch` themselves for
`TO_VICT`/`TO_CHAR`, with an early `return` inside the loop for the latter two). Census A2 had
proposed a `chain-walk from a non-room-head cursor` allow-reason instead — leave both sites raw,
annotated. T0 ruled the factory over the allow-reason for three reasons: it makes a materially
stronger exit criterion true (zero production `next_in_room` walks outside the allow-list, not "two
annotated exceptions"); it avoids minting a reason that would exist for exactly two sites; and for
LS-3, a walk spelled `occupants_from(head)` is one seam to re-point, where two hand-written loops are
two manual rewrites. The census-review's process finding A-11 independently swept all 78 production
`next_in_room` lines and confirmed exactly two head-walks exist, re-verifying the factory is correctly
sized rather than over- or under-built.

### The `room_by_id` ban, reconfirmed under load

LS-1 first ruled `room_by_id()` banned as a conversion target; LS-2 kept the ban and it held under a
much larger call volume. `room_by_id_impl` (`src/world/db_world.cpp:282`) rejects `rnum >=
top_of_world` **exclusively**, but `top_of_world` is an **inclusive** last-index everywhere in
production (`for (i = 0; i <= top_of_world; i++)`) — so the resolver silently excludes the last valid
room, a deliberate reproduction of `recount_light_room`'s own historical guard, and a trap for any
other caller. The concrete failure mode: `ScopedTestWorld` sets `top_of_world = room_count - 1`, so
`room_by_id(0)` returns **nullptr** under the single-room fixture roughly a dozen suites use. Every
LS-2 conversion uses `room_by_id_total` or `room_of` instead — both graceful-fallback, never null.
T3c's report demonstrates the ban's teeth directly: three sites (`check_simple_move:177`/`:178`, and
the four-times-repeated door family's `do_open`/`do_close`/`do_lock`/`do_unlock` resolver, and
`do_pull`'s pair) each cache a resolved room and immediately test `if (!room)` — a branch that was
already dead under `&world[x]` (never null) and stays exactly as dead under `room_by_id_total` (also
never null). Had `room_by_id` been used at any of these instead, an out-of-range `to_room` would make
the branch **live** for the first time — a silent behavior change no golden would catch. T4's
`SpecProVampireKiller` regression test goes one step further and deliberately positions a fixture room
at `world index 2 == top_of_world`, so a hypothetical future regression that substituted the banned
resolver would **crash the test fixture** rather than pass silently — the test is positioned to catch
exactly the resolver-class mistake this ban exists to prevent, not merely "does it compile."

### Two confirmed census defects

- **`db_boot.cpp::record_crime`'s witness walk does not exclude the victim.** Census A2 asserted
  `add_crime()` fires exactly once per crime, for "the eligible witness"; the filter
  (`(tmpchar == criminal) || IS_NPC(tmpchar) || GET_LEVEL(tmpchar) >= LEVEL_IMMORT`) excludes the
  criminal by pointer identity but never the victim, and the victim is necessarily an occupant of
  their own crime scene — so a realistic fixture yields **two** calls, not one. The T3a implementer
  caught this by testing against the function's actual, verified behavior rather than the census's
  literal framing (which would have produced a failing test, and "fixing" it to pass would have been
  a behavior change in a zero-behavior-change wave); the T3a adversarial review independently
  confirmed the defect and the implementer's correction. This is the identical class of finding LS-1's
  own process surfaced repeatedly — the census is a classification *guide*, never ground truth for a
  specific runtime behavior.
- **`handler.h`'s second `representation-impl` site drifted from the census's stated `:192` to the
  actual `:223`.** Census B (written before T1 landed) named `handler.h:131`/`:192` as the two
  `occupant_range`/`const_occupant_range` `operator++` bodies needing the annotation T5 would add. T1
  (`occupants_from`, `62c8a12`) inserted ~31 lines of new content between the two bodies after the
  census was written, shifting the second site down by 31 lines. T5 caught this immediately by
  re-grepping at conversion time rather than trusting the stale line number — exactly the CENSUS
  CONTRACT (LS-1 Amendment 2) requires, carried forward into this wave unchanged.

### What LS-2 did not do

Recorded as a boundary, not an omission — every item below is a named LS-3 input:

- **`src/tests`** (R2, above) — LS-3a's charter in full: the 25 genuine reads, the fixture-writing
  helper (`test_set_location`/`ScopedRoomOccupants`), and the gate's extension to cover it.
- **Every WRITE site.** LS-2 is reads-only throughout; `ch->in_room = …`, `world[i].field = …`, and
  every occupant-chain splice stays raw, annotated `// LS1-ALLOW: write`. Measured at LS-2's baseline:
  224 `(?:->|\.)in_room\s*=` writes (55 production + 169 tests) and 82 `next_in_room\s*=` writes (6
  production + 76 tests) — the good news buried in that second figure is that outside tests the
  occupant-chain splice is already down to six sites, five of them inside the allow-listed
  representation owner `placement.cpp`; the placement-seam wave's centralisation held. A related,
  gate-invisible wrinkle this wave itself produced: R6 converts a room-field write's *navigation*
  while leaving the *assignment* raw (`room_of(ch)->room_track[tmp].field = v;`), so after conversion
  none of the resulting 12 lines (`act_move.cpp` — `set_blood_trail`'s `bleed_track` quartet, and
  `perform_move_mount`'s and `do_move`'s `room_track` quartets) carries an `LS1-ALLOW` annotation or
  matches a bare `world[` grep; T3c's report enumerates all 12 lines verbatim so LS-3a's own census
  can re-add them to its write inventory by hand.
- **`zone_table[...]`** — out of both LS-1's and LS-2's charter throughout; `zone_by_id()` exists as
  its resolver, but the program's tracked triple is `->in_room`/`world[...]`/`next_in_room` only.
  Fresh count at LS-2's HEAD, **basis: comment/string-masked OCCURRENCES across all `src/**`** =
  **268** (`src/world` 126, `src/app` 51, `src/olc` 38, `src/tests` 33, `src/combat` 17, `src/script`
  3). Subtracting `src/tests` (33) gives **235 production masked occurrences — that is LS-3's
  planning figure**, and it is the number `docs/BUILD.md`'s basis matrix bolds. LS-1's own doc cited
  "~201" as a six-library figure, a different (narrower) base, not an error. **State the basis
  whenever quoting any of these**: the LS-2 follow-up quoted "185 production occurrences" when 185 is
  production masked *lines*, and adversarial review caught it — twice in the same sentence's history,
  which is why both documents now carry an explicit basis.
- **`NOWHERE` retirement.** Every `location_of(ch) == NOWHERE` comparison is a mechanical read and
  already converts; the sentinel's *deletion* is LS-3b's strict-equivalence job. Fresh count: 253
  total (178 production + 75 tests). Two sites are **public default arguments**, not merely body
  occurrences — `combat_hooks.h:249` and `interpre.h:128` both declare `int in_room = NOWHERE`, so
  retiring `NOWHERE` is a signature change at those two seams, not only a body change; LS-3b needs to
  rule on both explicitly.
- **`room_data::people` direct access (`->people`/`.people`).** Deliberately untracked by the gate —
  the program's rules fix the token set at four (`->in_room`/`.in_room`/`world[`/`next_in_room`), and
  adding a fifth mid-wave would have tripped 164 lines (77 production + 87 tests) the census never
  scoped a conversion for. Recorded as a named LS-3 candidate token — it is the occupant-chain's own
  head, and the chain and its head should retire together.
- **`char_data::was_in_room`** — see "Four `in_room` fields" above; a named, unruled LS-3 input.
- **The `&world`/`get_world()` singleton handoff (A-10).** `db_boot.cpp:472-473` passes `&world` (the
  whole room table) into `world_singleton<T>::m_world` (`src/singleton.h:83-85`/`:115`), vended by
  `get_world()` (`:92`) — a production analogue of `ScopedTestWorld::room()`'s own token-invisible
  raw-representation handoff. `get_world()` has **zero production callers today**, so it is inert; not
  fixed this wave (out of charter — it is not one of the four tracked tokens), recorded beside
  `was_in_room` and `->people` as an LS-3 input. Also noted: `room_data::BASE_WORLD`
  (`rots/core/room.h:96`, accessed as `BASE_WORLD + i` in `db_world.cpp`) is legitimately
  `resolver-impl` territory, not a new finding requiring action.

### The CADENCE AMENDMENT (process, 2026-07-24, after T2)

The standing rule through LS-1 was "docker synchronous, foreground, 600000 ms timeout, never
backgrounded" — but T2's implementer backgrounded the `rots64` gate despite that instruction appearing
twice in its own brief, then stalled waiting for a notification a subagent can never receive (the
exact failure the standing `subagent-docker-gate-stalls` memory note describes, recurring even against
an explicit prohibition). LS-2 restructured the division of labor rather than repeating the
exhortation: implementers run macOS build+ctest, ASan on test-file changes, the native boot golden,
and both census scripts, per commit; the controller runs the `rots64` build+ctest+boot-golden leg at
each tranche's end. This removed the stall mode structurally rather than by instruction. macOS and
`rots64` did not diverge anywhere in this wave except at the one gcc/clang-only warning class recorded
above ("Two gcc/clang divergences") — exactly the class of divergence the amendment's own risk
assessment anticipated the per-tranche split would still catch, just one tranche later than a
per-commit gate would have caught it.

### Reconciled chain

1618 → T1 +5 (`occupants_from`, `62c8a12`) = 1623 → T2 +0 (`utils.h` macro bodies, `a1e7673`) = 1623
→ T3a +9 across three commits (`cff5e34`+2/`9915927`+0/`41061f0`+7) = 1632, plus the gcc-only
fix-forward `b24a149` (+0) → T3b +9 across two commits (`17281e8`+4/`33f7e3b`+5) = 1641, plus the T3a
review-closure `694123d` (+0) → T3c +10 across two commits (`7703f89`+5/`e5f2da3`+5) = 1651 → T3d +10
across four commits (`da03d10`+4/`36183f9`+2/`8250d40`+1/`7ed9069`+3) = 1661 → T4 +28 across four
commits (`da3ec7f`+10/`b096229`+1/`6801bb1`+15/`305ee4a`+2) = 1689 → T5 +0 (`9016fcf`) = **1689**.
macOS native confirmed at every commit by its own task report; `rots64` confirmed at the T3a tranche
end (1632/1632, both boot goldens byte-identical) and at wave HEAD (1689/1689); intermediate
tranche-end `rots64` numbers are the controller's per-tranche responsibility under the CADENCE
AMENDMENT above and are not independently re-documented inside the per-task reports this section draws
from. ASan clean at every task that touched a new/rewritten test file (T1, T3a, T3b, T3c, T3d, T4);
`ConvertEquivalence` 17/17 and both census scripts (`location_read_census.py`/`string_view_census.py`)
exit 0 throughout; both boot goldens and the seed42 characterization golden byte-identical at every
commit. Skips carried forward unchanged from the LS-1 wave: 75 (macOS) / 77 (`rots64`) — no LS-2 task
touched a POSIX/32-bit-fixture-gated test. **The i386 finalization battery is measured** (`log/i386-battery/`,
`step1-20260725T162134Z.log`/`step2-20260725T164047Z.log`/`step3-20260725T170331Z.log`, run at
commit `2afaee9`): **1689 total / 6 skips** via the ctest flow, reconciling exactly against the
monolithic runner (1656 passed + 23 skipped = 1679 of 1689 gtest-visible cases; the remaining 10 are
the CMake ctest-only checks — nine acyclicity linkchecks plus `LocationReadCensus`; 23 − 17
monolithic-only `PerRace/ConvertEquivalence.*` skips leaves the identical 6-test remainder both
ways), boot golden matches — the standing reconciliation method, twelfth consecutive wave.

### Finalization repairs — what the gates found AFTER the wave went green

LS-2's per-task gates were green at 1689/1689 on three hosts with every golden byte-identical. Three
independent later checks each still found something, which is the wave's most transferable result:

1. **The i386 battery's monolithic runner** — the `DoRescue` `waiting_list` leak described above
   (`2afaee9`). Invisible to ctest by construction.
2. **The CI matrix** (`0732846`) — two defects neither macOS, `rots64`, nor i386 could see.
   *Windows/MSVC:* a test helper returned a `descriptor_data` **by value** whose `output` member
   pointed at the same object's `small_outbuf`; that is correct only under NRVO, and **NRVO is
   optional** (C++17 guarantees copy elision for prvalue temporaries, not for a named local returned
   by value). GCC and Clang elide, MSVC copies, leaving `output` dangling — CI captured the shredded
   result verbatim. *Linux UBSan:* `add_crime()`'s `memcpy(dst, crime_record, 0)` with
   `crime_record == nullptr` — UB even at zero length, because `memcpy` declares both pointers
   `nonnull`. That one is a **pre-existing production defect**; LS-2's new `record_crime` coverage is
   simply the first caller ever to reach `add_crime()` with an empty crime table. It is also the
   wave's only production change outside the conversions themselves.
3. **The two adversarial whole-branch reviews** (Opus 5 and Fable 5, independent, neither seeing the
   other). **They returned different blockers** — Fable found eight fixtures leaving shared `world[]`
   state unrestored, Opus found a ninth in `protocol_tests.cpp` that Fable's sweep missed. Five of
   the nine were dangling stack pointers in `world[].dir_option[]`; one was demonstrated as a live
   SIGSEGV during the fix. Opus additionally found **five or six vacuous tests** — tests that pass
   with the code they name deleted — two of which three separate documents cited as *closing* LS-1's
   inherited Family-F debt. The true figure was **2 of 6 sites pinned**; batch 2 raised it to 4 of 6
   with positive controls verified by sabotage-and-revert, and the remaining two
   (`vampire_killer`'s self-room site and `vampire_huntress`) were re-deferred with their cost
   recorded rather than left as a false claim of closure.

Both reviewers also found the gate itself defeatable three ways — an `LS1-ALLOW` inside a string
literal, a reason that merely *prefixes* an authorized one, and a scan that **exits 0 when it finds
zero files** (a path break turning the program's fail-closed exit criterion green). All three closed.

**The lesson worth carrying to LS-3a:** a green per-test suite is not evidence of fixture hygiene,
and a passing test is not evidence that anything is pinned. ctest gives every test its own process,
so leaked pointers never outlive anything; and a test written against a function's "nothing found"
path passes whether the code under it is correct, wrong, or absent. Fable's control experiment —
excluding all 71 branch-new tests and still producing 32 failures and a SIGSEGV under
`--gtest_shuffle` — established that the residual corpus-wide fragility is **pre-existing**, rooted
in `ScopedTestWorld`'s reuse branch whose "known-good state" contract comment is false. LS-2 removed
its own nine contributions; **the structural reset and the corpus sweep are named LS-3a inputs.**

**The battery caught the wave's one real defect, on a class the ctest flow structurally cannot
see.** Its first run died with `qemu: uncaught target signal 11` after 1054 tests, inside a
*pre-existing* test (`InterpreAccountMenu.ExtractCharReturnsAccountBackedCharactersToAccountAwareMenu`).
Reproduced natively — the all-in-one-process run segfaulted at a *different* test
(`CharacterizationCombatTest.DamageTranscriptSeed42`, after 602 tests), which is the signature of
cross-test global-state corruption: the crash site is merely whoever walks the corrupted list first
under a given ordering. Bisected by gtest filter to T3d's own
`DoRescue.FindsTheFighterAndRescuesTheVictimWhenSomeoneIsFightingThem`; `lldb` pinned the fault at
`comm.cpp:2848` in `abort_delay_impl()`, dereferencing a **stack address** still linked into the
process-global `waiting_list` after its scope ended. `DoRescueContext` already saved and restored
`combat_list` for precisely this hazard — nothing covered `waiting_list`, which `do_rescue`'s path
also links into. Fixed symmetrically in `2afaee9` (restoring the head drops every entry added during
the test, including the test body's own stack-local `tmp_ch` the fixture has no handle on).

**The standing lesson, recorded for LS-3a** (which adds many more tests touching global game state):
ctest was green at **1689/1689 throughout**. It runs each test in its own process, so a leaked
pointer into a global list never outlives anything. Only the single-process monolithic runner can
catch this class — which is exactly why AGENTS.md refuses to tolerate a monolithic SIGSEGV, and why
a green per-test suite is not evidence of fixture hygiene.

## Wave LS-3a As-built

Branch `arch/ls3a-mutation`, baseline master @`ce753f5` (**1704** tests — PR #24's load-path bugfix
moved the baseline off `ca901fc`/1696 mid-planning, ruling O-1). Merge-when-green under the owner's
standing grant; the combat row stays DONE and no library-membership or `*LayerAcyclicity` change
occurred — the nine linkchecks stay nine. Full process record: `.superpowers/sdd/ls3a-global-constraints.md`
(the plan and every ruling `R-A*`/`R-B*`/`R-C*`/`R-D*`/`AM-*`/`O-*`), `ls3a-census-{a,b,c,d}.md` plus
`ls3a-census-review.md` (T0 and the adversarial census review that returned **28** amendments — LS-1's
took 3, LS-2's took 11 — five of them blockers, three of which overturned controller rulings),
`ls3a-t0b-findings.md` (the T0b targeted re-census, its rulings, and the complete per-task landing
record), and `ls3a-wave-summary.md` (the closing one-page record for the finalization reviewers).
Those documents are authoritative for byte-level detail; this section and docs/BUILD.md's
"Wave LS-3a" subsection are the reconciled summary.

### AMENDMENT (2026-07-27, LS-3a T5, ruling R-C3): two clauses of Success criterion 2 are FALSE as written

Recorded as an amendment, not an edit — the criteria above stand exactly as written so the record of
what was believed at planning time survives. Both false clauses live in criterion 2.

**(a) As written: "`NOWHERE` is gone from the tree."** FALSE. Census C measured **175** production
`NOWHERE` sites and found **106 of them survive by design**: exit-graph `to_room` values,
`obj_data` object locations, persistence sentinels, and scratch room ids. `NOWHERE` is not a
character-location detail — it is the tree's generic "no room id" sentinel, and the swap owns only
the subset meaning "this character has no location." The site count is also the wrong unit: **two**
public default arguments spell it (`combat_hooks.h:249`, `interpre.h:128`) and a **third** spells it
as a bare `-1` literal that escapes a `NOWHERE` grep entirely (`handler.h:392`'s
`extract_char(char_data* ch, int new_room = -1)`, ruling R-C4) — retiring it there is a *signature*
change, not a token substitution. Compounding it, this spec's (and every prior document's) "four
`in_room` fields" list is **incomplete: there are SIX location stores, two of them PERSISTED** —
`char_data::in_room`, `obj_data::in_room`, `shop_data::in_room` (a shop VNUM, not a location at
all), `char_data::was_in_room`, the persisted `char_special2_data::load_room` (R-A2/AM-1: it rides
*inside* `char_data::in_room` across an eight-window login/rent protocol, which is why LS-3a built
the VNUM channel), and `affected_type::modifier` under `SPELL_BEACON` (AM-3: a room **rnum** in the
save format, with its own range-based absence test and its own unguarded `char_to_room(ch, -1)`
path — no census named it until the T0 review).
**CORRECTED (a):** *After LS-3b, `char_data` carries no location field and no `NOWHERE` comparison
anywhere in the tree observes a **character's** location; every surviving `NOWHERE` is an
exit-graph, object-location, persistence, or scratch-room-id sentinel, enumerated in census C. The
two `NOWHERE` default arguments and the one bare `-1` default argument are each converted or
explicitly retained with a recorded reason. All six location stores are dispositioned — swapped,
left as-is, or migrated — and the two persisted ones (`specials2.load_room`, the `SPELL_BEACON`
modifier) are covered by an explicit save-format decision, because a room id in the save format
cannot be swapped by a code change alone.*

**(b) As written: "the `world[NOWHERE]` indexing hazard retires."** FALSE. The hazard is not in
`char_data` and the swap does not touch it: it lives in `room_data::operator[]` over the **room
table**, whose out-of-range fallback (mudlog, return `world[0]`, and it can `exit(0)`) every
existing call site was written against and observes. Strict equivalence — this program's owner-set
policy — positively *requires* preserving it. Retiring it is a separate campaign over ~274
`room_of()` and ~218 `EXIT()` sites that nothing currently scopes. LS-3a re-confirmed the same
constraint from the other direction: its conversions were forbidden to hoist a resolver call,
because collapsing N `operator[]` reads into one cached local silently drops N−1 mudlogs.
**CORRECTED (b):** *After LS-3b, no code path can reach `room_data::operator[]` with an index
derived from a **character's absent location** — absence is representable in the `LocationSystem`
without an integer that indexes the room table. `operator[]`'s out-of-range fallback is itself
PRESERVED byte-for-byte (strict equivalence requires it), and its retirement is explicitly OUT of
the LocationSystem program's scope; if it is ever wanted it is a separate campaign over the ~274
`room_of()` + ~218 `EXIT()` sites, scoped by its own census.*

Criteria 1 and 3 stand as written. Criterion 1 (the grep gate) is met, and the gate has been
strengthened three times since it shipped — see docs/BUILD.md's gate paragraphs.

### What LS-3a landed, and its ONE exception to zero behavior change

Mutation is routed: every bare `ch->in_room = X` outside the two allow-listed owner files
(`src/entity/placement.cpp`, `src/entity/containment.cpp`) now goes through `set_location()`, the
test tier's 828 masked representation sites collapse into a single allow-listed helper header, and
the `LocationReadCensus` gate's `src/tests` deferral is retired against a five-token, 307-file,
whole-tree scan. `char_data::in_room`'s VNUM overload — a persisted room VNUM riding in the location
field across the login/rent protocol, which LS-3b would have broken **silently** — is de-overloaded
behind a named `stash_load_room_vnum`/`peek_load_room_vnum` channel. Full account: docs/BUILD.md's
"Wave LS-3a" subsection, which also carries the enumerated LS-3b input list.

**The exception (owner ruling O-2):** LS-3a is NOT strictly zero-behavior-change, unlike LS-1 and
LS-2. The owner folded a set of location-correctness fixes into the VNUM tranche as a flagged rider:
four `save_char` call sites that persisted an **rnum** into a VNUM-typed save field, two async
walkers that read a location with no guard at all, and a rent-load `ITEM_LIGHT` bump that
incremented the **wrong** room's light counter on every rent-load of a lit item. Each landed
red-first or sabotage-proven; each is named in the wave summary's rider inventory. Goldens still
never regenerated: both boot goldens and the seed42 characterization golden stayed byte-identical at
every commit of the wave, and the rider set was chosen so that none of it is golden-observable.

### Reconciled chain

1704 → batch 0 `40bce59b` +0 → T1 `7fb741a8`+0/`e45363c2`+0/`242fef4e`+13/`c1c64497`+8/`f2b876bd`+11
= 1736 → T2a `43b3fdfc` +0 → T2b `ef0a6dfa` +0 → T2c `4980ba1d`+7/`0d0745a5`+0/`430035d3`+12 (+ docs
rider `551fa375`+0) = 1755 → T2d `385a4c62`+0/`01f2aac8`+0/`762c15a1`+4 = 1759 → T2e-alpha
`61e45680`+9/`f26f5af4`+8/`19ebffbe`+0/`5bc808b8`+2 = 1778 → T2e-beta `50b53a40`+7/`8c7f5936`+6/
`bb395562`+3 = **1794** → T3 (eight commits, `8e6b5972`..`68b3ff63`) +0 → T4 (`dd6ea5da`/`92a8c5a0`)
+0 = **1794**. Per-task: T1 **+32**, T2 **+58**, T3/T4 **+0** each (both are migrations — T3 froze
counts and assertions deliberately, and its non-vacuity evidence is 30-plus sabotage probes rather
than new tests). macOS-native monolithic single-process run: 1783 ran / 1708 passed / 75 skipped,
exit 0; 1794 − 1783 = the eleven CMake ctest-only checks (nine `*LayerAcyclicity` linkchecks +
`LocationReadCensus` + `LocationReadCensusSelfTest`). Per owner ruling O-4 the `rots64` leg, both
boot goldens, `make smoke-account` (still MANDATORY, ruling R-A2), the i386 battery, and the six-job
CI matrix all run ONCE at T6 finalization; their numbers are recorded there, not here.

## Wave LS-3b As-built

Branch `arch/ls3b-swap`, baseline master @`1c4f2e6` (**1801** tests — LS-3a + PR #26 + PR #27 +
the docs fold-back all merged, every finalization leg measured green per AGENTS.md's chain entry).
Full process record: `.superpowers/sdd/ls3b-global-constraints.md` (the kickoff seed + the T0
CLOSE-OUT with rulings O-5/O-6/O-7/R-3b-A/B/C/D), `ls3b-census-{a,b,c,d}.md` +
`ls3b-census-review.md` (T0 and its 34-amendment adversarial review — 5 BLOCKER/16 IMPORTANT/13
MINOR, three census recommendations overturned), and `ls3b-t{1,2,3,4,5,6,7,8}-report.md` (the
per-task landing record this section reconciles against). Those documents are authoritative for
byte-level detail; this section and docs/BUILD.md's "Wave LS-3b" subsection are the reconciled
summary.

### (a) O-6: a SECOND amendment to corrected criterion (b), owner-approved 2026-07-28

LS-3a's own R-C3 amendment (above) already corrected criterion (b) once. The census review's F1
found the corrected text still unsatisfiable inside this wave's own declared scope: `room_of()` is,
verbatim, `room_by_id_total(location_of(ch))` (`src/entity/placement.cpp:228-231`), and both T0
censuses independently required `location_of()` to keep its plain `int`/`NOWHERE` contract (the
`location_of` cost is the largest call-site population in the tree — 264 sites — and a
representation change there was never budgeted). Measured at HEAD: **279 production `room_of(`
call sites** and **264 production `location_of(` call sites**; every one of the 279 still reaches
`room_data::operator[](-1)` for an absent character, and the tree pins that fallback
*deliberately*: `RoomOfTest.ReturnsTheFallbackPointerForANowhereCharacter`
(`src/tests/placement_tests.cpp:603-617`) exists precisely to hold it. The corrected-once text —
"no code path can reach `room_data::operator[]` with an index derived from a character's absent
location" — is therefore false as written the moment `room_of()` is called on an absent character,
which is every existing call site's contract.

**Owner ruling O-6 (2026-07-28, decided in session), closing F1/R-3b-1:** criterion (b) is amended
a second time, to option (b1). Quoted verbatim from the ruling:

> *no code path can reach `room_data::operator[]` with an index derived from a character's absent
> location EXCEPT through `room_of()`, whose room-0 fallback is itself preserved behavior*

— consistent with criterion (b)'s own "fallback preserved byte-for-byte" clause.
`RoomOfTest.ReturnsTheFallbackPointerForANowhereCharacter` continues to pin it, unchanged by this
wave. Options (b2) (a nullable `room_of_or_null()` + a per-site campaign) and (b3) (a narrower
"only the stored representation" reading) were considered and rejected — both are a second wave's
worth of work the owner declined to fold into this one.

### (b) Criterion (a) satisfied under the spec's own private-handle option

Criterion (a), as corrected by R-C3, requires *"`char_data` carries no location field... observed
anywhere in the tree."* T5 privatized the three location-bearing members by rename rather than
deletion (`char_data::in_room` → `char_data::ls_location_id_`, `char_data::next_in_room` →
`char_data::ls_next_in_room_`, `room_data::people` → `room_data::ls_first_occupant_`) — the
program spec's own delegated "or keeps only a private handle" option. Ruling **R-3b-A** records the
consequence: criterion (a)'s "no location field" clause is satisfied under the reading **"no
location field OBSERVABLE outside the placement core"**, not "no location field exists at all."
T8 gave that reading a mechanical, compiler-enforced witness rather than leaving it a documentation
claim: three C++20 `requires`-expression concepts (`HasPublicInRoomMember<char_data>`,
`HasPublicNextInRoomMember<char_data>`, `HasPublicPeopleMember<room_data>`) plus three
`static_assert`s, landed in `src/entity/placement.cpp` (already a whole-file `LS1-ALLOW`
exemption, and `rots_entity` is an always-built target on every preset). The assertions are quiet
today (the public spellings do not exist) and become a **named, readable build error** — not merely
a build break — the instant a future edit reintroduces `char_data::in_room` or the other two public
spellings; T8 proved this with three independent decoy-member probes, each producing the expected
diagnostic before being reverted.

### (c) The O-5 amendment's landed rider inventory

O-5 chose option (b) — single store + a narrow, flagged amendment to strict equivalence — over
keeping two stores or guarding every call site. T5's inventory (`ls3b-t5-report.md` §2) landed 12
distinct observable deltas, each with its own test and sabotage proof, plus one correction to a
review finding (F20) discovered during landing:

1. **D1** — a `NOWHERE`-placed character is no longer in the fallback room's chain →
   `CharToRoomTest.NowhereLinksTheCharacterIntoNoRoomAtAll`.
2. **D2/D3** — the fallback room's light/zone-power counters no longer take a leaked bump →
   `CharToRoomTest.NowhereDoesNotBumpTheFallbackRoomsLightOrItsZonePower`.
3. **D5 retired** (extract-time use-after-free) →
   `ExtractCharTeardown.ExtractingANowherePlacedCharacterLeavesEveryRoomChainIntact`.
4. **D6/S8 retired** (chain truncation on re-placement) →
   `CharToRoomTest.NowhereDoesNotTruncateAChainTheCharacterIsStillPartOf`.
5. **F6** — menu sitters stop receiving MSDP entirely for an in-range stashed VNUM →
   `MSDPProtocol.MsdpUpdateSkipsAMenuSitterCarryingAnInRangeStashedVnum`.
6. **F19/R20** — menu sitters stop receiving another room's weather →
   `WeatherBroadcastGuard.SkipsAMenuSitterCarryingAnInRangeStashedVnum`.
7. **F19/R21 (a state-corruption fix)** — a mage in the login/rent window no longer has a live
   expose-elements spell cancelled by a search of an unrelated room →
   `CleanExposeElementsGuard.SkipsAMageCarryingAnInRangeStashedVnum`.
8. **R9 armed** — an anti-alignment rent-loaded item can no longer stay equipped on a forbidden
   wearer → `EquipCharZapGuard.AntiEvilItemLandsInInventoryWhenOnlyTheStashedVnumIsSet`.
9. **R10 armed** — same for race-restricted items →
   `EquipCharZapGuard.RaceRestrictedItemLandsInInventoryWhenOnlyTheStashedVnumIsSet`.
10. **R23 armed** — `save_char()` persists the stashed VNUM instead of `NOWHERE` when there is no
    `load_room` and no location, so the player returns to the room they left rather than their
    racial start room → `SaveCharChannelFallback.PersistsTheStashedVnumWhenThereIsNoLoadRoomAndNoLocation`
    (+ its negative control).
11. **LS-3a rider R12 becomes real** — `stat <char>` reports the stashed room VNUM rather than an
    unrelated room's number → `ActWizInspection.StatCharacterReportsTheStashedRoomVnumForACharacterInTheLoginWindow`.
12. **`store_to_char()` postcondition** — the character it fills in is explicitly nowhere →
    `LoadRoomPersistence.TextRoundTripPreservesWhateverIntegerTheCallerPassed` (strengthened).

**Rider 13, a correction discovered during landing, not part of the original 12:** review finding
F20 claimed the split removes a resolver call from the rent-load equipment path but leaves the
lighting *outcome* unchanged. Measured, that was false — the `value[3]` fuel/ON normalization lived
*inside* the one-term `location_of(ch) != NOWHERE` guard, which pre-split was really asking "does
this field carry anything at all," so left one-term the split would have silently stopped
normalizing every rent-loaded lamp. Fixed to the two-term shape
`(location_of(ch) != NOWHERE) || (peek_load_room_vnum(ch) != NOWHERE)` (the same shape as R9/R10),
pinned by `LoadRoomRider.RentLoadingALitLightBumpsOnlyTheRoomTheCharacterIsPlacedIn` +
`AttachEquipmentTest.LightSlotHasNoRoomEffectWhenCharacterIsNowhere`. Every one of the 13 deltas is
non-golden-observable: both boot goldens and the seed42 characterization golden stayed
byte-identical at every commit of the wave, satisfying O-5's binding condition.

### (d) The keying ruling (R-3b-A) and its evidence headline

The program spec left keying — shed the location fields entirely behind an external registry, or
keep a private handle — as a T0 ruling. The census review's Table 2 decided it for the private
handle (Option H): H is immune to K1/K2/K3 (freed-key reuse, address-reuse, the 542 test-tier stack
locals — all of which **hit** an external pointer-keyed map); K10 shows `location_of`'s 264 sites
and `room_of`'s 279 sites keep a field-load cost under H, where an external map's hash probe is an
unbudgeted perf risk on the tree's two hottest location-reading populations; K13 —
`occupants_from` survives for free; K8 is the codebase's own precedent, 1-for-1 against external
pointer-keyed registries (`specialized_mages` dangled until commit `7f4ffaa8`). Option D (a
destructor hook) was ruled DEAD outright: placement-new-over-live-object, copy-assignment, and a
hard `-Wdeprecated-copy-dtor` build failure under `-Werror`. Under the private handle, **no
unregistration mechanism is owed at all** — the store dies with the struct — which retires the
`free_char()`/`free_obj()` non-unregistration finding (LS-3a's own enumerated LS-3b input #1) as a
non-issue rather than a fix.

### (e) The cursor family: `ScopedRenderLocation` (ruling R-3b-B)

The census review's Table 1 found the definitive cursor inventory to be **35** production
`set_location()` call sites — not the 47 (a LINE count, not a site count) or 31 either prior census
claimed — decomposing to **28 cursor** + **4 `NOWHERE` sentinel** + **3 scratch-placement-with-a-
live-rnum**, of which **31 needed disposition**. T2 landed `rots::entity::ScopedRenderLocation`
(new `rots_entity` public header, `src/entity/include/rots/entity/render_cursor.h`) — an RAII
scoped cursor that captures the restore target, writes a spoofed room for the render window's
duration, and restores on scope exit — and converted all 28 cursor sites across 10 windows
(`act_othe`/`act_info`/`fight`/`ranger`/`mage`/`spec_pro`) onto it, including the death-cry window
that sits directly on the seed42 damage path (re-verified byte-identical). Of the 3
scratch-with-live-rnum sites: `act_wiz.cpp:1476`/`mob_csv_extract.cpp:224` convert as
byte-identical substitutions (the mob is provably never room-linked in the window, per
`read_mobile()`'s own unconditional `NOWHERE` re-stamp); `shapemob.cpp:268` (P3/P4/P8) — flagged by
the review as never restored and whole-struct-copied into `mob_proto[]` — was investigated
exhaustively (every `mob_proto[]` consumer, every `SHAPE_PROTO(ch)->proto` access, every
`character_list` splice site) and the stale live-rnum stamp proven unobservable in every path;
converted to `NOWHERE` and pinned by two characterization tests, one of which drives the real
`read_mobile()`. The cursor family's own persistence-spoof hazard (F15: a read taken inside the
window, including a capture into a persisted field, sees the spoofed room) is reproduced exactly,
not fixed — `ScopedRenderLocation`'s contract states this explicitly.

### (f) O-7: the SPELL_BEACON disposition and the `sh_int` arithmetic

`affected_type::modifier` (a `sh_int`) holds a room rnum under `SPELL_BEACON`, persisted via
`db_players.cpp:690`. **Owner ruling O-7 (2026-07-28)**: PRESERVE the rnum save format —
**permanently closed**, not deferred — because VNUMs run to 34999 and extension rooms begin at
`EXTENSION_ROOM_HEAD = 100000`, both routinely exceeding a `sh_int`'s range and both needing to fit
the same shared, polymorphic `modifier` field every other affect type also uses; a VNUM migration
is arithmetically impossible in this format, not merely inconvenient. The wave adds a **mandatory
two-sided range guard** at `mage.cpp`'s `ASPELL(spell_beacon)`: the WRITE arm now refuses to install
a beacon (rather than silently wrapping the modifier) when the caster's location overflows
`sh_int`, and the READ arm's existing high-side (`> top_of_world`) "beacon corrupted" guard gains a
low-side (`< 0`) term, closing the previously-unguarded `char_to_room(caster, -1)` path — exactly
the class of hazard O-5 exists to retire, found living in a second, independent location. Both arms
are sabotage-proven and pinned (`SpellBeaconTest.*`, 4 tests); today's overflow is recorded as a
defect being fixed, an O-2-precedent flagged rider, not a feature preserved.

### (g) The perf verdict

T3 built a payload-dereference-corrected, mutation-arm-augmented benchmark harness
(`src/location_benchmark.h`/`src/entity/location_benchmark.cpp`) per the census review's F14, and
captured a pre-swap baseline (`.superpowers/sdd/ls3b-perf-baseline.md`) with ratified thresholds
from a measured-variance study (5 independent runs, two 5-run boot-time batches). Every ratified
threshold was met after T5's split:

| metric | threshold | measured ratio |
|---|---|---|
| M1 occupant-chain iteration (`pointer_walk`/`payload_walk`, occ. 1/8/64) | ≤ 1.10× | 0.947×–1.060× |
| M2 lookup (`location_of_walk`/`room_of_walk`) | ≤ 1.10× | 0.923×–1.000× |
| M0 `place_then_remove_cycle` (occ. 1/8/64) | ≤ 1.25× | 0.672×–1.000× |
| M4 boot time (median of ≥5 runs) | ≤ 1.05× | 1.0088× |

Every correctness self-check (checksums, resolved-room counts) held before and after. M0's four
single-op sub-arms (`tail_insert`/`unlink_head`/`unlink_middle`/`unlink_tail`) remain
**advisory-only**, per the baseline's own finding that they are dominated by this host's ~41.7 ns
clock-tick quantization (peak-to-peak noise up to 97.6%) rather than being genuinely unstable
primitives — every post-split value still sits on the same tick multiples the baseline documented,
nowhere near an order-of-magnitude shift. No threshold was breached and no change in the
occupancy-vs-cost curve's shape was observed — the expected result, since the intrusive occupant
chain survives unmodified as the private handle's own internal implementation, so only constant
factors could move.

### (h) What this wave deliberately did NOT do

- **`obj_data::in_room` stays out of scope.** Object locations were never part of this wave's
  char-location charter (T0's scope section named this explicitly); the `obj-location` gate prefix
  keeps its full population (99 lines at T8's final audit), untouched.
- **`room_data::operator[]`'s out-of-range fallback is PRESERVED, not retired, and stays out of
  the program**, per O-6's amended criterion (b) itself: the fallback (mudlog + return `world[0]`)
  is the criterion's own explicit exception, not a gap this wave left open. Retiring it remains a
  separate campaign over the ~274 `room_of()` + ~218 `EXIT()` sites LS-3a's own R-C3 amendment
  already scoped out — nothing in LS-3b widens that boundary.
- **`obj_to_proto()`'s `object_list` registration gap** (LS-3a's enumerated input #2) is a
  pre-existing production defect outside this wave's char-only charter; it is restated, not fixed,
  for a future object-tier wave to pick up.

### Reconciled chain

1801 → T1 `dbb165be`+6/`712a0640`+6 (T1b `71d70fcc`+0) = **1813** → T2 `f49fa799`+8/`e23d78e6`+3/
`1787c7b9`+0/`c9c01016`+2 = **1826** → T3 `445e61a5`+4/`8d2cca8d`+0 = **1830** → T4 `47492483`+0/
`b56f1e56`+0 = **1830** → T5 `accccb95`+13 = **1843** → T6 `6c171d2f`+0 = **1843** → T7
`4c6e23ca`+4/`8b655c9a`+2/`d42c6e41`+0 = **1849** → T8 `5cd9ab5c`+0/`5b6bfa9b`+0/`61d8613d`+0 =
**1849**. Per-task: T1 **+12**, T2 **+13**, T3 **+4**, T4/T6/T8 **+0** each, T5 **+13**, T7 **+6**.
See AGENTS.md's Testing Guidelines chain entry for per-commit gate citations. Per the wave's
adopted O-4-precedent cadence, `rots64` at final HEAD, both boot goldens, `make smoke-account`, the
i386 battery, and the six-job CI matrix are **not yet measured** at this docs commit — they are
owed at T9 finalization. The controller ran two `rots64` spot-gates mid-wave (at T2's and T5's own
HEADs, both matching macOS exactly — `.superpowers/sdd/ls3b-controller-gate-log.md`), which is not
a substitute for the full finalization leg. No library-membership or `*LayerAcyclicity` change —
the nine linkchecks stay nine; the combat row stays DONE. This is the program's **second** wave
that is not strictly zero-behavior-change (owner rulings O-5/O-6/O-7), following LS-3a's O-2
precedent: every observable delta is named, tested, and confirmed non-golden-observable.
