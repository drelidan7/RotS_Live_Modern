/* ************************************************************************
 *   File: objsave.c                                     Part of CircleMUD *
 *  Usage: loading/saving player objects for rent and crash-save           *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#include "platdef.h"
#include <ctype.h>
#include <cctype>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform_compat.h"
#include "char_utils.h"
#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "limits.h"
#include "pkill.h"
#include "spells.h"
#include "structs.h"
#include "text_view.h"
#include "utils.h"
#include "account_management.h"
#include "objects_json.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <format>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

/* these factors should be unique integers */
#define RENT_FACTOR 1
#define CRYO_FACTOR 4
#define RENT_HALFTIME 300
#define FORCERENT_FACTOR 10

// defines for follower saving
#define FOL_MOUNT 0
#define FOL_ORC_FRIEND 1
#define FOL_TAMED 2
#define FOL_GUARDIAN 3

#define MIN_RANK 1
#define MAX_RANK 10

int cost_per_day(struct obj_data* obj);

extern struct room_data world;
extern struct index_data* mob_index;
extern struct index_data* obj_index;
extern struct descriptor_data* descriptor_list;
extern struct player_index_element* player_table;
extern int top_of_p_table;
extern int r_mortal_start_room[];
extern int r_bugged_start_room;

/* Extern functions */

ACMD(do_action);
ACMD(do_tell);
SPECIAL(receptionist);
SPECIAL(cryogenicist);

void Crash_alias_load(struct char_data* ch, const objects_json::ObjectSaveData& data);
void Crash_follower_load(struct char_data* ch, const objects_json::ObjectSaveData& data);
int calc_load_room(struct char_data* ch, int load_result);
int Crash_get_filename(std::string_view original_name, char* filename);

FILE* fd;
int Crash_is_unrentable(struct obj_data* obj);

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

} // namespace

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

extern int generic_scalp;

obj_data* load_scalp(int number);

struct obj_data*

// Loads an object with a virtual number of `object.item_number' and stores
// it in `obj'. `obj' is then the object "prototype." we modify that
// prototype with what is stored in `object' -- the in-memory ObjectSaveData
// counterpart of the old obj_file_elem, populated identically regardless of
// whether it came from JSON, a legacy .obj file, or account-staged data.
Crash_obj2char(struct char_data*, const objects_json::ObjectRecord& object)
{
    struct obj_data* obj;

    if (real_object(object.item_number) > -1) {
        /* somewhat awkward, accounting for scalps... */
        if (object.item_number == generic_scalp) {

            /* player id numbers exceed sh_int size, so we started stashing the id in extra_flags.
             * scalp items don't use them, and load scalp knows where to put it. */
            int head_number = object.values[4];
            if (object.extra_flags != 0) {
                head_number = object.extra_flags;
            }
            obj = load_scalp(head_number);
        } else {
            obj = read_object(object.item_number, VIRT);
            obj->obj_flags.extra_flags = object.extra_flags;
            obj->obj_flags.timer = object.timer;
            obj->obj_flags.bitvector = object.bitvector;
            obj->loaded_by = object.loaded_by;

            /*
             * drink containers and light sources are the only types of items
             * that should be different in the obj_file_elem than our prototypes
             */
            if (obj->obj_flags.type_flag == ITEM_LIGHT || obj->obj_flags.type_flag == ITEM_DRINKCON) {
                obj->obj_flags.value[0] = object.values[0];
                obj->obj_flags.value[1] = object.values[1];
                obj->obj_flags.value[2] = object.values[2];
                obj->obj_flags.value[3] = object.values[3];
                obj->obj_flags.value[4] = object.values[4];
            }

            for (int j = 0; j < MAX_OBJ_AFFECT; j++) {
                obj->affected[j].location = static_cast<byte>(object.affects[j].location);
                obj->affected[j].modifier = object.affects[j].modifier;
            }
        }
        return obj;
    }
    return 0;
}

constexpr std::string_view overflow_str = "The buffer was overflowed, aborting.\r\n";
constexpr size_t overflow_len = overflow_str.size() + 1;

// Reader order mirrors Crash_load (design constraint carried over from Task
// 2): <name>.objs.json first (deserialize -- a corrupt JSON file is reported
// and this falls through to the legacy <name>.obj file below, unlike
// Crash_load, which refuses to fall back to a stale binary on a hard JSON
// error) -> legacy <name>.obj via the same portable decoder Crash_load uses.
// Only when both attempts leave have_data false does listrent report "no
// rent file". Both paths render from the same
// ObjectSaveData.objects list, so the output format is identical regardless
// of which storage format backs it.
//
// This also fixes a latent quirk in the old binary-only implementation: it
// looped `fread`ing raw obj_file_elem-sized chunks until EOF with no sentinel
// check, so on files with more than the sentinel-terminated top-level object
// list (i.e. every real rent file -- board points/aliases/followers follow),
// it kept "reading objects" out of that trailing binary data. Those bogus
// reads were usually harmless (real_object() rejects the garbage item
// numbers), but not guaranteed to be. Decoding through
// legacy_object_save_data_from_binary (as Crash_load already does) stops at
// the real sentinel, so this can no longer happen.
void Crash_listrent(struct char_data* ch, char* name)
{
    char buf[MAX_STRING_LENGTH];
    size_t bufpt, max_space;

    objects_json::ObjectSaveData data;
    bool have_data = false;

    const std::string json_path = player_objects_json_path(name);
    std::string json_contents;
    if (!json_path.empty() && read_binary_file_contents(json_path, &json_contents)) {
        std::string error_message;
        if (objects_json::deserialize_objects_from_json(json_contents, &data, &error_message)) {
            have_data = true;
        } else {
            log(std::format("SYSERR: corrupt objects JSON '{}' for listrent {}: {}", json_path, name, error_message));
        }
    }

    if (!have_data) {
        FILE* legacy_file = Crash_get_file_by_name(name, "rb");
        if (legacy_file != nullptr) {
            std::string legacy_bytes;
            const bool read_ok = read_open_file_contents(legacy_file, &legacy_bytes);
            fclose(legacy_file);
            if (read_ok) {
                bool accepted_missing_follower_section = false;
                std::string decode_error;
                if (objects_json::legacy_object_save_data_from_binary(legacy_bytes, &data, &accepted_missing_follower_section, &decode_error)) {
                    have_data = true;
                } else {
                    log(std::format("SYSERR: corrupt legacy object file for listrent {}: {}", name, decode_error));
                }
            }
        }
    }

    if (!have_data) {
        strcpy(buf, std::format("{} has no rent file.\n\r", name).c_str());
        log(buf);
        send_to_char(buf, ch);
        return;
    }

    max_space = MAX_STRING_LENGTH - overflow_len;

    bufpt = snprintf(buf, max_space, "%s rents with:\n\r", name);
    for (const objects_json::ObjectRecord& object : data.objects) {
        if (real_object(object.item_number) > -1) {
            struct obj_data* obj = read_object(object.item_number, VIRT);

            /* If we would overflow the buffer, don't add anymore */

            if (bufpt >= max_space - strlen(obj->short_description) - 1) {
                // overflow_str is fixed data, not a format string -- "%s" form for
                // non-literal-format-string hygiene (it contains no '%' today).
                snprintf(buf + bufpt, overflow_len, "%s", overflow_str.data());
                send_to_char(buf, ch);
                extract_obj(obj);
                return;
            }

            bufpt += snprintf(buf + bufpt, max_space - bufpt, "%s\n\r", obj->short_description);

            extract_obj(obj);
        }
    }
    send_to_char(buf, ch);
}

