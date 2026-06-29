# Specializations

**Source files:** spec enum `game_types::player_specs` (`structs.h:811-832`); per-spec data
`specialization_data`/`*_spec_data` (`structs.h:1467+`, set in `char_utils.cpp:1486-1505`);
warrior handlers `wild_fighting_handler.cpp`, `weapon_master_handler.cpp`,
`warrior_spec_handlers.h`. **Live** combat hooks live in `fight.cpp` (`heavy_fighting_effect`,
`defender_effect`, `wild_fighting_effect`, `get_evasion_malus`, `natural_attack_dam`),
`utility.cpp` (the light-fighting / weapon-master terms inside `get_real_OB`), `char_utils.cpp`
(encumbrance), `act_offe.cpp` (kick/swing/defend/maul/rescue), and `profs.cpp` (HP). Ranger specs live in
`ranger.cpp`, `utility.cpp`, `act_move.cpp`, `limits.cpp`, `handler.cpp`, `objsave.cpp`; mage in
`mage.cpp` / `battle_mage_handler.cpp`; mystic in `mystic.cpp`. Active spec read via
`utils::get_specialization` (modern) or `GET_SPEC(ch)` (legacy `PLRSPEC_*`). Selection command
`do_specialize` (`act_othe.cpp:1825`).
> ⚠️ `combat_manager.cpp` also contains spec hooks but is **dead code** — compiled, never
> invoked (see CLAUDE.md). Earlier drafts cited it for Protection's dodge bonus; the live path
> is `get_evasion_malus` in `fight.cpp`. Don't trust `combat_manager.cpp` line numbers.
**Status:** ✅ all specializations detailed (warrior in-doc; ranger in-doc; mage/mystic summarized
here with deep detail cross-referenced to the magic and cleric/mystic docs).

## What a specialization is
A character has **one active specialization** — a chosen focus layered on top of their
class-point build (§2 of `stats-and-character-power.md`) that reshapes how they fight or cast.
It's stored on the character and drives conditional bonuses throughout combat and magic
(extra damage, damage mitigation, procs, encumbrance handling, spell shaping). The full set
(`structs.h:811-832`):

The real enum (`game_types::player_specs`, `structs.h:811`) is ordered by historical accretion,
**not** by profession — the index is what the `PLRSPEC_*` `#define`s and the `specialize_name[]`
string table (`consts.cpp:2359`) key on:

```
0 PS_None     1 PS_Fire        2 PS_Cold         3 PS_Regeneration  4 PS_Protection
5 PS_Animals  6 PS_Stealth     7 PS_WildFighting 8 PS_Teleportation 9 PS_Illusion
10 PS_Lightning 11 PS_Guardian 12 PS_HeavyFighting 13 PS_LightFighting 14 PS_Defender
15 PS_Archery 16 PS_Darkness   17 PS_Arcane      18 PS_WeaponMaster 19 PS_BattleMage
```
Grouped by the profession whose mechanics each one actually amplifies:

```
Warrior  : PS_WildFighting, PS_HeavyFighting, PS_LightFighting, PS_WeaponMaster, PS_Defender
Ranger   : PS_Animals, PS_Stealth, PS_Archery
Mage     : PS_Fire, PS_Cold, PS_Lightning, PS_Darkness, PS_Arcane, PS_BattleMage, PS_Teleportation
Mystic   : PS_Regeneration, PS_Protection, PS_Illusion, PS_Guardian
```
These groupings are **descriptive, not enforced** — see the selection rules below.

> **Data note (player-base vintage).** A tally of the saved `spec` field across the archived player
> files shows only specs **0–11** in use; the newer warrior/mage additions — indices **12–19**
> (Heavy Fighting, Light Fighting, Defender, Archery, Darkness, Arcane, Weapon Master, Battle Mage)
> — have **zero** saved occurrences, i.e. they post-date that population. Wild Fighting (7) is by far
> the most common historical pick, followed by Stealth (6) and Illusion (9). The spec is stored as a
> plain integer (the enum index) in each player save's `spec` line.

## How a specialization is chosen

`do_specialize` (`act_othe.cpp:1825`): a player types `specialize <name>` (matched by name prefix
against `specialize_name[]`, `consts.cpp:2359`). Note the selectable strings differ slightly from
the `PS_*` enum names — e.g. `PS_Defender`'s match string is **`"defending"`** (`consts.cpp:2362`),
and `PS_WeaponMaster`/`PS_BattleMage` match `"weapon mastery"`/`"battle magic"`. The rules are
deliberately thin:
- **Level 12 minimum** (`GET_LEVEL(ch) < 12` → "too young to specialize").
- **One pick at a time** — once `get_specialization` is non-`PS_None`, `specialize` refuses to
  change it ("You are already specialized in *X*."). It is **not** permanent, though: a **prac
  reset** clears it (see below).
- **Respec via the Angel — from anywhere.** A player un-/re-specializes with `tell angel pracreset`.
  The **Angel** is not an NPC you meet in the world — it's a gateway that lets mortals run an
  approved subset of immortal commands from **anywhere** (`do_tell` resolves it via a global
  `get_char`, and the "Guardian angel" mob running `SPECIAL(resetter)` is perceivable across rooms,
  `utility.cpp:1484`). Full mechanism and the command set (`pracreset`, `reroll`) are documented in
  **[class-system.md → The Angel](class-system.md)**.
