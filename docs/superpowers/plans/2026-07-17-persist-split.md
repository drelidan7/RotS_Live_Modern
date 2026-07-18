# Persist Split + rots_persist Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Spec §10 step 4, slice 2 (docs/superpowers/specs/2026-07-16-library-architecture-design.md): carve the pure persistence codecs out of objsave/boards/mail/pkill and color.cpp, invert the last db_players→world edge, and stand up the **rots_persist** (L3) STATIC library with a whole-archive acyclicity linkcheck — with `rots_convert` re-plumbed onto it and the weld ledger shrinking again (~15 → ~6 entries).

**Architecture:** Same three instruments as the entity-seed wave, now applied at L3: (1) *verbatim single-cut carves* — the four files' codec namespaces are already contiguous and leaf-clean (classified this session; see Evidence base), so each carve is one block move into a new TU; (2) *pre-boot registration inversion* for `world_room_vnum` (db_world registers; null default = tripwire + NOWHERE, identical to the converter's current stub) and for `rename_char`'s `add_exploit_record` capture call; (3) *nm-gated library membership* with STOP-condition adjudication, exactly like rots_entity's two-round linkcheck cascade.

**Tech Stack:** C++20, CMake presets + flat Makefiles (×4 wiring per new TU), GoogleTest, characterization goldens, whole-archive linkchecks.

## Global Constraints

