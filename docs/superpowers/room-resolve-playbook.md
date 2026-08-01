# Room-resolve classification playbook

**Status: covers RR Wave R2 (small tiers — `src/entity/`, `src/pathfind/`,
`src/olc/`, `src/world/`), the first wave to apply the R1-shipped gate to real
production rows.** Recorded factually from the ACTUAL recipe R2's tasks
followed (Task 0's mini-census, Task 1's `entity`+`pathfind`+`olc` classification,
Task 2's `world` classification including three red-first `GUARDED`
conversions) — not a prescriptive design written in advance, mirroring how
`docs/superpowers/combat-migration-playbook.md` records the combat-row
migration's actual recipe rather than its original plan. This playbook is the
one later waves (R3 — `src/combat/`, roughly 95 rows / 186 sites; R4 —
`src/script/`, roughly 44 rows / 118 sites; and eventually the `src/app/`
tier) should read before writing their own Task 0 census, per the wave-3
brief's explicit "write the classification playbook R3+ will reuse" charge.

Source material for every claim below: `.superpowers/sdd/2026-08-01-rr2-small-tiers/
{rr2-census.md,task-1-report.md,task-2-report.md,progress.md}` (the wave's own
actuals) and `docs/superpowers/room-resolve-ledger.md` (the landed rows,
quoted verbatim where cited).

## The recipe (census → read → verify → cite)

1. **Census, from the scanner's own `--advise` heuristic, verified by reading
   the whole enclosing function — not trusted as-is.** R2's Task 0
   (`rr2-census.md`) ran `tools/room_resolve_census.py --advise` over the 38
   in-scope rows, then read every flagged function in full (not just the
   scanner's cited line) before writing a proof draft. The advisory is a
   *heuristic pattern match* (it looks for a nearby `occupant-loop?`/
   `entry-guard?` shape), not a semantic proof — two of its own suggestions
   were overturned by the census author before the document was even
   finished (see "Pitfalls" below). Every `caller-contract` draft additionally
   required a **tree-wide caller grep**, not an assumption that the census's
   caller count was exhaustive.
2. **Read the enclosing function, and every caller, before drafting a proof.**
   A proof draft is not "the scanner flagged an `if` nearby, so entry-guard" —
   it is "I read the function, found the actual dominating condition (or lack
   of one), and traced every real caller tree-wide." R2's clean rows
   (`equipment.cpp`'s two guards, `db_world.cpp`'s `show_tracks`/
   `recalc_zone_power`) took this at face value and closed quickly; its hard
   rows (`get_char_room`, `get_sun_level`, `reset_zone`) required following a
   call chain two or three functions deep (a `SPECIAL` macro's parameter
   order, an `ASPELL`'s four independent dispatch-mechanism callers, a
   zone-load-time normalization pass) before the proof held.
3. **Verify against the census's own advisory before accepting it, and
   independently re-derive any count the census asserts.** The census's own
   `get_char_room` draft claimed "every one of the 8 tree-wide production call
   sites [to `special()`]" — Task 1's implementer independently re-grepped and
   used 8; the coordinator's proof audit re-grepped again and found the real
   number is **10** (see "Pitfalls: caller-contract enumeration discipline"
   below). Never accept a predecessor task's count without your own grep.
4. **Cite exact `file:line` for every dominating condition, guard, and
   caller — read the file at that line before writing the citation, not
   after.** R2's fix rounds caught off-by-one line citations in both tasks
   (see "Pitfalls" below); the discipline that would have prevented them is
   simple and cheap: run `grep -n` (or open the file) for the exact line
   *after* drafting the proof text, immediately before committing, and
   confirm each cited line still says what the proof claims it says.

## Per-proof-kind recipes, one worked example each

### `entry-guard`

The dominating condition is a runtime branch in the SAME function, textually
preceding the resolver call, that excludes the sentinel (`NOWHERE`) before the
resolver-reaching spelling runs. The proof must state **both halves**: the
sentinel exclusion (which line, which condition) AND the in-range
justification (M-1 + append-only allocation, or an equivalent).

**Worked example** (`docs/superpowers/room-resolve-ledger.md`, `src/entity/
equipment.cpp · attach_equipment · room_of(`):

> guarded at src/entity/equipment.cpp:267 `if (location_of(ch) != NOWHERE)`
> dominating `room_of(ch)->light++;` at :268 (a one-line dominating `if`,
> distinct from the outer two-term guard at :214 covering a different, wider
> block). Sentinel exclusion at :267; in-range via M-1 (placement.cpp:369-395)
> + append-only allocation.

