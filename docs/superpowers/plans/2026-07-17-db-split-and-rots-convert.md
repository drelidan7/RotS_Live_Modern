# db.cpp Split + rots_convert Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split `src/db.cpp` (5803 lines) along the persist/world seam into `db_world.cpp` / `db_players.cpp` / `db_boot.cpp` (+ an entity-lifecycle TU), then stand up the `rots_convert` executable — the CI-linked acid test that the persistence boundary holds (spec §4, docs/superpowers/specs/2026-07-16-library-architecture-design.md).

**Architecture:** Functions and globals move **verbatim** into their classified TU (W/P/B/X per the inventory tables below); `db.h` keeps declaring everything (no caller changes). The one hard P→W data edge (`save_char` reading `world[ch->in_room].number`) becomes a seam function defined in `db_world.cpp`; live-game *capture* functions (`record_crime`, `add_exploit_record`) land in `db_boot.cpp` so the persist TU carries only codecs. `rots_convert` links platform + core + the character-conversion persist subset + minimal entity code, with a small, loud `convert_stubs.cpp` acting as the **explicit weld ledger** for the few app/combat symbols the entity TUs still reference — shrinking that file is the follow-on metric.

**Tech Stack:** C++20, CMake presets + flat Makefiles, GoogleTest, characterization goldens, the acyclicity-linkcheck pattern.

## Global Constraints

- **Zero behavior change for `ageland`.** All goldens byte-for-byte; ctest total currently **1253** on macOS/rots64.
- **Verbatim moves.** Functions/globals move with bodies, comments, and file-local helpers intact. No reordering within a destination TU beyond what compilation requires; anonymous-namespace helpers move with their callers (they are file-local — each destination TU gets its own copy ONLY if both halves use it; per the inventory no anon helper is shared across halves).
- **`db.h` is the stable surface**: every symbol it declares keeps identical linkage (external functions must not become static). Callers outside db.cpp are untouched.
- **Sources stay flat in `src/`** (`db_world.cpp`, `db_players.cpp`, `db_boot.cpp`, `entity_lifecycle.cpp`, `convert_main.cpp`, `convert_stubs.cpp`) — the flat Makefiles compile same-dir only.
- **`git mv db.cpp db_boot.cpp`** as the final rename (preserves blame for the boot remainder).
- **Verification cadence per task:** macOS `macos-arm64` build + full ctest + native boot golden AND rots64 build + ctest + boot golden; docker synchronous (WAIT on auto-backgrounded completions). New/rewritten test files → `macos-arm64-asan` run. i386 battery at finalization only.
- **CRLF/format-hook:** binary-mode python edits for existing files; new files via Write. db.cpp's line endings: check and preserve per moved block.
- Warnings are errors everywhere. `python3 tools/string_view_census.py --check` must stay exit 0 after any textual-interface change.
- Line ranges below refer to `git show 901191c:src/db.cpp` (pre-split; locate by name in the current file).

---

## Authoritative classification (from the reviewed inventory)

