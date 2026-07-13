# char_data / obj_data Ownership Map (RAII Lifecycle-Audit — Task 1, the gate)

**Status:** READ-ONLY audit. No code changed. This document is the greenlight
source that T3+ conversion tasks consume, and the escalation input T2 brings to
the owner. A mislabeled OWNING field that is actually prototype-shared becomes a
double-free the moment a later task converts it, so every free/alloc claim below
cites `file:line` against the source as it exists on branch
`modernization/raii-audit`.

## 0. The allocation model (load-bearing context)

Everything hinges on how char_data/obj_data storage is obtained and released:

- `CREATE`/`CREATE1` (`utils.h:208-217`) call `create_function`
  (`utility.cpp:1470`) which is **`calloc`** — raw, zeroed, *unconstructed* storage.
- `RELEASE` (`utils.h:218-223`) calls `free_function` (`utility.cpp:1495`) which is
  **`free`** — and is gated by `global_release_flag`.
- So char_data and obj_data are **C-allocated (calloc) and C-freed (free)**, not
  `new`/`delete`.
- `clear_char` (`db.cpp:3607`) runs a **placement-new** over the calloc'd storage
  (`new (ch) char_data();`, `db.cpp:3626`) to properly construct the non-trivial
  members (`specialization_data`, `player_damage_details::damage_map`), then
  `CREATE1(ch->profs, ...)`. `read_mobile` does the same placement-new
  (`db.cpp:1482`) before the prototype struct-copy.
- **`free_char` (`db.cpp:3381`; `free_alias_list` helper at `db.cpp:3370`) never runs `~char_data()`.** (The task brief's "`free_char` :3363 / `free_obj` :3411" line numbers were slightly stale; the citations throughout this map are against current source: `free_char` :3381, `free_alias_list` :3370, `free_obj` :3432.) It hand-frees a fixed
  set of members, manually calls `->reset()` on the two members that own heap
  (`extra_specialization_data.reset()` `db.cpp:3425`; `damage_details.reset()`
  `db.cpp:3426`), then `RELEASE(ch)` = `free(ch)` (`db.cpp:3428`). The destructor
  is bypassed. This asymmetry (placement-**new** on construct, **no** placement-
  delete on teardown) is the central constraint for the instance-ownership verdict
  in §6.

The instance-copy mechanism for NPCs/objects is a **whole-struct assignment**:
`*mob = mob_proto[i];` (`db.cpp:1484`) and `*obj = obj_proto[i];` (`db.cpp:1790`).
This bitwise/memberwise-copies **every pointer field**, so an NPC instance and an
object instance silently *alias* the prototype's `char*` strings (and, for NPCs,
`profs`). This aliasing — not any per-field alloc — is the entire reason the
string fields are CONDITIONAL and not OWNING.

---

## 1. Bucket definitions

- **OWNING** — this instance allocates it and `free_char`/`free_obj`
  unconditionally frees it (or a member destructor/reset does). Safe to convert.
- **NON-OWNING** — world-graph cross-link or back-ref; freed by nobody through this
  pointer (freed on a different path, or owned by another subsystem/the prototype).
  Stays raw **forever**. OUT of scope for conversion (global constraint).
- **CONDITIONAL** — freed only under a runtime test (`IS_NPC`/`item_number`),
  because the pointer is prototype-shared for one population and instance-owned for
  another. The prototype-string escalation set. Feeds T2.
- **LOCAL-SCRATCH** — untyped/transient scratch, or allocated-and-freed inside one
  function, never a durable owned resource of the instance.

---

## 2. char_data pointer fields

### 2a. Top-level char_data (`structs.h:1732-1816`)

