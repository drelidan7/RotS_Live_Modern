/* db_boot.cc */

// Renamed from db.cpp (Phase: db-split Task 2, spec Sec4a). Task 1 carved the
// WORLD half into db_world.cpp; this task carved the PERSIST half into
// db_players.cpp and the shared char/obj lifecycle helpers (used by BOTH the
// world loaders and the persist store paths) into entity_lifecycle.cpp. What
// remains here is the boot-orchestration (B) half: boot_db() and its startup
// helpers (reboot_wizlists/do_reload/reset_time/file_to_string[_alloc]/
// boot_crimes), plus the two live-game CAPTURE functions record_crime() and
// add_exploit_record() -- they walk world/combat state (world[]/combat_list)
// to *originate* a crime/exploit event and are callers of the db_players.cpp
// codecs, not part of the codec itself, so they stay here rather than making
// db_players.cpp depend on world/combat state. db.h still declares every
// symbol any TU calls across this split, so callers outside these files are
// unaffected.

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
 *  declarations of most of the 'global' variables                         *
 ************************************************************************ */

/* The global buffering system */
char buf[MAX_STRING_LENGTH];
char buf1[MAX_STRING_LENGTH];
char buf2[MAX_STRING_LENGTH];
char arg[MAX_STRING_LENGTH];

// `world`/`top_of_world` now live in db_world.cpp (Phase: db-split Task 1);
// re-declared locally here because boot_db()/record_crime() below still read
// `world` directly (see AGENTS.md's persist/world seam note).
extern struct room_data world;

// character_list/object_list now DEFINED in entity_lifecycle.cpp
// (world-seed Task 1, storage-placement only) -- they are lists OF
// entities, and that file already owns free_char()/free_obj(), the
// functions that unlink nodes from them. Neither is referenced
// elsewhere in this file, so no extern re-declaration is needed here.

// fight_messages[MAX_MESSAGES] storage-moved to fight.cpp (combat-pilot
// wave Task 4a), beside its loader load_messages(). See fight.cpp for
// the definition + ownership comment.


int no_mail = 0; /* mail disabled?		*/
// mini_mud now DEFINED in db_world.cpp (world-seed Task 1,
// storage-placement only). Not referenced elsewhere in this file, so
// no extern re-declaration is needed here.
int no_rent_check = 0; /* skip rent check on boot?	*/
time_t boot_time = 0; /* time of mud boot; time_t (not long) so &boot_time is a valid time_t* for
                         localtime() on Windows LLP64 -- Phase 3 Task 6 */
int restrict = 0; /* level of game restriction	*/
// boot_mode now DEFINED in db_world.cpp (world-seed Task 1,
// storage-placement only); boot_db() below still reads/writes it, so
// it keeps this extern.
extern int boot_mode; /* local var, to let know that reboot goes on */
char* credits = 0; /* game credits			*/
char* news = 0; /* mud news			*/
char* motd = 0; /* message of the day - mortals */
char* imotd = 0; /* message of the day - immorts */
char* help = 0; /* help screen			*/
char* info = 0; /* info page			*/
char* wizlist = 0; /* list of higher gods		*/
char* immlist = 0; /* list of peon gods		*/
char* background = 0; /* background story		*/
char* handbook = 0; /* handbook for new immortals	*/
char* policies = 0; /* policies page		*/
char* lastdeath = 0; /* policies page		*/
char* spell_tbl = 0; /* spells help			*/
char* power_tbl = 0; /* powers help			*/
char* skill_tbl = 0; /* skills help			*/
char* asima_tbl = 0; /* ASIMA help			*/
char* shape_tbl = 0; /* shape help			*/
char* msdp_tbl = 0; /* msdp help */

FILE* help_fl = 0; /* file for help text		*/
struct help_index_element* help_index = 0; /* the help table		*/
int top_of_helpt; /* top of help index table	*/

long beginning_of_time = 650336715;
// time_info now DEFINED in weather.cpp (world-seed Task 5,
// storage-placement only, mirroring the weather_info/pulse/top_idnum
// precedent); reset_time() below still writes the boot-time initial
// value, so it keeps this extern.
extern struct time_info_data time_info; /* the infomation about the time   */
// weather_info now DEFINED in weather.cpp (its single writer;
// world-seed Task 1, storage-placement only). utils.h already
// declares `extern struct weather_data weather_info;` globally, so no
// new extern is needed here.

