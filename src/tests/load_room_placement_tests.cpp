// load_room_placement_tests.cpp
//
// Executable ground truth for the load_character() -> Crash_load() ->
// Crash_follower_load() placement chain and for the rnum/vnum identity of
// the persisted `specials2.load_room` field.
//
// WHY THIS FILE EXISTS: the chain below was disputed on a static reading.
// Every claim it makes is now pinned by a test that observes the REAL
// production functions (calc_load_room, Crash_follower_load, char_to_room,
// real_room, world_room_vnum, write_player_text, load_player_from_text,
// store_to_char) rather than by an argument about what the source says.
//
// THE CHAIN UNDER TEST (file:line as of this commit):
//   db_players.cpp:1376  store_to_char: ch->in_room = GET_LOADROOM(ch)
//                        -- the raw persisted integer lands in in_room
//                           UNINTERPRETED (no real_room(), no vnum->rnum).
//   objsave.cpp:494-495  load_character (A): if (location_of(ch) == NOWHERE)
//                                              ch->in_room = ch->specials2.load_room;
//   objsave.cpp:497      load_character (B): fp = Crash_load(ch);
//   objsave.cpp:469        Crash_load: ch->specials2.load_room =
//                                        calc_load_room(ch, rentcode)
//                          -- calc_load_room (objsave.cpp:512+) returns an
//                             RNUM: its mortal branch is
//                             `load_room = real_room(location_of(ch))`.
//                             It never writes ch->in_room.
//   objsave.cpp:477        Crash_load: Crash_follower_load(ch, data)
//   objsave.cpp:718          Crash_follower_load: char_to_room(mob,   ch->specials2.load_room)
//   objsave.cpp:812          Crash_follower_load: char_to_room(mount, ch->specials2.load_room)
//                            -- both use the rnum computed at :469, the same
//                               index the player is about to be placed at.
//                               They MUST NOT read location_of(ch): in_room
//                               still holds the RAW persisted value here (a
//                               VNUM on the ordinary quit/rent path), and
//                               using it as a world[] index was the
//                               historical follower-misplacement bug whose
//                               fix this file pins.
//   objsave.cpp:502      load_character (C): char_to_room(ch, ch->specials2.load_room)
//                        -- the PLAYER is placed at the same rnum.
//
// So owner and followers receive the SAME index. char_to_room
// (placement.cpp:313) treats its argument as a world[] index (an rnum) in
// both cases -- which is why the historical code, which passed
// location_of(ch) for followers, sent them to world[vnum] while the owner
// went to world[real_room(vnum)] whenever `real_room(persisted) != persisted`
// (the normal condition on any real world).
//
// FIXTURE DESIGN -- WHY VNUM != RNUM MATTERS: a test world whose room
// numbers happen to equal their indices cannot see this bug at all; the two
// placements coincide and every assertion passes vacuously. Every fixture
// here therefore stamps world[i].number = 5 + 10*i, so rnum 3 <-> vnum 35 and
// the two are never confusable. Index 35 is ALSO a valid world[] slot in this
// fixture (create_bulk allocates room_count + EXTENSION_SIZE rooms), so the
// misplacement lands in a real, distinct room rather than in
// room_data::operator[]'s out-of-range fallback -- which is what happens on a
// real ~30k-room MUD, where a vnum used as an index is almost always in range
// and almost never the right room.
//
// SCOPE / WHAT IS NOT EXERCISED (stated plainly, per the "say what you
// stubbed" rule): these tests do not call load_character() or Crash_load()
// themselves. Both are gated on rent-file/JSON I/O and a booted world that
// this process does not have. Instead the tests replay load_character()'s and
// Crash_load()'s exact statement sequence (the four numbered steps above),
// calling the real calc_load_room(), the real Crash_follower_load(), and the
// real char_to_room() in the real order. What is NOT covered by that choice:
// whether Crash_load() can reach line :469/:477 at all for a given rent code,
// and any effect of the object-restore loop that runs between them. Neither
// touches in_room or load_room, so the confidence cost is low -- but it is a
// cost, and the ordering itself (that :469 precedes :477, and that neither
// writes in_room) is a source-read, not something these tests prove.
//
// HISTORY: this file first landed as pure characterization of the PRE-FIX
// behavior (owner and follower diverging; Emergency_save persisting an rnum).
// The follower/mount placement fix and the Emergency_save vnum fix flipped
// the divergence tests to the expectations below; each flipped or added test
// was run RED against the unfixed production code before the fix landed, so
// none of them can pass vacuously. The Q1 tests (calc_load_room never writes
// in_room) and the Q2 tests (save_char persists its argument verbatim outside
// the NOWHERE arm) pin behavior the fix deliberately did NOT change.

#include "../db.h"
#include "../handler.h"
#include "../objects_json.h"
#include "../utils.h"
#include "../zone.h"
#include "rots/core/character.h"
#include "rots/core/descriptor.h"
#include "rots/core/room.h"
#include "rots/core/types.h"
#include "test_char_cleanup.h"
#include "test_platform_compat.h"
#include "test_world.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

