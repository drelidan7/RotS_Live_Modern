/* obj_files.cc */

// Carved out of objsave.cpp (persist-split wave, PS Task 2). Holds the P
// (persistence) inventory per the plan's Evidence base: the account-backed
// object staging map and its key/take/stage/clear helpers, the file-IO
// primitives (read_open_file_contents/read_binary_file_contents), the
// player-object path helpers (player_object_bucket_path/Crash_get_filename/
// player_objects_json_path), the JSON writer (write_player_objects_json),
// Crash_get_file_by_name/Crash_delete_file/Crash_delete_crashfile/
// Crash_clean_file/update_obj_file, register_char_teardown_hook (wiring for
// clear_account_backed_object_bytes_for_character), plus the pure scattered
// helpers Crash_is_unrentable/cost_per_day/secs_to_unretire. objsave.cpp
// keeps everything else: Crash_obj2record/Crash_collect_objects (they read
// obj_index[] and belong with their G/X orchestrator callers), the alias
// pair, Crash_restore_weight/Crash_calculate_rent/Crash_report_rent, and
// all load/extract/rent/receptionist flow, including Crash_load()/
// Crash_listrent() which stay but call back into three of this TU's
// functions cross-TU (see the promotion comment below). Declarations stay
// in handler.h for the public API; objsave.cpp keeps file-local prototypes
// for the handful of helpers handler.h never declared. The dead file-scope
// `FILE* fd;` (objsave.cpp's prior line 89, zero references) was deleted,
// not moved -- the one deliberate deletion in this carve.

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <format>
#include <string>
#include <string_view>
#include <unordered_map>

#include "account_management.h"
#include "db.h"
#include "entity_hooks.h"
#include "handler.h"
#include "objects_json.h"
#include "platform_compat.h"
#include "rots/core/character.h"
#include "rots/core/object.h"
#include "text_view.h"
#include "utils.h"

// update_obj_file() (below) walks the player table; objsave.cpp declared
// these two as file-local externs (no header owns them).
extern struct player_index_element* player_table;
extern int top_of_p_table;

namespace {

// Keyed by lowercased character name. Populated once at account-linked login
// (interpre.cpp's login-staging call, which decodes whatever bytes account
// storage/the legacy runtime file produced into an ObjectSaveData before
// staging it -- Crash_load never has to touch a binary decoder for this
// source) and consumed exactly once by Crash_load's account-staged fallback.
std::unordered_map<std::string, objects_json::ObjectSaveData> g_staged_account_backed_object_data;

std::string account_backed_object_stage_key(const char_data* character)
{
    if (character == nullptr || GET_NAME(character) == nullptr || *GET_NAME(character) == '\0')
        return "";

    std::string key = GET_NAME(character);
    for (char& ch : key)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return key;
}

} // namespace

// Promoted out of this anonymous namespace (persist-split PS Task 2
// deviation, mirroring color_convert.cpp's sync_color_slot_foreground_from_ansi
// precedent): objsave.cpp's Crash_load()/Crash_listrent() (stay behind --
// they are load/rent flow, not codec) still call all three cross-TU after
// this carve, a duplicate-hazard the PS Task 2 classification's line-range
// inventory didn't flag. Declared locally in objsave.cpp (no header owns
// these file-IO internals). Bodies are otherwise byte-identical to their
// prior anonymous-namespace versions.
bool take_staged_account_backed_object_data_for_character(const char_data* character, objects_json::ObjectSaveData* data)
{
    const std::string stage_key = account_backed_object_stage_key(character);
    if (stage_key.empty())
        return false;

    const auto it = g_staged_account_backed_object_data.find(stage_key);
    if (it == g_staged_account_backed_object_data.end())
        return false;

    *data = std::move(it->second);
    g_staged_account_backed_object_data.erase(it);
    return true;
}