| Field | Type | Role | Bucket | Evidence |
|---|---|---|---|---|
| `profs` | `char_prof_data*` | prof coefficients/colors/specialization | **CONDITIONAL** | Alloc'd for every char in `clear_char` (`db.cpp:3627` `CREATE1`). NPC instance **aliases the proto's** `profs` via `*mob = mob_proto[i]` (`db.cpp:1484`). Freed **only** in the `!IS_NPC || nr==-1` branch (`db.cpp:3401` `RELEASE(ch->profs)`) — normal NPCs (nr≥0) never free it, so the shared proto pointer is not double-freed. |
| `skills` | `byte*` (array `MAX_SKILLS`=256) at audit time; **now `std::vector<byte>`, RAII T3 CONVERTED — see §4** | pracs spent per skill | **OWNING** | PC-only: `clear_char` allocates only when `mode != MOB_ISNPC` (`db.cpp:3651`). mob_proto built with `MOB_ISNPC` → NULL; NPC instance copies NULL. Freed if non-null (`db.cpp:3415-3419`, with a SYSERR log if an NPC ever had one). Never prototype-shared. |
| `knowledge` | `byte*` (array `MAX_SKILLS`) at audit time; **now `std::vector<byte>`, RAII T3 CONVERTED — see §4** | computed knowledge | **OWNING** | Same as `skills`: `db.cpp:3652` alloc, `db.cpp:3420-3422` free. PC-only, never shared. |
| `affected` | `affected_type*` | head of spell-affect list | **OWNING (POOL-MEDIATED — see caveat)** | Freed as a chain in `free_char`: `while (ch->affected) affect_remove(ch, ch->affected);` (`db.cpp:3391-3392`); also drained in `extract_char` (`handler.cpp:2051,2064`). **Nodes are NOT plainly `CREATE`'d/`RELEASE`'d per instance — they are pool-mediated:** allocated via `get_from_affected_type_pool()` → `CREATE`/calloc (`handler.cpp:612`, reached from `affect_to_char` at `handler.cpp:663`/`703`), freed via `put_to_affected_type_pool()` → `free()` (`handler.cpp:624`) inside the `affect_remove` drain (`handler.cpp:752`). AND `affect_remove` also unwinds a secondary **global** `affected_list` tracking structure (`handler.cpp:88`, unwound `handler.cpp:754-759`). So the naive `std::forward_list`/owned-nodes target below is **UNSAFE as written**. |
| `equipment[MAX_WEAR]` | `obj_data*[22]` | worn items | **NON-OWNING** | World graph. `extract_char` moves each to room or extracts it separately (`handler.cpp:1993-2005`), then nulls the slots; `free_char` never touches them. |
| `carrying` | `obj_data*` | inventory list head | **NON-OWNING** | World graph. Not freed by `free_char`; objects have independent lifecycle via `extract_obj`. |
| `desc` | `descriptor_data*` | player connection (NULL for mobs) | **NON-OWNING** | Back-ref. `free_char` never touches `ch->desc` (asserted by every test fixture, e.g. `test_char_cleanup.h:16`; freed via `close_socket`). |
| `next_in_room` | `char_data*` | room->people link | **NON-OWNING** | World-graph list link. |
| `next` | `char_data*` | `character_list` link | **NON-OWNING** | World-graph list link; unlinked in `extract_char` (`handler.cpp:2010-2022`). |
| `next_fighting` | `char_data*` | combat list link | **NON-OWNING** | World-graph list link. |
| `next_fast_update` | `char_data*` | fast-update list link | **NON-OWNING** | World-graph list link. |
| `followers` | `follow_type*` | list of this char's followers | **NON-OWNING** (via free_char) | Nodes `CREATE`'d in `handler.cpp:946` and released by the follow subsystem (stop/die_follower), **not** by `free_char`. Managed elsewhere. |
| `master` | `char_data*` | who this char follows | **NON-OWNING** | Cross-link. |
| `mount_data.mount` / `.rider` / `.next_rider` | `char_data*` | mount graph | **NON-OWNING** | Cross-links (`structs.h:1288-1297`). |
| `group` | `group_data*` | group membership | **NON-OWNING** (via free_char) | `new group_data`/`delete group` live in the grouping subsystem (`act_othe.cpp:500,526,603`); `free_char` never frees it. Managed elsewhere. |
| `temp` | `void*` | "any special structures if need be" | **LOCAL-SCRATCH** | Untyped transient scratch; no durable ownership contract. |
| `delay.next` | `char_data*` | `waiting_list` link (inside `waiting_type delay`) | **NON-OWNING** | World-graph list link (`structs.h:398`, `handler.cpp:1982-1985`). |
| `next_die` | `char_data*` | `death_waiting_list` link | **NON-OWNING** | World-graph list link (`structs.h:1809`). |

