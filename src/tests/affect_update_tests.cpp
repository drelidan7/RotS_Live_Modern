// TASK-020 -- affect_update()'s walk over the process-global affected_list
// must survive a death that the walk itself triggers.
//
// affect_update() (limits.cpp) walks affected_list with a pre-saved
// `tmplist2 = tmplist->next`. affect_update_room() re-casts a room's blaze on
// each occupant with caster == victim == the occupant; a lethal tick runs
// damage() -> die() -> raw_kill(), which strips every affect of the dead
// character, and affect_remove()'s tail then removes the character's own
// affected_list node through from_list_to_pool() -- which free()s it. When
// that node is the one the walk already saved as next, the next iteration
// dereferences freed memory. pool_to_list() inserts at the head, so the node
// right behind a freshly-cast blaze room's is whoever acquired their first
// affect just before the cast -- exactly the people standing in the blaze.
//
// The fixture reproduces that order literally: [blaze room, dying occupant,
// sentinel]. The extract_char seam is stubbed to the real unlink minus the
// free (the TASK-018 shape) so the stack occupant survives its own death; the
// affected_list node is freed by raw_kill()'s affect strip regardless, which
// is the defect. Against the unfixed walk this test is a use-after-free
// (ASan reports it; the plain build reads garbage). Against the fixed walk it
// asserts the walk completed, the occupant died exactly once, the sentinel's
// node is intact, and nothing resolved the dead occupant.
#include "../db.h"
#include "../entity_hooks.h"
#include "../handler.h"
#include "../spells.h"
#include "../utils.h"
#include "rots/core/character.h"
#include "rots/core/object.h"
#include "test_placement.h"
#include "test_random_utils.h"
#include "test_world.h"
#include <cstdlib>
#include <gtest/gtest.h>
#include <string>

extern obj_data* object_list;
extern struct index_data* mob_index;
extern universal_list* affected_list;
extern struct skill_data skills[];
extern char_data* combat_list;
extern char_data* combat_next_dude;
void affect_update();
void affect_update_room(struct room_data* room);
ASPELL(spell_blaze);

namespace {

struct RecordedExtraction {
    char_data* ch = nullptr;
    int calls = 0;
};
RecordedExtraction g_recorded_extraction;

void unlinking_extract_char_stub(char_data* ch, int /*new_room*/)
{
    g_recorded_extraction.ch = ch;
    ++g_recorded_extraction.calls;
    char_from_room(ch); // the real NPC arm's unlink, minus the free_char()
    remove_char_exists(ch->abs_number); // the real arm unregisters the character too
}

class ScopedUnlinkingExtractCharHook {
public:
    ScopedUnlinkingExtractCharHook()
    {
        g_recorded_extraction = RecordedExtraction {};
        rots::entity::set_extract_char_hook(unlinking_extract_char_stub);
    }
    ~ScopedUnlinkingExtractCharHook() { register_extract_char_hook(); }
    ScopedUnlinkingExtractCharHook(const ScopedUnlinkingExtractCharHook&) = delete;
    ScopedUnlinkingExtractCharHook& operator=(const ScopedUnlinkingExtractCharHook&) = delete;
};

// The death pipeline reads the NPC's prototype (activate_char_special's
// SPECIAL_DEATH probe, make_physical_corpse's corpse owner id); publish a
// one-entry table for the scope.
class ScopedMobIndex {
public:
    ScopedMobIndex()
        : m_previous(mob_index)
    {
        m_entry = index_data {};
        m_entry.virt = 1;
        mob_index = &m_entry;
    }
    ~ScopedMobIndex() { mob_index = m_previous; }
    ScopedMobIndex(const ScopedMobIndex&) = delete;
    ScopedMobIndex& operator=(const ScopedMobIndex&) = delete;

private:
    index_data* m_previous; // whatever table the suite found installed (normally null)
    index_data m_entry {}; // the single prototype slot nr = 0 names
};

// gtest_main does not run assign_spell_pointers(); install blaze's real body
// in its skills[] cell for the scope so affect_update_room() can re-cast it.
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

void release_corpse(room_data* room, obj_data* previous_object_list)
{
    obj_data* corpse = room->contents;
    if (corpse == nullptr)
        return;
    obj_from_room(corpse);
    if (object_list == corpse)
        object_list = corpse->next;
    RELEASE(corpse->name);
    RELEASE(corpse->short_description);
    RELEASE(corpse->description);
    std::free(corpse);
    object_list = previous_object_list;
}

bool affected_list_holds(const void* ptr)
{
    for (universal_list* node = affected_list; node; node = node->next)
        if (node->ptr.ch == ptr || node->ptr.room == ptr)
            return true;
    return false;
}

// Registers a unique abs_number in the char_exists table for the scope, the
// way register_npc_char() would for a real mob; affect_update() keeps a
// character's affected_list node only while char_exists() says so.
class ScopedCharExists {
public:
    explicit ScopedCharExists(char_data& ch, int abs_number)
        : m_ch(ch)
    {
        ch.abs_number = abs_number;
        set_char_exists(abs_number);
    }
    ~ScopedCharExists() { remove_char_exists(m_ch.abs_number); }
    ScopedCharExists(const ScopedCharExists&) = delete;
    ScopedCharExists& operator=(const ScopedCharExists&) = delete;

private:
    char_data& m_ch; // the character whose registration this scope owns
};

void make_npc(char_data& ch, char_prof_data& profs, int hit)
{
    ch.profs = &profs;
    ch.specials2.act = MOB_ISNPC;
    ch.nr = 0;
    ch.player.race = RACE_HUMAN;
    ch.player.level = 10;
    ch.tmpabilities.intel = 20;
    // affect_to_char() -> affect_total() rebuilds tmpabilities from abilities,
    // so the base must carry the intended hit points too.
    ch.abilities.hit = hit;
    ch.tmpabilities.hit = hit;
    ch.specials.position = POSITION_STANDING;
}

affected_type inert_affect(int duration)
{
    affected_type af {};
    af.type = SPELL_INFRAVISION; // inert for the damage path; only its affected_list node matters
    af.duration = duration;
    af.modifier = 0;
    af.location = 0;
    af.bitvector = 0;
    return af;
}

constexpr int kBlazeRoom = 7;
constexpr int kQuietRoom = 8;

} // namespace

