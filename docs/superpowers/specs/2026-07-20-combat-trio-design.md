# Combat-Trio Wave (olog_hai + mystic join rots_combat, profs conditional) — Design

**Date:** 2026-07-20 · **Branch:** `arch/combat-trio`, off master @26afa9a (post combat-pilot
merge) · **Predecessors:** the combat-pilot wave (`clerics.cpp`+`fight.cpp`, `rots_combat` 6 → 8
TUs) and its finalized recipe in `docs/superpowers/combat-migration-playbook.md`. This is the
**second** application of that playbook — the first to promote TUs standalone rather than as a
mutually-dependent pair, and the first to carry a census-gated conditional rider.

## Problem / evidence

The combat-pilot wave proved the four blocker-buster seams under real traffic and wrote the
migration playbook, but its two pilots (`clerics`/`fight`) were a **mutual** cycle that had to
promote jointly. Nine DEFER TUs plus the caveated `profs` remain in `ROTS_SERVER_SOURCES`. The
playbook's per-TU cost table (`docs/superpowers/combat-migration-playbook.md`, "Per-TU cost table
for the remaining DEFER TUs") names **`olog_hai` "the closest row to SEED-CLEAN of the 9"** and
`mystic` a mid-weight row whose membership incidentally **dissolves `profs`'s only real coupling**.
That table is an explicit *strong prior, not ground truth* (its own "Method and caveat" says a
fresh `nm` re-run is mandatory before any promotion), so the counts below are source-grepped
hypotheses this wave's Task 0 re-derives from `nm` before any code moves.

Source-level census (grep + body reads at 26afa9a, to be `nm`-confirmed in Task 0):

- **`olog_hai.cpp`** (602 lines) — **cross-TU-independent of both mystic and profs** (no shared
  symbols either direction). Resolved by prior waves: `get_char_room_vis`/`CAN_SEE`
  (olog_hai.cpp:113/146 → `visibility.cpp`, in-lib), `send_to_char` (~29 sites → `output_seam`,
  same-symbol, no edit). Residual: `do_move` (olog_hai.cpp:544, ACMD shape → the existing `move`
  cell), **`do_dismount` (olog_hai.cpp:252 — NOT one of the 25 cells; needs a 26th)**,
  `one_argument` ×5 (interpre.cpp parse-leaf), `big_brother::is_target_valid` ×4 (the pilot's
  `entity_hooks.h` pair), and `buf`/`buf2`/`arg` global-scratch retirement (10/3/5 genuine sites,
  no local shadows). A file-local `is_target_valid` free function (olog_hai.cpp:108) is distinct
  from the big_brother method — a self-call, no conversion.
- **`mystic.cpp`** (1851 lines) — **no reverse dependency on profs**; the only trio coupling is
  `profs → mystic`. Resolved by prior waves: `stop_riding`/`stop_follower`/
  `remove_character_from_group` (all L2), `set_mental_delay` (`fight.cpp`, in-lib since pilot Task
  4a), `get_char_room_vis` (in-lib), `send_to_room` (`output_seam`). Residual: `add_follower`
  (mystic.cpp:1675, handler.cpp:267), `one_argument`/`half_chop` (interpre.cpp parse-leaves),
  `do_flee` (mystic.cpp:202 → `flee` cell), `search_block` (already L0 `rots_util`), one genuine
  `buf` site (mystic.cpp:620). `scale_guardian` (mystic.cpp:1584) is mystic-internal.
- **`profs.cpp`** (739 lines, conditional rider) — one genuine coupling: `scale_guardian`
  (profs.cpp:233, defined mystic.cpp:1584). Other blockers: `get_guardian_type` (profs.cpp:226,
  defined utility.cpp:978), `add_exploit_record` ×8 (existing `persist_hooks.h` dispatch), 2
  genuine `buf` sites (profs.cpp:428-429). All other `buf`/`buf2` uses are parameter/local shadows
  (`draw_line`/`draw_coofs`, profs.cpp:166/177/179), not the globals.

