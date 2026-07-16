#include "../comm.h"
#include "../rots_net.h"
#include "rots/core/character.h"
#include "rots/core/room.h"
#include "rots/core/descriptor.h"
#include "rots/core/types.h"
#include "test_char_cleanup.h"
#include "test_world.h"

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>

#if defined(PREDEF_PLATFORM_LINUX)
#include <sys/socket.h>
#endif

extern descriptor_data* descriptor_list;
void show_string(descriptor_data* descriptor, char* input);

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

#if defined(PREDEF_PLATFORM_LINUX)
struct LocalSocketPair {
    // Carries bytes written by write_to_descriptor to the peer socket.
    SocketType writer = rots_net::kInvalidSocket;
    // Receives bytes from the writer so tests can inspect exact write lengths.
    SocketType reader = rots_net::kInvalidSocket;

    LocalSocketPair()
    {
        int socket_handles[2] = { -1, -1 };
        const int result = socketpair(AF_UNIX, SOCK_STREAM, 0, socket_handles);
        EXPECT_EQ(result, 0) << "socketpair() setup failed";
        writer = socket_handles[0];
        reader = socket_handles[1];
    }

    ~LocalSocketPair()
    {
        if (rots_net::is_valid_socket(writer)) {
            rots_net::close_socket(writer);
        }
        if (rots_net::is_valid_socket(reader)) {
            rots_net::close_socket(reader);
        }
    }

    LocalSocketPair(const LocalSocketPair&) = delete;
    LocalSocketPair& operator=(const LocalSocketPair&) = delete;
};
#endif

constexpr std::array<char, 8> bounded_message_storage { 'm', 'e', 's', 's', 'a', 'g', 'e', 'X' };
constexpr std::array<char, 8> embedded_null_message_storage { 'm', 'e', 's', '\0', 'a', 'g', 'e', 'X' };

std::string_view bounded_message()
{
    return std::string_view(bounded_message_storage.data(), 7);
}

std::string_view embedded_null_message()
{
    return std::string_view(embedded_null_message_storage.data(), 7);
}

template <typename Broadcast>
void expect_bounded_and_embedded_null_output(Broadcast broadcast, descriptor_data& descriptor)
{
    broadcast(bounded_message());
    EXPECT_STREQ(descriptor.output, "message");

    reset_capturing_descriptor(descriptor, descriptor.character);
    broadcast(embedded_null_message());
    EXPECT_STREQ(descriptor.output, "mes");
}

} // namespace

TEST(CommOutput, CommunicationFunctionsExposeBoundedMessageSignatures)
{
    static_assert(std::is_same_v<decltype(&send_to_all), void (*)(std::string_view)>);
    static_assert(std::is_same_v<decltype(&send_to_except),
        void (*)(std::string_view, char_data*)>);
    static_assert(std::is_same_v<decltype(&send_to_room), void (*)(std::string_view, int)>);
    static_assert(std::is_same_v<decltype(&send_to_room_except),
        void (*)(std::string_view, int, char_data*)>);
    static_assert(std::is_same_v<decltype(&send_to_room_except_two),
        void (*)(std::string_view, int, char_data*, char_data*)>);
    static_assert(std::is_same_v<decltype(&send_to_outdoor),
        void (*)(std::string_view, int)>);
    static_assert(std::is_same_v<decltype(&send_to_sector),
        void (*)(std::string_view, int)>);
    static_assert(std::is_same_v<decltype(&perform_to_all),
        void (*)(std::string_view, char_data*)>);
    static_assert(std::is_same_v<decltype(&write_to_descriptor),
        int (*)(SocketType, std::string_view)>);
    static_assert(std::is_same_v<decltype(&write_to_q),
        void (*)(std::string_view, txt_q*)>);
    static_assert(std::is_same_v<decltype(&page_string),
        void (*)(descriptor_data*, std::string_view)>);
    static_assert(std::is_same_v<decltype(&page_string_borrowed),
        void (*)(descriptor_data*, char*)>);
}

TEST(CommOutput, QueueCopiesABoundedMessageBeforeCallerStorageChanges)
{
    txt_q queue {};
    std::string caller_storage = "prefix-queued-suffix";

    write_to_q(std::string_view(caller_storage).substr(7, 6), &queue);
    caller_storage.assign(caller_storage.size(), 'X');

    ASSERT_NE(queue.head, nullptr);
    EXPECT_STREQ(queue.head->text, "queued");
    put_to_txt_block_pool(queue.head);
}

TEST(CommOutput, PagerCopiesLongBoundedTextBeforeCallerStorageChanges)
{
    descriptor_data descriptor {};
    ScopedDescriptorLargeOutbufReturn pager_cleanup { descriptor };
    reset_capturing_descriptor(descriptor, nullptr);
    std::string caller_storage;
    for (int line_number = 0; line_number < 24; ++line_number) {
        caller_storage += std::format("line {}\n", line_number);
    }
    const std::string expected_tail = "line 22\nline 23\n";

    page_string(&descriptor, caller_storage);
    ASSERT_NE(descriptor.showstr_point, nullptr);
    caller_storage.assign(caller_storage.size(), 'X');
    reset_capturing_descriptor(descriptor, nullptr);
    show_string(&descriptor, mutable_arg(""));

    EXPECT_STREQ(descriptor.output, expected_tail.c_str());
}

