/* ************************************************************************
 *   File: utility.c                                     Part of CircleMUD *
 *  Usage: various internal functions of a utility nature                  *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

/**************************************************************************
 * ROTS Documentation                                                      *
 *                                                                         *
 *                                                                         *
 * Universal list                                                          *
 *   A simple linked list structure which can hold pointers to char_data   *
 *   room_data or obj_data.  The number of universal_list structures are   *
 *   counted with universal_list_counter, and the number actively used in  *
 *   lists is held in used_in_universal_list.  Pool_to_list will add a new *
 *   or existing but unused universal_list structure into a linked list.   *
 **************************************************************************/

#include "platdef.h"
#include "fp_policy.h"

#if defined PREDEF_PLATFORM_LINUX
#include <arpa/telnet.h>
#endif

#include <assert.h>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctype.h>
#include <format>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "color.h"
#include "text_view.h"
#include "comm.h"
#include "platform_compat.h"
#include "rots_net.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "rots_rng.h"
#include "spells.h"
#include "rots/core/character.h"
#include "rots/core/object.h"
#include "rots/core/room.h"
#include "rots/core/descriptor.h"
#include "rots/core/tables.h"
#include "rots/core/types.h"
#include "rots/platform/log.h"
#include "utils.h"
#include "warrior_spec_handlers.h"

#include "char_utils.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <deque>

extern struct time_data time_info;
extern struct room_data world;
// character_list extern REMOVED (whole-branch review sweep,
// placement-seam wave): this file's only user, get_followers_level(),
// relocated to char_utils.cpp (placement-seam Task 5) -- this file has
// had no remaining reference since.
extern struct char_data* mob_proto;
extern int max_race_str[];
extern struct char_data* waiting_list;
int get_power_of_arda(char_data* ch);

static void check_container_proto(struct obj_data* obj, struct char_data* ch);

// vmudlog() always broadcasts at rots::log::kVmudlogBroadcastLevel (moved to
// rots_log.cpp, Task 2 of the logging-seam rewire); this keeps that
// platform-layer literal pinned to LEVEL_GOD without the platform header
// including rots/core/types.h just for one constant (see log.h's comment on
// the declaration).
static_assert(rots::log::kVmudlogBroadcastLevel == LEVEL_GOD,
    "platform vmudlog level diverged from LEVEL_GOD");

// dice() (relocated to rots_util.cpp, world-seed Task 1) logs at
// rots::log::kDiceUnderflowLogLevel when size < 1; keep that platform-layer
// literal pinned to LEVEL_IMMORT without rots/platform/log.h including
// rots/core/types.h just for one constant (see log.h's comment on the
// declaration).
static_assert(rots::log::kDiceUnderflowLogLevel == LEVEL_IMMORT,
    "platform dice() underflow log level diverged from LEVEL_IMMORT");

/*
 * Adds data to the char_data structure specifying that
 * `ch' has been retired (the PLR_RETIRED bit), sets the
 * time at which `ch' retired (specials1.retiredon), and
 * appends a retirement exploit to `ch's exploits.
 */
void retire(struct char_data* ch)
{
    SET_BIT(PLR_FLAGS(ch), PLR_RETIRED);
    ch->specials2.retiredon = time(0);
    add_exploit_record(EXPLOIT_RETIRED, ch, 0, "");
}

/*
 * Unset `ch's retired flag, add an exploit record showing
 * that `ch' was unretired, and reset `ch's retiredon time
 * to zero.
 */
void unretire(struct char_data* ch)
{
    REMOVE_BIT(PLR_FLAGS(ch), PLR_RETIRED);
    add_exploit_record(EXPLOIT_ACHIEVEMENT, ch, 0, "Unretired");
    ch->specials2.retiredon = 0;
}

// unaccent() relocated to db_players.cpp (persist-split PS Task 4,
// controller-adjudicated relocation): a pure char-range table lookup, no
// comm/world/char_data dependency at all. Declaration unchanged (utils.h);
// comm.cpp's two call sites are unaffected, now resolving down into
// rots_persist.

// do_squareroot() relocated to char_utils_combat.cpp (placement-seam Task 5;
// census verdict MOVE-OTHER-L2, moving together with its sole caller
// get_weapon_damage()): pure square_root[] table lookup, no world/output
// dependency. No declaring header (file-local, as it was here).

// get_current_time_phase() relocated to entity_lifecycle.cpp (entity-seed
// Task 5), alongside int pulse's definition (formerly comm.cpp); declaration
// unchanged in utils.h.

// default_exit_width[] table relocated to environment_utils.cpp
// (placement-seam Task 5), alongside its sole reader get_exit_width() (see
// that function's own relocation comment below): pure per-sector-type
// default-width data, no declaring header (file-local, as it was here).

// get_race_weight() relocated to entity_lifecycle.cpp (entity-seed Task 5);
// declaration unchanged in utils.h.

// get_race_height() relocated to entity_lifecycle.cpp (entity-seed Task 5);
// declaration unchanged in utils.h.

// get_race_perception() relocated to entity_lifecycle.cpp (db-split Task 4b);
// declaration unchanged in utils.h.

// get_naked_perception() relocated to entity_lifecycle.cpp (db-split Task 4b);
// declaration unchanged in utils.h.

// get_naked_willpower() relocated to entity_lifecycle.cpp (db-split Task 4b);
// declaration unchanged in utils.h.

// get_exit_width() relocated to environment_utils.cpp (placement-seam
// Task 5; census verdict MOVE-OTHER-L2), taking its default_exit_width[]
// table with it (see above): entity-pure room_data*-field lookup, no
// world[] access. No declaring header (file-local, as it was here).

// string_to_new_value() relocated to rots_util.cpp (placement-seam
// Task 5; census verdict MOVE-OTHER(platform)): pure text/int parsing, no
// game-type dependency. Declaration unchanged in utils.h.

// get_bow_weapon_damage() relocated to char_utils_combat.cpp
// (placement-seam Task 5; census verdict MOVE-OTHER-L2): entity-pure
// obj_data-method arithmetic. No declaring header (file-local, as it was
// here).

// get_weapon_damage() relocated to char_utils_combat.cpp (placement-seam
// Task 5; census verdict MOVE-OTHER-L2), taking do_squareroot()/
// get_bow_weapon_damage() with it: entity-pure combat-stat calculation, no
// world[]/output/combat-call dependency. Declaration unchanged in utils.h.

// weight_coof() relocated to char_utils_combat.cpp (placement-seam Task 5;
// census verdict MOVE-OTHER-L2): entity-pure CAN_WEAR lookup. No declaring
// header (file-local, as it was here).

