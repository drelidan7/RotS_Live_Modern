# Room-resolve classification playbook

**Status: covers RR Wave R2 (small tiers — `src/entity/`, `src/pathfind/`,
`src/olc/`, `src/world/`) and RR Wave R3 (`src/combat/` + the dispatch-pattern
policy, TASK-001 + TASK-002).** R2 was the first wave to apply the R1-shipped
gate to real production rows; R3 was the first to add a proof KIND
(`dispatch-invariant`) and the first whose classification work required
production code changes to make the proofs true. Recorded factually from the
ACTUAL recipe each wave's tasks followed (R2: Task 0's mini-census, Task 1's
`entity`+`pathfind`+`olc` classification, Task 2's `world` classification
including three red-first `GUARDED` conversions; R3: three parallel censuses,
the policy design, the guards, four classification tasks and two repair tasks
the wave's own findings forced) — not a prescriptive design written in advance,
mirroring how
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

### `dispatch-invariant` (Wave R3, owner ruling R3-O-1)

The sixth and newest `PROOF_KINDS` entry, and the first whose evidence lives
ENTIRELY OUTSIDE the row's own function. It covers the pattern that dominates
`src/combat/` and `src/script/`: a body resolves the room of an actor
(`ch`/`caster`) it never validated, because the actor arrived through a
dispatch mechanism — a `cmd_info[].command_pointer` ACMD dispatch, the
`combat_command` table, a `skills[].spell_pointer` door, one of the two SPECIAL
invokers, the OLC `shape_center` fan-out, or the mob-AI driver. The proof is
that the DISPATCHER guards the actor: one tripwire per entry point, on the
actor argument, in the `db_world.cpp:2083` idiom.

**The kind is only sound while the entry set is CLOSED.** What actually closes
a ROW is mandatory citation part (iii) below — its exhaustive direct-caller
list, re-grepped and read by a human. The mechanical half is a BACKSTOP over
that, not a substitute: the entry points live in a marker-anchored
dispatch-entry registry in `docs/superpowers/room-resolve-ledger.md`, and
`tools/room_resolve_census.py --check` asserts both directions over it (every
registered guard literal must still be present in that function's masked body;
every occurrence of a pinned dispatch spelling must lie inside a registered
entry). Both directions are LINE-BASED, and the review-fix rounds measured
exactly where that bites: a slot spelling split across two physical lines, or
read in an expression position, IS caught (the bare-identifier tokens exclude
only the `(`, `[` and `)(` call forms), but the eight NAME-form tokens
(`activate_char_special(` and its siblings) have no bare twin, so a split
spelling or an address-of read called later through a copy slips past them.
So does a guard compiled out by `#ifdef ROTS_NEVER` — the gate blanks
never-true CONSTANT `#if` conditions (`0`, `00`, `(0)`, `0L`, `!1`, `1 - 1`,
`0 - 0`) but cannot decide an identifier one without a preprocessor. R3's own
dual whole-branch review and its re-verification demonstrated eleven evading
spellings between them and the gate was widened each time, which
is precisely the evidence for treating it as a backstop. Read the ledger's own
"`dispatch-invariant` proofs and the dispatch-entry registry" section before
writing one of these rows — this playbook records the recipe, that section is
the contract.

**The three mandatory citation parts. All three, or the row is not proven:**

1. **The registry entry** — the `file:line` of the guard that covers this row's
   actor, and (per R3-C-7 below) the SPECIFIC guard line, not merely the entry.
2. **The actor parameter, BY NAME.** The guard covers one argument. Ruling
   R3-C-5: the tree's `location_of(A) == location_of(B)` equality checks all
   pass when BOTH are NOWHERE, so a target-derived id inherits the caster's
   status and nothing more. A row whose id is `victim`-derived is classified on
   its own merits and says so.