//============================================================================
// This function fixes the fact that we add containers to characters before we
// add items to them, so the item is being added to the character at 0 weight.
// This allows characters to get infinite dodge.
//============================================================================
void recalc_worn_weight(char_data* character)
{
    extern sh_int encumb_table[MAX_WEAR];
    extern sh_int leg_encumb_table[MAX_WEAR];

    character->specials.worn_weight = 0;
    character->points.encumb = 0;
    character->specials.encumb_weight = 0;
    character->specials2.leg_encumb = 0;

    for (int item_slot = 0; item_slot < MAX_WEAR; ++item_slot) {
        obj_data* item = character->equipment[item_slot];
        if (item) {
            int item_weight = item->get_weight();
            sh_int encumb_value = encumb_table[item_slot];
            sh_int leg_encumb_value = leg_encumb_table[item_slot];

            character->points.encumb += item->obj_flags.value[2] * encumb_value;
            character->specials2.leg_encumb += item->obj_flags.value[2] * leg_encumb_value;

            if (encumb_value > 0) {
                character->specials.encumb_weight += item_weight * encumb_value;
            } else {
                character->specials.encumb_weight += item_weight / 2;
            }

            character->specials.worn_weight += item_weight;
        }
    }
}

FILE* Crash_load(char_data* character)
{
    struct obj_data* equip_array[11];
    struct obj_data* obj;
    int cost, equip_lost;
    // num_of_hours starts at 0: the pre-charge `cost` computation below reads it
    // before the RENT_RENTED/TIMEDOUT/FORCED branch assigns the real value, and
    // the historical uninitialized read was UB (MSVC RTC3 aborts on it -- Phase 3
    // Task 6) with a potential divide-by-zero lurking in (RENT_HALFTIME + garbage).
    int num_of_hours = 0;
    int tmp;
    struct obj_data dummy_sack;
    auto fail_closed = [&character]() -> FILE* {
        REMOVE_BIT(character->specials.affected_by, AFF_TWOHANDED);
        return nullptr;
    };

    clear_object(&dummy_sack);

    equip_lost = 0;

    /* zero out our equipment array */
    for (tmp = 0; tmp < 11; tmp++)
        equip_array[tmp] = 0;

    // Reader order (design constraint 2): <name>.objs.json first (a corrupt
    // JSON file is a hard error -- refuse the load, do NOT fall through to a
    // stale binary); then the legacy <name>.obj via the Task 1 portable
    // decoder; then account-staged bytes (already decoded to an
    // ObjectSaveData at login-staging time -- see interpre.cpp).
    objects_json::ObjectSaveData data;
    bool have_data = false;

    const std::string json_path = player_objects_json_path(GET_NAME(character));
    std::string json_contents;
    if (!json_path.empty() && read_binary_file_contents(json_path, &json_contents)) {
        std::string error_message;
        if (!objects_json::deserialize_objects_from_json(json_contents, &data, &error_message)) {
            log(std::format("SYSERR: corrupt objects JSON '{}' for {}: {}", json_path, GET_NAME(character), error_message));
            return fail_closed();
        }
        have_data = true;
    } else {
        // Crash_get_file_by_name (not a bare fopen) so we keep its existing
        // diagnostic contract: it logs "SYSERR: unable to open crashsave
        // file..." for a genuine open failure but stays silent for the
        // ordinary "no .obj yet" (ENOENT) case, matching what this reader
        // order did before this task.
        FILE* legacy_file = Crash_get_file_by_name(const_cast<char*>(GET_NAME(character)), "r+b");
        std::string legacy_bytes;
        if (legacy_file != nullptr) {
            const bool read_ok = read_open_file_contents(legacy_file, &legacy_bytes);
            std::fclose(legacy_file);
            if (!read_ok) {
                log(std::format("SYSERR: unable to read legacy object file for {}.", GET_NAME(character)));
                return fail_closed();
            }

            bool accepted_missing_follower_section = false;
            std::string error_message;
            if (!objects_json::legacy_object_save_data_from_binary(legacy_bytes, &data, &accepted_missing_follower_section, &error_message)) {
                log(std::format("SYSERR: corrupt legacy object file for {}: {}", GET_NAME(character), error_message));
                return fail_closed();
            }
            have_data = true;
        } else if (take_staged_account_backed_object_data_for_character(character, &data)) {
            have_data = true;
        }
    }

    if (!have_data) {
        REMOVE_BIT(character->specials.affected_by, AFF_TWOHANDED);
        send_to_char("*** Your equipment was lost! Please contact an immortal. ***\n\r", character);
        strcpy(buf, std::format("{} entering game with no equipment.", GET_NAME(character)).c_str());
        GET_ALIAS(character) = 0;
        mudlog(buf, NRM, std::max(LEVEL_IMMORT, GET_INVIS_LEV(character)), TRUE);
        character->specials2.load_room = calc_load_room(character, RENT_UNDEF);
        return nullptr;
    }

    /* ok, we have data. now we find out how much to charge them */
    cost = (data.rent.net_cost_per_hour * RENT_HALFTIME * num_of_hours / (RENT_HALFTIME + num_of_hours));

    /* now we're finding out how long we need to charge them */
    if ((data.rent.rentcode == RENT_RENTED) || (data.rent.rentcode == RENT_TIMEDOUT) || (data.rent.rentcode == RENT_FORCED)) {
        num_of_hours = (time(0) - data.rent.time) / SECS_PER_REAL_HOUR;
        /* RENT FORMULA */
        cost = (data.rent.net_cost_per_hour * RENT_HALFTIME * num_of_hours / (RENT_HALFTIME + num_of_hours));

        /* extra charging for timeouts and link-renters */
        if ((data.rent.rentcode == RENT_TIMEDOUT) || (data.rent.rentcode == RENT_FORCED))
            cost *= FORCERENT_FACTOR;

        cost /= 100; /* TEMPORARY fix for high rent. should be looked into */

        /* can they pay for their rent? */
        if (cost > GET_GOLD(character)) {
            equip_lost = 1;
            strcpy(buf, std::format("{} entering game, rented equipment lost (no $).",
                GET_NAME(character)).c_str());
            mudlog(buf, NRM, std::max(LEVEL_IMMORT, GET_INVIS_LEV(character)), TRUE);
        } else {
            equip_lost = 0;
            GET_GOLD(character) = std::max(GET_GOLD(character) - cost, 0);
        }
    }

    switch (data.rent.rentcode) {
    case RENT_RENTED:
        strcpy(buf, std::format("{} un-renting and entering game.", GET_NAME(character)).c_str());
        mudlog(buf, NRM, std::max(LEVEL_IMMORT, GET_INVIS_LEV(character)), TRUE);
        break;
    case RENT_CRASH:
        strcpy(buf, std::format("{} retrieving crash-saved items and entering game.", GET_NAME(character)).c_str());
        mudlog(buf, NRM, std::max(LEVEL_IMMORT, GET_INVIS_LEV(character)), TRUE);
        break;
    case RENT_CAMP:
        strcpy(buf, std::format("{} un-camping and entering game.", GET_NAME(character)).c_str());
        mudlog(buf, NRM, std::max(LEVEL_IMMORT, GET_INVIS_LEV(character)), TRUE);
        break;
    case RENT_FORCED:
    case RENT_TIMEDOUT:
        strcpy(buf, std::format("{} retrieving force-saved items and entering game.", GET_NAME(character)).c_str());
        mudlog(buf, NRM, std::max(LEVEL_IMMORT, GET_INVIS_LEV(character)), TRUE);
        break;
    case RENT_QUIT:
        strcpy(buf, std::format("{} un-quit and entering game.", GET_NAME(character)).c_str());
        mudlog(buf, NRM, std::max(LEVEL_IMMORT, GET_INVIS_LEV(character)), TRUE);
        break;
    default:
        strcpy(buf, std::format("WARNING: {} entering game with undefined rent code.", GET_NAME(character)).c_str());
        mudlog(buf, NRM, std::max(LEVEL_IMMORT, GET_INVIS_LEV(character)), TRUE);
        break;
    }

    for (const objects_json::ObjectRecord& object : data.objects) {
        if (!equip_lost) {
            int wear_pos = object.wear_pos;

            if (object.item_number < 0)
                obj = &dummy_sack;
            else
                obj = Crash_obj2char(character, object);

            if (!obj) {
                strcpy(buf, std::format("LOAD ERROR, equipment lost for {}.", GET_NAME(character)).c_str());
                log(buf);
                obj = &dummy_sack;
            }

            // We have an object, set the "player touched this" variable to 1.
            obj->touched = 1;

            if (wear_pos < 0)
                wear_pos = MAX_WEAR;
            if (wear_pos < MAX_WEAR) {
                if (obj != &dummy_sack)
                    equip_char(character, obj, wear_pos);
                equip_array[0] = obj;
            } else if ((wear_pos == MAX_WEAR) || !equip_array[0]) {
                if (obj != &dummy_sack)
                    obj_to_char(obj, character);
                equip_array[0] = obj;
            } else {
                if (obj != &dummy_sack)
                    obj_to_obj(obj, equip_array[wear_pos - MAX_WEAR - 1], TRUE);
                equip_array[wear_pos - MAX_WEAR] = obj;
            }
        }
    }
    while (dummy_sack.contains) {
        obj = dummy_sack.contains;
        obj_from_obj(obj);
        obj_to_char(obj, character);
    }
    if (equip_lost) {
        send_to_char("\n\rYou could not afford your rent.\n\r"
                     "Your possesions have been sold to pay your debt!\n\r",
            character);
        if (!(character->specials2.load_room))
            character->specials2.load_room = calc_load_room(character, RENT_UNDEF);
    }

    recalc_worn_weight(character);
    if (IS_TWOHANDED(character) && (!character->equipment[WIELD] || character->equipment[WEAR_SHIELD]))
        REMOVE_BIT(character->specials.affected_by, AFF_TWOHANDED);

    character->specials2.load_room = calc_load_room(character, data.rent.rentcode);

    // Board points/aliases/followers are applied directly from the in-memory
    // ObjectSaveData now (regardless of source) -- there's no FILE* stream
    // left to keep reading from, unlike the old Crash_alias_load/
    // Crash_follower_load(ch, fp) calls load_character used to make after
    // Crash_load returned.
    Crash_alias_load(character, data);
    Crash_follower_load(character, data);

    // The return value is only ever used truthy by load_character (to decide
    // whether to fclose() it); the real payload is already fully applied to
    // `character` above.
    FILE* success_handle = std::tmpfile();
    if (success_handle == nullptr) {
        log(std::format("SYSERR: unable to create success handle for {} after a successful load.", GET_NAME(character)));
    }
    return success_handle;
}