// None of the three chain functions is declared in any header (objsave.cpp
// exports them only by linkage) -- forward-declared here with their real
// signatures, the same convention objsave_tests.cpp uses for
// gen_receptionist() and spec_pro_tests.cpp uses for the SPECIAL() family.
int calc_load_room(struct char_data *ch, int load_result);
void Crash_follower_load(struct char_data *ch, const objects_json::ObjectSaveData &data);
void Emergency_save(void);

// Process globals with no shared-header declaration, mirroring the local
// extern convention their own production TUs use (objsave.cpp:76 for
// r_mortal_start_room, objsave_tests.cpp for mob_index,
// characterization_combat_tests.cpp for top_of_mobt, shapemob_tests.cpp for
// mob_proto).
extern struct index_data *mob_index;
extern struct char_data *mob_proto;
extern int top_of_mobt;
extern struct char_data *character_list;
extern int r_mortal_start_room[];
extern int r_immort_start_room;
extern int r_frozen_start_room;
extern int r_bugged_start_room;
extern int mini_mud;
extern int new_mud;
extern struct zone_data *zone_table;
extern int top_of_zone_table;
extern int top_of_p_table;
extern struct player_index_element *player_table;
extern struct descriptor_data *descriptor_list;

void clear_char(struct char_data *ch, int mode);

namespace {

// Number of addressable rooms in the fixture world. Six is the smallest count
// that gives real_room()'s binary search a non-degenerate range while keeping
// every stamped vnum (5..55) inside create_bulk()'s allocated index space
// (6 + EXTENSION_SIZE == 56 slots, valid indices 0..55), so the "vnum used as
// an index" misplacement lands in a real room instead of operator[]'s
// out-of-range fallback.
constexpr int kRoomCount = 6;

// The rnum <-> vnum pair every test pivots on: world[3].number == 35.
// Deliberately far apart so no assertion can pass by coincidence.
constexpr int kOwnerRnum = 3;
constexpr int kOwnerVnum = 35;

// The vnum stamped on world[i]: 5, 15, 25, 35, 45, 55. Ascending, because
// real_room() (db_world.cpp:1732) binary-searches world[].number.
constexpr int room_vnum_for(int rnum) { return 5 + 10 * rnum; }

// Mob vnum of the fabricated follower prototype. Arbitrary; only has to be
// findable by real_mobile() in the one-entry fixture mob_index below.
constexpr int kFollowerMobVnum = 4200;

// A world whose room numbers deliberately differ from their indices, plus the
// single zone char_to_room() charges a non-NPC's race power to. Restores
// zone_table/top_of_zone_table on scope exit; ScopedTestWorld owns the world
// itself.
class ScopedVnumWorld {
  public:
    ScopedVnumWorld()
        : m_previous_zone_table(zone_table), m_previous_top_of_zone_table(top_of_zone_table) {
        for (int rnum = 0; rnum < kRoomCount; ++rnum) {
            world[rnum].number = room_vnum_for(rnum);
            world[rnum].zone = 0;
            world[rnum].people = nullptr;
            world[rnum].light = 0;
        }
        // The slot a vnum-as-index misplacement lands in. create_bulk()
        // dummy_room_data()-initialized it (number == -1, so real_room() can
        // never return it), but its occupant list is reset here so an
        // assertion about who ended up there starts from a known state.
        world[kOwnerVnum].people = nullptr;
        world[kOwnerVnum].zone = 0;

        zone_table = new zone_data[1]{};
        zone_table[0].number = 0;
        // 1, not 0: zone_by_id_impl() (zone_load.cpp:57) bounds-checks with
        // `znum >= top_of_zone_table` (exclusive), i.e. it reads this global as
        // a COUNT, not a top index -- the "boundary-symmetry caveat" its own
        // contract comment flags. A 0 here makes every zone_by_id() call
        // return nullptr and char_to_room() segfault on a non-NPC.
        top_of_zone_table = 1;
    }

    ~ScopedVnumWorld() {
        for (int rnum = 0; rnum < kRoomCount; ++rnum)
            world[rnum].people = nullptr;
        world[kOwnerVnum].people = nullptr;

        delete[] zone_table;
        zone_table = m_previous_zone_table;
        top_of_zone_table = m_previous_top_of_zone_table;
    }

    ScopedVnumWorld(const ScopedVnumWorld &) = delete;
    ScopedVnumWorld &operator=(const ScopedVnumWorld &) = delete;

  private:
    // ScopedTestWorld must be destroyed AFTER this class's own room resets,
    // so it is declared first (destruction runs in reverse declaration
    // order). Sized kRoomCount; sets top_of_world = kRoomCount - 1, which is
    // the range real_room() searches.
    ScopedTestWorld m_test_world{kRoomCount};

