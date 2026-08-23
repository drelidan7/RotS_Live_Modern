// fight_credit_tests.cpp
//
// TASK-021 Task 4. Two things are under test here, and they are two halves of
// one idea -- "who gets the blame for this death is not necessarily who is
// swinging":
//
//   * damage_credited() (fight.cpp) separates the ENGAGING attacker from the
//     CREDITED killer. damage() is now a forwarder that passes the attacker as
//     the credit, so every historical call site is unchanged; a room affect
//     that ticks long after its caster walked out can name that caster as the
//     killer without ever engaging it. apply_spell_damage_credited() (mage.cpp)
//     is the same split one layer up, with the saving-throw scaling read from
//     a cast-time caster_snapshot instead of from the live caster.
//   * The recorded poison origin (char_special_data::poisoned_by*,
//     resolve_poisoner()) replaces raw_kill()'s old
//     `attack_type == SPELL_POISON ||` heuristic, which assumed every poison
//     death was a player's doing.
//
// FIXTURES: the extract-char stub, the one-entry mob_index and the corpse
// release are file-local copies of mage_tests.cpp's ScopedFireballExtractCharHook
// / ScopedFireballMobIndex / release_fireball_corpse -- the repo's per-file-copy
// idiom for these three (no shared header declares them). ScopedCharExists
// copies affect_update_tests.cpp's, on the two-argument set_char_exists()
// TASK-021 added so char_by_abs_number()/resolve() can recover the pointer.

#include "../db.h"
#include "../entity_hooks.h"
#include "../big_brother.h"
#include "../combat_hooks.h"
#include "../handler.h"
#include "../persist_hooks.h"
#include "../pkill.h"
#include "../spells.h"
#include "../utils.h"
#include "../zone.h"
#include "rots/core/caster_snapshot.h"
#include "rots/core/character.h"
#include "rots/core/descriptor.h"
#include "rots/core/object.h"
#include "test_placement.h"
#include "test_random_utils.h"
#include "test_world.h"

#include <algorithm>
#include <cstdlib>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

extern char_data* combat_list;
extern char_data* combat_next_dude;
extern char_data* waiting_list;
extern char_data* character_list;
extern obj_data* object_list;
extern struct index_data* mob_index;
extern int top_of_world;
extern int mortal_start_room[];
extern struct skill_data skills[];
extern int r_mortal_start_room[];

void point_update(void);
// limits.cpp's per-character affect tick -- the ORDINARY poison DoT's home
// (its `case SPELL_POISON:` arm), as distinct from point_update()'s
// AFF_POISON-without-an-affect arm the tests further down already drive.
// No header declares it.
void affect_update_person(struct char_data* i, int mode);
// fight.cpp's own declaration (no header carries it -- the local-extern
// treatment poison_notification_tests.cpp already gives raw_kill()).
void raw_kill(char_data* dead_man, char_data* killer, int attack_type);

namespace {

// Rooms 0..3: the death room, the "somewhere else entirely" room a remote
// credited killer stands in, and slack. Well inside the window
// create_bulk()/ScopedTestWorld guarantee is dummy-initialized.
constexpr int kWorldRoomCount = 4;
constexpr int kDeathRoom = 1;
constexpr int kRemoteRoom = 2;

// abs_numbers this suite registers. Kept in a band no other suite in the
// monolithic runner uses (affect_update_tests: 7901; caster_snapshot_tests:
// 7903) and comfortably inside the char_exists bit table.
constexpr int kKillerAbsNumber = 7920;
constexpr int kSecondKillerAbsNumber = 7921;

// ---------------------------------------------------------------------------
// Recording seams
// ---------------------------------------------------------------------------

struct RecordedExtraction {
    char_data* ch = nullptr;
    int calls = 0;
};

RecordedExtraction g_recorded_extraction;

// The real NPC/PC arms' first observable act, minus the free_char()/save that
// would turn a stack fixture into a crash instead of a witness (mage_tests.cpp's
// unlinking_extract_char_stub, verbatim in intent).
void unlinking_extract_char_stub(char_data* ch, int /*new_room*/)
{
    g_recorded_extraction.ch = ch;
    ++g_recorded_extraction.calls;
    char_from_room(ch);
}

class ScopedRecordingExtractCharHook {
public:
    ScopedRecordingExtractCharHook()
    {
        g_recorded_extraction = RecordedExtraction {};
        rots::entity::set_extract_char_hook(unlinking_extract_char_stub);
    }
    ~ScopedRecordingExtractCharHook() { register_extract_char_hook(); }
    ScopedRecordingExtractCharHook(const ScopedRecordingExtractCharHook&) = delete;
    ScopedRecordingExtractCharHook& operator=(const ScopedRecordingExtractCharHook&) = delete;
};

struct RecordedDeath {
    char_data* dead_man = nullptr;
    char_data* killer = nullptr;
    bool called = false;
};

RecordedDeath g_recorded_death;

// raw_kill()'s own `dispatch_character_died(dead_man, killer, corpse)` is the
// narrowest observable of WHICH character raw_kill() was handed as the killer:
// group_gain() cannot serve, because it returns early whenever the killer does
// not stand in the dead man's room -- which is precisely the case this task
// exists to support.
void recording_character_died_stub(char_data* dead_man, char_data* killer, obj_data* /*corpse*/)
{
    g_recorded_death = RecordedDeath { dead_man, killer, true };
}

class ScopedRecordingCharacterDiedHook {
public:
    ScopedRecordingCharacterDiedHook()
    {
        g_recorded_death = RecordedDeath {};
        rots::entity::set_character_died_hook(recording_character_died_stub);
    }
    ~ScopedRecordingCharacterDiedHook() { register_character_died_hook(); }
    ScopedRecordingCharacterDiedHook(const ScopedRecordingCharacterDiedHook&) = delete;
    ScopedRecordingCharacterDiedHook& operator=(const ScopedRecordingCharacterDiedHook&) = delete;
};

void noop_crash_crashsave(char_data* /*ch*/, int /*rent_code*/) { }

// raw_kill()'s PC arm calls Crash_crashsave() (rent-file persistence) between
// the death save and the stat penalty this suite measures. Stubbed to a no-op
// for the scope so no test writes to the runtime data tree; restored to the
// real registration afterwards, the ScopedCharacterDiedHook shape
// big_brother_hooks_tests.cpp establishes.
class ScopedNoOpCrashCrashsave {
public:
    ScopedNoOpCrashCrashsave() { rots::combat::set_crash_crashsave_hook(noop_crash_crashsave); }
    ~ScopedNoOpCrashCrashsave() { register_crash_crashsave_hook(); }
    ScopedNoOpCrashCrashsave(const ScopedNoOpCrashCrashsave&) = delete;
    ScopedNoOpCrashCrashsave& operator=(const ScopedNoOpCrashCrashsave&) = delete;
};

// A two-slot zone table, and why this suite cannot use test_placement.h's
// ScopedZoneTableOwner.
//
// The tree carries TWO conventions for `top_of_zone_table`, and this suite is
// the first fixture to need both at once:
//   * zone_by_id_impl() (world/zone_load.cpp:59) reads it as a COUNT --
//     `znum >= top_of_zone_table` rejects -- so a one-zone table must set 1 or
//     char_to_room()/detach_char_from_room() null-deref on a PC. That is what
//     ScopedZoneTableOwner does, and it documents the trap.
//   * recalc_zone_power() (world/db_world.cpp:2144) -- which point_update()
//     calls first thing -- reads it as an inclusive TOP INDEX and WRITES
//     zone_table[top_of_zone_table]. Against ScopedZoneTableOwner's
//     one-element allocation that is a heap-buffer-overflow, caught by the
//     ASan preset (it is silently harmless in a plain build, which is why no
//     existing suite has tripped over it: occupant_order_tests.cpp's own
//     one-element fixture sets 0, the other convention, and so never drives a
//     PC through the resolver).
// Allocating a SPARE slot satisfies both readings at once: zone_by_id(0)
// resolves, and recalc_zone_power()'s inclusive loop stays inside the
// allocation. Only zone 0 is ever a room's zone here; slot 1 exists to be
// written past.
class ScopedCreditZoneTable {
public:
    ScopedCreditZoneTable()
        : m_owned_table(new zone_data[2] {})
        , m_previous_zone_table(zone_table)
        , m_previous_top_of_zone_table(top_of_zone_table)
    {
        m_owned_table[0].number = 0;
        m_owned_table[1].number = 1;
        zone_table = m_owned_table;
        top_of_zone_table = 1;
    }
    ~ScopedCreditZoneTable()
    {
        zone_table = m_previous_zone_table;
        top_of_zone_table = m_previous_top_of_zone_table;
        delete[] m_owned_table;
    }
    ScopedCreditZoneTable(const ScopedCreditZoneTable&) = delete;
    ScopedCreditZoneTable& operator=(const ScopedCreditZoneTable&) = delete;

private:
    // The table this fixture allocated -- deleted by address, never through the
    // global, so a body that repoints zone_table cannot make this a double free.
    zone_data* m_owned_table;
    // The table/count this fixture displaced, restored verbatim on scope exit.
    zone_data* m_previous_zone_table;
    int m_previous_top_of_zone_table;
};

// The death pipeline reads an NPC's prototype twice (activate_char_special()'s
// SPECIAL_DEATH probe and make_physical_corpse()'s corpse-owner id). This suite
// has no mob table, so publish a one-entry one with no spec-proc.
class ScopedCreditMobIndex {
public:
    ScopedCreditMobIndex()
        : m_previous(mob_index)
    {
        m_entry = index_data {};
        m_entry.virt = 1;
        mob_index = &m_entry;
    }
    ~ScopedCreditMobIndex() { mob_index = m_previous; }
    ScopedCreditMobIndex(const ScopedCreditMobIndex&) = delete;
    ScopedCreditMobIndex& operator=(const ScopedCreditMobIndex&) = delete;

private:
    index_data* m_previous; // the table this suite found installed (normally null)
    index_data m_entry {}; // the single prototype slot every fixture NPC's nr = 0 names
};

// Registers an abs_number in the char_exists table AND records the pointer, the
// way register_npc_char() does -- resolve_poisoner() needs both halves.
class ScopedCharExists {
public:
    ScopedCharExists(char_data& ch, int abs_number)
        : m_ch(ch)
    {
        ch.abs_number = abs_number;
        set_char_exists(abs_number, &ch);
    }
    ~ScopedCharExists() { remove_char_exists(m_ch.abs_number); }
    ScopedCharExists(const ScopedCharExists&) = delete;
    ScopedCharExists& operator=(const ScopedCharExists&) = delete;

private:
    char_data& m_ch; // the character whose registration this scope owns
};

// raw_kill()'s PC arm reads r_mortal_start_room[] (the death save's room) and
// mortal_start_room[] (the respawn load_room). Both are zero at rest, which is
// already a usable room here; pinned for the scope anyway so a sibling suite in
// the monolithic runner that repoints them cannot reach into this one.
class ScopedRacialStartRooms {
public:
    ScopedRacialStartRooms()
    {
        for (int race = 0; race < MAX_RACES; ++race) {
            m_previous_rnum[race] = r_mortal_start_room[race];
            m_previous_vnum[race] = mortal_start_room[race];
            r_mortal_start_room[race] = 0;
            mortal_start_room[race] = 0;
        }
    }
    ~ScopedRacialStartRooms()
    {
        for (int race = 0; race < MAX_RACES; ++race) {
            r_mortal_start_room[race] = m_previous_rnum[race];
            mortal_start_room[race] = m_previous_vnum[race];
        }
    }
    ScopedRacialStartRooms(const ScopedRacialStartRooms&) = delete;
    ScopedRacialStartRooms& operator=(const ScopedRacialStartRooms&) = delete;

private:
    // Prior values of the two boot-computed start-room arrays, restored verbatim.
    int m_previous_rnum[MAX_RACES];
    int m_previous_vnum[MAX_RACES];
};

// Saves and empties the four process-wide character lists a death touches, so
// nothing this suite's stack characters were linked into outlives the test.
// (The LS-2 finalization battery's monolithic SIGSEGV was exactly this class of
// leak -- a fixture char_data left on waiting_list.)
class ScopedGlobalCharacterLists {
public:
    ScopedGlobalCharacterLists()
        : m_combat_list(combat_list)
        , m_combat_next_dude(combat_next_dude)
        , m_waiting_list(waiting_list)
        , m_character_list(character_list)
    {
        combat_list = nullptr;
        combat_next_dude = nullptr;
        waiting_list = nullptr;
        character_list = nullptr;
        clear_test_random_values();
    }
    ~ScopedGlobalCharacterLists()
    {
        clear_test_random_values();
        combat_list = m_combat_list;
        combat_next_dude = m_combat_next_dude;
        waiting_list = m_waiting_list;
        character_list = m_character_list;
    }
    ScopedGlobalCharacterLists(const ScopedGlobalCharacterLists&) = delete;
    ScopedGlobalCharacterLists& operator=(const ScopedGlobalCharacterLists&) = delete;

private:
    // The four list heads this scope owns the restoration of.
    char_data* m_combat_list;
    char_data* m_combat_next_dude;
    char_data* m_waiting_list;
    char_data* m_character_list;
};

// make_corpse() CREATE()s a heap corpse and pushes it onto world[].contents and
// object_list; take both back out (mage_tests.cpp's release_fireball_corpse).
void release_test_corpse(room_data* room, obj_data* previous_object_list)
{
    obj_data* corpse = room->contents;
    if (corpse == nullptr) {
        object_list = previous_object_list;
        return;
    }
    obj_from_room(corpse);
    if (object_list == corpse)
        object_list = corpse->next;
    RELEASE(corpse->name);
    RELEASE(corpse->short_description);
    RELEASE(corpse->description);
    std::free(corpse);
    object_list = previous_object_list;
}

// ---------------------------------------------------------------------------
// Characters
// ---------------------------------------------------------------------------

// A one-hit-point NPC that any of this suite's hits kills: con 0 makes
// update_pos()'s `hit <= -con / 2` death test fire the moment hit reaches 0.
struct FragileNpc {
    char_data ch {};
    char_prof_data profs {};
    char short_descr[16] = "test_victim";

