/*
 * pkill.c
 *
 * Handle everything to do with player kill records.  This
 * includes tracking current pkills and calculating total
 * fame.
 *
 * NOTE: The pkill expiration functionality relies on the
 * daily reboot scheme to filter out old pkills.  Without
 * the daily reboot, extra code needs to be put into place
 * that periodically checks the pkill table for expired
 * records and removes them.
 *
 * Copyright Joe Luquette, January 2008
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "db.h"
#include "handler.h"
#include "json_utils.h"
#include "text_view.h"
#include "pkill.h"
#include "platform_compat.h"
#include "structs.h"
#include "utils.h"

// ---------------------------------------------------------------------------
// Phase 2a Task 6: pkill persistence as JSON, plus a one-time legacy
// struct-dump file converter. Follows the mail_json/boards_json precedent
// (Tasks 4/5): JSON-first load, legacy binary as a one-time fallback,
// atomic temp+rename writes, legacy file renamed '.migrated' after a
// verified convert. See pkill.h for the PKILL layout this decodes.
// ---------------------------------------------------------------------------
namespace pkill_json {
namespace {

    void set_error(std::string* error_message, std::string_view message)
    {
        if (error_message)
            error_message->assign(rots::text::truncate_at_null(message));
    }

    // Legacy on-disk format: PKILL_FILE (misc/pklist) is a raw concatenation
    // of fwrite(&p, sizeof(PKILL), 1, f) records (pkill_update_file, below).
    // PKILL (pkill.h) holds only int/unsigned-char fields, so its layout --
    // including compiler-inserted padding -- is identical on 32-bit and
    // 64-bit x86 builds. These offsetof-derived offsets therefore describe
    // the real on-disk bytes regardless of which ABI compiles this reader:
    // the Task 1 ABI-portability convention, applied here via offsetof
    // (rather than hand-picked literals) since PKILL is a real, already-
    // declared struct instead of a hand-reconstructed historical format.
    constexpr size_t kKillTimeOffset = offsetof(PKILL, kill_time);
    constexpr size_t kKillerOffset = offsetof(PKILL, killer);
    constexpr size_t kVictimOffset = offsetof(PKILL, victim);
    constexpr size_t kKillerLevelOffset = offsetof(PKILL, killer_level);
    constexpr size_t kVictimLevelOffset = offsetof(PKILL, victim_level);
    constexpr size_t kKillerPointsOffset = offsetof(PKILL, killer_points);
    constexpr size_t kVictimPointsOffset = offsetof(PKILL, victim_points);
    constexpr size_t kRecordSize = sizeof(PKILL);

    bool read_i32_at(const std::string &bytes, size_t record_offset, size_t field_offset, int *value,
                     std::string *error_message, std::string_view label) {
        label = rots::text::truncate_at_null(label);
        const size_t offset = record_offset + field_offset;
        if (offset + 4 > bytes.size()) {
            set_error(error_message,
                      std::string("Truncated pkill file while reading ") + std::string(label) + ".");
            return false;
        }
        const uint32_t raw = static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset]))
            | (static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset + 1])) << 8)
            | (static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset + 2])) << 16)
            | (static_cast<uint32_t>(static_cast<unsigned char>(bytes[offset + 3])) << 24);
        *value = static_cast<int>(raw);
        return true;
    }

    bool read_u8_at(const std::string &bytes, size_t record_offset, size_t field_offset, unsigned char *value,
                    std::string *error_message, std::string_view label) {
        label = rots::text::truncate_at_null(label);
        const size_t offset = record_offset + field_offset;
        if (offset + 1 > bytes.size()) {
            set_error(error_message,
                      std::string("Truncated pkill file while reading ") + std::string(label) + ".");
            return false;
        }
        *value = static_cast<unsigned char>(bytes[offset]);
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

    // Temp-file + rename atomic write, matching mail.cpp/boards.cpp's pattern.
    bool write_file_contents_atomically(std::string_view path, std::string_view contents, std::string* error_message)
    {
        const std::string path_owner(rots::text::truncate_at_null(path));
        contents = rots::text::truncate_at_null(contents);
        const std::string temp_path = path_owner + ".tmp";

        FILE* temp_file = std::fopen(temp_path.c_str(), "wb");
        if (temp_file == nullptr) {
            set_error(error_message, std::string("Unable to open temporary pkill file '") + temp_path + "': " + std::strerror(errno));
            return false;
        }

        const size_t bytes_written = contents.empty() ? 0 : std::fwrite(contents.data(), sizeof(char), contents.size(), temp_file);
        const int flush_result = std::fflush(temp_file);
        const int close_result = std::fclose(temp_file);

        if (bytes_written != contents.size() || flush_result != 0 || close_result != 0) {
            std::remove(temp_path.c_str());
            set_error(error_message, std::string("Failed to write temporary pkill file '") + temp_path + "'.");
            return false;
        }

        if (rots_rename_replace(temp_path, path_owner) != 0) {
            const std::string rename_error = std::strerror(errno);
            std::remove(temp_path.c_str());
            set_error(error_message, "Failed to move temporary pkill file into place: " + rename_error);
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

bool legacy_pkill_file_from_binary(const std::string& bytes, std::vector<PKILL>* records, std::string* error_message)
{
    if (records == nullptr) {
        set_error(error_message, "Pkill records output parameter must not be null.");
        return false;
    }

    if (bytes.size() % kRecordSize != 0) {
        set_error(error_message, "Pkill file corrupt: size is not a multiple of the record size.");
        return false;
    }

    std::vector<PKILL> parsed;
    const size_t num_records = bytes.size() / kRecordSize;
    parsed.reserve(num_records);
    for (size_t index = 0; index < num_records; ++index) {
        const size_t record_offset = index * kRecordSize;
        PKILL record {};
        int kill_time = 0, killer = 0, victim = 0, killer_points = 0, victim_points = 0;
        unsigned char killer_level = 0, victim_level = 0;
        if (!read_i32_at(bytes, record_offset, kKillTimeOffset, &kill_time, error_message, "kill_time"))
            return false;
        if (!read_i32_at(bytes, record_offset, kKillerOffset, &killer, error_message, "killer"))
            return false;
        if (!read_i32_at(bytes, record_offset, kVictimOffset, &victim, error_message, "victim"))
            return false;
        if (!read_u8_at(bytes, record_offset, kKillerLevelOffset, &killer_level, error_message, "killer_level"))
            return false;
        if (!read_u8_at(bytes, record_offset, kVictimLevelOffset, &victim_level, error_message, "victim_level"))
            return false;
        if (!read_i32_at(bytes, record_offset, kKillerPointsOffset, &killer_points, error_message, "killer_points"))
            return false;
        if (!read_i32_at(bytes, record_offset, kVictimPointsOffset, &victim_points, error_message, "victim_points"))
            return false;

        record.kill_time = kill_time;
        record.killer = killer;
        record.victim = victim;
        record.killer_level = killer_level;
        record.victim_level = victim_level;
        record.killer_points = killer_points;
        record.victim_points = victim_points;
        parsed.push_back(record);
    }

    *records = std::move(parsed);
    set_error(error_message, "");
    return true;
}

std::string serialize_pkill_to_json(const PkillStoreData& data)
{
    std::ostringstream output;
    output << "{\n";
    output << "  \"version\": " << data.version << ",\n";
    output << "  \"records\": [\n";
    for (size_t index = 0; index < data.records.size(); ++index) {
        const PKILL& record = data.records[index];
        output << "    {\n";
        output << "      \"kill_time\": " << record.kill_time << ",\n";
        output << "      \"killer\": " << record.killer << ",\n";
        output << "      \"victim\": " << record.victim << ",\n";
        output << "      \"killer_level\": " << static_cast<int>(record.killer_level) << ",\n";
        output << "      \"victim_level\": " << static_cast<int>(record.victim_level) << ",\n";
        output << "      \"killer_points\": " << record.killer_points << ",\n";
        output << "      \"victim_points\": " << record.victim_points << "\n";
        output << "    }";
        if (index + 1 < data.records.size())
            output << ",";
        output << "\n";
    }
    output << "  ]\n";
    output << "}\n";
    return output.str();
}

bool deserialize_pkill_from_json(std::string_view json, PkillStoreData *data, std::string *error_message) {
    if (data == nullptr) {
        set_error(error_message, "Pkill store output parameter must not be null.");
        return false;
    }

    PkillStoreData parsed;
    const bool ok = json_utils::JsonReader(json).parse_root_object(
        [&](std::string_view key, json_utils::JsonReader* reader, std::string* nested_error) {
            if (key == "version")
                return reader->parse_integer(&parsed.version, nested_error);
            if (key == "records") {
                return reader->parse_array(
                    [&](json_utils::JsonReader* record_reader, std::string* record_error) {
                        PKILL record {};
                        int killer_level = 0;
                        int victim_level = 0;
                        const bool record_ok = record_reader->parse_object(
                            [&](std::string_view record_key, json_utils::JsonReader* nested_reader, std::string* nested_record_error) {
                                if (record_key == "kill_time")
                                    return nested_reader->parse_integer(&record.kill_time, nested_record_error);
                                if (record_key == "killer")
                                    return nested_reader->parse_integer(&record.killer, nested_record_error);
                                if (record_key == "victim")
                                    return nested_reader->parse_integer(&record.victim, nested_record_error);
                                if (record_key == "killer_level")
                                    return nested_reader->parse_integer(&killer_level, nested_record_error);
                                if (record_key == "victim_level")
                                    return nested_reader->parse_integer(&victim_level, nested_record_error);
                                if (record_key == "killer_points")
                                    return nested_reader->parse_integer(&record.killer_points, nested_record_error);
                                if (record_key == "victim_points")
                                    return nested_reader->parse_integer(&record.victim_points, nested_record_error);
                                return nested_reader->skip_value(nested_record_error);
                            },
                            record_error);
                        if (!record_ok)
                            return false;
                        if (killer_level < 0 || killer_level > 255 || victim_level < 0 || victim_level > 255) {
                            set_error(record_error, "killer_level/victim_level must be in [0, 255].");
                            return false;
                        }
                        record.killer_level = static_cast<unsigned char>(killer_level);
                        record.victim_level = static_cast<unsigned char>(victim_level);
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

    if (parsed.version != PKILL_SCHEMA_VERSION) {
        set_error(error_message, "Unsupported pkill schema version.");
        return false;
    }

    *data = std::move(parsed);
    set_error(error_message, "");
    return true;
}

bool pkill_record_equal(const PKILL& a, const PKILL& b)
{
    return a.kill_time == b.kill_time
        && a.killer == b.killer
        && a.victim == b.victim
        && a.killer_level == b.killer_level
        && a.victim_level == b.victim_level
        && a.killer_points == b.killer_points
        && a.victim_points == b.victim_points;
}

bool pkill_records_equal(const std::vector<PKILL>& a, const std::vector<PKILL>& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t index = 0; index < a.size(); ++index)
        if (!pkill_record_equal(a[index], b[index]))
            return false;
    return true;
}

std::string pkill_json_path(std::string_view legacy_path)
{
    return std::string(rots::text::truncate_at_null(legacy_path)) + ".json";
}

bool load_pkill_json_store(std::string_view json_path, std::vector<PKILL>* records, std::string* error_message)
{
    std::string json_text;
    if (!read_whole_file_contents(json_path, &json_text))
        return false;

    PkillStoreData data;
    if (!deserialize_pkill_from_json(json_text, &data, error_message))
        return false;

    *records = std::move(data.records);
    return true;
}

bool write_pkill_json_store(std::string_view json_path, const std::vector<PKILL>& records, std::string* error_message)
{
    PkillStoreData data;
    data.records = records;
    return write_file_contents_atomically(json_path, serialize_pkill_to_json(data), error_message);
}

bool convert_legacy_pkill_file(std::string_view legacy_path, std::string* error_message)
{
    legacy_path = rots::text::truncate_at_null(legacy_path);
    if (legacy_path.empty()) {
        set_error(error_message, "Legacy pkill path must not be empty.");
        return false;
    }

    std::string legacy_bytes;
    if (!read_whole_file_contents(legacy_path, &legacy_bytes)) {
        set_error(error_message, std::string("Failed to read legacy pkill file '") + std::string(legacy_path) + "': " + std::strerror(errno));
        return false;
    }

    std::vector<PKILL> decoded;
    std::string decode_error;
    if (!legacy_pkill_file_from_binary(legacy_bytes, &decoded, &decode_error)) {
        set_error(error_message, "Decode failed: " + decode_error);
        return false;
    }

    PkillStoreData store;
    store.records = decoded;
    const std::string json = serialize_pkill_to_json(store);

    // Verify (binding conversion contract): re-decode the freshly serialized
    // JSON and compare it field-for-field to the original decode.
    PkillStoreData reparsed;
    std::string verify_error;
    if (!deserialize_pkill_from_json(json, &reparsed, &verify_error)) {
        set_error(error_message, "Verify-decode of freshly serialized JSON failed: " + verify_error);
        return false;
    }

    if (!pkill_records_equal(decoded, reparsed.records)) {
        set_error(error_message, "Verify mismatch: re-decoded JSON does not equal the original legacy decode.");
        return false;
    }

    const std::string json_path = pkill_json_path(legacy_path);
    std::string write_error;
    if (!write_file_contents_atomically(json_path, json, &write_error)) {
        set_error(error_message, write_error);
        return false;
    }

    const std::string migrated_path = std::string(legacy_path) + ".migrated";
    if (rots_rename_replace(legacy_path, migrated_path) != 0) {
        // JSON is written and verified; the legacy file simply couldn't be
        // retired (matches mail_json/boards_json's "partial success"
        // contract -- report but don't fail, nothing is at risk).
        set_error(error_message,
            std::string("Pkill file converted but legacy rename to '") + migrated_path + "' failed: " + std::strerror(errno));
        return true;
    }

    set_error(error_message, "");
    return true;
}

} // namespace pkill_json

/* Local variables.  Do not ever use these externally.
 * If you think you need access to these variables externally,
 * then you may need to write a new entrypoint to the PKILL API.
 * However, do not, under any circumstances, reference these
 * variables externally.
 */
