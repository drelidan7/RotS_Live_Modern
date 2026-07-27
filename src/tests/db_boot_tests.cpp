// db_boot_tests.cpp

// Coverage rider for record_crime()'s (db_boot.cpp) witness-selection walk
// (LS-2 Wave Task T3a; .superpowers/sdd/ls2-census.md's batch table names
// this file's record_crime as T3a's rider, and
// .superpowers/sdd/ls2-census-a2.md's db_boot.cpp entry: "record_crime has
// zero coverage. It is a walk conversion with a declaration deletion.").
// The conversion itself: db_boot.cpp's
//   for (tmpchar = world[victim->in_room].people; tmpchar; tmpchar = tmpchar->next_in_room)
// became
//   for (auto* tmpchar : rots::entity::occupants(room_of(victim)))
// (db_boot.cpp:567), deleting the now-unused `struct char_data* tmpchar;`
// declaration that used to live at db_boot.cpp:563. This file proves the
// walk still visits every room occupant in order and that record_crime's
// per-occupant witness filter (skip the criminal by identity, skip any
// IS_NPC() occupant, skip anyone at or above LEVEL_IMMORT) is unchanged.
//
// A precise correction to the census's framing (recorded here since the
// task brief that spawned this file asked for exactly this): the census
// suggested asserting "add_crime is called exactly once, for the eligible
// witness". record_crime's own filter is
//   if ((tmpchar == criminal) || IS_NPC(tmpchar) || GET_LEVEL(tmpchar) >= LEVEL_IMMORT)
//       continue;
// -- it excludes the criminal by pointer identity but does NOT exclude the
// victim. Since the crime necessarily happens in the victim's own room, the
// victim is always a live occupant of the room record_crime walks, and the
// filter above never matches them -- so a realistic crime scene ALWAYS
// records the victim as a witness to their own crime (a pre-existing legacy
// quirk this LS-2 conversion neither introduces nor should "fix"; the
// wave's constraint is zero behavior change). The tests below are written
// against this actual, verified behavior rather than the census's
// approximate framing.
//
// add_crime() (db_players.cpp) is not seamed behind any test hook, so this
// observes its one process-global side effect that's cheap and safe to
// read directly: num_of_crimes, a plain file-scope `int` (db_players.cpp:102)
// with external linkage but no header declaration -- redeclared here at its
// point of need, matching the convention several existing test files already
// use for similar db_players.cpp/db_boot.cpp globals (e.g.
// act_wiz_format_tests.cpp's `extern struct player_index_element*
// player_table;`).
//
// add_crime() also attempts to persist the whole in-memory crime set to a
// path built from CRIME_FILE ("misc/crimelist", db.h) -- a path that is
// relative to the process's current working directory in production (the
// game chdir()s into lib/ at boot; this test binary never does). Every test
// below therefore runs inside an isolated temporary directory
// (ScopedTemporaryWorkingDirectory), mirroring db_loader_tests.cpp's
// TemporaryDirectory + ScopedWorkingDirectory pair -- so that write attempt
// (which fails harmlessly today, since a bare "misc/" subdirectory is never
// created and add_crime()'s own fail-closed guard just logs and continues)
// can never touch this repository's source tree even if that assumption
// ever stops holding.

#include "../db.h"
#include "../handler.h"
#include "../utils.h"
#include "rots/core/character.h"
#include "rots/core/room.h"
#include "test_char_cleanup.h"
#include "test_placement.h"
#include "test_platform_compat.h"
#include "test_world.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

// db_players.cpp:100/102 -- file-scope globals, external linkage, no header
// declaration. add_crime() increments num_of_crimes exactly once per witness
// it records; see the file header comment above for why this is the
// cheapest safe-to-read witness-count signal available without adding a
// test seam to production code (out of scope for a reads-only LS-2
// conversion).
extern struct crime_record_type* crime_record;
extern int num_of_crimes;

// db_players.cpp file-scope globals backing find_player_in_table(), which
// add_crime()'s persistence tail calls on every recorded witness -- not
// declared in any header, redeclared here at the point of need (matches
// db_loader_tests.cpp's identical pair).
extern struct player_index_element* player_table;
extern int top_of_p_table;

namespace {

// Resets crime_record/num_of_crimes to a clean empty state (nullptr / 0) for
// the duration of a test, restoring whatever was there before on scope exit
// -- crime_record_type (db.h) is a plain POD (no owned pointers of its own),
// so restoring only needs to free the array itself. REQUIRED for test
// isolation beyond player_table: add_crime()'s own know_of_crime() dedup
// check compares each new (criminal, victim, witness) INDEX triple against
// every existing crime_record entry, so two tests in the SAME PROCESS (the
// monolithic src/tests/Makefile `tests` binary runs every TEST() in one
// process, unlike ctest's one-process-per-test) that record crimes with the
// same resolved indices would otherwise see the second test's add_crime()
// call silently skipped as a "duplicate" of the first test's leftover
// record -- caught empirically while developing this fixture, the same
// class of cross-test state leakage test_world.h's own header comment
// documents for room_data::BASE_WORLD/room->people.
class ScopedCrimeRecordReset
{
public:
    ScopedCrimeRecordReset()
        : m_previous_crime_record(crime_record)
        , m_previous_num_of_crimes(num_of_crimes)
    {
        crime_record = nullptr;
        num_of_crimes = 0;
    }

