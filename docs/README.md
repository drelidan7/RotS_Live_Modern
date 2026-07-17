# RotS documentation

Specifications of RotS's data formats and gameplay systems, written to be complete
enough to **rewrite the game from scratch** — independent of the current C++ source.

RotS is a heavily modified CircleMUD/DikuMUD derivative. Where a format or system still
matches stock CircleMUD it's noted; the value here is capturing the **RotS-specific
extensions and exact field orders**. The original world/player/object data is now available
again and the data-format docs have been **verified against it** (2026-06); these docs also
double as the spec for authoring a minimal bootable test world.

> Every doc cites `file:line` into `src/` so claims are checkable. Items the source left
> ambiguous are recorded under **Open questions** rather than guessed.

## Status

Legend: ✅ done · 🟡 partial · ⬜ not started

### Data formats (`data-formats/`) — the rewrite contract
| Doc | Status | Source of truth |
|-----|--------|-----------------|
| [World text files](data-formats/world-files.md) — rooms/mobs/objs/zones | ✅ verified vs live `lib/world/`; zone-command semantics + `reset_mode` documented | `db_world.cpp`, `zone.cpp` |
| [Shop files (`.shp`)](data-formats/shop-files.md) | ✅ verified vs live `lib/world/shp/` | `shop.cpp:586 boot_the_shops` |
| [Player save (text)](data-formats/player-save.md) | ✅ verified vs live `lib/players/`; filename encoding + SAVE_VERSION/migration resolved | `db_players.cpp save_player:1556` |
| [Object/rent binary](data-formats/object-rent-files.md) | ✅ verified vs live `lib/plrobjs/`; 32-bit padding/offsets corrected (record = 56 B); alias + delimiting resolved | `objsave.cpp`, `structs.h:1842/1866` |
| [Socials & combat messages](data-formats/socials-and-messages.md) | ✅ verified vs `lib/misc/{socials,messages}` | `act_soci.cpp`, `fight.cpp:115` |
| [Text tables & help](data-formats/text-tables.md) | ✅ verified vs `lib/text/`; dual-use `*_tbl` indexing corrected | `lib/text/`, `db_boot.cpp`, `modify.cpp:723` |
| [Mudlle (`.mdl`) & scripts (`.scr`)](data-formats/mudlle-and-scripts.md) | ✅ on-disk format verified; opcodes pinned to `script.h` + `lib/text/mudlle.keys` | `db_world.cpp`, `mudlle*.cpp` |
| [Maze/level files (`.lev` + `.maz`)](data-formats/maze-files.md) | ✅ `.lev`; live `lib/world/maz/*.maz` hall-library format documented | `levgen/maz.c` |