// armor_absorb() relocated to char_utils_combat.cpp (placement-seam Task 5;
// census verdict MOVE-OTHER-L2), alongside its weight_coof() dependency:
// entity-pure. Declaration unchanged in utils.h.

// get_real_stealth() relocated to char_utils_combat.cpp (placement-seam
// Task 5; census verdict MOVE-OTHER-L2, resolver dep): its single unchecked
// world[ch->in_room].sector_type read (no bounds test in the original)
// becomes room_by_id_total(ch->in_room)->sector_type per the BINDING
// addendum's resolver-variant rule -- see task-5-report.md for the exact
// before/after quote. No declaring header (file-local, as it was here).

// STAY-APP (placement-seam Task 5 census): the player_spec::
// weapon_master_handler spec-handler this function calls is app-tier, so it
// stays here even though its trio-mate get_real_dodge() below moved to
// char_utils_combat.cpp/rots_entity this task -- the OB/parry/dodge trio now
// splits across tiers. Task 7 must update AGENTS.md's Dead/Unused-Code trio
// paragraph to reflect this split.
int get_real_OB(char_data* ch)
{
    if (IS_NPC(ch)) {
        int base_npc_ob = (GET_OB(ch) + GET_BAL_STR(ch) + 15 - utils::get_skill_penalty(*ch) + GET_LEVEL(ch) / 2);
        if (IS_AFFECTED(ch, AFF_CONFUSE)) {
            base_npc_ob -= (get_confuse_modifier(ch) * 2 / 3);
        }
        return base_npc_ob;
    }

    int sun_mod = 0;
    int tmpob, tactics = 0, weapon_skill = 0;

    obj_data* weapon = ch->equipment[WIELD];

    int warrior_level = GET_PROF_LEVEL(PROF_WARRIOR, ch);
    int max_warrior_level = GET_MAX_RACE_PROF_LEVEL(PROF_WARRIOR, ch);
    int offense_stat = GET_BAL_STR(ch);

    // Light fighters can use dex and some of their ranger level with light weapons.
    if (utils::get_specialization(*ch) == game_types::PS_LightFighting) {
        if (weapon) {
            int bulk = weapon->get_bulk();
            if (bulk <= 2 || (bulk == 3 && weapon->get_weight() <= LIGHT_WEAPON_WEIGHT_CUTOFF)) {
                offense_stat = std::max(offense_stat, int(ch->tmpabilities.dex));

                int ranger_bonus = GET_PROF_LEVEL(PROF_RANGER, ch) / 3;
                warrior_level += ranger_bonus;
            }
        }
    }

    int ob_bonus = (warrior_level * 3 + 3 * max_warrior_level * GET_LEVELA(ch) / 30) / 2 + offense_stat;

    tmpob = GET_OB(ch);
    tmpob -= utils::get_skill_penalty(*ch);

    player_spec::weapon_master_handler weapon_master(ch);
    tmpob += weapon_master.get_bonus_OB();

    if (!weapon && utils::get_raw_knowledge(*ch, SKILL_NATURAL_ATTACK) == 0) {
        return tmpob + ob_bonus;
    } else if (!weapon && utils::get_raw_knowledge(*ch, SKILL_NATURAL_ATTACK) > 0) {
        weapon_skill = utils::get_raw_knowledge(*ch, SKILL_NATURAL_ATTACK);
        tmpob -= (GET_STR(ch) / 2 - 6);
    } else {
        weapon_skill = utils::get_raw_knowledge(*ch, weapon_skill_num(weapon->get_weapon_type()));

        if (IS_TWOHANDED(ch)) {
            if (weapon->is_ranged_weapon()) {
                tmpob += weapon->obj_flags.value[2] * (200 + GET_RAW_SKILL(ch, SKILL_ARCHERY)) / 100 - 15;
                weapon_skill = (weapon_skill + GET_RAW_SKILL(ch, SKILL_ARCHERY)) / 2;
            } else {
                tmpob += weapon->obj_flags.value[2] * (200 + GET_RAW_KNOWLEDGE(ch, SKILL_TWOHANDED)) / 100 - 15;
                weapon_skill = (weapon_skill + GET_RAW_KNOWLEDGE(ch, SKILL_TWOHANDED)) / 2;
            }
        } else {
            tmpob -= (weapon->obj_flags.value[2] * 2 - 6);
        }
    }

    switch (GET_TACTICS(ch)) {
    case TACTICS_DEFENSIVE:
        tmpob += ob_bonus - ob_bonus / 4 - 8;
        tactics = 4;
        break;
    case TACTICS_CAREFUL:
        tmpob += ob_bonus - ob_bonus / 8 - 4;
        tactics = 6;
        break;
    case TACTICS_NORMAL:
        tmpob += ob_bonus;
        tactics = 8;
        break;
    case TACTICS_AGGRESSIVE:
        tmpob += ob_bonus + ob_bonus / 16 + 2;
        tactics = 10;
        break;
    case TACTICS_BERSERK:
        tmpob += ob_bonus + ob_bonus / 16 + 5 + GET_RAW_SKILL(ch, SKILL_BERSERK) / 8;
        tactics = 10;
        break;
    default:
        tmpob += ob_bonus + GET_BAL_STR(ch);
        break;
    };

    if (IS_AFFECTED(ch, AFF_CONFUSE))
        tmpob -= (get_confuse_modifier(ch) * 2 / 3);

    /* to get the pre-power of arda malus, substitute 10 for sun_mod */
    sun_mod = get_power_of_arda(ch);
    if (sun_mod) {
        if (GET_RACE(ch) == RACE_URUK)
            tmpob = tmpob * 4 / 5 - sun_mod;
        if (GET_RACE(ch) == RACE_ORC)
            tmpob = tmpob * 3 / 4 - sun_mod;
        if (GET_RACE(ch) == RACE_MAGUS)
            tmpob = tmpob * 4 / 5 - sun_mod;
        if (GET_RACE(ch) == RACE_OLOGHAI)
            tmpob = tmpob * 4 / 5 - sun_mod;
    }

    if (!CAN_SEE(ch))
        tmpob -= 10;

    if (!weapon)
        tmpob += weapon_skill * (GET_STR(ch) + 20) * tactics / 1000;
    else
        tmpob += weapon_skill * (weapon->obj_flags.value[2] + 20) * tactics / 1000;

    return tmpob;
}

