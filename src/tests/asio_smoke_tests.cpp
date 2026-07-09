// Phase 3 Task 1: proves the vendored standalone Asio (third_party/asio/,
// wired in via -I.../ASIO_STANDALONE in src/Makefile, src/tests/Makefile,
// and src/CMakeLists.txt) actually compiles and links in the test binary on
// every platform this suite runs on. Kept permanently (not a throwaway TU)
// as a fast, always-on guard against the include path or ASIO_STANDALONE
// define silently regressing in any of the four build paths.
//
// <climits> before <asio.hpp>: on the i386 container's GCC 10 / libstdc++,
// asio/detail/thread_info_base.hpp uses UCHAR_MAX without including <climits>
// itself, relying on a transitive include that AppleClang's libc++ and newer
// libstdc++ happen to provide but GCC 10 does not — so asio.hpp fails to
// compile there unless the consumer pulls <climits> in first. The vendored
// Asio copy is kept byte-for-byte upstream, so the include belongs here (and
// in any future TU that includes asio.hpp on that toolchain — Task 4's
// connection layer will do the same in comm.cpp).
#include <climits>

#include <asio.hpp>
#include <gtest/gtest.h>

TEST(AsioSmoke, IoContextConstructsAndRunsAPostedHandler)
{
    asio::io_context ctx;
    bool ran = false;

    asio::post(ctx, [&ran]() { ran = true; });

    // io_context starts with no work outstanding once the posted handler is
    // its only queued item, so run() executes that one handler and returns.
    std::size_t handlers_run = ctx.run();

    EXPECT_TRUE(ran);
    EXPECT_EQ(1u, handlers_run);
}