void load_character(struct char_data* ch)
{
    extern struct char_data* character_list;
    FILE* fp;

    if (ch->in_room == NOWHERE)
        ch->in_room = ch->specials2.load_room;

    fp = Crash_load(ch);

    ch->next = character_list;
    character_list = ch;

    char_to_room(ch, ch->specials2.load_room);
    act("$n has entered the game.", TRUE, ch, 0, 0, TO_ROOM);

    // Crash_load already applied board points/aliases/followers directly from
    // its in-memory ObjectSaveData; `fp` (when non-null) is now only a
    // truthy "the load succeeded" signal, not a stream to keep reading from.
    if (fp)
        fclose(fp);
}

int calc_load_room(struct char_data* ch, int load_result)
{
    int load_room;
    int old_room;
    int i;

    extern const int mortal_maze_room[MAX_MAZE_RENT_MAPPINGS][2];
    extern int r_immort_start_room;
    extern int r_frozen_start_room;

    old_room = ch->specials2.load_room;
    load_room = real_room(old_room);

    /* if an immortal's load_room < 0, put them in the imm start room */
    if (GET_LEVEL(ch) >= LEVEL_IMMORT) {
        if (PLR_FLAGGED(ch, PLR_LOADROOM)) {
            if ((load_room = real_room(GET_LOADROOM(ch))) < 0)
                load_room = r_immort_start_room;
        } else
            load_room = r_immort_start_room;
        /* if they're INVSTART then we start them invis */
        if (PLR_FLAGGED(ch, PLR_INVSTART))
            GET_INVIS_LEV(ch) = GET_LEVEL(ch);
    } else {
        /* frozen people go to the frozen realroom */
        if (PLR_FLAGGED(ch, PLR_FROZEN))
            load_room = r_frozen_start_room;
        else {
            if (ch->in_room == NOWHERE)
                load_room = r_mortal_start_room[GET_RACE(ch)];
            else if ((load_room = real_room(ch->in_room)) < 0)
                load_room = r_mortal_start_room[GET_RACE(ch)];

            /* Look through maze mappings. If ch was in a maze room
             * before termination of game, he/she will unrent in the
             * mapped-to rooms. */
            for (i = 0; i < MAX_MAZE_RENT_MAPPINGS; i++)
                if (old_room / 100 == mortal_maze_room[i][0] / 100) { /* room/100 = zone */
                    load_room = real_room(mortal_maze_room[i][1]);
                    break;
                }
        }
    }

    if ((load_result == RENT_CRASH) && (ch->in_room >= EXTENSION_ROOM_HEAD))
        log("Error: objsave.cc tried to load in room > EXTENSION_ROOM_HEAD");
    if (GET_RACE(ch) == 0)
        load_room = r_immort_start_room;
    if (load_room < 0)
        load_room = r_mortal_start_room[GET_RACE(ch)];

    /* here checking for bugged characters */
    if (ch->tmpabilities.str < 1 || ch->abilities.str < 1 || ch->tmpabilities.dex < 1 || ch->abilities.dex < 1 || ch->tmpabilities.move < 1 || ch->points.spirit < 0 || ch->points.spirit > 100000 || ch->tmpabilities.move > 1000)
        load_room = real_room(r_bugged_start_room);

    /* Special exception for characters that aren't level 1 yet... */
    if (!GET_LEVEL(ch))
        load_room = r_mortal_start_room[GET_RACE(ch)];

    return load_room;
}

