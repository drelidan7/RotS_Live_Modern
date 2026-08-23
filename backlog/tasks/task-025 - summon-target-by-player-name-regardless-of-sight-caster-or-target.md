---
id: TASK-025
title: 'summon: target by player name regardless of sight (caster or target)'
status: To Do
assignee: []
created_date: '2026-08-23 00:15'
updated_date: '2026-08-23 00:50'
labels: []
milestone: m-3
dependencies: []
priority: medium
ordinal: 25000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
As a mage, I want 'cast summon <player>' to find its target by the player's name regardless of whether the caster or the target can currently see, so that a name-targeted world spell is not silently refused by room-sight rules that were designed for in-room targeting.

## Investigation (2026-08-22)

Reported: casting summon on a target that cannot see fails. The spell body itself (src/combat/mage.cpp, spell_summon, ~:824) has NO sight check -- it rejects only NPCs, self, same-room, POSITION_FIGHTING, NO_TELEPORT caster rooms, PRF_SUMMONABLE, immortals, recent-PK (SPELL_ANGER), and a new_saves_spell/other_side roll. The failure is upstream, in target resolution, and the caster sees either 'You failed.'-class wrong-target output or a 'No such player'/report_wrong_target() refusal before the spell ever runs.

Root cause chain:
1. consts.cpp:492 registers summon with targets = 6 = TAR_CHAR_ROOM | TAR_CHAR_WORLD and no TAR_DARK_OK (types.h:219).
2. do_cast (spell_pa.cpp ~:627) resolves the second word through target_from_word() (visibility.cpp:959); its TAR_CHAR_WORLD arm (visibility.cpp:1113) calls get_char_vis(ch, word, dark_ok) -- a VISIBILITY lookup. The pre-delay target_parser path (interpre.cpp ~:875, target_check_one :732) applies the same CAN_SEE(ch, target, dark_ok) gate.
3. get_char_vis -> CAN_SEE(sub, obj, light_mode) (visibility.cpp:527) refuses when: the TARGET's room is dark and the caster lacks infravision/holylight/moonvision (light_mode==0, :573-583); the target is hiding past see_hiding(); GET_INVIS_LEV; AFF_INVISIBLE; AND -- the two arms that make 'the target cannot see' matter -- the CASTER is AFF_BLIND (:596) or the caster is POSITION_SLEEPING or below (:605). Note: the victim being blind does not itself enter CAN_SEE(caster, victim); what the report describes as 'target cannot see' most likely corresponds to the target standing in a DARK room (the :573 arm), which is the most common way a target 'cannot see'. Worth confirming the exact repro with the reporter (blind target vs dark room vs caster blind) before fixing, but every arm is the same gate.
4. Precedent: 'tell' (interpre.cpp:1506) is the existing name-targeted world command and carries TAR_CHAR_WORLD | TAR_DARK_OK, which only lifts the light arm; it still leaves blind-caster/hiding/invis refusals.

Candidate fixes (decide in the task):
 (a) Minimal: add TAR_DARK_OK to summon's target mask in consts.cpp. Lifts only the dark-room arm; caster-blind, target-hiding, target-invisible still refuse. Cheapest, but does not meet 'regardless of sight'.
 (b) Full: resolve summon's target by exact player name with no visibility gate -- e.g. a new TAR_CHAR_WORLD_BY_NAME (or a TAR_ flag meaning 'name lookup, no CAN_SEE') handled in target_from_word()/target_check_one() via a get_player() walk over character_list that matches !IS_NPC && name equality (the get_player_vis() shape at visibility.cpp:646 minus CAN_SEE), with the delayed-cast re-validation in do_cast (spell_pa.cpp :764 TAR_CHAR_WORLD arm) unchanged. The spell body's own PRF_SUMMONABLE / immortal / anger / save gates remain the balance controls. Consider whether wizinvis (GET_INVIS_LEV) should still hide immortals -- the body already refuses GET_LEVEL(victim) >= LEVEL_IMMORT, so a name hit on an imm yields 'You failed.' rather than leaking presence, but 'No such player' vs 'You failed.' is itself an information channel; pick one message for every refusal.

Notes: the spell_summon banner comment says 'We don't use this spell anymore should it be completely removed?' -- it IS wired (spell_pa.cpp:1151) and reachable; retire that comment. PRF_SUMMONABLE is inverted (set == NOT summonable, docs/wizset.md:48); do not 'fix' that in passing. No existing test covers spell_summon or summon targeting (grep tests/ for spell_summon: only spell_registry_tests.cpp).
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 Exact repro confirmed and recorded in the task (which CAN_SEE arm fired: dark target room, caster blind, target hiding/invis) before code changes
- [ ] #2 Refusal messaging is uniform so name-lookup success/failure does not leak a wizinvis immortal's presence beyond what 'who' already shows; decision recorded
- [ ] #3 Other spells' target masks are unchanged (only summon's mask/lookup path moves); tell's TAR_DARK_OK behavior unchanged
- [ ] #4 Stale 'We don't use this spell anymore' comment on spell_summon retired; boot goldens + seed42 golden byte-identical; ASan clean on new test file; room_resolve_census --check green (any new room_of/room_by_id_total site classified in the ledger)
- [ ] #5 summon's target mask gains TAR_DARK_OK (consts.cpp:492, the tell precedent at interpre.cpp:1506); 'cast summon <name>' succeeds against a target standing in a dark room; red-first tests for target_from_word/target_check_one on the summon mask, plus a spell_summon body test
<!-- AC:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
2026-08-22: filed from an owner-reported bug + investigation in this session; Housekeeping milestone because it is a gameplay fix belonging to no active arc. Medium: a real, reproducible player-facing failure of a live spell, no crash.

2026-08-22 (owner ruling): option (a) chosen -- add TAR_DARK_OK to summon's mask, matching tell. Rationale: the target-mask machinery (target_from_word / target_check_one) has no precedent for a no-sight name lookup flag, so a new TAR_ flag would be a first; the owner prefers the established idiom. Partial precedent recorded for the record: a no-visibility name lookup DOES exist as a plain function, get_char(name) (entity_lifecycle.cpp:3372), used by do_tell's non-parser fallback arm (act_comm.cpp:247) and wiz commands (act_wiz.cpp:1352/:1720) -- but never through the target parser. If AC#1's repro turns out to be caster-blind / target-hiding / target-invisible rather than dark-room, (a) will not cover it; reopen the (b) decision then, with get_char() as the lookup to reuse rather than writing a new walk. AC#2 rewritten to (a)'s scope; AC#3 (refusal-message uniformity) stays, now a lighter check since no new lookup path is added.
<!-- SECTION:NOTES:END -->
