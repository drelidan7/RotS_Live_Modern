#include "../comm.h"
#include "../handler.h"
#include "../interpre.h"
#include "../spells.h"
#include "rots/core/character.h"
#include "rots/core/object.h"
#include "rots/core/descriptor.h"
#include "rots/core/types.h"
#include "../utils.h"
#include "test_world.h"

#include <gtest/gtest.h>

#include <string>

// Characterization tests pinning the EXACT observable bytes of build_prompt()
// (comm.cpp) and, transitively, add_prompt() (act_info.cpp). Task 7 extracted
// build_prompt from the game loop; Task 9 will rewrite the char*-buffer
// self-copies in BOTH build_prompt and add_prompt. These tests are the safety
// net for that rewrite: each pins one prompt-composition branch byte-for-byte
// and asserts the pinned string contains the branch's signature token, so a
// fixture that silently skips a branch cannot masquerade as coverage.
//
// Method: capture-and-pin. add_prompt formats HP/mana/move via message-table
// lookups (consts.cpp prompt_hit[]/prompt_mana[]/prompt_move[]/prompt_mount[])
// keyed on health/mana/move percentages; the expected strings below were
// captured from the CURRENT (unoptimized) code and are correct by definition
// (we are preserving current behavior, not deriving it).
//
// Deliberately NOT pinned here (reported to Task 9 as an unpinned branch): the
// fully-default prompt where pref == 0 and every pool is full. That state
// leaves the local `prompt` buffer empty, and build_prompt's terminator block
// then evaluates `prompt[strlen(prompt) - 1]` == prompt[-1], a 1-byte
// stack-buffer underflow read that AddressSanitizer would trap. Every state
// below deliberately produces a non-empty prompt core (a leading-space token
// that build_prompt's pptr++ trim consumes), so the ">" / "]" terminator is
// still pinned without triggering that pre-existing underflow. See the task-8
// report for the finding.

namespace {

// A single connected PC whose descriptor feeds build_prompt. build_prompt only
// reads point->character, so the descriptor needs nothing beyond that back
// pointer; no room/socket wiring is required for the non-combat branches.
struct PromptContext {
    // The player whose state build_prompt renders into a prompt string.
    char_data character {};
    // The descriptor build_prompt is called with; only its `character` back
    // pointer is read by the prompt path.
    descriptor_data descriptor {};

    PromptContext()
    {
        descriptor.character = &character;
        // A non-NPC PC (MOB_ISNPC clear) with a full, healthy pool baseline;
        // individual tests override the pieces their branch depends on.
        character.player.race = RACE_HUMAN;
        character.player.name = const_cast<char*>("Frodo");
        character.specials.position = POSITION_STANDING;
        character.abilities.hit = 100;
        character.tmpabilities.hit = 100;
        character.abilities.mana = 100;
        character.tmpabilities.mana = 100;
        character.abilities.move = 100;
        character.tmpabilities.move = 100;
    }
};

} // namespace

// build_prompt case: a normal (non-advanced) prompt for a standing, non-fighting
// PC ends in the ">" terminator. tmpabilities.move == 20/100 renders a non-empty
// " MV:Tired" core (200/1000 -> prompt_move[3]); the leading space is trimmed.
TEST(PromptFormat, NormalPromptTerminatesWithGreaterThan)
{
    PromptContext context;
    SET_BIT(context.character.specials2.pref, PRF_PROMPT);
    context.character.tmpabilities.move = 20;

    std::string out;
    build_prompt(&context.descriptor, out);

    EXPECT_EQ(out, "MV:Tired>");
    // Signature: normal prompt ends with ">", never the shaping "]".
    ASSERT_FALSE(out.empty());
    EXPECT_EQ(out.back(), '>');
    EXPECT_EQ(out.find(']'), std::string::npos);
}

// build_prompt case: a positive GET_INVIS_LEV prefixes the prompt with "i<N>".
TEST(PromptFormat, InvisLevelPrefixesLowercaseI)
{
    PromptContext context;
    SET_BIT(context.character.specials2.pref, PRF_PROMPT);
    context.character.specials.invis_level = 5;
    context.character.tmpabilities.move = 20;

    std::string out;
    build_prompt(&context.descriptor, out);

    EXPECT_EQ(out, "i5 MV:Tired>");
    // Signature: the invis head "i5" leads the prompt (no leading-space trim,
    // since "i" is not a space).
    EXPECT_EQ(out.rfind("i5", 0), 0u);
}

