# Cleric / Mystic system (powers & mental combat)

**Source files:** powers `mystic.cpp` (all `ASPELL(...)`), plus `spell_curse`/`spell_revive` there;
mental combat `clerics.cpp` (`do_mental` = the **will** command, `do_concentrate`,
`damage_stat`, `check_mind_block`, `weapon_willpower_damage`); saving throws `spell_pa.cpp:122-365`
(`saves_power`, `saves_mystic`, `saves_poison`, `saves_confuse`, `saves_insight`,
`saves_leadership`); caster level `mystic.cpp:68` (`get_mystic_caster_level`); spirit resource
`char_utils.cpp:93-110` (cap `MAX_SPIRITS = 90000`, gained on kills `fight.cpp:1256-1287`); power
table `consts.cpp:431-485,513-628`; ids `spells.h:85-117,177-179`; spec enum `structs.h:811`.
**Status:** ✅ powers, saves, mental combat, scaling. Mana regen (the mage resource) lives in
[magic-system.md §12](magic-system.md); this doc covers the **spirit** resource.

> **One class, two names.** "Cleric" and "Mystic" are the **same profession** (`PROF_CLERIC`);
> the code says `PROF_CLERIC` everywhere, players say "mystic". Its spells are called **powers**.
> Where the mage line (magic-system.md) is INT/spellpower/mana and rolls a **d20** save, the mystic
> line is **Willpower/Perception/spirit** and rolls **squared willpower contests** — a completely
> separate save family. A **default mystic is 30 mystic / 15 mage**; a default mage is the mirror
> (30 mage / 15 mystic), so every character has *some* of both.

## How to read this doc
Get these three quantities straight and the rest follows:

1. **Mystic caster level `L`** (`get_mystic_caster_level`, `mystic.cpp:68`) —
   `L = prof_level(CLERIC) + WIL/5` (+1 random chance for the `WIL % 5` remainder). Drives almost
   every power's magnitude/duration. **Willpower is baked into your caster level.**
2. **Willpower** — the offensive/defensive stat for **mental combat** (`saves_power` squares it).
3. **Perception** — gates whether you can *reach* a mind at all, and scales curse/concentrate.

The **spirit** pool (not mana) fuels powers (§1). "Typical" figures assume a **30-mystic** with a
**WIL ability score of ~22** (20–25 is the normal range; **25 is exceedingly high**) and
**best-in-slot willpower gear ≈ +10**. That gives caster level `L ≈ 34` and a mental-combat
**willpower of ~60** (§1 — note these are *different* numbers). Read the formulas as truth.

---

## 1. Caster level and the two resources

