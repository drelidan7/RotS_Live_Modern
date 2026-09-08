/* ************************************************************************
 *   File: act.movement.c                                Part of CircleMUD *
 *  Usage: movement commands, door handling, & sleep/rest/etc state        *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#include <algorithm>
#include <format>
#include <stdio.h>
#include <string.h>
#include <string_view>
#include <utility>
#include <vector>

#include "char_utils.h"
#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "combat_hooks.h"
#include "protocol.h"
#include "script.h"
#include "spells.h"
#include "rots/core/character.h"
#include "rots/core/object.h"
#include "rots/core/room.h"
#include "rots/core/descriptor.h"
#include "rots/core/types.h"
#include "utils.h"

#include <assert.h>

typedef char* string;

extern struct room_data world;
extern int top_of_world;
extern struct char_data* character_list;
extern struct descriptor_data* descriptor_list;
extern struct index_data* obj_index;
extern int rev_dir[];
extern const std::string_view dirs[];
extern const std::string_view refer_dirs[];
extern int movement_loss[];
extern struct time_info_data time_info;
extern struct skill_data skills[];
extern int get_real_stealth(struct char_data* ch);

extern void raw_kill(char_data* ch, char_data* killer, int attacktype);

ACMD(do_look);
ACMD(do_open);
ACMD(do_close);
ACMD(do_dismount);
/* external functs */
void death_cry(struct char_data* ch);
extern struct char_data* waiting_list;
void do_power_of_arda(char_data* ch);

ACMD(do_look);

bool should_double_strength(char_data* character)
{
    char_data* master = NULL;
    if (utils::is_pc(*character)) {
        master = character;
    } else {
        if (MOB_FLAGGED(character, MOB_MOUNT)) {
            master = character->mount_data.rider;
        }
    }

    if (master) {
        return utils::get_specialization(*master) == game_types::PS_HeavyFighting;
    }

    return false;
}

int get_room_move_penalty(const char_data* character, int room_sector)
{
    int room_move_penalty = movement_loss[room_sector];

    if (utils::get_specialization(*character) == game_types::PS_Stealth && !character->mount_data.mount) {
        room_move_penalty = std::max(1, room_move_penalty / 2);
    }

    return room_move_penalty;
}

int room_move_cost(char_data* character, room_data* new_room)
{
    assert(character);
    assert(new_room);

    int character_strength = character->tmpabilities.str;
    if (should_double_strength(character)) {
        character_strength = character_strength * 2;
    }

    int move_cost = std::max(20 + IS_CARRYING_W(character) / character_strength / 10,
        70 + IS_CARRYING_W(character) / character_strength / 20);

    // Now can carry str*6 pounds without penalty, penalty becomes heavy at str*10.
    if (MOB_FLAGGED(character, MOB_MOUNT)) {
        move_cost = IS_CARRYING_W(character) / GET_STR(character) / 20; // Mounts have less str penalty
    }

    if (move_cost < 100) {
        if (MOB_FLAGGED(character, MOB_MOUNT)) {
            move_cost = std::max(75, 50 + move_cost / 2);
        } else {
            move_cost = 100;
        }
    }

    move_cost += utils::get_leg_encumbrance(*character) * 2;
    int room_sector = new_room->sector_type;
    int room_move_penalty = get_room_move_penalty(character, room_sector);

    if (!MOB_FLAGGED(character, MOB_MOUNT)) {
        move_cost = room_move_penalty * (move_cost + number(0, 99)) / 2;
    } else {
        move_cost = room_move_penalty * (move_cost + number(0, 99)) * 2 / 5;
    }

    if (IS_RIDING(character)) {
        move_cost = (move_cost / (120 + GET_RAW_KNOWLEDGE(character, SKILL_RIDE) * 2 + GET_RAW_KNOWLEDGE(character, SKILL_ANIMALS) / 2)) * 5 / 4;
    } else {
        move_cost = move_cost / 100;
    }

    // Rooms always cost at least one to move.
    return std::max(move_cost, 1);
}

//***********************************************************************
static int check_simple_move_impl(char_data* character, int direction, int* move_cost, int mode,
    const rots::combat::checked_movement* checked)
/* Assumes,
     1. That there is no master and no followers.
     2. That the direction exists.

     Returns :

     0 : success
     1 : intercepted by specials, or something like that
     2 : need a boat
     3 : not enough move points
     4 : mount exhausted // obsolete
     5 : mount would not move // obsolete
     6 : has no control over mount
     7 : riders can't go indoors
     8 : race guard mob is in the room

     Also returns movement cost in move_cost
  */
{
    int need_movement;
    struct room_data *room_to, *room_from;

    if (!character || direction < 0 || direction >= NUM_OF_DIRS || location_of(character) == NOWHERE || !move_cost) {
        return 1;
    }
    room_from = room_of(character);
    const auto* initial_exit = room_from->dir_option[direction];
    const rots::combat::checked_movement transition {
        GET_ABS_NUM(character), direction, location_of(character), initial_exit ? initial_exit->to_room : NOWHERE
    };
    if (!rots::combat::matches_checked_movement(character, transition)) {
        return 1;
    }
    if (mode != SCMD_MOVING) {
        const bool intercepted = special(character, direction + 1, mutable_arg(""), SPECIAL_COMMAND, 0);
        if (intercepted || !rots::combat::matches_checked_movement(character, transition)) {
            return 1;
        }
    }
    if (checked && !rots::combat::matches_checked_movement(character, *checked)) {
        return 1;
    }
    if ((GET_POS(character) < POSITION_FIGHTING) || (PLR_FLAGGED(character, PLR_WRITING)))
        return 1;

    if (!room_from->dir_option[direction])
        return 1;
    if (room_from->dir_option[direction]->to_room < 0)
        return 1;
    if (character->delay.wait_value > 0)
        return 1;

    room_to = room_by_id_total(room_from->dir_option[direction]->to_room);
    if (!room_to)
        return 1;

    if (!checked && call_trigger(ON_BEFORE_ENTER, room_to, character, 0) == FALSE) {
        return 1;
    }
    // A callback may move/extract the actor or retarget the accepted exit.
    // Its own effects stand, but the outer transition is no longer valid.
    if (!rots::combat::matches_checked_movement(character, transition)) {
        return 1;
    }
    if (IS_SET(EXIT(character, direction)->exit_info, EX_NOWALK))
        return 8;

    // look out for the same need_movement in do_simple_move and others...
    need_movement = (room_move_cost(character, room_from) + room_move_cost(character, room_to)) / 2;

    /// if bloodied, +2 mvs to walk
    if ((GET_HIT(character) < GET_MAX_HIT(character) / 4) || has_critical_stat_damage(character))
        need_movement += 2;

    // If "sundelayed", harder to move. now, affects with power of Arda
    if (EVIL_RACE(character))
        do_power_of_arda(character);

    if (IS_SHADOW(character))
        need_movement = 0;

    if (room_from->sector_type == SECT_WATER_NOSWIM || room_to->sector_type == SECT_WATER_NOSWIM) {
        /*
         *    room_from->sector_type == SECT_WATER_SWIM ||
         *    room_to->sector_type == SECT_WATER_SWIM) {
         */
        if (!can_swim(character) && !IS_RIDING(character)) {
            // can swim on mounts
            return 2;
        } else {
            int boat = 0;
            struct obj_data* tmpobj;
            int tmp;

            for (tmpobj = character->carrying; tmpobj && !boat; tmpobj = tmpobj->next_content) {
                if (tmpobj->obj_flags.type_flag == ITEM_BOAT)
                    boat = 1;
            }
            for (tmp = 0; tmp < MAX_WEAR && !boat; tmp++) {
                if (character->equipment[tmp])
                    if ((character->equipment[tmp])->obj_flags.type_flag == ITEM_BOAT)
                        boat = 1;
            }

            if (!boat) {
                int m;

                /*
                 * m will be in the range [0, 6].  Since the sector movement
                 * cost for SECT_WATER_SWIM and SECT_WATER_NOSWIM are 8 and 10,
                 * this means that a 36r with mastered swim can swim a NOSWIM
                 * room with 1 movement cost.
                 */
                m = GET_PROF_LEVEL(PROF_RANGER, character) + GET_SKILL(character, SKILL_SWIM);
                m /= 20;

                need_movement = MAX(1, need_movement - m);
            }
        }
    }

    *move_cost = need_movement;

    if (IS_NPC(character)) { // checking mob limitations on movement.
        if ((mode != SCMD_FOLLOW) && (mode != SCMD_MOUNT) && IS_SET(room_to->room_flags, NO_MOB) && !IS_AFFECTED(character, AFF_CHARM))
            return 5;

        if ((mode != SCMD_FOLLOW) && MOB_FLAGGED(character, MOB_STAY_ZONE) && !IS_AFFECTED(character, AFF_CHARM) && (room_from->zone != room_to->zone))
            return 5;
        if ((mode != SCMD_FOLLOW) && MOB_FLAGGED(character, MOB_STAY_TYPE) && !IS_AFFECTED(character, AFF_CHARM) && (room_from->sector_type != room_to->sector_type))
            return 5;
    }

    if (GET_MOVE(character) < need_movement)
        return 3;

    // okay, character can move now - checking his mount.
    if (IS_RIDING(character)) {
        if (!char_exists(character->mount_data.mount_number)) {
            character->mount_data.mount = 0;
            character->mount_data.next_rider = 0;
        }
        if (IS_SET(EXIT(character, direction)->exit_info, EX_NORIDE))
            return 7;
        if (IS_SET(room_by_id_total(room_of(character)->dir_option[direction]->to_room)->room_flags, INDOORS))
            return 7;
        if (IS_SET(room_by_id_total(room_of(character)->dir_option[direction]->to_room)->room_flags, NORIDE))
            return 7;
        else if ((mode != SCMD_CARRIED) && ((character->mount_data.mount)->mount_data.rider != character))
            return 6;
    }

    // Checking for race_guard mobs in the room the character wants to move to

    for (auto* tmpch : rots::entity::occupants(room_by_id_total(room_of(character)->dir_option[direction]->to_room)))
        if (IS_NPC(tmpch) && MOB_FLAGGED(tmpch, MOB_RACE_GUARD))
            if ((GET_RACE(character) != GET_RACE(tmpch)) && !IS_NPC(character))
                return 8;

    return 0;
}

