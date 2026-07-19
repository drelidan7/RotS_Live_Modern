/* ************************************************************************
 *   File: handler.c                                     Part of CircleMUD *
 *  Usage: internal funcs: moving and finding chars/objs                   *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

/**************************************************************************
 *  ROTS Documentation                                                     *
 *                                                                         *
 *  Handling Affections                                                    *
 *    An affection should be applied to a character using affect_to_char   *
 *    with pointers to the character and the new affection.                *
 *    To remove an affection use affect_from_char sending a pointer to the *
 *    character and the skill number (from spells_pa.cc).                  *
 *    To remove an unknown affection from a character use affect_remove    *
 *    after checking that the pointer you are sending to the function is   *
 *    present in ch->affected.                                             *
 *                                                                         *
 *  affected_type_pool                                                     *
 *    This is a linked list of unused affections which can be allocated to *
 *    characters as and when they are needed.  Once the affection is       *
 *    removed from a character it returns to the pool until it is needed.  *
 *    If the pool becomes empty then a call to get_from_affected_type_pool *
 *    will allocate resources for a new affection. Rooms also use this     *
 *    pool since their affection handling should be almost identical to    *
 *    characters.                                                          *
 **************************************************************************/

#include "platdef.h"
#include <assert.h>
#include <cctype>
#include <charconv>
#include <ctype.h>
#include <format>
#include <iterator>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "spells.h"
#include "rots/core/character.h"
#include "rots/core/object.h"
#include "rots/core/room.h"
#include "rots/core/descriptor.h"
#include "rots/core/tables.h"
#include "rots/core/types.h"
#include "text_view.h"
#include "utils.h"
#include "zone.h" /* For zone_table */

#include "base_utils.h"
#include "char_utils.h"
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

/* external vars */
extern struct room_data world;
extern struct obj_data* object_list;
extern struct char_data* character_list;
extern struct index_data* mob_index;
extern struct index_data* obj_index;
extern struct descriptor_data* descriptor_list;
extern struct char_data* fast_update_list;
extern const std::string_view MENU;
extern struct skill_data skills[];
extern sh_int encumb_table[MAX_WEAR];
extern sh_int leg_encumb_table[MAX_WEAR];
extern long race_affect[];
extern int max_race_str[];

/* external functions */
void free_char(struct char_data*);
// recount_light_room() relocated to placement.cpp (placement-seam Task 1);
// this file's own remaining caller (below) needs its own local forward
// declaration now that the definition is no longer earlier in this file --
// same convention as limits.cpp's existing local declaration.
void recount_light_room(int room);
void stop_fighting(struct char_data*);
void remove_follower(struct char_data*);
void clear_memory(struct char_data*);
void show_character_menu(struct descriptor_data* d);

ACMD(do_save);
ACMD(do_return);

// char_control_array relocated to entity_lifecycle.cpp alongside
// char_exists()/set_char_exists()/remove_char_exists() (entity-seed Task 5);
// declarations unchanged in handler.h.
//
// last_control_set (formerly declared here) relocated to entity_lifecycle.cpp
// too (world-seed Task 1), alongside register_npc_char() (below,
// formerly here), its only nontrivial user.

int dummy_affected_var = 17;
// affected_list/affected_list_pool relocated to entity_lifecycle.cpp
// (entity-seed Task 5); affect_to_room()/affect_remove_room() below still
// read/write these, so they stay declared here via extern.
extern universal_list* affected_list;
extern universal_list* affected_list_pool;

// affected_type_pool/affected_type_counter (private backing state for
// get_from_affected_type_pool()/put_to_affected_type_pool() below) relocated
// to entity_lifecycle.cpp alongside those two functions (entity-seed Task 5)
// -- no other handler.cpp function reads them.

struct affected_type* get_from_affected_type_pool();
void put_to_affected_type_pool(struct affected_type*);

follow_type* follow_type_pool = 0;
int follow_type_counter = 0;

struct follow_type* get_from_follow_type_pool();
void put_to_follow_type_pool(struct follow_type*);

// fname() (+ its private fname_nameholder scratch buffer) relocated
// verbatim to char_utils.cpp (entity-completion Task 1): pure text
// manipulation, fname_nameholder is fname()'s only reader. Declaration
// stays in handler.h.

// char_power() relocated verbatim to placement.cpp (placement-seam Task 1):
// pure int math (MIN macro, LEVEL_IMMORT), no world/live-state dependency.
// Declaration stays in handler.h.

// other_side()/other_side_num() relocated verbatim to char_utils.cpp
// (entity-completion Task 1): pure IS_NPC/AFF_CHARM/GET_RACE/RACE_* macro
// logic -- no handler.cpp world/live-state dependency. Declarations stay
// in handler.h; the other_side() calls below still resolve through that
// declaration, now to the char_utils.cpp definition.

// recount_light_room() relocated to placement.cpp (placement-seam Task 1):
// its world[room] accesses become room_by_id(room) (rots_world resolver
// seam) -- see that file for the moved body and task-1-report.md for the
// exact substitution. No handler.h declaration to update (every caller,
// e.g. limits.cpp, already forward-declares this function locally).

// isname_c_string() (this anonymous-namespace helper, isname_nullable()'s
// sole caller) relocated to entity_lifecycle.cpp alongside isname_nullable()
// (entity-seed Task 5).

int isname(std::string_view query, std::string_view name_list, char full)
{
    query = rots::text::truncate_at_null(query);
    name_list = rots::text::truncate_at_null(name_list);

    std::size_t first_query_character = 0;
    while (first_query_character < query.size() && query[first_query_character] <= ' ') {
        ++first_query_character;
    }
    if (first_query_character == query.size()) {
        return 0;
    }
    query.remove_prefix(first_query_character);

    if ((query.size() < 3) || (query.size() > 4)) {
        full = 1;
    }

    // Bounded transliteration of isname_c_string: name_index is the single cursor the legacy
    // walk advances through the namelist, including during comparison, so candidate word starts
    // (byte 0 verbatim, then alpha-run/separator skips from the mismatch point) stay identical
    // to the retained C-string matcher even for keywords beginning with digits or punctuation.
    std::size_t name_index = 0;
    for (;;) {
        std::size_t query_index = 0;
        for (;;) {
            const bool name_exhausted = (name_index == name_list.size());
            if (query_index == query.size()
                && (!full || name_exhausted
                    || !std::isalpha(static_cast<unsigned char>(name_list[name_index])))) {
                return 1;
            }
            if (name_exhausted) {
                return 0;
            }
            if (query_index == query.size() || name_list[name_index] == ' '
                || LOWER(query[query_index]) != LOWER(name_list[name_index])) {
                break;
            }
            ++query_index;
            ++name_index;
        }

        while (name_index < name_list.size()
            && std::isalpha(static_cast<unsigned char>(name_list[name_index]))) {
            ++name_index;
        }
        if (name_index == name_list.size()) {
            return 0;
        }
        while (name_index < name_list.size()
            && (!std::isalpha(static_cast<unsigned char>(name_list[name_index]))
                || name_list[name_index] == ' ')) {
            ++name_index;
        }
    }
}

// isname_nullable() relocated to entity_lifecycle.cpp (entity-seed Task 5);
// declaration unchanged in handler.h.

void affect_modify_room(struct room_data* room, byte, int mod,
    long bitv, char add)
{
    bitv = bitv & (~PERMAFFECT);

    if (add == AFFECT_MODIFY_SET)
        SET_BIT(room->room_flags, bitv);
    else if (add == AFFECT_MODIFY_REMOVE) {
        REMOVE_BIT(room->room_flags, bitv);
        mod = -mod;
    }
}

// affect_modify() relocated to entity_lifecycle.cpp (db-split Task 4b);
// declaration unchanged in handler.h.

/* This updates a character by subtracting everything he is affected by */
/* restoring original abilities, and then affecting all again     ?????      */

void affect_total_room(struct room_data*, int)
{
}

// affect_naked() relocated to entity_lifecycle.cpp (db-split Task 4b).

// apply_gear_affects() (both overloads) relocated to entity_lifecycle.cpp (db-split Task 4b).

// modify_affects() relocated to entity_lifecycle.cpp (db-split Task 4b).

// affect_total() relocated to entity_lifecycle.cpp (db-split Task 4b);
// declaration unchanged in handler.h.

