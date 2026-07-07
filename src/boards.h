/* ************************************************************************
 *   File: boards.h                                      Part of CircleMUD *
 *  Usage: header file for bulletin boards                                 *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#ifndef BOARDS_H
#define BOARDS_H

#include "platdef.h" /* For byte typedefs */

#include <string>
#include <vector>

#define NUM_OF_BOARDS 24
// #define NUM_OF_BOARDS      (board_info_type::num_of_boards)
#define MAX_BOARD_MESSAGES 100 /* arbitrary */
#define MAX_BIG_BOARD_MESSAGES 500 /* arbitrary */
#define MAX_MAIL_MESSAGES 1000 /* arbitrary */
#define MAX_MESSAGE_LENGTH 4096 /* arbitrary, obsolete  */

#define BOARD_DIR "boards"
#define BOARD_HTML_DIR "boards"

#define INDEX_SIZE (((NUM_OF_BOARDS - 2) * MAX_BOARD_MESSAGES) + (2 * MAX_BIG_BOARD_MESSAGES) + MAX_MAIL_MESSAGES + 5)

struct board_msginfo {
    int slot_num; /* pos of message in "master index" */
    int msg_num; /* "absolute" number of the post */
    char* heading; /* pointer to message's heading */
    int level; /* level of poster */
    int post_time; /* when it was posted */
    int heading_len; /* size of header (for file write) */
    int message_len; /* size of message text (for file write) */
};

struct board_info_type {
    static byte num_of_boards;
    byte lnum; /* local number, should be 0 ... N */
    long vnum; /* vnum of this board */
    int read_lvl; /* min level to read messages on this board */
    int write_lvl; /* min level to write messages on this board */
    int remove_lvl; /* min level to remove messages from this board */
    char short_name[50]; /*filename without directories,used for html, too*/
    char filename[50]; /* file to save this board to */
    char title[50]; /* used in html only */
    int rnum; /* rnum of this board */

    int last_message; /* max number of the message written so far */
    int num_of_msgs;
    int max_of_msgs;
    byte tmp_allflag;
    byte is_changed;

    struct board_msginfo* msg_index;

    virtual void write_message(struct char_data* ch, char* arg, int num = 0);
    virtual int show_board(struct char_data* ch, char* arg, int allflag);
    virtual int select_msg(int msg, int softflag = 0); // 0 - hard search, !0-soft
    virtual int display_msg(struct char_data* ch, char* arg, int nextflag);
    int remove_msg(struct char_data* ch, char* arg);
    int count_msg(char_data* ch, int cur_num);
    void flush_board();
    void save_board();
    void load_board();
    void reset_board();
    virtual int msg_msgnum(int i) { return msg_index[i].msg_num; }
    virtual void msg_msgnum(int i, int j) { msg_index[i].msg_num = j; }
    virtual int approve_msg(char_data* ch, board_msginfo* msg, int cur_num, int* num);
    // see mail_info_type for explanation

    board_info_type(int objnum, int l_read, int l_write, int l_rem,
        int max_msg, char* file, char* titlename);
    board_info_type();
};

struct mail_info_type : board_info_type {
    int number;
    virtual void write_message(struct char_data* ch, char* arg, int num = 0);
    virtual int approve_msg(char_data* ch, board_msginfo* msg, int cur_num, int* num);
    // return 1 if msg was approved for list/read/etc
    // num is taken as the shown number of the previous message,
    // returned as the number to show with msg.
    mail_info_type(int objnum, int l_read, int l_write, int l_rem,
        int max_msg, char* file, char* titlename);
};
#define VNUM (vnum)
#define READ_LVL (read_lvl)
#define WRITE_LVL (write_lvl)
#define REMOVE_LVL (remove_lvl)
#define FILENAME (filename)
#define RNUM (rnum)

