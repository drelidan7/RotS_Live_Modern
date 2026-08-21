#include "../handler.h"
#include "../spells.h"
#include "../utils.h"
#include "rots/core/character.h"
#include "rots/core/descriptor.h"
#include "test_placement.h"
#include "test_random_utils.h"
#include "test_world.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <limits>
#include <optional>
#include <string>

int get_mage_caster_level(const char_data *caster);
int get_magic_power(const char_data *caster);
bool should_apply_spell_penetration(const char_data *caster);
double get_spell_pen_value(const char_data *caster);
double get_victim_saving_throw(const char_data *caster, const char_data *victim);
bool different_zone(int was_in, int to_room);
int random_exit(int room);
bool is_teleportation_room_valid(room_data *room);
int get_save_bonus(const char_data &caster, const char_data &victim,
                   game_types::player_specs primary_spec, game_types::player_specs opposing_spec);
bool is_friendly_taget(const char_data *caster, const char_data *victim);
void apply_chilled_effect(char_data *caster, char_data *victim);

struct loclife_coord {
    int number;
    signed char n;
    signed char e;
    signed char u;
};

int loclife_add_rooms(loclife_coord room, loclife_coord *roomlist, int *roomnum, int room_not);

extern room_data world;
extern int top_of_world;
extern char_data* combat_list;
extern char_data* combat_next_dude;

namespace {

// Rooms 0..32 (33 rooms) covers every room number any test in this file
// requests: ZoneGuard/RoomExitGuard use rooms up to 9, MageProcTest::SetUp
// requests room 32 (the highest).
constexpr int kMageTestWorldRoomCount = 33;

// Any create_bulk() allocation dummy_room_data()-initializes a window of
// EXTENSION_SIZE rooms and always spans at least indices [0, EXTENSION_SIZE)
// in the worst reuse case (a single-room create_bulk(1) another suite ran
// first), so keeping the count within EXTENSION_SIZE guarantees every room
// this suite touches is a real, initialized room even when ScopedTestWorld
// reuses a world it didn't allocate. (Since LS-3a T1 Stage A the reuse branch
// re-initializes all of [0, room_data::BASE_LENGTH] itself, so this bound is
// no longer load-bearing for INITIALIZATION; it still is for staying in bounds
// of a possibly-smaller reused allocation, which is exactly what the runtime
// assert in ScopedTestWorld's reuse branch checks.) Mirrors
// damage_test_context.h's compile-time guard on its single-room reliance. Bump
// both the world and this bound together if a future test needs a higher room
// number.
static_assert(kMageTestWorldRoomCount <= EXTENSION_SIZE,
    "mage_tests' shared world must fit inside create_bulk()'s "
    "dummy-initialized EXTENSION_SIZE window");

void ensure_test_world(int minimum_room_number) {
    // Migrated onto the shared multi-room ScopedTestWorld (test_world.h) --
    // this function used to hand-roll its own world.create_bulk()/
    // dummy_room_data() bootstrap; Wave-1 Task 3 left it as the "5th clone"
    // specifically because ScopedTestWorld was single-room-only at the time
    // (Task 5 added the room_count ctor param this suite needs).
    //
    // Sized once for the whole suite's lifetime (function-local static,
    // process duration -- matching this suite's pre-existing
    // never-torn-down idiom, unlike the single-room callers that scope a
    // fresh instance per test) to kMageTestWorldRoomCount, the highest room
    // any test in this file ever touches, rather than to whichever call
    // happens to run first. The original per-call-site sizing
    // (create_bulk(minimum_room_number + 2)) left a latent bug where a
    // small first call (e.g. ZoneGuard(7, 8)) permanently capped the array,
    // leaving higher rooms only default-constructed (heap garbage in
    // sector_type/room_flags/light/etc.) for whichever suite happened to
    // run first under --gtest_shuffle.
    static ScopedTestWorld shared_world(kMageTestWorldRoomCount);

    // A hand-rolled loop zeroing funct/bfs_dir/bfs_next for every room used to
    // live here, because neither room_data's constructor nor
    // dummy_room_data() touches those three and they were therefore heap
    // garbage. LS-3a T1 Stage B moved that zeroing into ScopedTestWorld's own
    // reset_all_rooms(), which runs in BOTH constructor branches -- so
    // shared_world's one-time construction above already covers every room in
    // the allocation, and re-doing it per call bought nothing.
    //
    // (The retired loop's comment justified itself with
    // "loclife_add_rooms/random_exit's room-graph walk reads them". That was
    // factually false -- neither function touches funct/bfs_dir/bfs_next. The
    // real consumer is find_first_step(), whose BFS uses bfs_dir/bfs_next as
    // scratch state across every room in [0, top_of_world].)
    //
    // CHANGE DISCLOSURE, and the risk taken with it: the scrub used to run on
    // EVERY call to this function, i.e. effectively once per test that touches
    // the world. It now runs ONCE, when shared_world is constructed. Anything
    // that dirties funct/bfs_dir/bfs_next BETWEEN tests in the monolithic
    // single-process runner is therefore no longer scrubbed before the next
    // test reads them. That is accepted rather than overlooked: the only
    // consumer is find_first_step()'s BFS, which writes bfs_dir/bfs_next as its
    // own scratch before reading them, and nothing in this suite sets .funct.
    // If a pathfinding test ever lands in this binary that leaves a room's BFS
    // scratch meaningful, this is the line to revisit.

    if (top_of_world < minimum_room_number) {
        top_of_world = minimum_room_number;
    }
}

struct ZoneGuard {
    int room_a;
    int room_b;
    int original_zone_a;
    int original_zone_b;

