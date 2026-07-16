/* ************************************************************************
 *   File: structs.h                                     Part of CircleMUD *
 *  Usage: header file for central structures and contstants               *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#ifndef STRUCTS_H
#define STRUCTS_H

#include <array>
#include <string_view>
#include <sys/types.h>

#include <stdio.h>

#include "color.h" /* For MAX_COLOR_FIELDS */
#include "platdef.h" /* For sh_int, ush_int, byte, etc. */

#include "protocol.h"
#include <algorithm>
#include <assert.h>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "rots/core/fwd.h"
#include "rots/core/tables.h"
#include "rots/core/types.h"

#include "rots/core/object.h"

struct char_data;
struct obj_data;
struct room_data;

/* possible targets for commands */

#include "rots/core/room.h"

/* ======================================================================== */

/* The following defs and structures are related to char_data   */

/* For 'equipment' */

#define WEAR_LIGHT 0
#define WEAR_FINGER_R 1
#define WEAR_FINGER_L 2
#define WEAR_NECK_1 3
#define WEAR_NECK_2 4
#define WEAR_BODY 5
#define WEAR_HEAD 6
#define WEAR_LEGS 7
#define WEAR_FEET 8
#define WEAR_HANDS 9
#define WEAR_ARMS 10
#define WEAR_SHIELD 11
#define WEAR_ABOUT 12
#define WEAR_WAISTE 13
#define WEAR_WRIST_R 14
#define WEAR_WRIST_L 15
#define WIELD 16
#define HOLD 17
#define WEAR_BACK 18
#define WEAR_BELT_1 19
#define WEAR_BELT_2 20
#define WEAR_BELT_3 21
#define WIELD_TWOHANDED 22

/* Predifined  conditions */
#define DRUNK 0
#define FULL 1
#define THIRST 2

/* Bitvector for 'affected_by' */
#define AFF_DETECT_HIDDEN (1 << 0)
#define AFF_INFRARED (1 << 1)
#define AFF_SNEAK (1 << 2)
#define AFF_HIDE (1 << 3)
#define AFF_DETECT_MAGIC (1 << 4)
#define AFF_CHARM (1 << 5)
#define AFF_CURSE (1 << 6)
#define AFF_SANCTUARY (1 << 7)
#define AFF_TWOHANDED (1 << 8)
#define AFF_INVISIBLE (1 << 9)
#define AFF_MOONVISION (1 << 10)
#define AFF_POISON (1 << 11)
#define AFF_SHIELD (1 << 12)
#define AFF_BREATHE \
    (1 << 13) // was paralysis - now indicates the ch can breathe whatever the room conditions
#define AFF_UNUSED (1 << 14) // unused affect
#define AFF_CONFUSE (1 << 15)
#define AFF_SLEEP (1 << 16)
#define AFF_BASH (1 << 17)
#define AFF_FLYING (1 << 18)
#define AFF_DETECT_INVISIBLE (1 << 19)
#define AFF_FEAR (1 << 20)
#define AFF_BLIND (1 << 21)
#define AFF_FOLLOW (1 << 22)
#define AFF_SWIM (1 << 23)
#define AFF_HUNT (1 << 24)
#define AFF_EVASION (1 << 25)
#define AFF_WAITING (1 << 26)
#define AFF_WAITWHEEL (1 << 27)
#define AFF_CONCENTRATION (1 << 29)
#define AFF_HAZE (1 << 30)
#define AFF_HALLUCINATE (1 << 31)

/* modifiers to char's abilities */

#define APPLY_NONE 0
#define APPLY_STR 1
#define APPLY_DEX 2
#define APPLY_INT 3
#define APPLY_WILL 4
#define APPLY_CON 5
#define APPLY_LEA 6
#define APPLY_PROF 7
#define APPLY_LEVEL 8
#define APPLY_AGE 9
#define APPLY_CHAR_WEIGHT 10
#define APPLY_CHAR_HEIGHT 11
#define APPLY_MANA 12
#define APPLY_HIT 13
#define APPLY_MOVE 14
#define APPLY_GOLD 15
#define APPLY_EXP 16
#define APPLY_DODGE 17
#define APPLY_OB 18
#define APPLY_DAMROLL 19
#define APPLY_SAVING_SPELL 20
#define APPLY_WILLPOWER 21
#define APPLY_REGEN 22
#define APPLY_VISION 23
#define APPLY_SPEED 24
#define APPLY_PERCEPTION 25
#define APPLY_ARMOR 26
#define APPLY_SPELL 27
#define APPLY_BITVECTOR 28
#define APPLY_MANA_REGEN 29
#define APPLY_RESIST 30
#define APPLY_VULN 31
#define APPLY_MAUL 32
#define APPLY_BEND 33

#define APPLY_PK_MAGE 34
#define APPLY_PK_MYSTIC 35
#define APPLY_PK_RANGER 36
#define APPLY_PK_WARRIOR 37

#define APPLY_SPELL_PEN 38
#define APPLY_SPELL_POW 39

#define ROOMAFF_SPELL 1
#define ROOMAFF_EXIT 2

