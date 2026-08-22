// src/combat/room_affect_tick.cpp -- TASK-021 Task 5.
//
// The four room-affect tick bodies, run from the caster_snapshot recorded for
// (room, spell) instead of by re-casting the spell with the occupant as its own
// caster. Each body below reproduces the arm of the original ASPELL that
// affect_update_room()'s re-cast used to reach, with three deliberate
// differences (all of them the point of the task):
//
//   * every formula input comes from the SNAPSHOT (mage/mystic caster level,
//     saving-throw DC, spell penetration, specialization) rather than from the
//     victim standing in for the caster;
//   * a lethal tick credits the RECORDED caster -- who may be standing
//     somewhere else, and may no longer exist -- through damage_credited()/
//     apply_spell_damage_credited() rather than crediting the victim itself;
//   * poison_tick() records the poisoner on the victim, so a later poison death
//     resolves back to whoever cast it (resolve_poisoner(), fight.cpp).
//
// The saved arm's two messages are both kept, but re-aimed: the victim-facing
// line always reaches the occupant (the old caster == victim shape suppressed
// it outright inside act()), and the caster-facing "$N shrugs off your poison
// with ease." is delivered only when the recorded caster is still alive AND
// standing in this room -- otherwise there is nobody to address. See
// poison_tick() below for the full account.
//
// Tier: rots_combat (L3). It calls down into rots_entity (affect_join/
// affect_to_char/saves_poison/saves_mystic) and rots_world (room lookups) and
// is called from limits.cpp, a peer -- never upward.

#include "room_affect_tick.h"

#include "char_utils.h"
#include "comm.h"
#include "handler.h"
#include "spells.h"
#include "utils.h"

#include "rots/core/caster_snapshot.h"
#include "rots/core/character.h"
#include "rots/core/room.h"
#include "rots/core/types.h"

// saves_mystic() lives in char_utils_combat.cpp (rots_entity) and no shared
// header ever declared it -- mystic.cpp keeps its own local declaration for the
// same reason (combat-trio wave, Task 1).
char saves_mystic(struct char_data* ch);

