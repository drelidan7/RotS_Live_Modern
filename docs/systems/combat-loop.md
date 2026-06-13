# Combat loop — hit resolution & damage

**Source files:** `src/combat_manager.cpp` (`roll_ob:111`, `is_hit_accurate:131`,
`offense_if_weapon_hits:149`, `on_weapon_hit:322`, `calculate_weapon_damage:364`,
`calculate_hit_damage:384`, `apply_weapon_damage:443`, `apply_armor_reduction:467`),
`src/fight.cpp` (round driver, `hit`, `damage`), `src/char_utils_combat.cpp` (OB/PB/DB)
**Status:** 🟡 core swing resolution + damage documented. Round timing/energy, armor
reduction details, and special attacks are partial (see Open questions).

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

### 1. Roll the attacker's OB (`roll_ob:111`)
```
roll       = d35 (0..35)
OB_roll    = get_real_ob + rand(1 .. 55 + OB/4) + roll
OB_roll    = OB_roll·7/8 − 40
if roll > 34 (a natural 35): critical → OB_roll += 100
```
The `·7/8 − 40` is why each point of raw OB is worth ~0.875 of effective margin.

### 2. Compare against the defender (`offense_if_weapon_hits:149`)
Working value starts at the rolled OB, then:
1. **Position bonus:** if the victim is below `POSITION_FIGHTING`, `+10` per position step
   (helpless targets are easier).
2. **Accurate hit?** (`is_hit_accurate:131`) Only possible on **Careful** or **Aggressive**
   tactics; chance = `ranger_level − skill_penalty − dodge_penalty`, scaled by the
   `ACCURACY` skill. An accurate hit **skips the dodge/evasion checks** ("finds an opening").
3. **Dodge** (non-accurate only): subtract `get_real_dodge(victim)` (DB). If OB < 0 and not a
   crit → **dodged (miss)**.
4. **Evasion** (non-accurate only): subtract the evasion malus (only vs `AFF_EVASION`;
   scales with cleric levels). If OB < 0 and not a crit → **evaded (miss)**.
5. **Parry:** subtract `get_real_parry(victim)` (PB) × the victim's current-parry % (which
   then decays to ⅔ for subsequent swings this round). If OB < 0 → **deflected (miss)**, which
   can trigger a **riposte** (`does_victim_riposte:249`, dex/ranger/stealth-based) and grip
   checks on two-handers.

Whatever OB **remains** after these subtractions is `remaining_ob`, passed to damage.

### 3. Damage (`on_weapon_hit:322` → `calculate_hit_damage:384`)
```
weapon_damage = get_weapon_damage(weapon)        # barehanded = BAREHANDED_DAMAGE·10; mobs ×0.5
base          = weapon_damage + points.damage·10
random_factor = d100² + 10000                    # ∈ [10000, 20000]

if accurate:   damage = base · random_factor / 100000          # ≈ base × 0.1–0.2
else:          damage = (base · (remaining_ob+100) · random_factor
                         + 133·(1+twohanded)·bal_str) / 13300000
```
So a **normal** hit scales with `remaining_ob + 100` — beating the defense by more does more
damage — which is the channel through which **STR/OB raise damage** (stats §10). The explicit
`bal_str` term is numerically tiny. Then:
- **Find weakness** (`does_find_weakness:404`, warrior-level × `EXTRA_DAMAGE` skill): ×1.5.
- **Rush** (Wild-fighting spec, 10 %): ×1.5.
- **Armor reduction** (`apply_armor_reduction:467`) is applied per hit location before the
  final `apply_damage`.

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

**The shape of the damage algorithm.** For a normal (non-accurate) hit:
```
damage = (weapon_damage + points.damage·10) · (remaining_OB + 100) · random / 13,300,000
```
The `random` factor is `d100² + 10000` (∈ 10000–20000, averaging ≈ 13,333), so the constants
nearly cancel and the **average normal hit simplifies to**:
```
average damage ≈ base · (remaining_OB + 100) / 1000
```
From this you can read off the relationships:
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
- **Accurate hits ignore the OB margin entirely:** `damage = base · random / 100000`
  (≈ `base · 0.133` on average). That's a flat ~13 % of base regardless of margin — better than
  a normal hit only when your OB margin would have been below ~33. Accurate hits trade burst for
  a guaranteed landing.

## Three example swings (plain English)
One attacker throughout: a level-30 warrior whose **standing OB is 200** (after gear/skills),
wielding a sword (`base damage = 250` = weapon 200 + `points.damage`·10) on **Normal tactics**.
Numbers are illustrative — absolute damage depends on weapon/gear scaling — but every step
follows the real formulas above. The dice each swing are the `d35` accuracy roll and the
`d100` damage roll.

### A) Critical hit → MUTILATE
The attacker rolls a **natural 35** on the accuracy die — a critical.
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
A middling accuracy roll, no crit.
- *Roll the OB:* `d35 = 16`; `(200 + 45 random + 16)·7/8 − 40 = 229 − 40 = 189`.
- *Defender:* `189 − 40 dodge − 60 parry = 89` **remaining OB** (still positive → it lands;
  the parry weakens the victim's next parry to ⅔).
- *Damage:* `d100 = 35` → random factor `35² + 10000 = 11225`.
  `250 · (89+100) · 11225 / 13,300,000 ≈ 40` → after the orc's armor, say **~15**.
- On screen: **"You slash the orc hard."** A solid, ordinary blow — landed because OB cleared
  both defenses, but by a modest margin, so the damage sits in the low/mid tiers.

### C) A miss (dodged)
A poor accuracy roll against a very nimble foe (a ranger: **DB 150**, PB 90).
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

## Mobs
Mobs use simplified OB/PB/DB (`get_real_npc_*` in `char_utils_combat.cpp`): OB/parry/dodge are
the stored `points.*` plus flat level/stat terms, with weapon damage halved
(`calculate_weapon_damage:377`). They cannot make "accurate" hits or riposte.

## Open questions
- **Round timing / energy:** how `ENERGY`/`ENE_regen` gate the number of swings per round, and
  multi-attack skills (fast attack, swing, kick/bash) — trace the round driver in `fight.cpp`.
- **`apply_armor_reduction`** specifics: how AC/armor by hit location and weapon type reduce
  damage (`combat_manager.cpp:467`).
- **Resistances/vulnerabilities** and damage-type handling (`check_resistances`, `fight.cpp`).
- **Special-attack damage paths** (archery `ranger.cpp`, spells → `magic.md`).
- `points.damage`/`points.OB` base values for players (stance/affect sources, `set_player_ob`).