struct char_data* waiting_list = 0; /*list of those with delayed commands*/
struct char_data* fast_update_list = 0; /* list for fast updating */
struct char_data* death_waiting_list = 0; /* list of those flagged to die... */

long judppwd; // password for JUDP IP registration
int judpavailable; // 1 if JUDP is available, 0 otherwise

/* local functions */
void index_boot(int mode);
void draw_map();
void initialiaze_small_map();
void assign_mobiles(void);
void assign_objects(void);
void assign_rooms(void);
void assign_the_shopkeepers(void);
void build_player_index(void);
void boot_mudlle();
void boot_crimes();
// file_to_string() itself has no callers left in this file (only
// file_to_string_alloc() called it, and both relocated together to
// db_players.cpp -- persist-split PS Task 4); file_to_string_alloc()'s
// declaration stays -- this file's boot-time text-file loads still call it.
int file_to_string_alloc(std::string_view name, char** buf);
void check_start_rooms(void);
void renum_world(void);
void reset_time(void);
void clear_char(struct char_data* ch, int mode);
void init_boards(void);
void initialize_buffers();
// void        add_follower(struct char_data *ch, struct char_data *leader);
void move_char_deleted(int index);

/* external functions */
extern struct descriptor_data* descriptor_list;
void load_messages(void);
void weather_and_time(int mode);
void assign_command_pointers(void);
void boot_social_messages(void);
void update_obj_file(void); /* In obj_files.cpp */
void sort_commands(void);
void load_banned(void);
// void	Read_Invalid_List(void);
struct help_index_element* build_help_index(FILE* fl, int* num, struct help_index_element** listpt);
void decrypt_line(unsigned char* line, int len);

extern struct skill_data skills[MAX_SKILLS];

extern struct help_index_summary help_content[];
extern int help_summary_length;

extern long race_affect[];
extern struct char_data* combat_list;

#define SAVEBUFLEN 3400


/*************************************************************************
 *  routines for booting the system                                       *
 *********************************************************************** */

/* thith is necessary for the autowiz system */
void reboot_wizlists(void)
{
    file_to_string_alloc(WIZLIST_FILE, &wizlist);
    file_to_string_alloc(IMMLIST_FILE, &immlist);
}

