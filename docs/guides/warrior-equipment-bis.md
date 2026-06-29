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
| **Heavy Fighting (2H)** | heaviest weapon + heaviest armor | `#5226` ice-encrusted battleaxe (2H cleave, d13.3/OB+15) | full plate | top bruiser DPS | fastest boss kills among durable specs |
| **Heavy Fighting (1H+shield)** | bulk-3 1H + shield + heavy armor | `#5044` gleaming broadsword + `#6510` golden shield | full plate | durable bruiser | best survivability/DPS balance |
| **Wild Fighting** | any big weapon, Berserk | `#5226` ice battleaxe (2H) | plate (offense from HP, not gear) | highest burst, glass cannon | fastest kills, dies fastest |
| **Light Fighting** | light 1H (bulk ≤ 2), minimal armor, high DEX + ranger | `#5435` marble dagger / `#5410` quicksilver rapier | leather | evasive duelist | glassy; avoid hard hitters |
| **Weapon Master** | the *right* weapon per target | `#5226` cleave (burst) / `#5410` pierce (vs armor) / `#5346` smite | plate | flexible technician | matches Wild vs the right target |
| **Defender** | shield + warrior/ranger levels | `#5044` broadsword + `#6510` golden shield | full plate | shield-wall tank/peeler | longest survival, slow kills |

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

**Model limits (be honest about these):** numbers are **average-case, relative** — not exact in-game
readouts. Parry decay (to ⅔ after the first parry in a round) is approximated as ~0.8 average
effectiveness; armor is averaged over a humanoid hit-location distribution; procs are expected-value;
PvE TTD assumes solo and no healing (these bosses are group content — TTD just ranks relative
squishiness). DPS/TTK/TTD are for **comparison between builds**, not absolute combat logs.

---

## The gear landscape (what actually exists)

Mining `lib/world/obj` (realistic pool, test/immortal items excluded): **222 weapons, ~591
armor/worn pieces, 47 shields.**

### Weapons — the per-type leaders (damage rating shown as in-game `identify`, ÷10)

