# Combat-Math Double Interiors Wave Implementation Plan (`fp-interiors`)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convert the interiors of the four core-combat formula families from per-step integer
truncation to full-precision `double` math, landing each result back in its unchanged `int`
destination through exactly ONE `rots::fp::to_game_int` (`std::lround`) boundary call — a
deliberate, owner-approved balance change delivered without any storage/schema change.

**Architecture:** Census first (T0, Opus, read-only: the exact boundary-site map, per-formula
delta bounds, the no-transcendentals proof, the tie-asymmetry flags, and the definitive T2 split);
then the `to_game_int` helper + byte-verbatim TEST-ONLY integer references with paired bound tests
and exact-value tests (T1, consumer-free — live code untouched); then per-family in-place
conversions (T2a–T2d, split per census, each family's paired tests go green in-task); then the
ONE intentional golden regen + same-seed transcript diff (T3); then docs (T4); then finalization —
i386 battery, Fable whole-branch review of code AND the transcript diff, CI-green PR (T5).
**No merge authority: the wave ends at a CI-green PR presenting the transcript diff for the
owner's own review and merge.** Spec: `docs/superpowers/specs/2026-07-22-fp-interiors-design.md`.
Predecessor: the Phase 1 FP unification (`docs/superpowers/specs/2026-07-15-phase1-fp-unification-design.md`).
Branch: `feat/fp-interiors` off master, HEAD `c9d38f9`.

**Tech Stack:** C++20 (`std::lround`, `std::format`), CMake presets + flat Makefiles, GoogleTest,
`fp_policy.h` boundary helper, `rots_rng` (mt19937), Python 3 byte-edits, `nm` for census. No new
library, no new seam header, no new linkcheck expected.

## Global Constraints

- **FP-determinism policy (hard, from Phase 1 / `docs/BUILD.md` "FP determinism"):** the combat
  path is SSE-only — NO x87, NO `long double`, NO fast-math, NO transcendentals; `-ffp-contract=off`
  is in force. The deterministic subset is `+ - * /` **and `sqrt`** and comparisons, `double`-only
  (`fp_policy.h`'s own comment: "The deterministic subset is + - * / sqrt, double-only" — `sqrt` via
  `do_squareroot` is IN-POLICY, not a banned transcendental). A converted chain that genuinely needs
  any other transcendental (`pow`/`exp`/`log`/trig) is an **auto-STOP to the owner**, never worked
  around.
- **ONE boundary helper, no bare rounding:** every boundary site calls `rots::fp::to_game_int(double)`
  (T1). No bare `std::lround`, `(int)`, `int(...)`, `static_cast<int>` cast, or truncation at any
  converted call site — the policy must grep cleanly (`grep -rn 'to_game_int' src/` finds every
  boundary; `grep -rn 'lround' src/` finds only the helper definition).
- **RNG stream unchanged:** all randomness stays `rots_rng` (`number(...)`); an integer draw is taken
  first, THEN converted to `double` — same seed → same draw sequence, byte-identical. Never move a
  `number()` call, never change its arguments, never draw in `double`.
- **Storage/struct/persistence types untouched:** every `char_ability_data`/`char_point_data`/
  `char_data`/`char_file_u` field, every struct member, every save/load path stays `int`. Only
  formula-*interior* locals become `double`. Death stays `<= 0` on int HP, unchanged. Display
  formatting unchanged.
- **Layer discipline unchanged:** the four families already live in `rots_entity`
  (L2: `recalc_abilities`, `get_real_dodge`), `rots_combat` (L3: `get_real_OB`/`get_real_parry`,
  `fight.cpp::hit()`, `get_weapon_damage`), and app files (`get_energy_regen` wrapper) already
  converted by prior waves. NO membership, seam, or linkcheck change is expected. Any surfaced
  coupling follows `docs/superpowers/combat-migration-playbook.md`; an UNEXPECTED coupling (a new
  cross-tier edge, a forced new library) is an **auto-STOP**.
- **Golden-drift rule (this wave's defining discipline — read carefully):** the converted formulas
  intentionally change combat outcomes, so the combat-characterization goldens WILL drift, but the
  spec mandates **exactly ONE golden-regen commit, late, at T3**. Therefore:
  - T0 enumerates the **exact set of existing tests** whose assertions depend on a converted formula
    (the combat golden reader `CharacterizationCombatTest.DamageTranscriptSeed42` plus any
    hard-coded exact-value assertions in `damage_tests.cpp`/`fight_proc_tests.cpp`/`visibility_tests.cpp`/
    `mage_tests.cpp`/`olog_hai_tests.cpp`/etc.). Call this the **EXPECTED-DRIFT SET**.
  - Every T2 task's gate runs the full ctest suite; the **only** permitted failures are members of
    the EXPECTED-DRIFT SET, and each T2 task report lists by exact test name which of them failed.
    **Any failure outside the EXPECTED-DRIFT SET = a real bug → STOP.** All other tests pass 100%.
  - The **boot goldens must stay byte-identical** at every task (no combat at boot):
    `docs/superpowers/goldens/boot-log.golden`. Boot-golden drift = a bug, never expected.
  - The **combat-smoke harness** (`scripts/combat-golden.sh`, `docs/superpowers/goldens/combat-smoke/`)
    is capture-only/informational (verify never gates) — same-seed transcript drift is real under the
    shared RNG pulse loop, so it is never a pass/fail gate.
  - The paired-reference tests and exact-value tests added in T1/T2 are NOT goldens; they must pass
    100% at every gate from the task that introduces them onward.
  - T3 is the single `UPDATE_GOLDENS=1` regen commit (declared in the commit message per the standing
    rule). After T3, the EXPECTED-DRIFT SET must pass again; any later drift = accidental second
    regression → STOP.
- **32-bit fixture guard:** `src/tests/goldens/legacy_*_fixture.bin` are 32-bit-only and are NOT
  touched by this wave (no persistence change). NEVER run `UPDATE_GOLDENS=1` against them, and NEVER
  regenerate them on a 64-bit host. The T3 regen touches ONLY the host-regenerable combat
  characterization golden(s) T0 enumerates (expected: `src/tests/goldens/combat_transcript_seed42.txt`),
  on the host presets.
- **Python byte-edits for every existing `.cpp`/`.h`** (the formatter-hook conflict — see MEMORY):
  measure each target file's CRLF profile BEFORE editing and preserve it; no line-ending flips. New
  test files are the only free-form Writes.
- **Flat-Makefile parity is BINDING and same-commit:** any new test file lands in BOTH
  `src/tests/Makefile` `SRCS` AND `src/CMakeLists.txt`'s `tests/*.cpp` list (~line 1253 ff) in the
  SAME commit. A CMake-only test add is an Important review finding. No new production TU or
  linkcheck is expected; if one is forced, that is an auto-STOP.
- **Per-task dual-host gates (both hosts, every task):** macOS arm64 (`cmake --preset macos-arm64` +
  build + `ctest --preset macos-arm64`) AND `rots64` (`docker compose run --rm --pull never rots64 …
  cmake --preset linux-x64 && build && ctest`); both boot goldens byte-identical
  (`scripts/boot-golden.sh --native build/macos-arm64/ageland verify` and
  `scripts/boot-golden.sh --service rots64 verify`); all NINE `*LayerAcyclicity` linkchecks green;
  `ConvertEquivalence` 17/17; `python3 tools/string_view_census.py --check` exit 0. **During T2 the
  EXPECTED-DRIFT SET failures are permitted per the golden-drift rule above.** Docker gates run
  SYNCHRONOUSLY in subagents.
- **ASan on any new/changed test file:** `cmake --preset macos-arm64-asan` + build + ctest, before
  finalization, for the task that touched it (T1 and each T2 that adds tests).
- **i386 battery finalization-only:** `scripts/i386-battery.sh` (sequential, fresh-branch full run) at
  T5; per MEMORY it takes 60–90+ min on this Apple Silicon host and can hang — never per-task, never
  concurrent. A monolithic-runner SIGSEGV is never tolerated (clean rebuild first).
- **`-Wall -Wextra -Werror` clean** (`/W4 /WX` on MSVC); `-funsigned-char`; `char[N]` members decay
  via `static_cast<const char*>` before `std::format`. No death tests. No direct `rand()`/`random()`.
- **STOP-and-adjudicate:** any boundary site, downstream tie-asymmetry, or drift a brief did not
  disposition → STOP with evidence. A genuine transcendental, an unexpected cross-tier coupling, a
  forced new library, or drift outside the EXPECTED-DRIFT SET all go to the owner.
- **No merge authority (owner-set, this wave):** the wave ends at a CI-green PR + the transcript diff
  attached for the owner's own review and merge. There is NO merge step and NO merge-when-green grant.
- Baseline tests **1510** both hosts (spec-pair wave final: macOS 75 skips / `rots64` 77 skips / i386
  6 skips via ctest). Deltas tracked per task, reconciled in T4. Implementers **Sonnet**; T0 census +
  heavy reviews **Opus**; whole-branch review **Fable** (reviews the TRANSCRIPT DIFF, not just code).
  Briefs/reports/census in `.superpowers/sdd/` with the **`fpi-`** prefix, gitignored, never committed.

---

### Task 0: Formula census + boundary inventory + no-transcendentals proof (read-only, Opus)

**Files:**
- Create: `.superpowers/sdd/fpi-census.md` (gitignored scratch).

**Interfaces:**
- Consumes: the four family locations verified below; the spec's four-family scope.
- Produces: the definitive per-boundary-site map, per-formula delta bounds, the EXPECTED-DRIFT SET,
  the no-transcendentals proof, the tie-asymmetry flag list, the per-function int-vs-double-return
  ruling, and the T2 split — every later task cites this file.

**Verified starting locations (confirmed against code at HEAD `c9d38f9`; the spec's own pointers are
imprecise — record the reconciliation in the census):**
- **Family 1 — max-HP/vitals recalc:** `recalc_abilities(char_data*)` is in
  **`src/entity_lifecycle.cpp:1964-2053`** (`rots_entity`, L2), NOT `profs.cpp` — `profs.cpp:735`
  only *calls* it. HP formula `entity_lifecycle.cpp:1977` (+ Defender bonus :1981, stealth malus
  :1985); mana `:1989`; move `:1993-2001`. The file-local `do_squareroot(int, char_data*)` helper is
  `entity_lifecycle.cpp:1957-1960` (anon namespace, `std::sqrt` — in-policy).
- **Family 2 — OB/PB/DB trio:** `get_real_OB(char_data*)` `src/visibility.cpp:151-263`;
  `get_real_parry(char_data*)` `src/visibility.cpp:265-…` (both `rots_combat`, L3);
  `get_real_dodge(char_data*)` `src/char_utils_combat.cpp:384-435` (`rots_entity`, L2). Contributing
  sub-chains to map: `weapon_master_handler::get_bonus_OB/get_bonus_PB`, `get_power_of_arda`
  (`char_utils_combat.cpp:453`), `get_confuse_modifier`, `utils::get_skill_penalty`,
  `utils::get_dodge_penalty`.
- **Family 3 — `hit()` damage:** the LIVE core formula is **`src/fight.cpp:2695`**
  (`dam = (dam * (OB + 100) * (10000 + (damage_roll * damage_roll) + (IS_TWOHANDED(ch) ? 2 : 1) *
  133 * GET_BAL_STR(ch))) / 13300000;`) — strength folded INSIDE the random factor; `damage_roll =
  number(0, 100)` at `:2689` (RNG draw — must stay an int draw, converted AFTER). `natural_attack_dam`
  `fight.cpp:2519-2539` (already partially double: `(int)((double)dam * 0.75/0.50)`). The deleted
  `combat_manager` variant is explicitly NOT a reference (repo dead-code warning).
- **Family 4 — speed/energy-regen:** the spec cites `utils::get_energy_regen(char_utils_combat.cpp:165)`
  but that is WRONG: `utils::get_energy_regen(const char_data&)` is **`src/char_utils.cpp:1350-1360`**
  (a thin wrapper: reads `points.ENE_regen`, multiplies by the wild-fighting attack-speed hook), and
  `char_utils_combat.cpp:162-262` is `get_weapon_damage`. The `str_speed`/`null_speed` harmonic-mean
  the spec quotes (`2*20*2500000/(weight*(bulk+3))`, then `1000000/(1000000/str_speed +
  1000000/null_speed²)`) appears in TWO places: inside **`recalc_abilities` (entity_lifecycle.cpp:2014-2048,
  which SETS `points.ENE_regen`)** and inside **`get_weapon_damage` (char_utils_combat.cpp:242-253)**.
  The pulse consumer is `fight.cpp:2938` (`fighter->specials.ENERGY += utils::get_energy_regen(*fighter)`).

- [ ] **Step 1: Build the object base.** `cd src && cmake --preset macos-arm64 && cmake --build
  --preset macos-arm64 -j4`. Confirm the base: nine libraries, `combat_command` 29 cells, 1510 tests,
  HEAD `c9d38f9` + this plan commit.
- [ ] **Step 2: Reconcile the four families against code.** Record the corrected locations above in
  `fpi-census.md` with a "spec-said → code-is" table (the profs.cpp→entity_lifecycle.cpp and
  char_utils_combat→char_utils moves). Body-read each family end-to-end.
- [ ] **Step 3: Boundary-site inventory.** For EACH family, enumerate every point where a
  `double`-computed value must land back in an `int` destination (a struct field write, a `return`,
  or an intermediate consumed by an int-only API). This is the exact list of `to_game_int` call sites
  T2 will add. Note the family-1/family-4 overlap explicitly: `recalc_abilities` holds BOTH the HP/mana/
  move writes AND the `points.ENE_regen` write — rule whether it converts as ONE task or splits by
  formula-block (see Step 8).
- [ ] **Step 4: Per-formula delta bounds.** For each converted formula, count the integer-truncation
  points in the OLD implementation (each `/` on ints, each intermediate int assignment that drops a
  fraction). Derive a defensible upper bound on |double_result − int_reference| from that count (each
  truncation loses <1 per step; the harmonic-mean/nested-division chains lose more — derive per chain).
  These bounds are what T1's paired tests assert; a transcription error blows the bound. Record the
  bound and its derivation per formula.
- [ ] **Step 5: No-transcendentals proof.** Prove each converted chain uses ONLY `+ - * /`, `sqrt`
  (via `do_squareroot`), and comparisons — no `pow`/`exp`/`log`/trig. `do_squareroot`'s `std::sqrt`
  is in-policy. A genuine transcendental anywhere in scope = **STOP to the owner** (record it, do not
  design around it).
- [ ] **Step 6: Tie-asymmetry flags.** Every in-scope chain feeds downstream int comparisons (a rating
  vs a roll, HP vs a threshold, `ENERGY <= ENE_TO_HIT`). List every downstream comparison whose
  semantics are asymmetric at an exact tie (`>` vs `>=`, `<` vs `<=`) where the single-rounding change
  could flip a marginal outcome. These are FLAGGED FOR EXPLICIT REVIEW, not silently inherited — the
  intended balance change is fine, but each flip site is named so the reviewer and owner see it.
- [ ] **Step 7: Return-type ruling per function.** For each family function, rule whether the double
  chain ends at the EXISTING int signature (default — the boundary IS the return/field-write) or a
  double-returning internal variant is justified (only if a caller in the SAME family would otherwise
  re-truncate then re-widen). Default is the simplest: boundary at the existing signature. Signatures
  that return ratings (`get_real_OB` etc.) keep their int return — callers are int-world.
- [ ] **Step 8: The T2 split + EXPECTED-DRIFT SET.** Propose the definitive T2 task split (default:
  **T2a** vitals — `recalc_abilities` HP/mana/move; **T2b** OB/PB/DB trio; **T2c** `hit()` damage +
  `natural_attack_dam` + `get_weapon_damage`'s dam_coef; **T2d** speed/energy-regen — `recalc_abilities`
  `str_speed`/`null_speed`/`ENE_regen` block + `get_weapon_damage`'s speed sub-chain). **Because family
  1 and family 4 both live in `recalc_abilities`, and `get_weapon_damage` spans families 3 and 4, rule
  the exact per-block ownership so no function is edited by two T2 tasks** (recommend either merging
  T2a+T2d into one `recalc_abilities` task, or a clean formula-block boundary — census decides).
  Enumerate the EXPECTED-DRIFT SET: run `ctest --preset macos-arm64` on the unchanged tree, then
  identify by name every test that hard-codes a formula-dependent exact value or reads
  `combat_transcript_seed42.txt` (grep the test tree for exact-value combat assertions). Confirmed
  golden reader: `CharacterizationCombatTest.DamageTranscriptSeed42` (`characterization_combat_tests.cpp:140`).
- [ ] **Step 9: Oracle availability + write the census.** Record that the sibling C# oracle depot
  (`RotS.Rules.Faithful`/`RotS.Rules.Modern`) is **ABSENT on this machine** (verified: no match under
  `~/Projects`) — so the T2 cross-check is best-effort per family (check again at T2 time, use if it
  has appeared, record absence otherwise; absence is NOT a STOP). Finalize `fpi-census.md` with the
  full disposition: boundary-site list, delta bounds, EXPECTED-DRIFT SET, tie flags, return rulings,
  T2 split. No code changes this task.

---

### Task 1: `to_game_int` helper + paired-reference scaffolding (consumer-free, Sonnet)

**Files:**
- Modify: `src/fp_policy.h` (add `rots::fp::to_game_int`; `#include <cmath>`).
- Create: `src/tests/fp_interiors_reference_tests.cpp` (TEST-ONLY byte-verbatim int references +
  paired-bound tests + exact-value tests — one file, or per-family files if the census prefers; wire
  each into BOTH build systems).
- Modify (SAME commit as each new test file): `src/tests/Makefile` `SRCS`; `src/CMakeLists.txt`
  `tests/*.cpp` list (~line 1253 ff).

**Interfaces:**
- Consumes: `fpi-census.md` (the OLD int formula bodies to extract, the per-formula delta bounds, the
  hand-computed exact points).
- Produces: `rots::fp::to_game_int(double) -> int` (the boundary every T2 site calls); the
  `*_int_reference(...)` free functions (test-tree only, NEVER linked into the game) each T2 family's
  paired tests compare against.

- [ ] **Step 1:** Add the helper to `src/fp_policy.h` (Python byte-edit, preserve CRLF profile):

```cpp
#include <cmath>

// Combat-math double-interior boundary (fp-interiors wave, 2026-07). Every core-combat formula
// computes in double and lands its result in its unchanged int destination through exactly this
// call -- one uniform std::lround (round-half-away-from-zero), no exception table (storage stays
// int, so display/cost/death logic only ever sees ints). See docs/BUILD.md "FP determinism" and
// docs/superpowers/specs/2026-07-22-fp-interiors-design.md. Grep 'to_game_int' for every boundary;
// no bare lround/cast is allowed at a converted call site.
namespace rots::fp {
inline int to_game_int(double value)
{
    return static_cast<int>(std::lround(value));
}
}
```

- [ ] **Step 2:** Write the failing exact-value tests for `to_game_int` FIRST (round-half-away-from-zero
  at `.5` ties both signs, truncation-vs-round cases, negatives). Run, confirm they fail (helper not
  yet visible / wrong include), then confirm they pass after Step 1. These are permanent.
- [ ] **Step 3:** For EACH family (per the census's extract list), copy the OLD integer formula
  **byte-verbatim** into a `static int <name>_int_reference(...)` in the test file — the pre-conversion
  arithmetic exactly, all-int. These references are the frozen oracle; they are NEVER included by the
  game binary (test tree only).
- [ ] **Step 4:** Write the paired tests: for a spread of representative inputs (census supplies the
  input vectors, including the boundary/tie inputs from Step 6), assert
  `abs(<double-formula-under-test> − <name>_int_reference(...)) <= <census bound>`. **In T1 the
  double formula under test is a TEST-LOCAL double transcription** (the live code is untouched this
  task — consumer-free); T2 will point the same assertions at the live converted function. Also add
  exact-value tests at the census's hand-computed points (the new math's expected value, computed by
  hand in the census, not by running the code).
- [ ] **Step 5:** Wire the new test file(s) into BOTH `src/tests/Makefile` `SRCS` and
  `src/CMakeLists.txt` in the SAME commit (flat-parity BINDING). Grep-verify both lists contain it.
- [ ] **Step 6:** Full dual-host gates + `macos-arm64-asan` (new test file). **Goldens UNCHANGED this
  task** (live code untouched — the EXPECTED-DRIFT SET must still pass 100%; if any combat golden
  drifts here, that is a bug — a T1 test must not touch live formulas). Commit (helper + references +
  tests, flat-paired).

---

### Task 2a: Family 1 conversion — `recalc_abilities` vitals (HP/mana/move) (Sonnet)

> Per T0 Step 8 this task's exact scope (and whether it merges with T2d, since both edit
> `recalc_abilities`) is the census's ruling. The steps below assume the default split; follow the
> census if it merged/reordered.

**Files:**
- Modify: `src/entity_lifecycle.cpp:1977-2003` (the HP/mana/move blocks of `recalc_abilities`, Python
  byte-edit, preserve CRLF).
- Modify: `src/tests/fp_interiors_reference_tests.cpp` (repoint the family-1 paired tests at the live
  `recalc_abilities` result; keep the `_int_reference`).

**Interfaces:**
- Consumes: `rots::fp::to_game_int`; the family-1 `*_int_reference`; the census boundary-site list.
- Produces: `recalc_abilities` computing HP/mana/move in `double` with `int` field writes through
  `to_game_int`; the int signature and every struct field type UNCHANGED.

- [ ] **Step 1:** Convert in place: the participating locals/intermediates in the HP/mana/move blocks
  → `double`; integer literals that participate → double literals; the final `character->abilities.hit
  = to_game_int(<double expr>)`, `.mana = to_game_int(...)`, `.move = to_game_int(...)`. Preserve the
  `std::min`/`std::max` clamps and the Defender/stealth/race adjustments — convert them into the double
  chain where they precede the boundary, or keep them int-side of the boundary exactly where the census
  ruled. No `number()` call is involved here (no RNG). No bare cast/lround.
- [ ] **Step 2:** Repoint the family-1 paired tests at the live function; they must go green within the
  census bound. Add any census-required exact-value test at a hand-computed input (the known "~1 HP"
  artifact point is a natural marquee case — the double result should be the un-truncated value).
- [ ] **Step 3 (oracle cross-check, best-effort):** re-check for the C# oracle depot; if present,
  cross-check the HP/mana/move formulas against `RotS.Rules.Faithful`/`Modern` where they correspond
  and record the comparison; if absent (expected on this machine), record absence — NOT a STOP.
- [ ] **Step 4:** Full dual-host gates + `macos-arm64-asan` (test file changed). **EXPECTED-DRIFT SET
  failures permitted and enumerated by name in the report; any other failure = STOP.** Boot goldens
  byte-identical. Commit.

---

### Task 2b: Family 2 conversion — OB/PB/DB trio (Sonnet)

**Files:**
- Modify: `src/visibility.cpp:151-263` (`get_real_OB`) and `get_real_parry` (`:265-…`).
- Modify: `src/char_utils_combat.cpp:384-435` (`get_real_dodge`).
- Modify: `src/tests/fp_interiors_reference_tests.cpp` (repoint the family-2 paired tests; census may
  add `visibility_tests.cpp` coverage riders per the coverage-gap rule).

**Interfaces:**
- Consumes: `to_game_int`; family-2 `*_int_reference`s; census sub-chain map.
- Produces: the three rating functions computing in `double`, each returning `int` through `to_game_int`
  at its `return` boundary; contributing sub-chains (tactics blocks, arda malus, confuse modifier,
  weapon-master bonus) folded into the double chain per census.

- [ ] **Step 1:** Convert each function's interior to `double` (locals `tmpob`/`tmpparry`/`dodge` etc.
  → `double`; participating literals → double; the tactics `switch` arms, arda `* 4/5 - sun_mod`
  blocks, and confuse `* 2/3` malus computed in double). Every `return` becomes `return
  to_game_int(<double expr>)`. Keep the NPC early-returns as int-boundary `to_game_int` returns too
  (they are the same rating boundary). `weapon_master.get_bonus_OB()/get_bonus_PB()` return ints —
  add them into the double chain; do not change the handler.
- [ ] **Step 2:** Repoint the family-2 paired tests; green within bounds. Add exact-value points from
  the census. Add `visibility_tests.cpp` coverage riders if the census flags previously-untested
  converted paths (coverage-gap rule), flat-paired same-commit if a new file.
- [ ] **Step 3 (oracle cross-check, best-effort):** as T2a Step 3, for the rating formulas.
- [ ] **Step 4:** Full dual-host gates + ASan (if a test file changed). EXPECTED-DRIFT SET enumerated;
  any other failure = STOP. Boot goldens byte-identical. Commit.

---

### Task 2c: Family 3 conversion — `hit()` damage (Sonnet)

**Files:**
- Modify: `src/fight.cpp:2688-2695` (the core damage formula) and `natural_attack_dam` (`:2519-2539`);
  `get_weapon_damage`'s `dam_coef` chain (`char_utils_combat.cpp:239-261`) IF the census assigns its
  damage half here (vs its speed half to T2d).
- Modify: `src/tests/fp_interiors_reference_tests.cpp`; possibly `damage_tests.cpp`/`fight_proc_tests.cpp`
  coverage riders per census.

**Interfaces:**
- Consumes: `to_game_int`; family-3 `*_int_reference`; the `damage_roll = number(0,100)` int draw
  contract.
- Produces: the damage formula computing in `double` with a single `to_game_int` at the `dam =`
  boundary; the RNG draw sequence byte-identical.

- [ ] **Step 1:** Convert `fight.cpp:2695`: keep `int damage_roll = number(0, 100)` at `:2689` and
  `weapon_master.do_on_damage_rolled(...)` at `:2692` EXACTLY as-is (int draw first, unchanged RNG),
  then compute `dam = to_game_int((double)dam * (OB + 100) * (10000.0 + (double)damage_roll *
  damage_roll + (IS_TWOHANDED(ch) ? 2 : 1) * 133.0 * GET_BAL_STR(ch)) / 13300000.0)` — the whole
  product/division in double, one boundary. `dam += GET_DAMAGE(ch) * 10` at `:2688` precedes it (int
  input to the double chain — census rules whether it folds in). Convert `natural_attack_dam`'s
  `dam = level_factor + str_factor + warrior_factor` and its `* 0.75/0.50` block to a clean double
  chain through `to_game_int` (retiring the existing `(int)((double)dam * …)` casts — those become the
  one boundary). The downstream `check_find_weakness`/`wild_fighting_effect`/`armor_effect` etc. stay
  int-in/int-out (out of scope) unless census pulls a specific one in.
- [ ] **Step 2:** Repoint family-3 paired tests; green within bounds; exact-value points. Coverage
  riders in `damage_tests.cpp`/`fight_proc_tests.cpp` per census, flat-paired.
- [ ] **Step 3 (oracle cross-check, best-effort):** as before, for the damage formula.
- [ ] **Step 4:** Full dual-host gates + ASan (if test file changed). EXPECTED-DRIFT SET enumerated
  (this task is the one most likely to drift `CharacterizationCombatTest.DamageTranscriptSeed42`); any
  other failure = STOP. Boot goldens byte-identical. Commit.

---

### Task 2d: Family 4 conversion — speed/energy-regen (Sonnet)

**Files:**
- Modify: `src/entity_lifecycle.cpp:2014-2048` (the `str_speed`/`null_speed`/`ENE_regen` block of
  `recalc_abilities`) and `src/char_utils_combat.cpp:242-253` (`get_weapon_damage`'s speed sub-chain)
  per the census's family-3/4 boundary. The `do_squareroot` helper (`entity_lifecycle.cpp:1957`,
  `char_utils_combat.cpp:140`) stays `sqrt`-based (in-policy) — census rules whether its
  int-in/int-out signature is a boundary or is pulled into a double chain.
- Modify: `src/tests/fp_interiors_reference_tests.cpp`.

**Interfaces:**
- Consumes: `to_game_int`; family-4 `*_int_reference`; the `points.ENE_regen` int field contract; the
  `fight.cpp:2938` pulse consumer (unchanged).
- Produces: the harmonic-mean speed/regen chains computing in `double` with a single `to_game_int` at
  the `points.ENE_regen`/`ene_regen` boundary. `utils::get_energy_regen` (`char_utils.cpp:1350`) is a
  thin wrapper reading the already-stored int `points.ENE_regen` — census rules whether it is touched
  at all (likely NOT: the precision lives in the SETTER, `recalc_abilities`, not the getter).

- [ ] **Step 1:** Convert the harmonic-mean chain in double: `null_speed`, `str_speed`, `dex_speed`,
  the `1000000/(1000000/str_speed + 1000000/null_speed²)` combine, and `do_squareroot(tmp/100)/20` →
  a double chain landing `GET_ENE_REGEN(character) = to_game_int(<double>)`. Preserve the race/weapon
  bonuses and the `rots::entity::dispatch_attack_speed_multiplier` multiply (`:2045`) — the multiply
  is a float already; fold it into the double chain before the boundary if census rules so, else keep
  its existing shape. Mirror the same conversion in `get_weapon_damage`'s speed sub-chain if census
  assigned it here. Note the `points.ENE_regen *= <multiplier>` at `:2045` is a post-boundary int
  write today — census rules whether the multiply moves inside the double chain (one boundary) or
  stays as a separate int op.
- [ ] **Step 2:** Repoint family-4 paired tests; green within bounds; exact-value points (the harmonic
  mean is the census's worst-truncation case — its bound is the loosest; verify the double result
  lands near the reference's ceiling of accumulated truncation loss).
- [ ] **Step 3 (oracle cross-check, best-effort):** as before, for the speed/regen formula.
- [ ] **Step 4:** Full dual-host gates + ASan (if test file changed). EXPECTED-DRIFT SET enumerated;
  any other failure = STOP. Boot goldens byte-identical. Commit. **After this task all four families
  are converted; the live code no longer truncates per-step in any in-scope chain.**

---

### Task 3: Golden regen (ONE commit) + same-seed transcript diff (Sonnet)

**Files:**
- Modify: `src/tests/goldens/combat_transcript_seed42.txt` (and any other host-regenerable combat
  characterization golden T0 enumerated) via `UPDATE_GOLDENS=1`. **NEVER touch `legacy_*_fixture.bin`
  (32-bit-only) and NEVER regenerate on a 64-bit build — this regen runs on the host presets for the
  combat characterization golden ONLY.**
- Produce (attach to PR, not committed): the old-vs-new same-seed transcript diff under
  `.superpowers/sdd/fpi-transcript-diff.md`.

**Interfaces:**
- Consumes: T2a–T2d complete (all formulas converted); the pre-wave locked combat-smoke baseline
  (`docs/superpowers/goldens/combat-smoke/transcript.golden`).
- Produces: the single intentional golden update; the transcript diff the PR presents and the Fable
  reviewer + owner examine.

- [ ] **Step 1:** Regenerate the combat characterization golden on the host preset:
  `cd src && UPDATE_GOLDENS=1 ctest --preset macos-arm64 -R CharacterizationCombatTest` (env vars
  inherit into the test process; the suite's `UPDATE_GOLDENS` branch is at
  `characterization_combat_tests.cpp:245`). Verify
  ONLY the enumerated combat characterization golden(s) changed (git diff shows nothing under
  `legacy_*_fixture.bin`). Commit alone, message declaring the intentional regen and citing the wave
  and the balance-change rationale (standing rule).
- [ ] **Step 2:** Produce the transcript diff: run `tools/combat_smoke.py` at a fixed `ROTS_RNG_SEED`
  against the pre-wave baseline and the converted build (capture rung — informational), diff the two
  transcripts, and write a readable summary to `.superpowers/sdd/fpi-transcript-diff.md` (what changed,
  the direction — expected upward bias removal, the marquee "~1 HP" case). This is NEVER a gate; it is
  the artifact the owner reviews before merging.
- [ ] **Step 3:** Full dual-host gates — now the EXPECTED-DRIFT SET must PASS again (golden regenerated);
  boot goldens byte-identical; nine linkchecks; ConvertEquivalence 17/17; string-view census exit 0.
  Any residual drift = a formula bug or an incomplete regen → STOP. Commit any test-side follow-up
  (exact-value assertions that superseded a drifted characterization assertion) if the census flagged
  them, flat-paired.

---

### Task 4: Docs (Sonnet)

**Files:**
- Modify: `docs/BUILD.md` ("FP determinism" section — the double-interior policy, `to_game_int`, the
  no-exception uniform `lround` boundary, `sqrt` in-policy).
- Modify: the Option C / double-precision-deferral record
  (`docs/superpowers/…` double-precision deferral decision, 2026-07-15) — status update: Phase 3 math
  delivered early via double interiors; Phase 2 storage remains deferred to the JSON-native pass.
- Modify: `AGENTS.md` — Testing Guidelines chain entry (from **1510**, per-task deltas reconciled:
  T1 + N, T2a–T2d + M each, T3 golden regen, i386 pending T5 noted); the four converted families and
  their tiers; no membership/DEFER change (combat row stays DONE).
- Modify: `docs/superpowers/specs/2026-07-22-fp-interiors-design.md` — append an **As-built** section
  (the profs.cpp→entity_lifecycle.cpp and char_utils_combat→char_utils location corrections; the final
  T2 split; the EXPECTED-DRIFT SET; the tie-asymmetry flags reviewed; oracle absence recorded).

- [ ] **Step 1:** Write the docs pass with the reconciled per-task test chain (baseline 1510, deltas
  from each task report). Python byte-edits for any `.md` under a CRLF profile (measure). Full dual-host
  gates once if any `src` doc-macro or comment is touched (docs-only otherwise needs no gate, but run
  the string-view census). Commit (docs; the As-built spec append may be a separate commit).

---

### Task 5: Finalization (i386 battery → Fable whole-branch review → CI-green PR; NO merge)

- [ ] **Step 1:** i386 battery — `scripts/i386-battery.sh` (sequential, fresh-branch full run; per
  MEMORY, 60–90+ min, never concurrent). Expect ctest N/N (N = 1510 + wave delta) with the same 6
  did-not-run skips; monolithic reconciliation exact (ctest-only linkcheck delta stays 9); the
  regenerated combat golden passes under qemu-i386 SSE (a mismatch here is a REAL finding — Phase 1
  determinism is what makes the i386 leg meaningful for double math). A monolithic SIGSEGV = clean
  rebuild + investigate, never tolerate.
- [ ] **Step 2:** Whole-branch review (**Fable**) — reviews the CODE AND the transcript diff
  (`.superpowers/sdd/fpi-transcript-diff.md`): every `to_game_int` boundary is the ONLY rounding at
  its site; no bare cast/lround (`grep -rn 'lround' src/` = helper only); the RNG draws are unmoved
  int draws; the tie-asymmetry flags were reviewed and the flips are the intended balance change; no
  storage/struct/persistence type changed; no transcendental entered the combat path. Doc-only fixes
  during the battery are OK; any `src` fix invalidates the battery → re-run.
- [ ] **Step 3:** Push + open the PR with all six blocking CI jobs (`legacy-32bit`, `linux-x64`,
  `sanitize-linux`, `macos-arm64`, `sanitize-macos`, `windows-msvc`; `clang-tidy-advisory`
  non-blocking) required green. The PR body PRESENTS the transcript diff and states this is a
  deliberate gameplay-affecting balance change. **STOP HERE — owner reviews the diff and merges.
  There is NO merge step in this wave.**
- [ ] **Step 4:** Bookkeeping (no branch prune — owner merges): update MEMORY
  (`double-precision-combat-deferred.md` → fp-interiors math LANDED, PR open awaiting owner merge;
  Phase 2 storage still deferred); note the ledger entry as PR-open. Report the CI-green PR + the
  transcript-diff artifact to the owner as the wave deliverable.
