// world_hooks_tests.cpp

// New test TU (l4-seed wave, Task 1; brief .superpowers/sdd/l4-task-1-brief.md
// Step 2b/Step 3/Step 5; census .superpowers/sdd/l4-census.md sections
// 3.3/3.5/3.6). Covers world_hooks.h's four hooks added this task:
// dispatch_do_wear() (zone.cpp's future do_wear() call site),
// dispatch_is_zone_populated() (zone.cpp's future is_empty() call site,
// semantic inverse), dispatch_equip_char() (zone.cpp's future equip_char()
// call site), and the dispatch_pkill_get_good_fame()/
// dispatch_pkill_get_evil_fame() pair (zone.cpp's future fame-lead gates).
// CONSUMER-FREE this task -- no zone.cpp call site converts yet, and
// zone.cpp itself is not yet a rots_world member -- so all four hooks are
// exercised directly against a recording stub, the same "registered stub
// receives args intact; unregistered default semantics asserted"
// discriminator shape this wave's script_hooks_tests.cpp establishes.
//
// HYGIENE: gtest_main.cpp registers act_obj2.cpp's/comm.cpp's/fight.cpp's/
// pkill.cpp's REAL forwarders (register_do_wear_hook()/
// register_is_zone_populated_hook()/register_equip_char_hook()/
// register_pkill_fame_hooks()) for the whole test binary. Every test below
// that swaps in a recording stub (or nullptr) restores the real
// registration on scope exit via its own Scoped* fixture, mirroring this
// tree's other *_hooks_tests.cpp Scoped* fixtures.

#include "../db.h"
#include "../world_hooks.h"
#include "rots/core/character.h"
#include "rots/core/object.h"

#include <gtest/gtest.h>

namespace {

// Swaps world_hooks.h's do-wear hook, then restores the REAL act_obj2.cpp
// forwarder via register_do_wear_hook() on destruction.
class ScopedDoWearHook {
public:
    explicit ScopedDoWearHook(rots::world::do_wear_fn hook) { rots::world::set_do_wear_hook(hook); }

    ~ScopedDoWearHook() { register_do_wear_hook(); }

    ScopedDoWearHook(const ScopedDoWearHook&) = delete;
    ScopedDoWearHook& operator=(const ScopedDoWearHook&) = delete;
};

class ScopedIsZonePopulatedHook {
public:
    explicit ScopedIsZonePopulatedHook(rots::world::is_zone_populated_fn hook)
    {
        rots::world::set_is_zone_populated_hook(hook);
    }

    ~ScopedIsZonePopulatedHook() { register_is_zone_populated_hook(); }

    ScopedIsZonePopulatedHook(const ScopedIsZonePopulatedHook&) = delete;
    ScopedIsZonePopulatedHook& operator=(const ScopedIsZonePopulatedHook&) = delete;
};

class ScopedEquipCharHook {
public:
    explicit ScopedEquipCharHook(rots::world::equip_char_fn hook)
    {
        rots::world::set_equip_char_hook(hook);
    }

    ~ScopedEquipCharHook() { register_equip_char_hook(); }

    ScopedEquipCharHook(const ScopedEquipCharHook&) = delete;
    ScopedEquipCharHook& operator=(const ScopedEquipCharHook&) = delete;
};

// Both fame hooks are registered/unregistered together via
// register_pkill_fame_hooks()/set_pkill_get_good_fame_hook()/
// set_pkill_get_evil_fame_hook() -- one Scoped fixture swaps both cells at
// once, matching the header's own "HOOK PAIR" framing.
class ScopedPkillFameHooks {
public:
    ScopedPkillFameHooks(rots::world::pkill_fame_query_fn good_hook,
        rots::world::pkill_fame_query_fn evil_hook)
    {
        rots::world::set_pkill_get_good_fame_hook(good_hook);
        rots::world::set_pkill_get_evil_fame_hook(evil_hook);
    }

    ~ScopedPkillFameHooks() { register_pkill_fame_hooks(); }

    ScopedPkillFameHooks(const ScopedPkillFameHooks&) = delete;
    ScopedPkillFameHooks& operator=(const ScopedPkillFameHooks&) = delete;
};

struct RecordedDoWearCall {
    char_data* mob = nullptr;
    bool called = false;
};

RecordedDoWearCall g_recorded_do_wear_call;

void recording_do_wear_stub(char_data* mob)
{
    g_recorded_do_wear_call = RecordedDoWearCall { mob, true };
}

struct RecordedIsZonePopulatedCall {
    int zone_nr = 0;
    bool called = false;
};

RecordedIsZonePopulatedCall g_recorded_is_zone_populated_call;

bool recording_is_zone_populated_stub(int zone_nr)
{
    g_recorded_is_zone_populated_call = RecordedIsZonePopulatedCall { zone_nr, true };
    return false;
}

struct RecordedEquipCharCall {
    char_data* character = nullptr;
    obj_data* item = nullptr;
    int item_slot = 0;
    bool called = false;
};

RecordedEquipCharCall g_recorded_equip_char_call;

void recording_equip_char_stub(char_data* character, obj_data* item, int item_slot)
{
    g_recorded_equip_char_call = RecordedEquipCharCall { character, item, item_slot, true };
}

bool g_good_fame_stub_called = false;
bool g_evil_fame_stub_called = false;

int recording_good_fame_stub()
{
    g_good_fame_stub_called = true;
    return 42;
}

int recording_evil_fame_stub()
{
    g_evil_fame_stub_called = true;
    return 17;
}

} // namespace

// dispatch_do_wear() -- DISCRIMINATOR: a recording stub proves the dispatch
// wrapper forwards its single char_data* argument intact.

