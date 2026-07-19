/* ************************************************************************
 *   File: comm.c                                        Part of CircleMUD *
 *  Usage: Communication, socket handling, main(), central game loop       *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#include <ctype.h>
#include <charconv>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "platdef.h"

// execinfo.h/sys/stat.h (Phase 3 Task 5, MSVC bring-up): both are POSIX/glibc-only
// -- <execinfo.h> for sigsegv_handler()'s backtrace()/backtrace_symbols_fd() crash
// dump (Windows has no equivalent API; CaptureStackBackTrace()+SymFromAddr() from
// dbghelp.lib is a different, much larger facility -- a documented gap below, not
// implemented here), <sys/stat.h> for main()'s umask() call (Windows has no umask
// concept at all; file permissions there are ACL-based, not a process-wide creation
// mask, so there is no drop-in translation -- also a documented gap below).
#if defined PREDEF_PLATFORM_LINUX
#include <execinfo.h>
#include <sys/stat.h>
#endif

#include <signal.h>
#include <string.h>

#include "big_brother.h"
#include "char_utils.h"
#include "color.h"
#include "comm.h"
#include "crashsave_schedule.h"
#include "db.h"
#include "entity_hooks.h"
#include "handler.h"
#include "interpre.h"
#include "limits.h"
#include "mudlle.h"
#include "output_seam.h"
#include "protocol.h"
#include "rots_net.h"
#include "rots_rng.h"
#include "script.h"
#include "skill_timer.h"
#include "spells.h"
#include "rots/core/character.h"
#include "rots/core/object.h"
#include "rots/core/room.h"
#include "rots/core/descriptor.h"
#include "rots/core/tables.h"
#include "rots/core/types.h"
#include "rots/platform/log.h"
#include "text_view.h"
#include "utils.h"
#include "warrior_spec_handlers.h"
#include "world_hooks.h"
#include "zone.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#define MAX_HOSTNAME 256
#define OPT_USEC 250000 /* time delay corresponding to 4 passes/sec */
#define DFLT_PORT 1024
#define MAX_PLAYERS 255
#define MAX_DESCRIPTORS_AVAILABLE 256

/* externs */
extern int restrict;
extern int mini_mud;
extern int new_mud;
extern int no_rent_check;
extern FILE* player_fl;
extern const std::string_view DFLT_DIR;
extern int mortal_start_room[];
extern struct room_data world; /* In db.c */
extern struct char_data* character_list; /* In db.c */
extern struct index_data* mob_index;
extern int top_of_world; /* In db.c */
extern struct time_info_data time_info; /* In db.c */
extern struct char_data* waiting_list; /* in db.cpp */
extern struct char_data* fast_update_list; /* in db.cpp */
extern char help[];
extern unsigned long stat_ticks_passed;
extern unsigned long stat_mortals_counter;
extern unsigned long stat_immortals_counter;
// Zenith's adjustment to show stats (whitie/darkie)
extern unsigned long stat_whitie_counter;
extern unsigned long stat_darkie_counter;
extern unsigned long stat_whitie_legend_counter;
extern unsigned long stat_darkie_legend_counter;
extern int get_percent_absorb(char_data* character);

/* local globals */
struct descriptor_data *descriptor_list = 0, *next_to_process = 0;
struct txt_block* bufpool = 0; /* pool of large output buffers */
int buf_largecount; /* # of large buffers which exist */
int buf_overflows; /* # of overflows of output */
int buf_switches; /* # of switches from small to large buf */
int circle_shutdown = 0; /* clean shutdown */
int circle_reboot = 0; /* reboot the game after a shutdown */
int no_specials = 0; /* Suppress ass. of special routines */
int last_desc = 0; /* last unique num assigned to a desc. */
SocketType mother_desc = 0; /* file desc of the mother connection */
SocketType maxdesc; /* highest desc num used */
int avail_descs; /* max descriptors available */
int tics = 0; /* for extern checkpointing */
int has_proxy; /* Game expects to be proxied */

FILE* fpCommand; // DEBUGGING
int iCommands = 0;

struct txt_block* txt_block_pool = 0;
int txt_block_counter = 0;

extern int nameserver_is_slow; /* see config.c */
extern int autosave_time; /* see config.c */
extern const std::string_view GREETINGS;

int process_output(struct descriptor_data* t);
int isbanned(char* hostname);

namespace {

bool parse_port_value(std::string_view text, sh_int* port, std::string* error_message)
{
    text = rots::text::truncate_at_null(text);
    if (text.empty()) {
        if (error_message)
            *error_message = "Port argument expected after option -p.";
        return false;
    }

    size_t digit_count = 0;
    while (digit_count < text.size() && text[digit_count] >= '0' && text[digit_count] <= '9')
        ++digit_count;

    if (digit_count == 0) {
        if (error_message)
            *error_message = "Illegal port #";
        return false;
    }

    int parsed_port = 0;
    const auto parse_result =
        std::from_chars(text.data(), text.data() + digit_count, parsed_port);
    if (parse_result.ec != std::errc()) {
        if (error_message)
            *error_message = "Illegal port #";
        return false;
    }
    if (parsed_port <= 1024) {
        if (error_message)
            *error_message = "Illegal port #";
        return false;
    }

    *port = static_cast<sh_int>(parsed_port);
    return true;
}

void populate_descriptor_host(descriptor_data* descriptor, uint32_t peer_address)
{
    // getnameinfo() replaces the historical gethostbyaddr() call here (portable
    // POSIX+Winsock, no new dependency) -- see rots_net::resolve_host_name.
    // Same fallback-to-numeric-IP behavior on any resolution failure.
    char resolved_host[MAX_HOSTNAME + 1];
    if (nameserver_is_slow || !rots_net::resolve_host_name(peer_address, resolved_host, sizeof(resolved_host))) {
        if (!nameserver_is_slow)
            log("Reverse hostname lookup failed; using numeric address.");
        const int i = peer_address;
        strcpy(descriptor->host,
            std::format("{}.{}.{}.{}", (i & 0x000000FF), (i & 0x0000FF00) >> 8, (i & 0x00FF0000) >> 16,
                (i & 0xFF000000) >> 24)
                .c_str());
        return;
    }

    strncpy(descriptor->host, resolved_host, 49);
    *(descriptor->host + 49) = '\0';
}

int send_initial_login_output(descriptor_data* descriptor)
{
    ProtocolNegotiate(descriptor);
    SEND_TO_Q(GREETINGS, descriptor);
    SEND_TO_Q("Account email: ", descriptor);
    if (*(descriptor->output) && process_output(descriptor) < 0)
        return -1;
    return 1;
}

bool reject_banned_descriptor_host(descriptor_data* descriptor)
{
    if (descriptor == nullptr || isbanned(descriptor->host) != BAN_ALL)
        return false;

    if (strcmp(descriptor->host, "shrout.org")) {
        mudlog(
                   std::format("Connection attempt denied from [{}]", static_cast<const char*>(descriptor->host))
                       ,
            NRM, LEVEL_GOD, TRUE);
    }

    return true;
}

int finish_proxy_header_if_ready(descriptor_data* descriptor)
{
    if (descriptor == nullptr || !descriptor->waiting_for_proxy_header)
        return 1;

    char* buffer = reinterpret_cast<char*>(&descriptor->proxy_peer_address);
    while (descriptor->proxy_peer_bytes_read < sizeof(descriptor->proxy_peer_address)) {
        const rots_net::ssize_type bytes_read = rots_net::read_socket(descriptor->descriptor,
            buffer + descriptor->proxy_peer_bytes_read,
            sizeof(descriptor->proxy_peer_address) - descriptor->proxy_peer_bytes_read);
        if (bytes_read > 0) {
            descriptor->proxy_peer_bytes_read += static_cast<byte>(bytes_read);
            continue;
        }

        if (bytes_read == 0) {
            log("EOF encountered while reading proxy header.");
            return -1;
        }

        if (rots_net::error_is_would_block(rots_net::last_error()))
            return 0;

        perror("reading proxy header");
        return -1;
    }

    populate_descriptor_host(descriptor, descriptor->proxy_peer_address);
    if (reject_banned_descriptor_host(descriptor))
        return -1;
    descriptor->waiting_for_proxy_header = false;
    return send_initial_login_output(descriptor);
}

} // namespace

#ifdef TESTING
bool parse_port_value_for_testing(
    std::string_view text, sh_int* port, std::string* error_message)
{
    return parse_port_value(text, port, error_message);
}
#endif

/* functions in this file */
int get_from_q(struct txt_q* queue, char* dest);
void run_the_game(sh_int port);
void game_loop(SocketType s);
SocketType init_socket(sh_int port);
SocketType pnew_connection(SocketType s);
SocketType pnew_descriptor(SocketType s);
int process_output(struct descriptor_data* t);
int process_input(struct descriptor_data* t);
void close_sockets(SocketType s);
void flush_queues(struct descriptor_data* d);
int perform_subst(struct descriptor_data* t, char* orig, char* subst);
void complete_delay(struct char_data* ch);
void stat_update();

bool parse_startup_options(int argc, char** argv, StartupOptions* options, std::string* error_message)
{
    if (options == nullptr)
        return false;

    StartupOptions parsed_options {};
    parsed_options.port = DFLT_PORT;
    parsed_options.dir = DFLT_DIR;
    parsed_options.mini_mud = false;
    parsed_options.new_mud = false;
    parsed_options.no_rent_check = false;
    parsed_options.restrict_game = false;
    parsed_options.no_specials = false;
    parsed_options.has_proxy = false;

    bool port_specified = false;
    int pos = 1;

    while ((pos < argc) && (*(argv[pos]) == '-')) {
        switch (*(argv[pos] + 1)) {
        case 'd':
            if (*(argv[pos] + 2))
                parsed_options.dir = argv[pos] + 2;
            else if (++pos < argc)
                parsed_options.dir = argv[pos];
            else {
                if (error_message)
                    *error_message = "Directory arg expected after option -d.";
                return false;
            }
            break;
        case 'm':
            parsed_options.mini_mud = true;
            parsed_options.no_rent_check = true;
            break;
        case 'n':
            parsed_options.new_mud = true;
            parsed_options.no_rent_check = true;
            break;
        case 'q':
            parsed_options.no_rent_check = true;
            break;
        case 'r':
            parsed_options.restrict_game = true;
            break;
        case 's':
            parsed_options.no_specials = true;
            break;
        case 'p': {
            const char* port_text = nullptr;
            if (*(argv[pos] + 2))
                port_text = argv[pos] + 2;
            else if (++pos < argc)
                port_text = argv[pos];
            else {
                if (error_message)
                    *error_message = "Port argument expected after option -p.";
                return false;
            }

            if (!parse_port_value(port_text, &parsed_options.port, error_message))
                return false;
            port_specified = true;
            break;
        }
        case 'x':
            parsed_options.has_proxy = true;
            break;
        default:
            if (error_message) {
                *error_message = std::format("SYSERR: Unknown option -{} in argument string.", *(argv[pos] + 1));
            }
            return false;
        }
        pos++;
    }

    if (pos < argc) {
        if (port_specified) {
            if (error_message)
                *error_message = "Unexpected extra argument after startup options.";
            return false;
        }
        if (!parse_port_value(argv[pos], &parsed_options.port, error_message))
            return false;
        pos++;
    }

    if (pos < argc) {
        if (error_message)
            *error_message = "Unexpected extra argument after startup options.";
        return false;
    }

    *options = parsed_options;
    return true;
}

