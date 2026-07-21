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
rots::combat::gain_exp_fn g_gain_exp_hook = nullptr;
rots::combat::gain_exp_regardless_fn g_gain_exp_regardless_hook = nullptr;
rots::combat::remove_fame_war_bonuses_fn g_remove_fame_war_bonuses_hook = nullptr;
rots::combat::crash_crashsave_fn g_crash_crashsave_hook = nullptr;
rots::combat::call_trigger_fn g_call_trigger_hook = nullptr;
rots::combat::pkill_create_fn g_pkill_create_hook = nullptr;

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


// extract_char() dispatch RE-HOMED to entity_lifecycle.cpp (l4-seed wave,
// Task 1; l4-task-1-brief.md Step 2a; l4-census.md section 3.4) -- see
// entity_hooks.h's extract_char_fn comment for the full history.

// gain_exp()/gain_exp_regardless()/remove_fame_war_bonuses() dispatch
// (Task 4b) -- see combat_hooks.h's comments above for the HOOK-not-MOVE
// rationale for each.
void set_gain_exp_hook(gain_exp_fn hook)
{
    g_gain_exp_hook = hook;
}

void gain_exp(char_data* ch, int gain)
{
    if (g_gain_exp_hook) {
        g_gain_exp_hook(ch, gain);
        return;
    }
    rots::log::write_stderr(
        "rots::combat: STUB gain_exp() called with no handler registered -- this should be "
        "unreachable once register_gain_exp_hook() has run.");
}

void set_gain_exp_regardless_hook(gain_exp_regardless_fn hook)
{
    g_gain_exp_regardless_hook = hook;
}

void gain_exp_regardless(char_data* ch, int gain)
{
    if (g_gain_exp_regardless_hook) {
        g_gain_exp_regardless_hook(ch, gain);
        return;
    }
    rots::log::write_stderr(
        "rots::combat: STUB gain_exp_regardless() called with no handler registered -- this "
        "should be unreachable once register_gain_exp_regardless_hook() has run.");
}

void set_remove_fame_war_bonuses_hook(remove_fame_war_bonuses_fn hook)
{
    g_remove_fame_war_bonuses_hook = hook;
}

void remove_fame_war_bonuses(char_data* ch, affected_type* pkaff)
{
    if (g_remove_fame_war_bonuses_hook) {
        g_remove_fame_war_bonuses_hook(ch, pkaff);
        return;
    }
    rots::log::write_stderr(
        "rots::combat: STUB remove_fame_war_bonuses() called with no handler registered -- "
        "this should be unreachable once register_remove_fame_war_bonuses_hook() has run.");
}


// App-other trio dispatch (Task 4b) -- Crash_crashsave()/call_trigger()/
// pkill_create(); see combat_hooks.h's comments above for each hook's own
// tripwire-default rationale.
void set_crash_crashsave_hook(crash_crashsave_fn hook)
{
    g_crash_crashsave_hook = hook;
}

void crash_crashsave(char_data* ch, int rent_code)
{
    if (g_crash_crashsave_hook) {
        g_crash_crashsave_hook(ch, rent_code);
        return;
    }
    rots::log::write_stderr(
        "rots::combat: STUB crash_crashsave() called with no handler registered -- this "
        "should be unreachable once register_crash_crashsave_hook() has run.");
}

void set_call_trigger_hook(call_trigger_fn hook)
{
    g_call_trigger_hook = hook;
}

int call_trigger(int trigger_type, void* subject, void* subject2, void* subject3)
{
    if (g_call_trigger_hook) {
        return g_call_trigger_hook(trigger_type, subject, subject2, subject3);
    }
    rots::log::write_stderr(
        "rots::combat: STUB call_trigger() called with no handler registered -- defaulting to "
        "TRUE (\"no script attached\") to avoid silently vetoing/immortalizing the caller's "
        "event; this should be unreachable once register_call_trigger_hook() has run.");
    return 1; // TRUE (utils.h) -- kept as a literal so this opaque-pointer TU need not include
              // utils.h just for the macro; see combat_hooks.cpp's file comment.
}

void set_pkill_create_hook(pkill_create_fn hook)
{
    g_pkill_create_hook = hook;
}

void pkill_create(char_data* victim)
{
    if (g_pkill_create_hook) {
        g_pkill_create_hook(victim);
        return;
    }
    rots::log::write_stderr(
        "rots::combat: STUB pkill_create() called with no handler registered -- this should "
        "be unreachable once register_pkill_create_hook() has run.");
}


// Behavior-wave Task 1 hooks dispatch (see combat_hooks.h's own comments
// above for each hook's rationale) -- backing storage follows the same
// "plain global, null until its owner's registrar runs" convention as every
// hook above.
} // namespace rots::combat

namespace {
rots::combat::mobile_activity_fn g_one_mobile_activity_hook = nullptr;
rots::combat::crash_idlesave_fn g_crash_idlesave_hook = nullptr;
rots::combat::crash_extract_objs_fn g_crash_extract_objs_hook = nullptr;
} // namespace

namespace rots::combat {

void set_one_mobile_activity_hook(mobile_activity_fn hook)
{
    g_one_mobile_activity_hook = hook;
}

void dispatch_one_mobile_activity(char_data* ch)
{
    if (g_one_mobile_activity_hook) {
        g_one_mobile_activity_hook(ch);
        return;
    }
    rots::log::write_stderr(
        "rots::combat: STUB dispatch_one_mobile_activity() called with no handler registered -- "
        "this should be unreachable once register_one_mobile_activity_hook() has run.");
}

void set_crash_idlesave_hook(crash_idlesave_fn hook)
{
    g_crash_idlesave_hook = hook;
}

void crash_idlesave(char_data* ch)
{
    if (g_crash_idlesave_hook) {
        g_crash_idlesave_hook(ch);
        return;
    }
    rots::log::write_stderr(
        "rots::combat: STUB crash_idlesave() called with no handler registered -- this should "
        "be unreachable once register_crash_idlesave_hook() has run.");
}

void set_crash_extract_objs_hook(crash_extract_objs_fn hook)
{
    g_crash_extract_objs_hook = hook;
}

void crash_extract_objs(obj_data* obj)
{
    if (g_crash_extract_objs_hook) {
        g_crash_extract_objs_hook(obj);
        return;
    }
    rots::log::write_stderr(
        "rots::combat: STUB crash_extract_objs() called with no handler registered -- this "
        "should be unreachable once register_crash_extract_objs_hook() has run.");
}

} // namespace rots::combat
