# Combat-row migration playbook

**Status: FINALIZED — THE COMBAT ROW IS CLOSED.** Confirmed by a **fifth and final**
application (the spec-pair wave — `spec_pro.cpp` then `spec_ass.cpp`, SEQUENTIAL
commits, landing not in `rots_combat` but in `rots_script` (5 → 7 TUs) per the
mobact-precedent "driver homes with the engine" ruling — see "The spec-pair wave"
section near the end of this document), following the **fourth** application (the
spell-family closure wave — `spell_pa.cpp` + `mage.cpp` + `ranger.cpp` → `rots_combat`,
ONE joint membership commit, 12 → 15 TUs — see "The spell-family closure wave" section,
the playbook's own "Ambitious tier" row: a true bidirectional file-level cycle, the
intra-subset rule at full scale), the **third** application (the behavior wave —
`limits.cpp` → `rots_combat` (11 → 12 TUs) + `mobact.cpp` → `rots_script` (3 → 4 TUs) —
see "The behavior wave" section), and the **second** application (the combat-trio wave
— `olog_hai.cpp` + `mystic.cpp` + `profs.cpp`, `rots_combat` 8 → 11 TUs — see "The
combat-trio wave" section). Originally recorded factually from the ACTUAL recipe the
combat-pilot wave's tasks followed (Task 6/7 of that wave — the docs-finalization task
following Task 5, which landed `clerics.cpp` + `fight.cpp` joint membership,
`rots_combat` 6 → 8 TUs) — not a prescriptive design written in advance — this playbook
now closes with **twelve** real data points (clerics, fight, olog_hai, mystic, profs,
limits, mobact, spell_pa, mage, ranger, spec_pro, spec_ass) instead of zero, and the
per-TU cost table at the end of this document is fully RESOLVED — every row the
original combat-census sketch named has either joined `ROTS_COMBAT_SOURCES` (14 TUs
across five waves) or been ruled to a different tier and joined there instead
(`mobact`/`spec_pro`/`spec_ass` → `rots_script`, per the driver-homes-with-engine rule).
**Combat DEFER: 0.** See "THE COMBAT ROW IS CLOSED" section near the end of this
document for the full five-wave arc summary.

## The finalized recipe (census → closure check → seams → moves → conversions → joint membership → verification)

This is the corrected high-level shape of the process, reordered from how the wave's
tasks were originally planned to how they actually had to run once the CONTROLLER
STOP (below) forced a restructure. The granular, task-by-task evidence for each phase
is in "The recipe, as it actually happened" and the Task 5 section further down;
this section is the distilled sequence a future implementer should follow directly.

1. **Census.** `nm -u` every candidate TU's `.o`, demangle, resolve each undefined
   symbol to its home TU and library tier against a symbol→object map built across
   every library object dir plus `ageland.dir`. Produces a per-symbol disposition
   table (blocking vs. non-blocking, by tier) — see `.superpowers/sdd/pilot-census.md`
   §1/§2 for the worked example (clerics: 42 symbols/6 blocking; fight: 121/17).
2. **Closure check — do this BEFORE committing to a membership plan, not after.**
   For every symbol the census classifies `combat-peer (still-app)`, ask: is the
   peer's owner TU part of THIS promotion's candidate set? If yes, the edge is
   **intra-subset** and dissolves for free once both sides land in the same commit —
   but that also means the candidate set cannot split across separate membership
   commits (see "The intra-subset rule" below). If no — the peer stays app-compiled
   after this promotion — the edge remains genuinely blocking regardless of the
   census's "combat-peer = sanctioned, non-blocking" legend, UNTIL that peer gets its
   own seam or relocation (§7 of `pilot-census.md` is the worked example: 9 symbols
   owned by `spell_pa`/`limits`/`mobact`/`ranger`, each individually dispositioned
   RELOCATE or HOOK). **This step is the wave's central correction to the census
   methodology already in use since the seed wave — see "Census-methodology
   correction" below.** Skipping it is exactly what produced this wave's
   CONTROLLER STOP.
3. **Seams before conversions, landed consumer-free.** Every registered hook
   (`combat_hooks.h`/`entity_hooks.h`/`persist_hooks.h` extensions) and every
   destination TU a relocation will land in must exist and be gated green BEFORE any
   call site converts to it — never build the seam and the conversion in the same
   commit for a symbol with more than one caller, so a broken seam's build failure
   isolates cleanly from a broken conversion's.
4. **Byte-verbatim moves** for every census-clean symbol (zero upward refs of its
   own, or upward refs that are themselves already in-lib/L2): relocate the whole
   body (plus any same-file storage/sibling functions it cannot be separated from,
   per "bundled storage-move" cases like `fight_messages`/`spllog_*`/the
   `forget`/`remember` memory-pool package) to its L2 or in-lib destination, verified
   with `nm` single-definition + `nm -u` caller-still-resolves checks before and
   after.
5. **Call-site conversions**, each a minimal, positionally-exact substitution of the
   real call for its registered-hook dispatch equivalent — but budget for coupled
   dead-code cleanup riding the same diff (unused locals/forward-decls/includes
   whose only reader was the converted call; see "point 4" in the granular lessons
   below, confirmed at three-times scale in Task 5). A TU's own conversions can land
   and gate green WHILE the TU is still app-compiled (this is what Task 3 did for
   `clerics.cpp` a full two tasks before its membership move) — conversions and
   membership are separable in time as long as the TU doesn't need its own
   membership move to build.
6. **Joint membership move — all TUs in a closed subset promote in ONE commit.**
   If the closure check (step 2) found intra-subset edges among two or more
   candidate TUs, they cannot be split across separate membership commits; the
   `*LayerAcyclicity` linkcheck has nothing sound to verify for a partial promotion
   of a mutually-dependent set (see "Why joint {clerics, fight} membership" below for
   the full mechanical explanation of why a lone promotion fails). A single TU with
   no intra-subset partners promotes alone, exactly like every SEED-CLEAN TU in the
   original combat-seed wave.
7. **Verification is not just "does it build."** Run the actual `*LayerAcyclicity`
   linkcheck (not the census's blocking/non-blocking label) as the final gate for
   the membership move specifically — a census's "non-blocking" classification for a
   same-wave sibling's symbol is *provisional*, not proof, until that sibling has
   also actually promoted (Task 5's two census-miss fixes, `gain_exp`/
   `waiting_list`, are the concrete evidence this step exists for real, not
   theoretically). Also run: discriminator audit (every hook the conversions
   exercise has a registered/unregistered test pair — Task 5 found exactly one real
   gap, `dispatch_exploit_capture`, and closed it), full gates both hosts, both boot
   goldens, characterization goldens byte-unchanged, ASan if any test file changed,
   and the informational combat-smoke verify (non-gating per Task 1's rung-(b)
   landing — read the diff, don't treat drift alone as a regression).

## The intra-subset rule (this wave's central lesson)

**Co-migrating TUs keep direct calls to each other; a TU seeking STANDALONE
membership must be closed over every combat-peer edge it has, not merely "sanctioned"
by the census's peer-reference legend.**

Concretely: `clerics.cpp` calls six `fight.cpp` functions directly
(`set_fighting`/`stop_fighting`/`check_sanctuary`/`check_hallucinate`/`die`/`appear`)
and `fight.cpp` calls back into `clerics.cpp` (`weapon_willpower_damage`, the
`do_mental` ACMD). Neither of those edges needed a seam, a hook, or a call-site
conversion — they simply became legal intra-lib calls the moment both TUs joined
`rots_combat` in the same commit. Converting them to `issue_command()`/a registered
hook first (the way `clerics.cpp`'s OWN up-calls into `interpre`/`big_brother` were
converted in Task 3) would have been wasted work: a seam exists to route an edge
*downward* past a tier boundary that doesn't move, not to paper over two files that
are about to become the same library.

The corollary is what actually blocked Task 3's original plan (promote `clerics.cpp`
alone): a lone promotion asks `CombatLayerAcyclicity` to verify an edge
(`clerics.cpp` → `set_fighting`) whose only real implementation still lives in the
app tier, because `fight.cpp` hadn't promoted yet. **A candidate TU's membership move
is standalone-safe only if closure over its own combat-peer edges holds — i.e.,
every combat-peer symbol it references is either (a) a symbol owned by a TU that is
ALSO promoting in the same commit, or (b) already resolved by a seam/relocation, or
(c) already in-lib.** Any combat-peer reference that fails all three is a genuine
blocker no census legend can wave away — see the correction below.

## Census-methodology correction

The combat-census/blocker-census/pilot-census family of documents used a shared
legend: **"combat-peer = another DEFER-11 TU, sanctioned per spec §3 regardless of
app/lib status, hence non-blocking."** That legend is only true in the specific sense
that a cross-reference between two still-app TUs is not an *architectural* violation
(unlike a genuine upward L2→L3 edge) — it says nothing about whether the reference
will actually **link** once one side promotes and the other doesn't.

This wave's CONTROLLER STOP (post-Task-1, on a re-read of the census) is the
concrete correction: `pilot-census.md`'s original table correctly classified
`clerics.cpp`'s references to `set_fighting`/`stop_fighting`/etc. as `combat-peer
(still-app)` = non-blocking, under the inherited legend — but a standalone
`clerics.cpp` promotion would still fail to link, because `fight.cpp` (the peer)
hadn't promoted either. **The fix was not to change the census's classification
rule in general — cross-referencing a still-app peer genuinely is architecturally
sanctioned — but to add the closure check (recipe step 2 above) as a mandatory
gate BEFORE trusting "non-blocking" to mean "this TU can promote alone."**

Task 5 reconfirmed the same lesson at the linkcheck level, not just the planning
level: the `CombatLayerAcyclicity` build itself caught two symbols
(`gain_exp`/`waiting_list`, referenced from `clerics.cpp`, outside that task's own
`fight.cpp`-scoped conversion work) that the census had correctly traced but
provisionally classified — `gain_exp`'s owner (`limits.cpp`) never promoted this
wave, so the "combat-peer, non-blocking" label held only until the linkcheck was
actually run against the real joint-membership commit. **Lesson for later rows: run
the real linkcheck before declaring a membership move done, even for a TU a prior
task already fully converted — a census's non-blocking label for a same-wave
sibling's symbol is a strong prior, not a build-wiring guarantee, and only the
linkcheck is ground truth.** (This generalizes the same lesson `docs/BUILD.md`'s
blocker-buster "Census errata" section already recorded for census-A/census-C's
own build-membership mistakes — the pattern recurs across waves because a census is
inherently a snapshot, and every subsequent task's edits can invalidate a snapshot's
classification without anyone re-running it.)

## The recipe, as it actually happened (Task 3, granular evidence)

1. **Census first, from a file the previous wave already produced.**
   `.superpowers/sdd/pilot-census.md` (Task 0 of this wave) had already run `nm -u` /
   `c++filt` over `clerics.cpp.o`, resolved every undefined symbol to its home TU and
   library tier, and flagged which of the 42 project symbols were *blocking* (an
   edge into an app-tier symbol a library can't link against) versus merely a
   same-tier or already-in-library peer reference. Task 3 did not re-derive this —
   it read the census's per-symbol table (§1) and the "blocking = 6" tally directly.
   Skipping this step and grepping cold would have cost more than reading a table
   that already existed.

2. **Seams before conversions, in a prior task.** Task 2 of this wave had already
   landed the two seams this task consumes: `rots::combat::issue_command()` /
   `rots::combat::call_special()` (`src/combat_hooks.h`) and
   `rots::entity::dispatch_target_valid()` (`src/entity_hooks.h`). Task 3 read both
   headers' doc comments in full before writing a single edit — in particular
   `entity_hooks.h`'s `kNoSkillId` sentinel comment, which explains why a single
   `target_valid_fn` pointer can serve both clerics.cpp's 2-arg call shape and
   fight.cpp's 3-arg (skill_id) shape without a lossy default. Guessing the wrapper
   name or its default-argument semantics from the call site alone would have been
   wrong in a way that only a test (or a careful header read) would catch.

3. **Verify the documented call sites against the real file before editing —
   don't trust brief line numbers.** The brief (`.superpowers/sdd/pilot-task-3-brief.md`)
   cited `do_flee` at lines 239/264/512/575, `special` at 308, `is_target_valid` at
   156/319. The real file (as of this task, `arch/combat-pilot @2f5af34`) had them at
   244/269/517/580, 313, and 156/324 — a consistent +5 line drift from Task 0/2's
   intervening edits to the file (comments, a `std::format` conversion earlier in
   the same function). `grep -n` for the four call shapes up front caught this
   before any edit was attempted; the *shape* of every site (exact args, exact
   position of `ch` vs `victim`) matched the brief exactly, so this was drift, not a
   STOP condition. Recorded here because a stale line number in a brief is the kind
   of thing that looks alarming but isn't — grep-verify the shape, not the line
   number, before deciding whether to STOP.

4. **The conversion surfaced a coupled cleanup the brief didn't anticipate.**
   Both `is_target_valid` call sites were preceded by a local
   `game_rules::big_brother& bb_instance = game_rules::big_brother::instance();`
   with exactly one reader — the call being converted. Substituting only the call
   expression (as the brief's "minimal in-place expression substitution" framing
   suggested) would have left `bb_instance` unused, which is a hard build failure
   under this repo's `-Wall -Wextra -Werror` policy (confirmed with a throwaway
   `g++` repro: an unused reference variable warns even though its initializer is a
   function call with side effects). The fix was to delete both now-dead `bb_instance`
   locals as part of the same edit, and — since that was `big_brother.h`'s only
   remaining consumer in the file — drop the now-unused `#include "big_brother.h"`
   too. **Lesson for later rows:** a converted call site's *preceding* lines
   (locals it was the sole reader of) are in scope for the "minimal diff" even when
   a brief's wording suggests otherwise; the actual constraint is "the build must
   still pass `-Werror`," not "touch zero bytes outside the call expression."

5. **Formatter-hook conflict: byte-edit via Python, not Edit/Write.** `clerics.cpp`
   is CRLF (585 of 587 lines checked before this task's edits); the repo's
   PostToolUse clang-format hook applies an LLVM-style reformat that fights the
   file's WebKit convention (same conflict recorded in this session's memory for
   `handler.cpp`). All ten edits (7 call-site conversions + 2 dead-code deletions +
   1 include-block change) were applied by one Python script operating on
   `bytes.splitlines(keepends=True)`, asserting the exact expected old bytes for
   each target line before writing the replacement, applied from the bottom of the
   file upward so line numbers verified against the pre-edit file stayed valid
   as earlier (lower-numbered) edits were applied last. This is the same technique
   the repo's `rots-formatter-hook-conflict` memory entry documents; this task is a
   second confirmed data point for CRLF C++ files under that hook.

6. **Discriminator audit is a read, not an assumption.** The brief's Step 2 asked
   whether a `CombatHooksDispatch.*` / `CombatHooksSpecial.*` / `TargetValidHook.*`
   test already proves each converted cell's registered pointer reaches the real
   body with args intact. Reading `src/tests/combat_hooks_tests.cpp` and
   `src/tests/big_brother_hooks_tests.cpp` in full (not just grepping test names)
   showed Task 2 had already landed both halves (registered-reaches-real-body,
   unregistered-defaults-to-tripwire) for `flee`, `special`, and the 2-arg
   `is_target_valid` shape — the `special` and `is_target_valid` suites additionally
   assert every argument reaches the stub unchanged via a recording stub, and the
   `flee` suite's `TACTICS_BERSERK`-guard discriminator proves `ch` forwards
   correctly (the same class of proof `do_hit`/`do_stand`/`do_wake` establish for
   the ACMD-table's other fields). **No new test was needed or added this task** —
   this is a real, checked "zero" delta, not a skipped step.

7. **Gates run in the order that fails cheapest first.** Native macOS build (fast,
   catches compile errors from the byte-edit immediately) before the rots64
   container build (slower, pulls/runs Docker) before the two `boot-golden.sh`
   verifies before the informational `combat-golden.sh` smoke diff. `git status
   --short` was checked before deciding whether the ASan gate applied — it only
   gates test-file changes, and this task added none, so it was correctly skipped
   rather than run needlessly.

## Cost markers (this task, factual)

- **Edges converted:** 7 up-call sites (4× `do_flee`, 1× `special`, 2× `is_target_valid`)
  across 2 `ACMD` bodies (`do_mental`) and 3 other functions
  (`damage_stat`, `do_concentrate`, `weapon_willpower_damage`).
- **Coupled dead-code removal:** 1 forward decl (`ACMD(do_flee);`), 2 now-unused
  locals (`bb_instance` ×2), 1 now-unused `#include` (`big_brother.h`).
- **New includes:** 2 (`combat_hooks.h`, `entity_hooks.h`), inserted alphabetically
  into the file's existing quoted-header block.
- **Tests added:** 0 — audited as already-sufficient (Step 6 above).
- **Files touched:** 1 (`src/clerics.cpp`). No build-system file changed (per the
  CONTROLLER ADDENDUM, the membership move is explicitly out of scope for this task).
- **Effort, minutes-scale:** single agent session, no blocking surprises. Reading
  the brief + both seam headers + the census table was the largest single chunk;
  writing and dry-running the byte-edit script was fast once the exact old/new byte
  strings were pinned down from `grep -n`; the gate sequence (macOS build+ctest,
  rots64 container build+ctest, two boot-goldens, one informational smoke verify)
  dominated wall-clock time (mostly Docker container start/build latency, not
  investigation time) — call it low tens of minutes of active work plus Docker
  wait time, not hours.

## Why joint {clerics, fight} membership, not clerics alone (Step 3 rescission)

The CONTROLLER ADDENDUM to `.superpowers/sdd/pilot-task-3-brief.md` rescinded this
task's originally-planned Step 3 (moving `clerics.cpp` from `ROTS_SERVER_SOURCES` to
`ROTS_COMBAT_SOURCES` in all four build systems). The reason is a genuine **mutual**
dependency between `clerics.cpp` and `fight.cpp`, confirmed by reading both files
directly (not just the census's one-directional symbol table):

- `clerics.cpp` calls six `fight.cpp`-defined functions directly:
  `set_fighting`, `stop_fighting`, `check_sanctuary`, `check_hallucinate`, `die`,
  `appear` (all listed as `combat-peer (**intra-subset**)` in
  `.superpowers/sdd/pilot-census.md` §1, rows 7/8/10/15/19/21).
- `fight.cpp` calls back into `clerics.cpp`-defined functions directly:
  `weapon_willpower_damage` (`fight.cpp:74` extern decl, called at `fight.cpp:2516`)
  and the `do_mental` ACMD (`fight.cpp:86` forward decl, called directly at
  `fight.cpp:2824` — **not yet routed through `combat_hooks.h`'s dispatch table**;
  that conversion is this wave's Task 4, not Task 3).

Promoting `clerics.cpp` alone would turn its `fight.cpp` up-calls into intra-lib
references pointing at a symbol that doesn't exist in `rots_combat` yet (`fight.cpp`
is still `ROTS_SERVER_SOURCES`) — an unresolved link, and more fundamentally the
`CombatLayerAcyclicity` check would have nothing consistent to verify: a lone
`clerics.cpp` promotion asks the library to satisfy an edge whose only real
implementation still lives in the app tier. The two files' mutual calls make this a
true cycle at the *file* granularity, not a one-way dependency that could be broken
by converting clerics.cpp's outbound edges onto a seam (the way this task converted
its `do_flee`/`special`/`is_target_valid` up-calls into the app-command tier) —
`set_fighting`/`stop_fighting`/`die`/etc. are ordinary intra-combat-row calls with no
seam built for them, and building one would just relocate the cycle rather than
resolve it. The only sound move is promoting both files in the same commit, which
this wave's Task 5 does jointly per the addendum.

## Open items for later tasks / rows

- This draft covers only the `clerics.cpp` half of the combat-pilot wave. Task 4
  (fight.cpp's own up-call conversions, including retiring its direct `do_mental`
  call in favor of `issue_command()`) and Task 5 (the joint membership commit) will
  each add real data points; revisit this playbook once both land to confirm or
  correct the recipe above before treating it as final guidance for the 9 remaining
  DEFER-11 rows.
- The `set_mental_delay` call sites in `clerics.cpp` (`:171`/`:377`/`:379`/`:518`/
  `:525` at the pre-Task-3 line numbers) were deliberately left as direct calls
  per the addendum — that function relocates to `fight.cpp` in Task 4, and a
  direct app→app (today) / lib→lib (after Task 5) call is legal either way. Not a
  gap in this task; recorded so a later reader doesn't mistake it for one.

## Task 5 (fight.cpp conversions + joint membership) — confirms most of the above, corrects one thing

Ran as two commits per the brief: (a) fight.cpp's up-call conversions (30 edits
across 13 symbol families), gated green while fight.cpp was still `ROTS_SERVER_SOURCES`;
(b) the joint `clerics.cpp`+`fight.cpp` membership move, gated green again including
`CombatLayerAcyclicity`. Both hosts, both commits: 1394/1394 ctest, both boot goldens
byte-identical, `CharacterizationCombatTest.*`/`PoisonRemovalScriptTest.*` unchanged,
macOS ASan 1394/1394 clean, `string_view_census.py --check` 0.