    // Whatever zone_table/top_of_zone_table held before this fixture ran
    // (always null/0 in this binary, which never boots world data), restored
    // verbatim so no later suite observes the fixture's table.
    zone_data *m_previous_zone_table;
    int m_previous_top_of_zone_table;
};

// Saves and restores the boot-computed start-room rnums calc_load_room()
// falls back to, plus the two "quiet boot" flags that suppress real_room()'s
// "Room %d does not exist in database" stderr line on a deliberate
// lookup miss (this suite provokes that miss on purpose).
class ScopedStartRooms {
  public:
    ScopedStartRooms()
        : m_previous_immort(r_immort_start_room), m_previous_frozen(r_frozen_start_room),
          m_previous_bugged(r_bugged_start_room), m_previous_mini_mud(mini_mud),
          m_previous_new_mud(new_mud) {
        for (int race = 0; race < MAX_RACES; ++race) {
            m_previous_mortal[race] = r_mortal_start_room[race];
            r_mortal_start_room[race] = kRacialStartRnum;
        }
        r_immort_start_room = kRacialStartRnum;
        r_frozen_start_room = kRacialStartRnum;
        r_bugged_start_room = room_vnum_for(kRacialStartRnum);
        mini_mud = 1;
        new_mud = 0;
    }

    ~ScopedStartRooms() {
        for (int race = 0; race < MAX_RACES; ++race)
            r_mortal_start_room[race] = m_previous_mortal[race];
        r_immort_start_room = m_previous_immort;
        r_frozen_start_room = m_previous_frozen;
        r_bugged_start_room = m_previous_bugged;
        mini_mud = m_previous_mini_mud;
        new_mud = m_previous_new_mud;
    }

    ScopedStartRooms(const ScopedStartRooms &) = delete;
    ScopedStartRooms &operator=(const ScopedStartRooms &) = delete;

    // The rnum every start-room fallback in calc_load_room() resolves to in
    // this fixture. Chosen != kOwnerRnum and != kOwnerVnum so a test can tell
    // "fell back to the start room" apart from both placement outcomes.
    static constexpr int kRacialStartRnum = 0;

  private:
    // Prior values of the boot-computed globals calc_load_room() reads,
    // restored verbatim at scope exit so no later suite in the monolithic
    // runner observes this fixture's substitutes.
    int m_previous_mortal[MAX_RACES];
    int m_previous_immort;
    int m_previous_frozen;
    int m_previous_bugged;
    int m_previous_mini_mud;
    int m_previous_new_mud;
};

// A one-entry mob_index/mob_proto pair so Crash_follower_load()'s
// real_mobile()/read_mobile() calls resolve against a real prototype -- the
// same fake-prototype shape shapemob_tests.cpp uses (clear_char(MOB_ISNPC)
// against a stack slot), not a mock: read_mobile() runs its whole real body
// against it.
class ScopedFollowerPrototype {
  public:
    ScopedFollowerPrototype()
        : m_previous_mob_index(mob_index), m_previous_mob_proto(mob_proto),
          m_previous_top_of_mobt(top_of_mobt) {
        clear_char(&m_prototype_storage[0], MOB_ISNPC);
        // Non-zero so read_mobile()'s "Mobile %d had its stats fixed."
        // mudlog path stays quiet and get_naked_perception()/
        // get_naked_willpower() read sane values.
        m_prototype_storage[0].abilities.str = 100;
        m_prototype_storage[0].abilities.intel = 100;
        m_prototype_storage[0].abilities.wil = 100;
        m_prototype_storage[0].abilities.dex = 100;
        m_prototype_storage[0].abilities.con = 100;
        m_prototype_storage[0].abilities.lea = 100;
        m_prototype_storage[0].abilities.hit = 100;
        m_prototype_storage[0].tmpabilities = m_prototype_storage[0].abilities;
        m_prototype_storage[0].player.level = 5;
        m_prototype_storage[0].specials.store_prog_number = 0;
        m_prototype_storage[0].in_room = NOWHERE; // LS1-ALLOW: fixture init

        m_index_storage = new index_data[1]{};
        m_index_storage[0].virt = kFollowerMobVnum;
        m_index_storage[0].number = 0;
        m_index_storage[0].func = nullptr;

        mob_index = m_index_storage;
        mob_proto = m_prototype_storage;
        top_of_mobt = 0;
    }

    ~ScopedFollowerPrototype() {
        delete[] m_index_storage;
        mob_index = m_previous_mob_index;
        mob_proto = m_previous_mob_proto;
        top_of_mobt = m_previous_top_of_mobt;
    }

    ScopedFollowerPrototype(const ScopedFollowerPrototype &) = delete;
    ScopedFollowerPrototype &operator=(const ScopedFollowerPrototype &) = delete;

  private:
    // The single fabricated mobile prototype read_mobile() copy-assigns from;
    // an array (not a scalar) because mob_proto is indexed as one.
    char_data m_prototype_storage[1]{};
    // Releases the prototype's clear_char()-allocated profs at scope exit.
    ScopedClearCharFields m_prototype_cleanup{m_prototype_storage[0]};
    // The single mob_index entry real_mobile() binary-searches; owned here.
    index_data *m_index_storage = nullptr;

