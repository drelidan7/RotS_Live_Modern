# Room-Affect Caster Snapshot (TASK-021) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Every room affect (blaze, mist of baazunga, haze, room-poison) snapshots its caster's casting state at cast time and ticks from that snapshot; tick kills — and poison DoT kills — are credited to the original caster.

**Architecture:** A POD `caster_snapshot` (the thirteen formula inputs + identity) is captured once by `caster_snapshot::capture(const char_data&)`. The combat formula helpers gain overloads that take `const caster_snapshot&`; their existing `char_data*` forms become one-line adapters, so the live cast and the per-tick re-cast share one formula path. Room affects keep their snapshot in a side table keyed by `(room id, spell)` owned by `entity_lifecycle.cpp` (NOT inside `affected_type`, which is embedded in the legacy binary file layout in `src/persist/include/rots/persist/file_formats.h:39` and must not change size). `affect_update_room()` stops re-running whole spell bodies and instead calls per-spell tick functions in a new `src/combat/room_affect_tick.cpp`. Kill credit flows through a new `damage_credited()` in `fight.cpp` whose `credited_killer` reaches `die()`/`raw_kill()` while the *engaging* attacker stays the victim itself when the caster is absent or in another room (so no cross-room `set_fighting`). Poison DoT credit lives on the poisoned character (`char_data::specials.poisoned_by_*`), set when the poison affect is joined and consumed by `point_update`'s poison tick; `raw_kill`'s `died_to_player = attack_type == SPELL_POISON || …` heuristic is replaced by the recorded origin.

**Tech Stack:** C++20, GoogleTest (`src/tests/`, both `src/CMakeLists.txt` and the flat `src/tests/Makefile`), the repo's deterministic `number()` queue (`test_random_utils.h`), the `extract_char` hook seam (`entity_hooks.h`), the RR census (`tools/room_resolve_census.py`).

**Spec:** `backlog/tasks/task-021 - Room-affects-snapshot-the-casters-casting-state-at-cast-time-...md` (the owner-ruled design, 2026-08-22) — read it in full first. Dependency TASK-020 is landed (`27445303`, the `affect_update` snapshot walk).

## Global Constraints

