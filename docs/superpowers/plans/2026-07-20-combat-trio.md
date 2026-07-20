# Combat-Trio Wave Implementation Plan (olog_hai + mystic join rots_combat; profs conditional)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate `olog_hai.cpp` and `mystic.cpp` into `rots_combat` (8 → 10 TUs), and `profs.cpp`
too (→ 11) **if and only if** Task 0's fresh census fires the rider gate. This is the second
application of the migration playbook and the first to promote TUs **standalone** (no forced cycle),
so it also validates that the recipe's closure check works for the non-mutual case.

**Architecture:** Census + closure first (Task 0, read-only), then seams/relocations/scratch
retirements landed consumer-free while all TUs stay app-compiled (Task 1), then per-TU up-call
conversions (Task 2 olog+mystic; Task 3 profs-if-rider), then a single membership commit +
`CombatLayerAcyclicity` (Task 4). Spec:
`docs/superpowers/specs/2026-07-20-combat-trio-design.md`. Recipe:
`docs/superpowers/combat-migration-playbook.md`. Branch: `arch/combat-trio` off master @26afa9a.

**Tech Stack:** C++20, CMake presets + flat Makefiles, GoogleTest, `nm`-based linkchecks, Python 3
(byte-edits for CRLF/formatter-hook files), the existing seam headers (`output_seam.h`,
`combat_hooks.h`, `entity_hooks.h`, `persist_hooks.h`, `world_hooks.h`).

## Global Constraints

- Every moved function body is **byte-verbatim** (reviewers re-diff moved spans). CRLF/LF: check
  each file's line-ending profile BEFORE editing; use python byte-edits where the formatter hook or
  mixed line endings threaten bytes (established method — `rots-formatter-hook-conflict` memory; the
  pilot re-confirmed CRLF handling for `clerics.cpp`, LF for `fight.cpp` — verify per file, don't
  assume).
