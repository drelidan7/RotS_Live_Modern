# Mudlle & script execution engine

**Source files:** `src/mudlle.h` (stack/list macros), `src/mudlle.cpp` (`SPECIAL(intelligent)` —
the mudlle interpreter — `:655-1196`; `mudlle_converter` — the mudlle compiler — `:1202-1336`),
`src/mudlle2.cpp` (stack/list primitives: `TO_STACK`, `FROM_STACK`, `TO_LIST`, `REMOVE_LIST`,
`compare_list`, …), `src/script.h` (the `command_type` `#define` constants — 118 defines, no C++
`enum`), `src/script.cpp`
(`run_script:715-1525` — the `.scr` executor; `call_trigger:601-664` and the `trigger_*` functions
— the trigger catalogue), `src/interpre.cpp` (`activate_char_special:1556`,
`activate_obj_special:1589`, `special:1605` — the callflag dispatcher used by mudlle and by
hardcoded C specials), `src/protos.h` (`info_script:164`, `script_head:175`, `script_data:184`),
`src/structs.h` (`char_special_data:1090-1173` — the mudlle runtime fields overlaid on
`char_data`), `src/spec_ass.cpp` (`virt_program_number:309`, `virt_obj_program_number:461` — a
**third**, unrelated dispatch mechanism, see RotS-specific notes), `src/comm.cpp`
(`complete_delay:2084` — the live wait/resume driver), `src/utils.h`
(`WAIT_STATE_BRIEF`/`WAIT_STATE_FULL` macros, `CALL_MASK`).

**Status:** 🟡 partial — the mudlle opcode set, the script command-execution loop, the full
trigger/dispatch catalogue, and one worked trace per engine (`.scr` and `.mdl`) are verified line
by line against the source and against live `lib/world/scr` and `lib/world/mdl` data. Not
independently verified: the runtime behavior was traced by hand-simulating the code, not by
attaching a debugger to a running server. See **Open questions**.

This doc covers the **execution engine** only. The on-disk `.mdl`/`.scr` file grammar, the
`script_head`/`script_data` field layout, and the loader (`load_mudlle`, `load_scripts`) are
already documented in
[`../data-formats/mudlle-and-scripts.md`](../data-formats/mudlle-and-scripts.md) — this doc does
not repeat that material and links to it instead.

## Purpose

RotS has two independent, non-interoperating scripting systems for NPC/room/object behavior, both
predating and unrelated to stock CircleMUD/DikuMUD's hardcoded C "special procedures":

- **Mudlle** — a tiny custom **stack/list bytecode machine**. Mudlle "source" (a single-letter
  opcode stream, occasionally with inline decimal literals and backtick-quoted text) is compiled at
  boot by `mudlle_converter` (`mudlle.cpp:1202`) into a second, address-resolved string, then
  interpreted opcode-by-opcode by `SPECIAL(intelligent)` (`mudlle.cpp:655`) every time the engine is
  invoked. Mudlle programs are attached only to **mobiles** (characters) — never to objects or
  rooms directly (see Data structures) — via `char_data::specials.union1.prog_number`.
- **Scripts** (`.scr`) — a **structured, tree-walking command-list interpreter**. Each command is a
  fixed 8-int-plus-text row (`script_data`, see the data-formats doc); `run_script`
  (`script.cpp:715`) walks the linked list, branching on `IF_*`/`BEGIN`/`END`/`END_ELSE_BEGIN`
  nodes. Scripts attach to characters, objects, *and* — nominally — rooms (rooms have no working
  trigger path today; see Format/Algorithm).

The two systems do **not** share an interpreter, a variable space, or a trigger catalogue. Mudlle
is reached through the same generic `callflag`-based "special procedure" call path
(`special()`/`activate_char_special()` in `interpre.cpp`) used by hardcoded C specials; scripts are
reached through a separate, purpose-built `call_trigger()`/`char_has_script()` path
(`script.cpp:601,577`) keyed on a fixed set of `ON_*` trigger codes. A given mob or object can carry
*both* a mudlle program (`specials.store_prog_number`/`union1.prog_number`) and a script
(`specials.script_number` / `obj_flags.script_number`) at once — they run independently and cannot
call each other.

## Data structures

### Mudlle runtime state (per character, on `char_data::specials`)

Mudlle has **no separate "script instance" struct** — its entire runtime state (stack, list,
program counter, call stack, and even the enable/disable mask) is packed directly into fields of
`char_special_data` (`structs.h:1090-1173`) that stock CircleMUD/DikuMUD uses for something else
entirely. This repurposing is the single most important thing to know before touching this code:

| Field (`char_special_data`, `structs.h`) | Stock DikuMUD meaning (comment in source) | Mudlle meaning | Accessor macro (`mudlle.h`) |
|---|---|---|---|
| `char* poofIn` | "Description on arrival of a god" | pointer to a `long[SPECIAL_STACKLEN]` — the **numeric stack** | `SPECIAL_STACK(ch)` (`mudlle.h:21`) |
| `char* poofOut` | "Description upon a god's exit" | pointer to a `struct special_list` — the **item list** | `SPECIAL_LIST_AREA(ch)` (`mudlle.h:46`) |
| `int invis_level` | "level of invisibility" | the numeric stack's fill pointer | `SPECIAL_STACKPOINT(ch)` (`mudlle.h:24`) |
| `union1.prog_number` (`int*`) | `reply_ptr` (alt. union member) | array of up to `SPECIAL_CALLLIST` (10) program indices — one per subroutine-call nesting level | `PROG_NUMBER(ch)` = `union1.prog_number[tactics]` (`mudlle.h:23`) |
| `union2.prog_point` (`int*`) | `reply_number` (alt. union member) | array of up to 10 program-counter values, one per nesting level | `PROG_POINT(ch)` = `union2.prog_point[tactics]` (`mudlle.h:22`) |
| `ubyte tactics` | "combat tactics of a person" | current subroutine nesting depth (0..`SPECIAL_CALLLIST-1`), indexes the two arrays above | — |
| `int store_prog_number` | doc'd as "in database, stores prog_numbers for mobiles" | **overloaded**: mudlle program index *or* an index into `virt_program_number`'s hardcoded-C table, depending on the `MOB_SPEC` flag — see RotS-specific notes | — |

`specials2.bad_pws` (stock meaning: "number of bad password attempts") is reused as the mudlle
**enable mask** via the `CALL_MASK(ch)` macro (`utils.h:479`) — see Format/Algorithm.

`special_list` (`mudlle.h:36`) is a fixed-size (`SPECIAL_STACKLEN` = 50, `mudlle.h:20`) singly
linked free-list/active-list hybrid: `field[SPECIAL_STACKLEN]` holds `target_data` entries
(`structs.h:330`: a `signed char type` tag — one of the `TARGET_*` constants, `structs.h:315-326` —
plus a `ptr` union of `char_data*`/`obj_data*`/`room_data*`/`txt_block*`, an `ch_num` safety number,
and a `cleanup()`/`operator==`), and `next[SPECIAL_STACKLEN]` links entries into the active
"current list" chain starting at `head`. `SPECIAL_MARK` (`mudlle.h:10`, value `-2`) is a sentinel
entry (slot 0) that can never be removed (`REMOVE_LIST` on it calls `UP_LIST` instead,
`mudlle2.cpp:175-178`) — it anchors the list so "move up past the last real element" is well
defined. `SPECIAL_VOID` (`-1`) marks a free slot.

Text values pushed to the list are heap-allocated `txt_block`s obtained from a pool
(`get_from_txt_block_pool`/`put_to_txt_block_pool`) — every `REMOVE_LIST` on a `TARGET_TEXT` entry
releases it back to the pool (`mudlle2.cpp:180-181`), so text lifetime is tied 1:1 to list
occupancy, not to a variable.

Objects and rooms have **no** equivalent poofIn/poofOut/stack/list fields — only `char_data` does.
`obj_flag_data::prog_number` (`structs.h:419`, a single plain `int`, no array/stack) is a
completely separate, unrelated field used only by `virt_obj_program_number` (see RotS-specific
notes), not by the mudlle interpreter.

### Script runtime state

