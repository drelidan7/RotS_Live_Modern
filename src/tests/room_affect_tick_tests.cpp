// src/tests/room_affect_tick_tests.cpp
//
// TASK-021 Task 5. room_affect_tick() replaces affect_update_room()'s
// self-re-cast arm -- the historical tick that ran
// `(skills[loc].spell_pointer)(tmpch, "", SPELL_TYPE_SPELL, tmpch, ...)`, i.e.
// re-cast the room's spell with the OCCUPANT standing in for the caster. Every
// formula input therefore came from the victim: a level-1 mob walking into a
// grandmaster's blaze took a level-1 blaze, and nobody was ever credited with
// the kill.
//
// WHAT THESE TESTS PROVE. A "the tick matches a live re-cast" equivalence test
// would be vacuous here: the live helper forms are one-line forwarders onto the
// snapshot forms (Task 2), so both sides run the same body no matter which
// fields the tick reads. Every test below therefore VARIES a snapshot field --
// or the recorded caster's existence, or the room it stands in -- and pins the
// resulting change in the tick's own observable (damage dealt, affect modifier,
// affect duration, who die() is told killed the victim, which room a mist
// spreads into). A tick that read the occupant's stats, or the caster's LIVE
// stats, or credited the wrong character, goes red.
//
// FIXTURES: the recording extract-char stub, the one-entry mob_index, the
// corpse release and ScopedCharExists are file-local copies of
// affect_update_tests.cpp's / fight_credit_tests.cpp's -- the repo's
// per-file-copy idiom for these (no shared header declares them). The zone
// table is fight_credit_tests.cpp's TWO-SLOT copy, not test_placement.h's
// shared ScopedZoneTableOwner: recalc_zone_power() reads top_of_zone_table as
// an inclusive top index and WRITES zone_table[top_of_zone_table], which
// overruns a one-element allocation (fight_credit_tests.cpp documents the trap
// in full). Any test here that lets a character die must use the two-slot one.

#include "../big_brother.h"
#include "../db.h"
#include "../entity_hooks.h"
#include "../handler.h"
#include "../room_affect_tick.h"
#include "../spells.h"
#include "../utils.h"
#include "../zone.h"
#include "rots/core/caster_snapshot.h"
#include "rots/core/character.h"
#include "rots/core/object.h"
#include "rots/core/room.h"

#include "test_placement.h"
#include "test_random_utils.h"
#include "test_world.h"

#include <cstdlib>
#include <gtest/gtest.h>
#include <string>

extern char_data* combat_list;
extern char_data* combat_next_dude;
extern char_data* waiting_list;
extern char_data* character_list;
extern obj_data* object_list;
extern struct index_data* mob_index;

extern struct skill_data skills[];

void affect_update_room(struct room_data* room);
ASPELL(spell_blaze);

