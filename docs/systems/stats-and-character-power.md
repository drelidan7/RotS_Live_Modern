# Stats, level & proficiency — how a character's power is built

**Source files:** `src/profs.cpp` (`class_HP:136`, `existing_profs:37`, `race_modifiers:50`,
`get_primary_stat:548`, `stat_assigner:580`, `roll_abilities:693`, `recalc_abilities:716`,
`advance_level_prof:190`), `src/limits.cpp` (`advance_mini_level:86`, `mana_gain:128`,
`hit_gain:200`, `do_start:864`), `src/db.cpp` (`init_char:2895`), `src/interpre.cpp`
(creation `nanny:2261`), `src/utils.h` (stat/derived macros), `src/consts.cpp`
(`square_root:2100`, `max_race_str:2256`)
**Status:** ✅ baseline complete. Combat-applied stats (OB/damage/parry/dodge) and
willpower/spell-power are derived in the combat/magic systems — forward-referenced here.

## Purpose
This is the foundation for every other system: what the six abilities are, how a character's
**class-point allotment** at creation translates into lasting power (HP, mana, attack speed,
proficiency), and how **level**, **mini-level**, and **per-profession level** advance. Read
this before the combat-loop doc — combat formulas consume the values defined here.

---

## 1. The six abilities
`char_ability_data` (`structs.h:1037`), field order **str, lea, intel, wil, dex, con**:

| Ability | Macro | Primary role (see §6 for formulas) |
|---------|-------|-------------------------------------|
| **Strength** (str) | `GET_STR` | carry weight, melee attack speed (`str_speed`), damage |
| **Intelligence** (intel) | `GET_INT` | mana pool & regen; Mage primary stat |
| **Will** (wil) | `GET_WILL` | mana, mental resistance/willpower; Mystic primary stat |
| **Dexterity** (dex) | `GET_DEX` | attack speed, dodge, carry count; Ranger primary stat |
| **Constitution** (con) | `GET_CON` | hit points & HP regen, movement |
| **Learning** (lea) | `GET_LEA` | governs how many **practice sessions** you gain (see §4). *Not* a class primary stat. Also a **drainable mental stat**: curse-type effects subtract from it and the victim **dies if it hits 0** (`clerics.cpp:367`); it regenerates over time (`limits.cpp:702`). |

Each character carries **three copies** of the ability block (`structs.h:1684-1686`):
- `constabilities` — the **rolled/permanent** scores (the baseline identity).
- `abilities` — the **maxima** after level/skill/race derivation (`recalc_abilities`).
- `tmpabilities` — the **current** values (drained by damage, drugs, spells; capped at
  `abilities`). `GET_STR` etc. read `tmpabilities`; `GET_*_BASE` read `abilities`.

`GET_STR` returns `-1` if str is 0 (`utils.h:352`) — a sentinel, not a real score.

---

## 2. Class points — the allotment that defines a character
At creation a player distributes points across the four professions
(Mage, Cleric/Mystic, Ranger, Warrior). The raw points live in
`char_prof_data.prof_coof[]` (`structs.h:1268`) — accessed as `GET_PROF_POINTS`.

**The real budget is 150 points total** (`interpre.cpp:2704,3085`; `points_used:185`). There
is a separate **per-profession clamp of 165** (`interpre.cpp:3129`), but with only 150 to
spend that clamp is never the binding limit — even an all-in-one build tops out at **150 in a
single class**. So treat **150** as both the total and the practical single-class maximum;
the 165 is just a defensive bound in the allocation code.

Players either pick a **preset** class or go custom (`existing_profs:37`), e.g.:
```
'm' Mage      {Mage100, Cleric25, Ranger16, Warrior9}
'w' Warrior   {Mage9,   Cleric16, Ranger25, Warrior100}
'a' Adventurer{Mage36,  Cleric36, Ranger36, Warrior42}
```
The creation prompt warns: *"the more points you spend on any class, the less each
following point will benefit you."* That diminishing return is literal — see below.

