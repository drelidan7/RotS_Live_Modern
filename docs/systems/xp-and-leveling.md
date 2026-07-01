# XP and leveling

**Source files:** kill-XP pipeline `src/fight.cpp` (`die:997`, `exp_with_modifiers:1095`,
`group_gain:1169`, chip-XP inside `damage:1588` at `fight.cpp:1850`); XP application/curve
`src/limits.cpp` (`xp_to_level:84`, `advance_mini_level:86`, `gain_exp:401`,
`gain_exp_regardless:424`, `do_start:864`); level-up effects `src/profs.cpp`
(`advance_level:392`, `advance_level_prof:190`, `check_for_special_levels:372`); immortal
level constants `structs.h:45-54`; immortal title ladder `src/act_info.cpp:3158`
(`get_level_abbr`); retirement `src/utility.cpp:72` (`retire`/`unretire`),
`src/objsave.cpp:1693` (`secs_to_unretire`); scripted XP grants `src/script.cpp:1031`
(`SCRIPT_GAIN_EXP`); mental-attack chip XP `src/clerics.cpp:226` (`do_mental`); admin
level-setting `src/act_wiz.cpp:1486` (`do_advance`).
**Status:** ✅ kill-XP formula, level curve, XP loss, and level-up hooks verified against
source and cross-checked against three real player saves; a few runtime-only quantities
(mob age, difficulty flag, zone coordinates in the general case) are flagged as
example-specific assumptions rather than universal constants.

> **No dead-code overlap.** Unlike combat (`combat_manager.cpp`) and the weather/room-arg
> OB/PB/DB helpers in `char_utils_combat.cpp`, none of the XP/leveling functions have a
> shadow implementation anywhere else in the tree — `grep -rn 'group_gain(\|gain_exp('
> src/` turns up exactly the call sites cited here, all of them live.

## Purpose
This is the system that converts combat (and, in one case, mudlle scripts) into character
progression: how much XP a kill is worth, how it's split across a group, the curve that
maps accumulated XP to character `level`, what happens mechanically at each level-up, and
what happens to XP/levels when a character dies. It sits downstream of combat
(`combat-loop.md`, `fight.cpp`) and upstream of the class/stat system — the per-level HP,
mana, move, and practice-session *formulas* already live in
**[stats-and-character-power.md §4/§6](stats-and-character-power.md)** and are not repeated
here; this doc covers how a character *arrives* at a level (the XP economy) and the
level-up *event* itself (messages, stat rerolls, spec/practice unlocks), not the resulting
stat curves.

## Data structures
- **`char_data.points.exp`** (`GET_EXP`, `structs.h`) — a player's cumulative XP counter.
  For an **NPC**, the same field (set once from the `.mob` file's `<exp>` column,
  `world-files.md`) is a **static kill-value constant**, not a wallet that grows — `die()`
  never lets an NPC gain XP (`gain_exp_regardless` early-returns for `IS_NPC`, `limits.cpp:426`).
- **`char_data.player.level`** (`GET_LEVEL`) — the headline character level. Mortal cap
  **`LEVEL_MAX = 30`**; immortal levels run **91–100** (below). Full mini-level/level
  relationship: **[stats-and-character-power.md §4](stats-and-character-power.md)**.
- **`char_data.specials2.mini_level` / `.max_mini_level`** (`GET_MINI_LEVEL`,
  `GET_MAX_MINI_LEVEL`) — the fine-grained XP-driven counter that actually gates level-ups
  and per-profession level-ups (`stats-and-character-power.md §4`).
- **`char_data.specials.attacked_level`** (`structs.h:1165`) — **NPC-only**: the highest
  `GET_LEVELB` of any attacker who has hit this mob during its current fight, refreshed on
  every hit (`fight.cpp:1739`, `clerics.cpp:332`). It decays by 2 per tick whenever the mob
  isn't actively `POSITION_FIGHTING` (dropping straight to 0 once it's ≤1,
  `limits.cpp:711-716`), and is separately zeroed the instant the mob's `SPELL_ANGER` affect
  wears off (`limits.cpp:1346-1347`). Feeds `group_gain`'s `npc_level_malus` (below) and PK
  exploit-record totals (`pkill.cpp:123`); it is *not* the mob's own level.
- **`char_data.profs.prof_exp[MAX_PROFS+1]`** (`structs.h:1271`) — read/written by
  `save_player`/`load_player` (`db.cpp:1922`, `db.cpp:2465`) but **never read anywhere
  else in the codebase**. It's a serialized, dead data field — see Open questions.
