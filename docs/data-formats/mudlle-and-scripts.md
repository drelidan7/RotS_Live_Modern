# Mudlle programs (`.mdl`) & scripts (`.scr`)

**Source files:** `src/db.cpp` (`load_mudlle:3047`, `boot_mudlle:3074`, `load_scripts:1022`),
`src/mudlle.cpp`/`mudlle2.cpp` (interpreter), `src/shapescript.cpp` (`.scr` OLC editor);
structs `script_head`/`script_data` (`protos.h:175-192`), `special_list` (`mudlle.h:36`)
**Status:** 🟢 on-disk formats verified against the real `.mdl`/`.scr` data. The script
**command-type** enum is enumerated in `src/script.h` (118 defines — see *Open questions*); the
Mudlle **language** opcodes are catalogued in `text/mudlle.keys`. The full stack model and each
opcode's exact param/text contract still belong in a systems doc.

## Overview
RotS has **two** scripting systems for NPC/room behavior, loaded from separate world
directories:
- **Mudlle** (`world/mdl`, `MDL_PREFIX`) — the older custom mob-AI language, stored as
  **source text** and compiled to an internal form at boot. Documented for builders by
  `MAN ASIMA` (`text/mudl_tbl`). The runtime is a stack machine (`special_list` in
  `mudlle.h`, executed in `mudlle*.cpp`).
- **Scripts** (`world/scr`, `SCR_PREFIX`) — a newer, **structured** command-list format
  edited via the `shapescript` OLC. Each command is a fixed row of integers plus a text
  field.

Both load before rooms/mobs (`boot_db:330,343`). Mobiles reference them by number
(`store_prog_number`, `script_number` in the `.mob` format — see `world-files.md`).

Each directory is loaded via an **index** file — one filename per line, `$`-terminated — that
lists the data files to read (`index_boot:705-742`). The default name is `index`; mini-mud mode
(`-m`) reads `index.min` and new-mud mode reads `index.new` (`index_boot:639-645`,
`db.h:30-32`). In the current data, `world/mdl/index` and `world/mdl/index.min` are byte-for-byte
identical. Data files are conventionally named `<vnum>.mdl` / `<vnum>.scr`, but the index may
name any file.

---

## Mudlle programs (`.mdl`) — `load_mudlle:3047`
```
#<zone>  <optional comment — ignored>
<mudlle source line>
<mudlle source line>
...
#<zone>
<mudlle source ...>
#99999
```
Parsing rules:
- Each program begins with a header line `#<n>`, where `n` is parsed from the char after
  `#` and stored as the program's **zone** (`mobile_program_zone[]`, `:3057,3062`). Any text
  after the number is a free-form comment — `sscanf(tmpstr+1, "%d", …)` stops at the number, so
  real programs use it as a label (e.g., `#1102  Stone statue/golem`).
- The body is all following lines until the next line whose first non-blank char is `#`
  (`:3063-3069`). The body text is stored raw in `mobile_program[]`.
- The list terminates at `#99999` (`:3059`).
- At boot, `boot_mudlle` (`:3074`) replaces each raw program with the output of
  `mudlle_converter()` — i.e., **the on-disk format is Mudlle source; compilation happens
  at load**. Programs are 1-indexed (`num_of_programs`).

> The Mudlle source grammar itself (commands, operators, the stack model behind
> `special_list` / `SPECIAL_STACKLEN`) is a language spec belonging in a dedicated
> **systems/mudlle-engine** doc — not captured here. Two references survive in `text/`:
> `mudl_tbl` (`MAN ASIMA`, the narrative builder guide) and `mudlle.keys`, a low-level
> cheat-sheet listing every opcode letter — the main body, the service `S` subcommands, and the
> `v`/`V`/`f` to-stack / from-stack / to-list groups — each with one-line semantics.

---

## Scripts (`.scr`) — `load_scripts:1022`
```
#<vnum> <name>~          <- name follows the vnum on the SAME line
<description>~
<command_type> <number> <p0> <p1> <p2> <p3> <p4> <p5>
<text>~
<command_type> <number> <p0> <p1> <p2> <p3> <p4> <p5>
<text>~
...
999 0 0 0 0 0 0          <- a row whose first int is 999 ends this command list (7 ints, no text)
#<vnum> <name>~
...
#99999                   <- end of file (also: a name string beginning with '$')
```
Per record (`load_scripts:1028-1088`):
- `#<vnum>` header **followed on the same line** by the `name` tilde-string (e.g.,
  `#1100 Thranduil's Herald~`): `fscanf("#%d\n", …)` consumes the number and the whitespace
  after it, then `fread_string` reads the rest as `name` (`:1036,1042`). `#99999` ends the file
  (`:1038`); a `name` beginning with `$` is also EOF (`:1044`).
- `description` (tilde string) — *saved back* to the file by the OLC (`script_head` comment).
- Then a **command list**, each command = **eight integers** followed by a **tilde text**
  string:
  | Int | `script_data` field | Meaning |
  |-----|---------------------|---------|
  | 1 | `command_type` | opcode (enum in `script.h`) |
  | 2 | `number` | command's ordinal/number |
  | 3–8 | `param[0..5]` | command parameters (often references to char_script variables) |
  | (string) | `text` | text payload — say/echo text, or a comment |
- A command row whose **first int is `999`** terminates the list (`:1057`). The terminator row
  has only **seven** integers (`999 0 0 0 0 0 0`) and no trailing text string — command rows
  have eight, but the loader's `fscanf` only tests the first value before breaking, so the
  shorter row parses fine.

### Structs
`script_head` (`protos.h:175`): `number`, `name`, `virt_num`, `description`, `host`
(indicates whether the script targets char/obj/room data), `script` (first command).
`script_data` (`protos.h:184`): doubly-linked command node — `room`, `number`,
`command_type`, `param[6]`, `text`.

## RotS-specific notes
- Both systems are RotS extensions (stock DikuMUD has only hardcoded C special procedures).
- The `shape*` OLC suite edits these live (`shapescript.cpp` for `.scr`,
  `shapemdl.cpp` for `.mdl`) and writes them back to the world files.
- `world/scr/n.scr` is an OLC scratch/template file (100 empty placeholder records with
  non-numeric vnums `#$$00`…`#$$99`). It is **not** listed in `scr/index`, is never booted, and
  would in fact misparse under `load_scripts` (`fscanf("#%d\n")` expects a number) — treat it as
  editor working state, not loadable data.

## Open questions
- **`command_type` semantics.** The enum *itself* is now pinned to `src/script.h` (118 defines):
  trigger codes such as `ON_ENTER 11`, `ON_DIE 14`, `ON_RECEIVE 15`, `ON_HEAR_SAY 17`, and
  command codes such as `SCRIPT_IF_INT_EQUAL 4`, `SCRIPT_DO_SAY 30`, `SCRIPT_LOAD_MOB 39`. The
  human-readable catalogue is `text/scr_tbl` (`COMMAND LIST` / `TRIGGER LIST`). What remains for
  a systems doc is each opcode's exact param/text contract.
- **Mudlle language spec** — grammar, builtins, the `special_list` stack discipline, and how
  `mudlle_converter` compiles source (→ systems/mudlle-engine doc). The opcode letters are
  catalogued in `text/mudlle.keys`.
- How `script_head.host` selects char/obj/room context, and how scripts are triggered at
  runtime.
