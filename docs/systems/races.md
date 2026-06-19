# Races

**Source files:** race ids `structs.h:858-875`; ability modifiers `profs.cpp:50-126`
(`race_modifiers` + `get_*_mod`); derived pools `profs.cpp:136-145` (`class_HP`), `:715-806`
(`recalc_abilities`); profession caps `utils.h:318` (`GET_MAX_RACE_PROF_LEVEL`) /
`char_utils.cpp:339`; advancement `utils.h:327-330` / `char_utils.cpp:362-380`; perception
`utility.cpp:312-369` & `char_utils.cpp:1252-1304`; innate affects/alignment/caps `consts.cpp:2231-2357`;
combat daylight penalty `utility.cpp:738-748,841-851,884-894` + `get_power_of_arda` `utility.cpp:1596`;
sun-spell penalty `utils.h:425`; terrain movement `act_move.cpp:517-545`; orc recruit
`act_othe.cpp:136-259`; race selection `interpre.cpp:2618-2648`; names `consts.cpp:1989,2011,2344`.
**Status:** ✅ rosters, stat differentials, derived pools, caps, perception, daylight, faction,
special abilities, class fits.

> **Scope.** This is the one-stop race guide. It cross-references rather than re-derives:
> stat-rolling order is [stats-and-character-power.md §3](stats-and-character-power.md); the
> profession-coefficient tweaks are [class-system.md](class-system.md); perception's role in mystic
> saves is [cleric-mystic-system.md §1-2](cleric-mystic-system.md); the mage-cap term in spellpower
> is [magic-system.md §1](magic-system.md); specializations are [specializations.md](specializations.md).

---

## 0. Roster at a glance

Ten races are selectable at creation (`interpre.cpp:2618`). Six more exist in the enum for NPCs /
immortals. "Net" = the sum of the six ability modifiers (how much total stat the race adds to the
rolled 80–85 pool — see §2).

| Race (creation key) | id | Side | Net stat | Headline trait |
|---|--:|---|--:|---|
| **Human** (`h`) | 1 | Free | **0** | No modifiers, no caps — the flexible baseline |
| **Dwarf** (`d`) | 2 | Free | **0** | STR/CON tank; +axe energy-regen; perception 0 |
| **Wood Elf** (`w`) | 3 | Free | **0** | DEX/INT; best archer; +move, poison resist, awareness |
| **Hobbit** (`b`) | 4 | Free | **0** | DEX/CON nimble; **+1 spell save** (anti-magic) |
| **Beorning** (`n`) | 6 | Free | **0** | STR+4/CON+4 bruiser; +20 dodge, damage-reduction, claw/swipe |
| **Uruk-Hai** (`u`) | 11 | Servant | **−8** | Sturdy warrior; **cheap** (but capped) mage levels |
| **Orc** (`c`) | 13 | Servant | **−14** | Weakest solo, but **recruits a 4-pet army** |
| **Magus / Uruk-Lhuth** (`l`) | 15 | Servant | **−6** | The evil **caster** (full mage cap, best evil INT) |
| **Olog-Hai** (`o`) | 17 | Servant | **−6** | STR+4/CON+4; ×1.5 regen, frenzy — but melts in daylight |
| **Haradrim** (`r`) | 18 | Servant | **−5** | Evil archer/spear; **immune to the daylight penalty** |

**NPC / immortal-only:** God (0), **High-Elf (5)** — fully statted (INT+2/DEX+2/CON−2, perception
100) but not creatable; Harad (12), Easterling (14), Undead (16, breathes underwater), Troll (20).

**Two design philosophies jump out of the "Net" column.** Free-peoples races **redistribute** a
fixed pool (net 0 — they trade one stat for another). Servant races pay a **net stat tax** (−5 to
−14) that is meant to be repaid by faction mechanics: Orc armies, Uruk's mage discount, Haradrim's
daylight immunity, Olog's regen. The Orc is deliberately the weakest *individual* in the game.

---

