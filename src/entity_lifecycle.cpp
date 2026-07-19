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
//
// db-split Task 4b added a second tenant: the affect / derived-ability
// engine (affect_modify/affect_naked/apply_gear_affects/modify_affects/
// affect_total/affect_to_char/affect_remove/affected_by_spell, relocated
// from handler.cpp; recalc_abilities/do_squareroot, from profs.cpp;
// get_race_perception/get_naked_perception/get_naked_willpower/
// get_confuse_modifier/encrypt_line/decrypt_line, from utility.cpp). Like
// the lifecycle helpers above, both store_to_char() (via affect_to_char())
// and char_to_store() (via affect_total()) call into this engine for every
// character, so it belongs to neither the world nor the persist half
// either -- and, same as the lifecycle helpers, it was previously
// hand-duplicated in convert_stubs.cpp so rots_convert could link without
// pulling in the whole handler.cpp/profs.cpp/utility.cpp TUs. Moving the
// real bodies here restores a single definition per symbol that both
// ageland and rots_convert link, per the review adjudication recorded in
// docs/superpowers/plans/2026-07-17-db-split-and-rots-convert.md's "Task
// 4b" section. See that section for the two helpers that stay in their
// origin TUs instead (shared with a sibling function outside this
// relocation) and this file's own affect-engine section comment below for
// the full rationale.

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
#include "warrior_spec_handlers.h"
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
#include "entity_hooks.h"
#include "player_file_finalize.h"
#include "rots/platform/log.h"
#include "skill_timer.h"
#include <cmath>
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
long top_idnum = 0; // moved here from db_players.cpp (entity-seed Task 5,
                    // storage-placement only); init_char() assigns
                    // ch->specials2.idnum from ++top_idnum below.
                    // db_players.cpp keeps reading/writing it via extern.
extern long race_affect[]; // consts.cpp -- init_char() reads race_affect[GET_RACE(ch)].
extern byte language_number; // consts.cpp -- recalc_skills() below (relocated PS Task 4).
extern byte language_skills[]; // consts.cpp -- recalc_skills() below (relocated PS Task 4).

/************************************************************************
 *  entity_hooks.h dispatch (entity-seed Task 5, +EC Task 2)            *
 ************************************************************************
 *  Backing storage + null-defaulted dispatch helpers for the four upward
 *  edges entity_hooks.h inverts (spec Sec13 pattern, mirroring
 *  output_seam.cpp): free_char()'s teardown notification (objsave.cpp
 *  registers the real clear_account_backed_object_bytes_for_character()),
 *  recalc_abilities()'s weapon-master attack-speed query
 *  (wild_fighting_handler.cpp registers the real
 *  player_spec::weapon_master_handler-backed implementation),
 *  get_energy_regen()'s wild-fighting attack-speed query (char_utils.cpp;
 *  wild_fighting_handler.cpp registers the real
 *  player_spec::wild_fighting_handler-backed implementation), and
 *  on_attacked_character()'s big-brother PK notification
 *  (char_utils_combat.cpp; big_brother.cpp registers a forwarder to
 *  game_rules::big_brother::instance().on_character_attacked_player()). All
 *  four registered by run_the_game(), before boot_db() -- see
 *  entity_hooks.h. The last two are dispatched cross-TU (char_utils.cpp /
 *  char_utils_combat.cpp), so their dispatch helpers below are NOT in the
 *  anonymous namespace the first two use -- see entity_hooks.h's
 *  declarations for why.
 ************************************************************************/
namespace rots::entity {

namespace {
// Backing storage for the registered char-teardown hook
// (register_char_teardown_hook(), objsave.cpp). Null until that
// registration runs; a null hook is a silent, provable no-op -- see
// dispatch_char_teardown() below, which does not log.
char_teardown_fn g_char_teardown_hook = nullptr;

// Backing storage for the registered attack-speed-multiplier hook
// (register_attack_speed_multiplier_hook(), wild_fighting_handler.cpp).
// Null until that registration runs; the null default reproduces
// rots_convert's historical player_spec::weapon_master_handler stub
// (tripwire log + a neutral 1.0f multiplier).
attack_speed_fn g_attack_speed_multiplier_hook = nullptr;

// Backing storage for the registered wild-fighting attack-speed hook
// (register_wild_attack_speed_multiplier_hook(), wild_fighting_handler.cpp).
// Null until that registration runs; the null default reproduces
// convert_stubs.cpp's now-deleted player_spec::wild_fighting_handler stub
// (tripwire log + a neutral 1.0f multiplier).
wild_attack_speed_fn g_wild_attack_speed_multiplier_hook = nullptr;

// Backing storage for the registered attacked-player (big-brother PK
// notification) hook (register_attacked_player_hook(), big_brother.cpp).
// Null until that registration runs; the null default is a tripwire no-op
// (combat never runs before run_the_game's registrations in ageland).
attacked_player_fn g_attacked_player_hook = nullptr;

// Backing storage for the registered txt-block-pool hook pair
// (register_txt_block_pool_hooks(), comm.cpp; world-seed Task 2). Null
// until that registration runs; unlike the hooks above, the null default
// is a hard failure (see dispatch_get_txt_block_from_pool() below) rather
// than a safe placeholder value.
get_txt_block_fn g_get_txt_block_pool_hook = nullptr;
put_txt_block_fn g_put_txt_block_pool_hook = nullptr;
} // namespace

void set_char_teardown_hook(char_teardown_fn hook)
{
    g_char_teardown_hook = hook;
}

void set_attack_speed_multiplier_hook(attack_speed_fn hook)
{
    g_attack_speed_multiplier_hook = hook;
}

void set_wild_attack_speed_multiplier_hook(wild_attack_speed_fn hook)
{
    g_wild_attack_speed_multiplier_hook = hook;
}

void set_attacked_player_hook(attacked_player_fn hook)
{
    g_attacked_player_hook = hook;
}

void set_get_txt_block_pool_hook(get_txt_block_fn hook)
{
    g_get_txt_block_pool_hook = hook;
}

void set_put_txt_block_pool_hook(put_txt_block_fn hook)
{
    g_put_txt_block_pool_hook = hook;
}

namespace {
void dispatch_char_teardown(const char_data* character)
{
    if (g_char_teardown_hook) {
        g_char_teardown_hook(character);
        return;
    }
    // Null default is a silent no-op: the staged-object map cleared by the old stub
    // (in the deleted convert_stubs.cpp) can only gain entries via interpre.cpp's login flow.
    // That flow never runs at all in rots_convert; in ageland it only runs after
    // register_char_teardown_hook() has registered the real hook. Erasing a never-inserted
    // key is equivalent to no-op.
}

float dispatch_attack_speed_multiplier(char_data* character)
{
    if (g_attack_speed_multiplier_hook) {
        return g_attack_speed_multiplier_hook(character);
    }
    rots::log::write_stderr(
        "rots::entity: STUB attack-speed-multiplier hook called with no sink registered -- this "
        "should be unreachable once register_attack_speed_multiplier_hook() has run.");
    return 1.0f;
}

// target_data::cleanup()/operator=()'s txt-block-pool GET, dispatched
// through the hook registered by register_txt_block_pool_hooks()
// (comm.cpp). Unlike the float-returning hooks above, an unregistered hit
// here is a hard failure (loud log + abort) rather than a safe fallback:
// the caller immediately dereferences the returned pointer
// (ptr.text->text), so a silently-returned null would surface as a
// confusing null-deref far from the real cause. comm.cpp registers the
// real pool function in run_the_game() before boot_db(), so ageland never
// reaches this path; rots_convert links rots_entity but never copies a
// TARGET_TEXT target (same class of "unreachable there too" as this
// header's other tripwire hooks).
struct txt_block* dispatch_get_txt_block_from_pool()
{
    if (g_get_txt_block_pool_hook) {
        return g_get_txt_block_pool_hook();
    }
    rots::log::write_stderr(
        "rots::entity: FATAL txt-block-pool GET hook called with no sink registered -- this "
        "should be unreachable once register_txt_block_pool_hooks() has run.");
    abort();
}

// target_data::cleanup()'s txt-block-pool PUT, dispatched through the same
// hook pair for the same reason (a discarded-without-registration txt_block
// would otherwise leak forever, silently, rather than fail loudly).
void dispatch_put_txt_block_to_pool(struct txt_block* block)
{
    if (g_put_txt_block_pool_hook) {
        g_put_txt_block_pool_hook(block);
        return;
    }
    rots::log::write_stderr(
        "rots::entity: FATAL txt-block-pool PUT hook called with no sink registered -- this "
        "should be unreachable once register_txt_block_pool_hooks() has run.");
    abort();
}
} // namespace

// Cross-TU dispatch for the wild-fighting attack-speed hook (called from
// char_utils.cpp's get_energy_regen(); see entity_hooks.h). External linkage
// -- unlike dispatch_attack_speed_multiplier() above, this cannot live in the
// anonymous namespace.
float dispatch_wild_attack_speed_multiplier(const char_data* character)
{
    if (g_wild_attack_speed_multiplier_hook) {
        return g_wild_attack_speed_multiplier_hook(character);
    }
    rots::log::write_stderr(
        "rots::entity: STUB wild-attack-speed-multiplier hook called with no sink registered -- this "
        "should be unreachable once register_wild_attack_speed_multiplier_hook() has run.");
    return 1.0f;
}

// Cross-TU dispatch for the attacked-player (big-brother PK notification)
// hook (called from char_utils_combat.cpp's on_attacked_character(); see
// entity_hooks.h). External linkage, same reason as
// dispatch_wild_attack_speed_multiplier() above.
void dispatch_attacked_player(const char_data* attacker, const char_data* attacked)
{
    if (g_attacked_player_hook) {
        g_attacked_player_hook(attacker, attacked);
        return;
    }
    rots::log::write_stderr(
        "rots::entity: STUB attacked-player hook called with no sink registered -- this should be "
        "unreachable once register_attacked_player_hook() has run.");
}

} // namespace rots::entity