    // Prior table pointers/bounds, restored verbatim at scope exit.
    index_data *m_previous_mob_index;
    char_data *m_previous_mob_proto;
    int m_previous_top_of_mobt;
};

// Installs a substitute player_table for the duration of a test and restores
// the previous table on scope exit -- INCLUDING on a mid-test fatal ASSERT.
// The round-trip test originally hand-rolled this save/restore, which a
// failing ASSERT_* would have skipped (it returns from the test body),
// leaking the fixture table into every later suite in the monolithic runner.
class ScopedPlayerTable {
  public:
    // Installs a one-entry table holding `name`; pass nullptr for an EMPTY
    // table (top_of_p_table == -1, so save_char()'s lookup loop never runs
    // and its "not being saved" early return fires -- the shape the
    // Emergency_save test relies on).
    explicit ScopedPlayerTable(const char *name)
        : m_previous_table(player_table), m_previous_top(top_of_p_table) {
        if (name != nullptr) {
            m_owned_table = new player_index_element[1]{};
            m_owned_table[0].name = strdup(name);
            player_table = m_owned_table;
            top_of_p_table = 0;
        } else {
            player_table = nullptr;
            top_of_p_table = -1;
        }
    }

    ~ScopedPlayerTable() {
        if (m_owned_table != nullptr) {
            free(m_owned_table[0].name);
            delete[] m_owned_table;
        }
        player_table = m_previous_table;
        top_of_p_table = m_previous_top;
    }

    ScopedPlayerTable(const ScopedPlayerTable &) = delete;
    ScopedPlayerTable &operator=(const ScopedPlayerTable &) = delete;

  private:
    // The substitute one-entry table this fixture owns (null in empty mode);
    // freed together with its strdup'd name on scope exit.
    player_index_element *m_owned_table = nullptr;
    // Prior process-global table pointer/bound, restored verbatim on exit.
    player_index_element *m_previous_table;
    int m_previous_top;
};

// A mortal player who survives every guard in calc_load_room() without
// tripping one of its overrides: level in [1, LEVEL_IMMORT), race != 0 (race
// 0 forces the immort start room), no PLR_FROZEN/PLR_LOADROOM/PLR_INVSTART,
// and abilities above the "bugged character" floor (str/dex/move >= 1, move
// <= 1000, spirit in [0, 100000]).
void make_mortal_player(char_data &player) {
    clear_char(&player, MOB_VOID);
    player.player.level = 10;
    player.player.race = RACE_HUMAN;
    player.abilities.str = 100;
    player.abilities.dex = 100;
    player.abilities.move = 100;
    player.tmpabilities = player.abilities;
    player.constabilities = player.abilities;
    player.points.spirit = 0;
    player.specials2.act = 0; // not IS_NPC; no PLR_* flags set
    player.in_room = NOWHERE; // LS1-ALLOW: fixture init
    player.next_in_room = nullptr;
    player.followers = nullptr;
    player.master = nullptr;
}

// One follower record shaped exactly like Crash_follower_save() writes for a
// plain (non-mount, non-guardian) charmed follower: the mob's vnum, no mount,
// no carried objects. flag_config FOL_MOUNT (objsave.cpp:60, value 0) is the
// zero-initialized default and its switch arm is a no-op for a
// non-MOB_AGGRESSIVE prototype, so the record exercises the placement path
// without dragging in the guardian/tame scaling branches.
objects_json::ObjectSaveData make_single_follower_save() {
    objects_json::ObjectSaveData data{};
    objects_json::FollowerData follower{};
    follower.fol_vnum = kFollowerMobVnum;
    follower.mount_vnum = 0;
    follower.flag_config = 0;
    data.followers.push_back(follower);
    return data;
}

// Unwinds everything read_mobile()/add_follower() did for a spawned follower:
// the room occupant list, the global character_list, the follow link (via the
// production stop_follower()), and the calloc+placement-new allocation itself
// (read_mobile does `CREATE(mob, ...); new (mob) char_data();`).
void release_spawned_follower(char_data *mob) {
    if (mob == nullptr)
        return;

    if (mob->master != nullptr)
        stop_follower(mob, FOLLOW_MOVE);

    if (mob->in_room != NOWHERE) {             // LS1-ALLOW: fixture teardown
        room_data &room = world[mob->in_room]; // LS1-ALLOW: fixture teardown
        if (room.people == mob) {
            room.people = mob->next_in_room;
        } else {
            for (char_data *occupant = room.people; occupant != nullptr;
                 occupant = occupant->next_in_room) {
                if (occupant->next_in_room == mob) {
                    occupant->next_in_room = mob->next_in_room;
                    break;
                }
            }
        }
    }

    if (character_list == mob) {
        character_list = mob->next;
    } else {
        for (char_data *listed = character_list; listed != nullptr; listed = listed->next) {
            if (listed->next == mob) {
                listed->next = mob->next;
                break;
            }
        }
    }

    // read_mobile() ends with register_npc_char(mob) (entity_lifecycle.cpp:1021),
    // which permanently SETS this mob's slot in the process-global char_exists
    // bit array and advances last_control_set. Production clears that bit in
    // free_char()/extract_char() (entity_lifecycle.cpp:694, handler.cpp:632/653);
    // a fixture that skips it leaks a "this character still exists" bit into
    // every later suite in the monolithic runner -- which is exactly how an
    // early draft of this file broke
    // OlogHaiHelpers.ReturnsNullForMissingCharacterTargets (it asserts
    // char_exists(42) is false, and this suite's third spawned follower had
    // been handed abs_number 42).
    remove_char_exists(mob->abs_number);

    // NOT RELEASE(mob->profs): read_mobile()'s `*mob = mob_proto[i]` is a
    // copy-assignment, and profs is a raw pointer, so the spawned mob SHARES
    // the prototype's profs allocation ("prototype-shared for NPCs" --
    // test_char_cleanup.h). ScopedFollowerPrototype's own
    // ScopedClearCharFields owns that release; freeing it here too aborts in
    // libmalloc. The remaining owning members (skills/knowledge vectors,
    // poof strings, damage_details map) WERE deep-copied by that assignment,
    // so the destructor below is the right owner for them.
    mob->~char_data();
    std::free(mob);
}

// Replays load_character()'s and Crash_load()'s exact statement sequence
// around the follower load, calling the real production functions in the real
// order. See this file's header comment for the file:line map and for exactly
// what this replay does and does not cover.
//
// Returns the follower the chain spawned (nullptr if none), so the caller can
// assert on where it landed and then release it.
char_data *run_load_placement_chain(char_data &player, const objects_json::ObjectSaveData &data,
                                    int rent_code) {
    // (A) load_character, objsave.cpp:494-495.
    if (location_of(&player) == NOWHERE)
        player.in_room = player.specials2.load_room; // LS1-ALLOW: replay of objsave.cpp:495

    // (B) Crash_load, objsave.cpp:469 -- the rnum the PLAYER will be placed at.
    player.specials2.load_room = calc_load_room(&player, rent_code);

    // (B) Crash_load, objsave.cpp:477 -- places followers at the same
    // resolved rnum (the placement-index fix this file pins).
    Crash_follower_load(&player, data);
    char_data *spawned = (player.followers != nullptr) ? player.followers->follower : nullptr;

    // (C) load_character, objsave.cpp:502 -- places the PLAYER.
    char_to_room(&player, player.specials2.load_room);

    return spawned;
}

} // namespace

