# Mob AI and Special Procedures

**Source files:** activity loop `src/mobact.cpp` (`mobile_activity`, `one_mobile_activity`, mob
memory); heartbeat driver `src/comm.cpp` (`game_loop`); flag catalog `src/structs.h:902-932`
(`MOB_*`); dispatch plumbing `src/interpre.h`/`src/interpre.cpp` (`SPECIAL` macro, `special()`,
`activate_char_special`); assignment tables `src/spec_ass.cpp` (`assign_mobiles`, `assign_objects`,
`assign_rooms`, `virt_program_number`, `virt_assignmob`); the specproc catalog itself
`src/spec_pro.cpp` (plus `postmaster` in `src/mail.cpp`, `gen_board` in `src/boards.cpp`,
`shop_keeper` in `src/shop.cpp`, `receptionist` in `src/objsave.cpp`); mudlle script entry point
`SPECIAL(intelligent)` in `src/mudlle.cpp`; combat AI hooks live in `src/fight.cpp`
(`perform_violence`, `natural_attack_dam`, `hit`); OB/parry/dodge formulas in `src/utility.cpp`
(`get_real_OB`, `get_real_parry`, `get_real_dodge` — **live** per `AGENTS.md`, not the dead
`char_utils_combat.cpp` versions); mob stat loading in `src/db.cpp` (`load_mobiles`, `read_mobile`).
**Status:** ✅ activity loop, flag catalog, combat AI, and specproc catalog verified against
source and live `lib/world/mob` data; mudlle/script triggers only cross-referenced (see
[mudlle-and-scripts.md](../data-formats/mudlle-and-scripts.md)); world-file grammar cross-referenced
to [world-files.md](../data-formats/world-files.md).

## Purpose
This covers how NPCs ("mobs") act on their own: the periodic tick that drives wandering,
scavenging, aggression, memory, and helper/bodyguard behavior; the flag vector that a rewrite must
reproduce bit-for-bit; how mobs fight (target selection, spellcasting, kick/bash, specialized boss
scripts); and **special procedures** — hard-coded or data-selected C++ callbacks attached to a
mob/object/room vnum that intercept commands or drive tick-based behavior beyond what the generic
flag-driven AI provides. It does **not** cover the mudlle bytecode scripting engine itself (a
separate system — see [mudlle-and-scripts.md](../data-formats/mudlle-and-scripts.md)) beyond
noting where mobs hand off to it, and it does not re-derive the swing-by-swing damage formula
(already exhaustively covered in [combat-loop.md](combat-loop.md)) beyond the NPC-specific
branches that formula takes.

## Data structures

### The heartbeat (`src/comm.cpp`, `src/structs.h`)
The main loop selects with a **250 ms** timeout (`OPT_USEC = 250000`, `comm.cpp:45`, "time delay
corresponding to 4 passes/sec") and increments a global `pulse` counter once per pass
(`comm.cpp:812`, reset to 0 at `pulse >= 2400`, i.e. every 10 minutes). Relevant tick divisors
(`structs.h:57-60`, all in units of one 250 ms pulse):
```
PULSE_ZONE          = 12   ( 3.0 s)  zone resets
PULSE_MOBILE        = 24   ( 6.0 s)  mobile_activity()
PULSE_VIOLENCE      = 12   ( 3.0 s)  nominal "combat round" length (parry regen, etc.)
PULSE_FAST_UPDATE   = 12   ( 3.0 s)  affect/regen tick
```
`perform_violence()` (the swing driver) is actually called **every pulse** (every 0.25 s,
`comm.cpp:822`), not once per `PULSE_VIOLENCE` — see [combat-loop.md §Attack
speed](combat-loop.md) for why: real attack cadence is gated by the `ENERGY`/`ENE_regen`
accumulator, not by this call frequency, and the `mini_tics` argument passed in is unused
(`fight.cpp:2716`).

### Mob action flags — `specials2.act` (`char_special2_data::act`, `structs.h:1187`)
A `long` bitvector, read from the mob file's single `<mob_action_flags>` integer
([world-files.md](../data-formats/world-files.md) — same field also carries the player `PLR_*`
flags for PCs, since NPC-ness (`MOB_ISNPC`) is just bit 3 of the same word). Tested with
`MOB_FLAGGED(ch, flag)` = `IS_NPC(ch) && IS_SET(MOB_FLAGS(ch), flag)` (`utils.h:217`,
`MOB_FLAGS(ch)` = `ch->specials2.act`, `utils.h:213`).

### Mob preference bitvector — `specials2.pref` (`structs.h:1188`)
A second `long`, `<pref>` in the mob file, reused for two purposes depending on character type:
for PCs it's a "preference flags" word (unrelated here); **for NPCs it is a per-race aggression
bitvector** — bit `N` set means "attack player race `N` on sight" — consumed by
`IS_AGGR_TO(ch, vict)` (`utils.h:637-638`):
```
IS_AGGR_TO(ch, vict) =
    IS_NPC(ch) &&
    ( (ch->specials2.pref & (1 << GET_RACE(vict)))                 // per-race aggro bit
      || (other_side(ch, vict) && !IS_NPC(vict) && GET_RACE(ch) != 0) )  // race-war fallback
```
The second disjunct is close to inert for ordinary mobs: `other_side()` (`handler.cpp:127-149`)
returns **0 (same side) immediately** whenever `character` is an NPC that is **not** `AFF_CHARM`ed
(`handler.cpp:129-132`) — so for a normal, uncharmed mob, only the `pref` bitvector actually drives
race-based aggression; the race-war fallback only matters for **charmed** NPCs (pets/guardians)
acting as if they were their controller's race.

### Memory records (`struct memory_rec`, referenced `mobact.cpp:71,378-417,423-544`)
A free-listed singly-linked list (`memory_rec_pool` / `memory_rec_active`,
`get_from_memory_rec_pool`/`put_to_memory_rec_pool`, `mobact.cpp:427-462`) hung off
`ch->specials.memory`. Each node records an enemy's persistent ID (`GET_IDNUM`) plus a live
pointer/`abs_number` pair (re-resolved via `update_memory_list`, `mobact.cpp:530-544`, whenever a
character with that idnum re-enters the game). `remember()`/`forget()`/`clear_memory()`
(`mobact.cpp:465-528`) add, remove, and wipe entries; entries are **not persisted** — memory is
lost on mob death/reboot (mob instances are recreated from `mob_proto` fresh each spawn).

### Special-procedure dispatch types (`src/interpre.h:34-71`)
```
special_func = int(*)(char_data *host, char_data *ch, int cmd, char *arg, int callflag, waiting_type *wtl)
SPECIAL_NONE     0   SPECIAL_COMMAND  1   SPECIAL_SELF    2   SPECIAL_ENTER  4
SPECIAL_DELAY    8   SPECIAL_TARGET  16   SPECIAL_DAMAGE 32   SPECIAL_DEATH 64
```
`host` is the mob/object/room the function is attached to; `ch` is the character that triggered
the call (for `SPECIAL_SELF`, `ch == host`). A specproc returning **nonzero** means "I fully
handled this — do not run the normal command/behavior", mirroring stock CircleMUD.
`ASSIGNMOB`/`ASSIGNOBJ`/`ASSIGNROOM`/`ASSIGNREALMOB`/`ASSIGNREALOBJ` (`interpre.h:43-65`) are thin
macros that resolve a vnum to an index-table slot and store a raw function pointer
(`mob_index[i].func` / `obj_index[i].func` / `world[i].funct`).

## Format / Algorithm

### 1. The tick — `mobile_activity()` (`mobact.cpp:47-63`)
Called once per `PULSE_MOBILE` (every 6 s). Walks the **entire live character list**
(`character_list`), and for each entry rolls `!number(0,3)` — a flat **25 % chance per tick** that
this particular mob actually acts this pass (so a given mob's *expected* time between activations
is ~24 s, geometrically distributed, not a fixed cadence). If the roll hits:
- An NPC (`IS_NPC(ch)`) runs `one_mobile_activity(ch)`.
- A **player** with a `store_prog_number` set (`ch->specials.store_prog_number`) instead resolves
  and calls that function with `SPECIAL_SELF` — i.e. the same tick infrastructure can drive scripted
  *player* behavior (used for automated/system pseudo-characters), though this is rare in practice.

### 2. Per-mob logic — `one_mobile_activity()` (`mobact.cpp:65-420`)
Guards first: bails if not an NPC, if `in_room` is out of range (logs `mudlog`), and computes
`is_passive` — a pet standing in the same room as its master is fully passive (skips **everything**
below) *unless* it's a guardian (`utils::is_guardian`, `char_utils.cpp:1352`) — guardians still act
even next to their master (`mobact.cpp:92-96`).

**Interrupt decay** (`mobact.cpp:99-107`): while fighting, `interrupt_count`/`interrupt_time`
(spell-interruption resistance state — see [magic-system.md](magic-system.md) §6 Battle Mage) tick
down once per mob-activity pass, not once per pulse.

The rest only runs if `ch->delay.wait_value <= 1` and `!is_passive` (`mobact.cpp:109`):

1. **Position enforcement** (`mobact.cpp:111-113`): if not charmed, `enforce_position(ch,
   ch->specials.default_pos)` (`mobact.cpp:552-585`) nudges the mob toward its file-configured
   default position (stand a sitting sentry back up, etc.) via `do_stand`/`do_sit`/`do_rest`/
   `do_sleep`, but never while `POSITION_FIGHTING` or below `POSITION_STUNNED`.
2. **Special-procedure dispatch, `SPECIAL_SELF`** (`mobact.cpp:116-140`): if `MOB_SPEC` is set and
   specprocs aren't globally suppressed (`-s` boot flag, `no_specials`, `comm.cpp:84,205`), calls
   `mob_index[ch->nr].func` if already bound (by `assign_mobiles`/`assign_the_shopkeepers`/a prior
   shapeshift — see §4), **or, if unbound, resolves `store_prog_number` through
   `virt_program_number()` right there on the tick** (`mobact.cpp:120-124`) — this is the *same*
   native-C++ specproc switch (`spec_ass.cpp:309-383`, cases `1`-`32`) that `virt_assignmob()` uses,
   just resolved dynamically per-activation instead of once at bind time; **it is not the mudlle
   path**, even though both are keyed off the same `store_prog_number` field. If **not**
   `MOB_SPEC`-flagged but the mob has a mudlle "program" attached (`union1.prog_number`), it instead
   calls `intelligent()` (`mudlle.cpp:655`) — the mudlle bytecode interpreter's tick entry point (see
   [mudlle-and-scripts.md](../data-formats/mudlle-and-scripts.md)) — this *is* the actual
   mudlle-script branch. **If the specproc/script returns nonzero, `one_mobile_activity` returns
   immediately** — none of the generic behavior below runs that tick. See "Ways to attach a
   specproc/script" below for how `MOB_SPEC` + `store_prog_number` disambiguate native-C++ vs.
   mudlle-script dispatch.