- **`affected_type` must not change size or layout** — it is embedded in the legacy binary player-file struct (`file_formats.h:39`, `affected_type affected[MAX_AFFECT]`) whose 32-bit fixtures are pinned; put NOTHING new in it.
- **No raw `char_data*` is ever stored for a caster.** Identity is `abs_number` (an index into `char_exists`'s `char[8001]` table) plus the pointer the node was created with; every use re-validates `char_exists(abs_number) && ptr->abs_number == abs_number` before dereferencing.
- **Snapshot the formula INPUTS, not rolled results**: `get_mage_caster_level`/`get_mystic_caster_level` roll `number()` on the stat remainder every call; that per-tick roll must survive.
- **Mixed-CRLF sources**: `src/combat/mage.cpp`, `src/combat/mystic.cpp`, `src/combat/limits.cpp`, `src/script/mobact.cpp` are CRLF/mixed — edit them BYTE-WISE (Python `rb`/`wb`, match `\r\n`), verify the `\r\n` count moves only by the lines you added, and never run `make format` (it churns 126 files).
- **Every per-commit gate**: `cmake --build --preset macos-arm64 -j4`, `ctest --preset macos-arm64`, `python3 tools/room_resolve_census.py --check && --self-test`, `python3 tools/location_read_census.py --check`, `python3 tools/string_view_census.py --check`, `scripts/boot-golden.sh --native build/macos-arm64/ageland verify` on any production-touching commit (must say `boot log matches golden`), ASan preset (`cmake --build --preset macos-arm64-asan -j4 && ctest --preset macos-arm64-asan`) on any commit touching a test file. `CharacterizationCombatTest.DamageTranscriptSeed42` must stay green (no golden regeneration).
- **RR ledger**: a new or moved resolver site (`room_of(`, `room_by_id_total(`, `EXIT(`, `OUTSIDE(` …) anywhere fails `--check` until it has a ledger row; production rows need a real proof (the playbook's kinds); test sites get `TEST-FIXTURE` rows; the `<!-- ROOM-RESOLVE-TOKEN-COUNTS -->` table's totals must be re-derived from `--check`'s own error text; `MAXIMUM_TODO_COUNT` (578) only ever changes to the `--check`-derived value.
- **New test files go into BOTH build systems** (`src/CMakeLists.txt` test list and `src/tests/Makefile` `SRCS`), alphabetically.
- **New library TU** (`src/entity/caster_snapshot.cpp`, `src/combat/room_affect_tick.cpp`) goes into the library's CMake source list (`ROTS_ENTITY_SOURCES` / `ROTS_COMBAT_SOURCES` in `src/CMakeLists.txt`) AND the flat `src/Makefile` object list; the `*LayerAcyclicity` linkchecks must stay green (entity may not call combat; combat may call entity).
- Commits: imperative subject ≤72 chars, a body saying why, trailers `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>` and `Claude-Session: https://claude.ai/code/session_01ENKQLZGMeM4QehsfXBA68j`. Stage only your files.
- Class-scoped variables carry a `//` comment describing their role (global convention).

## File Structure

| File | Responsibility |
|---|---|
| `src/core/include/rots/core/caster_snapshot.h` (new, L1) | The POD `caster_snapshot` and its `none()`/`is_none()`/`same_character_as()` helpers. No dependencies beyond `player_specs`. |
| `src/entity/caster_snapshot.cpp` (new, L2, `rots_entity`) | `caster_snapshot::capture(const char_data&)` — the only place that reads a live character into the struct. |
| `src/combat/mage.cpp`, `src/combat/spell_pa.cpp`, `src/combat/mystic.cpp`, `src/entity/char_utils_combat.cpp`, `src/entity/char_utils.cpp` | Snapshot overloads of the formula helpers; the `char_data*` forms forward to them. |
| `src/entity/entity_lifecycle.cpp` + `src/handler.h` | The room-affect caster store (`set_room_affect_caster`/`room_affect_caster`/erase in `affect_remove_room`), the `affect_to_room(room, af, snapshot)` overload. |
| `src/combat/fight.cpp` + `src/fight.h` (or wherever `damage` is declared — `grep -rn 'int damage(' src/*.h`) | `damage_credited()`; `damage()` forwards; `raw_kill` uses the recorded origin. |
| `src/core/include/rots/core/character.h` | `char_special_data::poisoned_by_abs_number` / `poisoned_by` (the poison DoT origin). |
| `src/combat/room_affect_tick.cpp` (new, L3, `rots_combat`) + `src/combat_hooks.h` or a new `src/room_affect_tick.h` | Per-spell tick bodies driven by a snapshot; `room_affect_tick(location, room, occupant)` dispatch. |
| `src/combat/limits.cpp` | `affect_update_room` calls `room_affect_tick` instead of `spell_pointer`; `point_update`'s poison arm credits the poisoner. |
| `src/olc/shaperom.cpp` | builder-placed room affects use `caster_snapshot::none()`. |
| `src/tests/caster_snapshot_tests.cpp`, `src/tests/room_affect_tick_tests.cpp`, `src/tests/fight_credit_tests.cpp` (new) | Tests per task below. |

---

### Task 1: The `caster_snapshot` type and `capture()`

**Files:**
- Create: `src/core/include/rots/core/caster_snapshot.h`
- Create: `src/entity/caster_snapshot.cpp`
- Modify: `src/CMakeLists.txt` (`ROTS_ENTITY_SOURCES` list — find it with `grep -n 'entity/placement.cpp' src/CMakeLists.txt`), `src/Makefile` (the entity object list — `grep -n 'placement' src/Makefile`)
- Test: `src/tests/caster_snapshot_tests.cpp` (new; add to `src/CMakeLists.txt` test list after `tests/big_brother_hooks_tests.cpp` and to `src/tests/Makefile` `SRCS` after `big_brother_hooks_tests.cpp`)

**Interfaces:**
- Produces:
  ```cpp
  struct caster_snapshot {
      int abs_number;            // identity for credit only; NEVER used to read stats
      char_data* identity_ptr;   // the pointer at capture; valid only while char_exists(abs_number) && identity_ptr->abs_number == abs_number
      int level_a;               // GET_LEVELA
      int mage_prof_level;       // profs->prof_level[PROF_MAGE] (or player.level for an NPC, as get_prof_level does)
      int cleric_prof_level;     // profs->prof_level[PROF_CLERIC]
      int intel, wil, perception;
      int spell_power, spell_pen;
      int willpower;             // GET_WILLPOWER (points.willpower) -- saves_poison's offence input
      game_types::player_specs specialization;
      int race;
      bool is_npc, is_charmed, is_pc_for_spell_pen;  // other_side()/should_apply_spell_penetration() inputs
      int master_mage_prof_level; // charmed-NPC spell-pen bonus input (0 when no master)
      char name[MAX_NAME_LENGTH + 1];
      static caster_snapshot capture(const char_data& caster);
      static caster_snapshot none();      // abs_number = -1, every stat 0, name "nobody"
      bool is_none() const;
      bool same_character_as(const char_data& ch) const; // abs_number == ch.abs_number && identity_ptr == &ch
      char_data* resolve() const;          // identity_ptr if char_exists(abs_number) && identity_ptr->abs_number == abs_number, else nullptr
  };
  ```
  (`MAX_NAME_LENGTH` — check the real constant name with `grep -rn 'MAX_NAME' src/core/include/rots/core/*.h`; if none exists, use a file-local `constexpr int kCasterNameCapacity = 64`.)

- [ ] **Step 1: Read the inputs each formula reads** (so the struct is derived, not guessed): `src/combat/mage.cpp:43-110` (`get_mage_caster_level`, `get_magic_power`, `should_apply_spell_penetration`, `get_spell_pen_value`), `src/combat/spell_pa.cpp:226-233` (`get_saving_throw_dc`), `src/combat/mage.cpp:1334-1352` (`get_save_bonus`), `src/combat/mystic.cpp:75-86` (`get_mystic_caster_level`), `src/entity/char_utils_combat.cpp` (`saves_poison`, ~line 555), `src/entity/char_utils.cpp` (`other_side`, ~line 60). Note `GET_PERCEPTION` is a macro over `IS_SHADOW` etc. (`src/utils.h:766`) — capture its *value*.

- [ ] **Step 2: Write the failing test**

```cpp
// src/tests/caster_snapshot_tests.cpp
#include "../handler.h"
#include "../utils.h"
#include "rots/core/caster_snapshot.h"
#include "rots/core/character.h"
#include <gtest/gtest.h>

namespace {
void make_mage(char_data& ch, char_prof_data& profs) {
    ch.profs = &profs;
    profs.prof_level[PROF_MAGE] = 25;
    profs.prof_level[PROF_CLERIC] = 3;
    profs.specialization = static_cast<int>(game_types::PS_Fire);
    ch.player.level = 30;
    ch.player.race = RACE_HUMAN;
    ch.tmpabilities.intel = 21;
    ch.tmpabilities.wil = 17;
    ch.points.spell_power = 4;
    ch.points.spell_pen = 2;
    ch.points.willpower = 9;
    ch.abs_number = 7903; // < 8001, the char_exists table size
}
} // namespace

TEST(CasterSnapshot, CaptureCopiesEveryFormulaInput) {
    char_data ch {};
    char_prof_data profs {};
    make_mage(ch, profs);
    const caster_snapshot snap = caster_snapshot::capture(ch);
    EXPECT_EQ(snap.abs_number, 7903);
    EXPECT_EQ(snap.identity_ptr, &ch);
    EXPECT_EQ(snap.mage_prof_level, 25);
    EXPECT_EQ(snap.cleric_prof_level, 3);
    EXPECT_EQ(snap.intel, 21);
    EXPECT_EQ(snap.wil, 17);
    EXPECT_EQ(snap.spell_power, 4);
    EXPECT_EQ(snap.spell_pen, 2);
    EXPECT_EQ(snap.willpower, 9);
    EXPECT_EQ(snap.specialization, game_types::PS_Fire);
    EXPECT_EQ(snap.race, RACE_HUMAN);
    EXPECT_FALSE(snap.is_npc);
    EXPECT_FALSE(snap.is_none());
}

TEST(CasterSnapshot, LaterStatChangesDoNotReachTheSnapshot) {
    char_data ch {};
    char_prof_data profs {};
    make_mage(ch, profs);
    const caster_snapshot snap = caster_snapshot::capture(ch);
    ch.tmpabilities.intel = 3; // the death penalty shape
    profs.prof_level[PROF_MAGE] = 1;
    EXPECT_EQ(snap.intel, 21);
    EXPECT_EQ(snap.mage_prof_level, 25);
}

TEST(CasterSnapshot, ResolveRequiresTheSameRegisteredCharacter) {
    char_data ch {};
    char_prof_data profs {};
    make_mage(ch, profs);
    set_char_exists(ch.abs_number);
    const caster_snapshot snap = caster_snapshot::capture(ch);
    EXPECT_EQ(snap.resolve(), &ch);
    remove_char_exists(ch.abs_number);
    EXPECT_EQ(snap.resolve(), nullptr) << "an extracted caster resolves to nobody";
    set_char_exists(ch.abs_number);
    ch.abs_number = 7904; // the slot was recycled by a different character
    EXPECT_EQ(snap.resolve(), nullptr);
    remove_char_exists(7903);
}

TEST(CasterSnapshot, NoneIsNeverResolvable) {
    const caster_snapshot none = caster_snapshot::none();
    EXPECT_TRUE(none.is_none());
    EXPECT_EQ(none.resolve(), nullptr);
}
```

- [ ] **Step 3: Wire the test file into both build systems, configure, build, run — expect a compile failure on the missing header**

Run: `cd src && cmake --preset macos-arm64 && cmake --build --preset macos-arm64 -j4 2>&1 | grep error | head`
Expected: `'rots/core/caster_snapshot.h' file not found`

- [ ] **Step 4: Write the header**

```cpp
// src/core/include/rots/core/caster_snapshot.h
#pragma once
// A cast-time snapshot of everything the combat formulas read from a caster
// (TASK-021). Room affects and poison credit keep this instead of a live
// char_data*: a caster who dies, levels, re-specs or is extracted after the
// cast never changes an active spell, and nothing can dangle. POD on purpose
// (copied by value, may live in pooled storage).
#include "rots/core/types.h" // game_types::player_specs

struct char_data;

struct caster_snapshot {
    static constexpr int kNameCapacity = 64;

    int abs_number; // identity for kill credit only; never used to read stats
    char_data* identity_ptr; // the pointer at capture; meaningful only through resolve()
    int level_a; // GET_LEVELA at cast time
    int mage_prof_level; // utils::get_prof_level(PROF_MAGE, caster)
    int cleric_prof_level; // utils::get_prof_level(PROF_CLERIC, caster)
    int intel; // tmpabilities.intel (mage caster level / save DC input)
    int wil; // tmpabilities.wil (mystic caster level input)
    int perception; // GET_PERCEPTION value (saves_poison offence input)
    int willpower; // GET_WILLPOWER value (saves_poison offence input)
    int spell_power; // points.spell_power (battle_mage_handler bonus input)
    int spell_pen; // points.spell_pen (save DC / spell penetration input)
    game_types::player_specs specialization; // utils::get_specialization
    int race; // GET_RACE (other_side / friendly-fire / max-race-prof inputs)
    bool is_npc; // IS_NPC (other_side, spell penetration, get_prof_level inputs)
    bool is_charmed; // IS_AFFECTED(AFF_CHARM) (other_side / spell pen inputs)
    bool is_pc_for_spell_pen; // should_apply_spell_penetration() at capture
    int master_mage_prof_level; // charmed NPC's master PROF_MAGE level (get_spell_pen_value), else 0
    char name[kNameCapacity]; // display name for messages when the caster is gone

    static caster_snapshot capture(const char_data& caster);
    static caster_snapshot none();
    bool is_none() const { return abs_number < 0; }
    bool same_character_as(const char_data& ch) const;
    char_data* resolve() const;
};
```

- [ ] **Step 5: Write `capture()`/`none()`/`resolve()`**

```cpp
// src/entity/caster_snapshot.cpp
#include "rots/core/caster_snapshot.h"
#include "char_utils.h"
#include "handler.h"
#include "utils.h"
#include "rots/core/character.h"
#include <cstring>

caster_snapshot caster_snapshot::capture(const char_data& caster)
{
    caster_snapshot snap {};
    snap.abs_number = caster.abs_number;
    snap.identity_ptr = const_cast<char_data*>(&caster);
    snap.level_a = GET_LEVELA(&caster);
    snap.mage_prof_level = utils::get_prof_level(PROF_MAGE, caster);
    snap.cleric_prof_level = utils::get_prof_level(PROF_CLERIC, caster);
    snap.intel = caster.tmpabilities.intel;
    snap.wil = caster.tmpabilities.wil;
    snap.perception = GET_PERCEPTION(&caster);
    snap.willpower = GET_WILLPOWER(&caster);
    snap.spell_power = caster.points.spell_power;
    snap.spell_pen = caster.points.spell_pen;
    snap.specialization = utils::get_specialization(caster);
    snap.race = GET_RACE(&caster);
    snap.is_npc = utils::is_npc(caster);
    snap.is_charmed = utils::is_affected_by(caster, AFF_CHARM);
    snap.is_pc_for_spell_pen = !snap.is_npc
        || (utils::is_mob_flagged(caster, MOB_ORC_FRIEND) && snap.is_charmed && caster.master && utils::is_pc(*caster.master));
    snap.master_mage_prof_level = (snap.is_npc && snap.is_charmed && caster.master)
        ? utils::get_prof_level(PROF_MAGE, *caster.master) : 0;
    const char* name = GET_NAME(&caster);
    std::snprintf(snap.name, sizeof(snap.name), "%s", name ? name : "someone");
    return snap;
}

caster_snapshot caster_snapshot::none()
{
    caster_snapshot snap {};
    snap.abs_number = -1;
    std::snprintf(snap.name, sizeof(snap.name), "%s", "nobody");
    return snap;
}

bool caster_snapshot::same_character_as(const char_data& ch) const
{
    return !is_none() && identity_ptr == &ch && ch.abs_number == abs_number;
}

char_data* caster_snapshot::resolve() const
{
    if (is_none() || identity_ptr == nullptr || !char_exists(abs_number))
        return nullptr;
    return identity_ptr->abs_number == abs_number ? identity_ptr : nullptr;
}
```
(`GET_PERCEPTION`/`GET_WILLPOWER`/`GET_LEVELA` take a pointer — check `src/utils.h` and adjust `&caster` vs `caster` accordingly; `utils::is_mob_flagged`/`is_affected_by`/`is_pc` are in `src/char_utils.h` — confirm with `grep -n 'is_mob_flagged\|is_affected_by\|is_pc' src/char_utils.h`. The `resolve()` guard order matters: `char_exists` first, the pointer read second.)

- [ ] **Step 6: Add `src/entity/caster_snapshot.cpp` to `ROTS_ENTITY_SOURCES` in `src/CMakeLists.txt` and to `src/Makefile`'s entity object list; build; run**

Run: `cd src && cmake --build --preset macos-arm64 -j4 && ../build/macos-arm64/ageland_tests --gtest_filter='CasterSnapshot.*'`
Expected: 4 tests PASS. Then `ctest --preset macos-arm64` (all pass; `EntityLayerAcyclicity` included) and the ASan preset for the new test file.

- [ ] **Step 7: Censuses and commit**

Run: `python3 tools/room_resolve_census.py --check && python3 tools/location_read_census.py --check && python3 tools/string_view_census.py --check` (the new TU has no resolver sites; if `--check` reports one, classify it per the ledger rules).
```bash
git add src/core/include/rots/core/caster_snapshot.h src/entity/caster_snapshot.cpp src/tests/caster_snapshot_tests.cpp src/CMakeLists.txt src/Makefile src/tests/Makefile
git commit -m "entity: add caster_snapshot, a cast-time copy of the formula inputs (TASK-021)"
```

---

### Task 2: Formula helpers take the snapshot; the live forms forward

**Files:**
- Modify: `src/combat/mage.cpp` (`get_mage_caster_level`, `get_magic_power`, `should_apply_spell_penetration`, `get_spell_pen_value`, `get_save_bonus`, `is_friendly_taget`), `src/combat/spell_pa.cpp` (`get_saving_throw_dc`), `src/combat/mystic.cpp` (`get_mystic_caster_level`), `src/entity/char_utils_combat.cpp` (`saves_poison`), `src/entity/char_utils.cpp` (`other_side`), and the headers that declare them (`src/spells.h`, `src/handler.h`, `src/char_utils.h` — find each with `grep -rn 'get_mage_caster_level\|get_saving_throw_dc\|saves_poison\|other_side' src/*.h`)
- Test: `src/tests/caster_snapshot_tests.cpp` (append)

**Interfaces:**
- Produces (all declared next to their existing `char_data*` forms):
  ```cpp
  int get_mage_caster_level(const caster_snapshot& caster);
  int get_magic_power(const caster_snapshot& caster);
  int get_saving_throw_dc(const caster_snapshot& caster);
  bool should_apply_spell_penetration(const caster_snapshot& caster);
  double get_spell_pen_value(const caster_snapshot& caster);
  int get_save_bonus(const caster_snapshot& caster, const char_data& victim, game_types::player_specs primary_spec, game_types::player_specs opposing_spec);
  bool is_friendly_taget(const caster_snapshot& caster, const char_data* victim);
  int other_side(const caster_snapshot& character, const char_data* other);
  int get_mystic_caster_level(const caster_snapshot& caster);
  char saves_poison(struct char_data* victim, const caster_snapshot& caster);
  ```
- The `char_data*` forms become `return f(caster_snapshot::capture(*caster), ...)`. `battle_mage_handler` is constructed from a `const char_data*` today (`player_spec::battle_mage_handler battle_mage_handler(caster)`); read `src/combat/battle_mage_handler.cpp` — if its constructor only reads the specialization, add a constructor taking `game_types::player_specs`; otherwise add `get_bonus_spell_power(spec, spell_power)`/`get_bonus_spell_pen(spec, spell_pen)` static forms and have both constructors' methods call them.

- [ ] **Step 1: Write the failing equivalence tests** — for each helper, the snapshot form and the live form must agree on the same character under the same queued rolls:

```cpp
TEST(CasterSnapshot, FormulaHelpersAgreeBetweenLiveAndSnapshotForms) {
    char_data ch {};
    char_prof_data profs {};
    make_mage(ch, profs);
    char_data victim {};
    char_prof_data victim_profs {};
    make_mage(victim, victim_profs);
    victim_profs.specialization = static_cast<int>(game_types::PS_Cold);
    const caster_snapshot snap = caster_snapshot::capture(ch);

    for (int i = 0; i < 4; ++i) push_test_random_value(0.5);
    const int live_level = get_mage_caster_level(&ch);
    for (int i = 0; i < 4; ++i) push_test_random_value(0.5);
    EXPECT_EQ(get_mage_caster_level(snap), live_level);

    for (int i = 0; i < 8; ++i) push_test_random_value(0.5);
    const int live_power = get_magic_power(&ch);
    for (int i = 0; i < 8; ++i) push_test_random_value(0.5);
    EXPECT_EQ(get_magic_power(snap), live_power);

    EXPECT_EQ(get_saving_throw_dc(snap), get_saving_throw_dc(&ch));
    EXPECT_EQ(get_save_bonus(snap, victim, game_types::PS_Fire, game_types::PS_Cold),
              get_save_bonus(ch, victim, game_types::PS_Fire, game_types::PS_Cold));
    EXPECT_EQ(should_apply_spell_penetration(snap), should_apply_spell_penetration(&ch));
    EXPECT_DOUBLE_EQ(get_spell_pen_value(snap), get_spell_pen_value(&ch));
    EXPECT_EQ(other_side(snap, &victim), other_side(&ch, &victim));
    EXPECT_EQ(is_friendly_taget(snap, &victim), is_friendly_taget(&ch, &victim));

    for (int i = 0; i < 4; ++i) push_test_random_value(0.5);
    const int live_mystic = get_mystic_caster_level(&ch);
    for (int i = 0; i < 4; ++i) push_test_random_value(0.5);
    EXPECT_EQ(get_mystic_caster_level(snap), live_mystic);

    for (int i = 0; i < 4; ++i) push_test_random_value(0.5);
    const char live_poison = saves_poison(&victim, &ch);
    for (int i = 0; i < 4; ++i) push_test_random_value(0.5);
    EXPECT_EQ(saves_poison(&victim, snap), live_poison);
    clear_test_random_values();
}

TEST(CasterSnapshot, MageCasterLevelStillRollsTheIntelRemainderPerCall) {
    char_data ch {};
    char_prof_data profs {};
    make_mage(ch, profs);
    ch.tmpabilities.intel = 23; // intel_factor 4, remainder roll number(0, 4)
    const caster_snapshot snap = caster_snapshot::capture(ch);
    push_test_random_value(0.0); // roll 0 -> no bump
    const int low = get_mage_caster_level(snap);
    push_test_random_value(0.99); // roll > 0 -> bump
    const int high = get_mage_caster_level(snap);
    clear_test_random_values();
    EXPECT_EQ(high, low + 1) << "the per-call remainder roll must survive the snapshot";
}
```
(The exact number of queued rolls per helper is not load-bearing — a longer queue just drains; use the same count on both sides of each pair.)

- [ ] **Step 2: Run — expect link/compile failures for the missing overloads**

- [ ] **Step 3: Implement the overloads.** Byte-wise edits. Pattern for each helper — the body moves to the snapshot form and reads `caster.<field>` where it read the character; the live form becomes a forwarder. Example for `get_mage_caster_level` (mage.cpp:43):

```cpp
int get_mage_caster_level(const caster_snapshot& caster)
{
    int mage_level = caster.mage_prof_level;

    // Factor in intel values not divisible by 5.
    int intel_factor = caster.intel / 5;
    if (number(0, intel_factor % 5) > 0) {
        ++intel_factor;
    }

    return mage_level + intel_factor;
}

int get_mage_caster_level(const char_data* caster)
{
    return get_mage_caster_level(caster_snapshot::capture(*caster));
}
```
`get_magic_power`: `GET_MAX_RACE_PROF_LEVEL(PROF_MAGE, caster)` is a macro over `GET_RACE(ch)` — add a `max_race_prof_level(int prof, int race)` inline in `src/utils.h` beside the macro (same table) and use `caster.race`; `GET_LEVELA` → `caster.level_a`. `get_saving_throw_dc`: `caster.mage_prof_level / 3`, `(caster.intel - 8) / 4`, the spell-pen bonus from `caster.spell_pen` through the handler. `should_apply_spell_penetration(snap)` → `caster.is_pc_for_spell_pen`; `get_spell_pen_value(snap)` → `caster.mage_prof_level + (caster.is_npc && caster.is_charmed ? caster.master_mage_prof_level / 3 : 0)` then the rest of the existing body. `get_save_bonus(snap, victim, ...)`: the caster half uses `caster.specialization`, the victim half is unchanged. `other_side(snap, other)`: replace `IS_NPC(character)`→`character.is_npc`, `IS_AFFECTED(character, AFF_CHARM)`→`character.is_charmed`, `GET_RACE(character)`→`character.race` (the `RACE_EAST`/`RACE_MAGI`/`RACE_GOOD`/`RACE_EVIL` macros take a character — add `race_is_east(int)`-style inline helpers or apply the macros to a local `int race`; read `src/utils.h` for their definitions and replicate exactly). `is_friendly_taget(snap, victim)`: `victim == caster` becomes `caster.same_character_as(*victim)`; the `victim->master` recursion stays. `get_mystic_caster_level(snap)`: `caster.cleric_prof_level`, `caster.wil`. `saves_poison(victim, snap)`: `offence = ((caster.willpower * 8) * caster.perception) / 100`.

- [ ] **Step 4: Build, run the two tests, run the full suite** — every existing test must stay green (the forwarders are byte-equivalent); `ctest --preset macos-arm64` and the ASan preset.

- [ ] **Step 5: Censuses (no resolver sites move in this task), then commit**

```bash
git add src/combat/mage.cpp src/combat/spell_pa.cpp src/combat/mystic.cpp src/entity/char_utils_combat.cpp src/entity/char_utils.cpp src/spells.h src/handler.h src/char_utils.h src/utils.h src/combat/battle_mage_handler.cpp src/tests/caster_snapshot_tests.cpp
git commit -m "combat: formula helpers read a caster_snapshot; live forms forward (TASK-021)"
```

---

### Task 3: The room-affect caster store

**Files:**
- Modify: `src/entity/entity_lifecycle.cpp` (`affect_to_room` at ~2948, `affect_remove_room` at ~2982), `src/handler.h` (declarations next to `affect_to_room`)
- Modify: `src/olc/shaperom.cpp:285` (builder affects → `caster_snapshot::none()`)
- Test: `src/tests/caster_snapshot_tests.cpp` (append)

**Interfaces:**
- Produces:
  ```cpp
  void affect_to_room(struct room_data* room, struct affected_type* af, const caster_snapshot& caster); // records the caster for (room, af->location)
  const caster_snapshot* room_affect_caster(const room_data* room, int spell); // nullptr when none recorded
  void set_room_affect_caster(room_data* room, int spell, const caster_snapshot& caster); // used by the mist move and by renewals
  ```
  The existing two-argument `affect_to_room(room, af)` stays and records `caster_snapshot::none()` (so every `ROOMAFF_SPELL` has an entry). `affect_remove_room` erases the entry for `(room, af->location)` when the removed affect is a `ROOMAFF_SPELL`.
- Storage: a file-local `std::map<std::pair<int, int>, caster_snapshot>` keyed by `(room->number, spell)` — `room->number` is the room's vnum-side id field already used as `tmplist->number` in `affect_to_room` (read it; if rooms can share `number` use the room pointer's index `room - &world[0]`... check `room_data::operator[]`/`room_by_id_total` for the canonical id and use whatever `affect_to_room` already stores in `tmplist->number`).

- [ ] **Step 1: Write the failing tests**

```cpp
TEST(RoomAffectCaster, AffectToRoomRecordsTheSnapshotAndRemoveErasesIt) {
    ScopedTestWorld world(4);
    room_data* room = room_by_id_total(2);
    char_data ch {};
    char_prof_data profs {};
    make_mage(ch, profs);
    affected_type blaze {};
    blaze.type = ROOMAFF_SPELL; blaze.duration = 3; blaze.modifier = 20; blaze.location = SPELL_BLAZE;
    affect_to_room(room, &blaze, caster_snapshot::capture(ch));
    const caster_snapshot* recorded = room_affect_caster(room, SPELL_BLAZE);
    ASSERT_NE(recorded, nullptr);
    EXPECT_EQ(recorded->mage_prof_level, 25);
    EXPECT_EQ(room_affect_caster(room, SPELL_HAZE), nullptr);
    affect_remove_room(room, room_affected_by_spell(room, SPELL_BLAZE));
    EXPECT_EQ(room_affect_caster(room, SPELL_BLAZE), nullptr);
}

TEST(RoomAffectCaster, TheTwoArgumentFormRecordsNobody) {
    ScopedTestWorld world(4);
    room_data* room = room_by_id_total(2);
    affected_type haze {};
    haze.type = ROOMAFF_SPELL; haze.duration = 3; haze.modifier = 5; haze.location = SPELL_HAZE;
    affect_to_room(room, &haze);
    const caster_snapshot* recorded = room_affect_caster(room, SPELL_HAZE);
    ASSERT_NE(recorded, nullptr);
    EXPECT_TRUE(recorded->is_none());
    affect_remove_room(room, room_affected_by_spell(room, SPELL_HAZE));
}
```
(`ScopedTestWorld` is in `src/tests/test_world.h`; `ROOMAFF_SPELL` in `rots/core/character.h:125`; `SPELL_*` in `src/spells.h`. Teardown through `affect_remove_room` keeps `affected_list` clean — the `affect_update_tests.cpp` fixture shows the idiom.)

- [ ] **Step 2: Run — expect link failures**

- [ ] **Step 3: Implement** in `entity_lifecycle.cpp` next to `affect_to_room`:

```cpp
namespace {
// The caster recorded for each live room affect, keyed by (room number,
// spell). Lives beside the affect rather than inside affected_type, which
// is embedded in the legacy binary player-file layout and must not grow.
std::map<std::pair<int, int>, caster_snapshot> g_room_affect_casters;
} // namespace

void set_room_affect_caster(room_data* room, int spell, const caster_snapshot& caster)
{
    g_room_affect_casters[{ room->number, spell }] = caster;
}

const caster_snapshot* room_affect_caster(const room_data* room, int spell)
{
    auto it = g_room_affect_casters.find({ room->number, spell });
    return it == g_room_affect_casters.end() ? nullptr : &it->second;
}

void affect_to_room(struct room_data* room, struct affected_type* af, const caster_snapshot& caster)
{
    affect_to_room(room, af);
    if (af->type == ROOMAFF_SPELL)
        set_room_affect_caster(room, af->location, caster);
}
```
The two-argument `affect_to_room` gains, at its end: `if (af->type == ROOMAFF_SPELL && room_affect_caster(room, af->location) == nullptr) set_room_affect_caster(room, af->location, caster_snapshot::none());`. `affect_remove_room` gains, after `put_to_affected_type_pool(af)` is NOT safe (af is pooled) — capture `const int spell = af->location; const bool is_room_spell = af->type == ROOMAFF_SPELL;` at the top and erase `g_room_affect_casters.erase({ room->number, spell })` when `is_room_spell`, after the unlink. `shaperom.cpp:285`'s loop stays on the two-argument form (records `none()` by construction) — add a one-line comment saying so.

- [ ] **Step 4: Build, run, full ctest + ASan; censuses; commit**

```bash
git add src/entity/entity_lifecycle.cpp src/handler.h src/olc/shaperom.cpp src/tests/caster_snapshot_tests.cpp
git commit -m "entity: room affects record their caster's snapshot (TASK-021)"
```

---

### Task 4: `damage_credited()` and the recorded poison origin

**Files:**
- Modify: `src/combat/fight.cpp` (`damage` at ~1770, `raw_kill` PC arm at ~1009, `die` at ~1085), its declaring header (`grep -rn 'int damage(char_data' src/*.h`)
- Modify: `src/core/include/rots/core/character.h` (`struct char_special_data` — add two fields), `src/entity/entity_lifecycle.cpp` (`clear_char` zeroes them; `affect_remove` clears them when the removed affect is `SPELL_POISON` and no other poison affect remains)
- Modify: `src/combat/limits.cpp:759` (`point_update`'s poison tick)
- Test: `src/tests/fight_credit_tests.cpp` (new; add to both build systems after `fight_proc_tests.cpp`)

**Interfaces:**
- Produces:
  ```cpp
  // fight.cpp. `attacker` engages (set_fighting/on_attacked_character) exactly as damage() always did;
  // `credited_killer` (may be nullptr, may equal attacker) is what reaches die()/raw_kill().
  int damage_credited(char_data* attacker, char_data* victim, char_data* credited_killer, int dam, int attacktype, int hit_location);
  int damage(char_data* attacker, char_data* victim, int dam, int attacktype, int hit_location); // == damage_credited(attacker, victim, attacker, ...)
  ```
  ```cpp
  // character.h, char_special_data:
  int poisoned_by_abs_number; // abs_number of the character whose poison this is; -1 when none
  char_data* poisoned_by; // identity pointer for poisoned_by_abs_number; valid only while char_exists() and ->abs_number match
  ```
  ```cpp
  // fight.cpp helper, used by point_update's poison tick:
  char_data* resolve_poisoner(const char_data& victim); // nullptr when no recorded poisoner exists any more
  ```

- [ ] **Step 1: Read `damage()` end to end** (`fight.cpp:1770-2115`) and note every use of `attacker` after the `set_fighting` block: the `die(victim, attacker, attacktype)` call at the POSITION_DEAD arm and the pet-master redirect just above it are the credit path; everything else is engagement.

- [ ] **Step 2: Write the failing tests.** Use `src/tests/mage_tests.cpp`'s fixture shapes (`ScopedFireballExtractCharHook`-style extract stub, `ScopedFireballMobIndex`, `ScopedZoneTableOwner`, a capturing descriptor) — copy them file-locally into `fight_credit_tests.cpp` (the repo's per-file-copy idiom); they need a `ScopedTestWorld`, a placed NPC victim with `abilities.hit = tmpabilities.hit = 1`, `nr = 0`, and a registered `abs_number` (`set_char_exists`, < 8001).

```cpp
TEST(DamageCredited, TheCreditedKillerReachesRawKillNotTheEngagingAttacker) {
    // victim dies to a self-engaged hit (attacker == victim), credited to a player mage standing elsewhere
    ...fixture: mage (PC, desc with capturing output, abs 7910, placed in room 3), victim (NPC, 1 hp, placed in room 2, abs 7911)
    ScopedRecordingExtractCharHook extraction;      // records the extraction
    testing::internal::CaptureStderr();
    const int died = damage_credited(&victim, &victim, &mage, 50, SPELL_BLAZE, 0);
    const std::string log = testing::internal::GetCapturedStderr();
    EXPECT_EQ(died, 1);
    EXPECT_EQ(extraction.calls, 1);
    EXPECT_EQ(mage.specials.fighting, nullptr) << "a remote credited killer is never engaged";
    EXPECT_NE(log.find("killed by"), std::string::npos) << "die()'s vmudlog names the credited killer: " << log; // for a PC victim; for the NPC victim assert group_gain ran for the mage instead -- read die()/group_gain() and pick the observable
}

TEST(DamageCredited, DamageForwardsWithTheAttackerAsCredit) { ... damage(&attacker, &victim, 50, ...) == damage_credited(&attacker, &victim, &attacker, 50, ...) observable through the same extraction/engagement assertions ... }

TEST(PoisonOrigin, RawKillTreatsAPoisonDeathAsAPlayerKillOnlyWhenTheRecordedPoisonerIsAPlayer) {
    // PC victim (make_mortal_player shape from load_room_placement_tests.cpp) with poisoned_by = an NPC snake -> stats take the 2/3 penalty arm;
    // with poisoned_by = a PC -> hp/4 arm. Drive raw_kill(&victim, resolve_poisoner(victim), SPELL_POISON) directly.
}

TEST(PoisonOrigin, PointUpdatePoisonTickCreditsTheResolvedPoisonerAndForgetsAnExtractedOne) {
    // victim AFF_POISON with no SPELL_POISON affect (the point_update arm's condition), 1 hp, poisoned_by = mage;
    // (a) mage registered -> extraction recorded, die() saw the mage (observable as above)
    // (b) remove_char_exists(mage.abs_number) -> tick still kills, credited killer nullptr, no dereference (ASan)
}
```
Write the assertions against real observables — read `die()` (`fight.cpp:1085`) to pick them: for an NPC victim and a PC killer, `group_gain(killer, dead_man)` runs (observable: the killer's `points.exp` rises — set `mage.points.exp = 0` first); for a PC victim, `die()`'s `vmudlog("%s killed by %s ...")` line names the killer.

- [ ] **Step 3: Run — expect compile failures on `damage_credited`/`poisoned_by`**

- [ ] **Step 4: Implement.** Rename the body of `damage()` to `damage_credited(attacker, victim, credited_killer, ...)` (byte-wise; the file is CRLF-mixed), change the POSITION_DEAD arm to:

```cpp
    if (GET_POS(victim) == POSITION_DEAD) {
        char_data* killer = credited_killer;
        // Redirect a pet's credit to its master when the master stands with the pet.
        if (killer && IS_NPC(killer)) {
            if (killer->master && (MOB_FLAGGED(killer, MOB_PET) || MOB_FLAGGED(killer, MOB_ORC_FRIEND)) && location_of(killer->master) == location_of(killer)) {
                killer = killer->master;
            }
        }
        die(victim, killer, attacktype);
        return 1;
    }
```
and add `int damage(char_data* attacker, char_data* victim, int dam, int attacktype, int hit_location) { return damage_credited(attacker, victim, attacker, dam, attacktype, hit_location); }`. Every other `attacker` use in the body stays `attacker`. Add the `char_special_data` fields (zeroed/`-1` in `clear_char`, `entity_lifecycle.cpp` — find the existing field resets). In `raw_kill`, replace `bool died_to_player = attack_type == SPELL_POISON || (killer != NULL && !IS_NPC(killer));` with `bool died_to_player = killer != NULL && !IS_NPC(killer);` and delete the `TODO(drelidan)` comment, replacing it with one sentence saying the poison origin is now recorded (TASK-021). Add `resolve_poisoner`:

```cpp
char_data* resolve_poisoner(const char_data& victim)
{
    const int number = victim.specials.poisoned_by_abs_number;
    char_data* ptr = victim.specials.poisoned_by;
    if (number < 0 || ptr == nullptr || !char_exists(number))
        return nullptr;
    return ptr->abs_number == number ? ptr : nullptr;
}
```
`point_update` (limits.cpp:759): `damage(i, i, 5, SPELL_POISON, 0)` → `damage_credited(i, i, resolve_poisoner(*i), 5, SPELL_POISON, 0)`. `affect_remove`: when `af->type == SPELL_POISON` and `affected_by_spell(ch, SPELL_POISON) == nullptr` after the unlink, reset `poisoned_by_abs_number = -1; poisoned_by = nullptr`.

- [ ] **Step 5: Build, run the new tests, full ctest (the seed42 characterization golden MUST stay byte-identical — `damage()`'s behavior is unchanged by construction; if it moves, the refactor is wrong, not the golden), ASan on the new test file, native boot golden, censuses (the new test's `room_by_id_total(` sites get `TEST-FIXTURE` rows; `fight.cpp`'s own row counts must not change — if `--check` reports a count change for `fight.cpp · damage`/`raw_kill`, the site moved between functions: add/adjust rows per the ledger's mixed-key rules), commit**

```bash
git add src/combat/fight.cpp src/fight.h src/core/include/rots/core/character.h src/entity/entity_lifecycle.cpp src/combat/limits.cpp src/tests/fight_credit_tests.cpp src/CMakeLists.txt src/tests/Makefile docs/superpowers/room-resolve-ledger.md
git commit -m "fight: damage_credited() separates engagement from kill credit; poison remembers its origin (TASK-021)"
```

---

### Task 5: Per-spell room ticks from the snapshot

**Files:**
- Create: `src/combat/room_affect_tick.cpp`, `src/room_affect_tick.h`
- Modify: `src/combat/limits.cpp` (`affect_update_room`, ~1493-1501), `src/CMakeLists.txt` (`ROTS_COMBAT_SOURCES`), `src/Makefile`
- Test: `src/tests/room_affect_tick_tests.cpp` (new; both build systems, after `rng_seed_tests.cpp`)

**Interfaces:**
- Produces:
  ```cpp
  // room_affect_tick.h
  // Runs one room-affect tick of `spell` on `occupant` from the snapshot recorded for (room, spell).
  // Returns true when the spell has a tick body (blaze, poison, haze, mist); false for anything else.
  bool room_affect_tick(int spell, room_data* room, char_data* occupant, const affected_type& affect);
  ```
- Consumes: Task 2's snapshot overloads, Task 3's `room_affect_caster`, Task 4's `damage_credited`.

- [ ] **Step 1: Read the four current re-cast arms once more and write down, per spell, exactly what the tick does TODAY with `caster == victim == occupant`:**
  - blaze (`mage.cpp` victim arm): `save_bonus = get_save_bonus(*caster, *victim, PS_Fire, PS_Cold)`; `saved = new_saves_spell(caster, victim, save_bonus)`; `dam = number(8, level) + 10` (level = `get_mage_caster_level(caster)`, computed at function entry); `if (saved) dam >>= 1`; no message (caster == victim); `apply_spell_damage(caster, victim, dam, SPELL_BLAZE, 0)`.
  - poison (`mystic.cpp` victim arm): `if (!saves_poison(victim, caster) && number(0, 0) < 50)` → affect_join `{SPELL_POISON, duration level+1, modifier -2, APPLY_STR, AFF_POISON}` (level = `get_mystic_caster_level(caster)`), "You feel very sick.", `damage(caster, victim, 5, SPELL_POISON, 0)`; else two `act()` messages.
  - haze (`mystic.cpp` victim arm, `type == SPELL_TYPE_SPELL`, `is_object == 0`): level = `get_mystic_caster_level(caster)` (+6 if `get_specialization(caster) == PS_Illusion`), `my_duration = number(0, 1)`, `if (!affected_by_spell(victim, SPELL_HAZE) && !saves_mystic(victim))` → `affect_to_char {SPELL_HAZE, my_duration, modifier level, APPLY_NONE, AFF_HAZE}` + two `act()` messages.
  - mist (`mage.cpp`): renew this room's mist to `max(duration, level / 5)` where level = `get_mage_caster_level(caster)`, then for each exit with a room: renew or create the adjacent mist with `duration level / 6`, modifier from SHADOWY. (The tick does NOT emit the "breathes out dark mists" messages when renewing — the current code's renewal arm is silent.)

- [ ] **Step 2: Write the failing tests.** Fixture per test: `ScopedTestWorld world(16)`, the extract-stub / mob-index / zone-table helpers copied file-locally, a blaze-speed occupant (NPC, 1 hp, registered abs_number), the caster recorded via `affect_to_room(room, &af, caster_snapshot::capture(mage))`.

```cpp
TEST(RoomAffectTick, BlazeTickDamageComesFromTheSnapshotNotTheOccupant) {
    // mage: PROF_MAGE 25, intel 21 -> high level; occupant: level 1, intel 3.
    // Record the snapshot, then WRECK the mage's stats (intel 3, prof 1) and extract the mage (remove_char_exists).
    // Queue 0.0 rolls; call room_affect_tick(SPELL_BLAZE, room, &occupant, *room_affected_by_spell(room, SPELL_BLAZE)).
    // occupant.tmpabilities.hit starts at 500 and must drop by number(8, LEVEL_FROM_SNAPSHOT)+10 (>>1 if saved) -- compute the expected
    // number from the snapshot fields with the same queued rolls (get_mage_caster_level(snap) with 0.0 -> mage_prof_level + intel/5).
}

TEST(RoomAffectTick, BlazeTickKillCreditsTheRecordedCasterWhenAlive) {
    // occupant 1 hp; mage registered and placed in ANOTHER room; mage.points.exp = 0.
    // tick -> extraction recorded once, mage.points.exp > 0 (group_gain credited the mage), mage.specials.fighting == nullptr.
}

TEST(RoomAffectTick, BlazeTickKillCreditsNobodyWhenTheCasterIsGone) {
    // as above but remove_char_exists(mage.abs_number) before the tick -> extraction recorded once, mage.points.exp == 0, no crash (ASan run).
}

TEST(RoomAffectTick, PoisonTickRecordsThePoisonerOnTheVictim) {
    // record a mystic's snapshot for SPELL_POISON; occupant 500 hp; queue 0.0 (saves_poison: offence roll vs defense roll -- pick rolls so the victim FAILS the save: read saves_poison and choose queued values accordingly, e.g. 0.99 for the offence draw and 0.0 for the defense draw);
    // tick -> affected_by_spell(&occupant, SPELL_POISON) != nullptr, occupant.specials.poisoned_by_abs_number == mystic.abs_number, hit == 495.
}

TEST(RoomAffectTick, HazeTickUsesTheSnapshotLevelForTheModifier) {
    // record a mystic snapshot (cleric prof 20, wil 15, spec Illusion); wreck the mystic; queue rolls so saves_mystic fails (perception-based: set occupant perception low);
    // tick -> affected_by_spell(&occupant, SPELL_HAZE)->modifier == get_mystic_caster_level(snap)+6 under the same rolls.
}

TEST(RoomAffectTick, MistTickRenewsFromTheSnapshotLevel) {
    // record a mage snapshot; set the room's mist duration to 1; tick -> duration == max(1, level/5) computed from the snapshot; an adjacent room gains a mist of duration level/6.
}

TEST(RoomAffectTick, UnknownSpellHasNoTickBody) {
    // room_affect_tick(SPELL_SANCTUARY, room, &occupant, af) == false and nothing changes.
}
```

- [ ] **Step 3: Run — expect link failures on `room_affect_tick`**

- [ ] **Step 4: Implement `src/combat/room_affect_tick.cpp`**

```cpp
#include "room_affect_tick.h"
#include "handler.h"
#include "spells.h"
#include "utils.h"
#include "char_utils.h"
#include "rots/core/caster_snapshot.h"
#include "rots/core/character.h"
#include "rots/core/room.h"

// The fight.cpp / mage.cpp / mystic.cpp helpers this file reuses (declared in spells.h / handler.h after Task 2/4).
int apply_spell_damage(char_data* caster, char_data* victim, int damage_dealt, int spell_number, int hit_location);

namespace {

// The attacker that ENGAGES the occupant for this tick's damage: the recorded
// caster only when it still exists and stands in the same room (so
// set_fighting never pairs characters across rooms); otherwise the occupant
// itself, exactly as the old self-re-cast did.
char_data* engaging_attacker(char_data* caster, char_data* occupant)
{
    if (caster != nullptr && location_of(caster) == location_of(occupant))
        return caster;
    return occupant;
}

int tick_spell_damage(const caster_snapshot& who, char_data* caster, char_data* occupant, int dam, int spell)
{
    // apply_spell_damage()'s saving-throw scaling reads the caster's spell
    // penetration: reproduce it from the snapshot, then hand the scaled damage
    // to damage_credited() with the credit pointing at the recorded caster.
    double saving_throw = occupant->specials2.saving_throw;
    if (should_apply_spell_penetration(who)) {
        saving_throw -= get_spell_pen_value(who);
        if (utils::is_pc(*occupant))
            saving_throw += utils::get_level_a(*occupant) / 5.0;
    }
    double multiplier = 1.0;
    if (saving_throw > 0)
        multiplier = 20.0 / (20.0 + saving_throw);
    else if (saving_throw < 0)
        multiplier = 2.0 - (20.0 / (20.0 - saving_throw));
    return damage_credited(engaging_attacker(caster, occupant), occupant, caster, int(dam * multiplier), spell, 0);
}

void blaze_tick(const caster_snapshot& who, char_data* caster, char_data* occupant)
{
    const int level = get_mage_caster_level(who);
    const int save_bonus = get_save_bonus(who, *occupant, game_types::PS_Fire, game_types::PS_Cold);
    const bool saved = new_saves_spell(who, occupant, save_bonus); // Task 2 overload -- add it: new_saves_spell(const caster_snapshot&, const char_data*, int) using get_saving_throw_dc(snapshot)
    int dam = number(8, level) + 10;
    if (saved)
        dam >>= 1;
    tick_spell_damage(who, caster, occupant, dam, SPELL_BLAZE);
}

void poison_tick(const caster_snapshot& who, char_data* caster, char_data* occupant)
{
    if (!saves_poison(occupant, who) && (number(0, 0) < 50)) {
        affected_type af {};
        af.type = SPELL_POISON;
        af.duration = get_mystic_caster_level(who) + 1;
        af.modifier = -2;
        af.location = APPLY_STR;
        af.bitvector = AFF_POISON;
        affect_join(occupant, &af, FALSE, FALSE);
        occupant->specials.poisoned_by_abs_number = who.abs_number;
        occupant->specials.poisoned_by = who.identity_ptr;
        send_to_char("You feel very sick.\n\r", occupant);
        damage_credited(engaging_attacker(caster, occupant), occupant, caster, 5, SPELL_POISON, 0);
    } else {
        act("You feel your body fend off the poison.", TRUE, occupant, 0, occupant, TO_VICT);
    }
}

void haze_tick(const caster_snapshot& who, char_data* occupant)
{
    int level = get_mystic_caster_level(who);
    if (who.specialization == game_types::PS_Illusion)
        level += 6;
    const int my_duration = number(0, 1);
    if (!affected_by_spell(occupant, SPELL_HAZE) && !saves_mystic(occupant)) {
        affected_type af {};
        af.type = SPELL_HAZE;
        af.duration = my_duration;
        af.modifier = level;
        af.location = APPLY_NONE;
        af.bitvector = AFF_HAZE;
        affect_to_char(occupant, &af);
        act("You feel dizzy as your surroundings seem to blur and twist.\n\r", TRUE, occupant, 0, occupant, TO_CHAR);
        act("$n staggers, overcome by dizziness!", FALSE, occupant, 0, 0, TO_ROOM);
    }
}

void mist_tick(const caster_snapshot& who, room_data* room)
{
    const int level = get_mage_caster_level(who);
    if (affected_type* here = room_affected_by_spell(room, SPELL_MIST_OF_BAAZUNGA))
        if (here->duration < level / 5)
            here->duration = level / 5;
    for (int direction = 0; direction < NUM_OF_DIRS; direction++) {
        if (!room->dir_option[direction] || room->dir_option[direction]->to_room == NOWHERE)
            continue;
        room_data* next = room_by_id_total(room->dir_option[direction]->to_room);
        if (affected_type* there = room_affected_by_spell(next, SPELL_MIST_OF_BAAZUNGA)) {
            if (there->duration < level / 5)
                there->duration = level / 5;
            continue;
        }
        affected_type af2 {};
        af2.type = ROOMAFF_SPELL;
        af2.duration = level / 6;
        af2.modifier = IS_SET(next->room_flags, SHADOWY) ? 1 : 0;
        af2.location = SPELL_MIST_OF_BAAZUNGA;
        af2.bitvector = 0;
        affect_to_room(next, &af2, who);
    }
}

} // namespace

bool room_affect_tick(int spell, room_data* room, char_data* occupant, const affected_type& /*affect*/)
{
    const caster_snapshot* recorded = room_affect_caster(room, spell);
    // A builder-placed or pre-snapshot affect carries no caster: tick from the
    // occupant's own stats, exactly as the pre-TASK-021 self-re-cast did.
    const caster_snapshot who = (recorded && !recorded->is_none()) ? *recorded : caster_snapshot::capture(*occupant);
    char_data* caster = (recorded && !recorded->is_none()) ? recorded->resolve() : nullptr;
    switch (spell) {
    case SPELL_BLAZE:
        blaze_tick(who, caster, occupant);
        return true;
    case SPELL_POISON:
        poison_tick(who, caster, occupant);
        return true;
    case SPELL_HAZE:
        haze_tick(who, occupant);
        return true;
    case SPELL_MIST_OF_BAAZUNGA:
        mist_tick(who, room);
        return true;
    default:
        return false;
    }
}
```
Notes the implementer must honor: (1) `mist_tick` reads the mist's renewal rule from `spell_mist_of_baazunga` — the adjacent-room branch in the original compares `oldaf->duration < af.duration` (the MAIN room's `level/5`, a long-standing quirk) — keep that quirk: compare against `level / 5`, as written above. (2) `new_saves_spell(const caster_snapshot&, …)` goes into Task 2's set if not already there. (3) `room_of(` must not appear in this file — every room comes in as a pointer — so the RR ledger gains no resolver rows except `room_by_id_total(` in `mist_tick` (one `PROVEN`/`entry-guard` row: the `to_room != NOWHERE` test dominates it; in-range by the exit-target invariant the ledger's `affect_update_room` row already cites).

