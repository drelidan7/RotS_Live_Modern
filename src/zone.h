/* zone.h */

#ifndef ZONE_H
#define ZONE_H

#include <stdio.h>

struct owner_list {
    int owner; /* one of the owners of zone/object */
    struct owner_list* next; /* next owner */
};

/* zone definition structure. for the 'zone-table'   */
struct zone_data {
    char* name; /* name of this zone                  */
    char* description;
    char* map;
    int lifespan; /* how long between resets (minutes)  */
    int age; /* current age of this zone (minutes) */
    int top; /* upper limit for rooms in this zone */
    int x, y; /* NEW - zone coordinates, for the map*/
    char symbol; /* NEW - symbol for the zone on the map */
    int level;
    int white_power, dark_power, magi_power; /* power of races present */
    struct extra_descr_data* zone_short_description; /* summary of a zone */
    struct extra_descr_data* zone_description; /* zone description */
    struct extra_descr_data* zone_map; /* for zone map   */

    int min_level_look; /* minimum level required for zone map etc */
    struct owner_list* owners;

    int reset_mode; /* conditions for reset (see below)   */
    int number; /* virtual number of this zone	  */
    int cmdno; /* Number of zone commands */
    struct reset_com* cmd; /* command table for reset	          */
    /*
     *  Reset mode:
     *  0: Don't reset, and don't update age
     *  1: Reset if no PC's are located in zone
     *  2: Just reset
     */
};

void zone_update(void);
void load_zones(FILE*);
void renum_zone_table(void);
void renum_zone_one(int);
void reset_zone(int);

extern struct zone_data* zone_table;
extern int top_of_zone_table;

namespace rots::world {
// Zone-id -> pointer resolver (placement-seam Task 1's second world
// resolver hook; census ADJUDICATE-1, Disposition A). Registered as
// entity_hooks.h's zone resolver hook by db_world.cpp's
// register_world_resolver_hooks() (run_the_game(), before boot_db()).
// Bounds-checked: nullptr for znum outside [0, top_of_zone_table) -- see
// entity_hooks.h's resolver-contract comment (controller-adjudicated,
// placement-seam Task 1). Defined in zone_load.cpp, next to the
// zone_table/top_of_zone_table storage it reads. No Task 1 caller
// exercises this resolver yet (char_to_room/char_from_room's primitive,
// Task 3, are its first callers per the census) -- the exclusive boundary
// convention here is chosen for symmetry with room_by_id_impl, not
// verified against any pre-existing zone_table[]-indexing call site; a
// future task moving a function with its own top_of_zone_table check must
// re-verify this boundary matches that call site's historical operator.
zone_data* zone_by_id_impl(int znum);
}

#endif /* ZONE_H */