### Points → proficiency coefficient (the sqrt curve)
`GET_PROF_COOF(prof, ch)` = `square_root[prof_points]`, with race tweaks
(orc ×≈⅔, uruk mage penalty) (`utils.h:330`). The `square_root` table is
**`square_root[x] = round(100·√x)`** (`consts.cpp:2100`), so:

| Points | Coefficient | "%" (coef/10) |
|-------:|------------:|--------------:|
| 9 | 300 | 30% |
| 16 | 400 | 40% |
| 25 | 500 | 50% |
| 100 | 1000 | 100% |
| 165 | ~1281 | ~128% |

So **a coefficient of 1000 (= 100 points) is "100% proficiency."** Because it's √-based,
going 100→121 points (+21) only moves you 100%→110%. This coefficient scales skill caps,
proficiency-level gain (§4), and class effectiveness.

### Points → class level at level 30 (the `levels` command)
The in-game `levels` command (`do_levels`, `act_info.cpp:2658`) prints, for each character
level `i` from 1 to 30, the class level you have in each profession:
```
class_level(i) = i · GET_PROF_COOF(prof) / 1000        (integer division)
```
At the level-30 row this is `30 · coef / 1000 = 3 · √points` for normal races. Class levels
are only displayed/earned through character level 30 (the loop stops at `i < 31`), so this is
your **final** class level in each profession. Mapping for representative point buys
(non-orc/uruk; `square_root` table values from `consts.cpp:2100`):

| Points | Coef | Class level @ L30 |
|-------:|-----:|------------------:|
| 0 | 0 | 0 |
| 4 | 200 | 6 |
| 9 | 300 | 9 |
| 16 | 400 | 12 |
| 25 | 500 | 15 |
| 36 | 600 | 18 |
| 49 | 700 | 21 |
| 64 | 800 | 24 |
| 81 | 900 | 27 |
| 100 | 1000 | 30 |
| 121 | 1100 | 33 |
| 144 | 1200 | 36 |
| **150** | **1224** | **36** |

So with the 150-point budget, the **maximum class level in any single profession is 36** (all
150 in one class). A balanced 4-way split (~37 each) yields ~`3·√37 ≈ 18` in each. Racial
class maluses (§9) reduce `coef` and therefore these class levels — e.g. a Common Orc's
coefficient is ×≈⅔, so the same points give a proportionally lower class level.

### Points → hit points (`class_HP:136`)
```
class_HP = √(3·WarriorPts + 2·RangerPts + 1·ClericPts) · 200      (orc ×4/7)
```
**Mage points contribute zero HP.** Warrior points are worth 3× ranger-equivalent and
6× cleric-equivalent for survivability. `class_HP` feeds the HP formula (§6) and also decides
whether Con or Learning is your 3rd-vs-5th-priority rolled stat — Con is prioritized when
`class_HP ≥ HEALTH_PROF_CUTOFF` (**3000**, `profs.cpp:513,634`).

---

## 3. How abilities are rolled at creation
`do_start` → `roll_abilities(ch, 80, 85)` (`limits.cpp:879`, `profs.cpp:693`):
1. `init_char` seeds all abilities to 9 (`db.cpp:2913`).
2. `stat_assigner` builds a **priority order**: each profession's **primary stat**
   (Mage→Int, Cleric→Will, Ranger→Dex, Warrior→Str — `get_primary_stat:548`) is ranked by
   how many points you put in that class. The highest-invested class's primary stat gets the
   highest roll. Con and Learning fill the remaining (3rd/5th) slots (`stat_assigner:580-637`).
3. `get_stat_array` rolls six values summing to **80–85** and assigns them in that priority
   order (`assign_stats:640`).
4. **Race modifiers** (`race_modifiers:50`) are added, floored at **1**
   (`assign_stats:655-675`). Example (str,int,wil,dex,con,lea): Dwarf `+2,0,-2,-3,+4,-1`;
   Wood Elf `-1,+1,0,+2,-2,0`; Olog-Hai `+4,-4,-4,-3,+4,-3`.

So your class allocation doesn't just buy proficiency — it **steers your best rolled stats
toward that class's primary ability**.

---

