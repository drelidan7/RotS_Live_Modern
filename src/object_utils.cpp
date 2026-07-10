#include "object_utils.h"
#include "char_utils.h"
#include "structs.h"

#include "handler.h"
#include "spells.h"
#include "utils.h"

//============================================================================
// Utility functions that take in an obj_data object.
//============================================================================
namespace utils {
//============================================================================
bool is_artifact(const obj_data& object)
{
    // drelidan:  This macro always returns false.
    return false;
}

//============================================================================
int get_item_type(const obj_data& object)
{
    return object.obj_flags.type_flag;
}

//============================================================================
bool can_wear(const obj_data& object, int body_part)
{
    return utils::is_set(object.obj_flags.wear_flags, body_part);
}

//============================================================================
int get_object_weight(const obj_data& object)
{
    return object.obj_flags.weight;
}

//============================================================================
bool is_object_stat(const obj_data& object, int stat)
{
    return utils::is_set(object.obj_flags.extra_flags, stat);
}

//============================================================================
int get_item_bulk(const obj_data& object)
{
    return object.obj_flags.value[2];
}

}

//============================================================================
// Implementations of functions defined in structs.h
//============================================================================
namespace game_types {
const char* get_weapon_name(weapon_type type)
{
    static const char* weapon_types[WT_COUNT] = {
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

    return weapon_types[type];
}
}

//============================================================================
bool obj_data::is_quiver() const
{
    if (obj_flags.type_flag == ITEM_CONTAINER) {
        return isname("quiver", name);
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
