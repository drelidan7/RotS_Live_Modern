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
//
// LS-3a WAVE T2 TRANCHE 2e ADDENDUM (rulings R-A2 / AM-1 / R-T0b-3). The
// chain above is also THE VNUM CHANNEL -- in_room carrying a room VNUM rather
// than a world[] index across this whole protocol. Before that tranche, three
// of the channel's sites were pinned by nothing anywhere in the tree:
// objsave.cpp:494 (the entry read), :495 (its write), and :556 (the
// extension-room crash-load notice). R-A2 requires characterization FIRST, so
// the Q1b/Q1c sections below landed before any conversion, together with
// AM-1's mandatory persisted-value test for the OTHER end of the channel --
// gen_receptionist()'s rent stash (objsave.cpp:1458-1461), whose write and
// whose single consumer must convert in the same commit or LS-3b silently
// persists load_room = NOWHERE for every renting player.
//
// The replay of :494-:495 now lives in ONE place,
// replay_load_character_guard() below, shared by the direct
// characterization tests and by run_load_placement_chain(). Its statements
// track production: when tranche 2e routed the production lines onto
// stash_load_room_vnum()/peek_load_room_vnum(), the replay was routed with
// them in the same commit, so this file keeps saying what the code does
// rather than what it used to do.

#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "../objects_json.h"
#include "../protocol.h"
#include "../spells.h"
#include "../utils.h"
#include "../world_hooks.h"
#include "../zone.h"
#include "rots/core/character.h"
#include "rots/core/descriptor.h"
#include "rots/core/object.h"
#include "rots/core/room.h"
#include "rots/core/types.h"
#include "test_char_cleanup.h"
#include "test_placement.h"
#include "test_platform_compat.h"
#include "test_world.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string>

// None of the three chain functions is declared in any header (objsave.cpp
// exports them only by linkage) -- forward-declared here with their real
// signatures, the same convention objsave_tests.cpp uses for
// gen_receptionist() and spec_pro_tests.cpp uses for the SPECIAL() family.
int calc_load_room(struct char_data *ch, int load_result);
void Crash_follower_load(struct char_data *ch, const objects_json::ObjectSaveData &data);
FILE *Crash_load(struct char_data *character);
void Emergency_save(void);
// The whole login placement, driven for real by the LS-3b T9b B-1 tests at
// the end of this file (nothing above them calls it; they replay it). Same
// linkage-only export as the three above.
void load_character(struct char_data *ch);
// fight.cpp's corpse builder, exported by linkage only like the objsave.cpp
// group. Driven by LoadWindowDeath.* to establish what raw_kill() has already
// done to a character's equipment by the time extract_char() sees it.
struct obj_data *make_corpse(struct char_data *character, struct char_data *killer,
                             int attack_type);

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
// The two process-global rosters extract_char() walks and re-links
// (handler.cpp:495-496 declares them the same local-extern way). Saved and
// restored by ScopedGlobalCharacterLists below -- a stack char_data left on
// either one outlives its frame, which is the leak class the LS-2 finalization
// battery caught as a monolithic-runner SIGSEGV.
extern struct char_data *combat_list;
extern struct char_data *waiting_list;
// make_physical_corpse() (fight.cpp) prepends every corpse it CREATE()s to
// this process-global list; LoadWindowDeath.* saves and unlinks so no later
// suite in the monolithic runner inherits a freed corpse.
extern struct obj_data *object_list;
// The VNUM sibling of r_mortal_start_room[] above. db_world.cpp:814-815
// derives the rnum array from this one with real_room() and CLAMPS to 0 on a
// miss, so the two can name different rooms; the death-save rider test below
// reproduces that divergence.
extern int mortal_start_room[];

void clear_char(struct char_data *ch, int mode);

// act_wiz.cpp exports do_wizset() only by linkage, like the objsave.cpp trio
// above; declared here with the ACMD signature act_wiz_format_tests.cpp uses.
ACMD(do_wizset);

// The two async walkers the mid-window guard tests below drive. Neither is
// declared in a shared header (protocol.cpp's is reached in production only
// through world_hooks.h's weather hook, comm.cpp's only from the pulse loop);
// forward-declared here exactly as protocol_tests.cpp and comm_act_tests.cpp
// already declare them for their own suites.
void broadcast_weather_msdp_update(rots::world::weather_msdp_kind kind);
void clean_expose_elements();

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
            room_by_id_total(rnum)->number = room_vnum_for(rnum);
            room_by_id_total(rnum)->zone = 0;
            clear_room_occupants(room_by_id_total(rnum));
            room_by_id_total(rnum)->light = 0;
        }
        // The slot a vnum-as-index misplacement lands in. create_bulk()
        // dummy_room_data()-initialized it (number == -1, so real_room() can
        // never return it), but its occupant list is reset here so an
        // assertion about who ended up there starts from a known state.
        clear_room_occupants(room_by_id_total(kOwnerVnum));
        room_by_id_total(kOwnerVnum)->zone = 0;

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
            clear_room_occupants(room_by_id_total(rnum));
        clear_room_occupants(room_by_id_total(kOwnerVnum));

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
        set_location(&m_prototype_storage[0], NOWHERE);

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

// Scoped equivalent of ageland's own `-d <dir>` startup option: the game
// resolves every persistence path relative to the data directory it chdir()s
// into at startup (run_the_game, comm.cpp -- std::filesystem::current_path
// on startup_options.dir, default "lib"), so pointing the process working
// directory at a temp dir for one test's lifetime gives the REAL save/load
// code an isolated on-disk lib layout with zero production changes.
// Restores the previous working directory on scope exit -- including on a
// mid-test fatal ASSERT -- because cwd is process-global and every other
// suite's relative paths (characterization goldens, fixtures) resolve
// against it.
class ScopedWorkingDirectory {
  public:
    explicit ScopedWorkingDirectory(const std::filesystem::path &target)
        : m_previous(std::filesystem::current_path()) {
        std::filesystem::current_path(target);
    }

    ~ScopedWorkingDirectory() {
        // error_code overload: a destructor must not throw. If the restore
        // ever failed, later suites' relative-path opens would fail loudly.
        std::error_code restore_error;
        std::filesystem::current_path(m_previous, restore_error);
    }

    ScopedWorkingDirectory(const ScopedWorkingDirectory &) = delete;
    ScopedWorkingDirectory &operator=(const ScopedWorkingDirectory &) = delete;

  private:
    // The launch-time working directory every other suite's relative paths
    // resolve against; restored verbatim on scope exit.
    std::filesystem::path m_previous;
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

// THE FIXTURE-HYGIENE RULE, applied to the three rosters extract_char() and
// do_wizset() touch. extract_char() unlinks its argument from character_list
// (and abort()s if it is not there), rewrites waiting_list around it, and
// walks combat_list stopping fights -- and its waiting_list surgery
// (handler.cpp:563-570) writes THROUGH the last element when the character is
// not on the list, so a roster another suite left populated would be
// corrupted by a test that never touches it. Everything this fixture guards is
// a process global whose entries here are stack char_data, so all three are
// restored verbatim on scope exit, including on a mid-test fatal ASSERT.
class ScopedGlobalCharacterLists {
  public:
    ScopedGlobalCharacterLists()
        : m_previous_character_list(character_list), m_previous_combat_list(combat_list),
          m_previous_waiting_list(waiting_list) {
        character_list = nullptr;
        combat_list = nullptr;
        waiting_list = nullptr;
    }

    ~ScopedGlobalCharacterLists() {
        character_list = m_previous_character_list;
        combat_list = m_previous_combat_list;
        waiting_list = m_previous_waiting_list;
    }

    ScopedGlobalCharacterLists(const ScopedGlobalCharacterLists &) = delete;
    ScopedGlobalCharacterLists &operator=(const ScopedGlobalCharacterLists &) = delete;

  private:
    // Prior heads of the three rosters, restored verbatim at scope exit so no
    // later suite in the monolithic runner observes this fixture's entries.
    char_data *m_previous_character_list;
    char_data *m_previous_combat_list;
    char_data *m_previous_waiting_list;
};

// specialized_mages is a comm.cpp-owned process-wide std::vector<char_data*>,
// and clean_expose_elements() walks it every fast-update pulse. A test that
// tracks a stack char_data MUST untrack it before the stack unwinds or a later
// suite dereferences a dangling pointer -- the same RAII comm_act_tests.cpp's
// ScopedSpecializedMage provides for its own suite. (T0b-1's finding S7 records
// that PRODUCTION has no such untracking step on the `stat file`/`wizset file`
// paths; that is a pre-existing bug, out of this wave's charter, not something
// this fixture works around.)
class ScopedTrackedMage {
  public:
    explicit ScopedTrackedMage(char_data *mage) : m_mage(mage) { track_specialized_mage(m_mage); }

    ~ScopedTrackedMage() { untrack_specialized_mage(m_mage); }

    ScopedTrackedMage(const ScopedTrackedMage &) = delete;
    ScopedTrackedMage &operator=(const ScopedTrackedMage &) = delete;

  private:
    // The character this fixture added to the process-wide roster; removed
    // again on scope exit, including on a mid-test fatal ASSERT.
    char_data *m_mage;
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
    set_location(&player, NOWHERE);
    reset_occupant_link(&player);
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