// STAY-APP (placement-seam Task 5 census): same weapon_master
// spec-handler app edge as get_real_OB() above; get_real_dodge() (the trio's
// third member) moved to char_utils_combat.cpp/rots_entity this task. See
// get_real_OB()'s comment above and Task 7's AGENTS.md follow-up.
int get_real_parry(struct char_data* ch)
{
    int sun_mod = 0;

    if (IS_NPC(ch)) {
        if (IS_AFFECTED(ch, AFF_CONFUSE))
            return (GET_PARRY(ch) + GET_LEVEL(ch) / 2 + 15) - (get_confuse_modifier(ch) * 2 / 3);
        else
            return (GET_PARRY(ch) + GET_LEVEL(ch) / 2 + 15);
    }

    int tmpparry, tmpskill, tactics, bonus, weapon_bonus = 0;
    struct obj_data* weapon;

    tmpparry = GET_PARRY(ch);
    bonus = GET_PROF_LEVEL(PROF_WARRIOR, ch) * 2 + std::min(30, GET_LEVEL(ch)) + GET_BAL_STR(ch);

    player_spec::weapon_master_handler weapon_master(ch);
    tmpparry += weapon_master.get_bonus_PB();

    weapon = ch->equipment[WIELD];
    if (!weapon && utils::get_raw_knowledge(*ch, SKILL_NATURAL_ATTACK) == 0) {
        return tmpparry + bonus / 2;
    } else if (!weapon && utils::get_raw_knowledge(*ch, SKILL_NATURAL_ATTACK) > 0) {
        tmpskill = GET_RAW_SKILL(ch, SKILL_NATURAL_ATTACK);
    } else {
        weapon_bonus = weapon->obj_flags.value[1];

        tmpskill = GET_RAW_KNOWLEDGE(ch, weapon_skill_num(weapon->obj_flags.value[3]));
        if (isname_nullable("bow", weapon->name)) {
            tmpskill = GET_RAW_SKILL(ch, SKILL_ARCHERY);
        }

        if (IS_TWOHANDED(ch)) {
            tmpskill = (tmpskill + GET_RAW_KNOWLEDGE(ch, SKILL_TWOHANDED)) / 2;
            if (isname_nullable("bow", weapon->name)) {
                tmpskill = (tmpskill + GET_RAW_SKILL(ch, SKILL_ARCHERY)) / 2;
            }
        }
    }

    tmpskill = (tmpskill + 3 * GET_RAW_KNOWLEDGE(ch, SKILL_PARRY)) / 4;
    if (GET_TACTICS(ch) == TACTICS_BERSERK) {
        tmpskill /= 2;
    }

    switch (GET_TACTICS(ch)) {
    case TACTICS_DEFENSIVE:
        tmpparry += bonus / 2 + 3 * bonus / 16;
        tactics = 4;
        break;
    case TACTICS_CAREFUL:
        tmpparry += bonus / 2 + bonus / 8;
        tactics = 6;
        break;
    case TACTICS_NORMAL:
        tmpparry += bonus / 2;
        tactics = 8;
        break;
    case TACTICS_AGGRESSIVE:
        tmpparry += bonus / 2 - bonus / 8;
        tactics = 10;
        break;
    case TACTICS_BERSERK:
        tmpparry += bonus / 2 - bonus / 8;
        tactics = 12;
        break;
    default:
        tmpparry += bonus / 2;
        tactics = 10;
        break;
    };

    tmpparry += tmpskill * (weapon_bonus + 20) * (14 - tactics) / 1000;
    // Parry should now have bigger effect on two-handers:
    if (IS_AFFECTED(ch, AFF_TWOHANDED))
        tmpparry += weapon_bonus / 2;

    if (IS_AFFECTED(ch, AFF_CONFUSE))
        tmpparry -= (get_confuse_modifier(ch) * 2 / 3);

    sun_mod = get_power_of_arda(ch);
    if (sun_mod) {
        if (GET_RACE(ch) == RACE_URUK)
            tmpparry = tmpparry * 9 / 10 - sun_mod;
        if (GET_RACE(ch) == RACE_ORC)
            tmpparry = tmpparry * 8 / 9 - sun_mod;
        if (GET_RACE(ch) == RACE_MAGUS)
            tmpparry = tmpparry * 9 / 10 - sun_mod;
        if (GET_RACE(ch) == RACE_OLOGHAI)
            tmpparry = tmpparry * 9 / 10 - sun_mod;
    }

    if (!CAN_SEE(ch)) {
        tmpparry -= 10;
    }

    return tmpparry;
}

// get_real_dodge() relocated to char_utils_combat.cpp (placement-seam
// Task 5; census verdict MOVE-OTHER-L2): entity-pure, the cleanest of the
// OB/parry/dodge trio (no weapon_master, no world[]) -- see get_real_OB()/
// get_real_parry() above for the trio's STAY-APP half. Declaration
// unchanged in utils.h.

// get_followers_level() relocated to char_utils.cpp (placement-seam
// Task 5; census verdict MOVE-OTHER-L2): entity-pure character_list walk.
// No declaring header (file-local, as it was here).

// number(double) relocated to rots_util.cpp (placement-seam Task 5;
// census verdict MOVE-OTHER(platform)): thin wrapper over the
// already-platform-tier number(). Declaration unchanged in utils.h.

// number_d() relocated to rots_util.cpp (placement-seam Task 5; census
// verdict MOVE-OTHER(platform)): thin wrapper over number(double)/
// number(). Declaration unchanged in utils.h.

// rots_asprintf() relocated to rots_util.cpp (placement-seam Task 5;
// census verdict MOVE-OTHER(platform), matching the rots_remove()/
// rots_rename_replace() precedent -- declared in platform_compat.h,
// defined in rots_util.cpp): pure libc varargs, no game-type dependency.

// strn_cmp() relocated to rots_util.cpp (placement-seam Task 5; census
// verdict MOVE-OTHER(platform)): the utils.h LOWER macro is inlined as
// the file's existing lower_ascii() helper (same precedent as
// str_cmp()/str_cmp_nullable() there). Declaration unchanged in utils.h.

// strn_cmp_nullable() relocated to rots_util.cpp (placement-seam
// Task 5; census verdict MOVE-OTHER(platform)): same LOWER-macro ->
// lower_ascii() substitution as strn_cmp() above. Declaration unchanged
// in utils.h.

/* log a death trap hit */
void log_death_trap(struct char_data* ch)
{
    //   extern struct room_data world;

    std::string message = std::format("{} hit death trap #{} ({})", GET_NAME(ch),
        world[ch->in_room].number, world[ch->in_room].name);
    mudlog(message, BRF, LEVEL_IMMORT, TRUE);
}

// mudlog_debug_mob() relocated to char_utils.cpp (placement-seam
// Task 5; census verdict MOVE-OTHER-L2): thin mudlog_aliased_mob()
// forwarder. Declaration unchanged in utils.h.

// mudlog_aliased_mob() relocated to char_utils.cpp (placement-seam
// Task 5; census verdict MOVE-OTHER-L2), alongside mudlog_debug_mob()
// above: entity-pure. Declaration unchanged in utils.h.

