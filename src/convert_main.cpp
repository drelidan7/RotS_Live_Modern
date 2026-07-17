// convert_main.cc -- entry point for `rots_convert`, the standalone character
// converter (db.cpp-split Task 3, spec Sec4b). This is a second executable
// (its own main(), no ageland game loop) that links only
// RotS::platform + RotS::core + the character-conversion persist subset
// (db_players.cpp/entity_lifecycle.cpp + the JSON/account codecs) -- NO
// world/combat/commands/app translation units. It exists to prove, at CI
// link time, that the persist path is genuinely decoupled from the game: if
// a future change re-welds db_players.cpp/entity_lifecycle.cpp to
// combat/world/commands, this executable fails to link and the build
// breaks. See docs/superpowers/specs/2026-07-16-library-architecture-design.md
// Sec4b and docs/superpowers/plans/2026-07-17-db-split-and-rots-convert.md
// Task 3 for the full rationale; convert_stubs.cpp documents every symbol
// this executable substitutes for its excluded app/combat home.
//
// Usage: rots_convert --lib <libdir> [--dry-run] [--character <name>]
//   --lib <libdir>      Required. Root directory containing players/, exactly
//                        like the `lib/` the game boots against (relative
//                        paths inside player files, e.g. "players/A-E/...",
//                        are resolved against this directory after chdir).
//   --dry-run            Load every character (build_player_index() +
//                        load_char()/store_to_char()) but do not call
//                        save_char() -- report what WOULD be converted
//                        without writing anything.
//   --character <name>   Convert only the named character instead of the
//                        whole player_table.
//
// Conversion strategy (byte-identical by construction): this driver calls
// the EXACT SAME functions a live login uses -- load_char() (db_players.cpp)
// to decode the on-disk record into a char_file_u, store_to_char()
// (db_players.cpp) to populate a scratch char_data from it, and save_char()
// (db_players.cpp) to write it back out. Nothing here re-implements any
// persistence logic; if the on-disk format changes, this driver does not
// need to change with it.
//
// THE LOAD-ROOM CHECKPOINT (read this before touching the save_char call
// below): store_to_char() sets `ch->in_room = GET_LOADROOM(ch)` --
// GET_LOADROOM expands to `ch->specials2.load_room`, which was just copied
// verbatim from the on-disk record (char_file_u::specials2.load_room). That
// field is persisted as a room VNUM (see db_players.cpp's
// write_player_text(): `fprintf(pf, "load_room   %d\n",
// chd.specials2.load_room)`), NOT a world[] array index -- so immediately
// after store_to_char(), ch->in_room momentarily holds a VNUM masquerading
// as the world[]-index-shaped field. That is a pre-existing legacy quirk
// (see objsave.cpp's calc_load_room()/load_character(), which are the code
// paths that actually convert it to a real world[] index via real_room()
// before ever using it as one) -- rots_convert never reaches that code, so
// it never matters here.
//
// save_char()'s guard is: `if ((load_room == NOWHERE) && (ch->in_room !=
// NOWHERE)) load_room = world_room_vnum(ch->in_room);` -- i.e. it ONLY
// dereferences ch->in_room as a world[] index (via the world_room_vnum()
// seam) when the CALLER passes load_room == NOWHERE while the character
// is (as far as save_char can tell) "somewhere". The live MUD's own
// just-loaded-not-yet-in-world call sites (interpre.cpp:3112, 3126, 3363 --
// the account-character-selection/login-verification paths, the closest
// live analogue to what this converter does) all pass
// `d->character->specials2.load_room` EXPLICITLY as the load_room argument,
// not NOWHERE. This driver matches that exact convention below. Since
// store_to_char() already set ch->in_room to that same
// ch->specials2.load_room value, passing it again as the explicit load_room
// argument means `load_room == ch->in_room` always holds at the guard --
// so the guard's `load_room == NOWHERE` and `ch->in_room != NOWHERE`
// branches can never BOTH be true (they would require the same value to be
// simultaneously NOWHERE and not-NOWHERE). world_room_vnum() is therefore
// PROVABLY UNREACHABLE from this call site, for every character, not just
// well-formed ones -- see convert_stubs.cpp's world_room_vnum() entry for
// the stub this leaves in place (a loud, never-supposed-to-fire guard, not
// a silent behavioral substitute).
//
// save_char() also refuses to save entirely when `IS_NPC(ch) ||
// !ch->desc` (both true for a converter-loaded PC with no live session).
// Player characters are never IS_NPC (clear_char(MOB_VOID) never sets
// MOB_ISNPC), so the guard reduces to needing a non-null ch->desc. This
// driver attaches the address of a stack-local descriptor_data to satisfy
// that null check -- mirroring the existing test pattern in
// tests/db_loader_tests.cpp (write_valid_legacy_player_file():
// `character->desc = &descriptor;`). notify_char is passed 0, so
// save_char() itself never dereferences the fake descriptor's contents
// (its only use of ch->desc is the truthiness check; the only content read
// off *ch->desc inside save_char() would be through a notify
// send_to_char(), which the 0 argument suppresses) -- BUT
// write_player_text() (db_players.cpp), which save_char() calls
// transitively for every non-account-native character, reads the
// PASSWORD and HOST to persist directly off `ch->desc->pwd`/
// `ch->desc->host`, not off the char_file_u this driver just loaded. A
// default-constructed (all-zero) descriptor here would therefore silently
// WIPE every converted character's password and last-known host on save --
// this was caught empirically by this executable's own functional smoke
// test against a real player file (see the report). convert_one_character()
// below copies both fields from loaded_record before calling
// store_to_char()/save_char(), mirroring interpre.cpp's own
// `strncpy(d->pwd, tmp_store.pwd, MAX_PWD_LENGTH)` convention exactly.