## 1. How "race" enters the mechanics (the channels)

Race never acts directly — it flows through eight channels. Every per-race effect below is one of
these:

1. **Ability-score modifiers** (`race_modifiers`, `profs.cpp:50`). Added to the rolled stats
   (which sum to 80–85), floored at 1 (stats-and-character-power §3). Order is **STR, INT, WIL, DEX,
   CON, LEA** (proven by `get_str_mod`…`get_lea_mod`, `profs.cpp:74-126`). The only per-race ability
   **cap** is STR = 22 for *all* races (`max_race_str`, `consts.cpp:2256`); there is no special CON
   cap, so a racial **+4 CON translates straight into HP** (see below).
   > **These modifiers touch only your *starting* stats.** They are applied **once**, in the
   > character-creation roll, and baked into your base abilities (`recalc_abilities` just copies the
   > stored base each tick — `profs.cpp:721-727`). They do **not** change how stats *grow*. Ongoing
   > stat growth is driven by your **class-proficiency allocation** — which profession's primary stat
   > you invest in (Mage→INT, Cleric→WIL, Ranger→DEX, Warrior→STR; stats-and-character-power §3) —
   > not by race. Race sets your starting line, not your growth curve.
2. **Derived pools** (`recalc_abilities`, `profs.cpp:715`):
   - **HP** `= 10 + min(30,level) + constHit·CON/20 + (class_HP·(CON+20)/14)·min(3000,mini_level)/100000`,
     where `class_HP = 200·√(3·warPts + 2·rangerPts + clericPts)` and is **×4/7 for Orcs**
     (`profs.cpp:140`). CON appears **twice** — it is the master survivability stat.
   - **Mana** `= 40 + INT + WIL/2 + 2·mageLevel`.
   - **Move** `= 80 + CON + 20 + rangerLevel + travelling/4`, **+15 for Wood/High-Elf**, **+50 for
     Beorning** (`profs.cpp:747-753`).
   - **Spirit** base 9 (set at creation; mystic resource).
3. **Profession caps** (`GET_MAX_RACE_PROF_LEVEL`, `utils.h:318`): everyone is **30** except
   **Orc = 20 in all four professions** and **Uruk = 27 in Mage** (30 elsewhere). This cap also
   feeds power: spellpower uses the mage cap (magic-system §1) and the OB ramp uses the warrior cap
   (`utility.cpp:679`), so an Orc's level-20 cap quietly weakens its damage curve too.
4. **Advancement coefficient** (`utils.h:327-330`): **Orc skills cost ~1.5× (×⅔ rate)**; **Uruk
   mage gets −100** to the coefficient (levels mage *cheaper/faster*, despite the 27 cap). Detail in
   class-system.md.
5. **Perception baseline** (`get_race_perception`): gates mystic fear/haze/terror saves and the
   reach check, and scales heal-over-time regen (cleric-mystic §1, §4.1). See the table in §2.
6. **Innate vision** (`race_affect`, `consts.cpp:2231`): **Infravision** (`AFF_INFRARED`, see in the
   dark) for Beorning/Uruk/Orc/Magus/Olog; **Moonvision** (`AFF_MOONVISION`, see outdoors under
   moonlight) for Wood-Elf/High-Elf/Haradrim; **none** for Human/Dwarf/Hobbit (need a light source).
7. **Alignment band** (`consts.cpp:2348`): Free peoples may range **−varies…+500**; servant races
   are locked **−500…−100** (always at least mildly evil). Killing shifts alignment within the band.
8. **Faction / side** (`other_side`, `handler.cpp`): Free peoples vs. servants can't group and are
   PK-valid against each other. Within the servants, **Uruk+Orc** are one bloc and **Magus+Haradrim**
   are a second ("magi"); the two blocs are hostile. Orcs **cannot follow or be followed by other
   players at all** — which is exactly why they get the recruit army (§3, code comment
   `act_othe.cpp:195`).