// ---------------------------------------------------------------------------
// Q1 -- what is ch->in_room at the moment Crash_follower_load runs?
// ---------------------------------------------------------------------------

// calc_load_room() (objsave.cpp:512) converts the persisted load_room to an
// rnum for its RETURN VALUE only; it never writes ch->in_room. So in_room
// still holds the RAW persisted integer store_to_char() deposited at
// db_players.cpp:1376 when Crash_follower_load() runs -- which is exactly
// why the follower placement (objsave.cpp:718/:812) must read
// ch->specials2.load_room and must NOT read location_of(ch), as the pre-fix
// code did.
TEST(LoadRoomChain, CalcLoadRoomLeavesInRoomHoldingTheRawPersistedValue) {
    ScopedVnumWorld fixture_world;
    ScopedStartRooms fixture_start_rooms;

    char_data player{};
    make_mortal_player(player);
    ScopedClearCharFields player_cleanup{player};

    // Exactly what store_to_char (db_players.cpp:1376) leaves behind for a
    // character whose on-disk load_room is the room VNUM 35.
    player.specials2.load_room = kOwnerVnum;
    player.in_room = GET_LOADROOM(&player); // LS1-ALLOW: replay of db_players.cpp:1376
    ASSERT_EQ(location_of(&player), kOwnerVnum);

    const int computed_load_room = calc_load_room(&player, RENT_RENTED);

    // The return value is an RNUM: real_room(35) == 3 in this world.
    EXPECT_EQ(computed_load_room, kOwnerRnum);
    // ...but in_room was not touched, so the follower loader still sees 35.
    EXPECT_EQ(location_of(&player), kOwnerVnum);
    // The two indices the chain's two char_to_room() calls will receive.
    EXPECT_NE(computed_load_room, location_of(&player));
}

// NOTE: this file's pre-fix revision had a second Q1 test here
// (FollowerPlacementArgumentIsTheVnumWhileOwnerPlacementArgumentIsTheRnum)
// that mirrored the two DIFFERENT expressions the production code passed to
// char_to_room(). After the fix both placements pass the SAME expression
// (ch->specials2.load_room), so the mirror collapsed to comparing a value
// with itself -- unfailable, and therefore deleted rather than kept as a
// vacuous test. The end-to-end placement tests below are the named
// regression guards now: they drive the real production functions and FAIL
// if the follower loader ever reads location_of(ch) again (proved red
// against the unfixed code).