- **`GET_LEVELB(ch)`** (`utils.h:314`) — `IS_NPC(ch) ? GET_LEVEL(ch) :
  min(GET_LEVEL(ch), LEVEL_MAX·⅔ + GET_LEVEL(ch)/3)` (all division integer/truncating).
  This is the "capped level" used directly inside `group_gain` (`fight.cpp:1250,1292`) to
  weight kill-XP shares. It also feeds PK weighting, but only **indirectly**: when a PC
  hits an NPC, `GET_LEVELB(attacker)` is written into the victim's persisted
  `specials.attacked_level` (`fight.cpp:1739`, `clerics.cpp:332`), and it's *that* capped,
  persisted value — not a fresh `GET_LEVELB` call — that `pkill_weight` reads as one input
  to its `total_levels` (`pkill.cpp:123`, `MAX(victim->specials.attacked_level,
  total_levels)`). The other input to that `MAX()`, `total_levels` itself, sums the **raw,
  uncapped** `GET_LEVEL` of every character still actively in `combat_list` fighting the
  victim (`pkill.cpp:117-120`) — so a live post-legend attacker's PK weight is *not* capped
  at 50 while the fight is ongoing; only the fallback `attacked_level` component is.
  For any mortal at or below `LEVEL_MAX` (30) the `GET_LEVELB` cap
  never binds — `20 + level/3 ≥ level` holds everywhere in `0..30`, with equality only at
  30 — so `GET_LEVELB == GET_LEVEL` for every character up through the "legend" cap. It
  **does** bind for the levels **above** 30 that mortals keep earning organically up to 90
  (`stats-and-character-power.md §4`): e.g. a level 60 character is capped-level `min(60,
  20+20)=40`, and a level 90 character is capped-level `min(90, 20+30)=50`. So a
  post-legend character's *raw* level keeps climbing (driving HP via `constHit`/CON, and
  practice sessions) but their **kill-XP-splitting weight caps out at 50** (and their
  *recorded* `attacked_level`, if it becomes the binding PK-weight term, caps the same way)
  — the incentive to keep leveling past 30 is entirely in the mechanics
  `stats-and-character-power.md §4` already covers, not in a bigger group-XP share.
- **`average_mob_life`** (`config.cpp:33`) — global config int, default **40** (in
  `MOB_AGE_TICKS` units — see below), the reference lifetime `exp_with_modifiers` uses to
  scale a mob's XP up/down based on how long it survived.

## Format / Algorithm

### 1. Kill XP — `group_gain` (the live path for every kill, PC or NPC victim)
Called once from `die()` (`fight.cpp:1011`) whenever a `killer` is known and the victim was
either an NPC or a connected player — i.e. it fires for ordinary combat deaths, not for
DT/poison/no-killer deaths (those only run the XP-loss path in §6).

**Step 1 — who's "involved."** Starting from `killer` and whoever `dead_man` was fighting,
`group_gain` walks the death room collecting every character actively fighting the victim,
then expands each of those into their **group** (`char_data::group`), and — for **pets/
orc-friends/guardians standing in the same room as their master** — credits the **master's**
group too (`master_gets_credit`, `fight.cpp:1156`). The result, `player_killers`, is a
dedup'd set of every **PC** entitled to a share; if it's empty (an all-NPC kill), the
function returns with no XP awarded.

**Step 2 — base pool and level total.**
```
share       = GET_EXP(dead_man) / 10                     // 10% of the victim's stored XP value
level_total = Σ GET_LEVELB(p) for p in player_killers
```
If the victim is an **NPC**:
```
spirit_gain     = GET_LEVEL(dead_man) * get_naked_perception(dead_man)     // ×3 if MOB_SHADOW
npc_level_malus = dead_man->specials.attacked_level
level_total    += npc_level_malus
share           = share * (num_killers + 1) / num_killers    // inflate per extra killer
```
If the victim is a **PC** (real player death): `spirit_gain = get_spirits(dead_man) * 100 / 4`
(no `npc_level_malus`, no killer-count inflation). Spirit is a separate resource — see
`cleric-mystic-system.md` / `class-system.md` — `group_gain` also grants it here (split by
each killer's share of the group's total `GET_PERCEPTION`) but that's incidental to XP.

Finally: `share = share / level_total` — the pool is now a **per-capped-level-point** rate.

