#pragma once
// rots/core/types.h — leaf of the data-model DAG (spec §5): primitive-typedef
// include, global constants, enums, and pure value structs. Entity types are
// visible only as forward declarations (rots/core/fwd.h); this header defines
// no entity and includes no entity header.
//
// The two relative includes below reach headers that still live flat in src/.
// They are relative on purpose: src/ must never join an include path
// (src/limits.h shadows <limits.h>, and MSVC has no -idirafter), and the
// "directory of the including file" rule resolves these identically on
// GCC/Clang/MSVC. They become pathed when platform/app headers relocate.
#include "../../../../color.h" // color_slot_data, MAX_COLOR_FIELDS (char_prof_data)
#include "../../../../platdef.h" // sh_int/ush_int/byte/sbyte/ubyte, SocketType
#include "rots/core/fwd.h"

#include <algorithm>
#include <assert.h>
#include <set>
#include <stdio.h>
#include <string_view>
#include <sys/types.h>
#include <vector>

const int constexpr MAX_SPIRITS = 90000;

const int constexpr ENE_TO_HIT = 1200;
const int constexpr BAREHANDED_DAMAGE = 2;
const int constexpr PRACS_PER_LEVEL = 3;
const int constexpr LEA_PRAC_FACTOR = 5;
const int constexpr LIGHT_WEAPON_WEIGHT_CUTOFF = 235;

const int constexpr PLAYER_RACE_MAX = 100;

const int constexpr INVALID_GUARDIAN = -1;
const int constexpr AGGRESSIVE_GUARDIAN = 0;
const int constexpr DEFENSIVE_GUARDIAN = 1;
const int constexpr MYSTIC_GUARDIAN = 2;

const int constexpr LEVEL_IMPL = 100;
const int constexpr LEVEL_GRGOD = 97;
const int constexpr LEVEL_AREAGOD = 95;
const int constexpr LEVEL_PERMIMM = 94;
const int constexpr LEVEL_GOD = 93;
const int constexpr LEVEL_IMMORT = 91;
const int constexpr LEVEL_MINIMM = LEVEL_IMMORT; /* The lowest immortal level */
const int constexpr LEVEL_MAX = 30;

const int constexpr LEVEL_FREEZE = LEVEL_PERMIMM;

const int constexpr NUM_OF_DIRS = 6;
const int constexpr PULSE_ZONE = 12;
const int constexpr PULSE_MOBILE = 24;
const int constexpr PULSE_VIOLENCE = 12;
const int constexpr PULSE_FAST_UPDATE = 12;
const int constexpr PULSE_MENTAL_FIGHT = 8;

const int constexpr MAX_CHARACTERS = 64000;
const int constexpr MAX_PCCHARACTERS = 32000;
const int constexpr SMALL_BUFSIZE = 512;
const int constexpr LARGE_BUFSIZE = 16384;
const int constexpr MAX_STRING_LENGTH = 8192;
const int constexpr MAX_INPUT_LENGTH = 255;
const int constexpr MAX_MESSAGES = 255;
const int constexpr MAX_ITEMS = 153;
const int constexpr MAX_RACES = 32;
const int constexpr MAX_BODYTYPES = 16;
const int constexpr MAX_BODYPARTS = 11;
const int constexpr MAX_RACE_NAME_LENGTH = 14;
const int constexpr MIN_NAME_LENGTH = 3;
const int constexpr MAX_NAME_LENGTH = 12;
const int constexpr MAX_PWD_LENGTH = 10; /* Used in char_file_u *DO*NOT*CHANGE* */
const int constexpr MAX_ACCOUNT_PASSWORD_LENGTH = 255;
const int constexpr HOST_LEN = 30; /* Used in char_file_u *DO*NOT*CHANGE* */
const int constexpr MAX_MAXBOARD = 22; /* the max number of boards ever -  */
/*   used in objsave                */

#define MAX_ZONES 500

#define BODYTYPE_ANIMAL 2

#define MESS_ATTACKER 1
#define MESS_VICTIM 2
#define MESS_ROOM 3

