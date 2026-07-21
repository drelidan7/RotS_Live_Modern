#include "../comm.h"
#include "../handler.h"
#include "../spells.h"
#include "../utils.h"
#include "rots/core/character.h"
#include "rots/core/descriptor.h"
#include "rots/core/object.h"
#include "test_random_utils.h"
#include "test_world.h"
#include <gtest/gtest.h>

#include <string>

bool is_victim_around(const char_data* character);
// perform_drop()/perform_give() (Cluster B wave Task 1; cb-task-1-brief.md
// Step 6; cb-census.md section 5.4 -- relocated here from act_obj1.cpp,
// coverage-gap rule).
int perform_drop(struct char_data* ch, struct obj_data* obj, sh_int RDR);
void perform_give(struct char_data* ch, struct char_data* vict, struct obj_data* obj);
bool can_double_hit(const char_data* character);
bool does_double_hit_proc(const char_data* character);
bool can_beorning_swipe(char_data* character);
bool does_beorning_swipe_proc(char_data* character);
void perform_violence(int mini_tics);
#ifdef TESTING
void reset_perform_violence_timing_for_testing();
#endif

extern char_data* combat_list;
extern char_data* combat_next_dude;

namespace {

struct FightProcTestContext {
    char_data attacker{};
    char_data victim{};
    char_prof_data profs{};
    obj_data weapon{};

    FightProcTestContext()
    {
        attacker.profs = &profs;
        // attacker.skills is an owning std::vector<byte> (RAII T3); size it to
        // MAX_SKILLS zeros the same way clear_char() would for a PC, since
        // this fixture never calls clear_char().
        attacker.skills.assign(MAX_SKILLS, 0);
        attacker.in_room = 1001;

        victim.in_room = 1001;
        attacker.specials.fighting = &victim;

        weapon.obj_flags.type_flag = ITEM_WEAPON;
        attacker.equipment[WIELD] = &weapon;
    }
};

} // namespace

class FightProcTest : public ::testing::Test {
  protected:
    void TearDown() override
    {
        clear_test_random_values();
    }
};

TEST(FightHelpers, ReportsVictimAsMissingWhenCombatTargetIsNull) {
    FightProcTestContext context;
    context.attacker.specials.fighting = nullptr;

    EXPECT_FALSE(is_victim_around(&context.attacker))
        << "Expected victim checks to fail once the attacker is no longer fighting anyone.";
}

TEST(FightHelpers, ReportsVictimAsMissingWhenTargetLeavesTheRoom) {
    FightProcTestContext context;
    context.victim.in_room = 2002;

    EXPECT_FALSE(is_victim_around(&context.attacker))
        << "Expected victim checks to fail when the target is no longer in the same room.";
}

TEST(FightHelpers, ReportsVictimAsAvailableWhenTargetRemainsInTheRoom) {
    FightProcTestContext context;

    EXPECT_TRUE(is_victim_around(&context.attacker))
        << "Expected victim checks to succeed while the target remains in the same room.";
}

TEST(FightHelpers, RequiresLightFightingSpecializationForDoubleHit) {
    FightProcTestContext context;
    context.profs.specialization = static_cast<int>(game_types::PS_WeaponMaster);

    EXPECT_FALSE(can_double_hit(&context.attacker))
        << "Expected double-hit to stay unavailable for non-light-fighting specializations.";
}

TEST(FightHelpers, RejectsDoubleHitWhenWeaponIsTooHeavy) {
    FightProcTestContext context;
    context.profs.specialization = static_cast<int>(game_types::PS_LightFighting);
    context.weapon.obj_flags.value[2] = 3;
    context.weapon.obj_flags.weight = LIGHT_WEAPON_WEIGHT_CUTOFF + 1;

    EXPECT_FALSE(can_double_hit(&context.attacker))
        << "Expected double-hit to reject bulk-3 weapons once they cross the light-weapon weight cutoff.";
}

TEST(FightHelpers, RejectsDoubleHitWhenAttackerIsUsingTwoHandedStyle) {
    FightProcTestContext context;
    context.profs.specialization = static_cast<int>(game_types::PS_LightFighting);
    context.attacker.specials.affected_by = AFF_TWOHANDED;

    EXPECT_FALSE(can_double_hit(&context.attacker))
        << "Expected double-hit to stay unavailable while the attacker is flagged as two-handed.";
}

TEST(FightHelpers, AllowsDoubleHitForLightWeaponAgainstNearbyVictim) {
    FightProcTestContext context;
    context.profs.specialization = static_cast<int>(game_types::PS_LightFighting);
    context.weapon.obj_flags.value[2] = 2;
    context.weapon.obj_flags.weight = LIGHT_WEAPON_WEIGHT_CUTOFF + 50;

    EXPECT_TRUE(can_double_hit(&context.attacker))
        << "Expected light-fighting characters to double-hit with a light one-handed weapon against a nearby victim.";
}

