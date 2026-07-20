// persist_hooks_tests.cpp

// New test TU (combat-pilot wave, Task 5; brief
// .superpowers/sdd/pilot-task-5-brief.md, discriminator audit step). Covers
// persist_hooks.h's dispatch_exploit_capture() dispatch entry point, which
// this task gave EXTERNAL linkage (db_players.cpp previously wrapped it in
// an anonymous namespace alongside dispatch_room_vnum(), since only
// db_players.cpp's own rename_char() called it) so fight.cpp's four
// add_exploit_record() call sites could route through it once fight.cpp
// joins rots_combat. The audit found every OTHER combat-pilot hook already
// had a registered/unregistered discriminator pair (combat_hooks_tests.cpp,
// big_brother_hooks_tests.cpp) except this one -- it was consumer-free
// (db_players.cpp-internal only) until this task, so nothing had exercised
// its dispatch wrapper directly before. Same "recording stub proves the
// dispatch wrapper forwards every argument intact; unregistered hits the
// documented tripwire default" shape as big_brother_hooks_tests.cpp's
// TargetValidHook/CharacterDiedHook suites.
//
// HYGIENE: gtest_main.cpp registers db_boot.cpp's REAL add_exploit_record()
// forwarder (register_exploit_capture_hook()) for the whole test binary.
// Every test below that swaps in a recording stub (or nullptr) restores the
// real registration on scope exit via ScopedExploitCaptureHook, mirroring
// ScopedTargetValidHook's/ScopedCharacterDiedHook's restore-via-real-
// registrar shape.

#include "../db.h"
#include "../persist_hooks.h"
#include "rots/core/character.h"

#include <gtest/gtest.h>

namespace {

// Swaps persist_hooks.h's exploit-capture hook, then restores the REAL
// db_boot.cpp forwarder via register_exploit_capture_hook() on destruction
// -- there is no per-hook "restore just this one" entry point, same
// rationale as big_brother_hooks_tests.cpp's Scoped* fixtures.
class ScopedExploitCaptureHook {
public:
    explicit ScopedExploitCaptureHook(rots::persist::exploit_capture_fn hook)
    {
        rots::persist::set_exploit_capture_hook(hook);
    }

    ~ScopedExploitCaptureHook() { register_exploit_capture_hook(); }

    ScopedExploitCaptureHook(const ScopedExploitCaptureHook&) = delete;
    ScopedExploitCaptureHook& operator=(const ScopedExploitCaptureHook&) = delete;
};

struct RecordedExploitCaptureCall {
    int record_type = 0;
    char_data* victim = nullptr;
    int int_param = 0;
    const char* extra = nullptr;
    bool called = false;
};

RecordedExploitCaptureCall g_recorded_exploit_capture_call;

void recording_exploit_capture_stub(int record_type, char_data* victim, int int_param, const char* extra)
{
    g_recorded_exploit_capture_call = RecordedExploitCaptureCall { record_type, victim, int_param, extra, true };
}

} // namespace

// dispatch_exploit_capture() -- DISCRIMINATOR: a recording stub proves the
// dispatch wrapper forwards record_type/victim/int_param/extra intact, the
// exact shape fight.cpp's four add_exploit_record() call sites now use
// (EXPLOIT_MOBDEATH/EXPLOIT_POISON/EXPLOIT_PK/EXPLOIT_DEATH).

TEST(ExploitCaptureHook, DispatchReachesARegisteredStubWithArgsIntact)
{
    g_recorded_exploit_capture_call = RecordedExploitCaptureCall {};
    ScopedExploitCaptureHook scoped(recording_exploit_capture_stub);
    char_data victim {};

    rots::persist::dispatch_exploit_capture(EXPLOIT_PK, &victim, 42, "extra-note");

    EXPECT_TRUE(g_recorded_exploit_capture_call.called)
        << "Expected the registered stub to have been reached.";
    EXPECT_EQ(g_recorded_exploit_capture_call.record_type, EXPLOIT_PK);
    EXPECT_EQ(g_recorded_exploit_capture_call.victim, &victim);
    EXPECT_EQ(g_recorded_exploit_capture_call.int_param, 42);
    EXPECT_STREQ(g_recorded_exploit_capture_call.extra, "extra-note");
}

TEST(ExploitCaptureHook, DispatchDefaultsToANoOpWhenUnregistered)
{
    g_recorded_exploit_capture_call = RecordedExploitCaptureCall {};
    ScopedExploitCaptureHook unregistered(nullptr);
    char_data victim {};

    rots::persist::dispatch_exploit_capture(EXPLOIT_PK, &victim, 42, "extra-note");

    EXPECT_FALSE(g_recorded_exploit_capture_call.called)
        << "Expected an unregistered exploit-capture hook to leave the (unrelated) stub's own "
           "recording flag untouched -- the real forwarder never ran, and the tripwire default is "
           "a logged no-op, not a call to any stub.";
}
