// editor_hooks_tests.cpp

// New test TU (Cluster B wave Task 1; cb-task-1-brief.md Step 3;
// cb-census.md section 5.1). Covers editor_hooks.h's one hook:
// dispatch_string_editor_init() (the six shape*.cpp OLC editors' future
// string_add_init() call site). CONSUMER-FREE this task -- no shape*.cpp
// call site converts yet -- so the hook is exercised directly against a
// recording stub, the same "registered stub receives args intact; no death
// test for the abort-tripwire unregistered default" discriminator shape
// script_hooks_tests.cpp's PersHook/VirtProgramNumberHook/
// VirtAssignmobHook suites establish for this wave's sibling seams.
//
// HYGIENE: gtest_main.cpp registers modify.cpp's REAL forwarder
// (register_string_editor_init_hook()) for the whole test binary. The one
// test below that swaps in a recording stub restores the real registration
// on scope exit via ScopedStringEditorInitHook, mirroring this tree's other
// *_hooks_tests.cpp Scoped* fixtures.
//
// No death test for dispatch_string_editor_init()'s unregistered
// (abort-tripwire) default, per this suite's no-death-test convention --
// script_hooks_tests.cpp's dispatch_pers()/dispatch_virt_program_number()/
// dispatch_virt_assignmob() document the same "untested by design" posture
// for their own abort tripwires; the registered path below is this hook's
// discriminator coverage.

#include "../editor_hooks.h"
#include "rots/core/descriptor.h"

#include <gtest/gtest.h>

// modify.cpp has no dedicated header (see editor_hooks.h's own comment);
// forward-declared locally here, mirroring comm.cpp's/gtest_main.cpp's own
// local declaration of this same registrar.
void register_string_editor_init_hook();

namespace {

// Swaps editor_hooks.h's string-editor-init hook, then restores the REAL
// modify.cpp forwarder via register_string_editor_init_hook() on
// destruction -- there is no per-hook "restore just this one" entry point,
// same rationale as this tree's other Scoped* fixtures.
class ScopedStringEditorInitHook {
public:
    explicit ScopedStringEditorInitHook(rots::editor::string_editor_init_fn hook)
    {
        rots::editor::set_string_editor_init_hook(hook);
    }

    ~ScopedStringEditorInitHook() { register_string_editor_init_hook(); }

    ScopedStringEditorInitHook(const ScopedStringEditorInitHook&) = delete;
    ScopedStringEditorInitHook& operator=(const ScopedStringEditorInitHook&) = delete;
};

struct RecordedStringEditorInitCall {
    descriptor_data* d = nullptr;
    char** str = nullptr;
    bool called = false;
};

RecordedStringEditorInitCall g_recorded_string_editor_init_call;

void recording_string_editor_init_stub(descriptor_data* d, char** str)
{
    g_recorded_string_editor_init_call = RecordedStringEditorInitCall { d, str, true };
}

} // namespace

// dispatch_string_editor_init() -- DISCRIMINATOR: a recording stub proves
// the dispatch wrapper forwards d/str intact.

TEST(StringEditorInitHook, DispatchReachesARegisteredStubWithArgsIntact)
{
    g_recorded_string_editor_init_call = RecordedStringEditorInitCall {};
    ScopedStringEditorInitHook scoped(recording_string_editor_init_stub);
    descriptor_data descriptor {};
    char* str = nullptr;

    rots::editor::dispatch_string_editor_init(&descriptor, &str);

    EXPECT_TRUE(g_recorded_string_editor_init_call.called)
        << "Expected the registered stub to have been reached.";
    EXPECT_EQ(g_recorded_string_editor_init_call.d, &descriptor);
    EXPECT_EQ(g_recorded_string_editor_init_call.str, &str);
}
