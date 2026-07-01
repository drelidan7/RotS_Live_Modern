# Guardian spirits (the Guardian mystic specialization)

**Source files:** summon spell & scaling `src/mystic.cpp:1491-1673` (`set_guardian_stats`,
`calc_guardian_hp`, `set_guardian_health`, `tweak_{aggressive,defensive,mystic}_guardian_stats`,
`scale_guardian`, `ASPELL(spell_guardian)`); vnum table & type lookup `src/consts.cpp:2392-2421`
(`guardian_mob[][3]`, `get_guardian_type`); level-up rescale `src/profs.cpp:198-216`
(`advance_level_prof`); charm/pet flag & predicate `src/char_utils.cpp:1352-1356`
(`utils::is_guardian`), declared `src/char_utils.h:204` (**not** `utils.h` — `utils.h:703-704` is
`get_guardian_type`, a different function; corrected here after miscite); AI behavior
`src/mobact.cpp:92,161-163,194-262`; order/command exclusion `src/act_offe.cpp:270-328`
(`do_order`); Orc spec-selection block `src/act_othe.cpp:1855-1858`; corpse-loot-vanish code
`src/fight.cpp:594-614` (`remove_random_item`) — **dead code, see Behavior below**; XP credit
`src/fight.cpp:1156-1166` (`master_gets_credit`); persistence
`src/objsave.cpp:680-754` (`Crash_follower_save`/`Crash_follower_load`, `FOL_GUARDIAN`),
`src/handler.cpp:947-1105` (`add_follower`, `stop_follower`, `die_follower`), `src/handler.cpp:1883-1912`
(`extract_char`); spec gate `src/structs.h:823` (`PS_Guardian`), `:847` (`PLRSPEC_GRDN`); mob
flag `src/structs.h:932` (`MOB_GUARDIAN`); spell table entry `src/consts.cpp:477-478`
(id 65, `SPELL_GUARDIAN` = `src/spells.h:106`). Mob prototype data verified against
`lib/world/mob/11.mob` (vnum 1110) and `lib/world/mob/13.mob` (vnums 1300-1399).
**Status:** ✅ summon path, scaling formulas, AI behavior, and persistence quirks verified
against source and live `lib/world/` data. Re-verified adversarially against source a second time:
corrected a stray citation (`is_guardian`'s declaration), the corpse-loot-vanish claim (the
underlying function turned out to be dead code, not a guardian-specific carve-out), the
Defensive-guardian "rescue-only" characterization (it also auto-engages via `do_hit`), the
level-up HP-rescale mechanics (max HP vs. current HP were conflated), and the race-gap count in
**Open questions** (was "eight playable races," actually four — the other five named weren't
player-creatable races to begin with). One functional gap (four races with no guardian mob)
confirmed by code inspection, listed below rather than guessed at.

> **Scope note.** This doc is the deep dive; [cleric-mystic-system.md §5](cleric-mystic-system.md)
> and [specializations.md](specializations.md) each carry a short summary of the same mechanic —
> cross-reference them for how Guardian fits into the broader power/spec catalog, but treat *this*
> file as authoritative for exact formulas, vnums, and lifecycle behavior.

## Purpose

**Guardian** is one of the four Mystic (Cleric) specializations (`PS_Guardian` /
`PLRSPEC_GRDN` = enum value 11, `structs.h:823,847`; string `"guardian"`,
`consts.cpp:2359-2362`). It is purely a **gate on a single power**: choosing this spec unlocks
the **`guardian`** power (spell id 65, `SPELL_GUARDIAN`), which summons a permanently-charmed
pet NPC ("guardian") that fights alongside the caster. Outside of gating that one power, the
Guardian specialization has **no other passive effects** (contrast Regeneration/Illusion/
Protection, which buff several powers each — `cleric-mystic-system.md §5`).

A guardian is **not** a generic charmed pet: it is exempt from the normal `order` command (it
"appears unwilling to do your bidding" — `act_offe.cpp:293-296`), is excluded from the
"stand passively next to master" pet behavior (`mobact.cpp:92`), and drives its own combat AI
(assist/rescue/flee-to-master — see **Behavior**, below).

## Data structures

### Specialization gate
- `game_types::PS_Guardian` (`structs.h:823`) / `#define PLRSPEC_GRDN 11` (`structs.h:847`) —
  the same enum value, referenced as `game_types::player_specs` (modern) or the legacy
  `GET_SPEC(ch)` macro interchangeably.
