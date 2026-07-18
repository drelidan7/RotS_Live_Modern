/* db_world.cc */

// Carved out of db.cpp (Phase: db-split Task 1, spec Sec4a) -- holds the WORLD
// half of the old persist/world split: index/world/mobile/object/script file
// loaders, the room_data/room_data_extension implementation, real_*()/vnum_*()
// lookups, and the small-map/draw-map utilities. db.cpp keeps the persist (P)
// and boot-orchestration (B) halves; db.h still declares every symbol either
// side calls, so callers outside these two TUs are unaffected.

#include "platdef.h"
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

// unistd.h doesn't exist on Windows (Phase 3 Task 5); everything this file uses
// from it (open()/close()/lstat()'s POSIX fd plumbing) lives inside the
// open_secure_temp_output_file()/write_text_file_atomically_clearing_stale_tmp()
// PREDEF_PLATFORM_LINUX branches below, which is why this include alone can be
// platform-gated without also needing to touch fcntl.h/sys/stat.h (both exist,
// in a reduced form, on the Windows CRT too, so they stay unconditional).
#if defined PREDEF_PLATFORM_LINUX
#include <unistd.h>
#elif defined PREDEF_PLATFORM_WINDOWS
// _sopen_s/_close (open_secure_temp_output_file) and GetFileAttributesA
// (write_text_file_atomically_clearing_stale_tmp) below -- Windows'
// equivalents of the <unistd.h> POSIX fd plumbing this file needs.
#include <io.h>
#endif

#include "color.h"
#include "comm.h"
#include "db.h"
#include "persist_hooks.h"
#include "handler.h"
#include "interpre.h"
#include "limits.h"
#include "mail.h"
#include "mudlle.h"
#include "pkill.h"
#include "platform_compat.h"
#include "protos.h"
#include "spells.h"
#include "rots/persist/file_formats.h"
#include "rots/core/character.h"
#include "rots/core/object.h"
#include "rots/core/room.h"
#include "rots/core/descriptor.h"
#include "rots/core/types.h"
#include "utils.h"
#include "zone.h"

#include "account_cache.h"
#include "account_management.h"
#include "big_brother.h"
#include "char_utils.h"
#include "character_json.h"
#include "exploits_json.h"
#include "json_utils.h"
#include "text_view.h"
#include "player_file_finalize.h"
#include "skill_timer.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <iostream>
#include <iterator>
#include <new>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

/**************************************************************************
 *  world-side 'global' variables (moved from db.cpp; see db.cpp for the   *
 *  persist/boot-side globals that remain there)                           *
 ************************************************************************ */

room_data* room_data::BASE_WORLD = 0;
int room_data::BASE_LENGTH = 0;
int room_data::TOTAL_LENGTH = 0;
room_data_extension* room_data::BASE_EXTENSION = 0;

struct room_data world; // = 0;  new room_data; /* class of rooms      	*/
int top_of_world = 0; /* ref to the top element of world	*/

struct index_data* mob_index; /* index table for mobile file	*/
struct char_data* mob_proto; /* prototypes for mobs		*/
int top_of_mobt = 0; /* top of mobile index table	*/

struct index_data* obj_index; /* index table for object file	*/
struct obj_data* obj_proto; /* prototypes for objs		*/
int top_of_objt = 0; /* top of object index table	*/

struct script_head* script_table = 0;
int top_of_script_table = 0;

extern const std::string_view mobile_program_base[];
char** mobile_program;
int* mobile_program_zone;
int num_of_programs;

int new_mud = 0;
extern int r_mortal_start_room[]; /* rnum of mortal start room	*/
extern int r_mortal_idle_room[]; /* rnum of mortal idle room	*/
int r_immort_start_room; /* rnum of immort start room	*/
int r_frozen_start_room; /* rnum of frozen start room	*/
int r_retirement_home_room; /* rnum of retirement home      */

char world_map[WORLD_AREA + 1];
char small_map[2 * SMALL_WORLD_RADIUS + 3]
              [4 * SMALL_WORLD_RADIUS + 7]; // Ingolemo small_map addition

// The following remain DEFINED in db.cpp (B: boot-orchestration globals) but
// are read by the world loaders below; re-declared locally per this
// codebase's existing convention for cross-TU globals (see e.g. db.cpp's own
// callers act_comm.cpp/fight.cpp re-declaring `extern struct room_data world;`).
extern char buf[MAX_STRING_LENGTH];
extern char buf1[MAX_STRING_LENGTH];
extern char buf2[MAX_STRING_LENGTH];
extern struct char_data* character_list;
extern struct obj_data* object_list;
extern int mini_mud;
extern int boot_mode;

// Also cross-TU externs db.cpp's original "external functions" prototype
// block declared (not W-classified globals themselves, just needed by the
// world loaders below); moved here since nothing in db.cpp's remaining P/B
// code uses them anymore.
extern byte language_number;
extern byte language_skills[];
extern universal_list* affected_list;
extern universal_list* affected_list_pool;
char* mudlle_converter(char*);

/* local functions */
void setup_dir(FILE* fl, int room, int dir);
void index_boot(int mode);
void load_rooms(FILE* fl);
void load_mobiles(FILE* mob_f);
void load_objects(FILE* obj_f);
void load_mudlle(FILE* fp);
void load_scripts(FILE* fl);
void draw_map();
void initialiaze_small_map();
void reset_small_map();
void boot_the_shops(FILE* shop_f, char* filename);
void boot_mudlle();
void check_start_rooms(void);
void renum_world(void);
char* fread_line(FILE* fp);

/* function to count how many hash-mark delimited records exist in a file */
int count_hash_records(FILE* fl)
{
    char buf[120];
    int count = 0;

    int tmp;

    while (fgets(buf, 120, fl)) {

        for (tmp = 0; (buf[tmp] != 0) && (buf[tmp] <= ' '); tmp++)
            ;
        if (buf[tmp] == '#')

            count++;
    }
    return (count - 1);
}

