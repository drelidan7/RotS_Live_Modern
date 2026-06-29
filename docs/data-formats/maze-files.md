# Maze / level files (`.lev` and `.maz`)

**Source files:** `levgen/maz.c` (`MAZloadLevel:381`, `MAZreadNextHall:18`,
`MAZmakeLevel:~330`, `MAZwriteLevel:369`), `levgen/maz.h`, `levgen/structs.h`,
`levgen/levgen.c`, `levgen/config.c`
**Status:** ✅ format complete. Connection-code semantics & assembly algorithm noted as open.

This doc covers **two related-but-distinct formats**, both **offline `levgen` input** — neither
is read by the game server at runtime (there is no `MAZ_PREFIX` / `DB_BOOT_MAZ` in `src/`):
- **`levgen/*.lev`** — full maze **blueprints**: grid size, vnum range, source/output paths,
  external connections, **and** hall definitions, in one file. Detailed in *Purpose* and
  *File structure* below.
- **`lib/world/maz/*.maz`** — hall **libraries**: hall definitions **only** (no properties or
  connection sections, no `~` separators). Detailed in *`.maz` hall-library files* below.

## Purpose
`levgen` is a **standalone C tool** (built separately — `cd levgen && make`) that
procedurally generates maze zones. A `.lev` file is its **input**: it describes maze
building blocks ("halls" — Tetris-like clusters of rooms) plus placement rules. The tool
randomly assembles them on a grid and **writes standard `.wld` and `.zon` world files**,
which the game server then loads through the normal world-file path (see `world-files.md`).

> So `.lev` is a **build-time** format consumed by `levgen`, **not** read by the game
> server. The mazes shipped in `levgen/*.lev` (Mirk, Orcish, Gwalin, …) regenerate the
> dungeon zones (e.g. zone 280) on demand. Samples exist in `levgen/` even though the main
> world data is missing — useful for validating this spec.

## File structure (`MAZloadLevel:381`)
Three sections separated by lines beginning with `~`:

### 1. Properties (key=value) — skipped by `MAZloadLevel`, parsed by the config reader
```
size.x=10                 grid width
size.y=10                 grid height
room.base=28000           first vnum for generated rooms
room.max=99               max rooms / vnum span
zone.name=Mirk Maze 280
zone.symbol=*
zone.x=13
zone.y=7
zone.nr=280               generated zone number
zone.lifespan=10
source.wld=../lib/world/wld/266.wld   template world file (room style to clone)
source.zon=../lib/world/zon/266.zon   template zone file
output.wld=../lib/world/wld/280.wld   where the generated .wld is written
output.zon=../lib/world/zon/280.zon   where the generated .zon is written
~
```
(`MAZloadLevel` fast-forwards past this section to the first `~`, `:393-405`; the keys are
read by the config parser in `levgen.c`/`config.c`.)

### 2. Connection / seed section (`MAZloadLevel:406-436`)
Line-leading token determines meaning; a coordinate of `*` means "pick at random":
| Token | Syntax | Meaning |
|-------|--------|---------|
| `@` | `@<x> <y> <exnum> <dir>` | **external connection**: grid cell (x,y) links out to existing world room vnum `<exnum>` in direction `<dir>` (stitches the maze into the surrounding world) |
| `!` | `!<x> <y>` | reserve cell (x,y) to stay **empty** (`Reserved`); `*` = random |
| `s` | `s<x> <y> <c>` | place map **symbol** `c` at cell (x,y); `*` = random |
Section ends at the next `~`.

### 3. Hall definitions (`MAZreadNextHall:18`)
```
#<hall name>
<rnum> <rx> <ry> <cN> <cE> <cS> <cW> [<symbol>]
<rnum> <rx> <ry> <cN> <cE> <cS> <cW> [<symbol>]
...

#<next hall name>
...
```
- Each hall starts with a `#<name>` header line; rooms follow until the next `#`.
- Each **room row** = 7 ints + optional symbol char (`:45`):
  `rnum` (room vnum), `rx`/`ry` (position **relative** to the hall), and four connection
  codes in order **N, E, S, W** (`cn[NORTH..WEST]`). A trailing non-numeric token sets that
  room's map `symbol`. A row with fewer than 7 numbers ends the hall.
- Up/Down exits are always cleared for maze rooms (`MAZregisterRoom:58`).
- Each hall is normalized so its first room sits at relative (0,0) (`MAZreorderHall`).

