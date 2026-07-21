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
#include <ctype.h>
#include <format>
#include <iterator>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "comm.h"
#include "entity_hooks.h"
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
// encumb_table[]/leg_encumb_table[] externs REMOVED (task-3 review
// Minor, placement-seam wave): both arrays' only uses in this file
// moved into equipment.cpp's attach_equipment()/detach_equipment()
// (placement-seam Task 3); this file has had no remaining reference
// to either since. See equipment.cpp's own extern declarations.
extern long race_affect[];
// max_race_str[] extern REMOVED (whole-branch review sweep, placement-seam
// wave): this file's only use (the weapon too-heavy check) moved to
// equipment.cpp's attach_equipment() (task-3 CRITICAL FIX; see that file's
// own max_race_str extern) -- this file has had no remaining reference
// since.

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

// follow_type_pool/follow_type_counter (private backing state for
// get_from_follow_type_pool()/put_to_follow_type_pool() below) relocated
// to entity_lifecycle.cpp alongside those two functions (placement-seam
// Task 4) -- no other handler.cpp function reads them directly; the
// forward declarations immediately below are unchanged and now resolve
// to the entity_lifecycle.cpp definitions (same pattern as the
// affected_type_pool functions above).

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

// isname() relocated to entity_lifecycle.cpp (placement-seam Task 4),
// alongside isname_nullable() (entity-seed Task 5). Declaration unchanged
// in handler.h.

// isname_nullable() relocated to entity_lifecycle.cpp (entity-seed Task 5);
// declaration unchanged in handler.h.

// affect_modify_room() relocated to entity_lifecycle.cpp (placement-seam
// Task 4). Declaration unchanged in handler.h.

// affect_modify() relocated to entity_lifecycle.cpp (db-split Task 4b);
// declaration unchanged in handler.h.

/* This updates a character by subtracting everything he is affected by */
/* restoring original abilities, and then affecting all again     ?????      */

// affect_total_room() relocated to entity_lifecycle.cpp (placement-seam
// Task 4) -- empty no-op body (census-flagged). Declaration unchanged in
// handler.h.

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

// affect_to_room() relocated to entity_lifecycle.cpp (placement-seam
// Task 4). Declaration unchanged in handler.h.

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

// affect_remove_room() relocated to entity_lifecycle.cpp (placement-seam
// Task 4). Declaration unchanged in handler.h.

// in_affected_list() DELETED (placement-seam Task 4): census-flagged
// DEAD, 0 callers repo-wide, re-verified via a fresh grep immediately
// before this deletion; its handler.h declaration is removed in the same
// commit.

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

// affect_from_char() relocated to entity_lifecycle.cpp (placement-seam
// Task 4). Declaration unchanged in handler.h.

// affected_by_spell() relocated to entity_lifecycle.cpp (db-split Task 4b);
// declaration unchanged in handler.h.

// room_affected_by_spell() relocated to entity_lifecycle.cpp
// (placement-seam Task 4). Declaration unchanged in handler.h.

// affect_join() relocated to entity_lifecycle.cpp (placement-seam
// Task 4). Declaration unchanged in handler.h.

//***************** follow_type procedures ********************************

// get_from_follow_type_pool()/put_to_follow_type_pool() relocated to
// entity_lifecycle.cpp, along with their follow_type_pool/
// follow_type_counter backing state (placement-seam Task 4); declarations
// (the forward-declared prototypes above) unchanged.

// add_follower() relocated verbatim to entity_lifecycle.cpp (L2;
// combat-trio wave, Task 1; trio-task-1-brief.md Step 3 / CONTROLLER
// ADDENDUM item 5; combat-trio-census.md section 5.4 -- census-clean,
// unrelated to stop_follower()'s own prior non-clean history despite the
// name-family similarity: stop_follower()/get_from_follow_type_pool()/
// put_to_follow_type_pool() were already L2 (combat-pilot wave Task 4a),
// and add_follower()'s only other calls are act() x3 (L1 output_seam) and
// printf (libc, a pre-existing error-path debug print, not routed through
// the log seam -- untouched by this move). Declaration unchanged
// (handler.h:223).

// stop_follower() relocated verbatim to entity_lifecycle.cpp (L2;
// combat-pilot wave Task 4a CONTROLLER ADDENDUM item 4, conditional
// re-check): pilot-census.md section 3.5 originally found this NOT
// census-clean (its forget(ch, ch->master) call was a genuine upward
// edge into still-app mobact.cpp). Once this task's own forget()/
// remember() package move (mobact.cpp -> entity_lifecycle.cpp) landed,
// re-deriving stop_follower's upward refs found forget() was its ONLY
// blocker -- put_to_follow_type_pool()/get_from_follow_type_pool()
// were already L2 (entity_lifecycle.cpp, world-seed-era). RELOCATED
// per the addendum's conditional. The stray, already-dead
// `void put_to_memory_rec_pool(struct memory_rec* oldaf);` forward
// declaration that used to sit here (zero call sites in this file) is
// dropped as part of this move's stranded-extern sweep. Declaration
// unchanged (handler.h:217).