// ---------------------------------------------------------------------------
// Q3 -- does a follower end up in the same room as its owner?
// ---------------------------------------------------------------------------

// The marquee test: drives the REAL Crash_follower_load() and the REAL
// char_to_room() through the chain's real ordering, with a load_room holding
// the room VNUM that save_char(ch, NOWHERE, 0) persists on every ordinary
// quit/rent/autosave. Owner and follower land in the SAME room. Before the
// fix, the follower loader read the raw vnum out of in_room and used it as a
// world[] index, sending the follower to world[35] while the owner went to
// world[3] -- this test failed with exactly that split against the unfixed
// code, so it cannot pass vacuously.
TEST(LoadRoomChain, FollowerLandsInTheSameRoomAsItsOwnerWhenLoadRoomHoldsAVnum) {
    ScopedVnumWorld fixture_world;
    ScopedStartRooms fixture_start_rooms;
    ScopedFollowerPrototype fixture_follower_prototype;

    char_data player{};
    make_mortal_player(player);
    ScopedClearCharFields player_cleanup{player};

    // The ordinary case: load_room holds a room VNUM (see the Q2 tests below
    // for why that is what the NOWHERE save path writes).
    player.specials2.load_room = kOwnerVnum;
    player.in_room = GET_LOADROOM(&player); // LS1-ALLOW: replay of db_players.cpp:1376

    const objects_json::ObjectSaveData save_data = make_single_follower_save();
    char_data *follower = run_load_placement_chain(player, save_data, RENT_RENTED);

    ASSERT_NE(follower, nullptr) << "Crash_follower_load did not spawn the fixture follower";

    // Both are placed at the rnum calc_load_room() computed.
    EXPECT_EQ(location_of(&player), kOwnerRnum);
    EXPECT_EQ(location_of(follower), kOwnerRnum);

    // Occupant list confirms the placement rather than just the in_room
    // fields: the follower was placed first (objsave.cpp:477 runs before
    // :499) and char_to_room() appends at the TAIL, so the follower heads
    // the chain with the owner behind him.
    EXPECT_EQ(world[kOwnerRnum].people, follower);
    EXPECT_EQ(follower->next_in_room, &player);
    EXPECT_EQ(player.next_in_room, nullptr);

    // The slot the raw vnum-as-index misplacement used to land in (index 35,
    // a dummy room with vnum -1 that real_room() can never return) stays
    // empty.
    EXPECT_EQ(world[kOwnerVnum].people, nullptr);

    release_spawned_follower(follower);
}

// Control: when the persisted load_room happens to satisfy
// real_room(v) == v, the chain places owner and follower together. This is
// the configuration under which the bug is INVISIBLE, and it is why a fixture
// world with number == index proves nothing. Stamped explicitly so the
// divergence test above cannot be dismissed as a fixture artifact.
TEST(LoadRoomChain, FollowerAndOwnerLandTogetherWhenTheVnumHappensToEqualItsRnum) {
    ScopedVnumWorld fixture_world;
    ScopedStartRooms fixture_start_rooms;
    ScopedFollowerPrototype fixture_follower_prototype;

    // Re-stamp the fixture world so room index 2 also has vnum 2 (numbers
    // stay ascending: 0, 1, 2, 35, 45, 55), making real_room(2) == 2.
    world[0].number = 0;
    world[1].number = 1;
    world[2].number = 2;

    char_data player{};
    make_mortal_player(player);
    ScopedClearCharFields player_cleanup{player};

    player.specials2.load_room = 2;
    player.in_room = GET_LOADROOM(&player); // LS1-ALLOW: replay of db_players.cpp:1376

    const objects_json::ObjectSaveData save_data = make_single_follower_save();
    char_data *follower = run_load_placement_chain(player, save_data, RENT_RENTED);

    ASSERT_NE(follower, nullptr);
    EXPECT_EQ(location_of(&player), 2);
    EXPECT_EQ(location_of(follower), 2);
    EXPECT_EQ(location_of(&player), location_of(follower));

    release_spawned_follower(follower);
}

// ---------------------------------------------------------------------------
// Q4 -- the other reachable precondition: a load_room holding an RNUM
// ---------------------------------------------------------------------------