- Only a Mystic (any character; the resource/level math uses `PROF_CLERIC`) can hold this spec.
  Selection is permanent-until-respec via `do_specialize` (`act_othe.cpp:1811+`); **Orcs cannot
  select Guardian** — `"Snagas can't specialize in guardian!"` is a hard runtime block
  (`act_othe.cpp:1855-1858`), independent of the summon-time race gate below.

### Spell table entry (`consts.cpp:477-478`)
```
{"guardian", PROF_CLERIC, 10, spell_guardian, POSITION_STANDING, 30, 55, 32768, 10, 65, 0, PLRSPEC_GRDN}
```
Fields per `struct skill_data` (`spells.h:355-370`): nominal learn level **10** (moot — see
`learn_type` below), minimum caster position **standing**, cost **30 spirit**
(`min_usesmana`, the mystic resource — `cleric-mystic-system.md §1`), **55** beats
(heartbeats) before the caster can act again, `learn_diff = 10`, **`learn_type = 65`** which is
the "spec-only" sentinel (comment at `spells.h:367`: *"If the skill is spec only set to 65
otherwise 1"*) — i.e. **only characters who already hold `PLRSPEC_GRDN` can ever learn/cast
this power**, regardless of nominal level. `spell_guardian` also double-checks this at
runtime (`GET_SPEC(caster) != PLRSPEC_GRDN` → `"You are not dedicated enough to cast this."`,
`mystic.cpp:1627-1630`), so there is no way to reach the summon code without the spec.

### Guardian-type constants (`structs.h:40-43`)
```cpp
const int INVALID_GUARDIAN    = -1;
const int AGGRESSIVE_GUARDIAN = 0;
const int DEFENSIVE_GUARDIAN  = 1;
const int MYSTIC_GUARDIAN     = 2;
```
These double as the column index into the vnum table below and the argument keyword order
(`"aggressive"`, `"defensive"`, `"mystic"`, `mystic.cpp:1615-1620`).

### Race → vnum table (`consts.cpp:2392-2405`)
```cpp
int guardian_mob[MAX_RACES][3] = {
    {1110, 1110, 1110}, {1315, 1316, 1301}, {1302, 1310, 1314}, {1308, 1309, 1303},
    {1304, 1306, 1307}, {0, 0, 0},          {0, 0, 0},          {0, 0, 0},
    {0, 0, 0},          {0, 0, 0},          {0, 0, 0},          {1311, 1317, 1318},
    {0, 0, 0}, ... (rows 12-31 all {0,0,0})
};
```
Columns are `[aggressive, defensive, mystic]`. `MAX_RACES = 32` (`structs.h:71`); only rows
0 (`RACE_GOD`), 1 (`RACE_HUMAN`), 2 (`RACE_DWARF`), 3 (`RACE_WOOD`), 4 (`RACE_HOBBIT`), and 11
(`RACE_URUK`) are populated (`structs.h:858-869` for the race constants). `get_guardian_type`
(`consts.cpp:2407-2421`) is the inverse lookup (mob vnum → which of the three types it is),
used by the level-up rescale path.

### `char_data` fields touched by scaling (`mystic.cpp:1491-1604`)
The summon completely overwrites, on the freshly-loaded mob instance:
- `player.level` — cosmetic "level" shown in `look`/`consider` etc.
- `abilities`, `tmpabilities`, `constabilities` (all three ability blocks — current, temporary,
  and "permanent baseline") for all six stats (`str/intel/wil/dex/con/lea`) and `hit` (HP).
- `points.OB`, `points.parry`, `points.dodge`, `points.damage`, `points.willpower`,
  `points.ENE_regen`.
It does **not** touch `points.spirit`/mana (a guardian mob doesn't cast) or `player.race`/
`player.sex` etc. — those come from the mob prototype.

## Format / Algorithm

### 1. Summon command
```
cast 'guardian' aggressive|defensive|mystic
```
`ASPELL(spell_guardian)` (`mystic.cpp:1606-1673`):
1. **Spec check** — must hold `PLRSPEC_GRDN` (above), else refused with no refund
   (`do_cast`, `spell_pa.cpp`, has already deducted 30 spirit before invoking the spell body —
   `cleric-mystic-system.md §1` — so a failed cast below this point except the "bad argument"
   branch keeps the cost).
2. **Argument parse** — `one_argument(arg, first_word)` then `search_block` against
   `{"aggressive","defensive","mystic"}`. On no/garbled match: prints "You'll have to be more
   specific..." **and refunds the 30 spirit** (`utils::add_spirits(caster, 30)`,
   `mystic.cpp:1642-1647`) — the only failure branch that refunds.
3. **One-guardian check** — walks the global `character_list` for an NPC whose `master == caster`
   and `utils::is_guardian(*tmpch)` is true; if found, refuses ("You already have a guardian.")
   **with no refund** (`mystic.cpp:1653-1660`).
4. **Vnum lookup & load** — `guardian_num = guardian_mob[GET_RACE(caster)][guardian_to_load]`;
   `read_mobile(guardian_num, VIRT)`. If the race/type cell is `0` (unsupported race — see **Open
   questions**) or the vnum doesn't resolve, refuses with "Could not find a guardian for you,
   please report." — **also no refund** (`mystic.cpp:1662-1665`).
5. **Spawn** — `char_to_room` into the caster's room, `act("$n appears with a flash.", ...)`,
   `add_follower(guardian, caster, FOLLOW_MOVE)` (standard follow registration — sets
   `guardian->master = caster` and links into `caster->followers`), then **directly** sets
   `SET_BIT(guardian->specials.affected_by, AFF_CHARM)` and `SET_BIT(MOB_FLAGS(guardian), MOB_PET)`
   (`mystic.cpp:1667-1671`) — note this is a **bare bit set, not `affect_to_char`**, so the charm
   has **no duration and never expires on its own** (contrast a normal `charm person` affect,
   which is a timed `affected_type`). The guardian mob prototype **already** carries the
   permanent `MOB_GUARDIAN` flag on disk (verified below), so no code needs to set it at summon
   time.
6. **Scale** — `scale_guardian(guardian_to_load, caster, guardian, /*restore_health=*/true)`
   (§below) is called synchronously before the player sees the mob.

### 2. Mob prototypes (verified against `lib/world/mob/13.mob`, `11.mob`)

| Race | Aggressive vnum | Defensive vnum | Mystic vnum |
|---|---|---|---|
| Human (`RACE_HUMAN`) | 1315 — *a golden lion* | 1316 — *a slim, lustrous lion* | 1301 — *a white albino lion* |
| Dwarf (`RACE_DWARF`) | 1302 — *a fierce bear* | 1310 — *a tall, hardy bear* | 1314 — *a bold white bear* |
| Wood-elf (`RACE_WOOD`) | 1308 — *a valiant dire wolf* | 1309 — *a stoic silver wolf* | 1303 — *a noble white wolf* |
| Hobbit (`RACE_HOBBIT`) | 1304 — *a large, husky mastiff* | 1306 — *a lean, loyal mutt* | 1307 — *a short, white hound* |
| Uruk (`RACE_URUK`) | 1311 — *a black, menacing lizard* | 1317 — *a vile, horned lizard* | 1318 — *a white-scaled lizard* |
| God (immortal, row 0) | 1110 — *Skippy the Kangaroo* (all three types) | | |

Each race gets its own themed animal family (lions/bears/wolves/dogs/lizards); the "God" row
(immortal test/admin account, `RACE_GOD = 0`) always loads the single joke mob **Skippy the
Kangaroo** (vnum 1110, `lib/world/mob/11.mob:169`) regardless of the `aggressive/defensive/
mystic` argument.

All 15 racial prototypes carry `MOB_GUARDIAN` (`1<<26`) permanently in their on-disk action-bits
(decoded from the raw bitfield, e.g. vnum 1302 = `102237194`). They split into two AI templates
by archetype (decoded the same way):
- **Aggressive & Mystic guardians carry `MOB_ASSISTANT`** (`1<<25`, "will assist his master if
  his master is in combat") — e.g. 1301/1302/1303/1304/1307/1308/1314/1315 = `102237194`.
- **Defensive guardians carry `MOB_BODYGUARD`** (`1<<16`, "rescues his master") instead —
  e.g. 1306/1309/1310/1316 = `68748298`.
- The three Uruk variants (1311/1317/1318) additionally carry a stray `MOB_SPEC` bit with **no**
  special-procedure assigned in `spec_ass.cpp` — harmless (the mob-act code only invokes a
  function if one is registered, `mobact.cpp:119-131`), but see **Open questions**.
- All 15 also carry `MOB_SENTINEL` (never wanders on its own), `MOB_CAN_SWIM`, `MOB_NORECALC`,
  and `MOB_FAST` (reacts immediately when someone enters its room). (Arithmetic check: summing
  just the named flags above for the Aggressive/Mystic case — `MOB_GUARDIAN + MOB_ASSISTANT +
  MOB_SENTINEL + MOB_CAN_SWIM + MOB_NORECALC + MOB_FAST` — totals `102237186`, eight short of the
  on-disk `102237194`. The remaining `8` is `MOB_ISNPC` (`1<<3`), an internal engine bit the
  loader force-sets on every mob regardless of what's on disk — see
  [world-files.md](../data-formats/world-files.md) ("`MOB_ISNPC` is force-set..."). Same +8 applies
  to the Defensive (`68748290 + 8 = 68748298`) and Uruk-variant (`+1` for `MOB_SPEC` on top) values
  below.)

The `<level> <OB> <parry> <dodge>` / `<hp_current> <hp_max>` / `<damage> <energy_regen>`
stat-block fields on disk (`world-files.md` mob format) are **not the live combat stats** — they
are consumed by `read_mobile` into `player.level`, a `[tmpabilities.hit, abilities.hit]` random
HP-roll range (`db.cpp:1144,1325-1326`), and `points.OB/parry/dodge/damage/ENE_regen`
respectively, but `scale_guardian` runs immediately afterward (step 6 above, same function call)
and **overwrites every one of them**. They are effectively pre-baked cosmetic values matching
what a **level-30 mystic's** summon would look like (see **Worked example** — the prototype
numbers line up almost exactly with the level-30 formula output), useful for builders/immortals
eyeballing the file but never seen live by a player.

### 3. Scaling formulas (`mystic.cpp:1491-1604`)

Let `L = utils::get_prof_level(PROF_CLERIC, *caster)` — **the caster's raw Mystic/Cleric
profession level**, *not* `get_mystic_caster_level()` (the WIL-adjusted "effective" level `L'`
used by most other mystic powers, `cleric-mystic-system.md §1`). This is a common trap: the
local variable is literally named `caster_mystic_level` in the source
(`mystic.cpp:1580`) but it is the **unmodified** profession level — willpower does **not**
influence guardian scaling.

**Baseline (`set_guardian_stats`, applied identically to `constabilities`, `abilities`, and
`tmpabilities`):**
```
ability_base = 8 + L/4          // str = int = wil = dex = con = lea = ability_base
```

**Per-type tweaks (`tweak_*_guardian_stats`)**, applied on top of the baseline, to `abilities`/
`tmpabilities`/`constabilities` alike unless noted:

| Type | STR | WIL | OB | Parry | Dodge | Damage | Base HP constant |
|---|---|---|---|---|---|---|---|
| **Aggressive** | `+5` | `−5` | `13·L/5` | `3 + L/2` | `L/10` | `L/3 + 1` | `9` |
| **Defensive** | — | `−5` | `L/2 − 2` | `8 + 2L` | `8 + L` | `L/6 + 1` | `22` |
| **Mystic** | — | `+5` | `0` | `0` | `0` | `L/6` | `5` |

(All divisions are integer/truncating, matching the C++ `int` arithmetic.)

**HP roll (`calc_guardian_hp`, `mystic.cpp:1502-1507`):**
```
random_factor = number(6.0) - 3.0        // continuous uniform in [-3.0, 3.0)
health = int(base_hp * (L + random_factor) / 3.0)
```
`number(double max)` (`utility.cpp:936`) returns a uniform **real** number in `[0, max)`, so
`random_factor` is continuous, not a d6 integer roll. `set_guardian_health` (`mystic.cpp:1509-
1521`) writes this `health` into **max HP** (`abilities.hit` = `GET_MAX_HIT`, `utils.h:388`, plus
`constabilities.hit`) and, when `restore_health` is true — always true on summon and on rent/quit
reload, false on level-up rescale — also into **current HP** (`tmpabilities.hit` = `GET_HIT`,
`utils.h:387`), i.e. current HP is fully topped off. On a level-up rescale (`restore_health =
false`) the two fields are *not* symmetric: max HP is overwritten with the new roll **only if
it's higher than the old max** (`abilities.hit < new_health`) — so max HP can rise but can never
drop, even on an unlucky low roll at a higher level. Current HP is **never increased** by this
path — it's set to `min(current_HP, new_max)`, which is a no-op in every real case
because current HP can't exceed the (never-shrinking) max to begin with. Net effect: a level-up
can only raise the guardian's HP ceiling; it neither heals it nor (as the clamp implies, though
the clamp never actually triggers in practice) damages it mid-fight.

