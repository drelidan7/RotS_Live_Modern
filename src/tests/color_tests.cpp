#include "../color.h"
#include "../interpre.h"
#include "../structs.h"
#include "../utils.h"
#include "test_char_cleanup.h"

#include <gtest/gtest.h>

#include <array>
#include <concepts>
#include <string_view>
#include <type_traits>

static_assert(std::same_as<std::remove_cvref_t<decltype(color_sequence[0])>, std::string_view>);

#include <cstring>
#include <string>

void clear_char(struct char_data* ch, int mode);
ACMD(do_color);

TEST(Color, FindsFieldsAndAnsiColorsFromBoundedViews)
{
    const std::array<char, 5> field_storage { 'm', 'a', 'g', 'i', 'c' };
    const std::string_view field_name(field_storage.data(), field_storage.size());
    constexpr std::string_view embedded_color("cyan\0ignored", 12);

    EXPECT_EQ(find_color_field(field_name), COLOR_MAGIC);
    EXPECT_EQ(find_ansi_color(embedded_color), CCYN);
    EXPECT_EQ(find_color_field({}), -1);
    EXPECT_EQ(find_ansi_color("unknown"), -1);
}
// const char*, matching interpre.cpp's definition exactly (MSVC's decorated
// names encode the element type, so a mismatched extern is a hard LNK2001
// there; GCC/Clang linked it silently -- Phase 3 Task 6).
extern const char* command[];
extern command_info cmd_info[];
void assign_command_pointers(void);
int old_search_block(char* argument, int begin, unsigned int length, const char** list, int mode);

namespace {

void initialize_player_character(char_data* character)
{
    clear_char(character, MOB_VOID);
    SET_BIT(PRF_FLAGS(character), PRF_COLOR);
}

descriptor_data make_descriptor()
{
    descriptor_data descriptor {};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    return descriptor;
}

} // namespace

TEST(Color, RendersLegacyAnsiForegroundSelections)
{
    char_data character {};
    initialize_player_character(&character);
    // Releases character.profs/skills/knowledge (clear_char() heap
    // allocations, via initialize_player_character()) at scope exit
    // (Phase 5 T6 leak sweep).
    ScopedClearCharFields character_cleanup { character };
    set_colornum(&character, COLOR_CHAT, CMAG);

    EXPECT_EQ(std::string(get_color_sequence(&character, COLOR_CHAT)), std::string(color_sequence[CMAG]));
}

TEST(Color, RendersTrueColorForegroundSelections)
{
    char_data character {};
    initialize_player_character(&character);
    // Releases character.profs/skills/knowledge (clear_char() heap
    // allocations, via initialize_player_character()) at scope exit
    // (Phase 5 T6 leak sweep).
    ScopedClearCharFields character_cleanup { character };
    set_truecolor_foreground(&character, COLOR_MAGIC, 180, 80, 255);

    const std::string sequence = get_color_sequence(&character, COLOR_MAGIC);
    EXPECT_NE(sequence.find("\x1B[38;2;180;80;255m"), std::string::npos) << sequence;
}

TEST(Color, RendersTrueColorBackgroundSelectionsAlongsideAnsiForegroundFallback)
{
    char_data character {};
    initialize_player_character(&character);
    // Releases character.profs/skills/knowledge (clear_char() heap
    // allocations, via initialize_player_character()) at scope exit
    // (Phase 5 T6 leak sweep).
    ScopedClearCharFields character_cleanup { character };
    set_colornum(&character, COLOR_WEATHER, CBCYN);
    set_truecolor_background(&character, COLOR_WEATHER, 10, 20, 35);

    const std::string sequence = get_color_sequence(&character, COLOR_WEATHER);
    EXPECT_NE(sequence.find(color_sequence[CBCYN]), std::string::npos) << sequence;
    EXPECT_NE(sequence.find("\x1B[48;2;10;20;35m"), std::string::npos) << sequence;
}

TEST(Color, ClearsBackgroundWithoutAffectingForegroundSelection)
{
    char_data character {};
    initialize_player_character(&character);
    // Releases character.profs/skills/knowledge (clear_char() heap
    // allocations, via initialize_player_character()) at scope exit
    // (Phase 5 T6 leak sweep).
    ScopedClearCharFields character_cleanup { character };
    set_truecolor_foreground(&character, COLOR_MAGIC, 180, 80, 255);
    set_truecolor_background(&character, COLOR_MAGIC, 10, 20, 35);

    clear_color_background(&character, COLOR_MAGIC);

    const std::string sequence = get_color_sequence(&character, COLOR_MAGIC);
    EXPECT_NE(sequence.find("\x1B[38;2;180;80;255m"), std::string::npos) << sequence;
    EXPECT_EQ(sequence.find("\x1B[48;2;10;20;35m"), std::string::npos) << sequence;
}