    FragileNpc()
    {
        ch.profs = &profs;
        ch.player.short_descr = short_descr;
        ch.specials2.act = MOB_ISNPC;
        ch.nr = 0; // prototype slot 0 of ScopedCreditMobIndex's one-entry table
        ch.player.race = RACE_HUMAN;
        ch.player.level = 1;
        ch.abilities.hit = 1;
        ch.tmpabilities.hit = 1;
        ch.specials.position = POSITION_STANDING;
    }
};

// A mortal player: raw_kill()'s `!IS_NPC(dead_man)` arm, which is where the
// poison-origin decision lives. No descriptor on purpose -- save_char() returns
// at its `IS_NPC(ch) || !ch->desc` guard, so the death save writes no file.
struct MortalPlayer {
    char_data ch {};
    char_prof_data profs {};
    char name[16] = "testplayer";

    MortalPlayer()
    {
        ch.profs = &profs;
        ch.player.name = name;
        ch.specials2.act = 0; // not IS_NPC, no PLR_* flags
        ch.player.race = RACE_HUMAN;
        ch.player.level = 10; // < LEVEL_IMMORT: the mortal respawn arm
        ch.specials.position = POSITION_STANDING;
        ch.abilities.hit = 400;
        ch.abilities.mana = 120;
        ch.abilities.str = 90;
        ch.abilities.intel = 90;
        ch.abilities.wil = 90;
        ch.abilities.dex = 90;
        ch.abilities.con = 90;
        ch.abilities.lea = 90;
        ch.tmpabilities = ch.abilities;
        ch.constabilities = ch.abilities;
    }
};

// A living mage/mob standing wherever the test puts it, used as the credited
// killer and as the recorded poisoner.
struct CreditedKiller {
    char_data ch {};
    char_prof_data profs {};
    char name[16] = "testmage";