3. **Helper** (`mobact.cpp:153-192`, gated on `MOB_HELPER` and awake and not already fighting):
   scans the room for an ally (another character) already fighting someone. It will **not** help
   an ally it is itself `IS_AGGR_TO` against, and never assists `MOB_GUARDIAN`s or orc-friend pets.
   It assists if either (a) it is `IS_AGGR_TO` the ally's enemy, or (b) it shares the ally's
   alignment sign (`GET_ALIGNMENT(ch) * GET_ALIGNMENT(ally) > 0`) and the ally isn't itself
   aggressive to that enemy. High-INT (`GET_INT_BASE(ch) >= 7`) helpers announce "I must protect my
   friend!" before assisting via `do_assist`.
4. **Bodyguard — follower side** (`mobact.cpp:195-219`, `MOB_BODYGUARD` + master in room): stands
   up if needed, then rescues its master if the master's opponent is specifically fighting the
   master back, or joins the fight against the master's attacker if not already fighting.
5. **Bodyguard — master side** (`mobact.cpp:222-237`): a `MOB_BODYGUARD` master rescues **any**
   follower who is being reciprocally attacked.
6. **Assistant** (`mobact.cpp:241-255`, `MOB_ASSISTANT` + master in room + master fighting + self
   not fighting): unconditionally assists its master (stands first if needed).
7. **Guardian special case** (`mobact.cpp:258-262`): a guardian (`is_guardian`) that is fighting but
   whose master has left the room `do_flee`s to find them.
8. **Switch targets mid-fight** (`mobact.cpp:264-272`, `MOB_SWITCHING` or `MOB_SHADOW`, fighting,
   standing/sitting-or-better): 1-in-4 chance per tick to retarget onto someone in the room who is
   attacking it but whom it isn't currently fighting back (handles being "railroaded" onto one
   attacker while others pile on for free).
9. Everything after this point requires **awake and not fighting** (`mobact.cpp:274`):
   - **Scavenger** (`mobact.cpp:275-304`, `MOB_SCAVENGER`, 1-in-6 chance if the room has contents):
     picks up the highest-`cost` gettable item in the room, or loots wearable items out of any
     container present (then auto-wears them via `do_wear "all"`).
   - **Wander** (`mobact.cpp:306-316`): if not `MOB_SENTINEL`, standing, has no master, and a
     random direction (0-45, so only 1-in-~7.5 of rolls land in a valid 0-5 direction range —
     `NUM_OF_DIRS` is checked after the roll) is a real, open, non-`NO_MOB`/non-`DEATH` exit: moves
     that way. `last_direction` (the direction of the *previous* successful move) suppresses
     *repeating* that move, not reversing it: if this tick's roll equals `last_direction`, the mob
     skips moving this tick and resets `last_direction` to `-1` (so a repeat roll is allowed again
     next tick) — net effect is "don't take the same exit two activations running," not "don't
     immediately backtrack" (the reverse/opposite direction is never checked). Honors
     `MOB_STAY_ZONE` (destination zone must match) and `MOB_STAY_TYPE` (destination sector type must
     match).
   - **Race-preference aggression** (`mobact.cpp:319-333`): if `specials2.pref` is nonzero, scans
     the room for the *first* visible character (mount riders excluded from being attackers here)
     it `IS_AGGR_TO`, attacks with `do_hit`, and **returns from the whole function** — this branch
     takes priority over and preempts flags-based aggression/memory below.
   - **Standard `MOB_AGGRESSIVE`** (`mobact.cpp:336-365`, only if not `MOB_MOUNT`): scans the room
     for the first visible PC not `PRF_NOHASSLE`. If `MOB_WIMPY` is also set, only sleeping/resting
     PCs (`!AWAKE`) qualify — the flag's second meaning ("aggressive mobs attack only sleeping
     victims", `structs.h:911-912`). One of the alignment-restriction bits
     (`MOB_AGGRESSIVE_EVIL`/`_GOOD`/`_NEUTRAL`) further filters by the victim's alignment; **if none
     of the three are set, alignment is ignored and the mob attacks any qualifying PC.**
     `MOB_SWITCHING` mobs randomize which of several qualifying victims they pick (reservoir-style
     coin flip) instead of always taking the first.
   - **Memory / Hunter** (`mobact.cpp:367-417`, needs `MOB_MEMORY` or `MOB_HUNTER` or
     `AFF_HUNT`, and a non-empty memory list): a pet that's inexplicably in `POSITION_FIGHTING` with
     no target flees. Then: if a remembered enemy is present in the room, attack (INT-gated flavor
     text) — unless the remembered enemy is the mob's own master (which instead calls `forget`).
     Otherwise, if `MOB_HUNTER`/`AFF_HUNT`, path toward the nearest remembered enemy's room via
     `find_first_step` (BFS) and move one step — an `AFF_CONFUSE`d hunter has a chance
     (`get_confuse_modifier`) each tick of failing to path correctly (see
     [Affect duration / confuse notes] — the confuse penalty here is intentional, not a bug).
     **How a mob gets a memory entry in the first place:** `MOB_MEMORY` mobs call `remember()`
     when struck first by a PC (wired in the combat code, not shown here) — memory itself is purely
     the *data structure*; the *trigger* to populate it is "this NPC was attacked while not already
     fighting."

