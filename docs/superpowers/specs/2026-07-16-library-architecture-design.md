# Library Architecture & Data-Model Decoupling — Design

**Date:** 2026-07-16
**Status:** Draft for review
**Scope:** A target architecture that carves the flat `src/` tree into a layered set of
static libraries, splits the god-headers, and decouples the core entity structs from the
relationships (location, session, account) currently baked into them.

---

## 1. Goals

1. **Findability** — make it obvious where combat, persistence, world-building, networking,
   commands, and accounts live.
2. **Explicit dependencies** — express the dependency graph as CMake link edges so new tangles
   are caught at build time, and the graph stays acyclic across layers.
3. **Smaller recompiles** — especially on the slow QEMU i386 toolchain. This is gated **more by
   the god-headers than by library boundaries**: touching `structs.h` today recompiles 75 files
   regardless of how the objects are grouped. The header split (Section 5) is the actual lever;
   libraries then enforce the result.
4. **One-library-at-a-time refactoring** — carve units that can be modernized in isolation with
   their own tests, without destabilizing the rest.
5. **Split the god-headers** — a first-class goal, not a side effect. `structs.h` (2302 lines,
   included by 75 files) and `utils.h` (234 `#define`s, included by 64 files) are the two headers
   whose coupling most limits every goal above.

### Non-goals (for the first implementation waves)

- Rewriting game logic or changing observable behavior. Characterization goldens
  (`src/tests/goldens/`, `scripts/boot-golden.sh`) must stay byte-for-byte green throughout.
- Migrating the retained binary/legacy data formats. The i386 container remains the canonical
  shipping ABI and legacy-format guard.
- Completing the macro→function and location-representation migrations in one pass — both are
  long-tail, staged efforts (Sections 7–8).

---

## 2. Current state (from dependency analysis)

- ~115K LOC, ~130 files, **one flat `src/`**, linked as loose `.o` files straight into `ageland`
  by both `src/Makefile` (OBJFILES) and `src/CMakeLists.txt`. No static libraries exist today.
- **God-headers / fan-in:** `structs.h` 75, `utils.h` 64, `db.h` 58, `comm.h` 57, `platdef.h` 53,
  `interpre.h` 52, `handler.h` 49, `char_utils.h` 36, `spells.h` 35.
- **`structs.h` holds the entire data model** — all 70 struct/class/union types
  (`obj_data`, `room_data`, `char_data`, `descriptor_data`, `char_file_u`, …) in one header.
- **A genuine foundation layer exists** — 7 translation units with zero data-model coupling
  (`rots_net`, `rots_crypt`, `rots_rng`, `clock`, `json_utils`, `crashsave_schedule`,
  `player_file_finalize`).
- **Tangled hubs:** `db.cpp` (5798 lines — world-load + player-load + boot orchestration + account
  glue) and `interpre.cpp` (command dispatch reaching *up* into migration tools and dev
  benchmarks).

Key structural finding used throughout this design: **characters and objects depend on rooms only
by an integer index (`in_room`) plus an intrusive-list convention — no struct embeds a `room_data*`
except `room_data` itself.** The room→{char,obj} ownership is the only type-level edge. The coupling
to remove is therefore *behavioral*, not structural.

---

## 3. Target library architecture

Eight static libraries in strict acyclic layers (each depends only downward), plus two executables.

```
┌─ L5  rots_app        → the ageland executable (server loop, session I/O)
├─ L4  rots_commands   → interpreter + all player commands
├─ L3  rots_combat   rots_world   rots_persist    (three peer domain libs)
├─ L2  rots_entity     → entity/relationship operations over the data model
├─ L1  rots_core       → the (split-up) data model + const tables
└─ L0  rots_platform   → OS/infra, zero game coupling
```

