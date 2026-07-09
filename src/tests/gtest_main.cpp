#include <gtest/gtest.h>

#include "../rots_net.h"

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
    ::testing::InitGoogleTest(&argc, argv);
    const int result = RUN_ALL_TESTS();
    rots_net::shutdown();
    return result;
}
#endif
