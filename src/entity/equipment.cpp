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
// blocks -- calling these primitives in between. As landed (controller
// adjudication), neither wrapper re-derives a stateless guard condition
// after the primitive call: attach_equipment() reports its outcome through
// the 5-arm EquipAttachOutcome enum below, with the too-heavy CHECK
// evaluated pre-affect, at its ORIGINAL position (see the CRITICAL FIX
// paragraph below); detach_equipment() keeps its ORIGINAL obj_data* return,
// proof-justified as sufficient without an added status signal -- see this
// file's detach_equipment() comment for why its HOLD guard's early return
// can never reach the wrapper's poison-check condition. The same
// status-returning shape recurs elsewhere in this wave, e.g. placement.cpp's
// `bool detach_char_from_room(char_data*)`, the char_from_room() SPLIT
// primitive. See handler.cpp's own per-function relocation comments (at
// equip_char/unequip_char) for the exact split-line accounting;
// task-3-report.md has the full byte-fidelity reassembly audit.
//
// CRITICAL FIX (task-3 re-review, post-controller-adjudication): an earlier
// version of this split left the too-heavy weapon CHECK (not just its
// message) in the wrapper, evaluated AFTER attach_equipment() had already
// run affect_modify()/affect_total() -- a real behavior change, since those
// calls can mutate exactly what the check reads (GET_BAL_STR/IS_TWOHANDED).
// The check now lives here, at attach_equipment()'s ORIGINAL position, and
// reports its result via EquipAttachOutcome (handler.h) instead of a raw
// bool -- see that enum's comment and handler.cpp's equip_char() wrapper
// for the exact mapping, and task-3-report.md for the order-aware
// reassembly audit this fix required.

#include "platdef.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <format>

#include "comm.h"
#include "entity_hooks.h"
#include "handler.h"
#include "interpre.h"
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

// max_race_str[] (consts.cpp, L1 core data table): GET_BAL_STR (utils.h)
// expands to a direct max_race_str[GET_RACE(ch)] read -- needed here now
// that attach_equipment()'s too-heavy check (task-3 review CRITICAL fix,
// above) evaluates GET_BAL_STR(ch) itself, at the check's ORIGINAL
// position. Same local extern-declaration pattern handler.cpp uses for
// its own max_race_str reference.
extern int max_race_str[];

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

    // outcome default covers ARMOR/SHIELD/LIGHT/no-dispatch-match -- all
    // originally fell through to affect_modify()/affect_total() with no
    // message decision of any kind, matching EquipAttachOutcome::OTHER.
    EquipAttachOutcome outcome = EquipAttachOutcome::OTHER;

    if (GET_ITEM_TYPE(obj) == ITEM_ARMOR)
        SET_DODGE(ch) += obj->obj_flags.value[3];

    else if (GET_ITEM_TYPE(obj) == ITEM_WEAPON) {
        SET_OB(ch) += obj->obj_flags.value[0];
        SET_PARRY(ch) += obj->obj_flags.value[1];

        // CRITICAL FIX (task-3 re-review; see handler.h's EquipAttachOutcome
        // comment): this check moved here, to its EXACT original position
        // (inside the WEAPON dispatch arm, before affect_modify() below), so
        // GET_BAL_STR(ch)/IS_TWOHANDED(ch) are read BEFORE affect_modify()
        // can mutate the inputs they read (an APPLY_STR affect changes
        // GET_BAL_STR; a bitvector affect can set AFF_TWOHANDED). Only the
        // OUTCOME crosses the call boundary now -- the wrapper emits no
        // message text decision of its own, only the send_to_char() call
        // the chosen outcome selects.
        if (GET_OBJ_WEIGHT(obj) > (GET_BAL_STR(ch) * 50) && !IS_TWOHANDED(ch))
            outcome = EquipAttachOutcome::WEAPON_TOO_HEAVY_ONE_HAND;
        else if (GET_OBJ_WEIGHT(obj) > (GET_BAL_STR(ch) * 100))
            outcome = EquipAttachOutcome::WEAPON_TOO_HEAVY_FOR_YOU;
        else
            outcome = EquipAttachOutcome::WEAPON;

    } else if (GET_ITEM_TYPE(obj) == ITEM_SHIELD) {
        SET_DODGE(ch) += obj->obj_flags.value[0];
        SET_PARRY(ch) += obj->obj_flags.value[1];
    } else if (GET_ITEM_TYPE(obj) == ITEM_LIGHT) {
        if ((location_of(ch) != NOWHERE) && (obj->obj_flags.value[2] != 0)) {
            if (obj->obj_flags.value[3] == 0)
                obj->obj_flags.value[3] = 1;
            room_of(ch)->light++;
        }
    }

    for (int j = 0; j < MAX_OBJ_AFFECT; j++)
        affect_modify(ch, obj->affected[j].location,
            obj->affected[j].modifier,
            obj->obj_flags.bitvector, AFFECT_MODIFY_SET, 0);

    affect_total(ch);

    return outcome;
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
        if ((location_of(ch) != NOWHERE) && (obj->obj_flags.value[2] != 0) && (obj->obj_flags.value[3] != 0)) {
            if (obj->obj_flags.value[3] > 0)
                obj->obj_flags.value[3] = 0;
            room_of(ch)->light--;
        }
    }

    for (j = 0; j < MAX_OBJ_AFFECT; j++)
        affect_modify(ch, obj->affected[j].location,
            obj->affected[j].modifier,
            obj->obj_flags.bitvector, AFFECT_MODIFY_REMOVE, 0);

    affect_total(ch);

    return (obj);
}