- **It's a full wipe**, not a targeted respec (`do_pracreset`, `spec_pro.cpp:398`): it first drops
  all your followers, then zeroes **all** `knowledge[]`/`skills[]`, restores your entire practice
  pool (`level·PRACS_PER_LEVEL + level·LEA/LEA_PRAC_FACTOR + 10`), sets specialization back to
  `PS_None`, and resets shooting/casting mode to normal. So changing spec means re-learning every
  skill from scratch — but your **class points are untouched** (a pracreset does not re-roll your
  profession allotment; see [class-system.md](class-system.md)).
- **No class/profession gate — by design.** The command does **not** check your profession levels:
  any character ≥ 12 can pick *any* spec (the only hard restriction is **Orcs cannot pick
  Guardian**, "Snagas can't specialize in guardian!", `act_othe.cpp:1855`). This is **intentional**
  and is the payoff of RotS's custom **class-point allotment** at creation (`existing_profs`,
  `profs.cpp:37`; see **[class-system.md](class-system.md)** for the full roster, the 150-point
  custom builder, and build archetypes): players distribute 150 points across the four professions —
  **mage / mystic / ranger / warrior** — and a profession's level scales with the **√** of its
  points (e.g. a **Swashbuckler** is R24/W24, a **Conjurer** M24/My24). Specs are deliberately
  left open so a **hybrid build can pick the spec that best leverages whatever mix it invested in.**
  A spec is only as strong as the profession levels it reads, so the warrior/ranger/mage/mystic
  groupings above describe *what makes a spec pay off*, not what you're allowed to choose.
  - **Worked example.** A custom **W24 / T24 / R10 / M9** build (warrior 24, mystic/theurge 24,
    ranger 10, mage 9) can take **Heavy Fighting** to turn its solid warrior level into front-line
    durability, *or* a **mystic spec** (e.g. Regeneration / Illusion / Guardian) to make its
    theurge-24 powers hit harder — same character, two legitimately different builds, because the
    spec isn't tied to a class.
  - And note every *default* melee class already carries ranger 25 (= R15 at level 30; see the
    warrior callout), so the **ranger** specs are live for ostensibly "warrior" characters too.
- On success it calls `recalc_abilities(ch)` so HP/spec-data hooks re-initialize immediately
  (`specialization_data::set`, `char_utils.cpp:1480`).
- `is_mage_spec()` (`structs.h:1477`) groups the six elemental/battle specs for the save matrix
  (magic-system.md §3); it's a mechanical grouping, not a selection gate.

> **Two names for the same spec.** The codebase carries two parallel identifier sets that map
> 1:1 by index: the modern `game_types::player_specs` enum (`PS_WildFighting`, used by the live
> handlers in `fight.cpp` / `char_utils.cpp` / the `*_handler.cpp` files) and the older
> `PLRSPEC_*` `#define`s (`PLRSPEC_WILD = 7`, used in `consts.cpp`, `act_info.cpp`,
> `handler.cpp`). `warrior-skills.md` cites the `PLRSPEC_*` form; this doc uses the `PS_*` form
> — they refer to the same specs.

---

## Warrior specializations (detailed)

> **Every default melee build is also a partial Ranger.** Profession levels come from each
> class's fixed **class-point** split (`existing_profs`, `profs.cpp:37`), and a prof's level
> scales with the **square root** of its points (`square_root[]` in `GET_PROF_COOF`, leveling in
> `limits.cpp:advance_mini_level`). Both the **Warrior** (`'w'` = warrior **100** / ranger **25**)
> and **Barbarian** (`'b'` = warrior **121** / ranger **25**) classes carry ranger 25 = 5², which
> is one-quarter of warrior's points, so ranger ends at **half the warrior level**: at character
> level 30 a default Warrior or Barbarian is **W30 / R15**. This is not an optional "dip" — it is
> inherent to the class, and **two warrior specs cash it in automatically**: **Defender** (block
> chance keys off `min(warrior, ranger)`) and **Light Fighting** (OB folds in `ranger/3`). The
> R15 figures are used in those sections below.

### Wild Fighting (`PS_WildFighting`) — the berserker / glass cannon
**Role:** all-out aggression that gets *stronger the closer you are to death*. Rewards the
Aggressive/Berserk stances; pairs offense with risk.
- **Rush** (`wild_fighting_handler::do_rush`/`get_rush_chance`): a chance each hit to "rush
  forward wildly" and add **+½ the hit's damage** (≈ ×1.5). Chance scales with tactics:
  **Berserk 15 %, Aggressive 10 %, Normal 5 %** (0 on defensive stances).
- **Rage at low health** (Berserk, ≤ 45 % HP, `get_attack_speed_multiplier`): bonus attack
  speed that **scales from +15 % at 45 % HP up to ~+59 % at 1 % HP** — the more wounded, the
  faster you swing. Entering rage broadcasts a message.
- **Bloodlust heal on kill** (Berserk, victim ≥ 60 % of your *capped* level, `on_unit_killed`):
  heal **10 % of your missing HP** on the kill — sustain that only pays out while you stay in
  Berserk and keep dropping meaningful targets.
