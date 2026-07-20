# Combat-row migration playbook (draft)

**Status:** DRAFT, written after the combat-pilot wave's Task 3 (clerics.cpp call-site
conversions). Recorded factually from the ACTUAL recipe that task followed — not a
prescriptive design written in advance — so a later wave repeating this pattern for
another `rots_combat` DEFER-11 row (`spec_ass`/`olog_hai`/`mage`/`mystic`/`mobact`/
`spell_pa`/`limits`/`ranger`/`spec_pro`/`fight`, per `AGENTS.md`'s DEFER TU list) can
compare its own experience against a real one instead of a guess. Will be revised as
later tasks in this wave (Task 4: fight.cpp; Task 5: joint clerics+fight membership
move) land and either confirm or correct it.

## The recipe, as it actually happened

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

- `clerics.cpp` calls seven `fight.cpp`-defined functions directly:
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
