/* db.cc */

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
#include "handler.h"
#include "interpre.h"
#include "limits.h"
#include "mail.h"
#include "mudlle.h"
#include "pkill.h"
#include "platform_compat.h"
#include "protos.h"
#include "spells.h"
#include "structs.h"
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

room_data* room_data::BASE_WORLD = 0;
int room_data::BASE_LENGTH = 0;
int room_data::TOTAL_LENGTH = 0;
room_data_extension* room_data::BASE_EXTENSION = 0;

struct room_data world; // = 0;  new room_data; /* class of rooms      	*/
int top_of_world = 0; /* ref to the top element of world	*/

struct char_data* character_list = 0; /* global linked list of chars	*/
struct index_data* mob_index; /* index table for mobile file	*/
struct char_data* mob_proto; /* prototypes for mobs		*/
int top_of_mobt = 0; /* top of mobile index table	*/

struct obj_data* object_list = 0; /* the global linked list of objs	*/
struct index_data* obj_index; /* index table for object file	*/
struct obj_data* obj_proto; /* prototypes for objs		*/
int top_of_objt = 0; /* top of object index table	*/

struct message_list fight_messages[MAX_MESSAGES]; /* fighting messages	*/

struct script_head* script_table = 0;
int top_of_script_table = 0;

extern const std::string_view mobile_program_base[];
char** mobile_program;
int* mobile_program_zone;
int num_of_programs;

struct player_index_element* player_table = 0; /* index to player file	*/
FILE* player_fl = 0; /* file desc of player file	*/
int top_of_p_table = 0; /* ref to top of table		*/
int top_of_p_file = 0; /* ref of size of p file	*/
long top_idnum = 0; /* highest idnum in use		*/

struct crime_record_type* crime_record = 0;
FILE* crime_file = 0;
int num_of_crimes = 0;

int no_mail = 0; /* mail disabled?		*/
int mini_mud = 0; /* mini-mud mode?		*/
int new_mud = 0;
int no_rent_check = 0; /* skip rent check on boot?	*/
time_t boot_time = 0; /* time of mud boot; time_t (not long) so &boot_time is a valid time_t* for
                         localtime() on Windows LLP64 -- Phase 3 Task 6 */
int restrict = 0; /* level of game restriction	*/
int boot_mode = 0; /* local var, to let know that reboot goes on */
extern int r_mortal_start_room[]; /* rnum of mortal start room	*/
extern int r_mortal_idle_room[]; /* rnum of mortal idle room	*/
int r_immort_start_room; /* rnum of immort start room	*/
int r_frozen_start_room; /* rnum of frozen start room	*/
int r_retirement_home_room; /* rnum of retirement home      */
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
struct time_info_data time_info; /* the infomation about the time   */
struct weather_data weather_info; /* the infomation about the weather */

struct char_data* waiting_list = 0; /*list of those with delayed commands*/
struct char_data* fast_update_list = 0; /* list for fast updating */
struct char_data* death_waiting_list = 0; /* list of those flagged to die... */

char world_map[WORLD_AREA + 1];
char small_map[2 * SMALL_WORLD_RADIUS + 3]
              [4 * SMALL_WORLD_RADIUS + 7]; // Ingolemo small_map addition

long judppwd; // password for JUDP IP registration
int judpavailable; // 1 if JUDP is available, 0 otherwise

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
void assign_mobiles(void);
void assign_objects(void);
void assign_rooms(void);
void assign_the_shopkeepers(void);
void build_player_index(void);
void boot_mudlle();
void boot_crimes();
int file_to_string(std::string_view name, char* buf);
int file_to_string_alloc(std::string_view name, char** buf);
void check_start_rooms(void);
void renum_world(void);
void reset_time(void);
void clear_char(struct char_data* ch, int mode);
void init_boards(void);
void initialize_buffers();
// void        add_follower(struct char_data *ch, struct char_data *leader);
char* fread_line(FILE* fp);
void move_char_deleted(int index);

/* external functions */
extern struct descriptor_data* descriptor_list;
void load_messages(void);
void weather_and_time(int mode);
void assign_command_pointers(void);
// void	assign_spell_pointers(void);
void boot_social_messages(void);
void update_obj_file(void); /* In objsave.c */
void sort_commands(void);
void load_banned(void);
// void	Read_Invalid_List(void);
struct help_index_element* build_help_index(FILE* fl, int* num, struct help_index_element** listpt);
void decrypt_line(unsigned char* line, int len);

extern struct skill_data skills[MAX_SKILLS];
extern byte language_number;
extern byte language_skills[];

extern struct help_index_summary help_content[];
extern int help_summary_length;

extern long race_affect[];
extern struct char_data* combat_list;

extern universal_list* affected_list;
extern universal_list* affected_list_pool;

#define SAVEBUFLEN 3400

// +1 (and the explicit terminator below): encrypt_line() fills all MAX_PWD_LENGTH
// bytes with encrypted data, leaving no room for a NUL -- the write_player_text()
// call below then reads this buffer with fprintf's "%s", which scans past the
// 10-byte allocation for a terminator that was never there. In a non-ASan build
// this reads whatever adjacent global happens to hold a zero byte (in practice,
// silently landing on a null combat_list/combat_next_dude pointer's low byte);
// under ASan it's a global-buffer-overflow read (caught by the Wave 4 D2
// characterization suite's mandatory ASan pass -- see db_save_roundtrip_tests.cpp).
// Pre-existing bug, unrelated to and untouched by the sprintf/strcpy->std::format
// conversion in the surrounding functions.
unsigned char pwdcrypt[MAX_PWD_LENGTH + 1];

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
char* mudlle_converter(char*);
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

void inc_p_table(void)
{
    struct player_index_element* tmpel;

    CREATE(tmpel, struct player_index_element, top_of_p_table + 2);
    if (!tmpel) {
        perror("inc_p_table");
        exit(1);
    }
    memcpy(tmpel, player_table, (top_of_p_table + 1) * sizeof(player_index_element));

    RELEASE(player_table);
    player_table = tmpel;
    top_of_p_table++;
}