// Reads the remainder of an already-open stream into `bytes`. Does not close
// `file` -- the caller owns that (it may need the handle for other reasons,
// e.g. Crash_get_file_by_name's diagnostic-logging-on-open-failure contract).
bool read_open_file_contents(FILE* file, std::string* bytes)
{
    if (file == nullptr || bytes == nullptr)
        return false;

    std::string loaded_bytes;
    char buffer[1024];
    while (true) {
        const size_t bytes_read = std::fread(buffer, sizeof(char), sizeof(buffer), file);
        if (bytes_read > 0)
            loaded_bytes.append(buffer, bytes_read);

        if (bytes_read < sizeof(buffer)) {
            if (std::ferror(file))
                return false;
            break;
        }
    }

    *bytes = std::move(loaded_bytes);
    return true;
}

bool read_binary_file_contents(std::string_view path, std::string* bytes)
{
    if (bytes == nullptr)
        return false;

    const std::string path_owner(rots::text::truncate_at_null(path));
    FILE* file = std::fopen(path_owner.c_str(), "rb");
    if (file == nullptr)
        return false;

    const bool read_ok = read_open_file_contents(file, bytes);
    std::fclose(file);
    return read_ok;
}

// Builds a fresh, empty save (an RENT_CRASH header with no objects/board
// points/aliases/followers) -- the JSON-era equivalent of what the old
// build_empty_account_backed_object_bytes() produced in binary, used when an
// account-linked character has no object-save data yet.
objects_json::ObjectSaveData build_default_account_backed_object_data()
{
    objects_json::ObjectSaveData data;
    data.rent.rentcode = RENT_CRASH;
    return data;
}

void stage_account_backed_object_data_for_character(const struct char_data* ch, const objects_json::ObjectSaveData& data)
{
    const std::string stage_key = account_backed_object_stage_key(ch);
    if (stage_key.empty())
        return;

    g_staged_account_backed_object_data[stage_key] = data;
}

void clear_account_backed_object_bytes_for_character(const struct char_data* ch)
{
    const std::string stage_key = account_backed_object_stage_key(ch);
    if (stage_key.empty())
        return;

    g_staged_account_backed_object_data.erase(stage_key);
}

// Registers the hook above as entity_hooks.h's char-teardown notification
// (entity-seed Task 5). Called once from run_the_game(), before boot_db().
void register_char_teardown_hook()
{
    rots::entity::set_char_teardown_hook(clear_account_backed_object_bytes_for_character);
}

namespace {

// Shared bucket-directory + lowercased-name resolution, factored out of
// Crash_get_filename so player_objects_json_path (the JSON writer/reader's
// path helper, added in this task) doesn't duplicate the bucket switch.
// Returns "" for an empty name; otherwise "plrobjs/<BUCKET>/<lowercased-name>"
// with no extension.
std::string player_object_bucket_path(std::string_view original_name)
{
    original_name = rots::text::truncate_at_null(original_name);
    if (original_name.empty())
        return std::string();

    std::string name(original_name.substr(0, 29));
    for (char& character : name)
        character = static_cast<char>(tolower(static_cast<unsigned char>(character)));

    const char* bucket;
    switch (name[0]) {
    case 'a':
    case 'b':
    case 'c':
    case 'd':
    case 'e':
        bucket = "A-E";
        break;
    case 'f':
    case 'g':
    case 'h':
    case 'i':
    case 'j':
        bucket = "F-J";
        break;
    case 'k':
    case 'l':
    case 'm':
    case 'n':
    case 'o':
        bucket = "K-O";
        break;
    case 'p':
    case 'q':
    case 'r':
    case 's':
    case 't':
        bucket = "P-T";
        break;
    case 'u':
    case 'v':
    case 'w':
    case 'x':
    case 'y':
    case 'z':
        bucket = "U-Z";
        break;
    default:
        bucket = "ZZZ";
        break;
    }

    return std::string("plrobjs/") + bucket + "/" + name;
}

} // namespace

int Crash_get_filename(std::string_view original_name, char* filename)
{
    const std::string base = player_object_bucket_path(original_name);
    if (base.empty())
        return 0;

    strcpy(filename, (base + ".obj").c_str());
    return 1;
}

