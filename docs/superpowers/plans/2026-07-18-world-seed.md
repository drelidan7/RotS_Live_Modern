# World-Core Seed (rots_world) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stand up the **rots_world** (L3) STATIC library seeded with `{db_world.cpp, zone_load.cpp (new), weather.cpp}` + a `WorldLayerAcyclicity` whole-archive linkcheck — the fourth domain library. Enablers: five storage moves, three relocations, db_world's scratch-buffer conversion, one adjudicated disposition (`target_data::operator=`), and three pre-boot hook inversions.

**Architecture:** The same three instruments, fourth application: verbatim relocation into the owning layer; definition-only storage moves (precedent: pulse/top_idnum); pre-boot registration inversion (precedent: eight existing registrations). Plus one codec/runtime-style split (zone.cpp — precedent: pkill/mail/boards).

**Scope honesty:** graph/script/mudlle/shape* are command/editor-coupled and stay app-compiled (documented). handler.cpp/utility.cpp remain app-tier pending the spec §7 Placement seam (recorded as the next major effort). This wave branches directly off master (no stack).

**Tech Stack:** C++20, CMake presets + flat Makefiles (×4 wiring per new TU), GoogleTest, goldens, whole-archive linkchecks.

## Global Constraints

- **Zero behavior change for `ageland`.** Goldens byte-for-byte; ctest baseline **1274** both hosts → expect **1275** after Task 5 (WorldLayerAcyclicity).
- Branch `arch/world-seed` off master @38e55c2. Verbatim moves; declarations stay in current headers; external linkage preserved; named deviations only.
- `rots_convert` links no world code — it must stay entirely unaffected (its gates run anyway: ConvertEquivalence 17/17, smoke 5292/5589).
- **Per-task gates:** macOS `macos-arm64` build + full ctest + native boot golden AND rots64 preset + ctest + boot golden (synchronous). **Boot goldens are this wave's decisive check** — the moved/converted code IS the boot path. i386 battery at finalization. Top-level `build/` is the container's — hands off; never `make -n test` on host. After any CMake source-list change, expect the container tree to need its generated-files wipe (3 prior incidents; the battery script may pre-clean `build/CMakeCache.txt`+`CMakeFiles` as its step 0 — add that to the battery script this wave).
- **Formatter hook:** python byte-edits for all existing .cpp/.h; new files via heredoc/python, WebKit style; `git diff --stat` per file. Census exit 0; warnings are errors.
- Root Makefile hand-list gains `rots_world_linkcheck` in Task 5.

---

## Evidence base (nm census + classification, this session, master @38e55c2)

