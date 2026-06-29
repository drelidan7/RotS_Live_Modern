# Text tables & help (`lib/text/`)

**Source files:** `src/db.cpp` (`boot_db:287`, `file_to_string_alloc` calls `:214-321`),
`src/modify.cpp` (`build_help_index:723`), `src/act_info.cpp` (`do_help`/`do_man` dispatch
`:2014-2122`); the chapter table `help_content[]` (`src/consts.cpp:2186`); structs
`help_index_element` (`db.h:197`), `help_index_summary` (`db.h:202`)
**Status:** ✅ complete (verified against the real `lib/text/` files).

## Purpose
Files under `lib/text/` provide the player-facing reference text: the `MAN`/`HELP` pages,
login screens, policies, and the keyword-indexed help system. Two display models coexist:
a handful of files are printed **verbatim** (Category 1), while `help_tbl` **plus seven
`MAN`-chapter `*_tbl` files are parsed into keyword indexes** through `help_content[]`
(Category 2). The indexed `*_tbl` files use the *same* on-disk structure as `help_tbl`.

> ⚠️ Important for a rewrite: `spel_tbl`, `pray_tbl`, `skil_tbl`, `shap_tbl`, `spec_tbl`,
> `msdp_tbl` (and the immortal-only `wizh_tbl`, `scr_tbl`) are **human-readable
> documentation surfaced by `HELP`/`MAN` — not the source of game mechanics.** Actual
> spell/skill/prayer data lives in code (`spells.h`, the `skills[]` array, etc.), to be
> covered in the systems docs and catalogs. Keeping these text files in sync with the code
> is manual.

## Category 1 — verbatim text (slurped via `file_to_string_alloc`)
The whole file is read into a buffer at boot and printed as-is when the relevant command is
used. No structure, no parsing. Reloadable at runtime via `do_reload` (`modify.cpp`/
`db.cpp:204`). Files (`db.h`):

| Constant | Path | Shown by |
|----------|------|----------|
| `HELP_PAGE_FILE` | `text/help` | `HELP` with no argument (`do_help` → `send_to_char(help…)`, `act_info.cpp:2119`) |
| `INFO_FILE` | `text/info` | `INFO` |
| `POLICIES_FILE` | `text/policies` | `POLICY` |
| `HANDBOOK_FILE` | `text/handbook` | immortal handbook |
| `BACKGROUND_FILE` | `text/backgr` | background story |
| (also) | `news`, `credits`, `motd`, `imotd`, `wizlist`, `immlist` | login/`CREDITS`/etc. |

These may contain `act()` color codes (`$CN`…) but are otherwise free text. A rewrite can
store them as plain files or markdown — there is no on-disk schema to match.

> ⚠️ The `*_tbl` MAN files (`spel_tbl`, `pray_tbl`, `skil_tbl`, `shap_tbl`, `msdp_tbl`, and
> `mudl_tbl`) are *also* `file_to_string_alloc`'d into buffers at boot (`spell_tbl`,
> `power_tbl`, `skill_tbl`, `shape_tbl`, `msdp_tbl`, `asima_tbl`; `db.cpp:221-226`), **but
> those buffers are never printed by any command** — they are vestigial. All except
> `mudl_tbl` reach players through the Category 2 index instead. `ASIMA_FILE = text/mudl_tbl`
> is neither indexed (no `help_content[]` chapter) nor displayed, so it is **currently
> orphaned/unreachable** — the only `*_tbl` MAN file with no live read path.

## Category 2 — keyword-indexed help (`help_content[]`) — `build_help_index:723`
The `help_content[]` table (`consts.cpp:2186`) lists **nine chapters**, each a file parsed
into an index of `{keyword, file-offset}` so `HELP`/`MAN` can seek straight to an entry. The
**same `build_help_index` parser and on-disk format apply to every chapter** (including
`help_tbl`). `HELP_KWRD_FILE = text/help_tbl` is chapter 0 (`general`).

| Chapter keyword | File | `imm_only` | Reached by |
|-----------------|------|-----------|------------|
| `general` | `text/help_tbl` | no | `HELP <kw>` (always chapter 0) |
| `spells` | `text/spel_tbl` | no | `MAN SPELLS <kw>` |
| `powers` | `text/pray_tbl` | no | `MAN POWERS <kw>` (mystic prayers) |
| `skills` | `text/skil_tbl` | no | `MAN SKILLS <kw>` |
| `specializations` | `text/spec_tbl` | no | `MAN SPECIALIZATIONS <kw>` |
| `wizard` | `text/wizh_tbl` | **yes** | `MAN WIZARD <kw>` (immortals) |
| `shape` | `text/shap_tbl` | **yes** | `MAN SHAPE <kw>` (immortals) |
| `script` | `text/scr_tbl` | **yes** | `MAN SCRIPT <kw>` (immortals) |
| `msdp` | `text/msdp_tbl` | no | `MAN MSDP <kw>` |

- `HELP <kw>` always searches chapter 0 (`general` = `help_tbl`); `MAN` selects a chapter by
  name (prefix match) then searches it (`do_help`, `act_info.cpp:2032-2106`). `MAN` with no
  chapter lists the chapters; `MAN <chapter>` with no keyword lists that chapter's topics.
- **`imm_only` gates the whole chapter**: `wizard`, `shape`, and `script` are hidden from —
  and unsearchable by — players below `LEVEL_IMMORT` (`act_info.cpp:2042-2044,2053`).
- `text/mudl_tbl` (the `ASIMA` mudlle docs) is **not** a chapter (see the Category 1 note).

### Format
```
<keyword> [<keyword> ...]          <- line 1 of an entry = whitespace-separated keywords
<body line>
<body line>
...
#                                  <- a line beginning with '#' ends the entry
<keyword> [<keyword> ...]
<body...>
#
...
#~                                 <- '#~' ends the file
```
Parsing rules (`build_help_index:731-763`):
- For each entry, the file offset (`ftell`) of the **keyword line** is recorded against
  **every** keyword on that line (one entry can be reached by several keywords).
- The body is everything until a line whose first char is `#`.
- A `#` line alone separates entries; a line beginning `#~` terminates the file.
- The index is sorted alphabetically by keyword after load (`:764-775`).
- Display: `HELP foo` seeks to the recorded `pos` and shows text up to the next `#`.

### `help_index_element` (`db.h:197`)
```c
struct help_index_element { char *keyword; long pos; };
```

## Worked example (`help_tbl`)
```
flee retreat
Leave combat in a random direction.  You may lose your footing.
Syntax:  flee
#
hide sneak
Conceal yourself.  Requires the hide skill.
#
#~
```
`HELP flee`, `HELP retreat`, `HELP hide`, and `HELP sneak` all resolve via the index.

## Open questions
- *(Resolved)* `help_content[]` = nine chapters (`consts.cpp:2186`); `help_index_summary`
  fields `keyword`/`descr` drive the `MAN` chapter listing, `filename` is the indexed file,
  `imm_only` gates the chapter. See Category 2.
- *(Resolved)* No `MAN`/help page is parsed elsewhere for game data — all are display-only;
  the per-file verbatim buffers for the `*_tbl` MAN files are loaded but unused.
- Why `mudl_tbl` (ASIMA) is still loaded though no command reads it — likely a removed
  command; safe to treat as dead for a rewrite.