/* extern fcnts */
void boot_db(void);
void affect_update(void); /* In spells.c */
void fast_update(void); /* In spells.c */
void point_update(void); /* In limits.c */
void mobile_activity(void);
void string_add(struct descriptor_data* d, char* str);
void perform_violence(int);
void show_string(struct descriptor_data* d, char* input);
void check_reboot(void);
int isbanned(char* hostname);
void weather_and_time(int mode);
void* virt_program_number(int number);
void* virt_obj_program_number(int number);
void replace_aliases(char_data* ch, char* line);

// int gethostname(char *, int);

namespace {

bool is_secret_input_state(int connection_state)
{
    switch (connection_state) {
    case CON_PWDNRM:
    case CON_PWDGET:
    case CON_PWDCNF:
    case CON_PWDNQO:
    case CON_PWDNEW:
    case CON_PWDNCNF:
    case CON_ACCTPWD:
    case CON_ACCTLINKPWD:
    case CON_ACCTNEWPWD:
    case CON_ACCTNEWPWDCNF:
    case CON_ACCTRESETOLD:
    case CON_ACCTRESETNEW:
    case CON_ACCTRESETCNF:
    case CON_ACCTLEGPWD:
    case CON_ACCTVERIFY:
    case CON_DELCNF1:
    case CON_ACCTDELCNF1:
        return true;
    default:
        return false;
    }
}

// Emulates POSIX `touch`: creates `path` empty if it doesn't exist, or bumps
// its modification time if it does, without disturbing existing content.
// Replaces the system("touch <path>") site below; that call's return value
// was never checked, so failures here are equally silent (errors discarded).
// A duplicate of act_wiz.cpp's helper of the same name -- this codebase's
// established convention is a private per-TU copy of small file-op helpers
// rather than new cross-TU coupling (see convert_plrobjs.cpp/convert_exploits.cpp).
void touch_file(std::string_view path)
{
    const std::string path_owner(rots::text::truncate_at_null(path));
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(path_owner, ec))
        std::ofstream(path_owner, std::ios::out).close();

    fs::last_write_time(path_owner, fs::file_time_type::clock::now(), ec);
}

} // namespace

ACMD(do_cast);
SPECIAL(intelligent);
const std::string_view wait_wheel[8] = { "\r|\r", "\r\\\r", "\r-\r", "\r/\r", "\r|\r", "\r\\\r", "\r-\r", "\r/\r" };

void sigsegv_handler(int sig)
{
    fprintf(stderr, "Error: signal %d:\n", sig);

#if defined PREDEF_PLATFORM_LINUX
    void* array[10];
    // get void*'s for all entries on the stack
    size_t size = backtrace(array, 10);
    // print out all the frames to stderr
    backtrace_symbols_fd(array, size, STDERR_FILENO);
#elif defined PREDEF_PLATFORM_WINDOWS
    // No backtrace on Windows in this phase -- CaptureStackBackTrace()+SymFromAddr()
    // (dbghelp.lib) is the real equivalent but is a substantially larger facility
    // (needs symbol-handler init/cleanup, a .pdb, etc.) than a like-for-like drop-in;
    // left as a Windows operational gap (crash still exits cleanly, just without a
    // stack dump) rather than half-implemented here. Phase 5/later-bring-up
    // candidate.
    fprintf(stderr, "(stack backtrace unavailable on Windows in this build)\n");
#endif

    exit(1);
}

/* *********************************************************************
 *  main game loop and related stuff				       *
 ********************************************************************* */
#ifndef TESTING
int main(int argc, char** argv)
{
    signal(SIGSEGV, sigsegv_handler);

    // WSAStartup/WSACleanup lifetime pairing on Windows; no-op on POSIX. Must
    // happen before any socket call -- see run_the_game()'s matching shutdown().
    rots_net::startup();

    // initialize the random number generator
    rots_rng::seed(static_cast<unsigned int>(std::time(0)));

    StartupOptions startup_options {};
    std::string parse_error;

#if defined PREDEF_PLATFORM_LINUX
    /* lets put the rots process in rwxrwx--- file mode */
    umask(S_IRWXO);
#elif defined PREDEF_PLATFORM_WINDOWS
    // Windows has no umask()/process-wide creation-mask concept -- file permissions
    // there are ACL-based, not mode-bit-based, so there is no drop-in translation.
    // Documented Windows operational gap: files this process creates get whatever
    // default ACL the OS/filesystem assigns, not an explicit rwxrwx--- equivalent.
#endif

    if (!parse_startup_options(argc, argv, &startup_options, &parse_error)) {
        if (!parse_error.empty())
            log(parse_error);
        fprintf(stderr, "Usage: %s [-m] [-q] [-r] [-s] [-x] [-d pathname] [-p port #] [ port # ]\n",
            argv[0]);
        exit(0);
    }

    has_proxy = startup_options.has_proxy ? 1 : 0;
    mini_mud = startup_options.mini_mud ? 1 : 0;
    new_mud = startup_options.new_mud ? 1 : 0;
    no_rent_check = startup_options.no_rent_check ? 1 : 0;
    restrict = startup_options.restrict_game ? 1 : 0;
    no_specials = startup_options.no_specials ? 1 : 0;

    if (mini_mud)
        log("Running in minimized mode & with no rent check.");
    if (new_mud)
        log("Running in pnew mode & with no rent check.");
    if (!startup_options.mini_mud && !startup_options.new_mud && no_rent_check)
        log("Quick boot mode -- rent check supressed.");
    if (restrict)
        log("Restricting game -- no pnew players allowed.");
    if (no_specials)
        log("Suppressing assignment of special routines.");
    if (has_proxy)
        log("Expecting proxy server.");

    /* Create the pidfile and log some info */
    // Was system("echo <pid> > .ageland.pid"); the return value was never
    // checked, so a failed write was already silent -- an unopenable file
    // is equally silent here (the stream's failure is simply not checked).
    std::ofstream(".ageland.pid", std::ios::out) << getpid() << "\n";
    log(std::format("Running game as pid {}.", getpid()));

    log(std::format("Running game on port {}.", startup_options.port));

    // std::filesystem::current_path (the setter overload) is the portable stand-in
    // for chdir() (Phase 3 Task 5, MSVC bring-up round 2 -- found via the
    // windows-msvc CI log): works identically on POSIX and Windows.
    {
        std::error_code chdir_error;
        std::filesystem::current_path(startup_options.dir, chdir_error);
        if (chdir_error) {
            fprintf(stderr, "Fatal error changing to data directory: %s\n", chdir_error.message().c_str());
            exit(0);
        }
    }

    log(std::format("Using {} as data directory.", startup_options.dir));

    // Open command log
    // Was system("mv -f last_cmds crash_cmds"); the return value was never
    // checked, so a failed move (e.g. last_cmds missing on first boot) was
    // already silent -- ec is discarded here to match. fs::rename already
    // overwrites an existing crash_cmds, matching "-f".
    {
        std::error_code rename_ec;
        std::filesystem::rename("last_cmds", "crash_cmds", rename_ec);
    }
    fpCommand = fopen("last_cmds", "w");
    rots_rng::seed(static_cast<unsigned int>(time(0)));
    run_the_game(startup_options.port);
    return (0);
}
#endif

// TODO(drelidan):  Move this into a place that makes sense.  We're cooking pasta!
std::vector<char_data*> specialized_mages;

// Installs the mudlog broadcast sink: the LEVEL_AREAGOD clamp, PRF_LOG*
// preference gating, descriptor_list walk, and CGRN color framing that used
// to be the second half of mudlog()'s body (utility.cpp, pre logging-seam).
// rots::log::write() (rots_log.cpp) already handles the file-write branch and
// the level < 0 early return, so this sink only ever sees a message that
// should broadcast -- level arrives UNclamped, exactly as mudlog received it;
// the clamp below is what mudlog used to apply before walking
// descriptor_list.
void register_mudlog_broadcast_sink()
{
    rots::log::set_sink([](std::string_view message_body, char type, int level) {
        struct descriptor_data* connection;
        char log_preference;

        if (level < LEVEL_AREAGOD) {
            level = LEVEL_AREAGOD;
        }

        const std::string message = std::format("[ {} ]\n\r", message_body);

        for (connection = descriptor_list; connection; connection = connection->next) {
            if (!connection->connected && !PLR_FLAGGED(connection->character, PLR_WRITING)) {
                log_preference = ((PRF_FLAGGED(connection->character, PRF_LOG1) ? 1 : 0) + (PRF_FLAGGED(connection->character, PRF_LOG2) ? 2 : 0) + (PRF_FLAGGED(connection->character, PRF_LOG3) ? 4 : 0));

                if ((GET_LEVEL(connection->character) >= level) && (log_preference >= type)) {
                    send_to_char(CC_FIX(connection->character, CGRN), connection->character);
                    send_to_char(message, connection->character);
                    send_to_char(CC_NORM(connection->character), connection->character);
                }
            }
        }
    });
}

/* Init sockets, run game, and cleanup sockets */
void run_the_game(sh_int port)
{
    SocketType s;

    void signal_setup(void);

    descriptor_list = NULL;
    register_mudlog_broadcast_sink();
    // register_game_output_sinks() is defined further down in this file
    // (after the send_to_char_impl/act_impl/track_specialized_mage_impl
    // bodies it references), but comm.h declares it, so the call here only
    // needs that declaration -- see register_game_output_sinks() itself for
    // why it must run before boot_db().
    register_game_output_sinks();
    // entity_hooks.h's four inversion hooks (entity-seed Task 5 + EC Task 2),
    // registered the same way and for the same reason: before boot_db(), so
    // ageland never runs entity_lifecycle.cpp's free_char()/recalc_abilities(),
    // char_utils.cpp's get_energy_regen(), or char_utils_combat.cpp's
    // on_attacked_character() with an unregistered hook.
    register_char_teardown_hook();
    register_attack_speed_multiplier_hook();
    register_wild_attack_speed_multiplier_hook();
    register_attacked_player_hook();
    // entity_hooks.h's txt-block-pool hook pair (world-seed Task 2
    // adjudication), registered the same way and for the same reason:
    // before boot_db(), so ageland never runs target_data::operator=()'s
    // TARGET_TEXT copy path (entity_lifecycle.cpp) with an unregistered
    // hook.
    register_txt_block_pool_hooks();
    // persist_hooks.h's two inversion hooks (persist-split PS Task 4),
    // registered the same way and for the same reason: before boot_db(), so
    // ageland never runs db_players.cpp's save_char()/rename_char() with an
    // unregistered hook.
    register_room_vnum_hook();
    register_exploit_capture_hook();
    // world_hooks.h's three inversion hooks (world-seed Task 3),
    // registered the same way and for the same reason: before boot_db(),
    // so ageland never runs db_world.cpp's index_boot()/boot_mudlle() or
    // weather.cpp's another_hour()/weather_change() with an unregistered
    // hook.
    register_boot_shops_hook();
    register_mudlle_converter_hook();
    register_weather_msdp_hook();
    register_world_broadcast_hooks();
    // entity_hooks.h's three world-resolver hooks (placement-seam Task 1),
    // registered the same way and for the same reason: before boot_db(), so
    // ageland never runs placement.cpp's room_by_id()/zone_by_id()/
    // obj_index_by_id() with an unregistered hook.
    register_world_resolver_hooks();

    log("Signal trapping.");
    signal_setup();

    log("Opening mother connection.");
    mother_desc = s = init_socket(port);

    boot_db();

    log(std::format("The char_data size is {}.", sizeof(char_data)));
    log("Entering game loop.");

    specialized_mages.clear();
    game_loop(s);

    close_sockets(s);
    // Matches rots_net::startup() in main(); WSACleanup on Windows, no-op on POSIX.
    rots_net::shutdown();
    // fclose(player_fl);

    if (circle_reboot) {
        log("Rebooting.");
        exit(52); /* what's so great about HHGTTG, anyhow? */
    }

    log("Normal termination of game.");
}