**Derived stats (`scale_guardian`, `mystic.cpp:1578-1604`):**
```
player.level      = L / 2
points.willpower   = player.level + abilities.wil        // abilities.wil already includes the per-type WIL tweak
points.ENE_regen   = base_energy_regen + L                // base_energy_regen = 70 for Aggressive, 60 otherwise
```

### 4. Level-up rescale (`profs.cpp:198-216`)
Every time the Mystic's `PROF_CLERIC` level increases (`advance_level_prof`), if the character's
active spec is `PS_Guardian`, the code walks `character->followers`, uses `get_guardian_type`
(inverse vnum lookup) to identify which of the three archetypes the follower is, and re-runs
`scale_guardian(..., restore_health=false)` on it — **only the first matching follower** (the
`break` at `profs.cpp:211` stops after one hit, so a second guardian-shaped follower, if one
somehow existed, would not be rescaled). This keeps the guardian's stats tracking the owner's
current level automatically; it is the only place scaling is re-applied outside of the initial
summon and the rent/quit reload path (§5).

### 5. Persistence across quit / rent (`objsave.cpp`, `handler.cpp`)
A guardian is a regular NPC follower and is captured by the general follower-save machinery
(`follower_file_elem`, `flag_config = FOL_GUARDIAN` when `MOB_FLAGGED(follower, MOB_PET)` and
not tamed/orc-friend — `objsave.cpp:706-713`; full binary layout in
[object-rent-files.md](../data-formats/object-rent-files.md)). Two consumers of this data
diverge in a way that changes whether the guardian actually survives:

- **`Crash_follower_save` only ever sees followers that are in the *same room* as the owner**
  (`objsave.cpp:694-695`) — a guardian left behind elsewhere is not saved by either path below.
- **Renting at an inn** (`gen_receptionist`/`do_rent` "Offer" flow, `objsave.cpp:1585-1601`)
  calls `Crash_rentsave(ch, cost)` (which internally calls `Crash_follower_save`) **before**
  `extract_char(ch)`. At that point `ch->followers` is still populated, so the guardian **is**
  written to the rent file and reloaded via `Crash_follower_load` → `FOL_GUARDIAN` case
  (`objsave.cpp:850-858`), which re-charms it, re-flags `MOB_PET`, and calls
  `scale_guardian(..., restore_health=true)` against the character's *current* level on load.
  (The field-`rent` command, `do_rent`, is unconditionally disabled — `"Field-rent is disabled.
  You have to go to an inn now."`, `objsave.cpp:1624` — so this is the only live rent path.)
- **A normal `quit`** (`act_othe.cpp:61-119`) does the two steps in the **opposite order** for a
  non-immortal: `extract_char(ch)` **then** `Crash_crashsave(ch)`. `extract_char` unconditionally
  calls `die_follower(ch)` when the character has followers (`handler.cpp:1904-1905`), which
  calls `stop_follower(guardian, FOLLOW_MOVE)` — this **strips `AFF_CHARM` and clears
  `ch->followers`** (`handler.cpp:982-1044`) *before* `Crash_crashsave`'s
  `Crash_follower_save` ever runs. The result: **a normal player's guardian is *not* persisted
  across `quit`** — it is silently un-charmed and left behind as a masterless NPC in the room
  (it won't wander; it carries `MOB_SENTINEL`), and the player must re-summon (and pay another
  30 spirit) after logging back in. Immortals take the opposite branch (`Crash_crashsave` before
  `extract_char`, `act_othe.cpp:106-114`), so an immortal's guardian *would* be captured by this
  save path if one existed. This asymmetry is directly traceable in the two branches of
  `do_quit`; whether it's an intentional "guardians don't survive a hard quit" design or an
  ordering bug is not stated anywhere in comments — see **Open questions**.
