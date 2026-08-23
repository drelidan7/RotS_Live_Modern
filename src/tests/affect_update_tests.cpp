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
extern universal_list* affected_list_pool;
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
// way register_npc_char() would for a real mob -- through the TWO-argument
// set_char_exists(), which also records the pointer char_by_abs_number()
// hands back. affect_update() updates a snapshotted entry only while that
// lookup still returns the very pointer the entry named (the recycled-slot
// guard), so the one-argument overload -- which leaves the pointer slot null
// -- cannot stand in for a real registration here.
class ScopedCharExists {
public:
    explicit ScopedCharExists(char_data& ch, int abs_number)
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

// The abs_number slot both recycled-slot tests below hand from one character
// to another. This suite owns 7901/7902; 7903 is caster_snapshot_tests', so
// this one takes 7904 to keep the monolithic runner's band map honest.
constexpr int kRecycledSlot = 7904;

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


// ---------------------------------------------------------------------------
// The recycled abs_number slot (final review, M-2)
// ---------------------------------------------------------------------------
//
// affect_update() snapshots (abs_number, char_data*) pairs BEFORE any body
// runs, then revisits them. char_exists() alone cannot validate such a pair:
// it is one bit, and register_npc_char() hands a freed slot straight to the
// next character its cursor reaches -- an ON_DIE / SPECIAL_DEATH script that
// read_mobile()s inside this very tick is enough. The walk therefore resolves
// the number back to a live pointer (char_by_abs_number()) and requires it to
// be the SAME pointer the snapshot named, the identity compare
// caster_snapshot::resolve()/resolve_poisoner() already use.
//
// The two tests below pin the two halves of that guard: the slot's NEW owner
// must not make the OLD entry look live (red in the plain build), and the old
// entry's pointer must never be dereferenced once it has been freed (an ASan
// witness -- the plain build reads the recycled bytes without complaint).

TEST(AffectUpdateWalk, DoesNotUpdateACharacterWhoseAbsNumberSlotWasRecycled)
{
    ScopedTestWorld world(16);

    char_data victim {};
    char_prof_data victim_profs {};
    make_npc(victim, victim_profs, 500);
    victim.abs_number = kRecycledSlot;
    set_char_exists(kRecycledSlot, &victim);

    affected_type victim_af = inert_affect(50);
    affect_to_char(&victim, &victim_af);
    ASSERT_TRUE(affected_list_holds(&victim)) << "the victim must be on the walk";
    ASSERT_NE(victim.affected, nullptr);

    // The victim was extracted, and register_npc_char()'s cursor handed its
    // number to a brand-new mob. The bit is set again -- for somebody else.
    char_data imposter {};
    char_prof_data imposter_profs {};
    make_npc(imposter, imposter_profs, 500);
    ScopedCharExists imposter_exists { imposter, kRecycledSlot };
    ASSERT_EQ(char_by_abs_number(kRecycledSlot), &imposter);
    ASSERT_NE(char_exists(kRecycledSlot), 0) << "the slot's bit is set -- for the new owner";

    testing::internal::CaptureStderr();
    affect_update();
    const std::string captured = testing::internal::GetCapturedStderr();

    const int duration_after = victim.affected ? victim.affected->duration : -1;
    const bool victim_node_gone = !affected_list_holds(&victim);
    while (victim.affected)
        affect_remove(&victim, victim.affected);

    EXPECT_EQ(duration_after, 50)
        << "the old owner of a recycled slot must not be updated (char_exists() alone would "
           "have ticked it down to 49); stderr was: "
        << captured;
    EXPECT_TRUE(victim_node_gone) << "and its entry is retired as stale instead";
    EXPECT_NE(captured.find("Getting Unknown char off the affected_list."), std::string::npos)
        << "the housekeeping arm must not name the stale pointer either; stderr was: " << captured;
    EXPECT_EQ(imposter.affected, nullptr) << "and the new owner gains nothing from the old entry";
}

TEST(AffectUpdateWalk, DoesNotDereferenceAFreedCharacterThroughARecycledSlot)
{
    ScopedTestWorld world(16);

    // A character that has already been extracted and freed, with its
    // affected_list node still in place -- the node is fabricated directly
    // (rather than through affect_to_char()) so that freeing the character
    // leaks no pooled affected_type behind it.
    char_data* const freed = new char_data {};
    freed->abs_number = kRecycledSlot;
    universal_list* const stale_node = pool_to_list(&affected_list, &affected_list_pool);
    stale_node->type = TARGET_CHAR;
    stale_node->number = kRecycledSlot;
    stale_node->ptr.ch = freed;
    const void* const freed_address = freed;
    delete freed;

    // ...and the brand-new mob that register_npc_char() handed the same slot.
    char_data imposter {};
    char_prof_data imposter_profs {};
    make_npc(imposter, imposter_profs, 500);
    ScopedCharExists imposter_exists { imposter, kRecycledSlot };

    testing::internal::CaptureStderr();
    affect_update(); // reads `freed->affected` if the guard is only char_exists()
    const std::string captured = testing::internal::GetCapturedStderr();

    EXPECT_FALSE(affected_list_holds(freed_address))
        << "the freed character's entry must be retired, not walked; stderr was: " << captured;
    EXPECT_NE(captured.find("Getting Unknown char off the affected_list."), std::string::npos)
        << "and reported without naming it; stderr was: " << captured;
}