// sprintbit() relocated to rots_util.cpp (placement-seam Task 5; census
// verdict MOVE-OTHER(platform)): the utils.h IS_SET macro is inlined as
// its own (flag)&(bit) definition and rots/core/room.h's BFS_MARK
// constant is inlined as a local constexpr, the same L0-must-not-include
// precedent as this file's other relocations. Declaration unchanged in
// utils.h.

// sprinttype() relocated to rots_util.cpp (placement-seam Task 5; census
// verdict MOVE-OTHER(platform)): pure text formatting. Declaration
// unchanged in utils.h.

// BLOCKING FINDING (placement-seam Task 5; see task-5-report.md): census
// verdict was MOVE-OTHER(platform) with "upward refs: none", but this function
// returns struct time_info_data BY VALUE -- that type is defined in
// rots/core/types.h (L1/rots_core), and every rots_util.cpp precedent already
// in that file (MAX_INPUT_LENGTH, FIND_ALL/FIND_ALLDOT/FIND_INDIV,
// COPP_IN_GOLD/COPP_IN_SILV, LEVEL_IMMORT) explicitly declines to include that
// header even for a single scalar constant. DEFERRED, not moved: stays here
// verbatim, app tier, pending a follow-up wave (see mud_time_passed()'s
// comment below and task-5-report.md's recommendation).
/* Calculate the REAL time passed over the last t2-t1 centuries (secs) */
struct time_info_data real_time_passed(time_t t2, time_t t1)
{
    long secs;
    struct time_info_data now;

    secs = (long)(t2 - t1);

    now.hours = (secs / SECS_PER_REAL_HOUR) % 24; /* 0..23 hours */
    secs -= SECS_PER_REAL_HOUR * now.hours;

    now.day = (secs / SECS_PER_REAL_DAY); /* 0..34 days  */
    secs -= SECS_PER_REAL_DAY * now.day;

    now.month = 0;
    now.year = 0;

    return now;
}

// BLOCKING FINDING (placement-seam Task 5; see task-5-report.md): same
// struct time_info_data-by-value / L1-type dependency as real_time_passed()
// above. DEFERRED, not moved: stays here verbatim, app tier. age() below
// (census verdict MOVE-OTHER-L2/char_utils.cpp) calls this function and is
// deferred alongside it for the same reason -- see age()'s own comment.
/* Calculate the MUD time passed over the last t2-t1 centuries (secs) */
struct time_info_data mud_time_passed(time_t t2, time_t t1)
{
    long secs;
    struct time_info_data now;

    secs = (long)(t2 - t1);

    now.hours = (secs / SECS_PER_MUD_HOUR) % 24; /* 0..23 hours */
    secs -= SECS_PER_MUD_HOUR * now.hours;

    now.day = (secs / SECS_PER_MUD_DAY) % 30; /* 0..34 days  */
    now.moon = (secs / SECS_PER_MUD_DAY) % 28; /* 0..34 days  */
    secs -= SECS_PER_MUD_DAY * now.day;

    now.month = (secs / SECS_PER_MUD_MONTH) % 12; /* 0..16 months */
    secs -= SECS_PER_MUD_MONTH * now.month;

    now.year = (secs / SECS_PER_MUD_YEAR); /* 0..XX? years */

    return now;
}

// DEFERRED alongside mud_time_passed() above (placement-seam Task 5; see
// task-5-report.md): age()'s own census verdict is MOVE-OTHER-L2/
// char_utils.cpp and its own body is entity-pure, but it calls
// mud_time_passed(), which cannot move to rots_util.cpp/L0 this task
// (blocking finding above) -- moving age() alone would leave an
// EntityLayerAcyclicity-breaking call from rots_entity into still-app-tier
// utility.cpp, the same class of problem Task 4 hit with
// parse_numbered_name()/get_char(). Stays here verbatim, app tier, until
// mud_time_passed()'s L1 dependency is resolved.
struct time_info_data age(struct char_data* ch)
{
    struct time_info_data player_age;

    player_age = mud_time_passed(time(0), ch->player.time.birth);

    player_age.year += 17; /* All players start at 17 */

    return player_age;
}

/*
** Turn off echoing (specific to telnet client)
*/

void echo_off(SocketType sock)
{

    char off_string[] = //"";
        {
            (char)IAC,
            (char)WILL,
            (char)TELOPT_ECHO,
            (char)0,
        };
    (void)rots_net::write_socket(sock, off_string, sizeof(off_string));
}

/*
** Turn on echoing (specific to telnet client)
*/

void echo_on(SocketType sock)
{
    char off_string[] = //"";
        {
            (char)IAC,
            (char)WONT,
            (char)TELOPT_ECHO,
            (char)TELOPT_NAOFFD,
            (char)TELOPT_NAOCRD,
            (char)0,
        };
    (void)rots_net::write_socket(sock, off_string, sizeof(off_string));
}

void initialize_buffers()
{
    return;
}

// encrypt_line_lp / encrypt_line() relocated to entity_lifecycle.cpp (db-split Task 4b).

// decrypt_line_line / decrypt_line() relocated to entity_lifecycle.cpp (db-split Task 4b).

// strcpy_lang() relocated to rots_util.cpp (placement-seam Task 5;
// census verdict MOVE-OTHER(platform)): pure text plus the
// already-platform-tier number(int,int). Declaration unchanged in
// utils.h.

// reshuffle() relocated to rots_util.cpp (placement-seam Task 5; census
// verdict MOVE-OTHER(platform)): pure array shuffle over the
// already-platform-tier number()/log(). Declaration unchanged in utils.h.

/*
 * Can character see at all?
 */
int CAN_SEE(struct char_data* sub)
{
    if ((sub)->in_room == NOWHERE)
        return 0;
    if (IS_AFFECTED((sub), AFF_BLIND) || PLR_FLAGGED(sub, PLR_WRITING))
        return 0;

    if (IS_SHADOW(sub))
        return 1;

    if (!IS_LIGHT((sub)->in_room) && (!IS_AFFECTED((sub), AFF_INFRARED) && !PRF_FLAGGED((sub), PRF_HOLYLIGHT) && !(OUTSIDE(sub) && IS_AFFECTED((sub), AFF_MOONVISION) && weather_info.moonlight)))
        return 0;

    return 1;
}

/*
 * Can subject see character "obj"?  Returns 0 if sub
 * cannot see obj, and 1 if sub can see obj.  CAN_SEE
 * is called way too many times.  From testing, it's
 * called three times for a kill command.
 */