ACMD(do_reload)
{
    int i, tmp;

    one_argument(argument, arg);

    if (!str_cmp_nullable(arg, "all") || *arg == '*') {
        file_to_string_alloc(NEWS_FILE, &news);
        file_to_string_alloc(CREDITS_FILE, &credits);
        file_to_string_alloc(MOTD_FILE, &motd);
        file_to_string_alloc(IMOTD_FILE, &imotd);
        file_to_string_alloc(HELP_PAGE_FILE, &help);
        file_to_string_alloc(INFO_FILE, &info);
        file_to_string_alloc(WIZLIST_FILE, &wizlist);
        file_to_string_alloc(IMMLIST_FILE, &immlist);
        file_to_string_alloc(POLICIES_FILE, &policies);
        file_to_string_alloc(HANDBOOK_FILE, &handbook);
        file_to_string_alloc(BACKGROUND_FILE, &background);
        file_to_string_alloc(SPELL_FILE, &spell_tbl);
        file_to_string_alloc(POWER_FILE, &power_tbl);
        file_to_string_alloc(SKILLS_FILE, &skill_tbl);
        file_to_string_alloc(ASIMA_FILE, &asima_tbl);
        file_to_string_alloc(SHAPE_FILE, &shape_tbl);
        file_to_string_alloc(MSDP_FILE, &msdp_tbl);
    } else if (!str_cmp_nullable(arg, "wizlist"))
        file_to_string_alloc(WIZLIST_FILE, &wizlist);
    else if (!str_cmp_nullable(arg, "immlist"))
        file_to_string_alloc(IMMLIST_FILE, &immlist);
    else if (!str_cmp_nullable(arg, "news"))
        file_to_string_alloc(NEWS_FILE, &news);
    else if (!str_cmp_nullable(arg, "credits"))
        file_to_string_alloc(CREDITS_FILE, &credits);
    else if (!str_cmp_nullable(arg, "motd"))
        file_to_string_alloc(MOTD_FILE, &motd);
    else if (!str_cmp_nullable(arg, "imotd"))
        file_to_string_alloc(IMOTD_FILE, &imotd);
    else if (!str_cmp_nullable(arg, "help"))
        file_to_string_alloc(HELP_PAGE_FILE, &help);
    else if (!str_cmp_nullable(arg, "info"))
        file_to_string_alloc(INFO_FILE, &info);
    else if (!str_cmp_nullable(arg, "policy"))
        file_to_string_alloc(POLICIES_FILE, &policies);
    else if (!str_cmp_nullable(arg, "handbook"))
        file_to_string_alloc(HANDBOOK_FILE, &handbook);
    else if (!str_cmp_nullable(arg, "background"))
        file_to_string_alloc(BACKGROUND_FILE, &background);
    else if (!str_cmp_nullable(arg, "spel_tbl"))
        file_to_string_alloc(SPELL_FILE, &spell_tbl);
    else if (!str_cmp_nullable(arg, "pray_tbl"))
        file_to_string_alloc(POWER_FILE, &power_tbl);
    else if (!str_cmp_nullable(arg, "skil_tbl"))
        file_to_string_alloc(SKILLS_FILE, &skill_tbl);
    else if (!str_cmp_nullable(arg, "mudl_tbl"))
        file_to_string_alloc(ASIMA_FILE, &asima_tbl);
    else if (!str_cmp_nullable(arg, "shap_tbl"))
        file_to_string_alloc(SHAPE_FILE, &shape_tbl);
    else if (!str_cmp_nullable(arg, "msdp_tbl"))
        file_to_string_alloc(MSDP_FILE, &msdp_tbl);
    else if (!str_cmp_nullable(arg, "xhelp")) {
        for (tmp = 0; tmp < help_summary_length; tmp++) {
            if (help_content[tmp].file)
                fclose(help_content[tmp].file);

            if (!(help_content[tmp].file = fopen(help_content[tmp].filename, "r")))
                return;
            else {
                for (i = 0; i < help_content[tmp].top_of_helpt; i++)
                    RELEASE(help_content[tmp].index[i].keyword);
                RELEASE(help_content[tmp].index);
                build_help_index(help_content[tmp].file, &(help_content[tmp].top_of_helpt),
                    &(help_content[tmp].index));
            }
        }
    } else {
        send_to_char("Unknown reload option.\n\r", ch);
        return;
    }

    send_to_char("Okay.\n\r", ch);
}