// The on-disk convention player objects live at now that JSON is primary:
// same bucket directory as the legacy .obj file, ".objs.json" extension.
// Shared by write_player_objects_json (the sole writer) and Crash_load's
// reader-order-first check.
std::string player_objects_json_path(std::string_view player_name)
{
    const std::string base = player_object_bucket_path(player_name);
    if (base.empty())
        return std::string();

    return base + ".objs.json";
}

// Single serialization point (design constraint 1): serializes `data`, writes
// it to a temp file in the player's bucket directory, and renames it over the
// target so a reader never observes a partially-written file. Also mirrors
// the same JSON string through the account-refresh path for linked
// characters (best-effort -- a mirror failure is logged but does not fail
// the primary save, matching the old refresh_account_backed_object_file's
// best-effort semantics).
bool write_player_objects_json(std::string_view player_name, const objects_json::ObjectSaveData& data, std::string* error)
{
    player_name = rots::text::truncate_at_null(player_name);
    if (player_name.empty()) {
        if (error)
            *error = "Player name must not be empty.";
        return false;
    }

    const std::string path = player_objects_json_path(player_name);
    if (path.empty()) {
        if (error)
            *error = "Unable to resolve objects JSON path.";
        return false;
    }

    const std::string json = objects_json::serialize_objects_to_json(data);
    const std::string temp_path = path + ".tmp";

    FILE* temp_file = std::fopen(temp_path.c_str(), "wb");
    if (temp_file == nullptr) {
        if (error)
            *error = std::string("Unable to open temporary objects file '") + temp_path + "': " + strerror(errno);
        return false;
    }

    const size_t bytes_written = json.empty() ? 0 : std::fwrite(json.data(), sizeof(char), json.size(), temp_file);
    const int flush_result = std::fflush(temp_file);
    const int close_result = std::fclose(temp_file);

    if (bytes_written != json.size() || flush_result != 0 || close_result != 0) {
        std::remove(temp_path.c_str());
        if (error)
            *error = std::string("Failed to write temporary objects file '") + temp_path + "'.";
        return false;
    }

    if (rots_rename_replace(temp_path, path) != 0) {
        const std::string rename_error = strerror(errno);
        std::remove(temp_path.c_str());
        if (error)
            *error = "Failed to move temporary objects file into place: " + rename_error;
        return false;
    }

    std::string mirror_error;
    if (!account::write_linked_character_object_json_file(".", player_name, data, &mirror_error) && !mirror_error.empty()) {
        log(std::format("SYSERR: failed to refresh account-native object file for {}: {}", player_name, mirror_error));
    }

    if (error)
        error->clear();
    return true;
}

FILE* Crash_get_file_by_name(std::string_view name, std::string_view mode)
{
    FILE* fp;

    if (!Crash_get_filename(name, buf))
        return 0;
    const std::string mode_owner(rots::text::truncate_at_null(mode));
    if (!(fp = fopen(buf, mode_owner.c_str()))) {
        const bool suppress_missing_read_side_file = (errno == ENOENT) && !mode_owner.empty() && (mode_owner[0] == 'r');
        if (!suppress_missing_read_side_file) {
            log(std::format("SYSERR: unable to open crashsave file '{}' for {}: {}", buf, name,
                strerror(errno))
                    );
        }
        return 0;
    }
    return fp;
}