- `info_script` (`protos.h:164`) — the **shared** per-firing execution context for `.scr`, attached
  once per owning entity via `char_data::specials.script_info` (`structs.h:1132`) or
  `obj_data::obj_flags.script_info` (`structs.h:421`), and re-initialized (mostly wiped) on every
  trigger firing (see Format/Algorithm → "Variable lifetime"):
  - `int index` — position of this entity's program in `script_table[]` (used only for error logs).
  - `script_data* next_command` — saved resume point, set only by `SCRIPT_DO_WAIT`
    (`script.cpp:952-959`) and consumed by `continue_char_script` (`script.cpp:1544`).
  - `int str_dynamic[3]`, `char* str[3]` — the three `strX` text variables (`SCRIPT_PARAM_STR1..3`).
  - `char_data* ch[3]`, `obj_data* ob[3]`, `room_data* rm[3]` — the `chX`/`obX`/`rmX` object-handle
    variables (`SCRIPT_PARAM_CH1..3`/`OB1..3`/`RM1..3`).
  - `int ints[3]` — the `intX` integer variables (`SCRIPT_PARAM_INT1..3`).
- `script_head`/`script_data` (`protos.h:175`, `184`) — covered in the data-formats doc; the only
  field relevant to execution beyond what that doc says is `script_data::command_type`, which is
  the dispatch key in `run_script`'s big `switch`.
- `waiting_type` (`structs.h:363`, holds `targ1`/`targ2` `target_data` and a `char_data* next`) —
  the shared delay/resume record embedded as `char_data::delay`, used by **both** engines' "pause
  and resume next pulse" mechanism (see Format/Algorithm → Waiting).

## Format / Algorithm

### 1. Mudlle: compilation (`mudlle_converter`, `mudlle.cpp:1202`)

Mudlle "source" (the raw text loaded by `load_mudlle`, see the data-formats doc) is not executed
directly — `boot_mudlle` (`db.cpp:3074`) runs every program through `mudlle_converter` once at boot
and replaces `mobile_program[i]` with the result. The converter does two passes over the source
(first to compute the compiled length and record label positions, second to emit the compiled
bytes), operating on the raw character stream **outside** backtick-quoted text (inside backticks,
every character including whitespace is passed through literally):

| Source syntax | Compiled to | Notes |
|---|---|---|
| whitespace, newlines (outside text) | *nothing* | Free formatting is purely cosmetic; the interpreter never sees it (`:1213-1214`, `:1268`). |
| `\` to end of line (outside text) | *nothing* | Line comment (`:1249-1255`, `:1323-1329`). |
| `` `text` `` | `` `text` `` verbatim | Backtick-delimited text pushed to the **list** at runtime by the interpreter's own `` ` `` case (`mudlle.cpp:722-751`), not by the converter. |
| `@xxx` (3 chars after `@`, outside text) | *nothing* (label definition) | Records `(mark_nam, mark_adr)` — a 3-char name hashed as base-128 digits, and its compiled byte offset (`:1216-1223`). Consumes 4 source chars, emits 0. |
| `Mxxx` (3 chars after `M`, outside text) | 4-digit zero-padded decimal offset | Looks up the label named `xxx` in the table built above and splices in its compiled address as literal ASCII digits (`:1277-1296`). If the label is undefined, compilation aborts and the "program" becomes the literal string `"Mark not found:"` + the next **5** raw source chars (`strncat(..., 5)` — the `Mxxx` token plus one stray character) + `"\n\r"` (`:1286-1292`) — this text, not a crash, is what the mob will run as its program if a builder mistypes a label. |
| `$zzzzz` (5 digits after `$`, outside text) | 6-digit zero-padded program index | Looks up `zzzzz` as a mudlle **zone** number in `mobile_program_zone[]` and splices in that program's *internal* 1-based index (or `99999` if not found) as ASCII digits (`:1301-1313`) — this is how one mudlle program calls another by its `#<zone>` header number rather than a compiler-internal index. |
| anything else | copied verbatim | Includes digits `0`-`9`, all single-letter opcodes, and punctuation. |

The upshot: the compiled program is a flat ASCII string mixing single-letter opcodes, inline
decimal-literal runs (which the interpreter treats as "push this number"), and backtick text —
labels and `$zone` references exist only at compile time and leave numeric addresses/indices behind.

### 2. Mudlle: the interpreter loop (`SPECIAL(intelligent)`, `mudlle.cpp:655`)

`intelligent()` is a `SPECIAL(...)` function (macro at `interpre.h:30`: signature
`int f(char_data* host, char_data* ch, int cmd, char* arg, int callflag, waiting_type* wtl)`), so it
is called through exactly the same call path as a hardcoded C special (see §5, Trigger catalogue).
`host` is the mobile that owns the program and all the stack/list/PC state; `ch` is "the other
character involved" (mover, attacker, speaker, etc., depending on the call site); `cmd`/`arg` are
the raw command number/argument text when the call arrived via a command hook.

**Gate check** (`:672-673`): `if (!IS_SET(CALL_MASK(host), callflag) && callflag != SPECIAL_DELAY) return FALSE;`
— the program only runs if the current `callflag` bit is set in the host's `CALL_MASK`
(`specials2.bad_pws`, reused per Data structures), **except** `SPECIAL_DELAY` (`interpre.h:38`,
value 8) always bypasses the mask, because a program that put itself to sleep via the `d` opcode
must always be resumable. `CALL_MASK` is initialized to `255` (all 8 flag bits) when a mudlle mob is
first loaded (`db.cpp:1231`), so a brand-new mob's very first invocation — whatever `callflag` it
happens to arrive with — always passes; programs then narrow their own mask with the `VI`
opcode pair (see the worked example) to stop being invoked for callflags they don't care about.

**Stale-reference sweep** (`:679-691`): before executing anything, a loop iterates every slot of
the host's list and nulls out `TARGET_CHAR` entries — but the sweep is **buggy as written**: the
existence test is `!char_exists(SPECIAL_LIST_REFS(host))`, and `SPECIAL_LIST_REFS` (`mudlle.h:61-62`)
always reads `field[SPECIAL_LIST_HEAD(ch)].ch_num` — the **head** slot's safety number — never
`field[tmp].ch_num` for the slot actually being examined. So every `TARGET_CHAR` slot is nulled or
kept based on whether the *head* entry's character still exists, not its own. The list head slot is
additionally validated via `CHECK_LIST` (`:691`). This (flawed) sweep is the interpreter's only
defense against dangling character pointers in the list — there is no equivalent sweep for
`TARGET_OBJ`/`TARGET_ROOM` at all.

**Main loop** (`:694-1195`): a `char* cmd_line = SPECIAL_PROGRAM(host)` (= `mobile_program[PROG_NUMBER(host)]`,
`mudlle.h:25`) is indexed by `PROG_POINT(host)`, the persistent program counter for the host's
*current* nesting level (`tactics`). The loop reads one `key` character at a time and:

- If `key` is a digit, it greedily consumes a run of digits, accumulates the decimal value, and
  pushes it to the stack (`:700-709`) — this is how integer literals (including compiled label
  addresses) enter the machine. **The `switch` below still fires** on the terminating non-digit
  character in the same iteration.
- Otherwise it dispatches on `key` in a large `switch` (`:714-1178`, opcode table below).
- After each opcode, `PROG_POINT(host)` is advanced by 1 and the next `key` is read (`:1180-1181`),
  unless the opcode already changed `PROG_POINT` directly (jumps, subroutine call/return).
- The loop terminates when `key == 0` (end of string — `PROG_POINT` resets to 0 for next time,
  `:1184-1185`), `key == ','` (**interrupt, return FALSE**, `:753-756`), `key == ';'`
  (**interrupt, return TRUE**, `:758-761`), or a runaway guard `cmd_count >= 100` trips
  (`:695` — the loop condition itself; this caps how many opcodes one `intelligent()` call can
  execute before yielding control back to the caller, i.e. **one command dispatch executes at most
  ~100 opcodes then simply stops mid-program**, silently, with no log message — see Error handling).
  On a `,`/`;` exit, `PROG_POINT` is left one past the interrupt character, so the **next**
  invocation resumes exactly there — this is the entire "suspension" mechanism; there is no
  separate saved-continuation object.

#### Opcode table (mudlle)

Grouped by function; letter is the compiled-program character; `mudlle.cpp` line is where the case
lives in the `SPECIAL(intelligent)` switch unless noted. Cross-checked against
`lib/text/mudlle.keys` (the builder cheat-sheet).

**Literals / structure**
| Op | Effect | Cite |
|---|---|---|
| `0`-`9` | push accumulated decimal literal to stack | `:700-709` |
| `` `text` `` | push a text block (verbatim, heap pool-allocated) to the **list** | `:722-751` |
| `.` | no-op | `:719` |
| `?` | dump stack and list contents via `do_say` (debug aid) | `:715-717`, `question_proc` `mudlle.cpp:546` |