    explicit CreditedKiller(bool npc)
    {
        ch.profs = &profs;
        if (npc) {
            ch.specials2.act = MOB_ISNPC;
            ch.nr = 0;
            ch.player.short_descr = name;
        } else {
            ch.specials2.act = 0;
            ch.player.name = name;
        }
        ch.player.race = RACE_HUMAN;
        ch.player.level = 30;
        ch.abilities.hit = 500;
        ch.tmpabilities.hit = 500;
        ch.specials.position = POSITION_STANDING;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// damage_credited(): engagement and credit are separate arguments
// ---------------------------------------------------------------------------

TEST(DamageCredited, TheCreditedKillerReachesRawKillNotTheEngagingAttacker)
{
    ScopedTestWorld test_world { kWorldRoomCount };
    ScopedCreditZoneTable zone_table_owner;
    ScopedCreditMobIndex prototype_table;
    ScopedGlobalCharacterLists global_lists;

    FragileNpc victim;
    CreditedKiller mage { /*npc=*/false };
    ScopedRoomOccupants death_room { room_by_id_total(kDeathRoom), kDeathRoom, { &victim.ch } };
    ScopedRoomOccupants remote_room { room_by_id_total(kRemoteRoom), kRemoteRoom, { &mage.ch } };

    obj_data* const previous_object_list = object_list;
    ScopedRecordingExtractCharHook extraction;
    ScopedRecordingCharacterDiedHook death;

    // The shape a room-affect tick produces: the victim is engaged with itself
    // (nobody else is in the room), and the kill is credited to a caster
    // standing somewhere else entirely.
    const int died = damage_credited(&victim.ch, &victim.ch, &mage.ch, 50, SPELL_BLAZE, 0);

    release_test_corpse(room_by_id_total(kDeathRoom), previous_object_list);

    EXPECT_EQ(died, 1);
    ASSERT_EQ(g_recorded_extraction.calls, 1) << "the fixture must actually kill the victim";
    EXPECT_EQ(g_recorded_extraction.ch, &victim.ch);
    ASSERT_TRUE(g_recorded_death.called) << "raw_kill() must have been reached";
    EXPECT_EQ(g_recorded_death.dead_man, &victim.ch);
    EXPECT_EQ(g_recorded_death.killer, &mage.ch)
        << "die()/raw_kill() must be told the CREDITED killer, not the engaging attacker";
    EXPECT_EQ(mage.ch.specials.fighting, nullptr) << "a remote credited killer is never engaged";
    EXPECT_EQ(location_of(&mage.ch), kRemoteRoom) << "and never moved";
}

TEST(DamageCredited, DamageForwardsWithTheAttackerAsCredit)
{
    ScopedTestWorld test_world { kWorldRoomCount };
    ScopedCreditZoneTable zone_table_owner;
    ScopedCreditMobIndex prototype_table;
    ScopedGlobalCharacterLists global_lists;

    FragileNpc victim;
    CreditedKiller mage { /*npc=*/false };
    ScopedRoomOccupants death_room { room_by_id_total(kDeathRoom), kDeathRoom,
        { &mage.ch, &victim.ch } };

    obj_data* const previous_object_list = object_list;
    ScopedRecordingExtractCharHook extraction;
    ScopedRecordingCharacterDiedHook death;

    const int died = damage(&mage.ch, &victim.ch, 50, SPELL_BLAZE, 0);

    // The engagement witness has to be read before teardown clears it.
    const char_data* const mage_was_fighting = mage.ch.specials.fighting;
    mage.ch.specials.fighting = nullptr;
    mage.ch.next_fighting = nullptr;
    victim.ch.specials.fighting = nullptr;
    victim.ch.next_fighting = nullptr;
    release_test_corpse(room_by_id_total(kDeathRoom), previous_object_list);

    EXPECT_EQ(died, 1);
    ASSERT_EQ(g_recorded_extraction.calls, 1);
    ASSERT_TRUE(g_recorded_death.called);
    EXPECT_EQ(g_recorded_death.killer, &mage.ch)
        << "damage() must credit the attacker it engaged -- the historical behavior";
    EXPECT_EQ(mage_was_fighting, &victim.ch)
        << "and, unlike a remote credited killer, that attacker IS engaged";
}

TEST(DamageCredited, ANullCreditIsCarriedThroughToDieAsNoKiller)
{
    ScopedTestWorld test_world { kWorldRoomCount };
    ScopedCreditZoneTable zone_table_owner;
    ScopedCreditMobIndex prototype_table;
    ScopedGlobalCharacterLists global_lists;

    FragileNpc victim;
    ScopedRoomOccupants death_room { room_by_id_total(kDeathRoom), kDeathRoom, { &victim.ch } };

    obj_data* const previous_object_list = object_list;
    ScopedRecordingExtractCharHook extraction;
    ScopedRecordingCharacterDiedHook death;

    // What a poison tick produces once the poisoner is gone: the tick still
    // kills, and nobody is credited. Nothing may dereference the null credit.
    const int died = damage_credited(&victim.ch, &victim.ch, nullptr, 50, SPELL_POISON, 0);

    release_test_corpse(room_by_id_total(kDeathRoom), previous_object_list);

    EXPECT_EQ(died, 1);
    ASSERT_EQ(g_recorded_extraction.calls, 1);
    ASSERT_TRUE(g_recorded_death.called);
    EXPECT_EQ(g_recorded_death.killer, nullptr) << "an uncredited death names no killer";
}

// ---------------------------------------------------------------------------
// resolve_poisoner(): the recorded origin, and when it stops answering
// ---------------------------------------------------------------------------

TEST(PoisonOrigin, ResolvePoisonerAnswersOnlyForALiveMatchingRecord)
{
    FragileNpc victim;
    CreditedKiller poisoner { /*npc=*/true };

    EXPECT_EQ(victim.ch.specials.poisoned_by_abs_number, -1)
        << "a character with no recorded poisoner starts at the -1 sentinel";
    EXPECT_EQ(resolve_poisoner(victim.ch), nullptr);

    {
        ScopedCharExists registered { poisoner.ch, kKillerAbsNumber };
        victim.ch.specials.poisoned_by_abs_number = poisoner.ch.abs_number;
        victim.ch.specials.poisoned_by = &poisoner.ch;

        EXPECT_EQ(resolve_poisoner(victim.ch), &poisoner.ch)
            << "a live, registered, self-consistent record resolves to its character";
    }

    // Registration gone (the poisoner was extracted): the record is stale.
    EXPECT_EQ(resolve_poisoner(victim.ch), nullptr);

    // ...and a DIFFERENT character recycled into that same abs_number slot must
    // not be mistaken for the recorded one.
    CreditedKiller impostor { /*npc=*/true };
    {
        ScopedCharExists recycled { impostor.ch, kKillerAbsNumber };
        EXPECT_EQ(resolve_poisoner(victim.ch), nullptr)
            << "a recycled slot answers for its new owner, not for the recorded poisoner";
    }

    // A recorded pointer whose own abs_number has moved on is equally stale.
    {
        ScopedCharExists registered { poisoner.ch, kSecondKillerAbsNumber };
        victim.ch.specials.poisoned_by = &poisoner.ch;
        victim.ch.specials.poisoned_by_abs_number = kKillerAbsNumber;
        EXPECT_EQ(resolve_poisoner(victim.ch), nullptr);
    }
}

// ---------------------------------------------------------------------------
// raw_kill(): a poison death is a player kill exactly when the poisoner was one
// ---------------------------------------------------------------------------
//
// The old line was `attack_type == SPELL_POISON || (killer && !IS_NPC(killer))`,
// so EVERY poison death took the player-kill arm (hit/4, mana 0) regardless of
// what poisoned the character. The NPC-poisoner test below is therefore the
// red-first discriminator: against the old expression it took the hit/4 arm and
// the 2/3 stat penalty never ran.
//
// ROUTE (controller ruling 2): raw_kill()'s PC arm is driven directly, NOT
// replayed through an extracted predicate. Its two persistence calls are
// harmless here -- save_char() returns at its own `IS_NPC(ch) || !ch->desc`
// guard for a descriptor-less fixture, and Crash_crashsave() is stubbed to a
// no-op for the scope by ScopedNoOpCrashCrashsave.

namespace {

// The observable: raw_kill() copies max abilities over current ones, then
// either quarters the hit points (player kill) or takes two thirds off every
// stat and drops hits to 1 (everything else).
void expect_player_kill_arm(const MortalPlayer& player)
{
    EXPECT_EQ(player.ch.tmpabilities.hit, player.ch.abilities.hit / 4)
        << "the player-kill arm quarters max hits";
    EXPECT_EQ(player.ch.tmpabilities.mana, 0);
    EXPECT_EQ(GET_STR(&player.ch), player.ch.abilities.str)
        << "the player-kill arm leaves the stats alone";
}

void expect_stat_penalty_arm(const MortalPlayer& player)
{
    EXPECT_EQ(player.ch.tmpabilities.hit, 1) << "the non-player-kill arm drops hits to 1";
    EXPECT_EQ(player.ch.tmpabilities.mana, 0);
    EXPECT_EQ(GET_STR(&player.ch), player.ch.abilities.str * 2 / 3)
        << "the non-player-kill arm takes two thirds of every stat";
    EXPECT_EQ(GET_INT(&player.ch), player.ch.abilities.intel * 2 / 3);
    EXPECT_EQ(GET_CON(&player.ch), player.ch.abilities.con * 2 / 3);
}

} // namespace

TEST(PoisonOrigin, RawKillTakesTheStatPenaltyArmWhenTheRecordedPoisonerIsAnNpc)
{
    ScopedTestWorld test_world { kWorldRoomCount };
    ScopedCreditZoneTable zone_table_owner;
    ScopedCreditMobIndex prototype_table;
    ScopedRacialStartRooms start_rooms;
    ScopedGlobalCharacterLists global_lists;
    ScopedNoOpCrashCrashsave no_rent_file;

    MortalPlayer player;
    CreditedKiller snake { /*npc=*/true };
    ScopedCharExists registered { snake.ch, kKillerAbsNumber };
    player.ch.specials.poisoned_by_abs_number = snake.ch.abs_number;
    player.ch.specials.poisoned_by = &snake.ch;

    ScopedRoomOccupants death_room { room_by_id_total(kDeathRoom), kDeathRoom,
        { &player.ch, &snake.ch } };
    obj_data* const previous_object_list = object_list;
    ScopedRecordingExtractCharHook extraction;
    ScopedRecordingCharacterDiedHook death;

    char_data* const poisoner = resolve_poisoner(player.ch);
    ASSERT_EQ(poisoner, &snake.ch);
    raw_kill(&player.ch, poisoner, SPELL_POISON);

    release_test_corpse(room_by_id_total(kDeathRoom), previous_object_list);

    EXPECT_EQ(g_recorded_death.killer, &snake.ch);
    expect_stat_penalty_arm(player);
}

TEST(PoisonOrigin, RawKillTakesThePlayerKillArmWhenTheRecordedPoisonerIsAPlayer)
{
    ScopedTestWorld test_world { kWorldRoomCount };
    ScopedCreditZoneTable zone_table_owner;
    ScopedCreditMobIndex prototype_table;
    ScopedRacialStartRooms start_rooms;
    ScopedGlobalCharacterLists global_lists;
    ScopedNoOpCrashCrashsave no_rent_file;

    MortalPlayer player;
    CreditedKiller mage { /*npc=*/false };
    ScopedCharExists registered { mage.ch, kKillerAbsNumber };
    player.ch.specials.poisoned_by_abs_number = mage.ch.abs_number;
    player.ch.specials.poisoned_by = &mage.ch;

    ScopedRoomOccupants death_room { room_by_id_total(kDeathRoom), kDeathRoom,
        { &player.ch, &mage.ch } };
    obj_data* const previous_object_list = object_list;
    ScopedRecordingExtractCharHook extraction;
    ScopedRecordingCharacterDiedHook death;

    char_data* const poisoner = resolve_poisoner(player.ch);
    ASSERT_EQ(poisoner, &mage.ch);
    raw_kill(&player.ch, poisoner, SPELL_POISON);

    release_test_corpse(room_by_id_total(kDeathRoom), previous_object_list);

    EXPECT_EQ(g_recorded_death.killer, &mage.ch);
    expect_player_kill_arm(player);
}

// ---------------------------------------------------------------------------
// point_update()'s poison tick credits what resolve_poisoner() answers
// ---------------------------------------------------------------------------

namespace {

// The tick arm under test is `!affected_by_spell(i, SPELL_POISON) &&
// IS_AFFECTED(i, AFF_POISON)` -- the AFF_POISON bit with no poison affect
// behind it. One point of health and con 0 make the 5-point tick lethal.
void arm_poison_tick(char_data& victim)
{
    SET_BIT(victim.specials.affected_by, AFF_POISON);
    victim.tmpabilities.hit = 1;
    victim.specials.position = POSITION_STANDING;
    character_list = &victim;
    victim.next = nullptr;
}

} // namespace

TEST(PoisonOrigin, PointUpdatePoisonTickCreditsTheResolvedPoisoner)
{
    ScopedTestWorld test_world { kWorldRoomCount };
    ScopedCreditZoneTable zone_table_owner;
    ScopedCreditMobIndex prototype_table;
    ScopedGlobalCharacterLists global_lists;

    FragileNpc victim;
    CreditedKiller mage { /*npc=*/false };
    ScopedCharExists registered { mage.ch, kKillerAbsNumber };
    victim.ch.specials.poisoned_by_abs_number = mage.ch.abs_number;
    victim.ch.specials.poisoned_by = &mage.ch;

    ScopedRoomOccupants death_room { room_by_id_total(kDeathRoom), kDeathRoom, { &victim.ch } };
    ScopedRoomOccupants remote_room { room_by_id_total(kRemoteRoom), kRemoteRoom, { &mage.ch } };
    obj_data* const previous_object_list = object_list;
    ScopedRecordingExtractCharHook extraction;
    ScopedRecordingCharacterDiedHook death;

    arm_poison_tick(victim.ch);
    point_update();

    release_test_corpse(room_by_id_total(kDeathRoom), previous_object_list);

    ASSERT_EQ(g_recorded_extraction.calls, 1) << "the poison tick must have killed the victim";
    ASSERT_TRUE(g_recorded_death.called);
    EXPECT_EQ(g_recorded_death.killer, &mage.ch)
        << "the tick credits the recorded poisoner, not the poisoned character itself";
    EXPECT_EQ(mage.ch.specials.fighting, nullptr) << "and never engages it";
}

TEST(PoisonOrigin, PointUpdatePoisonTickCreditsNobodyWhenThePoisonerIsGone)
{
    ScopedTestWorld test_world { kWorldRoomCount };
    ScopedCreditZoneTable zone_table_owner;
    ScopedCreditMobIndex prototype_table;
    ScopedGlobalCharacterLists global_lists;

    FragileNpc victim;
    CreditedKiller mage { /*npc=*/false };
    {
        ScopedCharExists registered { mage.ch, kKillerAbsNumber };
        victim.ch.specials.poisoned_by_abs_number = mage.ch.abs_number;
        victim.ch.specials.poisoned_by = &mage.ch;
    }
    // The poisoner has been extracted: the record survives, the character does
    // not. Nothing may dereference the recorded pointer to find that out --
    // the ASan preset is what proves the "nothing" (this test is a no-op under
    // a plain build if resolve_poisoner() reads through the stale pointer).
    ASSERT_EQ(resolve_poisoner(victim.ch), nullptr);

    ScopedRoomOccupants death_room { room_by_id_total(kDeathRoom), kDeathRoom, { &victim.ch } };
    obj_data* const previous_object_list = object_list;
    ScopedRecordingExtractCharHook extraction;
    ScopedRecordingCharacterDiedHook death;

    arm_poison_tick(victim.ch);
    point_update();

    release_test_corpse(room_by_id_total(kDeathRoom), previous_object_list);

    ASSERT_EQ(g_recorded_extraction.calls, 1) << "the tick still kills without a poisoner";
    ASSERT_TRUE(g_recorded_death.called);
    EXPECT_EQ(g_recorded_death.killer, nullptr);
}

// ---------------------------------------------------------------------------
// affect_remove() forgets the origin with the last poison affect
// ---------------------------------------------------------------------------

TEST(PoisonOrigin, AffectRemoveForgetsThePoisonerWhenTheLastPoisonAffectGoes)
{
    FragileNpc victim;
    CreditedKiller mage { /*npc=*/false };
    ScopedCharExists registered { mage.ch, kKillerAbsNumber };

    affected_type poison {};
    poison.type = SPELL_POISON;
    poison.duration = 5;
    poison.modifier = -2;
    poison.location = APPLY_STR;
    poison.bitvector = AFF_POISON;
    affect_to_char(&victim.ch, &poison);
    victim.ch.specials.poisoned_by_abs_number = mage.ch.abs_number;
    victim.ch.specials.poisoned_by = &mage.ch;

    ASSERT_NE(affected_by_spell(&victim.ch, SPELL_POISON), nullptr);
    affect_from_char(&victim.ch, SPELL_POISON);

    EXPECT_EQ(affected_by_spell(&victim.ch, SPELL_POISON), nullptr);
    EXPECT_EQ(victim.ch.specials.poisoned_by_abs_number, -1)
        << "the origin belongs to the poison; the last poison leaving takes it";
    EXPECT_EQ(victim.ch.specials.poisoned_by, nullptr);
    EXPECT_EQ(resolve_poisoner(victim.ch), nullptr);
}

TEST(PoisonOrigin, AffectRemoveKeepsThePoisonerWhileAnotherPoisonAffectRemains)
{
    FragileNpc victim;
    CreditedKiller mage { /*npc=*/false };
    ScopedCharExists registered { mage.ch, kKillerAbsNumber };

    affected_type poison {};
    poison.type = SPELL_POISON;
    poison.duration = 5;
    poison.modifier = -2;
    poison.location = APPLY_STR;
    poison.bitvector = AFF_POISON;
    affect_to_char(&victim.ch, &poison);
    affect_to_char(&victim.ch, &poison); // a second, independent poison affect
    victim.ch.specials.poisoned_by_abs_number = mage.ch.abs_number;
    victim.ch.specials.poisoned_by = &mage.ch;

    affect_remove(&victim.ch, victim.ch.affected);

    ASSERT_NE(affected_by_spell(&victim.ch, SPELL_POISON), nullptr)
        << "the fixture must leave one poison standing";
    EXPECT_EQ(victim.ch.specials.poisoned_by_abs_number, mage.ch.abs_number)
        << "one poison expiring does not forget the origin of the one still running";
    EXPECT_EQ(resolve_poisoner(victim.ch), &mage.ch);

    affect_from_char(&victim.ch, SPELL_POISON);
    EXPECT_EQ(victim.ch.specials.poisoned_by_abs_number, -1);
}

// ---------------------------------------------------------------------------
// apply_spell_damage_credited(): the same split one layer up (controller ruling 1)
// ---------------------------------------------------------------------------

namespace {

// Both forms are driven through the real damage() path, so every number() draw
// has to be identical between the two runs for the comparison to mean anything.
void queue_flat_rolls()
{
    clear_test_random_values();
    for (int i = 0; i < 40; ++i)
        push_test_random_value(0.0);
}

// A caster whose mage proficiency really moves the saving throw, and a victim
// with a saving throw for it to move: get_victim_saving_throw() subtracts
// mage_prof_level / 5 when spell penetration applies (a PC caster), and
// apply_spell_damage()'s multiplier then scales by 20 / (20 + saving_throw).
struct SpellDamageContext {
    ScopedTestWorld test_world { kWorldRoomCount };
    ScopedCreditZoneTable zone_table_owner;
    ScopedCreditMobIndex prototype_table;
    ScopedGlobalCharacterLists global_lists;
    CreditedKiller mage { /*npc=*/false };
    FragileNpc target;
    ScopedRoomOccupants room { room_by_id_total(kDeathRoom), kDeathRoom, { &mage.ch, &target.ch } };

    SpellDamageContext()
    {
        mage.profs.prof_level[PROF_MAGE] = 30; // spell penetration 30 / 5 = 6
        mage.ch.tmpabilities.intel = 20;
        target.ch.abilities.hit = 1000;
        target.ch.tmpabilities.hit = 1000;
        target.ch.tmpabilities.con = 100; // survives every hit below
        target.ch.specials2.saving_throw = 10;
        // Pre-engaged, so damage() never calls set_fighting() and this fixture
        // never touches the process-wide combat_list.
        mage.ch.specials.fighting = &target.ch;
        target.ch.specials.fighting = &mage.ch;
        mage.ch.specials.position = POSITION_FIGHTING;
        target.ch.specials.position = POSITION_FIGHTING;
    }

    ~SpellDamageContext()
    {
        mage.ch.specials.fighting = nullptr;
        target.ch.specials.fighting = nullptr;
        mage.ch.next_fighting = nullptr;
        target.ch.next_fighting = nullptr;
    }

    // Runs one hit and reports what it took off the target, resetting first.
    int measure(bool credited_form, const caster_snapshot& who)
    {
        target.ch.tmpabilities.hit = 1000;
        queue_flat_rolls();
        if (credited_form)
            apply_spell_damage_credited(who, &mage.ch, &target.ch, &mage.ch, 100, SPELL_MAGIC_MISSILE, 0);
        else
            apply_spell_damage(&mage.ch, &target.ch, 100, SPELL_MAGIC_MISSILE, 0);
        return 1000 - target.ch.tmpabilities.hit;
    }
};

} // namespace

TEST(ApplySpellDamageCredited, MatchesTheLiveFormForTheSameCasterUnderTheSameRolls)
{
    SpellDamageContext context;
    const caster_snapshot who = caster_snapshot::capture(context.mage.ch);

    const int live_damage = context.measure(/*credited_form=*/false, who);
    const int credited_damage = context.measure(/*credited_form=*/true, who);

    EXPECT_GT(live_damage, 0) << "the fixture must actually land a hit";
    EXPECT_EQ(credited_damage, live_damage)
        << "the credited form must run the live form's damage curve, not a copy of it";
}

TEST(ApplySpellDamageCredited, ScalesFromTheSnapshotsSpellPenetrationNotTheLiveCasters)
{
    SpellDamageContext context;
    // Captured while the caster still had PROF_MAGE 30 -- the state the room
    // affect was cast in.
    const caster_snapshot who = caster_snapshot::capture(context.mage.ch);
    const int credited_damage = context.measure(/*credited_form=*/true, who);

    // Now wreck the live caster: no mage proficiency at all, so the live form's
    // spell penetration collapses to zero and the victim's saving throw stands
    // undiminished.
    context.mage.profs.prof_level[PROF_MAGE] = 0;
    const int live_damage = context.measure(/*credited_form=*/false, who);
    const int credited_after = context.measure(/*credited_form=*/true, who);

    EXPECT_LT(live_damage, credited_damage)
        << "the live form must follow the caster's CURRENT proficiency";
    EXPECT_EQ(credited_after, credited_damage)
        << "the credited form must follow the SNAPSHOT, which the caster's collapse cannot touch";
}

// ---------------------------------------------------------------------------
// The ORDINARY poison DoT credits the recorded poisoner too (TASK-021 Task 6)
// ---------------------------------------------------------------------------
//
// point_update()'s arm above only fires for an AFF_POISON bit with no
// SPELL_POISON affect behind it -- worn gear, essentially. Every poison a
// character actually CASTS or eats produces a SPELL_POISON affect, and that
// ticks one level down, in affect_update_person()'s `case SPELL_POISON:` arm.
// Until this task that arm still ran `damage(i, i, 5, SPELL_POISON, 0)`, i.e.
// credited the poisoned character with its own death, so the recorded poisoner
// was never consulted for the DoT that does nearly all the killing (Task 4
// finding F2).

namespace {

// gtest_main never runs boot_db(), so skills[] is consts.cpp's static table.
// affect_update_person() only reaches an affect's arm when the skill is
// fast-updating or the affect's time_phase matches the world clock's; pinning
// is_fast for the scope makes the tick reachable without touching the clock.
class ScopedFastPoisonSkill {
public:
    ScopedFastPoisonSkill()
        : m_previous(skills[SPELL_POISON].is_fast)
    {
        skills[SPELL_POISON].is_fast = 1;
    }
    ~ScopedFastPoisonSkill() { skills[SPELL_POISON].is_fast = m_previous; }
    ScopedFastPoisonSkill(const ScopedFastPoisonSkill&) = delete;
    ScopedFastPoisonSkill& operator=(const ScopedFastPoisonSkill&) = delete;

private:
    byte m_previous; // the skills[] cell value this scope displaced
};

// Strips whatever affects a character still carries at scope exit:
// affect_to_char() links a stack char_data onto the process-global
// affected_list, and a death does not always take it back off.
class ScopedAffectCleanup {
public:
    explicit ScopedAffectCleanup(char_data& ch)
        : m_ch(ch)
    {
    }
    ~ScopedAffectCleanup()
    {
        while (m_ch.affected)
            affect_remove(&m_ch, m_ch.affected);
    }
    ScopedAffectCleanup(const ScopedAffectCleanup&) = delete;
    ScopedAffectCleanup& operator=(const ScopedAffectCleanup&) = delete;

private:
    char_data& m_ch; // the character whose affect list this scope empties
};

void noop_pkill_create(char_data* /*victim*/,
    const rots::combat::kill_contributor_list& /*contributors*/)
{
}

void noop_exploit_capture(int /*record_type*/, char_data* /*victim*/, int /*int_param*/,
    const char* /*extra*/)
{
}

// die()'s `killer != nullptr` arm -- the one a credited poison death now takes
// -- runs the exploit capture and pkill_create() between the damage and the
// stat penalty, and both of those PERSIST: the capture opens
// ./exploits/<initials>/<name>.exploits.json.tmp relative to the runner's
// working directory. Stubbed to no-ops for the scope so no test can write into
// a runtime data tree, and restored to their real registrations afterwards
// (ScopedNoOpCrashCrashsave's shape, one layer out).
class ScopedNoOpDeathPersistence {
public:
    ScopedNoOpDeathPersistence()
    {
        rots::combat::set_pkill_create_hook(noop_pkill_create);
        rots::persist::set_exploit_capture_hook(noop_exploit_capture);
    }
    ~ScopedNoOpDeathPersistence()
    {
        register_pkill_create_hook();
        register_exploit_capture_hook();
    }
    ScopedNoOpDeathPersistence(const ScopedNoOpDeathPersistence&) = delete;
    ScopedNoOpDeathPersistence& operator=(const ScopedNoOpDeathPersistence&) = delete;
};

// A real, lethal SPELL_POISON affect. Two things make the next tick fatal: one
// hit point, and a constitution of 2 -- update_pos() (fight.cpp:230) only calls
// a character dead at `GET_HIT <= -GET_CON / 2`, so the DoT's flat 5 points
// would leave a con-90 player merely stunned. The hit points are set AFTER
// affect_to_char(), which rebuilds tmpabilities from abilities through
// affect_total(); the constitution therefore has to go into abilities, which
// is what that rebuild reads.
void arm_poison_affect_tick(char_data& victim)
{
    victim.abilities.con = 2;
    victim.constabilities.con = 2; // affect_total() rebuilds tmpabilities from THIS one
    victim.tmpabilities.con = 2;
    affected_type af {};
    af.type = SPELL_POISON;
    af.duration = 5;
    af.modifier = 0;
    af.location = APPLY_NONE;
    af.bitvector = AFF_POISON;
    affect_to_char(&victim, &af);
    victim.tmpabilities.hit = 1;
    victim.specials.position = POSITION_STANDING;
}

} // namespace

TEST(PoisonOrigin, AffectUpdatePersonPoisonTickTakesThePlayerKillArmForAPlayerPoisoner)
{
    ScopedTestWorld test_world { kWorldRoomCount };
    ScopedCreditZoneTable zone_table_owner;
    ScopedCreditMobIndex prototype_table;
    ScopedRacialStartRooms start_rooms;
    ScopedGlobalCharacterLists global_lists;
    ScopedNoOpCrashCrashsave no_rent_file;
    ScopedNoOpDeathPersistence no_death_files;
    ScopedFastPoisonSkill fast_poison;

    MortalPlayer player;
    CreditedKiller mage { /*npc=*/false };
    ScopedCharExists registered { mage.ch, kKillerAbsNumber };
    player.ch.specials.poisoned_by_abs_number = mage.ch.abs_number;
    player.ch.specials.poisoned_by = &mage.ch;

    // The poisoner is somewhere else entirely -- the case the whole task exists
    // for. A DoT that credited the engaging attacker could never name it.
    ScopedRoomOccupants death_room { room_by_id_total(kDeathRoom), kDeathRoom, { &player.ch } };
    ScopedRoomOccupants remote_room { room_by_id_total(kRemoteRoom), kRemoteRoom, { &mage.ch } };
    obj_data* const previous_object_list = object_list;
    ScopedRecordingExtractCharHook extraction;
    ScopedRecordingCharacterDiedHook death;
    ScopedAffectCleanup player_affects { player.ch };

    arm_poison_affect_tick(player.ch);
    affect_update_person(&player.ch, /*mode=*/1);

    release_test_corpse(room_by_id_total(kDeathRoom), previous_object_list);

    ASSERT_TRUE(g_recorded_death.called) << "the DoT tick must have killed the player";
    EXPECT_EQ(g_recorded_death.killer, &mage.ch)
        << "the ordinary poison DoT credits the recorded poisoner, not the victim itself";
    EXPECT_EQ(mage.ch.specials.fighting, nullptr) << "and never engages it";
    expect_player_kill_arm(player);
}

TEST(PoisonOrigin, AffectUpdatePersonPoisonTickTakesTheStatPenaltyArmForAMobPoisoner)
{
    ScopedTestWorld test_world { kWorldRoomCount };
    ScopedCreditZoneTable zone_table_owner;
    ScopedCreditMobIndex prototype_table;
    ScopedRacialStartRooms start_rooms;
    ScopedGlobalCharacterLists global_lists;
    ScopedNoOpCrashCrashsave no_rent_file;
    ScopedNoOpDeathPersistence no_death_files;
    ScopedFastPoisonSkill fast_poison;

    MortalPlayer player;
    CreditedKiller snake { /*npc=*/true };
    ScopedCharExists registered { snake.ch, kKillerAbsNumber };
    player.ch.specials.poisoned_by_abs_number = snake.ch.abs_number;
    player.ch.specials.poisoned_by = &snake.ch;

    ScopedRoomOccupants death_room { room_by_id_total(kDeathRoom), kDeathRoom,
        { &player.ch, &snake.ch } };
    obj_data* const previous_object_list = object_list;
    ScopedRecordingExtractCharHook extraction;
    ScopedRecordingCharacterDiedHook death;
    ScopedAffectCleanup player_affects { player.ch };

    arm_poison_affect_tick(player.ch);
    affect_update_person(&player.ch, /*mode=*/1);

    release_test_corpse(room_by_id_total(kDeathRoom), previous_object_list);

    ASSERT_TRUE(g_recorded_death.called);
    EXPECT_EQ(g_recorded_death.killer, &snake.ch);
    // A mob's poison is not a player kill -- the retired heuristic said it was.
    expect_stat_penalty_arm(player);
}

TEST(PoisonOrigin, AffectUpdatePersonPoisonTickCreditsNobodyWhenThePoisonerIsGone)
{
    ScopedTestWorld test_world { kWorldRoomCount };
    ScopedCreditZoneTable zone_table_owner;
    ScopedCreditMobIndex prototype_table;
    ScopedRacialStartRooms start_rooms;
    ScopedGlobalCharacterLists global_lists;
    ScopedNoOpCrashCrashsave no_rent_file;
    ScopedNoOpDeathPersistence no_death_files;
    ScopedFastPoisonSkill fast_poison;

    MortalPlayer player;
    CreditedKiller mage { /*npc=*/false };
    {
        ScopedCharExists registered { mage.ch, kKillerAbsNumber };
        player.ch.specials.poisoned_by_abs_number = mage.ch.abs_number;
        player.ch.specials.poisoned_by = &mage.ch;
    }
    // The poisoner has been extracted: the record survives, the character does
    // not, and nothing may dereference the recorded pointer to find that out
    // (the ASan preset is what proves the "nothing").
    ASSERT_EQ(resolve_poisoner(player.ch), nullptr);

    ScopedRoomOccupants death_room { room_by_id_total(kDeathRoom), kDeathRoom, { &player.ch } };
    obj_data* const previous_object_list = object_list;
    ScopedRecordingExtractCharHook extraction;
    ScopedRecordingCharacterDiedHook death;
    ScopedAffectCleanup player_affects { player.ch };

    arm_poison_affect_tick(player.ch);
    affect_update_person(&player.ch, /*mode=*/1);

    release_test_corpse(room_by_id_total(kDeathRoom), previous_object_list);

    ASSERT_TRUE(g_recorded_death.called) << "the tick still kills without a poisoner";
    EXPECT_EQ(g_recorded_death.killer, nullptr);
}

// ---------------------------------------------------------------------------
// TASK-026 step 1: a sourceless kill falls back to the engaged opponent
// ---------------------------------------------------------------------------
//
// A poison/room tick whose caster cannot be resolved any more reaches
// damage_credited() with a null credit. Before this task that death went to
// die(NULL): no PK record for the players who were actually beating on the
// victim, and the victim took the harsher non-player-kill penalty. The
// fallback names the opponent the victim was engaged with at the moment it
// died.
//
// WHERE the fallback reads that opponent is load-bearing and is a deliberate
// deviation from the task brief, which put the read inside the death branch:
// by then damage_credited() has already run
// `if (!AWAKE(victim)) if (victim->specials.fighting) stop_fighting(victim);`
// (fight.cpp), and stop_fighting() clears specials.fighting for a dead
// character -- so a read in the death branch would answer nullptr every time
// and the fallback could never fire. The capture therefore sits immediately
// ABOVE that stop_fighting() call, which is the last instant the engagement is
// still observable. NotFightingAnybody below is the positive control that the
// fallback stays off when there is genuinely no opponent.

namespace {

// Puts `victim` one poison tick from death: one hit point, and a constitution
// of 2 so update_pos()'s `GET_HIT <= -GET_CON / 2` death test is reachable
// (arm_poison_affect_tick()'s two levers, without the affect -- these tests
// call damage_credited() directly rather than ticking one).
void arm_lethal_hit_points(char_data& victim)
{
    victim.abilities.con = 2;
    victim.constabilities.con = 2; // affect_total() rebuilds tmpabilities from THIS one
    victim.tmpabilities.con = 2;
    victim.tmpabilities.hit = 1;
    victim.specials.position = POSITION_STANDING;
}

} // namespace

TEST(SourcelessKillCredit, FallsBackToTheEngagedPlayerOpponent)
{
    ScopedTestWorld test_world { kWorldRoomCount };
    ScopedCreditZoneTable zone_table_owner;
    ScopedCreditMobIndex prototype_table;
    ScopedRacialStartRooms start_rooms;
    ScopedGlobalCharacterLists global_lists;
    ScopedNoOpCrashCrashsave no_rent_file;
    ScopedNoOpDeathPersistence no_death_files;

    MortalPlayer player;
    CreditedKiller opponent { /*npc=*/false };
    ScopedRoomOccupants death_room { room_by_id_total(kDeathRoom), kDeathRoom,
        { &player.ch, &opponent.ch } };
    obj_data* const previous_object_list = object_list;
    ScopedRecordingExtractCharHook extraction;
    ScopedRecordingCharacterDiedHook death;

    set_fighting(&player.ch, &opponent.ch);
    set_fighting(&opponent.ch, &player.ch);
    arm_lethal_hit_points(player.ch);

    // The sourceless tick: nobody is credited, and the victim "attacks" itself.
    const int died = damage_credited(&player.ch, &player.ch, nullptr, 5, SPELL_POISON, 0);

    release_test_corpse(room_by_id_total(kDeathRoom), previous_object_list);

    EXPECT_EQ(died, 1);
    ASSERT_TRUE(g_recorded_death.called) << "the tick must have killed the player";
    EXPECT_EQ(g_recorded_death.killer, &opponent.ch)
        << "a sourceless kill is credited to the opponent the victim was engaged with";
    expect_player_kill_arm(player);
}

TEST(SourcelessKillCredit, FallsBackToTheEngagedMobOpponent)
{
    ScopedTestWorld test_world { kWorldRoomCount };
    ScopedCreditZoneTable zone_table_owner;
    ScopedCreditMobIndex prototype_table;
    ScopedRacialStartRooms start_rooms;
    ScopedGlobalCharacterLists global_lists;
    ScopedNoOpCrashCrashsave no_rent_file;
    ScopedNoOpDeathPersistence no_death_files;

    MortalPlayer player;
    CreditedKiller orc { /*npc=*/true };
    ScopedRoomOccupants death_room { room_by_id_total(kDeathRoom), kDeathRoom,
        { &player.ch, &orc.ch } };
    obj_data* const previous_object_list = object_list;
    ScopedRecordingExtractCharHook extraction;
    ScopedRecordingCharacterDiedHook death;

    set_fighting(&player.ch, &orc.ch);
    set_fighting(&orc.ch, &player.ch);
    arm_lethal_hit_points(player.ch);

    const int died = damage_credited(&player.ch, &player.ch, nullptr, 5, SPELL_POISON, 0);

    release_test_corpse(room_by_id_total(kDeathRoom), previous_object_list);

    EXPECT_EQ(died, 1);
    ASSERT_TRUE(g_recorded_death.called);
    EXPECT_EQ(g_recorded_death.killer, &orc.ch)
        << "the fallback names whatever the victim was fighting, mob or player";
    // A mob kill is not a player kill: the harsher arm, exactly as before.
    expect_stat_penalty_arm(player);
}

// R2: naming the engaged opponent as the killer is not only a label -- it puts
// that opponent through everything die() does with a killer. group_gain() is
// the visible half (the kill mudlog and the MOB_MEMORY forget are the other
// two), and it only runs for an NPC victim or a connected player, so this case
// uses an NPC victim where the other three use a player to read raw_kill()'s
// arms. Before the fallback the tick reached die(NULL) and the opponent
// standing over the corpse earned nothing for it.
TEST(SourcelessKillCredit, TheEngagedOpponentCollectsTheKillsExperience)
{
    ScopedTestWorld test_world { kWorldRoomCount };
    ScopedCreditZoneTable zone_table_owner;
    ScopedCreditMobIndex prototype_table;
    ScopedRacialStartRooms start_rooms;
    ScopedGlobalCharacterLists global_lists;
    ScopedNoOpCrashCrashsave no_rent_file;
    ScopedNoOpDeathPersistence no_death_files;

    FragileNpc victim;
    CreditedKiller brawler { /*npc=*/false };
    victim.ch.points.exp = 1000000; // group_gain()'s share is GET_EXP(dead_man) / 10
    ScopedRoomOccupants death_room { room_by_id_total(kDeathRoom), kDeathRoom,
        { &victim.ch, &brawler.ch } };
    obj_data* const previous_object_list = object_list;
    ScopedRecordingExtractCharHook extraction;
    ScopedRecordingCharacterDiedHook death;

    set_fighting(&brawler.ch, &victim.ch);
    set_fighting(&victim.ch, &brawler.ch);
    ASSERT_EQ(brawler.ch.points.exp, 0);

    const int died = damage_credited(&victim.ch, &victim.ch, nullptr, 5, SPELL_POISON, 0);

    release_test_corpse(room_by_id_total(kDeathRoom), previous_object_list);

    EXPECT_EQ(died, 1);
    ASSERT_TRUE(g_recorded_death.called);
    EXPECT_EQ(g_recorded_death.killer, &brawler.ch);
    EXPECT_GT(brawler.ch.points.exp, 0)
        << "the engaged opponent IS the killer, so group_gain() pays it for the kill; "
           "a sourceless tick used to reach die(NULL) and award nobody";
}

TEST(SourcelessKillCredit, CreditsNobodyWhenTheVictimIsNotFightingAnybody)
{
    ScopedTestWorld test_world { kWorldRoomCount };
    ScopedCreditZoneTable zone_table_owner;
    ScopedCreditMobIndex prototype_table;
    ScopedRacialStartRooms start_rooms;
    ScopedGlobalCharacterLists global_lists;
    ScopedNoOpCrashCrashsave no_rent_file;
    ScopedNoOpDeathPersistence no_death_files;

    MortalPlayer player;
    ScopedRoomOccupants death_room { room_by_id_total(kDeathRoom), kDeathRoom, { &player.ch } };
    obj_data* const previous_object_list = object_list;
    ScopedRecordingExtractCharHook extraction;
    ScopedRecordingCharacterDiedHook death;

    ASSERT_EQ(player.ch.specials.fighting, nullptr);
    arm_lethal_hit_points(player.ch);

    const int died = damage_credited(&player.ch, &player.ch, nullptr, 5, SPELL_POISON, 0);

    release_test_corpse(room_by_id_total(kDeathRoom), previous_object_list);

    EXPECT_EQ(died, 1);
    ASSERT_TRUE(g_recorded_death.called);
    EXPECT_EQ(g_recorded_death.killer, nullptr)
        << "with no opponent there is nothing to fall back to -- the nobody arm is unchanged";
    expect_stat_penalty_arm(player);
}

// ---------------------------------------------------------------------------
// TASK-026 step 2: kill_contributors() -- who took part in this death
// ---------------------------------------------------------------------------
//
// A pure function over the combat list, the recorded poisoner and the primary
// killer. It answers the question pkill.cpp's three combat_list walks used to
// answer for themselves, and answers it better: a poisoner who is not in the
// room (and a room-affect caster who ticked the killing blow from another zone
// entirely) can be a contributor, which no walk of combat_list can ever see.

namespace {

// A contributor candidate that is nothing but a level and an act-flag word --
// kill_contributors() reads no more than that off anyone it considers, except
// for the pet redirect, which also reads `master` and the two locations.
struct Contributor {
    char_data ch {};
    char_prof_data profs {};
    char name[16] = "contributor";

    Contributor(bool npc, int level)
    {
        ch.profs = &profs;
        if (npc) {
            ch.specials2.act = MOB_ISNPC;
            ch.nr = 0;
            ch.player.short_descr = name;
        } else {
            ch.specials2.act = 0;
            ch.player.name = name;
        }
        ch.player.race = RACE_HUMAN;
        ch.player.level = level;
        ch.specials.position = POSITION_STANDING;
    }
};

// Links `fighter` into the process-wide combat list as an attacker of
// `victim`, the way set_fighting() would -- done by hand so a test can seat a
// character in the list without a room, a world or a two-way engagement.
void engage(char_data& fighter, char_data& victim)
{
    fighter.specials.fighting = &victim;
    fighter.next_fighting = combat_list;
    combat_list = &fighter;
}

} // namespace

TEST(KillContributors, CollectsEveryCharacterFightingTheVictim)
{
    ScopedGlobalCharacterLists global_lists;

    Contributor victim { /*npc=*/false, 10 };
    Contributor first { /*npc=*/false, 20 };
    Contributor second { /*npc=*/true, 25 };
    Contributor bystander { /*npc=*/false, 30 };
    Contributor other_victim { /*npc=*/false, 10 };

    engage(first.ch, victim.ch);
    engage(second.ch, victim.ch);
    engage(bystander.ch, other_victim.ch); // fighting somebody else entirely

    const auto contributors = rots::combat::kill_contributors(&victim.ch, nullptr);

    EXPECT_EQ(contributors.count, 2);
    EXPECT_TRUE(contributors.contains(&first.ch));
    EXPECT_TRUE(contributors.contains(&second.ch))
        << "the fighter walk keeps NPCs: pkill_weight() has always summed their levels too";
    EXPECT_FALSE(contributors.contains(&bystander.ch))
        << "somebody fighting a different character is not a contributor to this death";
}

TEST(KillContributors, DeduplicatesACharacterThatArrivesByEveryRoute)
{
    ScopedGlobalCharacterLists global_lists;

    Contributor victim { /*npc=*/false, 10 };
    Contributor killer { /*npc=*/false, 20 };
    ScopedCharExists registered { killer.ch, kKillerAbsNumber };
    victim.ch.specials.poisoned_by_abs_number = killer.ch.abs_number;
    victim.ch.specials.poisoned_by = &killer.ch;
    engage(killer.ch, victim.ch);

    // Fighting the victim, recorded as its poisoner AND named as the primary.
    const auto contributors = rots::combat::kill_contributors(&victim.ch, &killer.ch);

    EXPECT_EQ(contributors.count, 1) << "one character, one entry, whatever route it arrived by";
    EXPECT_TRUE(contributors.contains(&killer.ch));
}

TEST(KillContributors, IncludesTheRecordedPoisonerAndAPrimaryThatIsNowhereNearTheFight)
{
    ScopedGlobalCharacterLists global_lists;

    Contributor victim { /*npc=*/false, 10 };
    Contributor poisoner { /*npc=*/false, 20 };
    Contributor caster { /*npc=*/false, 30 };
    ScopedCharExists registered { poisoner.ch, kKillerAbsNumber };
    victim.ch.specials.poisoned_by_abs_number = poisoner.ch.abs_number;
    victim.ch.specials.poisoned_by = &poisoner.ch;

    // Neither is fighting the victim, and neither is in its room -- the whole
    // point: a combat_list walk could never find either of them.
    const auto contributors = rots::combat::kill_contributors(&victim.ch, &caster.ch);

    EXPECT_EQ(contributors.count, 2);
    EXPECT_TRUE(contributors.contains(&poisoner.ch));
    EXPECT_TRUE(contributors.contains(&caster.ch));
}

TEST(KillContributors, DropsAPoisonRecordThatNoLiveCharacterAnswersFor)
{
    ScopedGlobalCharacterLists global_lists;

    Contributor victim { /*npc=*/false, 10 };
    Contributor poisoner { /*npc=*/false, 20 };
    {
        ScopedCharExists registered { poisoner.ch, kKillerAbsNumber };
        victim.ch.specials.poisoned_by_abs_number = poisoner.ch.abs_number;
        victim.ch.specials.poisoned_by = &poisoner.ch;
    }
    ASSERT_EQ(resolve_poisoner(victim.ch), nullptr);

    const auto contributors = rots::combat::kill_contributors(&victim.ch, nullptr);

    EXPECT_EQ(contributors.count, 0)
        << "an extracted poisoner contributes nothing -- and is never dereferenced to find out";
}

TEST(KillContributors, ExcludesTheVictimItselfAndImmortals)
{
    ScopedGlobalCharacterLists global_lists;

    Contributor victim { /*npc=*/false, 10 };
    Contributor immortal { /*npc=*/false, LEVEL_IMMORT };
    engage(victim.ch, victim.ch); // a room tick's self-damage shape
    engage(immortal.ch, victim.ch);

    const auto contributors = rots::combat::kill_contributors(&victim.ch, &victim.ch);

    EXPECT_EQ(contributors.count, 0);
    EXPECT_FALSE(contributors.contains(&victim.ch))
        << "a character never contributes to its own death";
    EXPECT_FALSE(contributors.contains(&immortal.ch))
        << "immortals are not pkillers -- pkill_valid_killer() has always said so";
}

TEST(KillContributors, RedirectsAPetToItsMasterWhenTheMasterStandsWithIt)
{
    ScopedTestWorld test_world { kWorldRoomCount };
    ScopedGlobalCharacterLists global_lists;

    Contributor victim { /*npc=*/false, 10 };
    Contributor pet { /*npc=*/true, 15 };
    Contributor master { /*npc=*/false, 20 };
    SET_BIT(pet.ch.specials2.act, MOB_PET);
    pet.ch.master = &master.ch;

    ScopedRoomOccupants death_room { room_by_id_total(kDeathRoom), kDeathRoom,
        { &victim.ch, &pet.ch, &master.ch } };
    engage(pet.ch, victim.ch);

    const auto contributors = rots::combat::kill_contributors(&victim.ch, nullptr);

    EXPECT_EQ(contributors.count, 1);
    EXPECT_TRUE(contributors.contains(&master.ch))
        << "the pet's kill belongs to whoever is holding its leash";
    EXPECT_FALSE(contributors.contains(&pet.ch));
}

TEST(KillContributors, KeepsAPetWhoseMasterIsElsewhere)
{
    ScopedTestWorld test_world { kWorldRoomCount };
    ScopedGlobalCharacterLists global_lists;

    Contributor victim { /*npc=*/false, 10 };
    Contributor pet { /*npc=*/true, 15 };
    Contributor master { /*npc=*/false, 20 };
    SET_BIT(pet.ch.specials2.act, MOB_PET);
    pet.ch.master = &master.ch;

    ScopedRoomOccupants death_room { room_by_id_total(kDeathRoom), kDeathRoom,
        { &victim.ch, &pet.ch } };
    ScopedRoomOccupants remote_room { room_by_id_total(kRemoteRoom), kRemoteRoom, { &master.ch } };
    engage(pet.ch, victim.ch);

    const auto contributors = rots::combat::kill_contributors(&victim.ch, nullptr);

    EXPECT_EQ(contributors.count, 1);
    EXPECT_TRUE(contributors.contains(&pet.ch))
        << "damage_credited()'s own pet redirect has always required the same room; so does this";
    EXPECT_FALSE(contributors.contains(&master.ch));
}

TEST(KillContributors, DropsContributorsPastCapacityInsteadOfOverrunningTheArray)
{
    ScopedGlobalCharacterLists global_lists;

    constexpr int kCapacity = rots::combat::kill_contributor_list::kCapacity;
    Contributor victim { /*npc=*/false, 10 };
    std::vector<std::unique_ptr<Contributor>> crowd;
    for (int i = 0; i < kCapacity + 3; ++i) {
        crowd.push_back(std::make_unique<Contributor>(/*npc=*/false, 20));
        engage(crowd.back()->ch, victim.ch);
    }

    const auto contributors = rots::combat::kill_contributors(&victim.ch, nullptr);

    EXPECT_EQ(contributors.count, kCapacity) << "the list never grows past its fixed capacity";
    for (int i = 0; i < contributors.count; ++i) {
        EXPECT_NE(contributors.entries[i], nullptr);
    }
}

TEST(KillContributors, AddRefusesADuplicateAndAFullList)
{
    rots::combat::kill_contributor_list list;
    Contributor first { /*npc=*/false, 20 };

    EXPECT_TRUE(list.add(&first.ch));
    EXPECT_EQ(list.count, 1);
    EXPECT_FALSE(list.add(&first.ch)) << "a duplicate is refused, not appended";
    EXPECT_EQ(list.count, 1);

    std::vector<std::unique_ptr<Contributor>> filler;
    while (list.count < rots::combat::kill_contributor_list::kCapacity) {
        filler.push_back(std::make_unique<Contributor>(/*npc=*/false, 20));
        ASSERT_TRUE(list.add(&filler.back()->ch));
    }

    Contributor overflow { /*npc=*/false, 20 };
    EXPECT_FALSE(list.add(&overflow.ch)) << "a full list refuses, and says so";
    EXPECT_EQ(list.count, rots::combat::kill_contributor_list::kCapacity);
    EXPECT_FALSE(list.contains(&overflow.ch));
}

// ---------------------------------------------------------------------------
// TASK-026 step 3: pkill.cpp's three walks iterate the contributor list
// ---------------------------------------------------------------------------
//
// All three used to walk `combat_list` themselves, filtering on
// `c->specials.fighting == victim`. None of them had a test of any kind. They
// are declared here rather than in a header because pkill.cpp declares them
// nowhere else -- the local-extern treatment this file already gives
// raw_kill() and affect_update_person().

int pkill_weight(struct char_data* victim,
    const rots::combat::kill_contributor_list& contributors);
int pkill_opponents(struct char_data* victim,
    const rots::combat::kill_contributor_list& contributors);
int pkill_update_pkill_tab(struct char_data* victim, int w, int n,
    const rots::combat::kill_contributor_list& contributors);
extern PKILL* pkill_tab;
extern int pkill_tab_len;

namespace {

// pkill_update_pkill_tab() appends to the process-wide record table and there
// is no API that shrinks it again, so a test owns a private empty one for its
// scope and frees whatever the call allocated.
class ScopedEmptyPkillTable {
public:
    ScopedEmptyPkillTable()
        : m_previous_tab(pkill_tab)
        , m_previous_len(pkill_tab_len)
    {
        pkill_tab = nullptr;
        pkill_tab_len = 0;
    }
    ~ScopedEmptyPkillTable()
    {
        std::free(pkill_tab);
        pkill_tab = m_previous_tab;
        pkill_tab_len = m_previous_len;
    }
    ScopedEmptyPkillTable(const ScopedEmptyPkillTable&) = delete;
    ScopedEmptyPkillTable& operator=(const ScopedEmptyPkillTable&) = delete;

private:
    // The table and length this scope displaced, restored verbatim on exit.
    PKILL* m_previous_tab;
    int m_previous_len;
};

} // namespace

TEST(PkillContributorWalks, WeightSumsEveryContributorsLevelIncludingNpcs)
{
    Contributor victim { /*npc=*/false, 30 };
    Contributor player { /*npc=*/false, 20 };
    Contributor orc { /*npc=*/true, 10 };

    rots::combat::kill_contributor_list contributors;
    ASSERT_TRUE(contributors.add(&player.ch));
    ASSERT_TRUE(contributors.add(&orc.ch));

    // GET_LEVEL(victim) * 1000 / (total * total), total = 20 + 10.
    EXPECT_EQ(pkill_weight(&victim.ch, contributors), 30 * 1000 / (30 * 30))
        << "an NPC contributor's level counts toward the weight, as every NPC "
           "fighter's always did";
    EXPECT_EQ(pkill_weight(&victim.ch, rots::combat::kill_contributor_list {}), 0)
        << "no contributors, no weight -- and no division by zero";
}

// R1: a named consequence of routing the walks through the contributor set.
// pkill_weight()'s denominator used to be "every character in combat_list
// fighting the victim", with no validity filter at all -- an immortal helping
// out inflated it and shrank the weight of everybody else's kill. The
// contributor set excludes immortals, so the denominator is now the mortals'
// levels alone. (The set's pet redirect has the same character: a pet fighting
// beside its master contributes the MASTER's level to this sum, not its own.)
TEST(PkillContributorWalks, AnImmortalNeverEntersTheWeightDenominator)
{
    ScopedGlobalCharacterLists global_lists;

    Contributor victim { /*npc=*/false, 30 };
    Contributor mortal { /*npc=*/false, 20 };
    Contributor immortal { /*npc=*/false, LEVEL_IMMORT };
    engage(mortal.ch, victim.ch);
    engage(immortal.ch, victim.ch);

    const auto contributors = rots::combat::kill_contributors(&victim.ch, nullptr);
    ASSERT_EQ(contributors.count, 1);

    // GET_LEVEL(victim) * 1000 / (total * total), total = 20 -- NOT 20 + 91,
    // which the pre-TASK-026 combat_list walk would have summed (30000 / 12321
    // == 2, an eyewateringly different weight).
    EXPECT_EQ(pkill_weight(&victim.ch, contributors), 30 * 1000 / (20 * 20));
}

TEST(PkillContributorWalks, OpponentsCountsOnlyValidKillers)
{
    Contributor victim { /*npc=*/false, 30 };
    Contributor player { /*npc=*/false, 20 };
    Contributor immortal { /*npc=*/false, LEVEL_IMMORT };
    Contributor plain_mob { /*npc=*/true, 25 };

    rots::combat::kill_contributor_list contributors;
    ASSERT_TRUE(contributors.add(&player.ch));
    ASSERT_TRUE(contributors.add(&immortal.ch));
    ASSERT_TRUE(contributors.add(&plain_mob.ch));

    EXPECT_EQ(pkill_opponents(&victim.ch, contributors), 1)
        << "pkill_valid_killer() is applied per entry: the immortal and the "
           "unaffiliated mob are not player killers";
}

TEST(PkillContributorWalks, UpdatePkillTabWritesOneRecordPerValidContributor)
{
    ScopedEmptyPkillTable private_table;

    Contributor victim { /*npc=*/false, 30 };
    Contributor first { /*npc=*/false, 20 };
    Contributor second { /*npc=*/false, 25 };
    Contributor immortal { /*npc=*/false, LEVEL_IMMORT };
    victim.ch.specials2.idnum = 4001;
    first.ch.specials2.idnum = 4002;
    second.ch.specials2.idnum = 4003;
    immortal.ch.specials2.idnum = 4004;

    rots::combat::kill_contributor_list contributors;
    ASSERT_TRUE(contributors.add(&first.ch));
    ASSERT_TRUE(contributors.add(&immortal.ch));
    ASSERT_TRUE(contributors.add(&second.ch));

    const int opponents = pkill_opponents(&victim.ch, contributors);
    ASSERT_EQ(opponents, 2);
    const int start = pkill_update_pkill_tab(&victim.ch, /*w=*/1, opponents, contributors);

    ASSERT_EQ(start, 0);
    ASSERT_EQ(pkill_tab_len, 2);
    EXPECT_EQ(pkill_tab[0].killer, first.ch.specials2.idnum);
    EXPECT_EQ(pkill_tab[0].victim, victim.ch.specials2.idnum);
    EXPECT_EQ(pkill_tab[0].killer_level, 20);
    EXPECT_EQ(pkill_tab[1].killer, second.ch.specials2.idnum)
        << "the records follow the contributor list, skipping the invalid entry "
           "between them rather than shifting a record onto it";
    EXPECT_EQ(pkill_tab[1].victim_level, 30);
}

// ---------------------------------------------------------------------------
// TASK-026 step 4: die()'s early-out is "nobody took part", not "poison"
// ---------------------------------------------------------------------------
//
// die() used to skip pkill_create() whenever a poison death found the victim
// not fighting anyone -- the TODO on that block asked for exactly this fix.
// The condition is now the contributor set: no contributors, no record. A
// poisoner who is not in the room, and a room-affect caster who ticked the
// killing blow from another zone, are contributors, so their kills DO record.
//
// These drive the real die() (fight.cpp) with a capturing pkill hook. The
// seven cases are TASK-026's acceptance criteria.

// fight.cpp's own declaration (no header carries it).
void die(char_data* dead_man, char_data* killer, int attack_type);

namespace {

struct RecordedPkillCreate {
    // The victim die() named, and a copy of the set it handed over.
    char_data* victim = nullptr;
    rots::combat::kill_contributor_list contributors;
    // How many times the hook fired -- 0 is the "no record" observable.
    int calls = 0;
};

RecordedPkillCreate g_recorded_pkill;

void capturing_pkill_create(char_data* victim,
    const rots::combat::kill_contributor_list& contributors)
{
    g_recorded_pkill.victim = victim;
    g_recorded_pkill.contributors = contributors;
    ++g_recorded_pkill.calls;
}

// Which exploit record types the death captured, in order. The suite reads it
// for EXPLOIT_MOBDEATH (was this a mob death?) and EXPLOIT_PK.
std::vector<int> g_captured_exploits;

void capturing_exploit_capture(int record_type, char_data* /*victim*/, int /*int_param*/,
    const char* /*extra*/)
{
    g_captured_exploits.push_back(record_type);
}

bool captured(int record_type)
{
    return std::find(g_captured_exploits.begin(), g_captured_exploits.end(), record_type)
        != g_captured_exploits.end();
}

// ScopedNoOpDeathPersistence's shape, with the no-ops replaced by recorders.
class ScopedCapturingDeathPersistence {
public:
    ScopedCapturingDeathPersistence()
    {
        g_recorded_pkill = RecordedPkillCreate {};
        g_captured_exploits.clear();
        rots::combat::set_pkill_create_hook(capturing_pkill_create);
        rots::persist::set_exploit_capture_hook(capturing_exploit_capture);
    }
    ~ScopedCapturingDeathPersistence()
    {
        register_pkill_create_hook();
        register_exploit_capture_hook();
    }
    ScopedCapturingDeathPersistence(const ScopedCapturingDeathPersistence&) = delete;
    ScopedCapturingDeathPersistence& operator=(const ScopedCapturingDeathPersistence&) = delete;
};

// Everything die() -> raw_kill() needs to run against stack fixtures without
// touching a runtime data tree.
struct DeathHarness {
    ScopedTestWorld test_world { kWorldRoomCount };
    ScopedCreditZoneTable zone_table_owner;
    ScopedCreditMobIndex prototype_table;
    ScopedRacialStartRooms start_rooms;
    ScopedGlobalCharacterLists global_lists;
    ScopedNoOpCrashCrashsave no_rent_file;
    ScopedCapturingDeathPersistence capturing_persistence;
    ScopedRecordingExtractCharHook extraction;
    ScopedRecordingCharacterDiedHook death;
};

// Records `poisoner` as the origin of `victim`'s poison for the caller's scope.
class ScopedPoisonOrigin {
public:
    ScopedPoisonOrigin(char_data& victim, char_data& poisoner, int abs_number)
        : m_registration(poisoner, abs_number)
    {
        victim.specials.poisoned_by_abs_number = poisoner.abs_number;
        victim.specials.poisoned_by = &poisoner;
    }
    ScopedPoisonOrigin(const ScopedPoisonOrigin&) = delete;
    ScopedPoisonOrigin& operator=(const ScopedPoisonOrigin&) = delete;

private:
    // Keeps the poisoner's abs_number slot live so resolve_poisoner() answers.
    ScopedCharExists m_registration;
};

} // namespace

// Case 1: player poison kills a non-fighting victim.
TEST(DieContributorRecord, PlayerPoisonOnANonFightingVictimRecordsThePoisoner)
{
    DeathHarness harness;
    MortalPlayer player;
    CreditedKiller mage { /*npc=*/false };
    ScopedPoisonOrigin origin { player.ch, mage.ch, kKillerAbsNumber };
    ScopedRoomOccupants death_room { room_by_id_total(kDeathRoom), kDeathRoom, { &player.ch } };
    ScopedRoomOccupants remote_room { room_by_id_total(kRemoteRoom), kRemoteRoom, { &mage.ch } };
    obj_data* const previous_object_list = object_list;

    ASSERT_EQ(player.ch.specials.fighting, nullptr);
    die(&player.ch, &mage.ch, SPELL_POISON);
    release_test_corpse(room_by_id_total(kDeathRoom), previous_object_list);

    ASSERT_EQ(g_recorded_pkill.calls, 1)
        << "the old early-out skipped the record entirely for a victim not in combat";
    ASSERT_EQ(g_recorded_pkill.contributors.count, 1);
    EXPECT_EQ(g_recorded_pkill.contributors.entries[0], &mage.ch);
    EXPECT_TRUE(captured(EXPLOIT_POISON));
    expect_player_kill_arm(player);
}

// Case 2: mob poison kills a non-fighting victim.
TEST(DieContributorRecord, MobPoisonOnANonFightingVictimIsAMobDeath)
{
    DeathHarness harness;
    MortalPlayer player;
    CreditedKiller snake { /*npc=*/true };
    ScopedPoisonOrigin origin { player.ch, snake.ch, kKillerAbsNumber };
    ScopedRoomOccupants death_room { room_by_id_total(kDeathRoom), kDeathRoom,
        { &player.ch, &snake.ch } };
    obj_data* const previous_object_list = object_list;

    // The XP arithmetic this case now reaches. base_xp_gain is
    // -(exp - 3000) / (level + 2) = -(15000 - 3000) / 12 = -1000, which every
    // arm below takes std::min(0, ...) of. NOTE: 15000 exp is far below the
    // level-10 threshold (xp_to_level(10) = 150000), so gain_exp_regardless()'s
    // level-loss loop DOES fire during the first award (the fixture drops
    // several levels) -- that loop never touches points.exp, so the assertion
    // below still isolates the two awards: 15000 - 100 - 1000. (Corrected at
    // the TASK-026 re-review; an earlier draft of this comment said the loop
    // could not fire.)
    player.ch.points.exp = 15000;
    ASSERT_EQ(GET_LEVEL(&player.ch), 10);

    die(&player.ch, &snake.ch, SPELL_POISON);
    release_test_corpse(room_by_id_total(kDeathRoom), previous_object_list);

    EXPECT_TRUE(captured(EXPLOIT_MOBDEATH)) << "a mob's poison is a mob death";
    EXPECT_TRUE(captured(EXPLOIT_POISON));
    // TASK-026's named consequence on this very criterion: the retired
    // early-out RETURNED, so a mob's poison kill of a victim who was not
    // fighting stopped after the -100 (base_xp_gain / 10) award and never
    // reached die()'s own `if (IS_NPC(killer))` mob-death arm. It does now, and
    // that arm is the full -1000 -- a ten-fold XP loss where AC#2 asks for
    // "mob death (EXPLOIT_MOBDEATH, stat penalty)". 15000 - 100 - 1000.
    EXPECT_EQ(player.ch.points.exp, 13900)
        << "the mob-death XP arm applies to a non-fighting poison death now; the "
           "old early-out charged only base_xp_gain / 10";
    expect_stat_penalty_arm(player);
}

// R4 / MINOR-4: an immortal is not a contributor, so its kill records nothing --
// but everything else about the death still runs. The empty-set guard is the
// only thing that skips, and this pins that it skips exactly two statements.
TEST(DieContributorRecord, AnImmortalsKillWritesNoRecordAtAll)
{
    DeathHarness harness;
    MortalPlayer player;
    CreditedKiller immortal { /*npc=*/false };
    immortal.ch.player.level = LEVEL_IMMORT;
    ScopedRoomOccupants death_room { room_by_id_total(kDeathRoom), kDeathRoom,
        { &player.ch, &immortal.ch } };
    obj_data* const previous_object_list = object_list;

    ASSERT_EQ(player.ch.specials.fighting, nullptr);
    die(&player.ch, &immortal.ch, TYPE_HIT);
    release_test_corpse(room_by_id_total(kDeathRoom), previous_object_list);

    EXPECT_EQ(g_recorded_pkill.calls, 0)
        << "immortals are excluded from the contributor set, so nobody took part";
    EXPECT_FALSE(captured(EXPLOIT_PK));
    EXPECT_TRUE(captured(EXPLOIT_DEATH))
        << "the death record itself is unaffected -- only the PK pair is guarded";
}

// Case 3: a player's tick kills a victim who is fighting other players, with
// the caster nowhere near the room.
TEST(DieContributorRecord, ARemoteCastersTickRecordsBothTheCasterAndTheEngagedPlayers)
{
    DeathHarness harness;
    MortalPlayer player;
    CreditedKiller caster { /*npc=*/false };
    CreditedKiller brawler { /*npc=*/false };
    ScopedRoomOccupants death_room { room_by_id_total(kDeathRoom), kDeathRoom,
        { &player.ch, &brawler.ch } };
    ScopedRoomOccupants remote_room { room_by_id_total(kRemoteRoom), kRemoteRoom, { &caster.ch } };
    obj_data* const previous_object_list = object_list;

    set_fighting(&brawler.ch, &player.ch);
    die(&player.ch, &caster.ch, SPELL_BLAZE);
    release_test_corpse(room_by_id_total(kDeathRoom), previous_object_list);

    ASSERT_EQ(g_recorded_pkill.calls, 1);
    EXPECT_EQ(g_recorded_pkill.contributors.count, 2);
    EXPECT_TRUE(g_recorded_pkill.contributors.contains(&brawler.ch))
        << "the players who were actually swinging are still in the record";
    EXPECT_TRUE(g_recorded_pkill.contributors.contains(&caster.ch))
        << "and so is the caster whose tick landed the killing blow, from another room";
    expect_player_kill_arm(player);
}

// Case 4: mob poison kills a victim who is fighting players.
//
// The engagement here is deliberately ONE-WAY -- the brawler fights the player,
// the player's own `specials.fighting` stays null. That is the shape the old
// early-out could not see (it asked only whether the VICTIM was fighting) and
// it is a real shape in play: a victim who fled, was bashed out of the fight or
// simply never swung back is still being beaten on by everyone in the room.
// `kill_contributors()` walks combat_list for characters fighting the victim,
// so it finds the brawler either way.
TEST(DieContributorRecord, MobPoisonOnAnEngagedVictimStillRecordsTheEngagedPlayers)
{
    DeathHarness harness;
    MortalPlayer player;
    CreditedKiller snake { /*npc=*/true };
    CreditedKiller brawler { /*npc=*/false };
    ScopedPoisonOrigin origin { player.ch, snake.ch, kKillerAbsNumber };
    ScopedRoomOccupants death_room { room_by_id_total(kDeathRoom), kDeathRoom,
        { &player.ch, &snake.ch, &brawler.ch } };
    obj_data* const previous_object_list = object_list;

    set_fighting(&brawler.ch, &player.ch);
    die(&player.ch, &snake.ch, SPELL_POISON);
    release_test_corpse(room_by_id_total(kDeathRoom), previous_object_list);

    EXPECT_TRUE(captured(EXPLOIT_MOBDEATH)) << "the mob still gets the death";
    ASSERT_EQ(g_recorded_pkill.calls, 1);
    EXPECT_TRUE(g_recorded_pkill.contributors.contains(&brawler.ch));
    expect_stat_penalty_arm(player);
}

// Case 5: poisoned by player A, finished off by player B.
TEST(DieContributorRecord, APoisonerContributesEvenWhenSomebodyElseLandsTheBlow)
{
    DeathHarness harness;
    MortalPlayer player;
    CreditedKiller poisoner { /*npc=*/false };
    CreditedKiller finisher { /*npc=*/false };
    ScopedPoisonOrigin origin { player.ch, poisoner.ch, kKillerAbsNumber };
    ScopedRoomOccupants death_room { room_by_id_total(kDeathRoom), kDeathRoom,
        { &player.ch, &finisher.ch } };
    ScopedRoomOccupants remote_room { room_by_id_total(kRemoteRoom), kRemoteRoom,
        { &poisoner.ch } };
    obj_data* const previous_object_list = object_list;

    set_fighting(&finisher.ch, &player.ch);
    die(&player.ch, &finisher.ch, TYPE_HIT);
    release_test_corpse(room_by_id_total(kDeathRoom), previous_object_list);

    ASSERT_EQ(g_recorded_pkill.calls, 1);
    EXPECT_EQ(g_recorded_pkill.contributors.count, 2);
    EXPECT_TRUE(g_recorded_pkill.contributors.contains(&finisher.ch));
    EXPECT_TRUE(g_recorded_pkill.contributors.contains(&poisoner.ch))
        << "participation survives the poison not being what finally killed";
}

// Case 7: sourceless poison, victim fighting nobody. (Case 6 -- sourceless
// poison on an ENGAGED victim -- is SourcelessKillCredit.* above, which drives
// the same decision one layer down, in damage_credited().)
TEST(DieContributorRecord, NobodyTookPartSoNoRecordIsCreated)
{
    DeathHarness harness;
    MortalPlayer player;
    ScopedRoomOccupants death_room { room_by_id_total(kDeathRoom), kDeathRoom, { &player.ch } };
    obj_data* const previous_object_list = object_list;

    // The killer is the victim itself -- the shape a self-damaging tick with no
    // resolvable source leaves behind. It is not a contributor to its own death.
    die(&player.ch, &player.ch, SPELL_POISON);
    release_test_corpse(room_by_id_total(kDeathRoom), previous_object_list);

    EXPECT_EQ(g_recorded_pkill.calls, 0) << "no contributors, no PK record";
    EXPECT_FALSE(captured(EXPLOIT_PK));
    EXPECT_TRUE(captured(EXPLOIT_POISON)) << "the poison exploit is still captured, as before";
}