Plus two **daylight** systems that punish (most) servant races outdoors in sun:
- **Power of Arda** (`get_power_of_arda`, `utility.cpp:1596`) builds up on evil races outdoors in
  sunlight and docks **OB/parry/dodge** — both a multiplier and a flat `sun_mod` (= `SPELL_ARDA
  modifier/25`). The multipliers (when Arda is active): **OB** Uruk/Magus/Olog **×4/5**, Orc **×3/4**;
  **parry & dodge** Uruk/Magus/Olog **×9/10**, Orc **×8/9** (`utility.cpp:738-748,841-851,884-894`).
  **Olog-Hai accumulate it ×3** (`limits.cpp:1209`); **Haradrim are exempt entirely**
  (`limits.cpp:1201`).
- **Sun penalty** (`SUN_PENALTY`, `utils.h:425`): Uruk/Orc/Olog/Magus lose the bonus damage terms on
  dark spells (Dark Bolt, Black Arrow, Searing Darkness, Spear of Darkness) in daylight
  (magic-system §7). Haradrim, again, is not on the list.

---

## 2. The ability-score differential table

Modifiers added to the rolled 80–85 pool (§1.1). Player-facing races only; STR cap is 22 for all.

| Race | STR | INT | WIL | DEX | CON | LEA | Net | Read |
|---|--:|--:|--:|--:|--:|--:|--:|---|
| **Human** | 0 | 0 | 0 | 0 | 0 | 0 | **0** | Unmodified — your rolled spread is your spread |
| **Dwarf** | **+2** | 0 | −2 | −3 | **+4** | −1 | 0 | Tanky melee; clumsy (DEX−3); poor will/mana |
| **Wood Elf** | −1 | **+1** | 0 | **+2** | −2 | 0 | 0 | Agile & smart; fragile (CON−2) |
| **Hobbit** | **−3** | −1 | 0 | **+2** | **+2** | 0 | 0 | Nimble & sturdy; can't hit hard (STR−3) |
| **Beorning** | **+4** | **−4** | −2 | 0 | **+4** | −2 | 0 | Bruiser; mentally hopeless caster |
| **Uruk-Hai** | 0 | **−4** | −3 | 0 | **+2** | −3 | −8 | Full STR, sturdy; weak mind |
| **Orc** | −1 | −3 | −3 | −1 | −1 | **−5** | **−14** | Negative everywhere; worst leadership |
| **Magus** | −1 | −1 | −3 | 0 | **+1** | −2 | −6 | Mildest INT hit of the servants → caster |
| **Olog-Hai** | **+4** | **−4** | **−4** | −3 | **+4** | −3 | −6 | STR/CON twin of Beorning; clumsy, dumb |
| **Haradrim** | 0 | −2 | −2 | **+2** | 0 | −3 | −5 | Agile man-at-arms; full STR/CON |
| *(High-Elf, NPC)* | 0 | +2 | 0 | +2 | −2 | 0 | +2 | Net-positive — why it isn't playable |

**Perception baseline** (separate from the six abilities; `utility.cpp:312`, the value that feeds
mystic saves — see the discrepancy flag in §7):

| Race | Perc | Race | Perc | Race | Perc |
|---|--:|---|--:|---|--:|
| High-Elf | **100** | Human / Hobbit / Uruk / Magus / Olog / Haradrim / (Easterling/Troll) | **30** | Orc | **10** |
| Undead | 60 | Wood-Elf | **50** | Dwarf / God | **0** |
| Beorning | 30* | | | | |

\*See §7 — a second `get_race_perception` (`char_utils.cpp:1252`) returns **15** for Beorning and 0
for Olog/Haradrim. Perception is clamped to 0–100, and rises **+2 per cleric level** on top of the
baseline (cleric-mystic §1), so a 30-mystic adds +60.

---

## 3. Per-race profiles

### Free peoples

