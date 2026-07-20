# Combat-Pilot Wave Implementation Plan (clerics + fight join rots_combat)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate `clerics.cpp` then `fight.cpp` into `rots_combat` (6 → 8 TUs), proving the four
blocker-buster seams under real traffic, and deliver the combat smoke harness + migration playbook
the remaining DEFER TUs and the future int→double conversion will reuse.

**Architecture:** Characterization-first (smoke harness + baseline BEFORE any migration), then
seam riders, then per-TU migration (byte-verbatim body membership + call-site conversion to seam
dispatch). Spec: `docs/superpowers/specs/2026-07-20-combat-pilot-design.md`. Branch:
`arch/combat-pilot` off master @79fcb28.

**Tech Stack:** C++20, CMake presets + flat Makefiles (4 build systems), GoogleTest, nm-based
linkchecks, Python 3 (smoke tooling), the existing seam headers (`output_seam.h`,
`combat_hooks.h`, `entity_hooks.h`, `persist_hooks.h`, `world_hooks.h`).

## Global Constraints

- Every moved function body is **byte-verbatim** (reviewers re-diff moved spans). CRLF/LF: check
  each file's line-ending profile BEFORE editing; use python byte-edits where the formatter hook
  or mixed line endings threaten bytes (established method — see progress ledger CRASH RECOVERY
  note and `rots-formatter-hook-conflict` memory).
- Call-site conversions are the ONLY semantic edits allowed, each covered by a discriminator test.
- Per-task gates (both hosts): macOS arm64 preset build + ctest, rots64 container build + ctest,
  both boot goldens (`scripts/boot-golden.sh`), all 7 linkchecks green,
  `ConvertEquivalence` 17/17, `python3 tools/string_view_census.py --check` exit 0. New or
  substantially rewritten test files additionally run the `macos-arm64-asan` preset. From Task 3
  on, `scripts/combat-golden.sh verify` joins the per-task gates. i386 battery at finalization
  only (Task 8).
- Characterization goldens (`CharacterizationCombatTest.*`, `PoisonRemovalScriptTest.*`) must pass
  **unchanged** — drift = bug in the change.
- No death tests (standing rule). No direct `rand()`/`random()` — all randomness via `rots_rng`.
  `std::format` for composition; `-Wall -Wextra -Werror` clean; `-funsigned-char` semantics.
- STOP-and-adjudicate contract: any upward edge, cascade, or census miss a task surfaces that its
  brief did not disposition → STOP, document evidence, wait for controller ruling.
- Docker gates must run SYNCHRONOUSLY in implementer sessions (background container gates stall
  subagents — see `subagent-docker-gate-stalls` memory).
- Implementation subagents run on Sonnet (model-escalation gate decision); briefs are generated
  per task from this plan + T0's census.

---

### Task 0: Fresh census + adjudications (read-only)

**Files:**
- Create: `.superpowers/sdd/pilot-census.md`

**Interfaces:**
- Produces: the per-edge disposition table every later task's brief cites; confirms or overturns
  each "default hypothesis" below with `nm` evidence.

- [ ] **Step 1: Build current objects** — `cd src && cmake --preset macos-arm64 && cmake --build
  --preset macos-arm64 -j4` (objects land in `build/macos-arm64`).
- [ ] **Step 2: nm census** — `nm -u` on `clerics.cpp.o` and `fight.cpp.o`, demangle, resolve
  every undefined symbol against a symbol→object map built across all six library object dirs
  (`rots_platform.dir`/`rots_core.dir`/`rots_entity.dir`/`rots_persist.dir`/`rots_world.dir`/
  `rots_combat.dir`) plus `ageland.dir` — the exact combat-census method.