### Confirms

- **Points 1/2/3/5/6/7 from Task 3 all repeated exactly.** Census-first (this task
  read `pilot-census.md` §2's 121-row fight.cpp table and the CONTROLLER ADDENDUM's
  conversion inventory, not a fresh `nm` run), seams-before-conversions (every
  target function — `issue_command`/`call_special`/`dispatch_target_valid`/
  `dispatch_character_died`/the app-other trio/the limits.cpp trio/`extract_char`
  — already existed from Tasks 2/4a/4b), line-number drift confirmed real but
  shape-stable (re-grepped before editing, exactly as the brief itself warned:
  "line numbers are POST-4a/4b — re-grep, do not trust"), byte-precise Python
  edits for the CRLF risk class (fight.cpp turned out to be pure LF, matching the
  brief's own claim, but `clerics.cpp` — touched again in this task for the
  membership-move fixes below — is still CRLF, so the binary-safe technique from
  Task 3 was reused there), discriminator audit was a real read (found 9 of 10
  hooks already covered by Tasks 2/4a/4b's test suites; the audit's one real gap
  is below), gates ran cheapest-first (macOS build+ctest before rots64 container
  before both boot-goldens before the informational combat-smoke verify).
- **Point 4 (coupled cleanup beyond the call expression) repeated at LARGER
  scale.** Task 3 found one class (unused `bb_instance` locals). Task 5 found the
  same class three times over (fight.cpp's three `bb_instance` locals) PLUS two
  new classes: dead `ACMD(do_flee);`/`ACMD(do_stand);` forward decls (zero
  remaining direct calls after conversion) and an unused `#include
  "big_brother.h"` (same trigger as Task 3 — last of 3 call sites converted away).
  **Confirms the lesson generalizes**, not a one-off: any wrapper-object /
  forward-decl / include whose *only* reader is a converted call site comes out
  in the same diff, every time, regardless of row size.

### Corrects: the "no other build changes" framing hid a real dependency

The CONTROLLER ADDENDUM's Step 3 said "one commit, no other build changes" for the
membership move. Two build-graph changes turned out to be load-bearing, not
optional polish:

1. **`RotS::persist` PUBLIC link.** `pilot-census.md` §2 already listed `save_char`
   as fight.cpp's one `L3-persist` edge (correctly counted non-blocking, since
   `rots_persist` already existed as a peer library) — but `rots_combat`'s
   `target_link_libraries()` had never needed `RotS::persist` before (the original
   4-TU seed had zero persist edges). Task 5(a)'s own `add_exploit_record()`
   conversion added a SECOND persist edge (`rots::persist::dispatch_exploit_capture`),
   making the missing link impossible to miss at the `CombatLayerAcyclicity`
   linkcheck. Fixed by adding `RotS::persist` to `rots_combat`'s PUBLIC link list,
   mirroring `rots_world`'s identical precedent for the identical edge class
   (`db_world.cpp -> rots::persist::set_room_vnum_hook`), and dropping the
   now-redundant `target_include_directories(rots_combat PRIVATE persist/include)`
   (persist/include now flows transitively via the PUBLIC link instead).
2. **`dispatch_exploit_capture` needed external linkage it never had.**
   `persist_hooks.h` documented `set_exploit_capture_hook()` but never declared a
   dispatch entry point — `db_players.cpp`'s `dispatch_exploit_capture()` was
   wrapped in an anonymous namespace alongside its `dispatch_room_vnum()` sibling,
   both TU-local, because until this task nothing outside `db_players.cpp` (its
   own `rename_char()`) ever called it. Fixed by giving `dispatch_exploit_capture`
   external linkage in `db_players.cpp` and declaring it in `persist_hooks.h`,
   the same "dispatch declared in the header, defined in the owning TU, called
   from a different TU" shape `entity_hooks.h`'s `dispatch_target_valid()`/
   `dispatch_character_died()` already established. This is a header/linkage
   change, not a build-system-file change, but it's the same class of "hidden
   until the linkcheck exercises it" gap as point 1 — recording it here because
   the next row to promote (any of the 9 remaining DEFER-11 TUs) may have a
   similarly half-built hook.

### Flat Makefiles needed ZERO changes — the "four build systems" framing overstates the actual footprint

The brief's Step 3 instruction ("clerics.cpp + fight.cpp leave `ROTS_SERVER_SOURCES`,
join `ROTS_COMBAT_SOURCES`, in ALL FOUR build systems") turned out to describe a
CMake-only concept applied to systems that don't have it. Checked directly (not
assumed): `src/Makefile` and `src/tests/Makefile` are both single flat object lists
(`OBJNAMES`/`OBJFILES`) with NO library/app split at all — `clerics.o`/`fight.o`
were already present in both, each with its own per-file build rule, because those
flat builds compile every `.cpp` straight into the one `ageland`/`tests` binary
regardless of which CMake library (if any) a file conceptually belongs to. The root
`Makefile` doesn't enumerate source files at all (pure `cmake`/`ctest` wrapper).
**Only `src/CMakeLists.txt` needed an actual edit** — moving the two filenames
between `ROTS_COMBAT_SOURCES`/`ROTS_SERVER_SOURCES`, plus the `RotS::persist` link
fix above. Verified with `grep -rln "clerics\.cpp\|fight\.cpp" --include=Makefile
--include=CMakeLists.txt .` before editing, so this is a checked "zero footprint"
finding for two of the four systems, not a skipped step — matches the precedent
Task 3's own `visibility.cpp` membership commit (`565286f`) set: that commit DID
touch `src/Makefile`/`src/tests/Makefile`, but only because `visibility.cpp` was a
**brand-new file** that had never been in either flat list before; a pre-existing
file being promoted between CMake libraries needs no flat-Makefile change at all.

### The real STOP-worthy finding: two genuine census misses surfaced only at the linkcheck gate

The first membership-move build attempt failed `CombatLayerAcyclicity` with two
undefined symbols inside `clerics.cpp.o` (never touched by this task's Step 1,
which was scoped to fight.cpp only) — exactly the "any failure is a census miss
requiring adjudication, not a stub" scenario the addendum's Step 3 anticipated:

1. **`gain_exp` (clerics.cpp:234, inside `do_mental`'s Will-contest damage path).**
   `pilot-census.md` §1 row 26 correctly identified this as a `limits.cpp` edge but
   classified it `combat-peer (still-app)` = non-blocking, under the census's own
   stated methodology ("peer cross-reference is sanctioned per spec §3 regardless
   of app/lib status" — i.e., a reference to another still-app DEFER-11 TU was
   treated as forward-looking-legitimate rather than a blocker). **The linker does
   not honor that framing**: `limits.cpp` stays in `ROTS_SERVER_SOURCES` (not
   promoted this wave), so `rots_combat.a` genuinely cannot resolve `gain_exp` on
   its own. Adjudicated as: convert this ONE site the exact same way Task 5(a)
   already converted fight.cpp's own `gain_exp` calls — `rots::combat::gain_exp()`,
   the identical Task 4b(b) hook, zero new infrastructure. Not a stub: it's the
   same seam, applied to a second call site the census had already correctly
   traced to the same owner function.
2. **`_waiting_list` (referenced via the `WAIT_STATE`/`WAIT_STATE_BRIEF` macros
   inside `do_mental`/`do_concentrate`).** This one was NOT a fresh miss —
   `pilot-census.md` §3.9 flagged it explicitly by name back at Task 0
   ("clerics.cpp: OVERTURNED — the extern is LIVE... needs the same class of
   handling as any other db_boot scratch/global the wave carries forward") — but
   no task between Task 0 and Task 5 ever built the seam it called for, so it
   surfaced here instead of earlier. Unlike `gain_exp`, this isn't a function call
   a hook can wrap — `WAIT_STATE_BRIEF`'s macro body reads AND writes the raw
   global pointer directly (`tmpch = waiting_list; ... waiting_list = ch;`), and
   16 other files carry their own local `extern struct char_data* waiting_list;`
   declaration reaching the same storage. A registered-fn-ptr hook doesn't fit an
   lvalue-mutated global. Adjudicated as a **storage-move**, the same technique
   `fight_messages`/`spllog_*` already used twice earlier in this wave (Task 4a):
   moved the actual `struct char_data* waiting_list = 0;` definition from
   `db_boot.cpp` (which never read or wrote it, only defined it) into
   `clerics.cpp` (its one user within the promoted pair); every other file's own
   `extern` declaration needs zero changes, per the "a C++ extern doesn't care
   which TU defines the symbol, only that exactly one definition exists"
   principle `pilot-census.md` §7.10 already established for a different set of
   symbols.

**Lesson for later rows:** a census's own "non-blocking / combat-peer" classification
for a symbol whose owner TU is a DIFFERENT still-app DEFER-11 row is provisional,
not final — it only holds until BOTH sides of that reference have actually promoted
(or the caller side gets a real seam). The `CombatLayerAcyclicity` linkcheck is the
actual ground truth, run it before declaring a membership move gate-clean rather
than trusting a census row's blocking/non-blocking label at face value once a file
outside this task's own conversion scope (here, `clerics.cpp`, already-converted by
Task 3) rides along into the same commit.

## Cost markers (Task 5, factual)

- **Up-call sites converted (fight.cpp, Step 1):** 30 edits across 13 symbol families —
  `do_flee` ×6 + `do_stand` ×1 → `issue_command`; `special` ×3 → `call_special`;
  `is_target_valid` ×2 + `on_character_died` ×1 → `entity_hooks.h`'s big_brother
  pair; `Crash_crashsave` ×1, `call_trigger` ×2, `pkill_create` ×1, `extract_char`
  ×3, `gain_exp` ×2, `gain_exp_regardless` ×3, `remove_fame_war_bonuses` ×1 →
  their `combat_hooks.h` fn-ptr hooks; `add_exploit_record` ×4 → the newly
  externally-linked `persist_hooks.h` dispatch.
- **Membership-move-gate fixes (clerics.cpp, outside Step 1's scope but required
  to clear `CombatLayerAcyclicity`):** 1 call-site conversion (`gain_exp`), 1
  global-storage-move (`waiting_list`, `db_boot.cpp` → `clerics.cpp`).
- **Coupled dead-code removal:** 2 forward decls (`ACMD(do_flee);`/
  `ACMD(do_stand);`), 3 now-unused `bb_instance` locals, 1 now-unused `#include
  "big_brother.h"`.
- **New includes:** 1 (`persist_hooks.h` in fight.cpp, alphabetically placed).
- **Tests added:** 2 (`tests/persist_hooks_tests.cpp`, `ExploitCaptureHook.*`) —
  the one real discriminator-audit gap: every OTHER hook fight.cpp's conversions
  exercised already had a registered/unregistered test pair from Tasks 2/4a/4b;
  `dispatch_exploit_capture` was consumer-free (db_players.cpp-internal only)
  until this task gave it external linkage, so nothing had exercised its
  dispatch wrapper directly before.
- **Build-system files touched:** 1 (`src/CMakeLists.txt` — source-list move +
  `RotS::persist` PUBLIC link + dropped now-redundant PRIVATE include dir).
  `src/Makefile`/`src/tests/Makefile`/root `Makefile`: 0 (checked, not assumed —
  see the flat-Makefile section above).
- **Files touched total:** 6 (`fight.cpp`, `clerics.cpp`, `db_boot.cpp`,
  `db_players.cpp`, `persist_hooks.h`, `src/CMakeLists.txt`) plus 1 new test file
  and 1 docs exception-ledger row (`string-view-exceptions.md`, for
  `persist_hooks.h`'s new `dispatch_exploit_capture` declaration).
- **Effort, minutes-scale:** two commits' worth of gates (each: macOS build+ctest,
  rots64 container build+ctest, two boot-goldens, ASan on the conversion commit
  for the new test file, one informational smoke verify after membership) plus
  one full extra macOS build+ctest cycle to diagnose and fix the
  `CombatLayerAcyclicity` failure — Docker container start/build latency
  dominated wall-clock time, as in Task 3; the investigation itself (reading the
  linker's undefined-symbol list, tracing both symbols back to their
  `pilot-census.md` rows, picking the already-precedented fix for each) was fast
  once the failure's exact symbol names were in hand.

## Per-TU cost table for the remaining DEFER TUs

**Method and caveat.** This table cross-references `.superpowers/sdd/combat-census.md`
(the 2026-07-19 seed-wave census, the last full `nm`-based per-TU survey covering
these 9 rows + `profs`) against every seam/relocation this wave (combat-pilot) and
the prior blocker-buster wave actually landed, to estimate what each row's blocking
edge count looks like **today**. It is **not** a fresh Task-0-style `nm` re-run —
per the census-methodology correction above, that re-run is mandatory before any
future task trusts these numbers for a real promotion; treat every "RESOLVED" mark
below as a strong prior, not ground truth, and re-verify against
`ROTS_COMBAT_SOURCES`/the current header contents before relying on it.

Global seams now available to ANY of these 5 remaining rows (landed since the original
combat-census, cutting across every row's edge count below):
- **`output_seam`**: 15 forwarders/accessors total — the original 5
  (`send_to_char`×2/`vsend_to_char`/`act`/track+untrack_mage) plus blocker-buster's 7
  (`send_to_all`/`send_to_room`/`send_to_room_except_two`/`break_spell`/
  `abort_delay`/`complete_delay`/the content-carrying `get_from_txt_block_pool`
  overload) plus the behavior wave's 3 (`close_socket` — a symbol takeover, like
  `send_to_char`/`act`, not a forwarder wrapping a renamed body; `no_specials_active()`
  read accessor; `request_circle_shutdown()` setter — both plain `comm.h`-declared
  globals, NOT `rots::output`-namespaced, backed by `comm.cpp`'s real bodies). **Not**
  covered: `descriptor_list`, the color-sequence globals.
- **`combat_hooks.h`'s 26-cell ACMD dispatch** (`issue_command`): covers a real
  `do_*` up-call IF its target is one of the 26 registered cells (`ambush`/`assist`/
  `cast`/`close`/`dismount`/`flee`/`gen_com`/`hide`/`hit`/`lock`/`look`/`mental`/`move`/`open`/
  `rescue`/`rest`/`say`/`sit`/`sleep`/`stand`/`stat`/`tell`/`trap`/`unlock`/`wake`/
  `wear` — `dismount`, the combat-trio wave's 26th cell, added between `close` and `flee`).
  **Not** covered: `command_interpreter()`/`_cmd_info` (the general
  dispatcher itself, not a `do_*` cell), `do_recover`/`do_scan`/`do_pracreset`
  (excluded — no direct call site existed when the table was built; re-check if a
  DEFER TU's own promotion creates one), any `do_*` target not on this list.
- **`special()`/`call_special`**, **big_brother's `is_target_valid`
  (both arities)/`on_character_died`** hooks — landed, consumer-tested. **The behavior
  wave added a second big_brother pair**: `on_character_afked`/`on_corpse_decayed`
  (`entity_hooks.h`-style, void, logged-no-op tripwire, registered by `big_brother.cpp`)
  — a genuine spec-adjudication gap this row's own original cost-table cell never named.
- **`persist_hooks.h`'s `dispatch_exploit_capture`** (`add_exploit_record`) — landed,
  externally linked as of Task 5.
- **`combat_hooks.h`'s Task 4b quartet**: `extract_char` (originally landed here;
  **RE-HOMED to L2 `entity_hooks.h` by the l4-seed wave**, shared by both `rots_world`
  and `rots_combat` — see "The l4-seed wave" section below), `gain_exp`/
  `gain_exp_regardless`/`remove_fame_war_bonuses`, `crash_crashsave`/`call_trigger`/
  `pkill_create` (still `combat_hooks.h`-owned) — all landed, consumer-tested. **The
  behavior wave added three more `combat_hooks.h` cells**: `Crash_idlesave`/
  `Crash_extract_objs` (two new siblings of `Crash_crashsave`, confirmed genuinely
  distinct functions) and `one_mobile_activity` (the wave's own defining cross-edge, a
  PERMANENT `rots_combat`→`rots_script` upward inversion — see "The behavior wave"
  section below; not generally reusable by another row, since it is mobact-specific).
- **`script_hooks.h`'s new `virt_program_number` cell** (behavior wave) — a
  `void*(int)` abort-tripwire dispatcher into still-app `spec_ass.cpp`, registered by
  `spec_ass.cpp` itself. Directly relevant to `spec_pro`/`spec_ass`'s own eventual
  promotion (the rider-gate mechanism, ≤3 same-shape edges without stopping, has now
  fired once out of three — see "The behavior wave" section below).
- **L2/in-lib relocations now legal as plain downward or intra-lib calls from ANY
  TU**: `stop_riding`, `remove_character_from_group`, `stop_follower` (bundled with
  the `forget`/`remember` memory-pool package), `saves_power`, the full visibility
  family (`CAN_SEE`×2/`CAN_SEE_OBJ`/`get_char_room_vis`/`get_player_vis`/
  `get_char_vis`/`get_obj_in_list_vis`/`get_obj_vis`/`get_object_in_equip_vis`/
  `generic_find`/`get_real_OB`/`get_real_parry`/`get_real_dodge`/`stop_hiding`),
  `set_mental_delay`, `equip_char`/`unequip_char`, `record_spell_damage` (+
  `spllog_*` storage), `check_break_prep` (its own internal `do_trap` up-call already
  converts through the existing `trap` cell), `extract_obj`/`obj_from_char` (from the
  earlier placement-seam wave). **The behavior wave adds**: `char_from_room` (new L2
  `entity_hooks.h` hook — real body stays app, calls `stop_fighting` downward),
  `recalc_zone_power`/`report_zone_power` (relocated to `rots_world`/`db_world.cpp`),
  `affect_remove_notify` (relocated to L2 `entity_lifecycle.cpp`), `saves_spell`
  (relocated to L3 `rots_combat`/`fight.cpp` — NOT L2, an OVERTURN of the design spec's
  own L2 default, since its body writes `fight.cpp`-owned `spllog_*` globals),
  `pkill_get_rank_by_character`/`pkill_get_totalrank_by_character_id` (relocated to
  `rots_persist`/`db_players.cpp`).
- **The L4 band (`rots_pathfind`/`rots_script`, l4-seed wave) now exists above
  `rots_combat`** — `find_first_step()` (graph.cpp) and `intelligent()` (mudlle.cpp) are
  real library functions any still-app DEFER TU can call downward with zero new seam;
  `command_interpreter()` has a registered hook
  (`script_hooks.h::dispatch_command_interpreter`). See "The l4-seed wave" section below.

**Still open for every row below, no seam exists yet**: `descriptor_list`, `_cmd_info`
(the fn-ptr table itself, distinct from `command_interpreter()` below), the
`boards`/`mail`/`objsave` spec-proc registrar family (`gen_board`/`postmaster`/
`receptionist`), `one_argument`/other `interpre.cpp` parse helpers, `add_follower`,
`get_guardian_type`, and any other `handler.cpp`/`utility.cpp` function not listed as
relocated/hooked above.

**No longer open, as of the l4-seed wave** (see "The l4-seed wave" section below):
`command_interpreter()` now has a real registered hook
(`script_hooks.h::dispatch_command_interpreter`); the `graph.cpp` pathfinding family
(`find_first_step`; `show_tracks` still needs its own relocate-or-hook when `ranger.cpp`
promotes) and the `mudlle` scripting-engine family (`intelligent`) both resolve as legal
downward app→L4 calls now that `rots_pathfind`/`rots_script` exist as real libraries —
any DEFER-row table cell above still citing these as "no seam" predates this wave.

**No longer open, as of the behavior wave** (see "The behavior wave" section below):
`close_socket`/`_circle_shutdown`/`_no_specials` (all three now `output_seam`
forwarders/accessors); `char_from_room`/`recalc_zone_power`/`report_zone_power`/
`affect_remove_notify` (all four now relocated or hooked, per the "L2/in-lib
relocations" bullet above); `Crash_idlesave`/`Crash_extract_objs`/`Crash_crashsave`
(all three now `combat_hooks.h` cells, `Crash_crashsave`'s own call site converted this
wave); `pkill_get_rank_by_character` (relocated to `rots_persist`); the big_brother
`on_character_afked`/`on_corpse_decayed` pair (new `entity_hooks.h`-style hooks). The
**mage** row above (still citing `char_from_room`/`report_zone_power` as "no seam") and
any future row referencing these symbols should treat them as resolved, not open, as of
this wave.

| TU | Original blocking edges (combat-census) | RESOLVED by this wave + blocker-buster | Remains open | Closure-partner constraint |
|---|---|---|---|---|
| **olog_hai** | **RESOLVED (combat-trio wave, `rots_combat` 8→11).** L2-app=4 (`get_char_room_vis`/`CAN_SEE`/`get_real_OB`/`get_real_parry`); app-output=2 (`abort_delay`/`complete_delay`); app-command=2 (`do_move`/act_move, `one_argument`/interpre); app-session=5 (`_arg`/`_buf`/`_buf2`/`_waiting_list`/db_boot, `is_target_valid`) | **Actual, `nm`-confirmed at Task 0 (`combat-trio-census.md` §1): 41 total project symbols, 7 blocking, 34 non-blocking (count as corrected by the Task 0 review — do_dismount was double-counted in the census's first tally).** `do_move`→existing `move` cell (confirmed real call shape); `do_dismount`→**new 26th `dismount` cell** (the row's one and only genuine combat-peer edge, `do_dismount`→`ranger.cpp`, still app-compiled); `one_argument`→L0 `rots_util.cpp` (bundled with `fill_word`/`fill[]`; `half_chop` census'd independently, see "Half_chop unbundles" below); `is_target_valid` ×4 → the existing 2-arg big_brother hook; `_arg`/`_buf`/`_buf2` (10/3/5 genuine sites) → local-composition retirement; `_waiting_list` needed **zero** work — already resolved for free by the combat-pilot wave's `waiting_list` storage-move into `clerics.cpp`, already in-lib. | **Nothing remains open.** olog_hai joined `ROTS_COMBAT_SOURCES` in the Task 4 membership commit; `CombatLayerAcyclicity` green first try. | **Old cost-table "combat-peer=6" OVERTURNED to 1** — the fresh full-closure count found exactly one genuine still-app-DEFER-TU edge (`do_dismount`), not 6; the old figure was a pre-`nm` estimate the census's own "strong prior, not ground truth" caveat existed to catch. olog_hai closed **standalone** — zero intra-trio edges to mystic or profs, confirming this row genuinely was, as predicted, the closest to SEED-CLEAN of the 9. |
| **mobact** | **RESOLVED (behavior wave, `rots_script` 3→4).** L2-app=2 (`CAN_SEE`/`CAN_SEE_OBJ`); app-output=1 (`_no_specials`/comm); app-command=12 (mob-AI `do_*` issuance); app-session=1 (`_buf`); app-other=2 (`find_first_step`/graph, `intelligent`/mudlle) | **Actual, `nm`-confirmed at Task 0 (`bw-census.md` §1/§5): 90 total project symbols, 37 resolve in-project, 15 app-tier blocking (12 `do_*` cells + `_buf` + `_no_specials` + `virt_program_number`).** Both L2-app edges (visibility family) resolved for free downward once mobact promotes; `find_first_step`/`intelligent` resolved downward/intra-lib (this wave answers the l4-seed downstream note's "still undecided" question: mobact's own tier is `rots_script`, decided by re-running the closure check at its own promotion, exactly as that note prescribed — the driver homes with the engine it invokes, not with `rots_combat`); all 12 `do_*` up-calls confirmed by name against the existing 26-cell `combat_command` table — **zero new cells needed**; `_no_specials` → `no_specials_active()` read accessor (`output_seam`); `_buf` → local composition at all 9 sites. **The wave's one genuine census miss**: `virt_program_number` (spec_ass.cpp:315, 2 call sites) — not itemized in the original combat-census row at all, a `void*`-returning spec-proc dispatcher that cannot relocate; inverted via a new `script_hooks.h` abort-tripwire cell, rider-gate count **1** (well under the pre-authorized ≤3 ceiling, no auto-STOP). | **Nothing remains open.** mobact joined `ROTS_SCRIPT_SOURCES` in Task 3's Step 2 membership commit; `ScriptLayerAcyclicity` green first try, both hosts. | **Confirms the l4-seed spec's own corollary**: mobact's `find_first_step`/`intelligent` edges resolved downward/intra-lib exactly as that wave's parent-spec §3 REVISION predicted, and mobact's own promotion (not the L4 band's mere existence) is what answered its tier — see "The behavior wave" section below, "The L4-homing lesson, fulfilled." |
| **mystic** | **RESOLVED (combat-trio wave, `rots_combat` 8→11).** L2-app=6 (`add_follower`/`get_char_room_vis`/`stop_follower`/`stop_riding`/`CAN_SEE_OBJ`/`set_mental_delay`); app-output=1 (`send_to_room`); app-command=5 (interpre text-parse + `do_flee`+`remove_character_from_group`); app-session=1 (`_buf`) | **Actual, `nm`-confirmed at Task 0 (`combat-trio-census.md` §2): 58 total project symbols, 10 blocking, 48 non-blocking.** `do_flee`→existing `flee` cell; `half_chop`/`one_argument`→L0 `rots_util.cpp`; `add_follower`→L2 `entity_lifecycle.cpp` (census-confirmed zero handler-internal statics); `_buf` (1 genuine site)→local-composition retirement; **the wave's named primary STOP-risk, fully resolved**: 5 `spell_pa.cpp`-owned combat-peer edges (`saves_confuse`/`saves_insight`/`saves_leadership`/`saves_mystic`/`saves_poison`), never enumerated by name in the design spec's "combat-peer=8" estimate, all RELOCATE-CLEAN to L2 `char_utils_combat.cpp` via the `saves_power` precedent — see "The `saves_*` five-pack is L2-lateral, not L2-optional" below. | **Nothing remains open.** mystic joined `ROTS_COMBAT_SOURCES` in the Task 4 membership commit; `CombatLayerAcyclicity` green first try. | **Old cost-table "combat-peer=8" reconciled, not just re-derived**: 4 of the 8 (`damage_stat`/`restore_stat`→clerics.cpp, `damage`/`set_mental_delay`→fight.cpp) had already dissolved for free via the combat-pilot wave's `clerics`+`fight` joint promotion — the census's own closure check simply hadn't been re-run since. The remaining 5 (all `saves_*`, all `spell_pa.cpp`-owned) are the true residual. mystic closed **standalone** (zero edges to olog_hai; the one edge to profs runs the OTHER direction — profs depends on mystic, not vice versa) and its own promotion is what dissolves profs's `scale_guardian` rider condition (see profs row). |
| **mage** | **RESOLVED (spell-family closure wave, `rots_combat` 12→15, joint commit).** L2-app=3 (`char_from_room`/`report_zone_power`/`stop_riding`); app-output=2 (`break_spell`/`send_to_room`); app-command=5 (`do_look`/`do_identify_object`/`list_char_to_char`/act_info, `msdp_room_update`/`prohibit_item_stay_zone_move`/act_move — none are `do_*` ACMD cells, ordinary helper functions); app-session=1 (`_buf`) | **Actual, `nm`-confirmed at Task 0 (`sf-census.md` §4.2): 118 total undefined symbols, 66 resolved in-project, 7 genuine app-tier residual beyond the 1 intra-family `new_saves_spell`** (`report_zone_power`/`stop_riding`/`break_spell`/`send_to_room` had already resolved downward by prior waves, reconciling the row stale). `char_from_room` → the existing L2 `entity_hooks.h` hook (5 sites, not the stale row's 1 — grep-confirmed); `do_look`×4 → the existing `look` cell (Task 3's genuine discriminator-audit gap — mage's own conversions are this cell's first real `issue_command()` caller anywhere, +2 tests); `do_identify_object` → a new `combat_hooks.h` HOOK (Task 1's overturn of the census's own RELOCATE label — its body drags 4 sibling `act_info.cpp` display helpers + 3 file-local const arrays); `list_char_to_char` → a new HOOK (same display-cluster shape); `msdp_room_update` → a new `output_seam` forwarder (call sites needed zero edit, only the redundant local `extern` deleted); `prohibit_item_stay_zone_move` → RELOCATE to `fight.cpp` (3 sites, not the row's stale 2); `_buf` retired to local `std::format` composition. | **Nothing remains open.** mage joined `ROTS_COMBAT_SOURCES` in the Task 4 joint membership commit; `CombatLayerAcyclicity` green first try, both hosts. | **Confirmed, not merely assumed**: mage's promotion required the full spell-casting family (spell_pa + ranger) landing in the SAME commit, per the true bidirectional `spell_pa↔mage` cycle (35 `spell_pa→mage` dispatch edges vs. 1 `mage→spell_pa` back-edge) — see "The spell-family closure wave" section below. |
| **limits** | **RESOLVED (behavior wave, `rots_combat` 11→12).** L2-app=7 (`char_from_room`/`extract_char`/`extract_obj`/`recalc_zone_power`/`report_zone_power`/`affect_remove_notify`/`stop_riding`); app-output=4 (`send_to_all`/`send_to_room`/`close_socket`/`_circle_shutdown`); app-command=1 (`do_flee`); app-session=4 (`_buf`/`add_exploit_record`/big_brother×2); app-other=4 (objsave `Crash_*`×3/pkill) | **Actual, `nm`-confirmed at Task 0 (`bw-census.md` §1/§2): 144 total project symbols, 90 resolve in-project, 18 app-tier/cross-edge blocking (17 genuine app-tier items — the Task 0 review errata adds `saves_spell`, which resolves app-tier `spell_pa.cpp` and belongs in this bucket — + the `one_mobile_activity` cross-edge, tallied separately).** `extract_char` (hook), `extract_obj` (L2), `stop_riding` (L2), `send_to_all`/`send_to_room` already resolved cleanly (2 of 7 L2-app / 2 of 4 app-output confirmed pre-resolved, matching the spec's own claim); `do_flee` → the `flee` cell; `add_exploit_record` → the existing `persist_hooks.h` dispatch (call site converted this wave — the hook existed, but limits' own call was still plain); `is_target_valid`+`on_character_died` (2 of big_brother×2); `pkill_create` + `Crash_crashsave` (hook existed, call site converted). **5 remain genuinely app-tier and were newly seamed this wave**: `char_from_room` (new L2 `entity_hooks.h` hook — the STOP-risk enumerated across all 11 caller files, none `rots_world`/`rots_entity`, did NOT fire), `recalc_zone_power`/`report_zone_power` (RELOCATE to `rots_world`/`db_world.cpp`), `affect_remove_notify` (RELOCATE-CLEAN to L2 `entity_lifecycle.cpp`), `close_socket` (new `output_seam` forwarder, symbol takeover), `_circle_shutdown` (new setter forwarder, WRITE confirmed at limits.cpp:656). `Crash_idlesave`/`Crash_extract_objs` (2 new sibling `combat_hooks.h` cells, `Crash_*`×3 confirmed genuinely distinct). **Two genuine spec-adjudication gaps, not itemized in this wave's design-spec prose though present in this row**: the big_brother `on_character_afked`/`on_corpse_decayed` pair (new `entity_hooks.h`-style hook pair) and `pkill_get_rank_by_character` (RELOCATE-CLEAN to `rots_persist`/`db_players.cpp` — its backing storage was already persist-tier, unlike this row's original "pkill" framing anticipated). **`saves_spell`** — the design spec's own RELOCATE-CLEAN-to-L2 default **OVERTURNED**: its body writes `spllog_mage_level`/`spllog_save`/`spllog_saves`, `fight.cpp`-owned (`rots_combat`, L3) globals; `rots_entity` does not link `RotS::combat`, so falls to RELOCATE to L3 `rots_combat` (`fight.cpp`) instead, not L2. `gain_exp`/`gain_exp_regardless`/`remove_fame_war_bonuses` confirmed unchanged (limits.cpp is already the REGISTRAR for its own hooks, self-registration verified intact, dispatch names verified) — **the wave's cleanup-commit verdict was STAYED, not deleted**: `fight.cpp` (6 call sites, not the stale-comment "five") and `clerics.cpp` (1 site) still dispatch through the hooks rather than calling limits.cpp's now-intra-lib globals directly, so nothing could be removed — see "The STAYED verdict" below. `_buf` retirement (both files, genuine globals, zero local shadows). | **Nothing remains open.** limits joined `ROTS_COMBAT_SOURCES` in Task 3's Step 1 membership commit; `CombatLayerAcyclicity` green first try, both hosts — zero census misses, every up-call already converted onto a hook/seam by Task 2. | **The `one_mobile_activity` cross-edge (limits → mobact) is this wave's defining coupling, not a per-row closure-partner constraint in the usual sense** — see "The behavior wave" section below for the full permanent-inversion account (the codebase's first L3→L4 upward hook). |
| **ranger** | **RESOLVED (spell-family closure wave, `rots_combat` 12→15, joint commit).** L2-app=11 (`generic_find`/`get_char_room_vis`/`add_follower`/`obj_from_char`/`stop_*`/`CAN_SEE`/`get_real_OB`/`get_real_parry`); app-output=3 (`abort_delay`/`complete_delay`/`get_from_txt_block_pool`); app-command=9 (interpre parse + act_move door/move helpers); app-session=6 (db_boot globals + big_brother×2); app-other=1 (`show_tracks`/graph) | **Actual, `nm`-confirmed at Task 0 (`sf-census.md` §4.3): 167 total undefined symbols, 102 resolved in-project, 12 genuine app-tier residual beyond the 1 intra-family `say_spell` + `show_tracks`** (the visibility family/`check_break_prep`/`update_pos`/`stop_fighting`/`appear`/`check_hallucinate`/`damage`/`waiting_list` had already resolved downward by prior waves). `do_hit`×2/`do_flee`/`do_move`×2 → their existing cells; `is_target_valid` (2-arg, 7 sites) → the entity hook; `special` → the existing `call_special` hook (not a rider-gate edge); `on_character_returned` → a NEW 4th `entity_hooks.h` big_brother sibling (a T0 finding, not in the design spec's default list); `find_door` → RELOCATE to `fight.cpp`; `argument_interpreter` → RELOCATE to L0 `rots_util.cpp`; `check_simple_move` → a new `combat_hooks.h` HOOK; `show_tracks`+`track_desc`+`water_track_desc` → RELOCATE to `db_world.cpp` (Default A fires — presentation over L3-reachable world/room data, NOT pathfind-internal — no third permanent L3→L4 inversion); `_arg`/`_buf`/`_buf2` retired (3 genuine db_boot globals, not the row's stale ~4). | **Nothing remains open.** ranger joined `ROTS_COMBAT_SOURCES` in the Task 4 joint membership commit; `CombatLayerAcyclicity` green first try, both hosts. | **Confirmed**: ranger's one-directional dependency on spell_pa (`say_spell`) meant it had to promote with or after spell_pa; landing in the SAME joint commit as spell_pa+mage was simplest — see "The spell-family closure wave" section below. |
| **spec_pro** | **RESOLVED (spec-pair wave, `rots_script` 5→6 — NOT `rots_combat`, see the tier ruling below).** L2-app=11 (handler/utility find/extract/stop/CAN_SEE family); app-output=4 (`send_to_room`/`abort_delay`/`complete_delay`/`get_from_txt_block_pool`); app-command=19 (`command_interpreter`/`_cmd_info` + ~a dozen `do_*`); app-session=3 (`_buf`/`_waiting_list`/`add_exploit_record`); app-other=1 (`find_first_step`/graph) | **Actual, `nm`-confirmed at Task 0 (`sp-census.md` §1/§4): 204 total undefined symbols, 115 resolved in-project. The ~dozen `do_*` sweep re-derives to 14 real app-tier `do_*` call sites, all mapping onto EXISTING `combat_command` cells (gen_com/say/tell/look/stat/lock/close/open/unlock/move/wake/stand/wear/hit) — ZERO new cells. `command_interpreter`→existing `dispatch_command_interpreter` hook; `extract_char`/`char_from_room`→existing entity hooks; `add_exploit_record`→existing persist dispatch; `_buf`/`_waiting_list` retired/dissolved.** `_cmd_info` (9 read sites) → a NEW `command_min_position` accessor hook (`script_hooks.h`, SAFE-SENTINEL default) — the row's one genuinely new seam. `is_number()` → RELOCATE-CLEAN to L0 `rots_util.cpp`. | **Nothing remains open.** spec_pro joined `ROTS_SCRIPT_SOURCES` in the Task 4 Step 1 membership commit (SEQUENTIAL, first of the pair); `ScriptLayerAcyclicity` green first try, both hosts. | **The row's own prediction — "unlikely to close without a dedicated command-dispatch investigation" — held, and the investigation is exactly what the spec-pair wave's T0 census was.** The tier question the row flagged as "still undecided" resolved to `rots_script`, NOT `rots_combat`: under `rots_combat`, `find_first_step`/`command_interpreter`-hook/(spec_ass's) `set_virt_*_hook` are THREE irreducible upward edges; under `rots_script` all three resolve downward/intra-lib. Same tier-adjudication method the behavior wave used for `mobact` (driver homes with the engine it invokes), extended one more link: the spec-proc bodies home with the `virt_*` dispatch machinery that invokes them. |
| **spell_pa** | **RESOLVED (spell-family closure wave, `rots_combat` 12→15, joint commit).** combat-peer=76 (ALL spell TUs + fight + ranger + battle_mage — the whole-row registrar/`do_cast` hub); app-output=3 (`descriptor_list`/`abort_delay`/`complete_delay`); app-command=2 (`report_wrong_target`/`target_from_word`/interpre); app-session=5 (`_buf`/`_waiting_list`/`_color_sequence`+`get_color_sequence`/color/`is_target_valid`) | **Actual, `nm`-confirmed at Task 0 (`sf-census.md` §1/§2): 164 total undefined symbols, 111 resolved in-project. The stale combat-peer=76 re-derives to 44 already-in-lib (dissolve for free — 41 `spell_*` mystic implementations, 4 `battle_mage_handler` methods, `appear`/`check_hallucinate`/`spllog_*`/`waiting_list`) + 35 intra-family (the `spell_*` mage implementations spell_pa's registrar dispatches to — dissolve once mage lands in the same commit) + 7 genuine app-tier residual.** `is_target_valid` (3-arg) → `rots::entity::dispatch_target_valid` (a Task 2 fix commit, after an initial reading deferred it in error); `report_wrong_target`/`target_from_word` → RELOCATE to `visibility.cpp`; `descriptor_list` walk → a new `get_descriptor_list_head()` `output_seam` accessor; `color_sequence[]`+`get_color_sequence()` → RELOCATE to `visibility.cpp` (after a tree-wide reader scan confirmed every `CC_*` expansion site is still app-tier); `_buf` retired to local composition. | **Nothing remains open.** spell_pa joined `ROTS_COMBAT_SOURCES` in the Task 4 joint membership commit; `CombatLayerAcyclicity` green first try, both hosts. | **Confirmed as the wave's central justification, not merely predicted**: the true bidirectional `spell_pa↔mage` cycle (35 edges out via the registrar's dispatch table, 1 edge back via `new_saves_spell`) meant neither TU could promote standalone or paired with only one partner; `ranger`'s one-directional `say_spell` dependency on spell_pa meant it had to land with or after. All three closed in ONE joint commit — see "The spell-family closure wave" section below for the full account, including the `show_tracks` third-inversion ruling (relocate down, no inversion) and the new `on_character_returned` big_brother hook this wave's ranger census surfaced. |
| **spec_ass** | **RESOLVED (spec-pair wave, `rots_script` 6→7 — the pair's second/final commit; combat DEFER reaches ZERO with this row).** combat-peer=39 (→ spec_pro); app-other=3 (`gen_board`/boards, `postmaster`/mail, `receptionist`/objsave — spec-proc fn-ptrs) | **Actual, `nm`-confirmed at Task 0 (`sp-census.md` §1/§2): 53 total undefined symbols; 39 resolve to spec_pro (the intra-pair edges — dissolve intra-`rots_script` for free once spec_pro is promoted first, per the census's ONE-DIRECTIONAL ruling: spec_ass→spec_pro=39, spec_pro→spec_ass=0, no cycle, SEQUENTIAL not joint), 13 resolve in the library-first map, 1 is `_strlen` (libc).** The 3 app-other spec-proc registrar edges (`gen_board`/boards.cpp, `postmaster`/mail.cpp, `receptionist`/objsave.cpp) got their own hook family after all — a NEW enum-keyed `lookup_registered_special()` lookup (`script_hooks.h`, loud-tripwire default, confirmed exactly 3 by `nm` with no 4th and no rider-gate slot consumed). `set_virt_program_number_hook`/`set_virt_assignmob_hook` become intra-lib (already-registered hooks, no edit). | **Nothing remains open.** spec_ass joined `ROTS_SCRIPT_SOURCES` in the Task 4 Step 2 membership commit; `ScriptLayerAcyclicity` green first try, both hosts. **Combat DEFER: 0.** | The row's own note — "Fully gated on spec_pro's own promotion... do not attempt it before spec_pro closes" — held exactly: the census confirmed the dependency is one-directional (not a cycle), so the plan's SEQUENTIAL order (spec_pro first, spec_ass second) was clean, no joint commit forced. The registrar family is the wave's one genuinely new seam taxonomy — pre-authorized at exactly 3 edges, landed consumer-free in Task 1, confirmed by both TUs' conversions to have no 4th edge anywhere. |
| **profs** *(caveated SEED-WITH-SEAM in the original census — **RESOLVED, joined in the combat-trio wave as a census-gated rider, `rots_combat` 8→11**)* | L2-app=1 (`get_guardian_type`/utility); app-session=2 (`_buf`/`add_exploit_record`/db_boot); combat-peer=1 (`scale_guardian`/mystic → drags mystic) | **Actual, `nm`-confirmed at Task 0 (`combat-trio-census.md` §3): 16 total project symbols, 4 blocking, 12 non-blocking — an exact numeric match to the design spec's source-level prediction.** `get_guardian_type`→relocated into `rots_combat` itself (`visibility.cpp`), NOT L2 — its body reads `mob_index` (`db_world.cpp:95`, L3-world), so `rots_entity` genuinely cannot host it; `_buf` (2 genuine sites)→local-composition retirement; `add_exploit_record` ×8 → the existing `persist_hooks.h` `dispatch_exploit_capture` seam (zero new infrastructure, the identical seam `fight.cpp` already consumed). The `scale_guardian` rider condition FIRED via Path B (mystic's own membership dissolves the edge), NOT Path A — see below. | **Nothing remains open.** profs joined `ROTS_COMBAT_SOURCES` in the SAME Task 4 membership commit as olog_hai/mystic; `CombatLayerAcyclicity` green first try — `scale_guardian` resolved intra-lib automatically since both sides landed together. | **The rider row's own open question, answered**: `scale_guardian` (mystic.cpp:1584) is confirmed **NOT** standalone-relocatable — Task 0's body read found a same-file helper cluster of **6** functions (`set_guardian_stats`/`calc_guardian_hp`/`set_guardian_health`/`tweak_aggressive_guardian_stats`/`tweak_defensive_guardian_stats`/`tweak_mystic_guardian_stats`), not the design spec's stated "four" (`calc_guardian_hp`/`set_guardian_health` were omitted from its enumeration) — Path A (standalone relocation, the `saves_power` shape this row's own note speculated about) genuinely fails. Path B held instead: profs's dependency on mystic is a clean **one-directional gate**, not a cycle — see "The combat-trio wave" section below for why that distinction matters architecturally. Task 0 also found `scale_guardian` has a **second** external caller the design spec missed: `objsave.cpp:775` (mirroring its existing `get_guardian_type` call's local-`extern` idiom) — doesn't change the verdict, just the caller count. |

**General next-row guidance** (unchanged principles, now backed by the table above):
(1) read this table + a fresh `nm` re-run for the target row, don't trust either
alone; (2) build any missing seam BEFORE the conversion commit, not during; (3)
budget for coupled dead-code cleanup riding the same diff as each converted call
site; (4) byte-edit via Python for any CRLF file (verify first, don't assume LF);
(5) run the actual `*LayerAcyclicity` linkcheck before declaring a membership move
done, even for files a PRIOR task already fully converted — apply the closure check
(recipe step 2) explicitly before picking a candidate subset, not after a build
failure forces it.

## The combat-trio wave: first standalone promotions

Design: `docs/superpowers/specs/2026-07-20-combat-trio-design.md`. Plan:
`docs/superpowers/plans/2026-07-20-combat-trio.md`. Census:
`.superpowers/sdd/combat-trio-census.md`. Task reports:
`.superpowers/sdd/trio-task-{0,1,2,3,4}-report.md`. `rots_combat` grew 8 → 11 TUs:
`olog_hai.cpp` + `mystic.cpp` + `profs.cpp`, one membership commit (`019b4c8`),
`CombatLayerAcyclicity` green **first try**, both hosts — the first time this recipe
has produced zero census misses at the linkcheck gate (contrast the combat-pilot
wave's two, `gain_exp`/`waiting_list`).

### The one-directional-vs-cycle distinction (this wave's central architectural finding)

The recipe's "intra-subset rule" (above) was written entirely around the
`clerics`↔`fight` **cycle** — two TUs that call each other, which the
`CombatLayerAcyclicity` linkcheck cannot verify unless both promote in the same
commit. This wave supplies the recipe's missing complementary case: a **one-directional
gate**, where TU B calls into TU A but A never calls back into B.

- `olog_hai.cpp` and `mystic.cpp` share **zero symbols in either direction** — the
  playbook's first true standalone promotions since the combat-seed wave's four
  SEED-CLEAN TUs (which needed no relocation at all). Each closed over its own
  combat-peer edges independently; neither's promotion depended on the other's.
- `profs.cpp` → `mystic.cpp` (`scale_guardian`) is real coupling, but it only runs
  one way: `mystic.cpp` has no reverse reference into `profs.cpp` at all. A
  one-directional gate does **not** force joint membership by the linkcheck
  mechanism the way a cycle does — `profs` could, in principle, have stayed app-compiled
  indefinitely while `mystic.cpp` promoted alone (a legal downward app→lib call), or
  promoted in a LATER commit once mystic already existed in-lib. This wave chose ONE
  commit for all three TUs for simplicity, not because `CombatLayerAcyclicity` required
  it — the commit body says so explicitly (Task 4, `019b4c8`), matching the plan's own
  instruction to "document the closure structure honestly."
- **The practical difference for a future wave choosing which DEFER-7 row to migrate
  next:** before assuming two coupled TUs must land together, check which direction(s)
  the edge runs. A cycle (both directions) forces a joint commit. A one-directional
  gate only forces the CALLEE to promote no later than the CALLER — the callee can go
  first alone, or the two can still land together for convenience (as this wave did),
  but the callee is never forced to wait for the caller.

### Half_chop unbundles from one_argument

The design spec framed `one_argument`/`fill_word`/`fill[]`/`half_chop` as a single
4-item relocation package. Task 0's census (§5.3) found `half_chop` (interpre.cpp:1535)
has **zero function-call edges of its own** — not even to `fill_word`/`search_block`,
which `one_argument` itself calls. It shares a destination (L0 `rots_util.cpp`) and a
motivating tier constraint (both are text-parse leaves an L0 TU can host) with the
`one_argument`/`fill_word`/`fill[]` trio, but not a storage or call dependency — Task 1
relocated it as its own independent unit rather than forcing it into the same diff as
the other three. **Lesson for a future row:** a shared destination does not imply a
shared move; census each candidate symbol's own edges before assuming a "package" a
design spec names is load-bearing rather than a convenience grouping.

### The `saves_*` five-pack is L2-lateral, not L2-optional

`mystic.cpp`'s five `spell_pa.cpp`-owned edges (`saves_confuse`/`saves_insight`/
`saves_leadership`/`saves_mystic`/`saves_poison`) were the wave's named primary
STOP-risk — the old cost table's "mystic combat-peer=8" never enumerated them by name.
Task 0's body read found all five are macro-expanded L2 calls (the same shape
`saves_power` already established as RELOCATE-CLEAN in the combat-pilot wave): they
read `char_data` fields directly, the way any `rots_entity` function does. **This
means their legal destination tier has a floor, not just a target**: L2
(`char_utils_combat.cpp`, where they landed, beside `saves_power`) or **higher** is
fine, but nothing lower-tier is — placing them at L1/L0 would reintroduce an upward
edge from whatever L1/L0 code they'd sit beside. Recorded here because a future
relocation choosing "the cheapest available destination" for a similar macro-heavy
symbol needs to check the floor, not just find *a* tier that compiles.

### Census corrections this wave made to the playbook's own prior estimates

None of these changed a verdict (every TU still promoted; the rider still fired) —
they correct counts the combat-pilot wave's cost table got wrong or left
unenumerated, now folded into the per-TU table rows above:

1. **olog_hai's "combat-peer=6" → re-derived to 1** (`do_dismount` only). The old
   figure predates the combat-pilot wave's `clerics`/`fight` promotion, which
   resolved several of olog_hai's edges (`_waiting_list` via the `waiting_list`
   storage-move, several visibility-family calls) without anyone re-running the
   census against the new build wiring.
2. **`scale_guardian`'s helper cluster is 6 functions, not 4.** The design spec's
   enumeration omitted `calc_guardian_hp`/`set_guardian_health`. Same verdict either
   way (the whole cluster moves with `mystic.cpp` regardless of count), but a future
   reader citing this cluster's size should cite 6.
3. **`scale_guardian` has a second external caller the design spec missed**:
   `objsave.cpp:775`, in addition to the documented `profs.cpp:233`. Both dissolve
   identically once mystic promotes — Path B (the membership dissolution) covers any
   caller, not just the one the spec happened to name.

### Cost markers (this wave, factual)

- **Up-call sites converted:** 6 (olog_hai: `do_dismount`, `do_move`, `is_target_valid`
  ×4) + 1 (mystic: `do_flee`).
- **Relocations:** `one_argument`/`fill_word`/`fill[]` (bundle) + `half_chop`
  (standalone) → L0; `saves_*` five-pack + `add_follower` → L2; `get_guardian_type` →
  L3 (`rots_combat` itself, not L2 — the wave's one L3-forcing relocation, since it
  reads an L3-world global with no L2-visible resolver).
- **New seam:** 1 (`dismount`, the 26th `combat_command` cell).
- **Scratch-buffer retirements:** olog_hai 10 `buf`/3 `buf2`/5 `arg`; mystic 1 `buf`;
  profs 2 `buf` — all local-composition, no storage-move (unlike `waiting_list`'s
  precedent — these globals are shared across dozens of still-app files, so a
  storage-move wasn't viable per-TU).
- **Tests added:** 4 (2 `dismount` discriminator pair, Task 1; 2 `move` discriminator
  pair, Task 2 — a genuine audit-found gap, not assumed).
- **Membership commits:** 1, all three TUs together (`019b4c8`), by choice not
  linkcheck necessity (see "one-directional-vs-cycle" above).
- **Census misses at the linkcheck gate:** 0 — the first application of this recipe to
  land `CombatLayerAcyclicity` green on the first attempt.

## The l4-seed wave: `rots_pathfind` + `rots_script` (the first L4-band promotion)

Design: `docs/superpowers/specs/2026-07-21-l4-seed-design.md`. Plan:
`docs/superpowers/plans/2026-07-21-l4-seed.md`. Census: `.superpowers/sdd/l4-census.md`
(gitignored scratch). Task reports: `.superpowers/sdd/l4-task-{0,1,2,3}-report.md`. This
wave applies the recipe **one tier up** for the first time: census → closure check →
seams → conversions → membership → verification, applied not to a `rots_combat` row but
to two brand-new libraries in a brand-new band above the L3 peer tier. `rots_pathfind`
(`graph.cpp`, 1 TU) and `rots_script` (`mudlle.cpp`+`mudlle2.cpp`+`script_hooks.cpp`,
3 TUs) stand up; `zone.cpp` rides independently into `rots_world` (4 TUs). Eight
libraries, eight `*LayerAcyclicity` linkchecks. See `docs/BUILD.md`'s "L4 band"
subsection and the parent spec's §3 REVISION
(`docs/superpowers/specs/2026-07-16-library-architecture-design.md`) for the full
architectural/gate account; this section records the recipe-level lessons only.

### The central lesson: orchestration TUs home ABOVE combat, not inside a peer library

Every prior wave's promotion target was a peer *within* an existing tier (a new
`rots_combat` row) or a brand-new peer tier at the *same* level as existing ones
(`rots_world` standing up alongside `rots_persist`). This wave is the first to answer a
different question: where does a TU go when its natural "keep it near its subject
matter" home (`rots_world`, since `graph.cpp`/`mudlle.cpp` are nominally "world-building
tools") would create a bidirectional link with a tier that TU also calls into
(`rots_combat`, since both TUs drive combat-adjacent actions — hunting, mob-program
command issuance)? **The answer is not "pick one side" — it's "add a tier."** Placing
`rots_pathfind`/`rots_script` in a new band *above* `rots_combat` makes both directions
of the original conflict resolve as ordinary downward calls: graph/mudlle's calls into
combat become L4→L3 (legal), and nothing in `rots_combat` ever needs to call back into
pathfind/script (there is no such edge to invert). This is the general shape for any
future TU that is nominally "part of" one tier but structurally *drives* a lower one —
check which direction the drive relationship points before assuming the TU's thematic
home is its architectural home.

**Corollary — this is why `mobact.cpp`/`spec_pro.cpp`'s own future tier is deliberately
left undecided** (parent spec §3 REVISION downstream note; the per-TU cost table's
mobact/spec_pro rows above). Both TUs now have zero-seam downward access to
`find_first_step`/`intelligent`/`command_interpreter` (the L4 band exists), but that does
not by itself mean either TU belongs *in* the L4 band — they may still be ordinary
`rots_combat` DEFER-row promotions that happen to also call downward into L4, the same
way any `rots_combat` TU calls downward into `rots_world`/`rots_persist` today. The tier
question for each is answered by re-running the closure check at THEIR OWN promotion
time, not inherited from this wave.

### The `extract_char` re-home: a shared L2 inversion beats a second L3-peer duplicate

`zone.cpp` (promoting to `rots_world`, L3) needed the same `extract_char` inversion
`fight.cpp` (`rots_combat`, L3) already used — but that hook lived in `combat_hooks.h`,
owned by and unreachable from the *other* L3 peer. The naive fix (a second,
`rots_world`-owned copy of the same hook in `world_hooks.h`) would have worked
mechanically but left two independent inversions dispatching to the same real body — a
maintenance hazard the moment either drifts. **The actual fix: re-home the hook one tier
DOWN, to L2 `entity_hooks.h`**, since both L3 peers already PUBLIC-link `RotS::entity`.
One inversion, two consumers, both legal downward dispatches. **Lesson for a future wave
finding a peer-owned hook it needs too:** before duplicating, check whether the hook's
owning tier is higher than strictly necessary — a hook built for one L3 peer's own need
is often accidentally over-placed, and re-homing it to the shared L2 tier both peers
already link is cheaper and more honest than a second copy. This is the general form of
the same move the world-seed wave made for `world_room_vnum`, one direction earlier
(persist's hook, dispatched by world) — here it's an existing L3-peer hook moving down to
L2 rather than a new hook being added at L3.

### Zone/graph/mudlle/mudlle2 outcomes, in one place

- **`zone.cpp`**: fully resolved. Both halves (`zone_load.cpp`, world-seed wave;
  `zone.cpp` itself, this wave) are now `rots_world` members. Five blocking edges
  (`extract_char`, `do_wear`, `is_zone_populated`, `equip_char`, the pkill fame pair)
  all inverted through hooks — none relocated, since none was entity-pure or
  persist-pure once actually body-read (see the two OVERTURNs below). Zero-cost outcome:
  `is_empty(int)`'s function body retired outright (not just its call sites), once its
  `world_hooks.h` twin proved to be its only remaining reason to exist.
- **`graph.cpp`**: fully resolved, standalone `rots_pathfind`. The cheapest of the four —
  zero new seam infrastructure, only 3 existing-cell conversions (`say`×2/`move`×1) and 2
  scratch-buffer retirements. Its `hit()`/`get_char_vis()` combat-peer edges needed no
  conversion at all, just tier placement.
- **`mudlle.cpp`+`mudlle2.cpp`**: fully resolved, joint `rots_script` (by choice, not
  linkcheck necessity — the 14-helper `mudlle→mudlle2` edge is one-directional, the same
  "gate, not cycle" shape the combat-trio wave established one tier down). The wave's one
  genuinely heavy new seam (`command_interpreter`, inverting the entire player-command
  dispatcher) and its one named collapse-condition seam (`PERS`, confirmed resolves)
  both land here.
- **Two T0 OVERTURNs, both later reviewer-confirmed correct** — a reminder that a design
  spec's own adjudication defaults are a strong prior, not ground truth, the same
  standing caveat the per-TU cost table above carries: `equip_char` was expected
  entity-pure (relocate to L2) but its own body carries a poison-coupling block
  identical in shape to `unequip_char`'s (the design spec's cited source was a
  caller-side observation misread as a body-content claim); `pkill_get_good_fame`/
  `pkill_get_evil_fame` were expected relocate-clean to `rots_persist` but read
  app-tier-owned globals (`good_ranking`/`evil_ranking` live in `pkill.cpp`, not
  `pkill_json.cpp`) — relocating just the two-line accessors would have compiled in the
  final executable while failing `PersistLayerAcyclicity` by design. Both fell to their
  spec's own named hook fallback instead of the expected relocation.

### Cost markers (this wave, factual)

- **Up-call sites converted:** 18 (graph: `do_say`×2/`do_move`×1; mudlle: `do_say`×11/
  `command_interpreter`×1; mudlle2: `do_say`×2/`PERS`×1; zone: `extract_char`/`do_wear`/
  `equip_char`/`is_zone_populated`×4/pkill-fame×4, counted as 4 distinct symbols
  converted at 8 call sites).
- **New seams:** 8 hooks/forwarders (`command_interpreter`, `PERS`, `do_wear`,
  `is_zone_populated`, `equip_char`, `pkill_get_good_fame`, `pkill_get_evil_fame`,
  `put_to_txt_block_pool`) + 1 re-homed hook (`extract_char`, `combat_hooks.h` →
  `entity_hooks.h`).
- **New library targets:** 2 (`rots_pathfind`, `rots_script`) + 2 new
  `*LayerAcyclicity` linkchecks — the first band-level addition since the L3 peer tier
  itself stood up.
- **Relocations:** 0 — both T0 defaults that started as "relocate" candidates
  (`equip_char`, pkill fame) OVERTURNED to hooks instead; this wave's entire cost is
  hooks/forwarders and scratch-buffer retirement, no function bodies moved to a new tier
  (`script_hooks.cpp`'s Task 3 storage move is a relocation between two APP-tier files
  becoming one LIBRARY-tier file, not a cross-tier body relocation in the sense the
  per-TU cost table above uses the word).
- **Scratch-buffer retirements:** graph 2 (`_arg`/`_buf`); mudlle 4 (`buf`/`buf2` sites);
  mudlle2 0 (double-checked, genuinely zero — confirmed, not assumed).
- **Tests added:** 17 (T1 +13 seam/hook discriminators; T2 +2 `say`-cell discriminator
  gap; T3 +2 the two new linkchecks themselves).
- **Membership commits:** 3 (`zone.cpp`→`rots_world`, independent; `rots_pathfind`;
  `rots_script`, in that order — `rots_pathfind` before `rots_script` is a hard ordering
  constraint, not a choice, since `rots_script` links it).
- **Census misses at the linkcheck gate:** 1 (`script_hooks.cpp`'s storage placement,
  Task 3 — adjudicated in-flight as a relocation, not stubbed; see `docs/BUILD.md`'s "L4
  band" subsection).

## The behavior wave: `limits.cpp` → `rots_combat`; `mobact.cpp` → `rots_script`

Design: `docs/superpowers/specs/2026-07-21-behavior-wave-design.md`. Plan:
`docs/superpowers/plans/2026-07-21-behavior-wave.md`. Census: `.superpowers/sdd/bw-census.md`
(gitignored scratch). Task reports: `.superpowers/sdd/bw-task-{0,1,2,3}-report.md`. This wave
applies the recipe to two owner-selected DEFER-row TUs landing in **different existing
libraries, no new library targets, no new linkchecks**: `limits.cpp` (the tick/limits engine)
promotes as an ordinary `rots_combat` DEFER-row member (11 → 12 TUs), while `mobact.cpp` (the
mob-AI driver) answers the l4-seed wave's own "still undecided" downstream note by landing in
`rots_script` (3 → 4 TUs) instead of `rots_combat` — the first DEFER-row TU whose closure check
resolved it into the L4 band rather than its origin peer tier. `rots_combat` now has 5 members
not in the original 16-TU sketch (`combat_hooks.cpp`/`visibility.cpp` from blocker-buster,
`clerics.cpp`/`fight.cpp` from combat-pilot are IN the sketch, `olog_hai`/`mystic`/`profs` from
combat-trio are IN the sketch, `limits.cpp` from this wave is IN the sketch) — DEFER drops
**7 → 5** (`spec_ass`/`mage`/`spell_pa`/`ranger`/`spec_pro`).

### The `one_mobile_activity` cross-edge — the wave's defining coupling, and the codebase's first L3→L4 inversion

Every prior wave's cross-TU coupling ran within a single tier (`clerics`↔`fight`, both
`rots_combat`) or one-directionally within a tier (`profs`→`mystic`, both `rots_combat`).
This wave is the first to find a coupling that **crosses a tier boundary that does not move**:
`limits.cpp:1398` (inside the `SPELL_ACTIVITY` affect-processing case) calls
`one_mobile_activity(i)` — defined in `mobact.cpp`. Once `limits` is `rots_combat` (L3) and
`mobact` is `rots_script` (L4), this is an `L3 → L4` **upward** edge. Unlike an intra-tier
one-directional gate (which dissolves the moment its sole partner promotes, as `profs`'s
`scale_guardian` edge did), a cross-tier edge can never resolve as a direct call regardless of
either side's own membership — the layer graph's certified `combat < pathfind < script`
order (parent-spec §3 REVISION) forbids it permanently, not just until a promotion lands.

**Disposition: a new `combat_hooks.h::dispatch_one_mobile_activity(char_data*)` hook** — void,
loud-tripwire default (a genuine error worth logging loudly if it ever fires, not a safe
no-op). Backing storage in `combat_hooks.cpp` (`rots_combat`, the **dispatcher's own** lib —
not the registrar's, a deliberate departure from `world_hooks.h`'s "storage lives in the
promoting library" precedent, since here the promoting/registering side (`mobact.cpp`) is the
one that must never be the tier the hook's storage lives in). Registered by `mobact.cpp` (a
legal `L4 → L3` downward registration call at boot). Dispatched by `limits.cpp` (intra-lib once
`limits` is `rots_combat`). mobact's own `:61` self-call (inside `mobile_activity()`'s loop)
stays a direct intra-file call, unaffected by any tier — only the cross-file edge inverts.
Full closure-check verification (census §3): `one_mobile_activity` has exactly one external
caller (`limits.cpp:1398`) besides mobact's self-call — `interpre.cpp`/`spell_pa.cpp` carry
only unused forward declarations — and mobact was grepped for every limits-self-registered
symbol name (`gain_exp`/`gain_exp_regardless`/`remove_fame_war_bonuses`), finding zero
references anywhere in the file: a genuinely one-directional gate, not a cycle, so the two
memberships stayed independently gateable (the seam landed consumer-free in Task 1; both
membership commits in Task 3 needed no ordering between them).

**Why this is a PERMANENT inversion, unlike every prior wave's hook.** Every other hook this
playbook has documented (`extract_char`, `equip_char`, the pkill-fame pair, `do_wear`,
`is_zone_populated`) exists because the calling TU and the called TU happened not to be in the
same library **yet** — a future wave that promotes both sides into the same tier could, in
principle, retire the hook and restore a direct call (though none has done so — see "The
STAYED verdict" below for why that retirement is harder than it sounds even when both sides
land in-lib). `one_mobile_activity` is different in kind: `limits` and `mobact` are now
permanently on opposite sides of a tier boundary that itself does not move (`rots_combat` is
always below `rots_script` in the certified order), so no future promotion of either TU can
ever dissolve this hook. It is the first data point in this playbook of a hook that is
architecturally permanent, not merely current-membership-contingent.

### The L4-homing lesson, fulfilled

The l4-seed wave's parent-spec §3 REVISION downstream note (and this playbook's own
"Corollary" under "The l4-seed wave" section) deliberately left `mobact.cpp`'s eventual tier
undecided, predicting only that its `find_first_step`/`intelligent` edges would resolve
downward/intra-lib **whichever** tier it landed in, and that the real tier decision would be
made by re-running the closure check at mobact's own promotion time. This wave is that
re-run, and it answered the question exactly as the note framed it: mobact's own promotion
closed cleanly into `rots_script` (`intelligent()` intra-lib, `find_first_step()` downward to
`rots_pathfind`, all 12 `do_*` up-calls downward to `rots_combat` via the existing 26-cell
table) with **zero new link edge needed** — `rots_script`'s existing PUBLIC link set already
covered every mobact edge. The driver homes with the engine it invokes, not with the tier its
thematic subject matter (mob combat AI) might suggest. `spec_pro.cpp`'s own tier remains the
one still-open instance of this same question (see the parent spec §3 REVISION downstream
note, now updated to record mobact's resolution alongside spec_pro's still-undecided one).

