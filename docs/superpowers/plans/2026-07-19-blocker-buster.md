# Blocker-Buster (Combat-Growth Enablers) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the four combat-growth enablers — output-seam extension, command-dispatch seam, poison-notification hook (+its two consumer moves), visibility family → rots_combat — consumer-free except where the spec sanctions.

**Architecture:** Spec: `docs/superpowers/specs/2026-07-19-blocker-buster-design.md`. Evidence: **`.superpowers/sdd/blocker-census.md`** (authoritative per-question/per-function inventory; the tiebreaker on discrepancies) + BUILD.md's DEFER-11 inventory. Instruments: output_seam forwarding pattern; assign_spell_pointers registration precedent; entity_hooks.h hook taxonomy; verbatim relocation with the resolver-variant rule.

**Tech Stack:** C++20, CMake presets + flat Makefiles, GoogleTest, goldens, whole-archive linkchecks.

## Global Constraints

- **Zero behavior change for `ageland`.** Goldens byte-for-byte; ctest baseline **1343** both hosts, rising only by this wave's seam tests (totals recorded per task; Task 5 docs record finals).
- Branch `arch/blocker-buster` off master @e813138. Verbatim moves; declarations stay in current headers; named deviations only; STOP on uncensused edges or census conflicts.
- **Resolver-variant rule** (placement-seam convention) for any moved body's `world[]` access: unchecked original → `room_by_id_total`; bounds-checked → `room_by_id` + nullptr check; quote per-site original evidence. `weather_info`/other L3-world globals from L3-combat code are LEGAL PEER REFERENCES (extern, no seam) — cite the parent spec's peer-tier rule where used.
- **Per-task gates:** macOS `macos-arm64` build + full ctest + native boot golden AND rots64 preset + ctest + boot golden. **Docker: SYNCHRONOUS FOREGROUND ONLY, `--pull never`, timeout ≤600000 ms. Backgrounding a container gate WILL stall you unrecoverably (three occurrences across prior waves; a fourth is unacceptable) — if a gate feels long, it is still faster than a controller rescue.** Census exit 0; nm single-definition per move; all 6 linkchecks green; ASan preset on any new test file. i386 battery at finalization only (controller).
- **Formatter hook:** ALL .cpp/.h edits via binary-mode python byte-edits run through Bash (Write tool safe for .py/.md/CMake/shell only). Verify each file's line-ending convention FIRST (handler.cpp and utility.cpp are MIXED CRLF) and preserve byte-exactly outside edited regions. New files LF, WebKit, pre-formatted. `git diff --stat` + spot-check per file.
- Registration parity: every new registration lands in BOTH `run_the_game()` (pre-boot_db) and `src/tests/gtest_main.cpp`.

---

### Task 1: Output-seam extension (+7 forwarders)

**Files:** Modify `src/output_seam.h` / `src/output_seam.cpp` (the +7: `send_to_room` and its census-listed variants, `send_to_all`, `break_spell`, `abort_delay`, `complete_delay`, txt-pool entries — the census section D table is the exact list with signatures/definition sites), `src/comm.cpp` (register the real sinks where the current five register), `src/tests/gtest_main.cpp` (parity). Seam tests per the census's mechanism notes (new or existing test file per convention).

**Interfaces (Produces):** the extended forwarder set, same naming/dispatch style as the existing five — later waves' combat TUs call these exactly like `send_to_char` today.