| Lib | Layer | TUs | Contents |
|---|---|---|---|
| `rots_platform` | L0 | 7 | `rots_net, rots_crypt, rots_rng, clock, crashsave_schedule, json_utils, player_file_finalize` (verified clean leaves — the archive imports only libc/libstdc++/compiler symbols) |
| `rots_core` | L1 | 2 + split headers | `consts, config` + the carved-up data model (Section 5) (as-built: `consts.cpp` and `output_seam.cpp` joined `rots_core` in the entity-seed wave — see the resolved caveat below) |
| `rots_entity` | L2 | 6 | `char_utils, object_utils, environment_utils, handler, utility, char_utils_combat` (as-built: 5 of 6 have joined `rots_entity` — `handler`/`utility` remain app-compiled, deferred with named/uncounted welds respectively — see caveat below) |
| `rots_persist` | L3 | ~14 | `db_players` (from `db.cpp`), `objsave, boards, mail, pkill, character_json, objects_json, exploits_json, account_management (+6 #included fragments), account_cache, convert_exploits, convert_plrobjs, save_benchmark, savebench` |
| `rots_world` | L3 | ~15 | `db_world` (from `db.cpp`), `shapemdl, shapemob, shapeobj, shaperom, shapescript, shapezon, zone, script, mudlle, mudlle2, graph, weather, mob_csv_extract, obj2html` (as-built: 3 of ~15 have joined `rots_world` — `db_world`/`weather`/the new `zone_load` (carved out of `zone`) — `shape*`/`script`/`mudlle`/`mudlle2`/`graph`/`mob_csv_extract`/`obj2html`/`zone`'s reset half remain app-compiled, deferred — see caveat below) |
| `rots_combat` | L3 | 16 | `fight, limits, skill_timer, mobact, ranger, clerics, mage, mystic, profs, spell_pa, spec_pro, spec_ass, battle_mage_handler, weapon_master_handler, wild_fighting_handler, olog_hai` (as-built: 4 of 16 have joined `rots_combat` — `skill_timer`/`battle_mage_handler`/`weapon_master_handler`/`wild_fighting_handler` — `profs` is caveated SEED-WITH-SEAM and the remaining 11 (`fight, limits, mobact, ranger, clerics, mage, mystic, spell_pa, spec_pro, spec_ass, olog_hai`) DEFER, all still app-compiled; the blocker-buster wave added two further TUs NOT in this row's original 16-TU sketch — `combat_hooks.cpp`/`visibility.cpp`, enabler infrastructure rather than DEFER-11 migrations, bringing `rots_combat` to 6 TUs total — see caveat below) |
| `rots_commands` | L4 | 15 | `interpre, act_comm, act_info, act_move, act_obj1, act_obj2, act_offe, act_othe, act_soci, act_wiz, modify, delayed_command_interpreter, wait_functions, shop, ban` |
| `rots_app` | L5 | 6 | `comm, protocol, color, big_brother, signals, db_boot` (from `db.cpp`); `signals.cpp` calls up into game/session state (`descriptor_list`, `hupsig`, `unrestrict_game`) so it is app-layer, not foundation |

**Notes and honest caveats:**

- The three L3 libraries are a **peer tier**, not a sub-stack: `rots_persist`/`rots_world`/
  `rots_combat` may cross-reference within L3 (e.g. `db_players` needs world helpers). Treat L3 as
  one layer until `db.cpp` is fully split; do not pretend one L3 lib sits strictly below another.
- `olog_hai` (logging) and `big_brother` (anti-cheat) are cross-cutting; they are parked where
  their current dependencies point and are candidates to move once boundaries settle.
- The 6 `account_management_*.cpp` files are `#include`d into `account_management.cpp` (they are
  fragments, not separately compiled TUs) and stay together in `rots_persist`.
- `safe_template.cpp` **rejoined `rots_platform` as a clean L0 leaf** once the platform-level
  logging seam (Section 13) landed: `vmudlog`/`BRF` now resolve inside `rots_platform` itself
  (`rots_log.cpp`), and its one null-arg guard (formerly `utils.h`'s `nz`) is inlined at the call
  site, so the TU's only pathed `rots/` include is `rots/platform/log.h` — no `core/include` reach, no qualifier
  needed. Confirmed by `nm` on the built archive (`rots_platform_linkcheck` / `PlatformLayerAcyclicity`).
- **Resolved (entity-seed Tasks 1-2).** `consts.cpp` has joined `rots_core`. Its `skills[MAX_SKILLS]`
  table used to structurally embed ~69 `spell_*` function pointers, an `nm`-verified upward edge
  into `rots_combat`-tier code (a genuine L1→L3 reference, not just a link-time weld) — confirmed
  by `nm -uC` on the built `consts.cpp.o`, which resolved every one of those symbols as undefined.
  `get_guardian_type`'s separate `mob_index` edge had already been cut ahead of this wave
  (relocated to `utility.cpp`). Task 1 cut the `skills[]` weld at the root with a **registration
  scheme**: `consts.cpp`'s static initializer now leaves the function-pointer column null, and
  `assign_spell_pointers()` (`spell_pa.cpp`) populates all 69 cells at boot — called from
  `db_boot.cpp` immediately before `assign_command_pointers()`, the same boot-time-registration
  precedent that function already established. `SpellRegistry.*`
  (`src/tests/spell_registry_tests.cpp`) pins all 69 `{index, name, function}` triples against a
  positional transcription of the pre-change table (independently cross-checked against
  `spells.h`'s `SPELL_*` constants), asserting both the pointer AND the name column so an
  off-by-one in either extraction is caught. Task 2 then moved `consts.cpp` itself into
  `ROTS_CORE_SOURCES`. See `docs/BUILD.md` "Library layering" for the full `nm` evidence and the
  registration call site.
- **Resolved (persist-split wave).** `rots_persist` is stood up: 14 TUs (`db_players.cpp`,
  `character_json.cpp`, `objects_json.cpp`, `exploits_json.cpp`, `account_management.cpp` + its
  six `#include`d fragments, `account_cache.cpp`, `obj_files.cpp`, `pkill_json.cpp`,
  `mail_json.cpp`, `boards_json.cpp`, `convert_exploits.cpp`, `convert_plrobjs.cpp`,
  `color_convert.cpp`, `save_benchmark.cpp`) built as `librots_persist.a`/`RotS::persist`, with
  the `PersistLayerAcyclicity` linkcheck (§11) proving no upward edge beyond `rots_entity`/
  `rots_core`/`rots_platform` (ctest 1273→1274). It is narrower than the row above in two ways
  the row's aspirational framing didn't distinguish: (1) `objsave.cpp`/`boards.cpp`/`mail.cpp`/
  `pkill.cpp` contributed only their pure-codec halves (new TUs `obj_files.cpp`/`boards_json.cpp`/
  `mail_json.cpp`/`pkill_json.cpp`) — their runtime/bridge/gameplay halves stay app-compiled in
  `ROTS_SERVER_SOURCES`, unmoved; (2) `savebench.cpp` deferred entirely (`page_string()`,
  `modify.cpp`/app layer — not `nm`-clean), while its sibling `save_benchmark.cpp` did join. See
  `docs/BUILD.md`'s "`rots_persist`" section for the full carve/relocation/hook inventory. The
  **L3 peer-tier caveat above still holds**: `rots_world`/`rots_combat` remain entirely
  app-compiled (`db_world.cpp`, `fight.cpp`, and the rest of the L3 table's other two rows are
  still `ROTS_SERVER_SOURCES` members, not archived) — `rots_persist` is the first of the three
  peer libraries to actually exist as a build target, not proof the peer tier itself is done. The
  only two L3(persist)→app edges this wave found (`db_players.cpp`'s calls into `db_world.cpp`/
  `db_boot.cpp`) were cut via `persist_hooks.h`'s pre-boot-registered hook pair
  (`world_room_vnum`/`add_exploit_record`), the same dependency-inversion pattern §13 already used
  for `rots_entity`'s remaining edges — no L3→app edge was hidden or left unenforced, just
  inverted.
- **Resolved, partially (entity-completion wave).** `rots_entity` grows from 3 TUs to **5 of the
  row's original 6**: `char_utils.cpp` and `char_utils_combat.cpp` join
  `entity_lifecycle.cpp`/`object_utils.cpp`/`environment_utils.cpp` in `ROTS_ENTITY_SOURCES`
  (EC Task 3), preceded by EC Tasks 1-2 relocating/inverting both TUs' last real welds
  (`fname`/`fname_nameholder`/`other_side`/`other_side_num` → `char_utils.cpp` itself;
  `attack_hit_text[]`/`get_hit_text` → `consts.cpp`; the `wild_fighting_handler`
  construct-and-query and `big_brother::on_character_attacked_player()` calls inverted through
  two new `entity_hooks.h` hooks). `EntityLayerAcyclicity` went green first attempt — no cascade.
  The remaining two of the row's original six, `handler.cpp` and `utility.cpp`, are
  **deliberately deferred, not forgotten**: `handler.cpp` carries roughly 30 named-but-not-yet-
  enumerated welds into combat/world/commands-tier code; `utility.cpp` was never given a
  TU-wide `nm` census the way `char_utils.cpp` was, so its gap is app-wide rather than a short
  counted list. Both are recorded follow-on for a future wave, not this one's scope. See
  `docs/BUILD.md`'s "`rots_entity`" section for the full membership/STOP/gap account.
- **Resolved, partially (world-seed wave).** `rots_world` stands up as the second of the three L3
  peer libraries to actually exist as a build target (`rots_persist` was the first) — **3 of the
  row's original ~15 TUs**: `db_world.cpp` (from `db.cpp`), `zone_load.cpp` (a NEW TU, not in the
  row's original list — Task 4 carved zone-file parsing/loading out of `zone.cpp` byte-identically,
  264 moved lines, reviewer-diffed zero-difference), and `weather.cpp`. `WorldLayerAcyclicity`
  (§11) proves no upward edge beyond `rots_persist`/`rots_entity`/`rots_core`/`rots_platform` —
  ctest 1274→1275 the task it was added, then 1275→1281 after Task 5b's 6 targeted coverage tests.
  Getting there required the same instruments as the two prior L3/L2 slices: relocations and
  storage moves (Task 1: `register_npc_char`/`last_control_set` → `rots_entity`, `dice` →
  `rots_platform`, `time_info`/`weather_info`/`character_list`/`object_list`/`boot_mode`/
  `mini_mud` storage moved to their steady-state owning TU); scratch-buffer retirement (Task 2:
  `db_world.cpp`'s `buf`/`buf1`/`buf2` globals replaced with local composition, mirroring
  `db_players.cpp`'s prior conversion); three `world_hooks.h` hook inversions (Task 3: boot-the-
  shops, mudlle-converter, weather-MSDP) for app-tier calls relocation alone couldn't cut; and an
  `nm`-gated linkcheck cascade (Task 5) that surfaced four further upward edges the census had
  missed (`buf2` in the new `zone_load.cpp`, `time_info` read by `db_boot()`, and
  `send_to_sector()`/`send_to_outdoor()` calling into `comm.cpp`'s `descriptor_list` walkers),
  each controller-adjudicated and fixed byte-preservingly (a local-buffer conversion, a storage
  move, and a fourth `world_hooks.h` hook pair, respectively) before the linkcheck went green.
  One L3-peer edge is sanctioned, not cut: `db_world.cpp` registers `world_room_vnum()` as
  `rots_persist`'s pre-boot room-vnum hook (`rots::persist::set_room_vnum_hook`) — an
  L3(world)→L3(persist) edge, the mirror image of `rots_persist`'s own pre-existing edge into
  world/boot-tier hooks, and the reason `rots_world` links `RotS::persist` PUBLIC alongside its
  three other downward edges. `zone.cpp`'s **reset half** (`reset_zone()`/the runtime zone-reset-
  command interpreter), the `shape*`/`script`/`mudlle`/`mudlle2`/`graph` OLC-tool family, and
  `handler.cpp`/`utility.cpp` (still pending the §7 Placement seam, unchanged from the
  entity-completion wave's account above) are the row's remaining, **deliberately deferred** TUs —
  recorded follow-on for whichever future wave next touches the world/app boundary, not this
  wave's scope. See `docs/BUILD.md`'s "`rots_world`" section for the full membership/cascade/
  deferral account.
- **Resolved, partially (combat-seed wave).** `rots_combat` stands up as the third and last of the
  three L3 peer libraries — **4 of the row's original 16 TUs**: `skill_timer.cpp`,
  `battle_mage_handler.cpp`, `weapon_master_handler.cpp`, `wild_fighting_handler.cpp`.
  `CombatLayerAcyclicity` (§11) proves no upward edge beyond `rots_entity`/`rots_core`/
  `rots_platform` — ctest 1315→1316 the task it was added, then 1316→1343 after a standing
  coverage-gap rider added 27 targeted tests. Unlike the two prior L3 slices, this one needed
  **no** relocation, storage move, or hook inversion at all: the combat-census
  (`.superpowers/sdd/combat-census.md`, a 16-TU per-TU `nm`-evidence verdict table) found these
  four TUs already fully closed over L0/L1/L2 plus the existing `output_seam` — a pure membership
  move, and the linkcheck went green on the first attempt (no cascade, unlike `rots_entity`'s two
  rounds or `rots_world`'s four-edge cascade). The wave also completed two placement-seam
  deferral riders left over from that wave's step 4 third slice: the time quartet
  (`real_time_passed`/`mud_time_passed`/`day_to_str`/`age`) moved into `consts.cpp` (`rots_core`,
  L1), and `NumberedName` extracted out of `handler.h` into `rots/platform/numbered_name.h` — a
  tier deviation from this section's original core-tree sketch, forced by an L0-visibility
  constraint (`parse_numbered_name`, moving to L0 `rots_util.cpp`, needs the type visible from L0;
  `rots_platform` has no include path into `rots_core`) and precedented by `rots/platform/log.h`'s
  identical shape — after which `parse_numbered_name` (→ `rots_util.cpp`) and `get_char` (→
  `entity_lifecycle.cpp`) completed. `profs` (caveated SEED-WITH-SEAM) and the row's other 11 TUs
  (`fight, limits, mobact, ranger, clerics, mage, mystic, spell_pa, spec_pro, spec_ass, olog_hai`)
  remain entirely app-compiled — the census's blocker analysis (dominant blocker: the app-side
  `handler.cpp`/`utility.cpp` remainder, plus a command-dispatch seam for the mob-AI/spec-proc
  TUs) is recorded follow-on for whichever future wave next grows the row, not this wave's scope.
  The poison-notification hook (`obj_from_char`/`extract_obj`) stays rejected, unchanged from the
  entity-completion/world-seed waves' account. See `docs/BUILD.md`'s "`rots_combat`" section for
  the full membership/growth-inventory/backlog account.
- **Resolved, enablers only (blocker-buster wave) — the row's TU count is unchanged at 4 of 16, but
  both blockers the combat-seed wave's census left open are now CLEARED.** `rots_combat` gains two
  new TUs not in the row's original 16-TU sketch — `combat_hooks.cpp` (Task 2) and `visibility.cpp`
  (Tasks 4/4b) — bringing the library to **6 TUs total**, though neither is a DEFER-11 migration:
  this wave was explicitly **enabler-only, consumer-free** (no DEFER-11 TU moved). The four
  enablers: (1) `output_seam` extended by 7 forwarders (`send_to_all`/`send_to_room`/
  `send_to_room_except_two`/`break_spell`/`abort_delay`/`complete_delay`/the txt-pool content
  overload); (2) `combat_hooks.h`'s 25-cell boot-registered ACMD dispatch table (the
  `assign_spell_pointers()` precedent, registered from `interpre.cpp`/`db_boot.cpp`'s existing
  sequence — no call-site conversion this wave); (3) `entity_hooks.h`'s poison-notification hook,
  which **retires the poison-notification hook cluster this section's prior bullet called
  "rejected"** — `obj_from_char`/`extract_obj` completed their placement-seam-era deferred moves to
  `rots_entity` (L2) this wave, characterization-tested before and after the inversion; (4) the
  full visibility family (12 functions: `CAN_SEE`×2/`CAN_SEE_OBJ`/`get_char_room_vis`/
  `get_player_vis`/`get_char_vis`/`get_obj_in_list_vis`/`get_obj_vis`/`get_object_in_equip_vis`/
  `generic_find`/`get_real_OB`/`get_real_parry`, plus the carved-out `see_hiding`) moved out of
  `handler.cpp`/`utility.cpp` into the new `visibility.cpp` — the OB/parry/dodge trio this
  section's `rots_entity` bullet left split now reunites entirely in library code (L2
  `get_real_dodge` + L3 `get_real_OB`/`get_real_parry`), and `rots_combat` gains its first genuine
  L3-peer link (`RotS::world` PUBLIC, for `visibility.cpp`'s `weather_info` reference). Two census
  errata surfaced and were corrected in the process, not just noted: the planning census's ACMD
  target count (~19) was wrong in both directions (reconciled to 25, including a review-caught
  `do_mental` miss), and its visibility-family verdict conflated "thematically a combat-row TU"
  with "actually in `ROTS_COMBAT_SOURCES` today" (`ranger.cpp`'s `see_hiding`, `interpre.cpp`'s
  `search_block`) — both re-verified against current build wiring, not just the census's row
  assignment. `profs` and the row's other 11 DEFER TUs remain entirely app-compiled; the remaining
  blocker for each is now pure per-TU migration (body move + up-call conversion + fresh census
  re-verification), not enabler design. See `docs/BUILD.md`'s "`rots_combat`" section (the
  blocker-buster subsection) for the full seam/errata/growth-inventory account.

---

## 4. `db.cpp` split and the standalone converter

### 4a. Split `db.cpp` along the persist/world seam

`db.cpp` has three separable jobs that split onto two libraries plus the app:

| New unit | Responsibility | Lands in |
|---|---|---|
| `db_world.cpp` | room/mob/obj/zone/shop index + parse + reset | `rots_world` |
| `db_players.cpp` | pfile index, char load/store, player-table boot | `rots_persist` |
| `db_boot.cpp` | boot orchestration (invokes world + player load in order) | `rots_app` |

This moves world-file loading — which the converter does **not** need — out of persistence, so
`rots_persist` narrows to exactly player/character/account/board/mail persistence + the migration
converters.

### 4b. `rots_convert` — a second executable, and the boundary enforcer

A new executable with its own small `main()`:

```
rots_convert = rots_platform + rots_core + rots_entity(minimal) + rots_persist
               (NO rots_world, rots_combat, rots_commands, rots_app)
```

- It performs legacy → modern character conversion **en masse, outside MUD execution**.
- It calls the **same** `character_json` / `objects_json` / `exploits_json` / `convert_*` code the
  MUD uses, so mass-conversion output is byte-identical to in-MUD lazy conversion by construction.
  The existing `legacy_*_fixture.bin` goldens are its regression suite.
- **It is a required, CI-linked build target.** If a change re-welds the persistence path to the
  game (combat/world/commands/session), `rots_convert` fails to link and the build breaks. The
  converter is thus the executable acid-test that the persistence boundary holds.
- **Expected friction:** `rots_entity` (`char_utils`/`handler`) may be welded to combat/world at
  link time. The `rots_convert` link surfaces each weld precisely; cutting it (interface seam or
  relocating a misplaced function) is the intended "refactor one library at a time" work.

**As-built (db.cpp-split wave):** implemented per this section, with the following details fixed
by the real code rather than left to the sketch:

- **§4a shipped with an unforeseen fourth TU.** The split wasn't a clean three-way partition —
  `free_char`/`make_char_data`/`clear_char`/`init_char`/`reset_char`/`free_obj` and friends are
  called from *both* halves (`db_world.cpp`'s `read_mobile` and `db_players.cpp`'s store paths),
  so they belong to neither. They landed in a new `entity_lifecycle.cpp`, which also absorbed the
  affect/derived-ability engine (`affect_modify`/`affect_total`/`affect_naked`/`affect_to_char`/
  `affect_remove`/`recalc_abilities`/the naked-perception/willpower and
  confuse-modifier helpers) and the save-file cipher (`encrypt_line`/`decrypt_line`), relocated
  verbatim from `handler.cpp`/`profs.cpp`/`utility.cpp` once `rots_convert` (below) proved it
  needed them and would otherwise carry duplicate copies in the stub ledger. (`class_HP` and the affected-type pool helpers stayed in their origin TUs per the shared-helper rule — still used by non-relocated siblings; the converter carries documented stand-ins.) This seeds
  `rots_entity` earlier than planned — `entity_lifecycle.cpp` is that library's future contents,
  just not yet extracted into its own archive/target.
- **The `world_room_vnum` seam shipped exactly as designed**: declared in `db.h` next to
  `real_room`, defined in `db_world.cpp` as `return world[room_index].number;`, called by
  `save_char` (`db_players.cpp`) instead of touching `world[]` directly. `rots_convert` supplies
  its own definition in `convert_stubs.cpp`.
- **A capture/codec split fell out of the persist/world seam that the original two-table sketch
  didn't call out**: `record_crime` and `add_exploit_record` *observe* live game state
  (`world[]`/`combat_list` walks) rather than serialize an already-captured record, so they stayed
  in `db_boot.cpp` (the app-layer remainder) while the crime and exploit JSON codecs proper
  (encode/decode a record already in hand) went to `db_players.cpp`. This split is what makes
  `db_players.cpp` linkable into `rots_convert` at all — a capture function pulled in would have
  dragged combat/world state with it.
- **§4b shipped with membership deliberately narrower than the sketch's `rots_persist`-equals-all-
  persistence framing**: `rots_convert` links `db_players.cpp` + `entity_lifecycle.cpp` plus the
  JSON codecs, account system, and the legacy exploit/plrobj converters — but **not**
  `objsave.cpp`/`boards.cpp`/`mail.cpp`/`pkill.cpp` (as-built at this section's original writing).
  Those welds were deferred follow-on work, catalogued (symbol, real home, why the converter's call
  graph never reaches it, and the follow-on that removes it) in `convert_stubs.cpp`'s weld ledger
  rather than pulled in speculatively. Known follow-ons were cataloged in the ledger itself; the
  headline ones at the time: the `send_to_char` output seam, an `APPLY_SPELL` null-skip, and the
  objsave/boards/mail membership itself.
  **As-built update (persist-split PS Tasks 2-4):** the four files' pure-codec halves are now IN —
  `obj_files.cpp` (from `objsave.cpp`'s P block), `pkill_json.cpp`, `mail_json.cpp`, and
  `boards_json.cpp` all joined `rots_persist`, and `rots_convert` links their real definitions
  through `RotS::persist`. Their runtime/bridge/gameplay halves stay deliberately OUT — G-side
  orchestrators (`Crash_crashsave`/`idlesave`/`rentsave`, `Crash_load`/`Crash_listrent` and the
  rent/receptionist flow), `pkill_tab`/rankings/`combat_list` walkers, the mail store
  (`find_char_in_index`/`persist_mail_or_log`/`index_mail`/`scan_file`/`has_mail`) and postmaster
  gameplay, and boards' display half plus its `save_board`/`apply_board_save_data`/`load_board`
  bridge — none of these are `nm`-clean against the converter's link surface, and chasing them is
  recorded follow-on (boards' bridge in particular moves only once boards' runtime half gets its
  own split). See `docs/BUILD.md`'s "`rots_persist`" and "`rots_convert`" sections for the full
  carve inventory and the current weld-ledger count.
  **As-built update, the ending (entity-completion wave, branch `arch/entity-complete`,
  2026-07-18):** the "expected friction" this bullet predicted was real but finite. EC Task 1
  relocated `fname`/`fname_nameholder`/`other_side`/`other_side_num` (`handler.cpp` →
  `char_utils.cpp`) and `attack_hit_text[]`/`get_hit_text` (`fight.cpp` → `consts.cpp`); EC Task 2
  inverted the last stub body, `player_spec::wild_fighting_handler`'s ctor + attack-speed query,
  through a new `entity_hooks.h` hook — **the ledger reached zero stub function bodies**. EC
  Task 3 then deleted `src/convert_stubs.cpp` outright (`git rm`, the "feat: char_utils +
  char_utils_combat join rots_entity; convert_stubs.cpp deleted — weld ledger ZERO" commit,
  entity-completion wave — see `git log -- src/convert_stubs.cpp`) and moved
  `char_utils.cpp`/`char_utils_combat.cpp` into `ROTS_ENTITY_SOURCES`, so `rots_convert`'s only
  direct source is `convert_main.cpp` — the persistence boundary this ledger existed to document
  is now enforced structurally by four libraries' linkchecks plus this executable's own link, with
  no hand-maintained stand-in file left to go stale. The full arc: ~40 documented stub bodies at
  the db.cpp-split baseline → ~19 at entity-seed exit → 5 (four named groups) at persist-split
  exit → 0 after EC Task 2 → file deleted at EC Task 3. See `docs/BUILD.md`'s "`rots_entity`" and
  "`rots_convert`" sections for the per-task account.
- **The CI-linked boundary check works as designed**: `rots_convert` is in CMake's default `all`
  target (no `EXCLUDE_FROM_ALL`) so every CI job builds it, but it is deliberately **not** wired
  into the flat `src/Makefile`/`src/tests/Makefile` — those compile same-directory only against a
  hand-maintained `OBJFILES` list per binary, and a second multi-file executable isn't worth
  fighting that pattern for a CMake-native CI check.
- **Equivalence proven, not asserted**: the `ConvertEquivalence` GoogleTest suite
  (`src/tests/rots_convert_equivalence_tests.cpp`) parameterizes over every playable `RACE_*`
  constant (all sixteen, including the four NPC-only races) plus one affect-bearing case — 17
  cases — building a fixture legacy pfile, running it through `rots_convert` out-of-process, and
  asserting byte-identical output against the in-MUD conversion path. This is what makes "byte-
  identical by construction" (this section's claim) a tested property rather than an assumption.

---

## 5. `rots_core`: splitting the data-model god-header

Replace `structs.h` with a strict internal header DAG:

```
rots/core/
  types.h        leaf: sh_int/byte typedefs, enums (weapon_type, position, source_type),
                 pure value structs (obj_flag_data, obj_affected_type, extra_descr_data,
                 target_data, waiting_type, ability/point data). NO entity pointers.
  fwd.h          ONLY forward declarations:
                 struct char_data; struct obj_data; struct room_data; struct descriptor_data;
  object.h       obj_data        (includes types.h + fwd.h)
  room.h         room_data       (includes types.h + fwd.h)
  character.h    char_data       (includes types.h + fwd.h)
  descriptor.h   descriptor_data (includes types.h + fwd.h)
```

**The linchpin is `fwd.h`.** Because entities reference each other only by pointer
(`char_data::equipment[]`/`carrying` are `obj_data*`; `char_data::desc` is `descriptor_data*`;
`descriptor_data::character` is `char_data*`; `room_data` holds `char_data*`/`obj_data*` lists), the
four entity headers **include `fwd.h`, not each other.** Full definitions are pulled in only by the
`.cpp` files (and headers) that actually dereference members.

Consequences:

- **Compile cascade collapses.** Editing `room.h` rebuilds only TUs that touch rooms, not the 75
  files that merely needed `char_data`. This is the recompile win, independent of the library
  split; the libraries then enforce it.
- **Cross-entity coupling collapses.** A combat file including `character.h` + `object.h` becomes
  blind to `room.h` unless it genuinely uses rooms.

`char_file_u`, `obj_file_elem`, `rent_info` and the other file-format structs are **persistence**
types, not core entity types — they move to a `rots_persist` header, not `rots_core`.

**As built (header-split wave, Task 12 exit):** the carve landed with five deviations from the
prose above, none of them changing the DAG shape or the layout-probe-verified ABI:

(a) `types.h` includes `fwd.h`, not the reverse — `target_data`/`waiting_type` (both value structs
    that live in `types.h`) hold entity pointers (`char_data*`/`obj_data*`/`room_data*`), so they
    need the forward declarations. §5's "NO entity pointers" line above is amended in spirit to "no
    entity *definitions*" — `types.h` still never pulls in a full `char_data`/`obj_data`/`room_data`/
    `descriptor_data` body, only their forward-declared pointer types.

(b) A `tables.h` leaf exists alongside `types.h`, holding the `CONSTANTSMARK`-guarded
    `global_release_flag` extern/definition trick and the extern calendar/race table declarations
    (`weekdays`, `month_name`, `moon_phase`, `pc_races`, …) that `consts.cpp` defines. It was not
    called out in the original four-file `rots/core/` sketch above; it is a fifth leaf with the same
    "no entity pointers" shape as `types.h`.

(c) Persistence formats (`char_file_u`, `follower_file_elem`, `obj_file_elem`, `rent_info`,
    `RENT_*`) landed in `rots/persist/file_formats.h`, matching the "not `rots_core`" call above.

(d) Core headers reach `platdef.h`/`color.h`/`protocol.h` by explicit relative include
    (`"../../../../platdef.h"`), not a `-I`/`-idirafter` path entry, per the Global Constraints
    `src/` shadowing rule — this holds until those legacy headers themselves relocate.

(e) `.cpp` physical relocation into the `core/`/`persist/` subfolder tree (§9a's eventual layout) is
    deferred: the flat `src/Makefile` (and `src/tests/Makefile`) still list every `.cpp` by its
    current flat path, and moving them is deferred until that Makefile is retired in favor of
    CMake-only builds. Only the new header files live in the subfolder layout today.

---

## 6. Character / Session / Account / Location decoupling

There is no first-class *player* or *account* concept today — it is smeared across three places,
including **account identity stored as loose `char[]` buffers inside the socket struct
(`descriptor_data`)**.

**Target: four distinct concepts, each modeled at its own layer, every relationship external and
optional.**

```
Account   (rots_persist, L3)  ── owns ──▶  Character(s)
  │  login, email, credential hash, list of owned character names
  ▼
Session   (app/net, L5)  ── drives ──▶  Character (live avatar)
  │  socket, buffers, protocol, connection state; holds an Account handle
  ▼            (NOT loose account_name/email/password/character char[] fields)
Character (rots_core, L1)  ── has ──▶  Location? (optional), Equipment, Stats
     no account fields, no session fields (only a fwd-declared desc* back-pointer)
```

Migration direction:

- Lift a first-class `Account` type into `rots_persist` (the JSON account system is already most of
  it).
- Have `Session`/`descriptor_data` hold an `Account` handle instead of the four
  `account_name`/`account_email`/`account_password`/`account_character_name` `char[]` fields.
- Strip account identity out of both `descriptor_data` and `char_data`.

**Unifying principle:** a `Character` is a self-contained entity; being *in a room*, *driven by a
session*, and *owned by an account* are all external, optional relationships. This is exactly the
state `rots_convert` needs — a character with no session, no attached account, and no location —
so "a character with no location" falls out of the same principle rather than being a special case.

---

## 7. Placement / Containment / Equipment systems

`handler.cpp` (L2 `rots_entity`) is already the de-facto relationship layer — all mutation
(`char_to_room`, `char_from_room`, `obj_to_room`, `obj_to_char`, `equip_char`, `unequip_char`, …)
funnels through it. Promote it to three explicit systems that own the relationships **externally**:

| System | Owns | Replaces raw access to |
|---|---|---|
| Placement | character/object ↔ room | `ch->in_room`, `world[id]`, `room->people`/`next_in_room` |
| Containment | object ↔ (room \| char \| obj) sum type | `obj->in_room`/`carried_by`/`in_obj`/`contains` |
| Equipment | object ↔ char wear-slot | `ch->equipment[]` |

Access-site scale (the read surface, which is the whole game): **754 `->in_room` sites, 576
`world[...]` lookups, 104 `next_in_room` traversals.** Mutation is already centralized; only reads
are scattered. The decoupling is therefore staged:

**Stage 1 — encapsulate access (representation unchanged; mechanical; low-risk).**
Introduce read/iteration APIs that wrap today's exact representation:
`location_of(ch)` / `set_location(ch, room)`, `room_by_id(id)`,
`for (auto* occ : occupants(room))`. Convert call sites **library by library**
(`rots_combat`, then `rots_world`, …), each batch independently testable against goldens. After
Stage 1, nothing outside the Placement system knows how location is stored.

**Stage 2 — swap representation (localized to `rots_entity`; after Stage 1).**
A `LocationSystem` maps `char_data*` → room id and room id → occupants; `char_data` sheds
`in_room`/`next_in_room` (or keeps only a private handle). **"No location" = simply absent from the
map** — no `NOWHERE` sentinel, no `world[NOWHERE]` hazard. `rots_convert` links `rots_core` +
`rots_persist` and never instantiates the `LocationSystem`.

**Generalization (follow-on, not scoped now):** `char_data` carries four intrusive threads
(`next_in_room`, `next_fighting`, `next_fast_update`, `next`). Placement handles the first; the same
"external system owns the relationship, entity sheds the pointer" pattern later applies to the
combat fighting-list and the update lists.

---

## 8. Macro → function migration

`utils.h` (and peers) define **261 function-like macros** (210 in `utils.h`), with **~2,483 `GET_*`
and ~1,330 `IS_*` call sites**. Target: replace them with real functions, organized by the library
that owns the concept. Per the repository convention (prefer free functions / non-member helpers
over growing a class's public interface), the default is **C-style free functions taking the struct
by reference** — fitting the public-data-member structs — with member functions reserved for
intrinsic accessors that already exist (e.g. `char_data::get_level()`).

Migration is by macro **family**, each family landing in its owning library's header, and each
convertible independently and testably:

| Family | Examples | Target | Owning lib |
|---|---|---|---|
| Character accessors | `GET_LEVEL`, `GET_STR`, `GET_HIT` | `get_level(const char_data&)` free fns (or existing members) | `rots_core` |
| Predicates | `IS_NPC`, `IS_AFFECTED`, `IS_DARK` | free predicate fns | `rots_core` / `rots_entity` |
| Bit-flag ops | `IS_SET`, `SET_BIT`, `MOB_FLAGGED` | small typed inline helpers | `rots_platform` / base |
| Generic utility | `MAX`, `MIN`, `LOWER`, `UPPER`, `CAP` | `std::max/min`, `std::toupper`, existing text helpers | std / `rots_platform` |
| Memory | `CREATE`, `RELEASE`, `RECREATE` | RAII (continue the in-progress `std::vector`/owner migration) — **not** a 1:1 function wrap | n/a |

This is a long-tail effort woven into the per-library refactors, not a standalone wave.

---

## 9. Build system

**CMake becomes the single source of truth.** The library structure is expressed once, as CMake
targets with declared `target_link_libraries` edges; the historical `src/Makefile` is retired or
auto-generated rather than hand-maintained in lockstep. The i386 shipping build moves to CMake
(presets already exist: `linux-x86-legacy` and the container flow). Layer acyclicity is enforced by
the link graph — an upward edge is a link error.

Each library uses target-scoped includes (`target_include_directories(... PUBLIC ...)`) so a
consumer sees only the headers of libraries it links, reinforcing the boundaries at compile time.

### 9a. Target physical layout — subfolder per library

Each library is its own subdirectory under `src/`, added with `add_subdirectory()` and owning its
**own `CMakeLists.txt`** (a directory scope that defines the target and its usage requirements) —
*not* an `include()`d `.cmake` fragment (which runs in the parent scope and is reserved for shared
build snippets such as the `rots_build_flags` definition).

```
src/
  CMakeLists.txt          # add_subdirectory(platform); add_subdirectory(core); ...
  cmake/flags.cmake       # shared .cmake snippet: rots_build_flags
  platform/
    CMakeLists.txt        # add_library(rots_platform STATIC ...) + PUBLIC include dir
    rots_net.cpp  rots_rng.cpp  ...
    include/rots/platform/*.h
  core/  entity/  persist/  world/  combat/  commands/  app/   # same shape
```

- Each library exposes headers via `target_include_directories(rots_x PUBLIC include)`, so a
  consumer sees a library's headers only if it links it — this is what enforces the boundary and
  cleanly resolves the historical `src/limits.h`-shadows-`<limits.h>` hazard.
- **Includes become pathed** (`#include "rots/core/character.h"`), adopted *during the header split*
  (§5): that sweep already touches every consumer of `structs.h`, so relocating files into `core/`
  and pathing their includes in the same pass avoids editing the same includes twice. Doing the
  subfolder move before the header split would double the include churn.
- This is a **later step, not the first slice.** The initial plan deliberately moves no files
  (stand up the target graph first); physical relocation into subfolders happens per layer, starting
  with the header split. It composes cleanly with CMake-as-single-source-of-truth (no flat
  `Makefile` `OBJFILES` list to keep in sync with a nested tree).

---

## 10. Sequencing (incremental, boundary-safe)

Each step keeps `ageland` building and all goldens green; no big-bang cutover.

**The acyclic layer graph is a goal state reached incrementally, not declared up front.** The flat
code is still cyclic across the eventual boundaries (`entity`↔domains, `persist`↔`world`↔`combat`),
so the libraries are stood up one decoupled layer at a time — each extraction only happens once that
layer's code genuinely has no upward edges.

1. **Shared build-flags interface + foundation library.** Factor the common compile
   options/definitions/features into a `rots_build_flags` INTERFACE library (today they are
   duplicated inline on `ageland` and `ageland_tests`), then extract `rots_platform` — the one layer
   that is *already* acyclic (8 clean-leaf TUs, zero upward symbol references) — into a `STATIC`
   library consuming that interface. `ageland` links it; the Makefile is untouched (no files move);
   `ageland_tests` keeps compiling those sources directly so test behavior is byte-identical. This
   establishes the extraction pattern every later layer reuses.
2. **Split the god-header.** Carve `structs.h` into the `rots/core/` DAG (Section 5). This is the
   highest-leverage step for recompile time, and it is the prerequisite for the acyclic middle: the
   `entity`/`persist`/`world`/`combat` libraries cannot be cleanly extracted while `structs.h`
   forces every TU to depend on the whole data model.
3. **Split `db.cpp`** into `db_world` / `db_players` / `db_boot`; stand up `rots_convert` and make
   it link + pass the fixture goldens. This proves the persistence boundary.
4. **Peel the domain libs** (`rots_entity`, then `rots_persist`/`rots_world`/`rots_combat`, then
   `rots_commands`, then `rots_app`), one at a time, cutting welds as `rots_convert` and the link
   graph surface them.
5. **Staged decouplings** (location Stage 1→2, account/session separation, macro→function) run as
   long-tail work *within* the now-stable library boundaries.

**As-built (entity-seed wave, step 4 first slice):** `rots_entity` is seeded, not yet complete —
Tasks 1-6 stood up the library with its first three members (`entity_lifecycle.cpp`,
`object_utils.cpp`, `environment_utils.cpp`, all descended from the `db.cpp`-split's "unforeseen
fourth TU", §4a) and the `EntityLayerAcyclicity` linkcheck (§11) proving they carry no upward edge
beyond `rots_core`/`rots_platform`. Getting there required, in dependency order: the `skills[]`
registration scheme (§3, above) so `rots_core` could absorb `consts.cpp` first; the output seam
(§13's pattern, `output_seam.h`/`.cpp`) so the entity layer's `send_to_char`/`act`/mage-roster
calls invert instead of linking up into `comm.cpp`; the platform-helper relocations (new
`rots_util.cpp`) so entity code's `number()`/`str_dup`-family calls resolve in L0; and two
`entity_hooks.h` inversion hooks (char-teardown → `objsave.cpp` registers, attack-speed →
`wild_fighting_handler.cpp` registers) for the two remaining edges relocation alone couldn't cut.
Two rounds of link-time STOP conditions during Task 6 pulled a further ~23 leaf-clean symbols out
of `char_utils.cpp` and into `entity_lifecycle.cpp` (the `specialization_info` method family plus
`get_name`/`is_race_good`/`is_race_magi`) rather than stubbing them, since each was verified to
have no further upward reach.

`char_utils.cpp`, `char_utils_combat.cpp`, and `handler.cpp` — the other three TUs §3's original
table assigned to `rots_entity` — are **deliberately deferred**: the `rots_convert` link surfaced
real, named welds in each (`char_utils.cpp`: `get_hit_text`→`fight.cpp`, the
`wild_fighting_handler` ctor/method, `other_side`→`handler.cpp`; `char_utils_combat.cpp`:
`big_brother::on_character_attacked_player`; `handler.cpp`: ~30 welds, not yet enumerated) rather
than being cut speculatively this wave. `convert_stubs.cpp`'s weld ledger shrank from ~40
documented stubs (~1.6K lines) to ~15 as each task's relocation or seam removed the real edge a
stub stood in for — see `docs/BUILD.md`'s "`rots_convert`" section for the full task-by-task
account and the remaining stub inventory.

**As-built (persist-split wave, step 4 second slice):** `rots_persist` stands up as the second of
the three L3 peer libraries — 14 TUs, `PersistLayerAcyclicity` linkcheck, ctest 1273→1274 (see
`docs/BUILD.md`'s "`rots_persist`" section for the full membership/carve/relocation/hook account).
Getting there required the same three instruments as `rots_entity`'s slice, one layer up: verbatim
single-cut carves for the four codec TUs (`pkill_json.cpp`/`mail_json.cpp`/`boards_json.cpp`/
`obj_files.cpp` — each origin file's codec namespace was already contiguous and leaf-clean, so no
function-by-function extraction was needed, unlike `rots_entity`'s Task 5 relocations); pre-boot
registration inversion (`persist_hooks.h`) for `db_players.cpp`'s last two upward edges
(`world_room_vnum` → `db_world.cpp`, `add_exploit_record` → `db_boot.cpp`); and `nm`-gated library
membership with the same STOP-condition adjudication contract, which resolved nine relocations
(`find_player_in_table`/`find_name`/`unaccent`/`recalc_skills`/`utils::set_tactics`/
`set_shooting`/`set_casting`/`file_to_string`/`file_to_string_alloc`) and two membership-only moves
(`color_convert.cpp`, `save_benchmark.cpp`) without a single STOP round — the `nm` census surfaced
all nine relocations up front, unlike `rots_entity`'s two-round cascade. `rots_convert`'s weld
ledger shrank the rest of the way this mechanism can take it, from ~15 entries (entity-seed exit)
to 5 stub function bodies across 4 named groups (`fname`, `other_side`, `get_hit_text`, the
`wild_fighting_handler` ctor/method pair) — see `docs/BUILD.md`'s "`rots_convert`" section for the
per-symbol account. `rots_world`/`rots_combat`, the other two L3 peers, remain entirely
app-compiled; this slice does not touch them.

**As-built (entity-completion wave, step 4 third slice):** `rots_entity` closes out to **5 of the
row's original 6 TUs** — `char_utils.cpp` and `char_utils_combat.cpp` join
`entity_lifecycle.cpp`/`object_utils.cpp`/`environment_utils.cpp` in `ROTS_ENTITY_SOURCES`,
`EntityLayerAcyclicity` green first attempt, no cascade. Unlike the two prior slices, this one is
pure membership, not relocation: EC Tasks 1-2 (this wave's first two steps) had already relocated
both TUs' last real welds — `fname`/`fname_nameholder`/`other_side`/`other_side_num`
(`handler.cpp` → `char_utils.cpp`), `attack_hit_text[]`/`get_hit_text` (`fight.cpp` →
`consts.cpp`), and the `wild_fighting_handler` construct-and-query plus
`big_brother::on_character_attacked_player()` calls inverted through two new `entity_hooks.h`
hooks — so Task 3 itself only moved library membership and deleted the now-empty
`src/convert_stubs.cpp`. This is also the slice that **finishes the weld ledger's arc started at
the db.cpp-split baseline**: ~40 stub bodies → ~19 (entity-seed exit) → 5 (persist-split exit) →
0 (EC Task 2) → file deleted (EC Task 3, the "feat: char_utils + char_utils_combat join
rots_entity; convert_stubs.cpp deleted — weld ledger ZERO" commit, entity-completion wave — see
`git log -- src/convert_stubs.cpp`) — see `docs/BUILD.md`'s "`rots_entity`"
section for the full account. `handler.cpp` (~30 named-but-unenumerated welds) and `utility.cpp`
(an app-wide `nm` profile, never TU-wide-censused) are the row's remaining two TUs — deliberately
deferred, recorded follow-on for whichever wave next touches the entity/app boundary, not this
slice's scope. `rots_world`/`rots_combat`, the L3 peers, remain untouched by this wave too.

**As-built (world-seed wave, step 4 fourth slice):** `rots_world` stands up as the second of the
three L3 peer libraries — 3 TUs (`db_world.cpp`, the new `zone_load.cpp`, `weather.cpp`),
`WorldLayerAcyclicity` linkcheck, ctest 1274→1275→1281 (Task 5 adds the linkcheck, Task 5b adds 6
targeted coverage tests) — see `docs/BUILD.md`'s "`rots_world`" section for the full membership/
carve/relocation/hook/cascade account. Getting there needed the same three instruments as the two
prior L3/L2 slices, applied to `db_world.cpp`/`weather.cpp`/the new `zone_load.cpp`: relocations
and storage moves (Task 1); a scratch-buffer-to-local-composition conversion for `db_world.cpp`'s
`buf`/`buf1`/`buf2` globals (Task 2, the same pattern `db_players.cpp` proved first); three
`world_hooks.h` hook inversions for app-tier calls relocation alone couldn't cut (Task 3:
boot-the-shops, mudlle-converter, weather-MSDP); and a byte-identical carve of zone-file parsing/
loading out of `zone.cpp` into the new `zone_load.cpp` (Task 4, 264 lines, reviewer-diffed
zero-difference against the pre-carve blob), which also brought `zone_table`/`top_of_zone_table`
storage with it. Unlike the two prior slices, this one's `nm`-gated library membership step (Task
5) surfaced a **cascade**, not a clean pass: standing up `rots_world_linkcheck` found four upward
edges the wave's earlier census had missed (`buf2` in the new `zone_load.cpp`'s error labels,
`time_info` read by `db_boot()`, and `send_to_sector()`/`send_to_outdoor()` calling into
`comm.cpp`'s `descriptor_list` walkers from `weather.cpp`) — each controller-adjudicated per the
wave plan's STOP contract and fixed byte-preservingly (a local-buffer conversion, a storage move to
`weather.cpp`, and a fourth `world_hooks.h` hook pair) before the linkcheck went green, mirroring
`rots_entity` Task 6's two-round STOP precedent one layer up. One edge is sanctioned rather than
cut: `db_world.cpp` registers `world_room_vnum()` as `rots_persist`'s pre-boot room-vnum hook
(`rots::persist::set_room_vnum_hook`) — an L3(world)→L3(persist) edge, the mirror image of
`rots_persist`'s own pre-existing edge into world/boot-tier hooks (persist-split wave), and the
reason `rots_world` links `RotS::persist` PUBLIC alongside `RotS::entity`/`RotS::core`/
`RotS::platform`. `zone.cpp`'s reset half, the `shape*`/`script`/`mudlle`/`mudlle2`/`graph`
OLC-tool family, and `handler.cpp`/`utility.cpp` (still pending the §7 Placement seam) are the
row's remaining TUs — deliberately deferred, recorded follow-on for whichever wave next touches
the world/app boundary, not this slice's scope. `rots_combat`, the last L3 peer, remains untouched
by this wave.

**As-built (combat-seed wave, step 4 fifth slice):** `rots_combat` stands up as the third and last
of the three L3 peer libraries — 4 TUs (`skill_timer.cpp`, `battle_mage_handler.cpp`,
`weapon_master_handler.cpp`, `wild_fighting_handler.cpp`), `CombatLayerAcyclicity` linkcheck, ctest
1315→1316→1343 (Task 1 adds the linkcheck, Task 3b adds 27 targeted coverage tests closing the
`skill_timer.cpp`/`wild_fighting_handler.cpp` coverage-gap flags Task 1's citation step raised) —
see `docs/BUILD.md`'s "`rots_combat`" section for the full membership/growth-inventory/backlog
account. Unlike all four prior L2/L3 slices, this one needed none of their instruments —
no relocation, no storage move, no hook inversion, no linkcheck cascade: the combat-census
(planning-stage `nm`-evidence survey, `.superpowers/sdd/combat-census.md`) verified the 4 TUs
already fully closed over L0/L1/L2 plus the existing `output_seam`, so Task 1 was a pure
membership move and `CombatLayerAcyclicity` went green first attempt, confirmed non-vacuous by a
positive-PASS/negative-FAIL probe. Two riders left over from the placement-seam wave's third
slice (§3, above) completed alongside the seed: **Task 2** moved the time quartet
(`real_time_passed`/`mud_time_passed`/`day_to_str`/`age`) from `utility.cpp`/`act_info.cpp` into
`consts.cpp` (`rots_core`, L1) verbatim, exactly as this section's own step-4 sequencing sketched.
**Task 3** extracted `NumberedName` out of `handler.h` — but into `rots/platform/numbered_name.h`
(L0 `rots_platform`'s include tree), not the `rots_core` (L1) location this document's earlier
sketch implied: `parse_numbered_name`, the deferred function moving to L0 `rots_util.cpp`, needs
`NumberedName` visible from L0, and `rots_platform` has no include path into `rots_core` (a
compile-path check, not guesswork), so the type had to live at or below L0. `rots/platform/log.h`
(§13) is the standing precedent for an L0-visible shared type; `handler.h` now
compatibility-includes the new header, so no caller changed. `parse_numbered_name` then moved to
`rots_util.cpp` and `get_char` to `entity_lifecycle.cpp`, both verbatim. `profs` (caveated
SEED-WITH-SEAM: 3 small edges, but pulls in the deeply-blocked `mystic.cpp`) and the row's other
11 TUs (`fight, limits, mobact, ranger, clerics, mage, mystic, spell_pa, spec_pro, spec_ass,
olog_hai`) remain entirely app-compiled — the census's blocker analysis found their dominant
blocker is the app-side `handler.cpp`/`utility.cpp` remainder (unchanged from the entity-completion/
world-seed waves' accounts above), plus, for the mob-AI/spec-proc TUs specifically
(`mobact`/`spec_pro`), an upward call into L4 command entry points that a command-dispatch seam
would need to invert — recorded follow-on for whichever wave next grows the row, not this slice's
scope. The poison-notification hook (`obj_from_char`/`extract_obj`) stays rejected, re-confirmed
by this wave's census as only an ambitious-wave concern.

**As-built (combat-pilot wave, step 4 sixth slice):** `rots_combat` grows from 4 to **6 of the
row's original 16 TUs** — `clerics.cpp` and `fight.cpp` join as a **joint membership move**, both
in one commit. `CombatLayerAcyclicity` green both hosts; ctest 1365→1394 across the wave's six
tasks (see `AGENTS.md`'s Testing Guidelines for the full per-task chain). This is the row's first
DEFER-11 migration since the combat-seed wave's SEED-CLEAN 4-TU seed and the blocker-buster wave's
two enabler-only additions (`combat_hooks.cpp`/`visibility.cpp`, already counted in the "4" above)
— the four blocker-buster enablers (`output_seam` extension, the 25-cell command-dispatch table,
the poison-notification hook, the visibility family) proved themselves under real conversions for
the first time this wave, not merely built consumer-free. **The wave's central architectural
finding, superseding this document's own step-4 sequencing assumption that a census's per-TU
"non-blocking" verdict is sufficient to schedule a standalone promotion:** `clerics.cpp` and
`fight.cpp` call each other directly (six symbols one way, two the other), so neither could
promote alone — the census's `combat-peer (still-app)` legend correctly says such a cross-reference
is architecturally sanctioned, but says nothing about whether it will actually **link** once one
side promotes and the other doesn't. A post-Task-1 STOP restructured the task sequence so
conversions (Task 3, clerics.cpp's own up-call rewrites) and membership (Task 5, both files in one
commit) could land at different times even though membership itself could not split across files —
see `docs/superpowers/combat-migration-playbook.md`'s "The intra-subset rule" and
"Census-methodology correction" sections for the full mechanism, including the `nm` proof and the
two genuine census misses (`gain_exp`, `waiting_list`) the `CombatLayerAcyclicity` linkcheck itself
caught at the membership gate. New hooks this wave (`special()`, a big_brother `is_target_valid`/
`on_character_died` pair, `extract_char`, the `gain_exp` family, and an app-other trio —
`Crash_crashsave`/`call_trigger`/`pkill_create`) all follow the established registered-fn-ptr/
tripwire-default taxonomy; `rots_combat` gains its second genuine L3-peer link
(`RotS::persist` PUBLIC, for `fight.cpp`'s `save_char()` plus the new `dispatch_exploit_capture()`
hook). A combat smoke-test harness (`ROTS_RNG_SEED` + `tools/combat_smoke.py` + `scripts/
combat-golden.sh`) shipped as characterization-first infrastructure but landed on the fallback
ladder's capture-only rung: the shared global `rots_rng` engine's draw sequence proved unstable
under real-time pulse-loop interleaving even across same-seed re-runs, so the harness is
informational, not a merge gate — the wave's actual regression-catching burden stays on the
existing gtest-level characterization goldens. `profs` and the row's remaining 9 DEFER TUs
(`spec_ass, olog_hai, mage, mystic, mobact, spell_pa, limits, ranger, spec_pro`) remain entirely
app-compiled; the migration playbook this wave finalized
(`docs/superpowers/combat-migration-playbook.md`) carries a per-TU cost table for each, marking
`limits.cpp`'s own `gain_exp`-family hook registrars as already built. See `docs/BUILD.md`'s
"`rots_combat`" section (the combat-pilot subsection) for the full hooks/link/harness/storage-move
account.

---

## 11. Enforcement & verification

- **`rots_convert` must link** with only its four allowed libraries — CI-blocking.
- **Acyclic layers** enforced by the CMake link graph (no upward edges).
- **Characterization goldens** (`CharacterizationCombatTest.*`, `CharacterizationJson.*`,
  `boot-golden.sh verify`) stay byte-for-byte green at every step; intentional changes are
  regenerated with `UPDATE_GOLDENS=1` and called out.
- **`legacy_*_fixture.bin` goldens are 32-bit-only** — regenerate only inside the i386 container.
- Per the local cadence: build + test native macOS arm64 and `rots64` (incl. boot goldens) per
  change; run the i386 battery once at wave finalization; sanitizer run for any new/rewritten test
  file.

---

## 12. Risks

- **Hidden link-time welds** in `rots_entity` may make `rots_convert` hard to link initially. This
  is expected and is the point — the converter surfaces them for targeted fixing.
- **Header-split regressions** — a missed include after removing `structs.h`'s transitive pulls.
  Mitigated by building all presets and keeping the split mechanical.
- **i386 ABI sensitivity** — struct layout must not change during the header split (reordering
  members or headers must not alter layout). Legacy fixtures guard this.
- **Scope creep** — location, account, and macro migrations are long-tail. They are explicitly
  *enabled* by this architecture but sequenced as follow-on work, not part of the first carve.

---

## 13. Open follow-ons (out of scope for the first spec/plan)

- Externalize the `next_fighting` / `next_fast_update` / `next` intrusive lists (generalize the
  Placement pattern).
- Complete the macro→function migration family by family.
- Complete location Stage 2 (external `LocationSystem`) and the full account/session separation.
- **Platform logging seam via dependency inversion (a registered log sink).** Give `rots_platform`
  a logging facility that owns the raw timestamped stderr/file write (today's `log()` body,
  `utility.cpp:1285`) *and* defines a sink interface that higher layers register at boot:
  ```
  // rots/platform/log.h (L0 — no game types)
  using Sink = std::function<void(std::string_view msg, char type, int level)>;
  void set_sink(Sink);                                  // registered once at boot
  void write(char type, int level, std::string_view);   // raw write, then notify sink if level>=0
  void writef(char type, int level, const char* fmt, ...);
  ```
  The **app layer** registers a sink holding today's `mudlog` broadcast loop verbatim (iterate
  `descriptor_list`, gate on `PRF_LOG` prefs + `GET_LEVEL >= level`, `send_to_char` with color) — so
  the game-coupled half stays where `char_data`/`descriptor_data` live. The static dependency points
  down (app → platform's `Sink` type); control flows up at runtime through the callback, so
  `rots_platform` references no game symbol. `vmudlog`/`mudlog` become thin wrappers over
  `rots::log::write*` and can then live in `rots_platform` themselves, letting `safe_template.cpp`
  rejoin L0 as a clean leaf.
  - **Behavior-sensitive:** `mudlog` is on nearly every logging path; the sink must reproduce its
    exact gating (`level < 0` → file-only return; `level < LEVEL_AREAGOD` clamp; the `type`/pref
    comparison and color framing). Validate against goldens. This is why it is sequenced here, not in
    the zero-behavior-change skeleton branch (where `safe_template` is simply excluded from L0).
  - Bonus: one raw-write implementation, and tests can register a capturing sink to assert log
    output.

  **As-built (logging-seam wave):** implemented as designed above, with the following details fixed
  by the real code rather than left to the sketch:
  - There is only one stderr target — no separate log file existed to split, so `write_stderr`/
    `write` (`rots/platform/log.h` + `rots_log.cpp`) cover the whole raw-write half; there was no
    second sink to wire up.
  - The `LEVEL_AREAGOD` clamp and the `PRF_LOG*` preference gating stay app-side, inside the
    registered sink — they are game constants/macros (`char_data`/preference flags), not platform
    types, so `rots_platform` never sees them. The sink itself is `comm.cpp`'s
    `register_mudlog_broadcast_sink()`, holding the `descriptor_list` walk / color framing verbatim,
    registered in `run_the_game()` before the first `log()` call.
  - `vmudlog`'s broadcast level is `rots::log::kVmudlogBroadcastLevel` (`= 93`), hard-coded in the L0
    header rather than including the game's `LEVEL_GOD` constant — `utility.cpp` pins the two
    together with `static_assert(rots::log::kVmudlogBroadcastLevel == LEVEL_GOD, ...)` so they can
    never silently diverge.
  - `vmudlog` itself now lives in `rots_log.cpp` (moved out of `utility.cpp`), and `log()`/`mudlog()`
    are thin wrappers over `rots::log::write_stderr`/`write`.
  - The capturing-sink tests (`PlatformLog.*`, `src/tests/platform_log_tests.cpp`) replaced the
    `descriptor_list`-nulling test pattern for logging assertions, as this section's "bonus"
    anticipated — the older `ScopedDescriptorListReset`-based fixtures that predate the seam still
    exist elsewhere in the suite and continue to work unchanged.
