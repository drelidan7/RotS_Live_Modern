# Movement & zone runtime

**Source files:** `src/act_move.cpp` (`do_move`, `check_simple_move`, `room_move_cost`,
`racial_movement_reduction`, `perform_move_mount`, `do_open`/`do_close`/`do_lock`/`do_unlock`,
`do_follow`/`do_lead`/`do_ride`(`ranger.cpp`)/`do_pull`); `src/ranger.cpp` (`do_ride`, `do_dismount`,
`do_pick`, `do_track`); `src/act_offe.cpp` (`do_flee`, `do_bash` mode 3 door-bash); `src/consts.cpp`
(`movement_loss[]:1708`, `sector_types[]:1892`, `room_bits[]:1886`, `exit_bits[]:1897`);
`src/structs.h` (`ROOM_`/`EX_`/`SECT_` bit defines `:504-566`); `src/limits.cpp` (`move_gain:273`,
`fast_update:1483`, `check_breathing:929`); `src/utility.cpp` (`can_breathe:1618`,
`get_exit_width:377`, `default_exit_width[]:164`); `src/handler.cpp` (`can_swim:2491`,
`add_follower:947`, `circle_follow:1075`); `src/graph.cpp` (`find_first_step` BFS, `hunt_victim`,
`show_tracks`, `track_desc`); `src/zone.cpp` (`zone_update:281`, `reset_zone:478`, `is_empty:922`,
`check_if_flag:402`); `src/db.cpp` (`set_exit_state:1596`); `src/spec_pro.cpp` (`block_exit_*:2376+`,
`ferry_boat:1187`, `ferry_captain:1218`); `src/mobact.cpp` (hunting-memory AI `:367-417`).
**Status:** 🟡 partial — movement legality, cost formula, regen, doors, tracking/hunting, and zone
reset timing are covered and verified against live `.wld` data; some specprocs (ferries, race
guards) are noted but not exhaustively enumerated.

> **Scope.** The on-disk `.wld`/`.zon` grammar (room/exit fields, zone command letters) is
> [world-files.md](../data-formats/world-files.md) — this doc does not repeat field layouts, only
> their *runtime* meaning. Weather/light's effect on vision and combat is
> [time-and-weather.md §6](time-and-weather.md); teleport/blink/recall spells (which respect the
> `NO_TELEPORT` room flag) are [magic-system.md](magic-system.md).

---

## 1. Purpose

Two intertwined runtimes live here: **movement** (how a character legally steps from one room to
another, what it costs, and how that cost regenerates) and **zone** (how the persistent world
periodically repopulates itself). Both consume the room/exit/zone data structures defined on disk
(world-files.md) but add server-side state — movement points, follow chains, door bit toggles, BFS
pathing, and reset timers — that never touches disk.

---

## 2. Data structures

- **`room_direction_data`** (per-room, per-direction; `structs.h`, populated by `setup_dir`,
  world-files.md): `exit_info` (live `EX_*` bitvector, mutated at runtime by open/close/lock/pick/
  bash), `key` (key object vnum, `-1` = no keyhole), `to_room` (real room index after `renum_world`),
  `keyword`, `exit_width` (RotS; 0 = "use sector default", see §7.3).