namespace {

// Rooms this suite uses: the affected room, the room a remote recorded caster
// stands in, and the room a mist spreads/moves into. Room 0 is left alone.
constexpr int kWorldRoomCount = 8;
constexpr int kAffectedRoom = 1;
constexpr int kRemoteRoom = 2;
constexpr int kAdjacentRoom = 3;

// abs_numbers this suite registers, in a band no sibling suite in the
// monolithic runner uses (affect_update_tests: 7901/7902; caster_snapshot_tests:
// 7903; fight_credit_tests: 7920/7921) and inside the char_exists table.
constexpr int kCasterAbsNumber = 7930;
constexpr int kSecondCasterAbsNumber = 7931;
constexpr int kOccupantAbsNumber = 7932;

// Every queued draw answers the same normalized value, so number(from, to)
// returns `from + (to - from + 1) / 2` (integer truncation) at every call site
// and each test can state its expected arithmetic in closed form. 0.5 rather
// than 0.0 on purpose: with 0.0 the blaze's own `number(8, level)` collapses to
// 8 for EVERY level, which would hide exactly the snapshot dependence the first
// test exists to pin.
constexpr double kMidRoll = 0.5;

void queue_mid_rolls(int count)
{
    for (int i = 0; i < count; ++i)
        push_test_random_value(kMidRoll);
}

// The (room, spell) caster store is keyed by room->number -- the room's VNUM,
// unique per room in a real world. dummy_room_data() stamps EVERY test room
// with -1, so without this every room in ScopedTestWorld's world shares one map
// key and a record written for one room silently answers for all of them.
// Stamps distinct numbers on the rooms this suite uses and restores whatever
// was there on scope exit.
class ScopedRoomNumbers {
public:
    ScopedRoomNumbers()
    {
        for (int room_id : kNumberedRooms) {
            room_data* const room = room_by_id_total(room_id);
            m_previous[m_count++] = room->number;
            room->number = 1000 + room_id;
        }
    }
    ~ScopedRoomNumbers()
    {
        int index = 0;
        for (int room_id : kNumberedRooms)
            room_by_id_total(room_id)->number = m_previous[index++];
    }
    ScopedRoomNumbers(const ScopedRoomNumbers&) = delete;
    ScopedRoomNumbers& operator=(const ScopedRoomNumbers&) = delete;

private:
    // The rooms this suite ever records a caster for, and the numbers they
    // carried before this scope stamped them.
    static constexpr int kNumberedRooms[] = { kAffectedRoom, kRemoteRoom, kAdjacentRoom };
    int m_previous[3] {};
    int m_count = 0;
};

// ---------------------------------------------------------------------------
// Recording seams
// ---------------------------------------------------------------------------

struct RecordedExtraction {
    char_data* ch = nullptr;
    int calls = 0;
};
// What the stubbed extract_char hook saw: which character was extracted and how
// many times, read by every test that expects a tick to kill (or not to).
RecordedExtraction g_recorded_extraction;

// The real NPC arm's unlink, minus the free_char() that would turn a stack
// fixture into a crash instead of a witness (affect_update_tests.cpp's stub).
void unlinking_extract_char_stub(char_data* ch, int /*new_room*/)
{
    g_recorded_extraction.ch = ch;
    ++g_recorded_extraction.calls;
    char_from_room(ch);
    remove_char_exists(ch->abs_number);
}

class ScopedTickExtractCharHook {
public:
    ScopedTickExtractCharHook()
    {
        g_recorded_extraction = RecordedExtraction {};
        rots::entity::set_extract_char_hook(unlinking_extract_char_stub);
    }
    ~ScopedTickExtractCharHook() { register_extract_char_hook(); }
    ScopedTickExtractCharHook(const ScopedTickExtractCharHook&) = delete;
    ScopedTickExtractCharHook& operator=(const ScopedTickExtractCharHook&) = delete;
};

struct RecordedDeath {
    char_data* dead_man = nullptr;
    char_data* killer = nullptr;
    bool called = false;
};
// What raw_kill() handed dispatch_character_died(): the dead man and the KILLER
// it was told to credit -- this suite's witness for who takes the kill.
RecordedDeath g_recorded_death;

// raw_kill()'s own dispatch_character_died() is the narrowest observable of
// WHICH character raw_kill() was handed as the killer. group_gain()/points.exp
// cannot serve: group_gain() returns early whenever the killer does not stand
// in the dead man's room (fight.cpp:1288-1290), which is precisely the case a
// room affect whose caster walked away produces.
void recording_character_died_stub(char_data* dead_man, char_data* killer, obj_data* /*corpse*/)
{
    g_recorded_death = RecordedDeath { dead_man, killer, true };
}

class ScopedTickCharacterDiedHook {
public:
    ScopedTickCharacterDiedHook()
    {
        g_recorded_death = RecordedDeath {};
        rots::entity::set_character_died_hook(recording_character_died_stub);
    }
    ~ScopedTickCharacterDiedHook() { register_character_died_hook(); }
    ScopedTickCharacterDiedHook(const ScopedTickCharacterDiedHook&) = delete;
    ScopedTickCharacterDiedHook& operator=(const ScopedTickCharacterDiedHook&) = delete;
};

// The death pipeline reads an NPC's prototype twice (activate_char_special()'s
// SPECIAL_DEATH probe, make_physical_corpse()'s corpse-owner id); publish a
// one-entry table with no spec-proc for the scope.
class ScopedTickMobIndex {
public:
    ScopedTickMobIndex()
        : m_previous(mob_index)
    {
        m_entry = index_data {};
        m_entry.virt = 1;
        mob_index = &m_entry;
    }
    ~ScopedTickMobIndex() { mob_index = m_previous; }
    ScopedTickMobIndex(const ScopedTickMobIndex&) = delete;
    ScopedTickMobIndex& operator=(const ScopedTickMobIndex&) = delete;

private:
    index_data* m_previous; // the table this suite found installed (normally null)
    index_data m_entry {}; // the single prototype slot every fixture NPC's nr = 0 names
};

// gtest_main does not run assign_spell_pointers(); install blaze's real body in
// its skills[] cell for the scope so affect_update_room()'s FALLBACK arm is the
// genuine re-cast, not a null dereference (affect_update_tests.cpp's fixture).
class ScopedBlazeSpellPointer {
public:
    ScopedBlazeSpellPointer()
        : m_previous(skills[SPELL_BLAZE].spell_pointer)
    {
        skills[SPELL_BLAZE].spell_pointer = spell_blaze;
    }
    ~ScopedBlazeSpellPointer() { skills[SPELL_BLAZE].spell_pointer = m_previous; }
    ScopedBlazeSpellPointer(const ScopedBlazeSpellPointer&) = delete;
    ScopedBlazeSpellPointer& operator=(const ScopedBlazeSpellPointer&) = delete;

private:
    void (*m_previous)(char_data*, char*, int, char_data*, obj_data*, int, int); // the cell's prior value
};

// A two-slot zone table: zone_by_id_impl() reads top_of_zone_table as a COUNT,
// recalc_zone_power() as an inclusive TOP INDEX that it WRITES through. Only
// zone 0 is ever a room's zone here; slot 1 exists to be written past.
class ScopedTickZoneTable {
public:
    ScopedTickZoneTable()
        : m_owned_table(new zone_data[2] {})
        , m_previous_zone_table(zone_table)
        , m_previous_top_of_zone_table(top_of_zone_table)
    {
        m_owned_table[0].number = 0;
        m_owned_table[1].number = 1;
        zone_table = m_owned_table;
        top_of_zone_table = 1;
    }
    ~ScopedTickZoneTable()
    {
        zone_table = m_previous_zone_table;
        top_of_zone_table = m_previous_top_of_zone_table;
        delete[] m_owned_table;
    }
    ScopedTickZoneTable(const ScopedTickZoneTable&) = delete;
    ScopedTickZoneTable& operator=(const ScopedTickZoneTable&) = delete;

private:
    // The table this fixture allocated -- deleted by address, never through the
    // global, so a body that repoints zone_table cannot make this a double free.
    zone_data* m_owned_table;
    // The table/count this fixture displaced, restored verbatim on scope exit.
    zone_data* m_previous_zone_table;
    int m_previous_top_of_zone_table;
};

// Registers an abs_number in the char_exists table AND records the pointer, the
// way register_npc_char() does -- caster_snapshot::resolve() needs both halves.
// Idempotent on teardown: the extract stub above also unregisters, so a scope
// whose character died removes an already-removed slot (the affect_update_tests
// precedent, which does the same).
class ScopedTickCharExists {
public:
    ScopedTickCharExists(char_data& ch, int abs_number)
        : m_ch(ch)
    {
        ch.abs_number = abs_number;
        set_char_exists(abs_number, &ch);
    }
    ~ScopedTickCharExists() { remove_char_exists(m_ch.abs_number); }
    ScopedTickCharExists(const ScopedTickCharExists&) = delete;
    ScopedTickCharExists& operator=(const ScopedTickCharExists&) = delete;

private:
    char_data& m_ch; // the character whose registration this scope owns
};

// Saves and empties the four process-wide character lists a death touches, so
// nothing this suite's stack characters were linked into outlives the test.
class ScopedTickGlobalLists {
public:
    ScopedTickGlobalLists()
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
    ~ScopedTickGlobalLists()
    {
        clear_test_random_values();
        combat_list = m_combat_list;
        combat_next_dude = m_combat_next_dude;
        waiting_list = m_waiting_list;
        character_list = m_character_list;
    }
    ScopedTickGlobalLists(const ScopedTickGlobalLists&) = delete;
    ScopedTickGlobalLists& operator=(const ScopedTickGlobalLists&) = delete;

private:
    // The four list heads this scope owns the restoration of.
    char_data* m_combat_list;
    char_data* m_combat_next_dude;
    char_data* m_waiting_list;
    char_data* m_character_list;
};

// Publishes one ROOMAFF_SPELL affect (and its recorded caster) on a room for
// the scope, then takes it back out -- affect_to_room() pushes the room onto
// the process-global affected_list, and affect_remove_room() is what pops it
// and erases the (room, spell) caster record.
class ScopedRoomSpellAffect {
public:
    ScopedRoomSpellAffect(room_data* room, int spell, int duration, int modifier,
        const caster_snapshot& caster)
        : m_room(room)
        , m_spell(spell)
    {
        affected_type af {};
        af.type = ROOMAFF_SPELL;
        af.duration = duration;
        af.modifier = modifier;
        af.location = spell;
        af.bitvector = 0;
        affect_to_room(m_room, &af, caster);
    }
    ~ScopedRoomSpellAffect()
    {
        while (affected_type* live = room_affected_by_spell(m_room, m_spell))
            affect_remove_room(m_room, live);
    }
    ScopedRoomSpellAffect(const ScopedRoomSpellAffect&) = delete;
    ScopedRoomSpellAffect& operator=(const ScopedRoomSpellAffect&) = delete;