- **Wild swing** (`SKILL_SWING`, Wild-Fighting–gated): a telegraphed swing worth **1.5 ×** a
  kick's `S(M)`; while in **Berserk at ≤ 25 % HP**, `get_wild_swing_damage_multiplier` multiplies
  it a further **×1.33** (`act_offe.cpp:907`). Full formula in `warrior-skills.md`.
- Keeps **75 %** of natural-attack damage past level 11 (vs 50 % for most specs, full for Light
  Fighting; `natural_attack_dam`, `fight.cpp:2351`).
- **Note** the engine of the spec is the *auto-attack*: `rush` and the rage attack-speed bonus
  both ride every swing, so Wild Fighting snowballs in drawn-out fights, not from a single button.

### Heavy Fighting (`PS_HeavyFighting`) — the armored juggernaut
**Role:** wear the heaviest armor and swing the heaviest weapons with far less penalty; tanky
bruiser. Turns "too heavy to use well" into a strength.
- **Worn-weight soft cap** (`char_utils.cpp:634`): for each slot, weight above a per-slot
  threshold counts at only **⅓** — heavy armor encumbers a heavy fighter much less, preserving
  OB/dodge/attack-speed that encumbrance would otherwise sap.
- **Encumbrance soft cap** (`char_utils.cpp:696`): per-slot encumbrance above a cap is clamped
  to the cap, and the excess is currently **discarded entirely** (the intended `sqrt`-of-excess
  re-add is commented out — `// Drelidan: Removing this for now`), so stacking heavy gear costs
  a heavy fighter almost no encumbrance.
- **+10 % armor damage absorption** (`fight.cpp:2181`): incoming damage reduced by an extra
  tenth of what armor already blocks.
- **+5 % weapon damage** with heavy weapons (bulk ≥ 3 and weight over `LIGHT_WEAPON_WEIGHT_CUTOFF`,
  `heavy_fighting_effect`, `fight.cpp:2224`): `damage/20` bonus on every qualifying swing.
- **+20 % active-skill damage** (`do_skill_attack`, `act_offe.cpp:917`): kick, wild swing — any
  standard `S(M)` skill (`warrior-skills.md`) — hits **20 % harder** (`dam += dam/5`). Heavy
  Fighting is the only warrior spec that buffs *both* the auto-attack (via absorption/weapon
  bonus) and the active strikes.

### Light Fighting (`PS_LightFighting`) — the agile, dexterity-based duelist
**Role:** the fast, evasive fighter who fights on **Dexterity** rather than Strength. Wants a
**light, one-handed weapon** and minimal armor; trades the raw power of Heavy/Wild Fighting for
speed, extra strikes, and turning ranger levels into offense.

A weapon counts as "light" for these bonuses when **bulk ≤ 2, or bulk 3 with weight ≤
`LIGHT_WEAPON_WEIGHT_CUTOFF`** (`utility.cpp:668`, `fight.cpp:2648`).

- **Dexterity drives OB** (`get_real_OB`, `utility.cpp:666-677`): with a light weapon the OB
  "offense stat" becomes **`max(bal_str, DEX)`** — so a high-DEX light fighter uses Dexterity
  in place of Strength for offense. *Additionally* their effective warrior level for OB gains
  **+⅓ of their ranger level** (`warrior_level += ranger/3`), so ranger levels contribute to OB.
  **This is free for the default class:** a level-30 Warrior/Barbarian carries **R15** (see the
  callout above), so a light fighter's OB is computed as if they were **warrior level 35**
  (`30 + 15/3`) before the DEX stat term. (Recall OB ≈ `(warrior·3 + 3·max_war·level/30)/2 +
  offense_stat` — stats §10.)
- **Dexterity drives damage (indirectly):** live melee damage scales with `(OB + 100)`
  (stats §10 / combat-loop), and Light Fighting routes DEX into OB — so a DEX-built light
  fighter's damage rides on Dexterity instead of Strength. (The small explicit `bal_str` term
  in the damage formula is unchanged, but the dominant scaling is the OB channel.)
- **Passively boosts every OB-based skill — including kick** (`warrior-skills.md`): the hit
  margin `M = skill − ½DB − ½PB + ½·OB + 1d100 − 120` carries a **+½·OB** term, and the payload
  `S(M) ∝ (100 + M)` rises with it. Because Light Fighting's DEX substitution + `ranger/3` raise
  the *same*
  `get_real_OB` that `get_prob_skill` feeds on, a light fighter's **kick** (and bash land-chance,
  wild swing, bite, rend) all hit harder **for free**, with no skill points spent — a quiet
  second dividend of the OB the spec is already buying. The catch is the same as everywhere: it
  only applies while holding a **light** weapon (a 2H/heavy weapon drops the OB substitution, so
  the skill bonus evaporates with it).
- **Double strike (still active):** Light Fighting retains a **~20 % chance per attack to
  strike a second time** (`can_double_hit`/`does_double_hit_proc`, applied in the round driver
  `fight.cpp:2761`). Requirements: Light-Fighting spec, a **one-handed light weapon** (no
  two-handers), and a valid target in the room. The bonus strike is a full `hit()` and is
  **free** — the character's energy is restored before it fires ("you find an opening… and
  strike again rapidly"), and the extra damage is tracked in `light_fighting_data`.