// get_from_affected_type_pool()/put_to_affected_type_pool() relocated to
// entity_lifecycle.cpp, along with their affected_type_pool/
// affected_type_counter backing state (entity-seed Task 5); declarations
// (the forward-declared prototypes above) unchanged.

// affect_to_char() relocated to entity_lifecycle.cpp (db-split Task 4b);
// declaration unchanged in handler.h.

/* Standard mud call to put an affected structure to a room.  The room is added to
   the list of affected rooms if necessary, and its values are updated.  Similar to
   affect_to_char */

void affect_to_room(struct room_data* room, struct affected_type* af)
{
    struct affected_type* affected_alloc;
    struct affected_type* tmpaf;
    universal_list* tmplist;
    char perms_only;

    perms_only = 1;
    for (tmpaf = room->affected; tmpaf; tmpaf = tmpaf->next)
        if (!IS_SET(tmpaf->bitvector, PERMAFFECT))
            perms_only = 0;

    if (perms_only) {
        tmplist = pool_to_list(&affected_list, &affected_list_pool);
        tmplist->ptr.room = room;
        tmplist->number = room->number;
        tmplist->type = TARGET_ROOM;
    }

    affected_alloc = get_from_affected_type_pool();

    *affected_alloc = *af;
    affected_alloc->time_phase = get_current_time_phase();

    affected_alloc->next = room->affected;
    room->affected = affected_alloc;

    affect_modify_room(room, af->location, af->modifier, af->bitvector,
        AFFECT_MODIFY_SET);
    affect_total_room(room);
}

// affect_remove() relocated to entity_lifecycle.cpp (db-split Task 4b);
// declaration unchanged in handler.h.

void affect_remove_notify(struct char_data* ch, struct affected_type* af)
{
    extern const std::string_view spell_wear_off_msg[];

    if (!spell_wear_off_msg[af->type].empty() && !PLR_FLAGGED(ch, PLR_WRITING))
        vsend_to_char(ch, "%s\n", spell_wear_off_msg[af->type].data());

    affect_remove(ch, af);
}

/* Removes an affection from a room */

void affect_remove_room(struct room_data* room, struct affected_type* af)
{
    struct affected_type *hjp, *tmpaf;
    universal_list *tmplist, *tmplist2;
    int tmp, perms_only;

    //   assert(ch->affected);
    if (!room->affected)
        return;

    affect_modify_room(room, af->location, af->modifier, af->bitvector,
        AFFECT_MODIFY_REMOVE);

    /* remove structure *af from linked list */
    if (room->affected == af) {
        /* remove head of list */
        room->affected = af->next;
    } else {
        for (hjp = room->affected, tmp = 0;
             (hjp->next) && (hjp->next != af) && (tmp < MAX_AFFECT);
             hjp = hjp->next, tmp++) {
        }
        if (hjp->next != af) {
            log("SYSERR: FATAL : Could not locate affected_type in room->affected. (handler.c, affect_remove_room)");
            //	 exit(1);
            return;
        }
        hjp->next = af->next; /* skip the af element */
    }

    //   RELEASE(af);
    put_to_affected_type_pool(af);

    perms_only = 1;
    for (tmpaf = room->affected; tmpaf; tmpaf = tmpaf->next)
        if (!IS_SET(tmpaf->bitvector, PERMAFFECT))
            perms_only = 0;

    if (perms_only && affected_list) {
        for (tmplist = affected_list; tmplist; tmplist = tmplist2) {
            tmplist2 = tmplist->next;
            if ((tmplist->type == TARGET_ROOM) && (tmplist->ptr.room == room))
                from_list_to_pool(&affected_list, &affected_list_pool, tmplist);
        }
    }

    affect_total_room(room);
}

/* Returns 1 if a character is found in the affected_list.  0 if not */

int in_affected_list(struct char_data* ch)
{
    universal_list* tmplist;
    int found;

    found = 0;
    for (tmplist = affected_list; tmplist; tmplist = tmplist->next) {
        if (tmplist->ptr.ch == ch)
            found = 1;
    }
    return found;
}

/*
 * Same as affect_from_char below, except also sends a notification
 * to the character that the spell has faded.
 */
void affect_from_char_notify(struct char_data* ch, byte skill)
{
    extern const std::string_view spell_wear_off_msg[];

    if (!spell_wear_off_msg[skill].empty() && !PLR_FLAGGED(ch, PLR_WRITING))
        vsend_to_char(ch, "%s\n", spell_wear_off_msg[skill].data());

    affect_from_char(ch, skill);
}

/* Call affect_remove with every spell of spelltype "skill"
   Standard mud call to remove an affection of known type from a character.  */

void affect_from_char(struct char_data* ch, byte skill)
{
    struct affected_type *hjp, *t;
    int tmp;

    for (hjp = ch->affected, tmp = 0; hjp && (tmp < MAX_AFFECT);
         hjp = t, tmp++) {
        t = hjp->next;
        if (hjp->type == skill)
            affect_remove(ch, hjp);
    }
}

// affected_by_spell() relocated to entity_lifecycle.cpp (db-split Task 4b);
// declaration unchanged in handler.h.

/* Return a pointer to an affection if the room is affected by the spell.
   Otherwise return null. */
affected_type* room_affected_by_spell(const room_data* room, int spell)
{
    for (affected_type* status_effect = room->affected; status_effect; status_effect = status_effect->next) {
        if (status_effect->type == ROOMAFF_SPELL && status_effect->location == spell) {
            return status_effect;
        }
    }

    return NULL;
}

/* Similar to affect_to_char, affect_join is a general mud function to add an
   affection to a character.  If the character already has an affection of that
   type the values of the new affection are added.  Used for poison.  Average
   duration and average modifier are not implemented for some reason.*/

void affect_join(struct char_data* ch, struct affected_type* af,
    char, char)
{
    struct affected_type* hjp;
    char found = FALSE;

    for (hjp = ch->affected; !found && hjp; hjp = hjp->next) {
        if (hjp->type == af->type) {

            if (af->duration < hjp->duration)
                af->duration += hjp->duration;

            //	 if (avg_dur)
            //	    af->duration /= 2;

            if (((af->modifier >= 0) && (af->modifier < hjp->modifier)) || ((af->modifier >= 0) && (af->modifier < hjp->modifier)))
                af->modifier += hjp->modifier;

            //	 if (avg_mod)
            //	    af->modifier /= 2;

            affect_remove(ch, hjp);
            affect_to_char(ch, af);
            found = TRUE;
        }
    }
    if (!found)
        affect_to_char(ch, af);
}

//***************** follow_type procedures ********************************

struct follow_type* get_from_follow_type_pool()
{
    struct follow_type* folnew;

    if (follow_type_pool) {
        folnew = follow_type_pool;
        follow_type_pool = folnew->next;
    } else {
        CREATE(folnew, struct follow_type, 1);
        follow_type_counter++;
    }
    return folnew;
}

void put_to_follow_type_pool(struct follow_type* oldfol)
{
    oldfol->next = follow_type_pool;
    follow_type_pool = oldfol;
}

/* Do NOT call this before having checked if a circle of followers */
/* will arise. CH will follow leader                               */
void add_follower(char_data* follower, char_data* leader, int mode)
{
    if (!leader) {
        printf("add_follower called without leader for %s\n", GET_NAME(follower));
        return;
    }

    if (mode == FOLLOW_MOVE) {
        if (follower->master) {
            stop_follower(follower, FOLLOW_MOVE);
        }
        follower->master = leader;
    }

    follow_type* k = get_from_follow_type_pool();

    k->follower = follower;
    k->fol_number = follower->abs_number;
    if (mode == FOLLOW_MOVE) {
        k->next = leader->followers;
    }

    if (mode == FOLLOW_MOVE) {
        leader->followers = k;
        act("You now follow $N.", FALSE, follower, 0, leader, TO_CHAR);
        act("$n starts following you.", TRUE, follower, 0, leader, TO_VICT);
        act("$n now follows $N.", TRUE, follower, 0, leader, TO_NOTVICT);
    }
}

/* Called when stop following persons, or stopping charm */
/* This will NOT do if a character quits/dies!!          */

void put_to_memory_rec_pool(struct memory_rec* oldaf);

