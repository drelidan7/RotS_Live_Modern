/* equipment.cc */

// New TU (placement-seam wave, Task 3; plan
// docs/superpowers/plans/2026-07-19-placement-seam.md; spec
// docs/superpowers/specs/2026-07-19-placement-seam-design.md; census
// .superpowers/sdd/placement-census.md). Joins ROTS_ENTITY_SOURCES
// (rots_entity, L2) alongside placement.cpp/containment.cpp: owns the
// equipment-slot primitives SPLIT out of handler.cpp's equip_char (census
// row :815) and unequip_char (census row :919) -- the two functions the
// census could not classify as a clean MOVE because each welds a pure
// state-mutation core to app-tier output (`act`/`send_to_char`) and combat
// (`damage`/`raw_kill`) calls.
//
// attach_equipment(ch, obj, pos) / detach_equipment(ch, pos) below are the
// SPLIT primitives: slot assignment/clear, encumb/leg-encumb/weight math,
// OB/PB/dodge mutation, affect_modify()+affect_total() (both already L2,
// entity_lifecycle.cpp, db-split Task 4b), and the light inc/dec (world[]
// substituted for room_by_id_total() -- both original equip_char's
// world[character->in_room] and unequip_char's world[ch->in_room] accesses
// carried no bounds test of their own, so per the Task 1 follow-up's BINDING
// addendum this is the TOTAL resolver variant, not the nullptr-on-invalid
// one). unequip_char's `mudlog`/`log` "zero object" guard stays in
// detach_equipment -- L0-legal (mudlog/log are rots_platform primitives
// already called from L2 code elsewhere, e.g. db_world.cpp), and it is the
// brief's own explicit disposition for that guard.
//
// handler.cpp's equip_char()/unequip_char() app wrappers keep the public
// names/declarations (handler.h, unchanged) and the census's named "app
// remainder" -- the anti-align/anti-race zap messages + obj_to_char re-drop,
// the "too heavy" send_to_char messages, and the poison damage/raw_kill
// blocks -- calling these primitives in between. See handler.cpp's own
// per-function relocation comments (at equip_char/unequip_char) for the
// exact split-line accounting and the two DEVIATION notes (both wrappers
// re-check a stateless guard condition after the primitive call, since
// attach_equipment()/detach_equipment()'s signatures -- binding, from the
// task brief -- carry no explicit "did the primitive early-return" signal);
// task-3-report.md has the full byte-fidelity reassembly audit.

#include "platdef.h"
#include <cassert>
#include <cstdlib>
#include <cstring>

#include "entity_hooks.h"
#include "handler.h"
#include "rots/core/character.h"
#include "rots/core/object.h"
#include "rots/core/room.h"
#include "rots/core/types.h"
#include "utils.h"

// encumb_table[]/leg_encumb_table[] (consts.cpp, L1 core data table) --
// same local extern-declaration pattern handler.cpp itself used for these
// two arrays (no shared header declares them; see consts.cpp's own
// get_encumb_table()/get_leg_encumb_table() accessors, unused by this file).
extern sh_int encumb_table[MAX_WEAR];
extern sh_int leg_encumb_table[MAX_WEAR];