- **zone.cpp** (zone_table/top_of_zone_table DEFINED at zone.cpp:20-21 — NOT db_world): L half CONTIGUOUS at lines 48-302 (`zone_load_cursor` static :48, TESTING seam `reset_zone_load_cursor_for_testing` :56, `load_zones` :69 [caller db_world.cpp:297 index_boot], `renum_zone_table` :204 [caller db_boot.cpp:376], `renum_zone_one` :219 [also called by shapezon.cpp:524 — calls down after split]). R half (312-966: `zone_update` [comm heartbeat], `check_if_flag` [reads weather_info + pkill fame — stays R], `reset_zone` [handler placement calls + do_wear], reset_q pool, `is_empty` [descriptor_list walk]) stays as zone.cpp. L-half callees: fread_string (db_world ✓in-lib), CREATE/free_function ✓platform, vmudlog ✓platform, real_mobile/real_room/real_object ✓in-lib.
- **register_npc_char** (handler.cpp:2017-2044) + its only global `last_control_set` (handler.cpp:95, all 4 refs inside the function): pure abs_number allocation over the char_exists bit-array that already lives in entity_lifecycle.cpp:683-689. `register_pc_char` (handler.cpp:2046, one-line forwarder) STAYS in handler (calls down). Callers: db_world.cpp:839 (read_mobile), interpre via the forwarder, tests.
- **dice(int,int)** (utility.cpp:733-749): loops rots_rng::next() with a mudlog guard — deps are both platform now. ~15 callers. → rots_util.cpp.
- **target_data::operator=** (interpre.cpp:692-704, operator== at :706): deps = member cleanup() + `get_from_txt_block_pool()` + TARGET_TEXT. db_world references it. Disposition needs one fact this analysis lacks: `get_from_txt_block_pool`'s home TU and dependencies — Task 2 pre-checks and STOPs for adjudication (candidate dispositions: relocate the operators + txt_block pool machinery to entity_lifecycle if the pool is leaf-clean like the affected_type pool was; else hook).
- **Storage moves (definition-only; all readers/writers extern, spread is irrelevant to linkage):** `character_list` (db_boot.cpp:102) + `object_list` (db_boot.cpp:104) → entity_lifecycle.cpp (they are lists OF entities; entity_lifecycle owns free_char/free_obj); `weather_info` (db_boot.cpp:141; single-writer weather.cpp) → weather.cpp; `boot_mode` (db_boot.cpp:115) + `mini_mud` (db_boot.cpp:110) → db_world.cpp (db_boot/comm extern-write down). NOT needed (already resolve in rots_core): average_mob_life/immort_start_room/retirement_home_room (config.cpp), mortal_start_room/mortal_idle_room/moon_phase/rev_dir/num_of_sector_types (consts.cpp), r_mortal_start_room/r_mortal_idle_room (consts.cpp — written by db_world via extern, defined in core = downward ✓); r_immort_start_room/r_retirement_home_room already in db_world.
- **db_world scratch-buffer sites (buf/buf1/buf2 → locals, std::format pattern):** index_boot 177-314 (buf1 index filenames, buf2 path/error labels incl. the boot_the_shops(…, buf2) call at :300), load_rooms 317-444 (buf2 error label :330; buf fgets/sscanf scratch :340-407), setup_dir :449, load_scripts :621-661, read_mobile :715 + :772-775, load_mobiles :878-905, read_object :1034, load_objects :1090-1150, set_exit_state :1220-1259 (act() door messages), boot_mudlle :1742-1744. (count_hash_records/fread_string/load_mudlle already use locals — untouched.)
- **boot_the_shops** (shop.cpp:627-694): genuinely parse-side (fread_string/fscanf into shop_index) but its TU is command-tier and it calls `is_ok` (time_info-coupled) — HOOK, not relocation (registered by shop.cpp pre-boot; index_boot dispatches with a LOCAL error-label string post-buf-conversion).
- **mudlle_converter** (mudlle.cpp:1242+): pure text transform, called once at boot from db_world's boot_mudlle (:1524, driven by db_boot.cpp:358) — HOOK (mudlle.cpp registers).
- **weather MSDP push:** two `send_msdp_function(lambda)` sites (weather.cpp:435 eMSDP_WORLD_TIME; :568 eMDSP_WEATHER with per-char OUTSIDE conditional); the local dispatcher `send_msdp_function` (weather.cpp:175) walks descriptor_list; MSDPSend/MSDPSetString live in protocol.cpp. → HOOK: `dispatch_weather_msdp_update(kind)` at the two sites; the dispatcher + both lambda bodies relocate VERBATIM into the registered implementation (new function in protocol.cpp — it reads time_info/weather_info/weather_messages via extern, downward once the lib exists... NOTE time_info is db_boot's: reading it from protocol.cpp (app) is fine — the app reads wherever it likes; only the world lib's own references matter).

---

## Task 1: Relocations + storage moves