### Gameplay systems (`systems/`) — behavior & formulas
| Doc | Status | Source of truth |
|-----|--------|-----------------|
| [Class system](systems/class-system.md) | ✅ professions, preset roster, 150-pt custom builds, archetypes | `profs.cpp`, `interpre.cpp`, `consts.cpp` |
| [Stats, level & proficiency](systems/stats-and-character-power.md) | ✅ incl. OB/PB/DB derivation (§10) | `profs.cpp`, `limits.cpp`, `char_utils_combat.cpp` |
| [Combat loop — hit & damage](systems/combat-loop.md) | ✅ swing resolution, damage, hit locations, armor absorption, resists, special-attack funnel | `fight.cpp::hit` + `utility.cpp` (note: `combat_manager.cpp` is unused) |
| [Combat stat examples](systems/combat-stat-examples.md) | ✅ worked grids (W30/W33/W36) | derived from §6/§10 |
| [Specializations](systems/specializations.md) | ✅ all specs incl. selection rules; enum/order/count + spec-field storage cross-checked vs live player files | `*_handler.cpp`, `fight.cpp`, `ranger.cpp`, `mystic.cpp`, `act_othe.cpp` |
| [Races](systems/races.md) | ✅ channels, per-race tables, vision/perception/alignment; IDs + `race_modifiers` cross-checked vs live mob/player data | `consts.cpp`, `profs.cpp`, `utility.cpp` |
| [Weapons](systems/weapons.md) | ✅ weapon-type enum, value-field semantics, damage-type table; cross-checked vs live weapon objects | `structs.h`, `utility.cpp`, `fight.cpp` |
| [Warrior skills](systems/warrior-skills.md) | ✅ incl. spec/race-gated; damage formulas + ranges | `act_offe.cpp`, `fight.cpp`, `olog_hai.cpp` |
| [Ranger skills](systems/ranger-skills.md) | ✅ skill catalog + DEX-vs-ranger-level for dodge/skills | `ranger.cpp`, `utility.cpp`, `consts.cpp` |
| [Magic system — mage spells](systems/magic-system.md) | ✅ damage, saves, resistance, penetration, scaling, mana regen | `mage.cpp`, `spell_pa.cpp`, `consts.cpp` |
| [Cleric / Mystic system](systems/cleric-mystic-system.md) | ✅ powers, saves, mental combat, spirit, scaling | `mystic.cpp`, `clerics.cpp`, `spell_pa.cpp` |
| [XP & leveling](systems/xp-and-leveling.md) | ✅ kill/chip XP, level curve, death loss, advance_level; verified vs live mob + player saves | `fight.cpp`, `limits.cpp`, `profs.cpp` |
| [Movement & zones](systems/movement-and-zones.md) | ✅ move costs, doors, tracking BFS, mounts, ferries, zone-reset runtime; verified vs live `.wld` | `act_move.cpp`, `graph.cpp`, `limits.cpp` |
| [Objects & equipment](systems/objects-and-equipment.md) | ✅ item types, wear slots, `APPLY_*`, encumbrance, containers, decay; verified vs live `.obj` | `structs.h`, `handler.cpp`, `act_obj1/2.cpp` |
| [Mob AI & specprocs](systems/mob-ai-and-specprocs.md) | ✅ activity loop, `MOB_*` catalog (freq. vs 3,723 live mobs), combat AI, specproc catalog | `mobact.cpp`, `spec_pro.cpp`, `interpre.cpp` |
| [Shops & economy](systems/shops-and-economy.md) | ✅ price formulas, money, rent (free), corpse economics; verified vs live `.shp`/`.obj` | `shop.cpp`, `objsave.cpp`, `fight.cpp` |
| [PK & fame (Big Brother)](systems/pk-and-fame.md) | ✅ race-war gates, protection rules, warpoint formulas; `pklist` binary decoded byte-for-byte | `big_brother.cpp`, `pkill.cpp`, `act_offe.cpp` |
| [Communication & socials](systems/communication-and-socials.md) | ✅ languages/garbling, channels, `act()` $-codes, boards, mail; verified vs live `plrmail`/socials | `act_comm.cpp`, `comm.cpp`, `boards.cpp`, `mail.cpp` |
| [Shape OLC builder](systems/shape-olc-builder.md) | ✅ all six `shape` editors, permission model, save/implement semantics + known bugs | `shapemob/rom/obj/zon/script/mdl.cpp` |
| [Guardian spirits](systems/guardian-spirits.md) | ✅ summon path, scaling formulas, AI/lifecycle; verified vs live guardian mob prototypes | `mystic.cpp`, `consts.cpp`, `mobact.cpp` |
| [Mudlle engine (runtime)](systems/mudlle-engine.md) | ✅ trigger model, opcode semantics, suspension/state; worked examples traced vs live `.scr`/`.mdl` | `script.cpp`, `mudlle.cpp`, `mudlle2.cpp` |

> Note: world/player/object **data is available** and every data-format doc plus all systems docs
> have been verified against it. The eleven docs added 2026-07 each passed a three-stage pipeline:
> writer → independent adversarial verifier (every doc had errors found and fixed; all citations,
> formulas, and worked examples re-derived) → final spot-check of all contested claims against
> source. Genuinely unresolved items live in each doc's **Open questions** section.

### Content catalogs (`catalogs/`)
⬜ Spell list · Prayer list · Skill list · Race stats · Item types & wear slots · Socials

See `_TEMPLATE.md` for the per-doc structure.
