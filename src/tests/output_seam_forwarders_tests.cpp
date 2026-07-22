// output_seam_forwarders_tests.cpp

// New test TU (blocker-buster wave, Task 1; plan
// docs/superpowers/plans/2026-07-19-blocker-buster.md; brief
// .superpowers/sdd/task-1-brief.md). Standing coverage-gap rule (same class
// noted by wild_fighting_handler_tests.cpp's DispatchWildAttackSpeedMultiplier...
// test and placement_tests.cpp's ScopedWorldResolverHooks): output_seam.h's
// original five forwarders had no test proving their null-sink default is
// actually a safe no-op -- gtest_main.cpp registers the REAL sinks for the
// whole test process, so nothing ever exercised the tripwire branch. This
// file is the negative half ("defaults safely when unregistered") of three
// of the seven new forwarders' required coverage:
//   - send_to_all / send_to_room / send_to_room_except_two
// Their POSITIVE half ("reaches the real comm.cpp sink when registered") is
// already covered without any new test: comm_output_tests.cpp's pre-existing
// SendToAllForwardsBoundedViewsAndEmbeddedNullSemantics /
// SendToRoomForwardsBoundedViewsAndEmbeddedNullSemantics /
// SendToRoomExceptTwoForwardsBoundedViewsAndEmbeddedNullSemantics tests call
// the plain send_to_all()/send_to_room()/send_to_room_except_two() symbols
// and assert on delivered content -- as of this wave those plain symbols ARE
// output_seam.cpp's forwarders, dispatched to comm.cpp's real *_impl bodies
// via gtest_main.cpp's process-wide register_game_output_sinks() call, so
// those tests newly double as this seam's positive-path proof without
// modification. The remaining four new forwarders (break_spell/abort_delay/
// complete_delay/get_from_txt_block_pool(std::string_view)) have no prior
// coverage at all, so their positive AND negative proofs both live in the
// sibling file comm_delay_tests.cpp instead of here (different fixture
// domain: char_data delay/waiting_list state, not descriptor/world output).
//
// DISCRIMINATOR SHAPE: each test below sets up the same kind of recipient
// comm_output_tests.cpp's positive tests use (a connected descriptor able to
// capture output), swaps EVERY sink to null via ScopedOutputSinks, calls the
// forwarder, and asserts the recipient's output buffer is untouched --
// proving the unregistered path is a true no-op rather than a crash or a
// disguised partial delivery. Read together with comm_output_tests.cpp's
// mirror-image positive tests (same forwarder, same style of recipient,
// opposite registration state, opposite -- delivered vs. untouched --
// result), the two files jointly discriminate "reaches the real sink" from
// "hits the default" for all three forwarders covered here.
#include "../comm.h"
#include "../output_seam.h"
#include "rots/core/character.h"
#include "rots/core/descriptor.h"
#include "rots/core/types.h"

#include <gtest/gtest.h>

extern descriptor_data* descriptor_list;

namespace {

// Swaps rots::output::Sinks to an all-null instance for this scope (Sinks has
// no per-field setter, only the all-or-nothing set_sinks() -- mirrors
// placement_tests.cpp's ScopedWorldResolverHooks, which swaps entity_hooks.h's
// whole resolver set together for the identical reason). The destructor
// unconditionally calls comm.cpp's real register_game_output_sinks() so no
// later test in the binary ever observes an unregistered sink, even if this
// scope's test body fails an assertion and unwinds early.
class ScopedOutputSinks {
public:
    ScopedOutputSinks()
    {
        rots::output::set_sinks(rots::output::Sinks {});
    }

    ~ScopedOutputSinks()
    {
        register_game_output_sinks();
    }