- [ ] **Step 3: Classify + adjudicate.** Produce the disposition table. The spec's open
  adjudications, with this plan's default hypotheses (evidence gathered 2026-07-20):
  - `special()` (interpre.h:99, `int special(char_data*, int cmd, char* arg, int callflag,
    waiting_type* wtl, ...)`) — own registered hook in `combat_hooks.h` (int return, logged
    default returning 0 = "no spec-proc consumed the event", the proceed-normally outcome). NOT a
    26th ACMD cell (wrong signature).
  - `set_mental_delay` (utility.cpp:426) — relocate to `char_utils.cpp` (L2) if census-clean
    (serves clerics now AND olog_hai later); else rots_combat.
  - `remove_character_from_group` (act_othe.cpp:519, decl utils.h:785; callers fight.cpp ×3,
    mystic.cpp, handler.cpp:421) — relocate to `char_utils.cpp` (L2) if census-clean, else hook.
  - `stop_follower` (handler.cpp:301) / `stop_riding` (handler.cpp:988) — relocate to L2
    (`char_utils.cpp` or `entity_lifecycle.cpp`) if census-clean (the follow-family pools already
    moved to entity in the placement-seam wave); else hooks.
  - `extract_char` (handler.cpp:732 + :737 overload) — session-coupled teardown, NOT relocatable
    this wave → registered hook (void, logged no-op default, room param with sentinel mapping to
    the 1-arg overload).
  - App-other trio — `Crash_crashsave` (objsave), `call_trigger` (script.cpp; **int return whose
    FALSE prevents death** — default must return TRUE = the no-script-attached outcome),
    `pkill_create` (pkill.cpp) — three registered fn-ptrs in a `combat_hooks.h` extension
    (cross-owner → hooks family per taxonomy), each registered by a per-owner registrar called
    from `run_the_game()` pre-`boot_db()` + gtest parity.
  - Handler wrappers `equip_char`/`unequip_char` — MOVE both verbatim handler.cpp → fight.cpp
    (blocker-census §E's complementary lever; fight owns `damage()`/`raw_kill()`; app callers
    act_obj2/objsave/zone/handler then call DOWN, legal). T0 verifies no handler-internal-static
    snags.
  - `waiting_list` externs (clerics.cpp:35, fight.cpp:60) — textually DEAD (zero uses); confirm
    via nm (unused externs emit no undefined symbol) → delete in Task 2.
  - `fight_messages` — storage-move db_boot.cpp:108 → fight.cpp (its loader `load_messages()` is
    ALREADY fight.cpp:120); enumerate any non-fight readers.
  - `add_exploit_record` fight.cpp ×5 — convert to the EXISTING `persist_hooks.h`
    exploit-capture dispatch (registered since the persist-split wave); verify signature match
    `(int type, char_data*, int, const char*)`.
- [ ] **Step 4: Commit** — `git add .superpowers/sdd/pilot-census.md && git commit -m "docs:
  combat-pilot census (clerics/fight residual edges + adjudications)"`.

---

### Task 1: Combat smoke harness (BEFORE any migration)

**Files:**
- Modify: `src/rots_rng.h`, `src/rots_rng.cpp` (seed helper), `src/comm.cpp:487` + `:563`
  (call the helper)
- Create: `tools/combat_smoke.py`, `scripts/combat-golden.sh`,
  `docs/superpowers/goldens/combat-smoke/` (baseline transcript golden)
- Test: `src/tests/rng_seed_tests.cpp` (new — env-override behavior)

**Interfaces:**
- Produces: `rots_rng::seed_from_environment_or_time()` (void, reads `ROTS_RNG_SEED`);
  `scripts/combat-golden.sh {capture|verify}` — the gate every migration task runs.

- [ ] **Step 1: Write failing test** (`rng_seed_tests.cpp`): `setenv("ROTS_RNG_SEED","12345",1)`,
  call `seed_from_environment_or_time()`, record `rots_rng::random()` sequence; re-seed via env,
  assert identical sequence; unset env, assert function still seeds (no crash, time path).
  POSIX-gate the setenv usage as existing POSIX-only tests do.
- [ ] **Step 2: Run to verify failure** — `ctest --preset macos-arm64 -R RngSeed` → FAIL
  (symbol undefined).
- [ ] **Step 3: Implement helper** in `rots_rng.cpp`:

```cpp
void seed_from_environment_or_time()
{
    // ROTS_RNG_SEED (decimal) pins the mt19937 stream for the combat smoke
    // harness and future deterministic replays; unset or malformed falls back
    // to the historical time(0) seeding, byte-identical default behavior.
    if (const char* env_seed = std::getenv("ROTS_RNG_SEED")) {
        char* end = nullptr;
        const unsigned long value = std::strtoul(env_seed, &end, 10);
        if (end != env_seed && *end == '\0') {
            seed(static_cast<unsigned int>(value));
            return;
        }
    }
    seed(static_cast<unsigned int>(std::time(nullptr)));
}
```

  Declare in `rots_rng.h`; replace the two `rots_rng::seed(...time(0)...)` calls at
  comm.cpp:487/563 with `rots_rng::seed_from_environment_or_time()`.