/* body of the booting system */
void boot_db(void)
{
    int i, tmp;
    extern int no_specials;
    FILE* f;

    log("Boot db -- BEGIN.");
    boot_mode = 1;

    // Enable the account-resolution cache for the live server (it stays OFF in the test binary,
    // which never calls boot_db). read_account_file / find_linked_character_owner_account now
    // memoize their O(N) directory scans, with a full flush on every account.json write
    // (write_account_file). See account_cache.h. This is the adopted Phase-1 optimization; JSON
    // serialize/deserialize stay on v1.
    account_cache::set_enabled(true);
    log("Account-resolution cache: enabled.");

    log("Resetting the game time:");
    reset_time();
    log("Allocating the primary memory.");
    initialize_buffers();

    log("Reading news, credits, help, bground, info & motds.");
    file_to_string_alloc(NEWS_FILE, &news);
    file_to_string_alloc(CREDITS_FILE, &credits);
    file_to_string_alloc(MOTD_FILE, &motd);
    file_to_string_alloc(IMOTD_FILE, &imotd);
    file_to_string_alloc(HELP_PAGE_FILE, &help);
    file_to_string_alloc(INFO_FILE, &info);
    file_to_string_alloc(WIZLIST_FILE, &wizlist);
    file_to_string_alloc(IMMLIST_FILE, &immlist);
    file_to_string_alloc(POLICIES_FILE, &policies);
    file_to_string_alloc(HANDBOOK_FILE, &handbook);
    file_to_string_alloc(BACKGROUND_FILE, &background);
    file_to_string_alloc(LASTDEATH_FILE, &lastdeath);
    file_to_string_alloc(MSDP_FILE, &msdp_tbl);

    log("Opening help files.");
    for (tmp = 0; tmp < help_summary_length; tmp++) {
        //       log(help_content[tmp].filename);
        if (!(help_content[tmp].file = fopen(help_content[tmp].filename, "r")))
            log("   Could not open help file.");
        else {
            build_help_index(help_content[tmp].file, &(help_content[tmp].top_of_helpt),
                &(help_content[tmp].index));
            log(std::format("Chapter {}, {} entries.", help_content[tmp].keyword,
                help_content[tmp].top_of_helpt)
                    );
        }
    }
    log("Loading script table.");
    index_boot(DB_BOOT_SCR);

    log("Loading zone table.");
    index_boot(DB_BOOT_ZON);

    log("Drawing map");
    draw_map();

    /* Ingolemo small map addition */
    log("Drawing small map");
    initialiaze_small_map();

    log("Loading mudlle programs.");
    index_boot(DB_BOOT_MDL);
    log("Converting mudlle programs.");
    boot_mudlle();

    log("Loading rooms.");
    index_boot(DB_BOOT_WLD);

    log("Renumbering rooms.");
    renum_world();

    log("Checking start rooms.");
    check_start_rooms();

    log("Loading mobs and generating index.");
    index_boot(DB_BOOT_MOB);

    log("Loading objs and generating index.");
    index_boot(DB_BOOT_OBJ);

    log("Renumbering zone table.");
    renum_zone_table();

    log("Generating player index.");
    build_player_index();

    log("Loading pkill list.");
    boot_pkills();

    log("Loading crime list.");
    boot_crimes();

    log("Loading fight messages.");
    load_messages();

    log("Loading social messages.");
    boot_social_messages();

    if (!no_specials) {
        log("Loading shops.");
        index_boot(DB_BOOT_SHP);
    }

    log("   Commands.");
    assign_spell_pointers();
    // combat_hooks.h's boot-registered command-dispatch table (blocker-
    // buster wave Task 2) -- same "assign_*, before assign_command_pointers()"
    // slot as assign_spell_pointers() above; no ageland call site dispatches
    // through it yet (see combat_hooks.h), so ordering relative to
    // assign_command_pointers() below is not behavior-load-bearing this
    // wave -- placed here purely to keep every boot-time table populated in
    // one place.
    register_combat_command_dispatch();
    assign_command_pointers();

    log("Sorting command list.");
    sort_commands();

    log("Booting mail system.");
    if (!scan_file()) {
        log("    Mail boot failed -- Mail system disabled");
        no_mail = 1;
    }

    log("Loading boards and mail.");
    init_boards();

    log("Reading banned site and invalid-name list.");
    load_banned();

    if (!no_rent_check) {
        log("Deleting timed-out crash and rent files:");
        update_obj_file();
        log("Done.");
    }

    // Fixed-bug (Phase 5 T6, UBSan reference-binding-to-null-pointer):
    // big_brother::create()/skill_timer::create() used to run at the very
    // end of this function (see the comment near "Boot db -- DONE." below,
    // which explains why the calls moved), well AFTER this reset_zone()
    // loop -- but reset_zone() (spawning/positioning mobiles and running zone
    // commands/scripts for a fresh boot) can reach code that calls
    // game_rules::big_brother::instance()/game_timer::skill_timer::instance()
    // (confirmed live: a real Mirkwood zone hit this during a real
    // world-data boot under UBSan), which dereferences a still-null
    // singleton pointer -- undefined behavior in every boot that reaches
    // that path, sanitized or not. Both singletons only need weather_info
    // (a file-scope global, always valid) and `world` (populated by
    // renum_world() above, well before this loop), so constructing them
    // here instead is safe and satisfies every caller reset_zone() can
    // reach.
    game_rules::big_brother::create(weather_info, &world);
    game_timer::skill_timer::create(weather_info, &world);

    for (i = 0; i <= top_of_zone_table; i++) {
        vmudlog(NRM, "Resetting %s (rooms %d-%d).", zone_table[i].name,
            i ? (zone_table[i - 1].top + 1) : 0, zone_table[i].top);
        reset_zone(i);
    }

    log("Assigning function pointers:");

    if (!no_specials) {
        log("   Shopkeepers.");
        assign_the_shopkeepers();
        log("   Mobiles.");
        assign_mobiles();
        log("   Objects.");
        assign_objects();
        log("   Rooms.");
        assign_rooms();
    }

    boot_time = time(0);

    log("Recounting zone powers.");
    recalc_zone_power();

    log("Obtaining JUDP password.");
    if (!(f = fopen("../judp/password", "rb"))) {
        log("Could not open /judp/password, disabling JUDP.");
        judpavailable = 0;
    } else {
        fscanf(f, "%ld", &judppwd);
        judpavailable = 1;
        if (ferror(f)) {
            log("Could not read /judp/password, disabling JUDP.");
            judpavailable = 0;
        }
        fclose(f);
    }

    log("Boot db -- DONE.");
    boot_mode = 0;

    // Big Brother/skill_timer are now constructed earlier, right before the
    // zone-reset loop above (Phase 5 T6 fix) -- reset_zone() can reach code
    // that needs them mid-loop, so constructing them only after the loop
    // finished was too late. create()'s storage is a function-local static,
    // so this is intentionally left uncalled here rather than removed
    // outright: nothing about this comment block is load-bearing, but a
    // future reader diffing db.cpp against upstream should see exactly where
    // the call used to live and why it moved.
}

