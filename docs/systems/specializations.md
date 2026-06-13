# Specializations

**Source files:** spec enum `game_types::player_specs` (`structs.h:811-832`); per-spec data
`specialization_data`/`*_spec_data` (`structs.h:1467+`, set in `char_utils.cpp:1486-1505`);
warrior handlers `wild_fighting_handler.cpp`, `weapon_master_handler.cpp`,
`warrior_spec_handlers.h`; combat hooks in `fight.cpp`, `combat_manager.cpp`,
`char_utils.cpp` (encumbrance), `profs.cpp` (HP). Active spec read via
`utils::get_specialization`.
**Status:** ✅ the five requested warrior-side specs detailed; others stubbed.

## What a specialization is
A character has **one active specialization** — a chosen focus layered on top of their
class-point build (§2 of `stats-and-character-power.md`) that reshapes how they fight or cast.
It's stored on the character and drives conditional bonuses throughout combat and magic
(extra damage, damage mitigation, procs, encumbrance handling, spell shaping). The full set
(`structs.h:811-832`):

```
PS_None
Warrior-side : PS_Defender, PS_WildFighting, PS_HeavyFighting, PS_LightFighting, PS_WeaponMaster
Defensive    : PS_Protection
Mage         : PS_Fire, PS_Cold, PS_Lightning, PS_Darkness, PS_Arcane, PS_BattleMage
Ranger/Mystic: PS_Regeneration, PS_Animals, PS_Stealth, PS_Archery, PS_Guardian,
               PS_Illusion, PS_Teleportation
```
(Profession groupings above are by observed usage; confirm against the spec-selection code
when documenting the stubbed ones.)

---

## Warrior specializations (detailed)

### Wild Fighting (`PS_WildFighting`) — the berserker / glass cannon
**Role:** all-out aggression that gets *stronger the closer you are to death*. Rewards the
Aggressive/Berserk stances; pairs offense with risk.
- **Rush** (`wild_fighting_handler::do_rush`/`get_rush_chance`): a chance each hit to "rush
  forward wildly" and add **+½ the hit's damage** (≈ ×1.5). Chance scales with tactics:
  **Berserk 15 %, Aggressive 10 %, Normal 5 %** (0 on defensive stances).
- **Rage at low health** (Berserk, ≤ 45 % HP, `get_attack_speed_multiplier`): bonus attack
  speed that **scales from +15 % at 45 % HP up to ~+59 % at 1 % HP** — the more wounded, the
  faster you swing. Entering rage broadcasts a message.
- **Bloodlust heal on kill** (Berserk, victim ≥ 60 % of your level, `on_unit_killed`): heal
  **10 % of your missing HP** on the kill.
- **Wild swing** (Berserk, ≤ 25 % HP, `get_wild_swing_damage_multiplier`): the *wild swing*
  skill hits for **×1.33**.
- Keeps **75 %** of natural-attack damage past level 11 (vs 50 % for most specs;
  `fight.cpp:2351`).

### Heavy Fighting (`PS_HeavyFighting`) — the armored juggernaut
**Role:** wear the heaviest armor and swing the heaviest weapons with far less penalty; tanky
bruiser. Turns "too heavy to use well" into a strength.
- **Worn-weight soft cap** (`char_utils.cpp:634`): for each slot, weight above a per-slot
  threshold counts at only **⅓** — heavy armor encumbers a heavy fighter much less, preserving
  OB/dodge/attack-speed that encumbrance would otherwise sap.
- **Encumbrance soft cap** (`char_utils.cpp:696`): per-slot encumbrance above a cap is clamped,
  again cutting the penalty for stacking heavy gear.
- **+10 % armor damage absorption** (`fight.cpp:2181`): incoming damage reduced by an extra
  tenth of what armor already blocks.
- **+5 % weapon damage** with heavy weapons (bulk ≥ 3 and weight over the cutoff,
  `heavy_fighting_effect`, `fight.cpp:2224`): `damage/20` bonus.

### Light Fighting (`PS_LightFighting`) — the agile, dexterity-based duelist
**Role:** the fast, evasive fighter who fights on **Dexterity** rather than Strength. Wants a
**light, one-handed weapon** and minimal armor; trades the raw power of Heavy/Wild Fighting for
speed, extra strikes, and turning ranger levels into offense.

A weapon counts as "light" for these bonuses when **bulk ≤ 2, or bulk 3 with weight ≤
`LIGHT_WEAPON_WEIGHT_CUTOFF`** (`utility.cpp:668`, `fight.cpp:2648`).

