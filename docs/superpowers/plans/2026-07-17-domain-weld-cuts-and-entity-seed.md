# Domain Weld Cuts + rots_entity Seed Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Execute spec §10 step 4's first slice (docs/superpowers/specs/2026-07-16-library-architecture-design.md): cut the two headline welds — consts.cpp's `skills[]` spell-function-pointer table and the `send_to_char`/`act` output path — then relocate the leaf helpers those cuts unblock, so `consts.cpp` joins `rots_core` and a first `rots_entity` STATIC library (entity_lifecycle + object_utils + environment_utils) stands up with its own acyclicity linkcheck. `convert_stubs.cpp` (the weld ledger) shrinks from ~40 entries to ~15 — the wave's progress metric.

**Architecture:** Three instruments, all precedented in-tree: (1) *boot-time registration* for the spell pointers (mirror `assign_command_pointers()`, db_boot.cpp:396; the commented-out `assign_spell_pointers` hook at db_boot.cpp:176 is the DikuMUD-heritage slot for exactly this); (2) *dependency-inverted sinks* for the output path (mirror the logging seam, spec §13 — low layer owns null-defaulted function pointers, `run_the_game()` registers comm.cpp's verbatim bodies before boot); (3) *verbatim relocation INTO the lower layer* for shared leaf helpers (mirror db-split Task 4b — an upward caller becomes a downward caller when the definition moves down).

**Tech Stack:** C++20, CMake presets + flat Makefiles (4 build systems per new/renamed TU), GoogleTest, characterization goldens, nm-verified linkchecks.

## Global Constraints

- **Zero behavior change for `ageland`.** All goldens byte-for-byte; ctest baseline **1270** on macOS/rots64 (skips differ by platform per AGENTS.md).
- **Verbatim moves.** Relocated functions/globals move with bodies and comments intact. The ONLY sanctioned local deviation is inlining a trivial macro the destination TU must not see (`LOWER`, `CREATE`-expansion, `nz`-style guards) — precedent: logging-seam Task 3 inlined `nz()`; note each instance in the task report.
- **Declared surfaces are stable**: utils.h / comm.h / db.h / handler.h / spells.h keep declaring every moved symbol with identical signatures; external linkage never becomes static. Call sites outside the moved bodies are untouched (except the ≤5 sites this plan names explicitly).
- **Sources stay flat in `src/`** (new TUs: `spell_registry.cpp`? — no: registration lands in existing `spell_pa.cpp`; new files are `output_seam.cpp`, `output_seam.h`, `entity_hooks.h`, `rots_util.cpp`). Flat `src/Makefile` + `src/tests/Makefile` OBJFILES updated for every added TU.
- **rots_convert must keep linking and pass ConvertEquivalence 17/17 after every task** — it is the CI-linked boundary check; several tasks intentionally delete its stubs and link the real definitions instead.
- **Verification cadence per task:** macOS `macos-arm64` build + full ctest + native boot golden AND rots64 build + ctest + boot golden (docker with `--pull never`, WAIT on auto-backgrounded gates). New/rewritten test file → `macos-arm64-asan` run. i386 battery at finalization only.
- **CRLF/format-hook:** python byte-edits for existing files with mixed line endings (notably handler.cpp — see memory: rots-formatter-hook-conflict); new files via Write. Verify no formatter-hook rewrite with a byte-level diff after each edit to handler.cpp.
- Warnings are errors everywhere; `python3 tools/string_view_census.py --check` stays exit 0 after any textual-interface change.
- Branch: `arch/entity-seed` off current master (post-PR #5).

---

## Evidence base (gathered 2026-07-17, this session)

**Output-path census** (call sites per TU): objsave 11 `send_to_char`+33 `act`+2 desclist walks; boards 40+4+2 walks+2 `page_string`; handler 8 stc+2 `vsend_to_char`+21 act+1 walk; mail 1+11+2 `SEND_TO_Q`; utility 3+1+1 walk; db_players 3 stc; char_utils 1 stc; **object_utils / environment_utils / char_utils_combat / entity_lifecycle / pkill: ZERO output-path references.** `act()` is defined in comm.cpp:2427 (not handler.cpp). `descriptor_list` defined comm.cpp:107; NO central header extern — 18 TUs carry file-local externs. `vsend_to_char` (comm.cpp:2131) is vsnprintf-into-2048-buffer + `send_to_char` — fully seam-portable.

**skills[] weld:** `skill_data` at spells.h:356-371; `spell_pointer` type `void (*)(char_data*, char*, int, char_data*, obj_data*, int, int)` — the `ASPELL` signature (spells.h:401-409). consts.cpp:382-634 initializer has **69 non-null pointers**: 34 defined in mystic.cpp, 35 in mage.cpp (NOT spell_pa.cpp — it is a dispatcher). All 8 `.spell_pointer` reader sites are null-guarded (act_othe.cpp:817,837; spell_pa.cpp:492,889,981,1106; limits.cpp:1465-1476; entity_lifecycle.cpp:825-831). All other consumers read pure-data columns only (one benign in-place write: spec_pro.cpp:140 defaults `learn_diff=10`). Per CMakeLists.txt:86-104 + docs/BUILD.md nm evidence, the 69 pointers are consts.cpp's ONLY remaining upward edge.

**nm -uC census of rots_entity candidates** (macos-arm64 objects): `environment_utils` — clean leaf, zero game undefineds. `object_utils` — only `isname_nullable` (home: handler.cpp:314). `entity_lifecycle` — every undefined is either consts data (fixed by Task 2), a relocatable leaf helper (homes below), `_buf`/`_top_idnum`/`pulse-via-get_current_time_phase`, or the two genuine inversion targets (`clear_account_backed_object_bytes_for_character` → objsave.cpp; `player_spec::weapon_master_handler` ctor/`get_attack_speed_multiplier` → wild_fighting_handler.cpp, called at entity_lifecycle.cpp:619-620). `char_utils` and `char_utils_combat` have real combat/anti-cheat welds (`get_hit_text`→fight.cpp, `wild_fighting_handler`, `other_side`→handler.cpp:137, `big_brother::on_character_attacked_player`) — **both deferred; not in this wave's library.**

**Verified homes for relocation targets:** `get_encumb_table`/`get_leg_encumb_table` consts.cpp:1831/1851 (Task 2 fixes). `set_title` limits.cpp:374 (pure char_data + CREATE/RELEASE + std::format + `READ_TITLE`). `isname_nullable` handler.cpp:314. `log`/`mudlog` utility.cpp:1242/1247 (already thin platform-seam forwarders). `str_dup` utility.cpp:989; `str_cmp`/`str_cmp_nullable` utility.cpp:1134/1158. `number` family + `rots_test_random_hook` utility.cpp:906-970 (hook is the cross-platform replacement for the old `--wrap` seam — CMakeLists.txt:620-624; keep mechanism identical). `pulse` comm.cpp:783. `top_idnum` db_players.cpp (extern'd at entity_lifecycle.cpp:108). Pool helpers + `affected_list`/`affected_list_pool` + `char_exists` trio + `char_control_array`: handler.cpp. `class_HP`: profs.cpp. `get_race_weight`/`get_race_height`, `pool_to_list`/`from_list_to_pool` + `universal_list_counter`/`used_in_universal_list`, `get_current_time_phase`, `rots_remove`/`rots_rename_replace`: utility.cpp. Specialization accessors `utils::get_specialization`/`set_specialization`/`specialization_data::reset`/`get_minimum_insight_perception`: char_utils.cpp:1322/1331/1478/1227.

---

## Task 1: Cut the `skills[]` function-pointer weld (registration scheme)

**Files:**
- Modify: `src/consts.cpp` (skills[] initializer: 69 pointer cells → `nullptr`)
- Modify: `src/spell_pa.cpp` (add `assign_spell_pointers()` at end of file)
- Modify: `src/spells.h` (declare `void assign_spell_pointers();` near the ASPELL block)
- Modify: `src/db_boot.cpp` (call it at the `assign_command_pointers();` site, db_boot.cpp:396; delete the stale commented decl at :176)
- Test: `src/tests/spell_registry_tests.cpp` (new), wired into `src/CMakeLists.txt` ROTS_TEST_SOURCES + `src/tests/Makefile`

**Interfaces:**
- Consumes: the existing `skills[MAX_SKILLS]` global (consts.cpp), `ASPELL(spell_*)` declarations (spells.h).
- Produces: `void assign_spell_pointers();` — after it runs, `skills[i].spell_pointer` is byte-identical to today's static initializer for all 256 indices. Pre-registration reads hit the same null-guards that already protect non-spell rows today.

- [ ] **Step 1: Write the failing test.** `spell_registry_tests.cpp`: a positional expectation table of all 69 `{index, name, &spell_fn}` triples (extract mechanically from consts.cpp:382-634 — row N of the initializer IS skill index N; cross-check a handful against spells.h constants, e.g. `SPELL_DETECT_HIDDEN`=41 ↔ row 41 "detect hidden"). Test A: call `assign_spell_pointers()`, then for each triple assert `skills[i].spell_pointer == fn` AND `strcmp(skills[i].name, name) == 0` (the name column is independent data — it catches an off-by-one that a pointer-only check derived from the same extraction script would not). Test B: assert every OTHER index has `spell_pointer == nullptr`. Build the expectation table by hand-transcribing from consts.cpp BEFORE Step 3 nulls it (the current file is the golden source).
- [ ] **Step 2: Run the test — expect FAIL** (`assign_spell_pointers` undefined).
- [ ] **Step 3: Implement.** In `spell_pa.cpp`: `assign_spell_pointers()` = 69 assignments `skills[<index>].spell_pointer = spell_<name>;` in table order, each with the skill-name comment. In `consts.cpp`: replace each of the 69 `spell_*` cells with `nullptr` + a one-line column comment ("populated at boot by assign_spell_pointers(), spell_pa.cpp — see CMakeLists ROTS_CORE_SOURCES"). In `db_boot.cpp`: add `assign_spell_pointers();` directly before `assign_command_pointers();` (line ~396); remove the dead comment at :176. Declare in spells.h.
- [ ] **Step 4: Run the new test — expect PASS.** Then full both-host gates (ctest 1270+new, boot goldens both). CharacterizationCombat suites are the behavior check for cast paths. New test file → `macos-arm64-asan` configure/build/ctest.
- [ ] **Step 5: Commit** `refactor: skills[] spell pointers populated at boot (assign_spell_pointers) — consts.cpp now pure data`

## Task 2: `consts.cpp` joins `rots_core`; delete the converter's consts-data stubs

**Files:**
- Modify: `src/CMakeLists.txt` (move `consts.cpp` from ROTS_SERVER_SOURCES → ROTS_CORE_SOURCES; rewrite the 86-104 caveat comment to record the weld cut)
- Modify: `src/convert_stubs.cpp` (delete stubs now resolved by the real consts.cpp via RotS::core)
- Flat Makefiles: no change (consts.o already in OBJFILES; the flat build has no libraries)

**Interfaces:**
- Consumes: Task 1 (consts.cpp must have zero upward references — `rots_core_linkcheck` is the proof).
- Produces: `rots_core = {config.cpp, consts.cpp}`; rots_convert now links real `skills[]` (all-null pointers — it never calls `assign_spell_pointers()`, preserving today's stub contract exactly), `race_affect`, `max_race_str`, `get_skill_array`, `get_encumb_table`/`get_leg_encumb_table`, `language_number`/`language_skills`, `race_abbrevs`, `square_root`, `global_release_flag`.

- [ ] **Step 1:** Move `consts.cpp` into ROTS_CORE_SOURCES; update the comment block (the §3 caveat fired-and-resolved story). Build macos-arm64; `CoreLayerAcyclicity` (rots_core_linkcheck) must pass — this is the nm-verified acceptance test for Task 1's cut.
- [ ] **Step 2:** Delete from `convert_stubs.cpp`: `race_affect[]`, `max_race_str[]`, `skills[]` + `kSkillNames` + `get_skill_array()`, `language_number`/`language_skills`, `race_abbrevs[]`, `square_root[]`, `global_release_flag`, `create_function`?? — NO (utility.cpp home, Task 4), `get_encumb_table`/`get_leg_encumb_table` stubs. Update the file-header ledger note and the `recalc_skills` stub comment (real skills[] data is now linked; its knowledge-recomputation omission stays valid — spec_pro.cpp still not linked — but the "not available here" rationale changes to "assign_spell_pointers never runs here").
- [ ] **Step 3:** Full both-host gates + **ConvertEquivalence 17/17** + a functional re-smoke (`rots_convert --lib lib --dry-run` conversion count identical to the db-split wave's recorded 5292/5589).
- [ ] **Step 4: Commit** `refactor: consts.cpp joins rots_core (skills[] weld cut); converter links real const tables`

## Task 3: The output seam (`send_to_char`/`act` dependency inversion)

**Files:**
- Create: `src/output_seam.h`, `src/output_seam.cpp` (goes in ROTS_CORE_SOURCES — it forwards opaque pointers, never dereferences game types)
- Modify: `src/comm.cpp` (bodies → registered impls; `register_game_output_sinks()`)
- Modify: `src/comm.h` (declare `register_game_output_sinks()` next to `register_mudlog_broadcast_sink`)
- Modify: `src/convert_stubs.cpp` (delete 6 stubs)
- Modify: `src/CMakeLists.txt` (ROTS_CORE_SOURCES), `src/Makefile` + `src/tests/Makefile` (output_seam.o)

**Interfaces:**
- Produces (`output_seam.h`; plain function pointers — these are hot paths, no std::function):

```cpp
struct char_data;
struct obj_data;

namespace rots::output {
// One sink per output entry point the lower layers call. Null until the app
// layer registers (register_game_output_sinks(), comm.cpp) — a null sink is a
// logged no-op, matching the converter's historical stub semantics; in ageland
// registration precedes boot_db(), so the default never fires there.
using send_to_char_fn = void (*)(std::string_view message, char_data* character);
using send_to_char_id_fn = void (*)(std::string_view message, int character_id);
using act_fn = void (*)(std::string_view str, int hide_invisible, char_data* ch,
    obj_data* obj, void* vict_obj, int type, char spam_only);
using mage_roster_fn = void (*)(char_data* mage);

struct Sinks {
    send_to_char_fn send_to_char;       // comm.cpp's desc-delivery body
    send_to_char_id_fn send_to_char_id; // comm.cpp's descriptor_list-walk body
    act_fn act;                         // comm.cpp:2427's body
    mage_roster_fn track_mage;          // comm.cpp:677's body
    mage_roster_fn untrack_mage;        // comm.cpp's untrack body
};
void set_sinks(const Sinks& sinks); // registered once, before boot
}
```

- `output_seam.cpp` defines the five GLOBAL symbols with comm.h's exact signatures — `send_to_char(std::string_view, char_data*)`, `send_to_char(std::string_view, int)`, `act(...)`, `track_specialized_mage`, `untrack_specialized_mage` — each forwarding to its sink, or `rots::log::write_stderr` tripwire + no-op when null. It ALSO hosts `vsend_to_char` moved verbatim from comm.cpp:2131 (vsnprintf + `send_to_char` — no sink needed of its own). It includes `comm.h` so signature drift is a compile error.

- [ ] **Step 1:** Create header + TU per the spec above. In comm.cpp: rename the five bodies to file-scope statics (`send_to_char_impl`, `send_to_char_id_impl`, `act_impl`, `track_specialized_mage_impl`, `untrack_specialized_mage_impl`) — bodies verbatim, only the name line changes; delete `vsend_to_char` from comm.cpp (moved). Add `register_game_output_sinks()` filling a `Sinks` aggregate with the five impls; call it in `run_the_game()` immediately after `register_mudlog_broadcast_sink()` (comm.cpp:610 area — before boot_db, so no unregistered window exists). Internal comm.cpp callers keep calling the public names (now forwarders) — zero call-site churn.
- [ ] **Step 2:** Wire the new TU into all four build systems.
- [ ] **Step 3:** Delete from `convert_stubs.cpp`: both `send_to_char` overloads, `vsend_to_char`, `act`, `track_specialized_mage`, `untrack_specialized_mage` (the seam TU now provides tripwire-logged defaults with identical semantics — note this in the ledger header).
- [ ] **Step 4:** Full both-host gates (1270+ / boot goldens — boot + Characterization exercise the forwarders on every message) + ConvertEquivalence 17/17. `rots_core_linkcheck` must still pass (the seam TU's only outward needs are rots::log + libc — verify via the linkcheck, not by assertion).
- [ ] **Step 5: Commit** `refactor: send_to_char/act output seam — comm.cpp registers sinks at boot (spec §13 pattern)`

## Task 4: Platform-helper relocations (log/mudlog, allocators, text, RNG)

**Files:**
- Modify: `src/rots_log.cpp` (+`log`, `mudlog` — moved from utility.cpp:1242/1247)
- Create: `src/rots_util.cpp` (new rots_platform TU)
- Modify: `src/utility.cpp` (bodies removed), `src/comm.cpp` (nothing), `src/CMakeLists.txt` (ROTS_PLATFORM_SOURCES), `src/Makefile`/`src/tests/Makefile` (rots_util.o), `src/convert_stubs.cpp` (delete 10 stubs)

**Interfaces:**
- Produces in `rots_util.cpp` (utils.h keeps declaring all of them — precedent: vmudlog already lives in rots_log.cpp while declared elsewhere): `create_function`, `free_function`, `str_dup`, `str_cmp`, `str_cmp_nullable`, `rots_remove`, `rots_rename_replace`, `number(int,int)` + the double-hook variant + `double (*rots_test_random_hook)()` (utility.cpp:906-970, moved as a family; the TESTING queue in test_random_utils.cpp keeps working unchanged).
- NOT moved (deliberately): `file_to_string`/`file_to_string_alloc` (db_boot.cpp; they'd drag `MAX_STRING_LENGTH` into L0 — their converter stubs stay), `fname`/`other_side`/`unaccent` (game-semantics text, stays put).

- [ ] **Step 1:** Move `log`/`mudlog` into rots_log.cpp verbatim (they are already pure `rots::log::write*` forwarders). Inline-expand `LOWER` in the moved `str_cmp`/`str_cmp_nullable` and the `CREATE` expansion in `str_dup` (each with the sanctioned-deviation note). Byte-identity check per moved body (the wave's established python script method).
- [ ] **Step 2:** Build systems ×4; **`PlatformLayerAcyclicity` (rots_platform_linkcheck) must pass** — the L0 archive must stay self-contained with the new TU (this is the task's real acceptance test; `rots_test_random_hook` and `printf`/`calloc` are libc-clean, `rots_remove`'s Win32 branches compile under `_WIN32` only).
- [ ] **Step 3:** Delete from `convert_stubs.cpp`: `log`, `mudlog`, `create_function`, `free_function`, `str_dup`, `str_cmp`, `str_cmp_nullable`, `rots_remove`, `rots_rename_replace`, `number`. (The real definitions now arrive via RotS::platform.)
- [ ] **Step 4:** Full both-host gates + ConvertEquivalence 17/17 + census check.
- [ ] **Step 5: Commit** `refactor: relocate platform-pure helpers into rots_platform (rots_util.cpp; log/mudlog join rots_log)`

## Task 5: Entity-helper relocations + the two inversion hooks

**Files:**
- Create: `src/entity_hooks.h` (flat header; pathed relocation deferred with §5e)
- Modify: `src/entity_lifecycle.cpp` (receives relocations; hook plumbing; two local fixes), `src/handler.cpp` (loses pool allocator + lists + char_exists trio + array + `isname_nullable`), `src/utility.cpp` (loses list helpers + counters + race h/w + `get_current_time_phase`), `src/profs.cpp` (loses `class_HP`), `src/limits.cpp` (loses `set_title`), `src/char_utils.cpp` (loses the 4 specialization accessors), `src/comm.cpp` (loses `int pulse` definition), `src/db_players.cpp` (loses `top_idnum` definition; buf/buf1 → std::format locals at the 2 sites), `src/objsave.cpp` + `src/wild_fighting_handler.cpp` (register hooks), `src/convert_stubs.cpp` (delete ~16 entries)
- **handler.cpp edits via python byte-edits (mixed CRLF — formatter-hook hazard).**

**Interfaces:**
- Relocations INTO `entity_lifecycle.cpp` (verbatim; origin TUs keep calling down; all declarations stay in their current headers): `get_from_affected_type_pool`/`put_to_affected_type_pool` + `affected_list`/`affected_list_pool` (handler.cpp — note: the converter's simplified allocator is REPLACED by the real free-list one; content-identical output, ledger entry closes), `char_exists`/`set_char_exists`/`remove_char_exists` + `char_control_array` (handler.cpp), `isname_nullable` (handler.cpp:314 — object_utils' one edge), `pool_to_list`/`from_list_to_pool` + `universal_list_counter`/`used_in_universal_list` (utility.cpp), `get_race_weight`/`get_race_height` (utility.cpp), `get_current_time_phase` (utility.cpp:184) **+ the `int pulse` definition** (comm.cpp:783 — definition-only move; comm.cpp increments via extern; storage-placement precedent: `world_room_vnum` seam), `class_HP` (profs.cpp — preserve its current strong-definition linkage; see db-split Task 4b's IFNDR history), `set_title` (limits.cpp:374), `utils::get_specialization`/`utils::set_specialization`/`specialization_data::reset`/`utils::get_minimum_insight_perception` (char_utils.cpp — `set_specialization`'s `track/untrack_specialized_mage` calls now resolve DOWNWARD into Task 3's seam TU), **`long top_idnum` definition** (db_players.cpp → entity_lifecycle.cpp, same storage-placement rationale; db_players externs it).
- Produces (`entity_hooks.h`):

```cpp
struct char_data;

namespace rots::entity {
// Teardown notification: free_char() (entity_lifecycle.cpp) fires this for
// every character destroyed. objsave.cpp registers
// clear_account_backed_object_bytes_for_character at boot; null default is a
// provable no-op (the staged-object map can only gain entries via the login
// flow, which never runs before registration — see the former ledger entry).
using char_teardown_fn = void (*)(const char_data* character);
void set_char_teardown_hook(char_teardown_fn hook);

// recalc_abilities()' weapon branch (entity_lifecycle.cpp:619-620) queries the
// weapon-master attack-speed multiplier. wild_fighting_handler.cpp registers
// the real player_spec::weapon_master_handler-backed impl in run_the_game()
// BEFORE boot_db(), so ageland has no unregistered window; the null default
// (tripwire log + 1.0f) reproduces the converter's stub exactly (equipment is
// always null there, so the branch is unreachable anyway).
using attack_speed_fn = float (*)(char_data* character);
void set_attack_speed_multiplier_hook(attack_speed_fn hook);
}
```

with the two hook globals + null-default dispatch helpers defined in entity_lifecycle.cpp; `free_char`'s direct `clear_account_backed_object_bytes_for_character(...)` call and lines 619-620's `weapon_master_handler` construction are replaced by hook dispatch (2 call sites — the only non-verbatim edits, quoted in the report). Registration: `objsave.cpp` gains `register_char_teardown_hook()`, `wild_fighting_handler.cpp` gains `register_attack_speed_multiplier_hook()`, both declared in entity_hooks.h-adjacent headers they already own and called from `run_the_game()` next to `register_game_output_sinks()`.
- Local fixes: entity_lifecycle.cpp:583-584 `strcpy(buf, "SYSERR: 0 weight weapon"); mudlog(buf, ...)` → `mudlog("SYSERR: 0 weight weapon", NRM, LEVEL_GOD, TRUE);` (drops the `_buf` edge). db_players.cpp's two scratch-buffer uses (`save_char`'s fallback log via `buf`, `rename_char`'s paths via `buf1`) → local `std::string`/`std::format` composition (both functions already use std::format; ledger entry's recorded follow-on) — deletes the `buf`/`buf1` stub.

- [ ] **Step 1:** Relocate the verbatim list (byte-identity script per body; `nm` single-definition check across all objects after — no symbol defined twice, none lost).
- [ ] **Step 2:** `entity_hooks.h` + the two dispatch-site edits + registrations in run_the_game. STOP condition: if `set_title` or the specialization accessors drag an unexpected dependency (e.g. `READ_TITLE` reaching a non-consts table), report and fall back to a hook for that symbol rather than forcing the move.
- [ ] **Step 3:** Delete from `convert_stubs.cpp`: pool allocator pair, `affected_list`/`affected_list_pool`/`universal_list_counter`/`used_in_universal_list`, `pool_to_list`/`from_list_to_pool`, `class_HP`, `get_race_weight`/`get_race_height`, `get_current_time_phase` (real one now links; pulse stays 0 in the converter → returns 0, deterministic as before — note in ledger), `char_exists` trio + array, `set_title`, `isname_nullable`, `clear_account_backed_object_bytes_for_character`, `player_spec::weapon_master_handler` ctor + `get_attack_speed_multiplier`, `buf`/`buf1`.
- [ ] **Step 4:** Full both-host gates + ConvertEquivalence 17/17 + functional re-smoke (identical conversion counts). Goldens are the behavior proof for the hook edits (recalc_abilities feeds persisted derived stats — CharacterizationJson covers it).
- [ ] **Step 5: Commit** `refactor: relocate entity-tier leaf helpers into entity_lifecycle; invert objsave/combat edges via entity_hooks`

## Task 6: Stand up `rots_entity` (L2) + linkcheck; converter links it

**Files:**
- Modify: `src/CMakeLists.txt` (ROTS_ENTITY_SOURCES `{entity_lifecycle.cpp, object_utils.cpp, environment_utils.cpp}`; `add_library(rots_entity STATIC ...)` + `RotS::entity` alias, PUBLIC rots_build_flags + RotS::core + RotS::platform; ageland links RotS::entity; the three TUs leave ROTS_SERVER_SOURCES; ageland_tests compiles `${ROTS_ENTITY_SOURCES}` directly per the platform/core precedent; `rots_entity_linkcheck` + `EntityLayerAcyclicity` ctest mirroring the core linkcheck but linking `rots_entity + rots_core + rots_platform` whole-archive; rots_convert: drop `entity_lifecycle.cpp`/`object_utils.cpp` from its source list, link `RotS::entity`)
- Create: `src/tests/entity_linkcheck_main.cpp` (mirror core_linkcheck_main.cpp)
- Modify: `src/convert_stubs.cpp` (delete `utils::is_room_outside`/`utils::is_light` — environment_utils' real definitions now arrive via RotS::entity)
- Flat Makefiles: no structural change (no libraries there; the three .o names already listed)

**Interfaces:**
- Consumes: Tasks 2-5 (every formerly-undefined symbol in the three TUs now resolves inside the archive, from rots_core/rots_platform, or through a null-defaulted hook).
- Produces: `RotS::entity`; `EntityLayerAcyclicity` CI test; rots_convert's direct-compile list shrinks to `{convert_main, convert_stubs, db_players, character_json, objects_json, exploits_json, account_management, account_cache, convert_exploits, convert_plrobjs, char_utils}` (char_utils stays direct — its combat welds are next wave's work).

- [ ] **Step 1:** CMake target + linkcheck; build. If nm surfaces a residual undefined symbol this plan missed, STOP: report it with its home TU and disposition options (relocate vs hook vs defer that TU) — do not silently stub.
- [ ] **Step 2:** rots_convert re-plumb + stub deletions; ConvertEquivalence 17/17.
- [ ] **Step 3:** Full both-host gates (ctest incl. the new EntityLayerAcyclicity; boot goldens both).
- [ ] **Step 4: Commit** `feat: rots_entity static library (entity_lifecycle + object_utils + environment_utils) + acyclicity linkcheck`

## Task 7: Docs + finalization

- [ ] **Step 1:** docs/BUILD.md ("Library layering": rots_entity, the two new linkchecks' meaning, the seam/hook inventory); spec §3/§10 as-built notes (consts.cpp caveat resolved — rewrite the §3 caveat bullet; step-4 first-slice membership honestly recorded: char_utils/char_utils_combat/handler deferred with their nm-evidenced welds named); convert_stubs.cpp header updated to reflect the shrunken ledger; AGENTS.md build-section one-liner if warranted.
- [ ] **Step 2:** Finalization battery (sequential, per AGENTS.local.md): i386 `make test`; monolithic runner from `/rots/src/tests` (clean rebuild); i386 boot golden; flat i386 `make all`; `linux-x86-legacy` preset (incl. rots_convert + both linkchecks).
- [ ] **Step 3:** Whole-branch review; push; PR (six blocking CI jobs). Merge = owner's call.

---

## Self-Review Notes

- **Spec coverage:** §10 step 4 first slice ✓ (rots_entity stood up; welds cut as the link graph surfaced them); §3 caveat (skills[]) resolved ✓ Task 1-2; §13's seam pattern reused ✓ Task 3; §11 enforcement extended (EntityLayerAcyclicity) ✓ Task 6. Deferred honestly: char_utils/char_utils_combat/handler membership, objsave/boards/mail/pkill → rots_persist (their heavy output welds are now cuttable with Task 3's seam, but that's the next wave).
- **Placeholder scan:** hook/seam code is spelled out; relocation lists are exhaustive with verified homes; the two non-verbatim dispatch edits are named with line numbers. The 69-triple registration table is extraction-work by construction (source = current consts.cpp), with the name-column cross-check guarding transcription.
- **Type consistency:** `rots::output::Sinks`, `rots::entity::set_char_teardown_hook`/`set_attack_speed_multiplier_hook`, `assign_spell_pointers`, target names `rots_entity`/`RotS::entity`, test names `EntityLayerAcyclicity` used consistently across tasks.
- **Ordering rationale:** consts→core (T2) must precede the entity archive (T6) because entity_lifecycle references `skills[]`/`race_affect`/`max_race_str`; the output seam (T3) must precede the specialization-accessor move (T5) because `set_specialization` calls the mage-roster functions; platform moves (T4) must precede T6 because entity_lifecycle calls `number`/`str_dup`-family helpers.
- **Risk register:** (1) unregistered-window behavior — every registration happens in `run_the_game()` before `boot_db()`, and every dispatch site is null-guarded with semantics identical to today's converter stubs; (2) handler.cpp CRLF/formatter hazard — byte-edit method mandated; (3) `spec_pro.cpp:140`'s in-place `learn_diff` write means `skills[]` must stay non-const — do NOT const-qualify it while moving consts.cpp; (4) hot-path overhead — sinks are plain function pointers, one indirect call per message, no allocation.