/* reset the time in the game from file */
void reset_time(void)
{

    void initialize_weather();

    time_info = mud_time_passed(time(0), beginning_of_time);
    initialize_weather();
}

// file_to_string()/file_to_string_alloc() relocated to db_players.cpp
// (persist-split PS Task 4, controller-adjudicated relocation): pure
// filesystem I/O, no comm/world/combat access, and load_player()
// (db_players.cpp) is their primary consumer. This file's many boot-time
// text-file loads (wizlist/motd/help/etc., below and above) keep calling
// file_to_string_alloc() through the local forward declaration a few lines
// up, now resolving down into rots_persist.


//*************************************************************************
//*************************** Crime functions *****************************
//*************************************************************************

// void forget_crimes(char_data *ch, int criminal);
void read_crime_file();

void boot_crimes() { read_crime_file(); }

// Forward declaration for add_exploit_record()'s write_exploits() calls
// below -- write_exploits() now lives in db_players.cpp (db-split Task 2);
// not declared in db.h, so re-declared here at its point of need (same
// pattern as e.g. objsave.cpp/act_wiz.cpp's function-local extern for
// rename_char()).
void write_exploits(char_data* ch, exploit_record* record);

void record_crime(char_data* criminal, char_data* victim, int crime, int wit_type)
{
    struct char_data* tmpchar;

    if (IS_NPC(victim) || (GET_LEVEL(victim) >= LEVEL_IMMORT) || (IS_NPC(criminal)))
        return;
    for (tmpchar = world[victim->in_room].people; tmpchar; tmpchar = tmpchar->next_in_room) {
        if ((tmpchar == criminal) || (IS_NPC(tmpchar)) || (GET_LEVEL(tmpchar) >= LEVEL_IMMORT))
            continue;
        add_crime(criminal->specials2.idnum, victim->specials2.idnum, tmpchar->specials2.idnum,
            crime, wit_type);
    }
    return;
}