    // The live affect this scope published, re-found each call (affect_to_room
    // copies into pooled storage, so the caller never owns the node).
    affected_type& affect() const { return *room_affected_by_spell(m_room, m_spell); }

private:
    room_data* m_room; // the room this scope publishes on and cleans up
    int m_spell; // the ROOMAFF_SPELL location this scope owns
};

// Removes any room affect a body created on a room this suite does not manage
// through ScopedRoomSpellAffect (mist_tick() seeds adjacent rooms).
void clear_room_affects(room_data* room)
{
    while (room->affected)
        affect_remove_room(room, room->affected);
}

// make_corpse() CREATE()s a heap corpse and pushes it onto world[].contents and
// object_list; take both back out (affect_update_tests.cpp's release_corpse).
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

// The room affect's victim: a plain NPC with no specialization and no mage
// levels, so get_character_saving_throw() is (intel - 8) / 4 = 0 and
// get_save_bonus() is 0 -- every remaining term in the tick's arithmetic comes
// from the recorded caster, which is the point.
struct Occupant {
    char_data ch {};
    char_prof_data profs {};
    char short_descr[16] = "tick_victim";

    explicit Occupant(int hit_points)
    {
        ch.profs = &profs;
        ch.player.short_descr = short_descr;
        ch.specials2.act = MOB_ISNPC;
        ch.nr = 0; // prototype slot 0 of ScopedTickMobIndex's one-entry table
        ch.player.race = RACE_HUMAN;
        ch.player.level = 1;
        ch.tmpabilities.intel = 8;
        ch.tmpabilities.con = 10; // saves_poison() defense: GET_CON * 5
        ch.abilities.con = 10;
        ch.points.willpower = 0;
        ch.specials2.perception = 0; // saves_mystic() defense: perception * 9 / 10
        ch.specials2.saving_throw = 0;
        // affect_to_char()/affect_join() -> affect_total() rebuilds tmpabilities
        // from abilities, so the base must carry the intended hit points too.
        ch.abilities.hit = hit_points;
        ch.tmpabilities.hit = hit_points;
        ch.specials.position = POSITION_STANDING;
    }
};

// The recorded caster. A PC (act == 0) so should_apply_spell_penetration() is
// true and the blaze's saving-throw scaling actually reads the snapshot's
// PROF_MAGE level. `intel`/`wil` are chosen as multiples of 25 on purpose:
// get_mage_caster_level()/get_mystic_caster_level() roll
// `number(0, (ability / 5) % 5)` for the non-divisible remainder, and
// (25 / 5) % 5 == 0 makes that a number(0, 0) -- which returns without drawing
// from the queue at all, so each test's draw accounting stays exact.
struct Caster {
    char_data ch {};
    char_prof_data profs {};
    char name[16] = "tick_caster";

    Caster(int mage_prof, int cleric_prof, game_types::player_specs spec)
    {
        ch.profs = &profs;
        ch.player.name = name;
        ch.specials2.act = 0; // not IS_NPC
        ch.player.race = RACE_HUMAN;
        ch.player.level = 30;
        profs.prof_level[PROF_MAGE] = mage_prof;
        profs.prof_level[PROF_CLERIC] = cleric_prof;
        profs.specialization = static_cast<int>(spec);
        ch.tmpabilities.intel = 25;
        ch.tmpabilities.wil = 25;
        ch.points.spell_power = 0;
        ch.points.spell_pen = 0;
        ch.points.willpower = 20; // saves_poison() offence: willpower * 8 * perception / 100
        ch.specials2.perception = 100;
        ch.specials.tactics = 0;
        ch.abilities.hit = 500;
        ch.tmpabilities.hit = 500;
        ch.specials.position = POSITION_STANDING;
    }

    // The death-penalty shape every test applies right after capturing: if the
    // tick read the LIVE caster instead of its snapshot, these values are what
    // it would see.
    void wreck()
    {
        ch.tmpabilities.intel = 3;
        ch.tmpabilities.wil = 3;
        profs.prof_level[PROF_MAGE] = 1;
        profs.prof_level[PROF_CLERIC] = 1;
        profs.specialization = static_cast<int>(game_types::PS_None);
        ch.points.willpower = 1;
        ch.specials2.perception = 1;
    }
};

} // namespace