/* 'prof' for PC's */
#define PROF_GENERAL 0
#define PROF_MAGIC_USER 1
#define PROF_MAGE 1
#define PROF_CLERIC 2
#define PROF_THIEF 3
#define PROF_RANGER 3
#define PROF_WARRIOR 4

#define DEFAULT_PROFS 10

#define LANG_BASIC 0
#define LANG_ANIMAL 121
#define LANG_HUMAN 122
#define LANG_ORC 123

#define PLRSPEC_NONE 0
#define PLRSPEC_FIRE 1
#define PLRSPEC_COLD 2
#define PLRSPEC_REGN 3
#define PLRSPEC_PROT 4
#define PLRSPEC_PETS 5
#define PLRSPEC_STLH 6
#define PLRSPEC_WILD 7
#define PLRSPEC_TELE 8
#define PLRSPEC_ILLU 9
#define PLRSPEC_LGHT 10
#define PLRSPEC_GRDN 11
#define PLRSPEC_HFGT 12
#define PLRSPEC_LFGT 13
#define PLRSPEC_DFND 14
#define PLRSPEC_ARCH 15
#define PLRSPEC_DARK 16
#define PLRSPEC_ARCANE 17
#define PLRSPEC_WMSR 18
#define PLRSPEC_BTLEMS 19

/* Races for PCS */
const int constexpr RACE_GOD = 0;
const int constexpr RACE_HUMAN = 1;
const int constexpr RACE_DWARF = 2;
const int constexpr RACE_WOOD = 3;
const int constexpr RACE_HOBBIT = 4;
const int constexpr RACE_HIGH = 5;
const int constexpr RACE_BEORNING = 6;
const int constexpr RACE_URUK = 11;
const int constexpr RACE_ORC = 13;
const int constexpr RACE_MAGUS = 15;
const int constexpr RACE_OLOGHAI = 17;
const int constexpr RACE_HARADRIM = 18;

/* Races used for NPCs */
#define RACE_EASTERLING 14
#define RACE_HARAD 12
#define RACE_UNDEAD 16
#define RACE_TROLL 20

// A `#define localtime(x) localtime((time_t*)x)` macro used to live here,
// force-casting any pointer to time_t*. On Windows LLP64 (long = 4 bytes,
// time_t = 8) that cast made localtime(&some_long) read 4 bytes of adjacent
// stack, return nullptr for the resulting out-of-range time, and abort inside
// asctime() (Phase 3 Task 6). It is deliberately removed: every call site now
// passes a real time_t*, so any new mismatch fails at compile time on MSVC.

/* sex */
#define SEX_NEUTRAL 0
#define SEX_MALE 1
#define SEX_FEMALE 2

/* positions */
#define POSITION_DEAD 0
#define POSITION_SHAPING 1
#define POSITION_INCAP 2
#define POSITION_STUNNED 3
#define POSITION_SLEEPING 4
#define POSITION_RESTING 5
#define POSITION_SITTING 6
#define POSITION_FIGHTING 7
#define POSITION_STANDING 8

/* for mobile actions: specials.act */
#define MOB_VOID 0 /* for special uses only */
#define MOB_SPEC (1 << 0) /* spec-proc to be called if exist   */
#define MOB_SENTINEL (1 << 1) /* this mobile not to be moved       */
#define MOB_SCAVENGER (1 << 2) /* pick up stuff lying around        */
#define MOB_ISNPC (1 << 3) /* This bit is set for use with IS_NPC()*/
#define MOB_NOBASH (1 << 4) /* Set if a thief should NOT be killed  */
#define MOB_AGGRESSIVE (1 << 5) /* Set if automatic attack on NPC's     */
#define MOB_STAY_ZONE (1 << 6) /* MOB Must stay inside its own zone    */
#define MOB_WIMPY (1 << 7) /* MOB Will flee when injured, and if   */
/* aggressive only attack sleeping players*/
#define MOB_STAY_TYPE (1 << 8) /* MOB will move in rooms of the same type*/
#define MOB_MOUNT (1 << 9) /* MOB can be ridden	*/
#define MOB_CAN_SWIM \
    (1 << 10) /* MOB doesn't need a boat to swim. If GATEKEEPER, he wont' respond to knock/say */
#define MOB_MEMORY (1 << 11) /* remember attackers if struck first */
#define MOB_HELPER (1 << 12) /* attack chars attacking a PC in room */
#define MOB_AGGRESSIVE_EVIL (1 << 13)
#define MOB_AGGRESSIVE_NEUTRAL (1 << 14)
#define MOB_AGGRESSIVE_GOOD (1 << 15)
#define MOB_BODYGUARD (1 << 16) /* rescues his master, if any */
#define MOB_SHADOW (1 << 17) /* a ghost, a spirit, all that stuff */
#define MOB_SWITCHING (1 << 18) /* is stupid - won't switch opponents */
#define MOB_NORECALC (1 << 19)
#define MOB_FAST (1 << 20) /* will mob_act when somebody enters... */
#define MOB_PET (1 << 21) /* pet of a player */
#define MOB_HUNTER (1 << 22) /* memory + hunts his enemies         */
#define MOB_ORC_FRIEND (1 << 23) /* can be recruited by common orcs */
#define MOB_RACE_GUARD (1 << 24) /* will not allow players of different race into the room */
#define MOB_ASSISTANT (1 << 25) /* will assist his master if his master is in combat */
#define MOB_GUARDIAN (1 << 26) /* flag for guardian mobs */

