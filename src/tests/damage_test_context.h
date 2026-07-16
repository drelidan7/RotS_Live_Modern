#pragma once

// Shared fixture for tests that drive the live damage() path (fight.cpp).
// Lifted out of damage_tests.cpp (Task 8) so characterization tests can reuse
// the exact same attacker/victim/world bootstrap without duplicating it.

#include "../spells.h"
#include "../utils.h"
#include "rots/core/character.h"
#include "test_world.h"

extern room_data world;
extern int top_of_world;

namespace {

struct DamageTestContext {
    static constexpr int room_number = 1;

    // This context fights in room `room_number`, not ScopedTestWorld's
    // canonical room 0, so it relies on that index being a genuinely usable,
    // dummy_room_data()-initialized room. That holds for every allocation
    // path that can own the world here: ScopedTestWorld's create_bulk(1)
    // dummy-initializes indices [0, EXTENSION_SIZE - 1] outright (its
    // trailing-extension loop starts at amount - 1 == 0), and any world a
    // different suite allocated first covers at least that same trailing
    // range. The static_assert pins the reliance so a future room_number
    // bump can't silently walk past the guaranteed-initialized range.
    // (Historical context: an earlier revision re-ran dummy_room_data()
    // over rooms [0, room_number] here "just in case", but that clobbered
    // and leaked the name/description strings ScopedTestWorld had just
    // installed in room 0 on every construction — the exact leak class
    // this fixture exists to kill. Do not reintroduce it.)
    static_assert(room_number < EXTENSION_SIZE - 1,
        "room_number must stay inside the range create_bulk(1) dummy-initializes");

    // Owns/normalizes the shared process-wide test world (room 0) with the
    // same RAII discipline the other test-world clones now use (test_world.h);
    // constructed first (so BASE_WORLD exists before anything below runs) and
    // destructed last (after this class's own destructor body below has
    // released room `room_number`'s occupancy).
    ScopedTestWorld test_world;

    char_data attacker{};
    char_data victim{};
    affected_type victim_primary_affect{};
    affected_type victim_secondary_affect{};
    char attacker_name[16] = "test_attacker";
    char victim_name[16] = "test_victim";
    char_data* original_people = nullptr;

    DamageTestContext()
    {
        // ScopedTestWorld's constructor (above) just reset top_of_world to 0
        // (room 0 is all its own contract covers); raise it to cover the room
        // this context actually fights in. Plain assignment, not a
        // conditional grow: the old `if (top_of_world < room_number)` gate
        // was meaningless here since the member ctor made it always-true.
        top_of_world = room_number;
        original_people = world[room_number].people;

        attacker.specials2.act = MOB_ISNPC;
        victim.specials2.act = MOB_ISNPC;
        attacker.player.short_descr = attacker_name;
        victim.player.short_descr = victim_name;

        attacker.player.race = RACE_HUMAN;
        victim.player.race = RACE_HUMAN;
        attacker.player.level = 20;
        victim.player.level = 20;

        attacker.tmpabilities.con = 20;
        victim.tmpabilities.con = 20;
        attacker.abilities.hit = 500;
        victim.abilities.hit = 500;
        attacker.tmpabilities.hit = 500;
        victim.tmpabilities.hit = 500;
        attacker.tmpabilities.mana = 100;
        victim.tmpabilities.mana = 100;

        attacker.specials.position = POSITION_FIGHTING;
        victim.specials.position = POSITION_FIGHTING;
        attacker.specials.fighting = &victim;
        victim.specials.fighting = &attacker;

        attacker.in_room = room_number;
        victim.in_room = room_number;
        attacker.next_in_room = &victim;
        victim.next_in_room = nullptr;
        world[room_number].people = &attacker;
    }

    ~DamageTestContext()
    {
        world[room_number].people = original_people;
        attacker.next_in_room = nullptr;
        victim.next_in_room = nullptr;
        attacker.specials.fighting = nullptr;
        victim.specials.fighting = nullptr;
        attacker.in_room = NOWHERE;
        victim.in_room = NOWHERE;
    }

    void add_victim_affect(affected_type& affect, int type, int duration, int modifier = 0,
        int location = APPLY_NONE, long bitvector = 0)
    {
        affect = {};
        affect.type = type;
        affect.duration = duration;
        affect.modifier = modifier;
        affect.location = location;
        affect.bitvector = bitvector;
        affect.next = victim.affected;
        victim.affected = &affect;

        if (bitvector != 0) {
            victim.specials.affected_by |= bitvector;
        }
    }
};

} // namespace