const int constexpr SECS_PER_REAL_MIN = 60;
const int constexpr SECS_PER_REAL_HOUR = (60 * SECS_PER_REAL_MIN);
const int constexpr SECS_PER_REAL_DAY = (24 * SECS_PER_REAL_HOUR);
const int constexpr SECS_PER_REAL_YEAR = (365 * SECS_PER_REAL_DAY);

const int constexpr SECS_PER_MUD_HOUR = 60;
const int constexpr TICS_PER_SECOND = 4;
const int constexpr FAST_UPDATE_RATE = SECS_PER_MUD_HOUR * TICS_PER_SECOND / PULSE_FAST_UPDATE;

const int constexpr SECS_PER_MUD_DAY = (24 * SECS_PER_MUD_HOUR);
const int constexpr SECS_PER_MUD_MONTH = (30 * SECS_PER_MUD_DAY);
const int constexpr SECS_PER_MUD_YEAR = (12 * SECS_PER_MUD_MONTH);

#define COPP_IN_GOLD 1000
#define COPP_IN_SILV 100

#define SMALL_WORLD_RADIUS 10

#define WORLD_SIZE_X 50
#define WORLD_SIZE_Y 26
#define WORLD_AREA ((WORLD_SIZE_X + 4) * (WORLD_SIZE_Y + 2))

#define NOWHERE -1 /* nil reference for room-database    */

#define MAX_OBJ_AFFECT 2 /* Used in OBJ_FILE_ELEM *DO*NOT*CHANGE* */
#define OBJ_NOTIMER -700 /*changed*/
#define WEAPON_POISON_DUR 60

/* For 'char_payer_data' */

#define MAX_TOUNGE 3 /* Used in CHAR_FILE_U *DO*NOT*CHANGE* */
#define MAX_SKILLS 256 /* Used in CHAR_FILE_U *DO*NOT*CHANGE* */
#define MAX_WEAR 22
#define MAX_AFFECT 32 /* Used in CHAR_FILE_U *DO*NOT*CHANGE* */

typedef std::vector<struct char_data*> char_vector;
typedef char_vector::iterator char_iter;
typedef char_vector::const_iterator const_char_iter;

typedef std::set<struct char_data*> char_set;
typedef char_set::iterator char_set_iter;
typedef char_set::const_iterator const_char_set_iter;

struct combat_result_struct {
    combat_result_struct()
        : wants_to_flee(false)
        , will_die(false) {};
    combat_result_struct(bool wimpy, bool dead)
        : wants_to_flee(wimpy)
        , will_die(dead) {};

    bool wants_to_flee;
    bool will_die;
};

namespace game_types {
enum weapon_type {
    WT_UNUSED_1,
    WT_UNUSED_2,
    WT_WHIPPING,
    WT_SLASHING,
    WT_SLASHING_TWO,
    WT_FLAILING,
    WT_BLUDGEONING,
    WT_BLUDGEONING_TWO,
    WT_CLEAVING,
    WT_CLEAVING_TWO,
    WT_STABBING,
    WT_PIERCING,
    WT_SMITING,
    WT_BOW,
    WT_CROSSBOW,
    WT_COUNT,
};

const char* get_weapon_name(weapon_type type);
} // namespace game_types

namespace game_types {
enum player_specs {
    PS_None,
    PS_Fire,
    PS_Cold,
    PS_Regeneration,
    PS_Protection,
    PS_Animals,
    PS_Stealth,
    PS_WildFighting,
    PS_Teleportation,
    PS_Illusion,
    PS_Lightning,
    PS_Guardian,
    PS_HeavyFighting,
    PS_LightFighting,
    PS_Defender,
    PS_Archery,
    PS_Darkness,
    PS_Arcane,
    PS_WeaponMaster,
    PS_BattleMage,
    PS_Count,
};
}

enum source_type {
    SOURCE_PLAYER,
    SOURCE_MOB,
    SOURCE_ROOM,
    SOURCE_ITEM,
    SOURCE_OTHER,
    SOURCE_INVALID,
};

