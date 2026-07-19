/* containment.cc */

// New TU (placement-seam wave, Task 2; plan
// docs/superpowers/plans/2026-07-19-placement-seam.md; spec
// docs/superpowers/specs/2026-07-19-placement-seam-design.md; census
// .superpowers/sdd/placement-census.md). Joins ROTS_ENTITY_SOURCES
// (rots_entity, L2) alongside placement.cpp: owns the "mutation family" of
// the containment carve out of handler.cpp -- six of the census's seven
// mutation-family functions that link/unlink an obj_data into/out of a
// char's inventory, a room's contents list, another obj_data's contains
// list, or reassign an entire contents chain's owner. Bodies below are
// byte-identical to their handler.cpp originals (census rows
// obj_to_char:751, obj_to_room:1222, obj_from_room:1259, obj_to_obj:1290,
// obj_from_obj:1314, object_list_new_owner:1357 -- line numbers as recorded
// at census time, before Task 1 shifted handler.cpp by removing char_power/
// recount_light_room/get_char_room) except where a function's world[]
// access is replaced by Task 1's room resolver seam -- see each function's
// own comment below for its exact substitution and the original-body
// evidence that selected the resolver variant, per the Task 1 follow-up's
// BINDING addendum (task-2-brief.md): a historically UNCHECKED world[x]
// access uses room_by_id_total() (preserves room_data::operator[]'s
// graceful fallback-room behavior for every input); a historically
// bounds-checked access would use room_by_id() + a null check instead (no
// site in this file needed that variant -- see below).
//
// DEVIATION from the brief's Step 1 list (discovered during this task, not
// pre-existing in the census's disposition table): `obj_from_char` (census
// row 770) is NOT moved here. Its equipment-fallback branch calls
// `unequip_char(...)`, still defined in handler.cpp (app tier) until Task 3
// SPLITs it into equipment.cpp -- the census's own "Upward refs" column
// flagged this ("unequip_char->L2 (equipment sibling)"), but building this
// task's EntityLayerAcyclicity check (rots_entity_linkcheck, which
// force-loads rots_entity against ONLY RotS::core+RotS::platform, no app
// TUs) proved it a hard unresolved-symbol link failure today, not a future
// concern:
//
//   Undefined symbols for architecture arm64:
//     "unequip_char(char_data*, int)", referenced from:
//         obj_from_char(obj_data*) in librots_entity.a[8](containment.cpp.o)
//
// Resolving this the way Task 1 resolved its own undiscovered edges (a
// documented, self-adjudicated deviation rather than inventing new
// production hook machinery the brief's "produces nothing new" scoped out,
// or hand-splitting unequip_char early and duplicating Task 3's own
// planned work): obj_from_char stays in handler.cpp for this task and
// moves alongside unequip_char's SPLIT in Task 3, where the primitive it
// calls will itself be an L2 citizen. See task-2-report.md for the full
// evidence trail.

#include "platdef.h"
#include <cstdlib>
#include <cstring>
#include <format>

#include "entity_hooks.h"
#include "handler.h"
#include "rots/core/character.h"
#include "rots/core/object.h"
#include "rots/core/room.h"
#include "rots/core/types.h"
#include "utils.h"

/* give an object to a char   */
void obj_to_char(struct obj_data* object, struct char_data* ch)
{
    object->next_content = ch->carrying;
    ch->carrying = object;
    object->carried_by = ch;
    object->in_room = NOWHERE;
    object->obj_flags.timer = -1;
    IS_CARRYING_W(ch) += GET_OBJ_WEIGHT(object);
    if (IS_RIDING(object->carried_by))
        IS_CARRYING_W(object->carried_by->mount_data.mount) += GET_OBJ_WEIGHT(object);
    IS_CARRYING_N(ch)
    ++;

    /* set flag for crash-save system */
    if (!IS_NPC(ch))
        SET_BIT(PLR_FLAGS(ch), PLR_CRASH);
}

