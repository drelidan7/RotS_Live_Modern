/* ************************************************************************
 *   File: mobact.c                                      Part of CircleMUD *
 *  Usage: Functions for generating intelligent (?) behavior in mobiles    *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#include "platdef.h"
#include <cstdint>
#include <format>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "char_utils.h"
#include "combat_hooks.h"
#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "output_seam.h"
#include "rots/core/character.h"
#include "rots/core/object.h"
#include "rots/core/room.h"
#include "rots/core/types.h"
#include "script_hooks.h"
#include "utils.h"

/* external structs */
extern struct char_data* character_list;
extern struct index_data* mob_index;
extern struct room_data world;
extern int top_of_world;

// The twelve do_* forward decls formerly here (do_move/do_rescue/
// do_assist/do_stand/do_hit/do_say/do_flee/do_wear, plus do_sleep/
// do_rest/do_sit/do_wake declared near enforce_position() below) are
// RETIRED (behavior wave Task 2, bw-task-2-brief.md Step 1): every
// call site in this file now dispatches through the existing 26-cell
// rots::combat::issue_command() table instead of calling the ACMD
// symbol directly, so none of the twelve is referenced by name here
// any more.

SPECIAL(intelligent);

void one_mobile_activity(struct char_data*);
// void* virt_program_number(int number); forward decl RETIRED
// (behavior wave Task 2): both call sites below now dispatch
// through rots::script::dispatch_virt_program_number() instead.
int find_first_step(int, int);

void enforce_position(struct char_data*, int);

void mobile_activity(void)
{
    struct char_data* ch;
    SPECIAL(*tmpfunc);

    for (ch = character_list; ch; ch = ch->next)
        if (!number(0, 3)) {
            if (IS_NPC(ch))
                one_mobile_activity(ch);
            else {
                tmpfunc = (SPECIAL(*))
                    rots::script::dispatch_virt_program_number(ch->specials.store_prog_number);
                if (tmpfunc)
                    tmpfunc(ch, ch, 0, mutable_arg(""), SPECIAL_SELF, 0);
            }
        }
}

