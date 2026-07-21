// output_seam.cpp -- defines the five global output-path symbols
// (send_to_char x2, vsend_to_char, act, track_specialized_mage,
// untrack_specialized_mage) declared in comm.h/utils.h, forwarding each
// through the rots::output::Sinks registered by comm.cpp's
// register_game_output_sinks() (spec Sec13 pattern; see output_seam.h's file
// comment). Joins ROTS_CORE_SOURCES: it only reaches down into rots::log
// (rots_platform) and libc, never into a game type it would need to
// dereference -- opaque char_data*/obj_data* pointers pass straight through.
//
// vsend_to_char is hosted here too (moved verbatim from comm.cpp): it needs
// no sink of its own, since it only ever calls the send_to_char forwarder
// below.
//
// BLOCKER-BUSTER EXTENSION (+7): send_to_all/send_to_room/
// send_to_room_except_two/break_spell/abort_delay/complete_delay/
// get_from_txt_block_pool(std::string_view) join the five above (see
// output_seam.h's own extension comment). Same shape, same tier membership,
// same null-safe "logged no-op" default for every VOID forwarder; the one
// pointer-returning forwarder (get_from_txt_block_pool) instead
// tripwire-logs THEN ABORTS on an unregistered sink -- see its own
// definition below for the contrast with the six void forwarders.

#include "output_seam.h"

#include "comm.h"
#include "rots/platform/log.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <format>

namespace {

// Backing storage for the registered sinks. A plain namespace-scope static
// (unlike the logging seam's function-local static in rots_log.cpp) is safe
// here: Sinks is a POD aggregate of function pointers, so zero-
// initialization happens at compile time -- there is no dynamic initializer
// that could race another translation unit's static-init order. Null until
// comm.cpp's register_game_output_sinks() runs (before boot_db(), see
// output_seam.h).
rots::output::Sinks g_sinks {};

} // namespace

namespace rots::output {

void set_sinks(const Sinks& sinks)
{
    g_sinks = sinks;
}

} // namespace rots::output

void send_to_char(std::string_view message, char_data* character)
{
    if (g_sinks.send_to_char) {
        g_sinks.send_to_char(message, character);
        return;
    }
    rots::log::write_stderr(std::format(
        "rots::output: STUB send_to_char(message, char_data*) called with no sink registered "
        "(message: '{}') -- this should be unreachable once register_game_output_sinks() has run.",
        message));
}

void send_to_char(std::string_view message, int character_id)
{
    if (g_sinks.send_to_char_id) {
        g_sinks.send_to_char_id(message, character_id);
        return;
    }
    rots::log::write_stderr(std::format(
        "rots::output: STUB send_to_char(message, id={}) called with no sink registered "
        "(message: '{}') -- this should be unreachable once register_game_output_sinks() has run.",
        character_id, message));
}

void vsend_to_char(char_data* character, const char* format, ...)
{
#define BUFSIZE 2048
    char buf[BUFSIZE];
    va_list ap;

    va_start(ap, format);
    vsnprintf(buf, BUFSIZE - 1, format, ap);
    buf[BUFSIZE - 1] = '\0';
    va_end(ap);

    send_to_char(buf, character);
}

void act(std::string_view str, int hide_invisible, char_data* ch, obj_data* obj, void* vict_obj,
    int type, char spam_only)
{
    if (g_sinks.act) {
        g_sinks.act(str, hide_invisible, ch, obj, vict_obj, type, spam_only);
        return;
    }
    rots::log::write_stderr(std::format(
        "rots::output: STUB act('{}') called with no sink registered -- this should be "
        "unreachable once register_game_output_sinks() has run.",
        str));
}

void track_specialized_mage(char_data* mage)
{
    if (g_sinks.track_mage) {
        g_sinks.track_mage(mage);
        return;
    }
    rots::log::write_stderr(std::format(
        "rots::output: STUB track_specialized_mage({}) called with no sink registered -- this "
        "should be unreachable once register_game_output_sinks() has run.",
        static_cast<const void*>(mage)));
}

void untrack_specialized_mage(char_data* mage)
{
    if (g_sinks.untrack_mage) {
        g_sinks.untrack_mage(mage);
        return;
    }
    rots::log::write_stderr(std::format(
        "rots::output: STUB untrack_specialized_mage({}) called with no sink registered -- this "
        "should be unreachable once register_game_output_sinks() has run.",
        static_cast<const void*>(mage)));
}

void send_to_all(std::string_view message)
{
    if (g_sinks.send_to_all) {
        g_sinks.send_to_all(message);
        return;
    }
    rots::log::write_stderr(std::format(
        "rots::output: STUB send_to_all(message) called with no sink registered "
        "(message: '{}') -- this should be unreachable once register_game_output_sinks() has run.",
        message));
}

void send_to_room(std::string_view message, int room)
{
    if (g_sinks.send_to_room) {
        g_sinks.send_to_room(message, room);
        return;
    }
    rots::log::write_stderr(std::format(
        "rots::output: STUB send_to_room(message, room={}) called with no sink registered "
        "(message: '{}') -- this should be unreachable once register_game_output_sinks() has run.",
        room, message));
}

void send_to_room_except_two(
    std::string_view message, int room, char_data* excluded_first, char_data* excluded_second)
{
    if (g_sinks.send_to_room_except_two) {
        g_sinks.send_to_room_except_two(message, room, excluded_first, excluded_second);
        return;
    }
    rots::log::write_stderr(std::format(
        "rots::output: STUB send_to_room_except_two(message, room={}) called with no sink "
        "registered (message: '{}') -- this should be unreachable once "
        "register_game_output_sinks() has run.",
        room, message));
}