**Why olog_hai + mystic, in this order:** olog is the cleanest remaining row and has zero
intra-trio edges — the first true **standalone** promotion since the seed wave, a data point the
playbook lacks (every prior promotion was either seed-clean-in-isolation or the clerics/fight
cycle). mystic adds a mid-weight row and, by joining, retires `profs`'s scale_guardian blocker.

## Decision (owner-approved)

**Scope: `olog_hai.cpp` + `mystic.cpp` join `rots_combat` (8 → 10 TUs), with `profs.cpp` as a
census-gated conditional rider (→ 11 TUs if it fires).** No new smoke/verification infrastructure —
the pilot's `scripts/combat-golden.sh` (informational, rung-(b) capture-only) and dispatch-
equivalence discriminators carry forward unchanged.

### The rider gate (Task 0 decides from `nm` ground truth; source evidence predicts the outcome)

`profs.cpp` rides **only if**: `scale_guardian` is standalone-relocatable **OR** mystic's own
membership dissolves profs's `scale_guardian` edge — **AND** profs's other blockers
(`get_guardian_type` relocation, `buf` retirement, `add_exploit_record`) resolve cheaply. Else
profs drops out **loudly** (named in the Task 0 report and the wave's as-built), and the wave lands
olog+mystic (8 → 10) — or, if Task 0 finds mystic itself blocked, olog alone.

**Source-level prediction (Task 0 confirms/overturns):** rider **fires** via the second path.
- `scale_guardian` is **NOT** standalone-relocatable: its body (mystic.cpp:1584-1610) calls four
  mystic-internal helpers (`set_guardian_stats`/`tweak_aggressive_guardian_stats`/
  `tweak_defensive_guardian_stats`/`tweak_mystic_guardian_stats`, mystic.cpp:1497-1583) — moving it
  means moving that 5-function cluster, not a `saves_power`-style leaf. Path A fails.
- **mystic's membership dissolves the edge:** once mystic joins `rots_combat` (L3), `profs →
  scale_guardian` resolves as intra-lib (if profs rides) or as a legal app→lib downward call (if
  profs stays app). Path B holds — a one-directional edge, categorically unlike the clerics↔fight
  cycle that forced joint membership.
- **profs's other blockers are cheap:** `get_guardian_type` (utility.cpp:978) is a zero-function-
  call leaf reading only `mob_index` (db_world.cpp:95, L3-world) and `guardian_mob` (consts.cpp:2620,
  L1-core) — relocatable into `rots_combat` (which PUBLIC-links both `RotS::world` and `RotS::core`;
  its other caller objsave.cpp:774 then calls down, legal); `buf` retirement is 2 sites;
  `add_exploit_record` uses the dispatch the pilot already externally linked.

### Task skeleton

- **T0 — Fresh census + closure + adjudications (read-only).** `nm -u` re-census of
  `olog_hai.cpp`/`mystic.cpp`/`profs.cpp` at 26afa9a against a symbol→object map over all six
  library object dirs + `ageland.dir` (the `pilot-census.md` method). **Full closure check
  (playbook recipe step 2):** classify every combat-peer(still-app) edge and confirm each resolves
  to (a) an in-lib TU, (b) a same-commit promoting partner, (c) an existing seam/relocation, or is
  a genuine blocker needing disposition. Re-derive olog's old "combat-peer=6" / mystic's "=8"
  (`docs/superpowers/combat-migration-playbook.md` cost table) — likely now-in-lib (clerics/fight)
  or dispatch-indirected, but any edge to a still-app DEFER TU (spell_pa/mobact/ranger/limits) is a
  STOP-and-adjudicate. **Decides the profs rider gate** and adjudicates the defaults below.
- **T1 — Seams + shared relocations + scratch retirements (consumer-free, all TUs still
  app-compiled).** Add the 26th `dismount` `combat_command` cell + registration + discriminator;
  relocate the `one_argument`/`half_chop` parse-leaf package(s) to L0 `rots_util` (the
  `search_block` precedent); relocate `add_follower` to L2 `entity_lifecycle.cpp`; `buf`/`buf2`/
  `arg` retirements for olog_hai and mystic (byte-preserving, world-seed pattern). Each lands and
  gates green before any call-site converts to it.
- **T2 — olog_hai + mystic conversions (still app-compiled).** olog: `do_move`→`move` cell,
  `do_dismount`→`dismount` cell, `is_target_valid` ×4 → big_brother hook, `one_argument` now a
  downward L0 call. mystic: `do_flee`→`flee` cell, `one_argument`/`half_chop` downward. Positionally
  exact substitutions; coupled dead-code cleanup budgeted (playbook recipe step 5). Discriminator
  audit per newly-exercised cell/hook.
- **T3 — profs rider work (ONLY if T0 fired the rider; else skipped, renumber gracefully).**
  Relocate `get_guardian_type` (utility.cpp → `rots_combat`); profs `buf` retirement (2 sites);
  `add_exploit_record` ×8 → `persist_hooks.h` dispatch; discriminator audit.
- **T4 — Membership + linkcheck.** olog_hai + mystic (+ profs if rider) leave `ROTS_SERVER_SOURCES`
  for `ROTS_COMBAT_SOURCES` (CMake only — flat Makefiles need zero change for pre-existing files,
  per the pilot's checked finding). `CombatLayerAcyclicity` green both hosts. **Closure structure
  documented honestly:** this is NOT a cycle-forced joint move — olog is independent, mystic is
  independent, profs is one-directionally gated on mystic; one membership commit is chosen for
  simplicity, not because the linkcheck requires it.
- **T5 — Docs as-built + playbook row updates.** BUILD.md `rots_combat` (10 or 11 TUs), AGENTS.md
  totals/description, playbook cost-table rows for olog_hai/mystic/profs marked RESOLVED, parent
  spec §3/§10 additive note. Coverage-gap riders if T0 surfaced untested live code.
- **T6 — Finalization.** i386 battery (`scripts/i386-battery.sh`), whole-branch review, fix wave,
  push + PR + six blocking CI jobs + owner merge.

### Adjudication defaults (T0 confirms or overturns with `nm` evidence)

- **`do_dismount`** — add a 26th `dismount` `combat_command` cell (olog_hai.cpp:252 is the only
  known direct up-call; T0 confirms no others). Registered in `interpre.cpp`'s
  `register_combat_command_dispatch()` pointing at the real `do_dismount`. The cell's fn-ptr
  indirection also *breaks the direct combat-peer link*, so olog_hai.o carries no undefined
  `do_dismount` after conversion regardless of which still-app TU defines it.
- **`do_move`** — the existing `move` cell (combat_hooks.h:118); T0 verifies the cell is registered
  to `do_move` and olog_hai.cpp:544's shape matches the ACMD signature.
- **`one_argument`/`half_chop`** — relocate to L0 `rots_util` as text-parse leaves (bodies at
  interpre.cpp:1476/1535). `one_argument` travels as a 3-item package with `fill_word`
  (interpre.cpp:1511) and the `fill[]` table (interpre.cpp:581), since `fill_word` reads it;
  `search_block` is already L0. T0 censuses `half_chop`'s own downward edges before choosing.
- **`add_follower`** — relocate to L2 `entity_lifecycle.cpp` (handler.cpp:267). Its
  pool (`get_from_follow_type_pool`) and `stop_follower` are already L2; its only apparent upward
  edge, `act()` (×3), is already an `output_seam` L1 forwarder — no hook needed. T0 confirms no
  handler-internal-static snag.
- **`get_guardian_type`** — relocate into `rots_combat` (L3), NOT L2: it reads `mob_index`
  (L3-world). Rider-conditional (only needed if profs promotes). objsave.cpp:774 then calls down.
- **`scale_guardian`** — stays in mystic.cpp (not leaf-relocatable); dissolved for profs by mystic's
  membership.

## Verification

- Byte-verbatim mandate for every body move / relocation (python byte-edits where CRLF or the
  formatter hook threatens bytes — the established method; reviewers re-diff moved spans).
- Dispatch-equivalence discriminator (registered-reaches-real-body / unregistered-tripwire pair)
  for every newly-converted cell/hook class: the new `dismount` cell at minimum; audit `move`/
  `flee`/`is_target_valid` as already-covered (a real read, not an assumption — playbook recipe
  step 7).
- Per-task gates both hosts: macOS arm64 + rots64 build + full ctest (**1394 baseline** + this
  wave's additions), all `*LayerAcyclicity` linkchecks, both boot goldens,
  `ConvertEquivalence` 17/17, `python3 tools/string_view_census.py --check` exit 0. New/rewritten
  test files add the `macos-arm64-asan` preset. `scripts/combat-golden.sh verify` runs
  **informationally** (rung-(b), non-gating) from T2 on. i386 battery at finalization only.
- Characterization goldens (`CharacterizationCombatTest.*`, `PoisonRemovalScriptTest.*`) pass
  **unchanged** — drift = a bug in the change.

## Risks

- **mystic's combat-peer set is the wave's primary STOP-risk.** The cost table lists mystic
  combat-peer=8 without enumerating them; mystic is a spell TU that may reference still-app
  `spell_pa`/other-caster symbols the linkcheck won't resolve (exactly the census-methodology-
  correction class the pilot hit with `gain_exp`/`waiting_list`). T0's full closure check runs
  before any membership; any such edge STOPs for a relocate-or-hook disposition, growing T1 scope
  or dropping mystic from the wave.
- **Rider entanglement.** If T0 finds `get_guardian_type` cannot cleanly relocate (e.g., an
  unexpected upward edge) or mystic itself blocked, profs drops loudly rather than being forced.
- **`add_follower` handler-internal state.** A hidden static or handler-only helper would flip it
  from L2-relocate to hook; T0 body-reads before T1.
- **fight.cpp blast radius does not apply** — none of the three TUs is the melee hub; olog/mystic
  are skill/spell drivers, profs is leveling.

## Out of scope

- No other DEFER TU (`mobact`/`spec_pro`/`ranger`/`mage`/`limits`/`spell_pa`/`spec_ass`) — they
  stay app-compiled; same-tier calls remain legal.
- No new smoke/determinism work — the pilot's harness carries forward as-is.
- No `spell_pa`/`spec_ass` registrar-hub promotion (they land last, per the playbook).
- No wholesale handler.cpp/utility.cpp extraction beyond the four named leaf relocations.
- No int→double combat-math changes.

## As-built (Tasks 0-4 complete)

**Status: LANDED.** `rots_combat` grew 8 → 11 TUs (`019b4c8`): `olog_hai.cpp` + `mystic.cpp` +
`profs.cpp`, one membership commit, `CombatLayerAcyclicity` green **first try**, both hosts — zero
census misses at the linkcheck gate, the first time this recipe has achieved that (contrast the
combat-pilot wave's two, `gain_exp`/`waiting_list`). Test count: 1394 → 1398 (Task 1 +2 `dismount`
discriminator tests, Task 2 +2 `move` discriminator tests, Tasks 3-4 +0). Full evidence:
`.superpowers/sdd/combat-trio-census.md` (Task 0), `.superpowers/sdd/trio-task-{0,1,2,3,4}-report.md`.

**The rider FIRED, via the predicted Path B, exactly as this document's source-level prediction
called it.** Task 0's `nm`-confirmed body read (census §5.6) verified Path A (standalone-relocating
`scale_guardian`) genuinely fails — the helper cluster is 6 functions
(`set_guardian_stats`/`calc_guardian_hp`/`set_guardian_health`/`tweak_aggressive_guardian_stats`/
`tweak_defensive_guardian_stats`/`tweak_mystic_guardian_stats`), one more than this document's stated
"four" (`calc_guardian_hp`/`set_guardian_health` were omitted from the original enumeration — doesn't
change the verdict, corrects the count). Path B held as predicted: mystic's own closure check fully
resolved (its named primary STOP-risk — 5 unenumerated `spell_pa.cpp` combat-peer edges,
`saves_confuse`/`saves_insight`/`saves_leadership`/`saves_mystic`/`saves_poison` — turned out
RELOCATE-CLEAN via the `saves_power` precedent), so mystic's promotion dissolves `scale_guardian` for
any caller. Task 0 found a caller this document's prediction missed: `objsave.cpp:775`, a second
external `scale_guardian` caller alongside the documented `profs.cpp:233` — doesn't change the
verdict, Path B covers any caller, but the design's own claim that "profs.cpp:233 is the only
external caller" was wrong.

**Census overturns (Task 0, `.superpowers/sdd/combat-trio-census.md` §7 has the full list):**
1. olog_hai's "combat-peer=6" (this document's own §"Problem/evidence" estimate, inherited from the
   playbook's cost table) re-derives to **1** genuine still-app-DEFER edge (`do_dismount`).
2. `scale_guardian`'s helper cluster is **6**, not the 4 this document states.
3. `scale_guardian` has a **second** external caller (`objsave.cpp:775`) this document's source-level
   pass missed.
4. olog_hai's `_waiting_list` reference (4 `WAIT_STATE_FULL` sites) is absent from this document's
   residual list entirely — a positive finding, not a blocker: the combat-pilot wave's storage-move
   already resolved it for free.
5. mystic's 5 `spell_pa.cpp` combat-peer edges — this document's own text (the "Risks" section)
   flagged them as unenumerated and the wave's primary STOP-risk; Task 0 supplies the missing names
   and confirms all 5 RELOCATE-CLEAN.

None of these overturns changed a Task-skeleton decision — every adjudication default in this
document's own "Adjudication defaults" section was otherwise confirmed exactly as written (`do_move`,
`add_follower`, `get_guardian_type`'s L3 destination) by direct `nm`/body-read evidence, not merely
re-asserted.

**T2's ASan lesson (a reviewer catch, not a self-caught finding).** `trio-task-2-report.md` skipped
the `macos-arm64-asan` gate for its two new `move`-discriminator tests, justifying the skip by
claiming "this class of change — additive tests in an already-covered file, no new test *file* — has
not historically triggered a standalone ASan gate in this wave's own prior tasks' reports." This was
factually false: `trio-task-1-report.md` line 241 ran ASan for the **identical** class of change (two
new `dismount` tests appended to the same pre-existing `combat_hooks_tests.cpp`, also no new file) —
T1 is literally "this wave's prior task's report," and it did the opposite of what T2's report
claimed. The Task 2 reviewer caught the contradiction, independently ran `macos-arm64-asan` (clean
build, 1398/1398, zero ASan diagnostics — no actual defect existed), and closed the finding in-review
rather than accepting the report's stated rationale at face value. **Lesson: "additive tests, no new
file" is not itself a valid ASan-skip justification** — this wave's own Task 1 precedent already
required the gate for that exact shape of change; a report's stated rationale for skipping a
required-by-precedent gate needs checking against the actual prior-task reports, not just cited from
memory.

**First-standalone-promotion data point, and the one-directional-vs-cycle distinction.** Every prior
`rots_combat` growth was either SEED-CLEAN (combat-seed wave, zero relocation) or a mutually-dependent
pair forced into one commit (`clerics`+`fight`, a true cycle). olog_hai and mystic share zero symbols
in either direction — the playbook's first true standalone promotions. `profs`'s dependency on mystic
(`scale_guardian`) is real but one-directional (mystic never calls back into profs) — a category the
"intra-subset rule" hadn't previously had a data point for: a one-directional gate only forces the
callee (mystic) to promote no later than the caller (profs), never the reverse, and does not by itself
force joint membership the way a true cycle does. This wave chose one commit for all three TUs for
simplicity (documented explicitly in the Task 4 commit body, per this plan's own instruction), not
because `CombatLayerAcyclicity` required it. Full writeup, including the `half_chop` unbundling
finding and the `saves_*` five-pack's "L2-lateral floor" constraint:
`docs/superpowers/combat-migration-playbook.md`'s "The combat-trio wave" section.