## 4. Level, mini-level, and proficiency level
Three progression counters:
- **`level`** — the headline level. Mortal cap **`LEVEL_MAX = 30`** (`structs.h:52`);
  `GET_LEVELA` clamps to 30. Increments when `GET_EXP ≥ xp_to_level(level+1)`
  (`advance_mini_level:96`), where **`xp_to_level(lvl) = lvl² · 1500`** (`limits.cpp:84`) —
  a quadratic curve (level 30 ≈ 1.35M xp). (Per-mini-level XP = `xp_to_level(mini_level)/10000`,
  `limits.cpp:385`.)
- **`mini_level`** — a fine-grained sub-level incremented continuously as XP accrues
  (`advance_mini_level:86`, called from `gain_exp`). Each tick has a ~2% chance to add a
  permanent point of base HP (`constabilities.hit`, `:91`). `mini_level` (not `level`) drives
  the HP curve in §6, giving smooth growth. `max_mini_level` tracks the peak.
- **Per-profession level** `prof_level[prof]` — each class levels **independently**, faster
  the higher its coefficient: a profession advances when
  `mini_level · GET_PROF_COOF(prof) ≥ 100000 · (prof_level+1)` **and**
  `GET_PROF_COOF(prof) · 30 ≥ 1000 · (prof_level+1)` (`advance_mini_level:102-107`).
  `advance_level_prof` grants per-class perks on each gain (e.g. Mage +2 max mana,
  `profs.cpp:190`). For NPCs, `GET_PROF_LEVEL` just returns `GET_LEVEL` (`utils.h:316`).

**Practice sessions** are recomputed on every `advance_level`
(`update_available_practice_sessions`, `char_utils.cpp:1392-1404`):
```
max_practices = 10 (free) + level·PRACS_PER_LEVEL + level·max_lea / LEA_PRAC_FACTOR
available     = max_practices − practices_already_spent
```
with `PRACS_PER_LEVEL = 3`, `LEA_PRAC_FACTOR = 5` (`structs.h:34-35`) and `max_lea` =
permanent Learning (`abilities.lea`). So **Learning's payoff is bonus practices that scale
with level**: e.g. at level 30, each point of Learning adds `30/5 = 6` practices — lea 18
yields +108 practices on top of the `90 + 10` base, roughly doubling the total. Practices are
the currency spent to raise skills/spells (→ systems/skills.md).

### mini-level ↔ level, and progression past level 30
`mini_level` advances while `mini_level² · 3/20 ≤ EXP` (`gain_exp_regardless:435`), and
`xp_to_level = lvl²·1500`, so the two stay locked at **`mini_level = 100 · level`** (hence
`LEVEL_MAX·100 = 3000` at level 30, and ~100 mini-levels gained per character level).

Mortals keep leveling to **90** (`LEVEL_IMMORT = 91`, `structs.h:50`), but **class levels
stop growing at character level 30**: `do_levels` only computes rows up to 30, and in the HP
formula (§6) both `min(30, level)` and `min(3000, mini_level)` are pinned at their level-30
values. What *does* keep growing past 30:

- **Hit points.** Each mini-level advance has a **~2% chance** (`number() ≥ 0.98`, where
  `number()` is uniform [0,1], `utility.cpp:928`) to add **+1 permanent base HP**
  (`constabilities.hit`, `advance_mini_level:91`). Over ~100 mini-levels per character level
  that's **≈ +2 `constHit` per level** (the code comment's "≈2 HP/level"). Because the HP
  formula adds `constHit · CON / 20`, the **realized HP gain per level beyond 30 is
  ≈ 2 · CON / 20 = CON / 10 HP per level** on average — e.g. ~2 HP/level at CON 20, ~3 at
  CON 30. (Note this only accrues on *new* ground, i.e. when `mini_level > max_mini_level`;
  re-leveling after XP loss grants no new `constHit`.)
- **Practice sessions.** `max_practices = 10 + level·3 + level·lea/5` keeps rising because
  `level` climbs to 90 — so high levels keep yielding practices even with class levels frozen.

So a level-31→90 mortal is gaining HP (CON-scaled, ~2–3/level) and practices, but no further
class levels, OB/parry growth from level, or class-HP scaling.

---

