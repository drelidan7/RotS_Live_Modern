# Player save files

**Source files:** `src/db.cpp` (`save_player:2302`, `load_player:1736`,
`char_to_store:2099`, `store_to_char:1995`, `save_char:2475`, `load_char:1979`);
`struct char_file_u` (`structs.h:1783`)
**Status:** ✅ verified against real player files in `lib/players/`. Write format, load
parser, `SAVE_VERSION`, and the colormask migration are all confirmed below.

## Purpose
Each player character is persisted to its **own text file** under `lib/players/`. The
current format is line-oriented `key value` text under a `#player` section — **not** a
binary struct dump. (`struct char_file_u` still exists as the in-memory marshalling
buffer that `char_to_store` fills before writing, but it is no longer the on-disk layout.)

> ⚠️ The Explore-phase note that players are a "binary struct dump of `char_file_u`" was
> **wrong** — that's the legacy DikuMUD format. `save_player` writes text.

## File location & naming (`save_player:2311-2472`)
The character name is lowercased and bucketed by first letter into a subdirectory:

| First letter | Directory |
|--------------|-----------|
| a–e | `players/A-E/` |
| f–j | `players/F-J/` |
| k–o | `players/K-O/` |
| p–t | `players/P-T/` |
| u–z | `players/U-Z/` |
| other | `players/ZZZ/` |

The filename **encodes index metadata** as dotted suffixes:
```
players/<bucket>/<name>.<level>.<race>.<idnum>.<log_time>.<flags>
```
Save procedure: write to `players/temp`, then `rm <bucket>/<name>.*` and `cp` temp to the
suffixed filename (via `system()` calls — `db.cpp:2469-2476`). So the canonical file is found
by glob `<name>.*`, and level/race/idnum/last-logon/flags are readable without opening it. The
five suffix fields are pulled straight from the in-memory `player_table` entry, and `<flags>`
is the player's act-bitvector (`PLR_FLAGS` = `specials2.act`), so the filename's last field
equals the `act` line inside the file.

**`ZZZ` has a second role — the deleted-character graveyard.** Besides names starting with a
non-`a–z` character, `move_char_deleted` (`db.cpp:2274`) does `mv <ch_file> players/ZZZ/<name>`,
so deleted players land in `ZZZ` under their **bare name with no dotted suffix** (e.g.
`players/ZZZ/adom`). A directory scan of `ZZZ` therefore mixes (a) live non-`a–z`-named
characters and (b) bare-named tombstones of deleted ones.

**Legacy `#name#` filename variant.** Some imported files have the name wrapped in `#`
(e.g. `#khronos.96.0.42519.1161734537.2113600#`). No current code path produces this — the `#`
lives only in the filename and the contents are ordinary `#player` text. Treat these as legacy
artifacts when globbing/scanning the directory.

## File body format (`save_player:2364-2461`)
```
#player
version     <SAVE_VERSION>
name        <name>
sex         <int>
prof        <int>
race        <int>
bodytype    <int>
level       <int>
language    <int>
birth       <unix_time>
played      <seconds>
weight      <int>
height      <int>
title       <string to EOL>
hometown    <int>
description 
<multi-line text>
~
last_logon  <unix_time>
password    <encrypted>        (see Encryption below)
host        <string>
idnum       <long>
load_room   <int>
sp_to_learn <int>
alignment   <int>              (-1000..+1000)
act         <long bitvector>
pref        <long bitvector>
wimpy       <int>
freeze_lvl  <int>
bad_pws     <int>
conditions0 <int>              (drunk)
conditions1 <int>              (full)
conditions2 <int>              (thirst)
mini_lvl    <int>
morale      <int>
owner       <int>
rerolls     <int>
max_mini_lv <int>
perception  <int>
rp_flag     <int>
retiredon   <int>
ob          <int>
damage      <int>
ENE_regen   <int>
parry       <int>
dodge       <int>
gold        <int>
exp         <int>
encumb      <int>
spec        <int>              (active specialization)
```
Then **repeated / indexed** lines (only nonzero entries are written, except where noted):
```
color       <idx> <val>        per non-default color field, idx 0..MAX_COLOR_FIELDS-1 (16) [val != CNRM only]
talks       <idx> <val>        languages spoken, idx 0..MAX_TOUNGE-1 (3)   [all written]
skills      <idx> <val>        skill proficiency, idx 0..MAX_SKILLS-1 (256) [nonzero only]
affect      <slot> <type> <duration> <modifier> <location> <bitvector>     [duration!=0 only]
bodyparts   <idx> <val>        per-bodypart hp, idx 0..MAX_BODYPARTS-1 (11) [all written]
tmpstats    <str> <lea> <intel> <wil> <dex> <con>     (current abilities)
tmpabil     <hit> <mana> <move> <spirit>              (current hp/mana/move + spirit)
permstats   <str> <lea> <intel> <wil> <dex> <con>     (permanent/rolled abilities)
permabil    <hit> <mana> <move> 0                      (permanent maxima; 4th always 0)
prof_coef   <idx> <val>        proficiency %, idx 0..MAX_PROFS (5)
prof_level  <idx> <val>        level per profession, idx 0..MAX_PROFS (5)
prof_exp    <idx> <val>        exp per profession,  idx 0..MAX_PROFS (5); <val> is long (%ld)
end
```
The `end` token closes the record. `#player` is the only section today; the comment at
`:2364` notes more `#`-sections may be added later.