- **`room_data.room_flags`** — live `ROOM_*` bits (`structs.h:504-521`):

  | Bit | Name | Runtime effect |
  |--:|---|---|
  | 0 | `DARK` | Room is always dark regardless of sun (rare; most darkness is sun-driven — time-and-weather.md §6) |
  | 1 | `DEATH` | Instant `raw_kill` on entry (movement, mount-carry, or door-bash) — no save |
  | 2 | `NO_MOB` | Blocks unaffiliated (non-charmed) NPCs from entering (`check_simple_move` case 5) |
  | 3 | `INDOORS` | Marks a room as inside; gates riding (mounts can't enter), `enter`/`leave`, and (elsewhere) outdoor-only sun/moon effects |
  | 4 | `NORIDE` | Riders can't cross into this room (`check_simple_move` case 7) |
  | 5 | `PERMAFFECT`(`*PERM*`) | Marks a room-affect as permanent (world-files.md `F` field) |
  | 6 | `SHADOWY` | Room is shadow-world-adjacent (Mist of Baazunga et al.; not a movement gate) |
  | 7 | `NO_MAGIC` | Blocks spellcasting in the room (not movement) |
  | 8 | `TUNNEL` | **Defined, named in `room_bits[]`, never read by any `IS_SET(...,TUNNEL)` in the codebase — dead flag.** See §9. |
  | 9 | `PRIVATE` | Blocks immortal **`goto`/`at` teleport** into an *occupied* room below `LEVEL_GRGOD` (`act_wiz.cpp:240`) — not a walking restriction |
  | 10 | `GODROOM` | Blocks immortal teleport below `LEVEL_GOD` |
  | 11 | `BFS_MARK` | Scratch bit used *by* the tracking BFS (§6.1) — not set in world files |
  | 12 | `DRINK_WATER` | Room offers drinkable water (fountains/rivers) |
  | 13 | `DRINK_POISON` | Room's water is poisoned |
  | 14 | `SECURITYROOM` | Immortal-teleport gate (`LEVEL_GRGOD`) |
  | 15 | `PEACEROOM` | Blocks `hit`/`bash` initiation (`act_offe.cpp:454`) — not a movement gate |
  | 16 | `NO_TELEPORT` | Blocks mage teleport/blink/summon (magic-system.md) |
  | 17 | `HIDE_VNUM` | Cosmetic — hides the room vnum from immortals' `HOLYLIGHT` display |

- **`dir_option->exit_info`** — live `EX_*` bitvector (`structs.h:536-550`, all confirmed used):
  `ISDOOR(1)`, `CLOSED(2)`, `LOCKED(4)`, `NOFLEE(8)`, `RSLOCKED(16, unused beyond definition — see §9)`,
  `PICKPROOF(32)`, `DOORISHEAVY(64)`, `NOBREAK(128)`, `NO_LOOK(256)`, `ISHIDDEN(512)`, `ISBROKEN(1024)`,
  `NORIDE(2048)`, `NOBLINK(4096)`, `LEVER(8192)`, `NOWALK(16384)`.
- **`sector_type`** (`SECT_*`, `structs.h:554-566`) indexes `movement_loss[]` (§4) and
  `default_exit_width[]` (§7.3); 13 live values, `Inside(0)…Swamp(12)` (see world-files.md for the
  full room-file mapping).
- **`char_data.mount_data`**: `mount`/`mount_number` (the char's own mount, if riding),
  `rider`/`rider_number` (who rides *this* char, if it is a mount), `next_rider`/`next_rider_number`
  (linked list of extra riders/mounts being led together, §5.3).
- **`zone_data`** (`zone.cpp`/world-files.md): `age` (minutes since last reset, incremented in
  `zone_update`), `lifespan`, `reset_mode`, `cmd[]` (reset command list).
- **Tracking state** (`room_data.room_track[NUM_OF_TRACKS]` / `.bleed_track[NUM_OF_BLOOD_TRAILS]`):
  each slot holds `char_number` (mob `nr`, or `-race` for a PC), `data` (`hour*8 + direction`),
  `condition` (age counter, incremented hourly — time-and-weather.md §6 "Tracking & footprints").

---

## 3. Movement command flow (`do_move` / `check_simple_move`)

`do_move` (`act_move.cpp:643`) is the single entry point for all step-commands (`north`/`east`/…,
`flee`, `follow`-relay, mount-carry). High-level flow:

1. **Haze scramble.** If `AFF_HAZE` and `number(1,4)==1`, the requested direction is replaced with a
   random one (`:654-657`) — the player moves somewhere they didn't ask for.
2. **Delay/write guard.** A pending `wait_value` with priority ≤ 30 aborts the current delay
   (`:660-663`); writers (`PLR_WRITING`) and anyone below `POSITION_FIGHTING` can't move
   (`check_simple_move:160`).
3. **Ridden-mount short-circuit.** If `IS_RIDDEN(ch)` (i.e. `ch` *is* a mount with a rider), the
   command instead calls `perform_move_mount` directly (`:668-671`) — see §5.3.
4. **Exit existence.** No `dir_option[cmd]` or `to_room == NOWHERE` → *"You cannot go that way."*
5. **NPC auto-open.** An NPC (not already riding through) facing a closed, non-hidden, non-locked
   door auto-issues `do_open` before attempting the move (`:680-688`).
6. **`CAN_GO` legality** (`utils.h`, wraps `exit_info` bits): fails on `EX_CLOSED` without
   `EX_ISHIDDEN`/`PRF_HOLYLIGHT` → *"seems to be closed"*; a hidden exit reads as "cannot go that
   way" instead (players don't get told a secret door exists).
7. **Charm leash.** A charmed follower can't voluntarily leave a room its master is standing in
   (except while following/fleeing) — *"The thought of leaving your master makes you weep."*
   (`:710-715`).
8. **`check_simple_move`** computes legality + cost (§3.1) for the mover (and, if riding, separately
   for the mount, §5.3).
9. **Orc-pet jump-the-queue.** Charmed `MOB_ORC_FRIEND` pets in the same room get a 50% chance to
   move *before* their master, purely for visual randomness (`:766-784`).
10. **Sneak check on leaving.** Unless `AFF_SNEAK` beats a `number(0,125) > SKILL_SNEAK + stealth`
    roll, everyone in the room sees `"$n leaves <dir>."` A successful sneak instead calls
    `snuck_out(ch)` and **multiplies the movement cost by 1.5** (`:786-799`).
11. **Racial terrain discount** applied to the *origin* room's sector (§4.2).
12. **Track/blood-trail deposit** (unless flying, shadow, or a race-appropriate `stalk` skill roll
    erases footing — `:808-829`, `set_blood_trail` for `mark`-affected movers).
13. **Zone-bound item drop.** Crossing a zone boundary force-drops any `ITEM_STAY_ZONE` gear/inventory
    (recursing into containers) via `prohibit_item_stay_zone_move` (`:459-510, 836-838`).
14. **Room swap**, `do_look`, **movement points deducted**, sneak check on *entering* (symmetrical to
    #10, threshold shifted by -25 so entering is easier to sneak than leaving), `SPECIAL_ENTER` hook,
    `ON_ENTER` trigger, and `DEATH`-flag kill check (`:840-866`).
15. **Follower relay.** Every follower still in the vacated room and standing gets the same command
    replayed via `command_interpreter` (§5.4); a party can only lead 2 mounts safely — a 3rd+ has a
    1-in-20 chance per step to fall behind (`:990-996`).

### 3.1 `check_simple_move` — legality + cost (`act_move.cpp:132`)

Returns 0 (success) or one of 8 failure codes, 1–8 (comment block `:133-149`; codes 4/5 for
"mount exhausted"/"mount would not move" are marked **obsolete** in the source comment — dead
return paths kept for switch-completeness):

| Code | Meaning |
|--:|---|
| 0 | Success |
| 1 | Blocked by a `SPECIAL_COMMAND` hook, sub-`POSITION_FIGHTING`, `PLR_WRITING`, no exit, `to_room==NOWHERE`, a wait-delay, or `ON_BEFORE_ENTER` trigger veto |
| 2 | Needs a boat/swim skill (`SECT_WATER_NOSWIM` only, see §4.3) |
| 3 | Not enough movement points |
| 4 | *(obsolete)* mount exhausted |
| 5 | *(obsolete)* mount would not move — **also live**: NPC blocked by `NO_MOB`/`MOB_STAY_ZONE`/`MOB_STAY_TYPE` |
| 6 | Rider doesn't control this mount |
| 7 | Riders can't enter `INDOORS` or `NORIDE`/`EX_NORIDE` rooms |
| 8 | `EX_NOWALK` exit, or a `MOB_RACE_GUARD` NPC of a different race occupies the destination |

Checks in order (`:151-277`): special-command interception → position/writing → exit existence →
delay → `ON_BEFORE_ENTER` trigger → `EX_NOWALK` → **cost computation** (§4) → bloodied/Power-of-Arda
side effects → shadow-form free movement → water legality (§4.3) → NPC zone/type/NO_MOB fencing →
movement-point sufficiency → mount control/INDOORS/NORIDE → race-guard scan of the destination room's
occupants.

---

## 4. Movement-point cost

### 4.1 The base cost — `room_move_cost` (`act_move.cpp:83`)

Called **twice** per step (once for the room being left, once for the room being entered) and
averaged; `new_room` is whichever room is being costed:

```
str = character.tmpabilities.str
if should_double_strength(character):        # rider/mount is Heavy Fighting spec
    str *= 2

base = max(20 + carry_weight/str/10,
           70 + carry_weight/str/20)          # note: base is a plain max(), not the "load" curve
                                               # the comment on movement_loss[] describes

if character is a MOB_MOUNT:
    base = carry_weight / GET_STR(character) / 20     # mounts get a much smaller load penalty
    if base < 100: base = max(75, 50 + base/2)
elif base < 100:
    base = 100                                          # everyone on foot has a 100 floor

base += get_leg_encumbrance(character) * 2               # armor-derived leg encumbrance, char_utils.cpp:777

penalty = movement_loss[new_room.sector_type]             # halved (min 1) for Stealth-spec chars not mounted
if character is a MOB_MOUNT:
    cost = penalty * (base + number(0,99)) * 2/5
else:
    cost = penalty * (base + number(0,99)) / 2

if IS_RIDING(character):        # this call is costing the *rider's* half of a ridden move
    cost = (cost / (120 + 2*SKILL_RIDE + SKILL_ANIMALS/2)) * 5/4
else:
    cost = cost / 100

return max(cost, 1)             # every room-move costs at least 1 point
```

`movement_loss[]` (`consts.cpp:1708-1723`, indexed by `SECT_*`) — verified against the sector
enum and against live `.wld` sector numbers (§8):

| Sector | id | Cost | Sector | id | Cost |
|---|--:|--:|---|--:|--:|
| Inside | 0 | 2 | Water (swim) | 6 | 10 |
| City | 1 | 3 | Water (no-swim) | 7 | 12 |
| Field | 2 | 4 | Underwater | 8 | 12 |
| Forest | 3 | 7 | Road | 9 | 3 |
| Hills | 4 | 10 | Crack (earthquake) | 10 | 12 |
| Mountains | 5 | 11 | Dense forest | 11 | 9 |
| — | | | Swamp | 12 | 12 |

Indices 13–23 are all `0` (padding for future sectors; unused by any live sector id).

### 4.2 Racial terrain discount — `racial_movement_reduction` (`act_move.cpp:515`)

Applied **once**, after `check_simple_move` returns, using the sector of the room the character is
**leaving** (not entering) and the character's race — halves the already-computed `need_move`:

| Race | Sector | | Race | Sector |
|---|---|---|---|---|
| Human | Field | | Wood-Elf / Beorning | Forest or Dense Forest |
| Dwarf | Mountain | | Uruk-Hai / Orc | Swamp |
| Hobbit | Hills | | Haradrim / Magus / Olog-Hai | **none — explicitly excluded** (`:517-519`) |

This is a **second, independent** discount from the `get_room_move_penalty` Stealth-spec halving in
§4.1 — a Stealth-spec Human walking out of a field room can, in principle, stack both.

### 4.3 Water — swim vs. no-swim (`check_simple_move:195-233`)

The boat/swim check only fires when **either** the origin **or** destination sector is
`SECT_WATER_NOSWIM` — plain `SECT_WATER_SWIM` (rivers, shallow water) costs movement like any other
sector (10, per §4.1) with **no swim-skill or boat requirement at all**. For `NOSWIM` water:
- `!can_swim(ch) && !IS_RIDING(ch)` → **blocked** (return code 2, `check_simple_move:202`).
- Riding always passes (mounts "swim" the rider through) regardless of `can_swim`.
- Otherwise (i.e. `can_swim(ch)` was true, or riding): needs `ITEM_BOAT` equipped or carried, **or**
  a swim-skill discount `m = (GET_PROF_LEVEL(PROF_RANGER) + GET_SKILL(SKILL_SWIM)) / 20`, subtracted
  from the move cost (floor 1) — the code comment states a maxed ranger/swim (36+36=72 → m=3) can
  cross a `NOSWIM` room for as little as 1 point, versus the sector's base cost of 12.
- **`can_swim` gates PCs on the `PRF_SWIM` preference *before* anything else** (`handler.cpp:2500`):
  `if (!IS_NPC(ch) && !PRF_FLAGGED(ch, PRF_SWIM)) return FALSE;` runs first and short-circuits every
  other path in the function — a PC who hasn't typed `swim` (toggling `PRF_SWIM`) gets `can_swim()==
  false` and is blocked from `NOSWIM` water **even while carrying/wearing an `ITEM_BOAT`**, because
  `check_simple_move`'s own boat scan only runs in the `else` branch that `can_swim`/riding must reach
  first. A boat alone does not let an un-toggled PC cross `NOSWIM` water.

### 4.4 Sneak surcharge

A successful sneak-out (§3 step 10) multiplies the *entire* `need_move` by 1.5 — sneaking costs
noticeably more stamina than an announced walk.

---

## 5. Riding, following, and fleeing

### 5.1 Mounting/dismounting (`ranger.cpp:78` `do_ride`, `:204` `do_dismount`)
Only `GET_BODYTYPE(ch)==1` (humanoid) characters can ride; the target must be an `IS_NPC` flagged
`MOB_MOUNT`, not hostile (`IS_AGGR_TO`) unless calmed, not itself already mounted on another creature
(`potential_mount->mount_data.mount`, `:135`), not following someone else, and — if under `SKILL_CALM`
— the rider needs enough `is_strong_enough_to_tame` skill. A mount that already **has** a rider is
*not* rejected — mounting it instead joins the `next_rider` chain (see below), so multiple riders can
legitimately share one mount. Mounting sets up the doubly-tracked `mount_data` links on both sides and
adds the mount's weight to the rider's carried-weight ledger
(`IS_CARRYING_W(mount) += weight(rider) + carried(rider)`, `:201`). `do_dismount` tears the link down
and, for an NPC mount whose primary rider just dismounted (and isn't mid-fight), re-attaches it as a
regular follower (`:225-228`).

### 5.2 Movement while mounted
`do_move` branches (`:867-971`) into a **rider-then-mount** two-step: `check_simple_move` runs once
for the rider (legality/cost against the rider's stats) and once more for the mount itself with
`mode = SCMD_MOUNT`. Since the mount is an NPC, this second call would normally be subject to the
`NO_MOB` room-flag fencing every NPC gets — but the `NO_MOB` check is explicitly gated behind
`mode != SCMD_MOUNT` (`check_simple_move:237-239`), so a ridden mount is exempt from `NO_MOB` while
its rider is controlling it. `MOB_STAY_ZONE`/`MOB_STAY_TYPE`, by contrast, are only gated behind
`mode != SCMD_FOLLOW` (`:241-246`) and still apply to the mount even when ridden. Both move-point
pools are debited separately before `perform_move_mount` actually relocates everyone.

### 5.3 `perform_move_mount` (`act_move.cpp:309`) — moving the mount and its whole rider chain
Used both for the ridden-mount short-circuit (§3 step 3) and as the tail call of a rider-initiated
move. It replays `check_simple_move` for every rider in the `next_rider` chain (mode `SCMD_CARRIED`),
debiting each rider's own move points (with the same bloodied +2 and evil-race Power-of-Arda side
effects as a normal step), forcibly dismounts (`stop_riding`) any rider for whom the move fails, and
aborts the *entire* group's move (`stop_riding_all`) if any check hard-fails after the loop. On
success, every rider and the mount are moved atomically, look/enter messages fire for each, and a
single `DEATH`-flag check kills the whole convoy if the destination is lethal.

### 5.4 Following & group movement (`handler.cpp:947` `add_follower`, `:1075` `circle_follow`)
`follow` sets `ch->master` and links into the leader's `followers` list; `circle_follow` rejects
follow-loops. `do_move` replays the move command, synchronously and in the same server pulse, on
every follower still standing in the vacated room once the leader's own move succeeds (§3 step 15;
the relay is a direct `command_interpreter` call inside the same `do_move` invocation, not a deferred
tick) — followers aren't teleported alongside; each one re-runs `check_simple_move` itself and pays
its own movement cost, so a follower can fail to keep up (out of moves, blocked exit, etc.) even
though the leader made it through. `do_lead` lets a player drag a docile mount along the same way
without riding it.
Orcs are the one race that **cannot** use player-to-player following at all (races.md §1.8) — their
"followers" are always `MOB_ORC_FRIEND` pets from `recruit`.

### 5.5 Fleeing (`act_offe.cpp:332` `do_flee`)
Berserk-tactics characters can't flee. Up to 6 random directions are sampled looking for one candidate
that passes `CAN_GO`, isn't `EX_NOFLEE`/`EX_NOWALK`, doesn't lead to a `DEATH` room, and (for NPCs)
respects `MOB_STAY_ZONE`/`MOB_STAY_TYPE` (`:348-361`). As soon as **one** such candidate is found,
`check_simple_move(mode=SCMD_FLEE)` re-validates/prices that single direction (`:368`) and the loop
unconditionally `break`s (`:411`) — `do_flee` does **not** retry a different direction if this one
check fails; a failed flee this round means "PANIC! You couldn't escape!" (`:416`) below, not another
roll.
A **coin-flip** (`number(0,1)`) decides whether a *failed* check (out of moves, no swim, etc.) also
prints a more specific reason ("too exhausted to flee", etc.) before that — so fleeing can silently
"not really try" half the time, but the generic PANIC message always fires on any failure, not only
after exhausting all 6 direction samples. A successful flee costs the fleeing character (and,
symmetrically, whoever it was fighting) experience proportional to
`GET_LEVEL(ch) + GET_LEVEL(opponent)`, breaks all fight links in the room, clears `AFF_HUNT`, and
performs the move via `do_move(..., SCMD_FLEE)`.

---

## 6. Special movement

### 6.1 Tracking & hunting — BFS pathing (`graph.cpp`)
`find_first_step(src, target)` is a textbook BFS over the room graph, with two RotS wrinkles:
- **`IS_MARKED`** (the "don't revisit" bit) also **permanently excludes `NO_MOB` and `DEATH` rooms**
  from any path (`:55`) — hunting mobs and the `track` skill's pathing will never route through
  either, even if it's the only way to the target.
- **`IS_CLOSED`/`VALID_EDGE`** under `TRACK_THROUGH_DOORS` (the live compile-time setting, `:11,58`)
  treats an edge as passable **unless** it is `EX_LOCKED` or `EX_ISHIDDEN` *and* (`EX_CLOSED` **or**
  not a door at all) (`:59`) — see §9 for why that boolean reads oddly. In practice: open or
  plain-closed-but-unlocked doors never block a hunt/track path; a locked door always blocks (locked
  implies closed by game convention). A **hidden door only blocks while it is also closed** — an
  `EX_ISHIDDEN` door that has been opened (no `EX_CLOSED`) satisfies neither side of the `&&` and is
  fully passable to the BFS, exactly like a normal open door, even though it's still invisible to a
  non-`HOLYLIGHT` `look`/`exits`.

Two consumers:
- **`do_wiztrack`** (immortal command) prints the first-step direction toward a named character.
- **`hunt_victim`** (`mobact.cpp:367-417`, driven every `mobile_activity` pulse) — a mob flagged
  `MOB_HUNTER`, carrying `AFF_HUNT`, or with a `MOB_MEMORY` grudge re-derives a path to its
  memorized enemy every activity tick and takes exactly one `do_move` step toward it (attacking
  immediately if that step lands it in the same room). A **confuse** affect can randomly force
  `BFS_NO_PATH` instead of the real path (`get_confuse_modifier`, `:400-407`), causing a confused
  hunter to visibly lose the trail.

The player-facing **`track`** command (`ranger.cpp:233`) doesn't use BFS at all — it's a
`SKILL_TRACK` roll against the room's `room_track[]` footprint array (`show_tracks`, `graph.cpp:301`),
gated by a `WAIT_STATE` delay, blocked outright in water sectors, and reporting one line per matching,
un-aged-out footprint with an age-derived adjective (`track_desc`, `:245-285`, thresholds 0/1/2/3-4/
5-7/8-10/11-15/16-20/21-24/25+). `show_blood_trail` is the parallel system for the Haradrim `mark`
skill's blood trails (`bleed_track[]`), using `SKILL_MARK` instead of `SKILL_TRACK`.

### 6.2 Doors — open/close/lock/unlock/pick/bash (`act_move.cpp`, `ranger.cpp:550`)
All five interactive commands (`open`, `close`, `lock`, `unlock`, `pick`) share one shape: resolve a
direction or a carried/room container via `find_door`/`generic_find`, validate the relevant `EX_*`
bits in order (broken → not-a-door → already in the requested state → locked/lockable → pickproof/
lever), toggle the bit, message the room, and then **mirror the toggle onto the reverse exit of the
destination room** if one exists and actually points back (`act_move.cpp:1123-1134` for open,
similarly for close/lock/unlock/pick) — this is how a door "syncs" both sides without a shared
object. `EX_LEVER` doors refuse manual open/close entirely (must be `pull`ed, §6.4). A broken
(`EX_ISBROKEN`) door can't be manipulated normally; `set_exit_state` (used by zone `D` commands and
`shapezon`) explicitly clears `ISBROKEN` whenever a zone reset re-sets a door's state and echoes
*"The `<door>` blurs briefly"* to anyone present (`db.cpp:1626-1634`).

`pick` (`ranger.cpp:550`) additionally requires `!EX_PICKPROOF`/`!CONT_PICKPROOF` and a
`number(1,101) <= SKILL_PICK_LOCK` roll; success also propagates the unlock to the reverse exit.
`do_bash` mode 3 (`act_offe.cpp:586`) is the destructive alternative for doors that resist picking:
probability `= (SKILL_BASH/10 + STR-20) * 5`, with light races (Wood-Elf/High-Elf/Hobbit) forced to
`-1` (auto-fail, flavored as "throwing your light body" uselessly), Uruk/Dwarf +10, Beorning/Olog +20,
and an automatic `-1` (impossible) against `EX_DOORISHEAVY` or `EX_NOBREAK`. Failure costs HP/moves
and can drop the basher to `POSITION_RESTING`; success sets `EX_ISBROKEN`, clears `CLOSED|LOCKED` on
**both** sides, and knocks the basher through into the destination room.

### 6.3 Guard/blocking specprocs (`spec_pro.cpp:2376+`)
`block_exit_north/east/south/west/up/down` let a stationary NPC contest a specific exit without a
literal door: on a `CMD_<dir>` attempt by another character in the room, it rolls
`BLOCK_CHANCE = (SKILL_BLOCK+110)/(1+width)/(seen?1:2)/(standing?1:3) - 3*width) * host_weight /
(target_weight + 10*target_dex)` against `number(1,100)`, where `width = get_exit_width(room, dir)`
(§7.3), `seen = CAN_SEE(host, ch)` (can the *guard* see the mover?), and
`standing = GET_POS(host) >= POSITION_STANDING` (is the *guard* standing?) — a wider passage is
easier to slip past, a heavier guard (relative to the passer's weight+dexterity) blocks more
effectively, and the guard blocks *worse*, not better, in either of two cases: when the guard can't
see the passer (an invisible/hidden mover divides the chance by 2), or when the guard itself isn't
standing (a sitting/resting guard divides its own chance by 3). Both cases favor the passer slipping
through, the opposite of "helping the guard." A near-identical unified `SPECIAL(block_exit)`
taking a direction argument exists but is **commented out** (`:2316-2374`) — only the six
per-direction copies are compiled and assignable to mobs.

### 6.4 Levers (`do_pull`, `act_move.cpp:1855`)
`ITEM_LEVER` objects toggle a specific room's exit (`value[0]`=room vnum, `value[1]`=direction) via
`EX_CLOSED`, independent of the puller's own location, and mirror the toggle onto the destination's
reverse exit exactly like a manual door command — the mechanism behind `EX_LEVER` doors that refuse
direct open/close.

### 6.5 Ferries (`spec_pro.cpp:1187` `ferry_boat`, `:1218` `ferry_captain`)
A `ferry_boat` object special intercepts `enter <name>` when the object's name matches, looks up a
`ferry_boat_data[]` route table by the object's `prog_number`, and moves the entering character via
`_recursive_move` onto the ferry object's room. `ferry_captain` (an NPC special) independently walks
the ferry object itself between the route's fixed rooms over time, carrying whoever is aboard — a
scripted point-to-point transit system layered on top of normal room movement, not a BFS/pathing
system.

### 6.6 Falling, swimming, and drowning
There is no fall-damage/pit mechanic tied to sector type — the closest analog is **drowning**:
`can_breathe` (`utility.cpp:1618`) returns false in `SECT_UNDERWATER` (unless `AFF_BREATHE`,
shadow-form, or Undead race) or in `SECT_WATER_NOSWIM` for a character who can't swim. Every
`fast_update` tick (every `PULSE_FAST_UPDATE` = 3 real seconds, `limits.cpp:1526-1530`),
`check_breathing` (`:929`) either decays or grows a `SPELL_ASPHYXIATION` affect's `modifier` by 1.
Once the modifier exceeds 20 the character starts taking `modifier/5` damage per tick and losing
moves (floor 10); past a **death threshold** (`limits.cpp:1298-1320`) — 40 normally, **80** inside the
hardcoded zone vnum 262 ("under_water_zone") — the character drowns outright via `raw_kill`.
**Swimming** is a real, checked ability: `can_swim` (`handler.cpp:2491`) is true for shadows; for
NPCs flagged `MOB_CAN_SWIM` or flying; for a PC or NPC under an `AFF_SWIM` affect or carrying/wearing
an `ITEM_BOAT`; or for a PC with `SKILL_SWIM > 0`. **All** of these PC paths, however, are gated
behind the `PRF_SWIM` preference toggle checked first in the function (§4.3) — a PC who hasn't run
`toggle swim` returns `false` immediately, before `AFF_SWIM`/boat/skill are even reached, so `AFF_SWIM`
and boat possession only matter for NPCs or for PCs who already have `PRF_SWIM` on. It only matters
for entering `SECT_WATER_NOSWIM` (§4.3); `SECT_WATER_SWIM` never checks it.

---

## 7. Move-point regeneration (`limits.cpp`)

### 7.1 Cadence
`fast_update()` (`limits.cpp:1483`, called every `PULSE_FAST_UPDATE` = 12 pulses = **3 real
seconds**, `comm.cpp:829-832`) computes `move_gain(character) / FAST_UPDATE_RATE` each tick, where
`FAST_UPDATE_RATE = SECS_PER_MUD_HOUR·TICS_PER_SECOND / PULSE_FAST_UPDATE = 60·4/12 = 20`
(`structs.h:97`) — i.e. `move_gain()` is a **per-mud-hour** rate, split evenly (with fractional
carry via a `number()` coin-flip on the remainder, `:1496-1500`) across the 20 fast-update ticks that
occur in that hour. Moves are clamped to `[0, GET_MAX_MOVE]` (`:1520-1524`).

### 7.2 `move_gain` formula (`limits.cpp:273`)
**NPC mounts:** `26 + 2·level + CON + DEX + bonus_move_gain` — a flat, level-scaled regen
independent of the general formula below.

**Everyone else** (PCs and non-mount NPCs):
```
gain = 7.0
if bodytype == 2 (animal):          gain += level + 10
gain += (CON + DEX) / 2
gain += PROF_LEVEL(RANGER)/6 + KNOWLEDGE(SKILL_TRAVELLING)/40

# position multiplier
sleeping: gain *= 1.75
resting:  gain *= 1.50
sitting:  gain *= 1.125
(standing/fighting: ×1)

if NPC:
    if tamed (SKILL_TAME affect): gain *= 2, and *2 again if the tamer is Pets-spec
    gain += get_bonus_move_gain(character)
    return gain - 5
else:  # PC
    Wood-Elf/High-Elf:  gain += 5
    Beorning:            gain *= 1.50
    Olog-Hai (unpoisoned): gain *= 1.50
    poisoned:            gain *= 0.25
    marked (SKILL_MARK):  gain *= 0.25
    starving/dehydrated: gain *= 0.25
    gain += get_bonus_move_gain(character)
    return gain
```
`get_bonus_move_gain` (`:251`) adds the character's flat `points.move_regen` field, plus — scaled by
`perception/100` — the net of any `SPELL_CURING` (subtracts), `SPELL_RESTLESSNESS` (adds), or
`SPELL_VITALITY` (adds `duration·6/FAST_UPDATE_RATE`) affects. This is the same
perception-gates-mystic-heal-effectiveness pattern documented in cleric-mystic-system.md.

**Not present in `move_gain`:** unlike the cost side (§4), regen has **no direct sector-type term**
— terrain affects how much a move costs, not how fast moves come back. Position (resting/sleeping)
and race are the only situational multipliers.

### 7.3 Exit width (RotS; `db.cpp:893`, `utility.cpp:164,377`)
An optional per-exit passage width (0 = "use the sector default"). `get_exit_width` falls back to
`default_exit_width[sector_type]` (Field/Water sectors default to 5-6; Crack/Dense-Forest to 3) when
the room file didn't set one explicitly. It has exactly one live consumer: the `block_exit_*`
guard specprocs (§6.3) — a wider exit is harder for a lone guard to physically block. It does **not**
affect `room_move_cost` or mount/rider legality despite the doc-comment "mounts/large mobs" framing
in world-files.md; no code gates mount passage on `exit_width`.

---

## 8. Zone runtime

### 8.1 When a zone resets (`zone.cpp:281` `zone_update`)
`zone_update()` runs every `PULSE_ZONE` = 12 pulses (3 real seconds). It keeps a `static int timer`
that increments once per call; the guard `((++timer * PULSE_ZONE) / 4) >= 60` converts elapsed pulses
to elapsed **real seconds** (`PULSE_ZONE` pulses/call `÷ 4` pulses/sec, per `TICS_PER_SECOND`) and
only advances the internal per-zone `age` (in **minutes**) once that real-time total reaches 60
seconds — i.e. once per real minute, not at a "60-pulse" threshold (that would be 240 accumulated
pulses) (`:299-306`; the source comments flag this "4 passes/sec" assumption as only valid if
`PULSE_ZONE` divides 60, which it does). Each minute, live zones (`reset_mode != 0`) age by 1; the reset decision
per `reset_mode` (cross-ref world-files.md's field description) is:

| `reset_mode` | Fires when |
|--:|---|
| 0 | Never (zone `age` is pinned at `ZO_DEAD`=999, permanently non-live) |
| 1 | Zone is `is_empty` (no connected player currently in it) **and** `age >= lifespan` |
| 2 | `age >= lifespan`, unconditionally (players present or not) |
| 3 | (empty **and** `age >= lifespan`) **or** `age >= 3·lifespan` — a hybrid that eventually forces a reset even with players camping the zone |

`is_empty` (`:922`) checks the **descriptor list** (connected human players only, not NPCs) for
anyone whose `in_room` maps to the zone — a zone full of mobs/objects but no logged-in player still
counts as empty. A zone queued for reset is dequeued **one at a time per pulse** (`:353-374` — the
loop body always `break`s after the first), so many simultaneously-due zones reset in sequence, not
all at once, and each reset sets `age = 0` (`reset_zone`, `:877`) whether it ran automatically or was
forced.

### 8.2 What a reset does
Command-by-command semantics (`M`/`O`/`G`/`P`/`K`/`E`/`D`/`A`/`L`/`if_flag`) are documented in
world-files.md's "Zone reset-command semantics" section and are not repeated here. Two runtime
specifics worth flagging for a rewrite:
- **`D` (door state) is the one command that reaches back into the live movement-runtime data**: it
  calls `set_exit_state` (`db.cpp:1596`) which maps zone-file state `0/1/2` → `exit_info` bits
  `(none)/EX_CLOSED/EX_CLOSED|EX_LOCKED` (preserving all *other* bits, notably `EX_ISDOOR`,
  `EX_ISHIDDEN`, `EX_PICKPROOF`), unconditionally clears `EX_ISBROKEN` (so a bashed-open door is
  repaired by the next zone reset), and mirrors the new state onto the reverse exit if it points back
  — i.e. a zone reset **fixes** any door players broke or fiddled with in that zone.
- **`if_flag` bit 7 (`0x40`, "sun-gated")** ties zone resets to `weather_info.sunlight` (`zone.cpp:
  439-461`) — some `M`/`O` commands only fire while the sun is up or only while it's down, so a
  zone's population composition can shift with real-time day/night (time-and-weather.md §6 already
  notes this from the weather side; this is the implementing code).

---

## 9. Open questions

- **`perform_move_mount`'s `next_rider` chain loop may only ever process one extra rider, not the
  whole chain** (`act_move.cpp:330-336`). The loop's re-init clause reads
  `num2 = ch->mount_data.next_rider_number` where `ch` is the *mount* — but `next_rider`/
  `next_rider_number` are populated on **riders**, chained off `mount->mount_data.rider`
  (`ranger.cpp:194-199`), not on the mount itself. The mount's own `next_rider_number` field is never
  set by `do_ride`, so on the loop's second iteration `num2` likely reads a stale/zero value rather
  than the third rider's `next_rider_number`, which would make `char_exists(num2)` fail and terminate
  the loop after handling only the second rider (primary + 1 extra) even if a third+ rider is chained
  on. Not confirmed against the mount struct's actual default-initialization value (would need to trace
  mob-loading `bzero`/`calloc` behavior), so flagging rather than asserting a live bug — but a rewrite
  should re-derive this loop from scratch (walk `tmpch->mount_data.next_rider`/`next_rider_number`
  directly each iteration) rather than porting it as-is.
- **`ROOM_TUNNEL` (`structs.h:512`) is defined and named in `room_bits[]` (`consts.cpp:1887`) but
  never tested anywhere in the C++ source** (`grep -rn "TUNNEL" src/*.cpp src/*.h` finds only the
  definition and the display-name array). It cannot currently affect movement or anything else at
  runtime; likely a stock-CircleMUD leftover (bottleneck/one-at-a-time-passage semantics in some Diku
  derivatives) that was never wired into RotS. A rewrite should treat it as reserved/inert unless the
  original design intent for it is recovered from elsewhere (release notes, older code).
- **`EX_RSLOCKED` (`structs.h:540`, value 16)** — defined and named (`"LOCKED"`, duplicating
  `EX_LOCKED`'s display name in `exit_bits[]`) but no `IS_SET(...,EX_RSLOCKED)` call was found in the
  movement/door code searched for this doc. Possibly a "remote-switch locked" variant reserved for a
  lever/switch mechanic that never shipped, or dead. Flagging rather than guessing at intended
  behavior.
- **`IS_CLOSED`'s boolean in the tracking BFS** (`graph.cpp:59`) reads as
  `(LOCKED||HIDDEN) && (CLOSED || !ISDOOR)` — the `|| !ISDOOR` clause means a **non-door** edge would
  count as "closed" (and thus block a hunt/track path) if it somehow had `EX_LOCKED`/`EX_ISHIDDEN`
  set despite not being a door, which shouldn't occur in well-formed data but isn't structurally
  prevented. Not observed to cause a problem in the live `.wld` corpus, but worth a defensive
  assertion in a rewrite (only ever test `IS_CLOSED` on `EX_ISDOOR` edges).
- **`room_move_cost`'s "load" formula doesn't match its own comment.** The comment above
  `movement_loss[]` (`consts.cpp:1709`) describes a graduated cost curve ("min. is 3/4 .. 2->1 4->2.5
  6->4 10->7 times smaller"), but the actual code (`act_move.cpp:92-93`) is a plain `max()` of two
  affine terms in carry-weight, with a hard floor of 100 for any non-mount whose raw result is below
  100 (`:101-107`) — in practice this floor dominates for all but very overloaded characters, making
  the graduated-curve framing in the comment mostly theoretical for normal play. Confirmed by reading
  the code as-is; flagged because the comment and implementation diverge and a rewrite should follow
  the code, not the comment.
- **Zone `is_empty` counts only human-controlled descriptors**, so `reset_mode 1`/`3`'s "empty" test
  ignores NPC population entirely (including hostile mobs actively fighting each other) — confirmed
  intentional-looking (it's checking for *players* camping a zone), but worth stating explicitly since
  "empty" is not literally empty.
- **Ferry route data (`ferry_boat_data[]`, `num_of_ferries`/`num_of_captains`) wasn't traced to its
  definition table** in this pass — the mechanism (§6.5) is confirmed from the specproc code, but the
  actual route vnum lists (how many ferries exist, their stops) would need a follow-up read of
  wherever `ferry_boat_data` is populated (likely a small static table or a boot-time load) if a
  rewrite needs exact ferry behavior.

---

## 10. Worked examples

### 10.1 Regular movement — Fallen Trees → Bending River (`lib/world/wld/33.wld`)
Room **#3309** ("Fallen Trees", live data: `0 0 2 0` → flags 0, sector 2 = **Field**) has a `D1`
(east) exit to room **#3318** ("Bending River", `0 4096 6 0` → flags `DRINK_WATER`, sector 6 =
**Water/Swim**), `exit_info 0` — a plain, doorless exit. A Human character (STR 16, no carried
weight, no leg armor, not mounted, not sneaking, not evil) walks east:

1. **`room_move_cost(ch, room_from=#3309 [Field])`**: `base = max(20+0, 70+0) = 70` → floored to
   `100` (non-mount, <100) → `+0` leg encumbrance. `penalty = movement_loss[2] = 4`.
   `cost = 4·(100+r₁)/2 = 200+2r₁`, then `/100` (integer) → **2** if `r₁ < 50`, **3** if `r₁ ≥ 50`.
2. **`room_move_cost(ch, room_to=#3318 [Water/Swim])`**: same `base=100`. `penalty =
   movement_loss[6] = 10`. `cost = 10·(100+r₂)/2 = 500+5r₂`, `/100` → ranges **5** (`r₂=0`) to **9**
   (`r₂=99`).
3. `need_movement = (cost_from + cost_to) / 2` (integer). Illustrative roll `r₁=20 → 2`, `r₂=60 → 8`:
   `need_movement = (2+8)/2 = 5`.
4. Water check: destination sector is `SECT_WATER_SWIM`, **not** `NOSWIM` — the boat/swim branch
   never triggers; no skill or equipment needed to enter a swimmable river.
5. Back in `do_move`, `racial_movement_reduction(room_type = Field [the *origin*], RACE_HUMAN,
   need_move=5)` matches the Human/Field discount (§4.2) → **`5/2 = 2`** final movement points spent.

A Dwarf making the identical move (no Field/Mountain match) would instead pay the un-halved **5**
points for the same step — the racial discount only helps when the *room being left* matches the
race's terrain affinity, so approach direction matters for min-maxing travel cost.

### 10.2 A hidden door — Dark Swamp ↔ Shallow Grave (`lib/world/wld/119.wld`)
Room **#11928** ("Dark Swamp", `0 1 12 0` → flags `DARK`(1), sector 12 = **Swamp**) has a `D5`
(down) exit to room **#11976** ("Shallow Grave", also flags `1`/sector `12`), keyworded
`shallowgrave`, with `exit_info 515`. Decoding `515 = 1(ISDOOR) + 2(CLOSED) + 512(ISHIDDEN)`: it's a
**closed, hidden door with no lock** (`EX_LOCKED` not set) and `key = 0` (irrelevant — nothing needs
unlocking). Room #11976 carries the exact mirror: a `D4` (up) exit back to #11928, same keyword, same
`515` bits — confirming the two-sided-door convention documented in §6.2 is present in the on-disk
data itself, not just applied at runtime.
- A normal `look`/`exits` in #11928 won't reveal the down exit (`EX_ISHIDDEN`) unless the viewer has
  `PRF_HOLYLIGHT`; `down` from a non-holylight character reports "You cannot go that way" rather than
  "closed" (`do_move:691-693`), so the room doesn't even hint that a passage exists.
- Once found/searched, `open down` (or `pull`/discovery elsewhere) clears `EX_CLOSED`; since there's
  no `EX_LOCKED`, no key or `pick` roll is required — only the hidden-ness gated discovery, not
  passage.
- Both rooms being Swamp means an Uruk-Hai or Orc gets the §4.2 racial discount walking either
  direction through this doorway, while every other race pays the full Swamp `movement_loss[12]=12`
  base cost.