- [ ] **Step 1:** Read output_seam.{h,cpp} fully; replicate its pattern for each census-D forwarder (null-safe defaults per its established taxonomy — match what the existing five do, not the tripwire hooks). Python byte-edits.
- [ ] **Step 2:** comm.cpp + gtest_main registrations (parity).
- [ ] **Step 3:** Seam tests: each new forwarder proven to reach the real comm.cpp sink when registered (discriminator-style, per the combat-seed dispatch-test precedent) and to no-op/default safely when not. Red-proof each. ASan preset run.
- [ ] **Step 4:** Full both-host gates (totals = 1343 + this task's tests; goldens; census; 6 linkchecks).
- [ ] **Step 5:** Commit `feat: output_seam gains the seven combat-growth forwarders`

### Task 2: Command-dispatch seam (combat_hooks.h)

**Files:** Create `src/combat_hooks.h` (+ storage/dispatch TU decision per the census-C sketch — follow the assign_spell_pointers precedent: null-initialized table, populated at boot); modify `src/db_boot.cpp` (registration call in the existing assign_* sequence) or the census-recommended registration site, `src/tests/gtest_main.cpp` (parity). Seam tests.

**Interfaces (Produces):** the dispatch entry (e.g. `rots::combat::issue_command(...)` or enum-indexed table per census-C — the census's signature-family analysis governs; the ~19 ACMD targets enumerated there are the cells). NO call-site conversion this wave.

- [ ] **Step 1:** Implement per census-C (quote the chosen mechanism + precedent mapping in the report; tripwire-null cells).
- [ ] **Step 2:** Registration + parity; boot-window analysis in the report (registration precedes any possible dispatch — mirror prior waves' argument).
- [ ] **Step 3:** Seam tests: table-resolves-to-real-command discriminators for a representative sample of cells (do_hit, do_flee + 2 more; red-proofed; ASan).
- [ ] **Step 4:** Full both-host gates; commit `feat: combat command-dispatch seam (boot-registered table, assign_spell_pointers precedent)`

### Task 3: Poison-notification hook + its two consumer moves

**Files:** Modify `src/entity_hooks.h` (+notification hook per census-E's shape analysis), `src/handler.cpp` (wrapper fires the hook; loses obj_from_char + extract_obj), `src/containment.cpp` (+obj_from_char per its placement-census row), `src/object_utils.cpp` (+extract_obj per its row, obj_index_by_id substitution per the raw-array convention), `src/comm.cpp` + `gtest_main.cpp` (registration parity). Characterization + seam tests.

- [ ] **Step 1 (FIRST — before any inversion):** Characterization test capturing the CURRENT mudscript poison path behavior (SCRIPT_OBJ_FROM_CHAR on an equipped poisoned-removal-vulnerable item → the wrapper's damage side effect fires). Red-proof it against current code. This is the wave's behavior-sensitive anchor.
- [ ] **Step 2:** The hook per census-E (signature from its analysis; tripwire default per taxonomy — analyze whether unregistered-fire is reachable in rots_convert/tests and pick tripwire vs silent accordingly, documenting the class rationale per the established contrast convention).
- [ ] **Step 3:** Invert: the L2 callers' path routes through primitive + dispatch; the characterization test must pass UNCHANGED through the new path (the proof of identical behavior).
- [ ] **Step 4:** The two moves (verbatim per placement-census rows; deferral comments retired with breadcrumbs; nm ×2).
- [ ] **Step 5:** Full both-host gates (EntityLayerAcyclicity decisive); ASan; commit `refactor: poison-notification hook; obj_from_char/extract_obj complete their deferred moves (cluster 3 retired)`

### Task 4: Visibility family → rots_combat

**Files:** Create `src/visibility.cpp` (ROTS_COMBAT_SOURCES member; wiring ×4) unless census-A's per-function verdicts argue for split placement (follow the census; report the mapping). Modify `src/handler.cpp` + `src/utility.cpp` (lose the family: CAN_SEE ×2, CAN_SEE_OBJ, get_char_room_vis, get_player_vis, get_char_vis, get_obj_in_list_vis, get_obj_vis, get_object_in_equip_vis, generic_find, PERS, get_real_OB, get_real_parry — census-A is the authoritative list with per-function upward refs). Headers unchanged (verify per function).

- [ ] **Step 1:** Batch-move per census-A (verbatim; resolver-variant rule with per-site evidence; weather_info/peer refs cited; the trio comments at get_real_OB/get_real_parry retired — the live trio reunites in combat tier).
- [ ] **Step 2:** nm sweep (12+ symbols); CombatLayerAcyclicity + all linkchecks; handler.cpp/utility.cpp byte-discipline (mixed CRLF).
- [ ] **Step 3:** Full both-host gates; commit `refactor: visibility family joins rots_combat (census-verified L3 home; OB/parry/dodge trio reunited)`

### Task 5: Docs

- [ ] **Step 1:** BUILD.md (the four seams as-built; DEFER-11 inventory updated — blockers now CLEARED, listing what each future TU still needs; deferral-ledger: ALL THREE placement-seam clusters retired); AGENTS.md (totals + skips from fresh gates; trio paragraph final state); spec as-built note; parent spec §3 touch if warranted.
- [ ] **Step 2:** Census 0; docs gates; commit `docs: blocker-buster as-built (four seams, cluster 3 retired, growth inventory cleared)`

### Task 6: Finalization (controller-owned)

- [ ] Full i386 battery → whole-branch review (most capable model) → fix wave if needed → push → PR → CI → merge per owner authorization protocol.

---

## Self-Review Notes

- **Spec coverage:** all four deliverables (T1-T4), the characterization-first discipline for the behavior-sensitive item (T3 Step 1), docs (T5), finalization (T6). Consumer-free honored except the sanctioned T3 pair.
- **Placeholder scan:** exact inventories delegate to the census's lettered sections (A/C/D/E) by design — the census file carries the per-function/per-forwarder tables; every task names its census section + the report contract.
- **Type consistency:** combat_hooks.h, visibility.cpp, the forwarder names as census-D lists them.
- **Risk register:** (1) T3 is the behavior-sensitive core — characterization-first + red-proof is the control; (2) T4 is the hot-path batch — verbatim moves, no new dispatch, reviewer verifies; (3) census-A per-function surprises → STOP; (4) ordering: T1/T2 independent; T3 before T4 keeps handler.cpp edits serialized (both touch it).