### Two different "will" numbers — don't conflate them
This is the mystic-side trap (mirror of the mage's double-`saving_throw`, magic-system §5):

| Quantity | Macro / field | Value | What it drives |
|---|---|---|---|
| **WIL ability score** | `GET_WILL` = `tmpabilities.wil` | **20–25** typical (25 very high); +`APPLY_WILL` gear | caster level `L` (via `/5`) |
| **Willpower** (derived combat stat) | `GET_WILLPOWER` = `points.willpower` | `≈ cleric_level + WIL` → **~50–65** for a 30-mystic | **every `saves_*` roll and `do_mental`** (§2, §3) |

`GET_WILLPOWER` is recomputed by `get_naked_willpower` (`utility.cpp:372`):
```
willpower = prof_level(CLERIC) + WIL_score − confuse/10   (+ APPLY_WILLPOWER gear)
```
So the player-facing "will" stat (20–25) is **only one input**; the number that actually fights
minds folds in your whole cleric level, landing around **50–65**. **Best-in-slot willpower gear
(≈ +10)** pushes that to ~60–75 (and, if it's `APPLY_WILL` stat gear, nudges `L` up by ~+2 too).

### Mystic caster level — `get_mystic_caster_level` (`mystic.cpp:68`)
```
L = prof_level(CLERIC) + WIL_score/5      (+1 random chance on the WIL % 5 remainder)
```
Uses the **WIL ability score** (the mirror of the mage's INT-based `get_mage_caster_level`), so a
30-mystic with WIL 22 has `L ≈ 34`. For powers cast on **others**, several spells average in the
*target's* mystic level (e.g. curing/restlessness: `(L_caster + L_victim)/2 + 5`;
detect-hidden/slow-digestion add the victim's cleric level), so buffing a fellow mystic is stronger
than buffing a warrior.

### Perception — the mystic's other core stat (`get_naked_perception`, `utility.cpp:354`)
Perception gates whether you can reach a mind, drives `saves_mystic`/`saves_insight`, and scales
curse/concentrate. For PCs:
```
perception = race_baseline + prof_level(CLERIC)·2     (+50 Insight / −50..−100 Pragmatism)
```
So perception is **strongly mystic-level-driven** (`×2` per level → +60 at 30 mystic) on top of a
fixed **per-race baseline** (`get_race_perception`, `utility.cpp:312`):

| Race | Base | Race | Base | Race | Base |
|---|---|---|---|---|---|
| High-elf (`RACE_HIGH`) | **100** | Human / Hobbit / Uruk / Harad(rim) | **30** | Dwarf | **0** |
| Undead | **60** | Easterling / Magus / Troll / Beorning / Olog-Hai | **30** | God | **0** |
| Wood-elf (`RACE_WOOD`) | **50** | Orc | **10** | | |

(NPCs: shadows = 100, otherwise their stored `GET_PERCEPTION`.) A human 30-mystic therefore sits at
`30 + 60 = 90` perception; a High-elf at `100 + 60 = 160`. Because **fear/haze/terror defense is a
pure perception roll** (`saves_mystic`: resist when `number(0,100) ≤ perception·9/10`), a
High-elf's 144+ effective threshold makes them **outright immune to those effects**, while an Orc
30-mystic (70) resists only ~63 % of the time. Willpower never enters fear defense directly — only
through Insight/Pragmatism shifting perception (§5).

### Spirit — the power resource (`points.spirit`, `char_utils.cpp:93`)
Mystic powers cost **spirit**, *not* mana:
- **Cost** = the power's `min_usesmana` column (`USE_SPIRIT`, `spells.h:331`) — a **flat** cost
  (0–10 for most powers; 25–55 for mass spells / shift), charged in `do_cast` (`spell_pa.cpp:942`).
  A **Fame-War** cleric pays **×0.80** (`spell_pa.cpp:944`).
- **Mental powers double-dip:** `curse` and `revive` *also* spend spirit **inside** the spell,
  proportional to how many stat-points they move (`number(0, stat_damage)` per stat) — so a big
  curse can drain a lot of spirit on top of the base cost.
- **Gained from kills, not time** (`fight.cpp:1256-1287`): when something dies, its
  `level × perception` worth of spirit is split among nearby characters by **perception share**.
  There is **no per-tick spirit regen — this is intentional** (see §7): spirit is a resource you
  *spend time accumulating* into a large pool (cap `MAX_SPIRITS = 90000`) and draw down when
  needed, rather than a steadily-refilling bar like mana.

> **Mana** (the *mage* resource) regenerates on a timer; that formula — and the fact that
> **mystic level feeds it via a `cleric_level/5` term** — is documented in
> [magic-system.md §12](magic-system.md). It matters here because a 30-mystic/15-mage still has a
> real mana pool for their mage spells.

---

## 2. The mystic saving-throw family (`spell_pa.cpp`)

Mystic powers do **not** use the mage `new_saves_spell` d20 (magic-system §3). They use a family of
purpose-built rolls. Positive outcome = **victim resisted**.

In the formulas below, **`Wp`** = the **derived willpower** stat (`GET_WILLPOWER` ≈ `cleric+WIL`,
~50–65; §1), **not** the 20–25 ability score. Subscripts `c`/`v` = caster/victim.

| Function | Used by | Formula (victim resists when…) |
|---|---|---|
| **`saves_power`** (`:122`) | **mental combat**, willpower weapons | `number(0, (Wp_v + bonus)²) > number(0, power²)` — squared willpower vs. the attacker's "power" |
| **`saves_mystic`** (`:291`) | haze, fear/terror, dispel | `number(0,100) ≤ Perception_v · 9/10` — pure perception roll |
| **`saves_poison`** (`:307`) | poison, black arrow | `number(off/3,off) < number(def/2,def)`, `off = Wp_c·8·Percep_c/100`, `def = 5·CON_v + 3·Wp_v (+30 wood-elf)` |
| **`saves_confuse`** (`:323`) | confuse, hallucinate | `number(0, Wp_c) < number(0, Wp_v − (NPC?5))` |
| **`saves_insight`** (`:339`) | insight vs. pragmatism | `Percep_c·Wp_c/100 < Percep_v·Wp_v/100 + number(0,10)` |
| **`saves_leadership`** (`:355`) | fear, terror | `saves_mystic` **or** `number(1,115) ≤ leader's Leadership` (mount: Ride) |

The throughline: **Willpower (`Wp`) and Perception** are to mystic saves what INT and the
`saving_throw` stat are to mage saves. There is no gear "spell save" stat in this family — your
*stats* (and willpower gear, which lifts `Wp` on both offense and defense) are the contest.
**Mind Block** (§3) adds a separate 20 % hard-stop layer on mental-stat attacks.

---

## 3. Mental combat — `will`, `concentrate`, `curse`

Mental combat is a **stat-attrition** duel: instead of HP, you grind down the opponent's six
ability scores (STR, INT, WIL, DEX, CON, LEA) and their "concentration". **Driving any stat to 0
kills the victim** (`damage_stat`, `clerics.cpp:294` → `die`). It runs on its own clock:
`GET_MENTAL_DELAY`, ticking in `PULSE_MENTAL_FIGHT = 8` pulses (**2 seconds**).

### Shared gates (every mental action)
1. **Peace room** blocks it. **Mental delay** must be ready (`> 1` = "mind is not ready").
2. **Reach check** (`clerics.cpp:181`): `Perception_ch · Perception_victim < number(1,10000)` ⇒
   "couldn't reach $S mind." It's the **product** of both perceptions, so high perception on
   *either* side makes the mind easier to reach — a very perceptive victim is *always* reachable
   (even as their perception makes them resist the effects below).
3. **Readable mind** (`clerics.cpp:169`): target must be a **Shadow**, **bodytype 1**, or a
   **Beorning** — most mobs "cannot be fathomed."
4. **Self mind-block** while you have Mind Block up: 75 % chance it prevents *your own* attack.

### `will` — `do_mental` (`clerics.cpp:89`)
The basic mental strike. Rolls **two** `saves_power` checks; each one the victim *fails* adds 1 to
`damg` (so `damg ∈ {0,1,2}`):
```
attacker power = Wp_ch   + (concentrating ? cleric_level/2 : 0)   # Wp = GET_WILLPOWER, ~50-65 (§1)
victim save_bonus =        (concentrating ? cleric_level/2 : 0)
```
On a hit (`damg > 0`): pick a random target `tmp = number(0,6)` — `0–5` = the six stats,
**`6` = concentration** (and the attacker *gains* `damg` spirit). `damage_stat` subtracts `damg`
from that stat. On total miss (`damg == 0`) there's a **25 % chance you damage yourself**. Success
grants exp and can **interrupt the victim's spellcasting** / ruin a prepared spell. Sets your
mental delay to `PULSE_MENTAL_FIGHT` (2 s).

### `concentrate` — `do_concentrate` (`clerics.cpp:468`)
A **charge-up**. Toggling it on sets `AFF_CONCENTRATION` and a deeply-negative mental delay; while
held it **passively buffs your `will`** (the `+cleric/2` power above) *and* your defense (the
`+cleric/2` save bonus). **Releasing it while fighting** unleashes a burst:
```
extra = (−mental_delay + number(0, PULSE_MENTAL_FIGHT−1)) / PULSE_MENTAL_FIGHT   # how long you held it
extra = extra · Perception_victim / 100
while (extra > 0 and victim fails saves_power(Wp_ch + extra, 0)):
    damage a random stat by 1;  extra −= 2
```
So the longer you concentrate, the bigger the multi-stat hit — but it's still gated by the
victim's willpower saves and scaled by *their* perception.

### `curse` — `spell_curse` (`mystic.cpp:119`)
A spirit-fueled AoE-on-one-target stat assault. It computes a **point budget** and scatters it
across stats:
```
count = (L + 20) · Perception_victim / 100 / 10          # number of stat-damage points
```
Then it distributes `count` among the six stats (random, biased to clump on one stat), spending
spirit per stat hit, each applied through `damage_stat`. Blocked by your own Mind Block and the
readable-mind gate. Mental delay after = `actual_count · PULSE_MENTAL_FIGHT`. **Curse scales with
mystic level *and the victim's perception*** — high-perception targets take a bigger curse (the
flip side of perception being their mental armor elsewhere).

### `damage_stat` & Mind Block (`clerics.cpp:294`, `:62`)
All the above route stat damage through `damage_stat`, which: checks **SPECIAL_DAMAGE** procs and
Big-Brother PK rules, **Mind Block**, and **sanctuary**; gives anger; sets combat; subtracts the
stat; and **kills on 0**. **Mind Block** (`check_mind_block`) only guards the **mental stats**
(INT, WIL, LEA, concentration — `is_mental_stat`): each hit has a **20 %** chance to be fully
blocked while chipping the block's duration until it breaks.

### Willpower weapons (`weapon_willpower_damage`, `clerics.cpp:537`)
A weapon **attuned** (the `attune` power sets `ITEM_WILLPOWER`) lands a bonus mental hit on melee
swings (`fight.cpp:2444`): same perception gate, a `saves_power` vs. `weapon_level +
victim_cleric_level`, then `max(1, min(WIL_attacker/10, 4))` damage to a random stat. (Note the
save scales on the **defender's** mystic level — a mystic defends their own mind better.)

---

## 4. Power catalog

Level = `prof_level(CLERIC)` to learn; Spirit = base `USE_SPIRIT` cost (mental powers spend more
on top, §1). `L` = mystic caster level (§1). Grouped by role.

### Mental / offensive
| Power (`id`) | Lvl | Spirit | Effect & scaling |
|---|---|---|---|
| **Curse** (67) | 1 | 1 | §3 — stat assault, `count = (L+20)·Percep_v/100/10` |
| **Revive** (68) | 1 | 2 | §3 inverse — **restores** the victim's most-damaged stats, `count ≈ (3·9+L)·Percep_v/100/9`, spends spirit |
| **Hallucinate** (63) | 3 | 2 | foe's own physical swings "hit thin air"; charges = `cleric/10 + 2 (+1 if cleric>30) (+1 Illusion-spec)`; save `saves_confuse` — §4.2 |
| **Haze** (52) | 5 | 1 | `AFF_HAZE` disorient (random moves / dropped command targets); **+6 `L` Illusion-spec**; save `saves_mystic`; can haze a room — §4.2 |
| **Fear** (53) | 12 | 5 | flee effect, dur `L`; **+6 `L` Illusion-spec**; saves `saves_mystic` **&** `saves_leadership` |
| **Terror** (58) | 18 | 5 | room-wide Fear; **+6 `L` Illusion-spec** |
| **Poison** (43) | 14 | 5 | `AFF_POISON` (−2 STR, dur `L+1`) + 5 dmg; save `saves_poison`; can poison a room/food |
| **Confuse** (111) | 1* | 10 | `AFF_CONFUSE` front-loaded skill/knowledge/OB crush `2·dur−10`, dur `10+L`; save `saves_confuse`; *Illusion spec-gated* — §4.2 |

### Healing / regeneration *(Regeneration spec: +6 to the healing level)*
| Power (`id`) | Lvl | Spirit | Effect & scaling |
|---|---|---|---|
| **Curing Saturation** (45) | 4 | 1 | **flat** +HP / −move converter, `modifier = L+5` (avg w/ target), dur `(L+5)·10` updates — §4.1 |
| **Restlessness** (46) | 6 | 0 | **flat** +move / −HP converter (mirror of Curing; can go negative/lethal — §7); §4.1 |
| **Regeneration** (64) | 15 | 5 | strongest HP-over-time, **front-loaded** (∝ remaining duration), dur `(L−10)/2·20` updates — §4.1 |
| **Vitality** (57) | 11 | 5 | move-regen, **front-loaded** like Regeneration, dur `(L/3)·20` updates — §4.1 |
| **Mass Regen / Vitality / Insight** (158-160) | 15/11/6 | 50/50/25 | group-wide; **require 100 % skill** in the base power; free to "cast" (Expose-style) |
| **Resist Poison** (44) | 3 | 2 | suppress active poison |
| **Remove Poison** (87) | 8 | 2 | cure poison (char or food/drink) |

### Buffs / self & ally
| Power (`id`) | Lvl | Spirit | Effect & scaling |
|---|---|---|---|
| **Mind Block** (86) | 3 | 5 | mental-stat shield (§3), dur **`15 + 2·cleric`** (self only) |
| **Insight** (50) | 6 | 2 | **+50 Perception**, dur `10+L`; breaks/blocked by Pragmatism; save `saves_insight` |
| **Pragmatism** (51) | 6 | 5 | **−50 Perception** (−100 vs. wood-elf) debuff; the anti-Insight |
| **Evasion / "armor"** (42) | 2 | 0 | `AFF_EVASION` + armor mod `loc_level`; self `(L+5)/2`, ally `(cleric_v+L+5)/4` |
| **Resist Magic** (47) | 8 | 0 | **+`L/6` mage saving-throw** (`APPLY_SAVING_SPELL`, feeds magic-system §4); **+1 Protection-spec** |
| **Protection** (89) | 0* | 5 | elemental/physical **resistance** (`APPLY_RESIST` fire/cold/lightning/physical), dur `2L`; *Protection spec-gated* |
| **Death Ward** (83) | 20 | 0 | ward, dur `2L`, modifier `L/2` |
| **Sanctuary** (56) | 16 | 2 | `AFF_SANCTUARY`; blocked by Anger; dur = target cleric level |
| **Detect Hidden** (41) / **Detect Magic** (69) / **Infravision** (66) | 1-17 | 0-2 | detection buffs, dur scales with `L` (×3 / ×5 / ×1) |
| **Slow Digestion** (48) | 11 | 1 | slows hunger, dur `loc_level+12` |

### Utility / misc
| Power (`id`) | Lvl | Spirit | Effect |
|---|---|---|---|
| **Divination** (54) | 13 | 2 | reveals room flags, exits, doors/keys, contents (a builder/scout tool) |
| **Attune** (—) | — | — | flags wielded weapon `ITEM_WILLPOWER` → enables willpower-weapon hits (§3) |
| **Enchant Weapon** (60) | 20 | — | +6 OB + alignment flag on a non-magic weapon (one-time) |
| **Dispel Regeneration** (49) | 13 | 3 | strips Curing/Restless/Regen/Vitality; enemy targets get a `saves_mystic` |
| **Guardian** (65) | 10 | — | *Guardian spec-gated* — summons a pet mob; see §5 |
| **Shift** (70) | 30 | 55 | toggles shadow form — **immortal-only** in practice |

\* Levels marked `*` are spec-gated powers (`learn_type` includes `LEARN_SPEC`); the table `level`
is nominal.

### 4.1 The regeneration machinery — Curing / Restlessness / Vitality / Regeneration

These four powers all heal by injecting a **bonus** into the per-tick HP/move regen pipeline
(`hit_gain`/`move_gain`, `limits.cpp:200`/`:273`). None of them heal instantly — they raise your
regen *rate* for a duration. There are **two distinct mechanisms**, and only two of the four are
front-loaded.

**The shared plumbing.** Every `PULSE_FAST_UPDATE` (12 pulses = **3 real seconds**, call it one
*update*; **20 updates per MUD hour** since `FAST_UPDATE_RATE = 20`), `fast_update`
(`limits.cpp:1483`) credits `HP += hit_gain / 20` and `move += move_gain / 20`, with probabilistic
integer rounding (so per-update figures below are averages). `hit_gain`/`move_gain` add a bonus from
the active affects via `get_bonus_hit_gain` (`limits.cpp:173`) / `get_bonus_move_gain`
(`limits.cpp:251`), and that whole bonus is multiplied by **`perception/100`**
(`get_perception`, `char_utils.cpp:1293`, **clamped to 0–100**). So:
- **All four are perception-scaled.** At perception 100 you get the full value; at 50, half; at **≤ 0
  the spells do nothing** (the function early-outs to gear-only regen). There is **no** benefit above
  100 (the clamp). A human cleric sits at `30 + 2·level` perception (§1), i.e. ~78 / 90 / 100 at
  24t / 30t / 36t — so a sub-35 mystic is also losing regen to a sub-100 perception multiplier.
- **All four stack with your base regen** (CON, position, etc.) and with each other (Curing+Regen
  both add to HP; Restlessness+Vitality both add to move).

**Mechanism A — flat (`modifier`-based): Curing Saturation & Restlessness.** The bonus is `±modifier`,
which is **set once at cast and never changes**, so the rate is **constant** for the whole duration
then stops dead. These two are **HP↔stamina converters**, exact mirrors of each other:

| Power | HP regen | Move regen | Net |
|---|---|---|---|
| **Curing Saturation** (`:184`,`:262`) | **+modifier** | **−modifier** | converts stamina → HP, 1:1 |
| **Restlessness** (`:188`,`:264`) | **−modifier** | **+modifier** | converts HP → stamina, 1:1 |

Both set `modifier = healing_level = L + 5` (self-cast; on an ally it's `(L_caster + L_victim)/2 + 5`),
`+6` with Regen-spec, and `duration = healing_level · FAST_UPDATE_RATE / 2 = healing_level · 10`
updates (`mystic.cpp:774`/`:808`). The drain and the boost use the **same `modifier`**, so each
converts at exactly **1:1**. Because the drained side is subtracted from regen, a converter is
only a *net* gain if you have the other bar to spare — Restlessness can even push HP regen **negative
and kill you** (§7). Neither can be refreshed while active (recast prints "could not improve…").

> **Both at once cancel out.** The bonuses are summed before the perception multiplier, so with both
> active the net is `(Curing.mod − Restlessness.mod)·percep/100` on HP and the negation of that on move.
> Cast by the **same mystic on the same target with the same spec**, the two modifiers are equal and
> the effects **cancel exactly on both channels** — you get only base/gear regen, having spent spirit
> and two dispellable slots for nothing. They diverge (and stop cancelling) with different
> casters/specs/self-vs-other, the ±1 random WIL rounding in `get_mystic_caster_level`, or once the
> shorter-lived one expires. The pair is pointless together; the value is choosing the *one* direction
> to convert.

**Mechanism B — front-loaded (`duration`-based): Regeneration & Vitality.** Here the bonus is
**`affect->duration · 6 / FAST_UPDATE_RATE`** (`limits.cpp:192` for Regeneration→HP, `:266` for
Vitality→move). The key: both spells are flagged **`is_fast`** (`consts.cpp`), so `affect_update`
decrements their `duration` **every update** (every 3 s), and within the same pulse `fast_update`
runs *first* and reads the **current** duration (`comm.cpp:831-834`). So each update the bonus is
proportional to the *remaining* duration:

```
per-update bonus = (duration · 6 / 20) / 20 · perception/100  =  0.015 · duration · (perception/100)
```

Duration starts at `D`, ticks `D → D−1 → … → 1 → 0` (one per update), and the affect is removed when
it hits 0. **This is the front-loading you observed, and it's exactly linear:** the bonus is highest
on the first tick and declines in a straight line to ~0 at expiry. Consequences:
- **Peak (first-tick) bonus = `0.015 · D · perception/100` per update**; the **average is half the
  peak**; and the **total HP healed = `0.015 · perception/100 · D(D+1)/2 ≈ 0.0075 · D²`** — i.e.
  total healing grows with the **square** of duration (and duration grows with level, so total
  healing is ~quadratic in mystic level).
- **Refresh window:** recasting only takes effect once current duration has fallen **below half** of
  a fresh cast (`mystic.cpp:966`/`:869`); otherwise "still regenerating fast enough." So the optimal
  pattern is to let it decay to ~50 % then re-up — keeping you in the strong upper half of the curve.

Durations from the spell functions (self-cast, `L` = mystic caster level §1; integer division):

| Power | level term | duration `D` (updates) | bonus channel |
|---|---|--:|---|
| **Regeneration** (`mystic.cpp:948`) | `RL = L − 10` (+6 spec) | `(RL / 2) · 20` | HP, front-loaded |
| **Vitality** (`mystic.cpp:852`) | `VL = L` (+6 spec) | `(VL / 3) · 20` | move, front-loaded |

(Regeneration does nothing until `L > 10`, and unlike Curing it does **not** average in the target's
level — it uses the caster's `L` only, as does Vitality.)

#### Healing per update — 24t / 30t / 36t, at perception 100

**Assumptions:** WIL ability score 22 (→ `L = t + WIL/5 = t + 4`, the doc's house mystic; §1), so
`L = 28 / 34 / 40`; **perception 100** (multiply every number by `your_perception/100`); self-cast;
base/CON regen *excluded* (these are the spell bonus only). "+spec" = Regeneration specialization
(+6 to the level term). One update = 3 s.

**Regeneration (HP) and Vitality (move)** — *peak* (first-tick) bonus; it tapers linearly to 0, so
the **average is ~half** the peak and the **total** is the lifetime sum:

| | 24t (L28) | 30t (L34) | 36t (L40) |
|---|---|---|---|
| **Regeneration** peak HP/upd · dur · total | 2.7 · 9 min · ~244 | 3.6 · 12 min · ~434 | 4.5 · 15 min · ~677 |
| **Regeneration +spec** | 3.6 · 12 min · ~434 | 4.5 · 15 min · ~677 | 5.4 · 18 min · ~975 |
| **Vitality** peak mv/upd · dur · total | 2.7 · 9 min · ~244 | 3.3 · 11 min · ~365 | 3.9 · 13 min · ~509 |
| **Vitality +spec** | 3.3 · 11 min · ~365 | 3.9 · 13 min · ~509 | 4.5 · 15 min · ~677 |

**At the learn floor (15t).** Regeneration unlocks at 15 mystic, but `regen_level = L − 10` makes it
deliberately feeble right there. At 15t with the house WIL 22 (`L ≈ 19`): `regen_level = 9` →
duration `(9 / 2) · 20 = 80` updates = **4 minutes**, **peak 1.2 HP/update** tapering linearly to 0,
for a **total of ~49 HP** (at perception 100; a human 15-mystic's perception ~60 makes it ~29 HP,
~0.7 HP/update peak). Regeneration-spec (`regen_level = 15`) roughly **triples** it — 140 updates
≈ 7 min, ~148 HP at perception 100 — and a low-WIL splash (lower `L`) shaves it further. The `−10`
floor is why Regeneration ramps so hard above its learn level: by 24t (table above) the same spell
already totals ~244 HP, **~5× the 15t value**. So a 15-mystic splash gets a slow 4-minute trickle,
not the heavy front-loaded sustain a 24t+ mystic enjoys.

**Yardstick — what multiplier is that, really?** Take the common **regen-warrior archetype:
30 warrior / 15 ranger / 15 mystic, CON 16, WIL 15**, self-casting and running **Insight** so its
perception sits at the **100 cap** (human base `30 + cleric·2 = 60`, + Insight 50, clamped — §1).
Natural regen is `8 + CON/2 + warrior/3 + ranger/5 = 8 + 8 + 10 + 3 = 29 HP per MUD hour`
(`hit_gain`, `limits.cpp:200`; mystic levels don't feed HP regen, but the 15 ranger does), i.e.
**~116 HP over 4 minutes standing** (~145 resting). Regeneration at 15t / WIL 15 (`L = 18`,
`regen_level = 8`) lasts **exactly that 4-minute window** (80 updates) and, at 100 perception, adds
the **full ~49 HP** front-loaded on top:

| Position | Natural / 4 min | + Regen (100 percep) | Multiplier |
|---|--:|--:|--:|
| **Standing** | ~116 HP | +~49 → **~165 HP** | **~1.42×** |
| **Resting** (×1.25) | ~145 HP | +~49 → ~194 HP | ~1.34× |

Because the bonus is front-loaded it runs **~1.8× in the first updates** and tapers to **1.0×** by the
4-minute mark (the ~1.42× is the whole-window average; the regen `+49` is added *after* the position
multiplier, so resting boosts only the natural side). The multiplier is gated by the **recipient's**
perception (`get_bonus_hit_gain` reads the *regenerating* character's perception, not the caster's):
this archetype leans on **Insight to cap perception at 100** and bank the full +49 HP — a target
without it (e.g. a pure warrior at ~30 perception) keeps only ~30 % of the bonus (~15 HP, ~1.14×). So
even maxed, learn-floor Regeneration is a **~+40 % standing-heal** top-up, not a panic button; its
heavy sustain comes later as `regen_level = L − 10` climbs with mystic level.

**Curing Saturation (+HP / −move) and Restlessness (+move / −HP)** — *flat* (constant every update for
the whole duration); `HL = L + 5`:

| | 24t (HL33) | 30t (HL39) | 36t (HL45) |
|---|---|---|---|
| **Curing / Restlessness** rate/upd · dur · total | 1.65 · 16.5 min · ~545 | 1.95 · 19.5 min · ~760 | 2.25 · 22.5 min · ~1012 |
| **+spec** (HL = L+11 → 39/45/51) | 1.95 · 19.5 min · ~760 | 2.25 · 22.5 min · ~1012 | 2.55 · 25.5 min · ~1300 |

(The "total" for the converters is also the amount drained from the *other* bar — 545 HP gained costs
545 move at 24t, etc.)

**Reading it.** At perception 100, **Regeneration is the heaviest single-target HP power** — it opens
at 3.6 HP/update (1.2 HP/s) at 30t and rides for 12 minutes, totalling ~434 HP free (no resource
drained), and Regen-spec bumps every cell up one level-tier. Vitality is the move-side equivalent.
The two flat converters out-*total* them on paper (~760 HP at 30t) but only by **spending an equal
amount of the opposite bar**, and they pay it out as a low, steady trickle rather than a front-loaded
burst — they're stamina/HP management tools, not net healing. Multiply everything by your real
perception: a human 30-mystic (perception 90) actually sees ~90 % of these figures, a 24-mystic
(perception 78) ~78 %, and any mystic at/above 100 perception gets the full value.

### 4.2 Disorientation powers — Hallucinate, Haze, Confuse

The three Illusion-school disablers degrade what the target *can do* rather than dealing damage.
They split across **two** save families (§2): **Hallucinate & Confuse** roll `saves_confuse` (a
willpower contest), while **Haze** rolls `saves_mystic` (**pure perception** — no willpower, so
high-perception targets are nearly immune). None stacks with itself (each checks
`affected_by_spell` first).

#### Hallucinate (id 63, lvl 3, 2 spirit, `spell_hallucinate`, `mystic.cpp:1033`)
Makes the **target's own physical attacks miss** ("hit thin air"). It does **not** wind down on a
timer in practice — it's a **charge budget**:
```
charges (modifier) = cleric/10 + 2  (+1 if cleric > 30t)  (+1 Illusion-spec)   # ~5 at 30t, 6–7 higher/spec
duration = charges · 4              # slow-tick backstop only; the swings end it first
```
The budget uses the caster's **cleric prof level only** — WIL / `L` don't scale it. On each
**physical** swing by the hallucinating target (`check_hallucinate`, `fight.cpp:2846`, gated for
physical attacks at `fight.cpp:1626`, and also for archery/offense), it rolls
`number(1,100) > 100/(charges + 1)`:
- **pass** (the common case) → *"You hit thin air!"*, the swing **misses**, one charge is burned; at
  0 charges the effect clears.
- **fail** → the swing **connects** and the effect is **removed immediately**.

So the per-swing whiff chance is `charges/(charges + 1)` — **~83 % at 5 charges**, easing to 50 % on
the last charge as a breakthrough gets likelier. Net: it reliably eats **~3–5 of the target's
attacks** before fading. Save: `saves_confuse` (a resisted cast does nothing); the cast also fires a
0-damage `damage()` to engage. **Strong against high-attack-rate melee/archers, useless against a
caster** (only physical attacks are checked).

#### Haze (id 52, lvl 5, 1 spirit, `spell_haze`, `mystic.cpp:1077`)
`AFF_HAZE` is dizziness with two **flat** effects (its stored `modifier` doesn't scale them):
- **Movement** (`act_move.cpp:654`): each move command has a **25 %** chance (`number(1,4)==1`) to send
  you a **random** direction instead — *"You feel dizzy, and move randomly."*
- **Command targeting** (`interpre.cpp:1321`): **10 %** chance (`number(0,9)==0`) to **forget your
  command's target(s)** that round.

Two cast modes:
- **Single-target** (a victim, or your current fight target): `duration = number(0,1)` and haze is
  **not `is_fast`**, so it decrements only on the ~60 s slow affect cycle — a **brief, RNG** effect
  (seconds up to ~2 minutes). `+6` caster level if Illusion-spec (level only feeds the unused char
  `modifier` here).
- **Room** (cast `haze` with no target while not fighting): lays a room affect lasting **`level/3`
  slow-ticks (≈ `level/3` minutes)** at strength `level/2`, disorienting everyone in the room —
  *"You breathe out a disorienting mist."*

Save: `saves_mystic` → victim resists when `number(0,100) ≤ perception·9/10` (§2). Because it's a
**pure perception** roll, a High-elf or Insight-buffed target shrugs it off almost every time.

#### Confuse (id 111, lvl 1, 10 spirit, **Illusion spec-gated**, `spell_confuse`, `mystic.cpp:1443`)
A heavy, **front-loaded** skill/knowledge debuff. Sets `AFF_CONFUSE`, `duration = 10 + L`, and is
flagged **`is_fast`** so duration ticks down every **~3 s**. Its stored `modifier` (1) is a dummy —
the real strength is computed live from the **remaining duration**:
```
confuse penalty = 2 · duration − 10        # get_confuse_modifier
```
That penalty is subtracted from **both `get_skill` and `get_knowledge`** (`char_utils.cpp:886`/`:915`)
— i.e. essentially **every skill check, every casting-knowledge check, and combat OB/parry/dodge**
(the live `utility.cpp` `get_real_OB`/`parry`/`dodge` dock `2/3 ·` the penalty, `utility.cpp:651`+).
Because the penalty is `2·duration − 10` and duration falls 1/update, it is strongly **front-loaded**:
- **Peak at cast = `2·(10+L) − 10 = 2L + 10`** — e.g. **~78 at `L≈34`**, which all but **zeroes out**
  skills/knowledge (base knowledge is ~80) — then drops **2 per ~3 s** to nothing once `duration ≤ 5`.
- So the affect *lingers* for `10+L` updates (minutes), but its bite is gone in the final ~5 ticks.
- **Concentration clears it fast:** a confused character running `concentrate` (`AFF_CONCENTRATION`,
  §3) sheds **3 extra duration/update** while `duration ≥ 10` (`limits.cpp:1293`) — ~4× the normal
  decay, so a focused mystic shakes the fog quickly.

Save: `saves_confuse` (willpower, §2). Cast at `POSITION_FIGHTING` (mid-combat); at **10 spirit** it's
the priciest of the three. Best against **skill-dependent** opponents — a warrior's OB/parry or a
rival mystic's knowledge — but cheap to counter by concentrating. **Known trade-off:** for the last
few ticks `duration ∈ 1–4` makes the penalty *negative* (`2·dur−10` = −8..−2), so as confuse fades it
briefly hands the victim a small skill/knowledge/OB **bonus**. This is **intentional** — a built-in
**risk of confusing a target** (the tail end can help them), not a bug; don't clamp it.

**Quick contrast:** Hallucinate = a few guaranteed enemy whiffs (vs melee, willpower save);
Haze = cheap chip disorientation on movement/commands (perception save, immune-prone); Confuse = a
big up-front skill/OB crush that decays and is concentration-cleared (willpower save, spec-gated,
costly).

---

## 5. Scaling: specialization, mystic level, willpower

**Mystic level (`prof_level(CLERIC)`)** is the master lever: it is the bulk of `L` (durations &
magnitudes), the Mind-Block duration (`15+2·cleric`), the `concentrate` will bonus (`cleric/2`),
the Hallucinate charge count, the resist-magic modifier (`L/6`), and how much spirit you draw from
kills (perception-weighted but level-driven).

**Willpower** works on two layers (§1). The **WIL ability score** (20–25; 25 very high) is a minor
input to caster level `L` (only `WIL/5`, so ~+4–5 levels). The **derived willpower**
`Wp = cleric+WIL` (~50–65) is the heavy hitter: it's the squared offense *and* defense of every
`saves_power` exchange (§3) and the offense/defense term in `saves_confuse` / `saves_poison` /
`saves_insight`. Because `Wp` already contains your whole cleric level, **+10 willpower gear is a
~15–20 % swing** on it, not a doubling — meaningful but not transformative; a high-WIL mystic both
attacks minds harder and resists them.

**Perception** is the enabler: `race_baseline + cleric·2` (§1), so it scales hard with mystic level
on top of a per-race floor (High-elf 100 → Dwarf 0). It powers the reach check (`Percep·Percep` vs
`number(1,10000)`), `saves_mystic`/`saves_insight`, and **scales curse & concentrate output** by
`Percep_victim/100`. Crucially it is the **sole defense against fear/haze/terror** (`saves_mystic`
is pure perception — no willpower), so a high-perception race like the High-elf is effectively
immune to those. **Insight** (+50) and **Pragmatism** (−50) are the only willpower-mediated levers
that move perception, which is how willpower indirectly touches fear resistance.

**Specializations** (enum `structs.h:811`; framework in [specializations.md](specializations.md)):
- **Regeneration** (`PS_Regeneration` / `PLRSPEC_REGN`): **+6** healing level on Curing,
  Restlessness, Vitality, Regeneration (and +5/+10 on the mage cure/vitalize self). Owns the
  `refresh all` spec power.
- **Protection** (`PS_Protection` / `PLRSPEC_PROT`): **+1** Resist-Magic modifier; the
  **Protection** power is spec-gated. (Also a combat evasion role — specializations.md.)
- **Illusion** (`PS_Illusion` / `PLRSPEC_ILLU`): **+6 `L`** on Haze, Fear, Terror and **+1**
  Hallucinate charge; **Confuse** is spec-gated.
- **Guardian** (`PS_Guardian` / `PLRSPEC_GRDN`): the **Guardian** power is spec-gated. It summons
  a charmed pet in one of three builds, all scaling with mystic level (`mystic.cpp:1491-1576`):
  **aggressive** (`OB = 13·L/5`, +STR/−WIL, high damage), **defensive** (`parry = 8+2L`,
  `dodge = 8+L`, tanky HP `~22·L/3`), or **mystic** (high WIL, no OB/parry, caster-like). HP
  `= base·(L + number(−3,3))/3`.
- *Teleportation/Fire/Cold/etc. are **mage** specs* (magic-system §3/§8), not mystic.

---

## 6. Worked example — a 30-mystic `will`-attacks an equal player

Caster: **human** 30 mystic, **WIL score 22**, **+10 willpower gear**. So `L = 30 + 22/5 ≈ 34`,
**`Wp_caster = 30 + 22 + 10 = 62`** (the §1 derived stat), and perception `= 30 + 30·2 = 90`.
Target: a similar human 30-mystic with **no willpower gear** → `Wp_victim = 30 + 22 = 52`,
perception 90.

- **Reach:** `Percep_c · Percep_v = 90·90 = 8100` vs `number(1,10000)` — connects ~81 % of attempts
  (perception is a real gate against players; most mobs can't be read at all). Note the victim's
  perception is a *factor in the product*, so it cuts both ways: a High-elf target (perception 160)
  gives `90·160 = 14400`, which **always** clears the roll — they're **always reachable** for
  will/curse, yet simultaneously **immune to fear/haze/terror** (that's the separate `saves_mystic`
  roll on their own perception). High perception is great defense against *effects*, but makes your
  mind trivially *reachable* for stat attrition.
- **Strike (`will`):** two `saves_power` rolls, each the victim resists when
  `number(0, 52²=2704) > number(0, 62²=3844)` → resist chance `2704/(2·3844) ≈ 35 %`, i.e. the
  attacker **lands ~65 % per roll**, so `damg` averages ~1.3 per `will`. **Concentrating first**
  raises power to `62 + 30/2 = 77` (`77²=5929`) → resist drops to ~23 %, ~1.5 dmg/will, and grants
  the same `+15` to your own defense while held.
- **Curse instead:** `count = (L + 20)·Percep_v/100/10 = 54·70/100/10 ≈ 3` stat-points scattered
  across the six stats (spending spirit each) — a burst that bypasses `will`'s two-roll cap.
- **Lethality:** ability scores are only **~20–25**, and **any one stat hitting 0 is an instant
  kill** — but `will` damages a *random* stat ~1–2 at a time, so spread damage rarely focuses one
  stat down fast. The path to a kill is **curse/concentrate to pile damage on**, and a victim with
  any stat dropping toward `MIN_SAFE_STAT = 3` flees. Mind Block can hard-stop 20 % of the
  mental-stat hits along the way.

---

## 7. Open questions / flags

- **Spirit has no passive regen — intentional (confirmed).** It's a "spend time to acquire, bank a
  large pool for when you need it" resource, refilled only by participating in kills. This system
  is acknowledged as likely needing a redesign, but that's a **design decision, not a bug** — do
  not change it autonomously.
- **Two "will" quantities** (§1): the WIL ability score (`GET_WILL`, ~20–25) feeds only caster
  level, while the derived `GET_WILLPOWER` (`cleric+WIL`, ~50–65) drives all saves and mental
  combat. The shared word is a footgun (mirror of the mage `saving_throw` collision, magic-system
  §5); a rewrite should rename one (e.g. `wil_score` vs `mental_power`).
- **`get_mystic_caster_level` uses `WIL/5` once**, unlike `get_magic_power` which double-counts INT
  (magic-system §1) — asymmetry between the two caster-level functions; intentional?
- **Restlessness can be lethal** via negative regen (`limits.cpp:1508` notes "characters can die to
  negative regen") — verify the dispel/refresh paths can't strand a victim at lethal negative move
  regen.
- **Confuse's end-of-duration negative penalty is intentional (confirmed).** `get_confuse_modifier =
  2·duration − 10` goes negative for the last few ticks (`duration` 1–4 → −8..−2), so as confuse fades
  it briefly grants the victim a small skill/knowledge/OB **bonus** (§4.2). This is a deliberate,
  **known risk of confusing a target** — a built-in downside, not a bug. **Do not clamp it.**
- **`spell_death_ward` ANTI path removes `SPELL_INSIGHT`** (`mystic.cpp:1421`, even tagged
  "What the fuck?") — almost certainly a copy-paste bug (should remove Death Ward).
- **`spell_protection` leaves a `fprintf(stderr, ...)` debug line** (`mystic.cpp:1736`) — strip.
- **`saves_poison` `magus_save` is hard-coded 0** in `spell_poison` (`mystic.cpp:1219,1254`), so
  `number(0,0) < 50` is always true — the intended Magus poison resistance is a no-op.
- Powers flagged for removal in code comments: **Shift** (immortal-only), and the room
  Haze/Poison variants overlap mage versions.
