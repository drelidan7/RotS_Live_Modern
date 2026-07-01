# Objects & Equipment

**Source files:** struct definitions `structs.h:380-495` (`obj_flag_data`, `obj_affected_type`,
`obj_data`, `extra_descr_data`); wear/equip-position enums `structs.h:159-176` (`ITEM_WEAR_*`
bitflags), `structs.h:670-692` (`WEAR_*` slot indices); item-type enum `structs.h:115-138`;
`APPLY_*` enum `structs.h:742-783`; equip/unequip `handler.cpp:1209-1451`
(`obj_to_char`/`obj_from_char`/`equip_char`/`unequip_char`); affect application
`handler.cpp:253-588` (`affect_modify`/`affect_total`/`apply_gear_affects`/`modify_affects`);
wear/wield/hold/remove commands `act_obj2.cpp:626-1206` (`perform_wear`, `find_eq_pos`,
`do_wear`, `do_wield`, `do_grab`, `perform_remove`, `do_remove`); put/get/container commands
`act_obj1.cpp:36-283` (`perform_put`, `do_put`, `get_check_money`, `is_corpse`,
`perform_get_from_container`); armor math `utility.cpp:540-566` (`armor_absorb`) and
`fight.cpp:2144-2218` (`armor_effect`); decay/timers `limits.cpp:595-810` (`point_update`
object loop); corpse creation `fight.cpp:685-780` (`make_physical_corpse`); general object
helpers `object_utils.cpp`/`object_utils.h`; locks `act_move.cpp:1260-1410` (`is_key`,
`has_key`, `check_break_key`, `do_lock`, `do_unlock`); stat-command value labels
`act_wiz.cpp:626-692` (authoritative per-type value[] dump used throughout this doc); zone
attribute-set `zone.cpp:640-700` (`A` command, sub-type 5 pokes `obj->obj_flags.value[n]`
directly at reset time).
**Status:** 🟡 partial — the runtime object model, wear/equip pipeline, `APPLY_*` table, and
every item type's `value[]` semantics are verified against source and cross-checked against
live `lib/world/obj/` data. A few corners (some `extra_flags` bits, the on-disk `poisoned`
struct fields) are flagged as dead rather than fully traced, per Open Questions.

> **Scope.** This document owns the **runtime object model**: the in-memory `obj_data` struct,
> how an object gets worn/wielded/held, what happens to a character's stats when it is
> equipped/removed, and what each `ITEM_*` type's five `value[]` slots mean. It does **not**
> re-derive:
> - the on-disk `.obj` file grammar (`#vnum`, tilde strings, `E`/`A` blocks) — see
>   [world-files.md](../data-formats/world-files.md);
> - the player rent-file binary serialization of carried/worn objects — see
>   [object-rent-files.md](../data-formats/object-rent-files.md);
> - `ITEM_WEAPON`'s value fields, the weapon-type roster, or weapon×spec/race interactions —
>   fully owned by [weapons.md](weapons.md), one-line summary + link only, below;
> - the full melee-to-hit/damage pipeline or how `armor_effect` fits into a swing — owned by
>   [combat-loop.md](combat-loop.md);
> - `APPLY_*` fields that are stat-system internals (spell power, resistances, etc.) — see
>   [magic-system.md](magic-system.md) and [cleric-mystic-system.md](cleric-mystic-system.md)
>   for how casters *grant* those via spells; this doc only covers how *gear* applies them.

## Purpose

Every physical thing in the game world — a sword, a torch, a corpse, a pile of gold, a lever on
a wall — is one `obj_data` instance. Objects are either **prototypes** (loaded once from
`lib/world/obj/*.obj`, kept in `obj_proto[]`/`obj_index[]`) or **live instances**
(`read_object()`-cloned copies that exist in a room, in an inventory, in a container, or worn on
a character). Live instances chain into exactly one of: a room's `contents` list, a character's
`carrying` list, a character's `equipment[MAX_WEAR]` array, or another object's `contains` list
— tracked by `in_room` / `carried_by` / `in_obj` respectively (`structs.h:474-492`).

An object's behavior is driven almost entirely by two things: its **type** (`type_flag`, one of
25 `ITEM_*` constants) and that type's interpretation of the five-element **`value[]`** array.
`value[]` is untyped storage — the same five `int` slots mean "OB/parry/bulk/weapon-type/unused"
for a weapon, "capacity/lock-state/key-vnum/is-corpse/unused" for a container, and so on. This
doc's item-type table is the Rosetta stone for that overloading. Layered on top of type/value is
a small, fixed set of **generic** object mechanics that apply to (almost) every type regardless
of `value[]`: wear-position bitflags, up to two `APPLY_*` stat modifiers, extra-flags (glow/hum/
evil/no-drop/...), weight, decay timer, and level/rarity/material metadata.

## Data structures

### `obj_flag_data` — the type-independent object record (`structs.h:380-424`)