    ScopedOutputSinks(const ScopedOutputSinks&) = delete;
    ScopedOutputSinks& operator=(const ScopedOutputSinks&) = delete;
};

// Restores the process-global descriptor chain after an isolated test --
// same shape as comm_output_tests.cpp's ScopedDescriptorListReset, duplicated
// here rather than shared: the two files' fixtures diverge on their very next
// addition (that file grows content-matching helpers, this one stays a bare
// swap), so sharing a header for one field would buy little.
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
    // The descriptor chain in effect before this fixture ran; restored on destruction.
    descriptor_data* previous_descriptor_list_;
};

// A connected recipient able to capture queued output without opening a
// network connection -- same shape as comm_output_tests.cpp's
// ConnectedCharacterContext, minus the embedded-null/bounded-view content
// helpers this file's tests do not need (they only ever assert absence of
// delivery, never exact bytes).
struct ConnectedCharacterContext {
    // The character under test; exists only so descriptor.character has a valid target.
    char_data character {};
    // Captures queued output (or, for these tests, its deliberate absence).
    descriptor_data descriptor {};

    ConnectedCharacterContext()
    {
        descriptor.output = descriptor.small_outbuf;
        descriptor.small_outbuf[0] = '\0';
        descriptor.bufptr = 0;
        descriptor.bufspace = SMALL_BUFSIZE - 1;
        descriptor.connected = CON_PLYNG;
        descriptor.character = &character;
        character.desc = &descriptor;
    }
};

// Records a put_to_txt_block_pool() forwarder call -- DISCRIMINATOR fixture
// for this file's PutToTxtBlockPool tests below (l4-seed wave, Task 1;
// l4-task-1-brief.md Step 4). Unlike ScopedOutputSinks above (an all-null
// swap for the six unregistered-default tests), this scopes a Sinks with
// ONLY put_txt_block_to_pool populated -- sufficient since these two tests
// never call any other forwarder -- and restores the real sinks via
// register_game_output_sinks() on destruction, the same restore-via-real-
// registrar shape as this tree's *_hooks_tests.cpp Scoped* fixtures.
class ScopedPutTxtBlockToPoolSink {
public:
    explicit ScopedPutTxtBlockToPoolSink(rots::output::put_txt_block_to_pool_fn hook)
    {
        rots::output::Sinks sinks {};
        sinks.put_txt_block_to_pool = hook;
        rots::output::set_sinks(sinks);
    }

    ~ScopedPutTxtBlockToPoolSink()
    {
        register_game_output_sinks();
    }

    ScopedPutTxtBlockToPoolSink(const ScopedPutTxtBlockToPoolSink&) = delete;
    ScopedPutTxtBlockToPoolSink& operator=(const ScopedPutTxtBlockToPoolSink&) = delete;
};

struct RecordedPutTxtBlockToPoolCall {
    txt_block* block = nullptr;
    bool called = false;
};

RecordedPutTxtBlockToPoolCall g_recorded_put_txt_block_to_pool_call;

void recording_put_txt_block_to_pool_stub(txt_block* block)
{
    g_recorded_put_txt_block_to_pool_call = RecordedPutTxtBlockToPoolCall { block, true };
}

} // namespace

// put_to_txt_block_pool() -- DISCRIMINATOR: a recording stub proves the
// forwarder reaches a registered sink with its argument intact; the
// unregistered path is a SAFE logged no-op (unlike get_from_txt_block_pool's
// abort tripwire -- PUT never dereferences its argument), so this pair
// exercises both halves directly rather than needing a descriptor fixture.

TEST(OutputSeamForwarders, PutToTxtBlockPoolReachesARegisteredStubWithArgIntact)
{
    g_recorded_put_txt_block_to_pool_call = RecordedPutTxtBlockToPoolCall {};
    ScopedPutTxtBlockToPoolSink scoped(recording_put_txt_block_to_pool_stub);
    txt_block block {};

    put_to_txt_block_pool(&block);

    EXPECT_TRUE(g_recorded_put_txt_block_to_pool_call.called)
        << "Expected the registered stub to have been reached.";
    EXPECT_EQ(g_recorded_put_txt_block_to_pool_call.block, &block);
}