// Converts one live obj_data into the in-memory record the JSON writer
// stores -- the exact same fields the old Crash_obj2store fwrote as an
// obj_file_elem, just appended to a vector instead of streamed to a FILE*.
objects_json::ObjectRecord Crash_obj2record(obj_data* obj, int pos)
{
    objects_json::ObjectRecord record;

    if (obj->item_number >= 0) {
        record.item_number = obj_index[obj->item_number].virt;
    } else {
        record.item_number = obj->item_number;
    }

    record.values[0] = obj->obj_flags.value[0];
    record.values[1] = obj->obj_flags.value[1];
    record.values[2] = obj->obj_flags.value[2];
    record.values[3] = obj->obj_flags.value[3];
    record.values[4] = obj->obj_flags.value[4];
    record.extra_flags = obj->obj_flags.extra_flags;
    record.weight = obj->obj_flags.weight;
    record.timer = obj->obj_flags.timer;
    record.bitvector = obj->obj_flags.bitvector;
    record.loaded_by = obj->loaded_by;
    for (int index = 0; index < MAX_OBJ_AFFECT; index++) {
        record.affects[index].location = obj->affected[index].location;
        record.affects[index].modifier = obj->affected[index].modifier;
    }
    record.wear_pos = pos;

    // Stash the player_id in extra_flags for scalps, since we have an array of shorts and player
    // ids can exceed that.  Scalp loading knows how to interpret this, and assigns the in-game object
    // an extra_flags of 0.
    if (record.item_number == generic_scalp) {
        record.extra_flags = obj->obj_flags.value[4];
    }

    return record;
}

// Recursive collector mirroring the old Crash_save: appends `obj` and
// everything it contains/its siblings to `records`, in the same order the
// old FILE*-based writer streamed them in (top-level object first, then its
// contents, then the next sibling). Also carries over the same
// contained-object weight bookkeeping Crash_save performed (undone
// afterward by Crash_restore_weight for saves that keep the objects alive).
void Crash_collect_objects(struct obj_data* obj, int pos, std::vector<objects_json::ObjectRecord>* records)
{
    if (!obj)
        return;

    records->push_back(Crash_obj2record(obj, pos));

    const int tmpos = (pos < MAX_WEAR) ? MAX_WEAR : pos;
    Crash_collect_objects(obj->contains, tmpos + 1, records);
    Crash_collect_objects(obj->next_content, pos, records);

    for (struct obj_data* tmp = obj->in_obj; tmp; tmp = tmp->in_obj)
        GET_OBJ_WEIGHT(tmp) -= GET_OBJ_WEIGHT(obj);
}

// Builds the follower list mirroring the old Crash_follower_save, appending
// each NPC follower (and its rentable equipment) plus a ridden mount, if
// any, to `followers` instead of streaming follower_file_elem records to a
// FILE*.
void Crash_collect_followers(struct char_data* ch, std::vector<objects_json::FollowerData>* followers)
{
    extern struct index_data* mob_index;
    struct follow_type *k, *next_fol;

    for (k = ch->followers; k; k = next_fol) {
        next_fol = k->next;
        if (!IS_NPC(k->follower))
            continue;
        if (k->follower->in_room != ch->in_room)
            continue;

        objects_json::FollowerData follower;
        follower.fol_vnum = mob_index[k->follower->nr].virt;
        follower.mount_vnum = 0;
        if (IS_RIDING(k->follower))
            if ((k->follower->mount_data.mount)->mount_data.rider == k->follower)
                follower.mount_vnum = mob_index[k->follower->mount_data.mount->nr].virt;
        if (k->follower->followers)
            if (IS_NPC(k->follower->followers->follower))
                follower.mount_vnum = mob_index[k->follower->followers->follower->nr].virt;
        follower.wimpy = k->follower->specials2.wimp_level;
        follower.exp = k->follower->points.exp;
        if (MOB_FLAGGED(k->follower, MOB_ORC_FRIEND))
            follower.flag_config = FOL_ORC_FRIEND;
        else if (affected_by_spell(k->follower, SKILL_TAME))
            follower.flag_config = FOL_TAMED;
        else if (MOB_FLAGGED(k->follower, MOB_PET))
            follower.flag_config = FOL_GUARDIAN;
        else
            follower.flag_config = FOL_MOUNT;
        follower.spare1 = 0;
        follower.spare2 = 0;

        for (int x = 0; x < MAX_WEAR; x++)
            if (k->follower->equipment[x])
                if (!Crash_is_unrentable(k->follower->equipment[x]))
                    Crash_collect_objects(k->follower->equipment[x], x, &follower.objects);

        followers->push_back(std::move(follower));
    }

    if (IS_RIDING(ch)) {
        objects_json::FollowerData mount;
        mount.fol_vnum = mob_index[ch->mount_data.mount->nr].virt;
        mount.mount_vnum = 0;
        mount.wimpy = 0;
        mount.exp = 0;
        mount.spare1 = 0;
        mount.spare2 = 0;
        mount.flag_config = 0;
        followers->push_back(std::move(mount));
    }
}