/* For players : specials.act */
#define PLR_IS_NCHANGED (1 << 1)
#define PLR_FROZEN (1 << 2)
#define PLR_DONTSET (1 << 3) /* Dont EVER set (ISNPC bit) */
#define PLR_WRITING (1 << 4)
#define PLR_MAILING (1 << 5)
#define PLR_CRASH (1 << 6)
#define PLR_SITEOK (1 << 7)
#define PLR_NOSHOUT (1 << 8)
#define PLR_NOTITLE (1 << 9)
#define PLR_DELETED (1 << 10)
#define PLR_LOADROOM (1 << 11)
#define PLR_NOWIZLIST (1 << 12)
#define PLR_NODELETE (1 << 13)
#define PLR_INVSTART (1 << 14)
#define PLR_RETIRED (1 << 15)
#define PLR_SHAPING (1 << 16)
#define PLR_WR_FINISH (1 << 17)
#define PLR_ISSHADOW (1 << 18)
#define PLR_ISAFK (1 << 19)
#define PLR_INCOGNITO (1 << 20)
#define PLR_WAS_KITTED (1 << 21)

/* for players: preference bits */
#define PRF_BRIEF (1 << 0)
#define PRF_COMPACT (1 << 1)
#define PRF_NARRATE (1 << 2)
#define PRF_NOTELL (1 << 3)
#define PRF_MENTAL (1 << 4)
#define PRF_SWIM (1 << 5)
#define PRF_NOTHING2 (1 << 6)
#define PRF_PROMPT (1 << 7)
#define PRF_DISPTEXT (1 << 8)
#define PRF_NOHASSLE (1 << 9)
#define PRF_SUMMONABLE (1 << 11)
#define PRF_ECHO (1 << 12)
#define PRF_HOLYLIGHT (1 << 13)
#define PRF_COLOR (1 << 14)
#define PRF_SING (1 << 15)
#define PRF_WIZ (1 << 16)
#define PRF_LOG1 (1 << 17)
#define PRF_LOG2 (1 << 18)
#define PRF_LOG3 (1 << 19)
#define PRF_CHAT (1 << 20)
#define PRF_ROOMFLAGS (1 << 22)
#define PRF_SPAM (1 << 23)
#define PRF_MSDP (1 << 24) // TODO: Replace this is PRF_MSDP
#define PRF_WRAP (1 << 25)
#define PRF_LATIN1 (1 << 26)
#define PRF_SPINNER (1 << 27)
#define PRF_INV_SORT1 (1 << 28)
#define PRF_INV_SORT2 (1 << 29)
#define PRF_ADVANCED_VIEW (1 << 30)
#define PRF_ADVANCED_PROMPT (1 << 31)

struct memory_rec {
    struct char_data* enemy;
    int enemy_number;
    long id;
    struct memory_rec* next;
    struct memory_rec* next_on_mob;
};

/* char_special_data's fields are fields which are needed while the game
   is running, but are not stored in the playerfile.  In other words,
   a struct of type char_special_data appears in char_data but NOT
   char_file_u.  If you want to add a piece of data which is automatically
   saved and loaded with the playerfile, add it to char_special2_data.
*/

// Incomplete-type forward decl so char_special_data can hold a special_list*
// (the mudlle script target list). The full definition lives in mudlle.h
// (which #includes this header), so only a pointer is possible here -- RAII
// T5a decoupled this storage out of the poofOut char* type-pun.
struct special_list;

struct char_special_data {
    struct char_data* fighting; /* Opponent                             */
    struct char_data* hunting; /* Hunting person..                     */

    long affected_by; /* Bitvector for spells/skills affected by */
    sh_int resistance; /* bitvector for resistances */
    sh_int vulnerability; /* bitvector for vulnerabilities */

    sh_int position; /* Standing or ...                         */
    sh_int default_pos; /* Default position for NPC                */

    int carry_weight; /* Carried weight                          */
    sh_int worn_weight; /* Worn weight :)                          */
    sh_int encumb_weight; /* weight for skill encumberance            */

    byte carry_items; /* Number of items carried                 */
    int timer; /* Timer for update                        */
    int was_in_room; /* storage of location for linkdead people */

    int ENERGY; /* current energy */
    sh_int current_parry; /*parry currently affected by 'parry split' */

    signed char last_direction; /* The last direction the monster went     */
    int attack_type; /* The Attack Type Bitvector for NPC's     */

    owned_alias_list alias; /* aliases, 0 for mobs -- RAII T4, was `struct alias_list *` */

