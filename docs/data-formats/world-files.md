# World text files (rooms, mobiles, objects, zones)

**Source files:** `src/db.cpp` (`index_boot:600`, `load_rooms:740`, `setup_dir:867`,
`load_mobiles:1253`, `load_objects:1469`, `fread_string:2525`), `src/zone.cpp`
(`load_zones:45`), `src/db.h` (prefixes/struct defs)
**Status:** ✅ rooms, mobiles, objects, zones — verified against the live world data in
`lib/world/`. Zone reset-command semantics and `reset_mode` values are now documented (below).
Shop files (`.shp`) and Mudlle (`.mdl`)/scripts (`.scr`) have their own docs.

## Purpose
The persistent game world is a set of plain-text files under `lib/world/`, grouped by type.
They define rooms, NPC prototypes (mobiles), item prototypes (objects), and zones (vnum
ranges + reset scripts that populate the world). The game `chdir`s into `lib/` at startup
(`config.cpp:58 DFLT_DIR="lib"`) and loads each category via `index_boot()`.

This is a CircleMUD/DikuMUD lineage format (`#vnum` records, tilde-terminated strings,
tilde/`$` terminators) with substantial RotS-specific field additions — noted per section.

---

## Common conventions

### Directory layout & prefixes (`db.h:33-39`)
```
lib/world/wld/   rooms      (WLD_PREFIX "world/wld")
lib/world/mob/   mobiles    (MOB_PREFIX "world/mob")
lib/world/obj/   objects    (OBJ_PREFIX "world/obj")
lib/world/zon/   zones      (ZON_PREFIX "world/zon")
lib/world/shp/   shops      (SHP_PREFIX "world/shp")
lib/world/mdl/   mudlle     (MDL_PREFIX "world/mdl")
lib/world/scr/   scripts    (SCR_PREFIX "world/scr")
```

### Index file (`index_boot:642-665, 699-736`)
Each prefix directory contains an index file naming the data files to load, one per line,
terminated by a line containing `$`:
```
30.wld
31.wld
...
$
```
Filename is `index` normally, `index.min` in mini-mud mode (`-m`), `index.new` in new-mud
mode (`-n`) (`db.h:30-32` — `INDEX_FILE`/`MINDEX_FILE`/`NEWINDEX_FILE`).
**Boot order matters** (`boot_db:330-382`): scripts → zones → mudlle → **rooms → mobiles →
objects** → shops. Zones load before rooms because room→zone assignment uses zone vnum tops.

### Records
- A record starts with `#<vnum>` (an integer "virtual number").
- Mobile/object files end at vnum `>= 99999` or a token `$` (`db.cpp:1268, 1414, 1483`).
- Room files end with a record whose **name string begins with `$`** (`load_rooms:758`).
- `count_hash_records` (`db.cpp:583`) pre-counts `#` lines to size arrays.

### Strings — `fread_string` (`db.cpp:2525`)
A string spans one or more lines and is terminated by a `~`. Rules:
- Reading continues until a line whose last non-space char is `~`; the `~` is stripped.
- A leading all-blank line is discarded.
- Embedded line breaks are stored internally as `\r` (carriage return).
- Empty string = a lone `~`.

Example (4-line description):
```
You stand in a dim stone hall.  Cobwebs hang from
the ceiling and a cold draft comes from the north.
~
```

### Line endings — not normalized
Real world files do **not** use a single consistent newline. Mobile/object files terminate lines
with **`\n\r` (LF then CR)**; room/zone files are **mixed** (`\r\n`, `\r`, and `\n` all occur,
sometimes within one file). The C loader copes because `fread_string` strips leading control chars
(`< ' '`) and `fscanf`/`%d` treat any whitespace as a separator. **A from-scratch parser must accept
any line ending** — treat `\r` and `\n` interchangeably as separators; do not assume `\n`-only or
`\r\n`.

### Virtual vs real numbers
Files reference **virtual numbers** (vnums). After load, `renum_world`/`renum_zone_table`
convert them to **real** array indices (`db.cpp:926`, `zone.cpp:173`). On-disk = always vnums.

---

## Room file (`.wld`) — `load_rooms:740`, `setup_dir:867`