void clean_expose_elements()
{
    for (char_iter iter = specialized_mages.begin(); iter != specialized_mages.end();) {
        char_data* mage = *iter;
        if (mage->extra_specialization_data.is_mage_spec()) {
            elemental_spec_data* spec_data = mage->extra_specialization_data.get_mage_spec();
            if (spec_data->exposed_target) {
                // The mage has cast 'expose elements' on a target.  If that target is no longer
                // in the room, remove this.
                int room_number = mage->in_room;
                const room_data& current_room = world[room_number];

                bool found_target = false;
                for (char_data* person = current_room.people; person;
                    person = person->next_in_room) {
                    if (person == spec_data->exposed_target) {
                        found_target = true;
                        break;
                    }
                }

                if (!found_target) {
                    send_to_char("Your target is no longer vulnerable to your spells.\r\n", mage);
                    spec_data->reset();
                }
            }

            ++iter;
        } else {
            // character no longer has a mage spec - remove them from our tracked list
            iter = specialized_mages.erase(iter);
        }
    }
}

/* MORE SPAGHETTI!!! */

/* Implementation from for function defined in utils.h */
static void track_specialized_mage_impl(char_data* mage)
{
    if (!mage)
        return;

    char_iter found_mage = std::find(specialized_mages.begin(), specialized_mages.end(), mage);
    if (found_mage == specialized_mages.end()) {
        specialized_mages.push_back(mage);
    }
}

/* Implementation from for function defined in utils.h */
static void untrack_specialized_mage_impl(char_data* mage)
{
    if (!mage)
        return;

    char_iter found_mage = std::remove(specialized_mages.begin(), specialized_mages.end(), mage);
    if (found_mage != specialized_mages.end()) {
        specialized_mages.erase(found_mage);
    }
}

void add_prompt(std::string& prompt, struct char_data* ch, long flag);

void build_prompt(struct descriptor_data* point, std::string& out)
{
    std::string core;
    core.reserve(128);
    struct char_data* opponent;
    struct char_data* tank;

    if (GET_INVIS_LEV(point->character)) {
        std::format_to(std::back_inserter(core), "i{}", GET_INVIS_LEV(point->character));
    }

    if (IS_RIDING(point->character))
        core.append(" R");

    if (PRF_FLAGGED(point->character, PRF_ADVANCED_PROMPT)) {
        core.append(" [");
        add_prompt(core, point->character, PROMPT_ADVANCED);
    } else if (((GET_HIT(point->character) < GET_MAX_HIT(point->character)) || point->character->specials.fighting) && PRF_FLAGGED(point->character, PRF_PROMPT)) {
        core.append(" HP:");
    }

    opponent = point->character->specials.fighting;

    if (!PRF_FLAGGED(point->character, PRF_ADVANCED_PROMPT)) {
        add_prompt(core, point->character,
            PRF_FLAGGED(point->character, PRF_DISPTEXT)      ? PRF_DISPTEXT
                : !PRF_FLAGGED(point->character, PRF_PROMPT) ? 0
                                                             : PROMPT_ALL);
    }

    if (opponent && IS_MENTAL(opponent)) {
        core.append(" Mind:");
        add_prompt(core, point->character, PROMPT_STAT);
    }

    if (IS_RIDING(point->character))
        add_prompt(core, point->character->mount_data.mount, PROMPT_MOVE);

    if (GET_RACE(point->character) == RACE_BEORNING) {
        affected_type* maul_buff = affected_by_spell(point->character, SKILL_MAUL);
        if (maul_buff && maul_buff->location == APPLY_MAUL) {
            core.append(" Maul:");
            add_prompt(core, point->character, PROMPT_MAUL);
        }
    }

    const obj_data* quiver = point->character->equipment[WEAR_BACK];
    if (quiver && quiver->is_quiver()) {
        core.append(" A:(");
        add_prompt(core, point->character, PROMPT_ARROWS);
    }

    if (point->character->specials.position == POSITION_FIGHTING) {
        if (opponent) {
            if (opponent->specials.fighting != point->character) {
                tank = opponent->specials.fighting;
                if (tank) {
                    std::format_to(std::back_inserter(core), ", {}:", PERS(tank, point->character, FALSE, FALSE));
                    add_prompt(core, tank,
                        (IS_MENTAL(opponent)) ? PROMPT_STAT
                                              : PROMPT_HIT);
                }
            }
            std::format_to(std::back_inserter(core), ", {}:", PERS(opponent, point->character, FALSE, FALSE));

            add_prompt(core, opponent,
                (IS_MENTAL(point->character))
                    ? PROMPT_STAT
                    : (IS_SHADOW(opponent) ? PROMPT_STAT : PROMPT_HIT));
        }
    }

    // Check for a blank space in the first position or the last
    size_t start = (!core.empty() && core.front() == ' ') ? 1 : 0;
    if (!core.empty() && core.back() == ' ')
        core.pop_back();
    core.push_back(point->character->specials.position == POSITION_SHAPING ? ']' : '>');
    out.assign(core, start, std::string::npos);
}

/* Accept pnew connects, relay commands, and call 'heartbeat-functs' */
extern int pulse; // definition moved to entity_lifecycle.cpp (entity-seed Task 5,
                  // storage-placement only); game_loop() below still increments/
                  // resets it via this extern.

int get_health_percent(char_data* character)
{
    const float current_health = GET_HIT(character);
    const float max_health = GET_MAX_HIT(character);
    if (max_health <= 0.0f)
        return 0;
    if (current_health <= 0.0f)
        return 0;

    const float health_percent = (current_health / max_health) * 100.0f;

    return (int)health_percent;
}

void msdp_update()
{
    for (auto desc = descriptor_list; desc; desc = desc->next) {
        if (!desc->character || IS_NPC(desc->character)) {
            continue;
        }

        if (!desc->pProtocol) {
            continue;
        }

        if (desc->character->in_room < 0 || desc->character->in_room > top_of_world) {
            continue;
        }

        MSDPSetString(desc, eMSDP_CHARACTER_NAME, GET_NAME(desc->character));
        MSDPSetNumber(desc, eMSDP_ALIGNMENT, GET_ALIGNMENT(desc->character));
        MSDPSetNumber(desc, eMSDP_EXPERIENCE_MAX,
            xp_to_level(GET_LEVEL(desc->character) + 1) - xp_to_level(GET_LEVEL(desc->character)));
        MSDPSetNumber(desc, eMSDP_EXPERIENCE,
            xp_to_level(GET_LEVEL(desc->character) + 1) - GET_EXP(desc->character));
        MSDPSetNumber(desc, eMSDP_HEALTH, GET_HIT(desc->character));
        MSDPSetNumber(desc, eMSDP_HEALTH_MAX, GET_MAX_HIT(desc->character));

        MSDPSetString(desc, eMSDP_ROOM_NAME, world[desc->character->in_room].name);
        MSDPSetNumber(desc, eMSDP_ROOM_VNUM, world[desc->character->in_room].number);
        MSDPSetNumber(desc, eMSDP_LEVEL, GET_LEVEL(desc->character));
        MSDPSetNumber(desc, eMSDP_MANA, GET_MANA(desc->character));
        MSDPSetNumber(desc, eMSDP_MANA_MAX, GET_MAX_MANA(desc->character));
        MSDPSetNumber(desc, eMSDP_MOVEMENT, GET_MOVE(desc->character));
        MSDPSetNumber(desc, eMSDP_MOVEMENT_MAX, GET_MAX_MOVE(desc->character));
        MSDPSetNumber(desc, eMSDP_MONEY, GET_GOLD(desc->character));
        MSDPSetString(desc, eMSDP_RACE, pc_races[utils::get_race(*desc->character)]);
        MSDPSetNumber(desc, eMSDP_STR, GET_STR(desc->character));
        MSDPSetNumber(desc, eMSDP_INT, GET_INT(desc->character));
        MSDPSetNumber(desc, eMSDP_WILL, GET_WILL(desc->character));
        MSDPSetNumber(desc, eMSDP_DEX, GET_DEX(desc->character));
        MSDPSetNumber(desc, eMSDP_CON, GET_CON(desc->character));
        MSDPSetNumber(desc, eMSDP_LEA, GET_LEA(desc->character));
        MSDPSetNumber(desc, eMSDP_STR_PERM, GET_STR_BASE(desc->character));
        MSDPSetNumber(desc, eMSDP_INT_PERM, GET_INT_BASE(desc->character));
        MSDPSetNumber(desc, eMSDP_WIL_PERM, GET_WILL_BASE(desc->character));
        MSDPSetNumber(desc, eMSDP_DEX_PERM, GET_DEX_BASE(desc->character));
        MSDPSetNumber(desc, eMSDP_CON_PERM, GET_CON_BASE(desc->character));
        MSDPSetNumber(desc, eMSDP_LEA_PERM, GET_LEA_BASE(desc->character));
        MSDPSetNumber(desc, eMSDP_WIMPY, WIMP_LEVEL(desc->character));
        MSDPSetNumber(desc, eMDSP_SPELL_SAVE, GET_SAVE(desc->character));

        player_spec::battle_mage_handler battle_mage_handler(desc->character);
        MSDPSetNumber(
            desc, eMDSP_SPELL_PEN,
            battle_mage_handler.get_bonus_spell_pen(desc->character->points.get_spell_pen()));
        MSDPSetNumber(
            desc, eMDSP_SPELL_POWER,
            battle_mage_handler.get_bonus_spell_power(desc->character->points.get_spell_power()));

        MSDPSetNumber(desc, eMDSP_ARMOUR_ABS, get_percent_absorb(desc->character));
        MSDPSetNumber(desc, eMDSP_OFFENSIVE_BONUS, get_real_OB(desc->character));
        MSDPSetNumber(desc, eMDSP_PARRY, get_real_parry(desc->character));
        MSDPSetNumber(desc, eMDSP_DODGE, get_real_dodge(desc->character));
        MSDPSetNumber(desc, eMDSP_ATTACK_SPEED, utils::get_energy_regen(*desc->character) / 5);

        extern const std::string_view tactics[];
        MSDPSetString(desc, eMDSP_TACTIC, tactics[GET_TACTICS(desc->character) - 1]);

        MSDPSetNumber(desc, eMDSP_PERCEPTION, GET_PERCEPTION(desc->character));
        MSDPSetNumber(desc, eMDSP_WILLPOWER, GET_WILLPOWER(desc->character));
        MSDPSetNumber(desc, eMDSP_SKILL_ENCUMBRANCE, utils::get_encumbrance(*desc->character));
        MSDPSetNumber(desc, eMDSP_MOVEMENT_ENCUMBRANCE,
            utils::get_leg_encumbrance(*desc->character));
        MSDPSetNumber(desc, eMDSP_HEALTH_REGENERATION, (int)hit_gain(desc->character));
        MSDPSetNumber(desc, eMDSP_STAMINA_REGENERATION, (int)mana_gain(desc->character));
        MSDPSetNumber(desc, eMDSP_MOVEMENT_REGENERATION, (int)move_gain(desc->character));

        auto sector_type = world[desc->character->in_room].sector_type;
        auto weather_type = weather_info.sky[sector_type];
        // const std::string_view , matching weather.cpp's definition exactly (MSVC's
        // decorated names encode the element type, so the old non-const
        // declaration was a hard LNK2001 there; GCC/Clang linked it silently
        // -- Phase 3 Task 6).
        extern const std::string_view weather_messages[8][13];
        extern std::string strip_trailing_line_break(std::string_view text);

        if (OUTSIDE(desc->character)) {
            MSDPSetString(desc, eMDSP_WEATHER,
                strip_trailing_line_break(weather_messages[weather_type + 2][sector_type]));
        } else {
            MSDPSetString(desc, eMDSP_WEATHER, "You can have no feeling about the weather here.");
        }

        auto opponent = desc->character->specials.fighting;
        if (opponent && utils::is_npc(*opponent)) {
            MSDPSetNumber(desc, eMSDP_OPPONENT_HEALTH, get_health_percent(opponent));
            MSDPSetString(desc, eMSDP_OPPONENT_NAME, GET_NAME(opponent));
            MSDPSetString(desc, eMSDP_OPPONENT_LEVEL, std::to_string(GET_LEVEL(opponent)));
        } else if (opponent && utils::is_pc(*opponent)) {
            MSDPSetNumber(desc, eMSDP_OPPONENT_HEALTH, get_health_percent(opponent));
            MSDPSetString(desc, eMSDP_OPPONENT_NAME, pc_star_types[utils::get_race(*opponent)]);
            MSDPSetString(desc, eMSDP_OPPONENT_LEVEL, "???");
        } else {
            MSDPSetNumber(desc, eMSDP_OPPONENT_HEALTH, 0);
            MSDPSetString(desc, eMSDP_OPPONENT_NAME, "");
            MSDPSetString(desc, eMSDP_OPPONENT_LEVEL, "");
        }

        MSDPSetNumber(desc, eMSDP_SPIRIT, GET_SPIRIT(desc->character));

        MSDPUpdate(desc);
    }
}