TEST(OutputSeamForwarders, PutToTxtBlockPoolDefaultsToASafeNoOpWhenUnregistered)
{
    g_recorded_put_txt_block_to_pool_call = RecordedPutTxtBlockToPoolCall {};
    ScopedPutTxtBlockToPoolSink unregistered(nullptr);
    txt_block block {};

    put_to_txt_block_pool(&block);

    EXPECT_FALSE(g_recorded_put_txt_block_to_pool_call.called)
        << "Expected an unregistered put-to-txt-block-pool sink to leave the (unrelated) stub's "
           "own recording flag untouched -- the real forwarder never ran, and the tripwire "
           "default is a SAFE logged no-op (a leaked block, not a crash), not a call to any stub.";
}

TEST(OutputSeamForwarders, SendToAllDefaultsToANoOpWhenUnregistered)
{
    ScopedOutputSinks unregistered_sinks;
    ScopedDescriptorListReset descriptor_list_reset;
    ConnectedCharacterContext recipient;
    descriptor_list = &recipient.descriptor;

    send_to_all("hello");

    EXPECT_STREQ(recipient.descriptor.output, "")
        << "Expected an unregistered send_to_all sink to leave every descriptor's output buffer "
           "untouched, matching comm_output_tests.cpp's SendToAllForwardsBoundedViewsAnd"
           "EmbeddedNullSemantics test's delivered 'message' under the REGISTERED sink.";
}

TEST(OutputSeamForwarders, SendToRoomDefaultsToANoOpWhenUnregistered)
{
    ScopedOutputSinks unregistered_sinks;
    // world[]-backed room fixture (ScopedTestWorld would be needed for the
    // registered/positive path, which comm_output_tests.cpp already covers);
    // the unregistered forwarder never reaches world[] at all, so a bare
    // room id with no world fixture is sufficient here -- it proves the
    // early-return happens strictly before any world[] access, not merely
    // that a populated room saw no delivery.
    ConnectedCharacterContext recipient;

    send_to_room("hello", 0);

    EXPECT_STREQ(recipient.descriptor.output, "")
        << "Expected an unregistered send_to_room sink to never reach world[], leaving every "
           "descriptor's output buffer untouched.";
}

TEST(OutputSeamForwarders, SendToRoomExceptTwoDefaultsToANoOpWhenUnregistered)
{
    ScopedOutputSinks unregistered_sinks;
    ConnectedCharacterContext recipient;
    ConnectedCharacterContext excluded_first;
    ConnectedCharacterContext excluded_second;

    send_to_room_except_two("hello", 0, &excluded_first.character, &excluded_second.character);

    EXPECT_STREQ(recipient.descriptor.output, "")
        << "Expected an unregistered send_to_room_except_two sink to never reach world[], "
           "leaving every descriptor's output buffer untouched.";
}

// send_to_room_except() (Cluster B wave Task 1; cb-task-1-brief.md Step 4;
// cb-census.md section 5.5) -- comm.cpp's own real body backs this
// forwarder unchanged (script.cpp:1595-1596's two call sites already call
// the plain `send_to_room_except` symbol, so no consumer edit is needed for
// this seam to take effect). The POSITIVE half ("reaches the real comm.cpp
// sink when registered") is already covered without any new test:
// comm_output_tests.cpp's pre-existing
// SendToRoomExceptForwardsBoundedViewsAndEmbeddedNullSemantics test calls
// the plain send_to_room_except() symbol and asserts on delivered content --
// as of this task that plain symbol IS this forwarder, dispatched to
// comm.cpp's real send_to_room_except_impl() via gtest_main.cpp's
// process-wide register_game_output_sinks() call, so that test newly
// doubles as this seam's positive-path proof without modification (the same
// "no new positive test needed" shape as SendToAll/SendToRoom above).