Note the ability-stat field order is **str, lea, intel, wil, dex, con** (matches
`char_ability_data` at `structs.h:1037`, whose declaration order is str/lea/intel/wil/dex/con).

The `description` key is written **with a trailing space** (`fprintf(... "description \n%s~\n")`):
the closing `~` always sits on its **own line**, and an empty description is just
`description ` / newline / `~`. The loader nulls the first space in the key, so the trailing
space is harmless on read — but raw line readers should expect it.

⚠️ Real player files have **mixed line endings** — some LF-only, some CRLF. `load_player`'s
hand-rolled line reader advances a fixed two bytes past each `\n`, so CRLF files leave a stray
`\r` at the head of the parsed value; trim trailing `\r` defensively when reading these files.

## Encryption (`save_player:2381-2383`)
The password is copied (`MAX_PWD_LENGTH = 10`) and run through `encrypt_line()` before
writing. This is a simple reversible obfuscation (paired with `decrypt_line()`), **not** a
cryptographic hash — treat stored passwords as plaintext-equivalent for security purposes.

## Key size constants (`structs.h`)
`MAX_NAME_LENGTH=12`, `MAX_PWD_LENGTH=10`, `HOST_LEN=30`, `MAX_TOUNGE=3`, `MAX_SKILLS=256`,
`MAX_AFFECT=32`, `MAX_BODYPARTS=11`, `MAX_PROFS=4` (arrays sized `MAX_PROFS+1`),
`MAX_COLOR_FIELDS=16` (`color.h`).

## RotS-specific notes
- Per-character files with metadata-encoded filenames (vs. DikuMUD's single binary
  `players` file) are a RotS design.
- `affect` lines persist active spell/skill effects across logout.
- The many RotS combat/perception fields (`ob/damage/parry/dodge/ENE_regen/encumb/
  perception/rp_flag/morale/mini_lvl`) are saved inline.

## Versioning & migration
- **`SAVE_VERSION` is `1`** (`db.h:89`). It is written as `version 1` but the loader
  **ignores it** — `load_player` has `case 'V': break;` with no version branching. No real
  file carries any other value.
- The **only** migration is the color conversion. `load_char` (`db.cpp:1995`) unconditionally
  calls `convert_old_colormask` (`color.cpp:75`): *if* a legacy packed `color_mask` key is
  present it is unpacked into the per-field array (`colors[i] = color_mask >> (i*3) & 7`, 10
  fields); otherwise it is a no-op. Current files carry per-field `color` lines and no
  `color_mask`, so the conversion does nothing for them.

## Player index (in-memory only)
There is **no on-disk player index file**. `player_table` is rebuilt in memory at boot by
scanning the bucket directories; each entry's `ch_file` (`player_index_element`, `db.h:184`)
caches the full path so the file can be reopened without re-globbing. The dotted-suffix
filename fields are written from this same in-memory table at save time.

## Open questions
- **Load parser** — confirmed tolerant: unmatched keys fall through the `switch (line[0])`
  silently, and missing keys keep their zero-initialised struct value (`memset` at entry). No
  further action needed; the writer remains authoritative for the format.
- Whether legacy binary `char_file_u` files still need a one-time import path (likely moot —
  no binary player data exists in this fork; all sampled files are text).
