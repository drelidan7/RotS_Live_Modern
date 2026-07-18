/* boards_json.cc */

// Carved out of boards.cpp (persist-split wave, PS Task 2). Holds the
// boards_json codec namespace: JSON-first load, legacy binary board-file
// decoder, atomic temp+rename writes, and the one-time legacy-to-JSON
// converter (verify-reparse + '.migrated' rename). boards.cpp keeps the
// display half, the persist bridge (save_board/apply_board_save_data/
// load_board), constructors, and all board globals. Declarations stay in
// boards.h; body below is byte-identical to boards.cpp's prior namespace
// boards_json block (see boards.cpp's git history for the removed
// original) with ONE named deviation: write_file_contents_atomically()
// loses the anonymous-namespace linkage it had in boards.cpp --
// boards.cpp's save_board() (persist bridge, stays behind) still calls it
// cross-TU after this move (a duplicate-hazard the PS Task 2
// classification missed), so it needs external linkage. Declared in
// boards.h next to the rest of the boards_json API. boards.cpp's SECOND,
// separately-named read_whole_file() helper at (pre-carve) line ~1311
// belongs to the bridge, not this codec, and is untouched.

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <iterator>
#include <string>
#include <utility>

#include "boards.h"
#include "json_utils.h"
#include "platform_compat.h"
#include "text_view.h"

namespace boards_json {
namespace {

    void set_error(std::string* error_message, std::string_view message)
    {
        if (error_message)
            error_message->assign(rots::text::truncate_at_null(message));
    }

    // Little-endian 4-byte int read at an explicit offset -- portable
    // regardless of the reading process's own endianness/ABI (this repo's
    // established convention, see objects_json.cpp's read_pod/read_u32le).
    bool read_i32(const std::string &bytes, size_t *offset, int *value,
                  std::string *error_message, std::string_view label) {
        label = rots::text::truncate_at_null(label);
        if (*offset + 4 > bytes.size()) {
            set_error(error_message,
                      std::string("Truncated board file while reading ") + std::string(label) + ".");
            return false;
        }

        const uint32_t raw = static_cast<uint32_t>(static_cast<unsigned char>(bytes[*offset]))
            | (static_cast<uint32_t>(static_cast<unsigned char>(bytes[*offset + 1])) << 8)
            | (static_cast<uint32_t>(static_cast<unsigned char>(bytes[*offset + 2])) << 16)
            | (static_cast<uint32_t>(static_cast<unsigned char>(bytes[*offset + 3])) << 24);
        *value = static_cast<int>(static_cast<int32_t>(raw));
        *offset += 4;
        return true;
    }

    bool skip_bytes(const std::string &bytes, size_t *offset, size_t length, std::string *error_message,
                    std::string_view label) {
        label = rots::text::truncate_at_null(label);
        if (*offset + length > bytes.size()) {
            set_error(error_message,
                      std::string("Truncated board file while reading ") + std::string(label) + ".");
            return false;
        }
        *offset += length;
        return true;
    }

    // Reads `length_including_nul` bytes of text starting at `*offset` and
    // strips the trailing NUL byte the legacy writer always included (it
    // wrote strlen(text)+1 bytes -- see save_board's heading_len/message_len
    // computation below).
    bool read_text(const std::string &bytes, size_t *offset, size_t length_including_nul, std::string *out,
                   std::string *error_message, std::string_view label) {
        label = rots::text::truncate_at_null(label);
        if (*offset + length_including_nul > bytes.size()) {
            set_error(error_message,
                      std::string("Truncated board file while reading ") + std::string(label) + ".");
            return false;
        }
        if (length_including_nul == 0) {
            out->clear();
            return true;
        }
        out->assign(bytes, *offset, length_including_nul - 1);
        *offset += length_including_nul;
        return true;
    }

