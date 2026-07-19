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
// char_from_room() SPLIT (placement-seam Task 3, census row char_from_room:661
// / ADJUDICATE-2 Disposition A, binding): the room-list unlink + light-dec +
// zone-power-dec + in_room/next_in_room clear moved verbatim into
// placement.cpp's new detach_char_from_room(ch) primitive (L2) -- world[]/
// zone_table[] substituted for room_by_id_total()/zone_by_id() per the
// BINDING addendum's unchecked-access rule (this function's world[ch->in_room]
// reads carry no bounds test of their own). This app-side wrapper keeps the
// public name/declaration (handler.h, unchanged).
//
// bool status branch (controller adjudication, supersedes this task's own
// original unconditional-call version -- see task-3-report.md): the
// ORIGINAL char_from_room had two early-return paths that both skipped
// this trailing stop_fighting loop entirely; detach_char_from_room() now
// reports false on exactly those two paths (mapping below), so the loop
// only runs when it reports true (the full detach ran) -- reproducing the
// original's control flow exactly, with neither early-return condition
// re-evaluated here.
//   ORIGINAL early-return path                 -> primitive result
//   ch->in_room == NOWHERE (already nowhere)    -> false (skip)
//   `if (!i) return;` (room-list corruption)     -> false (skip)
//   (fell through to the end -- full detach ran) -> true (run loop)
void char_from_room(struct char_data* ch)
{
    if (detach_char_from_room(ch)) {
        int tmp;
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
}

// char_to_room() relocated verbatim to placement.cpp (placement-seam Task 3,
// census row char_to_room:716 / ADJUDICATE-1 Disposition A, binding): every
// world[room] access below was originally unchecked (no bounds test in this
// function) -- per the BINDING addendum's per-site rule, all become
// room_by_id_total(room); zone_table[world[room].zone] becomes
// zone_by_id(r->zone) (the new zone resolver, ADJUDICATE-1). Declaration
// stays in handler.h.

// obj_to_char() relocated to containment.cpp (placement-seam Task 2):
// entity-pure obj->char containment mutator, no world[] access. See that
// file for the moved body and task-2-report.md for the evidence trail.
// Declaration stays in handler.h.
//
// obj_from_char() stays here (placement-seam Task 3 STOP-CHECK,
// controller-adjudicated deferral -- Disposition B; see task-3-report.md
// for the full evidence trail; supersedes Task 2's own deferral note,
// which cited a now-resolved blocker): Task 2 deferred this function
// because unequip_char() wasn't yet an L2 citizen; Task 3's equipment
// SPLIT resolved THAT blocker (attach_equipment()/detach_equipment() now
// live in equipment.cpp, L2). But the task brief's mandatory STOP-CHECK
// on this function's unequip_char() call target (line ~708 below)
// surfaced a genuine reachable counter-example, not just a link-order
// problem: script.cpp's SCRIPT_ASSIGN_EQ command reads an EQUIPPED item
// (tmpch->equipment[pos]) into a script object-param slot, and a
// subsequent SCRIPT_OBJ_FROM_CHAR on that same slot reaches THIS
// function with an object whose carried_by is set but which is not in
// the ->carrying list -- i.e. the equipment-fallback branch below,
// calling unequip_char() (not the primitive), IS reachable in live
// mudscript-driven play, not just defensively. unequip_char() (the app
// wrapper) runs the poison damage/raw_kill block on that path;
// detach_equipment() (the L2 primitive) does not. Moving this function
// to containment.cpp would force its call at line ~708 to target
// detach_equipment() instead (calling the app-tier unequip_char()
// wrapper from L2 recreates exactly the upward edge Task 2's own
// EntityLayerAcyclicity check already rejected once), silently dropping
// that poison side effect for the scripted path -- a real behavior
// change, which this wave's Global Constraint ("Zero behavior change for
// ageland") does not permit without explicit owner sign-off. Deferred
// pending either a poison-notification hook (so the L2 primitive could
// signal the app tier without calling it) or an explicit accepted-risk
// decision on this narrow scripted edge case. Byte-identical to its
// pre-Task-2 body (unchanged since).

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

// equip_char() SPLIT (placement-seam Task 3, census row equip_char:815):
// primitive attach_equipment(ch, obj, pos) (L2, equipment.cpp) keeps the
// slot assignment, encumb/leg-encumb/weight math, OB/PB/dodge mutation,
// affect_modify()+affect_total(), and the light-inc (world[]->
// room_by_id_total(), unchecked historical access per the BINDING addendum).
// This app-side wrapper keeps the public name/declaration (handler.h,
// unchanged) and the census's named "app remainder": the initial guards
// (byte-preserved -- they gate whether the primitive is even called, so
// they must run first, exactly as in the original), the anti-align/
// anti-race zap `act` messages + `obj_to_char` re-drop (census 833-853),
// the "too heavy" `send_to_char` messages (census 880-883), and the poison
// `damage`/`raw_kill` block (census 905-916) -- all byte-preserved below.
//
// EquipAttachOutcome status branch (controller adjudication, supersedes
// this task's own original re-check version -- see task-3-report.md): the
// ORIGINAL's `if ((item_slot == HOLD) && !CAN_WEAR(item, ITEM_HOLD))
// return;` guard (between the weight math and the ARMOR/WEAPON/SHIELD/
// LIGHT dispatch) caused the ORIGINAL to skip the too-heavy messages,
// affect_modify()/affect_total(), AND the poison block together whenever
// it fired; the too-heavy messages only applied inside the WEAPON arm of
// that same dispatch. attach_equipment() now reports which of those two
// ORIGINAL conditions applied (mapping below) instead of the wrapper
// re-deriving either one -- both are evaluated exactly once, inside
// attach_equipment().
//   ORIGINAL condition                              -> primitive result
//   (item_slot == HOLD) && !CAN_WEAR(item, ITEM_HOLD) fired -> HOLD_EARLY_RETURN (run nothing further)
//   ran to completion, GET_ITEM_TYPE(item) == ITEM_WEAPON   -> WEAPON (run too-heavy check + poison block)
//   ran to completion, item is not a weapon                 -> OTHER (skip too-heavy check; run poison block)
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

    EquipAttachOutcome outcome = attach_equipment(character, item, item_slot);

    if (outcome == EquipAttachOutcome::HOLD_EARLY_RETURN)
        return;

    if (outcome == EquipAttachOutcome::WEAPON) {
        if (GET_OBJ_WEIGHT(item) > (GET_BAL_STR(character) * 50) && !IS_TWOHANDED(character))
            send_to_char("This weapon seems too heavy for one hand.\n\r", character);
        else if (GET_OBJ_WEIGHT(item) > (GET_BAL_STR(character) * 100))
            send_to_char("This weapon seems too heavy for you!\n\r", character);
    }

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

// unequip_char() SPLIT (placement-seam Task 3, census row unequip_char:919):
// primitive detach_equipment(ch, pos) (L2, equipment.cpp) keeps the mudlog
// zero-object guard (L0-legal, per this task's binding brief), slot clear,
// encumb/weight math, OB/PB/dodge mutation, affect_modify()+affect_total(),
// and the light-dec (world[]->room_by_id_total(), unchecked historical
// access per the BINDING addendum). This app-side wrapper keeps the public
// name/declaration (handler.h, unchanged) and the census's named "app
// remainder": the poison `damage`/`raw_kill` block (census 980-991),
// byte-preserved below.
//
// Controller adjudication (task-3-report.md) considered giving
// detach_equipment() a status return too (mirroring attach_equipment()
// above), then confirmed it unnecessary: this primitive's own `(pos ==
// HOLD) && !CAN_WEAR(obj, ITEM_HOLD)) return obj;` early return happens
// strictly BEFORE affect_modify()/affect_total(), so ch's affected-list
// (and therefore IS_AFFECTED(ch, AFF_POISON)) is PROVABLY unchanged
// whenever that guard fires -- nothing between capturing `was_poisoned`
// below and the poison check touches ch->specials.affected_by in that
// path. That makes the poison block's own condition (`was_poisoned != 0
// && !IS_AFFECTED(ch, AFF_POISON)`) false by construction whenever the
// primitive early-returned (one operand and its exact negation can never
// both hold), so this wrapper needs no re-derivation of the HOLD guard at
// all -- only the `if (!obj) return obj;` null check below remains,
// branching on the return value (not re-deriving `!ch->equipment[pos]`)
// for the zero-object case, exactly as the ORIGINAL returned out of this
// point too.
struct obj_data* unequip_char(struct char_data* ch, int pos)
{
    int was_poisoned = 0;

    was_poisoned = IS_AFFECTED(ch, AFF_POISON);

    struct obj_data* obj = detach_equipment(ch, pos);

    if (!obj)
        return obj;

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

// get_obj_in_list()/get_obj_in_list_num()/get_obj_in_list_vnum()/
// get_obj_in_list_num_containers()/count_obj_in_list()/get_obj() relocated
// to object_utils.cpp (placement-seam Task 2): get_obj_in_list_vnum()'s
// obj_index[item].virt read becomes obj_index_by_id(item)->virt (rots_world
// resolver seam, ADJUDICATE-3). See that file for the moved bodies and
// task-2-report.md for the evidence trail. Declarations stay in handler.h.
//
// get_obj_num() DELETED (placement-seam Task 2): census-flagged DEAD, 0
// callers repo-wide, re-verified via a fresh grep immediately before this
// deletion; its handler.h declaration is removed in the same commit.

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

// obj_to_room()/obj_from_room()/obj_to_obj()/obj_from_obj()/
// object_list_new_owner() relocated to containment.cpp (placement-seam
// Task 2): obj_to_room()/obj_from_room()'s world[room] accesses become
// room_by_id_total(room) (rots_world resolver seam, TOTAL variant -- both
// original bodies indexed world[] unchecked). See that file for the moved
// bodies and task-2-report.md for the evidence trail. Declarations stay in
// handler.h.

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

// extract_obj() DEVIATION (placement-seam Task 3 STOP-CHECK,
// controller-adjudicated deferral -- Disposition B; see task-3-report.md;
// supersedes Task 2's own deferral note): NOT moved this task either.
// ADJUDICATE-3's obj_index_by_id() substitution alone still compiles and
// links clean in isolation, but extract_obj calls obj_from_char(), which
// itself stays app-side this task -- see obj_from_char's own comment
// above for the STOP-CHECK evidence (a live mudscript path,
// SCRIPT_ASSIGN_EQ + SCRIPT_OBJ_FROM_CHAR, reaches obj_from_char's
// equipment-fallback branch with a genuinely equipped item, so
// obj_from_char must keep calling the app-tier unequip_char() wrapper
// -- with its poison damage/raw_kill block -- to stay behavior-
// identical; that upward edge is illegal from L2, so obj_from_char
// cannot move, and neither can extract_obj, which depends on it).
// Byte-identical to its pre-Task-2 body (unchanged since).

// update_object()/update_char_objects() relocated to object_utils.cpp
// (placement-seam Task 2): entity-pure recursive object-timer helpers, no
// world[]/obj_index[] access. See that file for the moved bodies and
// task-2-report.md for the evidence trail. Declarations stay in handler.h.

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

// create_money() relocated to object_utils.cpp (placement-seam Task 2):
// entity-pure object constructor, no world[] access. See that file for the
// moved body and task-2-report.md for the evidence trail. Declaration
// stays in handler.h.

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
