# Combat loop — hit resolution & damage

**Source files (live path):** `src/fight.cpp` — `hit:2362` (the whole swing), the round
driver `perform_violence:2716`, the fighting-list plumbing `combat_list:41` /
`set_fighting:221` / `stop_fighting:257`, plus `damage:1588`, `armor_effect:2144`,
`check_resistances` (`utility.cpp:1792`); hit-location tables `bodyparts[]`
(`consts.cpp:1621`, `structs.h:1728`); OB/PB/DB in `src/utility.cpp` (`get_real_OB:647`,
`get_real_parry:761`, `get_real_dodge:860`).
**Status:** ✅ swing resolution, damage, attack-speed/energy, hit locations, armor
reduction, resistances/vulnerabilities, and the special-attack damage funnel are all
documented below (one non-blocking item remains, see Open questions).

> ⚠️ **Which combat code is live.** There is a `combat_manager` class
> (`combat_manager.cpp`: `roll_ob`, `offense_if_weapon_hits`, `calculate_hit_damage`, …) and a
> matching OB/PB/DB set in `char_utils_combat.cpp`. **None of it is called** — `combat_manager`
> is compiled but never instantiated. The real combat is `fight.cpp::hit()`. The two are nearly
> identical in structure (this doc originally referenced the `combat_manager` versions); the
> formulas below now follow the **live `fight.cpp`/`utility.cpp`** code, which differs in one
> material way: the strength term sits *inside* the damage random factor (see §3).

> **OB, PB, and DB (offensive / parry / dodge bonuses) are defined in
> [`stats-and-character-power.md` §10](stats-and-character-power.md#10-offensive-ob-parry-pb--dodge-bonuses)** —
> they are stat-derived and documented there. This doc covers how those numbers are *rolled
> and compared* to produce hits and damage.

## Purpose
How a single weapon swing is resolved: roll the attacker's OB, subtract the defender's dodge,
evasion, and parry, and if anything gets through, compute and apply damage. Attacks are paced
by an **energy** budget that refills at the attacker's energy-regen rate (attack speed, §6 of
the stats doc); each swing runs the sequence below.

## One swing, end to end

### 1. Roll the attacker's OB (`fight.cpp:2407-2414`)
```
roll       = d35 (1..35)
OB_roll    = get_real_OB + rand(1 .. 55 + OB/4) + roll
OB_roll    = OB_roll·7/8 − 40
if roll == 35 (a natural 35): critical → OB_roll += 100
```
The `·7/8 − 40` is why each point of raw OB is worth ~0.875 of effective margin.

### 2. Compare against the defender (`fight.cpp:2424-2483`)
Working value starts at the rolled OB, then:
1. **Dodge + evasion:** subtract `get_real_dodge(victim)` (DB) plus the evasion malus (only vs
   `AFF_EVASION`, scales with cleric levels). If the result is `< 0` **and** the roll wasn't a
   natural 35, the swing **misses** — split into an *evade* or a *dodge* message by an
   evasion-vs-dodge roll.
2. **Frenzy** (`is_frenzy_active`, `fight.cpp:2449`): if the attacker is in a frenzy, the roll
   is forced to 35 — i.e. **treated as a critical**, which (per `roll_ob`) can't be dragged
   below 0 by dodge/parry. This is the live game's "guaranteed-hit" mechanism (there is *no*
   accuracy/"accurate hit" system in the live code — that exists only in the unused
   `combat_manager`).
3. **Position bonus:** if the victim is below `POSITION_FIGHTING`, `+10` per position step
   (helpless targets are easier).
4. **Parry:** subtract `get_real_parry(victim)` (PB) × the victim's current-parry % (which
   then decays to ⅔ for subsequent swings this round). A natural 35 is floored at 0 here. If
   OB < 0 → **deflected (miss)**, which can trigger a **riposte** (`check_riposte`,
   dex/ranger/stealth-based) and grip checks on two-handers.

Whatever OB **remains** after these subtractions is `remaining_OB`, passed to damage. (Tactics
shift OB/PB/DB themselves — full table in
[stats §10](stats-and-character-power.md#10-offensive-ob-parry-pb--dodge-bonuses).)

### 3. Damage (`fight.cpp:2509-2516`)
```
weapon_damage = get_weapon_damage(weapon)        # barehanded = natural_attack_dam; mobs ×0.5
base          = weapon_damage + points.damage·10
F (random factor) = 10000 + d100² + (twohanded ? 266 : 133)·bal_str    # bal_str is INSIDE F

damage = base · (remaining_OB + 100) · F / 13,300,000
```
Two things drive a hit's size: **`remaining_OB + 100`** (beating the defense by more does more
damage — the channel through which OB, and the stats feeding it, raise damage) and **`F`**,
which folds in the `d100²` roll *and* the attacker's **strength** (`133·bal_str`, doubled for
two-handers). So unlike the unused `combat_manager` — where the strength term was added
*outside* the product and was negligible — **STR contributes meaningfully here** (≈ +0.8 %/pt
direct, on top of its OB channel; stats §10). Then:
- **Find weakness** (`check_find_weakness:2051`, warrior-level × `EXTRA_DAMAGE` skill): ×1.5.
- **Rush** (Wild-fighting spec only; chance 5/10/15 % by Normal/Aggressive/Berserk tactics):
  adds +½ the hit's damage. See `specializations.md`.
- **Armor reduction** (`armor_effect`, `fight.cpp:2529`) is applied per hit location **here, in
  `hit()`** — *before* the call to `damage()`. Several specs alter this step — Heavy Fighting
  +10 % absorb, Defender shield block, Weapon Master armor/shield bypass (`specializations.md`).
  Full algorithm, the hit-location table it depends on, and how each entry point differs are in
  **"Hit locations," "Armor: how absorption reduces damage,"** and **"Special-attack damage
  paths"** below.

> ⚠️ **Melee's `armor_effect` only runs for ordinary weapon swings.** It happens in `hit()`
> (above), not in `damage()`. **Active skills (kick, bash, bite, …) and *all spells* call
> `damage()` directly, so they skip `armor_effect` entirely** — but **archery is a partial
> exception**: it has its *own*, simpler armor calculation (`apply_armor_to_arrow_damage`,
> `ranger.cpp:2016`) that only reduces damage against chain/metal armor. See "Special-attack
> damage paths" below for the full comparison. `damage()` itself (`fight.cpp:1588`) always
> applies **resistances/vulnerabilities** (`check_resistances`: ×⅔ resisted / ×3⁄2 vulnerable),
> Beorning/maul reduction, and the PK-fame bonus — regardless of entry point.

### Damage tiers (the message the room sees) — `get_damage_message_number:1406`, `dam_weapons:1367`
The **final** (post-armor) damage is bucketed into the verb you read on screen. `#w` is the
weapon's own verb (slash, pierce, crush, …):

| Final damage | Message tier |
|-------------:|--------------|
| 0 | **miss** ("$n misses $N") |
| 1 | scratch |
| 2–3 | barely \<verb\>s |
| 4–6 | lightly \<verb\>s |
| 7–11 | \<verb\>s (a plain hit) |
| 12–17 | \<verb\>s **hard** |
| 18–24 | \<verb\>s **very hard** |
| 25–33 | \<verb\>s **extremely hard** |
| 34–60 | **deeply wounds** |
| 61–90 | **severely wounds!** |
| 91+ | **MUTILATES** … with $s deadly \<weapon\>!! |

So the on-screen severity is just "how big was this one hit" — a MUTILATE is simply a swing
that pushed past 90 damage after armor, often a crit and/or a found weakness stacking up.

### Plain English: weapon damage and the shape of the formula

