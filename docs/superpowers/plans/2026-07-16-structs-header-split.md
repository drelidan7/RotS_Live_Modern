# structs.h God-Header Split Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Carve the 2302-line `src/structs.h` (included by 62 src TUs + 13 headers + 30 test files) into the `rots/core` header DAG of spec §5, migrate every consumer to precise pathed includes, delete `structs.h`, and stand up the `rots_core` L1 library — with zero behavior change and the i386 ABI layout byte-identical.

**Architecture:** New headers are born in their spec §9a locations (`src/core/include/rots/core/`, `src/persist/include/rots/persist/`). Content is **moved verbatim** (field order, types, comments) out of `structs.h`, which shrinks task-by-task into a pure umbrella; consumers are then migrated in per-future-library batches while the umbrella keeps everything else compiling; finally the 13 headers are trimmed to `fwd.h` + constants and the umbrella is deleted. `fwd.h` is the linchpin: entities reference each other only by pointer, so entity headers include `fwd.h`, never each other.

**Tech Stack:** C++20, CMake presets (`macos-arm64`, `linux-x64`, `linux-x86-legacy`, `windows-msvc`, `macos-arm64-asan`), flat `src/Makefile` + `src/tests/Makefile` (must keep working), GoogleTest, characterization goldens.

## Global Constraints