#define TAR_IGNORE (1 << 0)
#define TAR_CHAR_ROOM (1 << 1)
#define TAR_CHAR_WORLD (1 << 2)
#define TAR_SELF (1 << 3)
#define TAR_FIGHT_VICT (1 << 4)
#define TAR_SELF_ONLY (1 << 5)
#define TAR_SELF_NONO (1 << 6) /*Only a check, use with ei. TAR_CHAR_ROOM */
#define TAR_OBJ_INV (1 << 7)
#define TAR_OBJ_ROOM (1 << 8)
#define TAR_OBJ_WORLD (1 << 9)
#define TAR_OBJ_EQUIP (1 << 10)
#define TAR_NONE_OK (1 << 11)
#define TAR_DIR_NAME (1 << 12)
#define TAR_DIR_WAY (1 << 13)
#define TAR_DIRECTION (TAR_DIR_NAME | TAR_DIR_WAY)
#define TAR_TEXT (1 << 14)
#define TAR_TEXT_ALL (1 << 15)
#define TAR_ALL (1 << 16)
#define TAR_GOLD (1 << 17)
#define TAR_IN (1 << 18)
#define TAR_VALUE (1 << 19)
#define TAR_DARK_OK (1 << 20)
#define TAR_IN_OTHER (1 << 21)

/* dark_okay means, darkness is not counted against visibility */
/* tar_in_other is for "get meat sack", "steal sack wolf"      */
/* both are additional flags, to work with other target options*/

/* values for target_data... what is in the union, really */

#define TARGET_NONE 0
#define TARGET_CHAR 1
#define TARGET_OBJ 2
#define TARGET_ROOM 3
#define TARGET_TEXT 4
#define TARGET_GOLD 5
#define TARGET_DIR 6
#define TARGET_IN 7
#define TARGET_ALL 8
#define TARGET_VALUE 9
#define TARGET_OTHER 10
#define TARGET_IGNORE 11

#define GET_TARGET_TEXT(t1) ((t1)->ptr.text->text)

struct target_data {
    signed char type;
    union {
        struct char_data* ch;
        struct obj_data* obj;
        struct room_data* room;
        struct txt_block* text;
        void* other;
    } ptr;
    int ch_num; /* abs_number, if the target is a character, or just some
                digit data. int (not sh_int): abs_number is an int slot index
                allowed up to MAX_CHARACTERS (64000), and a truncated-negative
                value here would index char_control_array out of bounds via
                char_exists() (Backlog Cleanup T5, MSVC C4244 revisit). */
    int choice; /* what kind of target is this   */
    void cleanup(); /* cleans the target data, releases the text if nec. */
    void operator=(const target_data& t2);
    int operator==(const target_data& t2) const;

    target_data()
    {
        type = TARGET_NONE;
        ptr.other = 0;
        ch_num = 0;
        choice = 0;
    }

    // Explicit rule-of-three copy constructor (Phase 5 T5, -Wdeprecated-copy): operator= does
    // pool-managed deep-copy for TARGET_TEXT (see interpre.cpp), so the implicit memberwise
    // copy constructor would alias two target_data instances' ptr.text on the same pooled
    // txt_block -- a double-release/use-after-free once both are cleaned up. Delegate to the
    // same operator= logic instead of defaulting.
    target_data(const target_data& t2)
    {
        type = TARGET_NONE;
        ptr.other = 0;
        ch_num = 0;
        choice = 0;
        *this = t2;
    }
};

struct extra_descr_data {
    char* keyword; /* Keyword in look/examine          */
    char* description; /* What to see                      */
    struct extra_descr_data* next; /* Next in list                     */
};


struct waiting_type {
    int wait_value; /* number of ticks left to wait */
    int cmd; /* command to be performed after delay -
                                 0  = none,
                                 -1 = call special for NPC, none for PC */
    int subcmd; /* subcmd it is, probably as a chain flag
                                 for specials for NPCs */
    int priority; /* priority it is. 0 is the lowest */

    int flg; /* also for whatever needed, flags mostly */
    struct target_data targ1;
    struct target_data targ2;
    struct char_data* next;
};