TEST_F(FightProcTest, DoubleHitProcSucceedsAtOrAboveTwentyPercentThreshold) {
    FightProcTestContext context;

    push_test_random_value(0.80);
    EXPECT_TRUE(does_double_hit_proc(&context.attacker))
        << "Expected double-hit procs to succeed when the random roll reaches the 20 percent threshold.";

    push_test_random_value(0.79);
    EXPECT_FALSE(does_double_hit_proc(&context.attacker))
        << "Expected double-hit procs to fail when the random roll stays below the 20 percent threshold.";
}

TEST(FightHelpers, RequiresBeorningRaceForSwipe) {
    FightProcTestContext context;
    context.attacker.player.race = RACE_HUMAN;

    EXPECT_FALSE(can_beorning_swipe(&context.attacker))
        << "Expected swipe to stay unavailable for non-beorning characters.";
}

TEST(FightHelpers, RequiresNearbyVictimForBeorningSwipe) {
    FightProcTestContext context;
    context.attacker.player.race = RACE_BEORNING;
    context.victim.in_room = 2002;

    EXPECT_FALSE(can_beorning_swipe(&context.attacker))
        << "Expected beorning swipe to require the fighting target to remain nearby.";
}

TEST(FightHelpers, AllowsBeorningSwipeWhenVictimIsNearby) {
    FightProcTestContext context;
    context.attacker.player.race = RACE_BEORNING;

    EXPECT_TRUE(can_beorning_swipe(&context.attacker))
        << "Expected beorning swipe to be available when a beorning is actively fighting a nearby target.";
}

TEST_F(FightProcTest, BeorningSwipeProcUsesCombinedWarriorSkillAndLevelChance) {
    FightProcTestContext context;
    context.attacker.player.level = 30;
    context.attacker.profs = &context.profs;
    context.profs.prof_level[PROF_WARRIOR] = 18;
    context.attacker.skills[SKILL_SWIPE] = 70;

    push_test_random_value(0.16);
    EXPECT_TRUE(does_beorning_swipe_proc(&context.attacker))
        << "Expected swipe procs to succeed when the roll stays within the warrior+skill+level chance.";

    push_test_random_value(0.18);
    EXPECT_FALSE(does_beorning_swipe_proc(&context.attacker))
        << "Expected swipe procs to fail once the roll exceeds the computed warrior+skill+level chance.";
}

class PerformViolenceTest : public ::testing::Test {
  protected:
    void SetUp() override
    {
        combat_list = nullptr;
        combat_next_dude = nullptr;
#ifdef TESTING
        reset_perform_violence_timing_for_testing();
#endif
    }

    void TearDown() override
    {
        combat_list = nullptr;
        combat_next_dude = nullptr;
#ifdef TESTING
        reset_perform_violence_timing_for_testing();
#endif
    }
};

TEST_F(PerformViolenceTest, FirstCallAfterResetTicksZeroDeltaInsteadOfEpochGarbage) {
    FightProcTestContext context;
    // Keep the loop body from reaching hit()/stop_fighting(): a positive mental
    // delay that stays above 1 after perform_violence's unconditional decrement
    // makes it `continue` right after the tick() call this test is pinning.
    context.attacker.specials.mental_delay = 5;
    context.attacker.next_fighting = nullptr;
    context.attacker.group = nullptr;
    combat_list = &context.attacker;

    perform_violence(0);

    EXPECT_FLOAT_EQ(context.attacker.damage_details.get_elapsed_combat_seconds(), 0.0f)
        << "Expected perform_violence's first call after a timing reset to tick a zero "
           "delta instead of computing it against the default-constructed steady_clock epoch.";
}

// ---------------------------------------------------------------------------
// perform_drop()/perform_give() coverage riders (Cluster B wave Task 1;
// cb-task-1-brief.md Step 6; cb-census.md section 5.4). Both functions were
// untested live code before this relocation (grep across src/tests/ found
// zero prior coverage) -- the standing wave coverage-gap rule. Uses a real
// connected descriptor (gtest_main.cpp registers the REAL send_to_char/act
// sinks process-wide) so these tests observe the genuine queued output,
// the same "no capturing-sink fixture needed" shape as act_format_tests.cpp's
// RoomPairContext-based ActSoci suite.
// ---------------------------------------------------------------------------

namespace {

struct ItemTransferTestContext {
    char_data ch {};
    descriptor_data ch_descriptor {};
    char_data vict {};
    descriptor_data vict_descriptor {};
    obj_data obj {};