// Applies decoded follower data (from JSON, legacy binary, or account-staged
// data -- source no longer matters by this point) to the character,
// spawning each follower mob and its equipment. Mirrors the old
// Crash_follower_load(ch, fp) body exactly, just iterating
// `data.followers`/`follower.objects` vectors instead of reading a FILE*
// stream.
void Crash_follower_load(struct char_data* ch, const objects_json::ObjectSaveData& data)
{
    struct char_data *mob, *mount;
    struct affected_type af;
    struct obj_data* obj;
    int tmp;

    for (const objects_json::FollowerData& fol_elem : data.followers) {
        if ((tmp = real_mobile(fol_elem.fol_vnum)) < 0)
            break;
        mob = read_mobile(tmp, REAL);
        char_to_room(mob, ch->in_room);

        for (const objects_json::ObjectRecord& object : fol_elem.objects) {
            if (object.wear_pos > MAX_WEAR || object.wear_pos < 0)
                continue;

            obj = Crash_obj2char(mob, object);
            if (!obj) {
                log(std::format("LOAD ERROR, equipment lost for follower of {}.", GET_NAME(ch)));
                return;
            }
            equip_char(mob, obj, object.wear_pos);
        }
        switch (fol_elem.flag_config) {
        case FOL_ORC_FRIEND: // We should really have an add_recruit function
            af.type = SKILL_RECRUIT;
            af.duration = -1;
            af.modifier = 0;
            af.location = APPLY_NONE;
            af.bitvector = AFF_CHARM;
            affect_to_char(mob, &af);
            SET_BIT(MOB_FLAGS(mob), MOB_PET);
            break;

        case FOL_TAMED: // need same for tame
        {
            af.type = SKILL_TAME;
            af.duration = -1;
            af.modifier = 0;
            af.location = APPLY_NONE;
            af.bitvector = AFF_CHARM;
            affect_to_char(mob, &af);
            SET_BIT(MOB_FLAGS(mob), MOB_PET);

            // If the mob tame is flagged as aggressive and the mob is tamed,
            // assume that the mob has been calmed and add that spell effect to it.
            if (MOB_FLAGGED(mob, MOB_AGGRESSIVE)) {
                affected_type calm_affect;
                calm_affect.type = SKILL_CALM;
                calm_affect.duration = -1;
                calm_affect.modifier = 0;
                calm_affect.location = APPLY_NONE;
                calm_affect.bitvector = 0;
                affect_to_char(mob, &calm_affect);
            }

            if (GET_SPEC(ch) == PLRSPEC_PETS) {
                mob->abilities.str += 2;
                mob->tmpabilities.str += 2;
                mob->constabilities.str += 2;
                mob->points.ENE_regen += 40;
                mob->points.damage += 2;
            }

        } break;

        case FOL_GUARDIAN: // and guardian
        {
            // void, matching mystic.cpp's definition. The old `extern int`
            // return type linked anyway on GCC/Clang (Itanium mangling omits
            // the return type), but MSVC encodes it -- LNK2019 there
            // (Phase 3 Task 6). The call ignores the result either way.
            extern void scale_guardian(int, const char_data*, char_data*, bool);
            SET_BIT(mob->specials.affected_by, AFF_CHARM);
            SET_BIT(MOB_FLAGS(mob), MOB_PET);
            int guardian_type = get_guardian_type(ch->player.race, mob);
            scale_guardian(guardian_type, ch, mob, true);
            mob->damage_details.reset();
        } break;

        case FOL_MOUNT: {
            // If the prototype mount is flagged as aggressive but the mob is a mount,
            // assume that the mob has been calmed and add that spell effect to it.
            if (MOB_FLAGGED(mob, MOB_AGGRESSIVE)) {
                affected_type calm_affect;
                calm_affect.type = SKILL_CALM;
                calm_affect.duration = -1;
                calm_affect.modifier = 0;
                calm_affect.location = APPLY_NONE;
                calm_affect.bitvector = 0;
                affect_to_char(mob, &calm_affect);
            }
        } break;
        }
        REMOVE_BIT(MOB_FLAGS(mob), MOB_SPEC);
        mob->specials.store_prog_number = 0;
        REMOVE_BIT(MOB_FLAGS(mob), MOB_AGGRESSIVE);
        REMOVE_BIT(MOB_FLAGS(mob), MOB_STAY_ZONE);
        mob->specials2.pref = 0;
        add_follower(mob, ch, FOLLOW_MOVE);
        if ((tmp = real_mobile(fol_elem.mount_vnum)) > 0) {
            mount = read_mobile(tmp, REAL);
            char_to_room(mount, ch->in_room);
            add_follower(mount, mob, FOLLOW_MOVE);
        }
    }
}

void extract_followers(struct char_data* ch)
{
    struct obj_data* obj;
    struct follow_type *k, *next_fol;
    int x;

    for (k = ch->followers; k; k = next_fol) {
        next_fol = k->next;
        if (!IS_NPC(k->follower))
            continue;
        for (x = 0; x < MAX_WEAR; x++)
            if (k->follower->equipment[x])
                extract_obj(unequip_char(k->follower, x));
        for (x = 0; (x < 1000) && k->follower->carrying; x++) {
            obj = k->follower->carrying;
            obj_from_char(k->follower->carrying);
            extract_obj(obj);
        }
        extract_followers(k->follower);
        extract_char(k->follower);
    }
}

// Applies decoded board points and aliases to the character. Mirrors the
// old Crash_alias_load(ch, fp) body -- including its MAX_ALIAS overflow quirk
// (once `count` exceeds MAX_ALIAS it never grows again, since the increment
// is skipped for every dropped entry, so all aliases past the limit are
// read-and-discarded rather than stored) -- just iterating `data.aliases`
// instead of reading a FILE* stream.
void Crash_alias_load(struct char_data* ch, const objects_json::ObjectSaveData& data)
{
    struct alias_list *list = NULL, *list2;
    int count = 0;

    GET_ALIAS(ch) = 0;

    for (size_t index = 0; index < MAX_MAXBOARD; ++index)
        ch->specials.board_point[index] = (index < data.board_points.size()) ? data.board_points[index] : 0;

    for (const objects_json::AliasData& alias_data : data.aliases) {
        CREATE1(list2, alias_list);
        std::memset(list2->keyword, 0, sizeof(list2->keyword));
        std::memcpy(list2->keyword, alias_data.keyword.data(), std::min(alias_data.keyword.size(), sizeof(list2->keyword) - 1));

        const size_t command_length = alias_data.command.size();
        CREATE(list2->command, char, command_length + 1);
        std::memcpy(list2->command, alias_data.command.data(), command_length);
        list2->command[command_length] = '\0';

        if (count > MAX_ALIAS) { // We should have a create_alias function
            RELEASE(list2->command);
            RELEASE(list2); // to take care of this stuff
            continue;
        }
        if (GET_ALIAS(ch) == 0)
            list = GET_ALIAS(ch) = list2;
        else {
            list->next = list2;
            list = list->next;
        }
        count++;
    }
}

// Builds the board-points/alias section of the save. Mirrors the old
// Crash_alias_save's game-observable behavior: every in-memory alias
// (do_alias never leaves one with an empty command -- removing an alias
// unlinks it from the list instead) is carried over verbatim.
void Crash_collect_alias_data(struct char_data* ch, objects_json::ObjectSaveData* data)
{
    for (int index = 0; index < MAX_MAXBOARD; ++index)
        data->board_points[index] = ch->specials.board_point[index];

    for (struct alias_list* list = ch->specials.alias; list; list = list->next) {
        objects_json::AliasData alias;
        alias.keyword.assign(list->keyword, strnlen(list->keyword, sizeof(list->keyword)));
        alias.command = list->command ? list->command : "";
        data->aliases.push_back(std::move(alias));
    }
}

void Crash_restore_weight(struct obj_data* obj)
{
    if (obj) {
        Crash_restore_weight(obj->contains);
        Crash_restore_weight(obj->next_content);
        if (obj->in_obj)
            GET_OBJ_WEIGHT(obj->in_obj) += GET_OBJ_WEIGHT(obj);
    }
}

void Crash_extract_objs(struct obj_data* obj)
{
    if (obj) {
        Crash_extract_objs(obj->contains);
        Crash_extract_objs(obj->next_content);
        extract_obj(obj);
    }
}

int Crash_is_unrentable(struct obj_data* obj)
{
    if (!obj)
        return 0;

    if (IS_SET(obj->obj_flags.extra_flags, ITEM_NORENT) || (obj->obj_flags.cost_per_day < -1) || (obj->item_number <= -1) || (GET_ITEM_TYPE(obj) == ITEM_KEY))
        return 1;

    return 0;
}

void Crash_extract_norents(struct obj_data* obj)
{
    if (obj) {
        Crash_extract_norents(obj->contains);
        Crash_extract_norents(obj->next_content);
        if (Crash_is_unrentable(obj))
            extract_obj(obj);
    }
}

void Crash_extract_expensive(struct obj_data* obj)
{
    struct obj_data *tobj, *max;

    max = obj;
    for (tobj = obj; tobj; tobj = tobj->next_content)
        if (tobj->obj_flags.cost_per_day > max->obj_flags.cost_per_day)
            max = tobj;
    extract_obj(max);
}

