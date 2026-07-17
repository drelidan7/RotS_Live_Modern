#include <gtest/gtest.h>

#include "../big_brother.h"
#include "../comm.h"
#include "../rots_net.h"
#include "../skill_timer.h"
#include "../utils.h"

#if defined(_WIN32)
#include <crtdbg.h>
#include <windows.h>

// Suppress every interactive Windows crash/assert dialog for the test process
// (Phase 3 Task 6). Without this, a test that faults (SEH access violation,
// CRT heap-corruption check, failed assert()) pops a modal "program has stopped
// working" / Debug-Assertion dialog and BLOCKS -- on a headless CI runner that
// hang lasts until ctest's per-test timeout fires (observed as spurious 120s
// "Timeout" results that individually pass locally but each cost two minutes,
// blowing the 30-minute job wall). Routing faults straight to process
// termination + stderr makes a genuine crash fail in milliseconds and report
// as a normal test failure, instead of masquerading as a timeout. No effect on
// a passing test.
static void silence_windows_crash_dialogs()
{
    // No GP-fault message box; fail critical-error prompts (missing media, etc.)
    // rather than prompting; don't let a faulting child re-enable the box.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

    // Route CRT assert/error/warning reports to stderr instead of a modal dialog.
    for (int report_type : { _CRT_WARN, _CRT_ERROR, _CRT_ASSERT }) {
        _CrtSetReportMode(report_type, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(report_type, _CRTDBG_FILE_STDERR);
    }

    // abort() (which failed assertions/terminate call) must not raise the
    // "Debug Error!" dialog either.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
}
#endif

#ifdef TESTING
int main(int argc, char* argv[]) {
#if defined(_WIN32)
    silence_windows_crash_dialogs();
#endif
    // Initialize Winsock once for the whole test process (Phase 3 Task 6). The
    // game does this in comm.cpp's main(), but the test harness has its own
    // main() -- without it, every socket()/bind()/connect() in the AcceptPathTest
    // loopback fixtures (startup_options_tests.cpp) fails immediately with
    // WSANOTINITIALISED and rots_net::is_valid_socket() reports the returned
    // INVALID_SOCKET. No-op on POSIX (rots_net::startup() is empty there).
    rots_net::startup();
    // Construct the process-wide skill_timer/big_brother singletons before any
    // test runs (Phase 5 T6, ASan/UBSan sweep). Production reaches these via
    // boot-time skill_timer::create()/big_brother::create() calls (db.cpp) that
    // this test harness's main() never runs; a test that transitively calls
    // fight.cpp's damage() or act_info.cpp's do_affections/do_info without them
    // hits world_singleton<T>::instance() returning `*m_pInstance` while
    // m_pInstance is still null -- a reference-binding-to-null-pointer UB that
    // UBSan flags (previously papered over per-suite by the act_info_format_tests.cpp
    // ensure_skill_timer_created() helper; doing it once here for the whole
    // process closes the gap for every OTHER suite, e.g. mage_tests.cpp's
    // MageProcTest, that also reaches damage() without realizing it needs this).
    // Each create()'s storage is a function-local static, so this is a one-time,
    // idempotent, harness-only bootstrap -- it does not model boot's real
    // weather_info/world wiring and has no effect on shipped game behavior.
    // FIRST-CALL-WINS (singleton.h's world_singleton<T>::create): `static T
    // theInstance(&weather, world)` inside create() is a function-local
    // static, constructed exactly once on the first call that reaches it;
    // every later create() call -- here or in any test that adds its own --
    // just re-points m_pInstance at that same already-constructed instance
    // and silently ignores whatever (weather, world) args it was passed. A
    // future world-booting test that calls create() expecting a *different*
    // room_data*/weather_data& to take effect will get this process's
    // first-ever args instead, with no error or warning.
    game_timer::skill_timer::create(weather_info, nullptr);
    game_rules::big_brother::create(weather_info, nullptr);
    // Installs comm.cpp's real send_to_char/act/track_specialized_mage/
    // untrack_specialized_mage bodies as the entity-seed Task 3 output seam's
    // sinks (rots::output::set_sinks), once for the whole test process. The
    // game reaches this via run_the_game() (immediately after
    // register_mudlog_broadcast_sink(), before boot_db()), which this test
    // harness's main() never runs; without it, every test that calls
    // send_to_char()/act() -- directly, or transitively through
    // register_mudlog_broadcast_sink()'s own send_to_char() calls -- would
    // silently no-op against output_seam.cpp's tripwire-logged default
    // instead of delivering to a descriptor, exactly the same gap this
    // function already closes for skill_timer/big_brother above.
    register_game_output_sinks();
    ::testing::InitGoogleTest(&argc, argv);
    const int result = RUN_ALL_TESTS();
    rots_net::shutdown();
    return result;
}
#endif