    // Decodes one 28-byte `board_msginfo` record (slot_num, msg_num, the
    // `char*` heading pointer -- read past and discarded, level, post_time,
    // heading_len, message_len) plus its adjacent heading/message text.
    bool read_legacy_record(const std::string& bytes, size_t* offset, BoardMessageData* message, std::string* error_message)
    {
        int slot_num = 0, msg_num = 0, level = 0, post_time = 0, heading_len = 0, message_len = 0;
        if (!read_i32(bytes, offset, &slot_num, error_message, "message slot_num"))
            return false;
        if (!read_i32(bytes, offset, &msg_num, error_message, "message msg_num"))
            return false;
        if (!skip_bytes(bytes, offset, 4, error_message, "message heading pointer"))
            return false;
        if (!read_i32(bytes, offset, &level, error_message, "message level"))
            return false;
        if (!read_i32(bytes, offset, &post_time, error_message, "message post_time"))
            return false;
        if (!read_i32(bytes, offset, &heading_len, error_message, "message heading_len"))
            return false;
        if (!read_i32(bytes, offset, &message_len, error_message, "message message_len"))
            return false;

        if (heading_len < 1) {
            set_error(error_message, "Board file corrupt: message heading_len must be >= 1.");
            return false;
        }
        if (message_len < 0) {
            set_error(error_message, "Board file corrupt: message message_len must be >= 0.");
            return false;
        }

        std::string heading;
        if (!read_text(bytes, offset, static_cast<size_t>(heading_len), &heading, error_message, "message heading"))
            return false;

        const bool has_message = message_len > 0;
        std::string text;
        if (has_message && !read_text(bytes, offset, static_cast<size_t>(message_len), &text, error_message, "message body"))
            return false;

        message->slot_num = slot_num;
        message->msg_num = msg_num;
        message->level = level;
        message->post_time = post_time;
        message->heading = std::move(heading);
        message->has_message = has_message;
        message->message = has_message ? std::move(text) : std::string();
        return true;
    }

    bool read_binary_file_contents(std::string_view path, std::string* bytes)
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