    if (location_of(mob) != NOWHERE) {
        unlink_from_occupant_chain(*room_of(mob), mob);
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
// (A) load_character()'s own two-line preamble, objsave.cpp:494-495,
// replayed verbatim. Factored out of run_load_placement_chain() below (LS-3a
// Wave T2 tranche 2e) so the direct :494/:495 characterization tests and the
// whole-chain tests exercise ONE copy of the replay rather than two that can
// drift apart.
//
// WHAT THESE TWO LINES ARE. :494 READS the VNUM channel and :495 WRITES it --
// they are not an "is this character placed?" absence test, however much the
// NOWHERE comparison looks like one (ruling T0b-4, which overturned a census
// that had classified them by token shape). By the time load_character()
// runs, store_to_char() (db_players.cpp:1376) has already deposited the raw
// persisted integer into the same field; :494 asks whether that deposit
// happened, and :495 performs it for the character it did not happen for.
// No real_room() is applied on either line: whatever lands in the field is
// the on-disk integer, uninterpreted.
void replay_load_character_guard(char_data &player) {
    if (peek_load_room_vnum(&player) == NOWHERE)
        stash_load_room_vnum(&player, player.specials2.load_room);
}

char_data *run_load_placement_chain(char_data &player, const objects_json::ObjectSaveData &data,
                                    int rent_code) {
    // (A) load_character, objsave.cpp:494-495.
    replay_load_character_guard(player);

    // (B) Crash_load, objsave.cpp:469 -- the rnum the PLAYER will be placed at.
    player.specials2.load_room = calc_load_room(&player, rent_code);

    // (B) Crash_load, objsave.cpp:477 -- places followers at the same
    // resolved rnum (the placement-index fix this file pins).
    Crash_follower_load(&player, data);
    char_data *spawned = (player.followers != nullptr) ? player.followers->follower : nullptr;

    // (C) load_character, objsave.cpp:502 -- places the PLAYER.
    char_to_room(&player, player.specials2.load_room);

    // (D) load_character's tail -- the channel retirement (LS-3b T9b;
    // review-1 findings B-1a/B-1d and M-4). Part of the replayed sequence
    // because production performs it: the channel exists to carry the
    // persisted VNUM as far as the placement above and no further, and before
    // the store split char_to_room() ended its life by overwriting the shared
    // field. Tests that assert on post-login channel state must see the same
    // shape production leaves; the two tests that drive the REAL
    // load_character() (LoadRoomRider.*ThroughTheRealLoadCharacter and
    // LoadRoomRider.LoadCharacterRetiresTheVnumChannel*) are what pin the
    // production statement itself.
    stash_load_room_vnum(&player, NOWHERE);

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
TEST(LoadRoomChain, CalcLoadRoomLeavesTheChannelHoldingTheRawPersistedValue) {
    ScopedVnumWorld fixture_world;
    ScopedStartRooms fixture_start_rooms;

    char_data player{};
    make_mortal_player(player);
    ScopedClearCharFields player_cleanup{player};

    // Exactly what store_to_char (db_players.cpp:1376) leaves behind for a
    // character whose on-disk load_room is the room VNUM 35.
    player.specials2.load_room = kOwnerVnum;
    stash_load_room_vnum(&player, GET_LOADROOM(&player));
    // Read back through the CHANNEL, not through location_of(): since the
    // LS-3b store split (ls3b T5) the stash has its own storage and the
    // character's location stays absent for the whole load window.
    ASSERT_EQ(peek_load_room_vnum(&player), kOwnerVnum);

    const int computed_load_room = calc_load_room(&player, RENT_RENTED);

    // The return value is an RNUM: real_room(35) == 3 in this world.
    EXPECT_EQ(computed_load_room, kOwnerRnum);
    // ...but the CHANNEL was not touched, so the follower loader still sees 35.
    EXPECT_EQ(peek_load_room_vnum(&player), kOwnerVnum);
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
// Q1b -- objsave.cpp:494-495, load_character()'s guard and write
//
// LS-3a Wave T2 tranche 2e, ruling R-A2 (characterization lands BEFORE the
// conversion) and R-T0b-3 (these were two of THREE channel sites nothing in
// the tree pinned; :556 below is the third).
//
// WHY THEY WERE UNPINNED AND WHY IT MATTERED. A census had read :494's
// `location_of(ch) == NOWHERE` as an ordinary absence test and :495 as dead.
// T0b-4 overturned both: the pair is the VNUM channel's entry read and its
// second write. If :494 were left reading a real location store after LS-3b
// splits the two, a map-backed location_of() would return absent for EVERY
// logging-in character, the guard would fire unconditionally, and the
// persisted VNUM would be pushed into the location store with no
// char_to_room() -- on every single login, silently.
//
// The tests below drive the real calc_load_room()/char_to_room() through
// run_load_placement_chain()'s replay (see this file's SCOPE note for what a
// replay does and does not cover). load_character() itself is not called: it
// is gated on Crash_load()'s rent-file I/O and on act(...TO_ROOM) over a
// booted world, and it prepends its argument to the process-global
// character_list with no unwind -- the LS-2 finalization leak class. What is
// therefore NOT covered here: that :494/:495 really are the first two
// statements of load_character() (a source read), and the object-restore loop
// between them and the follower load (which touches neither field).
// ---------------------------------------------------------------------------

// The discriminating half of the guard: a character who ALREADY has a
// location must not have it clobbered by the persisted value. This is the
// W-A2 re-entry window (a character re-entering the game from the menu with
// in_room still set), and it is the ONLY arm in which the guard's presence is
// observable at all -- delete the `if` and this test fails, because in_room
// would be overwritten with the raw persisted VNUM.
//
// The other guard-false arm, the ordinary login, is deliberately NOT given a
// test of its own: store_to_char() (db_players.cpp:1376) has already copied
// specials2.load_room into in_room by then, so the two are equal, the guard
// is false, and even if it fired the write would be a self-assignment. No
// sabotage of either line can turn such a test red, and a test that cannot
// fail is not coverage (the vacuous-test class the LS-2 whole-branch review
// caught in finding O-I3). It is recorded here in prose instead.
TEST(LoadRoomChain, LoadCharacterGuardLeavesAnAlreadyPlacedCharacterAlone) {
    ScopedVnumWorld fixture_world;

    char_data player{};
    make_mortal_player(player);
    ScopedClearCharFields player_cleanup{player};

    // An RNUM in in_room (a real location) and a different VNUM on disk.
    set_location(&player, kOwnerRnum);
    player.specials2.load_room = kOwnerVnum;

    replay_load_character_guard(player);

    EXPECT_EQ(location_of(&player), kOwnerRnum);
    EXPECT_NE(location_of(&player), kOwnerVnum);
    // The persisted field is not touched either way.
    EXPECT_EQ(GET_LOADROOM(&player), kOwnerVnum);
}

// The ordinary login arm, recorded for what it is: a NO-OP. store_to_char()
// (db_players.cpp:1376) has already copied specials2.load_room into in_room,
// so the two are equal when load_character() runs; the guard is false, and
// even if it fired the write would be a self-assignment. This test cannot
// fail for a defect in either line and is not claimed to -- it is here
// because it is the state the live login path actually presents, and because
// the NEXT test is only meaningful against it.
// The write arm: a character with NO location at all (the fresh /
// account-backed path, where store_to_char()'s deposit never happened) gets
// the persisted integer copied in RAW. No real_room(), no normalization --
// which is the whole reason calc_load_room() has to convert it a moment
// later, and the reason a vnum-shaped and an rnum-shaped save are
// indistinguishable at this point.
TEST(LoadRoomChain, LoadCharacterGuardCopiesThePersistedIntegerRawWhenThereIsNoLocation) {
    ScopedVnumWorld fixture_world;

    char_data player{};
    make_mortal_player(player);
    ScopedClearCharFields player_cleanup{player};

    set_location(&player, NOWHERE);
    player.specials2.load_room = kOwnerVnum;

    replay_load_character_guard(player);

    EXPECT_EQ(peek_load_room_vnum(&player), kOwnerVnum);
    // Raw, not converted: real_room(35) is 3 in this world, and 3 is NOT what
    // landed in the channel.
    EXPECT_NE(peek_load_room_vnum(&player), kOwnerRnum);
    EXPECT_EQ(real_room(kOwnerVnum), kOwnerRnum);
    // And since the LS-3b store split the write lands in the channel ALONE:
    // the character is still, correctly, nowhere.
    EXPECT_EQ(location_of(&player), NOWHERE);
}

// :495 IS NOT DEAD -- the executable form of T0b-4's ruling. With the write,
// a locationless character with a persisted VNUM lands in that room; without
// it, calc_load_room()'s `location_of(ch) == NOWHERE` arm fires instead and
// sends them to the racial start room. Delete the write from
// replay_load_character_guard() and this test fails with exactly that split.
TEST(LoadRoomChain, LoadCharacterGuardWriteDecidesWhereALocationlessCharacterLands) {
    ScopedVnumWorld fixture_world;
    ScopedStartRooms fixture_start_rooms;

    char_data player{};
    make_mortal_player(player);
    ScopedClearCharFields player_cleanup{player};

    set_location(&player, NOWHERE);
    player.specials2.load_room = kOwnerVnum;
    // The two outcomes must be distinguishable for this test to mean
    // anything: room 3 is not the racial start room 0.
    ASSERT_NE(kOwnerRnum, ScopedStartRooms::kRacialStartRnum);

    const objects_json::ObjectSaveData no_objects{};
    run_load_placement_chain(player, no_objects, RENT_RENTED);

    EXPECT_EQ(location_of(&player), kOwnerRnum);
    EXPECT_NE(location_of(&player), ScopedStartRooms::kRacialStartRnum);
}

// ...and the case that explains why NO golden, test or gate ever observed
// :495 in twenty years: for a genuinely fresh character the persisted room IS
// the racial start room, so real_room(startVNUM) and the
// r_mortal_start_room[] fallback are the SAME number and the write is
// outcome-neutral by pure value coincidence (T0b-4's phrase). The site is
// live and reachable; its effect is simply invisible on the only path that
// normally reaches it.
TEST(LoadRoomChain, LoadCharacterGuardWriteIsOutcomeNeutralForAFreshStartRoomCharacter) {
    ScopedVnumWorld fixture_world;
    ScopedStartRooms fixture_start_rooms;

    const int start_room_vnum = room_vnum_for(ScopedStartRooms::kRacialStartRnum);

    // Path 1 -- WITH the write: the guard fires, the persisted start-room
    // VNUM lands in the channel, and calc_load_room() converts it.
    char_data with_write{};
    make_mortal_player(with_write);
    ScopedClearCharFields with_write_cleanup{with_write};
    set_location(&with_write, NOWHERE);
    with_write.specials2.load_room = start_room_vnum;
    replay_load_character_guard(with_write);
    ASSERT_EQ(peek_load_room_vnum(&with_write), start_room_vnum);
    const int room_with_write = calc_load_room(&with_write, RENT_RENTED);

    // Path 2 -- WITHOUT it: the channel stays NOWHERE, so calc_load_room()
    // takes its own `location_of(ch) == NOWHERE` arm and falls back to
    // r_mortal_start_room[] instead.
    char_data without_write{};
    make_mortal_player(without_write);
    ScopedClearCharFields without_write_cleanup{without_write};
    set_location(&without_write, NOWHERE);
    without_write.specials2.load_room = start_room_vnum;
    ASSERT_EQ(location_of(&without_write), NOWHERE);
    const int room_without_write = calc_load_room(&without_write, RENT_RENTED);

    // ...and the two arms COMPUTE THE SAME ANSWER. Asserted as an equality
    // between two live results rather than against a constant, so it fails if
    // either arm changes -- unlike a bare "lands in room 0", which would
    // survive any sabotage at all.
    EXPECT_EQ(room_with_write, room_without_write);
    EXPECT_EQ(room_with_write, ScopedStartRooms::kRacialStartRnum);
}

// ---------------------------------------------------------------------------
// Q1c -- objsave.cpp:556, the extension-room crash-load notice
//
// The third previously unpinned channel site (R-T0b-3). It is a LOG-ONLY
// range test over the channel, and EXTENSION_ROOM_HEAD is VNUM space:
// db_world.cpp assigns it to room_data::number, and extension rooms carry
// vnums 100001 and up. A census had proposed deleting it as incoherent; it is
// coherent, live, and stays -- so the tests below pin both halves of what it
// does (emit the notice) and does not do (change where anyone lands).
//
// The notice goes through log(), which writes straight to stderr rather than
// through rots::log's mudlog sink, so it is captured with gtest's own
// CaptureStderr -- the same idiom utility_format_tests.cpp uses. mini_mud is
// 1 under ScopedStartRooms, which suppresses real_room()'s own
// "does not exist in database" line, so the capture holds only what this site
// wrote.
// ---------------------------------------------------------------------------

TEST(LoadRoomChain, CalcLoadRoomLogsTheExtensionRoomNoticeOnACrashLoadWithoutChangingTheResult) {
    ScopedVnumWorld fixture_world;
    ScopedStartRooms fixture_start_rooms;

    char_data player{};
    make_mortal_player(player);
    ScopedClearCharFields player_cleanup{player};

    // The channel holds an extension-room VNUM; the persisted field holds an
    // ordinary one (calc_load_room reads them from two different places --
    // :522 takes old_room from specials2.load_room, the range test at :556
    // takes its operand from the channel).
    player.specials2.load_room = kOwnerVnum;
    stash_load_room_vnum(&player, EXTENSION_ROOM_HEAD);

    testing::internal::CaptureStderr();
    const int load_room = calc_load_room(&player, RENT_CRASH);
    const std::string captured = testing::internal::GetCapturedStderr();

    EXPECT_NE(captured.find("tried to load in room > EXTENSION_ROOM_HEAD"), std::string::npos)
        << "stderr: " << captured;
    // LOG-ONLY: no room in this world carries vnum 100000, so real_room()
    // misses and the ordinary racial-start-room fallback decides the result.
    // The notice changed nothing.
    EXPECT_EQ(load_room, ScopedStartRooms::kRacialStartRnum);
}

TEST(LoadRoomChain, CalcLoadRoomIsSilentJustBelowTheExtensionRoomThreshold) {
    ScopedVnumWorld fixture_world;
    ScopedStartRooms fixture_start_rooms;

    char_data player{};
    make_mortal_player(player);
    ScopedClearCharFields player_cleanup{player};

    player.specials2.load_room = kOwnerVnum;
    // One below the threshold: same crash rent code, same (missing) room, so
    // the ONLY difference from the test above is which side of >= the channel
    // value falls on.
    stash_load_room_vnum(&player, EXTENSION_ROOM_HEAD - 1);

    testing::internal::CaptureStderr();
    const int load_room = calc_load_room(&player, RENT_CRASH);
    const std::string captured = testing::internal::GetCapturedStderr();

    EXPECT_EQ(captured.find("tried to load in room > EXTENSION_ROOM_HEAD"), std::string::npos)
        << "stderr: " << captured;
    EXPECT_EQ(load_room, ScopedStartRooms::kRacialStartRnum);
}

TEST(LoadRoomChain, CalcLoadRoomIsSilentForAnExtensionRoomVnumOnANonCrashLoad) {
    ScopedVnumWorld fixture_world;
    ScopedStartRooms fixture_start_rooms;

    char_data player{};
    make_mortal_player(player);
    ScopedClearCharFields player_cleanup{player};

    player.specials2.load_room = kOwnerVnum;
    stash_load_room_vnum(&player, EXTENSION_ROOM_HEAD);

    testing::internal::CaptureStderr();
    // The other half of the && : an ordinary rent code silences the notice
    // for the very same channel value.
    const int load_room = calc_load_room(&player, RENT_RENTED);
    const std::string captured = testing::internal::GetCapturedStderr();

    EXPECT_EQ(captured.find("tried to load in room > EXTENSION_ROOM_HEAD"), std::string::npos)
        << "stderr: " << captured;
    EXPECT_EQ(load_room, ScopedStartRooms::kRacialStartRnum);
}

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
    stash_load_room_vnum(&player, GET_LOADROOM(&player));

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
    assert_room_chain_is(room_by_id_total(kOwnerRnum), {follower, &player});

    // The slot the raw vnum-as-index misplacement used to land in (index 35,
    // a dummy room with vnum -1 that real_room() can never return) stays
    // empty.
    EXPECT_EQ(rots::entity::first_occupant(room_by_id_total(kOwnerVnum)), nullptr);

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
    room_by_id_total(0)->number = 0;
    room_by_id_total(1)->number = 1;
    room_by_id_total(2)->number = 2;

    char_data player{};
    make_mortal_player(player);
    ScopedClearCharFields player_cleanup{player};

    player.specials2.load_room = 2;
    stash_load_room_vnum(&player, GET_LOADROOM(&player));

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
    stash_load_room_vnum(&player, GET_LOADROOM(&player));

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
    EXPECT_EQ(rots::entity::first_occupant(room_by_id_total(ScopedStartRooms::kRacialStartRnum)), follower);
    assert_occupant_link(follower, &player);
    EXPECT_EQ(rots::entity::first_occupant(room_by_id_total(kOwnerRnum)), nullptr);

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

        // ...and store_to_char (db_players.cpp) copies it straight into the
        // VNUM CHANNEL, which is the value calc_load_room() reads. Since the
        // LS-3b store split the same call also states the character's absence
        // explicitly (its discharged ordering obligation), so the location and
        // the channel are asserted separately below.
        char_data reader{};
        clear_char(&reader, MOB_VOID);
        ScopedClearCharFields reader_cleanup{reader};
        ScopedStoreToCharFields reader_store_cleanup{reader};
        // A deliberately non-absent starting location, so the assertion below
        // is not satisfied by clear_char()'s own NOWHERE. store_to_char()'s
        // POSTCONDITION since the LS-3b store split is that the character it
        // fills in is explicitly nowhere -- the ordering obligation LS-3a
        // recorded at that site and this commit discharged. Every production
        // caller happens to pass a freshly clear_char()'d character, so this
        // is the only place the postcondition is observable; that is exactly
        // why it is pinned here rather than argued.
        set_location(&reader, 12345);

        store_to_char(&reloaded, &reader);
        EXPECT_EQ(peek_load_room_vnum(&reader), persisted_value);
        EXPECT_EQ(location_of(&reader), NOWHERE)
            << "store_to_char() left a stale location on a character it just "
               "filled in from the player file";
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

// save_char()'s NOWHERE fallback, FIRST ARM (db_players.cpp:1940-1941) --
// the shape every ordinary quit/rent/autosave uses: `save_char(ch, NOWHERE,
// 0)`, meaning "work out the load room from where they are standing". The
// arm runs the registered room-vnum hook over the location, so an RNUM in
// in_room becomes a VNUM on disk.
//
// COVERAGE GAP, closed here: LoadRoomPersistence.TheNowhereSavePathConverts
// InRoomToAVnum (above) asserts world_room_vnum()'s own behavior, not
// save_char()'s use of it -- nothing in the tree drove this arm. It surfaced
// while sabotage-proving R23 below: corrupting the fallback region left all
// 1776 tests green.
TEST(LoadRoomPersistence, NowhereSaveResolvesTheCharactersLocationToItsRoomVnum) {
    ScopedVnumWorld fixture_world;
    ScopedPlayerTable fixture_player_table{nullptr};

    char_data player{};
    make_mortal_player(player);
    ScopedClearCharFields player_cleanup{player};
    RELEASE(player.player.name);
    CREATE(player.player.name, char, strlen("nowheresavechr") + 1);
    strcpy(player.player.name, "nowheresavechr");

    descriptor_data descriptor{};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = CON_PLYNG;
    descriptor.character = &player;
    descriptor.next = nullptr;
    player.desc = &descriptor;

    player.specials2.load_room = -12345;
    char_to_room(&player, kOwnerRnum);

    save_char(&player, NOWHERE, 0);

    // The room's VNUM, not the rnum the character was standing at.
    EXPECT_EQ(GET_LOADROOM(&player), kOwnerVnum);
    EXPECT_NE(GET_LOADROOM(&player), kOwnerRnum);
    EXPECT_NE(GET_LOADROOM(&player), NOWHERE);
    EXPECT_NE(strstr(descriptor.small_outbuf, "you are not being saved"), nullptr)
        << "output: " << descriptor.small_outbuf;

    char_from_room(&player);
    RELEASE(player.player.name);
}

// ...and the fallback's OTHER outcome, which is also R23's marker. A
// character with neither an explicit load_room nor a location persists
// NOWHERE. R23 adds a second arm that would rescue exactly this case FROM THE
// VNUM CHANNEL -- but the channel and the location are still the same field,
// so when this test's character has no location it has no stash either, the
// arm cannot fire, and NOWHERE is still what reaches the field.
//
// THIS TEST IS AN LS-3b MARKER, like the two in placement_tests.cpp's channel
// suite: the moment LS-3b gives the channel its own store, a character in
// this shape WITH something stashed will start persisting the stash instead,
// and this test must be revisited rather than silently satisfied. It is
// written against a character with nothing stashed, so it states today's
// behavior exactly and fails if the fallback region is broadened (proved by
// sabotage -- see the commit message).
TEST(LoadRoomPersistence, NowhereSaveWithNoLocationAndNoStashPersistsNowhere) {
    ScopedVnumWorld fixture_world;
    ScopedPlayerTable fixture_player_table{nullptr};

    char_data player{};
    make_mortal_player(player);
    ScopedClearCharFields player_cleanup{player};
    RELEASE(player.player.name);
    CREATE(player.player.name, char, strlen("nostashchr") + 1);
    strcpy(player.player.name, "nostashchr");

    descriptor_data descriptor{};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = CON_PLYNG;
    descriptor.character = &player;
    descriptor.next = nullptr;
    player.desc = &descriptor;

    player.specials2.load_room = -12345;
    ASSERT_EQ(location_of(&player), NOWHERE);
    ASSERT_EQ(peek_load_room_vnum(&player), NOWHERE);

    save_char(&player, NOWHERE, 0);

    EXPECT_EQ(GET_LOADROOM(&player), NOWHERE);
    EXPECT_NE(GET_LOADROOM(&player), -12345);
    EXPECT_NE(strstr(descriptor.small_outbuf, "you are not being saved"), nullptr)
        << "output: " << descriptor.small_outbuf;

    RELEASE(player.player.name);
}

// AM-1's mandatory persisted-value characterization for the OTHER end of the
// VNUM channel: the rent stash (objsave.cpp:1458-1461, gen_receptionist()).
//
// THE PROTOCOL. :1458 captures save_room from in_room while it is still a
// genuine location (an RNUM). :1459 extracts the character, which clears that
// location. :1460 then re-uses the very same field as a scratch slot for the
// room's VNUM, and :1461 hands that VNUM to save_char() -- the only consumer
// the write has. AM-1's ruling is that the write and its consumer must convert
// in the SAME commit: convert only the write and save_char() would read a
// post-extract in_room (NOWHERE), and LS-3b would then persist
// load_room = NOWHERE for EVERY renting player, unobserved by any golden,
// test or gate. This test is the observation AM-1 asks for -- what value
// reaches the persisted field.
//
// OBSERVATION POINT (the EmergencySave test's pattern, immediately below):
// with an empty player_table, save_char() stamps ch->specials2.load_room with
// its load_room argument (db_players.cpp:1927) and THEN takes its "you are not
// being saved" early return (:1939-1943) -- so the stamped field is exactly
// the value that would have been persisted, and no player-file I/O runs. The
// early return is asserted, not assumed.
//
// SCOPE -- what this replays rather than drives, and why (the "say what you
// stubbed" rule). gen_receptionist() is reachable from a test (objsave_tests
// .cpp drives its occupant walk), but reaching :1458 needs the full rent
// funnel: a receptionist mob whose mob_index[].func matches, an AWAKE and
// CAN_SEE-passing pair, matching races through five racial gates, and
// Crash_offer_rent() >= 0 -- which returns -1 unless the character is
// carrying a rentable object. Past that, extract_char() aborts outright if
// its argument is not linked into the process-global character_list. The
// four statements are therefore replayed here, calling the REAL
// char_from_room()/room_by_id_total()/save_char() in the real order.
//
// RECORDED WHILE WRITING THIS TEST (finding S9's mechanism, made concrete):
// on the ORDINARY rent path :1460/:1461 are redundant -- extract_char() has
// already called save_char(ch, room_by_id_total(was_in)->number, 0) itself
// (handler.cpp:616) with the identical value. They are load-bearing only for
// a SWITCHED IMMORTAL renting, where desc->original is set and extract_char()
// takes do_return() instead (handler.cpp:613-614). A test that drove the real
// gen_receptionist() on the ordinary path could still catch a wrong value
// here -- :1461 writes last and last write wins -- but it could not attribute
// it, which is worth knowing before anyone "simplifies" these two lines away.
TEST(LoadRoomPersistence, TheRentStashChainPersistsTheRoomVnumNotItsRnum) {
    ScopedVnumWorld fixture_world;
    ScopedPlayerTable fixture_player_table{nullptr};

    char_data player{};
    make_mortal_player(player);
    ScopedClearCharFields player_cleanup{player};
    RELEASE(player.player.name);
    CREATE(player.player.name, char, strlen("rentstashchr") + 1);
    strcpy(player.player.name, "rentstashchr");

    // save_char() requires a descriptor (it logs and returns for a character
    // without one); the capturing shape lets the early-return assertion below
    // read what it sent.
    descriptor_data descriptor{};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = CON_PLYNG;
    descriptor.character = &player;
    descriptor.next = nullptr;
    player.desc = &descriptor;

    // Sentinel distinct from both the rnum and the vnum, so the assertions
    // prove the chain actually wrote the field.
    player.specials2.load_room = -12345;

    char_to_room(&player, kOwnerRnum);
    ASSERT_EQ(location_of(&player), kOwnerRnum);

    // :1458 -- a genuine location read, still an RNUM here. This line is NOT
    // a channel member and does not convert.
    const int save_room = location_of(&player);
    ASSERT_EQ(save_room, kOwnerRnum);

    // :1459 -- extract_char()'s location half. The real extract_char() also
    // unlinks from character_list, saves, and parks the descriptor at
    // CON_SLCT; only the location clearing matters to the next two lines,
    // and it is what makes them necessary.
    char_from_room(&player);
    ASSERT_EQ(location_of(&player), NOWHERE);

    // :1460 -- the stash: the room's VNUM into the now-vacant field.
    stash_load_room_vnum(&player, room_by_id_total(save_room)->number);
    // :1484 -- the only consumer of that write.
    save_char(&player, peek_load_room_vnum(&player), 0);

    // THE ASSERTION AM-1 ASKS FOR: the persisted value is the room's VNUM...
    EXPECT_EQ(GET_LOADROOM(&player), kOwnerVnum);
    // ...not the rnum the character was standing at, and not the sentinel.
    EXPECT_NE(GET_LOADROOM(&player), kOwnerRnum);
    EXPECT_NE(GET_LOADROOM(&player), -12345);
    // ...and not NOWHERE, which is what a write converted apart from its
    // consumer would have persisted for every renting player.
    EXPECT_NE(GET_LOADROOM(&player), NOWHERE);

    EXPECT_NE(strstr(descriptor.small_outbuf, "you are not being saved"), nullptr)
        << "save_char did not take the empty-player-table early return; "
        << "output: " << descriptor.small_outbuf;

    RELEASE(player.player.name);
}

// ---------------------------------------------------------------------------
// End to end -- bytes on disk through the REAL Crash_load() to occupant chains
// ---------------------------------------------------------------------------

// Closes the one seam the LoadRoomChain tests above leave open: they replay
// load_character()/Crash_load()'s statement sequence rather than calling
// them, so the ordering claim (calc_load_room runs before the follower load)
// and the follower-file half of the round trip were source reads. This test
// drives the whole load path for real: a Crash_follower_save-shaped record
// goes through the real write_player_objects_json() serializer into a real
// plrobjs/ bucket on disk (in a temp data dir, via ScopedWorkingDirectory --
// the test-scoped equivalent of ageland's `-d`), and the REAL Crash_load()
// then finds the file, deserializes it, runs the real calc_load_room, and
// places the follower; the owner is placed exactly as load_character() does
// (objsave.cpp:502). Rent header: RENT_RENTED at time(now) with
// net_cost_per_hour 0, so the rent formula charges 0 and the load proceeds
// with equipment intact (the divergent rent arms are out of scope here --
// this test pins placement, not rent accounting).
//
// Non-vacuity: proved by sabotage-and-revert -- re-introducing the pre-fix
// `location_of(ch)` read at objsave.cpp:718 makes exactly this test (and the
// two replay-based placement tests) fail with the follower stranded in
// world[35]; see the commit message for the run record.
TEST(LoadRoomEndToEnd, RealCrashLoadPlacesPersistedFollowerWithItsOwner) {
    ScopedVnumWorld fixture_world;
    ScopedStartRooms fixture_start_rooms;
    ScopedFollowerPrototype fixture_follower_prototype;

    char path_template[] = "/tmp/rots-loadroom-e2e-XXXXXX";
    char *created_path = rots_mkdtemp(path_template);
    ASSERT_NE(created_path, nullptr);
    const std::filesystem::path temp_data_dir = created_path;

    char_data player{};
    make_mortal_player(player);
    ScopedClearCharFields player_cleanup{player};
    // Starts with 'e' so player_object_bucket_path() files it under the
    // A-E bucket created below.
    RELEASE(player.player.name);
    CREATE(player.player.name, char, strlen("endtoendchr") + 1);
    strcpy(player.player.name, "endtoendchr");

    char_data *follower = nullptr;
    {
        ScopedWorkingDirectory scoped_data_dir{temp_data_dir};
        std::error_code mkdir_error;
        ASSERT_TRUE(std::filesystem::create_directories("plrobjs/A-E", mkdir_error))
            << mkdir_error.message();

        // SAVE SIDE: the record shape Crash_follower_save persists for one
        // plain charmed follower, through the real serializer, onto disk.
        objects_json::ObjectSaveData save_data = make_single_follower_save();
        save_data.rent.rentcode = RENT_RENTED;
        save_data.rent.time = static_cast<int>(time(nullptr));
        save_data.rent.net_cost_per_hour = 0;
        std::string write_error;
        ASSERT_TRUE(write_player_objects_json(GET_NAME(&player), save_data, &write_error))
            << write_error;

        // LOAD SIDE: what store_to_char leaves behind (the raw persisted
        // VNUM in in_room), then the real Crash_load(), then the owner
        // placement exactly as load_character() performs it.
        player.specials2.load_room = kOwnerVnum;
        stash_load_room_vnum(&player, GET_LOADROOM(&player));

        FILE *load_handle = Crash_load(&player);
        // Crash_load's return is only a truthy success signal (a tmpfile
        // handle); close it as load_character does. Not asserted non-null:
        // std::tmpfile() can legitimately fail (and log) on locked-down
        // hosts while the load itself succeeded.
        if (load_handle != nullptr)
            fclose(load_handle);

        char_to_room(&player, player.specials2.load_room);
        follower = (player.followers != nullptr) ? player.followers->follower : nullptr;
    }

    ASSERT_NE(follower, nullptr)
        << "the real Crash_load did not spawn the follower persisted to disk";

    // Owner and the disk-loaded follower share the resolved rnum...
    EXPECT_EQ(location_of(&player), kOwnerRnum);
    EXPECT_EQ(location_of(follower), kOwnerRnum);

    // ...confirmed by the occupant chain (follower placed first, tail
    // append puts the owner behind him), and the pre-fix wrong slot
    // (world[vnum-as-index]) stays empty.
    assert_room_chain_is(room_by_id_total(kOwnerRnum), {follower, &player});
    EXPECT_EQ(rots::entity::first_occupant(room_by_id_total(kOwnerVnum)), nullptr);

    release_spawned_follower(follower);
    RELEASE(player.player.name);
    // After ScopedWorkingDirectory restored the cwd (its scope closed
    // above), the temp data dir can be removed by absolute path.
    std::filesystem::remove_all(temp_data_dir);
}

// ---------------------------------------------------------------------------
// THE O-2 RIDER SET -- the four rnum-persisting save_char sites
// ---------------------------------------------------------------------------
//
// LS-3a is otherwise a zero-behavior-change wave. Owner ruling O-2 folds ONE
// named exception into it: the save_char() call sites that hand the persisted
// `specials2.load_room` field an RNUM, when every reader of that field (and
// the on-disk format itself) treats it as a room VNUM. The round-trip test
// above (TextRoundTripPreservesWhateverIntegerTheCallerPassed) is why that is
// a bug and not a convention: save_char()'s explicit arm applies NO
// conversion, so an rnum-passing call site silently persists an rnum, and the
// next login runs it through real_room() as though it were a vnum.
//
// T0b-1's rider table names four sites; the tests below pin all four in their
// FIXED shape. Each test states its own red-first evidence (the value the
// unfixed line produced) in its comment, and the commit message carries the
// verbatim failure output.
//
// WHY ROWS 2 AND 3 SHARE A COMMIT (recorded here because it also bounds what
// row 2 can be tested with): raw_kill()'s save (fight.cpp, row 2) is followed,
// in the same function, by extract_char()'s own save (handler.cpp, row 3),
// and BOTH stamp ch->specials2.load_room. The later write wins, so row 2's
// argument is unobservable through any end state -- no test that drives
// raw_kill() can attribute a value to it. That is why row 2 is pinned by
// replaying its own statement (the shape below), while row 3 IS driven
// through the real extract_char(). Converting one without the other would
// leave a test observing the wrong row.

// ROW 1 -- interpre.cpp:3796, the post-login save, NORMAL arm.
//
// nanny()'s CON_SLCT '1' handler saves the character immediately after
// load_character() has placed them, and load_character() places by RNUM
// (objsave.cpp:502, char_to_room(ch, calc_load_room(...))). The unfixed line
// handed that rnum straight to save_char()'s explicit arm.
//
// FIXED SHAPE: save_char(d->character, NOWHERE, 0) -- not
// room_by_id_total(location_of(...))->number. For a placed character the two
// are the same value (save_char's NOWHERE arm runs the identical room-vnum
// hook over the identical location); they diverge only for a character with
// no location, which the next test pins and which is the whole reason NOWHERE
// is the right shape.
//
// RED-FIRST: with the unfixed statement (save_char(&player,
// location_of(&player), 0)) this test failed on its first assertion, 3 (the
// RNUM) against the expected 35 (the VNUM) -- the rnum-shaped failure.
//
// SCOPE -- replayed, not driven, and why. nanny()'s '1' arm continues into
// report_news()/report_mail()/send_to_char(WELC_MESSG)/ProtocolCreate()/
// ProtocolNegotiate()/msdp_room_update()/do_look()/update_memory_list(), none
// of which this process can satisfy (WELC_MESSG is a boot-loaded global text
// buffer; the protocol negotiation writes a live socket). The character state
// this test builds IS produced by production, though: the persisted VNUM is
// stashed by the same replay_load_character_guard() the chain tests use, and
// the placement rnum comes from the REAL calc_load_room().
TEST(LoadRoomRider, PostLoginSavePersistsTheLoginRoomVnumNotItsRnum) {
    ScopedVnumWorld fixture_world;
    ScopedStartRooms fixture_start_rooms;
    ScopedPlayerTable fixture_player_table{nullptr};

    char_data player{};
    make_mortal_player(player);
    ScopedClearCharFields player_cleanup{player};
    RELEASE(player.player.name);
    CREATE(player.player.name, char, strlen("postloginchr") + 1);
    strcpy(player.player.name, "postloginchr");

    descriptor_data descriptor{};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = CON_PLYNG;
    descriptor.character = &player;
    descriptor.next = nullptr;
    player.desc = &descriptor;

    // The state load_character() leaves behind at the moment nanny() saves:
    // the raw persisted VNUM in the channel, resolved to an RNUM by the real
    // calc_load_room(), and the character placed at that rnum.
    player.specials2.load_room = kOwnerVnum;
    replay_load_character_guard(player);
    player.specials2.load_room = calc_load_room(&player, RENT_RENTED);
    ASSERT_EQ(player.specials2.load_room, kOwnerRnum);
    char_to_room(&player, player.specials2.load_room);
    ASSERT_EQ(location_of(&player), kOwnerRnum);
    // ...and load_character's tail, which retires the channel (LS-3b T9b).
    // Outcome-neutral HERE -- this character is placed, so save_char's FIRST
    // fallback arm decides -- and included so the replayed sequence stays
    // faithful to production.
    stash_load_room_vnum(&player, NOWHERE);

    // Sentinel distinct from both the rnum and the vnum, so the assertions
    // prove save_char() actually wrote the field.
    player.specials2.load_room = -12345;

    // interpre.cpp:3796's own statement, in its fixed shape.
    save_char(&player, NOWHERE, 0);

    EXPECT_EQ(GET_LOADROOM(&player), kOwnerVnum);
    EXPECT_NE(GET_LOADROOM(&player), kOwnerRnum);
    EXPECT_NE(GET_LOADROOM(&player), -12345);
    EXPECT_NE(strstr(descriptor.small_outbuf, "you are not being saved"), nullptr)
        << "save_char did not take the empty-player-table early return; "
        << "output: " << descriptor.small_outbuf;

    char_from_room(&player);
    RELEASE(player.player.name);
}

// ROW 1, post-m-14: the bugged arm can no longer return -1. When
// real_room(r_bugged_start_room) misses, calc_load_room() clamps to the
// racial start room, so the character is genuinely PLACED.
//
// CORRECTED FROM THE ORIGINAL DESIGN PREDICTION (found red-first, verified
// empirically): the persisted value does NOT stay NOWHERE. Once the
// character is genuinely placed, save_char()'s FIRST arm --
// `(load_room == NOWHERE) && (location_of(ch) != NOWHERE)` (db_players.cpp)
// -- fires and converts the placement to its own vnum, ahead of the
// channel-based R23 arm the pre-flip test exercised. There is no more torn
// state (NOWHERE-persisted vs. actually-placed) for save_char to reconcile;
// placement and persistence now agree on the same room. See the commit
// message for the sabotage-probe evidence.
//
// DISCRIMINATION (spec review O-2): the racial start room is overridden to
// rnum 1 because the fixture default (rnum 0) is the same room the pre-fix
// room-0 fallback resolves to -- assertions against rnum 0 cannot tell the
// clamp from the bug. RED-FIRST: against the unclamped arm, computed here is
// -1 and location_of() stays NOWHERE, so the first two assertions fail.
TEST(LoadRoomRider, PostLoginSaveLandsABuggedCharacterInTheRacialStartRoom) {
    ScopedVnumWorld fixture_world;
    ScopedStartRooms fixture_start_rooms;
    ScopedPlayerTable fixture_player_table{nullptr};

    // The discrimination override: a start room distinct from rnum 0.
    constexpr int kDistinctStartRnum = 1;
    for (int race = 0; race < MAX_RACES; ++race)
        r_mortal_start_room[race] = kDistinctStartRnum;

    char_data player{};
    make_mortal_player(player);
    ScopedClearCharFields player_cleanup{player};
    RELEASE(player.player.name);
    CREATE(player.player.name, char, strlen("buggedchr") + 1);
    strcpy(player.player.name, "buggedchr");

    descriptor_data descriptor{};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = CON_PLYNG;
    descriptor.character = &player;
    descriptor.next = nullptr;
    player.desc = &descriptor;

    // A vnum no room in this fixture carries, so real_room() misses and the
    // bugged arm takes its NEW fallback.
    r_bugged_start_room = 999999;
    player.abilities.str = 0;
    player.tmpabilities.str = 0;

    player.specials2.load_room = kOwnerVnum;
    replay_load_character_guard(player);
    const int computed_load_room = calc_load_room(&player, RENT_RENTED);
    ASSERT_EQ(computed_load_room, kDistinctStartRnum)
        << "the clamped bugged arm must fall back to the racial start room";

    char_to_room(&player, computed_load_room);
    ASSERT_EQ(location_of(&player), kDistinctStartRnum);
    ASSERT_EQ(rots::entity::first_occupant(room_by_id_total(kDistinctStartRnum)), &player)
        << "a placed character must be linked into the start room's chain";
    ASSERT_EQ(rots::entity::first_occupant(room_by_id_total(0)), nullptr)
        << "...and NOT into room 0 (the pre-fix fallback room)";
    stash_load_room_vnum(&player, NOWHERE);

    player.specials2.load_room = -12345;
    save_char(&player, NOWHERE, 0);

    // CORRECTED FROM THE BRIEF'S ORIGINAL PREDICTION (found red-first,
    // verified empirically against the real save_char()): now that the
    // character is genuinely PLACED, save_char()'s FIRST arm --
    // `(load_room == NOWHERE) && (location_of(ch) != NOWHERE)`
    // (db_players.cpp) -- fires ahead of the channel-based R23 arm and
    // converts the placement to its vnum, so the persisted value is
    // room_vnum_for(kDistinctStartRnum), not NOWHERE. There is no more torn
    // state to reconcile: placement and persistence now agree, which is
    // itself the fix's point -- see the commit message for the sabotage
    // evidence that this arm, not the channel, decides the outcome here.
    EXPECT_EQ(GET_LOADROOM(&player), room_vnum_for(kDistinctStartRnum));
    EXPECT_NE(GET_LOADROOM(&player), kOwnerVnum);
    EXPECT_NE(GET_LOADROOM(&player), -12345);
    EXPECT_NE(GET_LOADROOM(&player), room_vnum_for(0));

    EXPECT_EQ(room_by_id_total(location_of(&player))->number,
              room_vnum_for(kDistinctStartRnum));

    EXPECT_NE(strstr(descriptor.small_outbuf, "you are not being saved"), nullptr)
        << "output: " << descriptor.small_outbuf;

    // LOAD-BEARING AGAIN (spec review O-2, the DoRescue/waiting_list class):
    // the character above is a stack object spliced into a process-global
    // chain. Unlink from the room it actually landed in, or the pointer
    // outlives this frame -- ctest cannot see that; the monolithic runner
    // and the i386 battery can.
    unlink_from_occupant_chain(*room_by_id_total(kDistinctStartRnum), &player);
    RELEASE(player.player.name);
}

// The positive control (spec 2.3): a RESOLVABLE bugged room is still used --
// the clamp must not overshoot into always-racial-start. Vacuity-proofed
// (the O-I3 class): the bugged room (rnum 2) is distinct from BOTH the
// overridden racial start (rnum 1) and room 0, so this fails if the clamp
// ignores a resolvable bugged room, and fails differently if the arm still
// returns -1.
TEST(LoadRoomRider, ResolvableBuggedRoomIsStillUsed) {
    ScopedVnumWorld fixture_world;
    ScopedStartRooms fixture_start_rooms;
    ScopedPlayerTable fixture_player_table{nullptr};

    for (int race = 0; race < MAX_RACES; ++race)
        r_mortal_start_room[race] = 1;
    r_bugged_start_room = room_vnum_for(2); // vnum 25 -> rnum 2, resolvable

    char_data player{};
    make_mortal_player(player);
    ScopedClearCharFields player_cleanup{player};

    // Below the bugged-character floor (str >= 1).
    player.abilities.str = 0;
    player.tmpabilities.str = 0;
    player.specials2.load_room = kOwnerVnum;
    replay_load_character_guard(player);

    EXPECT_EQ(calc_load_room(&player, RENT_RENTED), 2)
        << "a resolvable bugged room must win over the racial-start fallback";
}

// ROW 2 -- fight.cpp:948, raw_kill()'s death save.
//
// The unfixed line passed r_mortal_start_room[GET_RACE(dead_man)] -- an RNUM
// (db_world.cpp:814 computes it with real_room()) -- straight into
// save_char()'s explicit arm.
//
// FIXED SHAPE: room_by_id_total(r_mortal_start_room[race])->number. NOT
// mortal_start_room[race], the sibling VNUM array: db_world.cpp:814-815 CLAMPS
// r_mortal_start_room[tmp] to 0 when real_room(mortal_start_room[tmp]) misses,
// so after a boot with a missing start room the two arrays name different
// rooms and only the resolver-derived expression names the room the rest of
// raw_kill() actually sends the corpse's owner to. That divergence is
// reproduced below rather than argued.
//
// RED-FIRST: with the unfixed argument this test failed with 0 (the rnum)
// against the expected 5 (the vnum).
//
// SCOPE -- replayed, not driven, and why (the "say what you stubbed" rule).
// See the section header: extract_char()'s own save, eleven lines later in
// the same function, overwrites this stamp unconditionally, so raw_kill()
// cannot be driven to observe THIS line's argument. What the replay does not
// cover: that raw_kill() reaches the line at all for a given death (it is
// inside `if (!IS_NPC(dead_man))`), and the surrounding corpse/affect
// teardown. Neither touches load_room.
TEST(LoadRoomRider, DeathSavePersistsTheRacialStartRoomVnumNotItsRnum) {
    ScopedVnumWorld fixture_world;
    // r_mortal_start_room[every race] = rnum 0, whose room carries vnum 5.
    ScopedStartRooms fixture_start_rooms;
    ScopedPlayerTable fixture_player_table{nullptr};

    char_data player{};
    make_mortal_player(player);
    ScopedClearCharFields player_cleanup{player};
    RELEASE(player.player.name);
    CREATE(player.player.name, char, strlen("deathsavechr") + 1);
    strcpy(player.player.name, "deathsavechr");

    descriptor_data descriptor{};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = CON_PLYNG;
    descriptor.character = &player;
    descriptor.next = nullptr;
    player.desc = &descriptor;

    const int race = GET_RACE(&player);
    ASSERT_EQ(r_mortal_start_room[race], ScopedStartRooms::kRacialStartRnum);

    // The clamp hazard, made concrete: point the VNUM array at a room that is
    // NOT the one r_mortal_start_room[] indexes, exactly as a boot-time
    // real_room() miss would leave things (db_world.cpp:815 forces the rnum to
    // 0 while the vnum array keeps whatever the config named).
    const int previous_mortal_start_room = mortal_start_room[race];
    mortal_start_room[race] = kOwnerVnum;

    player.specials2.load_room = -12345;

    // fight.cpp:948's own statement, in its fixed shape.
    save_char(&player, room_by_id_total(r_mortal_start_room[race])->number, 0);

    // The start room's VNUM...
    EXPECT_EQ(GET_LOADROOM(&player), room_vnum_for(ScopedStartRooms::kRacialStartRnum));
    // ...not its rnum (what the unfixed line passed)...
    EXPECT_NE(GET_LOADROOM(&player), ScopedStartRooms::kRacialStartRnum);
    // ...and not the sibling VNUM array's entry, which the clamp has made
    // disagree.
    EXPECT_NE(GET_LOADROOM(&player), mortal_start_room[race]);
    EXPECT_NE(GET_LOADROOM(&player), -12345);
    EXPECT_NE(strstr(descriptor.small_outbuf, "you are not being saved"), nullptr)
        << "output: " << descriptor.small_outbuf;

    mortal_start_room[race] = previous_mortal_start_room;
    RELEASE(player.player.name);
}

// ROW 3 -- handler.cpp:616, extract_char()'s own save. DRIVEN, not replayed.
//
// The ternary's `new_room < 0` arm already resolved its rnum to a vnum
// (room_by_id_total(was_in)->number); the `>= 0` arm passed new_room raw. Its
// two production callers are fight.cpp:981/:984 (raw_kill's respawn), which
// pass r_mortal_start_room[race] / r_immort_start_room -- RNUMs both.
//
// FIXED SHAPE: room_by_id_total(new_room)->number, symmetric with the arm
// beside it.
//
// RED-FIRST: this test failed with 1 (the destination RNUM) against the
// expected 15 (its VNUM).
//
// The three distinct numbers this fixture uses are deliberate: the character
// dies in rnum 3 / vnum 35, is re-placed at rnum 1 / vnum 15, and the field
// starts at a -12345 sentinel. extract_char() itself pre-stamps load_room with
// the ORIGIN room's vnum (handler.cpp:573) before saving, so an assertion that
// only ruled out the sentinel would pass on the origin room's value.
TEST(LoadRoomRider, ExtractCharToARoomPersistsTheDestinationRoomVnumNotItsRnum) {
    ScopedVnumWorld fixture_world;
    ScopedPlayerTable fixture_player_table{nullptr};
    ScopedGlobalCharacterLists fixture_lists;

    char_data player{};
    make_mortal_player(player);
    ScopedClearCharFields player_cleanup{player};
    RELEASE(player.player.name);
    CREATE(player.player.name, char, strlen("extractchr") + 1);
    strcpy(player.player.name, "extractchr");

    descriptor_data descriptor{};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = CON_PLYNG;
    descriptor.character = &player;
    descriptor.next = nullptr;
    // Non-zero: extract_char() reads ch->desc->descriptor as "is there still a
    // socket behind this character?" (handler.cpp:592/:635). A zero here would
    // send the character down the close_socket() arm instead of the
    // re-placement arm this test is about. Never written to -- no I/O runs.
    descriptor.descriptor = 1;
    // NOT a switched immortal: with desc->original set, extract_char() takes
    // do_return() and never reaches the save at all (handler.cpp:613-614).
    descriptor.original = nullptr;
    player.desc = &descriptor;

    char_to_room(&player, kOwnerRnum);
    ASSERT_EQ(location_of(&player), kOwnerRnum);

    player.specials2.load_room = -12345;

    constexpr int kRespawnRnum = 1;
    extract_char(&player, kRespawnRnum);

    // The destination room's VNUM...
    EXPECT_EQ(GET_LOADROOM(&player), room_vnum_for(kRespawnRnum));
    // ...not its rnum (what the unfixed arm passed)...
    EXPECT_NE(GET_LOADROOM(&player), kRespawnRnum);
    // ...and not the origin room's vnum, which handler.cpp:573 pre-stamped.
    EXPECT_NE(GET_LOADROOM(&player), kOwnerVnum);
    EXPECT_NE(GET_LOADROOM(&player), -12345);

    // ...and the character really was re-placed by the arm under test.
    EXPECT_EQ(location_of(&player), kRespawnRnum);

    char_from_room(&player);
    RELEASE(player.player.name);
}

// ROW 3's SIBLING ARM -- the regression guard. `new_room < 0` already
// persisted a vnum before this tranche; this pins that it still does, so the
// symmetric edit above cannot be "simplified" into a single raw-rnum
// expression later. Driven through the same real extract_char().
//
// This arm also unlinks the character from character_list and parks the
// descriptor at the character menu (handler.cpp:596-608, :645-647), which is
// why it needs the character ON that list first: extract_char() abort()s
// outright when it cannot find its argument there.
//
// ls3b T7: the "no destination" argument below now spells
// handler.h's kNoRespawnRoom, not NOWHERE -- a pure spelling fix (R-C4):
// the two were numerically identical but semantically unrelated, and this
// call site was the one place in the tree still conflating them.
TEST(LoadRoomRider, ExtractCharWithoutADestinationStillPersistsTheOriginRoomVnum) {
    ScopedVnumWorld fixture_world;
    ScopedPlayerTable fixture_player_table{nullptr};
    ScopedGlobalCharacterLists fixture_lists;

    char_data player{};
    make_mortal_player(player);
    ScopedClearCharFields player_cleanup{player};
    RELEASE(player.player.name);
    CREATE(player.player.name, char, strlen("extractnochr") + 1);
    strcpy(player.player.name, "extractnochr");

    descriptor_data descriptor{};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = CON_PLYNG;
    descriptor.character = &player;
    descriptor.next = nullptr;
    descriptor.descriptor = 1;
    descriptor.original = nullptr;
    player.desc = &descriptor;

    player.next = nullptr;
    character_list = &player;

    char_to_room(&player, kOwnerRnum);
    player.specials2.load_room = -12345;

    extract_char(&player, kNoRespawnRoom);

    // The room they were standing in, as a VNUM.
    EXPECT_EQ(GET_LOADROOM(&player), kOwnerVnum);
    EXPECT_NE(GET_LOADROOM(&player), kOwnerRnum);
    EXPECT_NE(GET_LOADROOM(&player), -12345);
    // ...and they are no longer anywhere.
    EXPECT_EQ(location_of(&player), NOWHERE);

    RELEASE(player.player.name);
}


// ROW 4(a) -- act_wiz.cpp:3253, the `wizset file` save. DRIVEN end to end:
// the real do_wizset() is-file funnel, over a real player file on disk in a
// test-scoped data directory, with the value read back through the real
// load_char().
//
// WHAT THE SITE IS (T0b-1 called the census's classification of it the
// loudest misclassification in the set). is_file forces vict == cbuf: a
// character just materialised by store_to_char(), who is in no room and on no
// list, and whose location field therefore holds the RAW PERSISTED VNUM, not
// an rnum. The line is a correct pass-through of that vnum -- it only READS
// like a location because the channel and the location are the same field
// today. The mechanical conversion the other three rows took,
// room_by_id_total(location_of(vict))->number, would CORRUPT every one of
// these saves: in this fixture that expression resolves world[35], a
// create_bulk() filler room whose number is -1.
//
// FIXED SHAPE: peek_load_room_vnum(vict) -- byte-identical today (same field),
// but it names the channel, so LS-3b re-points it with the rest of the
// channel instead of silently persisting a location-store miss.
//
// COVERAGE GAP, closed here: nothing in the tree drove `wizset file` past its
// early guards (act_wiz_format_tests.cpp reaches only the usage/NPC/lookup
// rejections), so neither this save nor the file round trip behind it had any
// coverage at all. The field this test sets ("brief") is deliberately
// unrelated to rooms -- what is under test is what the save persists for an
// offline character, not what the field did.
TEST(LoadRoomRider, WizsetFileSavePersistsTheChannelVnumNotAResolvedRoomNumber) {
    ScopedVnumWorld fixture_world;
    ScopedGlobalCharacterLists fixture_lists;

    char path_template[] = "/tmp/rots-loadroom-wizset-XXXXXX";
    char *created_path = rots_mkdtemp(path_template);
    ASSERT_NE(created_path, nullptr);
    const std::filesystem::path temp_data_dir = created_path;

    {
        ScopedWorkingDirectory scoped_data_dir{temp_data_dir};
        std::error_code mkdir_error;
        // save_player() (db_players.cpp:1848) buckets by first letter and
        // stages through players/temp, so both directories must exist.
        ASSERT_TRUE(std::filesystem::create_directories("players/U-Z", mkdir_error))
            << mkdir_error.message();

        // A real, loadable player file whose persisted load_room is the room
        // VNUM 35 -- the value store_to_char() will deposit in the channel.
        {
            char_data writer{};
            make_mortal_player(writer);
            ScopedClearCharFields writer_cleanup{writer};
            descriptor_data writer_descriptor{};
            snprintf(writer_descriptor.pwd, sizeof(writer_descriptor.pwd), "%s", "WizSetPw");
            snprintf(writer_descriptor.host, sizeof(writer_descriptor.host), "%s", "wizset.test");
            writer.desc = &writer_descriptor;
            RELEASE(writer.player.name);
            CREATE(writer.player.name, char, strlen("wizsetchr") + 1);
            strcpy(writer.player.name, "wizsetchr");
            ASSERT_TRUE(write_player_text(&writer, kOwnerVnum, "players/U-Z/wizsetchr"));
            RELEASE(writer.player.title);
            RELEASE(writer.player.description);
            RELEASE(writer.player.name);
        }

        // load_char() resolves the name through player_table's ch_file, and
        // save_player() rewrites that same entry with the versioned name it
        // finalizes -- so the read-back below sees whatever the save wrote.
        ScopedPlayerTable fixture_player_table{"wizsetchr"};
        strcpy(player_table[0].ch_file, "players/U-Z/wizsetchr");

        char_data immortal{};
        make_mortal_player(immortal);
        ScopedClearCharFields immortal_cleanup{immortal};
        immortal.player.level = LEVEL_IMPL;
        RELEASE(immortal.player.name);
        CREATE(immortal.player.name, char, strlen("wizsetimm") + 1);
        strcpy(immortal.player.name, "wizsetimm");

        descriptor_data immortal_descriptor{};
        immortal_descriptor.output = immortal_descriptor.small_outbuf;
        immortal_descriptor.small_outbuf[0] = '\0';
        immortal_descriptor.bufptr = 0;
        immortal_descriptor.bufspace = SMALL_BUFSIZE - 1;
        immortal_descriptor.connected = CON_PLYNG;
        immortal_descriptor.character = &immortal;
        immortal_descriptor.next = nullptr;
        immortal.desc = &immortal_descriptor;
        char_to_room(&immortal, kOwnerRnum);

        char argument[] = "file wizsetchr brief on";
        do_wizset(&immortal, argument, nullptr, 0, 0);

        ASSERT_NE(strstr(immortal_descriptor.small_outbuf, "Saved in file."), nullptr)
            << "do_wizset did not reach its is_file save; output: "
            << immortal_descriptor.small_outbuf;

        // THE ASSERTION: the persisted value is the VNUM the offline character
        // carried in the channel, passed through verbatim.
        char reload_name[] = "wizsetchr";
        char_file_u reloaded{};
        ASSERT_GE(load_char(reload_name, &reloaded), 0)
            << "the saved player file could not be re-read";
        EXPECT_EQ(reloaded.specials2.load_room, kOwnerVnum);
        // ...not what a mechanical location resolve would have produced. The
        // filler room at index 35 carries dummy_room_data()'s number, -1, so
        // that conversion is observably distinct from a correct pass-through.
        EXPECT_NE(reloaded.specials2.load_room, room_by_id_total(kOwnerVnum)->number);
        EXPECT_NE(reloaded.specials2.load_room, NOWHERE);

        // ...and the offline character was never placed anywhere: no room's
        // occupant chain gained him, and nothing indexed by his raw VNUM did
        // either.
        assert_room_chain_is(room_by_id_total(kOwnerRnum), {&immortal});
        EXPECT_EQ(rots::entity::first_occupant(room_by_id_total(kOwnerVnum)), nullptr);

        char_from_room(&immortal);
        RELEASE(immortal.player.name);
    }

    std::filesystem::remove_all(temp_data_dir);
}

// THE LOAD-FAILURE ARM of the same funnel (LS-3a follow-up). It lives here
// rather than in act_wiz_format_tests.cpp because this file owns the only
// harness in the tree that reaches `wizset file` past its early guards at all.
//
// WHAT IT PINS: do_wizset()'s is_file block materialises a scratch character
// BEFORE it knows whether the player file loads, so the failure arm has to
// release it. That arm used to `RELEASE(cbuf)` -- struct shell only, leaking
// the profs allocation and the skills/knowledge vectors clear_char(MOB_VOID)
// had made -- and is now a char_data_ptr released on every exit path, the same
// shape `stat file` (act_wiz.cpp:1198) took for the identical defect.
//
// WHAT IT CANNOT PIN: the leak itself. LeakSanitizer does not run on macOS
// arm64 (the host this was developed on), so there is no assertion available
// here that goes red on a leak; the pre-fix code passes this test. It pins the
// arm's OBSERVABLE behavior -- the message, the early return, and that the
// release path runs clean under ASan -- and the leak fix rides on the code
// read plus the sanitize-linux CI leg, which does have LSan.
TEST(LoadRoomRider, WizsetFileReportsNoSuchPlayerAndReleasesTheScratchCharacter) {
    ScopedVnumWorld fixture_world;
    ScopedGlobalCharacterLists fixture_lists;
    // A player table holding SOMEBODY, so the miss below is a genuine
    // name-lookup failure rather than an empty-table shortcut.
    ScopedPlayerTable fixture_player_table{"wizsetchr"};

    char_data immortal{};
    make_mortal_player(immortal);
    ScopedClearCharFields immortal_cleanup{immortal};
    immortal.player.level = LEVEL_IMPL;
    RELEASE(immortal.player.name);
    CREATE(immortal.player.name, char, strlen("wizsetimm") + 1);
    strcpy(immortal.player.name, "wizsetimm");

    descriptor_data immortal_descriptor{};
    immortal_descriptor.output = immortal_descriptor.small_outbuf;
    immortal_descriptor.small_outbuf[0] = '\0';
    immortal_descriptor.bufptr = 0;
    immortal_descriptor.bufspace = SMALL_BUFSIZE - 1;
    immortal_descriptor.connected = CON_PLYNG;
    immortal_descriptor.character = &immortal;
    immortal_descriptor.next = nullptr;
    immortal.desc = &immortal_descriptor;
    char_to_room(&immortal, kOwnerRnum);

    char argument[] = "file ghostwizsetname brief on";
    do_wizset(&immortal, argument, nullptr, 0, 0);

    EXPECT_NE(strstr(immortal_descriptor.small_outbuf, "There is no such player."), nullptr)
        << "Expected do_wizset's is_file load-failure arm; output: "
        << immortal_descriptor.small_outbuf;
    // The arm returns immediately -- it must not fall through to the field
    // edit or the save.
    EXPECT_EQ(strstr(immortal_descriptor.small_outbuf, "Saved in file."), nullptr)
        << immortal_descriptor.small_outbuf;

    char_from_room(&immortal);
    RELEASE(immortal.player.name);
}

// ROW 4(b) -- and the reason it cannot be driven. CHARACTERIZATION of a
// SHADOWED FIELD, found while writing the test this replaces.
//
// do_wizset()'s field lookup is a PREFIX match over its table in declaration
// order (act_wiz.cpp:2897-2899, `strncmp(field, fields[l].cmd, strlen(field))`
// with a break on the first hit). Entry 35 is "roomflag" and entry 36 is
// "room" -- so every spelling of "room" the parser can be handed matches
// "roomflag" FIRST, and case 36 is unreachable through the only command that
// indexes this table (`fields[]` at act_wiz.cpp:2740 has exactly one consumer,
// controller-verified by grep). This test pins that: `wizset <victim> room on`
// toggles the victim's PRF_ROOMFLAGS and does NOT move him.
//
// CONSEQUENCE FOR THE RIDER, stated plainly rather than papered over: rider
// row 4(b)'s is_file branch (stash the typed VNUM, skip char_from_room/
// char_to_room) IS applied in this commit -- it removes a real defect class
// from the tree (an offline character spliced into a live room's occupant
// chain and then free_char()'d eleven lines later, plus R16's wrong-room light
// decrement) and it removes the app tier's last raw placement call on a VNUM
// -- but it is UNTESTABLE behavior, because no input reaches it. It is not
// covered by a test that pretends otherwise. If the shadowing is ever fixed
// (renaming entry 36, or reordering the table), THIS test fails first and
// says so, which is the point.
TEST(LoadRoomRider, WizsetRoomFieldIsShadowedByRoomflagSoTheRoomArmIsUnreachable) {
    ScopedVnumWorld fixture_world;
    ScopedGlobalCharacterLists fixture_lists;
    ScopedPlayerTable fixture_player_table{"wizsetvict"};

    char_data immortal{};
    make_mortal_player(immortal);
    ScopedClearCharFields immortal_cleanup{immortal};
    immortal.player.level = LEVEL_IMPL;
    RELEASE(immortal.player.name);
    CREATE(immortal.player.name, char, strlen("wizsetimm2") + 1);
    strcpy(immortal.player.name, "wizsetimm2");

    descriptor_data immortal_descriptor{};
    immortal_descriptor.output = immortal_descriptor.small_outbuf;
    immortal_descriptor.small_outbuf[0] = '\0';
    immortal_descriptor.bufptr = 0;
    immortal_descriptor.bufspace = SMALL_BUFSIZE - 1;
    immortal_descriptor.connected = CON_PLYNG;
    immortal_descriptor.character = &immortal;
    immortal_descriptor.next = nullptr;
    immortal.desc = &immortal_descriptor;

    char_data victim{};
    make_mortal_player(victim);
    ScopedClearCharFields victim_cleanup{victim};
    RELEASE(victim.player.name);
    CREATE(victim.player.name, char, strlen("wizsetvict") + 1);
    strcpy(victim.player.name, "wizsetvict");

    descriptor_data victim_descriptor{};
    victim_descriptor.output = victim_descriptor.small_outbuf;
    victim_descriptor.small_outbuf[0] = '\0';
    victim_descriptor.bufptr = 0;
    victim_descriptor.bufspace = SMALL_BUFSIZE - 1;
    victim_descriptor.connected = CON_PLYNG;
    victim_descriptor.character = &victim;
    victim_descriptor.next = nullptr;
    victim.desc = &victim_descriptor;

    // get_char_vis() walks character_list; both must be on it, and in the same
    // room so the immortal can see the victim.
    immortal.next = &victim;
    victim.next = nullptr;
    character_list = &immortal;

    char_to_room(&immortal, kOwnerRnum);
    char_to_room(&victim, kOwnerRnum);
    ASSERT_FALSE(PRF_FLAGGED(&victim, PRF_ROOMFLAGS));

    char argument[] = "wizsetvict room on";
    do_wizset(&immortal, argument, nullptr, 0, 0);

    // "room" selected entry 35, "roomflag" -- the reply names it (CAP()'d by
    // do_wizset's own BINARY-field reply path, hence the leading capital)...
    EXPECT_NE(strstr(immortal_descriptor.small_outbuf, "Roomflag ON"), nullptr)
        << "output: " << immortal_descriptor.small_outbuf;
    // ...it really toggled that preference...
    EXPECT_TRUE(PRF_FLAGGED(&victim, PRF_ROOMFLAGS));
    // ...and case 36 never ran: the victim did not move.
    EXPECT_EQ(location_of(&victim), kOwnerRnum);
    EXPECT_EQ(rots::entity::first_occupant(room_by_id_total(1)), nullptr);

    char_from_room(&victim);
    char_from_room(&immortal);
    RELEASE(immortal.player.name);
    RELEASE(victim.player.name);
}

// ---------------------------------------------------------------------------
// THE MID-WINDOW GUARDS (LS-3a T2 tranche 2e-beta, T0b-1 readers R9/R10 and
// R20/R21)
// ---------------------------------------------------------------------------
//
// Three production sites read a character's location during the login/rent
// window -- the stretch where char_data::in_room carries a persisted room
// VNUM rather than a world[] index, or nothing at all. Two of them (the
// equip_char zap guards) would BREAK when LS-3b makes absence the honest
// answer there; two of them (the async walkers) are wrong TODAY and produce a
// mudlog per pulse per menu-sitter while being so.
//
// The equip_char pair is a provable no-op at this commit and is pinned by
// characterization: what these tests assert is exactly what the tree did
// before the guard changed, so the guard cannot have altered it. The two
// walkers DO change behavior, for characters with no location only, and each
// is pinned by an assertion that fails when its guard is removed (recorded in
// the commit message).

// R9/R10 -- equip_char()'s anti-alignment zap. CHARACTERIZATION: a forbidden
// item is dropped into the wearer's INVENTORY, never left equipped, and the
// wearer and the room are both told. The guard around that behavior gained a
// second term this commit ((location_of(ch) != NOWHERE) ||
// (peek_load_room_vnum(ch) != NOWHERE)); today the two terms are the same
// expression over the same field, so this pins the same behavior before and
// after -- which is the claim the no-op derivation makes, stated as a test
// rather than only as a comment.
TEST(EquipCharZapGuard, AntiEvilItemLandsInInventoryNotOnAnEvilWearer) {
    ScopedVnumWorld fixture_world;
    ScopedGlobalCharacterLists fixture_lists;

    char_data wearer{};
    make_mortal_player(wearer);
    ScopedClearCharFields wearer_cleanup{wearer};
    GET_ALIGNMENT(&wearer) = -1000; // IS_EVIL: alignment <= -100

    descriptor_data descriptor{};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = CON_PLYNG;
    descriptor.character = &wearer;
    descriptor.next = nullptr;
    wearer.desc = &descriptor;
    wearer.specials.position = POSITION_STANDING;

    char_to_room(&wearer, kOwnerRnum);

    obj_data holy_symbol{};
    holy_symbol.obj_flags.type_flag = ITEM_ARMOR;
    holy_symbol.obj_flags.extra_flags = ITEM_ANTI_EVIL;
    holy_symbol.obj_flags.weight = 1;
    holy_symbol.in_room = NOWHERE; // LS1-ALLOW: obj-location (fixture init)
    holy_symbol.name = str_dup("symbol");
    holy_symbol.short_description = str_dup("a holy symbol");
    holy_symbol.description = str_dup("A holy symbol lies here.");

    equip_char(&wearer, &holy_symbol, WEAR_BODY);

    // Zapped: not worn...
    EXPECT_EQ(wearer.equipment[WEAR_BODY], nullptr);
    // ...but carried, which is what the comment beside obj_to_char() promises
    // ("changed to drop in inventory instead of ground").
    EXPECT_EQ(wearer.carrying, &holy_symbol);
    EXPECT_EQ(holy_symbol.carried_by, &wearer);
    EXPECT_EQ(holy_symbol.in_room, NOWHERE); // LS1-ALLOW: obj-location
    // ...and the wearer was told.
    EXPECT_NE(strstr(descriptor.small_outbuf, "You are zapped by"), nullptr)
        << "output: " << descriptor.small_outbuf;

    // Outcome-independent teardown: whichever of the two homes the item ended
    // up in is undone, so an assertion failure above reports as a failure
    // rather than as a crash in this cleanup.
    if (holy_symbol.carried_by != nullptr)
        obj_from_char(&holy_symbol);
    if (wearer.equipment[WEAR_BODY] == &holy_symbol)
        unequip_char(&wearer, WEAR_BODY);
    RELEASE(holy_symbol.name);
    RELEASE(holy_symbol.short_description);
    RELEASE(holy_symbol.description);
    char_from_room(&wearer);
}

// The CONTROL: the same item on a wearer the restriction does not apply to is
// equipped normally. Without this, the test above would pass just as well
// against an equip_char() that refused to equip anything at all.
TEST(EquipCharZapGuard, TheSameItemIsWornNormallyByANonEvilWearer) {
    ScopedVnumWorld fixture_world;
    ScopedGlobalCharacterLists fixture_lists;

    char_data wearer{};
    make_mortal_player(wearer);
    ScopedClearCharFields wearer_cleanup{wearer};
    GET_ALIGNMENT(&wearer) = 0; // IS_NEUTRAL, and the item is ANTI_EVIL only

    descriptor_data descriptor{};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = CON_PLYNG;
    descriptor.character = &wearer;
    descriptor.next = nullptr;
    wearer.desc = &descriptor;
    wearer.specials.position = POSITION_STANDING;

    char_to_room(&wearer, kOwnerRnum);

    obj_data holy_symbol{};
    holy_symbol.obj_flags.type_flag = ITEM_ARMOR;
    holy_symbol.obj_flags.extra_flags = ITEM_ANTI_EVIL;
    holy_symbol.obj_flags.weight = 1;
    holy_symbol.in_room = NOWHERE; // LS1-ALLOW: obj-location (fixture init)
    holy_symbol.name = str_dup("symbol");
    holy_symbol.short_description = str_dup("a holy symbol");
    holy_symbol.description = str_dup("A holy symbol lies here.");

    equip_char(&wearer, &holy_symbol, WEAR_BODY);

    EXPECT_EQ(wearer.equipment[WEAR_BODY], &holy_symbol);
    EXPECT_EQ(wearer.carrying, nullptr);
    EXPECT_EQ(strstr(descriptor.small_outbuf, "You are zapped by"), nullptr)
        << "output: " << descriptor.small_outbuf;

    // Outcome-independent teardown, as above.
    if (wearer.equipment[WEAR_BODY] == &holy_symbol)
        unequip_char(&wearer, WEAR_BODY);
    if (holy_symbol.carried_by != nullptr)
        obj_from_char(&holy_symbol);
    RELEASE(holy_symbol.name);
    RELEASE(holy_symbol.short_description);
    RELEASE(holy_symbol.description);
    char_from_room(&wearer);
}

// ===========================================================================
// R9/R10 ARMED (LS-3b T5 -- the store split; census B S5's "can only be
// witnessed in the split commit" rule).
//
// equip_char()'s two zap arms (fight.cpp) guard on
//     (location_of(character) != NOWHERE) || (peek_load_room_vnum(character) != NOWHERE)
// LS-3a wrote that second term as a deliberate no-op -- the two functions read
// one field, so `A != X || A != X` reduced to one term -- and said in the
// source that it would arm itself the instant LS-3b gave the channel its own
// store. It has. These two tests are the discriminators that could not be
// written before this commit, one per arm.
//
// THE SHAPE UNDER TEST is the real one: mid-Crash_load, the character has no
// location at all (objsave.cpp equips every worn item before char_to_room()
// runs) and the persisted room VNUM is sitting in the channel. With both
// terms, the zap fires and the forbidden item lands in inventory. With only
// the first, the guard goes FALSE, control falls through to the else-arm's
// log(), and the item is EQUIPPED on a character forbidden to wear it --
// which the very rent-load that produced it then persists. That is the
// regression these tests exist to catch, and it is exactly what they report
// when the second term is deleted (see the commit message's sabotage log).
// ===========================================================================

TEST(EquipCharZapGuard, AntiEvilItemLandsInInventoryWhenOnlyTheStashedVnumIsSet) {
    ScopedVnumWorld fixture_world;
    ScopedGlobalCharacterLists fixture_lists;

    char_data wearer{};
    make_mortal_player(wearer);
    ScopedClearCharFields wearer_cleanup{wearer};
    GET_ALIGNMENT(&wearer) = -1000; // IS_EVIL: alignment <= -100

    descriptor_data descriptor{};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = CON_PLYNG;
    descriptor.character = &wearer;
    descriptor.next = nullptr;
    wearer.desc = &descriptor;
    wearer.specials.position = POSITION_STANDING;

    // The load window, exactly as production carries it since the split: no
    // location, the persisted VNUM in the channel. NOT char_to_room().
    set_location(&wearer, NOWHERE);
    stash_load_room_vnum(&wearer, kOwnerVnum);
    ASSERT_EQ(location_of(&wearer), NOWHERE)
        << "the first guard term must be FALSE, or this test proves nothing";
    ASSERT_NE(peek_load_room_vnum(&wearer), NOWHERE);

    obj_data holy_symbol{};
    holy_symbol.obj_flags.type_flag = ITEM_ARMOR;
    holy_symbol.obj_flags.extra_flags = ITEM_ANTI_EVIL;
    holy_symbol.obj_flags.weight = 1;
    holy_symbol.in_room = NOWHERE; // LS1-ALLOW: obj-location (fixture init)
    holy_symbol.name = str_dup("symbol");
    holy_symbol.short_description = str_dup("a holy symbol");
    holy_symbol.description = str_dup("A holy symbol lies here.");

    equip_char(&wearer, &holy_symbol, WEAR_BODY);

    EXPECT_EQ(wearer.equipment[WEAR_BODY], nullptr)
        << "a rent-loaded anti-evil item stayed EQUIPPED on an evil wearer";
    EXPECT_EQ(wearer.carrying, &holy_symbol);
    EXPECT_EQ(holy_symbol.carried_by, &wearer);

    if (holy_symbol.carried_by != nullptr)
        obj_from_char(&holy_symbol);
    if (wearer.equipment[WEAR_BODY] == &holy_symbol)
        unequip_char(&wearer, WEAR_BODY);
    RELEASE(holy_symbol.name);
    RELEASE(holy_symbol.short_description);
    RELEASE(holy_symbol.description);
}

TEST(EquipCharZapGuard, RaceRestrictedItemLandsInInventoryWhenOnlyTheStashedVnumIsSet) {
    // The second arm (fight.cpp's race-restricted zap), same derivation. It
    // needs its own test because it is a separate `if` with its own copy of
    // the two-term guard -- deleting the second term from one arm leaves the
    // other green.
    ScopedVnumWorld fixture_world;
    ScopedGlobalCharacterLists fixture_lists;

    char_data wearer{};
    make_mortal_player(wearer);
    ScopedClearCharFields wearer_cleanup{wearer};
    wearer.player.race = RACE_HUMAN;

    descriptor_data descriptor{};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = CON_PLYNG;
    descriptor.character = &wearer;
    descriptor.next = nullptr;
    wearer.desc = &descriptor;
    wearer.specials.position = POSITION_STANDING;

    set_location(&wearer, NOWHERE);
    stash_load_room_vnum(&wearer, kOwnerVnum);

    obj_data dwarven_mail{};
    dwarven_mail.obj_flags.type_flag = ITEM_ARMOR;
    dwarven_mail.obj_flags.extra_flags = ITEM_DWARF; // human wearer: forbidden
    dwarven_mail.obj_flags.weight = 1;
    dwarven_mail.in_room = NOWHERE; // LS1-ALLOW: obj-location (fixture init)
    dwarven_mail.name = str_dup("mail");
    dwarven_mail.short_description = str_dup("a suit of dwarven mail");
    dwarven_mail.description = str_dup("A suit of dwarven mail lies here.");

    equip_char(&wearer, &dwarven_mail, WEAR_BODY);

    EXPECT_EQ(wearer.equipment[WEAR_BODY], nullptr)
        << "a rent-loaded race-restricted item stayed EQUIPPED on a forbidden wearer";
    EXPECT_EQ(wearer.carrying, &dwarven_mail);

    if (dwarven_mail.carried_by != nullptr)
        obj_from_char(&dwarven_mail);
    if (wearer.equipment[WEAR_BODY] == &dwarven_mail)
        unequip_char(&wearer, WEAR_BODY);
    RELEASE(dwarven_mail.name);
    RELEASE(dwarven_mail.short_description);
    RELEASE(dwarven_mail.description);
}

// ===========================================================================
// R23 ARMED (LS-3b T5). save_char()'s second fail-safe arm
// (db_players.cpp) -- `(load_room == NOWHERE) && (peek_load_room_vnum(ch) !=
// NOWHERE)` -- was written by LS-3a as a provably unreachable else-if, for the
// same reason R9/R10 were no-ops: channel and location were one field, so
// whenever control reached it either the first term was false or the peek was
// NOWHERE too. The store split makes it reachable, and these two tests are its
// first witnesses.
// ===========================================================================

TEST(SaveCharChannelFallback, PersistsTheStashedVnumWhenThereIsNoLoadRoomAndNoLocation) {
    ScopedVnumWorld fixture_world;
    ScopedStartRooms fixture_start_rooms;
    ScopedPlayerTable fixture_player_table{nullptr};

    char_data player{};
    make_mortal_player(player);
    ScopedClearCharFields player_cleanup{player};
    RELEASE(player.player.name);
    CREATE(player.player.name, char, strlen("channelchr") + 1);
    strcpy(player.player.name, "channelchr");

    descriptor_data descriptor{};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = CON_PLYNG;
    descriptor.character = &player;
    descriptor.next = nullptr;
    player.desc = &descriptor;

    // Both of save_char()'s preconditions for the SECOND arm: no explicit
    // load_room from the caller, no location on the character, and a real room
    // still known through the channel.
    set_location(&player, NOWHERE);
    stash_load_room_vnum(&player, kOwnerVnum);
    player.specials2.load_room = -12345; // must be overwritten, not inherited

    save_char(&player, NOWHERE, 0);

    EXPECT_EQ(GET_LOADROOM(&player), kOwnerVnum)
        << "the channel fail-safe did not fire: this character would come back "
           "at their racial start room instead of the room they left";
    EXPECT_NE(GET_LOADROOM(&player), -12345);

    RELEASE(player.player.name);
}

TEST(SaveCharChannelFallback, PersistsNowhereWhenNeitherALocationNorAStashIsAvailable) {
    // The CONTROL, and the reason the arm is an `else if` and not an
    // unconditional assignment: with nothing stashed either, NOWHERE is still
    // what gets persisted, which is what sends the character to their racial
    // start room -- the designed behavior for a character whose room is
    // genuinely unknown. A fail-safe that invented a room here would be worse
    // than the bug it fixes.
    ScopedVnumWorld fixture_world;
    ScopedStartRooms fixture_start_rooms;
    ScopedPlayerTable fixture_player_table{nullptr};

    char_data player{};
    make_mortal_player(player);
    ScopedClearCharFields player_cleanup{player};
    RELEASE(player.player.name);
    CREATE(player.player.name, char, strlen("nochanchr") + 1);
    strcpy(player.player.name, "nochanchr");

    descriptor_data descriptor{};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = CON_PLYNG;
    descriptor.character = &player;
    descriptor.next = nullptr;
    player.desc = &descriptor;

    set_location(&player, NOWHERE);
    ASSERT_EQ(peek_load_room_vnum(&player), NOWHERE)
        << "make_mortal_player must leave the channel empty for this control";
    player.specials2.load_room = -12345;

    save_char(&player, NOWHERE, 0);

    EXPECT_EQ(GET_LOADROOM(&player), NOWHERE);

    RELEASE(player.player.name);
}

// ===========================================================================
// TEARDOWN UNDER THE INVARIANT (LS-3b T5 -- mechanism (3) of ruling R-3b-A,
// and the retirement witness for census B's D5 use-after-free).
//
// free_char() performs NO location unregistration, and under the private
// handle that is CORRECT rather than lucky: the store dies with the struct.
// What made it dangerous before this commit was the TORN state -- review
// finding F4: extract_char() gates its unsplice on `location_of(ch) !=
// NOWHERE` (handler.cpp), and a character whose field said NOWHERE while the
// chain still held it took the else arm, was never unspliced, and reached
// free_char() with a process-global chain still pointing at it.
//
// The amendment makes that state unproducible, and this test is the executable
// form of that claim: place a character NOWHERE through the real production
// call, extract them through the real extract_char(), and assert the fallback
// room's chain is intact and empty afterwards. The heap allocation is
// deliberate -- extract_char() reaches free_char() on this path, so a stack
// char_data could not be used, and ASan is the second observer here.
// ===========================================================================

TEST(ExtractCharTeardown, ExtractingANowherePlacedCharacterLeavesEveryRoomChainIntact) {
    ScopedVnumWorld fixture_world;
    ScopedStartRooms fixture_start_rooms;
    ScopedGlobalCharacterLists fixture_lists;

    // A bystander who really is in room 0, published through the real
    // char_to_room(), so the test can tell "the chain is intact" from "the
    // chain is empty because nothing was ever in it".
    char_data bystander{};
    make_mortal_player(bystander);
    ScopedClearCharFields bystander_cleanup{bystander};
    bystander.specials2.act |= MOB_ISNPC; // no descriptor, no save path
    char_to_room(&bystander, 0);
    ASSERT_EQ(rots::entity::first_occupant(room_by_id_total(0)), &bystander);

    // The character under test: an NPC, so extract_char() takes the arm that
    // ends in free_char(). Heap-allocated exactly as production allocates it.
    char_data* doomed = nullptr;
    CREATE(doomed, char_data, 1);
    clear_char(doomed, MOB_ISNPC);
    doomed->player.name = str_dup("doomed");
    doomed->player.short_descr = str_dup("a doomed mob");
    doomed->nr = -1;
    doomed->next = character_list;
    character_list = doomed;

    // The production call that used to produce the torn state.
    char_to_room(doomed, NOWHERE);
    ASSERT_EQ(location_of(doomed), NOWHERE);
    ASSERT_EQ(rots::entity::first_occupant(room_by_id_total(0)), &bystander)
        << "the NOWHERE placement spliced into the fallback room -- the torn "
           "state this test exists to prove is gone";

    extract_char(doomed); // reaches free_char(): `doomed` is dangling after this

    // Room 0's chain is exactly what it was: the bystander, and nothing that
    // used to be `doomed`. Before the amendment the freed pointer was still
    // linked here, and walking the chain was a use-after-free.
    EXPECT_EQ(rots::entity::first_occupant(room_by_id_total(0)), &bystander);
    int occupants_seen = 0;
    for (char_data* occupant : rots::entity::occupants(room_by_id_total(0))) {
        (void)occupant;
        ++occupants_seen;
    }
    EXPECT_EQ(occupants_seen, 1) << "room 0's chain gained or lost a member";

    char_from_room(&bystander);
}

// R20 -- broadcast_weather_msdp_update() (protocol.cpp). This walker had no
// location guard at all, and its weather arm is the only one that reads the
// room. A character parked at the character menu has none, so room_of()
// resolved world[-1]: room_data::operator[] answers that with a mudlog and a
// room-0 FALLBACK, so the menu-sitter was told about room 0's weather, every
// tick, with a log line each time.
//
// TWO OBSERVABLES, because either alone would be weak: the MSDP variable must
// stay at the sentinel this test writes (proving the walker never reached
// MSDPSetString), and stderr must carry no operator[] complaint (proving the
// room was never resolved). PRF_MSDP is deliberately NOT set, so MSDPSend()
// takes its own early return (protocol.cpp:1251) and no socket write is
// attempted from a fixture descriptor with no socket.
//
// Non-vacuity: removing the guard makes BOTH assertions fail -- see the
// commit message for the run record.
TEST(WeatherBroadcastGuard, SkipsACharacterWithNoLocationInsteadOfReadingRoomZero) {
    ScopedVnumWorld fixture_world;

    char_data menu_sitter{};
    make_mortal_player(menu_sitter);
    ScopedClearCharFields menu_sitter_cleanup{menu_sitter};
    ASSERT_EQ(location_of(&menu_sitter), NOWHERE)
        << "the fixture character must start with no location -- that IS the case under test";

    descriptor_data descriptor{};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = CON_PLYNG;
    descriptor.character = &menu_sitter;
    descriptor.next = nullptr;
    descriptor.pProtocol = ProtocolCreate();
    menu_sitter.desc = &descriptor;

    MSDPSetString(&descriptor, eMDSP_WEATHER, "sentinel weather");

    descriptor_data *previous_descriptor_list = descriptor_list;
    descriptor_list = &descriptor;

    testing::internal::CaptureStderr();
    broadcast_weather_msdp_update(rots::world::weather_msdp_kind::weather);
    const std::string captured = testing::internal::GetCapturedStderr();

    descriptor_list = previous_descriptor_list;

    EXPECT_STREQ(descriptor.pProtocol->pVariables[eMDSP_WEATHER]->pValueString,
                 "sentinel weather")
        << "the walker sent weather to a character who is not in any room";
    EXPECT_EQ(captured.find("world[] called for negative room number"), std::string::npos)
        << "stderr: " << captured;

    ProtocolDestroy(descriptor.pProtocol);
    descriptor.pProtocol = nullptr;
}

// The CONTROL: a character who IS in a room still receives the broadcast, so
// the guard above cannot have silenced the walker outright.
TEST(WeatherBroadcastGuard, StillBroadcastsToACharacterWhoIsInARoom) {
    ScopedVnumWorld fixture_world;
    ScopedGlobalCharacterLists fixture_lists;

    char_data resident{};
    make_mortal_player(resident);
    ScopedClearCharFields resident_cleanup{resident};

    descriptor_data descriptor{};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = CON_PLYNG;
    descriptor.character = &resident;
    descriptor.next = nullptr;
    descriptor.pProtocol = ProtocolCreate();
    resident.desc = &descriptor;

    char_to_room(&resident, kOwnerRnum);
    MSDPSetString(&descriptor, eMDSP_WEATHER, "sentinel weather");

    descriptor_data *previous_descriptor_list = descriptor_list;
    descriptor_list = &descriptor;

    broadcast_weather_msdp_update(rots::world::weather_msdp_kind::weather);

    descriptor_list = previous_descriptor_list;

    EXPECT_STRNE(descriptor.pProtocol->pVariables[eMDSP_WEATHER]->pValueString,
                 "sentinel weather")
        << "a placed character stopped receiving weather -- the guard is too broad";

    ProtocolDestroy(descriptor.pProtocol);
    descriptor.pProtocol = nullptr;
    char_from_room(&resident);
}

// R21 -- clean_expose_elements() (comm.cpp), the same defect in the
// fast-update pulse loop. A mage with no location had his exposed-elements
// target searched for in ROOM 0 (operator[]'s fallback for world[-1]), so a
// spell cast anywhere else was cancelled the moment its caster hit the
// character menu -- with a mudlog per pulse while he sat there.
//
// The target here is deliberately in NO room's occupant chain: that is the
// shape that makes the unguarded walk reach its reset()/notify arm, so the
// assertions below distinguish "skipped the mage" from "searched and found".
TEST(CleanExposeElementsGuard, SkipsAMageWithNoLocationInsteadOfSearchingRoomZero) {
    ScopedVnumWorld fixture_world;

    // A raw char_data, NOT make_mortal_player(): the mage-spec block needs
    // ch->profs to point at a caller-owned char_prof_data, and clear_char()
    // would have CREATE1()d one that ScopedClearCharFields then frees -- a
    // free() of this stack object.
    char_data mage{};
    char_prof_data mage_profs{};
    mage.profs = &mage_profs;
    mage.player.race = RACE_HUMAN;
    mage.player.level = 20;
    mage_profs.specialization = static_cast<int>(game_types::PS_Cold);
    mage.extra_specialization_data.set(mage);
    elemental_spec_data *spec_data = mage.extra_specialization_data.get_mage_spec();
    ASSERT_NE(spec_data, nullptr);

    descriptor_data descriptor{};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = CON_PLYNG;
    descriptor.character = &mage;
    descriptor.next = nullptr;
    mage.desc = &descriptor;

    // No location: the menu-sitter shape.
    set_location(&mage, NOWHERE);

    // A target in no room's chain at all -- the shape the unguarded walk
    // would have failed to find in room 0, and then cancelled.
    char_data absent_target{};
    spec_data->exposed_target = &absent_target;

    ScopedTrackedMage tracked{&mage};

    testing::internal::CaptureStderr();
    clean_expose_elements();
    const std::string captured = testing::internal::GetCapturedStderr();

    EXPECT_EQ(spec_data->exposed_target, &absent_target)
        << "a mage with no location had his expose-elements target cancelled by a room-0 search";
    EXPECT_STREQ(descriptor.small_outbuf, "");
    EXPECT_EQ(captured.find("world[] called for negative room number"), std::string::npos)
        << "stderr: " << captured;
}

// ===========================================================================
// F19 -- THE SAME IN-RANGE STASHED-VNUM LEAK AT THE OTHER TWO WALKERS,
// CLOSED BY THE STORE SPLIT (LS-3b T5).
//
// The LS-3a R20/R21 guards above are one-sided (`< 0` / `>= 0`), so before the
// split they caught only the menu sitter whose persisted room VNUM happened to
// read negative. A stashed VNUM of any non-negative size PASSED both of them,
// and the walkers then used it as a world index. Review finding F19 filed both
// sites; each gets a paired test here, and R21's is the one that matters most
// because it destroys live spell state rather than merely reporting the wrong
// room.
// ===========================================================================

TEST(WeatherBroadcastGuard, SkipsAMenuSitterCarryingAnInRangeStashedVnum) {
    ScopedVnumWorld fixture_world;

    char_data menu_sitter{};
    make_mortal_player(menu_sitter);
    ScopedClearCharFields menu_sitter_cleanup{menu_sitter};

    // The post-split menu-sitter shape: no location, an IN-RANGE persisted
    // VNUM in the channel. kOwnerVnum is a real room in this fixture world, so
    // before the split this descriptor received THAT room's weather every tick
    // -- the guard's `< 0` half cannot see it.
    set_location(&menu_sitter, NOWHERE);
    stash_load_room_vnum(&menu_sitter, kOwnerVnum);
    ASSERT_GE(peek_load_room_vnum(&menu_sitter), 0);

    descriptor_data descriptor{};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = CON_PLYNG;
    descriptor.character = &menu_sitter;
    descriptor.next = nullptr;
    descriptor.pProtocol = ProtocolCreate();
    menu_sitter.desc = &descriptor;

    MSDPSetString(&descriptor, eMDSP_WEATHER, "sentinel weather");

    descriptor_data *previous_descriptor_list = descriptor_list;
    descriptor_list = &descriptor;

    broadcast_weather_msdp_update(rots::world::weather_msdp_kind::weather);

    descriptor_list = previous_descriptor_list;

    EXPECT_STREQ(descriptor.pProtocol->pVariables[eMDSP_WEATHER]->pValueString,
                 "sentinel weather")
        << "a menu sitter was told about the weather in a room read out of the "
           "world table with a persisted VNUM used as an index";

    ProtocolDestroy(descriptor.pProtocol);
    descriptor.pProtocol = nullptr;
}

// The CONTROL: a mage who IS in a room still gets the sweep, target absent
// from his room, so the guard cannot have disabled the maintenance pass.
TEST(CleanExposeElementsGuard, StillCancelsForAMageWhoIsInARoomAndCannotSeeTheTarget) {
    ScopedVnumWorld fixture_world;

    char_data mage{};
    char_prof_data mage_profs{};
    mage.profs = &mage_profs;
    mage.player.race = RACE_HUMAN;
    mage.player.level = 20;
    mage_profs.specialization = static_cast<int>(game_types::PS_Cold);
    mage.extra_specialization_data.set(mage);
    elemental_spec_data *spec_data = mage.extra_specialization_data.get_mage_spec();
    ASSERT_NE(spec_data, nullptr);

    descriptor_data descriptor{};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = CON_PLYNG;
    descriptor.character = &mage;
    descriptor.next = nullptr;
    mage.desc = &descriptor;

    // Placed via ScopedRoomOccupants rather than char_to_room(): this
    // fixture's char_data is raw (no clear_char()), and char_to_room() would
    // charge its zone power and light accounting for a character the test
    // never finishes wiring up. The fixture's set_location() call plus the
    // chain it publishes is exactly what the walk reads.
    ScopedRoomOccupants occupant_fixture{room_by_id_total(kOwnerRnum), kOwnerRnum, {&mage}};

    char_data absent_target{};
    spec_data->exposed_target = &absent_target;

    ScopedTrackedMage tracked{&mage};

    clean_expose_elements();

    EXPECT_EQ(spec_data->exposed_target, nullptr)
        << "the sweep no longer runs for a placed mage -- the guard is too broad";
    EXPECT_STREQ(descriptor.small_outbuf,
                 "Your target is no longer vulnerable to your spells.\r\n");
}

// F19's STATE-DESTROYING case, and the sharpest single argument for the store
// split. clean_expose_elements() runs every fast-update pulse over the
// process-wide specialized_mages roster, which every LOADED character joins --
// so rent-window and menu-sitting mages are on it. Its guard is `>= 0`, so
// before the split a mage carrying an in-range persisted VNUM passed it, the
// walk searched an UNRELATED room for the exposed target, failed to find it,
// told the mage "Your target is no longer vulnerable to your spells." and
// called spec_data->reset(). That is a live spell cancelled by a login, not a
// display glitch -- which is why it is named in the PR body as a
// state-corruption fix.
TEST(CleanExposeElementsGuard, SkipsAMageCarryingAnInRangeStashedVnum) {
    ScopedVnumWorld fixture_world;

    char_data mage{};
    char_prof_data mage_profs{};
    mage.profs = &mage_profs;
    mage.player.race = RACE_HUMAN;
    mage.player.level = 20;
    mage_profs.specialization = static_cast<int>(game_types::PS_Cold);
    mage.extra_specialization_data.set(mage);
    elemental_spec_data *spec_data = mage.extra_specialization_data.get_mage_spec();
    ASSERT_NE(spec_data, nullptr);

    descriptor_data descriptor{};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = CON_PLYNG;
    descriptor.character = &mage;
    descriptor.next = nullptr;
    mage.desc = &descriptor;

    // No location, an IN-RANGE persisted VNUM in the channel: the shape the
    // one-sided `>= 0` guard cannot catch on its own.
    set_location(&mage, NOWHERE);
    stash_load_room_vnum(&mage, kOwnerVnum);
    ASSERT_GE(peek_load_room_vnum(&mage), 0);

    // The target is somewhere the mage's real room is not -- and the room the
    // stashed VNUM names is deliberately NOT where the target is, so an
    // unguarded walk finds nothing and cancels.
    char_data absent_target{};
    spec_data->exposed_target = &absent_target;

    ScopedTrackedMage tracked{&mage};

    clean_expose_elements();

    EXPECT_EQ(spec_data->exposed_target, &absent_target)
        << "a mage in the login/rent window had a live spell cancelled by a "
           "search of the room his persisted VNUM happened to index";
    EXPECT_STREQ(descriptor.small_outbuf, "");
}

// R7/R8 END TO END -- the rent-load ordering, over the REAL world[] table,
// with the REAL equip_char()/char_to_room()/unequip_char(). A NAMED O-2
// BEHAVIOR CHANGE (LS-3a T2 tranche 2e-beta).
//
// THE DEFECT, as it stood: Crash_load() equips every worn item
// (objsave.cpp:439) while store_to_char() has left the raw persisted room VNUM
// in the location field and the character is in no room at all. The ITEM_LIGHT
// arm of attach_equipment() believed that integer and did world[VNUM].light++
// -- on a large world, a real and completely unrelated room. The matching
// decrement arrives later against the room the character is actually placed
// in, so the wrong room keeps its +1 FOREVER, once per rent-load of a lit
// light source, silently.
//
// This test walks the whole sequence and checks the counter at each step,
// including the closing balance: after the character unequips, both rooms are
// back at zero. Against the unfixed code the vnum-indexed room was left at 1
// (see the commit message for the run record).
//
// The two rooms are deliberately DISTINCT and both real: rnum 3 is where the
// character is placed, index 35 is the slot the persisted vnum 35 indexes --
// a create_bulk() filler room in this fixture, exactly as a stale vnum lands
// in a real room on the live world.
TEST(LoadRoomRider, RentLoadingALitLightBumpsOnlyTheRoomTheCharacterIsPlacedIn) {
    ScopedVnumWorld fixture_world;
    ScopedGlobalCharacterLists fixture_lists;

    char_data player{};
    make_mortal_player(player);
    ScopedClearCharFields player_cleanup{player};

    descriptor_data descriptor{};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = CON_PLYNG;
    descriptor.character = &player;
    descriptor.next = nullptr;
    player.desc = &descriptor;

    obj_data lamp{};
    lamp.obj_flags.type_flag = ITEM_LIGHT;
    lamp.obj_flags.weight = 1;
    lamp.obj_flags.value[2] = 10; // fuel
    lamp.obj_flags.value[3] = 0;  // not yet lit
    lamp.in_room = NOWHERE;       // LS1-ALLOW: obj-location (fixture init)
    lamp.name = str_dup("lamp");
    lamp.short_description = str_dup("a brass lamp");
    lamp.description = str_dup("A brass lamp lies here.");

    // THE LOAD WINDOW: what store_to_char (db_players.cpp:1376) leaves behind
    // -- the raw persisted VNUM, no placement of any kind.
    player.specials2.load_room = kOwnerVnum;
    stash_load_room_vnum(&player, GET_LOADROOM(&player));
    ASSERT_EQ(room_by_id_total(kOwnerVnum)->light, 0);
    ASSERT_EQ(room_by_id_total(kOwnerRnum)->light, 0);

    // objsave.cpp:439 -- Crash_load's equip loop, mid-window.
    equip_char(&player, &lamp, WEAR_LIGHT);

    EXPECT_EQ(player.equipment[WEAR_LIGHT], &lamp);
    // Normalized to ON, so the placement below can count it...
    EXPECT_EQ(lamp.obj_flags.value[3], 1);
    // ...and the room the stale vnum indexed was left alone. THIS is the fix.
    EXPECT_EQ(room_by_id_total(kOwnerVnum)->light, 0);

    // objsave.cpp:511 -- load_character places the player, and char_to_room's
    // own equipment sweep (placement.cpp:349-353) counts the lamp, once, at
    // the room that actually has him.
    char_to_room(&player, kOwnerRnum);
    EXPECT_EQ(room_by_id_total(kOwnerRnum)->light, 1);
    EXPECT_EQ(room_by_id_total(kOwnerVnum)->light, 0);

    // ...and the books balance: the later decrement has something to cancel.
    unequip_char(&player, WEAR_LIGHT);
    EXPECT_EQ(room_by_id_total(kOwnerRnum)->light, 0);
    EXPECT_EQ(room_by_id_total(kOwnerVnum)->light, 0);

    if (lamp.carried_by != nullptr)
        obj_from_char(&lamp);
    if (player.equipment[WEAR_LIGHT] == &lamp)
        unequip_char(&player, WEAR_LIGHT);
    RELEASE(lamp.name);
    RELEASE(lamp.short_description);
    RELEASE(lamp.description);
    char_from_room(&player);
}

// ===========================================================================
// LS-3b T9b -- REVIEW-1 FINDING B-1: THE LOAD-WINDOW RIDER GAPS
//
// The O-5 flagged-rider inventory was scoped to the MENU-SITTER (census
// review F21's bounded three-reader set) and never extended to the character
// being LOADED, who is inside the same window and runs a great deal more
// code. The whole-branch review found four deltas there. The four tests below
// close the two with real consequences and pin the two the review recorded as
// message/disposition changes; the corpse test additionally REFUTES the
// mechanism the review attributed to one of them (see its own header).
//
// WHAT CHANGED IN PRODUCTION: load_character() (objsave.cpp) now retires the
// VNUM channel once it has placed the character. Before the store split the
// channel and the location were one field, so char_to_room() ended the
// channel's life by overwriting it; splitting the stores accidentally
// extended it to the whole session. Every test below either drives the REAL
// load_character() or reproduces the exact window state production leaves.
// ===========================================================================

// B-1a. THE FLAGSHIP, driven end to end through the REAL load_character().
//
// interpre.cpp's CON_SLCT '1' handler calls save_char(d->character, NOWHERE,
// 0) immediately after load_character(), and its own comment states the
// reason: calc_load_room()'s bugged arm now clamps to the racial start room
// (m-14) rather than returning -1, so char_to_room() genuinely places the
// character.
//
// CORRECTED FROM THE ORIGINAL DESIGN PREDICTION (found red-first, verified
// empirically): NOWHERE does NOT persist here anymore. Once the character is
// genuinely placed, save_char()'s FIRST arm --
// `(load_room == NOWHERE) && (location_of(ch) != NOWHERE)` (db_players.cpp)
// -- fires ahead of the channel-based R23 arm this test used to exercise,
// and persists the placement's own vnum. THE CHANNEL-RETIREMENT SABOTAGE
// PROBE (spec 2.3's "B-1 witness survives" requirement) was re-run against
// this test post-flip and did NOT reproduce red: with
// `stash_load_room_vnum(ch, NOWHERE)` commented out of load_character(), this
// test still passes, because arm 1 short-circuits before the channel-based
// R23 arm is ever evaluated. The channel-retirement regression is still
// caught elsewhere in this file --
// LoadRoomRider.LoadCharacterRetiresTheVnumChannelOnceThePlacementIsDone
// (below) fails under the identical sabotage, on the ordinary non-bugged
// login path where arm 1 does not apply -- so B-1 protection is not lost
// from the suite, but THIS test is no longer its witness. See the commit
// message for both sabotage-probe outputs.
//
// The store split falsified the pre-clamp version of the paragraph above
// without touching it: save_char()'s R23 arm fired on the pre-fix shape
// (load_room NOWHERE, location NOWHERE, channel set) and persisted the STALE
// login VNUM instead -- sending the bugged character back to the room they
// came from. The channel retirement restored the documented behavior for
// that shape; m-14's clamp then removed the shape entirely (the character is
// no longer left unplaced), which is why arm 1 now decides the outcome
// instead.
//
// RED-FIRST: against the unclamped arm (pre-m-14), the placement assertions
// below fail first -- location_of(&player) stays NOWHERE against the
// expected racial start rnum 1.
TEST(LoadRoomRider, PostLoginSaveOfABuggedCharacterPersistsThePlacedRoomThroughTheRealLoadCharacter) {
    ScopedVnumWorld fixture_world;
    ScopedStartRooms fixture_start_rooms;
    ScopedPlayerTable fixture_player_table{nullptr};
    ScopedGlobalCharacterLists fixture_lists;

    // The discrimination override (spec review O-2): a start room distinct
    // from rnum 0, so assertions against it cannot agree with the pre-fix
    // room-0 fallback.
    constexpr int kDistinctStartRnum = 1;
    for (int race = 0; race < MAX_RACES; ++race)
        r_mortal_start_room[race] = kDistinctStartRnum;

    char path_template[] = "/tmp/rots-loadroom-b1a-XXXXXX";
    char *created_path = rots_mkdtemp(path_template);
    ASSERT_NE(created_path, nullptr);
    const std::filesystem::path temp_data_dir = created_path;

    char_data player{};
    make_mortal_player(player);
    ScopedClearCharFields player_cleanup{player};
    RELEASE(player.player.name);
    CREATE(player.player.name, char, strlen("buggedloadchr") + 1);
    strcpy(player.player.name, "buggedloadchr");

    descriptor_data descriptor{};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = CON_PLYNG;
    descriptor.character = &player;
    descriptor.next = nullptr;
    player.desc = &descriptor;

    // A vnum no room in this fixture carries, so calc_load_room()'s bugged
    // arm -- real_room(r_bugged_start_room), which sits AFTER the `< 0` clamp
    // above it -- yields -1.
    r_bugged_start_room = 999999;
    // Below calc_load_room()'s bugged-character floor (str >= 1).
    player.abilities.str = 0;
    player.tmpabilities.str = 0;

    // What store_to_char (db_players.cpp) leaves behind at the top of the
    // window: the raw persisted VNUM, no placement.
    player.specials2.load_room = kOwnerVnum;
    stash_load_room_vnum(&player, GET_LOADROOM(&player));

    {
        // The REAL login placement, in a temp data dir so Crash_load's
        // plrobjs/ probe misses and it takes its own no-data arm (which still
        // runs calc_load_room and returns).
        ScopedWorkingDirectory scoped_data_dir{temp_data_dir};
        load_character(&player);
    }

    ASSERT_EQ(location_of(&player), kDistinctStartRnum)
        << "the clamped bugged arm must place the character in the racial start room";
    ASSERT_EQ(rots::entity::first_occupant(room_by_id_total(kDistinctStartRnum)), &player);
    ASSERT_EQ(rots::entity::first_occupant(room_by_id_total(0)), nullptr)
        << "...and NOT room 0 (the pre-fix fallback)";

    // Sentinel distinct from every candidate, so the assertions below prove
    // save_char actually wrote the field.
    player.specials2.load_room = -12345;

    // interpre.cpp's own statement, verbatim.
    save_char(&player, NOWHERE, 0);

    // CORRECTED FROM THE BRIEF'S ORIGINAL PREDICTION (found red-first,
    // verified empirically against the real save_char()): the character is
    // genuinely PLACED post-clamp, so save_char()'s FIRST arm --
    // `(load_room == NOWHERE) && (location_of(ch) != NOWHERE)`
    // (db_players.cpp) -- fires ahead of the channel-based R23 arm this
    // test's header used to describe, and persists the placement's own vnum
    // instead of NOWHERE. See the commit message for the sabotage-probe
    // evidence of which arm actually decides the outcome now.
    EXPECT_EQ(GET_LOADROOM(&player), room_vnum_for(kDistinctStartRnum))
        << "the clamped bugged arm's placement is what save_char() now persists";
    EXPECT_NE(GET_LOADROOM(&player), kOwnerVnum);
    EXPECT_NE(GET_LOADROOM(&player), -12345);
    // ...and still not the rejected mechanical conversion's value (world[0]'s
    // vnum, reached through the room-0 fallback), which is the discrimination
    // the LS-3a rider row was written for and which this fix does not disturb.
    EXPECT_NE(GET_LOADROOM(&player), room_vnum_for(0));

    // LOAD-BEARING (spec review O-2, the DoRescue/waiting_list class): unlink
    // the stack character from the room it actually landed in before this
    // frame returns.
    unlink_from_occupant_chain(*room_by_id_total(kDistinctStartRnum), &player);
    RELEASE(player.player.name);
    std::filesystem::remove_all(temp_data_dir);
}

// B-1d / M-4. The channel's LIFETIME, on the ordinary (non-bugged) login --
// the root cause behind B-1a and behind the three guards whose second term is
// scoped to the load window (riders 8/10/12). Nothing in the tree ever wrote
// NOWHERE back into the channel, so after the split it stayed frozen at the
// login-time VNUM for the whole session.
//
// RED-FIRST: with the retirement removed this fails with 35 against NOWHERE.
TEST(LoadRoomRider, LoadCharacterRetiresTheVnumChannelOnceThePlacementIsDone) {
    ScopedVnumWorld fixture_world;
    ScopedStartRooms fixture_start_rooms;
    ScopedGlobalCharacterLists fixture_lists;

    char path_template[] = "/tmp/rots-loadroom-channel-XXXXXX";
    char *created_path = rots_mkdtemp(path_template);
    ASSERT_NE(created_path, nullptr);
    const std::filesystem::path temp_data_dir = created_path;

    char_data player{};
    make_mortal_player(player);
    ScopedClearCharFields player_cleanup{player};
    RELEASE(player.player.name);
    CREATE(player.player.name, char, strlen("channelchr") + 1);
    strcpy(player.player.name, "channelchr");

    descriptor_data descriptor{};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = CON_PLYNG;
    descriptor.character = &player;
    descriptor.next = nullptr;
    player.desc = &descriptor;

    player.specials2.load_room = kOwnerVnum;
    stash_load_room_vnum(&player, GET_LOADROOM(&player));

    {
        ScopedWorkingDirectory scoped_data_dir{temp_data_dir};
        load_character(&player);
    }

    // The channel did its job: the persisted VNUM resolved, through the real
    // calc_load_room, to the matching RNUM, and the character is there.
    EXPECT_EQ(location_of(&player), kOwnerRnum);
    // ...and it is spent. A channel that outlives login is what armed
    // save_char's R23 arm on the login save (B-1a) and silently widened the
    // three load-window guards past the window they are scoped to (B-1d).
    EXPECT_EQ(peek_load_room_vnum(&player), NOWHERE)
        << "the VNUM channel outlived the placement it exists to perform";

    // The three readers whose answers the stale channel changed, evaluated
    // here in their post-login state: with the channel retired, each one's
    // second term is dead and the first (the real location) decides -- which
    // is exactly what the single-store tree did before the split.
    EXPECT_NE(location_of(&player), NOWHERE);

    char_from_room(&player);
    RELEASE(player.player.name);
    std::filesystem::remove_all(temp_data_dir);
}

// B-1c. THE ZAP BROADCAST. equip_char()'s two zap arms narrate with
// act(..., TO_ROOM). Inside Crash_load the wearer has no location, and
// act_impl (comm.cpp) resolves TO_ROOM through
// `ch && location_of(ch) != NOWHERE`, so the line is DROPPED. Before the
// split location_of() returned the persisted VNUM, act() indexed world[] with
// it, and "$n is zapped by $p" was delivered to whoever happened to be
// standing in that unrelated room (or in room 0 via the fallback).
//
// This is an observable delta on the login path and it belongs to rider row
// 8, whose text named only the ITEM outcome. It is also strictly an
// improvement -- the pre-split line leaked a private message into a room the
// wearer was not in -- which is why it is pinned rather than reverted.
//
// The pair is deliberate: the control below proves the broadcast is alive for
// a placed wearer, so the silence asserted here is the window's doing and not
// a test that could never see the message at all.
TEST(LoadWindowBroadcast, ZapBroadcastIsSilentForALoadWindowWearer) {
    ScopedVnumWorld fixture_world;
    ScopedGlobalCharacterLists fixture_lists;

    char_data wearer{};
    make_mortal_player(wearer);
    ScopedClearCharFields wearer_cleanup{wearer};
    GET_ALIGNMENT(&wearer) = -1000; // IS_EVIL
    RELEASE(wearer.player.name);
    CREATE(wearer.player.name, char, strlen("zapwearer") + 1);
    strcpy(wearer.player.name, "zapwearer");

    descriptor_data wearer_descriptor{};
    wearer_descriptor.output = wearer_descriptor.small_outbuf;
    wearer_descriptor.small_outbuf[0] = '\0';
    wearer_descriptor.bufptr = 0;
    wearer_descriptor.bufspace = SMALL_BUFSIZE - 1;
    wearer_descriptor.connected = CON_PLYNG;
    wearer_descriptor.character = &wearer;
    wearer_descriptor.next = nullptr;
    wearer.desc = &wearer_descriptor;
    wearer.specials.position = POSITION_STANDING;

    // The bystander stands in world[kOwnerVnum] -- the slot the PRE-SPLIT
    // code would have indexed with the persisted vnum, and therefore the one
    // room that could have heard this.
    char_data bystander{};
    make_mortal_player(bystander);
    ScopedClearCharFields bystander_cleanup{bystander};
    RELEASE(bystander.player.name);
    CREATE(bystander.player.name, char, strlen("zapwitness") + 1);
    strcpy(bystander.player.name, "zapwitness");

    descriptor_data bystander_descriptor{};
    bystander_descriptor.output = bystander_descriptor.small_outbuf;
    bystander_descriptor.small_outbuf[0] = '\0';
    bystander_descriptor.bufptr = 0;
    bystander_descriptor.bufspace = SMALL_BUFSIZE - 1;
    bystander_descriptor.connected = CON_PLYNG;
    bystander_descriptor.character = &bystander;
    bystander_descriptor.next = nullptr;
    bystander.desc = &bystander_descriptor;
    bystander.specials.position = POSITION_STANDING;
    char_to_room(&bystander, kOwnerVnum);

    // THE LOAD WINDOW.
    set_location(&wearer, NOWHERE);
    stash_load_room_vnum(&wearer, kOwnerVnum);

    obj_data holy_symbol{};
    holy_symbol.obj_flags.type_flag = ITEM_ARMOR;
    holy_symbol.obj_flags.extra_flags = ITEM_ANTI_EVIL;
    holy_symbol.obj_flags.weight = 1;
    holy_symbol.in_room = NOWHERE; // LS1-ALLOW: obj-location (fixture init)
    holy_symbol.name = str_dup("symbol");
    holy_symbol.short_description = str_dup("a holy symbol");
    holy_symbol.description = str_dup("A holy symbol lies here.");

    equip_char(&wearer, &holy_symbol, WEAR_BODY);

    // Rider 8's own outcome is unchanged: the item is zapped into inventory.
    EXPECT_EQ(wearer.equipment[WEAR_BODY], nullptr);
    EXPECT_EQ(wearer.carrying, &holy_symbol);
    // The wearer is still told...
    EXPECT_NE(strstr(wearer_descriptor.small_outbuf, "You are zapped by"), nullptr)
        << "output: " << wearer_descriptor.small_outbuf;
    // ...and nobody else is. THIS is the delta rider row 8 did not name.
    EXPECT_STREQ(bystander_descriptor.small_outbuf, "")
        << "a load-window zap leaked its room broadcast into world[vnum]";

    if (holy_symbol.carried_by != nullptr)
        obj_from_char(&holy_symbol);
    if (wearer.equipment[WEAR_BODY] == &holy_symbol)
        unequip_char(&wearer, WEAR_BODY);
    RELEASE(holy_symbol.name);
    RELEASE(holy_symbol.short_description);
    RELEASE(holy_symbol.description);
    char_from_room(&bystander);
    RELEASE(wearer.player.name);
    RELEASE(bystander.player.name);
}

// THE CONTROL for the test above: the same zap, the same act() call, a wearer
// who IS in a room -- the broadcast lands. Without this, the silence asserted
// above would be satisfied by an equip_char() that never narrated anything.
TEST(LoadWindowBroadcast, ZapBroadcastReachesTheRoomForAPlacedWearer) {
    ScopedVnumWorld fixture_world;
    ScopedGlobalCharacterLists fixture_lists;

    char_data wearer{};
    make_mortal_player(wearer);
    ScopedClearCharFields wearer_cleanup{wearer};
    GET_ALIGNMENT(&wearer) = -1000; // IS_EVIL
    RELEASE(wearer.player.name);
    CREATE(wearer.player.name, char, strlen("zapwearer") + 1);
    strcpy(wearer.player.name, "zapwearer");

    descriptor_data wearer_descriptor{};
    wearer_descriptor.output = wearer_descriptor.small_outbuf;
    wearer_descriptor.small_outbuf[0] = '\0';
    wearer_descriptor.bufptr = 0;
    wearer_descriptor.bufspace = SMALL_BUFSIZE - 1;
    wearer_descriptor.connected = CON_PLYNG;
    wearer_descriptor.character = &wearer;
    wearer_descriptor.next = nullptr;
    wearer.desc = &wearer_descriptor;
    wearer.specials.position = POSITION_STANDING;

    char_data bystander{};
    make_mortal_player(bystander);
    ScopedClearCharFields bystander_cleanup{bystander};
    RELEASE(bystander.player.name);
    CREATE(bystander.player.name, char, strlen("zapwitness") + 1);
    strcpy(bystander.player.name, "zapwitness");

    descriptor_data bystander_descriptor{};
    bystander_descriptor.output = bystander_descriptor.small_outbuf;
    bystander_descriptor.small_outbuf[0] = '\0';
    bystander_descriptor.bufptr = 0;
    bystander_descriptor.bufspace = SMALL_BUFSIZE - 1;
    bystander_descriptor.connected = CON_PLYNG;
    bystander_descriptor.character = &bystander;
    bystander_descriptor.next = nullptr;
    bystander.desc = &bystander_descriptor;
    bystander.specials.position = POSITION_STANDING;

    char_to_room(&bystander, kOwnerRnum);
    char_to_room(&wearer, kOwnerRnum);

    obj_data holy_symbol{};
    holy_symbol.obj_flags.type_flag = ITEM_ARMOR;
    holy_symbol.obj_flags.extra_flags = ITEM_ANTI_EVIL;
    holy_symbol.obj_flags.weight = 1;
    holy_symbol.in_room = NOWHERE; // LS1-ALLOW: obj-location (fixture init)
    holy_symbol.name = str_dup("symbol");
    holy_symbol.short_description = str_dup("a holy symbol");
    holy_symbol.description = str_dup("A holy symbol lies here.");

    equip_char(&wearer, &holy_symbol, WEAR_BODY);

    EXPECT_EQ(wearer.equipment[WEAR_BODY], nullptr);
    EXPECT_NE(strstr(wearer_descriptor.small_outbuf, "You are zapped by"), nullptr)
        << "output: " << wearer_descriptor.small_outbuf;
    EXPECT_NE(strstr(bystander_descriptor.small_outbuf, "is zapped by"), nullptr)
        << "output: " << bystander_descriptor.small_outbuf;

    if (holy_symbol.carried_by != nullptr)
        obj_from_char(&holy_symbol);
    if (wearer.equipment[WEAR_BODY] == &holy_symbol)
        unequip_char(&wearer, WEAR_BODY);
    RELEASE(holy_symbol.name);
    RELEASE(holy_symbol.short_description);
    RELEASE(holy_symbol.description);
    char_from_room(&wearer);
    char_from_room(&bystander);
    RELEASE(wearer.player.name);
    RELEASE(bystander.player.name);
}

// B-1b, AND ITS REFUTATION. The review's claim: equip_char()'s poison arm can
// reach raw_kill() inside Crash_load, whereupon extract_char() -- taking its
// else arm now that the character is genuinely at NOWHERE -- destroys every
// worn item with extract_obj(), where the pre-split code obj_to_room()'d it.
//
// The tree says otherwise, and this test is the evidence. raw_kill() calls
// make_corpse() BEFORE extract_char(), and make_physical_corpse()
// unequip_char()s every worn slot into the corpse (fight.cpp) -- so by the
// time extract_char() runs, character->equipment[] is empty and its else arm
// has nothing to destroy. Worn gear is NOT lost on that path, before or after
// the split.
//
// What IS different, and what this test therefore pins as the honest delta,
// is the corpse's DESTINATION: make_physical_corpse ends with
// obj_to_room(corpse, location_of(character)). Pre-split that resolved the
// persisted VNUM as an index (an unrelated live room); post-split it is
// NOWHERE, which room_by_id_total() resolves to the room-0 fallback with its
// LEVEL_GOD mudlog. That is the same room-0-fallback class riders 1-2 already
// name -- not item destruction.
TEST(LoadWindowDeath, MakeCorpseEmptiesWornGearIntoTheCorpseBeforeExtractCharCanSeeIt) {
    ScopedVnumWorld fixture_world;
    ScopedGlobalCharacterLists fixture_lists;
    obj_data *const previous_object_list = object_list;

    char_data victim{};
    make_mortal_player(victim);
    ScopedClearCharFields victim_cleanup{victim};
    RELEASE(victim.player.name);
    CREATE(victim.player.name, char, strlen("poisonchr") + 1);
    strcpy(victim.player.name, "poisonchr");
    victim.specials.position = POSITION_STANDING;
    GET_GOLD(&victim) = 0; // move_gold() stays a no-op, so no money object is created

    descriptor_data descriptor{};
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = CON_PLYNG;
    descriptor.character = &victim;
    descriptor.next = nullptr;
    victim.desc = &descriptor;

    obj_data mail{};
    mail.obj_flags.type_flag = ITEM_ARMOR;
    mail.obj_flags.weight = 1;
    mail.in_room = NOWHERE; // LS1-ALLOW: obj-location (fixture init)
    mail.name = str_dup("mail");
    mail.short_description = str_dup("a suit of mail");
    mail.description = str_dup("A suit of mail lies here.");

    // THE LOAD WINDOW, with the item already worn (Crash_load's equip loop
    // has run; char_to_room has not).
    set_location(&victim, NOWHERE);
    stash_load_room_vnum(&victim, kOwnerVnum);
    equip_char(&victim, &mail, WEAR_BODY);
    ASSERT_EQ(victim.equipment[WEAR_BODY], &mail)
        << "the fixture item was zapped or refused; this test proves nothing";

    // raw_kill()'s own first act on the equipment.
    obj_data *corpse = make_corpse(&victim, nullptr, SPELL_POISON);
    ASSERT_NE(corpse, nullptr);

    // THE REFUTATION: extract_char()'s else arm cannot destroy what is no
    // longer there.
    for (int slot = 0; slot < MAX_WEAR; ++slot)
        EXPECT_EQ(victim.equipment[slot], nullptr) << "slot " << slot;
    EXPECT_EQ(mail.in_obj, corpse) // LS1-ALLOW: obj-location (the item's container, not a room)
        << "worn gear did not reach the corpse";

    // THE HONEST DELTA: the corpse lands in the room-0 fallback, because
    // location_of() is now truthfully NOWHERE. Pre-split it landed in
    // world[kOwnerVnum].
    EXPECT_EQ(room_by_id_total(NOWHERE), room_by_id_total(0));
    EXPECT_EQ(room_by_id_total(0)->contents, corpse);
    EXPECT_EQ(rots::entity::first_occupant(room_by_id_total(kOwnerVnum)), nullptr);

    // Teardown: take the stack item back out of the corpse, then unlink the
    // corpse from BOTH process globals it was pushed onto (world[].contents
    // and object_list) and release it -- it is heap storage
    // make_physical_corpse() CREATE()d, and its three strings are str_dup'd /
    // rots_asprintf'd. Same shape as characterization_combat_tests.cpp's own
    // corpse teardown.
    obj_from_obj(&mail);
    obj_from_room(corpse);
    if (object_list == corpse)
        object_list = corpse->next;
    RELEASE(corpse->name);
    RELEASE(corpse->short_description);
    RELEASE(corpse->description);
    RELEASE(mail.name);
    RELEASE(mail.short_description);
    RELEASE(mail.description);
    std::free(corpse);
    RELEASE(victim.player.name);
    object_list = previous_object_list;
}