PKILL* pkill_tab = NULL;
int pkill_tab_len = 0;

typedef struct {
    long* rank_tab;
    int rank_len;
    int rank_used;
    int side_fame;
} RANKING;

RANKING good_ranking = { NULL, 0, 0, 0 };
RANKING evil_ranking = { NULL, 0, 0, 0 };
RANKING total_ranking = { NULL, 0, 0, 0 };

/*
 * Return > 0 if 'race' is a good race; otherwise return -1.
 * This should not have to be implemented here--there should
 * be a globally useful function that can take race as an
 * integer and return the side without requiring a full char
 * data structure.
 */
int __pkill_side(int race)
{
    struct char_data c;

    /* Use a dummy structure so we can use the RACE_GOOD macro */
    GET_RACE(&c) = race;
    if (RACE_GOOD(&c))
        return 1;
    else
        return 0;
}

/*
 * Copy the PKILL structure at 'src' into 'dest'.
 */
void pkill_copy(PKILL* dest, PKILL* src)
{
    dest->kill_time = src->kill_time;
    dest->killer = src->killer;
    dest->victim = src->victim;
    dest->killer_level = src->killer_level;
    dest->victim_level = src->victim_level;
    dest->killer_points = src->killer_points;
    dest->victim_points = src->victim_points;
}