- [ ] **Step 4: Test passes** — `ctest --preset macos-arm64 -R RngSeed` → PASS. Commit.
- [ ] **Step 5: Write `tools/combat_smoke.py`** (model: `tools/account_smoke.py` — expect-style,
  marker-paced, NEVER sleep-paced): boot `ageland` on a scratch port with scratch `lib/` data and
  `ROTS_RNG_SEED=<fixed>`; connect; create the Implementor character (fresh-setup promotion);
  script a deterministic fight (load a fixed mob vnum, initiate combat, read until death message);
  emit the transcript between explicit BEGIN/END markers. CLI: `--mode {capture|verify}`,
  `--seed`, `--port`, `--binary`, `--transcript-dir`.
- [ ] **Step 6: Write `scripts/combat-golden.sh`** mirroring `scripts/boot-golden.sh`'s
  `capture`/`verify` UX (incl. `--native <binary>` / `--service <name>` forms), storing goldens
  under `docs/superpowers/goldens/combat-smoke/`.
- [ ] **Step 7: Determinism probe.** Run capture twice at the same seed; byte-compare. If stable
  → raw-transcript comparison is the verify mode. If flaky → apply the spec's fallback ladder:
  (a) normalized comparison (combat message sequence — hit/miss/damage/death lines only),
  documented; (b) if still unstable, harness lands capture-only, gap NAMED in the task report and
  the wave's bar rests on discriminators + goldens. Do not silently accept (b).
- [ ] **Step 8: Capture the pre-migration baseline** at this task's HEAD on macOS native; verify
  on rots64 (`scripts/combat-golden.sh --service rots64 verify`) to prove cross-platform
  determinism of the seeded stream. Commit tools + baseline.
- [ ] **Step 9: Full per-task gates** (Global Constraints) + ASan preset (new test file). Commit.

---

### Task 2: Shared riders (seams + retirements; consumer-free)

**Files:**
- Modify: `src/clerics.cpp` (buf retirement :220-227, dead extern :35),
  `src/fight.cpp` (dead extern :60), `src/entity_hooks.h` + its storage TU (big_brother hooks),
  `src/big_brother.cpp` (registrar), `src/combat_hooks.h` + `src/combat_hooks.cpp`
  (`special` hook), `src/interpre.cpp` (register special), `src/comm.cpp` + `src/gtest_main.cpp`
  (registration parity), `src/utility.cpp` + destination TU (set_mental_delay move),
  `src/act_othe.cpp` + destination TU (remove_character_from_group move, per T0)
- Test: extend `src/tests/combat_hooks_tests.cpp` + big_brother hook tests (registered-path
  semantics; no death tests for tripwires)

**Interfaces:**
- Consumes: T0 dispositions.
- Produces: `rots::combat::set_special_handler(special_fn)` + `rots::combat::call_special(...)`
  (exact param list mirrors interpre.h:99 verbatim); big_brother hook pair in `entity_hooks.h` —
  `set_target_valid_hook` (bool return; logged default TRUE = permissive legacy semantics —
  big_brother VETOES; matches the neutral-default class of the float hooks) and
  `set_character_died_hook` (void; logged no-op); relocated `set_mental_delay` /
  `remove_character_from_group` / `stop_follower` / `stop_riding` at their T0-adjudicated homes
  with signatures unchanged (decls in utils.h stay).

- [ ] **Step 1:** Delete the two dead `waiting_list` externs (T0-confirmed). Build both hosts.
- [ ] **Step 2:** clerics buf retirement — each `strcpy(buf, std::format(...).c_str()); act(buf,
  ...)` pair at clerics.cpp:220-227 becomes a local: `const std::string will_msg =
  std::format(...); act(will_msg, ...)` (act takes string_view via output_seam). Byte-identical
  output strings (world-seed buffer-retirement precedent).
- [ ] **Step 3:** TDD the `special` hook: failing discriminator test (register a recording stub →
  `call_special` reaches it with all args intact; unregistered → logged default returns 0), then
  implement in combat_hooks.{h,cpp}; `register_combat_command_dispatch()` (interpre.cpp) gains
  the `special` registration; gtest_main parity.
- [ ] **Step 4:** TDD the big_brother hooks (same pattern; registration in big_brother.cpp's
  registrar, wired where `register_attacked_player_hook` already rides in run_the_game +
  gtest_main).
- [ ] **Step 5:** Relocations per T0 (`set_mental_delay`, `remove_character_from_group`,
  `stop_follower`, `stop_riding` if adjudicated L2-clean) — byte-verbatim moves, nm
  single-definition check, linkchecks green.
