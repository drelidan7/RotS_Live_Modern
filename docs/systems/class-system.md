# Character classes — professions, class points & build archetypes

**Source files:** preset table `existing_profs` (`profs.cpp:37`); creation menu & custom builder
`interpre.cpp:2655-3130` (class prompt `:2663`, custom budget `:2704`, `points_used:185`,
per-prof clamp `:3129`); profession names `get_prof_name` (`profs.cpp:564`), `pc_prof_types`
(`consts.cpp:1986`); points→coefficient `GET_PROF_COOF`/`square_root[]` (`utils.h:330`,
`consts.cpp:2100`); class levels `do_levels` (`act_info.cpp:2658`); HP `class_HP` (`profs.cpp:136`);
primary-stat steering `stat_assigner`/`get_primary_stat` (`profs.cpp:548,580`).
**Status:** ✅ profession roster, preset classes, custom-build rules, and build archetypes.

> This doc owns the **class-generation** system. The downstream consequences of a build — exact HP,
> mana, attack-speed, stat rolls — are derived in
> [stats-and-character-power.md](stats-and-character-power.md); the focus you layer on top is in
> [specializations.md](specializations.md).

## The four professions

Every character is a blend of four professions (`pc_prof_types`, `consts.cpp:1986`;
`get_prof_name`, `profs.cpp:564`), each with a **primary stat** that creation favors when you invest
in it (`get_primary_stat`, `profs.cpp:548`):

| Profession | `PROF_*` | Primary stat | Governs |
|------------|----------|--------------|---------|
| **Mage** | `PROF_MAGE` | Intelligence | arcane spells (magic-system.md); contributes **0 HP** |
| **Mystic** | `PROF_CLERIC` | Will | cleric/mystic powers (cleric-mystic-system.md), spirit, mental combat |
| **Ranger** | `PROF_RANGER` | Dexterity | stealth/ambush/archery/pets, dodge, move (ranger-skills.md) |
| **Warrior** | `PROF_WARRIOR` | Strength | melee OB/parry, the most HP per point |

A character does not pick "a class" in the rigid sense — they pick a **point split across these
four**, and each profession then has its own **class level** (below). "Class" names are just
labelled presets over that point space.

## Choosing a class at creation

At creation (`interpre.cpp:2663`) the player either takes a **preset** class or builds a **custom**
one (enter `o`). The menu offers ten presets:

```
  Mys[T]ic     [R]anger     [W]arrior     [M]age
  Co[N]jurer   W[I]zard     [H]ealer      [S]washbuckler
  [B]arbarian  [A]dventurer
```

Custom builders are told: *"You are given **150 points** to work with, and you can split them
however you choose between the four classes. But be warned: the more points you spend on any class,
the less each following point will benefit you."* That warning is literal — points convert to power
on a **square-root curve** (below). A custom builder can even load a preset's spread as a starting
point and tweak from there (`interpre.cpp:3063`).

## Preset class roster

Point spreads from `existing_profs` (`profs.cpp:37`), in profession order **Mage / Mystic / Ranger /
Warrior**; every preset spends the full **150**. "Class levels @ L30" is the final per-profession
level at character level 30 (= `3·√points`, see below):

