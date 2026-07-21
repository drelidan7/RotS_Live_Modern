# Spell-Family Closure Wave Implementation Plan (`spell_pa.cpp` + `mage.cpp` + `ranger.cpp` → `rots_combat`)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Promote the three remaining spell-family TUs into `rots_combat` as ONE closed set
(12 → 15 TUs, combat DEFER 5 → 2) — the intra-subset rule at full scale; no new library, no new
linkcheck; `CombatLayerAcyclicity` is the membership gate.

**Architecture:** Census + closure first (Task 0, read-only — the largest census surface yet: the
76-peer `spell_pa` hub plus two 2.5-3.7k-line TUs), then seams/relocations consumer-free (Task 1),
then per-TU conversions (Task 2 `spell_pa`, Task 3 `mage`+`ranger`), then the JOINT membership
commit (Task 4 — all three TUs enter `ROTS_COMBAT_SOURCES` in one commit; a standalone promotion
of any one would leave `CombatLayerAcyclicity` unable to resolve intra-family edges), then docs +
carried sweep (Task 5a) and finalization (Task 5b). The marquee edge: ranger.cpp:271 calls
`show_tracks` (graph.cpp:319, `rots_pathfind` L4) — relocate-or-hook; if hooked it is the
codebase's THIRD permanent L3→L4 inversion. Spec:
`docs/superpowers/specs/2026-07-21-spell-family-closure-design.md`. Recipe:
`docs/superpowers/combat-migration-playbook.md` (cost table + intra-subset rule + census-method
correction FIRST). Branch: `arch/spell-family` off master @`92ba890`.

**Tech Stack:** C++20, CMake presets + flat Makefiles, GoogleTest, `nm` censuses, Python 3
byte-edits, the existing seam headers (`combat_hooks.h` 29 cells, `script_hooks.h`,
`entity_hooks.h`, `world_hooks.h`, `output_seam.h`, `editor_hooks.h`) — cells/forwarders/hooks may
be added; **no new seam header expected** (a genuinely new taxonomy = auto-STOP).

## Global Constraints

- **Flat-Makefile parity is BINDING and same-commit (owner-directed):** every source-list change
  to `src/CMakeLists.txt` lands its flat counterpart in the SAME commit — new production TU →
  `src/Makefile` AND `src/tests/Makefile` object lists; new test file → `src/tests/Makefile`
  `SRCS`; new linkcheck → root `Makefile` `test`-target list (not expected — no new library).
  Implementers verify the pairing by grep; reviewers treat a CMake-only source addition as an
  Important finding. (Membership MOVES between CMake lists need no flat change — the flat builds
  are monolithic — but any implementer claiming that verifies it by grep, not assumption.)
- Every moved function body is **byte-verbatim** (reviewers re-diff moved spans). Check each
  file's CRLF profile BEFORE editing (no map entries exist for `spell_pa.cpp`/`mage.cpp`/
  `ranger.cpp` — measure, don't assume); Python byte-edits for ALL existing `.cpp`/`.h` (formatter
  hook).
- Call-site conversions and relocations are the ONLY semantic edits; each covered by a
  discriminator or byte-verbatim re-diff. No spell/combat behavior change; goldens unchanged.
- **Layer order `platform < core < entity < persist < world < combat < pathfind < script < olc <
  app`; every link unidirectional; NO bidirectional peer links.** `rots_combat` PUBLIC-links
  world/persist/entity/core/platform — a relocation INTO `rots_combat` may reach only those + its
  own lib; `rots_world`/`rots_entity` may never gain a combat edge. An L3→L4 upward edge (e.g.
  `show_tracks` if hooked) must invert via a hook the L4 owner registers — never a link.
- **Intra-subset closure rule:** all three TUs promote in ONE membership commit; every upward
  edge of each must be (a) owned by a TU in the same commit, (b) seam/relocation-resolved, or
  (c) already downward. Census "non-blocking" is a prior, not a guarantee; `CombatLayerAcyclicity`
  is ground truth.