void index_boot(int mode)
{
    const char *index_filename, *prefix = NULL;
    FILE *index, *db_file;
    int rec_count = 0;

    switch (mode) {
    case DB_BOOT_WLD:
        prefix = WLD_PREFIX;
        break;
    case DB_BOOT_MOB:
        prefix = MOB_PREFIX;
        break;
    case DB_BOOT_OBJ:
        prefix = OBJ_PREFIX;
        break;
    case DB_BOOT_ZON:
        prefix = ZON_PREFIX;
        break;
    case DB_BOOT_SHP:
        prefix = SHP_PREFIX;
        break;
    case DB_BOOT_MDL:
        prefix = MDL_PREFIX;
        break;
    case DB_BOOT_SCR:
        prefix = SCR_PREFIX;
        break;
    default:
        log("SYSERR: Unknown subcommand to index_boot!");
        exit(1);
        break;
    }

    if (mini_mud)
        index_filename = MINDEX_FILE;
    else {
        if (new_mud)
            index_filename = NEWINDEX_FILE;
        else
            index_filename = INDEX_FILE;
    }

    strcpy(buf2, std::format("{}/{}", prefix, index_filename).c_str());

    if (!(index = fopen(buf2, "r"))) {
        perror(
            std::format("Error opening index file '{}'", static_cast<const char*>(buf2)).c_str());
        exit(1);
    }
    /* first, count the number of records in the file so we can malloc */
    if (mode != DB_BOOT_SHP) {
        fscanf(index, "%s\n", buf1);
        while (*buf1 != '$') {
            strcpy(buf2, std::format("{}/{}", prefix, static_cast<const char*>(buf1)).c_str());
            if (!(db_file = fopen(buf2, "r"))) {
                perror(buf2);
                exit(1);
            } else {
                if (mode == DB_BOOT_ZON)
                    rec_count++;
                else
                    rec_count += count_hash_records(db_file);
            }
            fclose(db_file);
            fscanf(index, "%s\n", buf1);
        }
        if (!rec_count) {
            log("SYSERR: boot error - 0 records counted");
            exit(1);
        }

        rec_count++;

        switch (mode) {
        case DB_BOOT_WLD:
            //	 CREATE(world, struct room_data, rec_count);
            world.create_bulk(rec_count);
            break;
        case DB_BOOT_MOB:
            CREATE(mob_proto, struct char_data, rec_count);
            CREATE(mob_index, struct index_data, rec_count);
            break;
        case DB_BOOT_OBJ:
            CREATE(obj_proto, struct obj_data, rec_count);
            CREATE(obj_index, struct index_data, rec_count);
            break;
        case DB_BOOT_ZON:
            CREATE(zone_table, struct zone_data, rec_count + 1);
            break;
        case DB_BOOT_MDL:
            CREATE(mobile_program, char*, rec_count + 1);
            CREATE(mobile_program_zone, int, rec_count + 1);
            num_of_programs = 0;
            break;
        case DB_BOOT_SCR:
            CREATE(script_table, struct script_head, rec_count);
            break;
        }
    }
    rewind(index);
    fscanf(index, "%s\n", buf1);
    while (*buf1 != '$') {
        strcpy(buf2, std::format("{}/{}", prefix, static_cast<const char*>(buf1)).c_str());
        if (!(db_file = fopen(buf2, "r"))) {
            perror(buf2);
            exit(1);
        }
        log(std::format("opened file {}.", static_cast<const char*>(buf2)));
        switch (mode) {
        case DB_BOOT_WLD:
            load_rooms(db_file);
            break;
        case DB_BOOT_OBJ:
            load_objects(db_file);
            break;
        case DB_BOOT_MOB:
            load_mobiles(db_file);
            break;
        case DB_BOOT_ZON:
            load_zones(db_file);
            break;
        case DB_BOOT_SHP:
            boot_the_shops(db_file, buf2);
            break;
        case DB_BOOT_MDL:
            load_mudlle(db_file);
            break;
        case DB_BOOT_SCR:
            load_scripts(db_file);
            break;
        }

        fclose(db_file);
        log("closed it.");
        fscanf(index, "%s", buf1);
    }
}

/* load the rooms */
void load_rooms(FILE* fl)
{
    extern char num_of_sector_types;
    static int room_nr = 0, zone = 0, virt_nr, flag, tmp, tmp2, tmp3, tmp4;
    int aff_set;
    char *temp, *temp2, chk[50];
    struct extra_descr_data* new_descr;
    struct affected_type* base_af;
    universal_list* tmplist;

    do {
        fscanf(fl, "#%d", &virt_nr);
        //      printf("reading room %d: %d\n",room_nr,virt_nr);
        strcpy(buf2, std::format("room #{}", virt_nr).c_str());
        temp = fread_string(fl, buf2);
        for (temp2 = temp; *temp2 && *temp2 < ' '; temp2++)
            ;
        //	printf("room %d:%s.flag=%d.\n",virt_nr, temp,(*temp2 != '$'));

        if ((flag = (*temp2 != '$'))) { /* a new record to be read */
            world[room_nr].number = virt_nr;
            world[room_nr].name = temp2;
            world[room_nr].description = fread_string(fl, buf2);
            fgets(buf, 255, fl);
            tmp = tmp2 = tmp3 = tmp4 = 0;
            if (top_of_zone_table >= 0) {
                //	    fscanf(fl, " %*d ");
                sscanf(buf, "%d %d %d %d", &tmp, &tmp2, &tmp3, &tmp4);

                /* OBS: Assumes ordering of input rooms */

                if (world[room_nr].number <= (zone ? zone_table[zone - 1].top : -1)) {
                    fprintf(stderr, "Room nr %d is below zone top %d.\n", virt_nr,
                        (zone ? zone_table[zone - 1].number : -1));
                    exit(0);
                }
                while (world[room_nr].number > zone_table[zone].top)
                    if (++zone > top_of_zone_table) {
                        fprintf(stderr, "Room %d is outside of any zone.\n", virt_nr);
                        exit(0);
                    }
                world[room_nr].zone = zone;
            } else
                sscanf(buf, "%d %d %d", &tmp2, &tmp3, &tmp4);

            //	 fscanf(fl, " %d ", &tmp);
            world[room_nr].room_flags = tmp2;
            //	 fscanf(fl, " %d ", &tmp);
            world[room_nr].sector_type = tmp3;
            if (world[room_nr].sector_type >= num_of_sector_types)
                world[room_nr].sector_type = num_of_sector_types - 1;
            world[room_nr].level = tmp4;

            world[room_nr].funct = 0;
            world[room_nr].contents = 0;
            world[room_nr].people = 0;
            world[room_nr].light = 0; /* Zero light sources */

            if (world[room_nr].room_flags) {
                CREATE1(base_af, affected_type);
                base_af->type = ROOMAFF_SPELL;
                base_af->duration = -1;
                base_af->modifier = 0;
                base_af->location = SPELL_NONE;
                base_af->bitvector = world[room_nr].room_flags | PERMAFFECT;

                world[room_nr].affected = base_af;
                base_af = 0;
            }
            for (tmp = 0; tmp <= 5; tmp++)
                world[room_nr].dir_option[tmp] = 0;

            world[room_nr].ex_description = 0;
            aff_set = 0;

            for (;;) {
                fscanf(fl, " %s \n", chk);

                if (*chk == 'D') /* direction field */
                    setup_dir(fl, room_nr, atoi(chk + 1));
                else if (*chk == 'E') /* extra description field */ {
                    CREATE(new_descr, struct extra_descr_data, 1);
                    new_descr->keyword = fread_string(fl, buf2);
                    new_descr->description = fread_string(fl, buf2);
                    new_descr->next = world[room_nr].ex_description;
                    world[room_nr].ex_description = new_descr;

                } else if (*chk == 'F') /* extra description field */ {

                    fgets(buf, 255, fl);
                    sscanf(buf, "%d %d %d %d", &tmp, &tmp2, &tmp3, &tmp4);

                    CREATE1(base_af, affected_type);

                    if (!aff_set) { /* putting the room to the affection list */
                        tmplist = pool_to_list(&affected_list, &affected_list_pool);
                        tmplist->ptr.room = &world[room_nr];
                        tmplist->number = world[room_nr].number;
                        tmplist->type = TARGET_ROOM;
                        aff_set = 1;
                    }

                    base_af->type = tmp; // ROOMAFF_SPELL;
                    base_af->location = tmp2; // SPELL_NONE;
                    base_af->duration = -1;
                    base_af->modifier = tmp3; // spell level
                    base_af->bitvector = tmp4 | PERMAFFECT; // what flags to set

                    base_af->next = world[room_nr].affected;
                    world[room_nr].affected = base_af;

                    base_af = 0;

                } else if (*chk == 'S') /* end of current room */
                    break;
            }

            room_nr++;
        }
    } while (flag);
    RELEASE(temp); /* cleanup the area containing the terminal $  */

    top_of_world = room_nr - 1;
    top_of_world++; // this is for the dummy EXTENSION_ROOM_HEAD room
                    // printf("top_of_world=%d\n",top_of_world);
}

/* read direction data */
void setup_dir(FILE* fl, int room, int dir)
{
    int tmp;

    strcpy(buf2, std::format("Room #{}, direction D{}", world[room].number, dir).c_str());

    CREATE(world[room].dir_option[dir], struct room_direction_data, 1);

    world[room].dir_option[dir]->general_description = fread_string(fl, buf2);
    world[room].dir_option[dir]->keyword = fread_string(fl, buf2);

    fscanf(fl, " %d ", &tmp);
    world[room].dir_option[dir]->exit_info = tmp;

    fscanf(fl, " %d ", &tmp);
    world[room].dir_option[dir]->key = tmp;

    fscanf(fl, " %d ", &tmp);
    world[room].dir_option[dir]->to_room = tmp;

    fscanf(fl, " %d ", &tmp);
    world[room].dir_option[dir]->exit_width = tmp;
    /*UPDATE*/
}

