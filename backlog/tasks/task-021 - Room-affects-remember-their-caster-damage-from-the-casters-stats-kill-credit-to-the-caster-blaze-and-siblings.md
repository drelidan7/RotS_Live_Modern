---
id: TASK-021
title: >-
  Room affects snapshot the caster's casting state at cast time (damage/saves
  from the snapshot, kill credit to the caster) -- blaze, mist, haze,
  room-poison
status: To Do
assignee: []
created_date: '2026-08-22 17:44'
updated_date: '2026-08-22 17:49'
labels: []
milestone: m-0
dependencies:
  - TASK-020
priority: medium
ordinal: 21000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Today a room affect (blaze, room-poison, haze, mist of baazunga -- every `ROOMAFF_SPELL` written by `affect_to_room`) keeps no record of who cast it or how strong they were: `affected_type` (types.h:700) carries only type/duration/time_phase/modifier/location/bitvector, and `affect_update_room` (limits.cpp:1493-1501) re-casts the spell every tick with the OCCUPANT as both caster and victim (`spell_pointer(tmpch, "", SPELL_TYPE_SPELL, tmpch, ...)`). Consequences: the per-tick damage and saves are computed from the victim's own stats (a level-30 mage's blaze burns a level-5 victim as a level-5 spell; the cast-time `modifier = level` is never used for damage); every tick kill is a self-kill (`killer == victim`), so the caster gets no kill credit or exp, a player victim takes `raw_kill`'s "died to a player" branch (hp/4 restore) because the killer is themselves, and pkill/exploit bookkeeping sees nobody; and the affect is unaffected by the caster dying, leaving, or changing.

**Requirement (owner, 2026-08-22): the room affect SNAPSHOTS the caster's casting state at cast time, so no runtime look-up of the caster is needed for damage/saves.** If the caster later dies (and e.g. loses intelligence to the death penalty), levels, re-specs, or is extracted, every active room spell they cast keeps behaving exactly as it did when cast. Concretely, capture at `affect_to_room` time everything the re-cast formulas read from the caster today -- at minimum: level, mage caster level (`get_mage_caster_level`), magic power (`get_magic_power`), specialization (`get_specialization`, for `get_save_bonus`), the race/side inputs `is_friendly_taget`/`other_side` use, and whatever `new_saves_spell` reads from the caster -- plus the caster's `abs_number` and display name. The `abs_number` is used ONLY for kill credit (resolved through `char_exists()` at the moment of a kill; never a raw `char_data*`); it is never used to re-read stats. If the caster no longer exists at kill time, the kill is credited to no one (the re-cast still uses the snapshot for damage) -- unless the owner rules a different fallback.

**Scope: ALL room-affecting spells**, not just blaze: `spell_blaze` (mage.cpp:2284), `spell_mist_of_baazunga` (mage.cpp:2410/:2440, including the mist's own move at limits.cpp:1541, which must carry the snapshot along), `spell_haze` (mystic.cpp:1169), `spell_poison`'s room arm (mystic.cpp:1303), and `shaperom.cpp:285`'s builder-placed permanent room affects (no caster -- a `no caster` snapshot the re-cast treats like today's behavior). Any future `affect_to_room` caller gets the snapshot by construction (make the caster-bearing constructor the only way to build a `ROOMAFF_SPELL`). The self-damaging DoT char affects (`point_update`'s `damage(i, i, 5, SPELL_POISON)` at limits.cpp:759 and the asphyxiation arm) have the same credit gap; include them only if the owner rules them in (they are char affects, a different list).

Design notes for the implementer (not rulings): the `ASPELL` signature takes a `char_data* caster`, so the re-cast path needs either (a) a per-spell tick entry that takes a `const room_affect_caster&` snapshot, or (b) a synthesized read-only caster proxy -- (a) keeps the existing spell bodies honest and is preferred; pick after reading how much of each body the tick path actually reuses. `affected_type` is a pooled, copied-by-value struct (`*affected_alloc = *af`), so the snapshot can live inline or as an index into a sidecar table keyed by the affect; either way it must survive the mist's `affect_to_room(other) / affect_remove_room(this)` move. The ledger's RR rows for these functions will move; re-derive the ceiling.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 A cast-time snapshot of the caster's casting state (level, mage caster level, magic power, specialization, side/race inputs, the save-roll inputs, abs_number, name) is stored with every ROOMAFF_SPELL at affect_to_room time; no raw char_data* is stored and no stat is re-read from the caster at tick time
- [ ] #2 affect_update_room's re-cast uses the snapshot for damage and saves; a test changes the caster's stats (or extracts the caster) after the cast and proves the tick damage is unchanged
- [ ] #3 A tick kill credits the snapshot's caster when char_exists(abs_number) (exp/group_gain, pkill/exploit path, "died to a player" semantics) and credits no one otherwise -- each pinned by a test; the fallback is owner-ruled
- [ ] #4 All room-affecting spells are converted: blaze, mist of baazunga (including the mist move carrying the snapshot), haze, room-poison, and the builder-placed permanent affects via a no-caster snapshot; any future affect_to_room caller cannot omit it
- [ ] #5 The self-damaging DoT char affects (poison, asphyxiation) are converted or explicitly excluded by owner ruling with the reason recorded
- [ ] #6 Boot goldens + seed42 golden byte-identical or regenerated with the behavior change named in the commit; ASan+UBSan clean; ledger/ceiling re-derived for any moved resolver site
<!-- AC:END -->