#define G_VNUM(i) ((i)->vnum)
#define G_READ_LVL(i) ((i)->read_lvl)
#define G_WRITE_LVL(i) ((i)->write_lvl)
#define G_REMOVE_LVL(i) ((i)->remove_lvl)
#define G_FILENAME(i) ((i)->filename)
#define G_RNUM(i) ((i)->rnum)

#define NEW_MSG_INDEX (msg_index[num_of_msgs])
#define MSG_HEADING(j) (msg_index[j].heading)
#define MSG_SLOTNUM(j) (msg_index[j].slot_num)
#define MSG_POSTTIME(j) (msg_index[j].post_time)
#define MSG_LEVEL(j) (msg_index[j].level)
#define MSG_CURMSG(ch) (ch->specials.board_point[lnum])

// JSON persistence for board files (Phase 2a Task 4). Replaces the legacy
// fwrite of `board_msginfo` (which includes a raw `char*` heading pointer,
// meaningless once reloaded) plus its adjacent heading/message text blobs.
// One JSON file per board, `<boardfile>.json` (e.g. "boards/boa11.boa.json"),
// written atomically (temp file + rename, see write_player_objects_json in
// objsave.cpp for the established pattern this follows). The legacy decoder
// stays only to support the one-time boot conversion of pre-existing
// `<boardfile>.boa` files; nothing ever writes that binary format again.
namespace boards_json {

static constexpr int BOARD_SCHEMA_VERSION = 1;

// One posted message. `has_message` distinguishes "no body was ever posted
// slot" (legacy on-disk message_len == 0 -- the slot_num on disk was already
// invalid/stale at save time) from "an empty-but-present body" (message_len
// == 1, i.e. just the NUL terminator) -- load_board() only calls find_slot()
// to allocate runtime storage when has_message is true, mirroring the legacy
// loader's own `if ((len2 = msg_index[i].message_len))` gate exactly.
struct BoardMessageData {
    int slot_num = 0;
    int msg_num = 0;
    int level = 0;
    int post_time = 0;
    std::string heading;
    bool has_message = false;
    std::string message;
};

struct BoardSaveData {
    int version = BOARD_SCHEMA_VERSION;
    int last_message = 0;
    std::vector<BoardMessageData> messages;
};

// Decodes the legacy 32-bit on-disk `<boardfile>.boa` layout: a header of
// two ints (num_of_msgs, last_message), followed by num_of_msgs records of
// exactly 28 bytes each (slot_num, msg_num, a `char*` heading pointer that is
// read-and-discarded, level, post_time, heading_len, message_len -- 7 x
// 4-byte fields; verified against a hexdump of a real board file, see
// docs/superpowers/sdd/p2a-task-4-report.md), each immediately followed by
// heading_len bytes of heading text and (iff message_len > 0) message_len
// bytes of message text -- both including their own trailing NUL byte.
bool legacy_board_file_from_binary(const std::string& bytes, BoardSaveData* data, std::string* error_message = nullptr);

std::string serialize_board_to_json(const BoardSaveData& data);
bool deserialize_board_from_json(const std::string& json, BoardSaveData* data, std::string* error_message = nullptr);

// Field-for-field structural equality (not a re-serialization/string
// compare) -- used by the converter's decode/serialize/re-decode/verify
// contract before it ever renames a legacy file away.
bool board_save_data_equal(const BoardSaveData& a, const BoardSaveData& b);

// Path convention: "<path>.json" for the JSON file, "<path>.migrated" for
// the legacy file once safely converted.
std::string board_json_path(const std::string& legacy_path);

// Converts one legacy `<boardfile>.boa` file at `legacy_path`: decode ->
// serialize -> re-decode -> field-equality verify -> write JSON (atomic
// temp+rename) -> rename legacy to `<legacy_path>.migrated`. Never destroys
// the legacy file: any failure at any step returns false (with
// `*error_message` set) and leaves the legacy file exactly as found.
bool convert_legacy_board_file(const char* legacy_path, std::string* error_message = nullptr);

} // namespace boards_json

#endif /* BOARDS_H */