void check_start_rooms(void)
{
    extern int mortal_start_room[];
    extern int mortal_idle_room[];
    extern int retirement_home_room;
    extern int immort_start_room;
    extern int frozen_start_room;
    int tmp;

    for (tmp = 0; tmp < MAX_RACES; tmp++)
        if ((r_mortal_start_room[tmp] = real_room(mortal_start_room[tmp])) < 0)
            r_mortal_start_room[tmp] = 0;
    for (tmp = 0; tmp < MAX_RACES; tmp++)
        if ((r_mortal_idle_room[tmp] = real_room(mortal_idle_room[tmp])) < 0)
            r_mortal_idle_room[tmp] = 0;

    if ((r_retirement_home_room = real_room(retirement_home_room)) < 0) {
        log("SYSERR:  Warning: retirement room does not exist.  Change in "
            "config.cc.");
        r_retirement_home_room = r_mortal_start_room[0];
    }
    if ((r_immort_start_room = real_room(immort_start_room)) < 0) {
        if (!mini_mud && !new_mud)
            log("SYSERR:  Warning: Immort start room does not exist.  Change in "
                "config.c.");
        r_immort_start_room = r_mortal_start_room[0];
    }

    if ((r_frozen_start_room = real_room(frozen_start_room)) < 0) {
        if (!mini_mud && !new_mud)
            log("SYSERR:  Warning: Frozen start room does not exist.  Change in "
                "config.c.");
        r_frozen_start_room = r_mortal_start_room[0];
    }
}

void renum_world(void)
{
    int room, door;

    for (room = 0; room <= top_of_world; room++)
        for (door = 0; door <= 5; door++)
            if (world[room].dir_option[door])
                if (world[room].dir_option[door]->to_room != NOWHERE)
                    world[room].dir_option[door]->to_room = real_room(world[room].dir_option[door]->to_room);
}

void symbol_to_map(int x, int y, int symb)
{
    if (x > WORLD_SIZE_X / 2)
        x = WORLD_SIZE_X / 2;
    world_map[(y + 1) * (WORLD_SIZE_X + 4) + x * 2 + 1] = symb;
}

void draw_map()
{
    int tmp;

    memset(world_map, ' ', WORLD_AREA);
    world_map[WORLD_AREA] = 0;

    for (tmp = 1; tmp < WORLD_SIZE_X + 1; tmp++) {
        world_map[tmp] = '-';
        world_map[WORLD_AREA - tmp - 3] = '-';
    }
    for (tmp = 0; tmp < WORLD_SIZE_Y + 2; tmp++) {
        world_map[tmp * (WORLD_SIZE_X + 4)] = '|';
        world_map[tmp * (WORLD_SIZE_X + 4) + WORLD_SIZE_X + 1] = '|';
        world_map[tmp * (WORLD_SIZE_X + 4) + WORLD_SIZE_X + 2] = '\n';
        world_map[tmp * (WORLD_SIZE_X + 4) + WORLD_SIZE_X + 3] = '\r';
    }
    world_map[0] = '+';
    world_map[WORLD_AREA - 3] = '+';
    world_map[WORLD_SIZE_X + 1] = '+';
    world_map[WORLD_AREA - WORLD_SIZE_X - 4] = '+';

    for (tmp = 0; tmp <= top_of_zone_table; tmp++)
        symbol_to_map(zone_table[tmp].x, zone_table[tmp].y, zone_table[tmp].symbol);
    //    printf("map is:\n%s\n",world_map);

    // Anduin...
    for (tmp = 2; tmp < 19; tmp++)
        symbol_to_map(8, tmp, '~');
    symbol_to_map(7, 17, '~');
    symbol_to_map(6, 17, '~');
    symbol_to_map(6, 16, '~');
    symbol_to_map(5, 16, '~');
    symbol_to_map(4, 16, '~');
    symbol_to_map(4, 15, '~');
    symbol_to_map(7, 19, '~');
    symbol_to_map(7, 20, '~');
    symbol_to_map(6, 21, '~');
    symbol_to_map(5, 22, '~');
    symbol_to_map(5, 23, '~');
    symbol_to_map(5, 24, '~');
    symbol_to_map(6, 25, '~');
}

//************begin Ingolemo small map addition************
void reset_small_map()
{
    for (int tmp1 = 1; tmp1 <= 2 * SMALL_WORLD_RADIUS + 1; tmp1++) {
        for (int tmp2 = 1; tmp2 <= (2 * SMALL_WORLD_RADIUS + 1) * 2 + 1; tmp2++) {
            small_map[tmp1][tmp2] = ' ';
        }
    }
}

void initialiaze_small_map()
{
    reset_small_map();
    small_map[0][0] = '+';
    small_map[0][4 * SMALL_WORLD_RADIUS + 4] = '+';
    small_map[2 * (SMALL_WORLD_RADIUS + 1)][0] = '+';
    small_map[2 * (SMALL_WORLD_RADIUS + 1)][4 * SMALL_WORLD_RADIUS + 4] = '+';
    for (int tmp = 1; tmp <= 4 * SMALL_WORLD_RADIUS + 3; tmp++) {
        small_map[0][tmp] = '-';
    }
    for (int tmp = 1; tmp <= 4 * SMALL_WORLD_RADIUS + 3; tmp++) {
        small_map[2 * SMALL_WORLD_RADIUS + 2][tmp] = '-';
    }
    for (int tmp = 1; tmp <= 2 * SMALL_WORLD_RADIUS + 1; tmp++) {
        small_map[tmp][0] = '|';
    }
    for (int tmp = 1; tmp <= 2 * SMALL_WORLD_RADIUS + 1; tmp++) {
        small_map[tmp][4 * SMALL_WORLD_RADIUS + 4] = '|';
    }
    for (int tmp = 0; tmp <= 2 * SMALL_WORLD_RADIUS + 1; tmp++) {
        small_map[tmp][4 * SMALL_WORLD_RADIUS + 5] = '\n';
    }
    for (int tmp = 0; tmp <= 2 * SMALL_WORLD_RADIUS + 1; tmp++) {
        small_map[tmp][4 * SMALL_WORLD_RADIUS + 6] = '\r';
    }
}
//************end Ingolemo small map addition************

void load_scripts(FILE* fl)
{
    static int script_no = 0;
    int tmp, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, tmp8;
    char* check;
    script_data* newscript;
    script_data* lastcmd;

    for (;;) {
        fscanf(fl, "#%d\n", &tmp);
        //	sprintf(buf2, "beginning of script #%d", tmp);
        if (tmp == 99999)
            break;

        script_table[script_no].number = tmp;
        check = fread_string(fl, buf2);

        if (*check == '$')
            break; // end of file

        newscript = 0;
        lastcmd = 0;
        script_table[script_no].name = check;
        script_table[script_no].description = fread_string(fl, buf2);
        script_table[script_no].script = 0;

        for (;;) {
            fscanf(fl, "%d %d %d %d %d %d %d %d\n", &tmp1, &tmp2, &tmp3, &tmp4, &tmp5, &tmp6, &tmp7,
                &tmp8);

            if (tmp1 == 999)
                break;

            CREATE1(newscript, script_data);
            newscript->room = 0;
            newscript->next = 0;
            newscript->text = 0;

            if (lastcmd) {
                newscript->prev = lastcmd;
                lastcmd->next = newscript;
            } else {
                newscript->prev = 0;
                script_table[script_no].script = newscript;
            }
            lastcmd = newscript;

            newscript->command_type = tmp1;
            newscript->number = tmp2;
            newscript->param[0] = tmp3;
            newscript->param[1] = tmp4;
            newscript->param[2] = tmp5;
            newscript->param[3] = tmp6;
            newscript->param[4] = tmp7;
            newscript->param[5] = tmp8;
            newscript->text = fread_string(fl, buf2);
        }

        script_no++;
    } // for (; ;)
    top_of_script_table = script_no - 1;
}