TEST(AffectUpdateWalk, SurvivesAnOccupantDyingToTheBlazeTickItIsProcessing) {
    ScopedTestWorld world(16);
    ScopedMobIndex prototype_table;
    ScopedZoneTableOwner zone_table_owner;
    ScopedBlazeSpellPointer blaze_cell;
    room_data* blaze_room = room_by_id_total(kBlazeRoom);
    room_data* quiet_room = room_by_id_total(kQuietRoom);

    char_data sentinel {}, occupant {};
    char_prof_data sentinel_profs {}, occupant_profs {};
    make_npc(sentinel, sentinel_profs, 500);
    make_npc(occupant, occupant_profs, 1); // any blaze tick is lethal
    ScopedCharExists sentinel_exists { sentinel, 7901 }; // char_exists is a char[8001] table
    ScopedCharExists occupant_exists { occupant, 7902 };
    ScopedRoomOccupants quiet_occupants { quiet_room, kQuietRoom, { &sentinel } };
    ScopedRoomOccupants blaze_occupants { blaze_room, kBlazeRoom, { &occupant } };
    obj_data* const previous_object_list = object_list;
    ScopedUnlinkingExtractCharHook extraction;

    // Creation order decides affected_list order (head insertion):
    // sentinel first (tail), occupant second, the room last (head).
    affected_type sentinel_af = inert_affect(50);
    affect_to_char(&sentinel, &sentinel_af);
    affected_type occupant_af = inert_affect(50);
    affect_to_char(&occupant, &occupant_af);
    affected_type blaze {};
    blaze.type = ROOMAFF_SPELL;
    blaze.duration = 5;
    blaze.modifier = 20;
    blaze.location = SPELL_BLAZE;
    blaze.bitvector = 0;
    affect_to_room(blaze_room, &blaze);
    ASSERT_EQ(affected_list->ptr.room, blaze_room) << "the blaze room must head the walk";
    ASSERT_EQ(affected_list->type, TARGET_ROOM);
    ASSERT_EQ(affected_list->next->type, TARGET_CHAR);
    ASSERT_EQ(affected_list->next->ptr.ch, &occupant) << "the dying occupant must be the saved next node";
    ASSERT_TRUE(affected_list_holds(&sentinel));

    // Every number() draw answers 0.0: the 1-in-13 re-cast roll fires, and
    // the blaze's own rolls are deterministic.
    for (int i = 0; i < 80; ++i)
        push_test_random_value(0.0);
    testing::internal::CaptureStderr();
    affect_update();
    const std::string captured = testing::internal::GetCapturedStderr();
    clear_test_random_values();

    // Teardown of process globals before assertions.
    release_corpse(blaze_room, previous_object_list);
    if (affected_type* left = room_affected_by_spell(blaze_room, SPELL_BLAZE))
        affect_remove_room(blaze_room, left);
    const bool sentinel_node_intact = affected_list_holds(&sentinel);
    const bool occupant_node_gone = !affected_list_holds(&occupant);
    while (sentinel.affected)
        affect_remove(&sentinel, sentinel.affected);
    while (occupant.affected)
        affect_remove(&occupant, occupant.affected);
    combat_list = nullptr;
    combat_next_dude = nullptr;

    ASSERT_EQ(g_recorded_extraction.calls, 1)
        << "the fixture must kill the occupant through the blaze tick; occupant hit=" << occupant.tmpabilities.hit
        << " loc=" << location_of(&occupant) << " sentinel_intact=" << sentinel_node_intact
        << " occupant_gone=" << occupant_node_gone << " blaze_left=" << (room_affected_by_spell(blaze_room, SPELL_BLAZE) != nullptr)
        << " stderr was: " << captured;
    EXPECT_EQ(g_recorded_extraction.ch, &occupant);
    EXPECT_EQ(location_of(&occupant), NOWHERE);
    EXPECT_TRUE(occupant_node_gone) << "raw_kill's affect strip frees the dead occupant's node";
    EXPECT_TRUE(sentinel_node_intact) << "the walk must reach and keep the node behind the freed one";
    EXPECT_EQ(captured.find("world[] called for negative room number."), std::string::npos)
        << "nothing may resolve the dead occupant; stderr was: " << captured;
}