#include "db.h"
#include "platform_compat.h"
#include "rots/core/character.h"
#include "rots/core/descriptor.h"
#include "rots/persist/file_formats.h"
#include "rots/platform/log.h"
#include "utils.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

// db_players.cpp/db_boot.cpp/interpre.cpp/etc. all declare their own
// file-local `extern` for these two globals rather than reaching them
// through a header (see e.g. interpre.cpp:71/81, act_wiz.cpp:59/71) --
// matching that established convention here rather than adding a new
// db.h-wide declaration.
extern struct player_index_element* player_table;
extern int top_of_p_table;

// build_player_index() itself follows the same pattern: db_boot.cpp's own
// boot_db() forward-declares it as a function-local extern (db_boot.cpp:157)
// rather than through db.h -- it is defined in db_players.cpp (which this
// executable links), just never header-declared anywhere in the tree.
extern void build_player_index(void);

namespace {

// Command-line options this driver understands; parsed once in main() and
// threaded through convert_one_character() and the player_table loop below.
struct ConvertOptions {
    // Root directory to chdir() into before build_player_index() -- the same
    // directory the game boots against (contains players/, accounts/, ...).
    std::string lib_dir;
    // --dry-run: load every character but skip the save_char() write, so the
    // tool can be smoke-tested against real data without mutating it.
    bool dry_run = false;
    // --character <name>: when non-empty, convert only this one character
    // instead of walking the whole player_table.
    std::string only_character;
};

void print_usage(const char* argv0)
{
    std::cerr << "Usage: " << argv0 << " --lib <libdir> [--dry-run] [--character <name>]\n"
              << "\n"
              << "  --lib <libdir>       Required. Root directory to convert (the same\n"
              << "                       directory layout the game boots against).\n"
              << "  --dry-run            Load every character but do not write anything.\n"
              << "  --character <name>   Convert only the named character.\n"
              << "  --help               Print this message and exit.\n";
}

// Loads, and (unless dry_run) re-saves, exactly one player_table entry via
// the same load_char()/store_to_char()/save_char() path a live login uses.
// Returns true if the character loaded successfully (independent of whether
// dry_run skipped the save), false on a load failure -- the caller counts
// failures but keeps converting the rest of the table.
bool convert_one_character(player_index_element& entry, bool dry_run)
{
    // load_char() takes a non-const char* (it lowercases the name in place
    // via load_player()); player_table entries are mutated by design here,
    // same as every other production caller (e.g. load_player() itself).
    char_file_u loaded_record { };
    const int load_result = load_char(entry.name, &loaded_record);
    if (load_result < 0) {
        std::cerr << "rots_convert: failed to load '" << entry.name << "'\n";
        return false;
    }

    // make_char_data(MOB_VOID): the same clean-scope-scratch-character
    // pattern db.h documents char_data_ptr for (a char loaded to inspect/
    // convert and then discarded, never inserted into a world graph).
    char_data_ptr character = make_char_data(MOB_VOID);

    // Stack-local descriptor purely to satisfy save_char()'s `!ch->desc`
    // refusal-to-save guard -- notify_char=0 below suppresses the only
    // send_to_char() call in save_char() that would dereference it (see the
    // load-room checkpoint comment at the top of this file). BUT its pwd/host
    // fields are NOT cosmetic: write_player_text() (db_players.cpp) composes
    // the persisted password/host directly from `ch->desc->pwd`/
    // `ch->desc->host`, not from the char_file_u this function just loaded
    // (`strcpy(chd.pwd, ch->desc->pwd); strncpy(chd.host, ch->desc->host,
    // HOST_LEN);` -- the same fields interpre.cpp's login flow populates via
    // `strncpy(d->pwd, tmp_store.pwd, MAX_PWD_LENGTH)` right after its own
    // store_to_char() call). A default-constructed (all-zero) descriptor
    // here would silently WIPE every converted character's password and
    // last-known host on save -- confirmed empirically against a real
    // player file during this executable's own smoke test. Copy both
    // straight from loaded_record (the just-decoded on-disk record) before
    // store_to_char()/save_char() run, mirroring interpre.cpp's convention
    // exactly.
    descriptor_data fake_descriptor { };
    std::strncpy(fake_descriptor.pwd, loaded_record.pwd, MAX_PWD_LENGTH);
    fake_descriptor.pwd[MAX_PWD_LENGTH] = '\0';
    std::strncpy(fake_descriptor.host, loaded_record.host, HOST_LEN);
    fake_descriptor.host[HOST_LEN] = '\0';
    character->desc = &fake_descriptor;

    store_to_char(&loaded_record, character.get());

    std::cout << "  " << entry.name << " (level " << GET_LEVEL(character) << ", race "
              << GET_RACE(character) << ", load_room " << character->specials2.load_room << ")"
              << (dry_run ? " [dry-run: not saved]" : " [saved]") << "\n";

    if (!dry_run) {
        // See the load-room checkpoint comment at the top of this file:
        // passing character->specials2.load_room explicitly (not NOWHERE)
        // mirrors interpre.cpp's own just-loaded/not-yet-in-world call
        // sites and provably never exercises the world_room_vnum() seam.
        save_char(character.get(), character->specials2.load_room, 0);
    }

    // save_char()'s own guards (IS_NPC/!desc, player_table lookup failure)
    // only log+return; they don't report failure to the caller. A
    // successful load is what this driver considers success -- the save
    // path re-uses the exact function the MUD uses and is proven correct
    // by Task 4's equivalence test, not re-validated by this driver.
    character->desc = nullptr;
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    ConvertOptions options;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--lib") {
            if (i + 1 >= argc) {
                std::cerr << "rots_convert: --lib requires an argument\n";
                print_usage(argv[0]);
                return 1;
            }
            options.lib_dir = argv[++i];
        } else if (arg == "--dry-run") {
            options.dry_run = true;
        } else if (arg == "--character") {
            if (i + 1 >= argc) {
                std::cerr << "rots_convert: --character requires an argument\n";
                print_usage(argv[0]);
                return 1;
            }
            options.only_character = argv[++i];
        } else {
            std::cerr << "rots_convert: unrecognized argument '" << arg << "'\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (options.lib_dir.empty()) {
        std::cerr << "rots_convert: --lib <libdir> is required\n";
        print_usage(argv[0]);
        return 1;
    }

