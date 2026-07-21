// script_hooks_tests.cpp

// New test TU (l4-seed wave, Task 1; brief .superpowers/sdd/l4-task-1-brief.md
// Step 1/Step 2c; census .superpowers/sdd/l4-census.md sections 3.1/3.2).
// Covers script_hooks.h's two hooks added this task: dispatch_command_
// interpreter() (mudlle.cpp's future command_interpreter() call site) and
// dispatch_pers() (mudlle2.cpp's future PERS() call site). CONSUMER-FREE
// this task -- no mudlle.cpp/mudlle2.cpp call site converts yet -- so both
// hooks are exercised directly against a recording stub, the same
// "registered stub receives args intact; unregistered default semantics
// asserted" discriminator shape combat_hooks_tests.cpp's CombatHooksExtractChar
// suite and big_brother_hooks_tests.cpp establish for this wave's sibling
// seams.
//
// HYGIENE: gtest_main.cpp registers interpre.cpp's/utility.cpp's REAL
// forwarders (register_command_interpreter_hook()/register_pers_hook()) for
// the whole test binary. Every test below that swaps in a recording stub (or
// nullptr) restores the real registration on scope exit via
// ScopedCommandInterpreterHook/ScopedPersHook, mirroring this tree's other
// *_hooks_tests.cpp Scoped* fixtures.
//
// No death test for dispatch_pers()'s unregistered (abort-tripwire) default,
// per this suite's no-death-test convention -- output_seam.cpp's
// get_from_txt_block_pool() forwarder documents the same "untested by
// design" posture for its own abort tripwire; the registered path below is
// this hook's discriminator coverage.

#include "../comm.h"
#include "../interpre.h"
#include "../script_hooks.h"
#include "../utils.h"
#include "rots/core/character.h"

#include <gtest/gtest.h>

// act_soci.cpp has no dedicated header (see script_hooks.h's own
// find_action_fn comment); forward-declared locally here, mirroring
// comm.cpp's/gtest_main.cpp's own local declaration of this same
// registrar.
void register_find_action_hook();

namespace {

// Swaps script_hooks.h's command-interpreter hook, then restores the REAL
// interpre.cpp forwarder via register_command_interpreter_hook() on
// destruction -- there is no per-hook "restore just this one" entry point,
// same rationale as this tree's other Scoped* fixtures.
class ScopedCommandInterpreterHook {
public:
    explicit ScopedCommandInterpreterHook(rots::script::command_interpreter_fn hook)
    {
        rots::script::set_command_interpreter_hook(hook);
    }

    ~ScopedCommandInterpreterHook() { register_command_interpreter_hook(); }

    ScopedCommandInterpreterHook(const ScopedCommandInterpreterHook&) = delete;
    ScopedCommandInterpreterHook& operator=(const ScopedCommandInterpreterHook&) = delete;
};

// Same shape as ScopedCommandInterpreterHook above, for script_hooks.h's
// PERS hook.
class ScopedPersHook {
public:
    explicit ScopedPersHook(rots::script::pers_fn hook) { rots::script::set_pers_hook(hook); }

    ~ScopedPersHook() { register_pers_hook(); }

    ScopedPersHook(const ScopedPersHook&) = delete;
    ScopedPersHook& operator=(const ScopedPersHook&) = delete;
};

// Same shape as ScopedCommandInterpreterHook above, for script_hooks.h's
// virt_program_number cell (behavior wave Task 1; CONTROLLER ADDENDUM
// item 3).
class ScopedVirtProgramNumberHook {
public:
    explicit ScopedVirtProgramNumberHook(rots::script::virt_program_fn hook)
    {
        rots::script::set_virt_program_number_hook(hook);
    }

    ~ScopedVirtProgramNumberHook() { register_virt_program_number_hook(); }

    ScopedVirtProgramNumberHook(const ScopedVirtProgramNumberHook&) = delete;
    ScopedVirtProgramNumberHook& operator=(const ScopedVirtProgramNumberHook&) = delete;
};

// Same shape as ScopedVirtProgramNumberHook above, for script_hooks.h's
// virt_assignmob cell (Cluster B wave Task 1; cb-task-1-brief.md Step 2;
// cb-census.md section 5.2 -- rider gate edge 2 of the pre-authorized <=3,
// no auto-STOP).
class ScopedVirtAssignmobHook {
public:
    explicit ScopedVirtAssignmobHook(rots::script::virt_assignmob_fn hook)
    {
        rots::script::set_virt_assignmob_hook(hook);
    }

    ~ScopedVirtAssignmobHook() { register_virt_assignmob_hook(); }