`group` / `damage_details.damage_map` keys and `extra_specialization_data` are
covered in §2d.

### 2b. char_player_data (`player.*`, `structs.h:1044-1065`)

| Field | Type | Bucket | Evidence |
|---|---|---|---|
| `name` | `char*` | **CONDITIONAL** | Prototype-shared for NPCs via `*mob = mob_proto[i]` (`db.cpp:1484`). Freed only in `!IS_NPC \|\| nr==-1`: `RELEASE(GET_NAME(ch))` (`db.cpp:3396`). See §5 for the `GET_NAME` NPC/PC divergence. |
| `short_descr` | `char*` | **CONDITIONAL** | Prototype-shared; freed only in the guarded branch `db.cpp:3398`. For NPCs `GET_NAME` **reads this**, not `name` (`utils.h:355`). |
| `long_descr` | `char*` | **CONDITIONAL** | Prototype-shared; freed only in the guarded branch `db.cpp:3399`. |
| `description` | `char*` | **CONDITIONAL** | Prototype-shared; freed only in the guarded branch `db.cpp:3400`. |
| `title` | `char*` | **CONDITIONAL** | Prototype-shared for NPCs; freed only in the guarded branch `db.cpp:3397`. |
| `death_cry` | `char*` | **NON-OWNING** (proto-owned) | Allocated **only on mob_proto** (`db.cpp:1651` via `fread_string`, or 0 at `db.cpp:1654`); struct-copied (aliased) into NPC instances; **never freed by `free_char`** at all. The prototype owns it; the instance borrows it. Only read (`fight.cpp:861-871`). |
| `death_cry2` | `char*` | **NON-OWNING** (proto-owned) | Same as `death_cry` (`db.cpp:1652,1655`, read `fight.cpp:870-871`). |

The commented-out NPC `else` branch in `free_char` (`db.cpp:3402-3413`) is the
historical proof of the sharing model: it would have freed `name/title/
short_descr/long_descr/description` for an NPC **only if the instance pointer
differed from `mob_proto[i]`'s** (`ch->player.name != mob_proto[i].player.name`).
It is disabled — so today normal NPCs free none of these strings. That dead branch
is exactly the "model the sharing explicitly" option T2 must weigh (§5).

### 2c. char_special_data (`specials.*`, `structs.h:1123-1206`)