Verification steps that landed it: the census flagged this row with an
advisory pointing at line 214 (a different, wider guard covering unrelated
code); Task 1 read the whole function and found the immediately-dominating
guard is actually a separate, narrower `if` three lines above the site
itself — the advisory's line number was directionally right (there IS a
guard nearby) but not the guard that actually dominates this specific call.
Reading the function in full, not just the flagged line, is what caught the
distinction.

### `loop-bound`

The resolver-reaching id is a loop induction variable (or a value derived
from one) whose invariant keeps it inside `[0, top_of_world]` for the
lifetime of the loop. The proof must state the loop's own bounds and, for
anything less trivial than a plain `for (i = 0; i <= top_of_world; i++)`,
walk the induction argument explicitly rather than wave at "standard binary
search."

**Worked example** (`docs/superpowers/room-resolve-ledger.md`, `src/world/
db_world.cpp · real_room · room_by_id_total(`):

> Tightened re-derivation (RR Wave R2 Task 2) of the rr2-census.md
> MEDIUM-confidence flag: the loop (db_world.cpp:1732-1757) maintains, by
> induction, `0 <= bot <= top_of_world` and `top <= top_of_world` at the top
> of every iteration... Every `room_by_id_total(mid)` call therefore receives
> `mid ∈ [0, top_of_world]`.

Verification steps that landed it: Task 0's census draft flagged this as
MEDIUM confidence specifically because "standard binary search" was accepted
without re-deriving the exact boundary arithmetic at the loop's termination
edge (`bot` becoming `top + 1`, or `top` going negative). Task 2 closed the
gap by explicit induction on both boundary cases — not by re-asserting the
general binary-search shape, but by proving the specific `mid ∈ [bot, top]`
invariant holds across every loop-body branch, including the two
termination-adjacent edges the census had specifically not re-derived. This
is the recipe's general lesson for `loop-bound` rows: "it's a loop with
`<= top_of_world`" is necessary but not sufficient when the loop's own
internal bookkeeping (not just its `for`-header) determines the bound.

### `caller-contract`