// build_prompt case: PRF_PROMPT with GET_HIT < GET_MAX_HIT renders the normal
// " HP:" label plus the prompt_hit[] health word (450/1000 -> "Wounded").
TEST(PromptFormat, HpBranchRendersHealthLabel)
{
    PromptContext context;
    SET_BIT(context.character.specials2.pref, PRF_PROMPT);
    context.character.tmpabilities.hit = 45;

    std::string out;
    build_prompt(&context.descriptor, out);

    EXPECT_EQ(out, "HP:Wounded>");
    // Signature: the HP branch's "HP:" label.
    EXPECT_NE(out.find("HP:"), std::string::npos);
}

// build_prompt/add_prompt case: PRF_ADVANCED_PROMPT renders the "[HP: h/H
// S: s/S MV: m/M]" pool block (the PROMPT_ADVANCED self-copy in add_prompt),
// terminated by "]" from the block and then ">" from build_prompt.
TEST(PromptFormat, AdvancedPromptRendersPoolBlock)
{
    PromptContext context;
    SET_BIT(context.character.specials2.pref, PRF_ADVANCED_PROMPT);
    context.character.tmpabilities.hit = 45;
    context.character.tmpabilities.mana = 60;
    context.character.tmpabilities.move = 70;

    std::string out;
    build_prompt(&context.descriptor, out);

    EXPECT_EQ(out, "[HP: 45/100 S: 60/100 MV: 70/100]>");
    // Signature: the advanced block's "HP: "/"MV: " labels and its "]" close.
    EXPECT_NE(out.find("HP: "), std::string::npos);
    EXPECT_NE(out.find("MV: "), std::string::npos);
    EXPECT_NE(out.find(']'), std::string::npos);
}

// build_prompt case: a mental opponent (fighting, but position not FIGHTING, so
// only the Mind branch fires) appends " Mind:" plus report_char_mentals of THIS
// character; equal base/current stats yield "in top shape".
TEST(PromptFormat, MentalOpponentRendersMindLabel)
{
    PromptContext context;
    SET_BIT(context.character.specials2.pref, PRF_PROMPT);
    context.character.tmpabilities.move = 20;
    // Equal base/current stats across the board -> "in top shape".
    context.character.abilities.str = 50;
    context.character.tmpabilities.str = 50;
    context.character.abilities.intel = 50;
    context.character.tmpabilities.intel = 50;
    context.character.abilities.wil = 50;
    context.character.tmpabilities.wil = 50;
    context.character.abilities.dex = 50;
    context.character.tmpabilities.dex = 50;
    context.character.abilities.con = 50;
    context.character.tmpabilities.con = 50;
    context.character.abilities.lea = 50;
    context.character.tmpabilities.lea = 50;

    // A mental opponent: a PC with PRF_MENTAL that this character is fighting.
    char_data opponent {};
    opponent.player.race = RACE_HUMAN;
    SET_BIT(opponent.specials2.pref, PRF_MENTAL);
    context.character.specials.fighting = &opponent;

    std::string out;
    build_prompt(&context.descriptor, out);

    EXPECT_EQ(out, "HP: MV:Tired Mind:in top shape>");
    // Signature: the mental-opponent branch's "Mind:" label.
    EXPECT_NE(out.find("Mind:"), std::string::npos);
}

// build_prompt/add_prompt case: a Beorning under the MAUL buff renders " Maul:"
// plus the PROMPT_MAUL counter (duration 8 -> 8*10/2 == "40/1000").
TEST(PromptFormat, MaulBranchRendersMaulCounter)
{
    PromptContext context;
    SET_BIT(context.character.specials2.pref, PRF_PROMPT);
    context.character.player.race = RACE_BEORNING;
    context.character.tmpabilities.move = 20;

    // Maul buff: an affected_type of type SKILL_MAUL whose location is
    // APPLY_MAUL. build_prompt gates on location == APPLY_MAUL; add_prompt
    // PROMPT_MAUL renders duration*10/2 as "<n>/1000".
    affected_type maul_aff {};
    maul_aff.type = SKILL_MAUL;
    maul_aff.location = APPLY_MAUL;
    maul_aff.duration = 8;
    maul_aff.next = nullptr;
    context.character.affected = &maul_aff;

    std::string out;
    build_prompt(&context.descriptor, out);

    EXPECT_EQ(out, "MV:Tired Maul:40/1000>");
    // Signature: the maul branch's "Maul:" label.
    EXPECT_NE(out.find("Maul:"), std::string::npos);
}