int check_simple_move(char_data* character, int direction, int* move_cost, int mode)
{
    return check_simple_move_impl(character, direction, move_cost, mode, nullptr);
}

static void do_move_impl(char_data* character, char* argument, waiting_type*, int command, int subcommand,
    const rots::combat::checked_movement* checked);

static void move_after_validation_impl(char_data* character,
    const rots::combat::checked_movement& movement, int mode)
{
    char direction[16];
    snprintf(direction, sizeof(direction), "%.*s", static_cast<int>(dirs[movement.direction].size()),
        dirs[movement.direction].data());
    do_move_impl(character, direction, nullptr, movement.direction + 1, mode, &movement);
}

// Registers the real check_simple_move() body above as combat_hooks.h's
// check_simple_move hook (spell-family closure wave Task 1; sf-census.md
// section 4.3). Called once from run_the_game(), before boot_db() -- same
// convention as register_command_interpreter_hook() (interpre.h).
void register_check_simple_move_hook()
{
    rots::combat::set_check_simple_move_hook(check_simple_move);
    rots::combat::set_checked_move_hook(move_after_validation_impl);
}

/*=================================================================================
   set_blood_trail:
   Here we are setting the blood trail for the harad ability in rooms.
   ------------------------------Change Log---------------------------------------
   slyon: Sept 8, 2017 - Created
==================================================================================*/
void set_blood_trail(struct char_data* ch, int dir)
{
    int tmp;
    if ((utils::is_npc(*ch) || (utils::get_race(*ch) != RACE_GOD))) {
        tmp = number(0, NUM_OF_BLOOD_TRAILS - 1);
        if (utils::is_npc(*ch)) {
            room_of(ch)->bleed_track[tmp].char_number = ch->nr;
        } else {
            room_of(ch)->bleed_track[tmp].char_number = -GET_RACE(ch);
        }

        room_of(ch)->bleed_track[tmp].data = time_info.hours * 8 + dir;
        room_of(ch)->bleed_track[tmp].condition = 0;
    }
}

/*
 * moves the mount and everybody riding it..
 * performs special() on everybody except the primary rider
 * or the mount itself, depending on who is moving.
 * Returns 1 on success, 0 otherwise
 *
 * assumes the move is legal, e.g. the door is open.
 */
int perform_move_mount(struct char_data* ch, int dir)
{
    int was_in, new_room, is_death, move_cost, tmp, should_show;
    char buff[1000];
    char buff2[1000];
    void show_mount_to_char(struct char_data* mount, struct char_data* viewer,
        std::string_view singular_rider_text, std::string_view plural_rider_text, int color);

    if (!ch || dir < 0 || dir >= NUM_OF_DIRS || location_of(ch) == NOWHERE
        || !EXIT(ch, dir) || !ch->mount_data.rider) {
        return 0;
    }
    const rots::combat::checked_movement transition {
        GET_ABS_NUM(ch), dir, location_of(ch), EXIT(ch, dir)->to_room
    };
    if (!rots::combat::matches_checked_movement(ch, transition)) {
        return 0;
    }
    char_data* const primary_rider = ch->mount_data.rider;
    const int primary_number = ch->mount_data.rider_number;
    if (char_by_abs_number(primary_number) != primary_rider) {
        ch->mount_data.rider = 0;
        return 0;
    }

    new_room = EXIT(ch, dir)->to_room;
    was_in = location_of(ch);

    is_death = IS_SET(room_by_id_total(new_room)->room_flags, DEATH);

    // Snapshot identities, not list links that a rider's callback may free.
    std::vector<std::pair<int, char_data*>> riders;
    for (char_data* rider = primary_rider; rider; rider = rider->mount_data.next_rider) {
        const int rider_number = GET_ABS_NUM(rider);
        if (char_by_abs_number(rider_number) != rider
            || std::find(riders.begin(), riders.end(), std::pair { rider_number, rider }) != riders.end()) {
            return 0;
        }
        riders.emplace_back(rider_number, rider);
        if (rider->mount_data.next_rider
            && char_by_abs_number(rider->mount_data.next_rider_number) != rider->mount_data.next_rider) {
            return 0;
        }
    }
    special(primary_rider, dir + 1, mutable_arg(""), SPECIAL_COMMAND, 0);
    if (!rots::combat::matches_checked_movement(ch, transition)
        || char_by_abs_number(primary_number) != primary_rider
        || location_of(primary_rider) != was_in || ch->mount_data.rider != primary_rider
        || primary_rider->mount_data.mount != ch) {
        return 0;
    }

    for (const auto& [rider_number, original_rider] : riders) {
        if (rider_number == primary_number) {
            continue;
        }
        char_data* rider = char_by_abs_number(rider_number);
        if (rider != original_rider || rider->mount_data.mount != ch) {
            continue;
        }
        if (location_of(rider) != was_in) {
            stop_riding(rider);
            continue;
        }
        tmp = check_simple_move(rider, dir, &move_cost, SCMD_CARRIED);
        if (!rots::combat::matches_checked_movement(ch, transition)
            || char_by_abs_number(primary_number) != primary_rider
            || ch->mount_data.rider != primary_rider || location_of(primary_rider) != was_in
            || primary_rider->mount_data.mount != ch) {
            return 0;
        }
        if (char_by_abs_number(rider_number) != rider || rider->mount_data.mount != ch) {
            continue;
        }
        if (tmp || location_of(rider) != was_in) {
            if (tmp == 3)
                send_to_char("You are too exhausted!\n\r", ch);
            stop_riding(rider);
        } else {
            // if bloodied harder to move -Z
            if (GET_HIT(ch) < GET_MAX_HIT(ch) / 4)
                move_cost += 2;

            // If "sundelayed", harder to move. - power of Arda
            if (EVIL_RACE(ch))
                do_power_of_arda(ch);
            // if (GET_RACE(ch) == RACE_ORC) move_cost += number(2,3);
            // else move_cost += number(1,2);

            GET_MOVE(rider) -= move_cost;
        }
    }

    /* now forming and sending the "leave" message */

    strcpy(buff, std::format(" leaving {}, riding on ", dirs[dir]).c_str());
    strcpy(buff2, std::format(" leaving {}, riding on ", dirs[dir]).c_str());

    for (auto* tmpvict : rots::entity::occupants(room_by_id_total(was_in))) {

        if ((tmpvict == ch) || (tmpvict->mount_data.mount == ch))
            continue;
        if (GET_POS(tmpvict) > POSITION_SLEEPING)
            show_mount_to_char(ch, tmpvict, buff, buff2, FALSE);
    }

    /* moving all people involved */
    for (const auto& [rider_number, original_rider] : riders) {
        char_data* rider = char_by_abs_number(rider_number);
        if (rider != original_rider || rider->mount_data.mount != ch || location_of(rider) != was_in) {
            continue;
        }
        if (rider != primary_rider) {
            send_to_char(std::format("You are carried {} by {}.\n\r", dirs[dir], PERS(ch, rider, FALSE, FALSE)), rider);
        }
        char_from_room(rider);
        char_to_room(rider, new_room);
        if (is_death) {
            raw_kill(rider, NULL, 0);
            if (!rots::combat::matches_checked_movement(ch, transition)) {
                return 0;
            }
        }
    }
    /* Setting tracks in room */
    if ((IS_NPC(ch) || (GET_RACE(ch) != RACE_GOD)) && !(IS_AFFECTED(ch, AFF_FLYING))) {
        tmp = number(0, NUM_OF_TRACKS - 1);
        if (IS_NPC(ch))
            room_of(ch)->room_track[tmp].char_number = ch->nr;
        else
            room_of(ch)->room_track[tmp].char_number = -GET_RACE(ch);

        room_of(ch)->room_track[tmp].data = time_info.hours * 8 + dir;
        room_of(ch)->room_track[tmp].condition = 0;
    }

    if (utils::is_affected_by_spell(*ch, SKILL_MARK)) {
        set_blood_trail(ch, dir);
    }

    stop_hiding(ch, TRUE);
    char_from_room(ch);
    char_to_room(ch, new_room);

    if (is_death) {
        raw_kill(ch, NULL, 0);
        return 0;
    }
    do_look(ch, mutable_arg(""), 0, 0, SCMD_LOOK_BRIEF);

    for (const auto& [rider_number, original_rider] : riders) {
        char_data* rider = char_by_abs_number(rider_number);
        if (rider == original_rider && rider->mount_data.mount == ch && location_of(rider) == new_room) {
            do_look(rider, mutable_arg(""), 0, CMD_LOOK, SCMD_LOOK_BRIEF);
        }
    }
    /* now forming and sending the "enter" message */

    strcpy(buff, std::format(" entering from {}, riding on ", refer_dirs[rev_dir[dir]]).c_str());
    strcpy(buff2, std::format(" entering from {}, riding on ", refer_dirs[rev_dir[dir]]).c_str());

    for (auto* tmpvict : rots::entity::occupants(room_by_id_total(new_room))) {

        should_show = 1;

        if ((tmpvict == ch) || (tmpvict->mount_data.mount == ch))
            should_show = 0;
        if (ch->mount_data.rider && !PRF_FLAGGED(tmpvict, PRF_SPAM)) {
            if ((ch->mount_data.rider->master == tmpvict) || (tmpvict->master && (ch->mount_data.rider->master == tmpvict->master)))
                should_show = 0;
        }
        if (GET_POS(tmpvict) <= POSITION_SLEEPING)
            should_show = 0;

        if (should_show)
            show_mount_to_char(ch, tmpvict, buff, buff2, FALSE);
    }

    for (const auto& [rider_number, original_rider] : riders) {
        char_data* rider = char_by_abs_number(rider_number);
        if (rider != original_rider || rider->mount_data.mount != ch || location_of(rider) != new_room) {
            continue;
        }
        special(rider, rev_dir[dir] + 1, mutable_arg(""), SPECIAL_ENTER, 0);
        if (char_by_abs_number(transition.actor_number) != ch || location_of(ch) != new_room) {
            return 0;
        }
        if (char_by_abs_number(rider_number) == rider && location_of(rider) != NOWHERE) {
            call_trigger(ON_ENTER, (void*)room_of(rider), (void*)rider, 0);
            if (char_by_abs_number(transition.actor_number) != ch || location_of(ch) != new_room) {
                return 0;
            }
        }
    }
    const bool intercepted = special(ch, rev_dir[dir] + 1, mutable_arg(""), SPECIAL_ENTER, 0);
    if (intercepted || char_by_abs_number(transition.actor_number) != ch || location_of(ch) == NOWHERE) {
        return 0;
    }
    call_trigger(ON_ENTER, (void*)room_of(ch), (void*)ch, 0);

    return 1;
}