**→ `db_world.cpp` (W):** `count_hash_records` 960, `index_boot` 978, `load_rooms` 1118, `setup_dir` 1246, `check_start_rooms` 1271, `renum_world` 1307, `symbol_to_map` 1318, `draw_map` 1325, `reset_small_map` 1370, `initialiaze_small_map` 1379, `load_scripts` 1407, `vnum_mobile` 1474, `vnum_object` 1490, `read_mobile` 1506, `load_mobiles` 1654, `read_object` 1823, `load_objects` 1867, `set_exit_state` 1989, `fread_string` 3304, `fread_line` 3381, `real_room` 3893, `real_mobile` 3921, `real_object` 3946, `real_program` 3971, `load_mudlle` 3988, `boot_mudlle` 4016, all `room_data`/`room_data_extension` methods 4674-4896 (incl. `operator[]`), `dummy_room_data` 4684. **Globals:** `room_data::BASE_*` statics 81-84, `world` 86, `top_of_world` 87, `mob_index` 90, `mob_proto` 91, `top_of_mobt` 92, `obj_index` 95, `obj_proto` 96, `top_of_objt` 97, `script_table`/`top_of_script_table` 101-102, `mobile_program`/`mobile_program_zone`/`num_of_programs` 105-107, `new_mud` 121, `r_immort_start_room`/`r_frozen_start_room`/`r_retirement_home_room` 129-131, `world_map` 163, `small_map` 164. Plus the NEW seam function (Task 1).
**→ `db_players.cpp` (P):** `inc_p_table` 545, the anon block 561-833 (all P), `legacy_player_path_for_testing` 836, `read_filename_field` 845, `build_directory` 861, `build_player_index` 923, anon block 2145-2173 (normalize/sanitize), `load_player_from_text` 2175, `load_player` 2470, `load_char` 2506, `load_char_from_text` 2522, `store_to_char` 2539, `char_to_store` 2674, `create_entry` 2760, `ensure_player_index_entry` 2782, both `update_player_index_entry_from_store` 2799/2804, `old_create_entry` 2852, `move_char_deleted` 2914, `delete_character_file` 2926, `write_player_text` 2954, `save_player` 3112, `save_char` 3192 (with the Task 1 seam call), `get_char_directory` 3683, the whole `crime_json` namespace 4049-4449, `read_crime_file` 4466, `add_crime` 4533, `know_of_crime` 4601, `forget_crimes` 4611, `write_exploits` 4898, anon block 4924-5349 (exploit codec), `load_exploit_records_for_character` 5351, `write_exploit_record_for_character` 5373, `load_object_save_data_for_character` 5429, `delete_exploits_file` 5650, `rename_char` 5724. **Globals:** `player_table` 109, `player_fl` 110, `top_of_p_table` 111, `top_of_p_file` 112, `top_idnum` 113, `crime_record` 115, `crime_file` 116, `num_of_crimes` 117, `pwdcrypt` 240.
**→ `db_boot.cpp` (B — the renamed db.cpp remainder):** `reboot_wizlists` 247, `ACMD(do_reload)` 253, `boot_db` 336, `reset_time` 536, `file_to_string_alloc` 3634, `file_to_string` 3648, `boot_crimes` 4037, **capture-side live-game functions** `record_crime` 4451 (world-walk) and `add_exploit_record` 5509 (combat_list walk) with a comment marking them capture-not-codec. **Globals:** `buf`/`buf1`/`buf2`/`arg` 76-79, `character_list` 89, `object_list` 94, `fight_messages` 99, `no_mail` 119, `mini_mud` 120, `no_rent_check` 122, `boot_time` 123, `restrict` 125, `boot_mode` 126, the 18 text ptrs 132-149, `help_fl`/`help_index`/`top_of_helpt` 151-153, `beginning_of_time` 155, `time_info` 156, `weather_info` 157, `waiting_list` 159, `fast_update_list` 160, `death_waiting_list` 161, `judppwd`/`judpavailable` 167-168. (The live-list/combat squatters keep a comment noting their eventual homes.)
**→ `entity_lifecycle.cpp` (X — new TU, future rots_entity):** `free_alias_list` 3438, `owned_alias_list::clone` 3458, `free_char` 3488, `make_char_data` 3586, `free_obj` 3595, `reset_char` 3740, `clear_char` 3771, `clear_object` 3821, `init_char` 3832. (Called by BOTH halves — `read_mobile`→`clear_char`, store paths→`clear_char`/`init_char` — so they live in neither.)

**The seam (Task 1):** `int world_room_vnum(int room_index);` declared in `db.h` next to `real_room`, defined in `db_world.cpp` as `return world[room_index].number;` — `save_char` (P) calls it instead of touching `world[]`. `rots_convert` provides its own definition (stub ledger).

---

## Task 1: Carve `db_world.cpp` (+ the save_char seam)

**Files:** Create `src/db_world.cpp`; modify `src/db.cpp`, `src/db.h` (add `world_room_vnum` decl), `src/CMakeLists.txt` (ROTS_SERVER_SOURCES + ageland_tests via the shared list), `src/Makefile` (OBJFILES), `src/tests/Makefile` (OBJFILES + per-object rule per existing pattern).
**Interfaces:** Produces `db_world.cpp` holding every W item above; `world_room_vnum(int)` (db.h-declared, db_world-defined); `save_char` in db.cpp now calls `world_room_vnum(ch->in_room)` — find the exact `world[ch->in_room].number` read at ~line 3205 and replace only that expression.