| Field | Type | Bucket | Evidence |
|---|---|---|---|
| `fighting` | `char_data*` | **NON-OWNING** | Combat cross-link. |
| `hunting` | `char_data*` | **NON-OWNING** | AI cross-link. |
| `alias` | `alias_list*` | **OWNING** | PC-only linked list built by `Crash_alias_load` (`objsave.cpp:1200-1217`, each node `CREATE1`'d with a `CREATE`'d `.command`). Freed via `free_alias_list` (`db.cpp:3370-3378`, called `db.cpp:3388`). 0 for mobs and for freshly-cleared chars — NULL-safe. (This is the backlog-T2 leak that was fixed.) |
| `poofIn` | `char*` | **OWNING** | Per-instance. For PCs/gods: a `str_dup`'d arrival string. For special mobs (`store_prog_number!=0`): a `SPECIAL_STACKLEN`-long buffer `CREATE`'d in `read_mobile` (`db.cpp:1549-1550`). For ordinary mobs: set to 0 (`db.cpp:1574`). Unconditionally `RELEASE`'d (`db.cpp:3385`) — safe because non-special mobs hold 0. |
| `poofOut` | `char*` | **OWNING** | Same shape as `poofIn`: special-mob `special_list` `CREATE1`'d (`db.cpp:1551-1552`) / else 0 (`db.cpp:1575`). Unconditionally `RELEASE`'d (`db.cpp:3386`). |
| `union1.reply_ptr` | `char_data*` | **NON-OWNING** | Cross-link (PC reply target), when the union is used as `reply_ptr` (`structs.h:1157-1160`). |
| `union1.prog_number` | `int*` | **OWNING (LEAKED — see §7 notes)** | Special-mob call list `CREATE`'d in `read_mobile` (`db.cpp:1556`); **not freed** by `free_char`. Latent leak, out of this wave's conversion scope. |
| `union2.prog_point` | `int*` | **OWNING (LEAKED)** | Special-mob call-point list `CREATE`'d (`db.cpp:1557`); **not freed** by `free_char`. Same latent leak. |
| `memory` | `memory_rec*` | **OWNING (freed off the free_char path)** | NPC attacker memory, nodes `CREATE`'d in `mobact.cpp:439`; freed via `clear_memory` in `extract_char` (`handler.cpp:2044`), **not** in `free_char`. |
| `recite_lines` | `char*` | **NON-OWNING** (borrowed) | Points **into** an object's `ex_description->description` (`spec_pro.cpp:3579`) or is advanced within that buffer / NULL'd (`spec_pro.cpp:3555-3557`); nulled at load (`db.cpp:1577`). Borrowed pointer; never freed through the char. |
| `script_info` | `info_script*` | **NON-OWNING** (proto/subsystem-owned) | Only ever set to 0 (`db.cpp:3639`); no `RELEASE`/`delete` of a char's `script_info` exists anywhere. Points at prototype/script-table data. |

### 2d. Managed members holding heap (not raw pointer fields, noted for completeness)

- `extra_specialization_data` (`specialization_data`, `structs.h:1782`) owns
  `current_spec_info` (`specialization_info*`, `structs.h:1537`) — `new`'d in
  `specialization_data::set` (`char_utils.cpp:1490-1508`), `delete`'d in
  `::reset` (`char_utils.cpp:1475-1478`) and the member's own destructor
  (`structs.h:1531`). `free_char` calls `.reset()` (`db.cpp:3425`). **Already
  RAII-managed; safe.** NULL on all mob protos (set to `PS_None` in `clear_char`
  `db.cpp:3644`), so the `*mob = mob_proto[i]` copy of this member never aliases a
  live object — but note the raw copy-assignment *would* shallow-copy the pointer
  if a proto ever held one (latent, currently harmless).
- `damage_details` (`player_damage_details`) holds `std::map<char_data*, ...>`
  (`structs.h:1681`); keys are **NON-OWNING** cross-links; `.reset()` just clears
  (`structs.h:1676`, called `db.cpp:3426`).

---

## 3. obj_data pointer fields (`structs.h:477-521`)

| Field | Type | Bucket | Evidence |
|---|---|---|---|
| `name` | `char*` | **CONDITIONAL** | Prototype-shared via `*obj = obj_proto[i]` (`db.cpp:1790`). Freed **only when `item_number == -1`** (`free_obj` `db.cpp:3437-3438`). Prototype-loaded objects have `item_number == i ≥ 0` → not freed. |
| `description` | `char*` | **CONDITIONAL** | Same guard; freed `db.cpp:3439`. |
| `short_description` | `char*` | **CONDITIONAL** | Same guard; freed `db.cpp:3440`. |
| `action_description` | `char*` | **CONDITIONAL** | Same guard; freed `db.cpp:3441`. |
| `ex_description` | `extra_descr_data*` | **CONDITIONAL** | List of `{keyword, description, next}` (`structs.h:377-381`). Freed as a chain **only when `item_number == -1`** (`db.cpp:3442-3448`). Prototype-shared otherwise. The dead NPC/obj `else` branch (`db.cpp:3449-3465`) mirrors the char one: would free only if the instance pointer `!= obj_proto[nr]`'s. |
| `carried_by` | `char_data*` | **NON-OWNING** | Cross-link. |
| `in_obj` | `obj_data*` | **NON-OWNING** | Container back-ref. |
| `contains` | `obj_data*` | **NON-OWNING** | Contents-list head; contents extracted independently in `extract_obj` (`handler.cpp:1831-1832`). |
| `next_content` | `obj_data*` | **NON-OWNING** | Contents-list link. |
| `next` | `obj_data*` | **NON-OWNING** | `object_list` link (`db.cpp:1797`, unlinked `handler.cpp:1835-1845`). |