/*
 * Return 1 if the PKILL record p has expired; otherwise return 0.
 */
int pkill_expired(PKILL* p)
{
    int days_passed, days_allowed;

    days_passed = (time(0) - p->kill_time) / (3600 * 24);

    if (p->killer_level == 0)
        days_allowed = 1;
    else
        days_allowed = 30 * p->victim_level / p->killer_level;

    vmudlog(CMP, "Check expiration: killer %d, victim %d, days elapsed %d.",
        p->killer, p->victim, days_passed);

    if (days_passed >= days_allowed)
        return 1;

    return 0;
}

/*
 * Determine the weight of this kill.  If the victim has many
 * high level opponents contributing to his death, then the
 * weight will be small; if the victim has relatively few
 * opponents and levels (relative to his own level), then the
 * weight will be large.
 */
int pkill_weight(struct char_data* victim)
{
    int total_levels;
    struct char_data* c;
    extern struct char_data* combat_list;

    total_levels = 0;
    for (c = combat_list; c != NULL; c = c->next_fighting)
        if (c->specials.fighting == victim)
            total_levels += GET_LEVEL(c);

    /* Get the larger of the two */
    total_levels = MAX(victim->specials.attacked_level, total_levels);

    /* Don't divide by zero */
    if (total_levels == 0)
        return 0;

    return GET_LEVEL(victim) * 1000 / (total_levels * total_levels);
}