int CAN_SEE(struct char_data* sub, struct char_data* obj, int light_mode)
{
    int tmp;

    if (!obj)
        return CAN_SEE(sub);
    if ((sub) == (obj))
        return 1;
    if (PLR_FLAGGED(sub, PLR_WRITING))
        return 0;

    /*
     * If you aren't in the same room as it, and it's an NPC,
     * you can't see it, unless it's the guardian angel.
     */
    if (GET_LEVEL(sub) < LEVEL_IMMORT && sub->in_room != obj->in_room && IS_NPC(obj) && strcmp(GET_NAME(obj), "Guardian angel"))
        return 0;

    if (IS_SHADOW(obj) || IS_SHADOW(sub)) {
        if ((sub->specials.fighting == obj) || (obj->specials.fighting == sub))
            return 1;
        if (!(!IS_NPC(sub) && PRF_FLAGGED((sub), PRF_HOLYLIGHT)) && (GET_PERCEPTION(sub) * GET_PERCEPTION(obj) < number(1, 10000)))
            return 0;
    }

    /*
     * Light/physical dependent stuff in here: shadows can
     * see though physical objects (hence see hidden players)
     * and don't worry about light sources.
     */
    if (!(IS_SHADOW(sub))) {
        if (GET_HIDING(obj)) {
            /* mobs have a 10% chance to simply not see people that sneak in */
            if (IS_NPC(sub) && IS_SET(obj->specials2.hide_flags, HIDING_SNUCK_IN) && !number(0, 9) && !IS_NPC(obj)) {
                act("$n glances directly at you, but doesn't seem to notice "
                    "your presence.",
                    FALSE, sub, 0, obj, TO_VICT);
                return 0;
            } else
                tmp = see_hiding(sub);
            if ((tmp < GET_HIDING(obj)) && !(!IS_NPC(sub) && PRF_FLAGGED((sub), PRF_HOLYLIGHT))) {
                return 0;
            }
        }

        if (!light_mode) {
            if (!IS_LIGHT((obj)->in_room) && !IS_AFFECTED((sub), AFF_INFRARED) && !(!IS_NPC(sub) && PRF_FLAGGED((sub), PRF_HOLYLIGHT)) && !(OUTSIDE(sub) && IS_AFFECTED((sub), AFF_MOONVISION) && weather_info.moonlight))
                return 0;
        }
    }
    /* End light/physical dependent stuff */

    /* If obj has an invis level, you can't see it unless you're >= that level */
    if ((GET_LEVEL(sub) < GET_INVIS_LEV(obj)) && !IS_NPC(obj))
        return 0;

    /* Blinded players don't see anything */
    if (IS_AFFECTED((sub), AFF_BLIND))
        return 0;

    /* You can't see invisible objects; unless you're an imm with HOLYLIGHT */
    if (IS_AFFECTED((obj), AFF_INVISIBLE))
        if (!(!IS_NPC(sub) && PRF_FLAGGED((sub), PRF_HOLYLIGHT)))
            return 0;

    /* Ok, noone can see if they are sleeping :) */
    if (GET_POS((sub)) <= POSITION_SLEEPING)
        return 0;

    return 1;
}

// can_sense() relocated to char_utils_combat.cpp (placement-seam Task 5;
// census verdict MOVE-OTHER-L2): entity-pure GET_* macro logic.
// Declaration unchanged in utils.h.

int CAN_SEE_OBJ(char_data* sub, obj_data* obj)
{
    if (!sub || !obj)
        return 0;

    if (IS_SHADOW(sub)) {
        if (IS_SET((obj)->obj_flags.extra_flags, ITEM_MAGIC) || IS_SET((obj)->obj_flags.extra_flags, ITEM_WILLPOWER))
            return 1;
        else
            return 0;
    }

    if ((!IS_SET((obj)->obj_flags.extra_flags, ITEM_INVISIBLE) || IS_AFFECTED((sub), AFF_DETECT_INVISIBLE)) && CAN_SEE(sub))
        return 1;

    return 0;
}

// CAN_GO() relocated to environment_utils.cpp (placement-seam Task 5; census
// verdict MOVE-OTHER-L2, resolver dep): the EXIT(ch,door) macro (utils.h:
// `#define EXIT(ch, door) (world[(ch)->in_room].dir_option[door])`)
// expands to an unchecked world[] index (no bounds test anywhere in the
// original body) -- per the BINDING addendum's resolver-variant rule,
// hoisted once into room_by_id_total(ch->in_room) with every EXIT(ch,door)
// site replaced by r->dir_option[door] -- see task-5-report.md for the
// full expansion evidence. Declaration unchanged in utils.h.

// get_confuse_modifier() relocated to entity_lifecycle.cpp (db-split Task 4b);
// declaration unchanged in utils.h.

// get_power_of_arda() relocated to char_utils_combat.cpp
// (placement-seam Task 5; census verdict MOVE-OTHER-L2): entity-pure
// affected_by_spell() lookup, needed by the OB/parry/dodge trio.
// Declaration unchanged in utils.h.

// has_critical_stat_damage() relocated to char_utils.cpp
// (placement-seam Task 5; census verdict MOVE-OTHER-L2): entity-pure
// GET_* macro logic. Declaration unchanged in utils.h.

// can_breathe() relocated to environment_utils.cpp (placement-seam Task 5;
// census verdict MOVE-OTHER-L2, resolver dep): its two unchecked
// world[ch->in_room].sector_type reads (no bounds test in the original)
// become a single hoisted room_by_id_total(ch->in_room) per the BINDING
// addendum's resolver-variant rule. Declaration unchanged in utils.h.

// nth() relocated to rots_util.cpp (placement-seam Task 5; census verdict
// MOVE-OTHER(platform)): pure text over the already-platform-tier
// rots_asprintf(). Declaration unchanged in utils.h.

// BLOCKING FINDING (placement-seam Task 5; see task-5-report.md): census
// verdict was MOVE-OTHER(platform) with "upward refs: none", but the body
// reads month_name[...] -- an external symbol defined in consts.cpp
// (rots_core, L1) and declared in rots/core/tables.h -- and takes
// struct time_info_data* (rots/core/types.h, L1) by pointer, dereferencing
// its fields. This is a genuine, hard PlatformLayerAcyclicity-breaking
// upward edge (not merely a header-inclusion style choice) -- the census's
// "none" was incomplete for this function. DEFERRED, not moved: stays here
// verbatim, app tier.
void day_to_str(struct time_info_data* loc_time_info, char* str)
{
    char* s;
    int day;
    day = loc_time_info->day + 1; /* day in [1..35] */

    s = nth(day);

    const std::string message = std::format("the {} day of {}", s, month_name[(int)loc_time_info->month]);
    strcpy(str, message.c_str());

    free(s);
}

// find_player_in_table() relocated to db_players.cpp (persist-split PS
// Task 4, controller-adjudicated relocation): a pure player_table/
// top_of_p_table index lookup (both already db_players.cpp globals), no
// comm/world dependency. Declaration unchanged (utils.h).