// parse_container_for_stay_zone()/prohibit_item_stay_zone_move() relocated
// verbatim to fight.cpp (spell-family closure wave Task 1; sf-census.md
// section 4.2: RELOCATE-CLEAN -- deps obj_to_room()/obj_from_char()/
// obj_from_obj() (containment.cpp/L2), unequip_char() (equipment.cpp/L2),
// act()/send_to_char() (output seam) all resolve downward once mage.cpp
// promotes. One documented deviation from strict byte-for-byte text: the
// two `strcpy(buf, "$n drops $p."); act(buf, ...)` pairs inline to a
// direct `act("$n drops $p.", ...)` call -- the retired global scratch
// buffer (db.h's extern char buf[MAX_STRING_LENGTH]) only ever held this
// same compile-time-constant template with no per-call substitution, so
// passing the literal directly is behavior-identical, not merely
// local-composition (there is no per-call value to compose). See
// fight.cpp's own copy of these two functions for the exact retrofit.
// Declaration unchanged (no shared header; mage.cpp's own local extern is
// unaffected; this file's own internal call site below needs one, added
// at the top of this file).
extern void prohibit_item_stay_zone_move(char_data* ch, int room);

/*
 * Reduces the movement cost of rooms based on character race and sector type.
 */
int racial_movement_reduction(const int room_type, const int race, const int movement_cost)
{
    // No love for third side or Olog-Hais
    if (race == RACE_HARADRIM || race == RACE_MAGUS || race == RACE_OLOGHAI) {
        return movement_cost;
    }

    // Dwarves be in mountains
    if (room_type == SECT_MOUNTAIN && race == RACE_DWARF) {
        return movement_cost / 2;
    }

    // Forests for Elves and Bears of course
    if ((room_type == SECT_DENSE_FOREST || room_type == SECT_FOREST) && (race == RACE_WOOD || race == RACE_BEORNING)) {
        return movement_cost / 2;
    }

    // Orcs and Uruk-Hais are swamp rats
    if (room_type == SECT_SWAMP && (race == RACE_URUK || race == RACE_ORC)) {
        return movement_cost / 2;
    }

    // Hobbits love them holes in hills
    if (room_type == SECT_HILLS && race == RACE_HOBBIT) {
        return movement_cost / 2;
    }

    // Humans riding their horses in the fields.
    if (room_type == SECT_FIELD && race == RACE_HUMAN) {
        return movement_cost / 2;
    }

    return movement_cost;
}

bool is_exit_valid(const room_direction_data room_direction)
{
    auto to_room = room_direction.to_room;
    auto exit_info = room_direction.exit_info;

    if (!to_room || to_room == NOWHERE) {
        return false;
    }

    if (exit_info == NOWHERE) {
        return false;
    }

    if (IS_SET(exit_info, EX_ISBROKEN)) {
        return false;
    }

    if (IS_SET(exit_info, EX_NOWALK)) {
        return false;
    }

    if (IS_SET(exit_info, EX_ISHIDDEN)) {
        return false;
    }

    return true;
}

// msdp_room_update() renamed to msdp_room_update_impl (spell-family closure
// wave Task 1; sf-census.md section 4.2): output_seam.h now owns the plain
// `msdp_room_update` global symbol (mirroring close_socket_impl's own
// takeover), registered by comm.cpp's register_game_output_sinks() to
// forward here. TASK-015 validates the supplied actor before resolving its room.
void msdp_room_update_impl(char_data* character)
{
    if (!character || utils::is_npc(*character)) {
        return;
    }

    // A player without a descriptor (linkdead) has nothing to update -- and
    // dereferencing character->desc below would crash. Reachable via spell_summon
    // targeting a linkdead player (TASK-025's body test pins this).
    if (!character->desc) {
        return;
    }

    if (!character->desc->pProtocol) {
        return;
    }

    const int actor_location = location_of(character);
    if (actor_location < 0 || actor_location > top_of_world) {
        return;
    }

    room_data* const actor_room = room_of(character);
    MSDPSetString(character->desc, eMSDP_ROOM_NAME, actor_room->name);
    MSDPSetNumber(character->desc, eMSDP_ROOM_VNUM, actor_room->number);

    std::string msdp_room = { };
    msdp_room += (char)MSDP_VAR;
    msdp_room += "VNUM";
    msdp_room += (char)MSDP_VAL;
    msdp_room += std::to_string(actor_room->number);
    msdp_room += (char)MSDP_VAR;
    msdp_room += "NAME";
    msdp_room += (char)MSDP_VAL;
    msdp_room += MSDPSanitizeValue(actor_room->name);
    msdp_room += (char)MSDP_VAR;
    msdp_room += "EXITS";
    msdp_room += (char)MSDP_VAL;
    msdp_room += (char)MSDP_ARRAY_OPEN;
    std::string exits_names = { };
    const std::string direction[NUM_OF_DIRS] = { "n", "e", "s", "w", "u", "d" };

    for (int exits = 0; exits < NUM_OF_DIRS; exits++) {
        if (actor_room->dir_option[exits] == nullptr) {
            continue;
        }

        const room_direction_data room_direction = *actor_room->dir_option[exits];

        if (room_direction.to_room >= 0 && room_direction.to_room <= top_of_world
            && is_exit_valid(room_direction)) {
            msdp_room += (char)MSDP_VAL;
            msdp_room += std::to_string(room_by_id_total(room_direction.to_room)->number);
            exits_names += (char)MSDP_VAL;
            exits_names += direction[exits];
        }
    }
    msdp_room += (char)MSDP_ARRAY_CLOSE;
    msdp_room += (char)MSDP_VAR;
    msdp_room += "TERRAIN";
    msdp_room += (char)MSDP_VAL;

    extern const std::string_view sector_types[];
    msdp_room += MSDPSanitizeValue(sector_types[actor_room->sector_type]);

    // Room exits need to be sent first before anything else
    MSDPSetArray(character->desc, eMSDP_ROOM_EXITS, exits_names);
    MSDPFlush(character->desc, eMSDP_ROOM_EXITS);
    MSDPSetTable(character->desc, eMSDP_ROOM, msdp_room);

    MSDPUpdate(character->desc);
}

