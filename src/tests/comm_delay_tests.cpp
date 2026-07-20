// comm_delay_tests.cpp

// New test TU (blocker-buster wave, Task 1; plan
// docs/superpowers/plans/2026-07-19-blocker-buster.md; brief
// .superpowers/sdd/task-1-brief.md). Covers the four output_seam.h
// extensions with no prior coverage anywhere in the suite: break_spell/
// abort_delay/complete_delay (the delayed-command teardown/completion trio)
// and get_from_txt_block_pool(std::string_view) (the content-carrying
// txt-block-pool getter overload) -- unlike send_to_all/send_to_room/
// send_to_room_except_two (covered by output_seam_forwarders_tests.cpp's
// negative half plus comm_output_tests.cpp's pre-existing positive half),
// none of these four were ever called by an existing test, so both halves
// of their coverage (reaches the real comm.cpp sink when registered; defaults
// safely when unregistered) live here together. Separate file from
// comm_output_tests.cpp/output_seam_forwarders_tests.cpp: this fixture
// domain is char_data delay/waiting_list state and the txt-block pool, not
// descriptor/world output -- no reset_capturing_descriptor/ConnectedCharacter
// Context/ScopedTestWorld machinery applies to any test below.
//
// DISCRIMINATOR SHAPE: each REACHES-THE-REAL-SINK test sets a sentinel value
// on the state the real comm.cpp body is known (by reading its source) to
// mutate, calls the plain forwarder with the REGISTERED sinks gtest_main.cpp
// installs process-wide, and asserts the mutation happened. Each DEFAULTS-
// TO-A-NO-OP test swaps every sink to null via ScopedOutputSinks (same
// fixture as output_seam_forwarders_tests.cpp, duplicated here for the same
// reason noted there: the two files' surrounding fixtures diverge enough
// that sharing one header for a 6-line class would buy little), sets the
// identical sentinel, calls the same forwarder, and asserts NOTHING moved --
// proving the tripwire-logged default is a true no-op, not a partial or
// silently-wrong mutation.
#include "../comm.h"
#include "../output_seam.h"
#include "../utils.h"
#include "rots/core/character.h"
#include "rots/core/types.h"

#include <gtest/gtest.h>

#include <string_view>

extern char_data* waiting_list;

namespace {

// Swaps rots::output::Sinks to an all-null instance for this scope -- see
// output_seam_forwarders_tests.cpp's ScopedOutputSinks for the full
// rationale (Sinks has no per-field setter, so this is an all-or-nothing
// swap); duplicated here rather than shared across the two files.
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

// Restores the process-global waiting_list chain after an isolated test --
// same RAII shape as comm_output_tests.cpp's ScopedDescriptorListReset,
// applied to comm.cpp's other process-global list (abort_delay's real body
// unlinks its argument from this list).
class ScopedWaitingListReset {
public:
    ScopedWaitingListReset()
        : previous_waiting_list_(waiting_list)
    {
        waiting_list = nullptr;
    }

    ~ScopedWaitingListReset()
    {
        waiting_list = previous_waiting_list_;
    }

    ScopedWaitingListReset(const ScopedWaitingListReset&) = delete;
    ScopedWaitingListReset& operator=(const ScopedWaitingListReset&) = delete;

private:
    // The waiting_list chain in effect before this fixture ran; restored on destruction.
    char_data* previous_waiting_list_;
};

} // namespace

// break_spell() (comm.cpp) -- real body is two unconditional field writes
// (ch->delay.wait_value = 0; ch->delay.subcmd = -1;), no output/world/
// waiting_list traffic at all (the rest of its historical body is commented
// out -- see comm.cpp). Sentinel values distinguishable from both fields'
// real-body targets (0 and -1) prove the mutation happened (or didn't).

TEST(CommDelay, BreakSpellReachesTheRealSinkWhenRegistered)
{
    char_data character {};
    character.delay.wait_value = 5;
    character.delay.subcmd = 7;

    break_spell(&character);

    EXPECT_EQ(character.delay.wait_value, 0)
        << "Expected the real break_spell body to zero delay.wait_value.";
    EXPECT_EQ(character.delay.subcmd, -1)
        << "Expected the real break_spell body to reset delay.subcmd to -1.";
}

TEST(CommDelay, BreakSpellDefaultsToANoOpWhenUnregistered)
{
    ScopedOutputSinks unregistered_sinks;
    char_data character {};
    character.delay.wait_value = 5;
    character.delay.subcmd = 7;

    break_spell(&character);

    EXPECT_EQ(character.delay.wait_value, 5)
        << "Expected an unregistered break_spell sink to leave delay.wait_value untouched.";
    EXPECT_EQ(character.delay.subcmd, 7)
        << "Expected an unregistered break_spell sink to leave delay.subcmd untouched.";
}

// abort_delay() (comm.cpp) -- real body unlinks its argument from the global
// waiting_list, clears AFF_WAITWHEEL/AFF_WAITING, and zeroes delay.next/
// wait_value/priority. A single-node waiting_list (character.delay.next ==
// nullptr) keeps the head-removal branch (wait_ch == waiting_list) the only
// one exercised, avoiding a multi-node list fixture no assertion here needs.