### obj_flag_data (`structs.h:403-447`)

| Field | Type | Bucket | Evidence |
|---|---|---|---|
| `script_info` | `info_script*` | **NON-OWNING** (proto/subsystem-owned) | Only ever set to 0 (`db.cpp:3665`); no per-object free exists. |

---

## 4. Per-boundary verdicts

### GREENLIGHT-CONVERT (OWNING / LOCAL-SCRATCH)

| Field(s) | Target type | Risk tier | Note |
|---|---|---|---|
| `skills`, `knowledge` | `std::vector<byte>` (RAII T3, converted) | **Low — CONVERTED** | PC-only, never aliased, unconditional free. `std::vector` (not `std::array`) chosen over the array alternative because "does this character even have a skill array" is a runtime property this codebase tests constantly (GET_SKILL/GET_KNOWLEDGE family, `recalc_skills`, `handle_pracs`, `char_data::reset_skills`/`get_spent_practice_count`) via a null-pointer check on the old `byte*` — `std::array<byte, MAX_SKILLS>` has no empty state to represent "NPC, never allocated," so every one of those call sites would need a parallel bool/sentinel; `std::vector` preserves the exact "empty ⟺ absent" contract (`.empty()` replaces the old truthiness check) with no new state. `free_char` (`db.cpp`) no longer `RELEASE()`s these two fields — instead it explicitly clears both vectors (`ch->skills = {}; ch->knowledge = {};`) before `RELEASE(ch)` (`free_function(ch)`, which runs WITHOUT `~char_data()`; see §0/§6) so their heap buffers don't leak. `clear_char`/`store_to_char`/`init_char` use `.assign(MAX_SKILLS, 0)` in place of `CREATE(..., byte, MAX_SKILLS)`. JSON/text save-load (`character_json.cpp`, `char_file_u`) are untouched — they read/write the separate fixed-size `char_file_u::skills[MAX_SKILLS]` array, not this field, so the wire format is unaffected. |
| `specials.alias` | `std::unique_ptr`-linked list, or model as `std::vector<alias_entry>` | **Medium** | Owned list; `Crash_alias_load` builds it, `free_alias_list` frees it. Conversion must preserve the `MAX_ALIAS` overflow quirk (`objsave.cpp:1195`) and 20-byte `keyword`. |
| `specials.poofIn`, `specials.poofOut` | `std::string` (PC path) — **but see caveat** | **High** | For PCs these are plain owned strings → trivially `std::string`. **BUT** the same fields are reused by special mobs as raw `long[]`/`special_list` buffers (`db.cpp:1549-1552`) via `SPECIAL_LIST_AREA`/`SPECIAL_STACKLEN` casts. A blind `std::string` conversion breaks the special-mob overload. Convert only after decoupling the special-mob storage into its own typed field. Tier High for that reason. |
| `affected` | keep intrusive list; a `std::forward_list`/owned-nodes target is **only** valid AFTER the prerequisite below | **GREENLIGHT-WITH-PREREQUISITE** | **Prerequisite (blocking): retire the `affected_type` pool (`get_/put_to_affected_type_pool`, `handler.cpp:602-624`) AND the global `affected_list`/`affected_list_pool` tracking (`handler.cpp:88-89`, unwound in `affect_remove` at `handler.cpp:754-759`) FIRST.** Until both are retired, a node's storage is owned by the pool and its membership is mirrored in a global list — an owned-node/`forward_list` conversion would double-free (pool still `free()`s the node) and desync the global list. Also note `affected_type` is serialized in `CHAR_FILE_U` and walked widely; low payoff. **No T3+ task may take the `forward_list` suggestion literally without doing the pool/`affected_list` retirement as an explicit predecessor step.** |
| `extra_specialization_data.current_spec_info` | already `new`/`delete`; could be `std::unique_ptr<specialization_info>` | **Low** | Cosmetic RAII tidy inside `specialization_data`; already leak-safe. |