static void do_move_impl(char_data* character, char* argument, waiting_type*, int command, int subcommand,
    const rots::combat::checked_movement* checked)
{
    int was_in, res_flag, to_room, tmp, need_move, tmp_move;
    char is_death, is_fol;
    std::vector<std::pair<int, char_data*>> followers;
    waiting_type tmpwtl;
    int mounts;

    if (!character || location_of(character) == NOWHERE || command < 1 || command > NUM_OF_DIRS) {
        return;
    }
    const int actor_number = GET_ABS_NUM(character);
    if (checked && !rots::combat::matches_checked_movement(character, *checked)) {
        return;
    }
    if (IS_AFFECTED(character, AFF_HAZE) && number(1, 4) == 1) {
        send_to_char("You feel dizzy, and move randomly.\n\r", character);
        command = number(1, NUM_OF_DIRS);
    }
    --command;
    if (checked && command != checked->direction) {
        checked = nullptr;
    }

    if ((character->delay.wait_value > 0) && (character->delay.priority <= 30)) {
        send_to_char("You could not concentrate anymore.\n\r", character);
        abort_delay(character);
    }

    for (const follow_type* follower = character->followers; follower; follower = follower->next) {
        followers.emplace_back(follower->fol_number, follower->follower);
    }
    is_fol = !followers.empty();

    if (IS_RIDDEN(character)) {
        perform_move_mount(character, command);
        return;
    }

    if (!room_of(character)->dir_option[command]) {
        send_to_char("You cannot go that way.\n\r", character);
        return;
    } else if (room_of(character)->dir_option[command]->to_room == NOWHERE) {
        send_to_char("You cannot go that way.\n\r", character);
        return;
    } else { /* Direction is possible */
        if (IS_NPC(character) && (subcommand == SCMD_MOVING) && IS_SET(EXIT(character, command)->exit_info, EX_ISDOOR | EX_CLOSED) && !IS_SET(EXIT(character, command)->exit_info, EX_ISHIDDEN | EX_LOCKED)) {
            tmpwtl.cmd = CMD_OPEN;
            tmpwtl.targ1.type = TARGET_DIR;
            tmpwtl.targ1.ch_num = command;
            tmpwtl.targ2.type = TARGET_NONE;
            do_open(character, mutable_arg(""), &tmpwtl, CMD_OPEN, 0);
        }

        if (!CAN_GO(character, command)) {
            if (IS_SET(EXIT(character, command)->exit_info, EX_ISHIDDEN) && !PRF_FLAGGED(character, PRF_HOLYLIGHT)) {
                send_to_char("You cannot go that way.\n\r", character);
                return;
            } else if (EXIT(character, command)->keyword) {
                if (IS_SHADOW(character))
                    send_to_char(std::format("You cannot pass through the {}.\n\r", fname(EXIT(character, command)->keyword)), character);
                else
                    send_to_char(std::format("The {} seems to be closed.\n\r", fname(EXIT(character, command)->keyword)), character);
                return;
            } else {
                send_to_char("It seems to be closed.\n\r", character);
                return;
            }
        } else if (EXIT(character, command)->to_room == NOWHERE) {
            send_to_char("You cannot go that way.\n\r", character);
            return;
        }
        if (IS_AFFECTED(character, AFF_CHARM) && (character->master) && (location_of(character) == location_of(character->master)) && (subcommand != SCMD_FOLLOW && subcommand != SCMD_FLEE)) {
            send_to_char("The thought of leaving your master makes you weep.\n\r", character);
            act("$n bursts into tears.", FALSE, character, 0, 0, TO_ROOM);
            return;
        }

        // Exit does exist, trying to move there

        to_room = EXIT(character, command)->to_room;
        is_death = IS_SET(room_by_id_total(to_room)->room_flags, DEATH);
        was_in = location_of(character);

        const bool different_zone = room_by_id_total(was_in)->zone != room_by_id_total(to_room)->zone;
        const rots::combat::checked_movement transition { actor_number, command, was_in, to_room };
        if (!rots::combat::matches_checked_movement(character, transition)) {
            return;
        }

        if (!IS_RIDING(character)) {
            res_flag = check_simple_move_impl(character, command, &need_move, subcommand, checked);
            if (!rots::combat::matches_checked_movement(character, transition)) {
                return;
            }

            if (subcommand == SCMD_FOLLOW) {
                if (res_flag != 0) {
                    act("ACK! $n could not follow, you lost $m!", FALSE, character, 0, character->master,
                        TO_VICT);
                    act("ACK! You could not follow $M!", FALSE, character, 0, character->master, TO_CHAR);
                } else
                    act("You follow $N.\n\r", FALSE, character, 0, character->master, TO_CHAR);
            }

            switch (res_flag) {
            case 0:
                break;
            case 1:
                return;
            case 2:
                send_to_char("You need to swim better or find a boat to go there.\n\r", character);
                return;
            case 3:
                send_to_char("You are too exhausted to move.\n\r", character);
                return;
            case 4:
                return;
            case 5:
                return;
            case 6:
                send_to_char("You do not control your mount.\n\r", character);
                return;
            case 7:
                send_to_char("Can not go there mounted.\n\r", character);
                return;
            case 8:
                send_to_char("You are prevented from entering there.\n\r", character);
                return;
            case 9:
                send_to_char("You cannot go that way.\n\r", character);
                return;
            }

            // At this point, check for common orc "followers" to move before their
            // master does, just for group randomness.

            if (is_fol) {
                for (const auto& [follower_number, original_follower] : followers) {
                    char_data* follower = char_by_abs_number(follower_number);
                    if (!follower || follower != original_follower || follower->master != character) {
                        continue;
                    }
                    if ((was_in == location_of(follower)) && (GET_POS(follower) >= POSITION_STANDING) && (IS_NPC(follower) && MOB_FLAGGED(follower, MOB_ORC_FRIEND) && MOB_FLAGGED(follower, MOB_PET)) && (number(1, 100) > 50)) {
                        // act("$n moves ahead of you.", FALSE, follower, 0, character, TO_VICT);
                        memset((char*)&tmpwtl, 0, sizeof(waiting_type));
                        tmpwtl.cmd = command + 1;
                        tmpwtl.subcmd = SCMD_FOLLOW;
                        command_interpreter(follower, argument, &tmpwtl);
                        if (!rots::combat::matches_checked_movement(character, transition)) {
                            return;
                        }
                    }
                }
            }

            if (!IS_AFFECTED(character, AFF_SNEAK) || (subcommand == SCMD_FLEE) || number(0, 125) > GET_SKILL(character, SKILL_SNEAK) + get_real_stealth(character)) {
                strcpy(buf2, std::format(" leaves {}.", dirs[command]).c_str());
                for (auto* tmpvict : rots::entity::occupants(room_of(character))) {
                    if ((character == tmpvict) || !CAN_SEE(tmpvict, character) || ((character->master == tmpvict) && IS_NPC(character) && MOB_FLAGGED(character, MOB_ORC_FRIEND)))
                        continue;
                    show_char_to_char(character, tmpvict, 0, buf2);
                }
            } else if (IS_AFFECTED(character, AFF_SNEAK)) {
                snuck_out(character);
                need_move *= 1.50;
            }

            const auto room_type = room_of(character)->sector_type;
            const auto race = character->player.race;

            need_move = racial_movement_reduction(room_type, race, need_move);

            // Here setting his tracks...

            if ((IS_NPC(character) || (GET_RACE(character) != RACE_GOD)) && !IS_SHADOW(character) && !IS_AFFECTED(character, AFF_FLYING)) { // &&
                //!(world[character->in_room].sector_type == SECT_WATER_NOSWIM) &&
                //!(world[character->in_room].sector_type == SECT_WATER_SWIM) ) now hunt will work in water

                if ((subcommand == SCMD_STALK) && (GET_KNOWLEDGE(character, SKILL_STALK) > number(0, 119))) {

                    send_to_char("You have found sure foothold.\n\r", character);
                    tmp = -1;
                } else
                    tmp = number(0, NUM_OF_TRACKS - 1);

                if (tmp >= 0) {
                    if (IS_NPC(character))
                        room_of(character)->room_track[tmp].char_number = character->nr;
                    else
                        room_of(character)->room_track[tmp].char_number = -GET_RACE(character);

                    room_of(character)->room_track[tmp].data = time_info.hours * 8 + command;
                    room_of(character)->room_track[tmp].condition = 0;
                }
            }

            if (utils::is_affected_by_spell(*character, SKILL_MARK)) {
                set_blood_trail(character, command);
            }

            // Check for item_stay_zone in players inventory and equipment.
            if (different_zone) {
                prohibit_item_stay_zone_move(character, was_in);
            }

            char_from_room(character);
            char_to_room(character, to_room);
            do_look(character, mutable_arg("\0"), 0, 0, 0);
            GET_MOVE(character) -= need_move;
            if (!IS_AFFECTED(character, AFF_SNEAK) || (subcommand == SCMD_FLEE) || number(0, 100) > GET_SKILL(character, SKILL_SNEAK) + get_real_stealth(character) - 25) {
                strcpy(buf2, std::format(" enters from {}.", refer_dirs[rev_dir[command]]).c_str());
                for (auto* tmpvict : rots::entity::occupants(room_of(character))) {
                    if ((tmpvict == character) || !CAN_SEE(tmpvict, character))
                        continue;
                    if (!PRF_FLAGGED(character, PRF_SPAM) && (subcommand == SCMD_FOLLOW) && character->master && ((tmpvict->master == character->master) || (tmpvict == character->master)))
                        continue;
                    show_char_to_char(character, tmpvict, 0, buf2);
                }
            } else if (IS_AFFECTED(character, AFF_SNEAK))
                snuck_in(character);

            if (!character->spec_busy) {
                special(character, rev_dir[command] + 1, mutable_arg(""), SPECIAL_ENTER, 0);
            }

            if (char_by_abs_number(actor_number) != character || location_of(character) == NOWHERE) {
                return;
            }
            call_trigger(ON_ENTER, (void*)room_of(character), (void*)character, 0);
            if (char_by_abs_number(actor_number) != character || location_of(character) != to_room) {
                return;
            }
            if (is_death) {
                raw_kill(character, NULL, 0);
                return;
            }
        } else { // riding...
            char_data* const mount = character->mount_data.mount;
            const int mount_number = character->mount_data.mount_number;
            if (char_by_abs_number(mount_number) != mount || mount->mount_data.rider != character) {
                send_to_char("You do not control your mount.\n\r", character);
                return;
            }
            res_flag = check_simple_move_impl(character, command, &need_move, subcommand, checked);
            if (!rots::combat::matches_checked_movement(character, transition)) {
                return;
            }

            if (subcommand == SCMD_FOLLOW) {
                if (res_flag != 0) {
                    act("ACK! $n could not follow, you lost $m!", TRUE, character, 0, character->master, TO_VICT);
                    act("ACK! You could not follow $M!", TRUE, character, 0, character->master, TO_CHAR);
                } else
                    act("You follow $N.\n\r", FALSE, character, 0, character->master, TO_CHAR);
            }
            switch (res_flag) {
            case 0:
                break;
            case 1:
                return;
            case 2:
                send_to_char("Your mount cannot swim.\n\r", character);
                return;
            case 3:
                send_to_char("You are too exhausted to ride.\n\r", character);
                return;
            case 4:
                send_to_char("Your mount is too exhausted to move.\n\r", character);
                return;
            case 5:
                send_to_char("Your mount would not go there.\n\r", character);
                return;
            case 6:
                send_to_char("You do not control your mount.\n\r", character);
                return;
            case 7:
                send_to_char("Can not go there mounted.\n\r", character);
                return;
            case 8:
                send_to_char("You cannot go that way.\n\r", character);
                return;
            }
            // GET_MOVE(character) -= need_move;       // This belongs below.
            if (!IS_RIDING(character) || character->mount_data.mount != mount
                || char_by_abs_number(mount_number) != mount || mount->mount_data.rider != character) {
                return;
            }
            res_flag = check_simple_move(mount, command, &tmp_move, SCMD_MOUNT);
            if (!rots::combat::matches_checked_movement(character, transition)
                || char_by_abs_number(mount_number) != mount || !IS_RIDING(character)
                || character->mount_data.mount != mount || mount->mount_data.rider != character) {
                return;
            }

            if (subcommand == SCMD_FOLLOW) {
                if (res_flag != 0) {
                    act("ACK! $n could not follow riding, you lost $m!", TRUE, character, 0, character->master,
                        TO_VICT);
                    act("ACK! You could not follow $M riding!", TRUE, character, 0, character->master, TO_CHAR);
                }
                // 	else
                // 	  act("You follow $N.\n\r", FALSE, character, 0, character->master, TO_CHAR);
            }
            switch (res_flag) {
            case 0:
                break;
            case 1:
                return;
            case 2:
                send_to_char("Your mount cannot swim.\n\r", character);
                return;
            case 3:
                send_to_char("Your mount is too exhausted to move.\n\r", character);
                return;
            case 5:
                send_to_char("Your mount would not go there.\n\r", character);
                return;
            case 6:
                send_to_char("Your mount does not control its mount (\?\?).\n\r", character);
                return;
            case 7:
                send_to_char("Can not go there mounted.\n\r", character);
                return;
            case 8:
                send_to_char("You cannot go that way.\n\r", character);
                return;
            }
            GET_MOVE(character) -= need_move;

            // At this point, check for common orc "followers" to move before their
            // master does, just for group randomness.

            if (is_fol) {
                for (const auto& [follower_number, original_follower] : followers) {
                    char_data* follower = char_by_abs_number(follower_number);
                    if (!follower || follower != original_follower || follower->master != character) {
                        continue;
                    }
                    if ((was_in == location_of(follower)) && (GET_POS(follower) >= POSITION_STANDING) && (IS_NPC(follower) && MOB_FLAGGED(follower, MOB_ORC_FRIEND) && MOB_FLAGGED(follower, MOB_PET)) && (number(1, 100) > 50)) {
                        // act("$n moves ahead of you.", FALSE, follower, 0, character, TO_VICT);
                        memset((char*)&tmpwtl, 0, sizeof(waiting_type));
                        tmpwtl.cmd = command + 1;
                        tmpwtl.subcmd = SCMD_FOLLOW;
                        command_interpreter(follower, argument, &tmpwtl);
                        if (!rots::combat::matches_checked_movement(character, transition)
                            || char_by_abs_number(mount_number) != mount || !IS_RIDING(character)
                            || character->mount_data.mount != mount || mount->mount_data.rider != character
                            || location_of(mount) != was_in) {
                            return;
                        }
                    }
                }
            }

            if (char_by_abs_number(mount_number) != mount || !IS_RIDING(character)
                || character->mount_data.mount != mount || mount->mount_data.rider != character
                || location_of(mount) != was_in) {
                return;
            }
            GET_MOVE(mount) -= tmp_move;

            strcpy(buf2, std::format("$N has forced you {}.\n\r", dirs[command]).c_str());
            act(buf2, FALSE, mount, 0, character, TO_CHAR);

            res_flag = perform_move_mount(mount, command);
        }

        if (char_by_abs_number(actor_number) != character || location_of(character) != to_room) {
            return;
        }
        msdp_room_update_impl(character);

        mounts = 0;
        if (IS_RIDING(character))
            mounts++;
        if (is_fol) { /* If success move followers */
            for (const auto& [follower_number, original_follower] : followers) {
                char_data* follower = char_by_abs_number(follower_number);
                if (!follower || follower != original_follower || follower->master != character) {
                    continue;
                }
                if ((was_in == location_of(follower)) && (GET_POS(follower) >= POSITION_STANDING)) {
                    //	  act("You follow $N.\n\r", FALSE, follower, 0, character, TO_CHAR);

                    memset((char*)&tmpwtl, 0, sizeof(waiting_type));
                    tmpwtl.cmd = command + 1;
                    tmpwtl.subcmd = SCMD_FOLLOW;
                    //	  do_move(follower, argument, &tmpwtl, command + 1, SCMD_FOLLOW);
                    // Can not lead too many mounts:
                    if (IS_NPC(follower) && (MOB_FLAGGED(follower, MOB_MOUNT))) {
                        mounts++;
                        if (mounts <= 2 || number(1, 20) != 20) {
                            command_interpreter(follower, argument, &tmpwtl);
                        } else {
                            send_to_char("One of your mounts has fallen behind!\r\n", character);
                        }
                    } else {
                        command_interpreter(follower, argument, &tmpwtl);
                    }
                    if (char_by_abs_number(actor_number) != character || location_of(character) != to_room) {
                        return;
                    }
                }
            }
        }
    }
}