**Step 3 — per-killer share and diminishing group bonus** (for each PC in `player_killers`,
skipping anyone `>= LEVEL_IMMORT`):
```
capped_level = GET_LEVELB(killer)
group_bonus  = min( share·capped_level/2,
                     (level_total − npc_level_malus − capped_level)·share/4 )
raw_share    = share·capped_level + group_bonus
tmp          = exp_with_modifiers(killer, dead_man, raw_share)
gain_exp(killer, tmp)
```
`group_bonus` is a soft cap on how much a killer benefits from *other* people's levels in
the kill — it's the smaller of "half your own share again" and "a quarter of everyone
else's combined level-weight" — so bringing more/higher-level help raises everyone's total
pool but with falling marginal return per additional killer. The message printed is
literally `"You receive your share of experience -- %d points.\r\n"` (`fight.cpp:1296`).
Every killer who receives XP also runs `wild_fighting_handler::on_unit_killed` (bloodlust
heal — `specializations.md`) and has a **10% chance to be saved** right there
(`number(0,9)==0`, to spread save-file I/O across large groups rather than saving everyone
on every kill).

### 2. `exp_with_modifiers` — per-killer, per-victim scaling (`fight.cpp:1095`)
Takes the raw pooled share from §1 and reshapes it based on *this specific* killer/victim
pairing:
```
if killer is RACE_ORC and victim is a MOB_ORC_FRIEND:  return 0   // orcs get nothing for orc-friends
base_exp /= max(GET_LEVEL(killer) + 1, GET_LEVEL(victim) − 2)      // level-gap normalization
if victim is a PC:  return base_exp                                // PC kills stop here
```
Everything below only applies when the victim is an **NPC**:
- **Deep under-level correction:** if `GET_LEVEL(victim) + 6 < GET_LEVEL(killer)`, replace
  `base_exp` with `6·base_exp / (killer_level − victim_level)` — killing something far below
  your level decays roughly as the inverse of the level gap (stronger decay than the
  `max(...)` division above already applies).
- **Mob-age scaling** (only if `GET_LEVEL(victim) > 5`): `age =
  MOB_AGE_TICKS(victim, now) * 40 / (victim_level + 20)`, where `MOB_AGE_TICKS(ch, tm) =
  (tm − ch->player.time.logon) / SECS_PER_MUD_HOUR` (`utils.h:653`) — real seconds since the
  mob last spawned, converted to mud-hours (**`SECS_PER_MUD_HOUR = 60`**, `structs.h:95`, so
  1 mud-hour = 1 real minute). Then, against `average_mob_life = 40`:
  ```
  if age < average_mob_life:  exp = exp * (average_mob_life·60 + age·40) / (average_mob_life·100)
  else:                       exp = exp * (140 − 40·average_mob_life/age) / 100
  ```
  At `age == average_mob_life` both branches evaluate to `exp * 1.0` (the `else` branch:
  `140 − 40 = 100`, `/100 = 1`). A mob killed **younger** than average is worth *less* than
  its base value (down toward 60% at `age = 0`); one that's survived **longer** than average
  is worth *more*, asymptotically approaching `+40%` as `age → ∞` (the `40·avg/age` term
  shrinks to 0, capping the multiplier at `140/100`). Mobs spawn with a **randomized initial
  age** at boot (`age = number(0, average_mob_life·2)` mud-hours, `db.cpp:1195`) but age
  **0** on a zone reset — so a mob killed right after a zone reset is worth noticeably less
  than one that's been wandering a while.
- **Flag/behavior bonuses** (each independently additive, off `base_exp` not the
  running `exp`): `MOB_AGGRESSIVE` or aggro-to-this-killer → `+base_exp/5`; `MOB_FAST` →
  `+base_exp/10`; `MOB_SWITCHING` → `+base_exp/10`; `MOB_MEMORY` → `+base_exp/20`.
- **Non-standing penalty:** if the mob's `default_pos < POSITION_STANDING`, `−base_exp/20`
  (sitting/sleeping mobs — shopkeepers, etc. — are worth slightly less).
- **Good-on-good penalty:** if both killer and victim are `IS_GOOD` (`alignment ≥ 100`),
  `exp = exp*2/3` — a 33% cut for killing an aligned-good mob as a good character.
- **Scripted/special mob bonus:** `MOB_FLAGGED(victim, MOB_SPEC)` **and** (it has an
  assigned C special-procedure **or** a nonzero mudlle `store_prog_number`) → `+base_exp/10`.
- **Difficulty scalar:** if `GET_DIFFICULTY(victim)` (`specials.prompt_number`, a runtime
  field — not part of the `.mob` file format, see `world-files.md`) is nonzero,
  `exp = exp * difficulty / 100`.