/*************************************************************************
 *  procedures for resetting, both play-time and boot-time	 	 *
 *********************************************************************** */

int vnum_mobile(char* searchname, struct char_data* ch)
{
    int nr, found = 0;

    for (nr = 0; nr <= top_of_mobt; nr++) {
        if (isname_nullable(searchname, mob_proto[nr].player.name)) {
            send_to_char(std::format("{:3}. [{:5}] {:<60.60}\n\r", ++found, mob_index[nr].virt,
                             nz(mob_proto[nr].player.short_descr))
                             ,
                ch);
        }
    }

    return (found);
}

int vnum_object(char* searchname, struct char_data* ch)
{
    int nr, found = 0;

    for (nr = 0; nr <= top_of_objt; nr++) {
        if (isname_nullable(searchname, obj_proto[nr].name)) {
            send_to_char(std::format("{:3}. [{:5}] {:<60.60}\n\r", ++found, obj_index[nr].virt,
                             nz(obj_proto[nr].short_description))
                             ,
                ch);
        }
    }
    return (found);
}

/* create a new mobile from a prototype */
struct char_data* read_mobile(int nr, int type)
{
    extern int average_mob_life;
    int i, age, was_fixed;
    byte tmp;
    struct char_data* mob;
    affected_type tmp_aff;

    if (type == VIRT) {
        if ((i = real_mobile(nr)) < 0) {
            strcpy(buf, std::format("Mobile (V) {} does not exist in database.", nr).c_str());
            return (0);
        }
    } else
        i = nr;

    CREATE(mob, struct char_data, 1);

    /* mob is raw calloc'd storage (CREATE == calloc, no ctor runs). Construct it
     * before the struct-copy below runs std::map/other non-trivial member
     * copy-assignment operators on it — see clear_char() for the full rationale.
     * mob_proto[i] (the source) is itself constructed via clear_char() in
     * load_mobiles(), so both operands of the assignment are valid by this point. */
    new (mob) char_data();

    *mob = mob_proto[i];

    mob->in_room = NOWHERE;

    mob->abilities.hit = number(mob->tmpabilities.hit, mob->abilities.hit);

    mob->tmpabilities = mob->abilities;
    mob->constabilities = mob->abilities;

    was_fixed = 0;

    if (GET_STR(mob) <= 0) {
        SET_STR(mob, 17);
        SET_STR_BASE(mob, 17);
        was_fixed = 1;
    }
    if (GET_INT(mob) <= 0) {
        GET_INT(mob) = 17;
        GET_INT_BASE(mob) = 17;
        was_fixed = 1;
    }
    if (GET_WILL(mob) <= 0) {
        GET_WILL(mob) = 17;
        GET_WILL_BASE(mob) = 17;
        was_fixed = 1;
    }
    if (GET_DEX(mob) <= 0) {
        GET_DEX(mob) = 17;
        GET_DEX_BASE(mob) = 17;
        was_fixed = 1;
    }
    if (GET_CON(mob) <= 0) {
        GET_CON(mob) = 17;
        GET_CON_BASE(mob) = 17;
        was_fixed = 1;
    }
    if (GET_LEA(mob) <= 0) {
        GET_LEA(mob) = 17;
        GET_LEA_BASE(mob) = 17;
        was_fixed = 1;
    }
    if (was_fixed) {
        strcpy(buf,
            std::format("Mobile {} had its stats fixed.", (nr >= 0) ? mob_index[nr].virt : -1)
                .c_str());
        mudlog(buf, CMP, LEVEL_GRGOD, TRUE);
    }

    mob->specials2.rawPerception = mob->specials2.perception = get_naked_perception(mob);
    // if((mob->specials2.perception == -1) || IS_SHADOW(mob))
    //  mob->specials2.perception = get_naked_perception(mob);

    GET_WILLPOWER(mob) = get_naked_willpower(mob);

    if (boot_mode) {
        age = number(0, average_mob_life * 2);
        mob->player.time.birth = time(0) - age * SECS_PER_MUD_HOUR;
        mob->player.time.played = 0;
        mob->player.time.logon = time(0) - age * SECS_PER_MUD_HOUR;
    } else {
        mob->player.time.birth = time(0);
        mob->player.time.played = 0;
        mob->player.time.logon = time(0);
    }
    if ((mob->specials.store_prog_number != 0) && (!IS_SET(mob->specials2.act, MOB_SPEC))) {
        // RAII T5a: the special-mob script stack/list/call-list/call-point
        // buffers now live in their own typed fields (special_stack,
        // special_list_area, special_prog_number, special_prog_point) instead
        // of being reinterpret_cast'd through poofIn/poofOut/union1/union2.
        // poofIn/poofOut are PC-only strings and stay null for mobs.
        CREATE(mob->specials.special_stack, long, SPECIAL_STACKLEN);
        CREATE1(mob->specials.special_list_area, special_list);

        tmp = mob->specials.store_prog_number;
        mob->specials.store_prog_number = 0;
        CREATE(mob->specials.special_prog_number, int, SPECIAL_CALLLIST);
        CREATE(mob->specials.special_prog_point, int, SPECIAL_CALLLIST);
        mob->specials.special_prog_number[0] = tmp;
        mob->specials.special_prog_point[0] = 0;
        mob->specials.tactics = 0;

        for (tmp = 0; tmp < SPECIAL_STACKLEN; tmp++) {
            SPECIAL_LIST_AREA(mob)->field[tmp].ptr.ch = 0;
            SPECIAL_LIST_AREA(mob)->next[tmp] = -1;
            SPECIAL_LIST_AREA(mob)->field[tmp].type = SPECIAL_VOID;
        }
        //     SPECIAL_LIST_HEAD(mob) = -1;
        SPECIAL_LIST_HEAD(mob) = 0;
        SPECIAL_LIST_AREA(mob)->field[0].type = SPECIAL_MARK;
        SPECIAL_LIST_AREA(mob)->next[0] = -1;
        mob->specials.invis_level = 0;
        CALL_MASK(mob) = 255;
    } else {
        mob->specials.special_stack = 0;
        mob->specials.special_list_area = 0;
        mob->specials.special_prog_number = 0;
        mob->specials.special_prog_point = 0;
    }
    mob->specials.poofIn.clear();
    mob->specials.poofOut.clear();
    mob->specials.recite_lines = NULL;
    /* insert in list */
    mob->next = character_list;
    character_list = mob;

    mob_index[i].number++;
    //- this appeared to fix new mage, but broke prac and rent sometimes
    // mob_index[i].func = 0;

    register_npc_char(mob);

    if (MOB_FLAGGED(mob, MOB_FAST)) {
        tmp_aff.type = SPELL_ACTIVITY;
        tmp_aff.duration = -1;
        tmp_aff.modifier = 1;
        tmp_aff.location = APPLY_SPEED;
        tmp_aff.bitvector = 0;
        affect_to_char(mob, &tmp_aff);
    }

    return mob;
}