- [ ] Step 1: Create `db_world.cpp` with db.cpp's include list pruned to what the W half needs (start from the full list; remove obviously-unneeded persist headers only if the build proves them unneeded — do not over-minimize). Move all W functions/globals verbatim, preserving relative order. File-local W prototypes (the 170-200 block entries for world loaders) move too.
- [ ] Step 2: Add the seam: `db.h` decl + `db_world.cpp` definition + the one-expression change in `save_char`.
- [ ] Step 3: Wire all four build systems (new TU flat).
- [ ] Step 4: Gates (macOS + rots64, ctest 1253 + boot goldens both). Boot goldens are the key check — boot exercises every W loader.
- [ ] Step 5: Commit `refactor: carve db_world.cpp out of db.cpp (world load/index/reset + world_room_vnum seam)`.

## Task 2: Carve `db_players.cpp` + `entity_lifecycle.cpp`; remainder becomes `db_boot.cpp`

**Files:** Create `src/db_players.cpp`, `src/entity_lifecycle.cpp`; `git mv src/db.cpp src/db_boot.cpp`; modify the four build files (replace db.cpp with the three new names; tests/Makefile per-object rules likewise).
**Interfaces:** Produces the P and X TUs per the classification; `db_boot.cpp` = boot orchestration + capture functions + shared globals. `db.h` unchanged apart from Task 1's addition.

