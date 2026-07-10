#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "../rots_rng.h"
#include "../structs.h"
#include "../utils.h"
#include "test_platform_compat.h"
#include "test_world.h"

#include <gtest/gtest.h>

#include <cstring>
#include <format>
#include <string>

// Characterization tests for Phase 4 Wave 2 Task 4 (std::format population on
// act_othe.cpp / act_soci.cpp / act_offe.cpp -- player-facing social/emote/
// offensive-combat messaging). Per the task's binding pattern, these pin the
// CURRENT byte-for-byte output -- confirmed passing against the pre-conversion
// source (via `git stash push -- src/act_othe.cpp src/act_soci.cpp
// src/act_offe.cpp`, rebuild, run, `git stash pop`) before each file's
// sprintf/strcpy/strcat sites were converted to std::format/std::string
// composition, and green again after.

extern char buf[];

ACMD(do_title);
ACMD(do_wimpy);
ACMD(do_language);
ACMD(do_casting);
ACMD(do_specialize);
ACMD(do_order);
ACMD(do_bash);
ACMD(do_insult);

void print_group_leader(const char_data* leader);
void print_group_member(const char_data* group_member);
void roll_for_character(char_data* character, char_data* roll_initiator);
void give_share(char_data* sender, char_data* receiver, int share_amount);

extern struct prompt_type prompt_hit[];
extern struct prompt_type prompt_mana[];
extern struct prompt_type prompt_move[];
extern char* casting[];
extern const char* specialize_name[];

void clear_char(struct char_data* ch, int mode);

namespace {

// Mirrors shape_format_tests.cpp's helper (Phase 4 Wave 2 Task 3): points a
// descriptor's output at its OWN small_outbuf so send_to_char()/act() output
// can be inspected directly instead of going to a real socket.
//
// CRITICAL: this mutates the caller's descriptor_data in place and must NEVER
// be replaced by a version that returns a descriptor_data by value.
// descriptor_data::output is a self-pointer into the same object's
// small_outbuf[]; copying/moving a descriptor_data (across a `return`, an
// `x = f()`, or a member-initializer) copies that pointer bytewise and leaves
// `output` aimed at the SOURCE object's buffer -- a dangling pointer once the
// source (a returned temporary) is destroyed. On MSVC's Debug config (NRVO
// disabled) that dangling write_to_output() target produced empty/garbage
// output, cross-descriptor bleed (a victim's message surfacing in the actor's
// buffer), and an eventual SEH 0xc0000005 access violation; Linux/macOS masked
// it via copy elision. Always declare the descriptor, then reset it in place.
void reset_capturing_descriptor(descriptor_data& descriptor, char_data* character)
{
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = 0; // CON_PLAYING
    descriptor.character = character;
}

// A single awake PC, connected to a capturing descriptor, with no world
// dependency -- covers every converted site reached only through
// send_to_char()/act(..., TO_CHAR) with hide_invisible == FALSE, where
// act()'s to==ch loop condition short-circuits past CAN_SEE (see
// comm.cpp::act()'s `(CAN_SEE(to, ch) || !hide_invisible)` clause), so no
// room/light bootstrap is needed at all.
struct SoloCharacterContext {
    char_data character {};
    descriptor_data descriptor {};

    SoloCharacterContext()
    {
        clear_char(&character, MOB_VOID);
        reset_capturing_descriptor(descriptor, &character);
        character.desc = &descriptor;
        character.specials.position = POSITION_STANDING;
    }
};

// Two ordinary (non-NPC) PCs sharing room 0 of the process-wide test world,
// for the converted sites that are only reachable via get_char_room_vis()
// (do_insult's victim lookup, do_order's victim lookup) -- CAN_SEE()'s
// darkness gate is bypassed with PRF_HOLYLIGHT on the actor (a real
// character would need an actual light source/room-light bookkeeping this
// fixture has no need to model) rather than reimplementing IS_LIGHT(room).
struct RoomPairContext {
    ScopedTestWorld test_world;
    char_data actor {};
    char_data victim {};
    descriptor_data actor_descriptor {};
    descriptor_data victim_descriptor {};
    char_data* original_people = nullptr;

