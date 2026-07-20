// big_brother_hooks_tests.cpp

// New test TU (combat-pilot wave, Task 2; brief
// .superpowers/sdd/pilot-task-2-brief.md, Step 4). Covers entity_hooks.h's
// two big_brother hook pairs added this task: dispatch_target_valid()
// (clerics.cpp's 2-arg / fight.cpp's 3-arg is_target_valid() calls) and
// dispatch_character_died() (fight.cpp's on_character_died() call).
// Consumer-free this wave -- no clerics.cpp/fight.cpp call site converts
// yet -- so both hooks are exercised directly, the same "recording stub
// proves the dispatch wrapper forwards every argument intact; unregistered
// hits the documented tripwire default" shape combat_hooks_tests.cpp's
// CombatHooksSpecial suite establishes for special()'s single-fn-ptr seam.
//
// HYGIENE: gtest_main.cpp registers big_brother.cpp's REAL forwarders
// (register_target_valid_hook()/register_character_died_hook()) for the
// whole test binary. Every test below that swaps in a recording stub (or
// nullptr) restores the real registration on scope exit via
// ScopedTargetValidHook/ScopedCharacterDiedHook, mirroring
// ScopedUnregisteredCombatCommand's/ScopedSpecialHandler's
// restore-via-real-registrar shape in combat_hooks_tests.cpp.

#include "../big_brother.h"
#include "../entity_hooks.h"
#include "rots/core/character.h"
#include "rots/core/object.h"

#include <gtest/gtest.h>

namespace {

// Swaps entity_hooks.h's target-valid hook, then restores the REAL
// big_brother.cpp forwarder via register_target_valid_hook() on
// destruction -- there is no per-hook "restore just this one" entry point,
// same rationale as combat_hooks_tests.cpp's Scoped* fixtures.
class ScopedTargetValidHook {
public:
    explicit ScopedTargetValidHook(rots::entity::target_valid_fn hook)
    {
        rots::entity::set_target_valid_hook(hook);
    }

    ~ScopedTargetValidHook() { register_target_valid_hook(); }

    ScopedTargetValidHook(const ScopedTargetValidHook&) = delete;
    ScopedTargetValidHook& operator=(const ScopedTargetValidHook&) = delete;
};

// Same shape as ScopedTargetValidHook above, for entity_hooks.h's
// character-died hook.
class ScopedCharacterDiedHook {
public:
    explicit ScopedCharacterDiedHook(rots::entity::character_died_fn hook)
    {
        rots::entity::set_character_died_hook(hook);
    }

    ~ScopedCharacterDiedHook() { register_character_died_hook(); }

    ScopedCharacterDiedHook(const ScopedCharacterDiedHook&) = delete;
    ScopedCharacterDiedHook& operator=(const ScopedCharacterDiedHook&) = delete;
};

struct RecordedTargetValidCall {
    char_data* attacker = nullptr;
    const char_data* victim = nullptr;
    int skill_id = 0;
    bool called = false;
};

RecordedTargetValidCall g_recorded_target_valid_call;

bool recording_target_valid_stub(char_data* attacker, const char_data* victim, int skill_id)
{
    g_recorded_target_valid_call = RecordedTargetValidCall { attacker, victim, skill_id, true };
    return false;
}

struct RecordedCharacterDiedCall {
    char_data* dead_man = nullptr;
    char_data* killer = nullptr;
    obj_data* corpse = nullptr;
    bool called = false;
};

RecordedCharacterDiedCall g_recorded_character_died_call;

void recording_character_died_stub(char_data* dead_man, char_data* killer, obj_data* corpse)
{
    g_recorded_character_died_call = RecordedCharacterDiedCall { dead_man, killer, corpse, true };
}

} // namespace

// dispatch_target_valid() -- DISCRIMINATOR: a recording stub proves the
// dispatch wrapper forwards attacker/victim/skill_id intact when a caller
// supplies all three (fight.cpp's 3-arg call shape).