- [ ] Step 1: Create `db_players.cpp` (P items verbatim, include list pruned from db.cpp's; keep `char_utils.h`, `character_json.h`, `exploits_json.h`, `account_*`, `player_file_finalize.h`, `rots/persist/file_formats.h`). Anonymous-namespace helper blocks move whole.
- [ ] Step 2: Create `entity_lifecycle.cpp` (X items verbatim; header comment: interim home, future rots_entity; these are shared by world instantiation and persist store paths).
- [ ] Step 3: `git mv db.cpp db_boot.cpp`; delete the moved bodies from it; keep B items + capture functions with the capture-not-codec comment; verify no function remains duplicated (`nm` on the built objects: each symbol defined exactly once).
- [ ] Step 4: Build-system updates; then gates (macOS + rots64, 1253 + boot goldens). Expect missing-extern fix-ups (e.g. a moved function needing an `extern` it previously saw file-locally) — resolve with explicit externs or db.h, never by re-merging blocks; list each in the report.
- [ ] Step 5: Monolithic smoke on rots64 (`cd /rots/src/tests && make clean && make -j"$(nproc)" tests && ../../bin/tests --gtest_list_tests | head`) — its Makefile compiles production objects itself.
- [ ] Step 6: Commit `refactor: split db.cpp into db_boot/db_players + entity_lifecycle (persist/world seam cut)`.

## Task 3: Stand up `rots_convert`

**Files:** Create `src/convert_main.cpp`, `src/convert_stubs.cpp`; modify `src/CMakeLists.txt` (new executable target), `docs/BUILD.md` (deferred to Task 5).
**Interfaces:** Executable `rots_convert` linking: `RotS::platform`, `RotS::core`, and compiling directly: `db_players.cpp, entity_lifecycle.cpp, character_json.cpp, objects_json.cpp, exploits_json.cpp, account_management.cpp, account_cache.cpp, convert_exploits.cpp, convert_plrobjs.cpp, object_utils.cpp, char_utils.cpp, color.cpp?` — NO world/combat/commands/app TUs. (`objsave`/`boards`/`mail`/`pkill` are deliberately OUT this wave — their welds are catalogued follow-on work.)

- [ ] Step 1: `convert_main.cpp`: minimal CLI — `rots_convert --lib <libdir> [--dry-run]`: chdir to libdir, `build_player_index()`, then for each `player_table` entry: `load_char(name, &st)`-equivalent via the existing load path and `save_char`-equivalent store — REUSE the exact functions the MUD uses (that is the point: byte-identical conversion by construction). Investigate and document the load-room handling: `store_to_char` sets `ch->in_room = GET_LOADROOM(ch)`; the converter must ensure the persisted load_room in the output equals the input's (the `world_room_vnum` stub's return feeds `save_char`'s room write — get this right and PROVE it with Task 4's equivalence test; if `save_char` guards on a valid room index, the stub returning `NOWHERE` semantics may be wrong — read the guard and match the in-MUD behavior for a character that is not in the world).
- [ ] Step 2: `convert_stubs.cpp` — the WELD LEDGER. One loud section per stubbed symbol, each with: the symbol, its real home, why the converter never exercises it, and the follow-on that would remove it. Expected initial ledger (from the nm closure analysis; adjust to what the link actually demands, and keep every stub minimal + `rots::log::write_stderr`-logging where reachable): `send_to_char` (comm), `act` (comm), `descriptor_list` (comm), `track_specialized_mage`/`untrack_specialized_mage` (comm), `get_hit_text` (fight), `player_spec::wild_fighting_handler` ctor + `get_attack_speed_multiplier` (combat), `nearest_ansi_color` (color — only if color.cpp isn't linked; prefer linking color.cpp if it's clean, check nm first), `world_room_vnum` (db_world — converter definition per Step 1's investigation), plus whatever the first link surfaces. If the link demands something from world/combat that is NOT stub-safe (i.e. the converter path genuinely executes it), STOP and report — that's a real design problem, not a stub candidate.
- [ ] Step 3: CMake target: `add_executable(rots_convert convert_main.cpp convert_stubs.cpp <the TU list>)`, linking `RotS::platform RotS::core rots_build_flags`; part of `all` so every CI job builds it (the CI-blocking boundary check per spec §4b). NOT added to the flat Makefiles this wave (CMake-only target; note in BUILD.md).
- [ ] Step 4: Gates: the link succeeding IS the acceptance test; then full both-host gates (1253 + boot goldens) proving ageland unaffected.
- [ ] Step 5: Commit `feat: rots_convert executable — persistence-boundary acid test (convert_stubs.cpp = weld ledger)`.

## Task 4: Conversion-equivalence test

**Files:** Create `src/tests/rots_convert_equivalence_test.sh` (or a gtest TU if fixtures allow — implementer picks based on what `characterization_json_tests`/`db_save_roundtrip_tests` fixtures provide); modify `src/CMakeLists.txt` (add_test, gated like other fixture-bound tests).

- [ ] Step 1: Build a test that proves §4b's claim: converting a fixture character via `rots_convert` produces byte-identical output to the in-MUD conversion path. Use portable text-pfile fixtures if available (legacy binary fixtures are 32-bit-gated — if only those exist, gate the test to the i386/CI legacy job and provide a macOS-skipping stub, documenting why).
- [ ] Step 2: Register in CTest; run it; new-test ASan pass (`macos-arm64-asan` full configure/build/ctest) if a compiled test was added.
- [ ] Step 3: Full both-host gates; note the new ctest total.
- [ ] Step 4: Commit `test: rots_convert output byte-equivalent to in-MUD conversion`.

## Task 5: Docs + finalization

- [ ] Step 1: `docs/BUILD.md`: db split (three TUs + entity_lifecycle), `rots_convert` (what it links, what the stub ledger means, CMake-only), updated library-layering text. Spec §4: as-built note (converter membership subset — objsave/boards/mail deferred with their welds catalogued; capture/codec split for crime+exploits; the `world_room_vnum` seam). AGENTS.md: add `rots_convert` one-liner to the build section if warranted.
- [ ] Step 2: Finalization battery (scripted, sequential): i386 `make test` + monolithic runner + i386 boot golden + flat i386 `make all` + `linux-x86-legacy` preset build (+ archive + rots_convert build check).
- [ ] Step 3: Commit docs; whole-branch review; push + PR for the six blocking CI jobs; merge = owner's call.

---

## Self-Review Notes

- **Spec §4a coverage:** the three-TU split ✓ (Tasks 1-2, with the inventory-driven X tier resolving the "shared entity lifecycle" problem the spec didn't anticipate); §4b converter ✓ (Task 3) with the membership honestly narrowed to the character-conversion mission; CI-linked boundary ✓ (part of `all` + every CI job).
- **The stub ledger is a deliberate, documented compromise**: link-level substitution surfaces each weld precisely (spec §4b's stated purpose) without blocking this wave on the send_to_char output seam; the ledger file makes the remaining debt enumerable and its shrinkage measurable.
- **Capture/codec split** (record_crime, add_exploit_record → db_boot) is what makes db_players converter-linkable at all — it is the plan's key non-obvious decision, justified by the caller census (capture functions are called from combat/commands, never from persist codecs).
- **Risk:** load-room semantics in the converter (Task 3 Step 1 / Task 4) — explicitly flagged as a correctness checkpoint with an equivalence test as the proof.
- **Type-consistency:** `world_room_vnum`, `entity_lifecycle.cpp`, `convert_stubs.cpp`, target name `rots_convert` used identically across tasks.