void load_mobiles(FILE* mob_f)
{
    static int i = 0;
    int nr, j;
    int tmp, tmp2, tmp3, tmp4, tmp5, tmp6;
    char chk[10], *tmpptr;
    char letter;

    if (!fscanf(mob_f, "%s\n", chk)) {
        perror("load_mobiles");
        exit(1);
    }

    for (;;) {
        if (*chk == '#') {
            sscanf(chk, "#%d\n", &nr);
            if (nr >= 99999)
                break;

            mob_index[i].virt = nr;
            mob_index[i].number = 0;
            mob_index[i].func = 0;

            clear_char(mob_proto + i, MOB_ISNPC);

            strcpy(buf2, std::format("mob vnum {}", nr).c_str());

            /***** String data *** */
            mob_proto[i].player.name = fread_string(mob_f, buf2);
            tmpptr = mob_proto[i].player.short_descr = fread_string(mob_f, buf2);

            if (tmpptr && *tmpptr)
                if (!str_cmp_nullable(fname(tmpptr), "a") || !str_cmp_nullable(fname(tmpptr), "an") || !str_cmp_nullable(fname(tmpptr), "the"))
                    *tmpptr = tolower(*tmpptr);

            mob_proto[i].player.long_descr = fread_string(mob_f, buf2);
            mob_proto[i].player.description = fread_string(mob_f, buf2);

            CREATE(mob_proto[i].player.title, char, 1);

            fscanf(mob_f, "%d ", &tmp);
            MOB_FLAGS(mob_proto + i) = tmp;
            SET_BIT(MOB_FLAGS(mob_proto + i), MOB_ISNPC);

            fscanf(mob_f, " %d %d %c \n", &tmp, &tmp2, &letter);
            mob_proto[i].specials.affected_by = tmp;
            GET_ALIGNMENT(mob_proto + i) = tmp2;
            GET_LOADLINE(mob_proto + i) = 0;

            /* New monsters */
            if (letter == 'N') {
                mob_proto[i].player.death_cry = fread_string(mob_f, buf2);
                mob_proto[i].player.death_cry2 = fread_string(mob_f, buf2);
            } else {
                mob_proto[i].player.death_cry = 0;
                mob_proto[i].player.death_cry2 = 0;
            }

            if ((letter == 'M') || (letter == 'N')) {
                fscanf(mob_f, " %d %d %d %d", &tmp, &tmp2, &tmp3, &tmp4);
                GET_LEVEL(mob_proto + i) = tmp;
                SET_OB(mob_proto + i) = tmp2;
                SET_DODGE(mob_proto + i) = tmp4;
                SET_PARRY(mob_proto + i) = tmp3;

                fscanf(mob_f, " %d %d", &tmp, &tmp2);
                mob_proto[i].tmpabilities.hit = tmp;
                mob_proto[i].abilities.hit = tmp2;

                fscanf(mob_f, " %d %d \n", &tmp, &tmp2);
                mob_proto[i].points.damage = tmp;
                mob_proto[i].points.ENE_regen = tmp2;
                mob_proto[i].specials.ENERGY = 1200;

                fscanf(mob_f, " %d %d %d \n", &tmp, &tmp2, &tmp3);
                GET_GOLD(mob_proto + i) = tmp;
                GET_EXP(mob_proto + i) = tmp2;
                /* Here we load owner integer */

                fscanf(mob_f, " %d %d %d %d %d \n", &tmp, &tmp2, &tmp3, &tmp4, &tmp5);
                mob_proto[i].specials.position = tmp;
                mob_proto[i].specials.default_pos = tmp2;
                mob_proto[i].player.sex = tmp3;
                mob_proto[i].player.race = tmp4;
                mob_proto[i].specials2.pref = tmp5;

                mob_proto[i].player.prof = 0;

                fscanf(mob_f, " %d %d %d %d %d %d \n", &tmp, &tmp2, &tmp3, &tmp4, &tmp5, &tmp6);
                mob_proto[i].player.weight = tmp;
                mob_proto[i].player.height = tmp2;
                mob_proto[i].specials.store_prog_number = tmp3;
                mob_proto[i].specials.butcher_item = tmp4;
                if (!IS_SET((mob_proto + i)->specials2.act, MOB_SPEC))
                    mob_proto[i].specials.store_prog_number = real_program(tmp3);
                if (letter == 'N')
                    mob_proto[i].player.corpse_num = tmp5;
                else
                    mob_proto[i].player.corpse_num = 0;
                mob_proto[i].specials2.rp_flag = tmp6;

                fscanf(mob_f, " %d %d %d %d \n", &tmp, &tmp2, &tmp3, &tmp4);
                mob_proto[i].player.prof = tmp;
                mob_proto[i].abilities.mana = tmp2;
                mob_proto[i].abilities.move = tmp3;
                mob_proto[i].player.bodytype = tmp4;

                fscanf(mob_f, " %d", &tmp);
                mob_proto[i].specials2.saving_throw = tmp;

                fscanf(mob_f, " %d %d %d %d %d %d \n", &tmp, &tmp2, &tmp3, &tmp4, &tmp5, &tmp6);
                mob_proto[i].abilities.str = tmp;
                mob_proto[i].abilities.intel = tmp2;
                mob_proto[i].abilities.wil = tmp3;
                mob_proto[i].abilities.dex = tmp4;
                mob_proto[i].abilities.con = tmp5;
                mob_proto[i].abilities.lea = tmp6;

                mob_proto[i].constabilities = mob_proto[i].abilities;

                int tmp7 = 0;
                fscanf(mob_f, " %d %d %d %d %d %d %d", &tmp, &tmp2, &tmp3, &tmp4, &tmp5, &tmp6,
                    &tmp7);
                if ((tmp > language_number) || (tmp <= 0)) {
                    mob_proto[i].player.language = 0;
                } else {
                    mob_proto[i].player.language = language_skills[tmp - 1];
                }

                mob_proto[i].specials2.perception = tmp2;
                mob_proto[i].specials.resistance = tmp3;
                mob_proto[i].specials.vulnerability = tmp4;
                mob_proto[i].specials.script_number = tmp5;
                mob_proto[i].points.spirit = tmp6;
                mob_proto[i].specials2.will_teach = tmp7;

                fscanf(mob_f, " \n");

                for (j = 0; j < 3; j++) /* Spare */
                {
                    GET_COND(mob_proto + i, j) = -1;
                }
            } /* End new monsters */

            for (j = 0; j < MAX_WEAR; j++) /* Initialisering Ok */
            {
                mob_proto[i].equipment[j] = 0;
            }

            mob_proto[i].nr = i;
            mob_proto[i].desc = 0;

            if (!fscanf(mob_f, "%s\n", chk)) {
                log(std::format("SYSERR: Format error in mob file near mob #{}", nr));
                exit(1);
            }

            i++;
        } else if (*chk == '$') /* EOF */
            break;
        else {
            log(std::format("SYSERR: Format error in mob file near mob #{}", nr));
            exit(1);
        }
    }
    top_of_mobt = i - 1;
}

/* create a new object from a prototype */
struct obj_data* read_object(int nr, int type)
{
    struct obj_data* obj;
    int i;

    if (nr < 0) {
        log("SYSERR: trying to create obj with negative num!");
        return 0;
    }

    if (type == VIRT) {
        if ((i = real_object(nr)) < 0) {
            strcpy(buf, std::format("Object (V) {} does not exist in database.", nr).c_str());
            return 0;
        }
    } else
        i = nr;

    CREATE(obj, struct obj_data, 1);
    *obj = obj_proto[i];

    /* storing closed/locked state for containers */
    if (GET_ITEM_TYPE(obj) == ITEM_CONTAINER)
        obj->obj_flags.value[4] = obj->obj_flags.value[1];

    /* add obj to the object list */
    obj->next = object_list;
    obj->obj_flags.timer = -1;
    object_list = obj;

    obj_index[i].number++;

    /*
     * Users can't create objects, only immortals, so we have to assume that
     * this is 0 as it hasn't been touched by a PC.  This should be checked in
     * do_load!
     */
    obj->touched = 0;
    obj->loaded_by = 0;

