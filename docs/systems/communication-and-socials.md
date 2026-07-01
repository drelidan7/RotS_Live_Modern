# Communication & Socials

**Source files:** in-room speech `src/act_comm.cpp` (`do_say`/`say_to_char:36-116`,
`do_gsay:124-169`, `do_tell:171-271`, `do_whisper:273-292`, `do_ask:294-313`, `do_write:317-390`,
`do_page:392-447`, `do_gen_com:482-652`, `do_afk:755-773`, `do_pray:775-816`); the act() message
engine `src/comm.cpp` (`convert_string:1888-2041`, `act:2044-2082`); language selection
`src/act_othe.cpp` (`do_language:1409-1449`), race→language assignment `src/spec_pro.cpp:157-191`;
socials runtime `src/act_soci.cpp` (`social_parser:216-258`, `do_action:262-316`, `do_insult:318-364`);
socials/messages **file format** is `docs/data-formats/socials-and-messages.md` (not duplicated here);
boards `src/boards.cpp`, `src/boards.h`; mail `src/mail.cpp`, `src/mail.h`; command dispatch/position
gating `src/interpre.cpp` (`command[]:299-...`, `COMMANDO` table, `report_wrong_position:620`);
colors `src/color.h`.
**Status:** 🟡 Core act() engine, in-room comms, channels, socials runtime, boards, and mail are
fully traced to source and cross-checked against live `lib/misc`/`lib/boards` data. A few $-codes
and one legacy code path have no observed call sites (see Open questions).

---

## Purpose
This is the "how do two players talk to each other" document: in-room speech (say/emote/whisper/
ask/insult), the tell/group-tell/yell/narrate/chat/sing channels, the race-language comprehension
system that can garble speech, the `act()` string-substitution engine that every one of the above
(and all combat/skill flavor text) is built on, the social-command runtime that plays back
`lib/misc/socials`, and the two persistent message stores — bulletin boards (`lib/boards/`) and
mudmail (`lib/misc/plrmail`). The on-disk *formats* for socials and combat messages are already
documented in `docs/data-formats/socials-and-messages.md`; this document covers the runtime that
interprets them, plus everything else players use to talk.

---

## Data structures

### `social_messg` (`db.h:223`, format detailed in socials-and-messages.md)
One record per social command (`smile`, `wave`, …): `command`, `hide`, `min_victim_position`,
`min_actor_position`, and the eight message-template strings (`char_no_arg` … `others_auto`).
Loaded once at boot into a flat, alphabetically-sorted array `soc_mess_list[]`
(`act_soci.cpp:40,144-158`) searched by binary search (`find_action:187-212`).

### `board_info_type` / `mail_info_type` (`boards.h:38-87`)
Per-board state: `vnum`/`rnum` (the board *object*), `read_lvl`/`write_lvl`/`remove_lvl`, an
in-memory `msg_index[]` of `board_msginfo` (heading pointer, poster level, post time, a slot number
into the global `msg_storage[]` text pool), and virtual `write_message`/`approve_msg` so
`mail_info_type` can override "which messages does this reader see" (filtered by recipient) while
reusing the rest of the board machinery. `board_msginfo` itself: `slot_num`, `msg_num`, `heading`,
`level`, `post_time`, `heading_len`, `message_len` (`boards.h:28-35`).

### Mail file records (`mail.h:87-116`)
Flat allocation-block file (`lib/misc/plrmail`, `MAIL_FILE` = `misc/plrmail`, `db.h:68`), block size
100 bytes (`BLOCK_SIZE`, `mail.h:39`). Two block types share the 100-byte slot:
- `header_block_type_d`: `long block_type` (`-1`=header), `long next_block` (link to a continuation
  data block, or `-2`/`LAST_BLOCK` if the message fits in one block), `char from[16]`, `char to[16]`
  (both `NAME_SIZE+1=16`), `long mail_time`, `char txt[56]` (`HEADER_BLOCK_DATASIZE+1`).
- `data_block_type_d`: `long block_type` (a byte offset link to the next block, or `-2`/`LAST_BLOCK`),
  `char txt[96]` (`DATA_BLOCK_DATASIZE+1`).
A deleted block is overwritten with `block_type = -3` (`DELETED_BLOCK`) and its offset is pushed onto
an in-memory free list (`push_free_list`/`pop_free_list`, `mail.cpp:118-140`) for reuse. There is
**no per-player mail file** — every player's mail lives in this one shared, block-allocated file,
indexed in memory at boot by `scan_file()` (`mail.cpp:239-275`) into a linked list keyed by lower-cased
recipient name (`mail_index_type`, `mail.h:108-116`).