## 5. The three ability copies in motion
`recalc_abilities` (`profs.cpp:716`) runs whenever stats/level/gear change: it copies
`constabilities`→`abilities`, then recomputes the **max** hit/mana/move (and attack speed)
from current ability values, level, mini-level, proficiency levels, skills, race, and the
wielded weapon. `tmpabilities` is then clamped to the new maxima.

---

## 6. The value of a stat — derived formulas
What one extra point in each ability buys (`recalc_abilities:729-805`, regen in
`limits.cpp`):

**Hit points** (`:729`):
```
max_hit = 10 + min(30, level)
        + constHit · CON / 20
        + (class_HP · (CON+20) / 14) · min(3000, mini_level) / 100000
   (+10% if Defender spec; reduced by Stealth skill)
```
→ **CON** scales HP both directly and as a multiplier on `class_HP·mini_level`; its value
grows with both your warrior/ranger investment and your progress.

**In plain English:** your hit points come from three stacked pieces. First, a small flat
floor (10 plus your level, which stops counting at 30). Second, a slowly-accumulating pool of
*permanent* base HP (`constHit`) that ticks up by roughly 2 points every level you adventure
through — this piece is multiplied by your Constitution. Third, and by far the largest, your
**survivability rating `class_HP`** (bought with Warrior, Ranger, and Cleric points — see §2)
multiplied by both your Constitution and how far you've progressed (mini-level, which maxes out
at level 30). So a character's HP is essentially *"how many melee/survival class points did I
buy"* × *"how high is my CON"* × *"how far have I leveled (to 30)."* Constitution multiplies
almost every term, which is why it's the single most valuable point of HP — and why the same
CON is worth more to a heavy-Warrior build (big `class_HP`) than to a mage. Past level 30 the
two level-driven terms freeze, so only the small `constHit` piece keeps growing (≈ CON/10 HP
per level, see §4).

**Mana** (`:741`): `max_mana = constMana + INT + WILL/2 + 2·profLevel(Mage)`
→ **INT** is worth 2× **WILL** for mana pool.

**Movement** (`:745`): `max_move = constMove + CON + 20 + profLevel(Ranger) + travelling/4`
(+15 Wood/High Elf, +50 Beorning).

**Attack speed / energy regen** (`:757-805`): with a weapon, derived from `GET_BAL_STR`
and `DEX` against weapon weight×bulk (`str_speed`, `null_speed`), plus the Attack/Stealth
skills; barehanded it's `60 + 5·DEX`. → **STR** drives weapon swing speed (heavier weapons
lean on STR), **DEX** helps light weapons and barehanded. Faster energy regen = more attacks.

**Carry** (`utils.h:588`): `weight ≤ 2000 + 1000·STR`; `count ≤ 5 + DEX/2 + level/2`.

**Regen per game hour** (PCs):
- `hit_gain = 8 + CON/2 + profLevel(Warrior)/3 + profLevel(Ranger)/5` × position mult
  (`limits.cpp:200`).
- `mana_gain = 8 + INT/2 + WILL/5 + (profLevel(Mage)+profLevel(Cleric))/5` × position mult
  (`limits.cpp:128`). Sleeping/resting/sitting multiply regen; poison and hunger/thirst cut
  it to 25%.

**Diminishing STR** (`GET_BAL_STR`, `utils.h:357`): above `max_race_str` (=22 for all races,
`consts.cpp:2256`) each further STR point counts only half for the speed/damage calc.

### Combat- and spell-applied stats (forward references)
`OB`, `damage`, `parry`, `dodge` (`GET_OB/GET_DAMAGE/GET_PARRY/GET_DODGE` read
`points.*`, `utils.h:411-442`) are **not** set in `recalc_abilities` for players — they are
computed in the combat loop from abilities + skills + weapon each round. `willpower`
(mental-fight strength) and `spell_power`/`spell_pen` (spell effectiveness/penetration) are a
base plus gear/affect modifiers (`handler.cpp:375`). Their full formulas belong in
**systems/combat-loop.md** and **systems/magic.md** (to be written).

---

## 7. Mobs vs. players
- Mob abilities, OB, parry, dodge, damage, energy-regen, perception, resistance, spirit, etc.
  are **read directly from the `.mob` file** (see `data-formats/world-files.md`), not rolled
  or derived from class points.