| Letter | Class | Mage | Mystic | Ranger | Warrior | Class levels @L30 (M/My/R/W) | Archetype |
|:--:|------|--:|--:|--:|--:|--|------|
| **m** | Mage | 100 | 25 | 16 | 9 | **30** / 15 / 12 / 9 | pure arcane caster |
| **t** | Mystic | 25 | 100 | 9 | 16 | 15 / **30** / 9 / 12 | pure cleric/mystic |
| **r** | Ranger | 16 | 9 | 100 | 25 | 12 / 9 / **30** / 15 | pure ranger |
| **w** | Warrior | 9 | 16 | 25 | 100 | 9 / 12 / 15 / **30** | pure melee (note baked-in **R15**) |
| **n** | **Conjurer** | 64 | 64 | 9 | 13 | **24 / 24** / 9 / 10 | mage/mystic caster hybrid |
| **i** | **Wizard** | 121 | 16 | 9 | 4 | **33** / 12 / 9 / 6 | extreme mage (over-100 in one class) |
| **h** | **Healer** | 25 | 121 | 0 | 4 | 15 / **33** / 0 / 6 | extreme mystic |
| **s** | **Swashbuckler** | 9 | 13 | 64 | 64 | 9 / 10 / **24 / 24** | the default ranger/warrior melee hybrid |
| **b** | **Barbarian** | 0 | 4 | 25 | 121 | 0 / 6 / 15 / **33** | extreme warrior |
| **a** | **Adventurer** | 36 | 36 | 36 | 42 | 18 / 18 / 18 / **19** | balanced jack-of-all-trades |

Note the four "pure" classes are **not** single-profession — e.g. **Warrior** still carries ranger
25 (→ R15 at L30), which is exactly what makes the ranger specs and the Defender/Light-Fighting
ranger terms live for melee characters (see specializations.md). **Swashbuckler** is the canonical
"hybrid" — a true 50/50 of ranger and warrior.

## Custom builds — the 150-point budget and the √ curve

The raw points live in `char_prof_data.prof_coof[]` (`structs.h:1268`, `GET_PROF_POINTS`). The
**total budget is 150** (`interpre.cpp:2704`, `points_used:185`); there's also a per-profession clamp
of **165** (`interpre.cpp:3129`), but with only 150 to spend that clamp never binds, so **150 is
both the total and the practical single-class maximum**.

> **Class points are permanent.** The point split is fixed at creation. The **Angel** respec
> (`tell angel pracreset`, below) wipes a character's **skills, practices, and specialization** back
> to a clean slate, but does **not** re-roll the profession allotment (`spec_pro.cpp:398` only
> touches `knowledge[]`/`skills[]`/`spells_to_learn`/spec, never `prof_coof[]`). So you can freely
> **respec** (re-learn skills, change your specialization; see
> [specializations.md](specializations.md)) but you cannot **rebuild your class** — the profession
> levels you bought at creation are with you for the character's life.

## The Angel — mortal respec & reroll

Players redo their *post-creation* choices through the **Angel**: `tell angel <command>`. It's a
gateway that exposes a small **approved subset of otherwise-immortal commands** to mortals.

Implementation (so the behavior is clear): it is **not** a follower and is never placed in a room
you visit. `do_tell` resolves its target with a **global** name lookup (`get_char`,
`handler.cpp:1614`), so "angel" is found from **anywhere**; the matched "Guardian angel" mob carries
special-proc 14 = `SPECIAL(resetter)` (`spec_ass.cpp`, `spec_pro.cpp:2648`) and is the single entity
a mortal may perceive across rooms (the explicit exception in `utility.cpp:1484`). The net effect is
that `tell angel …` behaves like a command available everywhere, even though under the hood it's an
omnipresent NPC + special proc.

The resetter accepts exactly two commands (`spec_pro.cpp:2659-2716`) — anything else returns
*"You may ask me for a pracreset and rerolls."*:

| `tell angel …` | Effect | Restriction | Immortal equivalent |
|----------------|--------|-------------|---------------------|
| **pracreset** | Drops your followers, then full reset: zeroes **all** `knowledge[]`/`skills[]`, restores the practice pool (`level·PRACS_PER_LEVEL + level·LEA/LEA_PRAC_FACTOR + 10`), sets spec → `PS_None`, shooting/casting → normal (`do_pracreset`, `spec_pro.cpp:398`) | none (any level) | `pracreset` command, `LEVEL_GRGOD` (`interpre.cpp:1972`) |
| **reroll** | Re-rolls your six abilities (`roll_abilities(ch, 80, 93)`) and shows the new `stat` | **level 6 only**; capped at **41** lifetime rerolls (`specials2.rerolls`, `spec_pro.cpp:2677`) | `reroll` / `SCMD_REROLL`, `LEVEL_GRGOD` (`act_wiz.cpp:2109`) |