void Crash_calculate_rent(struct obj_data* obj, int* cost)
{
    if (obj) {
        *cost += MAX(0, cost_per_day(obj) >> 4);
        Crash_calculate_rent(obj->contains, cost);
        Crash_calculate_rent(obj->next_content, cost);
    }
}

void Crash_crashsave(struct char_data* ch, int rent_code)
{
    int j;

    if (IS_NPC(ch))
        return;

    objects_json::ObjectSaveData data;
    data.rent.rentcode = rent_code;
    data.rent.time = time(0);

    Crash_collect_objects(ch->carrying, MAX_WEAR, &data.objects);
    Crash_restore_weight(ch->carrying);

    for (j = 0; j < MAX_WEAR; j++)
        if (ch->equipment[j]) {
            Crash_collect_objects(ch->equipment[j], j, &data.objects);
            Crash_restore_weight(ch->equipment[j]);
        }

    Crash_collect_alias_data(ch, &data);
    Crash_collect_followers(ch, &data.followers);

    std::string error_message;
    if (!write_player_objects_json(GET_NAME(ch), data, &error_message)) {
        log(std::format("SYSERR: crashsave: failed to write player objects for {}: {}", GET_NAME(ch), error_message));
        return;
    }

    REMOVE_BIT(PLR_FLAGS(ch), PLR_CRASH);
}

void Crash_idlesave(struct char_data* ch)
{
    int j;

    if (IS_NPC(ch))
        return;

    Crash_extract_norents(ch->carrying);
    for (j = 0; j < MAX_WEAR; j++)
        if (ch->equipment[j])
            Crash_extract_norents(ch->equipment[j]);

    objects_json::ObjectSaveData data;
    data.rent.net_cost_per_hour = 0;
    data.rent.rentcode = RENT_TIMEDOUT;
    data.rent.time = time(0);
    data.rent.gold = GET_GOLD(ch);

    Crash_collect_objects(ch->carrying, MAX_WEAR, &data.objects);
    for (j = 0; j < MAX_WEAR; j++)
        if (ch->equipment[j])
            Crash_collect_objects(ch->equipment[j], j, &data.objects);

    Crash_collect_alias_data(ch, &data);

    std::string error_message;
    if (!write_player_objects_json(GET_NAME(ch), data, &error_message)) {
        log(std::format("SYSERR: idlesave: failed to write player objects for {}: {}", GET_NAME(ch), error_message));
        return;
    }

    Crash_extract_objs(ch->carrying);
}

void Crash_rentsave(struct char_data* ch, int cost)
{
    struct obj_data* tmpobj;
    int j;

    if (IS_NPC(ch))
        return;

    Crash_extract_norents(ch->carrying);

    for (j = 0; j < MAX_WEAR; j++)
        if (ch->equipment[j])
            Crash_extract_norents(ch->equipment[j]);

    objects_json::ObjectSaveData data;
    data.rent.net_cost_per_hour = cost;
    data.rent.rentcode = RENT_RENTED;
    data.rent.time = time(0);
    data.rent.gold = GET_GOLD(ch);

    Crash_collect_objects(ch->carrying, MAX_WEAR, &data.objects);
    for (j = 0; j < MAX_WEAR; j++)
        if (ch->equipment[j]) {
            tmpobj = unequip_char(ch, j);
            Crash_collect_objects(tmpobj, j, &data.objects);
            Crash_extract_objs(tmpobj);
            ch->equipment[j] = 0;
        }

    Crash_collect_alias_data(ch, &data);
    Crash_collect_followers(ch, &data.followers);
    extract_followers(ch);

    std::string error_message;
    if (!write_player_objects_json(GET_NAME(ch), data, &error_message)) {
        log(std::format("SYSERR: rentsave: failed to write player objects for {}: {}", GET_NAME(ch), error_message));
        return;
    }

    Crash_extract_objs(ch->carrying);
}

/* ************************************************************************
 * Routines used for the "Offer"                                           *
 ************************************************************************* */

int Crash_report_unrentables(struct char_data* ch, struct char_data* recep,
    struct obj_data* obj)
{
    char buf[128];
    int has_norents = 0;

    if (obj) {
        if (Crash_is_unrentable(obj)) {
            has_norents = 1;
            strcpy(buf, std::format("$n tells you, 'You cannot store {}.'", OBJS(obj, ch)).c_str());
            act(buf, FALSE, recep, 0, ch, TO_VICT);
        }
        has_norents += Crash_report_unrentables(ch, recep, obj->contains);
        has_norents += Crash_report_unrentables(ch, recep, obj->next_content);
    }

    return has_norents;
}

void Crash_report_rent(struct char_data* ch, struct char_data* recep,
    struct obj_data* obj, long* cost, long* nitems, int factor)
{
    if (obj) {
        if (!Crash_is_unrentable(obj)) {
            (*nitems)++;
            *cost += MAX(0, (cost_per_day(obj) >> 1) * factor);
        }
        Crash_report_rent(ch, recep, obj->contains, cost, nitems, factor);
        Crash_report_rent(ch, recep, obj->next_content, cost, nitems, factor);
    }
}

int Crash_offer_rent(struct char_data* ch, struct char_data* receptionist,
    int factor, char mode)
/* mode == FALSE means we supress output of a successful rent */

{
    char buf[MAX_INPUT_LENGTH];
    int i;
    long totalcost = 0, numitems = 0, norent = 0, timeval;

    norent = Crash_report_unrentables(ch, receptionist, ch->carrying);
    for (i = 0; i < MAX_WEAR; i++)
        norent += Crash_report_unrentables(ch, receptionist, ch->equipment[i]);

    /* they've got norent objects */
    if (norent)
        return -1;

    i = 0;

    totalcost = ch->player.level * factor;

    Crash_report_rent(ch, receptionist, ch->carrying, &totalcost,
        &numitems, factor);

    for (i = 0; i < MAX_WEAR; i++)
        Crash_report_rent(ch, receptionist, ch->equipment[i], &totalcost,
            &numitems, factor);

    /* nothing worth renting with */
    if (!numitems) {
        act("$n tells you, 'But you are not carrying anything!  Just quit!'",
            FALSE, receptionist, 0, ch, TO_VICT);
        return -1;
    }

    /* RENT FORMULA */
    if (mode) {
        strcpy(buf, std::format("$n tells you, 'It will cost you {}{}.'",
            money_message(totalcost * RENT_HALFTIME * 24 / (RENT_HALFTIME + 24)),
            (factor == RENT_FACTOR ? " for the first day" : "")).c_str());
        act(buf, FALSE, receptionist, 0, ch, TO_VICT);
    }
    totalcost = 0;
    if (GET_GOLD(ch) < RENT_HALFTIME * totalcost)
        timeval = RENT_HALFTIME * GET_GOLD(ch) / totalcost / (RENT_HALFTIME - GET_GOLD(ch) / totalcost);
    else
        timeval = 99999;

    if (!timeval) {
        act("$n tells you, 'You haven't enough money to rent.'",
            FALSE, receptionist, 0, ch, TO_VICT);
        return -1;
    }

    if (mode) {
        do {
            if (timeval >= 99999) {
                strcpy(buf, "$n tells you, 'You have enough gold "
                             "for a lifetime of rent.'");
                break;
            }
            if (timeval < 24) {
                strcpy(buf, std::format("$n tells you, 'You have enough gold "
                             "for {} hour{} of rent.'",
                    timeval,
                    (timeval == 1) ? "" : "s").c_str());
                break;
            }

            timeval /= 24;

            if (timeval < 31) {
                strcpy(buf, std::format("$n tells you, 'You have enough gold "
                             "for {} day{} of rent.'",
                    timeval,
                    (timeval == 1) ? "" : "s").c_str());
                break;
            }

            timeval /= 12;

            if (timeval < 12) {
                strcpy(buf, std::format("$n tells you, 'You have enough gold "
                             "for at least {} month{} of rent.'",
                    timeval, (timeval == 1) ? "" : "s").c_str());
                break;
            }
            strcpy(buf, std::format("$n tells you, 'You have enough gold "
                         "for at least {} year{} of rent.'",
                timeval, (timeval == 1) ? "" : "s").c_str());
        } while (0);

        act(buf, FALSE, receptionist, 0, ch, TO_VICT);
    }

    totalcost = 0;
    return totalcost;
}

