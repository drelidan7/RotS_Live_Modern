# Combat-row migration playbook (draft)

**Status:** Confirmed by Task 5 (fight.cpp call-site conversions + joint
clerics.cpp+fight.cpp membership move), the combat-pilot wave's final task. Written
after Task 3 (clerics.cpp call-site conversions) and revised with real Task 5 data
below rather than left as a Task-3-only guess. Recorded factually from the ACTUAL
recipe each task followed — not a prescriptive design written in advance — so a
later wave repeating this pattern for another `rots_combat` DEFER-11 row
(`spec_ass`/`olog_hai`/`mage`/`mystic`/`mobact`/`spell_pa`/`limits`/`ranger`/
`spec_pro`, per `AGENTS.md`'s DEFER TU list — `fight` is no longer on this list,
having landed as of this task) can compare its own experience against two real data
points instead of one.

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

## Task 5 (fight.cpp conversions + joint membership) — confirms most of the above, corrects one thing

Ran as two commits per the brief: (a) fight.cpp's up-call conversions (17 sites
across 8 symbol families), gated green while fight.cpp was still `ROTS_SERVER_SOURCES`;
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

- **Up-call sites converted (fight.cpp, Step 1):** 17 across 8 symbol families —
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

## Status for the 9 remaining DEFER-11 rows

`spec_ass`/`olog_hai`/`mage`/`mystic`/`mobact`/`spell_pa`/`limits`/`ranger`/
`spec_pro` remain app-compiled. Each promotion should: (1) read that row's
`combat-census.md`/`pilot-census.md`-equivalent per-symbol table, not re-derive it;
(2) build any missing seam BEFORE the conversion commit, not during; (3) budget for
coupled dead-code cleanup riding the same diff as each converted call site; (4)
byte-edit via Python for any CRLF file (verify first, don't assume LF); (5) run the
actual `*LayerAcyclicity` linkcheck before declaring a membership move done, even
for files a PRIOR task already fully converted — a census's non-blocking
classification for a same-wave sibling TU's symbol is not proof until that sibling
has actually promoted too.