ACMD(do_move)
{
    do_move_impl(ch, argument, wtl, cmd, subcmd, nullptr);
}

// find_door() relocated verbatim to fight.cpp (spell-family closure wave
// Task 1; sf-census.md section 4.3: RELOCATE-CLEAN -- deps
// (search_block()/L0, isname_nullable()/L2, send_to_char()/L1 output
// seam, EXIT() macro's room resolve -- a raw world[] read until LS-2 T2/L3-world peer) all resolve downward
// or intra-lib. Declaration unchanged (no shared header; every caller
// uses a local extern -- this file's own four internal call sites below
// now need one too, added at the top of this file).
extern int find_door(struct char_data* ch, char* type, char* dir);

ACMD(do_open)
{
    int door, other_room, d1, d2;
    char type[MAX_INPUT_LENGTH], dir[MAX_INPUT_LENGTH];
    struct room_direction_data* back;
    struct obj_data* obj;
    struct char_data* victim;

    if (IS_SHADOW(ch)) {
        send_to_char("You are too insubstantial to do that.\n\r", ch);
        return;
    }

    half_chop(argument, buf1, buf2);

    back = 0;
    obj = 0;
    door = -1;

    if ((wtl && (wtl->targ1.type == TARGET_OBJ)) && !(*buf2)) {
        obj = wtl->targ1.ptr.obj;
    } else if (wtl && (wtl->targ1.type == TARGET_DIR)) {
        d1 = wtl->targ1.ch_num;
        if (!EXIT(ch, d1)) {
            send_to_char("Can't open anything in that direction.\n\r", ch);
            return;
        }
        if (wtl->targ2.type == TARGET_DIR) {
            d2 = wtl->targ2.ch_num;
            if (!EXIT(ch, d2)) {
                send_to_char("Can't open anything in that direction.\n\r", ch);
                return;
            }
            sscanf(EXIT(ch, d1)->keyword, "%s", type);
            if (str_cmp_nullable(EXIT(ch, d2)->keyword, type)) {
                send_to_char(std::format("No {} in that direction.\n\r", static_cast<const char*>(type)), ch);
                return;
            }
            door = d2;
        } else
            door = d1;
    } else {

        if (wtl && (wtl->targ1.type == TARGET_TEXT)) {
            argument = wtl->targ1.ptr.text->text;
        }

        argument_interpreter(argument, type, dir);

        if (!*type)
            send_to_char("Open what?\n\r", ch);
        else if (generic_find(argument, FIND_OBJ_INV | FIND_OBJ_ROOM, ch, &victim, &obj)) {

            // this is an object
        } else
            door = find_door(ch, type, dir);
    }

    /* perhaps it is a door */

    if (door >= 0) {
        if (IS_SET(EXIT(ch, door)->exit_info, EX_ISBROKEN))
            send_to_char("It is broken.\n\r", ch);
        else if (!IS_SET(EXIT(ch, door)->exit_info, EX_ISDOOR))
            send_to_char("That's impossible, I'm afraid.\n\r", ch);
        else if (!IS_SET(EXIT(ch, door)->exit_info, EX_CLOSED))
            send_to_char("It's already open!\n\r", ch);
        else if (IS_SET(EXIT(ch, door)->exit_info, EX_LOCKED))
            send_to_char("It seems to be locked.\n\r", ch);
        else if (IS_SET(EXIT(ch, door)->exit_info, EX_LEVER))
            send_to_char("It will not budge.\n\r", ch);

        else {
            REMOVE_BIT(EXIT(ch, door)->exit_info, EX_CLOSED);
            if (EXIT(ch, door)->keyword)
                act("$n opens the $F.", FALSE, ch, 0, EXIT(ch, door)->keyword, TO_ROOM);
            else
                act("$n opens the door.", FALSE, ch, 0, 0, TO_ROOM);
            send_to_char("Ok.\n\r", ch);
            /* now for opening the OTHER side of the door! */
            if ((other_room = EXIT(ch, door)->to_room) != NOWHERE)
                if ((back = room_by_id_total(other_room)->dir_option[rev_dir[door]]))
                    if (back->to_room == location_of(ch)) {
                        REMOVE_BIT(back->exit_info, EX_CLOSED);
                        if (back->keyword) {
                            send_to_room(std::format("The {} is opened from the other side.\n\r", fname(back->keyword)), EXIT(ch, door)->to_room);
                        } else
                            send_to_room("The door is opened from the other side.\n\r",
                                EXIT(ch, door)->to_room);
                    }
        }
    }
    if (obj) {
        if (obj->obj_flags.type_flag != ITEM_CONTAINER)
            send_to_char("That's not a container.\n\r", ch);
        else if (!IS_SET(obj->obj_flags.value[1], CONT_CLOSED))
            send_to_char("But it's already open!\n\r", ch);
        else if (!IS_SET(obj->obj_flags.value[1], CONT_CLOSEABLE))
            send_to_char("You can't do that.\n\r", ch);
        else if (IS_SET(obj->obj_flags.value[1], CONT_LOCKED))
            send_to_char("It seems to be locked.\n\r", ch);
        else {
            REMOVE_BIT(obj->obj_flags.value[1], CONT_CLOSED);
            send_to_char("Ok.\n\r", ch);
            act("$n opens $p.", FALSE, ch, obj, 0, TO_ROOM);
        }
    }
}