### HOLD-RAW-FOREVER (NON-OWNING) — reason: world graph / back-ref / other-owner

All fields bucketed NON-OWNING in §2–§3: `equipment[]`, `carrying`, `desc`,
`next_in_room`, `next`, `next_fighting`, `next_fast_update`, `master`,
`mount_data.*`, `delay.next`, `next_die`, `specials.fighting`, `specials.hunting`,
`union1.reply_ptr`, `specials.recite_lines` (borrowed), `specials.script_info`,
`player.death_cry(2)` (proto-owned), `damage_map` keys; and obj `carried_by`,
`in_obj`, `contains`, `next_content`, `next`, `obj_flags.script_info`.
**Reason:** converting any of these to an owning smart pointer would make the
world graph delete shared nodes (double-free / dangling), violating the global
constraint "the world graph stays raw." `followers`, `group`, and `memory` are
owned by *other subsystems* (follow / grouping / mob-memory), freed on paths other
than `free_char` — they stay raw here.

### ESCALATE (CONDITIONAL) → feeds T2

Char: `player.name`, `player.short_descr`, `player.long_descr`,
`player.description`, `player.title`, and `profs` (same guarded branch).
Obj: `name`, `description`, `short_description`, `action_description`,
`ex_description`. See §5 for the full escalation package.

---

## 5. Escalation quantification (T2 owner decision package)

### The shared-string set (CONDITIONAL)

**Count of prototype-shared pointer fields: 11** —
5 char strings (`name`, `short_descr`, `long_descr`, `description`, `title`),
+ char `profs` (same conditional), + 4 obj strings (`name`, `description`,
`short_description`, `action_description`), + obj `ex_description` list.

### The exact free-path logic (quote)

Char (`db.cpp:3394-3413`):
```c
if (!IS_NPC(ch) || (IS_NPC(ch) && ch->nr == -1)) {
    RELEASE(GET_NAME(ch));            // NPC: short_descr; PC: name  (utils.h:355)
    RELEASE(ch->player.title);
    RELEASE(ch->player.short_descr);
    RELEASE(ch->player.long_descr);
    RELEASE(ch->player.description);
    RELEASE(ch->profs);
} /* else if ((i = ch->nr) > -1) {  ...disabled per-pointer-diff branch... } */
```
Obj (`db.cpp:3437-3465`):
```c
if ((nr = obj->item_number) == -1) {
    RELEASE(obj->name); RELEASE(obj->description);
    RELEASE(obj->short_description); RELEASE(obj->action_description);
    if (obj->ex_description) for (...) { RELEASE(keyword); RELEASE(description); RELEASE(node); }
} /* else { ...disabled `!= obj_proto[nr]` branch... } */
```
The instance never owns these when it came from a prototype (`*mob = mob_proto[i]`
/ `*obj = obj_proto[i]`); the guard is what prevents freeing the prototype's copy.

### Read-site blast radius (why a type change is expensive)

Counts (production `src/*.cpp`/`*.h`, tests excluded; grep-derived, approximate —
they establish order of magnitude for the owner):