void stop_follower(struct char_data* ch, int mode)
{
    struct follow_type *j, *k;

    if (mode == FOLLOW_MOVE) {
        if (!ch->master)
            return;

        if ((GET_SPEC(ch->master) == PLRSPEC_PETS) && (IS_AFFECTED(ch, AFF_CHARM))) {
            ch->constabilities.str -= 2;
            ch->tmpabilities.str -= 2;
            ch->abilities.str -= 2;
            ch->points.ENE_regen -= 40;
            ch->points.damage -= 2;
        }

        forget(ch, ch->master); // in case we were "hunting" him

        if (IS_AFFECTED(ch, AFF_CHARM)) {
            act("You realize that $N is a jerk!", FALSE, ch, 0, ch->master, TO_CHAR);
            act("$n realizes that $N is a jerk!", FALSE, ch, 0, ch->master, TO_NOTVICT);
            act("$n hates your guts!", FALSE, ch, 0, ch->master, TO_VICT);
            if (affected_by_spell(ch, SKILL_TAME)) {
                affect_from_char(ch, SKILL_TAME);
                GET_MAX_MOVE(ch) -= 50; // move bonus for being tamed
            }
            if (affected_by_spell(ch, SKILL_RECRUIT)) {
                affect_from_char(ch, SKILL_RECRUIT);
            }
            REMOVE_BIT(ch->specials.affected_by, AFF_CHARM);
            ch->damage_details.reset();
        } else {
            act("You stop following $N.", FALSE, ch, 0, ch->master, TO_CHAR);
            if (ch->in_room == ch->master->in_room) {
                act("$n stops following $N.", FALSE, ch, 0, ch->master, TO_NOTVICT);
            }
            act("$n stops following you.", FALSE, ch, 0, ch->master, TO_VICT);
        }

        if (ch->master->followers->follower == ch) { /* Head of follower-list? */
            k = ch->master->followers;
            ch->master->followers = k->next;
            put_to_follow_type_pool(k);
        } else { /* locate follower who is not head of list */
            for (k = ch->master->followers; k->next->follower != ch; k = k->next)
                ;

            j = k->next;
            k->next = j->next;
            put_to_follow_type_pool(j);
        }

        ch->master = 0;
        if (affected_by_spell(ch, SKILL_TAME))
            affect_from_char(ch, SKILL_TAME);

        REMOVE_BIT(ch->specials.affected_by, AFF_CHARM);

        // Recursive call to rid ourselves of our group if we were a pet.
        if (IS_NPC(ch) && MOB_FLAGGED(ch, MOB_PET)) {
            REMOVE_BIT(MOB_FLAGS(ch), MOB_PET);
            stop_follower(ch, FOLLOW_GROUP);
        }
    }

    else if (mode == FOLLOW_REFOL) {
        if (!ch->master)
            return;

        act("You stop following $N.", FALSE, ch, 0, ch->master, TO_CHAR);
        if (ch->in_room == ch->master->in_room)
            act("$n stops following $N.", FALSE, ch, 0, ch->master, TO_NOTVICT);
        act("$n stops following you.", FALSE, ch, 0, ch->master, TO_VICT);

        if (ch->master->followers->follower == ch) { /* Head of follower-list? */
            k = ch->master->followers;
            ch->master->followers = k->next;
            put_to_follow_type_pool(k);
        } else { /* locate follower who is not head of list */
            for (k = ch->master->followers; k->next->follower != ch; k = k->next)
                ;

            j = k->next;
            k->next = j->next;
            put_to_follow_type_pool(j);
        }

        ch->master = 0;
    }
}

/* Check if making CH follow VICTIM will create an illegal */
/* Follow "Loop/circle"                                    */
char circle_follow(struct char_data* ch, struct char_data* victim, int)
{
    for (char_data* character = victim; character; character = character->master) {
        if (character == ch) {
            return (TRUE);
        }
    }

    return (FALSE);
}

/* Called when a character that follows/is followed dies */
void die_follower(char_data* character)
{
    struct follow_type *j, *k;

    if (character->master) {
        stop_follower(character, FOLLOW_MOVE);
    }

    for (k = character->followers; k; k = j) {
        j = k->next;
        stop_follower(k->follower, FOLLOW_MOVE);
    }

    group_data* group = character->group;
    if (group) {
        if (group->is_leader(character)) {
            // Do disband.
            size_t index = group->size() - 1;
            while (index >= 1) {
                remove_character_from_group(group->at(index), character);
                --index;
            }
        } else {
            // Remove the character from the group.
            remove_character_from_group(character, group->get_leader());
        }
    }
}

//**************************************************************************

/* move a player out of a room */
void char_from_room(struct char_data* ch)
{
    struct char_data* i;
    int tmp;
    if (ch->in_room == NOWHERE) {
        //      log("SYSERR: NOWHERE extracting char from room (handler.c, char_from_room)");
        //      exit(1);
        return; // he's already nowehre
    }

    for (tmp = 0; tmp < MAX_WEAR; tmp++)
        if (ch->equipment[tmp])
            if (ch->equipment[tmp]->obj_flags.type_flag == ITEM_LIGHT)
                if (ch->equipment[tmp]->obj_flags.value[2] && (ch->equipment[tmp]->obj_flags.value[3])) /* Light is ON */
                    world[ch->in_room].light--;

    if (ch == world[ch->in_room].people) /* head of list */
        world[ch->in_room].people = ch->next_in_room;

    else /* locate the previous element */ {
        for (i = world[ch->in_room].people;
             i && (i->next_in_room != ch); i = i->next_in_room)
            ;

        if (!i)
            return;

        i->next_in_room = ch->next_in_room;
    }

    tmp = char_power(GET_LEVEL(ch));

    if (!IS_NPC(ch)) {
        //     zone_table[world[ch->in_room].zone].nature_power -= tmp;
        if (RACE_GOOD(ch))
            zone_table[world[ch->in_room].zone].white_power -= tmp;
        else if (RACE_EVIL(ch))
            zone_table[world[ch->in_room].zone].dark_power -= tmp;
    }

    ch->in_room = NOWHERE;
    ch->next_in_room = 0;
    for (tmp = 0; ch->specials.fighting && (tmp < 100); tmp++) {
        if (ch->specials.fighting->specials.fighting == ch)
            stop_fighting(ch->specials.fighting);
        stop_fighting(ch);
    }
    if (tmp == 100) {
        strcpy(buf, std::format("Char_from_room: could not stop fighting for {}.\n",
            GET_NAME(ch)).c_str());
        mudlog(buf, NRM, LEVEL_GOD, TRUE);
    }
}

/* place a character in a room */
void char_to_room(struct char_data* ch, int room)
{
    struct char_data* tmpch;
    int tmp;

    /* append ch to the room's list */
    if (!world[room].people)
        world[room].people = ch;
    else {
        for (tmpch = world[room].people; tmpch->next_in_room; tmpch = tmpch->next_in_room)
            ;
        tmpch->next_in_room = ch;
    }
    ch->next_in_room = 0;
    ch->in_room = room;

    /* do they have a light? */
    for (tmp = 0; tmp < MAX_WEAR; tmp++)
        if (ch->equipment[tmp])
            if (ch->equipment[tmp]->obj_flags.type_flag == ITEM_LIGHT)
                if (ch->equipment[tmp]->obj_flags.value[2] && (ch->equipment[tmp]->obj_flags.value[3])) /* Light is ON */
                    world[room].light++;

    tmp = char_power(GET_LEVEL(ch));

    /* increase the goodness/evilness of this room's zone */
    if (!IS_NPC(ch)) {
        if (RACE_GOOD(ch))
            zone_table[world[room].zone].white_power += tmp;
        else if (RACE_EVIL(ch))
            zone_table[world[room].zone].dark_power += tmp;
    }
}

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