/*
 * Return 0 if 'c' is not a valid player killer.  Otherwise,
 * return 1.
 */
int pkill_valid_killer(struct char_data* killer, struct char_data* victim)
{
    /* The only valid NPCs are orc followers */
    if (IS_NPC(killer)) {
        if (!MOB_FLAGGED(killer, MOB_ORC_FRIEND) || !MOB_FLAGGED(killer, MOB_PET))
            return 0;

        /*
         * If we're here, we're dealing with an orc follower.  In
         * that case, we want to create a record ONLY when the
         * leader isn't engaged.  So if the leader is engaged, we'll
         * return invalid here.
         */
        if (killer->master != NULL && killer->master->specials.fighting == victim)
            return 0;
    }

    /* Immortals are not valid pkillers */
    if (GET_LEVEL(killer) >= LEVEL_IMMORT)
        return 0;

    return 1;
}

int pkill_opponents(struct char_data* victim)
{
    int total_opponents;
    struct char_data* c;
    extern struct char_data* combat_list;

    total_opponents = 0;
    for (c = combat_list; c != NULL; c = c->next_fighting)
        if (c->specials.fighting == victim && pkill_valid_killer(c, victim))
            ++total_opponents;

    return total_opponents;
}

/*
 * Same side kills are awarded zero points.
 */
int pkill_points(struct char_data* victim, struct char_data* killer, int weight)
{
    if (other_side(victim, killer))
        return GET_LEVEL(killer) * weight;
    else
        return 0;
}

int pkill_level(struct char_data* c)
{
    if (PLR_FLAGGED(c, PLR_INCOGNITO))
        return 50;
    else
        return GET_LEVEL(c);
}

/*
 * Shift the interval [a, b] in rank_tab to [a+shift, b+shift].
 * Note that the caller of this function has to take care of
 * refilling all of the values obliterated by this shift!
 */
void __shift_rank(RANKING* rnk, int a, int b, int shift, bool total)
{
    int i, n;
    long x; /* Dummy index to player table */
    extern struct player_index_element* player_table;

    /* Interval must be [a, b].  a == b is acceptable */
    if (a > b)
        return;

    /* Cannot shift past the end of the array */
    if (a + shift >= rnk->rank_len)
        return;

    n = b - a + 1;
    n = MIN(n, rnk->rank_len - (a + shift));

    /* Don't shift by 0 cells */
    if (n == 0)
        return;

    /* Update the rank array */
    memmove(&rnk->rank_tab[a + shift], &rnk->rank_tab[a], n * sizeof(long));

    /* For each player affected by the shift, modify their rank by shift */
    for (i = 0; i < n; ++i) {
        x = rnk->rank_tab[a + shift + i]; /* Player table offset */
        if (x == PKILL_UNRANKED)
            break;

        if (!total)
            player_table[x].rank = a + shift + i; /* Old rank + shift */
        else
            player_table[x].totalrank = a + shift + i;
    }
}