    // RAII T5b: PC/god arrival & departure strings, now owning std::string
    // (were str_dup'd char*). Empty for mobs. The special-mob script
    // buffers that used to alias these fields moved to special_stack /
    // special_list_area (above, RAII T5a). Destroyed by ~char_data(), which
    // free_char() now invokes explicitly before releasing the calloc storage
    // (RAII T6a; ownership-map section 6 dtor-order rule).
    std::string poofIn; /* Description on arrival of a god. */
    std::string poofOut; /* Description upon a god's exit.  */
    int invis_level; /* level of invisibility		       */
    /* also a stack pointer for special mobs   */

    // Special-mob (mudlle script interpreter) storage, decoupled from the
    // poofIn/poofOut/union1/union2 type-pun by RAII T5a. Each is heap-
    // allocated in read_mobile() for a special mob (store_prog_number != 0)
    // and null for PCs and ordinary mobs; all four are unconditionally
    // RELEASE()'d in free_char() (null-safe). Before T5a these lived
    // reinterpret_cast'd through poofIn (long*), poofOut (special_list*),
    // union1.prog_number and union2.prog_point -- an aliasing hazard that
    // blocked converting the PC poof strings to std::string and left
    // prog_number/prog_point leaked (no free path could tell them apart from
    // a PC's reply_ptr/reply_number). The SPECIAL_STACK/SPECIAL_LIST_AREA/
    // PROG_NUMBER/PROG_POINT macros (mudlle.h) now read these fields directly.
    long* special_stack; /* SPECIAL_STACK: SPECIAL_STACKLEN-long value stack */
    struct special_list* special_list_area; /* SPECIAL_LIST_AREA: target list */
    int* special_prog_number; /* PROG_NUMBER: mobile-program call list */
    int* special_prog_point; /* PROG_POINT: per-call cmd-line cursor list */

    // Single-member union retained (rather than a plain field) so PC reply-
    // target call sites keep the `union1.reply_ptr` spelling unchanged; the
    // special-mob prog_number member that shared this storage moved to
    // special_prog_number above (RAII T5a).
    union {
        struct char_data* reply_ptr;
    } union1;

    int store_prog_number; /* in database, stores prog_numbers for mobiles,*/
    /* in the game, can be used for PCs as for   */
    /* mobiles with SPECIAL flag set */
    struct info_script* script_info; /* Pointer to char_script (protos.h) */
    /* 0 if no script */
    int script_number; /* vnum of script */

    // Single-member union retained for the same reason as union1 above; the
    // special-mob prog_point member moved to special_prog_point (RAII T5a).
    union {
        int reply_number;
    } union2;

    struct memory_rec* memory; /* List of attackers to remember */
    sh_int current_bodypart; /* The number of current bodypart */

    ubyte tactics; /* combat tactics of a person */
    /* also, program pointer in call list for npc */

    int prompt_number; /* which prompt to use if PRF_DISPTEXT is set */
    /* for players, and difficulty_coof for mobs */

    int prompt_value; /* value to be inserted into text prompt */
    /* or zon_table index for NPC */
    int homezone; /* zone where it was loaded */
    int load_line; /* the line in zone that loaded the mob */

    byte trophy_line; /* for mobs, 0-4 in each zone */

    int board_point[MAX_MAXBOARD]; /* pointers on the current messages */

    int null_speed; /*UPDATE* For temporary use, should be removed later*/
    int str_speed; /*UPDATE* For temporary use, should be removed later*/

    int butcher_item; /* virtual item to load when buther corpse, 0 if none*/

    int was_ambushed;
    int attacked_level; /* the highest level of the attackers, NPC only */
    byte hide_value; /* how good you are hidden, if at all */

    int mental_delay;
    int trap_number; /* used to determine #s in trap */
    char* recite_lines; /* For reciters, how far read? */
    ubyte shooting; /* shooting speed for archery spec*/
    ubyte casting; /*  casting speed for spell casters*/
};

/* defines for hide_flags */
#define HIDING_WELL 0x01
#define HIDING_SNUCK_IN 0x04

struct follow_type {
    int fol_number; /* abs_number of the follower, for safety */
    struct char_data* follower;
    struct follow_type* next;
};

struct mount_data_type {
    struct char_data* mount;
    int mount_number;

    struct char_data* rider;
    int rider_number;

    struct char_data* next_rider;
    int next_rider_number;
};

struct specialization_info {
    virtual ~specialization_info() { }

    virtual std::string to_string(char_data& character) const = 0;
};

struct elemental_spec_data : public specialization_info {
    elemental_spec_data()
        : exposed_target(NULL)
        , spell_id(0)
    {
    }

    void reset()
    {
        exposed_target = NULL;
        spell_id = 0;
    }

    /* Target (if any) that the character has 'exposed' to their element. */
    char_data* exposed_target;
    int spell_id;

    virtual std::string to_string(char_data& character) const;

protected:
    void report_exposed_data(std::string& message_writer) const;
};

struct cold_spec_data : public elemental_spec_data {
public:
    cold_spec_data()
        : total_chill_ray_count(0)
        , successful_chill_ray_count(0)
        , failed_chill_ray_count(0)
        , total_chill_ray_damage(0)
        , total_cone_of_cold_count(0)
        , successful_cone_of_cold_count(0)
        , failed_cone_of_cold_count(0)
        , total_cone_of_cold_damage(0)
        , total_energy_sapped(0)
    {
    }