// As-built deviation from the destination map (Task 2): ITEM_WEAPON is
// pulled forward from its mapped object.h location (ITEM_* type-flag group,
// spec range 116-236) because weapon_flag_data's constructor below asserts
// against it and object.h does not exist until Task 3. Object.h's ITEM_*
// carve-out must skip this line.
#define ITEM_WEAPON 5

struct obj_flag_data {
public:
    int get_ob_coef() const { return value[0]; }
    int get_parry_coef() const { return value[1]; }
    int get_bulk() const { return value[2]; }
    game_types::weapon_type get_weapon_type() const { return game_types::weapon_type(value[3]); }
    int get_level() const { return level; }
    int get_weight() const { return std::max(weight, 1); }

    int get_base_damage_reduction() const { return value[1]; }

    bool is_cloth() const { return material == 1; }
    bool is_leather() const { return material == 2; }
    bool is_chain() const { return material == 3; }
    bool is_metal() const { return material == 4; }

    // Returns true if the object can be equipped.
    bool is_wearable() const;

    // Poison information
    bool is_weapon_poisoned() const { return poisoned; }
    int get_poison_duration() const { return poisondata[0]; }
    int get_poison_strength() const { return poisondata[1]; }
    int get_poison_multipler() const { return poisondata[2]; }

public:
    int value[5]; /* Values of the item (see list) */ /*changed*/
    byte type_flag; /* Type of item                     */
    int wear_flags; /* Where you can wear it            */
    int extra_flags; /* If it hums,glows etc             */
    int weight; /* Weigt what else                  */
    int cost; /* Value when sold (gp.)            */
    sh_int cost_per_day; /* Cost to keep pr. real day        */
    int timer; /* Timer for object                 */
    long bitvector; /* To set chars bits                */
    ubyte level; /* level of an item (not to correspond to character's*/
    ubyte rarity; /* rarity of an item */
    signed char material; /* what is it made of               */
    sh_int butcher_item; /* virtual item to butcher, 0 for none, -1 for butchered */
    int prog_number; /* for special objects... */
    int script_number; /* identifies the script which is triggered under certain conditions */
    struct info_script* script_info; /* Pointer to char_script (protos.h) 0 if no script */
    bool poisoned;
    int poisondata[5];
};

/* Wraps the object_flag_data and exposes values that are used for weapons. */
struct weapon_flag_data {
    weapon_flag_data(obj_flag_data* data)
        : object_flag_data(data)
    {
        assert(data);
        assert(data->type_flag == ITEM_WEAPON);
    }

    int get_ob_coef() const { return object_flag_data->value[0]; }
    int get_parry_coef() const { return object_flag_data->value[1]; }
    int get_bulk() const { return object_flag_data->value[2]; }
    game_types::weapon_type get_weapon_type() const
    {
        return game_types::weapon_type(object_flag_data->value[3]);
    }

private:
    obj_flag_data* object_flag_data;
};

/* Used in OBJ_FILE_ELEM *DO*NOT*CHANGE* */
struct obj_affected_type {
    byte location; /* Which ability to change (APPLY_XXX) */
    int modifier; /* How much it changes by              */
};

struct room_direction_data {
    char* general_description; /* When look DIR.                  */

    char* keyword; /* for open/close                  */

    ubyte exit_width; /* 1-6, default should be 4 */
    int exit_info; /* Exit info    changed from sh_int                   */
    int key; /* Key's number (-1 for no key)    */
    int to_room; /* Where direction leeds (NOWHERE) */
};
struct room_track_data {
    sh_int char_number; // race number for players, virt_number for mobs
    byte data; // data/8 = time of the track, data & 8 = direction
    sh_int condition; // Effective condition of the tracks in hours
    // Weather makes tracks age faster/slower etc

    room_track_data()
    {
        char_number = 0;
        data = 0;
        condition = 0;
    }
};

struct room_bleed_data {
    sh_int char_number;
    byte data;
    sh_int condition;

    room_bleed_data()
    {
        char_number = 0;
        data = 0;
        condition = 0;
    }
};

struct prof_type {
    char letter;
    sh_int Class_points[5];
};

/* This structure is purely intended to be an easy way to transfer */
/* and return information about time (real or mudwise).            */
struct time_info_data {
    byte hours, day, month, moon;
    sh_int year;
};

