# Combat stat examples — how stats move the numbers

Concrete worked comparisons of three warrior-leaning builds, and how each derived value
responds to a one-point stat change. Formulas and source citations are in
[`stats-and-character-power.md` §6, §10](stats-and-character-power.md) and
[`combat-loop.md`](combat-loop.md); this doc only plugs in numbers.

## Assumptions (held constant unless noted)
- **Character level 30**, **Human** (max class level 30, STR cap 22), **Normal** tactics.
- **Baseline stats: STR 20, CON 18, DEX 14** (all below caps, so `bal_str = STR`).
- No encumbrance (`skill_penalty = dodge_penalty = 0`), no daylight/confuse modifiers,
  `points.OB/parry/dodge = 0`.
- Representative gear/skills (needed only for *absolute* numbers; the **deltas** in the
  sensitivity tables are independent of these except energy regen):
  one-handed weapon, weight 150, **bulk 3**, parry rating `value[1] = 20`;
  weapon knowledge 80, parry knowledge 80, attack skill 80, dodge 60, stealth 40.
- `constHit ≈ 70` at level 30 (base 10 + ~2/level; it's random — see §4).

## The three builds
Point spend → class levels at L30 (`class_level = 3·√points`, §2):

| Build | M / C / R / W points | Class levels (M/C/R/W) | `class_HP` |
|-------|----------------------|------------------------|-----------:|
| **Default Warrior** (preset `w`) | 9 / 16 / 25 / 100 | 9 / 12 / 15 / 30 | √366·200 ≈ **3826** |
| **Barbarian** (preset `b`) | 0 / 4 / 25 / 121 | 0 / 6 / 15 / 33 | √417·200 ≈ **4084** |
| **All-in W36** | 0 / 0 / 0 / 150 | 0 / 0 / 0 / 36 | √450·200 ≈ **4243** |

## Baseline derived values (STR 20 / CON 18 / DEX 14)
Using the §6/§10 formulas:

| Value | Formula (at these assumptions) | Default W (W30,R15) | Barbarian (W33,R15) | All-in (W36,R0) |
|-------|--------------------------------|--------------------:|--------------------:|----------------:|
| **Max HP** | `40 + 0.9·constHit + class_HP·0.08143` | ≈ **416** | ≈ **437** | ≈ **449** |
| **OB** | `bal_str + 1.5·W + 45 + 14.7` | ≈ **124.7** | ≈ **129.2** | ≈ **133.7** |
| **PB** | `(2·W + 30 + bal_str)·0.5 + 33.6` | ≈ **88.6** | ≈ **91.6** | ≈ **94.6** |
| **DB** | `DEX + 0.7·R + 6.5` | ≈ **31.0** | ≈ **31.0** | ≈ **20.5** |
| **Energy regen** (attack speed) | harmonic(str_speed, null_speed²), §6 | ≈ **156** | ≈ **156** | ≈ **156** |
| Mana pool | `constMana + INT + WIL/2 + 2·M` | highest (M9) | low (M0) | lowest (M0) |

Takeaways:
- The **all-in W36** wins OB (+9 over default) and PB (+6) and HP (+33), but pays for it with
  **much lower DB** (no ranger levels: 20.5 vs 31) and **no mana/utility**. Energy regen is
  identical here because it depends on STR/DEX/weapon, not class points.
- The **Barbarian** sits between: 3 more warrior levels than default (+4.5 OB, +3 PB, slightly
  more HP from `class_HP`) while keeping ranger 15 for the same DB as the default warrior.

## Sensitivity — what one extra stat point does (Default Warrior, baseline)
"Exact" = independent of the gear/skill assumptions; "example" = depends on the weapon/skills
chosen above.

| Change | Max HP | OB | PB | DB | Energy regen | ~damage/hit |
|--------|-------:|---:|---:|---:|-------------:|------------:|
| **+1 STR** (20→21) | — | **+1.0** (exact) | **+0.5** (exact) | — | ≈ +1.7 (example) | **≈ +0.7 %** |
| **+1 CON** (18→19) | **≈ +11.7** (≈ constHit/20 + class_HP·0.00214) | — | — | — | — | — |
| **+1 DEX** (14→15) | — | — | — | **+1.0** (exact) | ≈ +1.3 (example) | tiny (via speed) |

Notes:
- **STR** is the broad combat stat: +1 OB *and* +0.5 PB *and* faster swings *and* ~+0.7 %
  damage per landed hit (via `remaining_ob`, see combat-loop) *and* fewer misses against dodge
  and parry — but **half-rate above STR 22** (`bal_str`). At baseline 20 you're still 1:1.
- **CON** buys ≈ **12 HP per point** here (more for the all-in build: `class_HP` is larger, so
  the `class_HP·0.00214` term grows — ≈ +12.6 HP/CON for W36). It does nothing for OB/PB/DB.
- **DEX** is the defensive/speed stat: +1 DB per point (1:1) and faster attacks; no direct
  offense. Its energy-regen value diminishes as DEX rises (the speed terms combine through a
  harmonic mean and a square root, §6).
- **Warrior class level** (build choice, not a stat): +1.5 OB and +1 PB per level — which is
  why W36 leads W30 by ≈ +9 OB / +6 PB. **Ranger class level**: +0.7 DB per level — why the
  all-in build's DB collapses.

## How to regenerate these numbers
Plug your real stats, class levels, weapon (`value[1]` parry, bulk, weight), and knowledge
skills into the §6/§10 formulas. The **deltas** marked "exact" hold for any character below the
caps on Normal tactics; switch tactics by applying the `*_tactics_modifier` multipliers from
`char_utils_combat.cpp`. Energy-regen and the absolute OB/PB/DB constants shift with gear and
skills, so treat the absolute columns as illustrative and the deltas as the durable result.