    RoomPairContext()
    {
        reset_capturing_descriptor(actor_descriptor, &actor);
        reset_capturing_descriptor(victim_descriptor, &victim);

        original_people = test_world.room().people;

        actor.in_room = 0;
        victim.in_room = 0;
        actor.next_in_room = &victim;
        victim.next_in_room = nullptr;
        test_world.room().people = &actor;

        actor.specials.position = POSITION_STANDING;
        victim.specials.position = POSITION_STANDING;
        actor.player.race = RACE_HUMAN;
        victim.player.race = RACE_HUMAN;
        SET_BIT(actor.specials2.pref, PRF_HOLYLIGHT);

        actor.desc = &actor_descriptor;
        victim.desc = &victim_descriptor;
    }

    ~RoomPairContext()
    {
        test_world.room().people = original_people;
        actor.next_in_room = nullptr;
        victim.next_in_room = nullptr;
        actor.in_room = NOWHERE;
        victim.in_room = NOWHERE;
    }
};

// A single character with one exit configured (EXIT(ch, door)), for
// do_bash's case-3 "throw yourself against the door" path -- pins the
// %s-keyword conversions without needing the success path's
// char_from_room()/char_to_room()/do_look()/WAIT_STATE_FULL() side effects
// (reached here only via the EX_DOORISHEAVY early-return branch).
struct DoorContext {
    static constexpr int door_direction = 0; // NORTH

    ScopedTestWorld test_world;
    char_data character {};
    descriptor_data descriptor {};
    room_direction_data exit {};
    char_data* original_people = nullptr;

    DoorContext()
    {
        reset_capturing_descriptor(descriptor, &character);

        original_people = test_world.room().people;

        character.in_room = 0;
        character.next_in_room = nullptr;
        test_world.room().people = &character;
        test_world.room().dir_option[door_direction] = &exit;

        character.specials.position = POSITION_STANDING;
        character.tmpabilities.str = 20;
        character.desc = &descriptor;
    }