    // Defensive consistency check for deserialized JSON: `has_message` and
    // `message` must agree (a hand-edited or corrupted JSON file could set
    // them inconsistently, which apply_board_save_data below has no sane way
    // to interpret).
    bool message_len_invariant_holds(const BoardSaveData& data)
    {
        for (const BoardMessageData& message : data.messages) {
            if (!message.has_message && !message.message.empty())
                return false;
        }
        return true;
    }

} // namespace

// Promoted out of boards_json's anonymous namespace (persist-split PS Task 2
// deviation, mirroring color_convert.cpp's sync_color_slot_foreground_from_ansi
// precedent): boards.cpp's persist bridge (save_board(), which stays behind)
// still calls this cross-TU after this carve, so it needs external linkage.
// Declared in boards.h. Body is otherwise byte-identical to its prior
// anonymous-namespace version (dedented one level to match boards_json's
// unindented top-level functions).
// Temp-file + rename atomic write, matching write_player_objects_json's
// pattern in objsave.cpp.
bool write_file_contents_atomically(std::string_view path, std::string_view contents, std::string* error_message)
{
    const std::string path_owner(rots::text::truncate_at_null(path));
    contents = rots::text::truncate_at_null(contents);
    const std::string temp_path = path_owner + ".tmp";

    FILE* temp_file = std::fopen(temp_path.c_str(), "wb");
    if (temp_file == nullptr) {
        set_error(error_message, std::string("Unable to open temporary board file '") + temp_path + "': " + std::strerror(errno));
        return false;
    }

    const size_t bytes_written = contents.empty() ? 0 : std::fwrite(contents.data(), sizeof(char), contents.size(), temp_file);
    const int flush_result = std::fflush(temp_file);
    const int close_result = std::fclose(temp_file);

    if (bytes_written != contents.size() || flush_result != 0 || close_result != 0) {
        std::remove(temp_path.c_str());
        set_error(error_message, std::string("Failed to write temporary board file '") + temp_path + "'.");
        return false;
    }

    if (rots_rename_replace(temp_path, path_owner) != 0) {
        const std::string rename_error = std::strerror(errno);
        std::remove(temp_path.c_str());
        set_error(error_message, "Failed to move temporary board file into place: " + rename_error);
        return false;
    }

    return true;
}


#ifdef TESTING
bool write_json_text_for_testing(std::string_view path, std::string_view contents, std::string* error_message)
{
    return write_file_contents_atomically(path, contents, error_message);
}

std::string binary_label_error_for_testing(std::string_view label)
{
    const std::string bytes;
    size_t offset = 0;
    int value = 0;
    std::string error_message;
    read_i32(bytes, &offset, &value, &error_message, label);
    return error_message;
}
#endif

bool legacy_board_file_from_binary(const std::string& bytes, BoardSaveData* data, std::string* error_message)
{
    if (data == nullptr) {
        set_error(error_message, "Board data output parameter must not be null.");
        return false;
    }

    size_t offset = 0;
    int num_of_msgs = 0, last_message = 0;
    if (!read_i32(bytes, &offset, &num_of_msgs, error_message, "num_of_msgs"))
        return false;
    if (!read_i32(bytes, &offset, &last_message, error_message, "last_message"))
        return false;

    if (num_of_msgs < 1) {
        set_error(error_message, "Board file corrupt: num_of_msgs must be >= 1.");
        return false;
    }

    BoardSaveData parsed;
    parsed.last_message = last_message;
    parsed.messages.reserve(static_cast<size_t>(num_of_msgs));
    for (int i = 0; i < num_of_msgs; ++i) {
        BoardMessageData message;
        if (!read_legacy_record(bytes, &offset, &message, error_message))
            return false;
        parsed.messages.push_back(std::move(message));
    }

    // Deliberate post-legacy hardening (Phase 2a Task 4 review finding): the
    // original loader trusted num_of_msgs and never checked for leftover
    // bytes after the last record, so a truncated/appended-to legacy file
    // could silently under- or over-read. This decoder is stricter and
    // rejects any trailing bytes outright. Verified harmless against all 25
    // real legacy .boa files on disk at the time this check was added (none
    // had trailing bytes) -- this is a one-time migration-path guard, not a
    // behavior change to the live JSON format.
    if (offset != bytes.size()) {
        set_error(error_message, "Board file corrupt: trailing bytes after the last message record.");
        return false;
    }

    *data = std::move(parsed);
    set_error(error_message, "");
    return true;
}

std::string serialize_board_to_json(const BoardSaveData& data)
{
    std::string output;
    output.append("{\n");
    std::format_to(std::back_inserter(output), "  \"version\": {},\n", data.version);
    std::format_to(std::back_inserter(output), "  \"last_message\": {},\n", data.last_message);
    output.append("  \"messages\": [\n");
    for (size_t index = 0; index < data.messages.size(); ++index) {
        const BoardMessageData& message = data.messages[index];
        output.append("    {\n");
        std::format_to(std::back_inserter(output), "      \"slot_num\": {},\n", message.slot_num);
        std::format_to(std::back_inserter(output), "      \"msg_num\": {},\n", message.msg_num);
        std::format_to(std::back_inserter(output), "      \"heading\": \"{}\",\n", json_utils::escape_json_string(message.heading));
        std::format_to(std::back_inserter(output), "      \"level\": {},\n", message.level);
        std::format_to(std::back_inserter(output), "      \"post_time\": {},\n", message.post_time);
        std::format_to(std::back_inserter(output), "      \"has_message\": {},\n", (message.has_message ? "true" : "false"));
        std::format_to(std::back_inserter(output), "      \"message\": \"{}\"\n", json_utils::escape_json_string(message.message));
        output.append("    }");
        if (index + 1 < data.messages.size())
            output.append(",");
        output.append("\n");
    }
    output.append("  ]\n");
    output.append("}\n");
    return output;
}

bool deserialize_board_from_json(std::string_view json, BoardSaveData *data, std::string *error_message) {
    if (data == nullptr) {
        set_error(error_message, "Board data output parameter must not be null.");
        return false;
    }

    BoardSaveData parsed;
    const bool ok = json_utils::JsonReader(json).parse_root_object(
        [&](std::string_view key, json_utils::JsonReader* reader, std::string* nested_error) {
            if (key == "version")
                return reader->parse_integer(&parsed.version, nested_error);
            if (key == "last_message")
                return reader->parse_integer(&parsed.last_message, nested_error);
            if (key == "messages") {
                return reader->parse_array(
                    [&](json_utils::JsonReader* message_reader, std::string* message_error) {
                        BoardMessageData message;
                        const bool message_ok = message_reader->parse_object(
                            [&](std::string_view message_key, json_utils::JsonReader* nested_reader, std::string* nested_message_error) {
                                if (message_key == "slot_num")
                                    return nested_reader->parse_integer(&message.slot_num, nested_message_error);
                                if (message_key == "msg_num")
                                    return nested_reader->parse_integer(&message.msg_num, nested_message_error);
                                if (message_key == "heading")
                                    return nested_reader->parse_string(&message.heading, nested_message_error);
                                if (message_key == "level")
                                    return nested_reader->parse_integer(&message.level, nested_message_error);
                                if (message_key == "post_time")
                                    return nested_reader->parse_integer(&message.post_time, nested_message_error);
                                if (message_key == "has_message")
                                    return nested_reader->parse_bool(&message.has_message, nested_message_error);
                                if (message_key == "message")
                                    return nested_reader->parse_string(&message.message, nested_message_error);
                                return nested_reader->skip_value(nested_message_error);
                            },
                            message_error);
                        if (!message_ok)
                            return false;
                        parsed.messages.push_back(std::move(message));
                        return true;
                    },
                    nested_error);
            }
            return reader->skip_value(nested_error);
        },
        error_message);

    if (!ok)
        return false;

    if (!message_len_invariant_holds(parsed)) {
        set_error(error_message, "Board JSON message is inconsistent: has_message=false but message is non-empty.");
        return false;
    }

    *data = std::move(parsed);
    set_error(error_message, "");
    return true;
}

bool board_save_data_equal(const BoardSaveData& a, const BoardSaveData& b)
{
    if (a.version != b.version || a.last_message != b.last_message)
        return false;
    if (a.messages.size() != b.messages.size())
        return false;

    for (size_t index = 0; index < a.messages.size(); ++index) {
        const BoardMessageData& m1 = a.messages[index];
        const BoardMessageData& m2 = b.messages[index];
        if (m1.slot_num != m2.slot_num || m1.msg_num != m2.msg_num || m1.level != m2.level
            || m1.post_time != m2.post_time || m1.heading != m2.heading
            || m1.has_message != m2.has_message || m1.message != m2.message)
            return false;
    }

    return true;
}

std::string board_json_path(std::string_view legacy_path)
{
    return std::string(rots::text::truncate_at_null(legacy_path)) + ".json";
}

bool convert_legacy_board_file(std::string_view legacy_path, std::string* error_message)
{
    legacy_path = rots::text::truncate_at_null(legacy_path);
    if (legacy_path.empty()) {
        set_error(error_message, "Legacy board path must not be empty.");
        return false;
    }

    std::string legacy_bytes;
    if (!read_binary_file_contents(legacy_path, &legacy_bytes)) {
        set_error(error_message, std::string("Failed to read legacy board file '") + std::string(legacy_path) + "': " + std::strerror(errno));
        return false;
    }

    BoardSaveData decoded;
    std::string decode_error;
    if (!legacy_board_file_from_binary(legacy_bytes, &decoded, &decode_error)) {
        set_error(error_message, "Decode failed: " + decode_error);
        return false;
    }

    const std::string json = serialize_board_to_json(decoded);

    // Verify (binding conversion contract): re-decode the freshly serialized
    // JSON and compare it field-for-field to the original decode -- not a
    // re-serialization/string comparison.
    BoardSaveData reparsed;
    std::string verify_error;
    if (!deserialize_board_from_json(json, &reparsed, &verify_error)) {
        set_error(error_message, "Verify-decode of freshly serialized JSON failed: " + verify_error);
        return false;
    }

    if (!board_save_data_equal(decoded, reparsed)) {
        set_error(error_message, "Verify mismatch: re-decoded JSON does not equal the original legacy decode.");
        return false;
    }

    const std::string json_path = board_json_path(legacy_path);
    std::string write_error;
    if (!write_file_contents_atomically(json_path, json, &write_error)) {
        set_error(error_message, write_error);
        return false;
    }

    const std::string migrated_path = std::string(legacy_path) + ".migrated";
    if (rots_rename_replace(legacy_path, migrated_path) != 0) {
        // The JSON is written and verified at this point -- data is not at
        // risk -- but the legacy file could not be retired. Matches
        // convert_plrobjs.cpp's "partial success" contract: report it (via
        // a non-empty, non-fatal error_message) but still return true, since
        // the thing that matters for subsequent loads (the JSON file) is
        // safely in place; the legacy file is simply left behind for a
        // future retry.
        set_error(error_message,
            std::string("Board converted but legacy rename to '") + migrated_path + "' failed: " + std::strerror(errno));
        return true;
    }

    set_error(error_message, "");
    return true;
}

} // namespace boards_json