TEST(Color, MapsKnownTrueColorToNearestAnsiFallback)
{
    EXPECT_EQ(nearest_ansi_color(180, 80, 255), CBMAG);
    EXPECT_EQ(nearest_ansi_color(10, 20, 35), CNRM);
}

TEST(Color, LegacyCommandSyntaxStillSetsAnsiForegroundSelection)
{
    char_data character {};
    descriptor_data descriptor = make_descriptor();
    // Re-point output to THIS object's own small_outbuf: make_descriptor()
    // returns by value and its internal `output = &small_outbuf` self-pointer
    // dangles into the returned-from frame when NRVO isn't applied (MSVC
    // Debug) -- writes would otherwise corrupt freed stack. Phase 3 Task 6.
    descriptor.output = descriptor.small_outbuf;
    initialize_player_character(&character);
    // Releases character.profs/skills/knowledge (clear_char() heap
    // allocations, via initialize_player_character()) at scope exit
    // (Phase 5 T6 leak sweep).
    ScopedClearCharFields character_cleanup { character };
    character.desc = &descriptor;

    char command[] = "magic bright magenta";
    do_color(&character, command, nullptr, 0, 0);

    EXPECT_EQ(get_colornum(&character, COLOR_MAGIC), CBMAG);
    EXPECT_EQ(character.profs->color_settings[COLOR_MAGIC].foreground.mode, COLOR_VALUE_ANSI16);
    EXPECT_EQ(character.profs->color_settings[COLOR_MAGIC].foreground.ansi, CBMAG);
    EXPECT_NE(std::string(descriptor.output).find("You colour magic"), std::string::npos);
}

TEST(Color, CommandSupportsTrueColorForegroundRgbSelection)
{
    char_data character {};
    descriptor_data descriptor = make_descriptor();
    // Re-point output to THIS object's own small_outbuf: make_descriptor()
    // returns by value and its internal `output = &small_outbuf` self-pointer
    // dangles into the returned-from frame when NRVO isn't applied (MSVC
    // Debug) -- writes would otherwise corrupt freed stack. Phase 3 Task 6.
    descriptor.output = descriptor.small_outbuf;
    initialize_player_character(&character);
    // Releases character.profs/skills/knowledge (clear_char() heap
    // allocations, via initialize_player_character()) at scope exit
    // (Phase 5 T6 leak sweep).
    ScopedClearCharFields character_cleanup { character };
    character.desc = &descriptor;

    char command[] = "magic fg rgb 180 80 255";
    do_color(&character, command, nullptr, 0, 0);

    EXPECT_EQ(get_colornum(&character, COLOR_MAGIC), CBMAG);
    EXPECT_EQ(character.profs->color_settings[COLOR_MAGIC].foreground.mode, COLOR_VALUE_TRUECOLOR);
    EXPECT_EQ(character.profs->color_settings[COLOR_MAGIC].foreground.red, 180);
    EXPECT_EQ(character.profs->color_settings[COLOR_MAGIC].foreground.green, 80);
    EXPECT_EQ(character.profs->color_settings[COLOR_MAGIC].foreground.blue, 255);
    EXPECT_NE(std::string(descriptor.output).find("#B450FF"), std::string::npos);
}

TEST(Color, CommandSupportsTrueColorBackgroundHexSelection)
{
    char_data character {};
    descriptor_data descriptor = make_descriptor();
    // Re-point output to THIS object's own small_outbuf: make_descriptor()
    // returns by value and its internal `output = &small_outbuf` self-pointer
    // dangles into the returned-from frame when NRVO isn't applied (MSVC
    // Debug) -- writes would otherwise corrupt freed stack. Phase 3 Task 6.
    descriptor.output = descriptor.small_outbuf;
    initialize_player_character(&character);
    // Releases character.profs/skills/knowledge (clear_char() heap
    // allocations, via initialize_player_character()) at scope exit
    // (Phase 5 T6 leak sweep).
    ScopedClearCharFields character_cleanup { character };
    character.desc = &descriptor;

    char command[] = "weather bg hex #0A1423";
    do_color(&character, command, nullptr, 0, 0);

    EXPECT_EQ(character.profs->color_settings[COLOR_WEATHER].background.mode, COLOR_VALUE_TRUECOLOR);
    EXPECT_EQ(character.profs->color_settings[COLOR_WEATHER].background.ansi, CNRM);
    EXPECT_EQ(character.profs->color_settings[COLOR_WEATHER].background.red, 10);
    EXPECT_EQ(character.profs->color_settings[COLOR_WEATHER].background.green, 20);
    EXPECT_EQ(character.profs->color_settings[COLOR_WEATHER].background.blue, 35);
    EXPECT_NE(std::string(descriptor.output).find("#0A1423"), std::string::npos);
}