namespace {

int find_player_table_index_by_name(std::string_view name)
{
    name = rots::text::truncate_at_null(name);
    if (name.empty())
        return -1;

    for (int index = 0; index <= top_of_p_table; ++index) {
        if (player_table[index].name && str_cmp(player_table[index].name, name) == 0)
            return index;
    }

    return -1;
}

[[noreturn]] void fail_duplicate_player_index_entry(std::string_view name, std::string_view source_a,
    std::string_view source_b)
{
    name = rots::text::truncate_at_null(name);
    source_a = rots::text::truncate_at_null(source_a);
    source_b = rots::text::truncate_at_null(source_b);
    log(std::format("Duplicate character '{}' found in both {} and {} while building player_table.",
        name.empty() ? "(null)" : name, source_a.empty() ? "unknown source" : source_a,
        source_b.empty() ? "unknown source" : source_b)
            );
    exit(1);
}

bool read_text_file_contents(std::string_view path, std::string* contents)
{
    if (contents == nullptr)
        return false;

    const std::string path_owner(rots::text::truncate_at_null(path));
    FILE* file = std::fopen(path_owner.c_str(), "rb");
    if (file == nullptr)
        return false;

    std::string text;
    char buffer[1024];
    while (true) {
        const size_t bytes_read = std::fread(buffer, sizeof(char), sizeof(buffer), file);
        if (bytes_read > 0)
            text.append(buffer, bytes_read);

        if (bytes_read < sizeof(buffer)) {
            if (std::ferror(file)) {
                std::fclose(file);
                return false;
            }
            break;
        }
    }

    std::fclose(file);
    *contents = std::move(text);
    return true;
}

bool has_suffix(std::string_view value, std::string_view suffix)
{
    value = rots::text::truncate_at_null(value);
    suffix = rots::text::truncate_at_null(suffix);
    return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool is_versioned_legacy_player_entry_name(std::string_view entry_name, std::string_view normalized_name)
{
    entry_name = rots::text::truncate_at_null(entry_name);
    normalized_name = rots::text::truncate_at_null(normalized_name);

    const size_t normalized_length = normalized_name.size();
    if (!entry_name.starts_with(normalized_name) || entry_name.size() <= normalized_length || entry_name[normalized_length] != '.')
        return false;

    std::string_view suffix = entry_name.substr(normalized_length + 1);
    for (int field_index = 0; field_index < 5; ++field_index) {
        if (suffix.empty())
            return false;

        while (!suffix.empty() && suffix.front() != '.') {
            if (!std::isdigit(static_cast<unsigned char>(suffix.front())))
                return false;
            suffix.remove_prefix(1);
        }

        if (field_index < 4) {
            if (suffix.empty() || suffix.front() != '.')
                return false;
            suffix.remove_prefix(1);
        }
    }

    return suffix.empty();
}

bool directory_has_versioned_legacy_player_entry(std::string_view directory_path,
    std::string_view normalized_name)
{
    const std::string directory_path_owner(rots::text::truncate_at_null(directory_path));
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::directory_iterator it(directory_path_owner, ec);
    if (ec)
        return false;

    const fs::directory_iterator end;
    for (; it != end; it.increment(ec)) {
        const std::string entry_name = it->path().filename().string();
        if (!entry_name.empty() && entry_name[0] != '.' && is_versioned_legacy_player_entry_name(entry_name, normalized_name))
            return true;
    }

    return false;
}

void populate_player_index_entry_from_store(const char_file_u& stored_character,
    std::string_view character_path)
{
    if (find_player_table_index_by_name(stored_character.name) >= 0)
        fail_duplicate_player_index_entry(stored_character.name, "legacy player index",
            character_path);

    char_file_u indexed_character = stored_character;
    std::string error_message;
    if (!update_player_index_entry_from_store(&indexed_character, character_path,
            &error_message)) {
        log(std::format("Failed to add account-native character {} to the player index: {}",
            static_cast<const char*>(stored_character.name), error_message)
                );
        exit(1);
    }

    top_idnum = MAX(top_idnum, indexed_character.specials2.idnum);
}

void build_account_native_player_index(void)
{
    namespace fs = std::filesystem;
    std::error_code accounts_ec;
    fs::directory_iterator accounts_it("accounts", accounts_ec);
    if (accounts_ec)
        return;

    const fs::directory_iterator dir_end;
    for (; accounts_it != dir_end; accounts_it.increment(accounts_ec)) {
        const std::string bucket_name = accounts_it->path().filename().string();
        if (bucket_name.empty() || bucket_name[0] == '.') {
            continue;
        }

        const std::string bucket_path = std::string("accounts/") + bucket_name;
        std::error_code bucket_ec;
        fs::directory_iterator bucket_it(bucket_path, bucket_ec);
        if (bucket_ec) {
            continue;
        }

        for (; bucket_it != dir_end; bucket_it.increment(bucket_ec)) {
            const std::string account_entry_name = bucket_it->path().filename().string();
            if (account_entry_name.empty() || account_entry_name[0] == '.') {
                continue;
            }

            const std::string account_json_path = bucket_path + "/" + account_entry_name + "/account.json";
            std::string account_json_text;
            if (!read_text_file_contents(account_json_path, &account_json_text)) {
                continue;
            }

            account::AccountData account_data;
            std::string error_message;
            if (!account::deserialize_account_from_json(account_json_text, &account_data,
                    &error_message)) {
                log(std::format("Failed to read account-native index source '{}': {}",
                    account_json_path, error_message)
                        );
                exit(1);
            }

            if (account::normalize_email(account_data.normalized_email) != account_entry_name) {
                log(std::format(
                    "Account-native index source '{}' has mismatched normalized email '{}'.",
                    account_json_path, account_data.normalized_email)
                        );
                exit(1);
            }

            for (const std::string& character_name : account_data.characters) {
                const std::string character_path = account::account_character_player_path(
                    ".", account_data.account_name, character_name);

                char_file_u stored_character { };
                if (!account::read_account_character_file(".", account_data.account_name,
                        character_name, &stored_character,
                        &error_message)) {
                    const std::string read_error = error_message;
                    bool account_character_exists = false;
                    std::string inspect_error;
                    if (!account::inspect_account_character_file(
                            ".", account_data.account_name, character_name,
                            &account_character_exists, &inspect_error)) {
                        log(std::format("Failed to inspect account-native character file '{}': {}",
                            character_path, inspect_error)
                                );
                        exit(1);
                    }

                    if (!account_character_exists)
                        continue;

                    log(std::format("Failed to read account-native character file '{}': {}",
                        character_path, read_error)
                            );
                    exit(1);
                }

                populate_player_index_entry_from_store(stored_character, character_path);
            }
        }
    }
}

int load_player_from_account_json_path(char* name, std::string_view player_path,
    struct char_file_u* char_element)
{
    player_path = rots::text::truncate_at_null(player_path);
    if (player_path.empty()) {
        log(std::format("Couldn't find account-native character file path for {}\n", name));
        return -1;
    }

    std::string json_text;
    if (!read_text_file_contents(player_path, &json_text)) {
        log(std::format("Couldn't read account-native character file for {} from {}\n", name,
            player_path)
                );
        return -1;
    }

    character_json::CharacterData character_data;
    std::string error_message;
    if (!character_json::deserialize_character_from_json(json_text, &character_data,
            &error_message)
        || !character_json::apply_character_data_to_store(character_data, char_element,
            &error_message)) {
        log(std::format("Couldn't parse account-native character file for {} from {}: {}\n", name,
            player_path, error_message)
                );
        return -1;
    }

    const int player_index = find_player_table_index_by_name(name);
    if (player_index < 0) {
        log(std::format("load_player: account-native player {} not in player_table", name));
        return -1;
    }

    char_element->player_index = player_index;
    return 1;
}

std::string legacy_player_path(
    std::string_view directory_path, std::string_view entry_name)
{
    const std::string directory_path_owner(
        rots::text::truncate_at_null(directory_path));
    entry_name = rots::text::truncate_at_null(entry_name);
    return std::format("{}{}", directory_path_owner, entry_name);
}

} // namespace

#ifdef TESTING
std::string legacy_player_path_for_testing(
    std::string_view directory_path, std::string_view entry_name)
{
    return legacy_player_path(directory_path, entry_name);
}
#endif

//  Reads a field from the player filename format (using FAT as index)

int read_filename_field(int pos, char* field, char* fname)
{
    int field_pos;

    memset(field, 0, 99);
    field_pos = 0;
    while ((pos < 79) && /* (fname[pos] != 0) && */ (fname[pos] != '.')) {
        field[field_pos] = fname[pos];
        field_pos++;
        pos++;
    }
    field[MAX(79, field_pos)] = '\n';
    return pos;
}

/* New index build for the new player files */
void build_directory(std::string_view directory_path)
{
    const std::string directory_path_owner(rots::text::truncate_at_null(directory_path));
    namespace fs = std::filesystem;
    char* tmpch;
    int i;

    // read_filename_field's terminator check is commented out (see above), so
    // it will scan past a plain '\0' looking for '.' up to pos 79 -- copy the
    // scanned name into a 256-byte zeroed buffer (matching the historical
    // dirent::d_name array's size/zero-padding) so that over-scan reads
    // trailing zero bytes instead of running off a short std::string buffer.
    char entry_name_buf[256];

    CREATE(tmpch, char, 100);

    std::error_code ec;
    fs::directory_iterator it(directory_path_owner, ec);
    const fs::directory_iterator end;

    for (; it != end; it.increment(ec)) {
        memset(entry_name_buf, 0, sizeof(entry_name_buf));
        const std::string entry_name = it->path().filename().string();
        strncpy(entry_name_buf, entry_name.c_str(), sizeof(entry_name_buf) - 1);

        if (entry_name_buf[0] == '.' || !strncmp(entry_name_buf, "CVS", 3)) {
            continue;
        }

        i = read_filename_field(0, tmpch, entry_name_buf);
        tmpch[i] = 0;
        if (!is_versioned_legacy_player_entry_name(entry_name_buf, tmpch) && directory_has_versioned_legacy_player_entry(directory_path_owner, tmpch)) {
            continue;
        }
        if (find_player_table_index_by_name(tmpch) >= 0)
            fail_duplicate_player_index_entry(tmpch, "legacy player index", entry_name_buf);
        create_entry(tmpch);

        i = read_filename_field(i + 1, tmpch, entry_name_buf);
        player_table[top_of_p_table].level = atoi(tmpch);

        i = read_filename_field(i + 1, tmpch, entry_name_buf);
        player_table[top_of_p_table].race = atoi(tmpch);

        i = read_filename_field(i + 1, tmpch, entry_name_buf);
        player_table[top_of_p_table].idnum = atoi(tmpch);

        i = read_filename_field(i + 1, tmpch, entry_name_buf);
        player_table[top_of_p_table].log_time = atoi(tmpch);

        i = read_filename_field(i + 1, tmpch, entry_name_buf);
        player_table[top_of_p_table].flags = atoi(tmpch);

        strcpy(player_table[top_of_p_table].ch_file,
            legacy_player_path(directory_path_owner, entry_name_buf).c_str());

        top_idnum = MAX(top_idnum, player_table[top_of_p_table].idnum);
    } // for (; it != end; it.increment(ec))

    RELEASE(tmpch);
}

void build_player_index(void)
{
    int nr, tt;

    top_of_p_file = top_of_p_table = -1;
    top_idnum = 1;

    build_directory("players/A-E/");
    build_directory("players/F-J/");
    build_directory("players/K-O/");
    build_directory("players/P-T/");
    build_directory("players/U-Z/");
    build_account_native_player_index();

    top_of_p_file = top_of_p_table;

    tt = time(0);

    // Automatic deletion of inactive low-level characters at boot is DISABLED.
    // Set to true to restore the original pruning (sub-level-20 chars inactive
    // for > level*7 days had a ~51% chance of deletion on each boot). Kept off
    // so imported/legacy characters are never auto-removed.
    const bool enable_auto_delete = false;

    for (nr = 0; nr <= top_of_p_table; nr++) {
        if (enable_auto_delete && player_table[nr].level < 20 && (!IS_SET(player_table[nr].flags, PLR_DELETED)) && (!IS_SET(player_table[nr].flags, PLR_RETIRED)) && ((tt - player_table[nr].log_time) > SECS_PER_REAL_DAY * player_table[nr].level * 7) && (number(0, 100) < 51)) {
            log(std::format("Mud auto-deleted char {}.", player_table[nr].name));
            Crash_delete_file(player_table[nr].name);
            delete_exploits_file(player_table[nr].name);
            move_char_deleted(nr);
        }
        if (strlen(player_table[nr].name) > 12)
            vmudlog(BRF, "%s, len=%d", player_table[nr].name, strlen(player_table[nr].name));
    }
}

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

/*************************************************************************
 *  stuff related to the save/load player system *
 *********************************************************************** */

// New load system (Fingolfin) under construction

#define KEY_INT(the_field, element) \
    if (!strcmp(line, the_field)) { \
        tmp1 = atoi(value);         \
        element = tmp1;             \
        break;                      \
    }

#define KEY_STR(the_field, element, length)                   \
    if (!strcmp(line, the_field)) {                           \
        memcpy(element, value, length);                       \
        for (ctmp = element; ctmp < element + length; ctmp++) \
            if (*ctmp == '\n')                                \
                *ctmp = '\0';                                 \
        break;                                                \
    }

#define KEY_LONG_STR(the_field, element, length)                                                   \
    if (!strcmp(line, the_field)) {                                                                \
        for (tmp1 = 0; position < input_end && *position != '~' && tmp1 < (length - 1);            \
            position++, tmp1++)                                                                    \
            element[tmp1] = *position;                                                             \
        if (position >= input_end || *position != '~') {                                           \
            log(std::format("load_player_from_text: malformed long string for {}", name)); \
            return -1;                                                                             \
        }                                                                                          \
        element[tmp1] = '\0';                                                                      \
        position++;                                                                                \
        while (position < input_end && (*position == '\r' || *position == '\n'))                   \
            position++;                                                                            \
        break;                                                                                     \
    }

#define KEY_AFF(the_field)                                                            \
    if (!strcmp(line, the_field)) {                                                   \
        sscanf(value, "%d %d %d %d %d %d", &tmp1, &tmp2, &tmp3, &tmp4, &tmp5, &tmp6); \
        char_element->affected[tmp1].type = tmp2;                                     \
        char_element->affected[tmp1].duration = tmp3;                                 \
        char_element->affected[tmp1].modifier = tmp4;                                 \
        char_element->affected[tmp1].location = tmp5;                                 \
        char_element->affected[tmp1].bitvector = tmp6;                                \
        break;                                                                        \
    }

#define KEY_STATS(the_field, e1, e2, e3, e4, e5, e6)                                  \
    if (!strcmp(line, the_field)) {                                                   \
        sscanf(value, "%d %d %d %d %d %d", &tmp1, &tmp2, &tmp3, &tmp4, &tmp5, &tmp6); \
        e1 = tmp1;                                                                    \
        e2 = tmp2;                                                                    \
        e3 = tmp3;                                                                    \
        e4 = tmp4;                                                                    \
        e5 = tmp5;                                                                    \
        e6 = tmp6;                                                                    \
        break;                                                                        \
    }

#define KEY_AB(the_field, e1, e2, e3, e4)                         \
    if (!strcmp(line, the_field)) {                               \
        sscanf(value, "%d %d %d %d", &tmp1, &tmp2, &tmp3, &tmp4); \
        e1 = tmp1;                                                \
        e2 = tmp2;                                                \
        e3 = tmp3;                                                \
        e4 = tmp4;                                                \
        break;                                                    \
    }

#define KEY_ARRAY(the_field, element)         \
    if (!strcmp(line, the_field)) {           \
        sscanf(value, "%d %d", &tmp1, &tmp2); \
        element[tmp1] = tmp2;                 \
        break;                                \
    }

namespace {

int normalize_tactics_value(int value)
{
    return (value >= TACTICS_DEFENSIVE && value <= TACTICS_BERSERK) ? value : TACTICS_NORMAL;
}

int normalize_shooting_value(int value)
{
    return (value >= SHOOTING_SLOW && value <= SHOOTING_FAST) ? value : SHOOTING_NORMAL;
}

int normalize_casting_value(int value)
{
    return (value >= CASTING_SLOW && value <= CASTING_FAST) ? value : CASTING_NORMAL;
}

void sanitize_persisted_combat_state(struct char_special2_data* specials2)
{
    if (specials2 == nullptr)
        return;

    specials2->tactics = normalize_tactics_value(specials2->tactics);
    specials2->shooting = normalize_shooting_value(specials2->shooting);
    specials2->casting = normalize_casting_value(specials2->casting);
    specials2->two_handed = specials2->two_handed ? 1 : 0;
}

} // namespace

int load_player_from_text(char* name, std::string_view player_text, struct char_file_u* char_element)
{
    std::string player_text_owner(rots::text::truncate_at_null(player_text));
    int tmp, tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, end;
    char line[100];
    char *tmpchar, *value, *ctmp, *position;
    const char* input_end = nullptr;

    memset((char*)char_element, 0, sizeof(struct char_file_u));

    for (tmpchar = name; *tmpchar; tmpchar++)
        *tmpchar = tolower(*tmpchar);

    for (tmp = 0; tmp <= top_of_p_table; tmp++)
        if (!str_cmp_nullable((player_table + tmp)->name, name))
            break;

    if (tmp > top_of_p_table) {
        log(std::format("load_player: player {} not in player_table", name));
        return -1;
    }

    char_element->player_index = tmp;
    if (player_text_owner.empty()) {
        log(std::format("Couldn't parse character file text for {}\n", name));
        return -1;
    }
    input_end = player_text_owner.data() + player_text_owner.size();

    for (tmp1 = 0; tmp1 < MAX_AFFECT; tmp1++) {
        char_element->affected[tmp1].type = 0;
        char_element->affected[tmp1].duration = 0;
        char_element->affected[tmp1].modifier = 0;
        char_element->affected[tmp1].location = 0;
        char_element->affected[tmp1].bitvector = 0;
    }

    for (tmp1 = 0; tmp1 < MAX_SKILLS; tmp1++)
        char_element->skills[tmp1] = 0;

    end = FALSE;
    position = player_text_owner.data();
    memset(char_element->description, 0, 512);
    while (end == FALSE) {
        if (position >= input_end) {
            log(std::format(
                "load_player_from_text: malformed player data for {} (unexpected end of input)",
                name)
                    );
            return -1;
        }

        /* clear line, then read off a line */
        memset(line, 0, 99);
        for (tmpchar = position, tmp1 = 0;
            tmpchar < input_end && (*tmpchar != '\n') && (*tmpchar != '\r') && (*tmpchar != '\0');
            tmpchar++, tmp1++) {
            if (tmp1 >= static_cast<int>(sizeof(line) - 1)) {
                log(std::format(
                    "load_player_from_text: malformed player data for {} (line too long)", name)
                        );
                return -1;
            }
            line[tmp1] = *tmpchar;
        }

        position = tmpchar;
        while (position < input_end && (*position == '\r' || *position == '\n'))
            ++position;

        for (tmp1 = 0; tmp1 < (int)(MIN(12, strlen(line))); tmp1++)
            if (isspace(line[tmp1])) {
                line[tmp1] = '\0';
                break;
            }
        value = line + 12;

        switch (UPPER(line[0])) {
        case '#':
            break;

        case 'A':
            KEY_INT("alignment", char_element->specials2.alignment);
            KEY_INT("act", char_element->specials2.act);
            KEY_AFF("affect");
            break;

        case 'B':
            KEY_INT("bodytype", char_element->bodytype);
            KEY_INT("birth", char_element->birth);
            KEY_INT("bad_pws", char_element->specials2.bad_pws);
            KEY_ARRAY("bodyparts", char_element->points.bodypart_hit);
            break;

        case 'C':
            KEY_INT("casting", char_element->specials2.casting);
            KEY_INT("conditions0", char_element->specials2.conditions[0]);
            KEY_INT("conditions1", char_element->specials2.conditions[1]);
            KEY_INT("conditions2", char_element->specials2.conditions[2]);
            KEY_INT("color_mask", char_element->profs.color_mask);
            if (!strcmp(line, "colorfg")) {
                int index = 0;
                int mode = 0;
                int ansi = 0;
                int red = 0;
                int green = 0;
                int blue = 0;
                if (sscanf(value, "%d %d %d %d %d %d", &index, &mode, &ansi, &red, &green, &blue) == 6 && index >= 0 && index < MAX_COLOR_FIELDS) {
                    char_element->profs.color_settings[index].foreground.mode = static_cast<unsigned char>(mode);
                    char_element->profs.color_settings[index].foreground.ansi = static_cast<unsigned char>(ansi);
                    char_element->profs.color_settings[index].foreground.red = static_cast<unsigned char>(red);
                    char_element->profs.color_settings[index].foreground.green = static_cast<unsigned char>(green);
                    char_element->profs.color_settings[index].foreground.blue = static_cast<unsigned char>(blue);
                    char_element->profs.colors[index] = static_cast<char>(ansi);
                }
                break;
            }
            if (!strcmp(line, "colorbg")) {
                int index = 0;
                int mode = 0;
                int ansi = 0;
                int red = 0;
                int green = 0;
                int blue = 0;
                if (sscanf(value, "%d %d %d %d %d %d", &index, &mode, &ansi, &red, &green, &blue) == 6 && index >= 0 && index < MAX_COLOR_FIELDS) {
                    char_element->profs.color_settings[index].background.mode = static_cast<unsigned char>(mode);
                    char_element->profs.color_settings[index].background.ansi = static_cast<unsigned char>(ansi);
                    char_element->profs.color_settings[index].background.red = static_cast<unsigned char>(red);
                    char_element->profs.color_settings[index].background.green = static_cast<unsigned char>(green);
                    char_element->profs.color_settings[index].background.blue = static_cast<unsigned char>(blue);
                }
                break;
            }
            KEY_ARRAY("color", char_element->profs.colors);
            break;

        case 'D':
            KEY_LONG_STR("description", char_element->description, 512);
            KEY_INT("damage", char_element->points.damage);
            if (!strcmp(line, "dodge")) {
                SET_DODGE(char_element) = atoi(value);
                break;
            }
            break;

        case 'E':
            KEY_INT("ENE_regen", char_element->points.ENE_regen);
            KEY_INT("exp", char_element->points.exp);
            KEY_INT("encumb", char_element->points.encumb);
            if (!strcmp(line, "end"))
                end = TRUE;
            break;

        case 'F':
            KEY_INT("freeze_lvl", char_element->specials2.freeze_level);
            break;

        case 'G':
            KEY_INT("gold", char_element->points.gold);
            break;

        case 'H':
            KEY_INT("height", char_element->height);
            KEY_INT("hometown", char_element->hometown);
            KEY_STR("host", char_element->host, HOST_LEN);
            break;

        case 'I':
            if (!strcmp(line, "idnum")) {
                char_element->specials2.idnum = atoi(value);
                break;
            }
            break;

        case 'J':
            break;

        case 'K':
            // `value` is attacker-controllable player-file text, not a format string --
            // pass it through "%s" rather than as the format argument itself (a
            // non-literal-format-string bug: any '%' bytes in the line would previously
            // be interpreted as conversion specifiers).
            printf("%s", value);
            break;

        case 'L':
            KEY_INT("level", char_element->level);
            KEY_INT("language", char_element->language);
            KEY_INT("last_logon", char_element->last_logon);
            KEY_INT("load_room", char_element->specials2.load_room);
            break;

        case 'M':
            KEY_INT("mini_lvl", char_element->specials2.mini_level);
            KEY_INT("morale", char_element->specials2.morale);
            KEY_INT("max_mini_lv", char_element->specials2.max_mini_level);
            break;

        case 'N':
            // Fixed-bug (Phase 5 T6, UBSan array-bounds): char_element->name is
            // char[MAX_NAME_LENGTH + 1] (13 bytes); the literal 20 here overran the
            // array by 7 bytes into the adjacent char_element->pwd field on every
            // player-file load (memcpy in the KEY_STR macro). Matches the
            // (array_size - 1)-length convention used by the "host"/"password"
            // KEY_STR calls above/below (which leave the struct's pre-zeroed final
            // byte as the terminator).
            KEY_STR("name", char_element->name, MAX_NAME_LENGTH);
            break;

        case 'O':
            KEY_INT("owner", char_element->specials2.owner);
            if (!strcmp(line, "ob")) {
                SET_OB(char_element) = atoi(value);
                break;
            }
            break;

        case 'P':
            KEY_INT("prof", char_element->prof);
            KEY_INT("played", char_element->played);
            KEY_STR("password", char_element->pwd, MAX_PWD_LENGTH);
            KEY_INT("pref", char_element->specials2.pref);
            KEY_INT("perception", char_element->specials2.perception);
            if (!strcmp(line, "parry")) {
                SET_PARRY(char_element) = atoi(value);
                break;
            }
            KEY_STATS("permstats", char_element->constabilities.str,
                char_element->constabilities.lea, char_element->constabilities.intel,
                char_element->constabilities.wil, char_element->constabilities.dex,
                char_element->constabilities.con);
            KEY_AB("permabil", char_element->constabilities.hit, char_element->constabilities.mana,
                char_element->constabilities.move, tmp);
            KEY_ARRAY("prof_coef", char_element->profs.prof_coof);
            KEY_ARRAY("prof_level", char_element->profs.prof_level);
            KEY_ARRAY("prof_exp", char_element->profs.prof_exp);
            break;

        case 'Q':
            break;

        case 'R':
            KEY_INT("race", char_element->race);
            KEY_INT("rerolls", char_element->specials2.rerolls);
            KEY_INT("rp_flag", char_element->specials2.rp_flag);
            KEY_INT("retiredon", char_element->specials2.retiredon);
            break;

        case 'S':
            KEY_INT("sex", char_element->sex);
            KEY_INT("shooting", char_element->specials2.shooting);
            KEY_INT("sp_to_learn", char_element->specials2.spells_to_learn);
            KEY_INT("spec", char_element->profs.specialization);
            KEY_ARRAY("skills", char_element->skills);
            break;

        case 'T':
            KEY_INT("tactics", char_element->specials2.tactics);
            KEY_STR("title", char_element->title, 80);
            KEY_ARRAY("talks", char_element->talks);
            KEY_INT("twohanded", char_element->specials2.two_handed);
            KEY_STATS("tmpstats", char_element->tmpabilities.str, char_element->tmpabilities.lea,
                char_element->tmpabilities.intel, char_element->tmpabilities.wil,
                char_element->tmpabilities.dex, char_element->tmpabilities.con);
            KEY_AB("tmpabil", char_element->tmpabilities.hit, char_element->tmpabilities.mana,
                char_element->tmpabilities.move, char_element->points.spirit);
            break;

        case 'U':
            break;

        case 'V':
            break;

        case 'W':
            KEY_INT("weight", char_element->weight);
            KEY_INT("wimpy", char_element->specials2.wimp_level);
            break;

        case 'X':
            break;

        case 'Y':
            break;

        case 'Z':
            break;
        }
    }
    decrypt_line((unsigned char*)char_element->pwd, MAX_PWD_LENGTH);
    sanitize_persisted_combat_state(&char_element->specials2);

    return 1;
}

int load_player(char* name, struct char_file_u* char_element)
{
    int tmp;
    char playerfname[100];
    char* pf = 0;

    for (tmp = 0; name[tmp]; ++tmp)
        name[tmp] = tolower(name[tmp]);

    for (tmp = 0; tmp <= top_of_p_table; tmp++)
        if (!str_cmp_nullable((player_table + tmp)->name, name))
            break;

    if (tmp > top_of_p_table) {
        log(std::format("load_player: player {} not in player_table", name));
        return -1;
    }

    strcpy(playerfname, (player_table + tmp)->ch_file);
    if (has_suffix(playerfname, ".character.json"))
        return load_player_from_account_json_path(name, playerfname, char_element);

    file_to_string_alloc(playerfname, &pf);
    if (!(pf)) {
        log(std::format("Couldn't find character file for {} in the player_table\n", name));
        return -1;
    }

    const int result = load_player_from_text(name, pf, char_element);
    RELEASE(pf);
    return result;
}

int find_name(char* name);

/* Load a char, TRUE if loaded, FALSE if not */
int load_char(char* name, struct char_file_u* char_element)
{
    int ret;
    extern void convert_old_colormask(struct char_file_u*);

    if (*name == '\0')
        return -1;

    ret = load_player(name, char_element);

    if (ret >= 0)
        convert_old_colormask(char_element);

    return ret;
}

int load_char_from_text(char* name, std::string_view player_text, struct char_file_u* char_element)
{
    int ret;
    extern void convert_old_colormask(struct char_file_u*);

    if (*name == '\0')
        return -1;

    ret = load_player_from_text(name, player_text, char_element);

    if (ret >= 0)
        convert_old_colormask(char_element);

    return ret;
}

/* copy data from the file structure to a char struct */
void store_to_char(struct char_file_u* st, struct char_data* ch)
{
    int i;

    ch->player_index = st->player_index;
    GET_SEX(ch) = st->sex;
    GET_PROF(ch) = st->prof;
    GET_RACE(ch) = st->race;
    GET_BODYTYPE(ch) = st->bodytype;
    GET_LEVEL(ch) = st->level;
    ch->player.language = st->language;

    ch->player.short_descr = 0;
    ch->player.long_descr = 0;

    // Fixed-bug (Phase 5 T6, LeakSanitizer): store_to_char() can run more
    // than once against the SAME char_data (e.g. interpre.cpp's login flow
    // re-verifies and re-stores an already-loaded d->character), and title/
    // description were unconditionally (re-)CREATE()'d every time, orphaning
    // whatever the PRIOR call had allocated -- the same leak class as the
    // ch->profs fix above, just for these two fields. RELEASE() first (a
    // no-op via its own null check the first time through) so a repeat call
    // frees the old buffer before allocating its replacement.
    RELEASE(ch->player.title);
    if (*st->title) {
        CREATE(ch->player.title, char, strlen(st->title) + 1);
        strcpy(ch->player.title, st->title);
    } else
        GET_TITLE(ch) = 0;

    RELEASE(ch->player.description);
    if (*st->description) {
        CREATE(ch->player.description, char, strlen(st->description) + 1);
        strcpy(ch->player.description, st->description);
    } else {
        CREATE(ch->player.description, char, 1);
        ch->player.description[0] = 0;
    }

    ch->player.hometown = st->hometown;

    ch->player.time.birth = st->birth;
    ch->player.time.played = st->played;
    ch->player.time.logon = time(0);

    for (i = 0; i < MAX_TOUNGE; i++)
        ch->player.talks[i] = st->talks[i];

    // Fixed-bug (Phase 5 T6, LeakSanitizer): every real call site
    // (interpre.cpp's account-character selection, act_wiz.cpp's `stat
    // file`/player-file lookups) calls clear_char() -- which already
    // CREATE1()s ch->profs -- immediately before store_to_char(), so this was
    // unconditionally allocating a SECOND char_prof_data and orphaning the
    // first one on every single call (one char_prof_data leaked per account
    // character selection in the live game). Only allocate if ch->profs is
    // still null; either way the final contents are identical (byte-copied
    // from st->profs below).
    if (!ch->profs)
        CREATE1(ch->profs, char_prof_data);
    memcpy(ch->profs, &(st->profs), sizeof(struct char_prof_data));
    ch->specials.alias = 0;

    ch->player.weight = st->weight;
    /* weight fix!! should be removed some time */
    if (ch->player.weight <= 200)
        ch->player.weight = get_race_weight(ch);
    ch->player.height = st->height;
    ch->tmpabilities = st->tmpabilities;
    ch->constabilities = st->constabilities;

    ch->points = st->points;
    ch->specials2 = st->specials2;
    sanitize_persisted_combat_state(&ch->specials2);

    /* New dynamic skill system: only PCs have a skill array allocated. */

    if (ch->skills.empty())
        ch->skills.assign(MAX_SKILLS, 0);
    for (i = 0; i < MAX_SKILLS; i++)
        SET_SKILL(ch, i, st->skills[i]);
    if (ch->knowledge.empty())
        ch->knowledge.assign(MAX_SKILLS, 0);
    recalc_skills(ch);

    ch->specials.carry_weight = 0;
    ch->specials.carry_items = 0;
    SET_PARRY(ch) = 0;
    ch->points.damage = 0; // st->points.damage;
    SET_DODGE(ch) = 0;
    SET_OB(ch) = 0;
    ch->points.encumb = 0;
    ch->specials2.leg_encumb = 0;
    ch->points.ENE_regen = st->points.ENE_regen;
    ch->specials.ENERGY = 1200;
    utils::set_tactics(*ch, ch->specials2.tactics);
    utils::set_shooting(*ch, ch->specials2.shooting);

    utils::set_specialization(*ch, game_types::PS_None);
    int spec = st->profs.specialization;
    utils::set_specialization(*ch, game_types::player_specs(spec));
    utils::set_casting(*ch, ch->specials2.casting);

    // Same leak class as title/description above -- RELEASE() first so a
    // repeat store_to_char() call on the same char_data doesn't orphan the
    // previous ch->player.name allocation.
    RELEASE(ch->player.name);
    CREATE(ch->player.name, char, strlen(st->name) + 1);
    strcpy(ch->player.name, st->name);

    ch->damage_details.reset();

    /* Add all spell effects */
    for (i = 0; i < MAX_AFFECT; i++) {
        if (st->affected[i].type)
            affect_to_char(ch, &st->affected[i]);
    }

    ch->in_room = GET_LOADROOM(ch);

    affect_total(ch);

    if (ch->specials2.two_handed)
        SET_BIT(ch->specials.affected_by, AFF_TWOHANDED);
    else
        REMOVE_BIT(ch->specials.affected_by, AFF_TWOHANDED);

    /* If you're not poisioned and you've been away for more than
      an hour, we'll set your HMV back to full */

    if (!IS_AFFECTED(ch, AFF_POISON) && (((long)(time(0) - st->last_logon)) >= SECS_PER_REAL_HOUR)) {
        ch->tmpabilities = ch->abilities;
    }
}

/* copy vital data from a players char-structure to the file structure */
void char_to_store(struct char_data* ch, struct char_file_u* st)
{
    int i;
    struct affected_type* af;

    /* Unaffect everything a character can be affected by */
    affect_total(ch, AFFECT_TOTAL_REMOVE);

    for (af = ch->affected, i = 0; i < MAX_AFFECT; i++) {
        if (af) {
            st->affected[i] = *af;
            st->affected[i].next = 0;
            af = af->next;
        } else {
            st->affected[i].type = 0; /* Zero signifies not used */
            st->affected[i].duration = 0;
            st->affected[i].modifier = 0;
            st->affected[i].location = 0;
            st->affected[i].bitvector = 0;
            st->affected[i].next = 0;
        }
    }

    if ((i >= MAX_AFFECT) && af && af->next)
        log("SYSERR: WARNING: OUT OF STORE ROOM FOR AFFECTED TYPES!!!");

    st->player_index = GET_INDEX(ch);
    st->birth = ch->player.time.birth;
    st->played = ch->player.time.played;
    st->played += (long)(time(0) - ch->player.time.logon);
    st->last_logon = time(0);

    ch->player.time.played = st->played;
    ch->player.time.logon = time(0);

    st->hometown = ch->player.hometown;
    memcpy(&(st->profs), ch->profs, sizeof(struct char_prof_data));

    st->weight = GET_WEIGHT(ch);
    st->height = GET_HEIGHT(ch);
    st->sex = GET_SEX(ch);
    st->prof = GET_PROF(ch);
    st->race = GET_RACE(ch);
    st->bodytype = GET_BODYTYPE(ch);
    st->level = GET_LEVEL(ch);
    st->language = ch->player.language;
    st->constabilities = ch->constabilities;
    st->tmpabilities = ch->tmpabilities;
    st->points = ch->points;
    st->specials2 = ch->specials2;

    st->points.dodge = GET_DODGE(ch);
    st->points.OB = GET_OB(ch);
    st->points.damage = GET_DAMAGE(ch);
    st->points.parry = GET_PARRY(ch);
    st->points.ENE_regen = GET_ENE_REGEN(ch);
    st->specials2.tactics = GET_TACTICS(ch);
    st->specials2.shooting = GET_SHOOTING(ch);
    st->specials2.casting = GET_CASTING(ch);
    st->specials2.two_handed = IS_TWOHANDED(ch) ? 1 : 0;
    sanitize_persisted_combat_state(&st->specials2);

    if (GET_TITLE(ch))
        strcpy(st->title, GET_TITLE(ch));
    else
        *st->title = '\0';

    if (ch->player.description) {
        if (strlen(ch->player.description) >= 512)
            ch->player.description[511] = 0;
        strcpy(st->description, ch->player.description);
    } else
        *st->description = '\0';

    for (i = 0; i < MAX_TOUNGE; i++)
        st->talks[i] = ch->player.talks[i];

    for (i = 0; i < MAX_SKILLS; i++)
        st->skills[i] = ch->skills[i];

    strcpy(st->name, GET_NAME(ch));

    /* add spell and eq affections back in now */
    affect_total(ch, AFFECT_TOTAL_SET);
} /* Char to store */

int create_entry(char* name)
{
    int i;

    if (top_of_p_table == -1) {
        CREATE(player_table, struct player_index_element, 1);
        top_of_p_table = 0;
    } else {
        inc_p_table();
    }
    CREATE(player_table[top_of_p_table].name, char, strlen(name) + 1);
    (player_table + top_of_p_table)->flags = 0;
    (player_table + top_of_p_table)->ch_file[0] = 0;
    (player_table + top_of_p_table)->warpoints = 0;
    (player_table + top_of_p_table)->race = 0;
    (player_table + top_of_p_table)->rank = PKILL_UNRANKED;
    (player_table + top_of_p_table)->totalrank = PKILL_UNRANKED;
    for (i = 0; (*(player_table[top_of_p_table].name + i) = LOWER(*(name + i))); i++)
        ;
    return (top_of_p_table);
}

int ensure_player_index_entry(std::string_view name)
{
    name = rots::text::truncate_at_null(name);
    if (name.empty())
        return -1;

    for (int index = 0; index <= top_of_p_table; ++index) {
        if (str_cmp((player_table + index)->name, name) == 0)
            return index;
    }

    char normalized_name[MAX_NAME_LENGTH + 1];
    std::snprintf(normalized_name, sizeof(normalized_name), "%.*s",
        static_cast<int>(name.size()), name.data());
    return create_entry(normalized_name);
}

void update_player_index_entry_from_store(struct char_file_u* stored_character)
{
    update_player_index_entry_from_store(stored_character, {}, nullptr);
}

bool update_player_index_entry_from_store(struct char_file_u* stored_character,
    std::string_view character_path, std::string* error_message)
{
    if (stored_character == nullptr || stored_character->name[0] == '\0') {
        if (error_message != nullptr)
            *error_message = "Cannot update player index for an empty stored character.";
        return false;
    }

    character_path = rots::text::truncate_at_null(character_path);
    if (!character_path.empty()) {
        const size_t path_length = character_path.size();
        const size_t path_capacity = sizeof(player_table[0].ch_file);
        if (path_length >= path_capacity) {
            if (error_message != nullptr)
                *error_message = "Account character storage path is too long for the live player index.";
            log(std::format("update_player_index_entry_from_store: account-native path for {} is "
                            "{} bytes; player index limit is {}",
                static_cast<const char*>(stored_character->name), path_length,
                path_capacity - 1)
                    );
            return false;
        }
    }

    const int player_index = ensure_player_index_entry(stored_character->name);
    if (player_index < 0) {
        if (error_message != nullptr)
            *error_message = "Could not create a live player index entry.";
        return false;
    }

    stored_character->player_index = player_index;
    player_table[player_index].level = stored_character->level;
    player_table[player_index].race = stored_character->race;
    player_table[player_index].idnum = stored_character->specials2.idnum;
    player_table[player_index].log_time = stored_character->last_logon;
    player_table[player_index].flags = stored_character->specials2.act;
    if (!character_path.empty())
        std::snprintf(player_table[player_index].ch_file,
            sizeof(player_table[player_index].ch_file), "%.*s",
            static_cast<int>(character_path.size()), character_path.data());
    if (error_message != nullptr)
        error_message->clear();
    return true;
}

/* create a new entry in the in-memory index table for the player file */
int old_create_entry(char* name)
{
    int i, num;
    struct player_index_element* tmpel;

    //   printf("create_entry: top=%d\n",top_of_p_table);
    if (top_of_p_table == -1) {
        CREATE(player_table, struct player_index_element, 1);
        top_of_p_table = 0;
        num = 0;
        i = 0;
    } else {
        for (i = 0; i <= top_of_p_table; i++)
            if (IS_SET((player_table + i)->flags, PLR_DELETED))
                break;

        if (i > top_of_p_table) {
            log("Could not find a deleted player, reallocating player_table."); // Fingolfin
                                                                                // -
                                                                                // looks
                                                                                // to
                                                                                // me
                                                                                // as
                                                                                // though
                                                                                // there
                                                                                // is
                                                                                // a
                                                                                // memory
                                                                                // leak
                                                                                // here
            CREATE(tmpel, struct player_index_element,
                top_of_p_table + 1); // old player_table is not freed
            if (!tmpel) {
                perror("create entry");
                exit(1);
            }
            memcpy(tmpel, player_table, top_of_p_table * sizeof(player_index_element));
            top_of_p_table++;
        } else
            RELEASE(player_table[i].name);

        num = i;
    }
    //   printf("placing the new player in position %d, total
    //   %d\n",num,top_of_p_table);
    CREATE(player_table[num].name, char, strlen(name) + 1);
    // printf("passed created, i=%d,num=%d\n",i,num);
    (player_table + i)->flags = 0;
    // (player_table + i)->title[0] = 0;
    (player_table + i)->warpoints = 0;
    (player_table + i)->race = 0;
    /* copy lowercase equivalent of name to table field */
    //   printf("create entry:copying the name %s, num=%d\n",name, num);
    for (i = 0; (*(player_table[num].name + i) = LOWER(*(name + i))); i++)
        ;

    return (num);
}

//  Moves a valid character file to the /zzz/ directory and deletes their
//  in-memory index record

void move_char_deleted(int index)
{
    // Was system("mv <ch_file> players/ZZZ/<name>"); the return value was
    // never checked, so a failed move was already silent -- ec is discarded
    // here to match.
    std::error_code rename_ec;
    std::filesystem::rename((player_table + index)->ch_file,
        std::string("players/ZZZ/") + (player_table + index)->name, rename_ec);
    player_table[index].name[0] = 0;
    player_table[index].idnum = 0;
}

void delete_character_file(struct char_data* ch)
{
    int tmp;

    for (tmp = 0; tmp <= top_of_p_table; tmp++) {
        if (!str_cmp_nullable((player_table + tmp)->name, ch->player.name))
            break;
    }

    if (tmp > top_of_p_table) {
        send_to_char("Bug: you are not in the character list: cannot delete.\n", ch);
        log(std::format("delete_character_file: could not find player: cannot delete: {}\n",
            ch->player.name)
                );
        return;
    }

    move_char_deleted(tmp);
}

/* write the vital data of a player to the player file */

void encrypt_line(unsigned char* line, int len);

// Serialize ch to scratch_path in the legacy versioned-text format. Returns false and
// removes the partial scratch on any write/close error, so the caller skips finalize and
// never destroys the live file. Body is the former save_player serialization, unchanged,
// so its bytes stay identical to the legacy path (pinned by the A/B oracle + round-trip test).
bool write_player_text(struct char_data* ch, int load_room, std::string_view scratch_path)
{
    const std::string scratch_path_owner(rots::text::truncate_at_null(scratch_path));
    // {}: char_to_store() below populates every char_file_u field except pwd/host
    // (set explicitly right after it returns); leaving chd uninitialized meant
    // chd.pwd's bytes past the password's null terminator (up to MAX_PWD_LENGTH) were
    // stack garbage, which memcpy(pwdcrypt, chd.pwd, MAX_PWD_LENGTH) then fed into
    // encrypt_line() -- an uninitialized-read that made the encrypted "password" line
    // non-reproducible across calls (caught by the Wave 4 D2 characterization suite's
    // mandatory ASan pass; see db_save_roundtrip_tests.cpp). Doesn't change the login
    // password check (a null-terminated compare never reads past the real password's
    // terminator either way) or any already-written save file -- only makes new writes'
    // trailing pwd bytes deterministic zero instead of undefined. Pre-existing bug,
    // unrelated to and untouched by the sprintf/strcpy->std::format conversion in the
    // surrounding functions.
    struct char_file_u chd { };
    int tmp;

    // "wb": this serialization is pinned byte-for-byte (A/B oracle + round-trip
    // test); CRT text mode on Windows would expand every '\n' to "\r\n" and
    // silently fork the on-disk format from the POSIX builds (Phase 3 Task 6).
    FILE* pf = fopen(scratch_path_owner.c_str(), "wb");
    if (!pf) {
        return false;
    }

    char_to_store(ch, &chd);
    strcpy(chd.pwd, ch->desc->pwd);
    strncpy(chd.host, ch->desc->host, HOST_LEN);
    if (!PLR_FLAGGED(ch, PLR_LOADROOM))
        chd.specials2.load_room = load_room;

    fprintf(pf, "#player\n"); // so we can have other #sections later...
    fprintf(pf, "version     %d\n", SAVE_VERSION);
    fprintf(pf, "name        %s\n", chd.name);
    fprintf(pf, "sex         %d\n", chd.sex);
    fprintf(pf, "prof        %d\n", chd.prof);
    fprintf(pf, "race        %d\n", chd.race);
    fprintf(pf, "bodytype    %d\n", chd.bodytype);
    fprintf(pf, "level       %d\n", chd.level);
    fprintf(pf, "language    %d\n", chd.language);
    fprintf(pf, "birth       %lld\n", static_cast<long long>(chd.birth));
    fprintf(pf, "played      %d\n", chd.played);
    fprintf(pf, "weight      %d\n", chd.weight);
    fprintf(pf, "height      %d\n", chd.height);
    fprintf(pf, "title       %s\n", chd.title);
    fprintf(pf, "hometown    %d\n", chd.hometown);
    fprintf(pf, "description \n%s~\n", chd.description);
    fprintf(pf, "last_logon  %lld\n", static_cast<long long>(chd.last_logon));
    memcpy(pwdcrypt, chd.pwd, MAX_PWD_LENGTH);
    encrypt_line((unsigned char*)pwdcrypt, MAX_PWD_LENGTH);
    pwdcrypt[MAX_PWD_LENGTH] = '\0'; // terminate explicitly -- see the pwdcrypt declaration comment
    fprintf(pf, "password    %s\n", pwdcrypt);
    fprintf(pf, "host        %s\n", chd.host);
    fprintf(pf, "idnum       %ld\n", chd.specials2.idnum);
    fprintf(pf, "load_room   %d\n", chd.specials2.load_room);
    fprintf(pf, "sp_to_learn %d\n", chd.specials2.spells_to_learn);
    fprintf(pf, "alignment   %d\n", chd.specials2.alignment);
    fprintf(pf, "act         %ld\n", chd.specials2.act);
    fprintf(pf, "pref        %ld\n", chd.specials2.pref);
    fprintf(pf, "wimpy       %d\n", chd.specials2.wimp_level);
    fprintf(pf, "freeze_lvl  %d\n", chd.specials2.freeze_level);
    fprintf(pf, "bad_pws     %d\n", chd.specials2.bad_pws);
    fprintf(pf, "conditions0 %d\n", chd.specials2.conditions[0]);
    fprintf(pf, "conditions1 %d\n", chd.specials2.conditions[1]);
    fprintf(pf, "conditions2 %d\n", chd.specials2.conditions[2]);
    fprintf(pf, "mini_lvl    %d\n", chd.specials2.mini_level);
    fprintf(pf, "morale      %d\n", chd.specials2.morale);
    fprintf(pf, "owner       %d\n", chd.specials2.owner);
    fprintf(pf, "rerolls     %d\n", chd.specials2.rerolls);
    fprintf(pf, "max_mini_lv %d\n", chd.specials2.max_mini_level);
    fprintf(pf, "perception  %d\n", chd.specials2.perception);
    fprintf(pf, "rp_flag     %d\n", chd.specials2.rp_flag);
    fprintf(pf, "retiredon   %d\n", chd.specials2.retiredon);
    fprintf(pf, "tactics     %d\n", chd.specials2.tactics);
    fprintf(pf, "shooting    %d\n", chd.specials2.shooting);
    fprintf(pf, "casting     %d\n", chd.specials2.casting);
    fprintf(pf, "twohanded   %d\n", chd.specials2.two_handed);
    fprintf(pf, "ob          %d\n", chd.points.OB);
    fprintf(pf, "damage      %d\n", chd.points.damage);
    fprintf(pf, "ENE_regen   %d\n", chd.points.ENE_regen);
    fprintf(pf, "parry       %d\n", chd.points.parry);
    fprintf(pf, "dodge       %d\n", chd.points.dodge);
    fprintf(pf, "gold        %d\n", chd.points.gold);
    fprintf(pf, "exp         %d\n", chd.points.exp);
    fprintf(pf, "encumb      %d\n", chd.points.encumb);
    fprintf(pf, "spec        %d\n", chd.profs.specialization);

    for (tmp = 0; tmp < MAX_COLOR_FIELDS; ++tmp)
        if (chd.profs.colors[tmp] != CNRM)
            fprintf(pf, "color       %d %d\n", tmp, chd.profs.colors[tmp]);
    for (tmp = 0; tmp < MAX_COLOR_FIELDS; ++tmp) {
        const color_value_data& foreground = chd.profs.color_settings[tmp].foreground;
        const color_value_data& background = chd.profs.color_settings[tmp].background;
        if (foreground.mode != COLOR_VALUE_DEFAULT)
            fprintf(pf, "colorfg     %d %d %d %d %d %d\n", tmp, foreground.mode, foreground.ansi,
                foreground.red, foreground.green, foreground.blue);
        if (background.mode != COLOR_VALUE_DEFAULT)
            fprintf(pf, "colorbg     %d %d %d %d %d %d\n", tmp, background.mode, background.ansi,
                background.red, background.green, background.blue);
    }

    for (tmp = 0; tmp < MAX_TOUNGE; tmp++)
        fprintf(pf, "talks       %d %d\n", tmp, chd.talks[tmp]);

    for (tmp = 0; tmp < MAX_SKILLS; tmp++)
        if (chd.skills[tmp])
            fprintf(pf, "skills      %d %d\n", tmp, chd.skills[tmp]);

    for (tmp = 0; tmp < MAX_AFFECT; tmp++)
        if (chd.affected[tmp].duration != 0) {
            fprintf(pf, "affect      %d %d %d %d %d %ld\n", tmp, chd.affected[tmp].type,
                chd.affected[tmp].duration, chd.affected[tmp].modifier,
                chd.affected[tmp].location, chd.affected[tmp].bitvector);
        }

    for (tmp = 0; tmp < MAX_BODYPARTS; tmp++)
        fprintf(pf, "bodyparts   %d %d\n", tmp, chd.points.bodypart_hit[tmp]);

    fprintf(pf, "tmpstats    %d %d %d %d %d %d\n", chd.tmpabilities.str, chd.tmpabilities.lea,
        chd.tmpabilities.intel, chd.tmpabilities.wil, chd.tmpabilities.dex,
        chd.tmpabilities.con);

    fprintf(pf, "tmpabil     %d %d %d %d\n", chd.tmpabilities.hit, chd.tmpabilities.mana,
        chd.tmpabilities.move, chd.points.spirit);

    fprintf(pf, "permstats    %d %d %d %d %d %d\n", chd.constabilities.str, chd.constabilities.lea,
        chd.constabilities.intel, chd.constabilities.wil, chd.constabilities.dex,
        chd.constabilities.con);

    fprintf(pf, "permabil     %d %d %d %d\n", chd.constabilities.hit, chd.constabilities.mana,
        chd.constabilities.move, 0);

    for (tmp = 0; tmp < MAX_PROFS + 1; tmp++)
        fprintf(pf, "prof_coef   %d %d\n", tmp, chd.profs.prof_coof[tmp]);

    for (tmp = 0; tmp < MAX_PROFS + 1; tmp++)
        fprintf(pf, "prof_level  %d %d\n", tmp, chd.profs.prof_level[tmp]);

    for (tmp = 0; tmp < MAX_PROFS + 1; tmp++)
        fprintf(pf, "prof_exp    %d %ld\n", tmp, chd.profs.prof_exp[tmp]);

    fprintf(pf, "end\n");

    if (ferror(pf)) {
        fclose(pf);
        std::remove(scratch_path_owner.c_str());
        return false;
    }
    if (fclose(pf) != 0) {
        std::remove(scratch_path_owner.c_str());
        return false;
    }
    return true;
}

/* New player save (Fingolfin) under construction */

void save_player(struct char_data* ch, int load_room, int index_pos)
{
    char name[255];
    char* tmpchar;
    char playerfname[100];

    strcpy(name, GET_NAME(ch));
    for (tmpchar = name; *tmpchar; tmpchar++)
        *tmpchar = tolower(*tmpchar);

    switch (tolower(*name)) {
    case 'a':
    case 'b':
    case 'c':
    case 'd':
    case 'e':
        strcpy(playerfname, std::format("players/A-E/{}", static_cast<const char*>(name)).c_str());
        break;
    case 'f':
    case 'g':
    case 'h':
    case 'i':
    case 'j':
        strcpy(playerfname, std::format("players/F-J/{}", static_cast<const char*>(name)).c_str());
        break;
    case 'k':
    case 'l':
    case 'm':
    case 'n':
    case 'o':
        strcpy(playerfname, std::format("players/K-O/{}", static_cast<const char*>(name)).c_str());
        break;
    case 'p':
    case 'q':
    case 'r':
    case 's':
    case 't':
        strcpy(playerfname, std::format("players/P-T/{}", static_cast<const char*>(name)).c_str());
        break;
    case 'u':
    case 'v':
    case 'w':
    case 'x':
    case 'y':
    case 'z':
        strcpy(playerfname, std::format("players/U-Z/{}", static_cast<const char*>(name)).c_str());
        break;
    default:
        strcpy(playerfname, std::format("players/ZZZ/{}", static_cast<const char*>(name)).c_str());
        break;
    }

    // If serialization fails, do NOT finalize: a failed finalize must never destroy the
    // live file. Leave the existing save intact and retry next cycle.
    if (!write_player_text(ch, load_room, "players/temp")) {
        return;
    }

    // std::string, not a fixed char[120]: the original snprintf(..., sizeof(versioned), ...)
    // truncated silently past 119 bytes -- a real-if-unlikely player-file-naming corruption
    // for a long enough playerfname/idnum/log_time combination. A std::string composes the
    // same "{}.{}.{}.{}.{}.{}" text with no artificial ceiling and no truncation risk.
    const std::string versioned = std::format("{}.{}.{}.{}.{}.{}", static_cast<const char*>(playerfname),
        (player_table + index_pos)->level, (player_table + index_pos)->race,
        (player_table + index_pos)->idnum, (long)(player_table + index_pos)->log_time,
        (player_table + index_pos)->flags);

    char dirpath[100];
    strcpy(dirpath, playerfname);
    char* dirslash = strrchr(dirpath, '/');
    if (dirslash) {
        *dirslash = '\0';
    }
    if (finalize_player_file_rename("players/temp", dirpath, name, versioned)) {
        strcpy((player_table + index_pos)->ch_file, versioned.c_str());
    } else {
        log("save_player: could not finalize player file.");
    }
}

void save_char(struct char_data* ch, int load_room, int notify_char)
{
    int tmp;
    char_file_u chd { };

    if (IS_NPC(ch) || (!ch->desc)) {
        log(std::format("save_char: ({}) zero desc or is_npc\n", GET_NAME(ch)));
        return;
    }

    /* if load_room isn't anywhere, but they are somewhere, we'll set
     * load_room to that somewhere */
    if ((load_room == NOWHERE) && (ch->in_room != NOWHERE))
        load_room = world[ch->in_room].number;

    ch->specials2.load_room = load_room;

    //  Send player a msg if this is an autosave.
    if (notify_char) {
        send_to_char(std::format("Saving {}.\n\r", GET_NAME(ch)), ch);
    }

    /* whois update block */
    for (tmp = 0; tmp <= top_of_p_table; tmp++)
        if (!str_cmp_nullable((player_table + tmp)->name, ch->player.name))
            break;

    if (tmp > top_of_p_table) {
        send_to_char("Error: you are not being saved.  Please contact an immortal.\n\r", ch);
        log(std::format("save_char: could not find player {}: Not saving.\n", ch->player.name)
                );
        return;
    }

    (player_table + tmp)->log_time = time(0);
    (player_table + tmp)->level = ch->player.level;
    (player_table + tmp)->idnum = ch->specials2.idnum;
    (player_table + tmp)->flags = PLR_FLAGS(ch);
    (player_table + tmp)->race = ch->player.race;
    char_to_store(ch, &chd);
    chd.specials2.load_room = load_room;

    std::string owner_account_name;
    std::string account_error;
    const bool account_native_player_entry = has_suffix((player_table + tmp)->ch_file, ".character.json");
    const bool linked_character = account::find_linked_character_owner_account(
                                      ".", GET_NAME(ch), &owner_account_name, &account_error)
        && !owner_account_name.empty();
    if (linked_character) {
        bool wrote_account_character_file = false;
        std::string character_file_error;
        const bool has_account_character_file = account::account_character_file_exists(
            ".", owner_account_name, GET_NAME(ch), &character_file_error);
        if (has_account_character_file) {
            std::string write_error;
            if (!account::write_account_character_file(".", owner_account_name, chd,
                    &write_error)) {
                log(std::format(
                    "save_char: failed to write account-native character file for {}: {}",
                    GET_NAME(ch), write_error)
                        );
            } else
                wrote_account_character_file = true;
        } else if (!character_file_error.empty()) {
            log(std::format("save_char: failed to inspect account-native character file for {}: {}",
                GET_NAME(ch), character_file_error)
                    );
        } else {
            std::string write_error;
            if (!account::write_account_character_file(".", owner_account_name, chd,
                    &write_error)) {
                log(std::format("save_char: failed to repair missing account-native character file "
                                "for {}: {}",
                    GET_NAME(ch), write_error)
                        );
            } else
                wrote_account_character_file = true;
        }
        if (wrote_account_character_file) {
            const std::string account_character_path = account::account_character_player_path(".", owner_account_name, GET_NAME(ch));
            std::string player_index_error;
            if (!update_player_index_entry_from_store(&chd, account_character_path,
                    &player_index_error)) {
                log(std::format(
                    "save_char: failed to refresh account-native player index for {}: {}",
                    GET_NAME(ch), player_index_error)
                        );
            }
        }
    } else if (account_native_player_entry) {
        if (account_error.empty())
            strcpy(buf, std::format("save_char: refusing legacy fallback for account-native "
                                    "character {} because linked ownership could not be resolved",
                            GET_NAME(ch))
                            .c_str());
        else
            strcpy(buf,
                std::format(
                    "save_char: refusing legacy fallback for account-native character {}: {}",
                    GET_NAME(ch), account_error)
                    .c_str());
        log(buf);
    } else {
        save_player(ch, load_room, tmp); // New save into individual files
    }
}

/************************************************************************
 *  procs of a (more or less) general utility nature			*
 ********************************************************************** */

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

// Releases the alias_list chain objsave.cpp's Crash_alias_load() builds
// (each node CREATE1()'d, each with a CREATE()'d .command string) and
// attaches to ch->specials.alias. Nothing else in the tree ever released
// this list -- every character with saved aliases leaked it on every
// free_char() (e.g. logout), a confirmed production leak (backlog T2,
// disclosed at Phase 5 T6 via sanitize.supp). NULL-safe: `list` is 0 for a
// freshly-cleared character and always 0 for mobs (structs.h: "aliases, 0
// for mobs"), so this is a no-op for both.
void free_alias_list(struct alias_list* list)
{
    while (list) {
        struct alias_list* next = list->next;
        RELEASE(list->command);
        RELEASE(list);
        list = next;
    }
}

// Deep-clones an alias_list chain (owned_alias_list's copy ctor/assignment,
// structs.h -- RAII T4). Mirrors do_alias()'s/Crash_alias_load()'s own node
// construction: each node CREATE1()'d, each .command CREATE()'d and NUL-
// terminated. In practice this only ever clones an empty (null) chain --
// NPCs never carry a real one (mob_proto's alias is always null; see
// structs.h's alias field comment) and no live PC char_data is ever
// whole-struct-copied -- but it is implemented as a real clone (not a
// shallow pointer copy) so char_special_data's copy assignment -- which
// `*mob = mob_proto[i]` (db.cpp's read_mobile(), the char_data whole-struct
// copy) relies on -- stays well-defined.
struct alias_list* owned_alias_list::clone(struct alias_list* src)
{
    struct alias_list* head = nullptr;
    struct alias_list* tail = nullptr;

