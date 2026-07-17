/* entity_lifecycle.cc */

// Carved out of db.cpp (Phase: db-split Task 2, spec Sec4a). Interim home for
// the shared char/obj lifecycle helpers used by BOTH world instantiation
// (db_world.cpp's read_mobile() -> clear_char()) and the persist store paths
// (db_players.cpp's store_to_char()/save_char() family -> make_char_data()/
// clear_char()/init_char()/free_char()). Neither the world (W) nor the
// persist (P) half owns this code, so the classification inventory gave it
// its own TU rather than forcing an arbitrary choice; a future rots_entity
// library member is the intended permanent home (see spec
// docs/superpowers/specs/2026-07-16-library-architecture-design.md Sec4a).
// db.h still declares every symbol here, so callers outside this file are
// unaffected.

#include "platdef.h"
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

// unistd.h doesn't exist on Windows (Phase 3 Task 5); everything this file uses
// from it (open()/close()/lstat()'s POSIX fd plumbing) lives inside the
// open_secure_temp_output_file()/write_text_file_atomically_clearing_stale_tmp()
// PREDEF_PLATFORM_LINUX branches below, which is why this include alone can be
// platform-gated without also needing to touch fcntl.h/sys/stat.h (both exist,
// in a reduced form, on the Windows CRT too, so they stay unconditional).
#if defined PREDEF_PLATFORM_LINUX
#include <unistd.h>
#elif defined PREDEF_PLATFORM_WINDOWS
// _sopen_s/_close (open_secure_temp_output_file) and GetFileAttributesA
// (write_text_file_atomically_clearing_stale_tmp) below -- Windows'
// equivalents of the <unistd.h> POSIX fd plumbing this file needs.
#include <io.h>
#endif

#include "color.h"
#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "limits.h"
#include "mail.h"
#include "mudlle.h"
#include "pkill.h"
#include "platform_compat.h"
#include "protos.h"
#include "spells.h"
#include "rots/persist/file_formats.h"
#include "rots/core/character.h"
#include "rots/core/object.h"
#include "rots/core/room.h"
#include "rots/core/descriptor.h"
#include "rots/core/types.h"
#include "utils.h"
#include "zone.h"

#include "account_cache.h"
#include "account_management.h"
#include "big_brother.h"
#include "char_utils.h"
#include "character_json.h"
#include "exploits_json.h"
#include "json_utils.h"
#include "text_view.h"
#include "player_file_finalize.h"
#include "skill_timer.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <iostream>
#include <iterator>
#include <new>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

// Cross-TU forward declarations for symbols init_char() below calls
// that are neither defined in this TU nor declared in db.h (db-split
// Task 2 fix-ups):
extern long top_idnum; // db_players.cpp -- init_char() assigns ch->specials2.idnum from ++top_idnum.
extern long race_affect[]; // consts.cpp -- init_char() reads race_affect[GET_RACE(ch)].


/************************************************************************
 *  procs of a (more or less) general utility nature			*
 ********************************************************************** */


// Releases the alias_list chain objsave.cpp's Crash_alias_load() builds
// (each node CREATE1()'d, each with a CREATE()'d .command string) and
// attaches to ch->specials.alias. Nothing else in the tree ever released
// this list -- every character with saved aliases leaked it on every
// free_char() (e.g. logout), a confirmed production leak (backlog T2,
// disclosed at Phase 5 T6 via sanitize.supp). NULL-safe: `list` is 0 for a
// freshly-cleared character and always 0 for mobs (structs.h: "aliases, 0
// for mobs"), so this is a no-op for both.
void free_alias_list(struct alias_list* list)
{
    while (list) {
        struct alias_list* next = list->next;
        RELEASE(list->command);
        RELEASE(list);
        list = next;
    }
}

// Deep-clones an alias_list chain (owned_alias_list's copy ctor/assignment,
// structs.h -- RAII T4). Mirrors do_alias()'s/Crash_alias_load()'s own node
// construction: each node CREATE1()'d, each .command CREATE()'d and NUL-
// terminated. In practice this only ever clones an empty (null) chain --
// NPCs never carry a real one (mob_proto's alias is always null; see
// structs.h's alias field comment) and no live PC char_data is ever
// whole-struct-copied -- but it is implemented as a real clone (not a
// shallow pointer copy) so char_special_data's copy assignment -- which
// `*mob = mob_proto[i]` (db.cpp's read_mobile(), the char_data whole-struct
// copy) relies on -- stays well-defined.
struct alias_list* owned_alias_list::clone(struct alias_list* src)
{
    struct alias_list* head = nullptr;
    struct alias_list* tail = nullptr;