/************************************************************************
 *  target_data member functions (relocated verbatim from interpre.cpp,   *
 *  world-seed Task 2 adjudication) -- char_data's implicitly-generated   *
 *  copy-assignment (db_world.cpp's read_mobile(): `*mob = mob_proto[i];`)*
 *  does a member-wise copy that ODR-uses target_data::operator=() through*
 *  the special_list::field[SPECIAL_STACKLEN] array embedded in           *
 *  char_special_data, even though db_world.cpp never spells the type     *
 *  name "target_data" -- confirmed via `nm -u` on db_world.cpp.o, which  *
 *  showed operator=() as an undefined symbol. Declarations stay in       *
 *  rots/core/types.h (core/L1, already included by this TU); only the    *
 *  three out-of-line bodies move here. cleanup()/operator=()'s two pool  *
 *  calls (previously direct calls to comm.cpp's get_from_txt_block_pool/ *
 *  put_to_txt_block_pool) now go through this TU's own dispatch helpers  *
 *  above -- comm.cpp is not a leaf module (its pool storage is entangled *
 *  with descriptor/output-buffer machinery), so the edge is inverted via *
 *  entity_hooks.h instead of relocating the pool itself.                 *
 ************************************************************************/
void target_data::cleanup()
{
    if (type == TARGET_TEXT)
        rots::entity::dispatch_put_txt_block_to_pool(ptr.text);
    ptr.other = 0;
    type = TARGET_NONE;
    ch_num = 0;
}

void target_data::operator=(const target_data& t2)
{
    cleanup();
    if (t2.type == TARGET_TEXT) {
        ptr.text = rots::entity::dispatch_get_txt_block_from_pool();
        strcpy(ptr.text->text, t2.ptr.text->text);
    } else
        ptr.other = t2.ptr.other;

    type = t2.type;
    ch_num = t2.ch_num;
    choice = t2.choice;
}

int target_data::operator==(const target_data& t2) const
{
    if ((type == t2.type) && (ptr.other == t2.ptr.other) && (ch_num == t2.ch_num))
        return 1;

    return 0;
}

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

// db_boot.cpp -- character_list/object_list's DEFINITIONS move here
// (storage-placement only; world-seed Task 1): they are lists OF
// entities, and this file already owns free_char()/free_obj() (below),
// the functions that unlink nodes from them. No shared header declares
// either one (verified across the tree) -- every consuming TU keeps its
// own local `extern` re-declaration, unaffected by this move.
struct char_data* character_list = 0; /* global linked list of chars	*/

struct obj_data* object_list = 0; /* the global linked list of objs	*/