    for (struct alias_list* node = src; node; node = node->next) {
        struct alias_list* copy;
        CREATE1(copy, alias_list);
        std::memcpy(copy->keyword, node->keyword, sizeof(copy->keyword));

        const size_t command_length = node->command ? strlen(node->command) : 0;
        CREATE(copy->command, char, command_length + 1);
        if (node->command)
            strcpy(copy->command, node->command);
        else
            copy->command[0] = '\0';

        copy->next = nullptr;
        if (!head)
            head = tail = copy;
        else {
            tail->next = copy;
            tail = copy;
        }
    }

    return head;
}

/* release memory allocated for a char struct */
void free_char(struct char_data* ch)
{
    clear_account_backed_object_bytes_for_character(ch);

    // RAII T6a: this function frees the char_data storage with a raw free()
    // (RELEASE(ch), below). Historically it ALSO ran ~char_data()'s work by
    // hand -- per-member reset()/move-assign-empty of every heap-owning member
    // (T3 skills/knowledge vectors, T4 alias, T5b poofIn/poofOut strings,
    // extra_specialization_data, damage_details). That hand teardown is now
    // subsumed by an explicit `ch->~char_data();` immediately before the free
    // (bottom of this function), giving the calloc/placement-new (clear_char,
    // read_mobile) a symmetric explicit-dtor/free teardown. See ownership-map.md
    // section 6.
    //
    // What must STILL be done by hand here, BEFORE ~char_data() runs, is
    // everything the implicit destructor does NOT (and must not) do:
    //   * the CONDITIONAL prototype-shared char* strings (name/title/descr/
    //     profs) -- freed only under the IS_NPC guard below, because for a
    //     normal NPC they alias mob_proto[nr] and the destructor leaving raw
    //     char* members untouched is exactly what prevents a double-free of the
    //     prototype;
    //   * the raw special-mob script pointers (special_stack/special_list_area/
    //     special_prog_number/special_prog_point) -- POD int*/long* with no
    //     destructor;
    //   * draining the `affected` spell-affect chain (pool-mediated, not an
    //     owned member the destructor knows about).
    // These read ch's members, so they run while the object is still alive.

    // RAII T5a: free the special-mob script buffers, now in their own typed
    // fields (were reinterpret_cast'd through poofIn/poofOut/union1/union2).
    // All null for PCs and ordinary mobs, so RELEASE (null-safe) is
    // unconditional. Releasing special_prog_number/special_prog_point here
    // fixes the ownership-map section 2c leak: pre-T5a these aliased
    // union1.prog_number / union2.prog_point, which free_char could not free
    // without risking a PC's reply_ptr/reply_number in the same union storage.
    RELEASE(ch->specials.special_stack);
    RELEASE(ch->specials.special_list_area);
    RELEASE(ch->specials.special_prog_number);
    RELEASE(ch->specials.special_prog_point);

    while (ch->affected)
        affect_remove(ch, ch->affected);

    if (!IS_NPC(ch) || (IS_NPC(ch) && ch->nr == -1)) {

        RELEASE(GET_NAME(ch));
        RELEASE(ch->player.title);
        RELEASE(ch->player.short_descr);
        RELEASE(ch->player.long_descr);
        RELEASE(ch->player.description);
        RELEASE(ch->profs);
    } /*  else if ((i = ch->nr) > -1) {
     if (ch->player.name && ch->player.name != mob_proto[i].player.name)
       RELEASE(ch->player.name);
     if (ch->player.title && ch->player.title != mob_proto[i].player.title)
       RELEASE(ch->player.title);
     if (ch->player.short_descr && ch->player.short_descr !=
   mob_proto[i].player.short_descr) RELEASE(ch->player.short_descr); if
   (ch->player.long_descr && ch->player.long_descr !=
   mob_proto[i].player.long_descr) RELEASE(ch->player.long_descr); if
   (ch->player.description && ch->player.description !=
   mob_proto[i].player.description) RELEASE(ch->player.description);
   } */

    // Diagnostic only (not teardown): a mob should never have carried a skills
    // array (clear_char only sizes skills/knowledge for mode != MOB_ISNPC). Log
    // the invariant violation while ch is still alive; the vector's heap buffer
    // is released by ~char_data() below like every other owning member.
    if (!ch->skills.empty() && IS_NPC(ch))
        log("SYSERR: Mob had skills array allocated!");

    remove_char_exists(ch->abs_number);

    // RAII T6a: symmetric teardown. clear_char()/read_mobile() construct this
    // object with a placement-new over calloc storage (new (ch) char_data());
    // the mirror image is an explicit destructor call followed by the raw
    // free. Running ~char_data() here destroys EVERY owning member --
    // skills/knowledge (std::vector, T3), specials.alias (owned_alias_list, T4),
    // specials.poofIn/poofOut (std::string, T5b), extra_specialization_data
    // (deletes current_spec_info) and damage_details (clears its std::map) --
    // which is exactly (and only) what the per-member reset()/move-assign-empty
    // lines removed above used to do by hand. Crucially it does NOT touch the
    // raw char* members (name/title/descr/profs, freed conditionally above; the
    // special-mob pointers, freed above), because those are POD pointers with
    // no destructor -- so a normal NPC whose strings alias the prototype is not
    // double-freed. RELEASE(ch) then frees the raw storage (gated by
    // global_release_flag exactly as before). Do NOT read *ch after this line.
    ch->~char_data();
    RELEASE(ch);
}

// RAII T6b: owning factory for a clean-scope char_data instance. Mirrors the
// canonical hand-written `CREATE(x, char_data, 1); clear_char(x, mode);`
// allocation (calloc storage + placement-new construction) and wraps the result
// in a char_data_ptr whose deleter is free_char -- so the whole
// allocate/construct/use/free lifecycle is single-owner and exception-safe.
// `mode` is passed straight to clear_char (MOB_VOID for a PC-shaped scratch
// char, MOB_ISNPC for a mob). See db.h for the world-graph caveat.
char_data_ptr make_char_data(int mode)
{
    struct char_data* ch;
    CREATE(ch, struct char_data, 1);
    clear_char(ch, mode);
    return char_data_ptr(ch);
}

/* release memory allocated for an obj struct */
void free_obj(struct obj_data* obj)
{
    int nr;
    struct extra_descr_data *thith, *next_one;

    if ((nr = obj->item_number) == -1) {
        RELEASE(obj->name);
        RELEASE(obj->description);
        RELEASE(obj->short_description);
        RELEASE(obj->action_description);
        if (obj->ex_description)
            for (thith = obj->ex_description; thith; thith = next_one) {
                next_one = thith->next;
                RELEASE(thith->keyword);
                RELEASE(thith->description);
                RELEASE(thith);
            }
    } /* else {
     if (obj->name && obj->name != obj_proto[nr].name)
       RELEASE(obj->name);
     if (obj->description && obj->description != obj_proto[nr].description)
       RELEASE(obj->description);
     if (obj->short_description && obj->short_description !=
   obj_proto[nr].short_description) RELEASE(obj->short_description); if
   (obj->action_description && obj->action_description !=
   obj_proto[nr].action_description) RELEASE(obj->action_description); if
   (obj->ex_description && obj->ex_description != obj_proto[nr].ex_description)
       for (thith = obj->ex_description; thith; thith = next_one) {
         next_one = thith->next;
         RELEASE(thith->keyword);
         RELEASE(thith->description);
         RELEASE(thith);
       }
   } */

    RELEASE(obj);
}

/* read contets of a text file, alloc space, point buf to it */
int file_to_string_alloc(std::string_view name, char** buf)
{
    char temp[MAX_STRING_LENGTH];

    if (file_to_string(name, temp) < 0)
        return -1;

    RELEASE(*buf);

    *buf = str_dup(temp);
    return 0;
}

/* read contents of a text file, and place in buf */
int file_to_string(std::string_view name, char* buf)
{
    const std::string name_owner(rots::text::truncate_at_null(name));
    FILE* fl;
    char tmp[100];

    *buf = '\0';

    if (!(fl = fopen(name_owner.c_str(), "r"))) {
        perror(std::format("Error reading {}", name_owner).c_str());
        *buf = '\0';
        return (-1);
    }

    do {
        fgets(tmp, 99, fl);

        if (!feof(fl)) {
            if (strlen(buf) + strlen(tmp) + 2 > MAX_STRING_LENGTH) {
                log("SYSERR: fl->strng: string too big (db.c, file_to_string)");
                *buf = '\0';
                return (-1);
            }

            strcat(buf, tmp);
            *(buf + strlen(buf) + 1) = '\0';
            *(buf + strlen(buf)) = '\r';
        }
    } while (!feof(fl));

    fclose(fl);

    return (0);
}

int get_char_directory(char* orig_name, char* filename)
{
    char *ptr, name[30];

    if (!*orig_name)
        return 0;

    strcpy(name, orig_name);
    for (ptr = name; *ptr; ptr++)
        *ptr = tolower(*ptr);

    switch (tolower(*name)) {
    case 'a':
    case 'b':
    case 'c':
    case 'd':
    case 'e':
        strcpy(filename, "/A-E/");
        break;
    case 'f':
    case 'g':
    case 'h':
    case 'i':
    case 'j':
        strcpy(filename, "/F-J/");
        break;
    case 'k':
    case 'l':
    case 'm':
    case 'n':
    case 'o':
        strcpy(filename, "/K-O/");
        break;
    case 'p':
    case 'q':
    case 'r':
    case 's':
    case 't':
        strcpy(filename, "/P-T/");
        break;
    case 'u':
    case 'v':
    case 'w':
    case 'x':
    case 'y':
    case 'z':
        strcpy(filename, "/U-Z/");
        break;
    default:
        strcpy(filename, "/ZZZ/");
        break;
    }

    return 1;
}

/* clear some of the the working variables of a char */
void reset_char(struct char_data* ch)
{
    int i;

    for (i = 0; i < MAX_WEAR; i++) /* Initialisering */
        ch->equipment[i] = 0;

    ch->followers = 0;
    ch->master = 0;
    ch->next_die = 0;
    ch->carrying = 0;
    ch->next = 0;
    ch->next_fighting = 0;
    ch->next_in_room = 0;
    ch->specials.fighting = 0;
    ch->specials.position = POSITION_STANDING;
    ch->specials.default_pos = POSITION_STANDING;
    ch->specials.carry_weight = 0;
    ch->specials.carry_items = 0;
    ch->specials.was_in_room = -1;

    if (GET_HIT(ch) <= 0)
        GET_HIT(ch) = 1;
    if (GET_MOVE(ch) <= 0)
        GET_MOVE(ch) = 1;
    if (GET_MANA(ch) <= 0)
        GET_MANA(ch) = 1;
}

/* clear ALL the working variables of a char and do NOT free any space
 * alloc'ed*/
void clear_char(struct char_data* ch, int mode)
{
    /* At every production call site, ch points to memory obtained via
     * CREATE()/calloc (raw, unconstructed storage), never to a char_data that has
     * already run its constructor. Placement-new value-initializes it in place:
     * this zeroes every POD member exactly like the old memset did, but also
     * properly constructs the non-trivial members (player_damage_details::damage_map
     * is a std::map; specialization_data has a user destructor) instead of leaving
     * them as zeroed-but-never-constructed memory, which is undefined behavior the
     * moment those members are used (deterministic SIGSEGV under libc++/macOS;
     * silently tolerated by libstdc++/Linux). See db.cpp read_mobile() for the
     * other call path that needs the same treatment.
     * (Test code also calls clear_char() directly on already-constructed stack
     * `char_data` objects to reset them between cases; that's safe in practice here
     * because the non-trivial members are always empty at that point, so
     * re-running their default constructors via this placement-new has nothing to
     * leak — but it's not the shape this function's placement-new was written for,
     * and isn't a pattern to extend to types where re-construction over a live
     * object could leak or double-free.) */
    new (ch) char_data();
    CREATE1(ch->profs, char_prof_data);
    memset(ch->profs->colors, CNRM, sizeof(ch->profs->colors[0]) * MAX_COLOR_FIELDS);

    ch->specials.alias = 0;
    ch->in_room = NOWHERE;
    ch->specials.was_in_room = NOWHERE;
    ch->specials.position = POSITION_STANDING;
    ch->specials.default_pos = POSITION_STANDING;
    SET_TACTICS(ch, TACTICS_NORMAL);
    SET_SHOOTING(ch, SHOOTING_NORMAL);
    SET_CASTING(ch, CASTING_NORMAL);
    ch->specials.script_info = 0;
    ch->specials.script_number = 0;
    ch->specials2.rp_flag = 0;
    ch->specials2.retiredon = 0;
    ch->specials2.hide_flags = 0;
    utils::set_specialization(*ch, game_types::PS_None);

    SET_DODGE(ch) = 0; /* Basic Armor */
    if (ch->abilities.mana < 100)
        ch->abilities.mana = 100;

    if (mode != MOB_ISNPC) {
        ch->skills.assign(MAX_SKILLS, 0);
        ch->knowledge.assign(MAX_SKILLS, 0);
        if (ch->desc)
            memset(ch->desc->pwd, 0, MAX_PWD_LENGTH);
    }
}

void clear_object(struct obj_data* obj)
{
    memset((char*)obj, 0, (size_t)sizeof(struct obj_data));

    obj->item_number = -1;
    obj->in_room = NOWHERE;
    obj->obj_flags.timer = -1;
    obj->obj_flags.script_info = 0;
}

/* initialize a new character only if prof is set */
void init_char(struct char_data* ch)
{
    int i;

    set_title(ch);

    ch->player.short_descr = 0;
    ch->player.long_descr = 0;
    ch->player.description = 0;

    ch->player.hometown = number(1, 4);

    ch->player.time.birth = time(0);
    ch->player.time.played = 0;
    ch->player.time.logon = time(0);

    for (i = 0; i < MAX_TOUNGE; i++)
        ch->player.talks[i] = 0;

    SET_STR(ch, 9);
    GET_INT(ch) = 9;
    GET_WILL(ch) = 9;
    GET_DEX(ch) = 9;
    GET_CON(ch) = 9;

    /* make favors for sex */
    ch->player.weight = get_race_weight(ch) * (85 + number(0, 30)) / 100;
    ch->player.height = get_race_height(ch) * (85 + number(0, 30)) / 100;

    ch->abilities.mana = 100;
    ch->tmpabilities.mana = GET_MAX_MANA(ch);
    ch->tmpabilities.hit = GET_MAX_HIT(ch);
    ch->abilities.move = 82;
    ch->tmpabilities.move = GET_MAX_MOVE(ch);
    GET_DODGE(ch) = 0;

    ch->specials2.idnum = ++top_idnum;

    if (ch->skills.empty())
        ch->skills.assign(MAX_SKILLS, 0);

    for (i = 0; i < MAX_SKILLS; i++) {
        SET_SKILL(ch, i, 0);
        SET_KNOWLEDGE(ch, i, 0);
    }

    ch->specials.affected_by = race_affect[GET_RACE(ch)];

    ch->specials2.saving_throw = 0;
    ch->specials2.rp_flag = 0;

    for (i = 0; i < 3; i++)
        GET_COND(ch, i) = (GET_LEVEL(ch) == LEVEL_IMPL ? -1 : 24);

    ch->damage_details.reset();

    /* The default preference flags */
    PRF_FLAGS(ch) |= PRF_SPAM | PRF_NARRATE | PRF_CHAT | PRF_WIZ | PRF_SING | PRF_PROMPT | PRF_ECHO | PRF_SPINNER;
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
//*************************** Crime functions *****************************
//*************************************************************************

int know_of_crime(int criminal, int victim, int witness);
// void forget_crimes(char_data *ch, int criminal);
void read_crime_file();

void boot_crimes() { read_crime_file(); }

// ---------------------------------------------------------------------------
// Phase 2a Task 6: crime-record persistence as JSON, plus a one-time legacy
// struct-dump file converter. Follows the mail_json/boards_json/pkill_json
// precedent (Tasks 4/5/6): JSON-first load, legacy binary as a one-time
// fallback, atomic temp+rename writes, legacy file renamed '.migrated'
// after a verified convert. See db.h for the crime_record_type layout this
// decodes, and for the crime_json::CrimeStoreData/CRIME_SCHEMA_VERSION
// declarations (exposed there so pod_persistence_json_tests.cpp can call
// this namespace's codec/converter directly).
// ---------------------------------------------------------------------------
namespace crime_json {
namespace {

    void set_error(std::string* error_message, std::string_view message)
    {
        if (error_message)
            error_message->assign(rots::text::truncate_at_null(message));
    }

    // Legacy on-disk format: CRIME_FILE (misc/crimelist) is a raw
    // concatenation of fwrite(crime_record + i, sizeof(crime_record_type), 1,
    // f) records (add_crime/forget_crimes, below). crime_record_type (db.h)
    // holds only int/sh_int fields, so its layout -- including any compiler-
    // inserted padding -- is identical on 32-bit and 64-bit x86 builds.
    // These offsetof-derived offsets therefore describe the real on-disk
    // bytes regardless of which ABI compiles this reader (the Task 1 ABI-
    // portability convention, applied via offsetof since crime_record_type is
    // a real, already-declared struct rather than a hand-reconstructed
    // historical format).
    constexpr size_t kCrimeTimeOffset = offsetof(crime_record_type, crime_time);
    constexpr size_t kCriminalOffset = offsetof(crime_record_type, criminal);
    constexpr size_t kVictimOffset = offsetof(crime_record_type, victim);
    constexpr size_t kCrimeOffset = offsetof(crime_record_type, crime);
    constexpr size_t kWitnessOffset = offsetof(crime_record_type, witness);
    constexpr size_t kWitnessTypeOffset = offsetof(crime_record_type, witness_type);
    constexpr size_t kRecordSize = sizeof(crime_record_type);

    bool read_i32_at(const std::string &bytes, size_t record_offset, size_t field_offset,
                     int *value, std::string *error_message, std::string_view label) {
        label = rots::text::truncate_at_null(label);
        const size_t offset = record_offset + field_offset;
        if (offset + 4 > bytes.size()) {
            set_error(error_message, std::string("Truncated crime file while reading ") +
                                         std::string(label) + ".");
            return false;
        }
        const uint32_t raw = static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset])) | (static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset + 1])) << 8) | (static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset + 2])) << 16) | (static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset + 3])) << 24);
        *value = static_cast<int>(raw);
        return true;
    }

    bool read_i16_at(const std::string &bytes, size_t record_offset, size_t field_offset,
                     sh_int *value, std::string *error_message, std::string_view label) {
        label = rots::text::truncate_at_null(label);
        const size_t offset = record_offset + field_offset;
        if (offset + 2 > bytes.size()) {
            set_error(error_message, std::string("Truncated crime file while reading ") +
                                         std::string(label) + ".");
            return false;
        }
        const uint16_t raw = static_cast<uint16_t>(static_cast<unsigned char>(bytes[offset])) | (static_cast<uint16_t>(static_cast<unsigned char>(bytes[offset + 1])) << 8);
        *value = static_cast<sh_int>(static_cast<int16_t>(raw));
        return true;
    }

    bool read_whole_file_contents(std::string_view path, std::string* bytes)
    {
        const std::string path_owner(rots::text::truncate_at_null(path));
        FILE* file = std::fopen(path_owner.c_str(), "rb");
        if (file == nullptr)
            return false;

        std::string loaded_bytes;
        char buffer[4096];
        bool read_ok = true;
        while (true) {
            const size_t bytes_read = std::fread(buffer, sizeof(char), sizeof(buffer), file);
            if (bytes_read > 0)
                loaded_bytes.append(buffer, bytes_read);
            if (bytes_read < sizeof(buffer)) {
                if (std::ferror(file))
                    read_ok = false;
                break;
            }
        }
        std::fclose(file);

        if (!read_ok)
            return false;

        *bytes = std::move(loaded_bytes);
        return true;
    }

    // Temp-file + rename atomic write, matching mail.cpp/boards.cpp/pkill.cpp's pattern.
    bool write_file_contents_atomically(std::string_view path, std::string_view contents,
        std::string* error_message)
    {
        const std::string path_owner(rots::text::truncate_at_null(path));
        contents = rots::text::truncate_at_null(contents);
        const std::string temp_path = path_owner + ".tmp";

        FILE* temp_file = std::fopen(temp_path.c_str(), "wb");
        if (temp_file == nullptr) {
            set_error(error_message, std::string("Unable to open temporary crime file '") + temp_path + "': " + strerror(errno));
            return false;
        }

        const size_t bytes_written = contents.empty() ? 0
                                                      : std::fwrite(contents.data(), sizeof(char), contents.size(), temp_file);
        const int flush_result = std::fflush(temp_file);
        const int close_result = std::fclose(temp_file);

        if (bytes_written != contents.size() || flush_result != 0 || close_result != 0) {
            std::remove(temp_path.c_str());
            set_error(error_message,
                std::string("Failed to write temporary crime file '") + temp_path + "'.");
            return false;
        }

        if (rots_rename_replace(temp_path, path_owner) != 0) {
            const std::string rename_error = strerror(errno);
            std::remove(temp_path.c_str());
            set_error(error_message, "Failed to move temporary crime file into place: " + rename_error);
            return false;
        }

        return true;
    }

} // namespace