- **Zero behavior change.** All goldens (`CharacterizationCombatTest.*`, `CharacterizationJson.*`, `scripts/boot-golden.sh`) stay byte-for-byte green at every task.
- **ABI is sacred.** Every struct moves **verbatim** — member order, member types, array bounds, comments. No reordering, no "cleanup", no renaming. `structs.h` has no `#pragma pack`/`packed`/`alignas`/bit-fields, so layout = declaration order + natural padding; the layout probe (Task 1) must diff empty after every carve task. clang-format (incl. the PostToolUse hook) never reorders declarations, so auto-formatting is layout-safe; still avoid gratuitous reformatting of moved blocks.
- **`src/` must NEVER be added to any `-I`/`-iquote`/`/I` include path** — `src/limits.h` shadows `<limits.h>` (see the long comment at `src/tests/Makefile:32-77` and `src/CMakeLists.txt:388-433`). MSVC has no `-idirafter`. Therefore the new headers reference still-flat legacy headers (`platdef.h`, `color.h`, `protocol.h`) by **explicit relative path** (`"../../../../platdef.h"`), which resolves via "directory of the including file" on GCC/Clang/MSVC alike, with zero new flags. Only the two new leaf dirs `core/include` and `persist/include` (which contain nothing but `rots/`) go on include paths.
- **Pathed includes for the new tree:** consumers write `#include "rots/core/character.h"` etc. (spec §9a). Cross-references *inside* the new tree are also pathed (`#include "rots/core/fwd.h"`).
- **`.cpp` files do not move on disk in this wave** (deviation from spec §9a's eventual layout, recorded in Task 12): the flat `src/Makefile` still lists them, and physical relocation is deferred until the Makefile is retired. Only *new header files* live in the subfolder layout.
- **Line-range references** in the move maps below refer to the pristine pre-split file: `git show d8f5a73:src/structs.h`. Locate blocks by name in the current file when extracting (earlier tasks shift line numbers).
- **Reference lists (line numbers) → destination decisions come from the reviewed inventory**; if the actual file disagrees with a range boundary by a line or two (comments), move the whole logical block including its comment.
- **Verification cadence (AGENTS.local.md), per task:** native `macos-arm64` build + full ctest + `scripts/boot-golden.sh --native build/macos-arm64/ageland verify`, AND `rots64` container build + ctest + `scripts/boot-golden.sh --service rots64 verify`. Do NOT run the i386 battery per task — finalization only. Current ctest count: **1247** (incl. `PlatformLayerAcyclicity`); expect it unchanged unless a task says otherwise.
- **Warnings are errors** (`-Wall -Wextra -Werror`, `/W4 /WX`); `ROTS_SUPPRESS_TEST_WARNINGS` stays OFF.
- Never re-add `#include "structs.h"` to fix a compile error in a batch task — add the precise `rots/core`/`rots/persist` header instead.
- `rots64` invocations use `docker compose run --rm --pull never rots64 bash -lc '…'` (the `--pull never` avoids this host's registry-probe hang).

---

## File Structure

```
src/
  structs.h                      # shrinks per task → pure umbrella (Task 5) → DELETED (Task 12)
  core/include/rots/core/
    fwd.h                        # forward decls of the four entities + txt_block
    types.h                      # primitives include, global constants, enums, pure value structs
    tables.h                     # CONSTANTSMARK global + extern const table declarations
    object.h                     # ITEM_*/MATERIAL_*/LIQ_*/CONT_* + obj_data
    room.h                       # room constants + room_data_extension + room_data
    character.h                  # char constants + char-adjacent structs + char_data + group_roll
    descriptor.h                 # CON_* + snoop_data + descriptor_data
  persist/include/rots/persist/
    file_formats.h               # char_file_u, follower_file_elem, obj_file_elem, rent_info, RENT_*
  CMakeLists.txt                 # include dirs (Task 2); rots_core STATIC + linkcheck (Task 13)
  Makefile                       # ALL_CPPFLAGS gains -Icore/include -Ipersist/include (Task 2)
  tests/Makefile                 # TEST_CXXFLAGS gains -I../core/include -I../persist/include (Task 2)
  utility.cpp                    # receives get_guardian_type() (Task 13)
  consts.cpp                     # loses get_guardian_type(); joins rots_core (Task 13)
.superpowers/sdd/scratch-header-split/
  layout_probe.cpp               # ABI layout probe (Task 1; not committed)
  layout-baseline-{macos,rots64,i386}.txt
```

### Destination map (authoritative; ranges from `git show d8f5a73:src/structs.h`)

**→ `rots/core/types.h`** (in this order):
| Block | Lines |
|---|---|
| game constants (`MAX_SPIRITS`…`WORLD_AREA`, LEVEL_*, PULSE_*, SECS_*, name lengths) — **minus line 31 `MAX_ALIAS`** (moves to `utils.h`, it expands `GET_LEVEL`) | 32-113 |
| `#define NOWHERE` (single line, pulled forward from the room block — cross-cutting sentinel used by `interpre.h`) | within 528-593 |
| sizing consts `MAX_OBJ_AFFECT`, `OBJ_NOTIMER`; `WEAPON_POISON_DUR` | 385-386, 403 |
| sizing consts `MAX_TOUNGE`/`MAX_SKILLS`/`MAX_WEAR`/`MAX_AFFECT` (*DO*NOT*CHANGE* group) | 726-729 |
| `char_vector`/`char_iter`/`char_set`/... typedefs | 238-244 |
| `combat_result_struct` | 246-256 |
| `game_types::weapon_type` enum + `get_weapon_name` proto (stays with its enum) | 259-278 |
| `game_types::player_specs` enum | 841-863 |
| `source_type` enum | 1417-1424 |
| TAR_*/TARGET_* consts + `GET_TARGET_TEXT` macro; `target_data`; `extra_descr_data`; `waiting_type` | 292-401 |
| `obj_flag_data`; `weapon_flag_data`; `obj_affected_type` | 405-476 |
| `room_direction_data`; `room_track_data`; `room_bleed_data` | 596-605, 616-641 |
| `prof_type` | 835-838 |
| `time_info_data`; `time_data`; `char_player_data`; `char_ability_data`; `char_point_data` | 1040-1122 |
| `alias_list`; `free_alias_list` proto; `owned_alias_list` | 1139-1143, 1150, 1184-1257 |
| `char_special2_data` | 1381-1415 |
| `affection_source`; `affected_type` | 1426-1446 |
| `char_prof_data` | 1465-1475 |
| `player_skill_data`; `damage_details`; `timed_damage_details` | 1724-1768, 1796-1820 |
| `race_bodypart_data` | 1985-1990 |
| weather SUN_*/SKY_*/MOON_*/SEASON_* consts + `weather_data` | 1996-2034 |
| `txt_block`; `txt_q` | 2143-2151 |
| `msg_type`; `message_type`; `message_list`; `prompt_type` | 2251-2275 |
| `universal_list` | 2277-2288 |

**→ `rots/core/tables.h`:** `CONSTANTSMARK`-guarded `global_release_flag` def/extern (281-285); `#ifndef CONSTANTSMARK` extern table decls `weekdays`/`month_name`/`moon_phase`/`pc_races`/`pc_race_types`/`pc_race_keywords`/`pc_star_types`/`pc_named_star_types` (913-925). Preserve both `#ifdef` blocks verbatim — `consts.cpp` defines `CONSTANTSMARK` (`src/consts.cpp:10`) before its includes and that contract must survive.

**→ `rots/core/object.h`:** ITEM_* (type/wear/extra flags), MATERIAL_*, LIQ_*, CONT_* (116-236); `obj_data` (479-523).

**→ `rots/core/room.h`:** room constants (528-593 **minus the `NOWHERE` line**: room flags, BFS_*, directions NORTH..DOWN, EX_*, SECT_*); EXTENSION_*/NUM_OF_BLOOD_TRAILS/NUM_OF_TRACKS (608-613); `room_data_extension` (643-650); `room_data` (652-692). Drop the redundant `struct room_data;` re-declaration at 614 (it lives in `fwd.h`).

**→ `rots/core/character.h`:** char constants WEAR_*, DRUNK/FULL/THIRST, AFF_*, APPLY_*, ROOMAFF_*, PROF_*, LANG_* (700-834 **minus 726-729**); PLRSPEC_*/RACE_* (866-905); SEX_*/POSITION_*/MOB_*/PLR_*/PRF_* (928-1028); `memory_rec` (1030-1036); `struct special_list;` fwd (1263); `char_special_data` + its two unions (1265-1372); HIDING_* (1375-1376); `follow_type` (1448-1452); `mount_data_type` (1454-1463); `specialization_info` and the whole spec-data hierarchy through `heavy_fighting_data` (1477-1683); `specialization_data` (1686-1721); `player_damage_details` (1770-1794); `group_damaga_data` (1822-1842); `group_data` (1845-1889); `char_data` (1892-1983); `group_roll` (2290-2300 — **must stay after `char_data`**: its inline ctor dereferences `character_name->player.name`).

**→ `rots/core/descriptor.h`:** `snoop_data` (2200-2203); BLOCK_STR_LEN (2205-2207); CON_* + DFLAG_IS_SPAMMING (2155-2198); `descriptor_data` (2209-2249). Keep original file order (2155 block first).

**→ `rots/persist/file_formats.h`:** the whole persist region 2036-2141 verbatim in original order — the `char_file_u` comment block + struct (2036-2077), `follower_file_elem` (2079-2091), sentinels `SENTINEL_ITEM_ID_VALUE`/`DEPRECATED_ID_VALUE` (2097-2098), `obj_file_elem` (2093-2112), RENT_* (2114-2119), `rent_info` (2122-2137). These are the layout-locked on-disk formats — byte-for-byte moves, guarded by the layout probe and (at finalization) the i386 `legacy_*_fixture.bin` goldens.

### Consumer include rule (used by Tasks 6-11)

For each migrated TU, replace `#include "structs.h"` (tests: `#include "../structs.h"`) with the union of matches from this token table (grep the TU), keeping this order; then compile and add whatever the errors still demand — never `structs.h`:

| TU references (any of) | Include |
|---|---|
| `char_file_u`, `obj_file_elem`, `follower_file_elem`, `rent_info`, `RENT_` | `"rots/persist/file_formats.h"` |
| `char_data` deref / `GET_` / `IS_NPC`/`IS_AFFECTED` etc. macro use, `PLR_`, `PRF_`, `MOB_`, `AFF_`, `APPLY_`, `POSITION_`, `SEX_`, `RACE_`, `WEAR_`, `PROF_`, `LANG_`, `PLRSPEC_`, `follow_type`, `group_data`, `memory_rec`, `specialization`, `mount_data` | `"rots/core/character.h"` |
| `obj_data` deref, `ITEM_`, `MATERIAL_`, `LIQ_`, `CONT_` | `"rots/core/object.h"` |
| `room_data` deref, `world[`, `SECT_`, `EX_`, `NORTH`/`SOUTH`/`EAST`/`WEST`/`UP`/`DOWN` dir consts, `room_direction_data`, BFS_ | `"rots/core/room.h"` |
| `descriptor_data` deref, `CON_`, `txt_q`, `snoop`, `BLOCK_STR_LEN` | `"rots/core/descriptor.h"` |
| `weekdays`, `month_name`, `moon_phase`, `pc_races`, `pc_race_types`, `pc_race_keywords`, `pc_star_types`, `pc_named_star_types`, `global_release_flag` | `"rots/core/tables.h"` |
| anything else from the old structs.h (target_data, waiting_type, weather_data, message/prompt types, enums, typedefs, LEVEL_/PULSE_/MAX_ constants, …) or pointer-only entity use | `"rots/core/types.h"` (and `"rots/core/fwd.h"` alone suffices for pure pointer use) |

Do not over-minimize: keep every header the token scan selected even if the TU happens to compile without it (transitive luck is what we're eliminating).

---

## Task 1: ABI layout baseline (probe on all three hosts)

**Files:**
- Create: `.superpowers/sdd/scratch-header-split/layout_probe.cpp` (scratch — NOT committed)
- Create: `.superpowers/sdd/scratch-header-split/layout-baseline-{macos,rots64,i386}.txt`

**Interfaces:**
- Produces: the three baseline files that every carve task (2-5) diffs against, and the probe re-used in Task 12.

- [ ] **Step 1: Write the probe**

Create `.superpowers/sdd/scratch-header-split/layout_probe.cpp`:

```cpp
// ABI layout probe for the structs.h split. Prints sizeof/alignof for every
// moved aggregate, plus offsetof for EVERY member of the four on-disk persist
// structs (standard-layout, so offsetof is well-defined). Output must be
// byte-identical before and after each carve task on the same host.
#include "structs.h"
#include <cstddef>
#include <cstdio>
#define P(T) std::printf(#T " size=%zu align=%zu\n", sizeof(T), alignof(T))
#define PO(T, m) std::printf(#T "." #m " off=%zu\n", offsetof(T, m))
int main()
{
    P(combat_result_struct); P(target_data); P(extra_descr_data); P(waiting_type);
    P(obj_flag_data); P(weapon_flag_data); P(obj_affected_type); P(obj_data);
    P(room_direction_data); P(room_track_data); P(room_bleed_data);
    P(room_data_extension); P(room_data);
    P(prof_type); P(time_info_data); P(time_data); P(char_player_data);
    P(char_ability_data); P(char_point_data); P(alias_list); P(owned_alias_list);
    P(char_special_data); P(char_special2_data); P(affection_source);
    P(affected_type); P(follow_type); P(mount_data_type); P(char_prof_data);
    P(specialization_data); P(player_skill_data); P(damage_details);
    P(player_damage_details); P(timed_damage_details); P(group_data);
    P(char_data); P(group_roll); P(race_bodypart_data); P(weather_data);
    P(memory_rec); P(universal_list); P(txt_block); P(txt_q); P(snoop_data);
    P(descriptor_data); P(msg_type); P(message_type); P(message_list);
    P(prompt_type);
    P(char_file_u); P(follower_file_elem); P(obj_file_elem); P(rent_info);
    // PO(...) lines: open structs.h and emit one PO line per member, in
    // declaration order, for each of: char_file_u, follower_file_elem,
    // obj_file_elem, rent_info. (Members read off the actual definitions —
    // these four are the on-disk formats and get full member-offset coverage.)
    return 0;
}
```

Fill in the `PO(...)` lines from the real member lists before compiling (one line per member of the four persist structs, declaration order).

- [ ] **Step 2: Capture the macOS baseline**

```bash
cd src
c++ -std=c++20 -funsigned-char ../.superpowers/sdd/scratch-header-split/layout_probe.cpp -o ../.superpowers/sdd/scratch-header-split/probe-macos
../.superpowers/sdd/scratch-header-split/probe-macos > ../.superpowers/sdd/scratch-header-split/layout-baseline-macos.txt
wc -l ../.superpowers/sdd/scratch-header-split/layout-baseline-macos.txt
```
Expected: compiles clean; baseline has ~50 `size=` lines + the PO lines (typically ~40-60 more).

- [ ] **Step 3: Capture the rots64 baseline**

```bash
docker compose run --rm --pull never rots64 bash -lc 'cd /rots/src && g++ -std=c++20 -funsigned-char /rots/.superpowers/sdd/scratch-header-split/layout_probe.cpp -o /tmp/probe && /tmp/probe' > .superpowers/sdd/scratch-header-split/layout-baseline-rots64.txt
```
Expected: same line count as macOS (values differ — that's fine, we diff per-host).

- [ ] **Step 4: Capture the i386 baseline**

```bash
docker compose run --rm --pull never rots bash -lc 'cd /rots/src && g++ -m32 -std=c++20 -funsigned-char /rots/.superpowers/sdd/scratch-header-split/layout_probe.cpp -o /tmp/probe && /tmp/probe' > .superpowers/sdd/scratch-header-split/layout-baseline-i386.txt
```
Expected: same line count; this is the shipping-ABI baseline diffed at Task 5 and finalization. (Single-TU qemu compile — minutes, not the full battery.)

- [ ] **Step 5: No commit** — scratch artifacts only. Record the three files' existence in the progress notes.

---

## Task 2: Include-path wiring + carve `fwd.h`, `types.h`, `tables.h`

**Files:**
- Create: `src/core/include/rots/core/fwd.h`, `src/core/include/rots/core/types.h`, `src/core/include/rots/core/tables.h`
- Modify: `src/structs.h` (remove moved blocks; include the new headers in their place), `src/utils.h` (receives `MAX_ALIAS`), `src/Makefile:` `ALL_CPPFLAGS`, `src/tests/Makefile:` `TEST_CXXFLAGS` (~line 80), `src/CMakeLists.txt` (include dirs for `ageland` and `ageland_tests`)

**Interfaces:**
- Produces: `#include "rots/core/fwd.h"` / `"rots/core/types.h"` / `"rots/core/tables.h"` resolvable from every build system; `structs.h` still provides the full old surface transitively. All later tasks depend on this wiring.

- [ ] **Step 1: Wire the include dirs in all four build systems**

`src/Makefile` — change `ALL_CPPFLAGS = $(DEPFLAGS) $(CPPFLAGS)` to:
```make
ALL_CPPFLAGS = $(DEPFLAGS) -Icore/include -Ipersist/include $(CPPFLAGS)
```
`src/tests/Makefile` — change `TEST_CXXFLAGS = $(CXXFLAGS) -idirafter ..` to:
```make
TEST_CXXFLAGS = $(CXXFLAGS) -I../core/include -I../persist/include -idirafter ..
```
`src/CMakeLists.txt` — after the `ageland` target definition add:
```cmake
# Pathed data-model headers (spec §9a). ONLY these two leaf dirs go on the
# include path — never src/ itself (src/limits.h would shadow <limits.h>).
# They contain nothing but rots/, so they cannot shadow any system header.
target_include_directories(ageland PRIVATE core/include persist/include)
```
and give `ageland_tests` the same two dirs (plain `target_include_directories(ageland_tests PRIVATE core/include persist/include)` next to its existing include setup — do NOT touch its `-idirafter`/`-iquote` COMPILE_OPTIONS).

Create the directories with a placeholder so the paths exist before Step 2: they'll be populated in Steps 2-4.

- [ ] **Step 2: Create `fwd.h`**

```cpp
#pragma once
// rots/core/fwd.h — forward declarations of the core entities (spec §5).
// Entity headers include THIS, never each other: entities reference one
// another only by pointer, so full definitions are pulled in only by the
// .cpp files (and headers) that actually dereference members.
struct char_data;
struct obj_data;
struct room_data;
struct descriptor_data;
struct txt_block; // defined in rots/core/types.h; held by pointer in target_data
```

- [ ] **Step 3: Create `types.h` and `tables.h` by MOVING the mapped blocks out of `structs.h`**

`types.h` skeleton — moved blocks land between the includes and the end, in the destination-map order:
```cpp
#pragma once
// rots/core/types.h — leaf of the data-model DAG (spec §5): primitive-typedef
// include, global constants, enums, and pure value structs. Entity types are
// visible only as forward declarations (rots/core/fwd.h); this header defines
// no entity and includes no entity header.
//
// The two relative includes below reach headers that still live flat in src/.
// They are relative on purpose: src/ must never join an include path
// (src/limits.h shadows <limits.h>, and MSVC has no -idirafter), and the
// "directory of the including file" rule resolves these identically on
// GCC/Clang/MSVC. They become pathed when platform/app headers relocate.
#include "../../../../color.h" // color_slot_data, MAX_COLOR_FIELDS (char_prof_data)
#include "../../../../platdef.h" // sh_int/ush_int/byte/sbyte/ubyte, SocketType
#include "rots/core/fwd.h"

#include <algorithm>
#include <assert.h>
#include <set>
#include <stdio.h>
#include <string_view>
#include <sys/types.h>
#include <vector>

// … moved blocks, in destination-map order …
```
`tables.h` skeleton:
```cpp
#pragma once
// rots/core/tables.h — extern declarations of the const data tables defined in
// consts.cpp, plus the CONSTANTSMARK definition trick: consts.cpp #defines
// CONSTANTSMARK before including this header so the guarded lines below become
// the definitions in exactly one TU. Preserved verbatim from structs.h.
#include <array>
#include <string_view>

// … moved blocks 281-285 and 913-925, both #ifdef guards intact …
```
Move rules for this step:
- Cut each mapped block from `structs.h` and paste verbatim (comments included).
- `MAX_ALIAS` (old line 31) moves to `src/utils.h`, placed directly above the `GET_LEVEL` macro definition it expands, with its original comment.
- At the top of `structs.h` (right after the include guard + its existing `#include` block), add:
```cpp
#include "rots/core/fwd.h"
#include "rots/core/tables.h"
#include "rots/core/types.h"
```
- Leave `structs.h`'s own std/`platdef.h`/`color.h`/`protocol.h` includes in place for now (Task 5 rewrites it as a minimal umbrella).
- If the compiler reports a std header the moved blocks needed that isn't in the skeleton list, add it to `types.h` (not the umbrella).

- [ ] **Step 4: Build + full test suite, macOS**

```bash
cd src
cmake --preset macos-arm64 && cmake --build --preset macos-arm64 -j4
ctest --preset macos-arm64
```
Expected: clean build, 1247/1247 (normal platform skips).

- [ ] **Step 5: Layout probe diff (macOS)**

```bash
../.superpowers/sdd/scratch-header-split/probe-macos >/dev/null 2>&1 || true  # stale binary — rebuild:
c++ -std=c++20 -funsigned-char ../.superpowers/sdd/scratch-header-split/layout_probe.cpp -o ../.superpowers/sdd/scratch-header-split/probe-macos
../.superpowers/sdd/scratch-header-split/probe-macos | diff - ../.superpowers/sdd/scratch-header-split/layout-baseline-macos.txt
```
Expected: **empty diff.** Any difference = a move changed layout; stop and fix before proceeding.

- [ ] **Step 6: Boot golden (native) + rots64 gate**

```bash
cd ..
scripts/boot-golden.sh --native build/macos-arm64/ageland verify
docker compose run --rm --pull never rots64 bash -lc 'cd /rots/src && cmake --preset linux-x64 && cmake --build --preset linux-x64 -j"$(nproc)" && ctest --preset linux-x64'
scripts/boot-golden.sh --service rots64 verify
```
Expected: both goldens PASS, rots64 1247/1247.

- [ ] **Step 7: Flat Makefile smoke (the wiring change is Makefile-visible)**

```bash
docker compose run --rm --pull never rots64 bash -lc 'cd /rots/src && make clean && make -j"$(nproc)" all'
```
Expected: flat build succeeds with the new `-I` flags. (64-bit container is fine for the wiring check; i386 runs at finalization.)

- [ ] **Step 8: Commit**

```bash
git add src/core src/structs.h src/utils.h src/Makefile src/tests/Makefile src/CMakeLists.txt
git commit -m "refactor: carve rots/core fwd.h+types.h+tables.h out of structs.h; wire pathed include dirs"
```

---

## Task 3: Carve `object.h` and `room.h`

**Files:**
- Create: `src/core/include/rots/core/object.h`, `src/core/include/rots/core/room.h`
- Modify: `src/structs.h`

**Interfaces:**
- Consumes: `rots/core/types.h`, `rots/core/fwd.h` (Task 2).
- Produces: `#include "rots/core/object.h"` (full `obj_data`), `#include "rots/core/room.h"` (full `room_data`).

- [ ] **Step 1: Create the two headers; move the mapped blocks**

Both start:
```cpp
#pragma once
// rots/core/object.h — object flag/material/liquid/container constants and the
// obj_data entity. Other entities are pointer-only (rots/core/fwd.h).
#include "rots/core/fwd.h"
#include "rots/core/types.h"
```
(equivalent comment for `room.h`). Move the destination-map blocks verbatim, preserving order. In `room.h`, remember: the `NOWHERE` line already left for `types.h` in Task 2 — if it didn't (it was listed in the Task 2 map), move it now and note it; drop the redundant `struct room_data;` line (old 614). In `structs.h`, replace the removed regions with, at the position of the first removed object line:
```cpp
#include "rots/core/object.h"
```
and at the position of the first removed room line:
```cpp
#include "rots/core/room.h"
```

- [ ] **Step 2: Build + tests + probe diff + boot goldens (both hosts)**

Same commands as Task 2 Steps 4-6 (macOS build/ctest, probe diff vs `layout-baseline-macos.txt` must be empty, native boot golden, rots64 build/ctest/boot golden). Expected: all green, 1247/1247 both hosts, empty probe diff.

- [ ] **Step 3: Commit**

```bash
git add src/core src/structs.h
git commit -m "refactor: carve rots/core object.h and room.h out of structs.h"
```

---

## Task 4: Carve `character.h` and `descriptor.h`

**Files:**
- Create: `src/core/include/rots/core/character.h`, `src/core/include/rots/core/descriptor.h`
- Modify: `src/structs.h`

**Interfaces:**
- Consumes: `rots/core/types.h`, `rots/core/fwd.h`.
- Produces: `#include "rots/core/character.h"` (full `char_data` + char-adjacent types), `#include "rots/core/descriptor.h"` (full `descriptor_data`).

- [ ] **Step 1: Create `character.h`; move the mapped blocks in original relative order**

```cpp
#pragma once
// rots/core/character.h — character constants (WEAR_/AFF_/APPLY_/PLR_/PRF_/…),
// the character-owned relationship/spec-data structs, and the char_data entity.
// Objects/rooms/descriptors are pointer-only (rots/core/fwd.h).
#include "rots/core/fwd.h"
#include "rots/core/types.h"

#include <map>
#include <string>
```
Ordering constraints (all satisfied by keeping original file order): `memory_rec` before `char_special_data`; the `struct special_list;` forward declaration (old line 1263) stays directly above `char_special_data`; `group_damaga_data` before `group_data`; everything before `char_data`; **`group_roll` last** (its inline ctor needs the full `char_data`).

- [ ] **Step 2: Create `descriptor.h`; move the mapped blocks**

```cpp
#pragma once
// rots/core/descriptor.h — connection-state constants (CON_*), snoop_data, and
// the descriptor_data session struct. char_data is pointer-only (fwd.h).
// protocol.h still lives flat in src/ — relative include on purpose, see
// rots/core/types.h for the rationale.
#include "../../../../protocol.h" // protocol_t
#include "rots/core/fwd.h"
#include "rots/core/types.h"
```
Move blocks in original order (2155-2249 region). In `structs.h`, replace each removed region with the corresponding `#include "rots/core/character.h"` / `#include "rots/core/descriptor.h"` at the first removed line's position.

- [ ] **Step 3: Build + tests + probe diff + boot goldens (both hosts)** — same commands/expectations as Task 2 Steps 4-6. `char_data`/`descriptor_data` sizes in the probe must match baseline exactly.

- [ ] **Step 4: Commit**

```bash
git add src/core src/structs.h
git commit -m "refactor: carve rots/core character.h and descriptor.h out of structs.h"
```

---

## Task 5: Carve `rots/persist/file_formats.h`; `structs.h` becomes a pure umbrella

**Files:**
- Create: `src/persist/include/rots/persist/file_formats.h`
- Modify: `src/structs.h` (final rewrite to pure umbrella)

**Interfaces:**
- Produces: `#include "rots/persist/file_formats.h"` (char_file_u, follower_file_elem, obj_file_elem, rent_info, RENT_*, sentinels); `structs.h` as a ~15-line umbrella (unchanged external surface).

- [ ] **Step 1: Create `file_formats.h`**

```cpp
#pragma once
// rots/persist/file_formats.h — the on-disk/legacy player and rent file
// formats (spec §5: persistence types, not core entity types). LAYOUT-LOCKED:
// these structs define historical binary formats decoded by the migration
// converters; member order and types are ABI. legacy_*_fixture.bin goldens
// (32-bit only) and the layout probe guard them.
#include "rots/core/types.h"

// … lines 2036-2141 of pre-split structs.h, verbatim, original order …
```

- [ ] **Step 2: Rewrite `structs.h` as a pure umbrella**

The whole file becomes:
```cpp
/* ************************************************************************
 *   File: structs.h                                    Part of CircleMUD *
 *  Transitional umbrella over the carved data-model headers (spec §5,     *
 *  docs/superpowers/specs/2026-07-16-library-architecture-design.md).     *
 *  New code includes the precise rots/core / rots/persist headers; this   *
 *  file is deleted once every consumer is migrated (header-split plan).   *
 ************************************************************************ */
#ifndef STRUCTS_H
#define STRUCTS_H

#include "rots/core/fwd.h"
#include "rots/core/types.h"
#include "rots/core/tables.h"
#include "rots/core/object.h"
#include "rots/core/room.h"
#include "rots/core/character.h"
#include "rots/core/descriptor.h"
#include "rots/persist/file_formats.h"

#endif /* STRUCTS_H */
```
(The old `<map>`/`<set>`/… std includes drop out here; any TU that transitively relied on them will fail loudly and gets the std include added to itself — record such fixes in the commit message.)

- [ ] **Step 3: Build + tests + probe diff on macOS AND rots64 AND i386-probe**

macOS + rots64: same as Task 2 Steps 4-6. Additionally re-run the probe on rots64 and i386 and diff against their baselines:
```bash
docker compose run --rm --pull never rots64 bash -lc 'cd /rots/src && g++ -std=c++20 -funsigned-char /rots/.superpowers/sdd/scratch-header-split/layout_probe.cpp -o /tmp/probe && /tmp/probe' | diff - .superpowers/sdd/scratch-header-split/layout-baseline-rots64.txt
docker compose run --rm --pull never rots bash -lc 'cd /rots/src && g++ -m32 -std=c++20 -funsigned-char /rots/.superpowers/sdd/scratch-header-split/layout_probe.cpp -o /tmp/probe && /tmp/probe' | diff - .superpowers/sdd/scratch-header-split/layout-baseline-i386.txt
```
Expected: **both diffs empty** — the full carve is layout-neutral on all three ABIs.

- [ ] **Step 4: Commit**

```bash
git add src/persist src/structs.h
git commit -m "refactor: carve rots/persist file_formats.h; structs.h is now a pure umbrella"
```

---

## Tasks 6-10: Migrate `.cpp` consumers to precise includes (five batches)

All five batch tasks share this shape. **Per-file procedure:** apply the *Consumer include rule* table; direct includers get `#include "structs.h"` **replaced** by the computed set (at the same position); transitive-only files (marked *(t)* below — they don't include structs.h at all today) get the computed set **added** after their existing project includes. Then run the full per-task gate.

**Steps for every batch task:**
- [ ] Step 1: Migrate every file in the batch per the procedure above.
- [ ] Step 2: `cd src && cmake --build --preset macos-arm64 -j4` — fix any missing-include error by adding the indicated precise header (or a std header) to the offending TU. Never `structs.h`.
- [ ] Step 3: `ctest --preset macos-arm64` → 1247/1247.
- [ ] Step 4: `cd .. && scripts/boot-golden.sh --native build/macos-arm64/ageland verify` → PASS.
- [ ] Step 5: rots64 build + ctest + boot golden (commands as in Task 2 Step 6) → all green.
- [ ] Step 6: Commit with the given message.

### Task 6: Combat batch (16 TUs)

**Files (modify):** `fight.cpp, limits.cpp, skill_timer.cpp, mobact.cpp, ranger.cpp, clerics.cpp, mage.cpp, mystic.cpp, profs.cpp, spell_pa.cpp, spec_pro.cpp, spec_ass.cpp, olog_hai.cpp, battle_mage_handler.cpp (t), weapon_master_handler.cpp (t), wild_fighting_handler.cpp (t)` — all under `src/`.

Commit: `git commit -m "refactor: combat TUs to precise rots/core includes"`

### Task 7: World batch (15 TUs)

**Files (modify):** `db.cpp, shapemdl.cpp, shapemob.cpp, shapeobj.cpp, shaperom.cpp, shapescript.cpp, shapezon.cpp, zone.cpp, script.cpp, mudlle.cpp, mudlle2.cpp, graph.cpp, weather.cpp, mob_csv_extract.cpp, obj2html.cpp`. Note `db.cpp` also matches the persist row (`char_file_u`, RENT_) — it gets `rots/persist/file_formats.h` too.

Commit: `git commit -m "refactor: world TUs to precise rots/core includes"`

### Task 8: Persist batch (11 TUs)

**Files (modify):** `objsave.cpp, boards.cpp, mail.cpp, pkill.cpp, convert_exploits.cpp, convert_plrobjs.cpp, save_benchmark.cpp, savebench.cpp, character_json.cpp (t), objects_json.cpp (t), account_management.cpp (t)`. (`account_management.cpp`'s six `#include`d fragment files inherit its includes — touch only the parent TU.)

Commit: `git commit -m "refactor: persist TUs to precise rots/core + rots/persist includes"`

### Task 9: Commands batch (15 TUs)

**Files (modify):** `interpre.cpp, act_comm.cpp, act_info.cpp, act_move.cpp, act_obj1.cpp, act_obj2.cpp, act_offe.cpp, act_othe.cpp, act_soci.cpp, act_wiz.cpp, modify.cpp, delayed_command_interpreter.cpp, wait_functions.cpp, shop.cpp, ban.cpp`.

Commit: `git commit -m "refactor: command TUs to precise rots/core includes"`

### Task 10: Entity + app batch (14 TUs)

**Files (modify):** `char_utils.cpp, char_utils_combat.cpp, object_utils.cpp, environment_utils.cpp, handler.cpp, utility.cpp, comm.cpp, signals.cpp, big_brother.cpp, config.cpp, consts.cpp, color.cpp (t), protocol.cpp (t), safe_template.cpp (t)`.

⚠ `handler.cpp` has mixed CRLF line endings on this machine — if the edit tooling or format hook mangles it, fall back to a byte-level scripted edit (python) as done in prior waves, and verify `git diff` shows only the include lines changing.

Commit: `git commit -m "refactor: entity/app TUs to precise rots/core includes"`

---

## Task 11: Test-suite batch (30 files)

**Files (modify)** — all under `src/tests/`, replacing `#include "../structs.h"` with pathed includes per the same rule table (the `-I../core/include -I../persist/include` wiring from Task 2 makes them resolve):
26 test TUs: `act_comm_alias_tests.cpp, act_format_tests.cpp, act_info_format_tests.cpp, act_wiz_format_tests.cpp, act_wiz_tests.cpp, characterization_json_tests.cpp, color_tests.cpp, comm_act_tests.cpp, comm_output_tests.cpp, db_save_roundtrip_tests.cpp, interpre_account_menu_tests.cpp, json_perf_tests.cpp, llp64_probe_tests.cpp, obj_flag_data_tests.cpp, object_utils_tests.cpp, objects_json_layout_tests.cpp, protocol_tests.cpp, prompt_format_tests.cpp, safe_template_tests.cpp, save_benchmark_tests.cpp, shape_format_tests.cpp, shapemob_tests.cpp, spec_pro_tests.cpp, spell_pa_tests.cpp, startup_options_tests.cpp, utility_format_tests.cpp`
4 fixture headers: `CharPlayerDataBuilder.h, ObjFlagDataBuilder.h, test_char_cleanup.h, test_world.h`.

- [ ] Step 1: Migrate all 30 files per the rule table.
- [ ] Step 2-5: Full per-task gate (macOS build/ctest/boot golden; rots64 build/ctest/boot golden) → 1247/1247 both hosts.
- [ ] Step 6: Also build the monolithic test Makefile on rots64 as a smoke for its `-I` wiring:
```bash
docker compose run --rm --pull never rots64 bash -lc 'cd /rots/src/tests && make clean && make -j"$(nproc)" tests && ../../bin/tests --gtest_list_tests | head'
```
Expected: builds and lists tests. (Full monolithic run happens on i386 at finalization.)
- [ ] Step 7: Commit — `git commit -m "refactor: test suite to precise rots/core includes"`

---

## Task 12: Trim the 13 headers; delete `structs.h`

**Files:**
- Modify: `src/utils.h, src/db.h, src/handler.h, src/interpre.h, src/spells.h, src/char_utils.h, src/account_management_types.h, src/character_json.h, src/objects_json.h, src/mob_csv_extract.h, src/script.h, src/mudlle.h, src/warrior_spec_handlers.h`
- Delete: `src/structs.h`
- Modify: `docs/superpowers/specs/2026-07-16-library-architecture-design.md` (as-built §5 note), `.superpowers/sdd/scratch-header-split/layout_probe.cpp`

**Interfaces:**
- Produces: no file in the repo includes `structs.h`; headers expose only `fwd.h` + the constants they genuinely need. This is the compile-cascade payoff.

- [ ] **Step 1: Replace `#include "structs.h"` in each header** per this table (then let the compiler adjudicate — add the *smallest* extra precise header a genuine error demands, remove nothing else):

| Header | Replacement |
|---|---|
| `utils.h` | `"rots/core/types.h"` + `"rots/core/fwd.h"` (constants + `weather_data` extern + macros that expand at call sites) |
| `db.h` | `"rots/core/types.h"` + `"rots/core/fwd.h"` |
| `handler.h` | `"rots/core/fwd.h"` + `"rots/persist/file_formats.h"` (RENT_CRASH default arg) |
| `interpre.h` | `"rots/core/types.h"` (NOWHERE, waiting_type in ACMD macros) + `"rots/core/fwd.h"` |
| `spells.h` | `"rots/core/types.h"` (MAX_SKILLS) + `"rots/core/fwd.h"` |
| `char_utils.h` | `"rots/core/fwd.h"` only (it already forward-declares; drop its own redundant fwds if now duplicated — identical re-declarations are legal, prefer the single fwd.h) |
| `account_management_types.h` | remove the include entirely |
| `character_json.h` | `"rots/persist/file_formats.h"` |
| `objects_json.h` | remove the include entirely |
| `mob_csv_extract.h`, `script.h`, `warrior_spec_handlers.h` | `"rots/core/fwd.h"` |
| `mudlle.h` | `"rots/core/types.h"` (txt_block, TARGET_*) + `"rots/core/fwd.h"` |

- [ ] **Step 2: Delete the umbrella and prove nothing references it**

```bash
git rm src/structs.h
grep -rn '"structs.h"\|structs\.h"' src/ && echo "STRAGGLERS FOUND" || echo "clean"
```
Expected: `clean`. Any straggler gets precise includes (it slipped through a batch), not a resurrection of the umbrella.

- [ ] **Step 3: Update the layout probe** to include the new headers instead of `structs.h`:
```cpp
#include "rots/core/character.h"
#include "rots/core/descriptor.h"
#include "rots/core/object.h"
#include "rots/core/room.h"
#include "rots/persist/file_formats.h"
```
Rebuild it and diff against all three baselines (macOS locally; rots64 + i386 via the Task 5 Step 3 commands, adding `-Icore/include -Ipersist/include` to the probe compile line now that the umbrella is gone):
```bash
c++ -std=c++20 -funsigned-char -Icore/include -Ipersist/include ../.superpowers/sdd/scratch-header-split/layout_probe.cpp -o ../.superpowers/sdd/scratch-header-split/probe-macos
../.superpowers/sdd/scratch-header-split/probe-macos | diff - ../.superpowers/sdd/scratch-header-split/layout-baseline-macos.txt
```
Expected: empty diffs on all three.

- [ ] **Step 4: Full gates both hosts** (macOS build/ctest/boot; rots64 build/ctest/boot) → 1247/1247, goldens PASS. Expect a tail of missing-include fixes in TUs during the first build — that is this task working as designed; fix each with precise includes and keep a list for the commit message.

- [ ] **Step 5: Record as-built deviations in the spec** (append a short "As built (header-split wave)" note to §5): (a) `types.h` includes `fwd.h` — `target_data`/`waiting_type` hold entity pointers, so the §5 wording "NO entity pointers" is amended to "no entity *definitions*"; (b) a `tables.h` exists for the CONSTANTSMARK extern tables; (c) persistence formats landed in `rots/persist/file_formats.h`; (d) core headers reach `platdef.h`/`color.h`/`protocol.h` by relative include until those relocate; (e) `.cpp` physical relocation deferred until the flat Makefile is retired.

- [ ] **Step 6: Commit**

```bash
git add -A src/ docs/superpowers/specs/2026-07-16-library-architecture-design.md
git commit -m "refactor: trim god-header includes to fwd/constants; delete structs.h umbrella"
```

---

## Task 13: Stand up `rots_core` (L1) with acyclicity link check

**Files:**
- Modify: `src/consts.cpp` (remove `get_guardian_type`), `src/utility.cpp` (receives it), `src/CMakeLists.txt` (rots_core STATIC + linkcheck + source-list split), `docs/BUILD.md` (library table + include-layout note)

**Interfaces:**
- Consumes: `rots_build_flags`, the Task 2 include wiring, the existing `rots_platform_linkcheck` pattern (`src/CMakeLists.txt`, CTest `PlatformLayerAcyclicity`, `src/tests/platform_linkcheck_main.cpp`).
- Produces: `rots_core` STATIC target + `RotS::core` alias owning `target_include_directories(rots_core PUBLIC core/include)`; `ageland` links it; CTest `CoreLayerAcyclicity`.

- [ ] **Step 1: Relocate `get_guardian_type` (the one upward edge in consts.cpp)**

Move the function body at `src/consts.cpp:2609` (declared `src/utils.h:774`; callers `objsave.cpp:1142`, `profs.cpp:233`) into `src/utility.cpp`, keeping the signature `int get_guardian_type(int race_number, const char_data* in_guardian_mob)`. Bring the exact `extern` declarations it needs (`mob_index` — defined `db.cpp:85` — and the `guardian_mob` table, copying the precise declared types from `consts.cpp`). `consts.cpp` keeps the `guardian_mob` table definition. Build + ctest on macOS: green, 1247/1247.

- [ ] **Step 2: Define `rots_core` and link it**

In `src/CMakeLists.txt`, following the `rots_platform` pattern exactly: add `set(ROTS_CORE_SOURCES consts.cpp config.cpp)`, remove those two from `ROTS_SERVER_SOURCES`, then:
```cmake
# --- L1: core data-model library ---------------------------------------------
add_library(rots_core STATIC ${ROTS_CORE_SOURCES})
add_library(RotS::core ALIAS rots_core)
set_target_properties(rots_core PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(rots_core PUBLIC rots_build_flags)
# The carved data-model headers (spec §5/§9a). PUBLIC: every consumer of the
# core data model gets the pathed include root by linking RotS::core.
target_include_directories(rots_core PUBLIC core/include)
```
`ageland` links `RotS::core` (keep the direct `persist/include` include-dir on `ageland`/`ageland_tests` until `rots_persist` exists; move the `core/include` dir off `ageland`/`ageland_tests` onto the library — but `ageland_tests` does NOT link `rots_core`; it keeps compiling `consts.cpp`/`config.cpp` directly (TESTING parity, same as the platform pattern), so `ageland_tests` keeps its own `target_include_directories(... core/include ...)`.

- [ ] **Step 3: Acyclicity link check**

Clone the `rots_platform_linkcheck` block (same CMakeLists file) into a `rots_core_linkcheck` that whole-archive-links `librots_core.a` (allowed lower layers: `rots_platform`, libc/libstdc++) with CTest name `CoreLayerAcyclicity`, gated the same way (`BUILD_TESTING`, `if(NOT MSVC)` if that's how the platform check is gated — mirror it exactly, including `LINK_DEPENDS`). Also append `rots_core_linkcheck` to the root `Makefile`'s `test` recipe target list (it builds named targets, not `all` — omitting a linkcheck binary leaves its CTest "Not Run", exactly the i386-battery failure fixed on master just before this plan).
**Contingency (decision rule, not a question):** if the link surfaces upward edges in `consts.cpp` beyond `get_guardian_type` (e.g. tables embedding function pointers to spell/command code), do NOT chase them in this task: revert consts.cpp to `ROTS_SERVER_SOURCES`, ship `rots_core = {config.cpp}` + headers, and record each surfaced edge in the progress notes and the spec's §3 caveats as follow-on weld-cutting work. `config.cpp` is verified clean (pure data).

- [ ] **Step 4: Negative test the link check** — temporarily add `extern void char_to_room(void*, int); void* probe(){return (void*)&char_to_room;}` to `config.cpp`, confirm `CoreLayerAcyclicity` FAILS to link, revert, rebuild clean.

- [ ] **Step 5: Full gates both hosts** (macOS build/ctest/boot; rots64 build/ctest/boot). Expected: **1248** tests now (the new `CoreLayerAcyclicity`), goldens PASS.

- [ ] **Step 6: Update `docs/BUILD.md`** — extend the "Library layering & the foundation acyclicity check" section with `rots_core` (membership, the PUBLIC include dir, the new CTest name) and a short "Pathed data-model includes" note (the two include roots, the relative-include rationale, the `src/limits.h` shadow rule).

- [ ] **Step 7: Commit**

```bash
git add src/consts.cpp src/utility.cpp src/CMakeLists.txt docs/BUILD.md
git commit -m "build: extract rots_core STATIC (consts+config, carved headers) + CoreLayerAcyclicity check"
```

---

## Finalization (once, before merge — not per-task)

- [ ] i386 battery (sequential, per AGENTS.local.md): container `make test`; monolithic runner from `/rots/src/tests` (`make clean && make tests && ../../bin/tests`); `scripts/boot-golden.sh verify`.
- [ ] Flat i386 Makefile build: `docker compose run --rm --pull never rots bash -lc 'cd /rots/src && make clean && make all'`.
- [ ] `linux-x86-legacy` preset: configure + build + ctest in the `rots` container.
- [ ] i386 layout-probe diff vs `layout-baseline-i386.txt` (probe now includes the carved headers; compile with `-Icore/include -Ipersist/include`): must be empty.
- [ ] One `macos-arm64-asan` configure/build/ctest pass (safety net for the include churn; no new test files were added, so this is belt-and-braces, not the new-test rule).
- [ ] Push and confirm all six blocking CI jobs green (incl. `windows-msvc` — the only host that exercises MSVC include resolution for the new tree).
- [ ] Whole-branch review, then superpowers:finishing-a-development-branch.

---

## Self-Review Notes

- **Spec coverage:** §5 header DAG → Tasks 2-5; §5 persistence-types relocation → Task 5; §9a pathed includes + header subfolder layout → Tasks 2-5 (with the `.cpp`-relocation deferral recorded in Task 12); consumer migration enabling the recompile win → Tasks 6-12; §3 L1 `rots_core` + §11 acyclicity enforcement → Task 13. §4 (`db.cpp`/`rots_convert`), §6-8 are later waves by design.
- **Ordering is the safety mechanism:** consumers gain precise includes (Tasks 6-11) while the umbrella still guarantees compilation; only then are transitive pulls removed (Task 12), so breakage surfaces as compile errors in exactly the files a batch missed, never as silent behavior change.
- **ABI:** verbatim moves + no pack/bitfield content (verified by inventory) + per-task layout-probe diffs on the host ABIs + i386 probe at Tasks 1/5/finalization + the 32-bit fixture goldens at finalization.
- **The `limits.h` trap is designed around** (relative includes from the new tree; only leaf dirs on include paths) rather than discovered mid-wave; MSVC include resolution (stack-based quoted lookup) is covered by the same relative-include choice and verified by the `windows-msvc` CI job at finalization.
- **Type-consistency check:** header names (`fwd.h`, `types.h`, `tables.h`, `object.h`, `room.h`, `character.h`, `descriptor.h`, `file_formats.h`) and target names (`rots_core`, `RotS::core`, `CoreLayerAcyclicity`) are used identically across tasks; `get_guardian_type` keeps its exact signature and `utils.h:774` declaration.
