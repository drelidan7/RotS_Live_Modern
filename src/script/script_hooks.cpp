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

#include <array>
#include <cstddef>
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

namespace {
// Backing storage for the registered find_action hook
// (register_find_action_hook(), act_soci.cpp; Cluster B wave Task 1). Null
// until that registration runs; the null default is a LOGGED SAFE-SENTINEL
// -1 (see dispatch_find_action() below) -- see script_hooks.h's
// find_action_fn comment for why this class, not abort.
rots::script::find_action_fn g_find_action_hook = nullptr;
} // namespace

namespace rots::script {

void set_find_action_hook(find_action_fn hook)
{
    g_find_action_hook = hook;
}

int dispatch_find_action(char* arg)
{
    if (g_find_action_hook) {
        return g_find_action_hook(arg);
    }
    rots::log::write_stderr(
        "rots::script: STUB dispatch_find_action() called with no handler registered -- this "
        "should be unreachable once register_find_action_hook() has run.");
    return -1;
}

} // namespace rots::script

namespace {

// Backing storage for the three registered SPECIAL() fn-ptrs the
// registrar-lookup family hands out (registered_special::gen_board/
// postmaster/receptionist; spec-pair wave Task 1). Indexed by
// registered_special; each slot is null until its owner's registrar runs
// (register_gen_board_special()/register_postmaster_special()/
// register_receptionist_special(), boards.cpp/mail.cpp/objsave.cpp). The
// unregistered default is an abort tripwire (see
// lookup_registered_special() below) -- see script_hooks.h's
// registered_special comment for why this class, not a safe sentinel.
std::array<rots::script::special_func_ptr, 3> g_registered_specials {};

} // namespace

namespace rots::script {

void set_registered_special(registered_special key, special_func_ptr hook)
{
    g_registered_specials[static_cast<std::size_t>(key)] = hook;
}

special_func_ptr lookup_registered_special(registered_special key)
{
    special_func_ptr hook = g_registered_specials[static_cast<std::size_t>(key)];
    if (hook) {
        return hook;
    }
    rots::log::write_stderr(
        "rots::script: FATAL lookup_registered_special() called with no handler registered for "
        "the requested key -- this should be unreachable once every owner's registrar has run.");
    std::abort();
}

} // namespace rots::script

namespace {

// Backing storage for the registered command_min_position hook
// (register_command_min_position_hook(), interpre.cpp; spec-pair wave
// Task 1). Null until that registration runs; the null default is a SAFE
// SENTINEL (POSITION_DEAD/0, see dispatch_command_min_position() below) --
// see script_hooks.h's command_min_position_fn comment for why this
// class, not abort.
rots::script::command_min_position_fn g_command_min_position_hook = nullptr;

} // namespace

namespace rots::script {

void set_command_min_position_hook(command_min_position_fn hook)
{
    g_command_min_position_hook = hook;
}

int dispatch_command_min_position(int cmd)
{
    if (g_command_min_position_hook) {
        return g_command_min_position_hook(cmd);
    }
    rots::log::write_stderr(
        "rots::script: STUB dispatch_command_min_position() called with no handler registered "
        "-- this should be unreachable once register_command_min_position_hook() has run.");
    return 0; // POSITION_DEAD -- see script_hooks.h's command_min_position_fn comment.
}

} // namespace rots::script

namespace {

// Backing storage for the registered target_check hook
// (register_target_check_hook(), interpre.cpp; spec-pair wave Task 1).
// Null until that registration runs; the null default is a SAFE
// SENTINEL (0, see dispatch_target_check() below) -- see
// script_hooks.h's target_check_fn comment for why this class, not
// abort.
rots::script::target_check_fn g_target_check_hook = nullptr;

} // namespace

namespace rots::script {

void set_target_check_hook(target_check_fn hook)
{
    g_target_check_hook = hook;
}

int dispatch_target_check(char_data* ch, int cmd, target_data* t1, target_data* t2)
{
    if (g_target_check_hook) {
        return g_target_check_hook(ch, cmd, t1, t2);
    }
    rots::log::write_stderr(
        "rots::script: STUB dispatch_target_check() called with no handler registered -- this "
        "should be unreachable once register_target_check_hook() has run.");
    return 0; // target_check()'s own "invalid target" sentinel -- see script_hooks.h's comment.
}

} // namespace rots::script
