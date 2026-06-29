# Weapons

**Source files:** weapon-type enum `structs.h:252` (`game_types::weapon_type`); value-field accessors
`structs.h:382-385` (`get_ob_coef`/`get_parry_coef`/`get_bulk`/`get_weapon_type`); type→skill
`spells.h:269` (`weapon_skill_num`); type→name `object_utils.cpp:259` (`get_weapon_name`); derived
damage `object_utils.cpp:208` (`get_weapon_damage`) + per-type tilt `object_utils.cpp:98`
(`get_weapon_type_modifiers`) + bow damage `:193`; live OB/parry use of weapon stats
`utility.cpp:647` / `:761`; attack speed `profs.cpp:757-806`; damage-type/message `fight.cpp:1998`
(`weapon_hit_type`); armor interaction `fight.cpp:2155-2218` (`armor_effect`); spec procs
`weapon_master_handler.cpp`.
**Status:** ✅ value fields, the full weapon-type roster, each type's intrinsic properties,
bulk/weight, handedness, and weapon-type × spec/race fit.

> **Scope.** This is the builder/type reference. The **combat math** — how a swing rolls OB vs.
> dodge/parry and how `get_weapon_damage` becomes final damage — lives in
> [combat-loop.md](combat-loop.md); the combat **specializations** are
> [specializations.md](specializations.md); the **racial** weapon affinities are
> [races.md](races.md); archery/ambush detail is [ranger-skills.md](ranger-skills.md). This doc owns
> the part that's "automatically true of a weapon because of its **type**."

---

## 0. What a builder sets — the weapon object fields

An `ITEM_WEAPON` is four numbers plus bulk/weight/level (`structs.h:382-385`):

| Field | Accessor | Meaning | What it drives |
|---|---|---|---|
| `value[0]` | `get_ob_coef()` | **OB bonus** | Added straight to your **OB** when you wield (`handler.cpp:1335`); *also* lowers derived damage (the trade-off below) |
| `value[1]` | `get_parry_coef()` | **Parry bonus** | Added to your **parry** when you wield (`handler.cpp:1336`) and again in the parry skill term (`get_real_parry`, `utility.cpp:786`); *also* lowers derived damage |
| `value[2]` | `get_bulk()` | **bulk** (0–8 in live data; damage peaks at 3; **no code cap**) | attack **speed**, OB term, damage, handedness threshold |
| `value[3]` | `get_weapon_type()` | **weapon type** (the enum below) | skill used, damage type/message, and every intrinsic effect in §1 |
| *weight* | `get_weight()` | physical weight | attack **speed** (with bulk) |
| *level* | `get_level()` | item level | raises derived damage |
| `value[4]` | — | legacy | unused by the formula |

> **OB / parry vs. damage — the power budget.** `value[0]` (OB) and `value[1]` (parry) are added
> directly to your **real** OB and parry the moment you wield (`handler.cpp:1334-1336`; the in-game
> `stat` display even labels them "OB" and "Parry", `act_wiz.cpp:643`). The **same two numbers also
> feed the damage formula inversely** — `damage ∝ (40 + level − parry) · (50 − OB) · …`
> (combat-loop.md) — so a weapon that grants a lot of OB and/or parry **does correspondingly less raw
> damage**. Offense (`value[0]`), defense (`value[1]`), and damage are three ways to spend the same
> budget: a builder makes an **offensive** weapon (e.g. a whip) by loading **`value[0]` (OB)** and a
> **defensive** one (e.g. a sword) by loading **`value[1]` (parry)**. Damage itself is **derived**,
> never a field. *(Bulk and weapon-skill add still more OB in `get_real_OB`; `value[1]` adds to parry
> a second time in the parry skill term — so the two coefficients are the weapon's baseline, not its
> only, OB/parry contribution.)*

---

## 1. The weapon types — what each type does automatically

`value[3]` selects the type. Each type fixes the **governing skill** (which weapon-skill knowledge
feeds your OB/parry), the **melee damage type** (`weapon_hit_type`, the room message + the armor
special-cases in §5), the **handedness**, and an **intrinsic combat tilt**.

