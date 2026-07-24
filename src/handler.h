/* ************************************************************************
 *   File: handler.h                                     Part of CircleMUD *
 *  Usage: header file: prototypes of handling and utility functions       *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#ifndef HANDLER_H
#define HANDLER_H

#include "platdef.h" /* For sh_int, ush_int, byte, etc. */
#include "rots/core/fwd.h"
#include "rots/persist/file_formats.h" /* For the RENT_CRASH macro */
#include "objects_json.h" /* For ObjectSaveData (account-backed object staging) */

#include <string_view>

/* handling the affected-structures */
#define AFFECT_TOTAL_UPDATE 3
#define AFFECT_TOTAL_SET 1
#define AFFECT_TOTAL_REMOVE 2
#define AFFECT_TOTAL_TIME 4

#define AFFECT_MODIFY_SET 1
#define AFFECT_MODIFY_REMOVE 0
#define AFFECT_MODIFY_TIME 2

void affect_total_room(struct room_data* room, int mode = AFFECT_TOTAL_UPDATE);
void affect_modify_room(struct room_data* room, byte loc, int mod, long bitv, char add);
void affect_to_room(struct room_data* room, struct affected_type* af);
void affect_remove_room(struct room_data* room, struct affected_type* af);
void affect_from_room(struct room_data* room, byte skill);

void affect_total(struct char_data* ch, int mode = AFFECT_TOTAL_UPDATE);
void affect_modify(struct char_data* ch, byte loc, int mod, long bitv, char add, sh_int counter);
void affect_to_char(struct char_data* ch, struct affected_type* af);
void affect_remove_notify(struct char_data*, struct affected_type*);
void affect_remove(struct char_data* ch, struct affected_type* af);
void affect_from_char_notify(struct char_data*, byte);
void affect_from_char(struct char_data* ch, byte skill);

#include <stddef.h>

affected_type* affected_by_spell(const char_data* character, byte skill, affected_type* firstaf = 0);
affected_type* room_affected_by_spell(const room_data* room, int spell);

void affect_join(struct char_data* ch, struct affected_type* af,
    char avg_dur, char avg_mod);

/* placement.cpp -- Stage-1 API (placement-seam Task 1; spec's location-
 * representation section). Room ids are the public currency; room_by_id()/
 * room_by_id_total()/zone_by_id()/obj_index_by_id() are the explicit
 * escalation to a resolved pointer -- each dispatches through
 * entity_hooks.h into rots_world's registered implementation; an
 * unregistered hook is a distinct, louder failure (tripwire abort) for all
 * four. room_by_id()/zone_by_id()/obj_index_by_id() return nullptr for an
 * out-of-range id (a normal "absent" result); room_by_id_total() instead
 * preserves room_data::operator[]'s historical graceful fallback (a
 * resolved room even out of range) for callers whose original code
 * indexed world[] unchecked -- see entity_hooks.h's fuller two-variant
 * contract comment for why the room resolver alone needed both.
 * location_of()/set_location()/is_in_room() are thin wrappers over
 * char_data::in_room -- no hook needed, char_data is an L1 core struct.
 * Defined in placement.cpp. */
struct room_data* room_by_id(int rnum);
struct room_data* room_by_id_total(int rnum);
struct zone_data* zone_by_id(int znum);
struct index_data* obj_index_by_id(int item_number);
int location_of(const struct char_data* ch);
void set_location(struct char_data* ch, int rnum);
bool is_in_room(const struct char_data* ch, int rnum);

// LS-1 Wave Task 1 addition (.superpowers/sdd/ls1-census.md Step 5,
// census-justified by ~161 counted self-room `world[X->in_room]` read
// sites the wave's T2 conversions collapse onto this call). Self-room
// convenience folding the ch->in_room read and the world[] resolve into
// one call: returns room_by_id_total(location_of(ch)) -- never null,
// preserving room_by_id_total()'s graceful out-of-range fallback rather
// than room_by_id()'s nullptr-on-invalid contract. Defined in
// placement.cpp. Consumer-free as landed -- T2's conversions are the
// first callers.
struct room_data* room_of(const struct char_data* ch);

/* utility */
struct obj_data* create_money(int amount);
/// Returns whether bounded `query` matches a word in `name_list` using legacy prefix rules.
int isname(std::string_view query, std::string_view name_list, char full = 1);
/// Matches nullable legacy text pointers without constructing a view from a null value.
int isname_nullable(const char* query, const char* name_list, char full = 1);