TEST(CommDelay, AbortDelayReachesTheRealSinkWhenRegistered)
{
    ScopedWaitingListReset waiting_list_reset;
    char_data character {};
    character.delay.next = nullptr;
    character.delay.wait_value = 9;
    character.delay.priority = 3;
    character.specials.affected_by = AFF_WAITWHEEL | AFF_WAITING;
    waiting_list = &character;

    abort_delay(&character);

    EXPECT_EQ(waiting_list, nullptr)
        << "Expected the real abort_delay body to unlink the sole waiting_list node.";
    EXPECT_EQ(character.delay.next, nullptr);
    EXPECT_EQ(character.delay.wait_value, 0);
    EXPECT_EQ(character.delay.priority, 0);
    EXPECT_FALSE(IS_SET(character.specials.affected_by, AFF_WAITWHEEL));
    EXPECT_FALSE(IS_SET(character.specials.affected_by, AFF_WAITING));
}

TEST(CommDelay, AbortDelayDefaultsToANoOpWhenUnregistered)
{
    ScopedOutputSinks unregistered_sinks;
    ScopedWaitingListReset waiting_list_reset;
    char_data character {};
    character.delay.next = nullptr;
    character.delay.wait_value = 9;
    character.delay.priority = 3;
    character.specials.affected_by = AFF_WAITWHEEL | AFF_WAITING;
    waiting_list = &character;

    abort_delay(&character);

    EXPECT_EQ(waiting_list, &character)
        << "Expected an unregistered abort_delay sink to leave waiting_list untouched.";
    EXPECT_EQ(character.delay.wait_value, 9);
    EXPECT_EQ(character.delay.priority, 3);
    EXPECT_TRUE(IS_SET(character.specials.affected_by, AFF_WAITWHEEL));
    EXPECT_TRUE(IS_SET(character.specials.affected_by, AFF_WAITING));
}

// complete_delay() (comm.cpp) -- real body always zeroes delay.wait_value
// and clears AFF_WAITWHEEL/AFF_WAITING, then branches on delay.cmd (CMD_SCRIPT
// re-enters the mudscript interpreter; -1 + NPC calls a special procedure;
// >0 re-enters command_interpreter()). delay.cmd == 0 hits none of those
// branches (CMD_SCRIPT is 9999, not 0 -- see interpre.h), isolating the
// always-run field-reset path without needing to construct a mob_index/
// command_interpreter-ready fixture no assertion here needs.

TEST(CommDelay, CompleteDelayReachesTheRealSinkWhenRegistered)
{
    char_data character {};
    character.delay.cmd = 0;
    character.delay.wait_value = 7;
    character.specials.affected_by = AFF_WAITWHEEL | AFF_WAITING;

    complete_delay(&character);

    EXPECT_EQ(character.delay.wait_value, 0)
        << "Expected the real complete_delay body to zero delay.wait_value.";
    EXPECT_FALSE(IS_SET(character.specials.affected_by, AFF_WAITWHEEL));
    EXPECT_FALSE(IS_SET(character.specials.affected_by, AFF_WAITING));
}

TEST(CommDelay, CompleteDelayDefaultsToANoOpWhenUnregistered)
{
    ScopedOutputSinks unregistered_sinks;
    char_data character {};
    character.delay.cmd = 0;
    character.delay.wait_value = 7;
    character.specials.affected_by = AFF_WAITWHEEL | AFF_WAITING;

    complete_delay(&character);

    EXPECT_EQ(character.delay.wait_value, 7)
        << "Expected an unregistered complete_delay sink to leave delay.wait_value untouched.";
    EXPECT_TRUE(IS_SET(character.specials.affected_by, AFF_WAITWHEEL));
    EXPECT_TRUE(IS_SET(character.specials.affected_by, AFF_WAITING));
}

// get_from_txt_block_pool(std::string_view) (comm.cpp) -- real body copies a
// bounded, truncate_at_null()-normalized view into a pool-allocated
// txt_block. Released back to the pool with put_to_txt_block_pool() after
// the assertion (mirrors comm_output_tests.cpp's QueueCopiesABoundedMessage
// BeforeCallerStorageChanges cleanup) so this test does not leak.

TEST(CommDelay, GetFromTxtBlockPoolReachesTheRealSinkWhenRegistered)
{
    txt_block* block = get_from_txt_block_pool(std::string_view("hello"));

    ASSERT_NE(block, nullptr)
        << "Expected the real get_from_txt_block_pool body to return a pool-allocated block.";
    EXPECT_STREQ(block->text, "hello");

    put_to_txt_block_pool(block);
}

TEST(CommDelay, GetFromTxtBlockPoolDefaultsToNullWhenUnregistered)
{
    ScopedOutputSinks unregistered_sinks;

    EXPECT_EQ(get_from_txt_block_pool(std::string_view("hello")), nullptr)
        << "Expected an unregistered get_from_txt_block_pool sink to return null rather than "
           "abort -- this seam's own 'logged no-op' taxonomy, deliberately not the abort() "
           "entity_hooks.h's twin no-arg-overload hook uses (see output_seam.cpp's forwarder "
           "comment for the reasoning).";
}