    ScopedVirtAssignmobHook(const ScopedVirtAssignmobHook&) = delete;
    ScopedVirtAssignmobHook& operator=(const ScopedVirtAssignmobHook&) = delete;
};

// Same shape as ScopedVirtAssignmobHook above, for script_hooks.h's
// find_action accessor hook (Cluster B wave Task 1; cb-task-1-brief.md
// Step 6; cb-census.md section 5.4 -- the one seam beyond the brief's
// explicit T1 list). Unlike the abort-tripwire hooks above, this one is
// SAFE-SENTINEL class, so both halves are testable.
class ScopedFindActionHook {
public:
    explicit ScopedFindActionHook(rots::script::find_action_fn hook)
    {
        rots::script::set_find_action_hook(hook);
    }

    ~ScopedFindActionHook() { register_find_action_hook(); }

    ScopedFindActionHook(const ScopedFindActionHook&) = delete;
    ScopedFindActionHook& operator=(const ScopedFindActionHook&) = delete;
};

struct RecordedCommandInterpreterCall {
    char_data* ch = nullptr;
    char* argument_chr = nullptr;
    waiting_type* argument_wtl = nullptr;
    bool called = false;
};

RecordedCommandInterpreterCall g_recorded_command_interpreter_call;

void recording_command_interpreter_stub(char_data* ch, char* argument_chr,
    waiting_type* argument_wtl)
{
    g_recorded_command_interpreter_call = RecordedCommandInterpreterCall { ch, argument_chr, argument_wtl, true };
}

struct RecordedPersCall {
    char_data* target = nullptr;
    char_data* observer = nullptr;
    int capitalize = 0;
    int force_visible = 0;
    bool called = false;
};

RecordedPersCall g_recorded_pers_call;
char g_pers_stub_name[] = "Stubbed Name";

char* recording_pers_stub(char_data* target, char_data* observer, int capitalize,
    int force_visible)
{
    g_recorded_pers_call = RecordedPersCall { target, observer, capitalize, force_visible, true };
    return g_pers_stub_name;
}

struct RecordedVirtProgramNumberCall {
    int number = 0;
    bool called = false;
};

RecordedVirtProgramNumberCall g_recorded_virt_program_number_call;
int g_virt_program_number_stub_marker = 0;

void* recording_virt_program_number_stub(int number)
{
    g_recorded_virt_program_number_call = RecordedVirtProgramNumberCall { number, true };
    return &g_virt_program_number_stub_marker;
}

struct RecordedVirtAssignmobCall {
    char_data* mob = nullptr;
    bool called = false;
};

RecordedVirtAssignmobCall g_recorded_virt_assignmob_call;

void recording_virt_assignmob_stub(char_data* mob)
{
    g_recorded_virt_assignmob_call = RecordedVirtAssignmobCall { mob, true };
}

struct RecordedFindActionCall {
    char* arg = nullptr;
    bool called = false;
};

RecordedFindActionCall g_recorded_find_action_call;

int recording_find_action_stub(char* arg)
{
    g_recorded_find_action_call = RecordedFindActionCall { arg, true };
    return 5;
}

} // namespace

// dispatch_command_interpreter() -- DISCRIMINATOR: a recording stub proves
// the dispatch wrapper forwards ch/argument_chr/argument_wtl intact.

TEST(CommandInterpreterHook, DispatchReachesARegisteredStubWithAllArgsIntact)
{
    g_recorded_command_interpreter_call = RecordedCommandInterpreterCall { };
    ScopedCommandInterpreterHook scoped(recording_command_interpreter_stub);
    char_data ch { };
    char argument[] = "";
    waiting_type wtl { };

    rots::script::dispatch_command_interpreter(&ch, argument, &wtl);

    EXPECT_TRUE(g_recorded_command_interpreter_call.called)
        << "Expected the registered stub to have been reached.";
    EXPECT_EQ(g_recorded_command_interpreter_call.ch, &ch);
    EXPECT_EQ(g_recorded_command_interpreter_call.argument_chr, argument);
    EXPECT_EQ(g_recorded_command_interpreter_call.argument_wtl, &wtl);
}

TEST(CommandInterpreterHook, DispatchDefaultsToALoggedNoOpWhenUnregistered)
{
    g_recorded_command_interpreter_call = RecordedCommandInterpreterCall { };
    ScopedCommandInterpreterHook unregistered(nullptr);
    char_data ch { };
    char argument[] = "";
    waiting_type wtl { };

    rots::script::dispatch_command_interpreter(&ch, argument, &wtl);

    EXPECT_FALSE(g_recorded_command_interpreter_call.called)
        << "Expected an unregistered command-interpreter hook to leave the (unrelated) stub's own "
           "recording flag untouched -- the real forwarder never ran, and the tripwire default is "
           "a logged no-op, not a call to any stub.";
}