- **"East bonus":** if the killer's race is `RACE_GOOD` (race index 1–9) and the death
  room's zone has `x > 8` (map x-coordinate; `8` is the river), `exp += exp *
  min(zone.x − 8, 5) * 3 / 100` — up to **+15%** for zones 5+ map-columns east of the
  river, flat past that (the bonus caps at `x ≥ 13`).
- **Blanket "TEMPORARY" bonus** (present in the live build, comment says exactly
  `/* TEMPORARY: */` but has apparently been live indefinitely): `exp += 2·exp /
  max(1, GET_LEVEL(killer) − 1)` — an extra bonus that shrinks as the killer's own level
  rises (largest relative effect at low levels: +100% at level 2, ~+7% at level 30).

### 3. Chip XP from raw damage (`damage()`, `fight.cpp:1850`)
Every time `damage()` actually applies HP loss between two *different* characters (melee,
spells, mental attacks — anything routed through the shared `damage()` function), the
attacker gets a small XP trickle **in addition to** any on-kill `group_gain` payout:
```
gain_exp(attacker, (1 + GET_LEVEL(victim)) * min(20 + 2·GET_LEVEL(attacker), dam) / (1 + GET_LEVEL(attacker)))
```
The `min(20 + 2·level, dam)` term caps how much of a single big hit "counts" — so this is a
steady per-swing dribble, not a way to farm XP with one huge nuke; it scales with how many
(capped) points of damage you land and with the victim's level relative to yours. The
near-identical formula reappears in `do_mental` (`clerics.cpp:226`) for the Will-vs-Will
mental-attack power, using `damg*5` (the mental "damage" stat-drain count, scaled ×5) in
place of raw HP damage. Both call sites use `gain_exp` (not `_regardless`), so they're
subject to the same per-call cap and the level-90 gain-off switch described in §5.

### 4. Scripted XP grants (`SCRIPT_GAIN_EXP`, `script.cpp:1031`)
Mudlle quest/trigger scripts can award a flat XP amount directly:
```
if GET_LEVEL(target) > 30:
    exp = exp * 25 / GET_LEVEL(target)
    exp = 6 * exp / (GET_LEVEL(target) - 25)
