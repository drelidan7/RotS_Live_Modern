# RotS documentation

Specifications of RotS's data formats and gameplay systems, written to be complete
enough to **rewrite the game from scratch** — independent of the current C++ source.

RotS is a heavily modified CircleMUD/DikuMUD derivative. Where a format or system still
matches stock CircleMUD it's noted; the value here is capturing the **RotS-specific
extensions and exact field orders**, since the original world data is no longer available
and these docs double as the spec for authoring a minimal bootable test world.

> Every doc cites `file:line` into `src/` so claims are checkable. Items the source left
> ambiguous are recorded under **Open questions** rather than guessed.

## Status

Legend: ✅ done · 🟡 partial · ⬜ not started

### Data formats (`data-formats/`) — the rewrite contract
| Doc | Status | Source of truth |
|-----|--------|-----------------|
| [World text files](data-formats/world-files.md) — rooms/mobs/objs/zones | 🟡 rooms/mobs/objs/zones done; zone-command **semantics** pending | `db.cpp`, `zone.cpp` |
| [Shop files (`.shp`)](data-formats/shop-files.md) | ✅ | `shop.cpp:586 boot_the_shops` |
| [Player save (text)](data-formats/player-save.md) | ✅ write format; load/versioning pending | `db.cpp save_player:2302` |
| [Object/rent binary](data-formats/object-rent-files.md) | ✅ write+layout; load delimiting pending | `objsave.cpp`, `structs.h:1842/1866` |
| [Socials & combat messages](data-formats/socials-and-messages.md) | ✅ | `act_soci.cpp`, `fight.cpp:115` |
| [Text tables & help](data-formats/text-tables.md) | ✅ | `lib/text/`, `db.cpp`, `modify.cpp:723` |
| [Mudlle (`.mdl`) & scripts (`.scr`)](data-formats/mudlle-and-scripts.md) | 🟡 on-disk format ✅; language/opcode enums → systems | `db.cpp`, `mudlle*.cpp` |
| [Maze/level files (`.lev`)](data-formats/maze-files.md) | ✅ | `levgen/maz.c` |

### Gameplay systems (`systems/`) — behavior & formulas
| Doc | Status | Source of truth |
|-----|--------|-----------------|
| [Stats, level & proficiency](systems/stats-and-character-power.md) | ✅ incl. OB/PB/DB derivation (§10) | `profs.cpp`, `limits.cpp`, `char_utils_combat.cpp` |
| [Combat loop — hit & damage](systems/combat-loop.md) | 🟡 swing resolution + damage; timing/armor pending | `fight.cpp::hit` + `utility.cpp` (note: `combat_manager.cpp` is unused) |
| [Combat stat examples](systems/combat-stat-examples.md) | ✅ worked grids (W30/W33/W36) | derived from §6/§10 |
| [Specializations](systems/specializations.md) | 🟡 warrior-side detailed; mage/ranger/mystic stubbed | `*_handler.cpp`, `fight.cpp`, `char_utils.cpp` |
| [Warrior skills](systems/warrior-skills.md) | ✅ incl. spec/race-gated; damage formulas + ranges | `act_offe.cpp`, `fight.cpp`, `olog_hai.cpp` |
| [Magic system — mage spells](systems/magic-system.md) | ✅ damage, saves, resistance, penetration, scaling | `mage.cpp`, `spell_pa.cpp`, `consts.cpp` |

⬜ Prayers/mystic · Ranger/Mystic skills · Races ·
XP/leveling · Movement/zones · Objects/equipment · Mob AI/specprocs · Shops/economy ·
PK/fame · Comms/socials · Shapeshift builder · Guardian spirits · Mudlle engine

> Note: combat/magic formula docs are best validated against a running server (blocked on
> missing world files); documented from source meanwhile, with live-only details flagged.

### Content catalogs (`catalogs/`)
⬜ Spell list · Prayer list · Skill list · Race stats · Item types & wear slots · Socials

See `_TEMPLATE.md` for the per-doc structure.