- Call-site conversions are the ONLY semantic edits allowed, each covered by a discriminator test.
- **Intra-subset closure rule (playbook, "The intra-subset rule" + "Census-methodology
  correction"):** a TU may promote only when it is closed over every combat-peer edge — each such
  symbol must be (a) owned by a TU promoting in the SAME commit, (b) already resolved by a
  seam/relocation, or (c) already in-lib. A census's "combat-peer / non-blocking" label is a strong
  prior, NOT a build-wiring guarantee; the `CombatLayerAcyclicity` linkcheck is ground truth. Run
  the closure check BEFORE picking the membership set, not after a build failure.
- Per-task gates (both hosts): macOS arm64 preset build + ctest, rots64 container build + ctest,
  both boot goldens (`scripts/boot-golden.sh`), all `*LayerAcyclicity` linkchecks green,
  `ConvertEquivalence` 17/17, `python3 tools/string_view_census.py --check` exit 0. New or
  substantially rewritten test files additionally run the `macos-arm64-asan` preset. From Task 2
  on, `scripts/combat-golden.sh verify` runs **informationally** (rung-(b), non-gating per the
  pilot's Task 1 finding — read the diff, don't treat drift alone as a regression). i386 battery at
  finalization only (Task 6).
- Characterization goldens (`CharacterizationCombatTest.*`, `PoisonRemovalScriptTest.*`) must pass
  **unchanged** — drift = bug in the change.
- No death tests (standing rule). No direct `rand()`/`random()` — all randomness via `rots_rng`.
  `std::format` for composition; `-Wall -Wextra -Werror` clean; `-funsigned-char` semantics.
- STOP-and-adjudicate contract: any upward edge, cascade, or census miss a task surfaces that its
  brief did not disposition → STOP, document evidence, wait for controller ruling.
- Docker gates must run SYNCHRONOUSLY in implementer sessions (background container gates stall
  subagents — `subagent-docker-gate-stalls` memory).
- Test-count baseline for this wave: **1394** (combat-pilot as-built, both hosts; i386 also
  1394/6-skips). Deltas tracked per task, reconciled in Task 5.
- Implementation subagents run on Sonnet (model-escalation gate); briefs generated per task from
  this plan + T0's census.

---

### Task 0: Fresh census + closure check + adjudications (read-only)

**Files:**
- Create: `.superpowers/sdd/combat-trio-census.md` (local scratch, gitignored — per the pilot's
  established "census is not a production commit" convention).

**Interfaces:**
- Produces the per-edge disposition table every later task's brief cites; confirms/overturns each
  default hypothesis with `nm` evidence; **decides the profs rider gate.**

- [ ] **Step 1: Build current objects** — `cd src && cmake --preset macos-arm64 && cmake --build
  --preset macos-arm64 -j4` (objects in `build/macos-arm64/CMakeFiles/ageland.dir/`).
- [ ] **Step 2: nm census** — `nm -u` on `olog_hai.cpp.o`, `mystic.cpp.o`, `profs.cpp.o`; demangle;
  resolve every undefined symbol against a symbol→object map over the six library object dirs
  (`rots_platform`/`rots_core`/`rots_entity`/`rots_persist`/`rots_world`/`rots_combat`.dir) +
  `ageland.dir` — the exact `pilot-census.md` method.
- [ ] **Step 3: Closure check (playbook recipe step 2).** For every symbol classified
  combat-peer(still-app), confirm it resolves to in-lib / same-commit-partner / existing-seam, or
  flag it a genuine blocker. **Re-derive olog's old combat-peer=6 and mystic's =8** (cost table);
  confirm they now resolve to clerics/fight/visibility (in-lib) or dispatch-indirection. Any edge
  to a still-app DEFER TU (`spell_pa`/`mobact`/`ranger`/`limits`/`mage`) = STOP-and-adjudicate
  (relocate-or-hook, the pilot §7 pattern).
- [ ] **Step 4: Adjudicate the defaults** (spec "Adjudication defaults"), with `nm` evidence:
  - `do_dismount` (olog_hai.cpp:252) — 26th `dismount` cell; enumerate ALL direct `do_dismount`
    up-calls project-wide (confirm olog is the only one this wave touches); confirm real
    `do_dismount` home + ACMD signature match.
  - `do_move` (olog_hai.cpp:544) — verify the `move` cell (combat_hooks.h:118) is registered to
    `do_move` and the call shape matches `acmd_fn`.
  - `one_argument` (interpre.cpp:1476) + `fill_word` (1511) + `fill[]` (581) — L0 `rots_util`
    package; `half_chop` (interpre.cpp:1535) — census its own downward edges, choose L0 target.
  - `add_follower` (handler.cpp:267) — confirm L2-clean (pool + `stop_follower` already L2; `act`
    via output_seam); check for handler-internal statics.
  - `get_guardian_type` (utility.cpp:978) — confirm zero fn-call edges; reads `mob_index`
    (db_world.cpp:95, L3) + `guardian_mob` (consts.cpp:2620, L1) → relocate into `rots_combat`;
    enumerate all callers (known: objsave.cpp:774, profs.cpp:226).
  - `scale_guardian` (mystic.cpp:1584) — confirm NOT leaf-relocatable (4 helper cluster
    1497-1583); confirm profs.cpp:233 is the only external caller.
  - `add_exploit_record` (profs.cpp, 8 sites) — signature match vs `persist_hooks.h` dispatch.
  - `buf`/`buf2`/`arg` retirement counts: olog (10/3/5 genuine), mystic (1 `buf`), profs (2 `buf`);
    distinguish genuine globals from `ASPELL` param / local shadows (spec cites the shadows).
- [ ] **Step 5: Decide the rider gate.** Apply the spec's gate to the `nm` evidence. Record the
  verdict (FIRE / DROP) and reasoning **explicitly** in the census. If DROP, name why loudly; the
  wave then lands olog+mystic only (Task 3 skipped, Task 4 promotes two TUs) — or olog only if
  mystic is blocked.

---

### Task 1: Seams + shared relocations + scratch retirements (consumer-free)

**Files:**
- Modify: `src/combat_hooks.h` + `src/combat_hooks.cpp` (`dismount` cell), `src/interpre.cpp`
  (register `dismount`; the `one_argument`/`half_chop`/`fill_word`/`fill[]` relocation source),
  `src/rots_util.cpp` (parse-leaf destination), `src/handler.cpp` (`add_follower` out),
  `src/entity_lifecycle.cpp` (`add_follower` in), `src/olog_hai.cpp` + `src/mystic.cpp` (buf/arg
  retirements)
- Test: extend `src/tests/combat_hooks_tests.cpp` (`dismount` registered/tripwire pair)

**Interfaces:**
- Consumes: T0 dispositions.
- Produces: `combat_command::dismount` cell (registered, discriminated); `one_argument`/`half_chop`
  (+`fill_word`/`fill[]`) at L0 `rots_util` (decls in `interpre.h` unchanged); `add_follower` body
  in `entity_lifecycle.cpp` (decl `handler.h:223` unchanged). All landing while olog/mystic/profs
  stay app-compiled.

- [ ] **Step 1: `dismount` cell.** TDD: failing discriminator (register recording stub →
  `issue_command(combat_command::dismount, ...)` reaches it with args intact; unregistered → logged
  no-op tripwire), then add the enumerator (alphabetical, before `flee`), the backing-array bump,
  and the `set_combat_command(combat_command::dismount, do_dismount)` line in
  `register_combat_command_dispatch()` (interpre.cpp). No call-site conversion yet.
- [ ] **Step 2: Parse-leaf relocation.** Byte-verbatim move `one_argument` + `fill_word` + `fill[]`
  (and `half_chop` per T0's target) from `interpre.cpp` to `rots_util.cpp`; `nm` single-definition
  check both link targets; every existing caller still resolves (decls in `interpre.h` unchanged).
  Watch the string_view census (`fill[]` is `std::string_view`) — run `--check`.
- [ ] **Step 3: `add_follower` relocation.** Byte-verbatim move handler.cpp:267 →
  `entity_lifecycle.cpp`; confirm `act`/`stop_follower`/pool all resolve downward from L2; `nm`
  single-definition; `EntityLayerAcyclicity` green.
- [ ] **Step 4: Scratch retirements.** olog_hai `buf`/`buf2` (10/3 sites) + `arg` (5 sites, each the
  `one_argument(argument, arg)` output param → a local `char first_arg[...]`); mystic `buf` (1
  site). Byte-identical output strings (world-seed local-composition pattern). CRLF byte-edit if
  the file requires (check first).
- [ ] **Step 5: Full gates + ASan** (extended test file). Commit per logical unit (dismount cell /
  parse-leaf move / add_follower move / retirements).

---

### Task 2: olog_hai + mystic conversions (still app-compiled)

**Files:**
- Modify: `src/olog_hai.cpp`, `src/mystic.cpp`
- Test: discriminator audit (see Step 3)

**Interfaces:**
- Consumes: `rots::combat::issue_command` (combat_hooks.h:163), the `dismount` cell (Task 1), the
  big_brother `is_target_valid` hook (`entity_hooks.h`, pilot Task 2), relocated `one_argument`/
  `half_chop` (Task 1, now downward L0 calls — no edit needed, they just link).
- Produces: both TUs fully converted, still in `ROTS_SERVER_SOURCES` (legal downward app→lib/L0
  calls at this point) — membership is Task 4.

- [ ] **Step 1: olog_hai conversions** (positionally exact): `do_move(ch, mutable_arg(""), 0, cmd+1,
  0)` (:544) → `issue_command(combat_command::move, ch, mutable_arg(""), 0, cmd+1, 0)`;
  `do_dismount(victim, mutable_arg(""), 0, 0, 0)` (:252) → `issue_command(combat_command::dismount,
  victim, mutable_arg(""), 0, 0, 0)`; `bb_instance.is_target_valid(...)` ×4 (:279-280/:300-301/
  :321-323/:425-426) → the 2-arg dispatch hook. Leave the file-local `is_target_valid` free function
  (:108) untouched — it is a self-call, not the big_brother method. Budget coupled dead-code cleanup
  (unused `bb_instance` locals / `big_brother.h` include if it becomes the last consumer — playbook
  recipe step 5 / point 4).
- [ ] **Step 2: mystic conversions:** `do_flee(victim, mutable_arg(""), NULL, 0, 0)` (:202) →
  `issue_command(combat_command::flee, ...)`. `one_argument`/`half_chop`/`search_block` now resolve
  as downward calls (no edit). Delete any now-dead `ACMD(do_flee);` forward decl.
- [ ] **Step 3: Discriminator audit** (a real read, not an assumption). Confirm registered/
  unregistered pairs exist for `move`, `dismount` (Task 1), `flee`, and the 2-arg `is_target_valid`
  shape; add only genuine gaps. Record any "zero added" as a verified zero.
- [ ] **Step 4: Full gates** + informational smoke verify + goldens unchanged. Commit per TU.

---

### Task 3: profs rider work — ONLY IF Task 0 fired the rider

> **If the rider DROPPED (Task 0 Step 5):** skip this entire task; Task 4 promotes two TUs
> (olog_hai + mystic) instead of three; renumber nothing else — the plan is structured so this task
> is cleanly removable.

**Files:**
- Modify: `src/utility.cpp` (`get_guardian_type` out), the `rots_combat` destination TU
  (`get_guardian_type` in — e.g. a suitable existing member per T0), `src/profs.cpp` (buf
  retirement + `add_exploit_record` conversions)
- Test: discriminator audit (`add_exploit_record` / exploit-capture dispatch already covered by the
  pilot's `ExploitCaptureHook.*`; audit, add only if a gap)

**Interfaces:**
- Consumes: `persist_hooks.h`'s `dispatch_exploit_capture` (externally linked since pilot Task 5);
  mystic's `scale_guardian` (dissolved by mystic joining in Task 4 — no edit in profs for it, the
  `extern` decl at profs.cpp:232 keeps working, intra-lib after membership).
- Produces: `get_guardian_type` body in `rots_combat` (decl `utils.h:781` unchanged; objsave.cpp:774
  calls down); profs fully converted.

- [ ] **Step 1: `get_guardian_type` relocation.** Byte-verbatim move utility.cpp:978 → the chosen
  `rots_combat` member (T0's target); `nm` single-definition; confirm `mob_index`/`guardian_mob`
  resolve via `rots_combat`'s PUBLIC `RotS::world`/`RotS::core` links; objsave.cpp:774 + profs.cpp:226
  still resolve. `CombatLayerAcyclicity` green (relocation lands consumer-free — profs not yet
  promoted, so this is app→lib downward from profs).
- [ ] **Step 2: profs `buf` retirement** (2 genuine sites, profs.cpp:428-429: `strcpy(buf,
  std::format(...).c_str()); mudlog(buf, ...)` → a local composed string). Leave the shadow locals
  (`draw_line`/`draw_coofs` params, local `char buf2[80]`) untouched. Byte-identical log output.
- [ ] **Step 3: `add_exploit_record` conversions** — 8 sites (profs.cpp:339/349/359/369/378/387/
  433/437) → the `persist_hooks.h` dispatch, positionally exact.
- [ ] **Step 4: Discriminator audit** + full gates + informational smoke + goldens unchanged.
  Commit per logical unit.

---

### Task 4: Membership + linkcheck

**Files:**
- Modify: `src/CMakeLists.txt` (`ROTS_COMBAT_SOURCES` / `ROTS_SERVER_SOURCES`). Flat Makefiles
  (`src/Makefile`, `src/tests/Makefile`) need **zero** change — pre-existing files, flat object
  lists (the pilot's checked finding); verify with `grep -rln "olog_hai\|mystic\|profs" --include=Makefile`.

**Interfaces:**
- Consumes: everything Tasks 1-3 produced.
- Produces: `rots_combat` = **10 TUs** (olog_hai + mystic) or **11** (+ profs if rider fired).

- [ ] **Step 1: Move source-list entries.** `olog_hai.cpp` + `mystic.cpp` (+ `profs.cpp` if rider)
  from `ROTS_SERVER_SOURCES` to `ROTS_COMBAT_SOURCES` in `src/CMakeLists.txt`, in ONE commit. Update
  the source-list comments (which TU joined, which wave).
- [ ] **Step 2: `CombatLayerAcyclicity` green** both hosts. A STOP here = a census miss (the
  pilot's `gain_exp`/`waiting_list` class); adjudicate with an existing seam/relocation before
  stubbing anything. `nm` single-definition for any storage the promotion pulls.
- [ ] **Step 3: Document the closure structure honestly** in the commit body: olog_hai independent
  (zero intra-trio edges); mystic independent (no reverse dep); profs one-directionally gated on
  mystic (`scale_guardian`) — one commit chosen for simplicity, NOT because a cycle forced it
  (contrast the pilot's clerics↔fight joint move).
- [ ] **Step 4: Full gates** — goldens unchanged, informational smoke, both hosts, ASan if any test
  changed. Commit.

---

### Task 5: Docs as-built + playbook row updates

**Files:**
- Modify: `docs/BUILD.md` (`rots_combat` section: 10/11 TUs, `dismount` cell, new relocations),
  `AGENTS.md` (test totals + `rots_combat` description + TU list), `docs/superpowers/
  combat-migration-playbook.md` (mark olog_hai/mystic/profs cost-table rows RESOLVED; record the
  first standalone-promotion data point and the one-directional-vs-cycle distinction), parent spec
  `docs/superpowers/specs/2026-07-16-library-architecture-design.md` (§3 row + §10 additive note),
  this wave's spec (As-built section)

- [ ] **Step 1:** As-built pass with a reconciled test-count chain (per-task deltas cited from task
  reports — the pilot's Task 5 method). Coverage-gap riders if T0 flagged untested surfaced code
  (standing wave rule). Note whether the rider fired. Focused gates. Commit.

---

### Task 6: Finalization

- [ ] **Step 1:** i386 battery — `scripts/i386-battery.sh` (sequential; markers stamp per commit;
  a fresh branch always re-runs the full battery once).
- [ ] **Step 2:** Whole-branch review (Fable), fix wave if findings (comment/doc-only fixes wait for
  the running battery; code fixes invalidate it — re-run).
- [ ] **Step 3:** Push + PR (validation steps per repo PR guidelines) + six blocking CI jobs
  (`legacy-32bit`, `linux-x64`, `sanitize-linux`, `macos-arm64`, `sanitize-macos`, `windows-msvc`)
  ; the MERGE itself is the owner's call — present the CI-green PR and stop (combat-pilot
  as-executed precedent: the owner performs the merge). Update `.superpowers/sdd/progress.md`
  throughout.