ACMD(do_close)
{
    int door, other_room, d1, d2;
    char type[MAX_INPUT_LENGTH], dir[MAX_INPUT_LENGTH];
    struct room_direction_data* back;
    struct obj_data* obj;
    struct char_data* victim;

    if (IS_SHADOW(ch)) {
        send_to_char("You are too insubstantial to do that.\n\r", ch);
        return;
    }

    back = 0;
    obj = 0;
    door = -1;

    half_chop(argument, buf1, buf2);

    if ((wtl && (wtl->targ1.type == TARGET_OBJ)) && !(*buf2)) {
        obj = wtl->targ1.ptr.obj;
    }

    else if (wtl && (wtl->targ1.type == TARGET_DIR)) {
        d1 = wtl->targ1.ch_num;
        if (!EXIT(ch, d1)) {
            send_to_char("Can't close anything in that direction.\n\r", ch);
            return;
        }
        if (wtl->targ2.type == TARGET_DIR) {
            d2 = wtl->targ2.ch_num;
            if (!EXIT(ch, d2)) {
                send_to_char("Can't close anything in that direction.\n\r", ch);
                return;
            }
            sscanf(EXIT(ch, d1)->keyword, "%s", type);
            if (str_cmp_nullable(EXIT(ch, d2)->keyword, type)) {
                send_to_char(std::format("No {} in that direction.\n\r", static_cast<const char*>(type)), ch);
                return;
            }
            door = d2;
        } else
            door = d1;
    } else {

        if (wtl && (wtl->targ1.type == TARGET_TEXT)) {
            argument = wtl->targ1.ptr.text->text;
        }

        argument_interpreter(argument, type, dir);

        if (!*type)
            send_to_char("Close what?\n\r", ch);
        else if (generic_find(argument, FIND_OBJ_INV | FIND_OBJ_ROOM, ch, &victim, &obj)) {

            /* this is an object */
        } else
            door = find_door(ch, type, dir);
    }
    /* Or a door */

    if (door >= 0) {
        if (IS_SET(EXIT(ch, door)->exit_info, EX_ISBROKEN))
            send_to_char("It is broken.\n\r", ch);
        else if (!IS_SET(EXIT(ch, door)->exit_info, EX_ISDOOR))
            send_to_char("There is nothing to close.\n\r", ch);
        else if (IS_SET(EXIT(ch, door)->exit_info, EX_CLOSED))
            send_to_char("It's already closed!\n\r", ch);
        else if (IS_SET(EXIT(ch, door)->exit_info, EX_LEVER))
            send_to_char("It will not budge.\n\r", ch);

        else {
            SET_BIT(EXIT(ch, door)->exit_info, EX_CLOSED);
            if (EXIT(ch, door)->keyword)
                act("$n closes the $F.", 0, ch, 0, EXIT(ch, door)->keyword, TO_ROOM);
            else
                act("$n closes the door.", FALSE, ch, 0, 0, TO_ROOM);
            send_to_char("Ok.\n\r", ch);
            /* now for closing the other side, too */
            if ((other_room = EXIT(ch, door)->to_room) != NOWHERE)
                if ((back = room_by_id_total(other_room)->dir_option[rev_dir[door]]))
                    if ((back->to_room == location_of(ch)) && IS_SET(back->exit_info, EX_ISDOOR)) {
                        SET_BIT(back->exit_info, EX_CLOSED);
                        if (back->keyword) {
                            send_to_room(std::format("The {} closes quietly.\n\r", back->keyword), EXIT(ch, door)->to_room);
                        } else
                            send_to_room("The door closes quietly.\n\r", EXIT(ch, door)->to_room);
                    }
        }
    }
    if (obj) {
        if (obj->obj_flags.type_flag != ITEM_CONTAINER)
            send_to_char("That's not a container.\n\r", ch);
        else if (IS_SET(obj->obj_flags.value[1], CONT_CLOSED))
            send_to_char("But it's already closed!\n\r", ch);
        else if (!IS_SET(obj->obj_flags.value[1], CONT_CLOSEABLE))
            send_to_char("That's impossible.\n\r", ch);
        else {
            SET_BIT(obj->obj_flags.value[1], CONT_CLOSED);
            send_to_char("Ok.\n\r", ch);
            act("$n closes $p.", FALSE, ch, obj, 0, TO_ROOM);
        }
    }
}

bool is_key(obj_data* item) { return item->obj_flags.type_flag == ITEM_KEY; }

/* returns NULL if the character does not have the key (or it is broken),
                   a pointer to the key if the character does have the key  */
obj_data* has_key(char_data* character, int key)
{
    for (obj_data* item = character->carrying; item; item = item->next_content)
        if (obj_index[item->item_number].virt == key)
            if (!(IS_SET(item->obj_flags.extra_flags, ITEM_BROKEN)))
                if (is_key(item))
                    return (item);

    if (character->equipment[HOLD])
        if (obj_index[character->equipment[HOLD]->item_number].virt == key)
            if (!(IS_SET(character->equipment[HOLD]->obj_flags.extra_flags, ITEM_BROKEN)))
                if (is_key(character->equipment[HOLD]))
                    return (character->equipment[HOLD]);

    return NULL;
}

void check_break_key(struct obj_data* obj, struct char_data* ch)
{
    if (!(IS_SET(obj->obj_flags.extra_flags, ITEM_BREAKABLE)) || IS_SET(obj->obj_flags.extra_flags, ITEM_BROKEN))
        return;
    act("Unfortunately, $p breaks as $n uses it!", FALSE, ch, obj, 0, TO_ROOM);
    act("Unfortunately, $p breaks as you use it!", FALSE, ch, obj, 0, TO_CHAR);
    SET_BIT(obj->obj_flags.extra_flags, ITEM_BROKEN);
}

ACMD(do_lock)
{
    int door, other_room;
    char type[MAX_INPUT_LENGTH], dir[MAX_INPUT_LENGTH];
    struct room_direction_data* back;
    struct obj_data* obj;
    struct char_data* victim;

    if (IS_SHADOW(ch)) {
        send_to_char("You are too insubstantial to do that.\n\r", ch);
        return;
    }

    argument_interpreter(argument, type, dir);

    if (!*type)
        send_to_char("Lock what?\n\r", ch);
    else if (generic_find(argument, FIND_OBJ_INV | FIND_OBJ_ROOM, ch, &victim, &obj))

        /* this is an object */

        if (obj->obj_flags.type_flag != ITEM_CONTAINER)
            send_to_char("That's not a container.\n\r", ch);
        else if (!IS_SET(obj->obj_flags.value[1], CONT_CLOSED))
            send_to_char("Maybe you should close it first...\n\r", ch);
        else if (obj->obj_flags.value[2] < 0)
            send_to_char("That thing can't be locked.\n\r", ch);
        else if (!has_key(ch, obj->obj_flags.value[2]))
            send_to_char("You don't seem to have the proper key.\n\r", ch);
        else if (IS_SET(obj->obj_flags.value[1], CONT_LOCKED))
            send_to_char("It is locked already.\n\r", ch);
        else {
            SET_BIT(obj->obj_flags.value[1], CONT_LOCKED);
            send_to_char("*Cluck*\n\r", ch);
            act("$n locks $p - 'cluck', it says.", FALSE, ch, obj, 0, TO_ROOM);
        }
    else if ((door = find_door(ch, type, dir)) >= 0) {

        /* a door, perhaps */

        if (IS_SET(EXIT(ch, door)->exit_info, EX_ISBROKEN))
            send_to_char("It is broken.\n\r", ch);
        else if (!IS_SET(EXIT(ch, door)->exit_info, EX_ISDOOR))
            send_to_char("That's absurd.\n\r", ch);
        else if (!IS_SET(EXIT(ch, door)->exit_info, EX_CLOSED))
            send_to_char("You have to close it first, I'm afraid.\n\r", ch);
        else if (EXIT(ch, door)->key < 0)
            send_to_char("There does not seem to be any keyholes.\n\r", ch);
        else if (!has_key(ch, EXIT(ch, door)->key))
            send_to_char("You don't have the proper key.\n\r", ch);
        else if (IS_SET(EXIT(ch, door)->exit_info, EX_LOCKED))
            send_to_char("It's already locked!\n\r", ch);
        else {
            SET_BIT(EXIT(ch, door)->exit_info, EX_LOCKED);
            if (EXIT(ch, door)->keyword)
                act("$n locks the $F.", 0, ch, 0, EXIT(ch, door)->keyword, TO_ROOM);
            else
                act("$n locks the door.", FALSE, ch, 0, 0, TO_ROOM);
            send_to_char("*Click*\n\r", ch);
            /* now for locking the other side, too */
            if ((other_room = EXIT(ch, door)->to_room) != NOWHERE)
                if ((back = room_by_id_total(other_room)->dir_option[rev_dir[door]]))
                    if ((back->to_room == location_of(ch)) && IS_SET(back->exit_info, EX_ISDOOR))
                        SET_BIT(back->exit_info, EX_LOCKED);
        }
    }
}

ACMD(do_unlock)
{
    int door, other_room;
    char type[MAX_INPUT_LENGTH], dir[MAX_INPUT_LENGTH];
    struct room_direction_data* back;
    struct obj_data* obj;
    struct char_data* victim;

    if (IS_SHADOW(ch)) {
        send_to_char("You are too insubstantial to do that.\n\r", ch);
        return;
    }

    argument_interpreter(argument, type, dir);

    if (!*type)
        send_to_char("Unlock what?\n\r", ch);
    else if (generic_find(argument, FIND_OBJ_INV | FIND_OBJ_ROOM, ch, &victim, &obj))

        /* this is an object */

        if (obj->obj_flags.type_flag != ITEM_CONTAINER)
            send_to_char("That's not a container.\n\r", ch);
        else if (!IS_SET(obj->obj_flags.value[1], CONT_CLOSED))
            send_to_char("Silly - it ain't even closed!\n\r", ch);
        else if (obj->obj_flags.value[2] < 0)
            send_to_char("Odd - you can't seem to find a keyhole.\n\r", ch);
        else if (!has_key(ch, obj->obj_flags.value[2]))
            send_to_char("You don't seem to have the proper key.\n\r", ch);
        else if (!IS_SET(obj->obj_flags.value[1], CONT_LOCKED))
            send_to_char("Oh.. it wasn't locked, after all.\n\r", ch);
        else {
            REMOVE_BIT(obj->obj_flags.value[1], CONT_LOCKED);
            send_to_char("*Click*\n\r", ch);
            act("$n unlocks $p.", FALSE, ch, obj, 0, TO_ROOM);
            check_break_key(has_key(ch, obj->obj_flags.value[2]), ch);
        }
    else if ((door = find_door(ch, type, dir)) >= 0) {

        /* it is a door */

        if (IS_SET(EXIT(ch, door)->exit_info, EX_ISBROKEN))
            send_to_char("It is broken.\n\r", ch);
        else if (!IS_SET(EXIT(ch, door)->exit_info, EX_ISDOOR))
            send_to_char("That's absurd.\n\r", ch);
        else if (!IS_SET(EXIT(ch, door)->exit_info, EX_CLOSED))
            send_to_char("Heck.. it ain't even closed!\n\r", ch);
        else if (EXIT(ch, door)->key < 0)
            send_to_char("You can't seem to spot any keyholes.\n\r", ch);
        else if (!has_key(ch, EXIT(ch, door)->key))
            send_to_char("You do not have the proper key for that.\n\r", ch);
        else if (!IS_SET(EXIT(ch, door)->exit_info, EX_LOCKED))
            send_to_char("It's already unlocked, it seems.\n\r", ch);
        else {
            REMOVE_BIT(EXIT(ch, door)->exit_info, EX_LOCKED);
            if (EXIT(ch, door)->keyword)
                act("$n unlocks the $F.", 0, ch, 0, EXIT(ch, door)->keyword, TO_ROOM);
            else
                act("$n unlocks the door.", FALSE, ch, 0, 0, TO_ROOM);
            send_to_char("*click*\n\r", ch);
            check_break_key(has_key(ch, EXIT(ch, door)->key), ch);
            /* now for unlocking the other side, too */
            if ((other_room = EXIT(ch, door)->to_room) != NOWHERE)
                if ((back = room_by_id_total(other_room)->dir_option[rev_dir[door]]))
                    if (back->to_room == location_of(ch))
                        REMOVE_BIT(back->exit_info, EX_LOCKED);
        }
    }
}

