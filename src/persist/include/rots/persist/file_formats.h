#pragma once
// rots/persist/file_formats.h — the on-disk/legacy player and rent file
// formats (spec §5: persistence types, not core entity types). LAYOUT-LOCKED:
// these structs define historical binary formats decoded by the migration
// converters; member order and types are ABI. legacy_*_fixture.bin goldens
// (32-bit only) and the layout probe guard them.
#include "rots/core/types.h"

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