// ---------------------------------------------------------------------------
// blaze: damage from the snapshot's mage level, not the occupant's
// ---------------------------------------------------------------------------
//
// Arithmetic, with every draw answering kMidRoll (see queue_mid_rolls):
//   level          = mage_prof + intel / 5                 (get_mage_caster_level)
//   dc             = 10 + mage_prof / 3 + (25 - 8) / 4     (get_saving_throw_dc)
//   save roll      = number(1, 20) = 11; save_value = 0    -> saved iff 11 > dc
//   dam            = number(8, level) + 10
//   spell pen      = mage_prof / 5.0; victim saving_throw = -pen
//   multiplier     = 2 - 20 / (20 + pen)                   (scale_spell_damage)
//   hp drop        = int(dam * multiplier)
// prof 25: level 30, dc 22 (never saved), dam = 8 + 11 + 10 = 29, mult 1.2  -> 34
// prof  5: level 10, dc 15 (11 > 15 false, not saved), dam = 8 + 1 + 10 = 19,
//          mult 2 - 20/21 = 1.047619 -> 19
// The occupant's own stats would give level 0 + 8/5 = 1, i.e. number(8, 1) -> a
// swapped number(1, 8) = 4, +10 = 14, and NO spell penetration at all (an NPC
// caster) -> a flat 14. Neither expectation below can be produced that way.
TEST(RoomAffectTick, BlazeTickDamageComesFromTheSnapshotNotTheOccupant)
{
    ScopedTestWorld world { kWorldRoomCount };
    ScopedRoomNumbers room_numbers;
    ScopedTickZoneTable zone_table_owner;
    ScopedTickMobIndex prototype_table;
    ScopedTickGlobalLists global_lists;

    room_data* const room = room_by_id_total(kAffectedRoom);
    Occupant occupant { 500 };
    ScopedRoomOccupants affected_room { room, kAffectedRoom, { &occupant.ch } };

    // --- the grandmaster's blaze -------------------------------------------
    Caster grandmaster { /*mage_prof=*/25, /*cleric_prof=*/0, game_types::PS_None };
    ScopedRoomSpellAffect blaze { room, SPELL_BLAZE, /*duration=*/5, /*modifier=*/25,
        caster_snapshot::capture(grandmaster.ch) };
    grandmaster.wreck(); // the live caster is now a level-1 wreck

    queue_mid_rolls(32);
    ASSERT_TRUE(room_affect_tick(SPELL_BLAZE, room, &occupant.ch, blaze.affect()));
    const int grandmaster_drop = 500 - occupant.ch.tmpabilities.hit;
    clear_test_random_values();

    // --- the apprentice's blaze, same room, same occupant, same rolls ------
    occupant.ch.tmpabilities.hit = 500;
    occupant.ch.specials.fighting = nullptr;
    Caster apprentice { /*mage_prof=*/5, /*cleric_prof=*/0, game_types::PS_None };
    set_room_affect_caster(room, SPELL_BLAZE, caster_snapshot::capture(apprentice.ch));
    apprentice.wreck();

    queue_mid_rolls(32);
    ASSERT_TRUE(room_affect_tick(SPELL_BLAZE, room, &occupant.ch, blaze.affect()));
    const int apprentice_drop = 500 - occupant.ch.tmpabilities.hit;
    clear_test_random_values();

    occupant.ch.specials.fighting = nullptr;

    EXPECT_EQ(grandmaster_drop, 34)
        << "the tick must burn from the recorded PROF_MAGE 25 / intel 25 snapshot";
    EXPECT_EQ(apprentice_drop, 19)
        << "and from PROF_MAGE 5 when that is what the room recorded";
    EXPECT_NE(grandmaster_drop, apprentice_drop)
        << "varying one snapshot field must move the tick's damage";
    EXPECT_NE(grandmaster_drop, 14) << "14 is what the occupant's own stats would deal";
    EXPECT_NE(apprentice_drop, 14);
}

// ---------------------------------------------------------------------------
// blaze: the kill goes to the recorded caster, or to nobody
// ---------------------------------------------------------------------------

TEST(RoomAffectTick, BlazeTickKillCreditsTheRecordedCasterWhenAlive)
{
    ScopedTestWorld world { kWorldRoomCount };
    ScopedRoomNumbers room_numbers;
    ScopedTickZoneTable zone_table_owner;
    ScopedTickMobIndex prototype_table;
    ScopedTickGlobalLists global_lists;

    room_data* const room = room_by_id_total(kAffectedRoom);
    Occupant occupant { 1 }; // con 10 -> update_pos() kills at hit <= -5
    occupant.ch.abilities.con = 0;
    occupant.ch.tmpabilities.con = 0; // ...and at hit <= 0 with con 0
    Caster mage { /*mage_prof=*/25, /*cleric_prof=*/0, game_types::PS_None };
    ScopedTickCharExists mage_exists { mage.ch, kCasterAbsNumber };
    ScopedTickCharExists occupant_exists { occupant.ch, kOccupantAbsNumber };
    ScopedRoomOccupants affected_room { room, kAffectedRoom, { &occupant.ch } };
    ScopedRoomOccupants remote_room { room_by_id_total(kRemoteRoom), kRemoteRoom, { &mage.ch } };

    obj_data* const previous_object_list = object_list;
    ScopedTickExtractCharHook extraction;
    ScopedTickCharacterDiedHook death;
    ScopedRoomSpellAffect blaze { room, SPELL_BLAZE, 5, 25, caster_snapshot::capture(mage.ch) };

    queue_mid_rolls(32);
    ASSERT_TRUE(room_affect_tick(SPELL_BLAZE, room, &occupant.ch, blaze.affect()));
    clear_test_random_values();

    const char_data* const mage_was_fighting = mage.ch.specials.fighting;
    const int mage_location = location_of(&mage.ch);
    mage.ch.specials.fighting = nullptr;
    mage.ch.next_fighting = nullptr;
    release_test_corpse(room, previous_object_list);

    ASSERT_EQ(g_recorded_extraction.calls, 1) << "the tick must kill the one-hit-point occupant";
    EXPECT_EQ(g_recorded_extraction.ch, &occupant.ch);
    ASSERT_TRUE(g_recorded_death.called) << "raw_kill() must have been reached";
    EXPECT_EQ(g_recorded_death.dead_man, &occupant.ch);
    EXPECT_EQ(g_recorded_death.killer, &mage.ch)
        << "die() must be told the RECORDED caster killed it, not the occupant itself";
    EXPECT_EQ(mage_was_fighting, nullptr) << "a caster in another room is credited, never engaged";
    EXPECT_EQ(mage_location, kRemoteRoom) << "and never moved";
}