- **Zero behavior change for `ageland`.** Goldens byte-for-byte; ctest baseline **1273** both hosts (macOS-arm64 + rots64); expect 1274 after Task 4 (PersistLayerAcyclicity).
- **Branch `arch/persist-split`, stacked on `arch/entity-seed`** (PR #6, CI all green, awaiting owner merge). If PR #6 merges mid-wave, rebase onto master at the next task boundary and note it in the ledger.
- **Verbatim moves.** Carved blocks move byte-identically (programmatic diff per block); the only sanctioned deviations are the ones this plan names per task. Declarations stay in their current headers (`pkill.h`, `mail.h`, `boards.h`, `handler.h`, `db.h`); external linkage never changes.
- **`rots_convert` must keep linking and pass ConvertEquivalence 17/17 after every task**; functional smoke count stays 5292/5589.
- **Verification cadence per task:** macOS `macos-arm64` build + full ctest + native boot golden AND rots64 build + ctest + boot golden (docker `--pull never`, synchronous). New/rewritten test file → `macos-arm64-asan`. i386 battery at finalization only. NOTE: `build/` top-level currently holds the i386 container tree — implementers must `cmake --preset macos-arm64` (fresh configure) before the first macOS build and must NOT delete or reconfigure the top-level `build/` cache from the host (the container owns it; see the entity-seed ledger's round-2 incident).
- **Formatter hook:** python byte-edits for all existing .cpp/.h; new files via bash heredoc/python in WebKit style; `git diff --stat` check per file.
- Warnings are errors everywhere; `python3 tools/string_view_census.py --check` stays exit 0.
- **Root Makefile gotcha (entity-seed lesson):** its `test` target hand-lists linkcheck build targets — Task 4 must add `rots_persist_linkcheck` there, and NOBODY runs `make -n test` on the host (it executes `+`-prefixed lines and poisons `build/`).

---

## Evidence base (classified this session, branch arch/entity-seed)

**Codec blocks are contiguous and leaf-clean** (only callees: file I/O helpers, `json_utils`, `rots::text`, `std::strerror`; zero live-game globals, zero messaging):
- `pkill.cpp` (1268 lines): `namespace pkill_json` **lines 50-427** (11 functions, incl. `convert_legacy_pkill_file` with verify-reparse + `.migrated` rename). Runtime/capture half (435-EOF) keeps `pkill_tab`/rankings/`combat_list` walkers and the X-bridge `pkill_read_file`/`pkill_delete_file`/`pkill_update_file` (they touch `pkill_tab`/`player_table` — orchestration, not codec).
- `mail.cpp` (1045 lines): `namespace mail_json` **lines 134-536**. Runtime store (553-879: `find_char_in_index`, `persist_mail_or_log`, `index_mail`, `scan_file`, `has_mail`) and postmaster gameplay (886-1045) stay.
- `boards.cpp` (1710 lines): `namespace boards_json` **lines 761-1189**. Display half (119-755, `descriptor_list` walks at 490/695, `page_string` at 505/639), persist bridge (`save_board` 1192 / `apply_board_save_data` 1336 / `load_board` 1393 — they read `msg_storage[]`/write HTML) and constructors stay. Board globals are fully boards.cpp-encapsulated (grep-verified).
- `objsave.cpp` (1917 lines, API in handler.h:160-177, no objsave.h): P block **lines 92-477** (staging map `g_staged_account_backed_object_data` + key/take helpers, `read_open_file_contents`/`read_binary_file_contents`, `player_object_bucket_path`, `Crash_get_filename`, `player_objects_json_path`, `write_player_objects_json`, `Crash_get_file_by_name`, `Crash_delete_file`, `Crash_delete_crashfile`, `Crash_clean_file`, `update_obj_file`, `build_default_account_backed_object_data`, `stage_...`, `clear_...`, `register_char_teardown_hook`) plus pure tail/scattered helpers `cost_per_day` (1904), `secs_to_unretire` (1910), `Crash_is_unrentable` (1288). The serialize pair `Crash_obj2record`/`Crash_collect_objects` reads `obj_index[]` and is called only by the G/X orchestrators (`Crash_crashsave`/`idlesave`/`rentsave`) — it STAYS in objsave.cpp, avoiding any obj_index seam. Dead file-scope `FILE* fd;` (line 89, zero references) is dropped — the one deliberate deletion. Alias pair (`Crash_alias_load` 1217 / `Crash_collect_alias_data` 1256) and `Crash_restore_weight`/`Crash_calculate_rent`/`Crash_report_rent` stay with their G callers.
- `color.cpp`: pure conversion helpers `sync_color_slot_foreground_from_ansi` (:55, anon-ns), `convert_old_colormask` (:366), `nearest_ansi_color` (:402) + shared tables `color_color[]` (:294) / `num_of_colors` (:313) — all leaf-clean. convert_stubs.cpp carries synchronized verbatim copies (its only *executing* duplicates).
- **Leaf-helper homes confirmed:** `rots_asprintf` utility.cpp:806 (decl platform_compat.h:11), `mud_time_passed` utility.cpp:1019, `day_to_str` utility.cpp:1370 — all leaf-clean, NOT moved this wave (their callers stay app-side).
- **db_players' remaining upward references** (from the current weld ledger): `world_room_vnum` (db_world; save_char's guarded call — converter stubs it with a proven-unreachable NOWHERE tripwire), `add_exploit_record` (db_boot capture; called by `rename_char`), `find_name`/`find_player_in_table` (home: interpre.cpp/db_boot-tier), `fname`/`unaccent` (utility.cpp), `Crash_get_filename`/`Crash_delete_file`/`build_default_account_backed_object_data` (objsave → Task 2 moves them into the carved P TU). Task 4 resolves each (relocate/invert/adjudicate) with nm evidence.

---

## Task 1: `color_convert.cpp` leaf TU (kills the ledger's executing duplicates)

**Files:**
- Create: `src/color_convert.cpp`
- Modify: `src/color.cpp` (loses the three helpers + the two table definitions; gains externs), `src/convert_stubs.cpp` (delete the color section: `sync_color_slot_foreground_from_ansi`, `kNumColors`, `nearest_ansi_color` + `ansi_palette`, `convert_old_colormask`), `src/CMakeLists.txt` (ROTS_SERVER_SOURCES + rots_convert source list), `src/Makefile` + `src/tests/Makefile` (color_convert.o)

**Interfaces:**
- Produces: `color_convert.cpp` defining `nearest_ansi_color(int,int,int)`, `convert_old_colormask(char_file_u*)`, `sync_color_slot_foreground_from_ansi(char_prof_data*, int)` (this one LOSES its anonymous namespace — color.cpp:378/399 and convert_old_colormask both call it cross-TU now; add its declaration to `color.h` next to the other two, which are already declared there — verify and mirror), plus the `color_color[]` and `num_of_colors` DEFINITIONS (color.cpp keeps using them via extern; they are already extern-declared — check `color.h`/`consts` externs and keep declarations wherever they live today).
- ageland compiles it via ROTS_SERVER_SOURCES; rots_convert adds it to its direct source list and DELETES the four convert_stubs copies — both binaries now execute the single real definition (ConvertEquivalence is the drift-guard that this was byte-equivalent).

- [ ] Step 1: Carve the three functions + two tables verbatim into color_convert.cpp (anon-ns promotion for the sync helper is the one named deviation; comment it). color.cpp keeps everything else (`do_color`, `show_color_slot_summary`, name lookup at :358 uses `color_color` via extern).
- [ ] Step 2: Wire ×4; delete the convert_stubs color section; update ledger header.
- [ ] Step 3: Gates both hosts (1273/1273 + boot goldens) + ConvertEquivalence 17/17 + smoke count.
- [ ] Step 4: Commit `refactor: color_convert.cpp leaf TU — one real definition for color conversion (ledger executing-duplicates deleted)`

## Task 2: Carve the three codec TUs (pkill_json / mail_json / boards_json) + objsave's P block

**Files:**
- Create: `src/pkill_json.cpp` (pkill.cpp:50-427), `src/mail_json.cpp` (mail.cpp:134-536), `src/boards_json.cpp` (boards.cpp:761-1189), `src/obj_files.cpp` (objsave.cpp P inventory per the Evidence base)
- Modify: the four origin files (blocks removed; a one-line relocation marker comment each), `src/CMakeLists.txt`, `src/Makefile`, `src/tests/Makefile` (4 new TUs), `src/convert_stubs.cpp` (NO deletions yet — that's Task 3's converter re-plumb; do not touch)
- Headers: UNCHANGED (pkill.h/mail.h/boards.h/handler.h already declare everything; anon-ns helpers that move stay anon-ns inside their new TU — each block's internal helpers move with it)

**Interfaces:**
- Produces four TUs whose implementation is byte-identical to the carved blocks. objsave's carve follows the Evidence-base inventory EXACTLY: the 92-477 block + `Crash_is_unrentable` + `cost_per_day` + `secs_to_unretire`; `register_char_teardown_hook` moves too (it is wiring for `clear_account_backed_object_bytes_for_character`, which moves); drop the dead `FILE* fd;`. Origin files keep their runtime/bridge/gameplay halves and call the moved functions through unchanged declarations.
- Duplicate-hazard: anon-ns helpers used by BOTH halves of an origin file must be identified before carving (the classification found none, but verify: e.g. mail.cpp's `read_whole_file_contents`-style helpers are used only inside `mail_json`; boards.cpp has a SECOND `read_whole_file` at :1311 belonging to the bridge — leave it). If a helper turns out shared, STOP and report (do not duplicate silently).

- [ ] Step 1: Carve pkill_json.cpp; build + focused ctest (`-R "Pkill|CharacterizationJson"`); commit `refactor: carve pkill_json codec into pkill_json.cpp (persist split)`.
- [ ] Step 2: Same for mail_json.cpp (`-R "Mail|CharacterizationJson"`); commit `refactor: carve mail_json codec into mail_json.cpp (persist split)`.
- [ ] Step 3: Same for boards_json.cpp (`-R "Board|CharacterizationJson"`); commit `refactor: carve boards_json codec into boards_json.cpp (persist split)`.
- [ ] Step 4: Carve obj_files.cpp per the inventory; nm single-definition check across all objects; commit `refactor: carve obj_files.cpp (object-save paths/staging/JSON codec) out of objsave.cpp`.
- [ ] Step 5: Full both-host gates (1273/1273, boot goldens, ConvertEquivalence 17/17, smoke, census 0).

## Task 3: Converter re-plumb onto the carved TUs + ledger shrink

**Files:**
- Modify: `src/CMakeLists.txt` (rots_convert source list += `obj_files.cpp`; the three codec TUs are NOT added here — they join via the Task 4 library link), `src/convert_stubs.cpp` (delete: `Crash_get_filename`, `Crash_delete_file`, `build_default_account_backed_object_data`; update ledger header + the "objsave/boards/mail/pkill deliberately OUT" framing — objsave's P half is now IN)

**Interfaces:**
- rots_convert compiles obj_files.cpp directly (until Task 4 moves it into the library) and links the real path/staging/JSON-write definitions. STOP condition: if obj_files.cpp drags an undefined symbol into the converter link that isn't already resolved (expected resolutions: `objects_json` codecs ✓ linked, `player_table`/`top_of_p_table` ✓ db_players, account mirror helpers ✓ account_management, entity hooks ✓ RotS::entity), report symbol + home + options; do not stub without adjudication.

- [ ] Step 1: Re-plumb; link; delete the three stubs; ledger prose update.
- [ ] Step 2: Full both-host gates + ConvertEquivalence 17/17 + smoke (count must stay 5292/5589 — the converter's behavior must not change, only its linkage).
- [ ] Step 3: Commit `refactor: rots_convert links real object-file codecs (obj_files.cpp); 3 more stubs die`

## Task 4: Invert the last persist→up edges; stand up `rots_persist` (L3) + linkcheck

**Files:**
- Modify: `src/db.h`/`src/db_world.cpp`/`src/db_players.cpp` (world_room_vnum inversion), `src/db_boot.cpp` + `src/db_players.cpp` (add_exploit_record inversion), `src/comm.cpp` (registrations in run_the_game — mirror the entity-hook pattern), `src/entity_hooks.h` OR a new `src/persist_hooks.h` (implementer picks: if both hooks are persist-facing, a small separate header keeps ownership clear — follow entity_hooks.h's style), `src/CMakeLists.txt` (ROTS_PERSIST_SOURCES + rots_persist STATIC + RotS::persist + linkcheck + rots_convert links RotS::persist and drops now-library'd direct sources), `src/tests/persist_linkcheck_main.cpp` (new), root `Makefile` (add rots_persist_linkcheck to the test target's hand-list), `src/convert_stubs.cpp` (delete stubs made real by the library link — candidates: `world_room_vnum` (hook default replaces it), `fname`/`unaccent`/`find_name`/`find_player_in_table` IF relocated, `add_exploit_record` (hook default), `recalc_skills`?? NO — spec_pro stays app; keep), plus flat Makefiles unchanged (no libraries there)

**Interfaces:**
- **world_room_vnum inversion:** `rots::persist::set_room_vnum_hook(int (*)(int))`, null default = `rots::log::write_stderr` tripwire + return NOWHERE (byte-identical to the converter's current proven-unreachable stub); db_world.cpp registers `world_room_vnum` in run_the_game() pre-boot (same slot as the other registrations); `save_char` calls the dispatch. The db.h-declared `world_room_vnum` symbol itself stays defined in db_world.cpp (other callers unaffected — verify caller set; if save_char is the ONLY caller, simplify: the dispatch IS the only consumer and the stub deletion follows).
- **add_exploit_record inversion:** same pattern; `rename_char` (db_players) calls the dispatch; db_boot.cpp registers the real capture function pre-boot; null default = loud tripwire no-op (matches the current stub's semantics — rename_char is unreachable in the converter).
- **Relocations (P-natural, adjudicate-by-evidence):** `find_player_in_table` → db_players.cpp (it is a player_table index lookup; its current home and all callers verified by nm/grep first); `fname`/`unaccent`/`find_name` — ONLY if nm shows the archive needs them AND they are leaf-clean; otherwise leave and keep their converter stubs (they may simply stay app-side if no persist TU references them after the carves — the ledger says their only persist-side callers are rename_char/read_crime_file paths; verify).
- **ROTS_PERSIST_SOURCES (nm-gated candidate set):** `db_players.cpp, character_json.cpp, objects_json.cpp, exploits_json.cpp, account_management.cpp, account_cache.cpp, obj_files.cpp, pkill_json.cpp, mail_json.cpp, boards_json.cpp, convert_exploits.cpp, convert_plrobjs.cpp` (the last two contain ACMDs whose output calls now resolve in rots_core — nm decides; if their command halves drag interpre-tier symbols, SPLIT decision goes to the controller, with "defer both TUs from the library, keep direct-compiled in converter" as the recorded fallback). `save_benchmark.cpp`/`savebench.cpp` join only if nm-clean; else defer with a comment.
- `rots_persist` PUBLIC-links `RotS::entity RotS::core RotS::platform rots_build_flags`; `PersistLayerAcyclicity` linkcheck force-loads rots_persist + normal-links entity/core/platform (mirror the entity block verbatim, incl. LINK_DEPENDS + per-platform spellings). ageland links `RotS::persist`; ageland_tests compiles `${ROTS_PERSIST_SOURCES}` directly; the TUs leave ROTS_SERVER_SOURCES. rots_convert becomes `convert_main.cpp convert_stubs.cpp char_utils.cpp` + `RotS::persist RotS::entity RotS::core RotS::platform`.
- **STOP condition (same contract as the entity wave):** any linkcheck undefined symbol not resolved by this plan's inversions/relocations is reported with symbol + home + disposition options; the controller adjudicates. Two rounds of cascade are normal; do not stub, weaken the check, or silently drop TUs.

- [ ] Step 1: The two inversions + registrations; both-host gates green (goldens prove ageland identical).
- [ ] Step 2: nm census on the candidate set (i386 or arm64 objects); report the residual-edge table BEFORE creating the library target; adjudication checkpoint.
- [ ] Step 3: Library + linkcheck + re-plumbs + root-Makefile hand-list; expect ctest 1274 (PersistLayerAcyclicity) both hosts.
- [ ] Step 4: Stub deletions per what the link now resolves; ledger header rewrite (the "deliberately OUT" era ends); census 0; ConvertEquivalence 17/17 + smoke.
- [ ] Step 5: Commit sequence: `refactor: invert world_room_vnum + add_exploit_record via pre-boot registration (persist hooks)` then `feat: rots_persist static library + PersistLayerAcyclicity linkcheck; converter links RotS::persist`

## Task 5: Docs + finalization

- [ ] Step 1: docs/BUILD.md "Library layering" (rots_persist membership + what PersistLayerAcyclicity proves + the four-carve story + remaining ledger inventory update); spec §3 (L3 peer-tier note updated: persist now archived; world/combat still app-compiled) + §10 step 4 as-built slice-2 note; AGENTS.md one-liner. convert_stubs.cpp header refresh.
- [ ] Step 2: i386 battery (the wave's es-i386-battery.sh pattern — sequential; container owns `build/`).
- [ ] Step 3: Whole-branch review (most capable model) with the accumulated Minor list; fix wave; push; PR **stacked on arch/entity-seed** (base = arch/entity-seed if PR #6 unmerged, else master after rebase). Merge = owner's call.

---

## Self-Review Notes

- **Spec coverage:** §10 step 4 slice 2 ✓ (persist domain lib); §4b's recorded follow-on (objsave/boards/mail membership) ✓ Tasks 2-4; §11 enforcement extended ✓ PersistLayerAcyclicity; the L3-peer-tier caveat honored — rots_world/rots_combat stay app-compiled, and the only persist→world edge is inverted, not hidden.
- **Right-sizing:** Task 2 groups four identical-shape single-cut carves with per-carve commits + focused tests (db-split precedent); Task 4 is the judgment-heavy one and carries the nm checkpoint + STOP contract that worked twice last wave.
- **Placeholder scan:** carve inventories are exact (line ranges + named exceptions); hook signatures follow the in-tree entity_hooks.h pattern by explicit reference; the one ambiguous membership call (convert_exploits/convert_plrobjs, save_benchmark/savebench) is an explicit adjudication point with a recorded fallback, not a TBD.
- **Type consistency:** `RotS::persist`, `PersistLayerAcyclicity`, `obj_files.cpp`, `pkill_json.cpp`/`mail_json.cpp`/`boards_json.cpp`, `set_room_vnum_hook` used consistently across tasks.
- **Risks:** (1) account_management.cpp is the likely cascade source in Task 4 — the STOP contract covers it; (2) stacked-branch rebase mid-wave — constraint names the task-boundary rule; (3) the shared `build/` tree host/container hazard — constraint repeats the round-2 lesson; (4) boards' bridge functions stay behind deliberately — the library carve does NOT chase `save_board`/`load_board` this wave (they walk runtime globals; they move only when boards' runtime half gets its own split, recorded as follow-on).