    void on_chill_ray_success(int damage);
    void on_chill_ray_fail(int damage);

    void on_cone_of_cold_success(int damage);
    void on_cone_of_cold_failed(int damage);

    void on_chill_applied(int chill_amount);

    int get_chill_ray_count() const { return total_chill_ray_count; }
    int get_successful_chills() const { return successful_chill_ray_count; }
    int get_saved_chills() const { return failed_chill_ray_count; }
    double get_chill_success_percentage() const
    {
        return double(successful_chill_ray_count) / double(total_chill_ray_count);
    }

    int get_cone_count() const { return total_cone_of_cold_count; }
    int get_successful_cones() const { return successful_cone_of_cold_count; }
    int get_saved_cones() const { return failed_cone_of_cold_count; }
    double get_cone_success_percentage() const
    {
        return double(successful_cone_of_cold_count) / double(total_cone_of_cold_count);
    }

    long get_total_energy_sapped() const { return total_energy_sapped; }

    virtual std::string to_string(char_data& character) const;

private:
    /* Total number of chill rays cast. */
    int total_chill_ray_count;

    /* Successful chill ray casts. */
    int successful_chill_ray_count;

    /* Failed chill ray casts. */
    int failed_chill_ray_count;

    /* Total damage dealt by chill ray. */
    int total_chill_ray_damage;

    /* Total number of cone of cold casts. */
    int total_cone_of_cold_count;

    /* Successful cone of cold casts. */
    int successful_cone_of_cold_count;

    /* Failed cone of cold casts. */
    int failed_cone_of_cold_count;

    /* Total damage from cone of cold. */
    int total_cone_of_cold_damage;

    /* Total energy sapped from the target. */
    long total_energy_sapped;
};

struct fire_spec_data : public elemental_spec_data {
    virtual std::string to_string(char_data& character) const;
};

struct lightning_spec_data : public elemental_spec_data {
    virtual std::string to_string(char_data& character) const;
};

struct darkness_spec_data : public elemental_spec_data {
    virtual std::string to_string(char_data& character) const;
};

struct arcane_spec_data : public elemental_spec_data {
    virtual std::string to_string(char_data& character) const;
};

struct defender_data : public specialization_info {
    defender_data()
        : last_block_time(0)
        , next_block_available(0)
        , is_blocking(false)
        , blocked_damage(0) {};

    /* The last time the player used the "block" command. */
    time_t last_block_time;

    /* Time the player can use the "block" command again. */
    time_t next_block_available;

    /* True if the player is currently "blocking". */
    bool is_blocking;

    void add_blocked_damage(int damage) { blocked_damage += damage; }
    unsigned int get_total_blocked_damage() { return blocked_damage; }

    virtual std::string to_string(char_data& character) const;

private:
    /* Total damage blocked by defender spec this session. */
    unsigned int blocked_damage;
};

struct battle_mage_spec_data : public specialization_info {
    virtual std::string to_string(char_data& character) const;
};

struct wild_fighting_data : public specialization_info {
    wild_fighting_data()
        : is_frenzying(false)
        , rush_forward_damage(0) {};

    /* True if the character is currently in a frenzy. */
    bool is_frenzying;

    /* Gets the energy regeneration multiplier for the character. */
    double get_eneregy_regen_multiplier(int current_hp, int max_hp);

    /* Gets the damage multiplier for the character. */
    double get_damage_multiplier(int current_hp, int max_hp);

    void add_rush_damage(int damage) { rush_forward_damage += damage; }
    unsigned int get_total_rush_damage() { return rush_forward_damage; }

    virtual std::string to_string(char_data& character) const;

private:
    /* Total extra damage added by rushing forward wildly. */
    unsigned int rush_forward_damage;
};

struct light_fighting_data : public specialization_info {
    light_fighting_data()
        : light_fighting_extra_hits(0) {};

    /* Weapon that is currently being used in the off-hand. */
    obj_data* off_hand_weapon;

    /* Current energy of the off-hand.  */
    sh_int off_hand_energy;

    /* Rate at which energy for hitting is regenerated for the off-hand. */
    sh_int off_hand_energy_regen;

    void add_light_fighting_proc() { ++light_fighting_extra_hits; }
    unsigned int get_total_light_fighting_procs() { return light_fighting_extra_hits; }

    virtual std::string to_string(char_data& character) const;

private:
    /* Total extra damage added by light fighting. */
    unsigned int light_fighting_extra_hits;
};

struct heavy_fighting_data : public specialization_info {
    heavy_fighting_data()
        : heavy_fighting_damage(0) {};

    void add_heavy_fighting_damage(int damage) { heavy_fighting_damage += damage; }
    unsigned int get_total_heavy_fighting_damage() { return heavy_fighting_damage; }

    virtual std::string to_string(char_data& character) const;

private:
    /* Total extra damage added by heavy fighting. */
    unsigned int heavy_fighting_damage;
};

/* ========== Structure for storing specialization information ============= */
struct specialization_data {
    specialization_data()
        : current_spec_info(NULL)
        , current_spec(game_types::PS_None) {};