int Crash_delete_file(std::string_view name)
{

    char filename[50];
    FILE* fl;
    int deleted_something = 0;

    if (!Crash_get_filename(name, filename))
        return 0;
    if ((fl = fopen(filename, "rb"))) {
        fclose(fl);

        if (unlink(filename) < 0) {
            if (errno != ENOENT) { /* if it fails, NOT because of no file */
                perror(std::format("SYSERR: deleting crash file {} (2)", static_cast<const char*>(filename)).c_str());
            }
        } else {
            deleted_something = 1;
        }
    } else if (errno != ENOENT) { /* if it fails but NOT because of no file */
        perror(std::format("SYSERR: deleting crash file {} (1)", static_cast<const char*>(filename)).c_str());
    }

    // JSON is now the primary format (this task): a deleted character's
    // .objs.json must go too, or a later player who takes the same name
    // would silently inherit the old character's rent contents on first
    // load (Crash_load's JSON-first check has no other way to know the
    // file is stale).
    const std::string json_path = player_objects_json_path(name);
    if (!json_path.empty()) {
        if (std::remove(json_path.c_str()) == 0)
            deleted_something = 1;
        else if (errno != ENOENT) {
            perror(std::format("SYSERR: deleting crash file {} (json)", json_path).c_str());
        }
    }

    return deleted_something;
}

int Crash_delete_crashfile(struct char_data* ch)
{

    char fname[MAX_INPUT_LENGTH];
    // Value-init: a truncated/empty crash file leaves fread() short and
    // rent.rentcode was then an indeterminate read (MSVC C4701/UB); zeroed it
    // is deterministically "not RENT_CRASH" (no delete).
    struct rent_info rent = {};
    FILE* fl;

    if (!Crash_get_filename(GET_NAME(ch), fname))
        return 0;
    if (!(fl = fopen(fname, "rb"))) {
        if (errno != ENOENT) { /* if it fails, NOT because of no file */
            perror(std::format("SYSERR: checking for crash file {} (3)", static_cast<const char*>(fname)).c_str());
        }
        return 0;
    }

    if (!feof(fl))
        fread(&rent, sizeof(struct rent_info), 1, fl);
    fclose(fl);
    if (rent.rentcode == RENT_CRASH)
        Crash_delete_file(GET_NAME(ch));
    return 1;
}

int Crash_clean_file(char* name)
{

    char fname[MAX_STRING_LENGTH];
    struct rent_info rent;

    FILE* fl;

    if (!Crash_get_filename(name, fname))
        return 0;
    /*
     * open for write so that permission problems will be flagged now,
     * at boot time.
     */

    if (!(fl = fopen(fname, "r+b"))) {
        if (errno != ENOENT) { /* if it fails, NOT because of no file */
            perror(std::format("SYSERR: OPENING OBJECT FILE {} (4)", static_cast<const char*>(fname)).c_str());
        }
        return 0;
    }
    if (!feof(fl))
        fread(&rent, sizeof(struct rent_info), 1, fl);
    fclose(fl);
    return 0;
}

void update_obj_file(void)
{

    int i;

    for (i = 0; i <= top_of_p_table; i++)
        Crash_clean_file((player_table + i)->name);
    return;
}

// Pure scattered helpers relocated here per the Evidence base inventory
// (objsave.cpp's prior lines ~1288/~1904/~1910). objsave.cpp keeps a local
// prototype for cost_per_day and Crash_is_unrentable (both still called
// from its rent/extract flow); secs_to_unretire has no caller left in
// objsave.cpp itself (its callers, act_move.cpp/interpre.cpp, already
// declare it locally via `extern`).

int Crash_is_unrentable(struct obj_data* obj)
{
    if (!obj)
        return 0;

    if (IS_SET(obj->obj_flags.extra_flags, ITEM_NORENT) || (obj->obj_flags.cost_per_day < -1) || (obj->item_number <= -1) || (GET_ITEM_TYPE(obj) == ITEM_KEY))
        return 1;

    return 0;
}

int cost_per_day(struct obj_data* obj)
{
    return (((obj->obj_flags.cost_per_day == -1) ? obj->obj_flags.cost / 100 : obj->obj_flags.cost_per_day) / ((obj->obj_flags.level <= 5) ? 8 : 4));
}

#define RENT_TIME 2592000 /* number of seconds one must remain retired */
long secs_to_unretire(struct char_data* ch)
/* returns the time, in seconds, until one may unretire */
{
    if (!IS_SET(PLR_FLAGS(ch), PLR_RETIRED))
        return 0;

    return (ch->specials2.retiredon + RENT_TIME) - time(0);
}