- **Worn-weight reduction** (`char_utils.cpp:661`): subtracts a per-slot amount from each worn
  item's weight (floored at 0), driving encumbrance toward nothing so the dodge/OB/speed
  penalties from gear largely vanish.
- **Full natural-attack damage:** exempt from the post-level-11 natural-attack damage cut that
  hits other specs (`fight.cpp:2351`).
- Best with light/no armor and a light one-hander; a two-handed or heavy weapon disables the
  DEX-OB substitution *and* the double strike.

### Weapon Master (`PS_WeaponMaster`) — the per-weapon specialist
**Role:** mastery of *whatever weapon you hold* — each weapon type grants a distinct package of
speed, damage, penetration, or crowd-control. Rewards picking the right weapon for the job
(`weapon_master_handler.cpp`).

| Weapon type | What mastery grants |
|-------------|---------------------|
| **Piercing** | +15 % attack speed; **25 %** chance to **ignore armor** |
| **Whipping** | +15 % attack speed; **40 %** chance to **ignore shields** |
| **Flailing** | +15 % damage; **40 %** chance to **ignore shields** |
| **Cleaving** (1H/2H) | +15 % damage; **50 %** chance to **re-roll the damage roll and keep the higher** |
| **Slashing** (1H/2H) | +5 OB, +5 PB; **40 %** chance to **regain energy** (momentum → faster next swing) |
| **Stabbing** (spear) | +10 PB; **50 %** chance to **punch through armor** |
| **Bludgeoning** (1H/2H) | +10 OB; **25 %** chance to **drain the victim's energy** (10× damage) — staggers them |
| **Smiting** | +10 OB; chance (`damage·0.5 %`) to inflict **HAZE** on the victim |

### Protection (`PS_Protection`) — the mitigation spec *(not a warrior spec)*
**Role:** damage avoidance and protective magic. The live combat hook is `get_evasion_malus`
(`fight.cpp:2328`), which is built entirely around **`PROF_CLERIC`** level and perception and
only fires while the target is under **`AFF_EVASION`** — so Protection reads as a **Cleric-side**
spec, not a warrior one. It also surfaces in caster contexts (`mage.cpp:644`, `mystic.cpp:542`).
Kept here only to complete the set; it belongs in the caster/cleric doc.
- **+3 to the evasion malus** *while `AFF_EVASION` is up* (`get_evasion_malus`, `fight.cpp:2328`):
  it widens the dodge bonus the EVASION affect already grants. It is **not** an always-on flat
  +3 dodge, and it does nothing without the EVASION effect active.
- Strengthens protective spell effects in the caster paths above (exact magnitudes → magic doc).
- ⚠️ The mirror in `combat_manager.cpp:237` is **dead code** — ignore it.

### Defender (`PS_Defender`) — the shield tank
**Role:** the survivability/peel specialist — soak hits behind a shield and protect allies.
Built around blocking and rescuing rather than dealing damage.
- **+10 % max HP** (`profs.cpp:732`) — a flat survivability bump on top of the HP formula (§6).
- **Shield block** (`defender_effect`, `fight.cpp:2263`): the block math **requires an equipped
  `ITEM_SHIELD`** — no race exception here, even a Beorning needs the shield for it to run. Each
  block removes **30 % of the incoming hit's damage**, from two independent rolls that can stack
  into a **60 % "critical block"**:
  1. **Passive block** — chance(%) = `max(warrior, ranger) level + min(warrior, ranger)/2`,
     rolled `number(0,100)`. The `min(warrior, ranger)` half-term means the **baked-in R15**
     (see the callout) is pure profit: a default **W30/R15** Defender blocks at **30 + 15/2 ≈
     37 %** per incoming hit, *not* 30 %. (A hypothetical pure W30 with no ranger would sit at
     30 %, but no default melee build is actually pure.)
  2. **`DEFEND` skill** — a Defender-only active ("hunker down behind your shield", `do_defend`,
     `act_offe.cpp:948`) that lays a temporary affect whose block chance(%) **equals your
     `DEFEND` knowledge**; it lasts ~**6 s** (2 fast-update ticks) on a ~**12 s** cooldown. A
     **Beorning** may *invoke* `DEFEND` without a shield (they "brace"), but since the reduction
     still runs through `defender_effect`'s shield check, that brace is mostly flavor unless a
     shield is worn.
- **Delay-free rescue** (`act_offe.cpp:784`): Defenders skip the `WAIT_STATE` that normally
  follows a `rescue`, so they can peel attackers off allies repeatedly — the core tank action.
- **Maul** is cheaper and stronger: energy cost **5 vs 10** (`act_offe.cpp:1300`) and a better
  damage-reduction profile (`maul_db 1.25 vs 2.0`, duration **6 vs 2**, `maul_damage_reduction`,
  `fight.cpp:1557`) — more mitigation, longer.
- Net: low personal damage, but the best raw survivability (HP + block + maul) and the only
  spec that can chain rescues without delay.

---

## Warrior play-styles — how the five specs actually play