### 3. Combat-tick specproc AI vs. auto-attack
The energy-driven auto-attack swing (every fighter, every pulse, once `ENERGY > ENE_TO_HIT`) is
generic and covered in [combat-loop.md](combat-loop.md) — it applies identically to PCs and mobs
and does **not** by itself make a mob kick, cast, or ambush. Those richer combat behaviors are
layered on **on top of** the auto-attack via the `SPECIAL_SELF` tick dispatch above (so a spell-
casting mob still auto-attacks every energy tick *and* additionally tries to cast on its ~6 s,
25 %-chance mobile-activity ticks):
- **`mob_warrior`** (`spec_pro.cpp:1870-1915`): while fighting and standing, rolls
  `number(0, warrior_prof_level) % 9` into a 9-slot table (`warrior_abilities[]`,
  `spec_pro.cpp:1866-1868`) that resolves to `CMD_KICK` (2 of 9 slots), `CMD_BASH` (2 of 9), or a
  no-op (5 of 9) — i.e., roughly a 4-in-9 chance per activation to throw a kick or bash instead of
  just relying on auto-attack, weighted toward "nothing extra" most ticks. Executed via
  `command_interpreter`, so it goes through the same skill-success/energy machinery as a player
  typing the command.
- **`mob_cleric`** / **`mob_magic_user`** (`spec_pro.cpp:1520-1581`): once per activation (if not
  already mid-cast), pick a target via `choose_caster_target` and a spell via
  `choose_mystic_spell`/`choose_mage_spell`, then issue `do_cast` directly. Both are superseded in
  practice by the richer variants below but remain assignable.
- **`choose_caster_target`** (`spec_pro.cpp:1489-1517`): if not fighting, targets **self** (heal/
  buff pass). If fighting, prefers whoever is actually engaged with it or its group (reservoir
  sampling across all room occupants satisfying `is_engaged_with_victim`) over blindly hitting
  `specials.fighting`.
- **`choose_mystic_spell`** (`spec_pro.cpp:1369-1403`): on self — cure poison first, then
  Regeneration/Curing once under ⅓ HP. On an enemy — an index into the shared `spell_list[][4]`
  table (`spec_pro.cpp:1354-1366`, 10 level brackets × 4 "spell school" columns) chosen by
  `min(level/5 - 1, 9)`, offense column (index 3).
- **`choose_mage_spell`** (`spec_pro.cpp:1419-1457`): heals self under ⅓ HP; a `MOB_WIMPY` mage
  under ¼ HP casts **Blink** to flee instead of fighting on; otherwise a flat 50 % chance to not
  cast at all that tick, else an index into the same `spell_list[][4]` table but keyed by
  `get_mob_spell_type()` — **race** selects the "school" column: Magus → cold(2), Uruk/Orc →
  fire(1), everyone else → arcane/default(0) (`spec_pro.cpp:1410-1417`).