// attach_equipment() SPLIT primitive (census row equip_char:815; see this
// file's top-of-file comment and handler.cpp's equip_char() wrapper comment
// for the exact split-line accounting). Byte-identical to the corresponding
// slice of the original equip_char body except: (a) parameter names
// character/item/item_slot -> ch/obj/pos, per this task's binding
// attach_equipment(char_data*, obj_data*, int) signature (the primitive is a
// NEW public API entry, not a preserved name, so it takes the plan's
// specified parameter names rather than the original's); (b)
// world[character->in_room] -> room_by_id_total(ch->in_room) (BINDING
// addendum, unchecked historical access); (c) the too-heavy send_to_char
// messages (app-tier output) are NOT here -- they moved to the wrapper.
//
// EquipAttachOutcome return (controller adjudication, supersedes this
// task's original void signature -- see task-3-report.md): the ORIGINAL
// equip_char had a single early `return;` (the HOLD/CAN_WEAR guard, still
// below) that skipped the too-heavy messages, affect_modify()/
// affect_total(), AND the poison block together. A void primitive gave the
// wrapper no way to detect that without re-evaluating the guard itself, so
// this primitive now reports its outcome explicitly: HOLD_EARLY_RETURN on
// that guard (mirroring the ORIGINAL's own `return;` there -- wrapper runs
// nothing further), or WEAPON/OTHER after a full run, decided by the SAME
// GET_ITEM_TYPE(obj) == ITEM_WEAPON test the dispatch below already made
// (evaluated once more, at the return statement, purely to report it --
// not a second evaluation reachable from two different places the way the
// wrapper's old re-check was). The wrapper branches on this value instead
// of re-deriving either the HOLD guard or the WEAPON-type test.
EquipAttachOutcome attach_equipment(char_data* ch, obj_data* obj, int pos)
{
    ch->equipment[pos] = obj;
    obj->carried_by = ch;
    obj->obj_flags.timer = -1;

    // Encumb and weight update:
    ch->points.encumb += obj->obj_flags.value[2] * encumb_table[pos];
    ch->specials2.leg_encumb += obj->obj_flags.value[2] * leg_encumb_table[pos];
    if (encumb_table[pos])
        GET_ENCUMB_WEIGHT(ch) += GET_OBJ_WEIGHT(obj) * encumb_table[pos];
    else
        GET_ENCUMB_WEIGHT(ch) += GET_OBJ_WEIGHT(obj) / 2;
    GET_WORN_WEIGHT(ch) += GET_OBJ_WEIGHT(obj);

    IS_CARRYING_W(ch) += GET_OBJ_WEIGHT(obj);

    if ((pos == HOLD) && !CAN_WEAR(obj, ITEM_HOLD))
        return EquipAttachOutcome::HOLD_EARLY_RETURN;

    if (GET_ITEM_TYPE(obj) == ITEM_ARMOR)
        SET_DODGE(ch) += obj->obj_flags.value[3];

    else if (GET_ITEM_TYPE(obj) == ITEM_WEAPON) {
        SET_OB(ch) += obj->obj_flags.value[0];
        SET_PARRY(ch) += obj->obj_flags.value[1];

    } else if (GET_ITEM_TYPE(obj) == ITEM_SHIELD) {
        SET_DODGE(ch) += obj->obj_flags.value[0];
        SET_PARRY(ch) += obj->obj_flags.value[1];
    } else if (GET_ITEM_TYPE(obj) == ITEM_LIGHT) {
        if ((ch->in_room != NOWHERE) && (obj->obj_flags.value[2] != 0)) {
            if (obj->obj_flags.value[3] == 0)
                obj->obj_flags.value[3] = 1;
            room_by_id_total(ch->in_room)->light++;
        }
    }

    for (int j = 0; j < MAX_OBJ_AFFECT; j++)
        affect_modify(ch, obj->affected[j].location,
            obj->affected[j].modifier,
            obj->obj_flags.bitvector, AFFECT_MODIFY_SET, 0);

    affect_total(ch);

    return (GET_ITEM_TYPE(obj) == ITEM_WEAPON) ? EquipAttachOutcome::WEAPON : EquipAttachOutcome::OTHER;
}