**Control flow / termination**
| Op | Effect | Cite |
|---|---|---|
| `,` | stop, return `FALSE` (interrupt) | `:753-756` |
| `;` | stop, return `TRUE` (interrupt) | `:758-761` |
| `Q` | zero the whole stack+list, reset PC/tactics, reset `CALL_MASK` to 255, return `FALSE` | `:763-798` |
| `R` | same full reset as `Q`, but return `TRUE` | `:763-798` |
| `g` | unconditional goto: pop address, jump (0 if out of program bounds) | `:1119-1124` |
| `i` | conditional goto: pop address, pop condition (or synthesize condition 0 if only one value on stack) — jump only if condition is truthy | `:1126-1139` |
| `K` | call subroutine: pop program index, push a new `tactics` frame (max depth `SPECIAL_CALLLIST`=10), switch `cmd_line`/`PROG_NUMBER` to the new program at PC 0. Quirk: the new program number is written to `PROG_NUMBER(ch)` — the *other* character's slot — while the depth increment and subsequent `SPECIAL_PROGRAM(host)` read use `host` (`:1102-1105`); correct only when `ch == host` (e.g. the AI self-tick) | `:1095-1108` |
| `r` | return from subroutine: pop the `tactics` frame (returns `FALSE` from the whole call if already at depth 0) | `:1110-1117` |
| `d` | delay: pop a round count, advance PC past the `d`, call `WAIT_STATE_FULL` (suspend via the shared `waiting_list`, see §6), return `FALSE` immediately | `:826-830` |

**Arithmetic / comparison (binary ops pop 2, push 1; degrade to a single-operand form if the stack has &lt;2 items)**
| Op | Effect | Cite |
|---|---|---|
| `+` `-` `*` `/` | sum / (b−a with a popped first, i.e. `stack[-2]-stack[-1]`) / product / integer quotient (div-by-zero says so via `do_say` and pushes 0) | `:992-1035` |
| `<` `>` `=` | `stack[-2] < stack[-1]`, etc. | `:1037-1065` |
| `&` `\|` | bitwise/logical AND / OR of the two popped values | `:1067-1085` |
| `!` | logical NOT of the popped value (or push 1 if stack empty) | `:1087-1093` |

**Stack↔list bridge**
| Op | Effect | Cite |
|---|---|---|
| `s` | `do_say` the list-head if it's text, then remove it | `:935-946` |
| `E` | echo list-head text (or a stock fallback line) to a char or room from the list, then remove | `:947-960` |
| `Z` | convert list-head to its string representation (`int_itemtostring`) | `:906-909`, `mudlle2.cpp:437` |
| `l` / `L` | "contains" check: scan the list for an entry matching the head (same type + pointer), push found-flag to stack, and on a hit rotate that entry to the head. **`l` and `L` behave identically** — they share one `case` body, and the differentiator meant to make `l` a soft (report-only) check tests `key == 'c'` (`:982-983`), which can never be true inside this case, so the soft variant is dead code | `:971-990` |

**Command execution** (the host mob performs a game command)
| Op | Effect | Cite |
|---|---|---|
| `m` / `c` / `C` | pop `subcmd` then `cmd` from the stack, take 0 (`m`) / 1 (`c`) / 2 (`C`) target(s) from the list head (removing them), build a synthetic `waiting_type` and run `command_interpreter(host, "", &tmpwtl)` — this is how a mudlle mob walks, opens doors, attacks, etc. | `:800-825` |