TEST(RoomAffectTick, BlazeTickKillCreditsNobodyWhenTheCasterIsGone)
{
    ScopedTestWorld world { kWorldRoomCount };
    ScopedRoomNumbers room_numbers;
    ScopedTickZoneTable zone_table_owner;
    ScopedTickMobIndex prototype_table;
    ScopedTickGlobalLists global_lists;

    room_data* const room = room_by_id_total(kAffectedRoom);
    Occupant occupant { 1 };
    occupant.ch.abilities.con = 0;
    occupant.ch.tmpabilities.con = 0;
    Caster mage { /*mage_prof=*/25, /*cleric_prof=*/0, game_types::PS_None };
    ScopedTickCharExists occupant_exists { occupant.ch, kOccupantAbsNumber };
    ScopedRoomOccupants affected_room { room, kAffectedRoom, { &occupant.ch } };
    ScopedRoomOccupants remote_room { room_by_id_total(kRemoteRoom), kRemoteRoom, { &mage.ch } };

    obj_data* const previous_object_list = object_list;
    ScopedTickExtractCharHook extraction;
    ScopedTickCharacterDiedHook death;

    // Captured WHILE registered, then unregistered: exactly the shape an
    // extracted caster leaves behind. resolve() must answer nobody, and the
    // tick must still run -- from the snapshot -- and still kill.
    caster_snapshot recorded = caster_snapshot::none();
    {
        ScopedTickCharExists mage_exists { mage.ch, kCasterAbsNumber };
        recorded = caster_snapshot::capture(mage.ch);
        ASSERT_EQ(recorded.resolve(), &mage.ch);
    }
    ASSERT_EQ(recorded.resolve(), nullptr) << "the recorded caster is gone";
    ScopedRoomSpellAffect blaze { room, SPELL_BLAZE, 5, 25, recorded };

    queue_mid_rolls(32);
    ASSERT_TRUE(room_affect_tick(SPELL_BLAZE, room, &occupant.ch, blaze.affect()));
    clear_test_random_values();

    release_test_corpse(room, previous_object_list);

    ASSERT_EQ(g_recorded_extraction.calls, 1) << "the tick still kills";
    ASSERT_TRUE(g_recorded_death.called);
    EXPECT_EQ(g_recorded_death.dead_man, &occupant.ch);
    EXPECT_EQ(g_recorded_death.killer, nullptr)
        << "a death from a departed caster's affect names no killer";
    EXPECT_EQ(mage.ch.specials.fighting, nullptr);
}

// ---------------------------------------------------------------------------
// poison: the victim remembers WHO poisoned it, and for how long
// ---------------------------------------------------------------------------
//
// saves_poison() is `number(offence / 3, offence) < number(defense / 2, defense)`
// (true == saved). With the caster at willpower 20 / perception 100 the offence
// is (20 * 8 * 100) / 100 = 160, so the low end of its draw is 53; the occupant's
// defense is GET_CON * 5 + GET_WILLPOWER * 3 = 50, its high end. 53 > 50, so the
// save fails for EVERY queued value and for either evaluation order of the two
// operands (the order of `<`'s operands is unspecified in C++).
TEST(RoomAffectTick, PoisonTickRecordsThePoisonerOnTheVictim)
{
    ScopedTestWorld world { kWorldRoomCount };
    ScopedRoomNumbers room_numbers;
    ScopedTickZoneTable zone_table_owner;
    ScopedTickMobIndex prototype_table;
    ScopedTickGlobalLists global_lists;

    room_data* const room = room_by_id_total(kAffectedRoom);
    Occupant occupant { 500 };
    Caster mystic { /*mage_prof=*/0, /*cleric_prof=*/20, game_types::PS_None };
    ScopedTickCharExists mystic_exists { mystic.ch, kCasterAbsNumber };
    ScopedRoomOccupants affected_room { room, kAffectedRoom, { &occupant.ch } };
    ScopedRoomOccupants remote_room { room_by_id_total(kRemoteRoom), kRemoteRoom, { &mystic.ch } };
    ScopedTickExtractCharHook extraction;
    ScopedTickCharacterDiedHook death;
    ScopedRoomSpellAffect poison { room, SPELL_POISON, 5, 10,
        caster_snapshot::capture(mystic.ch) };
    mystic.wreck(); // cleric prof 1 now; the snapshot still says 20

    queue_mid_rolls(32);
    ASSERT_TRUE(room_affect_tick(SPELL_POISON, room, &occupant.ch, poison.affect()));
    clear_test_random_values();

    affected_type* const applied = affected_by_spell(&occupant.ch, SPELL_POISON);
    ASSERT_NE(applied, nullptr) << "the save must fail and the poison must land";
    const int applied_duration = applied->duration;
    const int poisoner_abs_number = occupant.ch.specials.poisoned_by_abs_number;
    char_data* const poisoner_ptr = occupant.ch.specials.poisoned_by;
    // Read before the teardown below: affect_remove() CLEARS the recorded
    // origin when the poison affect goes away (Task 4).
    char_data* const resolved_poisoner = resolve_poisoner(occupant.ch);
    const int hit_after = occupant.ch.tmpabilities.hit;

    while (occupant.ch.affected)
        affect_remove(&occupant.ch, occupant.ch.affected);
    occupant.ch.specials.fighting = nullptr;

    // cleric prof 20 + wil 25 / 5 = 25; the affect's duration is level + 1.
    EXPECT_EQ(applied_duration, 26) << "duration comes from the SNAPSHOT's cleric level";
    EXPECT_EQ(poisoner_abs_number, kCasterAbsNumber);
    EXPECT_EQ(poisoner_ptr, &mystic.ch);
    EXPECT_EQ(resolved_poisoner, &mystic.ch)
        << "the recorded origin must resolve back to the caster that cast it";
    EXPECT_EQ(hit_after, 495) << "and the tick's own 5 points of damage still land";
    EXPECT_EQ(g_recorded_extraction.calls, 0);
}