/*
 * Delete rank 'r' by shifting the interval [r + 1, ranklen - 1]
 * into the position [r, ranklen - 2] and filling in the reference
 * at rank_tab[rank_len - 1] with an invalid marker (-1).
 */
void __delete_rank(RANKING* rnk, int r, bool totalrank)
{
    if (r != PKILL_UNRANKED) {
        __shift_rank(rnk, r + 1, rnk->rank_len - 1, -1, totalrank);
        rnk->rank_tab[rnk->rank_len - 1] = -1;
        --rnk->rank_used;
    }
}

/*
 * Insert a reference to character 'idx' at rank 'a'.
 */
void __insert_rank(RANKING* rnk, int a, long idx, bool totalrank)
{
    if (rnk->rank_tab == NULL || rnk->rank_len == 0) {
        CREATE(rnk->rank_tab, long, 1);
        ++rnk->rank_len;
    }

    if (rnk->rank_used == rnk->rank_len) {
        RECREATE(rnk->rank_tab, long, rnk->rank_len + 1, rnk->rank_len);
        ++rnk->rank_len;
    }

    __shift_rank(rnk, a, rnk->rank_len - 1, 1, totalrank);
    rnk->rank_tab[a] = idx;
    ++rnk->rank_used;
}

/*
 * Update the rank arrays for each of the RANKING structures.
 * The only argument is the index of the character in the
 * player table.  The idea of the algorithm is:
 *   (1) Identify the character: make sure the index to the
 *       player table is valid and find the player's rank in
 *       the RANKING structures.
 *   (2) Delete the player from the rank structures.
 *   (3) Update the player's warpoint totals.
 *   (4) Re-insert the player into the RANKING structures.
 *       (a) Note that if the player has non-positive fame
 *           after we update his warpoints, then we do not
 *           re-insert him into the RANKINGs.  We only rank
 *           people with fame > 0.
 */
void pkill_update_rank(long idx)
{
    int a;
    int b;
    int t;
    int r;
    int npoints;
    RANKING* rnk;
    RANKING* trnk;
    extern struct player_index_element* player_table;
    extern int top_of_p_table;

    if (idx == -1 || idx > top_of_p_table)
        return;

    r = player_table[idx].rank;
    t = player_table[idx].totalrank;
    npoints = player_table[idx].warpoints;

    /* Point at the correct RANKING structure */
    if (__pkill_side(player_table[idx].race) > 0)
        rnk = &good_ranking;
    else
        rnk = &evil_ranking;

    trnk = &total_ranking;

    __delete_rank(rnk, r, false);
    __delete_rank(trnk, t, true);

    /* Don't re-add people with negative fame.  Leave them out */
    if (npoints < 0) {
        player_table[idx].rank = PKILL_UNRANKED;
        player_table[idx].totalrank = PKILL_UNRANKED;
        return;
    }

    for (b = 0; b < trnk->rank_len; ++b) {
        if (trnk->rank_tab[b] == -1)
            break;

        if (npoints > player_table[trnk->rank_tab[b]].warpoints)
            break;
    }

    /* Find where to reinsert the guy we just deleted */
    for (a = 0; a < rnk->rank_len; ++a) {
        if (rnk->rank_tab[a] == -1)
            break;

        if (npoints > player_table[rnk->rank_tab[a]].warpoints)
            break;
    }

    __insert_rank(trnk, b, idx, true);
    __insert_rank(rnk, a, idx, false);

    player_table[idx].rank = a;
    player_table[idx].totalrank = b;
}

long pkill_update_character_by_id(long idnum, int points)
{
    int idx;
    struct char_data c;
    extern struct player_index_element* player_table;

    /* Update the killer's player table entry */
    idx = find_player_in_table("", idnum);
    if (idx >= 0) {
        player_table[idx].warpoints += points;
        if (__pkill_side(player_table[idx].race) > 0)
            good_ranking.side_fame += points;
        else
            evil_ranking.side_fame += points;
    }

    return idx;
}

/*
 * Extend the internal pkill list by n elements.  Note that
 * this function is considered extremely private, and is thus
 * prefixed by a __.
 */
void __pkill_extend_tab(int n)
{
    /* This can happen (think empty pkill file) */
    if (n == 0)
        return;

    if (pkill_tab_len == 0 || pkill_tab == NULL) {
        CREATE(pkill_tab, PKILL, n);
        pkill_tab_len = n;
    } else {
        RECREATE(pkill_tab, PKILL, n + pkill_tab_len, pkill_tab_len);
        pkill_tab_len += n;
    }
}

/*
 * Read in all of the pkills currently written to the pkill
 * file.  Store them in the global variable pkill_tab and
 * return the number of pkills read from the file.
 */