void one_mobile_activity(char_data* ch)
{
    int door, found, max, tmp, is_passive;
    struct char_data *tmp_ch, *tmpch, *vict;
    struct obj_data *obj, *best_obj, *inside, *next_obj;
    struct waiting_type wtl;
    struct memory_rec* names;
    struct follow_type* tmpfol;
    SPECIAL(*tmpfunc);

    wtl.cmd = wtl.subcmd = wtl.priority = 0;

    if (!ch)
        return;
    if (!IS_NPC(ch))
        return;

    if ((ch->in_room < 0) || (ch->in_room > top_of_world)) {
        const std::string out_of_range_message = std::format("mobile_act called for {} in {}.",
            GET_NAME(ch), ch->in_room);
        mudlog(out_of_range_message, NRM, LEVEL_IMPL, FALSE);
        return;
    }

    is_passive = 0;
    if (MOB_FLAGGED(ch, MOB_PET) && !utils::is_guardian(*ch) && ch->master && char_exists(ch->master_number)) {
        if (ch->in_room == ch->master->in_room) {
            is_passive = 1;
        }
    }

    // handle interrupts
    if (ch->specials.fighting && ch->interrupt_count > 0 && ch->interrupt_time > 0) {
        ch->interrupt_time = ch->interrupt_time - 1;
        if (ch->interrupt_time == 0) {
            ch->interrupt_count = ch->interrupt_count - 1;
            if (ch->interrupt_count > 0) {
                ch->interrupt_time = 10;
            }
        }
    }

    if (IS_MOB(ch) && ch->delay.wait_value <= 1 && !is_passive) {
        /* Tamed mobs can stay in non-default positions... */
        if (!IS_AFFECTED(ch, AFF_CHARM)) {
            enforce_position(ch, ch->specials.default_pos);
        }

        /* Examine call for special procedure */
        if (IS_SET(ch->specials2.act, MOB_SPEC) && !no_specials_active()) {
            const std::string prog_debug_message = std::format("{} - find prog: {}, func:{}", GET_NAME(ch), ch->specials2.act, reinterpret_cast<std::intptr_t>(mob_index[ch->nr].func));
            mudlog_aliased_mob(prog_debug_message, ch, "progdebug");
            if (!mob_index[ch->nr].func && ch->specials.store_prog_number) {
                tmpfunc = (SPECIAL(*))rots::script::dispatch_virt_program_number(ch->specials.store_prog_number);
                if (tmpfunc) {
                    if (tmpfunc(ch, ch, 0, mutable_arg(""), SPECIAL_SELF, 0)) {
                        return;
                    }
                }
            } else {
                if (mob_index[ch->nr].func) {
                    if ((*mob_index[ch->nr].func)(ch, ch, 0, mutable_arg(""), SPECIAL_SELF, 0)) {
                        return;
                    }
                }
            }

        } else {
            if (ch->specials.special_prog_number) {
                if (intelligent(ch, ch, 0, mutable_arg(""), SPECIAL_SELF, 0)) {
                    return;
                }
            }
        }

        // STOP here if mob is now busy (I believe this occurs when methods above return FALSE when they should be TRUE )
        // BECAUSE: other things are checking subcmd and delay time BUT subcmd gets set to 0 ABOVE!??
        if (ch->delay.wait_value && ch->delay.cmd) {
            return;
        }

        if (!ch->specials.fighting) {
            ch->interrupt_count = 0;
            ch->interrupt_time = 0;
        }

        /* mob - helper */
        const room_data& room = world[ch->in_room];
        if (AWAKE(ch) && IS_SET(ch->specials2.act, MOB_HELPER) && !ch->specials.fighting) {
            for (char_data* ally = room.people; ally; ally = ally->next_in_room) {
                // Don't assist allies that you are aggressive to.
                if (IS_AGGR_TO(ch, ally))
                    continue;

                // Never assist guardians or orc followers.
                if (MOB_FLAGGED(ally, MOB_GUARDIAN) || (MOB_FLAGGED(ally, MOB_ORC_FRIEND) && MOB_FLAGGED(ally, MOB_PET)))
                    continue;

                char_data* enemy = ally->specials.fighting;
                if (enemy && IS_NPC(ally) && CAN_SEE(ch, ally)) {
                    bool assist = false;
                    if (IS_AGGR_TO(ch, enemy)) {
                        // Always assist against targets that you are aggressive to.
                        assist = true;
                    } else {
                        // Assist if the ally has the same alignment as the helper, and the ally
                        // is not aggressive to its enemy.
                        if (GET_ALIGNMENT(ch) * GET_ALIGNMENT(ally) > 0 && !IS_AGGR_TO(ally, enemy)) {
                            assist = true;
                        }
                    }

                    if (assist) {
                        if (GET_INT_BASE(ch) >= 7) {
                            rots::combat::issue_command(rots::combat::combat_command::say, ch, mutable_arg("I must protect my friend!"), 0, 0, 0);
                        }
                        wtl.targ1.type = TARGET_CHAR;
                        wtl.targ1.ptr.ch = ally;
                        wtl.targ1.ch_num = ally->abs_number;
                        wtl.cmd = CMD_ASSIST;
                        rots::combat::issue_command(rots::combat::combat_command::assist, ch, mutable_arg(""), &wtl, 0, 0);
                        break;
                    }
                }
            }
        }

        /* bodyguard - follower */
        if (AWAKE(ch) && IS_SET(ch->specials2.act, MOB_BODYGUARD) && ch->master && (ch->master->in_room == ch->in_room)) {

            if (GET_POS(ch) < POSITION_FIGHTING)
                rots::combat::issue_command(rots::combat::combat_command::stand, ch, mutable_arg(""), 0, 0, 0);

            tmp_ch = (ch->master)->specials.fighting;

            if (tmp_ch && (tmp_ch->specials.fighting == ch->master)) {
                char rescue_target_name[MAX_STRING_LENGTH];
                sscanf(ch->master->player.name, "%s", rescue_target_name);
                //	     printf("trying to rescue %s.\n",rescue_target_name);
                wtl.targ1.type = TARGET_CHAR;
                wtl.targ1.ptr.ch = ch->master;
                wtl.targ1.ch_num = ch->master->abs_number;
                wtl.cmd = CMD_RESCUE;
                rots::combat::issue_command(rots::combat::combat_command::rescue, ch, rescue_target_name, &wtl, 0, 0);
            }
            if (tmp_ch && !(ch->specials.fighting)) {
                char hit_target_name[MAX_STRING_LENGTH];
                sscanf(tmp_ch->player.name, "%s", hit_target_name);
                wtl.targ1.type = TARGET_CHAR;
                wtl.targ1.ptr.ch = tmp_ch;
                wtl.targ1.ch_num = tmp_ch->abs_number;
                wtl.cmd = CMD_HIT;
                rots::combat::issue_command(rots::combat::combat_command::hit, ch, hit_target_name, &wtl, 0, 0);
            }
        }

        /* bodyguard - master */
        if (AWAKE(ch) && IS_SET(ch->specials2.act, MOB_BODYGUARD) && ch->followers) {
            if (GET_POS(ch) < POSITION_FIGHTING)
                rots::combat::issue_command(rots::combat::combat_command::stand, ch, mutable_arg(""), 0, 0, 0);

            for (tmpfol = ch->followers; tmpfol; tmpfol = tmpfol->next) {
                tmp_ch = (tmpfol->follower)->specials.fighting;
                if (tmp_ch && (tmp_ch->specials.fighting == tmpfol->follower)) {
                    char rescue_target_name[MAX_STRING_LENGTH];
                    sscanf(tmpfol->follower->player.name, "%s", rescue_target_name);
                    wtl.targ1.type = TARGET_CHAR;
                    wtl.targ1.ptr.ch = tmpfol->follower;
                    wtl.targ1.ch_num = tmpfol->follower->abs_number;
                    wtl.cmd = CMD_RESCUE;
                    rots::combat::issue_command(rots::combat::combat_command::rescue, ch, rescue_target_name, &wtl, 0, 0);
                }
            }
        }

        /* Assistant mob.  They always assist their master if their master is fighting
         * and they are not. */
        if (AWAKE(ch) && IS_SET(ch->specials2.act, MOB_ASSISTANT) && ch->master) {
            if (ch->master->in_room == ch->in_room && ch->master->specials.fighting && ch->specials.fighting == NULL) {
                if (CAN_SEE(ch, ch->master)) {
                    if (GET_POS(ch) < POSITION_FIGHTING) {
                        rots::combat::issue_command(rots::combat::combat_command::stand, ch, mutable_arg(""), NULL, 0, 0);
                    }

                    wtl.targ1.type = TARGET_CHAR;
                    wtl.targ1.ptr.ch = ch->master;
                    wtl.targ1.ch_num = ch->master->abs_number;
                    wtl.cmd = CMD_ASSIST;
                    rots::combat::issue_command(rots::combat::combat_command::assist, ch, mutable_arg(""), &wtl, 0, 0);
                }
            }
        }

        /* Guardians, special case */
        if (utils::is_guardian(*ch) && ch->master && ch->specials.fighting) {
            if (ch->master->in_room != ch->in_room) {
                rots::combat::issue_command(rots::combat::combat_command::flee, ch, mutable_arg(""), NULL, 0, 0);
            }
        }

        if (ch->specials.fighting && (GET_POS(ch) > POSITION_SITTING) && (IS_SET(ch->specials2.act, MOB_SWITCHING) || IS_SET(ch->specials2.act, MOB_SHADOW))) {
            for (tmpch = world[ch->in_room].people; tmpch;
                 tmpch = tmpch->next_in_room)
                if ((tmpch->specials.fighting == ch) && !number(0, 3) && CAN_SEE(ch, tmpch) && (tmpch != ch->specials.fighting)) {
                    ch->specials.fighting = tmpch;
                    act("$n turns to fight $N!", TRUE, ch, 0, tmpch, TO_ROOM);
                    break;
                }
        }

        if (AWAKE(ch) && !(ch->specials.fighting)) {
            if (IS_SET(ch->specials2.act, MOB_SCAVENGER)) { /* if scavenger */
                if (world[ch->in_room].contents && !number(0, 5)) {
                    for (max = 1, best_obj = 0, obj = world[ch->in_room].contents;
                         obj; obj = obj->next_content) {
                        if (CAN_GET_OBJ(ch, obj)) {
                            if (obj->obj_flags.cost > max) {
                                best_obj = obj;
                                max = obj->obj_flags.cost;
                            } else if (GET_ITEM_TYPE(obj) == ITEM_CONTAINER)
                                for (inside = obj->contains; inside; inside = next_obj) {
                                    next_obj = inside->next_content;
                                    if (inside->obj_flags.wear_flags > 1)
                                        if (IS_CARRYING_N(ch) < CAN_CARRY_N(ch)) {
                                            obj_from_obj(inside);
                                            obj_to_char(inside, ch);
                                            act("$n gets $p from $P.",
                                                TRUE, ch, inside, obj, TO_ROOM);
                                            rots::combat::issue_command(rots::combat::combat_command::wear, ch, mutable_arg("all"), 0, 0, 0);
                                        }
                                }
                        }
                    } /* for */

                    if (best_obj) {
                        obj_from_room(best_obj);
                        obj_to_char(best_obj, ch);
                        act("$n gets $p.", FALSE, ch, best_obj, 0, TO_ROOM);
                    }
                }
            } /* Scavenger */

            if (!IS_SET(ch->specials2.act, MOB_SENTINEL) && (GET_POS(ch) == POSITION_STANDING) && (!ch->master) && ((door = number(0, 45)) < NUM_OF_DIRS) && CAN_GO(ch, door) && !IS_SET(world[EXIT(ch, door)->to_room].room_flags, NO_MOB) && !IS_SET(world[EXIT(ch, door)->to_room].room_flags, DEATH)) {
                if (ch->specials.last_direction == door)
                    ch->specials.last_direction = -1;
                else {
                    /* checking for STAY flags */
                    if ((!IS_SET(ch->specials2.act, MOB_STAY_ZONE) || (world[EXIT(ch, door)->to_room].zone == world[ch->in_room].zone)) && (!IS_SET(ch->specials2.act, MOB_STAY_TYPE) || (world[EXIT(ch, door)->to_room].sector_type == world[ch->in_room].sector_type))) {
                        ch->specials.last_direction = door;
                        rots::combat::issue_command(rots::combat::combat_command::move, ch, mutable_arg(""), 0, ++door, 0);
                    }
                }
            } /* if can go */

            /* Here go Race aggressions */
            if (ch->specials2.pref) {
                for (tmp_ch = world[ch->in_room].people; tmp_ch;
                     tmp_ch = tmp_ch->next_in_room)
                    if ((ch != tmp_ch) && (!IS_SET(ch->specials2.act, MOB_MOUNT)) && IS_AGGR_TO(ch, tmp_ch) && CAN_SEE(ch, tmp_ch)) {
                        char hit_target_name[MAX_STRING_LENGTH];
                        sscanf(tmp_ch->player.name, "%s", hit_target_name);
                        wtl.targ1.type = TARGET_CHAR;
                        wtl.targ1.ptr.ch = tmp_ch;
                        wtl.targ1.ch_num = tmp_ch->abs_number;
                        wtl.cmd = CMD_HIT;
                        rots::combat::issue_command(rots::combat::combat_command::hit, ch, hit_target_name, &wtl, 0, 0);
                        break;
                    }
                if (tmp_ch)
                    return; // continue;
            }

            /* Standard aggressive mobs */
            if (IS_SET(ch->specials2.act, MOB_AGGRESSIVE) && (!IS_SET(ch->specials2.act, MOB_MOUNT))) {
                found = FALSE;
                vict = 0;
                for (tmp_ch = world[ch->in_room].people; tmp_ch && !found;
                     tmp_ch = tmp_ch->next_in_room) {
                    if (!IS_NPC(tmp_ch) && CAN_SEE(ch, tmp_ch) && !PRF_FLAGGED(tmp_ch, PRF_NOHASSLE)) {
                        if (!IS_SET(ch->specials2.act, MOB_WIMPY) || !AWAKE(tmp_ch)) {
                            if ((IS_SET(ch->specials2.act, MOB_AGGRESSIVE_EVIL) && IS_EVIL(tmp_ch)) || (IS_SET(ch->specials2.act, MOB_AGGRESSIVE_GOOD) && IS_GOOD(tmp_ch)) || (IS_SET(ch->specials2.act, MOB_AGGRESSIVE_NEUTRAL) && IS_NEUTRAL(tmp_ch)) || (!IS_SET(ch->specials2.act, MOB_AGGRESSIVE_EVIL) && !IS_SET(ch->specials2.act, MOB_AGGRESSIVE_NEUTRAL) && !IS_SET(ch->specials2.act, MOB_AGGRESSIVE_GOOD))) {
                                if (MOB_FLAGGED(ch, MOB_SWITCHING)) {
                                    if (!vict)
                                        vict = tmp_ch;
                                    else if (number(0, 1))
                                        vict = tmp_ch;
                                } else {
                                    vict = tmp_ch;
                                    found = TRUE;
                                }
                            }
                        }
                    }
                }
                if (vict) {
                    wtl.targ1.type = TARGET_CHAR;
                    wtl.targ1.ptr.ch = vict;
                    wtl.targ1.ch_num = vict->abs_number;
                    wtl.cmd = CMD_HIT;
                    // argument is unused here: wtl already carries the resolved
                    // target (act_offe.cpp's do_hit only parses argument when wtl
                    // has none), matching this file's mutable_arg("") idiom above.
                    rots::combat::issue_command(rots::combat::combat_command::hit, ch, mutable_arg(""), &wtl, 0, 0);
                    vict = 0;
                }
            } /* if aggressive */

            if ((IS_SET(ch->specials2.act, MOB_MEMORY) || IS_SET(ch->specials2.act, MOB_HUNTER) || (IS_AFFECTED(ch, AFF_HUNT))) && ch->specials.memory) {
                /* we assume pets do not hunt by themselves */
                if (MOB_FLAGGED(ch, MOB_PET) && (GET_POS(ch) == POSITION_FIGHTING))
                    rots::combat::issue_command(rots::combat::combat_command::flee, ch, mutable_arg(""), 0, 0, 0);

                /* checking memory */
                if (!IS_SET(ch->specials2.act, MOB_SENTINEL) && (GET_POS(ch) == POSITION_STANDING)) {
                    /* hunting the victim */
                }

                vict = 0;
                for (names = ch->specials.memory; names && !vict;
                     names = names->next_on_mob) {
                    if (names->enemy && char_exists(names->enemy_number) && (names->enemy->in_room == ch->in_room) && CAN_SEE(ch, names->enemy))
                        vict = names->enemy;
                }

                if (vict) {
                    if (ch->master == vict)
                        forget(ch, vict);
                    else {
                        if (GET_INT(ch) <= 6)
                            act("$n snarls and lunges at $N!", FALSE, ch, 0, vict, TO_ROOM);
                        else
                            act("$n grins evilly and attacks $N!", FALSE, ch, 0, vict, TO_ROOM);
                        char hit_target_name[MAX_STRING_LENGTH];
                        sscanf(vict->player.name, "%s", hit_target_name);
                        wtl.targ1.type = TARGET_CHAR;
                        wtl.targ1.ptr.ch = vict;
                        wtl.targ1.ch_num = vict->abs_number;
                        wtl.cmd = CMD_HIT;
                        rots::combat::issue_command(rots::combat::combat_command::hit, ch, hit_target_name, &wtl, 0, 0);
                    }
                } else if (IS_SET(ch->specials2.act, MOB_HUNTER) || IS_AFFECTED(ch, AFF_HUNT)) {
                    int modifier = 0;

                    if (IS_AFFECTED(ch, AFF_CONFUSE))
                        modifier = get_confuse_modifier(ch);

                    for (names = ch->specials.memory; names && !vict;
                         names = names->next_on_mob) {
                        if (names->enemy && char_exists(names->enemy_number) && (names->enemy->in_room != NOWHERE) && (number(0, 100) > modifier)) // confuse modifier...
                            tmp = find_first_step(ch->in_room, names->enemy->in_room);
                        else
                            tmp = BFS_NO_PATH;
                        if (tmp >= 0) { // found the way, moving there
                            rots::combat::issue_command(rots::combat::combat_command::move, ch, mutable_arg(""), 0, tmp + 1, 0);
                            break;
                        }
                    }
                }
            } /* mob memory */
        }
    } /* If IS_MOB(ch)  */
}