TEST(CommOutput, SendToAllForwardsBoundedViewsAndEmbeddedNullSemantics)
{
    ScopedDescriptorListReset descriptor_list_reset;
    ConnectedCharacterContext recipient;
    descriptor_list = &recipient.descriptor;

    expect_bounded_and_embedded_null_output(
        [](std::string_view message) { send_to_all(message); }, recipient.descriptor);
}

TEST(CommOutput, SendToExceptForwardsBoundedViewsAndEmbeddedNullSemantics)
{
    ScopedDescriptorListReset descriptor_list_reset;
    ConnectedCharacterContext recipient;
    ConnectedCharacterContext excluded;
    descriptor_list = &recipient.descriptor;

    expect_bounded_and_embedded_null_output(
        [&excluded](std::string_view message) { send_to_except(message, &excluded.character); },
        recipient.descriptor);
}

TEST(CommOutput, SendToRoomForwardsBoundedViewsAndEmbeddedNullSemantics)
{
    ScopedTestWorld test_world;
    ConnectedCharacterContext recipient;
    test_world.room().people = &recipient.character;
    recipient.character.in_room = 0;

    expect_bounded_and_embedded_null_output(
        [](std::string_view message) { send_to_room(message, 0); }, recipient.descriptor);
}

TEST(CommOutput, SendToRoomExceptForwardsBoundedViewsAndEmbeddedNullSemantics)
{
    ScopedTestWorld test_world;
    ConnectedCharacterContext recipient;
    ConnectedCharacterContext excluded;
    test_world.room().people = &recipient.character;
    recipient.character.in_room = 0;

    expect_bounded_and_embedded_null_output(
        [&excluded](std::string_view message) {
            send_to_room_except(message, 0, &excluded.character);
        },
        recipient.descriptor);
}

TEST(CommOutput, SendToRoomExceptTwoForwardsBoundedViewsAndEmbeddedNullSemantics)
{
    ScopedTestWorld test_world;
    ConnectedCharacterContext recipient;
    ConnectedCharacterContext excluded_first;
    ConnectedCharacterContext excluded_second;
    test_world.room().people = &recipient.character;
    recipient.character.in_room = 0;

    expect_bounded_and_embedded_null_output(
        [&excluded_first, &excluded_second](std::string_view message) {
            send_to_room_except_two(
                message, 0, &excluded_first.character, &excluded_second.character);
        },
        recipient.descriptor);
}

TEST(CommOutput, SendToOutdoorForwardsBoundedViewsAndEmbeddedNullSemantics)
{
    ScopedDescriptorListReset descriptor_list_reset;
    ScopedTestWorld test_world;
    ConnectedCharacterContext recipient;
    recipient.character.in_room = 0;
    recipient.character.specials.position = POSITION_STANDING;
    descriptor_list = &recipient.descriptor;

    expect_bounded_and_embedded_null_output(
        [](std::string_view message) { send_to_outdoor(message, 0); }, recipient.descriptor);
}

TEST(CommOutput, SendToSectorForwardsBoundedViewsAndEmbeddedNullSemantics)
{
    ScopedDescriptorListReset descriptor_list_reset;
    ScopedTestWorld test_world;
    ConnectedCharacterContext recipient;
    constexpr int sector_type = 3;
    test_world.room().sector_type = sector_type;
    recipient.character.in_room = 0;
    recipient.character.specials.position = POSITION_STANDING;
    descriptor_list = &recipient.descriptor;

    expect_bounded_and_embedded_null_output(
        [](std::string_view message) { send_to_sector(message, 3); },
        recipient.descriptor);
}

#if defined(PREDEF_PLATFORM_LINUX)
TEST(CommOutput, WriteToDescriptorUsesTheBoundedNormalizedLength)
{
    LocalSocketPair bounded_pair;

    ASSERT_EQ(write_to_descriptor(bounded_pair.writer, bounded_message()), 0);
    std::array<char, 16> bounded_output {};
    const rots_net::ssize_type bounded_bytes_read = rots_net::read_socket(
        bounded_pair.reader, bounded_output.data(), bounded_output.size());
    ASSERT_EQ(bounded_bytes_read, 7);
    EXPECT_EQ(std::string_view(bounded_output.data(), 7), "message");

    LocalSocketPair embedded_null_pair;
    ASSERT_EQ(write_to_descriptor(embedded_null_pair.writer, embedded_null_message()), 0);
    std::array<char, 16> embedded_null_output {};
    const rots_net::ssize_type embedded_null_bytes_read = rots_net::read_socket(
        embedded_null_pair.reader, embedded_null_output.data(), embedded_null_output.size());
    ASSERT_EQ(embedded_null_bytes_read, 3);
    EXPECT_EQ(std::string_view(embedded_null_output.data(), 3), "mes");
}
#endif

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