Both are deliberately **mortal-safe**: `pracreset` only resets learnable progress (never class
points or level), and `reroll` is fenced to the brief **level-6** window (its availability is
announced then, `profs.cpp:376`) with a hard lifetime cap. Together they let a player **respec**
(skills + specialization) and **re-roll stats** without immortal help — but never rebuild the class
itself.

### Points → proficiency coefficient
`GET_PROF_COOF(prof, ch) = square_root[points] = round(100·√points)` (`utils.h:330`,
`consts.cpp:2100`; race tweaks: orc ×≈⅔, uruk mage penalty). A coefficient of **1000 (= 100 points)
is "100% proficiency."**

### Points → class level (the `levels` command, `act_info.cpp:2658`)
```
class_level = charLevel · coef / 1000   (integer)   →   at L30:  class_level = 3·√points
```
Class levels are earned only through character level 30, so the L30 value is **final**:

| Points | 4 | 9 | 16 | 25 | 36 | 49 | 64 | 81 | 100 | 121 | 144 | **150** |
|-------:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|--:|
| **Class lvl @L30** | 6 | 9 | 12 | 15 | 18 | 21 | 24 | 27 | 30 | 33 | 36 | **36** |

The **diminishing return** is the whole point: the first 100 points buy 30 levels (3 per √-step),
but going 100→121 (+21 points) buys only 30→33. Racial maluses (stats-and-character-power.md §9)
scale the coefficient down — a Common Orc's ×≈⅔ gives proportionally lower class levels for the same
points.

## Build archetypes & worked examples

Because points convert on the √ curve and **specializations are not class-gated**
(specializations.md), builds span a spectrum from "all-in one class" to "broad generalist." Common
shapes:

- **All-in / "36 of their main class."** Dumping nearly the entire 150 into one profession
  (≈144–150 points) reaches the **class-level cap of 36** in that profession — six levels past what
  a 100-point "pure" preset gives (30). The trade is everything else: a 150/0/0/0 build is 36 in one
  class and **0 in the other three**. Min-maxers do this to maximize a single profession's scaling
  (e.g. a 36-warrior for melee, a 36-mage for spell power) and the spec that rides it.
- **Extreme presets** already lean this way: **Barbarian** (W33) and **Wizard** (M33) spend 121 in
  one class for +3 levels over pure, keeping only scraps elsewhere.
- **`27 / 20 / 12 / 8`** — a popular custom split (≈ **81 / 45 / 16 / 8** points = the full 150). It
  gives **near-primary strength** (27 vs the 30 a pure class gets — only three levels down) *plus* a
  **solid secondary** at 20 and two minor dips. The sweet spot for players who want a dominant main
  without being one-dimensional.
- **`32 / 12 / 10 / 8` — near-cap primary** (exactly **114 / 16 / 12 / 8** points = the full 150,
  with **zero slack**: those are the *minimum* point costs of each level, so this is the only
  allocation that yields 32/12/10/8 — one point either way breaks it). This pours 76% of the budget
  into one class to land **just 4 below the level-36 cap**, with three small utility splashes instead
  of a real secondary. **Exemplar — a Warrior primary (W32 / R12 / M10 / My8):** the reason to climb
  to **W32** rather than stop at a pure-warrior W30 is **find weakness** (`SKILL_EXTRA_DAMAGE`,
  `check_find_weakness`, `fight.cpp:2051`). Its proc chance is
  `(skill/3)·warrior/30 + max(0, warrior − 30)` — i.e. **every warrior level past 30 adds a flat
  +1%** on top of the normal scaling, so at 100 skill W30 ≈ 33% but **W32 ≈ 37%**, each proc adding
  **+50% damage** to the swing. Few things in the game keep scaling above level 30, and find weakness
  is one of them, which makes a 114-point W32 main meaningfully harder-hitting than W30 — while R12
  buys baseline dodge/move utility (ranger-skills.md), and M10/My8 give minor splash perks. Pair it
  with **Heavy/Wild Fighting** (or any warrior spec). The same shape works for any primary, but it's
  most worth the points for **warrior** precisely because of the over-30 find-weakness term.