ACMD(do_move);
ACMD(do_enter)
{
    int door;

    //   ACMD(do_move);

    one_argument(argument, buf);

    if (*buf) /* an argument was supplied, search for door keyword */ {
        for (door = 0; door < NUM_OF_DIRS; door++)
            if (EXIT(ch, door))
                if (EXIT(ch, door)->keyword)
                    if (!str_cmp_nullable(EXIT(ch, door)->keyword, buf)) {
                        do_move(ch, mutable_arg(""), wtl, ++door, 0);
                        return;
                    }
        send_to_char(std::format("There is no {} here.\n\r", static_cast<const char*>(buf)), ch);
    } else if (IS_SET(room_of(ch)->room_flags, INDOORS))
        send_to_char("You are already indoors.\n\r", ch);
    else {
        /* try to locate an entrance */
        for (door = 0; door < NUM_OF_DIRS; door++)
            if (EXIT(ch, door))
                if (EXIT(ch, door)->to_room != NOWHERE)
                    if (!IS_SET(EXIT(ch, door)->exit_info, EX_CLOSED) && IS_SET(room_by_id_total(EXIT(ch, door)->to_room)->room_flags, INDOORS)) {
                        do_move(ch, mutable_arg(""), wtl, ++door, 0);
                        return;
                    }
        send_to_char("You can't seem to find anything to enter.\n\r", ch);
    }
}

ACMD(do_leave)
{
    int door;

    extern long secs_to_unretire(struct char_data*);
    extern int r_mortal_start_room[];

    /* retired characters can 'leave' the retirement home */
    if (IS_SET(PLR_FLAGS(ch), PLR_RETIRED)) {
        if (secs_to_unretire(ch) <= 0) {
            send_to_char("You leave the retirement home, hungry for new adventures.\r\n", ch);
            act("A wisp of slate grey smoke settles around $n.\r\n"
                "The smoke recesses to the bar, and $n is nowhere to be found.",
                FALSE, ch, 0, 0, TO_ROOM);
            unretire(ch);
            char_from_room(ch);
            char_to_room(ch, r_mortal_start_room[GET_RACE(ch)]);
            do_look(ch, mutable_arg(""), 0, 0, 0);
        } else
            send_to_char("You cannot leave the retirement home yet.\r\n", ch);

        return;
    }

    if (!IS_SET(room_of(ch)->room_flags, INDOORS))
        send_to_char("You are outside.. where do you want to go?\n\r", ch);
    else {
        for (door = 0; door < NUM_OF_DIRS; door++)
            if (EXIT(ch, door))
                if (EXIT(ch, door)->to_room != NOWHERE)
                    if (!IS_SET(EXIT(ch, door)->exit_info, EX_CLOSED) && !IS_SET(room_by_id_total(EXIT(ch, door)->to_room)->room_flags, INDOORS)) {
                        do_move(ch, mutable_arg(""), wtl, ++door, 0);
                        return;
                    }
        send_to_char("I see no obvious exits to the outside.\n\r", ch);
    }
}

ACMD(do_stand)
{
    switch (GET_POS(ch)) {
    case POSITION_STANDING:
        act("You are already standing.", FALSE, ch, 0, 0, TO_CHAR);
        break;
    case POSITION_SITTING:
        act("You stand up.", FALSE, ch, 0, 0, TO_CHAR);
        act("$n clambers to $s feet.", TRUE, ch, 0, 0, TO_ROOM);
        if (ch->specials.fighting)
            GET_POS(ch) = POSITION_FIGHTING;
        else
            GET_POS(ch) = POSITION_STANDING;
        break;
    case POSITION_RESTING:
        act("You stop resting, and stand up.", FALSE, ch, 0, 0, TO_CHAR);
        act("$n stops resting, and clambers on $s feet.", TRUE, ch, 0, 0, TO_ROOM);
        if (ch->specials.fighting)
            GET_POS(ch) = POSITION_FIGHTING;
        else
            GET_POS(ch) = POSITION_STANDING;
        break;
    case POSITION_SLEEPING:
        //      act("You have to wake up first!", FALSE, ch, 0, 0, TO_CHAR);
        if (ch->specials.fighting)
            GET_POS(ch) = POSITION_FIGHTING;
        else
            GET_POS(ch) = POSITION_STANDING;
        act("You wake, and stand up.", FALSE, ch, 0, 0, TO_CHAR);
        act("$n awakes, and clambers on $s feet.", TRUE, ch, 0, 0, TO_ROOM);
        break;
    case POSITION_FIGHTING:
        act("Do you not consider fighting as standing?", FALSE, ch, 0, 0, TO_CHAR);
        break;
    case POSITION_STUNNED:
        return;
    case POSITION_INCAP:
        return;
    case POSITION_DEAD:
        return;
    default:
        act("You stop floating around, and put your feet on the ground.", FALSE, ch, 0, 0, TO_CHAR);
        act("$n stops floating around, and puts $s feet on the ground.", TRUE, ch, 0, 0, TO_ROOM);
        break;
    }
}

ACMD(do_sit)
{
    switch (GET_POS(ch)) {
    case POSITION_STANDING:
        act("You sit down.", FALSE, ch, 0, 0, TO_CHAR);
        act("$n sits down.", FALSE, ch, 0, 0, TO_ROOM);
        GET_POS(ch) = POSITION_SITTING;
        break;
    case POSITION_SITTING:
        send_to_char("You're sitting already.\n\r", ch);
        break;

    case POSITION_RESTING:
        act("You stop resting, and sit up.", FALSE, ch, 0, 0, TO_CHAR);
        act("$n stops resting.", TRUE, ch, 0, 0, TO_ROOM);
        GET_POS(ch) = POSITION_SITTING;
        break;
    case POSITION_SLEEPING:
        act("You have to wake up first.", FALSE, ch, 0, 0, TO_CHAR);
        break;
    case POSITION_FIGHTING:
        act("Sit down while fighting? are you MAD?", FALSE, ch, 0, 0, TO_CHAR);
        break;
    default:
        act("You stop floating around, and sit down.", FALSE, ch, 0, 0, TO_CHAR);
        act("$n stops floating around, and sits down.", TRUE, ch, 0, 0, TO_ROOM);
        GET_POS(ch) = POSITION_SITTING;
        break;
    }
}

ACMD(do_rest)
{
    switch (GET_POS(ch)) {
    case POSITION_STANDING:
        act("You sit down and rest your tired bones.", FALSE, ch, 0, 0, TO_CHAR);
        act("$n sits down and rests.", TRUE, ch, 0, 0, TO_ROOM);
        GET_POS(ch) = POSITION_RESTING;
        break;
    case POSITION_SITTING:
        act("You rest your tired bones.", FALSE, ch, 0, 0, TO_CHAR);
        act("$n rests.", TRUE, ch, 0, 0, TO_ROOM);
        GET_POS(ch) = POSITION_RESTING;
        break;
    case POSITION_RESTING:
        act("You are already resting.", FALSE, ch, 0, 0, TO_CHAR);
        break;
    case POSITION_SLEEPING:
        act("You have to wake up first.", FALSE, ch, 0, 0, TO_CHAR);
        break;
    case POSITION_FIGHTING:
        act("Rest while fighting?  Are you MAD?", FALSE, ch, 0, 0, TO_CHAR);
        break;
    default:
        act("You stop floating around, and stop to rest your tired bones.", FALSE, ch, 0, 0,
            TO_CHAR);
        act("$n stops floating around, and rests.", FALSE, ch, 0, 0, TO_ROOM);
        GET_POS(ch) = POSITION_SITTING;
        break;
    }
}

ACMD(do_sleep)
{

    if (IS_RIDING(ch))
        do_dismount(ch, mutable_arg(""), 0, 0, 0);

    switch (GET_POS(ch)) {
    case POSITION_STANDING:
    case POSITION_SITTING:
    case POSITION_RESTING:
        send_to_char("You go to sleep.\n\r", ch);
        act("$n lies down and falls asleep.", TRUE, ch, 0, 0, TO_ROOM);
        GET_POS(ch) = POSITION_SLEEPING;
        break;
    case POSITION_SLEEPING:
        send_to_char("You are already sound asleep.\n\r", ch);
        break;
    case POSITION_FIGHTING:
        send_to_char("Sleep while fighting?  Are you MAD?\n\r", ch);
        break;
    default:
        act("You stop floating around, and lie down to sleep.", FALSE, ch, 0, 0, TO_CHAR);
        act("$n stops floating around, and lie down to sleep.", TRUE, ch, 0, 0, TO_ROOM);
        GET_POS(ch) = POSITION_SLEEPING;
        break;
    }
}

ACMD(do_wake)
{
    struct char_data* tmp_char;

    one_argument(argument, arg);
    if (*arg) {
        if (GET_POS(ch) == POSITION_SLEEPING) {
            act("You can't wake people up if you are asleep yourself!", FALSE, ch, 0, 0, TO_CHAR);
        } else {
            tmp_char = get_char_room_vis(ch, arg);
            if (tmp_char) {
                if (tmp_char == ch) {
                    act("If you want to wake yourself up, just type 'wake'", FALSE, ch, 0, 0,
                        TO_CHAR);
                } else {
                    if (GET_POS(tmp_char) == POSITION_SLEEPING) {
                        //  if (IS_AFFECTED(tmp_char, AFF_SLEEP)) {
                        //     act("You can not wake $M up!", FALSE, ch, 0, tmp_char, TO_CHAR);
                        //  } else {
                        act("You wake $M up.", FALSE, ch, 0, tmp_char, TO_CHAR);
                        GET_POS(tmp_char) = POSITION_SITTING;
                        act("You are awakened by $n.", FALSE, ch, 0, tmp_char, TO_VICT);

                    } else {
                        act("$N is already awake.", FALSE, ch, 0, tmp_char, TO_CHAR);
                    }
                }
            } else {
                send_to_char("You do not see that person here.\n\r", ch);
            }
        }
    } else {
        //   if (IS_AFFECTED(ch, AFF_SLEEP)) {
        //	 send_to_char("You can't wake up!\n\r", ch);
        //   } else {
        if (GET_POS(ch) > POSITION_SLEEPING)
            send_to_char("You are already awake...\n\r", ch);
        else {
            send_to_char("You wake, and sit up.\n\r", ch);
            act("$n awakens.", TRUE, ch, 0, 0, TO_ROOM);
            GET_POS(ch) = POSITION_SITTING;
        }
    }
}