    ItemTransferTestContext()
    {
        ch_descriptor.output = ch_descriptor.small_outbuf;
        ch_descriptor.small_outbuf[0] = '\0';
        ch_descriptor.bufptr = 0;
        ch_descriptor.bufspace = SMALL_BUFSIZE - 1;
        ch_descriptor.connected = CON_PLYNG;
        ch_descriptor.character = &ch;
        ch.desc = &ch_descriptor;
        ch.in_room = 0;
        // GET_NAME(ch) (utils.h) reads player.name directly for a non-NPC
        // char_data -- a null name would feed std::format() (perform_drop's/
        // perform_give's OBJ-log line) a null const char*, undefined
        // behavior. A real player character always has one.
        ch.player.name = const_cast<char*>("Frodo");
        // act()'s AWAKE(to) guard (comm.cpp's act_impl) requires
        // GET_POS(to) > POSITION_SLEEPING for a TO_CHAR/TO_VICT/TO_NOTVICT
        // message to actually queue -- a zero-initialized char_data defaults
        // to POSITION_DEAD (0).
        ch.specials.position = POSITION_STANDING;

        vict_descriptor.output = vict_descriptor.small_outbuf;
        vict_descriptor.small_outbuf[0] = '\0';
        vict_descriptor.bufptr = 0;
        vict_descriptor.bufspace = SMALL_BUFSIZE - 1;
        vict_descriptor.connected = CON_PLYNG;
        vict_descriptor.character = &vict;
        vict.desc = &vict_descriptor;
        vict.in_room = 0;
        vict.player.name = const_cast<char*>("Samwise");
        vict.specials.position = POSITION_STANDING;

        // -1 keeps perform_drop's/perform_give's own OBJ-log line's
        // `(obj->item_number >= 0) ? obj_index[...] : -1` ternary off the
        // (unpopulated in this fixture) obj_index[] array.
        obj.item_number = -1;
        // OBJS(obj, ch) (utils.h) reads obj->short_description directly when
        // CAN_SEE_OBJ() is true (the default here: no darkness/shadow/
        // invisibility set) -- a null short_description would feed
        // std::format() a null const char*, undefined behavior. A real
        // object always has one; this fixture supplies a plain literal.
        obj.short_description = const_cast<char*>("a plain dagger");
    }
};

} // namespace

TEST(PerformDropGive, PerformDropRefusesAndKeepsObjectWhenItemIsNodrop)
{
    ScopedTestWorld test_world(1);
    ItemTransferTestContext context;
    context.obj.obj_flags.extra_flags = ITEM_NODROP;
    obj_to_char(&context.obj, &context.ch);

    const int result = perform_drop(&context.ch, &context.obj, 0);

    EXPECT_EQ(result, 0);
    EXPECT_NE(std::string(context.ch_descriptor.output).find("You can't drop"), std::string::npos)
        << "Expected the ITEM_NODROP guard's message.";
    EXPECT_EQ(context.ch.carrying, &context.obj)
        << "Expected the NODROP object to remain in the character's inventory.";
}

TEST(PerformDropGive, PerformDropMovesObjectToRoomAndSendsMessages)
{
    ScopedTestWorld test_world(1);
    ItemTransferTestContext context;
    obj_to_char(&context.obj, &context.ch);

    const int result = perform_drop(&context.ch, &context.obj, 0);

    EXPECT_EQ(result, 0);
    EXPECT_EQ(context.ch.carrying, nullptr)
        << "Expected the object to leave the character's inventory.";
    EXPECT_EQ(test_world.room().contents, &context.obj)
        << "Expected the object to land in the room's contents list.";
    EXPECT_NE(std::string(context.ch_descriptor.output).find("You drop"), std::string::npos)
        << "Expected the successful-drop message.";
}

TEST(PerformDropGive, PerformGiveRefusesAndKeepsObjectWhenItemIsNodrop)
{
    ScopedTestWorld test_world(1);
    ItemTransferTestContext context;
    context.obj.obj_flags.extra_flags = ITEM_NODROP;
    obj_to_char(&context.obj, &context.ch);

    perform_give(&context.ch, &context.vict, &context.obj);

    EXPECT_NE(std::string(context.ch_descriptor.output).find("Yeech"), std::string::npos)
        << "Expected the ITEM_NODROP guard's message.";
    EXPECT_EQ(context.ch.carrying, &context.obj)
        << "Expected the NODROP object to remain with the giver.";
    EXPECT_EQ(context.vict.carrying, nullptr)
        << "Expected the recipient to receive nothing.";
}

TEST(PerformDropGive, PerformGiveTransfersObjectAndSendsMessages)
{
    ScopedTestWorld test_world(1);
    ItemTransferTestContext context;
    obj_to_char(&context.obj, &context.ch);

    perform_give(&context.ch, &context.vict, &context.obj);

    EXPECT_EQ(context.ch.carrying, nullptr)
        << "Expected the object to leave the giver's inventory.";
    EXPECT_EQ(context.vict.carrying, &context.obj)
        << "Expected the object to land in the recipient's inventory.";
    EXPECT_NE(std::string(context.ch_descriptor.output).find("You give"), std::string::npos)
        << "Expected the giver's confirmation message.";
    EXPECT_NE(std::string(context.vict_descriptor.output).find("gives you"), std::string::npos)
        << "Expected the recipient's delivery message.";
}