**Files:** Modify handler.cpp (loses register_npc_char + last_control_set), entity_lifecycle.cpp (+register_npc_char, +last_control_set, +character_list/object_list definitions), utility.cpp (loses dice), rots_util.cpp (+dice), db_boot.cpp (loses 4 definitions), db_world.cpp (+boot_mode, +mini_mud), weather.cpp (+weather_info). Headers unchanged (verify each symbol's declaring header; report).

- [ ] Step 1: Relocate register_npc_char (+last_control_set) → entity_lifecycle.cpp; dice → rots_util.cpp (byte-identity; PlatformLayerAcyclicity must stay green — dice's mudlog dep is platform-resident).
- [ ] Step 2: Definition-only storage moves with ownership comments (precedent citations: pulse/top_idnum): character_list + object_list → entity_lifecycle.cpp; weather_info → weather.cpp; boot_mode + mini_mud → db_world.cpp. db_boot.cpp keeps externs where it still references them.
- [ ] Step 3: nm single-definition; full both-host gates (1274/1274, boot goldens both, ConvertEquivalence, smoke, census).
- [ ] Step 4: Commit `refactor: world-seed relocations + storage moves (register_npc_char, dice, entity lists, weather_info, boot flags)`

## Task 2: db_world scratch-buffer conversion + target_data disposition

- [ ] Step 1: Convert every buf/buf1/buf2 use in the Evidence-base site list to local std::string/std::format (or fixed local char arrays where a callee writes into the buffer — match each site's need; fgets/sscanf scratch gets a local char array, message composition gets std::format), byte-identical output strings (established method; quote any site where the composed bytes could differ and STOP if one genuinely would). The index_boot:300 call passes a LOCAL label to boot_the_shops (signature unchanged).
- [ ] Step 2: PRE-CHECK `get_from_txt_block_pool`: home TU + full deps + all callers. If it is a leaf pool like the affected_type pool was → relocate the pool machinery + target_data::operator=/operator== to entity_lifecycle.cpp (report as adjudication-recommended, then STOP for controller confirmation with the evidence). If it drags session/interpre state → STOP with hook-vs-defer options.
- [ ] Step 3: Full both-host gates (boot goldens decisive — every converted site is on the boot path); commit `refactor: db_world composes locally (scratch globals retired); target_data disposition per adjudication`

## Task 3: The three hooks (boot_the_shops, mudlle_converter, weather MSDP)

**Files:** Create src/world_hooks.h (entity_hooks.h style); modify db_world.cpp (2 dispatch sites: index_boot DB_BOOT_SHP case; boot_mudlle), weather.cpp (2 dispatch sites + send_msdp_function/lambda block REMOVED), shop.cpp (+register_boot_shops_hook), mudlle.cpp (+register_mudlle_converter_hook), protocol.cpp (+register_weather_msdp_hook + the relocated broadcast implementation, verbatim lambda bodies), comm.cpp run_the_game (+3 registrations → 11), gtest_main.cpp (+3 → 10).

- [ ] Step 1: Hooks + dispatches + registrations. Hook storage POD-null in db_world.cpp/weather.cpp (or one world_hooks TU section — implementer picks the existing pattern's closest analogue and documents). Null defaults: boot_the_shops → tripwire + no-op (ageland always registers pre-boot; a missing registration would boot without shops — the tripwire makes it unmissable); mudlle_converter → tripwire + return input unchanged; weather-MSDP → silent no-op (a pure notification; tests without protocol registration must not spam — note the deliberate contrast with the tripwire defaults, mirroring the char-teardown precedent).
- [ ] Step 2: Full both-host gates; commit `refactor: invert shop-boot, mudlle-convert, weather-MSDP edges via world hooks`

## Task 4: zone.cpp split → zone_load.cpp

- [ ] Step 1: Carve lines 48-302 (cursor + TESTING seam + load_zones + renum pair) + the zone_table/top_of_zone_table DEFINITIONS (:20-21, with ownership comment) into new src/zone_load.cpp verbatim; zone.cpp keeps the R half + externs. Check zone.h (or wherever declarations live) — unchanged expected.
- [ ] Step 2: Wiring ×4 (CMake ROTS_SERVER_SOURCES for now, flat Makefiles, tests Makefile); nm single-definition; zone_tests.cpp (uses the TESTING seam) must still build/pass.
- [ ] Step 3: Full both-host gates; commit `refactor: carve zone_load.cpp (zone-file parse + zone_table storage) out of zone.cpp`

## Task 5: rots_world (L3) + WorldLayerAcyclicity

- [ ] Step 1: ROTS_WORLD_SOURCES {db_world.cpp, zone_load.cpp, weather.cpp}; add_library rots_world STATIC + RotS::world alias; PUBLIC RotS::persist RotS::entity RotS::core RotS::platform rots_build_flags (db_world calls rots::persist::set_room_vnum_hook — the sanctioned L3-peer edge; document it); ageland links RotS::world; TUs leave ROTS_SERVER_SOURCES; ageland_tests compiles ${ROTS_WORLD_SOURCES}; rots_world_linkcheck (force-load world, normal-link persist/entity/core/platform, LINK_DEPENDS, both spellings) + ctest WorldLayerAcyclicity; root Makefile hand-list += rots_world_linkcheck. STOP contract on any undefined symbol (cascades expected-possible; adjudication as always).
- [ ] Step 2: Full both-host gates (expect 1275/1275); commit `feat: rots_world static library (db_world + zone_load + weather) + WorldLayerAcyclicity linkcheck`

## Task 6: Docs + finalization

- [ ] Step 1: BUILD.md (rots_world membership; the five-library layering picture; honest deferrals: zone reset half, graph/script/mudlle/shape*, handler/utility pending §7); spec §3/§10 as-built; AGENTS.md one-liner; battery script gains the build-tree pre-clean step 0.
- [ ] Step 2: i386 battery (per-step-resumable script).
- [ ] Step 3: Whole-branch review (most capable model) + fix wave; push; PR against master. Merge = owner's call.

---

## Self-Review Notes

- **Coverage:** every db_world/zone-L/weather undefined from the census is dispositioned (in-lib, downward-resolving, storage-moved, relocated, hooked, or Task-2-adjudicated); the one open fact (txt_block pool) has an explicit pre-check + STOP.
- **Risk register:** (1) boot-path behavior — every task's gates include both boot goldens; the scratch-buffer conversion is the largest mechanical surface and is the same pattern proven in db_players/write_exploits; (2) weather-MSDP relocation moves per-char conditional logic — verbatim lambda bodies + goldens (boot) + the MSDP path is runtime-only (goldens don't exercise MSDP; note: ctest's protocol/weather tests do — verify which tests cover it and cite them in the task report); (3) zone_table storage migrating with the L half — R half + ~6 external TUs extern it (downward once lib exists; plain cross-TU before); (4) linkcheck cascade at Task 5 — STOP contract; (5) container build-tree staleness — battery pre-clean added.
- **Type consistency:** RotS::world, WorldLayerAcyclicity, zone_load.cpp, world_hooks.h, register_boot_shops_hook/register_mudlle_converter_hook/register_weather_msdp_hook used consistently.