ACMD(do_lose)
{
    follow_type* tmpfol;
    char_data* tmpch;

    one_argument(argument, buf);

    if (!*buf) {
        send_to_char("Whom do you want to lose?\n\r", ch);
        return;
    }
    if (!str_cmp_nullable(buf, "all"))
        for (tmpfol = ch->followers; tmpfol; tmpfol = ch->followers)
            stop_follower(tmpfol->follower, FOLLOW_MOVE);
    else {
        tmpch = get_char_vis(ch, buf);
        if (!tmpch || other_side(ch, tmpch)) {
            send_to_char("Nobody by that name.\n\r", ch);
            return;
        }
        if (tmpch->master == ch)
            stop_follower(tmpch, FOLLOW_MOVE);
        else {
            send_to_char(std::format("But, {} is not following you!\n\r", HSSH(tmpch)), ch);
        }
    }
}

ACMD(do_follow)
{
    if (ch == NULL)
        return;

    char_data* leader = NULL;

    one_argument(argument, buf);

    if (*buf) {
        if (!str_cmp_nullable(buf, "self")) {
            leader = ch;
        } else {
            leader = get_char_room_vis(ch, buf);
        }
    } else {
        send_to_char("Whom do you wish to follow?\n\r", ch);
        return;
    }

    if (leader == NULL) {
        send_to_char("I see no person by that name here!\n\r", ch);
        return;
    }

    if (other_side(ch, leader) || (IS_NPC(leader) && MOB_FLAGGED(leader, MOB_MOUNT) && IS_AGGR_TO(leader, ch))) {
        send_to_char("It doesn't want you to follow it.\n\r", ch);
        return;
    }

    if (IS_SHADOW(ch)) {
        send_to_char("You cannot follow anything whilst being a shadow.\n\r", ch);
        return;
    }

    if (ch->master == leader) {
        send_to_char(std::format("You are already following {}.\n\r", HMHR(leader)), ch);
        return;
    }

    if (ch->master && (MOB_FLAGGED(ch, MOB_ORC_FRIEND) || MOB_FLAGGED(ch, MOB_PET))) {
        act("But you only feel like following $N!", FALSE, ch, 0, ch->master, TO_CHAR);
        act("$n only feels like following you!", FALSE, ch, 0, ch->master, TO_VICT);
    } else { /* Not Charmed follow person */
        if (leader == ch) {
            if (!ch->master) {
                send_to_char("You are already following yourself.\n\r", ch);
                return;
            }
            stop_follower(ch, FOLLOW_MOVE);
        } else {
            if (circle_follow(ch, leader, FOLLOW_MOVE)) {
                act("Sorry, but following in loops is not allowed.", FALSE, ch, 0, 0, TO_CHAR);
                return;
            }

            if (ch->master) {
                stop_follower(ch, FOLLOW_MOVE);
            }

            add_follower(ch, leader, FOLLOW_MOVE);
        }
    }
}

ACMD(do_refollow)
{
    if (ch->master == NULL) {
        send_to_char("But, you aren't following anyone!\n\r", ch);
        return;
    }

    char_data* leader = ch->master;

    stop_follower(ch, FOLLOW_REFOL);
    add_follower(ch, leader, FOLLOW_MOVE);
}

ACMD(do_lead)
{ // Added by Loman.
    char_data* potential_mount;
    char_data* mount;

    while (*argument && (*argument <= ' '))
        argument++;
    mount = 0;

    if (!*argument) { // default mount
        do_dismount(ch, mutable_arg(""), 0, 0, 0);
    } else {
        potential_mount = get_char_room_vis(ch, argument);
        if (!potential_mount) {
            send_to_char("There is nobody by that name.\n\r", ch);
            return;
        }
        if ((IS_NPC(potential_mount) && !IS_SET(potential_mount->specials2.act, MOB_MOUNT)) || !IS_NPC(potential_mount)) {
            send_to_char("You can not lead this.\n\r", ch);
            return;
        }
        if (IS_AGGR_TO(potential_mount, ch)) {
            act("$N doesn't want you to lead $M.", FALSE, ch, 0, potential_mount, TO_CHAR);
            return;
        }
        if (potential_mount->mount_data.mount) {
            act("$N is not in a position for you to lead $M.", FALSE, ch, 0, potential_mount,
                TO_CHAR);
            return;
        }
        mount = potential_mount;
    }

    if (!mount)
        return;

    if (mount == ch) {
        send_to_char("You tried to lead yourself, but failed.\n\r", ch);
        return;
    }

    if (IS_RIDING(ch) && ch->mount_data.mount == mount) {
        do_dismount(ch, mutable_arg(""), 0, 0, 0);
        return;
    }

    if (GET_POSITION(ch) == POSITION_FIGHTING) {
        send_to_char("You can't do this while fighting.\n\r", ch);
        return;
    }

    if (mount->master) {
        if (mount->master == ch)
            act("$N is already following you.", FALSE, ch, 0, mount, TO_CHAR);
        else
            act("$N is already following someone.", FALSE, ch, 0, mount, TO_CHAR);
        return;
    }

    if (IS_RIDDEN(mount)) {
        act("$N is already being ridden!", FALSE, ch, 0, mount, TO_CHAR);
        return;
    }

    if (affected_by_spell(mount, SKILL_CALM)) {
        if (!is_strong_enough_to_tame(ch, mount, false)) {
            send_to_char("Your skill with animals is insufficient to lead that beast.\r\n", ch);
            return;
        }
    }

    act("You grab $N and start leading $m.", FALSE, ch, 0, mount, TO_CHAR);
    act("$n grabs $N and starts leading $m.", FALSE, ch, 0, mount, TO_ROOM);
    add_follower(mount, ch, FOLLOW_MOVE);
}

ACMD(do_pull)
{

    obj_data* obj;
    int room_num, exit_num, next_room_num;
    int would_open; // 1 if open, 0 is close.
    room_data* room;
    room_data* next_room;

    if (IS_SHADOW(ch)) {
        send_to_char("You are too insubstantial to do that.\n\r", ch);
        return;
    }

    if (!wtl || (wtl->targ1.type != TARGET_OBJ)) {
        send_to_char("You can't pull this!\n\r", ch);
        return;
    }

    obj = wtl->targ1.ptr.obj;

    if (obj->in_room != location_of(ch)) { // LS1-ALLOW: obj-location
        send_to_char("It is not here.\n\r", ch);
        return;
    }

    if (!call_trigger(ON_PULL, obj, ch, 0))
        return;

    if (obj->obj_flags.type_flag != ITEM_LEVER) {
        if (CAN_WEAR(obj, ITEM_TAKE)) {
            act("$n drags $P around, for all to see.", TRUE, ch, 0, obj, TO_ROOM);
            act("You drag $P around, showing it off.", TRUE, ch, 0, obj, TO_CHAR);
        } else {
            act("$n tries to pull at $P, but cannot shift it.", TRUE, ch, 0, obj, TO_ROOM);
            act("You try to pull at $P, but it would not budge.", TRUE, ch, 0, obj, TO_CHAR);
        }
        return;
    }
    room_num = real_room(obj->obj_flags.value[0]);
    exit_num = obj->obj_flags.value[1];
    if (exit_num >= NUM_OF_DIRS)
        exit_num = -1;

    if (room_num >= 0)
        room = room_by_id_total(room_num);
    else
        room = 0;

    if (!room || (exit_num < 0) || !room->dir_option[exit_num] || !room->dir_option[exit_num]->keyword) {
        act("$P seems to be broken.", FALSE, ch, 0, obj, TO_CHAR);
        return;
    }
    act("You pull at $P.", FALSE, ch, 0, obj, TO_CHAR);
    act("$n pulls at $P.", FALSE, ch, 0, obj, TO_ROOM);

    if (IS_SET(room->dir_option[exit_num]->exit_info, EX_CLOSED)) {
        REMOVE_BIT(room->dir_option[exit_num]->exit_info, EX_CLOSED);
        send_to_room(std::format("The {} opens slowly.\n\r", room->dir_option[exit_num]->keyword), room_num);

        would_open = 1;

        if (location_of(ch) != room_num) {
            send_to_room("You hear low rumbling in the distance.\n\r", location_of(ch));
        }
    } else {
        SET_BIT(room->dir_option[exit_num]->exit_info, EX_CLOSED);
        send_to_room(std::format("The {} closes slowly.\n\r", room->dir_option[exit_num]->keyword), room_num);

        would_open = 0;

        if (location_of(ch) != room_num) {
            send_to_room("You hear low rumbling in the distance.\n\r", location_of(ch));
        }
    }

    //*** now opening the other side of the door

    next_room_num = room->dir_option[exit_num]->to_room;

    if (next_room_num >= 0)
        next_room = room_by_id_total(next_room_num);
    else
        next_room = 0;

    exit_num = rev_dir[exit_num];

    if (!next_room || (exit_num < 0) || !next_room->dir_option[exit_num] || !next_room->dir_option[exit_num]->keyword) {

        return;
    }

    if (next_room->dir_option[exit_num]->to_room != room_num)
        return;

    if (IS_SET(next_room->dir_option[exit_num]->exit_info, EX_CLOSED) || would_open) {
        REMOVE_BIT(next_room->dir_option[exit_num]->exit_info, EX_CLOSED);
        send_to_room(std::format("The {} opens slowly.\n\r", next_room->dir_option[exit_num]->keyword), next_room_num);

    } else {
        SET_BIT(next_room->dir_option[exit_num]->exit_info, EX_CLOSED);
        send_to_room(std::format("The {} closes slowly.\n\r", next_room->dir_option[exit_num]->keyword), next_room_num);
    }
}