The resolver-reaching id comes from a caller argument, not a loop the
function owns or a same-function guard. The proof must enumerate every real
caller tree-wide (by direct grep, not by trusting a predecessor's count) and
either place each one in a caller-family bucket with its own dominating
argument, or — per the ledger's own caller-contract token-pinning policy —
pin the function's name as a scanned token so a future new caller becomes its
own new site rather than silently inheriting this proof.

**Worked example** (`docs/superpowers/room-resolve-ledger.md`, `src/entity/
placement.cpp · get_char_room · room_by_id_total(`):

> Corrects the mini-census's premise: per the `SPECIAL(cname)` macro
> (src/interpre.h:49-52), the parameter named `ch` inside a SPECIAL body is
> the SECOND parameter — the acting character dispatching the command
> (matching `special()`'s own `ch`) — not the special's `host` mob (first
> parameter). `special()` (interpre.cpp:1246-1264) normalizes `in_room` to
> `location_of(ch)` whenever passed `NOWHERE` and `return`s `FALSE` without
> ever reaching the room-occupant dispatch loop... when that normalized value
> is `NOWHERE`... Ten production call sites of `special()` exist tree-wide
> (re-grepped: act_offe.cpp:367, act_othe.cpp:1847, act_move.cpp:162/:342/
> :456/:460/:824, interpre.cpp:1098/:1101, act_comm.cpp:660); every one of the
> first nine omits the trailing `in_room` argument or passes `NOWHERE`
> explicitly... The sole call site passing a real, non-default `in_room` is
> act_comm.cpp:660..., which supplies a loop-bound room id and only runs
> after this same function already calls `room_of(ch)` unconditionally at
> act_comm.cpp:656, independently proving `ch` placed there too.

Verification steps that landed it, in order: (1) the census's own draft
misread which of `SPECIAL(cname)`'s two parameters is named `ch`, an error
Task 1's implementer caught by reading the macro definition directly rather
than trusting the census's paraphrase; (2) Task 1's implementer traced
`special()`'s own `NOWHERE`-normalization and early-return behavior against
its cited characterization tests, not just its source; (3) Task 1's
implementer counted 8 tree-wide callers of `special()`; (4) the coordinator's
independent proof audit re-grepped and found the real count is **10**, a
MODERATE finding fixed in the same fix round (see "Pitfalls" below) — the
underlying conclusion survived unchanged, but the enumeration itself needed
a second independent count before it was trustworthy.

### `dominating-resolve`

The resolver-reaching id was produced by a resolve (or an equivalent
always-valid construction, like a freshly-allocated room) strictly earlier in
the *same* function or the *same* call chain, with no intervening
detach/relocate. The proof must point at the specific earlier statement and
confirm nothing between it and the site in question could invalidate it.

**Worked example** (`docs/superpowers/room-resolve-ledger.md`, `src/world/
db_world.cpp · room_data::create_exit · room_by_id_total(`):

> `room_data::create_exit(int dir, int room, char connect)` has exactly TWO
> production callers tree-wide (re-grepped): (a) mage.cpp:1705,
> `room_of(caster)->create_exit(DOWN, crack)`, where `crack` is the fresh
> return value of `world.create_room(...)` two lines earlier — a
> newly-allocated room id, always valid by construction (dominating-resolve);
> (b) the function's own self-recursive call at db_world.cpp:1961,
> `room_by_id_total(room)->create_exit(rev_dir[dir], this_room, FALSE)`,
> where `this_room = real_room(number)` (:1938) finds the CURRENT room by its
> own vnum — always found (dominating-resolve). The function's own
> `if (room < 0) room = this_room;` at :1940-1941 additionally substitutes
> any negative input with the same always-valid `this_room`.

Verification steps that landed it: this row's proof reads cleanly because
BOTH of its exactly-two callers independently supply a provably-valid id
through two different dominating-resolve shapes (a fresh allocation, and a
self-find-by-vnum) — the work was tracing that there really are only two
callers tree-wide (re-grepped, not assumed) and that neither one's
dominating construction could plausibly fail.

### `occupant-chain` — zero R2 instances

R2 landed **zero** rows classified `occupant-chain`, and the whole ledger
(across R1 and R2 combined) currently has zero such rows too — this is not
an oversight, it is what the tier boundaries actually contain. `rr2-census.md`
states the reasoning explicitly for the one place it looked plausible:

> no in-scope site walks a room's occupant chain as its OWN proof basis in
> these 38 rows — the closest candidates, zone.cpp's 'L'-case occupant walks,
> prove their MEMBER's placement via occupant-chain but the resolver SITE in
> question is the room argument feeding the walk, not the walked member, so
> they classify as entry-guard/dominating-resolve on the room id instead.

The distinction that matters: `occupant-chain` proves a *character's*
placement (its `location_of()` is provably `!= NOWHERE`, and provably still
in range) by virtue of being reachable through a room's own occupant list —
it does not, by itself, prove anything about the *room id* that was resolved
to obtain that list in the first place. Every R2 site that looked
occupant-chain-shaped on first read turned out, on closer reading, to be
proving the room-id argument by some other kind (typically
`entry-guard`/`dominating-resolve`), with the occupant walk itself as a
*consumer* of an already-proven room, not the proof. A future wave's row
should only claim `occupant-chain` when the resolver call in question is
itself resolving a member drawn from a room's occupant chain (see the
ledger's own "Occupant-chain proof caveats" section for the two-half rule
that kind still needs — the contrapositive gives `!= NOWHERE` only, not
in-range, and the invariant's second half does not hold inside a
`ScopedRenderLocation` window) — R3/R4 should expect this kind to remain rare
outside `rots_entity`'s own placement machinery, not force a site into it
for lack of a better-fitting kind.

## Pitfalls (measured from this wave's actual experience)

### The advisory/premise overturn rate

Counting precisely from the three reports (not "at least 4" — the exact
tally): **5 distinct advisory or premise overturns** landed across the wave,
plus a further **3 citation-count/line defects** caught only in post-task
review audits (listed separately below, since those are citation-accuracy
fixes on an already-correct conclusion, not overturns of the conclusion
itself).

The 5 overturns:

1. **`get_sun_level`'s `occupant-loop?` advisory, overturned at Task 0
   itself.** `rr2-census.md`: "OVERTURNED (no occupant loop anywhere in this
   function; the advisory heuristic mismatched)."
2. **`location_benchmark.cpp`'s `measure_iteration` `occupant-loop?`
   advisory, overturned at Task 0 itself.** Same document: "OVERTURNED from
   advisory (the flagged occupant-loop at :241 is `occupants(room)` where
   `room` was already resolved earlier at :237; the resolve itself, not the
   loop, is the site in question)."
3. **`get_char_room`'s `SPECIAL(cname)`-parameter premise, corrected at
   Task 1.** The census assumed the SPECIAL body's `ch` parameter was the
   special's own host mob; Task 1 read the macro definition and found it is
   actually the acting player (the second parameter) — see the
   `caller-contract` worked example above.
4. **`reset_zone`'s 5-site GUARDED-candidate list, wrong in 2 of 5, found
   at Task 2.** The census's own illustrative citation (an
   OR-across-two-different-args guard) did not describe 2 of its 5 flagged
   sites' actual shape — both ('O'/'P') already carried a correct
   single-arg guard and needed zero code change, landing `PROVEN` instead of
   `GUARDED`.