#ifdef TESTING
bool write_json_text_for_testing(std::string_view path, std::string_view contents, std::string* error_message)
{
    return write_file_contents_atomically(path, contents, error_message);
}
#endif

bool legacy_crime_file_from_binary(const std::string& bytes,
    std::vector<crime_record_type>* records,
    std::string* error_message)
{
    if (records == nullptr) {
        set_error(error_message, "Crime records output parameter must not be null.");
        return false;
    }

    if (bytes.size() % kRecordSize != 0) {
        set_error(error_message, "Crime file corrupt: size is not a multiple of the record size.");
        return false;
    }

    std::vector<crime_record_type> parsed;
    const size_t num_records = bytes.size() / kRecordSize;
    parsed.reserve(num_records);
    for (size_t index = 0; index < num_records; ++index) {
        const size_t record_offset = index * kRecordSize;
        crime_record_type record { };
        int crime_time = 0, crime = 0;
        sh_int criminal = 0, victim = 0, witness = 0, witness_type = 0;
        if (!read_i32_at(bytes, record_offset, kCrimeTimeOffset, &crime_time, error_message,
                "crime_time"))
            return false;
        if (!read_i16_at(bytes, record_offset, kCriminalOffset, &criminal, error_message,
                "criminal"))
            return false;
        if (!read_i16_at(bytes, record_offset, kVictimOffset, &victim, error_message, "victim"))
            return false;
        if (!read_i32_at(bytes, record_offset, kCrimeOffset, &crime, error_message, "crime"))
            return false;
        if (!read_i16_at(bytes, record_offset, kWitnessOffset, &witness, error_message, "witness"))
            return false;
        if (!read_i16_at(bytes, record_offset, kWitnessTypeOffset, &witness_type, error_message,
                "witness_type"))
            return false;

        record.crime_time = crime_time;
        record.criminal = criminal;
        record.victim = victim;
        record.crime = crime;
        record.witness = witness;
        record.witness_type = witness_type;
        parsed.push_back(record);
    }

    *records = std::move(parsed);
    set_error(error_message, "");
    return true;
}