- **`mob_magic_user_spec`** (`spec_pro.cpp:1733-1864`) is the richer, currently-preferred mage AI
  (assignable as prog `31`). Behavior branches, in priority order:
  1. Non-combat: a mob with the **`"conj"`** keyword in its name casts Regeneration then Curing on
     itself if not already affected (a dedicated "conjurer" archetype that self-buffs before a
     fight).
  2. Non-combat: any mage below 90 % HP without Shield (or with Shield and ≥50 % mana) casts
     `SPELL_CURE_SELF`.
  3. **Panic cast**: if fully spell-interrupted (`interrupt_count == 3`) and has `"shield"` in its
     name, 50 % chance to stop everyone fighting it and itself, then cast `SPELL_SHIELD` — a
     "break contact and ward up" escape valve.
  4. **Desperation Terror**: `"terror"`-keyword mob below 0 % HP-bracket (`quarter_pct`, i.e. very
     low), fighting, 20 % chance per tick to cast `SPELL_TERROR`.
  5. **Normal combat casting**: picks a target via `choose_caster_target`; a `"conj"` mob has a
     25 % chance to open with Confuse or (if not also `"lumage"`) Poison; otherwise, if not maxed
     out on interruptions, `get_combat_spells()` (`spec_pro.cpp:1694-1728`) builds a candidate list
     keyed by **HP-bracket alias** (see below) and `pick_a_spell()` rolls uniformly among them.
  - **HP-bracket keyword system**: a mob's name can carry one of six "mage type" alias substrings
    (`mage_aliases[]`, `spec_pro.cpp:1592`: `fmage`/`lmage`/`cmage`/`dmage`/`lumage`/`defaultm`),
    each mapping to its own `new_spell_list[type][bracket][…]` (`spec_pro.cpp:1604-1634`) across
    **4 HP brackets** (`percents[] = {76%, 51%, 26%, 0%}`, `spec_pro.cpp:1594-1600` — i.e. the
    spell pool gets nastier as the mage's own HP drops). Certain high-tier spells are level- and
    keyword-gated on top of the bracket table (`add_leveled_spell_to_list`,
    `spec_pro.cpp:1709-1725`): Fireball (fire, ≥40), Cone of Cold (cold, ≥30), Searing Darkness
    (dark, ≥40), Earthquake (default, ≥30), Lightning Strike (lightning, ≥35, only during an
    outdoor lightning-sky weather event), Spear of Darkness (lhuth, ≥40, only without sun penalty).
  - **This is a data-driven AI selector**: `has_alias()` (`utility.cpp:2205`) is a **plain substring
    search on the mob's `name`/keyword field** — there is no separate "mage archetype" flag. A
    rewrite must replicate keyword-substring matching against the mob's parsed name string, not a
    bitflag or enum.
- **`mob_ranger`** (`spec_pro.cpp:1917-1996`, legacy) / **`mob_ranger_new`**
  (`spec_pro.cpp:2068-2261`, current, prog `32`): both run on `SPECIAL_SELF` **and**
  `SPECIAL_ENTER` (i.e. also fire the instant a target walks into the room, not just on the mob's
  own tick). `mob_ranger_new` adds: a wimpy-HP gate (below 20 % max HP, `spec_pro.cpp:2094-2099`,
  suppresses aggression entirely rather than just fleeing); keyword-gated behavior — `"stab"` in
  the name prefers opening with `do_ambush` over a plain `do_hit`, `"hunt"` enables the
  room-track-following pursuit branch; auto re-hide (`do_hide`) whenever standing and not already
  hidden, both before and after moving; and `MOB_SWITCHING` retargeting mirrored from the generic
  path. Tracking (`spec_pro.cpp:2201-2237` / legacy `1964-1996`) walks the room's
  `room_track[NUM_OF_TRACKS]` array (`structs.h:585,588,636` — 10 slots of "who walked which
  direction how long ago") looking for a track belonging to a race it's aggressive to
  (`racial_aggr & (1 << -char_number)`, tracks store the walker as a **negative** race-derived
  index) or, if plain `MOB_AGGRESSIVE`, any player-race track, and follows the freshest one.
- **Unique boss scripts** (`ar_tarthalon`, `ghoul`, `dragon`, `vampire_huntress`/`thuringwethil`/
  `vampire_doorkeep`/`vampire_killer`, `wolf_summoner`, `healing_plant` — see catalog below) are
  all also dispatched through this same `SPECIAL_SELF` tick and layer boss-specific mechanics
  (breath weapons, minion summons, phase transitions, room-to-room hunting patterns) on top of the
  generic loop.

### 4. Ways to attach a specproc/script to a mob
The mob file's `<store_prog_number>` field ([world-files.md](../data-formats/world-files.md)) is
**overloaded**, disambiguated by whether `MOB_SPEC` is set at load time (`db.cpp:1351-1354`,
`load_mobiles`). There are also two **explicit, vnum-keyed tables** that bind `mob_index[nr].func`
directly and take priority regardless of `MOB_SPEC`/`store_prog_number` — so in total a mob's
specproc can come from any of four sources:
- **`MOB_SPEC` set** → the raw integer is left as-is; it is meant to index
  `virt_program_number()`'s switch (`spec_ass.cpp:309-383`, cases `1`-`32`, one per native C++
  specproc — see catalog below). This switch is consulted **two different ways**: (a) once, at
  bind time, via `virt_assignmob()` (`spec_ass.cpp:474-487`), which is only invoked from the
  mob-**shapeshifting** path (`shapemob.cpp:1859`) — i.e. when a mob is dynamically re-templated at
  runtime (polymorph/lycanthropy-style effects), not at normal boot load; and (b) dynamically,
  **every activation tick**, as the `mobact.cpp:120-124` fallback described in §2 step 2 — so a
  `MOB_SPEC` mob whose `store_prog_number` is a valid case (`1`-`32`) and whose `mob_index[nr].func`
  was never separately bound still gets its specproc resolved and called, just re-looked-up each
  tick instead of cached. A mob with `MOB_SPEC` set only ends up with **no** function attached at
  all when *both* (a)/(b) fail to resolve it (i.e. `store_prog_number` is `0` or outside `1`-`32`)
  **and** it isn't independently bound by the explicit vnum table or `assign_the_shopkeepers()`
  below — see Open questions for confirmed live examples (145+ mobs) and a corrected worked case
  (the doc previously cited a `store_prog_number = 1104` example that turned out to resolve fine via
  `assign_the_shopkeepers()`).
- **A third, narrower explicit table**: `assign_the_shopkeepers()` (`shop.cpp:655-671`, called at
  `db.cpp:419`, immediately before `assign_mobiles()`/`assign_objects()`/`assign_rooms()`) walks
  every loaded shop record and does `mob_index[shop_index[i].keeper].func = shop_keeper;` — i.e. a
  shop's keeper mob gets the `shop_keeper` specproc bound directly from **shop-file data**
  (`lib/world/shp/*.shp`, the keeper vnum field), completely independent of `MOB_SPEC`,
  `store_prog_number`, and the `assign_mobiles()` vnum table. This is why many mobs that look
  "unassigned" in `spec_ass.cpp` (tailors, barkeeps, armorers, etc.) still function as shops — see
  Open questions.
- **`MOB_SPEC` *not* set, but `store_prog_number != 0`** → the loader immediately rewrites it via
  `real_program(tmp3)` (`db.cpp:1353-1354`) — i.e. it's a **mudlle script** vnum, not a specproc
  index. At mob-instantiation time (`read_mobile`, `db.cpp:1204-1219`), this drives a `union1`/
  `union2` call-list setup consumed by `intelligent()` (`mudlle.cpp:655`) instead of a native
  function. This is the mudlle/script path — see
  [mudlle-and-scripts.md](../data-formats/mudlle-and-scripts.md) for the bytecode format and
  trigger codes (`ON_ENTER`, `ON_DIE`, `ON_RECEIVE`, `ON_HEAR_SAY`, etc.); this doc only notes the
  hand-off point.
- **The primary, explicit mechanism** is `assign_mobiles()` / `assign_objects()` / `assign_rooms()`
  (`spec_ass.cpp:83-273`), a **hard-coded vnum → function table** invoked once at boot
  (`db.cpp:421-425`, right after mob/obj/room load) via the `ASSIGNMOB(vnum, fname)` /
  `ASSIGNOBJ` / `ASSIGNROOM` macros. This writes `mob_index[real_mobile(vnum)].func` directly,
  **independent of `MOB_SPEC`/`store_prog_number`** — most named specprocs in the catalog below
  (guildmasters, receptionists, unique bosses, gatekeepers, ferries) are wired up this way, by
  explicit vnum, not by the mob's own file data. A mob only needs `MOB_SPEC` set so that the
  dispatch code (`interpre.cpp:1563`, `mobact.cpp:116`) will actually *look up and call*
  `mob_index[nr].func` — the flag gates whether the (already-bound) function fires, the table
  decides *which* function.

### 5. Command-triggered dispatch order — `special()` (`interpre.cpp:1605-1723`)
Separate from the tick above: every player command goes through `command_interpreter`, which (if
target-parsing succeeded) calls `special(ch, cmd, arg, SPECIAL_COMMAND, …)` **then**
`special(ch, cmd, arg, SPECIAL_TARGET, …)` before running the command's own handler
(`interpre.cpp:1392-1400`) — so **any** specproc in the chain can return nonzero and fully
pre-empt the built-in command. For `SPECIAL_COMMAND` the cascade order is:
1. The room's own special (`world[in_room].funct`), if any.
2. (Local room only) each of the actor's **equipped** items, left to right by wear slot.
3. (Local room only) each item in the actor's **inventory**.
4. Every **mobile present** in the room (`activate_char_special` on each, in room-list order) —
   this is how `guild`/`receptionist`/`shop_keeper`/`postmaster`/`gatekeeper`/`kit_room`-style
   command interception happens; a mob doesn't need to be the command's grammatical target to react
   to it.
5. Every **object present** in the room.
For `SPECIAL_TARGET`/`SPECIAL_DAMAGE`, the cascade instead walks the parsed command's `wtl.targ1`/
`targ2` slots directly (room/obj/char) rather than the whole room contents — this is how, e.g.,
`resetter` intercepts `tell angel pracreset` (a `TARGET_TEXT` match on `targ2`,
`spec_pro.cpp:2648-2659`) without being a literal room occupant of wherever the player is standing.
`SPECIAL_DEATH` calls only the dying character's own special. `activate_char_special`
(`interpre.cpp:1556-1585`) is the single per-character entry point both this cascade and the
mobile-activity tick above route through; it resolves `MOB_SPEC` (native) vs. `store_prog_number`
(mudlle) vs. neither, exactly as described in §4.

### 6. Mob stat derivation at load (`db.cpp`)
See [world-files.md](../data-formats/world-files.md) for the full `.mob` field grammar. AI-relevant
specifics not covered there:
- **HP is randomized per spawn, not fixed.** The file's `<hp_current>`/`<hp_max>` pair is loaded
  into `tmpabilities.hit`/`abilities.hit` on the **prototype** (`db.cpp:1325-1326`), but at each
  **instantiation** (`read_mobile`, `db.cpp:1144-1147`) the actual max HP is re-rolled uniformly
  between those two file values — `abilities.hit = number(tmpabilities.hit, abilities.hit)` — and
  then `tmpabilities`/`constabilities` are both reset to that rolled max, so **every mob spawns at
  full health**; the file's "current HP" field is really a **lower bound for the HP roll**, not a
  literal starting HP.
- **OB/parry/dodge are stored raw, not derived from level/stats**, unlike a PC's stat pipeline
  (`stats-and-character-power.md`). `SET_OB`/`SET_PARRY`/`SET_DODGE` (`db.cpp:1320-1322`) write the
  file's `<OB> <parry> <dodge>` straight into `points.OB`/`points.parry`/`points.dodge`
  (`utils.h:411-429`). The **live** combat formulas (`utility.cpp` — see AGENTS.md dead-code
  warning) then apply a small NPC-specific top-up on top of the raw file value every time OB/parry/
  dodge is queried:
  ```
  get_real_OB(NPC)    = file_OB    + GET_BAL_STR(ch) + 15 - skill_penalty + level/2   (utility.cpp:649-654)
  get_real_parry(NPC) = file_parry + level/2 + 15                                     (utility.cpp:765-769)
  get_real_dodge(NPC) = file_dodge + DEX - 5 + level/2                                (utility.cpp:864-868)
  ```
  (each also subtracts `get_confuse_modifier(ch)*2/3` while `AFF_CONFUSE`d). There is **no**
  warrior-level/tactics/weapon-skill math for NPCs — that entire branch of each function is
  PC-only.
- **Damage** (`points.damage`, file `<damage>`) feeds the shared `hit()` formula as a flat
  `GET_DAMAGE(ch) * 10` additive term (`fight.cpp:2509`) — see
  [combat-loop.md](combat-loop.md) for the full pipeline. An **unarmed** mob does **not** get the
  PC's `natural_attack_dam()` bonus (`fight.cpp:2387-2393` — that call is explicitly gated
  `!IS_NPC(ch)`), so an unarmed mob's damage comes entirely from this flat term plus the shared
  OB/roll/STR multiplier; a **weapon-wielding** mob's weapon-die roll is halved
  (`fight.cpp:2500-2501`, "mobs have weapon damage halved") before the same additive term applies.