int pkill_read_file(std::string_view file)
{
    const std::string file_owner(rots::text::truncate_at_null(file));
    const std::string json_path = pkill_json::pkill_json_path(file_owner);
    std::vector<PKILL> loaded_records;

    FILE* json_probe = fopen(json_path.c_str(), "rb");
    if (json_probe != NULL) {
        fclose(json_probe);
        std::string error_message;
        if (!pkill_json::load_pkill_json_store(json_path, &loaded_records, &error_message)) {
            vmudlog(BRF, "SYSERR: Pkill JSON file '%s' is malformed: %s", json_path.c_str(), error_message.c_str());
            return 0;
        }
    } else {
        /* No JSON store yet -- either a fresh install (no legacy file
         * either) or a legacy binary file waiting for its one-time
         * conversion. */
        FILE* legacy_probe = fopen(file_owner.c_str(), "rb");
        if (legacy_probe == NULL) {
            vmudlog(BRF, "read_pkill_file: pkill file '%s' does not exist.",
                file_owner.c_str());
            return 0;
        }
        fclose(legacy_probe);

        std::string convert_error;
        if (!pkill_json::convert_legacy_pkill_file(file, &convert_error)) {
            vmudlog(BRF, "SYSERR: Failed converting legacy pkill file '%s' to JSON: %s", file_owner.c_str(), convert_error.c_str());
            return 0;
        }
        if (!convert_error.empty())
            vmudlog(BRF, "Converted legacy pkill file '%s' to JSON (warning: %s).", file_owner.c_str(), convert_error.c_str());
        else
            vmudlog(BRF, "Converted legacy pkill file '%s' to JSON.", file_owner.c_str());

        std::string load_error;
        if (!pkill_json::load_pkill_json_store(json_path, &loaded_records, &load_error)) {
            vmudlog(BRF, "SYSERR: Pkill JSON file missing or malformed immediately after conversion: %s", load_error.c_str());
            return 0;
        }
    }

    /* Create enough pkill structs for all of the pkills and copy them in. */
    const int start = pkill_tab_len;
    __pkill_extend_tab(static_cast<int>(loaded_records.size()));
    for (size_t i = 0; i < loaded_records.size(); ++i)
        pkill_tab[start + i] = loaded_records[i];

    return static_cast<int>(loaded_records.size());
}

void pkill_delete_file(std::string_view file)
{
    const std::string file_owner(rots::text::truncate_at_null(file));
    const std::string json_path = pkill_json::pkill_json_path(file_owner);
    std::string error_message;
    if (!pkill_json::write_pkill_json_store(json_path, std::vector<PKILL>(), &error_message))
        vmudlog(BRF, "Could not delete pkill file '%s': %s", file_owner.c_str(), error_message.c_str());
}

/*
 * Write out all pkills which haven't expired to the given
 * pkill file.
 */
int pkill_update_file(std::string_view file, PKILL pkills[], int n)
{
    const std::string file_owner(rots::text::truncate_at_null(file));
    int i, nwritten;
    PKILL p;
    extern struct player_index_element* player_table;

    const std::string json_path = pkill_json::pkill_json_path(file_owner);
    std::vector<PKILL> existing_records;

    /* An absent JSON store is tolerated as empty (matches the legacy
     * fopen(file, "a")'s tolerance for a not-yet-existing file). A
     * present-but-malformed store fails closed: refuse to overwrite it. */
    FILE* json_probe = fopen(json_path.c_str(), "rb");
    if (json_probe != NULL) {
        fclose(json_probe);
        std::string load_error;
        if (!pkill_json::load_pkill_json_store(json_path, &existing_records, &load_error)) {
            vmudlog(BRF, "SYSERR: Pkill JSON file '%s' is malformed, refusing to overwrite: %s", json_path.c_str(), load_error.c_str());
            return 0;
        }
    }

    nwritten = 0;
    for (i = 0; i < n; ++i) {
        if (!pkill_expired(&pkills[i])) {
            /*
             * The pkill file stores killer and victim IDs, not
             * player table indexes (which would be useless to
             * store).  If we ever move from a binary pkill file
             * to a text pkill file, we should store idnums and
             * player table indexes in the PKILL struct so that
             * we don't have to reference the player_table here.
             */
            pkill_copy(&p, &pkills[i]);
            p.killer = player_table[p.killer].idnum;
            p.victim = player_table[p.victim].idnum;
            existing_records.push_back(p);
            ++nwritten;
        }
    }

    std::string write_error;
    if (!pkill_json::write_pkill_json_store(json_path, existing_records, &write_error)) {
        vmudlog(BRF, "Failed to write pkill file %s: %s", file_owner.c_str(), write_error.c_str());
        return 0;
    }

    return nwritten;
}

/*
 * Update the player table with all of the PKILLs recorded
 * in the array pkills.  Note that it isn't necessary to have
 * an entire list of PKILLs to update: we use this function
 * to update the player table ANY TIME a pkill record or set
 * of records is created.
 */