TEST(RoomAffectTick, PoisonTickDurationTracksTheRecordedCasterNotTheVictim)
{
    ScopedTestWorld world { kWorldRoomCount };
    ScopedRoomNumbers room_numbers;
    ScopedTickZoneTable zone_table_owner;
    ScopedTickMobIndex prototype_table;
    ScopedTickGlobalLists global_lists;

    room_data* const room = room_by_id_total(kAffectedRoom);
    Occupant occupant { 500 };
    Caster novice { /*mage_prof=*/0, /*cleric_prof=*/5, game_types::PS_None };
    ScopedTickCharExists novice_exists { novice.ch, kSecondCasterAbsNumber };
    ScopedRoomOccupants affected_room { room, kAffectedRoom, { &occupant.ch } };
    ScopedRoomOccupants remote_room { room_by_id_total(kRemoteRoom), kRemoteRoom, { &novice.ch } };
    ScopedTickExtractCharHook extraction;
    ScopedTickCharacterDiedHook death;
    ScopedRoomSpellAffect poison { room, SPELL_POISON, 5, 10,
        caster_snapshot::capture(novice.ch) };

    queue_mid_rolls(32);
    ASSERT_TRUE(room_affect_tick(SPELL_POISON, room, &occupant.ch, poison.affect()));
    clear_test_random_values();

    affected_type* const applied = affected_by_spell(&occupant.ch, SPELL_POISON);
    ASSERT_NE(applied, nullptr);
    const int applied_duration = applied->duration;
    const int poisoner_abs_number = occupant.ch.specials.poisoned_by_abs_number;

    while (occupant.ch.affected)
        affect_remove(&occupant.ch, occupant.ch.affected);
    occupant.ch.specials.fighting = nullptr;

    // cleric prof 5 + wil 25 / 5 = 10; +1. The sibling test's identical
    // occupant, identical rolls and identical room produced 26.
    EXPECT_EQ(applied_duration, 11) << "a different recorded caster gives a different duration";
    EXPECT_NE(applied_duration, 26);
    EXPECT_EQ(poisoner_abs_number, kSecondCasterAbsNumber)
        << "and the victim remembers THAT caster";
}

// ---------------------------------------------------------------------------
// haze: the affect's modifier is the snapshot's mystic level
// ---------------------------------------------------------------------------
//
// saves_mystic() is `number(0, 100) <= GET_PERCEPTION(ch) * 9 / 10` (true ==
// saved). The occupant's perception is 0, so the defense is 0 and the save
// fails for every draw above zero -- kMidRoll gives 50.
TEST(RoomAffectTick, HazeTickUsesTheSnapshotLevelForTheModifier)
{
    ScopedTestWorld world { kWorldRoomCount };
    ScopedRoomNumbers room_numbers;
    ScopedTickZoneTable zone_table_owner;
    ScopedTickMobIndex prototype_table;
    ScopedTickGlobalLists global_lists;

    room_data* const room = room_by_id_total(kAffectedRoom);
    Occupant occupant { 500 };
    ScopedRoomOccupants affected_room { room, kAffectedRoom, { &occupant.ch } };

    // --- an Illusion specialist: level 20 + 25/5 = 25, +6 for the spec ------
    Caster illusionist { /*mage_prof=*/0, /*cleric_prof=*/20, game_types::PS_Illusion };
    ScopedRoomSpellAffect haze { room, SPELL_HAZE, 5, 12,
        caster_snapshot::capture(illusionist.ch) };
    illusionist.wreck();

    queue_mid_rolls(16);
    ASSERT_TRUE(room_affect_tick(SPELL_HAZE, room, &occupant.ch, haze.affect()));
    clear_test_random_values();

    affected_type* const illusion_haze = affected_by_spell(&occupant.ch, SPELL_HAZE);
    ASSERT_NE(illusion_haze, nullptr) << "the mystic save must fail and the haze must land";
    const int illusion_modifier = illusion_haze->modifier;
    while (occupant.ch.affected)
        affect_remove(&occupant.ch, occupant.ch.affected);

    // --- the same cleric level, no Illusion specialization ------------------
    Caster plain { /*mage_prof=*/0, /*cleric_prof=*/20, game_types::PS_None };
    set_room_affect_caster(room, SPELL_HAZE, caster_snapshot::capture(plain.ch));
    plain.wreck();

    queue_mid_rolls(16);
    ASSERT_TRUE(room_affect_tick(SPELL_HAZE, room, &occupant.ch, haze.affect()));
    clear_test_random_values();

    affected_type* const plain_haze = affected_by_spell(&occupant.ch, SPELL_HAZE);
    ASSERT_NE(plain_haze, nullptr);
    const int plain_modifier = plain_haze->modifier;
    while (occupant.ch.affected)
        affect_remove(&occupant.ch, occupant.ch.affected);

    EXPECT_EQ(illusion_modifier, 31) << "cleric 20 + wil 25/5 + the Illusion bonus";
    EXPECT_EQ(plain_modifier, 25) << "the same levels without the Illusion bonus";
    EXPECT_NE(illusion_modifier, plain_modifier)
        << "the snapshot's specialization must reach the haze's modifier";
}

// ---------------------------------------------------------------------------
// mist: renewal and spread scale with the snapshot's mage level
// ---------------------------------------------------------------------------