- `GET_PROF_LEVEL(prof, mob)` and `GET_PROF_COOF(PROF_GENERAL,…)` collapse to the mob's
  `GET_LEVEL` (`utils.h:316,330`) — mobs have a single level, no per-profession system.
- Regen formulas branch for NPCs (e.g., out-of-combat mobs regen mana 1.5× — `limits.cpp:152`).
- Guardian/summoned mobs get stats set by formula from the caster's level
  (`mystic.cpp:1491+`).

---

## 8. Worked example — what a created Warrior looks like
Preset `'w'`: Mage 9 / Cleric 16 / Ranger 25 / Warrior 100.
- Coefficients: Mage 300 (30%), Cleric 400 (40%), Ranger 500 (50%), Warrior 1000 (100%).
- `class_HP = √(3·100 + 2·25 + 1·16)·200 = √366·200 ≈ 3826`.
- Rolled-stat priority: STR (Warrior, highest) gets the top roll; then Con/Learning; Dex
  (Ranger) next; Will (Cleric); Int (Mage) lowest — then race mods, floored at 1.
- Each extra **CON** point ≈ `+constHit/20` plus a `class_HP`-scaled term that grows with
  mini-level — i.e. CON is worth far more to this warrior (high `class_HP`) than to a pure
  mage (`class_HP`≈ from cleric pts only). Each extra **STR** speeds heavy-weapon swings up to
  the 22 soft cap. Putting the 150th point into Warrior past 100 yields ever-less coefficient
  (√ curve), illustrating the creation warning.

---

## 9. Racial information

### Stat modifiers (`race_modifiers`, `profs.cpp:50`)
Added to the rolled scores at creation, then floored at 1 (`assign_stats:655`). Column order
is **str, int, wil, dex, con, lea** (confirmed by `get_str_mod`…`get_lea_mod`,
`profs.cpp:75-126`; columns 7–8 are unused). Race ids from `structs.h:858-869`:

| Race (id) | Str | Int | Wil | Dex | Con | Lea | Playable? |
|-----------|----:|----:|----:|----:|----:|----:|-----------|
| God (0) | 0 | 0 | 0 | 0 | 0 | 0 | imm only |
| Human (1) | 0 | 0 | 0 | 0 | 0 | 0 | yes |
| Dwarf (2) | +2 | 0 | −2 | −3 | +4 | −1 | yes |
| Wood Elf (3) | −1 | +1 | 0 | +2 | −2 | 0 | yes |
| Hobbit (4) | −3 | −1 | 0 | +2 | +2 | 0 | yes |
| High Elf (5) | 0 | +2 | 0 | +2 | −2 | 0 | yes |
| Beorning (6) | +4 | −4 | −2 | 0 | +4 | −2 | yes |
| Uruk-Hai (11) | 0 | −4 | −3 | 0 | +2 | −3 | yes |
| Harad (12) | 0 | 0 | −1 | 0 | +1 | 0 | NPC |
| Common Orc (13) | −1 | −3 | −3 | −1 | −1 | −5 | yes |
| Uruk-Lhuth (15) | −1 | −1 | −3 | 0 | +1 | −2 | yes |
| Olog-Hai (17) | +4 | −4 | −4 | −3 | +4 | −3 | yes |
| Haradrim (18) | 0 | −2 | −2 | +2 | 0 | −3 | yes |

(Easterling 14, Undead 16, Troll 20 are NPC-only with all-zero modifiers.)

### Class maluses
Two races take penalties to their proficiency coefficient (and thus class levels, §2) — built
into `GET_PROF_COOF` (`utils.h:330`) and `class_HP` (`profs.cpp:140`):

- **Common Orc (RACE_ORC, 13):** coefficient for **every** class is reduced to
  `(square_root[pts]·2 + 2)/3` ≈ **⅔ of normal** — so the same point buy yields ~⅔ the class
  level. Additionally `class_HP` is scaled **×4/7** (`profs.cpp:140-142`), reducing the HP
  pool. Orcs are the most penalized progression-wise.