#### Human — the unmodified flexible baseline
- **Stats:** all 0. **Caps:** 30 everywhere. **Perception:** 30. **Vision:** none.
- **Special:** halves move cost in **fields** (`SECT_FIELD`). Widest alignment range (−300…+500).
- **Why play it:** zero penalties anywhere means your **rolled** primary stat is delivered intact and
  you keep full caps and normal advancement. There is no class a Human is *bad* at — it is the
  default pick for **Mage and Mystic**, where every other free race dents INT or WIL or perception.
- **Best fits:** anything; especially **Mage / Mystic** and any min-maxed hybrid.

#### Dwarf — the axe tank
- **Stats:** STR+2, CON+4, WIL−2, DEX−3, LEA−1. **Perception 0** (worst). **Vision:** none.
- **Special:** halves move cost in **mountains**; **bonus energy-regen (attack speed) with axes**
  (`min(eneRegen/10, 10)`, `profs.cpp:791`).
- **Reads:** CON+4 = a big HP pool; STR+2 = OB/damage; but DEX−3 hurts dodge, attack speed, and
  archery, and **WIL−2 + perception 0** make Dwarves nearly defenceless against mystic fear/haze and
  poor at any casting (low mana, low mental defense).
- **Two-handed weapons sidestep the DEX malus.** Attack speed for **high-bulk (≥4) weapons** is
  computed from **Strength only** — the DEX/STR speed blend applies *just* to lighter weapons
  (`bulk < 4`, `recalc_abilities`, `profs.cpp:769-782`). So a Dwarf's **DEX−3 barely slows a
  two-hander**, which also gets `×2` swing speed, a STR-scaled OB via `SKILL_TWOHANDED`
  (`utility.cpp:700`), and a parry bonus (`utility.cpp:835`). Since parry keys off **STR + warrior
  level** (not DEX) while *dodge* keys off DEX (`utility.cpp:775` vs `:870`), the Dwarf's natural game
  is a **two-handed STR bruiser that tanks through HP and parry, not dodge** — exactly the weapon
  class its stat line wants.
- **Best fits:** **Warrior**, specifically a **two-handed / axe** wielder (Heavy Fighting / Defender).
  Avoid Mage and Mystic.

#### Wood Elf — the premier archer
- **Stats:** DEX+2, INT+1, STR−1, CON−2. **Perception 50.** **Vision:** moonvision.
- **Special:** +15 max move and **+5 move regen**; **+30 poison save** (`spell_pa.cpp:312`); **+5
  awareness** to spot hidden foes (`ranger.cpp:1969`); **−1 archery wait** (faster shots,
  `ranger.cpp:2127`); halves move cost in **forest**.
- **Reads:** DEX+2 + the archery speed bonus + awareness = the **best bow user** in the game; INT+1
  and perception 50 keep casting and mystic defense respectable. CON−2 makes it squishy.
- **Best fits:** **Ranger / Archer** (top tier), secondarily **Mage**. Not a frontline tank.

#### Hobbit — the nimble anti-mage
- **Stats:** DEX+2, CON+2, STR−3, INT−1. **Perception 30.** **Vision:** none.
- **Special:** **+1 spell save** (`spell_pa.cpp:204` — unique; the best raw magic resistance in the
  game); halves move cost in **hills**.
- **Reads:** DEX+2/CON+2 makes a sturdy, evasive skirmisher, but STR−3 guts melee OB/damage, so it's
  no bruiser. The +1 spell save (which shifts *every* mage save 5%, magic-system §3) makes Hobbits
  the natural **counter to enemy mages** in PK.
- **Best fits:** **Ranger / dodge-skirmisher**, and any build that wants to **shrug off spells**.
  Avoid pure STR melee.

#### Beorning — the were-bear bruiser
- **Stats:** STR+4, CON+4, INT−4, WIL−2, LEA−2. **Perception 30.** **Vision:** infravision.
  Bodytype is set to **15 (bear)** at creation (`limits.cpp:872`).