    ZoneGuard(int first_room, int second_room)
        : room_a(first_room), room_b(second_room), original_zone_a(0), original_zone_b(0) {
        ensure_test_world(std::max(first_room, second_room));
        original_zone_a = room_by_id_total(first_room)->zone;
        original_zone_b = room_by_id_total(second_room)->zone;
    }

    ~ZoneGuard() {
        room_by_id_total(room_a)->zone = original_zone_a;
        room_by_id_total(room_b)->zone = original_zone_b;
    }
};

struct RoomExitGuard {
    int room_number;
    room_direction_data *original_exits[NUM_OF_DIRS]{};
    long original_room_flags = 0;
    // Publishes the guarded room's occupant chain as EMPTY for the guard's
    // lifetime and restores whatever head was there on unwind -- the
    // hand-rolled original_people save/restore this guard used to carry
    // (LS-3a T3, test_placement.h). A std::optional because the room it points
    // at only exists once the constructor body's ensure_test_world() has run;
    // it is destroyed AFTER the destructor body, so the exits and room_flags
    // still go back first, exactly as before.
    //
    // CHANGE DISCLOSURE: this guard did not previously EMPTY the room's chain,
    // it only saved and restored the head. Publishing an empty chain is a new
    // observable state for the guard's lifetime, and it is inert here for two
    // independent reasons, both checked: random_exit() and loclife_add_rooms()
    // -- the only functions these guards fence -- read dir_option/room_flags/
    // to_room and never the occupant chain at all; and ScopedTestWorld's reset
    // leaves every room's head null anyway, so there is nothing to empty in
    // practice. A future test that fences a room-occupant-reading function
    // with this guard must re-check that claim.
    std::optional<ScopedRoomOccupants> occupants;

    explicit RoomExitGuard(int room)
        : room_number(room), original_room_flags(0) {
        ensure_test_world(room);
        original_room_flags = room_by_id_total(room)->room_flags;
        for (int i = 0; i < NUM_OF_DIRS; ++i) {
            original_exits[i] = room_by_id_total(room)->dir_option[i];
        }
        occupants.emplace(room_by_id_total(room), room, std::initializer_list<char_data *>{});
    }

    ~RoomExitGuard() {
        for (int i = 0; i < NUM_OF_DIRS; ++i) {
            room_by_id_total(room_number)->dir_option[i] = original_exits[i];
        }
        room_by_id_total(room_number)->room_flags = original_room_flags;
    }
};

struct MageTestContext {
    char_data caster{};
    char_data victim{};
    char_data master{};
    char_prof_data caster_profs{};
    char_prof_data victim_profs{};
    char_prof_data master_profs{};
    char caster_name[16] = "test_mage";
    char victim_short_descr[16] = "test_target";
    char master_name[16] = "test_master";

    MageTestContext() {
        caster.profs = &caster_profs;
        victim.profs = &victim_profs;
        master.profs = &master_profs;

        caster.player.name = caster_name;
        victim.player.short_descr = victim_short_descr;
        master.player.name = master_name;

        caster.player.race = RACE_HUMAN;
        victim.player.race = RACE_HUMAN;
        master.player.race = RACE_HUMAN;

        caster.player.level = 30;
        victim.player.level = 30;
        master.player.level = 30;

        caster.tmpabilities.intel = 20;
        victim.tmpabilities.intel = 20;
        caster.points.spell_power = 0;
        victim.specials2.saving_throw = 0;
        caster.abilities.hit = 500;
        victim.abilities.hit = 500;
        caster.tmpabilities.hit = 500;
        victim.tmpabilities.hit = 500;
        caster.specials.position = POSITION_STANDING;
        victim.specials.position = POSITION_STANDING;
        set_location(&caster, 7);
        set_location(&victim, 7);
    }

    void prepare_for_spell_damage() {
        victim.specials2.act = MOB_ISNPC;
        victim.player.level = 0;
        victim.tmpabilities.intel = 8;
        victim.specials2.saving_throw = 0;
        victim.tmpabilities.hit = 500;
        victim.abilities.hit = 500;
        caster.specials.fighting = nullptr;
        victim.specials.fighting = nullptr;
    }

    void force_spell_save() {
        victim.specials2.act = MOB_ISNPC;
        victim.player.level = 90;
        victim.tmpabilities.intel = 25;
        victim.specials2.saving_throw = 0;
        victim.tmpabilities.hit = 500;
        victim.abilities.hit = 500;
        caster.specials.fighting = nullptr;
        victim.specials.fighting = nullptr;
    }
};

// Mirrors mystic_tests.cpp's/act_format_tests.cpp's/olog_hai_tests.cpp's own
// per-file copy of this helper (no shared header declares it): points a
// descriptor's output at its OWN small_outbuf so send_to_char() output can be
// inspected directly instead of going to a real socket. CRITICAL: mutates the
// caller's descriptor_data in place -- never replace with a version that
// returns a descriptor_data by value (descriptor_data::output is a self-pointer
// into small_outbuf[]; see act_format_tests.cpp's fuller note on this hazard).
void reset_capturing_descriptor(descriptor_data &descriptor, char_data *character) {
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = 0; // CON_PLYNG
    descriptor.character = character;
}

loclife_coord *find_loclife_room(loclife_coord *roomlist, int roomnum, int target_room) {
    for (int i = 0; i < roomnum; ++i) {
        if (roomlist[i].number == target_room) {
            return &roomlist[i];
        }
    }
    return nullptr;
}

} // namespace

