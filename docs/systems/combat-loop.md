# Combat loop — hit resolution & damage

**Source files (live path):** `src/fight.cpp` — `hit:2362` (the whole swing), the round
driver `:2755-2761`, `damage`, `armor_effect`; OB/PB/DB in `src/utility.cpp`
(`get_real_OB:647`, `get_real_parry:761`, `get_real_dodge:860`).
**Status:** 🟡 swing resolution, damage, and attack-speed/energy documented. Armor reduction
details and special attacks are partial (see Open questions).

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
- **Armor reduction** (`apply_armor_reduction:467`) is applied per hit location before the
  final `apply_damage`. Several specs alter this step — Heavy Fighting +10 % absorb, Defender
  shield block, Weapon Master armor/shield bypass (`specializations.md`).

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

**What "97/10" means.** `get_weapon_damage` (`object_utils.cpp:208`) returns the weapon's
damage rating **already multiplied by 10**, and `identify` prints it as `<value>/10`. So
`97/10` is an *average raw weapon damage of 9.7*, `111/10` is 11.1. Inside the formula
(`base = weapon_damage + points.damage·10`) the engine uses the un-divided number (97, 111),
so the `/10` is just a "move the decimal" readout for players. Typical weapons land around
~90–130 (i.e. 9–13).

**Where that number comes from (and why two weapons differ).** A weapon's damage rating isn't
free — it's computed as a **trade-off** against the weapon's other properties
(`damage_coef`, `object_utils.cpp:246`):
```
damage ∝ (40 + item_level − parry_coef) · (50 − OB_coef) · (20 − |bulk − 3|) / energy_regen
```
In words: a weapon's damage **goes up** with item level and with being bulky/slow, and **goes
down** the more OB or parry the weapon also grants, and the further its bulk is from ~3. So a
high-damage weapon has usually *paid for it* with lower OB, lower parry, or slower swings.

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
So **`ENE_regen` is your attack speed**: you swing roughly once every `1200 / ENE_regen` ticks.
A typical `ENE_regen` is ~100–160, i.e. a swing every ~8–12 ticks; double your `ENE_regen` and
you swing ~twice as often (more swings multiply *all* your damage). `get_energy_regen`
(`char_utils.cpp:1359`) multiplies the stored value by the Wild-Fighting rage bonus
(specializations.md); the stored `points.ENE_regen` is computed by `recalc_abilities` whenever
stats/gear change, and external haste/slow affects add to it directly (e.g. ±40,
`handler.cpp:994`, `ranger.cpp:1624`).

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
> ≈ +26 % attack speed (~122→153, ceiling ~234), but a **very heavy 2H** (`str_speed ≈ 12,000`)
> gains only ≈ +11 % (~87→96, ceiling ~110). So fast-attack is ~2–3× more valuable on light
> weapons; on a heavy weapon it's already near its low ceiling, and **Strength** (which raises
> both the bottleneck *and* the ceiling) is what actually speeds you up. The same diminishment
> applies to DEX's and stealth's speed contributions on heavy weapons.

(Separate multi-swing effects stack on top of raw speed: Light Fighting's ~20 % free double
strike and Wild-Fighting's rage attack-speed bonus — both in `specializations.md`.)

## Mobs
Mobs use the **NPC branches** of `get_real_OB`/`get_real_parry`/`get_real_dodge`
(`utility.cpp`): OB/parry/dodge are the stored `points.*` plus flat level/stat terms, and their
weapon damage is halved (`fight.cpp:2501`). They cannot riposte. Mob `ENE_regen` is read
straight from the `.mob` file (data-formats/world-files.md), not computed from stats.

## Open questions
- **`armor_effect` / `apply_armor_reduction`** specifics: how AC/armor by hit location and
  weapon type reduce damage (`fight.cpp armor_effect`).
- **Resistances/vulnerabilities** and damage-type handling (`check_resistances`, `fight.cpp`).
- **Special-attack damage paths** (archery `ranger.cpp`, spells → `magic.md`).
- `points.damage`/`points.OB` base values for players (stance/affect sources, `set_player_ob`).
