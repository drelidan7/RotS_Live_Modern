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

} // namespace

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