int gen_receptionist(struct char_data* ch, int cmd, char* arg, int mode)
{
    int cost = 0;
    static char newname[MAX_NAME_LENGTH + 1];
    char tmpname[MAX_NAME_LENGTH + 1];
    static long retirer = 0, namechanger = 0;
    struct char_data* recep = 0;
    struct char_data* tch;
    int save_room;
    const std::string_view action_tabel[9] = { "smile ", "twiddle ", "think ", "frown ", "glare ", "pout ", "sneeze ", "stare ", "yawn " };
    long rent_deadline;

    extern int valid_name(char*);
    extern int rename_char(struct char_data*, char*);
    extern int _parse_name(char*, char*);
    extern int number(int, int);
    extern int r_retirement_home_room;

    if ((!ch->desc) || IS_NPC(ch))
        return (FALSE);

    for (tch = world[ch->in_room].people; (tch) && (!recep); tch = tch->next_in_room)
        if (IS_MOB(tch) && (mob_index[tch->nr].func == receptionist))
            recep = tch;
    if (!recep) {
        log("SYSERR: Fubar'd receptionist.");
        return FALSE;
    }

    if ((cmd != CMD_RENT) && (cmd != CMD_OFFER)) {
        if (!number(0, 30))
            do_action(recep, mutable_arg(action_tabel[number(0, 8)]), 0, 0, 0);
        return FALSE;
    }
    if (!AWAKE(recep)) {
        send_to_char("She is unable to talk to you...\n\r", ch);
        return TRUE;
    }
    if (!CAN_SEE(recep, ch)) {
        act("$n says, 'I don't deal with people I can't see!'", FALSE, recep, 0, 0, TO_ROOM);
        return TRUE;
    }
    if (IS_AGGR_TO(recep, ch)) {
        act("$n says, 'I won't deal with you, $N.  Get out!'", FALSE, recep, 0, ch, TO_ROOM);
        return TRUE;
    }
    if (IS_RIDING(ch)) {
        send_to_char("Sorry you cannot rent while riding.\r\n", ch);
        return TRUE;
    }
    if (GET_RACE(recep) == RACE_WOOD && GET_RACE(ch) != RACE_WOOD) {
        act("$n tells you, 'Only those with Elven blood may rent here.  Please go elsewhere.'",
            FALSE, recep, 0, ch, TO_VICT);
        return TRUE;
    }

    if (GET_RACE(recep) == RACE_DWARF && GET_RACE(ch) != RACE_DWARF) {
        act("$n tells you, 'Dwarves only here.  Try elsewhere.'",
            FALSE, recep, 0, ch, TO_VICT);
        return (TRUE);
    }

    if (GET_RACE(recep) == RACE_BEORNING && GET_RACE(ch) != RACE_BEORNING) {
        act("$n tells you, 'Beornings only here. Try elsewhere.'", FALSE, recep, 0, ch, TO_VICT);
        return (TRUE);
    }

    if (GET_RACE(recep) == RACE_OLOGHAI && GET_RACE(ch) != RACE_OLOGHAI) {
        act("$n tells you, 'Olog-Hais only here. Try elsewhere.'", FALSE, recep, 0, ch, TO_VICT);
        return (TRUE);
    }

    if (GET_RACE(recep) == RACE_HARADRIM && GET_RACE(ch) != RACE_HARADRIM) {
        act("$n tells you, 'Haradrims only here. Try elsewhere.'", FALSE, recep, 0, ch, TO_VICT);
        return (TRUE);
    }

    if ((ch) && (!RP_RACE_CHECK(recep, ch))) {
        act("$n tells you, 'You may not stay here.  Please try elsewhere.'",
            FALSE, recep, 0, ch, TO_VICT);
        return (TRUE);
    }

    if (affected_by_spell(ch, SPELL_ANGER)) {
        if ((GET_RACE(recep) == 11) || (GET_RACE(recep) == 13))
            act("$n tells you, 'Wait until the blood dries, snaga, or you'll join your kill tonight.'",
                FALSE, recep, 0, ch, TO_VICT);
        else
            act("$n tells you, 'You're too angry.  Come back when you've cooled down.'",
                FALSE, recep, 0, ch, TO_VICT);
        return 1;
    }

    if (cmd == CMD_RENT) {
        if ((cost = Crash_offer_rent(ch, recep, 1, FALSE)) < 0)
            return TRUE;

        /* did they put 'retire' or 'namechange' after 'rent'? */
        while (*arg <= ' ' && *arg)
            ++arg;

        if (!strcmp(arg, "retire")) {
            if (!ch->desc)
                return TRUE;

            if (IS_SET(PLR_FLAGS(ch), PLR_RETIRED)) {
                act("$n tells you, 'But you're already retired.'",
                    FALSE, recep, 0, ch, TO_VICT);
                return TRUE;
            }

            if (ch->specials2.idnum != retirer) {
                act("$n tells you, 'So you'd like to retire, eh?'\r\n"
                    "$n tells you, 'Repeat the request to make it final.'",
                    FALSE, recep, 0, ch, TO_VICT);
                retirer = ch->specials2.idnum;
                return TRUE;
            } else {
                retire(ch);
                char_from_room(ch);
                char_to_room(ch, r_retirement_home_room);
                retirer = 0;
            }
        } else if (is_abbrev("namechange", arg)) {
            act("$n tells you, 'Sorry, we're having some internal technical "
                "difficulties.  Please try again later.'",
                FALSE, recep, 0, ch, TO_VICT);
            *newname = 0;
            *tmpname = 0;
            namechanger = 0;
            return TRUE;
            if (!ch->desc)
                return TRUE;

            if (IS_SET(PLR_FLAGS(ch), PLR_IS_NCHANGED)) {
                act("$n tells you, 'Sorry, you can only change your name once in a "
                    "lifetime.'",
                    FALSE, recep, 0, ch, TO_VICT);
                return TRUE;
            }

            /* eat off 'namechange' and any space/garbarge */
            while (*arg && isalpha(*arg))
                ++arg;
            while (*arg && *arg <= ' ')
                ++arg;

            if (!*arg) {
                act("$n tells you, 'You have to tell me what to change your name to.'",
                    FALSE, recep, 0, ch, TO_VICT);
                return TRUE;
            }

            if (find_player_in_table(arg, -1) != -1) {
                act("$n tells you, 'Sorry, that name's already in use.'",
                    FALSE, recep, 0, ch, TO_VICT);
                return TRUE;
            }

            if (_parse_name(arg, tmpname) || fill_word(tmpname) || !valid_name(tmpname)) {
                strcpy(buf, std::format("$n tells you, 'Sorry, '{}' is an invalid name.'", arg).c_str());
                act(buf, FALSE, recep, 0, ch, TO_VICT);
                *newname = 0;
                *tmpname = 0;
                namechanger = 0;
                return TRUE;
            }
            *tmpname = toupper(*tmpname);

            if (ch->specials2.idnum != namechanger) {
                strncpy(newname, tmpname, MAX_NAME_LENGTH);
                strcpy(buf, std::format("$n tells you, 'You'd like to change your name to '{}', "
                             "is that correct?\r\n"
                             "$n tells you, 'Repeat the request to make it final.'",
                    static_cast<const char*>(newname)).c_str());
                act(buf, FALSE, recep, 0, ch, TO_VICT);
                namechanger = ch->specials2.idnum;
                return TRUE;
            }
            /* they didn't type the same name the second time */
            else if (ch->specials2.idnum == namechanger && *newname && strcmp(newname, tmpname)) {
                strncpy(newname, tmpname, MAX_NAME_LENGTH);
                strcpy(buf, std::format("$n tells you, 'So you'd rather be named '{}'?'\r\n"
                             "$n tells you, 'Repeat the request to make it final.'",
                    static_cast<const char*>(newname)).c_str());
                act(buf, FALSE, recep, 0, ch, TO_VICT);
                return TRUE;
            }
            /* they typed 'rent namechange x' twice in a row */
            else {
                if (rename_char(ch, newname) < 0) {
                    act("$n tells you, 'Sorry, we're having some internal technical "
                        "difficulties.  Please try again later.'",
                        FALSE, recep, 0, ch, TO_VICT);
                    *newname = 0;
                    *tmpname = 0;
                    namechanger = 0;
                    return TRUE;
                }
                act("$n tells you, 'Your secret's safe with me.'",
                    FALSE, recep, 0, ch, TO_VICT);
                SET_BIT(PLR_FLAGS(ch), PLR_IS_NCHANGED);
                *newname = 0;
                *tmpname = 0;
                namechanger = 0;
            }
        }

        /* we don't charge for rent; yet */
        cost = 0;

        /* the normal rent process */
        if (cost) {
            rent_deadline = (GET_GOLD(ch) / (cost));
            if (rent_deadline < RENT_HALFTIME)
                rent_deadline = RENT_HALFTIME * rent_deadline / (RENT_HALFTIME - rent_deadline);
            else {
                rent_deadline = 99999;
                strcpy(buf, "You have enough money for a lifetime of rent.\n\r");
            }

            act(buf, FALSE, recep, 0, ch, TO_VICT);
        }

        if (mode == RENT_FACTOR) {
            affected_type* aff = affected_by_spell(ch, SPELL_FAME_WAR);
            act("$n stores your belongings and helps you into your private chamber.",
                FALSE, recep, 0, ch, TO_VICT);
            if (aff) {
                remove_fame_war_bonuses(ch, aff);
                affect_remove(ch, aff);
            }
            Crash_rentsave(ch, cost);
            strcpy(buf, std::format("{} has rented ({}/day, {} tot.)", GET_NAME(ch),
                cost, GET_GOLD(ch)).c_str());
        }

        mudlog(buf, NRM, (sh_int)MAX(LEVEL_IMMORT, GET_INVIS_LEV(ch)), TRUE);
        act("$n helps $N into $S private chamber.", FALSE, recep, 0, ch, TO_NOTVICT);
        save_room = ch->in_room;
        extract_char(ch);
        ch->in_room = world[save_room].number;
        save_char(ch, ch->in_room, 0);
    } else { /* Offer */
        Crash_offer_rent(ch, recep, mode, TRUE);
        act("$N gives $n an offer.", FALSE, ch, 0, recep, TO_ROOM);
    }

    return TRUE;
}