| Type (skill) | Top damage | Top OB | Notes |
|---|---|---|---|
| **2H cleaving** (Axe) | `#5226` ice-encrusted battleaxe **d13.3 / OB+15** bulk7 wt2750 L45 | `#5221` silver battleaxe OB+17 | the single best 2H in the game |
| **2H bludgeon** (Conc.) | `#5320` Durin's Sceptre d11.7 bulk8; `#5366` warhammer d10.8 | `#1512` dark warhammer OB+10 | |
| **2H slashing** (Slash) | `#5104` ornate scimitar d10.2 bulk7 | `#5113`/`#5114` OB+14 | balances OB + damage |
| **Flail** (Whip) | `#5611` iridescent flail d11.3 bulk7 | — | no 2H *type*, but bulk≥4 grips 2H |
| **Spear** (Spears) | `#5514` enruned mithril spear **d10.1 / OB+12** bulk6 | `#5515` ash spear OB+12 | **÷150 armor pen** (anti-armor) |
| **1H slashing** (Slash) | `#5044` gleaming broadsword d7.6/OB+4/pa+3 bulk3; `#5033` obsidian d7.3/**OB+10/pa+5** | `#5035` silver shortsword OB+14 | bulk-3 1H = heavy-fighting legal |
| **1H bludgeon/smite** | `#5362` white mace OB+12; `#5346` steel maul d9.0/OB+12 (smite) | | smite shatters metal armor |
| **Piercing** (Pierce) | `#5435` marble dagger **d5.4 bulk1**; `#5410` quicksilver rapier **d5.1/OB+6/pa+7 bulk2** | `#5426` sickle OB+12 | the Light-Fighting weapons |

Damage is **derived** (`get_weapon_damage`): it rises with item level and bulk-near-3, and falls the
more OB/parry the weapon also grants — so the ice battleaxe pairing d13.3 *with* OB+15 is exceptional
(most high-OB weapons pay in damage).

### Armor — absorption scales with **level + encumbrance** (`armor_absorb`)

Heavy plate (`#6227` body abs62, `#6221` head abs67, `#6244` hands abs75, `#6256` legs abs59,
`#6246` feet abs61, `#6254` arms abs59) gives the most absorb but at enc 5–7 and 850–2200 weight each
— crushing dodge/speed for anyone **except a Heavy Fighter** (who soft-caps the weight/encumbrance).
Chain (`#11002` abs52 enc2) and leather (`#11119` abs40 enc2, lighter pieces enc0–1) trade absorb for
mobility — the Light Fighter's kit.

### Shields — dodge + parry, **not** absorption

Shields are not a hit location, so they give **`value[0]`→dodge, `value[1]`→parry** (+ the Defender
block layer), never `armor_absorb`. Leaders: `#6510` golden / `#6529` sable targe **(dodge15/parry15,
enc3)**; `#9080` heater (dodge8/**parry22**, enc5); `#1636` buckler (dodge10/parry18); `#6530` tower
(**parry40** but **wt4000** → wrecks dodge — only for a Defender who's already abandoned dodge).

### Accessories — small OB sticks, and the artifact cliff

`A`-line stat gear is sparse and small: **waist** `#6040` rainbow belt **OB+8** (best), **about**
`#6316`/`#6338` werewolf fur / robe **OB+5**, **wrist** `#6647` red bracelet OB+2, **neck** `#6649`
green amulet OB+1, **hold** `#6955` werewolf heart OB+2, **rings** `#6602` ivory ring OB+2.
**Damroll (the `points.damage` channel, `base += damroll·10`) effectively does not exist on common
gear** — the *only* meaningful sources are the artifact rings **Narya `#1610`** and **Nenya `#7936`
(OB+100, DAMROLL+15 each)**. Those two are a separate [ceiling tier](#the-artifact-ceiling).

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
| **2H** | `#5226` ice battleaxe (gripped 2H) | 174 | 42 | −7 | 469 | 12 | **6.2 / 74 / 169** | **185 / 57** |
| **1H+shield** | `#5044` broadsword + `#6510` golden | 159 | 93 | 15 | 469 | 27 | 4.3 / 105 / 261 | 247 / 76 |
| 1H+shield (parry) | `#5033` obsidian (OB+10/pa+5) + `#6510` | 159 | 97 | 15 | 469 | 27 | 4.2 / 110 / 277 | 254 / 76 |

**The headline trade:** the **2H deals ≈ 45 % more PvP damage and ≈ 30 % more PvE damage**, but the
**1H+shield survives ≈ 50 % longer** (TTD 261 vs 169 in PvP; +30 % vs bosses) thanks to **+78 PB and
+22 DB** from the shield (dodge15/parry15) and the bulk-3 weapon. The 2H wins via the ×2 `str_speed`
isn't the story here (it's still slow at sp12 because the axe is wt2750) — it wins on **OB+15 on the
weapon, +6 from the 2H OB term, and ×2 STR inside the damage factor**. Pick **2H to delete things,
1H+shield to anchor a line / duel attrition.** Avoid the smite-maul/heater combo generically (slow,
and smite only helps vs metal armor — which bosses don't wear).

### Wild Fighting — the berserker glass cannon

Fights on STR and gets stronger as it dies: **rush** (+50 % damage at 5/10/15 % by tactics), rage
attack-speed below 45 % HP, bloodlust heal on kill. Wants **Berserk** (which guts parry/dodge). Same
STR build + plate as Heavy; weapon = `#5226` (2H) for the biggest rush procs.

| Build | OB | PB | DB | HP | PvP dps / TTK / TTD | PvE (Kraken) TTK / TTD |
|---|---:|---:|---:|---:|---|---|
| **Berserk 2H cleave** | 186 | 24 | **−25** | 469 | **6.8 / 67 / 120** | **170 / 42** |
| Normal 2H cleave | 156 | 42 | −31* | 469 | 5.5 / 84 / 126 | 207 / 44 |
| Berserk 1H broadsword | 176 | 52 | −12 | 469 | 5.2 / 88 / 144 | — |

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

| Weapon | OB | PB | DB | HP | spd | PvP dps / TTK / TTD | PvE (Kraken) TTK / TTD |
|---|---:|---:|---:|---:|---:|---|---|
| `#5435` marble dagger (d5.4 b1) | 170 | 70 | **39** | 427 | 38 | **4.8 / 96 / 104** | 220 / 31 |
| `#5410` quicksilver rapier (d5.1/OB+6/pa+7 b2) | 179 | 77 | 39 | 427 | 34 | 4.3 / 107 / 111 | 235 / 33 |

Its DPS rivals Heavy-1H+shield while carrying **DB 39** (vs 15) — in PvP it *dodges* what the bruiser
*tanks*, and the fast light weapon + double-strike keep pressure on. The catch: **low HP + light
armor make it glassy in PvE** (TTD ~30 s vs the Kraken — worst of the roster) and a 2H/heavy weapon
*disables both* the DEX-OB substitution and the double-strike. The marble dagger (bulk 1) edges the
rapier on raw DPS (faster, double-strikes); the rapier gives more parry. Use the `#6200–6205` thin-
metal **+dodge** kit only if you want DB 40+ over absorption.

### Weapon Master — the toolbox technician

Mastery of whatever you hold — pick the weapon for the **target**. All-on, no ramp. STR build + plate,
36w/6r.

| Weapon | What it brings | OB | spd | PvP dps | PvE (Kraken) TTK | Best against |
|---|---|---:|---:|---:|---:|---|
| `#5226` 2H cleave | +15 % dmg, 50 % reroll-higher | 156 | 12 | **6.4** | **179** | raw burst (matches Wild, no risk) |
| `#5346` smite maul | +10 OB, haze proc | 159 | 20 | 4.6 | — | metal-armored targets, control |
| `#5410` pierce rapier | +15 % speed, 25 % **ignore armor** | 152 | 37 | 3.5 | 220 | **heavily-armored** PvP targets |
| `#5514` spear | 50 % **double armor-pen** | 151 | 17 | 4.9 | — | armored targets |
| `#5044` 1H slash | +5 OB/+5 PB, energy refund | 152 | 27 | 4.1 | — | balanced/duelist |

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
| `#6510` golden (d15/p15) | 137 | 89 | 0 | **530** | 3.1 / 146 / **265** | 311 / **77** |
| `#9080` heater (d8/p22) | 135 | 96 | −10 | 530 | 3.1 / 150 / 249 | — |
| `#6530` tower (p40, wt4000) | 135 | **114** | −21 | 530 | 3.0 / 151 / 262 | — |

Lowest personal damage, **best survival** (TTD 265 PvP; longest boss survival). The **golden shield**
is the best all-round pick — the tower shield's huge parry is cancelled by the dodge it costs (wt4000).
Defender's value is **group play** (rescue/peel); solo it just out-lasts. Race: **Beorning** (can
`brace` to use DEFEND without a shield; +20 innate dodge).

---

## Cross-archetype comparison

**PvP** (vs reference duelist; champion set of each spec):

| Archetype | OB | PB | DB | HP | spd | DPS | TTK (s) | TTD (s) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Wild (Berserk 2H) | 186 | 24 | −25 | 469 | 12 | **6.8** | **67** | 120 |
| Weapon Master (2H cleave) | 156 | 42 | −31 | 469 | 12 | 6.4 | 72 | 125 |
| Heavy (2H ice cleave) | 174 | 42 | −7 | 469 | 12 | 6.2 | 74 | 169 |
| Light (dagger, 30w21r) | 170 | 70 | **39** | 427 | 38 | 4.8 | 96 | 104 |
| Heavy (1H + golden shield) | 159 | 93 | 15 | 469 | 27 | 4.3 | 105 | **261** |
| Defender (golden shield) | 137 | 89 | 0 | **530** | 27 | 3.1 | 146 | 265* |

*Defender TTD ties 1H-Heavy here but pulls ahead in group play (rescue/peel/critical-block variance).
Reading it: **2H specs win the DPS race; 1H+shield and Defender win the war of attrition; Light wins
by not being hit.**

**PvE — vs named bosses** (TTK = seconds to kill / TTD = seconds to die, solo, no heals):

| Archetype | Kraken | pale lady | Nargul (balrog) | cold-drake | snow-troll king |
|---|---|---|---|---|---|
| Heavy 2H (ice cleave) | 185 / 57 | 333 / 49 | 349 / 77 | 373 / 48 | 376 / 77 |
| Heavy 1H + shield | 247 / 76 | 426 / 71 | 491 / 100 | 484 / 66 | 559 / 114 |
| Wild (Berserk 2H) | **170 / 42** | **308 / 36** | 315 / 58 | 342 / 35 | 331 / 56 |
| Weapon Master (2H cleave) | 179 / 44 | 312 / 38 | 358 / 60 | 353 / 37 | 406 / 59 |
| Light (dagger) | 220 / 31 | 392 / 28 | 420 / 40 | 441 / 26 | 461 / 45 |
| Defender (golden) | 311 / 77 | 532 / 68 | 729 / 101 | 608 / 65 | 882 / 112 |

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

**Result: the cycle does NOT hold. It collapses into a power ladder — `Wild ≳ Heavy > Light`.**

| Matchup (A vs B) | A wins | B wins | intended | verdict |
|---|---:|---:|---|---|
| Light (careful) vs **Heavy** (2H, normal) | 0 % | **100 %** | Light > Heavy | ❌ **inverted** |
| **Wild** (berserk) vs Heavy (2H, normal) | **81 %** | 19 % | Heavy > Wild | ❌ **inverted** |
| **Wild** (berserk) vs Light (careful) | **100 %** | 0 % | Wild > Light | ✅ holds |

Only the **Wild > Light** leg works. The other two invert: **Heavy beats Light**, and **Wild beats
Heavy** (Heavy 1H+shield does better vs Wild — 32 % — but still loses).

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
| **Heavy's kick disabled** | **60 %** ← Light wins |
| Light's kick disabled | 0 % |
| Light's riposte disabled | 0 % (no change) |

So **Light's intended counter to armor (evasion + riposte) is real but insufficient; the thing that
actually decides Light-vs-Heavy is whose *kick* is bigger — and Heavy's is bigger** (Warrior 36 vs 30,
**plus Heavy Fighting's +20 % skill bonus**). The armor-bypassing skill that was supposed to let
*light* fighters answer armor is strongest in the *armored* spec's hands.

**Heavy > Wild fails (Wild wins ~81 %).** Heavy's armor only mitigates **auto-attacks**, but Wild's
damage largely routes *around* armor: **rush** (+50 %) and **rage** attack-speed pump the
auto-stream, and the **wild-swing** (kick ×1.5, armor-ignoring) is a big chunk of its output. Worse
for the intent, **Wild can wear the same plate** — Berserk already throws away the dodge that plate
would cost, so a Wild fighter pays *no meaningful price* for full plate and gets its absorption for
free. So Wild is *also* armored, hits far harder (OB 186 + procs vs 174), and out-races Heavy's modest
durability edge. The juggernaut never gets to outlast anything.

**Wild > Light holds (100 %).** As intended: Berserk OB 186 + rush + rage-speed + armor-ignoring
wild-swing shreds Light's 427 HP in ~35 s, and Light's small, plate-absorbed damage can't punish
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
intended cycle does not hold — it's a power ladder — and the mechanisms meant to create the cycle
(armor-bypassing skills, riposte) are real but currently mis-weighted toward the armored specs.*

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
baseline (`OB 174 / HP 469 / PvP dps 6.2 / Kraken 186-57`). **Dwarf (+2 STR, +4 CON, −3 DEX)** is the
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
`OB 174 / dps 6.2`), but a daytime field fight is a real handicap — dark-side warriors want to pick
their ground. **Orc** is additionally the most progression-penalized race: **`max_race_prof_level` is
20 (not 30)** so its OB level-term and find-weakness ceiling are lower, and **`class_HP` is ×4/7** →
much less HP (`HP 370 vs 469`, `dps 5.5`, Kraken `209/44`). Orcs pay for it with cheap pets/recruits
and the spec-agnostic class-point system — but as a *pure melee* they're the weakest race. Uruk-Hai is
the better dark-side warrior by far (only the Mage prof is penalized for them).

### Olog-Hai — its own category
The Olog is *not* "a bigger orc." **STR +4 / CON +4 but INT/WIL/DEX −3/−4/−3.** Critically, **the
STR +4 is wasted at the cap** (`bal_str` tops at 22) — if you're already building STR 22, the racial
bonus adds nothing to OB/damage; its real value is **(a)** letting you hit STR 22 with fewer rolled
points (freeing points for CON) and **(b)** the **+4 CON → +50 HP** (`HP 519 vs 469`, Kraken TTD
44 vs 39). The Olog's actual game-changer isn't in this table: **`frenzy` forces Berserk** (turning on
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

The Three Rings are in the world data and are in a class of their own. **Narya `#1610` + Nenya
`#7936` = OB +100 / DAMROLL +15 each** (+200 OB, +30 damroll for the pair). Because OB *multiplies*
damage and damroll feeds `base` directly, they don't improve a build — they **break the scale**:

| Build | rings | OB | PvP dps | PvP TTK |
|---|---|---:|---:|---:|
| Light (rapier) | ivory ×2 (OB+4) | 179 | 4.3 | 107 s |
| Light (rapier) | **Narya + Nenya** | **375** | **71.7** | **6 s** |

A pair of artifacts is worth **~15× the damage** of common rings. If they're in play, they dominate
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