void game_loop(SocketType s)
{
    fd_set input_set, output_set, exc_set;
    struct timeval null_time;
    // Target start-of-pulse instant on the monotonic steady_clock (immune to wall-clock
    // adjustments, unlike the gettimeofday/timeval pair this replaced). Set to "now" at
    // loop entry, then advanced each iteration to "now + however much of the pulse budget
    // is left" -- i.e. it always names the earliest instant the *next* pulse may begin.
    std::chrono::steady_clock::time_point last_time;
    // Fixed per-pulse time budget (250ms / OPT_USEC), replacing the old global timeval
    // "opt_time". 4 passes/sec target, same as always.
    constexpr std::chrono::microseconds pulse_interval(OPT_USEC);
    char comm[MAX_INPUT_LENGTH];
    struct descriptor_data *point, *next_point;
    struct char_data *wait_ch, *wait_tmp;
    AutosaveTimer autosave_timer;
    int sockets_connected, sockets_playing;
    int tmp;
    char tmpflag;

    null_time.tv_sec = 0;
    null_time.tv_usec = 0;

    last_time = std::chrono::steady_clock::now(); /* Init time values */

    maxdesc = s;

#if defined(OPEN_MAX)
    avail_descs = OPEN_MAX - 8;
#elif defined(USE_TABLE_SIZE)
    {
        int retval;

        retval = setdtablesize(64);
        if (retval == -1)
            log("SYSERR: unable to set table size");
        else {
            log(std::format("{} {}\n", "dtablesize set to: ", retval));
        }
        avail_descs = getdtablesize() - 8;
    }
#else
    avail_descs = MAX_DESCRIPTORS_AVAILABLE;
#endif

    avail_descs = std::min(avail_descs, MAX_PLAYERS);

    /* Main loop */
    while (!circle_shutdown) {
        /* Check what's happening out there */
        FD_ZERO(&input_set);
        FD_ZERO(&output_set);
        FD_ZERO(&exc_set);
        FD_SET(s, &input_set);
        for (point = descriptor_list; point; point = point->next)
            if (point->descriptor) {
                FD_SET(point->descriptor, &input_set);
                FD_SET(point->descriptor, &exc_set);
                FD_SET(point->descriptor, &output_set);
            }

        /* check out the time: how much of the pulse budget this iteration's work
           spent, and how much is left to sleep off before the next pulse may start */
        auto now = std::chrono::steady_clock::now();
        auto timeout = pulse_interval - std::chrono::duration_cast<std::chrono::microseconds>(now - last_time);
        if (timeout < std::chrono::microseconds::zero()) {
            timeout = std::chrono::microseconds::zero();
        }
        last_time = now + timeout;

        platform_block_game_loop_signals();

        /* Poll-select stays a select() (native to both BSD sockets and Winsock; only
           its fd types diverge, which a later task moves behind a platform shim) --
           EINTR-tolerant now so the signal mask above is belt-and-braces, not
           load-bearing. */
        null_time.tv_sec = 0;
        null_time.tv_usec = 0;
        while ((tmp = select(maxdesc + 1, &input_set, &output_set, &exc_set, &null_time)) < 0 && rots_net::error_is_interrupted(rots_net::last_error())) {
            null_time.tv_sec = 0;
            null_time.tv_usec = 0;
        }

        if (tmp < 0) {
            perror("Select poll");
            platform_restore_game_loop_signals();
            return;
        }

        /* Pulse throttle: sleep off whatever's left of the 250ms budget. */
        std::this_thread::sleep_for(timeout);

        platform_restore_game_loop_signals();

        /* Respond to whatever might be happening */

        /* Pnew connection? */
        if (FD_ISSET(s, &input_set)) {
            if (pnew_descriptor(s) == 0) // here was <0, had to change
            {
                perror("Pnew connection");
            }
        }

        /* kick out the freaky folks */
        for (point = descriptor_list; point; point = next_point) {
            next_point = point->next;
            if (point->descriptor) {
                if (FD_ISSET(point->descriptor, &exc_set)) {
                    FD_CLR(point->descriptor, &input_set);
                    FD_CLR(point->descriptor, &output_set);
                    close_socket(point, FALSE);
                }
            }
        }

        /* take our input off of the TCP queues */
        for (point = descriptor_list; point; point = next_point) {
            next_point = point->next;
            if (point->descriptor) {
                if (FD_ISSET(point->descriptor, &input_set)) {
                    if (process_input(point) < 0) {
                        close_socket(point, FALSE);
                    }
                }
            }
        }

        /* process_commands */
        for (wait_ch = waiting_list; wait_ch; wait_ch = wait_tmp) {
            if (wait_ch->delay.wait_value > 0) {
                (wait_ch->delay.wait_value)--;
            }

            if (wait_ch->delay.wait_value > 0) {
                if (!IS_NPC(wait_ch) && IS_AFFECTED(wait_ch, AFF_WAITWHEEL)) {
                    // Guard against a closed-but-still-linked descriptor (0 is
                    // close_socket()'s closed sentinel; a link-dead player's
                    // desc stays reachable) -- same `->descriptor` truthiness
                    // check the three sibling descriptor_list loops below use
                    // (Phase 3 Task 6 review).
                    if (PRF_FLAGGED(wait_ch, PRF_SPINNER) && wait_ch->desc && wait_ch->desc->descriptor) {
                        write_to_descriptor(wait_ch->desc->descriptor,
                            wait_wheel[wait_ch->delay.wait_value % 8]);
                    }
                }

                wait_tmp = wait_ch->delay.next;
            } else if (wait_ch->delay.wait_value == 0) {
                /* here is the block calling actual procedures */
                complete_delay(wait_ch);
                wait_tmp = wait_ch->delay.next;

                if (wait_ch->delay.wait_value == 0)
                    /* look out for the similar code in raw_kill() */
                    abort_delay(wait_ch);
            } else {
                wait_tmp = wait_ch->delay.next;
            }
        }

        for (point = descriptor_list; point; point = next_to_process) {
            next_to_process = point->next;
            if (point->descriptor) {
                if (point->character)
                    tmpflag = (!IS_AFFECTED(point->character, AFF_WAITING));
                else
                    tmpflag = 1;

                if (tmpflag && (get_from_q(&point->input, comm))) {
                    if (point->character && !IS_NPC(point->character) && point->connected == CON_PLYNG && point->character->specials.was_in_room != NOWHERE) {
                        if (point->character->in_room != NOWHERE) {
                            char_from_room(point->character);
                        }

                        char_to_room(point->character, point->character->specials.was_in_room);
                        point->character->specials.was_in_room = NOWHERE;
                        act("$n has returned.", TRUE, point->character, 0, 0, TO_ROOM);
                        point->character->specials.timer = 0;
                    }
                    if (point->character && IS_AFFECTED(point->character, AFF_WAITWHEEL)) {
                        point->character->delay.wait_value = 1;
                        point->character->specials.timer = 0;
                    }
                    if (point->character && IS_SET(PLR_FLAGS(point->character), PLR_WRITING)) {
                        string_add(point, comm);
                    }

                    point->prompt_mode = 1;
                    if (!point->connected) {
                        if (point->showstr_point) {
                            show_string(point, comm);
                        } else {
                            if (!IS_SET(PLR_FLAGS(point->character), PLR_WRITING)) {
                                replace_aliases(point->character, comm);
                                command_interpreter(point->character, comm, 0);
                            }
                        }
                    } else {
                        nanny(point, comm);
                    }
                }
            }
        }

        for (point = descriptor_list; point; point = next_point) {
            next_point = point->next;
            if (point->descriptor) {
                if (FD_ISSET(point->descriptor, &output_set) && *(point->output)) {
                    if (process_output(point) < 0) {
                        close_socket(point, FALSE);
                    } else {
                        point->prompt_mode = 1;
                    }
                }
            }
        }

        /* kick out the Phreaky Pholks II  -JE */
        for (point = descriptor_list; point; point = next_to_process) {
            next_to_process = point->next;
            if (point->descriptor) {
                if (STATE(point) == CON_CLOSE) {
                    close_socket(point, FALSE);
                }
            }
        }

        /* give the people some prompts */
        for (point = descriptor_list; point; point = point->next)
            if (point->prompt_mode && point->descriptor) {
                if (point->character) {
                    tmp = (IS_SET(PLR_FLAGS(point->character), PLR_WRITING));
                } else {
                    tmp = !(point->connected);
                }
                if (tmp) {
                    write_to_descriptor(point->descriptor, "] ");
                } else if (!point->connected) {
                    if (point->showstr_point)
                        write_to_descriptor(point->descriptor,
                            "*** Press return to continue, q to quit ***");
                    else { /*if point->showstr_point */
                        static std::string prompt_buffer;
                        build_prompt(point, prompt_buffer);

                        if (point->character)
                            tmpflag = !IS_AFFECTED(point->character, AFF_WAITWHEEL);
                        else
                            tmpflag = 1;
                        if (tmpflag)
                            write_to_descriptor(point->descriptor, prompt_buffer.c_str());
                    }
                }
                point->prompt_mode = 0;
            }

        /* handle heartbeat stuff */
        /* Note: pulse now changes every 1/4 sec  */

        pulse++;

        if (!((pulse + 3) % PULSE_ZONE)) {
            zone_update();
        }
        if (!((pulse + 9) % PULSE_MOBILE)) {
            mobile_activity();
        }
        perform_violence(pulse % (PULSE_VIOLENCE * 2));
        /* parry is restored in 2 combat (PULSE_VIOLENCE) rounds */

        if (!((pulse % (SECS_PER_MUD_HOUR * 4)))) {
            weather_and_time(1);
            point_update(); // putting affect_total call in point_update.
            stat_update();
        }
        if (!(pulse % (PULSE_FAST_UPDATE))) {
            // now increasing hp/mp/mana/spirit fast in fast_update..
            fast_update();
            affect_update();

            // clean-up expose elements
            clean_expose_elements();
        }

        msdp_update();

        // Periodic point-in-time crash-save snapshot cadence, driven by the configurable seconds
        // interval (autosave_time) through the unit-tested scheduler. Default 30s == 120 pulses (the
        // source's original cadence). Crash_save_all now saves EVERY connected player each cadence
        // (a consistent point-in-time snapshot), not only inventory-dirty ones.
        if (autosave_timer.tick(autosave_interval_pulses(autosave_time, TICS_PER_SECOND))) {
            Crash_save_all();
        }

        if (!(pulse % 4)) {
            game_timer::skill_timer& st_instance = game_timer::skill_timer::instance();
            st_instance.update_skill_timer();
        }

        if (!(pulse % 1200)) {
            sockets_connected = sockets_playing = 0;

            for (point = descriptor_list; point; point = next_point) {
                next_point = point->next;
                if (point->descriptor) {
                    sockets_connected++;
                    if (!point->connected) {
                        sockets_playing++;
                    }
                }
            }

            log(std::format("nusage: {:<3} sockets connected, {:<3} sockets playing", sockets_connected,
                sockets_playing)
                    );

#ifdef RUSAGE
            {
                struct rusage rusagedata;

                getrusage(0, &rusagedata);
                log(std::format("rusage: {} {} {} {} {} {} {}", rusagedata.ru_utime.tv_sec,
                    rusagedata.ru_stime.tv_sec, rusagedata.ru_maxrss, rusagedata.ru_ixrss,
                    rusagedata.ru_ismrss, rusagedata.ru_idrss, rusagedata.ru_isrss)
                        );
            }
#endif
        }

        if (pulse >= 2400)
            pulse = 0;

        tics++; /* tics since last checkpoint signal */

        // Save chars before a shutdown or reboot.  --S
        if (circle_shutdown || circle_reboot) {
            struct char_data* ch;

            for (ch = character_list; ch; ch = ch->next) {
                if (!IS_NPC(ch) && ch->desc) {
                    save_char(ch, NOWHERE, 0);
                    Crash_crashsave(ch);
                }
            }
        }
    }
}