Per record, in order:
```
#<vnum>
<name>~
<description>~
<zone> <room_flags> <sector_type> <level>
<field>...
S
```
- **Line 4** is 4 ints. The first (`<zone>`) is a placeholder — the room's zone is derived
  from which zone's `top` vnum range contains this vnum (`load_rooms:770-780`), not from this
  field. The meaningful values are `room_flags` (bitvector), `sector_type`, and **`level`**
  (a RotS addition — room level). Some **legacy rooms carry only 3 ints** (no `level`): `sscanf`
  matches 3 and leaves `level = 0` (e.g. `wld/14.wld` `#1401` = `0 8 0`, `wld/100.wld` `#10023` =
  `0 0 0`). (Separately, a different branch reads only 3 ints when no zone table exists yet —
  `db.cpp:788`.)

Then zero or more fields, each introduced by a letter token (`load_rooms:814`):

| Tok | Meaning | Payload |
|-----|---------|---------|
| `D<n>` | Exit in direction `n` (0–5) | `setup_dir`: `<general_desc>~` `<keyword>~` then 4 ints: `exit_info key to_room exit_width` |
| `E` | Extra (look-at) description | `<keyword>~` `<description>~` |
| `F` | Persistent room affect (**RotS**) | 4 ints: `type location modifier bitvector` |
| `S` | End of this room | — |

- Direction index `n`: 0–5 = **N, E, S, W, U, D** (`structs.h:529-534`).
- `exit_info` = door-flag bitvector, `key` = key obj vnum, `to_room` = destination vnum
  (`-1`/`NOWHERE` = none), `exit_width` = **RotS** passage width (mounts/large mobs).
- `F` adds a permanent `affected_type` to the room (`load_rooms:826-851`): `type`,
  `location` (apply), `modifier` (used as spell level), `bitvector` OR'd with `PERMAFFECT`.
  **No `.wld` file in the live corpus uses `F`** — the parser exists but is dead in practice.

---

## Mobile file (`.mob`) — `load_mobiles:1253`

Per record:
```
#<vnum>
<name (keywords)>~
<short_descr>~
<long_descr>~
<description>~
<mob_action_flags>
<affected_by> <alignment> <type_letter>
```
`type_letter` selects the body that follows (`load_mobiles:1303-1397`):
- `N` ("new monster"): two extra strings first — `<death_cry>~` `<death_cry2>~` — then the
  full stat block below.
- `M`: the full stat block, no death cries.
- other (legacy): no stat block.

In the **live world every mob is type `N`** (2,918 / 2,918 across all 321 `.mob` files); the `M`
and legacy branches are dead code for the current dataset.

**Full stat block** (N — and M historically), a flat sequence of ints in the exact order the
`fscanf` calls read them (*file order, not struct order*). **`fscanf` ignores line boundaries**, so
real files pack several of the groups below onto one physical line — the line breaks shown are
illustrative, not significant:
```
<level> <OB> <parry> <dodge>
<hp_current> <hp_max>
<damage> <energy_regen>
<gold> <exp> <ignored_owner>
<position> <default_position> <sex> <race> <pref>
<weight> <height> <store_prog_number> <butcher_item> <corpse_num> <rp_flag>
<prof> <mana> <move> <bodytype>
<saving_throw>
<str> <int> <wil> <dex> <con> <lea>
<language> <perception> <resistance> <vulnerability> <script_number> <spirit>
```
Notes:
- `MOB_ISNPC` is force-set (`:1295`); files needn't include it.
- `corpse_num` is only consumed for `N` mobs (`:1349`).
- The loader's `fscanf` reads a **7th** trailing int (`will_teach`) into a variable pre-set to 0,
  but **no live mob supplies it** — every one of the 2,918 mobs has exactly 6 values on this line,
  so `will_teach` is always 0. Treat it as absent on disk.
- Empty death cries are sometimes stored as the literal `(null)~` (parsed as the 4-char string
  `(null)`), not as an empty `~`.
- `ignored_owner` (3rd value of the gold/exp line) is read into a temporary but never stored; it is
  uniformly `17` in the live data (a legacy placeholder).
- `language` indexes `language_skills[language-1]`; out-of-range → 0 (`:1378`).
- Most of these (energy/regen, OB/parry/dodge, perception, resistance/vulnerability,
  spirit, prof, languages, script_number, butcher_item, rp_flag) are **RotS additions**;
  stock Diku mobiles are far simpler.

---

## Object file (`.obj`) — `load_objects:1469`

Per record:
```
#<vnum>
<name (keywords)>~
<short_description>~
<description>~
<action_description>~
<type_flag> <extra_flags> <wear_flags>
<value0> <value1> <value2> <value3> <value4>
<weight> <cost> <cost_per_day>
<level> <rarity> <material> <script_number> <reserved>
```
Then, optionally:
- Zero or more `E` extra-description blocks: `E` then `<keyword>~` `<description>~`
  (`:1548`).