    for (struct alias_list* node = src; node; node = node->next) {
        struct alias_list* copy;
        CREATE1(copy, alias_list);
        std::memcpy(copy->keyword, node->keyword, sizeof(copy->keyword));

        const size_t command_length = node->command ? strlen(node->command) : 0;
        CREATE(copy->command, char, command_length + 1);
        if (node->command)
            strcpy(copy->command, node->command);
        else
            copy->command[0] = '\0';

        copy->next = nullptr;
        if (!head)
            head = tail = copy;
        else {
            tail->next = copy;
            tail = copy;
        }
    }

    return head;
}

/* release memory allocated for a char struct */
void free_char(struct char_data* ch)
{
    clear_account_backed_object_bytes_for_character(ch);

    // RAII T6a: this function frees the char_data storage with a raw free()
    // (RELEASE(ch), below). Historically it ALSO ran ~char_data()'s work by
    // hand -- per-member reset()/move-assign-empty of every heap-owning member
    // (T3 skills/knowledge vectors, T4 alias, T5b poofIn/poofOut strings,
    // extra_specialization_data, damage_details). That hand teardown is now
    // subsumed by an explicit `ch->~char_data();` immediately before the free
    // (bottom of this function), giving the calloc/placement-new (clear_char,
    // read_mobile) a symmetric explicit-dtor/free teardown. See ownership-map.md
    // section 6.
    //
    // What must STILL be done by hand here, BEFORE ~char_data() runs, is
    // everything the implicit destructor does NOT (and must not) do:
    //   * the CONDITIONAL prototype-shared char* strings (name/title/descr/
    //     profs) -- freed only under the IS_NPC guard below, because for a
    //     normal NPC they alias mob_proto[nr] and the destructor leaving raw
    //     char* members untouched is exactly what prevents a double-free of the
    //     prototype;
    //   * the raw special-mob script pointers (special_stack/special_list_area/
    //     special_prog_number/special_prog_point) -- POD int*/long* with no
    //     destructor;
    //   * draining the `affected` spell-affect chain (pool-mediated, not an
    //     owned member the destructor knows about).
    // These read ch's members, so they run while the object is still alive.

    // RAII T5a: free the special-mob script buffers, now in their own typed
    // fields (were reinterpret_cast'd through poofIn/poofOut/union1/union2).
    // All null for PCs and ordinary mobs, so RELEASE (null-safe) is
    // unconditional. Releasing special_prog_number/special_prog_point here
    // fixes the ownership-map section 2c leak: pre-T5a these aliased
    // union1.prog_number / union2.prog_point, which free_char could not free
    // without risking a PC's reply_ptr/reply_number in the same union storage.
    RELEASE(ch->specials.special_stack);
    RELEASE(ch->specials.special_list_area);
    RELEASE(ch->specials.special_prog_number);
    RELEASE(ch->specials.special_prog_point);

    while (ch->affected)
        affect_remove(ch, ch->affected);

    if (!IS_NPC(ch) || (IS_NPC(ch) && ch->nr == -1)) {

        RELEASE(GET_NAME(ch));
        RELEASE(ch->player.title);
        RELEASE(ch->player.short_descr);
        RELEASE(ch->player.long_descr);
        RELEASE(ch->player.description);
        RELEASE(ch->profs);
    } /*  else if ((i = ch->nr) > -1) {
     if (ch->player.name && ch->player.name != mob_proto[i].player.name)
       RELEASE(ch->player.name);
     if (ch->player.title && ch->player.title != mob_proto[i].player.title)
       RELEASE(ch->player.title);
     if (ch->player.short_descr && ch->player.short_descr !=
   mob_proto[i].player.short_descr) RELEASE(ch->player.short_descr); if
   (ch->player.long_descr && ch->player.long_descr !=
   mob_proto[i].player.long_descr) RELEASE(ch->player.long_descr); if
   (ch->player.description && ch->player.description !=
   mob_proto[i].player.description) RELEASE(ch->player.description);
   } */

    // Diagnostic only (not teardown): a mob should never have carried a skills
    // array (clear_char only sizes skills/knowledge for mode != MOB_ISNPC). Log
    // the invariant violation while ch is still alive; the vector's heap buffer
    // is released by ~char_data() below like every other owning member.
    if (!ch->skills.empty() && IS_NPC(ch))
        log("SYSERR: Mob had skills array allocated!");

    remove_char_exists(ch->abs_number);

    // RAII T6a: symmetric teardown. clear_char()/read_mobile() construct this
    // object with a placement-new over calloc storage (new (ch) char_data());
    // the mirror image is an explicit destructor call followed by the raw
    // free. Running ~char_data() here destroys EVERY owning member --
    // skills/knowledge (std::vector, T3), specials.alias (owned_alias_list, T4),
    // specials.poofIn/poofOut (std::string, T5b), extra_specialization_data
    // (deletes current_spec_info) and damage_details (clears its std::map) --
    // which is exactly (and only) what the per-member reset()/move-assign-empty
    // lines removed above used to do by hand. Crucially it does NOT touch the
    // raw char* members (name/title/descr/profs, freed conditionally above; the
    // special-mob pointers, freed above), because those are POD pointers with
    // no destructor -- so a normal NPC whose strings alias the prototype is not
    // double-freed. RELEASE(ch) then frees the raw storage (gated by
    // global_release_flag exactly as before). Do NOT read *ch after this line.
    ch->~char_data();
    RELEASE(ch);
}