- **Idle/link-death timeout** (`Crash_idlesave`, `objsave.cpp:1127-1175`) never calls
  `Crash_follower_save` at all — a guardian present at an idle-timeout save is not written
  either, independent of the ordering issue above.

## Behavior (combat AI, orders, death)

- **Not orderable.** `do_order` explicitly refuses to relay a command to a guardian
  (`"Your guardian appears unwilling to do your bidding."`, `act_offe.cpp:293-296`), and the
  bulk `order followers` loop skips any follower for which `utils::is_guardian` is true
  (`act_offe.cpp:318`). A guardian's actions are entirely AI-driven; there is no way to direct
  it to attack a specific target, flee, or guard a specific person.
- **Not "passive" like other pets.** Ordinary charmed pets standing in the same room as their
  master are marked `is_passive` and skip most of the per-tick mob AI (`mobact.cpp:92-96`);
  guardians are explicitly excluded from that check (`!utils::is_guardian(*ch)`), so they always
  run their full AI tick even next to their owner.
- **Assist / rescue, by archetype** (driven by the `MOB_ASSISTANT` / `MOB_BODYGUARD` flags baked
  into the prototypes, §2 above, via the generic bodyguard/assistant handling in `mobact.cpp:194-
  255`). This is *not* a clean "rescue vs. assist" split — both archetypes end up fighting
  alongside their master, just via different commands:
  - **Aggressive/Mystic** (`MOB_ASSISTANT` branch, `mobact.cpp:241-255`): if the master is fighting
    and the guardian itself is idle, the guardian calls `do_assist` on its master.
  - **Defensive** (`MOB_BODYGUARD` branch, `mobact.cpp:195-219`) does **two** independent checks
    every tick, not just one: (1) if the master's current opponent is itself attacking the master
    back (mutual combat), the guardian calls `do_rescue` (`mobact.cpp:202-210`); *and, separately*
    (2) if the guardian is idle and the master has an opponent at all, the guardian calls
    `do_hit` directly against that opponent (`mobact.cpp:211-218`) — functionally the same
    "jump into master's fight" outcome as the Aggressive/Mystic `do_assist` path, just reached
    through a different command. So a Defensive guardian isn't purely reactive/rescue-only; it
    joins the master's fight whenever idle, exactly like the other two archetypes, and *additionally*
    prioritizes a rescue call when the opponent is mutually engaged with the master.
