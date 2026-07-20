/* ************************************************************************
 *   File: comm.h                                        Part of CircleMUD *
 *  Usage: header file: prototypes of public communication functions       *
 *                                                                         *
 *  All rights reserved.  See license.doc for complete information.        *
 *                                                                         *
 *  Copyright (C) 1993 by the Trustees of the Johns Hopkins University     *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 ************************************************************************ */

#ifndef COMM_H
#define COMM_H

#include <string>
#include <string_view>
#include <stdarg.h>

#include "platdef.h" /* For SocketType */
#include "utils.h" /* For the TRUE macro */

struct StartupOptions {
    sh_int port;
    std::string dir;
    bool mini_mud;
    bool new_mud;
    bool no_rent_check;
    bool restrict_game;
    bool no_specials;
    bool has_proxy;
};

/* comm.c */
/// Queues a bounded message for every connected player without retaining the view.
void send_to_all(std::string_view message);
/// Queues a bounded message for a connected character without retaining the view.
void send_to_char(std::string_view message, struct char_data* character);
/// Queues a bounded message for the connected character with the specified absolute identifier.
void send_to_char(std::string_view message, int character_id);
const char* get_char_name(int character_id);
struct char_data* get_character(int character_id);
/// Queues a bounded message for every connected player except the specified character.
void send_to_except(std::string_view message, struct char_data* excluded_character);
/// Queues a bounded message for every descriptor attached to a character in the room.
void send_to_room(std::string_view message, int room);
/// Queues a bounded message for room occupants other than the specified character.
void send_to_room_except(
    std::string_view message, int room, struct char_data* excluded_character);
/// Queues a bounded message for room occupants other than two specified characters.
void send_to_room_except_two(std::string_view message, int room,
    struct char_data* excluded_first, struct char_data* excluded_second);
/// Queues a bounded message for eligible outdoor players using the supplied light mode.
void send_to_outdoor(std::string_view message, int mode);
/// Queues a bounded message for eligible outdoor players in the supplied sector type.
void send_to_sector(std::string_view message, int sector_type);
/// Performs the legacy all-player delivery behavior for a bounded message.
void perform_to_all(std::string_view message, struct char_data* character);
void close_socket(struct descriptor_data* d, int drop_all = TRUE);
void break_spell(struct char_data* ch);
void abort_delay(char_data* wait_ch);
void complete_delay(struct char_data* ch);
/// Obtains an uninitialized text block from the reusable pool.
struct txt_block* get_from_txt_block_pool();
/// Obtains a text block whose owned storage contains the bounded input text.
struct txt_block* get_from_txt_block_pool(std::string_view line);
void put_to_txt_block_pool(struct txt_block*);

void vsend_to_char(struct char_data* ch, const char* format, ...);

/// Installs the game's mudlog broadcast sink (rots::log::set_sink) -- the
/// LEVEL_AREAGOD clamp, PRF_LOG* preference gating, descriptor_list walk, and
/// CGRN color framing that used to be mudlog()'s second half (utility.cpp,
/// pre logging-seam). Called once from run_the_game(), immediately after
/// descriptor_list is reset and before the first log() call.
void register_mudlog_broadcast_sink();

/// Installs the game's output sinks (rots::output::set_sinks, output_seam.h)
/// -- the real send_to_char/act/track_specialized_mage/
/// untrack_specialized_mage bodies, PLUS (blocker-buster wave, census section
/// D) send_to_all/send_to_room/send_to_room_except_two/break_spell/
/// abort_delay/complete_delay/get_from_txt_block_pool(std::string_view) --
/// that output_seam.cpp's forwarders otherwise fall back to a
/// tripwire-logged no-op for. Called once from run_the_game(), immediately
/// after register_mudlog_broadcast_sink() and before boot_db(), so ageland
/// never runs an output-path call with an unregistered sink.
void register_game_output_sinks();

/// Installs get_from_txt_block_pool()/put_to_txt_block_pool() (above) as
/// entity_hooks.h's txt-block-pool hook pair (world-seed Task 2
/// adjudication) -- target_data::cleanup()/operator=() (relocated to
/// entity_lifecycle.cpp) dispatch through it instead of calling this TU's
/// pool directly, since comm.cpp is not a leaf module. Called once from
/// run_the_game(), before boot_db(), so ageland never runs a TARGET_TEXT
/// copy with an unregistered hook.
void register_txt_block_pool_hooks();

/// Installs send_to_sector()/send_to_outdoor() (above) as world_hooks.h's
/// send-to-sector/send-to-outdoor hook pair (world-seed Task 5,
/// STOP-adjudicated cascade) -- weather.cpp's weather_message()/
/// weather_change()/check_sun_change()/another_hour() dispatch through it
/// instead of calling this TU's functions directly, since rots_world must
/// not reach descriptor_list (app-owned session data). Called once from
/// run_the_game(), before boot_db(), so ageland never runs a weather/time
/// tick with an unregistered hook.
void register_world_broadcast_hooks();

/// Expands a bounded action format and delivers it to the selected recipients.
void act(std::string_view str, int hide_invisible, struct char_data* ch,
    struct obj_data* obj, void* vict_obj, int type, char spam_only = FALSE);

#define TO_ROOM 0
#define TO_VICT 1
#define TO_NOTVICT 2
#define TO_CHAR 3

/// Writes a bounded message to a socket, retrying until every byte is sent or an error occurs.
int write_to_descriptor(SocketType descriptor, std::string_view text);
/// Copies a bounded message into the owning text queue.
void write_to_q(std::string_view text, struct txt_q* queue);
/// Appends a bounded message to a descriptor's output buffer without retaining the view.
void write_to_output(std::string_view text, struct descriptor_data* descriptor);
/// Copies bounded text into pager-owned storage before displaying its first page.
void page_string(struct descriptor_data* descriptor, std::string_view text);
/// Pages mutable storage whose lifetime the caller guarantees until paging completes.
void page_string_borrowed(struct descriptor_data* descriptor, char* text);
/// Builds the in-round status prompt text for a descriptor into the supplied buffer.
void build_prompt(struct descriptor_data* point, std::string& out);
bool parse_startup_options(int argc, char** argv, StartupOptions* options, std::string* error_message);
#ifdef TESTING
/// Parses a bounded port value through the production startup-option parser for focused tests.
bool parse_port_value_for_testing(
    std::string_view text, sh_int* port, std::string* error_message);
#endif

/* #define SEND_TO_Q(messg, desc)  write_to_q((messg), &(desc)->output) */
#define SEND_TO_Q(messg, desc) write_to_output((messg), desc)

#define USING_SMALL(d) ((d)->output == (d)->small_outbuf)
#define USING_LARGE(d) (!USING_SMALL(d))

// Implemented in spec_ass.cc.
typedef int (*special_func_ptr)(char_data* host, char_data* character, int cmd, char* argument, int call_flag, waiting_type* wait_list);
void* virt_program_number(int number);

special_func_ptr get_special_function(int number);

#endif /* COMM_H */
