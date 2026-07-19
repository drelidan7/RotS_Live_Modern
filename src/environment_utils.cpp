#include "environment_utils.h"
#include "utils.h"

#include "handler.h" // for room_by_id_total()/can_swim() -- CAN_GO()/can_breathe() (placement-seam Task 5)

#include "rots/core/character.h"
#include "rots/core/room.h"
#include "rots/core/types.h"

namespace utils {
//============================================================================
bool is_dark(const room_data& room, const weather_data& weather)
{
    if (room.light)
        return false;

    if (utils::is_set(room.room_flags, (long)DARK))
        return true;

    return weather.sunlight == SUN_DARK && room.sector_type != SECT_INSIDE && room.sector_type != SECT_CITY;
}

//============================================================================
bool is_light(const room_data& room, const weather_data& weather)
{
    return is_dark(room, weather);
}

//============================================================================
bool is_sunlit_exit(const weather_data& weather, const room_data& current_room, const room_data& adjacent_room, int door_index)
{
    using namespace utils;
    if (weather.sunlight != SUN_LIGHT)
        return false;

    bool is_next_room_dark = !is_room_sunlit(weather, adjacent_room);
    if (is_next_room_dark)
        return false;

    int exit_info = current_room.dir_option[door_index]->exit_info;
    bool is_sunlit_exit = is_set(exit_info, EX_ISBROKEN) || !is_set(exit_info, EX_CLOSED);

    return is_sunlit_exit;
}

//============================================================================
bool is_shadowy_exit(const room_data& current_room, const room_data& adjacent_room, int door_index)
{
    using namespace utils;

    int exit_info = current_room.dir_option[door_index]->exit_info;
    return is_set(adjacent_room.room_flags, (long)SHADOWY) && !is_set(exit_info, EX_CLOSED);
}

//============================================================================
bool is_room_sunlit(const weather_data& weather, const room_data& room)
{
    using namespace utils;
    if (weather.sunlight != SUN_LIGHT)
        return false;

    int flags = room.room_flags;
    bool is_room_dark = is_set(flags, DARK) || is_set(flags, SHADOWY) || is_set(flags, INDOORS);

    return !is_room_dark;
}

//============================================================================
bool is_room_outside(const room_data& room)
{
    return !utils::is_set(room.room_flags, (long)INDOORS);
}

//============================================================================
bool is_water_room(const room_data& room)
{
    int sector = room.sector_type;

    return sector == SECT_WATER_SWIM || SECT_WATER_NOSWIM || SECT_UNDERWATER;
}
}

//============================================================================
// default_exit_width[]/get_exit_width()/CAN_GO()/can_breathe() relocated
// verbatim from utility.cpp (placement-seam Task 5; census verdict
// MOVE-OTHER-L2 for all -- see placement-census.md's utility.cpp table).
// default_exit_width[] moves WITH get_exit_width() -- its sole reader --
// per the BINDING addendum (ownership: the two are not textually adjacent in
// utility.cpp, separated there by other already-relocated functions'
// comments, but belong together here). CAN_GO()/can_breathe() are the
// batch's two resolver-dependent movers: CAN_GO()'s EXIT(ch,door) macro
// (utils.h: `#define EXIT(ch, door) (world[(ch)->in_room].dir_option[door])`)
// and can_breathe()'s two world[ch->in_room].sector_type reads are both
// unchecked in their original bodies (no bounds test anywhere) -- per the
// BINDING addendum's resolver-variant rule, both hoist a single
// room_by_id_total(ch->in_room) and replace every world[]/EXIT() site with
// the resolved pointer's fields (see task-5-report.md for the full EXIT
// macro expansion evidence). Declarations unchanged (utils.h, except
// get_exit_width()/default_exit_width[], which have no declaring header
// anywhere in the tree -- file-local by precedent).
//============================================================================

int default_exit_width[] = {
    2, /* #define SECT_INSIDE          0 */
    4, /* #define SECT_CITY            1 */
    6, /* #define SECT_FIELD           2 */
    5, /* #define SECT_FOREST          3 */
    5, /* #define SECT_HILLS           4 */
    5, /* #define SECT_MOUNTAIN        5 */
    5, /* #define SECT_WATER_SWIM      6 */
    5, /* #define SECT_WATER_NOSWIM    7 */
    5, /* #define SECT_UNDERWATER      8 */
    4, /* #define SECT_ROAD            9 */
    3, /* #define SECT_CRACK          10 */
    3, /* #define SECT_DENSE_FOREST   11 */
    5, /* #define SECT_SWAMP          12 */
    0
};

int get_exit_width(struct room_data* room, int dir)
{
    if (!room || (dir < 0) || (dir >= NUM_OF_DIRS))
        return -1;
    if (!room->dir_option[dir])
        return -1;

    if (room->dir_option[dir]->exit_width != 0)
        return room->dir_option[dir]->exit_width;

    return default_exit_width[room->sector_type];
}

/* moved from utils.h */
int CAN_GO(struct char_data* ch, int door)
{
    room_data* r = room_by_id_total(ch->in_room);
    if ((r->dir_option[door] && (r->dir_option[door]->to_room != NOWHERE)) && (!(IS_SHADOW(ch) ? (IS_SET(r->dir_option[door]->exit_info, EX_DOORISHEAVY) && IS_SET(r->dir_option[door]->exit_info, EX_CLOSED)) : IS_SET(r->dir_option[door]->exit_info, EX_CLOSED)) || IS_SET(r->dir_option[door]->exit_info, EX_ISBROKEN)))
        return 1;

    return 0;
}

int can_breathe(struct char_data* ch)
{
    int result;

    result = 1;
    room_data* r = room_by_id_total(ch->in_room);
    if (r->sector_type == SECT_UNDERWATER) {
        result = 0;
        if (IS_AFFECTED(ch, AFF_BREATHE) || IS_SHADOW(ch) || (GET_RACE(ch) == RACE_UNDEAD))
            result = 1;
    }
    if (r->sector_type == SECT_WATER_NOSWIM && !(can_swim(ch)))
        result = 0;

    return result;
}