/* These data contain information about a players time data */
struct time_data {
    time_t birth; /* This represents the characters age                */
    time_t logon; /* Time of the last logon (used to calculate played) */
    int played; /* This is the total accumulated time played in secs */
};

struct char_player_data {
    char* name; /* PC / NPC s name (kill ...  )		*/
    char* short_descr; /* for 'actions'                        	*/
    char* long_descr; /* for 'look'.. Only here for testing   	*/
    char* description; /* Extra descriptions                   	*/
    char* title; /* PC / NPC s title                     	*/
    char* death_cry; /* NPC - message to give in death        */
    char* death_cry2; /* NPC - message to other rooms         */
    int corpse_num; /* what object to use as a corpse        */
    int race; /* PC /NPC's race                       	*/
    byte sex; /* PC / NPC s sex                       	*/
    byte bodytype; /* PC / NPC hit locations                */
    byte prof; /* PC s or NPC s prof                 	*/
    int level; /* PC / NPC s level                     	*/
    byte language; /* the lang he's presently speaking      */
    int hometown; /* PC s Hometown (zone)                 	*/
    byte talks[MAX_TOUNGE]; /* PC s Tounges 0 for NPC           	*/
    struct time_data time; /* PC s AGE in days                 	*/
    int weight; /* PC / NPC s weight                    	*/
    int height; /* PC / NPC s height                    	*/
    int ranking; /* PC / NPC s ranking in fame war */
};

/* Used in CHAR_FILE_U;
 * Changes may require serialization updates in loading code if we want to persist new data
 */
struct char_ability_data {
    signed char str;
    signed char lea;
    signed char intel;
    signed char wil;
    signed char dex;
    signed char con;
    //   ubyte spare1;      /* spare one, if we want to add one */
    //   ubyte spare2;      /* spare one, if we want to add one */
    //   ubyte spare3;      /* spare one, if we want to add one */
    //   ubyte spare4;      /* spare one, if we want to add one */
    /* in case of pwipe, we want some more spares here... */
    //   sh_int spirit;
    int hit;
    sh_int mana;
    sh_int move;
};

/* Used in CHAR_FILE_U;
 * Changes may require serialization updates in loading code if we want to persist new data
 */
struct char_point_data {

    ubyte bodypart_hit[MAX_BODYPARTS]; /* hit points of individual body parts */
    int gold; /* Money carried                           */
    int exp; /* The experience of the player            */
    int spirit; /* well, the spirit */
    int mana_regen = 0; /* bonus mana regen from gear/spells/etc.  can be negative */
    int health_regen = 0; /* bonus health regen from spells etc. */
    int move_regen = 0; /* bonus move regen from spells etc. */

    sh_int OB; /* OB in normal tactics   */
    sh_int damage; /*  damage in normal tactics */
    sh_int ENE_regen; /* Rate at which energy for hitting is regened*/
    sh_int parry; /* parry in normal tactics */
    sh_int dodge; /* dodge. all in this file are tactics independent.*/
    sh_int encumb; /* how encumbered a player is, affects casting and
                          dex skills */
    sh_int willpower; /* strength in mental fights */
    sh_int spell_pen;
    sh_int spell_power;
    sh_int get_spell_power() const { return spell_power; };
    void set_spell_power(sh_int bonus) { spell_power += bonus; };
    sh_int get_spell_pen() const { return spell_pen; };
};

// A single PC alias node (do_alias in act_comm.cpp, Crash_alias_load in
// objsave.cpp). Moved above char_special_data (was originally declared after
// it) so owned_alias_list below can use the complete type. keyword/command
// shape and the char[20] width are unchanged by RAII T4 -- both the runtime
// list shape and the on-disk format (which already goes through
// objects_json::AliasData, a separate std::string-based DTO translated in
// Crash_alias_load()/Crash_collect_alias_data(), objsave.cpp) are preserved
// exactly; see ownership-map.md's alias entry / task-4-report.md for why.
struct alias_list {
    char keyword[20];
    char* command;
    struct alias_list* next;
};