- [ ] **Step 5: Switch `affect_update_room` (limits.cpp, byte-wise) to the tick**

Replace the `(skills[tmpaf->location].spell_pointer)(tmpch, mutable_arg(""), SPELL_TYPE_SPELL, tmpch, 0, 0, 0);` call with:
```cpp
                            if (!room_affect_tick(tmpaf->location, room, tmpch, *tmpaf))
                                (skills[tmpaf->location].spell_pointer)(tmpch, mutable_arg(""), SPELL_TYPE_SPELL, tmpch, 0, 0, 0);
```
(the fallback keeps any future `ROOMAFF_SPELL` without a tick body behaving as before). Note: `tmpaf` may have been REMOVED by `mist_tick`'s renewals? No — `mist_tick` never removes; but `affect_remove_room` can run inside the occupant loop through a death (`raw_kill` strips the DEAD CHARACTER's affects, not the room's) — keep the existing `next_tmpaf` save-next, and add to `affect_update_room` a re-check `if (!room->affected) break;` after the occupant loop (a removed-last-affect guard; it cannot fire today, so the comment says it is defensive).

- [ ] **Step 6: Build, run the new suite, full ctest, ASan (new test file), native boot golden (must match — no live world data carries a room affect at boot; if it does NOT match, read the diff: it means a zone file places a room affect and the tick's behavior changed against it — stop and report), censuses + ledger rows for the new TU (`room_by_id_total(` in `mist_tick`, and `TEST-FIXTURE` rows for the test file), commit**

```bash
git add src/combat/room_affect_tick.cpp src/room_affect_tick.h src/combat/limits.cpp src/CMakeLists.txt src/Makefile src/tests/room_affect_tick_tests.cpp src/tests/Makefile docs/superpowers/room-resolve-ledger.md
git commit -m "combat: room affects tick from their caster's snapshot and credit kills (TASK-021)"
```

---

### Task 6: The casts record their snapshot; poison's victim arm records the poisoner

**Files:**
- Modify: `src/combat/mage.cpp` (`spell_blaze` ~2231-2290: the `affect_to_room(room_of(caster), &af)` at ~2284 and the `oldaf` renewal branch; `spell_mist_of_baazunga` ~2365-2445: both `affect_to_room` calls and both renewal branches), `src/combat/mystic.cpp` (`spell_haze` ~1140-1170; `spell_poison` ~1278-1330: the room arm's `affect_to_room` + renewal, and the victim arm's `affect_join` + `damage`)
- Test: `src/tests/room_affect_tick_tests.cpp` (append), `src/tests/mystic_tests.cpp` (append one poison-origin test if the poison victim arm is already exercised there — `grep -n 'spell_poison' src/tests/mystic_tests.cpp`)

- [ ] **Step 1: Write the failing tests**

```cpp
TEST(RoomAffectCast, BlazeRecordsTheCasterSnapshotAndRenewalReplacesIt) {
    // mage A casts blaze on room (room arm: victim == nullptr, obj == nullptr) -> room_affect_caster(room, SPELL_BLAZE)->abs_number == A.abs_number.
    // mage B (higher level) casts again -> the affect's modifier rose (existing behavior) AND the recorded snapshot is now B's.
    // mage C (lower level) casts again -> modifier unchanged AND the recorded snapshot stays B's (a weaker renewal does not steal the spell).
}
TEST(RoomAffectCast, PoisonVictimArmRecordsThePoisoner) {
    // spell_poison(&mystic, "", SPELL_TYPE_SPELL, &victim, ...) with rolls that fail the save -> victim.specials.poisoned_by_abs_number == mystic.abs_number.
}
TEST(RoomAffectCast, HazeAndMistRoomArmsRecordTheCaster) { ... room_affect_caster(room, SPELL_HAZE) / SPELL_MIST_OF_BAAZUNGA non-null and not none() ... }
```

- [ ] **Step 2: Run — expect failures (`room_affect_caster` returns the `none()` entry)**

- [ ] **Step 3: Implement** (byte-wise, CRLF): in each room arm, `const caster_snapshot who = caster_snapshot::capture(*caster);` right after the caster null-check, then `affect_to_room(room_of(caster), &af, who)` in the new-affect branch, and in the renewal branch, after raising `oldaf->duration`/`modifier`, `if (<the renewal raised modifier or duration>) set_room_affect_caster(room_of(caster), <SPELL>, who);` (blaze/poison/haze: when `oldaf->modifier < af.modifier`; mist: when `oldaf->duration < af.duration`). Poison's victim arm: after `affect_join(victim, &af, FALSE, FALSE);` add `victim->specials.poisoned_by_abs_number = caster ? caster->abs_number : -1; victim->specials.poisoned_by = caster;`. Leave the spell bodies' OWN formulas on the `char_data*` forms (they forward to the snapshot path since Task 2), so the live cast is unchanged in behavior.

- [ ] **Step 4: Build, run, full ctest, ASan, boot golden, censuses (`room_of(` counts in `spell_blaze`/`spell_mist_of_baazunga`/`spell_haze`/`spell_poison` may change — `--check` will say; update those rows' counts and proof text per the ledger's mixed-key rules, keeping each row's proof valid for its new sites), commit**

```bash
git add src/combat/mage.cpp src/combat/mystic.cpp src/tests/room_affect_tick_tests.cpp src/tests/mystic_tests.cpp docs/superpowers/room-resolve-ledger.md
git commit -m "spells: blaze/mist/haze/poison record their caster; poison remembers its poisoner (TASK-021)"
```

---

### Task 7: Docs, ledger reconciliation, backlog close-out

**Files:**
- Modify: `docs/superpowers/room-resolve-ledger.md` (final reconciliation — every count re-derived from `--check`), `tools/room_resolve_census.py` (`MAXIMUM_TODO_COUNT` only if `--check` reports the site-sum moved; the self-test pin moves with it), `docs/BUILD.md` (a "Room-affect caster snapshot (TASK-021)" subsection under the combat/RR area: the design, the `affected_type`-must-not-grow constraint, the stats/actor split, `damage_credited`, the poison origin, the mist quirk preserved, test counts), `AGENTS.md` (one paragraph in the Testing Guidelines chain: test delta with commit hashes, skips, the legs measured), `backlog/tasks/task-021...md` (tick every AC with the commit that met it; AC#3's owner-ruled fallback is "no credit" per the task text), `backlog/docs/journal.md` (dated entry), `backlog/docs/arc-room-resolve-retirement.md` (How it unfolded line)
- No source edits.

- [ ] **Step 1: Re-derive every number** — `ctest -N | tail -1` for the test total, `git log --oneline <base>..HEAD` for the per-task deltas, `python3 tools/room_resolve_census.py --check` for the site-sum, `parse_ledger()` for class totals.
- [ ] **Step 2: Write the docs; move TASK-021 to Done (`backlog task edit 21 -s Done --check-ac 1 ... --check-ac 6`).**
- [ ] **Step 3: Commit**: `git commit -m "docs+backlog: TASK-021 room-affect caster snapshot -- as-built, ledger, journal"`.
- [ ] **Step 4 (controller, not the implementer): finalization legs** — `rots64` + boot golden, macOS monolithic + six-seed shuffle, `make smoke-account` (`raw_kill`/`damage` changed — the login path is untouched but the death→save path moved; run it via the host-side method recorded in BUILD.md), then the i386 battery and a bounded adversarial review before the owner's merge call.

---

## Self-Review

**Spec coverage.** AC#1 (snapshot, POD, no raw pointer stored for stats) → Task 1 + Task 3. AC#2 (helpers take the snapshot, one shared path, test that stat changes/extraction don't alter tick damage, remainder roll survives) → Task 2 + Task 5's blaze test. AC#3 (credit when `char_exists`, none otherwise; exp/group_gain, pkill/"died to a player") → Task 4 + Task 5. AC#4 (all four room spells + builder affects; mist move carries the snapshot; future callers can't omit) → Task 3 (two-arg form records `none()` so nothing can be missing), Task 5 (`mist_tick` passes `who` to the adjacent rooms — covers the tick-time spread; the mist MOVE at `limits.cpp:1541` is the one remaining `affect_to_room` call: **Task 5 Step 5 must also change it** to `affect_to_room(room_by_id_total(roomnum), &newaf, room_affect_caster(room, SPELL_MIST_OF_BAAZUNGA) ? *room_affect_caster(room, SPELL_MIST_OF_BAAZUNGA) : caster_snapshot::none())` BEFORE `affect_remove_room(room, tmpaf)` erases the entry — added to Task 5's scope), Task 6 (casts). AC#5 (poison DoT credit; asphyxiation excluded) → Task 4 + Task 6. AC#6 (goldens, ASan, ledger) → every task's gate steps + Task 7.

**Placeholder scan.** The `...fixture:` lines in Task 4/5/6 tests describe fixtures whose exact code is the file-local copy of `mage_tests.cpp`'s helpers (named there); the implementer copies them verbatim — acceptable per the "per-file copy" idiom, but the expected values must be computed from the snapshot fields, not hard-coded.

**Type consistency.** `caster_snapshot::capture/none/resolve/same_character_as` (Task 1) used in Tasks 2, 3, 5, 6; `room_affect_caster`/`set_room_affect_caster`/`affect_to_room(room, af, snapshot)` (Task 3) used in Tasks 5, 6; `damage_credited`/`resolve_poisoner` (Task 4) used in Task 5; `room_affect_tick` (Task 5) used by `limits.cpp`. `new_saves_spell(const caster_snapshot&, const char_data*, int)` is required by Task 5 and must be added in Task 2 (listed in Task 5's notes; add it to Task 2's interface list when implementing).

---

## As built (2026-08-22, HEAD `a1449d14`)

Every task landed; the full account is in `docs/BUILD.md`'s "Room-affect caster snapshot
(TASK-021)" subsection (design, FLAGGED BEHAVIOR-CHANGE INVENTORY, known limits, gates) and
AGENTS.md's chain entry (per-task deltas, 1898 -> 1954). The rulings behind each item below are
in `.superpowers/sdd/2026-08-22-room-affect-caster-snapshot/progress.md`. Deviations from this
plan, in the order they arose:

1. **`caster_snapshot` gained a field the plan's own list does not have: `int tactics`** (Task 2).
   `battle_mage_handler`'s bonus is `value + tactics/2 + mage_level/12`, so without it the live
   `get_magic_power`/`get_saving_throw_dc` forwarders would have silently stripped `tactics/2`
   from every battle mage. Verified as a real regression by sabotage, not assumed.
2. **`name` is `char[64]`, not `char[MAX_NAME_LENGTH + 1]`** (Task 1 fix round). `GET_NAME()`
   returns `player.short_descr` for an NPC, which routinely exceeds the 12-character PC
   player-name limit — and NPC casters are the common case for room affects.
3. **`resolve()` goes through a pointer registry and never dereferences `identity_ptr`** (Task 1
   fix round). `abs_number` slots recycle and `free_char` releases the storage, so the plan's
   `identity_ptr->abs_number == abs_number` read was a genuine use-after-free. Closed with a
   parallel `characters_by_abs_number[MAX_CHARACTERS]` table beside the `char_exists` bit table,
   a two-argument `set_char_exists(int, char_data*)`, and a bounds-checked `char_by_abs_number()`.
   `resolve_poisoner()` (Task 4) uses the identical shape for the same reason.
4. **`other_side`'s live form does not call `capture()`** (Task 2). Both public forms funnel into
   one file-local `other_side_impl(is_npc, is_charmed, race, other)` — one body, byte-identical
   inputs — because `other_side` runs inside per-character display and grouping loops where a
   full capture (two `get_prof_level` lookups, a `GET_PERCEPTION` evaluation, a 64-byte
   `snprintf`) for three fields is a hot-path regression. Every other helper is a plain
   capture-forwarder exactly as planned.
5. **`max_race_prof_level` lives in `src/core/include/rots/core/character.h`**, not beside the
   macro in `src/utils.h` (Task 2). `RACE_*`/`PROF_*` are defined in `character.h` and `utils.h`
   does not include it; putting the function next to the constants keeps exactly one table, which
   is what the plan actually wanted, and `GET_MAX_RACE_PROF_LEVEL` forwards to it.
6. **`tick_spell_damage` was never written; `apply_spell_damage_credited` replaced it** (Tasks
   4/5). The plan's Task-5 sketch duplicated `apply_spell_damage`'s multiplier math. Instead Task
   4 extracted one static `scale_spell_damage()` that both `apply_spell_damage` and the new
   `apply_spell_damage_credited(who, attacker, victim, credited_killer, dam, spell, loc)` run, so
   the saving-throw multiplier has one body.
7. **Task 4 absorbed two items the plan put elsewhere, and handed two of its own to Task 6.**
   Absorbed: `get_victim_saving_throw`'s snapshot overload (plan: Task 2) and
   `apply_spell_damage_credited` (plan: Task 5's `tick_spell_damage`). Handed on: the ORDINARY
   poison DoT at `affect_update_person`'s `case SPELL_POISON:` — which is what every poison affect
   a spell, a bite or a meal applied actually ticks through, and which the plan never scoped, so
   AC#5 would not have been met — plus four further poison sources the plan's file list omitted
   (poisoned drink and food in `act_obj2.cpp`, black arrow in `mage.cpp`, the vampire huntress's
   bite in `spec_pro.cpp`). Both moved into Task 6 and landed there.
8. **The single shared writer `record_poison_origin()` is not in this plan at all**, and shipped
   with BOTH parameters non-const (Task 6). The plan wrote the two `poisoned_by*` fields inline at
   each site; a controller ruling replaced that with one writer, so a null poisoner CLEARS both
   halves rather than leaving a half-set record that would answer for whoever holds that
   `abs_number` slot today. The ruling's own signature took a `const char_data* poisoner`, but the
   stored field is a mutable `char_data*`, so that would have forced a `const_cast` inside the one
   function whose job is to write the pair honestly; every caller already holds a mutable pointer.
9. **The removed-last-affect guard is `continue`, not the plan's `break`** (Task 5). That
   statement sits inside `case ROOMAFF_SPELL:`, so `break` would leave only the *switch* and fall
   straight into the duration/mist-move code that dereferences `tmpaf` — it would guard nothing.
10. **`room_affect_tick.h` sits flat at `src/`**, as this plan directed. `src/combat/` would be
    the layering-correct home (the implementation is `rots_combat`); recorded as a known
    deviation from the physical-layout convention rather than silently moved.
11. **The plan's self-review addition landed as written**: Task 5 threads the snapshot through
    the mist MOVE in `affect_update_room`, reading the record into a local COPY before
    `affect_remove_room()` erases it.
12. **Two things landed that the plan did not ask for.** Task 5 repaired a pre-existing
    use-after-free in `affect_update_room`'s mist-move branch (`tmpaf = nullptr` after the
    removal; ASan-witnessed, observably a no-op). Task 6 lowered `MAXIMUM_TODO_COUNT` 578 -> 576,
    which follows from the resolver hoists the brief did ask for — leaving the two drained sites
    as ratchet slack would have let a future change add two unproven resolver sites for free.
13. **Two follow-ups were filed rather than fixed**: TASK-023 (the shared `ScopedZoneTableOwner`
    fixture is one zone slot short under `recalc_zone_power`, ASan-caught at Task 4; that suite
    carries a file-local two-slot table) and TASK-024 (character handles — retire raw `char_data`
    holders, from the design discussion this wave provoked). Task 6's own F1 — `spell_haze` and
    `spell_poison` dereferencing `caster` before their `if (!caster)` test — was folded into
    TASK-022's scope, which already carried the same shape in `spell_blaze`.

14. **A new library TU must be listed in THREE build files, not two.** `caster_snapshot.cpp`
    (`rots_entity`) and `room_affect_tick.cpp` (`rots_combat`) were wired into
    `src/CMakeLists.txt`'s `ROTS_*_SOURCES` and `src/Makefile`'s `OBJNAMES`, and both of those
    gates went green — but `src/tests/Makefile` carries its OWN production-object list
    (`OBJFILES`, line 115), and the flat monolithic test binary link is the only gate that reads
    it. The i386 battery's step 2 caught the omission as undefined references to
    `caster_snapshot::capture/none/same_character_as` and `room_affect_tick(...)`; both objects
    were added there (the `vpath` on line 113 already covers `../entity` and `../combat`). Same
    class as the recurring "new linkcheck missing from the root Makefile" finding: CMake/flat
    parity is part of the reconciliation method, and it now spans three lists.