/* put an object in a room */
//
// world[room] substitution (census row obj_to_room:1222; "Upward refs"
// column: world[].contents/.light/.number->SEAM): the ORIGINAL body indexed
// world[room] unchecked at every site below (no bounds test anywhere in
// the function) -- per the BINDING addendum's per-site selection rule
// ("unchecked world[...] access -> room_by_id_total"), this resolves via
// room_by_id_total(room) into a single local `room_data* r`, mirroring
// placement.cpp's recount_light_room precedent for hoisting the resolved
// pointer once at entry. `world[room].contents`/`.light`/`.number` below
// all become `r->contents`/`.light`/`.number`.
//
// The one non-resolver deviation: the original's debug/error branch built
// its mudlog message via the app-tier global scratch buffer `buf`
// (`strcpy(buf, std::format(...).c_str()); mudlog(buf, ...)`, both symbols
// defined in db_boot.cpp, ROTS_SERVER_SOURCES/app -- an upward edge the
// census's "Upward refs" column for this row did not list, only
// discovered while building this task's EntityLayerAcyclicity check).
// mudlog() takes a std::string_view, so passing the formatted std::string
// straight through is behavior-identical (same message text, same
// mudlog() call) without depending on the app-tier `buf` global at all;
// resolved the same way Task 1 resolved recount_light_room's undiscovered
// top_of_world edge -- see task-2-report.md for the full evidence trail.
void obj_to_room(struct obj_data* object, int room)
{
    int tmp;
    obj_data* tmpobj;

    if (!object)
        return;

    room_data* r = room_by_id_total(room);

    for (tmpobj = r->contents; tmpobj; tmpobj = tmpobj->next_content)
        if (tmpobj == object) {
            mudlog(std::format("obj_to_room: double call for room {}, object {}\n", r->number,
                       object->short_description),
                NRM, LEVEL_IMPL, TRUE);
            return;
        }
    object->next_content = r->contents;
    r->contents = object;

    if (GET_ITEM_TYPE(object) == ITEM_LIGHT) {
        if (object->obj_flags.value[2] && object->obj_flags.value[3]) {
            r->light++;
        }
    }
    for (tmp = 0, tmpobj = r->contents; tmpobj && (tmp < 1000);
        tmpobj = tmpobj->next_content, tmp++)
        ;
    if (tmp >= 1000) {
        mudlog("obj_to_room: infinite loop in room contents.", NRM, LEVEL_GOD, TRUE);
        r->contents = object;
        object->next_content = 0;
    }
    object->in_room = room;
    object->carried_by = 0;
    //   printf("obj_to_room %d, %p, descr:%s\n",r->number,object,object->description);
}

/* Take an object from a room */
//
// world[object->in_room] substitution (census row obj_from_room:1259;
// "Upward refs": world[].contents/.light->SEAM): the ORIGINAL body indexed
// world[object->in_room] unchecked (no bounds test) -> per the addendum's
// selection rule, room_by_id_total(object->in_room) into a local
// `room_data* r`; `world[object->in_room].contents`/`.light` below become
// `r->contents`/`.light`.
void obj_from_room(struct obj_data* object)
{
    struct obj_data* i;

    /* remove object from room */

    if (!object)
        return;

    room_data* r = room_by_id_total(object->in_room);

    if (object == r->contents) /* head of list */
        r->contents = object->next_content;

    else /* locate previous element in list */ {
        for (i = r->contents; i && (i->next_content != object); i = i->next_content)
            ;

        i->next_content = object->next_content;
    }

    if (GET_ITEM_TYPE(object) == ITEM_LIGHT) {
        if (object->obj_flags.value[2] && object->obj_flags.value[3]) {
            r->light--;
            if (object->obj_flags.value[3] > 0)
                object->obj_flags.value[3] = 0;
        }
    }
    object->in_room = NOWHERE;
    object->next_content = 0;
}

/* put an object in an object (quaint)  */
void obj_to_obj(obj_data* item, obj_data* container, char change_weight)
{
    if (!item || !container)
        return;

    item->next_content = container->contains;
    container->contains = item;
    item->in_obj = container;

    if (change_weight) {
        obj_data* tmp_obj = NULL;
        for (tmp_obj = item->in_obj; tmp_obj->in_obj; tmp_obj = tmp_obj->in_obj) {
            GET_OBJ_WEIGHT(tmp_obj) += GET_OBJ_WEIGHT(item);
        }

        /* top level object.  Subtract weight from inventory if necessary. */
        GET_OBJ_WEIGHT(tmp_obj) += GET_OBJ_WEIGHT(item);
        if (tmp_obj->carried_by) {
            IS_CARRYING_W(tmp_obj->carried_by) += GET_OBJ_WEIGHT(item);
        }
    }
}

/* remove an object from an object */
void obj_from_obj(obj_data* item)
{
    if (!item)
        return;

    if (item->in_obj) {
        obj_data* tmp;
        obj_data* obj_from = item->in_obj;
        if (item == obj_from->contains) /* head of list */
        {
            obj_from->contains = item->next_content;
        } else {
            for (tmp = obj_from->contains; tmp && (tmp->next_content != item);
                tmp = tmp->next_content)
                ; /* locate previous */

            if (!tmp) {
                perror("SYSERR: Fatal error in object structures.");
                abort();
            }

            tmp->next_content = item->next_content;
        }

        /* Subtract weight from containers container */
        for (tmp = item->in_obj; tmp->in_obj; tmp = tmp->in_obj) {
            GET_OBJ_WEIGHT(tmp) -= GET_OBJ_WEIGHT(item);
        }

        /* Subtract weight from char that carries the object */
        GET_OBJ_WEIGHT(tmp) -= GET_OBJ_WEIGHT(item);
        if (tmp->carried_by) {
            IS_CARRYING_W(tmp->carried_by) -= GET_OBJ_WEIGHT(item);
        }

        item->in_obj = 0;
        item->next_content = 0;
    } else {
        perror("SYSERR: Trying to object from object when in no object.");
        abort();
    }
}

/* Set all carried_by to point to new owner */
void object_list_new_owner(struct obj_data* list, struct char_data* ch)
{
    if (list) {
        object_list_new_owner(list->contains, ch);
        object_list_new_owner(list->next_content, ch);
        list->carried_by = ch;
    }
}