TEST(OutputSeamForwarders, SendToRoomExceptDefaultsToANoOpWhenUnregistered)
{
    ScopedOutputSinks unregistered_sinks;
    ConnectedCharacterContext recipient;
    ConnectedCharacterContext excluded;

    send_to_room_except("hello", 0, &excluded.character);

    EXPECT_STREQ(recipient.descriptor.output, "")
        << "Expected an unregistered send_to_room_except sink to never reach world[], leaving "
           "every descriptor's output buffer untouched.";
}

// ---------------------------------------------------------------------------
// Behavior-wave Task 1 accessors: close_socket()/no_specials_active()/
// request_circle_shutdown() (census sections 9/10). CONSUMER-FREE this
// task -- no mobact.cpp/limits.cpp call site converts yet -- same
// "recording stub proves forward-then-tripwire" discriminator shape as this
// file's put_to_txt_block_pool() suite above.
// ---------------------------------------------------------------------------

namespace {

struct RecordedCloseSocketCall {
    descriptor_data* d = nullptr;
    int drop_all = 0;
    bool called = false;
};

RecordedCloseSocketCall g_recorded_close_socket_call;

void recording_close_socket_stub(descriptor_data* d, int drop_all)
{
    g_recorded_close_socket_call = RecordedCloseSocketCall { d, drop_all, true };
}

class ScopedCloseSocketSink {
public:
    explicit ScopedCloseSocketSink(rots::output::close_socket_fn hook)
    {
        rots::output::Sinks sinks {};
        sinks.close_socket = hook;
        rots::output::set_sinks(sinks);
    }

    ~ScopedCloseSocketSink() { register_game_output_sinks(); }

    ScopedCloseSocketSink(const ScopedCloseSocketSink&) = delete;
    ScopedCloseSocketSink& operator=(const ScopedCloseSocketSink&) = delete;
};

bool g_no_specials_active_stub_return = false;
bool g_no_specials_active_stub_called = false;

bool recording_no_specials_active_stub()
{
    g_no_specials_active_stub_called = true;
    return g_no_specials_active_stub_return;
}

class ScopedNoSpecialsActiveSink {
public:
    explicit ScopedNoSpecialsActiveSink(rots::output::no_specials_active_fn hook)
    {
        rots::output::Sinks sinks {};
        sinks.no_specials_active = hook;
        rots::output::set_sinks(sinks);
    }

    ~ScopedNoSpecialsActiveSink() { register_game_output_sinks(); }

    ScopedNoSpecialsActiveSink(const ScopedNoSpecialsActiveSink&) = delete;
    ScopedNoSpecialsActiveSink& operator=(const ScopedNoSpecialsActiveSink&) = delete;
};

bool g_request_circle_shutdown_stub_called = false;

void recording_request_circle_shutdown_stub()
{
    g_request_circle_shutdown_stub_called = true;
}

class ScopedRequestCircleShutdownSink {
public:
    explicit ScopedRequestCircleShutdownSink(rots::output::request_circle_shutdown_fn hook)
    {
        rots::output::Sinks sinks {};
        sinks.request_circle_shutdown = hook;
        rots::output::set_sinks(sinks);
    }

    ~ScopedRequestCircleShutdownSink() { register_game_output_sinks(); }

    ScopedRequestCircleShutdownSink(const ScopedRequestCircleShutdownSink&) = delete;
    ScopedRequestCircleShutdownSink& operator=(const ScopedRequestCircleShutdownSink&) = delete;
};

} // namespace

TEST(OutputSeamForwarders, CloseSocketReachesARegisteredStubWithArgsIntact)
{
    g_recorded_close_socket_call = RecordedCloseSocketCall {};
    ScopedCloseSocketSink scoped(recording_close_socket_stub);
    descriptor_data descriptor {};

    close_socket(&descriptor, 1);

    EXPECT_TRUE(g_recorded_close_socket_call.called)
        << "Expected the registered stub to have been reached.";
    EXPECT_EQ(g_recorded_close_socket_call.d, &descriptor);
    EXPECT_EQ(g_recorded_close_socket_call.drop_all, 1);
}