int pkill_update_player_tab(PKILL pkills[], int nkills)
{
    int i;
    PKILL* p;

    for (i = 0; i < nkills; ++i) {
        p = &pkills[i];

        p->killer = pkill_update_character_by_id(p->killer,
            p->killer_points);
        pkill_update_rank(p->killer);

        p->victim = pkill_update_character_by_id(p->victim,
            p->victim_points);
        pkill_update_rank(p->victim);
    }

    return i;
}

/*
 * Update the pkill table assuming that the victim has just died
 * and that the pkill statistics weight w and opponents n pertain
 * to this pkill.
 */
int pkill_update_pkill_tab(struct char_data* victim, int w, int n)
{
    int i, start;
    int points;
    time_t t;
    struct char_data* c;
    extern struct char_data* combat_list;

    /* Record where the list of new PKILL records start */
    start = pkill_tab_len;

    /* Make room for the new PKILLs */
    __pkill_extend_tab(n);

    i = 0;
    t = time(0);
    for (c = combat_list; c != NULL; c = c->next_fighting) {
        if (c->specials.fighting == victim && pkill_valid_killer(c, victim)) {
            vmudlog(CMP, "Creating pkill: %s killed %s.",
                GET_NAME(c), GET_NAME(victim));

            pkill_tab[start + i].kill_time = t;
            pkill_tab[start + i].killer = c->specials2.idnum;
            pkill_tab[start + i].victim = victim->specials2.idnum;
            pkill_tab[start + i].killer_level = pkill_level(c);
            pkill_tab[start + i].victim_level = pkill_level(victim);

            points = pkill_points(victim, c, w);
            pkill_tab[start + i].killer_points = points;
            pkill_tab[start + i].victim_points = -1 * points;
            ++i;
        }
    }

    return start;
}

void pkill_create(struct char_data* victim)
{
    int weight;
    int opponents;
    int start;

    /* We don't record pkills for NPCs or immortals */
    if (IS_NPC(victim) || GET_LEVEL(victim) >= LEVEL_IMMORT)
        return;

    /* Get the kill statistics */
    weight = pkill_weight(victim);
    opponents = pkill_opponents(victim);

    start = pkill_update_pkill_tab(victim, weight, opponents);
    pkill_update_player_tab(&pkill_tab[start], opponents);
    pkill_update_file(PKILL_FILE, &pkill_tab[start], opponents);
}

/*
 * Remove all references to the character 'c' in the pkill
 * table.  Leave the character table untouched.
 *
 * Also note: requires that 'c' be in the player table.
 */
void pkill_unref_character(struct char_data* c)
{
    int idx;

    idx = find_player_in_table("", c->specials2.idnum);
    pkill_unref_character_by_index(idx);
}

/*
 * See the notes for pkill_unref_character above.
 */
void pkill_unref_character_by_index(int idx)
{
    int i;
    int r;
    extern struct player_index_element* player_table;

    /* Set all PKILL records referencing this char to ref -1 */
    for (i = 0; i < pkill_tab_len; ++i) {
        if (pkill_tab[i].killer == idx)
            pkill_tab[i].killer = -1;
        if (pkill_tab[i].victim == idx)
            pkill_tab[i].victim = -1;
    }

    /* Remove from the rank lists */
    r = player_table[idx].rank;
    if (r != PKILL_UNRANKED) {
        if (__pkill_side(player_table[idx].race) > 0)
            __delete_rank(&good_ranking, r, false);
        else
            __delete_rank(&evil_ranking, r, false);
    }

    int t = player_table[idx].totalrank;
    if (t != PKILL_UNRANKED) {
        __delete_rank(&total_ranking, t, true);
    }
}

int pkill_get_total()
{
    return pkill_tab_len;
}

/*
 * This is horrible and should be removed.  It can only be
 * removed, however, when someone updates the char_data API
 * to support this functionality for us.
 */
char* __pkill_name(int i)
{
    char* ret;
    extern struct player_index_element* player_table;

    if (i < 0)
        ret = strdup("someone");
    else
        ret = strdup(player_table[i].name);

    CAP(ret);

    return ret;
}

/*
 * Return a string representation of the PKILL record p.
 * This returns dynamically allocated memory.  The caller
 * MUST free it after use.
 *
 * The flag can either be PKILL_STRING_KILLED or
 * PKILL_STRING_SLAIN.  These define from which viewpoint
 * the pkill string is created.
 */
char* pkill_get_string(PKILL* p, int flag)
{
    char *killer, *victim;
    char* ret;
    char day[128];
    struct time_info_data t;
    extern long beginning_of_time;

    killer = __pkill_name(p->killer);
    victim = __pkill_name(p->victim);

    t = mud_time_passed(p->kill_time, beginning_of_time);
    day_to_str(&t, day);

    if (flag == PKILL_STRING_KILLED)
        rots_asprintf(&ret, "On %s, %s killed %s.\r\n",
            day, killer, victim);
    else if (flag == PKILL_STRING_SLAIN)
        rots_asprintf(&ret, "On %s, %s was slain by %s.\r\n",
            day, victim, killer);
    else
        rots_asprintf(&ret, "ERROR: Unknown pkill string!  Notify an immortal.\r\n");

    free(killer);
    free(victim);

    return ret;
}

