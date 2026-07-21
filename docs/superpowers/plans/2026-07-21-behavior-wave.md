# Behavior Wave Implementation Plan (`mobact.cpp` → `rots_script`; `limits.cpp` → `rots_combat`)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Promote the two owner-selected behavior TUs into **existing** libraries — `mobact.cpp`
(mob-AI driver) into `rots_script` (L4-upper, 4th TU) and `limits.cpp` (tick/limits engine) into
`rots_combat` (L3, 12th TU) — with **no new library targets and no new linkchecks**. The two existing
`ScriptLayerAcyclicity` and `CombatLayerAcyclicity` linkchecks are the membership gates. Certified
layer order unchanged: `platform < core < entity < persist < world < combat < pathfind < script <
app`, **every link unidirectional, no bidirectional peer links ever.**

**Architecture:** Census + closure first (Task 0, read-only), then seams/relocations/retirements
landed consumer-free while both TUs stay app-compiled (Task 1), then per-TU call-site conversions
(Task 2), then the two membership moves as **separate gateable commits** (Task 3), then docs (Task
4), then finalization (Task 5). The wave's defining coupling is the **`limits → one_mobile_activity`
(mobact) upward cross-edge** — a permanent inversion (`combat_hooks.h` hook, mobact registers, limits
dispatches) because the L3/L4 tier boundary does not move; the seam lands consumer-free in T1 so the
two memberships stay independently gateable. Spec:
`docs/superpowers/specs/2026-07-21-behavior-wave-design.md`. Recipe:
`docs/superpowers/combat-migration-playbook.md`. Censuses (method): `.superpowers/sdd/l4-census.md`,
`.superpowers/sdd/combat-trio-census.md`. Branch: `arch/behavior` off master @l4-seed-tip.

**Tech Stack:** C++20, CMake presets + flat Makefiles, GoogleTest, `nm`-based linkchecks, Python 3
(byte-edits for CRLF/formatter-hook files), the existing seam headers (`output_seam.h`,
`combat_hooks.h`, `entity_hooks.h`, `persist_hooks.h`, `world_hooks.h`, `script_hooks.h`) — this wave
adds cells/forwarders to `combat_hooks.h`, `script_hooks.h`, `entity_hooks.h`, and `output_seam.h`
but **no new header.**

## Global Constraints

- Every moved function body is **byte-verbatim** (reviewers re-diff moved spans). CRLF/LF: check each
  file's line-ending profile BEFORE editing; use Python byte-edits where the formatter hook or mixed
  line endings threaten bytes (established method — `rots-formatter-hook-conflict` memory). The
  l4-seed CRLF map already flags `handler.cpp`/`utility.cpp`/`comm.cpp`/`act_obj2.cpp`/`pkill.cpp` as
  mixed-CRLF; **verify `mobact.cpp`/`limits.cpp`/`objsave.cpp`/`spec_ass.cpp` and every edited file
  per file anyway, do not assume.**
- Call-site conversions and relocations are the ONLY semantic edits allowed, each covered by a
  discriminator or a byte-verbatim re-diff.
- **Layer order is `platform < core < entity < persist < world < combat < pathfind < script < app`;
  every link is unidirectional and there are NO bidirectional peer links, ever.** `rots_combat`
  already PUBLIC-links `RotS::world`, so `rots_world` may never link `RotS::combat` — any
  handler-family relocation into `rots_world` must have a body that reaches only world/lower.
  `rots_script` PUBLIC-links `RotS::pathfind` + the full L3 set; `mobact` needs no new link edge.
  `rots_combat` PUBLIC-links platform/core/entity/persist/world; `limits` needs no new link edge.
- **Intra-subset closure rule (playbook):** a TU may promote only when closed over every upward edge —
  each such symbol must be (a) owned by a TU promoting in the SAME commit, (b) already resolved by a
  seam/relocation, or (c) already in a library the promoting TU links downward. A census's
  "non-blocking" label is a strong prior, NOT a build-wiring guarantee; the `*LayerAcyclicity`
  linkcheck is ground truth. **The `mobact`↔`limits` cross-edge (`one_mobile_activity`) is a
  one-directional UPWARD gate — it must be inverted via a hook (never a direct call), and that hook
  must land before either membership commit.**