class MageProcTest : public ::testing::Test {
  protected:
    void SetUp() override { ensure_test_world(32); }

    // prepare_for_spell_damage()/force_spell_save() (above) null out
    // caster/victim's .specials.fighting before exercising the real spell
    // functions below (MagicMissile/ChillRay/LightningBolt/DarkBolt/Firebolt/
    // ConeOfCold), which route through fight.cpp's damage() -- and since
    // .specials.fighting starts null (unlike DamageTestContext, which
    // presets it), damage()'s own `if (!ch->specials.fighting)
    // set_fighting(...)` guard fires for real, pushing this test's
    // stack-resident caster/victim onto the process-global combat_list.
    // Without resetting it here, that dangling stack pointer survives past
    // this test (and this whole suite) into whatever runs next in the same
    // process -- a second cross-suite-pollution source discovered while
    // fixing the room_data::BASE_WORLD/world[0] one this task targets (see
    // test_world.h), landing in fight.cpp's stop_fighting() walking
    // combat_list from an unrelated later test. Mirrors the existing
    // combat_list/combat_next_dude reset in damage_tests.cpp's
    // DamageMethodTest and characterization_combat_tests.cpp's
    // CharacterizationCombatTest.
    void TearDown() override
    {
        clear_test_random_values();
        combat_list = nullptr;
        combat_next_dude = nullptr;
    }
};

TEST_F(MageProcTest, MageCasterLevelUsesCurrentIntelRoundingPath) {
    MageTestContext context;
    context.caster_profs.prof_level[PROF_MAGE] = 18;
    context.caster.tmpabilities.intel = 19;

    push_test_random_value(0.0);
    EXPECT_EQ(get_mage_caster_level(&context.caster), 21)
        << "Expected low queued rolls to keep the current partial-intelligence bonus unrounded.";

    push_test_random_value(0.99);
    EXPECT_EQ(get_mage_caster_level(&context.caster), 22)
        << "Expected high queued rolls to trigger the current partial-intelligence rounding bonus.";
}

TEST_F(MageProcTest, MagicPowerUsesBattleMageBonusLevelModifierAndIntelRounding) {
    MageTestContext context;
    context.caster_profs.prof_level[PROF_MAGE] = 24;
    context.caster_profs.specialization = static_cast<int>(game_types::PS_BattleMage);
    context.caster.specials.tactics = TACTICS_AGGRESSIVE;
    context.caster.points.spell_power = 60;
    context.caster.tmpabilities.intel = 19;

    push_test_random_value(0.0);
    push_test_random_value(0.0);
    EXPECT_EQ(get_magic_power(&context.caster), 124)
        << "Expected magic power to combine mage level, battle-mage bonus, level modifier, and the "
           "current low-roll intel contribution.";

    push_test_random_value(0.99);
    push_test_random_value(0.99);
    EXPECT_EQ(get_magic_power(&context.caster), 126)
        << "Expected magic power to increase by one when the queued intel-rounding roll succeeds.";
}

TEST(MageHelpers, SpellPenetrationAppliesForPlayersAndEligibleCharmedOrcFriends) {
    MageTestContext context;

    EXPECT_TRUE(should_apply_spell_penetration(&context.caster))
        << "Expected player casters to always apply spell penetration.";

    context.caster.specials2.act = MOB_ISNPC;
    EXPECT_FALSE(should_apply_spell_penetration(&context.caster))
        << "Expected ordinary NPC casters not to apply spell penetration.";

    context.caster.specials2.act = MOB_ISNPC | MOB_ORC_FRIEND;
    context.caster.specials.affected_by = AFF_CHARM;
    context.caster.master = &context.master;
    EXPECT_TRUE(should_apply_spell_penetration(&context.caster))
        << "Expected charmed orc-friend NPCs with a player master to apply spell penetration.";
}

TEST(MageHelpers, SpellPenetrationRejectsCharmedOrcFriendsWithoutPlayerMaster) {
    MageTestContext context;
    context.caster.specials2.act = MOB_ISNPC | MOB_ORC_FRIEND;
    context.caster.specials.affected_by = AFF_CHARM;

    EXPECT_FALSE(should_apply_spell_penetration(&context.caster))
        << "Expected charmed orc-friend NPCs without a master to skip spell penetration.";

    context.master.specials2.act = MOB_ISNPC;
    context.caster.master = &context.master;
    EXPECT_FALSE(should_apply_spell_penetration(&context.caster))
        << "Expected charmed orc-friend NPCs with a non-player master to skip spell penetration.";
}

TEST(MageHelpers, SpellPenValueUsesCasterAndMasterMageLevelsForCharmedNpcs) {
    MageTestContext context;
    context.caster_profs.prof_level[PROF_MAGE] = 20;

    EXPECT_DOUBLE_EQ(get_spell_pen_value(&context.caster), 4.0)
        << "Expected player spell penetration to use one fifth of the caster's mage level.";

    context.caster.specials2.act = MOB_ISNPC;
    context.caster.specials.affected_by = AFF_CHARM;
    context.caster.master = &context.master;
    context.caster.player.level = 20;
    context.master_profs.prof_level[PROF_MAGE] = 15;

    EXPECT_DOUBLE_EQ(get_spell_pen_value(&context.caster), 5.0)
        << "Expected charmed NPC spell penetration to include one third of the master's mage "
           "level.";
}