// RAII T6b: owning factory for a clean-scope char_data instance. Mirrors the
// canonical hand-written `CREATE(x, char_data, 1); clear_char(x, mode);`
// allocation (calloc storage + placement-new construction) and wraps the result
// in a char_data_ptr whose deleter is free_char -- so the whole
// allocate/construct/use/free lifecycle is single-owner and exception-safe.
// `mode` is passed straight to clear_char (MOB_VOID for a PC-shaped scratch
// char, MOB_ISNPC for a mob). See db.h for the world-graph caveat.
char_data_ptr make_char_data(int mode)
{
    struct char_data* ch;
    CREATE(ch, struct char_data, 1);
    clear_char(ch, mode);
    return char_data_ptr(ch);
}

/* release memory allocated for an obj struct */
void free_obj(struct obj_data* obj)
{
    int nr;
    struct extra_descr_data *thith, *next_one;

    if ((nr = obj->item_number) == -1) {
        RELEASE(obj->name);
        RELEASE(obj->description);
        RELEASE(obj->short_description);
        RELEASE(obj->action_description);
        if (obj->ex_description)
            for (thith = obj->ex_description; thith; thith = next_one) {
                next_one = thith->next;
                RELEASE(thith->keyword);
                RELEASE(thith->description);
                RELEASE(thith);
            }
    } /* else {
     if (obj->name && obj->name != obj_proto[nr].name)
       RELEASE(obj->name);
     if (obj->description && obj->description != obj_proto[nr].description)
       RELEASE(obj->description);
     if (obj->short_description && obj->short_description !=
   obj_proto[nr].short_description) RELEASE(obj->short_description); if
   (obj->action_description && obj->action_description !=
   obj_proto[nr].action_description) RELEASE(obj->action_description); if
   (obj->ex_description && obj->ex_description != obj_proto[nr].ex_description)
       for (thith = obj->ex_description; thith; thith = next_one) {
         next_one = thith->next;
         RELEASE(thith->keyword);
         RELEASE(thith->description);
         RELEASE(thith);
       }
   } */

    RELEASE(obj);
}

/* clear some of the the working variables of a char */
void reset_char(struct char_data* ch)
{
    int i;

    for (i = 0; i < MAX_WEAR; i++) /* Initialisering */
        ch->equipment[i] = 0;

    ch->followers = 0;
    ch->master = 0;
    ch->next_die = 0;
    ch->carrying = 0;
    ch->next = 0;
    ch->next_fighting = 0;
    ch->next_in_room = 0;
    ch->specials.fighting = 0;
    ch->specials.position = POSITION_STANDING;
    ch->specials.default_pos = POSITION_STANDING;
    ch->specials.carry_weight = 0;
    ch->specials.carry_items = 0;
    ch->specials.was_in_room = -1;

    if (GET_HIT(ch) <= 0)
        GET_HIT(ch) = 1;
    if (GET_MOVE(ch) <= 0)
        GET_MOVE(ch) = 1;
    if (GET_MANA(ch) <= 0)
        GET_MANA(ch) = 1;
}

/* clear ALL the working variables of a char and do NOT free any space
 * alloc'ed*/
