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
| `rots_platform` | L0 | 9 | `rots_net, signals, rots_crypt, rots_rng, clock, crashsave_schedule, json_utils, safe_template, player_file_finalize` |
| `rots_core` | L1 | 2 + split headers | `consts, config` + the carved-up data model (Section 5) |
| `rots_entity` | L2 | 6 | `char_utils, object_utils, environment_utils, handler, utility, char_utils_combat` |
| `rots_persist` | L3 | ~14 | `db_players` (from `db.cpp`), `objsave, boards, mail, pkill, character_json, objects_json, exploits_json, account_management (+6 #included fragments), account_cache, convert_exploits, convert_plrobjs, save_benchmark, savebench` |
| `rots_world` | L3 | ~15 | `db_world` (from `db.cpp`), `shapemdl, shapemob, shapeobj, shaperom, shapescript, shapezon, zone, script, mudlle, mudlle2, graph, weather, mob_csv_extract, obj2html` |
| `rots_combat` | L3 | 16 | `fight, limits, skill_timer, mobact, ranger, clerics, mage, mystic, profs, spell_pa, spec_pro, spec_ass, battle_mage_handler, weapon_master_handler, wild_fighting_handler, olog_hai` |
| `rots_commands` | L4 | 15 | `interpre, act_comm, act_info, act_move, act_obj1, act_obj2, act_offe, act_othe, act_soci, act_wiz, modify, delayed_command_interpreter, wait_functions, shop, ban` |
| `rots_app` | L5 | 5 | `comm, protocol, color, big_brother, db_boot` (from `db.cpp`) |

**Notes and honest caveats:**

- The three L3 libraries are a **peer tier**, not a sub-stack: `rots_persist`/`rots_world`/
  `rots_combat` may cross-reference within L3 (e.g. `db_players` needs world helpers). Treat L3 as
  one layer until `db.cpp` is fully split; do not pretend one L3 lib sits strictly below another.
- `olog_hai` (logging) and `big_brother` (anti-cheat) are cross-cutting; they are parked where
  their current dependencies point and are candidates to move once boundaries settle.
- The 6 `account_management_*.cpp` files are `#include`d into `account_management.cpp` (they are
  fragments, not separately compiled TUs) and stay together in `rots_persist`.

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

---

## 10. Sequencing (incremental, boundary-safe)

Each step keeps `ageland` building and all goldens green; no big-bang cutover.

1. **CMake targets, no code moves.** Introduce the 8 library targets and the `rots_convert` target
   in CMake against the *existing* flat files (via source lists), establishing the link graph and
   retiring the Makefile. Nothing moves yet; the graph is declared and enforced.
2. **Foundation first.** Physically relocate `rots_platform` (zero data-model coupling — lowest
   risk) and verify the layer builds standalone.
3. **Split the god-header.** Carve `structs.h` into the `rots/core/` DAG (Section 5). This is the
   highest-leverage step for recompile time and unblocks the decoupling.
4. **Split `db.cpp`** into `db_world` / `db_players` / `db_boot`; stand up `rots_convert` and make
   it link + pass the fixture goldens. This proves the persistence boundary.
5. **Peel the domain libs** (`rots_entity`, then `rots_persist`/`rots_world`/`rots_combat`, then
   `rots_commands`, then `rots_app`), one at a time, cutting welds as `rots_convert` and the link
   graph surface them.
6. **Staged decouplings** (location Stage 1→2, account/session separation, macro→function) run as
   long-tail work *within* the now-stable library boundaries.

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