/* ******************************************************************
 *  general utility stuff (for local use)			    *
 ****************************************************************** */

int get_from_q(struct txt_q* queue, char* dest)
{
    struct txt_block* tmp;

    /* Q empty? */
    if (!queue->head)
        return (0);

    tmp = queue->head;
    strcpy(dest, queue->head->text);
    queue->head = queue->head->next;

    //   RELEASE(tmp->text);
    //   RELEASE(tmp);
    put_to_txt_block_pool(tmp);
    return (1);
}

void write_to_output(std::string_view text, descriptor_data* descriptor)
{
    text = rots::text::truncate_at_null(text);

    // If the descriptor is already in the overflow state or there is no text,
    // another write cannot change its observable output.
    if (descriptor->bufptr < 0 || text.empty()) {
        return;
    }

    const std::size_t text_size = text.size();

    /* if we have enough space, just write to buffer and that's it! */
    if (text_size <= static_cast<std::size_t>(descriptor->bufspace)) {
        // This output path runs for nearly every player message. Copy the known
        // view length directly to avoid rescanning it or allocating a temporary.
        std::memcpy(descriptor->output + descriptor->bufptr, text.data(), text_size);
        descriptor->bufptr += static_cast<int>(text_size);
        descriptor->bufspace -= static_cast<int>(text_size);
        descriptor->output[descriptor->bufptr] = '\0';
        return;
    }

    /* otherwise, try to switch to a large buffer */
    const std::size_t existing_size = static_cast<std::size_t>(descriptor->bufptr);
    const std::size_t large_payload_capacity = LARGE_BUFSIZE - 1;
    if (descriptor->large_outbuf || existing_size > large_payload_capacity
        || text_size > large_payload_capacity - existing_size) {
        /* we're already using large buffer, or even the large buffer
        in't big enough -- switch to overflow state */
        descriptor->bufptr = -1;
        buf_overflows++;
        return;
    }

    buf_switches++;
    /* if the pool has a buffer in it, grab it */
    if (bufpool) {
        descriptor->large_outbuf = bufpool;
        bufpool = bufpool->next;
    } else { /* else create one */
        CREATE(descriptor->large_outbuf, struct txt_block, 1);
        CREATE(descriptor->large_outbuf->text, char, LARGE_BUFSIZE);
        buf_largecount++;
    }

    // Preserve the existing payload before redirecting output to the pooled
    // large buffer, then append the bounded view and restore C-string form.
    std::memcpy(descriptor->large_outbuf->text, descriptor->output, existing_size);
    std::memcpy(descriptor->large_outbuf->text + existing_size, text.data(), text_size);
    descriptor->output = descriptor->large_outbuf->text;
    descriptor->bufptr = static_cast<int>(existing_size + text_size);
    descriptor->bufspace = LARGE_BUFSIZE - 1 - descriptor->bufptr;
    descriptor->output[descriptor->bufptr] = '\0';
}

struct txt_block* get_from_txt_block_pool()
{
    struct txt_block* pnew;

    if (txt_block_pool) {
        pnew = txt_block_pool;
        txt_block_pool = pnew->next;
    } else {
        CREATE(pnew, struct txt_block, 1);
        CREATE(pnew->text, char, MAX_INPUT_LENGTH /*strlen(txt) + 1*/);
        txt_block_counter++;
    }
    return pnew;
}

struct txt_block* get_from_txt_block_pool(std::string_view line)
{
    txt_block* text_block = get_from_txt_block_pool();
    const std::string_view normalized_line = rots::text::truncate_at_null(line);
    const std::size_t copied_size
        = std::min(normalized_line.size(), static_cast<std::size_t>(MAX_INPUT_LENGTH - 1));
    if (copied_size > 0) {
        std::memcpy(text_block->text, normalized_line.data(), copied_size);
    }
    text_block->text[copied_size] = '\0';
    return text_block;
}
void put_to_txt_block_pool(struct txt_block* pold)
{

    pold->next = txt_block_pool;
    txt_block_pool = pold;
}

// Registers get_from_txt_block_pool()/put_to_txt_block_pool() (above) as
// entity_hooks.h's txt-block-pool hook pair -- world-seed Task 2's
// disposition for target_data::cleanup()/operator=() (relocated to
// entity_lifecycle.cpp; see that file's own "target_data member functions"
// section). comm.cpp is not a leaf module (this pool is entangled with the
// large_outbuf/bufpool descriptor-output machinery above), so the pool
// itself stays here and the edge is inverted instead. Called once from
// run_the_game(), before boot_db() -- see entity_hooks.h.
void register_txt_block_pool_hooks()
{
    rots::entity::set_get_txt_block_pool_hook(get_from_txt_block_pool);
    rots::entity::set_put_txt_block_pool_hook(put_to_txt_block_pool);
}

// Installs send_to_sector()/send_to_outdoor() as world_hooks.h's
// send-to-sector/send-to-outdoor hook pair (world-seed Task 5,
// STOP-adjudicated cascade). weather.cpp's weather_message()/
// weather_change()/check_sun_change()/another_hour() used to call these
// two functions directly; comm.cpp is not a leaf module (both walk
// descriptor_list, this file's own session-management data), so the
// functions themselves stay here and the edge is inverted instead.
// Called once from run_the_game(), before boot_db() -- see world_hooks.h.
void register_world_broadcast_hooks()
{
    rots::world::set_send_to_sector_hook(send_to_sector);
    rots::world::set_send_to_outdoor_hook(send_to_outdoor);
}

void write_to_q(std::string_view text, struct txt_q* queue)
{
    const std::string_view normalized_text = rots::text::truncate_at_null(text);
    txt_block* pnew = get_from_txt_block_pool(normalized_text.substr(0, 255));

    /* Q empty? */
    if (!queue->head) {
        pnew->next = NULL;
        queue->head = queue->tail = pnew;
    } else {
        queue->tail->next = pnew;
        queue->tail = pnew;
        pnew->next = NULL;
    }
}

void write_to_q_lang(char* txt, struct txt_q* queue, int freq)
{
    struct txt_block* pnew;
    int i, len;

    pnew = get_from_txt_block_pool();

    len = strlen(txt);
    if (len > 255)
        len = 255;
    //   strcpy(pnew->text, txt);
    for (i = 0; i < len; i++) {
        if (number(1, 100) > freq)
            pnew->text[i] = number(60, 90);
        else
            pnew->text[i] = txt[i];
    }
    txt[i] = 0;

    /* Q empty? */
    if (!queue->head) {
        pnew->next = NULL;
        queue->head = queue->tail = pnew;
    } else {
        queue->tail->next = pnew;
        queue->tail = pnew;
        pnew->next = NULL;
    }
}

/* Empty the queues before closing connection */
void flush_queues(struct descriptor_data* d)
{
    if (d->large_outbuf) {
        d->large_outbuf->next = bufpool;
        bufpool = d->large_outbuf;
        d->large_outbuf = 0;
    }

    while (get_from_q(&d->input, buf2))
        ;
}

/* ******************************************************************
 *  socket handling						    *
 ****************************************************************** */

SocketType init_socket(sh_int port)
{
    SocketType s;
    int opt = 1;
    char hostname[MAX_HOSTNAME + 1];
    struct sockaddr_in sa;
    struct sockaddr* saddr;

    saddr = (struct sockaddr*)&sa;
    memset((char*)saddr, 0, sizeof(struct sockaddr_in));
    if (gethostname(hostname, MAX_HOSTNAME)) {
        perror("gethostname");
        exit(1);
    }

    sa.sin_port = htons(port);
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = 0;

    s = socket(AF_INET, SOCK_STREAM, 0);
    if (!rots_net::is_valid_socket(s)) {
        perror("Init-socket");
        exit(1);
    }

    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt)) < 0) {
        perror("setsockopt REUSEADDR");
        exit(1);
    }

    /* #ifdef USE_LINGER */
    {
        struct linger ld;

        ld.l_onoff = 0; // FINGOLFIN changed from 1 on 24th April 2001
        ld.l_linger = 0;
        if (setsockopt(s, SOL_SOCKET, SO_LINGER, (char*)&ld, sizeof(ld)) < 0) {
            perror("setsockopt LINGER");
            exit(1);
        }
    }
    /* #endif */

    if (bind(s, saddr, sizeof(struct sockaddr)) != 0) {
        perror("bind");
        rots_net::close_socket(s);
        touch_file("../.killscript");
        exit(1);
    }

    /* set_nonblocking handles its own problems */
    rots_net::set_nonblocking(s);

    listen(s, 3);

    return (s);
}

SocketType pnew_connection(SocketType s)
{
    struct sockaddr_in isa;
    socklen_t i;
    SocketType t;

    i = sizeof(isa);
    t = accept(s, (struct sockaddr*)(&isa), &i);
    if (!rots_net::is_valid_socket(t)) {
        perror("Accept");
        return (0); // probably incorrect..
    }
    // SocketType is `int` on POSIX but `SOCKET` (an unsigned 64-bit handle) on Windows;
    // %d silently truncated/misprinted the handle there. Cast explicitly to a wide
    // signed type and use a matching, platform-identical format spec instead of relying
    // on SocketType's platform-dependent width/signedness.
    mudlog(std::format("Socket {} connected.", static_cast<long long>(t)), NRM,
        LEVEL_IMPL, TRUE);

    return (t);
}