- Per-task gates (both hosts): macOS arm64 preset build + ctest, `rots64` container build + ctest,
  both boot goldens (`scripts/boot-golden.sh`), **all eight** `*LayerAcyclicity` linkchecks green (the
  six existing + pathfind + script — **no new linkcheck this wave**), `ConvertEquivalence` 17/17,
  `python3 tools/string_view_census.py --check` exit 0. New/substantially-rewritten test files
  additionally run the `macos-arm64-asan` preset. i386 battery at finalization only (Task 5).
- Characterization goldens (`CharacterizationCombatTest.*`, `CharacterizationJson.*`,
  `PoisonRemovalScriptTest.*`, `boot-golden.sh verify`) must pass **unchanged** — drift = bug.
  `mobact` (mob activity/aggression/hunting/helper AI) and `limits` (affect wear-off/autosave/zone
  power) are both boot-golden-exercised, so the boot golden is a real net here.
- No death tests (standing rule). No direct `rand()`/`random()` — all randomness via `rots_rng`.
  `std::format` for composition; `-Wall -Wextra -Werror` clean; `-funsigned-char` semantics.
- STOP-and-adjudicate contract: any upward edge, cascade, or census miss a task surfaces that its
  brief did not disposition → STOP, document evidence, wait for controller ruling. The most likely
  STOPs: `char_from_room` body-reading a `rots_world`/`rots_entity` caller (barring both default
  homes), `virt_program_number`'s `nm` run surfacing a **fourth** same-shape spec-proc-dispatcher edge
  or **any different-shape** edge (the rider gate pre-authorizes up to three same-shape cells without
  stopping — see T0 Step 4), or a `recalc_zone_power`/`report_zone_power` body reaching a `rots_combat`
  symbol that bars a world home.
- Docker gates must run SYNCHRONOUSLY in implementer sessions (background container gates stall
  subagents — `subagent-docker-gate-stalls` memory).
- Test-count baseline for this wave: **1415** (l4-seed as-built, both hosts). Deltas tracked per task,
  reconciled in Task 4.
- Implementation subagents run on Sonnet (model-escalation gate); briefs generated per task from this
  plan + T0's census.

---

### Task 0: Verification census + closure check + adjudications (read-only)