// circle_follow() relocated to char_utils.cpp (placement-seam Task 4):
// pure master-chain walk, no world/live-state dependency. Declaration
// unchanged in handler.h.

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
// obj_from_char() relocated to containment.cpp (blocker-buster wave
// Task 3, census section E; supersedes the placement-seam-wave deferral
// this comment used to carry -- see git history for that trail).
// entity_hooks.h's poison-removal notification hook is what finally
// unblocked the move: the equipment-fallback branch's call target that
// used to force this function to stay app-side (this file's unequip_char()
// wrapper below, for its poison damage/raw_kill side effect) is now
// detach_equipment() (equipment.cpp, L2 primitive) plus a fired
// notification, reproducing that side effect without an upward L2->app
// call. See containment.cpp's obj_from_char() relocation comment,
// entity_hooks.h's poison_removal_fn doc comment, and
// .superpowers/sdd/task-3-report.md (blocker-buster wave) for the full
// evidence trail, including the pre-inversion characterization test
// (src/tests/poison_notification_tests.cpp) that pins this move as
// behavior-identical. Declaration stays in handler.h.

// equip_char()/unequip_char() bodies relocated verbatim to fight.cpp
// (combat-pilot wave Task 4a; pilot-census.md section 3.8 confirmed no
// handler.cpp-internal-static snag -- both wrappers only call
// attach_equipment()/detach_equipment() (L2 equipment.cpp), act()/
// send_to_char() (L1 output_seam), and damage()/raw_kill() (fight.cpp's
// own definitions)). Declarations unchanged (handler.h:97-98).

// get_number() relocated to rots_util.cpp (rots_platform, placement-seam
// Task 1's sequencing fix -- the census classifies it MOVE-OTHER(platform),
// originally scheduled for Task 4, but get_char_room's Task 1 move needed
// it moved first; see task-1-report.md). Declaration moved to utils.h (this
// file had none of its own -- every prior caller outside this file forward-
// declared it locally; those local externs are untouched and still resolve
// to the same definition).

// parse_numbered_name() relocated to rots_util.cpp (rots_platform,
// combat-seed Task 3): the placement-seam Task 4 deferral above is now
// resolved -- NumberedName moved to rots/platform/numbered_name.h, an
// L0-visible header, so this function no longer needs handler.h's
// transitive rots_core includes to be defined at L0. Declaration stays
// in handler.h (unchanged callers). get_char() below completes its own
// deferred move in this same commit; see its site.

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

// get_char() relocated to entity_lifecycle.cpp (rots_entity,
// combat-seed Task 3): the placement-seam Task 4 deferral above is now
// resolved -- parse_numbered_name() (immediately above) moved to
// rots_util.cpp (rots_platform, L0) in this same commit, so this is a
// legal downward edge (rots_entity links RotS::platform) instead of the
// upward edge that blocked the move before. Declaration stays in
// handler.h (unchanged callers).

// get_char_num() DELETED (placement-seam Task 4): census-flagged DEAD,
// 0 callers repo-wide, re-verified via a fresh grep immediately before
// this deletion; its handler.h declaration is removed in the same
// commit.

// obj_to_room()/obj_from_room()/obj_to_obj()/obj_from_obj()/
// object_list_new_owner() relocated to containment.cpp (placement-seam
// Task 2): obj_to_room()/obj_from_room()'s world[room] accesses become
// room_by_id_total(room) (rots_world resolver seam, TOTAL variant -- both
// original bodies indexed world[] unchecked). See that file for the moved
// bodies and task-2-report.md for the evidence trail. Declarations stay in
// handler.h.

// extract_obj() relocated to object_utils.cpp (blocker-buster wave
// Task 3, census section E, ADJUDICATE-3; supersedes the placement-seam-
// wave deferral this comment used to carry -- see git history for that
// trail). extract_obj() depended on obj_from_char(), which itself stayed
// app-side until this task's poison-removal notification hook resolved
// its own blocker (see obj_from_char's relocation comment above); with
// obj_from_char() an L2 citizen, extract_obj() moves in the same task,
// substituting obj_index_by_id() for its direct obj_index[] read (the
// same resolver get_obj_in_list_vnum() already uses, object_utils.cpp).
// See object_utils.cpp's extract_obj() relocation comment and
// .superpowers/sdd/task-3-report.md (blocker-buster wave) for the full
// evidence trail. Declaration stays in handler.h.

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