### `waiting_type` targeting (`wtl`)
Comms that support "smart" (pre-resolved) targeting — say, tell, reply, emote, socials — read a
pre-parsed target out of `wtl->targ1`/`targ2` (`TARGET_TEXT`, `TARGET_CHAR`, …) when the client sent
one (e.g. via a GUI target picker), and fall back to plain argument parsing otherwise. This appears
throughout `act_comm.cpp`/`act_soci.cpp` as `if (!wtl || wtl->targ1.type != TARGET_...) { parse text }
else { use wtl->targ1.ptr... }`.

---

## Format / Algorithm

### 1. In-room comms (`act_comm.cpp`)

| Command | Function | Min position (table) | Target | Notes |
|---|---|---|---|---|
| `say` | `do_say` (`:80-116`) | `POSITION_RESTING` (`interpre.cpp:1765`) | whole room | Runs each listener through the **language comprehension** check (below); NPCs get an `ON_HEAR_SAY` trigger call (`:113-114`) |
| `emote` | `do_emote` (`act_wiz.cpp:114-137`) | `POSITION_DEAD` / `POSITION_SLEEPING` (two table entries, `interpre.cpp:1785,1944`) | whole room | `$n <text>` to `TO_ROOM`; because `TO_ROOM` excludes the actor (see act() below), a second `TO_CHAR` call fires only if `PRF_ECHO` is set, otherwise the actor just gets "Ok." |
| `whisper` | `do_whisper` (`:273-292`) | `POSITION_RESTING` | one room target | No language garble; a self-whisper is a special-cased joke message |
| `ask` | `do_ask` (`:294-313`) | `POSITION_RESTING` | one room target | Same shape as whisper (question framing) |
| `insult` | `do_insult` (`act_soci.cpp:318-364`) | `POSITION_RESTING` (`interpre.cpp:1779`) | one room target | Not part of the `lib/misc/socials` file — three canned insults chosen by `random() % 3` |
| `write` | `do_write` (`:317-390`) | `POSITION_RESTING` (`interpre.cpp:1900`) | a held pen + a note object | Opens the line editor on the note's `action_description`; unrelated to boards/mail but shares the "in a proper position, has the tools" gating style |

None of `say`/`whisper`/`ask`/`emote` check `GET_INVIS_LEV` or call `stop_hiding` — only the
global channels (§2) do (see "Hide/invis interaction" below).

### 2. Language & speech comprehension (RotS-specific — `act_comm.cpp:36-78`, `spec_pro.cpp:157-191`, `act_othe.cpp:1409-1449`)

Every character has a **spoken language** (`ch->player.language`, one of `LANG_BASIC`=0,
`LANG_ANIMAL`=121, `LANG_HUMAN`=122, `LANG_ORC`=123 — `structs.h:800-803`) and, independently, a
**knowledge score 0-100** in each of those three as ordinary practiced skills named `"animal
language"`, `"human language"`, `"orcish language"` (`PROF_GENERAL`, skill ids 121-123,
`consts.cpp` skill table entries 121-123) that anyone can train regardless of profession.

- **Assignment at creation** (`spec_pro.cpp:157-191`): Human/Dwarf/Wood-Elf/Hobbit/High-Elf →
  `LANG_HUMAN`; **Beorning → `LANG_ANIMAL`** (the *only* free-peoples race not on the human tongue);
  Uruk/Orc/Magus/Olog-Hai/Haradrim/Harad → `LANG_ORC`; God/Easterling/default → `LANG_BASIC`. Your
  own starting language is auto-set to 100 knowledge (`SET_KNOWLEDGE(ch, tmp, 100)`); the other two
  languages start at 0 and must be practiced like any other skill. **Exception: `RACE_GOD`**
  additionally gets *all three* language skills set to 100 knowledge outright
  (`spec_pro.cpp:193-195`, a separate loop keyed on `GET_RACE(ch) == RACE_GOD`), so a god character
  understands every tongue at full fluency from creation, not just the no-garble `LANG_BASIC` case.
- **Garble algorithm** (`say_to_char`, `:36-78`, used by `do_say` and — via `do_gen_com` — by
  **yell, narrate, chat, and sing too**, so *every public speech channel* is subject to this, not
  just `say`):
  - If the speaker's `player.language` is `LANG_BASIC` (0, immortal-only "common language" —
    `do_language` refuses to set it below `LEVEL_IMMORT`, `act_othe.cpp:1421-1428`), there is
    **no garble at all** (`freq = 100`).
  - Otherwise `freq = GET_RAW_KNOWLEDGE(speaker, lang)` (NPC speakers are always treated as 100%
    fluent, `:46-48`), and if the *listener* is a PC it is further scaled:
    `freq = freq * GET_RAW_KNOWLEDGE(listener, lang) / 100` (`:51-52`). A listener with 0 knowledge
    of the speaker's language hears **fully garbled** text; a listener at 50% has each alphabetic
    character independently replaced with a random letter of the same case with `(100-freq)%`
    probability (`strcpy_lang`, `utility.cpp:1392-1412`), separately per-listener (each observer in
    the room gets their own randomized copy).
  - **Because Beorning is the only free-peoples race on `LANG_ANIMAL`**, a fresh Beorning is
    unintelligible to Human/Dwarf/Elf/Hobbit allies (and vice versa) until someone trains the other
    side's language — a non-obvious in-faction comprehension gap. All servant races share
    `LANG_ORC`, so there is no analogous gap on the dark side.