- **Uruk-Hai (RACE_URUK, 11):** **Mage** coefficient only is reduced by a flat **−100**
  (`GET_URUK_MAGE_PENALTY`, `utils.h:327`) — i.e. ~−10 proficiency points of mage power; their
  other three classes are normal, and there is no `class_HP` malus.

No other race has a coefficient/HP class malus; the rest differ only via stat modifiers above
and the misc racial effects below.

### Other racial effects (cross-references)
- **Movement** (`recalc_abilities:747-753`): Wood Elf & High Elf `+15` max move; Beorning
  `+50`.
- **Energy regen / attack speed** (`recalc_abilities:791-795`): Dwarf wielding an **axe**
  gains up to +10; Haradrim wielding **spears** gains up to +20.
- **STR soft cap** (`GET_BAL_STR`): 22 for all races (`max_race_str`, `consts.cpp:2256`).
- **Starting rooms** vary by race (`consts.cpp:2281`), e.g. Human/Dwarf/Hobbit 1160, Wood Elf
  1170, Beorning 1184, Uruk-Hai 10263, Common Orc 14499, Uruk-Lhuth 13626, Olog-Hai 1129,
  Haradrim 6604.

---

## 10. Offensive (OB), Parry (PB) & Dodge (DB) bonuses
These three derived combat values live here (not in the combat doc) because they are direct
functions of the stats above. The combat loop (→ `systems/combat-loop.md`) *consumes* them;
`systems/combat-stat-examples.md` works concrete builds. All player formulas below are from
`char_utils_combat.cpp` (`get_real_ob:335`, `get_real_parry:205`, `get_real_dodge:179`) at
**Normal tactics, level 30, no encumbrance**; other tactics apply the multipliers in the
`*_tactics_modifier` helpers. `points.OB/parry/dodge` are small stored bases (stance/gear/
affect contributions); treat them as 0 for a clean baseline.

`bal_str` = `get_bal_strength` (= STR below the racial cap of 22, half-rate above;
`char_utils.cpp:415`). `war`/`ranger` = per-profession levels (§4). `max_war = 30`
(20 for orcs, `char_utils.cpp:339`).

### OB — offensive bonus (`get_real_ob`)
```
ob_bonus = bal_str + 1.5·war + 1.5·max_war·(level/30)          # = bal_str + 1.5·war + 45 at L30
OB = points.OB − skill_penalty
   + weapon_term            # 1H: (6 − 2·bulk);  2H: bulk·(200+2h_knowledge)/100 − 15
   + ob_bonus               # Normal tactics passes ob_bonus through unchanged
   + weapon_knowledge·(bulk+20)·0.008
   (− 10 in normal daylight visibility; − confuse effects)
```
Then each swing rolls: `OB_roll = (OB + rand(1..55+OB/4) + d35)·7/8 − 40` (+100 on a crit
roll of 35) — `roll_ob:111`.

**In plain English:** OB ("offensive bonus") measures how well you land a telling blow. Its
standing value is built mostly from your **Warrior level** (each level ≈ +1.5, on top of a
fixed ~45 chunk for being level-30) and your **weapon skill** scaled by the weapon's size,
with **Strength** adding one point each (1:1, half-rate above 22) and your stance (tactics)
nudging it up or down. That standing number is then *heavily randomized every swing* — a wide
random spread is added, the total is scaled to ⅞ and shifted down by 40, and a natural top
roll lands a critical (+100). The crucial part is that OB isn't just hit-or-miss: whatever is
left after the defender's dodge and parry are subtracted ("remaining OB") **feeds straight
into damage** (combat-loop), so out-OB'ing your opponent both lands more hits *and* makes each
one hit harder.

### PB — parry bonus (`get_real_parry`, with a weapon)
```
parry_bonus = 2·war + level(≤30) + bal_str
PB = points.parry
   + parry_bonus·0.5                                   # Normal tactics ×0.5
   + parry_knowledge_factor·(weapon.value[1]+20)·0.006 # value[1] = weapon parry rating
   (+ weapon.value[1]/2 if two-handed; − 10 in daylight)
parry_knowledge_factor = knowledge(weapon) (+0.5·knowledge(twohanded) if 2H) + 0.75·knowledge(parry)
```