The five warrior specs separate along three axes: **what stat carries you** (Strength vs
Dexterity), **how heavy your kit is** (plate-and-greatsword vs leathers-and-a-dagger), and
**what you do to the fight** (delete it, outlast it, control it, or anchor for the group). Read
against the skills in `warrior-skills.md` and the OB / HP math in
`stats-and-character-power.md`, they resolve into five genuinely distinct archetypes — not five
flavors of "hit it harder."

### At a glance
| Spec | Carries on | Wants (gear) | Tactics home | Damage ↔ Defense | Plays like |
|------|-----------|--------------|--------------|------------------|------------|
| **Wild Fighting** | Strength | any weapon, light armor | Berserk / Aggressive | ◆◆◆◆◇ offense | berserker executioner — snowballs as the fight drags and as *you* bleed |
| **Heavy Fighting** | Strength | heaviest armor **+** heaviest weapon | Normal / Aggressive | ◆◆◆◆ bruiser | armored juggernaut — bring the biggest kit, pay no weight tax |
| **Light Fighting** | **Dexterity** (+ free R15) | light 1H, minimal armor | Normal / Aggressive | ◆◆◆ evasive offense | agile duelist — speed, double-strikes, hit-and-run |
| **Weapon Master** | the weapon's stat | the *right* weapon (carry several) | tactics-agnostic | ◆◆◆ toolbox | the technician — match the weapon to the target |
| **Defender** | CON / HP | shield + warrior(/ranger) levels | Defensive / Normal | ◆◇ tank/support | shield wall — soak hits and peel for the group |

### Wild Fighting — risk-it-all aggression
You fight on **Strength** and you fight forward. The whole kit rewards **Berserk** (and
secondarily Aggressive): `rush` fires on a flat **15 % / 10 % / 5 %** of *auto-attacks* for a
free **+50 % damage**, so faster weapons that throw more swings see more rush procs. The
signature is the **comeback curve** — below 45 % HP in Berserk your attack speed ramps from
**+15 % up to ~+59 %** at death's door, the **wild swing** skill jumps **×1.33** under 25 %, and
killing a worthy target (≥ 60 % of your capped level) **heals 10 % of your missing HP**. The
bill for all of that is Berserk itself: parry and dodge are gutted (stats §10) and you **can't
flee**. It is the classic **glass cannon** — terrifying in a fight it can finish, and a corpse
in one it can't. **Race fit:** an **Olog-Hai** is the natural shell — `frenzy` *forces* Berserk
(turning on rush/rage for free), adds +10 % damage and crit auto-attacks, and stacks the Olog
smash/cleave/overrun kit (`warrior-skills.md`). **Skills it leans on:** auto-attacks first, then
wild swing; bite/rend keep 75 % scaling. **Best at:** solo execution and snowball duels; weakest
when forced to play patient or kite.

### Heavy Fighting — the immovable bruiser
The fantasy is "too heavy to use well" turned into a strength. A heavy fighter wears the
**heaviest armor** and swings the **heaviest weapon** (bulk ≥ 3, over the weight cutoff) and
pays almost none of the encumbrance that would sap everyone else's OB, dodge, and swing speed —
worn weight over a per-slot cap counts at **⅓**, and over-cap encumbrance is dropped outright.
On top of that it is flatly **tankier** (**+10 %** of whatever armor already absorbs) and hits
harder on **both** channels: **+5 %** weapon damage on heavy swings *and* **+20 %** on its
active skills (kick, wild swing). It has no low-HP gimmick and no stance tax — it just performs
at a high, durable baseline from Normal or Aggressive, which makes it the **most forgiving**
warrior spec and the natural **frontline anchor**. **Race fit:** anything big — **Olog-Hai**
(raw STR + bulk) is the obvious one. **Best at:** sustained main-tank/bruiser duty in groups and
attrition fights; it trades Wild Fighting's burst ceiling for a floor nobody else has.

### Light Fighting — the dexterity duelist
The only warrior spec that **abandons Strength**: wielding a **light one-hander** (bulk ≤ 2, or
bulk 3 under the cutoff) your offense stat becomes **`max(STR, DEX)`**, so a DEX build routes
Dexterity into OB — and since live melee damage rides `(OB + 100)`, DEX into your damage too. It
also folds **⅓ of your ranger level** into OB — and since the default Warrior/Barbarian already
carries **R15** at level 30, that is a free **+5 effective warrior levels** of OB before you
spend a thing, which in turn quietly raises every OB-based skill (your **kick** included). It
also grants a **~20 % chance to strike twice** per attack (free, energy refunded). Gear is the
inverse of Heavy Fighting: **light weapon, little armor**, with worn weight and encumbrance
shaved toward zero so dodge/speed stay high. Crucially it **does not want Berserk** — that would
gut the dodge the whole spec is built on — so it plays at Normal/Aggressive as a slippery,
high-evasion **duelist/skirmisher**. It also keeps **full** natural-attack damage. The catch:
put a two-hander or a heavy weapon in its hands and you lose **both** the DEX-OB substitution
**and** the double strike — it is the most gear-disciplined of the five. **Race fit:** high-DEX
races and ranger multiclassers; a **Beorning's** `swipe` bonus-attack shares the same proc slot
as the double strike, so a Beorning gets a bonus hit either way. **Build note:** the ranger
contribution is automatic for a Warrior/Barbarian (R15 baked in) — you don't multiclass for it,
you just don't waste it. **Best at:** hit-and-run, kiting, and 1-v-1s where evasion compounds;
punished by being forced to stand and trade.

