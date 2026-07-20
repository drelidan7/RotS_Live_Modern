# Combat-Pilot Wave (clerics + fight join rots_combat) — Design

**Date:** 2026-07-20 · **Branch:** `arch/combat-pilot`, off master @79fcb28 (post blocker-buster
merge) · **Predecessor censuses:** `.superpowers/sdd/combat-census.md` (the 4-clean / 11-DEFER
verdict) and `.superpowers/sdd/blocker-census.md` (sections A–F; Scope 1 shipped as the
blocker-buster wave). Owner-sequenced in the progress ledger as the "clerics/fight pilot
migration on the four proven seams."

## Problem / evidence

The blocker-buster wave shipped all four combat-growth enablers **consumer-free**: the
`output_seam` +7 forwarders, the 25-cell `combat_hooks.h` boot-registered ACMD dispatch table,
the poison-notification hook (`entity_hooks.h`), and the 12-function visibility family in
`rots_combat`'s `visibility.cpp`. No DEFER TU has yet crossed the seams, so the per-TU migration
recipe (body membership + up-call conversion + census re-verification) is designed but unproven.

The two pilot TUs, per the pre-blocker-buster combat-census rows re-read against what the BB wave
resolved (all counts to be re-verified by this wave's fresh census, Task 0):

- **`clerics.cpp`** — the cleanest DEFER TU. Resolved by BB: `get_char_room_vis` (now
  `visibility.cpp`, in-lib), `break_spell`/`abort_delay`/`complete_delay` (now `output_seam`
  forwarders — same-symbol, so **no call-site edits needed**). Residual (~5):
  `set_mental_delay` (utility.cpp, app), `special()` (interpre.cpp — NOT one of the 25 dispatch
  cells; different signature than ACMD), `do_flee` (cell exists; call site must convert to
  `issue_command`), db_boot scratch globals (`_buf`, `_waiting_list`), and big_brother's
  `is_target_valid`.
- **`fight.cpp`** — the melee hub, fattest edge set. Resolved by BB: `extract_obj` (now L2
  `object_utils.cpp`), `CAN_SEE`/`get_real_OB`/`get_real_parry` (now `visibility.cpp`),
  `abort_delay`/`break_spell`/`complete_delay`/`send_to_room_except_two` (forwarders, no edits).
  Residual (~14): everything clerics has, plus handler.cpp wrapper calls (`extract_char`,
  `unequip_char` ×2, `stop_follower`, `stop_riding`), `remove_character_from_group`,
  `do_flee` ×6 + `do_stand` conversions, `_fight_messages` storage (defined db_boot.cpp:108),
  `add_exploit_record` (db_boot), big_brother ×2 (`on_character_attacked_player` class), and
  three app-other edges: `Crash_crashsave` (objsave runtime half), `call_trigger` (script.cpp),
  `pkill_create` (pkill.cpp runtime half). fight.cpp is also where the BB poison-hook
  implementation body already lives — after migration the registration becomes intra-lib.

**Why these two, in this order:** the blocker-census (§F Scope 2) named `clerics` the cleanest
pilot and `fight` the heaviest; the owner sequenced both as one wave. Clerics lands first and
writes the migration playbook; fight follows in the same wave applying it, adding its riders.
One battery/review/PR cycle instead of two.

## Decision (owner-approved)

**Scope: both TUs, clerics first, one wave.** Plus two owner-selected verification layers for
the up-call conversions (the wave's only semantic edits — body moves stay byte-verbatim):

1. **Dispatch-equivalence discriminator tests** — per converted call-site class, proving the
   registered cell reaches the same body the direct call did (the BB Task 2 discriminator
   pattern). Catches wrong-cell / unregistered-cell errors precisely.
2. **Full combat smoke harness** — a scripted live fight session with capture/verify transcript
   goldens. Owner rationale: dual-purpose infrastructure — it brackets this wave's migrations
   AND becomes the behavioral baseline for the future int→double combat-math conversion
   (see the double-precision deferral), so building it now has long-term value. The two layers
   catch disjoint error classes; both are wanted.

### Task skeleton

- **T0 — Fresh census + adjudications.** `nm -u` re-census of clerics.cpp/fight.cpp residual
  edges at 79fcb28 (the counts above derive from two stale censuses and are hypotheses, not
  facts). T0 also adjudicates the design-sensitive dispositions this spec deliberately leaves
  open (see "Adjudication defaults" below): `special()`, `set_mental_delay`,
  `remove_character_from_group`, the app-other seam mechanism, and the handler-wrapper
  disposition for fight's call sites.
- **T1 — Combat smoke harness, BEFORE any migration** (characterization-first; BB Task 3
  precedent). Deliverables:
  - `ROTS_RNG_SEED` environment override at the two comm.cpp `rots_rng::seed(time(0))` sites
    (comm.cpp:487/563), via one shared helper (`seed_from_environment_or_time()`), so a seeded
    boot yields the standard-specified, cross-platform-identical mt19937 stream.
  - `tools/combat_smoke.py` modeled on `tools/account_smoke.py` (expect-style, marker-paced —
    never sleep-paced): boot a seeded server on a scratch port with scratch lib data, create two
    test characters, run a scripted fight to the death, capture the transcript.
  - `scripts/combat-golden.sh` with `capture`/`verify` modes mirroring `boot-golden.sh`.
    Baseline captured at pre-migration HEAD and committed.
  - **Feasibility gate** (WS Task 5b precedent): if pulse-timing nondeterminism makes raw
    transcript comparison flaky, fall back to a normalized comparison (extract the combat
    message sequence — hits/misses/damage/death lines — and compare that), documenting the
    normalization honestly. If even normalized comparison proves unstable, the harness lands as
    capture-only tooling with the gap documented and the wave's verification bar rests on layer
    1 + goldens; that outcome must be named in the task report, not silently accepted.
- **T2 — Shared riders.** db_boot scratch retirement for the two TUs' `_buf`/`_buf2`/
  `_waiting_list` uses (world-seed buffer-retirement pattern, byte-preserving output);
  big_brother hooks — `is_target_valid` plus fight's two `on_character_*` calls — extending
  `entity_hooks.h`'s `set_attacked_player_hook` precedent (registration in `run_the_game()`
  pre-`boot_db` + gtest parity, tripwire defaults per the established taxonomy: value-returning
  hooks get a safe-default-plus-log only if a safe default exists, else abort); plus whatever
  T0 adjudicated for `special`/`set_mental_delay`.
- **T3 — Clerics migration.** `clerics.cpp` → `ROTS_COMBAT_SOURCES` (all four build systems);
  `do_flee` call-site conversion to `rots::combat::issue_command`; dispatch-equivalence tests;
  `CombatLayerAcyclicity` green; smoke verify against the T1 baseline. NOTE: `do_mental` is
  ACMD **in clerics.cpp** — interpre.cpp's registrar keeps registering it; the pointer simply
  lands in the library after migration (legal downward registration, no change needed). This
  task drafts the migration playbook.
- **T4 — Fight riders.** `_fight_messages[MAX_MESSAGES]` storage-move db_boot.cpp → fight.cpp
  (db_boot's boot-time loader keeps writing it — app writing lib-owned storage is downward);
  `add_exploit_record` via `persist_hooks.h`'s existing `exploit_capture_fn`; the three
  app-other seams per T0's adjudication; the handler-wrapper disposition per T0 (default
  hypothesis: fight's call sites convert to the L2 primitives — `detach_equipment`,
  `detach_char_from_room` — plus in-lib poison logic, since fight owns `damage()`/`raw_kill()`;
  `stop_follower`/`stop_riding`/`extract_char`/`remove_character_from_group` need T0 census
  before choosing relocate-vs-hook).
- **T5 — Fight migration.** Membership; `do_flee` ×6 + `do_stand` conversions; discriminators;
  poison-hook registration verified intra-lib with **no double-fire** (the BB Task 3
  characterization pair must still pass unchanged); smoke verify; goldens
  (`CharacterizationCombatTest.*`) unchanged.
- **T6 — Migration playbook.** `docs/superpowers/combat-migration-playbook.md`: the per-TU
  recipe (census → riders → membership → conversions → verification) with per-TU cost markers
  for the remaining DEFER TUs (mobact/spec_pro/ranger/olog_hai/mystic/mage/limits/spell_pa/
  spec_ass + profs), fed by what the two pilots actually cost.
- **T7 — Docs as-built.** BUILD.md `rots_combat` section, AGENTS.md totals/description updates, parent spec
  §10 additive as-built note; coverage-gap riders if T0 surfaces untested live code (standing
  wave rule).
- **T8 — Finalization.** i386 battery (`scripts/i386-battery.sh`), whole-branch Fable review,
  fix wave if any, push + PR + CI + merge under the owner-pre-authorized protocol carried
  forward from the combat-seed and blocker-buster waves.

### Adjudication defaults (T0 confirms or overturns with evidence)

- **`special()`** — interpre.cpp's spec-proc dispatcher, non-ACMD signature: its own registered
  hook (single fn-ptr, output_seam-style cell in `combat_hooks.h` or a sibling), NOT a 26th
  ACMD cell.
- **`set_mental_delay`** — utility.cpp: relocate (likely to rots_combat, since mental delay is
  combat semantics; entity if census shows entity-pure callers below L3).
- **App-other seams** (`Crash_crashsave`/`call_trigger`/`pkill_create`) — per the established
  taxonomy: comm.cpp-owned symbols go through `output_seam`; **cross-owner** edges warrant the
  `*_hooks.h` family. These are three distinct app owners → registered hooks (one small
  `combat_hooks.h` extension or per-owner additions), tripwire defaults per the
  void-vs-pointer taxonomy.
- **Handler wrappers** — see T4 default hypothesis above.

## Verification

- Byte-verbatim mandate for every body move (python byte-edits where CRLF requires; formatter
  hook countermeasures per the established method); reviewers re-diff moved spans.
- Dispatch-equivalence discriminator per converted call-site class (layer 1).
- Combat smoke capture/verify bracketing each migration (layer 2, feasibility-gated as above).
- Standard gates per task: macOS arm64 + rots64 builds, full ctest (1365 baseline + this wave's
  additions), all 7 linkchecks, both boot goldens, `ConvertEquivalence` 17/17, census
  `--check` 0. New/rewritten test files get the macOS ASan preset. i386 battery at
  finalization only.
- Characterization goldens (`CharacterizationCombatTest.*`, `PoisonRemovalScriptTest.*`) must
  pass **unchanged** — any drift is a bug in the change, per repo policy.

## Risks

- **fight.cpp blast radius** — the live melee path; mitigated by byte-verbatim moves, both
  verification layers, goldens, and clerics going first to shake out the recipe.
- **Smoke determinism** — pulse-timing vs RNG-stream coupling (zone resets and mob AI consume
  RNG per tick, so transcript stability depends on marker-paced command timing); explicitly
  feasibility-gated with a documented fallback ladder.
- **Census staleness** — all residual-edge counts in this spec are hypotheses from
  pre-blocker-buster documents; T0 re-derives them from `nm` ground truth before any code
  moves. STOP-and-adjudicate contract unchanged for anything T0 or a linkcheck surfaces.
- **Seed override touches production boot code** — smallest possible surface (env read + one
  helper); default behavior (no env var set) byte-identical to today's `time(0)` seeding.

## Out of scope

- No other DEFER TU call sites convert (they stay app-compiled; same-tier calls remain legal).
- No `PERS`/color seam (output-adjacent, separate concern).
- No wholesale handler.cpp/utility.cpp extraction (§7 Stage 2 territory).
- No `spell_pa`/`spec_ass` (registrar-hub TUs land last, after the row fills in).
- No int→double math changes — this wave only builds the smoke baseline that future work needs.

## As-built (T6/T7 docs-finalization pass)

The wave landed as designed in outline (both TUs, one wave, both verification layers built) but
diverged from the task skeleton above in every place real evidence disagreed with the spec's
hypotheses. Recorded per deviation, evidence-first, per the "STOP-and-adjudicate, don't paper
over" contract this spec itself set.

### T0: five adjudication defaults OVERTURNED by evidence

T0's own brief carried default hypotheses (this document's "Adjudication defaults" section plus
the plan's per-item defaults); an independent reviewer rebuilt the full 81.8k-symbol→object map
and re-resolved both TUs' complete undefined-symbol lists (not a sample) and overturned five of
them, all reviewer-confirmed against `nm` evidence:

1. **`stop_follower` is NOT L2-clean** (its `FOLLOW_MOVE` branch calls `forget(ch, ch->master)`,
   defined only in the still-app `mobact.cpp`) — needed the "else hooks" branch, not the plain L2
   move this spec's sibling `stop_riding` got. The pair diverges: `stop_riding` IS census-clean
   and relocated to L2 directly.
2. **`_waiting_list` is LIVE in `clerics.cpp`**, not textually dead as the plan's Task 2 Step 1
   hypothesized — `WAIT_STATE`/`WAIT_STATE_BRIEF` macro expansion reads/writes it invisibly to a
   plain-text grep. Confirmed genuinely dead in `fight.cpp` only.
3. **`set_mental_delay` relocates to `rots_combat` (fight.cpp), not `char_utils.cpp`** — its
   `set_fighting()` call is a `fight.cpp` symbol; landing at L2 would have created an
   `EntityLayerAcyclicity` violation the moment `fight.cpp` itself promoted.
4. **`special()`'s real signature has a named 6th parameter** `int in_room = NOWHERE` — not the
   variadic tail this spec's own prose shorthand implied. The `special_fn` type spells out all
   six parameters explicitly (a function-pointer type cannot itself carry a default); the dispatch
   wrapper (`call_special`) carries the default instead.
5. **`fight.cpp`'s `buf`/`buf2` scratch inventory was 29 genuine globals + 12 local shadows**, not
   41 uses needing retirement as the plan's Task 4 (later 4a) Step 2 estimated — 12 of the 41 are
   unrelated local `char* buf` declarations that merely shadow the global name.

Confirmed without change: `special()`→hook, `remove_character_from_group`→L2,
`extract_char`→hook (sentinel-mapped), the app-other trio→hooks (plus a second `call_trigger`
call site the brief had missed), the handler-wrapper verbatim-move hypothesis, `add_exploit_record`
→`persist_hooks.h` (4 sites, exact signature match), `fight_messages` (zero non-fight readers).

### CONTROLLER STOP: closure, not the task skeleton's T3/T5 split, governs membership timing

This is the wave's most consequential deviation from the task skeleton above. T3 ("Clerics
migration," including a membership move) was **planned** to promote `clerics.cpp` alone, write the
playbook, and let T5 repeat the pattern for `fight.cpp`. A post-T1 re-read of the census surfaced
that `clerics.cpp` and `fight.cpp` call each other directly
(`set_fighting`/`stop_fighting`/`check_sanctuary`/`check_hallucinate`/`die`/`appear` one way,
`weapon_willpower_damage`/`do_mental` the other) — edges the census's own legend correctly
classified `combat-peer (still-app)` = architecturally sanctioned, but which a **standalone**
`clerics.cpp` promotion cannot actually link, since `fight.cpp` would still be
`ROTS_SERVER_SOURCES` at that point. **Ruling:** T3 becomes conversions-only (clerics.cpp's own
up-call conversions land and gate green while the file stays app-compiled — a legal downward
app→app call at that point); the membership move itself moves to T5, where both TUs promote in
one joint commit. This restructure — and the general rule it establishes (co-migrating TUs keep
direct calls; standalone membership requires closure over every combat-peer edge, not merely a
census's "sanctioned" label) — is documented in full in
`docs/superpowers/combat-migration-playbook.md`'s "The intra-subset rule" and
"Census-methodology correction" sections; this spec's own T3/T5 task-skeleton split above should
be read as superseded by that restructure, not as what actually happened.

The same STOP also surfaced ~9 out-of-wave peer symbols the pilots reference in TUs that stay
app-compiled after this wave (`spell_pa`/`limits`/`mobact`/`ranger`). A census follow-up
(`.superpowers/sdd/pilot-census.md` §7) dispositioned all nine: RELOCATE (`saves_power`,
`record_spell_damage`+storage, the `forget`/`remember`+memory-pool package, `stop_hiding`) or HOOK
(`check_break_prep` via the existing `trap` cell, `gain_exp`+`gain_exp_regardless`,
`remove_fame_war_bonuses`) — all folded into Task 4's scope (see the 4a/4b split below).

### T4 → 4a/4b split (BB-wave precedent repeated)

The task skeleton's single "T4 — Fight riders" step grew too large for one gated unit once the
census follow-up's nine extra symbols joined its scope. The controller split it, mirroring the
blocker-buster wave's own Task 4/4b precedent: **4a** (byte-verbatim moves/storage-moves only —
`fight_messages`, the 29-use `buf`/`buf2` retirement, `equip_char`/`unequip_char`,
`set_mental_delay`, `record_spell_damage`+`spllog_*`, `check_break_prep`, `saves_power`, the
`forget`/`remember` package, `stop_hiding`, plus a **conditional**: re-check `stop_follower` once
`forget` lands at L2) and **4b** (hooks: `gain_exp`/`gain_exp_regardless`/
`remove_fame_war_bonuses`, `extract_char`, the app-other trio, plus a `stop_follower` hook —
**conditional on 4a's re-check firing HOOK, not RELOCATE**).

**The conditional fired RELOCATE, not HOOK — 4b's planned `stop_follower` hook became MOOT.** 4a's
own `nm` proof (`entity_lifecycle.cpp.o` carries zero undefined references to `forget`/anything
mobact-shaped after the memory-pool package landed there) showed `stop_follower`'s one blocker had
resolved to an intra-file call, not merely an intra-library one — a plain L2 relocation, dropped
cleanly from 4b's scope before that task started rather than built and then discovered unused.

### T5: the two census misses caught only at the linkcheck gate

The joint membership move's first build attempt failed `CombatLayerAcyclicity` with two undefined
symbols inside `clerics.cpp.o` — outside T5(a)'s own `fight.cpp`-scoped conversion work, surfaced
only because the joint commit pulled `clerics.cpp` along:

1. **`gain_exp`** (`clerics.cpp:234`, inside `do_mental`'s Will-contest damage path) — the census
   had correctly traced this to `limits.cpp` but classified it `combat-peer (still-app)` =
   non-blocking, under the inherited legend. `limits.cpp` never promoted this wave, so the label
   held only provisionally. **Fix:** converted this one site to `rots::combat::gain_exp()` — the
   identical 4b hook `fight.cpp` already used. Zero new infrastructure.
2. **`_waiting_list`** — flagged LIVE for clerics back at T0 (see overturn #2 above), but no task
   between T0 and T5 had actually built a seam for it, since a raw lvalue-mutated global (read AND
   written by the `WAIT_STATE`/`WAIT_STATE_BRIEF` macros) doesn't fit a registered-fn-ptr hook.
   **Fix:** a storage-move (the same technique `fight_messages`/`spllog_*` already used twice) —
   relocated the definition from `db_boot.cpp` (which never read or wrote it, only defined it)
   into `clerics.cpp`, the promoted pair's one actual user. All 16 other files' own `extern`
   declarations needed zero changes.

Neither was stubbed or reverted; both were resolved with existing techniques/seams per the STOP
contract. This is the concrete evidence behind the playbook's "a census's non-blocking
classification is a strong prior, not a build-wiring guarantee" lesson.

### T3's discriminator-audit outcome: zero new tests, a real "zero," not a skipped step

T3's brief called for a discriminator audit (does a registered/unregistered test pair already
prove each converted call-site class reaches the real body with args intact?). The audit found
Task 2 had already landed coverage for all three shapes clerics.cpp's conversions exercised
(`flee`, `special`, the 2-arg `is_target_valid`) — **no new test was added**, and this was recorded
as a verified "zero," not an assumed one (the file was read in full, not merely grepped for test
names). Task 5's own later audit found the identical pattern at larger scale: 9 of 10 hooks
`fight.cpp`'s conversions exercised already had coverage from Tasks 2/4a/4b; the one real gap
(`dispatch_exploit_capture`, consumer-free until T5 gave it external linkage) was closed with 2
new tests (`persist_hooks_tests.cpp`).

### Determinism: landed on the fallback ladder's rung (b), capture-only — named, not hidden

T1's own feasibility gate anticipated this outcome as a documented possibility; it is what
actually happened, not a worst-case avoided. Repeated same-seed trials (including a same-host,
immediate re-run with zero code changes between captures) showed the shared global `rots_rng`
engine's draw sequence shifts under real-time pulse-loop interleaving from other periodic RNG
consumers (door auto-close mechanics confirmed as one source; weather/regen-tick consumption not
ruled in or out). Neither a raw transcript compare nor the normalized (combat-lines-only) fallback
proved reliable — even the normalized comparison drifted on a same-seed immediate re-run. Rung (b)
landed: `scripts/combat-golden.sh verify` is **informational only**, never a merge gate; every
later migration task (T3, T5) ran it and recorded drift consistent with RNG-consumption-order
shifts from the new hook indirection, confirmed non-regression each time by the unchanged
gtest-level characterization goldens (`CharacterizationCombatTest.*`, `PoisonRemovalScriptTest.*`),
which run outside real time and don't share this problem. This spec's own "Verification" section
above should be read with that caveat: layer 2 (the smoke harness) shipped, but as a
gross-shape/informational check, not the byte-comparison gate its original framing implied.

### T6/T7: merged into one docs dispatch

The task skeleton above lists T6 (playbook) and T7 (docs as-built) as separate tasks. The
controller merged them into a single combined docs-finalization pass after T5 landed the wave's
central deliverable — no code changes remained that would make splitting the two documentation
tasks across separate gated commits worthwhile; both are covered by this as-built section plus
`docs/superpowers/combat-migration-playbook.md`'s finalized recipe/cost-table sections and the
`docs/BUILD.md`/`AGENTS.md` updates cited throughout.
