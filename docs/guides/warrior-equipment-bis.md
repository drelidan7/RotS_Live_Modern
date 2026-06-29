# Warrior equipment — best-in-slot by specialization (PvP & PvE)

A quantitative best-in-slot guide for every **warrior specialization**, built by evaluating the
**actual** weapon/armor/shield data in `lib/world/obj` against the **live** combat formulas. Numbers
come from a faithful re-implementation of the engine (OB/PB/DB, the damage equation, `armor_absorb`,
attack speed, all spec procs) — see [Methodology](#methodology--how-the-numbers-are-produced) and the
committed calculator in [`tools/`](tools/).

> **What this is.** A decision aid: which weapon, armor, shield, and accessories each warrior spec
> wants, *why* (how spec × stats × weapon coefficients × armor interact), and *how much* it matters
> (computed OB, damage-per-second, time-to-kill, time-to-die). Item picks are drawn from the live
> world; **artifact-tier** and **builder/test** items are flagged separately.

> **Source of truth.** Combat math: `utility.cpp` (`get_real_OB:647`, `get_real_parry:761`,
> `get_real_dodge:860`, `get_weapon_damage:426`, `armor_absorb:540`), `fight.cpp` (`hit` damage
> `:2509-2539`, `armor_effect:2150`, `check_find_weakness:2051`, `defender_effect:2261`,
> `can_double_hit:2634`), `profs.cpp` (`recalc_abilities:716`, `class_HP:136`), `char_utils.cpp`
> (encumbrance `:500-814`, `get_bal_strength:415`), `weapon_master_handler.cpp`,
> `wild_fighting_handler.cpp`, `handler.cpp:1318-1345` (equip). See also
> [weapons.md](../systems/weapons.md), [specializations.md](../systems/specializations.md),
> [stats-and-character-power.md](../systems/stats-and-character-power.md),
> [combat-loop.md](../systems/combat-loop.md).

---

## TL;DR

| Spec | Wants | Best realistic weapon | Armor | PvP role | PvE role |
|------|-------|-----------------------|-------|----------|----------|
| **Heavy Fighting (2H)** | heaviest weapon + heaviest armor | `#5226` ice-encrusted battleaxe (2H cleave, d11.4/OB+15) | full plate | top bruiser DPS | fastest boss kills among durable specs |
| **Heavy Fighting (1H+shield)** | bulk-3 1H + shield + heavy armor | `#5044` gleaming broadsword + `#26806` numenorean shield (+HIT 100) | full plate | durable bruiser | best survivability/DPS balance |
| **Wild Fighting** | any big weapon, Berserk | `#5226` ice battleaxe (2H) | plate (offense from HP, not gear) | highest burst, glass cannon | fastest kills, dies fastest |
| **Light Fighting** | light 1H (bulk ≤ 2), minimal armor, high DEX + ranger | `#5410` quicksilver rapier (d5.7) / `#5425` twilight dagger | leather | evasive duelist | glassy; avoid hard hitters |
| **Weapon Master** | the *right* weapon per target | `#5226` cleave (burst) / `#5410` pierce (vs armor) / `#5346` smite | plate | flexible technician | matches Wild vs the right target |
| **Defender** | shield + warrior/ranger levels | `#5044` broadsword + `#26806` numenorean shield (+HIT 100) | full plate | shield-wall tank/peeler | longest survival, slow kills |

Three rules fall out of the math and hold for **every** warrior:
1. **OB *multiplies* damage** (`dam ∝ remaining_OB + 100`), so out-OB'ing the target both lands more
   swings and makes each bigger. Stacking weapon damage without OB (or vice-versa) is wrong.
2. **Speed is damage.** `DPS = damage-per-swing × ENE_regen/5`. A 2H doubles `str_speed`; a light
   weapon adds the DEX speed-blend; Light Fighting adds a free ~20 % double-strike.
3. **The defensive split is parry (warrior/STR/weapon) vs dodge (DEX/ranger), with armor a third
   layer.** Heavy weapons and heavy armor sacrifice dodge; shields buy dodge+parry (not absorb).

---

## Methodology — how the numbers are produced

A Python re-implementation of the live formulas (committed under [`tools/`](tools/):
`wbis.py` = parser + combat engine, `warrior_bis.py` = archetype analysis, `mine.py` = item survey,
`analysis_output.txt` = raw run). It parses every object/mob from `lib/world/{obj,mob}` and computes
each archetype's stats and a **per-swing Monte-Carlo** of expected damage (the OB roll, dodge→parry
subtraction, crits, find-weakness, rush, double-strike, and armor-by-hit-location are all sampled).