// detach_equipment() SPLIT primitive (census row unequip_char:919; see this
// file's top-of-file comment and handler.cpp's unequip_char() wrapper
// comment for the exact split-line accounting). Parameter names (ch, pos)
// already matched the original unequip_char's own signature, so no rename
// was needed here (unlike attach_equipment() above); the local `obj`
// pointer is likewise unchanged. Byte-identical to the corresponding slice
// of the original unequip_char body except: (a) world[ch->in_room] ->
// room_by_id_total(ch->in_room) (BINDING addendum, unchecked historical
// access); (b) the trailing poison damage/raw_kill block (app-tier
// send_to_char/act/damage/raw_kill) is NOT here -- it moved to the wrapper.
// The `mudlog`/`log` "zero object" guard stays here -- L0-legal, and the
// task brief's explicit disposition for it.
//
// Return type left as obj_data* (controller adjudication considered, then
// confirmed unnecessary to change -- see task-3-report.md): unlike
// attach_equipment() above, this primitive's own `(pos == HOLD) &&
// !CAN_WEAR(obj, ITEM_HOLD)) return obj;` early return does NOT need an
// explicit status signal for its wrapper to reproduce the ORIGINAL's
// control flow, because that early return happens strictly BEFORE the only
// remaining app-tier tail (the wrapper's poison damage/raw_kill block) and
// BEFORE affect_modify()/affect_total() -- so ch's affected-list, and
// therefore IS_AFFECTED(ch, AFF_POISON), is PROVABLY unchanged by this call
// whenever that guard fires. The wrapper captures `was_poisoned =
// IS_AFFECTED(ch, AFF_POISON)` before calling this primitive; if this
// primitive early-returns at the HOLD guard, IS_AFFECTED(ch, AFF_POISON)
// at the wrapper's later check is IDENTICAL to `was_poisoned` (nothing
// touched ch->specials.affected_by in between), which makes the poison
// block's own condition (`was_poisoned != 0 && !IS_AFFECTED(ch,
// AFF_POISON)`) false by construction -- one operand or its exact negation
// can never both hold. The wrapper therefore needs no re-derivation of the
// HOLD guard at all; only the pre-existing `if (!obj) return obj;` null
// check (branching on the return value, not re-deriving
// `!ch->equipment[pos]`) remains, for the zero-object case.
struct obj_data* detach_equipment(char_data* ch, int pos)
{
    int j;
    struct obj_data* obj;

    assert(pos >= 0 && pos < MAX_WEAR);

    if (!ch->equipment[pos]) {
        mudlog("unequip_char called for zero object.", NRM, 0, 0);
        log("unequip_char called for zero object.");
        return 0;
    }

    obj = ch->equipment[pos];

    ch->equipment[pos] = 0;

    ch->points.encumb -= obj->obj_flags.value[2] * encumb_table[pos];
    ch->specials2.leg_encumb -= obj->obj_flags.value[2] * leg_encumb_table[pos];
    if (encumb_table[pos])
        GET_ENCUMB_WEIGHT(ch) -= GET_OBJ_WEIGHT(obj) * encumb_table[pos];
    else
        GET_ENCUMB_WEIGHT(ch) -= GET_OBJ_WEIGHT(obj) / 2;
    GET_WORN_WEIGHT(ch) -= GET_OBJ_WEIGHT(obj);

    IS_CARRYING_W(ch) -= GET_OBJ_WEIGHT(obj);

    if ((pos == HOLD) && !CAN_WEAR(obj, ITEM_HOLD))
        return obj;

    if (GET_ITEM_TYPE(obj) == ITEM_ARMOR) {
        SET_DODGE(ch) -= obj->obj_flags.value[3];

    } else if (GET_ITEM_TYPE(obj) == ITEM_WEAPON) {
        SET_OB(ch) -= obj->obj_flags.value[0];
        SET_PARRY(ch) -= obj->obj_flags.value[1];

    } else if (GET_ITEM_TYPE(obj) == ITEM_SHIELD) {
        SET_DODGE(ch) -= obj->obj_flags.value[0];
        SET_PARRY(ch) -= obj->obj_flags.value[1];

    } else if (GET_ITEM_TYPE(obj) == ITEM_LIGHT) {
        if ((ch->in_room != NOWHERE) && (obj->obj_flags.value[2] != 0) && (obj->obj_flags.value[3] != 0)) {
            if (obj->obj_flags.value[3] > 0)
                obj->obj_flags.value[3] = 0;
            room_by_id_total(ch->in_room)->light--;
        }
    }

    for (j = 0; j < MAX_OBJ_AFFECT; j++)
        affect_modify(ch, obj->affected[j].location,
            obj->affected[j].modifier,
            obj->obj_flags.bitvector, AFFECT_MODIFY_REMOVE, 0);

    affect_total(ch);

    return (obj);
}