namespace {

// The attacker that ENGAGES the occupant for this tick's damage: the recorded
// caster only when it still exists and stands in the same room (so set_fighting
// never pairs characters across rooms); otherwise the occupant itself, exactly
// as the old self-re-cast did.
char_data* engaging_attacker(char_data* caster, char_data* occupant)
{
    if (caster != nullptr && location_of(caster) == location_of(occupant))
        return caster;
    return occupant;
}

// mage.cpp's spell_blaze() victim arm. `dam = number(8, level) + 10`, halved on
// a save, then handed to apply_spell_damage_credited() -- which runs the ONE
// shared scale_spell_damage() the live cast uses, reading the saving throw from
// the snapshot rather than from a live caster.
void blaze_tick(const caster_snapshot& who, char_data* caster, char_data* occupant)
{
    const int level = get_mage_caster_level(who);
    const int save_bonus = get_save_bonus(who, *occupant, game_types::PS_Fire, game_types::PS_Cold);
    const bool saved = new_saves_spell(who, occupant, save_bonus);

    int dam = number(8, level) + 10;
    if (saved)
        dam >>= 1;

    apply_spell_damage_credited(who, engaging_attacker(caster, occupant), occupant, caster,
        dam, SPELL_BLAZE, 0);
}

// mystic.cpp's spell_poison() victim arm. `number(0, magus_save)` there is
// always `number(0, 0)` (magus_save is a zero-initialized local the function
// never writes), so it is spelled out as such here.
void poison_tick(const caster_snapshot& who, char_data* caster, char_data* occupant)
{
    if (!saves_poison(occupant, who) && (number(0, 0) < 50)) {
        affected_type af {};
        af.type = SPELL_POISON;
        af.duration = get_mystic_caster_level(who) + 1;
        af.modifier = -2;
        af.location = APPLY_STR;
        af.bitvector = AFF_POISON;
        affect_join(occupant, &af, FALSE, FALSE);

        // The origin resolve_poisoner() reads when this poison eventually
        // kills, written through the one shared writer (fight.cpp) so the two
        // halves of the record can never disagree. `caster` is the RESOLVED
        // character -- null when the recorded caster is gone, and null when
        // this room affect never had one, in which case nobody is credited.
        // (Task 5 stamped who.abs_number/who.identity_ptr directly here. For a
        // live caster that is the same record; for a departed one it wrote a
        // stale pair that resolve_poisoner() rejects anyway, and for an affect
        // with NO record at all it named the occupant as its own poisoner --
        // which would have made a player's death by a builder-placed poison
        // read as a player kill, the opposite of this tick's documented
        // "nobody credited" fallback.)
        record_poison_origin(occupant, caster);

        send_to_char("You feel very sick.\n\r", occupant);
        damage_credited(engaging_attacker(caster, occupant), occupant, caster, 5, SPELL_POISON, 0);
    } else {
        // The original saved arm (mystic.cpp:1337-1338) sent TWO lines, both
        // anchored on the caster: a TO_VICT line to the poisoned character and
        // a TO_CHAR line to whoever cast it. Anchoring the first on `caster` is
        // also what lets it through act()'s `recipient != ch` gate -- with
        // caster == victim, which is all the pre-TASK-021 room re-cast could
        // produce, act() suppressed the victim's own line entirely and
        // delivered only the (self-addressed) second one. With no caster left
        // to anchor on there is nothing for act() to render, so the line is
        // sent directly, the way this function's other victim-facing message
        // ("You feel very sick.") already is.
        if (caster != nullptr)
            act("You feel your body fend off the poison.", TRUE, caster, 0, occupant, TO_VICT);
        else
            send_to_char("You feel your body fend off the poison.\n\r", occupant);

        // ...and the caster-facing line only when there IS a caster to address:
        // still alive AND standing in this room. A caster who walked away, or
        // who is gone entirely, is told nothing.
        if (engaging_attacker(caster, occupant) == caster)
            act("$N shrugs off your poison with ease.", FALSE, caster, 0, occupant, TO_CHAR);
    }
}

// mystic.cpp's spell_haze() victim arm, for `type == SPELL_TYPE_SPELL` with
// `is_object == 0` -- the shape the room re-cast always produced.
void haze_tick(const caster_snapshot& who, char_data* occupant)
{
    int level = get_mystic_caster_level(who);
    if (who.specialization == game_types::PS_Illusion)
        level += 6;

    const int my_duration = number(0, 1);
    if (!affected_by_spell(occupant, SPELL_HAZE) && !saves_mystic(occupant)) {
        affected_type af {};
        af.type = SPELL_HAZE;
        af.duration = my_duration;
        af.modifier = level;
        af.location = APPLY_NONE;
        af.bitvector = AFF_HAZE;
        affect_to_char(occupant, &af);
        act("You feel dizzy as your surroundings seem to blur and twist.\n\r",
            TRUE, occupant, 0, occupant, TO_CHAR);
        act("$n staggers, overcome by dizziness!", FALSE, occupant, 0, 0, TO_ROOM);
    }
}

// mage.cpp's spell_mist_of_baazunga(), renewal arm. Two long-standing quirks of
// that function are preserved verbatim: the renewal is silent (the "breathes
// out dark mists" messages only ever fired on a FRESH cast), and an adjacent
// room that already carries a mist is renewed against the MAIN room's
// `level / 5` rather than against the smaller `level / 6` it would be seeded
// with.
void mist_tick(const caster_snapshot& who, room_data* room)
{
    const int level = get_mage_caster_level(who);

    if (affected_type* here = room_affected_by_spell(room, SPELL_MIST_OF_BAAZUNGA)) {
        if (here->duration < level / 5)
            here->duration = level / 5;
    }

    for (int direction = 0; direction < NUM_OF_DIRS; direction++) {
        if (!room->dir_option[direction] || room->dir_option[direction]->to_room == NOWHERE)
            continue;

        room_data* const next = room_by_id_total(room->dir_option[direction]->to_room);
        if (affected_type* there = room_affected_by_spell(next, SPELL_MIST_OF_BAAZUNGA)) {
            if (there->duration < level / 5)
                there->duration = level / 5;
            continue;
        }

        affected_type af2 {};
        af2.type = ROOMAFF_SPELL;
        af2.duration = level / 6;
        af2.modifier = IS_SET(next->room_flags, SHADOWY) ? 1 : 0;
        af2.location = SPELL_MIST_OF_BAAZUNGA;
        af2.bitvector = 0;
        affect_to_room(next, &af2, who);
    }
}

} // namespace

bool room_affect_tick(int spell, room_data* room, char_data* occupant, const affected_type& /*affect*/)
{
    const caster_snapshot* const recorded = room_affect_caster(room, spell);
    const bool has_caster = recorded != nullptr && !recorded->is_none();

    // A builder-placed affect, or one that predates the caster store, carries
    // no caster: tick from the occupant's own stats, exactly as the
    // pre-TASK-021 self-re-cast did.
    const caster_snapshot who = has_caster ? *recorded : caster_snapshot::capture(*occupant);
    char_data* const caster = has_caster ? recorded->resolve() : nullptr;

    switch (spell) {
    case SPELL_BLAZE:
        blaze_tick(who, caster, occupant);
        return true;
    case SPELL_POISON:
        poison_tick(who, caster, occupant);
        return true;
    case SPELL_HAZE:
        haze_tick(who, occupant);
        return true;
    case SPELL_MIST_OF_BAAZUNGA:
        mist_tick(who, room);
        return true;
    default:
        return false;
    }
}