**Validation.** The engine reproduces the worked examples in
[combat-stat-examples.md](../systems/combat-stat-examples.md): for a default W30/R15, STR 20 it
returns `ob_bonus = 110` (exact), `OB ≈ 124`, `DB = 30`, `class_HP = 3826` (exact), `ENE_regen = 156`
(exact). The small OB/PB deltas vs the doc are the source applying two terms the doc's hand-calc
omitted (the wielded weapon's own bulk-encumbrance OB penalty, and the flat weapon `value[1]` parry).

**Assumptions (stated so you can adjust them):**
- **Character level 30** — the OB level-term and HP mini-level term saturate at 30.
- **All weapon skills 100 %, fast-attack 100 %, plus parry/two-handed/dodge/extra-damage maxed**
  (per the exercise's premise). This removes weapon-skill as a differentiator: weapon choice is purely
  the item's `value[0]`/`value[1]`/bulk/type/level and your spec.
- **Race-agnostic baseline = Human** (no stat mods); racial synergies are a [separate section](#racial-synergies).
- **Class combos are profession (class) levels**, where `class_level = 3·√(points)` and points sum to
  ~150 — so `36w 6r` = warrior-class 36 / ranger-class 6.
- **Stat arrays** (Human, optimized per spec, stated per section): STR specs STR 22 / CON 20 / DEX 14;
  Light DEX 22 / STR 18 / CON 18; Defender CON 22 / STR 20 / DEX 14.
- **PvP target** = a solid (not BiS) reference duelist: W33/R15, STR 22, chainmail + golden shield +
  broadsword, Normal — `OB 135 / PB 90 / DB 30 / HP 458`.
- **PvE targets** = real named bosses parsed from the world: **the Kraken** (`#7706`, L30, HP 2300,
  dam 30), **the pale lady** (a tall pale-faced woman, `#15302`, L67, HP 5000, dam 32), **Nargul the
  balrog** (`#1814`, L40, OB 250, HP 3500), **a cold-drake** (`#2757`, L35, HP 5000, dam 27), **the
  snow-troll king** (`#25604`, L48, HP 3500). (Super-bosses like Runyavath `#20811` L94 / "Fluffy"
  `#25902` L90 exist but are off-scale — group content.)

**Data snapshot.** This pass was re-run against the expanded item library (2026-06): **280 weapons,
~885 armor/worn, 64 shields across 156 obj files** (up from 222 / 591 / 47). The tools read
`lib/world/{obj,mob}` directly, so re-running them regenerates every number here. Builder/test items
(`pristine *` at weight 10 000–30 000, `golem` plate at weight 60 000, etc.) are excluded.

**Model limits (be honest about these):** numbers are **average-case, relative** — not exact in-game
readouts. Parry decay (to ⅔ after the first parry in a round) is approximated as ~0.8 average
effectiveness; armor is averaged over a humanoid hit-location distribution; procs are expected-value;
PvE TTD assumes solo and no healing (these bosses are group content — TTD just ranks relative
squishiness). DPS/TTK/TTD are for **comparison between builds**, not absolute combat logs.

---

## The gear landscape (what actually exists)

Mining `lib/world/obj` (realistic pool, test/immortal items excluded): **280 weapons, ~885
armor/worn pieces, 64 shields** (156 obj files).

### Weapons — the per-type leaders (damage rating shown as in-game `identify`, ÷10)

| Type (skill) | Top damage | Top OB | Notes |
|---|---|---|---|
| **2H cleaving** (Axe) | `#5224` halberd / `#5227` **d12.6** bulk7; `#5226` ice battleaxe d11.4/**OB+15** | `#5221` silver battleaxe OB+17; `#5226` OB+15 | **`#5226` still the best 2H overall** — its OB+15 multiplies damage past the higher-d rivals |
| **2H bludgeon** (Conc.) | `#5320` Durin's Sceptre d11.7 bulk8; `#5370` jagged cudgel d11.4 | `#1512` dark warhammer OB+10 | |
| **2H slashing** (Slash) | `#5104`/`#5106` ornate d10.9 bulk7; `#5108` gilded d10.6/OB+12 | `#8121` **blade of essence OB+25** (d7.8) | OB+25 is huge but its low d nets less DPS |
| **Smite** (Conc.) | `#27425` **The Mighty Grond d15.5** bulk8 **wt5000** | `#27425` OB+13; `#5346` maul OB+12 | highest raw damage in the game, but wt5000 ⇒ ~half speed, so it *loses* on DPS; smite shines only vs metal armor |
| **Flail** (Whip) | `#5611` iridescent flail d11.3 bulk7 | `#7031` bramblethorn OB+17 | no 2H *type*, but bulk≥4 grips 2H |
| **Spear** (Spears) | `#5514` enruned mithril spear **d10.1 / OB+12** bulk6 | `#5520` gilded double-bladed OB+15 | **÷150 armor pen** (anti-armor) |
| **1H slashing** (Slash) | `#5059` basket-hilted **d8.5** bulk3; `#5033` obsidian d7.6/**OB+10/pa+4**; `#5044` broadsword d7.6/OB+4/pa+4 (wt240) | `#10021` serrated scimitar OB+14 | bulk-3 1H = heavy-fighting legal; `#5044` still edges it on OB+speed |
| **1H bludgeon/smite** | `#5306` dark runed mace d8.0/**OB+12** bulk3; `#5346` steel maul d9.0/OB+12 (smite) | | smite shatters metal armor |
| **Piercing** (Pierce) | `#5410` quicksilver rapier **d5.7/OB+5/pa+5 bulk2**; `#5425` twilight dagger d5.4 bulk2 wt100 | `#5426` sickle OB+12 | the Light-Fighting weapons (`#5435` marble dagger nerfed to d4.5) |
| **Bow** (Archery) | `#2703` composite longbow OB+12; `#2706` dragon-horn bow | — | **bows now exist** (`#2700`–`#2706`) — the world previously shipped none; ranged/Archery cross-builds are now possible |

Damage is **derived** (`get_weapon_damage`): it rises with item level and bulk-near-3, and falls the
more OB/parry the weapon also grants — which is why the new Grond (d15.5 but wt5000) and blade-of-essence
(OB+25 but d7.8) both *lose* to the ice battleaxe: **`#5226`'s balanced d11.4 + OB+15 beats raw damage
or raw OB**, because OB multiplies damage *and* speed matters (Grond swings at ~half the rate).

### Armor — absorption scales with **level + encumbrance** (`armor_absorb`)

Heavy plate (`#6227` body abs62, `#6221` head abs67, `#6244` hands abs75, `#6256` legs abs59,
`#6246` feet abs61, `#6254` arms abs59) gives the most absorb but at enc 5–7 and 850–2200 weight each
— crushing dodge/speed for anyone **except a Heavy Fighter** (who soft-caps the weight/encumbrance).
Chain (`#11000` abs48 / `#11020` **light mithril mail abs60** enc3) and leather (`#11119` abs40 enc2,
lighter pieces enc0–1) trade absorb for mobility — the Light Fighter's kit. (The new expansion adds an
`abs96` `pristine`/`golem` plate set at weight 10 000–60 000 — clearly builder/test gear, excluded.)

### Shields — dodge + parry, **not** absorption

Shields are not a hit location, so they give **`value[0]`→dodge, `value[1]`→parry** (+ the Defender
block layer), never `armor_absorb`. **New best:** `#26806` **black numenorean shield (dodge15/parry15
+ HIT 100)** — same block stats as the golden shield but **+100 HP**, so it's now the BiS shield for
any survivability build. Others: `#6510` golden / `#6529` sable targe / `#33614` spectral
(dodge15/parry15, enc3); `#9080` heater (dodge8/**parry22**, enc5); `#6530` blackshield
(dodge10/parry20); `#32809` light leaf shield (dodge16/parry12, **wt250** — for mobile builds).

### Accessories — small OB sticks, and the artifact cliff

`A`-line stat gear is sparse and small: **waist** `#6040` rainbow belt **OB+8** (best), **about**
`#6316`/`#6338` werewolf fur / robe **OB+5**, **head** `#26815` thorned crown **OB+5/DR+2** (new — an
offense head if you trade away absorb), **hands** `#6263` spiked chain gloves OB+2, **wrist** `#6647`
bracelet OB+2, **neck** `#6649` amulet OB+1, **hold** `#6955` werewolf heart OB+2, **rings** `#6602`
ivory ring OB+2. **Damroll (`points.damage`, `base += damroll·10`) still barely exists on common gear**
(the new `#26815`/`#26813` heads add only DR+2) — the *only* big sources remain the artifact rings
**Vilya `#5065` (OB+150/DR+20)**, **Narya `#1610`** and **Nenya `#7936` (OB+100/DR+15 each)** — now a
trio. They're a separate [ceiling tier](#the-artifact-ceiling).

---

## Per-archetype best-in-slot

All sets below also wear the common OB accessories (rainbow belt `#6040`, werewolf fur `#6316`, ×2
red bracelet `#6647`, ×2 amulet `#6649`, werewolf heart `#6955`, ×2 ivory ring `#6602`) unless noted.
**PvP** is vs the reference duelist; **PvE** is vs the Kraken (full boss table
[below](#cross-archetype-comparison)). DPS/TTK/TTD in the engine's relative units / seconds.

### Heavy Fighting — the armored juggernaut (two valid builds)

Wears the **heaviest plate** at almost no penalty (worn weight over a per-slot cap counts ⅓;
over-cap encumbrance is discarded) and gets **+10 % armor absorb**, **+5 % damage with a heavy weapon**
(bulk≥3 & wt>235), and **+20 % on active skills**. STR build (STR 22 / CON 20 / DEX 14), 36w/6r.

**Armor (both variants):** plate `#6227` `#6221` `#6256` `#6246` `#6244` `#6254`.

| Build | Weapon (+shield) | OB | PB | DB | HP | spd | PvP dps / TTK / TTD | PvE (Kraken) TTK / TTD |
|---|---|---:|---:|---:|---:|---:|---|---|
| **2H** | `#5226` ice battleaxe (gripped 2H) | 174 | 42 | −7 | 469 | 13 | **6.6 / 69 / 164** | **177 / 57** |
| **1H+shield** | `#5044` broadsword + `#26806` numenorean | 159 | 95 | 16 | **569** | 28 | 4.5 / 101 / 321 | 236 / 95 |
| 1H+shield (parry) | `#5033` obsidian (OB+10/pa+4) + `#26806` | 159 | 95 | 15 | 569 | 27 | 4.4 / 104 / 323 | 248 / 94 |

**The headline trade:** the **2H deals ≈ 47 % more PvP damage and ≈ 34 % more PvE damage**, but the
**1H+shield survives nearly 2× longer** (TTD 321 vs 164 in PvP; 95 vs 57 vs bosses) thanks to **+53 PB
and +23 DB** from the shield and **+100 HP** from the new numenorean shield. The 2H's ×2 `str_speed`
isn't the story (it's still slow at sp13 — the axe is wt2500) — it wins on **OB+15 on the weapon, +6
from the 2H OB term, and ×2 STR inside the damage factor**. Pick **2H to delete things, 1H+shield to
anchor a line / duel attrition** — the new `#26806` shield (golden's block + 100 HP) widens the
survivability gap further. Avoid the smite-maul/heater combo generically (slow, and smite only helps
vs metal armor — which bosses don't wear). *(Grond `#27425` d15.5 is tempting but at wt5000 it swings
at ~half speed and nets less DPS than the ice battleaxe.)*

### Wild Fighting — the berserker glass cannon

Fights on STR and gets stronger as it dies: **rush** (+50 % damage at 5/10/15 % by tactics), rage
attack-speed below 45 % HP, bloodlust heal on kill. Wants **Berserk** (which guts parry/dodge). Same
STR build + plate as Heavy; weapon = `#5226` (2H) for the biggest rush procs.

| Build | OB | PB | DB | HP | PvP dps / TTK / TTD | PvE (Kraken) TTK / TTD |
|---|---:|---:|---:|---:|---|---|
| **Berserk 2H cleave** | 187 | 24 | **−25** | 469 | **7.3 / 62 / 117** | **161 / 42** |
| Normal 2H cleave | 157 | 42 | −30* | 469 | 5.9 / 77 / 123 | 195 / 45 |
| Berserk 1H broadsword | 176 | 53 | −12 | 469 | 5.5 / 84 / 141 | — |

(Berserk OB includes the maxed-`SKILL_BERSERK` `+ob_bonus/16 + 5 + berserk/8` bonus.)

Highest sustained DPS of any warrior (rush + Berserk OB), but **the lowest effective defense** (DB
gutted, can't flee) → it dies fastest of the bruisers. The comeback curve (rage/heal, not modeled
here) makes its *real* ceiling higher in fights it can finish. **Race: Olog-Hai** (frenzy *forces*
Berserk for free + crits + 10 % damage) is the natural shell. *(\*Normal DB negative because plate's
weight isn't soft-capped the way Heavy Fighting caps it — Wild pays full encumbrance.)*

### Light Fighting — the dexterity duelist

The only spec that abandons STR: with a **light 1H (bulk ≤ 2)** its offense stat becomes
`max(STR, DEX)`, it folds **+ranger/3 into OB**, shaves worn weight/encumbrance toward zero, and gets
a free **~20 % double-strike**. Build: **DEX 22 / STR 18 / CON 18, 30w/21r** (ranger pays double:
OB *and* dodge), **leather** armor.

| Weapon / kit | OB | PB | DB | HP | spd | PvP dps / TTK / TTD | PvE (Kraken) TTK / TTD |
|---|---:|---:|---:|---:|---:|---|---|
| `#5410` quicksilver rapier (d5.7/OB+5/pa+5 b2) + leather | 176 | 74 | **39** | 427 | 34 | **4.8 / 95 / 106** | 228 / 31 |
| `#5410` rapier + `#6200–6205` thin-metal **+dodge** kit | 172 | 74 | **40** | 427 | 34 | 4.6 / 99 / 99 | 232 / 30 |

Its DPS rivals Heavy-1H+shield while carrying **DB 39** (vs 16) — in PvP it *dodges* what the bruiser
*tanks*, and the fast light weapon + double-strike keep pressure on. The catch: **low HP + light
armor make it glassy in PvE** (TTD ~30 s vs the Kraken — worst of the roster) and a 2H/heavy weapon
*disables both* the DEX-OB substitution and the double-strike. **The rapier `#5410` (buffed to d5.7)
is now the clear light pick** — the old marble dagger `#5435` was nerfed to d4.5; the `#5425` twilight
dagger (d5.4 bulk2 wt100) is a faster, lighter alternative. The thin-metal **+dodge** kit trades a
little absorption for DB 40+.

### Weapon Master — the toolbox technician

Mastery of whatever you hold — pick the weapon for the **target**. All-on, no ramp. STR build + plate,
36w/6r.

| Weapon | What it brings | OB | spd | PvP dps | PvE (Kraken) TTK | Best against |
|---|---|---:|---:|---:|---:|---|
| `#5226` 2H cleave | +15 % dmg, 50 % reroll-higher | 157 | 13 | **6.9** | **169** | raw burst (matches Wild, no risk) |
| `#5346` smite maul | +10 OB, haze proc | 159 | 20 | 4.7 | — | metal-armored targets, control |
| `#5410` pierce rapier | +15 % speed, 25 % **ignore armor** | 149 | 37 | 3.9 | 266 | **heavily-armored** PvP targets |
| `#5514` spear | 50 % **double armor-pen** | 151 | 17 | 4.9 | — | armored targets |
| `#5044` 1H slash | +5 OB/+5 PB, energy refund | 152 | 28 | 4.3 | — | balanced/duelist |

Against the light-armored reference duelist, raw cleave wins; the pierce/spear armor-bypass shines
**when the enemy stacks absorb** (the table understates them vs a plate-wearing PvP opponent, where
ignoring armor is worth far more). WM is the highest-skill, most loadout-dependent pick: a master with
one weapon is a master in one situation.

### Defender — the shield wall

Survivability/peel: **+10 % HP**, shield **block** (passive `max(W,R)+min(W,R)/2` %, each block −30 %,
two rolls stack to −60 %), the only **delay-free rescue**, a cheaper/stronger maul. Needs an
`ITEM_SHIELD`. Build: **CON 22 / STR 20 / DEX 14**, plate, broadsword `#5044`.

| Shield | OB | PB | DB | HP | PvP dps / TTK / TTD | PvE (Kraken) TTK / TTD |
|---|---:|---:|---:|---:|---|---|
| `#26806` numenorean (d15/p15 **+HIT 100**) | 137 | 91 | 0 | **630** | 3.3 / 139 / **310** | 294 / **93** |
| `#6510` golden (d15/p15) | 137 | 91 | 0 | 530 | 3.3 / 137 / 258 | 298 / 77 |
| `#9080` heater (d8/p22) | 135 | 98 | −10 | 530 | 3.2 / 144 / 251 | — |
| `#6530` blackshield (d10/p20) | 136 | 96 | −6 | 530 | 3.3 / 139 / 251 | — |

Lowest personal damage, **best survival** (TTD 310 PvP; longest boss survival). The **new `#26806`
numenorean shield is the best all-round pick** — it matches the golden shield's block stats and adds
**+100 HP**, pushing Defender HP to 630 and TTD to 310. Defender's value is **group play** (rescue/peel);
solo it just out-lasts. Race: **Beorning** (can `brace` to use DEFEND without a shield; +20 innate dodge).

---

## Cross-archetype comparison

**PvP** (vs reference duelist; champion set of each spec):

| Archetype | OB | PB | DB | HP | spd | DPS | TTK (s) | TTD (s) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Wild (Berserk 2H) | 187 | 24 | −25 | 469 | 13 | **7.3** | **62** | 117 |
| Weapon Master (2H cleave) | 157 | 42 | −30 | 469 | 13 | 6.9 | 67 | 122 |
| Heavy (2H ice cleave) | 174 | 42 | −7 | 469 | 13 | 6.6 | 69 | 164 |
| Light (rapier, 30w21r) | 176 | 74 | **39** | 427 | 34 | 4.8 | 95 | 106 |
| Heavy (1H + numenorean shield) | 159 | 95 | 16 | **569** | 28 | 4.5 | 101 | 321 |
| Defender (numenorean shield) | 137 | 91 | 0 | **630** | 28 | 3.3 | 139 | **310** |

*The new numenorean shield (+HIT 100) now puts Heavy-1H and Defender clearly ahead on survival (TTD
321 / 310); Defender still pulls further ahead in group play (rescue/peel/critical-block variance).
Reading it: **2H specs win the DPS race; 1H+shield and Defender win the war of attrition; Light wins
by not being hit.**

**PvE — vs named bosses** (TTK = seconds to kill / TTD = seconds to die, solo, no heals):

| Archetype | Kraken | pale lady | Nargul (balrog) | cold-drake | snow-troll king |
|---|---|---|---|---|---|
| Heavy 2H (ice cleave) | 177 / 57 | 317 / 49 | 330 / 77 | 351 / 47 | 358 / 77 |
| Heavy 1H + numenorean shield | 236 / 95 | 418 / 87 | 474 / 123 | 468 / 80 | 540 / 143 |
| Wild (Berserk 2H) | **161 / 42** | **291 / 36** | 294 / 58 | 319 / 35 | 317 / 57 |
| Weapon Master (2H cleave) | 169 / 44 | 295 / 37 | 328 / 60 | 333 / 37 | 385 / 59 |
| Light (rapier) | 228 / 31 | 403 / 29 | 423 / 41 | 449 / 27 | 455 / 45 |
| Defender (numenorean) | 294 / 93 | 501 / 83 | 688 / 122 | 570 / 78 | 869 / 135 |

Same ordering as PvP: **Wild/WM/2H-Heavy kill fastest; Defender/1H-Heavy last longest; Light is the
glass.** TTDs are short because these are **group bosses** (OB 150–250, dam 21–32) — the table ranks
relative durability, not "can I solo a balrog" (you can't, durably). Against the pale lady and drakes
(HP 5000), even the fastest build needs ~5–6 minutes — bring friends.

---

## Spec matchups — does the intended rock-paper-scissors hold?

The design intent for the three core fighting specs was a cycle: **Light > Heavy > Wild > Light**
(Light evades and ripostes the armored Heavy; Heavy's armor outlasts the Wild glass cannon; Wild's
raw offense bursts the squishy Light). To test it I built a **mutual duel simulator**
([`tools/rps_duel.py`](tools/rps_duel.py)): time-stepped combat with the full energy loop (incl. Wild
rage attack-speed), the **armor-ignoring active skills** (kick / wild-swing, which are wait-priority
59 — see below — so they fire *on top of* auto-attacks), **riposte**, defender block, find-weakness,
the Light double-strike, and per-location armor. Each spec runs its BiS gear in its preferred stance
(Heavy plate Normal; Light leather Careful; Wild plate Berserk), 3,000 duels per matchup.

**Result: the intended cycle still does NOT hold as a clean stand-up cycle. For the *2H* bruiser it's
a power ladder `Wild ≳ Heavy(2H) > Light`. But the expansion's new `#26806` numenorean shield (+100 HP)
materially changes one leg — see the note below the table.**

| Matchup (A vs B) | A wins | B wins | intended | verdict |
|---|---:|---:|---|---|
| Light (careful) vs **Heavy** (2H, normal) | 0 % | **100 %** | Light > Heavy | ❌ **inverted** |
| **Wild** (berserk) vs Heavy (2H, normal) | **80 %** | 20 % | Heavy > Wild | ❌ inverted (2H) |
| Heavy (**1H + numenorean shield**) vs **Wild** (berserk) | **93 %** | 7 % | Heavy > Wild | ✅ **now holds (shield variant)** |
| **Wild** (berserk) vs Light (careful) | **100 %** | 0 % | Wild > Light | ✅ holds |

**The new shield flips Heavy-vs-Wild for the shield build.** With the golden shield this matchup was a
~33 % loss for Heavy-1H; the numenorean shield's **+100 HP** tips a knife-edge fight into a **~93 %
win** — so against Wild the *armored* answer is the 1H+shield bruiser, not the 2H. Wild still beats the
**2H** Heavy (80 %, since the 2H trades the shield's parry/HP for offense), and still **crushes Light**
(100 %). And **Heavy still beats Light** (100 %). So the picture is now: **Light loses to both; among
the bruisers, 1H+shield Heavy > Wild > 2H Heavy** — closer to the intended "Heavy outlasts the glass
cannon," but only once you give Heavy the new shield, and only for the shield variant.

### Why each intended leg breaks

**Light > Heavy fails (Heavy wins 100 %).** Two compounding reasons, both about the math the RPS
was supposed to exploit:
1. **Heavy's OB is too high to evade.** A 36-warrior STR-22 Heavy on the ice battleaxe rolls ~171
   effective OB; even a *maximally* evasive Wood-Elf Light (DB 54 / PB 108, defensive stance) only
   subtracts ~140 — so Heavy still lands at a ~+30 margin nearly every swing. You cannot dodge your
   way out.
2. **Plate neutralizes Light's offense.** Light's dagger does ~14 pre-armor, and plate absorbs
   60–75 % of it — Light simply can't punch through, while its own HP (427) + leather fold to Heavy's
   weapon **plus** Heavy's armor-ignoring kick.

Riposte and Light's own kick keep it *in* the fight but aren't enough. The decisive lever is the
**kick**, and the sensitivity test proves it:

| Variant | Light win % vs Heavy-2H |
|---|---:|
| both kick (baseline) | 0 % |
| **Heavy's kick disabled** | **≈ 50 %** ← becomes a coin-flip |
| Light's kick disabled | 0 % |
| Light's riposte disabled | 0 % (no change) |

So **Light's intended counter to armor (evasion + riposte) is real but insufficient; the thing that
actually decides Light-vs-Heavy is whose *kick* is bigger — and Heavy's is bigger** (Warrior 36 vs 30,
**plus Heavy Fighting's +20 % skill bonus**). The armor-bypassing skill that was supposed to let
*light* fighters answer armor is strongest in the *armored* spec's hands.

**Heavy(2H) > Wild fails (Wild wins ~80 %); Heavy(1H+shield) > Wild now *holds* (~93 %).** Heavy's
armor only mitigates **auto-attacks**, but Wild's damage largely routes *around* armor: **rush**
(+50 %) and **rage** attack-speed pump the auto-stream, and the **wild-swing** (kick ×1.5,
armor-ignoring) is a big chunk of its output. Worse for the intent, **Wild can wear the same plate** —
Berserk already throws away the dodge that plate would cost, so a Wild fighter pays *no meaningful
price* for full plate and gets its absorption for free. Against the **2H** Heavy that's enough: Wild
hits far harder (OB 187 + procs vs 174) and out-races the 2H's modest durability edge. **But the 1H +
numenorean shield Heavy is a different animal** — the shield's parry/dodge layer *plus* its +100 HP
(569 vs 469) is the durability the intent assumed, and it tips the knife-edge: Heavy-1H+shield outlasts
Wild's burst and wins ~93 %. So the intended "armor outlasts the glass cannon" is real — it just needs
the *shield* build and the new shield's HP, not the raw 2H.

**Wild > Light holds (100 %).** As intended: Berserk OB 187 + rush + rage-speed + armor-ignoring
wild-swing shreds Light's 427 HP in ~33 s, and Light's small, plate-absorbed damage can't punish
Wild's gutted defense fast enough to matter. Light's evasion delays but doesn't save it.

### How bash / kick / wild-swing factor in (the crux)

- **Kick & wild-swing are "free."** They set a 4–7 s recovery with **wait-priority 59**, and the
  energy loop explicitly exempts priority 59 (`fight.cpp:2742`: `... || GET_WAIT_PRIORITY==59`), so
  you **keep auto-attacking and gaining energy during the recovery** — the skill is pure bonus
  damage, not a trade. And it calls `damage()` directly, so it **ignores armor**. That makes it the
  game's main answer to heavy armor — but it **scales with Warrior level and OB and gets +20 % for
  Heavy Fighting**, so the *biggest* armor-bypass belongs to the *most armored* build. This single
  fact is why the cycle inverts at Light-vs-Heavy.
- **Wild-swing** (kick ×1.5, ×1.33 more when Berserk ≤ 25 % HP) is Wild's best button and a major
  reason it beats Heavy.
- **Bash** deals ~1 damage; its payload is the **knockdown** (`AFF_BASH`: target can't act ~4.5–6 s,
  and a below-fighting-position target hands attackers +10 OB per position step and enables rend). It
  is a powerful *lockdown/setup* that favors whoever lands it — again the higher-OB Heavy/Wild — and
  it **auto-fails while in frenzy** (so an Olog Wild can't bash). Not in the damage sim (≈0 dmg) but
  it would *widen*, not close, the gaps.

### What would restore the cycle (design levers)

The intent is recoverable, but needs the numbers to stop favoring the armored bruiser on every axis:
- **Stop the armored spec from also owning the armor-bypass** — e.g. cap kick/wild-swing scaling, or
  drop Heavy Fighting's +20 % *skill* bonus (keep its weapon/absorb bonuses), so kick is a *light*
  fighter's equalizer, not a heavy one's finisher.
- **Make heavy armor cost Wild something** — Berserk (or Wild Fighting) should forfeit some armor
  absorption, or Wild's full-plate encumbrance should bite OB/speed harder, so Wild can't be a
  glass cannon *and* a juggernaut.
- **Let Light actually evade Heavy** — scale dodge effectiveness against very high OB, or make
  riposte hurt (it's currently a small, ~25 %-on-parry dagger counter), so evasion+riposte can out-
  attrition plate.

### Caveat — this is a stand-up duel

The sim is two fighters standing toe-to-toe until one dies. It deliberately ignores the things that
are **Light's real edge** and that the RPS partly lived in: **kiting / disengage** (Light halves move
cost, re-hides, and can simply *refuse* a fight it can't win — a to-the-death sim scores that as a
Heavy "win" when in practice it's a stalemate Light controls), **bash-lockdown chains**, **Wild's
bloodlust heal** (matters across multiple kills, not a single duel), **frenzy** (an Olog Wild is
stronger still), terrain, and consumables. So read the verdict as: *in a pure stand-up fight the
intended cycle does not fully hold — Light still loses to both bruisers, so it's closer to a power
ladder than a cycle — though the expansion's new numenorean shield (+100 HP) does restore the
intended **Heavy(1H+shield) > Wild** leg. The mechanisms meant to create the cycle (armor-bypassing
skills, riposte) are real but still mis-weighted toward the armored specs, and Light remains the odd
one out.*

---

## Class-combo effects (`36w6r`, `33w15r`, `30w21r`, `27w`, `20r`)

The combos are **profession-level splits**; the spec decides which levels pay off.

**Light Fighting** (rapier + leather) — ranger buys dodge for ≈ free OB:

| Combo | OB | DB | HP | PvP dps / TTD |
|---|---:|---:|---:|---|
| 36w / 6r | 181 | 29 | 444 | 4.5 / 108 |
| 33w / 15r | 181 | 35 | 433 | 4.5 / 109 |
| **30w / 21r** | 179 | **39** | 427 | 4.2 / 110 |
| 27w / 9r | 169 | 31 | 366 | 3.9 / 84 |
| 20w / 9r | 158 | 31 | 303 | 3.5 / 65 |

OB barely moves from 36w→30w (the **+ranger/3 OB** offsets the lost warrior levels), but **DB climbs
+10** and survivability via dodge improves — so **30w/21r is the Light sweet spot**. Dropping total
level (27w, 20r) tanks HP and everything else: Light wants **high total level + heavy ranger**.

**Defender** — block keys off `min(W,R)`, so ranger is pure profit:

| Combo | PB | DB | HP | block-relevant |
|---|---:|---:|---:|---|
| 36w / 6r | 92 | −6 | **543** | +3 % block (min 6) |
| 33w / 15r | 89 | 0 | 530 | +7.5 % block |
| **30w / 21r** | 86 | 4 | 523 | **+10.5 % block + most dodge** |

For warrior-level-hungry specs (**Heavy / Wild / Weapon Master**) the ranger levels are nearly inert —
take **36w/6r** for max OB, HP, and find-weakness (which *steepens* above warrior level 30:
26 %→33 %→39 %→45 % at W24/30/33/36). `27w`/`20r` builds are warriors only incidentally — fine for a
hybrid that spent its points on casting, but strictly weaker as a pure melee.

---

## Racial synergies

Baseline is race-agnostic (Human). Where the math makes a race clearly better, the deltas (all on the
Heavy 2H ice-battleaxe set, 36w/6r, vs the Kraken) are:

### Light side — Human · Dwarf · Wood/High Elf · Hobbit
The "fair" pool: no class maluses, STR cap 22, alignment can go full-good. **Human** is the flexible
baseline (`OB 174 / HP 469 / PvP dps 6.7 / Kraken 177-57`). **Dwarf (+2 STR, +4 CON, −3 DEX)** is the
shaped 2H/Heavy body: the DEX−3 barely dents a high-bulk two-hander's speed, the **+4 CON** adds HP,
and — key synergy — **a Dwarf wielding an axe gains energy-regen** (`profs.cpp:791`), and *the best 2H
in the game (`#5226`) is an axe*, so a Dwarf swings the top weapon faster than anyone (use the ice
battleaxe, not the weaker mithril greataxe). **Wood/High Elf** (DEX +2, lower STR/CON) lean
**Light Fighting** (DEX-OB, dodge) and bring **moonvision**; their low CON hurts a frontline. **Hobbit**
(DEX +2, CON +2, **STR cap 20**) is a natural Light Fighter — its STR cap is irrelevant once you fight
on DEX, and it brings the game's best anti-magic save. *Light-side, pick by build: Dwarf for
Heavy/2H-axe, Elf/Hobbit for Light.*

### Dark side — Uruk-Hai · Orc
Both take the **Power-of-Arda daylight penalty** — in sunlight an Uruk's OB/PB/DB are ×0.8/0.9/0.9 − sun,
an Orc's ×0.75/0.89/0.89 − sun. **At night / indoors they fight at full** (an Uruk equals a Human:
`OB 174 / dps 6.7`), but a daytime field fight is a real handicap — dark-side warriors want to pick
their ground. **Orc** is additionally the most progression-penalized race: **`max_race_prof_level` is
20 (not 30)** so its OB level-term and find-weakness ceiling are lower, and **`class_HP` is ×4/7** →
much less HP (`HP 370 vs 469`, `dps 5.7`, Kraken `202/44`). Orcs pay for it with cheap pets/recruits
and the spec-agnostic class-point system — but as a *pure melee* they're the weakest race. Uruk-Hai is
the better dark-side warrior by far (only the Mage prof is penalized for them).

### Olog-Hai — its own category
The Olog is *not* "a bigger orc." **STR +4 / CON +4 but INT/WIL/DEX −3/−4/−3.** Critically, **the
STR +4 is wasted at the cap** (`bal_str` tops at 22) — if you're already building STR 22, the racial
bonus adds nothing to OB/damage; its real value is **(a)** letting you hit STR 22 with fewer rolled
points (freeing points for CON) and **(b)** the **+4 CON → +50 HP** (`HP 519 vs 469`, Kraken TTD
62 vs 57). The Olog's actual game-changer isn't in this table: **`frenzy` forces Berserk** (turning on
Wild Fighting's rush/rage/wild-swing *for free*), adds **+10 % damage and crit auto-attacks**, and
stacks the Olog smash/cleave/overrun kit — so an Olog **Wild or Heavy** fighter has a burst ceiling no
other race reaches, at the cost of being a Berserk-locked glass body. **It is the definitive
big-2H/Wild race.** (DEX −3 makes Light Fighting a non-starter.)

### Haradrim — the third side
Neither free-peoples nor Mordor: evil-aligned but its **own** faction. **DEX +2 (no STR penalty),
−2 INT/−2 WIL/−3 LEA.** The DEX +2 gives it a foot in **dodge** (DB 6 vs −7 on the same 2H set) and
viable **Light Fighting**, but its signature is **spears**: **a Haradrim wielding a spear gains
energy-regen** (`profs.cpp:793`) *and* spears intrinsically **punch through armor (÷150)**, so a
Haradrim **spear** build is the premier **anti-armor** warrior — on the `#5514` enruned mithril spear
it posts `OB 168 / dps 6.1` (PvP, near the 2H-cleave) **and** ignores a third of any armored target's
mitigation (the Kraken/bosses are unarmored so the table doesn't show that edge — vs a plate PvP
opponent it's large). Haradrim also shoots a beat faster (bows) for an archery cross-build.

---

## The artifact ceiling

All Three Rings are now in the world data and are in a class of their own — and the expansion added
the strongest of them: **Vilya `#5065` = OB +150 / DAMROLL +20**, alongside **Narya `#1610`** and
**Nenya `#7936` (OB +100 / DAMROLL +15 each)**. Wearing the best pair (Vilya + Narya = **+250 OB,
+35 damroll**) doesn't improve a build — because OB *multiplies* damage and damroll feeds `base`
directly, it **breaks the scale**:

| Build | rings | OB | PvP dps | PvP TTK |
|---|---|---:|---:|---:|
| Light (rapier) | ivory ×2 (OB+4) | 176 | 4.8 | 95 s |
| Light (rapier) | **Vilya + Narya** | **422** | **95.8** | **5 s** |

The best artifact pair is worth **~20× the damage** of common rings. If they're in play, they dominate
every slot decision and trivialize the matchup — treat them as a separate (unique, contested) tier,
not part of a realistic best-in-slot set. They benefit *every* archetype equally (pure OB/damroll), so
they don't change the *relative* spec ranking — only the absolute numbers.

---

## Appendix — tools & regenerating these numbers

The calculator and item/mob parsers are committed under [`tools/`](tools/) (kept as reusable
artifacts of this analysis):

- **`wbis.py`** — the obj/mob parser + faithful combat engine (`get_real_OB/parry/dodge`,
  `get_weapon_damage`, `armor_absorb`, `ENE_regen`, `class_HP`, encumbrance, the per-swing
  Monte-Carlo, and `validate()` against the doc anchors). Run `python3 wbis.py` to see validation.
- **`mine.py`** — surveys the live item pool (per-type weapon leaders, per-slot armor, shields,
  stat-stick accessories).
- **`warrior_bis.py`** — builds every archetype/combo/race and prints all the tables above.
- **`rps_duel.py`** — the mutual duel simulator behind the [matchup section](#spec-matchups--does-the-intended-rock-paper-scissors-hold) (auto-attacks + rage, armor-ignoring skills, riposte, block).
- **`analysis_output.txt`, `rps_output.txt`** — captured runs.

To regenerate after world-data or formula changes: `cd docs/guides/tools && python3 warrior_bis.py`.
Paths are derived from the repo root, so they work in place. Adjust assumptions (level, stats, skill
knowledge, tactics, reference targets) at the top of `warrior_bis.py` / in the `Char` dataclass in
`wbis.py`.

> **Caveat for maintainers:** the engine mirrors the *current* live formulas (see the source cites at
> the top). If combat code changes (e.g. the commented-out heavy-fighting encumbrance re-add in
> `char_utils.cpp:722`, or the bow weapon-skill overload disagreement noted in
> [weapons.md](../systems/weapons.md)), re-validate `wbis.py` against
> [combat-stat-examples.md](../systems/combat-stat-examples.md) before trusting new numbers.