- **Attack speed** (`points.ENE_regen`, file `<energy_regen>`) is used completely unmodified as the
  mob's `ENERGY` accumulation rate (`get_energy_regen`, `char_utils.cpp:1359-1368` — the only
  per-character adjustment is the shared wild-fighting rage multiplier, which applies identically
  to PCs and NPCs). `ENERGY` starts at a flat `1200` for every mob (`specials.ENERGY = 1200`,
  `db.cpp:1331`) regardless of file data.
- **Perception/willpower** are computed, not read from file: `get_naked_perception(mob)` and
  `get_naked_willpower(mob)` are invoked for every mob at `read_mobile` time
  (`db.cpp:1189,1192`), the same functions used for PCs.
- **Stat floors**: any of STR/INT/WIL/DEX/CON/LEA at or below 0 is forced to `17` at spawn
  (`db.cpp:1150-1178`, logged as "Mobile had its stats fixed") — a safety net for content that
  ships a mob with an unset stat.

## RotS-specific notes
- The entire flag catalog, memory system, helper/bodyguard/assistant chain, race-preference
  aggression (`specials2.pref`), and the specialized mage/ranger/warrior AI (`mob_magic_user_spec`,
  `mob_ranger_new`, weapon-master-aware combat) are **RotS additions** on top of stock
  CircleMUD/DikuMUD, whose original `mobile_activity` only handled scavenger + wander + basic
  memory-less aggression.
- **Keyword-substring AI selection** (`has_alias`, §3 above) is a distinctly RotS pattern: instead
  of a mob-type enum, several specprocs branch on whether a literal substring (`"conj"`, `"stab"`,
  `"hunt"`, `"shield"`, `"terror"`, `"fmage"`/`"lmage"`/`"cmage"`/`"dmage"`/`"lumage"`/`"defaultm"`)
  appears anywhere in the mob's name/keyword string. A world builder "configures" a mage's element
  or a ranger's opening move purely by choosing what words go in the mob's `name~` line — there is
  no separate data field for it.
- **`no_specials` (`-s` boot flag, `comm.cpp:84,205`)** globally disables **both** dispatch paths
  (tick-driven `SPECIAL_SELF` in `mobact.cpp:116` and command-driven `special()` in
  `interpre.cpp:1392,1563`) — useful for isolating whether a bug is in a specproc vs. the base
  engine.
- **`MOB_NOBASH`** (`structs.h:908`) isn't consulted anywhere in this doc's dispatch code; it's
  checked directly by the bash/kick skill implementations (`act_offe.cpp:533,885`,
  `olog_hai.cpp:40`) to exempt certain mobs (e.g. very large creatures) from knockdown, independent
  of the AI loop.
- **`cryogenicist`** is forward-declared (`objsave.cpp:64`) but has **no implementation** anywhere
  in the source tree — a dead declaration, not a usable specproc. Don't budget rewrite effort for
  it beyond noting the name is currently vestigial.

## Full `MOB_*` flag catalog (`structs.h:902-932`)
All bits live in `specials2.act` (aliased `PLR_*` for players on the same word). Live-data
frequency is a scan of all 3,723 mob records across `lib/world/mob/*.mob` (see Worked example).