// Registers the real one_mobile_activity(ch) body above as
// combat_hooks.h's matching hook (behavior wave Task 1; census
// section 3). Called once from run_the_game()/gtest_main.cpp's
// main(), before boot_db() -- registrar wiring only, no call-site
// conversion yet: this file's own :61 self-call and limits.cpp's
// :1398 call stay direct/unconverted this task.
void register_one_mobile_activity_hook()
{
    rots::combat::set_one_mobile_activity_hook(one_mobile_activity);
}

// Mob-memory pool cluster (memory_rec_counter/memory_rec_pool/
// memory_rec_active globals + get_from_memory_rec_pool()/
// put_to_memory_rec_pool()/remember()/forget()/clear_memory()/
// update_memory_list()) relocated verbatim as a package to
// entity_lifecycle.cpp (combat-pilot wave Task 4a; pilot-census.md
// section 7.7/7.8 -- same self-contained free-list-pool shape as the
// universal_list_counter/pool_to_list() precedent already there).
// Declarations unchanged: forget()/remember() stay in handler.h;
// clear_memory()/update_memory_list()/memory_rec_counter's other
// callers keep their own local externs (handler.cpp, interpre.cpp,
// act_wiz.cpp) -- linkage doesn't care which TU defines the symbol.

// ACMD(do_stand); duplicate forward decl RETIRED (behavior wave Task 1,
// census section 5): identical to the declaration already at this
// file's own top (:39, still used by call sites earlier in this file,
// e.g. :204/:230/:251). do_sleep/do_rest/do_sit/do_wake ALSO RETIRED
// (behavior wave Task 2): enforce_position()'s own do_wake/do_sleep/
// do_rest/do_sit/do_stand calls below now dispatch through
// rots::combat::issue_command() like every other do_* call in this
// file, so none of the four is referenced by name here any more.

void enforce_position(struct char_data* ch, int new_pos)
{

    if (!IS_NPC(ch))
        return;

    if (GET_POS(ch) == new_pos)
        return;
    if (GET_POS(ch) == POSITION_FIGHTING)
        return;
    if (GET_POS(ch) <= POSITION_STUNNED)
        return;

    if ((GET_POS(ch) <= POSITION_SLEEPING) && (new_pos > POSITION_SLEEPING))
        rots::combat::issue_command(rots::combat::combat_command::wake, ch, mutable_arg(""), 0, 0, 0);

    switch (new_pos) {
    case POSITION_SLEEPING:
        rots::combat::issue_command(rots::combat::combat_command::sleep, ch, mutable_arg(""), 0, 0, 0);
        break;

    case POSITION_RESTING:
        rots::combat::issue_command(rots::combat::combat_command::rest, ch, mutable_arg(""), 0, 0, 0);
        break;

    case POSITION_SITTING:
        rots::combat::issue_command(rots::combat::combat_command::sit, ch, mutable_arg(""), 0, 0, 0);
        break;

    case POSITION_STANDING:
        rots::combat::issue_command(rots::combat::combat_command::stand, ch, mutable_arg(""), 0, 0, 0);
        break;
    }
}