/* release memory allocated for a char struct */
void free_char(struct char_data* ch)
{
    rots::entity::dispatch_char_teardown(ch);

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

/************************************************************************
 *  Entity-tier leaf helpers (entity-seed Task 5)                      *
 ************************************************************************
 *  Relocated verbatim from handler.cpp (get_from_affected_type_pool()/
 *  put_to_affected_type_pool() + their affected_type_pool/
 *  affected_type_counter backing state; the affected_list/
 *  affected_list_pool globals, shared with affect_to_room()/
 *  affect_remove_room(), which stay in handler.cpp and now reach these by
 *  extern; char_exists()/set_char_exists()/remove_char_exists() +
 *  char_control_array; isname_nullable() + its sole file-local helper
 *  isname_c_string()), utility.cpp (pool_to_list()/from_list_to_pool() +
 *  universal_list_counter/used_in_universal_list; get_race_weight()/
 *  get_race_height(); get_current_time_phase()), profs.cpp (class_HP() --
 *  kept as a strong, non-inline definition, matching its post-db-split-
 *  Task-4b linkage; see that task's IFNDR history), limits.cpp
 *  (set_title()), and char_utils.cpp (utils::get_specialization()/
 *  utils::set_specialization()/utils::get_minimum_insight_perception(),
 *  specialization_data::reset()). int pulse's DEFINITION (formerly
 *  comm.cpp) and long top_idnum's DEFINITION (formerly db_players.cpp,
 *  see this file's top) also move here -- storage-placement only, both
 *  origin TUs keep mutating the value via extern. Bodies, comments, and
 *  local quirks are unchanged from their origin files; only the enclosing
 *  TU differs. Declarations are unchanged in their origin headers (see
 *  each origin file's own relocation-marker comment).
 ************************************************************************/

// handler.cpp -- live-tick affect-duration bookkeeping list + its pool,
// shared with affect_to_room()/affect_remove_room() (handler.cpp, which now
// reach these two globals by extern).
universal_list* affected_list = 0;
universal_list* affected_list_pool = 0;

// handler.cpp -- private backing state for get_from_affected_type_pool()/
// put_to_affected_type_pool() immediately below; no other handler.cpp
// function reads these two, so they move with the pool functions rather
// than staying behind as an extern.
affected_type* affected_type_pool = 0;
int affected_type_counter = 0;

/*  If there is a structure of affected_type in the affected_type_pool list then
        it is removed, if not then one is CREATEd.  A pointer to an available affected_type
        structure is returned to be applied to a character or room. */

struct affected_type* get_from_affected_type_pool()
{
    struct affected_type* afnew;

    if (affected_type_pool) {
        afnew = affected_type_pool;
        affected_type_pool = afnew->next;

        memset(afnew, 0, sizeof(affected_type));
    } else {
        CREATE(afnew, struct affected_type, 1);
        affected_type_counter++;
    }
    return afnew;
}

/* Puts a struct affected_type into the head of the pool.
 ** Replaced with free at the moment to aid bughunting. */

void put_to_affected_type_pool(struct affected_type* oldaf)
{

    free(oldaf);
    //  oldaf->next = affected_type_pool;
    //  affected_type_pool = oldaf;
}

// handler.cpp -- char_exists()/set_char_exists()/remove_char_exists() bit-
// array bookkeeping. register_npc_char() (below, world-seed Task 1) now
// calls these directly (same TU); register_pc_char() (this file too, below
// -- placement-seam Task 4, a one-line forwarder onto register_npc_char())
// and utils::is_riding()/is_ridden() (utils.h macros) still reach these by
// extern/declaration.
char char_control_array[MAX_CHARACTERS / 8 + 1];

int char_exists(int num)
{
    return (char_control_array[num / 8] & (1 << (num % 8)));
}
void set_char_exists(int num)
{
    char_control_array[num / 8] |= (1 << (num % 8));
}
void remove_char_exists(int num)
{
    char_control_array[num / 8] &= ~(1 << (num % 8));
}

// handler.cpp -- register_npc_char() (+ its only global, last_control_set)
// relocated here (world-seed Task 1): pure abs_number allocation over the
// char_exists() bit-array above, which already lives in this file (entity-
// seed Task 5) -- register_npc_char() no longer needs to reach it by
// extern. register_pc_char() (handler.cpp), a one-line forwarder onto
// register_npc_char(), stays in handler.cpp and calls down; declarations
// for both remain unchanged in handler.h.
long last_control_set = -1;

int register_npc_char(struct char_data* mob)
{
    int i, flag;

    if (!mob) {
        log("register_char: zero char passed.");
        return -1;
    }
    flag = 0;
    for (i = last_control_set + 1; i < MAX_CHARACTERS; i++)
        if (!char_exists(i))
            break;
    if (i == MAX_CHARACTERS) {
        flag = 1;
        for (i = 0; i <= last_control_set; i++)
            if (!char_exists(i))
                break;
    }
    if (flag && (i > last_control_set)) {
        log("register_char: MUD IS OVERFLOWED.");
        exit(0);
    }
    set_char_exists(i);
    mob->abs_number = i;
    last_control_set = i;

    return i;
}

namespace {
// handler.cpp -- isname_nullable()'s sole helper (originally file-local to
// handler.cpp's own anonymous namespace); moved alongside it (its only
// caller), kept file-local here too -- same precedent as this file's
// do_squareroot() (db-split Task 4b), a small helper with exactly one
// caller.

// Nullable legacy callers already provide null-terminated strings. Keeping their sentinel walk
// here avoids constructing two views (and then scanning both again for first-null normalization)
// on this hot lookup path, while the public view overload retains bounded-input safety.
int isname_c_string(const char* query, const char* name_list, char full)
{
    while (*query && *query <= ' ') {
        ++query;
    }
    const std::size_t query_length = std::strlen(query);
    if (query_length == 0) {
        return 0;
    }
    if ((query_length < 3) || (query_length > 4)) {
        full = 1;
    }

    const char* current_name = name_list;
    for (;;) {
        const char* current_query = query;
        for (;;) {
            if (!*current_query
                && (!full || !std::isalpha(static_cast<unsigned char>(*current_name)))) {
                return 1;
            }
            if (!*current_name) {
                return 0;
            }
            if (!*current_query || *current_name == ' '
                || LOWER(*current_query) != LOWER(*current_name)) {
                break;
            }
            ++current_query;
            ++current_name;
        }

        while (std::isalpha(static_cast<unsigned char>(*current_name))) {
            ++current_name;
        }
        if (!*current_name) {
            return 0;
        }
        while (*current_name
            && (!std::isalpha(static_cast<unsigned char>(*current_name)) || *current_name == ' ')) {
            ++current_name;
        }
    }
}
} // namespace

int isname_nullable(const char* query, const char* name_list, char full)
{
    if (query == nullptr || name_list == nullptr) {
        return 0;
    }
    return isname_c_string(query, name_list, full);
}

// utility.cpp -- universal_list bookkeeping counters + pool_to_list()/
// from_list_to_pool(). handler.cpp's affect_to_room()/affect_remove_room(),
// db_world.cpp, limits.cpp, and act_wiz.cpp still reach these two functions
// (and affected_list/affected_list_pool above) via utils.h's declarations /
// their own local externs.
int universal_list_counter = 0;
int used_in_universal_list = 0;

/*
 * Takes the address of a linked list of universal_list structures
 * and its associated pool list and adds a new item at the beginning.
 * If a free universal_list structure is available in the pool it is
 * removed and returned.  If not, a new structure is created and
 * returned. Counts are kept of the number of universal list
 * structures created and the number in current use.
 */

struct universal_list*
pool_to_list(struct universal_list** list, struct universal_list** head)
{
    struct universal_list* tmplist;

    if (*head) {
        tmplist = *head;
        *head = tmplist->next;
        used_in_universal_list++;
    } else {
        CREATE1(tmplist, universal_list);
        universal_list_counter++;
        used_in_universal_list++;
    }

    tmplist->next = *list;
    *list = tmplist;

    return tmplist;
}

/*
 * Takes a list, its associated pool and a member of the list
 * and removes it from the list and adds it to the head of the
 * pool
 */
void from_list_to_pool(universal_list** list, universal_list**, universal_list* body)
{
    if (*list == body) {
        *list = body->next;
    } else {
        universal_list* tmplist = NULL;
        for (tmplist = *list; tmplist->next; tmplist = tmplist->next) {
            if (tmplist->next == body) {
                break;
            }
        }

        if (tmplist->next == body) {
            tmplist->next = body->next;
        }
    }

    /* Thus not putting universal lists into a pool, but freeing the memory */
    used_in_universal_list--;
    universal_list_counter++; /* added because we are freeing body */

    free(body);
}

// comm.cpp -- int pulse's DEFINITION moves here (storage-placement only);
// comm.cpp keeps mutating it via extern (game_loop()'s per-tick
// increment/reset).
int pulse = 0; // moved here from being a local variable

// utility.cpp
char get_current_time_phase()
{
    extern int pulse;

    return (pulse % (SECS_PER_MUD_HOUR * 4)) / PULSE_FAST_UPDATE;
}

// utility.cpp
int get_race_weight(struct char_data* ch)
{
    int gender_mod;

    if (GET_SEX(ch) == SEX_FEMALE)
        gender_mod = 8;
    else
        gender_mod = 10;

    switch (GET_RACE(ch)) {
    case RACE_GOD:
        return 100000 * gender_mod / 10;

    case RACE_HUMAN:
        return 17000 * gender_mod / 10;

    case RACE_DWARF:
        return 20000 * gender_mod / 10;

    case RACE_WOOD:
        return 12000 * gender_mod / 10;

    case RACE_HOBBIT:
        return 7000 * gender_mod / 10;

    case RACE_HIGH:
        return 13000 * gender_mod / 10;

    case RACE_URUK:
        return 16000 * gender_mod / 10;

    case RACE_HARAD:
        return 17000 * gender_mod / 10;

    case RACE_ORC:
        return 9000 * gender_mod / 10;

    case RACE_EASTERLING:
        return 17000 * gender_mod / 10;

    case RACE_MAGUS:
        return 16000 * gender_mod / 10;

    case RACE_TROLL:
        return 80000 * gender_mod / 10;

    case RACE_BEORNING:
        return 80000 * gender_mod / 10;

    case RACE_OLOGHAI:
        return 40000 * gender_mod / 10;

    case RACE_HARADRIM:
        return 17000 * gender_mod / 10;

    case RACE_UNDEAD:
        return 5000 * gender_mod / 10;

    default:
        return 15000;
    }

    return 0;
}

int get_race_height(struct char_data* ch)
{
    int gender_mod;

    if (GET_SEX(ch) == SEX_FEMALE)
        gender_mod = 9;
    else
        gender_mod = 10;

    switch (GET_RACE(ch)) {
    case RACE_GOD:
        return 200 * gender_mod / 10;

    case RACE_HUMAN:
        return 180 * gender_mod / 10;

    case RACE_DWARF:
        return 130 * gender_mod / 10;

    case RACE_WOOD:
        return 200 * gender_mod / 10;

    case RACE_HOBBIT:
        return 110 * gender_mod / 10;

    case RACE_HIGH:
        return 210 * gender_mod / 10;

    case RACE_URUK:
        return 170 * gender_mod / 10;

    case RACE_HARAD:
        return 180 * gender_mod / 10;

    case RACE_ORC:
        return 120 * gender_mod / 10;

    case RACE_EASTERLING:
        return 180 * gender_mod / 10;

    case RACE_MAGUS:
        return 170 * gender_mod / 10;

    case RACE_TROLL:
        return 225 * gender_mod / 10;

    case RACE_UNDEAD:
        return 180 * gender_mod / 10;

    case RACE_HARADRIM:
        return 180 * gender_mod / 10;

    case RACE_BEORNING:
        return 225 * gender_mod / 10;

    case RACE_OLOGHAI:
        return 200 * gender_mod / 10;

    default:
        return 200;
    }

    return 0;
}

// profs.cpp -- class_HP(); strong (non-inline) definition preserved (see
// db-split Task 4b's IFNDR history). profs.cpp's
// _INTERNAL::stat_assigner::organize() still calls this by extern.
int class_HP(const char_data* character)
{
    double hp_coofs = 3 * utils::get_prof_points(PROF_WARRIOR, *character) + 2 * utils::get_prof_points(PROF_RANGER, *character) + utils::get_prof_points(PROF_CLERIC, *character);

    if (GET_RACE(character) == RACE_ORC) {
        hp_coofs = hp_coofs * 4.0 / 7.0;
    }

    return int(std::sqrt(hp_coofs) * 200.0);
}

// limits.cpp -- set_title(). READ_TITLE()'s expansion only touches
// char_data fields (GET_RACE/GET_TITLE macros) and consts data
// (pc_race_types[], below) -- verified dependency-free of combat/world
// state before this relocation.
extern const std::string_view pc_race_types[]; // consts.cpp -- READ_TITLE() macro below.

#define READ_TITLE(ch) pc_race_types[GET_RACE(ch)]

void set_title(char_data* character)
{
    if (GET_TITLE(character))
        RELEASE(GET_TITLE(character));
    CREATE(GET_TITLE(character), char, READ_TITLE(character).size() + 5);

    strcpy(GET_TITLE(character), std::format("the {}", READ_TITLE(character)).c_str());
    *(GET_TITLE(character) + 4) = toupper(*(GET_TITLE(character) + 4));
}

// char_utils.cpp -- utils:: specialization/perception accessors.
namespace utils {

// char_utils.cpp:44 -- relocated verbatim (entity-seed Task 6,
// controller-adjudicated relocation); declaration unchanged (char_utils.h).
bool is_npc(const char_data& character)
{
    return utils::is_set(character.specials2.act, (long)MOB_ISNPC);
}

// char_utils.cpp:391 -- relocated verbatim (entity-seed Task 6,
// controller-adjudicated relocation); declaration unchanged (char_utils.h).
int get_prof_points(int prof, const char_data& character)
{
    // Added a safety check.
    if (prof > MAX_PROFS)
        return 0;

    // Add is_npc check like above?  Not in the current macro, so I won't be adding new functionality.
    return character.profs->prof_coof[prof];
}

// char_utils.cpp -- relocated verbatim (entity-seed Task 6,
// controller-adjudicated relocation, third pass): get_name()'s only calls
// are is_npc() (already in this archive, above) and player.name/
// short_descr field reads. Declaration unchanged (char_utils.h).
const char* get_name(const char_data& character)
{
    if (is_npc(character))
        return character.player.short_descr;

    return character.player.name;
}

// char_utils.cpp -- relocated verbatim (entity-seed Task 6,
// controller-adjudicated relocation, third pass): each char_data& overload
// forwards to its pure-int sibling, so both move together. Declarations
// unchanged (char_utils.h).
bool is_race_good(int race)
{
    return race > 0 && race < 10;
}

bool is_race_good(const char_data& character)
{
    return is_race_good(character.player.race);
}

bool is_race_magi(int race)
{
    return race == 15;
}

bool is_race_magi(const char_data& character)
{
    return is_race_magi(character.player.race);
}

int get_minimum_insight_perception(const char_data& character)
{
    int race = character.player.race;
    switch (race) {
    case RACE_GOD:
        return 0;
    case RACE_HUMAN:
        return 20;
    case RACE_DWARF:
        return 15;
    case RACE_WOOD:
        return 30;
    case RACE_HOBBIT:
        return 20;
    case RACE_BEORNING:
        return 15;
    case RACE_URUK:
        return 20;
    case RACE_ORC:
        return 15;
    case RACE_MAGUS:
        return 20;
    case RACE_HARADRIM:
        return 20;
    case RACE_OLOGHAI:
        return 15;
    default:
        return 0;
    }

    return 0;
}

game_types::player_specs get_specialization(const char_data& character)
{
    if (is_npc(character) || character.profs == NULL)
        return game_types::PS_None;

    return game_types::player_specs(character.profs->specialization);
}

void set_specialization(char_data& character, game_types::player_specs value)
{
    if (is_npc(character) || character.profs == NULL)
        return;

    if (character.extra_specialization_data.is_mage_spec()) {
        untrack_specialized_mage(&character);
    }

    character.profs->specialization = (int)value;
    character.extra_specialization_data.set(character);

    if (character.extra_specialization_data.is_mage_spec()) {
        track_specialized_mage(&character);
    }
}

// char_utils.cpp:217 -- relocated verbatim (persist-split PS Task 4,
// controller-adjudicated relocation, same pattern as set_specialization()
// above): db_players.cpp's store_to_char() calls these three setters
// unconditionally, so they needed to resolve inside rots_entity for
// rots_persist's linkcheck. Each is a plain char_data& field mutator gated
// on is_npc() (already in this archive, above); the getters
// (get_tactics()/get_shooting()/get_casting()) are not referenced from the
// persist tier and stay in char_utils.cpp. Declarations unchanged
// (char_utils.h).
void set_tactics(char_data& character, int value)
{
    if (value <= 0)
        value = TACTICS_NORMAL;

    if (!is_npc(character)) {
        character.specials.tactics = static_cast<ubyte>(value);
    }
}

// char_utils.cpp:237 -- relocated verbatim (persist-split PS Task 4,
// controller-adjudicated relocation); declaration unchanged (char_utils.h).
void set_shooting(char_data& character, int value)
{
    if (value <= 0)
        value = SHOOTING_NORMAL;

    if (!is_npc(character))
        character.specials.shooting = static_cast<ubyte>(value);
}

// char_utils.cpp:256 -- relocated verbatim (persist-split PS Task 4,
// controller-adjudicated relocation) with ONE mechanical substitution:
// char_utils.cpp's original body gated on `is_pc(character)`; is_pc()
// itself stays in char_utils.cpp (app layer) and calling it from here would
// be an upward edge, so this uses `!is_npc(character)` instead -- provably
// identical, since char_utils.cpp's is_pc() is defined as exactly
// `return !is_npc(character);` (see that file, next to get_casting()).
void set_casting(char_data& character, int value)
{
    if (value <= 0)
        value = CASTING_NORMAL;

    if (!is_npc(character)) {
        character.specials.casting = static_cast<ubyte>(value);
    }
}

} // namespace utils

// char_utils.cpp -- specialization_data::reset() (global scope, not inside
// namespace utils).
void specialization_data::reset()
{
    if (current_spec_info) {
        delete current_spec_info;
        current_spec_info = NULL;
    }

    current_spec = game_types::PS_None;
}

// char_utils.cpp:1391-1633 -- relocated verbatim (entity-seed Task 6,
// controller-adjudicated relocation): specialization_data::set() constructs
// each *_spec_data subclass below, so each subclass's vtable -- emitted
// where its key function (to_string(), the first non-inline virtual) is
// defined -- must live in the same archive that constructs the objects.
// Declarations unchanged (rots/core/character.h).
//============================================================================
// Specialization stuff!
//============================================================================
void cold_spec_data::on_chill_applied(int chill_amount)
{
    total_energy_sapped += chill_amount;
}

//============================================================================
void cold_spec_data::on_chill_ray_success(int damage)
{
    ++total_chill_ray_count;
    ++successful_chill_ray_count;
    total_chill_ray_damage += damage;
}

//============================================================================
void cold_spec_data::on_chill_ray_fail(int damage)
{
    ++total_chill_ray_count;
    ++failed_chill_ray_count;
    total_chill_ray_damage += damage;
}

//============================================================================
void cold_spec_data::on_cone_of_cold_success(int damage)
{
    ++total_cone_of_cold_count;
    ++successful_cone_of_cold_count;
    total_cone_of_cold_damage += damage;
}

//============================================================================
void cold_spec_data::on_cone_of_cold_failed(int damage)
{
    ++total_cone_of_cold_count;
    ++failed_cone_of_cold_count;
    total_cone_of_cold_damage += damage;
}

//============================================================================
void specialization_data::set(char_data& character)
{
    reset();

    game_types::player_specs spec = utils::get_specialization(character);
    if (spec == game_types::PS_Darkness) {
        current_spec_info = new darkness_spec_data();
    } else if (spec == game_types::PS_Fire) {
        current_spec_info = new fire_spec_data();
    } else if (spec == game_types::PS_Lightning) {
        current_spec_info = new lightning_spec_data();
    } else if (spec == game_types::PS_Arcane) {
        current_spec_info = new arcane_spec_data();
    } else if (spec == game_types::PS_Cold) {
        current_spec_info = new cold_spec_data();
    } else if (spec == game_types::PS_Defender) {
        current_spec_info = new defender_data();
    } else if (spec == game_types::PS_LightFighting) {
        current_spec_info = new light_fighting_data();
    } else if (spec == game_types::PS_HeavyFighting) {
        current_spec_info = new heavy_fighting_data();
    } else if (spec == game_types::PS_WildFighting) {
        current_spec_info = new wild_fighting_data();
    } else if (spec == game_types::PS_BattleMage) {
        current_spec_info = new battle_mage_spec_data();
    }

    current_spec = spec;
}

//============================================================================
std::string specialization_data::to_string(char_data& character) const
{
    if (current_spec_info) {
        return current_spec_info->to_string(character);
    }

    return std::string("You are not specialized in anything.\r\n");
}

//============================================================================
std::string elemental_spec_data::to_string(char_data&) const
{
    std::string message_writer;
    message_writer.append("You are specialized in a mage specialization.\n");
    message_writer.append("------------------------------------------------------------\n");
    message_writer.append("You have access to the 'expose elements' spell, which makes a particular\n");
    message_writer.append("elemental spell cost much less mana on the target.  cast 'expose elements'.\n");
    message_writer.append("------------------------------------------------------------\n");
    report_exposed_data(message_writer);
    return message_writer;
}

//============================================================================
void elemental_spec_data::report_exposed_data(std::string& message_writer) const
{
    if (exposed_target) {
        const skill_data* skills = get_skill_array();
        const char* skill_name = skills[spell_id].name;

        std::format_to(std::back_inserter(message_writer), "{} is exposed to the spell [{}].\n",
            utils::get_name(*exposed_target), skill_name);
        message_writer.append("------------------------------------------------------------\n");
    }
}

//============================================================================
std::string cold_spec_data::to_string(char_data&) const
{
    std::string message_writer;
    message_writer.append("You are specialized in cold.\n");
    message_writer.append("------------------------------------------------------------\n");
    message_writer.append("Your cold spells are more difficult to resist.\n");
    message_writer.append("Your fire spells are easier to resist.\n");
    message_writer.append("You have access to the 'expose elements' spell, which makes a particular\n");
    message_writer.append("elemental spell cost much less mana on the target.  cast 'expose elements'.\n");
    message_writer.append("Your cone of cold spell can now chill targets.\n");
    message_writer.append("Your chill ray spell is much harder to resist.\n");
    message_writer.append("------------------------------------------------------------\n");
    /*
        message_writer << "Chill Ray:" << std::endl;
        message_writer << "\tTotal Casts: " << get_chill_ray_count() << std::endl;
        message_writer << "\tSuccessful Casts: " << get_successful_chills() << std::endl;
        message_writer << "\tFailed Casts: " << get_saved_chills() << std::endl;
        message_writer << "\tTotal Damage: " << total_chill_ray_damage << std::endl << std::endl;
        message_writer << "Cone of Cold:" << std::endl;
        message_writer << "\tTotal Casts: " << get_cone_count() << std::endl;
        message_writer << "\tSuccessful Casts: " << get_successful_cones() << std::endl;
        message_writer << "\tFailed Casts: " << get_saved_cones() << std::endl;
        message_writer << "\tTotal Damage: " << total_cone_of_cold_damage << std::endl << std::endl;
        message_writer << "\tTotal Attacks Stopped: " << get_total_energy_sapped() / ENE_TO_HIT << std::endl;
        */
    report_exposed_data(message_writer);
    return message_writer;
}

//============================================================================
std::string fire_spec_data::to_string(char_data& character) const
{
    std::string message_writer;
    message_writer.append("You are specialized in fire.\n");
    message_writer.append("------------------------------------------------------------\n");
    message_writer.append("Your fire spells are more difficult to resist.\n");
    message_writer.append("Your cold spells are easier to resist.\n");
    message_writer.append("You have access to the 'expose elements' spell, which makes a particular\n");
    message_writer.append("elemental spell cost much less mana on the target.  cast 'expose elements'.\n");
    if (utils::is_race_good(character)) {
        message_writer.append("The minimum damage of firebolt is increased significantly.\n");
        message_writer.append("Your fireballs will no longer spread to friendly targets.\n");
    } else {
        message_writer.append("Your searing darkness spell deals significantly more fire damage.\n");
    }
    message_writer.append("------------------------------------------------------------\n");
    report_exposed_data(message_writer);
    return message_writer;
}

//============================================================================
std::string lightning_spec_data::to_string(char_data&) const
{
    std::string message_writer;
    message_writer.append("You are specialized in lightning.\n");
    message_writer.append("------------------------------------------------------------\n");
    message_writer.append("Your lightning spells are more difficult to resist.\n");
    message_writer.append("You have access to the 'expose elements' spell, which makes a particular\n");
    message_writer.append("elemental spell cost much less mana on the target.  cast 'expose elements'.\n");
    message_writer.append("Lightning bolt does not lose effectiveness indoors, and deals increased damage.\n");
    message_writer.append("You can cast lightning strike without a storm at slightly reduced effectiveness.\n");
    message_writer.append("------------------------------------------------------------\n");
    report_exposed_data(message_writer);
    return message_writer;
}

//============================================================================
std::string darkness_spec_data::to_string(char_data& character) const
{
    std::string message_writer;
    message_writer.append("You are specialized in darkness.\n");
    message_writer.append("------------------------------------------------------------\n");
    message_writer.append("Your dark spells are more difficult to resist.\n");
    message_writer.append("You have access to the 'expose elements' spell, which makes a particular\n");
    message_writer.append("elemental spell cost much less mana on the target.  cast 'expose elements'.\n");
    message_writer.append("Your dark bolt spell deals increased damage.\n");
    if (utils::is_race_magi(character)) {
        message_writer.append("Your black arrow is harder to resist.\n");
        message_writer.append("Your spear of darkness spell deals additional damage.\n");
    } else {
        message_writer.append("Your searing darkness spell deals additional dark damage.\n");
    }
    message_writer.append("------------------------------------------------------------\n");
    report_exposed_data(message_writer);
    return message_writer;
}

//============================================================================
std::string arcane_spec_data::to_string(char_data&) const
{
    std::string message_writer;
    message_writer.append("You are specialized in the arcane.\n");
    message_writer.append("------------------------------------------------------------\n");
    message_writer.append("You have access to the 'expose elements' spell, which makes a particular\n");
    message_writer.append("elemental spell cost much less mana on the target.  cast 'expose elements'.\n");
    message_writer.append("You can cast spells at a normal, fast, or slow pace.\n");
    message_writer.append("Slow cast spells against exposed targets will restore mana.\n");
    message_writer.append("------------------------------------------------------------\n");
    report_exposed_data(message_writer);

    return message_writer;
}

//============================================================================
std::string heavy_fighting_data::to_string(char_data&) const
{
    return std::string("You are specialized in heavy fighting.");
}

//============================================================================
std::string light_fighting_data::to_string(char_data&) const
{
    return std::string("You are specialized in light fighting.\n\r");
}

//============================================================================
std::string defender_data::to_string(char_data&) const
{
    return std::string("You are specialized in defending.\n\r");
}

std::string battle_mage_spec_data::to_string(char_data&) const
{
    return std::string("You are specialized in battle mage.\n\r");
}

//============================================================================
std::string wild_fighting_data::to_string(char_data&) const
{
    return std::string("You are specialized in wild fighting.\n\r");
}

/************************************************************************
 *  Affect / derived-ability engine (db-split Task 4b)                *
 ************************************************************************
 *  Relocated verbatim from handler.cpp (affect_modify/affect_naked/
 *  apply_gear_affects/modify_affects/affect_total/affect_to_char/
 *  affect_remove/affected_by_spell), profs.cpp (do_squareroot/
 *  recalc_abilities), and utility.cpp (get_race_perception/
 *  get_naked_perception/get_naked_willpower/get_confuse_modifier/
 *  encrypt_line/decrypt_line). These were previously hand-duplicated in
 *  convert_stubs.cpp so rots_convert could link without the whole
 *  handler.cpp/profs.cpp/utility.cpp TUs; Task 4b restores a single real
 *  definition per symbol (this TU is linked by BOTH ageland and
 *  rots_convert) and deletes the convert_stubs.cpp duplicates. Bodies,
 *  comments, and local quirks are unchanged from their origin files;
 *  only the enclosing TU differs. Declarations are unchanged in
 *  handler.h/utils.h.
 *
 *  get_from_affected_type_pool()/put_to_affected_type_pool(),
 *  affected_list/affected_list_pool, and class_HP() -- the three shared
 *  helpers this section's moved bodies call -- were themselves relocated
 *  into this same TU by entity-seed Task 5 (see the "Entity-tier leaf
 *  helpers" section above), so they are now real in-TU definitions rather
 *  than the forward-declared externs this comment used to describe.
 ************************************************************************/

// Shared-helper forward declarations for symbols this section's moved
// bodies call but do NOT define here:
extern int max_race_str[]; // consts.cpp -- recalc_abilities()'s GET_BAL_STR() macro
extern struct skill_data skills[]; // consts.cpp -- affect_modify()'s APPLY_SPELL case

// handler.cpp's file-local macro (APPLY_MAUL case, affect_modify() below).
// Macros do not cross translation units, so this must be redefined here.
#define MAX_MAUL_DODGE 50

namespace {
// Verbatim copy of profs.cpp's do_squareroot(int, char_data*) overload --
// moved alongside recalc_abilities() (its only caller). Kept file-local
// (anonymous namespace) so it does not collide at link time with
// utility.cpp's separate, differently-implemented do_squareroot(int,
// char_data*) overload (a pre-existing, unrelated same-signature function
// used by a different call site in that TU).
/*
 * This function returns 200 * sqrt(i).
 */
inline int do_squareroot(int i, char_data*)
{
    return int(std::sqrt(i) * 200.0);
}
} // namespace

/* This is called whenever some of person's stats/level change */
void recalc_abilities(char_data* character)
{
    int tmp, tmp2, dex_speed;
    struct obj_data* weapon;

    if (!IS_NPC(character)) {
        character->abilities.str = character->constabilities.str;
        character->abilities.lea = character->constabilities.lea;
        character->abilities.intel = character->constabilities.intel;
        character->abilities.wil = character->constabilities.wil;
        character->abilities.dex = character->constabilities.dex;
        character->abilities.con = character->constabilities.con;

        character->abilities.hit = 10 + std::min(LEVEL_MAX, GET_LEVEL(character)) + character->constabilities.hit * GET_CON(character) / 20 + (class_HP(character) * (GET_CON(character) + 20) / 14) * std::min(LEVEL_MAX * 100, (int)GET_MINI_LEVEL(character)) / 100000;

        // Characters specialized in defender get 10% bonus HP.
        if (utils::get_specialization(*character) == game_types::PS_Defender) {
            character->abilities.hit += character->abilities.hit / 10;
        }

        // dirty test to see if this ranger change can work
        character->abilities.hit = std::max(character->abilities.hit - (GET_RAW_SKILL(character, SKILL_STEALTH) * GET_LEVELA(character) + GET_RAW_SKILL(character, SKILL_STEALTH) * 3) / 33, 10);

        character->tmpabilities.hit = std::min(character->tmpabilities.hit, character->abilities.hit);

        character->abilities.mana = character->constabilities.mana + GET_INT(character) + GET_WILL(character) / 2 + GET_PROF_LEVEL(PROF_MAGE, character) * 2;

        character->tmpabilities.mana = std::min(character->tmpabilities.mana, character->abilities.mana);

        character->abilities.move = character->constabilities.move + GET_CON(character) + 20 + GET_PROF_LEVEL(PROF_RANGER, character) + GET_RAW_KNOWLEDGE(character, SKILL_TRAVELLING) / 4;

        if ((GET_RACE(character) == RACE_WOOD) || GET_RACE(character) == RACE_HIGH)
            character->abilities.move += 15;

        // Giving the beorning race 50+ moves
        if (GET_RACE(character) == RACE_BEORNING) {
            character->abilities.move += 50;
        }

        character->tmpabilities.move = std::min(character->tmpabilities.move, character->abilities.move);

        weapon = character->equipment[WIELD];
        if (weapon) {
            if (GET_OBJ_WEIGHT(weapon) == 0) {
                /*UPDATE*, temporary check for 0 weight weapons*/
                GET_OBJ_WEIGHT(weapon) = 1;
                mudlog("SYSERR: 0 weight weapon", NRM, LEVEL_GOD, TRUE);
            }

            int bulk = weapon->get_bulk();
            character->specials.null_speed = 3 * GET_DEX(character) + 2 * (GET_RAW_SKILL(character, SKILL_ATTACK) + GET_RAW_SKILL(character, SKILL_STEALTH) / 2) / 3 + 100;

            character->specials.str_speed = GET_BAL_STR(character) * 2500000 / (GET_OBJ_WEIGHT(weapon) * (bulk + 3));

            if (IS_TWOHANDED(character)) {
                character->specials.str_speed *= 2;
            }

            /* Dex adjustment by Fingol */
            if (bulk < 4) {
                dex_speed = GET_DEX(character) * 2500000 / (GET_OBJ_WEIGHT(weapon) * (bulk + 3));

                tmp2 = (character->specials.str_speed * bulk / 5) + (dex_speed * (5 - bulk) / 5);

                character->specials.str_speed = std::max(character->specials.str_speed, tmp2);
            }

            tmp = 1000000;
            tmp /= 1000000 / character->specials.str_speed + 1000000 / (character->specials.null_speed * character->specials.null_speed);

            game_types::weapon_type w_type = weapon->get_weapon_type();
            GET_ENE_REGEN(character) = do_squareroot(tmp / 100, character) / 20;

            // Custom energy regen based on race, etc.
            if (GET_RACE(character) == RACE_DWARF && weapon_skill_num(w_type) == SKILL_AXE) {
                GET_ENE_REGEN(character) += std::min(GET_ENE_REGEN(character) / 10, 10);
            } else if (GET_RACE(character) == RACE_HARADRIM && weapon_skill_num(w_type) == SKILL_SPEARS) {
                GET_ENE_REGEN(character) += std::min(GET_ENE_REGEN(character) / 20, 20);
            }

            // weapon masters get bonus attack speed with some weapons.
            character->points.ENE_regen *= rots::entity::dispatch_attack_speed_multiplier(character);

        } else {
            GET_ENE_REGEN(character) = 60 + 5 * GET_DEX(character);

            /*---------------- Beornings get a different speed calc here -----------------*/
        }
    }
}

// spec_pro.cpp:129 -- relocated verbatim (persist-split PS Task 4,
// controller-adjudicated relocation, mirroring entity-seed Task 5's
// affect/derived-ability engine move): db_players.cpp's store_to_char()
// calls this unconditionally, so it needed to resolve inside rots_entity for
// rots_persist's linkcheck. Reads only skills[]/square_root[]/
// language_number/language_skills (consts.cpp, already rots_core) plus
// char_data fields -- no comm/world/combat_list access, so it is the
// sibling of recalc_abilities() above. Declaration unchanged (spells.h);
// spec_pro.cpp's own two other call sites (handle_pracs() and a wiz
// learning-reset command) now call down into this TU through that
// declaration.
void recalc_skills(struct char_data* ch)
{

    int skill_no, pracs_used, tmps, tmp, difficulty, skill_level;
    if (ch->knowledge.empty() || ch->skills.empty())
        return;

    for (skill_no = 1; skill_no < MAX_SKILLS; skill_no++) {
        pracs_used = ch->skills[skill_no] * 20;

        if (!skills[skill_no].learn_diff)
            skills[skill_no].learn_diff = 10;
        skill_level = skills[skill_no].level;

        /* 3 pracs in weapon mastery is same as 1 prac in all weapons */
        if ((skill_no >= 0) && (skill_no < 10) && (skill_no != 8) && (skill_no != 7))
            pracs_used += ch->skills[10] * 20 * 3 / 8;

        difficulty = skills[skill_no].learn_diff;

        /* pracs /= coof. - (lvl/30)*(1-coof) (where coof 0 to 1) */
        if (skills[skill_no].type != PROF_WARRIOR) {
            tmps = GET_PROF_COOF((int)skills[skill_no].type, ch) - skill_level * (1000 - GET_PROF_COOF((int)skills[skill_no].type, ch)) / 30;

            if (skill_level < 20)
                tmps = tmps * (80 + skill_level) / 100 + 200 - skills[(int)skill_no].level * 10;

            tmps = MIN(1000, tmps);

            pracs_used = pracs_used * tmps * 10;
        } else
            pracs_used = pracs_used * 10000;

        // pracs used * 100000 with adjustment
        tmps = 1000 - pracs_used / (difficulty * 100);
        ch->knowledge[skill_no] = (10000 - tmps * tmps / 100) / 99;
        if (tmps < 0)
            ch->knowledge[skill_no] = 100;
    }

    switch (GET_RACE(ch)) {
    case RACE_GOD:
        tmp = LANG_BASIC;
        break;
    case RACE_HUMAN:
    case RACE_DWARF:
    case RACE_WOOD:
    case RACE_HOBBIT:
    case RACE_HIGH:
        tmp = LANG_HUMAN;
        break;
    case RACE_BEORNING:
        tmp = LANG_ANIMAL;
        break;
    case RACE_URUK:
    case RACE_HARAD:
    case RACE_ORC:
    case RACE_HARADRIM:
    case RACE_OLOGHAI:
    case RACE_MAGUS:
        tmp = LANG_ORC;
        break;
    case RACE_EASTERLING:
        tmp = LANG_BASIC;
        break;
    default:
        tmp = LANG_BASIC;
        break;
    }

    // Set the spoken language.
    ch->player.language = tmp;

    if (tmp != LANG_BASIC)
        SET_KNOWLEDGE(ch, tmp, 100);

    if (GET_RACE(ch) == RACE_GOD)
        for (tmp = 0; tmp < language_number; tmp++)
            SET_KNOWLEDGE(ch, language_skills[tmp], 100);

    if (!IS_NPC(ch) && (GET_RACE(ch) == RACE_MAGUS) && (GET_RAW_KNOWLEDGE(ch, SPELL_BLINK) == 0))
        SET_KNOWLEDGE(ch, SPELL_BLINK, 10);

    if (!IS_NPC(ch) && (GET_RACE(ch) == RACE_BEORNING) && (GET_RAW_KNOWLEDGE(ch, SKILL_NATURAL_ATTACK) == 0)) {
        SET_KNOWLEDGE(ch, SKILL_NATURAL_ATTACK, 10);
    }
}

void affect_modify(struct char_data* ch, byte loc, int mod, long bitv, char add, sh_int counter)
{
    int tmp, tmp2;

    if (add == AFFECT_MODIFY_SET) {
        SET_BIT(ch->specials.affected_by, bitv);
        if (utils::is_set(bitv, long(AFF_CHARM))) {
            ch->damage_details.reset();
        }
    } else if (add == AFFECT_MODIFY_REMOVE) {
        REMOVE_BIT(ch->specials.affected_by, bitv);
        if (utils::is_set(bitv, long(AFF_CHARM))) {
            ch->damage_details.reset();
        }

        mod = -mod;
    }
    ch->specials.affected_by |= race_affect[GET_RACE(ch)];

    if (add == AFFECT_MODIFY_TIME) {
        return; /* so, usual affects are not modified in this call */
    }

    switch (loc) {
    case APPLY_NONE:
        break;

    case APPLY_STR:
        SET_STR_BASE(ch, GET_STR_BASE(ch) + mod);
        SET_STR(ch, GET_STR(ch) + mod);
        break;

    case APPLY_LEA:
        GET_LEA_BASE(ch) += mod;
        GET_LEA(ch) += mod;
        break;

    case APPLY_DEX:
        GET_DEX_BASE(ch) += mod;
        GET_DEX(ch) += mod;
        break;

    case APPLY_INT:
        GET_INT_BASE(ch) += mod;
        GET_INT(ch) += mod;
        break;

    case APPLY_WILL:
        GET_WILL_BASE(ch) += mod;
        GET_WILL(ch) += mod;
        break;

    case APPLY_CON:
        GET_CON_BASE(ch) += mod;
        GET_CON(ch) += mod;
        break;

    case APPLY_PROF:
        /* ??? GET_PROF(ch) += mod; */
        break;

    case APPLY_LEVEL:
        /* ??? GET_LEVEL(ch) += mod; */
        break;

    case APPLY_AGE:
        ch->player.time.birth -= (mod * SECS_PER_MUD_YEAR);
        break;

    case APPLY_CHAR_WEIGHT:
        GET_WEIGHT(ch) += mod;
        break;

    case APPLY_CHAR_HEIGHT:
        GET_HEIGHT(ch) += mod;
        break;

    case APPLY_MANA:
        GET_MAX_MANA(ch) += mod;
        if (GET_MANA(ch) >= GET_MAX_MANA(ch) - mod)
            GET_MANA(ch) += mod;

        break;

    case APPLY_WILLPOWER:
        GET_WILLPOWER(ch) += mod;
        break;

    case APPLY_HIT:
        GET_MAX_HIT(ch) += mod;
        if (GET_HIT(ch) >= GET_MAX_HIT(ch) - mod)
            GET_HIT(ch) += mod;

        break;

    case APPLY_MOVE:
        GET_MAX_MOVE(ch) += mod;
        if (GET_MOVE(ch) >= GET_MAX_MOVE(ch) - mod)
            GET_MOVE(ch) += mod;
        break;

    case APPLY_GOLD:
        break;

    case APPLY_EXP:
        break;

    case APPLY_DODGE:
        SET_DODGE(ch) += mod;
        break;

    case APPLY_OB:
        SET_OB(ch) += mod;
        break;

    case APPLY_SPELL_PEN:
        ch->points.spell_pen += mod;
        break;

    case APPLY_SPELL_POW:
        ch->points.spell_power += mod;
        break;

    case APPLY_DAMROLL:
        GET_DAMAGE(ch) += mod;
        break;

    case APPLY_SAVING_SPELL:
        GET_SAVE(ch) += mod;
        break;

    case APPLY_VISION:
        if (add) {
            if (mod > 0)
                SET_BIT(ch->specials.affected_by, AFF_INFRARED);
            if (mod < 0)
                SET_BIT(ch->specials.affected_by, AFF_BLIND);
        } else {
            if (mod > 0)
                REMOVE_BIT(ch->specials.affected_by, AFF_BLIND);
            if (mod < 0)
                REMOVE_BIT(ch->specials.affected_by, AFF_INFRARED);
        }

    case APPLY_REGEN:
        break;

    case APPLY_SPEED:
        GET_ENE_REGEN(ch) += mod;
        break;

    case APPLY_BEND: {
        GET_ENE_REGEN(ch) += (GET_ENE_REGEN(ch) / 2);
        SET_OB(ch) += mod;
    } break;

    case APPLY_ARMOR:
        //     mod = (2*mod*GET_PERCEPTION(ch))/100;
        //     SET_DODGE(ch) += mod;
        break;
    case APPLY_MAUL:
        if (!add) {
            SET_DODGE(ch) += std::min((counter * 5), MAX_MAUL_DODGE);
        }

        if (add) {
            SET_DODGE(ch) += -(std::min((counter * 5), MAX_MAUL_DODGE));
        }
        break;

    case APPLY_PERCEPTION:
        // Lego: Since we loop through every gear slot, we need to track the underlying perception value rather than update the final perception value
        //       because each subsequent iteration would add/subtract the overridden minimum percep.
        //       Then we override the underlying value with our minimum perception logic and expose that to the rest of the game.
        ch->specials2.rawPerception += mod;

        if (affected_by_spell(ch, SPELL_INSIGHT)) {
            int minimumRacePerception = utils::get_minimum_insight_perception(*ch);

            ch->specials2.perception = std::max(ch->specials2.rawPerception, minimumRacePerception);
        } else {
            ch->specials2.perception = ch->specials2.rawPerception;
        }
        break;

    case APPLY_SPELL:
        if (!add)
            mod = -mod;
        tmp = mod & 255; // spell number, in skills[] table
        tmp2 = mod / 256; // spell level
        if (!tmp2)
            tmp2 = GET_LEVEL(ch);
        if (tmp >= 128)
            break;

        if (!skills[tmp].spell_pointer)
            break;

        if (add)
            skills[tmp].spell_pointer(ch, mutable_arg(""), SPELL_TYPE_SPELL, ch, 0, 0, 1);
        else
            skills[tmp].spell_pointer(ch, mutable_arg(""), SPELL_TYPE_ANTI, ch, 0, 0, 1);
        break;

    case APPLY_BITVECTOR:
        if (add) {
            if ((mod < 0) || (mod > 31))
                mod = 0;
            SET_BIT(ch->specials.affected_by, 1 << mod);
        } else {
            mod = -mod;
            if ((mod < 0) || (mod > 31))
                mod = 0;
            REMOVE_BIT(ch->specials.affected_by, 1 << mod);
        }
        break;

    case APPLY_MANA_REGEN:
        ch->points.mana_regen += mod;
        break;

    case APPLY_RESIST:
        // Fixed-bug (Phase 5 T6, UBSan negative-shift-exponent): AFFECT_MODIFY_REMOVE
        // above negates `mod` (it's a bit position here, e.g. PLRSPEC_FIRE,
        // not a numeric stat delta -- the negation is this function's generic
        // "undo an add" convention for every APPLY_* case, not specific to
        // this one). `1 << mod` with mod now negative is UB (confirmed live:
        // an APPLY_VULN affect wearing off during a real world-data boot hit
        // this under UBSan); negate back to recover the original bit
        // position before shifting.
        if (mod >= 0)
            GET_RESISTANCES(ch) |= (1 << mod);
        else
            GET_RESISTANCES(ch) &= ~(1 << (-mod));
        break;

    case APPLY_VULN:
        if (mod >= 0)
            GET_VULNERABILITIES(ch) |= (1 << mod);
        else
            GET_VULNERABILITIES(ch) &= ~(1 << (-mod));
        break;

    default:
        log("SYSERR: Unknown apply adjust attempt (handler.c, affect_modify).");
        break;
    } /* switch */
}

void affect_naked(char_data* ch)
{
    // sets some intrinsic parameters
    // assumes that the char is naked and has no affections.

    // SET_PERCEPTION(ch, get_naked_perception(ch));
    int nakedPerception = get_naked_perception(ch);
    ch->specials2.rawPerception = ch->specials2.perception = nakedPerception;
    GET_WILLPOWER(ch) = get_naked_willpower(ch);
    ch->specials.affected_by |= race_affect[GET_RACE(ch)];

    if (!IS_NPC(ch)) {
        GET_RESISTANCES(ch) = 0;
        GET_VULNERABILITIES(ch) = 0;
    }
}

void apply_gear_affects(char_data* character, const obj_data* item, int modify_flag)
{
    for (int count = 0; count < MAX_OBJ_AFFECT; ++count) {

        const obj_affected_type& obj_affect = item->affected[count];
        if (obj_affect.location == APPLY_SPELL)
            continue;

        affect_modify(character, obj_affect.location, obj_affect.modifier, item->obj_flags.bitvector, modify_flag, 0);
    }
}

void apply_gear_affects(char_data* character, int modify_flag)
{
    for (int item_index = 0; item_index < MAX_WEAR; ++item_index) {
        const obj_data* item = character->equipment[item_index];
        if (item == nullptr)
            continue;

        if (item_index == HOLD && !CAN_WEAR(item, ITEM_HOLD))
            continue;

        apply_gear_affects(character, item, modify_flag);
    }
}

void modify_affects(char_data* character, int modify_flag)
{
    int count = 0;
    affected_type* af = character->affected;
    while (count < MAX_AFFECT && af != nullptr) {
        affect_modify(character, af->location, af->modifier, af->bitvector, modify_flag, af->counter);
        ++count;
        af = af->next;
    }
}

void affect_total(struct char_data* ch, int mode)
{
    if (mode & AFFECT_TOTAL_REMOVE) {
        apply_gear_affects(ch, AFFECT_MODIFY_REMOVE);
        modify_affects(ch, AFFECT_MODIFY_REMOVE);

        recalc_abilities(ch);
        affect_naked(ch);
    }

    if (mode & AFFECT_TOTAL_SET) {
        apply_gear_affects(ch, AFFECT_MODIFY_SET);
        modify_affects(ch, AFFECT_MODIFY_SET);
    }

    if (mode & AFFECT_TOTAL_TIME) {
        apply_gear_affects(ch, AFFECT_MODIFY_TIME);
        modify_affects(ch, AFFECT_MODIFY_TIME);
    }
    /* Make certain values are between 0..100, not < 0 and not > 100! */

    signed char max_value = 100;
    signed char min_dex_str = 1;
    signed char min_others = 0;

    ch->abilities.dex = std::max(min_dex_str, std::min(ch->abilities.dex, max_value));
    ch->abilities.intel = std::max(min_others, std::min(ch->abilities.intel, max_value));
    ch->abilities.wil = std::max(min_others, std::min(ch->abilities.wil, max_value));
    ch->abilities.con = std::max(min_others, std::min(ch->abilities.con, max_value));
    ch->abilities.str = std::max(min_dex_str, std::min(ch->abilities.str, max_value));
    ch->abilities.lea = std::max(min_others, std::min(ch->abilities.lea, max_value));
}

/* Insert an affect_type in a char_data structure
   Automatically sets apropriate bits and applys

   1.  Checks to see if the character is on the affected list.  If not they are added
   2.  Allocates memory for the new affection (also inserting it into affected_list)
   3.  Copies the parameters of the affection to the structure in the affected_list
   4.  Adds it to the ch->affected list
   5.  Calls affect_modify and affect_total to update the characters stats/abilities  */

void affect_to_char(struct char_data* ch, struct affected_type* af)
{
    struct affected_type* affected_alloc;
    universal_list* tmplist;
    char mybuf[255];

    if (!ch)
        return;

    // 1
    if (!ch->affected) {
        tmplist = pool_to_list(&affected_list, &affected_list_pool);
        tmplist->ptr.ch = ch;
        tmplist->number = ch->abs_number;
        tmplist->type = TARGET_CHAR;

        // nz(): GET_NAME(ch) can be null for a bare/uninitialized char_data
        // (e.g. a test fixture) -- glibc's old sprintf("%s", NULL) printed
        // "(null)" here without crashing; std::format calls strlen()
        // unconditionally and crashes on a null char*, so nz() preserves
        // the old byte-identical output instead (utils.h).
        strcpy(mybuf, std::format("Char to aff_list: {}\n\r", nz(GET_NAME(ch))).c_str());
    }

    // 2
    affected_alloc = get_from_affected_type_pool();

    // 3
    *affected_alloc = *af;

    // 4
    affected_alloc->next = ch->affected;
    ch->affected = affected_alloc;

    affected_alloc->time_phase = get_current_time_phase();

    // 5
    affect_modify(ch, af->location, af->modifier, af->bitvector,
        AFFECT_MODIFY_SET, af->counter);
    affect_total(ch);
}

/* Remove an affected_type structure from a char (called when duration
   reaches zero). Pointer *af must never be NIL! Frees mem and calls
   affect_location_apply
                                               */
void affect_remove(struct char_data* ch, struct affected_type* af)
{
    struct affected_type* hjp;
    universal_list *tmplist, *tmplist2;
    int tmp;

    //   assert(ch->affected);
    // Looks as though the following line is "just in case", but where did af come from in this case?
    if (!ch->affected)
        return;

    affect_modify(ch, af->location, af->modifier, af->bitvector,
        AFFECT_MODIFY_REMOVE, af->counter);

    /* remove structure *af from linked list */
    if (ch->affected == af) {
        /* remove head of list */
        ch->affected = af->next;
    } else {
        for (hjp = ch->affected, tmp = 0;
             (hjp->next) && (hjp->next != af) && (tmp < MAX_AFFECT);
             hjp = hjp->next, tmp++) {
        }
        if (hjp->next != af) {
            log("SYSERR: FATAL : Could not locate affected_type in ch->affected. (handler.c, affect_remove)");
            //	 exit(1);
            return;
        }
        hjp->next = af->next; /* skip the af element */
    }

    //   RELEASE(af);
    put_to_affected_type_pool(af);

    if (!ch->affected && affected_list) {
        for (tmplist = affected_list; tmplist; tmplist = tmplist2) {
            tmplist2 = tmplist->next;
            if ((tmplist->type == TARGET_CHAR) && (tmplist->ptr.ch == ch))
                from_list_to_pool(&affected_list, &affected_list_pool, tmplist);
        }
    }

    affect_total(ch);
}

/* Return if a char is affected by a spell (SPELL_XXX), NULL indicates not affected.
   start_affect is not used anywhere in the mud...*/
affected_type* affected_by_spell(const char_data* ch, byte skill, affected_type* start_affect)
{
    if (!start_affect)
        start_affect = ch->affected;

    int count = 0;
    for (affected_type* status_affect = start_affect; status_affect && (count < MAX_AFFECT); status_affect = status_affect->next, count++) {
        if (status_affect->type == skill) {
            return status_affect;
        }
    }

    return NULL;
}

sh_int
get_race_perception(struct char_data* ch)
{
    switch (GET_RACE(ch)) {
    case RACE_GOD:
        return 0;
    case RACE_HUMAN:
        return 30;
    case RACE_DWARF:
        return 0;
    case RACE_WOOD:
        return 50;
    case RACE_HOBBIT:
        return 30;
    case RACE_HIGH:
        return 100;
    case RACE_URUK:
        return 30;
    case RACE_HARAD:
        return 30;
    case RACE_ORC:
        return 10;
    case RACE_EASTERLING:
        return 30;
    case RACE_MAGUS:
        return 30;
    case RACE_UNDEAD:
        return 60;
    case RACE_TROLL:
        return 30;
    case RACE_HARADRIM:
        return 30;
    case RACE_BEORNING:
        return 30;
    case RACE_OLOGHAI:
        return 30;
    default:
        return 0;
    }
    return 0;
}

sh_int
get_naked_perception(struct char_data* ch)
{
    int tmp;

    if (IS_NPC(ch)) {
        if (MOB_FLAGGED(ch, MOB_SHADOW))
            return 100;
        else
            return GET_PERCEPTION(ch);
    }

    tmp = get_race_perception(ch);
    tmp += GET_PROF_LEVEL(PROF_CLERIC, ch) * 2;

    return tmp;
}

sh_int
get_naked_willpower(struct char_data* ch)
{
    return GET_PROF_LEVEL(PROF_CLERIC, ch) + GET_WILL(ch) - (get_confuse_modifier(ch) / 10);
}

int get_confuse_modifier(struct char_data* ch)
{
    struct affected_type* aff;
    int modifier = 0;

    if (IS_AFFECTED(ch, AFF_CONFUSE))
        for (aff = ch->affected; aff; aff = aff->next)
            if (aff->type == SPELL_CONFUSE)
                modifier = aff->duration * 2 - 10;

    return modifier;
}

unsigned char encrypt_line_lp[1000];
void encrypt_line(unsigned char* line, int len)
{
    unsigned char k1, k2;
    int tmp;
    /*static*/ unsigned char* lp = encrypt_line_lp;

    for (tmp = 0; tmp < len; tmp++)
        if (line[tmp] > 127)
            line[tmp] -= 128;

    for (tmp = 0; tmp < len - 1; tmp++) {
        k1 = (line[tmp] * 16); // here was *32...
        k2 = (line[tmp + 1] / 8);
        lp[tmp] = (k1 + k2) & 127;
        lp[tmp] += 32;
        //    printf("encoding: '%c' %d %d '%c'%d\n",line[tmp],k1,k2 ,lp[tmp],lp[tmp]);
    }
    k1 = (line[len - 1] * 16);
    k2 = (line[0] / 8);
    // k2 = 0;
    lp[len - 1] = (k1 + k2) & 127;
    lp[len - 1] += 32;
    //  printf("e-encoding: '%c' %d %d '%c'%d\n",line[len-1],k1,k2 ,lp[len-1],lp[len-1]);

    for (tmp = 0; tmp < len; tmp++)
        line[tmp] = lp[tmp];
}

unsigned char decrypt_line_line[1000];
void decrypt_line(unsigned char* lp, int len)
{
    unsigned char k1, k2;
    int tmp;
    /*static*/ unsigned char* line = decrypt_line_line;

    k1 = ((lp[len - 1] - 32) * 8);
    // k1 = 0;
    k2 = ((lp[0] - 32) / 16);
    line[0] = (k1 + k2) & 127;
    //  printf("d-decoding: '%c'%d %d %d '%c'%d\n",lp[0],lp[0],k1,k2 ,line[0],line[0]);
    for (tmp = 1; tmp < len; tmp++) {
        k1 = ((lp[tmp - 1] - 32) * 8);
        k2 = ((lp[tmp] - 32) / 16); // here was /32
        line[tmp] = (k1 + k2) & 127;
        //    printf("decoding: '%c'%d %d %d '%c'%d\n",lp[tmp],lp[tmp],k1,k2 ,line[tmp],line[tmp]);
    }

    for (tmp = 0; tmp < len; tmp++)
        lp[tmp] = line[tmp];
}

/************************************************************************
 *  Functions relocated verbatim from handler.cpp (placement-seam Task 4; *
 *  census verdict MOVE-OTHER-L2 -- see placement-census.md's handler.cpp *
 *  table and task-4-report.md for the full evidence trail). isname()     *
 *  joins isname_nullable() (above); the affect_*_room()/affect_from_char()/
 *  room_affected_by_spell()/affect_join() family joins affect_modify()/  *
 *  affect_total()/affect_to_char()/affect_remove()/affected_by_spell()   *
 *  (already relocated here in db-split Task 4b -- the census's "affect-  *
 *  machinery note"); the follow_type pool joins the affected_type pool   *
 *  above (same ownership pattern: private backing state moves with its   *
 *  two accessors, handler.cpp's add_follower()/stop_follower() keep      *
 *  reaching them via the pre-existing local forward declarations near    *
 *  this file's original top); register_pc_char() (a trivial one-line      *
 *  forwarder) joins register_npc_char() above. Bodies are byte-identical *
 *  to their handler.cpp originals -- none of this batch touches         *
 *  world[]/zone_table[]/obj_index[] (census-verified: no SEAM            *
 *  substitution needed). Declarations are unchanged in handler.h.        *
 *  get_char() is DEFERRED, not moved -- see its own comment below for    *
 *  why (a cascading dependency on parse_numbered_name(), itself deferred *
 *  in handler.cpp this task).                                            *
 ************************************************************************/

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

// affect_total_room()'s original preceding doc comment ("This updates a
// character by subtracting everything he is affected by...") was already
// orphaned from affect_total() (moved to this file in db-split Task 4b)
// before this task moved affect_total_room() too -- it stays behind in
// handler.cpp rather than being duplicated or rewritten here, per this
// task's verbatim-move mandate.
void affect_total_room(struct room_data*, int)
{
}

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

// handler.cpp -- private backing state for get_from_follow_type_pool()/
// put_to_follow_type_pool() immediately below; add_follower()/
// stop_follower() (handler.cpp) are the pool functions' only other callers
// and reach them via the pre-existing local forward declarations near this
// file's original top (same pattern as the affected_type_pool functions
// above) -- no other handler.cpp function reads follow_type_pool/
// follow_type_counter directly, so the globals move with the pool
// functions rather than staying behind as an extern.
follow_type* follow_type_pool = 0;
int follow_type_counter = 0;

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

// get_char() relocated here from handler.cpp (combat-seed Task 3,
// completing the placement-seam Task 4 deferral): its only non-L2
// dependency, parse_numbered_name(), moved to rots_util.cpp
// (rots_platform, L0) in this same commit, so this is now a legal
// downward edge (rots_entity links RotS::platform) instead of the
// upward edge that blocked the move before. Declaration stays in
// handler.h (unchanged callers).
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

// handler.cpp -- register_npc_char() (above) is register_pc_char()'s only
// callee; declaration unchanged in handler.h.
int register_pc_char(struct char_data* ch)
{

    return register_npc_char(ch);
}