| Bit | Value | Name | Meaning | Live use |
|----:|------:|------|---------|---------:|
| — | 0 | `MOB_VOID` | Sentinel "no flags" value; not a real bit. | — |
| 0 | 1 | `MOB_SPEC` | A specproc should be looked up/called for this mob (native or, if unset, mudlle — see §4). | 21.0% |
| 1 | 2 | `MOB_SENTINEL` | Never wanders on its own (§2 Wander). | 51.8% |
| 2 | 4 | `MOB_SCAVENGER` | Picks up valuable/wearable loose items each tick (§2 Scavenger). | 10.6% |
| 3 | 8 | `MOB_ISNPC` | Marks the character as an NPC; force-set by the loader — mob files needn't include it. | 100% (forced) |
| 4 | 16 | `MOB_NOBASH` | Exempt from bash/kick knockdown (checked in `act_offe.cpp`, not the AI loop). | 8.4% |
| 5 | 32 | `MOB_AGGRESSIVE` | Attacks qualifying PCs on sight each tick (§2 Standard aggression). | 23.9% |
| 6 | 64 | `MOB_STAY_ZONE` | Wander destinations must be in the same zone. | 8.8% |
| 7 | 128 | `MOB_WIMPY` | Two effects: (a) if also `MOB_AGGRESSIVE`, only attacks sleeping/resting PCs; (b) auto-`do_flee`s once HP < 20% max (`fight.cpp:1896-1903`). | 4.1% |
| 8 | 256 | `MOB_STAY_TYPE` | Wander destinations must share the current room's sector type. | 12.8% |
| 9 | 512 | `MOB_MOUNT` | Can be ridden; also exempted from standard aggression and race-preference attacks (`mobact.cpp:322,336`). | 1.0% |
| 10 | 1024 | `MOB_CAN_SWIM` | Doesn't need a boat to cross water; a gatekeeper with this flag also won't open gates on request. | 9.3% |
| 11 | 2048 | `MOB_MEMORY` | Remembers (and re-attacks) whoever struck it first while not fighting (§2 Memory/Hunter). | 29.7% |
| 12 | 4096 | `MOB_HELPER` | Joins fights already in progress for allies in the room (§2 Helper). | 19.7% |
| 13 | 8192 | `MOB_AGGRESSIVE_EVIL` | Restricts standard aggression to evil-aligned victims. | 0.03% (1 mob) |
| 14 | 16384 | `MOB_AGGRESSIVE_NEUTRAL` | Restricts standard aggression to neutral-aligned victims. | 0.03% (1 mob) |
| 15 | 32768 | `MOB_AGGRESSIVE_GOOD` | Restricts standard aggression to good-aligned victims. | 0.03% (1 mob) |
| 16 | 65536 | `MOB_BODYGUARD` | Rescues its master's attackers' victims and vice versa (§2 Bodyguard). | 3.1% |
| 17 | 131072 | `MOB_SHADOW` | Marks incorporeal/ghost-type mobs; interacts with mental-combat handling in `perform_violence` (`fight.cpp:2741`). | 4.4% |
| 18 | 262144 | `MOB_SWITCHING` | Randomizes/retargets among multiple viable victims instead of committing to the first found. | 13.3% |
| 19 | 524288 | `MOB_NORECALC` | Opts a mob **prototype** out of `recalculate_mob()` during the OLC builder's "recalc all" pass (`SHAPE_RECALC_ALL`, `shapemob.cpp:2119-2124`) — a builder-tool mass-recalculation of authored stats, not anything the runtime AI loop checks. | 23.2% |
| 20 | 1048576 | `MOB_FAST` | Comment says "will mob_act when somebody enters", but that's stale — the flag itself is never tested in `mobact.cpp`/`interpre.cpp` (`SPECIAL_ENTER` fires unconditionally for any mob with a bound specproc, `act_move.cpp:447,451,860`, regardless of this bit). It **is** live elsewhere: at instantiation (`read_mobile`, `db.cpp:1247-1253`) a `MOB_FAST` mob is granted a permanent (`duration = -1`) `SPELL_ACTIVITY`/`APPLY_SPEED` affect that adds a flat `+1` to `GET_ENE_REGEN` (`handler.cpp:402-404`) — a small, permanent attack-speed buff — and it also adds a flat exp bonus for whoever kills it (`exp_with_modifiers`, `fight.cpp:1124`). | 28.5% |
| 21 | 2097152 | `MOB_PET` | Marks a tamed/purchased pet; makes it passive next to its master (unless a guardian) and exempts it from carrying/using items in `pet_shops` purchase flow. | — (runtime-set, not authored) |
| 22 | 4194304 | `MOB_HUNTER` | Actively paths toward remembered enemies room-to-room via BFS, rather than just re-attacking on sight (§2 Memory/Hunter). | 8.0% |
| 23 | 8388608 | `MOB_ORC_FRIEND` | Can be recruited as a follower by common orcs; also excluded from being assisted by `MOB_PET` helpers if it's a pet (`mobact.cpp:162`). | 0.8% |
| 24 | 16777216 | `MOB_RACE_GUARD` | Blocks players of a different race from entering the room (enforced elsewhere; not part of the tick AI). | 0.1% (2 mobs) |
| 25 | 33554432 | `MOB_ASSISTANT` | Unconditionally assists its master when the master is fighting (§2 Assistant). | 0.4% |
| 26 | 67108864 | `MOB_GUARDIAN` | Marks a summoned Guardian (Mystic spec, [specializations.md](specializations.md)); stays active next to its master and flees toward the master if separated mid-fight. | 0.4% |

## Special procedure catalog

### Assignment mechanism recap
Two independent binding tables, both consumed through the same `activate_char_special`/
`activate_obj_special` runtime dispatch (§4-§5 above): the **explicit vnum table**
(`assign_mobiles`/`assign_objects`/`assign_rooms`, `spec_ass.cpp:83-273`, run once at boot,
`db.cpp:421-425`) and the **numbered switch** (`virt_program_number`, `spec_ass.cpp:309-383`,
cases `1`-`32`, consulted via `store_prog_number` + `MOB_SPEC`, mainly at shapeshift time). The
tables below list every native specproc that exists in the source, grouped by purpose, each with
its dispatch `SPECIAL_*` flags and how it's typically bound.

### Skill trainers, shops, and mail (mostly `SPECIAL_COMMAND`)
| Function | File:line | Behavior |
|---|---|---|
| `guild` | `spec_pro.cpp:206` | Handles `practice`/`practise`. Refuses PCs with no `skills`/`knowledge` array, refuses if `WILL_TEACH` doesn't include the PC's race, refuses shadows and race-restricted (`RP_RACE_CHECK`) PCs. Lists or teaches skills from the guildmaster's `guildmasters[]` table, gated by prof level and skill-spec match. |
| `receptionist` (`gen_receptionist`) | `objsave.cpp:1612` | Rent/check-in desk — delegates to `gen_receptionist(ch, cmd, arg, RENT_FACTOR)`. |
| `shop_keeper` | `shop.cpp:494` | Buy/sell/value dispatch for a shop's `shop_index` entry; regenerates the keeper's cash pool each `SPECIAL_SELF` tick; on `SPECIAL_DAMAGE` retorts "Don't even think about it." instead of fighting back; on `SPECIAL_DEATH` donates its gold to charity (sets it to 0); auto-opens/closes on the shop's configured hours. |
| `pet_shops` | `spec_pro.cpp:417` | `list`/`buy` against the pets standing in the room one vnum above the shop's own room; strips carry/wear capacity from the purchased pet and follows it to the buyer. |
| `postmaster` | `mail.cpp:503` | `mail`/`check`/`receive` command dispatch into the mail subsystem; refuses NPC-without-descriptor callers. |
| `gen_board` | `boards.cpp:207` | Generic bulletin-board handler for `write`/`look`/`read`/`remove`/`next`/`send`, branching on `SCMD_NEWS`/`SCMD_MAIL` subcommand into the news vs. mail board. |
| `kit_room` | `spec_pro.cpp:573` | One-time `ask`-triggered starter-kit handout; computes race/class/level-gated item list from the `kit_eq[]` table (`spec_pro.cpp:527-569`), sets `PLR_WAS_KITTED` so it can't be repeated. |
| `resetter` | `spec_pro.cpp:2648` | Backs the "Guardian angel" `tell <angel> pracreset`/`reroll` mechanism ([class-system.md → The Angel](class-system.md)): matches a `TARGET_TEXT` command word against `wtl.targ2`, drops followers, and calls `do_pracreset`. |
| `obj_willpower` | `spec_pro.cpp:638` | Object-side upkeep: clears its own `prog_number` if it's no longer wielded by its holder (a self-disarming trigger, not a command handler). |