**In plain English:** PB ("parry bonus") is the Warrior's active defense — deflecting blows
with the weapon. It scales with your **Warrior level** (≈ +1 per level), your **Strength**
(+0.5 per point), and — most of all — your **weapon and parry skills multiplied by the
weapon's parry rating** (`value[1]`), so a parry-built weapon in skilled hands is worth far
more than raw stats. Two-handers parry better (an extra half of the weapon's parry rating).
Aggressive stances trade parry away for offense (the tactics multiplier). One important
limiter: each successful parry **decays to ⅔ strength for the rest of the round**
(combat-loop), so parry is strong against a single foe but degrades when you're swarmed. You
need a weapon to parry at all (barehanded gets only half the level/strength bonus).

### DB — dodge bonus (`get_real_dodge`)
```
dodge_skill_factor = (dodge + 0.5·stealth + 60)·(0.005·ranger) + (dodge + 0.25·stealth)·0.05
DB = points.dodge + DEX + (dodge_skill_factor − dodge_penalty + 3)   # Normal tactics adds DEX
```
(skills here are the *raw* dodge/stealth skill values.)

**In plain English:** DB ("dodge bonus") is avoiding blows by moving, and it's the **Ranger's**
defense the way parry is the Warrior's. It's almost entirely **Dexterity** (1:1) plus your
**Ranger level scaling your Dodge and Stealth skills** — high ranger level turns those skills
into a large dodge, while a character with no ranger levels barely dodges at all (compare the
all-in W36 build in `combat-stat-examples.md`, whose DB collapses). **Heavy armor hurts it**
(the encumbrance `dodge_penalty` is subtracted), and stance shifts it (defensive adds Dex,
berserk halves it). So the natural defensive split is: Warriors lean on parry (weapon + STR),
Rangers lean on dodge (DEX + ranger skills), and a balanced fighter gets some of each.

### The marginal value of one stat point (Normal tactics, below caps)
| +1 to… | Effect |
|--------|--------|
| **STR** | **+1 OB** (1:1, via `ob_bonus`; half above STR 22) → **≈ +0.6–0.8 % damage per landed normal hit** (see below) **and** fewer misses vs dodge/parry. **+0.5 PB**. Also raises melee attack speed and lowers encumbrance penalties. |
| **DEX** | **+1 DB** (1:1, via the tactics term). Raises attack speed (esp. light weapons). No direct OB/PB. |
| **CON** | HP and HP-regen only (§6) — no OB/PB/DB. |
| **+1 warrior class level** | **+1.5 OB** and **+1 PB**. |
| **+1 ranger class level** | scales DB through `dodge_skill_factor` (0.5 %·(dodge+½stealth+60) per level). |

**Why STR ≈ +0.7 %/point of damage.** In a normal (non-accurate) hit, damage scales with
`ob_factor = remaining_ob + 100` (`calculate_hit_damage:386`, → `systems/combat-loop.md`).
Each STR point adds +1 raw OB → **+0.875** to `remaining_ob` after `roll_ob`'s ×7/8, i.e.
`+0.875/(remaining_ob+100)`. At a typical winning margin (`remaining_ob` ≈ 20–60) that's
**≈ 0.55–0.73 %** more damage **per landed hit**, before counting the extra hits STR buys by
beating dodge/parry more often. (The explicit `strength_factor` term in the damage formula is
numerically negligible — STR's damage value flows through OB.)

### Basic example (Normal tactics, L30, STR 20 / CON 18 / DEX 14, no gear bonuses)
A **default Warrior** (W class level 30): `ob_bonus = 20 + 1.5·30 + 45 = 110`;
`parry_bonus = 2·30 + 30 + 20 = 110` → PB contribution `55`; DB ≈ `DEX(14) + ranger/skill
terms`. A **36-warrior** (all 150 points in Warrior → W36) gets `ob_bonus = 20 + 1.5·36 + 45 =
119` (+9 raw OB ≈ +7.9 after the roll) and `parry_bonus = 2·36+30+20 = 122` → PB `61`, but
**lower DB** (no ranger levels) and no mana. The full build-vs-build grid — and how each value
moves when you shift STR/CON/DEX by a point — is in **`systems/combat-stat-examples.md`**.

### The damage value of a Warrior level
A Warrior class level (bought with warrior points, §2) raises damage through **two channels**;
a third, "rush," is a spec choice rather than a per-level effect:

1. **OB: +1.5 per warrior level** (`get_ob_bonus`) → ≈ **+1.31 effective remaining-OB** per
   level after `roll_ob`'s ⅞ scaling. Since a normal hit's damage ∝ `(remaining_OB + 100)`,
   that's `+1.31/(margin+100)` per level *on hits that land* — **and**, when the fight is
   close, more OB also makes more swings land at all (it pushes borderline dodges/parries into
   hits). This second effect is where most of the value lives against tough targets.
2. **Find-weakness frequency** (`does_find_weakness`, needs the `EXTRA_DAMAGE` skill): a
   warrior-level-scaled chance to deal **×1.5**. With the skill maxed:

   | Warrior level | Find-weakness chance | Avg damage ×mult (`1 + 0.5·p`) |
   |--------------:|---------------------:|-------------------------------:|
   | 24 | 26 % | 1.13 |
   | 27 | 29 % | 1.145 |
   | **30** | **33 %** | **1.165** |
   | 33 | 39 % | 1.195 |
   | 36 | 45 % | 1.225 |

   Note the **kicker above level 30** (`prob += war − 30`): levels 31→36 add chance faster
   than 25→30, so high warrior levels are disproportionately rewarded. (With *no* `EXTRA_DAMAGE`
   skill this channel is zero and a warrior level's damage value is purely the OB channel.)
3. **Rush** (`does_rush`): a flat 10 % chance of ×1.5, but **only** for the Wild-fighting
   specialization — it's a build choice worth ~+5 % flat damage *if* you take that spec, and it
   does **not** scale with warrior level. Don't count it as part of per-level value.

**Putting it together (vs. a level-30, 30-warrior baseline).** Because the OB channel's worth
depends entirely on the target's defenses, the same warrior levels are worth far more against a
tough opponent than a soft one:

| Warrior level | vs W30 — **low-defense** target (you out-OB them, ~always hit) | vs W30 — **high-defense** target (evenly matched, lots of borderline swings) |
|--------------:|:--:|:--:|
| 24 | ≈ **−6 %** | ≈ **−15 %** |
| 27 | ≈ **−3 %** | ≈ **−7 %** |
| 30 | baseline | baseline |
| 33 | ≈ **+4 %** | ≈ **+9 %** |
| 36 | ≈ **+8–9 %** | ≈ **+18 %** |

**Why the two columns differ so much.** Against a **low-defense** target you already land
nearly every swing and your OB margin is large (say ~150), so extra OB is just a small per-hit
bump (`1.31/250 ≈ +0.5 %/level`) and the find-weakness channel does most of the work — the
spread is modest. Against a **high-defense** target your margin hovers near zero, so each
warrior level both (a) is a bigger *fraction* of the small margin (`1.31/100 ≈ +1.3 %/level`)
*and* (b) converts a meaningful slice of misses into hits — together roughly **doubling** the
per-level value, and making the gap between a 24- and a 36-warrior swing from a few percent to
the order of ±15–18 %. The closer the match, the more every warrior level (and every point of
OB) is worth.

> Assumptions: `EXTRA_DAMAGE` maxed; W30 margins of ~150 (low-defense) and ~0 (high-defense);
> Normal tactics. Figures are approximate — see `combat-loop.md` for the damage formula and
> `combat-stat-examples.md` for the build grids.

---

> **Base seeds:** `do_start` sets the `const*` baselines `hit=10, mana=40, move=80`
> (`limits.cpp:881-884`); `constabilities.hit` then grows ~2%/mini-level (§4).

## Open questions
- **`willpower`/`spell_power`/`spell_pen`** base derivation (level/prof contribution beyond
  gear) — (→ magic/combat docs).
- The combat-round derivation of **OB/damage/parry/dodge** for players (→ systems/combat-loop.md).