// Releases the alias_list chain built by do_alias()/Crash_alias_load() (each
// node CREATE1()'d, each with a CREATE()'d .command string). Declared here
// (matching the db.h declaration exactly) rather than pulled in via #include
// "db.h" to avoid a circular header dependency -- db.h itself needs
// structs.h's char_data/alias_list types.
void free_alias_list(struct alias_list* list);

// RAII T4: owns the alias_list chain that used to be a bare
// `alias_list* alias` field on char_special_data. do_alias()'s add/remove/
// replace logic and Crash_alias_load()/Crash_collect_alias_data() all do
// direct node-level surgery (unlink-then-manually-free, CREATE1()-then-link)
// on the raw chain, so this type is deliberately NOT a general owning smart
// pointer: it behaves exactly like the raw `alias_list*` it replaces at
// every existing read/reseat call site --
//   - implicitly converts to/from `alias_list*` (so `list = ch->specials.alias;`,
//     `if (ch->specials.alias)`, `for (...; list; list = list->next)` etc. are
//     unchanged at every call site: act_comm.cpp's do_alias, act_wiz.cpp's
//     do_show "aliases" branch, interpre.cpp's replace_aliases(),
//     objsave.cpp's Crash_alias_load()/Crash_collect_alias_data());
//   - assigning a bare `alias_list*` (operator=(alias_list*)) is a PLAIN
//     RESEAT -- it does NOT free the previous chain -- because do_alias()'s
//     remove path (`GET_ALIAS(ch) = list->next;`) reseats the head past a
//     node it frees itself, by hand, on the very next lines; an
//     auto-freeing assignment here would double-free that node.
// What this type adds beyond the raw pointer: a destructor that calls
// free_alias_list(), so nothing has to free this chain by hand. Since RAII
// T6a, free_char() (db.cpp) runs an explicit `ch->~char_data();` before it
// releases the calloc storage, so this member's destructor runs as part of
// that teardown -- free_char() no longer resets it by hand (ownership-map.md
// §6). reset() remains available for explicit mid-life clearing.
//
// Copy/move: char_data instances are whole-struct-copied for NPCs/objects
// (`*mob = mob_proto[i]`, db.cpp) and mob_proto's alias is always null (NPCs
// never carry aliases -- see the `alias` field comment below), so this copy
// path is never exercised with a non-empty chain in practice. Implemented as
// a real deep clone anyway (rather than a shallow pointer copy) so
// char_special_data's implicitly-generated copy assignment -- which the
// whole-struct copy relies on -- stays well-defined instead of a
// double-free-on-destruction landmine if that invariant is ever violated.
class owned_alias_list {
public:
    owned_alias_list() noexcept = default;
    owned_alias_list(struct alias_list* head) noexcept
        : head_(head)
    {
    }

    owned_alias_list(const owned_alias_list& other)
        : head_(clone(other.head_))
    {
    }
    owned_alias_list(owned_alias_list&& other) noexcept
        : head_(other.head_)
    {
        other.head_ = nullptr;
    }

    owned_alias_list& operator=(const owned_alias_list& other)
    {
        if (this != &other) {
            free_alias_list(head_);
            head_ = clone(other.head_);
        }
        return *this;
    }
    owned_alias_list& operator=(owned_alias_list&& other) noexcept
    {
        if (this != &other) {
            free_alias_list(head_);
            head_ = other.head_;
            other.head_ = nullptr;
        }
        return *this;
    }
    // Plain reseat, no free -- see the class comment above.
    owned_alias_list& operator=(struct alias_list* head) noexcept
    {
        head_ = head;
        return *this;
    }

    ~owned_alias_list() { free_alias_list(head_); }

    operator struct alias_list*() const noexcept { return head_; }

    // Lets `GET_ALIAS(ch)->keyword`-style call sites (utils.h's GET_ALIAS
    // macro expands to a bare `ch->specials.alias`, so `->` is applied
    // directly to this wrapper, not to a raw pointer) keep working
    // unchanged: `->` resolution requires either a real pointer or an
    // operator-> overload, unlike assignment/comparison/`if`, which the
    // conversion operator above already covers.
    struct alias_list* operator->() const noexcept { return head_; }