/* take an object from a char */
void obj_from_char(struct obj_data* object)
{
    struct obj_data* tmp;
    int i;

    if (object->carried_by->carrying == object) { /* head of list */
        object->carried_by->carrying = object->next_content;
        IS_CARRYING_N(object->carried_by)
        --;
    } else {
        for (tmp = object->carried_by->carrying;
             tmp && (tmp->next_content != object);
             tmp = tmp->next_content)
            ; /* locate previous */
        if (tmp) {
            tmp->next_content = object->next_content;
            IS_CARRYING_N(object->carried_by)
            --;
        } else {
            for (i = 0; i < MAX_WEAR; i++)
                if (object->carried_by->equipment[i] == object)
                    break;
            if (i < MAX_WEAR)
                unequip_char(object->carried_by, i);
        }
    }

    /* set flag for crash-save system */
    if (!IS_NPC(object->carried_by))
        SET_BIT(PLR_FLAGS(object->carried_by), PLR_CRASH);

    if (IS_RIDING(object->carried_by))
        IS_CARRYING_W(object->carried_by->mount_data.mount) -= GET_OBJ_WEIGHT(object);

    IS_CARRYING_W(object->carried_by) -= GET_OBJ_WEIGHT(object);
    object->carried_by = 0;
    object->next_content = 0;
    object->in_room = NOWHERE;

    if (IS_OBJ_STAT(object, ITEM_WILLPOWER))
        REMOVE_BIT(object->obj_flags.extra_flags, ITEM_WILLPOWER);
    if (object->obj_flags.prog_number == 1)
        object->obj_flags.prog_number = 0;
}

void equip_char(char_data* character, obj_data* item, int item_slot)
{
    int was_poisoned = IS_AFFECTED(character, AFF_POISON);

    assert(item_slot >= 0 && item_slot < MAX_WEAR);

    if (character->equipment[item_slot]) {
        log(std::format("SYSERR: Char is already equipped: {}, {}", GET_NAME(character),
            item->short_description)
                );
        return;
    }

    if (item->in_room != NOWHERE) {
        log("SYSERR: EQUIP: Obj is in_room when equip.");
        return;
    }

    if ((IS_OBJ_STAT(item, ITEM_ANTI_EVIL) && IS_EVIL(character)) || (IS_OBJ_STAT(item, ITEM_ANTI_GOOD) && IS_GOOD(character)) || (IS_OBJ_STAT(item, ITEM_ANTI_NEUTRAL) && IS_NEUTRAL(character))) {
        if (character->in_room != NOWHERE) {

            act("You are zapped by $p and instantly drop it.", FALSE, character, item, 0, TO_CHAR);
            act("$n is zapped by $p and instantly drops it.", FALSE, character, item, 0, TO_ROOM);
            obj_to_char(item, character); /* changed to drop in inventory instead of ground */
            return;
        } else
            log("SYSERR: ch->in_room = NOWHERE when equipping char.");
    }

    if ((IS_OBJ_STAT(item, ITEM_HARADRIM) && GET_RACE(character) != RACE_HARADRIM) || (IS_OBJ_STAT(item, ITEM_HUMAN) && GET_RACE(character) != RACE_HUMAN) || (IS_OBJ_STAT(item, ITEM_DWARF) && GET_RACE(character) != RACE_DWARF) || (IS_OBJ_STAT(item, ITEM_WOODELF) && GET_RACE(character) != RACE_WOOD) || (IS_OBJ_STAT(item, ITEM_HOBBIT) && GET_RACE(character) != RACE_HOBBIT) || (IS_OBJ_STAT(item, ITEM_BEORNING) && GET_RACE(character) != RACE_BEORNING) || (IS_OBJ_STAT(item, ITEM_URUK) && GET_RACE(character) != RACE_URUK) || (IS_OBJ_STAT(item, ITEM_ORC) && GET_RACE(character) != RACE_ORC) || (IS_OBJ_STAT(item, ITEM_MAGUS) && GET_RACE(character) != RACE_MAGUS) || (IS_OBJ_STAT(item, ITEM_OLOGHAI) && GET_RACE(character) != RACE_OLOGHAI)) {
        if (character->in_room != NOWHERE) {

            act("You are zapped by $p and instantly drop it.", FALSE, character, item, 0, TO_CHAR);
            act("$n is zapped by $p and instantly drops it.", FALSE, character, item, 0, TO_ROOM);
            obj_to_char(item, character); /* changed to drop in inventory instead of ground */
            return;
        } else
            log("SYSERR: ch->in_room = NOWHERE when equipping char.");
    }

    character->equipment[item_slot] = item;
    item->carried_by = character;
    item->obj_flags.timer = -1;

    // Encumb and weight update:
    character->points.encumb += item->obj_flags.value[2] * encumb_table[item_slot];
    character->specials2.leg_encumb += item->obj_flags.value[2] * leg_encumb_table[item_slot];
    if (encumb_table[item_slot])
        GET_ENCUMB_WEIGHT(character) += GET_OBJ_WEIGHT(item) * encumb_table[item_slot];
    else
        GET_ENCUMB_WEIGHT(character) += GET_OBJ_WEIGHT(item) / 2;
    GET_WORN_WEIGHT(character) += GET_OBJ_WEIGHT(item);

    IS_CARRYING_W(character) += GET_OBJ_WEIGHT(item);

    if ((item_slot == HOLD) && !CAN_WEAR(item, ITEM_HOLD))
        return;

    if (GET_ITEM_TYPE(item) == ITEM_ARMOR)
        SET_DODGE(character) += item->obj_flags.value[3];

    else if (GET_ITEM_TYPE(item) == ITEM_WEAPON) {
        SET_OB(character) += item->obj_flags.value[0];
        SET_PARRY(character) += item->obj_flags.value[1];

        if (GET_OBJ_WEIGHT(item) > (GET_BAL_STR(character) * 50) && !IS_TWOHANDED(character))
            send_to_char("This weapon seems too heavy for one hand.\n\r", character);
        else if (GET_OBJ_WEIGHT(item) > (GET_BAL_STR(character) * 100))
            send_to_char("This weapon seems too heavy for you!\n\r", character);

    } else if (GET_ITEM_TYPE(item) == ITEM_SHIELD) {
        SET_DODGE(character) += item->obj_flags.value[0];
        SET_PARRY(character) += item->obj_flags.value[1];
    } else if (GET_ITEM_TYPE(item) == ITEM_LIGHT) {
        if ((character->in_room != NOWHERE) && (item->obj_flags.value[2] != 0)) {
            if (item->obj_flags.value[3] == 0)
                item->obj_flags.value[3] = 1;
            world[character->in_room].light++;
        }
    }

    for (int j = 0; j < MAX_OBJ_AFFECT; j++)
        affect_modify(character, item->affected[j].location,
            item->affected[j].modifier,
            item->obj_flags.bitvector, AFFECT_MODIFY_SET, 0);

    affect_total(character);

    // Special case for poisoned objects.  The wearer should get poison damage
    // when wearing/removing something poisoned.
    if (was_poisoned == 0 && IS_AFFECTED(character, AFF_POISON)) {
        extern void raw_kill(struct char_data * character, char_data * killer, int attacktype);

        damage(character, character, 5, SPELL_POISON, 0);

        if (GET_HIT(character) <= 0) {
            act("$n suddenly collapses on the ground.",
                TRUE, character, 0, 0, TO_ROOM);
            send_to_char("Your body failed to the magic.\n\r", character);
            raw_kill(character, NULL, 0);
        }
    }
}

struct obj_data* unequip_char(struct char_data* ch, int pos)
{
    int j;
    int was_poisoned = 0;
    struct obj_data* obj;

    was_poisoned = IS_AFFECTED(ch, AFF_POISON);

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
            world[ch->in_room].light--;
        }
    }

    for (j = 0; j < MAX_OBJ_AFFECT; j++)
        affect_modify(ch, obj->affected[j].location,
            obj->affected[j].modifier,
            obj->obj_flags.bitvector, AFFECT_MODIFY_REMOVE, 0);

    affect_total(ch);

    // Special case for poisoned objects.  The wearer should get poison damage
    // when wearing/removing something poisoned.
    if (was_poisoned != 0 && !IS_AFFECTED(ch, AFF_POISON)) {
        extern void raw_kill(struct char_data * ch, char_data * killer, int attacktype);

        damage(ch, ch, 5, SPELL_POISON, 0);

        if (GET_HIT(ch) <= 0) {
            act("$n suddenly collapses on the ground.",
                TRUE, ch, 0, 0, TO_ROOM);
            send_to_char("Your body failed to the magic.\n\r", ch);
            raw_kill(ch, NULL, 0);
        }
    }

    return (obj);
}

// get_number() relocated to rots_util.cpp (rots_platform, placement-seam
// Task 1's sequencing fix -- the census classifies it MOVE-OTHER(platform),
// originally scheduled for Task 4, but get_char_room's Task 1 move needed
// it moved first; see task-1-report.md). Declaration moved to utils.h (this
// file had none of its own -- every prior caller outside this file forward-
// declared it locally; those local externs are untouched and still resolve
// to the same definition).