// NumberedName moved to rots/platform/numbered_name.h (combat-seed Task 3):
// parse_numbered_name() below now lives in rots_util.cpp (rots_platform,
// L0), which cannot include this app-tier header; the type moved with it.
// Compatibility include below keeps every existing handler.h-including
// caller seeing the identical spelling/layout.
#include "rots/platform/numbered_name.h"
NumberedName parse_numbered_name(std::string_view input);
char* fname(char* namelist);

/* ******** objects *********** */

void obj_to_char(struct obj_data* object, struct char_data* ch);
void obj_from_char(struct obj_data* object);

void equip_char(struct char_data* ch, struct obj_data* obj, int pos);
struct obj_data* unequip_char(struct char_data* ch, int pos);
// attach_equipment()/detach_equipment() (placement-seam Task 3): the
// equip_char()/unequip_char() SPLIT primitives (equipment.cpp, L2) --
// public API, per this wave's "declarations stay in current headers"
// constraint. See equipment.cpp's top-of-file comment and handler.cpp's
// equip_char()/unequip_char() wrapper comments for the split-line
// accounting and task-3-report.md for the reassembly audit.
//
// EquipAttachOutcome (controller adjudication, task-3-report.md): the
// status attach_equipment() returns so its wrapper equip_char() can
// select which "app remainder" tail to run WITHOUT re-evaluating either
// of attach_equipment()'s own guard conditions a second time -- the
// `(pos == HOLD) && !CAN_WEAR(obj, ITEM_HOLD)` early-return test, and the
// `GET_ITEM_TYPE(obj) == ITEM_WEAPON` dispatch test. Each of those two
// ORIGINAL conditions now evaluates exactly once, inside
// attach_equipment(); see equipment.cpp's and handler.cpp's equip_char()
// comments for the exact mapping.
//
// CRITICAL FIX (task-3 re-review): the too-heavy CHECK
// (`GET_OBJ_WEIGHT(obj) > GET_BAL_STR(ch)*50 && !IS_TWOHANDED(ch)` /
// `> GET_BAL_STR(ch)*100`) reads character stats affect_modify() below
// can mutate (an APPLY_STR affect changes GET_BAL_STR; a bitvector affect
// can set AFF_TWOHANDED) -- the ORIGINAL evaluated it BEFORE the affect
// loop, inside the WEAPON dispatch arm. An earlier version of this split
// evaluated it in the wrapper, AFTER attach_equipment() had already run
// affect_modify()/affect_total() -- an observable behavior change goldens
// cannot see (whether the warning fires depends on the item's own
// affects). The check itself now lives in attach_equipment(), at the
// ORIGINAL's exact position, and its OUTCOME (not a raw bool) is what
// crosses the call boundary -- WEAPON/WEAPON_TOO_HEAVY_ONE_HAND/
// WEAPON_TOO_HEAVY_FOR_YOU below. The wrapper only chooses which
// send_to_char() text to emit (or none); it performs no stat comparison
// of its own.
enum class EquipAttachOutcome {
    HOLD_EARLY_RETURN, // the (pos == HOLD) && !CAN_WEAR(obj, ITEM_HOLD) guard
                       // fired; matches the ORIGINAL's own bare `return;` at that
                       // point -- the wrapper must run nothing further.
    WEAPON, // ran to completion; GET_ITEM_TYPE(obj) == ITEM_WEAPON, neither
            // too-heavy condition held -- the wrapper emits no message.
    WEAPON_TOO_HEAVY_ONE_HAND, // ran to completion; a weapon, and
                               // GET_OBJ_WEIGHT(obj) > GET_BAL_STR(ch)*50 &&
                               // !IS_TWOHANDED(ch) held (evaluated pre-affect,
                               // inside the primitive) -- the wrapper emits
                               // "This weapon seems too heavy for one hand.".
    WEAPON_TOO_HEAVY_FOR_YOU, // ran to completion; a weapon, the one-hand
                              // condition above did not hold but
                              // GET_OBJ_WEIGHT(obj) > GET_BAL_STR(ch)*100 did
                              // (same pre-affect evaluation) -- the wrapper
                              // emits "This weapon seems too heavy for you!".
    OTHER, // ran to completion; not a weapon -- no too-heavy check applies
           // (matches the ORIGINAL structure, where that check lived only
           // inside the WEAPON else-if arm).
};
EquipAttachOutcome attach_equipment(struct char_data* ch, struct obj_data* obj, int pos);
struct obj_data* detach_equipment(struct char_data* ch, int pos);