- **Never received help from other MOB_HELPER mobs.** The generic "helper" AI (mobs with
  `MOB_HELPER` that jump into allies' fights) explicitly skips assisting anything flagged
  `MOB_GUARDIAN` (`mobact.cpp:161-163`) — a guardian fights alone (plus whatever its own
  archetype flag grants it), even standing next to a friendly helper mob.
- **Flees to reunite with its master if separated mid-fight**: `mobact.cpp:257-262` — if a
  guardian is fighting and its master is no longer in the same room, it calls `do_flee`
  (regardless of `MOB_WIMPY`/HP — this check is guardian-specific and unconditional).
  There is no code that teleports/pulls the guardian to the master otherwise — a guardian left
  behind in an untouched room simply stays there (`MOB_SENTINEL`).
- **The "corpse item vanishes" chance never runs at all, for anyone — not just guardians.**
  `remove_random_item(character, corpse)` is dead code: its only call site is commented out
  (`fight.cpp:765`, `// remove_random_item(character, corpse); // To re-enable item decay, remove
  the comment before this line.`). The doc previously claimed guardians were "explicitly skipped"
  from this chance; that was wrong on two counts. First, the function is never invoked in the live
  build, so *no* corpse (guardian, other NPC, or player) is ever subject to it — this isn't a
  guardian-specific carve-out, it's an inert leftover. Second, even reading the dead function in
  isolation, its condition is the **opposite** of "skip guardians": `remove_random_item`
  (`fight.cpp:594-614`) only *runs* its vanish-roll when `!IS_NPC(ch) || IS_AFFECTED(ch, AFF_CHARM)
  || utils::is_guardian(*ch)` — i.e. guardians and charmed pets are explicitly *included* in the
  set of corpses eligible for the roll, not excluded. Moot either way since guardians carry no
  items, but the doc's original claim (mechanism, direction, and liveness) was all incorrect.
- **Kill XP still credits the owner** when the guardian is in the same room as its master at the
  kill (`master_gets_credit`, `fight.cpp:1156-1166`, checked via the `MOB_GUARDIAN` flag
  alongside `MOB_PET`/`MOB_ORC_FRIEND`) — same rule as any other pet/orc-friend follower.
- **On the guardian's own death**: it is a normal NPC death — `raw_kill` extracts it
  (`fight.cpp:1038`, the `IS_NPC(dead_man)` branch of `die()`), dropping a corpse like any
  mob. The owner is simply down a guardian until they recast (spirit permitting).
- **On the owner's death/quit/disconnection**: any code path that calls `extract_char` on the
  owner runs `die_follower`, which un-charms and detaches every follower including the guardian
  (`handler.cpp:1087-1098`, `:1904-1905`) — the guardian is not destroyed, just released as a
  masterless NPC in whatever room it was standing in. See **Persistence**, §5, for how this
  interacts with the save order on `quit` specifically. Ordinary in-combat player death (not
  going through `extract_char`) does not appear to sever the follow link — see **Open
  questions**.
- **Dismissal**: there is **no dedicated "dismiss guardian" command**. The only way to be rid of
  a guardian intentionally is to let/force it die in combat, or to walk away and quit (which
  detaches it per above). `order`, being blocked, cannot be used to command it away either.

## Limits

- **One guardian at a time**, enforced at cast time by scanning `character_list` for any NPC
  with `master == caster` and `utils::is_guardian` true (`mystic.cpp:1653-1660`) — a second
  `cast 'guardian' ...` while one is alive is refused (no refund). There is no explicit check
  against having *other* kinds of followers simultaneously (a mount, a tamed animal, an
  orc-recruit) — nothing in `spell_guardian` inspects `caster->followers` for non-guardian
  entries, so in principle a mystic could ride a mount, keep a tamed pet, **and** field a
  guardian at once; this is not exercised anywhere else in the reviewed code and is not called
  out as disallowed.
- Only 5 of the 32 race slots in `guardian_mob[][]` are populated (Human, Dwarf, Wood-elf,
  Hobbit, Uruk, plus the joke "God"/immortal row). Of the **ten player-creatable races**
  (`interpre.cpp:2618-2648`, cross-referenced in [races.md §0](races.md)), that leaves **four**
  — Beorning, Magus/Uruk-Lhuth, Olog-Hai, Haradrim — with no guardian at all, plus Orc which is
  separately blocked from *selecting* the spec in the first place — see **Open questions**.

## RotS-specific notes

- This is a RotS-original mechanic layered on the stock Diku charm-follower system; nothing
  comparable exists in vanilla CircleMUD/DikuMUD.
- Unlike vanilla `charm person`, the guardian's `AFF_CHARM` is a bare bit set outside the
  `affected_type` list (`mystic.cpp:1670`) — it never appears in an `affect list`/`score` output
  and never times out on its own; only death or `extract_char`-driven detachment ends it.
- The Guardian spec is otherwise inert — unlike Regeneration/Illusion/Protection (each of which
  buffs 3-4 unrelated powers, `cleric-mystic-system.md §5`), Guardian's *only* effect is
  unlocking this one power.

## Worked example

**Human 30-Mystic** (`PROF_CLERIC` level 30, specialized Guardian) casts
`cast 'guardian' aggressive` in their own room.

1. Cost: 30 spirit deducted by `do_cast` up front.
2. `guardian_to_load = AGGRESSIVE_GUARDIAN (0)`; race = `RACE_HUMAN (1)` →
   `guardian_num = guardian_mob[1][0] = 1315` (a golden lion).
3. `read_mobile(1315, VIRT)` loads the prototype, spawns it in the caster's room, follows the
   caster, gets `AFF_CHARM` + `MOB_PET`.
4. `scale_guardian(AGGRESSIVE_GUARDIAN, caster, guardian, restore_health=true)` with
   `L = utils::get_prof_level(PROF_CLERIC, caster) = 30` (raw level — the caster's Willpower
   score plays no role here):
   - Baseline: `ability_base = 8 + 30/4 = 8 + 7 = 15` → all six stats start at 15.
   - Aggressive tweak: `str = 15 + 5 = 20`, `wil = 15 − 5 = 10` (int/dex/con/lea stay at 15).
   - `OB = 13·30/5 = 78`
   - `parry = 3 + 30/2 = 18`
   - `dodge = 30/10 = 3`
   - `damage = 30/3 + 1 = 11`
   - `HP = int(9·(30 + r)/3)` for `r ∈ [−3, 3)` → **HP ∈ [81, 98]**, e.g. `r = 0` → exactly `90`.
   - `player.level = 30/2 = 15`
   - `points.willpower = 15 + 10 (wil) = 25`
   - `points.ENE_regen = 70 (aggressive base) + 30 = 100`
5. **Cross-check against the live data**: the on-disk prototype for the *dwarf* aggressive
   guardian (vnum 1302, `lib/world/mob/13.mob`) is baked with exactly
   `level 15, OB 78, parry 18, dodge 3, damage 11, energy_regen 100, hp_max 100` — matching this
   level-30 formula output field-for-field (HP `100` sits just above the `[81,98]` range,
   consistent with the file being a hand-set approximate/round figure rather than one exact
   random draw). Every other racial variant in `13.mob` matches the same pattern for its
   archetype (Defensive: `OB 13, parry 68, dodge 38, damage 6, regen 90`; Mystic: `OB/parry/
   dodge 0, damage 5, regen 90`), strongly confirming these files were authored by running this
   exact formula at `L = 30` and are not independently hand-tuned.
6. If this caster already had a live guardian, step 3 onward never runs and the 30 spirit is
   kept (no refund). If they'd mistyped `cast 'guardian'` with no argument, the summon aborts at
   step 2 and the 30 spirit is refunded.

## Open questions

- **Four of the ten player-creatable races have no guardian mob at all** (corrected — a previous
  pass through this doc miscounted this as "eight playable races" and wrongly included races that
  aren't player-creatable). Character creation (`interpre.cpp:2618-2648`) only ever assigns one of
  ten race constants: Human, Dwarf, Wood-elf, Hobbit, Beorning, Uruk-Hai, Orc, Magus (Uruk-Lhuth),
  Olog-Hai, Haradrim (roster confirmed in [races.md §0](races.md), "Ten races are selectable at
  creation"). `guardian_mob[][]` only has non-zero rows for Human, Dwarf, Wood-elf, Hobbit, and
  Uruk-Hai, and Orc is separately hard-blocked from *selecting* the spec at all
  (`act_othe.cpp:1855-1858`). That leaves **Beorning, Magus/Uruk-Lhuth, Olog-Hai, and Haradrim**
  as the actual gap: nothing stops a character of one of these four races from selecting Guardian
  and then hitting a dead end at cast time. High-elf (`RACE_HIGH = 5`), Easterling, Harad, Undead,
  and Troll are *not* part of this gap — none of them is reachable through character creation.
  High-elf has no chargen path (only Human/Dwarf/Wood-elf/Hobbit/Beorning/Uruk-Hai/Orc/Magus/
  Olog-Hai/Haradrim are offered, `interpre.cpp:2596-2608`) and appears only in NPC/mechanic checks
  elsewhere; Easterling/Harad/Undead/Troll are explicitly commented `/* Races used for NPCs */`
  in `structs.h:871-875` and have no creation path either. `read_mobile(0, VIRT)` fails
  (`lib/world/mob` contains no `#0` record in any zone), so a Guardian-specced Beorning/Magus/
  Olog-Hai/Haradrim character who casts the power gets **"Could not find a guardian for you,
  please report."** every time, with **no refund** of the 30 spirit. This reads as a genuine
  content gap rather than intended design — flagging rather than guessing at the fix.
- **Uruk guardians (1311/1317/1318) carry a stray `MOB_SPEC` bit** with no function assigned in
  `spec_ass.cpp`. Harmless as coded (no-op), but unclear whether it's leftover from cloning a
  templated mob or intentional groundwork for an uruk-specific special.
- **Owner death mid-combat (not via `quit`/`extract_char`)**: `die()` for a player (`fight.cpp`)
  does not appear to call `extract_char`/`die_follower` in the branch actually taken for player
  deaths (only the `IS_NPC(dead_man)` branch calls `raw_kill`); whether/when a guardian's follow
  link is severed on a player's in-combat death (as opposed to `quit`) was not traced end-to-end
  and should be verified by direct testing (kill a Guardian-spec character with a live guardian
  present and observe whether the guardian survives the respawn/resurrection flow).
- **Is the `quit`-vs-`rent` save-order asymmetry (§5) intentional?** Nothing in comments states
  that guardians are meant to not survive a plain `quit` while surviving an inn-rent; it may
  simply be an accident of `do_quit`'s branch ordering for non-immortals. Flagging rather than
  "fixing" — this is exactly the kind of order-dependent legacy behavior a rewrite needs to
  either deliberately preserve or deliberately call out as a behavior change.
- **No guard against non-guardian followers stacking with a guardian** (mounts, tamed pets,
  orc recruits) — not verified against actual play, since the summon check only scans for an
  *existing guardian specifically*.
- Whether **hallucinate/haze/other mystic AoE room effects** (which are cast by the player, not
  the pet) can affect the caster's own guardian was not investigated — out of scope for this
  doc; flagged only in case it matters for a rewrite's targeting logic.