/*
 * Return a pointer to the character structure associated with
 * `idnum'; this is possible ONLY if a character is playing!
 * find_playing_char will return NULL if no such player was
 * found.
 */
struct char_data*
find_playing_char(int idnum)
{
    struct descriptor_data* d;
    extern struct descriptor_data* descriptor_list;

    for (d = descriptor_list; d; d = d->next)
        if ((d->character && d->connected == CON_PLYNG) && (d->character->specials2.idnum == idnum))
            return d->character;

    return NULL;
}

void set_mental_delay(struct char_data* ch, int value)
{
    if (!GET_MENTAL_DELAY(ch))
        set_fighting(ch, 0);

    GET_MENTAL_DELAY(ch) += value;
}

// universal_list_counter/used_in_universal_list + pool_to_list()/
// from_list_to_pool() relocated to entity_lifecycle.cpp (entity-seed Task 5);
// declarations unchanged in utils.h.

// check_resistances() relocated to char_utils_combat.cpp
// (placement-seam Task 5; census verdict MOVE-OTHER-L2): entity-pure
// skills[] (rots_core, L1) table lookup. Declaration unchanged in utils.h.

/*
 * Compare `obj' to its prototype; return 0 if the object
 * is altered, and 1 if it's the same.  If no prototype is
 * found, we return -1.
 */
int compare_obj_to_proto(struct obj_data* obj)
{
    int diff = 0, i;
    struct obj_data* tmp;
    struct extra_descr_data *edesc, *tmp_edesc;
    extern struct obj_data* obj_proto;
    extern int top_of_objt;
    int generic_scalp = 19;

    if (!obj) // If there's no object, what in tarnation are you trying?
        return -2;

    if (!(obj->item_number) || (obj->item_number < 0) || (obj->item_number > top_of_objt))
        return -2;

    // SPECIAL CASES, such as generic scalps/heads, go here!  Some items DO NOT
    // have prototypes, and will almost certainly cause the MUD to crash if they
    // are seen as valid.
    //
    // Since scalps, for one, have no value other than as ornaments, they can
    // be excluded.
    if (obj->item_number == generic_scalp)
        return 0;

    // Check if there is a prototype.
    tmp = &obj_proto[obj->item_number];

    if (!tmp)
        diff--;
    else {
        // Compare the easy stuff.
        if (str_cmp_nullable(obj->name, tmp->name))
            diff++;
        if (str_cmp_nullable(obj->description, tmp->description))
            diff++;
        if (str_cmp_nullable(obj->short_description, tmp->short_description))
            diff++;
        if (str_cmp_nullable(obj->action_description, tmp->action_description))
            diff++;

        // Compare extra descriptions.
        edesc = obj->ex_description;
        tmp_edesc = tmp->ex_description;

        if (edesc && tmp_edesc)
            do {
                if (!((edesc->keyword == tmp_edesc->keyword) && (!str_cmp_nullable(edesc->description, tmp_edesc->description))))
                    diff++;
                edesc = edesc->next;
                tmp_edesc = tmp_edesc->next;
            } while (tmp_edesc && edesc);

        // If there is still a description on one object, and not on the other...
        if ((edesc && !tmp_edesc) || (tmp_edesc && !edesc))
            diff++;

        // Compare flags.
        if (obj->obj_flags.value[0] != tmp->obj_flags.value[0])
            diff++;
        // See below for values 1, 2 and 3.
        if (obj->obj_flags.value[4] != tmp->obj_flags.value[4])
            diff++;
        if (obj->obj_flags.type_flag != tmp->obj_flags.type_flag)
            diff++;
        if (obj->obj_flags.wear_flags != tmp->obj_flags.wear_flags)
            diff++;
        if (obj->obj_flags.extra_flags != tmp->obj_flags.extra_flags)
            diff++;
        if (obj->obj_flags.cost != tmp->obj_flags.cost)
            diff++;
        if (obj->obj_flags.cost_per_day != tmp->obj_flags.cost_per_day)
            diff++;
        if (obj->obj_flags.timer != tmp->obj_flags.timer)
            diff++;
        if (obj->obj_flags.bitvector != tmp->obj_flags.bitvector)
            diff++;
        if (obj->obj_flags.level != tmp->obj_flags.level)
            diff++;
        if (obj->obj_flags.rarity != tmp->obj_flags.rarity)
            diff++;
        if (obj->obj_flags.material != tmp->obj_flags.material)
            diff++;
        if (obj->obj_flags.prog_number != tmp->obj_flags.prog_number)
            diff++;

        // Weight is special as this might be a container.
        if ((obj->obj_flags.type_flag != ITEM_CONTAINER) && (obj->obj_flags.type_flag != ITEM_DRINKCON) && (obj->obj_flags.weight != tmp->obj_flags.weight))
            diff++;

        // Value 1 is special for drink containers.
        if ((obj->obj_flags.type_flag != ITEM_DRINKCON) && (obj->obj_flags.value[1] != tmp->obj_flags.value[1]))
            diff++;
        // Values 2 and 3 are special for light sources.
        if (obj->obj_flags.type_flag != ITEM_LIGHT) {
            if (obj->obj_flags.value[2] != tmp->obj_flags.value[2])
                diff++;
            if (obj->obj_flags.value[3] != tmp->obj_flags.value[3])
                diff++;
        }

        // Compare affections.
        for (i = 0; i < MAX_OBJ_AFFECT; i++) {
            if (!((obj->affected[i].location == tmp->affected[i].location) && (obj->affected[i].modifier == tmp->affected[i].modifier)))
                diff++;
        }

        // Special check for enchant.  Enchant shouldn't make an object different.
        // Enchant applies APPLY_OB to an otherwise unaffected object.  It will
        // also apply one of (or both of) anti-good, anti-evil.
        if ((obj->obj_flags.type_flag == ITEM_WEAPON) && (tmp->obj_flags.type_flag == ITEM_WEAPON)) {
            // Any type of enchant.
            if ((obj->affected[0].location == APPLY_OB) && (tmp->affected[0].location != APPLY_OB))
                diff--;

            // First check if the object has ANTI_GOOD, ANTI_EVIL, or MAGIC on it in
            // the prototype.
            int obj_flags = 0;
            int tmp_flags = 0;

            if (IS_OBJ_STAT(obj, ITEM_MAGIC))
                obj_flags += ITEM_MAGIC;
            if (IS_OBJ_STAT(obj, ITEM_ANTI_GOOD))
                obj_flags += ITEM_ANTI_GOOD;
            if (IS_OBJ_STAT(obj, ITEM_ANTI_EVIL))
                obj_flags += ITEM_ANTI_EVIL;

            if (IS_OBJ_STAT(tmp, ITEM_MAGIC))
                tmp_flags += ITEM_MAGIC;
            if (IS_OBJ_STAT(tmp, ITEM_ANTI_GOOD))
                tmp_flags += ITEM_ANTI_GOOD;
            if (IS_OBJ_STAT(tmp, ITEM_ANTI_EVIL))
                tmp_flags += ITEM_ANTI_EVIL;

            // Now check the enchant.
            if ((obj->obj_flags.extra_flags != tmp->obj_flags.extra_flags) && (obj->obj_flags.extra_flags - obj_flags + tmp_flags == tmp->obj_flags.extra_flags))
                diff--;
        }
    }

    return diff;
}