// find_eq_pos() RELOCATED to equipment.cpp (rots_entity, L2; Cluster B
// wave Task 1; cb-task-1-brief.md Step 6; cb-census.md section 5.4)
// from act_obj2.cpp: a pure obj-field lookup (CAN_WEAR macro) plus one
// send_to_char() on the invalid-body-part path -- entity-pure, no
// world[]/combat dependency. act_obj2.cpp keeps a local forward
// declaration (do_wear() still calls this downward).

int find_eq_pos(struct char_data* ch, struct obj_data* obj, char* arg)
{
    int where = -1;

    static const std::string_view keywords[] = {
        "!RESERVED!",
        "finger",
        "!RESERVED!",
        "neck",
        "!RESERVED!",
        "body",
        "head",
        "legs",
        "feet",
        "hands",
        "arms",
        "sheild",
        "about",
        "waist",
        "wrist",
        "!RESERVED!",
        "!RESERVED!",
        "!RESERVED!",
        "back",
        "!RESERVED!",
        "!RESERVED!",
        "belt",
        "\n"
    };

    if (!arg || !*arg) {
        if (CAN_WEAR(obj, ITEM_WEAR_FINGER))
            where = WEAR_FINGER_R;
        if (CAN_WEAR(obj, ITEM_WEAR_NECK))
            where = WEAR_NECK_1;
        if (CAN_WEAR(obj, ITEM_WEAR_BODY))
            where = WEAR_BODY;
        if (CAN_WEAR(obj, ITEM_WEAR_HEAD))
            where = WEAR_HEAD;
        if (CAN_WEAR(obj, ITEM_WEAR_LEGS))
            where = WEAR_LEGS;
        if (CAN_WEAR(obj, ITEM_WEAR_FEET))
            where = WEAR_FEET;
        if (CAN_WEAR(obj, ITEM_WEAR_HANDS))
            where = WEAR_HANDS;
        if (CAN_WEAR(obj, ITEM_WEAR_ARMS))
            where = WEAR_ARMS;
        if (CAN_WEAR(obj, ITEM_WEAR_SHIELD))
            where = WEAR_SHIELD;
        if (CAN_WEAR(obj, ITEM_WEAR_ABOUT))
            where = WEAR_ABOUT;
        if (CAN_WEAR(obj, ITEM_WEAR_WAISTE))
            where = WEAR_WAISTE;
        if (CAN_WEAR(obj, ITEM_WEAR_WRIST))
            where = WEAR_WRIST_R;
        if (CAN_WEAR(obj, ITEM_WEAR_BACK))
            where = WEAR_BACK;
        if (CAN_WEAR(obj, ITEM_WEAR_BELT))
            where = WEAR_BELT_1;
    } else {
        if ((where = search_block(arg, keywords, FALSE)) < 0) {
            send_to_char(std::format("'{}'?  What part of your body is THAT?\n\r", arg), ch);
        }
    }

    return where;
}

