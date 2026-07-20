// combat_hooks.cpp -- backing storage + dispatch for combat_hooks.h's
// boot-registered command-dispatch table (blocker-buster wave Task 2; see
// combat_hooks.h's file comment for the full design). Joins
// ROTS_COMBAT_SOURCES: like output_seam.cpp (its closest precedent -- a
// seam header with several FUTURE callers rather than one owning TU, hence
// its own dedicated .cpp instead of folding storage into a caller file the
// way entity_hooks.h's per-hook pairs do), it only reaches down into
// rots::log (RotS::platform) for its tripwire message and passes
// char_data*/waiting_type* straight through opaquely -- never dereferenced,
// never allocated -- so no game-type header is needed here.

#include "combat_hooks.h"

#include "rots/platform/log.h"

#include <array>
#include <cstddef>
#include <format>

namespace {

// Backing storage: a plain array of function pointers, zero-initialized
// (all nullptr) at compile time -- same "POD aggregate, no dynamic
// initializer, no static-init-order risk" reasoning as output_seam.cpp's
// g_sinks. Null until register_combat_command_dispatch() (interpre.cpp)
// runs, mirroring assign_spell_pointers()'s skills[] population.
std::array<rots::combat::acmd_fn, static_cast<std::size_t>(rots::combat::combat_command::count)>
    g_command_table {};

// Backing storage for special()'s registered-hook seam (combat-pilot wave
// Task 2) -- a single fn-ptr, not an enum-indexed cell of the table above,
// since special() is its own dispatch target (see combat_hooks.h's
// special_fn comment). Null until register_combat_command_dispatch()
// (interpre.cpp) runs, same convention as g_command_table above.
rots::combat::special_fn g_special_handler = nullptr;

// Backing storage for Task 4b's hooks (combat-pilot wave): each is an
// independent fn-ptr, following g_special_handler's own "single global,
// not an enum-indexed cell" shape, since none of these share
// combat_command's fixed ACMD signature. Null until each hook's own
// per-owner registrar (handler.cpp/limits.cpp/objsave.cpp/script.cpp/
// pkill.cpp) runs.
rots::combat::extract_char_fn g_extract_char_hook = nullptr;

} // namespace

namespace rots::combat {

void set_combat_command(combat_command command, acmd_fn handler)
{
    g_command_table[static_cast<std::size_t>(command)] = handler;
}

void issue_command(
    combat_command command, char_data* ch, char* argument, waiting_type* wtl, int cmd, int subcmd)
{
    const std::size_t index = static_cast<std::size_t>(command);
    if (g_command_table[index]) {
        g_command_table[index](ch, argument, wtl, cmd, subcmd);
        return;
    }
    rots::log::write_stderr(std::format(
        "rots::combat: STUB issue_command(cell={}) called with no handler registered -- this "
        "should be unreachable once register_combat_command_dispatch() has run.",
        index));
}

void set_special_handler(special_fn handler)
{
    g_special_handler = handler;
}

int call_special(
    char_data* ch, int cmd, char* arg, int callflag, waiting_type* wtl, int in_room)
{
    if (g_special_handler) {
        return g_special_handler(ch, cmd, arg, callflag, wtl, in_room);
    }
    rots::log::write_stderr(
        "rots::combat: STUB call_special() called with no handler registered -- this should "
        "be unreachable once register_combat_command_dispatch() has run.");
    return 0;
}


// extract_char() dispatch (Task 4b) -- see combat_hooks.h's extract_char_fn
// comment for the sentinel-forward rationale.
void set_extract_char_hook(extract_char_fn hook)
{
    g_extract_char_hook = hook;
}

void extract_char(char_data* ch, int new_room)
{
    if (g_extract_char_hook) {
        g_extract_char_hook(ch, new_room);
        return;
    }
    rots::log::write_stderr(
        "rots::combat: STUB extract_char() called with no handler registered -- this should "
        "be unreachable once register_extract_char_hook() has run.");
}

void extract_char(char_data* ch)
{
    extract_char(ch, -1);
}

} // namespace rots::combat