- **Rider gate: 1 slot remains** (2 of ≤3 same-shape spec-proc-dispatcher cells used). A new
  same-shape edge may take the last slot with its own discriminator pair; a second new one, or
  ANY different-shape edge, is an auto-STOP. A genuinely new seam taxonomy is an auto-STOP.
- Per-task gates (both hosts): macOS arm64 build + ctest, `rots64` build + ctest, both boot
  goldens, all NINE `*LayerAcyclicity` linkchecks, `ConvertEquivalence` 17/17,
  `python3 tools/string_view_census.py --check` exit 0. ASan preset on any new/substantially-
  rewritten test file (additive-only is not an exemption). i386 battery at finalization only.
- Characterization goldens + boot goldens unchanged — drift = bug. Spell paths are
  boot-golden-light: the coverage-gap rule is EXPECTED to fire; untested relocated/converted
  bodies get targeted ctest riders. Combat smoke harness stays capture-only/informational.
- No death tests; no direct `rand()`/`random()` (rots_rng only); `std::format` composition;
  `-Wall -Wextra -Werror`; `-funsigned-char`; `char[N]` members decay via
  `static_cast<const char*>` before `std::format`.
- STOP-and-adjudicate: any edge/cascade/census-miss a brief did not disposition → STOP with
  evidence. Docker gates SYNCHRONOUS in subagents. Test baseline **1468**; deltas per task,
  reconciled in T5a.
- Implementers Sonnet; T0 census + heavy reviews Opus; whole-branch review Fable. Briefs/reports/
  census in `.superpowers/sdd/` (never committed; `sf-` prefix). **Owner grants for this program
  (explicit, 2026-07-21): merge-when-green THIS wave + Wave B, autonomous-spec-gate for Wave B.**

---

### Task 0: Verification census + closure check + adjudications (read-only)

**Files:**
- Create: `.superpowers/sdd/sf-census.md` (gitignored scratch).

**Interfaces:**
- Produces the per-edge disposition table Tasks 1-4 cite: the full three-TU `nm` closure, the
  76-peer re-derivation, the `show_tracks` relocate-or-hook ruling, every helper-family
  disposition, and the joint-membership verdict.

- [ ] **Step 1: Build objects** — `cd src && cmake --preset macos-arm64 && cmake --build --preset
  macos-arm64 -j4`. Confirm base: `rots_combat` 12 TUs, `combat_command` 29 cells, nine
  libraries, master @`92ba890` + the spec/plan commits.