- **Changing your spoken language**: `set language <name>` (dispatched through `do_set`'s menu,
  `act_othe.cpp:1409-1449`, reached as option 14 of `change_comm[]`) sets which of the 3 tongues (or,
  for immortals, common) *you speak*; it does **not** grant you comprehension of others — that is
  purely the practiced knowledge score above.

### 3. Channels

| Command | Function | Scope | Faction-gated? | Move/mana cost | Toggle to use it |
|---|---|---|---|---|---|
| `tell` / `reply` | `do_tell` (`:171-271`), `SCMD_TELL`/`SCMD_REPLY` | one player, world-wide | yes — `other_side(ch,vict)` blocks it (`:240`) | none | `PRF_NOTELL` on the **sender** blocks sending ("You can't tell other people while you have notell on."); `PRF_NOTELL` on the **target** *also* blocks delivery — the check `(GET_POS(vict) <= POSITION_SLEEPING) \|\| (!IS_NPC(vict) && PRF_FLAGGED(vict, PRF_NOTELL))` (`:251`) sends the actor "$E can't hear you." and never delivers the tell if either the victim is asleep-or-lower **or** has `PRF_NOTELL` set |
| `gtell` (group tell) | `do_gsay` (`:124-169`) | your `char_data::group` members only | implicit (groups can't cross factions) | none | none — always available if grouped |
| `yell` | `do_gen_com`, `SCMD_YELL` | everyone in the **same zone** (`world[...].zone` match, `:616`), **not** room-limited | **no** — no `other_side` check for yell; enemies in the same zone overhear it | none | `PLR_NOSHOUT` (admin mute) blocks it; no PRF toggle gate (`channels[SCMD_YELL] = -1`, `:453-458`) |
| `narrate` | `do_gen_com`, `SCMD_NARRATE` | world-wide, own faction only | **yes** — `other_side(i->character, ch)` skips the other side (`:627`) | none | `PRF_NARRATE` must be on (`:536-540`); toggled via `set narrate` |
| `chat` | `do_gen_com`, `SCMD_CHAT` | world-wide, own faction only | yes, same as narrate | none | `PRF_CHAT` must be on; `set chat` |
| `sing` | `do_gen_com`, `SCMD_SING` | world-wide, own faction only | yes, same as narrate | none | `PRF_SING` must be on; toggled the same way as narrate/chat via `set sing` — `"sing"` **is** listed in `change_comm[]` (index 18, `act_othe.cpp:1458`) and dispatches to `do_gen_tog(ch, arg, wtl, 0, SCMD_SING)` (`act_othe.cpp:1591-1592`) |
| `pray` (no target) | `do_pray` (`:775-816`), no argument | room only | n/a | none | Flavor-only; "You raise your prayers to the sky." |
| `pray <player>` | `do_pray`, targeted | that player (in-world, must pass `other_side`/`get_char_vis`) | yes | none | **Not** a channel to immortals — it is a social-style roleplay message to another *player character* ("hoping for $S good will"); if the praying character is itself an NPC with a `master` (a charmed pet), the target's faction is checked against the **master's** faction (`other_side(ch->master, tar_ch)`) *in addition to* the pet's own (`other_side(ch, tar_ch)`) — both must pass (`act_comm.cpp:790-797`) |

RotS **renamed** two stock-CircleMUD channels: the source comments still call out the old identity —
`"chat", /* gossip */` and `"narrate", /* auction */` (`interpre.cpp:483-484`) — and `"yell", /* shout */`
(`:317`). Functionally chat/narrate/sing are one mechanism (`do_gen_com`) differentiated only by
their `PRF_*` toggle and color code; yell is the odd one out (zone-scoped, not faction-gated, no
PRF toggle).

**Immortal side-broadcast prefixes** (`do_gen_com`, `:561-591`, immortals only, narrate/chat/sing
only — not yell): typing `-w`/`-l` (Light), `-d` (Dark/Uruk), `-m` (Magi/Uruk-Lhuth), or `-a` (All of
Arda) before the message overrides which faction the fan-out targets (`imm_to_race`/`imm_side`,
compared via `other_side_num`, `:624-626`), and the immortal's own echo gets a `" to the Light."` /
`" to the Dark."` / etc. suffix (`imm_side_message[]`, `:492-498`).

**Every global-channel command breaks stealth**: `do_gen_com` calls `stop_hiding(ch, TRUE)`
(`:534`) before sending — yelling/narrating/chatting/singing reveals a hidden character. `say`,
`tell`, `whisper`, `ask`, and `gsay` do **not** call `stop_hiding`.

**Other utility comms in this file** (not full channels): `page`/`beep` (`do_page`, `:392-447`,
immortal-to-anyone or "page all" broadcast to un-logged-in-menu descriptors) and `alias` (`do_alias`,
`:654-753`, a client-side command-macro store, capped at `MAX_ALIAS`, unrelated to speech).

### 4. The `act()` message engine (`src/comm.cpp:1888-2082`) — the core rewrite contract

`act()` is the single formatting/fan-out primitive used by **every** message above, every combat hit
line, every social, and virtually all flavor text in the codebase.

```
void act(const char *str, int hide_invisible, struct char_data *ch,
         struct obj_data *obj, void *vict_obj, int type, char spam_only = 0);
```

- `str` — a template string containing `$`-codes (table below).
- `hide_invisible` — if true, a would-be recipient who **cannot see `ch`** (`CAN_SEE(to, ch)`) is
  skipped entirely, rather than just seeing a degraded name.
- `ch` — the actor. `obj` — the "primary" object (`$o`/`$p`/`$a`/`$b`). `vict_obj` — a `void*` that
  is reinterpreted per-code: usually a `char_data*` victim (`$N`/`$M`/`$S`/`$E`/`$O`/`$P`/`$A`/`$B`),
  but for the door-keyword code (`$F`) it is a raw `char*`, and for `$T` it is used as a literal
  `char*` (verbatim, no lookup).
- `type` — one of `TO_ROOM`(0) / `TO_VICT`(1) / `TO_NOTVICT`(2) / `TO_CHAR`(3) (`comm.h:43-46`).
- `spam_only` — if true, only recipients with `PRF_SPAM` set receive the message (used to suppress
  high-frequency combat spam for players who don't want it).

**Recipient resolution and per-recipient gating** (`act:2054-2082`):

| `type` | Recipient set | Notes |
|---|---|---|
| `TO_CHAR` | `ch` only | The only mode where `to == ch` is allowed through |
| `TO_VICT` | `vict_obj` (cast to `char_data*`) only | Delivered even if the victim is **asleep** (`AWAKE(to) \|\| type == TO_VICT`, `:2072`) — sleeping characters still "hear" a `TO_VICT` message |
| `TO_ROOM` | everyone in `ch`'s room **except `ch` itself** | `to != ch` is required unless `type == TO_CHAR` (`:2071`) — this is why every emote/social needs a *separate* `TO_CHAR` call to echo to the actor |
| `TO_NOTVICT` | everyone in `ch`'s room except `ch` **and** except `vict_obj` | `:2073` |

For every candidate recipient, ALL of the following must hold: has a live descriptor
(`to->desc`), passes the `type`/`ch` identity rule above, `CAN_SEE(to, ch) || !hide_invisible`,
is awake (or the message is `TO_VICT`), is **not** `PLR_WRITING` (mid-string-editor — boards/mail/
notes), is not the excluded `TO_NOTVICT` victim, and (if `spam_only`) has `PRF_SPAM` set.

**$-code substitution table** (`convert_string`, `comm.cpp:1899-2013`):

| Code | Expands to | Visibility gating |
|---|---|---|
| `$n` | `ch`'s displayed name (`PERS(ch, to, cap=false, force_visible=false)`) | `PERS` shows "someone" if `to` can't see `ch`; shows the enemy race's `*starred*` name (colored `COLOR_ENMY`) if `other_side(to, ch)` |
| `$N` | `vict_obj`'s displayed name (`PERS`, same rules) | as above, relative to `to` seeing `vict_obj` |
| `$K` | `vict_obj`'s name, but **force-visible** (`PERS(..., force_visible=TRUE)`) | used by `say_to_char` for **narrate/chat/sing** (`do_gen_com` passes `force_visible=TRUE` for those three, `act_comm.cpp:633`) so the speaker's name is never hidden there; **`yell` is the exception** — it passes `force_visible=FALSE` (uses plain `$N`, `act_comm.cpp:619`), so a yelling character who fails `CAN_SEE` still renders as "someone" to listeners |
| `$m` / `$M` | him/her/it for `ch` / `vict_obj` (`HMHR`) | none (pronoun only reflects `player.sex`) |
| `$s` / `$S` | his/her/its (possessive, `HSHR`) | none |
| `$e` / `$E` | he/she/it (`HSSH`) | none |
| `$o` / `$O` | `obj` / `vict_obj`-as-object short name via `fname()` (bare keyword, no article) | "something" if `!CAN_SEE_OBJ(to, obj)` (`OBJN`) |
| `$p` / `$P` | `obj` / `vict_obj`-as-object **full short description** | "something" if not visible (`OBJS`) |
| `$a` / `$A` | "a"/"an" article for `obj` / `vict_obj`-as-object, by first-letter vowel test (`SANA`) | none |
| `$T` | `vict_obj` reinterpreted as a raw `char*`, inserted **verbatim** | none — caller-supplied literal text, e.g. arbitrary skill/spell flavor fill-ins |
| `$F` | `vict_obj` reinterpreted as `char*`, passed through `fname()` | none — used for **door/exit keywords** (`act_move.cpp:1118,1228,1343,1410`) |
| `$b` / `$B` | current body-part name for `ch` / `vict_obj` (`GET_CURRPART`, indexes `bodyparts[race][current_bodypart]`) | none |
| `$CN`/`$CC`/`$CY`/`$CT`/`$CS`/`$CR`/`$CH`/`$CD`/`$CK`/`$CO`/`$CE`/`$CG` | per-recipient ANSI color for Narrate/Chat/Yell/Tell/Say/Room/Hit/Damage/Character/Object/Description/Group-tell (`CC_USE(to, COLOR_*)`) | Empty string if `to` has `PRF_COLOR` off; each player can remap which physical color each slot uses |
| `$$` | literal `$` | none |

After substitution the engine also re-applies the color code that a `$n`/`$N` PERS lookup may have
"clobbered" mid-string (`clobbered_color`/`used_color`, `:2018-2023` — a `$C..` color set before a
`$n`/`$N` needs restating afterward since `PERS` doesn't preserve surrounding ANSI state), appends
`\n\r`, and capitalizes the first non-ANSI character of the finished line (`:2039-2040`).

### 5. Socials runtime (`act_soci.cpp`)

Socials are **not** individually registered in the main command table. `command_interpreter`
(`interpre.cpp:1341-1348`) falls back to `find_action()` (binary search over the sorted
`soc_mess_list[]`) for any input word that doesn't match a real command; a hit sets
`cmd = CMD_SOCIAL` (`interpre.h:268`, value `22`), which the table maps to `do_action`
(`COMMANDO(22, POSITION_DEAD, do_action, ...)`, `interpre.cpp:1775`).

`do_action` resolution (`:262-316`):
1. Resolve the social record either from a pre-parsed `wtl` (if `wtl->cmd == CMD_SOCIAL`) or by
   re-running `social_parser` on the raw argument (`:216-258`) — which does a case-preserving lookup
   of the command word via `find_action`, then tries to resolve the rest of the line as an in-room
   visible target via `get_char_room_vis`.
2. **Actor position check**: `GET_POS(ch) < action->min_actor_position` → `report_wrong_position(ch)`
   (a position-specific refusal message, `interpre.cpp:620`) and abort. This runs before any
   branching below, so it applies uniformly to no-target, self-target, and other-target socials.
3. **No target given** → send `char_no_arg` to the actor, `act(others_no_arg, hide, ...)` to the room.
4. **Target is the actor** (`vict == ch`) → `char_auto` to actor, `act(others_auto, hide, ...)` to
   the room.
5. **Target found, and target's position** `< action->min_victim_position` → the actor alone gets
   `"$N is not in a proper position for that."`; **no message is broadcast to the room or the
   target** in this case.
6. **Target found, valid position** → three separate `act()` calls fan the event out: `char_found`
   to the actor (`TO_CHAR`, `hide=0` — always visible to the person who typed the social), 
   `others_found` to the room minus target (`TO_NOTVICT`, honoring the record's `hide` flag), and
   `vict_found` to the target (`TO_VICT`, honoring `hide`).

`do_insult` (`:318-364`) is a **hardcoded** social with the same room-target shape but is not
file-driven — three canned lines chosen by `random() % 3`. Only the first (`case 0`) is gender-aware,
branching on both `GET_SEX(ch)` and `GET_SEX(victim)` to pick one of four lines ("fighting like a
woman" / "women can't fight" / "smallest... (brain?)" / beauty-contest-against-a-troll); the other two
(`case 1` and the `default`) are fixed text regardless of either party's sex.

### 6. Boards (`src/boards.cpp`, `src/boards.h`)

- **One board object per board.** `init_boards()` (`:133-205`) hand-registers 24 boards
  (`NUM_OF_BOARDS`, `boards.h:16`), each tied to a specific object vnum, with independent
  `read_lvl`/`write_lvl`/`remove_lvl` (e.g. the general News Board, vnum 1104, is world-readable but
  needs `LEVEL_GOD+1` (94) to post; several immortal/coder boards need `LEVEL_GRGOD`/`LEVEL_GOD`
  (97/93) just to read). All are wired via `SPECIAL(gen_board)` on the object (`spec_ass.cpp:218-241`).
  `find_board()` locates whichever board object is in the player's current room (`:117-130`).
- **Commands**: standing at a board object, `look board`/`look board all`, `read <n>`/`read next`/
  `read last`, `write <headline>` (opens the line editor via `string_add_init`), and `remove <n>`
  (only your own post unless `GET_LEVEL(ch) >= LEVEL_AREAGOD`, `:676`) all route through
  `gen_board()` (`:207-334`), which maps the *object* command (look/read/write/remove/next) onto the
  right `board_info_type` method.
  - `write_message` (`:336-402`) refuses below `write_lvl`, refuses if the board is full
    (`num_of_msgs >= max_of_msgs`), auto-tags an `IMM`-prefixed headline as literal text `"Imm"` for
    below-`LEVEL_IMMORT` posters (`:367-370` — so mortals can't fake an immortal-only heading), and
    stamps the heading with a timestamp + `(poster name)`.
  - Reading is filtered by `approve_msg` (`:404-416`): only messages numbered above the reader's
    per-board "last read" pointer are shown unless `all` is requested, and immortal-tagged headings
    are hidden from sub-immortal readers.
  - **Persistence**: each board saves to `lib/boards/<short_name>.boa` (binary: message count +
    `board_msginfo[]` index, then heading/text blobs back-to-back, `save_board`/`load_board`,
    `:717-890`), plus a parallel human-readable `.html`/`.index` pair for out-of-game browsing
    (`BOARD_HTML_DIR = "boards"`, same directory).
- Verified live: `lib/boards/` contains the expected `<name>.boa`/`.html`/`.index` triples (e.g.
  `general.boa`, `immort.boa`, `coders.boa`, `Alkar.boa`, `boa11.boa`…`boa18.boa`) matching the
  `short_name` strings in `init_boards()`.

**Dead code inside boards.cpp**: `mail_info_type* mail_board` (`:96,189`) and the `SCMD_MAIL`/
`CMD_SEND` branches of `gen_board()` (`:242-265,284-287`) implement a second, board-based mail
system, but nothing in the live command table ever sets `wtl->subcmd = SCMD_MAIL` or dispatches
`CMD_SEND` — the only board command actually wired to a real player command is `SCMD_NEWS`
(`"news"` → `do_board` → `gen_board(..., SCMD_NEWS, ...)`, `interpre.cpp:1803`). The comment in
`boards.cpp:1101-1104` confirms this directly: *"The old report_mail function has been replaced
with has_mail from the new mail system."* All real player mail goes through `mail.cpp` (§7), not
this board-based path.

### 7. Mail (`src/mail.cpp`)

- **No standalone `mail` command** — `"mail"`/`"check"`/`"receive"` (command words 155/153/154) are
  all bound to `do_not_here` in the main table (`interpre.cpp:2040-2044`); they only function when a
  **postmaster mob** (`SPECIAL(postmaster)`, assigned to mob vnum 1118 — coincidentally also object
  vnum 1118's board — `spec_ass.cpp:87`) intercepts the raw command number in its special-procedure
  hook and calls one of `postmaster_send_mail` / `postmaster_check_mail` / `postmaster_receive_mail`
  (`mail.cpp:503-525`).
- **Send** (`postmaster_send_mail:539-594`): requires `GET_LEVEL(ch) >= MIN_MAIL_LEVEL` (10),
  charges `STAMP_PRICE` (200 = 2 silver, waived for immortals) in gold, resolves the recipient via
  `find_name` (must be a registered player), then opens the line editor on a `PLR_MAILING |
  PLR_WRITING`-flagged buffer; when the editor terminates, `store_mail(to, from, text)` is invoked
  (per the install-notes header comment, wired into `modify.cpp`'s string-editor completion path)
  and writes the message into the shared block file.
- **Check** (`postmaster_check_mail:596-613`): reports yes/no via `has_mail()` (index lookup only,
  no content read).
- **Receive** (`postmaster_receive_mail:615-662`): for every pending message, `read_delete()`
  (`:387-496`) is called, which (a) reads and concatenates the header block + any linked continuation
  data blocks, (b) marks each consumed block `DELETED_BLOCK` and returns it to the free list, and
  (c) the caller wraps the resulting text in a freshly-allocated `ITEM_NOTE` object (short desc "a
  letter", cost 30/upkeep 10) dropped into the recipient's inventory — mail is read by **taking
  physical letter objects and reading them**, not via an inbox UI. The letter's header text differs
  by the *reader's* current alignment (`is_good = GET_ALIGNMENT(ch) > 0`): a "Postal Service of
  Gondor" banner for good-aligned readers vs. a "Subversive Messaging System" banner otherwise —
  flavor only, does not affect delivery.
- **Persistence** verified live: `lib/misc/plrmail` is 171,400 bytes = exactly 1,714 × 100-byte
  blocks (`BLOCK_SIZE`), confirming the on-disk block layout. Decoding block 21 (little-endian
  fields) yields a readable `header_block_type`: `block_type=-1`, `next_block=-2` (single-block
  message), `from="Radasul"`, `to="ceana"`, a plausible Unix `mail_time`, and `txt="Hey girl,
  sup?      \n\r"` — confirming the struct layout in §Data structures against real data.

### 8. AFK / hide / invis interactions with comms

- **AFK** (`PLR_ISAFK`): set explicitly by `afk` (`do_afk`, `:755-773`, which also plays a
  Big-Brother anti-AFK-grief hook) or automatically by the idle timer (`limits.cpp:517-527`);
  cleared automatically the moment the player next types **any** command (`interpre.cpp:1260-1261`).
  It is purely advisory: `do_tell` shows the sender `"$E is away from keyboard."` (`:254-255`) but
  **still delivers the tell**; no other comm command checks it.
- **Invisibility / wizinvis**: `do_gen_com` (yell/narrate/chat/sing) refuses outright while
  `GET_INVIS_LEV(ch) > 0` unless the speaker is `>= LEVEL_AREAGOD` (`:529-532`, `"You cannot do this
  when invisible."`); `say`/`tell`/`whisper`/`ask`/socials have no such check — an invisible player
  can use all of those normally, and recipients who fail `CAN_SEE` simply see a degraded `$n`
  (`"someone"`) via `act()`'s `hide_invisible` gating (§4), except where a call explicitly passes
  `force_visible` (global-channel headers use `$K`, not `$n`/`$N`, precisely so speaker/target names
  are never hidden there).
- **Hiding/sneaking**: only the global channels call `stop_hiding(ch, TRUE)` (§3); in-room speech
  and socials never break hide/sneak state.
- **No ignore-list feature.** There is no per-target "ignore" mechanism for tells or any other comm
  in this codebase (a grep for `ignore` across the comm-related files turns up nothing beyond a
  code comment and an unrelated combat-message string). The only blanket controls are the sender-side
  `PRF_NOTELL` toggle (blocks all outgoing tells) and the admin-only `PLR_NOSHOUT` mute flag
  (toggled via wizard command, `act_wiz.cpp:2200`; checked at the top of `do_gen_com`, `:500-503`,
  blocking yell/narrate/chat/sing entirely for that player).

---

## RotS-specific notes

- **Language-based speech garbling is a RotS addition** absent from stock CircleMUD/DikuMUD, layered
  onto every public speech channel (say/yell/narrate/chat/sing) via the shared `say_to_char` helper,
  driven by three ordinary practiceable "language" skills plus a race-assigned native tongue. This
  interacts with the race/faction system (`docs/systems/races.md` §1.8): Beorning is a comprehension
  outlier within its own (Free peoples) faction.
- **Channel renames**: `chat`/`narrate` are the stock `gossip`/`auction` channels under new names
  (comments in `interpre.cpp:483-484` retain the old identity); `yell` is the renamed `shout`
  (`:317`). Unlike stock CircleMUD's flat gossip/shout model, RotS layers the **race-war faction
  gate** onto chat/narrate/sing (but pointedly *not* onto yell, which is meant to be locally
  overheard by enemies) and adds the immortal `-w/-l/-d/-m/-a` side-targeting prefix system.
  `PLR_NOSHOUT` genuinely mutes *all four* of yell/narrate/chat/sing, not just yell, despite the name.
- **`pray` is not an immortal-petition channel here** — with no argument it's a solo flavor emote;
  with an argument it's a social-style message to another **player**, gated by the same
  faction (`other_side`) check as everything else. There is no distinct "petition" mechanic wired
  through this file (`do_petitio`, `:31-34`, just tells the player to type the full word "petition").
- **`act()`'s `TO_ROOM` excludes the actor** — this is stock CircleMUD behavior, but it is easy to
  get wrong in a rewrite (it's why nearly every emote/social/give/etc. needs a paired `TO_CHAR` call).
- **The board/mail split is a maintenance artifact, not a design choice**: `boards.cpp` ships a
  complete, unused second "board-flavored" mail implementation (`mail_info_type`, `SCMD_MAIL`/
  `CMD_SEND`) alongside the real, separately-written `mail.cpp` block-file system. A rewrite should
  implement only the latter.
- **Mail delivery is physical-object based**, not an inbox: `receive` manufactures one `ITEM_NOTE`
  per pending message that the player must carry and `read`. This has gameplay consequences (mail
  can be lost if the letter object decays/is dropped) worth preserving or deliberately changing in a
  rewrite.

---

## Worked example

`perform_give` (`src/act_obj1.cpp:549-578`, backing the `give` command) fires the same logical event
to three different audiences with three different `act()` calls — a clean, real illustration of the
engine's `TO_CHAR`/`TO_VICT`/`TO_NOTVICT` fan-out and `$`-code resolution:

```cpp
act("You give $p to $N.",   FALSE, ch, obj, vict, TO_CHAR);    // :569
act("$n gives you $p.",     FALSE, ch, obj, vict, TO_VICT);    // :570
act("$n gives $p to $N.",   TRUE,  ch, obj, vict, TO_NOTVICT); // :571
```

Say Aragorn (`ch`) gives *a shadowy blade* (`obj`, `short_description = "a shadowy blade"`) to
Éowyn (`vict = vict_obj`), in a room that also contains Gimli (sighted) and Frodo (currently wearing
an amulet of invisibility Aragorn can't see through):

| Call | Recipient(s) | `$`-code resolution | Rendered text |
|---|---|---|---|
| Line 1, `TO_CHAR` | Aragorn only (`to == ch`, the one case that's allowed) | `$p` → `OBJS(obj, Aragorn)`: Aragorn holds/can see the blade → `"a shadowy blade"`. `$N` → `PERS(Éowyn, Aragorn, ...)`: Aragorn can see her → `"Éowyn"` | `"You give a shadowy blade to Éowyn."` |
| Line 2, `TO_VICT` | Éowyn only | `$n` → `PERS(Aragorn, Éowyn, ...)` → `"Aragorn"`. `$p` → `OBJS(obj, Éowyn)`: she can see it → `"a shadowy blade"` | `"Aragorn gives you a shadowy blade."` |
| Line 3, `TO_NOTVICT`, `hide_invisible=TRUE` | Gimli only — Frodo is excluded because `hide_invisible=TRUE` and he **can't see** Aragorn (assume Aragorn has no invis; flip the example if needed — the point is the gate exists) | For Gimli: `$n` → `"Aragorn"`, `$p` → `OBJS(obj, Gimli)` → `"a shadowy blade"` (Gimli can see it), `$N` → `PERS(Éowyn, Gimli, ...)` → `"Éowyn"` | Gimli sees: `"Aragorn gives a shadowy blade to Éowyn."` — a hypothetical blind/can't-see observer would instead be **skipped entirely** (not shown "someone gives something to someone") because `TO_NOTVICT` with `hide_invisible=TRUE` filters at the recipient-selection stage, not just at the per-code substitution stage |

If Aragorn and Éowyn were on opposite sides of the race war (`other_side` true), `$n`/`$N` in every
line would instead render the enemy's **starred race name** in the observer's enemy color (e.g.
`"*an Uruk-Hai*"`) rather than the personal name — the same `PERS()` logic races.md documents for
combat.

---

## Open questions

- **`$T` and `$b`/`$B` have no observed call sites** in the current source (a literal-string grep for
  `$T`, `$b[^a-zA-Z]`, `$B[^a-zA-Z]` across all `.cpp` files returns nothing, whereas `$F` does show
  real uses for door keywords). `$b`/`$B` expand via `GET_CURRPART(ch)` (`utils.h:385`,
  `bodyparts[race].parts[ch->specials.current_bodypart]`), and specifically the **`current_bodypart`
  field is never assigned anywhere** in the source (only ever read, via this one macro) — so that
  field, and therefore `$b`/`$B`, look dead. **Correction/narrowing:** the underlying `bodyparts[]`
  table itself is *not* dead data — its `.parts[]` (body-part name strings), `.armor_location[]`, and
  `.percent[]` members are actively read by the live melee path, e.g. `fight.cpp:1474`
  (`part.parts[hit_location]`) for hit-location flavor text, just indexed by a locally-computed
  `hit_location` rather than by `ch->specials.current_bodypart`. So only the `$b`/`$B` act()-codes and
  their specific backing field are vestigial (a removed feature that let arbitrary code "select" a
  body part on a character for later `$b`/`$B` substitution?) — not the body-part data itself. Treat
  `$b`/`$B` as dead unless a counter-example turns up.
- **Exact `hide` flag semantics for socials** remain formally unresolved (flagged already in
  `docs/data-formats/socials-and-messages.md`) but this pass narrows it down: `hide` is passed
  straight through as `act()`'s `hide_invisible` (`act_soci.cpp:299,306,312-313`), so `hide=1` means
  "an observer who can't see the actor doesn't see this room-message at all." Live confirmation:
  `lib/misc/socials` has both `hide=0` examples (`flutter`, `babble`, `adore`, `hail`) and `hide=1`
  examples (`smile`, `dance`, `caress`, `eye`, `melt`, …) — plausibly grouped by whether the social
  reads as something a hidden/sneaking character could plausibly still pull off unnoticed (loud
  physical socials like `dance` vs. quiet ones), but that grouping rationale is inferred, not
  confirmed from source or comments.
- **Whether `pray`'s `POSITION_FIGHTING` minimum (`interpre.cpp:1994`) is intentional** — it means a
  resting/sitting character *cannot* pray (needs to be at least in a fight-or-standing posture),
  which reads oddly for a "raise a prayer" flavor command; worth confirming against player-facing
  behavior rather than assuming it's a typo.
- **CAN_SEE's full visibility ruleset** (infravision/moonvision/invis levels/hide value comparisons)
  is out of scope here — it's referenced throughout `act()` and `PERS()` but belongs in a dedicated
  perception/visibility doc if one doesn't already exist; races.md covers only the innate-vision
  angle (§1.6).