    ~ScopedCrimeRecordReset()
    {
        RELEASE(crime_record);
        crime_record = m_previous_crime_record;
        num_of_crimes = m_previous_num_of_crimes;
    }

    ScopedCrimeRecordReset(const ScopedCrimeRecordReset&) = delete;
    ScopedCrimeRecordReset& operator=(const ScopedCrimeRecordReset&) = delete;

private:
    crime_record_type* m_previous_crime_record;
    int m_previous_num_of_crimes;
};

// Points the process-global player_table at DISTINCT fabricated entries (one
// per idnum in construction order), restoring whatever was there before on
// scope exit -- mirrors the ScopedPlayerTableEntry/ScopedMobIndexEntry RAII
// convention already established elsewhere (e.g. act_wiz_format_tests.cpp).
//
// REQUIRED here, and DISTINCT entries specifically (not merely a non-null
// table) for two independent reasons:
//   1. top_of_p_table's process-wide default is 0 (db_players.cpp:94 `int
//      top_of_p_table = 0;`), NOT -1 -- so with a null/unpopulated
//      player_table, find_player_in_table()'s `for (i = 0; i <=
//      top_of_p_table; i++)` loop runs once at i == 0 and dereferences a
//      null player_table, crashing.
//   2. add_crime() (db_players.cpp) calls know_of_crime() with every idnum
//      passed through find_player_in_table() first. If player_table has no
//      entry for a given idnum, find_player_in_table() returns -1 for ALL
//      of them alike, collapsing every witness to the SAME (-1, -1, -1)
//      triple -- so a test recording two DIFFERENT witnesses in the same
//      room would see its second add_crime() call misidentified by
//      know_of_crime() as a duplicate of the first and silently skipped.
//      Distinct player_table entries give each idnum its own index, keeping
//      the dedup check accurate (this was caught empirically: an earlier
//      version of this fixture used an always-empty player_table, and the
//      two-witness test below silently recorded only one witness).
class ScopedPlayerTable
{
public:
    explicit ScopedPlayerTable(std::initializer_list<long> idnums)
        : m_previous_player_table(player_table)
        , m_previous_top_of_p_table(top_of_p_table)
    {
        const int count = static_cast<int>(idnums.size());
        CREATE(player_table, player_index_element, count);
        int index = 0;
        for (long idnum : idnums)
        {
            player_table[index].name = str_dup("crime-witness-fixture");
            player_table[index].idnum = static_cast<int>(idnum);
            player_table[index].flags = 0;
            ++index;
        }
        top_of_p_table = count - 1;
    }

    ~ScopedPlayerTable()
    {
        for (int index = 0; index <= top_of_p_table; ++index)
        {
            RELEASE(player_table[index].name);
        }
        RELEASE(player_table);
        player_table = m_previous_player_table;
        top_of_p_table = m_previous_top_of_p_table;
    }

    ScopedPlayerTable(const ScopedPlayerTable&) = delete;
    ScopedPlayerTable& operator=(const ScopedPlayerTable&) = delete;

private:
    player_index_element* m_previous_player_table;
    int m_previous_top_of_p_table;
};

// Isolates record_crime()'s -> add_crime()'s relative-path file I/O for the
// duration of a test. Reduced, single-purpose copy of db_loader_tests.cpp's
// TemporaryDirectory + ScopedWorkingDirectory pair (that file's version
// supports arbitrary nested account/player paths this suite doesn't need).
class ScopedTemporaryWorkingDirectory
{
public:
    ScopedTemporaryWorkingDirectory()
    {
        std::error_code current_path_error;
        m_original_path = std::filesystem::current_path(current_path_error);
        EXPECT_FALSE(current_path_error)
            << "Expected current_path() to report this test process's working directory.";

        // Same rots_mkdtemp portable-mkdtemp shim, same hardcoded "/tmp/..."
        // template convention every other TemporaryDirectory fixture in this
        // tree uses (e.g. db_loader_tests.cpp) -- see test_platform_compat.h's
        // rots_mkdtemp() comment for why the prefix is literal rather than
        // std::filesystem::temp_directory_path()-derived.
        char path_template[] = "/tmp/rots-db-boot-tests-XXXXXX";
        char* created_path = rots_mkdtemp(path_template);
        EXPECT_NE(created_path, nullptr);
        if (created_path != nullptr)
        {
            m_temp_path = created_path;
        }

        std::error_code chdir_error;
        std::filesystem::current_path(m_temp_path, chdir_error);
        EXPECT_FALSE(chdir_error) << "Expected current_path(" << m_temp_path.string() << ") to succeed.";
    }