- [ ] **Step 2: nm census, three TUs** — `nm -u` on `spell_pa.cpp.o`/`mage.cpp.o`/`ranger.cpp.o`;
  demangle; resolve against the NINE library object dirs + `ageland.dir` (bw-census §0 method;
  resolve library-first — the cluster-b census's stale-orphan-object lesson). Tally per TU;
  reconcile against the playbook rows (spell_pa combat-peer=76 / mage=7 / ranger=9 and their
  app-tier splits), explicitly listing which old-row items are ALREADY RESOLVED (the spec's
  prior-wave list) with evidence.
- [ ] **Step 3: Intra-family closure map.** Enumerate every `spell_pa ↔ mage ↔ ranger` edge (all
  six directions) plus each TU's edges into already-in-lib family members — these dissolve
  intra-lib at T4 and need NO seams; the census records them as the joint-commit justification.
  Any cycle among the three is fine (same commit); any edge to a STILL-APP TU outside the three
  is a blocking disposition item.
- [ ] **Step 4: Adjudicate the marquee + helper families**, with body-read evidence:
  - **`show_tracks`** (graph.cpp:319, called ranger.cpp:271 with `(ch, argument, 1)`; extern decl
    ranger.cpp:57): body-read its reach. Default A (preferred): RELOCATE down if its body is
    presentation over data reachable from L3 (check what graph-internal state it touches).
    Default B: `combat_hooks.h` hook, `graph.cpp` registers pre-boot (lib-registrar — the
    `call_trigger` shape), loud-tripwire int default — the THIRD permanent L3→L4 inversion,
    recorded as such. Whichever fires, apply the hook-owner-banner lesson (no "app-tier
    permanently" comments anywhere near it).
  - **`target_from_word`** (spell_pa.cpp:601, decl :43) / **`report_wrong_target`** (:602, decl
    :45) — interpre.cpp bodies; RELOCATE if parse-pure over L2-reachable state, else hook.
  - **`descriptor_list` walk** (spell_pa.cpp:113, extern :34) — enumerate what the loop does;
    default: an output_seam/world_hooks accessor or purpose-built query hook per the
    `is_zone_populated` shape; storage NEVER moves (comm-owned).
  - **Mage's helpers**: `do_look` (mage.cpp:834 — full ACMD shape `(victim, mutable_arg(""), 0,
    0, 0)` → the EXISTING `look` cell, no new seam); `do_identify_object` (:857, 2-arg helper —
    body-read, RELOCATE-or-hook); `list_char_to_char` (:631 — act_info display helper, body-read);
    `prohibit_item_stay_zone_move` (:811/:960, act_move — body-read); `msdp_room_update` (:835-837
    local extern + call — protocol/session, default output_seam-class forwarder; delete the
    mid-function extern decl on conversion).
  - **Ranger's door/move parse helpers** (~9, per its app-command=9 row) — enumerate by grep +
    `nm`, body-read each: RELOCATE (parse-pure) vs hook (door/session state).
  - **db_boot globals** (ranger app-session row, ~4 after `_buf`) — enumerate; accessors or
    storage-moves per `no_specials` precedent.
  - **Color-sequence** (`_color_sequence`/`get_color_sequence`, spell_pa app-session row) —
    body-read; accessor vs storage-move.
  - **`_buf`/`buf` usage in all three TUs** — enumerate genuine global references (header-extern
    route included, the cluster-b `_buf` lesson: a plain grep can miss or over-match) → local
    composition.
  - **ACMD sweep**: every `do_*` call in the three TUs vs the 29-cell table; new cells only on
    confirmed real shapes; note ranger/spell_pa/mage are THEMSELVES ACMD owners heavily called
    FROM the table's dispatchers (inbound edges are legal app→lib, zero seams).
- [ ] **Step 5: Closure verdict + membership plan.** The joint commit's exact three-line CMake
  move; confirmation NO flat-Makefile change is needed for the moves (grep both flat Makefiles —
  all three TUs already in the monolithic object lists) but any NEW test file T1-T3 add is
  same-commit flat-paired (BINDING rule); the seam list T1 must land; per-TU conversion lists for
  T2/T3; expected coverage-gap riders (spell paths boot-golden-light).

---

### Task 1: Seams + relocations (consumer-free, all three TUs still app-compiled)

**Files:**
- Modify (per T0's rulings): `src/combat_hooks.{h,cpp}` (possible `show_tracks` hook + any new
  cell), `src/output_seam.{h,cpp}` / `src/world_hooks.{h,cpp}` (accessors/forwarders:
  `msdp_room_update`-class, `descriptor_list` query, db_boot/color accessors), `src/graph.cpp`
  (register `show_tracks` if hooked / lose it if relocated), `src/interpre.cpp` +
  destination TU (target_from_word/report_wrong_target relocation or registration),
  `src/act_info.cpp`/`src/act_move.cpp` + destinations (mage/ranger helper relocations or
  registrations), `src/comm.cpp`/`src/protocol.cpp`/`src/db_boot.cpp`/`src/color.cpp` (real-body
  registrations/backings per taxonomy owner).
- Test: extend the matching `src/tests/*_tests.cpp` seam files (discriminator pair per new
  cell/hook/accessor); coverage riders for relocated bodies per the coverage-gap rule. **Every
  new test file → `src/tests/Makefile` `SRCS` in the SAME commit (BINDING).**

**Interfaces:**
- Consumes: `sf-census.md` dispositions.
- Produces: every seam/relocation T2/T3 convert onto — named exactly as the census rules them —
  landed consumer-free with registration parity (`run_the_game` AND `gtest_main`).

- [ ] **Step 1:** TDD each new hook/cell/accessor: failing discriminator pair first
  (registered-reaches-stub-with-args-intact / unregistered-default per house pattern; no death
  tests), then implementation + boot registration both sides. One commit per logical unit.
- [ ] **Step 2:** Byte-verbatim relocations per T0 (Python byte-edits; `nm` single-definition
  check per move; destination tier legality re-checked against the census's floor ruling).
  Coverage riders where a moved body was untested live code.
- [ ] **Step 3:** Full gates both hosts + ASan (test files changed) + goldens unchanged +
  flat-parity grep evidence in the report.

---

### Task 2: `spell_pa.cpp` conversions (still app-compiled)

**Files:**
- Modify: `src/spell_pa.cpp`
- Test: discriminator audit (Step 3)

**Interfaces:**
- Consumes: T1 seams; the 29-cell table; existing entity/world/persist hooks.
- Produces: `spell_pa.cpp` fully converted — every remaining undefined symbol resolves downward
  or into the T4 joint set (mage/ranger edges stay DIRECT — they dissolve intra-lib at T4; the
  census's intra-family map is the authority on which calls to leave alone).

- [ ] **Step 1:** Convert per the census list (expected: `target_from_word`/`report_wrong_target`
  per their ruling; the `descriptor_list` walk → its accessor/query; color-sequence per ruling;
  `_buf` retirement; delete converted-away extern decls incl. spell_pa.cpp:34/:43/:45 as they
  become unreferenced). Leave every intra-family (mage/ranger/in-lib) call DIRECT.
- [ ] **Step 2:** Verify the already-resolved set needs zero edits (`waiting_list`,
  `is_target_valid`, `abort_delay`/`complete_delay`, `saves_*`, `record_spell_damage`,
  `check_break_prep`) — recorded reads, not assumptions.
- [ ] **Step 3:** Discriminator audit: pairs exist for everything this TU now dispatches through;
  close genuine gaps only (l4-seed `say`-cell precedent); record verified zeros.
- [ ] **Step 4:** Full gates + ASan (if tests changed) + goldens. Commit.

---

### Task 3: `mage.cpp` + `ranger.cpp` conversions (still app-compiled)

**Files:**
- Modify: `src/mage.cpp`, `src/ranger.cpp`
- Test: discriminator audit (Step 4)

**Interfaces:**
- Consumes: T1 seams; the `look` cell (mage.cpp:834's existing-cell reuse); the behavior-wave
  `char_from_room` hook (mage.cpp:831); `report_zone_power` in `rots_world` (mage.cpp:555 —
  verify legal downward, likely zero edit); L2 `stop_riding` (mage.cpp:830/:956 — zero edit).
- Produces: both TUs fully converted; intra-family calls left DIRECT for T4.

- [ ] **Step 1: mage.cpp** — `do_look` :834 → `rots::combat::issue_command(combat_command::look,
  victim, mutable_arg(""), 0, 0, 0)`; `char_from_room` :831 → the entity_hooks dispatch;
  `msdp_room_update` :837 → its T1 seam (delete the :835 mid-function extern);
  `do_identify_object` :857, `list_char_to_char` :631, `prohibit_item_stay_zone_move` :811/:960
  per their T1 rulings (relocation = call unchanged, hook = dispatch conversion); `break_spell`
  :728 → verify the existing output_seam forwarder route needs zero edit (it is a takeover —
  recorded read); `_buf` per census. Delete converted-away decls (:134/:145/:147/:148 as they
  become unreferenced).
- [ ] **Step 2: ranger.cpp** — `show_tracks` :271 per the T0 ruling (relocated → call unchanged +
  delete extern :57; hooked → `rots::combat::dispatch_show_tracks(ch, argument, 1)` + delete
  extern); the ~9 door/move helpers per rulings; db_boot accessors; `_buf`; delete dead decls.
  The already-L2/in-lib set (`add_follower`, `stop_*`, `obj_from_char`, visibility family,
  big_brother hooks, delay seams) needs zero edits — recorded reads.
- [ ] **Step 3:** Verify NO edit to any intra-family call in either TU (census map cited).
- [ ] **Step 4:** Discriminator audit both TUs; genuine gaps only; verified zeros recorded.
- [ ] **Step 5:** Full gates + ASan (if tests changed) + goldens. Commit per TU.

---

### Task 4: Joint membership (ONE commit, all three TUs)

**Files:**
- Modify: `src/CMakeLists.txt` — move `spell_pa.cpp`, `mage.cpp`, `ranger.cpp` from
  `ROTS_SERVER_SOURCES` → `ROTS_COMBAT_SOURCES` in one commit; house-style comments (which TUs,
  which wave, the intra-subset joint-commit rationale, the `show_tracks` resolution). NO new
  library/linkcheck/link-set change expected (census confirms `rots_combat`'s PUBLIC set
  suffices).
- Verify: flat Makefiles need ZERO change for the moves (all three TUs already in both monolithic
  object lists — grep evidence in the report, per the BINDING parity rule's verification clause);
  root Makefile untouched.

**Interfaces:**
- Consumes: Tasks 1-3 complete.
- Produces: `rots_combat` at 15 TUs; combat DEFER = 2; all nine linkchecks green.

- [ ] **Step 1:** The joint move + comments; build both hosts; `CombatLayerAcyclicity` green. A
  failure = census miss → bisect by staging per-TU membership LOCALLY (never committed) to
  attribute the edge, then adjudicate seam/relocation per Global Constraints (STOP if
  undispositioned).
- [ ] **Step 2:** All nine linkchecks + full ctest + both boot goldens + `ConvertEquivalence`
  17/17 + svc-check. Commit (one commit).

---

### Task 5a: Docs + carried sweep

**Files:**
- Modify: `docs/BUILD.md` (rots_combat 15 TUs; the `show_tracks` resolution — third-inversion
  note if hooked), `docs/superpowers/combat-migration-playbook.md` (spell_pa/mage/ranger rows
  RESOLVED with actual counts; "spell-family closure" as-built section; DEFER list → 2),
  parent spec `2026-07-16-...-design.md` (as-built slice), this wave's spec (As-built section),
  `AGENTS.md` (test chain + DEFER 2 + rots_combat 15; i386 pending T5b noted honestly),
  progress.md is controller-owned (do not edit).
- Carried sweep (src comments, object-code-neutral, verified): fight.cpp:3272-74/:3358-60 +
  combat_hooks.h:330-336 stale `call_trigger` "app-tier permanently" banners → reworded to the
  second-inversion reality; editor_hooks.{h,cpp} stale "starts in ROTS_SERVER_SOURCES" banner;
  the uncommented test-recording-globals family (comment per repo convention).

- [ ] **Step 1:** Docs pass with reconciled per-task test chain (baseline 1468); sweep fixes with
  `nm`-neutrality evidence where claimed; full gates once (sweep touches src). Commits: docs
  separate from sweep.

---

### Task 5b: Finalization

- [ ] **Step 1:** i386 battery (`scripts/i386-battery.sh`, sequential, fresh-branch full run).
  Expect: ctest N/N (N = 1468 + wave delta), monolithic reconciliation exact (ctest-only
  linkcheck delta stays 9; flat parity BINDING rule means zero missing-test surprises), boot
  golden matches.
- [ ] **Step 2:** Whole-branch review (Fable). Doc-only fixes may land during the battery; ANY
  src fix invalidates it → re-run.
- [ ] **Step 3:** Push + PR + six blocking CI jobs. **MERGE WHEN GREEN (owner grant, this
  program).** ff-merge, delete branch.
- [ ] **Step 4:** Bookkeeping: ledger WAVE close (merge SHA, final chain, DEFER 2), memory
  update, branch prune. **Then IMMEDIATELY begin Wave B (`spec_pro` → `spec_ass`) under the
  autonomous-spec-gate grant**: new branch `arch/spec-pair` off the merged master; Wave B's spec
  written against this wave's as-built (charter: this wave's spec §Wave B); same task shape
  (census → seams → conversions → memberships → docs → finalization → pre-authorized merge);
  the pre-authorized registrar-hook family exactly 3 edges (gen_board/postmaster/receptionist);
  auto-STOPs unchanged. DEFER 0 ends the program.
