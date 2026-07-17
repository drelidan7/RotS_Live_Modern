#pragma once
// rots/core/object.h — object flag/material/liquid/container constants and the
// obj_data entity. Other entities are pointer-only (rots/core/fwd.h).
#include "rots/core/fwd.h"
#include "rots/core/types.h"

/* The following defs are for obj_data  */

/* For 'type_flag' */

#define ITEM_LIGHT 1
#define ITEM_SCROLL 2
#define ITEM_WAND 3
#define ITEM_STAFF 4
// ITEM_WEAPON (5) lives in rots/core/types.h (Task 2 forward-pull: weapon_flag_data's
// constructor asserts against it there before this header exists).
#define ITEM_FIREWEAPON 6
#define ITEM_MISSILE 7
#define ITEM_TREASURE 8
#define ITEM_ARMOR 9
#define ITEM_POTION 10
#define ITEM_WORN 11
#define ITEM_OTHER 12
#define ITEM_TRASH 13
#define ITEM_TRAP 14
#define ITEM_CONTAINER 15
#define ITEM_NOTE 16
#define ITEM_DRINKCON 17
#define ITEM_KEY 18
#define ITEM_FOOD 19
#define ITEM_MONEY 20
#define ITEM_PEN 21
#define ITEM_BOAT 22
#define ITEM_FOUNTAIN 23
#define ITEM_SHIELD 24
#define ITEM_LEVER 25

/* Bitvector for item materials (used in shop.cc) */

// #define MATERIAL_USUAL_STUFF	(1 << 0)
#define MATERIAL_CLOTH (1 << 0)
#define MATERIAL_LEATHER (1 << 1)
#define MATERIAL_CHAIN (1 << 2)
#define MATERIAL_METAL (1 << 3)
#define MATERIAL_WOOD (1 << 4)
#define MATERIAL_STONE (1 << 5)
#define MATERIAL_CRYSTAL (1 << 6)
#define MATERIAL_GOLD (1 << 7)
#define MATERIAL_SILVER (1 << 8)
#define MATERIAL_MITHRIL (1 << 9)
#define MATERIAL_FUR (1 << 10)
#define MATERIAL_GLASS (1 << 11)

/* Bitvector For 'wear_flags' */

#define ITEM_TAKE 1
#define ITEM_WEAR_FINGER 2
#define ITEM_WEAR_NECK 4
#define ITEM_WEAR_BODY 8
#define ITEM_WEAR_HEAD 16
#define ITEM_WEAR_LEGS 32
#define ITEM_WEAR_FEET 64
#define ITEM_WEAR_HANDS 128
#define ITEM_WEAR_ARMS 256
#define ITEM_WEAR_SHIELD 512
#define ITEM_WEAR_ABOUT 1024
#define ITEM_WEAR_WAISTE 2048
#define ITEM_WEAR_WRIST 4096
#define ITEM_WIELD 8192
#define ITEM_HOLD 16384
#define ITEM_THROW 32768
#define ITEM_WEAR_BACK 65536
#define ITEM_WEAR_BELT 131072
/* UNUSED, CHECKS ONLY FOR ITEM_LIGHT #define ITEM_LIGHT_SOURCE  65536 */

/* Bitvector for 'extra_flags' */

#define ITEM_GLOW (1 << 0)
#define ITEM_HUM (1 << 1)
#define ITEM_DARK (1 << 2)
#define ITEM_BREAKABLE (1 << 3) /* was ITEM_LOCK - now used to indicate a breakable item */
#define ITEM_EVIL (1 << 4)
#define ITEM_INVISIBLE (1 << 5)
#define ITEM_MAGIC (1 << 6)
#define ITEM_NODROP (1 << 7)
#define ITEM_BROKEN (1 << 8) /* was ITEM_BLESS - now used to break keys etc */
#define ITEM_ANTI_GOOD (1 << 9) /* not usable by good people    */
#define ITEM_ANTI_EVIL (1 << 10) /* not usable by evil people    */
#define ITEM_ANTI_NEUTRAL (1 << 11) /* not usable by neutral people */
#define ITEM_NORENT (1 << 12) /* not allowed to rent the item */
#define ITEM_NOINVIS (1 << 14) /* not allowed to cast invis on */
#define ITEM_WILLPOWER (1 << 15) /* indicates a weapon which can damage a wraith */
#define ITEM_IMM (1 << 16)
#define ITEM_HUMAN (1 << 17)
#define ITEM_DWARF (1 << 18)
#define ITEM_WOODELF (1 << 19)
#define ITEM_HOBBIT (1 << 20)
#define ITEM_BEORNING (1 << 21)
#define ITEM_URUK (1 << 22)
#define ITEM_ORC (1 << 23)
#define ITEM_MOBORC (1 << 24)
#define ITEM_MAGUS (1 << 25)
#define ITEM_OLOGHAI (1 << 26)
#define ITEM_HARADRIM (1 << 27)
#define ITEM_STAY_ZONE (1 << 28)

/* Some different kind of liquids */
#define LIQ_WATER 0
#define LIQ_BEER 1
#define LIQ_WINE 2
#define LIQ_ALE 3
#define LIQ_DARKALE 4
#define LIQ_WHISKY 5
#define LIQ_LEMONADE 6
#define LIQ_FIREBRT 7
#define LIQ_LOCALSPC 8
#define LIQ_SLIME 9
#define LIQ_MILK 10
#define LIQ_TEA 11
#define LIQ_COFFE 12
#define LIQ_BLOOD 13
#define LIQ_SALTWATER 14
#define LIQ_CLEARWATER 15

/* for containers  - value[1] */

#define CONT_CLOSEABLE 1
#define CONT_PICKPROOF 2
#define CONT_CLOSED 4
#define CONT_LOCKED 8

/* ======================== Structure for object ========================= */
struct obj_data {
public:
    int get_ob_coef() const { return obj_flags.get_ob_coef(); }
    int get_parry_coef() const { return obj_flags.get_parry_coef(); }
    int get_bulk() const { return obj_flags.get_bulk(); }
    game_types::weapon_type get_weapon_type() const { return obj_flags.get_weapon_type(); }
    int get_level() const { return obj_flags.get_level(); }
    int get_weight() const { return obj_flags.get_weight(); }

    int get_base_damage_reduction() const { return obj_flags.get_base_damage_reduction(); }

    const char_data* get_owner() const { return carried_by; }

    // Returns true if the object can be equipped.
    bool is_wearable() const { return obj_flags.is_wearable(); }

    // Returns true if the object is a quiver.
    bool is_quiver() const;

    // Returns true if the weapon is a ranged weapon.
    bool is_ranged_weapon() const;

public:
    int item_number; /* Where in data-base               */
    int in_room; /* In what room -1 when conta/carr  */

    struct obj_flag_data obj_flags; /* Object information               */
    struct obj_affected_type affected[MAX_OBJ_AFFECT]; /* Which abilities in PC to change  */

    char* name; /* Title of object :get etc.        */
    char* description; /* When in room                     */
    char* short_description; /* when worn/carry/in cont.         */
    char* action_description; /* What to write when used          */
    struct extra_descr_data* ex_description; /* extra descriptions     */
    struct char_data* carried_by; /* Carried by :NULL in room/conta   */

    int owner;
    struct obj_data* in_obj; /* In what object NULL when none    */
    struct obj_data* contains; /* Contains objects                 */

    struct obj_data* next_content; /* For 'contains' lists             */
    struct obj_data* next; /* For the object list              */
    int touched; /* Has a PC touched this object?    */
    int loaded_by; /* idnum of immortal who loaded the object (else 0) */
};