| `value[3]` | Type | Name shown | Skill | Melee damage type | Intrinsic property |
|--:|---|---|---|---|---|
| 2 | `WT_WHIPPING` | whipping | **Whip** | `TYPE_WHIP` | *damage-only* tilt: +8 parry / −5 OB in the dmg formula (≈ slightly less damage) — **not** a real defense bonus; see note |
| 5 | `WT_FLAILING` | flailing | **Whip** | `TYPE_FLAIL` | neutral coefs |
| 3 | `WT_SLASHING` | slashing | **Slash** | `TYPE_SLASH` | *damage-only* tilt: −2 parry in the dmg formula (≈ slightly more damage) — see note |
| 4 | `WT_SLASHING_TWO` | two-handed slashing | **Slash** | `TYPE_SLASH` | as slashing, **two-handed** |
| 6 | `WT_BLUDGEONING` | bludgeoning | **Concussion** | `TYPE_CRUSH` | *damage-only* tilt: +3 parry in the dmg formula (≈ slightly less damage) — see note |
| 7 | `WT_BLUDGEONING_TWO` | bludgeoning | **Concussion** | `TYPE_BLUDGEON` | as bludgeoning, **two-handed** |
| 8 | `WT_CLEAVING` | cleaving | **Axe** | `TYPE_CLEAVE` | neutral coefs |
| 9 | `WT_CLEAVING_TWO` | two-handed cleaving | **Axe** | `TYPE_CLEAVE` | as cleaving, **two-handed** |
| 10 | `WT_STABBING` | stabbing | **Spears** | `TYPE_SPEARS` | **intrinsic armor penetration** (§5) — spears bypass ~⅓ extra armor |
| 11 | `WT_PIERCING` | piercing | **Pierce** | `TYPE_PIERCE` | neutral coefs (light/fast in practice) |
| 12 | `WT_SMITING` | smiting | **Concussion** | `TYPE_SMITE` | **crushes rigid-metal armor** (§5) |
| 13 | `WT_BOW` | bow | **Archery** | (ranged) | ranged; STR-independent damage |
| 14 | `WT_CROSSBOW` | crossbow | **Archery** | (ranged) | ranged; STR-independent damage |

(`0`/`1` are unused error types. Note several *types* share one *skill*: whip & flail both train
**Whip**; bludgeoning & smiting both train **Concussion**; the `_TWO` variants train the same skill
as their one-handed sibling. Only slashing, bludgeoning, and cleaving have a dedicated `_TWO` type —
**whipping and flailing have no two-handed variant** (any `bulk ≥ 4` weapon can still be gripped two
hands via `twohand`, §3). Builder data note: types **13/14 (bow/crossbow) are documented and coded but
have *zero* weapon objects in the live world** — the archery path has no deliverable weapon yet.)

**The per-type tilt is a *damage-only* nudge — do not read it as the weapon's role.** The switch in
the **live** `get_weapon_damage` (`utility.cpp:469-484`) adjusts the parry/OB coefficients **used to
compute damage**, and nothing else — it never touches your real OB or parry. In the live path a
**whip** computes as if +8 parry / −5 OB and a **bludgeon** as if +3 parry (both ⇒ a few-percent
*less* damage), while **slashing** computes as if −2 parry (⇒ a touch *more*); all other types are
neutral. A weapon's actual offensive/defensive character is its **builder-set `value[0]`/`value[1]`**
(§0): conventionally whips are built **offensive** (high OB) and swords **defensive** (high parry) —
the *opposite* of what this damage tilt's signs imply, which is exactly why it reads backwards. It is
a small balancing factor layered on top of the OB/parry budget, not a statement that whips parry well.
**But type absolutely *does* move your real OB and parry — through the *governing skill* it selects
(and Weapon-Master bonuses), not through this damage switch. See §1.1.**
(Heads-up: a second, **dead** copy of this switch — `object_utils.cpp:98`, reached only by the dead
`combat_manager.cpp` — uses the *opposite* sign for bludgeoning, `−3`. The live one is `utility.cpp`;
see §6.)

