/* pkill_json.cc */

// Carved out of pkill.cpp (persist-split wave, PS Task 2). Holds the pkill_json
// codec namespace: JSON-first load, legacy binary struct-dump decode
// (offsetof-derived, ABI-portable), atomic temp+rename writes, and the
// one-time legacy-to-JSON converter (verify-reparse + '.migrated' rename).
// pkill.cpp keeps the runtime/capture half: pkill_tab, rankings,
// combat_list walkers, and the X-bridge pkill_read_file/pkill_delete_file/
// pkill_update_file (they touch pkill_tab/player_table -- orchestration,
// not codec). Declarations stay in pkill.h; body below is byte-identical to
// pkill.cpp's prior namespace pkill_json block (see pkill.cpp's git history
// for the removed original).

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "json_utils.h"
#include "pkill.h"
#include "platform_compat.h"
#include "text_view.h"

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
    std::string output;
    output.append("{\n");
    std::format_to(std::back_inserter(output), "  \"version\": {},\n", data.version);
    output.append("  \"records\": [\n");
    for (size_t index = 0; index < data.records.size(); ++index) {
        const PKILL& record = data.records[index];
        output.append("    {\n");
        std::format_to(std::back_inserter(output), "      \"kill_time\": {},\n", record.kill_time);
        std::format_to(std::back_inserter(output), "      \"killer\": {},\n", record.killer);
        std::format_to(std::back_inserter(output), "      \"victim\": {},\n", record.victim);
        std::format_to(std::back_inserter(output), "      \"killer_level\": {},\n", static_cast<int>(record.killer_level));
        std::format_to(std::back_inserter(output), "      \"victim_level\": {},\n", static_cast<int>(record.victim_level));
        std::format_to(std::back_inserter(output), "      \"killer_points\": {},\n", record.killer_points);
        std::format_to(std::back_inserter(output), "      \"victim_points\": {}\n", record.victim_points);
        output.append("    }");
        if (index + 1 < data.records.size())
            output.append(",");
        output.append("\n");
    }
    output.append("  ]\n");
    output.append("}\n");
    return output;
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