/*
 * Return a list of all kills which occurred in the past 24
 * hours of real time.  Returns NULL if no valid pkills are
 * found.
 *
 * NOTE: The pkill table pkill_tab is kept in chronological
 * order, so we return the first entry in the table which is
 * within the 24 hour period and then assume all of the
 * following pkills are valid.
 */
PKILL*
pkill_get_new_kills(int* n)
{
    int i;
    time_t now, time_limit;

    /* Time limit is currently 1 day */
    time_limit = 24 * 3600;
    now = time(0);

    for (i = 0; i < pkill_tab_len; ++i)
        if (now - pkill_tab[i].kill_time < time_limit)
            break;

    if (i != pkill_tab_len) {
        *n = pkill_tab_len - i;
        return &pkill_tab[i];
    } else {
        *n = 0;
        return NULL;
    }
}

PKILL*
pkill_get_all(int* n)
{
    *n = pkill_tab_len;

    return pkill_tab;
}

void boot_pkills()
{
    int n;

    pkill_tab_len = pkill_read_file(PKILL_FILE);
    vmudlog(BRF, "Pkills read: %d", pkill_tab_len);

    n = pkill_update_player_tab(pkill_tab, pkill_tab_len);
    vmudlog(BRF, "Updated player table with %d pkills from file.", n);

    pkill_delete_file(PKILL_FILE);
    vmudlog(BRF, "Pkill file deleted.");

    n = pkill_update_file(PKILL_FILE, pkill_tab, pkill_tab_len);
    vmudlog(BRF, "Pkills file updated with %d records.", n);
}

int pkill_get_good_fame()
{
    return good_ranking.side_fame / 100;
}

int pkill_get_evil_fame()
{
    return evil_ranking.side_fame / 100;
}

LEADER*
__new_leader(std::string_view name, int idx, int rank, int fame, int race, int side, int invalid)
{
    LEADER* ldr;

    CREATE(ldr, LEADER, 1);
    const std::string name_owner(rots::text::truncate_at_null(name));
    ldr->name = strdup(name_owner.c_str());
    CAP(ldr->name);
    ldr->player_idx = idx;
    ldr->rank = rank;
    ldr->fame = fame;
    ldr->race = race;
    ldr->side = side;
    ldr->invalid = invalid;

    return ldr;
}

void pkill_free_leader(LEADER* ldr)
{
    free(ldr->name);
    free(ldr);
}

/*
 * Get the fame leader of the specified rank.  Rank values
 * are integers in [0, ...].  Hence the highest rank is
 * rank 0, the second highest is 1, etc.  A non-NULL LEADER
 * structure is ALWAYS returned, even if no leader with the
 * requested rank is found.  In this case, a dummy leader is
 * returned.
 *
 * Also note that the leader is pulled from the side of the
 * given reference race.  If a good race is used, a good
 * leader will be returned; otherwise an evil leader will be
 * chosen.
 *
 * A NULL rank_tab happens whenever there are no leaders;
 * i.e., when there have been no pkills or there is no
 * fame.
 */
LEADER*
pkill_get_leader_by_rank(int rank, int race)
{
    int idx;
    RANKING* rnk;
    LEADER* ldr;
    extern struct player_index_element* player_table;

    /* Get the correct ranking structure */
    if (__pkill_side(race) > 0)
        rnk = &good_ranking;
    else
        rnk = &evil_ranking;

    /* Check rank for bounds then check table */
    if (rank >= 0 && rank < rnk->rank_len)
        idx = rnk->rank_tab[rank];
    else
        idx = -1;

    /* Dummy leader if no such leader was found */
    if (idx == -1)
        ldr = __new_leader("", idx, PKILL_UNRANKED, 0, 0, 0, -1);
    else {
        ldr = __new_leader(player_table[idx].name,
            idx, rank,
            player_table[idx].warpoints / 100,
            player_table[idx].race,
            __pkill_side(player_table[idx].race), 0);
    }

    return ldr;
}

/*
 * Given a char_data structure, return the character's rank in
 * O(1) time.
 */
int pkill_get_rank_by_character(struct char_data* c, bool totalRank)
{
    return pkill_get_totalrank_by_character_id(GET_INDEX(c), totalRank);
}

int pkill_get_totalrank_by_character_id(int idx, bool totalRank)
{
    extern struct player_index_element* player_table;
    extern int top_of_p_table;

    if (idx < 0 || idx > top_of_p_table)
        return PKILL_UNRANKED;

    if (totalRank)
        return player_table[idx].totalrank;

    return player_table[idx].rank;
}
