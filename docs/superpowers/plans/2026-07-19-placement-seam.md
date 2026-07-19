# Placement Seam & Relationship-Layer Carve Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Carve the entity-pure relationship core of `handler.cpp`/`utility.cpp` into `rots_entity` (three new system TUs + existing L2 TUs) behind a three-resolver world seam, per the approved spec.

**Architecture:** Spec: `docs/superpowers/specs/2026-07-19-placement-seam-design.md`. Evidence base and authoritative per-function inventory: **`.superpowers/sdd/placement-census.md`** (planning-phase census, master @2d94fb2 — every task below names its function list; the census table is the tiebreaker on any discrepancy). Instruments: verbatim relocation; function-granular SPLIT (lib primitive + app wrapper keeping the public name); world-resolver hooks (txt-block-pool pattern: providing lib defines impls + register function, app calls it pre-boot).

**Tech Stack:** C++20, CMake presets + flat Makefiles (×4 wiring per new TU), GoogleTest, goldens, whole-archive linkchecks.

## Global Constraints

- **Zero behavior change for `ageland`.** Goldens byte-for-byte; ctest baseline **1281** both hosts (+ Task 6's additions at the end; totals recorded in Task 7 docs).
- Branch `arch/placement-seam` off master @2d94fb2. Verbatim moves; declarations stay in current headers (`handler.h`, `utils.h`); external linkage preserved; named deviations only. SPLIT functions: the app wrapper KEEPS the public name and its declaration; the lib primitive gets a new name declared in the same header.
- **Controller-adjudicated census dispositions (binding):** ADJUDICATE-1 → Disposition A (`zone_by_id` resolver); ADJUDICATE-2 → A (`detach_char_from_room` primitive + app wrapper); ADJUDICATE-3 → A (`obj_index_by_id` resolver; `extract_obj` moves; `get_obj_in_list_vnum` rides it). DEAD functions `in_affected_list`, `get_obj_num`, `get_char_num` are DELETED (0 callers, census-verified). `initialize_buffers` (empty body, called by db_boot) stays untouched this wave.
- **Named deviation from spec:** the seam is THREE world resolvers (`room_by_id`, `zone_by_id`, `obj_index_by_id`), not one — census-surfaced (spec anticipated census-demanded additions). All three: same class, one shared registration function, tripwire-abort null defaults.
- **Per-task gates:** macOS `macos-arm64` build + full ctest + native boot golden AND rots64 preset + ctest + boot golden (synchronous, `--pull never`). Census exit 0. `EntityLayerAcyclicity` (and `PlatformLayerAcyclicity` for Task 5's platform moves) are the decisive structural checks. nm single-definition per moved symbol. i386 battery at finalization only.
- **Formatter hook:** ALL .cpp/.h edits via python byte-edits run through Bash (Write tool safe only for .py/.md/CMake/shell). **`handler.cpp` has MIXED CRLF/LF line endings** (long-standing; see memory/rots-formatter-hook-conflict) — operate in binary mode; preserve every byte outside moved/edited regions; `git diff --stat` per file after each edit batch. New TUs created via python with LF, WebKit style.
- Hook count guard: if any task discovers an upward edge NOT covered by the census (a 4th resolver candidate, an output edge inside a MOVE-verdict function), STOP and report for controller adjudication — do not improvise.

---

### Task 1: Seam foundation — placement.cpp + the three world resolvers

**Files:**
- Create: `src/placement.cpp` (new ROTS_ENTITY_SOURCES member)
- Modify: `src/entity_hooks.h` (+3 resolver hook types/setters), `src/db_world.cpp` (+`rots::world::room_by_id`/`obj_index_by_id` impls + `register_world_resolver_hooks()`), `src/zone_load.cpp` (+`rots::world::zone_by_id` impl), `src/db.h` (declare `register_world_resolver_hooks`), `src/zone.h` (declare zone accessor), `src/comm.cpp` run_the_game (+1 registration call), `src/tests/gtest_main.cpp` (+1, parity), `src/handler.cpp` (loses the 3 moved functions), `src/handler.h` (unchanged — verify), build wiring ×4 for placement.cpp.

**Interfaces (Produces — later tasks rely on these exact names):**
```cpp
// entity_hooks.h (rots::entity)
using room_resolver_fn = room_data* (*)(int rnum);
using zone_resolver_fn = zone_data* (*)(int znum);
using obj_index_resolver_fn = index_data* (*)(int item_number);
void set_room_resolver_hook(room_resolver_fn);
void set_zone_resolver_hook(zone_resolver_fn);
void set_obj_index_resolver_hook(obj_index_resolver_fn);
// placement.cpp — Stage-1 API (public, declared in handler.h)
room_data* room_by_id(int rnum);            // dispatch → hook; tripwire abort if unregistered
zone_data* zone_by_id(int znum);            // same
index_data* obj_index_by_id(int item_number); // same
int location_of(const char_data* ch);       // ch->in_room
void set_location(char_data* ch, int rnum); // in_room assignment (list linkage stays in call sites this wave)
bool is_in_room(const char_data* ch, int rnum);
// occupants(room): rots::entity::occupant_range wrapping people/next_in_room walk (range-for capable)
```

- [ ] **Step 1:** Create `src/placement.cpp`: file header (provenance comment naming this plan + census), the three dispatch functions with tripwire-abort defaults (txt-block-pool comment style: no safe placeholder pointer; registered pre-boot by `register_world_resolver_hooks()`), the L1 wrappers above, `occupant_range`, then MOVE (verbatim, python byte-edit extraction from handler.cpp) `char_power` (handler.cpp:126), `recount_light_room` (:140, its `world[]` refs → `room_by_id`), `get_char_room` (:1164, same). Hook storage POD-null in placement.cpp (anonymous namespace; dispatches are the public functions above — external linkage needed by later tasks' moved code).
- [ ] **Step 2:** entity_hooks.h additions (comment per hook: call sites + tripwire rationale). rots_world impls: `db_world.cpp` `room_data* rots::world::room_by_id_impl(int rnum) { return &world[rnum]; }` + `index_data* obj_index_by_id_impl(int n) { return &obj_index[n]; }` + `register_world_resolver_hooks()` (registers all three; declared in db.h); `zone_load.cpp` `zone_data* rots::world::zone_by_id_impl(int znum) { return &zone_table[znum]; }` (declared in zone.h; ownership comment). comm.cpp run_the_game calls `register_world_resolver_hooks()` alongside the existing pre-boot_db registrations; gtest_main.cpp likewise (parity).
- [ ] **Step 3:** Wiring ×4 (CMake ROTS_ENTITY_SOURCES, src/Makefile, src/tests/Makefile, confirm ageland_tests pickup). nm single-definition for the 3 moved functions. `git diff` byte-discipline check on handler.cpp.
- [ ] **Step 4:** Full both-host gates (1281/1281, both boot goldens, census 0; EntityLayerAcyclicity + all other linkchecks green).
- [ ] **Step 5:** Commit `refactor: placement.cpp seam — Stage-1 API + three world resolvers (room/zone/obj_index)`

### Task 2: Containment & object-utils carve

**Files:** Create `src/containment.cpp`; modify `src/handler.cpp` (loses the functions), `src/object_utils.cpp` (gains its census-assigned set), build wiring ×4 for containment.cpp. Headers unchanged (verify).

**Interfaces:** Consumes Task 1's `room_by_id`/`obj_index_by_id`. Produces nothing new — all moved functions keep their public names/declarations.

- [ ] **Step 1:** MOVE verbatim into `src/containment.cpp` (mutation family, census lines 48-49, 64-68): `obj_to_char`, `obj_from_char`, `obj_to_room` (world refs → `room_by_id`), `obj_from_room` (same), `obj_to_obj`, `obj_from_obj`, `object_list_new_owner`.
- [ ] **Step 2:** MOVE verbatim into `src/object_utils.cpp` (query/lifecycle family): `get_obj_in_list`, `get_obj_in_list_num`, `get_obj_in_list_num_containers`, `count_obj_in_list`, `get_obj`, `update_object`, `update_char_objects`, `create_money`, `extract_obj` (its `obj_index[...]--` → `obj_index_by_id(...)->number--`), `get_obj_in_list_vnum` (`.virt` read → `obj_index_by_id(...)->virt`). DELETE `get_obj_num` (DEAD, 0 callers — verify with a fresh grep before deleting; STOP if a caller appeared).
- [ ] **Step 3:** Wiring ×4 for containment.cpp; nm single-definition (17 symbols); handler.cpp diff discipline check.
- [ ] **Step 4:** Full both-host gates; commit `refactor: containment.cpp + object_utils carve (obj mutation/query/lifecycle to rots_entity)`

### Task 3: Placement & equipment SPLITs

**Files:** Create `src/equipment.cpp`; modify `src/placement.cpp` (+`char_to_room` MOVE, +`detach_char_from_room` primitive), `src/handler.cpp` (SPLIT wrappers replace originals), `src/handler.h` (+3 primitive declarations — the ONE sanctioned header change; primitives are public API), build wiring ×4 for equipment.cpp.

**Interfaces (Produces):**
```cpp
// placement.cpp
void detach_char_from_room(char_data* ch);  // census ADJ-2: unlink + light-dec + zone-power-dec + clear in_room/next_in_room
// equipment.cpp
void attach_equipment(char_data* ch, obj_data* obj, int pos);  // equip primitive per census split line
obj_data* detach_equipment(char_data* ch, int pos);            // unequip primitive per census split line
```

- [ ] **Step 1:** `char_to_room` MOVEs verbatim to placement.cpp (`world[]` → `room_by_id`, `zone_table[...]` → `zone_by_id`).
- [ ] **Step 2:** SPLIT `char_from_room` per census line 46: primitive `detach_char_from_room` (everything through clearing `in_room`/`next_in_room`, zone-power-dec via `zone_by_id`) into placement.cpp; app-side `char_from_room` wrapper in handler.cpp = call primitive, then the trailing `stop_fighting` loop (handler.cpp:703-712 byte-preserved). 37 callers unchanged.
- [ ] **Step 3:** SPLIT `equip_char`/`unequip_char` per census lines 50-51 exactly: primitives (slot/encumb/weight/OB-PB-dodge/affect_modify+affect_total/light via `room_by_id`) into `src/equipment.cpp`; app wrappers keep the zap-messages + re-drop (equip :833-853), too-heavy message (:880-883), poison damage/raw_kill blocks (equip :905-916, unequip :980-991) byte-preserved. The `mudlog` zero-object guard stays in the unequip primitive (L0-legal).
- [ ] **Step 4:** Byte-fidelity audit: reassemble each SPLIT function's wrapper+primitive text and diff against the pre-task original — every original line must appear exactly once (in wrapper or primitive), no logic reordered beyond the call boundary. Quote the audit method + result in the report.
- [ ] **Step 5:** Wiring ×4 for equipment.cpp; nm; full both-host gates (boot goldens decisive — equip/placement run at boot via zone reset). Commit `refactor: placement/equipment splits — lib primitives + app wrappers (char_from_room, equip_char, unequip_char); char_to_room joins placement`

### Task 4: handler.cpp remainder batch (MOVE-OTHER + platform + dead)

**Files:** Modify `src/handler.cpp` (loses 26 functions), `src/entity_lifecycle.cpp` (+13 per census: isname, affect_modify_room, affect_total_room, affect_to_room, affect_remove_room, affect_from_char, room_affected_by_spell, affect_join, get_from_follow_type_pool + `follow_type_pool`/counter globals, put_to_follow_type_pool, get_char, register_pc_char), `src/char_utils.cpp` (+3: circle_follow, keyword_matches_char, can_swim), `src/rots_util.cpp` or platform home (+4: get_number, parse_numbered_name, find_all_dots, money_message). DELETE `in_affected_list`, `get_char_num` (DEAD — fresh-grep verify each). Headers unchanged (verify).

- [ ] **Step 1:** Batch-move per the census Target-TU column (verbatim; python byte-edits; group commits are fine within the one task commit). Fresh-grep each DEAD function before deletion; STOP if callers appeared.
- [ ] **Step 2:** nm single-definition sweep (26 symbols); PlatformLayerAcyclicity + EntityLayerAcyclicity green locally.
- [ ] **Step 3:** Full both-host gates; commit `refactor: handler.cpp remainder carve — affect/follow/lookup family to rots_entity, text parsers to platform, dead code deleted`

### Task 5: utility.cpp carve (L2 + platform batches)

**Files:** Modify `src/utility.cpp` (loses 34 functions), `src/char_utils_combat.cpp` (+11: do_squareroot, get_bow_weapon_damage, get_weapon_damage, weight_coof, armor_absorb, get_real_stealth [world refs → room_by_id], get_real_dodge, can_sense, get_power_of_arda, check_resistances + see census), `src/char_utils.cpp` (+7: get_followers_level, mudlog_debug_mob, mudlog_aliased_mob, age, has_critical_stat_damage, has_alias, has_program), `src/environment_utils.cpp` (+3: get_exit_width + `default_exit_width[]` table, CAN_GO [EXIT macro → room_by_id], can_breathe), platform home (+14: string_to_new_value, number(double), number_d, rots_asprintf, strn_cmp, strn_cmp_nullable, sprintbit, sprinttype, real_time_passed, mud_time_passed, nth, day_to_str, strcpy_lang, reshuffle). Headers unchanged (verify).

- [ ] **Step 1:** Batch-move per census (utility.cpp table). NOTE: `get_real_OB`/`get_real_parry` STAY-APP (weapon_master spec-handler edge) while `get_real_dodge` moves — the trio splits across tiers; add a one-line comment at each STAY function noting why (AGENTS.md documents this trio's history — do not confuse with the deleted `utils::` trio).
- [ ] **Step 2:** nm sweep; linkchecks; full both-host gates.
- [ ] **Step 3:** Commit `refactor: utility.cpp carve — combat-stat/char/environment helpers to rots_entity, pure text/time/rng to platform`

### Task 6: Targeted coverage (standing coverage-gap rule)

**Files:** Test files under `src/tests/` (new or extended) + test-build wiring; NO production changes.

- [ ] **Step 1:** Candidates (feasibility-gated; honest fallbacks; no mock-heavy tests): (a) `detach_char_from_room`/`char_to_room` semantics through registered resolvers — occupant-list linkage, light count, zone white/dark power bookkeeping (test zone/room fixtures + stub or real resolvers; restore hygiene per the RAII precedent in entity_lifecycle_tests.cpp); (b) `attach_equipment`/`detach_equipment` primitives — slot assignment, encumbrance/weight deltas, affect_modify/affect_total invocation, light inc/dec; (c) resolver tripwire NOT tested (abort path — no death tests, per standing rule).
- [ ] **Step 2:** Red-proof each test (transient production break → observe failure → revert, tree-clean proof). Both-host gates + **macOS ASan preset** (new test files). Record exact new ctest totals per host.
- [ ] **Step 3:** Commit `test: cover placement/equipment primitives through the world-resolver seam (placement-seam T6)`

### Task 7: Docs

- [ ] **Step 1:** BUILD.md "Library layering": rots_entity grows to 8 TUs (`placement.cpp`/`containment.cpp`/`equipment.cpp` join the five); the three-resolver seam + registration; honest deferrals (game-wide call-site campaign, Stage 2 LocationSystem, STAY-APP inventory incl. visibility/proto/zone-power families, `get_real_OB`/`get_real_parry`). Parent spec §7: as-built note (Stage 1 scoped delivery). AGENTS.md: rots_entity one-liner update + new ctest totals from Task 6; note the OB/parry/dodge tier split in the Dead/Unused-Code section's trio paragraph.
- [ ] **Step 2:** Census 0; docs-only gates; commit `docs: placement-seam as-built (rots_entity 8 TUs, three-resolver seam, Stage-1 scope)`

### Task 8: Finalization (controller-owned — not for an implementer subagent)

- [ ] **Step 1:** Full i386 battery (`scripts/i386-battery.sh`; per-commit markers).
- [ ] **Step 2:** Whole-branch review (most capable model) with the accumulated Minor roll-up; fix wave if needed.
- [ ] **Step 3:** Push; PR against master (describe: the carve inventory, the three-resolver seam + named deviation, SPLIT wrappers, dead-code deletions, deferred follow-ons). Merge = owner's call.

---

## Self-Review Notes

- **Spec coverage:** three system TUs (T1/T2/T3), Stage-1 API + id-currency rule (T1), tier-line split contract incl. function-granular re-splits (T3), census-driven inventory with STOPs (all tasks), resolver seam + tripwire + parity (T1), wiring/linkcheck enforcement (every task), coverage rule (T6), docs (T7), deferrals stated (T7/T8 PR).
- **Placeholder scan:** every task names its exact function list from the census (the census file is the authoritative tiebreaker and every brief must point to it); API/primitive signatures given; commit messages exact.
- **Type consistency:** `room_by_id`/`zone_by_id`/`obj_index_by_id`, `detach_char_from_room`, `attach_equipment`/`detach_equipment`, `register_world_resolver_hooks` used consistently across T1/T2/T3/T6.
- **Risk register:** (1) SPLIT byte-fidelity — T3 Step 4's reassembly audit is the check; (2) handler.cpp mixed CRLF — binary-mode discipline in every task touching it; (3) hidden upward edges — census was function-body-based; the linkchecks + STOP contract catch drift; (4) hot-path resolver cost — whole-branch review sanity-check (spec risk 2); (5) 26+34-function batches are large — nm sweeps + goldens pin them; batches split T4/T5 to keep review scope per-file-of-origin.
