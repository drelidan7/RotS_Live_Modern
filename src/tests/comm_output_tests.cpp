#include "../comm.h"
#include "../structs.h"
#include "test_char_cleanup.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <string_view>

extern descriptor_data* descriptor_list;

namespace {

class ScopedDescriptorListReset {
public:
    ScopedDescriptorListReset()
        : previous_descriptor_list_(descriptor_list)
    {
        descriptor_list = nullptr;
    }

    ~ScopedDescriptorListReset()
    {
        descriptor_list = previous_descriptor_list_;
    }

    ScopedDescriptorListReset(const ScopedDescriptorListReset&) = delete;
    ScopedDescriptorListReset& operator=(const ScopedDescriptorListReset&) = delete;

private:
    // Restores the process-global descriptor chain after an isolated test.
    descriptor_data* previous_descriptor_list_;
};

void reset_capturing_descriptor(descriptor_data& descriptor, char_data* character)
{
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = 0;
    descriptor.character = character;
}

struct ConnectedCharacterContext {
    // Receives messages through the character-pointer and character-ID APIs.
    char_data character {};
    // Captures queued output without opening a network connection.
    descriptor_data descriptor {};

    ConnectedCharacterContext()
    {
        reset_capturing_descriptor(descriptor, &character);
        character.desc = &descriptor;
        character.abs_number = 4207;
    }
};

} // namespace

TEST(CommOutput, SendToCharNullPointerRemainsANoOp)
{
    ConnectedCharacterContext context;

    send_to_char(static_cast<const char*>(nullptr), &context.character);

    EXPECT_STREQ(context.descriptor.output, "");
}

TEST(CommOutput, WriteToOutputAcceptsANonNullTerminatedSlice)
{
    descriptor_data descriptor {};
    reset_capturing_descriptor(descriptor, nullptr);
    const char storage[] = { 'x', 'v', 'i', 'e', 'w', 'y' };

    write_to_output(std::string_view(storage + 1, 4), &descriptor);

    EXPECT_STREQ(descriptor.output, "view");
    EXPECT_EQ(descriptor.bufptr, 4);
    EXPECT_EQ(descriptor.bufspace, SMALL_BUFSIZE - 5);
}

TEST(CommOutput, WriteToOutputTruncatesAtAnEmbeddedNull)
{
    descriptor_data descriptor {};
    reset_capturing_descriptor(descriptor, nullptr);
    const char storage[] = { 'o', 'k', '\0', 'n', 'o' };

    write_to_output(std::string_view(storage, sizeof(storage)), &descriptor);

    EXPECT_STREQ(descriptor.output, "ok");
    EXPECT_EQ(descriptor.bufptr, 2);
}

TEST(CommOutput, WriteToOutputUsesTheLastAvailableSmallBufferBytes)
{
    descriptor_data descriptor {};
    reset_capturing_descriptor(descriptor, nullptr);
    descriptor.bufspace = 4;

    write_to_output(std::string_view("view"), &descriptor);

    EXPECT_STREQ(descriptor.output, "view");
    EXPECT_EQ(descriptor.bufptr, 4);
    EXPECT_EQ(descriptor.bufspace, 0);
    EXPECT_EQ(descriptor.output[4], '\0');
}

TEST(CommOutput, WriteToOutputPromotesToTheLargeBufferUsingViewLength)
{
    descriptor_data descriptor {};
    ScopedDescriptorLargeOutbufReturn descriptor_large_outbuf_cleanup { descriptor };
    reset_capturing_descriptor(descriptor, nullptr);
    std::memcpy(descriptor.small_outbuf, "pre", 4);
    descriptor.bufptr = 3;
    descriptor.bufspace = 2;

    write_to_output(std::string_view("view"), &descriptor);

    ASSERT_NE(descriptor.large_outbuf, nullptr);
    EXPECT_STREQ(descriptor.output, "preview");
    EXPECT_EQ(descriptor.bufptr, 7);
    EXPECT_EQ(descriptor.bufspace, LARGE_BUFSIZE - 8);
}

TEST(CommOutput, SendToCharAcceptsAViewForACharacterPointer)
{
    ConnectedCharacterContext context;
    const std::string storage = "prefix-message-suffix";

    send_to_char(std::string_view(storage).substr(7, 7), &context.character);

    EXPECT_STREQ(context.descriptor.output, "message");
}

TEST(CommOutput, SendToCharIgnoresAnEmptyView)
{
    ConnectedCharacterContext context;

    send_to_char(std::string_view(), &context.character);

    EXPECT_STREQ(context.descriptor.output, "");
}

TEST(CommOutput, SendToCharTruncatesAViewAtAnEmbeddedNull)
{
    ConnectedCharacterContext context;
    const char storage[] = { 'o', 'k', '\0', 'n', 'o' };

    send_to_char(std::string_view(storage, sizeof(storage)), &context.character);

    EXPECT_STREQ(context.descriptor.output, "ok");
}

TEST(CommOutput, SendToCharAcceptsAViewForACharacterId)
{
    ScopedDescriptorListReset descriptor_list_reset;
    ConnectedCharacterContext context;
    descriptor_list = &context.descriptor;
    const std::string storage = "prefix-message-suffix";

    send_to_char(std::string_view(storage).substr(7, 7), context.character.abs_number);

    EXPECT_STREQ(context.descriptor.output, "message");
}
