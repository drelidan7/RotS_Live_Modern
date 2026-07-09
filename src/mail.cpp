/* ************************************************************************
 *   File: mail.c                                        Part of CircleMUD *
 *  Usage: Internal funcs and player spec-procs of mud-mail system         *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

/******* MUD MAIL SYSTEM MAIN FILE ***************************************

NOTE - Copied with ammendments from Circle v.3 beta

Written by Jeremy Elson (jelson@server.cs.jhu.edu)

INSTALLATION INSTRUCTIONS
-------------------------

0.  Change your makefile so that mail.c is compiled and linked into the MUD.

1.  Edit mail.h and change the constants to your personal preferences.

2.  Create a room in your MUD designated as the Post Office and create a
    mobile to be used as the postmaster; in spec_assign.c, assign the function
    "postmaster" to your mailman MOB.

3.  In db.h, define MAIL_FILE as the mail file's filename.

4.  In db.c, define an int called no_mail to indicate whether the mail
    system is active or not.  Include mail.h in db.c.  Somewhere in the mud's
    bootup sequence, call the mail system's boot-up function like this:

        log("Booting mail system.");
        if (!scan_file()) {
           log("   Mail system error -- mail system disabled!");
           no_mail = 1;
        }

5.  In structs.h, define a player ACT flag called PLR_MAILING.  If your MUD
    has a flag to indicate whether or not the player is writing (i.e. on
    the board) change PLR_WRITING (in this file) to the name of your writing
    flag.  If your mud has no writing flag, change (PLR_MAILING | PLR_WRITING)
    (in this file) to simply PLR_MAILING.

6.  Include mail.h in interpreter.c.  As a character is logging in, make
    sure to clear the PLR_MAILING bit (and PLR_WRITING bit, if necessary)
    right after the character is loaded (this happens after the name is
    entered).  This prevents strange things from happening in case a player
    cuts their link while writing mail.

7.  Include mail.h in modify.c.  In the function string_add in modify.c,
    make the following modification:

        if (terminator)	{
           if (!d->connected && (IS_FLAGGED(d->character, PLR_MAILING))) {
              store_mail(d->name, d->character->player.name, *d->str);
              RELEASE(*d->str);
              RELEASE(d->str);
              RELEASE(d->name);
              d->name = 0;
              SEND_TO_Q("Message sent!\n\r", d);
              if (!IS_NPC(d->character))
                 REMOVE_BIT(d->character->specials.act, PLR_MAILING | PLR_WRITING);
           }

           d->str = 0;
           if (d->connected == CON_EXDSCR) {
              SEND_TO_Q(MENU, d);
                . . . . .

END OF INSTALLATION INSTRUCTIONS

Note: you may notice some similarity in this mail system to another mail
system written for Alex(?) MUD.  Originally, I had intended to use their
low-level mail system and write my own interface for it, but I later
found that their mail system didn't work (or I wasn't using it correctly :)).
So, I wrote my own low-level system with the same function names so that
the interface I had just written would work with it.

Bottom line -- all code you see below was written by me.

Send comments, bug reports, etc. to jelson@server.cs.jhu.edu

*/

#include "platdef.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "platform_compat.h"
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "comm.h"
#include "db.h"
#include "handler.h"
#include "interpre.h"
#include "json_utils.h"
#include "mail.h"
#include "structs.h"
#include "utils.h"

void postmaster_send_mail(struct char_data* ch, int cmd, char* arg, char_data* host);
void postmaster_check_mail(struct char_data* ch, int cmd, char* arg, char_data* host);
void postmaster_receive_mail(struct char_data* ch, int cmd, char* arg, char_data* host);

extern struct room_data* world;
extern struct index_data* mob_index;
extern struct obj_data* object_list;
extern int no_mail;
int find_name(char* name);
int _parse_name(char* arg, char* name);

// ---------------------------------------------------------------------------
// Phase 2a Task 5: mail persistence as JSON, plus a one-time legacy
// block-file converter. See mail.h for the mail_json namespace's
// schema/contract doc comments.
// ---------------------------------------------------------------------------
namespace mail_json {
namespace {

