// Coverage rider for objsave.cpp's gen_receptionist() find-walk (LS-2 Wave
// Task T3d; ls2-task-3d-report.md). The converted walk (objsave.cpp:1228,
// rots::entity::occupants(room_of(ch)) with an explicit `break` replacing
// the original `(!recep)` loop condition) had zero coverage anywhere in
// src/tests/ before this rider -- src/tests/objsave_json_tests.cpp only
// exercises the JSON codec helpers, a different translation unit's concern.

#include "../comm.h"
#include "../db.h"
#include "../handler.h"
#include "../interpre.h"
#include "../utils.h"
#include "rots/core/character.h"
#include "rots/core/descriptor.h"
#include "rots/core/types.h"
#include "test_world.h"

#include <gtest/gtest.h>

// gen_receptionist() has no header declaration anywhere in the tree (a
// SPECIAL()-shaped shared helper only called from within objsave.cpp
// itself, e.g. SPECIAL(receptionist)'s own `return gen_receptionist(ch,
// cmd, arg, RENT_FACTOR);`) -- forward-declared here with its real
// signature, mirroring spec_pro_tests.cpp's forward declarations of
// header-less SPECIAL()-family product helpers.
int gen_receptionist(struct char_data *ch, int cmd, char *arg, int mode);

// mob_index has no header declaration anywhere in the tree (an app-global
// db.cpp owns) -- forward-declared locally, mirroring
// interpre_special_tests.cpp's identical declaration. index_data's full
// definition comes from ../db.h (already included above).
extern struct index_data *mob_index;

// receptionist is the real registered SPECIAL() whose mob_index[].func
// pointer gen_receptionist's converted walk matches against
// (objsave.cpp:1229's `mob_index[tch->nr].func == receptionist`) --
// forward-declared with the SPECIAL() macro, mirroring
// act_wiz_format_tests.cpp's ScopedMobIndexEntry's own dummy stub except
// this one must be the REAL address the walk is looking for.
SPECIAL(receptionist);

namespace {

// Mirrors comm_act_tests.cpp's reset_capturing_descriptor() (duplicated,
// not shared -- that copy lives in a different TU's anonymous namespace).
void reset_capturing_descriptor(descriptor_data &descriptor, char_data *character) {
    descriptor.output = descriptor.small_outbuf;
    descriptor.small_outbuf[0] = '\0';
    descriptor.bufptr = 0;
    descriptor.bufspace = SMALL_BUFSIZE - 1;
    descriptor.connected = 0; // CON_PLYNG
    descriptor.character = character;
}

// A renter (ch) alone in room 0 of a fresh one-room test world. The
// found-case test chains a receptionist mob in itself; the not-found case
// deliberately leaves the room holding only ch.
struct GenReceptionistContext {
    ScopedTestWorld test_world;
    char_data ch{};
    descriptor_data ch_descriptor{};
    char_data *original_people = nullptr;

    GenReceptionistContext() {
        reset_capturing_descriptor(ch_descriptor, &ch);
        ch.desc = &ch_descriptor;

        original_people = test_world.room().people;
        ch.in_room = 0;
        ch.next_in_room = nullptr;
        test_world.room().people = &ch;
    }

    ~GenReceptionistContext() {
        test_world.room().people = original_people;
        ch.next_in_room = nullptr;
        ch.in_room = NOWHERE;
    }
};

// Swaps mob_index for a single fabricated entry pointing at the REAL
// receptionist() SPECIAL -- mirrors act_wiz_format_tests.cpp's
// ScopedMobIndexEntry (restores the prior table on scope exit).
class ScopedReceptionistMobIndex {
  public:
    ScopedReceptionistMobIndex() : previous_(mob_index) {
        mob_index = new index_data[1]{};
        mob_index[0].virt = 3060; // arbitrary, matches no real vnum in this fixture
        mob_index[0].number = 1;
        mob_index[0].func = &receptionist;
    }

    ~ScopedReceptionistMobIndex() {
        delete[] mob_index;
        mob_index = previous_;
    }

    ScopedReceptionistMobIndex(const ScopedReceptionistMobIndex &) = delete;
    ScopedReceptionistMobIndex &operator=(const ScopedReceptionistMobIndex &) = delete;

  private:
    index_data *previous_;
};

} // namespace

// (a) FOUND: a mob whose mob_index[].func == receptionist shares the room.
// recep is left at its default value-initialized position (POSITION_DEAD,
// well below POSITION_SLEEPING), so the found path's very first CMD_RENT
// guard after the walk (`if (!AWAKE(recep))`, objsave.cpp:1243) fires --
// the cheapest deterministic branch that only a genuinely FOUND recep can
// reach (the not-found path never gets past the walk to this guard at
// all).
TEST(GenReceptionist, FindsTheRegisteredReceptionistMobAndProceedsPastTheWalk) {
    GenReceptionistContext context;
    ScopedReceptionistMobIndex mob_index_scope;

    char_data recep{};
    recep.in_room = 0;
    recep.specials2.act = MOB_ISNPC;
    recep.nr = 0; // indexes mob_index_scope's single fabricated entry
    context.ch.next_in_room = &recep;
    recep.next_in_room = nullptr;

    const int result = gen_receptionist(&context.ch, CMD_RENT, mutable_arg(""), 0);

    EXPECT_STREQ(context.ch_descriptor.output, "She is unable to talk to you...\n\r")
        << "Expected the converted occupants(room_of(ch)) walk to find recep and reach the "
           "!AWAKE(recep) guard immediately past it.";
    EXPECT_EQ(result, TRUE);

    recep.in_room = NOWHERE;
}

// (b) NOT FOUND: the room holds only ch -- no mob at all, let alone one
// whose mob_index[].func matches. The walk must exit with recep still
// nullptr (its own `struct char_data* recep = 0;` initializer, untouched
// by this conversion -- gen_receptionist's walk is NOT Family F, unlike
// do_rescue's, since recep was already pre-initialized before LS-2), so
// the `if (!recep)` branch fires: the literal SYSERR log (unobservable via
// send_to_char) and a FALSE return, with nothing sent to ch.
TEST(GenReceptionist, ReportsFailureWhenNoReceptionistMobIsInTheRoom) {
    GenReceptionistContext context;

    const int result = gen_receptionist(&context.ch, CMD_RENT, mutable_arg(""), 0);

    EXPECT_STREQ(context.ch_descriptor.output, "")
        << "Expected no message to ch on the not-found path (the SYSERR notice goes to log(), "
           "not send_to_char()).";
    EXPECT_EQ(result, FALSE);
}