- **Special:** **+20 dodge** flat (`utility.cpp:874`); innate **physical damage reduction** (scales
  with maul/defend skills, `fight.cpp:1632`); unarmed hits are **claws** (`TYPE_CLAW`); a **swipe**
  multi-target attack (`fight.cpp:2660`); **+50 max move** and **×1.5 move regen**; halves move cost
  in forest. Its bear attacks (**rend / bite / maul / swipe**) are race-locked skills (§4).
- **Reads:** STR+4 + CON+4 = top-end OB, damage, and HP; +20 dodge and damage reduction make it
  tanky beyond its stats; the mobility (move + regen) is excellent. INT−4/WIL−2 mean it should never
  cast.
- **Best fits:** **Warrior** — the apex free-peoples melee (Wild Fighting / Heavy Fighting /
  Defender). A pure bruiser.

### Servants of the Enemy

#### Uruk-Hai — the sturdy warrior with a mage discount
- **Stats:** CON+2, INT−4, WIL−3, LEA−3 (STR & DEX full). **Caps:** **Mage 27**, else 30.
  **Perception 30.** **Vision:** infravision.
- **Special:** **−100 mage coefficient** (`GET_URUK_MAGE_PENALTY`, `utils.h:327`) — Uruks level the
  **Mage** profession noticeably *cheaper* than anyone else, even though it caps at 27. Suffers the
  daylight Power-of-Arda penalty (OB ×4/5) and the dark-spell sun penalty. Halves move cost in swamp.
- **Reads:** full STR with CON+2 = a solid warrior body; the mage discount makes the Uruk the
  intended **warrior/mage hybrid** of the dark side, despite weak INT.
