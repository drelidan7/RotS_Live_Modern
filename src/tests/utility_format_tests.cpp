// Characterization tests for Phase 4 Wave 1 Task 5 (std::format conversion
// pattern) covering the utility.cpp sites converted in this wave:
// mudlog/log_death_trap (message-wrapping sprintf), sprintbit/sprinttype
// (strcpy/strcat chains building a bitfield/enum description into a
// caller-owned buffer), and day_to_str (ordinal-day formatting). Each test
// pins the CURRENT byte-for-byte output before conversion; per the wave's
// binding pattern, these must PASS against the unmodified code first (that
// pass IS the pin), then stay green after the sprintf/strcpy/strcat call
// sites are converted to std::format.

#include "rots/core/character.h"
#include "rots/core/room.h"
#include "rots/core/descriptor.h"
#include "rots/core/tables.h"
#include "rots/core/types.h"
#include "../comm.h"
#include "../utils.h"
#include "test_world.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <string_view>

extern struct descriptor_data* descriptor_list;
namespace {

// Mirrors ScopedDescriptorListReset (interpre_account_menu_tests.cpp) --
// swaps the process-global descriptor_list for the duration of a test and
// restores it on scope exit, so the mudlog broadcast sink's descriptor_list
// walk has exactly the listeners a test wants and nothing left over for the
// next test.
class ScopedDescriptorListReset {
public:
    ScopedDescriptorListReset()
        : previous_(descriptor_list)
    {
        descriptor_list = nullptr;
    }

    ~ScopedDescriptorListReset()
    {
        descriptor_list = previous_;
    }

    ScopedDescriptorListReset(const ScopedDescriptorListReset&) = delete;
    ScopedDescriptorListReset& operator=(const ScopedDescriptorListReset&) = delete;

private:
    descriptor_data* previous_;
};

descriptor_data make_listening_descriptor(char_data* character)
{
    descriptor_data descriptor {};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = 0; // CON_PLAYING
    descriptor.character = character;
    return descriptor;
}

// A level-100, log-flagged, colour-off player character wired up as the
// sole descriptor_list listener: satisfies the registered mudlog broadcast
// sink's (comm.cpp) `!i->connected && !PLR_FLAGGED(..., PLR_WRITING)` gate
// and its `GET_LEVEL(i->character) >= level` / `tp >= type` thresholds
// (level gets floored to LEVEL_AREAGOD == 95 inside that sink) for every
// type/level this test file passes. Colour stays off (PRF_COLOR unset) so
// CC_FIX/CC_NORM contribute no escape bytes, keeping the pinned strings
// plain text.
struct MudlogListenerContext {
    char_data character {};
    descriptor_data descriptor;

    MudlogListenerContext()
        : descriptor(make_listening_descriptor(&character))
    {
        character.desc = &descriptor;
        character.player.level = 100;
        character.specials2.pref = PRF_LOG1 | PRF_LOG2 | PRF_LOG3;
    }
};

} // namespace

TEST(UtilityFormat, MudlogWrapsMessageInBracketsForQualifyingListeners)
{
    ScopedDescriptorListReset descriptor_list_reset;
    register_mudlog_broadcast_sink();
    MudlogListenerContext listener;
    descriptor_list = &listener.descriptor;

    char message[] = "a plain log line";
    mudlog(message, BRF, LEVEL_GOD, FALSE);

    EXPECT_EQ(std::string(listener.descriptor.output), "[ a plain log line ]\n\r");
}

TEST(UtilityFormat, MudlogPreservesEmptyMessageBody)
{
    ScopedDescriptorListReset descriptor_list_reset;
    register_mudlog_broadcast_sink();
    MudlogListenerContext listener;
    descriptor_list = &listener.descriptor;

    char message[] = "";
    mudlog(message, BRF, LEVEL_GOD, FALSE);

    EXPECT_EQ(std::string(listener.descriptor.output), "[  ]\n\r");
}

TEST(UtilityFormat, MudlogTreatsBracesInsideMessageAsLiteralText)
{
    // Edge case for the std::format conversion specifically: the message is
    // an *argument*, never the format string itself, so literal `{`/`}`
    // bytes inside it (which std::format would otherwise treat as
    // replacement-field syntax if misused as the format string) must pass
    // through unchanged.
    ScopedDescriptorListReset descriptor_list_reset;
    register_mudlog_broadcast_sink();
    MudlogListenerContext listener;
    descriptor_list = &listener.descriptor;

    char message[] = "value is {0} and {}";
    mudlog(message, BRF, LEVEL_GOD, FALSE);

    EXPECT_EQ(std::string(listener.descriptor.output), "[ value is {0} and {} ]\n\r");
}