gain_exp(target, exp)
```
Only applies to PCs (`!IS_NPC` guard). The `> 30` branch is a deliberate diminishing-returns
knock-down for characters past the mortal class-level cap — consistent with the "few things
keep scaling above level 30" theme in `class-system.md`.

### 5. Applying XP — `gain_exp` / `gain_exp_regardless` (`limits.cpp:401-470`)
`gain_exp` is a **sanity-checked wrapper**, used by every combat/script XP source above:
```
if gain > 0 and level < LEVEL_IMMORT - 1 (i.e. < 90):   gain = min(gain, 7000);  gain_exp_regardless(ch, gain)
if gain < 0 and level < LEVEL_IMMORT (i.e. < 91):        gain = max(gain, -10000); gain_exp_regardless(ch, gain)
```
So: **no single `gain_exp` call can grant more than 7000 XP or remove more than 10000**,
and **a level-90 character can no longer gain positive XP through normal play** (the hard
organic ceiling — reaching 91+ requires an immortal `advance`, §8) while still being able to
*lose* XP normally up through level 90. `gain_exp_regardless` skips both checks entirely —
it's used for admin-driven grants (`do_advance`), first-implementor bootstrapping
(`do_start`), and the level-loss path itself (below), all of which need to move XP by more
than the per-call caps allow.

`gain_exp_regardless(ch, gain)` (NPCs no-op immediately — `IS_NPC` early return):
- **Positive gain:** `GET_EXP(ch) += gain`, then a loop advances `mini_level` one step at a
  time for as long as `mini_level² · 3/20 ≤ EXP` (calling `advance_mini_level` — which is
  where actual `level` increments happen, per `stats-and-character-power.md §4`).
- **Negative gain (XP loss, §6):** `GET_EXP(ch) += gain` (gain is negative), then repeatedly
  de-levels while XP has fallen more than a **20,000 XP tolerance band** below the current
  level's threshold.
- **Floor:** `GET_EXP(ch)` is clamped to **0** afterward (never goes negative).
- If anything changed (`is_altered`), `affect_total(ch)` recomputes max HP/mana/move (the
  formulas in `stats-and-character-power.md §6`), and current HP is topped back up by
  however much max HP just grew (`GET_HIT = max(GET_HIT, new_max − old_deficit)`), so a
  level-up (or delevel) never leaves you sitting at a HP value that's now impossible.

### 6. XP loss on death (`die()`, `fight.cpp:1047-1086`)
```
base_xp_gain = -(GET_EXP(dead_man) - 3000) / (GET_LEVEL(dead_man) + 2)
```
This is always ≤ 0 for any character with more than 3000 XP (i.e. above the very start of
level 1) — it's a **loss** expressed as a negative number, shrinking (in magnitude, per
level) as the dying character's own level rises, because it's divided by `level + 2`.
- **No killer** (DT/poison/starvation/etc.): `gain_exp_regardless(dead_man,
  min(0, base_xp_gain / 10))` — only **one tenth** of the "full" loss.
- **Killed by another character:** `gain_exp_regardless(dead_man, min(0, base_xp_gain / 10))`
  is applied **unconditionally** first (same 1/10-scale loss as above, regardless of who or
  what the killer is), and then, **only if the killer is an NPC that is not a
  player's orc-friend or pet**, a **second, much larger** loss is applied:
  `gain_exp_regardless(dead_man, min(0, base_xp_gain))` — the **full** (un-divided-by-10)
  amount. Net effect: dying to a "real" hostile NPC costs roughly **1.1×** `base_xp_gain`;
  dying to anything else (PK, DT, poison, a controlled pet/orc-friend) costs only
  **0.1×** `base_xp_gain`. The `min(0, ...)` guards are defensive — `base_xp_gain` is
  already ≤ 0 whenever `GET_EXP > 3000`, so in practice these calls are always a real loss
  (or a no-op right at the very start of level 1, where `GET_EXP < 3000` would make
  `base_xp_gain` positive and `min(0, ...)` zeroes it out).
- **Poison special-case:** if the fatal attack was `SPELL_POISON` and the victim wasn't
  actively fighting when it killed them, `die()` records an exploit and **returns
  immediately after `raw_kill`**, skipping the pkill/death-exploit bookkeeping below (a
  `TODO` in the source flags this as a known-unclear early-out, not a considered design
  choice — see Open questions).
- The de-level loop itself (inside `gain_exp_regardless`, §5) subtracts
  `PRACS_PER_LEVEL + GET_LEA_BASE(ch)/LEA_PRAC_FACTOR` practice sessions **per level lost**,
  directly from `SPELLS_TO_LEARN`, and resets `mini_level = 100 · new_level` — but it
  **does not touch `prof_level[]`.** Per-profession (Mage/Cleric/Ranger/Warrior) levels are
  only ever *increased* (`advance_level_prof`, §7) or explicitly recomputed by the immortal
  `advance <name> <negative levels>` command (`act_wiz.cpp:1560-1576`); ordinary XP-loss
  de-leveling leaves them exactly where they were. A character can therefore delevel in
  character-level terms while keeping profession levels earned at the higher level —
  see Open questions.
- **`fleeing` also costs a small amount of XP** outside of death: a mob-panic flee that
  *succeeds* (`do_flee`'s NPC/charm path, `act_offe.cpp:391-393`) costs the fleeing PC
  `GET_LEVEL(self) + GET_LEVEL(opponent)` XP via plain `gain_exp(ch, -loose)` — small next
  to a death, but a real (if minor) disincentive to flee successfully.

### 7. `advance_level` — what happens on a level-up (`profs.cpp:392`)
Fires once per character-level gained, from inside the `advance_mini_level` loop
(`limits.cpp:96-100`) the moment `xp_to_level(level+1) ≤ GET_EXP`:
1. `"You feel more powerful!\n\r"` to the character.
2. If already `>= LEVEL_IMMORT`, refill all three hunger/thirst/drunk conditions to "full"
   and reset `player.race = RACE_GOD` (immortals don't eat/drink/get drunk, and are
   race-neutral — cosmetic/administrative, not gameplay-relevant to XP itself).
3. `mudlog`'d at `max(LEVEL_IMMORT, invis_level)` visibility, so only staff normally see the
   "X advanced to level Y" line.
4. **Exploit-log milestones:** level 6, and then every 5th level from level 10 up
   (`level == 6 || (level > 6 && level % 5 == 0)`), plus a one-time `EXPLOIT_BIRTH` record
   at level 1.
5. **The level-6 stat reroll and per-level stat-gain roll** — fully detailed in
   `stats-and-character-power.md §3` (`roll_abilities(ch, 80, 93)` once, gated on
   `max_mini_level < 600`; `check_stat_increase` on every level-up thereafter where new
   ground was broken). Not duplicated here.
6. `check_for_special_levels` prints one extra flavor/unlock line, only at these levels
   (no effect at any other level):

   | Level | Message | Mechanical unlock |
   |---|---|---|
   | 6 | "You are now able to see your statistics and reroll them!" | `stat` command; ties into the stat reroll above |
   | 12 | "You are now able to pick a specialization!" | `specialize` command becomes available (`specializations.md`) |
   | 20 | "You are now able to set your title!" | custom title |
   | 30 | "You are now a legend character!" | cosmetic "legend" status — this is also `LEVEL_MAX`, the class/HP-scaling ceiling (`stats-and-character-power.md §4`) |
7. `update_available_practice_sessions()` recomputes `SPELLS_TO_LEARN` from the current
   level/Learning formula (`stats-and-character-power.md §4`) — this is what makes practice
   totals climb every level, all the way to 90, even after class levels freeze at 30.
8. `save_char(character, NOWHERE, 0)`.

**Per-profession level-ups** (`advance_level_prof`, `profs.cpp:190`) are a *separate* event
that can fire multiple times per character-level (once per profession that crosses its own
threshold that tick — see the dual condition in `advance_mini_level`,
`stats-and-character-power.md §4`). Each grants a one-line flavor message and a small
mechanical perk:

| Profession | Message | Perk |
|---|---|---|
| Mage | "You feel more adept in magic!" | `+2` max mana |
| Cleric | "Your spirit grows stronger!" | (also rescales a Guardian-spec summon's HP if the caller has one active, without healing it to full) |
| Ranger | "You feel more agile!" | — |
| Warrior | "You have become better at combat!" | — |

### 8. Immortal levels and retirement
Levels **91–100** are staff/administrative, not earned through normal play (§5's level-90
gain cutoff blocks organic entry). The ladder (`structs.h:45-54`, titles from
`get_level_abbr`, `act_info.cpp:3158-3196`):

| Level(s) | Constant | Title shown to players |
|---|---|---|
| 91–92 | `LEVEL_IMMORT` (91, aliased `LEVEL_MINIMM`) | "one of the Lesser Maiar (level N)" |
| 93 | `LEVEL_GOD` | "one of the Maiar" |
| 94 | `LEVEL_PERMIMM` (= `LEVEL_FREEZE`, aliases) | "one of the Greater Maiar" |
| 95–96 | `LEVEL_AREAGOD` (95) | "one of the Valar (level N)" |
| 97–99 | `LEVEL_GRGOD` (97) | "one of the Aratar (level N)" |
| 100 | `LEVEL_IMPL` | "an Implementor" |

The ten immortal levels (91–100) partition exactly across this table with no gaps or
overlaps — every immortal level has exactly one title.

Immortal levels are set only by `do_advance` (`act_wiz.cpp:1486`) — an existing immortal
promoting/demoting another character — or by the **first-ever character creation**
bootstrap: `do_start` checks `top_of_p_table == 0` (no other player files exist yet) and, if
so, calls `gain_exp_regardless(ch, xp_to_level(LEVEL_IMPL))` (**15,000,000 XP**), which the
`advance_mini_level` loop then walks all the way from level 0 to level **100** in one shot
(`limits.cpp:920-923`) — this is the mechanical source of the "first character created
becomes a level-100 Implementor" behavior noted in this repo's setup docs. `do_advance` also
hard-caps at level 100 ("100 is the highest possible level.").

**Retirement** (`PLR_RETIRED` flag bit, `structs.h:949`; set/cleared by `retire`/`unretire`,
`utility.cpp:72-87`) is an unrelated, orthogonal admin action — not a level or an XP state.
An immortal (`LEVEL_GOD+1`+) can `retire` a player,
which sets the flag, timestamps `specials2.retiredon`, and logs an exploit; a retired
character can leave the retirement home (room `retirement_home_room = 1151`,
`config.cpp:45`) once `secs_to_unretire` (`objsave.cpp:1693`) allows it, or be `unretire`d by
staff. It doesn't change level, XP, or profession levels — it only gates a handful of
unrelated command behaviors (e.g. `RETIRED(ch)` is one of several conditions selecting the
plain vs. extended stat display in `do_stat`, `act_info.cpp:3903`) and drives cosmetic
`[Retired]` tags on `who`/score output.

## RotS-specific notes
- Stock CircleMUD/DikuMUD grants XP **only on the kill** (a single `gain_exp` call in
  `raw_kill`/`die`, `mob_exp / num_players` style split, no per-hit trickle). RotS layers
  **three independent XP sources** on top of a heavily reworked kill formula: the on-kill
  `group_gain`/`exp_with_modifiers` pipeline (§1–2, itself far more elaborate than stock's
  flat mob-exp/count split — level-gap decay, mob age, aggressive/fast/memory flags,
  good-on-good penalty, zone-based "east bonus," a level-30-aware script bonus), a
  **per-swing/per-cast damage-proportional chip XP** on every `damage()` call (§3, not
  present in stock at all), and **mudlle script-granted XP** (§4) for quest/trigger content.
- The **mini-level** system (a 100-tick-per-level fine counter that also independently
  drives **per-profession** leveling) has no stock analogue — stock CircleMUD levels are a
  single flat counter with no sub-level granularity and no independent multi-class leveling.
- **Level de-sync between character level and profession level after XP loss** is RotS-
  specific behavior (not carried over from a simpler stock death penalty): because de-
  leveling never reduces `prof_level[]`, a character who reaches character level 20 with
  Warrior profession level 18, then loses several character levels to death, keeps Warrior
  18 even though their character level (and hence available HP/mini-level budget) has
  dropped — see §6 and Open questions.
- The **level-90 organic gain ceiling** (`gain_exp`'s `LEVEL_IMMORT - 1` check) is a RotS
  guard rail that has no equivalent need in stock CircleMUD, where the immortal level range
  and the mortal XP table are typically kept far enough apart (or gated by other means) that
  this kind of check isn't needed.

## Worked example
A **level-20 character** (call them Anborn — human, `RACE_GOOD`, no orc blood) and a
**level-20 groupmate** are standing together fighting **"the gate guard"** (keywords `harbor
gate guard man`, vnum **10004**, in `lib/world/mob/100.mob`, zone 100 "Lake-town I"), both
already engaged when the guard drops. From the raw `.mob` data:

```
level=20  OB=80 parry=40 dodge=20  hp_cur=273 hp_max=390  damage=11 energy_regen=110
gold=389  exp=24090  ignored_owner=17
position=8(STANDING) default_position=8(STANDING) sex=1 race=1 pref=456704
mob_action_flags=526347 → bits 0,1,3,11,19 → MOB_SPEC | MOB_SENTINEL | MOB_ISNPC | MOB_MEMORY | MOB_NORECALC
affected_by=0  alignment=0 (neutral, not IS_GOOD)  store_prog_number=2 (nonzero → counts for the MOB_SPEC bonus)
```
Zone 100's header line (`L 24 3 0`) sets `x=24, y=3` — `x > 8`, so the "east bonus" (§2)
applies to `RACE_GOOD` killers, at its cap (`min(24−8,5)=5` → +15%).

**Assumptions made explicit for this example** (all runtime-only, can't be read off disk):
the guard is at **exactly `average_mob_life` (40) mud-hours old** when it dies, so the
age-scaling term in §2 evaluates to exactly `×1.0`; `GET_DIFFICULTY` (a runtime-only field,
not part of the `.mob` file) is its default **0**; both PCs are `RACE_GOOD` humans with
neutral-to-good alignment (so the good-on-good *penalty* does **not** apply, since the
victim's own alignment is 0, not ≥100).

**`group_gain`:**
```
player_killers = {Anborn, groupmate}         (both engaged, same group)
share       = GET_EXP(guard)/10 = 24090/10 = 2409
level_total = GET_LEVELB(Anborn) + GET_LEVELB(groupmate) = 20 + 20 = 40
npc_level_malus = attacked_level = 20   (both attackers have been level 20 all fight)
level_total += 20  → 60
share = share * (2+1)/2 = 2409*3/2 = 3613          (2 killers → ×1.5, integer truncation)
share = share / level_total = 3613/60 = 60
```
Per killer (identical for both, since both are capped-level 20):
```
capped_level = 20
group_bonus  = min(60*20/2, (60-20-20)*60/4) = min(600, 300) = 300
raw_share    = 60*20 + 300 = 1500
```
**`exp_with_modifiers(Anborn, guard, 1500)`:**
```
base_exp = 1500 / max(20+1, 20-2) = 1500/21 = 71
victim is NPC → continue
20+6 < 20? no → no deep-underlevel penalty
age scaling: age == average_mob_life → ×1.0, exp stays 71
AGGRESSIVE/FAST/SWITCHING: none set → +0
MEMORY set → exp += 71/20 = 3            → exp = 74
default_pos(8) < STANDING(8)? no → +0
IS_GOOD(Anborn) && IS_GOOD(guard)? guard alignment=0, not good → skip
MOB_SPEC set and store_prog_number≠0 → exp += 71/10 = 7   → exp = 81
GET_DIFFICULTY == 0 → skip
RACE_GOOD(Anborn) && zone.x(24) > 8 → exp += 81*5*3/100 = 81*15/100 = 12   → exp = 93
TEMPORARY bonus: exp += 2*93/max(1,20-1) = 186/19 = 9      → exp = 102
```
**Result:** each of the two level-20 killers receives **102 XP** from this kill (printed as
`"You receive your share of experience -- 102 points."`), on top of whatever small chip-XP
(§3) they already picked up hit-by-hit during the fight itself.

**Verification against real player saves.** Three sampled `lib/players/` save files (levels
1, 15, and 30) were checked against `xp_to_level` and the `mini_level` recurrence:

| Saved level | Saved `exp` | `xp_to_level(level)` | `xp_to_level(level+1)` | exp in range? | Saved `mini_lvl` | Predicted mini_lvl (largest `n` with `n²·3/20 ≤ exp`) |
|---|---|---|---|---|---|---|
| 1 (`lib/players/A-E/damien.1.11.*`) | 3,000 | 1,500 | 6,000 | ✅ | 141 | 141²·3/20 = 2,982 ≤ 3,000; 142² ·3/20 = 3,024 > 3,000 → **141** ✅ |
| 15 (`lib/players/K-O/kurupt.15.2.*`) | 347,218 | 337,500 | 384,000 | ✅ | 1,521 | 1,521²·3/20 = 347,016 ≤ 347,218; 1,522² ·3/20 = 347,472 > 347,218 → **1,521** ✅ |
| 30 (`lib/players/A-E/achmed.30.18.*`) | 1,360,123 | 1,350,000 | 1,441,500 | ✅ | 3,011 | 3,011²·3/20 = 1,359,918 ≤ 1,360,123; 3,012² ·3/20 = 1,360,822 > 1,360,123 → **3,011** ✅ |

All three saves match `xp_to_level(lvl) = lvl²·1500` and the `mini_level` advance condition
(`gain_exp_regardless`, `limits.cpp:435-436`) **exactly**, with no rounding slack — strong
confirmation that both formulas are unchanged from what's currently live and that no other
hidden term perturbs mini-level growth in practice.

## Open questions
- **Does `prof_exp[]` do anything?** It's saved and loaded (`db.cpp:1922`, `2465`) but no
  code path reads it back for game logic (no XP-per-profession display, no per-profession
  leveling check uses it — that check uses `mini_level · GET_PROF_COOF`, not `prof_exp`).
  Likely a vestigial/legacy field from an earlier per-profession-XP design; flagged here
  rather than guessed at.
- **Is the character-level / profession-level de-sync after death (§6) intentional?**
  Nothing in the source comments or exploit logging suggests it was a deliberate design
  choice vs. simply never having been wired up — de-leveling reduces `SPELLS_TO_LEARN` and
  `mini_level` but leaves `prof_level[]` untouched. Left as an open question rather than
  assumed to be a bug, per repo convention.
- **The `SPELL_POISON` early-out in `die()`** (`fight.cpp:1057-1066`) is explicitly flagged
  in-source with a `TODO(drelidan)` as not-fully-understood by the current maintainer; this
  doc describes its current behavior (skip pkill/death-exploit bookkeeping when the victim
  wasn't actively fighting) without asserting it's correct or final.
- **The "TEMPORARY" bonus** in `exp_with_modifiers` (`fight.cpp:1150`) is labeled temporary
  in a source comment but has no accompanying removal date, ticket, or flag — it appears to
  be permanently live. Left as-is; a rewrite should decide whether to keep it as documented
  behavior or treat it as a still-open cleanup item.
- **How common are post-legend (level 31–90) mortals in practice?** `GET_LEVELB`'s cap
  (see Data structures) only matters for characters who kept earning XP past level 30, and
  no player-save sample was cross-checked specifically at, say, level 60 or 90 to confirm
  the capped-level arithmetic against a real `attacked_level`/PK record — the level 1/15/30
  saves used for the Worked example are all at or below the cap threshold, so this doc's
  live-data verification doesn't exercise the `GET_LEVELB` clamp itself.
- **Zone-`x` "east bonus" data coverage** was checked for exactly one zone (100, Lake-town)
  for this doc's worked example; a full audit of how many live zones actually sit at
  `x > 8` (and therefore participate in this bonus at all) hasn't been done.