TEST(RoomAffectTick, MistTickRenewsFromTheSnapshotLevel)
{
    ScopedTestWorld world { kWorldRoomCount };
    ScopedRoomNumbers room_numbers;
    ScopedTickZoneTable zone_table_owner;
    ScopedTickMobIndex prototype_table;
    ScopedTickGlobalLists global_lists;

    room_data* const room = room_by_id_total(kAffectedRoom);
    room_data* const adjacent = room_by_id_total(kAdjacentRoom);
    room_direction_data exit_north {};
    exit_north.to_room = kAdjacentRoom;
    room->dir_option[NORTH] = &exit_north;
    adjacent->room_flags = 0; // no SHADOWY: the seeded mist's modifier must be 0

    Occupant occupant { 500 };
    ScopedRoomOccupants affected_room { room, kAffectedRoom, { &occupant.ch } };

    // --- a level-30 mage: main room renews to 30 / 5, the exit gets 30 / 6 --
    Caster grandmaster { /*mage_prof=*/25, /*cleric_prof=*/0, game_types::PS_None };
    const caster_snapshot grand_snapshot = caster_snapshot::capture(grandmaster.ch);
    ScopedRoomSpellAffect mist { room, SPELL_MIST_OF_BAAZUNGA, /*duration=*/1, /*modifier=*/0,
        grand_snapshot };
    grandmaster.wreck();

    queue_mid_rolls(8);
    ASSERT_TRUE(room_affect_tick(SPELL_MIST_OF_BAAZUNGA, room, &occupant.ch, mist.affect()));
    clear_test_random_values();

    const int grand_here = mist.affect().duration;
    affected_type* const seeded = room_affected_by_spell(adjacent, SPELL_MIST_OF_BAAZUNGA);
    ASSERT_NE(seeded, nullptr) << "the tick must spread the mist through the exit";
    const int grand_there = seeded->duration;
    const int grand_there_modifier = seeded->modifier;
    const caster_snapshot* const spread_caster = room_affect_caster(adjacent, SPELL_MIST_OF_BAAZUNGA);
    ASSERT_NE(spread_caster, nullptr);
    const int spread_abs_number = spread_caster->abs_number;

    // --- reset both rooms and repeat with a level-10 mage -------------------
    clear_room_affects(adjacent);
    mist.affect().duration = 0;
    Caster apprentice { /*mage_prof=*/5, /*cleric_prof=*/0, game_types::PS_None };
    apprentice.ch.abs_number = kSecondCasterAbsNumber;
    set_room_affect_caster(room, SPELL_MIST_OF_BAAZUNGA, caster_snapshot::capture(apprentice.ch));
    apprentice.wreck();

    queue_mid_rolls(8);
    ASSERT_TRUE(room_affect_tick(SPELL_MIST_OF_BAAZUNGA, room, &occupant.ch, mist.affect()));
    clear_test_random_values();

    const int apprentice_here = mist.affect().duration;
    affected_type* const seeded_again = room_affected_by_spell(adjacent, SPELL_MIST_OF_BAAZUNGA);
    ASSERT_NE(seeded_again, nullptr);
    const int apprentice_there = seeded_again->duration;

    clear_room_affects(adjacent);
    room->dir_option[NORTH] = nullptr;

    EXPECT_EQ(grand_here, 6) << "level 30 / 5";
    EXPECT_EQ(grand_there, 5) << "level 30 / 6";
    EXPECT_EQ(grand_there_modifier, 0) << "the exit room is not SHADOWY";
    EXPECT_EQ(spread_abs_number, grand_snapshot.abs_number)
        << "the spread mist carries the SAME recorded caster, not caster_snapshot::none()";
    EXPECT_EQ(apprentice_here, 2) << "level 10 / 5";
    EXPECT_EQ(apprentice_there, 1) << "level 10 / 6";
    EXPECT_NE(grand_here, apprentice_here);
    EXPECT_NE(grand_there, apprentice_there);
}

// The renewal is a MAXIMUM, never a downgrade -- the quirk the original
// spell_mist_of_baazunga renewal arm has always had, preserved verbatim.
TEST(RoomAffectTick, MistTickNeverShortensAStrongerMist)
{
    ScopedTestWorld world { kWorldRoomCount };
    ScopedRoomNumbers room_numbers;
    ScopedTickZoneTable zone_table_owner;
    ScopedTickMobIndex prototype_table;
    ScopedTickGlobalLists global_lists;

    room_data* const room = room_by_id_total(kAffectedRoom);
    Occupant occupant { 500 };
    ScopedRoomOccupants affected_room { room, kAffectedRoom, { &occupant.ch } };
    Caster apprentice { /*mage_prof=*/5, /*cleric_prof=*/0, game_types::PS_None };
    ScopedRoomSpellAffect mist { room, SPELL_MIST_OF_BAAZUNGA, /*duration=*/40, /*modifier=*/0,
        caster_snapshot::capture(apprentice.ch) };

    queue_mid_rolls(8);
    ASSERT_TRUE(room_affect_tick(SPELL_MIST_OF_BAAZUNGA, room, &occupant.ch, mist.affect()));
    clear_test_random_values();

    EXPECT_EQ(mist.affect().duration, 40) << "level 10 / 5 = 2 must not overwrite 40";
}

// ---------------------------------------------------------------------------
// no tick body: the caller's fallback arm still owns the spell
// ---------------------------------------------------------------------------

TEST(RoomAffectTick, UnknownSpellHasNoTickBody)
{
    ScopedTestWorld world { kWorldRoomCount };
    ScopedRoomNumbers room_numbers;
    ScopedTickZoneTable zone_table_owner;
    ScopedTickMobIndex prototype_table;
    ScopedTickGlobalLists global_lists;

    room_data* const room = room_by_id_total(kAffectedRoom);
    Occupant occupant { 500 };
    ScopedRoomOccupants affected_room { room, kAffectedRoom, { &occupant.ch } };
    Caster mage { /*mage_prof=*/25, /*cleric_prof=*/20, game_types::PS_None };
    ScopedRoomSpellAffect sanctuary { room, SPELL_SANCTUARY, 5, 12,
        caster_snapshot::capture(mage.ch) };

    queue_mid_rolls(8);
    EXPECT_FALSE(room_affect_tick(SPELL_SANCTUARY, room, &occupant.ch, sanctuary.affect()))
        << "a spell with no tick body must report so, and leave everything alone";
    clear_test_random_values();

    EXPECT_EQ(occupant.ch.tmpabilities.hit, 500);
    EXPECT_EQ(occupant.ch.affected, nullptr);
    EXPECT_EQ(occupant.ch.specials.poisoned_by_abs_number, -1);
    EXPECT_EQ(sanctuary.affect().duration, 5);
}