    ~specialization_data() { reset(); }

    void reset();

    void set(char_data& character);

    specialization_info* current_spec_info;

    bool is_mage_spec() const
    {
        if (current_spec == game_types::PS_None)
            return false;

        return current_spec == game_types::PS_Darkness || current_spec == game_types::PS_Arcane || current_spec == game_types::PS_Fire || current_spec == game_types::PS_Cold || current_spec == game_types::PS_Lightning || current_spec == game_types::PS_BattleMage;
    }

    elemental_spec_data* get_mage_spec() const
    {
        if (is_mage_spec()) {
            return static_cast<elemental_spec_data*>(current_spec_info);
        }

        return NULL;
    }

    game_types::player_specs get_current_spec() const { return current_spec; }
    std::string to_string(char_data& character) const;

private:
    game_types::player_specs current_spec;
};

class player_damage_details {
public:
    player_damage_details()
        : damage_map()
        , elapsed_combat_seconds(0) {};

    void add_damage(int skill_id, int damage) { damage_map[skill_id].add_damage(damage); }
    void tick(float delta) { elapsed_combat_seconds += delta; }
    void reset()
    {
        damage_map.clear();
        elapsed_combat_seconds = 0;
    }

    // Raw accumulated combat time, as fed by tick(); exposed (alongside the existing
    // damage-map getters) so callers/tests can observe elapsed time directly instead
    // of only through the formatted get_damage_report() text.
    float get_elapsed_combat_seconds() const { return elapsed_combat_seconds; }

    std::string get_damage_report(const char_data* character) const;

private:
    std::map<int, damage_details> damage_map;
    float elapsed_combat_seconds;
};

class group_damaga_data {
public:
    group_damaga_data()
        : damage_map() {};

    void add_damage(struct char_data* character, int damage)
    {
        damage_map[character].add_damage(damage);
    }
    void track_time(struct char_data* character, float elapsed_seconds)
    {
        damage_map[character].tick(elapsed_seconds);
    }
    void remove(struct char_data* character) { damage_map.erase(character); }
    void reset() { damage_map.clear(); }

    std::string get_damage_report() const;

private:
    std::map<struct char_data*, timed_damage_details> damage_map;
};

/* ================== Structure for grouping ===================== */
class group_data {
public:
    /* Groups cannot exist without a leader. */
    group_data(struct char_data* in_leader)
        : leader(in_leader)
        , pc_count(0)
    {
        add_member(in_leader);
    };

    void add_member(struct char_data* member);
    bool remove_member(struct char_data* member);

    void track_damage(struct char_data* character, int damage)
    {
        damage_report.add_damage(character, damage);
    }
    void track_combat_time(struct char_data* character, float elapsed_seconds);
    void reset_damage();
    std::string get_damage_report() const { return damage_report.get_damage_report(); }

    struct char_data* get_leader() const { return leader; }
    bool is_member(struct char_data* character) const;
    bool is_leader(struct char_data* character) const { return character == leader; }
    int get_pc_count() const { return pc_count; }

    void get_pcs_in_room(char_vector& pc_vec, int room_number) const;
    size_t size() const { return members.size(); }
    char_iter begin() { return members.begin(); }
    char_iter end() { return members.end(); }
    const_char_iter begin() const { return members.begin(); }
    const_char_iter end() const { return members.end(); }
    struct char_data* at(size_t index) { return members.at(index); }
    bool contains(const char_data* character)
    {
        return std::find(members.begin(), members.end(), character) != members.end();
    }

private:
    struct char_data* leader;
    char_vector members;

    group_damaga_data damage_report;
    int pc_count;
};

/* ================== Structure for player/non-player ===================== */
struct char_data {
public:
    int get_cur_str() const { return tmpabilities.str; }
    int get_cur_int() const { return tmpabilities.intel; }
    int get_cur_wil() const { return tmpabilities.wil; }
    int get_cur_dex() const { return tmpabilities.dex; }
    int get_cur_con() const { return tmpabilities.con; }
    int get_cur_lea() const { return tmpabilities.lea; }

    int get_max_str() const { return abilities.str; }
    int get_max_int() const { return abilities.intel; }
    int get_max_wil() const { return abilities.wil; }
    int get_max_dex() const { return abilities.dex; }
    int get_max_con() const { return abilities.con; }
    int get_max_lea() const { return abilities.lea; }

    int get_level() const { return player.level; }
    bool is_legend() const { return player.level >= LEVEL_MAX; }
    // Returns the level of the player, or the 'maximum' level, whichever is lower.
    int get_capped_level() const { return std::min(get_level(), LEVEL_MAX); }

    /* Returns the leader of the character's group, if the character is in a group. */
    char_data* get_group_leader() const { return group ? group->get_leader() : NULL; }

    int get_spent_practice_count() const;
    int get_max_practice_count() const;
    void update_available_practice_sessions();

    // Resets all known skills and practice sessions for a character.
    void reset_skills();

    // returns true if the affected pointer is valid
    bool is_affected() const;

