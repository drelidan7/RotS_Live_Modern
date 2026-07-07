# Object / rent files (`.obj` — player inventory & equipment)

> **LEGACY — retired 2026-07; live format is JSON** (see `src/objects_json.cpp` /
> `boards.cpp` / `mail.cpp`). This document describes the frozen 32-bit layout that the
> migration decoders read; it is no longer written by the live game. See CLAUDE.md's
> persistence gotcha for the current JSON-first save/load path and the one-time
> legacy-to-JSON converters.

**Source files:** `src/objsave.cpp` (`Crash_get_filename:73`, `Crash_write_rentcode:348`,
`Crash_obj2store:635`, `Crash_save:999`, `Crash_rentsave:1177`, `Crash_follower_save:680`,
`Crash_alias_save:952`, loaders `load_character:548`/`Crash_*_load`); structs
`obj_file_elem` (`structs.h:1842`), `rent_info` (`structs.h:1866`), `follower_file_elem`
(`structs.h:1826`)
**Status:** ✅ write format + layout complete and **verified against real `.obj` files**.
Struct sizes/offsets below include the 32-bit compiler padding observed on disk. Load-side
section delimiting, the alias on-disk format, and `nitems` are resolved (see Notes).

## Purpose
A player's carried items, worn equipment, mounts/followers, and command aliases are stored
together in a per-player **binary** file (the DikuMUD "Crash/rent" system). Unlike the
player character file (which is now text — see `player-save.md`), these remain raw
`fwrite` of fixed structs, so the layout is the compiler's struct layout for the **32-bit**
build (4-byte `int`/`long`/`time_t`, default alignment). This is why the build must stay
32-bit (or a converter is needed): the on-disk contract is the binary struct image.

## File location & naming (`Crash_get_filename:73`)
```
plrobjs/<bucket>/<name>.obj
```
Bucketed by first letter exactly like player files: `A-E F-J K-O P-T U-Z`, else `ZZZ`.
One file per character. Opened `"w+b"` for save, `"rb"`/`"r+b"` for load/clean.

## On-disk structures

### `rent_info` — file header (`structs.h:1866`) — **48 bytes on disk**
```c
int    time;              // off 0  : unix time of rent/crash
int    rentcode;          // off 4  : RENT_* (see below)
int    net_cost_per_hour; // off 8  : rent cost
int    gold;              // off 12 : gold on hand at save
int    nitems;            // off 16 : item count — NOT set by any saver; garbage on disk (see Notes)
sh_int spare0, spare1, spare2; // off 20-25 : 3 shorts
// off 26-27 : 2 bytes compiler padding to 4-byte-align spare3
int    spare3, spare4, spare5, spare6, spare7; // off 28-47 : 5 ints
```
`sizeof(rent_info) == 48`. Every header-only file (no items/aliases) is exactly 48 bytes
(e.g. `aranduril.obj`, `terrin.obj`, `arrond.obj`), confirming the size and the 2-byte pad.
`rentcode` values (`structs.h:1857-1863`): `RENT_UNDEF 0, RENT_CRASH 1, RENT_RENTED 2,
RENT_CAMP 3, RENT_FORCED 4, RENT_TIMEDOUT 5, RENT_QUIT 6`.

### `obj_file_elem` — one per object (`structs.h:1842`) — **56 bytes on disk**
```c
sh_int item_number_deprecated; // legacy id; set to DEPRECATED_ID_VALUE(-255) by new saves
sh_int value[5];
int    extra_flags;
int    weight;
int    timer;
long   bitvector;              // 4 bytes in the 32-bit build
struct obj_affected_type affected[MAX_OBJ_AFFECT];   // MAX_OBJ_AFFECT=2 (see padding below)
sh_int wear_pos;              // placement, see nesting below
int    loaded_by;            // idnum of immortal loader, 0 if zone-loaded
int    item_number;          // object VNUM (was spare2; widened to int)
```
**`obj_affected_type` is 8 bytes, not 5.** It is `{byte location; int modifier;}`
(`structs.h:445`); the compiler inserts **3 bytes of padding** after the 1-byte `location`
to 4-byte-align `modifier`. So each entry is 8 bytes and `affected[2]` is **16 bytes**.
A second pad of **2 bytes** sits after `wear_pos` (2 bytes) to align the following `int`.
Net result: `sizeof(obj_file_elem) == 56`. The naive field-byte count (48) is **wrong** — a
binary reader/converter using 48 will be 8 bytes off on every record.