SocketType pnew_descriptor(SocketType s)
{
    SocketType desc;
    struct descriptor_data *pnewd, *point, *next_point;
    socklen_t size;
    int sockets_connected, sockets_playing;
    struct sockaddr_in sock = {};

    if ((desc = pnew_connection(s)) == 0) // here was <0, too bad
        return (0); // here was -1, too bad...

    sockets_connected = sockets_playing = 0;

    for (point = descriptor_list; point; point = next_point) {
        next_point = point->next;
        if (point->descriptor) {
            sockets_connected++;
            if (!point->connected)
                sockets_playing++;
        }
    }

    /*	if ((maxdesc + 1) >= avail_descs) */
    if (sockets_connected >= avail_descs) {
        write_to_descriptor(desc, "Sorry, RotS is full right now... try again later!  :-)\n\r");
        rots_net::close_socket(desc);
        return (0);
    } else if (desc > maxdesc)
        maxdesc = desc;

    CREATE(pnewd, struct descriptor_data, 1);

    /* find info */
    size = sizeof(sock);

    int err = 0;

    rots_net::set_nonblocking(desc);

    // The game is running behind a proxy; defer proxy-header intake to the normal
    // descriptor input path so a short header cannot block the whole server loop.
    if (!has_proxy) {
        err = getpeername(desc, (struct sockaddr*)&sock, &size);

        if (err < 0)
            perror("getpeername");
    }

    if (err < 0) {
        *pnewd->host = '\0';
    } else if (!has_proxy) {
        populate_descriptor_host(pnewd, sock.sin_addr.s_addr);
    }

    if (!has_proxy && reject_banned_descriptor_host(pnewd)) {
        rots_net::close_socket(desc);
        RELEASE(pnewd);
        return (0);
    }

    /*  Uncomment this if you want pnew connections logged.  It's usually not
    necessary, and just adds a lot of unnecessary bulk to the logs.

   sprintf(buf2, "Pnew connection from [%s]", pnewd->host);
   log(buf2);
*/

    /* init desc data */
    pnewd->descriptor = desc;
    pnewd->connected = CON_NME;
    pnewd->bad_pws = 0;
    pnewd->proxy_peer_address = 0;
    pnewd->proxy_peer_bytes_read = 0;
    pnewd->waiting_for_proxy_header = has_proxy ? true : false;
    *pnewd->account_name = '\0';
    *pnewd->account_email = '\0';
    *pnewd->account_password = '\0';
    *pnewd->account_character_name = '\0';
    pnewd->pos = -1;
    //   pnewd->wait = 1;
    pnewd->prompt_mode = 0;
    *pnewd->buf = '\0';
    pnewd->str = 0;
    pnewd->showstr_head = 0;
    pnewd->showstr_point = 0;
    *pnewd->last_input = '\0';
    pnewd->output = pnewd->small_outbuf;
    *(pnewd->output) = '\0';
    pnewd->bufspace = SMALL_BUFSIZE - 1;
    pnewd->large_outbuf = NULL;
    pnewd->input.head = NULL;
    pnewd->next = descriptor_list;
    pnewd->character = 0;
    pnewd->original = 0;
    pnewd->snoop.snooping = 0;
    pnewd->snoop.snoop_by = 0;
    pnewd->login_time = time(0);
    pnewd->dflags = 0;
    pnewd->last_input_time = pnewd->login_time;
    pnewd->pProtocol = ProtocolCreate();

    if (++last_desc == 1000)
        last_desc = 1;
    //   pnewd->desc_num = last_desc;
    pnewd->desc_num = int(desc);

    /* prepend to list */

    descriptor_list = pnewd;

    if (!pnewd->waiting_for_proxy_header && send_initial_login_output(pnewd) < 0) {
        close_socket(pnewd, FALSE);
        return (0);
    }

    return (1);
}

extern sh_int screen_width; /* config.cpp */

void append_lines(char* target, char* source, int* len)
{
    sh_int i, tmp;
    int sourcelen;

    tmp = *len;
    sourcelen = strlen(source);

    for (i = 0; i < sourcelen; i++) {
        *(target++) = source[i];
        tmp++;
        if (source[i] == '\r')
            tmp = 0;
        if (source[i] == '\n')
            tmp--;
        if (tmp > screen_width) {
            *(target++) = '\n';
            *(target++) = '\r';
            tmp = 0;
        }
    }
    *len = tmp;
    *target = 0;
}

char process_output_buffer[LARGE_BUFSIZE + 20];

int process_output(struct descriptor_data* t)
{
    char *i = process_output_buffer, *c;
    int wid_count, i_shift;
    /* start writing at the 2nd space so we can prepend "% " for snoop */
    wid_count = 0;
    if (!t->prompt_mode && !t->connected) {
        strcpy(i + 2, "\n\r");
        i_shift = 2;
    } else
        i_shift = 0;
    if ((t->character) && (IS_SET(PRF_FLAGS(t->character), PRF_WRAP)))
        append_lines(i + 2 + i_shift, t->output, &wid_count);
    else
        strcpy(i + 2 + i_shift, t->output);

    /* they don't have latin-1 set. unaccent all of our latin-1 chars */
    if (t->character)
        if (!PRF_FLAGGED(t->character, PRF_LATIN1))
            for (c = i + 2; *c; ++c)
                *c = unaccent(*c);

    if (t->bufptr < 0)
        strcat(i + 2, "**OVERFLOW**");

    if (!t->connected && !(t->character && !IS_NPC(t->character) && PRF_FLAGGED(t->character, PRF_COMPACT)))
        strcat(i + 2, "\n\r");

    if (write_to_descriptor(t->descriptor, i + 2) < 0)
        return -1;

    if (t->snoop.snoop_by) {
        i[0] = '%';
        i[1] = ' ';
        SEND_TO_Q(i, t->snoop.snoop_by->desc);
    }

    /* if we were using a large buffer, put the large buffer on the buffer
      pool and switch back to the small one */
    if (t->large_outbuf) {
        t->large_outbuf->next = bufpool;
        bufpool = t->large_outbuf;
        t->large_outbuf = NULL;
        t->output = t->small_outbuf;
    }

    /* reset total bufspace back to that of a small buffer */
    t->bufspace = SMALL_BUFSIZE - 1;
    t->bufptr = 0;
    *(t->output) = '\0';

    return 1;
}

int write_to_descriptor(SocketType descriptor, std::string_view text)
{
    text = rots::text::truncate_at_null(text);
    std::size_t bytes_sent = 0;

    // Two sentinels to reject, both load-bearing (Phase 3 Task 6 review):
    // - 0 is this codebase's own "closed descriptor" marker: close_socket()
    //   sets conn_descriptor->descriptor = 0 while the descriptor_data can
    //   stay reachable (e.g. a link-dead player's waitwheel spinner in
    //   game_loop), so writing here would hit stdin and perror-spam every
    //   pulse. That pre-existing convention takes precedence over the
    //   theoretical validity of SOCKET 0 on Windows -- the same tradeoff the
    //   historical `desc <= 0` guard made.
    // - is_valid_socket() rejects the platform invalid-handle sentinel:
    //   kInvalidSocket is -1 on POSIX, while on Windows INVALID_SOCKET is a
    //   huge unsigned all-ones value that the old `<= 0` comparison could
    //   never catch (Phase 3 Task 4 finding).
    if (descriptor == 0 || !rots_net::is_valid_socket(descriptor)) {
        return 0;
    }

    try {
        while (bytes_sent < text.size()) {
            const rots_net::ssize_type bytes_written = rots_net::write_socket(
                descriptor, text.data() + bytes_sent, text.size() - bytes_sent);
            if (bytes_written < 0) {
                perror("Write to socket");
                return (-1);
            }
            bytes_sent += static_cast<std::size_t>(bytes_written);
        }
    } catch (...) {
        vmudlog(NRM, "Exception in write_to_descriptor");
        return -1;
    }

    return (0);
}

void break_spell(struct char_data* ch)
{
    //  if(IS_AFFECTED(ch, AFF_WAITWHEEL)){
    //        printf("breaking spell for %s\n", ch->player.name);
    //    REMOVE_BIT(ch->specials.affected_by, AFF_WAITWHEEL);
    //    REMOVE_BIT(ch->specials.affected_by, AFF_WAITING);
    //    if(ch->desc)
    //      write_to_descriptor(ch->desc->descriptor,"\r");
    ch->delay.wait_value = 0;
    ch->delay.subcmd = -1;

    //     if((ch->delay.cmd == -1) && IS_NPC(ch)){
    //       if(IS_NPC(ch) && mob_index[ch->nr].func){
    //		printf("calling special-return\n");
    // 	(*mob_index[ch->nr].func)
    // 	  (ch, 0, -1, "", 0, &(ch->delay));
    //       }
    //     }
    //     else if(ch->delay.cmd > 0){
    //       //	      printf("gonna interpret, name=%s, delay=%p\n",wait_ch->player.name,
    //       &(wait_ch->delay)); command_interpreter(ch, mutable_arg(""), &(ch->delay));
    //     }
    //  }
    //  else{
    // no problem - just couldn't break it
    // send_to_char("Minor problem in break_spell. Please notify imps. :)\n\r",
    //		 ch);
    //  }
}

char process_input_tmp[MAX_INPUT_LENGTH + 2];
char process_input_buffer[MAX_INPUT_LENGTH + 60];
int process_input(struct descriptor_data* t)
{
    int sofar, thisround, begin, squelch, i, k, flag, failed_subst = 0;
    char* tmp = process_input_tmp;
    char* buffer = process_input_buffer;

    if (!t->descriptor)
        return (0);

    const int proxy_header_result = finish_proxy_header_if_ready(t);
    if (proxy_header_result <= 0)
        return proxy_header_result;

    sofar = flag = 0;
    begin = strlen(t->buf);

    /* Read in some stuff */
    do {
        char inbuf[2048];
        thisround = static_cast<int>(rots_net::read_socket(t->descriptor, inbuf, sizeof(inbuf)));
        if (thisround > 0) {
            /* Filter out telnet/MSDP negotiation if protocol is active */
            if (t->pProtocol) {
                char filtered[MAX_STRING_LENGTH];
                filtered[0] = '\0';
                ProtocolInput(t, inbuf, thisround, filtered);

                int add = (int)strlen(filtered);
                int room = MAX_STRING_LENGTH - (begin + sofar) - 1;
                if (add > room)
                    add = room;
                if (add > 0) {
                    memcpy(t->buf + begin + sofar, filtered, add);
                    sofar += add;
                    *(t->buf + begin + sofar) = 0;
                }
            } else {
                /* No protocol yet; append raw bytes (clamped) */
                int room = MAX_STRING_LENGTH - (begin + sofar) - 1;
                int add = thisround;
                if (add > room)
                    add = room;
                if (add > 0) {
                    memcpy(t->buf + begin + sofar, inbuf, add);
                    sofar += add;
                    *(t->buf + begin + sofar) = 0;
                }
            }
        } else {
            if (thisround < 0) {
                if (!rots_net::error_is_would_block(rots_net::last_error())) {
                    perror("Read1 - ERROR");
                    return (-1);
                } else
                    break;
            } else {
                log("EOF encountered on socket read.");
                return (-1);
            }
        }
    } while (!ISNEWL(*(t->buf + begin + sofar - 1)));

    if (t->character)
        t->character->specials.timer = 0;

    *(t->buf + begin + sofar) = 0;

    /* if no pnewline is contained in input, return without proc'ing */
    for (i = begin; !ISNEWL(*(t->buf + i)); i++)
        if (!*(t->buf + i))
            return (0);

    /* input contains 1 or more pnewlines; process the stuff */
    for (i = 0, k = 0; *(t->buf + i);) {
        if (!ISNEWL(*(t->buf + i)) && !(flag = (k >= (MAX_INPUT_LENGTH - 2))))
            /* is this a backspace? */
            if ((*(t->buf + i) == '\b') || ((unsigned char)*(t->buf + i) == 177))
                if (k) { /* more than one char ? */
                    if (*(tmp + --k) == '$')
                        k--;
                    i++;
                } else
                    i++; /* no or just one char.. Skip backsp */
            else if (isascii(unaccent(*(t->buf + i))) && isprint(unaccent(*(t->buf + i)))) {
                /* trans char, double for '$' (printf)	*/
                if ((*(tmp + k) = *(t->buf + i)) == '$')
                    *(tmp + ++k) = '$';
                k++;
                i++;
            } else
                i++;
        else { /* the char pointed to IS a newline, or we've overflowed */
            *(tmp + k) = 0;
            /* is this a blank line? are they casting? end the cast if so */
            if ((*tmp == 0) && (STATE(t) == CON_PLYNG) && IS_AFFECTED(t->character, AFF_WAITWHEEL))
                break_spell(t->character);

            /* they entered a command, no? so we update their last_input_time */
            t->last_input_time = time(0);

            const bool secret_input = is_secret_input_state(t->connected);

            /* if they input !, then we repeat the last input */
            if (!secret_input && *tmp == '!')
                strcpy(tmp, t->last_input);
            else if (!secret_input && *tmp == '^') {
                if (!(failed_subst = perform_subst(t, t->last_input, tmp)))
                    strcpy(t->last_input, tmp);
            } else if (!secret_input && *tmp > ' ')
                strcpy(t->last_input, tmp);

            // COMMAND LOG
            if (iCommands == 200) {
                fclose(fpCommand);
                fpCommand = fopen("last_cmds", "w");
                iCommands = 0;
            }

            if ((t->connected == CON_PLYNG) && (t->character))
                fprintf(fpCommand, "%3lld %-16s: %s\n", static_cast<long long>(t->descriptor), GET_NAME(t->character), tmp);

            iCommands++;
            fflush(fpCommand);

            if (!failed_subst)
                write_to_q(tmp, &t->input);

            if (t->snoop.snoop_by && !secret_input) {
                SEND_TO_Q("% ", t->snoop.snoop_by->desc);
                SEND_TO_Q(tmp, t->snoop.snoop_by->desc);
                SEND_TO_Q("\n\r", t->snoop.snoop_by->desc);
            }

            if (flag) {
                strcpy(buffer, std::format("Line too long.  Truncated to:\n\r{}\n\r", tmp).c_str());
                if (write_to_descriptor(t->descriptor, buffer) < 0)
                    return (-1);

                /* skip the rest of the line */
                for (; !ISNEWL(*(t->buf + i)); i++)
                    ;
            }

            /* find end of entry */
            for (; ISNEWL(*(t->buf + i)); i++)
                ;

            /* squelch the entry from the buffer */
            for (squelch = 0;; squelch++)
                if ((*(t->buf + squelch) = *(t->buf + i + squelch)) == '\0')
                    break;
            k = 0;
            i = 0;
        }
    }
    return 1;
}

