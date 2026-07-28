// render_cursor_tests.cpp

// LS-3b Wave T2 (ruling R-3b-B; .superpowers/sdd/ls3b-census-review.md's
// Table 1 / F15). TDD suite for rots::entity::ScopedRenderLocation, written
// BEFORE any production cursor site converts onto it (this commit is
// consumer-free). Covers: the save/spoof/restore round trip, retarget() for
// multi-write windows, restore()'s early-exit idempotence, LIFO nesting of
// two cursors on one character, and the F15 persistence-spoof contract --
// a read taken from inside the window (including one that would be
// persisted, e.g. into specials.was_in_room) observes the SPOOFED room, not
// the real one.
//
// No room/zone resolver fixture is needed anywhere in this file:
// ScopedRenderLocation is defined purely in terms of location_of()/
// set_location() (bare char_data::in_room field access, handler.h), which
// dispatch through no entity_hooks.h resolver -- see placement_tests.cpp's
// "set_location(ch, rnum) / is_in_room(ch, rnum)" section-header comment for
// the same point made about those two functions directly.

#include "../handler.h"
#include "rots/core/character.h"
#include "rots/entity/render_cursor.h"

#include <gtest/gtest.h>

#include <type_traits>

namespace {

using rots::entity::ScopedRenderLocation;

// Neither copyable nor movable (render_cursor.h's own contract comment: a
// copy or move has no sensible meaning for a single scoped write/restore
// pair). Compile-time proof, sabotage-checked by temporarily deleting one
// `= delete` at a time in the header and confirming this fails to compile,
// then reverting -- see the T2 report's probe log.
static_assert(!std::is_copy_constructible_v<ScopedRenderLocation>);
static_assert(!std::is_copy_assignable_v<ScopedRenderLocation>);
static_assert(!std::is_move_constructible_v<ScopedRenderLocation>);
static_assert(!std::is_move_assignable_v<ScopedRenderLocation>);

} // namespace

TEST(ScopedRenderLocationTest, ConstructorSpoofsLocationOfToTheGivenRoom)
{
    char_data character { };
    set_location(&character, 10);

    ScopedRenderLocation cursor(&character, 20);

    EXPECT_EQ(location_of(&character), 20);
}

TEST(ScopedRenderLocationTest, DestructorRestoresTheLocationCapturedAtConstruction)
{
    char_data character { };
    set_location(&character, 10);

    {
        ScopedRenderLocation cursor(&character, 20);
        EXPECT_EQ(location_of(&character), 20);
    }

    EXPECT_EQ(location_of(&character), 10);
}

TEST(ScopedRenderLocationTest, ReadInsideTheWindowObservesTheSpoofedRoomNotTheRealOne)
{
    // F15: a read taken from inside the window -- including one that would
    // be persisted (specials.was_in_room's real capture is
    // `victim->specials.was_in_room = location_of(victim);`, fight.cpp) --
    // sees the spoofed room. This is the wave's binding strict-equivalence
    // requirement, not a bug this class fixes.
    char_data character { };
    set_location(&character, 100);

    ScopedRenderLocation cursor(&character, 200);
    character.specials.was_in_room = location_of(&character);

    EXPECT_EQ(character.specials.was_in_room, 200);
}

TEST(ScopedRenderLocationTest, RetargetMovesTheCursorWithoutChangingTheRestoreTarget)
{
    char_data character { };
    set_location(&character, 1);

    ScopedRenderLocation cursor(&character, 2);
    EXPECT_EQ(location_of(&character), 2);

    cursor.retarget(3);
    EXPECT_EQ(location_of(&character), 3);

    cursor.retarget(4);
    EXPECT_EQ(location_of(&character), 4);

    cursor.restore();
    // Restores to the room captured at CONSTRUCTION (1), not either
    // intermediate retarget() target (2 or 3).
    EXPECT_EQ(location_of(&character), 1);
}

TEST(ScopedRenderLocationTest, ExplicitRestoreReturnsTheCharacterEarly)
{
    char_data character { };
    set_location(&character, 7);

    ScopedRenderLocation cursor(&character, 8);
    cursor.restore();

    EXPECT_EQ(location_of(&character), 7);
}

TEST(ScopedRenderLocationTest, DestructorAfterAnExplicitRestoreIsANoOp)
{
    // Idempotence (render_cursor.h's own contract): once restore() has run,
    // a later write to the character's location by anything else must
    // survive the cursor's destructor -- the destructor must NOT re-fire
    // and clobber it back to the originally captured room. This is the one
    // assertion a non-idempotent restore()/destructor pairing would fail.
    char_data character { };
    set_location(&character, 7);

    {
        ScopedRenderLocation cursor(&character, 8);
        cursor.restore();
        EXPECT_EQ(location_of(&character), 7);

        // Something else relocates the character after the cursor's own
        // restore() ran but before it goes out of scope.
        set_location(&character, 99);
    }

    // A non-idempotent destructor would have restored 7 here, clobbering
    // the intervening write to 99.
    EXPECT_EQ(location_of(&character), 99);
}

TEST(ScopedRenderLocationTest, ExplicitRestoreCalledTwiceIsANoOpOnTheSecondCall)
{
    char_data character { };
    set_location(&character, 3);

    ScopedRenderLocation cursor(&character, 4);
    cursor.restore();
    EXPECT_EQ(location_of(&character), 3);

    // Something else moves the character between the two restore() calls.
    set_location(&character, 42);
    cursor.restore();

    // The second restore() call must be a no-op -- it must not re-write the
    // room 3 captured at construction over the intervening write to 42.
    EXPECT_EQ(location_of(&character), 42);
}

TEST(ScopedRenderLocationTest, TwoCursorsOnOneCharacterUnwindLifo)
{
    char_data character { };
    set_location(&character, 1);

    ScopedRenderLocation outer(&character, 2);
    EXPECT_EQ(location_of(&character), 2);

    {
        ScopedRenderLocation inner(&character, 3);
        EXPECT_EQ(location_of(&character), 3);
    }
    // Inner unwound first, restoring the room captured at ITS construction
    // (2, the outer cursor's spoofed room) -- not room 1.
    EXPECT_EQ(location_of(&character), 2);

    outer.restore();
    EXPECT_EQ(location_of(&character), 1);
}