TEST(OutputSeamForwarders, CloseSocketDefaultsToANoOpWhenUnregistered)
{
    g_recorded_close_socket_call = RecordedCloseSocketCall {};
    ScopedCloseSocketSink unregistered(nullptr);
    descriptor_data descriptor {};

    close_socket(&descriptor, 1);

    EXPECT_FALSE(g_recorded_close_socket_call.called)
        << "Expected an unregistered close_socket sink to leave the (unrelated) stub's own "
           "recording flag untouched -- the real forwarder never ran, and the tripwire default "
           "is a logged no-op, not a call to any stub.";
}

TEST(OutputSeamForwarders, NoSpecialsActiveReachesARegisteredStubAndForwardsReturnValue)
{
    g_no_specials_active_stub_called = false;
    g_no_specials_active_stub_return = true;
    ScopedNoSpecialsActiveSink scoped(recording_no_specials_active_stub);

    const bool result = no_specials_active();

    EXPECT_TRUE(g_no_specials_active_stub_called)
        << "Expected the registered stub to have been reached.";
    EXPECT_TRUE(result) << "Expected no_specials_active() to forward the stub's own return value.";
}

TEST(OutputSeamForwarders, NoSpecialsActiveDefaultsToPermissiveFalseWhenUnregistered)
{
    g_no_specials_active_stub_called = false;
    ScopedNoSpecialsActiveSink unregistered(nullptr);

    const bool result = no_specials_active();

    EXPECT_FALSE(g_no_specials_active_stub_called)
        << "Expected the (unrelated) stub's own called flag to stay untouched -- the real "
           "forwarder never ran.";
    EXPECT_FALSE(result)
        << "Expected an unregistered no_specials_active sink to default to false (specials not "
           "suppressed), matching the global's own pre-boot value.";
}

TEST(OutputSeamForwarders, RequestCircleShutdownReachesARegisteredStub)
{
    g_request_circle_shutdown_stub_called = false;
    ScopedRequestCircleShutdownSink scoped(recording_request_circle_shutdown_stub);

    request_circle_shutdown();

    EXPECT_TRUE(g_request_circle_shutdown_stub_called)
        << "Expected the registered stub to have been reached.";
}

TEST(OutputSeamForwarders, RequestCircleShutdownDefaultsToANoOpWhenUnregistered)
{
    g_request_circle_shutdown_stub_called = false;
    ScopedRequestCircleShutdownSink unregistered(nullptr);

    request_circle_shutdown();

    EXPECT_FALSE(g_request_circle_shutdown_stub_called)
        << "Expected an unregistered request_circle_shutdown sink to leave the (unrelated) "
           "stub's own recording flag untouched -- the real forwarder never ran, and the "
           "tripwire default is a logged no-op, not a call to any stub.";
}

// ---------------------------------------------------------------------------
// msdp_room_update()/get_descriptor_list_head() (spell-family closure wave
// Task 1; sf-census.md sections 4.1/4.2). CONSUMER-FREE this task -- no
// mage.cpp/spell_pa.cpp call site converts yet -- same "recording stub
// proves forward-then-tripwire" discriminator shape as the pairs above.
// ---------------------------------------------------------------------------

namespace {

struct RecordedMsdpRoomUpdateCall {
    char_data* ch = nullptr;
    bool called = false;
};

RecordedMsdpRoomUpdateCall g_recorded_msdp_room_update_call;

void recording_msdp_room_update_stub(char_data* ch)
{
    g_recorded_msdp_room_update_call = RecordedMsdpRoomUpdateCall { ch, true };
}

class ScopedMsdpRoomUpdateSink {
public:
    explicit ScopedMsdpRoomUpdateSink(rots::output::msdp_room_update_fn hook)
    {
        rots::output::Sinks sinks {};
        sinks.msdp_room_update = hook;
        rots::output::set_sinks(sinks);
    }