- [ ] **Step 6:** Full gates + ASan (new/extended test files). Commit per logical unit
  (retirements / special hook / bb hooks / relocations).

---

### Task 3: Clerics migration + playbook draft

**Files:**
- Modify: `src/clerics.cpp` (call-site conversions), `src/CMakeLists.txt`
  (ROTS_COMBAT_SOURCES), `src/Makefile`, `src/tests/Makefile` (membership move — follow the BB
  Task 4 wiring pattern exactly)
- Create: `docs/superpowers/combat-migration-playbook.md` (draft)
- Test: discriminator coverage check (see Step 2)

**Interfaces:**
- Consumes: `rots::combat::issue_command` (combat_hooks.h:141), `rots::combat::call_special`
  (Task 2), big_brother hooks (Task 2).
- Produces: clerics.cpp as the 7th `rots_combat` TU; playbook draft.

- [ ] **Step 1: Convert up-calls.** `do_flee(...)` at clerics.cpp:239/264/512/575 →
  `rots::combat::issue_command(rots::combat::combat_command::flee, victim, mutable_arg(""),
  NULL, 0, 0);` (args positionally identical). `special(...)` at :308 →
  `rots::combat::call_special(killer, 0, mutable_arg(""), SPECIAL_DAMAGE, &wait_data)`.
  `bb_instance.is_target_valid(...)` at :156/:319 → the Task 2 hook dispatch. Delete the now-dead
  `ACMD(do_flee);` forward decl at :88. `do_mental` (ACMD **defined** in clerics.cpp) needs NO
  change — interpre.cpp's registrar keeps registering it; the pointer simply lands in the library.
- [ ] **Step 2: Discriminator audit.** Verify a `CombatHooksDispatch.*` discriminator exists for
  EVERY cell/hook this task's conversions target (`flee`, `special`, `is_target_valid`); add any
  missing (BB Task 2 landed 9 + mental's pair — audit, don't assume).
- [ ] **Step 3: Membership move** — clerics.cpp from `ROTS_SERVER_SOURCES` to
  `ROTS_COMBAT_SOURCES` in all four build systems. `CombatLayerAcyclicity` must go green; a STOP
  here = census miss, adjudicate before stubbing anything.
- [ ] **Step 4: Full gates** incl. `scripts/combat-golden.sh verify` (transcript unchanged vs
  Task 1 baseline) + goldens unchanged. Commit.
- [ ] **Step 5: Playbook draft** — record the actual recipe + costs (edges converted, tests
  added, surprises) in `docs/superpowers/combat-migration-playbook.md`. Commit.

---

### Task 4: Fight riders (seams + storage; consumer-free)

**Files:**
- Modify: `src/db_boot.cpp` (fight_messages definition out), `src/fight.cpp` (storage in,
  load_messages buf/buf2 retirement), `src/combat_hooks.h` + `.cpp` (extract_char +
  Crash_crashsave/call_trigger/pkill_create hooks per T0), `src/handler.cpp` (wrapper moves out),
  registrar homes per T0 (`src/objsave.cpp`, `src/script.cpp`, `src/pkill.cpp` or comm.cpp),
  `src/comm.cpp` + `src/gtest_main.cpp` (parity)
- Test: extend `src/tests/combat_hooks_tests.cpp` (new hooks' registered-path + default-path
  semantics; `call_trigger` default MUST be tested to return TRUE)

**Interfaces:**
- Consumes: T0 dispositions.
- Produces: `fight_messages[MAX_MESSAGES]` storage in fight.cpp (extern in db.h/utils.h
  unchanged); hooks `set_extract_char_hook`/`set_crash_save_hook`/`set_death_trigger_hook`
  (int return, default TRUE)/`set_pkill_create_hook` + dispatch fns; `equip_char`/`unequip_char`
  bodies in fight.cpp (decls unchanged — app callers unaffected).

- [ ] **Step 1:** `fight_messages` storage-move (db_boot.cpp:108 → fight.cpp, beside its loader
  `load_messages()` at fight.cpp:120) with ownership comment; nm single-definition both link
  targets.
- [ ] **Step 2:** fight.cpp `buf`/`buf2` retirement — **ALL 41 uses** (measured 2026-07-20; T0
  re-enumerates), not just the load_messages cluster at :156-183. Same local-composition pattern
  per use class (db_players/db_world precedent); note `fread_string`'s label param type before
  choosing `std::string::c_str()` vs a local char array. Output strings byte-identical.