- **Best fits:** **Warrior**, and **Battle-Mage / warrior-mage** hybrids (lean on the mage discount;
  don't expect high spell damage from INT−4).

#### Orc — the disposable swarm commander
- **Stats:** every ability negative (STR/DEX/CON −1, INT/WIL −3, **LEA −5**); **net −14**, the worst
  in the game. **Caps: 20 in *all* professions.** **HP ×4/7.** **Advancement ~1.5× cost.**
  **Perception 10.** **Vision:** infravision. **Worst** daylight penalty after Olog (OB ×3/4).
- **Special — RECRUIT** (`do_recruit`, `act_othe.cpp:136`): the orc-only command. Recruits NPCs
  flagged `MOB_ORC_FRIEND` as permanent charmed **pets** (`MOB_PET`, loses aggression/spec/stay-zone):
  - up to **4 followers**;
  - each recruit's level ≤ **`(orc_level·2 + 2)/5`**;
  - total recruited levels ≤ **`min(orc_level, 48)`** — so a max-level orc fields a 48-level force.
  - Can't recruit while fighting, in shadow form, or (as a charmed NPC) under a master.
- **Reads:** the Orc is deliberately the weakest *individual* — low everything, a level-20 ceiling,
  reduced HP, and slow leveling. The compensation is the **pet army** plus the design rule that orcs
  **cannot group with players at all** (`act_othe.cpp:195`), so their followers *are* their party.
- **Best fits:** a **numbers / pet-swarm** playstyle rather than a class. Within the level-20 cap,
  cheap-to-train **Warrior or Ranger** skills get the most out of the orc body; the army does the
  heavy lifting. (Orcs also **can't take the Guardian spec** — specializations.md.)

#### Magus (Uruk-Lhuth) — the dark caster
- **Stats:** INT−1 (the **mildest** INT hit of any servant), CON+1, WIL−3, STR−1, LEA−2. **Caps:**
  30 everywhere (full **Mage 30**, unlike the Uruk). **Perception 30.** **Vision:** infravision.
- **Special:** the sorcerer-uruk. Suffers Power of Arda (OB ×4/5) and the dark-spell sun penalty, so
  it casts from **indoors / at night**. Sits in the **magi** faction with Haradrim (hostile to the
  Uruk/Orc bloc *and* the free peoples).
- **Reads:** INT−1 + full mage cap makes the Magus the **best offensive caster on the dark side** —
  the home of **Darkness** specialization and the **exclusive dark-mage spell line** (Spear of
  Darkness, Leach, Word of Agony, …; see §4 and magic-system §7). Don't melee in daylight.
- **Best fits:** **Mage** (dark/arcane caster); a serviceable **Mystic** too (WIL−3 hurts, but
  perception 30 is fine).

#### Olog-Hai — the regenerating juggernaut that fears the sun
- **Stats:** STR+4, CON+4, INT−4, WIL−4, DEX−3, LEA−3 (a Beorning-like body with worse DEX/WIL).
  **Perception 30.** **Vision:** infravision.
- **Special:** **×1.5 HP regen and ×1.5 move regen** when not poisoned (`limits.cpp:239,340`) — but
  **cannot receive** the mystic Curing/Restlessness/Vitality/Regeneration spells (they refuse Olog
  targets, `mystic.cpp:757`+), so that innate regen *is* its sustain. **Frenzy** grants it an extra
  attack (`fight.cpp:2241`) but it **cannot cool down out of frenzy** (`act_othe.cpp:1315`).
  **Worst daylight race:** accumulates Power of Arda **×3** (`limits.cpp:1209`) on top of the OB ×4/5
  / parry & dodge ×9/10 penalties. Olog-only active skills: **smash / frenzy / stomp / cleave /
  overrun** (§4).
- **Reads:** STR+4/CON+4 + ×1.5 regen = a self-sustaining wrecking ball at night/indoors; INT/WIL−4
  rule out casting; the ×3 Arda makes daylight open-field fighting near-suicidal.
- **Best fits:** **Warrior** — the dark-side bruiser built around **Wild Fighting / frenzy / berserk**
  (specializations.md notes frenzy *forces* Berserk on an Olog). Fight after dark.

#### Haradrim — the daylight-proof archer/spearman
- **Stats:** DEX+2, INT−2, WIL−2, LEA−3 (STR & CON full). **Perception 30.** **Vision:**
  **moonvision** (like the elves — they are Southron *men*, not orcs).
- **Special:** **exempt from Power of Arda** (`limits.cpp:1201`) — **the only servant race with no
  daylight combat penalty**, a major edge. **−1 archery wait** (`ranger.cpp:2127`); **arrow break
  chance halved** (`ranger.cpp:2168`); **bonus energy-regen with spears** (`profs.cpp:793`) and may
  **ambush with heavy (>2 bulk) spears** (`ranger.cpp:925`); can **see the `(marked)` status** on
  targets (`act_info.cpp:603`). Magi faction (with Magus). It also owns several **race-locked ranger
  skills** (mark, blind, …; see §4).
- **Reads:** DEX+2 + the archery/spear bonuses make Haradrim the **evil counterpart to the Wood Elf**
  — the dark side's ranged/skirmish specialist — and the daylight immunity means they're the servant
  race you can actually field in open daytime PvP.
- **Best fits:** **Ranger / Archer**, and **spear Warrior** (the spear affinities + ambush).

### NPC / immortal races (not creatable)
**High-Elf** is fully implemented (INT+2/DEX+2/CON−2, perception **100** → effectively immune to
mystic fear/haze, +15 move, moonvision) and net-positive — which is presumably why it's reserved.
**Undead** can breathe underwater (`utility.cpp:1625`). **God, Harad, Easterling, Troll** are mob
shells with no modifiers.

---

## 4. Race-exclusive skills & spells

Some content is locked to a race — but by **two different mechanisms** that behave differently:

**(a) Learn-gated (guild access) — the dark-mage spells.** For players, *casting* is **not**
race-checked: `can_cast_spell` (`spell_pa.cpp:410`) only validates profession, known knowledge,
position, and mana. Exclusivity is enforced at **learning** time — every guildmaster mob carries an
`rp_flag` race bitmask, and `SPECIAL(guild)` refuses to teach unless `RP_RACE_CHECK(host, ch)` passes
(`spec_pro.cpp:232`, `utils.h:643`). A spell is "race-exclusive" because only that side's
guildmasters, placed in that side's territory with the matching `rp_flag`, teach it — a free-peoples
mage simply has **no trainer** for the dark line. (So it's a *trainer-access* gate, not a hard
cast-time block.)

**(b) Use-gated (hardcoded) — the racial active skills.** The bear/Olog/Haradrim abilities are blocked
at the command itself by a `GET_RACE(ch) != RACE_X` check (it answers "Unrecognized command" to
everyone else), so they are race-locked regardless of knowledge.

### The dark-mage line — built around the Magus (Uruk-Lhuth)
The evil counterpart to the standard mage catalog (magic-system §7), taught only by evil guildmasters
and centered on the **Magus**, the full-mage-cap (30) dark caster:
**Word of Pain** (100), **Word of Sight** (101), **Word of Agony** (102), **Shout of Pain** (103),
**Word of Shock** (104), **Spear of Darkness** (105), **Leach** (106), **Black Arrow** (107). The
Magus also starts knowing **Blink** free (`spec_pro.cpp:197`). The tiering is visible in the mob
caster-AI: `get_mob_spell_type` (`spec_pro.cpp:1410`) hands the **Magus** the fullest dark set
(type 2), **Uruk/Orc** a narrower one (type 1), and free peoples none — so the Magus is the race the
whole line is designed for, with Uruk/Orc dabbling. (Damage/cast figures for these spells are in
magic-system §7: Word of Pain ≈ Magic Missile, Spear of Darkness the unsaveable nuke, Leach the
drain-heal, Word of Agony the Chilled DoT, etc.)

### Hardcoded race-locked active skills
| Race | Exclusive skills | Gate |
|---|---|---|
| **Haradrim** | **mark**, **blind**, plus further ranger skills (e.g. bend-time / wind-blast) and **spear ambush** | `can_ch_mark` / `can_ch_blind` / `can_harad_use_skill` (`ranger.cpp:2964, 3158, 3411`); spear-ambush gate (`:925`) |
| **Beorning** | **rend**, **bite**, **maul**, **swipe** (the bear attacks) | `can_bear_skill` (`act_offe.cpp:1005`); starts with **natural attack** free (`spec_pro.cpp:200`) |
| **Olog-Hai** | **smash**, **frenzy**, **stomp**, **cleave**, **overrun** | `is_skill_valid` (`olog_hai.cpp:49`) |

Note the difference from §3's **passive** racial traits: the Haradrim archery/arrow/spear *bonuses*,
Beorning's claw/damage-reduction, and the perception/regen/vision effects apply automatically and
aren't "skills" you activate. (The mystic **mass** spells — Mass Regeneration/Vitality/Insight — are
gated by **mastery**, not race: `is_spell_free` + 100 % skill, cleric-mystic §4.) Starting **kits**
also differ by race side (`KIT_DARKIE`/`KIT_THIRD`, `spec_pro.cpp:596-604`).

---

## 5. Race × class suitability

Pulling §3 together. "✓✓" = signature fit, "✓" = good, "·" = workable, "✗" = fights the race.

| Race | Warrior | Ranger/Archer | Mage | Mystic | Signature build |
|---|:--:|:--:|:--:|:--:|---|
| **Human** | ✓ | ✓ | ✓✓ | ✓✓ | Anything — unmodified caster/flex |
| **Dwarf** | ✓✓ | · | ✗ | ✗ | Axe tank / Defender (CON+4, +axe speed) |
| **Wood Elf** | · | ✓✓ | ✓ | ✓ | Archer (DEX+2, −1 wait, +awareness) |
| **Hobbit** | · | ✓✓ | ✓ | ✓ | Dodge-skirmisher / anti-mage (+1 save) |
| **Beorning** | ✓✓ | · | ✗ | ✗ | Bruiser (STR+4/CON+4, +20 dodge, swipe) |
| **Uruk-Hai** | ✓✓ | ✓ | ✓ (cheap, cap 27) | · | Warrior / Battle-Mage hybrid |
| **Orc** | ✓ (cap 20) | ✓ (cap 20) | · (cap 20) | · (cap 20) | **Pet-swarm** commander (recruit army) |
| **Magus** | · | · | ✓✓ | ✓ | Dark caster (Darkness/Arcane) |
| **Olog-Hai** | ✓✓ | · | ✗ | ✗ | Frenzy juggernaut (night/indoor melee) |
| **Haradrim** | ✓ (spear) | ✓✓ | · | · | Daylight-proof archer / spear ambusher |

**Stat→class logic.** Warriors want **STR + CON** (OB, damage, HP) → Beorning, Olog, Dwarf, Uruk.
Archers/Rangers want **DEX** (+ the archery bonuses) → Wood Elf, Haradrim, Hobbit. Mages want **INT**
and the **mage cap** → Human, Magus (evil), Uruk (cheap but capped). Mystics want **WIL + perception**
→ Human, High-Elf (NPC); every evil race is hampered by WIL−3/−4, and Dwarves by perception 0.

**Caps & survivability tilt it further.** The Orc's level-20 cap and HP ×4/7 push it away from solo
power entirely and toward its recruit army. CON-heavy races (Dwarf/Beorning/Olog +4) get
outsized HP because CON enters the HP formula twice and has no special cap. Perception 0 (Dwarf) is a
real liability vs. mystics, while perception 50 (Wood Elf) / 100 (High-Elf) is a near-immunity to
fear/haze/terror.

---

## 6. Worked differential — how a +4 CON actually pays off

A level-30 character with **base rolled CON ~15** vs. a **Dwarf/Beorning/Olog (+4 → CON 19)**, all
else equal, on the HP formula (`profs.cpp:729`):
- The `constHit·CON/20` term and the `(class_HP·(CON+20)/14)` term both scale with CON. Going 15→19
  lifts the `(CON+20)` factor from 35→39 (**+11 %** on the dominant class-HP term) *and* adds to the
  `constHit·CON/20` term — a meaningful HP bump on top of an already-high pool, which is why
  Beorning/Olog tanks feel so durable.
- For an **Orc**, the same class-HP term is first multiplied by **4/7 ≈ 0.57** (`class_HP`), so even
  with comparable CON an Orc has markedly less HP than a free-peoples warrior — by design.

And how the **−1 archery wait** compounds for Wood-Elf/Haradrim: shaving a beat off every shot is a
flat attack-speed gain that stacks with their DEX+2 (which already drives attack timing and dodge),
making them the two fastest bow users — see ranger-skills.md for the archery beat math.

---

## 7. Open questions / flags

- **Two `get_race_perception` functions disagree.** `utility.cpp:312` (feeds `get_naked_perception`
  → mystic saves) gives **Beorning 30, Olog 30, Haradrim 30**; `char_utils.cpp:1252` (the fallback
  inside the clamped `get_perception`) gives **Beorning 15** and **no case for Olog/Haradrim → 0**.
  In practice the stored, naked-derived value governs, so this doc uses the `utility.cpp` numbers —
  but the divergence is a latent bug (a code path that reads the `char_utils` fallback would see a
  different Beorning/Olog/Haradrim perception). Consolidate into one table.
- **`max_race_str` is the only ability cap** and it's a uniform 22 — there is no `max_race_con`
  /`_dex`/etc., so racial CON/DEX bonuses are uncapped beyond the global stat ceiling. Confirm that's
  intended (CON especially, given its double weight in HP).
- **Olog-Hai cannot be mystic-healed** (Curing/Restlessness/Vitality/Regeneration all refuse it) yet
  has ×1.5 innate regen — intended identity, but worth stating in any mystic-support docs so healers
  don't waste casts on an Olog ally.
- **Orc + Guardian spec is blocked** ("Snagas can't specialize in guardian!", `act_othe.cpp:1855`);
  no other race/spec restriction exists (specializations.md).
