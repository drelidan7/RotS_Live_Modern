# Entity Completion (char_utils weld cuts, ledger zero) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Cut the weld ledger's final five welds so `char_utils.cpp` and `char_utils_combat.cpp` join `rots_entity` — after which `convert_stubs.cpp` is EMPTY and is deleted outright: `rots_convert` becomes `convert_main.cpp` + the four libraries, zero stubs. The persistence boundary stops being "enforced modulo a ledger" and becomes simply real.

**Architecture:** Two instruments, both now twice-proven: verbatim relocation into the layer that owns the concept (fname/other_side → char_utils; the attack_hit_text message table → consts/rots_core), and pre-boot registration inversion for the two genuinely-combat/app edges (wild-fighting attack speed; big-brother PK notification), extending `entity_hooks.h` and the existing registration blocks.

**Tech Stack:** C++20, CMake presets + flat Makefiles, GoogleTest, characterization goldens, whole-archive linkchecks.

## Global Constraints

- **Zero behavior change for `ageland`.** Goldens byte-for-byte; ctest baseline **1274** both hosts (no new tests planned).
- **Branch `arch/entity-complete`, stacked on `arch/persist-split`** (PR #7, CI green, awaiting merge; PR #6 below it). If either PR merges mid-wave, rebase at the next task boundary and note it.
- **Verbatim moves; declarations stay in current headers; external linkage preserved.** Named deviations only, quoted in reports.
- **`rots_convert` keeps linking + ConvertEquivalence 17/17 + smoke 5292/5589 after every task.** Census exit 0.
- **Per-task gates:** macOS `macos-arm64` build + full ctest + native boot golden AND rots64 preset + ctest + boot golden (docker `--pull never`, synchronous). i386 battery at finalization. Top-level `build/` belongs to the i386 container — hands off from the host; never run `make -n test` on the host.
- **Formatter hook:** python byte-edits for existing .cpp/.h (handler.cpp and fight.cpp are mixed-CRLF suspects — byte mode, `git diff --stat` check per file).
- Warnings are errors everywhere. Root Makefile hand-list needs NO change this wave (no new linkcheck target).

---

## Evidence base (verified this session, branch arch/persist-split tip b923e3e)

Fresh `nm -uC` on the two TUs: every undefined symbol already resolves inside {rots_entity, rots_core, rots_platform} EXCEPT exactly five welds:
1. `fname(char*)` — home **handler.cpp:119** (namelist first-name text helper). char_utils caller: `utils::get_object_name`.
2. `other_side(const char_data*, const char_data*)` — home **handler.cpp:137**; body is pure `IS_NPC`/`AFF_CHARM`/`GET_RACE`/`RACE_EAST|MAGI|EVIL|GOOD` macro logic (utils.h:679-682 — pure race-int comparisons, no tables). Sibling `other_side_num(int, int)` at handler.cpp:161 moves with it (same block/logic family). Callers: char_utils (`is_hostile_to`), utility.cpp, pkill.cpp, handler.cpp internals — all legal downward callers post-move.
3. `get_hit_text(int)` — home **fight.cpp** (~line 50211 offset; directly above `dam_message`), pure arithmetic over `attack_hit_text[]` (fight.cpp:3797, `struct attack_hit_type` string-pair table, ~30 rows, zero code deps). Other users: fight.cpp in-file (2 references) and obj2html.cpp:541 (already uses a local `extern`). Both call/extern DOWN once the table + accessor live in consts.cpp (rots_core).
4. `player_spec::wild_fighting_handler` ctor + `get_attack_speed_multiplier()` — the ONLY char_utils use is `get_energy_regen` at **char_utils.cpp:1279-1280** (construct + single query — the exact shape of the weapon_master hook cut last wave).
5. `game_rules::big_brother::on_character_attacked_player` — the ONLY char_utils_combat use is `on_attacked_character` at **char_utils_combat.cpp:104-105** (`big_brother::instance()` + one notification call, guarded by `is_long_anger`).

`char_utils_combat`'s `utils::is_pc`/`is_ridden` refs resolve in char_utils.cpp (they join together). `char_utils`'s `is_room_outside`/`is_light`/`can_wear`/`get_specialization`/`is_npc`/`get_name`/`char_exists` refs already resolve in the entity archive; `get_skill_array`/`get_encumb_table`/`get_leg_encumb_table`/`square_root` in rots_core.

Remaining convert_stubs.cpp inventory (verified): exactly the 5 bodies for welds 1-4 above (fname, other_side, get_hit_text, wild_fighting pair). Weld 5 has no stub (char_utils_combat was never converter-linked). After Tasks 1-2 land the real definitions/hook-defaults, the file is empty → Task 3 deletes it.

---

## Task 1: Relocations (fname + other_side pair → char_utils; attack_hit_text + get_hit_text → consts)

**Files:**
- Modify: `src/handler.cpp` (loses fname, other_side, other_side_num — relocation markers), `src/char_utils.cpp` (receives them), `src/fight.cpp` (loses attack_hit_text[] + get_hit_text; gains `extern struct attack_hit_type attack_hit_text[];` for its in-file readers), `src/consts.cpp` (receives table + accessor)
- Headers: UNCHANGED (declarations already live in handler.h/utils.h/spells.h — verify where each is declared and confirm no change needed; obj2html.cpp's local extern at :541 keeps working)

**Interfaces:**
- PRE-CHECK each body before moving (STOP if wrong): `fname` must be pure text (no world/live-state); `other_side`/`other_side_num` pure macro logic (confirmed above); `attack_hit_text[]`/`get_hit_text` pure data + arithmetic. `get_hit_text` and the table go to consts.cpp because it is message DATA of the same class as the skills[] name column — rots_core is its owning layer; fight.cpp keeps reading `attack_hit_text[]` through a new file-local extern (the one non-verbatim addition; quote it).
- CRITICAL: consts.cpp is in `rots_core` — after this move `rots_core_linkcheck` (CoreLayerAcyclicity) must STILL pass (get_hit_text's only deps are the table + `TYPE_HIT` constant — verify TYPE_HIT's header is core-reachable; if it drags a non-core header, STOP and report).

- [ ] Step 1: Relocate the handler.cpp trio into char_utils.cpp (byte-identity script; handler.cpp is mixed-CRLF — byte-edit).
- [ ] Step 2: Relocate table + accessor into consts.cpp; add fight.cpp's extern; verify obj2html unchanged.
- [ ] Step 3: nm single-definition check; full both-host gates (1274/1274, boot goldens, ConvertEquivalence 17/17, smoke, census 0); all four LayerAcyclicity tests green.
- [ ] Step 4: Commit `refactor: relocate fname/other_side to char_utils, attack_hit_text/get_hit_text to consts (weld cuts)`

## Task 2: The two hooks (wild-fighting attack speed; big-brother PK notification)

**Files:**
- Modify: `src/entity_hooks.h` (two new hook types + setters, matching the existing style incl. `//` responsibility comments), `src/entity_lifecycle.cpp` (hook storage + dispatch helpers, next to the existing two), `src/char_utils.cpp` (:1279-1280 → dispatch call), `src/char_utils_combat.cpp` (:104-105 → dispatch call), `src/wild_fighting_handler.cpp` (+`register_wild_attack_speed_multiplier_hook()`), `src/big_brother.cpp` (+`register_attacked_player_hook()`), `src/comm.cpp` (both registrations in run_the_game() beside the existing six), `src/tests/gtest_main.cpp` (both registrations beside the existing five — the hook-parity lesson from the entity wave's final review is now standing policy)

**Interfaces:**
```cpp
// entity_hooks.h additions (mirror existing style)
namespace rots::entity {
// get_energy_regen()'s wild-fighting attack-speed query (char_utils.cpp).
// wild_fighting_handler.cpp registers the real construct-and-query pre-boot;
// null default = tripwire log + 1.0f (converter: unreachable-by-invariant,
// same class as the weapon-master hook).
using wild_attack_speed_fn = float (*)(const char_data* character);
void set_wild_attack_speed_multiplier_hook(wild_attack_speed_fn hook);

// on_attacked_character()'s PK notification (char_utils_combat.cpp).
// big_brother.cpp registers a forwarder to big_brother::instance().
// on_character_attacked_player() pre-boot; null default = tripwire no-op
// (combat never runs before run_the_game's registrations in ageland).
using attacked_player_fn = void (*)(const char_data* attacker, const char_data* attacked);
void set_attacked_player_hook(attacked_player_fn hook);
}
```
- The two dispatch-site edits are the ONLY non-verbatim code edits (quote before/after). The registered wild-fighting body reproduces char_utils.cpp:1279-1280 exactly (construct `player_spec::wild_fighting_handler handler(const_cast<char_data*>(&character)); return handler.get_attack_speed_multiplier();`). The registered big-brother body reproduces :104-105 (`instance()` + call). Signature note: the dispatch takes `const char_data*` and the registered bodies own any const_cast the originals already performed — no new mutability.

- [ ] Step 1: Hooks + dispatches + registrations (python byte-edits).
- [ ] Step 2: Full both-host gates (goldens are the proof the hooks never fire unregistered in ageland); ConvertEquivalence + smoke; census.
- [ ] Step 3: Commit `refactor: invert wild-fighting speed + big-brother PK edges via entity hooks (char_utils de-welded)`

## Task 3: Membership + LEDGER ZERO (delete convert_stubs.cpp)

**Files:**
- Modify: `src/CMakeLists.txt` (ROTS_ENTITY_SOURCES += char_utils.cpp char_utils_combat.cpp; both leave ROTS_SERVER_SOURCES; ageland_tests inherits via ${ROTS_ENTITY_SOURCES}; rots_convert sources shrink to `convert_main.cpp` ONLY + the four libs; update the rots_convert comment block — the weld-ledger era is over), `src/convert_stubs.cpp` (**DELETED — `git rm`**), flat Makefiles (no structural change — TU .o names already listed; convert_stubs was never in them)
- STOP contract: if `rots_entity_linkcheck` surfaces ANY undefined symbol after the membership change, report symbol + home + options and wait (the census says there are none — but the entity wave's census missed two cascade rounds; expect the possibility).

- [ ] Step 1: Membership + linkcheck; adjudication checkpoint if it fails.
- [ ] Step 2: `git rm src/convert_stubs.cpp`; rots_convert = convert_main.cpp + RotS::persist/entity/core/platform; the converter's link succeeding with zero stubs IS the milestone. Check docs/superpowers/string-view-exceptions.md for convert_stubs rows and delete any (census must stay exit 0).
- [ ] Step 3: Full both-host gates (1274/1274 incl. all four linkchecks, boot goldens, ConvertEquivalence 17/17, smoke 5292/297, census 0).
- [ ] Step 4: Commit `feat: char_utils + char_utils_combat join rots_entity; convert_stubs.cpp deleted — weld ledger ZERO`

## Task 4: Docs + finalization

- [ ] Step 1: docs/BUILD.md ("Library layering": rots_entity's five-TU membership; the ledger-zero milestone — rots_convert now compiles ONE file; remaining spec §3 gaps stated honestly: handler.cpp + utility.cpp still app-compiled with their weld counts); spec §3/§10 as-built (entity membership vs the original six-TU sketch — handler/utility deferred, named welds); AGENTS.md test-total/one-liner touch-ups if needed. convert_stubs' historical role gets one paragraph in BUILD.md (the ledger pattern worked: ~40 entries → 0 across three waves; git history is the archive).
- [ ] Step 2: i386 battery (ps-i386-battery.sh pattern, sequential).
- [ ] Step 3: Whole-branch review (most capable model) + fix wave; push; PR stacked on arch/persist-split. Merge = owner's call.

---

## Self-Review Notes

- **Coverage:** the five welds map 1:1 to Tasks 1-2; membership + deletion is Task 3; §10 step 4's entity library reaches 5 of the spec's 6 sketched TUs (handler.cpp deferred with ~30 welds; utility.cpp deferred — its nm profile is app-wide). The ledger-zero claim is checked by the build itself (no stub TU to hide behind).
- **Placeholder scan:** hook code spelled out; relocation targets have verified homes/line numbers; the one non-verbatim addition (fight.cpp's extern) is named.
- **Risks:** (1) TYPE_HIT header reachability for consts.cpp — pre-checked with a STOP; (2) unknown cascade at Task 3's linkcheck — STOP contract; (3) stacked-branch depth (two unmerged PRs below) — rebase-at-task-boundary rule carried forward; (4) hook parity in tests — registrations added to gtest_main.cpp by construction this time, not as a review finding.