struct obj_data* obj_to_proto(struct obj_data* obj)
{
    // Converts an existing object to a copy of its prototype.
    int i, aff_count = 0;
    struct obj_data* tmp;
    struct extra_descr_data *edesc, *tmp_edesc;
    extern struct obj_data* obj_proto;
    extern struct index_data* obj_index;
    struct obj_data* new_obj;

    CREATE(new_obj, struct obj_data, 1);

    // Get the prototype.
    tmp = &obj_proto[obj->item_number];

    // Copy everything over.
    new_obj->item_number = tmp->item_number;
    new_obj->owner = obj->owner;
    new_obj->carried_by = obj->carried_by;
    new_obj->in_obj = obj->in_obj;
    new_obj->name = str_dup(tmp->name);
    new_obj->description = str_dup(tmp->description);
    new_obj->short_description = str_dup(tmp->short_description);
    new_obj->action_description = str_dup(tmp->action_description);

    // Copy extra descriptions.
    tmp_edesc = tmp->ex_description;

    if (tmp_edesc) {
        CREATE1(new_obj->ex_description, extra_descr_data);
        edesc = new_obj->ex_description;

        do {
            edesc->keyword = str_dup(tmp_edesc->keyword);
            edesc->description = str_dup(tmp_edesc->description);

            if (tmp_edesc->next) {
                tmp_edesc = tmp_edesc->next;
                CREATE1(edesc->next, extra_descr_data);
                edesc = edesc->next;
            }
        } while (tmp_edesc);
    }

    // Copy flags.
    new_obj->obj_flags.value[0] = tmp->obj_flags.value[0];
    new_obj->obj_flags.value[1] = tmp->obj_flags.value[1];
    new_obj->obj_flags.value[2] = tmp->obj_flags.value[2];
    new_obj->obj_flags.value[3] = tmp->obj_flags.value[3];
    new_obj->obj_flags.value[4] = tmp->obj_flags.value[4];
    new_obj->obj_flags.type_flag = tmp->obj_flags.type_flag;
    new_obj->obj_flags.wear_flags = tmp->obj_flags.wear_flags;
    new_obj->obj_flags.extra_flags = tmp->obj_flags.extra_flags;
    new_obj->obj_flags.weight = tmp->obj_flags.weight;
    new_obj->obj_flags.cost = tmp->obj_flags.cost;
    new_obj->obj_flags.cost_per_day = tmp->obj_flags.cost_per_day;
    new_obj->obj_flags.timer = tmp->obj_flags.timer;
    new_obj->obj_flags.bitvector = tmp->obj_flags.bitvector;
    new_obj->obj_flags.level = tmp->obj_flags.level;
    new_obj->obj_flags.rarity = tmp->obj_flags.rarity;
    new_obj->obj_flags.material = tmp->obj_flags.material;
    new_obj->obj_flags.prog_number = tmp->obj_flags.prog_number;

    // If a prototype has any affections, it doesn't matter if the original
    // object is enchanted, because enchant will go away.  So, copy all of
    // the prototype's affections.
    for (i = 0; i < MAX_OBJ_AFFECT; i++) {
        new_obj->affected[i].location = tmp->affected[i].location;
        new_obj->affected[i].modifier = tmp->affected[i].modifier;
        if (tmp->affected[i].location > 0)
            aff_count = 1;
    }

    // Now, if the object was enchanted, and the prototype isn't affected,
    // restore the enchant.
    if (!aff_count) {
        // No affections on the prototype.  Apply away.
        // Only do this if the apply is APPLY_OB!
        if (obj->affected[0].location == APPLY_OB) {
            new_obj->affected[0].location = obj->affected[0].location;
            new_obj->affected[0].modifier = obj->affected[0].modifier;

            int obj_flags = 0;

            if (IS_OBJ_STAT(obj, ITEM_MAGIC))
                obj_flags += ITEM_MAGIC;
            if (IS_OBJ_STAT(obj, ITEM_ANTI_GOOD))
                obj_flags += ITEM_ANTI_GOOD;
            if (IS_OBJ_STAT(obj, ITEM_ANTI_EVIL))
                obj_flags += ITEM_ANTI_EVIL;

            if (IS_OBJ_STAT(new_obj, ITEM_MAGIC))
                obj_flags -= ITEM_MAGIC;
            if (IS_OBJ_STAT(new_obj, ITEM_ANTI_GOOD))
                obj_flags -= ITEM_ANTI_GOOD;
            if (IS_OBJ_STAT(new_obj, ITEM_ANTI_EVIL))
                obj_flags -= ITEM_ANTI_EVIL;

            new_obj->obj_flags.extra_flags += obj_flags;
        }
    }

    // Now move the contents from the old object to the new.
    tmp = obj->contains;

    while (tmp) {
        obj_from_obj(tmp);
        obj_to_obj(tmp, new_obj);
        tmp = obj->contains;
    };

    // The new object must be touched if the old one was touched.
    if (obj->touched == 1) {
        new_obj->touched = 1;
    }

    extract_obj(obj);
    (obj_index[new_obj->item_number].number)++;

    return new_obj;
}