NumberedName parse_numbered_name(std::string_view input)
{
    input = rots::text::truncate_at_null(input);
    const std::size_t dot_position = input.find('.');
    if (dot_position == std::string_view::npos) {
        return { 1, input };
    }

    const std::string_view digits = input.substr(0, dot_position);
    const std::string_view remainder = input.substr(dot_position + 1);
    if (digits.empty() || !std::isdigit(static_cast<unsigned char>(digits.front()))) {
        // Empty (".") or non-digit-led prefix (including a '-' sign, which
        // std::from_chars would otherwise accept for int): legacy get_number's
        // isdigit loop produced 0 (no match) for every such input.
        return { 0, remainder };
    }
    int parsed_number = 0;
    const auto [parse_end, parse_error]
        = std::from_chars(digits.data(), digits.data() + digits.size(), parsed_number);
    if (parse_error != std::errc() || parse_end != digits.data() + digits.size()) {
        // Interior non-digit or overflowing prefix: legacy atoi produced 0 (no
        // match) for the former; overflow is tightened to the same result.
        return { 0, remainder };
    }
    return { parsed_number, remainder };
}

/* Search a given list for an object, and return a pointer to that object */
struct obj_data* get_obj_in_list(char* name, struct obj_data* list)
{
    struct obj_data* i;
    int j, number;
    char tmpname[MAX_INPUT_LENGTH];
    char* tmp;

    strcpy(tmpname, name);
    tmp = tmpname;
    if (!(number = get_number(&tmp)))
        return (0);

    for (i = list, j = 1; i && (j <= number); i = i->next_content)
        if (isname_nullable(tmp, i->name, 0)) {
            if (j == number)
                return (i);
            j++;
        }

    return (0);
}

/* Search a given list for an object number, and return a ptr to that obj */
struct obj_data* get_obj_in_list_num(int num, struct obj_data* list)
{
    struct obj_data* i;

    for (i = list; i; i = i->next_content)
        if (i->item_number == num)
            return (i);

    return (0);
}

/* Search a given list for a specified vnum, and return a ptr to that obj */
struct obj_data* get_obj_in_list_vnum(int vnum, struct obj_data* list)
{
    struct obj_data* i;

    if (vnum == 0)
        return 0;

    for (i = list; i; i = i->next_content)
        if (((i->item_number >= 0) ? obj_index[i->item_number].virt : 0) == vnum)
            return (i);

    return (0);
}

/* Search a given list for an object number - including containers */
struct obj_data* get_obj_in_list_num_containers(int num, struct obj_data* list)
{

    struct obj_data* i = 0;

    if (!list)
        return 0;

    if (list->contains)
        i = get_obj_in_list_num_containers(num, list->contains);
    if (!i)
        return get_obj_in_list_num(num, list);
    else
        return i;
}

int count_obj_in_list(int num, struct obj_data* list)
{
    struct obj_data* i;
    int n;

    for (n = 0, i = list; i; i = i->next_content)
        if (!num || (i->item_number == num))
            n++;

    return n;
}

/*search the entire world for an object, and return a pointer  */
struct obj_data* get_obj(char* name)
{
    struct obj_data* i;
    int j, number;
    char tmpname[MAX_INPUT_LENGTH];
    char* tmp;

    strcpy(tmpname, name);
    tmp = tmpname;
    if (!(number = get_number(&tmp)))
        return (0);

    for (i = object_list, j = 1; i && (j <= number); i = i->next)
        if (isname_nullable(tmp, i->name)) {
            if (j == number)
                return (i);
            j++;
        }

    return (0);
}

/*search the entire world for an object number, and return a pointer  */
struct obj_data* get_obj_num(int nr)
{
    struct obj_data* i;

    for (i = object_list; i; i = i->next)
        if (i->item_number == nr)
            return (i);

    return (0);
}

// get_char_room() relocated to placement.cpp (placement-seam Task 1): its
// world[room] access becomes room_by_id(room) (rots_world resolver seam);
// see that file for the moved body and task-1-report.md for the exact
// substitution. Declaration stays in handler.h.

/* search all over the world for a char, and return a pointer if found */
struct char_data* get_char(std::string_view name)
{
    const auto [requested_match_number, query] = parse_numbered_name(name);
    if (requested_match_number == 0) {
        return (0);
    }

    int match_index = 1;
    for (char_data* candidate = character_list;
         candidate != nullptr && match_index <= requested_match_number;
         candidate = candidate->next) {
        if (candidate->player.name != nullptr && isname(query, candidate->player.name)) {
            if (match_index == requested_match_number) {
                return candidate;
            }
            ++match_index;
        }
    }

    return (0);
}

/* search all over the world for a char num, and return a pointer if found */
struct char_data* get_char_num(int nr)
{
    struct char_data* i;

    for (i = character_list; i; i = i->next)
        if (i->nr == nr)
            return (i);

    return (0);
}

/* put an object in a room */
void obj_to_room(struct obj_data* object, int room)
{
    int tmp;
    obj_data* tmpobj;

    if (!object)
        return;

    for (tmpobj = world[room].contents; tmpobj; tmpobj = tmpobj->next_content)
        if (tmpobj == object) {
            strcpy(buf, std::format("obj_to_room: double call for room {}, object {}\n", world[room].number, object->short_description).c_str());
            mudlog(buf, NRM, LEVEL_IMPL, TRUE);
            return;
        }
    object->next_content = world[room].contents;
    world[room].contents = object;

    if (GET_ITEM_TYPE(object) == ITEM_LIGHT) {
        if (object->obj_flags.value[2] && object->obj_flags.value[3]) {
            world[room].light++;
        }
    }
    for (tmp = 0, tmpobj = world[room].contents; tmpobj && (tmp < 1000);
         tmpobj = tmpobj->next_content, tmp++)
        ;
    if (tmp >= 1000) {
        mudlog("obj_to_room: infinite loop in room contents.",
            NRM, LEVEL_GOD, TRUE);
        world[room].contents = object;
        object->next_content = 0;
    }
    object->in_room = room;
    object->carried_by = 0;
    //   printf("obj_to_room %d, %p, descr:%s\n",world[room].number,object,object->description);
}

