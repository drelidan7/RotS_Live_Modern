#include "../comm.h"
#include "../comm_testing.h"
#include "../protocol.h"
#include "../rots_net.h"
#include "rots/core/character.h"
#include "rots/core/descriptor.h"
#include "rots/core/room.h"
#include "rots/core/types.h"
#include "test_char_cleanup.h"
#include "test_placement.h"
#include "test_world.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <format>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#if defined(PREDEF_PLATFORM_LINUX)
#include <sys/socket.h>
#endif

extern descriptor_data* descriptor_list;
void show_string(descriptor_data* descriptor, char* input);
int process_output(descriptor_data* descriptor);
int process_input(descriptor_data* descriptor);
int get_from_q(txt_q* queue, char* destination);
extern int iCommands;

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
    ScopedRoomOccupants occupants { &test_world.room(), 0, { &recipient.character } };

    expect_bounded_and_embedded_null_output(
        [](std::string_view message) { send_to_room(message, 0); }, recipient.descriptor);
}

TEST(CommOutput, SendToRoomExceptForwardsBoundedViewsAndEmbeddedNullSemantics)
{
    ScopedTestWorld test_world;
    ConnectedCharacterContext recipient;
    ConnectedCharacterContext excluded;
    ScopedRoomOccupants occupants { &test_world.room(), 0, { &recipient.character } };

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
    ScopedRoomOccupants occupants { &test_world.room(), 0, { &recipient.character } };

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
    set_location(&recipient.character, 0);
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
    set_location(&recipient.character, 0);
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

#if defined(PREDEF_PLATFORM_LINUX)
namespace {

// Saturates a nonblocking socket so the next write deterministically sends no bytes.
void fill_socket_send_buffer(SocketType socket_handle)
{
    rots_net::set_nonblocking(socket_handle);
    const std::string filler(8192, 'x');
    for (;;) {
        const auto written = rots_net::write_socket(socket_handle, filler.data(), filler.size());
        if (written < 0) {
            ASSERT_TRUE(rots_net::error_is_would_block(rots_net::last_error()));
            return;
        }
        ASSERT_GT(written, 0);
    }
}

TEST(CommOutput, ZeroByteWouldBlockIsDeferredInsteadOfFatal)
{
    LocalSocketPair sockets;
    fill_socket_send_buffer(sockets.writer);
    EXPECT_EQ(write_to_descriptor(sockets.writer, "queued game output"), -2);
}

TEST(CommOutput, DeferredFlushRetainsSmallOutputForRetry)
{
    LocalSocketPair sockets;
    ConnectedCharacterContext context;
    context.descriptor.descriptor = sockets.writer;
    context.descriptor.connected = CON_SLCT;
    write_to_output("queued game output", &context.descriptor);
    ASSERT_STREQ(context.descriptor.output, "queued game output");
    const int original_space = context.descriptor.bufspace;
    const int original_length = context.descriptor.bufptr;
    fill_socket_send_buffer(sockets.writer);
    EXPECT_EQ(process_output(&context.descriptor), 0);
    EXPECT_EQ(context.descriptor.output, context.descriptor.small_outbuf);
    EXPECT_STREQ(context.descriptor.output, "queued game output");
    EXPECT_EQ(context.descriptor.bufspace, original_space);
    EXPECT_EQ(context.descriptor.bufptr, original_length);

    rots_net::set_nonblocking(sockets.reader);
    char drain[8192];
    while (rots_net::read_socket(sockets.reader, drain, sizeof(drain)) > 0) {
    }
    EXPECT_EQ(process_output(&context.descriptor), 1);
    const auto received = rots_net::read_socket(sockets.reader, drain, sizeof(drain));
    ASSERT_GT(received, 0);
    EXPECT_EQ(std::string(drain, static_cast<size_t>(received)), "queued game output");
    EXPECT_STREQ(context.descriptor.output, "");
    EXPECT_EQ(context.descriptor.bufptr, 0);
}

TEST(CommOutput, DeferredFlushRetainsLargeOutputOwnershipForRetry)
{
    LocalSocketPair sockets;
    ConnectedCharacterContext context;
    context.descriptor.descriptor = sockets.writer;
    context.descriptor.connected = CON_SLCT;
    const std::string long_output(SMALL_BUFSIZE + 50, 'z');
    write_to_output(long_output, &context.descriptor);
    ASSERT_NE(context.descriptor.large_outbuf, nullptr);
    auto* const original_large_buffer = context.descriptor.large_outbuf;
    char* const original_output = context.descriptor.output;
    const int original_space = context.descriptor.bufspace;
    const int original_length = context.descriptor.bufptr;
    fill_socket_send_buffer(sockets.writer);
    EXPECT_EQ(process_output(&context.descriptor), 0);
    EXPECT_EQ(context.descriptor.large_outbuf, original_large_buffer);
    EXPECT_EQ(context.descriptor.output, original_output);
    EXPECT_EQ(std::string(context.descriptor.output), long_output);
    EXPECT_EQ(context.descriptor.bufspace, original_space);
    EXPECT_EQ(context.descriptor.bufptr, original_length);
    rots_net::set_nonblocking(sockets.reader);
    char drain[8192];
    while (rots_net::read_socket(sockets.reader, drain, sizeof(drain)) > 0) {
    }
    EXPECT_EQ(process_output(&context.descriptor), 1);
    EXPECT_EQ(context.descriptor.large_outbuf, nullptr);
}

} // namespace
#endif

namespace {

// Owns deterministic write outcomes and restores the borrowed I/O callback at scope exit.
class ScopedScriptedWrites {
public:
    ScopedScriptedWrites()
        : previous_(comm_testing::set_socket_write_override(write_bytes))
    {
        active_ = this;
    }
    ~ScopedScriptedWrites()
    {
        comm_testing::set_socket_write_override(previous_);
        active_ = nullptr;
    }
    // Ordered byte counts; a negative count means a zero-byte would-block outcome.
    std::vector<rots_net::ssize_type> outcomes;
    // Exact bytes accepted before a deferred/fatal outcome.
    std::string accepted;
    // Number of write attempts, including failed attempts.
    size_t calls = 0;

private:
    static rots_net::ssize_type write_bytes(SocketType, const void* bytes, size_t length)
    {
        const auto call_index = active_->calls++;
        auto result = static_cast<rots_net::ssize_type>(length);
        if (call_index < active_->outcomes.size()) {
            result = active_->outcomes[call_index];
        }
        if (result < 0) {
#if defined(_WIN32)
            WSASetLastError(WSAEWOULDBLOCK);
#else
            errno = EWOULDBLOCK;
#endif
            return -1;
        }
        result = std::min(result, static_cast<rots_net::ssize_type>(length));
        active_->accepted.append(static_cast<const char*>(bytes), static_cast<size_t>(result));
        return result;
    }
    // Restores any enclosing test's override; this fixture is used without nesting.
    comm_testing::socket_write_fn previous_;
    // Borrows the single synchronous writer fixture while its override is installed.
    static ScopedScriptedWrites* active_;
};
ScopedScriptedWrites* ScopedScriptedWrites::active_ = nullptr;

TEST(CommOutput, PartialWriteThenWouldBlockRemainsFatalWithoutWholeBufferRetry)
{
    ScopedScriptedWrites writes;
    writes.outcomes = { 3, -1 };
    EXPECT_EQ(write_to_descriptor(42, "abcdef"), -1);
    EXPECT_EQ(writes.accepted, "abc");
    EXPECT_EQ(writes.calls, 2u);
}

TEST(CommOutput, PendingBarePromptSurvivesDeferredFlushThenBreaksExactlyOnce)
{
    ScopedScriptedWrites writes;
    writes.outcomes = { -1 };
    ConnectedCharacterContext context;
    context.descriptor.descriptor = 42;
    context.descriptor.connected = CON_SLCT;
    context.descriptor.prompt_mode = 1;
    context.descriptor.bare_prompt_pending = true;
    write_to_output("room text", &context.descriptor);
    ASSERT_STREQ(context.descriptor.output, "room text");
    EXPECT_EQ(comm_testing::flush_pending_output(&context.descriptor, true), 0);
    EXPECT_EQ(context.descriptor.prompt_mode, 0);
    EXPECT_TRUE(context.descriptor.bare_prompt_pending);
    EXPECT_TRUE(writes.accepted.empty());
    EXPECT_EQ(comm_testing::flush_pending_output(&context.descriptor, true), 1);
    EXPECT_EQ(writes.accepted, "\n\rroom text");
    EXPECT_FALSE(context.descriptor.bare_prompt_pending);
    EXPECT_EQ(context.descriptor.prompt_mode, 1);
}

TEST(CommOutput, UnwritableQueuedOutputSuppressesPromptUntilFlushed)
{
    ScopedScriptedWrites writes;
    ConnectedCharacterContext context;
    context.descriptor.descriptor = 42;
    context.descriptor.prompt_mode = 1;
    send_to_char("room text", &context.character);
    EXPECT_EQ(comm_testing::flush_pending_output(&context.descriptor, false), 0);
    comm_testing::write_prompt(&context.descriptor);
    EXPECT_EQ(writes.calls, 0u);
    EXPECT_STREQ(context.descriptor.output, "room text");
    EXPECT_EQ(context.descriptor.prompt_mode, 0);
    EXPECT_EQ(comm_testing::flush_pending_output(&context.descriptor, true), 1);
    comm_testing::write_prompt(&context.descriptor);
    EXPECT_EQ(writes.accepted, "room text\n\r>");
    EXPECT_TRUE(context.descriptor.bare_prompt_pending);
}

TEST(CommOutput, EditorPagerAndNormalPromptsMarkOnlySuccessfulWrites)
{
    for (const int prompt_kind : { 0, 1, 2 }) {
        ScopedScriptedWrites writes;
        writes.outcomes = { -1 };
        ConnectedCharacterContext context;
        context.descriptor.descriptor = 42;
        context.descriptor.prompt_mode = 1;
        char page[] = "more";
        if (prompt_kind == 0) {
            SET_BIT(context.character.specials2.act, PLR_WRITING);
        } else if (prompt_kind == 1) {
            context.descriptor.showstr_point = page;
        }
        comm_testing::write_prompt(&context.descriptor);
        EXPECT_FALSE(context.descriptor.bare_prompt_pending) << prompt_kind;
        EXPECT_TRUE(writes.accepted.empty());
        context.descriptor.prompt_mode = 1;
        comm_testing::write_prompt(&context.descriptor);
        EXPECT_TRUE(context.descriptor.bare_prompt_pending) << prompt_kind;
        EXPECT_FALSE(writes.accepted.empty());
    }
}

TEST(CommOutput, EmptyOrInvalidPromptWritesNeverClaimABarePromptReachedTheSocket)
{
    ScopedScriptedWrites writes;
    ConnectedCharacterContext context;
    context.descriptor.descriptor = 42;
    comm_testing::write_bare_prompt(&context.descriptor, "");
    comm_testing::write_bare_prompt(&context.descriptor, std::string_view("\0ignored", 8));
    context.descriptor.descriptor = 0;
    comm_testing::write_bare_prompt(&context.descriptor, ">");
    context.descriptor.descriptor = rots_net::kInvalidSocket;
    comm_testing::write_bare_prompt(&context.descriptor, ">");
    EXPECT_FALSE(context.descriptor.bare_prompt_pending);
    EXPECT_EQ(writes.calls, 0u);
}

} // namespace

namespace {

// Supplies bounded read fragments without depending on TCP packet coalescing.
class ScopedScriptedReads {
public:
    ScopedScriptedReads()
        : previous_(comm_testing::set_socket_read_override(read_bytes))
        , previous_command_count_(iCommands)
    {
        active_ = this;
        iCommands = 0;
    }
    ~ScopedScriptedReads()
    {
        comm_testing::set_socket_read_override(previous_);
        active_ = nullptr;
        iCommands = previous_command_count_;
    }
    // Each entry is one read result; empty entries represent orderly EOF.
    std::vector<std::string> chunks;
    // Number of attempted reads, including the final would-block.
    size_t calls = 0;

private:
    static rots_net::ssize_type read_bytes(SocketType, void* buffer, size_t capacity)
    {
        const auto chunk_index = active_->calls++;
        if (chunk_index >= active_->chunks.size()) {
#if defined(_WIN32)
            WSASetLastError(WSAEWOULDBLOCK);
#else
            errno = EWOULDBLOCK;
#endif
            return -1;
        }
        const std::string_view chunk = active_->chunks[chunk_index];
        EXPECT_LE(chunk.size(), capacity);
        const size_t count = std::min(chunk.size(), capacity);
        std::memcpy(buffer, chunk.data(), count);
        return static_cast<rots_net::ssize_type>(count);
    }
    // Restores the prior test callback when the fixture leaves scope.
    comm_testing::socket_read_fn previous_;
    // Avoids the unrelated command-log rotation threshold while processing fixture input.
    int previous_command_count_;
    // Borrows the active fixture only during its synchronous callback lifetime.
    static ScopedScriptedReads* active_;
};
ScopedScriptedReads* ScopedScriptedReads::active_ = nullptr;

TEST(CommOutput, ReadQuotaRetainsPartialLineForNextPulse)
{
    ScopedScriptedReads reads;
    reads.chunks = { "a", "b", "c", "d", "e", "f", "g", "h", "\r\n" };
    descriptor_data descriptor { };
    descriptor.descriptor = 42;
    descriptor.connected = CON_NME;
    EXPECT_EQ(process_input(&descriptor), 0);
    EXPECT_EQ(reads.calls, 8u);
    EXPECT_STREQ(descriptor.buf, "abcdefgh");
    EXPECT_EQ(descriptor.input.head, nullptr);
    EXPECT_EQ(process_input(&descriptor), 1);
    char command[MAX_INPUT_LENGTH] = { };
    EXPECT_EQ(get_from_q(&descriptor.input, command), 1);
    EXPECT_STREQ(command, "abcdefgh");
    EXPECT_STREQ(descriptor.buf, "");
}

TEST(CommOutput, NegotiationOnlyReadsConsumeQuotaWithoutInspectingAnEmptyTextTail)
{
    ScopedScriptedReads reads;
    const std::string negotiation { static_cast<char>(255), static_cast<char>(252), 1 };
    reads.chunks.assign(9, negotiation);
    descriptor_data descriptor { };
    descriptor.descriptor = 42;
    descriptor.connected = CON_NME;
    descriptor.pProtocol = ProtocolCreate();
    EXPECT_EQ(process_input(&descriptor), 0);
    EXPECT_EQ(reads.calls, 8u);
    EXPECT_STREQ(descriptor.buf, "");
    EXPECT_EQ(process_input(&descriptor), 0);
    EXPECT_EQ(reads.calls, 10u);
    EXPECT_STREQ(descriptor.buf, "");
    ProtocolDestroy(descriptor.pProtocol);
}

TEST(CommOutput, ScriptedInputEofIsFatalAndWouldBlockRetainsPartialText)
{
    ScopedScriptedReads reads;
    descriptor_data descriptor { };
    descriptor.descriptor = 42;
    descriptor.connected = CON_NME;
    std::strcpy(descriptor.buf, "partial");
    EXPECT_EQ(process_input(&descriptor), 0);
    EXPECT_STREQ(descriptor.buf, "partial");
    reads.chunks = { "unused", "" };
    EXPECT_EQ(process_input(&descriptor), -1);
    EXPECT_STREQ(descriptor.buf, "partial");
}

} // namespace