TEST(MageHelpers, VictimSavingThrowUsesSpellPenetrationAndPlayerLevelAdjustment) {
    MageTestContext context;
    context.caster_profs.prof_level[PROF_MAGE] = 20;
    context.victim.specials2.saving_throw = 10;
    context.victim.player.level = 25;

    EXPECT_DOUBLE_EQ(get_victim_saving_throw(&context.caster, &context.victim), 11.0)
        << "Expected player victims to offset spell penetration with the current level-based "
           "saving-throw adjustment.";

    context.caster.specials2.act = MOB_ISNPC;
    EXPECT_DOUBLE_EQ(get_victim_saving_throw(&context.caster, &context.victim), 10.0)
        << "Expected NPC casters without spell penetration eligibility to leave the victim saving "
           "throw unchanged.";
}

TEST(MageHelpers, DifferentZoneReflectsCurrentWorldZoneNumbers) {
    ZoneGuard zone_guard(7, 8);

    room_by_id_total(7)->zone = 12;
    room_by_id_total(8)->zone = 12;
    EXPECT_FALSE(different_zone(7, 8))
        << "Expected rooms in the same zone to report that they are not in different zones.";

    room_by_id_total(8)->zone = 13;
    EXPECT_TRUE(different_zone(7, 8))
        << "Expected rooms with different zone numbers to report that they are in different zones.";
}

TEST_F(MageProcTest, RandomExitReturnsNowhereForInvalidRoomNumbers) {
    EXPECT_EQ(random_exit(-1), NOWHERE);
    EXPECT_EQ(random_exit(999999), NOWHERE);
}

TEST_F(MageProcTest, RandomExitFallsBackToSameRoomWhenNoBlinkableExitsExist) {
    RoomExitGuard room_guard(7);
    RoomExitGuard destination_guard(8);
    room_direction_data blocked_exit{};
    for (int i = 0; i < NUM_OF_DIRS; ++i) {
        room_by_id_total(7)->dir_option[i] = nullptr;
    }
    blocked_exit.to_room = 8;
    blocked_exit.exit_info = EX_NOBLINK;
    room_by_id_total(7)->dir_option[NORTH] = &blocked_exit;
    room_by_id_total(8)->room_flags = 0;

    EXPECT_EQ(random_exit(7), 7) << "Expected random_exit to leave the caster in place when every "
                                    "exit is excluded from blinking.";
}

TEST_F(MageProcTest, RandomExitChoosesAmongEligibleExitsUsingQueuedRandomRolls) {
    RoomExitGuard room_guard(7);
    RoomExitGuard north_guard(8);
    RoomExitGuard east_guard(9);
    room_direction_data north_exit{};
    room_direction_data east_exit{};

    for (int i = 0; i < NUM_OF_DIRS; ++i) {
        room_by_id_total(7)->dir_option[i] = nullptr;
    }
    north_exit.to_room = 8;
    east_exit.to_room = 9;
    room_by_id_total(7)->dir_option[NORTH] = &north_exit;
    room_by_id_total(7)->dir_option[EAST] = &east_exit;
    room_by_id_total(8)->room_flags = 0;
    room_by_id_total(9)->room_flags = 0;

    push_test_random_value(0.0);
    EXPECT_EQ(random_exit(7), 8)
        << "Expected the lowest queued roll to choose the first eligible blink exit.";

    push_test_random_value(0.99);
    EXPECT_EQ(random_exit(7), 9)
        << "Expected the highest queued roll to choose the last eligible blink exit.";
}

// RR Wave R3 Task 2p -- the wave's one GUARDED room-resolve conversion
// (mage.cpp:944, docs/superpowers/room-resolve-ledger.md's
// `src/combat/mage.cpp . spell_blink . room_by_id_total(` row).
//
// RED-FIRST against the pre-guard body: spell_blink resolved `room`
// unconditionally at mage.cpp:944, including on the two paths that had just
// established `room == NOWHERE` (mage.cpp:935-937 and :941) and five lines
// before `fail` is first consulted (mage.cpp:949). An unplaced victim therefore
// drove room_data::operator[]'s negative-room mudlog (db_world.cpp:2082-2085)
// and had room 0's NO_TELEPORT flag decide an already-decided failure.
//
// BOTH halves are asserted on purpose: the absent mudlog is what the guard
// removes, and the still-delivered failure message is what it must NOT change
// -- so this test cannot pass by the function simply doing nothing.
TEST_F(MageProcTest, BlinkDoesNotResolveARoomForAnUnplacedVictimBeforeFailing) {
    MageTestContext context;
    descriptor_data caster_descriptor{};
    reset_capturing_descriptor(caster_descriptor, &context.caster);
    context.caster.desc = &caster_descriptor;
    set_location(&context.caster, NOWHERE);

    testing::internal::CaptureStderr();
    // victim == nullptr, so the body substitutes the caster (mage.cpp:922-923)
    // -- the shape every real dispatch of this TAR_SELF_ONLY spell takes.
    spell_blink(&context.caster, nullptr, 0, nullptr, nullptr, 0, 0);
    const std::string captured = testing::internal::GetCapturedStderr();

    EXPECT_EQ(captured.find("world[] called for negative room number."), std::string::npos)
        << "Expected an already-failed blink never to resolve NOWHERE; stderr was: " << captured;
    EXPECT_EQ(std::string(caster_descriptor.output),
              "The world spins around, but nothing happens.\n\r")
        << "Expected the pre-existing failure message to be unchanged by the new guard.";
}