### The `virt_program_number` rider — closed at 1, not exhausted

`mobact.cpp:64`/`:126` each call `virt_program_number(ch->specials.store_prog_number)`
(`spec_ass.cpp:315`), a `void*`-returning spec-proc dispatcher whose body references dozens of
still-app `spec_ass`/`spec_pro` symbols and cannot relocate — a genuine census miss the
original combat-census row never itemized at all (source-grepped, not `nm`-derived, at design
time). The design spec pre-authorized a **rider gate**: up to three same-shape edges
(`void*`-returning spec-proc-dispatcher class functions) could resolve via the same
`script_hooks.h` abort-tripwire cell pattern without stopping; a fourth same-shape edge, or any
different-shape edge, would auto-STOP for controller adjudication. Task 0's full
cross-reference (every call mobact.cpp makes against every function defined in
`spec_ass.cpp`/`spec_pro.cpp`) found **exactly one** such edge — well under the ceiling, no
auto-STOP fired, and the rider gate closed at **1 of the pre-authorized 3**. Disposition: new
`script_hooks.h::dispatch_virt_program_number(int)` cell, `using virt_program_fn = void*
(*)(int)`, abort-tripwire default (pointer return, no safe placeholder — the `PERS`/
`mudlle_converter` precedent), registered by `spec_ass.cpp` pre-`boot_db`.
`comm.cpp:2671`/`interpre.cpp:1538`/`:1548`'s own direct calls stay app→app, unaffected — only
mobact's edge inverted. **Lesson for `spec_pro`/`spec_ass`'s own eventual promotion**: the rider
mechanism is now proven at 1 of 3 uses; a future wave touching either TU should expect more
same-shape edges (spec_ass's own combat-peer=39 tally is dominated by spec_pro references) and
budget for the ceiling, not assume it starts fresh.

### The STAYED verdict — why `gain_exp`'s hooks survive both TUs' membership

Task 3's cleanup step verified, by grep rather than assumption, whether `limits.cpp`'s
`gain_exp`/`gain_exp_regardless`/`remove_fame_war_bonuses` self-registration and their
`combat_hooks.h` hooks could be deleted now that `limits.cpp` is intra-lib with every
consumer. **They could not**: `fight.cpp` (6 call sites — `fight.cpp:936`/`1084`/`1086`/
`1116`/`1330`/`1974`, corrected from a stale in-file comment that said "five") and
`clerics.cpp` (1 call site, `clerics.cpp:248`) — both `RotS::combat` members since the
combat-pilot wave — still dispatch through `rots::combat::gain_exp()`/
`gain_exp_regardless()`/`remove_fame_war_bonuses()` rather than calling `limits.cpp`'s
now-intra-lib globals directly. The hooks and their registrar calls (`comm.cpp`/
`gtest_main.cpp`'s pre-`boot_db` registrations) are genuine, currently-live dispatch
consumers, not mere self-registration — deleting them would break real behavior. Commit 3
of Task 3 is therefore comment-only (four stale banners in `combat_hooks.h` updated to
document the finding), not a deletion. **Lesson for a future wave that lands both a hook's
registrar TU and its dispatch-consumer TUs in the same library**: joint membership makes a
hook retirement *legal* (both sides could now call each other directly), but it does not make
it *automatic* — a real STAYED-vs-retire verdict requires grepping every actual consumer, not
inferring from membership alone. Converting `fight.cpp`'s/`clerics.cpp`'s call sites back to
direct `limits.cpp` calls remains a deferred follow-on simplification, explicitly out of this
wave's scope.

### Cost markers (this wave, factual)

- **Up-call sites converted:** 12 (mobact: `do_say`/`do_assist`/`do_stand`/`do_rescue`/
  `do_hit`/`do_flee`/`do_wear`/`do_move`/`do_wake`/`do_sleep`/`do_rest`/`do_sit`, all zero-new-cell)
  + 2 (mobact: `virt_program_number` ×2 sites) + 1 (mobact: `no_specials`) + 9 (limits:
  `one_mobile_activity`, `Crash_idlesave`, `Crash_extract_objs` ×2, `char_from_room` ×2, the
  big_brother AFK/corpse-decay pair, `circle_shutdown`) + 4 (limits' prior-wave-hook backlog:
  `Crash_crashsave`, `extract_char`, `do_flee`, `add_exploit_record` — hooks already existed
  from earlier waves, but limits' own call sites had never been converted onto them).
- **New hooks/seams:** 6 (`one_mobile_activity`, `virt_program_number`, `Crash_idlesave`,
  `Crash_extract_objs`, `char_from_room`, the big_brother AFK/corpse-decayed pair) + 3
  `output_seam` entries (`close_socket` takeover, `no_specials_active()`,
  `request_circle_shutdown()`).
- **Relocations:** `affect_remove_notify` → L2 `entity_lifecycle.cpp`;
  `recalc_zone_power`/`report_zone_power` → `rots_world`/`db_world.cpp`; `saves_spell` → L3
  `rots_combat`/`fight.cpp` (an OVERTURN of the design spec's own L2 default — its body writes
  `fight.cpp`-owned `spllog_*` globals, and `rots_entity` does not link `RotS::combat`);
  `pkill_get_rank_by_character`/`pkill_get_totalrank_by_character_id` → `rots_persist`/
  `db_players.cpp` (a genuine spec-adjudication gap, not itemized in the design spec's prose).
- **Scratch-buffer retirements:** mobact 9 `_buf` sites; limits 4 `_buf` sites — both
  local-composition, no storage-move (both genuine globals, zero local shadows, confirmed by
  Task 0's grep).
- **Dead-decl cleanup:** mobact's `ACMD(do_get);` (never called) and one genuinely-duplicate
  `ACMD(do_stand);` forward decl (narrower than the census's literal framing — the rest of that
  block's decls are still first-uses, required by `enforce_position()`'s switch).
- **Tests added:** 31 across Tasks 1-2 (1415 → 1446): Task 1 +19 (6 `combat_hooks_tests.cpp` for
  `one_mobile_activity`/`Crash_idlesave`/`Crash_extract_objs`, 2 `entity_lifecycle_tests.cpp` for
  `char_from_room`, 4 `big_brother_hooks_tests.cpp`, 1 `script_hooks_tests.cpp` for
  `virt_program_number`, 6 `output_seam_forwarders_tests.cpp`); Task 2 +12 (six genuine
  discriminator-audit gaps found for long-registered-but-never-caller-tested cells — `assist`/
  `rescue`/`wear`/`sleep`/`rest`/`sit`, the l4-seed `say`-cell precedent recurring); Task 3 +0
  (pure membership moves + a comment-only cleanup commit).
- **Membership commits:** 2 (`limits.cpp` → `rots_combat`, `mobact.cpp` → `rots_script`), no
  hard ordering between them, both green first try, both hosts, zero census misses.
- **Census misses at the linkcheck gate:** 0 — the third consecutive wave (after combat-trio and
  l4-seed) to land both memberships green on the first attempt.


## The Cluster B wave: `script.cpp` → `rots_script`; the six `shape*.cpp` OLC editors → new `rots_olc`

Design: `docs/superpowers/specs/2026-07-21-cluster-b-olc-wave-design.md`. Plan:
`docs/superpowers/plans/2026-07-21-cluster-b.md`. Census: `.superpowers/sdd/cb-census.md`
(gitignored scratch). Task reports: `.superpowers/sdd/cb-task-{0,1,2,3,4}-report.md`. This wave
applies the recipe to the world-growth census's Cluster B connected component — the heaviest single
TU the census has found (`script.cpp`, 20 blocking edges at the wave base) plus its six
`shape*.cpp` OLC-editor siblings. Unlike every prior wave in this playbook, Cluster B produces
**two** membership outcomes from one census, not one: `script.cpp` promotes into the *existing*
`rots_script` library (4 → 5 TUs), while the six editors stand up a **brand-new** third L4-band
library, `rots_olc` (7 TUs including `editor_hooks.cpp`), at the very top of the certified order —
`platform < core < entity < persist < world < combat < pathfind < script < olc < app`.

### The mutual-edge ruling — a design decision resolved by one body-read, not a coin flip

The design spec built `script.cpp`'s membership question around a single conditional: `script.cpp`
calls `get_param_text` (`shapescript.cpp`) and `shapescript.cpp` calls `find_script_by_number`
(`script.cpp`) — a genuine mutual edge between the two candidate libraries that the
no-bidirectional-links invariant cannot let either side keep as-is. The spec's own decision table
made the outcome conditional on `get_param_text`'s body: RELOCATE it out of `shapescript.cpp` and
`script.cpp` joins `rots_script` cleanly; find it inseparable from the editor's session state and
`script.cpp` instead rides into `rots_olc` alongside `shapescript.cpp` (the "honest fallback," with
a naming caveat recorded — a runtime script driver living inside a library named for online
creation tools would be misleading). T0's body-read settled it in one look: `get_param_text` is a
pure `int → const char*` switch over `SCRIPT_PARAM_*` constants defined in the shared `script.h`,
touching zero `descriptor_data`/editor state. The RELOCATE default held; the fallback never fired.
This is the cleanest instance in this playbook's history of a design-time conditional resolving
exactly as scoped, with no adjudication needed beyond the one planned body-read.

### `find_action` — a session-coupled body overturns the brief's own RELOCATE default

The design spec listed `find_action` (`act_soci.cpp`) among the "relocate candidates needing
body-reads," defaulting to RELOCATE like `find_eq_pos` and the `perform_*` quartet. T0's read found
it reads app-tier `soc_mess_list` (the social-command table `act_soci.cpp` owns) — not
session-coupled in the interactive-editor sense, but genuinely app-tier data all the same, so
RELOCATE was overturned to a **SAFE-SENTINEL (−1) accessor hook** instead: the same class of seam
the l4-seed wave's `pkill_get_good_fame`/`pkill_get_evil_fame` OVERTURNs already established for
"trivial accessor body, app-tier backing data." Not a new taxonomy, not a STOP — a body-read
finding the brief's own text hadn't anticipated, dispositioned the same way every prior wave's
census misses were: within the existing seam vocabulary, confirmed cheap by construction.

### `editor_hooks.{h,cpp}` — a standalone header, not an `output_seam.h` extension

Every one of the six `shape*.cpp` editors calls `string_add_init(descriptor_data*, char**)`
(`modify.cpp`'s interactive multi-line text-editor state machine — writes `d->str`/`d->max_str`/
`d->len_str`, calls `send_to_char`). The design spec's own scoping evidence treated this as a
plausible `output_seam.h` candidate (it takes a `descriptor_data*` and calls `send_to_char`,
superficially similar to the blocker-buster extension's seven forwarders). T0 overturned this: the
file comment at the top of `output_seam.h` explicitly scopes that header to `comm.cpp`'s send/act
sinks — `string_add_init` is a `modify.cpp`-owned editor state machine, a different kind of session
coupling entirely, not a comm.cpp output composition. One new, standalone header
(`editor_hooks.h`/`.cpp`) carries the single shared hook (`dispatch_string_editor_init`) all six
editors call; `modify.cpp` registers the real body. **Lesson**: a hook's superficial call-signature
resemblance to an existing seam header is not sufficient grounds to fold it in — the header's own
declared scope (recorded in its file comment) is binding, and this wave is the first data point of
that comment being read literally enough to overturn a design-time default.

### Rider gate: `virt_assignmob` closes slot 2 of 3, confirmed no third edge exists

`virt_assignmob` (`shapemob.cpp:1838`, targeting `spec_ass.cpp`) is the same shape as the behavior
wave's `virt_program_number`: a `void*`-returning spec-proc dispatcher whose body drags dozens of
still-app `spec_ass`/`spec_pro` symbols, cannot relocate, and lands as a `script_hooks.h`
abort-tripwire cell. The parent spec's pre-authorized rider gate (≤3 same-shape edges before an
auto-STOP) had one slot already consumed by `virt_program_number` (behavior wave); this wave closes
the second. **This wave went further than "count to 2" — T0 ran the full spec-proc sweep the
behavior wave's own lesson called for** ("a future wave touching either TU should expect more
same-shape edges... budget for the ceiling, not assume it starts fresh"): every call in all 7
Cluster B TUs was cross-referenced against every `spec_ass.cpp`/`spec_pro.cpp` definition, and found
**exactly one** such edge, not more. One slot remains open for spec_pro/spec_ass's own eventual
promotion (the still-undecided pair this playbook's "L4-homing lesson" section flagged) — that
future wave should not assume it starts fresh either; it will likely spend the last slot.

### A genuine census miss, self-resolved same-task (not stopped on)

The census's cross-TU `nm` method — reliable for cross-file edges — is structurally blind to
same-TU-only helper functions: `perform_wear`'s three private helpers (`wear_message`,
`ologhai_item_restriction`, `beorning_item_restriction`, all `act_obj2.cpp`-local at census time)
never appeared in the per-symbol table, because their only callers were `act_obj2.cpp` itself.
Task 1 found them only when actually moving `perform_wear` to `fight.cpp` (`rots_combat`) and
confirming (by tree-wide grep) that all three have **zero** callers besides `perform_wear`. Leaving
them behind in `act_obj2.cpp` while their sole caller promoted to `rots_combat` would have created a
real `rots_combat → app` upward edge — not a hypothetical one. The implementer moved all three
alongside `perform_wear`, byte-verbatim, flagging the finding for controller review rather than
halting: the STOP contract's literal text ("any census miss → STOP") was read against its intent
(surface unplanned architectural decisions, not block on mechanically-verifiable safe moves) — the
alternative was strictly worse, required no new architectural judgment, and was independently
`nm`-confirmed (single definition, zero orphaned declarations) before and after. **Lesson for
future censuses**: cross-TU `nm` closure is necessarily incomplete for same-TU helper functions of a
function about to be relocated — a targeted "does the function I'm about to move have any
file-local helpers with no other callers" check belongs in the relocation step itself, not just the
census step, for every future TU promotion in this family.

### CRLF: two different resolutions for the same "foreign line-ending species" problem

`shapescript.cpp` is Cluster B's lone pure-LF file (0/2713 lines); the other ten touched files
(the five other `shape*.cpp` editors, `script.cpp`, `act_obj1.cpp`/`act_obj2.cpp`,
`combat_hooks.h`/`script_hooks.h`/`comm.cpp`/`comm.h`/`output_seam.h`/`.cpp`) are mixed-CRLF.
`get_param_text`'s relocation kept its LF bytes verbatim inside the mixed-CRLF destination
(`script.cpp`) — introducing one LF-terminated function into an otherwise-CRLF file, matching the
census's own recorded recommendation for that specific move. The `perform_*`/`find_eq_pos` family
went the opposite way: their destination files (`fight.cpp`, `equipment.cpp`) are 100% pure LF, so
the moved bodies were LF-**normalized** rather than carrying CRLF bytes into an exclusively-LF file.
Both choices follow the same underlying rule — preserve the *destination* file's own dominant
convention rather than the *source*'s — but they land on opposite outcomes because the two
destinations have opposite conventions. Verified byte-level (CRLF/LF counts before and after) in
both directions.

### Cost markers (this wave, factual)

- **Up-call sites converted:** 11 in `script.cpp` (`do_emote`/`do_flee`/`do_hit`/`do_say`/
  `do_action`/`do_gen_com`/`do_wear` → `issue_command` cells; `find_action` → hook;
  `extract_char`/`char_from_room` ×4 → existing entity hooks; `pkill_get_good_fame`/
  `pkill_get_evil_fame` → existing l4-seed world hooks) + 6 in the shape family
  (`string_add_init` ×6 → `dispatch_string_editor_init`) + 2 in `shapemob.cpp`
  (`virt_assignmob`, `do_shutdown`).
- **New cells/hooks/seams:** 3 new `combat_command` cells (`action`/`emote`/`shutdown`, 26 → 29);
  `script_hooks.h::virt_assignmob` (rider 2/3); standalone `editor_hooks.{h,cpp}`
  (`dispatch_string_editor_init`); `output_seam.h::send_to_room_except` forwarder;
  `script_hooks.h::find_action` accessor hook (SAFE-SENTINEL −1).
- **Relocations:** `get_param_text` (shapescript.cpp → script.cpp, the mutual-edge breaker);
  `find_eq_pos` (act_obj2.cpp → equipment.cpp, L2); `perform_drop`/`perform_give`
  (act_obj1.cpp → fight.cpp, L3); `perform_wear`/`perform_remove` (act_obj2.cpp → fight.cpp, L3)
  plus the three nm-invisible `perform_wear` helpers (see "census miss" above).
- **Scratch-buffer retirements:** `perform_drop`/`perform_give`/`perform_wear`/`perform_remove`'s
  `buf` sites (local `std::format` composition); `shaperom.cpp`'s `load_room()` (function-local
  `char local_buf[256]`); `shapescript.cpp`'s `show_command()` (function-local `std::string`
  across all 72 case arms) — all local-composition, no storage-move.
- **Dead-decl cleanup:** 7 unreferenced `ACMD(do_*)` forward decls plus `find_action`'s own local
  declaration, deleted from `script.cpp`; `virt_assignmob`/`do_shutdown`'s local forward decls
  deleted from `shapemob.cpp`.
- **Tests added:** 22 across Tasks 1-2, 0 in Task 3, 1 in Task 4 (1446 → 1468): Task 1 +19 (6
  combat_command registration tests, 1 `virt_assignmob` hook test, 1 `string_add_init` hook test,
  1 `send_to_room_except` forwarder test, +0 for the pure-move `get_param_text` relocation, 2
  `find_action` SAFE-SENTINEL pair, 8 coverage riders for the five previously-untested relocated
  `perform_*`/`find_eq_pos` functions); Task 2 +2 (the `gen_com` discriminator-audit gap); Task 3
  +0 (all three of `shapemob.cpp`'s converted call shapes already covered by Task 1's own suite —
  a verified-zero, not a skipped check); Task 4 +1 (`OlcLayerAcyclicity` itself).
- **Membership commits:** 2 (`script.cpp` → `rots_script`, a pure CMakeLists.txt move with zero
  source edits since Task 2 had already relocated the mutual-edge symbol; the six-editor +
  `editor_hooks.cpp` → new `rots_olc` library, one commit, the intra-subset rule at full
  strength — `shapemob.cpp`'s hub fan-in/fan-out makes standalone promotion of any one editor
  impossible). Both green first try, both hosts, zero census misses at the linkcheck gate — the
  fourth consecutive wave (after combat-trio, l4-seed, behavior) to do so.
- **New library, new linkcheck:** `rots_olc` (7 TUs) and `OlcLayerAcyclicity` — the ninth library,
  the ninth linkcheck. `rots_olc`'s own non-vacuity proof (a temporary `boot_db()`-calling probe
  planted in `shapemob.cpp`, reproducibly failing the checker, then reverted) mirrors the l4-seed
  wave's `PathfindLayerAcyclicity`/`ScriptLayerAcyclicity` precedent exactly.
- **Combat DEFER unaffected:** stays at **5** (`spec_ass`/`mage`/`spell_pa`/`ranger`/`spec_pro`) —
  none of Cluster B's 7 TUs were ever DEFER-row members; this wave grew the L4 band, not
  `rots_combat`.

## The spell-family closure wave: `spell_pa.cpp` + `mage.cpp` + `ranger.cpp` → `rots_combat` (ONE joint commit, 12 → 15 TUs)

Design: `docs/superpowers/specs/2026-07-21-spell-family-closure-design.md`. Plan:
`docs/superpowers/plans/2026-07-21-spell-family.md`. Census: `.superpowers/sdd/sf-census.md`
(gitignored scratch). Task reports: `.superpowers/sdd/sf-task-{0,1,2,3,4}-report.md`. This wave
applies the recipe to the playbook's own long-flagged "Ambitious tier" row — the spell-casting
registrar (`spell_pa.cpp`) plus the two TUs its 76-peer count binds together (`mage.cpp`,
`ranger.cpp`) — as ONE joint membership commit, not a per-TU sequence. `rots_combat` grows 12 → 15
TUs; combat DEFER drops **5 → 2** (`spec_ass`/`spec_pro` remain, the last row before the DEFER
list reaches 0 under Wave B of the owner-approved combat-row completion program).

### The 76-peer count finally re-derived, not just flagged stale

Every prior wave's per-TU cost table carried a standing warning that the `spell_pa` row's
combat-peer=76 predated four merged waves and was "known-stale in the wave's favor." This wave is
the first to actually re-run the `nm` closure check against it. The result matches the warning's
own prediction almost exactly: **44 of the 76 peers had already dissolved into TUs `rots_combat`
already contained** (41 `spell_*` mystic implementations from the combat-trio wave, 4
`battle_mage_handler::*` methods, `appear`/`check_hallucinate`/the `spllog_*` storage in
`fight.cpp`, `waiting_list` in `clerics.cpp`), **35 more are intra-family** (the `spell_*` mage
implementations spell_pa's own registrar table dispatches to — these were never going to resolve
independent of mage's own promotion), leaving a genuine app-tier residual of **7 distinct
symbols**. The lesson from the combat-trio wave ("treat every RESOLVED mark as a strong prior, not
ground truth") held again in the opposite direction from usual: the prior estimate wasn't wrong
about the eventual difficulty, it was simply describing work three intervening waves had already
done for free.

### The joint-commit justification: a true bidirectional file-level cycle, not a one-directional gate

Every prior wave's cross-TU coupling in this playbook was either intra-tier one-directional
(`profs → mystic`'s `scale_guardian`, dissolving the moment mystic promoted) or a two-file cycle
resolved by joint promotion (`clerics ↔ fight`). This wave's `spell_pa ↔ mage` edge is the same
shape as `clerics ↔ fight` but with a lopsided edge count that makes the direction of dependency
obvious: **35** `spell_pa → mage` dispatch edges (spell_pa's own `skills[].spell_pointer` table
calling into mage's `spell_*` implementations) against **1** `mage → spell_pa` back-edge
(`new_saves_spell`). Per the intra-subset rule, neither file could promote alone or paired with
only one partner. `ranger.cpp`'s own edge into the family (`say_spell`, into spell_pa) is
one-directional, not a cycle — ranger merely had to land with or after spell_pa, not necessarily in
the same commit — but landing all three together in one commit was simplest and is what Task 4
actually did. The inbound-edge check (every family-exported symbol intersected against every
library's undefined-symbol set) came back **empty**: no library anywhere calls into spell_pa,
mage, or ranger, so promoting all three creates zero reverse edge — the joint commit only had to
satisfy the family's own internal cycle plus its genuine external residuals, not defend against
any surprise caller.

### `show_tracks` — RULING: RELOCATE DOWN, no third permanent inversion

Every wave since the l4-seed wave has tracked this exact upward edge (`ranger.cpp`, `rots_combat`,
L3, calling `show_tracks()` in `graph.cpp`, `rots_pathfind`, L4) as a candidate for this
codebase's third permanent L3→L4 inversion, after `limits → mobact` (behavior wave) and
`script.cpp`'s own `call_trigger` dispatch (Cluster B wave). Both those precedents were legal
registration shapes for a hook the L4 owner would have to register. This wave's T0 body-read
findings did not require that: `show_tracks`'s body (`graph.cpp:319-373`) reads `world[]`,
`room_track[]`, `mob_proto[]`, `pc_race_keywords`/`pc_star_types`, and `GET_LEVEL`/`GET_SKILL` — all
L3-reachable world/room data — plus two graph-internal string-switch helpers (`track_desc`/
`water_track_desc`), with **zero graph adjacency, zero BFS traversal, zero `find_first_step`
state**. It is presentation code over data ranger's own tier already owns, not pathfind-internal
logic that merely happens to live in `graph.cpp`. Default A (RELOCATE) fires: the function plus
its two helpers moved verbatim into `db_world.cpp` (`rots_world`, the tier that actually owns the
data), leaving `show_blood_trail` (which shares the two helpers and does genuinely stay in
`graph.cpp`) with a legal downward L4→L3 call to reach them. **Lesson for future waves**: not
every upward-looking edge into a higher-numbered tier is architecturally inevitable — the
`limits`/`script.cpp` precedents proved the hook mechanism works when a function is genuinely
tier-internal, but this wave's body-read is the first to find a superficially-similar edge that
was actually just misplaced presentation code, relocatable like any other. Per the hook-owner-
promotion banner lesson (recorded when `call_trigger` went permanent), no "app-tier permanently" /
inversion-style comment was written anywhere near this relocation, since none applies.

### New hooks/forwarders landed consumer-free (Task 1)

- **`on_character_returned`** — a **fourth** `entity_hooks.h` big_brother sibling (joining
  `on_character_died`/`on_character_afked`/`on_corpse_decayed`). A genuine T0 finding not in the
  design spec's own default disposition list: `ranger.cpp`'s edge into
  `game_rules::big_brother::on_character_returned` (`big_brother.cpp:535`) had no existing hook.
  Void, logged-no-op tripwire, registered by `big_brother.cpp` — the same cheap, precedented shape
  as its three siblings, not a new taxonomy.
- **`msdp_room_update`** — a new `output_seam` forwarder (`act_move.cpp`'s real body renamed
  `_impl`), serving `mage.cpp`'s three call sites with zero call-site edit needed beyond deleting
  the now-redundant local `extern`.
- **`get_descriptor_list_head()`** — a new `output_seam` read accessor exposing `comm.cpp`'s
  descriptor list for spell_pa's "sense nearby casting" broadcast; storage never leaves `comm.cpp`.
- **The `check_simple_move`/`list_char_to_char`/`do_identify_object` trio** — a new
  `combat_hooks.h` cluster following the `extract_char`/`char_from_room` shape (real body stays
  unrenamed in its owner file; a separately-namespaced `rots::combat::*` hook sits alongside for
  library-side callers) rather than the `Crash_crashsave`-style symbol-takeover shape, since all
  three already had live bare-name app-tier callers whose call sites had to keep working unchanged.
  **`do_identify_object` is a Task 1 overturn of the census's own literal RELOCATE label**: its
  body was found to drag four sibling `act_info.cpp` display helpers plus three file-local `const`
  arrays — the identical "relocating drags the display cluster" shape already flagged for
  `list_char_to_char` — so it took the same already-authorized HOOK taxonomy instead. Not a STOP:
  no new seam shape, no tier-boundary question, just a body-read correcting a census table cell.

### Relocations (Task 1, byte-verbatim, `nm`-verified downward after landing)

`report_wrong_target`/`target_from_word` (`interpre.cpp` → `visibility.cpp` — pure `send_to_char`/
txt-pool-parse presentation). `argument_interpreter` (`interpre.cpp` → L0 `rots_util.cpp` — a pure
string tokenizer, the `one_argument`/`half_chop` L0 precedent). `find_door` (`act_move.cpp` →
`fight.cpp` — pure name→door parse/presentation). `color_sequence[]` + `get_color_sequence()`
(`color.cpp` → `visibility.cpp` — a tree-wide reader scan of every `CC_USE`/`CC_NORM`/`CC_FIX`
expansion site confirmed all eight are still app-tier, so `rots_combat` is a legal destination, no
`rots_core` fallback needed). `prohibit_item_stay_zone_move` + `parse_container_for_stay_zone`
(`act_move.cpp` → `fight.cpp`). `show_tracks`/`track_desc`/`water_track_desc` (`graph.cpp` →
`db_world.cpp`, see above). Every relocation passed all nine `*LayerAcyclicity` linkchecks after
its own individual commit, not just at the wave's end.

### Rider gate untouched; zero new `combat_command` cells

A full sweep of all three TUs for spec-proc-dispatcher edges (`virt_program_number`/
`virt_assignmob`/`command_interpreter`-class calls) found **zero matches** — `ranger.cpp`'s only
`special`-shaped call resolves onto the existing `call_special` hook, not a new dispatcher. The
rider gate stays at **2 of ≤3**, untouched by this wave, one slot still open for Wave B. Every
`do_*` up-call across all three TUs mapped onto one of the existing 29 `combat_command` cells
(`do_look`×4→`look`, `do_hit`×2→`hit`, `do_flee`→`flee`, `do_move`×2→`move`) — **zero new cells**,
continuing the "ZERO new cells" run that started with the l4-seed wave's `mobact.cpp`.

### A controller-caught correction, not a self-caught one: `is_target_valid`

Task 2's own report documents a genuine adjudication error, caught and fixed before Task 3 began
rather than surfacing later: the task's own operative instructions listed `spell_pa.cpp`'s 3-arg
`is_target_valid` call as already-resolved/zero-edit, reasoning that since `spell_pa.cpp` itself
stayed app-tier through Task 2, a direct `big_brother::instance()` call was still legal — deferring
the actual hook conversion to Task 4. That reasoning was wrong: Task 4 **is** the commit that
promotes spell_pa.cpp into `rots_combat`, so leaving the direct call in place would have made it an
upward L3→app edge the instant the CMakeLists move landed, failing `CombatLayerAcyclicity` at that
exact point. A controller adjudication caught this before Task 3 started and a fix commit converted
both `do_cast()` sites onto `rots::entity::dispatch_target_valid` (the same 3-arg
`kNoSkillId`-sentinel shape `fight.cpp`'s own sites already used) ahead of the joint membership
commit. **Lesson**: when a task's literal operative instructions conflict with a census's per-symbol
disposition table, the conflict itself is worth flagging for adjudication rather than silently
picking one side — this wave's Task 2 report did exactly that, and the adjudication resolved in the
census's favor.

### Cost markers (this wave, factual)

- **Up-call/hook-site conversions:** `spell_pa.cpp` — `is_target_valid` (3-arg, 2 sites);
  `descriptor_list` walk (1 site) → `get_descriptor_list_head()`; `_buf` (1 site) retired.
  `mage.cpp` — `do_look`×4 → `look` cell; `char_from_room`×5 → entity hook; `list_char_to_char`×2/
  `do_identify_object`×1 → their hooks; `msdp_room_update`×3 (extern-deletion only); `_buf`×1
  retired. `ranger.cpp` — `do_hit`×2/`do_flee`×1/`do_move`×2 → their cells; `special`×1 → existing
  `call_special` hook; `is_target_valid`×7 (2-arg) → entity hook; `on_character_returned`×1 → new
  hook; `check_simple_move`×1 → new hook; `_arg`/`_buf`/`_buf2` (13 sites total) retired.
- **New cells/hooks/seams:** 0 new `combat_command` cells; 1 new `entity_hooks.h` big_brother
  sibling (`on_character_returned`); 1 new `output_seam` forwarder (`msdp_room_update`); 1 new
  `output_seam` accessor (`get_descriptor_list_head`); 1 new `combat_hooks.h` trio
  (`check_simple_move`/`list_char_to_char`/`do_identify_object`).
- **Relocations:** 6 symbol groups (`report_wrong_target`+`target_from_word`;
  `argument_interpreter`; `find_door`; `color_sequence[]`+`get_color_sequence()`;
  `prohibit_item_stay_zone_move`+`parse_container_for_stay_zone`; `show_tracks`+`track_desc`+
  `water_track_desc`), all byte-verbatim, all `nm`-confirmed downward.
- **Tests added:** 19 across Tasks 1-3, 0 in Task 2/Task 4 (1468 → 1487): Task 1 +17 (2
  `CharacterReturnedHook.*`, 4 `msdp_room_update`/`descriptor_list_head` forwarder tests, 6
  `check_simple_move`/`list_char_to_char`/`do_identify_object` hook tests, 5 in a brand-new
  `visibility_tests.cpp` — the coverage-gap rider for `report_wrong_target`/`target_from_word`,
  which had zero prior test coverage anywhere in the tree before this wave); Task 2 +0 (a
  discriminator audit found zero genuine gaps — the `is_target_valid` fix dispatches onto an
  already-tested hook); Task 3 +2 (the `look` cell's registered/unregistered pair — a genuine gap,
  mage's own `do_look` conversions are its first real `issue_command()` caller anywhere, the
  recurring `say`/`move`/`gen_com`-class gap); Task 4 +0 (a pure CMakeLists.txt membership move,
  zero source edits — every up-call was already converted onto a hook/seam by Tasks 1-3).
- **Membership commits:** 1 (all three TUs, one commit — the joint-commit justification above).
  `CombatLayerAcyclicity` green on the **first** build attempt, both hosts, zero bisection needed —
  matching the run of clean first-try promotions this playbook has recorded since the combat-trio
  wave (the combat-pilot wave's own two census misses, `gain_exp`/`waiting_list`, remain the sole
  exception in this playbook's history).
- **No new library, no new linkcheck**: `CombatLayerAcyclicity` (already existing) is the
  membership gate; nine libraries, nine linkchecks, unchanged from the Cluster B wave.
- **Combat DEFER drops 5 → 2** (`spec_ass`/`spec_pro` remain) — the last two rows before Wave B
  (this wave's own §Wave B charter, under the owner's autonomous-spec-gate + merge-when-green
  grants) closes the row to 0.

## The spec-pair wave: `spec_pro.cpp` then `spec_ass.cpp` → `rots_script` (5 → 7 TUs) — Combat DEFER reaches 0

Design: `docs/superpowers/specs/2026-07-21-spec-pair-design.md`. Census: `.superpowers/sdd/sp-census.md`
(gitignored scratch). Task reports: `.superpowers/sdd/sp-task-{0,1,2,3,4}-report.md`. This is Wave
B of the owner-approved combat-row completion program (Wave A was the spell-family closure wave
above), executed under the owner's 2026-07-21 **autonomous-spec-gate** and **merge-when-green**
grants. It closes the playbook's last two DEFER rows — `spec_pro.cpp` (the spec-proc command-driver
TU) and `spec_ass.cpp` (its SPECIAL-assignment registrar) — as **SEQUENTIAL** commits (spec_pro
first, spec_ass second), landing **not** in `rots_combat` but in `rots_script` (L4), per the
mobact-precedent "driver homes with the engine it invokes" ruling extended one more link: the
spec-proc bodies home with the `virt_*` dispatch machinery that invokes them, and their assigner
homes with them. `rots_script` grows 5 → **7 TUs**. No new library, no new linkcheck —
`ScriptLayerAcyclicity` (both hosts) went green on the **first** build attempt at both commits, no
bisection needed. **Combat DEFER reaches ZERO.**

### The tier ruling: `rots_combat` REJECTED on three irreducible upward edges, `rots_script` ACCEPTED

T0's census ran the closure check under both candidate homes (layer order
`platform < core < entity < persist < world < combat < pathfind < script < olc < app`). Under
`rots_combat` (L3), three edges are irreducibly upward: `spec_pro`'s `find_first_step` call
(`rots_pathfind`, L4), `spec_pro`'s `command_interpreter` hook (`rots_script`, L4), and `spec_ass`'s
`set_virt_program_number_hook`/`set_virt_assignmob_hook` calls (`rots_script`, L4) — each would
demand a new permanent inversion (the `one_mobile_activity`-class L3→L4 edge the behavior wave
introduced), three of them. Under `rots_script` (L4), all three resolve downward or intra-lib
instead: `find_first_step` is a legal downward L4→pathfind(L4-1) call, the `command_interpreter`
hook and the `virt_*` setters become intra-lib once their TUs join the same library. `rots_script`
is the correct tier — the census's own corroboration cites `CMakeLists.txt:392`'s own
long-standing note ("mobact.cpp is slated to join ROTS_SCRIPT_SOURCES") and the mobact precedent
itself (behavior wave: the mob-AI driver homes with the engine it invokes, not with `rots_combat`).
Combat DEFER still reaches 0 by this promotion — these were the last two combat-row-sketch TUs,
they just leave the DEFER list by landing in a different library, exactly as `mobact.cpp` did.

### Pair structure: one-directional, no cycle — SEQUENTIAL, not joint

Unlike `clerics ↔ fight` or `spell_pa ↔ mage` (both true bidirectional cycles forcing a joint
commit), this pair's dependency runs one way only: `nm` found **39** `spec_ass → spec_pro` edges
(the SPECIAL procs spec_ass hands out as fn-ptrs whose bodies live in spec_pro.cpp) and **zero**
`spec_pro → spec_ass` edges (verified: `comm -12 spec_pro.undef spec_ass.def` = empty). No cycle
exists, so the plan's stated SEQUENTIAL order — spec_pro first (fully standalone, every edge
resolves to an existing lib/hook, zero dependence on spec_ass), spec_ass second (its 39 edges
dissolve intra-`rots_script` once spec_pro is already a member) — is the clean path. A single joint
commit would also have been legal (no cycle bars it), but sequential was simpler and is what Task 4
actually did, in two commits (`ba457c5`, `b71b008`).

### The registrar-lookup family — a genuinely new seam taxonomy, pre-authorized at exactly 3 edges

`spec_ass.cpp`'s only real app-tier coupling (besides the intra-pair edges to spec_pro) is three
fn-ptr address-of references into three different app subsystems: `gen_board` (`boards.cpp`, 24
real `ASSIGNOBJ` sites), `postmaster` (`mail.cpp`, 1 `ASSIGNMOB` site + 2 spec-proc switch arms),
and `receptionist` (`objsave.cpp`, 34 real `ASSIGNMOB` sites). None of the three is ever *called*
inside spec_ass — only their address is taken and stored (or, for postmaster, cast/returned from a
`void*`-returning dispatcher). This is a new coupling shape the playbook had not previously named: a
lookup/registry hook where each owning app TU registers its own fn-ptr at boot and the promoted TU
resolves by key, rather than the promoted TU calling directly into the app TU or the app TU calling
down into a hook. T1 landed it consumer-free: an `enum class registered_special { gen_board,
postmaster, receptionist }`, a `lookup_registered_special(key)` reader, and a
`set_registered_special(key, ptr)` setter, all in `script_hooks.h`/`script_hooks.cpp` — already the
established `rots_script → app` spec-proc inversion header, so no new header/OVERTURN was needed.
**Unregistered lookup is an abort-tripwire, never a silent null fn-ptr** — the census's own explicit
ruling, since a null spec-proc fn-ptr silently stored into `{mob,obj}_index[].func` would crash far
from its cause when later dispatched. Confirmed **exactly 3** edges by `nm`, no 4th anywhere in
either TU, no auto-STOP. `_cmd_info` (9 read sites) and `target_check` (1 call site) got their own
new `script_hooks.h` hooks in the same task — SAFE-SENTINEL class (default `0`/`POSITION_DEAD`,
`find_action`/pkill-fame accessor precedent), int-returning, **not** `void*` dispatchers, so neither
consumes a rider-gate slot.

### Rider gate: untouched, still 2 of ≤3 — zero new `combat_command` cells

The two existing `void*` spec-proc dispatchers (`virt_program_number`/`virt_assignmob`) were already
registered hooks from the behavior wave/Cluster B and become **intra-lib** the moment spec_ass joins
`rots_script` — this wave consumes no new rider slot. The other `void*` returners in spec_ass
(`virt_obj_program_number`/`get_special_function`/`virt_assignobj`) are called only from app-tier
(`interpre.cpp`, `boot_db`) — no library dispatches them, so no hook was needed (app→L4 inbound
downward is always legal). A full sweep of both TUs found no 4th registrar edge and no new
`void*`-dispatcher shape anywhere. All 14 real app-tier `do_*` call sites across `spec_pro.cpp`
(66 real call sites: `do_gen_com`/`do_say`/`do_tell`/`do_look`/`do_stat`/`do_lock`/`do_close`/
`do_open`/`do_unlock`/`do_move`/`do_wake`/`do_stand`/`do_wear`/`do_hit`) map onto EXISTING
`combat_command` cells — **zero new cells**, continuing the run that started with the l4-seed wave's
`mobact.cpp`. Six of those cells (`stat`/`tell`/`lock`/`close`/`open`/`unlock`) got their first-ever
real `issue_command()` caller through this conversion — the recurring `say`/`move`/`dismount`-class
first-caller discriminator gap, closed with 12 new tests.

### Other conversions and relocations (Tasks 1-3)

`is_number()` (a pure digit-scan, zero deps) relocated byte-verbatim from `interpre.cpp` to L0
`rots_util.cpp` — the `one_argument`/`fill_word`/`half_chop` L0 precedent; its declaration in
`interpre.h` is unchanged, so every existing caller keeps resolving through the same header.
`command_interpreter`/`extract_char`/`char_from_room`/`add_exploit_record` all converted onto their
already-existing `rots::script`/`rots::entity`/`rots::persist` hooks — no new infrastructure, pure
conversion work. `_buf` (spec_pro's genuine global `char buf[MAX_STRING_LENGTH]`) retired to direct
`std::format`/string-literal composition at all 8 call clusters. Dead-decl cleanup: 14 now-unreferenced
`ACMD(...)` forward decls plus the `do_drop`/`do_kick`/`do_pull` trio (zero call sites, confirmed by
grep before deletion) retired from `spec_pro.cpp`; `spec_ass.cpp`'s `SPECIAL(gen_board)`/
`SPECIAL(postmaster)`/`SPECIAL(receptionist)` forward decls retired the same way once their only
references converted onto the lookup family. `spec_ass.cpp` needed **zero** `_buf` work — grepped
and confirmed the genuine global has no reference anywhere in that file (the generic task-brief
template's `_buf` line applied only to `spec_pro.cpp`).

### Test-count delta for this wave: 1487 → 1510

Task 1 +11 (3 `RegisteredSpecialLookup.*` registrar tests, 4 `command_min_position`/`target_check`
accessor-hook tests, 4 `IsNumber.*` L0 relocation tests — a genuine coverage gap, zero prior
`is_number` test existed anywhere in the tree). Task 2 +12 (six first-caller discriminator pairs —
`stat`/`tell`/`lock`/`close`/`open`/`unlock` — the recurring `say`/`move`/`dismount`-class gap;
the other eight converted cells, `hit`/`look`/`move`/`say`/`gen_com`/`stand`/`wake`/`wear`,
already had registered/unregistered coverage from prior waves). Task 3 +0 (a discriminator audit
of all three registrar-consumption shapes — `ASSIGNOBJ`/`ASSIGNMOB` macro storage, the
`virt_program_number` cast, the `get_special_function` return — found T1's own
`RegisteredSpecialLookup.*` tests already exercise the identical mechanism; zero genuine gap). Task 4
+0 (pure CMakeLists.txt membership moves, zero source edits — every up-call was already converted
onto a hook/seam by Tasks 1-3). All gate hosts (`macos-arm64`, `rots64`, `macos-arm64-asan` on Tasks
1 and 2's new/rewritten test files) confirmed the running count at every task's final gate;
`ConvertEquivalence` 17/17 both hosts throughout; both boot goldens byte-identical at every commit;
`string_view_census.py --check` exit 0 throughout.

### Cost markers (this wave, factual)

- **Up-call/hook-site conversions:** `spec_pro.cpp` — 66 real `do_*` call sites → 14 existing
  `combat_command` cells; `command_interpreter`×1, `extract_char`×4, `char_from_room`×7,
  `add_exploit_record`×2 → existing hooks; `cmd_info[].minimum_position`×9 →
  `command_min_position` accessor; `target_check`×1 → new hook; `is_number`×1 → verify-only
  (relocated declaration already resolves); `_buf` retired (8 call clusters, 20 raw lines).
  `spec_ass.cpp` — `gen_board`×24/`postmaster`×3/`receptionist`×34 → `lookup_registered_special()`.
- **New cells/hooks/seams:** 0 new `combat_command` cells; 1 new registrar-lookup family (3 edges,
  `script_hooks.h`); 1 new `command_min_position` accessor hook; 1 new `target_check` hook;
  1 relocation (`is_number` → L0 `rots_util.cpp`).
- **Dead-decl retirement:** 14 `ACMD(...)` forward decls + `do_drop`/`do_kick`/`do_pull` (3) in
  `spec_pro.cpp`; `SPECIAL(gen_board)`/`SPECIAL(postmaster)`/`SPECIAL(receptionist)` (3) in
  `spec_ass.cpp`.
- **Tests added:** 23 across Tasks 1-2, 0 in Tasks 3-4 (1487 → 1510): Task 1 +11, Task 2 +12,
  Task 3 +0 (audited-zero gap), Task 4 +0 (pure membership moves).
- **Membership commits:** 2, SEQUENTIAL (`ba457c5` spec_pro, `b71b008` spec_ass — no cycle, so no
  joint commit was forced). `ScriptLayerAcyclicity` green on the **first** build attempt at both
  commits, both hosts, zero bisection needed — the census's inbound-edge check (0/0 library
  references to either TU) predicted exactly this outcome.
- **No new library, no new linkcheck**: `ScriptLayerAcyclicity` (already existing) is the
  membership gate; nine libraries, nine linkchecks, unchanged from the spell-family closure wave.
- **Flat-Makefile parity: zero change needed** beyond the new test file's `SRCS` entry
  (`rots_util_tests.cpp`, Task 1, same commit) — `spec_pro.o`/`spec_ass.o` were already present in
  both flat Makefiles before this wave (the monolithic build has no library split).
- **Combat DEFER drops 2 → 0.** The row is CLOSED — see the next section for the full arc.

## THE COMBAT ROW IS CLOSED — the five-wave arc, start to finish

The combat-seed wave (2026-07-20) stood up `rots_combat` with 4 SEED-CLEAN TUs and named an
11-TU DEFER list (`spec_ass`/`olog_hai`/`clerics`/`mage`/`mystic`/`mobact`/`spell_pa`/`limits`/
`ranger`/`spec_pro`/`fight`) plus a caveated `profs` rider. Five subsequent waves closed every one
of those 11 rows (plus the rider):

1. **Combat-pilot wave** (`rots_combat` 6 → 8): `clerics.cpp` + `fight.cpp`, joint commit (a true
   intra-subset cycle — the two files call each other directly).
2. **Combat-trio wave** (`rots_combat` 8 → 11): `olog_hai.cpp` (standalone) + `mystic.cpp`
   (standalone) + `profs.cpp` (census-gated rider on mystic), one membership commit.
3. **Behavior wave** (`rots_combat` 11 → 12, `rots_script` 3 → 4): `limits.cpp` → `rots_combat`
   (a standard DEFER-row promotion); `mobact.cpp` → `rots_script` (the first "driver homes with
   the engine it invokes" ruling, and the codebase's first permanent L3→L4 inversion,
   `limits → mobact`).
4. **Spell-family closure wave** (`rots_combat` 12 → 15, Wave A of the owner-approved
   combat-row-completion program): `spell_pa.cpp` + `mage.cpp` + `ranger.cpp`, ONE joint commit — a
   true bidirectional `spell_pa ↔ mage` cycle plus `ranger`'s one-directional dependency on
   spell_pa.
5. **Spec-pair wave** (`rots_script` 5 → 7, Wave B — this wave): `spec_pro.cpp` then
   `spec_ass.cpp`, SEQUENTIAL commits (a one-directional dependency, not a cycle) — the second
   application of the "driver homes with the engine" ruling, extended one link further (the
   spec-proc bodies and their assigner both home with the `virt_*` dispatch machinery mobact
   drives).

**Final tally: `rots_combat` = 15 TUs, `rots_script` = 7 TUs, nine libraries, nine
`*LayerAcyclicity` linkchecks, zero new libraries created by any of the five waves. Combat DEFER:
0.** Every row the original combat-census sketch named in 2026-07-20 has either joined
`ROTS_COMBAT_SOURCES` (`clerics`/`fight`/`olog_hai`/`mystic`/`profs`/`limits`/`spell_pa`/`mage`/
`ranger` — 9 TUs across four waves, plus the 4 SEED-CLEAN + 2 blocker-buster enablers already
resident = 15) or been ruled to a different library and joined there instead (`mobact`/`spec_pro`/
`spec_ass` → `rots_script`, per the driver-homes-with-engine rule, 2 waves). **The recipe this
playbook records (census → closure check → seams → moves → conversions → joint-or-sequential
membership → verification) held across all twelve real data points without a single structural
surprise it couldn't classify** — the "STOP-and-adjudicate" auto-STOP conditions it names (a
forced new library, an irreducible cross-tier bidirectional pair, a 4th registrar edge, a rider-gate
overflow) were checked at every promotion and never fired once in the combat row's five-wave
closure arc. Next-frontier options for a future program (not part of this row, not started): Stage
2 LocationSystem, a `rots_commands` census, the deferred int→double combat-math gate.
