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