void add_exploit_record(int recordtype, char_data* victim, int iIntParam, const char* chParam)
{
    struct char_data* killer;
    struct exploit_record exploitrec;
    int iFirstDeath = 0;
    // time_t (not long) for localtime() on Windows LLP64 -- Phase 3 Task 6.
    time_t ct;
    char* tmstr;

    if (IS_NPC(victim) || (GET_LEVEL(victim) >= LEVEL_IMMORT))
        return;

    /* get time as a string */
    ct = time(0);
    tmstr = (char*)asctime(localtime(&ct));
    *(tmstr + strlen(tmstr) - 1) = '\0';
    strcpy(exploitrec.chtime, std::format("{}", tmstr).c_str());

    // It's a PK record
    switch (recordtype) {
    case EXPLOIT_PK: {
        std::set<char_data*> seen_chars;
        for (killer = combat_list; killer; killer = killer->next_fighting) {
            if (killer->specials.fighting == victim) {
                char_data* cur_killer = killer;
                if (IS_NPC(killer)) {
                    if (killer->master && (MOB_FLAGGED(killer, MOB_PET) || MOB_FLAGGED(killer, MOB_ORC_FRIEND))) {
                        cur_killer = killer->master;
                    }
                }

                // If we have a killer and he's unique, add it to the exploits.
                if (cur_killer && !IS_NPC(cur_killer) && seen_chars.insert(cur_killer).second) {
                    // only trophies for chars
                    // CREATE A TROPHY RECORD
                    exploitrec.type = EXPLOIT_PK;
                    strcpy(exploitrec.chtime, std::format("{}", tmstr).c_str());
                    exploitrec.shintVictimID = GET_IDNUM(victim);
                    strcpy(exploitrec.chVictimName, std::format("{}", GET_NAME(victim)).c_str());
                    exploitrec.iVictimLevel = GET_LEVEL(victim);
                    exploitrec.iKillerLevel = GET_LEVEL(cur_killer);

                    // player to write to, structure
                    write_exploits(cur_killer, &exploitrec);
                }
            }
        }
    } break;

    case EXPLOIT_DEATH: {
        std::set<char_data*> seen_chars;
        for (killer = combat_list; killer; killer = killer->next_fighting) {
            if (killer->specials.fighting == victim) {
                char_data* cur_killer = killer;
                if (IS_NPC(killer)) {
                    if (killer->master && (MOB_FLAGGED(killer, MOB_PET) || MOB_FLAGGED(killer, MOB_ORC_FRIEND))) {
                        cur_killer = killer->master;
                    }
                }

                // If we have a killer and he's unique, add it to the exploits.
                if (cur_killer && !IS_NPC(cur_killer) && seen_chars.insert(cur_killer).second) {
                    // only trophies for chars
                    exploitrec.type = EXPLOIT_DEATH;
                    exploitrec.shintVictimID = GET_IDNUM(cur_killer);
                    // killed by..
                    strcpy(exploitrec.chVictimName,
                        std::format("{}", GET_NAME(cur_killer)).c_str());
                    exploitrec.iVictimLevel = GET_LEVEL(victim);
                    exploitrec.iKillerLevel = GET_LEVEL(cur_killer);
                    // used to indicate separators between subsequent deaths.
                    if (iFirstDeath == 0) {
                        exploitrec.iIntParam = 1;
                        iFirstDeath++;
                    } else {
                        exploitrec.iIntParam = 0;
                    }

                    // player to write to, structure
                    write_exploits(victim, &exploitrec);
                }
            }
        }
    } break;

    case EXPLOIT_LEVEL:
        exploitrec.iIntParam = iIntParam;
        exploitrec.type = EXPLOIT_LEVEL;
        write_exploits(victim, &exploitrec);
        break;

    case EXPLOIT_BIRTH:
        exploitrec.type = EXPLOIT_BIRTH;
        write_exploits(victim, &exploitrec);
        break;

    case EXPLOIT_STAT:
        exploitrec.type = EXPLOIT_STAT;
        strcpy(exploitrec.chVictimName, std::format("{}", chParam).c_str());
        exploitrec.iIntParam = iIntParam;
        write_exploits(victim, &exploitrec);
        break;

    case EXPLOIT_MOBDEATH:
        exploitrec.type = EXPLOIT_MOBDEATH;
        strcpy(exploitrec.chVictimName, std::format("{}", chParam).c_str());
        exploitrec.iVictimLevel = GET_LEVEL(victim);
        exploitrec.iIntParam = iIntParam;
        write_exploits(victim, &exploitrec);
        break;

    case EXPLOIT_RETIRED:
        exploitrec.type = EXPLOIT_RETIRED;
        write_exploits(victim, &exploitrec);
        break;

    case EXPLOIT_ACHIEVEMENT:
        exploitrec.type = EXPLOIT_ACHIEVEMENT;
        strcpy(exploitrec.chVictimName, std::format("{}", chParam).c_str());
        write_exploits(victim, &exploitrec);
        break;

    case EXPLOIT_NOTE:
        exploitrec.type = EXPLOIT_NOTE;
        strcpy(exploitrec.chVictimName, std::format("{}", chParam).c_str());
        write_exploits(victim, &exploitrec);
        break;

    case EXPLOIT_POISON:
        exploitrec.type = EXPLOIT_POISON;
        write_exploits(victim, &exploitrec);
        break;

    case EXPLOIT_REGEN_DEATH:
        exploitrec.type = EXPLOIT_REGEN_DEATH;
        write_exploits(victim, &exploitrec);
        break;
    }
    return;
}

// Registers add_exploit_record() (above) as persist_hooks.h's
// exploit-capture hook (persist-split PS Task 4). Called once from
// run_the_game(), before boot_db() -- see persist_hooks.h.
void register_exploit_capture_hook()
{
    rots::persist::set_exploit_capture_hook(add_exploit_record);
}