    int get_dodge() const { return points.dodge; }

public:
    int abs_number; /* bit number in the control array */
    int player_index; /* Index in player table */
    int nr; /* monster nr (pos in file)      */
    int in_room; /* Location                      */

    struct char_player_data player; /* Normal data                   */
    struct char_ability_data abilities; /* Max. Abilities                 */
    struct char_ability_data tmpabilities; /* Current abilities    */
    struct char_ability_data constabilities; /* Rolled abilities */
    struct char_point_data points; /* Points                        */
    struct char_special_data specials; /* Special playing constants      */
    struct char_special2_data specials2; /* Additional special constants  */
    struct char_prof_data* profs; /* prof cooficients */
    specialization_data extra_specialization_data; /* extra data used by some specializations */
    player_damage_details damage_details; /* structure for storing damage data */
    // Pracs spent per skill. PC-only: clear_char() sizes this to MAX_SKILLS
    // (mode != MOB_ISNPC); NPCs (and mob_proto, which copies into every NPC
    // instance via `*mob = mob_proto[i]`) always leave it empty. Emptiness is
    // the load-bearing "does this character have a skill array at all?" test
    // GET_SKILL/SET_SKILL (utils.h) and recalc_skills()/handle_pracs()
    // (spec_pro.cpp) branch on -- it stands in for the old null-pointer check
    // (RAII T3; was `byte*`, CREATE()/RELEASE()'d by hand).
    std::vector<byte> skills;
    // Computed knowledge per skill (derived from `skills` at logon by
    // recalc_skills()). Same PC-only/empty-means-absent contract as `skills`
    // above (RAII T3; was `byte*`).
    std::vector<byte> knowledge;
    struct affected_type* affected; /* affected by what spells       */
    struct obj_data* equipment[MAX_WEAR]; /* Equipment array               */

    struct obj_data* carrying; /* Head of list                  */
    struct descriptor_data* desc; /* NULL for mobiles              */

    struct char_data* next_in_room; /* For room->people - list         */
    struct char_data* next; /* For either monster or ppl-list  */
    struct char_data* next_fighting; /* For fighting list               */
    struct char_data* next_fast_update; /* For fast-update list            */

    struct follow_type* followers; /* List of chars followers       */
    struct char_data* master; /* Who is char following?        */
    int master_number;

    struct mount_data_type mount_data;
    group_data* group; /* The group that the character belongs to.  Can be null. */

    void* temp; /* pointer to any special structures if need be   */
    struct waiting_type delay;
    struct char_data* next_die; /* next to die in the death_waiting_list :(  */
    int classpoints; /* Only used for character creation in interpre.cc new_player_select.  Move
                        variable elsewhere? */
    int interrupt_count = 0; /* Meant to store times interupted so that npc mages know to stop casting in battle */
    int interrupt_time = 0; /* Meant to be a countdown timer to remove 1 from interrupt_count */

    bool spec_busy;
};

/* ***********************************************************************
   File element for player file.  Changes here require changes in
   save/load code in the database.
*************************************************************************/
struct char_file_u {
    byte sex;
    byte prof;
    byte race;
    byte bodytype;
    byte level;
    byte language;
    int player_index; /* Index in player table */
    time_t birth; /* Time of birth of character     */
    int played; /* Number of secs played in total */

    int weight;
    int height;

    char title[80];
    sh_int hometown;
    char description[512];
    byte talks[MAX_TOUNGE];

    struct char_ability_data tmpabilities;
    struct char_ability_data constabilities;

    struct char_point_data points;

    byte skills[MAX_SKILLS];

    struct affected_type affected[MAX_AFFECT];

    struct char_special2_data specials2;
    struct char_prof_data profs;

    time_t last_logon; /* Time (in secs) of last logon */
    char host[HOST_LEN + 1]; /* host of last logon */

    /* char data */
    char name[MAX_NAME_LENGTH + 1];
    char pwd[MAX_PWD_LENGTH + 1];
};

/* *************************************************************
 * The follower structure is used in objsave for saving followers
 * Changing it will result in the loss of followers and eq      *
 ***************************************************************/
struct follower_file_elem {
    int fol_vnum;
    int mount_vnum;
    int wimpy;
    int exp;
    int flag_config;
    int spare1;
    int spare2;
};

/* ************************************************************************
 *  file element for object file. BEWARE: Changing it will ruin rent files *
 ************************************************************************ */

constexpr const sh_int SENTINEL_ITEM_ID_VALUE = -17;
constexpr const sh_int DEPRECATED_ID_VALUE = -255;
struct obj_file_elem {

    sh_int item_number_deprecated; // this used to be the ID number, but it wasn't big enough.

    sh_int value[5];
    int extra_flags;
    int weight;
    int timer;
    long bitvector;
    struct obj_affected_type affected[MAX_OBJ_AFFECT];
    sh_int wear_pos;
    int loaded_by; // idnum of immortal who loaded object.  0 if loaded by a zone command etc.
    int item_number; // this used to be spare2, but we needed more item numbers.
};

#define RENT_UNDEF 0
#define RENT_CRASH 1
#define RENT_RENTED 2
#define RENT_CAMP 3
#define RENT_FORCED 4
#define RENT_TIMEDOUT 5
#define RENT_QUIT 6