### Doors, transport, and world geometry
| Function | File:line | Behavior |
|---|---|---|
| `gatekeeper` | `spec_pro.cpp:693` | Opens gates at sunrise / closes at sunset automatically; also answers `say open <door>`/`knock`, with a short `WAIT_STATE`-delayed open, refusing enemies (`IS_AGGR_TO`) by raising an alarm (`yell`) instead, and refusing race-restricted (`RP_RACE_CHECK`) callers; a `MOB_CAN_SWIM` gatekeeper never opens on request. |
| `gatekeeper2` | `spec_pro.cpp:942` | "Paranoid" variant — gates are closed by default and never auto-opened at sunrise; otherwise identical knock/say-open flow to `gatekeeper`. |
| `gatekeeper_no_knock` | `spec_pro.cpp:873` | Simplified — only the sunrise/sunset auto open/close; does not react to `say`/`knock` at all. |
| `block_exit_north/east/south/west/up/down` | `spec_pro.cpp:2376-2646` (six near-identical functions, one per direction) | A mob "blocking" one specific exit: intercepts movement in that direction, rolls `BLOCK_CHANCE(host, ch, exit_width)` to physically stop the mover, and can be told to stand down via the `block`/movement command per direction. (An earlier generic, direction-parameterized `block_exit` is present but commented out, `spec_pro.cpp:2329-2374` — dead code, superseded by the six per-direction copies.) |
| `react_trap` | `spec_pro.cpp:2736` | Room-entry/command hook for a mob that has set a trap (`do_trap`): abandons the trap if the mob itself performs an incompatible command, otherwise re-runs `do_trap` targeting whoever entered/acted. |
| `ferry_boat` | `spec_pro.cpp:1187` | Object specproc on a boat: `enter <boat-name>` recursively carries the character (and any followers in the same room) across the configured `ferry_boat_data` route. |
| `ferry_captain` | `spec_pro.cpp:1218` | Drives a ferry NPC's arrival/departure flavor text along its configured route/timer. |
| `room_temple` | `spec_pro.cpp:1105` | `concentrate` command in a temple room refills the caster's spirit pool to 100 if below. |
| `vortex_elevator` | `spec_pro.cpp:3400` | Cosmetic "makes everyone dizzy" message generator for a themed room; explicitly an author's scratch/learning proc per its own comment. |

### Race/scripted combat & wandering AI (`SPECIAL_SELF` / `SPECIAL_ENTER`)
| Function | File:line | Behavior |
|---|---|---|
| `mob_cleric` | `spec_pro.cpp:1520` | Basic mystic-caster tick: pick target/spell via `choose_caster_target`/`choose_mystic_spell`, `do_cast`, refill spirit to 100 first. |
| `mob_magic_user` | `spec_pro.cpp:1553` | Basic mage-caster tick, mirrors `mob_cleric` using `choose_mage_spell`. |
| `mob_magic_user_spec` | `spec_pro.cpp:1733` (prog `31`) | The richer, keyword/HP-bracket-driven mage AI — see §3 above. |
| `mob_warrior` | `spec_pro.cpp:1870` (prog `5`) | Random kick/bash-or-nothing while fighting — see §3 above. |
| `mob_ranger` | `spec_pro.cpp:1917` (prog `15`) | Legacy ranger AI: ambush on sight/entry of an `IS_AGGR_TO` target, auto-hide, then track-following movement via `room_track`. |
| `mob_ranger_new` | `spec_pro.cpp:2068` (prog `32`) | Current ranger AI — see §3 above (wimpy gate, keyword-gated ambush/tracking, auto-rehide, `MOB_SWITCHING` support). |
| `mob_jig` | `spec_pro.cpp:2282` | A `jig`-command dance loop; cancels itself if the mob is forced into any command requiring better-than-resting position. |
| `snake` | `spec_pro.cpp:662` | While `POSITION_FIGHTING`, a level-scaled per-tick chance to bite and apply `spell_poison` instead of a normal attack. |
| `dragon` | `spec_pro.cpp:2909` (prog `30`) | While fighting, a 1-in-5 chance per tick to breathe on **everyone else in the room** simultaneously (`dice(level/2, 6)` damage, `SPELL_DRAGONSBREATH`) — the one specproc that damages the whole room in a single call rather than a single target. |
| `ghoul` | `spec_pro.cpp:2867` (prog `19`) | While fighting, occasionally (1-in-31) spawns an escort ghoul (vnum 15300) as a follower, up to 3 at a time; while not fighting, occasionally despawns/buries a childless follower ghoul. |
| `swarm` | `spec_pro.cpp:2907` | Stub — `return 0` unconditionally; present as an assignable placeholder with no behavior. |
| `ar_tarthalon` | `spec_pro.cpp:2771` (prog `18`) | Unique boss: while fighting, calls named lieutenant NPCs ("Balkazor", "Karahaz") by name via flavor `say`s and force-moves them room-to-room along a hard-coded room-number path toward the boss. |
| `vampire_huntress` | `spec_pro.cpp:2934` (prog `20`) | Unique boss (bat form): patrols a hard-coded room-to-room cycle when not fighting, reversing/holstering by time of day (day = hide, dawn = specific room); on encountering a PC has a 1-in-3 chance each of flying off / attacking / **abducting** the victim (teleport + poison + imprisonment behind auto-locked doors). Also disengages and flees to heal below 20% HP. |
| `thuringwethil` | `spec_pro.cpp:3154` (prog `21`) | Unique boss (human form): regenerates HP each tick unless a specific glowing-plate object is present in the room (which suppresses regen and eventually drives her off); dies permanently (and grants an achievement, `add_exploit_record`) below 500 HP; on reaching full HP while out of combat, transforms back into `vampire_huntress` (extracts self, spawns the bat-form mob in its place). |
| `vampire_doorkeep` | `spec_pro.cpp:3215` (prog `22`) | Opens/closes a specific door pair based on whether anyone in either adjoining room is carrying the "safe passage" light item (vnum 7008); occasionally spooks non-carriers with a flavor message. |
| `vampire_killer` | `spec_pro.cpp:3262` (prog `23`) | Unique jailer boss: instantly kills (`raw_kill`) an unguarded PC prisoner it finds (rare per-tick chance to give them an escape window), otherwise patrols a hard-coded dungeon room graph opening/closing iron doors toward whichever cell currently holds a victim. |
| `wolf_summoner` | `spec_pro.cpp:3414` (prog `26`) | While fighting, an **80%-per-tick** chance (`number(1,10) > CHANCE` with `CHANCE=2` — i.e. it *skips* the howl only 2 times in 10, not the reverse) to howl and spawn a follower wolf (vnum = its own vnum + 1) up to `level/3` at a time, and immediately sics the new wolf on its own current target. |
| `reciter` | `spec_pro.cpp:3458` (prog `27`) | Flavor NPC that recites a carried scroll's extended description one line per activation (50% skip chance) while not fighting; discards the scroll and stops once dead. |
| `herald` | `spec_pro.cpp:3513` (prog `28`) | Announces each new (non-NPC) arrival to a room by name/title exactly once, using its own `specials.memory` list purely as a "have I announced this person" set (an example of the memory data structure being repurposed for a non-combat role). |
| `healing_plant` | `spec_pro.cpp:3383` (prog `24`) | Passive room fixture: each `SPECIAL_SELF` tick, heals every **good-aligned** character in the room (not itself) for `1..max(1, level/2)`. |

### Object-side
| Function | File:line | Behavior |
|---|---|---|
| `obj_willpower` | `spec_pro.cpp:638` | See table above (self-clearing prog on unwield). |