- Up to `MAX_OBJ_AFFECT` apply lines: `A` then 2 ints `<location> <modifier>` (`:1556`).

The record ends when the next token is `#` (next object) or `$` (EOF) — there is no `S`.
Notes:
- `value[0..4]` meaning depends on `type_flag` (weapon dice, container capacity, light
  duration, etc.) — to be enumerated in the Objects system doc.
- `level`, `rarity`, `material`, `script_number` are **RotS additions** (`:1531-1535`);
  a 5th int on that line is read but currently unused. The old `poisoned`/`poisondata`
  fields are commented out (`:1536-1540`).

---

## Zone file (`.zon`) — `load_zones:45`

Per record:
```
#<zone_number> <name>~              (number AND name on ONE line; `#` is space-padded before name)
<description>~
<map>~
<owner_id> <owner_id> ... 0          (owner list, terminated by 0; rest of line ignored)
<symbol> <x> <y> <level>             (%c %d %d %d)
<top>                                (highest room vnum belonging to this zone)
<lifespan>                           (minutes between resets)
<reset_mode>                         (0=never; 1=empty & age≥lifespan; 2=always when age≥lifespan; 3=hybrid — see below)
<command lines...>
S
```
The `description`, `map`, owner list, `symbol/x/y/level` are **RotS additions** (stock Diku
zones have only name, top, lifespan, reset_mode).

### Reset command lines (`load_zones:86-163`)
Each command is:
```
<cmd> <if_flag> <arg1> <arg2> <arg3> <arg4> <arg5> [<arg6> [<arg7>]]   <comment to EOL>
```
- `if_flag`: if nonzero, execute only if the previous command executed
  (DikuMUD-style chaining).
- Commands `M N X H E K Q` read **two** extra args (`arg6 arg7`); `P` reads **one** (`arg6`);
  others read five args (`load_zones:127-143`).
- `S` terminates the command list (`:98`).
- The trailing text on each line is a human comment (only preserved by the OLC `shapezon`).

Command letters confirmed in the **live** `.zon` files (counts across all 341 files): `M` load
mobile, `O` load object (×1502), `G` give object to last-loaded mob (×52), `K` equip/kit mob,
`P` put object in container, `D` set door state (×1722), `A`/`L` attribute-set / condition-check
(`arg1` = sub-type selector), plus `N`, `X`, `H`, `E`, `Q`, and `.` (a no-op placeholder, ×122).
The classic Diku `M/O/G/E/P/D` letters are **all live and used** — an earlier note that `O`/`D`/`G`
were "stale" was wrong; they were simply absent from the (then-unavailable) sample data.

---

## Zone reset-command semantics (`reset_zone` / `zone.cpp` + live data)
- `M arg1 arg2 _ arg4 arg5 [arg6 arg7]` — load mob `arg1` into room `arg2`; `arg4` = spawn prob %,
  `arg5` = max allowed in world.
- `O arg1 arg2 _ arg4 arg5` — load object `arg1` into room `arg2`.
- `G arg1` — give object `arg1` to the last-loaded mob.
- `P _ arg2 arg3 … arg6` — put object `arg2` into container `arg3`; `arg6` = count.
- `K arg1…arg7` — equip the last-loaded mob with the listed object vnums (wear/wield).
- `D arg1 arg2 arg3` — set exit `arg2` of room `arg1` to door-state `arg3`.
- `A` / `L` — `arg1` selects a sub-type; attribute-set / condition-check commands.
- `.` — no-op placeholder (reads the base 5 args, does nothing at runtime).
- `if_flag` (2nd field): if nonzero, the command runs only if the previous command executed.

**`reset_mode`** (`zone.cpp:308-327`): `0` never; `1` reset when the zone is empty **and**
age ≥ lifespan; `2` always when age ≥ lifespan; `3` hybrid — (empty **and** age ≥ lifespan)
**or** age ≥ 3 × lifespan.

The **object record tail** question is resolved: in the live corpus nothing follows the `A`
lines but the next `#<vnum>` or `$`.

## Open questions
- **Bitvector/enum tables**: `room_flags`, `sector_type`, `exit_info`, mob `action`/
  `affected_by`, object `type_flag`/`extra_flags`/`wear_flags`, apply `location` codes —
  to be enumerated (belongs in catalogs + the per-system docs).