std::string serialize_crime_to_json(const CrimeStoreData& data)
{
    std::ostringstream output;
    output << "{\n";
    output << "  \"version\": " << data.version << ",\n";
    output << "  \"records\": [\n";
    for (size_t index = 0; index < data.records.size(); ++index) {
        const crime_record_type& record = data.records[index];
        output << "    {\n";
        output << "      \"crime_time\": " << record.crime_time << ",\n";
        output << "      \"criminal\": " << record.criminal << ",\n";
        output << "      \"victim\": " << record.victim << ",\n";
        output << "      \"crime\": " << record.crime << ",\n";
        output << "      \"witness\": " << record.witness << ",\n";
        output << "      \"witness_type\": " << record.witness_type << "\n";
        output << "    }";
        if (index + 1 < data.records.size())
            output << ",";
        output << "\n";
    }
    output << "  ]\n";
    output << "}\n";
    return output.str();
}

bool deserialize_crime_from_json(std::string_view json, CrimeStoreData *data,
                                 std::string *error_message) {
    if (data == nullptr) {
        set_error(error_message, "Crime store output parameter must not be null.");
        return false;
    }

    CrimeStoreData parsed;
    const bool ok = json_utils::JsonReader(json).parse_root_object(
        [&](std::string_view key, json_utils::JsonReader* reader, std::string* nested_error) {
            if (key == "version")
                return reader->parse_integer(&parsed.version, nested_error);
            if (key == "records") {
                return reader->parse_array(
                    [&](json_utils::JsonReader* record_reader, std::string* record_error) {
                        crime_record_type record { };
                        int criminal = 0, victim = 0, witness = 0, witness_type = 0;
                        const bool record_ok = record_reader->parse_object(
                            [&](std::string_view record_key,
                                json_utils::JsonReader* nested_reader,
                                std::string* nested_record_error) {
                                if (record_key == "crime_time")
                                    return nested_reader->parse_integer(&record.crime_time,
                                        nested_record_error);
                                if (record_key == "criminal")
                                    return nested_reader->parse_integer(&criminal,
                                        nested_record_error);
                                if (record_key == "victim")
                                    return nested_reader->parse_integer(&victim,
                                        nested_record_error);
                                if (record_key == "crime")
                                    return nested_reader->parse_integer(&record.crime,
                                        nested_record_error);
                                if (record_key == "witness")
                                    return nested_reader->parse_integer(&witness,
                                        nested_record_error);
                                if (record_key == "witness_type")
                                    return nested_reader->parse_integer(&witness_type,
                                        nested_record_error);
                                return nested_reader->skip_value(nested_record_error);
                            },
                            record_error);
                        if (!record_ok)
                            return false;
                        for (int narrow_value : { criminal, victim, witness, witness_type }) {
                            if (narrow_value < -32768 || narrow_value > 32767) {
                                set_error(record_error, "criminal/victim/witness/witness_type must "
                                                        "fit in a signed 16-bit field.");
                                return false;
                            }
                        }
                        record.criminal = static_cast<sh_int>(criminal);
                        record.victim = static_cast<sh_int>(victim);
                        record.witness = static_cast<sh_int>(witness);
                        record.witness_type = static_cast<sh_int>(witness_type);
                        parsed.records.push_back(record);
                        return true;
                    },
                    nested_error);
            }
            return reader->skip_value(nested_error);
        },
        error_message);

    if (!ok)
        return false;

    if (parsed.version != CRIME_SCHEMA_VERSION) {
        set_error(error_message, "Unsupported crime schema version.");
        return false;
    }

    *data = std::move(parsed);
    set_error(error_message, "");
    return true;
}