```c
struct obj_flag_data {
    int value[5];             /* meaning depends on type_flag — see the catalog below */
    byte type_flag;           /* ITEM_* — what kind of object this is */
    int wear_flags;           /* ITEM_TAKE / ITEM_WEAR_* / ITEM_WIELD / ITEM_HOLD bitvector */
    int extra_flags;          /* ITEM_GLOW / ITEM_MAGIC / ITEM_ANTI_EVIL / ... bitvector */
    int weight;                /* physical weight; rolls up through nested containers, §5 */
    int cost;                  /* sale value in copper-equivalent gold pieces */
    sh_int cost_per_day;       /* rent cost */
    int timer;                 /* decay countdown, ticks at point_update cadence, §6 */
    long bitvector;            /* AFF_* bit(s) granted while worn (paired with affected[]) */
    ubyte level;               /* item level — RotS addition; feeds weapon/armor derived power */
    ubyte rarity;               /* RotS addition; cosmetic/rarity-display only in code reviewed */
    signed char material;      /* index into object_materials[]; 4 == metal (smite check, §1) */
    sh_int butcher_item;       /* corpse-only: vnum to butcher into, 0 none, -1 already butchered */
    int prog_number;           /* mudlle/special-proc hook number */
    int script_number;         /* RotS addition; identifies a trigger script */
    struct info_script *script_info;
    bool poisoned;              /* weapon on-hit poison flag — DEAD, see Open Questions */
    int poisondata[5];          /* duration/strength/multiplier for the above — also DEAD */
};
```
Accessor methods (`structs.h:382-403`) exist mostly for the weapon fields (`get_ob_coef()` etc.,
owned by [weapons.md](weapons.md)) and material checks: `is_cloth()`/`is_leather()`/
`is_chain()`/`is_metal()` test `material == 1/2/3/4` — a **plain sequential index**, not a
bitmask (contrast with the `MATERIAL_*` `(1 << n)` bitflags at `structs.h:143-155`, used only by
`shop.cpp:190`'s "what materials will this shop buy" filter via `1 << item->obj_flags.material`
— the two representations coexist and their bit-numbering does **not** line up name-for-name;
treat `material` as the authoritative 0–13 index into `object_materials[]`
(`consts.cpp:1726-1729`: 0 "Usual stuff", 1 cloth, 2 leather, 3 chain, **4 metal**, 5 wood, 6
stone, 7 crystal, 8 gold, 9 silver, 10 mithril, 11 fur, 12 glass, 13 plant).

### `obj_affected_type` — one stat modifier (`structs.h:445-448`)
```c
struct obj_affected_type {
    byte location; /* an APPLY_* constant */
    int  modifier;  /* signed delta */
};
```
`MAX_OBJ_AFFECT = 2` (`structs.h:360`) — **every object carries at most two `APPLY_*` mods**,
stored in `obj_data::affected[2]`. This cap is baked into the rent-file binary layout too (see
[object-rent-files.md](../data-formats/object-rent-files.md)).

### `obj_data` — the live instance (`structs.h:451-495`)
```c
struct obj_data {
    int item_number;   /* index into obj_index[]; -1 for one-off objects (e.g. money, §5) */
    int in_room;        /* NOWHERE (-1) unless sitting directly in a room */
    obj_flag_data obj_flags;
    obj_affected_type affected[MAX_OBJ_AFFECT];
    char *name, *description, *short_description, *action_description;
    extra_descr_data *ex_description;   /* linked list of look-at keyword/description pairs */
    char_data *carried_by;              /* NULL unless carried or worn by a character */
    int owner;
    obj_data *in_obj;                   /* the container this sits in, else NULL */
    obj_data *contains;                 /* head of this object's own contents list */
    obj_data *next_content;             /* sibling link within contains/carrying/room lists */
    obj_data *next;                     /* global object_list link */
    int touched;                        /* has a PC's `get`/`put` handled this? (loot logging) */
    int loaded_by;                      /* immortal idnum that OLC-loaded this, else 0 */
};
```
`extra_descr_data` (`structs.h:354-358`) is a simple `{keyword, description, next}` singly
linked list, looked up by `find_ex_description()` (`act_info.cpp:299`) — used for room, object,
*and* equipped-item look-at text (`act_info.cpp:1253-1290`).

### Wear-position enum — `WEAR_*` (equipment array indices, `structs.h:670-692`)

| # | Constant | Slot | # | Constant | Slot |
|--:|---|---|--:|---|---|
| 0 | `WEAR_LIGHT` | held light source | 11 | `WEAR_SHIELD` | shield |
| 1 | `WEAR_FINGER_R` | ring (right) | 12 | `WEAR_ABOUT` | worn about body (cloak) |
| 2 | `WEAR_FINGER_L` | ring (left) | 13 | `WEAR_WAISTE` | waist/belt anchor *(sic — misspelled in source)* |
| 3 | `WEAR_NECK_1` | neck (1st) | 14 | `WEAR_WRIST_R` | wrist (right) |
| 4 | `WEAR_NECK_2` | neck (2nd) | 15 | `WEAR_WRIST_L` | wrist (left) |
| 5 | `WEAR_BODY` | body armor | 16 | `WIELD` | wielded weapon |
| 6 | `WEAR_HEAD` | head | 17 | `HOLD` | held item (non-light) |
| 7 | `WEAR_LEGS` | legs | 18 | `WEAR_BACK` | back |
| 8 | `WEAR_FEET` | feet | 19 | `WEAR_BELT_1` | belt pouch 1 |
| 9 | `WEAR_HANDS` | hands | 20 | `WEAR_BELT_2` | belt pouch 2 |
| 10 | `WEAR_ARMS` | arms | 21 | `WEAR_BELT_3` | belt pouch 3 |

`MAX_WEAR = 22` (`structs.h:698`) — the `equipment[MAX_WEAR]` array on `char_data` has exactly
these 22 slots. `WIELD_TWOHANDED = 22` (`structs.h:692`) is **not** a 23rd array slot; it's a
display-only index into the `where[]`/`beornwhere[]` label arrays (`consts.cpp:1757-1777`,
consumed at `act_info.cpp:420`) used when printing gear for a character currently gripping
`WIELD` two-handed (`AFF_TWOHANDED` set) — the object still physically lives at index 16.

### Wear-flags bitvector — `ITEM_WEAR_*` (what an object *can* be worn as, `structs.h:159-176`)

| Bit value | Constant | Enables slot(s) |
|--:|---|---|
| 1 | `ITEM_TAKE` | pickupable at all (gates `get`/`drop`, not a wear position) |
| 2 | `ITEM_WEAR_FINGER` | `WEAR_FINGER_R`/`_L` |
| 4 | `ITEM_WEAR_NECK` | `WEAR_NECK_1`/`_2` |
| 8 | `ITEM_WEAR_BODY` | `WEAR_BODY` |
| 16 | `ITEM_WEAR_HEAD` | `WEAR_HEAD` |
| 32 | `ITEM_WEAR_LEGS` | `WEAR_LEGS` |
| 64 | `ITEM_WEAR_FEET` | `WEAR_FEET` |
| 128 | `ITEM_WEAR_HANDS` | `WEAR_HANDS` |
| 256 | `ITEM_WEAR_ARMS` | `WEAR_ARMS` |
| 512 | `ITEM_WEAR_SHIELD` | `WEAR_SHIELD` |
| 1024 | `ITEM_WEAR_ABOUT` | `WEAR_ABOUT` |
| 2048 | `ITEM_WEAR_WAISTE` | `WEAR_WAISTE` |
| 4096 | `ITEM_WEAR_WRIST` | `WEAR_WRIST_R`/`_L` |
| 8192 | `ITEM_WIELD` | `WIELD` |
| 16384 | `ITEM_HOLD` | `HOLD` *(see the enforcement gap noted in §3)* |
| 32768 | `ITEM_THROW` | (bit exists; **no runtime reader found** — see Open Questions) |
| 65536 | `ITEM_WEAR_BACK` | `WEAR_BACK` |
| 131072 | `ITEM_WEAR_BELT` | `WEAR_BELT_1..3` (first free of the three) |

`CAN_WEAR(obj, bit)` (`utils.h:584`) is a plain `IS_SET` test against `wear_flags`.

## The item-type catalog

`type_flag` (`structs.h:115-138`) selects one of 25 `ITEM_*` constants. The table below gives
each type's `value[0..4]` meaning as read at actual call sites — not just the naming convention
— with the authoritative label source being the immortal `stat` command's per-type switch
(`act_wiz.cpp:626-692`), cross-checked against every other reader. **Live-instance counts** are
from a full scan of `lib/world/obj/*.obj` (§7); types with **zero** live instances are flagged
the same way [weapons.md](weapons.md) flags zero bow/crossbow objects — the code path exists,
the content doesn't.

| # | Type | value[0] | value[1] | value[2] | value[3] | value[4] | Live count |
|--:|---|---|---|---|---|---|--:|
| 1 | `ITEM_LIGHT` | Color (cosmetic; no reader found beyond `stat` display) | Type (cosmetic; no reader found) | **Hours of fuel remaining**, countdown timer (`limits.cpp:781-798`); `0` = "can't be lit" (`do_light`, `act_obj2.cpp:1191`); **negative = infinite/never decays** | **Lit flag** (0/1); auto-set to 1 on wear/hold if fuel≠0 (`handler.cpp:1346-1352`) | unused | 27 |
| 2 | `ITEM_SCROLL` | spell 1 | spell 2 | spell 3 | spell 4 | unused | **0** |
| 3 | `ITEM_WAND` | spell | charges ("Stamina") | — | — | — | 1 |
| 4 | `ITEM_STAFF` | spell | charges | — | — | — | **0** |
| 5 | `ITEM_WEAPON` | OB coef | Parry coef | Bulk | weapon type | legacy/unused | 298 — **fully owned by [weapons.md](weapons.md)**, one-line summary only |
| 6 | `ITEM_FIREWEAPON` | shares the `stat` display with `ITEM_WEAPON` (`act_wiz.cpp:641-644`); **0 live instances**, no dedicated runtime switch found beyond that shared label | | | | | **0** |
| 7 | `ITEM_MISSILE` | To-Hit bonus | To-Dam bonus | (unlabeled) | Break % (chance the missile is destroyed on use) | — | 8 — only usable from a `quiver`-named `ITEM_CONTAINER` (`act_obj1.cpp:100,108,124`) |
| 8 | `ITEM_TREASURE` | **no value[] semantics found at any runtime call site** — pure sell-value/flavor object; `cost` field carries its worth | | | | | 99 |
| 9 | `ITEM_ARMOR` | **unused by the formula** except the sentinel `-1` (disables absorption entirely, `utility.cpp:544-545`); never displayed | Used **twice**, with opposite-feeling effects: inside `armor_absorb` (`utility.cpp:560`) it's a *penalty* subtracted from the raw absorb score (`absorb = points - value[1]*9`, before the diminishing-returns clamp) — so a **higher** value[1] actually *lowers* the computed absorb %; separately, inside `armor_effect` (`fight.cpp:2158`) it's reused as the *flat starting floor* (`damage_reduction = value[1]`) that `armor_absorb`'s percentage term is then added on top of. Not simply "a floor added inside `armor_absorb`." | Encumbrance contribution (feeds `armor_absorb`'s bulk term *and* `equip_char`'s `points.encumb`, `handler.cpp:1318-1319`) | **Dodge bonus**, added straight to `SET_DODGE` on equip (`handler.cpp:1331-1332`) | unused | 903 |
| 10 | `ITEM_POTION` | same 4-spell layout as `ITEM_SCROLL` (drunk instead of read) | | | | | **0** |
| 11 | `ITEM_WORN` | generic wearable — no type-specific `value[]` reader; only the shared `APPLY_*`/`affected[]` mechanism applies (§4) | | | | | **0** |
| 12 | `ITEM_OTHER` | no `value[]` semantics found | | | | | 91 |
| 13 | `ITEM_TRASH` | no `value[]` semantics found; shops refuse to buy it (`shop.cpp:379`) | | | | | 1272 |
| 14 | `ITEM_TRAP` | spell cast on trigger | trap "hitpoints" (uses before it's spent) | — | — | — | **0** |
| 15 | `ITEM_CONTAINER` | **max weight capacity** — compared as a **weight sum**, not an item count, despite the `stat`/html label "Max-contains" (`perform_put`, `act_obj1.cpp:39`) | `CONT_*` bitvector: `1` closeable, `2` pickproof, `4` closed, `8` locked (`structs.h:230-233`) | **Key vnum** required to lock/unlock; `< 0` = "no keyhole, can't be locked" (`do_lock`/`do_unlock`, `act_move.cpp:1313,1379`) — **overloaded on corpses**, see §6 | **Corpse identifier**: `1` = this container is a corpse (`is_corpse`, `act_obj1.cpp:185`) | unused (except corpses, §6) | 246 |
| 16 | `ITEM_NOTE` | tongue/language index | — | — | — | — | 58 |
| 17 | `ITEM_DRINKCON` | max liquid units | current liquid units | `LIQ_*` liquid type (`structs.h:211-226`) | poisoned flag (0/1) | — | 31 |
| 18 | `ITEM_KEY` | "Keytype" — conventionally **the key's own vnum** (matched by value against a container/door's key field); not enforced by the engine to equal the actual vnum | — | — | — | — | 300 |
| 19 | `ITEM_FOOD` | Hunger satisfied ("Makes full") | (unlabeled in `stat`; no dedicated reader found beyond generic display) | (unlabeled; ditto) | poisoned flag (0/1) | — | 169 |
| 20 | `ITEM_MONEY` | **amount, in copper** (`create_money`, `handler.cpp:2283`; `get_check_money`, `act_obj1.cpp:171-180`) — single base currency, see §5 | — | — | — | — | 9 (builder-placed; player-death gold uses the same type via `create_money`) |
| 21 | `ITEM_PEN` | none — pure tool-presence check (`act_comm.cpp:351,377`, for `write`) | | | | | **0** |
| 22 | `ITEM_BOAT` | none — pure presence check for `sail`-type movement (`act_move.cpp:209-214`) | | | | | 4 |
| 23 | `ITEM_FOUNTAIN` | max liquid units | current liquid units (auto-refills to value[0] every `point_update`, `limits.cpp:780`) | liquid type | poisoned flag | — | 20 |
| 24 | `ITEM_SHIELD` | **Dodge bonus** — added to `SET_DODGE` on equip (`handler.cpp:1344`) | **Parry bonus** — added to `SET_PARRY` on equip (`handler.cpp:1345`) | Encumbrance ("Skill Enc.") | Block coefficient (consumed by the Defender spec's block roll — see [specializations.md](specializations.md), not re-derived here) | — | 66 |
| 25 | `ITEM_LEVER` | target room vnum | exit direction to toggle in that room (`act_move.cpp:1883`ff) | — | — | — | 53 |

**Zero-instance types in the live world:** `SCROLL`, `STAFF`, `FIREWEAPON`, `POTION`, `WORN`,
`TRAP`, `PEN` — all have working code paths (readers exist) but no content currently uses them.
Treat their `value[]` semantics above as source-derived, not data-verified.

**RotS-specific vs. stock CircleMUD:** the base type roster (`LIGHT` through `LEVER` minus the
RotS-only slots) matches stock Diku/Circle closely; `LEVER` (room-mechanism objects) and the
level/rarity/material/script_number metadata quartet on every object are RotS additions (also
called out in [world-files.md](../data-formats/world-files.md)). `ITEM_FIREWEAPON` exists as a
type constant but has no distinct behavior from `ITEM_WEAPON` found in the reviewed source —
likely a placeholder for a firearms/gunpowder concept that was never built out.

### `extra_flags` — cosmetic/behavioral bits, live vs. dead (`structs.h:181-209`)

Most matter for alignment/race gating (§3) or rent/PK rules; a few are defined but **never read
anywhere in `src/*.cpp`** and should be treated as dead:

| Flag | Status | Where read |
|---|---|---|
| `ITEM_GLOW`, `ITEM_HUM`, `ITEM_EVIL`, `ITEM_INVISIBLE`, `ITEM_MAGIC` | live | `act_info.cpp` (look/examine display), `char_utils.cpp`/`utility.cpp` (detect-invis/detect-magic checks) |
| `ITEM_NODROP` | live | `act_obj1.cpp` (blocks `drop`) |
| `ITEM_BREAKABLE` / `ITEM_BROKEN` | live | **set** only by `check_break_key` (`act_move.cpp:1280-1287`) and only ever invoked from the key/lock path (`is_key`/`has_key`, keys-only in practice), but generically **read** for display by `act_info.cpp:253,371` (appends `" (broken)"` to *any* object's look/examine text, not gated to `ITEM_KEY`) as well as by `has_key` (`act_move.cpp:1267,1273`, a broken key no longer unlocks); comment notes these bits were repurposed from the old `ITEM_LOCK`/`ITEM_BLESS` |
| `ITEM_ANTI_GOOD`/`ITEM_ANTI_EVIL`/`ITEM_ANTI_NEUTRAL` | live | `equip_char`, §3 |
| `ITEM_NORENT` | live | `objsave.cpp:1049` (rent-save exclusion) |
| `ITEM_WILLPOWER` | live | set/cleared by the `attune` mystic power and on drop (`handler.cpp:1267-1268`); see [cleric-mystic-system.md](cleric-mystic-system.md) |
| `ITEM_HUMAN`…`ITEM_OLOGHAI` (race flags) | live | `equip_char`, §3 |
| `ITEM_STAY_ZONE` | live | `act_move.cpp:468-501` — blocks carrying the item out of its zone |
| `ITEM_DARK`, `ITEM_IMM`, `ITEM_NOINVIS`, `ITEM_MOBORC` | **dead** — defined, never read by any `.cpp` at runtime | — |
| `ITEM_THROW` (wear-flag, not extra-flag) | **dead** — no runtime reader found | — |

## Wear/wield/hold/remove pipeline

### Slot resolution — `find_eq_pos` (`act_obj2.cpp:823-890`)
`wear <item>` with no location argument walks `CAN_WEAR` bit-by-bit in a fixed priority order
(finger → neck → body → head → legs → feet → hands → arms → shield → about → waist → wrist →
back → belt) and **the last matching bit wins** (`where` is overwritten, not short-circuited) —
so an object flagged for multiple slots resolves to whichever check runs *last* in that list,
not the first. With an explicit location argument (`wear <item> <location>`), a keyword table
(`"finger"`, `"neck"`, `"body"`, ... — `act_obj2.cpp:827-851`) maps text to a `WEAR_*` index via
`search_block`. `wield`/`hold` bypass `find_eq_pos` entirely — `do_wield` (`:964`) always targets
`WIELD`, `do_grab` (`:1008`) always targets `HOLD` (or `WEAR_LIGHT` if the item is an unworn
light and that slot is free, `:1025-1026`).

### `perform_wear` (`act_obj2.cpp:695-821`) — validity checks, in order
1. **Beorning race restriction** (`beorning_item_restriction`, `:642-692`): bears can't wear
   shields, body/leg/arm/foot gear, hand gear (their claws), hold *anything at all* (`CAN_WEAR(item,
   ITEM_HOLD)` is checked and blocked outright, `:664-667`), or wield weapons; wearing armor
   (`armor_absorb(item) > 0`) on body/head/legs/arms is also blocked outright. Head gear that
   isn't armor (e.g. a non-absorbing hat/circlet) is **not** blocked — the direct `CAN_WEAR`
   checks never test `ITEM_WEAR_HEAD`, only the armor-absorb check reaches `WEAR_HEAD`.
2. **Olog-hai race restriction** (`ologhai_item_restriction`, `:626-640`): can't wield weapons
   at or below `bulk 3` **and** at/under `LIGHT_WEAPON_WEIGHT_CUTOFF` weight — "too small for
   massive hands."
3. **Shield-vs-two-handed lock**: if already gripping `WIELD` two-handed (`IS_TWOHANDED`) **and**
   the `WIELD` slot is actually occupied, a shield cannot be equipped (`:768-771`) — this is the
   only place the engine enforces "two-handed excludes off-hand"; see
   [weapons.md §3](weapons.md#3-handedness--one-handed-two-handed-ranged) for how the grip itself
   is set (`do_twohand`, `fight.cpp:2800`).
4. **Wear-flag match**: `CAN_WEAR(item, item_slots[item_slot])` — note `item_slots[HOLD] ==
   ITEM_TAKE`, **not** `ITEM_HOLD` (`act_obj2.cpp:702`); see the callout below.
5. **Belt-anchor requirement**: `WEAR_BELT_1` requires `WEAR_WAISTE` already occupied.
6. `ON_WEAR` script trigger (`call_trigger`) may veto.
7. **Slot overflow for paired slots**: finger/neck/wrist/belt bump to the 2nd (or 3rd) instance
   of the slot if the first is full.
8. **If the resolved slot is occupied** (`:801-816`): with `wearall` set (the `wear all` command
   path), print a slot-specific "you're already wearing/wielding/holding..." message and stop —
   no auto-remove happens under `wear all`. Without `wearall`, either error ("Your hands are
   already full!", if `IS_CARRYING_N(character) >= CAN_CARRY_N(character)`) or
   auto-`perform_remove` the old item first.
9. `obj_from_char` → `wear_message` (race-specific text, `beorn_wear_messages` vs.
   `wear_messages`, `act_obj2.cpp:476-624`) → `equip_char`.

**The `HOLD`-slot enforcement gap.** `item_slots[HOLD] = ITEM_TAKE` means `perform_wear`'s
slot-validity gate for `hold`/`grab` only checks "is this pickupable at all," **not**
`ITEM_HOLD`. Any takeable object can occupy `HOLD` via `do_grab`. `equip_char`/`unequip_char`
compensate partially — see the next section.

### Restrictions enforced only at `equip_char` time (not at `perform_wear` time)
- **Alignment** (`handler.cpp:1291-1300`): `ITEM_ANTI_EVIL`+evil wearer, `ITEM_ANTI_GOOD`+good
  wearer, or `ITEM_ANTI_NEUTRAL`+neutral wearer all **zap and instantly drop** the item back into
  inventory (not the floor) with a message; `IS_GOOD`/`IS_EVIL`/`IS_NEUTRAL` are alignment-score
  thresholds (`utils.h:633-635`: good ≥ 100, evil ≤ −100, neutral is neither).
- **Race** (`handler.cpp:1302-1311`): the ten `ITEM_HUMAN`/`ITEM_DWARF`/`ITEM_WOODELF`/
  `ITEM_HOBBIT`/`ITEM_BEORNING`/`ITEM_URUK`/`ITEM_ORC`/`ITEM_MAGUS`/`ITEM_OLOGHAI`/
  `ITEM_HARADRIM` flags gate equip the same "zap and drop" way if the wearer's race
  (`GET_RACE`, [races.md](races.md)) doesn't match. **`ITEM_MOBORC` is defined but not checked
  here** — dead for this purpose (see extra_flags table above).

Both checks fire **after** `character->equipment[item_slot] = item` is already assigned in
`do_wield`'s and `perform_wear`'s call path — i.e. by the time the reject fires, the slot
assignment/weight bookkeeping hasn't happened yet in these particular branches (the checks are
the first two things `equip_char` does, before slot bookkeeping) — implementers should preserve
this ordering (reject before applying weight/stat effects) rather than the `HOLD` case's
apply-then-partially-unwind pattern.

## Equip/unequip effects — `equip_char` / `unequip_char` (`handler.cpp:1273-1451`)

### `equip_char(character, item, item_slot)` — step order
1. Assert slot is empty and in range; reject if the item is somehow still `in_room`.
2. **Alignment gate**, then **race gate** (above) — early-return (zap+drop) on failure.
3. Assign `equipment[item_slot] = item`; clear the object's decay timer to `-1` (equipped items
   don't decay, `:1315`).
4. **Encumbrance/weight bookkeeping** (`:1317-1326`): `points.encumb` and
   `specials2.leg_encumb` accumulate `value[2] * encumb_table[slot]` /
   `leg_encumb_table[slot]` (per-slot multiplier tables, `consts.cpp:1779-1810` — e.g. body=1,
   head=1, hands=2, arms=2, shield=1, about=1, wielded weapon=1, everything else 0 for the arm
   table; legs/feet/body dominate the leg table). `GET_ENCUMB_WEIGHT` adds the item's weight
   **times that slot's encumb multiplier** if the multiplier is nonzero (so hands/arms, whose
   multiplier is 2, count the item at *double* its weight — not "full weight" — while
   body/head/shield/about/wielded-weapon, multiplier 1, do add exactly the full weight), else
   (multiplier 0) adds **half** the weight. `GET_WORN_WEIGHT` and `IS_CARRYING_W` both take the
   full weight unconditionally regardless of slot.
5. **`HOLD`-slot bail-out** (`:1328-1329`): if `item_slot == HOLD` and the item lacks
   `ITEM_HOLD`, **return immediately** — weight/encumbrance from step 4 already applied, but the
   type-specific effects (step 6) and the `APPLY_*` loop (step 7) are **skipped**. A non-`ITEM_HOLD`
   object placed in `HOLD` therefore occupies the slot and adds weight, but grants none of its
   stat bonuses.
6. **Type-specific intrinsic effects** (`:1331-1352`) — only four types do anything here:
   - `ITEM_ARMOR`: `SET_DODGE += value[3]`.
   - `ITEM_WEAPON`: `SET_OB += value[0]`, `SET_PARRY += value[1]`; a weight/STR mismatch prints a
     "too heavy" warning (cosmetic, doesn't block the wield) — see
     [weapons.md](weapons.md) for the coefficient trade-off this represents.
   - `ITEM_SHIELD`: `SET_DODGE += value[0]`, `SET_PARRY += value[1]`.
   - `ITEM_LIGHT`: if `value[2] != 0` (has fuel) and the char is in a room, force-ignite
     (`value[3] = 1` if unset) and increment `world[room].light`. **Wearing/holding a fueled
     light auto-lights it** — no separate `light` command needed.
   - Every other type (armor's dodge/weapon's OB-PB/shield/light aside) has **no** intrinsic
     effect here; its only impact on the wearer comes from step 7.
7. **Generic `APPLY_*` loop** (`:1354-1357`): for each of the object's up to
   `MAX_OBJ_AFFECT` (2) `affected[]` entries, call `affect_modify(character, location,
   modifier, item->obj_flags.bitvector, AFFECT_MODIFY_SET, 0)`, then `affect_total(character)`
   to re-derive dependent stats.
8. **Poison special-case** (`:1361-1374`): if wearing this item newly makes the character
   `AFF_POISON`'d (i.e. its `bitvector` includes that flag and it wasn't already set), apply 5
   points of poison damage immediately, possibly killing the character on the spot.

### `unequip_char(ch, pos)` — mirror image
Same steps in reverse: subtract encumbrance/weight, re-check the `HOLD`/`ITEM_HOLD` bail-out
(`:1407-1408`, same asymmetry as equip), subtract the armor/weapon/shield intrinsic bonuses,
turn off a lit light and decrement room light if applicable, run the `APPLY_*` loop with
`AFFECT_MODIFY_REMOVE` (which negates `mod` — `handler.cpp:268`), `affect_total`, then the
mirrored poison special-case (removing a poisoned item can also deal 5 poison damage, if that
was the character's only poison source). Clears `equipment[pos] = 0` **before** the weight
subtraction, so the object must be captured in a local first (as the source does).

### The `APPLY_*` table — what gear (and spells) can modify (`affect_modify`, `handler.cpp:253-493`)

`affect_modify` is a single big `switch (loc)` shared by **both** gear (`apply_gear_affects`)
and spell/skill affects (`modify_affects`) — see §4 for why that unification matters.

| # | Constant | Effect (case body cites `handler.cpp` line) | Notes |
|--:|---|---|---|
| 0 | `APPLY_NONE` | no-op | |
| 1 | `APPLY_STR` | `STR_BASE` and `STR` both += mod | |
| 2 | `APPLY_DEX` | `DEX` (+base) += mod | |
| 3 | `APPLY_INT` | `INT` (+base) += mod | |
| 4 | `APPLY_WILL` | `WILL` (+base) += mod | |
| 5 | `APPLY_CON` | `CON` (+base) += mod | |
| 6 | `APPLY_LEA` | `LEA` (+base) += mod | leadership stat |
| 7 | `APPLY_PROF` | **no-op** — code commented out (`:312-314`) | dead |
| 8 | `APPLY_LEVEL` | **no-op** — code commented out (`:316-318`) | dead |
| 9 | `APPLY_AGE` | shifts `player.time.birth` | |
| 10 | `APPLY_CHAR_WEIGHT` | `GET_WEIGHT += mod` | |
| 11 | `APPLY_CHAR_HEIGHT` | `GET_HEIGHT += mod` | |
| 12 | `APPLY_MANA` | max mana += mod, current mana follows if it was at the cap | |
| 13 | `APPLY_HIT` | same pattern for max HP | |
| 14 | `APPLY_MOVE` | same pattern for max move | |
| 15 | `APPLY_GOLD` | **no-op** (`:356-357`) | dead |
| 16 | `APPLY_EXP` | **no-op** (`:359-360`) | dead |
| 17 | `APPLY_DODGE` | `SET_DODGE += mod` | |
| 18 | `APPLY_OB` | `SET_OB += mod` | |
| 19 | `APPLY_DAMROLL` | `GET_DAMAGE += mod` | |
| 20 | `APPLY_SAVING_SPELL` | `GET_SAVE += mod` | feeds [magic-system.md](magic-system.md) saves |
| 21 | `APPLY_WILLPOWER` | `GET_WILLPOWER += mod` | |
| 22 | `APPLY_REGEN` | **no-op** (`:399-400`) | dead |
| 23 | `APPLY_VISION` | sets/clears `AFF_INFRARED`/`AFF_BLIND` per sign of mod | on `add`; the `remove` branch inverts |
| 24 | `APPLY_SPEED` | `GET_ENE_REGEN += mod` | attack-speed pool |
| 25 | `APPLY_PERCEPTION` | adjusts `specials2.rawPerception`, then re-derives displayed perception (interacts with the Insight spell's floor, `:425-438`) | |
| 26 | `APPLY_ARMOR` | **no-op** — both statements inside are commented out (`:411-414`) | dead |
| 27 | `APPLY_SPELL` | grants/revokes a spell-like ability by invoking `skills[n].spell_pointer` directly (`SPELL_TYPE_SPELL`/`SPELL_TYPE_ANTI`) | `mod` is packed: low byte = spell number, `mod/256` = level |
| 28 | `APPLY_BITVECTOR` | sets/clears an arbitrary bit `1 << mod` in `specials.affected_by` | generic AFF_* toggle by number |
| 29 | `APPLY_MANA_REGEN` | `points.mana_regen += mod` | |
| 30 | `APPLY_RESIST` | sets/clears bit `mod` of `GET_RESISTANCES` | |
| 31 | `APPLY_VULN` | sets/clears bit `mod` of `GET_VULNERABILITIES` | |
| 32 | `APPLY_MAUL` | dodge penalty proportional to a stacking `counter`, capped at `MAX_MAUL_DODGE` | affect-only (uses the `counter` param gear affects always pass as 0) |
| 33 | `APPLY_BEND` | `GET_ENE_REGEN += GET_ENE_REGEN/2`, then `SET_OB += mod` | |
| 34-37 | `APPLY_PK_MAGE/MYSTIC/RANGER/WARRIOR` | **not handled in the switch at all** — falls to `default:` and logs `"Unknown apply adjust attempt"` | defined but unimplemented; see Open Questions |
| 38 | `APPLY_SPELL_PEN` | `points.spell_pen += mod` | [magic-system.md](magic-system.md) |
| 39 | `APPLY_SPELL_POW` | `points.spell_power += mod` | [magic-system.md](magic-system.md) |

`AFF_CHARM` toggling (via `bitv`) additionally resets `damage_details` regardless of `loc`
(`:259-266`) — a charm/uncharm always clears cached combat state.

## Object affects vs. spell affects — the shared mechanism

**They funnel through the same function**, `affect_modify`, but via two different drivers:

- **Gear**: `apply_gear_affects(character, modify_flag)` (`handler.cpp:520-544`) walks every
  equipped slot (skipping empty slots and, per the `HOLD` gap above, `HOLD` items lacking
  `ITEM_HOLD`) and calls `affect_modify` once per non-empty `affected[]` entry, always passing
  `counter = 0`. Gear affects have **no duration** — they are added when equipped and removed
  when unequipped; there is no timer to expire while worn. (One exception: an `affected[]`
  entry whose `location == APPLY_SPELL` is explicitly **skipped** by `apply_gear_affects`,
  `:525-526` — so an object cannot use the `APPLY_SPELL` ability-grant mechanism through the
  normal equip path; only spell/skill affects reach that case.)
- **Spells/skills**: `modify_affects(character, modify_flag)` (`handler.cpp:546-555`) walks the
  character's `affected` linked list (`affected_type`, each with its own `duration`/`counter`)
  and calls `affect_modify` per node, passing that node's real `counter`. These *do* expire —
  duration/tick handling is spell-affect machinery covered in
  [magic-system.md](magic-system.md)/[cleric-mystic-system.md](cleric-mystic-system.md), not
  duplicated here.
- **`affect_total(ch, mode)`** (`handler.cpp:557-588`) is the only place both are combined: a
  `REMOVE` pass (strip gear + spell affects, `recalc_abilities`, `affect_naked`) then a `SET`
  pass (reapply both) — full stat recompute from scratch, called after **any** equip/unequip or
  affect add/remove. `AFFECT_TOTAL_TIME` mode exists for per-tick affect processing
  (`AFFECT_MODIFY_TIME` short-circuits `affect_modify` immediately, `:274-276`, so this pass
  currently does nothing beyond whatever the affect-duration tick logic itself does elsewhere).

So: **object affects and spell affects are the same 40-case switch**, differing only in *how
long they last* (worn = indefinite vs. spell = timed) and in the small per-driver carve-outs
above (`APPLY_SPELL` gear skip; `counter` only meaningful for spell affects like `APPLY_MAUL`).

## Weight & capacity

### Carry limits — flat formulas, not a STR-indexed table (`utils.h:588-598`)
```
CAN_CARRY_W(ch) = 2000 + 1000 * GET_STR(ch)     /* weight units */
CAN_CARRY_N(ch) = 5 + GET_DEX(ch)/2 + GET_LEVEL(ch)/2   /* item count */
```
There is **no** `str_app`-style lookup table in this codebase (grep confirms none exists) —
unlike stock CircleMUD's carry-weight table, RotS computes both limits directly from the 1–100
ability scores (`affect_total` clamps `str`/`dex`/etc. to `[1,100]` or `[0,100]`,
`handler.cpp:578-587`) and `GET_LEVEL`. `GET_STR(ch)` itself is `tmpabilities.str`, or `-1` if
zero (`utils.h:352` — a defensive fallback, not a real ability value). `IS_CARRYING_W`/
`IS_CARRYING_N` are running totals stored on the character (`specials.carry_weight`/
`carry_items`), updated incrementally by every `obj_to_char`/`obj_from_char`/`equip_char`/
`unequip_char` call rather than recomputed from scratch each time.

`CAN_CARRY_OBJ`/`CAN_GET_OBJ` (`utils.h:604-608`) gate `get`/pickup against both limits plus
`ITEM_TAKE` and visibility.

### Container capacity & weight roll-up
- **Capacity is a weight budget, not a slot count**: `perform_put` rejects with "won't fit" if
  `GET_OBJ_WEIGHT(container) + GET_OBJ_WEIGHT(new item) > container.value[0]`
  (`act_obj1.cpp:39`) — despite the `stat`/html label "Max-contains," this is weight, not item
  count.
- **A container's own `weight` field is live, not static**: `obj_to_obj(item, container,
  change_weight=true)` (default `true`, `handler.h:73`) walks up the `in_obj` chain adding the
  new item's weight to every ancestor container, then — if the top-level ancestor is carried —
  adds it once to the carrier's `IS_CARRYING_W` (`handler.cpp:1717-1737`). **So yes, contained
  items count toward carry weight**, charged exactly once at the top level; nested containers'
  displayed weight is genuinely cumulative (a backpack's weight grows as you fill it, and a
  pouch inside it adds to the backpack's weight too).
- **Corpses hard-cap capacity at 0** (`fight.cpp:706`: "You can't store stuff in a corpse") —
  loot is placed into a corpse via direct `obj_to_obj` calls at death time, bypassing the
  `perform_put` weight check that would otherwise always fail.
- **Locks**: `CONT_CLOSEABLE`/`_PICKPROOF`/`_CLOSED`/`_LOCKED` are independent bits in `value[1]`
  (`structs.h:230-233`); `do_lock`/`do_unlock` require the container `CLOSED`, a non-negative
  `value[2]` (a keyhole exists), and a matching key vnum carried or held
  (`has_key`, `act_move.cpp:1264-1278` — checks `carrying` list and the `HOLD` slot only, **not**
  other equipment slots). A key flagged `ITEM_BREAKABLE` can shatter on successful unlock
  (`check_break_key`, `:1280-1287`).
- **Quivers**: any `ITEM_CONTAINER` whose `name` contains the keyword `"quiver"`
  (`obj_data::is_quiver()`, `object_utils.cpp:284-290`) restricts **`put` only** to `ITEM_MISSILE`
  objects (`act_obj1.cpp:100-141`) — the check there is an inline `isname("quiver",
  container->name)` re-implementing the same test, not a literal call to `is_quiver()`. There is
  **no matching restriction on retrieval**: `get`/`get_from_container` (`act_obj1.cpp:246-283`)
  never checks `is_quiver()`, so any object type that ends up inside a quiver (however it got
  there) can be `get`-ed back out freely. The `is_quiver()` method itself is called elsewhere for
  gameplay-significant purposes this doc doesn't otherwise cover: `ranger.cpp:2293,2723` requires
  a PC archer to be wearing a quiver in `WEAR_BACK` before firing a bow (auto-pulling an arrow
  from it), and `big_brother.cpp:156` exempts quivers from the normal "no looting containers"
  corpse-loot-protection rule so arrows can still be looted from a dead archer's quiver.

### Money — a single scalar currency (`structs.h:103-104`, `handler.cpp:2260-2291`)
`ITEM_MONEY.value[0]` is a **flat copper count**; `COPP_IN_SILV = 100`, `COPP_IN_GOLD = 1000`
are display-only denomination breakpoints used by `money_message()`/room-description text (a
pile can read "N copper coins," "~N silver coins," "N gold coins," or "a lot of gold" purely
based on `value[0]`'s magnitude — `handler.cpp:2261-2275`). `GET_GOLD(ch)` on a character is
likewise one integer (copper-denominated); there is no separate silver/gold ledger. Picking up a
money-pile object (`get_check_money`, `act_obj1.cpp:169-181`) adds `value[0]` to `GET_GOLD` and
destroys the object; it is never actually "worn" or "carried" as an item — `item_number = -1`
marks it a throwaway, non-prototype object (`handler.cpp:2285`).

## Object condition, decay, and corpses

**There is no armor durability/breaking mechanic.** `armor_effect` (`fight.cpp:2144-2218`,
already documented for its damage-reduction role in [weapons.md §5](weapons.md#5-damage-type--armor-recap))
only ever *reads* `armor->obj_flags.value[1]`/`.material` to compute how much damage to subtract
— it never writes back to the armor object, decrements a durability counter, or sets
`ITEM_BROKEN`. `ITEM_BREAKABLE`/`ITEM_BROKEN` are only ever *set* by the key/lock path (§5) — the
display code (`act_info.cpp:253,371`) will show `(broken)` on any object flagged that way, but
nothing outside `check_break_key` ever sets the bit, so armor/weapons never end up broken this
way. Confirmed by
grep: no `.cpp` file decrements an armor/weapon "durability" or "condition" field at combat
time — if a rewrite wants item durability, it is a net-new system, not a port.

### Decay timer — `obj_flags.timer`, ticks in `point_update` (`limits.cpp:595-810`)
`point_update()` runs once per **`SECS_PER_MUD_HOUR * 4` pulses** — with 4 pulses/sec
(`comm.cpp:45` `OPT_USEC = 250000`) and `SECS_PER_MUD_HOUR = 60` (`structs.h:95`), that's every
**240 pulses = 60 real seconds = exactly one mud hour** (`comm.cpp:825-827`) — the same cadence
documented for affect-duration ticks elsewhere in this repo's memory notes. Each call:
1. If `timer >= 0`, decrement it (no-op at exactly 0 already reached the floor).
2. At `timer == 0`: announce decay ("decays in your hands" / "decays into dust"), and if the
   object is an `ITEM_CONTAINER`:
   - if it's a corpse (`value[3] == 1`), notify Big Brother (`on_corpse_decayed`) for PK
     loot-protection bookkeeping;
   - **spill contents** up one level (into the parent container, the carrier's room, or the
     object's own room, in that priority) and give each spilled item a fresh `timer =
     LOOT_DECAY_TIME` (`config.cpp:32`, **5** ticks ≈ 5 minutes) so loose loot doesn't linger
     forever after its container rots away.
   - `extract_obj()` destroys the (now-empty) container.
3. Negative timers never decay (`< 0` skips the countdown entirely) — used for permanent
   objects and, per the `ITEM_LIGHT` row, "infinite fuel" light sources.
4. `ITEM_FOUNTAIN` refills to `value[0]` every tick (unconditionally — a zone-reset-style
   "fountains never run dry between explicit resets" behavior).
5. `ITEM_LIGHT`: if lit (`value[3] > 0`) and fuel remains (`value[2] > 0`) and the holder isn't
   an NPC, burn one hour of fuel (`value[2]--`). `IS_NPC(j->carried_by)` (`utils.h:208`) is
   null-safe (`(ch) && IS_SET(...)`), so an **uncarried** lit light (room-resident, no `carried_by`
   at all) also passes the "isn't an NPC" test and burns fuel down on the same schedule as a
   PC-held one — the NPC exemption only matters for lights an NPC is actually carrying/wielding.
   At `value[2] == 0` the light **goes out and is destroyed** (`extract_obj`) with a room message
   — torches don't become inert husks, they vanish. Below `value[2] < 3` (still lit) a "flickers
   weakly" warning fires each tick.

### Corpse timers (`config.cpp:30-32`, set in `corpse_decay_time`, `fight.cpp:571-586`)
- NPC corpse: **10 ticks** (≈10 min), or **+15** (25 ticks, ≈25 min) if the NPC was
  `MOB_ORC_FRIEND` (a tamed pet-like mob, presumably so an owner has time to loot/butcher).
- PC corpse: **45 ticks** (≈45 min).
- `make_physical_corpse` (`fight.cpp:685-780`) sets `value[0] = 0` (no capacity — see above),
  `value[3] = 1` (corpse marker), and **overloads `value[2]` and `value[4]`** with the killed
  creature's identity: the mob's vnum if it was an NPC, or **the negative player idnum** if it
  was a PC (`:719-727`). This repurposes the "key vnum" semantics of `value[2]` (§ catalog) —
  a corpse's `value[2] < 0` doubles as "this is a player corpse," which
  `perform_get_from_room` (`act_obj1.cpp:288`) reads to gate PK loot-protection (a player corpse
  killed by another player can't be moved/looted freely by third parties, per Big Brother
  rules — mob-killed corpses can be).
- `butcher_item` (a separate `obj_flag_data` field, not part of `value[]`) carries the vnum to
  butcher the corpse into, copied from the killed mob's own `specials.butcher_item`.

### Zone-reset value overrides
Builders can poke an object's `value[n]` directly at zone-reset time via the `A` reset command,
sub-type `5`: `obj->obj_flags.value[arg2] = arg3` on the most recently loaded object
(`zone.cpp:670-675`). This is how, e.g., a decorative `ITEM_LIGHT` scenery object (which starts
`value[3] = 0`/unlit because it's never worn — see the `hearth` worked example, §7) could be
pre-lit at every zone reset without a player ever touching it, though no live zone file was
found doing this for a light specifically.

## Object utility layer (`object_utils.cpp`/`.h`)

General-purpose, non-weapon-specific helpers a rewrite should reproduce as thin field
accessors (weapon-specific members like `get_ob_coef`/`get_weapon_type`/`get_weapon_damage` are
[weapons.md](weapons.md)'s territory):

| Function | Behavior | Note |
|---|---|---|
| `utils::is_artifact(const obj_data&)` | **always returns `false`** | dead stub — mirrors the `IS_ARTIFACT(obj)` macro (`utils.h:580`), which is hardcoded `0`. No artifact system exists despite the name. |
| `utils::get_item_type` | `obj.obj_flags.type_flag` | thin wrapper |
| `utils::can_wear(obj, part)` | `IS_SET(wear_flags, part)` | thin wrapper around `CAN_WEAR` |
| `utils::get_object_weight` | `obj.obj_flags.weight` | thin wrapper around `GET_OBJ_WEIGHT` |
| `utils::is_object_stat(obj, stat)` | `IS_SET(extra_flags, stat)` | thin wrapper around `IS_OBJ_STAT` |
| `utils::get_item_bulk` | `value[2]` | weapon-flavored name but generic; only weapons/shields use bulk meaningfully |
| `obj_data::is_quiver()` | name contains `"quiver"` **and** type is `ITEM_CONTAINER` | string-based, not a flag — a builder could name any container "quiver" |
| `obj_flag_data::is_wearable()` | `type_flag` is one of `{WAND, STAFF, WEAPON, FIREWEAPON, ARMOR, WORN, SHIELD}` | note: **not** the same test as "has any `ITEM_WEAR_*` bit set" — e.g. a `LIGHT` or `CONTAINER` with wear flags set is *not* considered "wearable" by this helper even though `perform_wear` would happily equip it |
| `obj_data::is_ranged_weapon()` | weapon type is `WT_BOW`/`WT_CROSSBOW` | owned by [weapons.md](weapons.md) |
| `game_types::get_weapon_name(weapon_type)` | string table lookup | owned by [weapons.md](weapons.md) |

`object_utils.cpp` also hosts the **dead** `utils::get_weapon_damage(const obj_data&)`
implementation (reached only by the unused `combat_manager.cpp`, per AGENTS.md) — not
re-documented here; see [weapons.md's cheat-sheet](weapons.md#6-builder-cheat-sheet--open-questions)
for the live-vs-dead damage-formula divergence.

## Worked examples (verified against live `lib/world/obj/`)

A full type-census of `lib/world/obj/*.obj` (**156 files** — re-counted directly via `ls
lib/world/obj/*.obj | wc -l`; a prior pass over this doc misstated this as "325 files," likely
stale from an earlier snapshot of the git-ignored, externally-sourced world-files checkout, see
AGENTS.md) found: 27 `LIGHT`, 0 `SCROLL`, 1 `WAND`,
0 `STAFF`, 298 `WEAPON`, 0 `FIREWEAPON`, 8 `MISSILE`, 99 `TREASURE`, 903 `ARMOR`, 0 `POTION`, 91
`OTHER`, 0 `WORN`, 1272 `TRASH`, 0 `TRAP`, 246 `CONTAINER`, 58 `NOTE`, 31 `DRINKCON`, 300 `KEY`,
169 `FOOD`, 9 `MONEY`, 0 `PEN`, 4 `BOAT`, 20 `FOUNTAIN`, 66 `SHIELD`, 53 `LEVER`.

### Armor — `#27000`, "werewolf skull helm" (`lib/world/obj/270.obj`)
```
9 0 17
31 1 1 -3 0
65 4000 0
20 0 11 0 0
A 18 3
```
`type=9(ARMOR) extra=0 wear=17(1+16 = ITEM_TAKE + ITEM_WEAR_HEAD)`. Values: `value[0]=31`
(unused by `armor_absorb` except as the `-1` sentinel — legacy data here, like the
weapon `value[4]` leftovers `weapons.md` flags), `value[1]=1` (min-absorb floor),
`value[2]=1` (encumbrance), `value[3]=-3` (**negative dodge** — this helm is a dodge *penalty*,
unusual but the field is a signed int and nothing clamps it). `weight=65, cost=4000,
cost_per_day=0`. `level=20, rarity=0, material=11(fur — matches the fur-trimmed werewolf-skull
flavor text), script_number=0`. One `A` line: `APPLY_WILLPOWER(18) +3`.

### Container (lockable) — `#26238` "webbed larder chest" + its key `#26239` (`262.obj`)
```
#26238                              #26239
15 0 0                              18 4104 1
4000 15 26239 0 0                   26239 0 0 0 0
10000 0 0                           10 0 0
0 0 0 0 0                           0 0 0 0 0
```
Chest: `type=15(CONTAINER)`, `wear=0` (fixed scenery, not takeable). `value[0]=4000` (weight
capacity), `value[1]=15` = `CONT_CLOSEABLE|CONT_PICKPROOF|CONT_CLOSED|CONT_LOCKED` (all four
bits — closed, locked, and pickproof simultaneously), `value[2]=26239` (**the exact vnum of the
matching key**), `value[3]=0` (not a corpse). Key `#26239` "spinneret sharp key":
`type=18(KEY)`, `extra=4104 = ITEM_NORENT(4096) + ITEM_BREAKABLE(8)` (fragile, can't be rented),
`value[0]=26239` — **its own vnum**, confirming the "Keytype = the key's own vnum" convention
and cross-verifying the chest's `value[2]` reference end-to-end from real data.

### Container (wearable pouch) — `#2059` "tiny treasure chest" (`20.obj`)
```
15 0 131073
1000 0 0 0 0
40 125 -1
0 0 5 0 0
```
`wear=131073 = ITEM_WEAR_BELT(131072) + ITEM_TAKE(1)` — a container meant to be worn on a belt
slot (`WEAR_BELT_1..3`), demonstrating that `ITEM_CONTAINER` and "wearable" are not mutually
exclusive despite `obj_flag_data::is_wearable()` not listing `CONTAINER`. `value[1]=0` (not
closeable at all — always open), `value[2]=0` (no lock, consistent with "not closeable").
`material=5` (wood).

### Light (decorative, mechanically inert) — `#26225` "hearth fireplace" (`262.obj`)
```
1 0 0
0 0 -2 0 0
100000 0 0
0 0 6 0 0
```
`type=1(LIGHT), wear=0` (not takeable — this is a room fixture, per its description "an ancient
hearth smolders here"). `value[2]=-2` (negative → would never decay *if* lit), but `value[3]=0`
on load and **nothing in the reviewed source lights a room-resident (non-worn) `ITEM_LIGHT`
automatically** — `equip_char`'s auto-ignite path only fires when the object is equipped/held,
and `recount_light_room`'s room-contents loop (`handler.cpp:187-189`) only counts objects whose
`value[3] != 0` already. So despite the "hazy light" flavor text, this object contributes **zero**
mechanical room light unless a builder explicitly pre-lit it via the zone `A 5` reset command
(§6) — no such reset line was found for this vnum in the corpus. Flagged as a
description/mechanics mismatch, not asserted as definitely a bug (a zone file elsewhere could
set it).

### Food (poisonable) — `#1504` "strip of crocodile flesh" (`15.obj`)
```
19 4096 1
5 176 14 0 0
30 0 0
0 0 0 0 0
```
`type=19(FOOD), extra=4096(ITEM_NORENT — raw meat can't be rent-saved), wear=1(ITEM_TAKE)`.
`value[0]=5` (satisfies 5 hours of hunger), `value[1]=176` and `value[2]=14` have **no reader
found** for `ITEM_FOOD` beyond the generic display path — likely vestigial/unused per-item data,
`value[3]=0` (not poisoned).

### Money — `#1125` "gold coins" (`11.obj`)
```
20 0 1
1000000 0 0 0 0
1 0 -1
0 102 0 0 0
```
`type=20(MONEY), wear=1(TAKE)`. `value[0]=1,000,000` — one million **copper**, i.e. 1,000 gold
at `COPP_IN_GOLD=1000`, matching a builder-authored "big treasure pile." `material=102` is
**out of range** for `object_materials[]` (only 0–13 defined) — the `stat`/html display code
guards this exact case (`(material >= 0 && material < num_of_object_materials) ? ... :
"Unknown"`, `act_wiz.cpp:595`), so this renders as "Unknown" material rather than crashing; a
rewrite must replicate that bounds check.

### Key — `#11301` "inlaid wooden key" (`113.obj`)
```
18 4104 1
11301 0 0 0 0
12 0 -1
0 0 5 0 0
```
`extra=4104 = ITEM_NORENT + ITEM_BREAKABLE`. `value[0]=11301` — again, **the key's own vnum**,
confirming that convention a second time independent of the chest example above.

## RotS-specific notes (recap)

- `LEVER`, and the level/rarity/material/script_number/`loaded_by` metadata, plus followers'
  richer rent-save format, are RotS additions over stock Circle/Diku — see
  [world-files.md](../data-formats/world-files.md) and
  [object-rent-files.md](../data-formats/object-rent-files.md) for the on-disk side.
- Alignment/race equip gates (`ITEM_ANTI_*`, the ten race flags) and the Beorning/Olog-hai
  hard-coded species restrictions in `perform_wear` are RotS's racial-diversity system layered
  on top of the generic Diku wear pipeline; see [races.md](races.md).
- The unified `APPLY_*`/`affect_modify` mechanism for both gear and spells is itself a
  fairly standard Diku pattern (stock CircleMUD's `APPLY_*` list is smaller); RotS's additions
  are the caster/PK-specific entries (`SPELL_PEN`/`SPELL_POW`/`SAVING_SPELL`/`WILLPOWER`/the
  unused `PK_MAGE`..`PK_WARRIOR` quartet).
- The container weight-rollup (`obj_to_obj`'s ancestor-chain weight propagation) and the
  copper-only single-currency money model are consistent with stock Diku-family design, not
  RotS-specific.

## Open questions

- **`APPLY_PK_MAGE`/`PK_MYSTIC`/`PK_RANGER`/`PK_WARRIOR` (34–37) are defined but not implemented**
  in `affect_modify`'s switch — they fall to `default:` and log an "Unknown apply adjust
  attempt" error if anything ever tries to grant them. No live object or spell was found
  granting these in the files reviewed; unclear whether they're planned-but-unbuilt or fully
  vestigial. A rewrite should either implement them or drop the constants.
- **The on-disk `poisoned`/`poisondata[5]` fields are read by `does_victim_save_on_weapon_poison`
  (`fight.cpp:1938-1945`) but that function itself is never called from anywhere**, and the only
  code that would ever *set* `.poisoned` on an object is inside a commented-out block in
  `db.cpp` (see [world-files.md](../data-formats/world-files.md)). Net effect: the
  weapon-carries-its-own-poison mechanic is fully dead in the current build — food/drink poison
  (`value[3]` on `FOOD`/`DRINKCON`/`FOUNTAIN`) is the *only* live poison-via-object path. Worth
  confirming with the team whether this was intentionally retired in favor of a
  skill/spell-applied `AFF_POISON` (which is very much alive) before deciding whether to port it.
- **`ITEM_THROW`, `ITEM_MOBORC`, `ITEM_DARK`, `ITEM_IMM`, `ITEM_NOINVIS`**: defined, zero runtime
  readers found via grep. Listed as dead in the tables above; flagging again here in case a
  reader exists in a file outside the `src/*.cpp` search (e.g. a `.h`-only inline, or dynamically
  via a spec-proc table this pass didn't trace).
- **`ITEM_FIREWEAPON` / `ITEM_TREASURE` / `ITEM_OTHER` / `ITEM_TRASH` / `ITEM_WORN`**: no
  type-specific `value[]` behavior was found for any of these beyond the shared generic
  (`APPLY_*`, wear-flags, decay-timer) mechanics. Given zero (`FIREWEAPON`, `WORN`) or nonzero
  (`TREASURE` 99, `OTHER` 91, `TRASH` 1272) live counts, it's plausible these are intentionally
  "dumb" catch-all types (sellables, quest props, junk) rather than under-documented systems —
  but this wasn't exhaustively verified against every spec_proc/mudlle script that might key off
  them by type.
- **`ITEM_FOOD`'s `value[1]`/`value[2]`**: no reader found in the reviewed `.cpp` files. Could be
  vestigial spice/flavor-text data, or read only from mudlle scripts (`.mdl`/`.scr` files), which
  this pass did not search — flagged rather than guessed.
- **The `hearth`-style "decorative but mechanically dark" light objects** (§7): confirmed no
  automatic ignition path for room-resident (non-worn) `ITEM_LIGHT` objects exists in `src/*.cpp`,
  but zone `A 5` reset lines *could* pre-light one and none were found doing so for the sampled
  vnum — this is a "not observed" rather than "proven absent across all 341 zone files" finding.
- **Rarity (`obj_flags.rarity`)**: field exists, is loaded (`world-files.md`), and is displayed,
  but no gameplay-mechanical reader (e.g. drop-rate weighting, a rarity-based stat multiplier)
  was found in this pass — may be purely cosmetic/display, or may be read by a system outside
  this doc's search scope (loot tables, mudlle). Flagged rather than asserted either way.