- **Dexterity drives OB** (`get_real_OB`, `utility.cpp:666-677`): with a light weapon the OB
  "offense stat" becomes **`max(bal_str, DEX)`** — so a high-DEX light fighter uses Dexterity
  in place of Strength for offense. *Additionally* their effective warrior level for OB gains
  **+⅓ of their ranger level** (`warrior_level += ranger/3`), so ranger investment contributes
  to OB. (Recall OB ≈ `(warrior·3 + 3·max_war·level/30)/2 + offense_stat` — stats §10.)
- **Dexterity drives damage (indirectly):** live melee damage scales with `(OB + 100)`
  (stats §10 / combat-loop), and Light Fighting routes DEX into OB — so a DEX-built light
  fighter's damage rides on Dexterity instead of Strength. (The small explicit `bal_str` term
  in the damage formula is unchanged, but the dominant scaling is the OB channel.)
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

### Protection (`PS_Protection`) — the mitigation/defensive spec
**Role:** damage avoidance and protective magic. Appears in caster contexts (`mage.cpp:644`,
`mystic.cpp:542`) as well as combat, so it likely belongs to a caster profession rather than
pure Warrior — confirm when detailing the caster specs.
- **+3 evasion/dodge** against incoming attacks (`combat_manager.cpp:237`, `fight.cpp:2328`).
- Strengthens protective spell effects in the caster paths above (exact magnitudes → magic doc).

### Defender (`PS_Defender`) — the shield tank
**Role:** the survivability/peel specialist — soak hits behind a shield and protect allies.
Built around blocking and rescuing rather than dealing damage.
- **+10 % max HP** (`profs.cpp:732`) — a flat survivability bump on top of the HP formula (§6).
- **Shield block** (`defender_effect`, `fight.cpp:2263`): requires a shield equipped (Beornings
  can brace without one). Each block removes **30 % of the incoming hit's damage**, from two
  independent sources that can stack to a **60 % "critical block"**:
  1. **Passive block** — chance = `max(warrior, ranger) level + min(warrior, ranger)/2` (so a
     pure W30 ≈ 30 % per hit; mixing in ranger levels raises it).
  2. **`DEFEND` skill** — a Defender-only active ("hunker down behind your shield",
     `act_offe.cpp:954`) that applies an extra block-chance affect (chance = the affect's
     modifier) for a time.
- **Delay-free rescue** (`act_offe.cpp:784`): Defenders skip the `WAIT_STATE` that normally
  follows a `rescue`, so they can peel attackers off allies repeatedly — the core tank action.
- **Maul** is cheaper and stronger: energy cost **5 vs 10** (`act_offe.cpp:1300`) and a better
  damage-reduction profile (`maul_db 1.25 vs 2.0`, duration **6 vs 2**, `maul_damage_reduction`,
  `fight.cpp:1557`) — more mitigation, longer.
- Net: low personal damage, but the best raw survivability (HP + block + maul) and the only
  spec that can chain rescues without delay.

---

## Non-warrior specializations (stubs — to be detailed)

> Each needs its own pass against the magic / skills systems. Captured here so the set is
> complete.

**Mage (elemental & hybrid)** — shape the mage's offensive magic; data classes in
`structs.h:1481-` and `char_utils.cpp:1486+`:
- **Fire** (`PS_Fire`), **Cold** (`PS_Cold`), **Lightning** (`PS_Lightning`),
  **Darkness** (`PS_Darkness`), **Arcane** (`PS_Arcane`) — element-themed spell specialists
  (e.g. `cold_spec_data` tracks chill/energy-sap state). ⬜
- **Battle Mage** (`PS_BattleMage`) — melee-capable caster hybrid. ⬜

**Ranger / Mystic (utility & support)** — profession mapping to confirm:
- **Regeneration** (`PS_Regeneration`), **Animals** (`PS_Animals`),
  **Stealth** (`PS_Stealth`), **Archery** (`PS_Archery`), **Guardian** (`PS_Guardian`),
  **Illusion** (`PS_Illusion`), **Teleportation** (`PS_Teleportation`). ⬜

## Open questions
- **How a spec is chosen/changed** (level requirement, command, profession gating) — trace the
  spec-selection code and confirm the warrior/mage/ranger/mystic groupings above.
- Concrete magnitudes for the **Protection** caster bonuses and all **stubbed** specs.
- Per-slot values in the heavy/light `*_weight_table`/`*_encumb_table` (`char_utils.cpp`).