## Output
`MAZmakeLevel`/`MAZwriteLevel` emit ordinary world files: the `.zon` ends with the standard
`S\n#99999\n$~\n` (zone-command terminator + file terminator) and the `.wld` ends with
`#99999\n\r$~\n\r` (`maz.c:355,377`) — i.e. the generated files conform to `world-files.md`.

## `.maz` hall-library files (`lib/world/maz/`)
The runtime tree ships **hall-library** files under `lib/world/maz/`: `0.maz`, `3.maz`, `4.maz`,
`5.maz`, plus `index` and `index.min`. Like `.lev`, these are **offline `levgen` input, NOT
loaded by the game server at runtime** — no `MAZ_PREFIX` / `DB_BOOT_MAZ` exists in `src/`, and
no `fopen` in `src/` references them. They are ASCII text with **CRLF** line endings.

A `.maz` file is a **hall library only** — *just* the hall-definition section of a `.lev` file
(section 3 above), with **no** properties section, **no** connection/seed section, and therefore
**no `~` separators**. It is a flat list of halls ending in a sentinel:

```
#<N1> <N2> <hall name>
<rnum> <rx> <ry> <cN> <cE> <cS> <cW> [<symbol>]
...
#<N1> <N2> <next hall name>
...
#0
```

- **Hall header** = `#<N1> <N2> <hall name>` — note the **two leading integers** (e.g.
  `#10 0 Training area 1`, `#40 2 Hummerhorn Cavern`, `#15 2 Moldering Rooms`).
  `MAZreadNextHall` consumes the whole `#` line as a delimiter and **never parses those two
  numbers** (`maz.c:26-48`); their meaning is unrecorded (plausibly placement weight /
  orientation hints — see open questions). The `.lev` halls use bare `#<name>` headers instead.
- **Room rows** are the **same 7-int format** as `.lev`: `rnum rx ry cN cE cS cW [symbol]`
  (`maz.c:45`).
- The file ends with a bare **`#0` sentinel** — a `#` header with no following room rows, which
  makes `MAZreadNextHall` stop at EOF.
- Halls draw room vnums from **multiple existing zones** (e.g. `0.maz` references 17xx and 18xx
  rooms), mirroring the `.lev` `source.wld` room-cloning approach.

### `index` / `index.min`
Plain text, **one filename per line, terminated by a `$` line** — the same convention as the
`world/*/index` files consumed by `index_boot` (`db.cpp:648-671`). `index` lists all four maze
files (`0.maz`, `3.maz`, `4.maz`, `5.maz`); `index.min` lists only `0.maz` (the mini set,
mirroring `MINDEX_FILE`). No `src/` code currently boots these directories — they follow the
world-index convention but are inert at runtime.

## RotS-specific notes
- Procedural dungeon generation is a RotS feature; stock DikuMUD has only hand-built zones.
- Generated rooms clone the look/sector of the `source.wld` template zone.
- **`mortal_maze_room[]` (`db.h:94`) is a rent-rescue map, not a runtime generator.** When a
  player quits/rents inside a maze zone that is later regenerated (new room vnums), the saved
  login room no longer exists. On load, `objsave.cpp:606-613` matches the saved room's zone
  (`old_room / 100`) against `mortal_maze_room[i][0] / 100` and relocates the player to the
  safe vnum `mortal_maze_room[i][1]` (e.g. 235xx/236xx/237xx → 10700; 231xx/232xx/233xx →
  6330). The server never regenerates mazes itself; `levgen` runs offline and its `.wld`/`.zon`
  output loads through the normal world path.

## Open questions
- **Connection codes** `cN/cE/cS/cW` exact meaning (0 = no exit; nonzero values appear to
  distinguish internal-hall links vs. hall-edge connectors vs. forced openings) — confirm in
  the grid-assembly logic (`MAZmakeLevel` + `MAZregisterHall`).
- The **assembly algorithm**: how halls are chosen/placed/rotated and how `room.base`/
  `room.max` bound vnum allocation.
- Exact **config key parser** (`levgen.c`/`config.c`) and the full key list.
- The two leading integers in `.maz` hall headers (`#<N1> <N2> <name>`) — read past by
  `MAZreadNextHall`, so their meaning (weight? orientation? legacy field?) is unconfirmed.