**Two intrinsic armor effects** (these are true for *anyone* wielding the type, not just a spec):
- **Spears (`TYPE_SPEARS`) punch through armor.** In `armor_effect` the armor's absorption is divided
  by **150 instead of 100** (`fight.cpp:2163-2165`: "Spears hit the armor, but then go right through
  it"), so ~⅓ of the armor's mitigation is ignored. (A Weapon-Master spear *proc* doubles that to /300.)
- **Smiting (`TYPE_SMITE`) shatters plate.** Against **rigid-metal armor** (`material == 4`) a smite
  has a **20 % chance** to add back **2× the blocked amount** as damage (`fight.cpp:2205` — "bones
  crunch"). It does nothing extra vs. chain/leather/mithril.

### 1.1 How weapon type reaches OB, parry, damage & speed

Weapon type is **not** cosmetic and it is **not** just a damage knob — it is a first-class lever on
all four combat numbers. The single biggest *intrinsic* channel (no spec required) is the **governing
skill**:

> **Governing skill → your OB *and* parry.** The type picks which weapon skill applies
> (`weapon_skill_num`: slash, whip, axe, spears, pierce, concussion, archery), and **that skill's
> knowledge scales both stats**. OB gains `weapon_skill · (bulk + 20) · tactics / 1000`
> (`utility.cpp:756`); parry uses `tmpskill · (value[1] + 20) · (14 − tactics) / 1000`, where
> `tmpskill` blends the weapon skill **¼** with your general `SKILL_PARRY`
> (`utility.cpp:788-833`). So for a given fighter, a weapon of a type they've **trained** delivers
> materially more OB and parry than an untrained type — "wield the weapon you've skilled" is a real,
> intrinsic OB/parry swing, not flavor.

Every channel type touches (✓ = affects; **intrinsic** applies to everyone, the rest are conditional):

| Channel | OB | Parry | Damage | Speed | Scope |
|---|:-:|:-:|:-:|:-:|---|
| **Governing skill** (type → `weapon_skill_num`) | ✓ | ✓ | — | — | intrinsic |
| **Per-type damage tilt** (whip/slash/bludgeon, §1) | — | — | ✓ | — | intrinsic |
| **Spear armor-pen / smite-crush** (§5) | — | — | ✓ vs armor | — | intrinsic |
| **Two-handed** (`*_TWO`, §3) | ✓ | ✓ | ✓ | ✓ ×2 | intrinsic |
| **Ranged** (bow/crossbow, §3) | ✓ | (no parry) | ✓ STR-free | — | intrinsic |
| **Bulk** (`value[2]`, type-correlated, §2) | ✓ | — | ✓ | ✓ | intrinsic |
| **Weapon Master** (§4) | ✓ | ✓ | ✓ | ✓ | spec |
| **Light / Heavy Fighting** (bulk-gated, §4) | ✓ | — | ✓ | — | spec |
| **Dwarf + axe / Haradrim + spear** (§4) | — | — | — | ✓ | race |

So the long-standing builder rule of thumb is correct: **a weapon's *type* shapes its OB, parry,
damage, and speed** — the `value[0]`/`value[1]` fields set the flat baseline, and the type (via the
governing skill, handedness, bulk, the armor special-cases, and the spec/race hooks) determines how
much of it actually lands and on which axis. The damage-only coef tilt in §1 is just one small piece
of that, and the one most easily misread.

---

## 2. Bulk & weight — the speed ↔ power dial

`bulk` (`value[2]`) and `weight` are the builder's main lever; they trade attack speed against
per-hit power (the full speed math is `recalc_abilities`, `profs.cpp:757-806`; the damage side is
combat-loop.md):

- **Attack speed** ≈ `BAL_STR · 2500000 / (weight · (bulk + 3))` — **heavier/bulkier ⇒ slower swings**
  (lower `ENE_regen`). Two-handed **doubles** it; below `bulk 4` a DEX term blends in (so light
  weapons can be DEX-driven — relevant to Light Fighting and low-STR/high-DEX races, races.md).
- **Per-hit damage rises** as the weapon gets bulky/slow (the `/energy_regen` term), but the
  `(20 − |bulk − 3|)` term peaks at **bulk 3** and falls off linearly either side — so very low or very
  high bulk both shave damage off that factor. The fall-off is **not** trivial at the top: there is no
  code cap on bulk and live two-handers reach **bulk 8**, where that term keeps only `(20 − 5)/20` ≈
  **75 %** of its peak. Net: a heavy weapon hits harder but less often.
- **OB shifts with bulk** in `get_real_OB`: a **one-handed** weapon pays `−(bulk·2 − 6)` (so bulk 1 →
  **+4 OB**, bulk 3 → 0, bulk 5 → **−4 OB**, bulk 8 → **−10 OB**), while **two-handed/ranged** weapons
  get a bulk *bonus* `bulk·(200 + skill)/100 − 15`. Weapon-skill knowledge then scales as
  `skill·(bulk + 20)·tactics/1000`.

**Thresholds builders should know:** `bulk ≤ 2` (or `bulk == 3` with weight ≤ `LIGHT_WEAPON_WEIGHT_CUTOFF`)
= "light" → enables **Light Fighting** bonuses; `bulk ≥ 3` with weight above that cutoff = "heavy" →
enables **Heavy Fighting** bonuses (§4). `bulk ≥ 4` is the threshold to wield two-handed. There is **no
upper cap** — about a quarter of live weapons run **bulk 6–8** (the heaviest two-handers), so don't read
the "neutral ≈ 3" peak as a ceiling.

---

## 3. Handedness — one-handed, two-handed, ranged

- **Inherently two-handed types** are `WT_SLASHING_TWO` / `WT_BLUDGEONING_TWO` / `WT_CLEAVING_TWO`.
  Any `bulk ≥ 4` weapon can also be gripped two-handed via the `twohand` command (sets
  `AFF_TWOHANDED`). Two-handed grants: **×2 swing speed** (`profs.cpp:772`), an **OB bonus** scaled by
  `SKILL_TWOHANDED` (`utility.cpp:700`), **+parry** = `value[1]/2` (`utility.cpp:835`), and **×2 the
  STR term** in the damage formula (combat-loop.md). Gripping a *small* one-hander with two hands
  risks a wasted-energy penalty (`check_grip`). This is the build the **Dwarf** is shaped for — its
  DEX−3 barely slows a high-bulk two-hander (races.md §3).
- **Ranged types** (`WT_BOW`/`WT_CROSSBOW`) use **Archery**, fire through the shoot/archery path
  (ranger-skills.md), and have a **separate, STR-independent damage** formula:
  `min(weapon_level, owner_level)/3 + OB_coef + bulk` (`get_bow_weapon_damage`, `object_utils.cpp:193`).
  Their OB blends weapon-skill with Archery 50/50. They can't parry in melee. **Wood-Elf** and
  **Haradrim** shoot a beat faster (races.md). *(Data caveat: the ranged code paths exist, but **no
  bow or crossbow weapon object currently ships in the world** — zero `ITEM_WEAPON`s of type 13/14
  across `lib/world/obj/`. Archery is wired up with no deliverable weapon yet.)*

---

## 4. Weapon type × specialization & race — what's good for what

Beyond the intrinsic effects, several **specializations** key off weapon type/bulk (so the "right"
weapon depends on your spec):

| Spec | Weapon it rewards | Effect |
|---|---|---|
| **Weapon Master** (`weapon_master_handler.cpp`) | *flat per-type bonuses + a per-type proc* | **+OB** (`get_bonus_OB`): bludgeoning/smiting **+10**, slashing **+5**. **+Parry** (`get_bonus_PB`): stabbing **+10**, slashing **+5**. **Speed**: piercing/whipping **×1.15** (`get_attack_speed_multiplier`). **Damage**: cleaving/flailing **+15 %** (`get_total_damage`). **Procs**: pierce → ignore armor; spear → double armor-pen; flail/whip → ignore shields; cleaving → reroll damage higher; slashing → refund energy; bludgeoning → burn victim energy; smiting → haze. (All gated on the Weapon-Master spec.) |
| **Light Fighting** | light weapons (`bulk ≤ 2`, or `3` & light) | OB uses **DEX** (not STR) + `ranger/3`; a **double-hit** proc (`utility.cpp:666`, `fight.cpp` `can_double_hit`). Favors whip / piercing / stabbing |
| **Heavy Fighting** | heavy weapons (`bulk ≥ 3` & heavy) | **+5 % damage** per hit (`fight.cpp:2222`), +10 % HP, +10 % armor absorb. Favors the two-handed types |
| **Defender** | (needs a **shield**, not a weapon) | shield block layer — orthogonal to weapon type |

**Racial weapon affinities** (races.md): **Dwarf + axe** → bonus energy-regen (`profs.cpp:791`);
**Haradrim + spear** → bonus energy-regen + may ambush with heavy spears (`:793`, `ranger.cpp:925`);
**Wood-Elf / Haradrim + bow** → −1 archery beat.

So: a **two-handed cleaving axe** suits a Dwarf Heavy-Fighter; a **light piercing/whip** weapon suits
a Light Fighter or a DEX race; a **spear** is the natural anti-armor pick (and a Haradrim's signature);
a **bow** is the Wood-Elf/Haradrim Archer's; a **Weapon Master** picks the type whose proc they want.

---

## 5. Damage type & armor (recap)

`weapon_hit_type` (`fight.cpp:1998`) maps `value[3]` → the `TYPE_*` used for the room message **and**
the armor special-cases:

| `value[3]` | `TYPE_*` | Verb | Armor interaction |
|--:|---|---|---|
| 0–2 | `TYPE_WHIP` | whip | normal |
| 3–4 | `TYPE_SLASH` | slash | normal |
| 5 | `TYPE_FLAIL` | flail | normal (WM: ignore shields) |
| 6 | `TYPE_CRUSH` | crush | normal |
| 7 | `TYPE_BLUDGEON` | pound | normal |
| 8–9 | `TYPE_CLEAVE` | cleave | normal |
| 10 | `TYPE_SPEARS` | stab | **÷150 armor** (intrinsic pen); WM proc ÷300 |
| 11 | `TYPE_PIERCE` | pierce | normal (WM: ignore most armor) |
| 12 | `TYPE_SMITE` | smite | **20 % bone-crush vs. rigid-metal** armor |
| 13–14 | `TYPE_BLUDGEON`/`TYPE_WHIP` | (ranged) | ranged; see §3 |

On the defensive side, the **victim's** Heavy-Fighting spec adds +10 % armor absorption
(`fight.cpp:2181`), and physical types route through `check_resistances` (Wild resist/vuln). The full
armor-subtraction step and which swings even reach armor are in combat-loop.md.

---

## 6. Builder cheat-sheet & open questions

**Pick the *type* for its skill/damage-type/special behavior, then tune offense vs. defense vs. damage
with the value fields:** raise **`value[0]`** for a high-OB (offensive) weapon, **`value[1]`** for a
high-parry (defensive) one, and remember both *cost* derived damage. By type: a hard-hitting two-hander
→ a `*_TWO` type at **bulk 4–8** (live two-handers span that whole range; bulk 6–8 are common for the
heaviest); an armor-shredder → **stabbing** (spear); an anti-plate weapon → **smiting**; a fast DEX
weapon → low-bulk **piercing/whipping**; a bow → **bow/crossbow** (set `value[0]` + bulk + level; STR is
irrelevant — note **no bow/crossbow object exists in the world yet**, so you'd be building the first).
Damage is *derived*, never set directly.

**Flags / quirks for maintainers:**
- **`weapon_hit_type` case 13 (bow) is missing a `break`** (`fight.cpp:2033-2037`) — it falls through
  to case 14, so a bow's melee `TYPE_` resolves to `TYPE_BLUDGEON`, not `TYPE_WHIP`. Bows normally use
  the ranged path, so it rarely bites, but it's a bug.
- **`weapon_skill_num` overloads disagree for bows.** The `int` overload (`spells.h:239`) maps
  type 13→Whip and 14→Concussion; the enum overload (`:269`) maps both →Archery. Different call sites
  hit different overloads (`utility.cpp:693` enum vs `:788` int) — reconcile.
- **The per-type damage switch tilts *damage* only**, never the wielder's real OB/parry — the whip's
  "+8 parry" does not make it parry better, it makes it hit slightly softer. Easy to misread as the
  weapon's role (it actually runs opposite to it).
- **Two `get_weapon_damage` implementations, and the live one isn't the obvious one.** Melee
  (`fight.cpp:2384`), archery, and the in-game `stat` display all call the `int`/pointer version
  (`utility.cpp:426`, declared `utils.h:21`); the `const obj_data&`/`double` version
  (`object_utils.cpp:208`, `utils::` namespace) is reached **only** by the dead `combat_manager.cpp`.
  They diverge — **bludgeoning** is `parry_coef += 3` (live) vs. `= −3` (dead), opposite damage signs.
  (`combat-loop.md` currently cites the dead `object_utils.cpp` one — worth reconciling there too.)
- **`value[4]` is unused** by the formula, but it still carries **leftover non-zero legacy data on ~24
  live weapons** (values 1–57) — a rewrite should zero or repurpose it rather than trust it. And
  `value[0]`/`value[1]` are confusingly *called* "coefficients" while the in-game readout calls them
  "OB"/"Parry" — a rewrite should align the names.
- **Nothing validates `value[3]` (weapon type) on load.** At least one live object (`#5034`, a builder
  placeholder "tiger's invisible weapon") carries an **out-of-range type 22**, which falls through
  `weapon_hit_type`'s `default` to `TYPE_HIT` and through `weapon_skill_num` to barehanded — a rewrite
  should range-check the type against `WT_COUNT` at load.