void break_spell(char_data* ch)
{
    if (g_sinks.break_spell) {
        g_sinks.break_spell(ch);
        return;
    }
    rots::log::write_stderr(std::format(
        "rots::output: STUB break_spell({}) called with no sink registered -- this should be "
        "unreachable once register_game_output_sinks() has run.",
        static_cast<const void*>(ch)));
}

void abort_delay(char_data* wait_ch)
{
    if (g_sinks.abort_delay) {
        g_sinks.abort_delay(wait_ch);
        return;
    }
    rots::log::write_stderr(std::format(
        "rots::output: STUB abort_delay({}) called with no sink registered -- this should be "
        "unreachable once register_game_output_sinks() has run.",
        static_cast<const void*>(wait_ch)));
}

void complete_delay(char_data* ch)
{
    if (g_sinks.complete_delay) {
        g_sinks.complete_delay(ch);
        return;
    }
    rots::log::write_stderr(std::format(
        "rots::output: STUB complete_delay({}) called with no sink registered -- this should be "
        "unreachable once register_game_output_sinks() has run.",
        static_cast<const void*>(ch)));
}

// Unlike the six void forwarders above, an unregistered hit here is a hard
// failure (loud log + abort), matching entity_hooks.h's get_txt_block_fn
// twin hook (the no-arg overload's pair) and its documented rationale: there
// is no safe placeholder txt_block* to return. comm.cpp's own write_to_q()
// calls this overload internally and immediately dereferences the result
// (pnew->next = ...) -- a silently-returned null would surface as a
// confusing null-deref far from the real cause instead of failing loudly at
// the hook boundary (exactly the failure mode this wave's red-proofing hit
// when the earlier revision of this forwarder returned null: it produced a
// SEGFAULT in write_to_q(), not a clean, attributable failure). comm.cpp
// registers the real pool function in run_the_game() before boot_db(), so
// ageland never reaches this path; rots_convert links rots_core but never
// calls this overload either (same "unreachable there too" class as
// entity_hooks.h's other tripwire hooks). Untested by design, same as every
// other abort tripwire in the tree -- no death test (not an established
// suite idiom here); the positive path (GetFromTxtBlockPoolReachesTheReal
// SinkWhenRegistered, comm_delay_tests.cpp) is this forwarder's coverage.
txt_block* get_from_txt_block_pool(std::string_view line)
{
    if (g_sinks.get_txt_block_from_pool) {
        return g_sinks.get_txt_block_from_pool(line);
    }
    rots::log::write_stderr(std::format(
        "rots::output: FATAL get_from_txt_block_pool(line='{}') called with no sink registered -- "
        "this should be unreachable once register_game_output_sinks() has run.",
        line));
    std::abort();
}

// Unlike get_from_txt_block_pool() above, an unregistered hit here is a
// SAFE logged no-op, not abort: PUT never dereferences its argument (see
// output_seam.h's put_txt_block_to_pool_fn comment) -- a discarded block
// would leak, not crash. comm.cpp registers the real
// put_to_txt_block_pool_impl() body in run_the_game() before boot_db(), so
// ageland never reaches this path in practice.
void put_to_txt_block_pool(struct txt_block* pold)
{
    if (g_sinks.put_txt_block_to_pool) {
        g_sinks.put_txt_block_to_pool(pold);
        return;
    }
    rots::log::write_stderr(
        "rots::output: STUB put_to_txt_block_pool() called with no sink registered -- this "
        "should be unreachable once register_game_output_sinks() has run.");
}

// close_socket() (behavior wave Task 1) -- unlike the six void forwarders
// above (which have always lived here), this one takes over a symbol
// comm.cpp itself used to define AND call internally; comm.cpp's own real
// body is renamed to close_socket_impl (registered as this seam's sink)
// and its own internal call sites now call close_socket_impl directly,
// mirroring how send_to_all() has no comm.cpp-internal caller of its own
// plain-symbol form. Same null-safe "logged no-op" default as the six void
// forwarders above.
void close_socket(struct descriptor_data* d, int drop_all)
{
    if (g_sinks.close_socket) {
        g_sinks.close_socket(d, drop_all);
        return;
    }
    rots::log::write_stderr(
        "rots::output: STUB close_socket() called with no sink registered -- this should be "
        "unreachable once register_game_output_sinks() has run.");
}

// no_specials_active() (behavior wave Task 1) -- read accessor, safe
// tripwire-logged `false` default (specials not suppressed, the same
// permissive posture as the global's own pre-boot value).
bool no_specials_active()
{
    if (g_sinks.no_specials_active) {
        return g_sinks.no_specials_active();
    }
    rots::log::write_stderr(
        "rots::output: STUB no_specials_active() called with no sink registered -- this should "
        "be unreachable once register_game_output_sinks() has run.");
    return false;
}

// request_circle_shutdown() (behavior wave Task 1) -- setter forwarder,
// safe logged no-op default (a missed shutdown request under an
// unregistered sink, not a crash).
void request_circle_shutdown()
{
    if (g_sinks.request_circle_shutdown) {
        g_sinks.request_circle_shutdown();
        return;
    }
    rots::log::write_stderr(
        "rots::output: STUB request_circle_shutdown() called with no sink registered -- this "
        "should be unreachable once register_game_output_sinks() has run.");
}