### Weapon Master — the toolbox technician
Mastery of **whatever weapon you're holding** — each weapon *type* unlocks a different package
(see the table above), so the spec rewards **reading the fight and swapping weapons** rather than
committing to one stat or stance. Piercing/whips shred **armored or shielded** targets; cleaving
re-rolls for **burst**; flailing/cleaving add flat **damage**; bludgeon and smite bring
**control** (energy-drain stagger, HAZE); slashing gives **balance** (+OB/+PB and momentum
refunds). Bonuses are live and always-on through `get_real_OB` and the damage hooks, so there is
no ramp and no risk gimmick — your power is **your weapon knowledge and your kit breadth**. This
is the **highest-skill, most flexible** PvP pick and the most loadout-dependent: a master with
one weapon is a master in one situation. **Best at:** players who want to answer the *specific*
enemy in front of them; weakest when stuck with the wrong tool for the job.

### Defender — the shield wall
The survivability/peel specialist, built to **soak and protect** rather than deal damage.
**+10 % max HP**, a shield **block** that strips **30 %** of an incoming hit (two rolls can stack
to a **60 %** crit-block; passive chance ≈ **37 %** at a default W30/R15, plus the `DEFEND`
active), the
only **delay-free `rescue`** in the game (chain-peel attackers off allies with no recovery), and
a **cheaper, stronger `maul`** (5 vs 10 move; more reduction, longer) round out the most durable
warrior in the roster. The flip side is the lowest personal damage and a dependence on **having
allies to protect** — its best tools (rescue, peel) are wasted solo. **Race fit:** **Beorning**,
which can `brace` to use `DEFEND` without a shield and synergizes with the maul mitigation.
**Best at:** group frontline and ally protection; it is the clearest "support" warrior and the
one that most changes how a *party* fights rather than how *you* fight.