bool crime_record_equal(const crime_record_type& a, const crime_record_type& b)
{
    return a.crime_time == b.crime_time && a.criminal == b.criminal && a.victim == b.victim && a.crime == b.crime && a.witness == b.witness && a.witness_type == b.witness_type;
}

bool crime_records_equal(const std::vector<crime_record_type>& a,
    const std::vector<crime_record_type>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t index = 0; index < a.size(); ++index)
        if (!crime_record_equal(a[index], b[index]))
            return false;
    return true;
}

std::string crime_json_path(std::string_view legacy_path) { return std::string(rots::text::truncate_at_null(legacy_path)) + ".json"; }

bool load_crime_json_store(std::string_view json_path, std::vector<crime_record_type>* records,
    std::string* error_message)
{
    std::string json_text;
    if (!read_whole_file_contents(json_path, &json_text))
        return false;

    CrimeStoreData data;
    if (!deserialize_crime_from_json(json_text, &data, error_message))
        return false;

    *records = std::move(data.records);
    return true;
}

bool write_crime_json_store(std::string_view json_path,
    const std::vector<crime_record_type>& records,
    std::string* error_message)
{
    CrimeStoreData data;
    data.records = records;
    return write_file_contents_atomically(json_path, serialize_crime_to_json(data), error_message);
}

bool crime_store_safe_to_overwrite(std::string_view json_path, std::string* error_message)
{
    const std::string json_path_owner(rots::text::truncate_at_null(json_path));
    FILE* probe = std::fopen(json_path_owner.c_str(), "rb");
    if (probe == nullptr) {
        set_error(error_message, "");
        return true;
    }
    std::fclose(probe);

    std::vector<crime_record_type> existing_records;
    return load_crime_json_store(json_path, &existing_records, error_message);
}

bool convert_legacy_crime_file(std::string_view legacy_path, std::string* error_message)
{
    legacy_path = rots::text::truncate_at_null(legacy_path);
    if (legacy_path.empty()) {
        set_error(error_message, "Legacy crime path must not be empty.");
        return false;
    }

    std::string legacy_bytes;
    if (!read_whole_file_contents(legacy_path, &legacy_bytes)) {
        set_error(error_message, std::string("Failed to read legacy crime file '") + std::string(legacy_path) + "': " + strerror(errno));
        return false;
    }

    std::vector<crime_record_type> decoded;
    std::string decode_error;
    if (!legacy_crime_file_from_binary(legacy_bytes, &decoded, &decode_error)) {
        set_error(error_message, "Decode failed: " + decode_error);
        return false;
    }

    CrimeStoreData store;
    store.records = decoded;
    const std::string json = serialize_crime_to_json(store);

    // Verify (binding conversion contract): re-decode the freshly serialized
    // JSON and compare it field-for-field to the original decode.
    CrimeStoreData reparsed;
    std::string verify_error;
    if (!deserialize_crime_from_json(json, &reparsed, &verify_error)) {
        set_error(error_message,
            "Verify-decode of freshly serialized JSON failed: " + verify_error);
        return false;
    }

    if (!crime_records_equal(decoded, reparsed.records)) {
        set_error(error_message,
            "Verify mismatch: re-decoded JSON does not equal the original legacy decode.");
        return false;
    }

    const std::string json_path = crime_json_path(legacy_path);
    std::string write_error;
    if (!write_file_contents_atomically(json_path, json, &write_error)) {
        set_error(error_message, write_error);
        return false;
    }

    const std::string migrated_path = std::string(legacy_path) + ".migrated";
    if (rots_rename_replace(legacy_path, migrated_path) != 0) {
        // JSON is written and verified; the legacy file simply couldn't be
        // retired (matches mail_json/boards_json/pkill_json's "partial
        // success" contract -- report but don't fail, nothing is at risk).
        set_error(error_message, std::string("Crime file converted but legacy rename to '") + migrated_path + "' failed: " + strerror(errno));
        return true;
    }

    set_error(error_message, "");
    return true;
}

} // namespace crime_json

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

void read_crime_file()
{
    int tmp;

    num_of_crimes = 0;
    const std::string json_path = crime_json::crime_json_path(CRIME_FILE);
    std::vector<crime_record_type> loaded_records;

    FILE* json_probe = fopen(json_path.c_str(), "rb");
    if (json_probe != NULL) {
        fclose(json_probe);
        std::string error_message;
        if (!crime_json::load_crime_json_store(json_path, &loaded_records, &error_message)) {
            log(("SYSERR: Crime JSON file '" + json_path + "' is malformed: " + error_message)
                    );
            CREATE1(crime_record, crime_record_type);
            return;
        }
    } else {
        /* No JSON store yet -- either a fresh install (no legacy file
         * either) or a legacy binary file waiting for its one-time
         * conversion. */
        FILE* legacy_probe = fopen(CRIME_FILE, "rb");
        if (legacy_probe == NULL) {
            log("Crime file does not exist, creating it.");
            CREATE1(crime_record, crime_record_type);
            return;
        }
        fclose(legacy_probe);

        std::string convert_error;
        if (!crime_json::convert_legacy_crime_file(CRIME_FILE, &convert_error)) {
            log(("SYSERR: Failed converting legacy crime file '" + std::string(CRIME_FILE) + "' to JSON: " + convert_error)
                    );
            CREATE1(crime_record, crime_record_type);
            return;
        }
        if (!convert_error.empty())
            log(("Converted legacy crime file to JSON (warning: " + convert_error + ")."));
        else
            log("Converted legacy crime file to JSON.");

        std::string load_error;
        if (!crime_json::load_crime_json_store(json_path, &loaded_records, &load_error)) {
            log(("SYSERR: Crime JSON file missing or malformed immediately after conversion: " + load_error)
                    );
            CREATE1(crime_record, crime_record_type);
            return;
        }
    }

    num_of_crimes = static_cast<int>(loaded_records.size());
    if (num_of_crimes == 0) {
        CREATE1(crime_record, crime_record_type);
    } else {
        CREATE(crime_record, crime_record_type, num_of_crimes);
        for (tmp = 0; tmp < num_of_crimes; tmp++)
            crime_record[tmp] = loaded_records[tmp];
    }

    for (tmp = 0; tmp < num_of_crimes; tmp++) {
        crime_record[tmp].criminal = find_player_in_table("", crime_record[tmp].criminal);
        crime_record[tmp].victim = find_player_in_table("", crime_record[tmp].victim);
        crime_record[tmp].witness = find_player_in_table("", crime_record[tmp].witness);
    }
}