TEST(Color, CommandCanClearBackgroundToDefault)
{
    char_data character {};
    descriptor_data descriptor = make_descriptor();
    // Re-point output to THIS object's own small_outbuf: make_descriptor()
    // returns by value and its internal `output = &small_outbuf` self-pointer
    // dangles into the returned-from frame when NRVO isn't applied (MSVC
    // Debug) -- writes would otherwise corrupt freed stack. Phase 3 Task 6.
    descriptor.output = descriptor.small_outbuf;
    initialize_player_character(&character);
    // Releases character.profs/skills/knowledge (clear_char() heap
    // allocations, via initialize_player_character()) at scope exit
    // (Phase 5 T6 leak sweep).
    ScopedClearCharFields character_cleanup { character };
    character.desc = &descriptor;
    set_truecolor_background(&character, COLOR_MAGIC, 10, 20, 35);

    char command[] = "magic bg default";
    do_color(&character, command, nullptr, 0, 0);

    EXPECT_EQ(character.profs->color_settings[COLOR_MAGIC].background.mode, COLOR_VALUE_DEFAULT);
    EXPECT_EQ(character.profs->color_settings[COLOR_MAGIC].background.ansi, CNRM);
    EXPECT_NE(std::string(descriptor.output).find("background"), std::string::npos);
}

TEST(Color, CommandListsStructuredForegroundAndBackgroundSelections)
{
    char_data character {};
    descriptor_data descriptor = make_descriptor();
    // Re-point output to THIS object's own small_outbuf: make_descriptor()
    // returns by value and its internal `output = &small_outbuf` self-pointer
    // dangles into the returned-from frame when NRVO isn't applied (MSVC
    // Debug) -- writes would otherwise corrupt freed stack. Phase 3 Task 6.
    descriptor.output = descriptor.small_outbuf;
    initialize_player_character(&character);
    // Releases character.profs/skills/knowledge (clear_char() heap
    // allocations, via initialize_player_character()) at scope exit
    // (Phase 5 T6 leak sweep).
    ScopedClearCharFields character_cleanup { character };
    // do_color's structured listing overflows descriptor.small_outbuf,
    // promoting it to a heap-allocated large_outbuf block (Phase 5 T6 leak
    // sweep; see test_char_cleanup.h).
    ScopedDescriptorLargeOutbufReturn descriptor_large_outbuf_cleanup { descriptor };
    character.desc = &descriptor;
    set_truecolor_foreground(&character, COLOR_MAGIC, 180, 80, 255);
    set_truecolor_background(&character, COLOR_WEATHER, 10, 20, 35);

    char command[] = "";
    do_color(&character, command, nullptr, 0, 0);

    const std::string output = descriptor.output;
    EXPECT_NE(output.find("magic"), std::string::npos);
    EXPECT_NE(output.find("fg #B450FF"), std::string::npos) << output;
    EXPECT_NE(output.find("weather"), std::string::npos);
    EXPECT_NE(output.find("bg #0A1423"), std::string::npos) << output;
}

TEST(Color, CommandRejectsOutOfRangeRgbValues)
{
    char_data character {};
    descriptor_data descriptor = make_descriptor();
    // Re-point output to THIS object's own small_outbuf: make_descriptor()
    // returns by value and its internal `output = &small_outbuf` self-pointer
    // dangles into the returned-from frame when NRVO isn't applied (MSVC
    // Debug) -- writes would otherwise corrupt freed stack. Phase 3 Task 6.
    descriptor.output = descriptor.small_outbuf;
    initialize_player_character(&character);
    // Releases character.profs/skills/knowledge (clear_char() heap
    // allocations, via initialize_player_character()) at scope exit
    // (Phase 5 T6 leak sweep).
    ScopedClearCharFields character_cleanup { character };
    character.desc = &descriptor;

    char command[] = "magic fg rgb 300 80 255";
    do_color(&character, command, nullptr, 0, 0);

    EXPECT_NE(std::string(descriptor.output).find("RGB values must be between 0 and 255"), std::string::npos);
    EXPECT_NE(character.profs->color_settings[COLOR_MAGIC].foreground.mode, COLOR_VALUE_TRUECOLOR);
}

