#include "object_utils.h"
#include "char_utils.h"
#include "rots/core/character.h"
#include "rots/core/object.h"
#include "rots/core/types.h"

#include <cstdlib>
#include <cstring>
#include <format>

#include "db.h" // clear_object() declaration (entity_lifecycle.cpp, L2), used by
                 // create_money() below. free_obj() is NOT called by anything in this
                 // file today (placement-seam Task 2 review Minor, fixed Task 3): it was
                 // anticipated for extract_obj(), which stayed app-side (see extract_obj's
                 // own STOP-CHECK-adjudicated deferral note, handler.cpp, and
                 // task-3-report.md) -- re-add free_obj to this comment when extract_obj
                 // finally moves here.
#include "entity_hooks.h"
#include "handler.h"
#include "utils.h"

// New members (placement-seam wave, Task 2; plan
// docs/superpowers/plans/2026-07-19-placement-seam.md; census
// .superpowers/sdd/placement-census.md). Joins containment.cpp's mutation
// family with the "query/lifecycle family" of the containment carve out of
// handler.cpp -- nine functions relocated verbatim from handler.cpp (census
// rows get_obj_in_list:1050, get_obj_in_list_num:1073,
// get_obj_in_list_vnum:1085 (ADJUDICATE-3), get_obj_in_list_num_containers
// :1100, count_obj_in_list:1116, get_obj:1129, update_object:1413,
// update_char_objects:1424, create_money:1794; line numbers as recorded at
// census time, before Task 1 shifted handler.cpp) except where
// get_obj_in_list_vnum's obj_index[] access is replaced by Task 1's
// obj_index_by_id() resolver -- see that function below for the exact
// substitution. Per the BINDING addendum (task-2-brief.md): obj_index is a
// raw C array with no operator[]-style fallback wrapper (unlike room_data),
// so obj_index_by_id() has only the ONE nullptr-on-invalid variant, and the
// original call site already read it unchecked (genuine UB on an
// out-of-range item_number) -- the addendum says deref its result directly,
// matching the original's own unchecked access, rather than adding a new
// null check the original never had.
//
// get_obj_num (handler.cpp:1152, census-flagged DEAD -- 0 callers
// repo-wide, re-verified via a fresh grep at this task's start; only
// reference left anywhere was its own handler.h declaration) is DELETED
// rather than moved, per the census's recommendation and this wave's
// Global Constraints (dead functions with 0 callers are deleted, not
// relocated).
//
// extract_obj (census row 1367, ADJUDICATE-3) is STILL NOT here as of
// Task 3 either (STOP-CHECK-adjudicated deferral, Disposition B -- see
// task-3-report.md, supersedes this note's original Task-2-only
// rationale below). See handler.cpp's own relocation comment at
// extract_obj's still-resident definition (and containment.cpp's/
// handler.cpp's obj_from_char comments) for the full evidence: extract_obj
// calls obj_from_char(), which stays app-side because a live mudscript
// path (script.cpp SCRIPT_ASSIGN_EQ + SCRIPT_OBJ_FROM_CHAR) can reach
// obj_from_char's equipment-fallback branch with a genuinely equipped
// object, where only the app-tier unequip_char() wrapper (not
// equipment.cpp's detach_equipment() primitive) preserves the poison
// damage/raw_kill side effect -- a real behavior-preservation blocker,
// not merely the Task-2-era link-ordering one this note originally
// described ("ITS OWN unequip_char() dependency isn't L2-resolvable
// until Task 3's equipment SPLIT" -- that SPLIT landed this task, and
// resolved nothing here). get_obj_in_list_vnum has no such dependency,
// so it moved on schedule (Task 2) -- ADJUDICATE-3's obj_index_by_id
// resolver is delivered either way.
extern struct obj_data* object_list;

//============================================================================
// Utility functions that take in an obj_data object.
//============================================================================
namespace utils {
//============================================================================
bool is_artifact(const obj_data&)
{
    // drelidan:  This macro always returns false.
    return false;
}

//============================================================================
int get_item_type(const obj_data& object) { return object.obj_flags.type_flag; }

//============================================================================
bool can_wear(const obj_data& object, int body_part)
{
    return utils::is_set(object.obj_flags.wear_flags, body_part);
}

//============================================================================
int get_object_weight(const obj_data& object) { return object.obj_flags.weight; }

//============================================================================
bool is_object_stat(const obj_data& object, int stat)
{
    return utils::is_set(object.obj_flags.extra_flags, stat);
}

//============================================================================
int get_item_bulk(const obj_data& object) { return object.obj_flags.value[2]; }

} // namespace utils

//============================================================================
// Implementations of functions defined in structs.h
//============================================================================
namespace game_types {
const char* get_weapon_name(weapon_type type)
{
    static const std::string_view weapon_types[WT_COUNT] = {
        "Error, Unused weapon type, contact Imms",
        "Error, Unused weapon type, contact Imms",
        "whipping",
        "slashing",
        "two-handed slashing",
        "flailing",
        "bludgeoning",
        "bludgeoning",
        "cleaving",
        "two-handed cleaving",
        "stabbing",
        "piercing",
        "smiting",
        "bow",
        "crossbow",
    };

    return weapon_types[type].data();
}
} // namespace game_types

//============================================================================
bool obj_data::is_quiver() const
{
    if (obj_flags.type_flag == ITEM_CONTAINER) {
        return isname_nullable("quiver", name);
    }
    return false;
}

//============================================================================
bool obj_flag_data::is_wearable() const
{
    static int WEARABLE_ITEMS[7] = {
        ITEM_WAND,
        ITEM_STAFF,
        ITEM_WEAPON,
        ITEM_FIREWEAPON,
        ITEM_ARMOR,
        ITEM_WORN,
        ITEM_SHIELD,
    };

    for (int index = 0; index < 7; index++) {
        if (WEARABLE_ITEMS[index] == type_flag) {
            return true;
        }
    }

    return false;
}

//============================================================================
bool obj_data::is_ranged_weapon() const
{
    if (obj_flags.type_flag == ITEM_WEAPON) {
        game_types::weapon_type w_type = get_weapon_type();
        return w_type == game_types::WT_BOW || w_type == game_types::WT_CROSSBOW;
    }

    return false;
}

//============================================================================
// Functions relocated verbatim from handler.cpp (placement-seam Task 2 --
// see this file's own top-of-file relocation comment for the census rows
// and the one ADJUDICATE-3 resolver substitution below).
//============================================================================

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
//
// obj_index[i->item_number].virt substitution (census row
// get_obj_in_list_vnum:1085, ADJUDICATE-3): the ORIGINAL body read
// obj_index[i->item_number].virt unchecked, guarded only by the
// `i->item_number >= 0` ternary already in the source (no resolver-style
// bounds check) -- per the addendum, obj_index_by_id() is dereferenced
// directly in its place with no new null check added.
struct obj_data* get_obj_in_list_vnum(int vnum, struct obj_data* list)
{
    struct obj_data* i;

    if (vnum == 0)
        return 0;

    for (i = list; i; i = i->next_content)
        if (((i->item_number >= 0) ? obj_index_by_id(i->item_number)->virt : 0) == vnum)
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
            new_descr->description = str_dup(std::format("It looks to be about {} gold coins.", (amount / COPP_IN_GOLD))
                    .c_str());
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