**On-disk byte offsets (32-bit x86, default alignment):**

| Offset | Size | Field |
|-------:|-----:|-------|
| 0  | 2  | `item_number_deprecated` |
| 2  | 10 | `value[5]` |
| 12 | 4  | `extra_flags` |
| 16 | 4  | `weight` |
| 20 | 4  | `timer` |
| 24 | 4  | `bitvector` |
| 28 | 1  | `affected[0].location` |
| 29 | 3  | *padding* |
| 32 | 4  | `affected[0].modifier` |
| 36 | 1  | `affected[1].location` |
| 37 | 3  | *padding* |
| 40 | 4  | `affected[1].modifier` |
| 44 | 2  | `wear_pos` |
| 46 | 2  | *padding* |
| 48 | 4  | `loaded_by` |
| 52 | 4  | `item_number` |
| **56** | | **total** |

The padding bytes (offsets 29-31, 37-39, 46-47) are uninitialized stack memory in real
files — visible as differing garbage from file to file (e.g. the 2-byte pad at offset 46-47
reads `ff bf` in `airez.obj` and `32 28` in `olvar.obj`). The loader never reads them.
`loaded_by` is likewise garbage in old-format records (saves predating the field never
zeroed the struct); the loader ignores it for old records.

Notes (`Crash_obj2store:635`):
- `item_number` holds the object **vnum** (`obj_index[...].virt`), or a negative special.
- New saves stamp `item_number_deprecated = DEPRECATED_ID_VALUE` so loaders can tell new
  from old records.
- Special case: for `generic_scalp` objects, the player-id is stashed in `extra_flags`
  (because ids exceed `sh_int`); scalp loading reverses this and zeroes `extra_flags`
  (`:663-668`).

### `follower_file_elem` — one per follower/mount (`structs.h:1826`) — **28 bytes on disk**
```c
int fol_vnum;      // follower mob vnum; -17 sentinel terminates the follower list
int mount_vnum;
int wimpy;
int exp;
int flag_config;   // FOL_ORC_FRIEND / FOL_TAMED / FOL_GUARDIAN / FOL_MOUNT
int spare1, spare2;
```
Seven `int`s, no padding: `sizeof(follower_file_elem) == 28`. The terminator record only
sets `fol_vnum = -17`; its other 24 bytes are stack garbage on disk (the loader checks only
`fol_vnum`).

## File layout (`Crash_rentsave:1177`)
In write order:
1. **`rent_info`** header (`Crash_write_rentcode:350`).
2. **Carried items**, then **each worn item** (`Crash_save` with `pos = MAX_WEAR` for
   carried, `pos = slot` for equipment). `Crash_save` is recursive and writes, per object:
   the object record, then **its container contents** (`obj->contains`) at nesting
   `pos+1`, then the next sibling (`obj->next_content`) — a depth-first flatten
   (`Crash_save:1007-1016`). Container membership is reconstructed from the `wear_pos`
   depth values: carried/contained items use `wear_pos >= MAX_WEAR`, with `+1` per nesting
   level; worn items use `wear_pos = slot (< MAX_WEAR)`.