### Reading the axes together
- **Stat:** Wild / Heavy / Weapon Master scale on **Strength**; **Light Fighting** is the lone
  **Dexterity** path (and the one that turns the class's free **R15** into OB).
- **The free R15:** every default Warrior/Barbarian is **W30/R15** at level 30 (class-point
  math, callout above). Only two specs cash it in — **Defender** (≈37 % passive block instead of
  30 %) and **Light Fighting** (+5 OB warrior-levels → harder kicks/skills) — for everyone else
  those ranger levels are inert.
- **Gear weight:** **Heavy** and **Light** are mirror images — one removes the penalty for going
  *heaviest*, the other removes it for going *lightest*; **Weapon Master** cares about weapon
  *type* over weight; **Wild** and **Defender** are defined by stance and shield respectively.
- **Risk curve:** **Wild Fighting** is the only spec that gets *stronger as it dies* (and pays
  for it by being unable to flee); the rest deliver a steady profile.
- **Solo vs group:** **Defender** is the most group-defining (rescue/peel); **Wild** and
  **Light** are the strongest solo/duelists; **Heavy** anchors a line; **Weapon Master** flexes
  to either.
- **Not a warrior spec:** **Protection** lives here only for completeness — its live hook keys
  off `PROF_CLERIC` and the `AFF_EVASION` effect, so it belongs to the caster/cleric side.

---

## Ranger specializations (detailed)

> **None of the three ranger specs *gate* the core ranger skills.** Any ranger can hide, sneak,
> tame, calm, or shoot without a spec — the spec **amplifies** those skills and unlocks one extra
> ability each (Stalking, Whistle, shooting-speed control). The deep per-skill mechanics live in
> **[ranger-skills.md](ranger-skills.md)**; this section is the spec layer on top of them.

### Stealth (`PS_Stealth` / `PLRSPEC_STLH`, idx 6) — the assassin
**Role:** maximize concealment and the alpha strike. Turns a ranger's hide/sneak/ambush kit from
a utility into a kill opener.
- **+5 to `get_real_stealth`** (`utility.cpp:582`) — the only stealth-skill *spec* bonus. It feeds
  three things at once (see ranger-skills.md): the **hide concealment ceiling** (`hide_prof` is
  `SKILL_HIDE + get_real_stealth + RangerLevel − 30`), **ambush success**, and **sneak** success.
- **Ambush damage ×1.25 vs players / ×1.5 vs NPCs** (`ambush_calculate_damage`, `ranger.cpp:868`):
  `damage·10/8` (PC) or `damage·3/2` (NPC), applied before the soft cap. This is the single biggest
  ambush multiplier in the formula.
- **Room movement cost halved** (`get_room_move_penalty`, `act_move.cpp:74`): per-room move cost is
  `max(1, penalty/2)` while **not mounted** — a stealth ranger roams/kites for half the move points.
- **Snuck-in auto-hide wait halved** (`ranger.cpp:1895`): the small automatic hide that fires when
  you sneak into a room costs 50% of the normal wait, so you re-conceal almost instantly after moving.
- **+5 to *seeing* hiders** (`see_hiding`, `ranger.cpp:1968`): a stealth-spec seeker adds +5 to
  `can_see`, i.e. you're also better at **detecting other hidden characters** (counter-stealth).
- **Unlocks Stalking and Cover** (`SKILL_STALK`, `consts.cpp:426`, `learn_type 65`) — **Stalking**
  is the spec-only leave-no-tracks movement skill; **Cover** (`do_cover`, `ranger.cpp:1755`, command
  `CMD_COVER`) is a separate action that erases tracks you've *already* left. Cover has no skill of
  its own — it gates on your `SKILL_STALK` knowledge (`ranger.cpp:1771`), so the Stalking grant
  unlocks both.

### Animals (`PS_Animals` / `PLRSPEC_PETS`, idx 5) — the beastmaster
**Role:** field a stable of tamed animal followers and make them meaningfully stronger. The only
spec whose power is *other creatures*.
- **Calm +30** (`do_calm`, `ranger.cpp:1327`): `calm_skill += 30` — far more reliable pacification
  (calm succeeds on `calm_skill > 1d150`).
- **+1 effective level on calm and tame checks** (`ranger.cpp:1444`, `ranger.cpp:1503`):
  `levels_over_required += 1`, so you can calm/tame creatures one level higher than a non-spec ranger
  (on top of the ranger-level/skill terms in ranger-skills.md).
- **Stronger pets** (`do_tame`, `ranger.cpp:1620`): a successfully tamed pet permanently gains
  **+2 STR, +40 `ENE_regen`, +2 damage**. The bonus is persisted/re-applied on load
  (`objsave.cpp:840`) and cleanly removed if the pet stops following (`handler.cpp:990`).
- **4× pet move regen** (`limits.cpp:321`): every tame already regens move at ×2; if the master is
  Animals-spec it doubles **again** to ×4 — pets that effectively never tire.
- **Silent orders** (`do_order`, `act_offe.cpp:301`): commanding your pets isn't broadcast to the
  room, so you direct a menagerie without telegraphing it.
- **Unlocks Whistle** (`SKILL_WHISTLE`, `consts.cpp:425`, `learn_type 65`) — the spec-only zone-wide
  pet recall that also grants pets `AFF_HUNT` speed.

### Archery (`PS_Archery` / `PLRSPEC_ARCH`, idx 15) — the marksman
**Role:** sustained, efficient ranged DPS. Less about bigger hits, more about controlling cadence
and conserving ammunition.
- **Shooting-speed control is spec-only** (`do_shooting`, `act_othe.cpp:1169`): only an archery
  specialist may set **slow / normal / fast** shooting ("Only players specialized in archery may set
  their speed."). Non-spec rangers are locked to normal cadence. **This means the entire fast/slow
  tradeoff documented in [ranger-skills.md](ranger-skills.md) — slow = ×2 damage/×2 time, fast =
  ÷2/÷2 — is an Archery-spec exclusive.** It's the spec's defining lever: arrow-economy mode (slow)
  vs. more proc rolls (fast).
- **Arrow breakage halved** (`does_arrow_break`, `ranger.cpp:2172`): `break_percentage >>= 1` against
  metal/chain armor. Combined with slow-shooting's fewer shots, an archery specialist burns far
  fewer arrows — the spec is built around ammo sustainability. (Stacks with the Haradrim crude-arrow
  halving, `ranger.cpp:2168`.)
- **Score readout** (`act_info.cpp`): your current shooting mode is shown on `score` only with the
  spec — a cosmetic tell of the above.
- Note what archery spec does **not** do: it adds **no** per-arrow damage, accuracy, or armor-bypass
  bonus (those scale with ranger level + STR + Accuracy skill regardless — ranger-skills.md). Its
  value is cadence control + ammo economy + (via slow mode) burst.

### Ranger play-styles — at a glance
| Spec | Engine | Wants | Plays like |
|------|--------|-------|-----------|
| **Stealth** | hide → ambush alpha + mobility | light kit, terrain cover | assassin/skirmisher — open from concealment, halve your travel cost, re-hide instantly |
| **Animals** | tamed follower fleet | calm/tame targets + a zone of beasts | beastmaster — your damage and tankiness are your pets; whistle to mass them |
| **Archery** | ranged cadence + ammo economy | a good bow, arrow supply, a screen | kiting marksman — control fire rate, never run dry; wants to shoot before melee (shots are interrupt-cancelled — ranger-skills.md) |

---

## Mage specializations (summary)

Full per-spell damage/save numbers live in **[magic-system.md](magic-system.md)** (§3 save matrix,
§6 Battle-Mage, §7 spell table). Every elemental spec also gets a flat **−2 save bonus** when casting
its own element (`get_save_bonus`, `mage.cpp:1304`); `is_mage_spec()` (`structs.h:1477`) groups the
six elemental/battle specs for that matrix. Highlights, verified in `mage.cpp`:

- **Fire** (`PS_Fire`, 1) — Firebolt damage floored at caster level (`mage.cpp:1486`); Searing
  Darkness fire component **+50%** (`mage.cpp:1785`); spares friendly splash targets (`is_friendly_taget`).
  (The in-game `spec_tbl` help also claims Fire *increases Fireball damage* — that text is **stale**;
  the live Fire effect on Fireball is only the friendly-splash exemption, `mage.cpp:1834`-`1854`.)
- **Cold** (`PS_Cold`, 2) — Chill Ray an **extra −4** save (−6 total) (`mage.cpp:1379`); applies the
  **Chilled** energy-drain effect on a landed hit (failed save) — now on **both Chill Ray**
  (`mage.cpp:1384`) **and Cone of Cold** (`mage.cpp:1529`), in addition to Word of Agony
  (`apply_chilled_effect`, `mage.cpp:1348`).
- **Lightning** (`PS_Lightning`, 10) — Lightning Bolt **+10%** and ignores the indoor penalty
  (`mage.cpp:1426`); may cast Lightning Strike outside a storm at ×⅘ instead of being blocked
  (`mage.cpp:1737`).
- **Darkness** (`PS_Darkness`, 16) — Dark Bolt **+10%** (`mage.cpp:1459`), Spear of Darkness **+5%**
  (`mage.cpp:2134`), Searing Darkness dark component **+10%** (`mage.cpp:1778`); Black Arrow ignores
  the sun penalty (`mage.cpp:2035`).
- **Arcane** (`PS_Arcane`, 17) — the **universal** element: counts as your primary for offensive
  saves *and* as the opposing element on defense (`mage.cpp:1311`); no flat damage bonus of its own.
  **Casting-speed control is spec-only** (`do_casting`, `act_othe.cpp:1103`): only an arcane
  specialist may `set casting fast | normal | slow` ("Only players specialized in arcane may set
  their casting speed."). **Fast** halves a spell's cast time but **doubles** its mana cost;
  **slow** doubles the cast time but **halves** the mana cost; normal is the default. This is the
  mage mirror of Archery's shooting-speed lever (above) and is Arcane's defining in-combat tool —
  mana-economy mode (slow) vs. faster spells in a fight (fast).
- **Teleportation** (`PS_Teleportation`, 8) — **mage** spec: Blink range **5 vs 3** zones
  (`mage.cpp:924`), Relocate **+1** zone (`mage.cpp:1016`).
- **Battle Mage** (`PS_BattleMage`, 19) — melee/caster hybrid (`battle_mage_handler.cpp`): adds
  **`tactics/2 + mage_level/12`** to **both** `spell_power` and `spell_pen`; **cannot prepare
  spells** (`return !is_battle_spec`); and while at **≥ Aggressive tactics** rolls to **resist
  casting interruption** from damage/mental/armor (`base_chance + warrior/100 + mage/100 +
  tactics·2/100`). ⚠️ The flip side of that interruption check is engine-wide: for **anyone who is
  not** a battle-mage spec, `does_spell_get_interrupted()` returns **true** — which is exactly why a
  ranger's `shoot` (and any wait-wheel action) is auto-cancelled by damage (ranger-skills.md). See
  magic-system.md §6.

---

## Mystic specializations (summary)

Full detail in **[cleric-mystic-system.md §5](cleric-mystic-system.md)**; verified in `mystic.cpp`:

- **Regeneration** (`PS_Regeneration`, 3) — **+6** healing/regen level on Curing Saturation
  (`mystic.cpp:768`), Restlessness (`:802`), Vitality (`:854`), and the Regeneration power (`:950`),
  plus **+5** Cure Self (`mystic.cpp:530`) and **+10** Vitalize Self (`:743`).
- **Protection** (`PS_Protection`, 4) — **+1** Resist-Magic modifier (`mystic.cpp:543`); gates the
  Protection power; and has a **combat** hook — **+3** to the evasion malus while the target is under
  `AFF_EVASION` (`get_evasion_malus`, `fight.cpp:2328`). This is the spec listed under the warrior
  section for completeness, but it is a **mystic/cleric** spec.
- **Illusion** (`PS_Illusion`, 9) — **+6** effect level on Haze (`mystic.cpp:1136`), Fear (`:1184`),
  and Terror (`:1291`); **+1** Hallucinate charge (`:1059`); gates **Confuse**.
- **Guardian** (`PS_Guardian`, 11) — gates the Guardian summon (runtime check, `mystic.cpp:1627`):
  a charmed guardian in one of three builds — **aggressive** (`OB = 13·L/5`), **defensive**
  (high parry/dodge, `parry = 8 + 2L`), or **mystic** (high willpower) — all HP-scaling with mystic
  level (`mystic.cpp:1502`). **Orcs cannot pick this spec** (`act_othe.cpp:1855`).

---

## Open questions
- Per-slot values in the heavy/light `*_weight_table`/`*_encumb_table` (`char_utils.cpp`) — exact
  thresholds for the warrior weight/encumbrance soft caps.
- The **`IS_VULNERABLE(host, PLRSPEC_STLH)`** path in `see_hidden` (`spec_pro.cpp:2008`) halves a
  *seeker's* detection — it reads as a per-creature **vulnerability bitvector** keyed by spec index,
  not a Stealth-spec benefit. Where vulnerabilities are assigned (race? mob flags?) is unconfirmed.
- Whether any content actually grants/uses **`PS_Teleportation`** in the Expose-Elements spell
  selection (the case is missing from the selection switch — likely a minor code gap, `mage.cpp`).