/* header block for rent files */
struct rent_info {
    int time;
    int rentcode;
    int net_cost_per_hour;
    int gold;
    int nitems;
    sh_int spare0;
    sh_int spare1;
    sh_int spare2;
    int spare3;
    int spare4;
    int spare5;
    int spare6;
    int spare7;
};

/* ***********************************************************
 *  The following structures are related to descriptor_data   *
 *********************************************************** */

/* modes of connectedness */

#define CON_PLYNG 0
#define CON_NME 1
#define CON_NMECNF 2
#define CON_PWDNRM 3
#define CON_PWDGET 4
#define CON_PWDCNF 5
#define CON_QSEX 6
#define CON_RMOTD 7
#define CON_SLCT 8
#define CON_EXDSCR 9
#define CON_QPROF 10
#define CON_LDEAD 11
#define CON_PWDNQO 12
#define CON_PWDNEW 13
#define CON_PWDNCNF 14
#define CON_CLOSE 15
#define CON_DELCNF1 16
#define CON_DELCNF2 17
#define CON_QRACE 18
#define CON_QOWN 19
#define CON_QOWN2 20
#define CON_CREATE 21
#define CON_CREATE2 22
#define CON_LINKLS 23
#define CON_LATIN 24
#define CON_COLOR 25
#define CON_ACCTPWD 26
#define CON_ACCTSLCT 27
#define CON_ACCTLINKPWD 28
#define CON_ACCTNEWCNF 29
#define CON_ACCTNEWPWD 30
#define CON_ACCTNEWPWDCNF 31
#define CON_ACCTMENU 32
#define CON_ACCTLINKNAME 33
#define CON_ACCTRESETOLD 34
#define CON_ACCTRESETNEW 35
#define CON_ACCTRESETCNF 36
#define CON_ACCTNEWCHAR 37
#define CON_ACCTLEGPWD 38
#define CON_ACCTVERIFY 39
#define CON_ACCTDELCNF1 40

/* modes for flags */
#define DFLAG_IS_SPAMMING 1

struct snoop_data {
    struct char_data* snooping; /* Who is this char snooping		*/
    struct char_data* snoop_by; /* And who is snooping this char	*/
};

#define BLOCK_STR_LEN                     \
    512 /* how much to allocate initially \
           for string_add messages */

struct descriptor_data {
    SocketType descriptor; /* file descriptor for socket	*/
    char* name; /* ptr to name for mail system		*/
    char host[50]; /* hostname				*/
    uint32_t proxy_peer_address; /* pending proxy peer address */
    byte proxy_peer_bytes_read; /* bytes read for pending proxy header */
    bool waiting_for_proxy_header; /* descriptor is waiting for proxy header completion */
    char pwd[MAX_PWD_LENGTH + 1]; /* password			*/
    char account_name[MAX_INPUT_LENGTH]; /* authenticated account login */
    char account_email[MAX_INPUT_LENGTH]; /* authenticated account email */
    char account_password[MAX_ACCOUNT_PASSWORD_LENGTH + 1]; /* transient account password */
    char account_character_name[MAX_INPUT_LENGTH]; /* pending account character action */
    int bad_pws; /* number of bad pw attemps this login	*/
    int pos; /* position in player-file		*/
    int connected; /* mode of 'connectedness'		*/
    //   int	wait;			/* wait for how many loops    	*/
    int desc_num; /* unique num assigned to desc		*/
    time_t login_time; /* when the person connected; time_t (not long) so &login_time is a valid time_t* for localtime() on Windows LLP64 -- Phase 3 Task 6 */
    char* showstr_head; /* for paging through texts		*/
    char* showstr_point; /*		-			*/
    char** str; /* for the modify-str system		*/
    unsigned int max_str; /*  allocated length of *str		*/
    unsigned int len_str; /* present length of *str               */
    unsigned int cur_str; /* current pointer position in *str     */
    int prompt_mode; /* control of prompt-printing		*/
    char buf[MAX_STRING_LENGTH]; /* buffer for raw input			*/
    char last_input[MAX_INPUT_LENGTH]; /* the last input			*/
    char small_outbuf[SMALL_BUFSIZE]; /* standard output bufer		*/
    char* output; /* ptr to the current output buffer	*/
    int bufptr; /* ptr to end of current output		*/
    int bufspace; /* space left in the output buffer	*/
    unsigned char dflags; /* flags for this descriptor            */
    time_t last_input_time; /* time(0) of last_input               */
    struct txt_block* large_outbuf; /* ptr to large buffer, if we need it */
    struct txt_q input; /* q of unprocessed input		*/
    struct char_data* character; /* linked to char			*/
    struct char_data* original; /* original char if switched		*/
    struct snoop_data snoop; /* to snoop people			*/
    struct descriptor_data* next; /* link to next descriptor		*/
    protocol_t* pProtocol;
};

class group_roll {
public:
    char* character_name;
    int roll;

    group_roll(char_data* character_name, int roll)
        : character_name(character_name->player.name)
        , roll(roll)
    {
    }
};

#endif /* STRUCTS_H */