TEST(MageHelpers, TeleportationRoomValidationRejectsOccupiedAndRestrictedRooms) {
    room_data test_room{};
    char_data occupant{};

    // A stack stub room, not a world[] room, so it has no rnum to stamp: the
    // occupant's location id stays 0 -- the value char_data{} already gave it,
    // and one is_teleportation_room_valid() never reads (it looks only at
    // room->people and room->room_flags). The nested scope reproduces the
    // hand-rolled `test_room.people = nullptr;` that used to follow the first
    // assertion: the helper's unwind puts the head back, and the empty helper
    // below re-publishes an empty chain for the flag cases (LS-3a T3,
    // test_placement.h).
    {
        ScopedRoomOccupants occupied{&test_room, 0, {&occupant}};
        EXPECT_FALSE(is_teleportation_room_valid(&test_room))
            << "Expected occupied rooms to be invalid teleportation destinations.";
    }

    ScopedRoomOccupants empty{&test_room, 0, {}};
    test_room.room_flags = DEATH;
    EXPECT_FALSE(is_teleportation_room_valid(&test_room))
        << "Expected death rooms to be invalid teleportation destinations.";

    test_room.room_flags = SECURITYROOM;
    EXPECT_FALSE(is_teleportation_room_valid(&test_room))
        << "Expected security rooms to be invalid teleportation destinations.";

    test_room.room_flags = NO_TELEPORT;
    EXPECT_FALSE(is_teleportation_room_valid(&test_room))
        << "Expected no-teleport rooms to be invalid teleportation destinations.";

    test_room.room_flags = GODROOM;
    EXPECT_FALSE(is_teleportation_room_valid(&test_room))
        << "Expected god rooms to be invalid teleportation destinations.";
}

TEST(MageHelpers, TeleportationRoomValidationAcceptsEmptyOrdinaryRooms) {
    room_data test_room{};
    test_room.room_flags = 0;
    ScopedRoomOccupants empty{&test_room, 0, {}};

    EXPECT_TRUE(is_teleportation_room_valid(&test_room))
        << "Expected empty rooms without teleport restrictions to be valid teleportation "
           "destinations.";
}

TEST(MageHelpers, SaveBonusUsesCasterAndVictimSpecializationMatchups) {
    MageTestContext context;
    context.caster_profs.specialization = static_cast<int>(game_types::PS_Fire);
    context.victim_profs.specialization = static_cast<int>(game_types::PS_Cold);

    EXPECT_EQ(
        get_save_bonus(context.caster, context.victim, game_types::PS_Fire, game_types::PS_Cold),
        -4)
        << "Expected matching caster specialization and opposing victim specialization to stack "
           "the current save-bonus reductions.";

    context.caster_profs.specialization = static_cast<int>(game_types::PS_Cold);
    context.victim_profs.specialization = static_cast<int>(game_types::PS_Fire);
    EXPECT_EQ(
        get_save_bonus(context.caster, context.victim, game_types::PS_Fire, game_types::PS_Cold), 4)
        << "Expected opposing caster specialization and matching victim specialization to stack "
           "the current save-bonus increases.";

    context.caster_profs.specialization = static_cast<int>(game_types::PS_Arcane);
    context.victim_profs.specialization = static_cast<int>(game_types::PS_Arcane);
    EXPECT_EQ(
        get_save_bonus(context.caster, context.victim, game_types::PS_Fire, game_types::PS_Cold),
        -4)
        << "Expected arcane specialization to count as primary for the caster and opposing for the "
           "victim in the current implementation.";
}

TEST(MageHelpers, FriendlyTargetTreatsSelfFollowersAndSameSideCharactersAsFriendly) {
    MageTestContext context;
    char_data follower{};
    follower.master = &context.caster;

    EXPECT_TRUE(is_friendly_taget(&context.caster, &context.caster))
        << "Expected a caster to always count as a friendly target to themselves.";
    EXPECT_TRUE(is_friendly_taget(&context.caster, &follower))
        << "Expected follower chains ending at the caster to count as friendly targets.";
    EXPECT_TRUE(is_friendly_taget(&context.caster, &context.victim))
        << "Expected same-side characters to count as friendly targets.";

    context.victim.player.race = RACE_ORC;
    EXPECT_FALSE(is_friendly_taget(&context.caster, &context.victim))
        << "Expected characters on the opposing side to count as non-friendly targets.";
}

TEST(MageHelpers, ChilledEffectUsesVictimEnergyAndTracksColdSpecDrain) {
    MageTestContext context;
    context.victim.specials.ENERGY = 120;
    context.victim.points.ENE_regen = 3;

    apply_chilled_effect(&context.caster, &context.victim);

    EXPECT_EQ(context.victim.specials.ENERGY, 48)
        << "Expected chilled effect to remove half the victim's energy plus four rounds of current "
           "energy regeneration.";

    context.caster_profs.specialization = static_cast<int>(game_types::PS_Cold);
    context.caster.extra_specialization_data.set(context.caster);
    context.victim.specials.ENERGY = 120;

    apply_chilled_effect(&context.caster, &context.victim);

    auto *cold_data =
        static_cast<cold_spec_data *>(context.caster.extra_specialization_data.current_spec_info);
    ASSERT_NE(cold_data, nullptr);
    EXPECT_EQ(cold_data->get_total_energy_sapped(), 72)
        << "Expected cold specialization bookkeeping to track the exact energy drained by chilled "
           "effect.";
}

TEST_F(MageProcTest, MagicMissileHalvesDamageWhenSaveIsForced) {
    MageTestContext context;
    context.caster.tmpabilities.intel = 25;
    context.force_spell_save();
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);

    spell_magic_missile(&context.caster, nullptr, 0, &context.victim, nullptr, 0, 0);

    EXPECT_EQ(context.victim.tmpabilities.hit, 494)
        << "Expected strong-saving victims to halve magic missile's minimum deterministic damage "
           "on the real damage path.";
}