    return obj;
}

/* read all objects from obj file; generate index and prototypes */
void load_objects(FILE* obj_f)
{
    static int i = 0;
    int tmp, tmp2, tmp3, tmp4, tmp5, j, nr;
    char chk[50], *tmpptr;
    struct extra_descr_data* new_descr;

    if (!fscanf(obj_f, "%s\n", chk)) {
        perror("load_objects");
        exit(1);
    }

    for (;;) {
        if (*chk == '#') {
            sscanf(chk, "#%d\n", &nr);
            if (nr >= 99999)
                break;

            obj_index[i].virt = nr;
            obj_index[i].number = 0;
            obj_index[i].func = 0;

            clear_object(obj_proto + i);

            strcpy(buf2, std::format("object #{}", nr).c_str());

            /* *** string data *** */

            tmpptr = obj_proto[i].name = fread_string(obj_f, buf2);
            if (!tmpptr) {
                fprintf(stderr, "format error at or near %s\n", buf2);
                exit(1);
            }

            tmpptr = obj_proto[i].short_description = fread_string(obj_f, buf2);
            if (*tmpptr)
                if (!str_cmp_nullable(fname(tmpptr), "a") || !str_cmp_nullable(fname(tmpptr), "an") || !str_cmp_nullable(fname(tmpptr), "the"))
                    *tmpptr = tolower(*tmpptr);
            tmpptr = obj_proto[i].description = fread_string(obj_f, buf2);
            if (tmpptr && *tmpptr)
                *tmpptr = toupper(*tmpptr);
            obj_proto[i].action_description = fread_string(obj_f, buf2);

            /* *** numeric data *** */

            fscanf(obj_f, " %d %d %d", &tmp, &tmp2, &tmp3);
            obj_proto[i].obj_flags.type_flag = tmp;
            obj_proto[i].obj_flags.extra_flags = tmp2;
            obj_proto[i].obj_flags.wear_flags = tmp3;

            fscanf(obj_f, " %d %d %d %d %d", &tmp, &tmp2, &tmp3, &tmp4, &tmp5);
            obj_proto[i].obj_flags.value[0] = tmp;
            obj_proto[i].obj_flags.value[1] = tmp2;
            obj_proto[i].obj_flags.value[2] = tmp3;
            obj_proto[i].obj_flags.value[3] = tmp4;
            obj_proto[i].obj_flags.value[4] = tmp5;

            fscanf(obj_f, " %d %d %d", &tmp, &tmp2, &tmp3);
            obj_proto[i].obj_flags.weight = tmp;
            obj_proto[i].obj_flags.cost = tmp2;
            obj_proto[i].obj_flags.cost_per_day = tmp3;

            fscanf(obj_f, " %d %d %d %d %d", &tmp, &tmp2, &tmp3, &tmp4, &tmp5);
            obj_proto[i].obj_flags.level = tmp;
            obj_proto[i].obj_flags.rarity = tmp2;
            obj_proto[i].obj_flags.material = tmp3;
            obj_proto[i].obj_flags.script_number = tmp4;
            /*fscanf(obj_f, " %d %d %d %d %d", &tmp, &tmp2, &tmp3, &tmp4);
            obj_proto[i].obj_flags.poisoned = tmp;
            obj_proto[i].obj_flags.poisondata[0] = tmp2;
            obj_proto[i].obj_flags.poisondata[1] = tmp3;
            obj_proto[i].obj_flags.poisondata[2] = tmp4;*/

            /* *** extra descriptions *** */

            obj_proto[i].ex_description = 0;

            strcpy(
                buf2,
                std::format("{} - extra desc. section", static_cast<const char*>(buf2)).c_str());

            while (fscanf(obj_f, " %s \n", chk), *chk == 'E') {
                CREATE(new_descr, struct extra_descr_data, 1);
                new_descr->keyword = fread_string(obj_f, buf2);
                new_descr->description = fread_string(obj_f, buf2);
                new_descr->next = obj_proto[i].ex_description;
                obj_proto[i].ex_description = new_descr;
            }

            for (j = 0; (j < MAX_OBJ_AFFECT) && (*chk == 'A'); j++) {
                fscanf(obj_f, " %d %d ", &tmp, &tmp2);
                obj_proto[i].affected[j].location = tmp;
                obj_proto[i].affected[j].modifier = tmp2;
                fscanf(obj_f, " %s \n", chk);
            }

            for (; (j < MAX_OBJ_AFFECT); j++) {
                obj_proto[i].affected[j].location = APPLY_NONE;
                obj_proto[i].affected[j].modifier = 0;
            }

            obj_proto[i].in_room = NOWHERE;
            obj_proto[i].next_content = 0;
            obj_proto[i].carried_by = 0;
            obj_proto[i].in_obj = 0;
            obj_proto[i].contains = 0;
            obj_proto[i].item_number = i;
            obj_proto[i].touched = 0;

            i++;
        } else if (*chk == '$') /* EOF */
            break;
        else {
            log(std::format("Format error in obj file at or near obj #{}", nr));
            exit(1);
        }
    }
    top_of_objt = i - 1;
}

/* execute the reset command table of a given zone */
//************************************************************************
int set_exit_state(struct room_data* room, int dir, int newstate)
{
    const int door_mask = (EX_CLOSED | EX_LOCKED);
    int tmp, tmp2;
    struct char_data* tmpmob;

    if (!room)
        return 0;
    if (!room->dir_option[dir])
        return 0;
    if (room->dir_option[dir]->to_room == NOWHERE)
        return 0;
    tmp = room->dir_option[dir]->exit_info;
    if (!IS_SET(tmp, EX_ISDOOR))
        return 0; // Can't open/close not door.
    switch (newstate) {
    case 0:
        tmp2 = 0;
        break;
    case 1:
        tmp2 = EX_CLOSED;
        break;
    case 2:
        tmp2 = EX_CLOSED | EX_LOCKED;
        break;
    default:
        tmp2 = 0;
        break;
    }
    //	tmp2 = newstate;
    tmp2 = (tmp & ~door_mask) | (tmp2 & door_mask);
    if (IS_SET(tmp, EX_ISBROKEN)) {
        strcpy(buf,
            std::format("The {} blurs briefly.", nz(room->dir_option[dir]->keyword)).c_str());
        tmpmob = room->people;
        if (tmpmob) {
            act(buf, FALSE, tmpmob, 0, 0, TO_ROOM);
            act(buf, FALSE, tmpmob, 0, 0, TO_CHAR);
        }
        REMOVE_BIT(tmp2, EX_ISBROKEN);
    }
    if (IS_SET(tmp2, EX_CLOSED) && !IS_SET(tmp, EX_CLOSED)) {
        strcpy(buf,
            std::format("The {} closes quietly.", nz(room->dir_option[dir]->keyword)).c_str());
        tmpmob = room->people;
        if (tmpmob) {
            act(buf, FALSE, tmpmob, 0, 0, TO_ROOM);
            act(buf, FALSE, tmpmob, 0, 0, TO_CHAR);
        }
    }
    if (IS_SET(tmp2, EX_LOCKED) && !IS_SET(tmp, EX_LOCKED)) {
        strcpy(buf, "You hear a sound of a lock snapping shut.");
        tmpmob = room->people;
        if (tmpmob) {
            act(buf, FALSE, tmpmob, 0, 0, TO_ROOM);
            act(buf, FALSE, tmpmob, 0, 0, TO_CHAR);
        }
    }
    if (!IS_SET(tmp2, EX_LOCKED) && IS_SET(tmp, EX_LOCKED)) {
        strcpy(buf, "You hear a sound of a key turning..");
        tmpmob = room->people;
        if (tmpmob) {
            act(buf, FALSE, tmpmob, 0, 0, TO_ROOM);
            act(buf, FALSE, tmpmob, 0, 0, TO_CHAR);
        }
    }
    if (!IS_SET(tmp2, EX_CLOSED) && IS_SET(tmp, EX_CLOSED)) {
        strcpy(buf, std::format("{} opens quietly.", nz(room->dir_option[dir]->keyword)).c_str());
        tmpmob = room->people;
        if (tmpmob) {
            act(buf, FALSE, tmpmob, 0, 0, TO_ROOM);
            act(buf, FALSE, tmpmob, 0, 0, TO_CHAR);
        }
    }
    room->dir_option[dir]->exit_info = tmp2;
    return 1;
}