    // Frees the chain and nulls the head. No longer called by free_char()
    // (RAII T6a: the char_data destructor -- now invoked explicitly before the
    // calloc/free teardown -- runs ~owned_alias_list() instead). Retained for
    // any caller that needs to clear this member mid-life; see class comment.
    void reset() noexcept
    {
        free_alias_list(head_);
        head_ = nullptr;
    }

private:
    // Deep-clones a chain node-by-node (CREATE1()'d nodes, CREATE()'d command
    // strings) -- mirrors do_alias()'s/Crash_alias_load()'s own node
    // construction exactly. Defined in db.cpp, next to free_alias_list().
    static struct alias_list* clone(struct alias_list* src);

    // Head of the owned chain; nullptr for mobs and for a freshly-cleared PC
    // (matches the field's pre-RAII-T4 "aliases, 0 for mobs" contract below).
    struct alias_list* head_ = nullptr;
};

/* Used in CHAR_FILE_U;
 * Changes may require serialization updates in loading code if we want to persist new data
 */
struct char_special2_data {
    long idnum; /* player's idnum			*/
    int load_room; /* Which room to place char in		*/
    int spells_to_learn; /* How many can you learn yet this level*/
    int alignment; /* +-1000 for alignments          */
    long act; /* act flag for NPC's; player flag for PC's */
    long pref; /* preference flags for PC's,
                                                racial agg/non-agg NPCs		*/
    int wimp_level; /* Below this # of hit points, flee! */
    byte freeze_level; /* Level of god who froze char, if any	*/
    int bad_pws; /* number of bad password attemps	*/
    /* also a call mask for special mobiles */
    int saving_throw; /* saving throw for new mobiles */
    int rawPerception; /* a raw value without any overrides applied. can be outside of the 0 to 100
                          range */
    int perception; /* perception changes between 0 and 100 */
    int conditions[3]; /* Drunk full etc.			*/

    int mini_level;
    int max_mini_level;
    int morale; /* flag to account for good/evil zones, and such */
    int owner;

    ubyte rerolls; /* Number of rerolls that has happened */
    int leg_encumb; /* how encumbered is char for movement/dodging? */
    int rp_flag; /* Special flag for PC, racial behaviour for - */
    /* NPC (bitvector) */
    int retiredon; /* time of retirement */
    int hide_flags; /* flag set for hide info */
    long will_teach;
    int tactics;
    int shooting;
    int casting;
    int two_handed;
};

struct affection_source {
    source_type type;
    int source_id;
    void* source;
};

/* Used in CHAR_FILE_U Change with the utmost care */
struct affected_type {
    sh_int type; /* The type of spell that caused this      */

    int duration; /* For how long its effects will last      */
    char time_phase; /* when exactly in the tick it was cast  */
    /*  is set in affect_to_char, room */

    sh_int modifier; /* This is added to apropriate ability     */
    sh_int location; /* Tells which ability to change(APPLY_XXX)*/
    long bitvector; /* Tells which bits to set (AFF_XXX)       */
    sh_int counter;

    struct affected_type* next;
};

// As-built deviation from the destination map (Task 2): MAX_PROFS is pulled
// forward from its mapped character.h location (PROF_* group, spec range
// 700-834) because char_prof_data below needs it and character.h does not
// exist until Task 4. Character.h's PROF_* carve-out must skip this line.
#define MAX_PROFS 4

struct char_prof_data {
    int prof_coof[MAX_PROFS + 1]; /* 100 would mean 100% in that class */
    int prof_level[MAX_PROFS + 1];
    long prof_exp[MAX_PROFS + 1];
    int specializations[5];

    long color_mask;
    char colors[16];
    color_slot_data color_settings[MAX_COLOR_FIELDS];
    int specialization;
};

/* ========== Structure used for storing skill data ============= */
struct player_skill_data {
    byte skills[MAX_SKILLS];
    byte knowledge[MAX_SKILLS];
};

/* ========== Structures used for tracking player damage ============= */
class damage_details {
public:
    damage_details()
        : total_damage(0)
        , instance_count(0)
        , largest_damage(0)
    {
    }

    virtual ~damage_details() { }