TEST_F(MageProcTest, ChillRayAppliesChilledEffectAndTracksColdSpecOnFailedSave) {
    MageTestContext context;
    context.caster.tmpabilities.intel = 25;
    context.caster_profs.specialization = static_cast<int>(game_types::PS_Cold);
    context.caster.extra_specialization_data.set(context.caster);
    context.victim.specials.ENERGY = 120;
    context.victim.points.ENE_regen = 3;
    context.prepare_for_spell_damage();

    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);

    spell_chill_ray(&context.caster, nullptr, 0, &context.victim, nullptr, 0, 0);

    auto *cold_data =
        static_cast<cold_spec_data *>(context.caster.extra_specialization_data.current_spec_info);
    ASSERT_NE(cold_data, nullptr);
    EXPECT_EQ(context.victim.tmpabilities.hit, 480);
    EXPECT_EQ(context.victim.specials.ENERGY, 48);
    EXPECT_EQ(cold_data->get_successful_chills(), 1);
    EXPECT_EQ(cold_data->get_total_energy_sapped(), 72);
}

TEST_F(MageProcTest, ChillRayTracksColdSpecFailureOnSavedCast) {
    MageTestContext context;
    context.caster.tmpabilities.intel = 25;
    context.caster_profs.specialization = static_cast<int>(game_types::PS_Cold);
    context.caster.extra_specialization_data.set(context.caster);
    context.force_spell_save();

    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);

    spell_chill_ray(&context.caster, nullptr, 0, &context.victim, nullptr, 0, 0);

    auto *cold_data =
        static_cast<cold_spec_data *>(context.caster.extra_specialization_data.current_spec_info);
    ASSERT_NE(cold_data, nullptr);
    EXPECT_EQ(context.victim.tmpabilities.hit, 490);
    EXPECT_EQ(cold_data->get_saved_chills(), 1);
}

TEST_F(MageProcTest, LightningBoltUsesSpecializationBonusAndSaveReduction) {
    MageTestContext context;
    context.caster.tmpabilities.intel = 25;
    context.caster_profs.specialization = static_cast<int>(game_types::PS_Lightning);
    context.force_spell_save();

    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);

    spell_lightning_bolt(&context.caster, nullptr, 0, &context.victim, nullptr, 0, 0);

    EXPECT_EQ(context.victim.tmpabilities.hit, 485)
        << "Expected lightning specialization to boost indoor lightning bolt damage before the "
           "strong victim save halves it on the real damage path.";
}

TEST_F(MageProcTest, DarkBoltUsesSpecializationBonusWithoutSunPenalty) {
    MageTestContext context;
    context.caster.tmpabilities.intel = 25;
    context.caster_profs.specialization = static_cast<int>(game_types::PS_Darkness);
    context.prepare_for_spell_damage();

    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);

    spell_dark_bolt(&context.caster, nullptr, 0, &context.victim, nullptr, 0, 0);

    EXPECT_EQ(context.victim.tmpabilities.hit, 469)
        << "Expected darkness specialization to apply its current 10% raw-damage bonus when "
           "sunlight is not weakening the spell.";
}

TEST_F(MageProcTest, FireboltUsesFireSpecMinimumDamageAndSaveReduction) {
    MageTestContext context;
    context.caster.tmpabilities.intel = 25;
    context.caster_profs.specialization = static_cast<int>(game_types::PS_Fire);
    context.force_spell_save();

    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);

    spell_firebolt(&context.caster, nullptr, 0, &context.victim, nullptr, 0, 0);

    EXPECT_EQ(context.victim.tmpabilities.hit, 498)
        << "Expected firebolt's strong-save path to halve the specialization-clamped minimum "
           "damage on the real damage path.";
}

TEST_F(MageProcTest, ConeOfColdAppliesChilledEffectAndColdSpecTrackingOnFailedSave) {
    MageTestContext context;
    context.caster.tmpabilities.intel = 25;
    context.caster_profs.specialization = static_cast<int>(game_types::PS_Cold);
    context.caster.extra_specialization_data.set(context.caster);
    context.victim.specials.ENERGY = 120;
    context.victim.points.ENE_regen = 3;
    context.prepare_for_spell_damage();

    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);
    push_test_random_value(0.0);

    spell_cone_of_cold(&context.caster, nullptr, 0, &context.victim, nullptr, 0, 0);

    auto *cold_data =
        static_cast<cold_spec_data *>(context.caster.extra_specialization_data.current_spec_info);
    ASSERT_NE(cold_data, nullptr);
    EXPECT_EQ(context.victim.tmpabilities.hit, 465);
    EXPECT_EQ(context.victim.specials.ENERGY, 48);
    EXPECT_EQ(cold_data->get_successful_cones(), 1);
    EXPECT_EQ(cold_data->get_total_energy_sapped(), 72);
}

