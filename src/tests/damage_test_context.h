#pragma once

// Shared fixture for tests that drive the live damage() path (fight.cpp).
// Lifted out of damage_tests.cpp (Task 8) so characterization tests can reuse
// the exact same attacker/victim/world bootstrap without duplicating it.

#include "../spells.h"
#include "../utils.h"

extern room_data world;
extern int top_of_world;

// Production room initializer from db.cpp: zeroes people/contents/
// ex_description/dir_option and resets number/zone/sector_type/room_flags/
// light. Not exposed in any header, so declared here for test bootstrap use.
void dummy_room_data(room_data* room);

namespace {

void ensure_test_world(int minimum_room_number)
{
    if (!room_data::BASE_WORLD) {
        world.create_bulk(minimum_room_number + 2);
        top_of_world = minimum_room_number + 1;

        // create_bulk() only dummy_room_data()-initializes the trailing
        // EXTENSION_SIZE rooms; the "real" rooms [0, amount-1) get just
        // room_data's constructor, which sets number/zone/level/name/
        // description/affected and leaves everything else — people,
        // contents, dir_option[], funct, bfs_* — as whatever heap garbage
        // `new room_data[...]` landed on. The real game never notices
        // because db_boot()'s load_rooms() fully populates every room
        // before anything runs; tests skip that bootstrap. The garbage is
        // silently harmless while the backing memory happens to read as
        // zero, but turns into order-dependent segfaults once earlier
        // tests' allocator churn dirties the heap first and some code path
        // walks a garbage pointer — seen while writing the Task 8
        // characterization golden as (a) death_cry()'s CAN_GO() loop
        // reading dir_option[], (b) obj_to_room() walking room contents
        // for the corpse, and (c) act(..., TO_ROOM) walking world[0].people
        // from interpre's reconnect path, whose fixture assumes room 0 of
        // a previously-allocated world is usable as-is. Zero every real
        // room once at allocation. Skip index amount-1: create_bulk()
        // already dummy'd it and stamped it EXTENSION_ROOM_HEAD, which a
        // re-run of dummy_room_data() would clobber.
        for (int room = 0; room < minimum_room_number + 1; ++room) {
            dummy_room_data(&world[room]);
            world[room].funct = nullptr;
            world[room].bfs_dir = 0;
            world[room].bfs_next = nullptr;
        }
    } else if (top_of_world < minimum_room_number) {
        top_of_world = minimum_room_number;
    }
}

struct DamageTestContext {
    static constexpr int room_number = 1;

    char_data attacker{};
    char_data victim{};
    affected_type victim_primary_affect{};
    affected_type victim_secondary_affect{};
    char attacker_name[16] = "test_attacker";
    char victim_name[16] = "test_victim";
    char_data* original_people = nullptr;

    DamageTestContext()
    {
        ensure_test_world(room_number);
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