SPECIAL(receptionist)
{
    if (callflag != SPECIAL_COMMAND)
        return FALSE;

    return gen_receptionist(ch, cmd, arg, RENT_FACTOR);
}

ACMD(do_rent)
{
    int save_room;

    send_to_char("Field-rent is disabled. You have to go to an inn now.\n\r", ch);
    return;

    if (ch->specials.fighting) {
        send_to_char("No way! You are fighting still!\n\r", ch);
        return;
    }

    if (affected_by_spell(ch, SPELL_ANGER)) {
        send_to_char("You're too angry.\n\r", ch);
        return;
    }

    if (IS_SHADOW(ch)) {
        send_to_char("You are too insubstantial to rent.\n\r", ch);
        return;
    }

    act("You set a camp right on the spot.\n\r"
        "Be aware - this feature will be removed later.",
        FALSE, ch, 0, 0, TO_CHAR);
    act("$n sets a small camp and rents in it.", FALSE, ch, 0, 0, TO_ROOM);

    Crash_rentsave(ch, 0);
    strcpy(buf, std::format("{} has field-rented ({} total gold)", GET_NAME(ch),
        GET_GOLD(ch)).c_str());

    mudlog(buf, NRM, (sh_int)MAX(LEVEL_IMMORT, GET_INVIS_LEV(ch)), TRUE);

    save_room = ch->in_room;
    extract_char(ch);
    ch->in_room = world[save_room].number;
    save_char(ch, ch->in_room, 0);
}

void Crash_save_all(void)
{
    struct descriptor_data* d;
    for (d = descriptor_list; d; d = d->next) {
        if (d->connected != CON_PLYNG)
            continue;
        if (d->character == nullptr) {
            // Defensive: a CON_PLYNG descriptor should always have a character. Skip and log a
            // broken one rather than aborting the whole point-in-time snapshot for everyone else.
            log("Crash_save_all: CON_PLYNG descriptor with no character; skipping its snapshot.");
            continue;
        }
        if (IS_NPC(d->character))
            continue;
        // Point-in-time snapshot: save EVERY connected player each cadence (no PLR_CRASH dirty
        // gate) so PvP/group participants recover to the same moment, silently (notify=0) so a
        // routine snapshot does not spam "Saving X.". Crash_crashsave also clears PLR_CRASH, so
        // the previous explicit REMOVE_BIT here is now redundant. Modeled on Emergency_save below.
        Crash_crashsave(d->character);
        save_char(d->character, NOWHERE, 0);
    }
}

void Emergency_save(void)
{
    struct descriptor_data* d;
    for (d = descriptor_list; d; d = d->next) {
        if ((d->connected == CON_PLYNG) && !IS_NPC(d->character)) {
            Crash_crashsave(d->character);
            save_char(d->character, r_mortal_start_room[GET_RACE(d->character)], 0);
        }
    }
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