## Worked example
**Grey Wolf**, vnum **2301** (`lib/world/mob/23.mob`), decoded field-by-field against
`load_mobiles` (`db.cpp:1259+`) and [world-files.md](../data-formats/world-files.md):

```
#2301
grey wolf~
a grey wolf~
A grey wolf is growling at you.~
   Standing on four strong legs, this beast is growling and showing his sharp
white teeth. ...
~
1128 3 0 N
(null)~
(null)~
7 28 14 7 65 97 5 84
0 30300 17
8 8 0 0 0
50 150 0 7259 0 0
0 27 105 2
1 10 10 10 10 10 10
121 0 0 0 0 0 0
```

**Flags** — `mob_action_flags = 1128`. `1128 = 8 + 32 + 64 + 1024`:
| Bit | Flag | Effect for this mob |
|---|---|---|
| 3 (8) | `MOB_ISNPC` | Forced by loader; not authored. |
| 5 (32) | `MOB_AGGRESSIVE` | Attacks any visible, non-`PRF_NOHASSLE` PC on sight each tick — no `_EVIL`/`_GOOD`/`_NEUTRAL` bit is set, so alignment is **not** checked; it attacks anyone. |
| 6 (64) | `MOB_STAY_ZONE` | Wanders freely, but only within its own zone. |
| 10 (1024) | `MOB_CAN_SWIM` | Can wander through water-sector rooms in its zone without a boat. |

Not set: `MOB_SPEC` (bit 0) — so this wolf has **no** specproc; all its behavior comes from the
generic `one_mobile_activity` loop. Not `MOB_SENTINEL`, so it does wander (subject to
`MOB_STAY_ZONE`). Not `MOB_MEMORY`/`MOB_HUNTER`/`MOB_WIMPY`/`MOB_HELPER`/etc.

`affected_by = 3 = AFF_DETECT_HIDDEN | AFF_INFRARED` (`structs.h:707-708`) — permanently sees
invisible-to-darkness and detects hidden characters, appropriate for a predator. `alignment = 0`
(true neutral). Death cries are the literal `(null)~` placeholder noted in
[world-files.md](../data-formats/world-files.md) — this wolf has no custom death message.

**Stats**: `level 7`, file `OB 28 / parry 14 / dodge 7`, `hp 65-97` (rolled per spawn — always
full at spawn), `damage 5`, `ENE_regen 84`, `gold 0`, `exp 30300`, `position/default_position`
both `8` (`POSITION_STANDING`), `sex 0`, `race 0` (`RACE_GOD` — mobs frequently leave race at the
default/unused value since it mostly only matters for `IS_AGGR_TO`'s per-race `pref` bit and PC
racial interactions), `pref 0` (no race-specific target list — its aggression is purely the
`MOB_AGGRESSIVE` flag, not race-war logic), `weight 50`, `height 150`, `store_prog_number 0`
(confirms no specproc/script), `butcher_item 7259` (a skinning/hide drop), `prof 0`, `mana 27`,
`move 105`, `bodytype 2`, `saving_throw 1`, all six core stats `10`, `language 121`.

**Live combat numbers** via the NPC branches (§ Mob stat derivation): with `GET_BAL_STR = 10`
(STR 10 is under any race's `max_race_str`, so it passes through unmodified, `utils.h:357`):
```
get_real_OB    = 28 + 10 + 15 - 0 + 7/2  = 56
get_real_parry = 14 + 7/2 + 15           = 32
get_real_dodge =  7 + 10 - 5 + 7/2       = 15
```

**Per-tick behavior**: every ~6 s (25% chance per `mobile_activity` pass), if not fighting and
awake, it rolls a 1-in-45.5-ish (0-45 roll, only 0-5 valid) chance to wander to an adjacent room in
its home zone (water rooms included, thanks to `MOB_CAN_SWIM`); independent of that, it scans the
room every tick for any visible non-NPC PC (regardless of alignment or sleep state — no
`MOB_WIMPY`) and, if one is found, immediately `do_hit`s them, short-circuiting the wander roll for
that tick. Once fighting, the generic energy-driven auto-attack (`combat-loop.md`) takes over at
`ENE_regen = 84` per pulse until `ENERGY` crosses `1200`; it has no kick/bash/spell kit (no
specproc), so its only combat behavior is that plain auto-attack swinging at OB 56 into
whatever parry/dodge the victim brings.

## Open questions
- **`MOB_SPEC` set with no bound function — corrected worked case.** An earlier pass of this doc
  cited mobs carrying `MOB_SPEC` plus `store_prog_number = 1104` (e.g. vnums 10000/10001/10006/
  10010/10011/10015/10016/10018/10022/10023/10024/10059/10060 — tailor/leatherworker/barmaid/
  armorer/barman-type shop NPCs in `lib/world/mob/100.mob`) as "unresolvable, no bound function."
  That was **wrong**: `1104` isn't a valid `virt_program_number()` case, but all 13 of those vnums
  are listed as a shop's `keeper` field in `lib/world/shp/100.shp`, so `assign_the_shopkeepers()`
  (see §4) binds `mob_index[nr].func = shop_keeper` for them at boot — they are **not** orphaned.
  (Three more `MOB_SPEC`+`1104` vnums — 10005/10013/10017 — are separately bound to `receptionist`
  via the explicit `assign_mobiles()` table.) The *general* phenomenon this was trying to illustrate
  is still real, just from a more mundane cause: a live scan found **145 mobs** with `MOB_SPEC` set,
  `store_prog_number = 0`, not present in `assign_mobiles()`/`assign_objects()`/`assign_rooms()`,
  and not any shop's keeper vnum — for these, `mobact.cpp:120-124`'s dynamic-resolution fallback
  never even fires (it's gated on `store_prog_number` being truthy), so `MOB_SPEC` is a pure no-op
  for them. Example: vnum `2007` (`lib/world/mob/20.mob`), name `"scholar renowned man human
  guildmaster closedrots"` — the `closedrots` keyword suggests retired/disabled content, consistent
  with these being authoring leftovers rather than an intentional design. Whether *all* 145 are
  similarly vestigial, or some are load-bearing through a mechanism this doc didn't find, is
  unconfirmed.
- **Where `remember()` is actually called from combat code — resolved.** An earlier pass of this
  doc treated this as untraced; it is in fact called at `fight.cpp:2592`, right where a victim is
  newly placed into `POSITION_FIGHTING`: `if (IS_NPC(victim) && IS_SET(victim->specials2.act,
  MOB_MEMORY) && !IS_NPC(ch) && (GET_LEVEL(ch) < LEVEL_IMMORT)) remember(victim, ch);` — i.e. a
  `MOB_MEMORY` mob remembers a non-NPC, sub-immortal attacker the instant that attacker's hit puts
  the mob into fighting position (which is what "struck first while not fighting" cashes out to in
  code). Other `remember()` call sites exist for unrelated flows: `fight.cpp:1694` (a different
  combat path), `clerics.cpp:247`, `ranger.cpp:1698`, and `spec_pro.cpp:3544` (a specproc-driven
  case) — not traced further here.
- **`cryogenicist`** (`objsave.cpp:64`) is declared but has no body anywhere in the tree — confirm
  whether this is truly dead or whether an out-of-tree/older revision defines it.
- **Exact skinning/`butcher_item` interaction** with mob AI (if any) was out of scope here — it's
  cross-referenced only descriptively in the worked example.
