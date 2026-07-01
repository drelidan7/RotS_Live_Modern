# Shape OLC builder (in-game world editor)

**Source files:** `src/shapemob.cpp` (entry point `do_shape`, mobile editor, `get_permission`,
`recalculate_mob`), `src/shaperom.cpp` (room editor), `src/shapeobj.cpp` (object editor),
`src/shapezon.cpp` (zone editor), `src/shapescript.cpp` (`.scr` script editor),
`src/shapemdl.cpp` (`.mdl` Mudlle-program editor); shared state structs and `SHAPE_*` constants
in `src/protos.h:15-235`.
**Status:** ✅ done — all six editors analyzed at the source-line level; `shaperom.cpp` (room
editor) fully read and cited directly for this doc, the other five cross-checked against prior
dense extractions and spot-verified against source.

## Purpose

"Shape" is RotS's in-game, line-oriented OLC (online creation) system — the builder tool used to
create and edit rooms, mobile (NPC) prototypes, object prototypes, zones, `.scr` scripts, and
`.mdl` Mudlle programs, all from inside a live telnet session, without restarting the server. It
is entered via the `shape` player command, `ACMD(do_shape)` (`src/shapemob.cpp:1861`), which
allocates one of six per-type state structs into the generic `ch->temp` field and then hands
control to a per-type "center" function (`shape_center_proto/obj/room/zone/script/mudlle`) that
runs for every subsequent line the player types, until the player types `free` or `done`.

This is **not** a player-facing shapeshifting/polymorph spell — a repo-wide grep for
`shapechange`/`polymorph`/`change_shape`/`SPELL_SHAPECHANGE` outside the `shape*.cpp` files
returns nothing, confirming no such mechanic exists in this codebase. The `docs/README.md`
backlog item "Shapeshift builder" refers to this OLC system; the name is a historical misnomer
(the game calls it "shape" because it lets a builder "shape" the world, not because it changes
character shapes).

Each editor is single-record-at-a-time: a builder loads or creates one room/mob/object/zone/
script/program, edits fields by number, then explicitly `save`s (writes to disk) and/or
`implement`s (patches the live in-memory prototype/table) it. The on-disk record formats it reads
and writes are the same `.wld`/`.mob`/`.obj`/`.zon`/`.mdl`/`.scr` formats documented in
`docs/data-formats/world-files.md` and `docs/data-formats/mudlle-and-scripts.md` — this doc
does not repeat those grammars except where the OLC's writer deviates from or is a strict subset
of them.

## Data structures

### Procedure / flag constants (`src/protos.h:15-53`)

Editor selector passed to `do_shape` / used as `ch->temp`'s discriminant tag (`act` field):
`SHAPE_PROTOS 1` (mobile), `SHAPE_OBJECTS 2`, `SHAPE_ROOMS 3`, `SHAPE_ZONES 4`, `SHAPE_MUDLLES 5`,
`SHAPE_RECALC_ALL 6`, `SHAPE_MASTER_MOBILE 7`, `SHAPE_MASTER_OBJECT 8`, `SHAPE_SCRIPTS 9`.