**Two-letter groups** (each reads one more source char as the sub-opcode)
| Prefix | Group | Handler | Sub-opcode catalogue |
|---|---|---|---|
| `S` | service commands on stack/list (dup/delete/rotate/reset/list-manipulate/scan-for-duplicate) | `service_commands`, `mudlle.cpp:57` | `u d x f b r U D X F B C N n` — see `mudlle.cpp:64-189` (`N`/`n` at `:135,151` are absent from `lib/text/mudlle.keys`' human list) |
| `v` | to-stack: read a game value onto the stack (command/subcommand/callflag, hit/mana/move, level, room number, path direction, list comparison/scan, random number, list-item type) | `int_tostack`, `mudlle.cpp:192` | `C c I h H m M v V l r R P = N T S` — `:201-337` |
| `V` | from-stack: write the popped value into a game field (call mask, hit/mana/move) | `int_fromstack`, `mudlle.cpp:605` | `I h m v` — `:612-635` |
| `f` | to-list: push a game entity onto the list (self, command-line arg, stack-int→text, string concat/split, current room, room-by-number, invoker, first char/first-non-NPC in room, fighting opponent) | `int_tolist`, `mudlle.cpp:341` | `s a A i + / r R h c C 0 f` — `:352-495` |

**Errors**: any unrecognized top-level `key` character falls to `default:` which `do_say`s
`"I can't understand my command (%c), alas :("` and continues to the next character (`:1172-1177`)
— a malformed opcode does **not** abort the program, it just announces itself and is skipped.
Unrecognized sub-opcode letters inside `v`/`V`/`f`/`S` mostly fall through to a silent `value = 0` /
no-op default (e.g. `mudlle.cpp:333-336`), except `f`'s default which does emit a `do_say`
(`mudlle.cpp:491-494`).

### 3. Scripts: the command-list interpreter (`run_script`, `script.cpp:715`)

`run_script(info_script* info, script_data* position)` walks the doubly-linked `script_data` list
starting at `position` with a plain `switch (curr->command_type)`, advancing `curr = curr->next`
after each non-branching command. There is no program counter, no call stack, and no bytecode —
each node is interpreted directly, and "variables" are always looked up by symbolic parameter
number (100-999 range, `SCRIPT_PARAM_*` defines at `script.h:101-162`) through the accessor
functions below, never by raw stack position.

**Branching** follows one behavioral pattern across all the `IF_*` node types (illustrated by
`SCRIPT_IF_INT_EQUAL`, `:1050-1073`, and behaviorally identical for `SCRIPT_IF_INT_LESS`,
`_GREATER`, `_TRUE`, `_FALSE`, `_IS_NPC`, `_ROOM_SUNLIT`, `_STR_EQUAL`, `_STR_CONTAINS` —
`_FALSE`/`_ROOM_SUNLIT` are coded with early-exit guards rather than nested if/else, but the
observable branch behavior is the same):
- If the test is true, execution falls through to `curr->next` (normally a `SCRIPT_BEGIN` node).
- If false, and `curr->next` is a `SCRIPT_BEGIN`, execution jumps via `get_next_command()`
  (`script.cpp:700`) to **one past** the matching `SCRIPT_END`/`SCRIPT_END_ELSE_BEGIN` — i.e. it
  skips the entire true-block (recursing into any nested `SCRIPT_BEGIN` blocks along the way,
  `:706-707`) and lands either after the block (no else) or at the first command of an else-block.
- If false and `curr->next` is *not* a `SCRIPT_BEGIN` (a single bare command with no block), it
  simply skips that one command (`curr = curr->next->next`).
- If a param lookup fails (missing/null variable) or there's no `curr->next` to fall back on, the
  whole script aborts (`exit = TRUE`) with whatever `return_value` was already set (default `1`).
- `SCRIPT_END_ELSE_BEGIN`, when reached by **normal forward execution** (i.e., the preceding
  true-block just finished), always calls `get_next_command()` on itself to skip the else-block
  entirely (`:987-989`) — this is what makes if/else symmetric regardless of which branch was
  taken.
- `SCRIPT_ABORT` and running off the end of the list (`curr == NULL`) both end the script
  immediately with `return_value` unchanged from `1` (`:745-747`, `:1521-1522`).
- `SCRIPT_RETURN_FALSE` explicitly forces `return_value = FALSE` and stops (`:1367-1370`).

**No recursion/runaway-loop guard**: unlike mudlle's `cmd_count < 100` cap and `SPECIAL_CALLLIST`
depth limit, `run_script` has no opcode-count ceiling and no subroutine mechanism at all (there is
no "call another script" command in the enum) — the only way a `.scr` program can loop is via
`SCRIPT_DO_WAIT`'s cross-pulse resume (below); within one `run_script` call the list is walked
strictly forward/skip, so it always terminates as long as the data doesn't contain a cyclic `next`
chain (which the loader never produces).

#### Command_type category tables (`script.h`)

Structural/flow (1-10):
| Value | Name | Effect | Cite |
|---|---|---|---|
| 1 | `SCRIPT_BEGIN` | block start; no-op, advances | `script.cpp:824-826` |
| 2 | `SCRIPT_END` | block end; no-op, advances | `:983-985` |
| 3 | `SCRIPT_ABORT` | stop, keep current `return_value` (normally "continue") | `:745-747` |
| 4 | `SCRIPT_IF_INT_EQUAL` | branch on `*a == *b` | `:1050-1073` |
| 5 | `SCRIPT_END_ELSE_BEGIN` | end true-block / skip else-block | `:987-989` |
| 6 | `SCRIPT_RETURN_FALSE` | stop, force `return_value = FALSE` | `:1367-1370` |
| 7 | `SCRIPT_IF_INT_LESS` | branch on `*a < *b` | `:1075-1098` |
| 8 | `SCRIPT_IF_IS_NPC` | branch on `IS_NPC(chX)` | `:1175-1195` |
| 9 | `SCRIPT_IF_STR_EQUAL` | branch on case-insensitive string equality vs `curr->text` | `:1254-1274` |
| 10 | `SCRIPT_IF_STR_CONTAINS` | branch on `strstr(uppercase(param text), curr->text)` — the *param* side is uppercased, `curr->text` is matched verbatim (so it must be authored in uppercase to hit) | `:1223-1252` |
| 72 | `SCRIPT_IF_INT_GREATER` | branch on `*a > *b` | `:1100-1123` |
| 73 | `SCRIPT_IF_INT_TRUE` | branch on `*a > 0` | `:1125-1146` |
| 74 | `SCRIPT_IF_ROOM_SUNLIT` | branch on `IS_SUNLIT(room)` | `:1197-1221` |
| 76 | `SCRIPT_IF_INT_FALSE` | branch on `*a < 1` | `:1148-1173` |

Triggers (11-23) — these are never executed as *commands*; they only mark the head of a per-entity
linked list so `char_has_script()` can find the entry point for a given event (see §5):
`ON_ENTER`(11), `ON_BEFORE_ENTER`(12), `ON_BEFORE_DIE`(13, unused — see Open questions),
`ON_DIE`(14), `ON_RECEIVE`(15), `ON_EXAMINE_OBJECT`(16), `ON_HEAR_SAY`(17), `ON_DAMAGE`(18),
`ON_EAT`(19), `ON_DRINK`(20), `ON_WEAR`(21), `ON_PULL`(22), `ON_HEAR_YELL`(23).

Actions (30-77) — world-mutating ones are called out with exact effects; the rest are
send-text/assignment helpers:
| Value | Name | Effect | Cite |
|---|---|---|---|
| 30 | `SCRIPT_DO_SAY` | `do_say(chX, sprintf(text, textparam))` — **one** `%s`-style substitution from `param[1]` | `:921-930` |
| 31 | `SCRIPT_ASSIGN_STR` | copy `curr->text` into a `strX` variable (heap-allocates) | `:815-822` |
| 32/33 | `SCRIPT_SEND_TO_ROOM`/`_X` | `send_to_room[_except]` with one substitution | `:1385-1410` |
| 34 | `SCRIPT_DO_HIT` | builds a synthetic `waiting_type` targeting `ch2` and calls `do_hit(ch1, 0, &wtl, CMD_HIT, SCMD_MURDER)` — **forces an attack** | `:895-909` |
| 35 | `SCRIPT_ASSIGN_INV` | find an object by vnum in a char's inventory, bind to `obX`, count into an `intX` | `:769-790` |
| 36 | `SCRIPT_SEND_TO_CHAR` | `send_to_char` with one substitution | `:1372-1383` |
| 37 | `SCRIPT_DO_FLEE` | `do_flee(chX)` | `:857-864` |
| 38 | `SCRIPT_DO_EMOTE` | `do_emote(chX, curr->text)` | `:848-855` |
| 39 | `SCRIPT_LOAD_MOB` | **`read_mobile(real_mobile(param0), REAL)`** — spawns a fresh mob instance into the world (not placed in a room yet) and binds it to `chX` | `:1276-1283` |
| 40/65/75 | `SCRIPT_TELEPORT_CHAR`/`_X`/`_XL` | `char_from_room`+`char_to_room`; `_X`/`_XL` leave followers behind, plain variant also relocates NPC followers standing in the same room; all three stop riding first | `:1465-1515` |
| 41 | `SCRIPT_DO_YELL` | `do_gen_com(..., SCMD_YELL)` with one substitution | `:972-981` |
| 42 | `SCRIPT_DO_FOLLOW` | `circle_follow`/`add_follower` — makes `ch1` follow `ch2` | `:866-880` |
| 43 | `SCRIPT_EXTRACT_CHAR` | `extract_char()` if NPC — **removes a character from the game** | `:1007-1018` |
| 44 | `SCRIPT_SET_EXIT_STATE` | `set_exit_state()`, mirrored onto the reverse exit if it points back | `:1412-1423` |
| 45 | `SCRIPT_RAW_KILL` | `raw_kill(chX, NULL, 0)` — **kills the character** | `:1358-1365` |
| 46 | `SCRIPT_DO_GIVE` | `perform_give(ch1, ch2, obj)`, only if `obj` is actually carried by `ch1` | `:882-893` |
| 47/77 | `SCRIPT_LOAD_OBJ`/`_X` | `read_object(real_object(...), REAL)` — **spawns an object**, not yet placed anywhere; `_X` loads relative to `ob1`'s item number | `:1285-1302` |
| 48/49 | `SCRIPT_OBJ_TO_CHAR`/`_TO_ROOM` | `obj_to_char`/`obj_to_room` | `:1326-1344` |
| 50 | `SCRIPT_CHANGE_EXIT_TO` | mutates `dir_option[dir]->to_room` directly | `:828-836` |
| 51 | `SCRIPT_EXTRACT_OBJ` | `extract_obj()` — **removes an object from the world** | `:1020-1029` |
| 52 | `SCRIPT_ASSIGN_EQ` | bind `obX` to `chX->equipment[pos]` | `:749-767` |
| 53/54/55 | `SCRIPT_DO_DROP`/`_REMOVE`/`_WEAR` | `perform_drop`/`perform_remove`/`perform_wear`, each gated by an ownership/position sanity check | `:838-846`, `:911-919`, `:961-970` |
| 56 | `SCRIPT_DO_SOCIAL` | resolves `curr->text` via `find_action()` and runs it via `do_action` | `:932-950` |
| 57/60-63/69 | `SCRIPT_SET_INT_VALUE`/`_SUM`/`_MULT`/`_DIV`/`_SUB`/`_RANDOM` | integer assignment/arithmetic into a writable int param (see `is_int_param_writable`, `:237-260`) | `:1426-1463` |
| 58 | `SCRIPT_DO_WAIT` | suspends the script — see §6 | `:952-959` |
| 59 | `SCRIPT_PAGE_ZONE_MAP` | `send_to_char(zone_table[...].map, chX)` | `:1346-1355` |
| 64 | `SCRIPT_GAIN_EXP` | `gain_exp()`, with a level-30+ exp-curve dampening formula inlined | `:1031-1048` |
| 66/67 | `SCRIPT_OBJ_FROM_ROOM`/`_FROM_CHAR` | `obj_from_room`/`obj_from_char`, gated by a location-match check | `:1304-1324` |
| 68 | `SCRIPT_ASSIGN_ROOM` | find object by vnum in a room, bind to `obX` | `:792-813` |
| 70 | `SCRIPT_EQUIP_CHAR` | loads up to 5 objects by vnum straight into a char's inventory then `do_wear(ch, "all")` | `:991-1005` |
| 71 | `SCRIPT_SET_INT_WAR_STATUS` | reads the good/evil "fame war" leader into an int | `:1459-1463`, `int_0ary_op:334` |
| 99 | `SCRIPT_COMMAND_NONE` | reserved placeholder, not handled in `run_script` — falls to `default: exit = TRUE` | — |

Any `command_type` not in the `switch` (including 99, and any value the OLC hasn't wired up)
silently ends the script via the `default: exit = TRUE;` case (`:1517-1519`) — **no log message**,
`return_value` keeps whatever it already was (normally the initial `1`, i.e. "continue normally").

### 4. Value/expression decoding (scripts)

Every script parameter is a small integer that is *dispatched*, not indexed, through one of:
`get_char_param`/`get_obj_param`/`get_room_param`/`get_int_param`/`get_text_param`
(`script.cpp:513,557,527,353,172`) — each is a hand-written `switch` over the
`SCRIPT_PARAM_*` constants (full numeric table in `script.h:101-162`). Room params
`CH1_ROOM`/`CH2_ROOM`/`CH3_ROOM` are **computed** (`&world[chX->in_room]`,
`:537-551`) rather than stored. Writes go through `set_int_value`/`assign_char_param`/
`assign_obj_param` (`:267`, `:498`, `:484`); `is_int_param_writable` (`:237`) denies writes to
derived fields (`chX.level`, `.rank`, `.race`, `.exp`, `obX.vnum`) — attempted writes are logged via
`vmudlog(BRF, ...)` (`:280-283`) and silently dropped, not applied.

### 5. Trigger catalogue and dispatch sites

The two engines are reached by **structurally different** dispatch mechanisms.

**Mudlle / hardcoded-C specials** — reached through a single generic `callflag` bitmask
(`interpre.h:34-41`): `SPECIAL_NONE`(0) `SPECIAL_COMMAND`(1) `SPECIAL_SELF`(2) `SPECIAL_ENTER`(4)
`SPECIAL_DELAY`(8) `SPECIAL_TARGET`(16) `SPECIAL_DAMAGE`(32) `SPECIAL_DEATH`(64). A mudlle program
gates which flags it responds to via its own `CALL_MASK` (see §2); there is no per-callflag
sub-catalogue the way scripts have `ON_*` codes — one program can, in principle, react to all 8.

| Callflag | Fired by / when | Dispatch site |
|---|---|---|
| `SPECIAL_COMMAND` | Every recognized player/mob command, before the command runs | `interpre.cpp:1393-1394` (`special(ch, cmd, arg, SPECIAL_COMMAND, argument_info, NOWHERE)`) |
| `SPECIAL_TARGET` | Same command dispatch, checked against the command's parsed target(s) instead of the room | `interpre.cpp:1396-1397` |
| `SPECIAL_SELF` | Mobile AI activity pulse (per-mob "think" tick) | `mobact.cpp:136` (also `:116-132` for the hardcoded-C/virt-table branch) |
| `SPECIAL_ENTER` | A character finishes moving into/out of a room | `act_move.cpp:447,451,860` |
| `SPECIAL_DAMAGE` | Damage dealt in combat, checked against the victim/attacker as targets (routed through `special()`'s target branch, `interpre.cpp:1633-1682`) | `fight.cpp:1648` (in `damage()`, on the attacker), `fight.cpp:2576`, `clerics.cpp:304` |
| `SPECIAL_DEATH` | A character dies — fired at the top of `raw_kill()`; a nonzero return **aborts the rest of `raw_kill`** (the death itself) | `fight.cpp:891` (`if (special(dead_man, 0, "", SPECIAL_DEATH, &tmpwtl)) return;`); handled by `special()`'s early-out branch, `interpre.cpp:1626-1629` |
| `SPECIAL_DELAY` | Resuming from a `d`-opcode sleep (passed to both the `virt_program_number` and mudlle `intelligent()` resume branches) | `comm.cpp:2102,2104` (live path, see §6) |
| `SPECIAL_NONE` | Resuming from a non-command wait with `delay.cmd == -1` when a hardcoded `mob_index[].func` C special handles the resume (the other two resume branches get `SPECIAL_DELAY` instead) | `comm.cpp:2099` |

`special()` (`interpre.cpp:1605`) is the fan-out point: for `SPECIAL_COMMAND`/`SPECIAL_SELF`/
`SPECIAL_ENTER` it walks the room's people/inventory/equipment/contents and calls
`activate_char_special`/`activate_obj_special` on each (`:1690-1719`); for `SPECIAL_TARGET`/
`SPECIAL_DAMAGE` it instead checks the two explicit targets in the `waiting_type` (`:1633-1682`);
for `SPECIAL_DEATH` it calls the dying character's own special directly (`:1626-1629`).
`activate_char_special` (`:1556`) is where the fork lives. For mobiles (`IS_MOB`): a
`mob_index[].func` hardcoded-C pointer wins if `MOB_SPEC` is set (`:1563-1566`); else a nonzero
`specials.store_prog_number` is resolved through `virt_program_number` (`:1567-1571`, see
RotS-specific notes); else, if `specials.union1.prog_number` is set, `intelligent()` runs directly
(`:1572-1576`) — this is the only branch that is genuinely mudlle bytecode. There is also a fourth,
player-only branch (`:1577-1582`): a non-NPC with a nonzero `store_prog_number` goes through
`virt_program_number` with **no** `MOB_SPEC` check at all.

**Scripts** — reached through `call_trigger(trigger_type, subject, subject2, subject3)`
(`script.cpp:601`), a `switch` keyed on the `ON_*` constant, called from these game-logic sites
(all confirmed by grep, not by the data-formats doc's partial list):

| Trigger | Call site | Notes |
|---|---|---|
| `ON_BEFORE_ENTER` | `act_move.cpp:176` | Gating — `FALSE` blocks the move. Fans out to every char already in the room via `trigger_room_event`→`trigger_before_char_enter` (`script.cpp:666-680`, `1555-1573`). |
| `ON_ENTER` | `act_move.cpp:449,454,863` | Non-gating notification. Fans out to room (currently a no-op stub, `trigger_room_enter:1535-1538`), every other char present, and every object in the room (`script.cpp:682-690`). |
| `ON_DIE` | `fight.cpp:1000` | Gating — `FALSE` prevents the death. |
| `ON_DAMAGE` | `fight.cpp:1824` | Gating. Fires on the victim's own script (`trigger_char_damage`), then — if still `TRUE` — on the attacker's wielded weapon's script (`call_trigger:612-617`). |
| `ON_RECEIVE` | `act_obj1.cpp:581` | Non-gating (return value discarded at the call site). |
| `ON_EXAMINE_OBJECT` | `act_info.cpp:320` | Gating — `FALSE` presumably suppresses the default examine text (caller-dependent). |
| `ON_HEAR_SAY` | `act_comm.cpp:114` | Also internally triggers `ON_HEAR_YELL` scripts on the same listener via `trigger_char_hear` (`script.cpp:1648-1656`) — `call_trigger`'s own `ON_HEAR_YELL` case (`:643-645`) calls the same `trigger_char_hear` function, so yells and says share one handler. |
| `ON_HEAR_YELL` | *(no direct `call_trigger(ON_HEAR_YELL, ...)` call site found — see Open questions; reached today only as the secondary check inside `trigger_char_hear`)* | |
| `ON_DRINK` | `act_obj2.cpp:207` | |
| `ON_EAT` | `act_obj2.cpp:282` | |
| `ON_WEAR` | `act_obj2.cpp:785` | Gating — `FALSE` blocks wearing. |
| `ON_PULL` | `act_move.cpp:1880` | Gating — `FALSE` blocks the lever pull. |
| `ON_BEFORE_DIE` | *(defined in `script.h:36` "implemented??"; no `call_trigger`/dispatch site found anywhere in `src/`)* | Dead trigger code — see Open questions. |

`char_has_script(&index, script_no, script_type)` (`script.cpp:577`) is how any of the `trigger_*`
functions locate the entry point: it looks up `script_no` (the owning entity's
`specials.script_number`/`obj_flags.script_number`) in `script_table[]` via
`find_script_by_number` (`:80`), then linearly scans that entity's *entire* command list for a node
whose `command_type` equals the requested `ON_*` value (`:588-590`) — i.e. **one script can carry
several trigger entry points** (several `ON_*` header rows), each with its own following command
chain, and `run_script` is invoked starting at `script_position->next` (the trigger header itself is
never executed as a command).

Every `trigger_char_*` function first checks `IS_AFFECTED(ch, AFF_WAITING)` and returns `1`
("continue normally") without running anything if the entity is mid-`SCRIPT_DO_WAIT`
(`script.cpp:1561,1581,1600,1619,1638,1666` — "Characters who are on the waiting list cannot run
scripts", per the file banner comment `script.cpp:13-14`) — this is a deliberate re-entrancy guard,
not an oversight.

### 6. Waiting / delay / suspension — shared by both engines

Both `SCRIPT_DO_WAIT` (`script.cpp:952-959`, via `WAIT_STATE_BRIEF`) and mudlle's `d` opcode
(`mudlle.cpp:826-830`, via `WAIT_STATE_FULL`) use the **same** underlying delay primitive:
`WAIT_STATE_BRIEF`/`WAIT_STATE_FULL` (macros, `utils.h:481-543`) set `char_data::delay` fields
(`wait_value`, `cmd`, `subcmd`, `priority`, target/flag data) and splice the character into the
**global intrusive linked list** `waiting_list` (`char_data::delay.next`). Execution does **not**
block synchronously — the calling `run_script`/`intelligent()` invocation returns immediately
(mudlle's `d` case literally `return FALSE;`s, `mudlle.cpp:829`; `run_script`'s `SCRIPT_DO_WAIT`
case just sets `exit = TRUE` and returns whatever `return_value` already held — default `1`/"continue
normally", `script.cpp:958`) and the game's main heartbeat loop decrements `delay.wait_value` once per
pulse for every character on `waiting_list` (`comm.cpp:598-620`); when it reaches 0, the loop calls
the free function `complete_delay(char_data*)` (`comm.cpp:2084`), which:
- if `delay.cmd == CMD_SCRIPT` (`interpre.h:336`, value 9999): calls `continue_char_script(ch)`
  (`script.cpp:1544`), which re-initializes `ch->specials.script_info` (see RotS-specific notes for
  what this wipes) and resumes `run_script` at the saved `next_command`.
- otherwise (a `-1` "special resume" for NPCs, `comm.cpp:2096-2104`): calls the mob's
  `mob_index[].func` with `callflag = SPECIAL_NONE` (`:2099`), or `virt_program_number`'s function
  with `SPECIAL_DELAY` (`:2102`), or (if neither applies and `union1.prog_number` is set)
  `intelligent()` directly with `SPECIAL_DELAY` (`:2104`). The mudlle resume picks up the saved
  `PROG_POINT`/`tactics` state, i.e. the interpreter simply reads `key` from wherever `PROG_POINT`
  was left and continues the same `switch`-based loop.

**Important — a parallel, unused implementation exists.** `src/wait_functions.cpp`/`.h` define a
`game_types::wait_list` singleton class with its own `wait_state_full`/`complete_delay`/`update()`,
and `src/delayed_command_interpreter.cpp`/`.h` define a class that duplicates `complete_delay`'s
dispatch logic. Both compile (both are listed in the Makefile's object list) but **neither is
reachable from the live game loop**: `wait_list::update()` (the only function that would ever drain
`m_waitingList`) has zero callers anywhere in `src/`, and `wait_list::create()`/`::instance()` also
have zero callers, confirmed by `grep -rn` across the whole tree. `delayed_command_interpreter` is
only ever constructed inside `wait_list::complete_delay` (`wait_functions.cpp:179`), which is itself
unreachable. This is the same pattern AGENTS.md documents for `combat_manager.cpp` — dead code that
compiles cleanly and will mislead anyone reading it as "the" wait mechanism. **The live wait/resume
path is exclusively the `WAIT_STATE_BRIEF`/`WAIT_STATE_FULL` macros + the global `waiting_list` +
`comm.cpp`'s free-function `complete_delay`/`abort_delay`, drained by the heartbeat loop at
`comm.cpp:598-620`.**

### 7. Error handling

- **Mudlle bad opcode**: `do_say`s a diagnostic to the room and continues at the next character
  (`mudlle.cpp:1172-1177`) — never aborts the program.
- **Mudlle bad `Mxxx` label**: compile-time only; produces a literal `"Mark not found:..."` program
  body (`mudlle.cpp:1286-1292`) that will just `do_say` a garbled string forever at runtime (it has
  no valid opcodes past the text itself once interpreted... actually it *is* just interpreted as
  opcodes character by character like any other program, so it will likely hit the "I can't
  understand" default case repeatedly rather than actually saying the diagnostic — not independently
  verified, see Open questions).
- **Mudlle empty stack underflow**: `FROM_STACK`/`STACK_VALUE` return `0` rather than
  erroring when the stack pointer is `<= 0` (`mudlle2.cpp:47-68`) — silent, not logged.
- **Script bad param / missing variable**: aborts the *rest of that script invocation* with
  `exit = TRUE` and whatever `return_value` already held (`script.cpp:1069-1072` and identically for
  every `IF_*`/most action commands) — no log message for a merely-null variable.
- **Script writes to a read-only int param**: logged via `vmudlog(BRF, "Script #%d tried to write to
  unwritable integer parameter %s", ...)` (`script.cpp:280-283`) and the write is dropped.
- **Script unknown binary/unary/0-ary int op**: `op_error()` logs `vmudlog(BRF, "Unrecognized binary
  integer operation %d in script #%d", ...)` (`script.cpp:286-291`) and returns `0`.
- **Script divide by zero**: `vmudlog(BRF, "Script #%d tried to divide by 0.", ...)`
  (`script.cpp:311-312`) — note the `case` **falls through** to `SCRIPT_SET_INT_RANDOM` immediately
  below it in `int_binary_op` (no `break`/`return` after the log call, `script.cpp:307-313`), so a
  divide-by-zero actually returns `number(*a, *b)` (a random value) rather than `0` — likely an
  unintended fallthrough, see Open questions.
- **`call_trigger` unknown trigger_type**: `log("Error in call_trigger: unknown trigger_type")`,
  returns `1` (`script.cpp:659-661`).
- **`trigger_room_event` unknown trigger_type**: same pattern, `script.cpp:692-694`.

## RotS-specific notes

- Stock CircleMUD/DikuMUD has neither system — only hardcoded C "special procedures" assigned by
  vnum (`ASSIGNMOB`/`ASSIGNOBJ`, `interpre.h:43-64`). RotS layers **three** independent dispatch
  mechanisms on top of/alongside that stock mechanism, all reachable through the very similar-looking
  `store_prog_number`/`prog_number` fields, which makes them easy to confuse:
  1. **Hardcoded C specials**, assigned directly to `mob_index[].func`/`obj_index[].func` — stock
     mechanism, unrelated to this doc.
  2. **`virt_program_number`/`virt_obj_program_number`** (`spec_ass.cpp:309,461`) — a small
     hand-written `switch (number)` mapping a *virtual special number* (stored in the very same
     `specials.store_prog_number`/`obj_flags.prog_number` fields mudlle uses!) to a hardcoded C++
     `SPECIAL()` function pointer (e.g. `snake`, `gatekeeper`, `mob_cleric`, **`block_exit_north`**
     — `spec_pro.cpp:2376`, a hand-coded, more elaborate reimplementation of exactly the scenario
     traced in the mudlle worked example below). For mobiles this path is effectively gated on the
     `MOB_SPEC` flag (`specials2.act`), but **indirectly**: `activate_char_special`'s
     `store_prog_number` branch (`interpre.cpp:1567-1571`) has no `MOB_SPEC` test of its own —
     instead, `read_mobile` (`db.cpp:1204-1214`) *moves* a non-`MOB_SPEC` mob's `store_prog_number`
     into `union1.prog_number[0]` and zeroes it at load time, so a nonzero `store_prog_number` at
     runtime implies `MOB_SPEC` was set. (Players can also reach `virt_program_number` via a
     separate branch with no `MOB_SPEC` check at all, `interpre.cpp:1577-1582`.) It is **not**
     mudlle bytecode and is out of scope for this doc — documenting the ~30-entry table belongs in
     a "hardcoded specials" systems doc.
  3. **Mudlle bytecode** — taken when `MOB_SPEC` is *not* set and `union1.prog_number` is nonzero;
     this is the actual subject of §1-2 above.
  The overload is resolved once, at mob load time, in `read_mobile`: `db.cpp:1204-1231` allocates
  the mudlle stack/list (`poofIn`/`poofOut`) and initializes `CALL_MASK = 255` **only** when
  `store_prog_number != 0 && !MOB_SPEC` — an entity with `MOB_SPEC` set never gets mudlle state
  allocated at all, so `intelligent()` could not run for it even if reached.
- **Return-value convention is inverted between the two callflag-adjacent mechanisms and the
  script trigger mechanism.** For `special()`/`activate_char_special()` (used by mudlle and
  hardcoded C specials), returning **`TRUE`/1 means "I handled this event, suppress the default
  action"** (e.g. `may_not_perform |= special(...)`, `interpre.cpp:1393-1397` — a nonzero result
  blocks the normal command). For `call_trigger()`/`run_script()` (used by `.scr` scripts),
  returning **`FALSE`/0 means "veto/suppress"** and `TRUE`/1 means "continue as normal" (documented
  explicitly in `script.cpp`'s file banner, `:10-11`, and in `script.h`'s comments on `ON_DIE`/
  `SCRIPT_RETURN_FALSE`). Porting logic between the two systems (or from either into a rewrite)
  must flip this polarity.
- **Field repurposing beyond the mudlle stack fields**: `specials2.bad_pws` ("number of bad
  password attempts" — a player-login counter) doubles as mudlle's `CALL_MASK` for mobiles
  (`utils.h:479`); `specials.tactics` ("combat tactics of a person") doubles as the mudlle
  subroutine-nesting depth; `specials.invis_level` doubles as the mudlle stack pointer. None of
  these are documented anywhere except the inline `/* also a ... */` comments next to the field
  declarations (`structs.h:1117-1144`).
- `script_head::host` (`protos.h:180`, declared as `int* host`, commented "whether the script is
  for char, obj or room_data") is **never read or written anywhere** in `load_scripts`,
  `run_script`, `call_trigger`, or `shapescript.cpp` — confirmed by exhaustive grep. It is dead/
  vestigial. Runtime char-vs-obj-vs-room context is instead determined entirely by *which* trigger
  function (`trigger_char_*` vs `trigger_object_*`) and *which* owning struct's `script_number`
  field led to the dispatch — never by data read from the script itself. This resolves the
  data-formats doc's open question about `host`.
- **`info_script` is mostly, but not entirely, reset on every trigger firing.** `initialise_script_info_char`/`_obj`
  (`script.cpp:94,106`) call `clear_script_info` (`:118`) on *every* dispatch, which nulls `ch[0..2]`,
  `rm[0..2]`, `ob[0..2]`, and frees `str[0..2]` — but does **not** touch `ints[0..2]`. That means
  `intX` script variables persist across separate, unrelated trigger firings of the same script
  (e.g. a counter incremented on `ON_HEAR_SAY` survives into the next `ON_ENTER`), while `chX`/
  `obX`/`rmX`/`strX` never do. Rooms currently have no working script path (`trigger_room_enter`
  is a stub returning `1`, `script.cpp:1535-1538`) even though `script_head` nominally supports a
  room "host".
- Rooms have no mudlle state (`poofIn`/`poofOut`/stack/list) at all — a mudlle program can only ever
  be attached to a character. A "blocking exit" program (see worked example) must be attached to a
  guard **mobile** standing in the room, not to the room or an exit/door object directly.

## Worked example

### A. `.scr` trigger trace — script #1100 "Thranduil's Herald" (`ON_ENTER`)

Raw file (`lib/world/scr/11.scr`, first record):

```
#1100 Thranduil's Herald~
~
11 1 0 0 0 0 0 0
 starts here~
8 2 200 201 0 0 0 0
 If this is a mob...~
1 3 0 0 0 0 0 0
 ... then do nothing~
5 4 0 0 0 0 0 0
 But if it's a player...~
30 5 100 201 0 0 0 0
 Lord Thranduil, %s is here seeking an audience.~
2 6 0 0 0 0 0 0
 ~
999 0 0 0 0 0 0
```

Loaded by `load_scripts` (`db.cpp:1025`) into a 6-node linked list (rows 1-6; the `999` row is the
terminator, never a node). Any mobile whose `.mob` record sets `specials.script_number = 1100`
carries this script; when a character walks into that mobile's room, `act_move.cpp` calls
`call_trigger(ON_ENTER, &world[room], mover, 0)` (`act_move.cpp:454`) →
`trigger_room_event(ON_ENTER, room, mover)` (`script.cpp:682-690`) → for each other character
`tmpch` in the room (the herald mob included) → `trigger_char_enter(tmpch, mover, room)`
(`script.cpp:1613-1630`), which:
1. Skips entirely if the herald is `AFF_WAITING` (`:1619-1620`).
2. `char_has_script(&index, herald->specials.script_number /* 1100 */, ON_ENTER)`
   (`script.cpp:577`) finds row 1 (`command_type == 11`).
3. `initialise_script_info_char(herald, index)` — resets `info_script`, sets `ch[0] = herald`
   (`SCRIPT_PARAM_CH1` = 100), `ch[1] = mover` (`SCRIPT_PARAM_CH2` = 200), `rm[0] = room`.
4. `run_script(info, row1->next)` — execution starts at **row 2**, not row 1 (the `ON_ENTER` header
   row is never itself interpreted as a command).

Trace of `run_script`:
- **Row 2** (`command_type = 8`, `SCRIPT_IF_IS_NPC`, `param[0] = 200`): `tmpch = get_char_param(200,
  info) = ch[1] = mover`. (`param[1] = 201` is present in the data but `SCRIPT_IF_IS_NPC`'s handler
  never reads `param[1]` — harmless OLC-authored padding.)
  - **If `mover` is an NPC**: `IS_NPC` true → `curr = curr->next` → **row 3**.
  - **If `mover` is a player**: false branch; `curr->next` (row 3) is `SCRIPT_BEGIN`, so
    `curr = get_next_command(row3)` — walks from row 3 until it hits the next `SCRIPT_END`/
    `SCRIPT_END_ELSE_BEGIN` **without recursing further in** (row 4 *is* immediately a
    `SCRIPT_END_ELSE_BEGIN`, so the scan stops there at once) and returns `row4->next` = **row 5**.
    Jump straight to the `SCRIPT_DO_SAY`.
- **Row 3** (NPC path only, `command_type = 1`, `SCRIPT_BEGIN`): no-op, `curr = curr->next` → row 4.
- **Row 4** (NPC path only, `command_type = 5`, `SCRIPT_END_ELSE_BEGIN`): reached by normal forward
  flow (the true/NPC block "finished"), so it calls `get_next_command(row4)` to skip the else-block
  — walks from `row4->next` (row 5, `SCRIPT_DO_SAY`, not an end marker) to `row4->next->next`
  (row 6, `SCRIPT_END` — stop) and returns `row6->next` = `NULL` (row 6 is the last real node; the
  `999` terminator was never linked). `curr = NULL` → loop's post-switch check
  (`script.cpp:1521-1522`) sets `exit = TRUE`. **The herald says nothing when a mob enters.**
- **Row 5** (player path only, `command_type = 30`, `SCRIPT_DO_SAY`, `param[0] = 100`,
  `param[1] = 201`): `txt1 = get_text_param(201 /* SCRIPT_PARAM_CH2_NAME */, info) = GET_NAME(ch[1])`
  = the entering player's name. `sprintf(output, " Lord Thranduil, %s is here seeking an audience.",
  txt1)`. `tmpch = get_char_param(100, info) = ch[0] = herald`. `do_say(herald, output, ...)` — the
  herald mob speaks the line with the player's name substituted for `%s`. `curr = curr->next` → row 6.
- **Row 6** (`command_type = 2`, `SCRIPT_END`): no-op, `curr = curr->next = NULL` → loop ends.

Both paths return `return_value = 1` (never set to `FALSE`) — `ON_ENTER` is a non-gating
notification trigger, so the room-enter itself was never in question here regardless.

### B. Mudlle bytecode trace — program #7701 "Blocking exit north"

Raw source (`lib/world/mdl/77.mdl`):

```
#7701  Blocking exit north
1VI
@stt
vC 1=!Mend i
fh `The Kraken's bulk blocks your way north.`E; Mstt g
@end , Mstt g
```

Any mobile whose `.mob` record sets `store_prog_number` to (the real index for) zone `7701`, with
`MOB_SPEC` *not* set, gets this program bound as `union1.prog_number[0]` at load time, plus a fresh
mudlle stack/list and `CALL_MASK = 255` (`db.cpp:1204-1231`).

**Compile time** (`mudlle_converter`, `mudlle.cpp:1202`): whitespace/newlines vanish, `@stt`/`@end`
become zero-width label definitions recording their compiled byte offsets (call them `α` for `@stt`
and `β` for `@end`; `α` = 3, i.e. right after the 3-char prologue `1VI`), and every `Mxxx` reference
is replaced with a 4-digit zero-padded decimal string of the referenced label's offset. The
backtick text is copied through verbatim. The compiled string is effectively:
`1VI` + `vC1=!` + `{4-digit β}` + `i` + `fh` + `` `The Kraken's bulk blocks your way north.` `` +
`E;` + `{4-digit α}` + `g` + `,` + `{4-digit α}` + `g` (with `β` = the offset of the `,`
immediately after `@end`).

**Run 1 — bootstrap** (whatever call reaches this mob first; typically its own AI tick, `mobact.cpp:136`,
`intelligent(mob, mob, 0, "", SPECIAL_SELF, 0)`): gate passes (`CALL_MASK == 255` covers every
flag). `PROG_POINT` starts at 0.
- `1` → push `1` to stack.
- `V` `I` → `int_fromstack` case `I` (`mudlle.cpp:613`): pop `1`, `CALL_MASK(host) = 1`
  (`SPECIAL_COMMAND` only, from now on).
- (label `@stt`, zero-width, no runtime effect) — execution flows straight on to offset `α`.
- `v` `C` → `int_tostack` case `C` (`mudlle.cpp:202-207`): `wtl` is `0` here, so `value = cmd = 0`;
  push `0`.
- `1` → push `1`.
- `=` → pop `1`, pop `0`; push `(0 == 1)` = `0`.
- `!` → pop `0`; push `!0` = `1`.
- digits (`β`) → push the compiled address of `@end`.
- `i` → pop address `β`, pop condition `1` (truthy) → `PROG_POINT = β - 1` (the loop's own `+1`
  lands exactly on `β`).
- at `β`: `,` → `PROG_POINT++`; **return `FALSE`**. `mobact.cpp`'s `if (intelligent(...)) return;`
  does *not* return early, so normal mob AI continues this pulse. `PROG_POINT` is now saved at
  `β + 1` — the start of the trailing `{4-digit α}g` after the comma.

**Run 2 — a character types `north` in the guard's room**: `interpre.cpp:1393-1394` calls
`special(mover, 1 /* CMD_NORTH+1 */, "", SPECIAL_COMMAND, argument_info, NOWHERE)`, which
(`interpre.cpp:1712`) calls `activate_char_special(guard, mover, 1, "", SPECIAL_COMMAND,
argument_info)` for the guard mobile present in the room → falls to the `union1.prog_number`
branch (`interpre.cpp:1572-1576`) → `intelligent(guard, mover, 1, "", SPECIAL_COMMAND,
argument_info)`. Gate passes (`IS_SET(CALL_MASK=1, SPECIAL_COMMAND=1)` is true). `PROG_POINT`
resumes at the saved `β + 1`:
- digits (`α`) → push `3` (the address of `@stt`).
- `g` → pop `3`; `PROG_POINT = 3 - 1 = 2`, then the loop's `+1` lands back on `α` = 3. (This is the
  "come back and re-check" loop the trailing `,{α}g`/`;{α}g` pairs exist for.)
- `v` `C` → this time `wtl` = `argument_info` is non-null, so `value = wtl->cmd = 1`; push `1`.
- `1` → push `1`.
- `=` → pop `1`, pop `1` → push `(1==1)` = `1`.
- `!` → pop `1` → push `!1` = `0`.
- digits (`β`) → push address of `@end`.
- `i` → pop `β`, pop condition `0` (falsy) → **no jump**; `PROG_POINT` just advances past `i`,
  landing on `f`.
- `f` `h` → `int_tolist` case `h` (`mudlle.cpp:430-438`): `ch` (the mover) is non-null →
  `TO_LIST(host, mover, TARGET_CHAR)`, `TO_STACK(host, 1)`.
- backtick text → `TO_LIST(host, "The Kraken's bulk blocks your way north.", TARGET_TEXT)` — now
  the list head; the mover entry is second.
- `E` → echo (`mudlle.cpp:947-960`): list head is `TARGET_TEXT`, so copy it + `"\n\r"` into `buf`
  and `REMOVE_LIST` it (exposing the mover at the head); list head is now `TARGET_CHAR`, so
  `send_to_char(buf, mover)` — **the mover receives "The Kraken's bulk blocks your way
  north.\n\r"**; `REMOVE_LIST` again empties the list back to the sentinel mark.
- `;` → `PROG_POINT++`; **return `TRUE`**.

Back in `activate_char_special`/`special()`, a `TRUE` return means "handled — suppress the default
action": `may_not_perform |= special(...)` becomes true, and the normal `do_move` north-movement
command is **never executed** — the character is blocked. `PROG_POINT` is now saved just past the
`;`, at the start of the *other* trailing `{4-digit α}g` (the one embedded in the `fh...;` line
itself), so the **next** invocation of this mob (whatever callflag it arrives with, since only
`SPECIAL_COMMAND`/`SPECIAL_DELAY` can reach it now) will likewise `goto @stt` and re-arm the check —
identical mechanism to Run 1's resume, just via the other copy of the `{α}g` pair.

## Open questions

Per the task brief, this doc was expected to resolve three items the data-formats doc deferred:

- **`command_type` param/text contract** — **resolved.** Every action/comparison opcode's exact
  parameter usage is documented in the category tables above, cited to the specific `case` in
  `run_script` (`script.cpp`). The general contract (params are symbolic `SCRIPT_PARAM_*` numbers
  resolved through `get_*_param`/`assign_*_param`, not raw indices; `curr->text` supplies at most
  one `%s`-style substitution via `sprintf(output, curr->text, txt1)`) is described in §3-4.
- **Mudlle language/stack spec** — **resolved.** §1-2 above trace the compiler (`mudlle_converter`)
  and the full interpreter opcode set, cross-checked against `lib/text/mudlle.keys`, with a
  character-by-character worked trace (Worked example B).
- **`script_head.host` semantics** — **resolved as: dead field.** See RotS-specific notes; it is
  never read anywhere in the loader, executor, or OLC. Runtime char/obj/room context comes entirely
  from which trigger function/dispatch site fired, not from data in the script.

Remaining open items from this doc's own research:

- **`ON_HEAR_YELL` has no direct `call_trigger(ON_HEAR_YELL, ...)` call site.** It is only ever
  reached as a second check inside `trigger_char_hear` (`script.cpp:1648-1656`), itself invoked via
  `call_trigger(ON_HEAR_SAY, ...)` from `act_comm.cpp:114`. Whether yell text actually reaches
  `trigger_char_hear` through a separate code path elsewhere (e.g. a yell-specific act.comm function
  not grepped for `call_trigger` directly) was not traced further.
- **`ON_BEFORE_DIE` (`script.h:36`, commented "implemented??" in the source itself) has zero dispatch
  sites anywhere in `src/`.** It appears to be reserved/aspirational rather than live. Not listed in
  `call_trigger`'s `switch` either — a script authored with this trigger type can never fire.
- **Divide-by-zero fallthrough in `int_binary_op`** (`script.cpp:307-313`): the `SCRIPT_SET_INT_DIV`
  case logs the error but has no `break`/`return`, falling into `SCRIPT_SET_INT_RANDOM` and
  returning a random value instead of `0`. Not confirmed whether this is intentional (unlikely,
  given the log message explicitly frames it as an error) or a latent bug.
- **`info_script`'s partial reset on `SCRIPT_DO_WAIT` resume**: `continue_char_script`
  (`script.cpp:1544-1551`) calls `initialise_script_info_char(ch, -1)`, which still runs
  `clear_script_info` and wipes `ch[1]`/`ch[2]`/`ob[*]`/`rm[*]`/`str[*]` before resuming at
  `next_command` — only `ch[0]` is restored (to the owning character) and `ints[*]` survive. A
  script that does `SCRIPT_DO_WAIT` then tries to use `ch2`/`ob1`/`str1` etc. after the wait would
  see them as null/empty. Not confirmed whether any live script relies on (or is broken by) this.
- **Malformed-`Mxxx` recovery program body** (`mudlle.cpp:1286-1292`) was reasoned about but not
  executed/traced — unclear whether it actually surfaces the "Mark not found" text to a player or
  just falls into the "I can't understand my command" default loop.
- **Exact pulse cadence of the heartbeat loop that drains `waiting_list`** (`comm.cpp:598-620`) —
  not measured here; only its existence and mechanism were confirmed. (Related affect-duration
  cadence is documented elsewhere per project memory, but was not re-verified for this doc.)
- **Which live mob(s) actually reference script #1100 or mudlle program #7701** were not identified
  (would require parsing the large `.mob` fixed-column format across all zones) — the worked
  examples trace the engine's behavior generically for "a mobile whose script_number/prog_number is
  set to X," which is sufficient to validate the engine but not to name the specific NPC(s) in the
  live world.
