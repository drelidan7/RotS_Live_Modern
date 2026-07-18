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

#include "output_seam.h"

#include "comm.h"
#include "rots/platform/log.h"

#include <cstdarg>
#include <cstdio>
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