TEST(UtilityFormat, MudlogAcceptsANonNullTerminatedSlice)
{
    ScopedDescriptorListReset descriptor_list_reset;
    register_mudlog_broadcast_sink();
    MudlogListenerContext listener;
    descriptor_list = &listener.descriptor;
    const std::string storage = "prefix-message-suffix";

    mudlog(std::string_view(storage).substr(7, 7), BRF, LEVEL_GOD, FALSE);

    EXPECT_STREQ(listener.descriptor.output, "[ message ]\n\r");
}

TEST(UtilityFormat, MudlogTruncatesAViewAtAnEmbeddedNull)
{
    ScopedDescriptorListReset descriptor_list_reset;
    register_mudlog_broadcast_sink();
    MudlogListenerContext listener;
    descriptor_list = &listener.descriptor;
    const char storage[] = { 'o', 'k', '\0', 'n', 'o' };

    mudlog(std::string_view(storage, sizeof(storage)), BRF, LEVEL_GOD, FALSE);

    EXPECT_STREQ(listener.descriptor.output, "[ ok ]\n\r");
}

TEST(UtilityFormat, MobLogHelpersAcceptBoundedAndEmbeddedNullText)
{
    ScopedDescriptorListReset descriptor_list_reset;
    register_mudlog_broadcast_sink();
    MudlogListenerContext listener;
    descriptor_list = &listener.descriptor;

    char_data mobile {};
    mobile.player.name = const_cast<char*>("debug progdebug");
    const std::string message_storage = "prefix-bounded-suffix";
    mudlog_debug_mob(std::string_view(message_storage).substr(7, 7), &mobile);
    EXPECT_STREQ(listener.descriptor.output, "[ bounded ]\n\r");

    listener.descriptor.output[0] = '\0';
    listener.descriptor.bufptr = 0;
    listener.descriptor.bufspace = SMALL_BUFSIZE - 1;
    const char alias_storage[] = { 'p', 'r', 'o', 'g', 'd', 'e', 'b', 'u', 'g', '\0', 'x' };
    const char log_storage[] = { 'o', 'k', '\0', 'n', 'o' };
    mudlog_aliased_mob(std::string_view(log_storage, sizeof(log_storage)), &mobile,
        std::string_view(alias_storage, sizeof(alias_storage)));
    EXPECT_STREQ(listener.descriptor.output, "[ ok ]\n\r");
}

TEST(UtilityFormat, HasAliasAcceptsBoundedAndEmbeddedNullKeywords)
{
    char_data mobile {};
    mobile.player.name = const_cast<char*>("spells p_hide");
    const std::string keyword_storage = "p_hide-ignored";
    EXPECT_EQ(has_alias(&mobile, std::string_view(keyword_storage).substr(0, 6)), 1);

    const char embedded_keyword[] = { 's', 'p', 'e', 'l', 'l', 's', '\0', 'x' };
    EXPECT_EQ(has_alias(&mobile, std::string_view(embedded_keyword, sizeof(embedded_keyword))), 1);
}

TEST(UtilityFormat, CreateFunctionAcceptsBoundedFileViewsOnSuccessfulAllocation)
{
    const std::string file_storage = "bounded.cpp-ignored";
    void* bounded_allocation = create_function(1, 1, 42,
        std::string_view(file_storage).substr(0, 11));
    ASSERT_NE(bounded_allocation, nullptr);
    free_function(bounded_allocation);

    const char embedded_file[] = { 'f', 'i', 'l', 'e', '.', 'c', 'p', 'p', '\0', 'x' };
    void* embedded_allocation = create_function(1, 1, 42,
        std::string_view(embedded_file, sizeof(embedded_file)));
    ASSERT_NE(embedded_allocation, nullptr);
    free_function(embedded_allocation);
}

TEST(UtilityFormat, LogWritesOnlyTheSelectedViewToStderr)
{
    const std::string storage = "prefix-message-suffix";
    testing::internal::CaptureStderr();

    log(std::string_view(storage).substr(7, 7));

    const std::string output = testing::internal::GetCapturedStderr();
    EXPECT_TRUE(output.ends_with(" :: message\n"));
    EXPECT_EQ(output.find("suffix"), std::string::npos);
}

TEST(UtilityFormat, LogTruncatesAViewAtAnEmbeddedNull)
{
    const char storage[] = { 'o', 'k', '\0', 'n', 'o' };
    testing::internal::CaptureStderr();

    log(std::string_view(storage, sizeof(storage)));

    const std::string output = testing::internal::GetCapturedStderr();
    EXPECT_TRUE(output.ends_with(" :: ok\n"));
    EXPECT_EQ(output.find("no"), std::string::npos);
}