void add_crime(int criminal, int victim, int witness, int crime, int wit_type)
{
    int time_kill;
    crime_record_type* tmprecord;

    if (know_of_crime(find_player_in_table("", criminal), find_player_in_table("", victim),
            find_player_in_table("", witness)))
        return;
    CREATE(tmprecord, crime_record_type, num_of_crimes + 1);
    memcpy(tmprecord, crime_record, num_of_crimes * sizeof(crime_record_type));
    RELEASE(crime_record);
    crime_record = tmprecord;

    time_kill = time(0);
    crime_record[num_of_crimes].crime_time = time_kill;
    crime_record[num_of_crimes].criminal = criminal;
    crime_record[num_of_crimes].victim = victim;
    crime_record[num_of_crimes].witness = witness;
    crime_record[num_of_crimes].crime = crime;
    crime_record[num_of_crimes].witness_type = wit_type;

    log(std::format("criminal: {}, victim: {}, witness: {}", criminal, victim, witness));

    // Phase 2a final-review Important 2: mirror pkill_update_file's
    // (pkill.cpp) fail-closed guard. If the on-disk store is present but
    // malformed -- notably including the case where boot's read_crime_file
    // already failed to load it, leaving crime_record as an empty in-memory
    // list -- refuse to overwrite it: doing so would permanently crush
    // whatever's salvageable in the real file with just this session's
    // (possibly near-empty) in-memory records. The crime above is still
    // recorded in memory for this session either way; only persistence is
    // skipped.
    const std::string crime_json_path = crime_json::crime_json_path(CRIME_FILE);
    std::string safety_error;
    if (!crime_json::crime_store_safe_to_overwrite(crime_json_path, &safety_error)) {
        log(("SYSERR: Crime JSON file '" + crime_json_path + "' is malformed, refusing to overwrite: " + safety_error)
                );
    } else {
        /* Persist the whole live crime set as JSON (idnum-keyed, matching the
         * legacy on-disk format) -- the mail_json/boards_json/pkill_json
         * "rewrite the whole store on each mutation" pattern, since JSON has no
         * append mode. crime_record[0, num_of_crimes) currently holds
         * player-table INDEXES (translated at boot / after each prior
         * add_crime, below); crime_record[num_of_crimes] is still idnum-valued
         * (set just above), matching what the legacy file always stored. */
        std::vector<crime_record_type> to_write;
        to_write.reserve(num_of_crimes + 1);
        for (int i = 0; i < num_of_crimes; ++i) {
            crime_record_type record = crime_record[i];
            record.criminal = (player_table + crime_record[i].criminal)->idnum;
            record.victim = (player_table + crime_record[i].victim)->idnum;
            record.witness = (player_table + crime_record[i].witness)->idnum;
            to_write.push_back(record);
        }
        to_write.push_back(crime_record[num_of_crimes]);

        std::string write_error;
        if (!crime_json::write_crime_json_store(crime_json_path, to_write, &write_error))
            mudlog("Could not open crime_file for writing.", NRM, LEVEL_IMMORT, TRUE);
    }

    crime_record[num_of_crimes].criminal = find_player_in_table("", criminal);
    crime_record[num_of_crimes].victim = find_player_in_table("", victim);
    crime_record[num_of_crimes].witness = find_player_in_table("", witness);

    num_of_crimes++;
}

int know_of_crime(int criminal, int victim, int witness)
{
    int tmp;

    for (tmp = 0; tmp < num_of_crimes; tmp++)
        if ((criminal == crime_record[tmp].criminal) && (victim == crime_record[tmp].victim) && (witness == crime_record[tmp].witness))
            return 1;
    return 0;
}