// Characterization pins for Phase 4 Wave 1 Task 5 (std::format conversion).
// These pin the exact bytes `do_color` sends for its three "invalid input"
// listing branches BEFORE the sprintf(buf, "%s %s", buf, ...)/strcat(buf, ...)
// chains that build them are converted to std::format composition. All three
// pre-conversion sprintf calls alias `buf` as both source and destination
// (undefined behavior in the strict sense, though glibc's implementation
// happens to produce the intended left-to-right accumulation) -- the
// conversion must reproduce these exact bytes without relying on that
// aliasing trick.
TEST(Color, UnknownFieldNameListsAllValidFieldsWithLeadingSpaceAndCrlf)
{
    char_data character {};
    descriptor_data descriptor = make_descriptor();
    descriptor.output = descriptor.small_outbuf;
    initialize_player_character(&character);
    // Releases character.profs/skills/knowledge (clear_char() heap
    // allocations, via initialize_player_character()) at scope exit
    // (Phase 5 T6 leak sweep).
    ScopedClearCharFields character_cleanup { character };
    character.desc = &descriptor;

    char command[] = "bogus-field";
    do_color(&character, command, nullptr, 0, 0);

    const std::string output = descriptor.output;
    EXPECT_EQ(output,
        "Possible arguments are:\n\r"
        " narrate chat yell tell say roomname hit damage character object enemy description group magic weather off on default\n\r"
        "Usage:\n\r"
        "  color <slot> <ansi colour>\n\r"
        "  color <slot> fg ansi <ansi colour>\n\r"
        "  color <slot> fg rgb <red> <green> <blue>\n\r"
        "  color <slot> fg hex #RRGGBB\n\r"
        "  color <slot> fg default\n\r"
        "  color <slot> bg ansi <ansi colour>\n\r"
        "  color <slot> bg rgb <red> <green> <blue>\n\r"
        "  color <slot> bg hex #RRGGBB\n\r"
        "  color <slot> bg default\n\r")
        << output;
}

TEST(Color, UnknownAnsiColourNameInsideFgBranchListsEightBaseColours)
{
    char_data character {};
    descriptor_data descriptor = make_descriptor();
    descriptor.output = descriptor.small_outbuf;
    initialize_player_character(&character);
    // Releases character.profs/skills/knowledge (clear_char() heap
    // allocations, via initialize_player_character()) at scope exit
    // (Phase 5 T6 leak sweep).
    ScopedClearCharFields character_cleanup { character };
    character.desc = &descriptor;

    char command[] = "magic fg ansi bogus-colour";
    do_color(&character, command, nullptr, 0, 0);

    const std::string output = descriptor.output;
    EXPECT_EQ(output,
        "Possible colours are:"
        " normal red green yellow blue magenta cyan white.\r\n"
        "Additionally, you may prefix any of the above colours with 'bright'.\r\n")
        << output;
}

TEST(Color, UnknownAnsiColourNameAtTopLevelListsEightBaseColours)
{
    char_data character {};
    descriptor_data descriptor = make_descriptor();
    descriptor.output = descriptor.small_outbuf;
    initialize_player_character(&character);
    // Releases character.profs/skills/knowledge (clear_char() heap
    // allocations, via initialize_player_character()) at scope exit
    // (Phase 5 T6 leak sweep).
    ScopedClearCharFields character_cleanup { character };
    character.desc = &descriptor;

    char command[] = "magic bogus-colour";
    do_color(&character, command, nullptr, 0, 0);

    const std::string output = descriptor.output;
    EXPECT_EQ(output,
        "Possible colours are:"
        " normal red green yellow blue magenta cyan white.\r\n"
        "Additionally, you may prefix any of the above colours with 'bright'.\r\n")
        << output;
}

TEST(Color, InterpreterAcceptsColorAsAliasForColour)
{
    assign_command_pointers();

    char american_spelling[] = "color";
    char british_spelling[] = "colour";
    const int color_command = old_search_block(american_spelling, 0, std::strlen(american_spelling), const_cast<const char**>(command), 0);
    const int colour_command = old_search_block(british_spelling, 0, std::strlen(british_spelling), const_cast<const char**>(command), 0);

    ASSERT_GE(color_command, 0);
    ASSERT_GE(colour_command, 0);
    EXPECT_EQ(cmd_info[color_command].command_pointer, cmd_info[colour_command].command_pointer);
    EXPECT_EQ(cmd_info[color_command].command_pointer, &do_color);
}