int perform_subst(struct descriptor_data* t, char* orig, char* subst)
{
    char *first, *second, *strpos;

    first = subst + 1;
    if (!(second = strchr(first, '^'))) {
        SEND_TO_Q("Invalid substitution.\n\r", t);
        return 1;
    }

    *(second++) = '\0';

    if (!(strpos = strstr(orig, first))) {
        SEND_TO_Q("Invalid substitution.\n\r", t);
        return 1;
    }

    std::string result(orig, strpos - orig);
    result += second;
    if (((strpos - orig) + strlen(first)) < strlen(orig))
        result += strpos + strlen(first);
    strcpy(subst, result.c_str());

    return 0;
}

void close_sockets(SocketType s)
{
    log("Closing all sockets.");
    while (descriptor_list) {
        close_socket(descriptor_list);
    }

    rots_net::close_socket(s);
}

void close_socket(descriptor_data* conn_descriptor, int drop_all)
{
    descriptor_data* tmp;

    clear_account_backed_object_bytes_for_character(conn_descriptor->character);

    // Alert Big Brother that the character is leaving.
    char_data* character = conn_descriptor->character;
    if (character && utils::is_pc(*character)) {
        game_rules::big_brother& bb_instance = game_rules::big_brother::instance();
        bb_instance.on_character_disconnected(character);
    }

    if (conn_descriptor->descriptor) {
        // Same SocketType-width issue as pnew_connection() above: cast explicitly and
        // use the same %lld spelling on both platforms.
        mudlog(
                   std::format("Closing socket {}.", static_cast<long long>(conn_descriptor->descriptor)),
            NRM, LEVEL_IMPL, TRUE);

        rots_net::close_socket(conn_descriptor->descriptor);
        conn_descriptor->descriptor = 0;
        conn_descriptor->desc_num = -1;
    }

    flush_queues(conn_descriptor);
    if (conn_descriptor->pProtocol) {
        ProtocolDestroy(conn_descriptor->pProtocol);
        conn_descriptor->pProtocol = nullptr;
    }
    if (conn_descriptor->descriptor == maxdesc) {
        --maxdesc;
    }

    /* Forget snooping */
    if (conn_descriptor->snoop.snooping) {
        conn_descriptor->snoop.snooping->desc->snoop.snoop_by = 0;
    }
    if (conn_descriptor->snoop.snoop_by) {
        send_to_char("Your victim is no longer among us.\n\r", conn_descriptor->snoop.snoop_by);
        conn_descriptor->snoop.snoop_by->desc->snoop.snooping = 0;
    }

    if (conn_descriptor->character) {
        if (conn_descriptor->connected == CON_PLYNG) {
            save_char(conn_descriptor->character, NOWHERE, 0);
            act("$n has lost $s link.", TRUE, conn_descriptor->character, 0, 0, TO_ROOM);
            mudlog(std::format("Closing link to: {} [{}].",
                       GET_NAME(conn_descriptor->character), static_cast<const char*>(conn_descriptor->host))
                           ,
                NRM, std::max(LEVEL_IMMORT, GET_INVIS_LEV(conn_descriptor->character)), TRUE);
            //	 d->character->desc = 0;
            // Deliberate: this player was just flushed by the save_char above, then moved to
            // CON_LINKLS. The point-in-time autosave snapshot (Crash_save_all) filters on CON_PLYNG,
            // so link-dead players are correctly excluded -- the exclusion is the CON_PLYNG state
            // filter, NOT the (commented-out) `desc = 0` detach. If that detach is ever re-enabled,
            // revisit the snapshot's reliance on connection state rather than a null desc.
            conn_descriptor->connected = CON_LINKLS;
        } else {
            std::string buf;
            if (conn_descriptor->character->player.name) {
                buf = std::format("Losing player: {} [{}].", GET_NAME(conn_descriptor->character),
                    static_cast<const char*>(conn_descriptor->host));
            } else {
                buf = std::format(
                    "Losing Unnamed player [{}].", static_cast<const char*>(conn_descriptor->host));
            }
            mudlog(buf, NRM,
                std::max(LEVEL_IMMORT, GET_INVIS_LEV(conn_descriptor->character)), TRUE);
            free_char(conn_descriptor->character);
            drop_all = 1;
        }
    } else {
        mudlog("Losing descriptor without char.", NRM, LEVEL_IMMORT, TRUE);
        drop_all = 1;
    }

    if ((conn_descriptor->connected != CON_LINKLS) || drop_all) {
        //    mudlog("Detaching descriptor.", NRM, LEVEL_IMPL, TRUE);
        if (next_to_process == conn_descriptor) /* to avoid crashing the process loop */
            next_to_process = next_to_process->next;
        if (conn_descriptor == descriptor_list) /* this is the head of the list */
            descriptor_list = descriptor_list->next;
        else { /* This is somewhere inside the list */
            /* Locate the previous element */
            for (tmp = descriptor_list; (tmp->next != conn_descriptor) && tmp; tmp = tmp->next)
                ;
            if (tmp) {
                tmp->next = conn_descriptor->next;
            }
        }
        RELEASE(conn_descriptor->showstr_head);
        RELEASE(conn_descriptor);
    }
}

/* ****************************************************************
 *	Public routines for system-to-player-communication	  *
 *******************************************************************/
static void send_to_char_impl(std::string_view message, char_data* character)
{
    // Early out if we have no message or character.
    if (message.empty() || character == nullptr) {
        return;
    }

    // Early out if the character isn't connected.
    if (character->desc == nullptr || character->desc->connected != CON_PLYNG) {
        return;
    }

    write_to_output(message, character->desc);
}

static void send_to_char_id_impl(std::string_view message, int character_id)
{
    if (message.empty()) {
        return;
    }

    for (descriptor_data* connection = descriptor_list; connection;
        connection = connection->next) {
        char_data* character = connection->character;
        if (character && character->abs_number == character_id && connection->connected == CON_PLYNG) {
            write_to_output(message, connection);
            break;
        }
    }
}

const char* get_char_name(int character_id)
{
    char_data* character = get_character(character_id);
    if (character) {
        return character->player.name;
    }
    return nullptr;
}

char_data* get_character(int character_id)
{
    for (descriptor_data* connection = descriptor_list; connection; connection = connection->next) {
        char_data* character = connection->character;
        if (character && character->abs_number == character_id && connection->connected == CON_PLYNG) {
            return character;
        }
    }

    return NULL;
}

void send_to_all(std::string_view message)
{
    for (descriptor_data* connection = descriptor_list; connection;
        connection = connection->next) {
        if (connection->connected == CON_PLYNG) {
            SEND_TO_Q(message, connection);
        }
    }
}

void send_to_outdoor(std::string_view message, int mode)
{
    for (descriptor_data* connection = descriptor_list; connection;
        connection = connection->next) {
        if (!connection->connected && (connection->character->in_room != NOWHERE)) {
            if ((OUTSIDE(connection->character)
                    && ((mode != OUTDOORS_LIGHT)
                        || !IS_SET(world[connection->character->in_room].room_flags, DARK)))
                && (connection->character->specials.position > POSITION_SLEEPING)
                && (!PLR_FLAGGED(connection->character, PLR_WRITING))) {
                SEND_TO_Q(message, connection);
            }
        }
    }
}

//  For weather messages - sends to outdoor sector
void send_to_sector(std::string_view message, int sector_type)
{
    if (sector_type > 12 || sector_type < 0) {
        return;
    }
    for (descriptor_data* connection = descriptor_list; connection;
        connection = connection->next) {
        if (!connection->connected && (connection->character->in_room != NOWHERE)) {
            if ((world[connection->character->in_room].sector_type == sector_type)
                && (connection->character->specials.position > POSITION_SLEEPING)
                && (!PLR_FLAGGED(connection->character, PLR_WRITING))
                && OUTSIDE(connection->character)) {
                SEND_TO_Q(message, connection);
            }
        }
    }
}

void send_to_except(std::string_view message, char_data* excluded_character)
{
    for (descriptor_data* connection = descriptor_list; connection;
        connection = connection->next) {
        if (excluded_character->desc != connection && !connection->connected) {
            SEND_TO_Q(message, connection);
        }
    }
}

void send_to_room(std::string_view message, int room)
{
    for (char_data* occupant = world[room].people; occupant;
        occupant = occupant->next_in_room) {
        if (occupant->desc) {
            SEND_TO_Q(message, occupant->desc);
        }
    }
}

void send_to_room_except(std::string_view message, int room, char_data* excluded_character)
{
    for (char_data* occupant = world[room].people; occupant;
        occupant = occupant->next_in_room) {
        if (occupant != excluded_character && occupant->desc) {
            SEND_TO_Q(message, occupant->desc);
        }
    }
}

void send_to_room_except_two(std::string_view message, int room,
    char_data* excluded_first, char_data* excluded_second)
{
    for (char_data* occupant = world[room].people; occupant;
        occupant = occupant->next_in_room) {
        if (occupant != excluded_first && occupant != excluded_second && occupant->desc) {
            SEND_TO_Q(message, occupant->desc);
        }
    }
}

/*
 * The PERS function (still capitalized because it used to be
 * a macro) returns a colored string that is terminated.  Thus,
 * the terminating color of the string terminates any other
 * color on the line, and we set the clobbered_color flag.  It
 * turns out that enemy is the only color that we do not wish
 * to use to color as a full line--that is why PERS takes care
 * of its own coloring.
 *
 * That is, we want to have:
 *   <HIT>You force your Will against <ENMY>*an Uruk*<NORM><HIT>'s X!<NORM>
 * But we don't want to have:
 *   <NORM>You wield <OBJ>a shadowy blade<NORM><NORM>.<NORM>
 */