// build_prompt/add_prompt case: a quiver worn on WEAR_BACK renders " A:(" plus
// the PROMPT_ARROWS count of contained arrows followed by ")".
TEST(PromptFormat, ArrowsBranchRendersQuiverCount)
{
    PromptContext context;
    SET_BIT(context.character.specials2.pref, PRF_PROMPT);
    context.character.tmpabilities.move = 20;

    // A quiver (ITEM_CONTAINER named "quiver") holding three arrows.
    obj_data arrow_a {};
    obj_data arrow_b {};
    obj_data arrow_c {};
    arrow_a.next_content = &arrow_b;
    arrow_b.next_content = &arrow_c;
    arrow_c.next_content = nullptr;

    obj_data quiver {};
    quiver.name = const_cast<char*>("quiver");
    quiver.obj_flags.type_flag = ITEM_CONTAINER;
    quiver.contains = &arrow_a;
    context.character.equipment[WEAR_BACK] = &quiver;

    std::string out;
    build_prompt(&context.descriptor, out);

    EXPECT_EQ(out, "MV:Tired A:(3)>");
    // Signature: the arrows branch's "A:(" section.
    EXPECT_NE(out.find("A:("), std::string::npos);
}

// build_prompt/add_prompt case: a riding PC renders " R" plus the mount's move
// pool via add_prompt(mount, PROMPT_MOVE). A registered NPC mount (MOB_MOUNT)
// selects the prompt_mount[] table (300/1000 -> " Mount:Weary").
TEST(PromptFormat, RidingRendersMountAndMove)
{
    PromptContext context;
    SET_BIT(context.character.specials2.pref, PRF_PROMPT);
    context.character.tmpabilities.move = 20;

    char_data mount {};
    SET_BIT(mount.specials2.act, MOB_ISNPC);
    SET_BIT(mount.specials2.act, MOB_MOUNT);
    mount.abilities.move = 100;
    mount.tmpabilities.move = 30;
    const int mount_number = 4211;
    context.character.mount_data.mount = &mount;
    context.character.mount_data.mount_number = mount_number;
    // IS_RIDING needs both a non-null mount pointer and char_exists(number).
    set_char_exists(mount_number);

    std::string out;
    build_prompt(&context.descriptor, out);

    EXPECT_EQ(out, "R MV:Tired Mount:Weary>");
    // Signature: the riding "R" head and the mount's " Mount:" move label.
    EXPECT_EQ(out.rfind('R', 0), 0u);
    EXPECT_NE(out.find("Mount:"), std::string::npos);

    remove_char_exists(mount_number);
}

// build_prompt case: POSITION_SHAPING terminates the prompt with "]" instead of
// the normal ">".
TEST(PromptFormat, ShapingPositionTerminatesWithBracket)
{
    PromptContext context;
    SET_BIT(context.character.specials2.pref, PRF_PROMPT);
    context.character.tmpabilities.move = 20;
    context.character.specials.position = POSITION_SHAPING;

    std::string out;
    build_prompt(&context.descriptor, out);

    EXPECT_EQ(out, "MV:Tired]");
    // Signature: the shaping terminator "]" (never ">").
    ASSERT_FALSE(out.empty());
    EXPECT_EQ(out.back(), ']');
    EXPECT_EQ(out.find('>'), std::string::npos);
}

// build_prompt case: FIGHTING with a mutually-engaged opponent (opponent fights
// back, so there is no tank) renders ", <opponent>:" plus the opponent's health
// word via PERS + add_prompt(PROMPT_HIT).
TEST(PromptFormat, CombatOpponentRendersOpponentName)
{
    ScopedTestWorld world;
    PromptContext context;
    SET_BIT(context.character.specials2.pref, PRF_PROMPT);
    // PRF_HOLYLIGHT guarantees CAN_SEE(observer, opponent) so PERS renders the
    // opponent's name rather than "someone"; both HUMAN so other_side() is 0
    // and PERS uses GET_NAME, not the enemy race-star form.
    SET_BIT(context.character.specials2.pref, PRF_HOLYLIGHT);
    context.character.in_room = 0;
    context.character.tmpabilities.move = 20;
    context.character.specials.position = POSITION_FIGHTING;

    char_data opponent {};
    opponent.player.race = RACE_HUMAN;
    opponent.player.name = const_cast<char*>("Sauron");
    opponent.in_room = 0;
    opponent.specials.position = POSITION_FIGHTING;
    opponent.abilities.hit = 100;
    opponent.tmpabilities.hit = 45;
    // Mutual engagement: opponent fights this character, so there is no tank.
    opponent.specials.fighting = &context.character;
    context.character.specials.fighting = &opponent;

    std::string out;
    build_prompt(&context.descriptor, out);

    EXPECT_EQ(out, "HP:Healthy MV:Tired, Sauron:Wounded>");
    // Signature: the opponent's PERS-rendered name.
    EXPECT_NE(out.find("Sauron"), std::string::npos);
}