TEST_F(MageProcTest, LocateLifeAddsReachableRoomsWithUpdatedCoordinates) {
    RoomExitGuard room_guard(7);
    room_direction_data north_exit{};
    room_direction_data east_exit{};
    room_direction_data down_exit{};
    loclife_coord origin{7, 2, -1, 3};
    loclife_coord roomlist[8]{};
    int roomnum = 0;

    for (int i = 0; i < NUM_OF_DIRS; ++i) {
        room_by_id_total(7)->dir_option[i] = nullptr;
    }

    north_exit.to_room = 8;
    east_exit.to_room = 9;
    down_exit.to_room = 10;
    room_by_id_total(7)->dir_option[NORTH] = &north_exit;
    room_by_id_total(7)->dir_option[EAST] = &east_exit;
    room_by_id_total(7)->dir_option[DOWN] = &down_exit;

    EXPECT_EQ(loclife_add_rooms(origin, roomlist, &roomnum, NOWHERE), 3)
        << "Expected locate-life room expansion to add each reachable adjacent room once.";
    EXPECT_EQ(roomnum, 3);

    loclife_coord *north_room = find_loclife_room(roomlist, roomnum, 8);
    loclife_coord *east_room = find_loclife_room(roomlist, roomnum, 9);
    loclife_coord *down_room = find_loclife_room(roomlist, roomnum, 10);

    ASSERT_NE(north_room, nullptr);
    ASSERT_NE(east_room, nullptr);
    ASSERT_NE(down_room, nullptr);

    EXPECT_EQ(north_room->n, 3);
    EXPECT_EQ(north_room->e, -1);
    EXPECT_EQ(north_room->u, 3);

    EXPECT_EQ(east_room->n, 2);
    EXPECT_EQ(east_room->e, 0);
    EXPECT_EQ(east_room->u, 3);

    EXPECT_EQ(down_room->n, 2);
    EXPECT_EQ(down_room->e, -1);
    EXPECT_EQ(down_room->u, 2);
}

TEST_F(MageProcTest, LocateLifeSkipsBlockedDuplicateAndExcludedRooms) {
    RoomExitGuard room_guard(7);
    room_direction_data north_exit{};
    room_direction_data east_exit{};
    room_direction_data south_exit{};
    room_direction_data west_exit{};
    loclife_coord origin{7, 0, 0, 0};
    loclife_coord roomlist[8]{};
    int roomnum = 1;

    for (int i = 0; i < NUM_OF_DIRS; ++i) {
        room_by_id_total(7)->dir_option[i] = nullptr;
    }

    roomlist[0].number = 8;
    north_exit.to_room = 8;
    east_exit.to_room = 11;
    east_exit.exit_info = EX_CLOSED | EX_DOORISHEAVY;
    south_exit.to_room = 12;
    west_exit.to_room = 13;

    room_by_id_total(7)->dir_option[NORTH] = &north_exit; // duplicate
    room_by_id_total(7)->dir_option[EAST] = &east_exit;   // blocked
    room_by_id_total(7)->dir_option[SOUTH] = &south_exit; // excluded
    room_by_id_total(7)->dir_option[WEST] = &west_exit;   // valid

    EXPECT_EQ(loclife_add_rooms(origin, roomlist, &roomnum, 12), 1)
        << "Expected locate-life room expansion to skip duplicates, excluded rooms, and heavy "
           "closed exits.";
    EXPECT_EQ(roomnum, 2);

    loclife_coord *west_room = find_loclife_room(roomlist, roomnum, 13);
    ASSERT_NE(west_room, nullptr);
    EXPECT_EQ(west_room->n, 0);
    EXPECT_EQ(west_room->e, -1);
    EXPECT_EQ(west_room->u, 0);
}

// ---------------------------------------------------------------------------
// SPELL_BEACON -- O-7 rider coverage (ls3b-global-constraints.md owner ruling
// O-7; ls3b-census-review.md F7; ls3b-census-b.md section 1.5). Zero prior
// coverage existed for spell_beacon() anywhere in the tree before this
// tranche. affected_type::modifier is a persisted sh_int (db_players.cpp's
// KEY_AFF / character_json.cpp's require_short_range); O-7 PRESERVES that
// room-rnum save format and makes the one-sided range guard (mage.cpp,
// mode == 2) two-sided, and adds a new write-side guard (mode == 1) that
// refuses to install a beacon whose modifier cannot fit the sh_int at all.
// Both are flagged, O-2-precedent behavior changes: a corrupt/absent beacon
// now always takes the spell's existing failure arm instead of ever reaching
// an unguarded char_to_room(caster, -1) placement, and an overflowing write
// is refused outright instead of silently truncating.
// ---------------------------------------------------------------------------

namespace {

// Saves and restores the process-global top_of_world for exactly the
// duration of a single test -- ReadArmStillRefusesToReturnWhenTheStoredModifier
// ExceedsTopOfWorld needs a KNOWN bound to compare its oversized modifier
// against, and top_of_world is shared process-wide state this whole suite
// (ensure_test_world()) only ever grows, never shrinks.
struct ScopedTopOfWorld {
    int previous;
    explicit ScopedTopOfWorld(int value)
        : previous(top_of_world) {
        top_of_world = value;
    }
    ~ScopedTopOfWorld() { top_of_world = previous; }
};

} // namespace

TEST(SpellBeaconTest, WriteArmRefusesToInstallABeaconWhenTheCastersLocationOverflowsTheSavedModifier) {
    // The write-arm guard runs BEFORE spell_beacon() ever calls room_of(caster),
    // so this needs no ScopedTestWorld at all: an out-of-range location is
    // rejected on its own terms, the same way a real extension-room rnum
    // above SHRT_MAX would be rejected once ~4630 extension rooms exist in a
    // real boot (ls3b-census-review.md F7).
    MageTestContext context;
    constexpr int kOverflowingRoom = static_cast<int>(std::numeric_limits<sh_int>::max()) + 1;
    set_location(&context.caster, kOverflowingRoom);

    char set_word[] = "set";
    txt_block set_text{};
    set_text.text = set_word;
    context.caster.delay.targ2.type = TARGET_TEXT;
    context.caster.delay.targ2.ptr.text = &set_text;

    ASSERT_EQ(affected_by_spell(&context.caster, SPELL_BEACON), nullptr);

    spell_beacon(&context.caster, nullptr, 0, nullptr, nullptr, 0, 0);

    EXPECT_EQ(affected_by_spell(&context.caster, SPELL_BEACON), nullptr)
        << "Expected the write-arm overflow guard to refuse installing a beacon whose modifier "
           "cannot fit the persisted sh_int, instead of silently truncating it.";
}

