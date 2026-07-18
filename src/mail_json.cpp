/* mail_json.cc */

// Carved out of mail.cpp (persist-split wave, PS Task 2). Holds the
// mail_json codec namespace: JSON-first load, legacy 100-byte block-chain
// binary decoder, atomic temp+rename writes, and the one-time legacy-to-
// JSON converter (verify-reparse + '.migrated' rename). mail.cpp keeps the
// runtime store (find_char_in_index/persist_mail_or_log/index_mail/
// scan_file/has_mail) and postmaster gameplay. Declarations stay in
// mail.h; body below is byte-identical to mail.cpp's prior namespace
// mail_json block (see mail.cpp's git history for the removed original)
// with ONE named deviation: read_whole_file_contents() and
// write_file_contents_atomically() lose the anonymous-namespace linkage
// they had in mail.cpp -- mail.cpp's scan_file()/persist_mail_or_log()
// still call them cross-TU after this move (a duplicate-hazard the PS
// Task 2 classification missed), so they need external linkage. Declared
// in mail.h next to the rest of the mail_json API.

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <iterator>
#include <string>
#include <vector>

#include "json_utils.h"
#include "mail.h"
#include "platform_compat.h"
#include "text_view.h"

namespace mail_json {
namespace {

    void set_error(std::string* error_message, std::string_view message)
    {
        if (error_message)
            error_message->assign(rots::text::truncate_at_null(message));
    }

    // --- Legacy on-disk block-chain format -----------------------------
    //
    // Frozen forever: these describe bytes already written to disk by every
    // historical build of this mud, so they are literal, hardcoded 32-bit
    // values -- never derived from sizeof(long) or mail.h's (mutable, live-
    // format) NAME_SIZE -- so this decoder can never silently misread old
    // files if some future change alters those for newly-written data.
    // Verified byte-for-byte against the real 171,400-byte
    // lib/misc/plrmail (1,714 100-byte blocks; see
    // docs/superpowers/sdd/p2a-task-5-report.md for the hexdump walkthrough).
    constexpr size_t kBlockSize = 100; // historical BLOCK_SIZE
    constexpr size_t kLongSize = 4; // sizeof(long) on the 32-bit build that wrote these files
    constexpr size_t kNameFieldSize = 16; // NAME_SIZE(15) + 1 for the NUL
    constexpr size_t kHeaderBlockDataSize = kBlockSize - 1 - (kNameFieldSize * 2 + 3 * kLongSize); // 55
    constexpr size_t kHeaderTextFieldSize = kHeaderBlockDataSize + 1; // 56
    constexpr size_t kDataBlockDataSize = kBlockSize - kLongSize - 1; // 95
    constexpr size_t kDataTextFieldSize = kDataBlockDataSize + 1; // 96
    constexpr long kHeaderBlock = -1;
    constexpr long kLastBlock = -2;
    // kDeletedBlock (-3) is intentionally not referenced: the converter's
    // top-level scan only ever acts on kHeaderBlock, silently skipping
    // everything else (deleted blocks, chain-interior data blocks, and any
    // other stray on-disk value) -- exactly mirroring the live scan_file's
    // own tolerance. See legacy_mail_file_from_binary below.

    // Little-endian 4-byte int read at an explicit offset -- portable
    // regardless of the reading process's own endianness/ABI (this repo's
    // established convention, see boards.cpp/objects_json.cpp).
    bool read_i32(const std::string &bytes, size_t *offset, long *value,
                  std::string *error_message, std::string_view label) {
        label = rots::text::truncate_at_null(label);
        if (*offset + 4 > bytes.size()) {
            set_error(error_message, std::string("Truncated mail file while reading ") +
                                         std::string(label) + ".");
            return false;
        }

        const uint32_t raw = static_cast<uint32_t>(static_cast<unsigned char>(bytes[*offset]))
            | (static_cast<uint32_t>(static_cast<unsigned char>(bytes[*offset + 1])) << 8)
            | (static_cast<uint32_t>(static_cast<unsigned char>(bytes[*offset + 2])) << 16)
            | (static_cast<uint32_t>(static_cast<unsigned char>(bytes[*offset + 3])) << 24);
        *value = static_cast<long>(static_cast<int32_t>(raw));
        *offset += 4;
        return true;
    }