    void set_error(std::string* error_message, const std::string& message)
    {
        if (error_message)
            *error_message = message;
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
    bool read_i32(const std::string& bytes, size_t* offset, long* value, std::string* error_message, const char* label)
    {
        if (*offset + 4 > bytes.size()) {
            set_error(error_message, std::string("Truncated mail file while reading ") + label + ".");
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
    bool read_fixed_cstring(const std::string& bytes, size_t* offset, size_t field_size, std::string* out, std::string* error_message, const char* label)
    {
        if (*offset + field_size > bytes.size()) {
            set_error(error_message, std::string("Truncated mail file while reading ") + label + ".");
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

    bool read_whole_file_contents(const char* path, std::string* bytes)
    {
        FILE* file = std::fopen(path, "rb");
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
    bool write_file_contents_atomically(const std::string& path, const std::string& contents, std::string* error_message)
    {
        const std::string temp_path = path + ".tmp";

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

        if (rots_rename_replace(temp_path.c_str(), path.c_str()) != 0) {
            const std::string rename_error = std::strerror(errno);
            std::remove(temp_path.c_str());
            set_error(error_message, "Failed to move temporary mail file into place: " + rename_error);
            return false;
        }

        return true;
    }

} // namespace

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
    std::ostringstream output;
    output << "{\n";
    output << "  \"messages\": [\n";
    for (size_t index = 0; index < data.messages.size(); ++index) {
        const MailMessageData& message = data.messages[index];
        output << "    {\n";
        output << "      \"to\": \"" << json_utils::escape_json_string(message.to) << "\",\n";
        output << "      \"from\": \"" << json_utils::escape_json_string(message.from) << "\",\n";
        output << "      \"mail_time\": " << message.mail_time << ",\n";
        output << "      \"body\": \"" << json_utils::escape_json_string(message.body) << "\"\n";
        output << "    }";
        if (index + 1 < data.messages.size())
            output << ",";
        output << "\n";
    }
    output << "  ]\n";
    output << "}\n";
    return output.str();
}

bool deserialize_mail_from_json(const std::string& json, MailStoreData* data, std::string* error_message)
{
    if (data == nullptr) {
        set_error(error_message, "Mail data output parameter must not be null.");
        return false;
    }

    MailStoreData parsed;
    const bool ok = json_utils::JsonReader(json).parse_root_object(
        [&](const std::string& key, json_utils::JsonReader* reader, std::string* nested_error) {
            if (key == "messages") {
                return reader->parse_array(
                    [&](json_utils::JsonReader* message_reader, std::string* message_error) {
                        MailMessageData message;
                        const bool message_ok = message_reader->parse_object(
                            [&](const std::string& message_key, json_utils::JsonReader* nested_reader, std::string* nested_message_error) {
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

std::string mail_json_path(const std::string& legacy_path)
{
    return legacy_path + ".json";
}

bool convert_legacy_mail_file(const char* legacy_path, std::string* error_message)
{
    if (legacy_path == nullptr || !*legacy_path) {
        set_error(error_message, "Legacy mail path must not be empty.");
        return false;
    }

    std::string legacy_bytes;
    if (!read_whole_file_contents(legacy_path, &legacy_bytes)) {
        set_error(error_message, std::string("Failed to read legacy mail file '") + legacy_path + "': " + std::strerror(errno));
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
    if (rots_rename_replace(legacy_path, migrated_path.c_str()) != 0) {
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

// ---------------------------------------------------------------------------
// Runtime in-memory mail store + index (unchanged in spirit from the legacy
// implementation, but the "position" a position_list_type node carries is
// now an index into g_mail_messages rather than a legacy file byte offset --
// deletions tombstone the slot (RuntimeMailMessage::deleted) instead of
// erasing it, so positions recorded by other still-live index entries never
// dangle. JSON serialization always skips tombstoned entries, so they never
// reappear after a reboot.
// ---------------------------------------------------------------------------

// In-memory position list / recipient index. Pure bookkeeping (never part
// of the on-disk format either legacy or JSON) -- moved out of mail.h since
// nothing outside mail.cpp ever referenced these types. `position` used to
// be a legacy file byte offset; it is now an index into g_mail_messages
// (see below).
struct position_list_type_d {
    long position;
    struct position_list_type_d* next;
};
typedef struct position_list_type_d position_list_type;

struct mail_index_type_d {
    char recipient[NAME_SIZE + 1]; /* who the mail is for */
    position_list_type* list_start; /* list of mail positions    */
    struct mail_index_type_d* next;
};
typedef struct mail_index_type_d mail_index_type;

struct RuntimeMailMessage {
    mail_json::MailMessageData data;
    bool deleted = false;
};

static std::vector<RuntimeMailMessage> g_mail_messages; /* all messages loaded this boot, plus any stored since; tombstoned (not erased) on delete so recorded positions stay valid */

mail_index_type* mail_index = 0; /* list of recs in the mail file  */

mail_index_type*
find_char_in_index(char* searchee)
{
    mail_index_type* temp_rec;

    if (!*searchee) {
        log("SYSERR: Mail system -- non fatal error #1.");
        return 0;
    }

    for (temp_rec = mail_index;
         (temp_rec && str_cmp(temp_rec->recipient, searchee));
         temp_rec = temp_rec->next)
        ;

    return temp_rec;
}

// Writes the whole in-memory g_mail_messages set (skipping tombstoned
// entries) to <MAIL_FILE>.json atomically. Called after every store_mail/
// read_delete mutation, mirroring the legacy write_to_file's synchronous-
// to-disk semantics (each legacy mutation landed on disk immediately too).
// A write failure is logged but not otherwise fatal -- the in-memory state
// (and thus game-visible behavior) is unaffected either way.
static void persist_mail_or_log(void)
{
    mail_json::MailStoreData data;
    data.messages.reserve(g_mail_messages.size());
    for (const RuntimeMailMessage& message : g_mail_messages) {
        if (!message.deleted)
            data.messages.push_back(message.data);
    }

    const std::string json = mail_json::serialize_mail_to_json(data);
    const std::string json_path = mail_json::mail_json_path(MAIL_FILE);
    std::string write_error;
    if (!mail_json::write_file_contents_atomically(json_path, json, &write_error)) {
        char errbuf[512];
        snprintf(errbuf, sizeof(errbuf), "SYSERR: failed to write mail JSON file '%s': %s", json_path.c_str(), write_error.c_str());
        log(errbuf);
    }
}

void index_mail(char* raw_name_to_index, long pos)
{
    mail_index_type* new_index;
    position_list_type* new_position;
    char name_to_index[100]; /* I'm paranoid.  so sue me. */
    char* src;
    int i;

    if (!raw_name_to_index || !*raw_name_to_index) {
        log("SYSERR: Mail system -- non-fatal error #4.");
        return;
    }

    for (src = raw_name_to_index, i = 0; *src;)
        name_to_index[i++] = tolower(*src++);
    name_to_index[i] = 0;

    if (!(new_index = find_char_in_index(name_to_index))) {
        /* name not already in index.. add it */
        new_index = (mail_index_type*)malloc(sizeof(mail_index_type));
        strncpy(new_index->recipient, name_to_index, NAME_SIZE);
        new_index->recipient[strlen(name_to_index)] = '\0';
        new_index->list_start = 0;

        /* add to front of list */
        new_index->next = mail_index;
        mail_index = new_index;
    }

    /* now, add this position to front of position list */
    new_position = (position_list_type*)malloc(sizeof(position_list_type));
    new_position->position = pos;
    new_position->next = new_index->list_start;
    new_index->list_start = new_position;
}

/* SCAN_FILE */
/* scan_file is called once during boot-up.  It loads/converts the mail
   store and indexes all entries currently in it. */
int scan_file(void)
{
    const std::string json_path = mail_json::mail_json_path(MAIL_FILE);

    auto load_json_bytes_into_index = [&](const std::string& json_bytes) -> bool {
        mail_json::MailStoreData data;
        std::string error;
        if (!mail_json::deserialize_mail_from_json(json_bytes, &data, &error)) {
            char errbuf[512];
            snprintf(errbuf, sizeof(errbuf), "SYSERR: Mail JSON file '%s' corrupt: %s", json_path.c_str(), error.c_str());
            log(errbuf);
            return false;
        }

        g_mail_messages.clear();
        g_mail_messages.reserve(data.messages.size());
        int total_messages = 0;
        for (mail_json::MailMessageData& message : data.messages) {
            const long position = static_cast<long>(g_mail_messages.size());
            std::string to_copy = message.to; // index_mail lower-cases its own scratch copy; keep the stored value untouched.
            g_mail_messages.push_back(RuntimeMailMessage { std::move(message), false });
            index_mail(const_cast<char*>(to_copy.c_str()), position);
            total_messages++;
        }

        char buf[128];
        snprintf(buf, sizeof(buf), "   Mail file read -- %d messages.", total_messages);
        log(buf);
        return true;
    };

    std::string json_bytes;
    if (mail_json::read_whole_file_contents(json_path.c_str(), &json_bytes))
        return load_json_bytes_into_index(json_bytes) ? 1 : 0;

    /* No JSON store yet -- either a fresh install (no legacy file either) or
       a legacy block-chain file needs one-time conversion. */
    FILE* legacy_probe = fopen(MAIL_FILE, "rb");
    if (!legacy_probe) {
        log("Mail file non-existant... creating new file.");
        std::string write_error;
        if (!mail_json::write_file_contents_atomically(json_path, mail_json::serialize_mail_to_json({}), &write_error)) {
            char errbuf[512];
            snprintf(errbuf, sizeof(errbuf), "SYSERR: Failed creating empty mail JSON file '%s': %s", json_path.c_str(), write_error.c_str());
            log(errbuf);
            return 0;
        }
        g_mail_messages.clear();
        return 1;
    }
    fclose(legacy_probe);

    std::string convert_error;
    if (!mail_json::convert_legacy_mail_file(MAIL_FILE, &convert_error)) {
        char errbuf[512];
        snprintf(errbuf, sizeof(errbuf), "SYSERR: Failed converting legacy mail file '%s' to JSON: %s", MAIL_FILE, convert_error.c_str());
        log(errbuf);
        return 0;
    }

    char logbuf[512];
    if (!convert_error.empty())
        snprintf(logbuf, sizeof(logbuf), "Converted legacy mail file '%s' to JSON (warning: %s).", MAIL_FILE, convert_error.c_str());
    else
        snprintf(logbuf, sizeof(logbuf), "Converted legacy mail file '%s' to JSON.", MAIL_FILE);
    log(logbuf);

    if (!mail_json::read_whole_file_contents(json_path.c_str(), &json_bytes)) {
        log("SYSERR: Mail JSON file missing immediately after conversion.");
        return 0;
    }
    return load_json_bytes_into_index(json_bytes) ? 1 : 0;
} /* end of scan_file */

/* HAS_MAIL */
/* a simple little function which tells you if the guy has mail or not */
int has_mail(char* recipient)
{
    if (find_char_in_index(recipient))
        return 1;
    return 0;
}

/* STORE_MAIL  */
/* call store_mail to store mail.  (hard, huh? :-) )  Pass 3 pointers..
   who the mail is to (name), who it's from (name), and a pointer to the
   actual message text.			*/

void store_mail(char* to, char* from, char* message_pointer)
{
    if (!message_pointer) // sender probably aborted
        return;

    if (!*from || !*to || !*message_pointer) {
        log("SYSERR: Mail system -- non-fatal error #5.");
        return;
    }

    mail_json::MailMessageData message;

    char to_buf[NAME_SIZE + 1];
    memset(to_buf, 0, sizeof(to_buf));
    strncpy(to_buf, to, NAME_SIZE);
    for (char* tmp = to_buf; *tmp; tmp++)
        *tmp = tolower(*tmp);
    to_buf[NAME_SIZE] = '\0';
    message.to = to_buf;

    char from_buf[NAME_SIZE + 1];
    memset(from_buf, 0, sizeof(from_buf));
    strncpy(from_buf, from, NAME_SIZE);
    from_buf[NAME_SIZE] = '\0';
    message.from = from_buf;

    message.mail_time = time(0);
    message.body = message_pointer;

    const long position = static_cast<long>(g_mail_messages.size());
    g_mail_messages.push_back(RuntimeMailMessage { std::move(message), false });
    index_mail(to_buf, position); /* add it to mail index in memory */

    persist_mail_or_log();
} /* store mail */

/* READ_DELETE */
/* read_delete takes 1 char pointer to the name of the person whose mail
you're retrieving.  It returns to you a char pointer to the message text.
The mail is then discarded from the file and the mail index. */

char* read_delete(char* recipient, char* recipient_formatted, int is_good)
/* recipient is the name as it appears in the index.
     recipient_formatted is the name as it should appear on the mail
     header (i.e. the text handed to the player) */
{
    mail_index_type *mail_pointer, *prev_mail;
    position_list_type* position_pointer;
    long mail_address;
    char *message, *tmstr, buf[200];
    size_t string_size;

    if (!*recipient || !*recipient_formatted) {
        log("SYSERR: Mail system -- non-fatal error #6.");
        return 0;
    }
    if (!(mail_pointer = find_char_in_index(recipient))) {
        log("SYSERR: Mail system -- post office spec_proc error?  Error #7.");
        return 0;
    }
    if (!(position_pointer = mail_pointer->list_start)) {
        log("SYSERR: Mail system -- non-fatal error #8.");
        return 0;
    }

    if (!(position_pointer->next)) /* just 1 entry in list. */ {
        mail_address = position_pointer->position;
        RELEASE(position_pointer);

        /* now free up the actual name entry */
        if (mail_index == mail_pointer) { /* name is 1st in list */
            mail_index = mail_pointer->next;
            RELEASE(mail_pointer);
        } else {
            /* find entry before the one we're going to del */
            for (prev_mail = mail_index;
                 prev_mail->next != mail_pointer;
                 prev_mail = prev_mail->next)
                ;

            prev_mail->next = mail_pointer->next;
            RELEASE(mail_pointer);
        }
    } else {
        /* move to next-to-last record */
        while (position_pointer->next->next)
            position_pointer = position_pointer->next;

        mail_address = position_pointer->next->position;
        RELEASE(position_pointer->next);
        position_pointer->next = 0;
    }

    /* ok, now lets do some readin'! */
    if (mail_address < 0 || static_cast<size_t>(mail_address) >= g_mail_messages.size()
        || g_mail_messages[static_cast<size_t>(mail_address)].deleted) {
        log("SYSERR: Oh dear.");
        no_mail = 1;
        log("SYSERR: Mail system disabled!  -- Error #9.");
        return 0;
    }

    RuntimeMailMessage& stored = g_mail_messages[static_cast<size_t>(mail_address)];

    time_t mail_time_value = static_cast<time_t>(stored.data.mail_time);
    tmstr = asctime(localtime(&mail_time_value));
    *(tmstr + strlen(tmstr) - 1) = '\0';

    if (is_good)
        sprintf(buf, " --- Postal Service of Gondor ---\n\r"
                     "Date: %s\n\r"
                     "  To: %s\n\r"
                     "From: %s\n\r\n\r",
            tmstr, recipient_formatted, stored.data.from.c_str());
    else
        sprintf(buf, " !!! Subversive Messaging System !!!\n\r"
                     "Date: %s\n\r"
                     "  To: %s\n\r"
                     "From: %s\n\r\n\r",
            tmstr, recipient_formatted, stored.data.from.c_str());

    string_size = strlen(buf) + stored.data.body.size() + 1;
    message = (char*)malloc(string_size);
    strcpy(message, buf);
    message[strlen(buf)] = '\0';
    strcat(message, stored.data.body.c_str());
    message[string_size - 1] = '\0';

    /* mark the message as deleted (tombstoned, not erased -- see
       g_mail_messages' declaration comment) and persist. */
    stored.deleted = true;
    persist_mail_or_log();

    return message;
}

/*****************************************************************
** Below is the spec_proc for a postmaster using the above       **
** routines.  Written by Jeremy Elson (jelson@server.cs.jhu.edu) **
*****************************************************************/

SPECIAL(postmaster)
{
    if (!ch->desc)
        return 0; /* so mobs don't get caught here */

    switch (cmd) {
    case CMD_MAIL: /* mail */
        postmaster_send_mail(ch, cmd, arg, host);
        return 1;
        break;
    case CMD_CHECK: /* check */
        postmaster_check_mail(ch, cmd, arg, host);
        return 1;
        break;
    case CMD_RECEIVE: /* receive */
        postmaster_receive_mail(ch, cmd, arg, host);
        return 1;
        break;
    default:
        return 0;
        break;
    }
}

int mail_ok(struct char_data* ch)
{
    if (no_mail) {
        send_to_char("Sorry, the mail system is having technical difficulties.\n\r", ch);
        return 0;
    }

    return 1;
}

/* Find mailman removed - crashed the mud and isn't needed anyway... */

void postmaster_send_mail(struct char_data* ch, int cmd, char* arg, char_data* host)
{
    struct char_data* mailman;
    char buf[200], recipient[100], *tmp;

    mailman = host;

    if (GET_LEVEL(ch) < MIN_MAIL_LEVEL) {
        sprintf(buf, "$n tells you, 'Sorry, you have to be level %d to send mail!'", MIN_MAIL_LEVEL);
        act(buf, FALSE, mailman, 0, ch, TO_VICT);
        return;
    }

    if (!*arg) { /* you'll get no argument from me! */
        act("$n tells you, 'You need to specify an addressee!'",
            FALSE, mailman, 0, ch, TO_VICT);
        return;
    }

    if (GET_GOLD(ch) < STAMP_PRICE && GET_LEVEL(ch) < LEVEL_IMMORT) {
        sprintf(buf, "$n tells you, 'A stamp costs %d silver coins.'\n\r"
                     "$n tells you, '...which I see you can't afford.'",
            STAMP_PRICE / 100);
        act(buf, FALSE, mailman, 0, ch, TO_VICT);
        return;
    }

    _parse_name(arg, recipient);

    if (find_name(recipient) < 0) {
        act("$n tells you, 'No one by that name is registered here!'",
            FALSE, mailman, 0, ch, TO_VICT);
        return;
    }

    for (tmp = recipient; *tmp; tmp++)
        *tmp = tolower(*tmp);

    act("$n starts to write some mail.", TRUE, ch, 0, 0, TO_ROOM);

    if (GET_LEVEL(ch) < LEVEL_IMMORT) {
        sprintf(buf, "$n tells you, 'I'll take %d silver coins for the stamp.'",
            STAMP_PRICE / 100);
        act(buf, FALSE, mailman, 0, ch, TO_VICT);
        GET_GOLD(ch) -= STAMP_PRICE;
    }

    sprintf(buf, "$n tells you, 'Write your message, use %%e when done.'");
    act(buf, FALSE, mailman, 0, ch, TO_VICT);
    SET_BIT(PLR_FLAGS(ch), PLR_MAILING | PLR_WRITING);

    ch->desc->name = (char*)str_dup(recipient);
    ch->desc->str = (char**)malloc(sizeof(char*));
    *(ch->desc->str) = 0;
    ch->desc->max_str = MAX_MAIL_SIZE;
}

void postmaster_check_mail(struct char_data* ch, int cmd, char* arg, char_data* host)
{
    struct char_data* mailman;
    char buf[200], recipient[100], *tmp;

    mailman = host;

    _parse_name(GET_NAME(ch), recipient);

    for (tmp = recipient; *tmp; tmp++)
        *tmp = tolower(*tmp);

    if (has_mail(recipient))
        sprintf(buf, "$n tells you, 'You have mail waiting.'");
    else
        sprintf(buf, "$n tells you, 'Sorry, you don't have any mail waiting.'");
    act(buf, FALSE, mailman, 0, ch, TO_VICT);
}

void postmaster_receive_mail(struct char_data* ch, int cmd, char* arg, char_data* host)
{
    struct char_data* mailman;
    char buf[200], recipient[100], *tmp;
    struct obj_data* tmp_obj;

    mailman = host;

    _parse_name(GET_NAME(ch), recipient);

    for (tmp = recipient; *tmp; tmp++)
        *tmp = tolower(*tmp);

    if (!has_mail(recipient)) {
        sprintf(buf, "$n tells you, 'Sorry, you don't have any mail waiting.'");
        act(buf, FALSE, mailman, 0, ch, TO_VICT);
        return;
    }

    while (has_mail(recipient)) {
        CREATE(tmp_obj, struct obj_data, 1);
        clear_object(tmp_obj);

        tmp_obj->name = str_dup("mail paper letter");
        tmp_obj->short_description = str_dup("a letter");
        tmp_obj->description = str_dup("Someone has dropped a letter here.");

        tmp_obj->obj_flags.type_flag = ITEM_NOTE;
        tmp_obj->obj_flags.wear_flags = ITEM_TAKE | ITEM_HOLD;
        tmp_obj->obj_flags.weight = 1;
        tmp_obj->obj_flags.cost = 30;
        tmp_obj->obj_flags.cost_per_day = 10;
        tmp_obj->action_description = read_delete(recipient, GET_NAME(ch), (GET_ALIGNMENT(ch) > 0));

        if (!tmp_obj->action_description)
            tmp_obj->action_description = str_dup("Mail system error - please report.  Error #11.\n\r");

        tmp_obj->next = object_list;
        object_list = tmp_obj;

        obj_to_char(tmp_obj, ch);

        tmp_obj->item_number = -1;

        act("$n gives you a letter.", FALSE, mailman, 0, ch, TO_VICT);
        act("$N gives a letter to $n.", FALSE, ch, 0, mailman, TO_ROOM);
    }
}