- **Even hybrids** — **Swashbuckler** (R24/W24) and **Conjurer** (M24/My24) split 64/64 for two
  strong halves; **Adventurer** (≈18 in everything) is the generalist.
- **Asymmetric four-way hybrids** — e.g. **W24 / Mystic24 / R10 / M9**: enough warrior to make
  **Heavy Fighting** pay off for durability, *and* enough mystic to make a **mystic spec's** powers
  strong — the same character can spec either way (the design intent behind un-gated specs;
  specializations.md "How a specialization is chosen").

### Picking a spec to match the build
Since any character ≥ level 12 can pick any spec, the right spec is the one your points already pay
for:

| Build | Strong specs to consider |
|-------|--------------------------|
| Warrior / Barbarian (high W) | Wild / Heavy / Light Fighting, Weapon Master, Defender |
| Swashbuckler (R24/W24) | any warrior spec **or** Stealth / Archery — both halves are real |
| Ranger (high R) | Stealth, Animals, Archery |
| Mage / Wizard (high M) | Fire / Cold / Lightning / Darkness / Arcane / Teleportation / Battle Mage |
| Mystic / Healer (high My) | Regeneration / Protection / Illusion / Guardian |
| Conjurer (M24/My24) | a mage **or** mystic spec |
| W/My/R/M four-way | Heavy Fighting (durability) **or** a mystic spec (stronger powers) |

## Does going past level 30 pay off? (per-profession over-30 scaling)

The level-36 ceiling on a heavy build is only worth chasing if the profession's abilities keep
scaling **past 30**. The deciding factor in the code is **which "level" a formula reads**:
- **`GET_PROF_LEVEL(prof, ch)`** is **uncapped** — it reaches ~36 for a heavy single-class build, so
  anything keyed on it keeps scaling 30→36.
- **`GET_LEVELA(ch)` = `min(level, 30)`** (and `GET_LEVELB`, `min(30, …)`) **plateau at 30** —
  anything keyed on these stops improving.

A full scan shows the engine overwhelmingly scales on **uncapped profession level**, so **heavy
single-class allocation is rewarded for *all four* professions**, not just warrior. Only **two**
abilities have an explicit *accelerating* `if (level > 30)` bonus (`grep` confirms): warrior
find-weakness and a small mystic one. But the bigger story is the pervasive *linear-uncapped*
scaling.

| Profession | What keeps scaling 30→36 (uncapped `prof_level`) | The standout |
|------------|--------------------------------------------------|--------------|
| **Warrior** | **find weakness ACCELERATES** (`+1%`/level past 30, on top of base — `fight.cpp:2057`); kick/swing/rend/bite damage `(2+W)·(100+M)/250` (`act_offe.cpp:903`); bash chance `+W`; maul duration `W−10`; natural-attack damage `+W` (`fight.cpp:2339`); parry `W·2` (`utility.cpp:775`); Defender block `max(W,R)` (`fight.cpp:2278`). OB partly scales — the `W·3/2` term grows but its `·GET_LEVELA/30` companion term caps (`utility.cpp:679`). | the only **accelerating** mechanic in the game |
| **Ranger** | the **broadest** set: archery damage multiplier `0.8 + 0.02·(R−20)` with **no upper cap** (1.0→1.12, `ranger.cpp:2059`); shoot/mark cadence `−R/12`; ambush success `+R`, ambush HP-scale `min(hp, R·20)`; hide `+R` (`ranger.cpp:1942`); dodge (`utility.cpp:870`); mark damage/success/duration; tame/calm; HP & move regen; gather-herb yield. Ambush *damage cap* still grows past 30 but **decelerates** to `+4`/level (`ranger.cpp:829`). | the most numerous uncapped perks |
| **Mage** | **every damage spell** — they all read `get_magic_power` → `get_mage_caster_level = prof_level + int/5`, **uncapped** (`mage.cpp:30`), so a 36-mage casts at ~6 caster levels higher across the board; spell **penetration** `mage_level/5`; proc chances (earthquake crack, black-arrow poison). Only the `GET_MAX_RACE_PROF·GET_LEVELA/30` sub-term of magic power caps. | whole **offensive output** scales |
| **Mystic** | **every power/heal** reads `get_mystic_caster_level = prof_level + wil/5`, **uncapped** (`mystic.cpp:68`) → larger heals, longer durations, higher effect levels; the **Guardian** summon's OB/parry/dodge/HP/damage all scale linearly with mystic level (`mystic.cpp:1502+`); Hallucinate has a small explicit **+1 over-30** step (`mystic.cpp:1058`). | whole **power output** scales |