// Registers the real extract_char(ch, new_room) body above as
// entity_hooks.h's extract_char hook (RE-HOMED from combat_hooks.h,
// l4-seed wave Task 1; l4-census.md section 3.4 -- originally landed
// combat-pilot wave Task 4b). Called once from run_the_game(), before
// boot_db() -- same convention as this file's other registrars (e.g.
// register_poison_removal_hook() in fight.cpp).
void register_extract_char_hook()
{
    rots::entity::set_extract_char_hook(extract_char);
}

/* ***********************************************************************
   Here follows high-level versions of some earlier routines, ie functions
   which incorporate the actual player-data.
   *********************************************************************** */

// keyword_matches_char() relocated to char_utils.cpp (placement-seam
// Task 4). Declaration unchanged in handler.h.

// get_char_room_vis()/get_player_vis()/get_char_vis() relocated to
// visibility.cpp (blocker-buster Task 4b, completing Task 4's split):
// the 3-arg CAN_SEE(sub,obj,light_mode) overload they ride now resolves
// in-lib too (Task 4b Step 1(a) carved see_hiding() out of ranger.cpp).
// get_char_room_vis's one unchecked world[ch->in_room].people site
// resolves via room_by_id_total(ch->in_room)->people per the BINDING
// addendum's resolver-variant rule. Declarations unchanged in handler.h.
// See task-4b-report.md for the full evidence.
// get_obj_in_list_vis() relocated to visibility.cpp (blocker-buster
// Task 4; census section A verdict MOVE-L3-COMBAT): entity-pure list
// walk riding the moved CAN_SEE_OBJ(), no world[] touch of its own.
// Declaration unchanged in handler.h.

// get_obj_vis() relocated to visibility.cpp (blocker-buster Task 4;
// census section A verdict MOVE-L3-COMBAT): its one
// world[ch->in_room].contents site (unchecked in the original, no bounds
// test) resolves via room_by_id_total(ch->in_room)->contents per the
// BINDING addendum's resolver-variant rule. Declaration unchanged in
// handler.h.

// get_object_in_equip_vis() relocated to visibility.cpp (blocker-buster
// Task 4; census section A verdict MOVE-L3-COMBAT): entity-pure
// equipment-array scan riding the moved CAN_SEE_OBJ(), no world[] touch.
// Declaration unchanged in handler.h.

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

// generic_find() relocated to visibility.cpp (blocker-buster Task 4b,
// completing Task 4's split): its search_block() call now resolves
// in-lib via rots_util.cpp/rots_platform (Task 4b Step 1(b) mini-census
// verdict: platform-clean, get_number() precedent), and it rides
// get_char_room_vis()/get_char_vis() above, both now in-lib too. Its one
// unchecked world[ch->in_room].contents site resolves via
// room_by_id_total(ch->in_room)->contents per the BINDING addendum's
// resolver-variant rule (the same substitution get_obj_vis() already
// uses for the identical original expression). Declaration unchanged in
// handler.h. See task-4b-report.md for the full evidence.
// find_all_dots() relocated to rots_util.cpp (rots_platform,
// placement-seam Task 4). Declaration unchanged in handler.h.

// money_message() relocated to rots_util.cpp (rots_platform,
// placement-seam Task 4). Declaration unchanged in handler.h.

// char_exists()/set_char_exists()/remove_char_exists() (+ char_control_array,
// removed above) relocated to entity_lifecycle.cpp (entity-seed Task 5).
// register_npc_char() (+ its only global, last_control_set, formerly
// declared above) relocated there too (world-seed Task 1) -- pure
// abs_number allocation over that same bit-array, so it no longer needs to
// reach it by extern; declarations unchanged in handler.h.
// register_pc_char() relocated to entity_lifecycle.cpp (placement-seam
// Task 4), alongside register_npc_char() (its only callee, already
// there). Declaration unchanged in handler.h.

// can_swim() relocated to char_utils.cpp (placement-seam Task 4).
// Declaration unchanged in handler.h.

// stop_riding() relocated to char_utils.cpp (combat-pilot wave Task 2;
// census-confirmed L2-clean -- char_exists()/act() only, no upward refs;
// see .superpowers/sdd/pilot-census.md section 3.4). stop_riding_all()
// below stays here: it CALLS stop_riding() (not the other way around), so
// it is not a required same-move companion -- it keeps working via the
// unchanged handler.h declaration, now resolving to a legal app->L2
// downward call. Declaration unchanged in handler.h.

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