TEST(TargetValidHook, DispatchTargetValidForwardsExplicitSkillIdToARegisteredStub)
{
    g_recorded_target_valid_call = RecordedTargetValidCall {};
    ScopedTargetValidHook scoped(recording_target_valid_stub);
    char_data attacker {};
    char_data victim {};

    const bool result = rots::entity::dispatch_target_valid(&attacker, &victim, 7);

    EXPECT_TRUE(g_recorded_target_valid_call.called)
        << "Expected the registered stub to have been reached.";
    EXPECT_EQ(g_recorded_target_valid_call.attacker, &attacker);
    EXPECT_EQ(g_recorded_target_valid_call.victim, &victim);
    EXPECT_EQ(g_recorded_target_valid_call.skill_id, 7);
    EXPECT_FALSE(result) << "Expected dispatch_target_valid() to forward the stub's return value.";
}

// DISCRIMINATOR: an omitted skill_id argument (clerics.cpp's 2-arg call
// shape) reaches the stub as entity_hooks.h's kNoSkillId sentinel -- proving
// dispatch_target_valid()'s own default mirrors the header's documented
// contract, the same "omitted-argument-defaults-correctly" proof
// combat_hooks_tests.cpp's CallSpecialOmittedInRoomArgumentDefaultsToNowhereLikeInterpreH99
// establishes for call_special()'s in_room default.

TEST(TargetValidHook, DispatchTargetValidOmittedSkillIdDefaultsToNoSkillIdSentinel)
{
    g_recorded_target_valid_call = RecordedTargetValidCall {};
    ScopedTargetValidHook scoped(recording_target_valid_stub);
    char_data attacker {};
    char_data victim {};

    rots::entity::dispatch_target_valid(&attacker, &victim);

    EXPECT_TRUE(g_recorded_target_valid_call.called);
    EXPECT_EQ(g_recorded_target_valid_call.skill_id, rots::entity::kNoSkillId)
        << "Expected the 2-arg-shaped call to reach the stub with entity_hooks.h's kNoSkillId "
           "sentinel, not an arbitrary default.";
}

TEST(TargetValidHook, DispatchTargetValidDefaultsToPermissiveTrueWhenUnregistered)
{
    ScopedTargetValidHook unregistered(nullptr);
    char_data attacker {};
    char_data victim {};

    const bool result = rots::entity::dispatch_target_valid(&attacker, &victim, 3);

    EXPECT_TRUE(result) << "Expected an unregistered target-valid hook to default to permissive "
                            "TRUE -- big_brother VETOES by returning false, so \"no big brother "
                            "installed\" must default to \"allow\".";
}

// dispatch_character_died() -- DISCRIMINATOR: a recording stub proves the
// dispatch wrapper forwards dead_man/killer/corpse intact.

TEST(CharacterDiedHook, DispatchCharacterDiedReachesARegisteredStubWithAllArgsIntact)
{
    g_recorded_character_died_call = RecordedCharacterDiedCall {};
    ScopedCharacterDiedHook scoped(recording_character_died_stub);
    char_data dead_man {};
    char_data killer {};
    obj_data corpse {};

    rots::entity::dispatch_character_died(&dead_man, &killer, &corpse);

    EXPECT_TRUE(g_recorded_character_died_call.called)
        << "Expected the registered stub to have been reached.";
    EXPECT_EQ(g_recorded_character_died_call.dead_man, &dead_man);
    EXPECT_EQ(g_recorded_character_died_call.killer, &killer);
    EXPECT_EQ(g_recorded_character_died_call.corpse, &corpse);
}

TEST(CharacterDiedHook, DispatchCharacterDiedDefaultsToANoOpWhenUnregistered)
{
    g_recorded_character_died_call = RecordedCharacterDiedCall {};
    ScopedCharacterDiedHook unregistered(nullptr);
    char_data dead_man {};
    char_data killer {};
    obj_data corpse {};

    rots::entity::dispatch_character_died(&dead_man, &killer, &corpse);

    EXPECT_FALSE(g_recorded_character_died_call.called)
        << "Expected an unregistered character-died hook to leave the (unrelated) stub's own "
           "recording flag untouched -- the real forwarder never ran, and the tripwire default is "
           "a logged no-op, not a call to any stub.";
}