/* read and allocate space for a '~'-terminated string from a given file */
char* fread_string(FILE* fl, std::string_view error)
{
    char buf[MAX_STRING_LENGTH], tmp[MAX_STRING_LENGTH];
    char* rslt;
    char *point, *tmppoint;
    int flag, markfirst;

    memset(buf, 0, MAX_STRING_LENGTH);
    markfirst = 0;
    do {
        *tmp = 0;
        if (!fgets(tmp, MAX_STRING_LENGTH, fl)) {
            const std::string error_owner(rots::text::truncate_at_null(error));
            fprintf(stderr, "fread_string: format error at or near %s\n", error_owner.c_str());
            exit(0);
        }

        /* Here we skip blank lines in the beginning of the text */
        if (!markfirst) {
            for (tmppoint = tmp; *tmppoint <= ' ' && *tmppoint; tmppoint++)
                continue;
            if (!*tmppoint)
                *tmp = 0;
            markfirst = 1;
        }

        for (tmppoint = tmp; (*tmppoint < ' ') && (*tmppoint != 0); tmppoint++)
            continue;

        if (strlen(tmppoint) + strlen(buf) > MAX_STRING_LENGTH) {
            log("SYSERR: fread_string: string too large (db.c)");
            exit(0);
        } else
            strcat(buf, tmppoint);

        for (point = buf + strlen(buf) - 2; point >= buf && isspace(*point); point--)
            continue;
        // Fixed-bug (Phase 5 T6, ASan stack-buffer-underflow): when buf is
        // empty or 1 byte after trimming, the loop above leaves `point`
        // below `buf` (buf + strlen(buf) - 2, e.g. buf-2 or buf-1) without
        // ever dereferencing it (short-circuited by `point >= buf`) -- but
        // the old `*point == '~'` here dereferenced it unconditionally,
        // reading stack memory before the buffer (confirmed live: a real
        // zone file line hit this during a real world-data boot under ASan).
        // `flag` was also read uninitialized in that same case (never
        // assigned before this line ran). Bounds-check first: an
        // out-of-range `point` can't be the '~' terminator, so this matches
        // the previous behavior for every case that wasn't already
        // undefined (garbage stack bytes essentially never equal '~', so
        // real-world output is unaffected).
        flag = (point >= buf && *point == '~');
        if (flag)
            *point = 0;
        else if (strlen(buf)) {
            *(buf + strlen(buf) + 1) = '\0';
            *(buf + strlen(buf)) = '\r';
        }
    } while (!flag);

    /* do the allocate boogie  */
    if (strlen(buf) > 0) {
        CREATE(rslt, char, strlen(buf) + 1);
        strcpy(rslt, buf);
    } else
        CREATE(rslt, char, 1);

    return (rslt);
}

/*
 * Read to end of line into static buffer
 *  this function is an amended version found in Smaug - I think we should
 * credit them in some way, though to say that our code is in any way
 * smaug-based would be misleading.  On the other hand, this function is very
 * simple and we could have written it ourselves :)
 */

char* fread_line(FILE* fp)
{
    static char line[MAX_STRING_LENGTH];
    char* pline;
    char c;
    int ln;

    pline = line;
    line[0] = '\0';
    ln = 0;

    /*
     * Skip blanks.
     * Read first char.
     */
    do {
        if (feof(fp)) {
            log("fread_line: EOF encountered on read.\n\r");
            strcpy(line, "");
            return line;
        }
        c = getc(fp);
    } while (isspace(c));

    ungetc(c, fp);
    do {
        if (feof(fp)) {
            log("fread_line: EOF encountered on read.\n\r");
            *pline = '\0';
            return line;
        }
        c = getc(fp);
        *pline++ = c;
        ln++;
        if (ln >= (MAX_STRING_LENGTH - 1)) {
            log("fread_line: line too long");
            break;
        }
    } while (c != '\n' && c != '\r');

    do {
        c = getc(fp);
    } while (c == '\n' || c == '\r');

    ungetc(c, fp);
    *pline = '\0';
    return line;
}

/* returns the real number of the room with given virt number */
int real_room(int virt)
{
    int bot, top, mid;

    bot = 0;
    top = top_of_world;

    /* perform binary search on world-table */
    for (;;) {
        mid = (bot + top) / 2;

        //      if ((world + mid)->number == virt)
        if (world[mid].number == virt)
            return (mid);
        if (bot >= top) {
            if (!mini_mud && !new_mud && virt)
                fprintf(stderr, "Room %d does not exist in database\n", virt);
            return (-1);
        }
        //      if ((world + mid)->number > virt)
        if (world[mid].number > virt)
            top = mid - 1;
        else
            bot = mid + 1;
    }
}

/* returns the real number of the monster with given virt number */
int real_mobile(int virt)
{
    int bot, top, mid;
    bot = 0;
    top = top_of_mobt;

    /* perform binary search on mob-table */
    for (;;) {
        mid = (bot + top) / 2;

        if ((mob_index + mid)->virt == virt) {
            return (mid);
        }
        if (bot >= top) {
            return (-1);
        }
        if ((mob_index + mid)->virt > virt) {
            top = mid - 1;
        } else {
            bot = mid + 1;
        }
    }
}

/* returns the real number of the object with given virt number */
int real_object(int virt)
{
    int bot, top, mid;

    bot = 0;
    top = top_of_objt;

    /* perform binary search on obj-table */
    for (;;) {
        mid = (bot + top) / 2;

        if ((obj_index + mid)->virt == virt) {
            return (mid);
        }
        if (bot >= top) {
            return (-1);
        }
        if ((obj_index + mid)->virt > virt) {
            top = mid - 1;
        } else {
            bot = mid + 1;
        }
    }
}

int real_program(int virt)
{
    int tmp = 0;

    for (tmp = 0; tmp <= num_of_programs; tmp++) {
        if (mobile_program_zone[tmp] == virt) {
            break;
        }
    }

    if (tmp == num_of_programs + 1) {
        return 0;
    }

    return tmp;
}

void load_mudlle(FILE* fp)
{
    int tmp;
    char str[MAX_STRING_LENGTH];
    char* tmpstr;

    fgets(str, MAX_STRING_LENGTH, fp);
    tmpstr = str;
    while (*tmpstr && (*tmpstr < ' '))
        tmpstr++;
    do {
        sscanf(tmpstr + 1, "%d", &tmp);
        memset(str, 0, MAX_STRING_LENGTH);
        if (tmp == 99999)
            break;
        num_of_programs++;
        mobile_program_zone[num_of_programs] = tmp;
        do {
            tmpstr = str + strlen(str);
            fgets(tmpstr, MAX_STRING_LENGTH, fp);
            while (*tmpstr && (*tmpstr < ' '))
                tmpstr++;
        } while (*tmpstr != '#');
        *tmpstr = 0;
        mobile_program[num_of_programs] = str_dup(str);
    } while (1);
}

void boot_mudlle()
{
    int i;
    char* tmpstr;

    for (i = 1; i <= num_of_programs; i++) {
        tmpstr = mobile_program[i];
        mobile_program[i] = mudlle_converter(mobile_program[i]);
        //    printf("mobile_program[%d]=%s.\n",i,mobile_program[i]);
        RELEASE(tmpstr);
    }
}