**Files:**
- Create: `.superpowers/sdd/behavior-census.md` (local scratch, gitignored — the established "census
  is not a production commit" convention).

**Interfaces:**
- Produces the per-edge disposition table every later task's brief cites; confirms/overturns each
  adjudication default with `nm`/body-read evidence; resolves the `char_from_room` tier question, the
  `virt_program_number` edge count, and the "combat-peer=10 re-derive."

- [ ] **Step 1: Build current objects** — `cd src && cmake --preset macos-arm64 && cmake --build
  --preset macos-arm64 -j4` (objects in `build/macos-arm64/CMakeFiles/ageland.dir/`). Confirm the
  l4-seed tip is the base (`rots_script` = mudlle/mudlle2/script_hooks; `rots_combat` = 11 TUs).
- [ ] **Step 2: nm census** — `nm -u` on `mobact.cpp.o` and `limits.cpp.o`; demangle; resolve every
  undefined symbol against a symbol→object map over the **eight** library object dirs
  (`rots_platform`/`rots_core`/`rots_entity`/`rots_persist`/`rots_world`/`rots_combat`/`rots_pathfind`/
  `rots_script`.dir) + `ageland.dir` — the l4-census/combat-trio-census §0 method. Record per-TU
  blocking tallies and reconcile against the combat-census rows (mobact `L2-app=2/…`, limits
  `L2-app=7/…`).
- [ ] **Step 3: Closure check (playbook recipe step 2), BOTH directions of the cross-edge.**
  - Confirm **`limits → one_mobile_activity` (mobact)** is the sole `limits → mobact` edge, and
    **`mobact → limits` is EMPTY** (no `gain_exp`/limits symbol in mobact) — a one-directional UPWARD
    gate, requiring a permanent inversion (not a dissolve-on-joint-membership case).
  - Verify every mobact edge resolves downward/intra-lib under the `rots_script` placement: the 12
    `do_*` cells (downward to combat), `intelligent` (intra-lib mudlle), `find_first_step` (downward
    pathfind), `CAN_SEE`/`forget`/`get_confuse_modifier`/`mudlog_aliased_mob`/`char_exists`/`obj_*`/
    `mob_index`/`world`. Flag `virt_program_number` (spec_ass, app — the new upward blocker) and
    `no_specials` (comm, app) as the only genuine app-tier residuals.
  - Verify every limits edge resolves under the `rots_combat` placement; **re-derive the
    "combat-peer=10" tally with the fresh `nm` run** and disposition each (e.g. `saves_spell`).
- [ ] **Step 4: Adjudicate the defaults** (spec "Adjudication defaults"), with evidence:
  - `one_mobile_activity` — confirm the one external caller (limits:1398); signature `void(char_data*)`;
    hook home `combat_hooks.h`, storage `combat_hooks.cpp`, registrar `mobact.cpp`, dispatcher limits;
    loud-tripwire default.
  - `virt_program_number` (spec_ass.cpp:315) — confirm exactly one mobact edge (:64/:126) and that
    `comm.cpp:2671`/`interpre.cpp:1536-1546` stay direct; signature `void*(int)`; abort-tripwire
    (pointer return); `spec_ass.cpp` registrar; new `script_hooks.h` cell. **RIDER GATE (controller
    PRE-AUTHORIZED):** additional mobact→spec-proc edges of the SAME `void*`/spec-proc-dispatcher
    shape may be resolved with the same `script_hooks.h` abort-cell pattern **up to THREE total such
    edges without stopping** (each with its own discriminator pair, dispositioned in this census). A
    **fourth** same-shape edge, or **any different-shape** edge (data global, non-dispatcher fn), is
    an **auto-STOP** for controller adjudication.
  - `no_specials` (comm.cpp, read at mobact:122) — enumerate writers/readers; default = read accessor
    forwarder in `output_seam` (comm-owned), backed by comm.cpp; storage-move fallback.
  - `char_from_room` (handler.cpp:349) — **enumerate ALL callers** (STOP if any `rots_world`/
    `rots_entity` caller). Default = L2 `entity_hooks.h` hook (real body stays app, calls
    `stop_fighting` downward, registered app-side); L3 `rots_combat` relocation only if callers are
    app+combat-only.
  - `recalc_zone_power`/`report_zone_power` (handler.cpp:762/791; report also mage.cpp:555) —
    body-read; default RELOCATE to `rots_world` (L3); hook fallback if a combat/entity body edge bars
    it.
  - `affect_remove_notify` (handler.cpp:209) — confirm body is only `vsend_to_char` (L1) +
    `affect_remove` (L2); RELOCATE-CLEAN to L2 `entity_lifecycle.cpp`.
  - `Crash_idlesave` (objsave.cpp:980) / `Crash_extract_objs` (objsave.cpp:898) — confirm distinct
    from `Crash_crashsave`; signatures; two new `combat_hooks.h` sibling cells, registered by
    objsave.cpp.
  - `close_socket` (comm.cpp) — new `output_seam` forwarder, void.
  - `circle_shutdown` (comm.cpp global, WRITTEN limits:656) — body-read the write path + other
    writers; default = setter forwarder in the comm-owned seam; storage-move fallback.
  - `saves_spell` (spell_pa.cpp:180) — body-read; RELOCATE-CLEAN to L2 `char_utils_combat.cpp` (the
    `saves_*` precedent) unless a lower floor surfaces; the leading "combat-peer=10" item.
  - `gain_exp`/`gain_exp_regardless`/`remove_fame_war_bonuses` — confirm they stay UNTOUCHED (limits
    self-registers; legal once in-lib); record full retirement as deferred follow-on.
  - `_buf` retirement counts for both TUs; distinguish genuine globals from local shadows.
- [ ] **Step 5: Record the closure verdict and membership plan.** mobact → `rots_script` (needs the
  `one_mobile_activity` hook registration + `virt_program_number` hook + `no_specials` accessor);
  limits → `rots_combat` (needs the two `Crash_*` hooks + the four handler-family dispositions +
  `close_socket`/`circle_shutdown` seams + `saves_spell` relocation + the `one_mobile_activity`
  dispatch conversion). **Two separate membership commits, no hard ordering** (coupled only through the
  T1 seam). Note there is **no new library target and no new linkcheck.**

---

### Task 1: Seams + relocations + retirements (consumer-free, both TUs still app-compiled)

**Files:**
- Modify: `src/combat_hooks.{h,cpp}` (the `one_mobile_activity` hook + the two `Crash_*` sibling
  hooks), `src/script_hooks.{h,cpp}` (the `virt_program_number` cell), `src/output_seam.{h,cpp}`
  (`close_socket` forwarder + the `no_specials`/`circle_shutdown` accessors if seamed there),
  `src/entity_hooks.h` + its backing TU (the `char_from_room` hook if hooked),
  `src/entity_lifecycle.cpp` (`affect_remove_notify` relocation), the `rots_world` destination
  (`recalc_zone_power`/`report_zone_power` if relocated), `src/char_utils_combat.cpp` (`saves_spell`
  relocation), `src/mobact.cpp` (register `one_mobile_activity` — registrar wiring only, no conversion
  yet), `src/spec_ass.cpp` (register `virt_program_number`), `src/objsave.cpp` (register the two
  `Crash_*` hooks), `src/comm.cpp` (register `close_socket`/`no_specials`/`circle_shutdown` real
  bodies), `src/handler.cpp` (register `char_from_room` real body if hooked; source of the moved
  handler-family bodies).
- Test: extend `src/tests/combat_hooks_tests.cpp` (`one_mobile_activity`, the two `Crash_*`), the
  script-hooks test file (`virt_program_number`), the appropriate seam/entity test files (accessors,
  `char_from_room` if hooked); add coverage for the relocated bodies where surfaced (wave coverage-gap
  rule).

**Interfaces:**
- Consumes: T0 dispositions.
- Produces: `combat_hooks.h::dispatch_one_mobile_activity` (registered by mobact),
  `combat_hooks.h`'s two new `Crash_*` cells, `script_hooks.h::dispatch_virt_program_number`
  (registered by spec_ass), the `output_seam` `close_socket` forwarder + `no_specials`/
  `circle_shutdown` accessors, the L2 `entity_hooks.h::char_from_room` hook (if hooked) or its L3
  relocation, `affect_remove_notify` in L2, `recalc_zone_power`/`report_zone_power` in `rots_world`
  (if relocated), `saves_spell` in L2. All landing while mobact/limits stay app-compiled.

- [ ] **Step 1: `one_mobile_activity` hook (`combat_hooks.{h,cpp}`).** `using mobile_activity_fn =
  void (*)(char_data*)`, `set_one_mobile_activity_hook(...)`, `dispatch_one_mobile_activity(char_data*)`;
  backing storage + dispatch in `combat_hooks.cpp` (the DISPATCHER's lib). **Loud-tripwire default.**
  Add the app-side boot registration call for mobact's registrar (registrar body wired into mobact.cpp
  in Step 6, called pre-`boot_db`). TDD: failing discriminator (registered stub reached with `char_data*`
  intact; unregistered → logged tripwire) first.
- [ ] **Step 2: `virt_program_number` cell (`script_hooks.{h,cpp}`).** `using virt_program_fn =
  void* (*)(int)`, setter + `dispatch_virt_program_number(int)`; storage in `script_hooks.cpp`
  (rots_script's own seam TU, the l4-seed placement precedent). **Abort-tripwire default** (pointer
  return). Register the real `spec_ass.cpp` body pre-boot. Discriminator pair (registered reaches the
  real switch / unregistered aborts).
- [ ] **Step 3: two `Crash_*` sibling hooks (`combat_hooks.{h,cpp}`).** `Crash_idlesave`
  (`void(char_data*)`) + `Crash_extract_objs` (`void(obj_data*)`), mirroring the existing
  `Crash_crashsave` hook; registered app-side by `objsave.cpp`. Discriminator pair each.
- [ ] **Step 4: `output_seam` forwarders/accessors.** `close_socket` forwarder (void); the
  `no_specials` read accessor and `circle_shutdown` setter (per T0's home ruling — output_seam if
  seamed, else storage-move handled here). Real bodies/backing registered by `comm.cpp`. Discriminator
  or accessor test per new entry.
- [ ] **Step 5: handler-family relocations/hook (per T0).** `affect_remove_notify` → byte-verbatim
  move to L2 `entity_lifecycle.cpp` (`nm` single-definition; callers still resolve). `char_from_room`
  → L2 `entity_hooks.h` hook (real body stays in handler.cpp, registered app-side) OR L3 relocation
  per T0. `recalc_zone_power`/`report_zone_power` → byte-verbatim move to `rots_world` (or hook per
  T0); `WorldLayerAcyclicity` green. `saves_spell` → byte-verbatim move to L2 `char_utils_combat.cpp`;
  `EntityLayerAcyclicity` green.
- [ ] **Step 6: Wire mobact's `one_mobile_activity` registrar (no call-site conversion yet).** Add the
  registrar function to `mobact.cpp` and its boot call; mobact stays app-compiled, its own `:61`
  self-call and `:1398` limits call unchanged this task.
- [ ] **Step 7: Full gates + ASan** (new/extended test files). Commit per logical unit
  (one_mobile_activity hook / virt_program_number cell / Crash_* pair / output_seam accessors /
  affect_remove_notify / char_from_room / zone-power relocation / saves_spell).

---

### Task 2: mobact + limits conversions (both TUs still app-compiled)

**Files:**
- Modify: `src/mobact.cpp`, `src/limits.cpp`
- Test: discriminator audit (see Step 4)

**Interfaces:**
- Consumes: the Task 1 seams; the existing 26-cell `combat_command` table
  (`rots::combat::issue_command`); the relocated handler-family/`saves_spell` bodies.
- Produces: both TUs fully converted, still in `ROTS_SERVER_SOURCES` (legal downward/dispatch calls at
  this point) — membership is Task 3.

- [ ] **Step 1: mobact.cpp conversions** (positionally exact): the twelve `do_*` up-calls →
  `rots::combat::issue_command(combat_command::{say,assist,stand,rescue,hit,flee,wear,move,wake,sleep,
  rest,sit}, ...)` matching each site's `acmd_fn` arg shape; `virt_program_number(...)` (:64/:126) →
  `rots::script::dispatch_virt_program_number(...)`; `no_specials` (:122) → the accessor. `_buf`
  retirement (:91/123/209/218/235/329/398 → local composition). `intelligent`/`find_first_step`/
  `forget`/`CAN_SEE`/`get_confuse_modifier` need **no** conversion (downward/intra-lib once promoted,
  Task 3). Add includes (`combat_hooks.h`, `script_hooks.h`, `output_seam.h`); delete dead decls
  (`ACMD(do_get);` :36, the duplicated position-ACMD block :440-444, converted-away `do_*` decls).
- [ ] **Step 2: limits.cpp conversions:** `one_mobile_activity(i)` (:1398) →
  `rots::combat::dispatch_one_mobile_activity(i)`; `Crash_idlesave`/`Crash_extract_objs` → their new
  hooks; `char_from_room` → the L2 hook dispatch (or the relocated call per T0); `recalc_zone_power`/
  `report_zone_power` → the relocated `rots::world::` calls (or hooks); `affect_remove_notify` →
  the relocated L2 call; `close_socket` → the forwarder; `circle_shutdown = 1` (:656) → the setter;
  `saves_spell` → the relocated L2 call; `_buf` retirement. `do_flee` (:1386) → flee cell.
  `extract_char`/`extract_obj`/`stop_riding`/`send_to_*`/`add_exploit_record`/big_brother/`pkill_create`/
  `Crash_crashsave`/`gain_exp`-family need **no** new conversion (existing seams / stay-untouched).
  Delete dead decls surfaced by the conversions.
- [ ] **Step 3: Verify `gain_exp` family stays intact.** Confirm limits' `register_*_hook` self-
  registration and fight/clerics' `rots::combat::gain_exp()` dispatch all remain — do NOT delete the
  self-registration (deferred follow-on). A real read, recorded as a verified "no change."
- [ ] **Step 4: Discriminator audit** (a real read, not an assumption). Confirm registered/unregistered
  pairs exist for `one_mobile_activity`, `virt_program_number`, the two `Crash_*`, `char_from_room`
  (if hooked), the accessors, and that the twelve `do_*` cells + `send_to_*` forwarders are covered;
  add only genuine gaps (note the l4-seed `say`-cell gap-fill precedent — a long-registered cell can
  still lack a caller-side discriminator). Record any "zero added" as a verified zero.
- [ ] **Step 5: Full gates + ASan** (if any test changed) + goldens unchanged. Commit per TU.

---

### Task 3: Memberships (two separate gateable commits — no new library targets)

**Files:**
- Modify: `src/CMakeLists.txt` (move `mobact.cpp` from `ROTS_SERVER_SOURCES` → `ROTS_SCRIPT_SOURCES`;
  move `limits.cpp` from `ROTS_SERVER_SOURCES` → `ROTS_COMBAT_SOURCES`; update the source-list
  comments — which TU joined, which wave, that the `one_mobile_activity` cross-edge was inverted via a
  hook not a link). Verify `src/Makefile`/`src/tests/Makefile` need **zero** change (both pre-existing
  files with per-file rules in the flat lists — the combat-pilot/combat-trio/l4-seed checked finding);
  **verify** with `grep -rln "mobact\|limits" --include=Makefile`. Root `Makefile` needs no
  per-library edit for membership growth of existing libs (no new target).

**Interfaces:**
- Consumes: everything Tasks 1-2 produced.
- Produces: `rots_script` grown to 4 TUs (+`mobact.cpp`); `rots_combat` grown to 12 TUs
  (+`limits.cpp`); all eight `*LayerAcyclicity` linkchecks green (no new ones).

- [ ] **Step 1: `limits.cpp` → `ROTS_COMBAT_SOURCES` (its own commit).** Move out of
  `ROTS_SERVER_SOURCES`; update the comment. `CombatLayerAcyclicity` green both hosts. A STOP here =
  a census miss (adjudicate with a seam/relocation, never a stub) — the most likely being a
  "combat-peer=10" symbol T0 under-dispositioned, or a `recalc_zone_power` world-home edge into
  combat.
- [ ] **Step 2: `mobact.cpp` → `ROTS_SCRIPT_SOURCES` (its own commit).** Move out of
  `ROTS_SERVER_SOURCES`; update the comment (the engine/driver pairing rationale; `intelligent`
  intra-lib, `find_first_step` downward, `one_mobile_activity` inverted, `virt_program_number`
  hooked). `ScriptLayerAcyclicity` green both hosts. A STOP here = a mobact→app edge the closure check
  missed (e.g. a second `virt_program_number`-class spec-proc edge).
- [ ] **Step 3: All eight linkchecks green both hosts.** `Platform`/`Core`/`Entity`/`Persist`/`World`/
  `Combat`/`Pathfind`/`Script` `LayerAcyclicity`. Confirm non-vacuous is unnecessary (no new
  checker), but re-confirm the two growing checkers still pass against their enlarged archives.
- [ ] **Step 4: Flat-Makefile zero-change verification.** Confirm `src/Makefile`/`src/tests/Makefile`
  need no change for the two pre-existing files (checked, not assumed).
- [ ] **Step 5: Document the closure structure in the commit bodies.** limits and mobact are
  independent commits (no hard ordering); the `one_mobile_activity` edge is a permanent upward
  inversion (hook), not an intra-lib dissolve; full gates each commit + goldens unchanged.

---

### Task 4: Docs — playbook rows RESOLVED, DEFER → 5, §-updates

**Files:**
- Modify: `docs/superpowers/combat-migration-playbook.md` (the per-TU cost table: mark **mobact** and
  **limits** rows RESOLVED with the actual `nm`-confirmed blocking counts; record the
  `one_mobile_activity` cross-edge inversion, the `virt_program_number` new-edge finding, the
  `char_from_room` tier resolution, and the `Crash_* ×3` distinct-function confirmation; add a
  "behavior wave" section with the cost markers), `docs/BUILD.md` ("Library layering" — `rots_script`
  4 TUs / `rots_combat` 12 TUs, the `one_mobile_activity` cross-band inversion note),
  `docs/superpowers/specs/2026-07-16-library-architecture-design.md` (§10 as-built slice + the
  §3-downstream note now answering mobact's tier), `AGENTS.md` (test totals + the two libraries' new
  member counts + DEFER list 7 → 5), this wave's spec (As-built section),
  `.superpowers/sdd/progress.md` (the WAVE block).

- [ ] **Step 1:** As-built pass with a reconciled test-count chain (per-task deltas cited from task
  reports — the l4-seed Task 4 method; baseline 1415). Update DEFER list to **5** (`spec_ass`/
  `spec_pro`/`ranger`/`mage`/`spell_pa`). Coverage-gap riders if T0/T1 flagged untested surfaced code
  (standing wave rule). Focused gates. Commit.

---

### Task 5: Finalization

- [ ] **Step 1:** i386 battery — `scripts/i386-battery.sh` (sequential; markers stamp per commit; a
  fresh branch always re-runs the full battery once). Confirm the i386 CMake tree builds `rots_script`
  (4 TUs) and `rots_combat` (12 TUs) and all eight `*LayerAcyclicity` tests pass in the container.
- [ ] **Step 2:** Whole-branch review (Fable), fix wave if findings (comment/doc-only fixes wait for
  the running battery; code fixes invalidate it — re-run).
- [ ] **Step 3:** Push + PR (validation steps per repo PR guidelines) + six blocking CI jobs
  (`legacy-32bit`, `linux-x64`, `sanitize-linux`, `macos-arm64`, `sanitize-macos`, `windows-msvc`);
  `clang-tidy-advisory` non-blocking. **MERGE WHEN GREEN** — the owner has **explicitly pre-authorized
  this wave's merge** (progress.md OWNER AFK AUTHORIZATIONS: "behavior wave merge authority: MERGE WHEN
  GREEN — battery + whole-branch ready + CI all green → ff-merge + report"). Perform the ff-merge to
  master once battery + whole-branch review + all six CI jobs are green.
- [ ] **Step 4: Post-merge bookkeeping (leave the AFK run in a clean state).** After the ff-merge:
  close the wave's ledger (final reconciled test-count chain + per-task commit range recorded in
  `.superpowers/sdd/progress.md`'s WAVE block, status → merged with the merge SHA); update the
  Library-split progress memory (`rots_script` 4 TUs / `rots_combat` 12 TUs, DEFER 7 → 5, and any new
  lesson — the `one_mobile_activity` cross-band inversion, the `virt_program_number` rider outcome, the
  `no_specials`/`circle_shutdown` accessors-not-storage-move lesson); delete the merged `arch/behavior`
  branch (local + remote) and prune the worktree if one was used. Then report the finished state.