void clear_char(struct char_data* ch, int mode)
{
    /* At every production call site, ch points to memory obtained via
     * CREATE()/calloc (raw, unconstructed storage), never to a char_data that has
     * already run its constructor. Placement-new value-initializes it in place:
     * this zeroes every POD member exactly like the old memset did, but also
     * properly constructs the non-trivial members (player_damage_details::damage_map
     * is a std::map; specialization_data has a user destructor) instead of leaving
     * them as zeroed-but-never-constructed memory, which is undefined behavior the
     * moment those members are used (deterministic SIGSEGV under libc++/macOS;
     * silently tolerated by libstdc++/Linux). See db.cpp read_mobile() for the
     * other call path that needs the same treatment.
     * (Test code also calls clear_char() directly on already-constructed stack
     * `char_data` objects to reset them between cases; that's safe in practice here
     * because the non-trivial members are always empty at that point, so
     * re-running their default constructors via this placement-new has nothing to
     * leak — but it's not the shape this function's placement-new was written for,
     * and isn't a pattern to extend to types where re-construction over a live
     * object could leak or double-free.) */
    new (ch) char_data();
    CREATE1(ch->profs, char_prof_data);
    memset(ch->profs->colors, CNRM, sizeof(ch->profs->colors[0]) * MAX_COLOR_FIELDS);

    ch->specials.alias = 0;
    ch->in_room = NOWHERE;
    ch->specials.was_in_room = NOWHERE;
    ch->specials.position = POSITION_STANDING;
    ch->specials.default_pos = POSITION_STANDING;
    SET_TACTICS(ch, TACTICS_NORMAL);
    SET_SHOOTING(ch, SHOOTING_NORMAL);
    SET_CASTING(ch, CASTING_NORMAL);
    ch->specials.script_info = 0;
    ch->specials.script_number = 0;
    ch->specials2.rp_flag = 0;
    ch->specials2.retiredon = 0;
    ch->specials2.hide_flags = 0;
    utils::set_specialization(*ch, game_types::PS_None);

    SET_DODGE(ch) = 0; /* Basic Armor */
    if (ch->abilities.mana < 100)
        ch->abilities.mana = 100;

    if (mode != MOB_ISNPC) {
        ch->skills.assign(MAX_SKILLS, 0);
        ch->knowledge.assign(MAX_SKILLS, 0);
        if (ch->desc)
            memset(ch->desc->pwd, 0, MAX_PWD_LENGTH);
    }
}

void clear_object(struct obj_data* obj)
{
    memset((char*)obj, 0, (size_t)sizeof(struct obj_data));

    obj->item_number = -1;
    obj->in_room = NOWHERE;
    obj->obj_flags.timer = -1;
    obj->obj_flags.script_info = 0;
}

/* initialize a new character only if prof is set */
void init_char(struct char_data* ch)
{
    int i;

    set_title(ch);

    ch->player.short_descr = 0;
    ch->player.long_descr = 0;
    ch->player.description = 0;

    ch->player.hometown = number(1, 4);

    ch->player.time.birth = time(0);
    ch->player.time.played = 0;
    ch->player.time.logon = time(0);

    for (i = 0; i < MAX_TOUNGE; i++)
        ch->player.talks[i] = 0;

    SET_STR(ch, 9);
    GET_INT(ch) = 9;
    GET_WILL(ch) = 9;
    GET_DEX(ch) = 9;
    GET_CON(ch) = 9;

    /* make favors for sex */
    ch->player.weight = get_race_weight(ch) * (85 + number(0, 30)) / 100;
    ch->player.height = get_race_height(ch) * (85 + number(0, 30)) / 100;

    ch->abilities.mana = 100;
    ch->tmpabilities.mana = GET_MAX_MANA(ch);
    ch->tmpabilities.hit = GET_MAX_HIT(ch);
    ch->abilities.move = 82;
    ch->tmpabilities.move = GET_MAX_MOVE(ch);
    GET_DODGE(ch) = 0;

    ch->specials2.idnum = ++top_idnum;

    if (ch->skills.empty())
        ch->skills.assign(MAX_SKILLS, 0);

    for (i = 0; i < MAX_SKILLS; i++) {
        SET_SKILL(ch, i, 0);
        SET_KNOWLEDGE(ch, i, 0);
    }

    ch->specials.affected_by = race_affect[GET_RACE(ch)];

    ch->specials2.saving_throw = 0;
    ch->specials2.rp_flag = 0;

    for (i = 0; i < 3; i++)
        GET_COND(ch, i) = (GET_LEVEL(ch) == LEVEL_IMPL ? -1 : 24);

    ch->damage_details.reset();

    /* The default preference flags */
    PRF_FLAGS(ch) |= PRF_SPAM | PRF_NARRATE | PRF_CHAT | PRF_WIZ | PRF_SING | PRF_PROMPT | PRF_ECHO | PRF_SPINNER;
}