void forget_crimes(char_data* ch, int criminal)
{
    int tmp, count, not_write;

    if (IS_NPC(ch) || !RACE_GOOD(ch))
        return;
    count = 0;
    if (criminal == -1) // -1 is forget all crimes witnessed
        for (tmp = 0; tmp < num_of_crimes; tmp++)
            if (crime_record[tmp].witness == find_player_in_table("", ch->specials2.idnum))
                count = 1;
    if (!(criminal == -1))
        for (tmp = 0; tmp < num_of_crimes; tmp++)
            if ((crime_record[tmp].witness == find_player_in_table("", ch->specials2.idnum)) && (crime_record[tmp].criminal == find_player_in_table("", criminal)))
                count = 1;
    if (!count)
        return;

    std::vector<crime_record_type> to_write;
    count = 0;

    for (tmp = 0; tmp < num_of_crimes; tmp++) {
        if (criminal == -1) // -1 is forget all - player has died etc
            not_write = !(crime_record[tmp].witness == find_player_in_table("", ch->specials2.idnum));
        else
            not_write = !((crime_record[tmp].witness == find_player_in_table("", ch->specials2.idnum)) && (crime_record[tmp].criminal == find_player_in_table("", criminal)));
        if (not_write) {
            crime_record_type record { };
            record.crime_time = crime_record[tmp].crime_time;
            record.criminal = (player_table + crime_record[tmp].criminal)->idnum;
            record.victim = (player_table + crime_record[tmp].victim)->idnum;
            record.witness = (player_table + crime_record[tmp].witness)->idnum;
            record.crime = crime_record[tmp].crime;
            record.witness_type = crime_record[tmp].witness_type;
            to_write.push_back(record);
            count++;
        }
    }

    // Phase 2a final-review Important 2: same fail-closed guard as
    // add_crime -- refuse to overwrite a present-but-malformed on-disk
    // store (see crime_json::crime_store_safe_to_overwrite's comment in
    // db.h for the rationale).
    const std::string crime_json_path = crime_json::crime_json_path(CRIME_FILE);
    std::string safety_error;
    if (!crime_json::crime_store_safe_to_overwrite(crime_json_path, &safety_error)) {
        log(("SYSERR: Crime JSON file '" + crime_json_path + "' is malformed, refusing to overwrite: " + safety_error)
                );
        return;
    }

    std::string write_error;
    if (!crime_json::write_crime_json_store(crime_json_path, to_write, &write_error))
        return;

    log(std::format("Crimes rewritten:{}.", count));
    RELEASE(crime_record);
    read_crime_file();
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

void write_exploits(char_data* ch, exploit_record* record)
{
    if (ch != nullptr && ch->desc != nullptr && *ch->desc->account_name != '\0') {
        account::AccountData account_data;
        std::string account_error;
        if (account::read_account_file(".", ch->desc->account_name, &account_data,
                &account_error)
            && !account::account_has_character(account_data, GET_NAME(ch))) {
            return;
        }
    }

    std::string error_message;
    if (!write_exploit_record_for_character(".", GET_NAME(ch), *record, &error_message)) {
        strcpy(buf, std::format("**ERROR: Could not persist exploit file for character: {}", error_message).c_str());
        mudlog(buf, NRM, LEVEL_IMMORT, TRUE);
    } else {
        // Anti-rollback: an exploit record (PK -> killer; death/level/stat/birth/... -> victim)
        // marks a state-changing event. Persist the character immediately after the CONFIRMED write
        // so a crash before the next autosave snapshot cannot roll the event back. Gated on the
        // successful write only -- not the orphaned-account early return above, nor a logged write
        // failure.
        save_char(ch, NOWHERE, 0);
    }
}

namespace {

void set_db_error(std::string* error_message, std::string_view message)
{
    if (error_message)
        error_message->assign(rots::text::truncate_at_null(message));
}

// Outcome of a one-time legacy '<name>.exploits' -> '<name>.exploits.json'
// conversion attempt (see convert_legacy_runtime_exploit_file below).
// Distinguishing these matters to the caller: a malformed *legacy file*
// content) has no authoritative alternative and is safe to discard, but an
// environmental failure (I/O error reading it, disk full / a stale ".tmp"
// blocking the write, ...) says nothing about whether the legacy file's
// content is good -- deleting it in that case would destroy a healthy
// character's exploit history over a transient problem.
enum class LegacyExploitConversionOutcome {
    kSuccess, // Converted (and, unless the trailing rename itself failed, retired the legacy file).
    kContentCorrupt, // Legacy bytes are malformed (size isn't a multiple of the record size):
                     // unrecoverable, safe to discard.
    kInfraFailure, // Read/verify/write failure unrelated to the legacy file's content: leave the
                   // legacy file untouched.
};

// Forward declarations: Task 6's runtime-exploit-file JSON helpers are
// defined below (near open_secure_temp_output_file, which they build on),
// but load_exploit_history_bytes (defined above that point) needs to call
// them.
std::string exploits_json_path_for_legacy(std::string_view legacy_path);
LegacyExploitConversionOutcome
convert_legacy_runtime_exploit_file(std::string_view legacy_path,
    std::vector<exploit_record>* decoded_records,
    std::string* error_message);

bool read_binary_file_contents(std::string_view path, std::string* contents,
    std::string* error_message)
{
    if (contents == nullptr) {
        set_db_error(error_message, "Output buffer must not be null.");
        return false;
    }

    const std::string path_owner(rots::text::truncate_at_null(path));
    FILE* file = std::fopen(path_owner.c_str(), "rb");
    if (file == nullptr) {
        set_db_error(error_message,
            "Failed to open file '" + path_owner + "': " + std::string(strerror(errno)));
        return false;
    }

    contents->clear();
    char buffer[1024];
    while (true) {
        const size_t bytes_read = std::fread(buffer, sizeof(char), sizeof(buffer), file);
        if (bytes_read > 0)
            contents->append(buffer, bytes_read);

        if (bytes_read < sizeof(buffer)) {
            if (std::ferror(file)) {
                std::fclose(file);
                set_db_error(error_message, "Failed to read file '" + path_owner + "'.");
                return false;
            }
            break;
        }
    }

    std::fclose(file);
    set_db_error(error_message, "");
    return true;
}

bool load_exploit_history_bytes(std::string_view root_directory,
    std::string_view character_name, std::string* bytes,
    std::string* error_message)
{
    if (bytes == nullptr) {
        set_db_error(error_message, "Exploit history output buffer must not be null.");
        return false;
    }

    std::string owner_account_name;
    if (!account::find_linked_character_owner_account(root_directory, character_name,
            &owner_account_name, error_message))
        return false;

    if (!owner_account_name.empty()) {
        std::vector<exploit_record> account_records;
        if (account::read_account_exploit_file(root_directory, owner_account_name, character_name,
                &account_records, error_message)) {
            if (!exploits_json::exploit_records_to_binary(account_records, bytes, error_message))
                return false;

            const std::string runtime_path = account::legacy_exploits_file_path(root_directory, character_name);
            if (std::remove(runtime_path.c_str()) != 0 && errno != ENOENT) {
                set_db_error(error_message, "Failed to retire legacy exploit file '" + runtime_path + "': " + std::string(strerror(errno)));
                return false;
            }

            return true;
        }

        const std::string read_error = error_message ? *error_message : "";
        bool account_file_exists = false;
        std::string inspect_error;
        if (!account::inspect_account_exploit_file(root_directory, owner_account_name,
                character_name, &account_file_exists,
                &inspect_error)) {
            set_db_error(error_message, inspect_error);
            return false;
        }
        if (account_file_exists) {
            set_db_error(error_message, read_error);
            return false;
        }

        set_db_error(error_message, "");
    }

    // Phase 2a Task 6: this runtime/non-account-native path now prefers
    // '<name>.exploits.json'; a legacy '<name>.exploits' binary file is a
    // one-time conversion fallback. This is reached both by truly
    // non-linked characters and by linked-but-not-yet-account-native
    // characters (see the fall-through above) -- both cases share the same
    // on-disk runtime file, so both benefit from the same JSON storage.
    const std::string runtime_path = account::legacy_exploits_file_path(root_directory, character_name);
    const std::string runtime_json_path = exploits_json_path_for_legacy(runtime_path);

    FILE* json_file = std::fopen(runtime_json_path.c_str(), "rb");
    if (json_file != nullptr) {
        std::fclose(json_file);
        std::string json_text;
        if (!read_binary_file_contents(runtime_json_path, &json_text, error_message))
            return false;

        exploits_json::ExploitHistoryData history;
        std::string json_error;
        if (!exploits_json::deserialize_exploits_from_json(json_text, &history, &json_error)) {
            // Fail closed on malformed authoritative JSON: don't silently
            // discard runtime exploit history (matches the account-native
            // JSON handling above).
            set_db_error(error_message, "Exploit JSON file '" + runtime_json_path + "' is malformed: " + json_error);
            return false;
        }

        if (!exploits_json::exploit_records_to_binary(history.records, bytes, error_message))
            return false;

        set_db_error(error_message, "");
        return true;
    }
    if (errno != ENOENT) {
        set_db_error(error_message, "Failed to open exploit file '" + runtime_json_path + "': " + std::string(strerror(errno)));
        return false;
    }

    FILE* runtime_file = std::fopen(runtime_path.c_str(), "rb");
    if (runtime_file != nullptr) {
        std::fclose(runtime_file);

        std::vector<exploit_record> decoded_records;
        std::string convert_error;
        const LegacyExploitConversionOutcome outcome = convert_legacy_runtime_exploit_file(runtime_path, &decoded_records, &convert_error);

        if (outcome == LegacyExploitConversionOutcome::kSuccess || outcome == LegacyExploitConversionOutcome::kInfraFailure) {
            // kSuccess: the JSON store now holds this data (converted and,
            // barring a failed retirement rename, the legacy file is gone).
            // kInfraFailure: an environmental problem (read I/O error,
            // verify mismatch, or a write failure such as disk-full or an
            // unresolvable stale ".tmp") -- NOT evidence the legacy file's
            // *content* is bad. Leave the legacy file untouched and decode
            // in-memory for this load only (decoded_records already holds
            // the legacy content whenever the initial decode succeeded;
            // it's only empty here if we couldn't even read the file, which
            // this load treats as "no history yet" without destroying
            // anything on disk). Conversion is retried on the next login.
            if (!exploits_json::exploit_records_to_binary(decoded_records, bytes, error_message))
                return false;
            set_db_error(error_message, "");
            return true;
        }

        // kContentCorrupt: unconvertible legacy binary (size isn't a
        // multiple of the record size). Matches the pre-existing behavior
        // of discarding a corrupt runtime file rather than blocking
        // forever -- there is no authoritative alternative to fail closed
        // to, for a character with no account-native history.
        if (std::remove(runtime_path.c_str()) != 0 && errno != ENOENT) {
            set_db_error(error_message, "Failed to remove malformed exploit file '" + runtime_path + "': " + std::string(strerror(errno)));
            return false;
        }
    } else if (errno != ENOENT) {
        set_db_error(error_message, "Failed to open exploit file '" + runtime_path + "': " + std::string(strerror(errno)));
        return false;
    }

    bytes->clear();
    set_db_error(error_message, "");
    return true;
}

FILE* open_secure_temp_output_file(std::string_view path, std::string* error_message)
{
    const std::string path_owner(rots::text::truncate_at_null(path));
#if defined PREDEF_PLATFORM_LINUX
    const int file_descriptor = open(path_owner.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (file_descriptor < 0) {
        set_db_error(error_message, "Failed to open temporary exploit file '" + path_owner + "': " + std::string(strerror(errno)));
        return nullptr;
    }

    FILE* file = fdopen(file_descriptor, "wb");
    if (file == nullptr) {
        close(file_descriptor);
        std::remove(path_owner.c_str());
        set_db_error(error_message,
            "Failed to create stream for temporary exploit file '" + path_owner + "'.");
        return nullptr;
    }

    set_db_error(error_message, "");
    return file;
#elif defined PREDEF_PLATFORM_WINDOWS
    // Real Windows implementation (Phase 3 Task 6; previously a documented
    // gap, Phase 3 Task 5). _sopen_s's _O_CREAT|_O_EXCL pairing is the CRT's
    // direct equivalent of O_CREAT|O_EXCL: it atomically fails with EEXIST if
    // `path` already exists, giving the same "detect a stale/racing writer"
    // guarantee the POSIX branch's O_EXCL provides. _S_IREAD|_S_IWRITE is the
    // closest pmode equivalent of 0600 (Windows' ACL-based permission model
    // has no owner/group/other bits to map this onto exactly). There is
    // deliberately no O_NOFOLLOW equivalent here: NTFS reparse points
    // (symlinks/junctions) are a different attack surface than POSIX
    // symlinks, and _O_CREAT|_O_EXCL already refuses to write through *any*
    // pre-existing path (reparse point or not) -- the property this function
    // actually needs (never silently overwrite something already at `path`)
    // holds without a separate check.
    int file_descriptor = -1;
    const errno_t open_error = _sopen_s(&file_descriptor, path_owner.c_str(), _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY,
        _SH_DENYNO, _S_IREAD | _S_IWRITE);
    if (open_error != 0) {
        set_db_error(error_message, "Failed to open temporary exploit file '" + path_owner + "': " + std::string(strerror(open_error)));
        return nullptr;
    }

    FILE* file = _fdopen(file_descriptor, "wb");
    if (file == nullptr) {
        _close(file_descriptor);
        std::remove(path_owner.c_str());
        set_db_error(error_message,
            "Failed to create stream for temporary exploit file '" + path_owner + "'.");
        return nullptr;
    }

    set_db_error(error_message, "");
    return file;
#endif
}

// Phase 2a Task 6: the runtime (non-account-linked) exploit history file
// moves from a raw exploit_record binary dump to JSON, reusing
// exploits_json's existing serialization (already used by the
// account-linked storage path). '<name>.exploits.json' is the live store;
// a legacy '<name>.exploits' binary file is a one-time conversion fallback
// (decode -> serialize -> re-decode -> field-equality verify -> write JSON
// -> rename legacy to '.migrated'), mirroring the mail_json/boards_json/
// pkill_json/crime_json converters. Account-linked behavior (the
// `!owner_account_name.empty()` branches above/below) is unchanged.
std::string exploits_json_path_for_legacy(std::string_view legacy_path)
{
    return std::string(rots::text::truncate_at_null(legacy_path)) + ".json";
}

// Generalizes open_secure_temp_output_file's temp+rename write for text
// (JSON) content, rather than exploit_record binary bytes.
bool write_text_file_atomically(std::string_view path, std::string_view contents,
    std::string* error_message)
{
    const std::string path_owner(rots::text::truncate_at_null(path));
    contents = rots::text::truncate_at_null(contents);
    const std::string temp_path = path_owner + ".tmp";

    FILE* file = open_secure_temp_output_file(temp_path, error_message);
    if (file == nullptr)
        return false;

    const size_t bytes_written = contents.empty() ? 0 : std::fwrite(contents.data(), sizeof(char), contents.size(), file);
    const int close_result = std::fclose(file);

    if (bytes_written != contents.size() || close_result != 0) {
        std::remove(temp_path.c_str());
        set_db_error(error_message, "Failed to write temporary exploit file '" + temp_path + "'.");
        return false;
    }

    if (rots_rename_replace(temp_path, path_owner) != 0) {
        std::remove(temp_path.c_str());
        set_db_error(error_message, "Failed to move temporary exploit file into place: " + std::string(strerror(errno)));
        return false;
    }

    set_db_error(error_message, "");
    return true;
}

// Writes `contents` to `path` via write_text_file_atomically, retrying once
// if that fails because a stale '<path>.tmp' is blocking O_EXCL. Only
// convert_legacy_runtime_exploit_file (below) calls this: it is converting
// our *own* legacy runtime file as a one-time background step, so a leftover
// tmp here is almost certainly debris from a previous interrupted conversion
// attempt, not attacker-controlled input -- safe to clear and retry.
// write_exploit_record_for_character's write path (the character actually
// playing right now) must keep failing closed on a pre-existing tmp instead
// (see DbLoader.FailsClosedWhenTemporaryExploitPathAlreadyExists), so it
// calls write_text_file_atomically directly rather than through this helper.
bool write_text_file_atomically_clearing_stale_tmp(std::string_view path,
    std::string_view contents,
    std::string* error_message)
{
    if (write_text_file_atomically(path, contents, error_message))
        return true;

    const std::string temp_path = std::string(rots::text::truncate_at_null(path)) + ".tmp";
#if defined PREDEF_PLATFORM_LINUX
    struct stat temp_stat { };
    // lstat (not stat): only clear a stale plain file, never follow/remove a
    // symlink planted at the tmp path.
    if (lstat(temp_path.c_str(), &temp_stat) != 0 || !S_ISREG(temp_stat.st_mode))
        return false;
#elif defined PREDEF_PLATFORM_WINDOWS
    // Real Windows implementation (Phase 3 Task 6; open_secure_temp_output_file
    // above now actually works on this platform, so this retry path is
    // reachable here too, unlike when this was a documented gap in Task 5).
    // GetFileAttributesA is the Windows equivalent of the POSIX branch's
    // lstat(): it reports FILE_ATTRIBUTE_REPARSE_POINT for the *final*
    // path component without following it if that component is itself a
    // reparse point (symlink/junction) -- the same "don't remove/follow a
    // symlink planted at the tmp path" property lstat()+S_ISREG gives the
    // POSIX branch. Only clear a plain file (reject missing paths,
    // directories, and reparse points).
    const DWORD attributes = GetFileAttributesA(temp_path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return false;
#endif

    if (std::remove(temp_path.c_str()) != 0)
        return false;

    return write_text_file_atomically(path, contents, error_message);
}

// One-time conversion of a legacy '<name>.exploits' binary file to
// '<name>.exploits.json'. On kSuccess, returns the decoded records (the
// caller already has the bytes decoded once here, no need to re-read). See
// LegacyExploitConversionOutcome for what each outcome means to the caller
// and why they must be handled differently -- in short: kContentCorrupt is
// the only outcome where it is safe to discard the legacy file.
LegacyExploitConversionOutcome
convert_legacy_runtime_exploit_file(std::string_view legacy_path,
    std::vector<exploit_record>* decoded_records,
    std::string* error_message)
{
    std::string legacy_bytes;
    if (!read_binary_file_contents(legacy_path, &legacy_bytes, error_message)) {
        // Could not even read the legacy file (I/O error, permissions,
        // ...): an environmental failure that says nothing about whether
        // the file's *content* is good. decoded_records is left empty by
        // the caller's own vector default-construction; don't delete a file
        // we couldn't read.
        return LegacyExploitConversionOutcome::kInfraFailure;
    }

    if (!exploits_json::exploit_records_from_binary(legacy_bytes, decoded_records, error_message)) {
        set_db_error(error_message,
            "Decode failed: " + (error_message ? *error_message : std::string()));
        return LegacyExploitConversionOutcome::kContentCorrupt;
    }

    exploits_json::ExploitHistoryData history;
    history.records = *decoded_records;
    const std::string json = exploits_json::serialize_exploits_to_json(history);

    // Verify (binding conversion contract): re-decode the freshly serialized
    // JSON and compare it field-for-field to the original decode. A
    // mismatch here means our own serializer/deserializer disagree with
    // themselves -- decoded_records above already holds a successful decode
    // of the legacy file, so this is an internal/environmental problem, not
    // evidence the legacy file's content is corrupt.
    exploits_json::ExploitHistoryData reparsed;
    std::string verify_error;
    if (!exploits_json::deserialize_exploits_from_json(json, &reparsed, &verify_error)) {
        set_db_error(error_message,
            "Verify-decode of freshly serialized JSON failed: " + verify_error);
        return LegacyExploitConversionOutcome::kInfraFailure;
    }
    if (!exploits_json::exploit_records_equal(*decoded_records, reparsed.records)) {
        set_db_error(error_message,
            "Verify mismatch: re-decoded JSON does not equal the original legacy decode.");
        return LegacyExploitConversionOutcome::kInfraFailure;
    }

    const std::string json_path = exploits_json_path_for_legacy(legacy_path);
    std::string write_error;
    if (!write_text_file_atomically_clearing_stale_tmp(json_path, json, &write_error)) {
        // Write failure (disk full, permissions, or a stale '.tmp' that
        // couldn't be cleared): environmental, not content corruption.
        // decoded_records already holds this load's data; leave the legacy
        // file alone and let conversion retry next login.
        set_db_error(error_message, write_error);
        return LegacyExploitConversionOutcome::kInfraFailure;
    }

    const std::string legacy_path_owner(rots::text::truncate_at_null(legacy_path));
    const std::string migrated_path = legacy_path_owner + ".migrated";
    if (rots_rename_replace(legacy_path_owner, migrated_path) != 0) {
        // JSON is written and verified; the legacy file simply couldn't be
        // retired (matches the other Task 6 converters' "partial success"
        // contract -- report but don't fail, nothing is at risk).
        set_db_error(error_message, "Exploit file converted but legacy rename to '" + migrated_path + "' failed: " + std::string(strerror(errno)));
        return LegacyExploitConversionOutcome::kSuccess;
    }

    set_db_error(error_message, "");
    return LegacyExploitConversionOutcome::kSuccess;
}

} // namespace

bool load_exploit_records_for_character(std::string_view root_directory,
    std::string_view character_name,
    std::vector<exploit_record>* records,
    std::string* error_message)
{
    if (records == nullptr) {
        set_db_error(error_message, "Exploit record output vector must not be null.");
        return false;
    }

    std::string bytes;
    if (!load_exploit_history_bytes(root_directory, character_name, &bytes, error_message))
        return false;

    if (!exploits_json::exploit_records_from_binary(bytes, records, error_message)) {
        set_db_error(error_message, "Exploit history for '" + std::string(character_name) + "' is malformed.");
        return false;
    }

    return true;
}

bool write_exploit_record_for_character(std::string_view root_directory,
    std::string_view character_name,
    const exploit_record& record, std::string* error_message)
{
    std::vector<exploit_record> records;
    if (!load_exploit_records_for_character(root_directory, character_name, &records,
            error_message))
        return false;

    records.insert(records.begin(), record);

    std::string owner_account_name;
    if (!account::find_linked_character_owner_account(root_directory, character_name,
            &owner_account_name, error_message))
        return false;

    if (!owner_account_name.empty()) {
        if (!account::write_account_exploit_file(root_directory, owner_account_name, character_name,
                records, error_message))
            return false;

        const std::string runtime_path = account::legacy_exploits_file_path(root_directory, character_name);
        if (std::remove(runtime_path.c_str()) != 0 && errno != ENOENT) {
            set_db_error(error_message, "Failed to retire legacy exploit file '" + runtime_path + "': " + std::string(strerror(errno)));
            return false;
        }

        set_db_error(error_message, "");
        return true;
    }

    // Phase 2a Task 6: the non-linked runtime store is JSON now (reusing
    // exploits_json's serialization), written atomically to
    // '<name>.exploits.json'.
    exploits_json::ExploitHistoryData history;
    history.records = records;
    const std::string json = exploits_json::serialize_exploits_to_json(history);

    const std::string runtime_path = account::legacy_exploits_file_path(root_directory, character_name);
    const std::string runtime_json_path = exploits_json_path_for_legacy(runtime_path);

    if (!write_text_file_atomically(runtime_json_path, json, error_message))
        return false;

    // Defensive cleanup: load_exploit_records_for_character (called above)
    // will already have migrated/retired any legacy binary file it found,
    // but remove it here too in case one reappeared since then.
    if (std::remove(runtime_path.c_str()) != 0 && errno != ENOENT) {
        set_db_error(error_message, "Failed to retire legacy exploit file '" + runtime_path + "': " + std::string(strerror(errno)));
        return false;
    }

    set_db_error(error_message, "");
    return true;
}

bool load_object_save_data_for_character(std::string_view root_directory,
    std::string_view character_name,
    objects_json::ObjectSaveData* data,
    std::string* error_message)
{
    if (data == nullptr) {
        set_db_error(error_message, "Object-save output parameter must not be null.");
        return false;
    }

    std::string owner_account_name;
    if (!account::find_linked_character_owner_account(root_directory, character_name,
            &owner_account_name, error_message))
        return false;

    if (!owner_account_name.empty()) {
        if (account::read_account_object_data(root_directory, owner_account_name, character_name,
                data, error_message))
            return true;

        const std::string read_error = error_message ? *error_message : "";
        bool account_object_exists = false;
        std::string inspect_error;
        if (!account::inspect_account_object_file(root_directory, owner_account_name,
                character_name, &account_object_exists,
                &inspect_error)) {
            set_db_error(error_message, inspect_error);
            return false;
        }
        if (account_object_exists) {
            set_db_error(error_message, read_error);
            return false;
        }
    }

    const std::string runtime_path = account::legacy_object_file_path(root_directory, character_name);
    FILE* runtime_file = std::fopen(runtime_path.c_str(), "rb");
    if (runtime_file != nullptr) {
        std::fclose(runtime_file);

        std::string legacy_bytes;
        if (!read_binary_file_contents(runtime_path, &legacy_bytes, error_message))
            return false;

        // The one-time legacy decode this bridge retires everything else in
        // favor of: real .obj files on disk were written by the 32-bit game
        // and never change, so the portable explicit-offset decoder (not a
        // native-struct memcpy) is what actually reads them correctly on any
        // ABI. A decode failure here is a soft failure -- log and hand back a
        // fresh default so a corrupt legacy file degrades an account-backed
        // login to an empty inventory instead of rejecting it outright
        // (matches the pre-Task-1 interpre.cpp staging behavior).
        objects_json::ObjectSaveData decoded;
        bool accepted_missing_follower_section = false;
        std::string decode_error;
        if (!objects_json::legacy_object_save_data_from_binary(
                legacy_bytes, &decoded, &accepted_missing_follower_section, &decode_error)) {
            log(std::format("SYSERR: unable to decode account-staged object data for {}: {}",
                character_name, decode_error)
                    );
            *data = build_default_account_backed_object_data();
            set_db_error(error_message, "");
            return true;
        }

        *data = std::move(decoded);
        set_db_error(error_message, "");
        return true;
    }

    if (errno != ENOENT) {
        set_db_error(error_message, "Failed to open object file '" + runtime_path + "': " + std::string(strerror(errno)));
        return false;
    }

    *data = build_default_account_backed_object_data();
    set_db_error(error_message, "");
    return true;
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

int delete_exploits_file(char* name)
{
    char filename[70];
    char tname[60];
    char* tmpchar;
    char temp[100];
    strcpy(tname, name);
    for (tmpchar = tname; *tmpchar; tmpchar++)
        *tmpchar = tolower(*tmpchar);

    switch (tolower(*tname)) {
    case 'a':
    case 'b':
    case 'c':
    case 'd':
    case 'e':
        strcpy(filename,
            std::format("exploits/A-E/{}.exploits", static_cast<const char*>(tname)).c_str());
        break;
    case 'f':
    case 'g':
    case 'h':
    case 'i':
    case 'j':
        strcpy(filename,
            std::format("exploits/F-J/{}.exploits", static_cast<const char*>(tname)).c_str());
        break;
    case 'k':
    case 'l':
    case 'm':
    case 'n':
    case 'o':
        strcpy(filename,
            std::format("exploits/K-O/{}.exploits", static_cast<const char*>(tname)).c_str());
        break;
    case 'p':
    case 'q':
    case 'r':
    case 's':
    case 't':
        strcpy(filename,
            std::format("exploits/P-T/{}.exploits", static_cast<const char*>(tname)).c_str());
        break;
    case 'u':
    case 'v':
    case 'w':
    case 'x':
    case 'y':
    case 'z':
        strcpy(filename,
            std::format("exploits/U-Z/{}.exploits", static_cast<const char*>(tname)).c_str());
        break;
    default:
        strcpy(filename,
            std::format("exploits/ZZZ/{}.exploits", static_cast<const char*>(tname)).c_str());
        break;
    }
    strcpy(temp, std::format("Deleting trophy file: {}", static_cast<const char*>(tname)).c_str());
    mudlog(temp, NRM, LEVEL_IMMORT, TRUE);

    // no checks, because file might not even exist
    // std::remove (portable ISO C, <cstdio>) instead of the POSIX-only unlink() --
    // Phase 3 Task 5: identical semantics for deleting a plain file by path, and
    // works unchanged on Windows.
    if (std::remove(filename) < 0) {
        //   if (errno != ENOENT) { /* if it fails, NOT because of no file */
        //      sprintf(buf1, "SYSERR: deleting exploit file %s (2)", filename);
        //      perror(buf1);
        //   }
    }

    return (1);
}

int rename_char(struct char_data* ch, char* newname)
{
    char namebuf[64], *c, new_exploit_file[64], old_exploit_file[64];
    int player_i, i;

    if ((!*newname || !ch) || (find_player_in_table(newname, -1) != -1) || (!Crash_get_filename(GET_NAME(ch), buf)) || ((player_i = find_name(GET_NAME(ch))) < 0))
        return -1;

    /* note this in exploits, i hate the ! on NOTE, so we use ACHIEVEMENT */
    strcpy(namebuf, std::format("Name: {}->{}", GET_NAME(ch), newname).c_str());
    vmudlog(BRF, "%s namechanged: now known as %s.", GET_NAME(ch), newname);
    add_exploit_record(EXPLOIT_ACHIEVEMENT, ch, 0, namebuf);

    /* remove their char file */
    // Was system("rm <buf>"); the return value was never checked, so a
    // failed remove was already silent -- ec is discarded here to match.
    {
        std::error_code remove_ec;
        std::filesystem::remove(buf, remove_ec);
    }

    /* make the name file-ready */
    for (c = newname; *c; ++c)
        *c = tolower(unaccent(*c));

    /* get the name of the new exploit file */
    get_char_directory(newname, namebuf);
    strcpy(
        new_exploit_file,
        std::format("exploits{}{}.exploits", static_cast<const char*>(namebuf), newname).c_str());

    /* get the name of the old exploit file */
    get_char_directory(GET_NAME(ch), namebuf);
    strcpy(buf, GET_NAME(ch));
    for (c = buf; *c; ++c)
        *c = tolower(unaccent(*c));

    strcpy(old_exploit_file,
        std::format("exploits{}{}.exploits", static_cast<const char*>(namebuf),
            static_cast<const char*>(buf))
            .c_str());

    /* now move the exploits */
    // Was system("mv <old_exploit_file> <new_exploit_file>"); the return
    // value was never checked, so a failed move was already silent -- ec is
    // discarded here to match.
    {
        std::error_code rename_ec;
        std::filesystem::rename(old_exploit_file, new_exploit_file, rename_ec);
    }

    /* now remove their ch_file */
    // Was system("rm <ch_file>"); return value never checked -- same silent
    // discard as above.
    {
        std::error_code remove_ec;
        std::filesystem::remove(player_table[player_i].ch_file, remove_ec);
    }

    /* release the buffers in the player table and in their personal
     * char_data structure */
    RELEASE(player_table[player_i].name);
    RELEASE(ch->player.name);

    i = strlen(newname);
    /* make new buffers for the length of the new name */
    CREATE(player_table[player_i].name, char, i + 1);
    CREATE(ch->player.name, char, i + 1);

    /* first letter uppercase */
    *newname = toupper(*newname);
    /* assign and terminate */
    strncpy(player_table[player_i].name, newname, i);
    player_table[player_i].name[i] = 0;

    strncpy(ch->player.name, newname, i);
    ch->player.name[i] = 0;

    return 1;
}