    void add_damage(int damage)
    {
        total_damage += damage;
        instance_count++;
        largest_damage = std::max(largest_damage, damage);
    }

    float get_average_damage() const
    {
        return static_cast<float>(total_damage) / static_cast<float>(instance_count);
    }

    int get_total_damage() const { return total_damage; }
    int get_instance_count() const { return instance_count; }
    int get_largest_damage() const { return largest_damage; }

    virtual void reset()
    {
        total_damage = 0;
        instance_count = 0;
        largest_damage = 0;
    }

private:
    int total_damage;
    int instance_count;
    int largest_damage;
};
class timed_damage_details : public damage_details {
public:
    timed_damage_details()
        : damage_details()
        , elapsed_combat_seconds(0)
    {
    }
    virtual ~timed_damage_details() { }

    float get_combat_time() const { return elapsed_combat_seconds; }
    float get_dps() const
    {
        return static_cast<float>(get_total_damage()) / std::max(elapsed_combat_seconds, 0.5f);
    }
    void tick(float delta) { elapsed_combat_seconds += delta; }

    virtual void reset()
    {
        damage_details::reset();
        elapsed_combat_seconds = 0;
    }

private:
    float elapsed_combat_seconds;
};

struct race_bodypart_data {
    const std::string_view parts[MAX_BODYPARTS];
    sh_int percent[MAX_BODYPARTS];
    sh_int bodyparts;
    ubyte armor_location[MAX_BODYPARTS];
};

/* ======================================================================== */
/* ====================== Weather structures and defines ================== */
/* ======================================================================== */
/* How much light is in the land ? */
#define SUN_DARK 0
#define SUN_RISE 1
#define SUN_LIGHT 2
#define SUN_SET 3

/* And how is the sky ? */
#define SKY_CLOUDLESS 0
#define SKY_CLOUDY 1
#define SKY_RAINING 2
#define SKY_LIGHTNING 3
#define SKY_SNOWING 4
#define SKY_BLIZZARD 5

/* And the moon's condition? eight phases total (see weather.cc) */
#define MOON_NEW 0
#define MOON_QUART1 1
#define MOON_HALF1 2
#define MOON_QUART2 3
#define MOON_FULL 4
#define MOON_QUART3 5
#define MOON_HALF2 6
#define MOON_QUART4 7

/* What season is it? */
#define SEASON_SPRING 0
#define SEASON_SUMMER 1
#define SEASON_AUTUMN 2
#define SEASON_WINTER 3

struct weather_data {
    int pressure; /* How is the pressure (Mb)? */
    int change; /* How fast and in what way does it change? */
    int sky[13]; /* How is the sky? cloudy, sunny, etc for each sector_type*/
    int sunlight; /* And how much sun. (day/night etc) */
    int moonlight;
    int moonphase;
    int temperature[13]; /* Temperature in each sector */
    int snow[13]; /* Is there snow on the ground? */
};

struct txt_block {
    char* text;
    struct txt_block* next;
};

struct txt_q {
    struct txt_block* head;
    struct txt_block* tail;
};

struct msg_type {
    char* attacker_msg; /* message to attacker */
    char* victim_msg; /* message to victim   */
    char* room_msg; /* message to room     */
};

struct message_type {
    struct msg_type die_msg; /* messages when death			*/
    struct msg_type miss_msg; /* messages when miss			*/
    struct msg_type hit_msg; /* messages when hit			*/
    struct msg_type sanctuary_msg; /* messages when hit on sanctuary	*/
    struct msg_type self_msg; /* messages when hit on god		*/
    struct message_type* next; /* to next messages of this kind.	*/
};

struct message_list {
    int a_type; /* Attack type				*/
    int number_of_attacks; /* How many attack messages to chose from. */
    struct message_type* msg; /* List of messages.			*/
};

struct prompt_type {
    const char* message;
    int value;
};

struct universal_list {
    int type;
    int number; /* abs_number for ch, whatever else for obj, */
    /* room number optional for rooms*/
    union {
        char_data* ch;
        room_data* room;
        obj_data* obj;
    } ptr;

    universal_list* next;
};