    ~ScopedMsdpRoomUpdateSink() { register_game_output_sinks(); }

    ScopedMsdpRoomUpdateSink(const ScopedMsdpRoomUpdateSink&) = delete;
    ScopedMsdpRoomUpdateSink& operator=(const ScopedMsdpRoomUpdateSink&) = delete;
};

descriptor_data* g_descriptor_list_head_stub_return = nullptr;
bool g_descriptor_list_head_stub_called = false;

descriptor_data* recording_descriptor_list_head_stub()
{
    g_descriptor_list_head_stub_called = true;
    return g_descriptor_list_head_stub_return;
}

class ScopedDescriptorListHeadSink {
public:
    explicit ScopedDescriptorListHeadSink(rots::output::descriptor_list_head_fn hook)
    {
        rots::output::Sinks sinks {};
        sinks.descriptor_list_head = hook;
        rots::output::set_sinks(sinks);
    }

    ~ScopedDescriptorListHeadSink() { register_game_output_sinks(); }

    ScopedDescriptorListHeadSink(const ScopedDescriptorListHeadSink&) = delete;
    ScopedDescriptorListHeadSink& operator=(const ScopedDescriptorListHeadSink&) = delete;
};

} // namespace

TEST(OutputSeamForwarders, MsdpRoomUpdateReachesARegisteredStubWithArgIntact)
{
    g_recorded_msdp_room_update_call = RecordedMsdpRoomUpdateCall {};
    ScopedMsdpRoomUpdateSink scoped(recording_msdp_room_update_stub);
    char_data character {};

    msdp_room_update(&character);

    EXPECT_TRUE(g_recorded_msdp_room_update_call.called)
        << "Expected the registered stub to have been reached.";
    EXPECT_EQ(g_recorded_msdp_room_update_call.ch, &character);
}

TEST(OutputSeamForwarders, MsdpRoomUpdateDefaultsToANoOpWhenUnregistered)
{
    g_recorded_msdp_room_update_call = RecordedMsdpRoomUpdateCall {};
    ScopedMsdpRoomUpdateSink unregistered(nullptr);
    char_data character {};

    msdp_room_update(&character);

    EXPECT_FALSE(g_recorded_msdp_room_update_call.called)
        << "Expected an unregistered msdp_room_update sink to leave the (unrelated) stub's own "
           "recording flag untouched -- the real forwarder never ran, and the tripwire default "
           "is a logged no-op, not a call to any stub.";
}

TEST(OutputSeamForwarders, DescriptorListHeadReachesARegisteredStubAndForwardsReturnValue)
{
    g_descriptor_list_head_stub_called = false;
    descriptor_data sentinel {};
    g_descriptor_list_head_stub_return = &sentinel;
    ScopedDescriptorListHeadSink scoped(recording_descriptor_list_head_stub);

    descriptor_data* result = get_descriptor_list_head();

    EXPECT_TRUE(g_descriptor_list_head_stub_called)
        << "Expected the registered stub to have been reached.";
    EXPECT_EQ(result, &sentinel)
        << "Expected get_descriptor_list_head() to forward the stub's own return value.";
}

TEST(OutputSeamForwarders, DescriptorListHeadDefaultsToNullptrWhenUnregistered)
{
    g_descriptor_list_head_stub_called = false;
    ScopedDescriptorListHeadSink unregistered(nullptr);

    descriptor_data* result = get_descriptor_list_head();

    EXPECT_FALSE(g_descriptor_list_head_stub_called)
        << "Expected the (unrelated) stub's own called flag to stay untouched -- the real "
           "forwarder never ran.";
    EXPECT_EQ(result, nullptr)
        << "Expected an unregistered descriptor_list_head sink to default to nullptr (an empty "
           "list), the same safe sentinel every existing head-walk loop already treats as "
           "\"no players\".";
}

