#include "../rots_net.h"

#include <cstring>
#include <gtest/gtest.h>

#if defined(PREDEF_PLATFORM_LINUX)
#include <sys/socket.h>
#endif

namespace {

// A connected loopback TCP pair, built with socketpair() on POSIX. rots_net has
// no POSIX-specific test-only surface, so the pair is assembled locally with
// raw BSD calls -- this is test scaffolding, not part of the shim itself.
#if defined(PREDEF_PLATFORM_LINUX)
struct LoopbackSocketPair
{
    LoopbackSocketPair()
    {
        int raw_pair[2] = { -1, -1 };
        const int result = socketpair(AF_UNIX, SOCK_STREAM, 0, raw_pair);
        EXPECT_EQ(result, 0) << "socketpair() setup failed";
        first = raw_pair[0];
        second = raw_pair[1];
    }

    ~LoopbackSocketPair()
    {
        if (rots_net::is_valid_socket(first))
        {
            rots_net::close_socket(first);
        }
        if (rots_net::is_valid_socket(second))
        {
            rots_net::close_socket(second);
        }
    }

    // One end of the connected pair; tests read/write/configure this handle and
    // the destructor closes it unless a test hands ownership away by resetting
    // it to kInvalidSocket.
    SocketType first;
    // The opposite end of the same pair, used as the peer for round-trip
    // traffic; closed by the destructor under the same ownership rule.
    SocketType second;
};
#endif

} // namespace

TEST(RotsNet, InvalidSocketSentinelIsNotValid)
{
    EXPECT_FALSE(rots_net::is_valid_socket(rots_net::kInvalidSocket));
}

TEST(RotsNet, ZeroIsAValidSocketHandle)
{
    // Descriptor/handle 0 is a legitimate (if unusual) socket value; only the
    // dedicated sentinel means "no socket". comm.cpp separately uses 0 as its
    // own "unset descriptor field" convention -- that is a comm.cpp-level
    // policy, not something rots_net::is_valid_socket should bake in.
    EXPECT_TRUE(rots_net::is_valid_socket(static_cast<SocketType>(0)));
}

TEST(RotsNet, StartupAndShutdownAreIdempotentOnRepeatedCalls)
{
    // No-ops on POSIX; must never crash or leave state that breaks a second
    // call. Windows' WSAStartup/WSACleanup are themselves refcounted, so this
    // is meaningful there too once the preset goes green.
    rots_net::startup();
    rots_net::startup();
    rots_net::shutdown();
    rots_net::shutdown();
}

#if defined(PREDEF_PLATFORM_LINUX)

TEST(RotsNet, SetNonblockingThenEmptyReadWouldBlock)
{
    LoopbackSocketPair pair;
    ASSERT_TRUE(rots_net::is_valid_socket(pair.first));
    ASSERT_TRUE(rots_net::is_valid_socket(pair.second));

    rots_net::set_nonblocking(pair.first);

    char buffer[16];
    const rots_net::ssize_type result = rots_net::read_socket(pair.first, buffer, sizeof(buffer));

    // Nothing was ever written to pair.second, so a nonblocking read on pair.first
    // must fail immediately (never block the test) and classify as "would block".
    EXPECT_LT(result, 0);
    EXPECT_TRUE(rots_net::error_is_would_block(rots_net::last_error()));
    EXPECT_FALSE(rots_net::error_is_interrupted(rots_net::last_error()));
}

TEST(RotsNet, WriteThenReadRoundTripsBytes)
{
    LoopbackSocketPair pair;
    ASSERT_TRUE(rots_net::is_valid_socket(pair.first));
    ASSERT_TRUE(rots_net::is_valid_socket(pair.second));

    const char message[] = "rots_net round-trip";
    const rots_net::ssize_type written = rots_net::write_socket(pair.first, message, sizeof(message));
    ASSERT_EQ(written, static_cast<rots_net::ssize_type>(sizeof(message)));

    char received[sizeof(message)] = { 0 };
    rots_net::ssize_type total_read = 0;
    while (total_read < static_cast<rots_net::ssize_type>(sizeof(message)))
    {
        const rots_net::ssize_type read_now = rots_net::read_socket(
            pair.second, received + total_read, sizeof(received) - total_read);
        ASSERT_GT(read_now, 0);
        total_read += read_now;
    }

    EXPECT_EQ(0, std::memcmp(message, received, sizeof(message)));
}

TEST(RotsNet, CloseSocketIsValidOnAFreshlyOpenedDescriptorAndIdempotentIsNotAssumed)
{
    LoopbackSocketPair pair;
    ASSERT_TRUE(rots_net::is_valid_socket(pair.first));

    // A single close() on a still-open, valid descriptor must succeed (0).
    const int close_result = rots_net::close_socket(pair.first);
    EXPECT_EQ(close_result, 0);

    // Consumed by the explicit close above; tell the fixture destructor not to
    // close it again (POSIX close() on an already-closed fd is UB territory,
    // not something rots_net needs to paper over).
    pair.first = rots_net::kInvalidSocket;
}

TEST(RotsNet, LastErrorReflectsTheMostRecentFailedCall)
{
    LoopbackSocketPair pair;
    ASSERT_TRUE(rots_net::is_valid_socket(pair.first));
    rots_net::set_nonblocking(pair.first);

    char buffer[4];
    const rots_net::ssize_type result = rots_net::read_socket(pair.first, buffer, sizeof(buffer));
    ASSERT_LT(result, 0);
    EXPECT_TRUE(rots_net::error_is_would_block(rots_net::last_error()));
}

#endif // PREDEF_PLATFORM_LINUX

TEST(RotsNet, ResolveHostNameFailsGracefullyOnObviouslyUnroutableAddress)
{
    // 0.0.0.0 -> never reverse-resolves to a name; must return false and never
    // crash. This keeps populate_descriptor_host's fallback-to-numeric path
    // exercised without depending on real DNS/nsswitch behavior in CI.
    char host_buffer[256] = { 0 };
    const bool resolved = rots_net::resolve_host_name(0u, host_buffer, sizeof(host_buffer));
    // Whether or not the local resolver has an opinion about 0.0.0.0 varies by
    // host, so only assert the contract that matters: on failure the buffer is
    // left alone (no garbage/crash), and the call returns promptly either way.
    if (!resolved)
    {
        EXPECT_STREQ(host_buffer, "");
    }
}