void convert_string(std::string_view format_text, int, struct char_data* ch,
    struct obj_data* obj, void* vict_obj, struct char_data* to, char* output_buffer)
{
    bool clobbered_color = false;
    const char* used_color = nullptr;
    std::size_t write_index = 0;

    // Bounded splice into the caller's MAX_STRING_LENGTH buffer: truncation
    // during the write is byte-identical to the old build-then-clip because
    // overflow only ever drops trailing bytes.
    const auto append = [&](std::string_view text) {
        const std::size_t space = static_cast<std::size_t>(MAX_STRING_LENGTH - 1) - write_index;
        const std::size_t copy_size = std::min(text.size(), space);
        std::memcpy(output_buffer + write_index, text.data(), copy_size);
        write_index += copy_size;
    };

    for (std::size_t format_index = 0; format_index < format_text.size(); ++format_index) {
        const std::size_t dollar_position = format_text.find('$', format_index);
        if (dollar_position != format_index) {
            // Copy the whole plain-text run up to the next token (or the end) at once.
            append(format_text.substr(format_index,
                (dollar_position == std::string_view::npos ? format_text.size() : dollar_position)
                    - format_index));
            if (dollar_position == std::string_view::npos) {
                break;
            }
            format_index = dollar_position;
        }
        if (format_index + 1 >= format_text.size()) {
            append(format_text.substr(format_index, 1));
            break;
        }
        const char token = format_text[++format_index];
        const char* replacement = nullptr;
        switch (token) {
        case 'C': /* This is a two-letter color code */
            if (format_index + 1 >= format_text.size()) {
                vmudlog(NRM, "ERROR: Incomplete color code in act().");
                break;
            }
            switch (format_text[++format_index]) {
            case 'N': /* Narrate */
                replacement = CC_USE(to, COLOR_NARR);
                break;
            case 'C': /* Chat */
                replacement = CC_USE(to, COLOR_CHAT);
                break;
            case 'Y': /* Yell */
                replacement = CC_USE(to, COLOR_YELL);
                break;
            case 'T': /* Tell */
                replacement = CC_USE(to, COLOR_TELL);
                break;
            case 'S': /* Say */
                replacement = CC_USE(to, COLOR_SAY);
                break;
            case 'R': /* Room name */
                replacement = CC_USE(to, COLOR_ROOM);
                break;
            case 'H': /* Hit */
                replacement = CC_USE(to, COLOR_HIT);
                break;
            case 'D': /* Damage */
                replacement = CC_USE(to, COLOR_DAMG);
                break;
            case 'K': /* Character */
                replacement = CC_USE(to, COLOR_CHAR);
                break;
            case 'O': /* Object */
                replacement = CC_USE(to, COLOR_OBJ);
                break;
            case 'E': /* Description */
                replacement = CC_USE(to, COLOR_DESC);
                break;
            case 'G': /* Group Tell */
                replacement = CC_USE(to, COLOR_GTELL);
                break;
            default:
                vmudlog(NRM, "ERROR: Unrecognized color code '%c'.", format_text[format_index]);
            }
            used_color = replacement;
            break;
        case 'K': /* PERS, but force_visible */
            replacement = PERS((struct char_data*)vict_obj, to, FALSE, TRUE);
            break;
        case 'n': /* See note at top of function on PERS and color */
            replacement = PERS(ch, to, FALSE, FALSE);
            clobbered_color = true;
            break;
        case 'N': /* See note at top of function on PERS and color */
            replacement = PERS((struct char_data*)vict_obj, to, FALSE, FALSE);
            clobbered_color = true;
            break;
        case 'm':
            replacement = HMHR(ch);
            break;
        case 'M':
            replacement = HMHR((struct char_data*)vict_obj);
            break;
        case 's':
            replacement = HSHR(ch);
            break;
        case 'S':
            replacement = HSHR((struct char_data*)vict_obj);
            break;
        case 'e':
            replacement = HSSH(ch);
            break;
        case 'E':
            replacement = HSSH((struct char_data*)vict_obj);
            break;
        case 'o':
            replacement = OBJN(obj, to);
            break;
        case 'O':
            replacement = OBJN((struct obj_data*)vict_obj, to);
            break;
        case 'p':
            replacement = OBJS(obj, to);
            break;
        case 'P':
            replacement = OBJS((struct obj_data*)vict_obj, to);
            break;
        case 'a':
            replacement = SANA(obj);
            break;
        case 'A':
            replacement = SANA((struct obj_data*)vict_obj);
            break;
        case 'T':
            replacement = (char*)vict_obj;
            break;
        case 'F':
            replacement = fname((char*)vict_obj);
            break;
        case 'b':
            replacement = GET_CURRPART(ch).data();
            break;
        case 'B':
            replacement = GET_CURRPART((struct char_data*)vict_obj).data();
            break;
        case '$':
            replacement = "$";
            break;
        default:
            log("SYSERR: Illegal $-code to act():");
            log(std::format("SYSERR: {}", format_text));
            break;
        }
        if (replacement != nullptr) {
            append(replacement);
        }
        if (clobbered_color && used_color != nullptr) {
            append(used_color);
            clobbered_color = false;
        }
    }

    append("\n\r");
    if (used_color) {
        append(CC_NORM(to));
    }
    output_buffer[write_index] = '\0';

    /* Find the first character in the string, ignoring ANSI colors */
    std::size_t visible_index = 0;
    while (visible_index < write_index && output_buffer[visible_index] == '\x1B') {
        const char* color_end
            = static_cast<const char*>(std::memchr(output_buffer + visible_index, 'm', write_index - visible_index));
        if (color_end == nullptr) {
            break;
        }
        visible_index = static_cast<std::size_t>(color_end - output_buffer) + 1;
    }
    if (visible_index < write_index && isalpha(static_cast<unsigned char>(output_buffer[visible_index]))) {
        output_buffer[visible_index]
            = static_cast<char>(toupper(static_cast<unsigned char>(output_buffer[visible_index])));
    }
}

char act_buffer[MAX_STRING_LENGTH];
static void act_impl(std::string_view str, int hide_invisible, struct char_data* ch,
    struct obj_data* obj, void* vict_obj, int type, char spam_only)
{
    struct char_data* to;
    char* buf = act_buffer;

    str = rots::text::truncate_at_null(str);
    if (str.empty())
        return;

    to = 0;

    if (type == TO_VICT)
        to = (struct char_data*)vict_obj;
    else {
        if (type == TO_CHAR)
            to = ch;
        else if (ch && ch->in_room != NOWHERE)
            to = world[ch->in_room].people;
        else if (obj && obj->in_room != NOWHERE)
            to = world[obj->in_room].people;
    }

    if (!to)
        return;
    //   printf("act(%s) called, to=%p\n",str, to);
    for (; to; to = to->next_in_room) {
        if (to->desc && (to != ch || type == TO_CHAR) && (CAN_SEE(to, ch) || !hide_invisible) && (AWAKE(to) || type == TO_VICT) && !PLR_FLAGGED(to, PLR_WRITING) && !(type == TO_NOTVICT && to == (struct char_data*)vict_obj) && (!spam_only || PRF_FLAGGED(to, PRF_SPAM))) {
            convert_string(str, hide_invisible, ch, obj, vict_obj, to, buf);
            if (*buf != '\0')
                SEND_TO_Q(buf, to->desc);
        }
        if ((type == TO_VICT) || (type == TO_CHAR))
            return;
    }
}

// Installs the game's output sinks (rots::output::set_sinks): the real
// send_to_char/act/track_specialized_mage/untrack_specialized_mage bodies
// above (renamed to *_impl, file-scope statics) replace the null-defaulted,
// tripwire-logging defaults that output_seam.cpp's forwarders otherwise fall
// back to. Called once from run_the_game(), immediately after
// register_mudlog_broadcast_sink() and before boot_db(), so ageland never
// runs any output-path call with an unregistered sink.
void register_game_output_sinks()
{
    rots::output::Sinks sinks {};
    sinks.send_to_char = send_to_char_impl;
    sinks.send_to_char_id = send_to_char_id_impl;
    sinks.act = act_impl;
    sinks.track_mage = track_specialized_mage_impl;
    sinks.untrack_mage = untrack_specialized_mage_impl;
    rots::output::set_sinks(sinks);
}

void complete_delay(struct char_data* ch)
{
    SPECIAL(*tmpfunc);

    ch->delay.wait_value = 0;
    REMOVE_BIT(ch->specials.affected_by, AFF_WAITWHEEL);
    REMOVE_BIT(ch->specials.affected_by, AFF_WAITING);

    if (ch->delay.cmd == CMD_SCRIPT) {
        continue_char_script(ch);
        return;
    }

    if (ch->delay.cmd == -1 && IS_NPC(ch)) {
        /* Here calls special procedure */
        if (mob_index[ch->nr].func)
            (*mob_index[ch->nr].func)(ch, 0, -1, mutable_arg(""), SPECIAL_NONE, &(ch->delay));
        else if (ch->specials.store_prog_number) {
            tmpfunc = (SPECIAL(*))virt_program_number(ch->specials.store_prog_number);
            tmpfunc(ch, 0, ch->delay.cmd, mutable_arg(""), SPECIAL_DELAY, &(ch->delay));
        } else if (ch->specials.special_prog_number)
            intelligent(ch, 0, -1, mutable_arg(""), SPECIAL_DELAY, &(ch->delay));
    } else if (ch->delay.cmd > 0)
        command_interpreter(ch, mutable_arg(""), &(ch->delay));
}

int in_waiting_list(char_data* ch)
{
    if (waiting_list == NULL || ch == NULL)
        return 0;

    for (char_data* iter = waiting_list; iter; iter = iter->delay.next)
        if (iter == ch)
            return 1;

    return 0;
}

void abort_delay(char_data* wait_ch)
{
    char_data* wait_tmp2;
    REMOVE_BIT(wait_ch->specials.affected_by, AFF_WAITWHEEL);
    REMOVE_BIT(wait_ch->specials.affected_by, AFF_WAITING);

    if (wait_ch == waiting_list) {
        waiting_list = wait_ch->delay.next;
    } else {
        for (wait_tmp2 = waiting_list; wait_tmp2; wait_tmp2 = wait_tmp2->delay.next)
            if (wait_tmp2->delay.next == wait_ch)
                break;
        if (wait_tmp2)
            wait_tmp2->delay.next = wait_ch->delay.next;
    }
    wait_ch->delay.next = 0;
    wait_ch->delay.wait_value = 0;
    wait_ch->delay.priority = 0;

    if (in_waiting_list(wait_ch)) {
        log("SYSERR: abort_delay, character remaining in waiting_list");
        abort_delay(wait_ch);
    }
}

void stat_update()
{
    stat_ticks_passed++;

    for (descriptor_data* point = descriptor_list; point; point = point->next) {
        if (point->character && (STATE(point) == CON_PLYNG)) {
            if (GET_LEVEL(point->character) < LEVEL_IMMORT) {
                stat_mortals_counter++;
                if (GET_RACE(point->character) < 10 && GET_RACE(point->character) != 0) {
                    stat_whitie_counter++;
                    if (GET_LEVEL(point->character) >= 30) {
                        stat_whitie_legend_counter++;
                    }
                }
                if (GET_RACE(point->character) >= 10) {
                    stat_darkie_counter++;
                    if (GET_LEVEL(point->character) >= 30) {
                        stat_darkie_legend_counter++;
                    }
                }

            } else {
                stat_immortals_counter++;
            }
        }
    }
}