// Several live call sites still pass an RNUM where save_char()'s contract
// wants a VNUM (interpre.cpp:3796 `save_char(d->character,
// location_of(d->character), 0)` runs immediately after load_character() has
// set in_room to an rnum; fight.cpp:948 passes r_mortal_start_room[],
// documented "rnum of mortal start room" at db_world.cpp:129 -- see the
// EmergencySave test below for the one such site this fix wave corrected).
// This test pins what the chain does on the NEXT login after an rnum reached
// disk: real_room() cannot find a room whose VNUM equals that rnum, so
// calc_load_room() falls back to the racial start room -- and the follower
// comes along to the SAME room. Before the fix the follower was left standing
// in the room the player logged out of (the raw value read as an index); this
// test failed with exactly that split against the unfixed code.
TEST(LoadRoomChain, RnumShapedLoadRoomSendsOwnerAndFollowerToTheStartRoomTogether) {
    ScopedVnumWorld fixture_world;
    ScopedStartRooms fixture_start_rooms;
    ScopedFollowerPrototype fixture_follower_prototype;

    char_data player{};
    make_mortal_player(player);
    ScopedClearCharFields player_cleanup{player};

    // An RNUM in the field, as the rnum-passing save sites above persist it.
    player.specials2.load_room = kOwnerRnum;
    player.in_room = GET_LOADROOM(&player); // LS1-ALLOW: replay of db_players.cpp:1376

    // No room in this world has VNUM 3 (they are 5, 15, 25, 35, 45, 55).
    ASSERT_EQ(real_room(kOwnerRnum), -1);

    const objects_json::ObjectSaveData save_data = make_single_follower_save();
    char_data *follower = run_load_placement_chain(player, save_data, RENT_RENTED);

    ASSERT_NE(follower, nullptr);

    // calc_load_room()'s `else if ((load_room = real_room(location_of(ch))) < 0)`
    // arm fires: the owner goes to the racial start room, not where he was.
    EXPECT_EQ(location_of(&player), ScopedStartRooms::kRacialStartRnum);
    // ...and the follower is placed at the same resolved rnum, not at the raw
    // index (which for an rnum-shaped save is the room the player logged out
    // from -- where the pre-fix code stranded him).
    EXPECT_EQ(location_of(follower), ScopedStartRooms::kRacialStartRnum);

    // Occupant lists: the follower was placed first and char_to_room()
    // appends at the tail, so the follower heads the chain with the owner
    // behind him; the logged-out-from room stays empty.
    EXPECT_EQ(world[ScopedStartRooms::kRacialStartRnum].people, follower);
    EXPECT_EQ(follower->next_in_room, &player);
    EXPECT_EQ(world[kOwnerRnum].people, nullptr);

    release_spawned_follower(follower);
}

// ---------------------------------------------------------------------------
// Q2 -- is specials2.load_room a vnum or an rnum on disk?
// ---------------------------------------------------------------------------

// The ONLY conversion anywhere in save_char() (db_players.cpp:1924-1927) is
// the NOWHERE arm, which runs the registered room-vnum hook over in_room.
// world_room_vnum() (db_world.cpp) is that hook's production body: it turns
// an rnum into a VNUM. So `save_char(ch, NOWHERE, 0)` persists a vnum.
TEST(LoadRoomPersistence, TheNowhereSavePathConvertsInRoomToAVnum) {
    ScopedVnumWorld fixture_world;

    EXPECT_EQ(world_room_vnum(kOwnerRnum), kOwnerVnum);
    EXPECT_NE(world_room_vnum(kOwnerRnum), kOwnerRnum);
}

// ...and the explicit arm applies NO conversion: whatever integer the caller
// hands save_char() is the integer written to disk. Pinned end-to-end through
// the real serializer (write_player_text, db_players.cpp:1674 -- the function
// save_char() reaches for every non-account-native character) and the real
// parser (load_player_from_text, db_players.cpp:755), for BOTH a vnum-shaped
// and an rnum-shaped value. The format cannot tell them apart, and neither
// end normalizes, so an rnum-passing call site silently persists an rnum.
TEST(LoadRoomPersistence, TextRoundTripPreservesWhateverIntegerTheCallerPassed) {
    ScopedVnumWorld fixture_world;

    // load_player_from_text() resolves the name against player_table before
    // parsing; one entry is all it needs.
    ScopedPlayerTable fixture_player_table{"loadroomchr"};

    char path_template[] = "/tmp/rots-loadroom-roundtrip-XXXXXX";
    char *created_path = rots_mkdtemp(path_template);
    ASSERT_NE(created_path, nullptr);
    const std::string temp_dir = created_path;
    const std::string scratch = temp_dir + "/loadroom-scratch";

    // Both shapes travel the identical path. 35 is the room's VNUM; 3 is its
    // RNUM. Nothing in the format distinguishes them.
    for (const int persisted_value : {kOwnerVnum, kOwnerRnum}) {
        char_data writer{};
        descriptor_data writer_descriptor{};
        make_mortal_player(writer);
        ScopedClearCharFields writer_cleanup{writer};
        snprintf(writer_descriptor.pwd, sizeof(writer_descriptor.pwd), "%s", "LoadRmPw");
        snprintf(writer_descriptor.host, sizeof(writer_descriptor.host), "%s", "loadroom.test");
        writer.desc = &writer_descriptor;
        RELEASE(writer.player.name);
        CREATE(writer.player.name, char, strlen("loadroomchr") + 1);
        strcpy(writer.player.name, "loadroomchr");

        ASSERT_TRUE(write_player_text(&writer, persisted_value, scratch))
            << "write_player_text failed for " << persisted_value;

        // Read the serialized bytes back with the production parser.
        FILE *file = fopen(scratch.c_str(), "rb");
        ASSERT_NE(file, nullptr);
        std::string serialized;
        char buffer[1024];
        size_t bytes_read = 0;
        while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0)
            serialized.append(buffer, bytes_read);
        fclose(file);

        EXPECT_NE(serialized.find("load_room   " + std::to_string(persisted_value) + "\n"),
                  std::string::npos)
            << "serialized bytes did not carry the caller's value verbatim";

        char name_buffer[] = "loadroomchr";
        char_file_u reloaded{};
        ASSERT_GE(load_player_from_text(name_buffer, serialized, &reloaded), 0);

        // Verbatim through the round trip -- no vnum/rnum normalization.
        EXPECT_EQ(reloaded.specials2.load_room, persisted_value);

        // ...and store_to_char (db_players.cpp:1376) copies it straight into
        // in_room, which is the value Crash_follower_load reads.
        char_data reader{};
        clear_char(&reader, MOB_VOID);
        ScopedClearCharFields reader_cleanup{reader};
        ScopedStoreToCharFields reader_store_cleanup{reader};
        store_to_char(&reloaded, &reader);
        EXPECT_EQ(location_of(&reader), persisted_value);
        EXPECT_EQ(GET_LOADROOM(&reader), persisted_value);

        RELEASE(writer.player.title);
        RELEASE(writer.player.description);
        RELEASE(writer.player.name);
    }

    std::filesystem::remove(scratch);
    std::filesystem::remove(temp_dir);
}