| Field / accessor | Read sites | Notes |
|---|---|---|
| `GET_NAME(` macro | **~288** | `#define GET_NAME(ch) (IS_NPC(ch) ? (ch)->player.short_descr : (ch)->player.name)` (`utils.h:355`). A `std::string` migration must keep this ternary returning something `printf("%s")`/`strcpy`-compatible at all 288 sites. |
| `player.name` (direct) | ~108 | |
| `player.description` | ~32 | |
| `player.short_descr` | ~27 | Also every NPC `GET_NAME`. |
| `GET_TITLE(` / `player.title` | ~27 / ~14 | `#define GET_TITLE(ch) ((ch)->player.title)` (`utils.h:357`). |
| `player.long_descr` | ~24 | |
| obj `short_description` (any var) | ~91 | Accessed via many object variable names, not just `obj->`. |
| obj `name` (`obj->name`) | ~31 | |
| obj `action_description` (any var) | ~29 | |
| obj `description` (`obj->description`) | ~23 | |
| obj `ex_description` (`obj->`) | ~17 | Plus list-walk sites. |

**Total order of magnitude: 600+ read sites**, the overwhelming majority
consuming a raw `char*` through `printf("%s")`, `strcpy`, `str_cmp`, `strcat`,
`std::format` with `static_cast<const char*>`, and passing to functions typed
`char*`/`const char*`. This is the blast radius of changing the field type.

### Preliminary recommendation: **KEEP-RAW-CONDITIONAL** (do NOT convert to `std::string` in this wave)