struct obj_data* get_obj_in_list(char* name, struct obj_data* list);
struct obj_data* get_obj_in_list_num(int num, struct obj_data* list);
struct obj_data* get_obj_in_list_vnum(int vnum, struct obj_data* list);
struct obj_data* get_obj_in_list_num_containers(int num, struct obj_data* list);
int count_obj_in_list(int num, struct obj_data* list);
/* returns the number of objects with this virtual number.
    if num == 0, returns the total number of objects */

struct obj_data* get_obj(char* name);
// get_obj_num() DELETED (placement-seam Task 2): census-flagged DEAD, 0
// callers repo-wide -- see object_utils.cpp's relocation comment.

void obj_to_room(struct obj_data* object, int room);
void obj_from_room(struct obj_data* object);
void obj_to_obj(struct obj_data* obj, struct obj_data* obj_to, char change_weight = 1);
void obj_from_obj(struct obj_data* obj);
void object_list_new_owner(struct obj_data* list, struct char_data* ch);

void extract_obj(struct obj_data* obj);

/* ******* characters ********* */
int other_side(const char_data* character, const char_data* other);
int other_side_num(int ch_race, int i_race);

struct char_data* get_char_room(char* name, int room);
// get_char_num() DELETED (placement-seam Task 4): census-flagged DEAD, 0
// callers repo-wide -- see entity_lifecycle.cpp's relocation comment.
/// Finds a world character whose name matches the bounded lookup text.
struct char_data* get_char(std::string_view name);

// detach_char_from_room() (placement-seam Task 3): the char_from_room()
// SPLIT primitive (placement.cpp, L2) -- public API, per this wave's
// "declarations stay in current headers" constraint. See
// placement.cpp's and handler.cpp's char_from_room() comments for the
// split-line accounting. bool return (controller adjudication,
// supersedes this task's original void signature -- see
// task-3-report.md): false on either of the ORIGINAL char_from_room's
// two early-return paths (both of which skipped its trailing
// stop_fighting loop), true after the full detach; the wrapper
// branches on this instead of re-deriving either condition.
bool detach_char_from_room(struct char_data* ch);
void char_from_room(struct char_data* ch);

// Registers handler.cpp's real char_from_room(ch) body above as
// entity_hooks.h's char-from-room hook (behavior wave Task 1; census
// section 11). Called once from run_the_game(), before boot_db() --
// same convention as register_extract_char_hook() below.
void register_char_from_room_hook();
void char_to_room(struct char_data* ch, int room);
void extract_char(struct char_data* ch, int new_room = -1);

// Registers handler.cpp's real extract_char(ch, new_room) body as
// entity_hooks.h's extract_char hook (originally combat_hooks.h,
// combat-pilot wave Task 4b, pilot-census.md section 3.6; RE-HOMED to L2
// entity_hooks.h by the l4-seed wave Task 1 so both rots_world and
// rots_combat can dispatch through the same shared inversion -- see
// l4-task-1-report.md). Called once from run_the_game(), before boot_db()
// -- same convention as this file's other registrars (e.g.
// register_poison_removal_hook() below).
void register_extract_char_hook();

// in_affected_list() DELETED (placement-seam Task 4): census-flagged DEAD, 0
// callers repo-wide -- see entity_lifecycle.cpp's relocation comment.

/* find if character can see */
int keyword_matches_char(struct char_data*, struct char_data*, char*);
struct char_data* get_char_room_vis(struct char_data* ch, char* name,
    int dark_ok = 0);
struct char_data* get_player_vis(struct char_data* ch, char* name);
struct char_data* get_char_vis(struct char_data* ch, char* name,
    int dark_ok = 0);
/// Finds a visible object whose keywords match the bounded lookup text.
struct obj_data* get_obj_in_list_vis(struct char_data* ch, std::string_view name,
    struct obj_data* list, int num);
struct obj_data* get_obj_vis(struct char_data* ch, char* name);
struct obj_data* get_object_in_equip_vis(struct char_data* ch,
    char* arg, struct obj_data* equipment[], int* j);