5. **`reset_zone`'s case-6 (`:395`) root cause, re-diagnosed at Task 2.**
   The census described it as the same "malformed zone file" class as the
   other 4 unvalidated sites; Task 2 found `ZCMD.arg1` there is provably `==
   6` (the switch's own case selector, never routed through `real_room()` at
   load time at all) — a structurally different residual-risk shape
   (reading one of `create_bulk()`'s trailing dummy rooms if
   `top_of_world < 6`), guarded the same way but for a different reason.

The lesson: **treat every census advisory as a hypothesis to falsify, not a
conclusion to cite.** Roughly 1 in 8 of this wave's 38 rows (5/38 ≈ 13%)
needed a real correction beyond "read the function and confirm the
advisory" — high enough that skipping the falsification step on any
individual row would be a real risk, not a formality.

### The both-halves rule

Every `entry-guard`/`caller-contract` proof built on a `location_of()
!= NOWHERE` (or equivalent sentinel) test must state **both** halves: the
sentinel exclusion (which line, which condition — the trivial half) AND the
in-range justification (the ledger's M-1 precondition, `placement.cpp:
369-395`, plus append-only room allocation — the half that is easy to
forget because it isn't a local runtime check at all, but a program-wide
invariant). Every clean R2 row above states this explicitly (e.g.
`attach_equipment`: "Sentinel exclusion at :267; in-range via M-1
(placement.cpp:369-395) + append-only allocation.") — this is not
decoration, it is the ledger's own standing rule (see the ledger doc's "The
location_of() guard proves only the sentinel half" section), and a proof
that states only the sentinel half is incomplete even if its guard citation
is otherwise perfectly accurate.

### The two-room-macro rule

`IS_SUNLIT_EXIT`/`IS_SHADOWY_EXIT` each take **two** room-id arguments (the
current room and the adjacent room across a door) — the ledger's own
standing rule (design doc section 6/O-13) requires a row classifying either
macro's call site to state a proof covering **both** room-id arguments
separately; proving only one leaves the other's validity unaddressed. R2
landed **zero** rows for either macro — not because the rule doesn't apply
to this wave's tiers, but because neither macro is called from any of them.
A tree-wide grep (`grep -rn "IS_SUNLIT_EXIT\|IS_SHADOWY_EXIT" src`) confirms
every real call site of both macros — six `IS_SUNLIT_EXIT(` sites plus two
`IS_SHADOWY_EXIT(` sites, matching the ledger's own token-counts table —
lives inside `src/app/act_info.cpp`'s `do_look`/`do_exits` `ACMD` bodies,
all still `TODO`; the macros themselves are defined once in `src/utils.h`
and called from nowhere else in the tree. **Neither R3 (`src/combat/`) nor
R4 (`src/script/`) will hit this pattern** — it is exclusively an app-tier
(`do_look`/`do_exits`) concern, so the eventual `src/app/` wave, not R3 or
R4, is where a future implementer must budget the extra per-site work of
proving two room arguments instead of one per call site. Recorded here so a
future task doesn't have to rediscover the macros' call-site distribution
from scratch, and so nobody assumes R3/R4 need this recipe in their own
planning just because the vocabulary and the rule are already pinned in the
ledger doc.

### Caller-contract enumeration discipline

A `caller-contract` proof's enumerated caller count is a *claim*, and this
wave's own experience shows it gets re-derived and corrected by the next
reader, not rubber-stamped: `get_char_room`'s "8 tree-wide production call
sites" (Task 1's own independently-grepped count) was re-grepped by the
coordinator's proof audit and corrected to **10** — `act_offe.cpp:367,
act_othe.cpp:1847, act_move.cpp:162/:342/:456/:460/:824, interpre.cpp:
1098/:1101, act_comm.cpp:660`. The underlying conclusion (only one call site
passes a non-default argument) survived unchanged, but the count itself was
wrong until a second, independent grep caught it. **Any enumeration a proof
depends on — caller counts, site lists, the number of registered dispatch
targets — should be treated as re-derivable by a reviewer, and gotten
exactly right the first time**, not "close enough that the conclusion still
holds." A wrong count that happens not to change the conclusion this time is
still a defect a reviewer must catch, and costs a fix-round cycle either way.

### Off-by-one citation defects (recurring, caught in both task audits)

Both Task 1's and Task 2's own review-fix rounds caught wrong line numbers,
not wrong conclusions:

- Task 1's fix round: `can_breathe`'s two caller citations pointed at the
  wrong line (`limits.cpp:1016`, the function's *definition* line, instead
  of `:1022`, the actual call; `:1417`, a blank line, instead of `:1418`);
  `get_char_room`'s dispatch-loop citation pointed at `interpre.cpp:
  1349-1350` (a comment line and the `for`) instead of the real `:1351-1352`.
- Task 2's fix round left one line-range mislabel *unfixed and explicitly
  deferred*: `f9bd3d52`'s own amendment to the `reset_zone · room_of(`
  dominating-resolve row mislabels case-6's line range as `zone.cpp:410-424`
  (that's case 5's span; case 6 is actually `425-464`) — the load-bearing
  pin (`:451`) is correct, only the descriptive range is wrong, and
  `progress.md` records it as deferred to "the whole-branch fix wave" rather
  than silently left uncorrected.

**The rule this implies: verify every line number by reading the file at
that exact line before writing it into a proof, immediately before
committing — not from memory of an earlier read, and not by trusting a
predecessor task's citation.** This defect class is cheap to introduce
(files drift under intervening edits, and it's easy to cite the line you
remember instead of the line you just re-checked) and cheap to catch (one
`grep -n` or file-open per citation) — but it recurred in every task this
wave ran, so budget for it as a standing tax on every proof-writing pass,
not a one-off mistake to fix once and forget.

### Mixed-class key splits

A single `(file, function, token)` ledger key may cover sites that actually
need *different* proof kinds — the ledger's own documented allowance. R2
produced three real instances:

- `src/pathfind/graph.cpp · find_first_step · room_by_id_total(` (originally
  one `TODO` row, count 7) split into a `loop-bound` row (count 1, `:122`)
  and an `entry-guard` row (count 6, the remaining six sites) — the
  census itself recommended the split rather than forcing one kind.
- `src/entity/location_benchmark.cpp · measure_lookup · room_of(` (count 2)
  split into a `caller-contract` row (`:326`) and a `dominating-resolve`
  row (`:358`), since `:358` is dominated by this same function's own
  `char_to_room()` call three lines earlier while `:326` is not.
- `src/world/zone.cpp · reset_zone · room_of(` (count 2) split into an
  `entry-guard` row (`:398`/renumbered `:451`, dominated by an explicit
  `location_of(tmpmob) >= 0` in the same `if`) and a `dominating-resolve`
  row (`:442`/renumbered `:495`, dominated by the same invocation's own
  earlier placement).

**When a function's sites don't all share one proof shape, split the row —
do not force a weaker or stronger kind onto every site to keep one row.**
The ledger's own gate is fine with this (it verifies per-key site-count sums,
not per-row shape uniformity); the discipline needed is in the proof
authoring, not the tool.

### The same-function site-swap limit

This is a **reviewer-facing blind spot inherited from the ledger's own
design**, not something R2 discovered fresh, but R2's `reset_zone` row splits
are exactly the shape where it matters and is worth restating here: deleting
one already-`GUARDED` site from a function and adding a new, unguarded site
to the *same* function in the *same* edit needs no ledger edit at all if the
function's per-key site count for that token does not change. The gate can
only ever see aggregate counts per `(file, function, token)` key, never
individual call-site identity — this is why R2's reset_zone rows each state,
in their own proof text, exactly *which* line each classified site is (not
just "6 sites, entry-guard"), so a reviewer auditing the row can check each
cited line individually rather than trusting the aggregate count alone. Any
future row covering multiple sites in one function should follow the same
practice: name every site's line in the proof text, don't rely on the count
column to carry that information.

### Formatter-pass disclosure

Task 2's commit (`4a7409d4`) bundled a whole-file `make format` reformat into
the same commit as its three `GUARDED` behavior changes, without disclosing
that the reformat had happened. `progress.md` records the review's own
finding: "Minor (deferred): whole-file make-format reformat bundled
undisclosed into the behavior-change commit (verified idempotent/
pure-whitespace; disclosure gap only)." The reformat itself was harmless
(verified idempotent, pure whitespace) — the defect is purely that a
reviewer diffing a *behavior-change* commit had to first separate "real
edit" from "formatter noise" without being told which lines were which.
**Run `make format` as its own separate commit whenever a task's real edit
also needs formatting, or explicitly disclose in the commit message /
report exactly which lines are formatter-only** — never let a
behavior-change diff quietly include an undisclosed reformat pass.

## The GUARDED procedure

`GUARDED` is the ledger's own class for "this site cannot be proven safe as
written, so it needed a real code change" — a genuine behavior change, not a
proof-only reclassification. R2's three `reset_zone` guards (`zone.cpp:314/
:342/:448`, Task 2) are this wave's only `GUARDED` instances and establish
the procedure below.

1. **Red-first: write the test against the pre-guard code and confirm it
   fails, before writing the guard.** Task 2's `src/tests/
   zone_reset_guard_tests.cpp` (3 tests) ran against the unmodified
   `zone.cpp` and failed exactly as predicted, with the failure's root cause
   independently confirmed — not just "the assertion failed" but "the
   assertion failed because `room_data::operator[]`'s own negative-index
   mudlog fired," proof the test exercises the real degrade path rather than
   a fixture accident:

   ```
   [ RUN      ] ResetZoneTest.SkipsTheLCommandSettingLastMobWhenItsRoomArgFailedToResolveAtLoadTime
   2, Sat Aug  1 09:42:08 :: world[] called for negative room number.
   zone_reset_guard_tests.cpp:138: Failure
   Expected equality of these values:
     ((&candidate)->points.gold)
       Which is: 555
     0
   [  FAILED  ]
   ```

2. **State the absent behavior, not just the present one.** The census's own
   `NOWHERE-REACHABLE` findings format (`rr2-census.md`'s "NOWHERE-REACHABLE
   list with absent-behavior proposals" section) is the template: for a site
   that cannot be proven, state explicitly what currently happens (the
   degrade path — a mudlog plus a silent fallback read) and what the guard
   will make happen instead (an early skip, not a crash, not a silent
   continuation) — this is what turns "add a guard" from a vague intention
   into a testable, red-first-verifiable behavior statement before any code
   is written.
3. **Enumerate every GUARDED site as a flagged behavior-change commit, by
   name.** The ledger's `Kind` column for a `GUARDED` row is free text
   (unlike `PROVEN`'s closed `PROOF_KINDS` vocabulary) specifically so it can
   cite the wave and the exact sites changed: `RR Wave R2 Task 2
   (zone.cpp:314/:342/:448)`. This is not decoration — it is how a future
   reader (or a whole-branch review) finds every real behavior change this
   wave made without having to diff the whole commit against the ledger
   text.
4. **Boot-golden byte-identical requirement, when zone/boot-path code is
   touched.** `zone.cpp::reset_zone` runs at every zone reset, including
   boot — Task 2 ran `scripts/boot-golden.sh --native build/macos-arm64/
   ageland verify` and confirmed "boot log matches golden (338 zone resets
   exercised, byte-identical — confirms none of the three new guards fire
   against real world data)." **A boot-golden match after a `GUARDED` change
   to boot-path code is not optional evidence — it is the only proof that
   the new guard doesn't silently change behavior against the game's own
   real world data**, as distinct from the red-first tests' proof that it
   changes behavior correctly against a *malformed* fixture.
5. **Both-build-systems test wiring.** The new test file
   (`zone_reset_guard_tests.cpp`) was wired into both `src/CMakeLists.txt`
   and the flat `src/tests/Makefile` in the same commit — the standing
   two-build-system pattern every other test-file addition in this
   repository's history follows (see AGENTS.md's Testing Guidelines section
   for the general rule); a test wired into only one build system is
   invisible to whichever verification path doesn't use it.

## The stayed-TODO taxonomy

Every row a task chose not to drain must stay `TODO` with an explicit,
enumerated reason — **never silently left**. R2's 9 stayed-TODO rows (11
sites — see "Reconciling the exact stayed-TODO total" below for why the row
count and the site count differ) fall into four named categories:

1. **RR-O-1-blocked.** `src/entity/placement.cpp · char_to_room ·
   room_by_id_total(` (1 row, 1 site) is reached with `room == NOWHERE` **by
   design** — its own header comment states the resolve is preserved
   unconditionally, including for `NOWHERE`, to keep `room_data::
   operator[]`'s negative-room mudlog firing for that case. This is not a
   row nobody got to; it is the wave's F17/RR-O-1 site, structurally blocked
   on the program's own pending owner ruling about `operator[]`'s degrade
   paths (design doc's already-planned "delete the negative-resolve mudlog,
   replace with an abort" arm-flip). A faithful guard here would silently
   drop a load-bearing diagnostic, so the census's own absent-behavior
   proposal recommends explicitly NOT attempting one this wave.
2. **Dispatch-pattern deferral, awaiting the R3+ policy design.** 6 rows / 8
   sites rest on an implicit "a playing character issuing a command is
   always placed" invariant — the ACMD-argument-`ch` class
   (`src/olc/shapemob.cpp · do_shape · room_of(`, `src/olc/shapescript.cpp ·
   shape_center_script · room_of(`, `src/olc/shapezon.cpp ·
   extra_coms_zone · room_of(`, `src/olc/shapezon.cpp · shape_center_zone ·
   room_of(`, `src/world/weather.cpp · weather_to_char · OUTSIDE(`, `src/
   world/weather.cpp · weather_to_char · room_of(`). This invariant is
   **not** one of the five closed `PROOF_KINDS`, and the design doc itself
   is cited as explicitly NOT guaranteeing it at the command-dispatch layer
   ("`command_interpreter` checks position, not placement; the `NOWHERE`
   rejection lives only in `special()`"). An OWNER RULING (recorded in
   `progress.md`'s Task 0 entry) explicitly deferred formalizing this class
   to R3+/app-tier planning, with its own dedicated mini-census — R3/R4
   should expect to hit this pattern far more often than R2 did, since most
   of `src/combat/`'s and `src/script/`'s `do_*`/`ASPELL` bodies are exactly
   this shape (see the ledger's still-`TODO` rows for `act_move.cpp`/
   `act_info.cpp`/`mage.cpp` for dozens of examples already sitting in the
   ledger today).
3. **Scale-flagged.** `src/entity/environment_utils.cpp · CAN_GO ·
   room_of(` (1 row, 1 site) has **42 tree-wide call sites** — too many for
   a Task-1-scoped per-site or even per-caller-family enumeration to
   complete responsibly. The census flagged this STOP-*adjacent* (not a
   formal STOP, but deserving the same attention) and recommended a
   caller-family batch proof for a future task rather than 42 individual
   proofs; Task 1 did not attempt the full enumeration and left the row
   `TODO` rather than land a partial proof.
4. **Medium-confidence refusal.** `src/entity/containment.cpp ·
   obj_to_room · room_by_id_total(` (1 row, 1 site) has 20 real production
   call sites across three provenance patterns; Task 1 read all 20 and found
   most resolve cleanly, but several route through
   `prohibit_item_stay_zone_move`/`parse_container_for_stay_zone`
   (`fight.cpp`), whose own room-id provenance needs a further caller-chain
   trace Task 1 judged out of its remaining scope to complete to the
   ruling's own no-medium-confidence-proofs standard. **The rule the wave's
   plan states explicitly**: "Medium-confidence caller-contracts... T1/T2
   must fully verify (grep every caller) or leave TODO with reason — no
   medium-confidence proofs land." Task 1 chose the second option rather
   than land a proof it could not fully stand behind.

**The rule underlying all four categories: a refusal to classify is not a
gap in the work, it is a deliverable — every stayed-TODO row states, in the
task report and (for the taxonomy above) in this playbook, exactly which
category it falls into and why**, so a future reader never has to
re-diagnose "was this row skipped by accident or on purpose."

### Reconciling the exact stayed-TODO total

An early draft of this wave's stayed-TODO accounting undercounted, bracketed
as `[1 char_to_room + 5 OLC/dispatch + 3 weather_to_char]` — summing that
bracket gives 9, but it omits `CAN_GO` and `obj_to_room` (1 site each). The
precise, re-derived total (matching the task reports' own per-tier tallies:
Task 1 "8 sites stayed TODO," Task 2 "3 sites stayed TODO," 8 + 3 = 11) is:

- **9 ROWS** stayed `TODO`: `char_to_room` (1), the 4 OLC dispatch-pattern
  rows (1 row each), `CAN_GO` (1), `obj_to_room` (1), `weather_to_char`'s 2
  rows (`OUTSIDE(`/`room_of(`).
- **11 SITES** stayed `TODO` across those 9 rows: `char_to_room` (1), the 4
  OLC rows (2 + 1 + 1 + 1 = 5), `CAN_GO` (1), `obj_to_room` (1),
  `weather_to_char` (1 + 2 = 3). 1 + 5 + 1 + 1 + 3 = **11**.

The ceiling arithmetic uses the site count, not the row count (the ledger's
`MAXIMUM_TODO_COUNT` is a **site-sum** ratchet): 83 in-scope sites − 72
drained = 11 stayed `TODO`, matching 788 (pre-wave ceiling) − 72 (drained
this wave) = **716** (the ceiling `--check` itself reported after Task 2,
confirmed programmatically, never hand-computed). **This is exactly the
kind of miscount this playbook's own pitfalls section warns about — even an
early planning tally should be re-derived from the task reports and the
tool's own `--check` output before being repeated forward**, not copied at
face value into a later doc without independently re-summing it.

## Per-row cost table

| Tier | Rows (in-scope) | Sites (in-scope) | Task | Commits | Fix rounds | Test delta | Sites drained | Sites stayed TODO |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `src/entity/` | 16 | 31 | T1 | shared, see below | 1 | 0 | 28 | 3 |
| `src/pathfind/` | 4→5 (1 split) | 11 | T1 | shared, see below | 1 | 0 | 11 | 0 |
| `src/olc/` | 5 | 6 | T1 | shared, see below | 1 | 0 | 1 | 5 |
| `src/world/` | 13 | 35 | T2 | 2 (`4a7409d4`, `f9bd3d52`) | 1 | +3 | 32 (29 PROVEN + 3 GUARDED) | 3 |
| **Total** | **38** | **83** | T1+T2 | **4** (T1: `19c96b2a`+`f502c909`; T2: `4a7409d4`+`f9bd3d52`) | **2** | **+3** | **72** | **11** |

Additional measured figures: **5** distinct census advisory/premise overturns
(2 at Task 0 census-writing, 1 at Task 1, 2 at Task 2 — see "Pitfalls"
above); **3** further citation defects caught only in review-fix rounds (1
MODERATE caller-count miscount + 2 cosmetic line-number fixes, Task 1's fix
round) plus 1 reasoning-completeness finding (Important-2, Task 2's fix
round) and 1 still-deferred cosmetic line-range mislabel (Task 2, batched
into a future whole-branch fix wave); ceiling `788 → 716` (programmatically
re-derived from `--check`, never hand-computed, per the ledger's own
standing instruction); ctest `1862 → 1865` (the +3 `ResetZoneTest.*` cases);
ASan clean at the one task (T2) that touched a new test file; both boot
goldens byte-identical at every commit that touched boot-path code.

### Estimation note for R3 (`src/combat/`, ~95 rows / 186 sites) and R4 (`src/script/`, ~44 rows / 118 sites)

**This is a projection from R2's measured rates, not a fresh census — a real
Task-0-style census is still mandatory at the start of R3 and R4, and every
number below should be treated as a planning prior to falsify, exactly the
way this playbook's own "Pitfalls" section describes for a single row's
advisory.**

R2 landed 38 rows / 83 sites across 2 tasks (≈19 rows/task, ≈41.5
sites/task), 4 commits, 2 fix rounds (1 per task — every task in this wave
needed exactly one), +3 tests, and 5 advisory overturns (≈13% of rows).
Scaling those rates:

- **R3 (combat, 95 rows / 186 sites):** ≈5 R2-task-equivalents of row volume
  (95/19 ≈ 5) — very likely needs **multiple tasks**, not one, given the
  combat tier's sheer size (`mage.cpp` alone already contributes dozens of
  still-`TODO` rows spanning `spell_*`/`ASPELL` bodies visible in the ledger
  today). At R2's per-row overturn rate, expect on the order of a **dozen**
  advisory/premise corrections, not the roughly-5 R2 saw. At R2's
  per-task fix-round rate (100% of tasks needed one), budget a fix round for
  every task, not as a contingency but as the expected shape. **The
  dispatch-pattern deferral category is the single biggest wildcard**: combat
  is thick with `ACMD`/`ASPELL` bodies whose actor argument is exactly the
  "dispatched character is placed" shape this wave explicitly deferred to
  R3+'s own policy design — until that policy exists, expect a
  larger-than-R2-proportional share of combat rows to stay `TODO` under it
  (R2's rate, 11/83 ≈ 13%, is very likely a floor for combat, not a
  representative estimate) — this makes designing that policy a
  **prerequisite** for R3 draining anywhere near its full row count, not an
  optional nicety.
- **R4 (script, 44 rows / 118 sites):** ≈2.3 R2-task-equivalents of row
  volume, but a notably higher sites-per-row ratio (118/44 ≈ 2.68 vs. R2's
  83/38 ≈ 2.18) — expect **more mixed-class row splits** (the
  `find_first_step`/`reset_zone` shape) proportionally, and budget the
  citation-audit time that implies. `src/script/`'s own TU membership
  (`mudlle.cpp`/`mudlle2.cpp`/`mobact.cpp`/`spec_pro.cpp`/`spec_ass.cpp`/
  `script.cpp`, per `docs/superpowers/combat-migration-playbook.md`'s
  "driver homes with the engine" precedent) is also heavily
  dispatch-argument-shaped (mob-AI driver, spec-proc dispatch) — the same
  R3+ policy design combat needs will very likely gate a comparable
  proportion of R4's rows too.

Neither estimate should be used to set a ceiling target in advance of a real
census — they exist to help staff and sequence the work (how many tasks, how
many fix rounds to budget, whether the dispatch-pattern policy needs to land
before or alongside the first classification task), not to predict the
final drained/stayed-TODO split before the actual rows are read.
