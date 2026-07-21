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
#include "script_hooks.h"
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

// get_real_OB()/get_real_parry() relocated to visibility.cpp
// (blocker-buster Task 4; census section A verdict MOVE-L3-COMBAT,
// retiring their placement-seam Task 5 STAY-APP comments): the
// player_spec::weapon_master_handler dependency that kept them app-tier
// now resolves in-lib -- weapon_master_handler.cpp has been a
// rots_combat seed TU since combat-seed Task 1 (ROTS_COMBAT_SOURCES,
// src/CMakeLists.txt), so the reference is an intra-lib peer call, not
// an app edge. The OB/parry/dodge trio now reunites across rots_entity
// (get_real_dodge(), char_utils_combat.cpp, placement-seam Task 5) and
// rots_combat (get_real_OB()/get_real_parry(), here) -- blocker-buster
// Task 5 updates AGENTS.md's Dead/Unused-Code trio paragraph. Both
// bodies moved verbatim (no world[] touch in either). Declarations
// unchanged in utils.h.

// get_real_dodge() relocated to char_utils_combat.cpp (placement-seam
// Task 5; census verdict MOVE-OTHER-L2): entity-pure, the cleanest of the
// OB/parry/dodge trio (no weapon_master, no world[]) -- see get_real_OB()/
// get_real_parry() above; the trio now reunites entirely in library code,
// split across the L2 (here)/L3 (visibility.cpp, rots_combat) tier line,
// not STAY-APP. Declaration
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

// real_time_passed()/mud_time_passed()/age() relocated to consts.cpp
// (combat-seed Task 2; placement-seam deferral rider -- see
// task-5-report.md's BLOCKING FINDING entries, now resolved): the
// first two return struct time_info_data (rots/core/types.h, L1) BY
// VALUE, which rots_core may do directly; age() cascades via
// mud_time_passed() and moves alongside it. Declarations unchanged in
// utils.h (mud_time_passed(), age()); real_time_passed() has no header
// declaration -- act_info.cpp keeps its own pre-existing local extern
// forward declaration, unchanged.

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

// CAN_SEE(char_data*) (the 1-arg light-only overload) relocated to
// visibility.cpp (blocker-buster Task 4; census section A verdict
// MOVE-L3-COMBAT): weather_info is a legal L3-world peer reference; its
// unchecked IS_LIGHT/OUTSIDE world[] reads (utils.h) resolve via
// room_by_id_total() per the BINDING addendum's resolver-variant rule --
// see task-4-report.md for the exact before/after quote. Declaration
// unchanged in utils.h.

// CAN_SEE(sub, obj, light_mode) (the 3-arg light/hiding overload) relocated
// to visibility.cpp (blocker-buster Task 4b; census section A, completing
// Task 4's split): its see_hiding(sub) call now resolves in-lib -- Task 4b
// Step 1(a)'s mini-census carved see_hiding() out of ranger.cpp into
// visibility.cpp (entity-pure, zero other ranger.cpp dependency). Its one
// unchecked world[] site (the IS_LIGHT((obj)->in_room)/OUTSIDE(sub) macro
// expansions) resolves via two hoisted room_by_id_total() pointers (obj's
// room for IS_LIGHT, sub's room for OUTSIDE) per the BINDING addendum's
// resolver-variant rule; weather_info/act()/number() are the same legal
// peer/seam/L0 refs Task 4 already established for the 1-arg overload.
// Declaration unchanged in utils.h. See task-4b-report.md for the full
// evidence.
// can_sense() relocated to char_utils_combat.cpp (placement-seam Task 5;
// census verdict MOVE-OTHER-L2): entity-pure GET_* macro logic.
// Declaration unchanged in utils.h.

// CAN_SEE_OBJ() relocated to visibility.cpp (blocker-buster Task 4;
// census section A verdict MOVE-L3-COMBAT): rides the moved CAN_SEE(sub)
// 1-arg overload, no direct world[]/weather_info touch of its own.
// Declaration unchanged in utils.h.

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

// day_to_str() relocated to consts.cpp (combat-seed Task 2;
// placement-seam deferral rider -- see task-5-report.md's BLOCKING
// FINDING, now resolved): reads month_name[] (consts.cpp, rots_core/L1)
// and takes struct time_info_data* (rots/core/types.h, L1) by pointer,
// both directly referenceable from rots_core. Still calls nth()
// (rots_util.cpp, L0) -- L1 calling L0 is downward and fine.
// Declaration unchanged in utils.h.

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

// set_mental_delay() relocated verbatim to fight.cpp (combat-pilot
// wave Task 4a; pilot-census.md section 3.2): it calls set_fighting(),
// defined in fight.cpp -- staying here would create an L2->L3-combat
// upward edge once fight.cpp joins rots_combat. Declaration unchanged
// (utils.h:739).

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

// Registers the real PERS() body above as script_hooks.h's PERS hook
// (l4-seed wave, Task 1; l4-task-1-brief.md Step 2c; l4-census.md section
// 3.2). Called once from run_the_game(), before boot_db() -- same
// convention as this file's other registrars.
void register_pers_hook()
{
    rots::script::set_pers_hook(PERS);
}

// has_alias() relocated to char_utils.cpp (placement-seam Task 5; census
// verdict MOVE-OTHER-L2): entity-pure player.name text match.
// Declaration unchanged in utils.h.

// has_program() relocated to char_utils.cpp (placement-seam Task 5;
// census verdict MOVE-OTHER-L2): entity-pure. Declaration unchanged in
// utils.h.

// get_guardian_type() relocated to visibility.cpp (rots_combat, L3;
// combat-trio wave Task 3): reads mob_index[] (db_world.cpp, L3-world)
// and guardian_mob[] (consts.cpp, L1-core) -- both legal downward/peer
// references once inside rots_combat, which PUBLIC-links RotS::world
// and RotS::core (the same L3-peer-reference precedent visibility.cpp's
// own weather_info comment already establishes). Declaration unchanged
// in utils.h.
