// Capturing-sink tests for the platform logging seam (rots/platform/log.h,
// spec §13, logging-seam plan Task 4). These exercise rots::log::set_sink/
// write_stderr/write plus the game-facing mudlog()/vmudlog() wrappers purely
// against the platform layer -- no descriptor_list/game state needed, unlike
// utility_format_tests.cpp's broadcast-sink pins (which register the real
// app sink via register_mudlog_broadcast_sink() and assert on a listening
// descriptor's output buffer).
//
// ScopedLogSink below is also this task's reference mitigation for the
// Task 2 review's Minor finding: once any test in this binary calls
// register_mudlog_broadcast_sink() (utility_format_tests.cpp does, several
// times), the production sink stays installed process-wide for every test
// that runs afterward in the same binary -- set_sink() has no "unset", only
// swap. ScopedLogSink captures whatever sink is live when it is constructed
// (the production sink, another test's capturing sink, or none at all) and
// restores exactly that on destruction, so this file's tests observe only
// their own capturing lambda regardless of what ran before them, and leave
// the previously-installed sink exactly as they found it for whatever test
// runs next.

#include "../utils.h"
#include "rots/platform/log.h"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

// One notification captured by a ScopedLogSink's lambda: the raw
// (unformatted, unclamped) message body plus the mudlog channel type and
// level the sink was invoked with -- mirrors rots::log::Sink's parameter
// list so a test can assert on exactly what a caller (mudlog()/vmudlog())
// handed the sink.
struct CapturedLog {
    std::string message;
    char type;
    int level;
};

// Installs a capturing lambda as the process-wide log sink for the guard's
// lifetime and restores whatever sink was previously installed when the
// guard is destroyed (set_sink's chain-restore contract) -- see the file
// header comment for why this doubles as the fix for cross-test sink
// leakage in this binary.
class ScopedLogSink {
public:
    ScopedLogSink()
        : previous_(rots::log::set_sink([this](std::string_view msg, char type, int level) {
            calls_.push_back(CapturedLog { std::string(msg), type, level });
        }))
    {
    }

    ~ScopedLogSink() { rots::log::set_sink(std::move(previous_)); }

    ScopedLogSink(const ScopedLogSink&) = delete;
    ScopedLogSink& operator=(const ScopedLogSink&) = delete;

    // Notifications this guard's capturing lambda has recorded so far, in
    // call order.
    const std::vector<CapturedLog>& calls() const { return calls_; }

private:
    // The sink installed before this guard ran (possibly empty, possibly
    // the production broadcast sink another test left registered);
    // reinstated verbatim in the destructor regardless of what a test does
    // to the sink slot in between.
    rots::log::Sink previous_;
    // Notifications recorded by this guard's capturing lambda. Owned here
    // (rather than a free-standing static) so the lambda can safely capture
    // `this` -- the guard's address is stable for its whole lifetime -- and
    // so each test gets its own fresh, empty capture list.
    std::vector<CapturedLog> calls_;
};

} // namespace

TEST(PlatformLog, MudlogNotifiesSinkOnceWithRawBodyTypeAndLevel)
{
    ScopedLogSink sink;

    // file=FALSE: skip the stderr file-write branch so this test's output is
    // just the sink notification, per the brief's "prefer file=FALSE where
    // output cleanliness matters" guidance.
    mudlog("test: mudlog notify", BRF, LEVEL_GOD, FALSE);

    ASSERT_EQ(sink.calls().size(), 1u);
    EXPECT_EQ(sink.calls()[0].message, "test: mudlog notify");
    EXPECT_EQ(sink.calls()[0].type, BRF);
    EXPECT_EQ(sink.calls()[0].level, LEVEL_GOD);
}

TEST(PlatformLog, MudlogWithNegativeLevelSkipsSinkNotification)
{
    ScopedLogSink sink;

    // level < 0 is mudlog's file-only path: rots::log::write() returns right
    // after the (skipped, file=FALSE here) stderr branch, before ever
    // touching the sink.
    mudlog("test: negative level is file-only", BRF, -1, FALSE);

    EXPECT_TRUE(sink.calls().empty());
}

TEST(PlatformLog, VmudlogNotifiesSinkWithFormattedTextAtLevelGod)
{
    ScopedLogSink sink;

    // vmudlog always calls rots::log::write(..., kVmudlogBroadcastLevel,
    // true) -- this both proves the formatted text reaches the sink and,
    // via the LEVEL_GOD comparison, proves end-to-end that
    // kVmudlogBroadcastLevel (static_asserted in utility.cpp) really is
    // LEVEL_GOD at the call site, not just at the assert.
    vmudlog(BRF, "fmt %d", 7);

    ASSERT_EQ(sink.calls().size(), 1u);
    EXPECT_EQ(sink.calls()[0].message, "fmt 7");
    EXPECT_EQ(sink.calls()[0].type, BRF);
    EXPECT_EQ(sink.calls()[0].level, LEVEL_GOD);
}

TEST(PlatformLog, SetSinkReturnsPreviousSinkForChainRestore)
{
    ScopedLogSink outer;

    bool inner_called = false;
    auto inner_lambda = [&inner_called](std::string_view, char, int) { inner_called = true; };

    // set_sink() must hand back whatever was installed before this call --
    // outer's capturing lambda -- not an empty std::function and not
    // inner_lambda itself.
    rots::log::Sink returned = rots::log::set_sink(inner_lambda);
    ASSERT_TRUE(static_cast<bool>(returned));

    // Invoke the returned sink directly: if it really is outer's original
    // lambda, this appends to outer's own capture list (bypassing whatever
    // is currently installed, i.e. inner_lambda) instead of setting
    // inner_called.
    returned("chain-restore probe", BRF, LEVEL_GOD);
    EXPECT_FALSE(inner_called);
    ASSERT_EQ(outer.calls().size(), 1u);
    EXPECT_EQ(outer.calls()[0].message, "chain-restore probe");

    // outer's destructor restores whatever sink was live before THIS test
    // touched the slot, regardless of inner_lambda being currently
    // installed -- no manual cleanup needed here.
}

TEST(PlatformLog, WriteStderrNeverNotifiesSink)
{
    ScopedLogSink sink;

    // write_stderr() is the platform half of log(): a bare timestamped
    // stderr line with no type/level and no sink involvement at all, unlike
    // write()/mudlog().
    rots::log::write_stderr("test: write_stderr bypasses the sink");

    EXPECT_TRUE(sink.calls().empty());
}
