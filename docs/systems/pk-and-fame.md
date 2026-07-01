# PK, Big Brother, and Fame/Warpoints

**Source files:** race-war sides `handler.cpp:127-172` (`other_side`, `other_side_num`),
`utils.h:612-615` (`RACE_GOOD`/`RACE_EVIL`/`RACE_EAST`/`RACE_MAGI` macros), `char_utils.cpp:1026-1083`
(`is_race_good`/`is_race_evil`/`is_race_easterling`/`is_race_magi`/`is_race_haradrim`); command-level
PK gates `act_offe.cpp:52-121` (`do_hit`); Big Brother engagement/looting rules `src/big_brother.cpp` /
`big_brother.h` (all of `game_rules::big_brother`); idle/AFK plumbing `limits.cpp:510-587`,
`act_comm.cpp:754-771`, `interpre.cpp:1256-1266`; death/corpse handling `fight.cpp:651-667`
(`parse_container`), `fight.cpp:673-680` (`move_wearables_to_corpse`), `fight.cpp:685-771`
(`make_physical_corpse`), `fight.cpp:877-958` (`raw_kill`), `fight.cpp:997-1093` (`die`); fame/warpoint
engine `src/pkill.cpp` / `pkill.h` (whole file); display commands `act_info.cpp:3547-3699` (`do_fame`),
`act_info.cpp:3701-3748` (`do_rank`); persistence path `db.h:73` (`PKILL_FILE`),
`db.h:184-195` (`struct player_index_element`); alignment shift on kill `fight.cpp:791-845`
(`change_alignment`).
**Status:** ✅ engagement rules, looting rules, fame formula, persistence lifecycle, death consequences
verified against code and live `lib/misc/pklist*` / `lib/players/` data. 🟡 a couple of code paths
flagged as latent bugs rather than guessed at (see Open questions).

> **Scope.** This is RotS's player-killing stack end to end: who may fight whom, the Big Brother
> protection layer, the warpoints/fame ledger, and how death differs when the killer is a player.
> None of this exists in stock CircleMUD/DikuMUD — **PK, Big Brother, and fame/warpoints are entirely
> RotS-specific**, built on top of the stock combat/death code path. Race identity itself (ids, stat
> tables, the good/evil split) is documented in [races.md](races.md) — this doc only covers the *war*
> mechanics race enables; see races.md §1.8 for the faction/side channel.

---

## 1. Purpose

RotS is a race-war PvP MUD: ten playable races split into "free peoples" and "servants of the enemy"
(races.md §0), and killing enemy players is not only permitted but is the scoring mechanic for the
game-wide "War in Middle-earth" (`do_fame war`). Three cooperating subsystems make this work:

1. **Race-war side checks** (`other_side`/`other_side_num`, `RACE_GOOD`/`RACE_EVIL`/`RACE_MAGI`) decide
   who is a legitimate *enemy* — this gates grouping, some spells, and PK bonuses, but does **not**
   by itself block combat between same-side players (that's Big Brother's job below).