TEST(DoWearHook, DispatchReachesARegisteredStubWithArgIntact)
{
    g_recorded_do_wear_call = RecordedDoWearCall { };
    ScopedDoWearHook scoped(recording_do_wear_stub);
    char_data mob { };

    rots::world::dispatch_do_wear(&mob);

    EXPECT_TRUE(g_recorded_do_wear_call.called)
        << "Expected the registered stub to have been reached.";
    EXPECT_EQ(g_recorded_do_wear_call.mob, &mob);
}

TEST(DoWearHook, DispatchDefaultsToALoggedNoOpWhenUnregistered)
{
    g_recorded_do_wear_call = RecordedDoWearCall { };
    ScopedDoWearHook unregistered(nullptr);
    char_data mob { };

    rots::world::dispatch_do_wear(&mob);

    EXPECT_FALSE(g_recorded_do_wear_call.called)
        << "Expected an unregistered do-wear hook to leave the (unrelated) stub's own recording "
           "flag untouched -- the real forwarder never ran, and the tripwire default is a logged "
           "no-op, not a call to any stub.";
}

// dispatch_is_zone_populated() -- DISCRIMINATOR: a recording stub proves the
// dispatch wrapper forwards zone_nr intact and returns the stub's value.

TEST(IsZonePopulatedHook, DispatchReachesARegisteredStubWithZoneNrIntact)
{
    g_recorded_is_zone_populated_call = RecordedIsZonePopulatedCall { };
    ScopedIsZonePopulatedHook scoped(recording_is_zone_populated_stub);

    const bool result = rots::world::dispatch_is_zone_populated(7);

    EXPECT_TRUE(g_recorded_is_zone_populated_call.called)
        << "Expected the registered stub to have been reached.";
    EXPECT_EQ(g_recorded_is_zone_populated_call.zone_nr, 7);
    EXPECT_FALSE(result) << "Expected dispatch_is_zone_populated() to forward the stub's own "
                            "return value.";
}

TEST(IsZonePopulatedHook, DispatchDefaultsToTruePopulatedWhenUnregistered)
{
    ScopedIsZonePopulatedHook unregistered(nullptr);

    const bool result = rots::world::dispatch_is_zone_populated(3);

    EXPECT_TRUE(result) << "Expected an unregistered is-zone-populated hook to default to TRUE "
                           "(\"assume populated\") -- the safe-sentinel choice that degrades to "
                           "\"never reset\" rather than risking a reset while players are "
                           "actually present.";
}

// dispatch_equip_char() -- DISCRIMINATOR: a recording stub proves the
// dispatch wrapper forwards character/item/item_slot intact.

TEST(EquipCharHook, DispatchReachesARegisteredStubWithAllArgsIntact)
{
    g_recorded_equip_char_call = RecordedEquipCharCall { };
    ScopedEquipCharHook scoped(recording_equip_char_stub);
    char_data character { };
    obj_data item { };

    rots::world::dispatch_equip_char(&character, &item, 3);

    EXPECT_TRUE(g_recorded_equip_char_call.called)
        << "Expected the registered stub to have been reached.";
    EXPECT_EQ(g_recorded_equip_char_call.character, &character);
    EXPECT_EQ(g_recorded_equip_char_call.item, &item);
    EXPECT_EQ(g_recorded_equip_char_call.item_slot, 3);
}

TEST(EquipCharHook, DispatchDefaultsToALoggedNoOpWhenUnregistered)
{
    g_recorded_equip_char_call = RecordedEquipCharCall { };
    ScopedEquipCharHook unregistered(nullptr);
    char_data character { };
    obj_data item { };

    rots::world::dispatch_equip_char(&character, &item, 3);

    EXPECT_FALSE(g_recorded_equip_char_call.called)
        << "Expected an unregistered equip-char hook to leave the (unrelated) stub's own "
           "recording flag untouched -- the real forwarder never ran, and the tripwire default "
           "is a logged no-op, not a call to any stub.";
}

// dispatch_pkill_get_good_fame()/dispatch_pkill_get_evil_fame() --
// DISCRIMINATOR: a recording stub pair proves both dispatch wrappers reach
// their own registered stub and forward its return value.

TEST(PkillFameHooks, DispatchReachesTheRegisteredStubsAndForwardsReturnValues)
{
    g_good_fame_stub_called = false;
    g_evil_fame_stub_called = false;
    ScopedPkillFameHooks scoped(recording_good_fame_stub, recording_evil_fame_stub);

    const int good_fame = rots::world::dispatch_pkill_get_good_fame();
    const int evil_fame = rots::world::dispatch_pkill_get_evil_fame();

    EXPECT_TRUE(g_good_fame_stub_called) << "Expected the registered good-fame stub to have been "
                                            "reached.";
    EXPECT_TRUE(g_evil_fame_stub_called) << "Expected the registered evil-fame stub to have been "
                                            "reached.";
    EXPECT_EQ(good_fame, 42);
    EXPECT_EQ(evil_fame, 17);
}

TEST(PkillFameHooks, DispatchDefaultsToZeroForBothSidesWhenUnregistered)
{
    ScopedPkillFameHooks unregistered(nullptr, nullptr);

    const int good_fame = rots::world::dispatch_pkill_get_good_fame();
    const int evil_fame = rots::world::dispatch_pkill_get_evil_fame();

    EXPECT_EQ(good_fame, 0) << "Expected an unregistered good-fame hook to default to 0 -- the "
                               "safe-sentinel choice that keeps zone.cpp's fame-lead comparisons "
                               "from spuriously favoring either side.";
    EXPECT_EQ(evil_fame, 0) << "Expected an unregistered evil-fame hook to default to 0, same "
                               "rationale as the good-fame default above.";
}
