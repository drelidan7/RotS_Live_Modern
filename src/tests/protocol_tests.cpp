#include "../protocol.h"
#include "../structs.h"

// IAC/DO/WILL/TELOPT_TTYPE: from <arpa/telnet.h> on POSIX; that header does
// not exist on Windows, where platdef.h hand-declares the same fixed RFC
// 854/1091 byte values instead (Phase 3 Task 5's stand-in, used here in
// Task 6). structs.h above already pulls in platdef.h.
#if !defined(_WIN32)
#include <arpa/telnet.h>
#endif

#include <gtest/gtest.h>

#include <cstring>
#include <string>

namespace {

descriptor_data make_descriptor()
{
    descriptor_data descriptor {};
    descriptor.pProtocol = ProtocolCreate();
    return descriptor;
}

TEST(ProtocolInput, IgnoresLoneIacByteOnFreshDescriptor)
{
    descriptor_data descriptor = make_descriptor();
    char output[MAX_INPUT_LENGTH] = { '\0' };
    char input[] = { static_cast<char>(IAC) };

    ProtocolInput(&descriptor, input, 1, output);

    EXPECT_STREQ(output, "");

    ProtocolDestroy(descriptor.pProtocol);
}

TEST(ProtocolInput, IgnoresTruncatedHandshakeVerbWithoutReadingPastBuffer)
{
    descriptor_data descriptor = make_descriptor();
    char output[MAX_INPUT_LENGTH] = { '\0' };
    char input[] = { static_cast<char>(IAC), static_cast<char>(DO) };

    ProtocolInput(&descriptor, input, 2, output);

    EXPECT_STREQ(output, "");
    EXPECT_EQ(descriptor.pProtocol->PendingInputLength, 2);

    ProtocolDestroy(descriptor.pProtocol);
}

TEST(ProtocolInput, CompletesSplitHandshakeAcrossCallsWithoutLeakingTelnetBytes)
{
    descriptor_data descriptor = make_descriptor();
    char output[MAX_INPUT_LENGTH] = { '\0' };
    char partial[] = { static_cast<char>(IAC), static_cast<char>(WILL) };
    char option[] = { static_cast<char>(TELOPT_TTYPE) };

    ProtocolInput(&descriptor, partial, 2, output);

    EXPECT_STREQ(output, "");
    EXPECT_EQ(descriptor.pProtocol->PendingInputLength, 2);

    output[0] = '\0';
    ProtocolInput(&descriptor, option, 1, output);

    EXPECT_STREQ(output, "");
    EXPECT_EQ(descriptor.pProtocol->PendingInputLength, 0);
    EXPECT_TRUE(descriptor.pProtocol->bTTYPE);

    ProtocolDestroy(descriptor.pProtocol);
}

TEST(ProtocolInput, BuffersTruncatedMxpSequenceWithoutReadingPastBuffer)
{
    descriptor_data descriptor = make_descriptor();
    char output[MAX_INPUT_LENGTH] = { '\0' };
    std::string input = "\x1b[1";

    ProtocolInput(&descriptor, input.data(), static_cast<int>(input.size()), output);

    EXPECT_STREQ(output, "");
    EXPECT_EQ(descriptor.pProtocol->PendingInputLength, static_cast<int>(input.size()));

    ProtocolDestroy(descriptor.pProtocol);
}

TEST(ProtocolInput, BuffersLoneEscapeByteUntilMxpPrefixContinues)
{
    descriptor_data descriptor = make_descriptor();
    char output[MAX_INPUT_LENGTH] = { '\0' };
    std::string first = "\x1b";
    std::string second = "[1";

    ProtocolInput(&descriptor, first.data(), static_cast<int>(first.size()), output);

    EXPECT_STREQ(output, "");
    EXPECT_EQ(descriptor.pProtocol->PendingInputLength, static_cast<int>(first.size()));

    output[0] = '\0';
    ProtocolInput(&descriptor, second.data(), static_cast<int>(second.size()), output);

    EXPECT_STREQ(output, "");
    EXPECT_EQ(descriptor.pProtocol->PendingInputLength, 3);

    ProtocolDestroy(descriptor.pProtocol);
}

} // namespace