2. **Big Brother** (`game_rules::big_brother`, a Meyer's-singleton world service) is the runtime
   protection layer: it blocks attacks on AFK/looting/god/writing/vastly-mismatched-level targets,
   and enforces a "loot at most 2 items from an enemy's corpse" rule.
3. **pkill/fame** (`src/pkill.cpp`) is the scoring ledger: every valid PK is weighed by level and
   opponent count into "warpoints," which accumulate per character and per race-war side, are ranked,
   and are displayed via `fame`/`rank`.

## 2. Data structures

### 2.1 Race-war side (no dedicated struct — pure functions over `GET_RACE`)
- `RACE_GOOD(ch)` — race id in **(0, 10)** exclusive of the endpoints, i.e. 1-9 (`utils.h:612`).
- `RACE_EVIL(ch)` — race id **> 10** (`utils.h:613`).
- `RACE_EAST(ch)` — race id **== 14** (Easterling, NPC-only) (`utils.h:614`).
- `RACE_MAGI(ch)` — race id **== 15 (Magus/Uruk-Lhuth) OR == 18 (Haradrim)** (`utils.h:615`) — this
  macro already bakes in the "magi bloc" (races.md §1.8).
- `other_side(character, other)` (`handler.cpp:127`): returns **1** (different sides) or **0** (same
  side / at least one NPC without `AFF_CHARM` / either is `RACE_GOD`). Order of checks: NPCs (not
  charmed) are *never* "other side" to anyone; Gods are never "other side"; Easterling-vs-non-Easterling
  is other side; Magi(Magus+Haradrim)-vs-non-Magi is other side; then plain good-vs-evil.
- `other_side_num(ch_race, i_race)` (`handler.cpp:151`) is a race-id-only variant (no `char_data`
  needed) used by `do_whois`; encodes the same three blocs (free peoples ≤ `RACE_BEORNING`(6); the
  Uruk/Orc bloc = race ≥ `RACE_URUK`(11) excluding Magus/Easterling/Haradrim; the Magus+Haradrim bloc)
  and additionally treats **same race, same side** (trivially 0) and *any two different races within
  a bloc* as same-side.
- `game_rules::big_brother::is_same_side_race_war(attacker_race, victim_race)` (`big_brother.cpp:398`)
  is a **third**, independently-written implementation of the identical three-bloc logic (God-anyone,
  good-vs-good, magi-bloc, evil-non-magi-bloc). Functionally equivalent to `other_side`, but used only
  for Big Brother's own loot/alert logic — not literally the same code path as combat's `other_side`.

### 2.2 `game_rules::big_brother` (`big_brother.h:20-141`)
A `world_singleton` (constructed once in `db.cpp:452` at boot). Internal state (`#if USE_BIG_BROTHER`,
always on by default — `big_brother.h:12-14`):

| Member | Type | Role |
|---|---|---|
| `m_corpse_map` | `map<obj_data*, player_corpse_data>` | Tracks every PC (and orc-friend NPC) corpse currently under looting protection. |
| `m_afk_characters` | `set<const char_data*>` | Characters currently AFK-protected (cannot be attacked). |
| `m_looting_characters` | `set<int>` (by `abs_number`) | Characters whose *own* corpse is still protected — used to show "under the protection of the Gods" and block others from attacking them while they're recovering their own loot. |
| `m_last_engaged_pk_time` | `map<const char_data*, tm>` | Timestamp of the last time a character either attacked or was attacked by another player; gates whether AFK protection can be granted. |
| `m_can_be_helpful_skills` / `m_harmful_skills` | `set<int>` | Skill/spell IDs classified as always-offensive vs. situationally-helpful (§4.3), populated once in `populate_skill_sets` (`big_brother.cpp:28-103`). |

`player_corpse_data` (`big_brother.h:68-87`, one entry per protected corpse):

| Field | Meaning |
|---|---|
| `num_items_looted` / `max_num_items_looted` | Running count vs. cap (**always 2**, `big_brother.cpp:637,659`) of items an *enemy* has taken from this corpse. |
| `player_race` | The dead character's race, used for the same-side-loot check. |
| `killer_id` | `abs_number` of the killer (or -1). |
| `player_id` | `abs_number` of the dead character. |
| `is_npc` | True only for tracked orc-friend NPC corpses (see §4.2). |
| `is_killer_pc` | True if a **player** landed the kill — this is what makes the corpse itself un-movable by anyone but the owning side (§4.4). |

### 2.3 `PKILL` (`pkill.h:12-20`) — one record per (killer, victim) pair for a single death
```c
typedef struct {
    int kill_time;                 // time_t of the death
    int killer;                    // idnum on disk; player_table INDEX while resident in memory
    int victim;                    // idnum on disk; player_table INDEX while resident in memory
    unsigned char killer_level;    // "display" level -- see pkill_level(), incognito-masked
    unsigned char victim_level;    // same
    int killer_points;             // warpoints credited to the killer (positive)
    int victim_points;             // warpoints debited from the victim (== -killer_points)
} PKILL;
```
The header comment (`pkill.h:4-11`) flags this as legacy-fragile: `kill_time` should be `time_t` and
`killer`/`victim` should be `long`, but the on-disk binary layout can't change without a migration.
**Dual meaning of `killer`/`victim`:** on disk (and immediately after `pkill_read_file`) these fields
hold the **idnum**; the moment `pkill_update_player_tab` runs, they're overwritten in place with the
**player_table array index** for that idnum, and stay that way until the next `pkill_update_file` call
converts them back to idnums for writing (`pkill.cpp:489-495`, `:461-463`).

### 2.4 `RANKING` (`pkill.cpp:37-46`) — three global leaderboards
```c
typedef struct { long *rank_tab; int rank_len; int rank_used; int side_fame; } RANKING;
RANKING good_ranking, evil_ranking, total_ranking;   // file-local globals
```
`rank_tab` is a dense, warpoints-descending array of `player_table` indexes (grown one slot at a time
by `__insert_rank`); `side_fame` is the running sum of every point ever credited/debited to that side
(used for the war-victory readout, §6). `good_ranking`/`evil_ranking` rank a player against **only
their own side**; `total_ranking` ranks everyone globally (used for `player_table[idx].totalrank`).

### 2.5 `LEADER` (`pkill.h:42-50`) — a display-only snapshot, not persisted
```c
typedef struct { char *name; int player_idx; int rank; int fame; int race; int side; int invalid; } LEADER;
```
Built on demand by `pkill_get_leader_by_rank` (`pkill.cpp:780-812`) purely to feed `do_fame`/`do_rank`
output; freed immediately after printing (`pkill_free_leader`). **There is no persistent "leader" or
"legend" character flag anywhere in the codebase** — see §8.

### 2.6 `player_index_element` (`db.h:184-195`) — where warpoints actually live
```c
struct player_index_element {
  char *name; sh_int level; sh_int race; int idnum; time_t log_time; long flags;
  int warpoints; int rank; int totalrank; char ch_file[80];
};
```
`warpoints`, `rank`, and `totalrank` are **not** written anywhere in the player's own save file
(cross-check: [player-save.md](../data-formats/player-save.md) — the on-disk filename suffix is only
`<name>.<level>.<race>.<idnum>.<log_time>.<flags>`, `db.cpp:522-536`; no warpoints field exists in the
`#player` text format either). `warpoints` is entirely **ephemeral, in-memory state that is rebuilt
from the pkill file on every boot** — see §7.4. This is the single most important fact about fame
persistence: reboot the server with no pkill file and everyone's fame is 0.

## 3. Format / Algorithm — who can attack whom

### 3.1 Command-level gates in `do_hit` (`act_offe.cpp:52-121`), checked *before* Big Brother
`do_hit` is reached from two commands mapped directly onto it — `kill` and `hit` (both dispatch with
`subcmd == SCMD_HIT`, `interpre.cpp:1777,1835`) — and also from `assist`, which calls `do_hit`
internally on the assister's behalf with `subcmd == SCMD_MURDER` (`act_offe.cpp:233`). There is no
`murder` player command (`SCMD_MURDER` is purely an internal subcmd value for `assist`), and `slay`
is a **separate** god-only `ACMD` (`act_offe.cpp:236`): for a mortal caster it just falls through to
`do_hit`, but for an actual god it bypasses every gate below and calls `raw_kill` directly, so it is
out of scope for the rest of this section. In order, for a `kill`/`hit`/`assist` invocation targeting
`victim`:
1. Peace room (`PEACEROOM` room flag) blocks all attacks (`act_offe.cpp:57-61`).
2. Can't hit yourself, can't hit your own charmed pet or your own charmer (`:82-90`).
3. **PvE lowbie guard:** non-NPC `ch` under level 4 attacking an NPC more than `3×level + 2` levels
   above them is refused ("Your arm refuses to attack such a powerful opponent," `:94-99`) — this is
   PvE, not PK. This check is additionally gated on `subcmd == SCMD_HIT` (`:94`), so it only fires for
   the `kill`/`hit` commands themselves, not when the same code path is reached via `assist`.
4. **"Will of the Valar" — good-vs-good newbie lock:** if `ch` is a player under level 10 and **both**
   `ch` and `victim` are `RACE_GOOD`, the attack is blocked outright (`:101-107`). Note this check is
   asymmetric: it only fires for `RACE_GOOD(ch) && RACE_GOOD(victim)` — there is no equivalent
   evil-vs-evil block written in code.
5. **Same-side low-combined-level lock:** if both are players, `!other_side(ch, victim)` (i.e. same
   side of the war, evil-vs-evil included), and `GET_LEVEL(ch) + GET_LEVEL(victim) < 8`, the attack is
   refused with "Group with this person and get some levels instead" (`:108-113`). This is the only
   rule that also protects low-level **evil** players from each other.
6. **Big Brother gate** (`bb_instance.is_target_valid(ch, victim)`, `:115-121`) — see §3.2. Only after
   all five checks above pass does Big Brother get consulted.

None of steps 3-5 consult race-war side except step 5's `other_side` test and step 4's good-vs-good
test — i.e. **cross-side PK between two players is allowed at any level** by this function; the only
level protection for a cross-side victim is Big Brother's 3× level-range rule (§3.3).

### 3.2 `big_brother::is_target_valid` (`big_brother.cpp:266-322`) — the universal attack gate
Called (directly or via the skill-aware overload) from essentially every offensive command/spell path:
`act_offe.cpp`, `fight.cpp` (melee round + `hit()`), `ranger.cpp` (archery/ambush), `spell_pa.cpp`
(spell casting), `clerics.cpp`, `mage.cpp`/`mystic.cpp` transitively via other checks, `olog_hai.cpp`.
Recursion rules, in order:
1. Null pointers, or attacking yourself → always valid (not Big Brother's concern).
2. **Uncharmed NPC attackers are never blocked** — Big Brother only restrains PCs and *charmed*
   NPCs (recurses into `is_target_valid(attacker->master, victim)` for `MOB_PET`/`MOB_ORC_FRIEND`
   mobs, i.e. an orc's recruited pet or anyone's charmed pet inherits its owner's PK rights).
3. **Can't attack a player who is `PLR_WRITING`** (composing mail/notes) — an always-on courtesy
   protection, independent of AFK state.
4. If the *victim* is an NPC: if it's a ridden mount, recurse onto the rider; if it's a pet/orc-friend,
   recurse onto its master; otherwise NPCs are unprotected (`true`).
5. **Can't attack a God** (`victim->player.level >= LEVEL_MINIMM`, i.e. ≥ 91, `structs.h:50-51`).
6. **Can't attack an AFK-protected target** (§4.1).
7. **Can't attack a looting-protected target** (§4.2 — i.e. a player still standing over/recovering
   their own corpse's loot).
8. **Can't attack outside the level-range window** (§3.3).
9. Otherwise valid.

### 3.3 Level-range rule (`is_level_range_appropriate`, `big_brother.cpp:381-395`)
```
attacker_level = get_level_legend_cap(attacker)   // = min(player.level, LEVEL_MAX=30) for PCs
defender_level = get_level_legend_cap(victim)
invalid if attacker_level >= 3 * defender_level
invalid if defender_level >= 3 * attacker_level
```
i.e. neither side may out-level the other by **3× or more**. `get_level_legend_cap` (`char_utils.cpp:315-318`)
is a thin alias of `get_level_a`, which for NPCs returns the raw mob level uncapped, but for PCs
clamps to `LEVEL_MAX` (30) — despite the name, there is no separate "legend"/post-cap level track
in this codebase (see §8).

### 3.4 Skill-aware overload (`is_target_valid(attacker, victim, skill_id)`, `big_brother.cpp:325-348`)
Used by spell/skill entry points that need finer-grained classification than a raw melee hit:
1. If the base target-valid check already passes, allow.
2. Otherwise, if `skill_id` is in `m_harmful_skills` (all melee attack types, archery, offensive
   spells, bash/trap/mark/blind/smash/etc. — the full list is populated in
   `populate_skill_sets`, `big_brother.cpp:36-102`), deny.
3. Otherwise, if `skill_id` is in `m_can_be_helpful_skills` (Curing, Restlessness, Insight,
   Pragmatism, Dispel Regeneration — spells that can target either an ally *or* an enemy), allow only
   if caster and target are on the **same** race-war side (`is_same_side_race_war`) — i.e. you can
   land a "helpful" spell on an enemy as a harmful effect (these five spells double as
   curses/removals) even through Big Brother protection, but you can't use them to buff a protected
   ally.
4. Any skill in neither set (e.g. purely informational/utility skills) is left unrestricted.

## 4. Format / Algorithm — Big Brother protection details

### 4.1 AFK protection
- **Manual:** `do_afk` (`act_comm.cpp:754-771`) sets `PLR_ISAFK` and calls `on_character_afked`,
  *unless* the character is affected by `SPELL_ANGER`, in which case protection is refused outright
  ("You are too angry to be granted the protection of the Gods... wait a few minutes").
- **Automatic idle-out:** `check_idling` (`limits.cpp:510-587`), run once per point-update pulse
  (comments describe the thresholds in minutes): after **3** idle ticks with no `specials.fighting`,
  the character is flagged `PLR_ISAFK` and `on_character_afked` fires (no `SPELL_ANGER` check on this
  path); after **8** they are pulled into an idle "void" room; after **28** they are disconnected.
  Gods (`level >= LEVEL_GOD`, i.e. ≥ 93) are exempt from the AFK/void/disconnect *behaviors*, but their
  idle timer still runs: past 15 ticks they get `PLR_ISAFK` set directly (`limits.cpp:514-516`),
  bypassing `on_character_afked` entirely — so this has display-only consequences (shown as away in
  `look`/`tell`, `act_info.cpp:593`, `act_comm.cpp:254`) and never grants Big Brother attack protection
  (moot anyway, since gods are already unattackable via the separate `LEVEL_MINIMM` check in
  `is_target_valid`, §3.2 step 5).
- **The 15-minute PK cooldown** (`on_character_afked`, `big_brother.cpp:493-529`): protection is
  granted **only if** the character has not been on either end of a player-vs-player attack
  (`on_character_attacked_player`, which stamps `m_last_engaged_pk_time` for *both* combatants) in the
  last `CUTOFF_SECS = 900` seconds. If they engaged more recently, they get "You have engaged in PK
  too recently to benefit from the Gods protection" instead, and remain attackable.
- Protection is cleared the instant the player issues any real command (`interpre.cpp:1256-1266`,
  `PLR_ISAFK` removed, `on_character_returned` fires) — a single keypress is enough to lose it.
- Landing (or receiving) an attack also immediately clears **looting** protection for both parties
  (`on_character_attacked_player`, `big_brother.cpp:486-489`) — you cannot fight back while still
  flagged as a protected looter.

### 4.2 Corpse-looting protection (`on_loot_item`, `big_brother.cpp:114-198`)
Triggered on `on_character_died` (`big_brother.cpp:440-469`), which registers a `player_corpse_data`
entry keyed by the actual corpse object **only if the corpse is non-empty**. Two cases:
- A **PC** corpse: always tracked, and the dead player is added to `m_looting_characters` (their own
  "still recovering loot" protection, distinct from AFK protection).
- An **NPC** corpse: tracked *only* if the NPC is `MOB_ORC_FRIEND` (i.e. an orc's recruited pet,
  races.md §3 "recruit"), and never grants the player-style looting protection — this exists purely
  to extend corpse-immovability (§4.4) to a fallen orc-pet.

When someone attempts to loot an item from a tracked corpse (`on_loot_item`):
1. NPC looters with a living master recurse to the master's permissions.
2. The corpse's **owner** may always loot everything from their own corpse (and this also clears
   their looting protection immediately).
3. Anyone on the **same race-war side** as the dead character may loot everything, unrestricted.
4. **Money is never counted** as an item — coin can always be taken regardless of side.
5. An **enemy** may loot up to `max_num_items_looted` = **2** non-money, non-container items (a
   quiver is explicitly exempted from the container ban and *does* count against the 2-item cap,
   `obj_data::is_quiver()`). Containers themselves (backpacks, sacks, chests) can never be looted by
   an enemy — but see §5 for why worn/wearable gear ends up outside containers on a PK death anyway.
6. The cleanup check is actually `item->next_content == NULL`, i.e. *this successfully-looted item was
   the tail node of the corpse's internal linked list* — not literally "the corpse is now empty."
   `obj_to_obj` always **prepends** (`handler.cpp:1717-1723`: `item->next_content = container->contains;
   container->contains = item;`), so the tail node is whichever item was placed into the corpse
   **first** (the last item in the dead character's own inventory list, assigned wholesale via
   `corpse->contains = character->carrying` before any equipment is prepended — `fight.cpp:730` — or,
   if they carried nothing, the first piece of equipment stripped by `make_physical_corpse`'s wear-slot
   loop). A plain `get all corpse` walks the list head-to-tail (`act_obj1.cpp:246-251`), so in the
   common case the tail item is naturally the last one taken and this does coincide with "corpse is
   empty." But the check only runs inside the `can_loot_item` branch, so a *denied* loot (cap already
   hit) never triggers it — and a looter who names that specific tail-position item directly (instead
   of `get all`) can trigger full cleanup, removing the corpse from `m_corpse_map` and clearing the
   dead player's `m_looting_characters` entry (with a "You feel the protection of the Gods fade from
   you..." message and a `mudlog`), after taking just **one** item — while unrelated loot still sits in
   the corpse, now completely unprotected for anyone, ally or enemy. See Open questions.
Every successful loot (self, ally, or enemy) is logged via `log_item_looted`, and if the looter is on
the *opposite* side, the dead player is sent an anonymous "An enemy looted..." alert; same-side looters
are named to the victim.

### 4.3 Cleanup triggers
- **Disconnect** (`on_character_disconnected`, `comm.cpp:1651-1652`): clears AFK set, looting-eligible
  set, PK-engagement timestamp, and drops any corpse-protection entry the disconnecting player owned.
- **Corpse decay** (`on_corpse_decayed`, `limits.cpp:750-751`): when a corpse object times out and is
  destroyed, its protection entry (and the owner's looting-protected status) is dropped.

### 4.4 Corpse-move protection (`is_corpse_protected`, `big_brother.cpp:239-263`)
Distinct from *looting*: this gate answers whether the corpse **object itself** can be dragged/moved
by a given character. If the corpse's killer was a **player**, the corpse can never be moved by anyone
(`is_killer_pc` short-circuits to protected=true regardless of side). If the killer was an NPC/no one,
the corpse may only be moved by someone on the dead character's own race-war side — an enemy still
cannot relocate (e.g. to hide or stash) a corpse they didn't personally kill as a player.

### 4.5 `get_valid_target` is unimplemented
`big_brother::get_valid_target` (`big_brother.cpp:428-432`) — meant to redirect an attacker to a valid
substitute target when their intended target fails Big Brother's check — is a stub that always returns
`NULL` (`// TODO(drelidan): Implement logic here.`). No caller currently depends on it doing anything
useful; commands simply refuse the action outright when `is_target_valid` fails.

## 5. Format / Algorithm — death consequences, PK vs. non-PK

All deaths funnel through `die()` → (NPC: `raw_kill` directly) / (PC: XP adjustment, then
`raw_kill()` → `make_corpse()` → `big_brother::on_character_died` → stat/position reset →
`extract_char`) (`die()` is `fight.cpp:997-1093`; `raw_kill()` is `fight.cpp:877-958`).

### 5.1 Corpse contents — the "no hiding gear in a bag" rule
`make_physical_corpse` (`fight.cpp:685-771`) always: strips all *worn* equipment into the corpse,
dumps carried gold via `move_gold`, and moves the character's entire inventory (bags and all) into the
corpse. Then, **only if the death was to `SPELL_POISON` or to a player killer** (`attack_type ==
SPELL_POISON || !IS_NPC(killer)`, `:751-753`):
```
move_wearables_to_corpse(corpse) → parse_container(corpse, each top-level container)
```
`parse_container` (`fight.cpp:651-667`) **recursively** walks every bag inside the corpse and pulls
any item that `is_wearable()` or is an `ITEM_KEY` out to the corpse's top level — "so people can't
hide anything." Non-wearable, non-key items (potions, scrolls, quest items, crafting materials, …)
stay nested inside their bag, and since Big Brother never lets an enemy loot a **container**
(§4.2 rule 5), those items are effectively safe from enemy looting even after a PK death — only your
equippable gear and keys are guaranteed exposed. Dying to ordinary mob/environmental damage leaves
your backpacks' contents undisturbed inside the bags in your corpse.

### 5.2 XP loss — a PK death is much cheaper than a mob death
`die()` (`fight.cpp:997-1093`) computes once:
```
base_xp_gain = -(dead_man->points.exp - 3000) / (dead_man->player.level + 2)
```
(a negative number once total exp exceeds 3000 — i.e. a penalty). Then:
- **Every** player death (DT/poison/incap/no killer, killed by a player, or killed by an NPC) applies
  `gain_exp_regardless(dead_man, min(0, base_xp_gain / 10))` — a flat **10%-of-formula** loss that
  always happens.
- **Additionally**, if the killer is a "real" NPC (`IS_NPC(killer)` and **not** `MOB_ORC_FRIEND`/`MOB_PET`
  — i.e. a wild mob, not a player's recruited pet), a **second, full** `min(0, base_xp_gain)` loss is
  applied on top.
- Net effect: dying to another **player** (or to that player's pet/orc-friend) costs **only the 10%
  penalty**; dying to a wild NPC mob costs the **10% penalty plus the full penalty** (~1.1× the base
  formula) — PK death is deliberately the cheaper way to die, XP-wise.
- `SPELL_POISON` deaths get an extra early-out: if the poisoned character is not actively fighting
  when they die, they're killed immediately via `raw_kill` without going through the PK-record/exploit
  bookkeeping below (`fight.cpp:1057-1063`).

### 5.3 Post-death stats — PK death is also gentler on the body
Regardless of cause, `raw_kill` restores `tmpabilities` to the character's max `abilities` and then:
```
died_to_player = (attack_type == SPELL_POISON) || (killer != NULL && !IS_NPC(killer))
```
- **`died_to_player == true`:** current hit points are set to **max_hit / 4**, mana to 0. Base
  ability scores (STR/INT/WIL/DEX/CON/LEA) and move points are **untouched**.
- **`died_to_player == false`** (mob/environmental death): base ability scores are each cut to **2/3**
  of their current value, hit is set to **1**, and both mana and move are set to **0**.

So a PK death leaves you at a quarter health with full stats and moves; a mob death leaves you at 1 HP,
no mana/move, and a temporary stat cut. (Non-immortal characters are also respawned at their race's
mortal-start room in both cases; immortals go to the immortal start room instead.)

### 5.4 Alignment shift on a kill (`change_alignment`, `fight.cpp:791-845`)
Called once per **group-credited** killer against the dead target (`fight.cpp:1298`, inside the XP
group-share loop — i.e. this fires for *any* kill, mob or PK, not only PK). For `RACE_GOOD(ch)`
killers: killing an evil-aligned victim always shifts alignment toward good (a big jump if the killer
wasn't already evil-leaning themselves, `GET_ALIGNMENT(ch) >= 0`; a slow trickle — divisor 100 vs. 200
depending on the victim's aggressiveness — if the killer had drifted evil); killing a good-aligned
victim always costs alignment, more so if the victim was peaceful (`/2`) than if it was aggressive
(`/10`).

For evil-race killers the direction is easy to misread: `align = 1` (a flat, tiny **gain**, i.e. less
evil) fires when the **victim was more evil than the killer**
(`GET_ALIGNMENT(victim) < GET_ALIGNMENT(ch)` — lower/more-negative alignment means more evil);
otherwise the shift is `-(victim's alignment / 25)`, which is still a **gain** if the victim was evil
(just less evil than the killer, so its alignment is closer to zero) and only becomes a **loss** when
the victim was good-aligned (positive), proportional to how good it was. Net effect: an evil-race
killer essentially only *loses* alignment by killing good-aligned targets — killing any evil target,
even a milder one, nudges them (slightly) toward less evil. Alignment is then clamped to the killer's
race's `min_race_align`/`max_race_align` band (races.md §1.7). This is the same mechanic for mob kills
and PK kills; nothing PK-specific changes here beyond the fact that PK victims are player-aligned
rather than mob-aligned.

## 6. Format / Algorithm — pkill/fame computation

### 6.1 Eligibility (`pkill_valid_killer`, `pkill.cpp:136-158`)
A combat-list participant is a valid "killer" for record-creation purposes only if:
- It is a **player** below `LEVEL_IMMORT` (91), **or**
- It is an NPC that is **both** `MOB_ORC_FRIEND` and `MOB_PET` (i.e. a live orc-recruited pet,
  races.md §3 "recruit"/`do_recruit`), **and** its master is **not simultaneously** also fighting the
  same victim (to avoid double-crediting one PK — the master's own combat-list entry would otherwise
  generate its own separate, correctly-attributed record).
- Immortals of any kind are never valid killers (no PK record, no fame, no alignment/points).

### 6.2 Weight (`pkill_weight`, `pkill.cpp:111-130`)
```
total_levels = Σ GET_LEVEL(c) for every c in combat_list where c->specials.fighting == victim
total_levels = max(total_levels, victim->specials.attacked_level)
weight = 0                                   if total_levels == 0
       = GET_LEVEL(victim) * 1000 / total_levels²   (integer division)  otherwise
```
Crucially, `total_levels` sums **every** current combatant against the victim — including invalid
killers (immortals, ordinary NPC mobs) that will never get their own PKILL record — so a solo player's
credited weight shrinks sharply if other (uncredited) attackers were also piling on. This rewards
solo/duo kills of a strong victim and punishes gang-kills even for the one player who does get a
record (see the worked example, §9).

### 6.3 Points (`pkill_points`, `pkill.cpp:177-183`)
```
points(victim, killer, weight) = GET_LEVEL(killer) * weight   if other_side(victim, killer)
                                = 0                            otherwise (same-side kill: no fame)
```
`killer_points = points`; `victim_points = -points` (`pkill.cpp:533-535`) — every PK is a strict
zero-sum transfer of warpoints from victim to killer.

### 6.4 Display level masking (`pkill_level`, `pkill.cpp:185-191`)
```
pkill_level(c) = 50   if PLR_FLAGGED(c, PLR_INCOGNITO)
              = GET_LEVEL(c)   otherwise
```
This is what gets stored in the record's `killer_level`/`victim_level` bytes — **not** what feeds the
weight/points math above (those always use the real `GET_LEVEL`). Practical effect: an incognito
character's true level is hidden from the `fame`/pkill display **and** from the expiration-day
calculation (§7.2, which reads the stored, possibly-masked level) — but their true level still governs
how much fame the kill is actually worth.

### 6.5 Creating a record — `pkill_create` (`pkill.cpp:543-560`), called from `die()` (`fight.cpp:1070`)
Only for a non-NPC, non-immortal victim. Computes `weight` and the number of valid `opponents`, then
for **every** valid killer currently fighting the victim, appends one `PKILL` record
(`pkill_update_pkill_tab`, `pkill.cpp:506-541`) with that killer's own `GET_LEVEL(killer) * weight`
points, immediately (1) folds those points into `player_table[...].warpoints` and re-sorts the
rankings (`pkill_update_player_tab` → `pkill_update_rank`), and (2) appends the record to the on-disk
pkill file in **append mode** (`pkill_update_file`, opens with `"a"`, `pkill.cpp:444`) — so warpoints
and the on-disk ledger are both updated in real time, per kill, not just at reboot.

### 6.6 Rank maintenance (`__insert_rank`/`__delete_rank`/`__shift_rank`, `pkill.cpp:198-343`)
`pkill_update_rank(idx)` deletes the player from both their side's `RANKING` and `total_ranking` (a
`memmove`-based shift of the dense rank array), then — **only if their new warpoints are ≥ 0** —
re-inserts them at the correct sorted position in both. Negative-fame players are dropped from the
leaderboards entirely (`player_table[idx].rank/totalrank = PKILL_UNRANKED = -1`) but keep their
(negative) warpoints value; they simply don't show up in `fame war`/`rank`.

## 7. Format / Algorithm — persistence: `misc/pklist`

### 7.1 File location and on-disk record layout
`PKILL_FILE = "misc/pklist"` (`db.h:73`). It is a flat binary array of `PKILL` structs (§2.3), written
with the platform's native struct layout/padding — on the 32-bit build this is **24 bytes per record**
(3× `int` + 2× `unsigned char` + 2 bytes of alignment padding + 2× `int`). On disk, `killer`/`victim`
are **idnums**, not player-table indexes (`pkill.cpp:461-463`).

### 7.2 Expiration (`pkill_expired`, `pkill.cpp:84-102`)
```
days_passed  = (now - kill_time) / 86400
days_allowed = 1                                if killer_level == 0
             = 30 * victim_level / killer_level  otherwise   (both are the stored, possibly-masked levels)
expired = days_passed >= days_allowed
```
A kill of a much-higher-level victim by a much-lower-level killer decays **slowly** (long
`days_allowed`); a kill of a much-lower-level victim decays almost immediately. This directly rewards
"giant-killing" not just in raw points (§6.2-6.3) but in how long that fame stays on the books.

### 7.3 Boot lifecycle (`boot_pkills`, `pkill.cpp:712-727`) — the actual persistence mechanism
```
1. pkill_read_file(PKILL_FILE)        — read every record currently in the file, no filtering.
2. pkill_update_player_tab(...)       — apply EVERY record's points to player_table[...].warpoints,
                                         unconditionally (no expiration check here).
3. pkill_delete_file(PKILL_FILE)      — truncate the file to empty.
4. pkill_update_file(PKILL_FILE, ...) — rewrite only the records that are NOT expired as of now.
```
The header comment (`pkill.cpp:8-12`) states this outright: *"the pkill expiration functionality
relies on the daily reboot scheme to filter out old pkills."* The practical consequence: a record that
technically expired since the last reboot **still counts toward warpoints for the remainder of the
current uptime** (step 2 doesn't filter), and is only dropped from the file (and thus stops counting)
starting from the **next** reboot's step 2. Fame is therefore always exactly "sum of every pkill
record currently in `misc/pklist`, evaluated as of the last boot" — it is **not** stored per-character
anywhere; `player_table[idx].warpoints` starts at 0 every boot (`db.cpp:2197,2258`) and is rebuilt
purely from this file.

### 7.4 Verified against live data
The live-dev `lib/misc/pklist` is currently empty (0 bytes — a fresh `make setup` checkout has no PK
history). Two historical backups are present and decode cleanly against the current 24-byte struct
layout:
- `lib/misc/pklist.bak.jan-24-2008` — **2856 bytes / 24 = exactly 119 records.** Decodes cleanly;
  used for the worked example below.
- `lib/misc/pklist.old` — **9860 bytes**, divides evenly by **20**, not 24 (`9860/20 = 493` exactly).
  This is a fossil from an **older** `PKILL` layout that predates the `killer_level`/`victim_level`
  fields (5 plain `int`s: `kill_time, killer, victim, killer_points, victim_points` = 20 bytes). It is
  not read by the current `pkill_read_file` (which only ever opens `PKILL_FILE`); it's dead weight
  left over from a prior migration, not a bug in the live system.

## 8. Format / Algorithm — display (`do_fame`, `do_rank`) and "leader"

- **`fame war`** (`act_info.cpp:3574-3617`): prints the top-10 leaderboard for each side
  side-by-side (via `pkill_get_leader_by_rank(i, RACE_WOOD)` for a representative good race and
  `RACE_URUK` for a representative evil race — the race argument only selects which `RANKING` struct
  to read, per `__pkill_side`), then each side's total fame (`pkill_get_good_fame`/`pkill_get_evil_fame`
  = `side_fame / 100`), then a one-line victory readout (good total > evil total, evil > good, or tied).
- **`fame all`**: lists every kill from the last 24 real-time hours (`pkill_get_new_kills`,
  `pkill.cpp:681-702` — relies on the table being kept in chronological order and returns everything
  from the first in-window record onward).
- **`fame <name>`**: lists every historical record (bounded only by whatever survived expiration/reboot,
  §7.3) where that character is killer or victim, and a final line with total fame =
  `player_table[idx].warpoints / 100`.
- **`rank`** (`act_info.cpp:3701-3748`): shows the caller's position in their own side's ranking, with
  3 entries above and below.
- **Displayed "fame" is always `warpoints / 100`** (integer division, truncating toward zero) — the
  raw per-kill point values (hundreds to low thousands per §9) are two orders of magnitude larger than
  the "fame" number players actually see.
- **No "leader"/"legend" character flag exists.** `LEADER` (§2.5) is a transient display struct; the
  word "leader" in this codebase means "occupant of rank N on a `RANKING` array," nothing more. There
  is likewise no extended post-cap "legend level" track — `get_level_legend_cap` (§3.3) is just an
  alias for the ordinary level-30 cap.

## 9. Worked example (real historical data, decoded from `lib/misc/pklist.bak.jan-24-2008`)

Record #3 of 119 (0-indexed), decoded with format `<iiiBBxxii>`:

| Field | Value |
|---|---|
| `kill_time` | `1197517419` → **Wed Dec 12 22:43:39 2007** |
| `killer` (idnum) | `1009877099` — no live character file survives under this idnum (deleted/renamed since). |
| `victim` (idnum) | `1009863807` — matches the live save file `lib/players/F-J/girith.36.1.1009863807.1205439977.0`: **Girith**, race **1 = Human** (a free-peoples race), currently saved at **level 36**, no `PLR_*` flags set (suffix `0`). |
| `killer_level` (stored) | **24** |
| `victim_level` (stored) | **36** — matches Girith's live-saved level exactly, and the save's `log_time` (1205439977, Mar 2008) is shortly after this death, consistent with no further leveling since. |
| `killer_points` | **1488** |
| `victim_points` | **-1488** |

**Reproducing the math** (§6.2-6.3), assuming this was a solo kill (`total_levels` = the killer's own
real level, 24 — no evidence of other combatants in the record set for this timestamp):
```
weight  = floor(victim_level * 1000 / total_levels²) = floor(36 * 1000 / 24²) = floor(36000 / 576) = 62
points  = killer_level(real) * weight = 24 * 62 = 1488          ✓ matches killer_points exactly
```
Since `points ≠ 0`, this confirms `other_side(Girith, killer)` was true — i.e. this was a genuine
cross-side PK (Girith, a free-peoples Human, killed by an evil-side character).

**Applying it to the ledger:** Girith's `warpoints` drop by 1488 (displayed fame drops by
`1488/100 = 14`, truncated); the killer's `warpoints` rise by 1488 (+14 displayed fame), and
`evil_ranking.side_fame` increases by 1488 while `good_ranking.side_fame` decreases by 1488
(`pkill_update_character_by_id`, §2.4). **Decay window:** `days_allowed = 30 * 36 / 24 = 45` — this
record remains in `misc/pklist` (and keeps counting toward both warpoints totals) for 45 days of
in-game uptime after Dec 12 2007, checked only at each reboot (§7.2-7.3).

**A cautionary companion data point** in the same file: the *same* killer (idnum `1009877099`) has two
more records only 26 and 50 minutes later — one against Girith again (`killer_level=24`,
`victim_level=36`, `killer_points=192`) and one against a different victim, idnum `1009861622`
(live file `lib/players/K-O/marty.41.2.1009861622...`, a level-41 Dwarf today) with `killer_points=24`.
Both are far below the 1488 of the first kill despite similar or higher stored victim levels, which is
exactly what §6.2's `total_levels` sensitivity predicts: if other combatants (even ones who never earn
their own PKILL record — a group of NPCs, or immortals, or players who didn't land the last hit) were
also fighting the same victim, `total_levels` — and therefore `weight` — collapses quickly. The exact
attacker roster behind those two follow-up kills cannot be reconstructed from the pkill file alone
(see Open questions).

## 10. RotS-specific notes

Everything in this document is RotS-specific — there is no PK, Big Brother, or fame/warpoints system
in stock CircleMUD/DikuMUD, which is built for cooperative PvE with optional, unscored PvP. The parts
of the *combat* pipeline this system hooks into (`die`, `raw_kill`, `make_corpse`, `change_alignment`)
are stock-derived functions that RotS has heavily modified to add the PK-specific branches described
in §5 and §9; the branch points themselves (`died_to_player`, the `pkill_create`/`add_exploit_record`
calls, the `game_rules::big_brother` gate calls) are the RotS additions layered on top.

## 11. Open questions

- **Likely out-of-bounds read for orc-pet PK records.** `pkill_valid_killer` (`pkill.cpp:136-151`)
  allows an NPC to be the recorded "killer" only when it is a live orc-recruited pet (`MOB_ORC_FRIEND`
  + `MOB_PET`) whose master is *not* also currently fighting the same victim. In that path, the record's
  `killer` field is the **pet's own `specials2.idnum`** (`pkill.cpp:528`), which will not resolve via
  `find_player_in_table` (`pkill_update_character_by_id`, `pkill.cpp:345-362`) and so
  `pkill_update_player_tab` overwrites `p->killer` with **`-1`** (`pkill.cpp:489`). `pkill_update_rank(-1)`
  safely no-ops, but `pkill_update_file` (`pkill.cpp:437-472`) unconditionally does
  `p.killer = player_table[p.killer].idnum;` (`pkill.cpp:462`) — i.e. `player_table[-1]`, an
  out-of-bounds read. This is only reachable in the narrow case where an orc's pet independently lands
  a killing blow while its master isn't simultaneously engaged with the same victim. Not exercised in
  the (empty) live `pklist`, so it may simply never trigger in practice, but it reads as a real latent
  bug rather than intentional behavior — flagging rather than fixing per doc scope.
- **`total_levels` in `pkill_weight` counts uncredited combatants.** As shown in §9, the weight
  denominator sums every current combatant against the victim (valid killer or not), but only valid
  killers ever get a `PKILL` record. This means a fame value in the pkill file cannot be fully
  "explained" from the file alone — the size of the attacking group (including any NPCs/immortals
  involved) is lost information once the record is written. Unclear whether this is an intentional
  anti-zerg design (it certainly *behaves* that way) or an artifact of reusing the same combat-list
  scan for both `pkill_weight` and `pkill_opponents` (which *does* filter through `pkill_valid_killer`,
  `pkill.cpp:160-172`).
- **Corpse-looting protection can end early if the "first-in" item is targeted directly.** As detailed
  in §4.2 point 6, `on_loot_item`'s end-of-corpse cleanup keys off `item->next_content == NULL` (this
  item was the tail of the corpse's linked list), not an actual "0 items remain" count. Because
  `obj_to_obj` prepends, the tail item is deterministically whichever item was placed into the corpse
  *first* — normally invisible to players, but not unguessable (e.g. always the last-listed item in
  `inventory` before death, or a specific wear-slot's item if the victim was carrying nothing). A
  looter who does `get <that item> corpse` instead of `get all corpse` can end all looting protection
  for the corpse (self, ally, and enemy-cap alike) after taking a single item, exposing the rest of the
  loot to anyone. Not something the (empty) live `pklist`/corpse data can confirm has ever happened;
  flagging as a latent exploit rather than fixing per doc scope.
- **Three independent implementations of "same side"** (`other_side` in `handler.cpp`, `other_side_num`
  in `handler.cpp`, and `big_brother::is_same_side_race_war` in `big_brother.cpp`) encode the same
  three-bloc logic (good bloc / Uruk+Orc bloc / Magus+Haradrim bloc) but are written independently with
  no shared helper. They appear consistent for all ten playable races by inspection, but a future
  race addition would need all three updated in lockstep — a latent maintenance hazard rather than a
  current bug.
- **`pklist.old`'s older 20-byte record format** (§7.4) is unreachable from the running game (wrong
  filename) and was not decoded further than confirming the byte-count fit; if it's ever useful for
  historical fame reconstruction, its exact field order among the 5 ints was not verified here (assumed
  `kill_time, killer, victim, killer_points, victim_points` by analogy with the current struct minus
  the two level bytes, but not confirmed against a second independent record).