TEST(SpellBeaconTest, ReadArmRefusesToReturnWhenTheStoredModifierIsNegativeInsteadOfPlacingTheCasterNowhere) {
    // Simulates a beacon persisted while the caster had no location (or any
    // other corrupt negative modifier) -- exactly the state O-7's two-sided
    // guard exists to catch. The failure branch touches no room/zone state,
    // so this needs no ScopedTestWorld either.
    MageTestContext context;
    set_location(&context.caster, NOWHERE);

    affected_type corrupted_beacon{};
    corrupted_beacon.type = SPELL_BEACON;
    corrupted_beacon.duration = 5;
    corrupted_beacon.modifier = -1;
    corrupted_beacon.location = 0;
    corrupted_beacon.bitvector = 0;
    affect_to_char(&context.caster, &corrupted_beacon);
    ASSERT_NE(affected_by_spell(&context.caster, SPELL_BEACON), nullptr);

    char return_word[] = "return";
    txt_block return_text{};
    return_text.text = return_word;
    context.caster.delay.targ2.type = TARGET_TEXT;
    context.caster.delay.targ2.ptr.text = &return_text;

    spell_beacon(&context.caster, nullptr, 0, nullptr, nullptr, 0, 0);

    EXPECT_EQ(location_of(&context.caster), NOWHERE)
        << "Expected the corrupted-beacon failure arm to leave the caster exactly where they "
           "were -- never placed via an unguarded char_to_room(ch, -1).";
    EXPECT_EQ(affected_by_spell(&context.caster, SPELL_BEACON), nullptr)
        << "A rejected beacon is still consumed (removed), matching the pre-rider corrupted-beacon "
           "arm's own behavior.";
}

TEST(SpellBeaconTest, ReadArmStillRefusesToReturnWhenTheStoredModifierExceedsTopOfWorld) {
    // Regression guard for the guard's OTHER side: the pre-rider high-side
    // check (`> top_of_world`) must still reject an out-of-range modifier
    // after becoming two-sided -- proves `< 0 ||` was ADDED, not substituted.
    ScopedTopOfWorld scoped_top(10);
    MageTestContext context;
    set_location(&context.caster, 5);

    affected_type corrupted_beacon{};
    corrupted_beacon.type = SPELL_BEACON;
    corrupted_beacon.duration = 5;
    corrupted_beacon.modifier = 5000;
    corrupted_beacon.location = 0;
    corrupted_beacon.bitvector = 0;
    affect_to_char(&context.caster, &corrupted_beacon);
    ASSERT_NE(affected_by_spell(&context.caster, SPELL_BEACON), nullptr);

    char return_word[] = "return";
    txt_block return_text{};
    return_text.text = return_word;
    context.caster.delay.targ2.type = TARGET_TEXT;
    context.caster.delay.targ2.ptr.text = &return_text;

    spell_beacon(&context.caster, nullptr, 0, nullptr, nullptr, 0, 0);

    EXPECT_EQ(location_of(&context.caster), 5)
        << "Expected the pre-existing high-side guard to still reject an out-of-range modifier "
           "after the rider made the check two-sided.";
    EXPECT_EQ(affected_by_spell(&context.caster, SPELL_BEACON), nullptr);
}

TEST(SpellBeaconTest, RoundTripsToAnInRangeStoredRoomUnchanged) {
    // Positive control: an ordinary in-range room installs and returns
    // exactly as before this rider -- proves the two new guards reject only
    // what O-7 says they must, not ordinary use (the "in-range round-trip
    // unchanged" half of the rider's contract).
    ScopedTestWorld test_world{3};
    ScopedZoneTableOwner zone_table_owner;
    MageTestContext context;

    char_to_room(&context.caster, 1);
    ASSERT_EQ(location_of(&context.caster), 1);

    char set_word[] = "set";
    txt_block set_text{};
    set_text.text = set_word;
    context.caster.delay.targ2.type = TARGET_TEXT;
    context.caster.delay.targ2.ptr.text = &set_text;

    spell_beacon(&context.caster, nullptr, 0, nullptr, nullptr, 0, 0);

    affected_type *beacon = affected_by_spell(&context.caster, SPELL_BEACON);
    ASSERT_NE(beacon, nullptr)
        << "Expected an ordinary in-range room to still install a beacon -- the write-arm guard "
           "must not reject valid rooms.";
    EXPECT_EQ(beacon->modifier, 1);

    detach_char_from_room(&context.caster);
    char_to_room(&context.caster, 2);
    ASSERT_EQ(location_of(&context.caster), 2);

    txt_block return_text{};
    char return_word[] = "return";
    return_text.text = return_word;
    context.caster.delay.targ2.ptr.text = &return_text;

    spell_beacon(&context.caster, nullptr, 0, nullptr, nullptr, 0, 0);

    EXPECT_EQ(location_of(&context.caster), 1)
        << "Expected an in-range stored beacon to relocate the caster exactly as before the O-7 "
           "rider.";
    EXPECT_EQ(affected_by_spell(&context.caster, SPELL_BEACON), nullptr)
        << "A successful return still consumes the beacon.";
}
