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
| [World text files](data-formats/world-files.md) — rooms/mobs/objs/zones | ✅ verified vs live `lib/world/`; zone-command semantics + `reset_mode` documented | `db.cpp`, `zone.cpp` |
| [Shop files (`.shp`)](data-formats/shop-files.md) | ✅ verified vs live `lib/world/shp/` | `shop.cpp:586 boot_the_shops` |
| [Player save (text)](data-formats/player-save.md) | ✅ verified vs live `lib/players/`; filename encoding + SAVE_VERSION/migration resolved | `db.cpp save_player:2302` |
| [Object/rent binary](data-formats/object-rent-files.md) | ✅ verified vs live `lib/plrobjs/`; 32-bit padding/offsets corrected (record = 56 B); alias + delimiting resolved | `objsave.cpp`, `structs.h:1842/1866` |
| [Socials & combat messages](data-formats/socials-and-messages.md) | ✅ verified vs `lib/misc/{socials,messages}` | `act_soci.cpp`, `fight.cpp:115` |
| [Text tables & help](data-formats/text-tables.md) | ✅ verified vs `lib/text/`; dual-use `*_tbl` indexing corrected | `lib/text/`, `db.cpp`, `modify.cpp:723` |
| [Mudlle (`.mdl`) & scripts (`.scr`)](data-formats/mudlle-and-scripts.md) | ✅ on-disk format verified; opcodes pinned to `script.h` + `lib/text/mudlle.keys` | `db.cpp`, `mudlle*.cpp` |
| [Maze/level files (`.lev` + `.maz`)](data-formats/maze-files.md) | ✅ `.lev`; live `lib/world/maz/*.maz` hall-library format documented | `levgen/maz.c` |

### Gameplay systems (`systems/`) — behavior & formulas
| Doc | Status | Source of truth |
|-----|--------|-----------------|
| [Class system](systems/class-system.md) | ✅ professions, preset roster, 150-pt custom builds, archetypes | `profs.cpp`, `interpre.cpp`, `consts.cpp` |
| [Stats, level & proficiency](systems/stats-and-character-power.md) | ✅ incl. OB/PB/DB derivation (§10) | `profs.cpp`, `limits.cpp`, `char_utils_combat.cpp` |
| [Combat loop — hit & damage](systems/combat-loop.md) | 🟡 swing resolution + damage; timing/armor pending | `fight.cpp::hit` + `utility.cpp` (note: `combat_manager.cpp` is unused) |
| [Combat stat examples](systems/combat-stat-examples.md) | ✅ worked grids (W30/W33/W36) | derived from §6/§10 |
| [Specializations](systems/specializations.md) | ✅ all specs incl. selection rules; enum/order/count + spec-field storage cross-checked vs live player files | `*_handler.cpp`, `fight.cpp`, `ranger.cpp`, `mystic.cpp`, `act_othe.cpp` |
| [Races](systems/races.md) | ✅ channels, per-race tables, vision/perception/alignment; IDs + `race_modifiers` cross-checked vs live mob/player data | `consts.cpp`, `profs.cpp`, `utility.cpp` |
| [Weapons](systems/weapons.md) | ✅ weapon-type enum, value-field semantics, damage-type table; cross-checked vs live weapon objects | `structs.h`, `utility.cpp`, `fight.cpp` |
| [Warrior skills](systems/warrior-skills.md) | ✅ incl. spec/race-gated; damage formulas + ranges | `act_offe.cpp`, `fight.cpp`, `olog_hai.cpp` |
| [Ranger skills](systems/ranger-skills.md) | ✅ skill catalog + DEX-vs-ranger-level for dodge/skills | `ranger.cpp`, `utility.cpp`, `consts.cpp` |
| [Magic system — mage spells](systems/magic-system.md) | ✅ damage, saves, resistance, penetration, scaling, mana regen | `mage.cpp`, `spell_pa.cpp`, `consts.cpp` |
| [Cleric / Mystic system](systems/cleric-mystic-system.md) | ✅ powers, saves, mental combat, spirit, scaling | `mystic.cpp`, `clerics.cpp`, `spell_pa.cpp` |

⬜ XP/leveling · Movement/zones · Objects/equipment · Mob AI/specprocs · Shops/economy ·
PK/fame · Comms/socials · Shapeshift builder · Guardian spirits · Mudlle engine

> Note: world/player/object **data is now available** and every data-format doc plus the
> data-checkable systems docs (races, weapons, specializations) have been verified against it
> (2026-06). Combat/magic *formula* docs still need a running server for full validation;
> pure-runtime claims remain flagged in-doc.

### Content catalogs (`catalogs/`)
⬜ Spell list · Prayer list · Skill list · Race stats · Item types & wear slots · Socials

See `_TEMPLATE.md` for the per-doc structure.