// dispatch_pers() -- DISCRIMINATOR: a recording stub proves the dispatch
// wrapper forwards target/observer/capitalize/force_visible intact and
// returns the stub's own pointer.

TEST(PersHook, DispatchReachesARegisteredStubWithAllArgsIntactAndForwardsReturnValue)
{
    g_recorded_pers_call = RecordedPersCall { };
    ScopedPersHook scoped(recording_pers_stub);
    char_data target { };
    char_data observer { };

    char* result = rots::script::dispatch_pers(&target, &observer, 1, 0);

    EXPECT_TRUE(g_recorded_pers_call.called)
        << "Expected the registered stub to have been reached.";
    EXPECT_EQ(g_recorded_pers_call.target, &target);
    EXPECT_EQ(g_recorded_pers_call.observer, &observer);
    EXPECT_EQ(g_recorded_pers_call.capitalize, 1);
    EXPECT_EQ(g_recorded_pers_call.force_visible, 0);
    EXPECT_EQ(result, g_pers_stub_name)
        << "Expected dispatch_pers() to forward the stub's own return value.";
}

// dispatch_virt_program_number() -- DISCRIMINATOR: a recording stub proves
// the dispatch wrapper forwards `number` intact and returns the stub's own
// pointer. No death test for the unregistered (abort-tripwire) default,
// same no-death-test convention as dispatch_pers() above.

TEST(VirtProgramNumberHook, DispatchReachesARegisteredStubWithArgIntactAndForwardsReturnValue)
{
    g_recorded_virt_program_number_call = RecordedVirtProgramNumberCall { };
    ScopedVirtProgramNumberHook scoped(recording_virt_program_number_stub);

    void* result = rots::script::dispatch_virt_program_number(7);

    EXPECT_TRUE(g_recorded_virt_program_number_call.called)
        << "Expected the registered stub to have been reached.";
    EXPECT_EQ(g_recorded_virt_program_number_call.number, 7);
    EXPECT_EQ(result, &g_virt_program_number_stub_marker)
        << "Expected dispatch_virt_program_number() to forward the stub's own return value.";
}

// dispatch_virt_assignmob() (Cluster B wave Task 1; cb-task-1-brief.md
// Step 2; cb-census.md section 5.2) -- DISCRIMINATOR: a recording stub
// proves the dispatch wrapper forwards `mob` intact. No death test for the
// unregistered (abort-tripwire) default, same no-death-test convention as
// dispatch_pers()/dispatch_virt_program_number() above.

TEST(VirtAssignmobHook, DispatchReachesARegisteredStubWithArgIntact)
{
    g_recorded_virt_assignmob_call = RecordedVirtAssignmobCall {};
    ScopedVirtAssignmobHook scoped(recording_virt_assignmob_stub);
    char_data mob {};

    rots::script::dispatch_virt_assignmob(&mob);

    EXPECT_TRUE(g_recorded_virt_assignmob_call.called)
        << "Expected the registered stub to have been reached.";
    EXPECT_EQ(g_recorded_virt_assignmob_call.mob, &mob);
}

// dispatch_find_action() (Cluster B wave Task 1; cb-task-1-brief.md
// Step 6; cb-census.md section 5.4) -- DISCRIMINATOR: a recording stub
// proves the dispatch wrapper forwards `arg` intact and returns the stub's
// own value; the unregistered default is a SAFE SENTINEL (-1,
// find_action()'s own "not found" return value), so BOTH halves are
// testable, unlike this file's abort-tripwire cells above.

TEST(FindActionHook, DispatchReachesARegisteredStubWithArgIntactAndForwardsReturnValue)
{
    g_recorded_find_action_call = RecordedFindActionCall {};
    ScopedFindActionHook scoped(recording_find_action_stub);
    char argument_text[] = "smile";

    const int result = rots::script::dispatch_find_action(argument_text);

    EXPECT_TRUE(g_recorded_find_action_call.called)
        << "Expected the registered stub to have been reached.";
    EXPECT_EQ(g_recorded_find_action_call.arg, argument_text);
    EXPECT_EQ(result, 5) << "Expected dispatch_find_action() to forward the stub's own return value.";
}

TEST(FindActionHook, DispatchDefaultsToSafeSentinelNegativeOneWhenUnregistered)
{
    ScopedFindActionHook unregistered(nullptr);
    char argument_text[] = "smile";

    const int result = rots::script::dispatch_find_action(argument_text);

    EXPECT_EQ(result, -1)
        << "Expected an unregistered find_action hook to default to -1 -- find_action()'s own "
           "\"not found\" sentinel -- matching script.cpp's one call site's existing != -1 "
           "validity guard exactly.";
}