    std::error_code chdir_error;
    std::filesystem::current_path(options.lib_dir, chdir_error);
    if (chdir_error) {
        std::cerr << "rots_convert: could not chdir to '" << options.lib_dir
                  << "': " << chdir_error.message() << "\n";
        return 1;
    }

    // Same boot-time call the MUD makes (db_boot.cpp's boot_db()) to
    // populate player_table/top_of_p_table from players/<A-E|F-J|...>/ plus
    // the account-native index -- reused verbatim, not re-implemented.
    build_player_index();

    std::cout << "rots_convert: " << (top_of_p_table + 1) << " character(s) indexed under '"
              << options.lib_dir << "'" << (options.dry_run ? " (dry-run)" : "") << "\n";

    int converted = 0;
    int failed = 0;
    bool found_requested_character = options.only_character.empty();

    for (int i = 0; i <= top_of_p_table; ++i) {
        player_index_element& entry = player_table[i];
        if (entry.name == nullptr || entry.name[0] == '\0') {
            // move_char_deleted() blanks a slot's name in place rather than
            // compacting the table -- skip tombstoned entries.
            continue;
        }
        if (!options.only_character.empty() && options.only_character != entry.name) {
            continue;
        }
        found_requested_character = true;

        if (convert_one_character(entry, options.dry_run)) {
            ++converted;
        } else {
            ++failed;
        }
    }

    if (!found_requested_character) {
        std::cerr << "rots_convert: no character named '" << options.only_character
                  << "' found in the player index\n";
        return 1;
    }

    std::cout << "rots_convert: " << converted << " converted, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