3. **Aliases** (`Crash_alias_save:952`), in this exact order:
   - a sentinel `obj_file_elem` (56 bytes) with `item_number = -17` — this **doubles as the
     item-list terminator** (see "Section delimiting" under Notes); the loader stops reading
     items when it hits it (`Crash_load:493`).
   - the `board_point` array: `MAX_MAXBOARD` × `sizeof(sh_int)` = **22 × 2 = 44 bytes**
     (`:963`). Old files written when `MAX_MAXBOARD == 20` have only **40 bytes** here; the
     loader tolerates the mismatch because the alias terminator (below) still stops the list.
   - zero or more aliases, each: a **20-byte** null-padded `keyword`, then `int` (4-byte)
     command length, then `length` bytes of command text (**not** null-terminated on disk)
     (`:968-985`).
   - a **20-byte** all-zero terminator (`:989-995`); the loader stops when it reads a keyword
     whose first byte is `\0` (`Crash_alias_load:923`).
4. **Followers** (`Crash_follower_save:680`) — **only in the rent and crash-save paths**
   (`Crash_rentsave`, `Crash_crashsave`); the idle/timeout path `Crash_idlesave`
   (`RENT_TIMEDOUT`) writes aliases but **does not** call `Crash_follower_save`, so its files
   have no follower section. When present: for each followed NPC in the room, a
   `follower_file_elem`, then that follower's equipment as `obj_file_elem` records, then a
   `dummy_object` (`item_number = SENTINEL_ITEM_ID_VALUE(-17)`) marking end of that
   follower's eq. The mount (if ridden) is written similarly. The list ends with a
   `follower_file_elem` whose `fol_vnum = -17`.

### File-size arithmetic (verified on real files)
With header = 48, object record = 56, board = 44, alias terminator = 20, follower
terminator = 28:
- `airez.obj` = **104** = 48 + 56 (one item; old save, no alias section).
- `vatest.obj` = **168** = 48 + 56 (sentinel) + 44 (board) + 20 (alias term) — `RENT_TIMEDOUT`,
  no items, no follower section.
- `olvar.obj` = **252** = 48 + 56 (one item) + 56 (sentinel) + 44 (board) + 20 (alias term)
  + 28 (follower term) — `RENT_RENTED`.
- `kain.obj` = **3509**: the alias sentinel sits exactly at file offset 2680 = 48 + 47 × 56,
  confirming the 56-byte record stride across 47 item records.

## Sentinels
- `SENTINEL_ITEM_ID_VALUE = -17`, `DEPRECATED_ID_VALUE = -255` (`structs.h:1840-1841`).
- Follower list terminator: `follower_file_elem.fol_vnum == -17` (`:749`).
- Per-follower eq terminator: `obj_file_elem.item_number == -17` (`:688`).

## RotS-specific notes
- Followers/mounts persistence (with per-follower eq blocks) is richer than stock Diku rent.
- `loaded_by` (immortal id) and the widened `item_number` are RotS additions.

## Notes (resolved questions)
- **Section delimiting on load — by sentinel, not by `nitems`.** The loader reads object
  records until it hits the `obj_file_elem` whose `item_number == -17`
  (`Crash_load:493`); that sentinel is the first record the alias writer emits, so it both
  ends the item list and opens the alias section. There is no separate item-count gate and
  no reliance on `nitems`.
- **`nitems` is unused.** No save path (`Crash_rentsave`, `Crash_crashsave`, `Crash_idlesave`)
  sets it, so it is garbage on disk, and the loader never reads it. It may have been intended
  only for the rent-offer price path. Treat the field as reserved/garbage.
- **Alias on-disk format** is documented above under File layout step 3.
- **Latent bug (caveat for converters/readers).** `Crash_alias_save` writes a 20-byte
  keyword for every alias but `continue`s — skipping the length+command write — when the
  command string is empty (`:972-975`). The loader (`Crash_alias_load`) always reads
  keyword→length→command in lockstep, so an empty-command alias desyncs the stream: it reads
  the next keyword's leading bytes as a length, logs `Alias_load error!`, and returns,
  dropping every alias after the empty one. Real files exhibit this (e.g. an alias with an
  empty command followed by unreachable aliases). A converter should resync on the 20-byte
  alias terminator rather than trusting per-alias framing.
- A 64-bit rebuild would change `long`/`time_t`/alignment and break this format; a converter
  would be required. (Out of scope while building 32-bit.)