/* Take an object from a room */
void obj_from_room(struct obj_data* object)
{
    struct obj_data* i;

    /* remove object from room */

    if (!object)
        return;

    if (object == world[object->in_room].contents) /* head of list */
        world[object->in_room].contents = object->next_content;

    else /* locate previous element in list */ {
        for (i = world[object->in_room].contents; i && (i->next_content != object); i = i->next_content)
            ;

        i->next_content = object->next_content;
    }

    if (GET_ITEM_TYPE(object) == ITEM_LIGHT) {
        if (object->obj_flags.value[2] && object->obj_flags.value[3]) {
            world[object->in_room].light--;
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
            for (tmp = obj_from->contains; tmp && (tmp->next_content != item); tmp = tmp->next_content)
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

/* Extract an object from the world */
void extract_obj(struct obj_data* obj)
{
    struct obj_data *temp1, *temp2;

    if (obj->in_room != NOWHERE)
        obj_from_room(obj);
    else if (obj->carried_by)
        obj_from_char(obj);
    else if (obj->in_obj) {
        temp1 = obj->in_obj;
        if (temp1->contains == obj) /* head of list */
            temp1->contains = obj->next_content;
        else {
            for (temp2 = temp1->contains;
                 temp2 && (temp2->next_content != obj);
                 temp2 = temp2->next_content)
                ;

            if (temp2) {
                temp2->next_content = obj->next_content;
            }
        }
    }

    for (; obj->contains; extract_obj(obj->contains))
        ;
    /* leaves nothing ! */

    if (object_list == obj) /* head of list */
        object_list = obj->next;
    else {
        for (temp1 = object_list;
             temp1 && (temp1->next != obj);
             temp1 = temp1->next)
            ;

        if (temp1)
            temp1->next = obj->next;
    }

    if (obj->item_number >= 0)
        (obj_index[obj->item_number].number)--;
    // printf("extracting object %s in room %d\n",obj->name, obj->in_room);
    free_obj(obj);
}

void update_object(struct obj_data* obj, int use)
{

    if (obj->obj_flags.timer > 0)
        obj->obj_flags.timer -= use;
    if (obj->contains)
        update_object(obj->contains, use);
    if (obj->next_content)
        update_object(obj->next_content, use);
}

void update_char_objects(struct char_data* ch)
{

    int i;

    //    for (tmp = 0; tmp < MAX_WEAR; tmp++)
    //    if (ch->equipment[tmp])
    //      if (ch->equipment[tmp]->obj_flags.type_flag == ITEM_LIGHT){
    //        if (ch->equipment[tmp]->obj_flags.value[2] > 0){
    // 	    (ch->equipment[tmp]->obj_flags.value[2])--;
    // 	 if(ch->equipment[tmp]->obj_flags.value[2] == 0){
    // 	   send_to_char("Your light went out.\n\r",ch);
    // 	   recount_light_room(ch->in_room);
    // 	 }
    // 	 else if((ch->equipment[tmp]->obj_flags.value[2] < 3) &&
    // 		 (ch->equipment[tmp]->obj_flags.value[2] > 0))
    // 	   send_to_char("Your light is fading.\n\r",ch);
    //        }
    //      }
    for (i = 0; i < MAX_WEAR; i++)
        if (ch->equipment[i])
            update_object(ch->equipment[i], 2);

    if (ch->carrying)
        update_object(ch->carrying, 1);
}

/* Extract a ch completely from the world, and leave his stuff behind */

ACMD(do_look);
void stop_fighting_him(struct char_data* ch);

void extract_char(struct char_data* ch)
{
    return extract_char(ch, -1);
}

void extract_char(struct char_data* ch, int new_room)
{
    struct obj_data* i;
    struct char_data *k, *k2, *next_char;
    struct descriptor_data* t_desc;
    int l, was_in;

    extern struct char_data* combat_list;
    extern struct char_data* waiting_list;

    if (!IS_NPC(ch) && !ch->desc) {
        for (t_desc = descriptor_list; t_desc; t_desc = t_desc->next)
            if (t_desc->original == ch)
                do_return(t_desc->character, mutable_arg(""), 0, 0, 0);
    }

    if (ch->followers || ch->master || ch->group)
        die_follower(ch);

    GET_ENERGY(ch) = 1201;
    GET_MENTAL_DELAY(ch) = 0;
    stop_fighting_him(ch);
    stop_fighting(ch);
    stop_riding(ch);
    abort_delay(ch);

    if (ch->desc) {
        /* Forget snooping */
        if (ch->desc->snoop.snooping)
            ch->desc->snoop.snooping->desc->snoop.snoop_by = 0;

        if (ch->desc->snoop.snoop_by) {
            send_to_char("Your victim is no longer among us.\n\r",
                ch->desc->snoop.snoop_by);
            ch->desc->snoop.snoop_by->desc->snoop.snooping = 0;
        }
        ch->desc->snoop.snooping = ch->desc->snoop.snoop_by = 0;
    }

    if (ch->carrying) {
        /* transfer ch's objects to room */
        if (ch->in_room != NOWHERE) {
            if (world[ch->in_room].contents) /* room nonempty */ {
                /* locate tail of room-contents */
                for (i = world[ch->in_room].contents; i->next_content;
                     i = i->next_content)
                    ;

                /* append ch's stuff to room-contents */
                i->next_content = ch->carrying;
            } else
                world[ch->in_room].contents = ch->carrying;

            /* connect the stuff to the room */
            for (i = ch->carrying; i; i = i->next_content) {
                i->carried_by = 0;
                i->in_room = ch->in_room;
            }
            ch->carrying = 0;
        } else {
            struct obj_data* j;
            for (i = ch->carrying; i; i = j) {
                j = i->next_content;
                extract_obj(i);
            }
        }
    }

    //    while (ch->affected)
    //       affect_remove(ch, ch->affected);

    for (k = combat_list; k; k = next_char) {
        next_char = k->next_fighting;
        if (k->specials.fighting == ch)
            stop_fighting(k);
    }
    for (k2 = 0, k = waiting_list; k; k = k->delay.next) {
        if (k == ch)
            break;
        k2 = k;
    }
    if (!k2)
        waiting_list = ch->delay.next;
    else
        k2->delay.next = ch->delay.next;
    /* Must remove from room before removing the equipment! */
    if (ch->in_room != NOWHERE) {
        was_in = ch->in_room;
        ch->specials2.load_room = world[ch->in_room].number;
        char_from_room(ch);

        /* clear equipment_list */
        for (l = 0; l < MAX_WEAR; l++)
            if (ch->equipment[l])
                obj_to_room(unequip_char(ch, l), was_in);

        recount_light_room(was_in);
    } else {
        was_in = NOWHERE;
        for (l = 0; l < MAX_WEAR; l++)
            if (ch->equipment[l])
                extract_obj(unequip_char(ch, l));
    }
    for (l = 0; l < MAX_WEAR; l++)
        ch->equipment[l] = 0;

    if (IS_NPC(ch) || !(ch->desc) || (!ch->desc->descriptor) || (new_room < 0)) {
        /* pull the char from the list */

        if (ch == character_list)
            character_list = ch->next;
        else {
            for (k = character_list; (k) && (k->next != ch); k = k->next)
                ;
            if (k)
                k->next = ch->next;
            else {
                log(std::format("SYSERR: Trying to remove {} from character_list. (handler.c, extract_char)", GET_NAME(ch))
                        );
                abort();
            }
        }
    }

    if (ch->desc) {
        if (ch->desc->original) {
            do_return(ch, mutable_arg(""), 0, 0, 0);
        } else
            save_char(ch, (new_room < 0) ? ((was_in == NOWHERE) ? -1 : world[was_in].number) : new_room, 0);
    }

    if (IS_NPC(ch)) {
        if (ch->nr > -1) { /* if mobile */
            mob_index[ch->nr].number--;
            if (mob_index[ch->nr].number < 0)
                mob_index[ch->nr].number = 0;
        }
        if (GET_LOADLINE(ch)) {
            zone_table[GET_LOADZONE(ch)].cmd[GET_LOADLINE(ch) - 1].existing--;
            if (zone_table[GET_LOADZONE(ch)].cmd[GET_LOADLINE(ch) - 1].existing < 0)
                zone_table[GET_LOADZONE(ch)].cmd[GET_LOADLINE(ch) - 1].existing = 0;
        }

        clear_memory(ch); /* Only NPC's can have memory */
        remove_char_exists(ch->abs_number);
        free_char(ch);
    } else if (ch->desc) {
        if (!ch->desc->descriptor) {
            close_socket(ch->desc);
        } else if (new_room >= 0) {
            while (ch->affected)
                affect_remove(ch, ch->affected);
            char_to_room(ch, new_room);
            ch->specials.was_in_room = NOWHERE;
            send_to_char("Your spirit found a new body to wear.\n\r", ch);
            SET_POS(ch) = POSITION_RESTING;
            utils::set_spirits(ch, utils::get_spirits(ch) / 2);
            do_look(ch, mutable_arg(""), 0, 0, 0);
        } else {
            ch->desc->connected = CON_SLCT;
            show_character_menu(ch->desc);
        }
    } else {
        while (ch->affected)
            affect_remove(ch, ch->affected); // Extracted characters' affections were not being removed
        remove_char_exists(ch->abs_number);
        free_char(ch);
    }
}

/* ***********************************************************************
   Here follows high-level versions of some earlier routines, ie functions
   which incorporate the actual player-data.
   *********************************************************************** */

int keyword_matches_char(struct char_data* ch, struct char_data* vict, char* keyword)
{
    int check;

    if (other_side(ch, vict)) {
        check = isname_nullable(keyword, pc_race_keywords[GET_RACE(vict)].data());
    } else
        check = isname_nullable(keyword, vict->player.name);

    return check;
}

struct char_data* get_char_room_vis(struct char_data* ch, char* name, int dark_ok)
{
    struct char_data* i;
    int j, number, check;
    char tmpname[MAX_INPUT_LENGTH];
    char* tmp;

    strcpy(tmpname, name);
    tmp = tmpname;
    if (!(number = get_number(&tmp)))
        return (0);

    j = 1;
    for (i = world[ch->in_room].people; i && (j <= number); i = i->next_in_room) {

        check = keyword_matches_char(ch, i, tmp);
        if (check)
            if (CAN_SEE(ch, i, dark_ok)) {
                if (j == number)
                    return (i);
                j++;
            }
    }
    return (0);
}

struct char_data* get_player_vis(struct char_data* ch, char* name)
{
    struct char_data* i;

    for (i = character_list; i; i = i->next)
        if (!IS_NPC(i) && !str_cmp_nullable(i->player.name, name) && CAN_SEE(ch, i))
            return i;

    return 0;
}

struct char_data* get_char_vis(struct char_data* ch, char* name, int dark_ok)
{
    struct char_data* i;
    int j, number, check;
    char tmpname[MAX_INPUT_LENGTH];
    char* tmp;

    /* check location */
    if ((i = get_char_room_vis(ch, name)))
        return (i);

    strcpy(tmpname, name);
    tmp = tmpname;
    if (!(number = get_number(&tmp)))
        return (0);

    for (i = character_list, j = 1; i && (j <= number); i = i->next) {
        if (other_side(ch, i))
            check = isname_nullable(tmp, pc_race_keywords[i->player.race].data());
        else
            check = isname_nullable(tmp, i->player.name);

        if (check)
            if (CAN_SEE(ch, i, dark_ok)) {
                if (j == number)
                    return (i);
                j++;
            }
    }

    return (0);
}

struct obj_data* get_obj_in_list_vis(struct char_data* ch, std::string_view name,
    struct obj_data* list, int num)
{
    if (num < 9999) {
        obj_data* indexed_object = list;
        for (int list_index = 1; indexed_object != nullptr && list_index < num; ++list_index) {
            indexed_object = indexed_object->next_content;
        }
        return indexed_object;
    }

    const auto [requested_match_number, query] = parse_numbered_name(name);
    if (requested_match_number == 0) {
        return (0);
    }

    int match_index = 1;
    for (obj_data* candidate_object = list;
         candidate_object != nullptr && match_index <= requested_match_number;
         candidate_object = candidate_object->next_content) {
        if (candidate_object->name != nullptr && isname(query, candidate_object->name, 0)
            && CAN_SEE_OBJ(ch, candidate_object)) {
            if (match_index == requested_match_number) {
                return candidate_object;
            }
            ++match_index;
        }
    }
    return (0);
}

/*search the entire world for an object, and return a pointer  */
struct obj_data* get_obj_vis(struct char_data* ch, char* name)
{
    struct obj_data* i;
    int j, number;
    char tmpname[MAX_INPUT_LENGTH];
    char* tmp;

    /* scan items carried */
    if ((i = get_obj_in_list_vis(ch, name, ch->carrying, 9999)))
        return (i);

    /* scan room */
    if ((i = get_obj_in_list_vis(ch, name, world[ch->in_room].contents, 9999)))
        return (i);

    strcpy(tmpname, name);
    tmp = tmpname;
    if (!(number = get_number(&tmp)))
        return (0);

    /* ok.. no luck yet. scan the entire obj list   */
    for (i = object_list, j = 1; i && (j <= number); i = i->next)
        if (isname_nullable(tmp, i->name, 0))
            if (CAN_SEE_OBJ(ch, i)) {
                if (j == number)
                    return (i);
                j++;
            }
    return (0);
}

struct obj_data* get_object_in_equip_vis(struct char_data* ch,
    char* arg, struct obj_data* equipment[], int* j)
{

    for ((*j) = 0; (*j) < MAX_WEAR; (*j)++)
        if (equipment[(*j)])
            if (CAN_SEE_OBJ(ch, equipment[(*j)]))
                if (isname_nullable(arg, equipment[(*j)]->name))
                    return (equipment[(*j)]);

    return (0);
}

struct obj_data* create_money(int amount)
{
    struct obj_data* obj;
    struct extra_descr_data* new_descr;

    if (amount <= 0) {
        log("SYSERR: Try to create negative or 0 money.");
        exit(1);
    }

    CREATE(obj, struct obj_data, 1);
    CREATE(new_descr, struct extra_descr_data, 1);
    clear_object(obj);
    if (amount == 1) {
        obj->name = str_dup("coin money copper");
        obj->short_description = str_dup("a coin");
        obj->description = str_dup("One miserable copper coin is lying here.");
        new_descr->keyword = str_dup("coin gold");
        new_descr->description = str_dup("It's just one miserable little copper coin.");
    } else {
        obj->name = str_dup("coins money gold");
        if (amount <= 100) {
            obj->short_description = str_dup("a small pile of coins");
            obj->description = str_dup("A small pile of coins is lying here.");
        } else if (amount <= 1000) {
            obj->short_description = str_dup("a pile of coins");
            obj->description = str_dup("A pile of coins is lying here.");
        } else if (amount <= 25000) {
            obj->short_description = str_dup("a large heap of coins");
            obj->description = str_dup("A large heap of coins is lying here.");
        } else if (amount <= 500000) {
            obj->short_description = str_dup("a huge mound of coins");
            obj->description = str_dup("A huge mound of coins is lying here.");
        } else {
            obj->short_description = str_dup("an enormous mountain of coins");
            obj->description = str_dup("An enormous mountain of money is lying here.");
        }

        new_descr->keyword = str_dup("coins money gold");
        if (amount < COPP_IN_SILV) {
            new_descr->description = str_dup(std::format("There are {} copper coins.", amount).c_str());
        } else if (amount < COPP_IN_GOLD) {
            new_descr->description = str_dup(
                std::format("There are about {} silver coins.", (amount / COPP_IN_SILV)).c_str());
        } else if (amount < 10 * COPP_IN_GOLD) {
            new_descr->description = str_dup(
                std::format("It looks to be about {} gold coins.", (amount / COPP_IN_GOLD)).c_str());
        } else if (amount < 100 * COPP_IN_GOLD) {
            new_descr->description = str_dup(std::format("You guess there are, maybe, {} gold coins.",
                10 * ((amount / 10 / COPP_IN_GOLD)))
                                                  .c_str());
        } else
            new_descr->description = str_dup("There is a lot of gold.");
    }

    new_descr->next = 0;
    obj->ex_description = new_descr;

    obj->obj_flags.type_flag = ITEM_MONEY;
    obj->obj_flags.wear_flags = ITEM_TAKE;
    obj->obj_flags.value[0] = amount;
    obj->obj_flags.cost = amount;
    obj->item_number = -1;

    obj->next = object_list;
    object_list = obj;

    return (obj);
}

/* Generic Find, designed to find any object/character                    */
/* Calling :                                                              */
/*  *arg     is the sting containing the string to be searched for.       */
/*           This string doesn't have to be a single word, the routine    */
/*           extracts the next word itself.                               */
/*  bitv..   All those bits that you want to "search through".            */
/*           Bit found will be result of the function                     */
/*  *ch      This is the person that is trying to "find"                  */
/*  **tar_ch Will be NULL if no character was found, otherwise points     */
/* **tar_obj Will be NULL if no object was found, otherwise points        */
/*                                                                        */
/* The routine returns a pointer to the next word in *arg (just like the  */
/* one_argument routine).                                                 */

int generic_find(char* arg, int bitvector, struct char_data* ch,
    struct char_data** tar_ch, struct obj_data** tar_obj)
{
    static const std::string_view ignore[] = {
        "the",
        "in",
        "on",
        "at",
        "\n"
    };

    int i, namelen = 0;
    char name[256];
    char found, tmpfound;

    found = FALSE;

    /* Eliminate spaces and "ignore" words */
    while (*arg && !found) {

        for (; *arg == ' '; arg++)
            ;

        for (i = 0; (name[i] = *(arg + i)) && (name[i] != ' '); i++)
            ;
        name[i] = 0;
        namelen = i;
        arg += i;
        if (search_block(name, ignore, TRUE) > -1)
            found = TRUE;
    }

    if (!name[0])
        return (0);

    *tar_ch = 0;
    *tar_obj = 0;

    if (IS_SET(bitvector, FIND_CHAR_ROOM)) { /* Find person in room */
        if ((*tar_ch = get_char_room_vis(ch, name))) {
            return (FIND_CHAR_ROOM);
        }
    }

    if (IS_SET(bitvector, FIND_CHAR_WORLD)) {
        if ((*tar_ch = get_char_vis(ch, name))) {
            return (FIND_CHAR_WORLD);
        }
    }

    if (IS_SET(bitvector, FIND_OBJ_EQUIP)) {
        for (found = FALSE, i = 0; i < MAX_WEAR && !found; i++) {
            if (namelen > 2)
                tmpfound = (ch->equipment[i] && strn_cmp_nullable(name, ch->equipment[i]->name, namelen) == 0);
            else
                tmpfound = (ch->equipment[i] && str_cmp_nullable(name, ch->equipment[i]->name) == 0);
            if (tmpfound) {
                *tar_obj = ch->equipment[i];
                found = TRUE;
            }
        }
        if (found) {
            return (FIND_OBJ_EQUIP);
        }
    }

    if (IS_SET(bitvector, FIND_OBJ_INV)) {
        //   if ((*tar_obj = get_obj_in_list_vis(ch, name, ch->carrying,9999))) {
        if ((*tar_obj = get_obj_in_list(name, ch->carrying))) {
            return (FIND_OBJ_INV);
        }
    }

    if (IS_SET(bitvector, FIND_OBJ_ROOM)) {
        if ((*tar_obj = get_obj_in_list_vis(ch, name, world[ch->in_room].contents, 9999))) {
            return (FIND_OBJ_ROOM);
        }
    }

    if (IS_SET(bitvector, FIND_OBJ_WORLD)) {
        if ((*tar_obj = get_obj_vis(ch, name))) {
            return (FIND_OBJ_WORLD);
        }
    }

    return (0);
}

/* a function to scan for "all" or "all.x" */
int find_all_dots(char* arg)
{
    if (!strcmp(arg, "all"))
        return FIND_ALL;
    else if (!strncmp(arg, "all.", 4)) {
        strcpy(arg, arg + 4);
        return FIND_ALLDOT;
    } else
        return FIND_INDIV;
}

char* money_message(int sum, int mode)
{
    static char moneystr[100];
    int g, s, c;

    *moneystr = 0;

    if (sum < 0) {
        strcpy(moneystr, std::format("{} copper coins", sum).c_str());
        return moneystr;
    }

    g = sum / COPP_IN_GOLD;
    c = sum % COPP_IN_GOLD;
    s = c / COPP_IN_SILV;
    c = c % COPP_IN_SILV;

    std::string out;
    if (g)
        std::format_to(std::back_inserter(out), "{} gold", g);
    if (g && c && s)
        out += ", ";
    if (!c && s && g)
        out += " and ";
    if (s)
        std::format_to(std::back_inserter(out), "{} silver", s);
    if ((g || s) && c)
        out += " and ";
    if (c || (!sum))
        std::format_to(std::back_inserter(out), "{} copper", c);

    if (mode)
        std::format_to(std::back_inserter(out), " coin{}",
            ((g == 1) && (s == 1)) || c == 1 ? "" : "s");

    strcpy(moneystr, out.c_str());
    return moneystr;
}

// char_exists()/set_char_exists()/remove_char_exists() (+ char_control_array,
// removed above) relocated to entity_lifecycle.cpp (entity-seed Task 5).
// register_npc_char() (+ its only global, last_control_set, formerly
// declared above) relocated there too (world-seed Task 1) -- pure
// abs_number allocation over that same bit-array, so it no longer needs to
// reach it by extern; declarations unchanged in handler.h.
int register_pc_char(struct char_data* ch)
{

    return register_npc_char(ch);
}

int can_swim(struct char_data* ch)
{

    struct obj_data* tmpobj;
    int tmp;

    if (IS_SHADOW(ch))
        return TRUE;

    if (!IS_NPC(ch) && !PRF_FLAGGED(ch, PRF_SWIM))
        return FALSE;

    if (IS_NPC(ch))
        if (IS_SET(ch->specials2.act, MOB_CAN_SWIM) || IS_AFFECTED(ch, AFF_FLYING) || IS_SHADOW(ch))
            return TRUE;

    if (IS_AFFECTED(ch, AFF_SWIM))
        return TRUE;

    if (!IS_NPC(ch) && GET_SKILL(ch, SKILL_SWIM) > 0)
        return TRUE;

    for (tmpobj = ch->carrying; tmpobj; tmpobj = tmpobj->next_content)
        if (tmpobj->obj_flags.type_flag == ITEM_BOAT)
            return TRUE;

    for (tmp = 0; tmp < MAX_WEAR; tmp++)
        if (ch->equipment[tmp])
            if ((ch->equipment[tmp])->obj_flags.type_flag == ITEM_BOAT)
                return TRUE;

    return FALSE;
}

void stop_riding(struct char_data* ch)
{
    struct char_data *tmpch, *mount;

    while (IS_RIDDEN(ch))
        stop_riding(ch->mount_data.rider);

    tmpch = 0;

    if (!IS_RIDING(ch))
        return;

    if (char_exists(ch->mount_data.mount_number)) {
        mount = ch->mount_data.mount;
        act("You stop riding $N.", FALSE, ch, 0, mount, TO_CHAR);
        act("$n stops riding $N.", FALSE, ch, 0, mount, TO_NOTVICT);
        act("$n stops riding you.", FALSE, ch, 0, mount, TO_VICT);

        if ((mount)->mount_data.rider == ch) {
            tmpch = ch;
            (mount)->mount_data.rider = ch->mount_data.next_rider;
            (mount)->mount_data.rider_number = ch->mount_data.next_rider_number;
        } else {
            for (tmpch = (mount)->mount_data.rider;
                 tmpch->mount_data.next_rider;
                 tmpch = tmpch->mount_data.next_rider) {
                if (tmpch->mount_data.next_rider == ch) {
                    tmpch->mount_data.next_rider = ch->mount_data.next_rider;
                    tmpch->mount_data.next_rider_number = ch->mount_data.next_rider_number;
                    break;
                }
            }
        }
        if (tmpch)
            IS_CARRYING_W(mount) -= GET_WEIGHT(ch) + IS_CARRYING_W(ch);
    }
    ch->mount_data.mount = 0;
    ch->mount_data.next_rider = 0;
    return;
}

void stop_riding_all(char_data* mount)
{
    char_data* tmpch;

    for (tmpch = character_list; tmpch; tmpch = tmpch->next)
        if (tmpch->mount_data.mount == mount)
            stop_riding(tmpch);
}

void recalc_zone_power()
{
    int tmp;
    char_data* tmpch;

    for (tmp = 0; tmp <= top_of_zone_table; tmp++) {
        // nature_power is set from the zone files.
        zone_table[tmp].white_power = 0;
        zone_table[tmp].dark_power = 0;
        zone_table[tmp].magi_power = 0;
    }

    for (tmpch = character_list; tmpch; tmpch = tmpch->next)
        if (!IS_NPC(tmpch) && (tmpch->in_room != NOWHERE)) {
            tmp = char_power(GET_LEVEL(tmpch));
            if (RACE_GOOD(tmpch))
                zone_table[world[tmpch->in_room].zone].white_power += tmp;
            else if (RACE_EVIL(tmpch))
                zone_table[world[tmpch->in_room].zone].dark_power += tmp;
            else if (RACE_MAGI(tmpch))
                zone_table[world[tmpch->in_room].zone].magi_power += tmp;
        }
}

/*
 * Indicates what side of the race war has the most influence
 * over this zone; returns -1 for dominating evil, 1 for domi-
 * nating good, 0 for neither.
 */
int report_zone_power(struct char_data* ch)
{
    struct zone_data* z;

    z = &zone_table[world[ch->in_room].zone];

    if (RACE_GOOD(ch)) {
        if (((z->dark_power > char_power(z->level) * 3 / 2) && (z->dark_power > z->white_power * 3 / 2)))
            return -1;
        if (((z->magi_power > char_power(z->level) * 3 / 2) && (z->magi_power > z->white_power * 3 / 2)))
            return -1;
    }

    else if (RACE_EVIL(ch)) {
        if (((z->white_power > char_power(z->level) * 4) && (z->white_power > z->dark_power * 4)))
            return 1;
        if (((z->magi_power > char_power(z->level) * 4) && (z->magi_power > z->dark_power * 4)))
            return -1;
    }

    else if (RACE_MAGI(ch)) {
        if (((z->white_power > char_power(z->level) * 4) && (z->white_power > z->magi_power * 4)))
            return 1;
        if (((z->dark_power > char_power(z->level) * 4) && (z->dark_power > z->magi_power * 4)))
            return -1;
    }

    return 0;
}