// Emergency_save() (objsave.cpp:1550) runs on the signal-driven shutdown
// paths (signals.cpp) -- exactly when NO 30-second autosave will follow to
// rewrite the field -- and historically passed r_mortal_start_room[] (an
// RNUM, db_world.cpp:129) straight into save_char(), whose explicit arm
// persists its argument verbatim (proved by the round-trip test above). That
// put an rnum on disk permanently. This test drives the REAL Emergency_save()
// over a fixture descriptor list and asserts the value it now hands
// save_char() is the start room's VNUM.
//
// Observation point: with an empty player_table, save_char() takes its "you
// are not being saved" early return (db_players.cpp:1939-1943) AFTER stamping
// ch->specials2.load_room with its load_room argument (db_players.cpp:1927)
// -- so the stamped field IS the value that would have been persisted, and no
// player-file I/O runs. Crash_crashsave()'s object write fails gracefully
// (and writes nothing) because no plrobjs/ directory exists under the test
// working directory. This test failed against the unfixed code (stamped 0,
// the rnum, instead of 5, the vnum), so it cannot pass vacuously.
TEST(LoadRoomPersistence, EmergencySavePersistsTheStartRoomVnumNotItsRnum) {
    ScopedVnumWorld fixture_world;
    // r_mortal_start_room[every race] = rnum 0, whose room carries vnum 5 --
    // rnum and vnum deliberately distinct so the assertion can tell them
    // apart.
    ScopedStartRooms fixture_start_rooms;

    // save_char() resolves the name against player_table; an empty table
    // (top_of_p_table == -1, loop body never runs) forces the early-return
    // path described above without reading the table pointer.
    ScopedPlayerTable fixture_player_table{nullptr};

    char_data player{};
    make_mortal_player(player);
    ScopedClearCharFields player_cleanup{player};
    RELEASE(player.player.name);
    CREATE(player.player.name, char, strlen("emrgsavechr") + 1);
    strcpy(player.player.name, "emrgsavechr");

    // The capturing-descriptor shape from comm_output_tests.cpp: queued
    // output lands in small_outbuf, no network connection involved.
    // CON_PLYNG satisfies both Emergency_save()'s own gate and
    // send_to_char()'s connected check.
    descriptor_data descriptor{};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = CON_PLYNG;
    descriptor.character = &player;
    descriptor.next = nullptr;
    player.desc = &descriptor;

    // Install a descriptor list containing ONLY the fixture descriptor, so
    // Emergency_save() cannot touch anything another suite may have left in
    // the global list; restored below.
    descriptor_data *previous_descriptor_list = descriptor_list;
    descriptor_list = &descriptor;

    // Sentinel distinct from both the rnum (0) and the vnum (5), so the
    // assertion proves Emergency_save() actually wrote the field.
    player.specials2.load_room = -12345;

    Emergency_save();

    // The persisted value is the start room's VNUM...
    EXPECT_EQ(GET_LOADROOM(&player), room_vnum_for(ScopedStartRooms::kRacialStartRnum));
    // ...not its rnum (what the pre-fix code passed).
    EXPECT_NE(GET_LOADROOM(&player), ScopedStartRooms::kRacialStartRnum);

    // Confirm save_char() took the expected early-return path (the stamped
    // field above is therefore the would-be-persisted value, and no player
    // file was written).
    EXPECT_NE(strstr(descriptor.small_outbuf, "you are not being saved"), nullptr)
        << "save_char did not take the empty-player-table early return; "
        << "output: " << descriptor.small_outbuf;

    descriptor_list = previous_descriptor_list;
    RELEASE(player.player.name);
}