// check_inventory_proto/check_equipment_proto/check_container_proto (below):
// their sprintf(buf, " - ...%s...", tmp->short_description) sites are left
// unconverted for Phase 4 Wave 1 Task 5. Characterizing them needs a real
// obj_index/obj_proto prototype-table fixture to drive compare_obj_to_proto()
// down both its result>0 and result<0 branches -- out of scope for this
// leaf-module wave without broader object-system test scaffolding (the
// result<0 branch's sprintf is additionally a pre-existing dead store: its
// `buf` is never sent to the character, matching current behavior exactly
// either way). Left as sprintf pending a dedicated object_utils/objsave wave.
void check_inventory_proto(struct char_data* ch)
{
    int result = 0;
    struct obj_data* tmp;

    for (tmp = ch->carrying; tmp; tmp = tmp->next_content) {
        if (tmp->obj_flags.type_flag == ITEM_CONTAINER)
            check_container_proto(tmp, ch);

        result = compare_obj_to_proto(tmp);
        if (result > 0) {
            char buf[1024];

            // Justified skip -- see the block comment above
            // check_inventory_proto (utility.cpp:2252-2260): left unconverted
            // for Phase 4 Wave 1 Task 5 pending a dedicated
            // object_utils/objsave characterization wave.
#if defined(__clang__)
#pragma clang diagnostic push
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#endif
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
            sprintf(buf, " - An object in your inventory, %s, was updated.\n\r", tmp->short_description);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
            send_to_char(buf, ch);
            obj_to_char(obj_to_proto(tmp), ch);
        } else if (result < 0) {
            char buf[1024];

            // Justified skip -- same as above; this branch's `buf` is
            // additionally a pre-existing dead store (never sent), matching
            // current behavior exactly either way.
#if defined(__clang__)
#pragma clang diagnostic push
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#endif
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
            sprintf(buf, " - An object in your inventory, %s, has no prototype.  Please notify imps.\n\r",
                tmp->short_description);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        }
    }
}

void check_equipment_proto(struct char_data* ch)
{
    int i, result = 0;
    struct obj_data* tmp;

    for (i = 0; i < MAX_WEAR; i++) {
        tmp = ch->equipment[i];

        if (tmp) {
            if (tmp->obj_flags.type_flag == ITEM_CONTAINER)
                check_container_proto(tmp, ch);

            result = compare_obj_to_proto(tmp);
            if (result > 0) {
                char buf[1024];

                // Justified skip -- see the block comment above
                // check_inventory_proto (utility.cpp:2252-2260).
#if defined(__clang__)
#pragma clang diagnostic push
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#endif
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
                sprintf(buf, " - An object in your equipment, %s, was updated.\n\r", tmp->short_description);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
                send_to_char(buf, ch);
                obj_to_char(obj_to_proto(tmp), ch);
            } else if (result < 0) {
                char buf[1024];

                // Justified skip -- same as above; pre-existing dead store.
#if defined(__clang__)
#pragma clang diagnostic push
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#endif
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
                sprintf(buf, " - An object in your inventory, %s, has no prototype.  Please notify imps.\n\r",
                    tmp->short_description);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
            }
        }
    }
}

static void check_container_proto(struct obj_data* obj, struct char_data* ch)
{
    int result = 0;
    struct obj_data* tmp;

    for (tmp = obj->contains; tmp; tmp = tmp->next_content) {
        if (tmp->obj_flags.type_flag == ITEM_CONTAINER)
            check_container_proto(tmp, ch);

        result = compare_obj_to_proto(tmp);
        if (result > 0) {
            char buf[1024];

            // Justified skip -- see the block comment above
            // check_inventory_proto (utility.cpp:2252-2260).
#if defined(__clang__)
#pragma clang diagnostic push
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#endif
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
            sprintf(buf, " - An object in %s, %s, was updated.\n\r", obj->short_description, tmp->short_description);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
            send_to_char(buf, ch);
            obj_to_char(obj_to_proto(tmp), ch);
        } else if (result < 0) {
            char buf[1024];

            // Justified skip -- same as above; pre-existing dead store.
#if defined(__clang__)
#pragma clang diagnostic push
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#endif
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
            sprintf(buf, " - An object in your inventory, %s, has no prototype.  Please notify imps.\n\r",
                tmp->short_description);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
        }
    }
}

/*
 * Show 'target' to 'observer' if 'observer' can see 'target'.
 * Enemies are shown as starred races, colored by 'observer's
 * enemy color preference.  If 'target' is a recruited orc mob,
 * it will show up as an enemy.
 *
 * If 'capitalize' is true, then we attempt to capitalize the
 * final string.  If 'force_visible' is true, then we assume
 * that 'observer' can see 'target' even if they fail CAN_SEE.
 * force_visible is notably used by global communications,
 * where we always want everyone to appear visible.
 *
 * NOTE: It is the responsibility of the -caller- to preserve
 * any color settings in a string where PERS is used!
 */
char* PERS(struct char_data* target, struct char_data* observer,
    int capitalize, int force_visible)
{
    static char name[128];

    if (CAN_SEE(observer, target) || force_visible) {
        if (other_side(observer, target)) {
            snprintf(name, 127, "%s%s%s",
                CC_USE(observer, COLOR_ENMY),
                pc_star_types[GET_RACE(target)].data(),
                CC_NORM(observer).data());
        } else if (IS_NPC(target) && MOB_FLAGGED(target, MOB_ORC_FRIEND) && MOB_FLAGGED(target, MOB_PET) && other_side(target, observer)) {
            snprintf(name, 127, "%s%s%s",
                CC_USE(observer, COLOR_ENMY),
                pc_star_types[GET_RACE(target)].data(),
                CC_NORM(observer).data());
        } else
            snprintf(name, 127, "%s", GET_NAME(target));

        name[127] = '\0';
    } else
        // Left as sprintf for Phase 4 Wave 1 Task 5: characterizing this
        // branch needs a CAN_SEE(observer, target) == false fixture (room
        // light/invisibility state), which is out of scope for this
        // leaf-module wave. Trivial single-literal, no format args, no
        // aliasing -- low risk, deferred pending a lighter PERS-specific
        // fixture.
#if defined(__clang__)
#pragma clang diagnostic push
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#endif
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
        sprintf(name, "someone");
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

    if (capitalize)
        CAP(name);

    return name;
}

// has_alias() relocated to char_utils.cpp (placement-seam Task 5; census
// verdict MOVE-OTHER-L2): entity-pure player.name text match.
// Declaration unchanged in utils.h.

// has_program() relocated to char_utils.cpp (placement-seam Task 5;
// census verdict MOVE-OTHER-L2): entity-pure. Declaration unchanged in
// utils.h.

/* Returns the guardian type.  Returns INVALID_GUARDIAN if the mob is not a guardian.
 * guardian_mob is a data table defined in consts.cpp (rots_core); mob_index is the
 * mobile index table defined in db.cpp. Both are declared extern here rather than
 * pulled in via a shared header, matching the historical local-extern idiom this
 * function used before its relocation out of consts.cpp. */
int get_guardian_type(int race_number, const char_data* in_guardian_mob)
{
    extern struct index_data* mob_index;
    extern int guardian_mob[MAX_RACES][3];
    if (race_number >= MAX_RACES)
        return INVALID_GUARDIAN;

    int virtual_number = mob_index[in_guardian_mob->nr].virt;
    for (int guardian_type = AGGRESSIVE_GUARDIAN; guardian_type <= MYSTIC_GUARDIAN;
         ++guardian_type) {
        if (guardian_mob[race_number][guardian_type] == virtual_number) {
            return guardian_type;
        }
    }

    return INVALID_GUARDIAN;
}