- [ ] **Step 3:** TDD the four hooks (failing discriminator per hook → implement → pass); wire
  per-owner registrars, run_the_game + gtest_main parity. NO fight.cpp call-site conversion in
  this task (consumer-free, mirrors BB).
- [ ] **Step 4:** Wrapper move — `equip_char`/`unequip_char` bodies handler.cpp → fight.cpp,
  byte-verbatim; poison block untouched; `PoisonRemovalScriptTest.*` unchanged (no double-fire);
  all app callers (act_obj2/objsave/zone/handler) still resolve (decls unchanged).
- [ ] **Step 5:** Full gates + ASan + smoke verify. Commit per logical unit.

---

### Task 5: Fight migration

**Files:**
- Modify: `src/fight.cpp` (call-site conversions), the four build systems (membership move)
- Test: discriminator audit (stand + all Task 4 hooks)

**Interfaces:**
- Consumes: everything Tasks 2/4 produced.
- Produces: fight.cpp as the 8th `rots_combat` TU; `combat_list`/`combat_next_dude` and the
  poison-hook impl + `register_poison_removal_hook()` now lib-resident (comm.cpp keeps calling
  the registrar — downward, legal).

- [ ] **Step 1: Convert up-calls** (each positionally identical to the direct call):
  `do_flee` ×6 (fight.cpp:1921/1929/1934/2677/2683/2688) + `do_stand` ×1 (:2834) →
  `issue_command`; `special` ×3 (:894/:1667/:2649) → `call_special`;
  `bb_instance.is_target_valid` ×2 (:1618/:2471) + `on_character_died` (:914) → Task 2 hooks;
  `Crash_crashsave` (:919), `call_trigger` (:1003 — keep the `== FALSE` prevents-death branch
  shape exactly), `pkill_create` (:1073) → Task 4 hooks; `add_exploit_record` ×5
  (:1047/:1061/:1074/:1079 + T0's enumeration) → the existing persist_hooks dispatch;
  `extract_char` ×3 (:951/:954/:959) → extract_char hook (room-sentinel mapping for :959).
  `unequip_char`/`stop_riding`/`stop_follower`/`remove_character_from_group` resolve per their
  Task 2/4 homes (in-lib or L2 — no seam needed).
- [ ] **Step 2: Discriminator audit** for every newly-exercised cell/hook (`stand`, the Task 4
  four); add missing ones.
- [ ] **Step 3: Membership move** ×4 build systems; `CombatLayerAcyclicity` green (STOP contract
  on any surprise edge); nm single-definition for combat_list/fight_messages/poison impl.
- [ ] **Step 4: Full gates** — goldens (`CharacterizationCombatTest.*`,
  `PoisonRemovalScriptTest.*`) unchanged, smoke verify vs the SAME Task 1 baseline, both hosts,
  ASan if tests changed. Commit.
- [ ] **Step 5: Playbook update** with fight's actual costs. Commit.

---

### Task 6: Migration playbook (finalize)

**Files:**
- Modify: `docs/superpowers/combat-migration-playbook.md`

- [ ] **Step 1:** Finalize the recipe (census → riders → membership → conversions →
  verification) and the per-TU cost table for the remaining DEFER TUs
  (mobact/spec_pro/ranger/olog_hai/mystic/mage/limits/spell_pa/spec_ass + profs), marking each
  TU's known blockers from combat-census + what this wave's seams already cover. Commit.

---

### Task 7: Docs as-built

**Files:**
- Modify: `docs/BUILD.md` (rots_combat section: 8 TUs, new hooks, smoke harness), `AGENTS.md`
  (test totals, rots_combat description, smoke-gate mention), parent spec
  `docs/superpowers/specs/2026-07-16-library-architecture-design.md` (additive as-built note),
  wave spec As-built section

- [ ] **Step 1:** As-built pass with reconciled test-count chain (per-task deltas cited from task
  reports — the BB Task 5 method). Coverage riders if T0 flagged untested surfaced code
  (standing wave rule). Focused gates. Commit.

---

### Task 8: Finalization

- [ ] **Step 1:** i386 battery — `scripts/i386-battery.sh` (sequential; markers stamp per
  commit).
- [ ] **Step 2:** Whole-branch review (Fable), fix wave if findings (comment/doc-only fixes wait
  for the running battery; code fixes invalidate it — re-run).
- [ ] **Step 3:** Push + PR (validation steps listed per repo PR guidelines) + six blocking CI
  jobs + merge under the owner-pre-authorized protocol (combat-seed/blocker-buster precedent).
  Update `.superpowers/sdd/progress.md` throughout.
