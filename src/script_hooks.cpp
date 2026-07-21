// script_hooks.cpp -- backing storage + dispatch for BOTH of script_hooks.h's
// upward-edge hooks: PERS (from T1) and command_interpreter (RELOCATED here
// in the l4-seed wave's Task 3 membership move, l4-task-3-brief.md Step 3).
// PERS's real body and registrar are utility.cpp; command_interpreter's real
// body and registrar are interpre.cpp -- two different owning TUs, neither
// of which is itself a rots_script member (interpre.cpp stays app-tier by
// design, utility.cpp likewise), so neither hook has a single natural owning
// caller file. Mirrors combat_hooks.cpp (a seam header with several FUTURE
// callers rather than one owning TU, hence its own dedicated .cpp instead of
// folding storage into a caller file): it only reaches down into rots::log
// (RotS::platform) for its tripwire messages and passes char_data*/
// waiting_type* straight through opaquely -- never dereferenced, never
// allocated -- so no game-type header is needed here.
//
// command_interpreter's backing storage originally lived in interpre.cpp
// (T1's placement, reasoning it was "the TU that already visits the
// target"), but that TU stays app-tier permanently (it defines
// command_interpreter() itself, a session/command-coupled function that
// cannot promote) -- so once mudlle.cpp's call site converted to
// rots::script::dispatch_command_interpreter() in Task 2, that placement
// became a genuine rots_script -> app upward edge the moment mudlle.cpp
// joined this library. Caught by rots_script_linkcheck's first ordinary
// link (two undefined symbols: dispatch_pers/dispatch_command_interpreter)
// at this task's Step 3, not silently missed -- the separate negative-probe
// exercise only confirmed the checker is non-vacuous; the fix is this
// relocation, byte-verbatim except for its own banner comment and the
// dropped "namespace rots::script { ... }" wrapper interpre.cpp no longer
// needs (this file already opens rots::script once, below, for both hooks).
// interpre.cpp keeps only register_command_interpreter_hook() -- the
// registrar, a legal app -> lib downward call into this header's public
// set_command_interpreter_hook() API.

#include "script_hooks.h"

#include "rots/platform/log.h"

#include <cstdlib>

namespace {

// Backing storage for the registered PERS hook (register_pers_hook(),
// utility.cpp). Null until that registration runs; the null default is an
// abort tripwire (see dispatch_pers() below) -- see script_hooks.h's pers_fn
// comment for why this class, not a logged no-op.
rots::script::pers_fn g_pers_hook = nullptr;

// Backing storage for the registered command-interpreter hook
// (register_command_interpreter_hook(), interpre.cpp). Null until that
// registration runs; the null default is a LOUD LOGGED TRIPWIRE + no-op
// (see dispatch_command_interpreter() below) -- see script_hooks.h's
// command_interpreter_fn comment for why this class, not abort.
rots::script::command_interpreter_fn g_command_interpreter_hook = nullptr;

} // namespace

namespace rots::script {

void set_pers_hook(pers_fn hook) { g_pers_hook = hook; }

char* dispatch_pers(char_data* target, char_data* observer, int capitalize, int force_visible)
{
    if (g_pers_hook) {
        return g_pers_hook(target, observer, capitalize, force_visible);
    }
    rots::log::write_stderr(
        "rots::script: FATAL dispatch_pers() called with no handler registered -- this should "
        "be unreachable once register_pers_hook() has run.");
    std::abort();
}

void set_command_interpreter_hook(command_interpreter_fn hook)
{
    g_command_interpreter_hook = hook;
}

void dispatch_command_interpreter(char_data* ch, char* argument_chr, waiting_type* argument_wtl)
{
    if (g_command_interpreter_hook) {
        g_command_interpreter_hook(ch, argument_chr, argument_wtl);
        return;
    }
    rots::log::write_stderr(
        "rots::script: STUB dispatch_command_interpreter() called with no handler registered -- "
        "this should be unreachable once register_command_interpreter_hook() has run.");
}

} // namespace rots::script

namespace {
// Backing storage for the registered virt_program_number hook
// (register_virt_program_number_hook(), spec_ass.cpp; behavior wave Task 1).
// Null until that registration runs; the null default is an abort tripwire
// (see dispatch_virt_program_number() below) -- see script_hooks.h's
// virt_program_fn comment for why this class, not a logged no-op.
rots::script::virt_program_fn g_virt_program_number_hook = nullptr;
} // namespace

namespace rots::script {

void set_virt_program_number_hook(virt_program_fn hook)
{
    g_virt_program_number_hook = hook;
}

void* dispatch_virt_program_number(int number)
{
    if (g_virt_program_number_hook) {
        return g_virt_program_number_hook(number);
    }
    rots::log::write_stderr(
        "rots::script: FATAL dispatch_virt_program_number() called with no handler registered -- "
        "this should be unreachable once register_virt_program_number_hook() has run.");
    std::abort();
}

} // namespace rots::script

namespace {
// Backing storage for the registered virt_assignmob hook
// (register_virt_assignmob_hook(), spec_ass.cpp; Cluster B wave Task 1).
// Null until that registration runs; the null default is an abort tripwire
// (see dispatch_virt_assignmob() below) -- see script_hooks.h's
// virt_assignmob_fn comment for why this class, not a logged no-op.
rots::script::virt_assignmob_fn g_virt_assignmob_hook = nullptr;
} // namespace

namespace rots::script {

void set_virt_assignmob_hook(virt_assignmob_fn hook)
{
    g_virt_assignmob_hook = hook;
}

void dispatch_virt_assignmob(char_data* mob)
{
    if (g_virt_assignmob_hook) {
        g_virt_assignmob_hook(mob);
        return;
    }
    rots::log::write_stderr(
        "rots::script: FATAL dispatch_virt_assignmob() called with no handler registered -- this "
        "should be unreachable once register_virt_assignmob_hook() has run.");
    std::abort();
}

} // namespace rots::script