//*************************************************************************
//*************************************************************************

room_data::room_data()
{
    number = -1;
    zone = 0;
    level = 0;
    name = 0;
    description = 0;
    affected = NULL;
}

void dummy_room_data(room_data* room)
{
    int tmp;

    room->name = str_dup("New room");
    room->description = str_dup("\n\r");
    room->ex_description = 0;
    room->contents = 0;
    room->people = 0;

    for (tmp = 0; tmp < NUM_OF_DIRS; tmp++) {
        room->dir_option[tmp] = 0;
    }
    room->number = -1;
    room->zone = 0;
    room->sector_type = 0;
    room->room_flags = 0;
    room->light = 0;
}
room_data_extension::room_data_extension()
{
    int tmp;
    CREATE(extension_world, room_data, EXTENSION_SIZE);
    for (tmp = 0; tmp < EXTENSION_SIZE; tmp++)
        dummy_room_data(extension_world + tmp);
    extension_next = 0;
}
room_data_extension::~room_data_extension()
{
    if (extension_next)
        delete extension_next;
    RELEASE(extension_world);
}

void room_data::create_exit(int dir, int room, char connect)
{
    int this_room;
    extern int rev_dir[];

    //  printf("create_exit called for room %d\n",room);
    this_room = real_room(number);

    if (room < 0)
        room = this_room;

    if ((dir < 0) || (dir >= NUM_OF_DIRS))
        return;

    if (dir_option[dir]) {
        //    RELEASE(dir_option[dir]->general_description);
        //    RELEASE(dir_option[dir]->keyword);
    } else {
        dir_option[dir] = new room_direction_data;
        dir_option[dir]->general_description = str_dup("");
        dir_option[dir]->keyword = str_dup("");
        dir_option[dir]->exit_width = 0;
        dir_option[dir]->exit_info = 0;
        dir_option[dir]->key = -1;
    }
    dir_option[dir]->to_room = real_room(world[room].number);
    //  printf("exit to room %d,
    //  %d\n",dir_option[dir]->to_room,world[room].number);
    if (connect && (room != this_room))
        world[room].create_exit(rev_dir[dir], this_room, FALSE);
    //  printf("create exift returns\n");
}
//************************************************************************
int room_data::create_room(int zone)
{
    // here adding a room, returns the real number of the room

    int place;
    room_data_extension* ext;
    room_data* new_room;
    // checking the base first

    new_room = 0;

    // sprintf(mybuf,"create room, base, total %d %d",BASE_LENGTH, TOTAL_LENGTH);
    //  log(mybuf);

    for (place = 0; place < TOTAL_LENGTH; place++) {
        //    printf("create_room, checking %d:
        //    %d\n",place,(BASE_WORLD+place)->number);
        if (world[place].number < 0)
            break;
    }

    if (place >= TOTAL_LENGTH) {
        //    printf("could not create room\n");
        //    exit(0);  // temporary passage

        place = TOTAL_LENGTH;
        if (!BASE_EXTENSION) {
            BASE_EXTENSION = new room_data_extension;
            TOTAL_LENGTH += EXTENSION_SIZE;
            //      sprintf(mybuf,"created first, total=%d",TOTAL_LENGTH);
            //      log(mybuf);
            // printf(" numbersm %d %d\n",world[place].number, world[place+1].number);
            new_room = BASE_EXTENSION->extension_world;
        } else {
            for (ext = BASE_EXTENSION; ext->extension_next; ext = ext->extension_next)
                ;

            ext->extension_next = new room_data_extension;
            //      sprintf(mybuf,"created next, total=%d",TOTAL_LENGTH);
            //      log(mybuf);
            new_room = ext->extension_next->extension_world;
            TOTAL_LENGTH += EXTENSION_SIZE;
        }
    } else
        new_room = &world[place];

    dummy_room_data(new_room);
    if (place == 0)
        new_room->number = 0;
    else
        new_room->number = world[place - 1].number + 1;

    //  sprintf(mybuf,"created %d, %d",place, new_room->number);
    //  log(mybuf);

    new_room->zone = zone;
    top_of_world++;
    return place;
}

/*
 * This function's only called once, after all rooms in the
 * database have been counted.  It allocates as many rooms as
 * are needed on boot.
 */
void room_data::create_bulk(int amount)
{
    int tmp;

    if (BASE_WORLD != 0) {
        printf("Double allocation for room_data!\n");
        exit(0);
    }

    // BASE_WORLD = (room_data *)calloc(sizeof(room_data), amount +
    // EXTENSION_SIZE);
    BASE_WORLD = new room_data[amount + EXTENSION_SIZE];
    if (!BASE_WORLD) {
        printf("Could not allocate %d rooms for room_data\n", amount);
        exit(0);
    }
    BASE_LENGTH = amount + EXTENSION_SIZE - 1;
    TOTAL_LENGTH = amount + EXTENSION_SIZE - 1;

    for (tmp = 0; tmp < EXTENSION_SIZE; tmp++)
        dummy_room_data(BASE_WORLD + amount - 1 + tmp);

    (BASE_WORLD + amount - 1)->number = EXTENSION_ROOM_HEAD;

    // remember that top_of_world is increased due to this in load_rooms
    BASE_EXTENSION = 0;
}
//**********************************************************************
void room_data::delete_room()
{
    printf("room_data desctructor was called.\n");
    if (BASE_EXTENSION)
        delete BASE_EXTENSION;
}

room_data& room_data::operator[](int i)
{
    int offset;
    room_data_extension* ext;

    if (!BASE_WORLD) {
        // Was exit(0): a filtered/subset gtest run that reaches world[]
        // before the world is allocated would exit the whole test process
        // with a *success* code, silently truncating the run instead of
        // failing it. abort() keeps the same protective intent (this is an
        // unrecoverable invariant violation, not something to try to limp
        // past) but reports as a crash -- non-zero exit, test-visible --
        // instead of a false-green success.
        fprintf(stderr, "SYSERR: room_data::operator[] called, but BASE_WORLD is not allocated\n");
        abort();
    }

    if (i < 0) {
        mudlog("world[] called for negative room number.", NRM, LEVEL_GOD, TRUE);
        //    send_to_all("****world[] called for negative room number.****");
        return *(BASE_WORLD);
    }

    if (i >= BASE_LENGTH) {
        offset = i - BASE_LENGTH;
        //    printf("[] extra, offset=%d\n",offset);
        ext = BASE_EXTENSION;
        while (ext && (offset >= EXTENSION_SIZE)) {
            ext = ext->extension_next;
            offset -= EXTENSION_SIZE;
        }
        if (!ext) {
            strcpy(buf,
                std::format("room_data called for a room outside the world, {}\n", i).c_str());
            mudlog(buf, NRM, LEVEL_GRGOD, TRUE);
            if (i == r_immort_start_room)
                exit(0);
            else
                return world[r_immort_start_room];
        }
        //    printf("return, offse=%d\n",offset);
        return *(ext->extension_world + offset);
    }

    return *(BASE_WORLD + i);
}

// Seam (db.cpp-split Task 1, spec Sec4): the persist half's save_char() used
// to read world[ch->in_room].number directly -- the persist path's only
// direct world[] access. This function replaces that read so db.cpp/db_players
// never touches world[] itself; see db.h for the declaration.
int world_room_vnum(int room_index)
{
    return world[room_index].number;
}

// Registers world_room_vnum() (above) as persist_hooks.h's room-vnum hook
// (persist-split PS Task 4). Called once from run_the_game(), before
// boot_db() -- see persist_hooks.h.
void register_room_vnum_hook()
{
    rots::persist::set_room_vnum_hook(world_room_vnum);
}