**So which benefits *disproportionately*?**
- **Warrior** is unique in having an ability that *accelerates* past 30 (find weakness), so its
  30→36 melee gain is slightly super-linear — the cleanest "reward for going all-in."
- **Mage and Mystic** arguably gain the most *total* output, because **their entire kit** (every
  spell's damage/heal/duration) rides one uncapped caster-level term — +6 caster levels lifts
  everything at once.
- **Ranger** has the **most** individually-uncapped perks (archery especially keeps climbing with no
  ceiling), so a 36r is broadly stronger across stealth, archery, and pets.

**The real counterweight is points, not a level cap.** Abilities scale *linearly* past 30, but the
**√ point curve** means those levels are *expensive*: 30→36 costs **100→144 points** (+44 for +6
levels) versus 0→6 levels for the first ~4 points. So the question is never "does it still scale"
(it almost always does) but "are 6 uncapped levels in my main worth ~44 points I could spread into a
second profession?" Find weakness (accelerating) and the casters' all-spells caster level make that
trade most attractive for **warrior, mage, and mystic** mains; for hybrids the points usually buy
more elsewhere.

> Things that **don't** reward going past 30 (keyed on capped level): the character-level companion
> terms in OB/parry (`GET_LEVELA`), the HP formula's level term (`min(LEVEL_MAX, level)`,
> `profs.cpp:729`), and victim-side save rolls (`GET_LEVELA`, `spell_pa.cpp`). These are about
> *character* level (capped at 30), not *profession* level.

## How class points shape the rest of the character

Class points don't just set proficiency — they cascade (details in
[stats-and-character-power.md](stats-and-character-power.md)):
- **Hit points** (`class_HP`, `profs.cpp:136`): `√(3·Warrior + 2·Ranger + 1·Mystic points)·200`
  (orc ×4/7). **Mage points give zero HP**; warrior points are worth 3× ranger and 6× mystic for
  survivability — the main reason melee hybrids are tanky and pure casters are fragile.
- **Rolled stats** (`stat_assigner`, `profs.cpp:580`): your highest-invested profession's **primary
  stat** gets your best roll, the next class the next, etc. — so a Swashbuckler rolls high
  Dex+Str, a Conjurer high Int+Will.
- **Skill caps & proficiency gain** scale with the coefficient (§ above), so more points in a class
  = higher ceilings in its skills.

## Cross-references
- Downstream power (HP, mana, attack speed, stat rolls, leveling): [stats-and-character-power.md](stats-and-character-power.md).
- The focus layered on a build, and why specs aren't class-gated: [specializations.md](specializations.md).
- Per-profession skill detail: [ranger-skills.md](ranger-skills.md), [warrior-skills.md](warrior-skills.md), [magic-system.md](magic-system.md), [cleric-mystic-system.md](cleric-mystic-system.md).