    // Reads a fixed-size field and takes the C-string up to its first NUL
    // (or the whole field, if none) -- matches how from/to/txt were always
    // written (strncpy + explicit trailing NUL), regardless of how much of
    // the field is actually used.
    bool read_fixed_cstring(const std::string &bytes, size_t *offset, size_t field_size, std::string *out,
                            std::string *error_message, std::string_view label) {
        label = rots::text::truncate_at_null(label);
        if (*offset + field_size > bytes.size()) {
            set_error(error_message, std::string("Truncated mail file while reading ") +
                                         std::string(label) + ".");
            return false;
        }
        const char* field_start = bytes.data() + *offset;
        const void* nul = memchr(field_start, '\0', field_size);
        const size_t string_len = nul ? (static_cast<const char*>(nul) - field_start) : field_size;
        out->assign(field_start, string_len);
        *offset += field_size;
        return true;
    }

    // Decodes one message rooted at a header block (bytes[header_offset] is
    // already known to be block_type == kHeaderBlock): the header's
    // from/to/mail_time/txt, then walks the next_block chain (following
    // each data block's own block_type-as-link field) until kLastBlock,
    // concatenating each block's text. Defensively rejects out-of-range or
    // cyclic links (the legacy read_delete had no such protection and could
    // in principle spin/misbehave on a corrupt chain; the real file has
    // zero such cases -- see the task report).
    bool decode_message_at(const std::string& bytes, size_t header_offset, MailMessageData* message, std::string* error_message)
    {
        size_t offset = header_offset;
        long block_type = 0, next_block = 0, mail_time = 0;
        if (!read_i32(bytes, &offset, &block_type, error_message, "header block_type"))
            return false;
        if (!read_i32(bytes, &offset, &next_block, error_message, "header next_block"))
            return false;

        std::string from, to;
        if (!read_fixed_cstring(bytes, &offset, kNameFieldSize, &from, error_message, "header from"))
            return false;
        if (!read_fixed_cstring(bytes, &offset, kNameFieldSize, &to, error_message, "header to"))
            return false;
        if (!read_i32(bytes, &offset, &mail_time, error_message, "header mail_time"))
            return false;

        std::string body;
        if (!read_fixed_cstring(bytes, &offset, kHeaderTextFieldSize, &body, error_message, "header txt"))
            return false;

        std::vector<long> visited_links;
        visited_links.push_back(static_cast<long>(header_offset));
        long chain_link = next_block;
        while (chain_link != kLastBlock) {
            if (chain_link < 0 || static_cast<size_t>(chain_link) % kBlockSize != 0
                || static_cast<size_t>(chain_link) + kBlockSize > bytes.size()) {
                set_error(error_message, "Mail file corrupt: message chain links to an invalid block.");
                return false;
            }
            for (long visited : visited_links) {
                if (visited == chain_link) {
                    set_error(error_message, "Mail file corrupt: message chain contains a cycle.");
                    return false;
                }
            }
            visited_links.push_back(chain_link);

            size_t data_offset = static_cast<size_t>(chain_link);
            long data_block_type = 0;
            if (!read_i32(bytes, &data_offset, &data_block_type, error_message, "data block_type"))
                return false;
            std::string data_text;
            if (!read_fixed_cstring(bytes, &data_offset, kDataTextFieldSize, &data_text, error_message, "data txt"))
                return false;

            body += data_text;
            chain_link = data_block_type;
        }

        message->to = std::move(to);
        message->from = std::move(from);
        message->mail_time = mail_time;
        message->body = std::move(body);
        return true;
    }

} // namespace

// Promoted out of mail_json's anonymous namespace (persist-split PS Task 2
// deviation, mirroring color_convert.cpp's sync_color_slot_foreground_from_ansi
// precedent): mail.cpp's runtime store (scan_file/persist_mail_or_log) still
// calls these two cross-TU after this carve, so they need external linkage.
// Declared in mail.h. Bodies are otherwise byte-identical to their prior
// anonymous-namespace versions (dedented one level to match mail_json's
// unindented top-level functions).
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

// Temp-file + rename atomic write, matching write_player_objects_json's
// pattern in objsave.cpp.
bool write_file_contents_atomically(std::string_view path, std::string_view contents, std::string* error_message)
{
    const std::string path_owner(rots::text::truncate_at_null(path));
    contents = rots::text::truncate_at_null(contents);
    const std::string temp_path = path_owner + ".tmp";

    FILE* temp_file = std::fopen(temp_path.c_str(), "wb");
    if (temp_file == nullptr) {
        set_error(error_message, std::string("Unable to open temporary mail file '") + temp_path + "': " + std::strerror(errno));
        return false;
    }

    const size_t bytes_written = contents.empty() ? 0 : std::fwrite(contents.data(), sizeof(char), contents.size(), temp_file);
    const int flush_result = std::fflush(temp_file);
    const int close_result = std::fclose(temp_file);

    if (bytes_written != contents.size() || flush_result != 0 || close_result != 0) {
        std::remove(temp_path.c_str());
        set_error(error_message, std::string("Failed to write temporary mail file '") + temp_path + "'.");
        return false;
    }

    if (rots_rename_replace(temp_path, path_owner) != 0) {
        const std::string rename_error = std::strerror(errno);
        std::remove(temp_path.c_str());
        set_error(error_message, "Failed to move temporary mail file into place: " + rename_error);
        return false;
    }

    return true;
}



#ifdef TESTING
bool write_json_text_for_testing(std::string_view path, std::string_view contents, std::string* error_message)
{
    return write_file_contents_atomically(path, contents, error_message);
}
#endif

bool legacy_mail_file_from_binary(const std::string& bytes, MailStoreData* data, std::string* error_message)
{
    if (data == nullptr) {
        set_error(error_message, "Mail data output parameter must not be null.");
        return false;
    }

    if (bytes.size() % kBlockSize != 0) {
        set_error(error_message, "Mail file corrupt: size is not a multiple of the 100-byte block size.");
        return false;
    }

    MailStoreData parsed;
    const size_t num_blocks = bytes.size() / kBlockSize;
    for (size_t block_index = 0; block_index < num_blocks; ++block_index) {
        const size_t block_offset = block_index * kBlockSize;
        size_t peek_offset = block_offset;
        long block_type = 0;
        if (!read_i32(bytes, &peek_offset, &block_type, error_message, "block_type"))
            return false;

        if (block_type != kHeaderBlock)
            continue; // Deleted/interior/stray blocks: silently skipped, matching
                       // the live scan_file's own tolerance (see the file header
                       // comment on kDeletedBlock above).

        MailMessageData message;
        std::string decode_error;
        if (!decode_message_at(bytes, block_offset, &message, &decode_error)) {
            set_error(error_message, decode_error);
            return false;
        }
        parsed.messages.push_back(std::move(message));
    }

    *data = std::move(parsed);
    set_error(error_message, "");
    return true;
}

std::string serialize_mail_to_json(const MailStoreData& data)
{
    std::string output;
    output.append("{\n");
    output.append("  \"messages\": [\n");
    for (size_t index = 0; index < data.messages.size(); ++index) {
        const MailMessageData& message = data.messages[index];
        output.append("    {\n");
        std::format_to(std::back_inserter(output), "      \"to\": \"{}\",\n", json_utils::escape_json_string(message.to));
        std::format_to(std::back_inserter(output), "      \"from\": \"{}\",\n", json_utils::escape_json_string(message.from));
        std::format_to(std::back_inserter(output), "      \"mail_time\": {},\n", message.mail_time);
        std::format_to(std::back_inserter(output), "      \"body\": \"{}\"\n", json_utils::escape_json_string(message.body));
        output.append("    }");
        if (index + 1 < data.messages.size())
            output.append(",");
        output.append("\n");
    }
    output.append("  ]\n");
    output.append("}\n");
    return output;
}

bool deserialize_mail_from_json(std::string_view json, MailStoreData *data, std::string *error_message) {
    if (data == nullptr) {
        set_error(error_message, "Mail data output parameter must not be null.");
        return false;
    }

    MailStoreData parsed;
    const bool ok = json_utils::JsonReader(json).parse_root_object(
        [&](std::string_view key, json_utils::JsonReader* reader, std::string* nested_error) {
            if (key == "messages") {
                return reader->parse_array(
                    [&](json_utils::JsonReader* message_reader, std::string* message_error) {
                        MailMessageData message;
                        const bool message_ok = message_reader->parse_object(
                            [&](std::string_view message_key, json_utils::JsonReader* nested_reader, std::string* nested_message_error) {
                                if (message_key == "to")
                                    return nested_reader->parse_string(&message.to, nested_message_error);
                                if (message_key == "from")
                                    return nested_reader->parse_string(&message.from, nested_message_error);
                                if (message_key == "mail_time")
                                    return nested_reader->parse_long(&message.mail_time, nested_message_error);
                                if (message_key == "body")
                                    return nested_reader->parse_string(&message.body, nested_message_error);
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

    *data = std::move(parsed);
    set_error(error_message, "");
    return true;
}

bool mail_store_data_equal(const MailStoreData& a, const MailStoreData& b)
{
    if (a.messages.size() != b.messages.size())
        return false;

    for (size_t index = 0; index < a.messages.size(); ++index) {
        const MailMessageData& m1 = a.messages[index];
        const MailMessageData& m2 = b.messages[index];
        if (m1.to != m2.to || m1.from != m2.from || m1.mail_time != m2.mail_time || m1.body != m2.body)
            return false;
    }

    return true;
}

std::string mail_json_path(std::string_view legacy_path)
{
    return std::string(rots::text::truncate_at_null(legacy_path)) + ".json";
}

bool convert_legacy_mail_file(std::string_view legacy_path, std::string* error_message)
{
    legacy_path = rots::text::truncate_at_null(legacy_path);
    if (legacy_path.empty()) {
        set_error(error_message, "Legacy mail path must not be empty.");
        return false;
    }

    std::string legacy_bytes;
    if (!read_whole_file_contents(legacy_path, &legacy_bytes)) {
        set_error(error_message, std::string("Failed to read legacy mail file '") + std::string(legacy_path) + "': " + std::strerror(errno));
        return false;
    }

    MailStoreData decoded;
    std::string decode_error;
    if (!legacy_mail_file_from_binary(legacy_bytes, &decoded, &decode_error)) {
        set_error(error_message, "Decode failed: " + decode_error);
        return false;
    }

    const std::string json = serialize_mail_to_json(decoded);

    // Verify (binding conversion contract): re-decode the freshly serialized
    // JSON and compare it field-for-field to the original decode -- not a
    // re-serialization/string comparison.
    MailStoreData reparsed;
    std::string verify_error;
    if (!deserialize_mail_from_json(json, &reparsed, &verify_error)) {
        set_error(error_message, "Verify-decode of freshly serialized JSON failed: " + verify_error);
        return false;
    }

    if (!mail_store_data_equal(decoded, reparsed)) {
        set_error(error_message, "Verify mismatch: re-decoded JSON does not equal the original legacy decode.");
        return false;
    }

    const std::string json_path = mail_json_path(legacy_path);
    std::string write_error;
    if (!write_file_contents_atomically(json_path, json, &write_error)) {
        set_error(error_message, write_error);
        return false;
    }

    const std::string migrated_path = std::string(legacy_path) + ".migrated";
    if (rots_rename_replace(legacy_path, migrated_path) != 0) {
        // The JSON is written and verified at this point -- data is not at
        // risk -- but the legacy file could not be retired. Matches
        // boards.cpp/convert_plrobjs.cpp's "partial success" contract:
        // report it (via a non-empty, non-fatal error_message) but still
        // return true, since the thing that matters for subsequent loads
        // (the JSON file) is safely in place; the legacy file is simply
        // left behind for a future retry.
        set_error(error_message,
            std::string("Mail converted but legacy rename to '") + migrated_path + "' failed: " + std::strerror(errno));
        return true;
    }

    set_error(error_message, "");
    return true;
}

} // namespace mail_json