3. **The body's exhaustive direct-caller list**, with every door that is NOT
   the registered dispatcher proven separately, or the row split. This is the
   part that fails most often — five of R3's eight stayed-`TODO` dispatch rows
   failed on it.

**Worked example, ACMD shape** (`src/combat/olog_hai.cpp · do_overrun ·
room_of(`, landed by R3's Task 3d and re-pointed by Task 1d):

> `for (auto* tmpch : rots::entity::occupants(room_of(ch)))` at
> olog_hai.cpp:565. (i) ENTRY: guarded at interpre.cpp:1175, the
> `command_interpreter` registry entry's PRE-DISPATCH tripwire. R3-C-7
> ADJACENCY: nothing but the `may_not_perform` test lies between that guard and
> the ACMD dispatch at interpre.cpp:1180... (ii) ACTOR: `ch` — the name the
> registry's own actor column carries... no victim/target id is read here, so
> R3-C-5 does not apply. (iii) DIRECT CALLERS: `do_overrun` is an `ACMD`
> (olog_hai.cpp:524) and its ONLY door is the `cmd_info` table, `COMMANDO(245,
> ... do_overrun, ...)` at interpre.cpp:1957 — re-grepped tree-wide at this
> commit: no `set_combat_command` cell, no direct call from any other body, no
> fn-ptr storage. INTERVENING RELOCATION: the site sits inside the `for (dis =
> 0; dis <= total_moves; dis++)` loop whose tail relocates `ch` through
> `issue_command(combat_command::move, ch, ...)` at olog_hai.cpp:572 — itself a
> registry entry guarding the same `ch` — and `do_move`'s relocation is the
> ADJACENT pair `char_from_room(ch); char_to_room(ch, to_room);` at
> act_move.cpp:807-808, which re-places on the very next statement, so no later
> iteration re-enters :565 unplaced.

What landing it took: a tree-wide re-grep for every one of the five ACMDs in
that file (each turned out to have exactly one door, its `COMMANDO` row), and
the intervening-relocation clause — a guard at dispatch proves nothing about
iteration 2 by itself.

**Worked example, ASPELL shape** (`src/combat/mystic.cpp · spell_poison ·
room_of(`, landed by R3's Task 2d):

> Both sites sit in the room arm `if (!victim && !obj &&
> !(caster->specials.fighting))` (mystic.cpp:1284) and resolve the CASTER's own
> room... (i) REGISTRY ENTRIES: `spell_poison` is entered only through
> `skills[].spell_pointer`; the rows cite `do_cast` (spell_pa.cpp:928) and
> `do_use` (act_othe.cpp:806)... THE APPLY_SPELL DOOR IS BLOCKED: the landed
> sites require `!victim`, and `affect_modify`'s APPLY_SPELL pair dispatches
> with `victim == caster == ch`, so that door cannot enter this arm...
> (iii) DIRECT CALLERS: one — `SPECIAL(snake)` at spec_pro.cpp:670, which
> passes `host->specials.fighting` as `victim` under the
> `host->specials.fighting &&` test at spec_pro.cpp:667, a NON-NULL victim, so
> the `!victim` room arm is unreachable from it and the row does not split.

What landing it took: reading each dispatch's ARGUMENT LIST to decide whether
the deliberately-unguarded APPLY_SPELL arm can reach the row's specific sites.
An ASPELL body is usually several mutually exclusive arms, and only some of
them are reachable from each door — this is what separates the six ASPELL rows
that landed from the ten that stayed `TODO` under `APPLY_SPELL-window`.

**R3-C-7: adjacency, and the `do_cast` cautionary tale.** A row inherits an
entry's guard ONLY if no statement between that guard and the dispatch can run
code with the actor as a participant. R3's Task 1b put `do_cast`'s tripwire at
`spell_pa.cpp:499`, near the top of the function, and justified it as
"straight-line within the one function, with no other entry into the body" —
which answers *entry*, not *relocation*. Task 2d landed six ASPELL rows on that
entry and flagged the gap in its own report. Task 1c then read `:499`..the
dispatch in full and found **two** statements that can move `ch`:
`complete_delay(ch)` at `spell_pa.cpp:516` (mainline — every prepared-spell
cast reaches it, and it re-enters `command_interpreter` at comm.cpp:2837 and
from there arbitrary spec procs) and `appear(ch)` at `:721`
(`affect_from_char` → `affect_total` → `affect_modify`'s APPLY_SPELL arm, an
arbitrary further ASPELL). It STOPped rather than write the rows. Task 1d's own
full read found a **third** the STOP had missed — `check_hallucinate(ch,
tar_char)` at `spell_pa.cpp:895`, ONE STATEMENT above the dispatch.

The lesson is the ruling: **do not close this by exhaustion.** Three readers of
the same 430 lines found two, then three relocating calls; a fourth reader
might find a fourth. The structural answer is ADJACENCY — the tripwire sits
IMMEDIATELY before the dispatch statement, with only scalar/local work between,
and an entry that must ALSO resolve or parse the actor earlier carries a SECOND
tripwire there. Three of R3's nine entries needed two
(`command_interpreter`, `do_cast`, `cast_mass_spell`); the registry's
`Guard literal` cell therefore holds a ` || `-separated list and `--check`
requires EVERY literal to be present, because a two-tripwire entry is only as
strong as its weakest.

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

## Pitfalls (measured from these waves' actual experience)

The first eight subsections are R2's; the ninth is Wave R3's own, and where
the two disagree about a rate, R3's is the one a dispatch-heavy tier should
plan against.

### The advisory/premise overturn rate (R2)

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
in-range justification (plus append-only room allocation — the half that is
easy to forget because it isn't a local runtime check at all, but a
program-wide invariant). **Cite it as the ledger's "The in-range half's
standing citation (R3-C-2)" section words it**, not as the `M-1
(placement.cpp:369-395)` boilerplate every R1/R2 row carries: that span is
`ScopedRenderLocation`'s banner and does not point at an in-range argument at
all (see Wave R3's pitfalls below). R2's landed rows are deliberately not
rewritten — the prose section supersedes their boilerplate — but no new row
should reproduce it. Every clean R2 row above states this explicitly (e.g.
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
every real call site of both macros — 5 `IS_SUNLIT_EXIT(` call sites
(act_info.cpp:1546/:1744/:1749/:1758/:1764) plus 1 `IS_SHADOWY_EXIT(` call
site (act_info.cpp:1555), 6 real call sites total, all inside
`src/app/act_info.cpp`'s `do_look`/`do_exits` `ACMD` bodies, all still
`TODO`. The ledger's own token-counts table's 6/2 totals additionally count
the two `src/utils.h` `#define` lines themselves (:511/:514, DECL rows, not
call sites) — the macros are defined once there and called from nowhere
else in the tree. **Neither R3 (`src/combat/`) nor
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

### Wave R3's own pitfalls (`src/combat/`)

R3 measured a much harsher advisory regime than R2 did, and hit six defect
classes R2 never saw. Ruling R3-C-6 states the headline: **in this tier
`--advise` is a search hint only.**

- **The advisory overturn rate is a coin flip, and the two censuses disagree
  about how bad.** Census A overturned **14 of 42** advisories (**33%**);
  census B overturned **11 of 14** (**79%**). R2's rate was 5 of 38 rows
  (≈13%). Budget a falsification read for every single advisory in a
  dispatch-heavy tier, not a sampling.
- **`occupant-loop?` routinely flags the SITE ITSELF as its own proof.** The
  recurring shape: the advisory points at an `occupants(room_of(ch))` loop that
  IS the resolver site in question — the loop CONSUMES the room this call
  resolves, so it cannot be its proof. R3 landed at least six rows that say so
  in their own text (`send_magic_room_message` :69, `spell_word_of_shock`
  :2033, `spell_flash` :689, `spell_terror`, `spell_reveal_life`,
  `spell_word_of_sight`). R2 had already seen this twice
  (`location_benchmark.cpp`); R3 shows it is the heuristic's dominant failure
  mode, not an occasional one.
- **`entry-guard?` names the nearest CASTER guard regardless of which id the
  site resolves.** `group_gain`'s row is the clean example: the advisory named
  a guard on `killer` while the site resolves `dead_man`'s room, and the
  :1265 TRANSFER step that actually makes the proof
  (`location_of(killer) != location_of(dead_man)` → return) appears in the
  advisory not at all. A `location_of` test near the site is not evidence that
  it is a test of the RIGHT variable.
- **Line drift across a wave is not an edge case — it is the default.** Every
  citation Task 1d checked was stale: the 19 ACMD rows' `interpre.cpp`
  citations by **+49**, the six ASPELL rows' registration citations by **+35**.
  Task 4's own pass then found more that Task 1d had missed or mis-diagnosed:
  `mystic.cpp` rows by **+32** (Task 2d's, written before Task 1d's insertion)
  and **+46** (Task 2p's, written before Task 1b's as well), `fight.cpp`
  citations inside OTHER files' rows by **+33** (Task 3p re-pointed its own
  rows and nobody else's), `act_othe.cpp` by **+14**, `mobact.cpp` by
  **+15/+16**, `spell_pa.cpp` by **+17**, plus four regression-pin test-file
  citations. **A task that inserts lines into a file must re-point every ledger
  citation into that file, not only the rows it wrote** — and the docs task
  must re-derive the lot anyway, because "unchanged" claims are themselves
  wrong (Task 1d asserted `mystic.cpp`'s cited lines sat above its insertion
  point; they sat below it, and it had missed a whole four-citation set).
- **Mixed-CRLF files silently explode a diff.** `src/combat/mage.cpp` (2545 of
  2558 lines CR-terminated) and `src/combat/mystic.cpp` (1896 of 1906) are
  MIXED. Editing either with ordinary Python text I/O rewrites every line
  ending and produces a whole-file diff; Task 1d hit exactly that and caught it
  only because its `clang-format` drift-line count moved the WRONG WAY
  (51 → 15) for an insertion. Edit these files at the byte level and check the
  CR count before and after. Relatedly, `cd src && make format` churns
  **126 files** tree-wide — the checked-in tree is NOT format-clean, so never
  run it casually inside a behavior-change task (Task 2p ran it once and
  reverted all of it).
- **A SPECIAL *host* is not the guarded argument.** `activate_char_special`'s
  parameters are `(char_data* character, char_data* victim, ...)` — the HOST
  first, the ACTING character second — and R3's tripwire guards `victim`. Any
  caller chain that passes a `host` (`SPECIAL(shop_keeper)`'s six direct
  `do_open`/`do_close`/`do_lock`/`do_unlock` calls in `src/app/shop.cpp`) is
  NOT covered, and cost three rows / ten sites that census B expected to drain.
  Check which parameter your chain actually carries before citing the entry.
- **The both-NOWHERE equality hole (R3-C-5).** `location_of(A) ==
  location_of(B)` passes when both are NOWHERE. This is not theoretical: it
  invalidated `spell_summon`'s victim half, it is why `different_zone` could
  not land, and it made `cast_mass_spell`'s first guard test VACUOUS until the
  fixture was rewritten to put both the caster and the group member at NOWHERE.
  Any proof leaning on such a comparison excludes exactly one real room, not
  the sentinel.
- **Re-derive a census's arithmetic from its own tables, not its own summary
  sentence.** Census A's summary said 11 rows / 16 sites were
  APPLY_SPELL-window-reachable; counting the sub-table it wrote gives **10 rows
  / 14 sites** (the eleventh row's own C-2 column reads "n/a"). The stayed-TODO
  TOTAL was unaffected, which is exactly why nobody noticed until Task 2d
  counted. The figure had by then been copied into the wave spec AND the ledger
  prose.
- **The in-range citation the whole program had been copying was wrong.** Every
  R1/R2 proof spells the in-range half "in-range via M-1
  (`placement.cpp:369-395`)"; that span is `ScopedRenderLocation`'s explanatory
  banner, not an in-range argument (census A's finding L-4, ruling R3-C-2). The
  honest citation names the WRITE side — `char_to_room` (`placement.cpp:548`,
  its unconditional resolve at `:564`) and `would_break_the_absence_invariant`
  (`:410-420`, enforced `:427`/`:440`) — plus append-only allocation, and says
  plainly that the in-range half is INHERITED from the write side, which this
  same program drains. **Cite the ledger's "The in-range half's standing
  citation (R3-C-2)" section rather than re-typing the wording**; R3 itself
  shipped two different spellings of it across four tasks and needed a docs
  pass to normalize all 54 rows onto one.

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

**Wave R3's three GUARDED rows add one rule: guard at the POINT OF USE, and be
willing to say the briefed shape is not implementable.** R3's brief specified
"ONE guard at `raw_kill`'s entry on `dead_man`'s placement" covering both the
`death_cry` and `get_corpse_desc` rows. Task 3p read it and refused: the
`get_corpse_desc` read is two frames below `raw_kill` behind a REQUIRED RETURN
VALUE (the corpse), so no guard inside `raw_kill` can reach it without a
signature change, and an entry-evaluated flag would already be STALE by
`fight.cpp:982` because `call_special`/`stop_riding`/the `affect_remove` loop
run in between. It landed one guard per function instead —
`if (was_in == NOWHERE) { return; }` at `fight.cpp:927` in `death_cry`, and a
leading `location_of(ch) != NOWHERE &&` term at `fight.cpp:567` in
`get_corpse_desc` (the `limits.cpp:813` idiom, which keeps the row's site count
at 3 so no token count moves) — each red-first, each with an IN-BODY POSITIVE
CONTROL (a PLACED character for whom the removed behavior must still happen)
that passed in the same red run, so neither assertion can pass vacuously. The
third R3 guard, `mage.cpp:944`'s `!fail &&` term in `spell_blink`, adds a
different lesson: it was deliberately made LINE-COUNT-NEUTRAL and applied at
the byte level, because `mage.cpp` is mixed-CRLF and because inserting an
explanatory comment block would have invalidated ~40 verified `mage.cpp:NNN`
ledger citations. The explanation went in the ledger row, the test header and
the commit message instead.

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

Wave R3 added three more and widened one. All four are also recorded in
`docs/superpowers/room-resolve-ledger.md`'s "Stayed-TODO taxonomy: the Wave R3
additions" section, because R3's rows cite them by name.

5. **`APPLY_SPELL-window`** (owner ruling R3-O-2). Rows reachable with an
   UNPLACED caster through `affect_modify`'s APPLY_SPELL arm
   (`entity_lifecycle.cpp:2440`/`:2442`) — the login/rent-load window, where
   `store_to_char` sets NOWHERE (`db_players.cpp:1403`) and calls
   `affect_total` on the next line, and `Crash_load` (`objsave.cpp:506`) does
   the same before placement. That arm is DELIBERATELY not a dispatch entry: a
   tripwire there would fire on every login of a character carrying an
   `APPLY_SPELL` affect, so it is pinned in the script's
   `DISPATCH_TOKEN_EXEMPT_SITES` with that reason instead. R3 left **10 rows /
   14 sites** here, all in `mage.cpp`, to be ruled together with the program's
   pending `RR-O-1` ruling — both are rulings about the same login window.
6. **`intervening-relocation`** (Wave R3 Task 1c). A row whose every door IS a
   registered dispatch entry, but where a statement between one entry's guard
   and the site can MOVE the actor, so the guard's conclusion does not survive
   to the site. Distinct from "the guard sits below the site" (that is textual
   order, and R3 closed its one instance by moving the guard); this one is
   about what runs in between. R3's single instance,
   `visibility.cpp · target_from_word · EXIT(` (1 row / 2 sites), survives even
   R3-C-7's adjacency answer, because its `do_cast` door
   (`spell_pa.cpp:624`) sits BETWEEN that entry's two tripwires. Closing it
   needs a third tripwire, which no ruling authorized.
7. **`owner-punted`** (owner ruling R3-O-4). `olog_hai::get_random_target`
   (1 row / 2 sites) has zero production callers; this repository's own
   dead-code heuristic would delete it, but the owner ruled it is NOT deleted,
   NOT proven, and NOT to be removed — intent unknown, disposition a future
   design decision. **A row can stay `TODO` because the owner said so**, and
   that is a category, not an omission.

**`scale-flagged` widened** (owner ruling R3-O-3): alongside R2's `CAN_GO`
(42 call sites) and `obj_to_room` (20), it now also covers
`visibility.cpp`'s `CAN_SEE` (the `:578`/`:580` half — the `:121` entry-guard
half drains, so the row SPLITS) and `get_char_room_vis`. These two carry a
compensating control while they wait, and it is worth copying: pinning their
NAMES as scanned tokens (the usual remedy for an id-taking function) would have
minted ~127 new ledger rows and raised the ceiling by the same amount for zero
proof value, so R3 pinned the tree-wide production CALLER COUNTS instead
(`PINNED_CALLER_COUNTS`, measured `CAN_SEE(` **86** / `get_char_room_vis(`
**41**), and `--check` fails on drift in EITHER direction.

**The rule underlying all seven categories: a refusal to classify is not a
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
| **Total (R2)** | **38** | **83** | T1+T2 | **4** (T1: `19c96b2a`+`f502c909`; T2: `4a7409d4`+`f9bd3d52`) | **2** | **+3** | **72** | **11** |
| `src/combat/` (**R3**) | 95 → 100 (5 splits) | 186 | T1a/T1b/T2p/T3p/T2d/T3d/T1c/T1d (+ T0 census, read-only) | 22 | **2 whole repair TASKS** (T1c, T1d) | +25 | 130 | 56 |
| R2-deferred riders (**R3**) | 6 | 8 | T3p (`weather_to_char` ×2, inside its own commit), T3e (OLC ×4) | 1 | — | 0 | 8 | 0 |
| **Total (R3)** | **101 → 106** | **194** | 11 task-units | **23** classification/production + 3 wave-admin + the T4 docs commits | **2** | **+25** | **138** | **56** |

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

### Estimation note: what R3 actually cost, and what that implies for R4

**R3 is now measured, so R4 should be planned from R3's rates, not R2's.** The
R2-derived projection that follows this block is kept verbatim as the
historical record — it is worth reading BECAUSE it was wrong in an instructive
direction, and the corrections are stated here first.

What R2's projection got right: it called the dispatch-pattern deferral "the
single biggest wildcard" and said designing that policy was a **prerequisite**
for R3 draining anywhere near its row count. Both held. It also predicted "on
the order of a dozen" advisory/premise corrections against R2's roughly five;
the two censuses together overturned **25** advisories (14 of 42 in A, 11 of 14
in B) before a single row was written, plus at least six further premise
overturns during implementation.

What it got wrong, and by how much:

- **It predicted R2's 13% stayed-TODO rate was "very likely a floor" for
  combat.** The measured rate is **56 of 186 sites, 30%** — more than double,
  and that is WITH the dispatch policy landed. The residue is not one thing:
  25% of it is the login window (`APPLY_SPELL-window`), 23% unproven extra
  doors, 34% medium-confidence refusals the wave's own no-medium-proofs rule
  forbade landing.
- **The dispatch share was the tier, not a slice of it.** 45 of 95 combat rows
  / 84 of 186 sites were DISPATCH-PATTERN at census time; `dispatch-invariant`
  ended up carrying **56 of the 138 sites this wave drained (41%)**, and it is
  now the second-largest proof kind in the whole ledger after `entry-guard`.
- **Fix rounds are not per-task review cycles at this scale; they are whole
  TASKS.** R2 budgeted "one fix round per task". R3 spent **two entire extra
  tasks** repairing its own policy — T1c (moving `command_interpreter`'s guard
  above `target_parser`, after T3d found it did not dominate) and T1d
  (implementing R3-C-7's adjacency answer after T1c STOPped on `do_cast`) —
  plus a citation-repair pass in the docs task. Budget a repair task, not a
  review comment.

For **R4 (`src/script/`, ~44 rows / 118 sites)**, R3's measurements say:

- The `dispatch-invariant` machinery is BUILT and R4 inherits it — registry,
  gate directions, self-tests, nine guarded entry points. R4's marginal cost
  for a dispatch row is a caller re-grep plus an adjacency check, not a policy.
- **Adjacency has almost no mechanical witness.** The registry's downward
  direction asks whether a guard literal is PRESENT, never whether it still
  runs BEFORE the dispatch; R3's review-2 demonstrated moving `do_use`'s guard
  below both its dispatches with `--check` still green. The one exception is
  `do_use` itself, whose two T5-fix literals quote guard-and-dispatch
  contiguously so the substring check doubles as an ordering check. Copy that
  shape when you add an entry, and lean on the per-entry TESTS for the rest.
- **TWO direct-door functions, SIX dispatch arms, are R4's — exempted rather
  than registered.** `complete_delay_impl` (`src/app/comm.cpp`) dispatches at
  `:2830` (`(*mob_index[ch->nr].func)(...)`, behind its `:2829` presence
  test), at `:2833` (through the `virt_program_number(...)` address fetched at
  `:2832`) and at `:2835` (`intelligent(...)`);
  `game_types::delayed_command_interpreter::run`
  (`src/app/delayed_command_interpreter.cpp`) does the same three ways at
  `:47` (address fetched at `:45`), `:51` (address fetched by
  `get_special_function(...)` at `:50`) and `:53` (`intelligent(...)`). Every
  one hands a spec-proc body an actor nothing validated. They carry seven
  pinned keys in `DISPATCH_TOKEN_EXEMPT_SITES` reading "M-4 direct door, R4
  design input"; registering them means writing a guard, which R3 did not.
  **The Task 5-fix round listed each function by its `mob_index[].func` arm
  ALONE** — two thirds of the real doors were missing from a list that ships
  as R4 design input, which the re-verification's M-5 caught and Task 5-fix2
  completed. When you inherit this list, re-derive it from the gate
  (`DISPATCH_TOKEN_EXEMPT_SITES` filtered to `category == "STOP"`), never
  from prose.
- **The known gap is the SPECIAL host.** `activate_char_special`'s tripwire
  guards `victim`, not `character`, and `src/app/shop.cpp`'s
  `SPECIAL(shop_keeper)` calls four ACMD bodies directly with a HOST. R3
  measured the fix at **one guard for 3 rows / 10 sites** — the cheapest
  remaining ratio in that tier, and squarely R4/app-tier shaped.
  `delayed_command_interpreter.cpp:47`/`:51` is a second host-passing door that
  bypasses both SPECIAL invokers entirely.
- Expect the `APPLY_SPELL-window` and `intervening-relocation` categories to
  recur, and expect the same advisory overturn rate: `src/script/` is
  `mudlle.cpp`/`mobact.cpp`/`spec_pro.cpp`/`spec_ass.cpp`/`script.cpp`, i.e.
  dispatch machinery all the way down.

---

### (Historical) Estimation note for R3 (`src/combat/`, ~95 rows / 186 sites) and R4 (`src/script/`, ~44 rows / 118 sites), written at the end of R2

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