**What "97/10" means.** `get_weapon_damage` (the **live** one is `utility.cpp:426`; an unused
`double` twin sits at `object_utils.cpp:208`, reached only by the dead `combat_manager.cpp`) returns
the weapon's damage rating **already multiplied by 10**, and `identify` prints it as `<value>/10`. So
`97/10` is an *average raw weapon damage of 9.7*, `111/10` is 11.1. Inside the formula
(`base = weapon_damage + points.damage·10`) the engine uses the un-divided number (97, 111),
so the `/10` is just a "move the decimal" readout for players. Typical weapons land around
~90–130 (i.e. 9–13).

**Where that number comes from (and why two weapons differ).** A weapon's damage rating isn't
free — it's computed as a **trade-off** against the weapon's other properties
(`dam_coef`, `utility.cpp:503`):
```
damage ∝ (40 + item_level − parry_coef) · (50 − OB_coef) · (20 − |bulk − 3|) / energy_regen
```
In words: a weapon's damage **goes up** with item level and with being bulky/slow, and **goes
down** the more OB or parry the weapon also grants, and the further its bulk is from ~3. So a
high-damage weapon has usually *paid for it* with lower OB, lower parry, or slower swings.
**Weapon *type* also nudges this**: the live formula adds a per-type tweak to `parry_coef`/`OB_coef`
(`utility.cpp:469`) — whip `+8 parry / −5 OB`, slashing `−2 parry`, bludgeoning `+3 parry` — a small
**damage-only** adjustment (it does not change your real OB/parry). The separate effects of type on
your actual OB, parry and speed are documented in [weapons.md](weapons.md).

**Is 91/10 vs 97/10 meaningful?** Yes, but modestly. Those 6 points add directly to `base`, so
the weapon's damage contribution rises by roughly **+4 % to +7 %** per hit (the exact percent
shrinks as your own `points.damage` grows, since that dilutes the weapon's share). It's a real
edge — but a 91/10 weapon with noticeably higher OB, parry, or attack speed can easily out-
perform a 97/10 one overall, because OB *multiplies* damage (below) and speed adds whole extra
swings. Compare the full stat line, not just the damage number.

**The shape of the damage algorithm.**
```
damage = (weapon_damage + points.damage·10) · (remaining_OB + 100) · F / 13,300,000
F = 10000 + d100² + (twohanded ? 266 : 133)·bal_str
```
`F` carries the `d100²` roll (averaging ~3,333) **and** the strength term, so
`F ≈ 13,333 + 133·bal_str` (1H). Ignoring STR, the constants nearly cancel and the
**average hit simplifies to** `≈ base · (remaining_OB + 100) / 1000` (STR pushes that up ~20 %
at STR 20). From this you can read off the relationships:
- **Weapon damage is linear.** Double the weapon's rating → double the damage (all else equal).
  `points.damage` adds linearly too (each point = +10 to `base`).
- **OB is linear with a +100 floor, so it has diminishing *percentage* returns.** Damage scales
  with `(remaining_OB + 100)`, not `remaining_OB`. A hit that *barely* wins (margin ≈ 0) still
  deals ~`base/10`; each extra point of margin adds a *flat* amount but a *shrinking fraction*
  (`1/(margin+100)` — about +1 % at margin 0, ~+0.5 % at margin 100). It is **not** exponential
  and **not** logarithmic — it's linear-with-offset.
- **Weapon damage and OB multiply each other.** They're complementary, not interchangeable: a
  big weapon gains more absolute damage from extra OB, and a high-OB fighter gets more out of a
  big weapon. There's no point stacking one to the exclusion of the other.
- **Strength rides inside `F`.** Each STR point adds `133` (1H) / `266` (2H) to `F` ≈ +0.8 %/pt
  per hit, *on top of* STR's OB-channel contribution (stats §10). A DEX-based Light Fighter
  routes DEX into OB instead (`specializations.md`).

## Three example swings (plain English)
One attacker throughout: a level-30 warrior whose **standing OB is 200** (after gear/skills),
wielding a sword (`base damage = 250` = weapon 200 + `points.damage`·10) on **Normal tactics**.
Numbers are illustrative — absolute damage depends on weapon/gear scaling — but every step
follows the real formulas above. The dice each swing are the `d35` to-hit roll and the
`d100` damage roll.

### A) Critical hit → MUTILATE
The attacker rolls a **natural 35** on the d35 to-hit roll — a critical.
- *Roll the OB:* `(200 + 70 random + 35)·7/8 − 40 = 266 − 40 = 226`, then **+100 for the crit
  → 326**.