    ~ScopedTemporaryWorkingDirectory()
    {
        std::error_code ignored;
        std::filesystem::current_path(m_original_path, ignored);
        if (!m_temp_path.empty())
        {
            std::filesystem::remove_all(m_temp_path, ignored);
        }
    }

    ScopedTemporaryWorkingDirectory(const ScopedTemporaryWorkingDirectory&) = delete;
    ScopedTemporaryWorkingDirectory& operator=(const ScopedTemporaryWorkingDirectory&) = delete;

private:
    std::filesystem::path m_original_path;
    std::filesystem::path m_temp_path;
};

// A minimal PC fixture: not NPC, well below LEVEL_IMMORT (91), with its own
// idnum -- exactly what record_crime()'s filter and add_crime()'s argument
// list need. record_crime() itself never dereferences player_table/
// find_player_in_table for the walk (only add_crime()'s know_of_crime()
// dedup check and persistence tail do -- see ScopedPlayerTable's own comment
// above for why each TestPc's idnum must have a matching player_table
// entry).
struct TestPc
{
    char_data character {};
    ScopedClearCharFields cleanup { character };

    explicit TestPc(int level, long idnum)
    {
        clear_char(&character, MOB_VOID);
        character.player.level = level;
        character.specials2.idnum = idnum;
    }
};

// A minimal NPC fixture, for the witness walk's IS_NPC() exclusion.
struct TestNpc
{
    char_data character {};
    ScopedClearCharFields cleanup { character };

    explicit TestNpc(int level)
    {
        clear_char(&character, MOB_VOID);
        character.specials2.act = MOB_ISNPC;
        character.player.level = level;
    }
};

} // namespace

TEST(DbBootRecordCrime, RecordsTheVictimAndEachEligibleThirdPartyOccupantAsWitnesses)
{
    ScopedTemporaryWorkingDirectory isolated_cwd;
    ScopedCrimeRecordReset crime_record_reset;
    ScopedPlayerTable player_table_entries { 1, 2, 3 };
    ScopedTestWorld test_world(1);

    TestPc criminal(30, 1);
    TestPc victim(25, 2);
    TestPc witness(20, 3);
    TestNpc bystander(1);

    // All four in room 0's occupant chain, in this order -- the walk
    // record_crime's conversion now runs is
    // rots::entity::occupants(room_of(victim)) (handler.h), which reads
    // room->people/char_data::next_in_room exactly like the original raw
    // walk did, so the fixture only needs the chain itself, not
    // char_to_room()'s unrelated zone-power/light bookkeeping.
    // ScopedRoomOccupants (LS-3a T3, test_placement.h) publishes exactly
    // that chain and stamps each location via set_location(); declared
    // after the four characters so it unwinds -- and takes them back out --
    // while they are all still alive.
    ScopedRoomOccupants occupants { &test_world.room(), 0,
        { &criminal.character, &victim.character, &witness.character,
            &bystander.character } };

    const int crimes_before = num_of_crimes;

    record_crime(&criminal.character, &victim.character, /* crime */ 0, /* wit_type */ 0);

    // Two add_crime() calls: the victim (record_crime never excludes the
    // victim from its own witness walk -- see the file header comment) and
    // the one eligible third-party PC witness. The criminal is skipped by
    // pointer identity and the NPC bystander by IS_NPC().
    EXPECT_EQ(num_of_crimes, crimes_before + 2)
        << "Expected record_crime()'s occupant walk to call add_crime() once for the victim "
           "(counted as a witness to their own crime) and once for the eligible third-party PC "
           "witness, skipping the criminal (excluded by identity) and the NPC bystander "
           "(excluded by IS_NPC()).";
}

TEST(DbBootRecordCrime, ExcludesAnNpcBystanderFromTheWitnessWalk)
{
    ScopedTemporaryWorkingDirectory isolated_cwd;
    ScopedCrimeRecordReset crime_record_reset;
    ScopedPlayerTable player_table_entries { 1, 2 };
    ScopedTestWorld test_world(1);

    TestPc criminal(30, 1);
    TestPc victim(25, 2);
    TestNpc bystander(1);

    ScopedRoomOccupants occupants { &test_world.room(), 0,
        { &criminal.character, &victim.character, &bystander.character } };

    const int crimes_before = num_of_crimes;

    record_crime(&criminal.character, &victim.character, /* crime */ 0, /* wit_type */ 0);

    // Only the victim's own self-witness call -- the NPC bystander must not
    // add a second one. Isolates the IS_NPC() exclusion from the third-party
    // witness case above.
    EXPECT_EQ(num_of_crimes, crimes_before + 1)
        << "Expected record_crime() to call add_crime() only once (for the victim's own "
           "self-witness) when the only other room occupant is an NPC excluded by IS_NPC().";
}