void add_follower(struct char_data* ch, struct char_data* leader, int mode);
void stop_follower(struct char_data* ch, int mode);
char circle_follow(struct char_data* ch, struct char_data* victim, int mode);
void die_follower(struct char_data* ch);

#define FOLLOW_MOVE 1
#define FOLLOW_GROUP 2
#define FOLLOW_REFOL 3

/* find all dots */

int find_all_dots(char* arg);

#define FIND_INDIV 0
#define FIND_ALL 1
#define FIND_ALLDOT 2

/* Generic Find */

int generic_find(char* arg, int bitvector, struct char_data* ch,
    struct char_data** tar_ch, struct obj_data** tar_obj);

#define FIND_CHAR_ROOM 1
#define FIND_CHAR_WORLD 2
#define FIND_OBJ_INV 4
#define FIND_OBJ_ROOM 8
#define FIND_OBJ_WORLD 16

#define FIND_OBJ_EQUIP 32

/* prototypes from crash save system */

int Crash_get_filename(std::string_view original_name, char* filename);
int Crash_delete_file(std::string_view name);
int Crash_delete_crashfile(struct char_data* ch);
int Crash_clean_file(char* name);
void Crash_listrent(struct char_data* ch, char* name);
// int	Crash_load(struct char_data *ch);
void Crash_crashsave(struct char_data* ch, int rent_code = RENT_CRASH);

// Registers objsave.cpp's real Crash_crashsave(ch, rent_code) body above as
// combat_hooks.h's crash_crashsave hook (combat-pilot wave Task 4b;
// pilot-census.md section 3.7). Called once from run_the_game(), before
// boot_db() -- same convention as register_extract_char_hook() above.
void register_crash_crashsave_hook();
void Crash_idlesave(struct char_data* ch);

// Registers objsave.cpp's real Crash_idlesave(ch)/Crash_extract_objs(obj)
// bodies as combat_hooks.h's matching sibling hooks (behavior wave
// Task 1; census section 8). Called once from run_the_game(), before
// boot_db() -- same convention as register_crash_crashsave_hook() above.
void register_crash_idlesave_hook();
void register_crash_extract_objs_hook();
void Crash_save_all(void);
FILE* Crash_get_file_by_name(std::string_view name, std::string_view mode);
FILE* Crash_load(struct char_data* ch);
void stage_account_backed_object_data_for_character(const struct char_data* ch, const objects_json::ObjectSaveData& data);
void clear_account_backed_object_bytes_for_character(const struct char_data* ch);
// Registers clear_account_backed_object_bytes_for_character() (above) as
// entity_hooks.h's char-teardown hook. Called once from run_the_game(),
// before boot_db() (entity-seed Task 5).
void register_char_teardown_hook();
objects_json::ObjectSaveData build_default_account_backed_object_data();

// Promoted to handler.h (persist-split PS Task 2, obj_files.cpp carve):
// objsave.cpp's Crash_listrent()/Crash_load()/Crash_crashsave()-family (stay
// behind) call these two cross-TU now that the P inventory moved.
std::string player_objects_json_path(std::string_view player_name);
bool write_player_objects_json(std::string_view player_name, const objects_json::ObjectSaveData& data, std::string* error);

/* prototypes from fight.c */
void set_fighting(struct char_data* ch, struct char_data* victim);
void stop_fighting(struct char_data* ch);
void hit(struct char_data* ch, struct char_data* victim, int type);
void forget(struct char_data* ch, struct char_data* victim);
void remember(struct char_data* ch, struct char_data* victim);
int damage(struct char_data* ch, struct char_data* victim, int dam, int attacktype, int hit_location);
int check_sanctuary(char_data* ch, char_data* victim);
// Registers fight.cpp's poison_removal_hook_impl() as entity_hooks.h's
// poison-removal notification. Called once from run_the_game(), before
// boot_db() (blocker-buster wave Task 3).
void register_poison_removal_hook();

char* money_message(int sum, int mode = 0);

int char_exists(int num);
void set_char_exists(int num);
void remove_char_exists(int num);
int register_npc_char(struct char_data*);
int register_pc_char(struct char_data*);

int can_swim(struct char_data* ch);

void stop_riding(struct char_data* ch);
void stop_riding_all(struct char_data* mount); /*emergency dismount */
int char_power(int lev);
void recalc_zone_power();
int report_zone_power(char_data* ch);

#endif /* HANDLER_H */