- *Defender (mob: DB 40, PB 60):* a crit can't be turned into a miss, so the dodge/parry
  subtractions just shave the margin: `326 − 40 − 60 = 226` **remaining OB**. (It also "finds
  a weakness" this swing, ×1.5.)
- *Damage:* `d100 = 80` → random factor `80² + 10000 = 16400`.
  `250 · (226+100) · 16400 / 13,300,000 ≈ 100`, then ×1.5 ≈ **150 damage**.
- On screen: **"You MUTILATE the orc's chest with your deadly sword!!"** A MUTILATE isn't a
  special move — it's just a normal swing whose margin (big remaining OB from the crit) and a
  found weakness pushed the one hit past 90.

### B) Regular hit → "slash hard"
A middling to-hit roll, no crit.
- *Roll the OB:* `d35 = 16`; `(200 + 45 random + 16)·7/8 − 40 = 229 − 40 = 189`.
- *Defender:* `189 − 40 dodge − 60 parry = 89` **remaining OB** (still positive → it lands;
  the parry weakens the victim's next parry to ⅔).
- *Damage:* `d100 = 35` → random factor `35² + 10000 = 11225`.
  `250 · (89+100) · 11225 / 13,300,000 ≈ 40` → after the orc's armor, say **~15**.
- On screen: **"You slash the orc hard."** A solid, ordinary blow — landed because OB cleared
  both defenses, but by a modest margin, so the damage sits in the low/mid tiers.

### C) A miss (dodged)
A poor to-hit roll against a very nimble foe (a ranger: **DB 150**, PB 90).
- *Roll the OB:* `d35 = 3`, low random; `(200 + 10 random + 3)·7/8 − 40 = 186 − 40 = 146`.
- *Defender:* subtract dodge — `146 − 150 = −4`. It's **below 0 and not a crit → the attack is
  dodged.** The swing stops here: no parry check, no damage roll.
- On screen: **"The ranger dodges your attack."** Had the OB instead cleared dodge but gone
  below 0 at the **parry** step, you'd read **"deflects your attack"** — and a skilled
  defender might answer with a **riposte**.

### The single-stat lever
In example A, one more point of **STR** would have raised the standing OB by 1 → ~+0.9 to the
226 remaining OB → `(327/326) ≈ +0.3 %` on that already-huge hit; in example B the same point
is ~`+0.5 %` (smaller margin), **and** in example C it could be exactly what flips a −1 result
to a 0 and turns a dodge into a glancing hit. That's why STR's value is "more damage when you
land *and* more landings" — see `combat-stat-examples.md`.

## Hit locations — where a swing lands

Every melee swing (and, separately, every arrow — see "Special-attack damage paths") rolls a
**body part**, and each body part maps to an **armor slot**. Both live in one table,
`bodyparts[MAX_BODYTYPES]` (`consts.cpp:1621`, type `race_bodypart_data` at `structs.h:1728`),
indexed by the victim's `GET_BODYTYPE` (`ch->player.bodytype`, `utils.h:346`). `MAX_BODYTYPES =
16`, `MAX_BODYPARTS = 11` (`structs.h:72-73`). Each row has:
- `parts[11]` — the body-part name shown in messages ("head", "left leg", …).
- `percent[11]` — the % chance that part is hit (must sum to ≤ 100).
- `bodyparts` — how many of the 11 slots this body type actually uses (0 = "no locations at
  all," see below).
- `armor_location[11]` — which **equipment slot** (a `WEAR_*` constant, `structs.h:670-698`)
  armor for that body part lives in.

### Rolling the location (`hit()`, `fight.cpp:2490-2499`)
```
location = 0
if bodyparts[victim.bodytype].bodyparts != 0:
    roll = number(1, 100)
    while roll > 0 and location < MAX_BODYPARTS:
        roll -= percent[location]
        location += 1          # increment happens even on the iteration that zeroes roll
if location > 0:
    location -= 1              # undo that last increment
tmp = armor_location[location]      # the WEAR_* slot to check for armor
```
So it's a straightforward **weighted die roll down the `percent[]` list** — no aiming, no skill
influence. The increment-then-correct shape matters: the loop body always advances `location`
*before* re-testing, even on the pass that drives `roll` to ≤0, so the final `-1` is undoing that
extra step, not applying a second one. (An earlier draft of this doc modelled it as "loop with an
explicit `break`, then always subtract 1," which double-corrects and lands one slot short — e.g.
for bodytype 1 a roll of 50 should hit `body` at index 2, not `head` at index 1. The pseudocode
above matches the live `for` loop's actual post-increment semantics, confirmed against the
independent archery implementation below, which uses the equivalent `while (...) percent[hit_location++]`
form.) If `percent[]` doesn't sum to 100, the last part absorbs the remainder (rolls that exceed
the running total simply land on the last iterated slot). If `bodyparts == 0` for this body type,
`location` never advances past `0`, and `armor_location[0]` is always `0` (`WEAR_LIGHT` — i.e.
**no armor slot at all**, so `armor_effect` finds nothing to reduce). Archery uses the identical
roll independently (`get_hit_location`, `ranger.cpp:1997-2014`).

### The live table (`consts.cpp:1621-1699`)
| Bodytype | Who uses it | Parts (`percent`%) | Armor slot per part |
|---:|---|---|---|
| 0 | Default/blank (fresh `char_data`, memset zero; some mobs whose `.mob` file omits `bodytype`) | none (`bodyparts=0`) | n/a — **no hit locations, no armor ever absorbs** |
| **1** | **Player characters (non-Beorning)**, most humanlike mobs | head 15, body 35, L/R leg 8/8, L/R foot 2/2, L/R arm 10/10, L/R hand 5/5 | head→`WEAR_HEAD`, body→`WEAR_BODY`, legs→`WEAR_LEGS`, feet→`WEAR_FEET`, arms→`WEAR_ARMS`, hands→`WEAR_HANDS` |
| 2 | Quadrupeds (hind/foreleg mobs, e.g. horses, wolves) | head 15, body 37, hindlegs 7/7, hindfeet 2/2, forelegs 11/11, forefeet 4/4 | **all 0 — quadrupeds have no armor slots** |
| 3 | Many-legged / limbless-armor mobs | head 10, body 10, then 8 identical "leg" slots at 10 each | head→`WEAR_HEAD`, two of the leg slots→`WEAR_FEET`, rest→0 |
| 4 | Winged mobs (birds, small dragons) | head 20, body 30, L/R leg 8/8, L/R wing 15/15, L/R claw 2/2 | **all 0 — no armor slots** |
| **15** | **Beorning PCs** (claws instead of hands) | head 15, body 35, L/R **hindleg** 8/8, L/R **hindfoot** 2/2, L/R **foreleg** 10/10, L/R **claw** 5/5 — *same 11 percentages as bodytype 1* | identical `armor_location[]` to bodytype 1 (head→`WEAR_HEAD`, body→`WEAR_BODY`, "hindleg"→`WEAR_LEGS`, "hindfoot"→`WEAR_FEET`, "foreleg"→`WEAR_ARMS`, "claw"→`WEAR_HANDS`) |
| 5–14 | Unused (all-blank rows, "can easily be added" per source comment) | — | — |

(Percentages verified directly from the live table; e.g. bodytype 1's `body` slot at 35% plus
`head` at 15% account for half of all hits landing center-mass or the head.)

> ⚠️ **Bodytype 15's `parts[]` names are quadruped-styled, not "identical to bodytype 1."** The
> live `bodyparts[15]` row (`consts.cpp:1694-1698`) reuses bodytype 1's exact `percent[]` and
> `armor_location[]` arrays — so a Beorning's hit distribution and which `WEAR_*` slot absorbs
> which hit are numerically identical to any other PC — but its *display strings* are copy-pasted
> from the quadruped layout: `"left hindleg"/"right hindleg"/"left hindfoot"/"right hindfoot"/
> "left foreleg"/"right foreleg"/"left claw"/"right claw"` instead of bodytype 1's `"left leg"/
> "right leg"/"left foot"/"right foot"/"left arm"/"right arm"/"left hand"/"right hand"`. In
> practice this means combat messages describe a Beorning's own limbs with quadruped terminology
> (e.g. "$n's hindfoot") even though the character is bipedal and uses ordinary `WEAR_LEGS`/
> `WEAR_FEET`/`WEAR_ARMS`/`WEAR_HANDS` gear slots underneath. This looks like a copy/paste
> artifact in the source data table, not an intentional design choice — flagged here rather than
> "fixed," since it's live data, not something this doc should silently paper over.

### How a character gets a bodytype
- **PCs:** set once at character creation, `do_start` (`limits.cpp:872-876`):
  `GET_BODYTYPE(ch) = 15` for `RACE_BEORNING`, else `1` for everyone else — persisted to the
  player file thereafter (`bodytype` field, `db.cpp:1817/2008/2376`) and never recomputed from
  race again. Every other playable race (Human, Elf, Dwarf, Orc, Uruk, Haradrim, Woodman, …) is
  bodytype 1.
- **Mobs:** read directly from the `.mob` file's stat block, the `<bodytype>` field
  (`../data-formats/world-files.md`, `db.cpp:1365`). This is **hand-authored per mob**, not derived
  from `race`. Verified against live data: `lib/world/mob/268.mob` (`mewlip guard`, vnum 26800)
  has stat-block tail `... 0 44 98 1 ...` → `prof=0 mana=44 move=98 bodytype=1` — a bipedal
  humanoid correctly tagged bodytype 1. A `.mob` file that simply omits/zeroes this field leaves
  the mob at **bodytype 0 — meaning that mob's own armor never reduces damage against it**,
  regardless of what it's wearing. This is a real, data-dependent gotcha (not a bug): any mob
  authored without an explicit bodytype falls into this "armor inert" bucket.
- **Shapeshifting** (Olog-hai / ranger forms, `shapemob.cpp`) can swap `player.bodytype` at
  runtime to match the new form's hit-location table.

## Armor: how absorption reduces damage

Armor absorption is **not** part of `damage()` — it is a step inside `hit()` (`fight.cpp:2528-
2529`) that runs *before* the melee formula's result is handed to `damage()`, using the hit
location rolled above:
```
tmp = bodyparts[victim.bodytype].armor_location[location]
dam = armor_effect(ch, victim, dam, tmp, w_type)
```

### `armor_effect` (`fight.cpp:2144-2219`)
```
if location is out of range: return 0                      # no reduction, no crash
armor = victim.equipment[location]
if armor exists:
    damage_reduction = armor.value[1]                       # "min absorb" — flat floor
    divisor = 100
    if w_type == TYPE_SPEARS:
        divisor += 50                                       # spears punch partway through armor
        if weapon-master spear proc fires: divisor *= 2      # (spec bonus, doubles the punch-through)
    damage_reduction += ((dam − damage_reduction)·armor_absorb(armor) + 50) / divisor
    if weapon-master "ignores_armor" proc fires (daggers):    # bypass proc
        damage_reduction = armor.value[1]                     # only the flat floor applies
    if victim is Heavy Fighting spec:
        damage_reduction += damage_reduction / 10             # +10% absorb for the defender
    dam -= damage_reduction
    if w_type == TYPE_SMITE and armor.material == 4 (rigid metal):   # "bone-crunch"
        20% chance: dam += damage_reduction * 2                # armor recoil hurts MORE than bare skin
return dam
```
Plain English:
- **No armor in that slot → no reduction at all** (not even 0 — the whole block is skipped).
- The weapon's damage is split into a **flat floor** (`value[1]`, "min absorb" — always
  subtracted) plus a **percentage cut** of the *remainder* above that floor
  (`armor_absorb(armor)%`), so heavier hits still get a meaningful percentage taken off, not just
  a fixed chip.
- **Spears are designed to defeat armor**: their divisor (150, or 300 with a Weapon-Master proc)
  shrinks the percentage term to ⅔ (or ⅓) of normal — spears "hit the armor, but then go right
  through it," per the source comment.
- **Weapon Master** can proc a **dagger bypass** (only the flat floor applies — the % term is
  discarded) or a **spear double-punch** (halves the effective absorb again).
- **Heavy Fighting** (the *victim's* spec) adds a flat **+10 %** to whatever was absorbed —
  independent of, and stacking with, the attacker's own procs.
- **Smiting weapons vs. rigid (`material == 4`) armor** ("werewolf skull helm"-style rigid
  plate) have a **20 % chance** to turn the armor against its wearer — the flat+percent reduction
  that was subtracted is added back **twice**, i.e. the armor itself becomes a damage source.
  (The check is `if (number() >= 0.80)`, and `number()` with no args returns a uniform double in
  `[0.0, 1.0)` — `utility.cpp:928-933` — so the *condition* covers the top 20 % of the range, a
  20 % chance, not 80 %; easy to misread the literal `0.80` as "80 % chance," but it's a threshold
  the roll must clear from above, not a probability.) This does *not* trigger against chain
  (`material == 3`) or softer materials (`object_materials[]`, `consts.cpp:1726`:
  cloth/leather/chain/metal/wood/…).

### `armor_absorb` — the underlying % (`utility.cpp:540-563`)
```
if armor.value[0] == -1: return 0             # "not real armor" flag (decorative/robe pieces)
weight_coef = 3 if BODY-slot, 2 if ARMS or LEGS, else 1     # (weight_coof, utility.cpp:527-537)
encumb_points = value[2]·6 + weight / weight_coef / 20
points = level + encumb_points
if encumb_points < 30:
    points += encumb_points·(60 − encumb_points) / 90       # bell-curve bonus, peaks ~+15 at encumb=30
if value[2] != 0:
    points += 3                                              # flat kicker for any encumbering piece
absorb = points − value[1]·9                                 # min-absorb TRADES OFF against % absorb
absorb = max(absorb, 0)
if absorb > 50:
    absorb = 100 − 2500 / absorb                              # asymptotic curve, approaches but never hits 100%
return absorb
```
Reading the shape: **item level and weight both raise the %**, but weight is discounted for
torso/arm/leg pieces (`weight_coef` 2–3) versus everything else (helms, boots, gloves — coef 1),
so a heavy piece "costs" less encumbrance on the body than the same weight on the head or hands.
`value[2]` ("encumberance," the value the game shows builders, `act_info.cpp:4320-4324`) is a
small integer knob, not the item's weight — most live armor uses `0` or `1` (see data check
below). **`value[1]` ("min absorb") is a genuine trade-off, not a pure bonus**: every point adds
1 flat damage-reduction point in `armor_effect` but *removes 9 points* of the % absorb here — a
"chip damage" piece (high min-absorb) is a different design than a "big hits only" piece (high %,
low min-absorb).

### Field semantics, verified against live data
`value[0..3]` for `ITEM_ARMOR` (type 9) are labelled for builders as
`"", "Min Absorbtion", "Encumberance", "Dodge"` (`act_info.cpp:4319-4323`); `value[3]` is a
straight DB modifier added/removed on wear (`SET_DODGE(character) += item.value[3]`,
`handler.cpp:1330-1331` / `-=` on remove, `handler.cpp:1409-1410`) — independent of
`armor_effect`. Parsing all 903 `ITEM_ARMOR` records in `lib/world/obj/*.obj` confirms this
matches real data:
- `value[0] == -1` items exist and are common for robe/cloth-style "armor" (e.g. vnum `6300`,
  `6305`, `2060`, `26260`, all `value = [-1, 0, 0, 1, 0]`; vnum `10703` is the same pattern with a
  0 DB instead, `value = [-1, 0, 0, 0, 0]`) — these compute `armor_absorb() == 0` regardless of
  the other fields, i.e. **they occupy an armor slot but absorb nothing**, matching the
  "decorative/robe" reading of the flag.
- A concrete worked example, vnum `27000` — "a werewolf skull helm" (`lib/world/obj/270.obj`):
  `type=ITEM_ARMOR(9)`, `wear=17` (`WEAR_HEAD | ITEM_TAKE`), `value=[31, 1, 1, -3, 0]`,
  `weight=65`, `level=20`. Head slot → `weight_coef=1` → `encumb_points = 1·6 + 65/1/20 = 9`;
  `points = 20 + 9 = 29`; since `9 < 30`, bonus `= 9·(60−9)/90 = 5` → `points = 34`; `value[2]=1`
  nonzero → `+3` → `37`; `absorb = 37 − 1·9 = 28` (≤ 50, no asymptotic clamp). So this level-20
  helm gives **~28 % absorb, 1 flat point, and −3 DB** — internally consistent with the formula
  and with its `A 18 3` apply line (a separate stat bonus, unrelated to absorb).
- Scanning heavy `ITEM_ARMOR` body pieces (`wear & WEAR_BODY`) across the same data set produces
  a realistic spread: ordinary plate/mail bodies land **~55–60 %** absorb (e.g. vnums `6227`,
  `6220`, `6243` at level 27–30), and the single highest-weight (80,000) body piece in the data
  set hits **99 %** — confirming the `100 − 2500/absorb` curve does approach, but never reach,
  100 % even for extreme items.

### The `score` "armor %" readout is a simplified estimate, not the combat formula
`get_percent_absorb` (`act_info.cpp:1577-1599`, surfaced as "Your armour absorbs about N% damage"
in `score`, `act_info.cpp:1715-1717`) walks **every** hit location for the player's own bodytype,
computes each slot's absorb the same way (`value[1]` + `armor_absorb()%`, with the same Heavy
Fighting +10 % bonus), and averages the results **weighted by `percent[]`** — i.e. "if you got hit
in every location proportionally to how often it happens, how much would armor take off a 10-point
hit, on average." It intentionally **omits** the spear/smite/weapon-master modifiers from
`armor_effect` (those are per-swing procs, not steady-state armor), so treat the `score` number as
a directional average, not a per-hit guarantee.

## Resistances and vulnerabilities

`check_resistances(victim, attacktype)` (`utility.cpp:1792-1811`) is **not** an elemental
race-based lookup table — it tests a **per-character bitvector** (`specials.resistance` /
`specials.vulnerability`, `sh_int` fields, `structs.h:1095-1096`), one bit per **specialization /
attack-group id** (`PLRSPEC_*`, `structs.h:836-855`: `NONE, FIRE, COLD, REGN, PROT, PETS, STLH,
WILD, TELE, ILLU, LGHT, GRDN, HFGT, LFGT, DFND, ARCH, DARK, ARCANE, WMSR, BTLEMS`), via
`IS_RESISTANT`/`IS_VULNERABLE` (`bitvector & (1 << group)`, `utils.h:687-691`).
```
if attacktype < MAX_SKILLS(256):
    group = skills[attacktype].skill_spec         # the attack's own spec group (e.g. a fire spell → PLRSPEC_FIRE)
    if IS_RESISTANT(victim, group):  return  1
    if IS_VULNERABLE(victim, group): return -1
if attacktype in [TYPE_HIT..TYPE_CRUSH] or attacktype == SKILL_ARCHERY:
    if IS_RESISTANT(victim, PLRSPEC_WILD):  return  1     # plain weapon damage / arrows -> the "physical" bucket
    if IS_VULNERABLE(victim, PLRSPEC_WILD): return -1
return 0
```
So a **skill or spell's own `skill_spec`** column (set per-skill in the `skills[]` table,
`consts.cpp` skill list) decides which resistance bit it checks — e.g. a fire-type spell checks
`PLRSPEC_FIRE`. **Plain weapon swings and arrows** aren't individually tagged in that table, so
they're explicitly routed to the generic **`PLRSPEC_WILD`** ("physical") bucket instead — the
`resistance_name[]` display array literally labels it `"WILD-F"` with the comment "also
resistance to hit-crush" (`consts.cpp:2365-2390`).

### How the result is applied (`damage`, `fight.cpp:1743-1761`)
```
tmp = check_resistances(victim, attacktype)
if number(0,2) == 0 and IS_PHYSICAL(attacktype):   # IS_PHYSICAL = TYPE_HIT..TYPE_CRUSH only (fight.cpp:37-38)
    tmp = 0                                         # 1-in-3 chance to void a resist/vuln — plain weapon swings ONLY
if tmp > 0: dam = dam * 2 / 3        # "You resist a lot." / "$n resists a lot."
if tmp < 0: dam = dam * 3 / 2        # "You feel it a lot."
```
The **⅓-chance override that discards the resist/vulnerability result** only fires for
`IS_PHYSICAL` attack types (the same `TYPE_HIT..TYPE_CRUSH` range used above) — **archery, active
skills, and spells always apply their resist/vuln result**; only bare weapon swings have a chance
to ignore it. This scaling happens in `damage()` for every entry point (see the funnel below), so
it stacks with — and runs independently of — armor (which only exists on the melee path) and the
Beorning/maul and PK-fame adjustments immediately above it in the same function
(`fight.cpp:1626-1640`).

### Where the bits actually get set
Resistance/vulnerability is **data- and affect-driven, not computed from race**:
- **Mobs** read `resistance`/`vulnerability` straight out of the `.mob` file's stat block
  (`db.cpp:1391-1392`; see `../data-formats/world-files.md`'s `<resistance> <vulnerability>` fields)
  — a flat, hand-authored bitmask per mob, independent of its `race` field.
- **Items and spell affects** flip the bits via `affect_modify`'s `APPLY_RESIST` / `APPLY_VULN`
  cases (`handler.cpp:476-487`): applying an affect with `location = APPLY_RESIST` and
  `modifier = <PLRSPEC id>` sets that bit; removing it (or un-equipping the item) clears it.
  Any item `A` apply line, or any spell affect, using those constants grants the effect — this is
  how gear can grant elemental resistance.
- **Concrete spell example:** the mystic "Protection" spell (`mystic.cpp:1761-1824`) lets a
  caster choose fire / cold / lightning / "physical," applying `APPLY_RESIST` with
  `modifier = PLRSPEC_FIRE / PLRSPEC_COLD / PLRSPEC_LGHT / PLRSPEC_WILD` respectively — i.e. the
  in-game "resist fire" effect is exactly this bit being set for the spell's duration.
- **No race formula.** There is no `get_race_resistance()`-style function; if a race consistently
  resists something, it's because its mobs were hand-authored with the bit set, not because of a
  race → resistance rule in code.

## Special-attack damage paths — the shared `damage()` funnel

Every damage-dealing action in the game — melee swings, arrows, active skills (kick, bash, rend,
bite, maul, …), and every damage spell — ultimately calls the same function,
**`damage(attacker, victim, dam, attacktype, hit_location)`** (`fight.cpp:1588`). What differs is
everything *upstream* of that call: how much damage is computed, whether armor is checked, and
what `attacktype`/`hit_location` are passed in.

| Path | Entry point | To-hit / accuracy | Damage formula | Armor? | `attacktype` passed | `hit_location` passed |
|---|---|---|---|---|---|---|
| **Melee weapon swing** | `hit()` (`fight.cpp:2362`) | OB vs. dodge/parry, §1–2 above | §3 above (`base·(OB+100)·F/13.3M`) | **Yes** — full `armor_effect` (this doc) | `w_type` (`TYPE_HIT..TYPE_CRUSH`/`TYPE_SPEARS`/`TYPE_SMITE`) | rolled body part (this doc) |
| **Archery** | `on_arrow_hit` → `shoot_calculate_damage` (`ranger.cpp:2085-2118, 2392-2407`) | separate hit/miss + `check_archery_accuracy` roll (`ranger.cpp:1975-1987`) — see `ranger-skills.md` | ranger-level/DEX/bow/arrow formula — see `ranger-skills.md` | **Partial** — its own `apply_armor_to_arrow_damage` (`ranger.cpp:2016-2054`): only reduces damage if the armor's `material` is chain (3) or metal (4); chain is halved-effective vs. arrows; a successful accuracy roll **bypasses armor entirely**; no spear/smite/weapon-master interactions | `SKILL_ARCHERY` | independently-rolled body part (`get_hit_location`, same `bodyparts[]` table) |
| **Active skills** (kick/bash/rend/bite/maul/olog-hai skills, …) | `act_offe.cpp`, `olog_hai.cpp` call `damage()` directly | skill-specific success roll — see `warrior-skills.md` | skill-specific — see `warrior-skills.md` | **No** — `armor_effect` is never called on this path | the skill's own `SKILL_*` id (e.g. `SKILL_KICK=12`, `SKILL_BASH=13`, `SKILL_REND=55`, `SKILL_BITE=113`) | `0` (no location roll — always the "no part" slot) |
| **Spells** | `mage.cpp`'s `apply_spell_damage` wrapper (`mage.cpp:98-112`) → `damage()`; `mystic.cpp` calls `damage()` directly (2 sites: `SPELL_HALLUCINATE`, `SPELL_POISON`) | spell hit is typically unconditional once cast succeeds; damage is scaled by the **victim's saving throw** (`get_victim_saving_throw`, `mage.cpp:80-96`): `×20/(20+save)` if positive, `×(2 − 20/(20−save))` if negative — see `magic-system.md` | spell-specific — see `magic-system.md` | **No** — spells never call `armor_effect` | the `SPELL_*` id | `0` |

> **`clerics.cpp` currently has zero `damage()` call sites.** Despite `PROF_CLERIC` being a real,
> compiled profession, the live cleric spell set (`clerics.cpp`, 580 lines) is support/utility
> only (mind-block, concentrate, …) — there is no cleric damage spell in the current code to
> route through this funnel. If that changes, it would presumably follow the same
> "call `damage()` directly" pattern as `mystic.cpp`.

What every path shares, inside `damage()` itself (`fight.cpp:1588` onward), regardless of which
row above got it there:
1. Hallucination check (`IS_PHYSICAL` types only, `fight.cpp:1626-1627`).
2. Beorning "maul" damage reduction (`maul_damage_reduction`, `fight.cpp:1632-1634`) — **the
   source comment above this call claims "physical weapons only... spell damage is still at its
   full amount,"** but `maul_damage_reduction(char_data* ch, int damage)` takes no `attacktype`
   parameter at all and the call site's only guard is `GET_RACE(victim) == RACE_BEORNING && dam >
   1` — there is no `IS_PHYSICAL` check anywhere on this path. As written, the 10%+ reduction
   applies to **all** damage a Beorning takes, spells included; the comment appears stale relative
   to the code. Flagged here rather than silently "corrected," since it may be an intentional gap
   nobody has closed rather than a deliberate design choice.
3. PK-fame bonus for outranked opponents (`fight.cpp:1636-1640`).
4. Aggro/engagement bookkeeping (`set_fighting`, group/follower breaks).
5. **Resistances/vulnerabilities** (`check_resistances`, previous section) — applies to *every*
   row in the table above, including skills and spells.
6. A hard **200-damage overflow clamp** (`fight.cpp:1759-1762`), **except for `SKILL_AMBUSH`**
   (`if (dam > 200 && attacktype != SKILL_AMBUSH)`) — so it is the near-universal damage cap in
   the live game (applied after everything above, to every other path), not a literally
   exception-free one; an ambush hit can exceed 200.
7. HP subtraction, death/extraction.

**Messages also diverge by path** (`generate_damage_message`, `fight.cpp:1467-1547`):
`IS_PHYSICAL` types use the generic weapon-tier table (`dam_weapons`/`get_damage_message_number`,
this doc's "Damage tiers" section); `SKILL_ARCHERY` reuses that same tier table but rewrites the
verb to "shoot/shot" (`fight.cpp:1485-1509`); everything else (skills and spells) looks up a
**dedicated message set** from `fight_messages[]`, loaded at boot from `lib/misc/messages`
(`db.cpp:61,164,375`) and keyed by the exact `attacktype`/`SKILL_*`/`SPELL_*` number
(`fight.cpp:1511-1546`). This is verified against the live data file: `lib/misc/messages` opens
with `M / 12` (kick), `M / 13` (bash), `M / 18` (swing), `M / 55` (rend), `M / 113` (bite) blocks
— matching `SKILL_KICK=12`, `SKILL_BASH=13`, `SKILL_SWING=18`, `SKILL_REND=55`, `SKILL_BITE=113`
(`spells.h:49-159`) exactly, each with its own self/hit/miss/die message variants.

For the actual to-hit and damage *formulas* of each specific skill/spell, see
**`ranger-skills.md`** (archery), **`warrior-skills.md`** (kick/bash/rend/maul and other active
skills), and **`magic-system.md`** (spell damage and saving throws) — this section only maps
which pipeline each path uses and what it skips.

## The combat list — who is fighting, and in what order

Before the energy loop (below) can give anyone a swing, the engine needs a roster of who is in
combat. That roster is a single **world-global singly-linked list**, `combat_list`
(`fight.cpp:41`), threaded through each character's own `next_fighting` pointer
(`char_data.next_fighting`, `structs.h:1706`). There is **one list for the entire game** — every
fighting PC and mob in every room is on the same chain; fights are *not* bucketed per-room. A
second global, `combat_next_dude` (`fight.cpp:42`), is the "next dude trick" that keeps the walk
safe when nodes are removed mid-iteration (below).

### Adding an entry (`set_fighting`, `fight.cpp:221`)
When a character starts fighting, `set_fighting`:
1. **Early-outs if already fighting** — if `ch->specials.fighting` is already set it just bumps
   the position to `POSITION_FIGHTING` and returns. So `set_fighting` is "*start* fighting," not
   "switch target"; it does not re-add or re-order an already-engaged character.
2. **De-dups** — linearly scans `combat_list` for `ch`; if found, skips insertion.
3. **Prepends to the head** otherwise — `ch->next_fighting = combat_list; combat_list = ch;`.

The insert is therefore **LIFO**: the *most recently engaged* combatant becomes the new head and
will be the **first** one the round driver visits. Entries are never sorted, shuffled, or ordered
by initiative/speed/level — list position is purely a function of *when you joined the fight*
(newest first), as later mutated by removals and target hand-offs.

### Removing an entry (`stop_fighting`, `fight.cpp:257`)
Removal is deliberately reluctant, because leaving combat usually means "find a new foe," not
"drop off the list":
1. It scans the list for **another character who is attacking `ch`** (one `ch` can see and isn't
   already its direct opponent). If found, `ch` **stays on the list** and simply **switches
   target** to that attacker ("You turn to face your next enemy").
2. If none is found but `ch` is alive and still has combat state (energy below the swing
   threshold, or a mental delay), `ch->specials.fighting` is cleared **but `ch` is left on the
   list** ("Do not remove from the list yet") to be cleaned up on a later pass.
3. Only when `ch` is truly done is it **unlinked**. If `ch` happens to be the saved
   `combat_next_dude`, that global is advanced to `ch->next_fighting` *first* so the in-progress
   walk doesn't follow a dangling pointer. `stop_fighting_him` (`fight.cpp:325`) is the bulk
   version — it walks the list and stops everyone who was targeting a now-gone character.

### Is the list walked in order? (`perform_violence`, `fight.cpp:2716`)
Yes. Every pulse, `perform_violence` walks `combat_list` from the **head to the tail** following
`next_fighting`, touching each fighter exactly once (`fight.cpp:2723`). The only subtlety is the
`combat_next_dude` save (`fight.cpp:2729`): the *next* node is captured **before** the current
fighter acts, so if that fighter — or its victim — dies, flees, or changes rooms and gets
unlinked during its own `hit()`, the loop still resumes from the right place. The traversal order
is otherwise a plain, fixed linked-list order.

### Does walking in order make combat deterministic each round?
**The *order of resolution* is deterministic; the *outcome* is not.** Keep the two apart:

- **Sequencing is deterministic and reproducible for a given list state.** If `A` sits ahead of
  `B` in the list, `A`'s swing fully resolves before `B`'s this pulse — there is no randomization
  of turn order. This has real consequences: in a mutual-kill race the fighter **nearer the head
  acts first** and can drop the other before it ever swings; flees, quaffs, and room-wide effects
  all resolve in list order. Because it's one global list, this fixed priority even spans
  *unrelated* fights.
- **But the order is not a stable "initiative."** New combatants **prepend** (a fighter who joins
  or re-engages jumps to the front and acts first next pulse), and removals / target hand-offs
  reshuffle who is present. So the relative order of two fighters is stable only while neither
  leaves and re-enters the list.
- **And every action's result is fully stochastic.** Each swing's to-hit (`d35`), damage
  (`d100²`), and *all* procs — find-weakness, rush, the ~20 % light-fighting double strike,
  Beorning swipe, weapon-master procs — call the RNG (`number()`). Two rounds replayed from the
  identical list state produce different damage.
- **A fighter doesn't even necessarily act each pass.** Whether it swings depends on its
  `ENERGY` crossing `ENE_TO_HIT` (1200) and on wait-state/position gating (energy loop below), so
  fighters fire on their *own* cadence, not once per pulse.

So "combat happens in a deterministic fashion each round" is true **only** in the narrow sense
that, for a fixed snapshot of the list, who-acts-before-whom is fixed (head-first, newest-engaged
first). It is **not** deterministic in damage or who-wins, and the ordering itself drifts as the
global list is mutated.

## Attack speed — the energy loop (`profs.cpp:766-805`, `fight.cpp:2750`)

> **Live path confirmed.** The whole chain runs in the real game:
> `game_loop` (`comm.cpp:471`) → heartbeat → `perform_violence()` every `PULSE_VIOLENCE`
> (`comm.cpp:822`) → the energy loop below (`fight.cpp:2750`) → `get_energy_regen`
> (`char_utils.cpp:1359`) → `points.ENE_regen`, which is set by `recalc_abilities`
> (`profs.cpp:716`, called on equip/affect changes `handler.cpp:563`, level/start
> `limits.cpp:899`, stat rolls `profs.cpp:710`). None of this touches the unused `combat_manager`.

### How energy becomes swings
Each combat tick, a fighter in `POSITION_FIGHTING`+ who isn't waiting gains energy
(`fight.cpp:2750`):
```
ENERGY += get_energy_regen(fighter)        # = points.ENE_regen × wild-fighting rage multiplier
when ENERGY > ENE_TO_HIT (1200):  hit() fires and deducts 1200
```
So **`ENE_regen` is your attack speed**, and `perform_violence` runs **every pulse (¼ second)**
(`comm.cpp:822`; its `mini_tics` arg is ignored). `get_energy_regen` (`char_utils.cpp:1359`)
multiplies the stored value by the Wild-Fighting rage bonus (specializations.md); the stored
`points.ENE_regen` is computed by `recalc_abilities` whenever stats/gear change, and external
haste/slow affects add to it directly (e.g. ±40, `handler.cpp:994`, `ranger.cpp:1624`).

### Wall-clock: the `score` "Speed" number = hits per minute
Energy ticks in 4×/second, so over a minute you gain `240 · ENE_regen` energy and spend `1200`
per swing:
```
hits/min = 240 · ENE_regen / 1200 = ENE_regen / 5
```
That's exactly the **"Speed" value the `score` command prints** (`get_energy_regen(ch) / 5`,
`act_info.cpp:1865`) — a 1–2 digit number you can read directly as **swings per minute**. So:
```
seconds per swing = 60 / Speed = 300 / ENE_regen
```
| `ENE_regen` | `score` Speed (hits/min) | seconds/swing |
|------------:|-------------------------:|--------------:|
| 60  | 12 | 5.0 s |
| 100 | 20 | 3.0 s |
| 120 | 24 | 2.5 s |
| 150 | 30 | 2.0 s |
| 180 | 36 | 1.7 s |
| 240 | 48 | 1.25 s |

So a `score` Speed of **30 means ~one swing every 2 seconds**; doubling your Speed halves your
time-per-swing and doubles damage output. `PULSE_VIOLENCE = 12` pulses = **a 3-second "combat
round,"** so Speed/20 = swings per round (Speed 20 ≈ 1 swing/round). Note the base loop fires at
most one swing per pulse, so Speed effectively caps near 48 (4/sec) — but Light Fighting's free
double strike (~20 %) and Wild-Fighting's rage bonus add swings *on top* of the displayed Speed,
so real output can exceed it.

### How `ENE_regen` is computed (with a weapon)
```
null_speed = 3·DEX + 2·(fast_attack + stealth/2)/3 + 100      # "handling" speed
str_speed  = bal_str · 2,500,000 / (weight · (bulk + 3))      # "heave the weapon" speed
str_speed ×= 2   if two-handed
if bulk < 4:                                                  # light / one-handed only
    dex_speed = DEX · 2,500,000 / (weight · (bulk + 3))
    str_speed = max(str_speed, str_speed·bulk/5 + dex_speed·(5−bulk)/5)
combined  = 1,000,000 / (1,000,000/str_speed + 1,000,000/null_speed²)   # harmonic blend
ENE_regen = 10 · √(combined / 100)                           # do_squareroot(x)=200·√x, ÷20
```
Then: Dwarf+axe `+min(regen/10,10)`, Haradrim+spear `+min(regen/20,20)`, Weapon-Master
piercing/whipping `×1.15` (specializations.md). **Barehanded** uses a flat
`ENE_regen = 60 + 5·DEX`.

### Plain English
Your swing speed comes from blending two numbers:
- **`str_speed` — can you physically heave this weapon?** It's your **Strength** divided by the
  weapon's **weight × bulk**. A heavy, bulky weapon makes that denominator large, so you need a
  lot of Strength to swing it quickly — **this is the dominant lever for heavy/two-handed
  weapons** (and two-handers double `str_speed`, since you put both hands into it).
- **`null_speed` — your baseline handling speed.** It's `100` + **3·DEX** + your **fast-attack**
  and **stealth** skills. This is the floor that doesn't depend on the weapon's weight.

The two are merged with a **harmonic blend**, so the **slower of the two limits you** — being
strong doesn't help if your handling is poor, and vice-versa. Note `null_speed` is **squared**
in the blend, which makes the handling term (DEX + fast-attack + stealth) count for a lot once
it's your bottleneck. The whole thing is square-rooted, so each input has **diminishing
returns** — pushing one number ever higher yields progressively less.

> **What "harmonic blend" means.** A normal (arithmetic) average adds two numbers and halves
> them, so a big value can offset a small one — average of 10 and 1000 is 505. A **harmonic**
> combination instead adds their *reciprocals*: `combined = 1 / (1/A + 1/B)`. That result is
> always **smaller than either input and pulled toward the smaller one** — for 10 and 1000 it's
> ≈ 9.9, barely above the small value. It's the same math as **two pipes filling a tub** (total
> flow is limited mostly by the narrower pipe) or **resistors in parallel**. The practical
> upshot for attack speed: your two speed components (`str_speed` and `null_speed²`) can't cover
> for each other — whichever is your **bottleneck sets your pace**, and improving the component
> that's *already* good barely moves your speed. To get faster, raise your *weakest* side: more
> STR if a heavy weapon is dragging `str_speed` down, or more DEX/fast-attack/stealth if poor
> handling (`null_speed`) is the limit.
>
> *Worked example:* with `str_speed = 55,000` and `null_speed² = 43,000`,
> `combined = 1/(1/55,000 + 1/43,000) ≈ 24,100` — below both, nearer the smaller (43,000). Push
> `null_speed²` up to 500,000 and `combined` only rises to ≈ 49,500: the now-much-larger handling
> term gave almost nothing, because `str_speed` (55,000) had become the bottleneck.

- **Strength → heavy weapons.** For bulk-≥3 / two-handed weapons, `str_speed` is the bottleneck,
  so STR is what keeps a big weapon swinging at a usable rate.
- **Dexterity → light weapons.** DEX always feeds `null_speed` (3/pt, any weapon). On top of
  that, for **bulk < 4** weapons a `dex_speed` term lets DEX *substitute for Strength* on the
  heave, weighted `(5−bulk)/5` — **80 % at bulk 1, 60 % at bulk 2, 40 % at bulk 3** — and the
  game takes the better of pure-STR or the DEX-blend. So a nimble fighter can swing a light
  weapon fast on DEX with little STR. For **bulk ≥ 4 (heavy/two-handed) this blend is off** —
  DEX then only helps via `null_speed` (there is *no* DEX cap; the substitution is simply
  disabled — see stats §6).
- **Fast attack & stealth.** Both raise `null_speed` via `2·(fast_attack + stealth/2)/3` — so
  **fast-attack counts at full weight and stealth at half**. Yes, stealth speeds you up, but
  only half as much per point as fast attack. Because `null_speed` is squared in the blend,
  investing here is most valuable when handling (not strength) is your limiting factor — i.e.
  for light/fast builds and dexterous fighters.

> **Heavy weapons blunt fast-attack (a harmonic-blend consequence).** Because `combined` can
> never exceed the smaller input, **`str_speed` is a hard ceiling**: `ENE_regen` tops out at
> ≈ `10·√(str_speed/100)` no matter how much fast-attack / DEX / stealth you pile on. A heavy
> weapon makes `str_speed` small, so that ceiling is low and the handling stats saturate against
> it fast. Isolating fast-attack 0→90 (DEX 14): a **light 1H** (`str_speed ≈ 55,000`) gains
> ≈ +26 % attack speed — `ENE_regen` ~122→153, i.e. **score Speed ~24→31 swings/min**, ceiling
> ~234 (Speed ~47) — but a **very heavy 2H** (`str_speed ≈ 12,000`) gains only ≈ +11 %
> — `ENE_regen` ~87→96, **Speed ~17→19**, ceiling ~110 (Speed ~22). So fast-attack is ~2–3× more valuable on light
> weapons; on a heavy weapon it's already near its low ceiling, and **Strength** (which raises
> both the bottleneck *and* the ceiling) is what actually speeds you up. The same diminishment
> applies to DEX's and stealth's speed contributions on heavy weapons.

(Separate multi-swing effects stack on top of raw speed: Light Fighting's ~20 % free double
strike and Wild-Fighting's rage attack-speed bonus — both in `specializations.md`.)

## Mobs
Mobs use the **NPC branches** of `get_real_OB`/`get_real_parry`/`get_real_dodge`
(`utility.cpp`): OB/parry/dodge are the stored `points.*` plus flat level/stat terms, and their
weapon damage is halved (`fight.cpp:2501`). They cannot riposte. Mob `ENE_regen` is read
straight from the `.mob` file (`../data-formats/world-files.md`), not computed from stats.

## Future / proposed changes (design notes — NOT yet implemented)

> ⚠️ Everything in this section is **prospective design intent**, not current behavior. It is
> recorded here so the rationale and the risks are captured before any code is written. The live
> loop today is exactly as documented above: `perform_violence` runs once per pulse, energy gates
> swings at `ENE_TO_HIT = 1200`, and `hit()` runs `damage()` straight through to death mid-walk.

### 1. Decouple the combat loop from the violence tick (free-running energy)
**Intent.** Stop pacing combat at the fixed pulse rate and instead let it advance **as fast as it
can**, so that energy accrues on a finer-grained (ideally real-time) basis and **marginal
`ENE_regen` gains actually buy more swings.**

**Why today's model wastes those gains.** In `perform_violence` (`fight.cpp:2716`) each fighter
(a) only *regens* energy while `ENERGY <= ENE_TO_HIT` — regen **pauses** once it's over the
threshold — and (b) fires **at most one `hit()` per pulse** (it's an `if`, not a `while`). At 4
pulses/sec that hard-caps raw attack speed at **~4 swings/sec (score Speed ~48)**; any `ENE_regen`
beyond ~1200/pulse is throttled away. So two builds with very different speed can converge to the
same real output, and small speed upgrades near the cap do nothing (only Light Fighting's double
strike and Wild-Fighting rage currently exceed the cap — see §attack-speed).

**What would change.** Drive energy accrual by **elapsed wall-clock time** (the loop already
computes `time_delta` via `gettimeofday` for `damage_details.tick`) — e.g. `ENERGY += ENE_regen ·
time_delta`, remove the regen pause and the one-swing-per-pass cap (loop `while (ENERGY >
ENE_TO_HIT)`), and run the resolver at a high frequency or on demand. The `score` "Speed" readout
becomes a true, uncapped swings/minute.

**Potential side-effects / risks.**
- **Damage economy must be re-tuned.** Removing the ~4/sec ceiling makes high-`ENE_regen` builds
  (DEX / fast-attack / light-weapon / hasted) scale **linearly with no cap** — a large, direct
  DPS buff. Mob HP, PvP time-to-kill, and the `ENE_regen` formula were all balanced around the
  current cap; lifting it without re-tuning would make fast builds dominant.
- **Frame-rate-dependent combat (correctness hazard).** "As fast as it can" makes swing rate a
  function of **server load** unless accrual is strictly time-scaled. Flat per-call regen on a
  variable-frequency loop = combat that literally speeds up when the server is idle and slows
  under load. Time-scaling (`· time_delta`) is mandatory, not optional.
- **Pulse-denominated timers drift.** Wait-states, mental-delay decrement, skill recovery
  (`WAIT_STATE_FULL` in pulses), Olog skill cooldowns, and **parry restoration (+3/pulse,
  `fight.cpp:2723`)** are all measured in pulses tied to `PULSE_VIOLENCE = 12` (the 3 s "round").
  Decoupling swings from pulses desyncs these from actual swing cadence; each needs re-derivation
  in seconds.
- **CPU cost.** A tight free-running resolver walks the **global** `combat_list` far more often;
  with many simultaneous fights this can peg a core. A higher fixed frequency + time-scaled
  accrual is cheaper than a true busy loop.
- **Output spam / bandwidth.** More swings/sec = proportionally more combat messages per client
  (and to the room). Faster builds could flood scrollback and consume bandwidth; message
  batching/throttling may be needed.
- **Energy-as-resource skills shift.** `ENERGY` is also a skill currency — Weapon-Master
  bludgeon **drains** `10× damage` energy as a stagger, slashing **refunds** `ENE_TO_HIT/2`.
  Faster regen weakens energy-drain CC and changes the value of the refund.

### 2. Phase combat into hit → damage → resolution (a deferred "dying list")
**Intent.** Split each round into explicit phases so that two fighters who **both** have the
energy to swing can land on each other **simultaneously**, each able to kill the other —
**postponing death** until everyone has acted. Today, because `hit()` runs `damage()` straight
through to extraction inside the walk, the fighter **nearer the head of `combat_list` resolves
first and can drop its opponent before that opponent ever swings** (see "The combat list" above).
Phasing removes that ordering bias for lethal exchanges.

**Proposed phases (per round):**
1. **Hit phase** — for every fighter with `ENERGY > ENE_TO_HIT`, roll to-hit / compute the swing
   (hit-or-miss, `remaining_OB`), spending energy. No HP is touched yet.
2. **Damage phase** — apply each computed swing's damage to its target. A target whose HP crosses
   the lethal threshold is **added to a new `dying_list`** (mirroring `combat_list`) and **marked
   dying — but NOT extracted.** It can still take further hits this phase.
3. **Resolution phase** — *after* all fighting is resolved, walk the `dying_list` once and
   actually kill/extract each entry (corpse, loot, XP, death triggers, `stop_fighting_him`).

**Relationship to the determinism discussion.** This directly targets the "*who survives a
mutual-kill race is decided by arbitrary list position*" problem: outcome for simultaneous lethal
blows becomes **order-independent** (both die). Per-swing results stay stochastic (RNG to-hit /
damage / procs) — phasing fixes *sequencing fairness*, not randomness.

**Potential side-effects / risks.**
- **Kill attribution becomes ambiguous.** XP, PK fame/ranking, quest credit, and on-kill procs
  assume one clear killer at the moment of death. If A and B kill each other, who gets credit?
  Both? This must be decided per system.
- **On-kill effects that change the killer's own survival.** Wild-Fighting **bloodlust** heals
  `10 %` of missing HP on a kill (`wild_fighting_handler::on_unit_killed`). If a dying killer's
  bloodlust fires in the resolution phase, does the heal **pull them back off the `dying_list`**?
  That is either an intended skill-expression mechanic or an exploit — it needs an explicit rule,
  and resolution-phase ordering (heals before deaths?) starts to matter.
- **"Wasted" overkill / already-dead targets.** A target marked dying in the damage phase can
  still be struck by other attackers later in the same phase. Those hits hit a corpse-to-be —
  decide whether they're ignored, counted for credit, or redirected.
- **Invariant breakage.** Vast amounts of code assume a struck/dead character is *immediately*
  removed (`hit()` re-checks victim alive/same-room/awake each swing; `stop_fighting`'s reluctant
  hand-off; the `combat_next_dude` trick). A character that is "dying but still present" for a
  whole phase violates those assumptions; targeting, rescue, flee, and area effects all need
  audit. (Upside: deferring removal actually *simplifies* the mid-walk-deletion hazard the
  `combat_next_dude` trick exists to handle — for deaths specifically.)
- **Retaliation / new engagements mid-round.** Being hit currently triggers aggro / auto-engage
  via `set_fighting`, which **prepends** the newcomer to `combat_list`. Phasing must define
  whether a fighter engaged *during* the hit/damage phase acts this round or next, or ordering
  artifacts return through the back door.
- **Transient cross-phase state & crash safety.** A half-applied round (damage dealt, deaths not
  yet processed) must never be observed by a save/checkpoint. Keep all three phases inside one
  synchronous pass — no yielding between them.
- **Player feel.** Simultaneous "trades" (both die) change PvP texture and the on-screen
  kill/death message ordering; this is a deliberate design choice, not just a mechanical one.

### How the two changes interact
They are in tension. Change 2 needs a **well-defined round boundary** to batch hit → damage →
resolution, while Change 1 wants to **dissolve** the fixed tick. The likely reconciliation is to
keep discrete, phased rounds but **raise their frequency** and make energy/timers **time-scaled**,
rather than a truly free-running loop — i.e. apply Change 1's time-based accrual *to* Change 2's
phased round, not instead of it.

## Open questions
- `points.damage`/`points.OB` **base values** for players — where the un-modified numbers that
  feed the OB roll (§1) and the damage formula (§3) originate before tactics/spec/affect
  modifiers are layered on (`set_player_ob`, `limits.cpp:972` and callers at `:1068-1108`, which
  is tactics-stance-driven — full stance table is stats-and-character-power.md territory, but the
  *base* pre-stance value's source wasn't traced as part of this pass).
