#pragma once
// rots/core/room.h — room flag/direction/sector constants and the room_data
// entity (with its extension helper). Other entities are pointer-only
// (rots/core/fwd.h). NOWHERE was pulled forward to rots/core/types.h in Task 2
// (cross-cutting sentinel used by interpre.h); it is not redefined here.
#include "rots/core/fwd.h"
#include "rots/core/types.h"

/* ======================================================================= */
/* The following defs are for room_data  */

/* Bitvector For 'room_flags' */

#define DARK (1 << 0)
#define DEATH (1 << 1)
#define NO_MOB (1 << 2)
#define INDOORS (1 << 3)
#define NORIDE (1 << 4)
#define PERMAFFECT (1 << 5)
#define SHADOWY (1 << 6)
#define NO_MAGIC (1 << 7)
#define TUNNEL (1 << 8)
#define PRIVATE (1 << 9)
#define GODROOM (1 << 10)
#define BFS_MARK (1 << 11)
#define DRINK_WATER (1 << 12)
#define DRINK_POISON (1 << 13)
#define SECURITYROOM (1 << 14)
#define PEACEROOM (1 << 15)
#define NO_TELEPORT (1 << 16)
#define HIDE_VNUM (1 << 17)

#define BFS_ERROR -1
#define BFS_ALREADY_THERE -2
#define BFS_NO_PATH -3

/* For 'dir_option' */

#define NORTH 0
#define EAST 1
#define SOUTH 2
#define WEST 3
#define UP 4
#define DOWN 5

#define EX_ISDOOR 1
#define EX_CLOSED 2
#define EX_LOCKED 4
#define EX_NOFLEE 8
#define EX_RSLOCKED 16
#define EX_PICKPROOF 32
#define EX_DOORISHEAVY 64 /*changed*/
#define EX_NOBREAK 128 /*changed*/
#define EX_NO_LOOK 256 /*changed*/
#define EX_ISHIDDEN 512 /* for hidden exit*/
#define EX_ISBROKEN 1024
#define EX_NORIDE 2048
#define EX_NOBLINK 4096
#define EX_LEVER 8192
#define EX_NOWALK 16384

/* For 'Sector types' */

#define SECT_INSIDE 0
#define SECT_CITY 1
#define SECT_FIELD 2
#define SECT_FOREST 3
#define SECT_HILLS 4
#define SECT_MOUNTAIN 5
#define SECT_WATER_SWIM 6
#define SECT_WATER_NOSWIM 7
#define SECT_UNDERWATER 8
#define SECT_ROAD 9
#define SECT_CRACK 10
#define SECT_DENSE_FOREST 11
#define SECT_SWAMP 12

/* ========================= Structure for room ========================== */
#define EXTENSION_START 90000
#define EXTENSION_SIZE 50
#define EXTENSION_ROOM_HEAD 100000

const int NUM_OF_BLOOD_TRAILS = 3;
#define NUM_OF_TRACKS 10

struct room_data_extension {
    room_data* extension_world;
    //  int extension_length;  use EXTENSION_SIZE instead
    room_data_extension* extension_next;

    room_data_extension();
    ~room_data_extension();
};

struct room_data {
    static room_data* BASE_WORLD;
    static int BASE_LENGTH;
    static int TOTAL_LENGTH;
    static room_data_extension* BASE_EXTENSION;

    int number; /* Rooms number                       */
    int zone; /* Room zone (for resetting)          */
    byte level;
    int sector_type; /* sector type (move/hide)*/ /*changed*/
    char* name; /* Rooms name 'You are ...'           */
    char* description; /* Shown when entered                 */
    struct extra_descr_data* ex_description; /* for examine/look       */
    struct room_direction_data* dir_option[NUM_OF_DIRS]; /* Directions */
    struct room_track_data room_track[NUM_OF_TRACKS]; /* track info.. */
    long room_flags; /* DEATH,DARK ... etc                 */
    int alignment; /*changed*/
    byte light; /* Number of lightsources in room     */

    byte bfs_dir;
    room_data* bfs_next; /*instead ot bfs_queue structure, for hunting*/

    int (*funct)(struct char_data*, struct char_data*, int, char*, int, waiting_type*);
    /* special procedure, check SPECIAL in interpre.h      */
    struct obj_data* contents; /* List of items in room              */
    struct char_data* people; /* List of NPC / PC in room           */

    struct affected_type* affected; /* room affects */

    struct room_bleed_data bleed_track[NUM_OF_BLOOD_TRAILS];

    room_data();

    room_data& operator[](int i);

    int create_room(int zone); /* active constructor, returns real number  -
                                                           use this one to add rooms */
    void create_bulk(int amount); /* initial world constructor, the first alloc*/
    void delete_room(); /* active destructor - use it when removing rooms */
    void create_exit(int dir, int room, char connect = 1);
};