Reasoning:
1. **Sharing is real and load-bearing.** The prototype/instance aliasing via
   whole-struct copy (`db.cpp:1484,1790`) means "who owns this string" is a runtime
   property (`IS_NPC && nr` / `item_number`), not a static one. `std::string` has
   single-owner value semantics; you cannot make an NPC instance's `std::string
   name` *alias* the prototype's without a copy. Two escape hatches, both costly:
   - **(a) Make every instance own a copy** (deep-copy strings at `read_mobile`/
     `read_object`). Removes the conditional, but adds a heap alloc + copy per mob
     spawn / per object load — a hot path (mobs respawn constantly). Measurable
     runtime + memory regression on a MUD that spawns thousands of mobs.
   - **(b) Model the sharing explicitly** with a shared/interned string type
     (`std::shared_ptr<const std::string>` or a string-intern handle) so instances
     cheaply share the prototype's immutable text. Cleaner long-term, but touches
     all ~600 read sites and the save/load paths, and must interop with the
     `GET_NAME` ternary and the C string APIs still pervasive in the codebase.
2. **The dead per-pointer-diff branches** (`db.cpp:3402-3413`, `3449-3465`) show the
   original authors already reasoned about `ptr != proto.ptr` sharing and chose to
   disable per-instance freeing entirely. Any conversion must consciously replace
   that model, not stumble into it.
3. **Blast radius vs. payoff.** 600+ raw-`char*` read sites for a field set that is
   already leak-safe (prototype owns; freed once at proto teardown). The RAII win
   is marginal; the regression/behavior-drift risk (goldens are byte-pinned) is
   high.

**What the owner is being asked at T2:** ratify KEEP-RAW-CONDITIONAL for the 11
shared fields for this wave, OR fund option (b) (interned/shared immutable
prototype strings) as its own separately-scoped, characterization-heavy sub-wave.
Option (a) is not recommended (hot-path cost). If KEEP-RAW is ratified, these 11
fields are explicitly excluded from T3+ and the audit's greenlit set stands as §4.

---

## 6. Instance-ownership feasibility verdict (`unique_ptr<char_data, free_char_deleter>`)

**Verdict: FEASIBLE but blocked on a teardown-symmetry fix; sequence it LAST (T6),
and only after the CONDITIONAL question (T2) is settled.**

- A `unique_ptr<char_data, free_char_deleter>` where `free_char_deleter` calls
  `free_char(p)` is mechanically straightforward and would give factory call sites
  (`read_mobile`, the PC-create paths) exception-safe, single-owner handles.
- **The blocker is the construct/teardown asymmetry (§0).** `clear_char`/
  `read_mobile` **placement-new** the object over calloc storage (`db.cpp:3626,
  1482`), but `free_char` frees with `free_function(ch)` (`db.cpp:3428`) **without
  running `~char_data()`**. It hand-destroys exactly the two heap-owning members
  (`extra_specialization_data.reset()`, `damage_details.reset()`). This works today
  only because every other durable member is a POD pointer freed explicitly. **The
  moment any member becomes a type with a non-trivial destructor** (`std::string`,
  `std::vector`, `std::unique_ptr` from a T3/T4/T5 conversion), `free_char` will
  leak/UB unless it first invokes `ch->~char_data()` (or explicitly destroys that
  member) before `free_function(ch)`.
- Therefore the factory task must: (1) make `free_char` call the destructor
  explicitly (`ch->~char_data();` then `free_function(ch)`), keeping the
  calloc/placement-new/explicit-dtor/free lifecycle internally consistent, **or**
  (2) migrate the whole allocation to `new`/`delete` (larger blast radius — every
  `CREATE(mob,...)` site, and the `global_release_flag` gating semantics in
  `RELEASE`). Option (1) is the minimal, lowest-risk path and is the natural home
  for the `free_char_deleter`.
- Because the destructor must correctly destroy whatever members earlier tasks
  converted, **T6 (factory) must come after the member conversions it needs to
  destroy**, and its acceptance is an ASan/LeakSanitizer proof (global constraint:
  "same allocations freed, same order, no double-free, no leak").

---

## 7. Proposed T3+ task sizing (coordinator input)

Derived from the greenlit set (§4). Each conversion task runs the dual local gate
+ macOS ASan gate (global constraints); output-visible ones get byte-pins that
pass against unchanged source first.

| Task | Scope | Risk tier | Depends on |
|---|---|---|---|
| **T3** | `skills` + `knowledge` → `std::vector<byte>` (see §4 for why `vector` over `array`). PC-only, unaliased, unconditional free. Preserve JSON/text save-load wire format. **DONE.** | **Low — CONVERTED** | T1 (this doc) |
| **T4** | `specials.alias` list → owned `std::unique_ptr`/vector model. Preserve `MAX_ALIAS`/20-byte-keyword quirks. | **Medium** | T1 |
| **T5** | Decouple special-mob overload of `poofIn`/`poofOut`/`union1`/`union2` (the `SPECIAL_LIST_AREA` reuse) into a typed field, THEN convert the PC `poofIn`/`poofOut` to `std::string`. Also address the `union1.prog_number`/`union2.prog_point` leak (§2c) here. | **High** | T1 |
| **T6** | `free_char` teardown-symmetry fix (explicit `~char_data()` before free) + `unique_ptr<char_data, free_char_deleter>` factory. ASan/Leak proof. | **High** | T3, T4, T5 (must destroy whatever they converted) |
| **T7 (optional / gated on T2)** | Only if the owner funds option (b): interned/shared immutable prototype strings for the 11 CONDITIONAL fields. Separately scoped, characterization-heavy. | **Very High** | T2 ruling |
| **(No task)** | The NON-OWNING world graph — explicitly out of scope, stays raw forever. | — | — |

Recommended per-wave contents (if T2 ratifies KEEP-RAW-CONDITIONAL): **T3, T4, T5,
T6.** T7 only if the owner elects to model sharing.

---

## 8. UNRESOLVED

Nothing blocks the classifications above from source. Two items are flagged as
resolved-but-worth-owner-visibility rather than unresolved:

- `specials.union1.prog_number` / `specials.union2.prog_point` — confirmed OWNING
  (special-mob allocations, `db.cpp:1556-1557`) and confirmed **not freed** by
  `free_char` (a real latent leak for special mobs). Out of this wave's conversion
  scope; flagged for T5 to clean up opportunistically. Not "unresolved" — the
  source is unambiguous.
- `specials.script_info` / `obj_flags.script_info` (`info_script*`) — classified
  NON-OWNING because no per-instance free exists anywhere (`db.cpp:3639,3665` only
  null them). If a future reader believes the script subsystem transfers ownership
  to the instance, that would need confirmation in the script-loading code
  (`protos.h`/script tables) — but nothing on the char/obj teardown path frees it,
  so for THIS audit's purpose (what `free_char`/`free_obj` may convert) it is
  correctly HOLD-RAW.