// build_prompt case: FIGHTING an opponent who is engaged with a DIFFERENT
// character (the tank) renders the tank line first, then the opponent line --
// both names via PERS.
TEST(PromptFormat, CombatTankAndOpponentRenderBothNames)
{
    ScopedTestWorld world;
    PromptContext context;
    SET_BIT(context.character.specials2.pref, PRF_PROMPT);
    SET_BIT(context.character.specials2.pref, PRF_HOLYLIGHT);
    context.character.in_room = 0;
    context.character.tmpabilities.move = 20;
    context.character.specials.position = POSITION_FIGHTING;

    char_data tank {};
    tank.player.race = RACE_HUMAN;
    tank.player.name = const_cast<char*>("Gandalf");
    tank.in_room = 0;
    tank.abilities.hit = 100;
    tank.tmpabilities.hit = 70;

    char_data opponent {};
    opponent.player.race = RACE_HUMAN;
    opponent.player.name = const_cast<char*>("Sauron");
    opponent.in_room = 0;
    opponent.abilities.hit = 100;
    opponent.tmpabilities.hit = 45;
    // Opponent fights the tank, not this character.
    opponent.specials.fighting = &tank;
    context.character.specials.fighting = &opponent;

    std::string out;
    build_prompt(&context.descriptor, out);

    EXPECT_EQ(out, "HP:Healthy MV:Tired, Gandalf:Hurt, Sauron:Wounded>");
    // Signature: both the tank's and the opponent's PERS-rendered names.
    EXPECT_NE(out.find("Gandalf"), std::string::npos);
    EXPECT_NE(out.find("Sauron"), std::string::npos);
}

// add_prompt case: PRF_DISPTEXT selects the prompt_text[] table entry indexed
// by specials.prompt_number, formatting specials.prompt_value into it (the
// PRF_DISPTEXT self-copy in add_prompt). Index 4 is "Shaping: %d".
TEST(PromptFormat, DispTextBranchRendersSelectedPromptText)
{
    PromptContext context;
    SET_BIT(context.character.specials2.pref, PRF_DISPTEXT);
    context.character.specials.prompt_number = 4;
    context.character.specials.prompt_value = 7;

    std::string out;
    build_prompt(&context.descriptor, out);

    EXPECT_EQ(out, "Shaping: 7>");
    // Signature: the prompt_text[]-formatted disptext string.
    EXPECT_NE(out.find("Shaping: 7"), std::string::npos);
}

// build_prompt case: the fully-default prompt -- pref == 0, every pool full,
// standing and not fighting -- leaves the composed core empty. Task 9 guarded
// the terminator against the old prompt[strlen(prompt) - 1] == prompt[-1]
// stack-underflow read, so an empty core now yields exactly ">" (defined
// behavior, ASan-clean) rather than reading one byte before the buffer.
TEST(PromptFormat, EmptyCorePromptTerminatesWithGreaterThan)
{
    PromptContext context;

    std::string out;
    build_prompt(&context.descriptor, out);

    // Empty core -> just the ">" terminator, no underflow.
    EXPECT_EQ(out, ">");
}

// add_prompt case: PRF_PROMPT with a PARTIAL mana pool pins a non-empty
// prompt_mana[] word. 45/100 -> 450/1000, which selects prompt_mana[3]
// " S:Charged"; the leading space is trimmed by build_prompt. Full hit/move
// contribute nothing, isolating the mana word.
TEST(PromptFormat, PartialManaRendersManaWord)
{
    PromptContext context;
    SET_BIT(context.character.specials2.pref, PRF_PROMPT);
    context.character.tmpabilities.mana = 45;

    std::string out;
    build_prompt(&context.descriptor, out);

    EXPECT_EQ(out, "S:Charged>");
    // Signature: the prompt_mana[]-selected spell-point word.
    EXPECT_NE(out.find("S:Charged"), std::string::npos);
}