    ~DoorContext()
    {
        test_world.room().people = original_people;
        test_world.room().dir_option[door_direction] = nullptr;
        character.in_room = NOWHERE;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// act_othe.cpp
// ---------------------------------------------------------------------------

// do_title's own-title query (empty title) and set-title (special chars in
// both name and title) forms -- pins the two converted sprintf sites at
// act_othe.cpp:314/326 ("Your present title is: {}" / "OK, you're now {}
// {}."), both of which read GET_TITLE(ch)/GET_NAME(ch) (char_player_data's
// `title`/`name` fields are char*, not char[N] -- confirmed via structs.h,
// so no static_cast<const char*> decay applies at these two sites).
TEST(ActOthe, DoTitleQueryFormatsEmptyTitleAsEmptyString)
{
    SoloCharacterContext ctx;
    ctx.character.player.level = 25;
    ctx.character.player.title = str_dup("");

    char empty_argument[] = "";
    do_title(&ctx.character, empty_argument, nullptr, 0, 0);

    EXPECT_EQ(std::string(ctx.descriptor.output), "Your present title is: \n\r");
}

TEST(ActOthe, DoTitleSetFormatsNameAndTitleWithSpecialCharacters)
{
    SoloCharacterContext ctx;
    ctx.character.player.level = 25;
    ctx.character.player.name = const_cast<char*>("Bragnaer");
    ctx.character.player.title = str_dup("");

    char new_title[] = "the Bold, Destroyer of Orcs";
    do_title(&ctx.character, new_title, nullptr, 0, 0);

    EXPECT_EQ(std::string(ctx.descriptor.output),
        "OK, you're now Bragnaer the Bold, Destroyer of Orcs.\n\r");

    RELEASE(ctx.character.player.title);
}

// do_wimpy's "set" branch -- pins the plain %d -> {} conversion at
// act_othe.cpp:861.
TEST(ActOthe, DoWimpySetFormatsIntegerHitPointThreshold)
{
    SoloCharacterContext ctx;
    ctx.character.abilities.hit = 500;
    ctx.character.tmpabilities.hit = 500;

    char wimpy_argument[] = "150";
    do_wimpy(&ctx.character, wimpy_argument, nullptr, 0, 0);

    EXPECT_EQ(std::string(ctx.descriptor.output), "OK, you'll flee if you drop below 150 hit points.\n\r");
}

// roll_for_character's actor-perspective message -- pins the
// {:>8}/{:>3} width conversions at act_othe.cpp:335 (translated from the
// original "%8s -- Rolled: %3d"). A short (3-char) name keeps the first
// printed character a space, since act()'s CAP(strp) only capitalizes when
// the first non-ANSI character is alphabetic -- avoiding an unrelated
// capitalization edge case in this pin. act() unconditionally appends
// "\n\r" (comm.cpp::convert_string), even though roll_for_character's own
// literal has none. roll_for_character rolls via number(1, 100)
// (rots_rng-backed); seed deterministically, learn the draw with an
// identical throwaway call, then re-seed so the real call reproduces it --
// avoids hardcoding a value that depends on rots_rng's internal sequence.
TEST(ActOthe, RollForCharacterFormatsRightJustifiedNameAndRoll)
{
    SoloCharacterContext ctx;
    ctx.character.player.name = const_cast<char*>("Tom");

    char_data roll_initiator {};
    clear_char(&roll_initiator, MOB_VOID);
    descriptor_data initiator_descriptor {};
    reset_capturing_descriptor(initiator_descriptor, &roll_initiator);
    roll_initiator.desc = &initiator_descriptor;
    roll_initiator.specials.position = POSITION_STANDING;

    rots_rng::seed(42);
    int expected_roll = number(1, 100);
    rots_rng::seed(42);

    roll_for_character(&ctx.character, &roll_initiator);

    EXPECT_EQ(std::string(initiator_descriptor.output),
        std::format("     Tom -- Rolled: {:>3}\n\r", expected_roll));
}

// print_group_leader/print_group_member write directly into the global
// `buf` (db.cpp) rather than sending anything themselves -- pins the
// {:>9},{:>11},{:>13} width conversions (translated from
// "HP:%9s,%11s,%13s"), including the MOB_FLAGGED(...) branch's {:>2} (Lvl)
// field (act_othe.cpp's print_group_member) which only NPCs with
// MOB_ORC_FRIEND can reach (a recruited pet in the group).
TEST(ActOthe, PrintGroupLeaderFormatsFullHealthStatusLine)
{
    char_data leader {};
    clear_char(&leader, MOB_VOID);
    leader.player.name = const_cast<char*>("Leader");
    leader.abilities.hit = 100;
    leader.tmpabilities.hit = 100;
    leader.abilities.mana = 100;
    leader.tmpabilities.mana = 100;
    leader.abilities.move = 100;
    leader.tmpabilities.move = 100;

    print_group_leader(&leader);

    EXPECT_STREQ(buf, "HP:  Healthy,     S:Full, MV:Energetic -- Leader (Head of group)\n\r");
}

TEST(ActOthe, PrintGroupMemberFormatsLevelFieldForOrcFriendPet)
{
    char_data member {};
    clear_char(&member, MOB_VOID);
    member.specials2.act = MOB_ISNPC;
    SET_BIT(MOB_FLAGS(&member), MOB_ORC_FRIEND);
    member.player.short_descr = const_cast<char*>("a recruited orc");
    member.player.level = 5;
    member.abilities.hit = 100;
    member.tmpabilities.hit = 100;
    member.abilities.mana = 100;
    member.tmpabilities.mana = 100;
    member.abilities.move = 100;
    member.tmpabilities.move = 100;

    print_group_member(&member);

    EXPECT_STREQ(buf, "HP:  Healthy,     S:Full, MV:Energetic -- a recruited orc (Lvl: 5)\n\r");
}

TEST(ActOthe, PrintGroupMemberFormatsPlainLineForNonOrcFriendMember)
{
    char_data member {};
    clear_char(&member, MOB_VOID);
    member.player.name = const_cast<char*>("Fellow");
    member.abilities.hit = 100;
    member.tmpabilities.hit = 100;
    member.abilities.mana = 100;
    member.tmpabilities.mana = 100;
    member.abilities.move = 100;
    member.tmpabilities.move = 100;

    print_group_member(&member);

    EXPECT_STREQ(buf, "HP:  Healthy,     S:Full, MV:Energetic -- Fellow\n\r");
}

// give_share's money-split message -- pins the two %s -> {} conversion at
// act_othe.cpp:737 (sender's name + money_message()'s own already-converted
// output).
TEST(ActOthe, GiveShareFormatsSenderNameAndMoneyMessage)
{
    char_data sender {};
    clear_char(&sender, MOB_VOID);
    sender.player.name = const_cast<char*>("Sender");

    char_data receiver {};
    clear_char(&receiver, MOB_VOID);
    descriptor_data receiver_descriptor {};
    reset_capturing_descriptor(receiver_descriptor, &receiver);
    receiver.desc = &receiver_descriptor;
    receiver.specials.position = POSITION_STANDING;

    give_share(&sender, &receiver, 1000); // 1000 copper == 1 gold

    EXPECT_EQ(std::string(receiver_descriptor.output),
        std::format("Sender splits some money among the group; you receive {}.\r\n",
            money_message(1000, 0)));
}

// do_casting's "possible modes" listing -- pins the sprintf+strcat
// accumulation converted to std::string composition (act_othe.cpp's
// do_casting, the same accumulation shape as do_shooting/do_tactics/
// do_language/do_specialize/do_apply's "possible X are" listings).
TEST(ActOthe, DoCastingListsPossibleModesWhenArgumentUnrecognized)
{
    SoloCharacterContext ctx;
    // clear_char() (via SoloCharacterContext) already CREATE1()s ch->profs;
    // just stamp the specialization GET_SPEC() reads.
    ctx.character.profs->specialization = PLRSPEC_ARCANE;

    char unrecognized_mode[] = "gibberish";
    do_casting(&ctx.character, unrecognized_mode, nullptr, 0, 0);

    std::string expected = "Possible casting modes are:\n\r   ";
    for (int index = 0; casting[index][0] != '\n'; ++index) {
        expected += casting[index];
        expected += " casting.";
        expected += "\n\r    ";
    }
    EXPECT_EQ(std::string(ctx.descriptor.output), expected);
}

// do_specialize's "possible specializations" listing when GET_SPEC(ch) is
// unset -- pins the strcpy+strcat accumulation (with the trailing ", "
// trim, originally `buf[strlen(buf) - 2] = 0`, now
// `std::string::resize(size() - 2)`) converted to std::string composition.
TEST(ActOthe, DoSpecializeListsPossibleSpecializationsWhenNoneChosen)
{
    SoloCharacterContext ctx;
    ctx.character.player.level = 20;

    char no_argument[] = "";
    do_specialize(&ctx.character, no_argument, nullptr, 0, 0);

    int num_of_specializations = (int)game_types::PS_Count;
    std::string expected = "You can specialize in ";
    for (int index = 1; index < num_of_specializations; ++index) {
        expected += specialize_name[index];
        expected += ", ";
    }
    expected.resize(expected.size() - 2);
    expected += ".\n\r";
    EXPECT_EQ(std::string(ctx.descriptor.output), expected);
}

// do_language's own-language query -- pins the ternary-decayed
// skill_data::name (char[50]) read at act_othe.cpp:1496 ("You are using
// {}."). Since ch->player.language == 0 here, the ternary's other branch
// ("common language", a string literal) is what's actually printed, but the
// ternary's common type is still resolved at compile time across both
// branches (one of which is the char[50] array), so this exercises the
// exact expression the char[N]-decay note calls out -- the array branch
// decays to `const char*` as part of the conditional operator's own
// common-type conversion, before std::format ever sees the value.
TEST(ActOthe, DoLanguageQueryFormatsCommonLanguageWhenNoneSet)
{
    SoloCharacterContext ctx;
    ctx.character.player.language = 0;

    char no_argument[] = "";
    do_language(&ctx.character, no_argument, nullptr, 0, 0);

    EXPECT_EQ(std::string(ctx.descriptor.output), "You are using common language.\n\r");
}

// ---------------------------------------------------------------------------
// act_soci.cpp
// ---------------------------------------------------------------------------

// do_insult's other-target message -- pins the %s -> {} conversion at
// act_soci.cpp:330 ("You insult {}.") with a victim name containing a
// special (apostrophe) character, reached through get_char_room_vis() +
// PERS() (both gated by CAN_SEE(), bypassed here via PRF_HOLYLIGHT on the
// actor). This message is sent via plain send_to_char() (not act()), so no
// trailing "\n\r" is appended beyond the literal's own "\n\r".
TEST(ActSoci, DoInsultFormatsOtherTargetVictimNameWithApostrophe)
{
    RoomPairContext ctx;
    ctx.victim.player.name = const_cast<char*>("O'Rourke");

    char insult_argument[] = "O'Rourke";
    do_insult(&ctx.actor, insult_argument, nullptr, 0, 0);

    EXPECT_EQ(std::string(ctx.actor_descriptor.output), "You insult O'Rourke.\n\r");
}

// do_insult's self-target message ("You feel insulted.") is a plain literal
// (no sprintf site at all) -- included alongside the other-target pin above
// purely to document that the self-vs-other branch split doesn't touch any
// converted site.
TEST(ActSoci, DoInsultSelfTargetSendsPlainLiteralMessage)
{
    RoomPairContext ctx;
    ctx.actor.player.name = const_cast<char*>("Bragnaer");

    char insult_self[] = "Bragnaer";
    do_insult(&ctx.actor, insult_self, nullptr, 0, 0);

    EXPECT_EQ(std::string(ctx.actor_descriptor.output), "You feel insulted.\n\r");
}

// ---------------------------------------------------------------------------
// act_offe.cpp
// ---------------------------------------------------------------------------

// do_order's victim-found message -- pins the %s -> {} conversion at
// act_offe.cpp:299 ("$N orders you to '{}'"), the one site in this task
// requiring an explicit static_cast<const char*> decay (message is a local
// char[256], not a pointer -- act_offe.cpp's do_order declares `char
// name[100], message[256];`). Delivered through act(..., TO_CHAR) with
// hide_invisible == FALSE (so CAN_SEE isn't re-checked for delivery, only
// for the earlier get_char_room_vis() lookup), and act() substitutes $N ->
// "You" (addressee is the victim itself) and appends "\n\r".
TEST(ActOffe, DoOrderFormatsMessageToVictimWithApostrophe)
{
    RoomPairContext ctx;
    ctx.victim.player.name = const_cast<char*>("Victim");
    // victim->master stays null (default-constructed), so do_order takes the
    // "$n has an indifferent look" branch after this message rather than the
    // (victim->master == ch) branch that would call command_interpreter() on
    // a not-fully-live victim -- irrelevant to the message this test pins,
    // which is composed and sent before that branch is reached either way.

    char order_argument[] = "Victim attack the orc's camp!";
    do_order(&ctx.actor, order_argument, nullptr, 0, 0);

    const std::string output(ctx.victim_descriptor.output);
    EXPECT_NE(output.find("orders you to 'attack the orc's camp!'"), std::string::npos)
        << "actual output: " << output;
}

// do_bash's case-3 ("throw yourself against the door") path -- pins the
// EXIT(ch, door)->keyword conversions at act_offe.cpp:612/629 ("You throw
// yourself on the {}." / "The {} would not budge."), reached via the
// EX_DOORISHEAVY early-return branch so the (unrelated) success path's
// char_from_room()/char_to_room()/do_look()/WAIT_STATE_FULL() side effects
// don't need to be modeled. RACE_HUMAN takes the "throw yourself" (not
// "light body") branch.
TEST(ActOffe, DoBashHeavyDoorFormatsThrowMessageThenWouldNotBudge)
{
    DoorContext ctx;
    ctx.character.player.race = RACE_HUMAN;
    ctx.exit.keyword = const_cast<char*>("gate");
    ctx.exit.exit_info = EX_ISDOOR | EX_CLOSED | EX_DOORISHEAVY;

    waiting_type wtl {};
    wtl.targ1.type = TARGET_OTHER;
    wtl.targ1.ch_num = DoorContext::door_direction;

    do_bash(&ctx.character, const_cast<char*>(""), &wtl, 0, 3);

    EXPECT_EQ(std::string(ctx.descriptor.output),
        "You throw yourself on the gate.\n\rThe gate would not budge.\n\r");
}

// Same path for a RACE_WOOD character -- pins the "light body" variant
// message (act_offe.cpp:606) that RACE_WOOD/RACE_HIGH/RACE_HOBBIT take
// instead.
TEST(ActOffe, DoBashHeavyDoorFormatsLightBodyMessageForWoodElf)
{
    DoorContext ctx;
    ctx.character.player.race = RACE_WOOD;
    ctx.exit.keyword = const_cast<char*>("gate");
    ctx.exit.exit_info = EX_ISDOOR | EX_CLOSED | EX_DOORISHEAVY;

    waiting_type wtl {};
    wtl.targ1.type = TARGET_OTHER;
    wtl.targ1.ch_num = DoorContext::door_direction;

    do_bash(&ctx.character, const_cast<char*>(""), &wtl, 0, 3);

    EXPECT_EQ(std::string(ctx.descriptor.output),
        "You throw your light body on the gate.\n\rThe gate would not budge.\n\r");
}