// ---------------------------------------------------------------------------
// affect_update_room(): a mist that MOVES carries its recorded caster along
// ---------------------------------------------------------------------------
//
// affect_remove_room() erases the (room, spell) caster record, so the move arm
// has to read the snapshot BEFORE it removes the source affect. If it does not,
// the destination is left with caster_snapshot::none() and the next tick there
// falls back to the occupant's own stats -- the exact behavior this task exists
// to retire. The room is left EMPTY so the occupant loop's own tick (which
// would seed the destination with a mist of its own and cancel the move) never
// runs.
TEST(RoomAffectTick, AffectUpdateRoomCarriesTheCasterWhenTheMistMoves)
{
    ScopedTestWorld world { kWorldRoomCount };
    ScopedRoomNumbers room_numbers;
    ScopedTickZoneTable zone_table_owner;
    ScopedTickMobIndex prototype_table;
    ScopedTickGlobalLists global_lists;

    room_data* const room = room_by_id_total(kAffectedRoom);
    room_data* const adjacent = room_by_id_total(kAdjacentRoom);
    room_direction_data exit_north {};
    exit_north.to_room = kAdjacentRoom;
    room->dir_option[NORTH] = &exit_north;
    ScopedRoomOccupants affected_room { room, kAffectedRoom, {} };

    Caster mage { /*mage_prof=*/25, /*cleric_prof=*/0, game_types::PS_None };
    ScopedTickCharExists mage_exists { mage.ch, kCasterAbsNumber };
    const caster_snapshot recorded = caster_snapshot::capture(mage.ch);
    ScopedRoomSpellAffect mist { room, SPELL_MIST_OF_BAAZUNGA, /*duration=*/2, /*modifier=*/0,
        recorded };

    // Every draw at 0.0: movechance = number(1, 100) = 1 (< 75, the move fires)
    // and direction = number(0, NUM_OF_DIRS - 1) = 0 == NORTH.
    for (int i = 0; i < 16; ++i)
        push_test_random_value(0.0);
    testing::internal::CaptureStderr();
    affect_update_room(room);
    const std::string captured = testing::internal::GetCapturedStderr();
    clear_test_random_values();

    const bool source_kept_the_mist = room_affected_by_spell(room, SPELL_MIST_OF_BAAZUNGA) != nullptr;
    affected_type* const moved = room_affected_by_spell(adjacent, SPELL_MIST_OF_BAAZUNGA);
    const caster_snapshot* const moved_caster
        = moved ? room_affect_caster(adjacent, SPELL_MIST_OF_BAAZUNGA) : nullptr;
    const int moved_abs_number = moved_caster ? moved_caster->abs_number : -999;
    const bool moved_is_none = moved_caster ? moved_caster->is_none() : true;

    clear_room_affects(adjacent);
    room->dir_option[NORTH] = nullptr;

    ASSERT_NE(moved, nullptr) << "the mist must have drifted north; stderr was: " << captured;
    EXPECT_FALSE(source_kept_the_mist) << "and left its old room";
    EXPECT_FALSE(moved_is_none) << "the moved mist must not lose its caster";
    EXPECT_EQ(moved_abs_number, recorded.abs_number)
        << "the destination must carry the SAME recorded caster the source had";
}

// ---------------------------------------------------------------------------
// affect_update_room(): the occupant tick really does route through the tick
// ---------------------------------------------------------------------------
//
// The wiring itself, not just the tick body: affect_update_room() must call
// room_affect_tick() and only fall back to the historical
// `(skills[loc].spell_pointer)(tmpch, "", SPELL_TYPE_SPELL, tmpch, ...)`
// re-cast when the tick reports no body. blaze's REAL body is installed in
// skills[] here, so the fallback arm is the genuine pre-TASK-021 behavior and
// the two arms are told apart by the damage they deal, not by a crash.
//
// Draws, in order: number(1, 100) (the mist movechance, read at function entry)
// and number(0, 12) (the 1-in-13 "nothing happens" roll) both answer 0.0, so
// the roll comes up 0 and the tick fires. Everything after that is kMidRoll:
//   tick arm     -- level 30, dc 22, roll 11 -> not saved, dam 29, x1.2 -> 34
//   fallback arm -- the occupant re-casts on itself: level 2, dc 10, roll 11 ->
//                   SAVED, dam (number(2, 8) = 5) + 10 = 15, halved to 7, and no
//                   spell penetration at all (an NPC caster) -> 7
TEST(RoomAffectTick, AffectUpdateRoomTicksBlazeFromTheRecordedCasterNotTheOccupant)
{
    ScopedTestWorld world { kWorldRoomCount };
    ScopedRoomNumbers room_numbers;
    ScopedTickZoneTable zone_table_owner;
    ScopedTickMobIndex prototype_table;
    ScopedTickGlobalLists global_lists;
    ScopedBlazeSpellPointer blaze_cell;

    room_data* const room = room_by_id_total(kAffectedRoom);
    Occupant occupant { 500 };
    ScopedRoomOccupants affected_room { room, kAffectedRoom, { &occupant.ch } };
    Caster mage { /*mage_prof=*/25, /*cleric_prof=*/0, game_types::PS_None };
    ScopedRoomSpellAffect blaze { room, SPELL_BLAZE, /*duration=*/5, /*modifier=*/25,
        caster_snapshot::capture(mage.ch) };
    mage.wreck();

    push_test_random_value(0.0); // movechance
    push_test_random_value(0.0); // the 1-in-13 roll: 0 means "do something"
    queue_mid_rolls(32);
    affect_update_room(room);
    clear_test_random_values();

    const int hit_after = occupant.ch.tmpabilities.hit;
    occupant.ch.specials.fighting = nullptr;

    EXPECT_EQ(hit_after, 466)
        << "affect_update_room() must tick through room_affect_tick() (500 - 34), not re-cast "
           "the spell with the occupant as its own caster (which would leave 493)";
    EXPECT_NE(hit_after, 493) << "493 is the pre-TASK-021 self-re-cast result";
    EXPECT_NE(hit_after, 500) << "and the tick must have fired at all";
}
