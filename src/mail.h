/* ************************************************************************
 *   File: mail.h                                        Part of CircleMUD *
 *  Usage: header file for mail system                                     *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#ifndef MAIL_H
#define MAIL_H

#include <string>
#include <vector>

/******* MUD MAIL SYSTEM HEADER FILE ****************************
 ***     written by Jeremy Elson (jelson@server.cs.jhu.edu)   ***
 ****   compliments of CircleMUD (circle.cs.jhu.edu 4000)    ****
 ***************************************************************/

/* INSTALLATION INSTRUCTIONS in MAIL.C */

/* You can modify the following constants to fit your own MUD.  */

/* command numbers of the "mail", "check", and "receive" commands
   in your interpreter. */

#define CMD_CHECK 153
#define CMD_RECEIVE 154
#define CMD_MAIL 155

/* minimum level a player must be to send mail	*/
#define MIN_MAIL_LEVEL 10

/* # of gold coins required to send mail	*/
#define STAMP_PRICE 200

/* Maximum size of mail in bytes (arbitrary)	*/
#define MAX_MAIL_SIZE 4000

/* Max size of player names			*/
#define NAME_SIZE 15

/* USER CHANGABLE DEFINES ABOVE **
***************************************************************************
**   DON'T TOUCH DEFINES BELOW  */

int scan_file(void);
int has_mail(char* recipient);
void store_mail(char* to, char* from, char* message_pointer);
char* read_delete(char* recipient, char* recipient_formatted, int is_good);

#define CHAR_SIZE sizeof(char)

// ---------------------------------------------------------------------------
// Phase 2a Task 5: mail persistence as JSON, plus a one-time legacy converter.
//
// Replaces the historical 100-byte block-chain file (`lib/misc/plrmail`,
// `header_block_type`/`data_block_type` structs -- see git history for
// mail.h prior to this task) with one JSON file, `<mailfile>.json`, holding
// every live message flat: `{"messages": [{"to","from","mail_time","body"}]}`.
// The legacy block structs/constants (BLOCK_SIZE, HEADER_BLOCK_DATASIZE,
// DATA_BLOCK_DATASIZE, HEADER_BLOCK/LAST_BLOCK/DELETED_BLOCK, and the two
// on-disk structs) are now converter-local (mail.cpp, mail_json namespace)
// since nothing outside mail.cpp ever referenced them.
//
// Public mail API (scan_file/has_mail/store_mail/read_delete) keeps its
// exact signatures and game-visible behavior. Written atomically (temp file
// + rename, the write_player_objects_json pattern in objsave.cpp).
namespace mail_json {

// One live message. mail_time is the historical `time(0)` value (seconds
// since epoch) store_mail always recorded; read_delete renders it via
// asctime(localtime(...)), unchanged.
struct MailMessageData {
    std::string to;
    std::string from;
    long mail_time = 0;
    std::string body;
};

struct MailStoreData {
    std::vector<MailMessageData> messages;
};

// Decodes the legacy 32-bit on-disk block-chain file: fixed 100-byte blocks;
// a block's leading 4-byte little-endian `block_type` is -1 (header, starts
// a message), -2 (last block of a chain), -3 (deleted), or a byte offset
// (multiple of 100) linking to the next block in a chain. Only scans for
// header blocks and walks each one's chain to reconstruct to/from/mail_time
// and the concatenated body -- exactly mirroring the legacy scan_file's own
// tolerance of ignoring anything that isn't a header block (deleted blocks,
// and any other stray on-disk value, are silently skipped, never treated as
// an error). Verified byte-for-byte against the real
// 171,400-byte lib/misc/plrmail (1,714 blocks; one pre-existing, already-
// orphaned garbage block was already silently ignored by the live
// scan_file, and remains so here) -- see
// docs/superpowers/sdd/p2a-task-5-report.md.
bool legacy_mail_file_from_binary(const std::string& bytes, MailStoreData* data, std::string* error_message = nullptr);

std::string serialize_mail_to_json(const MailStoreData& data);
bool deserialize_mail_from_json(const std::string& json, MailStoreData* data, std::string* error_message = nullptr);

// Field-for-field structural equality (not a re-serialization/string
// compare) -- used by the converter's decode/serialize/re-decode/verify
// contract before it ever renames a legacy file away.
bool mail_store_data_equal(const MailStoreData& a, const MailStoreData& b);

// Path convention: "<path>.json" for the JSON file, "<path>.migrated" for
// the legacy file once safely converted.
std::string mail_json_path(const std::string& legacy_path);

// Converts one legacy mail file at `legacy_path`: decode -> serialize ->
// re-decode -> field-equality verify -> write JSON (atomic temp+rename) ->
// rename legacy to `<legacy_path>.migrated`. Never destroys the legacy
// file: any failure at any step returns false (with `*error_message` set)
// and leaves the legacy file exactly as found.
bool convert_legacy_mail_file(const char* legacy_path, std::string* error_message = nullptr);

} // namespace mail_json

#endif /* MAIL_H */