TEST(UtilityFormat, LogDeathTrapReportsCharacterNameRoomNumberAndName)
{
    ScopedDescriptorListReset descriptor_list_reset;
    register_mudlog_broadcast_sink();
    ScopedTestWorld test_world;
    test_world.room().number = 4207;

    MudlogListenerContext listener;
    descriptor_list = &listener.descriptor;

    char_data victim {};
    victim.specials2.act = 0; // PC, not NPC -- GET_NAME resolves to player.name
    victim.player.name = const_cast<char*>("Frodo");
    victim.in_room = 0;

    log_death_trap(&victim);

    EXPECT_EQ(std::string(listener.descriptor.output),
        "[ Frodo hit death trap #4207 (The Testing Meadow) ]\n\r");
}

namespace {

} // namespace

TEST(UtilityFormat, SprintbitReportsErrorForNegativeVector)
{
    const std::string_view names[] = { "alpha", "\n" };
    char result[64];

    sprintbit(-1, names, result, 0);

    EXPECT_STREQ(result, "SPRINTBIT ERROR!");
}

TEST(UtilityFormat, SprintbitReportsNoneOrNoAdditionalAttributesForZeroVector)
{
    const std::string_view names[] = { "alpha", "\n" };
    char result[64];

    sprintbit(0, names, result, 0);
    EXPECT_STREQ(result, "<NONE>");

    sprintbit(0, names, result, 1);
    EXPECT_STREQ(result, "has no additional attributes. ");
}

TEST(UtilityFormat, SprintbitNormalModeJoinsSetFlagsWithLeadingSpaces)
{
    const std::string_view names[] = { "alpha", "beta", "\n" };
    char result[64];

    sprintbit(0x3 /* alpha | beta */, names, result, 0);

    EXPECT_STREQ(result, " alpha beta.");
}

TEST(UtilityFormat, SprintbitIdentifyModeTwoJoinsSetFlagsWithAnd)
{
    const std::string_view names[] = { "alpha", "beta", "\n" };
    char result[64];

    sprintbit(0x3 /* alpha | beta */, names, result, 2);

    EXPECT_STREQ(result, " alpha and beta.");
}

TEST(UtilityFormat, SprintbitIdentifyModeOneListsAttributesOnePerLine)
{
    const std::string_view names[] = { "alpha", "beta", "\n" };
    char result[64];

    sprintbit(0x3 /* alpha | beta */, names, result, 1);

    EXPECT_STREQ(result, "has the following attributes.\r\nalpha.\r\nbeta.");
}

TEST(UtilityFormat, SprintbitEmitsUndefineForReservedBitsWithoutALeadingSpace)
{
    // Bit 0 (alpha) is named; bit 2 has no defined name ("\n" placeholder),
    // exercising the "UNDEFINE " branch -- which (unlike the named-flag
    // branch) appends no separating space of its own. Pinning this exact,
    // slightly-inconsistent spacing is the point: the conversion must not
    // "fix" it.
    const std::string_view names[] = { "alpha", "beta", "\n" };
    char result[64];

    sprintbit(0x5 /* alpha | (undefined bit 2) */, names, result, 0);

    EXPECT_STREQ(result, " alphaUNDEFINE .");
}

TEST(UtilityFormat, SprinttypeReturnsUndefinedForOutOfRangeType)
{
    const std::string_view names[] = { "first", "second", "\n" };
    char result[64];

    sprinttype(0, names, result);
    EXPECT_STREQ(result, "first");

    sprinttype(1, names, result);
    EXPECT_STREQ(result, "second");

    sprinttype(5, names, result);
    EXPECT_STREQ(result, "UNDEFINED");
}

TEST(UtilityFormat, DayToStrFormatsOrdinalDayAndMonthName)
{
    char str[64];
    time_info_data time_info {};

    time_info.day = 0; // 1st
    time_info.month = 0;
    day_to_str(&time_info, str);
    EXPECT_EQ(std::string(str), "the 1st day of " + std::string(month_name[0]));

    time_info.day = 1; // 2nd
    day_to_str(&time_info, str);
    EXPECT_EQ(std::string(str), "the 2nd day of " + std::string(month_name[0]));

    time_info.day = 10; // 11th -- the 11/12/13 special case (not "11st")
    day_to_str(&time_info, str);
    EXPECT_EQ(std::string(str), "the 11th day of " + std::string(month_name[0]));

    time_info.day = 20; // 21st
    time_info.month = 1;
    day_to_str(&time_info, str);
    EXPECT_EQ(std::string(str), "the 21st day of " + std::string(month_name[1]));
}