`procedure` state machine values (shared by every editor): `SHAPE_NONE 0`, `SHAPE_CREATE 1`,
`SHAPE_EDIT 2`, `SHAPE_LOAD 3`, `SHAPE_SAVE 4`, `SHAPE_ADD 5`, `SHAPE_STOP 6`, `SHAPE_FREE 7`,
`SHAPE_DELETE 8`, `SHAPE_SIMPLE_EDIT 9`, `SHAPE_IMPLEMENT 10`, `SHAPE_MODE 11`,
`SHAPE_RECALCULATE 12`, `SHAPE_DONE 13`, `SHAPE_CURRENT 14` (zone-only, toggles "current room
only" listing).

`flags` bitfield (same numeric value reused per-struct as `SHAPE_PROTO_LOADED` /
`SHAPE_OBJECT_LOADED` / `SHAPE_ROOM_LOADED` / `SHAPE_ZONE_LOADED` / `SHAPE_MUDLLE_LOADED` /
`SHAPE_SCRIPT_LOADED` = 1): `SHAPE_SIMPLE_ACTIVE 2` (multi-line string editor is mid-collection),
`SHAPE_DIGIT_ACTIVE 4` (single-line/numeric prompt is awaiting its second entry),
`SHAPE_DELETE_ACTIVE 8` (save pass should omit the record instead of writing it — this is how
delete is implemented), `SHAPE_FILENAME 16` (a target file path has been resolved),
`SHAPE_SIMPLEMODE 32` (mobile editor only: field numbers are being remapped through the "simple"
menu), `SHAPE_CHAIN 64` (currently walking the guided field-by-field creation sequence),
`SHAPE_CURRFLAG 128` (zone editor: auto-track "current room" for masked listing).

Accessor macros cast `ch->temp`: `SHAPE_PROTO(ch)`, `SHAPE_OBJECT(ch)`, `SHAPE_ROOM(ch)`,
`SHAPE_ZONE(ch)`, `SHAPE_MUDLLE(ch)`, `SHAPE_SCRIPT(ch)` (`protos.h:55-60`) — only one is valid at
a time per player, since `ch->temp` is a single untagged pointer; the game relies on the caller
always knowing which editor is active (no runtime type tag is checked beyond the `act` field a
few call sites read).

### Per-record file path templates (`protos.h:62-78`)

| Type | Live file | Backup-on-save file |
|---|---|---|
| Mobile | `world/mob/%s.mob` (`SHAPE_MOB_DIR`) | `world/mob/oldmobs/%s.mob` |
| Object | `world/obj/%s.obj` (`SHAPE_OBJ_DIR`) | `world/obj/oldobjs/%s.obj` |
| Room | `world/wld/%s.wld` (`SHAPE_ROM_DIR`) | `world/wld/oldroms/%s.wld` |
| Zone | `world/zon/%s.zon` (`SHAPE_ZON_DIR`) | `world/zon/oldzons/%s.zon` |
| Mudlle | `world/mdl/%s.mdl` (`SHAPE_MDL_DIR`) | `world/mdl/oldmdls/%s.mdl` |
| Script | `world/scr/%s.scr` (`SHAPE_SCRIPT_DIR`) | `world/scr/oldscrs/%s.scr` |

`%s` is always the **zone bucket** — `vnum / 100`, formatted as a bare integer (e.g. vnum 3042 →
file `world/wld/30.wld`) — matching the `<zone>/100` bucketing convention in
`docs/data-formats/world-files.md`.

### Per-editor state structs (`protos.h:82-221`)

Each struct carries: `act` (type tag), the in-progress record pointer (`proto`/`object`/`room`/
`root` zone-tree/`script`/`txt`), `procedure`, `editflag` (which numbered field/case is active —
the state-machine cursor), `flags`, `f_from`/`f_old` (resolved file paths, 80-byte buffers),
`permission` (result of `get_permission`, gates save/implement), `position` (the player's stance
before `shape_standup` sat them down, restored on exit), and `tmpstr` (scratch buffer used by the
multi-line `string_add_init` collector). Type-specific extras:
- `shape_proto` (mobile): `mode` ('N'/'M' record format), `shift` (declared, unused —
  see Open questions), `number` (vnum, `-1` if never saved).
- `shape_object`: `basenum` (vnum in DB, `-1` if fresh), `number`.
- `shape_room`: `exit_chosen` (which of the 6 directions is currently selected for exit sub-field
  editing, `-1` = none).
- `shape_zone`: `root`/`curr` (`zone_tree` linked list — one node per reset command),
  `zone_name`/`zone_descr`/`zone_map`/`zone_number`/`top`/`lifespan`/`reset_mode`, `root_owner`
  (`owner_list` linked list — `struct owner_list { int owner; owner_list* next; }`, `zone.h:8-11`),
  `x`/`y`/`level`/`symbol` (map fields), `mask` (a `reset_com` used to filter the `/50` listing),
  `cur_room` (0 = whole zone, else filter to one room).
- `shape_script`: `index_pos` (position in the live `script_table`, `-1` if never implemented),
  `script`/`root` (`script_data` linked list), `cur_room`, `name`, `description`, `number`.
- `shape_mudlle`: `txt` (the entire program source as one string — this editor has no field
  structure at all), `prog_num` (vnum typed at `/load`), `real_num` (index into the live
  `mobile_program[]` array, from `real_program()`).

`zone_tree` (`protos.h:127-135`) wraps a `reset_com` (the same struct
`docs/data-formats/world-files.md` documents for the on-disk `.zon` command lines) with `room`/
`number` (computed, not stored) and a builder-only free-text `comment` — the comment is preserved
across save/load by the OLC but is **not** part of the canonical `.zon` grammar column count
documented in `world-files.md` (it rides along as trailing text on the command line).

## Format / Algorithm

### 1. Entry point and permission model

`do_shape` (`shapemob.cpp:1861-2149`) dispatches on the first word of the command:
`mobile`→`SHAPE_PROTOS`, `object`→`SHAPE_OBJECTS`, `room`→`SHAPE_ROOMS`, `zone`→`SHAPE_ZONES`,
`program`→`SHAPE_MUDLLES`, `script`→`SHAPE_SCRIPTS`, plus three admin-only verbs matched by exact
string: `recalc_mobile` (requires `GET_LEVEL(ch) == LEVEL_IMPL` = 100, `:1887-1888`),
`master_mobile` and `master_object` (require `GET_LEVEL(ch) >= LEVEL_GRGOD` = 97, `:1890-1895`).

**Level gate for everyone else** (`:1901-1904`):
```cpp
if ((GET_LEVEL(ch) < LEVEL_GOD) && (key != SHAPE_ROOMS)) {
    send_to_char("You are permitted to shape rooms only.\n\r", ch);
    return;
}
```
`LEVEL_GOD = 93` (`structs.h:49`). So **any staff member below level 93 (GOD) may only ever reach
the room editor** — mobile/object/zone/script/program editing requires GOD+. This is the reason
the room editor (`shaperom.cpp`) is the most-exercised editor in practice.

A player may only shape one thing at a time: `do_shape` refuses if `ch->temp` is already set
(`:1908-1911`, "You are already shaping something. Free it first."), and each editor's `free`
command releases the struct and clears `ch->temp` (e.g. `free_room`, `shaperom.cpp:1604-1651`).

**Zone-ownership permission** — `get_permission(int zonnum, char_data* ch, int mode = 0)`
(`shapemob.cpp:167-201`, prototype `protos.h:230`):
1. Look up `zonnum` in `zone_table` by matching `.number`; if not found, print a warning and
   return 0 (no zone ⇒ no permission).
2. If the zone's owner list head has `owner == 0` ("common/unowned" zone): permission =
   `mode ? 0 : 1`, **then forced to 0 if `GET_LEVEL(ch) < LEVEL_GOD`** — so an unowned zone is
   editable by any GOD+ builder when `mode==0`, but `mode=1` callers (zone-ownership-list edits)
   always deny access to unowned zones regardless of level.
3. Otherwise (owned zone): permission = 1 iff `ch->specials2.idnum` appears in the zone's owner
   list, else 0.
4. **Unconditional override:** `if (ch->player.level >= LEVEL_GRGOD) perm = 1` — GRGOD (97) and
   above always have permission, regardless of ownership.
5. If the result is 0, sends "You have no permission to this zone. Limited permission only."
   (the record can still be *loaded* and viewed/edited in the local buffer, but `save`/`add`/
   `implement` all re-check `permission != 0` and refuse.)

**Master idnums** (`mobile_master_idnum`, `object_master_idnum`, externs declared
`shapemob.cpp:24-25`) are a bypass: set via `shape master_mobile <idnum>` /
`shape master_object <idnum>` (GRGOD+ only, `:2131-2143`). `load_proto` (`shapemob.cpp:1271-1274`)
special-cases `GET_IDNUM(ch) == mobile_master_idnum` to force `permission = 1` unconditionally,
**outright bypassing `get_permission`/zone ownership** for the one designated player id.
`load_object` (`shapeobj.cpp:1079-1086`) does the same but checks **two** ids:
`GET_IDNUM(ch) == object_master_idnum || GET_IDNUM(ch) == object_master2_idnum`
(`object_master2_idnum` extern'd at `shapeobj.cpp:18`). Unlike `object_master_idnum`,
`object_master2_idnum` has **no in-game setter** — `shape master_object` only ever overwrites
`object_master_idnum` — it is a permanent compile-time bypass hardcoded in `config.cpp:120`
(`int object_master2_idnum = 35795; // Incanus`, alongside `object_master_idnum = 1293; // Erika`
at `config.cpp:119` and `mobile_master_idnum = 51566; // Raziel` at `config.cpp:118`). There is no
equivalent master idnum for rooms, zones, scripts, or Mudlle programs.

### 2. Shared editing framework (state machine, prompt macros, sanitization)

Every "center" function (`shape_center_proto/obj/room/zone/script`) follows the same shape:

```
if procedure not in {SHAPE_NONE, SHAPE_EDIT}:
    forward to extra_coms_<type>(ch, arg)     // word commands: load/save/free/done/...
    return
if procedure == SHAPE_NONE:
    print "Enter any non-number..." banner; return
do {
    if editflag == 0:
        if first token of arg is a digit: editflag = atoi(token)   // field number
        else: forward to extra_coms_<type>(ch, arg); return
    if not <TYPE>_LOADED: print "nothing to shape"; return
    switch (editflag) {
        case <N>: <edit field N>; if SHAPE_CHAIN: editflag = <type>_chain[N]; break;
        ...
        case 50: list; editflag = 0; break;
        default: print help; editflag = 0; break;
    }
} while (editflag != 0);
```
(`shaperom.cpp:421-1016` for the room editor; identical skeleton confirmed in `shapemob.cpp:449-
1017`, `shapeobj.cpp:339-863`, `shapezon.cpp:658+`, `shapescript.cpp:1368-2275`.) `shape_mudlle`
is the exception — it has no numbered field editor at all (§3.6 below).

**Two-phase prompt pattern.** Each field-editing macro is entered twice per edit, using a flag bit
to distinguish first entry (print prompt, sit the player down, return) from second entry (consume
the player's next line as the value, stand back up, clear `editflag`). Concretely, `LINECHANGE`
(single-line string field, e.g. `shaperom.cpp:354-398`):
```cpp
#define LINECHANGE(line, addr)
    if (!IS_SET(flags, SHAPE_DIGIT_ACTIVE)) {
        print "Enter line <line>:\n[<addr>]"; position = shape_standup(ch, POSITION_SHAPING);
        prompt_number = 2; SET_BIT(flags, SHAPE_DIGIT_ACTIVE); return;
    } else {
        sscanf(arg, "%s", str);               // second entry: str is the new value
        if (str == "%q") { arg[0] = 0; }      // %q => set field to empty string
        RELEASE(addr); CREATE(addr, ...); strcpy(addr, arg);
        for each char: '#' -> '+', '~' -> '-';  // protect record-delimiter chars
    }
    REMOVE_BIT(flags, SHAPE_DIGIT_ACTIVE); shape_standup(...); prompt_number = 4; editflag = 0;
```
`DIGITCHANGE`/`DIGITCHANGEL` are the integer analog (uses `string_to_new_value(arg, &tmp)`, which
supports absolute values, `+N`/`-N` relative deltas, and `pN`/`mN` set/clear-bit-N forms —
`utility.cpp:390`). `DESCRCHANGE` is the multi-line analog, used for long free-text fields
(room/mob description, object action description, extra-description text, zone
description/map, script description, and the entire Mudlle program body):
```cpp
#define DESCRCHANGE(line, addr)
    if (!IS_SET(flags, SHAPE_SIMPLE_ACTIVE)) {
        print "You are about to change <line>:"; position = shape_standup(...);
        prompt_number = 1; SET_BIT(flags, SHAPE_SIMPLE_ACTIVE);
        tmpstr = str_dup(addr);
        string_add_init(ch->desc, &tmpstr);    // hand off to the shared multi-line collector
        return;
    } else {
        addr = tmpstr; clean_text(addr); tmpstr = 0;
        REMOVE_BIT(flags, SHAPE_SIMPLE_ACTIVE); shape_standup(...); prompt_number = 4;
        editflag = 0;
    }
```
`string_add_init` / `string_add` (`modify.cpp:178+`, `:241+`) is the game's shared multi-line text
editor (also used by `mset description` etc.): it collects lines into `*d->str` until the player
types `%e` (save and exit — triggers the macro's second-entry branch), `%q` (abort, discards the
buffer), or `%h` (prints help). This is a different terminator convention from the single-`@`-line
editor mentioned elsewhere in the codebase — Shape always uses the `%e`/`%q` variant.

`clean_text(char* str)` (`shapemob.cpp:152-166`, shared by every editor) sanitizes text before it
is written to disk: converts a **leading** `#` on each line to `+` (protects the `#<vnum>` record
delimiter) and **every** `~` to `-` (protects the string terminator), tracking start-of-line via a
`startline` flag reset after the first non-space character. `LINECHANGE`'s inline sanitizer does
the same two substitutions but unconditionally (single-line fields have no "start of line"
ambiguity).

`shape_standup(ch, pos)` (`shapemob.cpp:203+`) sits the player at `POSITION_SHAPING` while a
multi-part prompt is outstanding and restores their prior position afterward; it returns the
*previous* position so the macro can restore it on the second entry.

**Field chaining.** A 50/51-element `int <type>_chain[]` array (`proto_chain` `shapemob.cpp:32-
38`, `obj_chain` `shapeobj.cpp:22-28`, `room_chain` `shaperom.cpp:105-111`) maps "just-finished
field N" → "next field to prompt for", but **only while `SHAPE_CHAIN` is set** (entered via the
`new`/creation-sequence command, or field 49 in the mobile/object editors, field 15 in the room
editor for extra-description creation). This drives the guided "walk me through every field"
flow used when creating a brand-new record; outside the chain, finishing a field simply sets
`editflag = 0` and returns control to the player for the next `/<n>` command. A chain value of 0
ends the sequence.

**Mobile editor's extra "simple mode" indirection**: the mobile editor additionally supports a
`SHAPE_SIMPLEMODE` flag that remaps a smaller numbered menu (fields 1-12, 40, 49, 50) onto the
full extended field-number space via `command_simple_convert()` (`shapemob.cpp:111-151`) — e.g.
simple field `7` (level) → extended field `8`. This remap is applied only when `SHAPE_SIMPLEMODE`
is set and `SHAPE_CHAIN` is not (`shapemob.cpp:500-503`). No other editor has a simple-mode
layer.

### 3. Per-editor field catalog and command set

All six editors share the same top-level word-command vocabulary via their own
`extra_coms_<type>` dispatcher (prefix-matched with `strncmp(str, "keyword", strlen(str))`, so any
unambiguous prefix works): `load`, `save`, `add`, `free`, `done`, `delete`, `implement`, plus a
creation verb that is spelled `new` for mobile/object/script but `create` for room/zone (room and
zone additionally accept both spellings in places — see quirks below). `done` is always
`save` + `implement` + `free` in one step (e.g. `shapezon.cpp:2126`, `shaperom.cpp:1786-1793`).

#### 3.1 Room editor (`shaperom.cpp`) — most-used editor (non-GOD builders are restricted to it)

Numbered fields (`list_help_room`, `shaperom.cpp:1018-1042`; edit cases `:468-1013`):

| # | Field | Room struct member | Notes |
|---|---|---|---|
| 1 | name | `room->name` | `LINECHANGE` |
| 2 | description | `room->description` | `DESCRCHANGE`, multi-line |
| 3 | room flag | `room->room_flags` | `DIGITCHANGEL` (long bitvector), raw integer, no flag-name table |
| 4 | sector type | `room->sector_type` | `DIGITCHANGE`, raw integer |
| 5 | select exit | `exit_chosen` (not persisted to the room) | prompts for a direction letter U/D/N/E/S/W; creates a blank `dir_option` slot if none exists yet (`:726-733`) |
| 6 | exit type (flags) | `dir_option[exit_chosen]->exit_info` | requires field 5 first; the historical `convert_exit_flag()` compact-code translator (`:31-103`) is present but commented out at the call site — the field is edited as a raw bitvector, not the compact 0-4/10-14/... code |
| 7 | remove exit | frees `dir_option[dir]` | prompts for a direction letter directly (does not require field 5) |
| 8 | exit keyword | `dir_option[exit_chosen]->keyword` | `LINECHANGE`; requires field 5 first |
| 9 | exit description | `dir_option[exit_chosen]->general_description` | `DESCRCHANGE`; requires field 5 first |
| 10 | key number | `dir_option[exit_chosen]->key` | `DIGITCHANGE` |
| 11 | room to exit to | `dir_option[exit_chosen]->to_room` | `DIGITCHANGE`, raw vnum, **no existence check** |
| 12 | exit width | `dir_option[exit_chosen]->exit_width` | `DIGITCHANGE`, RotS addition (mount/large-mob passage width) |
| 13 | extra-desc keyword | head of `ex_description` list | `LINECHANGE`; only edits the **most recently added** (list head) extra description |
| 14 | extra description | head of `ex_description` list | `DESCRCHANGE` |
| 15 | add extra description | prepends a blank node to `ex_description` | auto-chains into field 13 (keyword) via `room_chain[15]` (`room_chain[15] == 13`) |
| 16 | remove extra description | pops the head of `ex_description` | |
| 17 | room level | `room->level` | `DIGITCHANGE`, RotS addition |
| 18 | change room affection | — | **disabled**: prints "Not available at the moment" (`:903-908`); real code is commented out, dated "fingolfin, december 2001", marked unstable |
| 19 | add new room affection | — | **disabled**, same comment/date (`:949-955`) |
| 20 | remove top room affection | — | **disabled**, same comment/date (`:971-977`) |
| 50 | list | `list_room()` | prints all of the above (`:1044-1116`) |

Room affections (`room->affected`, an `affected_type` list — same struct family as
character/object affects) are therefore **read-only in the live OLC**: `load_room` parses `F`
lines from disk into the list and `write_room`/`implement_room` still write/apply them, but a
builder cannot add, remove, or change one interactively; only whatever was already on disk (or
pre-existing in the loaded record) survives a round trip. `docs/data-formats/world-files.md`
independently notes no live `.wld` file actually uses the `F` token, so this is dead code on
both the read and write side of the current dataset.

Room-editor-specific commands (`extra_coms_room`, `:1653-1796`): `create <zone#>` →
`create_room()` (`:1314-1375`) allocates a blank room, verifies `1 <= zone# <= MAX_ZONES` (500,
`structs.h:82`) and `get_permission(zone#, ch)`, then immediately calls `append_room()` to
allocate + persist a vnum (rooms are given a vnum at creation time, unlike mobiles/objects which
stay `-1` until first save/add). `load <vnum>` → `load_room()` (`:1123-1311`, detailed in §4).
`add`/`save`/`delete`/`implement`/`done` all gate on `SHAPE_ROOM(ch)->permission != 0` inside
`replace_room`/`append_room`/`implement_room`.

#### 3.2 Mobile (mob prototype) editor (`shapemob.cpp`)

41 numbered fields (extended mode) covering the full `char_data` prototype: identity (name/short/
long/description), flags (`act` bitvector — forced to include `MOB_ISNPC`, `affected_by`,
`resistance`, `vulnerability`, `rp_flag`, race `pref`/aggression), combat stats (OB/parry/dodge,
min/max hit, damage, energy regen), economy (gold, xp), position/default position, sex/race/
bodytype, six ability scores (str/int/wil/dex/con/lea), language (clamped only at the **upper**
bound to `language_number`, `:866-867` — `if (mob->player.language > language_number)
mob->player.language = language_number;` has no matching lower-bound/negative check), perception,
saving throw, script/program numbers, death cries,
corpse vnum, butcher item, spirit, will-teach. See the full # → member table already captured in
the per-file extraction (reproduced faithfully; spot-checked field 30's language clamp at
`shapemob.cpp:864-873`). No editor-side symbolic flag-name tables exist anywhere in this file —
`grep` for `action_bits`/`affected_bits` finds nothing; bitfields are edited as raw integers via
`DIGITCHANGE`/`DIGITCHANGEL` (which support `+N`/`-N`/`pN`/`mN` relative edits through
`string_to_new_value`). A "simple mode" 12-field subset (name/short/long/description/flags/
affections/level/sex/race/bodytype/race-aggression/butcher-item/spirit) is remapped onto the
extended field numbers via `command_simple_convert()`.

Mobile-only special commands: field 48 forces a Y/N-gated `recalculate_mob()` (formulas in
`shapemob.cpp:40-109`, unchanged from the earlier extraction) before continuing; `recalc_mobile`
(top-level `do_shape` verb, LEVEL_IMPL only) iterates every mob in `mob_index[]`, recalculates and
saves any whose `act` flags don't include `MOB_NORECALC` (`structs.h:925`), then **reboots the
MUD** via `do_shutdown` (`shapemob.cpp:2114-2129`) — the only Shape operation with a
whole-server side effect.

#### 3.3 Object (item prototype) editor (`shapeobj.cpp`)

21 numbered fields: identity (alias/short/full/action descriptions), extra descriptions (add/
edit-keyword/edit-text/remove — same head-of-list-only pattern as rooms), type/extra/wear flags
(raw integers, no `item_types[]`/`wear_bits[]`/`extra_bits[]` name tables referenced anywhere in
the file), `value[0..4]` (five raw ints, no per-type-specific prompts), weight/cost/cost-per-day/
level/rarity/material (only `material` gets a name-table *display*, via `object_materials[]`, in
the `list` command — not in the edit prompt), affections (raw `(location modifier)` integer
pairs, terminated by `APPLY_NONE`), program/script numbers. Field 49 drives the creation chain.
Quirk: the word command is `new` but the printed help advertises it as `create`
(`shapeobj.cpp:1758` vs. `:1792`).

#### 3.4 Zone editor (`shapezon.cpp`)

The only editor with **no interactive creation path** — `create` prints "Zone cannot be created
that simple... talk to the Implementors" (`:2089`); zones must already exist in the zone table
and be `load`ed by number. Fields cover zone metadata (name, multi-line description and ASCII
map, reset lifespan/mode, level, map symbol + x/y coordinates — the last two gated to
`level > LEVEL_GRGOD`), the owner list (add/remove, gated to `level >= LEVEL_AREAGOD` (95) *and*
`get_permission(zone, ch, mode=1)`, which — per §1 rule 2 above — always denies unowned/common
zones this particular edit regardless of level unless GRGOD+), and the zone's reset-command list
as a doubly linked `zone_tree`. Command editing is two-step: `/3` picks the command letter
(`M N O G H E Q P D L A K` — the prompt text itself only advertises `<MOGEPDKAL>`
(`shapezon.cpp:845`), a stale subset that omits `N`, `H`, and `Q`, but the `switch` at
`shapezon.cpp:864-888` accepts all twelve, matching the letters `docs/data-formats/world-files.md`
documents for the live `.zon` corpus) then chains to `/4`, which prompts for the letter-specific
argument count (`A`: 3 ints, no `if_flag`; `O`/`G`: 6 ints; `P`: 7; `L`: 7; `K`: 8; `D`: 4;
`M`/`N`/`H`/`E`/`Q`: 8 ints), then `/5` for the free-text comment. The `D` (door state) case
(`shapezon.cpp:945-957`) clamps `state` (`tmp[3]`) to `<= 2` and `dir` (`tmp[2]`) to `>= 0`, but
has an apparent copy-paste bug on the upper bound: `if (tmp[2] > 5) tmp[3] = 0;` zeroes **`state`**
instead of clamping **`dir`** when `dir > 5` — `dir` itself is never capped at 5. **No vnum
existence validation happens in the editor** — a builder can type any mob/object/room vnum for a
command's arguments and it is accepted verbatim; validation to a real-number (rnum) only happens
at `implement` time via `renum_zone_one` (`zone.cpp:188`), and a bad vnum there simply resolves
to an invalid rnum rather than being rejected up front.

`/16`/`/17` (coordinates, symbol) require `level > LEVEL_GRGOD`; the load path itself also grants
full permission unconditionally to `level >= LEVEL_GRGOD` builders (`:1706-1709`) regardless of
ownership, echoing the `get_permission` override.

#### 3.5 Script editor (`shapescript.cpp`)

Structured command-list editor for the `.scr` format (`docs/data-formats/mudlle-and-scripts.md`
§Scripts): fields are name (`LINECHANGE`), description (`DESCRCHANGE`), and a doubly linked list
of `script_data` commands, each with a `command_type` (chosen by *name*, not number — `get_command`
(`:2277-2466`) is a hand-written `switch`/`strcmp` cascade with no lookup table, covering every
trigger/control/conditional/action/system opcode the language defines), up to 6 `param[]` slots
(edited either as symbolic tokens resolved by `get_parameter()`, e.g. `ch1.level`, `ob2.vnum`,
`int1`, or as raw integers via a digit-only variant), and a free-text `text` field (used for
`do_say` text, comments, etc.). Commands can be navigated (next/prev/select-by-number), inserted
before/after, removed, and swapped with the next command; a "current room" filter restricts
navigation to commands tagged with a matching room. `check_script_syntax` (case `/14`) is a stub
that does nothing.

#### 3.6 Mudlle program editor (`shapemdl.cpp`)

The simplest editor: there is exactly **one editable field**, the entire program source text
(`edit` word-command hands the whole `txt` buffer to `string_add_init`, no field-numbered menu at
all). `load <n>` either finds an existing `#n` record in the zone-bucket `.mdl` file or, if it
hits the `#99999` sentinel first, starts a fresh empty program at that number. `save` inserts or
replaces the record in vnum-sorted position in the file. This editor is also the only one whose
disk backup is a shelled-out `cp` (`COPY_COMMAND` from `platdef.h:32`) rather than an in-process
character-by-character copy loop — see quirks below.

### 4. Load path (representative: rooms)

`load_room(ch, "load <vnum>")` (`shaperom.cpp:1123-1311`):
1. Parse `<vnum>` with `sscanf`; compute the zone-bucket filename `vnum/100` and open
   `world/wld/<bucket>.wld` `"r+"`.
2. Store the resolved path as `f_from`, set `SHAPE_FILENAME`, and precompute the backup path
   `f_old = world/wld/oldroms/<bucket>.wld`.
3. Resolve `permission = get_permission(bucket, ch)`.
4. `find_mob(f, vnum)` (name is generic/reused across editors) scans `#<n>` headers until it
   finds `n >= vnum`. If `n == -1` (EOF, corrupt file) → abort. If `n > vnum` (record absent) →
   silently treat as a fresh room via `new_room()` and report "could not find room #N, created
   it." — **loading a nonexistent vnum creates a blank in-memory room rather than erroring**,
   and that new room keeps the *requested* vnum, not `n`.
5. Otherwise parses the found record: two `get_text()` strings (name, description), then the
   `<zone-placeholder> <room_flags> <sector_type> <level>` line, then a loop over `E`/`F`/`D<n>`
   tokens until `S` — this is exactly the `.wld` grammar in `world-files.md`, read with the same
   loose `fscanf`/`fgets` mixture (line boundaries are not significant for the numeric block).
6. Sets `SHAPE_ROOM_LOADED`, records `prompt_value = vnum`, resets `exit_chosen = -1`.

Mobile (`load_proto`, `shapemob.cpp:1245-1541`), object (`load_object`, `shapeobj.cpp:1050-1344`),
zone (`load_zone`, `shapezon.cpp:1643-1813`), and script (`load_script`) follow the identical
five-step shape (bucket by `vnum/100`, resolve `f_from`/`f_old`, resolve permission — with the
master-idnum bypass for mobile/object — scan for the record or synthesize a blank one, parse the
body per that format's grammar). The Mudlle loader (`load_mudlle`, `shapemdl.cpp:27-125`) is the
odd one out: it does **not** abort on zero permission, only warns "limited permission" — the
enforcement is deferred to `save_mudlle`/`implement_mudlle`.

### 5. Save path — the generic file-rewrite algorithm

Every "replace" function (`replace_proto`/`replace_object`/`replace_room`/`replace_zone`/
`replace_script`) implements the **same four-step in-place rewrite**, illustrated here from
`replace_room` (`shaperom.cpp:1378-1494`):

1. **Guard**: must have `SHAPE_FILENAME`, must have the record loaded, `f_from != f_old`, and
   `permission != 0` (else "You may not do that in this zone."). If the record's number is still
   `-1` (never saved/added), delegate to the "append" function instead (creating a new vnum,
   §6).
2. **Backup**: open `f_from` `"r+"` and `f_old` `"w+"`, then copy the **entire** source file into
   the backup byte-by-byte via a `fscanf("%c")`/`fprintf("%c")` loop running until `EOF`. This
   fully overwrites whatever was in `world/<type>/old<type>s/<bucket>.<ext>` from a previous save
   — it is a single-generation backup, not a history.
3. **Rewrite**: reopen `f_from` `"w"` (**this truncates the live file**) and `f_old` `"r"` (the
   backup just made). Stream backup → live, copying bytes verbatim, but intercept every `#`:
   parse the following record number `i`; while `i < target_vnum`, copy the record through
   unchanged; when `i == target_vnum`, stop copying (the old body is skipped by scanning to the
   next `#`) and capture the *next* record's number as `oldnum`.
4. **Write & resume**: unless `SHAPE_DELETE_ACTIVE` is set, call the type's `write_<type>()` to
   emit the freshly edited record at the target vnum; then write the `#<oldnum>` header for the
   record that followed, and copy the remainder of the backup file through verbatim to `EOF`. If
   `SHAPE_DELETE_ACTIVE` **is** set, step 4 simply omits the `write_<type>()` call — the target
   record is dropped from the rewritten file and nothing else changes. This is the *entire*
   implementation of `delete`: `extra_coms_room`'s `SHAPE_DELETE` case (`:1754-1779`) is a
   two-message "are you sure? type yes" confirmation gate that, on confirmation, sets
   `SHAPE_DELETE_ACTIVE` and calls the same `replace_room()`.

Object/mobile/zone/script "replace" functions are structurally identical (confirmed by direct
reading of `shapeobj.cpp:1352-1527`, `shapemob.cpp:1546-1664`, `shapezon.cpp:1817-1973`,
`shapescript.cpp:401-507`); only the record grammar written by step 4's `write_*` differs, and
each matches its corresponding on-disk format in `docs/data-formats/world-files.md` /
`mudlle-and-scripts.md` (e.g. `write_proto` always emits the `N`-format mobile record with an `N`
literal on the flags line, matching the "every live mob is type N" observation in
`world-files.md`).

**Crash-safety caveat (applies to all six editors):** there is no temp-file-plus-atomic-rename.
The live file is opened `"w"` (truncating it) and rewritten in place directly from the backup; a
crash or `kill -9` mid-rewrite leaves a truncated/partial `.wld`/`.mob`/`.obj`/`.zon`/`.scr` file,
recoverable only by manually restoring from `world/<type>/old<type>s/` (which itself holds only
the single most recent pre-save snapshot, since step 2 overwrites it every save). Two builders
saving into the same zone-bucket file concurrently will clobber each other — there is no locking.
The Mudlle editor's `save_mudlle` (`shapemdl.cpp:127-182`) shells out `system("cp <f_from>
<f_old>")` for its backup step instead of the char-copy loop, but has the identical truncate-and-
rewrite live-file step and the identical single-generation-backup caveat.

### 6. Add path — new-vnum assignment

When a record's number is `-1` (freshly created via `new`/`create` and never yet saved), `save`
forwards to the type's "append" function (`append_proto`/`append_object`/`append_room`/
`append_zone`(n/a — zones can't be created)/`append_script`). All four implement the same
algorithm (confirmed directly for rooms, `append_room` `shaperom.cpp:1495-1602`):
1. Guard on `permission != 0`.
2. Backup `f_from` → `f_old` (same full-file char-copy as the replace path).
3. Reopen `f_old` `"r+"` (source) and `f_from` `"w+"` (target, truncated). Stream-copy every
   record up to (but not including) the terminal sentinel `#99999`, tracking `i1` = the last real
   record number seen.
4. `fseek(f2, -1, SEEK_CUR)` to back up over the just-written `#` of the `99999` sentinel, then
   call `write_<type>(f2, record, i1 + 1)` — **the new vnum is unconditionally `(highest existing
   vnum in the file) + 1`**, i.e. last-in-file plus one, not "first free slot" or "next in the
   containing zone's numeric range." Re-emit the `#99999` (and, for rooms, a `$~` room-file
   terminator) sentinel afterward.
5. Store the new vnum into the record and into `ch->specials.prompt_value`.

Because the new vnum is simply "append to this bucket file," a builder who wants a specific vnum
must instead `load` that (nonexistent) vnum directly (§4 step 4), which creates a blank record
*at that requested number* rather than at "last + 1" — the two creation paths (`create`/`new`
word-command vs. `load <specific-vnum>`) produce records with different vnum-assignment
semantics.

`append_object` has a **copy-paste bug**: when building the target filename from a bare
`add <filename>` argument (no record loaded yet), it resolves the path via `SHAPE_MOB_DIR`/
`SHAPE_MOB_BACKDIR` (`shapeobj.cpp:1583-1585`) — the *mobile* directory macros — instead of
`SHAPE_OBJ_DIR`/`SHAPE_OBJ_BACKDIR`. In practice this code path is rarely hit because objects are
normally created via `new`, which pre-resolves `f_from`/`f_old` correctly through the zone number,
so the buggy branch only fires if a builder explicitly runs `add <filename>` with no file already
selected. `append_room`'s equivalent branch (`shaperom.cpp:1510`) has its own bug: it calls
`sscanf("%s %s", str, fname)` against the **literal string** `"%s %s"` instead of the intended
`arg` variable, so the branch can never successfully parse a filename and always falls through to
"No file defined to write into."

### 7. Implement semantics

"Implement" patches the **in-memory** prototype/table so newly spawned mobs/objects/rooms/zone
resets/scripts/programs use the edited data immediately, without touching disk (that's `save`'s
job) and without restarting the server — except for genuinely new records, which cannot be
implemented until the next reboot re-reads the file from disk and assigns them a real number
(`rnum`).

| Editor | Implement function | Live effect | New-record behavior |
|---|---|---|---|
| Room | `implement_room` (`shaperom.cpp:208-317`) | `memcpy`-free field-by-field copy into `world[real_room(vnum)]`; deep-copies extra descriptions and affects, remaps `dir_option[].to_room` through `real_room()` (falls back to `NOWHERE` if unresolved) | `real_room(vnum) < 0` → "Maybe reboot will help", aborts |
| Mobile | `implement_proto` (`shapemob.cpp:1802-1860`) | `memcpy(proto, shape_buffer, sizeof(char_data))` wholesale, then re-`CREATE`s the four string fields | `proto->nr == -1` → "You can't implement fresh created mob. Wait for reboot." |
| Object | `implement_object` (`shapeobj.cpp:176-252`) | Field-by-field copy into `obj_proto[real_object(basenum)]`; ex_descriptions are deep-copied and **prepended** to the existing list rather than replacing it | `real_object(basenum) < 0` → "Maybe reboot will help" |
| Zone | `implement_zone` (`shapezon.cpp:369-507`) | Rebuilds `zone_table[adr]`'s name/description/map/owners/level/coords/lifespan/reset_mode and the entire `reset_com` command array from the builder's linked list; calls `renum_zone_one` to convert vnums to rnums | Zone must already exist in `zone_table` (zones can't be freshly created at all, §3.4) |
| Script | `implement_script` (`shapescript.cpp:509-564`) | Frees and rebuilds `script_table[index_pos]`'s command list in place — because entities reference scripts by a shared table index, **this takes effect immediately for every mob/object/room currently using that script** (no per-entity copy exists) | `index_pos == -1` (script did not exist at boot) → refuses, save-only |
| Mudlle | `implement_mudlle` (`shapemdl.cpp:184-206`) | `RELEASE`s and replaces `mobile_program[real_num]` with `mudlle_converter(txt)` — compiles the source right here; also immediate for every referencing mob | *intends* `real_num < 0` (new program) → "new program requires reboot", but **this guard is dead code** — see below |

**Confirmed bug: Mudlle's "new program" implement guard never fires.** `implement_mudlle`
(`shapemdl.cpp:198`) refuses only when `real_num < 0`. But `real_num` is never actually negative:
for a genuinely new program (`/load` hit the `#99999` sentinel), `load_mudlle`
(`shapemdl.cpp:82-84`) sets `real_num = 0` explicitly; for an existing-on-disk-but-not-yet-rebooted
program, `real_num = real_program(number)` (`shapemdl.cpp:100-102`), and `real_program()`
(`db.cpp:3037-3051`) returns **`0`**, not `-1`, when the vnum isn't found in the live
`mobile_program_zone[]` table (`db.cpp:3046-3048`). So `real_num` is always `>= 0`, the `< 0` check
never triggers, and typing `implement` on a brand-new, never-before-implemented Mudlle program
silently runs `RELEASE(mobile_program[0]); mobile_program[0] = mudlle_converter(txt);` —
**overwriting whichever program currently lives at index 0** instead of refusing and asking for a
reboot, as the send_to_char text at `shapemdl.cpp:199` claims it would.

**Important asymmetry**: room/mobile/object/zone "implement" only updates the **prototype**
table — live already-spawned instances in the world (a specific mob standing in a room, an
object already sitting in someone's inventory) are *not* retroactively updated; only *newly
spawned* copies (next zone reset, next `load_mob`/`load_obj` script/mudlle call, etc.) pick up
the change. Scripts and Mudlle programs are the exception, because those are referenced
indirectly through a shared index rather than copied per-instance, so their "implement" is
visible to already-existing entities too.

## RotS-specific notes

- Stock CircleMUD's OLC (OasisOLC-family: `redit`/`medit`/`oedit`/`zedit`) is a much later,
  separately-maintained addition to the CircleMUD ecosystem and is **not present in this
  codebase at all** — RotS instead has this bespoke "Shape" system, which predates and differs
  structurally from stock OLC conventions (no `.new`/`.mine` staging files, no `OLC_*` state
  macros, no `redit_disp_menu`-style dispatch tables). Anyone porting familiarity from stock
  CircleMUD OLC should not expect the same command vocabulary or save semantics.
- Shape is the **single umbrella** for six otherwise-unrelated content types, including two that
  stock Diku has no OLC concept of at all: Mudlle programs and `.scr` scripts (RotS-specific
  scripting/AI systems — see `docs/data-formats/mudlle-and-scripts.md`). It is one command
  (`do_shape`) with a switch on the first argument, not six separate commands.
- The **rooms-only restriction for sub-GOD builders** (`shapemob.cpp:1901-1904`) is a
  RotS-specific staff-tier policy: apprentice builders can shape rooms but nothing else, forcing
  mob/object/zone/script content changes through a smaller circle of GOD+ staff.
- The zone-ownership model (`get_permission`, owner lists, `LEVEL_AREAGOD`/`LEVEL_GRGOD`
  overrides, and the master-idnum bypass for mobiles/objects) has no stock-Diku analog; stock
  zones have no owner concept at all.
- `save`/`implement`/`done` being three independently invokable verbcs (rather than one "save"
  that always does both) is itself RotS's design — it lets a builder test a change live
  (`implement`) before persisting it (`save`), or persist without immediately activating a
  disruptive change (until the next reboot re-reads the file naturally).

## Worked example

A GOD-level builder wants to rename room vnum 3042 (a room whose zone is bucket `30`, i.e. lives
in `world/wld/30.wld`) from whatever it's currently called to `"The Sunlit Courtyard"`, then make
the change take effect immediately and persist it to disk.

```
> shape room 3042
You start shaping a room.
loading room #3042

> 1
Enter line NAME:
[the old room name]
```
Walking through the mechanics: `do_shape` matched `room` → `SHAPE_ROOMS`, allocated a
`shape_room` via `CREATE1(ch->temp, shape_room)`, set `procedure = SHAPE_EDIT`, and (since the
player did not type `new`) built the string `"load 3042"` and called
`shape_center_room(ch, "load 3042")` (`shapemob.cpp:1991-2018`, the `case SHAPE_ROOMS:` block).
Because `procedure == SHAPE_EDIT`
and `editflag == 0` but the first token `"load"` is not numeric, `shape_center_room` immediately
forwards to `extra_coms_room(ch, "load 3042")` (`shaperom.cpp:436-439`), whose `SHAPE_LOAD`
branch calls `load_room(ch, "load 3042")`. That function computes bucket `3042/100 = 30`, opens
`world/wld/30.wld` `"r+"`, records `f_from`/`f_old`, resolves `permission = get_permission(30,
ch)` (1, since a GOD is editing — assuming the zone is unowned or the builder is GRGOD+/an
owner), scans for `#3042`, and parses the record's name/description/flags/exits into the local
`shape_room` buffer. `SHAPE_ROOM_LOADED` is set and the loop returns to the player prompt.

Typing `1` sends `"1"` into `shape_center_room`: `editflag == 0`, first token `"1"` is numeric,
so `editflag = 1` (field 1 = name). The `switch` hits `case 1: LINECHANGE("NAME", room->name)`.
Because `SHAPE_DIGIT_ACTIVE` is not yet set, `LINECHANGE`'s first branch fires: it prints
`"Enter line NAME:\n[<current name>]"`, sits the player down at `POSITION_SHAPING` via
`shape_standup`, sets `SHAPE_DIGIT_ACTIVE`, and `return`s — the loop does not iterate further.

```
> The Sunlit Courtyard
Room #3042 changed.               (via subsequent /50 or implicit re-prompt)
```
The player's next line, `"The Sunlit Courtyard"`, re-enters `shape_center_room` with `editflag`
still `1` (unchanged since last call). This time `SHAPE_DIGIT_ACTIVE` **is** set, so
`LINECHANGE`'s second branch runs: `sscanf` extracts the token(s) into `str`/`arg`, the old
`room->name` buffer is `RELEASE`d, a new buffer of the right length is `CREATE`d, the text is
copied in, and each character is scanned for `#`→`+`/`~`→`-` sanitization (moot here, since the
name contains neither). `SHAPE_DIGIT_ACTIVE` is cleared, the player is stood back up,
`editflag` is reset to `0`, and (since `SHAPE_CHAIN` is not set — this is an ad-hoc edit, not part
of a creation sequence) the outer `do`-loop exits because `editflag == 0`.

```
> implement
Room implemented.

> save
Saved as room #3042
```
`implement` dispatches through `extra_coms_room` → `SHAPE_IMPLEMENT` → `implement_room(ch)`
(`shaperom.cpp:208-317`): it resolves `rnum = real_room(3042)`, and — assuming the room already
existed at boot (it does, since we loaded rather than created it) — `SUBST(name)` frees nothing
(the `SUBST` macro here just allocates+copies; freeing the old prototype string is not done, see
Open questions) and copies the new name into `world[rnum].name`. Any player currently standing in
room 3042 sees the new name on their next `look`; the change is **not yet on disk**.

`save` dispatches → `SHAPE_SAVE` → `replace_room(ch, "")` (`shaperom.cpp:1378-1494`): backs up
the entire `world/wld/30.wld` into `world/wld/oldroms/30.wld`, then truncates and rewrites
`world/wld/30.wld` from that backup, substituting the new name at the `#3042` record via
`write_room()`, leaving every other room in the bucket file untouched. From this point the new
name survives a reboot as well.

If the player instead typed `done`, the same two steps (`replace_room` then `implement_room`)
would run back-to-back, followed by `extra_coms_room(ch, "free")` to release the shaping session
in one command.

## Open questions

- **`shift` field**: `shape_proto`, `shape_object`, `shape_room`, and `shape_zone` all declare an
  `sh_int shift` member (`protos.h:87`, `:101`, `:117`, `:141`) documented in its own comment as
  "for editor, how much was typed already," but no code in any of the six `.cpp` files reads or
  writes it (confirmed by grep across all six files — zero hits for `->shift`). Left as dead
  struct padding, or a leftover from an earlier input-parsing design; not guessed at further.
- **Room affections (fields 18-20)**: disabled since "fingolfin, december 2001" with the working
  code commented out and marked "unstable." The commented-out code shows the intended shape
  (single-affection editing tied to `ROOMAFF_SPELL`/skill lookups), but since it never ran in
  the shipped binary it's unclear whether the load/save/implement round-trip of `F` lines was
  ever fully correct — `docs/data-formats/world-files.md` independently found no live `.wld`
  file uses `F` at all, so this cannot be cross-checked against real data.
- **`append_object`'s `SHAPE_MOB_DIR` bug and `append_room`'s literal-format-string `sscanf` bug**
  (§6): both look like copy-paste/typo errors rather than intentional behavior, but neither is
  exercised by the normal `new`-based creation flow (which pre-resolves the filename correctly),
  so it's unclear whether any live content was ever created through the buggy `add <filename>`
  path, or whether the bug is purely latent.
- **Zone reset-command comment persistence**: the `zone_tree` in-memory `comment` field is
  preserved by the Shape editor's own save/load round-trip, but it's not confirmed here whether
  the *canonical* boot-time `load_zones()` parser (documented in `world-files.md`) treats
  trailing-line text identically byte-for-byte, or whether Shape's writer could introduce subtle
  formatting differences (e.g. extra whitespace) that the boot parser tolerates differently than
  Shape's own reader. Not verified in this pass — would require a live round-trip test.
- **`implement_proto`/`implement_object` string-leak on implement** (noted in the mobile/object
  per-file analysis, not independently re-verified line-by-line here beyond the room editor):
  reported that the old prototype's string buffers are overwritten by a `memcpy`/field-copy
  without first freeing them, leaking one string set per `implement` call. Confirmed present for
  rooms too — `implement_room`'s `SUBST` macro (`shaperom.cpp:204-206`) unconditionally
  `CREATE`s+copies without a preceding `RELEASE`, and the commented-out "Windows-only" `SUBST`
  variant at `:193-203` shows a `RELEASE`-then-`CREATE` version once existed but was replaced.
  Whether this leak is large enough to matter in practice (builders `implement` far less often
  than the server runs) is a judgment call for a rewrite, not a correctness question with a
  single right answer.
- **Whether `MOB_NORECALC` is meant to gate the interactive `/48`/`recalculate` commands** as
  well as the mass `recalc_mobile` sweep: the source only checks the flag in the mass-sweep path
  (`shapemob.cpp:2119-2124`); the single-mob interactive recalculate paths do not check it at
  all. This could be intentional (an individual builder recalculating one mob on purpose should
  override the "don't touch me" flag) or an oversight — left unresolved rather than guessed.
- **`get_permission`'s internal side-effect writes through the wrong struct type.** Independent of
  its `return perm;` value (which is what every caller actually uses), `get_permission`
  (`shapemob.cpp:167-201`) also does `SHAPE_PROTO(ch)->permission = ...` directly inside the
  "unowned zone" branch (`:179-185`) — hardcoded to the `shape_proto` cast regardless of which
  editor is actually active. `get_permission` is called from all six editors (confirmed by grep:
  `shaperom.cpp:1152`, `shapeobj.cpp:1086`/`:1826`, `shapezon.cpp:1254`/`:1278`,
  `shapescript.cpp:255`/`:1102`, `shapemdl.cpp:115`), and the six per-editor structs are laid out
  differently (`shape_room`'s `permission` is a 2-byte `sh_int` at a different offset than
  `shape_proto`'s 4-byte `int permission`). A standalone offset check under a 32-bit compile
  (`clang --target=i386-pc-linux-gnu`, matching this repo's mandatory `-m32` target) shows
  `shape_proto` is 192 bytes with `permission` at offset 188 (a 4-byte write ending exactly at byte
  192), while `shape_room` is only 188 bytes with `permission` at offset 186 — so the same 4-byte
  write, applied to a buffer actually allocated as `shape_room` (via `CREATE1`, which allocates
  `sizeof(type) + 1` bytes, i.e. 189), would land 3 bytes past the end of the allocation. This is a
  latent heap-adjacent write whenever a non-mobile editor calls `get_permission` on an unowned
  zone — extremely common, since the room editor is what non-GOD builders are restricted to. It is
  **not confirmed to cause observable corruption in practice**: real allocators typically round
  small requests up to an internal size class, which may absorb the overrun silently. Flagged here
  rather than asserted as an active crash bug — would need an ASan/valgrind run against the live
  32-bit binary to confirm impact.
